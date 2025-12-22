/* log_example.c - Log 系統使用範例 */
#include "log.h"
#include <unistd.h>

int main(void) {
    // 範例 1: 初始化日誌系統（輸出到 stderr，級別為 INFO）
    log_init(LOG_INFO, NULL);
    
    LOG_DEBUG("這條 DEBUG 訊息不會顯示（級別太低）");
    LOG_INFO("這是一條 INFO 訊息");
    LOG_WARN("這是一條 WARN 訊息");
    LOG_ERROR("這是一條 ERROR 訊息");
    
    // 範例 2: 動態調整日誌級別
    log_set_level(LOG_DEBUG);
    LOG_DEBUG("現在 DEBUG 訊息會顯示了");
    
    // 範例 3: 輸出到檔案
    log_cleanup();
    log_init(LOG_INFO, "test.log");
    LOG_INFO("這條訊息會寫入 test.log 檔案");
    
    // 範例 4: 使用格式化輸出
    int player_id = 123;
    const char *username = "Player1";
    LOG_INFO("Player %s (ID: %d) joined the game", username, player_id);
    LOG_ERROR("Failed to process attack: damage=%d, seq_num=%u", 50, 1001);
    
    // 清理
    log_cleanup();
    return 0;
}

