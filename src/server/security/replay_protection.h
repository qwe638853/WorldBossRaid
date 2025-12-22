#ifndef REPLAY_PROTECTION_H
#define REPLAY_PROTECTION_H

#include <stdint.h>
#include <stdbool.h>

// Replay Protection 狀態結構
// 每個連線維護一個實例，用於追蹤已接收的封包序列號
typedef struct {
    uint32_t last_seq_num;  // 最後接收到的序列號
    bool initialized;        // 是否已初始化（收到第一個封包）
} ReplayProtection;

// 初始化 Replay Protection 狀態
void replay_protection_init(ReplayProtection *rp);

// 驗證封包序列號，防止重放攻擊
// 返回 true 表示封包有效（非重放），false 表示可能是重放攻擊
bool replay_protection_validate(ReplayProtection *rp, uint32_t seq_num);

#endif

