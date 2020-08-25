#include "disk_io.h"

void read_data(void* buf, ssize_t count, int offset) {
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
