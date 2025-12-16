// client_handler.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../common/protocol.h"
#include "gamestate.h"
#include "dice.h"

// handle client connection in worker process
void handle_client(int *client_socket_ptr) {
    int client_socket = *client_socket_ptr;
    GamePacket packet;
    ssize_t bytes_read;
    int player_id = -1;

    // TODO: Implement packet receiving and processing logic
    // 1. Receive OP_JOIN
    // 2. Call game_player_join() to assign player_id
    // 3. Send OP_JOIN_RESP
    // 4. Loop: Receive OP_ATTACK, process, send OP_GAME_STATE
    
    close(client_socket);
    if (player_id >= 0) {
        game_player_leave(player_id);
    }
    exit(0);
}

