#include "module.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    // 最后根据段 ID 分别解析后面的各个段的内容
    // 和其他二进制格式（例如 Java 类文件）一样，Wasm 二进制格式也是以魔数和版本号开头，
    // 之后就是模块的主体内容，这些内容被分别放在不同的段（Section）中。
    // 一共定义了 12 种段，每种段分配了 ID（从 0 到 11）。除了自定义段之外，其他所有段都最多只能出现一次，且须按照 ID 递增的顺序出现。
    // ID 从 0 到 11 依次有如下 12 个段：
    // 自定义段、类型段、导入段、函数段、表段、内存段、全局段、导出段、起始段、元素段、代码段、数据段
    while (pos < byte_count) {
        // 每个段的第 1 个字节为该段的 ID，用于标记该段的类型
        uint32_t id = read_LEB_unsigned(bytes, &pos, 7);

        // 紧跟在段 ID 后面的 4 个字节用于记录该段所占字节总长度
        uint32_t slen = read_LEB_unsigned(bytes, &pos, 32);

        switch (id) {
            case CustomID: {
                // 解析自定义段
                // TODO: 暂不处理自定义段内容，直接跳过
                pos += slen;
                break;
            }
            case TypeID: {
                // 解析类型段
                // 即解析模块中所有函数类型（也叫函数签名或函数原型）
                // 函数原型示例：(a, b, ...) -> (x, y, ...)
                // 类型段编码格式如下：
                // type_sec: 0x01|byte_count|vec<func_type>

                // 读取类型段中所有函数类型的数量
                m->type_count = read_LEB_unsigned(bytes, &pos, 32);

                // 为存储类型段中的函数类型申请内存
                m->types = acalloc(m->type_count, sizeof(Type), "Module->types");

                // 遍历解析每个类型 func_type，其编码格式如下：
                // func_type: 0x60|param_count|(param_val)+|return_count|(return_val)+
                for (uint32_t i = 0; i < m->type_count; i++) {
                    Type *type = &m->types[i];

                    // 函数标记值 FtTag（即 0x60），暂时忽略
                    read_LEB_unsigned(bytes, &pos, 7);

                    // 解析函数参数个数
                    type->param_count = read_LEB_unsigned(bytes, &pos, 32);
                    type->params = acalloc(type->param_count, sizeof(uint32_t),
                                           "type->params");
                    // 解析函数每个参数的类型
                    for (uint32_t p = 0; p < type->param_count; p++) {
                        type->params[p] = read_LEB_unsigned(bytes, &pos, 32);
                    }

                    // 解析函数返回值个数
                    type->result_count = read_LEB_unsigned(bytes, &pos, 32);
                    type->results = acalloc(type->result_count, sizeof(uint32_t),
                                            "type->results");
                    // 解析函数每个返回值的类型
                    for (uint32_t r = 0; r < type->result_count; r++) {
                        type->results[r] = read_LEB_unsigned(bytes, &pos, 32);
                    }

                    // 基于函数类型计算的唯一掩码值
                    type->mask = get_type_mask(type);
                }
                break;
            }
            case FuncID: {
                // 解析函数段
                // 函数段列出了内部函数的函数类型在所有函数类型中的索引，函数的局部变量和字节码则存在代码段中
                // 编码格式如下：
                // func_sec: 0x03|byte_count|vec<type_idx>

                // 读取函数段所有函数的数量
                m->function_count += read_LEB_unsigned(bytes, &pos, 32);

                // 为存储函数段中的所有函数申请内存
                Block *functions;
                functions = acalloc(m->function_count, sizeof(Block), "Block(function)");

                // 由于解析了导入段在解析函数段之前，而导入段中可能有导入外部模块函数
                // 因此如果 m->import_func_count 不为 0，则说明已导入外部函数，并存储在了 m->functions 中
                // 所以需要先将存储在了 m->functions 中的导入函数对应数据拷贝到 functions 中
                // 简单来说，就是先将之前解析导入函数所得到的数据，拷贝到新申请的内存中（因为之前申请的内存已不足以存储所有函数的数据）
                if (m->import_func_count != 0) {
                    memcpy(functions, m->functions, sizeof(Block) * m->import_func_count);
                }
                m->functions = functions;

                // 遍历每个函数项，读取其对应的函数类型在所有函数类型中的索引，并根据索引获取到函数类型
                for (uint32_t f = m->import_func_count; f < m->function_count; f++) {
                    // f 为该函数在所有函数（包括导入函数）中的索引
                    m->functions[f].fidx = f;
                    // tidx 为该内部函数的函数类型在所有函数类型中的索引
                    uint32_t tidx = read_LEB_unsigned(bytes, &pos, 32);
                    // 通过索引 tidx 从所有函数类型中获取到具体的函数类型
                    m->functions[f].type = &m->types[tidx];
                }
                break;
            }
            default: {
                pos += slen;
                // 如果不是上面 0 到 11 ID，则报错
                FATAL("Section %d unimplemented\n", id)
            }
        }
    }

    return m;
}
