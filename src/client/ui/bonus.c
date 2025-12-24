#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "bonus.h"

// --- 炸彈與爆擊 ASCII ART ---
static const char *CRIT_ART[] = {
    "          _ ._  _ , _ ._        ",
    "        (_ ' ( `  )_  .__)      ",
    "      ( (  (    )   `)  ) _)    ",
    "     (__ (_   (_ . _) _) ,__)   ",
    "           `~~`\\ ' . /`~~`      ",
    "                ;   ;           ",
    "                /   \\           ",
    "  _____________/_ __ \\_____________ ",
    " |                                 |",
    " |   CRITICAL   DAMAGE   !!!       |",
    " |_________________________________|",
    "           \\_/__\\_/             ",
    "                                    ",
    "     ____  ____  _____ _______ !    ",
    "    / ___||  _ \\|_   _|__   __|!    ",
    "   | |    | |_) | | |    | |   !    ",
    "   | |___ |  _ < _| |_   | |   !    ",
    "    \\____||_| \\_\\_____|  |_|   !    ",
    NULL
};

// --- 爆炸粒子結構 ---
typedef struct {
    int y, x;
    int life;
    char ch;
} Particle;

#define MAX_PARTICLES 50

void ui_show_bonus_screen(void) {
    // 假設外部已經初始化過 ncurses

    // 設定爆炸配色
    if (has_colors()) {
        init_pair(1, COLOR_RED, COLOR_BLACK);    // 爆炸紅
        init_pair(2, COLOR_YELLOW, COLOR_BLACK); // 火焰黃
        init_pair(3, COLOR_WHITE, COLOR_BLACK);  // 煙霧白
    }

    int timer = 0;
    int ch;
    timeout(50); // 加快動畫速度，讓爆炸感更強

    // 粒子系統初始化
    Particle particles[MAX_PARTICLES];
    for(int i=0; i<MAX_PARTICLES; i++) particles[i].life = 0;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    while ((ch = getch()) != ' ') { // 按空白鍵繼續
        erase();
        getmaxyx(stdscr, rows, cols); // 視窗大小可能改變

        // 1. 計算圖案位置
        int art_height = 0;
        while(CRIT_ART[art_height] != NULL) art_height++;
        int start_y = (rows - art_height) / 2;
        
        // 2. 繪製炸彈與 CRIT 字樣 (紅黃閃爍)
        int color_pair = (timer % 2 == 0) ? 1 : 2; 
        attron(COLOR_PAIR(color_pair) | A_BOLD);

        for (int i = 0; CRIT_ART[i] != NULL; i++) {
            int len = strlen(CRIT_ART[i]);
            int x = (cols - len) / 2;
            mvprintw(start_y + i, x, "%s", CRIT_ART[i]);
        }
        attroff(COLOR_PAIR(color_pair) | A_BOLD);

        // 3. 爆炸粒子特效
        // 每幀產生新粒子
        for(int k=0; k<5; k++) {
            int idx = rand() % MAX_PARTICLES;
            if (particles[idx].life <= 0) {
                particles[idx].y = start_y + rand() % art_height; // 在圖案周圍爆炸
                particles[idx].x = (cols/2) + (rand() % 40) - 20;
                particles[idx].life = rand() % 10 + 5; // 存活時間
                char symbols[] = "*#@!.";
                particles[idx].ch = symbols[rand() % 5];
            }
        }

        // 繪製與更新粒子
        for(int i=0; i<MAX_PARTICLES; i++) {
            if (particles[i].life > 0) {
                // 隨機選色
                int p_color = (rand() % 3) + 1; 
                attron(COLOR_PAIR(p_color));
                mvaddch(particles[i].y, particles[i].x, particles[i].ch);
                attroff(COLOR_PAIR(p_color));

                // 粒子擴散運動
                particles[i].y += (rand() % 3) - 1; 
                particles[i].x += (rand() % 3) - 1;
                particles[i].life--;
            }
        }

        // 4. 底部震撼提示
        if (timer % 4 == 0) { // 慢速閃爍
            attron(A_REVERSE | A_BOLD);
            char *hint = " MASSIVE DAMAGE DEALT! ";
            mvprintw(rows - 4, (cols - strlen(hint))/2, "%s", hint);
            attroff(A_REVERSE | A_BOLD);
        }

        mvprintw(rows - 2, (cols - 25)/2, "PRESS [SPACE] TO CONTINUE");

        refresh();
        timer++;
    }
}