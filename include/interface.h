#ifndef INTERFACE_H
#define INTERFACE_H

#include "globals.h"

int change_dir(const char* path);

int remove(const char* path);

int create_file(const char* path, enum file_type file_type);

int list_entries(const char* path, int all);

void display_help();

int copy_from_local(const char* dest_path);

int copy_to_local(const char* src_path);

int copy(const char* src_path, const char* dest_path);

int move(const char* src_path, const char* dest_path);

void print_work_path();

int print_contents(const char* path);

#endif // INTERFACE_H
