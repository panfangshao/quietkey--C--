// 日志与控制台。
//
// 程序是 /SUBSYSTEM:WINDOWS（双击不弹黑框），所以默认**没有控制台**。
// 但命令行诊断模式必须能把结果打回你启动它的那个终端里——
// 靠 AttachConsole(ATTACH_PARENT_PROCESS) 挂到父进程的控制台上。
//
// 日志同时写 OutputDebugStringW，用 DebugView 之类的工具随时能看，
// 不需要重新编译。
#pragma once

#include <windows.h>

/// 挂到父进程的控制台（从 PowerShell/cmd 启动时）；父进程没有控制台就自己开一个。
/// 命令行模式和 --console 都靠它。重复调用无害。
void AttachToConsole();

/// 之后的日志是否也打到控制台。默认只写 OutputDebugString。
void SetLogToConsole(bool enabled);

/// 写一条日志（自动补时间戳和换行）。
void LogF(const wchar_t* fmt, ...);

/// 直接输出给用户看的一行（命令行模式用），不带时间戳前缀。
void PrintF(const wchar_t* fmt, ...);
