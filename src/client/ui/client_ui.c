#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> // 用於 usleep (動畫延遲)
#include <time.h>

// 定義圖檔名稱
#define FILE_STAGE_1 "AsciiText.txt"
#define FILE_STAGE_2 "2nd.txt" // 請建立這個檔案放第二階段的圖

// 遊戲參數
#define PLAYER_MAX_HP 100
#define BOSS_MAX_HP 20000

typedef struct {
    int boss_hp;
    int player_hp;
    int stage; // 1 或 2
    char log_msg[100];     // 戰鬥紀錄
    char dice_visual[100]; // 骰子顯示字串
} GameState;

GameState state;

// --- 讀取並顯示 ASCII 檔案 ---
void draw_boss_art() {
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
void draw_ui() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int center_x = cols / 2 - 25;
    int ui_y = rows - 7;

    // 1. Boss 資訊
    if (state.stage == 1)
        mvprintw(ui_y, center_x, "BOSS: SUCCUBUS QUEEN [NORMAL]");
    else
        attron(COLOR_PAIR(2)), mvprintw(ui_y, center_x, "BOSS: SUCCUBUS QUEEN [ENRAGED]"), attroff(COLOR_PAIR(2));

    // 2. Boss 血條
    ui_y++;
    mvprintw(ui_y, center_x - 4, "HP:");
    int bar_width = 50;
    float ratio = (float)state.boss_hp / BOSS_MAX_HP;
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
    mvprintw(ui_y + 1, 2, "[SPACE] Attack (Roll Dice)    [2] Force Phase 2    [Q] Quit");
    
    // 顯示骰子結果與 Log
    attron(A_BOLD);
    mvprintw(ui_y + 1, center_x, "%s", state.dice_visual); // 顯示骰子
    attroff(A_BOLD);
    
    mvprintw(ui_y + 1, cols - strlen(state.log_msg) - 2, "%s", state.log_msg);
}

// --- 動畫特效：骰子轉動 ---
void animate_dice_roll() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    
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

int main() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    srand(time(NULL));

    state.boss_hp = BOSS_MAX_HP;
    state.player_hp = PLAYER_MAX_HP;
    state.stage = 1;
    strcpy(state.log_msg, "Ready to fight!");
    strcpy(state.dice_visual, "Dice: [?] [?] [?]");

    int ch;
    while((ch = getch()) != 'q') {
        
        // 處理輸入
        if (ch == ' ') {
            // 1. 播放動畫 (這會卡住主執行緒 0.5秒，模擬等待)
            animate_dice_roll();
            
            // 2. 計算最終結果 (模擬 Server 回傳)
            int d1 = rand() % 6 + 1;
            int d2 = rand() % 6 + 1;
            int d3 = rand() % 6 + 1;
            int dmg = d1 + d2 + d3;
            
            state.boss_hp -= dmg * 10; // 放大傷害方便測試
            if(state.boss_hp < 0) state.boss_hp = 0;
            
            // 更新最終顯示文字
            sprintf(state.dice_visual, "Result: [%d] [%d] [%d]", d1, d2, d3);
            sprintf(state.log_msg, "Hit! -%d DMG", dmg * 10);
            
            // 自動切換階段邏輯 (例如血量低於 50%)
            if (state.boss_hp <= BOSS_MAX_HP / 2 && state.stage == 1) {
                state.stage = 2;
                strcpy(state.log_msg, "WARNING: PHASE 2!");
                // 這裡可以加個閃爍特效
                flash(); 
            }
        }
        
        // 手動切換測試
        if (ch == '2') {
            state.stage = 2;
            strcpy(state.log_msg, "Manual Switch: Phase 2");
        }

        // 繪圖迴圈
        erase();
        draw_boss_art();
        draw_ui();
        refresh();
    }

    endwin();
    return 0;
}