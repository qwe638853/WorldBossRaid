#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "login.h"

// --- 霸氣全大寫 ASCII LOGO ---
const char *TITLE_ART[] = {
    // [0-6] WORLD (紅色)
    "__          __  ____    _____    _       _____  ",
    "\\ \\        / /  / __ \\  |  __ \\  | |     |  __ \\ ",
    " \\ \\  /\\  / /  | |  | | | |__) | | |     | |  | |",
    "  \\ \\/  \\/ /   | |  | | |  _  /  | |     | |  | |",
    "   \\  /\\  /    | |__| | | | \\ \\  | |____ | |__| |",
    "    \\/  \\/     \\____/   |_|  \\_\\ |______||_____/ ",
    "                                                 ",
    // [7-13] BOSS (黃色)
    " ____     ____     ____    ____                  ",
    " |  _ \\   / __ \\   / ___|  / ___|                ",
    " | |_) | | |  | |  \\___ \\  \\___ \\                ",
    " |  _ <  | |  | |   ___) |  ___) |               ",
    " | |_) | | |__| |  |____/  |____/                ",
    " |____/   \\____/                                 ",
    "                                                 ",
    // [14-20] RAID (藍色)
    " _____           _   _____                       ",
    " |  __ \\   /\\    | | |  __ \\                     ",
    " | |__) | /  \\   | | | |  | |                    ",
    " |  _  / / /\\ \\  | | | |  | |                    ",
    " | | \\ \\/ ____ \\ |_| | |__| |                    ",
    " |_|  \\/_/    \\_\\(_) |_____/                     ",
    NULL
};

// --- 真・巨劍 (修正劍柄對齊) ---
// 修正說明：原本劍柄 | | 偏左，現在往右移了一格，對齊劍尖
const char *BIG_SWORD_ART[] = {
    "      ^      ", 
    "     / \\     ",
    "    /   \\    ",
    "   /     \\   ",
    "  |       |  ",
    "  |   |   |  ",
    "  |   |   |  ",
    "  |   |   |  ",
    "  |   |   |  ",
    "  |   |   |  ",
    "  |   |   |  ",
    "   \\     /   ",
    "    \\   /    ",
    "     \\ /     ",
    "  .--' '--.  ", 
    " /         \\ ",
    "     | |     ", // <-- 這裡往右移了一格 (原本是4個空白，改為5個)
    "     | |     ",
    "     | |     ",
    "     | |     ", 
    "     | |     ",
    "    /___\\    ",
    NULL
};

// 計算 Logo 高度
static int get_logo_height() {
    int h = 0;
    while(TITLE_ART[h] != NULL) h++;
    return h;
}

// 計算 Logo 最寬處
static int get_logo_width() {
    int w = 0;
    for (int i = 0; TITLE_ART[i] != NULL; i++) {
        int len = strlen(TITLE_ART[i]);
        if (len > w) w = len;
    }
    return w;
}

// 進場動畫
static void play_intro_animation() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    
    if (has_colors()) {
        init_pair(10, COLOR_GREEN, COLOR_BLACK);
        attron(COLOR_PAIR(10));
    }

    // 稍微加快動畫速度，減少等待感
    for (int frame = 0; frame < 20; frame++) {
        for (int i = 0; i < 40; i++) { 
            int y = rand() % rows;
            int x = rand() % cols;
            char ch = (rand() % 2) ? '1' : '0';
            if (rand() % 15 == 0) ch = '|';
            mvaddch(y, x, ch);
        }
        refresh();
        usleep(15000); 
    }
    
    if (has_colors()) attroff(COLOR_PAIR(10));
    clear();
    refresh();
}

// 繪製 Logo (紅 -> 黃 -> 藍)
static void draw_logo(int start_y) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int logo_width = get_logo_width();
    int start_x = (cols / 2) - (logo_width / 2);
    if (start_x < 0) start_x = 0;

    if (has_colors()) {
        init_pair(1, COLOR_RED, COLOR_BLACK);    // WORLD
        init_pair(4, COLOR_YELLOW, COLOR_BLACK); // BOSS
        init_pair(5, COLOR_BLUE, COLOR_BLACK);   // RAID
    }

    for (int i = 0; TITLE_ART[i] != NULL; i++) {
        int color_pair = 0;
        if (i <= 6) color_pair = 1;
        else if (i <= 13) color_pair = 4;
        else color_pair = 5;

        if (has_colors()) attron(COLOR_PAIR(color_pair) | A_BOLD);
        mvprintw(start_y + i, start_x, "%s", TITLE_ART[i]);
        if (has_colors()) attroff(COLOR_PAIR(color_pair) | A_BOLD);
    }
}

// 繪製裝飾 (巨劍)
static void draw_decorations(int start_y) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int logo_width = get_logo_width();
    int logo_left_edge = (cols / 2) - (logo_width / 2); 
    int logo_right_edge = logo_left_edge + logo_width; 
    
    int sword_width = 13; 
    // 這裡將 padding 從 4 改為 8，讓劍離文字稍微遠一點點，更有氣勢
    int padding = 8; 

    int left_sword_x = logo_left_edge - padding - sword_width;
    int right_sword_x = logo_right_edge + padding;

    if (has_colors()) {
        init_pair(3, COLOR_CYAN, COLOR_BLACK); 
        attron(COLOR_PAIR(3) | A_BOLD);
    }

    for (int i = 0; BIG_SWORD_ART[i] != NULL; i++) {
        if (start_y + i >= rows) break;
        
        if (left_sword_x > 0) 
            mvprintw(start_y + i, left_sword_x, "%s", BIG_SWORD_ART[i]);
        
        if (right_sword_x < cols - sword_width) 
            mvprintw(start_y + i, right_sword_x, "%s", BIG_SWORD_ART[i]);
    }

    if (has_colors()) attroff(COLOR_PAIR(3) | A_BOLD);
}

// 繪製輸入框
static void draw_input_box(int y, int width, char *buffer) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int x = (cols / 2) - (width / 2); 

    if (has_colors()) {
        init_pair(2, COLOR_WHITE, COLOR_BLUE); 
        attron(COLOR_PAIR(2));
    }

    mvhline(y - 1, x, ACS_HLINE, width);
    mvaddch(y - 1, x - 1, ACS_ULCORNER);
    mvaddch(y - 1, x + width, ACS_URCORNER);

    mvprintw(y, x - 1, "%c", ACS_VLINE);
    for(int i=0; i<width; i++) addch(' '); 
    addch(ACS_VLINE);

    mvhline(y + 1, x, ACS_HLINE, width);
    mvaddch(y + 1, x - 1, ACS_LLCORNER);
    mvaddch(y + 1, x + width, ACS_LRCORNER);

    if (has_colors()) attroff(COLOR_PAIR(2)); 
    
    attron(A_BOLD);
    char *prompt = "ENTER YOUR NAME:";
    int prompt_x = (cols / 2) - (strlen(prompt) / 2);
    
    if (strlen(buffer) == 0) {
        for(int i=0; i<strlen(prompt); i++) {
            mvaddch(y - 2, prompt_x + i, prompt[i]);
            refresh();
            usleep(25000); 
        }
    } else {
        mvprintw(y - 2, prompt_x, "%s", prompt);
    }
    attroff(A_BOLD);

    if (has_colors()) attron(COLOR_PAIR(2));

    echo();   
    curs_set(1); 
    move(y, x + 1); 
    getnstr(buffer, width - 2); 
    noecho();
    curs_set(0);
}

// 對外接口
void ui_login_get_player_name(char *out_name, int max_len) {
    if (!out_name || max_len <= 0) return;

    play_intro_animation();

    clear();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int logo_h = get_logo_height();
    int input_box_h = 3; 
    int padding = 4; 
    int total_content_height = logo_h + padding + input_box_h;
    
    int start_y = (rows - total_content_height) / 2;
    if (start_y < 0) start_y = 0;

    draw_logo(start_y);
    draw_decorations(start_y);
    
    int box_y = start_y + logo_h + padding;
    char player_name[50] = {0};
    int box_width = 30;
    draw_input_box(box_y, box_width, player_name);

    if (strlen(player_name) == 0) strcpy(player_name, "Unknown");

    attron(A_BOLD | A_BLINK | COLOR_PAIR(4)); 
    char welcome_msg[100];
    sprintf(welcome_msg, ">> WELCOME, Player %s <<", player_name);
    mvprintw(box_y + 3, (cols - strlen(welcome_msg))/2, "%s", welcome_msg);
    attroff(A_BOLD | A_BLINK | COLOR_PAIR(4));
    
    refresh();
    sleep(1);

    strncpy(out_name, player_name, max_len - 1);
    out_name[max_len - 1] = '\0';
}

#ifdef BUILD_TEST
int main() {
    initscr();
    cbreak();
    start_color();
    srand(time(NULL));

    char name[50];
    ui_login_get_player_name(name, 50);

    endwin();
    return 0;
}
#endif