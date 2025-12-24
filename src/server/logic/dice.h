/* src/server/logic/dice.h */
#ifndef DICE_H
#define DICE_H

#include <stdbool.h>
#include "../../common/protocol.h" // 引用封包結構
#include "gamestate.h"             // 引用資料層

// --- 戰鬥結果結構 ---
// Server 計算完畢後，填寫這張表回傳給 Client UI
typedef struct {
    int boss_dice;       // Boss 擲出的點數 (1-6)
    int dmg_dealt;       // 這次對王造成的傷害
    int dmg_taken;       // 玩家受到的反擊傷害
    
    // --- 狀態旗標 ---
    bool is_win;         // 玩家是否拼點勝利
    bool is_crit;        // 是否爆擊 (顯示紅字/震動)
    bool boss_just_died; // 是否剛好擊殺 (觸發全服廣播/換王)

    // --- 新增：彩蛋特效專用欄位 ---
    bool is_lucky_kill;  // 是否觸發 0.0001% 天選之人 (UI 顯示: LUCKY!!)
    int current_streak;  // 目前連勝數 (UI 顯示: COMBO x3!!)
} AttackResult;

// --- 核心函數 ---

// 初始化隨機數種子
void dice_init();

// 處理攻擊請求
// 參數:
//   player_dice: Client 傳來的點數
//   player_name: 玩家名字
//   result_out:  (輸出) 詳細戰鬥數據
//   state_out:   (輸出) 更新後的血量狀態
void game_process_attack(int player_dice, const char* player_name, 
                         AttackResult *result_out, Payload_GameState *state_out);

#endif