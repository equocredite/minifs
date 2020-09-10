#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "interface.h"
#include "inode.h"
#include "block.h"
#include "str_util.h"
#include "net_io.h"

int change_dir(const char* path) {
    int dest_inode_id = traverse(path);
    if (is_dir(dest_inode_id)) {
        send_success();
        work_inode_id = dest_inode_id;

        disable_succfail = 1;
        print_work_path();
        disable_succfail = 0;

        return work_inode_id;
    } else {
        send_failure("invalid path\n");
        return -1;
    }
}

int remove(const char* path) {
    int parent_inode_id;
    char* filename;
    get_parent_and_filename(path, &parent_inode_id, &filename);
    int inode_id = go(parent_inode_id, filename);
    free(filename);

    if (inode_id == ROOT_INODE_ID) {
        send_failure("anus sebe udali, pyos\n");
        return -1;
    }
    if (!is_allocated_inode_id(inode_id)) {
        send_failure("invalid path\n");
        return -1;
    }
    if (remove_file_from_dir(parent_inode_id, inode_id) == -1) {
        send_failure("no such file\n");
    }
    send_success();
    return 0;
}

int create_file(const char* path, enum file_type file_type) {
    if (file_exists(path)) {
        send_failure("file already exists\n");
        return -1;
    }
    int parent_inode_id;
    char* filename;
    get_parent_and_filename(path, &parent_inode_id, &filename);
    if (parent_inode_id == -1 || !is_dir(parent_inode_id)) {
        send_failure("incorrect path\n");
        free(filename);
        return -1;
    }
    if (get_free_space_in_file(parent_inode_id) < sizeof(struct entry)) {
        send_failure("not enough space in directory\n");
        free(filename);
        return -1;
    }
                                    // a directory requires a block immediately
    if (get_n_free_inodes() == 0 || (file_type == DIRECTORY && get_n_free_blocks() == 0)) {
        send_failure("not enough space in MiniFS\n");
        free(filename);
        return -1;
    }
    send_success();

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

int list_entries(const char* path, int all) {
    int inode_id = (path == NULL ? work_inode_id : traverse(path));

    if (!is_dir(inode_id)) {
        send_failure("not a directory\n");
        return -1;
    }
    send_success();

    struct inode inode;
    read_inode(&inode, inode_id);
    char block[BLOCK_SIZE];

    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            continue;
        }
        read_block(block, inode.direct[i]);
        for (struct entry* entry = (struct entry*)block; (void*)entry < (void*)block + BLOCK_SIZE; ++entry) {
            if (is_correct_inode_id(entry->inode_id)) {
                if (!all && entry->filename[0] == '.') {
                    continue;
                }
                send_msg(entry->filename);
                send_msg("\n");
            }
        }
    }

    return 0;
}

void display_help() {
    send_success();
    send_msg(
        "                     MiniFS commands\n"
        "-----------------------------------------------------------------\n"
        "* help                         display help\n"
        "* exit                         exit from MiniFS\n"
        "* cd path                      change current directory along path\n"
        "* ls [path]                    list files in current directory or by path\n"
        "                               options: \n"
        "                                 --all    don't omit files starting with '.'\n"
        "* cp [options] src dest        make a copy of src at dest\n"
        "                               options: \n"
        "                                 --from-local    copy a local file to MiniFS\n"
        "                                 --to-local      copy a file from MiniFS to local FS\n"
        "* rm path                      remove file or directory\n"
        "* mv src dest                  move src to dest\n"
        "* mkdir path                   create a directory\n"
        "* touch path                   create a file\n"
        "* cat path                     print contents of a file\n"
        "* pwd                          print path to current working directory\n"
        "-----------------------------------------------------------------\n"
    );
}

int copy_from_local(const char* dest_path) {
    send_success(); // sync
    size_t size;
    recv_nbytes(&size, sizeof(size));
    if (size > (int64_t)MAX_FILE_SIZE) {
        send_failure("file too big\n");
        return -1;
    }
    if (get_n_blocks_needed((int)size) > get_n_free_blocks()) {
        send_failure("not enough free blocks left");
        return -1;
    }

    disable_succfail = 1;
    int inode_id = create_file(dest_path, REGULAR_FILE);
    disable_succfail = 0;

    if (inode_id == -1) {
        send_failure("couldn't create file");
        return -1;
    }
    send_success();

    char buf[BLOCK_SIZE];
    for (int n_bytes_left = size; n_bytes_left > 0; n_bytes_left -= BLOCK_SIZE) {
        int n_bytes_cur = (n_bytes_left < BLOCK_SIZE ? n_bytes_left : BLOCK_SIZE);
        recv_nbytes(buf, n_bytes_cur);
        append_to_file(inode_id, buf, n_bytes_cur);
    }
    return inode_id;
}

int copy_to_local(const char* src_path) {
    int src_inode_id = traverse(src_path);
    if (src_inode_id == -1) {
        send_failure("invalid path\n");
        return -1;
    }
    struct inode src_inode;
    read_inode(&src_inode, src_inode_id);
    if (src_inode.file_type != REGULAR_FILE) {
        send_failure("not a regular file\n");
        return -1;
    }
    send_success();
    char buf[BLOCK_SIZE];
    for (int n_bytes_left = src_inode.size, ptr = 0; n_bytes_left > 0; n_bytes_left -= BLOCK_SIZE, ++ptr) {
        read_block(buf, src_inode.direct[ptr]);
        int n_bytes_cur = (n_bytes_left < BLOCK_SIZE ? n_bytes_left : BLOCK_SIZE);
        send_nbytes(buf, n_bytes_cur);
    }
    return 0;
}

int copy(const char* src_path, const char* dest_path) {
    int src_inode_id = traverse(src_path);
    if (src_inode_id == -1) {
        send_failure("invalid_path\n");
        return -1;
    }
    struct inode src_inode;
    read_inode(&src_inode, src_inode_id);
    if (src_inode.file_type != REGULAR_FILE) {
        send_failure("not a regular file\n");
        return -1;
    }
    if (get_n_blocks_needed(src_inode.size) > get_n_free_blocks()) {
        send_failure("not enough free blocks in MiniFS\n");
        return -1;
    }

    disable_succfail = 1;
    int new_inode_id = create_file(dest_path, REGULAR_FILE);
    disable_succfail = 0;

    if (new_inode_id == -1) {
        send_failure("couldn't create file\n");
        return -1;
    }
    send_success();

    char buf[BLOCK_SIZE];
    for (int n_bytes_left = src_inode.size, ptr = 0; n_bytes_left > 0; n_bytes_left -= BLOCK_SIZE, ++ptr) {
        read_block(buf, src_inode.direct[ptr]);
        int n_bytes_cur = (n_bytes_left < BLOCK_SIZE ? n_bytes_left : BLOCK_SIZE);
        append_to_file(new_inode_id, buf, n_bytes_cur);
    }
    return new_inode_id;
}

int move(const char* src_path, const char* dest_path) {
    int src_inode_id = traverse(src_path);
    if (src_inode_id == -1 || src_inode_id == ROOT_INODE_ID) {
        send_failure("invalid source path\n");
        return -1;
    }
    if (file_exists(dest_path)) {
        send_failure("file at destination path already exists\n");
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
        send_success();
        return 0;
    }
    if (dest_parent_inode_id == -1) {
        send_failure("invalid path\n");
        free(src_filename);
        free(dest_filename);
        return -1;
    }
    if (get_free_space_in_file(dest_parent_inode_id) < sizeof(struct entry)) {
        send_failure("not enough space in destination directory\n");
        free(src_filename);
        free(dest_filename);
        return -1;
    }
    send_success();
    add_file_to_dir(dest_parent_inode_id, src_inode_id, dest_filename);
    remove_file_from_dir(src_parent_inode_id, src_inode_id);
    if (is_dir(src_inode_id)) {
        remove_file_from_dir(src_inode_id, src_parent_inode_id);
        add_file_to_dir(src_inode_id, dest_parent_inode_id, "..");
    }
    free(src_filename);
    free(dest_filename);
    return 0;
}

void print_work_path() {
    if (work_inode_id == ROOT_INODE_ID) {
        send_success();
        send_msg("/\n");
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
    send_success();
    send_msg(buf);
    send_msg("\n");
    free(buf);
}

int print_contents(const char* path) {
    int inode_id = traverse(path);
    if (inode_id == -1) {
        send_failure("invalid path\n");
        return -1;
    }
    if (!is_regular_file(inode_id)) {
        send_failure("not a regular file\n");
        return -1;
    }
    send_success();

    struct inode inode;
    read_inode(&inode, inode_id);
    char block[BLOCK_SIZE];
    for (int i = 0; i < N_DIRECT_PTRS; ++i) {
        if (!is_correct_block_id(inode.direct[i])) {
            break;
        }
        read_block(block, inode.direct[i]);
        int size = min(inode.size - BLOCK_SIZE * i, BLOCK_SIZE);
        send_nbytes(block, size);
    }
    return 0;
}
