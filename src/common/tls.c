/* src/common/tls.c - 共用的 TLS/SSL 函式庫 */
#include "tls.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/err.h>

// ============================================================================
// 共用函數實作
// ============================================================================

// 初始化 OpenSSL 庫
void tls_init_openssl(void) {
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
}

// 清理 OpenSSL 資源
void tls_cleanup_openssl(void) {
    EVP_cleanup();
}

// 清理 SSL_CTX 資源
void tls_cleanup_context(SSL_CTX *ctx) {
    if (ctx) {
        SSL_CTX_free(ctx);
    }
}

// 優雅關閉 SSL 連線
void tls_shutdown(SSL *ssl) {
    if (ssl) {
        SSL_shutdown(ssl);
    }
}

// 釋放 SSL 物件
void tls_free_ssl(SSL *ssl) {
    if (ssl) {
        SSL_free(ssl);
    }
}

// ============================================================================
// Server 端專用函數實作
// ============================================================================

// 建立並配置 Server 端 SSL 上下文
SSL_CTX *tls_create_server_context(const char *cert_file, const char *key_file) {
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    
    if (!ctx) {
        LOG_ERROR("Error creating SSL context");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 載入伺服器證書
    if (!SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM)) {
        LOG_ERROR("Error loading certificate: %s", cert_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // 載入伺服器私鑰
    if (!SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM)) {
        LOG_ERROR("Error loading private key: %s", key_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // 驗證私鑰與證書是否匹配
    if (!SSL_CTX_check_private_key(ctx)) {
        LOG_ERROR("Private key does not match certificate");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    LOG_INFO("SSL context created successfully (cert: %s, key: %s)", cert_file, key_file);
    return ctx;
}

// Server 端執行 TLS 握手
SSL *tls_server_handshake(SSL_CTX *ctx, int client_fd) {
    if (!ctx) {
        LOG_ERROR("Invalid SSL context");
        return NULL;
    }

    // 創建新的 SSL 物件
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        LOG_ERROR("Error creating SSL object");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 將 socket 綁定到 SSL 物件
    if (SSL_set_fd(ssl, client_fd) != 1) {
        LOG_ERROR("Error setting socket file descriptor");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    // 執行 TLS 握手（Server 端使用 SSL_accept）
    LOG_DEBUG("Performing TLS handshake (server side)...");
    int ret = SSL_accept(ssl);
    
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        LOG_ERROR("TLS handshake failed (error code: %d)", err);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    LOG_INFO("TLS handshake successful");
    tls_print_connection_info(ssl);

    return ssl;
}

// ============================================================================
// Client 端專用函數實作
// ============================================================================

// 建立並配置 Client 端 SSL 上下文
SSL_CTX *tls_create_client_context(const char *ca_cert_file) {
    // 使用 TLS 客戶端方法
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    
    if (!ctx) {
        LOG_ERROR("Error creating SSL context");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 如果提供了 CA 證書檔案，載入它來驗證伺服器證書
    if (ca_cert_file) {
        if (!SSL_CTX_load_verify_locations(ctx, ca_cert_file, NULL)) {
            LOG_ERROR("Error loading CA certificate: %s", ca_cert_file);
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ctx);
            return NULL;
        }
        
        // 設定驗證模式：驗證伺服器證書
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        LOG_INFO("CA certificate loaded. Server certificate will be verified (CA: %s)", ca_cert_file);
    } else {
        // 沒有提供 CA 證書，不驗證伺服器（僅用於開發測試）
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
        LOG_WARN("No CA certificate provided. Server certificate will NOT be verified!");
        LOG_WARN("This is insecure and should only be used for development/testing");
    }

    return ctx;
}

// Client 端執行 TLS 握手
SSL *tls_client_handshake(SSL_CTX *ctx, int sockfd) {
    if (!ctx) {
        LOG_ERROR("Invalid SSL context");
        return NULL;
    }

    // 創建新的 SSL 物件
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        LOG_ERROR("Error creating SSL object");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 將 socket 綁定到 SSL 物件
    if (SSL_set_fd(ssl, sockfd) != 1) {
        LOG_ERROR("Error setting socket file descriptor");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    // 執行 TLS 握手（客戶端使用 SSL_connect）
    LOG_DEBUG("Performing TLS handshake (client side)...");
    int ret = SSL_connect(ssl);
    
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        LOG_ERROR("TLS handshake failed (error code: %d)", err);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    LOG_INFO("TLS handshake successful");
    tls_print_connection_info(ssl);

    return ssl;
}

// 驗證伺服器證書（Client 端專用）
int tls_verify_server_certificate(SSL *ssl) {
    if (!ssl) {
        return -1;
    }

    // 取得伺服器證書
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        LOG_ERROR("No server certificate received");
        return -1;
    }

    // 檢查證書驗證結果
    long verify_result = SSL_get_verify_result(ssl);
    if (verify_result != X509_V_OK) {
        LOG_ERROR("Certificate verification failed: %s",
                  X509_verify_cert_error_string(verify_result));
        X509_free(cert);
        return -1;
    }

    // 顯示證書資訊（可選）
    char *subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    
    if (subject) {
        LOG_DEBUG("Server certificate subject: %s", subject);
        OPENSSL_free(subject);
    }
    if (issuer) {
        LOG_DEBUG("Server certificate issuer: %s", issuer);
        OPENSSL_free(issuer);
    }

    X509_free(cert);
    LOG_INFO("Server certificate verified successfully");
    return 0;
}

// ============================================================================
// 工具函數實作
// ============================================================================

// 顯示 SSL 連線資訊
void tls_print_connection_info(SSL *ssl) {
    if (!ssl) return;

    const char *cipher = SSL_get_cipher(ssl);
    if (cipher) {
        LOG_DEBUG("Cipher: %s", cipher);
    }

    const char *version = SSL_get_version(ssl);
    if (version) {
        LOG_DEBUG("Protocol: %s", version);
    }
}

