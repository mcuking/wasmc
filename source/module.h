#ifndef WASMC_MODULE_H
#define WASMC_MODULE_H

#include <stdint.h>
#include <stdlib.h>

#define WA_MAGIC 0x6d736100// 魔数（magic number）
#define WA_VERSION 0x01    // Wasm 标准的版本号

#define PAGE_SIZE 0x10000     // 每页内存的大小 65536，即 64 * 1024，也就是 64KB
#define STACK_SIZE 0x10000    // 操作数栈的容量 65536，即 64 * 1024，也就是 64KB
#define CALLSTACK_SIZE 0x1000 // 调用栈的容量 4096，即 4 * 1024，也就是 4KB
#define BLOCKSTACK_SIZE 0x1000// 控制块栈的容量 4096，即 4 * 1024，也就是 4KB
#define BR_TABLE_SIZE 0x10000 // 跳转指令索引表大小 65536，即 64 * 1024，也就是 64KB

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

// 段 ID 枚举
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

// 控制块（包含函数）签名结构体
// 注：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，非函数类型的控制块不能有参数，且最多只能有一个返回值
typedef struct Type {
    uint32_t param_count; // 参数数量
    uint32_t *params;     // 参数类型集合
    uint32_t result_count;// 返回值数量
    uint32_t *results;    // 返回值类型集合
    uint64_t mask;        // 基于控制块（包含函数）签名计算的唯一掩码值
} Type;

// 控制块（包含函数）结构体
typedef struct Block {
    uint8_t block_type;// 控制块类型，包含 5 种，分别是 0x00: function, 0x01: init_exp, 0x02: block, 0x03: loop, 0x04: if
    Type *type;        // 控制块签名，即控制块的返回值的数量和类型
    uint32_t fidx;     // 函数在所有函数中的索引（仅针对控制块类型为函数的情况）

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

// 表结构体
typedef struct Table {
    uint8_t elem_type;// 表中元素的类型（必须为函数引用，编码为 0x70）
    uint32_t min_size;// 表的元素数量限制下限
    uint32_t max_size;// 表的元素数量限制上限
    uint32_t cur_size;// 表的当前元素数量
    uint32_t *entries;// 用于存储表中的元素
} Table;

// 内存结构体
typedef struct Memory {
    uint32_t min_size;// 最小页数
    uint32_t max_size;// 最大页数
    uint32_t cur_size;// 当前页数
    uint8_t *bytes;   // 用于存储数据
} Memory;

// 导出项结构体
typedef struct Export {
    char *export_name;     // 导出项成员名
    uint32_t external_kind;// 导出项类型（类型可以是函数/表/内存/全局变量）
    void *value;           // 用于存储导出项的值
} Export;

// 全局变量值/操作数栈的值结构体
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

/*
 * 栈式虚拟机的背景知识：
 * 调用栈--callstack
 * 栈帧--stack frame
 * 操作数栈--operand stack
 * 栈指针--stack pointer
 * 帧指针--frame pointer
 * 控制块（包含函数）返回地址--return address
 * 调用栈是由一个个独立的栈帧组成，每次控制块（包含函数）的调用，都会向调用栈压入一个栈帧
 * 每次控制块（包含函数）的执行结束，都会从调用栈弹出对应栈帧并销毁
 * 一连串的控制块（包含函数）调用，就是不停创建和销毁栈帧的过程。但在任一时刻，只有位于调用栈顶的栈帧是活跃的，也就是所谓的当前栈帧
 * 每个栈帧包含以下内容：
 * 1. 栈帧对应的控制块（包含函数）结构体
 * 2. 操作数栈，用于存储参数、局部变量、操作数，所有的栈帧共享同一个完整的操作数栈，每个栈帧会占用这个操作数栈中的某一部分，
 * 所以要两个指针来划定该栈帧在操作数栈的范围，其中 stack pointer 栈指针指向了该栈帧的操作数栈顶，frame pointer 帧指针指向该栈帧的操作数栈底
 * 3. 控制块（包含函数）返回地址，存储该栈帧调用指令的下一条指令的地址，当该栈帧从调用栈弹出时，会返回到该栈帧调用指令的下一条指令继续执行，
 * 换句话说就是当前栈帧对应的控制块（包含函数）执行完退出后，返回到调用该控制块的地方继续执行后面的指令
 *
 * 注：目前这个解释器定义的栈帧中比没有类似 JVM 虚拟机栈帧中的局部变量表，而是将参数、局部变量和操作数都放在了操作数栈上，主要目的有两个：
 * 1. 实现简单，不需要额外定义局部变量表，可以很大程度简化代码
 * 2. 让参数传递变成无操作 NOP，可以让两个栈帧的操作数栈有一部分数据是重叠的，这部分数据就是参数，这样自然就起到了参数传递在不同控制块（包含函数）之间的传递
 * */

// 栈帧结构体
typedef struct Frame {
    Block *block;// 栈帧对应的控制块（包含函数）结构体

    // 下面三个属性是在该栈帧被压入调用栈顶时，保存的当时的运行时的状态，
    // 目的是为了在该栈帧关联的控制块执行完成，该栈帧弹出时，恢复压栈之前的运行时状态
    int sp;     // stack pointer 用于保存该栈帧被压入操作数栈顶前的【操作数栈顶指针】的值
    int fp;     // frame pointer 用于保存该栈帧被压入操作数栈顶前的【当前栈帧的操作数栈底指针】的值
    uint32_t ra;// return address 用于保存【函数返回地址】，即【该栈帧调用指令的下一条指令的地址】，
                // 也就是该栈帧被压入操作数栈顶前的 m->pc 的值
                // 当该栈帧从调用栈弹出时，会返回到该栈帧调用指令的下一条指令继续执行，
                // 换句话说就是当前栈帧对应的函数执行完后，返回到调用该函数的地方继续执行后面的指令
                // 注：该属性均针对类型为函数的控制块（只有函数执行完才会返回），其他类型的控制块没有该属性
} Frame;

// Wasm 内存格式结构体
typedef struct Module {
    const uint8_t *bytes;// 用于存储 Wasm 二进制模块的内容
    uint32_t byte_count; // Wasm 二进制模块的字节数

    Type *types;        // 用于存储模块中所有函数签名
    uint32_t type_count;// 模块中所有函数签名的数量

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

    // 下面属性用于记录运行时（即栈式虚拟机执行指令流的过程）状态，相关背景知识请查看上面栈帧结构体的注释
    uint32_t pc;                     // program counter 程序计数器，记录下一条即将执行的指令的地址
    int sp;                          // operand stack pointer 操作数栈顶指针，指向完整的操作数栈顶（注：所有栈帧共享一个完整的操作数栈，分别占用其中的某一部分）
    int fp;                          // current frame pointer into stack 当前栈帧的帧指针，指向当前栈帧的操作数栈底
    StackValue stack[STACK_SIZE];    // operand stack 操作数栈，用于存储参数、局部变量、操作数
    int csp;                         // callstack pointer 调用栈指针，保存处在调用栈顶的栈帧索引，即当前栈帧在调用栈中的索引
    Frame callstack[CALLSTACK_SIZE]; // callstack 调用栈，用于存储栈帧
    uint32_t br_table[BR_TABLE_SIZE];// 跳转指令索引表
} Module;

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module
struct Module *load_module(const uint8_t *bytes, uint32_t byte_count);

#endif
