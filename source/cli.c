#include "interpreter.h"
#include "module.h"
#include "utils.h"
#include <readline/history.h>
#include <readline/readline.h>
#include <stdint.h>
#include <string.h>

#define BEGIN(x, y) "\033[" #x ";" #y "m"// x: 背景，y: 前景
#define CLOSE "\033[0m"                  // 关闭所有属性

// 命令行主函数
int main(int argc, char **argv) {
    char *mod_path;       // Wasm 模块文件路径
    uint8_t *bytes = NULL;// Wasm 模块文件映射的内存
    int byte_count;       // Wasm 模块文件映射的内存大小
    char *line = NULL;    // 指向每行输入的字符串的指针
    int res;              // 调用函数过程中的返回值，true 表示函数调用成功，false 表示函数调用失败

    // 如果参数数量不为 2，则报错并提示正确调用方式，然后退出
    if (argc != 2) {
        fprintf(stderr, "The right usage is:\n%s WASM_FILE_PATH\n", argv[0]);
        return 2;
    }

    // 第二个参数即 Wasm 文件路径
    mod_path = argv[1];

    // 加载 Wasm 模块，并映射到内存中
    bytes = mmap_file(mod_path, &byte_count);

    // 如果 Wasm 模块文件映射的内存为 NULL，则报错提示
    if (bytes == NULL) {
        fprintf(stderr, "Could not load %s", mod_path);
        return 2;
    }

    // 解析 Wasm 模块，即将 Wasm 二进制格式转化成内存格式
    Module *m = load_module(bytes, byte_count);

    // 无限循环，每次循环处理单行命令
    while (1) {
        line = readline(BEGIN(49, 34) "wasmc$ " CLOSE);

        // 指向每行输入的字符串的指针仍为 NULL，则退出命令行
        if (!line) {
            break;
        }

        // 如果输入的字符串为 quit，则退出命令行
        if (strcmp(line, "quit") == 0) {
            free(line);
            break;
        }

        // 将输入的字符串加入到历史命令
        add_history(line);

        // 参数个数初始化为 0
        argc = 0;

        // 将输入的字符串按照空格拆分成多个参数
        argv = split_argv(line, &argc);

        // 如果没有参数，则继续下一个循环
        if (argc == 0) {
            continue;
        }

        // 重置运行时相关状态，主要是清空操作数栈、调用栈等
        m->sp = -1;
        m->fp = -1;
        m->csp = -1;

        // 通过名称（即第一个参数）从 Wasm 模块中查找同名的导出函数
        Block *func = get_export(m, argv[0]);

        // 如果没有查找到函数，则报错提示信息，并进入下一个循环
        if (!func) {
            ERROR("no exported function named '%s'\n", argv[0])
            continue;
        }

        // 解析函数参数，并将参数压入到操作数栈
        parse_args(m, func->type, argc - 1, argv + 1);

        // 调用指定函数
        res = invoke(m, func->fidx);

        // 如果 invoke 函数返回 true，则说明函数成功执行，
        // 在判断函数是否有返回值，如果有返回值，则将返回值打印出来；
        // 如果 invoke 函数返回 true，则说明函数执行过程中出现异常，将异常信息打印出来即可。
        // 注：在解释执行函数过程中，如果有异常，会将异常信息写入到 exception 中
        if (res) {
            if (m->sp >= 0) {
                printf("%s\n", value_repr(&m->stack[m->sp]));
                // 刷新标准输出缓冲区，把输出缓冲区里的东西打印到标准输出设备上，已实现及时获取执行结果
                fflush(stdout);
            }
        } else {
            ERROR("Exception: %s\n", exception)
        }

        // readline 会为输入的字符串动态分配内存，所以使用完之后需要将内存释放掉
        free(line);
    }

    return 0;
}
