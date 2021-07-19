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

// 函数类型（或称函数签名）对应结构体
typedef struct Type {
    uint32_t param_count; // 函数的参数数量
    uint32_t *params;     // 函数的参数类型集合
    uint32_t result_count;// 函数的返回值数量
    uint32_t *results;    // 函数的返回值类型集合
    uint64_t mask;        // 基于函数类型计算的唯一掩码值
} Type;

// 控制块（包含函数）对应的结构体
typedef struct Block {
    uint8_t block_type;  // 控制块类型，0x00: function, 0x01: init_exp, 0x02: block, 0x03: loop, 0x04: if
    uint32_t fidx;       // 函数在所有函数中的索引（仅针对控制块类型为函数的情况）
    Type *type;          // 函数类型，注：用来描述所有类型的控制块的入参和出参，不仅限于函数类型的控制块
    uint32_t local_count;// 局部变量数量（仅针对控制块类型为函数的情况）
    uint32_t *locals;    // 用于存储局部变量的值（仅针对控制块类型为函数的情况）
} Block;

// Wasm 内存格式对应的结构体
typedef struct Module {
    const uint8_t *bytes;// 用于存储 Wasm 二进制模块的内容
    uint32_t byte_count; // Wasm 二进制模块的字节数

    Type *types;        // 用于存储模块中所有函数类型
    uint32_t type_count;// 模块中所有函数类型的数量

    uint32_t import_func_count;// 导入函数的数量
    uint32_t function_count;   // 所有函数的数量（包括导入函数）
    Block *functions;          // 用于存储模块中所有函数（包括导入函数和模块内定义函数）
} Module;

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module
struct Module *load_module(const uint8_t *bytes, uint32_t byte_count);

#endif
