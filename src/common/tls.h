#ifndef COMMON_TLS_H
#define COMMON_TLS_H

#include <openssl/ssl.h>
#include <openssl/err.h>

// ============================================================================
// 共用函數（Client 和 Server 都使用）
// ============================================================================

// 初始化 OpenSSL 庫（必須在第一次使用 TLS 前呼叫）
void tls_init_openssl(void);

// 清理 OpenSSL 資源（程式結束前呼叫）
void tls_cleanup_openssl(void);

// 清理 SSL_CTX 資源
void tls_cleanup_context(SSL_CTX *ctx);

// 優雅關閉 SSL 連線
void tls_shutdown(SSL *ssl);

// 釋放 SSL 物件
void tls_free_ssl(SSL *ssl);

// ============================================================================
// Server 端專用函數
// ============================================================================

// 建立並配置 Server 端 SSL 上下文
// cert_file: 伺服器證書檔案路徑
// key_file:  伺服器私鑰檔案路徑
// 返回: SSL_CTX 物件，失敗返回 NULL
SSL_CTX *tls_create_server_context(const char *cert_file, const char *key_file);

// Server 端執行 TLS 握手
// ctx: SSL 上下文
// client_fd: 已接受的客戶端 socket 檔案描述符
// 返回: SSL 物件，失敗返回 NULL
SSL *tls_server_handshake(SSL_CTX *ctx, int client_fd);

// ============================================================================
// Client 端專用函數
// ============================================================================

// 建立並配置 Client 端 SSL 上下文
// ca_cert_file: CA 證書檔案路徑（用於驗證伺服器證書）
//               如果為 NULL，則不驗證伺服器證書（不安全，僅用於開發測試）
// 返回: SSL_CTX 物件，失敗返回 NULL
SSL_CTX *tls_create_client_context(const char *ca_cert_file);

// Client 端執行 TLS 握手
// ctx: SSL 上下文
// sockfd: 已連線的 socket 檔案描述符
// 返回: SSL 物件，失敗返回 NULL
SSL *tls_client_handshake(SSL_CTX *ctx, int sockfd);

// 驗證伺服器證書（Client 端專用）
// ssl: SSL 物件
// 返回: 0 表示驗證成功，-1 表示驗證失敗
int tls_verify_server_certificate(SSL *ssl);

// ============================================================================
// 工具函數
// ============================================================================

// 顯示 SSL 連線資訊（加密套件等）
void tls_print_connection_info(SSL *ssl);

#endif

