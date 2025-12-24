#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include <openssl/ssl.h>

// Handle client connection in worker process
// This function will be called in a forked child process
// 接收 SSL* 物件，內部會自動使用 SSL_read/SSL_write 進行加密通訊
void handle_client(SSL *ssl);

#endif

