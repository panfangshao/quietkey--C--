// 第 1 层：Windows 全局媒体传输控制（GSMTC）。
//
// 这是「一次都不切窗口」的正解。浏览器在播放视频时都会向系统注册一个媒体会话——
// 就是按音量键时弹出的那个媒体浮层背后的东西。我们直接枚举会话并调用
// TryTogglePlayPauseAsync，全程：
//
//   * 不需要焦点，不切窗口，不动前台
//   * 不需要窗口标题匹配（标题会随播放进度变，本来就不可靠）
//   * 窗口最小化、被完全遮挡都照样生效
//
// 注意：向浏览器顶层 HWND 投递合成的 WM_KEYDOWN 是**行不通**的，
// Chromium 的输入走自己的 renderer 管线会直接把它丢掉。所以对网页视频来说，
// 这一层不是「优化」，而是唯一可行的无感方案。**不要以「精简」为由删掉它。**

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>  // 遍历 IVectorView 要它
#include <winrt/Windows.Media.Control.h>

#include <algorithm>
#include <cwctype>

#include "log.h"
#include "recency.h"
#include "strategy.h"

using namespace winrt;
using namespace winrt::Windows::Media::Control;

namespace {

using Session = GlobalSystemMediaTransportControlsSession;
using SessionManager = GlobalSystemMediaTransportControlsSessionManager;
using PlaybackStatus = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

/// 枚举到的一个会话。
struct Found {
    Session session{nullptr};
    std::wstring aumid;   // 小写
    bool playing = false;
    /// TimelineProperties.LastUpdatedTime，取不到时为 0。
    /// 它比播放状态更能反映"这个会话被碰过"：手动播了又停这种**净状态零变化**
    /// 的操作，状态比对完全看不见，但它会被刷新。
    int64_t touchedAt = 0;
};

std::wstring ToLower(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return s;
}

/// "chrome.exe" -> "chrome"，用于和 AppUserModelId 做模糊比对。
std::wstring ExeStem(const std::wstring& exe) {
    std::wstring s = ToLower(exe);
    const size_t dot = s.rfind(L".exe");
    if (dot != std::wstring::npos && dot + 4 == s.size()) {
        s.erase(dot);
    }
    return s;
}

std::vector<Found> Collect(const SessionManager& manager) {
    std::vector<Found> all;
    for (const Session& s : manager.GetSessions()) {
        Found f;
        f.session = s;
        try {
            f.aumid = ToLower(std::wstring(s.SourceAppUserModelId().c_str()));
        } catch (const hresult_error&) {
        }
        try {
            f.playing = s.GetPlaybackInfo().PlaybackStatus() == PlaybackStatus::Playing;
        } catch (const hresult_error&) {
        }
        try {
            f.touchedAt = s.GetTimelineProperties().LastUpdatedTime().time_since_epoch().count();
        } catch (const hresult_error&) {
        }
        all.push_back(std::move(f));
    }
    return all;
}

std::vector<SeenSession> SeenRows(const std::vector<Found>& all) {
    // **不做任何过滤**：下标要和 all 一一对应，MostRecentIndex 返回的下标
    // 才能直接拿来索引 all。
    std::vector<SeenSession> rows;
    rows.reserve(all.size());
    for (const Found& f : all) {
        rows.push_back(SeenSession{f.aumid, f.touchedAt});
    }
    return rows;
}

/// 有且只有一个正在播放的会话时返回它的下标。
int OnlyPlaying(const std::vector<Found>& all) {
    int found = -1;
    for (size_t i = 0; i < all.size(); ++i) {
        if (!all[i].playing) continue;
        if (found >= 0) return -1;  // 不止一个
        found = static_cast<int>(i);
    }
    return found;
}

std::wstring ShortError(const hresult_error& e) {
    std::wstring msg(e.message().c_str());
    if (msg.empty()) {
        wchar_t buf[32];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"HRESULT 0x%08X",
                     static_cast<unsigned>(e.code().value));
        return buf;
    }
    return msg;
}

}  // namespace

std::vector<SessionInfo> ListMediaSessions() {
    std::vector<SessionInfo> out;
    try {
        const SessionManager manager = SessionManager::RequestAsync().get();
        const std::vector<Found> all = Collect(manager);
        const std::vector<SeenSession> rows = SeenRows(all);
        const int recent = MostRecentIndex(rows);

        int64_t newest = 0;
        for (const Found& f : all) {
            newest = (std::max)(newest, f.touchedAt);
        }
        for (size_t i = 0; i < all.size(); ++i) {
            SessionInfo info;
            info.aumid = all[i].aumid;
            info.playing = all[i].playing;
            info.recent = (static_cast<int>(i) == recent);
            info.hasTimeline = all[i].touchedAt > 0;
            info.behindSecs = BehindSeconds(all[i].touchedAt, newest);
            out.push_back(std::move(info));
        }
    } catch (const hresult_error& e) {
        LogF(L"枚举媒体会话失败: %s", ShortError(e).c_str());
    }
    return out;
}

/// 目标会话的挑选顺序（第一个命中即用）：
///   1. 配置匹配   —— target.exe 填了进程名就永远听配置的
///   2. 最近活跃   —— 见 recency.h，followRecent 控制
///   3. 唯一在播放 —— 只开了一个视频的日常场景
///   4. 系统当前会话（GetCurrentSession）
///   5. 唯一会话
///
/// 第 4、5 条是「能暂停、不能恢复」的解药：视频一旦被我们暂停，
/// 正在播放的会话数就变成 0，只靠第 3 条会彻底找不回目标，
/// 于是掉到对浏览器无效的第 2/3 层，表现为再按一次没反应。
Outcome GsmtcToggle(const TargetSpec& spec, const ResolvedTarget* resolved,
                    bool followRecent, std::wstring* activity, bool* sawSessions) {
    if (sawSessions) {
        *sawSessions = false;
    }
    try {
        const SessionManager manager = SessionManager::RequestAsync().get();
        const std::vector<Found> all = Collect(manager);
        if (all.empty()) {
            return Outcome::Skip(L"系统当前没有任何媒体会话");
        }
        if (sawSessions) {
            *sawSessions = true;
        }

        const std::vector<SeenSession> rows = SeenRows(all);
        if (activity) {
            *activity = DescribeActivity(rows);
        }

        // 配置里的进程名，以及实际解析到的窗口的进程名，都拿来做匹配依据。
        std::vector<std::wstring> wanted;
        if (!spec.exe.empty()) {
            wanted.push_back(ExeStem(spec.exe));
        }
        if (resolved && !resolved->exe.empty()) {
            wanted.push_back(ExeStem(resolved->exe));
        }

        int idx = -1;
        const wchar_t* route = L"";

        for (size_t i = 0; i < all.size() && idx < 0; ++i) {
            for (const std::wstring& w : wanted) {
                if (w.empty() || all[i].aumid.empty()) continue;
                if (all[i].aumid.find(w) != std::wstring::npos ||
                    w.find(all[i].aumid) != std::wstring::npos) {
                    idx = static_cast<int>(i);
                    route = L"配置匹配";
                    break;
                }
            }
        }
        if (idx < 0 && followRecent) {
            // 按会话逐个比时间戳，不按进程名归并——所以一个浏览器开多个视频标签页、
            // 各自注册一个会话时，也能精确挑到你最近碰过的那一个。
            idx = MostRecentIndex(rows);
            if (idx >= 0) route = L"最近活跃";
        }
        if (idx < 0) {
            idx = OnlyPlaying(all);
            if (idx >= 0) route = L"唯一在播放";
        }
        if (idx < 0) {
            std::wstring current;
            try {
                const Session cur = manager.GetCurrentSession();
                if (cur) {
                    current = ToLower(std::wstring(cur.SourceAppUserModelId().c_str()));
                }
            } catch (const hresult_error&) {
            }
            if (!current.empty()) {
                for (size_t i = 0; i < all.size(); ++i) {
                    if (all[i].aumid == current) {
                        idx = static_cast<int>(i);
                        route = L"系统当前会话";
                        break;
                    }
                }
            }
        }
        if (idx < 0 && all.size() == 1) {
            idx = 0;
            route = L"唯一会话";
        }

        if (idx < 0) {
            std::wstring names;
            for (const Found& f : all) {
                if (!names.empty()) names += L", ";
                names += f.aumid;
            }
            return Outcome::Skip(std::to_wstring(all.size()) + L" 个会话中没有匹配项（" + names + L"）");
        }

        const Found& target = all[idx];
        const wchar_t* direction = target.playing ? L"已暂停" : L"已播放";

        bool toggleEnabled = true;
        try {
            toggleEnabled = target.session.GetPlaybackInfo().Controls().IsPlayPauseToggleEnabled();
        } catch (const hresult_error&) {
        }

        // 我们自己造成的这次切换同样会刷新会话的时间线时间戳，
        // 所以"暂停之后再按一次恢复"天然落在同一个会话上，不需要额外记账。
        if (toggleEnabled) {
            if (target.session.TryTogglePlayPauseAsync().get()) {
                return Outcome::Applied(target.aumid + L" → " + direction + L"（" + route + L"）");
            }
        }

        // Toggle 被声明为不可用、或调用后被拒绝时，按当前状态退到明确的 Play / Pause——
        // 有些播放器只在暂停态下开放 Play，Toggle 一直报 false，
        // 不退这一步就永远恢复不了。
        const bool ok = target.playing ? target.session.TryPauseAsync().get()
                                       : target.session.TryPlayAsync().get();
        if (ok) {
            return Outcome::Applied(target.aumid + L" → " + direction + L"（" + route + L"·显式）");
        }
        return Outcome::Failed(L"会话 " + target.aumid + L" 拒绝了播放/暂停请求");
    } catch (const hresult_error& e) {
        return Outcome::Skip(L"媒体会话不可用: " + ShortError(e));
    }
}
