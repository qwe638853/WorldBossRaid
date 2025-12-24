#ifndef INPUT_VALIDATOR_H
#define INPUT_VALIDATOR_H

#include <stdbool.h>
#include "common/protocol.h"

// 驗證用戶名是否合法
// 返回 true 表示合法，false 表示不合法
bool input_validate_username(const char *username);

// 驗證封包的 OpCode 是否合法
bool input_validate_opcode(uint16_t opcode);

// 驗證封包長度是否合法（根據 OpCode）
bool input_validate_packet_size(uint16_t opcode, uint32_t packet_length);

// 驗證攻擊封包的 payload 是否合法
bool input_validate_attack_payload(const Payload_Attack *attack);

#endif



