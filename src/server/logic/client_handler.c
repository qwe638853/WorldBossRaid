// client_handler.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../common/protocol.h"
#include "gamestate.h"
#include "dice.h"


// 預期完整流程：
// 1. 收第一包 OP_JOIN，分配 player_id，回 OP_JOIN_RESP
// 2. 進入主迴圈：
//    - 收封包
//    - 依 opcode 分流（OP_ATTACK / OP_HEARTBEAT / OP_LEAVE ...）
//    - 用 dice 決定傷害 / 拼點
//    - 用 gamestate 改 Boss HP、讀最新狀態
//    - 回 OP_GAME_STATE（以及各種通知）


// 從 socket 收一個 GamePacket（未來會在這裡實作 blocking recv）
static int recv_packet(int client_socket, GamePacket *pkt);

// 對 client 回傳一個 GamePacket（未來會在這裡實作 send 邏輯）
static int send_packet(int client_socket, GamePacket *pkt);

// 處理第一包 OP_JOIN：讀 username、分配 player_id、回 OP_JOIN_RESP
static int handle_join(int client_socket, GamePacket *pkt_in, int *out_player_id);

// 處理 OP_ATTACK：擲骰子、改 Boss HP、回最新 OP_GAME_STATE
static int handle_attack(int client_socket, GamePacket *pkt_in, int player_id);

// handle client connection in worker process
void handle_client(int *client_socket_ptr) {
    int client_socket = *client_socket_ptr;
    GamePacket packet;
    ssize_t bytes_read; // TODO: 實作時可用在 recv 迴圈裡
    int player_id = -1;

    // TODO: Implement packet receiving and processing logic
    // 1. 呼叫 recv_packet() 收第一包 OP_JOIN
    // 2. 呼叫 handle_join() 分配 player_id 並回 OP_JOIN_RESP
    // 3. 進入 while 迴圈：
    //    - recv_packet() 收封包
    //    - 依 pkt.header.opcode 呼叫對應處理函式（例如 handle_attack()）
    //    - 視情況回傳 OP_GAME_STATE 或其他通知
    
    close(client_socket);
    if (player_id >= 0) {
        game_player_leave(player_id);
    }
    exit(0);
}

// ================================
// 以下是預留的函式「空殼」，只宣告名稱，不寫邏輯
// 之後你或隊友可以在這裡慢慢把 TODO 補滿
// ================================

static int recv_packet(int client_socket, GamePacket *pkt) {
    // TODO: 實作從 client_socket 收完整一包 GamePacket
    // 建議流程：先收 PacketHeader，再依 length 收 body，最後回傳 0/-1
    (void)client_socket;
    (void)pkt;
    return -1;
}

static int send_packet(int client_socket, GamePacket *pkt) {
    // TODO: 實作將 GamePacket 送回 client
    // 建議流程：根據 pkt->header.length 用 send() 送出全部 bytes
    (void)client_socket;
    (void)pkt;
    return -1;
}

static int handle_join(int client_socket, GamePacket *pkt_in, int *out_player_id) {
    // TODO: 實作：
    // 1. 從 pkt_in->body.join 讀出 username
    // 2. 呼叫 game_player_join() 取得 player_id
    // 3. 組一個 OP_JOIN_RESP 封包，呼叫 send_packet() 回給 client
    (void)client_socket;
    (void)pkt_in;
    (void)out_player_id;
    return -1;
}

static int handle_attack(int client_socket, GamePacket *pkt_in, int player_id) {
    // TODO: 實作：
    // 1. 利用 dice_* 函式做「拼點對決」
    // 2. 呼叫 game_attack_boss() 改 Boss 狀態
    // 3. 組一個 OP_GAME_STATE 封包，呼叫 send_packet() 回給 client
    (void)client_socket;
    (void)pkt_in;
    (void)player_id;
    return -1;
}
