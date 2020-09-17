/* TO-DO:
1. replace manual option parsing with getopt()
2. indirect pointers in inodes
3. make return values (void or return code) consistent
4. block and entry iterators for an inode
6. timestamps
7. separate network messaging between data socket and sync socket
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

#include "globals.h"
#include "bit_util.h"
#include "str_util.h"
#include "block.h"
#include "inode.h"
#include "interface.h"
#include "disk_io.h"
#include "net_io.h"

// just so that their definitions exist
int disk_fd = -1;
int client_fd = -1;
int work_inode_id = -1;
int disable_succfail = 0;

int open_disk(const char* disk_path) {
    disk_fd = open(disk_path, O_RDWR);
    struct superblock sb;
    read_superblock(&sb);
    if (sb.magic != MAGIC) {
        puts("corrupted disk");
        return -1;
    }
    return 0;
}

void create_root_dir() {
    struct inode inode;
    inode.file_type = DIRECTORY;
    inode.ref_count = 1;
    inode.created       =
    inode.last_accessed =
    inode.last_modified = time(NULL);
    memset(inode.direct, -1, sizeof(inode.direct));
    allocate_inode(); // will return 0
    init_dir(&inode, 0, 0);
    write_inode(&inode, 0);
}

void create_disk(const char* path) {
    disk_fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (disk_fd == -1) {
        return;
    }
    ftruncate(disk_fd, DISK_SIZE);

    // initialize disk with -1's
    char buf[BLOCK_SIZE];
    memset(buf, -1, sizeof(buf));
    lseek(disk_fd, 0, SEEK_SET);
    for (int i = 0; i * BLOCK_SIZE < DISK_SIZE; ++i) {
        write_data(buf, BLOCK_SIZE, -1);
    }

    struct superblock sb = {
        .magic         = MAGIC,
        .n_free_blocks = N_BLOCKS,
        .n_free_inodes = N_INODES
    };
    write_superblock(&sb);
    create_root_dir();
}

int main(int argc, char** argv) {
    const char* disk_path = "disk";
    const int port = (argc >= 2 ? atoi(argv[1]) : 8080);

    if (access(disk_path, R_OK | W_OK) != -1) {
        puts("open existing disk");
        if (open_disk(disk_path) == -1) {
            close(disk_fd);
            exit(1);
        }
    } else {
        puts("create a disk");
        create_disk(disk_path);
    }
    if (disk_fd == -1) {
        puts("error opening/creating fs");
        exit(1);
    }
    work_inode_id = ROOT_INODE_ID;

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        puts("couldn't create socket");
        exit(1);
    }
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        puts("setsockopt error");
    }
    struct sockaddr_in serv_addr, client_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_fd, (struct sockaddr*)(&serv_addr), sizeof(serv_addr)) < 0) {
        puts("bind error");
        exit(1);
    }
    listen(sock_fd, 1);

    int addrlen;
    client_fd = accept(sock_fd, (struct sockaddr*)(&client_addr), (socklen_t*)(&addrlen));
    //fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);

    char buf[1024];
    while (recv_msg(buf) > 0) {
        //printf("got msg: %s\n", buf);
        char** tokens = split_str(buf, " ");

        if (strcmp(tokens[0], "help") == 0) {
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

    close(disk_fd);
}
