// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h> // to use waitpid to collect the zombie process
#include <signal.h>
#include "logic/gamestate.h"
#include "logic/dice.h"
#include "logic/client_handler.h"
#include "common/tls.h"

#define PORT 8888
#define BUFFER_SIZE 1024

// signal handler for the child process
void sigchld_handler(int s) {
    (void)s; // 避免未使用參數警告
    while(waitpid(-1, NULL, WNOHANG) > 0); 
}

// signal handler for the ctrl + c
void sigint_handler(int s) {
    (void)s; // 避免未使用參數警告
    printf("\n[Master] Server shutting down... cleaning up IPC.\n");
    gamestate_destroy(); // release the shared memory (critical!)
    exit(0);
}

int main(){
    int server_fd, new_socket;
    struct sockaddr_in address;
    struct sigaction sa;

    tls_init_openssl();
    SSL_CTX *ctx = tls_create_server_context("../certs/server/server.crt", "../certs/server/server.key");
    if (!ctx) {
        fprintf(stderr, "Error creating SSL context\n");
        exit(EXIT_FAILURE);
    }

    // initialize the game state
    gamestate_init();
    
    // initialize the dice system (random number generator)
    dice_init();

    // Setup SIGCHLD handler to reap zombie processes
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction SIGCHLD");
        exit(EXIT_FAILURE);
    }

    // Setup SIGINT handler for graceful shutdown
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(EXIT_FAILURE);
    }

    // create a socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // allow the socket to be reused immediately
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1000) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Master] Server listening on port %d\n", PORT);

    while (1) {
        struct sockaddr_in client_address;
        socklen_t client_addrlen = sizeof(client_address);

        // accept a new connection 
        if ((new_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen)) < 0) {
            perror("accept");
            continue; // Continue accepting instead of exiting
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(new_socket);
            continue;
        }
        if (pid == 0) {
            // child process (worker)
            close(server_fd); // Close server socket in child

            // 執行 TLS 握手，將普通 socket 升級為 SSL socket
            SSL *ssl = tls_server_handshake(ctx, new_socket);
            if (!ssl) {
                fprintf(stderr, "Error performing TLS handshake\n");
                close(new_socket);
                exit(EXIT_FAILURE);
            }

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
    return 0;
}