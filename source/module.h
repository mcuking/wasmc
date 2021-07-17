#ifndef WASMC_MODULE_H
#define WASMC_MODULE_H

#define WA_MAGIC 0x6d736100
#define WA_VERSION 0x01

typedef struct Module {
    const uint8_t *bytes;
    uint32_t byte_count;
} Module;

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module
struct Module *load_module(const uint8_t *bytes, uint32_t byte_count);

#endif
