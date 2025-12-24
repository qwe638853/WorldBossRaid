/* src/server/security/rate_limiter.c - Rate Limiting for DDoS Protection */
#include "rate_limiter.h"
#include "../../common/log.h"
#include <time.h>

// 初始化 Rate Limiter
void rate_limiter_init(RateLimiter *rl, uint32_t max_requests, uint32_t window_seconds) {
    if (!rl) return;
    
    rl->request_count = 0;
    rl->window_start = time(NULL);
    rl->max_requests = max_requests;
    rl->window_seconds = window_seconds;
}

// 檢查是否允許請求（滑動窗口算法）
bool rate_limiter_check(RateLimiter *rl) {
    if (!rl) return false;
    
    time_t now = time(NULL);
    time_t elapsed = now - rl->window_start;
    
    // 如果超過時間窗口，重置計數器
    if (elapsed >= (time_t)rl->window_seconds) {
        rl->request_count = 1;
        rl->window_start = now;
        return true;
    }
    
    // 檢查是否超過限制
    if (rl->request_count >= rl->max_requests) {
        LOG_WARN("Rate limit exceeded: %u requests in %u seconds (max: %u)",
                 rl->request_count, (uint32_t)elapsed, rl->max_requests);
        return false;
    }
    
    // 允許請求，增加計數
    rl->request_count++;
    return true;
}

// 重置 Rate Limiter
void rate_limiter_reset(RateLimiter *rl) {
    if (!rl) return;
    rl->request_count = 0;
    rl->window_start = time(NULL);
}



