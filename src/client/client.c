#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <ncurses.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

#include "common/protocol.h"
#include "common/tls.h"
#include "client/ui/login.h"
#include "client/ui/client_ui.h"

#define UI_MAX_NAME 32  // 與 client_ui.h 一致

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8888
// CA 證書路徑（用於驗證伺服器證書）
// 如果設為 NULL，則不驗證伺服器證書（僅用於開發測試）
#define CA_CERT_FILE "../certs/ca/ca.crt"  // 或 NULL 來跳過驗證

// 簡單的全域序號計數器（真的要用時可以做成 thread-safe）
static uint32_t g_seq_num = 1;

// ---------------------------------------------------------------------------
// Multi-threaded 架構：共享資料結構
// ---------------------------------------------------------------------------

typedef struct {
    pthread_mutex_t lock;              // 保護共享資料
    pthread_cond_t state_updated_cond;  // 通知 UI 有新狀態
    pthread_cond_t attack_request_cond; // 通知 Network 有攻擊請求
    
    UiGameState latest_state;          // 最新的遊戲狀態
    bool state_updated;                // 是否有新狀態
    bool attack_requested;             // UI 是否請求攻擊
    bool attack_completed;             // 攻擊是否完成
    bool should_exit;                  // 是否應該退出
    
    int network_error;                 // Network Thread 的錯誤碼（0=成功）
} SharedGameState;

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

// 送出一次攻擊（OP_ATTACK），並等待 OP_GAME_STATE 回來，結果存成 Payload_GameState
static int net_attack_and_get_state(SSL *ssl, Payload_GameState *out_state) {
    GamePacket pkt;
    pkt_init_local(&pkt, OP_ATTACK);

    // 這裡先簡單設一個「占位用」damage 值
    // 真正的傷害可以交給 server 的 dice 邏輯決定
    pkt.body.attack.damage = 0;

    if (pkt_send_local(ssl, &pkt, sizeof(Payload_Attack)) < 0) {
        fprintf(stderr, "Failed to send OP_ATTACK\n");
        return -1;
    }

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

    if (out_state) {
        *out_state = pkt.body.game_state;
    }
    return 0;
}

// 給壓力測試模式沿用的版本：只印出 Boss 狀態
static int send_attack_and_show_state(SSL *ssl) {
    Payload_GameState state;
    if (net_attack_and_get_state(ssl, &state) < 0) {
        return -1;
    }
    printf("[Client] Boss HP: %d / %d, Online Players: %d\n",
           state.boss_hp, state.max_hp, state.online_count);
    return 0;
}

// 送出 HEARTBEAT，取得最新 GameState
static int net_heartbeat_get_state(SSL *ssl, Payload_GameState *out_state) {
    GamePacket pkt;
    pkt_init_local(&pkt, OP_HEARTBEAT);

    if (pkt_send_local(ssl, &pkt, 0) < 0) {
        fprintf(stderr, "Failed to send OP_HEARTBEAT\n");
        return -1;
    }

    if (pkt_recv_local(ssl, &pkt) < 0) {
        fprintf(stderr, "Failed to receive game state (heartbeat)\n");
        return -1;
    }
    if (pkt.header.opcode != OP_GAME_STATE) {
        fprintf(stderr, "Unexpected opcode after heartbeat: 0x%X\n",
                pkt.header.opcode);
        return -1;
    }
    if (out_state) {
        *out_state = pkt.body.game_state;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Multi-threaded 架構：Network Thread（處理所有網路操作）
// ---------------------------------------------------------------------------

typedef struct {
    SSL *ssl;
    SharedGameState *shared;
} NetworkThreadArgs;

static void *network_thread_func(void *arg) {
    NetworkThreadArgs *args = (NetworkThreadArgs *)arg;
    SSL *ssl = args->ssl;
    SharedGameState *shared = args->shared;
    
    pthread_mutex_lock(&shared->lock);
    shared->network_error = 0;
    pthread_mutex_unlock(&shared->lock);
    
    // 先發送一次 heartbeat 來初始化狀態
    Payload_GameState server_state;
    if (net_heartbeat_get_state(ssl, &server_state) == 0) {
        pthread_mutex_lock(&shared->lock);
        shared->latest_state.boss_hp = server_state.boss_hp;
        shared->latest_state.max_hp = server_state.max_hp;
        shared->latest_state.online_count = server_state.online_count;
        shared->latest_state.stage = server_state.stage;
        shared->latest_state.is_respawning = server_state.is_respawning;
        shared->latest_state.is_lucky = server_state.is_lucky;
        shared->latest_state.is_crit = 0;
        shared->latest_state.last_player_damage = 0;
        shared->latest_state.last_boss_dice = 0;
        shared->latest_state.last_player_streak = 0;
        shared->latest_state.dmg_taken = 0;
        strncpy(shared->latest_state.last_killer, server_state.last_killer, UI_MAX_NAME - 1);
        shared->latest_state.last_killer[UI_MAX_NAME - 1] = '\0';
        shared->state_updated = true;
        pthread_cond_signal(&shared->state_updated_cond);
        pthread_mutex_unlock(&shared->lock);
    }
    
    // 持續循環：處理攻擊請求和定期 heartbeat
    while (!shared->should_exit) {
        bool has_attack = false;
        
        // 檢查是否有攻擊請求
        pthread_mutex_lock(&shared->lock);
        if (shared->attack_requested) {
            has_attack = true;
            shared->attack_requested = false; // 標記為正在處理
        }
        pthread_mutex_unlock(&shared->lock);
        
        if (has_attack) {
            // 處理攻擊請求
            Payload_GameState server_state;
            if (net_attack_and_get_state(ssl, &server_state) == 0) {
                // 更新共享狀態
                pthread_mutex_lock(&shared->lock);
                shared->latest_state.boss_hp = server_state.boss_hp;
                shared->latest_state.max_hp = server_state.max_hp;
                shared->latest_state.online_count = server_state.online_count;
                shared->latest_state.stage = server_state.stage;
                shared->latest_state.is_respawning = server_state.is_respawning;
                shared->latest_state.is_crit = server_state.is_crit;
                shared->latest_state.is_lucky = server_state.is_lucky;
                shared->latest_state.last_player_damage = server_state.last_player_damage;
                shared->latest_state.last_boss_dice = server_state.last_boss_dice;
                shared->latest_state.last_player_streak = server_state.last_player_streak;
                shared->latest_state.dmg_taken = server_state.dmg_taken;
                strncpy(shared->latest_state.last_killer, server_state.last_killer, UI_MAX_NAME - 1);
                shared->latest_state.last_killer[UI_MAX_NAME - 1] = '\0';
                shared->state_updated = true;
                shared->attack_completed = true;
                pthread_cond_signal(&shared->state_updated_cond);
                pthread_mutex_unlock(&shared->lock);
            } else {
                // 攻擊失敗
                pthread_mutex_lock(&shared->lock);
                shared->network_error = -1;
                shared->attack_completed = true;
                pthread_cond_signal(&shared->state_updated_cond);
                pthread_mutex_unlock(&shared->lock);
            }
        } else {
            // 沒有攻擊請求，發送 heartbeat
            Payload_GameState server_state;
            if (net_heartbeat_get_state(ssl, &server_state) == 0) {
                // 更新共享狀態（heartbeat 不包含攻擊相關資訊）
                pthread_mutex_lock(&shared->lock);
                shared->latest_state.boss_hp = server_state.boss_hp;
                shared->latest_state.max_hp = server_state.max_hp;
                shared->latest_state.online_count = server_state.online_count;
                shared->latest_state.stage = server_state.stage;
                shared->latest_state.is_respawning = server_state.is_respawning;
                // heartbeat 時保留 is_lucky（用於廣播 Lucky Kill）
                shared->latest_state.is_lucky = server_state.is_lucky;
                // heartbeat 時清除攻擊相關資訊（除非是 Lucky Kill）
                if (!server_state.is_lucky) {
                    shared->latest_state.is_crit = 0;
                    shared->latest_state.last_player_damage = 0;
                    shared->latest_state.last_boss_dice = 0;
                    shared->latest_state.last_player_streak = 0;
                    shared->latest_state.dmg_taken = 0;
                }
                strncpy(shared->latest_state.last_killer, server_state.last_killer, UI_MAX_NAME - 1);
                shared->latest_state.last_killer[UI_MAX_NAME - 1] = '\0';
                shared->state_updated = true;
                pthread_cond_signal(&shared->state_updated_cond);
                pthread_mutex_unlock(&shared->lock);
            } else {
                // Heartbeat 失敗，可能是連線斷了
                pthread_mutex_lock(&shared->lock);
                shared->network_error = -1;
                shared->should_exit = true;
                pthread_cond_signal(&shared->state_updated_cond);
                pthread_mutex_unlock(&shared->lock);
                break;
            }
            
            // 等待 0.5 秒後再發送下一次 heartbeat
            usleep(500000);
        }
    }
    
    return NULL;
}

// ---------------------------------------------------------------------------
// Multi-threaded 架構：一般互動模式（UI Thread + Network Thread）
// ---------------------------------------------------------------------------

static int run_interactive_mode_threaded(void) {
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    int sockfd = -1;
    int player_id = -1;
    pthread_t network_thread;
    NetworkThreadArgs net_args;
    SharedGameState shared;
    int ret = EXIT_FAILURE;
    
    // 初始化共享資料結構
    pthread_mutex_init(&shared.lock, NULL);
    pthread_cond_init(&shared.state_updated_cond, NULL);
    pthread_cond_init(&shared.attack_request_cond, NULL);
    memset(&shared.latest_state, 0, sizeof(UiGameState));
    shared.state_updated = false;
    shared.attack_requested = false;
    shared.attack_completed = false;
    shared.should_exit = false;
    shared.network_error = 0;
    
    // 1. 初始化 TLS
    tls_init_openssl();
    ctx = tls_create_client_context(CA_CERT_FILE);
    if (!ctx) {
        fprintf(stderr, "Failed to create TLS context\n");
        goto cleanup_shared;
    }
    
    // 2. 建立 TCP 連線
    sockfd = connect_to_server();
    if (sockfd < 0) {
        goto cleanup_ctx;
    }
    
    // 3. 執行 TLS 握手
    ssl = tls_client_handshake(ctx, sockfd);
    if (!ssl) {
        fprintf(stderr, "TLS handshake failed\n");
        goto cleanup_sock;
    }
    
    // 4. 驗證伺服器證書（如果提供了 CA 證書）
    if (CA_CERT_FILE != NULL) {
        if (tls_verify_server_certificate(ssl) != 0) {
            fprintf(stderr, "Server certificate verification failed!\n");
            goto cleanup_ssl;
        }
    }
    
    // 5. 啟動 ncurses，顯示登入畫面取得玩家名稱
    char username[MAX_PLAYER_NAME] = {0};
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    
    ui_login_get_player_name(username, sizeof(username));
    
    // 6. 送出 OP_JOIN 並等待回覆（這部分仍在主線程，因為在 login 之後）
    player_id = send_join_and_wait_resp(ssl, username);
    if (player_id < 0) {
        fprintf(stderr, "Join failed. Exit.\n");
        goto cleanup_ncurses;
    }
    
    // 7. 啟動 Network Thread
    net_args.ssl = ssl;
    net_args.shared = &shared;
    if (pthread_create(&network_thread, NULL, network_thread_func, &net_args) != 0) {
        perror("pthread_create (network)");
        goto cleanup_ncurses;
    }
    
    // 8. 在主線程（UI Thread）中運行 ui_game_loop
    // attack callback：通過共享資料結構與 Network Thread 通信
    int attack_cb_impl(UiGameState *out_state) {
        if (!out_state) return -1;
        
        // 請求攻擊
        pthread_mutex_lock(&shared.lock);
        shared.attack_requested = true;
        shared.attack_completed = false;
        pthread_cond_signal(&shared.attack_request_cond);
        pthread_mutex_unlock(&shared.lock);
        
        // 等待 Network Thread 完成攻擊（使用超時避免永久阻塞）
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5; // 5 秒超時
        
        pthread_mutex_lock(&shared.lock);
        while (!shared.attack_completed && !shared.should_exit) {
            int ret = pthread_cond_timedwait(&shared.state_updated_cond, &shared.lock, &timeout);
            if (ret == ETIMEDOUT) {
                // 超時
                pthread_mutex_unlock(&shared.lock);
                return -1;
            }
        }
        
        if (shared.network_error != 0 || shared.should_exit) {
            pthread_mutex_unlock(&shared.lock);
            return -1;
        }
        
        // 複製最新狀態
        *out_state = shared.latest_state;
        shared.state_updated = false;
        pthread_mutex_unlock(&shared.lock);
        
        return 0;
    }
    
    // heartbeat callback：從共享資料結構讀取最新狀態
    int heartbeat_cb_impl(UiGameState *out_state) {
        if (!out_state) return -1;
        
        pthread_mutex_lock(&shared.lock);
        // 檢查是否有新狀態
        if (shared.state_updated) {
            *out_state = shared.latest_state;
            shared.state_updated = false;
            pthread_mutex_unlock(&shared.lock);
            return 0;
        }
        // 沒有新狀態，返回當前狀態
        *out_state = shared.latest_state;
        pthread_mutex_unlock(&shared.lock);
        return 0;
    }
    
    // 運行 UI 迴圈（在主線程中，因為 ncurses 必須在同一個線程）
    ui_game_loop(username, attack_cb_impl, heartbeat_cb_impl);
    
    // 9. 通知 Network Thread 退出
    pthread_mutex_lock(&shared.lock);
    shared.should_exit = true;
    pthread_cond_signal(&shared.attack_request_cond);
    pthread_mutex_unlock(&shared.lock);
    
    // 10. 等待 Network Thread 結束
    pthread_join(network_thread, NULL);
    
    ret = (player_id >= 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    
cleanup_ncurses:
    endwin();
cleanup_ssl:
    if (ssl) {
        tls_shutdown(ssl);
        tls_free_ssl(ssl);
    }
cleanup_sock:
    if (sockfd >= 0) {
        close(sockfd);
    }
cleanup_ctx:
    if (ctx) {
        tls_cleanup_context(ctx);
    }
    tls_cleanup_openssl();
cleanup_shared:
    pthread_mutex_destroy(&shared.lock);
    pthread_cond_destroy(&shared.state_updated_cond);
    pthread_cond_destroy(&shared.attack_request_cond);
    
    return ret;
}

// ---------------------------------------------------------------------------
// 一般互動模式：單一玩家 + 單執行緒（保留作為備用）
// ---------------------------------------------------------------------------

static int run_interactive_mode(void) {
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    int sockfd = -1;
    int player_id = -1;

    // 1. 初始化 TLS
    tls_init_openssl();
    ctx = tls_create_client_context(CA_CERT_FILE);
    if (!ctx) {
        fprintf(stderr, "Failed to create TLS context\n");
        goto cleanup;
    }

    // 2. 建立 TCP 連線
    sockfd = connect_to_server();
    if (sockfd < 0) {
        goto cleanup;
    }

    // 3. 執行 TLS 握手
    ssl = tls_client_handshake(ctx, sockfd);
    if (!ssl) {
        fprintf(stderr, "TLS handshake failed\n");
        goto cleanup;
    }

    // 4. 驗證伺服器證書（如果提供了 CA 證書）
    if (CA_CERT_FILE != NULL) {
        if (tls_verify_server_certificate(ssl) != 0) {
            fprintf(stderr, "Server certificate verification failed!\n");
            goto cleanup;
        }
    }

    // 5. 啟動 ncurses，顯示登入畫面取得玩家名稱
    char username[MAX_PLAYER_NAME] = {0};
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();

    ui_login_get_player_name(username, sizeof(username));

    // 6. 送出 OP_JOIN 並等待回覆
    player_id = send_join_and_wait_resp(ssl, username);
    if (player_id < 0) {
        fprintf(stderr, "Join failed. Exit.\n");
        goto cleanup;
    }

    // 7. 進入 Boss 戰 UI 迴圈
    struct {
        SSL *ssl;
    } ui_ctx = { ssl };

    // attack callback：發送 OP_ATTACK，並轉換成 UiGameState
    int attack_cb_impl(UiGameState *out_state) {
        if (!out_state) return -1;
        Payload_GameState s;
        if (net_attack_and_get_state(ui_ctx.ssl, &s) < 0) return -1;
        out_state->boss_hp = s.boss_hp;
        out_state->max_hp = s.max_hp;
        out_state->online_count = s.online_count;
        out_state->stage = s.stage;
        out_state->is_respawning = s.is_respawning;
        out_state->is_crit = s.is_crit;
        out_state->is_lucky = s.is_lucky;
        out_state->last_player_damage = s.last_player_damage;
        out_state->last_boss_dice = s.last_boss_dice;
        out_state->last_player_streak = s.last_player_streak;
        out_state->dmg_taken = s.dmg_taken;
        strncpy(out_state->last_killer, s.last_killer, UI_MAX_NAME - 1);
        out_state->last_killer[UI_MAX_NAME - 1] = '\0';
        return 0;
    }

    // heartbeat callback：定期詢問最新血量
    int heartbeat_cb_impl(UiGameState *out_state) {
        if (!out_state) return -1;
        Payload_GameState s;
        if (net_heartbeat_get_state(ui_ctx.ssl, &s) < 0) return -1;
        out_state->boss_hp = s.boss_hp;
        out_state->max_hp = s.max_hp;
        out_state->online_count = s.online_count;
        out_state->stage = s.stage;
        out_state->is_respawning = s.is_respawning;
        out_state->is_crit = 0;
        out_state->is_lucky = 0;
        out_state->last_player_damage = 0;
        out_state->last_boss_dice = 0;
        out_state->last_player_streak = 0;
        out_state->dmg_taken = 0;
        strncpy(out_state->last_killer, s.last_killer, UI_MAX_NAME - 1);
        out_state->last_killer[UI_MAX_NAME - 1] = '\0';
        return 0;
    }

    ui_game_loop(username, attack_cb_impl, heartbeat_cb_impl);

cleanup:
    // 清理資源
    endwin();
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
    if (argc > 1 && strcmp(argv[1], "--single-thread") == 0) {
        // 單線程模式（備用）  ./client --single-thread
        return run_interactive_mode();
    }

    // 預設：Multi-threaded 架構（UI Thread + Network Thread）
    return run_interactive_mode_threaded();
}
