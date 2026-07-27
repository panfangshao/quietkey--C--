# 项目：quietkey（C++ 版）

Windows 托盘常驻工具。一个全局热键切换后台视频的播放/暂停，**不抢焦点、不切窗口**，
用户可以一边打笔记一边控制视频。技术栈 **C++17 + Win32 + C++/WinRT**，
无第三方依赖，仅 Windows，不做跨平台抽象。

> 这是 Rust + egui 版本的重写。下面记的坑大多是那一版真金白银踩出来的结论，
> 换语言之后依然成立——它们是 Windows 的行为，不是语言的问题。

## 命令

```powershell
build.bat          # Release → build\quietkey.exe
build.bat debug
build.bat clean
```

程序是 `/SUBSYSTEM:WINDOWS`（双击不弹黑框），所以**默认没有控制台**。
排查用 `--console`（把日志打到当前终端）或 `--list-sessions` / `--test-once`。
日志同时写 `OutputDebugString`，DebugView 能随时旁观。

## 架构

**单线程**：一个隐藏窗口 + 一条消息循环，动作在 `WM_HOTKEY` 里同步执行。
Rust 版为了躲开 egui 的重绘节奏必须另起工作线程，这里不需要——
界面是原生对话框，不存在"隐藏时不重绘"的问题。

策略链最慢的一层是 `SendMessageTimeoutW`（上限 120ms），期间界面短暂无响应可接受，
何况平时窗口根本不显示。**不要为此引入线程**，那会把跨线程状态同步的复杂度带进来，
而现在一个 `Config` 结构体就够了。

## 策略链——项目的核心

`strategy.cpp` 按**对用户的打扰程度**从小到大跑四层，第一个 `Applied` 即停：

1. `gsmtc.cpp` —— Windows 媒体会话（C++/WinRT）
2. `messages.cpp` `AppCommandPlayPause` —— `WM_APPCOMMAND` 定向投给指定 HWND
3. `messages.cpp` `PostSpace` —— `WM_KEYDOWN`/`WM_KEYUP` 投给焦点子窗口
4. `fallback.cpp` —— 抢前台 + `SendInput` + 还原。**默认关闭**

以下几条是设计依据，忘掉就会被改错：

- **合成的 `WM_KEYDOWN` 对浏览器视频无效。** Chromium 的键盘输入走自己的 renderer
  管线，不读顶层 HWND 的消息队列。对网页视频（本项目的主场景）来说第 1 层不是优化，
  而是唯一可行的无感路径。**不要以「精简」为由删掉 GSMTC。**
- **第 2 层用 `SendMessageTimeoutW` 而非 `PostMessageW`，是刻意的。** 返回值是区分
  「对方接住了」和「对方无视了」的唯一依据——处理 `WM_APPCOMMAND` 的程序返回 TRUE，
  `DefWindowProc` 返回 0。换成 `PostMessageW` 会让第 2 层永远报成功，
  把第 3、4 层永久饿死。
  > 已知例外：Chromium 系**会执行**这个命令却返回 0，于是第 2 层对浏览器报假失败。
  > 目前不改判据（改了会饿死后面几层），第 1 层本来就先命中，影响有限。
- **第 3 层无法确认任何事。** `PostMessageW` 是异步的，只表示「塞进队列了」。
  它乐观返回 `Applied`，detail 里带「无法确认」字样。这也正是每一层都要能单独开关的原因。
- **只有第 4 层会切焦点**（`ActionReport::intrusive`），界面要标出来。新增层时保持这个标记准确。
- 第 4 层的两次 `Sleep` **去不掉**：`SetForegroundWindow` 返回时前台切换还没走完，
  立刻 `SendInput` 会打到旧窗口上。这是 Win32 的固有行为，和语言、按键库都无关。
  能做的是把它从「三段 500ms 硬等」压到 30ms 并可配置。

每次尝试都记进 `ActionReport::trace` 并在界面显示。**这条 trace 是主要排查手段，改策略链时要保留。**

## 第 1 层挑哪个媒体会话

`GsmtcToggle` 按五级顺序挑，命中的路线会写进 trace（`chrome → 已暂停（最近活跃）`）：

1. **配置匹配** —— `target.exe` 填了进程名就永远听配置的
2. **最近活跃** —— 见 `recency.h`，`followRecentSession` 控制
3. **唯一在播放** —— 只开了一个视频的日常场景
4. **系统当前会话** —— `GetCurrentSession`
5. **唯一会话**

两条容易改错的依据：

- **第 3 条不能单独存在。** 视频被我们暂停后，正在播放的会话数变成 0，
  只靠它会彻底找不回目标，掉到对浏览器无效的第 2/3 层——症状是
  「能暂停、再按一次没反应」。第 4、5 条就是为这个补的。
- **第 2 条的判据是 `TimelineProperties.LastUpdatedTime`，不是播放状态。**
  实测（Chromium 系）：手动播放/暂停/拖进度条会刷新它，**一直播着不会**——
  它不随播放进度自己往前跑。所以它就是"这个会话上次被碰是什么时候"，
  取最大的即可，`recency.cpp` 因此是**无状态纯函数**。

  走过两条弯路，别再回去：

  * **订阅 `PlaybackInfoChanged` 事件**：订阅得上（拿得到 token）但**回调一次都不触发**。
    GSMTC 的事件由中等完整性级别的系统媒体服务回调进来，程序一旦以管理员身份运行，
    跨完整性级别的入站 COM 调用会被系统拦掉，功能静默失效；而出站调用
    （枚举会话、切播放状态）全都正常，极难察觉。
    manifest 里写死 `asInvoker` 也是为了这个。
  * **比对播放状态快照**：盲区是「手动播放 → 再手动暂停」这种一来一回，
    净状态和上一轮一模一样，比对结果是"没动过"，热键跟不过去——
    而这恰恰是最常见的用法。这条弯路还附带要求"把自己造成的变化回填快照"，
    一整套记账逻辑，换成时间戳后全部消失。

## 「热键没反应」的排查顺序

实际踩过的顺序，照着走能省一整轮：

1. **程序还在跑吗。** 托盘图标在不在。测试时被 `Stop-Process` 掉过、用户为了编译退掉过——
   两次都表现成"按了完全没反应"，两次都查了半天。
2. **`--list-sessions` 有没有会话。** 播放器**随时可能撤销媒体会话**：标签页被浏览器
   丢弃/降级、视频被静音、长时间暂停之后都会。实测中「Chrome 开着 B 站页面、
   用户确认有声音」，会话数依然是 0；**在页面上按 F5 重新播放后立刻恢复**。
   这时第 1 层根本够不着它，不是代码问题。
3. **会话在但控不到**，才轮到看 trace 里的命中路线和挑选顺序。

对应的提示语也要说人话：没有任何会话时**不能**报「尚未设置目标窗口」——
那是答非所问，会把用户引向完全错误的方向（`strategy.cpp` 里已按这个分支给出具体建议）。

### 怎么造一个"一定存在"的媒体会话来自测

排查"是我读不到还是它没注册"时，需要一个已知存在的会话做对照。
生成一段几十秒的低音量 WAV，用系统默认播放器打开即可——浏览器打开本地音频
一样会注册 SMTC 会话。这个方法在这次排查里一次就把问题定死了：
自造会话能被正确读到、切换也正常，于是问题锁定在"B 站那个标签页没注册"。

## 目标窗口解析

`TargetSpec`（`config.h`）存的是**匹配规则**（进程名/标题子串/类名），
**绝不存 `HWND`**——目标程序一重启句柄就失效。
`ResolveTarget`（`winutil.cpp`）每次触发都重新解析，多个命中时取标题最长的
（浏览器真正的视频标签页标题比「新标签页」长）。

窗口拾取器**故意不勾** `titleContains`：视频标题会随播放进度变，拿它匹配很脆。

`ListWindows` 过滤了 owner 窗口、`WS_EX_TOOLWINDOW`、以及 **DWM cloaked** 窗口。
cloaked 检查是必需的，去掉之后拾取器里会塞满看不见的 UWP 幽灵窗口。

## Win32 层的不显然约束

- **托盘图标必须挂在能收系统广播的窗口上。** 用 `HWND_MESSAGE` 消息窗口收不到
  `TaskbarCreated`，资源管理器一重启图标就再也回不来。所以用的是普通隐藏窗口。
- **弹右键菜单前要 `SetForegroundWindow`，弹完补一个 `PostMessage(WM_NULL)`**，
  否则菜单点别处不消失。
- **`ForceForeground` 必须做 `AttachThreadInput` 那套动作**：后台进程裸调
  `SetForegroundWindow` 会被 Windows 的前台锁定机制拒绝。
- **热键注册在组合键被占用时会直接失败**，这是 `RegisterHotKey` 的固有行为，
  不是 bug。要把错误暴露给用户，**不要静默吞掉**。
- **单实例互斥体不能省。** 第二个实例注册热键必定失败，用户会对着一个
  "热键没反应"的程序发懵——Rust 版就这么浪费过一整轮排查。
- **`msctls_hotkey32` 不支持 Win 键**，需要 Win 组合只能手改 INI。
  这是控件的限制，界面上不要假装支持。

## 编码与工具链的坑

- **`.bat` 文件必须纯 ASCII。** 批处理按控制台 OEM 代码页读取，UTF-8 的中文会被撕碎，
  连命令解析都可能崩。中文说明放 README。
- **VS 装在 `C:\Program Files (x86)\`，路径里的括号会提前闭合 `for`/`if` 的括号组。**
  所有 VS 路径用 `!延迟展开!` + 引号，vswhere 的输出走临时文件而不是 `for /f` 反引号。
- **`vcvars64.bat` 自己会打一句假错误** `'vswhere.exe' is not recognized`，
  环境其实设置正确，build.bat 里已静音。
- **源码用 `/utf-8` 编译**，`.rc` 里加 `#pragma code_page(65001)`，否则中文全乱。
- **`WritePrivateProfileStringW(NULL,NULL,NULL,path)` 的返回值不可信。**
  实测每一项都写成功、文件完全正确，这个"刷新缓存"调用仍可能返回 FALSE。
  成败要以每一项写入的返回值为准，否则界面弹假的「配置写入失败」。
- **INI 要先写 UTF-16 BOM 才能存中文。** `WritePrivateProfileStringW` 只有在文件
  已经是 UTF-16 时才按宽字符写，否则按 ANSI 转换，窗口标题里的中文变问号。
- **不要无条件 `freopen("CONOUT$", stdout)`。** 那会把用户的重定向抢回控制台，
  `--list-sessions > 文件` 写出来是空的。已有 std 句柄就直接用，
  没有才 `AttachConsole`/`AllocConsole`。
- 控制台输出要**同时支持真控制台和管道**：前者用 `WriteConsoleW`，
  后者必须转 UTF-8 走 `WriteFile`（`WriteConsoleW` 对管道会静默失败）。

## 验证改动

`build.bat` 能挡住编译期问题，但 Win32/WinRT 层只能实机验证：

1. `build\quietkey.exe --list-sessions` —— 确认播放器有没有注册媒体会话
2. 起一个视频，`--test-once` 跑一遍策略链，看逐层 trace
3. 界面里「立即测试一次」用**草稿配置**跑（不必先保存），验证改设置的效果
4. 把焦点放在**第三个**窗口（文本编辑器）里按真实热键，确认打字不被打断——
   这才是本项目真正的验收标准

### 用脚本验证界面时，三个反复上当的陷阱

1. **截图前必须 `SetProcessDPIAware()`。** 这台机器是 150% 缩放，不做 DPI 感知的
   截图进程拿到的 `GetWindowRect` 是虚拟化坐标，`CopyFromScreen` 用物理像素，
   结果只截到窗口左上角一部分，看起来像"界面文字被裁掉了"。
2. **`GetWindowText` 读不到别的进程里编辑框/列表框的文本**（静态控件能读，
   因为文本存在窗口结构里）。跨进程要用 `SendMessage(WM_GETTEXT)`。
3. **PowerShell 给 `string` 参数传 `$null` 可能变成空字符串**，
   于是 `FindWindowW(class, $null)` 变成"找标题为空的窗口"，永远找不到。
   把参数声明成 `IntPtr` 传 `IntPtr::Zero`，或者干脆用 `EnumWindows` 自己筛。

这三条都曾让我以为程序有 bug 并动手"修"了不存在的问题。**先怀疑测量方法。**
