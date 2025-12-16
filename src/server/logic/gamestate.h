#ifndef GAMESTATE_H
#define GAMESTATE_H

#include <pthread.h>           // to use pthread_mutex_t
#include "../../common/protocol.h" // to use Payload_GameState

// the core: shared memory structure (Shared Memory Structure)
// this is the data that is shared by all processes in the OS memory
typedef struct {
    int boss_hp;        // the current health of the boss
    int max_hp;         // the maximum health of the boss
    int online_count;   // the number of online players
    pthread_mutex_t lock; // the lock for the shared memory
} GameSharedData;

// function declaration for the game state

void game_init();

// destroy the shared memory (release the shared memory, delete the lock)
void game_destroy();

// player join, return the assigned player ID (simply use online_count as the ID)
int game_player_join();

// player leave
void game_player_leave(int player_id);

// attack the boss
// damage: the damage this time
// state_out: (output parameter) the latest state after the attack, let the worker take it and return to the client
void game_attack_boss(int damage, Payload_GameState *state_out);

// read the current state (read-only, but also needs the lock)
void game_get_state(Payload_GameState *state_out);

#endif 