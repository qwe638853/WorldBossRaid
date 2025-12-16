#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

// Handle client connection in worker process
// This function will be called in a forked child process
void handle_client(int *client_socket_ptr);

#endif

