#include <stdlib.h>
#include <stdio.h>
#include "utils.h"

// 申请内存
void *acalloc(size_t nmemb, size_t size, char *name) {
    void *res = calloc(nmemb, size);
    if (res == NULL) {
        FATAL("Could not allocate %lu bytes for %s", nmemb * size, name)
    }
    return res;
}
