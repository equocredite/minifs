#include <stdlib.h>
#include <string.h>

#include "str_util.h"

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

void reverse_str(char* str) {
    int len = strlen(str);
    for (int l = 0, r = len - 1; l < r; ++l, --r) {
        char c = str[l];
        str[l] = str[r];
        str[r] = c;
    }
}
