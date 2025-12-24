#ifndef GAMESTATE_H
#define GAMESTATE_H

#include <pthread.h>
#include <stdbool.h>

// --- 定義常數 ---
#define BOSS_1_MAX_HP 1000
#define BOSS_2_MAX_HP 2000

// 最多紀錄幾位玩家的連勝狀態 (針對彩蛋功能)
#define MAX_TRACKED_PLAYERS 100 

// --- 定義列舉 ---
// Boss 的階段
typedef enum {
    BOSS_STAGE_1 = 0,   // 第一隻王
    BOSS_STAGE_2 = 1,   // 第二隻王
    BOSS_STAGE_DEAD = 2 // 全部打完
} BossStage;

// --- 定義結構 ---

// 玩家歷史紀錄 (用於計算彩蛋連擊)
// 這是放在 Shared Memory 裡面的，所以大家都能讀寫
typedef struct {
    char name[32];    // 玩家名字
    int last_dice;    // 上一次骰出的數字
    int streak_count; // 連續骰出一樣數字且勝利的次數
} PlayerHistory;

// 共享記憶體的核心結構
typedef struct {
    // --- 同步機制 ---
    pthread_mutex_t lock; // 互斥鎖 (保護這塊記憶體)

    // --- 遊戲數據 ---
    int current_hp;       // 當前血量
    int max_hp;           // 最大血量
    BossStage stage;      // 目前是第幾隻王
    int online_count;     // 線上人數
    
    // --- 狀態標記 ---
    bool is_respawning;   // 是否正在重生冷卻中
    char last_killer[32]; // 上一隻王的擊殺者名字 (給廣播用)

    // --- 新增：玩家連擊紀錄表 ---
    PlayerHistory players[MAX_TRACKED_PLAYERS]; 

} GameSharedData;

// --- 函數宣告 ---

// 1. 初始化與銷毀
void gamestate_init();
void gamestate_destroy();

// 2. 玩家管理
// 回傳新的 online_count
int gamestate_player_join();             
void gamestate_player_leave();

// 3. 狀態讀取 (給 Server 包封包用)
// 為了避免 Race Condition，這裡會回傳一個複本 (Snapshot)
void gamestate_get_snapshot(GameSharedData *out_data);

// 4. 狀態寫入 (給 Dice 模組呼叫)
// damage: 要扣多少血
// attacker_name: 如果這一擊打死王，紀錄是誰殺的
// 回傳: true 代表這一擊剛好把王殺死 (Just Killed)
bool gamestate_apply_damage(int damage, const char* attacker_name);

// 5. 強制切換下一隻王 (重生時間到時呼叫)
void gamestate_spawn_next_boss();

// 6. 新增：更新玩家紀錄並取得連擊數 (給 Dice 模組呼叫)
// 參數：name(名字), current_dice(這次骰多少), is_win(這次贏了嗎)
// 回傳：目前的連擊次數 (如果是 3 就代表觸發彩蛋)
int gamestate_update_streak(const char* name, int current_dice, bool is_win);

#endif