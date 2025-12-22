/* src/common/tls.c - 共用的 TLS/SSL 函式庫 */
#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        fprintf(stderr, "[TLS Server] Error creating SSL context\n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 載入伺服器證書
    if (!SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM)) {
        fprintf(stderr, "[TLS Server] Error loading certificate: %s\n", cert_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // 載入伺服器私鑰
    if (!SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM)) {
        fprintf(stderr, "[TLS Server] Error loading private key: %s\n", key_file);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // 驗證私鑰與證書是否匹配
    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "[TLS Server] Private key does not match certificate\n");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    printf("[TLS Server] SSL context created successfully\n");
    return ctx;
}

// Server 端執行 TLS 握手
SSL *tls_server_handshake(SSL_CTX *ctx, int client_fd) {
    if (!ctx) {
        fprintf(stderr, "[TLS Server] Invalid SSL context\n");
        return NULL;
    }

    // 創建新的 SSL 物件
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        fprintf(stderr, "[TLS Server] Error creating SSL object\n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 將 socket 綁定到 SSL 物件
    if (SSL_set_fd(ssl, client_fd) != 1) {
        fprintf(stderr, "[TLS Server] Error setting socket file descriptor\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    // 執行 TLS 握手（Server 端使用 SSL_accept）
    printf("[TLS Server] Performing TLS handshake...\n");
    int ret = SSL_accept(ssl);
    
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        fprintf(stderr, "[TLS Server] TLS handshake failed (error code: %d)\n", err);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    printf("[TLS Server] TLS handshake successful!\n");
    tls_print_connection_info(ssl, "[TLS Server]");

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
        fprintf(stderr, "[TLS Client] Error creating SSL context\n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 如果提供了 CA 證書檔案，載入它來驗證伺服器證書
    if (ca_cert_file) {
        if (!SSL_CTX_load_verify_locations(ctx, ca_cert_file, NULL)) {
            fprintf(stderr, "[TLS Client] Error loading CA certificate: %s\n", ca_cert_file);
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ctx);
            return NULL;
        }
        
        // 設定驗證模式：驗證伺服器證書
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        printf("[TLS Client] CA certificate loaded. Server certificate will be verified.\n");
    } else {
        // 沒有提供 CA 證書，不驗證伺服器（僅用於開發測試）
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
        fprintf(stderr, "[TLS Client] WARNING: No CA certificate provided. Server certificate will NOT be verified!\n");
        printf("[TLS Client] This is insecure and should only be used for development/testing.\n");
    }

    return ctx;
}

// Client 端執行 TLS 握手
SSL *tls_client_handshake(SSL_CTX *ctx, int sockfd) {
    if (!ctx) {
        fprintf(stderr, "[TLS Client] Invalid SSL context\n");
        return NULL;
    }

    // 創建新的 SSL 物件
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        fprintf(stderr, "[TLS Client] Error creating SSL object\n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    // 將 socket 綁定到 SSL 物件
    if (SSL_set_fd(ssl, sockfd) != 1) {
        fprintf(stderr, "[TLS Client] Error setting socket file descriptor\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    // 執行 TLS 握手（客戶端使用 SSL_connect）
    printf("[TLS Client] Performing TLS handshake...\n");
    int ret = SSL_connect(ssl);
    
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        fprintf(stderr, "[TLS Client] TLS handshake failed (error code: %d)\n", err);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    printf("[TLS Client] TLS handshake successful!\n");
    tls_print_connection_info(ssl, "[TLS Client]");

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
        fprintf(stderr, "[TLS Client] No server certificate received\n");
        return -1;
    }

    // 檢查證書驗證結果
    long verify_result = SSL_get_verify_result(ssl);
    if (verify_result != X509_V_OK) {
        fprintf(stderr, "[TLS Client] Certificate verification failed: %s\n",
                X509_verify_cert_error_string(verify_result));
        X509_free(cert);
        return -1;
    }

    // 顯示證書資訊（可選）
    char *subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    
    if (subject) {
        printf("[TLS Client] Server certificate subject: %s\n", subject);
        OPENSSL_free(subject);
    }
    if (issuer) {
        printf("[TLS Client] Server certificate issuer: %s\n", issuer);
        OPENSSL_free(issuer);
    }

    X509_free(cert);
    printf("[TLS Client] Server certificate verified successfully!\n");
    return 0;
}

// ============================================================================
// 工具函數實作
// ============================================================================

// 顯示 SSL 連線資訊
void tls_print_connection_info(SSL *ssl, const char *prefix) {
    if (!ssl || !prefix) return;

    const char *cipher = SSL_get_cipher(ssl);
    if (cipher) {
        printf("%s Cipher: %s\n", prefix, cipher);
    }

    const char *version = SSL_get_version(ssl);
    if (version) {
        printf("%s Protocol: %s\n", prefix, version);
    }
}

