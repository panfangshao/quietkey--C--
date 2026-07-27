// 「最近活跃的播放器」判定 —— 让热键跟着**你最近动过的那个视频**走。
//
// ## 要解决的问题
//
// 两个浏览器同时开着视频时，「唯一在播放」这条规则失效（有两个在播），
// 命中权就全交给系统的 GetCurrentSession。那个"系统当前会话"由 Windows 自己决定、
// 有出名的粘性，会赖在先注册的会话上——表现为「我刚在 B 浏览器点了播放，
// 热键却还在暂停 A 浏览器」。先后顺序只能自己判。
//
// ## 判据：TimelineProperties.LastUpdatedTime
//
// 每个媒体会话都带一个"播放位置信息最后一次被刷新的时刻"。实测（Chromium 系）：
//
//   * 手动播放、手动暂停、拖进度条 → **会刷新**
//   * 一直播着不动 → **纹丝不动**，不随播放进度自己往前跑
//   * 完全没碰 → 不动
//
// 所以它就是"这个会话上次被碰是什么时候"的直接答案，取最大的那个即可。
//
// ## 走过的两条弯路，别再回去
//
//   1. **订阅 PlaybackInfoChanged 事件**：订阅得上（拿得到 token）但回调一次都不触发。
//      GSMTC 的事件由中等完整性级别的系统媒体服务回调进来，程序一旦以管理员身份运行，
//      跨完整性级别的入站 COM 调用会被系统拦掉，功能静默失效；而出站调用
//      （枚举会话、切播放状态）全都正常，极难察觉。
//   2. **比对播放状态快照**：盲区是「手动播放 → 再手动暂停」这种一来一回，
//      净状态和上一轮一模一样，比对结果是"没动过"，热键跟不过去——
//      而这恰恰是最常见的用法。
//
// 换成时间戳之后这里是**无状态纯函数**，不记历史、不需要回填自己造成的变化。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SeenSession {
    std::wstring aumid;   // 小写 AppUserModelId，只用于展示
    int64_t touchedAt;    // LastUpdatedTime（FILETIME，100 纳秒）；0 = 该播放器不上报
};

/// 最近被碰过的那个会话在 `seen` 里的下标；全都不上报时返回 -1。
int MostRecentIndex(const std::vector<SeenSession>& seen);

/// `touchedAt` 比 `newest` 早多少秒。FILETIME 的单位是 100 纳秒。
float BehindSeconds(int64_t touchedAt, int64_t newest);

/// 排查用的一行摘要：`msedge(最近) chrome(52.1s前)`。
std::wstring DescribeActivity(const std::vector<SeenSession>& seen);
