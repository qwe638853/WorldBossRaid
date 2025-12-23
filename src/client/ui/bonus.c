#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// --- LUCKY MAN ASCII ART ---
const char *LUCKY_ART[] = {
    " _    _   _   _____  _  __ __     __",
    "| |  | | | | / ____|| |/ / \\ \\   / /",
    "| |  | | | || |     | ' /   \\ \\_/ / ",
    "| |__| |_| || |____ | . \\    \\   /  ",
    "|_____|\\___/ \\_____||_|\\_\\    |_|   ",
    "                                    ",
    " __  __          _   _ ",
    "|  \\/  |   /\\   | \\ | |",
    "| \\  / |  /  \\  |  \\| |",
    "| |\\/| | / /\\ \\ | . ` |",
    "| |  | |/ ____ \\| |\\  |",
    "|_|  |_/_/    \\_\\_| \\_|",
    NULL
};

int main() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    srand(time(NULL));

    // 設定多種鮮豔顏色
    if (has_colors()) {
        init_pair(1, COLOR_YELLOW, COLOR_BLACK); // 金色
        init_pair(2, COLOR_CYAN, COLOR_BLACK);   // 青色
        init_pair(3, COLOR_MAGENTA, COLOR_BLACK);// 紫色
        init_pair(4, COLOR_GREEN, COLOR_BLACK);  // 綠色
        init_pair(5, COLOR_RED, COLOR_BLACK);    // 紅色 (CRITICAL)
    }

    int timer = 0;
    int ch;
    timeout(100); // 動畫速度

    while ((ch = getch()) != ' ') { // 按空白鍵繼續
        erase();
        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        // 1. 繪製 LUCKY MAN (顏色會不斷變化)
        int color_idx = (timer % 4) + 1; // 1~4 循環
        attron(COLOR_PAIR(color_idx) | A_BOLD);

        int art_height = 0;
        while(LUCKY_ART[art_height] != NULL) art_height++;
        int start_y = (rows / 2) - (art_height / 2) - 4;

        for (int i = 0; LUCKY_ART[i] != NULL; i++) {
            int len = strlen(LUCKY_ART[i]);
            int x = (cols - len) / 2;
            mvprintw(start_y + i, x, "%s", LUCKY_ART[i]);
        }
        attroff(COLOR_PAIR(color_idx) | A_BOLD);

        // 2. 顯示恭喜訊息 (閃爍)
        if (timer % 2 == 0) {
            attron(COLOR_PAIR(5) | A_BOLD); // 紅色
            char *msg1 = "!!! CONGRATULATIONS !!!";
            mvprintw(start_y + art_height + 2, (cols - strlen(msg1))/2, "%s", msg1);
            attroff(COLOR_PAIR(5) | A_BOLD);
        }

        // 3. 顯示高額傷害提示
        attron(A_BOLD);
        char *msg2 = "CRITICAL HIT! MASSIVE DAMAGE DEALT!";
        mvprintw(start_y + art_height + 4, (cols - strlen(msg2))/2, "%s", msg2);
        attroff(A_BOLD);

        // 4. 背景隨機噴撒 "$", "*", "!" 符號
        attron(COLOR_PAIR(1)); // 金色符號
        for(int k=0; k<10; k++) {
            int ry = rand() % rows;
            int rx = rand() % cols;
            // 避開中間區域 (簡單判斷)
            if (ry < start_y || ry > start_y + art_height + 5) {
                char symbol = (rand() % 2 == 0) ? '$' : '*';
                mvaddch(ry, rx, symbol);
            }
        }
        attroff(COLOR_PAIR(1));

        // 5. 底部提示
        mvprintw(rows - 2, (cols - 25)/2, "PRESS [SPACE] TO CONTINUE");

        refresh();
        timer++;
    }

    endwin();
    return 0;
}