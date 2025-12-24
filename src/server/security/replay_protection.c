/* src/server/security/replay_protection.c - 簡單的封包重放攻擊防護 */
#include "replay_protection.h"
#include "../../common/log.h"

// 初始化 Replay Protection 狀態
void replay_protection_init(ReplayProtection *rp) {
    if (!rp) return;
    rp->last_seq_num = 0;
    rp->initialized = false;
}

// 驗證封包序列號，防止重放攻擊
// 簡單策略：新封包的 seq_num 必須大於最後接收的 seq_num
// 處理 uint32_t 溢位情況（允許從 0xFFFFFFFF 回到 0）
bool replay_protection_validate(ReplayProtection *rp, uint32_t seq_num) {
    if (!rp) {
        return false;
    }

    // 第一個封包：直接接受
    if (!rp->initialized) {
        rp->last_seq_num = seq_num;
        rp->initialized = true;
        return true;
    }

    // 檢查是否為重放封包
    // 策略：新 seq_num 必須 > last_seq_num
    // 但考慮溢位：如果 last_seq_num 很大（接近 0xFFFFFFFF），
    // 而 seq_num 很小（接近 0），可能是正常的溢位，也接受
    
    // 簡單實作：只檢查是否等於或小於最後的 seq_num（不允許重複）
    // 允許溢位：如果 seq_num < last_seq_num 且差距很大，可能是溢位，也接受
    uint32_t diff;
    
    if (seq_num > rp->last_seq_num) {
        // 正常情況：新封包
        diff = seq_num - rp->last_seq_num;
    } else {
        // 可能的情況：重放或溢位
        diff = rp->last_seq_num - seq_num;
        
        // 如果差距很大（超過一半的 uint32_t 範圍），可能是溢位，接受
        // 否則視為重放攻擊
        if (diff < 0x7FFFFFFF) {
            // 差距不大，可能是重放攻擊
            LOG_WARN("Possible replay attack detected: seq_num=%u, last_seq_num=%u (diff=%u)",
                     seq_num, rp->last_seq_num, diff);
            return false;
        }
        // 差距很大，可能是溢位，接受
    }

    // 更新最後接收的序列號
    rp->last_seq_num = seq_num;
    return true;
}

