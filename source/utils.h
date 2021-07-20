#ifndef WASMC_UTILS_H
#define WASMC_UTILS_H

#include "module.h"
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

// 解码针对无符号整数的 LEB128 编码
uint64_t read_LEB_unsigned(const uint8_t *bytes, uint32_t *pos, uint32_t maxbits);

// 解码针对有符号整数的 LEB128 编码
uint64_t read_LEB_signed(const uint8_t *bytes, uint32_t *pos, uint32_t maxbits);

// 申请内存
void *acalloc(size_t nmemb, size_t size, char *name);

// 在原有内存基础上重新申请内存
void *arecalloc(void *ptr, size_t old_nmemb, size_t nmemb, size_t size, char *name);

// 基于函数类型计算唯一的掩码值
uint64_t get_type_mask(Type *type);

#endif
