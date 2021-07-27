#include "interpreter.h"
#include "module.h"
#include "utils.h"
#include <stdbool.h>
#include <stdio.h>

// 控制块（包含函数）被调用前，将【待调用的控制块（包含函数）关联的栈帧】压入到调用栈顶，成为当前栈帧
void push_block(Module *m, Block *block, int sp) {
    // 因新的栈帧要压入调用栈顶成为当前栈帧，所以调用栈指针（保存处在调用栈顶的栈帧索引）要加 1
    m->csp += 1;
    // 将 参数 block 设置为 当前栈帧关联的控制块
    m->callstack[m->csp].block = block;
    // 将 参数 sp 设置为 当前栈帧的操作数栈顶
    m->callstack[m->csp].sp = sp;
    // 将 上一个当前栈帧的操作数栈底 设置为 当前栈帧的操作数栈底
    m->callstack[m->csp].fp = m->fp;
    // 将 当前程序计数器保存的下一条即将执行的指令的地址 设置为 控制块（包含函数）返回地址
    // 也就是等到当前栈帧关联到控制块（包含函数）执行完后，返回到调用该控制块（包含函数）的地方继续执行后面的指令
    m->callstack[m->csp].ra = m->pc;
}

// 为调用索引为 fidx 的函数作准备，主要准备如下：
// 将函数参数和局部变量压入操作数栈，并将当前控制控制帧保存到控制栈中
// 且将函数的起始指令地址设置为 pc（program counter 程序计数器，用于记录下一条待执行指令的地址）
void setup_call(Module *m, uint32_t fidx) {
}

// 虚拟机执行字节码中的指令流
bool interpret(Module *m) {
    // 正常情况不会执行到这里
    return false;
}
