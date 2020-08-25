#ifndef STR_UTIL_H
#define STR_UTIL_H

char** split_str(const char* const_str, const char* delim);

char** split_path(const char* path_str);

void free_tokens(char** tokens);

void reverse_str(char* str);

#endif // STR_UTIL_H
