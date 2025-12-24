#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h> // 用於 bool 類型
#include <unistd.h> // 用於 usleep (動畫延遲)
#include <time.h>
#include "client_ui.h"
#include "bonus.h"
#include "end_ui.h"
#include "god.h"

// 定義圖檔名稱
#define FILE_STAGE_1 "../src/client/ui/AsciiText.txt"
#define FILE_STAGE_2 "../src/client/ui/2nd.txt"

// 遊戲參數（顯示用上限，實際 max_hp 由 Server 提供）
#define PLAYER_MAX_HP 100
// 當 server 還沒給 max_hp 時，用這個預設值畫血條，避免除以 0
#define DEFAULT_BOSS_MAX_HP 2000

typedef struct {
    int boss_hp;
    int max_hp;            // 由 Server 回傳，用來算血條比例
    int player_hp;
    int stage; // 1 或 2
    char log_msg[100];     // 戰鬥紀錄
    char dice_visual[100]; // 骰子顯示字串
    char last_killer[UI_MAX_NAME]; // 最後一擊玩家名稱（顯示 Victory 用）
    char player_name[UI_MAX_NAME]; // 當前玩家名稱（顯示在 UI 上）
    bool has_shown_lucky_kill; // 是否已經顯示過 Lucky Kill 畫面（避免重複顯示）
} LocalGameState;

static LocalGameState state;

// --- 讀取並顯示 ASCII 檔案 ---
static void draw_boss_art() {
    // 根據階段決定要讀哪個檔案
    const char *filename = (state.stage == 1) ? FILE_STAGE_1 : FILE_STAGE_2;
    
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        mvprintw(5, 5, "Error: Cannot open %s", filename);
        return;
    }

    char line[2048];
    int y = 0;
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // 設定 Boss 顏色 (P1:白 / P2:紅)
    int color = (state.stage == 1) ? 1 : 2;
    if (has_colors()) {
        init_pair(1, COLOR_WHITE, COLOR_BLACK);
        init_pair(2, COLOR_RED, COLOR_BLACK);
        attron(COLOR_PAIR(color));
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (y >= max_y - 8) break; 
        line[strcspn(line, "\n")] = 0;
        
        int len = strlen(line);
        int x = (max_x - len) / 2;
        if (x < 0) x = 0;

        mvprintw(y, x, "%s", line);
        y++;
    }

    if (has_colors()) attroff(COLOR_PAIR(color));
    fclose(fp);
}

// --- 繪製 UI ---
static void draw_ui() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int center_x = cols / 2 - 25;
    int ui_y = rows - 7;

    // 1. Boss 資訊
    if (state.stage == 1)
        mvprintw(ui_y, center_x, "            BOSS: SUCCUBUS QUEEN [NORMAL]");
    else
        attron(COLOR_PAIR(2)), mvprintw(ui_y, center_x, "            BOSS: SUCCUBUS QUEEN [ENRAGED]"), attroff(COLOR_PAIR(2));

    // 2. Boss 血條
    ui_y++;
    mvprintw(ui_y, center_x - 4, "HP:");
    int bar_width = 50;
    int max_hp = state.max_hp > 0 ? state.max_hp : DEFAULT_BOSS_MAX_HP;
    float ratio = (float)state.boss_hp / max_hp;
    int fill = (int)(ratio * bar_width);

    init_pair(3, COLOR_WHITE, COLOR_RED);
    attron(COLOR_PAIR(3));
    mvprintw(ui_y, center_x, "[");
    for(int i=0; i<bar_width; i++) addch(i < fill ? ' ' : ' '); // 用背景色畫條
    // 注意：為了視覺效果，我們用紅色背景畫滿，空的部分應該關閉顏色
    // 這裡簡化處理：直接用紅色背景代表血量
    attroff(COLOR_PAIR(3));
    
    // 補上未填滿的部分(黑色背景)
    move(ui_y, center_x + 1 + fill);
    for(int i=fill; i<bar_width; i++) printw(".");
    
    printw("] %d", state.boss_hp);

    // 3. 分隔線與操作區
    ui_y += 2;
    mvhline(ui_y, 0, '-', cols);
    
    // 底部操作欄布局：左邊命令 | 中間骰子 | 右邊玩家名稱和log
    int bottom_y = ui_y + 1;
    
    // 左邊：縮短命令文字，避免覆蓋
    mvprintw(bottom_y, 2, "[SPACE] Attack    [2] Phase 2    [Q] Quit");
    
    // 中間：骰子結果（稍微偏左，給右邊留空間）
    int dice_x = cols / 2 - 15; // 比 center_x 更靠左一點
    attron(A_BOLD);
    mvprintw(bottom_y, dice_x, "%s", state.dice_visual);
    attroff(A_BOLD);
    
    // 右邊：玩家名稱和 log 訊息
    int dice_end = dice_x + strlen(state.dice_visual);
    int log_start = cols - strlen(state.log_msg) - 2;
    
    // 計算玩家名稱位置：在骰子和 log 之間，稍微靠右
    if (state.player_name[0] != '\0') {
        char player_display[64];
        snprintf(player_display, sizeof(player_display), "Player: %s", state.player_name);
        int player_name_len = strlen(player_display);
        
        // 玩家名稱位置：從骰子結束後留更多空間，然後再往右一點
        int player_name_x = dice_end + 5; // 骰子後空 5 格
        
        // 確保不會與 log 重疊，至少留 3 格間距
        if (player_name_x + player_name_len + 3 < log_start) {
            mvprintw(bottom_y, player_name_x, "%s", player_display);
        } else {
            // 如果空間不夠，放在 log 前面一點
            player_name_x = log_start - player_name_len - 3;
            if (player_name_x > dice_end + 3) {
                mvprintw(bottom_y, player_name_x, "%s", player_display);
            }
        }
    }
    
    // 最右邊：log 訊息
    mvprintw(bottom_y, log_start, "%s", state.log_msg);
}

// --- 動畫特效：骰子轉動 ---
static void animate_dice_roll() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)rows;
    (void)cols;
    
    // 動畫播放 10 幀 (約 0.5 秒)
    for(int i = 0; i < 10; i++) {
        // 隨機生成假數字來製造「跳動」效果
        int d1 = rand() % 6 + 1;
        int d2 = rand() % 6 + 1;
        int d3 = rand() % 6 + 1;
        
        sprintf(state.dice_visual, "Rolling... [%d] [%d] [%d]", d1, d2, d3);
        
        // 只重繪 UI 部分，避免整個畫面閃爍太嚴重
        draw_ui();
        refresh();
        
        usleep(50000); // 暫停 50ms (50000 微秒)
    }
}

int ui_game_loop(const char *player_name, UiAttackCallback attack_cb, UiHeartbeatCallback heartbeat_cb) {
    if (!attack_cb) return -1;

    // 清除 login 畫面殘留（確保 "ENTER YOUR NAME:" 等文字消失）
    clear();
    refresh();

    // 初始化本地顯示狀態
    state.boss_hp = 0;
    state.max_hp = 0;
    state.player_hp = PLAYER_MAX_HP;
    state.stage = 1;
    state.has_shown_lucky_kill = false;
    strcpy(state.log_msg, "Connecting to Boss...");
    strcpy(state.dice_visual, "Dice: [?] [?] [?]");
    if (player_name) {
        strncpy(state.player_name, player_name, UI_MAX_NAME - 1);
        state.player_name[UI_MAX_NAME - 1] = '\0';
    } else {
        state.player_name[0] = '\0';
    }

    // 設定非阻塞輸入，讓我們可以在沒有按鍵時做 heartbeat
    timeout(100); // 100ms

    int ch;
    int idle_ticks = 0;

    while((ch = getch()) != 'q') {
        // 處理輸入
        if (ch == ' ') {
            UiGameState g;

            // 1. 播放本地骰子動畫
            animate_dice_roll();

            // 2. 呼叫攻擊 callback，實際去 Server 擲骰 / 扣血
            if (attack_cb(&g) == 0) {
                state.boss_hp = g.boss_hp;
                state.max_hp = g.max_hp;
                state.stage = (g.stage == 0) ? 1 : (g.stage == 1 ? 2 : state.stage);
                strncpy(state.last_killer, g.last_killer, UI_MAX_NAME - 1);
                state.last_killer[UI_MAX_NAME - 1] = '\0';

                // 更新顯示文字
                snprintf(state.dice_visual, sizeof(state.dice_visual),
                         "You vs Boss: [%d] vs [%d]",
                         (int)g.last_player_damage, (int)g.last_boss_dice);

                // 處理 Lucky Kill：所有客戶端都顯示 god 畫面（最高優先）
                if (g.is_lucky && !state.has_shown_lucky_kill) {
                    strcpy(state.log_msg, "LUCKY MAN!!! INSTANT KILL!");
                    ui_show_god_screen(); // 顯示天選之人畫面
                    state.has_shown_lucky_kill = true; // 標記已顯示，避免重複
                }
                // 處理一般爆擊 / 三連擊：顯示 bonus 畫面（只有自己看到）
                else if (g.is_crit) {
                    if (g.last_player_streak >= 3) {
                        snprintf(state.log_msg, sizeof(state.log_msg),
                                 "CRIT! -%d (Combo x%d)",
                                 (int)g.last_player_damage, (int)g.last_player_streak);
                    } else {
                        snprintf(state.log_msg, sizeof(state.log_msg),
                                 "CRIT! -%d",
                                 (int)g.last_player_damage);
                    }
                    ui_show_bonus_screen(); // 顯示 CRIT / COMBO 動畫
                }
                else if (g.dmg_taken > 0) {
                    snprintf(state.log_msg, sizeof(state.log_msg),
                             "Ouch! Boss hits you for %d", (int)g.dmg_taken);
                } else {
                    snprintf(state.log_msg, sizeof(state.log_msg),
                             "Hit! -%d", (int)g.last_player_damage);
                }

                // 判斷是否所有 Boss 已經死亡
                if (g.stage == 2 && g.boss_hp <= 0 && !g.is_respawning) {
                    ui_show_victory_screen(state.last_killer);
                    return 0;
                }
            } else {
                strcpy(state.log_msg, "Network error during attack");
            }
        }

        // Heartbeat：每 N 個 tick 主動問一次最新血量
        if (heartbeat_cb) {
            idle_ticks++;
            if (idle_ticks > 5) { // 約 0.5 秒
                UiGameState g;
                if (heartbeat_cb(&g) == 0) {
                    state.boss_hp = g.boss_hp;
                    state.max_hp = g.max_hp;
                    state.stage = (g.stage == 0) ? 1 : (g.stage == 1 ? 2 : state.stage);
                    strncpy(state.last_killer, g.last_killer, UI_MAX_NAME - 1);
                    state.last_killer[UI_MAX_NAME - 1] = '\0';

                    // 心跳也要能偵測到「Lucky Kill 事件」→ 所有 Client 一起顯示 god 畫面
                    if (g.is_lucky && !state.has_shown_lucky_kill) {
                        ui_show_god_screen();
                        state.has_shown_lucky_kill = true; // 標記已顯示，避免重複
                    }
                    
                    // 如果 Lucky Kill 事件已經結束（is_lucky 變回 0），重置標記
                    if (!g.is_lucky && state.has_shown_lucky_kill) {
                        state.has_shown_lucky_kill = false;
                    }

                    // 心跳也要能偵測到「第二隻 Boss 被打死」→ 所有 Client 一起進 Victory 畫面
                    if (g.stage == 2 && g.boss_hp <= 0 && !g.is_respawning) {
                        ui_show_victory_screen(state.last_killer);
                        return 0;
                    }
                }
                idle_ticks = 0;
            }
        }

        // 繪圖迴圈
        erase();
        draw_boss_art();
        draw_ui();
        refresh();
    }

    return 0;
}