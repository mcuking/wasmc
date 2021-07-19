#ifndef WASMC_MODULE_H
#define WASMC_MODULE_H

#define WA_MAGIC 0x6d736100
#define WA_VERSION 0x01

#include <stdlib.h>

// 段 ID 的枚举
typedef enum {
    CustomID,// 自定义段 ID
    TypeID,  //类型段 ID
    ImportID,// 导入段 ID
    FuncID,  // 函数段 ID
    TableID, // 表段 ID
    MemID,   // 内存段 ID
    GlobalID,// 全局段 ID
    ExportID,// 导出段 ID
    StartID, // 起始段 ID
    ElemID,  // 元素段 ID
    CodeID,  // 代码段 ID
    DataID   //数据段 ID
} SecID;

// Wasm 内存格式对应的结构体
typedef struct Module {
    const uint8_t *bytes;// 用于存储 Wasm 二进制模块的内容
    uint32_t byte_count; // Wasm 二进制模块的字节数
} Module;

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module
struct Module *load_module(const uint8_t *bytes, uint32_t byte_count);

#endif
