// client.c - Client for World Boss Raid
//
// 提供兩種模式：
// 1. 一般互動模式（預設）：單一玩家，透過 TLS 連線手動攻擊世界王。
// 2. 壓力測試模式（--stress）：使用多個 thread 同時連線並自動攻擊，用來測試 Server 承載量。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "common/protocol.h"
#include "common/tls.h"

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8888
// CA 證書路徑（用於驗證伺服器證書）
// 如果設為 NULL，則不驗證伺服器證書（僅用於開發測試）
#define CA_CERT_FILE "../certs/ca/ca.crt"  // 或 NULL 來跳過驗證

// 簡單的全域序號計數器（真的要用時可以做成 thread-safe）
static uint32_t g_seq_num = 1;

// 壓力測試相關常數
#define STRESS_WORKER_COUNT        100   // 同時開多少條連線
#define STRESS_ATTACKS_PER_WORKER   20   // 每條連線攻擊幾次

// 壓力測試用的 thread 參數
typedef struct {
    SSL_CTX *ctx;       // 共用的 TLS context（thread-safe 用來建立 SSL 物件）
    int      worker_id; // 第幾號機器人
} StressWorkerArgs;

// 計算封包 checksum：這裡用「所有位元組的簡單總和」
static uint16_t calc_checksum(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFF);
}

// 初始化一個 GamePacket：只設定 header 基本欄位
static void pkt_init_local(GamePacket *pkt, uint16_t opcode) {
    memset(pkt, 0, sizeof(GamePacket));
    pkt->header.opcode = opcode;
    pkt->header.seq_num = g_seq_num++; // 每送一包就加一
}

// 封裝送封包邏輯：給 payload 長度，幫你算 length + checksum 然後 SSL_write()
static int pkt_send_local(SSL *ssl, GamePacket *pkt, size_t payload_size) {
    // length = header + payload
    pkt->header.length = sizeof(PacketHeader) + (uint32_t)payload_size;

    // 先將 body 當成 raw bytes 來算 checksum
    pkt->header.checksum = calc_checksum(
        (const uint8_t *)&pkt->body.raw[0],
        payload_size
    );

    size_t total = pkt->header.length;
    ssize_t sent = SSL_write(ssl, pkt, total);
    if (sent < 0) {
        int err = SSL_get_error(ssl, sent);
        fprintf(stderr, "[Client] SSL_write error: %d\n", err);
        ERR_print_errors_fp(stderr);
        return -1;
    }
    if ((size_t)sent != total) {
        fprintf(stderr, "Partial send: expected %zu, got %zd\n", total, sent);
        return -1;
    }
    return 0;
}

// 封裝收封包邏輯：先收 header，再依 length 收 body（使用 SSL_read）
static int pkt_recv_local(SSL *ssl, GamePacket *pkt) {
    ssize_t n;
    size_t received = 0;

    // 先收 header
    while (received < sizeof(PacketHeader)) {
        n = SSL_read(ssl,
                     ((uint8_t *)&pkt->header) + received,
                     sizeof(PacketHeader) - received);
        if (n <= 0) {
            int err = SSL_get_error(ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                continue; // 重試
            }
            fprintf(stderr, "[Client] SSL_read header error: %d\n", err);
            ERR_print_errors_fp(stderr);
            return -1;
        }
        received += (size_t)n;
    }

    // 檢查 length 合不合理
    if (pkt->header.length < sizeof(PacketHeader) ||
        pkt->header.length > sizeof(GamePacket)) {
        fprintf(stderr, "Invalid packet length: %u\n", pkt->header.length);
        return -1;
    }

    size_t body_len = pkt->header.length - sizeof(PacketHeader);
    received = 0;

    // 再收 body
    while (received < body_len) {
        n = SSL_read(ssl,
                     ((uint8_t *)&pkt->body.raw[0]) + received,
                     body_len - received);
        if (n <= 0) {
            int err = SSL_get_error(ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                continue; // 重試
            }
            fprintf(stderr, "[Client] SSL_read body error: %d\n", err);
            ERR_print_errors_fp(stderr);
            return -1;
        }
        received += (size_t)n;
    }

    // 可以選擇驗證 checksum（這裡只是示範怎麼算）
    uint16_t expect = calc_checksum((const uint8_t *)&pkt->body.raw[0],
                                    body_len);
    if (expect != pkt->header.checksum) {
        fprintf(stderr, "Checksum mismatch: expect=%u, got=%u\n",
                expect, pkt->header.checksum);
        // 先印警告，但不強制中斷
    }

    return 0;
}

// 建立與 server 的 TCP 連線
static int connect_to_server(void) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[Client] Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);
    return sockfd;
}

// 送出 OP_JOIN，帶玩家名稱，並等待 OP_JOIN_RESP
static int send_join_and_wait_resp(SSL *ssl, const char *username) {
    GamePacket pkt;
    pkt_init_local(&pkt, OP_JOIN);

    // 填入玩家名稱（超過長度就被截斷）
    strncpy(pkt.body.join.username, username, MAX_PLAYER_NAME - 1);
    pkt.body.join.username[MAX_PLAYER_NAME - 1] = '\0';

    if (pkt_send_local(ssl, &pkt, sizeof(Payload_Join)) < 0) {
        fprintf(stderr, "Failed to send OP_JOIN\n");
        return -1;
    }

    printf("[Client] Sent OP_JOIN as '%s'\n", pkt.body.join.username);

    // 等待伺服器的回覆（理想情況下是 OP_JOIN_RESP）
    if (pkt_recv_local(ssl, &pkt) < 0) {
        fprintf(stderr, "Failed to receive join response\n");
        return -1;
    }

    if (pkt.header.opcode != OP_JOIN_RESP) {
        fprintf(stderr, "Unexpected opcode after join: 0x%X\n",
                pkt.header.opcode);
        return -1;
    }

    Payload_JoinResp *resp = &pkt.body.join_resp;
    if (resp->status == 1) {
        printf("[Client] Join success! Your player_id = %d\n",
               resp->player_id);
        return resp->player_id;
    } else {
        printf("[Client] Join failed. Server status = %d\n", resp->status);
        return -1;
    }
}

// 送出一次攻擊（OP_ATTACK），並等待 OP_GAME_STATE 回來
static int send_attack_and_show_state(SSL *ssl) {
    GamePacket pkt;
    pkt_init_local(&pkt, OP_ATTACK);

    // 這裡先簡單設一個「占位用」damage 值
    // 真正的傷害可以交給 server 的 dice 邏輯決定
    pkt.body.attack.damage = 0;

    if (pkt_send_local(ssl, &pkt, sizeof(Payload_Attack)) < 0) {
        fprintf(stderr, "Failed to send OP_ATTACK\n");
        return -1;
    }

    printf("[Client] Sent OP_ATTACK\n");

    // 等待伺服器回傳最新遊戲狀態（OP_GAME_STATE）
    if (pkt_recv_local(ssl, &pkt) < 0) {
        fprintf(stderr, "Failed to receive game state\n");
        return -1;
    }

    if (pkt.header.opcode != OP_GAME_STATE) {
        fprintf(stderr, "Unexpected opcode after attack: 0x%X\n",
                pkt.header.opcode);
        return -1;
    }

    Payload_GameState *state = &pkt.body.game_state;
    printf("[Client] Boss HP: %d / %d, Online Players: %d\n",
           state->boss_hp, state->max_hp, state->online_count);
    return 0;
}

// ---------------------------------------------------------------------------
// 一般互動模式：單一玩家 + 單執行緒
// ---------------------------------------------------------------------------

static int run_interactive_mode(void) {
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    int sockfd = -1;
    int player_id = -1;

    // 1. 輸入玩家名稱
    char username[MAX_PLAYER_NAME];
    printf("Enter your player name: ");
    if (fgets(username, sizeof(username), stdin) == NULL) {
        fprintf(stderr, "Failed to read username\n");
        return EXIT_FAILURE;
    }
    // 去掉換行
    username[strcspn(username, "\n")] = '\0';
    if (username[0] == '\0') {
        strncpy(username, "Player", sizeof(username) - 1);
        username[sizeof(username) - 1] = '\0';
    }

    // 2. 初始化 TLS
    tls_init_openssl();
    ctx = tls_create_client_context(CA_CERT_FILE);
    if (!ctx) {
        fprintf(stderr, "Failed to create TLS context\n");
        goto cleanup;
    }

    // 3. 建立 TCP 連線
    sockfd = connect_to_server();
    if (sockfd < 0) {
        goto cleanup;
    }

    // 4. 執行 TLS 握手
    ssl = tls_client_handshake(ctx, sockfd);
    if (!ssl) {
        fprintf(stderr, "TLS handshake failed\n");
        goto cleanup;
    }

    // 5. 驗證伺服器證書（如果提供了 CA 證書）
    if (CA_CERT_FILE != NULL) {
        if (tls_verify_server_certificate(ssl) != 0) {
            fprintf(stderr, "Server certificate verification failed!\n");
            goto cleanup;
        }
    }

    // 6. 送出 OP_JOIN 並等待回覆
    player_id = send_join_and_wait_resp(ssl, username);
    if (player_id < 0) {
        fprintf(stderr, "Join failed. Exit.\n");
        goto cleanup;
    }

    // 7. 簡單互動迴圈：按 'a' 攻擊、'q' 離開
    printf("Press 'a' then Enter to attack, 'q' then Enter to quit.\n");

    char line[16];
    while (1) {
        printf("> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        if (line[0] == 'q' || line[0] == 'Q') {
            printf("[Client] Quit.\n");
            break;
        } else if (line[0] == 'a' || line[0] == 'A') {
            if (send_attack_and_show_state(ssl) < 0) {
                fprintf(stderr, "Error during attack. Closing.\n");
                break;
            }
        } else {
            printf("Unknown command. Use 'a' to attack, 'q' to quit.\n");
        }
    }

cleanup:
    // 清理資源
    if (ssl) {
        tls_shutdown(ssl);
        tls_free_ssl(ssl);
    }
    if (sockfd >= 0) {
        close(sockfd);
    }
    if (ctx) {
        tls_cleanup_context(ctx);
    }
    tls_cleanup_openssl();

    return (player_id >= 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

// ---------------------------------------------------------------------------
// 壓力測試模式：用多個 thread 同時攻擊 Server
// ---------------------------------------------------------------------------

static void *stress_worker_thread(void *arg) {
    StressWorkerArgs *w = (StressWorkerArgs *)arg;
    SSL *ssl = NULL;
    int sockfd = -1;
    int player_id = -1;
    char username[MAX_PLAYER_NAME];

    // 每個 worker 自己建立 TCP 連線
    sockfd = connect_to_server();
    if (sockfd < 0) {
        goto cleanup;
    }

    // 使用共用的 ctx 做 TLS 握手
    ssl = tls_client_handshake(w->ctx, sockfd);
    if (!ssl) {
        fprintf(stderr, "[Stress %d] TLS handshake failed\n", w->worker_id);
        goto cleanup;
    }

    // 驗證伺服器證書（如果有提供 CA）
    if (CA_CERT_FILE != NULL) {
        if (tls_verify_server_certificate(ssl) != 0) {
            fprintf(stderr, "[Stress %d] Server certificate verification failed\n", w->worker_id);
            goto cleanup;
        }
    }

    // 組一個機器人名稱
    snprintf(username, sizeof(username), "bot_%03d", w->worker_id);

    // JOIN
    player_id = send_join_and_wait_resp(ssl, username);
    if (player_id < 0) {
        fprintf(stderr, "[Stress %d] Join failed\n", w->worker_id);
        goto cleanup;
    }

    // 固定次數的攻擊迴圈
    for (int i = 0; i < STRESS_ATTACKS_PER_WORKER; ++i) {
        if (send_attack_and_show_state(ssl) < 0) {
            fprintf(stderr, "[Stress %d] Attack failed at #%d\n", w->worker_id, i);
            break;
        }
        // 稍微休息一下，避免所有連線同時瞬間打完
        usleep(100 * 1000); // 100ms
    }

cleanup:
    if (ssl) {
        tls_shutdown(ssl);
        tls_free_ssl(ssl);
    }
    if (sockfd >= 0) {
        close(sockfd);
    }

    return NULL;
}

static int run_stress_mode(void) {
    SSL_CTX *ctx = NULL;
    pthread_t threads[STRESS_WORKER_COUNT];
    StressWorkerArgs args[STRESS_WORKER_COUNT];

    printf("[Stress] Starting stress test with %d workers, %d attacks each...\n",
           STRESS_WORKER_COUNT, STRESS_ATTACKS_PER_WORKER);

    tls_init_openssl();
    ctx = tls_create_client_context(CA_CERT_FILE);
    if (!ctx) {
        fprintf(stderr, "[Stress] Failed to create TLS context\n");
        tls_cleanup_openssl();
        return EXIT_FAILURE;
    }

    // 建立所有 worker thread
    for (int i = 0; i < STRESS_WORKER_COUNT; ++i) {
        args[i].ctx = ctx;
        args[i].worker_id = i;
        if (pthread_create(&threads[i], NULL, stress_worker_thread, &args[i]) != 0) {
            perror("[Stress] pthread_create");
            threads[i] = 0; // 標記這個 thread 沒有成功建立
        }
    }

    // 等待所有 worker 結束
    for (int i = 0; i < STRESS_WORKER_COUNT; ++i) {
        if (threads[i]) {
            pthread_join(threads[i], NULL);
        }
    }

    tls_cleanup_context(ctx);
    tls_cleanup_openssl();

    printf("[Stress] All workers finished.\n");
    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// 程式進入點：依據參數決定模式
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--stress") == 0) {
        // 壓力測試模式  ./client --stress
        return run_stress_mode();
    }

    // 預設：一般互動模式
    return run_interactive_mode();
}
