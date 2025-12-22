/* src/server/logic/dice.c */
#include "dice.h"
#include "../../common/log.h"
#include <stdlib.h> // for rand, srand
#include <time.h>   // for time
#include <string.h> // for memset

// 定義一擊必殺的傷害數值
#define INSTANT_KILL_DAMAGE 999999

// 初始化隨機數系統
void dice_init() {
    srand(time(NULL));
    LOG_DEBUG("Random Number Generator Initialized");
}

// 核心戰鬥處理函式
void game_process_attack(int player_dice, const char* player_name, 
                         AttackResult *result_out, Payload_GameState *state_out) {
    
    // 0. 安全檢查與初始化
    if (!result_out || !state_out) return;
    
    // 清空結果結構 (包含新的 lucky_kill 和 streak 都會被設為 0/false)
    memset(result_out, 0, sizeof(AttackResult));

    // 1. 取得當前遊戲狀態快照 (Snapshot)
    GameSharedData current_state;
    gamestate_get_snapshot(&current_state);

    // 狀態檢查：如果正在重生或遊戲已結束，攻擊無效
    if (current_state.is_respawning || current_state.stage == BOSS_STAGE_DEAD) {
        state_out->boss_hp = current_state.current_hp;
        state_out->max_hp = current_state.max_hp;
        state_out->online_count = current_state.online_count;
        return;
    }

    // 2. Boss 擲骰子
    int boss_dice = (rand() % 6) + 1;
    result_out->boss_dice = boss_dice;

    // --- 彩蛋 1: 天選之人 (Lucky Kill) ---
    // 機率：0.0001% (百萬分之一)
    // 測試時建議改成 (rand() % 100) == 0 來比較好觸發
    bool is_lucky_kill = (rand() % 1000000) == 777777;

    if (is_lucky_kill) {
        LOG_WARN("EASTER EGG: %s triggered LUCKY KILL! (0.0001%% chance)", player_name);
        
        // 設定回傳狀態
        result_out->is_win = true;
        result_out->is_crit = true;
        result_out->is_lucky_kill = true; // [更新] 告訴 UI 這是天選之人
        result_out->dmg_dealt = INSTANT_KILL_DAMAGE;
        
        // 執行扣血
        bool killed = gamestate_apply_damage(result_out->dmg_dealt, player_name);
        if (killed) result_out->boss_just_died = true;

    } 
    else if (player_dice > boss_dice) {
        // --- 情況 A: 一般勝利 ---
        result_out->is_win = true;
        result_out->dmg_dealt = player_dice;

        // 基礎爆擊判定
        if (player_dice == 6) {
            result_out->is_crit = true;
            result_out->dmg_dealt *= 2; 
        }

        // --- 彩蛋 2: 三連擊 (Combo Kill) ---
        // 呼叫 gamestate 更新並取得目前連擊數
        int streak = gamestate_update_streak(player_name, player_dice, true);
        
        // [更新] 將連擊數填入回傳結構，讓 UI 顯示
        result_out->current_streak = streak;

        if (streak >= 3) {
            LOG_WARN("EASTER EGG: %s triggered 3-COMBO KILL! (Dice: %d)", player_name, player_dice);
            result_out->is_crit = true; 
            result_out->dmg_dealt = INSTANT_KILL_DAMAGE; // 秒殺
        }

        // 執行扣血
        bool killed = gamestate_apply_damage(result_out->dmg_dealt, player_name);
        if (killed) result_out->boss_just_died = true;

    } 
    else {
        // --- 情況 B/C: 輸或平手 ---
        result_out->is_win = false;
        
        // 中斷連勝
        gamestate_update_streak(player_name, player_dice, false);
        result_out->current_streak = 0; // [更新] 輸了就是 0

        if (player_dice < boss_dice) {
            result_out->dmg_taken = boss_dice + 10;
        } else {
            result_out->dmg_taken = 0;
            result_out->dmg_dealt = 0;
        }
    }

    // 3. 再次取得最新狀態 (回傳給 Client 更新血條)
    gamestate_get_snapshot(&current_state);
    
    state_out->boss_hp = current_state.current_hp;
    state_out->max_hp = current_state.max_hp;
    state_out->online_count = current_state.online_count;
}