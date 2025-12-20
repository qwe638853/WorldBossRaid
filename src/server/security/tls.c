#include "tls.h"
#include <stdio.h>

void init_openssl() {
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
}

SSL_CTX *create_tls_context(const char *cert_file, const char *key_file) {
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);    
    if (!ctx) {
        fprintf(stderr, "Error creating SSL context\n");
        exit(EXIT_FAILURE);
    }
    if(!SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM)) {
        fprintf(stderr, "Error loading certificate\n");
        exit(EXIT_FAILURE);
    }
    if(!SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM)) {
        fprintf(stderr, "Error loading private key\n");
        exit(EXIT_FAILURE);
    }

    return ctx;

}

SSL *perform_tls_handshake(SSL_CTX *ctx, int client_fd) {
    SSL *ssl = SSL_new(ctx);       // create a new SSL object
    SSL_set_fd(ssl, client_fd);    // bind the socket to the SSL object

    if (SSL_accept(ssl) <= 0) {    // perform the TLS handshake
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

void cleanup_tls(SSL_CTX *ctx) {
    SSL_CTX_free(ctx);
    EVP_cleanup();
}
