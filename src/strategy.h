// 播放/暂停的四层策略链，按「对用户的打扰程度」从小到大排列。
//
//   1. gsmtc      Windows 媒体会话 —— 浏览器网页视频主力，完全无感
//   2. messages   WM_APPCOMMAND 定向投递 —— VLC / PotPlayer 等，完全无感
//   3. messages   PostMessage 键盘消息 —— 原生 Win32 播放器，完全无感
//   4. fallback   抢焦点 + SendInput + 还原 —— **会闪一下**，最后的兜底
//
// 逐层尝试，第一个成功的即返回，并把每层的结果记下来给界面显示，
// 让用户一眼看出命中的是哪一层、有没有掉到会闪烁的兜底路径。
#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "winutil.h"

enum class OutcomeKind {
    Applied,        // 这一层成功接管了本次操作
    NotApplicable,  // 不适用于当前目标（不是失败，继续往下走）
    Failed,         // 适用但执行失败
};

struct Outcome {
    OutcomeKind kind = OutcomeKind::NotApplicable;
    std::wstring detail;

    static Outcome Applied(std::wstring d) { return {OutcomeKind::Applied, std::move(d)}; }
    static Outcome Skip(std::wstring d) { return {OutcomeKind::NotApplicable, std::move(d)}; }
    static Outcome Failed(std::wstring d) { return {OutcomeKind::Failed, std::move(d)}; }

    const wchar_t* Label() const {
        switch (kind) {
            case OutcomeKind::Applied: return L"命中";
            case OutcomeKind::Failed: return L"失败";
            default: return L"跳过";
        }
    }
};

struct TraceEntry {
    std::wstring layer;
    std::wstring label;
    std::wstring detail;
};

/// 单次触发的完整报告。
struct ActionReport {
    bool ok = false;
    std::wstring winner;   // 最终命中的层名，空表示没命中
    std::wstring detail;
    double elapsedMs = 0.0;
    std::vector<TraceEntry> trace;
    bool intrusive = false;  // 命中的这一层是否切了焦点（即掉到了兜底层）

    std::wstring Summary() const;
};

/// 执行一次「播放/暂停」切换，逐层降级。
ActionReport TogglePlayPause(const Config& cfg);

// ---- 各层的实现，供调度器和诊断调用 ----

/// 第 1 层。
/// `activity` 填入「谁最近被碰过」的摘要，供日志和排查用；
/// `sawSessions` 填入系统当前有没有媒体会话——它决定了失败时该告诉用户什么，
/// "一个会话都没有"和"有会话但没匹配上"是完全不同的两个问题。
Outcome GsmtcToggle(const TargetSpec& spec, const ResolvedTarget* resolved,
                    bool followRecent, std::wstring* activity, bool* sawSessions);

/// 第 2 层：把多媒体键的「播放/暂停」定向投给目标窗口。
Outcome AppCommandPlayPause(const ResolvedTarget& target);

/// 第 3 层：给目标窗口投递空格键消息。
Outcome PostSpace(const ResolvedTarget& target);

/// 第 4 层：抢焦点 → SendInput → 立刻还原。
Outcome FocusAndSendSpace(const ResolvedTarget& target, unsigned settleMs);

/// 诊断面板/命令行用：列出系统当前的媒体会话。
struct SessionInfo {
    std::wstring aumid;
    bool playing = false;
    bool recent = false;       // 是否是「最近被碰过」的那一个
    float behindSecs = 0.0f;   // 比最近的那个早多少秒
    bool hasTimeline = false;  // 该播放器是否上报时间线
};
std::vector<SessionInfo> ListMediaSessions();
