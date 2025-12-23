#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// --- VICTORY ASCII ART (金字招牌) ---
const char *VICTORY_ART[] = {
    " __      __  _____   _____   _______   ____    _____   __     __",
    " \\ \\    / / |_   _| / ____| |__   __| / __ \\  |  __ \\  \\ \\   / /",
    "  \\ \\  / /    | |  | |        | |    | |  | | | |__) |  \\ \\_/ / ",
    "   \\ \\/ /     | |  | |        | |    | |  | | |  _  /    \\   /  ",
    "    \\  /     _| |_ | |____    | |    | |__| | | | \\ \\     | |   ",
    "     \\/     |_____| \\_____|   |_|     \\____/  |_|  \\_\\    |_|   ",
    "                                                                  ",
    NULL
};

int main() {
    // 1. 初始化 ncurses
    initscr();
    cbreak();
    noecho();
    curs_set(0); // 隱藏游標
    start_color();

    // 2. 設定顏色
    if (has_colors()) {
        init_pair(1, COLOR_YELLOW, COLOR_BLACK); // 金色 (Victory)
        init_pair(2, COLOR_CYAN, COLOR_BLACK);   // 青色 (副標題)
        init_pair(3, COLOR_WHITE, COLOR_BLACK);  // 白色 (提示)
    }

    int timer = 0; // 用來控制閃爍動畫
    int ch;
    
    // 設定 getch 為非阻塞模式，讓動畫可以一直跑
    timeout(100); 

    // --- 主迴圈 ---
    while ((ch = getch()) != 'q') {
        erase(); // 清除畫面

        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        // --- A. 繪製 VICTORY 大字 ---
        attron(COLOR_PAIR(1) | A_BOLD); // 開啟金色粗體
        
        // 計算高度與置中
        int art_height = 0;
        while(VICTORY_ART[art_height] != NULL) art_height++;
        
        int start_y = (rows / 2) - (art_height / 2) - 2;
        
        for (int i = 0; VICTORY_ART[i] != NULL; i++) {
            int len = strlen(VICTORY_ART[i]);
            int x = (cols - len) / 2;
            mvprintw(start_y + i, x, "%s", VICTORY_ART[i]);
        }
        attroff(COLOR_PAIR(1) | A_BOLD);

        // --- B. 繪製閃爍的慶祝文字 ---
        // 每 5 個 frame (約 0.5秒) 切換一次顯示狀態
        if ((timer / 5) % 2 == 0) {
            attron(COLOR_PAIR(2) | A_BOLD);
            char *msg = "BOSS DEFEATED! LEGENDARY VICTORY!";
            mvprintw(start_y + art_height + 2, (cols - strlen(msg)) / 2, "%s", msg);
            attroff(COLOR_PAIR(2) | A_BOLD);
        }

        // --- C. 底部提示 ---
        attron(COLOR_PAIR(3));
        char *hint = "Press [Q] to Close";
        mvprintw(rows - 2, (cols - strlen(hint)) / 2, "%s", hint);
        attroff(COLOR_PAIR(3));

        refresh(); // 更新畫面
        timer++;
    }

    // 結束程式
    endwin();
    return 0;
}