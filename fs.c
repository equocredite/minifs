/* TO-DO:
1. magic number in superblock
2. indirect pointers in inodes
3. make return values (void or return code) consistent
4. block and entry iterators for an inode
5. add more argument checking, and replace is_correct -> is_allocated / is_dir etc.
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
#include <time.h>

#define MAGIC 13371488

struct superblock {
    int magic;
    int n_free_blocks;
    int n_free_inodes;
};

enum file_type {
    REGULAR_FILE,
    DIRECTORY
};

#define N_DIRECT_PTRS 23

struct inode {
    enum file_type file_type;
    int            size;
    int            ref_count;
    int            direct[N_DIRECT_PTRS];
    //int            indirect;
    time_t         created;
    time_t         last_accessed;
    time_t         last_modified;
};

#define BLOCK_SIZE 1024
#define N_BLOCKS   128 // the actual data blocks, not including special blocks at the beginning
#define N_INODES   128

#define FILENAME_LEN 28

// depth can't be more than N_INODES
// the first of them is root inode, whose name is empty
// the rest (N_INODES - 1) dirs have names up to FILENAME_LEN in length
// after each filename there is a slash
// plus a symbol for null terminator and plus two just in case :)
#define MAX_PATH_LEN ((N_INODES - 1) * FILENAME_LEN + N_INODES + 3)

const int DISK_SIZE       = BLOCK_SIZE                      // superblock
                          + BLOCK_SIZE                      // block bitmap (with padding to a whole block)
                          + BLOCK_SIZE                      // inode bitmap (with padding to a whole block)
                          + N_INODES * sizeof(struct inode) // inode table (an integral number of blocks)
                          + BLOCK_SIZE * N_BLOCKS;          // data blocks
const int DATA_OFFSET     = BLOCK_SIZE * 3 + sizeof(struct inode) * N_INODES;
const int MAX_FILE_SIZE   = N_DIRECT_PTRS * BLOCK_SIZE; // one actual data block for each direct pointer
                          //+ (BLOCK_SIZE / sizeof(int)) * BLOCK_SIZE; // a single indirect pointer that expands into a whole block of pointers

const int ROOT_INODE_ID = 0;
int disk_fd;
int work_inode_id;

struct entry {
    int    inode_id;
    char   filename[FILENAME_LEN];
};

int first_bit(int x) {
    for (int i = 0; i < 8; ++i) {
        if (x & 1) {
            return i;
        }
        x >>= 1;
    }
    return -1;
}

int is_one(char num, int bit) {
    assert(0 <= bit && bit < 8);
    return num & (1 << bit);
}

void set_one(char* num, int bit) {
    assert(!is_one(*num, bit));
    *num |= (1 << bit);
}

void set_zero(char* num, int bit) {
    assert(is_one(*num, bit));
    *num ^= (1 << bit);
}

char** split_str(const char* const_str, const char* delim) {
    if (const_str == NULL) {
        return NULL;
    }
    size_t len = strlen(const_str);
    char str[len + 1];
    strcpy(str, const_str);
    // worst case: a/b/c/d/e/.../z -> ceil(len / 2), plus null terminator
    char** tokens = calloc((len + 3) / 2, sizeof(char*));
    char* token;
    char* saveptr = NULL;
    size_t n = 0;
    for (token = strtok_r(str, delim, &saveptr); token != NULL; token = strtok_r(NULL, delim, &saveptr)) {
        tokens[n] = malloc(strlen(token) + 1);
        strcpy(tokens[n], token);
        ++n;
    }
    tokens[n] = NULL;
    return tokens;
}

char** split_path(const char* path_str) {
    return split_str(path_str, "/");
}

void free_tokens(char** tokens) {
    for (char** token = tokens; *token != NULL; ++token) {
        free(*token);
    }
    free(tokens);
}

void read_data(void* buf, ssize_t count, int offset) {
    // sometimes we want to continue just where we left before
    if (offset >= 0) {
        lseek(disk_fd, offset, SEEK_SET);
    }
    for (ssize_t bytes_read = 0, read_now = 0; bytes_read < count; bytes_read += read_now) {
        read_now = read(disk_fd, buf + bytes_read, count - bytes_read);
    }
}

void write_data(const void* buf, ssize_t count, int offset) {
    if (offset >= 0) {
        lseek(disk_fd, offset, SEEK_SET);
    }
    for (ssize_t bytes_written = 0, written_now = 0; bytes_written < count; bytes_written += written_now) {
        written_now = write(disk_fd, buf + bytes_written, count - bytes_written);
    }
}

void read_superblock(struct superblock* sb) {
    read_data(sb, sizeof(struct superblock), 0);
}

void write_superblock(const struct superblock* sb) {
    write_data(sb, sizeof(struct superblock), 0);
}

void read_block_bitmap(void* block_bitmap) {
    read_data(block_bitmap, N_BLOCKS / 8, BLOCK_SIZE);
}

void write_block_bitmap(const void* block_bitmap) {
    write_data(block_bitmap, N_BLOCKS / 8, BLOCK_SIZE);
}

void read_inode_bitmap(void* inode_bitmap) {
    read_data(inode_bitmap, N_INODES / 8, BLOCK_SIZE * 2);
}

void write_inode_bitmap(const void* inode_bitmap) {
    write_data(inode_bitmap, N_INODES / 8, BLOCK_SIZE * 2);
}

int is_correct_block_id(int block_id) {
    return 0 <= block_id && block_id < N_BLOCKS;
}

int is_correct_inode_id(int inode_id) {
    return 0 <= inode_id && inode_id < N_INODES;
}

int is_allocated_inode_id(int inode_id) {
    if (!is_correct_inode_id(inode_id)) {
        return 0;
    }
    char byte;
    read_data(&byte, 1, BLOCK_SIZE * 2 + inode_id / 8);
    return !is_one(byte, inode_id % 8);
}

int get_inode_offset(int inode_id) {
    assert(is_correct_inode_id(inode_id));
    return BLOCK_SIZE * 3 + sizeof(struct inode) * inode_id;
}

void read_inode(struct inode* inode, int inode_id) {
    assert(is_correct_inode_id(inode_id));
    read_data(inode, sizeof(struct inode), get_inode_offset(inode_id));
}

void write_inode(const struct inode* inode, int inode_id) {
    assert(is_correct_inode_id(inode_id));
    write_data(inode, sizeof(struct inode), get_inode_offset(inode_id));
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

void read_block(void* block, int block_id) {
    assert(is_correct_block_id(block_id));
    read_data(block, BLOCK_SIZE, DATA_OFFSET + BLOCK_SIZE * block_id);
}

void write_block(const void* block, int block_id) {
    assert(is_correct_block_id(block_id));
    write_data(block, BLOCK_SIZE, DATA_OFFSET + BLOCK_SIZE * block_id);
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
            set_zero(block_bitmap + i, index); // mark as allocated
            break;
        }
    }
    write_block_bitmap(block_bitmap);

    char buf[BLOCK_SIZE];
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
    read_data(&byte, 1, BLOCK_SIZE + block_id / 8);
    if (is_one(byte, block_id % 8)) {
        // block wasn't allocated
        return -1;
    }
    set_one(&byte, block_id % 8);
    write_data(&byte, 1, BLOCK_SIZE + block_id / 8);

    return 0;
}

void print_bytes(int num) {
    for (int i = 0; i < 8; ++i) {
        printf("%d ", num & 1);
        num >>= 1;
    }
    puts("");
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
    read_data(&byte, 1, BLOCK_SIZE * 2 + inode_id / 8);
    if (is_one(byte, inode_id % 8)) {
        return -1;
    }
    set_one(&byte, inode_id % 8);
    write_data(&byte, 1, BLOCK_SIZE * 2 + inode_id / 8);

    return 0;
}

void init_dir(struct inode* inode, int inode_id, int parent_inode_id) {
    int block_id = allocate_block();
    inode->direct[0] = block_id;
    inode->size      = sizeof(struct entry) * 2; // . and ..

    struct entry entry;
    entry.inode_id = inode_id;
    strcpy(entry.filename, ".");
    write_data(&entry, sizeof(struct entry), DATA_OFFSET + BLOCK_SIZE * block_id);
    entry.inode_id = parent_inode_id;
    strcpy(entry.filename, "..");
    write_data(&entry, sizeof(struct entry), -1);
}

// given inode_id of a directory, find filename in it and return corresponding inode id
int go(int inode_id, const char* filename) {
    if (inode_id == -1) {
        return -1;
    }

    struct inode inode;
    read_inode(&inode, inode_id);
    char block[BLOCK_SIZE];

    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            continue;
        }
        read_block(block, inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (void*)entry < (void*)block + BLOCK_SIZE; ++entry) {
            if (strcmp(entry->filename, filename) == 0) {
                return entry->inode_id;
            }
        }
    }

    return -1;
}

int file_exists_in_dir(int dir_inode_id, const char* filename) {
    return (go(dir_inode_id, filename) != -1);
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

// i should probably have named this function 'resolve', but it's too late
// given an absolute or a relative path, return the corresponding file's inode id
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

int change_dir(const char* path) {
    int dest_inode_id = traverse(path);
    if (is_dir(dest_inode_id)) {
        work_inode_id = dest_inode_id;
        return work_inode_id;
    } else {
        return -1;
    }
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

// either of the last two pointers may be null, if we don't need that information
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
    char block[BLOCK_SIZE];

    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            break;
        }
        read_block(block, inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + BLOCK_SIZE; ++entry) {
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

void remove_file_from_dir(int dir_inode_id, int file_inode_id) {
    struct inode dir_inode;
    read_inode(&dir_inode, dir_inode_id);
    char block[BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            continue;
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + BLOCK_SIZE; ++entry) {
            if (entry->inode_id == file_inode_id) {
                entry->inode_id = -1;
                write_block(block, dir_inode.direct[i]);

                dir_inode.size -= sizeof(struct entry);
                write_inode(&dir_inode, dir_inode_id);

                decrement_ref_count(file_inode_id);
                return;
            }
        }
    }
}

int remove(const char* path) {
    int parent_inode_id;
    char* filename;
    get_parent_and_filename(path, &parent_inode_id, &filename);
    int inode_id = go(parent_inode_id, filename);
    free(filename);

    if (inode_id == ROOT_INODE_ID) {
        puts("anus sebe udali, pyos");
        return -1;
    }
    if (!is_allocated_inode_id(inode_id)) {
        puts("invalid path");
        return -1;
    }
    remove_file_from_dir(parent_inode_id, inode_id);
    return 0;
}

int min(int a, int b) {
    return a < b ? a : b;
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

    char block[BLOCK_SIZE];
    // search for an unoccupied space for the new entry
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            dir_inode.direct[i] = allocate_block();
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (void*)entry < (void*)block + BLOCK_SIZE; ++entry) {
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

int get_free_space_in_file(int inode_id) {
    struct inode inode;
    read_inode(&inode, inode_id);
    return MAX_FILE_SIZE - inode.size;
}

int create_file(const char* path, enum file_type file_type) {
    if (file_exists(path)) {
        printf("%s already exists\n", (file_type == DIRECTORY ? "directory" : "file"));
        return -1;
    }
    int parent_inode_id;
    char* filename;
    get_parent_and_filename(path, &parent_inode_id, &filename);
    if (parent_inode_id == -1 || !is_dir(parent_inode_id)) {
        puts("incorrect path");
        free(filename);
        return -1;
    }
    if (get_free_space_in_file(parent_inode_id) < sizeof(struct entry)) {
        puts("not enough space in directory");
        free(filename);
        return -1;
    }
                                    // a directory requires a block immediately
    if (get_n_free_inodes() == 0 || (file_type == DIRECTORY && get_n_free_blocks() == 0)) {
        puts("not enough space in MiniFS");
        free(filename);
        return -1;
    }

    struct inode inode;
    inode.file_type     = file_type;
    inode.size          = 0;
    inode.ref_count     = 0;
    inode.created       =
    inode.last_accessed =
    inode.last_modified = time(NULL);
    memset(inode.direct, -1, sizeof(inode.direct));
    int new_inode_id = allocate_inode();
    if (file_type == DIRECTORY) {
        init_dir(&inode, new_inode_id, parent_inode_id);
    }
    write_inode(&inode, new_inode_id);
    // the newly-created inode is already on disk,
    // but currently it's isolated from the general hierarchy
    add_file_to_dir(parent_inode_id, new_inode_id, filename);

    free(filename);
    return new_inode_id;
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

int list_entries(const char* path_str) {
    int inode_id;
    if (path_str != NULL) {
        inode_id = traverse(path_str);
    } else {
        inode_id = work_inode_id;
    }

    if (!is_dir(inode_id)) {
        printf("%s: not a directory\n", path_str);
        return -1;
    }

    struct inode inode;
    read_inode(&inode, inode_id);
    char block[BLOCK_SIZE];

    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            continue;
        }
        read_block(block, inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (void*)entry < (void*)block + BLOCK_SIZE; ++entry) {
            if (is_correct_inode_id(entry->inode_id) && strcmp(entry->filename, ".") != 0 && strcmp(entry->filename, "..") != 0) {
                //printf("%d ", entry->inode_id);
                printf("%s\n", entry->filename);
            }
        }
    }

    return 0;
}

void display_help() {
    puts(
        "                     MiniFS commands\n"
        "-----------------------------------------------------------------\n"
        "* help                         display help\n"
        "* exit                         exit from MiniFS\n"
        "* cd path                      change current directory along path\n"
        "* ls [path]                    list files in current directory or by path\n"
        "* cp [options] src dest        make a copy of src at dest\n"
        "                               options: \n"
        "                                --from-local    copy a local file to MiniFS\n"
        "                                --to-local      copy a file from MiniFS to local FS\n"
        "* rm path                      remove file or directory\n"
        "* mv src dest                  move src to dest\n"
        "* mkdir path                   create a directory\n"
        "* touch path                   create a file\n"
        "* cat path                     print contents of a file\n"
        "* pwd                          print path to current working directory\n"
        "-----------------------------------------------------------------"
    );
}

int get_n_blocks_needed(int size) {
    int n_blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE; // round up
                                // need an additional indirection level
    return n_blocks_needed/* + (int)(n_blocks_needed > N_DIRECT_PTRS)*/;
}

void append_to_file(int inode_id, const void* data, int n_bytes) {
    struct inode inode;
    read_inode(&inode, inode_id);
    int ptr = inode.size / BLOCK_SIZE;
    int bytes_written = 0;
    if (inode.size % BLOCK_SIZE != 0) {
        puts("% != 0");
        int write_now = min(n_bytes, BLOCK_SIZE - (inode.size % BLOCK_SIZE));
        write_data(data, write_now, DATA_OFFSET + inode.direct[ptr] * BLOCK_SIZE + (inode.size % BLOCK_SIZE));
        bytes_written += write_now;
        ++ptr;
    }
    for (; bytes_written < n_bytes; ++ptr) {
        inode.direct[ptr] = allocate_block();
        int write_now = min(n_bytes - bytes_written, BLOCK_SIZE);
        write_data(data, write_now, DATA_OFFSET + inode.direct[ptr] * BLOCK_SIZE);
        bytes_written += write_now;

    }
    inode.size += n_bytes;
    write_inode(&inode, inode_id);
}

int copy_from_local(const char* src_path, const char* dest_path) {
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        printf("couldn't open %s\n", src_path);
        return -1;
    }
    struct stat src_stat;
    if (fstat(src_fd, &src_stat) == -1) {
        printf("couldn't obtain metadata for %s\n", src_path);
        goto err;
    }
    if (!S_ISREG(src_stat.st_mode)) {
        printf("%s: not a regular file\n", src_path);
        goto err;
    }
    if (src_stat.st_size > (int64_t)MAX_FILE_SIZE) {
        puts("file too big");
        goto err;
    }
    if (get_n_blocks_needed(src_stat.st_size) > get_n_free_blocks()) {
        puts("not enough free blocks left in MiniFS");
        goto err;
    }

    int inode_id = create_file(dest_path, REGULAR_FILE);
    if (inode_id == -1) {
        puts("couldn't create file");
        goto err;
    }

    char buf[BLOCK_SIZE];
    FILE* src_fp = fdopen(src_fd, "r");
    for (int n_bytes_left = src_stat.st_size; n_bytes_left > 0; n_bytes_left -= BLOCK_SIZE) {
        int n_bytes_cur = (n_bytes_left < BLOCK_SIZE ? n_bytes_left : BLOCK_SIZE);
        fread(buf, 1, n_bytes_cur, src_fp);
        append_to_file(inode_id, buf, n_bytes_cur);
    }

    fclose(src_fp);
    return inode_id;

err:
    close(src_fd);
    return -1;
}

int copy_to_local(const char* src_path, const char* dest_path) {
    int src_inode_id = traverse(src_path);
    if (src_inode_id == -1) {
        printf("%s: invalid path\n", src_path);
        return -1;
    }
    struct inode src_inode;
    read_inode(&src_inode, src_inode_id);
    if (src_inode.file_type != REGULAR_FILE) {
        printf("%s: not a regular file\n", src_path);
        return -1;
    }

    FILE* dest_fp = fopen(dest_path, "w");
    if (dest_fp == NULL) {
        printf("%s: couldn't open or create\n", dest_path);
        return -1;
    }

    char buf[BLOCK_SIZE];
    for (int n_bytes_left = src_inode.size, ptr = 0; n_bytes_left > 0; n_bytes_left -= BLOCK_SIZE, ++ptr) {
        read_block(buf, src_inode.direct[ptr]);
        int n_bytes_cur = (n_bytes_left < BLOCK_SIZE ? n_bytes_left : BLOCK_SIZE);
        fwrite(buf, 1, n_bytes_cur, dest_fp);
    }

    fclose(dest_fp);
    return 0;
}

int copy(const char* src_path, const char* dest_path) {
    int src_inode_id = traverse(src_path);
    if (src_inode_id == -1) {
        printf("%s: invalid path\n", src_path);
        return -1;
    }
    struct inode src_inode;
    read_inode(&src_inode, src_inode_id);
    if (src_inode.file_type != REGULAR_FILE) {
        printf("%s: not a regular file\n", src_path);
        return -1;
    }
    if (get_n_blocks_needed(src_inode.size) > get_n_free_blocks()) {
        puts("not enough free blocks in MiniFS");
        return -1;
    }

    int new_inode_id = create_file(dest_path, REGULAR_FILE);
    if (new_inode_id == -1) {
        puts("couldn't create file");
        return -1;
    }

    char buf[BLOCK_SIZE];
    for (int n_bytes_left = src_inode.size, ptr = 0; n_bytes_left > 0; n_bytes_left -= BLOCK_SIZE, ++ptr) {
        read_block(buf, src_inode.direct[ptr]);
        int n_bytes_cur = (n_bytes_left < BLOCK_SIZE ? n_bytes_left : BLOCK_SIZE);
        append_to_file(new_inode_id, buf, n_bytes_cur);
    }

    return new_inode_id;
}

void rename_file_in_dir(int dir_inode_id, const char* filename, const char* new_filename) {
    struct inode dir_inode;
    read_inode(&dir_inode, dir_inode_id);
    char block[BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            continue;
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + BLOCK_SIZE; ++entry) {
            if (strcmp(entry->filename, filename) == 0) {
                strcpy(entry->filename, new_filename);
                write_block(block, dir_inode.direct[i]);
                return;
            }
        }
    }
}

int move(const char* src_path, const char* dest_path) {
    int src_inode_id = traverse(src_path);
    if (src_inode_id == -1 || src_inode_id == ROOT_INODE_ID) {
        return -1;
    }
    if (file_exists(dest_path)) {
        printf("%s already exists\n", dest_path);
        return -1;
    }

    int src_parent_inode_id;
    char* src_filename;
    get_parent_and_filename(src_path, &src_parent_inode_id, &src_filename);

    int dest_parent_inode_id;
    char* dest_filename;
    get_parent_and_filename(dest_path, &dest_parent_inode_id, &dest_filename);

    if (src_parent_inode_id == dest_parent_inode_id) {
        rename_file_in_dir(src_parent_inode_id, src_filename, dest_filename);
        free(src_filename);
        free(dest_filename);
        return 0;
    }
    if (dest_parent_inode_id == -1) {
        printf("%s: invalid path\n", dest_path);
        free(src_filename);
        free(dest_filename);
        return -1;
    }
    if (get_free_space_in_file(dest_parent_inode_id) < sizeof(struct entry)) {
        puts("not enough space in destination directory");
        free(src_filename);
        free(dest_filename);
        return -1;
    }
    add_file_to_dir(dest_parent_inode_id, src_inode_id, dest_filename);
    remove_file_from_dir(src_parent_inode_id, src_inode_id);
    free(src_filename);
    free(dest_filename);
    return 0;
}

void check_error(int ret_code) {
    if (ret_code == -1) {
        puts("error");
    }
}

void get_filename_by_inode(int dir_inode_id, int inode_id, char* filename) {
    struct inode dir_inode;
    read_inode(&dir_inode, dir_inode_id);
    char block[BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(dir_inode.direct[i])) {
            continue;
        }
        read_block(block, dir_inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (char*)entry < block + BLOCK_SIZE; ++entry) {
            if (entry->inode_id == inode_id) {
                strcpy(filename, entry->filename);
                return;
            }
        }
    }
}

void reverse_str(char* str) {
    int len = strlen(str);
    for (int l = 0, r = len - 1; l < r; ++l, --r) {
        char c = str[l];
        str[l] = str[r];
        str[r] = c;
    }
}

void print_work_path() {
    if (work_inode_id == ROOT_INODE_ID) {
        printf("/");
        return;
    }
    char* buf = calloc(MAX_PATH_LEN, 1);
    int cur_inode_id = work_inode_id;
    // climb up the tree
    while (cur_inode_id != ROOT_INODE_ID) {
        int parent_inode_id = go(cur_inode_id, "..");
        char filename[FILENAME_LEN + 1];
        get_filename_by_inode(parent_inode_id, cur_inode_id, filename);
        reverse_str(filename);
        strcat(filename, "/");
        strcat(buf, filename);
        cur_inode_id = parent_inode_id;
    }
    reverse_str(buf);
    printf("%s", buf);
    free(buf);
}

int print_contents(const char* path) {
    int inode_id = traverse(path);
    if (inode_id == -1) {
        puts("invalid path");
        return -1;
    }
    if (!is_regular_file(inode_id)) {
        printf("%s is not a regular file\n", path);
        return -1;
    }
    struct inode inode;
    read_inode(&inode, inode_id);
    char block[BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            break;
        }
        read_block(block, inode.direct[i]);
        int size = min(inode.size - BLOCK_SIZE * i, BLOCK_SIZE);
        fwrite(block, 1, size, stdout);
    }
    puts("");
    return 0;
}

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
            check_error(list_entries(tokens[1]));
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
