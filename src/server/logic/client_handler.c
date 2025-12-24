// client_handler.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
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

// 簡單的 checksum：對 payload 所有位元組做加總（和 client 一致）
static uint16_t calc_checksum(const uint8_t *data, size_t len);

// 從 SSL 連線收一個 GamePacket（使用 SSL_read 進行加密接收）
// rp: Replay Protection 狀態（用於驗證 seq_num，可為 NULL）
static int recv_packet(SSL *ssl, GamePacket *pkt, ReplayProtection *rp);

// 對 client 回傳一個 GamePacket（使用 SSL_write 進行加密發送）
// payload_size: 實際 payload 長度
static int send_packet(SSL *ssl, GamePacket *pkt, size_t payload_size);

// 處理第一包 OP_JOIN：讀 username、分配 player_id、回 OP_JOIN_RESP
static int handle_join(SSL *ssl, GamePacket *pkt_in, int *out_player_id, char *out_username);

// 處理 OP_ATTACK：擲骰子、改 Boss HP、回最新 OP_GAME_STATE
static int handle_attack(SSL *ssl, GamePacket *pkt_in, const char *player_name);

// handle client connection in worker process
// 接收 SSL* 物件，內部自動使用 SSL_read/SSL_write 進行加密通訊
void handle_client(SSL *ssl) {
    GamePacket packet;
    int player_id = -1;
    char username[MAX_PLAYER_NAME] = {0};

    // 初始化 Replay Protection（防止重放攻擊）
    ReplayProtection rp;
    replay_protection_init(&rp);

    // 接收第一包 OP_JOIN
    if (recv_packet(ssl, &packet, &rp) < 0) {
        LOG_ERROR("Failed to receive first packet");
        goto cleanup;
    }
    LOG_DEBUG("Received first packet: opcode=0x%02X", packet.header.opcode);

    // 必須先 JOIN 才能進入遊戲
    if (packet.header.opcode != OP_JOIN) {
        LOG_WARN("First packet is not OP_JOIN, opcode=0x%02X", packet.header.opcode);
        goto cleanup;
    }

    // 處理加入：分配 player_id、紀錄名稱並回傳 OP_JOIN_RESP
    if (handle_join(ssl, &packet, &player_id, username) < 0) {
        LOG_ERROR("handle_join failed");
        goto cleanup;
    }

    // 進入主迴圈：處理 ATTACK / HEARTBEAT / LEAVE
    while (1) {
        if (recv_packet(ssl, &packet, &rp) < 0) {
            LOG_INFO("Client disconnected or recv error, closing connection");
            break;
        }

        switch (packet.header.opcode) {
            case OP_ATTACK:
                if (handle_attack(ssl, &packet, username) < 0) {
                    LOG_ERROR("handle_attack failed");
                    goto cleanup;
                }
                break;

            case OP_HEARTBEAT: {
                GameSharedData snap;
                Payload_GameState state;
                gamestate_get_snapshot(&snap);
                state.boss_hp = snap.current_hp;
                state.max_hp = snap.max_hp;
                state.online_count = snap.online_count;
                state.stage = (uint8_t)snap.stage;
                state.is_respawning = snap.is_respawning ? 1 : 0;
                state.is_crit = 0;
                state.is_lucky = 0;
                state.last_player_damage = 0;
                state.last_boss_dice = 0;
                state.last_player_streak = 0;
                state.dmg_taken = 0;
                strncpy(state.last_killer, snap.last_killer, MAX_PLAYER_NAME);

                GamePacket resp;
                memset(&resp, 0, sizeof(resp));
                resp.header.opcode = OP_GAME_STATE;
                resp.body.game_state = state;

                if (send_packet(ssl, &resp, sizeof(Payload_GameState)) < 0) {
                    LOG_ERROR("Failed to send GAME_STATE for heartbeat");
                    goto cleanup;
                }
                break;
            }

            case OP_LEAVE:
                LOG_INFO("Client requested leave");
                goto cleanup;

            default:
                LOG_WARN("Unknown opcode from client: 0x%02X", packet.header.opcode);
                break;
        }
    }

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
static int send_packet(SSL *ssl, GamePacket *pkt, size_t payload_size) {
    if (!ssl || !pkt) return -1;

    pkt->header.length = (uint32_t)(sizeof(PacketHeader) + payload_size);
    pkt->header.checksum = calc_checksum((const uint8_t *)&pkt->body.raw[0],
                                         payload_size);

    size_t total = pkt->header.length;
    size_t sent_total = 0;

    while (sent_total < total) {
        int n = SSL_write(ssl, ((uint8_t *)pkt) + sent_total, total - sent_total);
        if (n <= 0) {
            int err = SSL_get_error(ssl, n);
            LOG_ERROR("SSL_write error: %d", err);
            ERR_print_errors_fp(stderr);
            return -1;
        }
        sent_total += (size_t)n;
    }
    return 0;
}

static int handle_join(SSL *ssl, GamePacket *pkt_in, int *out_player_id, char *out_username) {
    if (!ssl || !pkt_in || !out_player_id || !out_username) return -1;

    const char *name = pkt_in->body.join.username;
    strncpy(out_username, name, MAX_PLAYER_NAME - 1);
    out_username[MAX_PLAYER_NAME - 1] = '\0';

    int online = gamestate_player_join();
    int player_id = online;
    *out_player_id = player_id;

    LOG_INFO("Player joined: name=%s, id=%d, online=%d", out_username, player_id, online);

    GamePacket resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.opcode = OP_JOIN_RESP;
    resp.body.join_resp.player_id = player_id;
    resp.body.join_resp.status = 1;

    if (send_packet(ssl, &resp, sizeof(Payload_JoinResp)) < 0) {
        LOG_ERROR("Failed to send OP_JOIN_RESP");
        return -1;
    }

    return 0;
}

static int handle_attack(SSL *ssl, GamePacket *pkt_in, const char *player_name) {
    if (!ssl || !player_name || !pkt_in) return -1;

    // 如果 client 有帶骰子值則使用，否則 server 自行擲骰
    int player_dice = pkt_in->body.attack.damage;
    if (player_dice <= 0 || player_dice > 6) {
        player_dice = (rand() % 6) + 1;
    }

    AttackResult result;
    Payload_GameState state;
    game_process_attack(player_dice, player_name, &result, &state);

    // 附加本次戰鬥資訊到回傳狀態
    state.is_crit = result.is_crit ? 1 : 0;
    state.is_lucky = result.is_lucky_kill ? 1 : 0;
    state.last_player_damage = result.dmg_dealt;
    state.last_boss_dice = result.boss_dice;
    state.last_player_streak = result.current_streak;
    state.dmg_taken = result.dmg_taken;

    LOG_DEBUG("Attack: player=%s dice=%d boss_dice=%d dmg=%d taken=%d hp=%d/%d",
              player_name, player_dice, result.boss_dice,
              result.dmg_dealt, result.dmg_taken,
              state.boss_hp, state.max_hp);

    GamePacket resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.opcode = OP_GAME_STATE;
    resp.body.game_state = state;

    if (send_packet(ssl, &resp, sizeof(Payload_GameState)) < 0) {
        LOG_ERROR("Failed to send OP_GAME_STATE");
        return -1;
    }

    return 0;
}

static uint16_t calc_checksum(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFF);
}