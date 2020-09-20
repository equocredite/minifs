#include "disk_io.h"

void read_data(void* buf, ssize_t count, int offset) {
    for (ssize_t bytes_read = 0, read_now = 0; bytes_read < count; bytes_read += read_now) {
        read_now = pread(disk_fd, buf + bytes_read, count - bytes_read, offset + bytes_read);
    }
}

void write_data(const void* buf, ssize_t count, int offset) {
    for (ssize_t bytes_written = 0, written_now = 0; bytes_written < count; bytes_written += written_now) {
        written_now = pwrite(disk_fd, buf + bytes_written, count - bytes_written, offset + bytes_written);
    }
}

void read_block_bitmap(void* block_bitmap) {
    read_data(block_bitmap, N_BLOCKS / 8, MINIFS_BLOCK_SIZE);
}

void write_block_bitmap(const void* block_bitmap) {
    write_data(block_bitmap, N_BLOCKS / 8, MINIFS_BLOCK_SIZE);
}

void read_inode_bitmap(void* inode_bitmap) {
    read_data(inode_bitmap, N_INODES / 8, MINIFS_BLOCK_SIZE * 2);
}

void write_inode_bitmap(const void* inode_bitmap) {
    write_data(inode_bitmap, N_INODES / 8, MINIFS_BLOCK_SIZE * 2);
}
