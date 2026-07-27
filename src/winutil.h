// Win32 窗口相关的封装：枚举、按规则解析目标、抢前台、发按键。
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "config.h"

struct WindowInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring title;
    std::wstring className;
    std::wstring exe;   // 进程可执行文件名，如 "chrome.exe"
};

struct ResolvedTarget {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring exe;
};

/// 列出可见的顶层窗口，供「窗口拾取器」展示。
///
/// 过滤掉 owner 窗口、WS_EX_TOOLWINDOW、以及 **DWM cloaked** 窗口。
/// cloaked 检查是必需的：去掉之后列表里会塞满看不见的 UWP 幽灵窗口。
std::vector<WindowInfo> ListWindows();

/// 按匹配规则实时解析出当前的目标窗口。
///
/// 多个命中时取**标题最长**的——浏览器真正在播视频的标签页，
/// 标题总比「新标签页」长。
bool ResolveTarget(const TargetSpec& spec, ResolvedTarget* out);

HWND GetForegroundHwnd();
std::wstring GetWindowTitleText(HWND hwnd);

/// 把窗口抢到前台。
///
/// 后台进程裸调 SetForegroundWindow 会被 Windows 的前台锁定机制拒绝，
/// 必须先 AttachThreadInput 把自己"挂"到当前前台线程的输入队列上。
bool ForceForeground(HWND hwnd);

/// 用 SendInput 发一个按键（按下 + 抬起）。作用于当前前台窗口。
bool SendKeyToForeground(UINT vk);
