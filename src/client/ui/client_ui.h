#ifndef UI_CLIENT_UI_H
#define UI_CLIENT_UI_H

#include <stdint.h>

// 從 Server 回來的遊戲狀態，轉成給 UI 用的精簡結構
#define UI_MAX_NAME 32
typedef struct {
    int32_t boss_hp;
    int32_t max_hp;
    int32_t online_count;
    uint8_t stage;           // 0/1/2
    uint8_t is_respawning;   // 0/1
    uint8_t is_crit;         // 0/1
    uint8_t is_lucky;        // 0/1
    int32_t last_player_damage;
    int32_t last_boss_dice;
    int32_t last_player_streak;
    int32_t dmg_taken;
    char    last_killer[UI_MAX_NAME]; // 最後一擊的玩家名稱（如果有）
} UiGameState;

// 由 UI 呼叫、交給 client 的 callback：發動一次攻擊
// out_state: 由 callback 填入最新狀態（從 Server 拿到）
// 回傳 0 表示成功，其它為失敗
typedef int (*UiAttackCallback)(UiGameState *out_state);

// 由 UI 呼叫、交給 client 的 callback：發送 heartbeat 取得最新狀態
typedef int (*UiHeartbeatCallback)(UiGameState *out_state);

// 主戰鬥畫面迴圈：
// - player_name: 當前玩家名稱（用於顯示在 UI 上）
// - 由空白鍵觸發 attack_cb
// - 由計時觸發 heartbeat_cb（可為 NULL 表示不使用）
// - 內部會依 is_lucky/stage 等決定是否顯示 bonus / end 畫面
// 回傳：0 正常結束（玩家按 Q）、非 0 表示錯誤
int ui_game_loop(const char *player_name, UiAttackCallback attack_cb, UiHeartbeatCallback heartbeat_cb);

#endif

