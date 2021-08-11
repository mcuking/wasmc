#ifndef WASMC_INTERPRETER_H
#define WASMC_INTERPRETER_H

#include "module.h"
#include <stdbool.h>
#include <stdint.h>

// 调用函数前的设置，主要设置内容如下：
// 1. 将当前函数关联的栈帧压入到调用栈顶成为当前栈帧，同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
// 2. 将当前函数的局部变量压入到操作数栈顶（默认初始值为 0）
// 3. 将函数的字节码部分的【起始地址】设置为 pc（即下一条待执行指令的地址），即开始执行函数字节码中的指令流
void setup_call(Module *m, uint32_t fidx);

// 虚拟机执行字节码中的指令流
bool interpret(Module *m);

// 调用索引为 fidx 的函数
bool invoke(Module *m, uint32_t fidx);

// 计算初始化表达式
// 参数 type 为初始化表达式的返回值类型
// 参数 *pc 为初始化表达式的字节码部分的【起始地址】
void run_init_expr(Module *m, uint8_t type, uint32_t *pc);

#endif
