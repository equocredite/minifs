#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#include "globals.h"
#include "net_io.h"

void send_nbytes(const void* buf, int n) {
    send(client_fd, buf, n, 0);
}

void send_msg(const char* buf) {
    send_nbytes(buf, strlen(buf));
}

void send_success() {
    if (nested) {
        return;
    }
    send_msg("1");
}

void send_failure(const char* msg) {
    if (nested) {
        return;
    }
    send_msg("0");
    send_msg(msg);
}

int recv_nbytes(void* buf, int n) {
    return recv(client_fd, buf, n, 0);
}

// to-do: fix this shit
int recv_msg(char buf[MINIFS_BLOCK_SIZE]) {
    memset(buf, 0, MINIFS_BLOCK_SIZE);
    int bytes_read = recv(client_fd, buf, MINIFS_BLOCK_SIZE - 1, 0);
    return bytes_read;
}

void discard_msg() {
    char buf[MINIFS_BLOCK_SIZE];
    while (recv_msg(buf) > 0) {};
}
