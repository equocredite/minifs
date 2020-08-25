#ifndef DISK_IO_H
#define DISK_IO_H

#include <sys/types.h>
#include <unistd.h>

#include "globals.h"

void read_data(void* buf, ssize_t count, int offset);

void write_data(const void* buf, ssize_t count, int offset);

void read_block_bitmap(void* block_bitmap);

void write_block_bitmap(const void* block_bitmap);

void read_inode_bitmap(void* inode_bitmap);

void write_inode_bitmap(const void* inode_bitmap);

#endif // DISK_IO_H
