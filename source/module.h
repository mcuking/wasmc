#ifndef WASMC_MODULE_H
#define WASMC_MODULE_H

#include <stdlib.h>

#define WA_MAGIC 0x6d736100// 魔数（magic number）
#define WA_VERSION 0x01    // Wasm 标准的版本号

#define PAGE_SIZE 0x10000     // 每页内存的大小 65536，即 64 * 1024，也就是 64KB
#define BLOCKSTACK_SIZE 0x1000// 控制块栈的容量 4096

#define I32 0x7f    // -0x01
#define I64 0x7e    // -0x02
#define F32 0x7d    // -0x03
#define F64 0x7c    // -0x04
#define ANYFUNC 0x70// -0x10
#define BLOCK 0x40  // -0x40

// 导出项/导入项类型
#define KIND_FUNCTION 0
#define KIND_TABLE 1
#define KIND_MEMORY 2
#define KIND_GLOBAL 3

// 段 ID 的枚举
typedef enum {
    CustomID,// 自定义段 ID
    TypeID,  // 类型段 ID
    ImportID,// 导入段 ID
    FuncID,  // 函数段 ID
    TableID, // 表段 ID
    MemID,   // 内存段 ID
    GlobalID,// 全局段 ID
    ExportID,// 导出段 ID
    StartID, // 起始段 ID
    ElemID,  // 元素段 ID
    CodeID,  // 代码段 ID
    DataID   // 数据段 ID
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
    uint8_t block_type;// 控制块类型，0x00: function, 0x01: init_exp, 0x02: block, 0x03: loop, 0x04: if
    uint32_t fidx;     // 函数在所有函数中的索引（仅针对控制块类型为函数的情况）
    Type *type;        // 函数类型，注：用来描述所有类型的控制块的入参和出参，不仅限于函数类型的控制块

    uint32_t local_count;// 局部变量数量（仅针对控制块类型为函数的情况）
    uint32_t *locals;    // 用于存储局部变量的值（仅针对控制块类型为函数的情况）

    uint32_t start_addr;// 控制块中字节码部分的【起始地址】
    uint32_t end_addr;  // 控制块中字节码部分的【结束地址】
    uint32_t else_addr; // 控制块中字节码部分的【else 地址】(仅针对控制块类型为 if 的情况)
    uint32_t br_addr;   // 控制块中字节码部分的【跳转地址】

    char *import_module;// 导入函数的导入模块名（仅针对从外部模块导入的函数）
    char *import_field; // 导入函数的导入成员名（仅针对从外部模块导入的函数）
    void *(*func_ptr)();// 导入函数的实际值（仅针对从外部模块导入的函数）
} Block;

// 表对应的结构体
typedef struct Table {
    uint8_t elem_type;// 表中元素的类型（必须为函数引用，编码为 0x70）
    uint32_t min_size;// 表的元素数量限制下限
    uint32_t max_size;// 表的元素数量限制上限
    uint32_t cur_size;// 表的当前元素数量
    uint32_t *entries;// 用于存储表中的元素
} Table;

// 内存对应的结构体
typedef struct Memory {
    uint32_t min_size;// 最小页数
    uint32_t max_size;// 最大页数
    uint32_t cur_size;// 当前页数
    uint8_t *bytes;   // 用于存储数据
} Memory;

// 导出项对应结构体
typedef struct Export {
    char *export_name;     // 导出项成员名
    uint32_t external_kind;// 导出项类型（类型可以是函数/表/内存/全局变量）
    void *value;           // 用于存储导出项的值
} Export;

// 全局变量值/操作数栈的值对应的结构体
typedef struct StackValue {
    uint8_t value_type;// 值类型
    union {
        uint32_t uint32;
        int32_t int32;
        uint64_t uint64;
        int64_t int64;
        float f32;
        double f64;
    } value;// 值
} StackValue;

// Wasm 内存格式对应的结构体
typedef struct Module {
    const uint8_t *bytes;// 用于存储 Wasm 二进制模块的内容
    uint32_t byte_count; // Wasm 二进制模块的字节数

    Type *types;        // 用于存储模块中所有函数类型
    uint32_t type_count;// 模块中所有函数类型的数量

    uint32_t import_func_count;// 导入函数的数量
    uint32_t function_count;   // 所有函数的数量（包括导入函数）
    Block *functions;          // 用于存储模块中所有函数（包括导入函数和模块内定义函数）
    Block **block_lookup;      // 模块中所有 Block 的 map，其中 key 为为对应操作码 Block_/Loop/If 的地址

    Table table;// 表

    Memory memory;// 内存

    StackValue *globals;  // 用于存储全局变量的相关数据（值以及值类型等）
    uint32_t global_count;// 全局变量的数量

    Export *exports;      // 用于存储导出项的相关数据（导出项的值、成员名以及类型等）
    uint32_t export_count;// 导出项数量

    uint32_t start_function;// 起始函数在本地模块所有函数中索引，而起始函数是在【模块完成初始化后】，【被导出函数可调用之前】自动被调用的函数
} Module;

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module
struct Module *load_module(const uint8_t *bytes, uint32_t byte_count);

#endif
