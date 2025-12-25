/* src/server/security/replay_protection.c - Simple replay attack protection */
#include "replay_protection.h"
#include "../../common/log.h"

// Initialize the Replay Protection state
void replay_protection_init(ReplayProtection *rp) {
    if (!rp) return;
    rp->last_seq_num = 0;
    rp->initialized = false;
}

// Validate packet sequence number to prevent replay attacks
// Simple strategy: The new seq_num must be greater than the last received seq_num
// Handles uint32_t overflow (allows rollover from 0xFFFFFFFF to 0)
bool replay_protection_validate(ReplayProtection *rp, uint32_t seq_num) {
    if (!rp) {
        return false;
    }

    // First packet: accept directly
    if (!rp->initialized) {
        rp->last_seq_num = seq_num;
        rp->initialized = true;
        return true;
    }

    // Check for replayed packets
    // Strategy: New seq_num must be > last_seq_num
    // However, considering overflow: if last_seq_num is near 0xFFFFFFFF
    // and seq_num is small (near 0), it may be a valid rollover and should be accepted

    // Simple implementation: only check if equal to or less than the last seq_num (no duplicates allowed)
    // Allow overflow: if seq_num < last_seq_num and the difference is large, might be overflow, accept it
    uint32_t diff;
    
    if (seq_num > rp->last_seq_num) {
        // Normal case: new packet
        diff = seq_num - rp->last_seq_num;
    } else {
        // Potential replay or overflow
        diff = rp->last_seq_num - seq_num;
        
        // If the difference is large (greater than half of uint32_t), likely overflow, accept it
        // Otherwise, treat as a replay attack
        if (diff < 0x7FFFFFFF) {
            LOG_WARN("Possible replay attack detected: seq_num=%u, last_seq_num=%u (diff=%u)",
                     seq_num, rp->last_seq_num, diff);
            return false;
        }
        // Large difference, likely overflow, accept it
    }

    // Update the last received sequence number
    rp->last_seq_num = seq_num;
    return true;
}
