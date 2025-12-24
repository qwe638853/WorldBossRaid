#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Rate Limiter 結構
// 用於追蹤每個連線的請求頻率，防止 DDoS 和刷攻擊
typedef struct {
    uint32_t request_count;      // 時間窗口內的請求數量
    time_t window_start;         // 當前時間窗口的開始時間
    uint32_t max_requests;       // 時間窗口內允許的最大請求數
    uint32_t window_seconds;     // 時間窗口大小（秒）
} RateLimiter;

// 初始化 Rate Limiter
// max_requests: 時間窗口內允許的最大請求數
// window_seconds: 時間窗口大小（秒）
void rate_limiter_init(RateLimiter *rl, uint32_t max_requests, uint32_t window_seconds);

// 檢查是否允許請求（滑動窗口算法）
// 返回 true 表示允許請求，false 表示超過限制
bool rate_limiter_check(RateLimiter *rl);

// 重置 Rate Limiter（用於手動重置或測試）
void rate_limiter_reset(RateLimiter *rl);

#endif



