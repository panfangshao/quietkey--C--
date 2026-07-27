// 配置的持久化。
//
// 存 INI 而不是 TOML/JSON：Win32 自带 GetPrivateProfileString/WritePrivateProfileString，
// 零第三方依赖，记事本能直接改，读不到的字段天然回落到默认值——
// 新增配置项不会破坏旧文件。
//
// **关键设计：不存 HWND。** 窗口句柄在目标程序重启后就失效了，
// 所以存的是一组匹配规则（进程名 / 标题子串 / 窗口类名），
// 每次触发热键时重新解析成当前的 HWND。
#pragma once

#include <string>

struct TargetSpec {
    std::wstring exe;             // 进程可执行文件名，不区分大小写，如 "chrome.exe"
    std::wstring titleContains;   // 标题包含该子串，不区分大小写
    std::wstring className;       // 窗口类名，精确匹配

    bool Empty() const;
    /// 人类可读的摘要，界面上显示用。
    std::wstring Describe() const;
};

struct Strategies {
    bool gsmtc = true;            // 第 1 层：媒体会话
    bool appcommand = true;       // 第 2 层：WM_APPCOMMAND
    bool postmessage = true;      // 第 3 层：PostMessage 空格
    // 第 4 层默认关闭：它是唯一会打断你打字的方案，
    // 让用户确认前三层都不管用之后再主动开启。
    bool focusSendInput = false;
};

struct Config {
    std::wstring hotkey = L"Alt+Q";
    TargetSpec target;
    Strategies strategies;
    /// 兜底层切走焦点后等多久再发按键（毫秒）。
    unsigned focusSettleMs = 30;
    /// 第 1 层挑会话时，优先跟随你最近手动播放/暂停过的那一个。
    bool followRecentSession = true;

    static Config Load();
    bool Save() const;
};

/// %APPDATA%\quietkey\config.ini
std::wstring ConfigPath();

// ---- 开机自启 ----
//
// 真相存在注册表（HKCU\...\Run）而不是配置文件里：用户可能用任务管理器的
// 「启动」页、或者别的工具改掉它，配置文件跟着走只会两边打架。
// 界面上的勾选框每次打开都从注册表现读。

bool IsAutoStartEnabled();
/// 返回是否成功。失败的原因通常是注册表被安全软件锁了，要如实告诉用户。
bool SetAutoStart(bool enabled);
