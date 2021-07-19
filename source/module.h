#ifndef WASMC_MODULE_H
#define WASMC_MODULE_H

#define WA_MAGIC 0x6d736100
#define WA_VERSION 0x01

#include <stdlib.h>

// Wasm 内存格式对应的结构体
typedef struct Module {
    const uint8_t *bytes;// 用于存储 Wasm 二进制模块的内容
    uint32_t byte_count; // Wasm 二进制模块的字节数
} Module;

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module
struct Module *load_module(const uint8_t *bytes, uint32_t byte_count);

#endif
