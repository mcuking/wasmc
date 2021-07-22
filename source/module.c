#include "module.h"
#include "utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 解析表段中的表 table_type（目前表段只会包含一张表）
// 表 table_type 编码如下：
// table_type: 0x70|limits
// limits: flags|min|(max)?
// 注：之所以要封装成独立函数，是因为在 load_module 函数中有两次调用：1.解析本地定义的表段；2. 解析从外部导入的表
void parse_table_type(Module *m, uint32_t *pos) {
    // 由于表段中只会有一张表，所以无需遍历

    // 表中的元素必需为函数引用，所以编码必需为 0x70
    m->table.elem_type = read_LEB_unsigned(m->bytes, pos, 7);
    ASSERT(m->table.elem_type == ANYFUNC, "Table elem_type 0x%x unsupported", m->table.elem_type)

    // flags 为标记位，如果为 0 表示只需指定表中元素数量下限；为 1 表示既要指定表中元素数量的上限，又指定表中元素数量的下限
    uint32_t flags = read_LEB_unsigned(m->bytes, pos, 32);
    // 先读取表中元素数量下限，同时设置为该表的当前元素数量
    uint32_t tsize = read_LEB_unsigned(m->bytes, pos, 32);
    m->table.min_size = tsize;
    m->table.cur_size = tsize;
    // flags 为 1 表示既要指定表中元素数量的上限，又指定表中元素数量的下限
    if (flags & 0x1) {
        // 读取表中元素数量的上限
        tsize = read_LEB_unsigned(m->bytes, pos, 32);
        // 表的元素数量最大上限为 64K，如果读取的表的元素数量上限值超过 64K，则默认设置 64K，否则设置为读取的值即可
        m->table.max_size = (uint32_t) fmin(0x10000, tsize);
    } else {
        // flags 为 0，表示没有特别指定表的元素数量上限，所以设置为默认的 64K 即可
        m->table.max_size = 0x10000;
    }
}

// 解析内存段中的内存 mem_type（目前内存段只会包含一块内存）
// 内存 mem_type 编码如下：
// mem_type: limits
// limits: flags|min|(max)?
// 注：之所以要封装成独立函数，是因为在 load_module 函数中有两次调用：1.解析本地定义的内存段；2. 解析从外部导入的内存
void parse_memory_type(Module *m, uint32_t *pos) {
    // 由于内存段中只会有一块内存，所以无需遍历

    // flags 为标记位，如果为 0 表示只指定内存大小的下限；为 1 表示既指定内存大小的上限，又指定内存大小的下限
    uint32_t flags = read_LEB_unsigned(m->bytes, pos, 32);
    // 先读取内存大小的下限，并设置为该内存的初始大小
    uint32_t pages = read_LEB_unsigned(m->bytes, pos, 32);
    m->memory.min_size = pages;
    m->memory.cur_size = pages;

    // flags 为 1 表示既指定内存大小上限，又指定内存大小下限
    if (flags & 0x1) {
        // 读取内存大小上限
        pages = read_LEB_unsigned(m->bytes, pos, 32);
        // 内存大小最大上限为 2GB，如果读取的内存大小上限值超过 2GB，则默认设置 2GB，否则设置为读取的值即可
        m->memory.max_size = (uint32_t) fmin(0x8000, pages);
    } else {
        // flags 为 0，表示没有特别指定内存大小上限，所以设置为默认的 2GB 即可
        m->memory.max_size = 0x8000;
    }
}

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

    // 起始函数索引初始值设置为 -1
    m->start_function = -1;

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

        // 每次解析某个段的数据时，先将当前解析到的位置保存起来，以便后续使用
        uint32_t start_pos = pos;

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

                // 函数段编码格式如下：
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
            case TableID: {
                // 解析表段
                // 表段持有一个带有类型的引用数组，比如像函数这种无法作为原始字节存储在模块线性内存中的项目
                // 通过为 Wasm 框架提供一种能安全映射对象的方式，表段可以为 Wasm 提供一部分代码安全性
                // 当代码想要访问表段中引用的数据时，它要向 Wasm 框架请求变种特定索引处的条目，
                // 然后 Wasm 框架会读取存储在这个索引处的地址，并执行相关动作

                // 表段和表项编码格式如下：
                // table_sec: 0x04|byte_count|vec<table_type> # vec 目前长度只能是 1
                // table_type: 0x70|limits
                // limits: flags|min|(max)?

                // 读取表的数量
                uint32_t table_count = read_LEB_unsigned(bytes, &pos, 32);
                // 模块最多只能定义一张表，因此 table_count 必需为 1
                ASSERT(table_count == 1, "More than 1 table not supported")

                // 解析表段中的表 table_type（目前模块只会包含一张表）
                parse_table_type(m, &pos);

                // 为存储表中的元素申请内存（在解析元素段时会用到--将元素段中的索引存储到刚申请的内存中）
                m->table.entries = acalloc(m->table.cur_size, sizeof(uint32_t), "Module->table.entries");
                break;
            }
            case MemID: {
                // 解析内存段
                // 内存段列出了模块内定义的内存，由于 Wasm 模块不能直接访问设备内存，
                // 实例化模块的环境传入一个 ArrayBuffer，Wasm 模块示例将其用作线性内存。
                // 模块的内存被定义为 Wasm 页，每页 64KB。当环境指定 Wasm 模块可以使用多少内存时，指定的是初始页数，
                // 可能还有一个最大页数。如果模块需要更多内存，可以请求内存增长指定页数。如果指定了最大页数，则框架会防止内存增长超过这一点
                // 如果没有指定最大页数，则内存可以无限增长

                // 内存段和内存类型编码格式如下：
                // mem_sec: 0x05|byte_count|vec<mem_type> # vec 目前长度只能是 1
                // mem_type: limits
                // limits: flags|min|(max)?

                // 读取内存的数量
                uint32_t memory_count = read_LEB_unsigned(bytes, &pos, 32);
                // 模块最多只能定义一块内存，因此 memory_count 必需为 1
                ASSERT(memory_count == 1, "More than 1 memory not supported\n")

                // 解析内存段中内存 mem_type（目前模块只会包含一块内存）
                parse_memory_type(m, &pos);

                // 为存储内存中的数据申请内存（在解析数据段时会用到--将数据段中的数据存储到刚申请的内存中）
                m->memory.bytes = acalloc(m->memory.cur_size * PAGE_SIZE, sizeof(uint32_t), "Module->memory.bytes");
                break;
            }
            case GlobalID: {
                // 解析全局段
                // 全局段列出了模块内定义的所有全局变量
                // 每一项包括全局变量的类型（值类型和可变性）以及初始值

                // 全局段和全局项的编码格式如下：
                // global_sec: 0x60|byte_count|vec<global>
                // global: global_type|init_expr
                // global_type: val_type|mut
                // init_expr: (byte)+|0x0B

                // 读取模块中全局变量的数量
                uint32_t global_count = read_LEB_unsigned(bytes, &pos, 32);

                // 遍历全局段中的每一个全局变量项
                for (uint32_t g = 0; g < global_count; g++) {
                    // 先读取全局变量的值类型
                    uint8_t type = read_LEB_unsigned(bytes, &pos, 7);

                    // 再读取全局变量的可变性
                    uint8_t mutability = read_LEB_unsigned(bytes, &pos, 1);
                    // TODO: 可变性暂无用处，故先将变量 mutability 标记为无用
                    (void) mutability;

                    // 先保存当前全局变量的索引
                    uint32_t gidx = m->global_count;

                    // 全局变量数量加 1
                    m->global_count += 1;

                    // 由于新增一个全局变量，所以需要重新申请内存，调用 arecalloc 函数在原有内存基础上重新申请内存
                    m->globals = arecalloc(m->globals, gidx, m->global_count, sizeof(StackValue), "globals");

                    // 设置当前全局变量的值类型
                    m->globals[gidx].value_type = type;

                    // TODO: 设置当前全局变量的初始值，需要执行表达式 init_expr 对应的字节码指令，来获得初始值，要等到虚拟机完成后才可实现
                }
                pos = start_pos + slen;
                break;
            }
            case ExportID: {
                // 解析导出段
                // 导出段包含模块所有导出成员，主要包含四种：函数、表、内存、全局变量
                // 导出项主要包括两个：1.导出成员名  2.导出描述：1 个字节的类型（0-函数、1-表、2-内存、3-全局变量）+ 导出项在相应段中的索引

                // 导出段编码格式如下：
                // export_sec: 0x07|byte_count|vec<export>
                // export: name|export_desc
                // export_desc: tag|[func_idx, table_idx, mem_idx, global_idx]

                // 读取导出项数量
                uint32_t export_count = read_LEB_unsigned(bytes, &pos, 32);

                // 遍历所有导出项，解析对应数据
                for (uint32_t e = 0; e < export_count; e++) {
                    // 读取导出成员名
                    char *name = read_string(bytes, &pos, NULL);

                    // 读取导出类型
                    uint32_t external_kind = bytes[pos++];

                    // 读取导出项在相应段中的索引
                    uint32_t index = read_LEB_unsigned(bytes, &pos, 32);

                    // 先保存当前导出项的索引
                    uint32_t eidx = m->export_count;

                    // 导出项数量加 1
                    m->export_count += 1;

                    // 由于新增一个导出项，所以需要重新申请内存，调用 arecalloc 函数在原有内存基础上重新申请内存
                    m->exports = arecalloc(m->exports, eidx, m->export_count, sizeof(Export), "exports");

                    // 设置导出项的成员名
                    m->exports[eidx].export_name = name;

                    // 设置导出项的类型
                    m->exports[eidx].external_kind = external_kind;

                    // 根据导出项的类型，设置导出项的值
                    switch (external_kind) {
                        case KIND_FUNCTION:
                            // 获取函数并赋给导出项
                            m->exports[eidx].value = &m->functions[index];
                            break;
                        case KIND_TABLE:
                            // 目前 WASM 版本规定只能定义一张表，所以索引只能为 0
                            ASSERT(index == 0, "Only 1 table in MVP");
                            // 获取模块内定义的表并赋给导出项
                            m->exports[eidx].value = &m->table;
                            break;
                        case KIND_MEMORY:
                            // 目前 WASM 版本规定只能定义一个内存，所以索引只能为 0
                            ASSERT(index == 0, "Only 1 memory in MVP");
                            // 获取模块内定义的内存并赋给导出项
                            m->exports[eidx].value = &m->memory;
                            break;
                        case KIND_GLOBAL:
                            // 获取全局变量并赋给导出项
                            m->exports[eidx].value = &m->globals[index];
                            break;
                        default:
                            break;
                    }
                }
                break;
            }
            case StartID: {
                // 解析起始段
                // 起始段记录了起始函数索引，而起始函数是在【模块完成初始化后】，【被导出函数可调用之前】自动被调用的函数
                // 可以将起始函数视为一种初始化全局变量或内存的函数，且函数必须处于被模块内部，不能是从外部导入的
                // 起始函数的作用有两个：
                // 1. 在模块加载后进行初始化工作
                // 2. 将模块变成可执行文件

                // 起始段的编码格式如下：
                // start_sec: 0x08|byte_count|func_idx
                m->start_function = read_LEB_unsigned(bytes, &pos, 32);
                break;
            }
            case ElemID: {
                // 解析元素段
                // 元素段用于存放表初始化数据
                // 元素项包含三部分：1.表索引（初始化哪张表）2.表内偏移量（从哪开始初始化） 3. 函数索引列表（给定的初始化数据）

                // 元素段编码格式如下：
                // elem_sec: 0x09|byte_count|vec<elem>
                // elem: table_idx|offset_expr|vec<func_id>

                // 读取元素数量
                uint32_t elem_count = read_LEB_unsigned(bytes, &pos, 32);

                // 依次对表中每个元素进行初始化
                for (uint32_t c = 0; c < elem_count; c++) {
                    // 读取表索引 table_idx（即初始化哪张表）
                    uint32_t index = read_LEB_unsigned(bytes, &pos, 32);
                    // 目前 WASM 版本规定一个模块只能定义一张表，所以 index 只能为 0
                    ASSERT(index == 0, "Only 1 default table in MVP")

                    // TODO: 设置表内偏移量（从哪开始初始化），需要执行表达式 offset_expr 对应的字节码指令，来获得偏移量，要等到虚拟机完成后才可实现

                    // 暂时设置为 0
                    uint32_t offset = 0;

                    // 函数索引列表（即给定的元素初始化数据）
                    uint32_t num_elem = read_LEB_unsigned(bytes, &pos, 32);
                    // 遍历函数索引列表，将列表中的函数索引设置为元素的初始值
                    for (uint32_t n = 0; n < num_elem; n++) {
                        m->table.entries[offset + n] = read_LEB_unsigned(bytes, &pos, 32);
                    }
                }
                pos = start_pos + slen;
                break;
            }
            case DataID: {
                // 解析数据段
                // 数据段用于存放内存的初始化数据
                // 元素项包含三部分：1.内存索引（初始化哪块内存）2. 内存偏移量（从哪里开始初始化）3. 初始化数据

                // 数据段编码格式如下：
                // data_sec: 0x09|byte_count|vec<data>
                // data: mem_idx|offset_expr|vec<byte>

                // 读取数据数量
                uint32_t mem_count = read_LEB_unsigned(bytes, &pos, 32);

                // 依次对内存中每个部分进行初始化
                for (uint32_t s = 0; s < mem_count; s++) {
                    // 读取内存索引 mem_idx（即初始化哪块内存）
                    uint32_t index = read_LEB_unsigned(bytes, &pos, 32);
                    // 目前 WASM 版本规定一个模块只能定义一块内存，所以 index 只能为 0
                    ASSERT(index == 0, "Only 1 default memory in MVP");

                    // TODO: 设置内存偏移量（从哪开始初始化），需要执行表达式 offset_expr 对应的字节码指令，来获得偏移量，要等到虚拟机完成后才可实现

                    // 暂时设置为 0
                    uint32_t offset = 0;

                    // 读取初始化数据所占内存大小
                    uint32_t size = read_LEB_unsigned(bytes, &pos, 32);

                    // 将写在二进制文件中的初始化数据拷贝到指定偏移量的内存中
                    memcpy(m->memory.bytes + offset, bytes + pos, size);
                    pos += size;
                }
                break;
            }
            default: {
                // 如果没有匹配到任何段，则只需 pos 增加相应值即可
                pos += slen;
                // 如果不是上面 0 到 11 ID，则报错
                FATAL("Section %d unimplemented\n", id)
            }
        }
    }

    return m;
}
