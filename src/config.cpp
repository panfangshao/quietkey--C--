#include "config.h"

#include <windows.h>
#include <shlobj.h>

#include "log.h"

namespace {

constexpr wchar_t kSection[] = L"quietkey";

std::wstring ReadString(const wchar_t* key, const std::wstring& fallback, const std::wstring& path) {
    wchar_t buf[512];
    GetPrivateProfileStringW(kSection, key, fallback.c_str(), buf, _countof(buf), path.c_str());
    return buf;
}

bool ReadBool(const wchar_t* key, bool fallback, const std::wstring& path) {
    return GetPrivateProfileIntW(kSection, key, fallback ? 1 : 0, path.c_str()) != 0;
}

bool WriteString(const wchar_t* key, const std::wstring& value, const std::wstring& path) {
    return WritePrivateProfileStringW(kSection, key, value.c_str(), path.c_str()) != FALSE;
}

bool WriteBool(const wchar_t* key, bool value, const std::wstring& path) {
    return WriteString(key, value ? L"1" : L"0", path);
}

/// 让 INI 以 UTF-16 存储。
///
/// WritePrivateProfileStringW 只有在文件**已经带 UTF-16 BOM** 时才按宽字符写，
/// 否则它会按当前 ANSI 代码页转换——窗口标题里的中文、日文就此变成问号。
/// 所以文件不存在时先手工写一个 BOM 出来。
void EnsureUnicodeIni(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    const unsigned char bom[2] = {0xFF, 0xFE};
    DWORD written = 0;
    WriteFile(h, bom, sizeof(bom), &written, nullptr);
    CloseHandle(h);
}

}  // namespace

bool TargetSpec::Empty() const {
    return exe.empty() && titleContains.empty() && className.empty();
}

std::wstring TargetSpec::Describe() const {
    std::wstring s;
    if (!exe.empty()) {
        s += L"进程=" + exe;
    }
    if (!titleContains.empty()) {
        if (!s.empty()) s += L"  ";
        s += L"标题包含=\"" + titleContains + L"\"";
    }
    if (!className.empty()) {
        if (!s.empty()) s += L"  ";
        s += L"类名=" + className;
    }
    return s.empty() ? std::wstring(L"（未设置）") : s;
}

std::wstring ConfigPath() {
    PWSTR appdata = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        dir = appdata;
        CoTaskMemFree(appdata);
    } else {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(_countof(tmp), tmp);
        dir = tmp;
    }
    dir += L"\\quietkey";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

Config Config::Load() {
    Config cfg;
    const std::wstring path = ConfigPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LogF(L"没有配置文件，使用默认值：%s", path.c_str());
        return cfg;
    }

    cfg.hotkey = ReadString(L"hotkey", cfg.hotkey, path);
    cfg.target.exe = ReadString(L"target_exe", L"", path);
    cfg.target.titleContains = ReadString(L"target_title", L"", path);
    cfg.target.className = ReadString(L"target_class", L"", path);
    cfg.strategies.gsmtc = ReadBool(L"strategy_gsmtc", cfg.strategies.gsmtc, path);
    cfg.strategies.appcommand = ReadBool(L"strategy_appcommand", cfg.strategies.appcommand, path);
    cfg.strategies.postmessage = ReadBool(L"strategy_postmessage", cfg.strategies.postmessage, path);
    cfg.strategies.focusSendInput = ReadBool(L"strategy_focus_sendinput", cfg.strategies.focusSendInput, path);
    cfg.followRecentSession = ReadBool(L"follow_recent_session", cfg.followRecentSession, path);
    cfg.focusSettleMs = GetPrivateProfileIntW(kSection, L"focus_settle_ms", cfg.focusSettleMs, path.c_str());
    if (cfg.focusSettleMs > 500) {
        cfg.focusSettleMs = 500;  // 超过半秒的"硬等"没有意义，只会让焦点乱跳更久
    }
    return cfg;
}

bool Config::Save() const {
    const std::wstring path = ConfigPath();
    EnsureUnicodeIni(path);

    bool ok = true;
    ok &= WriteString(L"hotkey", hotkey, path);
    ok &= WriteString(L"target_exe", target.exe, path);
    ok &= WriteString(L"target_title", target.titleContains, path);
    ok &= WriteString(L"target_class", target.className, path);
    ok &= WriteBool(L"strategy_gsmtc", strategies.gsmtc, path);
    ok &= WriteBool(L"strategy_appcommand", strategies.appcommand, path);
    ok &= WriteBool(L"strategy_postmessage", strategies.postmessage, path);
    ok &= WriteBool(L"strategy_focus_sendinput", strategies.focusSendInput, path);
    ok &= WriteBool(L"follow_recent_session", followRecentSession, path);
    ok &= WriteString(L"focus_settle_ms", std::to_wstring(focusSettleMs), path);

    // 刷盘。**这一句的返回值不能用来判定成败**：实测即使前面每一项都写成功、
    // 文件内容完全正确，这个"全 NULL"的刷新调用也可能返回 FALSE，
    // 于是界面弹出"配置写入失败"的假警报。成败以上面每一项的返回值为准。
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());

    if (!ok) {
        LogF(L"配置写入失败（错误码 %lu）：%s", GetLastError(), path.c_str());
    }
    return ok;
}
