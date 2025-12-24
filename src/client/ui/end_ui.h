#ifndef UI_END_UI_H
#define UI_END_UI_H

// 顯示最終勝利畫面（兩隻 Boss 都被打敗）
// winner_name: 擊殺最後一隻 Boss 的玩家名稱（可為空字串）
// 需在外部已經初始化 ncurses
// 函式會阻塞，直到玩家按下 Q 才返回
void ui_show_victory_screen(const char *winner_name);

#endif

