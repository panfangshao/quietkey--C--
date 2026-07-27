#include "log.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>  // _countof
#include <cwchar>
#include <string>

namespace {

bool g_toConsole = false;
bool g_consoleReady = false;

// 控制台输出统一走这里：WriteConsoleW 而不是 wprintf，
// 免得受 C 运行时的区域设置/字符模式影响，中文更稳。
// 不能叫 WriteConsole：定义了 UNICODE 之后那是个宏，会被展开成 WriteConsoleW，
// 于是自己把 Win32 的同名函数遮住，报"函数不接受 5 个参数"。
void WriteToConsole(const wchar_t* text) {
    if (!g_consoleReady) {
        return;
    }
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        return;
    }

    // 两条路必须都走通：
    //   * 句柄是真控制台 → WriteConsoleW，宽字符直出，中文最稳；
    //   * 句柄被重定向到管道/文件（`quietkey --list-sessions > a.txt` 或被
    //     PowerShell 捕获）→ WriteConsoleW 会**静默失败**，得转成 UTF-8 走 WriteFile。
    //     只写前者的话，重定向时输出凭空消失，排查工具反而成了坑。
    DWORD mode = 0;
    const DWORD len = static_cast<DWORD>(wcslen(text));
    DWORD written = 0;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, text, len, &written, nullptr);
        return;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(len),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return;
    }
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(len), &utf8[0], bytes, nullptr, nullptr);
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

int FormatV(wchar_t* buf, size_t cap, const wchar_t* fmt, va_list args) {
    const int n = _vsnwprintf_s(buf, cap, _TRUNCATE, fmt, args);
    return n < 0 ? static_cast<int>(cap - 1) : n;
}

}  // namespace

void AttachToConsole() {
    if (g_consoleReady) {
        return;
    }

    // **已经有输出目标就绝对不要动它。** 从终端启动时（不管有没有 `> a.txt`
    // 或被 PowerShell 捕获）std 句柄是继承来的，直接往上写即可。
    // 早先这里无条件 freopen("CONOUT$")，等于把用户的重定向抢回控制台，
    // 结果 `--list-sessions > 文件` 写出来的是空文件——排查工具自己成了坑。
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        g_consoleReady = true;
        return;
    }

    // 双击启动：没有任何输出目标，挂父控制台，挂不上就自己开一个。
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) {
        return;
    }
    h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return;
        }
        SetStdHandle(STD_OUTPUT_HANDLE, h);
    }
    g_consoleReady = true;
}

void SetLogToConsole(bool enabled) {
    g_toConsole = enabled;
    if (enabled) {
        AttachToConsole();
    }
}

void LogF(const wchar_t* fmt, ...) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t body[1024];
    va_list args;
    va_start(args, fmt);
    FormatV(body, _countof(body), fmt, args);
    va_end(args);

    wchar_t line[1152];
    _snwprintf_s(line, _countof(line), _TRUNCATE, L"[%02d:%02d:%02d] %s\n",
                 st.wHour, st.wMinute, st.wSecond, body);

    OutputDebugStringW(line);
    if (g_toConsole) {
        WriteToConsole(line);
    }
}

void PrintF(const wchar_t* fmt, ...) {
    wchar_t body[2048];
    va_list args;
    va_start(args, fmt);
    FormatV(body, _countof(body), fmt, args);
    va_end(args);

    AttachToConsole();
    WriteToConsole(body);
    OutputDebugStringW(body);
}
