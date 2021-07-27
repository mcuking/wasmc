#ifndef WASMC_UTILS_H
#define WASMC_UTILS_H

extern char exception[];

#include "module.h"
#include <stdbool.h>
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

// 从字节数组中读取字符串，其中字节数组的开头 4 个字节用于表示字符串的长度
// 注：如果参数 result_len 不为 NULL，则会被赋值为字符串的长度
char *read_string(const uint8_t *bytes, uint32_t *pos, uint32_t *result_len);

// 申请内存
void *acalloc(size_t nmemb, size_t size, char *name);

// 在原有内存基础上重新申请内存
void *arecalloc(void *ptr, size_t old_nmemb, size_t nmemb, size_t size, char *name);

// 查找动态库中的 symbol
// 如果解析成功则返回 true
// 如果解析失败则返回 false 并设置 err
bool resolve_sym(char *filename, char *symbol, void **val, char **err);

// 基于函数签名计算唯一的掩码值
uint64_t get_type_mask(Type *type);

// 根据表示该控制块的类型的值（占一个字节），返回控制块的类型（或签名），即控制块的返回值的数量和类型
// 0x7f 表示有一个 i32 类型返回值、0x7e 表示有一个 i64 类型返回值、0x7d 表示有一个 f32 类型返回值、0x7c 表示有一个 f64 类型返回值、0x40 表示没有返回值
// 注：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值
Type *get_block_type(uint8_t value_type);

#endif
