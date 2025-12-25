// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h> // to use waitpid to collect the zombie process
#include <signal.h>
#include "logic/gamestate.h"
#include "logic/dice.h"
#include "logic/client_handler.h"
#include "common/tls.h"
#include "common/log.h"

#define PORT 8888
#define BUFFER_SIZE 1024

// signal handler for the child process
void sigchld_handler(int s) {
    (void)s; // 避免未使用參數警告
    while(waitpid(-1, NULL, WNOHANG) > 0); 
}

// signal handler for the ctrl + \ (SIGQUIT)
void sigquit_handler(int s) {
    (void)s; // 避免未使用參數警告
    LOG_INFO("Server shutting down... cleaning up IPC.");
    gamestate_destroy(); // release the shared memory (critical!)
    log_cleanup();
    exit(0);
}

int main(){
    int server_fd, new_socket;
    struct sockaddr_in address;
    struct sigaction sa;

    // 初始化日誌系統（輸出到 stderr，級別為 INFO）
    // 可以改為 LOG_DEBUG 來顯示更詳細的日誌，或改為 "server.log" 輸出到檔案
    log_init(LOG_INFO, NULL);

    LOG_INFO("Initializing World Boss Raid Server...");

    tls_init_openssl();
    // 證書路徑：嘗試多個可能的相對路徑（支援從不同目錄運行）
    // 1. 從 build/ 目錄運行：../certs/server/server.crt
    // 2. 從專案根目錄運行：certs/server/server.crt
    // 3. 從其他子目錄運行：../../certs/server/server.crt
    const char *cert_paths[] = {
        "../certs/server/server.crt",      // build/ 目錄
        "certs/server/server.crt",          // 專案根目錄
        "../../certs/server/server.crt",   // 子目錄（如 scriptt/heartbeat/）
        "../../../certs/server/server.crt" // 更深層的子目錄
    };
    const char *key_paths[] = {
        "../certs/server/server.key",
        "certs/server/server.key",
        "../../certs/server/server.key",
        "../../../certs/server/server.key"
    };
    
    const char *cert_file = NULL;
    const char *key_file = NULL;
    
    // 嘗試每個路徑，找到第一個存在的
    for (int i = 0; i < 4; i++) {
        FILE *test = fopen(cert_paths[i], "r");
        if (test) {
            fclose(test);
            cert_file = cert_paths[i];
            key_file = key_paths[i];
            break;
        }
    }
    
    // 如果都找不到，使用第一個路徑（會產生錯誤，但至少會顯示明確的錯誤訊息）
    if (!cert_file) {
        cert_file = cert_paths[0];
        key_file = key_paths[0];
    }
    
    SSL_CTX *ctx = tls_create_server_context(cert_file, key_file);
    if (!ctx) {
        LOG_ERROR("Failed to create SSL context");
        log_cleanup();
        exit(EXIT_FAILURE);
    }
    LOG_INFO("TLS context created successfully");

    // initialize the game state
    gamestate_init();
    LOG_INFO("Game state initialized");

    // initialize the dice system (random number generator)
    dice_init();
    LOG_INFO("Dice system initialized");

    // Setup SIGCHLD handler to reap zombie processes
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        LOG_ERROR("Failed to setup SIGCHLD handler: %s", strerror(errno));
        log_cleanup();
        exit(EXIT_FAILURE);
    }

    // Setup SIGQUIT handler for graceful shutdown (Ctrl+\)
    sa.sa_handler = sigquit_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGQUIT, &sa, NULL) == -1) {
        LOG_ERROR("Failed to setup SIGQUIT handler: %s", strerror(errno));
        log_cleanup();
        exit(EXIT_FAILURE);
    }

    // Disable SIGINT (Ctrl+C)
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        LOG_ERROR("Failed to disable SIGINT: %s", strerror(errno));
        log_cleanup();
        exit(EXIT_FAILURE);
    }

    // create a socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        log_cleanup();
        exit(EXIT_FAILURE);
    }

    // allow the socket to be reused immediately
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set socket option: %s", strerror(errno));
        close(server_fd);
        log_cleanup();
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        log_cleanup();
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1000) < 0) {
        LOG_ERROR("Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        log_cleanup();
        exit(EXIT_FAILURE);
    }

    LOG_INFO("Server listening on port %d", PORT);

    while (1) {
        struct sockaddr_in client_address;
        socklen_t client_addrlen = sizeof(client_address);

        // accept a new connection 
        if ((new_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen)) < 0) {
            LOG_WARN("Failed to accept connection: %s", strerror(errno));
            continue; // Continue accepting instead of exiting
        }

        LOG_DEBUG("New connection accepted from %s:%d", 
                  inet_ntoa(client_address.sin_addr), 
                  ntohs(client_address.sin_port));

        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("Failed to fork worker process: %s", strerror(errno));
            close(new_socket);
            continue;
        }
        if (pid == 0) {
            // child process (worker)
            close(server_fd); // Close server socket in child

            // 執行 TLS 握手，將普通 socket 升級為 SSL socket
            SSL *ssl = tls_server_handshake(ctx, new_socket);
            if (!ssl) {
                LOG_ERROR("TLS handshake failed");
                close(new_socket);
                exit(EXIT_FAILURE);
            }
            LOG_DEBUG("TLS handshake successful, starting client handler");

            // 直接傳入 SSL 物件，不需要傳 socket
            // handle_client 內部會使用 SSL_read/SSL_write 進行加密通訊
            handle_client(ssl);
            // 注意：handle_client 內部會負責清理 SSL 資源
            exit(0);
        } else {
            // parent process (master)
            close(new_socket); // Close client socket in parent
        }
    }

    // Should never reach here, but cleanup if needed
    close(server_fd);
    gamestate_destroy();
    log_cleanup();
    return 0;
}