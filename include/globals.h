#ifndef GLOBALS_H
#define GLOBALS_H

#define MAGIC 13371488

enum file_type {
    REGULAR_FILE,
    DIRECTORY
};

#define BLOCK_SIZE 1024
#define N_BLOCKS   128 // the actual data blocks, not including special blocks at the beginning
#define N_INODES   128
#define INODE_SIZE 128

#define N_DIRECT_PTRS 23

#define FILENAME_LEN 28

// depth can't be more than N_INODES
// the first of them is root inode, whose name is empty
// the rest (N_INODES - 1) dirs have names up to FILENAME_LEN in length
// after each filename there is a slash
// plus a symbol for null terminator and plus two just in case :)
#define MAX_PATH_LEN ((N_INODES - 1) * FILENAME_LEN + N_INODES + 3)

#define DISK_SIZE (BLOCK_SIZE * 3 + N_INODES * INODE_SIZE + BLOCK_SIZE * N_BLOCKS)



// #define DISK_SIZE           BLOCK_SIZE \                      // superblock
//                           + BLOCK_SIZE \                    // block bitmap (with padding to a whole block)
//                           + BLOCK_SIZE                      // inode bitmap (with padding to a whole block)
//                           + N_INODES * INODE_SIZE // inode table (an integral number of blocks)
//                           + BLOCK_SIZE * N_BLOCKS;          // data blocks
#define DATA_OFFSET (BLOCK_SIZE * 3 + INODE_SIZE * N_INODES)
#define MAX_FILE_SIZE (N_DIRECT_PTRS * BLOCK_SIZE) // one actual data block for each direct pointer

#define ROOT_INODE_ID 0
int disk_fd;
int work_inode_id;

#endif // GLOBALS_H
