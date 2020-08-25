#ifndef SUPERBLOCK_H
#define SUPERBLOCK_H

struct superblock {
    int magic;
    int n_free_blocks;
    int n_free_inodes;
};

void read_superblock(struct superblock* sb);

void write_superblock(const struct superblock* sb);

void read_block(void* block, int block_id);

void write_block(const void* block, int block_id);

int is_correct_block_id(int block_id);

int update_superblock(int delta_free_blocks, int delta_free_inodes);

int get_n_free_blocks();

int get_n_free_inodes();

int allocate_block();

int free_block(int block_id);

int get_n_blocks_needed(int size);

#endif // SUPERBLOCK_H
