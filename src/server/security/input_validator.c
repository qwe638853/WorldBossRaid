/* src/server/security/input_validator.c - Input Validation */
#include "input_validator.h"
#include "../../common/log.h"
#include "../../common/protocol.h"
#include <string.h>
#include <ctype.h>

// 驗證用戶名是否合法
bool input_validate_username(const char *username) {
    if (!username) {
        LOG_WARN("Username is NULL");
        return false;
    }
    
    size_t len = strnlen(username, MAX_PLAYER_NAME);
    
    // 檢查長度
    if (len == 0 || len >= MAX_PLAYER_NAME) {
        LOG_WARN("Invalid username length: %zu (must be 1-%zu)", len, MAX_PLAYER_NAME - 1);
        return false;
    }
    
    // 檢查是否包含非法字符（只允許字母、數字、底線、連字號）
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)username[i]) && 
            username[i] != '_' && 
            username[i] != '-') {
            LOG_WARN("Invalid character in username: '%c' at position %zu", username[i], i);
            return false;
        }
    }
    
    return true;
}

// 驗證封包的 OpCode 是否合法
bool input_validate_opcode(uint16_t opcode) {
    switch (opcode) {
        case OP_JOIN:
        case OP_ATTACK:
        case OP_LEAVE:
        case OP_HEARTBEAT:
            return true;
        default:
            LOG_WARN("Invalid opcode: 0x%02X", opcode);
            return false;
    }
}

// 驗證封包長度是否合法（根據 OpCode）
bool input_validate_packet_size(uint16_t opcode, uint32_t packet_length) {
    uint32_t min_size = sizeof(PacketHeader);
    uint32_t max_size = sizeof(GamePacket);
    
    // 基本長度檢查
    if (packet_length < min_size || packet_length > max_size) {
        LOG_WARN("Invalid packet length: %u (expected: %u-%u)", 
                 packet_length, min_size, max_size);
        return false;
    }
    
    // 根據 OpCode 檢查預期的 payload 大小
    uint32_t expected_payload_size = 0;
    switch (opcode) {
        case OP_JOIN:
            expected_payload_size = sizeof(Payload_Join);
            break;
        case OP_ATTACK:
            expected_payload_size = sizeof(Payload_Attack);
            break;
        case OP_LEAVE:
        case OP_HEARTBEAT:
            // 這些 opcode 可能沒有 payload 或 payload 很小
            expected_payload_size = 0;
            break;
        default:
            // 未知 opcode，只檢查基本長度
            return true;
    }
    
    uint32_t expected_total = sizeof(PacketHeader) + expected_payload_size;
    if (expected_payload_size > 0 && packet_length != expected_total) {
        LOG_WARN("Packet size mismatch for opcode 0x%02X: got %u, expected %u",
                 opcode, packet_length, expected_total);
        return false;
    }
    
    return true;
}

// 驗證攻擊封包的 payload 是否合法
bool input_validate_attack_payload(const Payload_Attack *attack) {
    if (!attack) {
        LOG_WARN("Attack payload is NULL");
        return false;
    }
    
    // 檢查傷害值是否在合理範圍內（例如：0-1000）
    // 注意：實際傷害由伺服器計算，這裡只是驗證客戶端發送的數值
    if (attack->damage < 0 || attack->damage > 1000) {
        LOG_WARN("Invalid damage value: %d (expected: 0-1000)", attack->damage);
        return false;
    }
    
    return true;
}



