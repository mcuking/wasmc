#ifndef WASMC_INTERPRETER_H
#define WASMC_INTERPRETER_H

#include "module.h"
#include <stdbool.h>

// 为调用索引为 fidx 的函数作准备，主要准备如下：
// 将函数参数和局部变量压入操作数栈，并将当前控制控制帧保存到控制栈中
// 且将函数的起始指令地址设置为 pc（program counter 程序计数器，用于记录下一条待执行指令的地址）
void setup_call(Module *m, uint32_t fidx);

// 虚拟机执行字节码中的指令流
bool interpret(Module *m);

#endif
