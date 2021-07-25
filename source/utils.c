#include "utils.h"
#include "module.h"
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 全局的异常信息，用于收集运行时（即虚拟机执行指令过程）中的异常信息
char exception[4096];

/*
 * LEB128（Little Endian Base 128） 变长编码格式目的是节约空间
 * 对于 32 位整数，编码后可能是 1 到 5 个字节
 * 对于 64 位整数，编码后可能是 1 到 10 个字节
 * 越小的整数，编码后占用的字节数就越小
 *
 * https://en.wikipedia.org/wiki/LEB128#Decode_unsigned_integer
 * 针对无符号整数的 LEB128 编码特点：
 * 1. 采用小端编码方式，即低位字节在前，高位字节在后
 * 2. 采用 128 进制，每 7 个比特为一组，由一个字节的后 7 位承载，空出来的最高位是标记位，1 表示后面还有后续字节，0 表示没有
 * 例如：LEB128 编码为 11100101 10001110 00100110，解码为 000 0100110 0001110 1100101
 * 注：0x80 -- 10000000    0x7f -- 01111111
 *
 * 针对有符号整数的 LEB128 编码，与上面无符号的完全相同，
 * 只有最后一个字节的第二高位是符号位，如果是 1，表示这是一个负数，需将高位全部补全为 1，如果是 0，表示这是一个正数，需将高位全部补全为 0
*/
uint64_t read_LEB(const uint8_t *bytes, uint32_t *pos, uint32_t maxbits, bool sign) {
    uint64_t result = 0;
    uint32_t shift = 0;
    uint32_t bcnt = 0;
    uint32_t startpos = *pos;
    uint64_t byte;

    while (true) {
        byte = bytes[*pos];
        *pos += 1;
        // 取字节中后 7 位作为值插入到 result 中，按照小端序，即低位字节在前，高位字节在后
        result |= ((byte & 0x7f) << shift);
        shift += 7;
        // 如果某个字节的最高位为 0，即和 0x80 相与结果为 0，则表示该字节为最后一个字节，没有后续字节了
        if ((byte & 0x80) == 0) {
            break;
        }
        bcnt += 1;
        // (maxbits + 7 - 1) / 7 表示要表示 maxbits 位二进制数字所需要的字节数减 1
        // 由于 bcnt 是从 0 开始
        // 所以 bcnt > (maxbits + 7 - 1) / 7 表示该次循环所得到的字节数 bcnt + 1 已经超过了 maxbits 位二进制数字所需的字节数 (maxbits + 7 - 1) / 7 + 1
        // 也就是该数字的位数超出了传入的最大位数值，所以报错
        if (bcnt > (maxbits + 7 - 1) / 7) {
            FATAL("Unsigned LEB at byte %d overflow", startpos)
        }
    }

    // 如果是有符号整数，针对于最后一个字节，则需要
    if (sign && (shift < maxbits) && (byte & 0x40)) {
        result |= -(1 << shift);
    }
    return result;
}

// 解码针对无符号整数的 LEB128 编码
uint64_t read_LEB_unsigned(const uint8_t *bytes, uint32_t *pos, uint32_t maxbits) {
    return read_LEB(bytes, pos, maxbits, false);
}

// 解码针对有符号整数的 LEB128 编码
uint64_t read_LEB_signed(const uint8_t *bytes, uint32_t *pos, uint32_t maxbits) {
    return read_LEB(bytes, pos, maxbits, true);
}

// 从字节数组中读取字符串，其中字节数组的开头 4 个字节用于表示字符串的长度
// 注：如果参数 result_len 不为 NULL，则会被赋值为字符串的长度
char *read_string(const uint8_t *bytes, uint32_t *pos, uint32_t *result_len) {
    // 读取字符串的长度
    uint32_t str_len = read_LEB_unsigned(bytes, pos, 32);
    // 为字符串申请内存
    char *str = malloc(str_len + 1);
    // 将字节数组的数据拷贝到字符串 str 中
    memcpy(str, bytes + *pos, str_len);
    // 字符串以字符 '\0' 结尾
    str[str_len] = '\0';
    // 字节数组位置增加相应字符串长度
    *pos += str_len;
    // 如果参数 result_len 不为 NULL，则会被赋值为字符串的长度
    if (result_len) {
        *result_len = str_len;
    }
    return str;
}

// 申请内存
void *acalloc(size_t nmemb, size_t size, char *name) {
    void *res = calloc(nmemb, size);
    if (res == NULL) {
        FATAL("Could not allocate %lu bytes for %s", nmemb * size, name)
    }
    return res;
}

// 在原有内存基础上重新申请内存
void *arecalloc(void *ptr, size_t old_nmemb, size_t nmemb, size_t size, char *name) {
    // 重新分配内存
    void *res = realloc(ptr, nmemb * size);
    if (res == NULL) {
        FATAL("Could not allocate %lu bytes for %s", nmemb * size, name)
    }
    // 将新申请的内存中的前面部分--即为新数据准备的内存空间，用 0 进行初始化
    memset(res + old_nmemb * size, 0, (nmemb - old_nmemb) * size);
    return res;
}

// 查找动态库中的 symbol
// 如果解析成功则返回 true
// 如果解析失败则返回 false 并设置 err
bool resolve_sym(char *filename, char *symbol, void **val, char **err) {
    void *handle = NULL;
    dlerror();

    if (filename) {
        handle = dlopen(filename, RTLD_LAZY);
        if (!handle) {
            *err = dlerror();
            return false;
        }
    }

    // 查找动态库中的 symbol
    // 根据 动态链接库 操作句柄(handle)与符号(symbol)，返回符号对应的地址。使用这个函数不但可以获取函数地址，也可以获取变量地址。
    // handle：由 dlopen 打开动态链接库后返回的指针；
    // symbol：要求获取的函数或全局变量的名称。
    // 返回值：指向函数的地址，供调用使用。
    *val = dlsym(handle, symbol);

    if ((*err = dlerror()) != NULL) {
        return false;
    }
    return true;
}

// 基于函数类型计算唯一的掩码值
uint64_t get_type_mask(Type *type) {
    uint64_t mask = 0x80;

    if (type->result_count == 1) {
        mask |= 0x80 - type->results[0];
    }
    mask = mask << 4;
    for (uint32_t p = 0; p < type->param_count; p++) {
        mask = ((uint64_t) mask) << 4;
        mask |= 0x80 - type->params[p];
    }
    return mask;
}

// 根据目前版本的 Wasm 标准，控制块不能有参数，且最多只能只能有一个返回值
// 注：目前多返回值提案还没有进入 Wasm 标准
uint32_t block_type_results[4][1] = {{I32}, {I64}, {F32}, {F64}};

Type block_types[5] = {
        {
                .result_count = 0,
        },
        {
                .result_count = 1,
                .results = block_type_results[0],
        },
        {
                .result_count = 1,
                .results = block_type_results[1],
        },
        {
                .result_count = 1,
                .results = block_type_results[2],
        },
        {
                .result_count = 1,
                .results = block_type_results[3],
        }};

// 根据表示该控制块的类型的值（占一个字节），返回控制块的类型（或签名），即控制块的返回值的数量和类型
// 0x7f 表示有一个 i32 类型返回值、0x7e 表示有一个 i64 类型返回值、0x7d 表示有一个 f32 类型返回值、0x7c 表示有一个 f64 类型返回值、0x40 表示没有返回值
// 注：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能只能有一个返回值
Type *get_block_type(uint8_t value_type) {
    switch (value_type) {
        case 0x40:
            return &block_types[0];
        case I32:
            return &block_types[1];
        case I64:
            return &block_types[2];
        case F32:
            return &block_types[3];
        case F64:
            return &block_types[4];
        default:
            FATAL("invalid block_type value_type: %d\n", value_type)
    }
}
