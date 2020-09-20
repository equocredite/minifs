#ifndef INODE_H
#define INODE_H

#include "inode.h"
#include "disk_io.h"

struct inode {
    enum file_type file_type;
    int            size;
    int            user_id;
    int            ref_count;
    int            direct[N_DIRECT_PTRS];
    //int            indirect;
    time_t         created;
    time_t         last_accessed;
    time_t         last_modified;
};

struct entry {
    int    inode_id;
    char   filename[FILENAME_LEN];
};

int get_inode_offset(int inode_id);

int is_correct_inode_id(int inode_id);

int is_allocated_inode_id(int inode_id);

int is_dir(int inode_id);

int is_regular_file(int inode_id);

void read_inode_bitmap(void* inode_bitmap);

void write_inode_bitmap(const void* inode_bitmap);

void read_inode(struct inode* inode, int inode_id);

void write_inode(const struct inode* inode, int inode_id);

int allocate_inode();

int free_inode(int inode_id);

int init_dir(struct inode* inode, int inode_id, int parent_inode_id);

int check_inode_id(int inode_id);

int go(int inode_id, const char* filename);

int file_exists_in_dir(int dir_inode_id, const char* filename);

int get_free_space_in_file(int inode_id);

int traverse_from(int inode_id, char** path);

// i should probably have named this function 'resolve', but it's too late;
// given an absolute or a relative path, return the corresponding file's inode id
int traverse(const char* path_str);

int file_exists(const char* path);

void get_parent_and_filename(const char* path_str, int* parent_inode_id, char** filename);

void increment_ref_count(int inode_id);

void decrement_ref_count(int inode_id);

int add_file_to_dir(int dir_inode_id, int file_inode_id, const char* filename);

void remove_inode_regular(int inode_id);

void remove_inode(int);

void remove_inode_dir(int inode_id);

void remove_inode(int inode_id);

int get_ref_count(int inode_id);

int remove_file_from_dir(int dir_inode_id, int file_inode_id);

int min(int a, int b);

int append_to_file(int inode_id, const void* data, int n_bytes);

int rename_file_in_dir(int dir_inode_id, const char* filename, const char* new_filename);

int get_filename_by_inode(int dir_inode_id, int inode_id, char* filename);

#endif // INODE_H
