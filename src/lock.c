#include <pthread.h>

#include "lock.h"
#include "globals.h"

pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

void write_lock() {
    if (!nested) {
        pthread_rwlock_wrlock(&lock);
    }
}

void read_lock() {
    if (!nested) {
        pthread_rwlock_rdlock(&lock);
    }
}

void unlock() {
    if (!nested) {
        pthread_rwlock_unlock(&lock);
    }
}
