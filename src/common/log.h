#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdbool.h>

// 日誌級別（從低到高）
typedef enum {
    LOG_DEBUG = 0,  // 除錯資訊（最詳細）
    LOG_INFO  = 1,  // 一般資訊
    LOG_WARN  = 2,  // 警告
    LOG_ERROR = 3,  // 錯誤
    LOG_FATAL = 4   // 嚴重錯誤（最高）
} LogLevel;

// 初始化日誌系統
// level: 設定日誌級別（只輸出該級別及以上的日誌）
// output_file: 輸出檔案（NULL 表示輸出到 stderr）
void log_init(LogLevel level, const char *output_file);

// 設定日誌級別（執行時動態調整）
void log_set_level(LogLevel level);

// 取得當前日誌級別
LogLevel log_get_level(void);

// 關閉日誌系統（關閉檔案等）
void log_cleanup(void);

// 核心日誌函數（內部使用）
void log_write(LogLevel level, const char *file, int line, const char *func, const char *fmt, ...);

// 便利宏定義（自動包含檔案名、行號、函數名）
#define LOG_DEBUG(fmt, ...) log_write(LOG_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_write(LOG_INFO,  __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_write(LOG_WARN,  __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(LOG_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) log_write(LOG_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#endif

