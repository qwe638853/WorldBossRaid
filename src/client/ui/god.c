#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "god.h"

// --- 霸氣立體皇冠 ASCII ART ---
// 特點：
// 1. 加寬加高，佔據更多畫面
// 2. 底部使用了 \___/ 的透視畫法，營造 3D 厚重感
// 3. 頂部寶石加大
static const char *GOD_ART[] = {
    "               .+.                   .+.                   .+.               ",
    "             '.   .'               '.   .'               '.   .'             ",
    "            (  `.'  )             (  `.'  )             (  `.'  )            ",
    "             \\  |  /               \\  |  /               \\  |  /             ",
    "              \\ | /                 \\ | /                 \\ | /              ",
    "               \\|/                   \\|/                   \\|/               ",
    "               / \\                   / \\                   / \\               ",
    "              /   \\                 /   \\                 /   \\              ",
    "             /     \\               /     \\               /     \\             ",
    "        ____/       \\_____________/       \\_____________/       \\____        ",
    "       /                                                                 \\       ",
    "      |      [+]           [+]           [+]           [+]      |      ",
    "      |                                                                 |      ",
    "       \\_______________________________________________________________/       ",
    "        \\_____________________________________________________________/        ",
    "         \\___________________________________________________________/         ",
    NULL
};

// --- 星星/落粉粒子結構 ---
typedef struct {
    float y, x;
    float speed;
    char ch;
    int color_id;
} Star;

#define MAX_STARS 120 // 稍微增加星星數量以配合大皇冠

void ui_show_god_screen(void) {
    // 1. 設定金色配色
    if (has_colors()) {
        init_pair(10, COLOR_YELLOW, COLOR_BLACK); // 亮金色 (星星/皇冠)
        init_pair(11, COLOR_WHITE, COLOR_BLACK);  // 亮白色 (閃爍點綴)
    }

    int timer = 0;
    int ch;
    timeout(50); 

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // 2. 粒子系統初始化
    Star stars[MAX_STARS];
    for(int i=0; i<MAX_STARS; i++) {
        stars[i].y = (float)(rand() % rows);
        stars[i].x = (float)(rand() % cols);
        stars[i].speed = (float)((rand() % 5) + 2) / 10.0;
        char shapes[] = "*+..'"; 
        stars[i].ch = shapes[rand() % 5];
        stars[i].color_id = (rand() % 2 == 0) ? 10 : 11;
    }

    // 3. 動畫迴圈
    while ((ch = getch()) != ' ') { 
        erase();
        getmaxyx(stdscr, rows, cols);

        // A. 繪製落星背景
        for(int i=0; i<MAX_STARS; i++) {
            attron(COLOR_PAIR(stars[i].color_id));
            if (stars[i].color_id == 10) attron(A_BOLD);

            mvaddch((int)stars[i].y, (int)stars[i].x, stars[i].ch);

            if (stars[i].color_id == 10) attroff(A_BOLD);
            attroff(COLOR_PAIR(stars[i].color_id));

            stars[i].y += stars[i].speed;
            if (stars[i].y >= rows) {
                stars[i].y = 0;
                stars[i].x = rand() % cols;
                stars[i].speed = (float)((rand() % 5) + 2) / 10.0;
            }
        }

        // B. 繪製霸氣皇冠 (置中)
        int art_height = 0;
        while(GOD_ART[art_height] != NULL) art_height++;
        int center_y = rows / 2;
        // 因為皇冠很大，稍微向上偏移一點，讓文字有空間
        int start_art_y = center_y - (art_height / 2) - 3; 

        attron(COLOR_PAIR(10) | A_BOLD); // 金色粗體
        for (int i = 0; GOD_ART[i] != NULL; i++) {
            int len = strlen(GOD_ART[i]);
            int x = (cols - len) / 2;
            mvprintw(start_art_y + i, x, "%s", GOD_ART[i]);
        }
        attroff(COLOR_PAIR(10) | A_BOLD);

        // C. 繪製核心文字
        char *msg = "Congratulations, you are God's chosen one!!!";
        int msg_x = (cols - strlen(msg)) / 2;
        int msg_y = start_art_y + art_height + 2; // 在皇冠下方顯示
        int text_color = (timer % 10 < 5) ? 10 : 11; 
        
        attron(COLOR_PAIR(text_color) | A_BOLD | A_BLINK);
        
        // 文字裝飾線
        mvhline(msg_y - 1, msg_x - 4, '-', strlen(msg) + 8);
        mvhline(msg_y + 1, msg_x - 4, '-', strlen(msg) + 8);
        
        mvprintw(msg_y, msg_x, "%s", msg);
        attroff(COLOR_PAIR(text_color) | A_BOLD | A_BLINK);

        // D. 底部提示
        attron(A_DIM);
        char *hint = " [ DIVINE INTERVENTION ACTIVE ] - PRESS SPACE ";
        mvprintw(rows - 2, (cols - strlen(hint))/2, "%s", hint);
        attroff(A_DIM);

        refresh();
        timer++;
    }
}