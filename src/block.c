#include <assert.h>
#include <string.h>

#include "block.h"
#include "globals.h"
#include "disk_io.h"
#include "bit_util.h"

void read_superblock(struct superblock* sb) {
    read_data(sb, sizeof(struct superblock), 0);
}

void write_superblock(const struct superblock* sb) {
    write_data(sb, sizeof(struct superblock), 0);
}

void read_block(void* block, int block_id) {
    assert(is_correct_block_id(block_id));
    read_data(block, MINIFS_BLOCK_SIZE, DATA_OFFSET + MINIFS_BLOCK_SIZE * block_id);
}

void write_block(const void* block, int block_id) {
    assert(is_correct_block_id(block_id));
    write_data(block, MINIFS_BLOCK_SIZE, DATA_OFFSET + MINIFS_BLOCK_SIZE * block_id);
}

int is_correct_block_id(int block_id) {
    return 0 <= block_id && block_id < N_BLOCKS;
}

int update_superblock(int delta_free_blocks, int delta_free_inodes) {
    struct superblock sb;
    read_superblock(&sb);
    int new_n_free_blocks = sb.n_free_blocks + delta_free_blocks;
    int new_n_free_inodes = sb.n_free_inodes + delta_free_inodes;
    if (new_n_free_blocks < 0 || new_n_free_blocks > N_BLOCKS || new_n_free_inodes < 0 || new_n_free_inodes > N_INODES) {
        return -1;
    }
    sb.n_free_blocks = new_n_free_blocks;
    sb.n_free_inodes = new_n_free_inodes;
    write_superblock(&sb);
    return 0;
}

int get_n_free_blocks() {
    struct superblock sb;
    read_superblock(&sb);
    return sb.n_free_blocks;
}

int get_n_free_inodes() {
    struct superblock sb;
    read_superblock(&sb);
    return sb.n_free_inodes;
}

int allocate_block() {
    if (update_superblock(-1, 0) == -1) {
        return -1;
    }
    int allocated_block_id = -1;

    char block_bitmap[N_BLOCKS / 8];
    read_block_bitmap(block_bitmap);
    for (int i = 0; i < N_BLOCKS / 8; ++i) {
        int index = first_bit(block_bitmap[i]);
        if (index != -1) {
            allocated_block_id = i * 8 + index;
            set_zero(block_bitmap + i, index);
            break;
        }
    }
    write_block_bitmap(block_bitmap);

    char buf[MINIFS_BLOCK_SIZE];
    memset(buf, -1, sizeof buf);
    write_block(buf, allocated_block_id);

    return allocated_block_id;
}

int free_block(int block_id) {
    if (!is_correct_block_id(block_id) || update_superblock(1, 0) == -1) {
        return -1;
    }

    // we don't actually need the whole bitmap like when allocating, just one tiny byte
    char byte;
    read_data(&byte, 1, MINIFS_BLOCK_SIZE + block_id / 8);
    if (is_one(byte, block_id % 8)) {
        // block wasn't allocated
        return -1;
    }
    set_one(&byte, block_id % 8);
    write_data(&byte, 1, MINIFS_BLOCK_SIZE + block_id / 8);

    return 0;
}

int get_n_blocks_needed(int size) {
    int n_blocks_needed = (size + MINIFS_BLOCK_SIZE - 1) / MINIFS_BLOCK_SIZE; // round up
                                // need an additional indirection level
    return n_blocks_needed/* + (int)(n_blocks_needed > N_DIRECT_PTRS)*/;
}
