#include "interpreter.h"
#include "module.h"
#include "opcode.h"
#include "utils.h"
#include <stdbool.h>
#include <stdio.h>

// 控制块（包含函数）被调用前，将关联的栈帧压入到调用栈顶，成为当前栈帧，
// 同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
void push_block(Module *m, Block *block, int sp) {
    /* 1. 压入调用栈顶 */

    // 因新的栈帧要压入调用栈顶成为当前栈帧，所以调用栈指针（保存处在调用栈顶的栈帧索引）要加 1
    m->csp += 1;

    /* 2. 关联控制块 */

    // 将 参数 block 设置为 当前栈帧关联的控制块
    m->callstack[m->csp].block = block;

    /* 3. 保存 sp */

    // 将该栈帧被压入操作数栈顶前的【操作数栈顶指针】保存到 frame->sp 中，
    // 以便后续当前栈帧关联的控制块执行完成，当前栈帧弹出后，恢复压栈前的【操作数栈顶指针】
    m->callstack[m->csp].sp = sp;

    /* 4. 保存 fp */

    // 将该栈帧被压入操作数栈顶前的【当前栈帧的操作数栈底指针】保存到 frame>fp 中，
    // 以便后续该栈帧关联的控制块执行完成，该栈帧弹出后，恢复压栈前的【当前栈帧的操作数栈底指针】
    m->callstack[m->csp].fp = m->fp;

    /* 5. 保存 ra */

    // 将该栈帧被压入操作数栈顶前的【下一条即将执行的指令的地址】保存到 frame>ra 中，
    // 以便后续该栈帧关联的函数执行完后，返回到调用该函数的地方继续执行后面的指令
    m->callstack[m->csp].ra = m->pc;
}

// 当前控制块（包含函数）执行结束后，将关联的当前栈帧从调用栈顶中弹出，
// 同时恢复该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
Block *pop_block(Module *m) {
    /* 1. 弹出调用栈顶 */

    // 从调用栈顶中弹出当前栈帧，同时调用栈指针减 1
    Frame *frame = &m->callstack[m->csp--];

    /* 2. 校验控制块的返回值类型 */

    // 获取控制帧对应控制块（包含函数）的签名（即控制块的返回值的数量和类型）
    Type *t = frame->block->type;
    // 背景知识：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值
    // 如果控制块的返回值数量为 1，也就是有一个返回值时，需要对返回值类型进行校验
    if (t->result_count == 1) {
        // 获取当前栈帧的操作数栈顶值，也就是控制块（包含函数）的返回值，
        // 判断其类型和【控制块签名中的返回值类型】是否一致，如果不一致则报错
        if (m->stack[m->sp].value_type != t->results[0]) {
            sprintf(exception, "call type mismatch");
            return NULL;
        }
    }

    /* 3. 恢复 sp */

    // 因为该栈帧弹出，所以需要恢复该栈帧被压入调用栈前的【操作数栈顶指针】
    // 注：frame->sp 保存的是该栈帧被压入调用栈前的【操作数栈顶指针】
    if (t->result_count == 1) {
        // 背景知识：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值
        // 如果控制块有一个返回值，则这个返回值需要压入到恢复后的操作数栈顶，即恢复后的操作数栈长度需要加 1
        // 所以恢复的【操作数栈顶指针值】 是 该栈帧被压入调用栈前的【操作数栈顶指针】再加 1
        if (frame->sp < m->sp) {
            m->stack[frame->sp + 1] = m->stack[m->sp];
            m->sp = frame->sp + 1;
        }
    } else {
        // 如果控制块没有返回值，则直接恢复该栈帧被压入调用栈前的【操作数栈顶指针】即可
        if (frame->sp < m->sp) {
            m->sp = frame->sp;
        }
    }

    /* 4. 恢复 fp */

    // 因为该栈帧弹出，所以需要恢复该栈帧被压入调用栈前的【当前栈帧的操作数栈底指针】
    // 注：frame->fp 保存的是该栈帧被压入调用栈前的【当前栈帧的操作数栈底指针】
    m->fp = frame->fp;

    /* 5. 恢复 ra */

    // 当控制块类型为函数时，在函数执行完成该栈帧弹出时，需要返回到该函数调用指令的下一条指令继续执行
    if (frame->block->block_type == 0x00) {
        // 将函数返回地址赋给程序计数器 pc（记录下一条即将执行的指令的地址）
        m->pc = frame->ra;
    }

    return frame->block;
}

// 调用函数前的设置，主要设置内容如下：
// 1. 将当前函数关联的栈帧压入到调用栈顶成为当前栈帧，同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
// 2. 将当前函数的局部变量压入到操作数栈顶（默认初始值为 0）
// 3. 将函数的字节码部分的【起始地址】设置为 pc（即下一条待执行指令的地址），即开始执行函数字节码中的指令流
void setup_call(Module *m, uint32_t fidx) {
    // 根据索引 fidx 从 m->functions 中获取当前函数
    Block *func = &m->functions[fidx];

    // 获取函数签名
    Type *type = func->type;
    // 将当前函数关联的栈帧压入到调用栈顶，成为当前栈帧，同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
    // 注：第三个参数操作数栈顶指针减去函数参数个数的原因如下：
    // 调用该函数的父函数的栈帧的操作数栈，和该函数的栈帧的操作数栈，是相邻的，且有一部分数据是重叠的，
    // 这部分数据就是子函数的参数，这样就起到了父函数将参数传递给子函数的作用，所以目前操作数栈顶会有 type->param_count 个参数
    // 真实的操作数栈顶位置应该去除掉子函数参数个数，因为当子函数执行完成后，操作数栈上的参数应该要被消耗掉，
    // 所以真实的操作数栈顶指针应该是 m->sp - (int)type->param_count
    // push_block 函数的第三个参数的 sp 本意就是栈帧压入调用栈时的真实操作数栈顶，待后面函数执行完栈帧弹出时，恢复 push_block 中缓存的真实操作数栈顶
    push_block(m, func, m->sp - (int) type->param_count);

    // 设置当前栈帧的操作数栈底指针 fp，减去函数参数个数的原因同上，也是为了从父函数传递参数给子函数
    m->fp = m->sp - (int) type->param_count + 1;

    // 将当前函数的局部变量压入到操作数栈顶（默认初始值为 0）
    for (uint32_t lidx = 0; lidx < func->local_count; lidx++) {
        m->sp += 1;
        m->stack[m->sp].value_type = func->locals[lidx];
        m->stack[m->sp].value.uint64 = 0;
    }

    // 将函数的字节码部分的【起始地址】设置为 m->pc（即下一条待执行指令的地址）
    m->pc = func->start_addr;
}

// 虚拟机执行字节码中的指令流
bool interpret(Module *m) {
    const uint8_t *bytes = m->bytes;// Wasm 二进制内容
    StackValue *stack = m->stack;   // 操作数栈
    uint8_t opcode;                 // 操作码
    uint32_t cur_pc;                // 当前的程序计数器（即下一条即将执行的指令的地址）
    Block *block;                   // 控制块
    uint8_t value_type;             // 控制块返回值的类型（根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值）
    uint32_t cond;                  // 保存在操作数栈顶的判断条件的值

    while (m->pc < m->byte_count) {
        opcode = bytes[m->pc];// 读取指令中的操作码
        cur_pc = m->pc;       // 保存程序计数器的值（即下一条即将执行的指令的地址）
        m->pc += 1;           // 程序计数器加 1，即指向下一条指令

        switch (opcode) {
            /*
             * 控制指令--其他指令（2 条）
             * */
            case Unreachable:
                // 指令作用：引发运行时错误
                // 当执行 Unreachable 操作码时，则报错并返回 false 退出虚拟机执行
                sprintf(exception, "%s", "unreachable");
                return false;
            case Nop:
                // 指令作用：什么都不做
                // 注：Nop 即 No Operation 缩写
                continue;

            /*
             * 控制指令--结构化控制指令（3 条）
             * */
            case Block_:
            case Loop:
                // 指令作用：将当前控制块（block 或 loop 类型）关联的栈帧压入到调用栈顶，成为当前栈帧

                // 该指令的立即数为控制块的返回值类型（占 1 个字节）
                // TODO: 暂时不需要控制块的返回值类型，故暂时忽略
                value_type = read_LEB_unsigned(bytes, &m->pc, 32);
                (void) value_type;

                // 如果调用栈溢出，则报错并返回 false 退出虚拟机执行
                if (m->csp >= CALLSTACK_SIZE) {
                    sprintf(exception, "call stack exhausted");
                    return false;
                }

                // 在 block_lookup 中根据 Loop/Block_ 操作码的地址查找对应的控制块
                // 注：block_lookup 索引就是控制块的起始地址，而控制块就是以 Block_/Loop/If 操作码为开头
                block = m->block_lookup[cur_pc];

                // 控制块（包含函数）被调用前，将【待调用的控制块（包含函数）关联的栈帧】压入到调用栈顶，成为当前栈帧，
                // 同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
                push_block(m, block, m->sp);
                continue;
            case If:
                // 指令作用：将当前控制块（if 类型）关联的栈帧压入到调用栈顶，成为当前栈帧

                // 该指令的立即数为控制块的返回值类型（占 1 个字节）
                // TODO: 暂时不需要控制块的返回值类型，故暂时忽略
                value_type = read_LEB_unsigned(bytes, &m->pc, 32);
                (void) value_type;

                // 如果调用栈溢出，则报错并返回 false 退出虚拟机执行
                if (m->csp >= CALLSTACK_SIZE) {
                    sprintf(exception, "call stack exhausted");
                    return false;
                }

                // 在 block_lookup 中根据 If 操作码的地址查找对应的控制块
                // 注：block_lookup 索引就是控制块的起始地址，而控制块就是以 Block_/Loop/If 操作码为开头
                block = m->block_lookup[cur_pc];

                // 控制块（包含函数）被调用前，将【待调用的控制块（包含函数）关联的栈帧】压入到调用栈顶，成为当前栈帧，
                // 同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
                push_block(m, block, m->sp);

                // 从操作数栈顶获取判断条件的值
                // 注：在调用 If 指令时，操作数栈顶保存的就是判断条件的值
                cond = stack[m->sp--].value.uint32;
                // 如果判断条件为 false，则将程序计数器 pc 设置为 else 控制块首地址或 if 控制块结尾地址，
                // 即跳过 if 分支的代码对应的指令，执行后面的指令
                if (cond == 0) {
                    if (block->else_addr == 0) {
                        // 如果不存在 else 分支，则跳转到 if 控制块结尾的下一条指令继续执行
                        m->pc = block->br_addr + 1;
                        // 在上面的 push_block 函数中 if 控制块对应的栈帧已经被压入到调用栈且调用栈顶索引 csp 加 1，
                        // 此时不需要执行 if 控制块的指令，所以调用栈顶索引需要减 1
                        m->csp -= 1;
                    } else {
                        // 如果存在 else 分支，则执行 else 分支代码对应的字节码的起始指令，也是 Else_ 指令的下一条指令
                        m->pc = block->else_addr;
                    }
                }
                continue;

            /*
             * 控制指令--伪指令（2 条）
             * 注：Else_ 和 End 指令只起分隔作用，故称为伪指令
             * */
            case Else_:
                // 指令作用：跳转到控制块的结尾指令继续执行

                // 获取当前栈帧对应的控制块
                block = m->callstack[m->csp].block;
                // 跳转到控制块的结尾指令继续执行
                // 注：当上一个分支对应的指令流执行完成后，会执行到 Else_ 指令，则需要跳过 Else_ 指令后面的 else 分支对应的指令流，
                // 直接执行控制块的结尾指令，可以看出 Else_ 指令起到了分隔多个分支对应的指令流的作用
                m->pc = block->br_addr;
                continue;
            case End_:
                // 当前控制块（包含函数）执行结束后，将关联的当前栈帧从调用栈顶中弹出，
                // 同时恢复该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
                block = pop_block(m);

                // 如果 pop_block 函数返回 NULL，则说明有报错（具体逻辑可查看 pop_block 函数），
                // 则直接返回 false 退出虚拟机执行
                if (block == NULL) {
                    return false;
                }

                if (block->block_type == 0x00) {
                    // 1. 当控制块类型为函数时，且调用栈为空（即 csp 为 -1），说明已经执行完顶层的控制块，
                    // 则直接返回 true 退出虚拟机执行，否则继续执行下一条指令
                    if (m->csp == -1) {
                        return true;
                    }
                } else if (block->block_type == 0x01) {
                    // 2. 当控制块类型为初始化表达式时，说明只有一层控制块调用，则直接返回 true 退出虚拟机执行即可
                    return true;
                }
                // 3. 当控制块的块类型为 block/loop/if，则继续执行下一条指令
                continue;
            default:
                // 无法识别的非法操作码（不在 Wasm 规定的字节码）
                return false;
        }
    }

    // 正常情况不会执行到这里
    return false;
}

// 调用索引为 fidx 的函数
bool invoke(Module *m, uint32_t fidx) {
    bool result;

    // 调用函数前的设置，主要设置内容如下：
    // 1. 将当前函数关联的栈帧压入到调用栈顶成为当前栈帧，同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
    // 2. 将当前函数的局部变量压入到操作数栈顶（默认初始值为 0）
    // 3. 将函数的字节码部分的【起始地址】设置为 pc（即下一条待执行指令的地址），即开始执行函数字节码中的指令流
    setup_call(m, fidx);

    // 虚拟机执行起始函数的字节码中的指令流
    result = interpret(m);

    // 返回虚拟机的执行指令的结果
    // 如果结果为 false，表示执行过程中出现异常。如果结果为 true，表示成功执行完指令流。
    return result;
}

// 计算初始化表达式
// 参数 type 为初始化表达式的返回值类型
// 参数 *pc 为初始化表达式的字节码部分的【起始地址】
void run_init_expr(Module *m, uint8_t type, uint32_t *pc) {
    m->pc = *pc;// 将控制块中字节码部分的【起始地址】赋值给程序计数器 m->pc（程序计数器，记录下一条即将执行的指令的地址）

    Block block = {
            .block_type = 0x01,          // 控制块类型为初始化表达式
            .type = get_block_type(type),// 控制块签名（返回值数量和类型）由参数 type 决定
            .start_addr = *pc            // 控制块中字节码部分的【起始地址】
    };
    // 初始化表达式的字节码中的指令流被执行前，将【待调用的初始化表达式控制块关联的栈帧】压入到调用栈顶，成为当前栈帧，
    // 同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
    push_block(m, &block, m->sp);

    // 虚拟机执行初始化表达式的字节码中的指令流
    interpret(m);

    // 当初始化表达式的字节码中的指令流被执行完成后，需要将在 Wasm 二进制字节码中的当前位置的地址赋给参数 *pc，以便继续解析后面的 Wasm 二进制字节码
    // 注：run_init_expr 函数是在解析 Wasm 二进制字节码的 load_module 函数中调用，
    // 在调用 run_init_expr 函数计算初始化表达式结果后，仍需要继续解析后面的二进制字节码内容
    *pc = m->pc;

    // 初始化表达式的字节码中的指令流执行完成后，操作数栈顶保存的就是指令流的执行结果，也就是初始化表达式计算的返回值
    // 由于初始化表达式计算一定会有返回值，且目前版本的 Wasm 规范规定控制块最多只能有一个返回值，所以初始化表达式计算必定会有一个返回值
    // 所以可以通过比对保存在操作数栈顶的值类型和参数 type 是否相同，来判断计算得到的返回值的类型是否正确
    ASSERT(m->stack[m->sp].value_type == type, "init_expr type mismatch 0x%x != 0x%x", m->stack[m->sp].value_type, type)
}
