/* src/common/log.c - 多級別日誌系統 */
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>  // for isatty()

// 內部狀態
static LogLevel g_log_level = LOG_INFO;  // 預設為 INFO
static FILE *g_log_file = NULL;          // 日誌檔案（NULL 表示使用 stderr）
static bool g_log_initialized = false;

// 日誌級別名稱
static const char *log_level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

// 日誌級別顏色（ANSI 顏色碼，用於終端機）
static const char *log_level_colors[] = {
    "\033[36m",  // DEBUG: 青色
    "\033[32m",  // INFO:  綠色
    "\033[33m",  // WARN:  黃色
    "\033[31m",  // ERROR: 紅色
    "\033[35m"   // FATAL: 洋紅色
};
static const char *log_reset_color = "\033[0m";

// 初始化日誌系統
void log_init(LogLevel level, const char *output_file) {
    g_log_level = level;
    
    if (output_file) {
        g_log_file = fopen(output_file, "a");  // 追加模式
        if (!g_log_file) {
            fprintf(stderr, "[Log] Failed to open log file: %s (%s)\n", 
                    output_file, strerror(errno));
            g_log_file = stderr;  // 失敗時使用 stderr
        }
    } else {
        g_log_file = stderr;  // 預設輸出到 stderr
    }
    
    g_log_initialized = true;
    LOG_INFO("Log system initialized (level: %s)", log_level_names[level]);
}

// 設定日誌級別
void log_set_level(LogLevel level) {
    g_log_level = level;
}

// 取得當前日誌級別
LogLevel log_get_level(void) {
    return g_log_level;
}

// 關閉日誌系統
void log_cleanup(void) {
    if (g_log_file && g_log_file != stderr && g_log_file != stdout) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_log_initialized = false;
}

// 取得檔案名（不含路徑）
static const char *get_filename(const char *filepath) {
    const char *filename = strrchr(filepath, '/');
    return filename ? filename + 1 : filepath;
}

// 核心日誌函數
void log_write(LogLevel level, const char *file, int line, const char *func, const char *fmt, ...) {
    // 如果日誌級別低於設定級別，不輸出
    if (level < g_log_level) {
        return;
    }
    
    // 如果未初始化，使用預設設定
    if (!g_log_initialized) {
        g_log_file = stderr;
        g_log_level = LOG_INFO;
    }
    
    // 取得當前時間
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // 取得檔案名（不含路徑）
    const char *filename = get_filename(file);
    
    // 判斷是否為終端機輸出（用於決定是否使用顏色）
    bool is_tty = (g_log_file == stderr || g_log_file == stdout) && isatty(fileno(g_log_file));
    
    // 輸出日誌前綴
    if (is_tty) {
        fprintf(g_log_file, "%s[%s]%s %s [%s:%d] %s(): ", 
                log_level_colors[level],
                log_level_names[level],
                log_reset_color,
                time_str,
                filename,
                line,
                func);
    } else {
        fprintf(g_log_file, "[%s] %s [%s:%d] %s(): ", 
                log_level_names[level],
                time_str,
                filename,
                line,
                func);
    }
    
    // 輸出日誌內容
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);
    
    fprintf(g_log_file, "\n");
    fflush(g_log_file);  // 立即刷新，確保日誌及時寫入
}

