#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include "login.h"
// --- 霸氣全大寫 ASCII LOGO (WORLD BOSS RAID) ---
const char *TITLE_ART[] = {
    "__          __   ____    _____    _       _____  ",
    "\\ \\        / /  / __ \\  |  __ \\  | |     |  __ \\ ",
    " \\ \\  /\\  / /  | |  | | | |__) | | |     | |  | |",
    "  \\ \\/  \\/ /   | |  | | |  _  /  | |     | |  | |",
    "   \\  /\\  /    | |__| | | | \\ \\  | |____ | |__| |",
    "    \\/  \\/      \\____/  |_|  \\_\\ |______||_____/ ",
    "                                                     ",
    "  ____     ____     ____    ____                     ",
    " |  _ \\   / __ \\   / ___|  / ___|                    ",
    " | |_) | | |  | |  \\___ \\  \\___ \\                    ",
    " |  _ <  | |  | |   ___) |  ___) |                   ",
    " | |_) | | |__| |  |____/  |____/                    ",
    " |____/   \\____/                                     ",
    "                                                     ",
    "  _____           _   _____                          ",
    " |  __ \\   /\\    | | |  __ \\                         ",
    " | |__) | /  \\   | | | |  | |                        ",
    " |  _  / / /\\ \\  | | | |  | |                        ",
    " | | \\ \\/ ____ \\ |_| | |__| |                        ",
    " |_|  \\/_/    \\_\\(_) |_____/                         ",
    NULL
};

// 計算 Logo 高度
static int get_logo_height() {
    int h = 0;
    while(TITLE_ART[h] != NULL) h++;
    return h;
}

// 繪製 Logo (紅色 + 置中)
static void draw_logo(int start_y) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // 1. 計算 Logo 最大寬度 (為了水平置中)
    int max_width = 0;
    for (int i = 0; TITLE_ART[i] != NULL; i++) {
        int len = strlen(TITLE_ART[i]);
        if (len > max_width) max_width = len;
    }
    int start_x = (cols - max_width) / 2;
    if (start_x < 0) start_x = 0;

    // 2. 設定紅色
    if (has_colors()) {
        init_pair(1, COLOR_RED, COLOR_BLACK); 
        attron(COLOR_PAIR(1) | A_BOLD);
    }

    // 3. 繪製
    for (int i = 0; TITLE_ART[i] != NULL; i++) {
        mvprintw(start_y + i, start_x, "%s", TITLE_ART[i]);
    }

    if (has_colors()) attroff(COLOR_PAIR(1) | A_BOLD);
}

// 繪製輸入框
static void draw_input_box(int y, int width, char *buffer) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int x = (cols - width) / 2; // 水平置中

    if (has_colors()) {
        init_pair(2, COLOR_WHITE, COLOR_BLUE); 
        attron(COLOR_PAIR(2));
    }

    // 畫框 (上邊框)
    mvhline(y - 1, x, ACS_HLINE, width);
    mvaddch(y - 1, x - 1, ACS_ULCORNER);
    mvaddch(y - 1, x + width, ACS_URCORNER);

    // 中間填色
    move(y, x - 1);
    addch(ACS_VLINE);
    for(int i=0; i<width; i++) addch(' '); 
    addch(ACS_VLINE);

    // 下邊框
    mvhline(y + 1, x, ACS_HLINE, width);
    mvaddch(y + 1, x - 1, ACS_LLCORNER);
    mvaddch(y + 1, x + width, ACS_LRCORNER);

    // 提示文字 (放在框框正上方)
    mvprintw(y - 2, x, "ENTER YOUR NAME:");

    if (has_colors()) attroff(COLOR_PAIR(2));

    echo();   
    curs_set(1); 
    
    // 游標定位
    move(y, x + 2); 
    getnstr(buffer, 12); 

    noecho();
    curs_set(0);
}

void ui_login_get_player_name(char *out_name, int max_len) {
    if (!out_name || max_len <= 0) return;

    // 假設外部已經呼叫過 initscr()/start_color()
    clear();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // --- 關鍵計算：垂直置中 (Midline Alignment) ---
    int logo_h = get_logo_height();
    int input_box_h = 3; // 輸入框高度
    
    // --- 修改處：加大間距 ---
    int padding = 4;     // 原本是 2，現在改成 6，讓輸入框往下移
    
    int total_content_height = logo_h + padding + input_box_h;
    
    // 起始 Y 座標 = (螢幕高度 - 內容總高度) / 2
    int start_y = (rows - total_content_height) / 2;
    
    // 防止視窗太小時跑出邊界
    if (start_y < 0) start_y = 0;

    // 1. 畫 Logo
    draw_logo(start_y);
    
    // 2. 畫輸入框 (在 Logo 下方)
    int box_y = start_y + logo_h + padding;
    
    char player_name[50] = {0};
    draw_input_box(box_y, 30, player_name);

    // 預設名字
    if (strlen(player_name) == 0) {
        strcpy(player_name, "Unknown Hero");
    }

    // 3. 歡迎訊息
    attron(A_BOLD | A_BLINK);
    char welcome_msg[100];
    sprintf(welcome_msg, "WELCOME, %s!", player_name);
    mvprintw(box_y + 3, (cols - strlen(welcome_msg))/2, "%s", welcome_msg);
    attroff(A_BOLD | A_BLINK);
    
    refresh();
    sleep(1);

    // 將名稱複製給呼叫者
    strncpy(out_name, player_name, max_len - 1);
    out_name[max_len - 1] = '\0';
}