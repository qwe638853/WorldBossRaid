// client.c - Simple interactive client for World Boss Raid
//
// 目標：給你一個「最小可懂版」的 client：
// - 建立 TCP 連線到 server
// - 送出 OP_JOIN (帶玩家名稱)
// - 讓你按鍵送出 OP_ATTACK
// - 收回 OP_GAME_STATE，印出 Boss 血量
//
// 先不用 UI / 多執行緒，讓你清楚看到 Client 在做什麼、封包怎麼組、怎麼跟 server 溝通。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "../common/protocol.h"

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8888

// 簡單的全域序號計數器（真的要用時可以做成 thread-safe）
static uint32_t g_seq_num = 1;

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

// 封裝送封包邏輯：給 payload 長度，幫你算 length + checksum 然後 send()
static int pkt_send_local(int sockfd, GamePacket *pkt, size_t payload_size) {
    // length = header + payload
    pkt->header.length = sizeof(PacketHeader) + (uint32_t)payload_size;

    // 先將 body 當成 raw bytes 來算 checksum
    pkt->header.checksum = calc_checksum(
        (const uint8_t *)&pkt->body.raw[0],
        payload_size
    );

    size_t total = pkt->header.length;
    ssize_t sent = send(sockfd, pkt, total, 0);
    if (sent < 0) {
        perror("send");
        return -1;
    }
    if ((size_t)sent != total) {
        fprintf(stderr, "Partial send: expected %zu, got %zd\n", total, sent);
        return -1;
    }
    return 0;
}

// 封裝收封包邏輯：先收 header，再依 length 收 body
static int pkt_recv_local(int sockfd, GamePacket *pkt) {
    ssize_t n;
    size_t received = 0;

    // 先收 header
    while (received < sizeof(PacketHeader)) {
        n = recv(sockfd,
                 ((uint8_t *)&pkt->header) + received,
                 sizeof(PacketHeader) - received,
                 0);
        if (n <= 0) {
            if (n < 0) perror("recv header");
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
        n = recv(sockfd,
                 ((uint8_t *)&pkt->body.raw[0]) + received,
                 body_len - received,
                 0);
        if (n <= 0) {
            if (n < 0) perror("recv body");
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
static int send_join_and_wait_resp(int sockfd, const char *username) {
    GamePacket pkt;
    pkt_init_local(&pkt, OP_JOIN);

    // 填入玩家名稱（超過長度就被截斷）
    strncpy(pkt.body.join.username, username, MAX_PLAYER_NAME - 1);
    pkt.body.join.username[MAX_PLAYER_NAME - 1] = '\0';

    if (pkt_send_local(sockfd, &pkt, sizeof(Payload_Join)) < 0) {
        fprintf(stderr, "Failed to send OP_JOIN\n");
        return -1;
    }

    printf("[Client] Sent OP_JOIN as '%s'\n", pkt.body.join.username);

    // 等待伺服器的回覆（理想情況下是 OP_JOIN_RESP）
    if (pkt_recv_local(sockfd, &pkt) < 0) {
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
static int send_attack_and_show_state(int sockfd) {
    GamePacket pkt;
    pkt_init_local(&pkt, OP_ATTACK);

    // 這裡先簡單設一個「占位用」damage 值
    // 真正的傷害可以交給 server 的 dice 邏輯決定
    pkt.body.attack.damage = 0;

    if (pkt_send_local(sockfd, &pkt, sizeof(Payload_Attack)) < 0) {
        fprintf(stderr, "Failed to send OP_ATTACK\n");
        return -1;
    }

    printf("[Client] Sent OP_ATTACK\n");

    // 等待伺服器回傳最新遊戲狀態（OP_GAME_STATE）
    if (pkt_recv_local(sockfd, &pkt) < 0) {
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

int main(void) {
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

    // 2. 連線到 server
    int sockfd = connect_to_server();

    // 3. 送出 OP_JOIN 並等待回覆
    int player_id = send_join_and_wait_resp(sockfd, username);
    if (player_id < 0) {
        fprintf(stderr, "Join failed. Exit.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // 4. 簡單互動迴圈：按 'a' 攻擊、'q' 離開
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
            if (send_attack_and_show_state(sockfd) < 0) {
                fprintf(stderr, "Error during attack. Closing.\n");
                break;
            }
        } else {
            printf("Unknown command. Use 'a' to attack, 'q' to quit.\n");
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}


