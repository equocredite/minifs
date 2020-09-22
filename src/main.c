/* TO-DO:
1. replace manual option parsing with getopt()
2. indirect pointers in inodes
3. make return values (void or return code) consistent
4. block and entry iterators for an inode
6. timestamps
7. separate network messaging between data socket and sync socket
8. wait for epoll notification instead of spinning
*/

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>

#include "globals.h"
#include "bit_util.h"
#include "str_util.h"
#include "block.h"
#include "inode.h"
#include "interface.h"
#include "disk_io.h"
#include "net_io.h"

int disk_fd;
_Thread_local int nested;
_Thread_local int client_fd;
_Thread_local int work_inode_id;
_Thread_local int user_id;

FILE* log_fp;

void daemonize() {
    pid_t pid;
    if ((pid = fork()) < 0) {
        exit(1);
    } else if (pid > 0) {
        exit(0);
    }
    if (setsid() < 0) {
        exit(1);
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    if ((pid = fork()) < 0) {
        exit(1);
    } else if (pid > 0) {
        exit(0);
    }
    umask(0);
}

void log_msg(const char* msg) {
    fprintf(log_fp, "%s\n", msg);
    fflush(log_fp);
}

void create_root_dir() {
    struct inode inode;
    inode.file_type = DIRECTORY;
    inode.ref_count = 1;
    inode.created       =
    inode.last_accessed =
    inode.last_modified = time(NULL);
    inode.user_id       = 0;
    memset(inode.direct, -1, sizeof(inode.direct));
    allocate_inode(); // will return 0
    init_dir(&inode, 0, 0);
    write_inode(&inode, 0);
}

void create_disk(const char* path) {
    disk_fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (disk_fd == -1) {
        log_msg("couldn't create disk");
        exit(1);
    }

    // initialize disk with -1's
    char buf[MINIFS_BLOCK_SIZE];
    memset(buf, -1, sizeof(buf));
    for (int i = 0; i * MINIFS_BLOCK_SIZE < DISK_SIZE; ++i) {
        write_data(buf, MINIFS_BLOCK_SIZE, MINIFS_BLOCK_SIZE * i);
    }

    struct superblock sb = {
        .magic         = MAGIC,
        .n_free_blocks = N_BLOCKS,
        .n_free_inodes = N_INODES
    };
    write_superblock(&sb);
    create_root_dir();
}

int setup_server(int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        log_msg("couldn't create socket");
        exit(1);
    }
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        log_msg("setsockopt error");
    }
    struct sockaddr_in serv_addr, client_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_fd, (struct sockaddr*)(&serv_addr), sizeof(serv_addr)) < 0) {
        log_msg("bind error");
        exit(1);
    }
    listen(sock_fd, 1);
    return sock_fd;
}

void* process_client(void* new_client_fd) {
    client_fd = *((int*)(new_client_fd));
    free((int*)new_client_fd);
    work_inode_id = ROOT_INODE_ID;
    nested = 0;

    char buf[1024];
    // log in
    recv_msg(buf);
    user_id = atoi(buf);
    send_success();

    while (recv_msg(buf) > 0) {
        printf("got msg: %s\n", buf);
        char** tokens = split_str(buf, " ");

        if (strcmp(tokens[0], "exit") == 0) {
            free_tokens(tokens);
            return NULL;
        } else if (strcmp(tokens[0], "help") == 0) {
            display_help();
        } else if (strcmp(tokens[0], "pwd") == 0) {
            print_work_path();
        } else if (strcmp(tokens[0], "cd") == 0) {
            change_dir(tokens[1]);
        } else if (strcmp(tokens[0], "ls") == 0) {
            if (tokens[1] != NULL && strcmp(tokens[1], "--all") == 0) {
                list_entries(tokens[2], 1);
            } else {
                list_entries(tokens[1], 0);
            }
        } else if (strcmp(tokens[0], "cp") == 0) {
            if (strcmp(tokens[1], "--from-local") == 0) {
                copy_from_local(tokens[3]);
            } else if (strcmp(tokens[1], "--to-local") == 0) {
                copy_to_local(tokens[2]);
            } else {
                copy(tokens[1], tokens[2]);
            }
        } else if (strcmp(tokens[0], "rm") == 0) {
            remove(tokens[1]);
        } else if (strcmp(tokens[0], "mv") == 0) {
            move(tokens[1], tokens[2]);
        } else if (strcmp(tokens[0], "mkdir") == 0) {
            create_file(tokens[1], DIRECTORY);
        } else if (strcmp(tokens[0], "touch") == 0) {
            create_file(tokens[1], REGULAR_FILE);
        } else if (strcmp(tokens[0], "cat") == 0) {
            print_contents(tokens[1]);
        } else {
            send_failure("unknown command; type 'help' for help\n");
        }
        free_tokens(tokens);
    }
    return NULL;
}

int main(int argc, char** argv) {
    daemonize();
    log_fp = fopen("log", "w+");
    create_disk("/dev/minifs");
    int sock_fd = setup_server(argc >= 2 ? atoi(argv[1]) : 8080);
    while (1) {
        int* new_client_fd = malloc(sizeof(int));
        *new_client_fd = accept(sock_fd, NULL, NULL);
        pthread_t thread;
        pthread_create(&thread, NULL, process_client, new_client_fd);
    }
    close(disk_fd);
}
