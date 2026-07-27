#include "winutil.h"

#include <dwmapi.h>
#include <psapi.h>

#include <algorithm>
#include <cwctype>

namespace {

std::wstring ToLower(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return s;
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

std::wstring ExeNameOf(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return std::wstring();
    }
    wchar_t path[MAX_PATH] = {};
    DWORD len = _countof(path);
    std::wstring name;
    if (QueryFullProcessImageNameW(h, 0, path, &len)) {
        const std::wstring full(path, len);
        const size_t slash = full.find_last_of(L'\\');
        name = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
    }
    CloseHandle(h);
    return name;
}

/// DWM 认为这个窗口是"隐形"的吗（UWP 挂起、虚拟桌面切走等）。
bool IsCloaked(HWND hwnd) {
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != FALSE;
    }
    return false;
}

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<WindowInfo>*>(lparam);

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;   // 对话框之类的附属窗口
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (IsCloaked(hwnd)) return TRUE;

    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, _countof(title));
    if (title[0] == L'\0') return TRUE;  // 无标题的多半不是用户能看见的窗口

    WindowInfo info;
    info.hwnd = hwnd;
    info.title = title;

    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, _countof(cls));
    info.className = cls;

    GetWindowThreadProcessId(hwnd, &info.pid);
    info.exe = ExeNameOf(info.pid);

    out->push_back(std::move(info));
    return TRUE;
}

}  // namespace

std::vector<WindowInfo> ListWindows() {
    std::vector<WindowInfo> out;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&out));
    return out;
}

bool ResolveTarget(const TargetSpec& spec, ResolvedTarget* out) {
    if (!out || spec.Empty()) {
        return false;
    }

    const std::vector<WindowInfo> all = ListWindows();
    const WindowInfo* best = nullptr;
    for (const WindowInfo& w : all) {
        if (!spec.exe.empty() && ToLower(w.exe) != ToLower(spec.exe)) continue;
        if (!spec.className.empty() && w.className != spec.className) continue;
        if (!spec.titleContains.empty() && !ContainsNoCase(w.title, spec.titleContains)) continue;
        if (!best || w.title.size() > best->title.size()) {
            best = &w;
        }
    }
    if (!best) {
        return false;
    }
    out->hwnd = best->hwnd;
    out->title = best->title;
    out->exe = best->exe;
    return true;
}

HWND GetForegroundHwnd() {
    return GetForegroundWindow();
}

std::wstring GetWindowTitleText(HWND hwnd) {
    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, _countof(buf));
    return buf;
}

bool ForceForeground(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    if (SetForegroundWindow(hwnd)) {
        return true;
    }

    // 被前台锁定机制拒绝了：把自己挂到当前前台线程的输入队列上再试。
    const HWND fg = GetForegroundWindow();
    const DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
    const DWORD myThread = GetCurrentThreadId();
    bool ok = false;
    if (fgThread != 0 && fgThread != myThread && AttachThreadInput(myThread, fgThread, TRUE)) {
        ok = SetForegroundWindow(hwnd) != FALSE;
        BringWindowToTop(hwnd);
        AttachThreadInput(myThread, fgThread, FALSE);
    }
    return ok || GetForegroundWindow() == hwnd;
}

bool SendKeyToForeground(UINT vk) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = static_cast<WORD>(vk);
    in[0].ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, in, sizeof(INPUT)) == 2;
}
