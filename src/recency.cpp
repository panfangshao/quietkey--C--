#include "recency.h"

#include <cstdio>

int MostRecentIndex(const std::vector<SeenSession>& seen) {
    int best = -1;
    int64_t bestAt = 0;
    for (size_t i = 0; i < seen.size(); ++i) {
        if (seen[i].touchedAt <= 0) {
            continue;  // 这个播放器不上报时间线，参与不了比较
        }
        if (best < 0 || seen[i].touchedAt > bestAt) {
            best = static_cast<int>(i);
            bestAt = seen[i].touchedAt;
        }
    }
    return best;
}

float BehindSeconds(int64_t touchedAt, int64_t newest) {
    return static_cast<float>(newest - touchedAt) / 1e7f;
}

std::wstring DescribeActivity(const std::vector<SeenSession>& seen) {
    int64_t newest = 0;
    for (const SeenSession& s : seen) {
        if (s.touchedAt > newest) {
            newest = s.touchedAt;
        }
    }

    std::wstring out;
    for (const SeenSession& s : seen) {
        if (!out.empty()) {
            out += L" ";
        }
        if (s.touchedAt <= 0) {
            out += s.aumid + L"(无时间线)";
        } else if (s.touchedAt == newest) {
            out += s.aumid + L"(最近)";
        } else {
            wchar_t buf[64];
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"(%.1fs前)",
                         BehindSeconds(s.touchedAt, newest));
            out += s.aumid + buf;
        }
    }
    return out;
}
