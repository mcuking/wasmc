#include "module.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module，以便后续虚拟机基于此执行对应指令
struct Module *load_module(const uint8_t *bytes, const uint32_t byte_count) {
    // 用于标记解析 Wasm 二进制文件第 pos 个字节
    uint32_t pos = 0;

    // 声明内存格式对应的结构体 m
    struct Module *m;

    // 为 Wasm 内存格式对应的结构体 m 申请内存
    m = acalloc(1, sizeof(struct Module), "Module");

    m->bytes = bytes;
    m->byte_count = byte_count;

    // 首先读取魔数(magic number)，检查是否正确
    // 注：和其他很多二进制文件（例如 Java 类文件）一样，Wasm 也同样使用魔数来标记其二进制文件类型
    // 所谓魔数，你可以简单地将它理解为具有特定含义的一串数字
    // 一个标准 Wasm 二进制模块文件的头部数据是由具有特殊含义的字节组成的
    // 其中开头的前四个字节为 '（高地址）0x6d 0x73 0x61 0x00（低地址）'，这四个字节对应的 ASCII 字符为 'asm'
    uint32_t magic = ((uint32_t *) (bytes + pos))[0];
    pos += 4;
    ASSERT(magic == WA_MAGIC, "Wrong module magic 0x%x\n", magic)

    // 然后读取当前 Wasm 二进制文件所使用的 Wasm 标准版本号，检查是否正确
    // 注：紧跟在魔数后面的 4 个字节是用来表示当前 Wasm 二进制文件所使用的 Wasm 标准版本号
    // 目前所有 Wasm 模块该四个字节的值为 '（高地址）0x00 0x00 0x00 0x01（低地址）'，即表示使用的 Wasm 标准版本为 1
    uint32_t version = ((uint32_t *) (bytes + pos))[0];
    pos += 4;
    ASSERT(version == WA_VERSION, "Wrong module version 0x%x\n", version)

    return m;
}
