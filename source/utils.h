#ifndef WASMC_UTILS_H
#define WASMC_UTILS_H

#include <stdlib.h>

// 报错
#define FATAL(...)                                             \
    {                                                          \
        fprintf(stderr, "Error(%s:%d): ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);                          \
        exit(1);                                               \
    }

// 断言
#define ASSERT(exp, ...)                                                    \
    {                                                                       \
        if (!(exp)) {                                                       \
            fprintf(stderr, "Assert Failed (%s:%d): ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                                   \
            exit(1);                                                        \
        }                                                                   \
    }

// 申请内存
void *acalloc(size_t nmemb, size_t size, char *name);

#endif
