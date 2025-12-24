#ifndef UI_LOGIN_H
#define UI_LOGIN_H

// 顯示登入畫面並取得玩家名稱
// 需在呼叫前已經初始化 ncurses（initscr/start_color 等）
// out_name: 用來儲存玩家名稱的緩衝區
// max_len:  緩衝區大小（包含結尾 '\0'）
void ui_login_get_player_name(char *out_name, int max_len);

#endif

