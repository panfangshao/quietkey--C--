// 设置对话框（原生 Win32 对话框资源，见 app.rc 的 IDD_SETTINGS）。
#pragma once

#include <windows.h>

#include <string>

#include "config.h"
#include "strategy.h"

/// 由 main.cpp 实现：顶部状态行的文字（热键是否注册、累计触发次数）。
/// 放在这里是为了让对话框不必反向依赖 main 的内部状态。
std::wstring AppStatusLine();

/// 打开设置窗口（非模态，已打开则前置）。
///
/// `cfg` 指向主程序持有的配置；用户点「保存并应用」时会写回它，
/// 并通过 `onApply` 通知主程序重挂热键、落盘。
void ShowSettingsDialog(HINSTANCE instance, HWND owner, Config* cfg,
                        void (*onApply)(const Config&));

/// 主消息循环要把消息先交给它——非模态对话框的 Tab 切换、回车、
/// 快捷键都靠 IsDialogMessage 处理，不转交的话键盘在对话框里是废的。
bool SettingsHandleMessage(MSG* msg);

/// 有新的触发结果时刷新「上次触发」区域。窗口没开就什么都不做。
void SettingsRefreshLastReport(const ActionReport& report);

/// 热键状态变化后刷新顶部状态行。
void SettingsRefreshStatus();
