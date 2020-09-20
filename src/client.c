#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include "globals.h"
#include "str_util.h"

int con_fd;
char buf[MINIFS_BLOCK_SIZE];
char work_path[MAX_PATH_LEN];

void create_connection(const char* ip, int port) {
    con_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (con_fd < 0) {
        puts("couldn't create socket");
        exit(1);
    }
    if (setsockopt(con_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        puts("setsockopt error");
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) != 1) {
        puts("invalid address");
        exit(1);
    }
    if (connect(con_fd, (struct sockaddr*)(&serv_addr), sizeof(serv_addr)) < 0) {
        puts("connection failed");
        exit(1);
    }
    fcntl(con_fd, F_SETFL, fcntl(con_fd, F_GETFL, 0) | O_NONBLOCK);
}

void send_nbytes(const void* buf, int n) {
    send(con_fd, buf, n, 0);
}

void send_msg(const char* buf) {
    send_nbytes(buf, strlen(buf));
}

void get_cmd() {
    fgets(buf, sizeof(buf), stdin);
    if (buf[strlen(buf) - 1] == '\n') {
        buf[strlen(buf) - 1] = '\0';
    }
}

// milliseconds
void sleep_for(int ms) {
    struct timespec ts = {
        .tv_sec = (ms / 1000),
        .tv_nsec = (ms % 1000) * (int)(1e6)
    };
    nanosleep(&ts, &ts);
}

//milliseconds
double elapsed_time(clock_t start) {
    return 1000.0 * (clock() - start) / (1.0 * CLOCKS_PER_SEC);
}

// 300 milliseconds timeout
int recv_nbytes(int n) {
    clock_t start = clock();
    int bytes_read;
    while ((bytes_read = recv(con_fd, buf, n, 0)) == -1) {
        if (errno == EAGAIN) {
            if (elapsed_time(start) > 300.0) {
                return 0;
            }
            //sleep_for(50);
        } else {
            puts("connection broke");
            exit(1);
        }
    }
    buf[bytes_read] = '\0';
    return bytes_read;
}

int get_response() {
    return recv_nbytes(sizeof(buf) - 1);
}

int is_success() {
    recv_nbytes(1);
    return buf[0] == '1';
}

int is_failure() {
    return !is_success();
}

void skip_succfail() {
    is_success();
}

void print_response() {
    while (get_response() > 0) {
        printf("%s", buf);
    }
}

void discard_response() {
    while (get_response() > 0) {}
}

void print_prompt() {
    printf("%s$ ", work_path);
}

void change_dir() {
    send_msg(buf);
    if (is_success()) {
        work_path[0] = '\0';
        int len;
        while ((len = get_response()) > 0) {
            *strchrnul(buf, '\n') = '\0';
            strcat(work_path, buf);
        }
    } else {
        puts("error");
        print_response();
    }
}

int copy_from_local(const char* src_path) {
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        printf("%s: couldn't open\n", src_path);
        return -1;
    }
    struct stat src_stat;
    if (fstat(src_fd, &src_stat) == -1) {
        printf("%s: couldn't obtain metadata\n", src_path);
        close(src_fd);
        return -1;
    }
    if (!S_ISREG(src_stat.st_mode)) {
        printf("%s: not a regular file\n", src_path);
        close(src_fd);
        return -1;
    }

    send_msg(buf);
    skip_succfail(); // sync
    send_nbytes(&src_stat.st_size, sizeof(src_stat.st_size));

    if (is_failure()) {
        print_response();
        close(src_fd);
        return -1;
    }

    FILE* src_fp = fdopen(src_fd, "r");
    char buf[MINIFS_BLOCK_SIZE + 1];
    for (int n_bytes_left = src_stat.st_size; n_bytes_left > 0; n_bytes_left -= MINIFS_BLOCK_SIZE) {
        int n_bytes_cur = (n_bytes_left < MINIFS_BLOCK_SIZE ? n_bytes_left : MINIFS_BLOCK_SIZE);
        fread(buf, 1, n_bytes_cur, src_fp);
        buf[n_bytes_cur] = '\0';
        send_nbytes(buf, n_bytes_cur);
    }

    fclose(src_fp);
    return 0;
}

int copy_to_local(const char* dest_path) {
    FILE* dest_fp = fopen(dest_path, "w");
    if (dest_fp == NULL) {
        printf("%s: couldn't open or create\n", dest_path);
        return -1;
    }
    send_msg(buf);
    if (is_failure()) {
        puts("error");
        print_response();
        fclose(dest_fp);
        return -1;
    }
    int len;
    while ((len = get_response()) > 0) {
        fwrite(buf, 1, len, dest_fp);
    }
    fclose(dest_fp);
    return 0;
}

void get_user_id() {
    while (1) {
        printf("user id: ");
        get_cmd();
        if (atoi(buf) <= 0) {
            puts("invalid id");
            continue;
        }
        return;
    }
}

void handle_sigint(int signum) {
    if (con_fd != -1) {
        send_msg("exit");
    }
    puts("");
    exit(0);
}

void setup_handler(int signum, struct sigaction* action, void handle(int)) {
    memset(action, 0, sizeof(struct sigaction));
    action->sa_handler = handle;
    action->sa_flags = SA_RESTART;
    sigaction(signum, action, NULL);
}

int main(int argc, char** argv) {
    con_fd = -1;
    struct sigaction action_int;
    setup_handler(SIGINT, &action_int, handle_sigint);

    const char* ip = (argc >= 2 ? argv[1] : "127.0.0.1");
    const int port = (argc >= 3 ? atoi(argv[2]) : 8080);

    get_user_id();
    create_connection(ip, port);
    strcpy(work_path, "/");
    send_msg(buf); // user id
    skip_succfail();

    while (1) {
        print_prompt();
        get_cmd();

        char** tokens = split_str(buf, " ");
        // some special cases
        // if none of these, then we simply send data and print response
        if (strcmp(tokens[0], "exit") == 0) {
            free_tokens(tokens);
            break;
        } else if (strcmp(tokens[0], "cd") == 0) {
            change_dir();
        } else if (strcmp(tokens[0], "cp") == 0 && strcmp(tokens[1], "--from-local") == 0) {
            copy_from_local(tokens[2]);
        } else if (strcmp(tokens[0], "cp") == 0 && strcmp(tokens[1], "--to-local") == 0) {
            copy_to_local(tokens[3]);
        } else {
            send_msg(buf);
            if (is_failure()) {
                puts("error");
            }
            print_response();
        }
        free_tokens(tokens);
    }
    close(con_fd);
}
