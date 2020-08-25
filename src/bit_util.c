#include <assert.h>

#include "bit_util.h"

int first_bit(char x) {
    for (int i = 0; i < 8; ++i) {
        if (x & 1) {
            return i;
        }
        x >>= 1;
    }
    return -1;
}

int is_one(char x, int bit) {
    assert(0 <= bit && bit < 8);
    return x & (1 << bit);
}

void set_one(char* x, int bit) {
    assert(!is_one(*x, bit));
    *x |= (1 << bit);
}

void set_zero(char* x, int bit) {
    assert(is_one(*x, bit));
    *x ^= (1 << bit);
}
