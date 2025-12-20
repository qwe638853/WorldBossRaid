#ifndef TLS_MODULE_H
#define TLS_MODULE_H

#include <openssl/ssl.h>
#include <openssl/err.h>

// 初始化 OpenSSL 庫
void init_openssl();

// 建立並配置 SSL 上下文 (Context)
SSL_CTX *create_tls_context(const char *cert_file, const char *key_file);

// 執行 TLS 握手並返回 SSL 物件
SSL *perform_tls_handshake(SSL_CTX *ctx, int client_fd);

// 清理資源
void cleanup_tls(SSL_CTX *ctx);

#endif