// client_handler.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include "common/protocol.h"
#include "common/tls.h"
#include "common/log.h"
#include "gamestate.h"
#include "dice.h"
#include "../security/replay_protection.h"


// 預期完整流程：
// 1. 收第一包 OP_JOIN，分配 player_id，回 OP_JOIN_RESP
// 2. 進入主迴圈：
//    - 收封包
//    - 依 opcode 分流（OP_ATTACK / OP_HEARTBEAT / OP_LEAVE ...）
//    - 用 dice 決定傷害 / 拼點
//    - 用 gamestate 改 Boss HP、讀最新狀態
//    - 回 OP_GAME_STATE（以及各種通知）


// 從 SSL 連線收一個 GamePacket（使用 SSL_read 進行加密接收）
// rp: Replay Protection 狀態（用於驗證 seq_num，可為 NULL）
static int recv_packet(SSL *ssl, GamePacket *pkt, ReplayProtection *rp);

// 對 client 回傳一個 GamePacket（使用 SSL_write 進行加密發送）
static int send_packet(SSL *ssl, GamePacket *pkt);

// 處理第一包 OP_JOIN：讀 username、分配 player_id、回 OP_JOIN_RESP
static int handle_join(SSL *ssl, GamePacket *pkt_in, int *out_player_id);

// 處理 OP_ATTACK：擲骰子、改 Boss HP、回最新 OP_GAME_STATE
static int handle_attack(SSL *ssl, GamePacket *pkt_in, int player_id);

// handle client connection in worker process
// 接收 SSL* 物件，內部自動使用 SSL_read/SSL_write 進行加密通訊
void handle_client(SSL *ssl) {
    GamePacket packet;
    int player_id = -1;
    
    // 初始化 Replay Protection（防止重放攻擊）
    ReplayProtection rp;
    replay_protection_init(&rp);

    // 接收第一包 OP_JOIN
    if (recv_packet(ssl, &packet, &rp) < 0) {
        LOG_ERROR("Failed to receive first packet");
        goto cleanup;
    }
    LOG_DEBUG("Received first packet: opcode=0x%02X", packet.header.opcode);

    // TODO: Implement packet receiving and processing logic
    // 1. ✅ 已接收第一包，檢查是否為 OP_JOIN
    // 2. 呼叫 handle_join() 分配 player_id 並回 OP_JOIN_RESP
    // 3. 進入 while 迴圈：
    //    - recv_packet() 收封包
    //    - 驗證 seq_num（replay protection，已在 recv_packet 內完成）
    //    - 依 pkt.header.opcode 呼叫對應處理函式（例如 handle_attack()）
    //    - 視情況回傳 OP_GAME_STATE 或其他通知
    
cleanup:
    // 清理 SSL 連線（會自動關閉底層 socket）
    if (ssl) {
        tls_shutdown(ssl);
        tls_free_ssl(ssl);
    }
    if (player_id >= 0) {
        gamestate_player_leave();
    }
    exit(0);
}

// ================================
// 以下是預留的函式「空殼」，只宣告名稱，不寫邏輯
// 之後你或隊友可以在這裡慢慢把 TODO 補滿
// ================================

// SSL 包裝層：使用 SSL_read 取代 recv()
// rp: Replay Protection 狀態（用於驗證 seq_num）
static int recv_packet(SSL *ssl, GamePacket *pkt, ReplayProtection *rp) {
    if (!ssl || !pkt) return -1;
    
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
            return -1;
        }
        received += (size_t)n;
    }

    // 驗證封包長度
    if (pkt->header.length < sizeof(PacketHeader) ||
        pkt->header.length > sizeof(GamePacket)) {
        LOG_WARN("Invalid packet length: %u (expected: %zu-%zu)", 
                 pkt->header.length, sizeof(PacketHeader), sizeof(GamePacket));
        return -1;
    }

    // 驗證 Replay Protection（防止重放攻擊）
    if (rp && !replay_protection_validate(rp, pkt->header.seq_num)) {
        LOG_ERROR("Replay attack detected! seq_num=%u", pkt->header.seq_num);
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
            return -1;
        }
        received += (size_t)n;
    }

    return 0;
}

// SSL 包裝層：使用 SSL_write 取代 send()
static int send_packet(SSL *ssl, GamePacket *pkt) {
    // TODO: 實作將 GamePacket 送回 client
    // 建議流程：根據 pkt->header.length 用 SSL_write() 送出全部 bytes
    // 使用 SSL_write() 取代原本的 send()
    // 範例：SSL_write(ssl, buffer, size)
    (void)ssl;
    (void)pkt;
    return -1;
}

// 內部邏輯函數：不需要修改，因為它們只呼叫 send_packet/recv_packet
static int handle_join(SSL *ssl, GamePacket *pkt_in, int *out_player_id) {
    // TODO: 實作
    // 1. 從 pkt_in->body.join 讀出 username
    // 2. 呼叫 game_player_join() 取得 player_id
    // 3. 組一個 OP_JOIN_RESP 封包，呼叫 send_packet() 回給 client
    (void)ssl;
    (void)pkt_in;
    (void)out_player_id;
    return -1;
}

// 內部邏輯函數：不需要修改，因為它們只呼叫 send_packet/recv_packet
static int handle_attack(SSL *ssl, GamePacket *pkt_in, int player_id) {
    // TODO: 實作：
    // 1. 利用 dice_* 函式做「拼點對決」
    // 2. 呼叫 game_attack_boss() 改 Boss 狀態
    // 3. 組一個 OP_GAME_STATE 封包，呼叫 send_packet() 回給 client
    (void)ssl;
    (void)pkt_in;
    (void)player_id;
    return -1;
}
//testtest
