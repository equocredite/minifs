#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "inode.h"
#include "disk_io.h"
#include "block.h"
#include "bit_util.h"
#include "str_util.h"

int get_inode_offset(int inode_id) {
    assert(is_correct_inode_id(inode_id));
    return MINIFS_BLOCK_SIZE * 3 + sizeof(struct inode) * inode_id;
}

int is_correct_inode_id(int inode_id) {
    return 0 <= inode_id && inode_id < N_INODES;
}

int is_allocated_inode_id(int inode_id) {
    if (!is_correct_inode_id(inode_id)) {
        return 0;
    }
    char byte;
    read_data(&byte, 1, MINIFS_BLOCK_SIZE * 2 + inode_id / 8);
    return !is_one(byte, inode_id % 8);
}

int is_dir(int inode_id) {
    if (!is_allocated_inode_id(inode_id)) {
        return 0;
    }
    struct inode inode;
    read_inode(&inode, inode_id);
    return inode.file_type == DIRECTORY;
}

int is_regular_file(int inode_id) {
    if (!is_allocated_inode_id(inode_id)) {
        return 0;
    }
    struct inode inode;
    read_inode(&inode, inode_id);
    return inode.file_type == REGULAR_FILE;
}

void read_inode(struct inode* inode, int inode_id) {
    assert(is_correct_inode_id(inode_id));
    read_data(inode, sizeof(struct inode), get_inode_offset(inode_id));
}

void write_inode(const struct inode* inode, int inode_id) {
    assert(is_correct_inode_id(inode_id));
    write_data(inode, sizeof(struct inode), get_inode_offset(inode_id));
}

int allocate_inode() {
    if (update_superblock(0, -1) == -1) {
        return -1;
    }
    int allocated_inode_id = -1;

    char inode_bitmap[N_INODES / 8];
    read_inode_bitmap(inode_bitmap);
    for (int i = 0; i < N_INODES / 8; ++i) {
        int index = first_bit(inode_bitmap[i]);
        if (index != -1) {
            allocated_inode_id = i * 8 + index;
            set_zero(inode_bitmap + i, index);
            break;
        }
    }
    write_inode_bitmap(inode_bitmap);

    return allocated_inode_id;
}

int free_inode(int inode_id) {
    if (!is_correct_inode_id(inode_id) || update_superblock(0, 1) == -1) {
        return -1;
    }

    char byte;
    read_data(&byte, 1, MINIFS_BLOCK_SIZE * 2 + inode_id / 8);
    if (is_one(byte, inode_id % 8)) {
        return -1;
    }
    set_one(&byte, inode_id % 8);
    write_data(&byte, 1, MINIFS_BLOCK_SIZE * 2 + inode_id / 8);

    return 0;
}

int init_dir(struct inode* inode, int inode_id, int parent_inode_id) {
    int block_id = allocate_block();
    if (block_id == -1) {
        return -1;
    }
    inode->direct[0] = block_id;
    inode->size      = sizeof(struct entry) * 2; // . and ..

    struct entry entry;
    entry.inode_id = inode_id;
    strcpy(entry.filename, ".");
    write_data(&entry, sizeof(struct entry), DATA_OFFSET + MINIFS_BLOCK_SIZE * block_id);
    entry.inode_id = parent_inode_id;
    strcpy(entry.filename, "..");
    write_data(&entry, sizeof(struct entry), DATA_OFFSET + MINIFS_BLOCK_SIZE * block_id + sizeof(struct entry));
    return 0;
}

int check_user_id(int inode_id) {
    struct inode inode;
    read_inode(&inode, inode_id);
    return (inode.user_id == 0 || inode.user_id == user_id);
}

int go(int inode_id, const char* filename) {
    if (!is_dir(inode_id)) {
        return -1;
    }

    struct inode inode;
    read_inode(&inode, inode_id);

    char block[MINIFS_BLOCK_SIZE];

    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            continue;
        }
        read_block(block, inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (void*)entry < (void*)block + MINIFS_BLOCK_SIZE; ++entry) {
            if (is_allocated_inode_id(entry->inode_id) && strcmp(entry->filename, filename) == 0) {
                if (!check_user_id(entry->inode_id)) {
                    return -1;
                }
                return entry->inode_id;
            }
        }
    }
    return -1;
}

int file_exists_in_dir(int dir_inode_id, const char* filename) {
    return is_correct_inode_id(go(dir_inode_id, filename));
}

int get_free_space_in_file(int inode_id) {
    struct inode inode;
    read_inode(&inode, inode_id);
    return MAX_FILE_SIZE - inode.size;
}

int traverse_from(int inode_id, char** path) {
    for (char** token = path; *token != NULL; ++token) {
        if (!is_correct_inode_id(inode_id)) {
            return -1;
        }
        inode_id = go(inode_id, *token);
    }
    return inode_id;
}

int traverse(const char* path_str) {
    char** path = split_path(path_str);
    int src_inode_id = (path_str[0] == '/' ? ROOT_INODE_ID : work_inode_id);
    int dest_inode_id = traverse_from(src_inode_id, path);
    free_tokens(path);
    return dest_inode_id;
}

int file_exists(const char* path) {
    return (traverse(path) != -1);
}

void get_parent_and_filename(const char* path_str, int* parent_inode_id, char** filename) {
    if (traverse(path_str) == ROOT_INODE_ID) {
        if (parent_inode_id != NULL) {
            *parent_inode_id = ROOT_INODE_ID;
        }
        if (filename != NULL) {
            *filename = malloc(2);
            strcpy(*filename, ".");
        }
        return;
    }

    char** path = split_path(path_str);
    int inode_id = (path_str[0] == '/' ? ROOT_INODE_ID : work_inode_id);
    char** path_elem;
    for (path_elem = path; *(path_elem + 1) != NULL; ++path_elem) {
        if (inode_id == -1) {
            break;
        }
        inode_id = go(inode_id, *path_elem);
    }

    if (parent_inode_id != NULL) {
        *parent_inode_id = inode_id;
    }
    if (filename != NULL) {
        *filename = malloc(strlen(*path_elem) + 1);
        strcpy(*filename, *path_elem);
    }
    free_tokens(path);
}

void increment_ref_count(int inode_id) {
    // yes, we don't need the whole inode and could only read a specific int,
    // but i don't want to fuck with offsets anymore
    struct inode inode;
    read_inode(&inode, inode_id);
    ++inode.ref_count;
    write_inode(&inode, inode_id);
}

void decrement_ref_count(int inode_id) {
    struct inode inode;
    read_inode(&inode, inode_id);
    --inode.ref_count;
    if (inode.ref_count == 0) {
        remove_inode(inode_id);
    } else {
        write_inode(&inode, inode_id);
    }
}

int add_file_to_dir(int dir_inode_id, int file_inode_id, const char* filename) {
    assert(is_dir(dir_inode_id));
    assert(is_allocated_inode_id(file_inode_id));
    assert(filename != NULL);

    increment_ref_count(file_inode_id);

    struct entry new_entry;
    new_entry.inode_id = file_inode_id;
    strcpy(new_entry.filename, filename);

    struct inode dir_inode;
    read_inode(&dir_inode, dir_inode_id);

    char block[MINIFS_BLOCK_SIZE];
    // search for an unoccupied space for the new entry
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            dir_inode.direct[i] = allocate_block();
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (void*)entry < (void*)block + MINIFS_BLOCK_SIZE; ++entry) {
            if (!is_correct_inode_id(entry->inode_id)) {
                *entry = new_entry;
                write_block(block, dir_inode.direct[i]);

                dir_inode.size += sizeof(struct entry);
                write_inode(&dir_inode, dir_inode_id);
                return 0;
            }
        }
    }

    return -1;
}

void remove_inode_regular(int inode_id) {
    struct inode inode;
    read_inode(&inode, inode_id);
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            break;
        }
        free_block(inode.direct[i]);
    }
    free_inode(inode_id);
}

void remove_inode(int);

void remove_inode_dir(int inode_id) {
    struct inode inode;
    read_inode(&inode, inode_id);
    char block[MINIFS_BLOCK_SIZE];

    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            break;
        }
        read_block(block, inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + MINIFS_BLOCK_SIZE; ++entry) {
            if (strcmp(entry->filename, ".") == 0 || strcmp(entry->filename, "..") == 0) {
                continue;
            }
            if (is_correct_inode_id(entry->inode_id)) {
                remove_inode(entry->inode_id);
            }
        }
        free_block(inode.direct[i]);
    }
    free_inode(inode_id);
}

void remove_inode(int inode_id) {
    if (is_regular_file(inode_id)) {
        remove_inode_regular(inode_id);
    } else if (is_dir(inode_id)) {
        remove_inode_dir(inode_id);
    }
}

int get_ref_count(int inode_id) {
    struct inode inode;
    read_inode(&inode, inode_id);
    return inode.ref_count;
}

int remove_file_from_dir(int dir_inode_id, int file_inode_id) {
    struct inode dir_inode;
    read_inode(&dir_inode, dir_inode_id);
    char block[MINIFS_BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            continue;
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + MINIFS_BLOCK_SIZE; ++entry) {
            if (entry->inode_id == file_inode_id) {
                entry->inode_id = -1;
                write_block(block, dir_inode.direct[i]);

                dir_inode.size -= sizeof(struct entry);
                write_inode(&dir_inode, dir_inode_id);
                if (entry->filename[0] != '.') {
                    decrement_ref_count(file_inode_id);
                }
                return 0;
            }
        }
    }
    return -1;
}

int min(int a, int b) {
    return a < b ? a : b;
}

int append_to_file(int inode_id, const void* data, int n_bytes) {
    struct inode inode;
    read_inode(&inode, inode_id);
    int ptr = inode.size / MINIFS_BLOCK_SIZE;
    int bytes_written = 0;
    if (inode.size % MINIFS_BLOCK_SIZE != 0) {
        int write_now = min(n_bytes, MINIFS_BLOCK_SIZE - (inode.size % MINIFS_BLOCK_SIZE));
        write_data(data, write_now, DATA_OFFSET + inode.direct[ptr] * MINIFS_BLOCK_SIZE + (inode.size % MINIFS_BLOCK_SIZE));
        bytes_written += write_now;
        ++ptr;
    }
    for (; bytes_written < n_bytes; ++ptr) {
        inode.direct[ptr] = allocate_block();
        if (!is_correct_block_id(inode.direct[ptr])) {
            inode.size += bytes_written;
            write_inode(&inode, inode_id);
            return -1;
        }
        int write_now = min(n_bytes - bytes_written, MINIFS_BLOCK_SIZE);
        write_data(data, write_now, DATA_OFFSET + inode.direct[ptr] * MINIFS_BLOCK_SIZE);
        bytes_written += write_now;

    }
    inode.size += bytes_written;
    write_inode(&inode, inode_id);
    return 0;
}

int rename_file_in_dir(int dir_inode_id, const char* filename, const char* new_filename) {
    struct inode dir_inode;
    read_inode(&dir_inode, dir_inode_id);
    char block[MINIFS_BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            continue;
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + MINIFS_BLOCK_SIZE; ++entry) {
            if (strcmp(entry->filename, filename) == 0) {
                strcpy(entry->filename, new_filename);
                write_block(block, dir_inode.direct[i]);
                return 0;
            }
        }
    }
    return -1;
}

int get_filename_by_inode(int dir_inode_id, int inode_id, char* filename) {
    struct inode dir_inode;
    read_inode(&dir_inode, dir_inode_id);
    char block[MINIFS_BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            continue;
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + MINIFS_BLOCK_SIZE; ++entry) {
            if (entry->inode_id == inode_id) {
                strcpy(filename, entry->filename);
                return 0;
            }
        }
    }
    return -1;
}
