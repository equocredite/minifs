#ifndef GLOBALS_H
#define GLOBALS_H

#include <pthread.h>

#define MAGIC 13371488

enum file_type {
    REGULAR_FILE,
    DIRECTORY
};

#define MINIFS_BLOCK_SIZE 1024
#define N_BLOCKS   128 // the actual data blocks, not including special blocks at the beginning
#define N_INODES   128
#define MINIFS_INODE_SIZE 128
#define ROOT_INODE_ID 0
#define N_DIRECT_PTRS 22
#define FILENAME_LEN 28

// depth can't be more than N_INODES
// the first of them is root inode, whose name is empty
// the rest (N_INODES - 1) dirs have names up to FILENAME_LEN in length
// after each filename there is a slash
// plus a symbol for null terminator and plus two just in case :)
#define MAX_PATH_LEN ((N_INODES - 1) * FILENAME_LEN + N_INODES + 3)

#define DISK_SIZE (MINIFS_BLOCK_SIZE * 3 + N_INODES * MINIFS_INODE_SIZE + MINIFS_BLOCK_SIZE * N_BLOCKS)



// #define DISK_SIZE           MINIFS_BLOCK_SIZE \                      // superblock
//                           + MINIFS_BLOCK_SIZE \                    // block bitmap (with padding to a whole block)
//                           + MINIFS_BLOCK_SIZE                      // inode bitmap (with padding to a whole block)
//                           + N_INODES * MINIFS_INODE_SIZE // inode table (an integral number of blocks)
//                           + MINIFS_BLOCK_SIZE * N_BLOCKS;          // data blocks
#define DATA_OFFSET (MINIFS_BLOCK_SIZE * 3 + MINIFS_INODE_SIZE * N_INODES)
#define MAX_FILE_SIZE (N_DIRECT_PTRS * MINIFS_BLOCK_SIZE) // one actual data block for each direct pointer

int disk_fd;
// set to 1 when current "upper level" function was called by another one
// if 1, success/failure messages over the net are disabled, and locks are not taken
// yes, it's a crutch
extern _Thread_local int nested;
extern _Thread_local int client_fd; // returned by accept()
extern _Thread_local int work_inode_id;
extern _Thread_local int user_id;

pthread_rwlock_t lock;

#endif // GLOBALS_H
