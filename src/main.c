/* TO-DO:
1. replace manual option parsing with getopt()
2. indirect pointers in inodes
3. make return values (void or return code) consistent
4. block and entry iterators for an inode
5. add more argument checking, and replace is_correct -> is_allocated / is_dir etc.
6. timestamps
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

#include "globals.h"
#include "bit_util.h"
#include "str_util.h"
#include "block.h"
#include "inode.h"
#include "interface.h"
#include "disk_io.h"

// just so that their definitions exist
int disk_fd = 0;
int work_inode_id = 0;

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

void check_error(int ret_code) {
    if (ret_code == -1) {
        puts("error");
    }
}

int main(int argc, char** argv) {
    const char* disk_path = argv[1];
    if (access(disk_path, R_OK | W_OK) != -1) {
        puts("open existing disk");
        if (open_disk(disk_path) == -1) {
            puts("exiting");
            close(disk_fd);
            return 0;
        }
    } else {
        puts("create a disk");
        create_disk(disk_path);
    }
    if (disk_fd == -1) {
        puts("error opening/creating fs");
        return 0;
    }
    work_inode_id = ROOT_INODE_ID;

    while (1) {
        print_work_path();
        printf("$ ");
        char buf[512];
        fgets(buf, sizeof(buf), stdin);
        if (buf[strlen(buf) - 1] == '\n') {
            buf[strlen(buf) - 1] = '\0';
        }
        char** tokens = split_str(buf, " ");

        if (strcmp(tokens[0], "exit") == 0) {
            free_tokens(tokens);
            break;
        } else if (strcmp(tokens[0], "help") == 0) {
            display_help();
        } else if (strcmp(tokens[0], "pwd") == 0) {
            print_work_path();
            puts("");
        } else if (strcmp(tokens[0], "cd") == 0) {
            check_error(change_dir(tokens[1]));
        } else if (strcmp(tokens[0], "ls") == 0) {
            if (tokens[1] != NULL && strcmp(tokens[1], "--all") == 0) {
                check_error(list_entries(tokens[2], 1));
            } else {
                check_error(list_entries(tokens[1], 0));
            }
        } else if (strcmp(tokens[0], "cp") == 0) {
            if (strcmp(tokens[1], "--from-local") == 0) {
                check_error(copy_from_local(tokens[2], tokens[3]));
            } else if (strcmp(tokens[1], "--to-local") == 0) {
                check_error(copy_to_local(tokens[2], tokens[3]));
            } else {
                check_error(copy(tokens[1], tokens[2]));
            }
        } else if (strcmp(tokens[0], "rm") == 0) {
            check_error(remove(tokens[1]));
        } else if (strcmp(tokens[0], "mv") == 0) {
            check_error(move(tokens[1], tokens[2]));
        } else if (strcmp(tokens[0], "mkdir") == 0) {
            check_error(create_file(tokens[1], DIRECTORY));
        } else if (strcmp(tokens[0], "touch") == 0) {
            check_error(create_file(tokens[1], REGULAR_FILE));
        } else if (strcmp(tokens[0], "cat") == 0) {
            check_error(print_contents(tokens[1]));
        } else {
            puts("unknown command; type 'help' for help");
        }

        free_tokens(tokens);
    }

    close(disk_fd);
}
