#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h> // to use uint32_t, uint16_t (ensure cross-platform size consistency)

#define MAX_PLAYER_NAME 32
#define MAX_ERROR_MESSAGE 64
#define MAX_PAYLOAD_SIZE 1024

typedef enum {
    // [Client -> Server]
    OP_JOIN         = 0x10, // join the game
    OP_ATTACK       = 0x11, // attack the boss
    OP_LEAVE        = 0x12, // leave the game
    OP_HEARTBEAT    = 0x13, // heartbeat (keep alive)

    // [Server -> Client]
    OP_JOIN_RESP    = 0x20, // join result
    OP_GAME_STATE   = 0x21, // broadcast the boss state (health update)
    OP_ERROR        = 0x22  // error
} OpCode;

typedef struct __attribute__((packed)) {
    uint32_t length;    // the length of the packet (Header + Body)
    uint16_t opcode;    // the opcode (OpCode)
    uint16_t checksum;  // the checksum (CRC16 or simple addition)
    uint32_t seq_num;   // the sequence number (used to detect lost packets or retransmission)
} PacketHeader;


typedef struct __attribute__((packed)) {
    char username[MAX_PLAYER_NAME];
} Payload_Join;

typedef struct __attribute__((packed)) {
    int32_t damage;    // how much damage this time
} Payload_Attack;

typedef struct __attribute__((packed)) {
    char error_message[MAX_ERROR_MESSAGE];
} Payload_Error;

typedef struct __attribute__((packed)) {
    int32_t player_id;  // assigned to the player
    uint8_t status;     // 1=success, 0=failed
} Payload_JoinResp;

typedef struct __attribute__((packed)) {
    int32_t boss_hp;        // the current health
    int32_t max_hp;         // the maximum health (needed to draw the health bar)
    int32_t online_count;   // the number of online players (for monitoring)
} Payload_GameState;

typedef struct __attribute__((packed)) {
    PacketHeader header;
    union {
        Payload_Join      join;
        Payload_Attack    attack;
        Payload_JoinResp  join_resp;
        Payload_GameState game_state;
        Payload_Error     error;
        char              raw[MAX_PAYLOAD_SIZE]; // used for encryption/calculation of length
    } body;
} GamePacket;

// define the function prototype for the protocol
void pkt_init(GamePacket *pkt, uint16_t opcode);
int pkt_send(int sockfd, GamePacket *pkt);

#endif