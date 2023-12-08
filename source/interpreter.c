#include "interpreter.h"
#include "module.h"
#include "opcode.h"
#include "utils.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
        // 判断其类型和【控制块签名中的返回值类型】是否一致，如果不一致则记录异常信息
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
        // 所以恢复的【操作数栈顶指针值】是 该栈帧被压入调用栈前的【操作数栈顶指针】再加 1
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
    uint32_t depth;                 // 跳转指令的目标标签索引
    uint32_t fidx;                  // 函数索引
    uint32_t idx;                   // 变量索引
    uint8_t *maddr;                 // 实际内存地址指针
    uint32_t addr;                  // 用于计算相对内存地址
    uint32_t offset;                // 内存偏移量
    uint32_t a, b, c;               // 用于 I32 数值计算
    uint64_t d, e, f;               // 用于 I64 数值计算
    float g, h, i;                  // 用于 F32 数值计算
    double j, k, l;                 // 用于 F64 数值计算

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
                // 当执行 Unreachable 操作码时，则记录异常信息并返回 false 退出虚拟机执行
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

                // 如果调用栈溢出，则记录异常信息并返回 false 退出虚拟机执行
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

                // 如果调用栈溢出，则记录异常信息并返回 false 退出虚拟机执行
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
                // 指令作用：控制块执行结束后，将关联的当前栈帧从调用栈顶中弹出，并根据具体情况决定是否退出虚拟机的执行

                // 当前控制块（包含函数）执行结束后，将关联的当前栈帧从调用栈顶中弹出，
                // 同时恢复该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
                block = pop_block(m);

                // 如果 pop_block 函数返回 NULL，则说明有异常（具体逻辑可查看 pop_block 函数），
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

            /*
             * 控制指令--跳转指令（4 条）
             * */
            case Br:
                // 指令作用：跳转到目标控制块的跳转地址继续执行后面的指令

                // 该指令的立即数表示跳转的目标标签索引（占 4 个字节）
                // 另外该目标标签索引是相对的，例如为 0 表示该指令所在的控制块定义的跳转标签，
                // 为 1 表示往外一层控制块定义的跳转标签，
                // 为 2 表示再往外一层控制块定义的跳转标签，以此类推
                depth = read_LEB_unsigned(bytes, &m->pc, 32);
                // 将目标控制块关联的栈帧设置为当前栈帧
                m->csp -= (int) depth;
                // 跳转到目标控制块的跳转地址继续执行后面的指令
                m->pc = m->callstack[m->csp].block->br_addr;
                continue;
            case BrIf:
                // 指令作用：根据判断条件决定是否跳转到目标控制块的跳转地址继续执行后面的指令

                // 该指令的立即数表示跳转的目标标签索引（占 4 个字节）
                // 另外该目标标签索引是相对的，例如为 0 表示该指令所在的控制块定义的跳转标签，
                // 为 1 表示往外一层控制块定义的跳转标签，
                // 为 2 表示再往外一层控制块定义的跳转标签，以此类推
                depth = read_LEB_unsigned(bytes, &m->pc, 32);
                // 将操作数栈顶值弹出，作为判断条件
                cond = stack[m->sp--].value.uint32;
                // 如果为真则跳转，否则不跳转
                if (cond) {
                    // 将目标控制块关联的栈帧设置为当前栈帧
                    m->csp -= (int) depth;
                    // 跳转到目标控制块的跳转地址继续执行后面的指令
                    m->pc = m->callstack[m->csp].block->br_addr;
                }
                continue;
            case BrTable: {
                // 指令作用：根据运行时具体情况决定跳转到哪个目标控制块的跳转地址继续执行后面的指令

                // 该指令的立即数给定了 n+1 个跳转目标标签索引
                // 其中前 n 个目标标签索引构成一个索引表，后一个标签索引为默认索引
                // 最终跳转到哪一个目标标签索引，需要在运行期间才能决定

                // 该指令执行时，先从操作数栈顶弹出一个 i32 类型的值 m，
                // 如果 m 小于 n，则跳转到索引表第 m 个索引指向的目标标签处，
                // 否则跳转到默认索引指定的标签处

                // 读取目标标签索引的数量，也就是索引表的大小
                uint32_t count = read_LEB_unsigned(bytes, &m->pc, 32);

                // 如果索引表超出了规定的最大值，则记录异常信息并直接返回 false 退出虚拟机执行
                if (count > BR_TABLE_SIZE) {
                    sprintf(exception, "br_table size %d exceeds max %d\n", count, BR_TABLE_SIZE);
                    return false;
                }

                // 构造索引表
                for (uint32_t n = 0; n < count; n++) {
                    m->br_table[n] = read_LEB_unsigned(bytes, &m->pc, 32);
                }

                // 读取默认索引
                depth = read_LEB_unsigned(bytes, &m->pc, 32);

                // 从操作数栈顶弹出一个 i32 类型的值 m
                int32_t didx = stack[m->sp--].value.int32;
                // 如果 m 小于索引表大小 n，则跳转到索引表第 m 个索引指向的目标标签处，
                // 否则跳转到默认索引指定的标签处
                if (didx >= 0 && didx < (int32_t) count) {
                    depth = m->br_table[didx];
                }

                // 将目标控制块关联的栈帧设置为当前栈帧
                m->csp -= (int) depth;
                // 跳转到目标控制块的跳转地址继续执行后面的指令
                m->pc = m->callstack[m->csp].block->br_addr;
                continue;
            }
            case Return:
                // 指令作用：直接跳出最外层控制块，最终效果是函数返回

                // 循环向外层控制块跳转，直到跳转到当前函数对应的控制块（也就是循环条件中判断是否是函数类型的代码块）
                while (m->csp >= 0 && m->callstack[m->csp].block->block_type != 0x00) {
                    m->csp--;
                }
                // 直接跳到当前函数对应的控制块结尾处，即 End_ 指令处并执行该指令
                // 对应的当前栈帧弹出调用栈和退出虚拟机执行 是在 End_ 指令执行逻辑中
                m->pc = m->callstack[m->csp].block->end_addr;
                continue;

            /*
             * 控制指令----函数调用指令（2 条）
             * */
            case Call:
                // 指令作用：调用指定函数
                // 注：Call 指令要调用的函数是在编译期确定的，也就是说被调用函数的索引硬编码在 call 指令的立即数中

                // 读取该指令的立即数，也就是被调用函数的索引（占 4 个字节）
                fidx = read_LEB_unsigned(bytes, &m->pc, 32);

                // 如果函数索引值小于 m->import_func_count，则说明该函数为外部函数
                // 原因：在解析 Wasm 二进制文件内容时，首先解析导入段中的函数到 m->functions，然后再解析函数段中的函数到 m->functions
                if (fidx < m->import_func_count) {
                    // TODO: 暂时忽略调用外部引入函数情况
                } else {
                    // 如果调用栈溢出，则记录异常信息并返回 false 退出虚拟机执行
                    if (m->csp >= CALLSTACK_SIZE) {
                        sprintf(exception, "call stack exhausted");
                        return false;
                    }

                    // 调用函数前的设置，主要设置内容如下：
                    // 1. 将当前函数关联的栈帧压入到调用栈顶成为当前栈帧，同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
                    // 2. 将当前函数的局部变量压入到操作数栈顶（默认初始值为 0）
                    // 3. 将函数的字节码部分的【起始地址】设置为 pc（即下一条待执行指令的地址），即开始执行函数字节码中的指令流
                    setup_call(m, fidx);
                }
                continue;
            case CallIndirect: {
                // 指令作用：根据运行期间操作数栈顶的值调用指定函数
                // 注：在编译期只能确定被调用函数的类型（call_indirect 指令的立即数里存放的是被调用函数的类型索引），
                // 具体调用哪个函数只有在运行期间根据操作数栈顶的值才能确定

                // 第一个立即数表示被调用函数的类型索引（占 4 个字节）
                uint32_t tidx = read_LEB_unsigned(bytes, &m->pc, 32);

                // 第二个立即数为保留立即数（占 1 个比特位）
                read_LEB_unsigned(bytes, &m->pc, 1);

                // 操作数栈顶保存的值是【函数索引值】在表 table 中的索引
                uint32_t val = stack[m->sp--].value.uint32;
                // 如果该值大于或等于表 table 的最大值，则记录异常信息并返回 false 退出虚拟机执行
                if (val >= m->table.max_size) {
                    sprintf(exception, "undefined element 0x%x (max: 0x%x) in table", val, m->table.max_size);
                    return false;
                }

                // 从表 table 中读取【函数索引值】
                fidx = m->table.entries[val];

                // 如果函数索引值小于 m->import_func_count，则说明该函数为外部函数
                // 原因：在解析 Wasm 二进制文件内容到内存时，是先解析导入段中的函数到 m->functions，然后再解析函数段中的函数到 m->functions
                if (fidx < m->import_func_count) {
                    // TODO: 暂时忽略调用外部引入函数情况
                } else {
                    // 通过函数索引获取到函数
                    Block *func = &m->functions[fidx];
                    // 获取函数签名
                    Type *ftype = func->type;

                    // 如果调用栈溢出，则记录异常信息并返回 false 退出虚拟机执行
                    if (m->csp >= CALLSTACK_SIZE) {
                        sprintf(exception, "call stack exhausted");
                        return false;
                    }

                    // 如果【实际函数类型】和【指令立即数中对应的函数类型】不相同，
                    // 则记录异常信息并返回 false 退出虚拟机执行
                    if (ftype->mask != m->types[tidx].mask) {
                        sprintf(exception, "indirect call type mismatch (call type and function type differ)");
                        return false;
                    }

                    // 调用函数前的设置，主要设置内容如下：
                    // 1. 将当前函数关联的栈帧压入到调用栈顶成为当前栈帧，同时保存该栈帧被压入调用栈顶前的运行时状态，例如 sp fp ra 等
                    // 2. 将当前函数的局部变量压入到操作数栈顶（默认初始值为 0）
                    // 3. 将函数的字节码部分的【起始地址】设置为 pc（即下一条待执行指令的地址），即开始执行函数字节码中的指令流
                    setup_call(m, fidx);

                    // 由于 setup_call 函数中会将函数参数和局部变量压入操作数栈，
                    // 所以可以校验【函数签名中声明的参数数量 + 函数局部变量数量】和【压入操作数栈的函数参数和局部变量总数】是否相等，
                    // 如果不相等则记录异常信息并返回 false 退出虚拟机执行
                    if (ftype->param_count + func->local_count != m->sp - m->fp + 1) {
                        sprintf(exception, "indirect call type mismatch (param counts differ)");
                        return false;
                    }

                    // 由于 setup_call 函数中会将函数参数和局部变量压入操作数栈，
                    // 所以可以遍历【压入操作数栈的函数参数】的值，校验其类型和【函数签名中声明的参数类型】是否相等，
                    // 如果不相等则记录异常信息并返回 false 退出虚拟机执行
                    for (uint32_t n = 0; n < ftype->param_count; n++) {
                        if (ftype->params[n] != m->stack[m->fp + n].value_type) {
                            sprintf(exception, "indirect call type mismatch (param types differ)");
                            return false;
                        }
                    }
                }
                continue;
            }

            /*
             * 参数指令（2 条）
             * */
            case Drop:
                // 指令作用：丢弃操作数栈顶值
                m->sp--;
                continue;
            case Select:
                // 指令作用：从栈顶弹出 3 个操作数，根据最先弹出的操作数从其他两个操作数中选择一个压栈
                // 如果为 true，则则将最后弹出的操作数压栈；如果为 false，则将中间弹出的操作数压栈。
                // 注：最先弹出的操作数必须是 i32 类型，其他 2 个操作数数相同类型就可以

                // 最先弹出的操作数必须是 i32 类型，否则报错
                ASSERT(stack[m->sp].value_type == I32, "The type of operand stack top value need to be i32 when call select instruction \n")
                // 先从操作数栈弹出一个值作为判断条件
                cond = stack[m->sp--].value.uint32;

                // 先将次栈顶设置为栈顶，
                // 如果判断条件为 true，则将最后弹出的操作数压栈，
                // 最后弹出的操作数也就是当前的次栈顶的值，已经将其设置为栈顶值，所以后面无需再做任何操作
                m->sp--;

                // 如果判断条件为 false，则将中间弹出的操作数压栈，
                // 中间弹出的操作数压栈也就是 m->sp-- 之前的栈顶值，
                // 所以用 m->sp-- 之前的栈顶值覆盖掉  m->sp-- 之后的栈顶值即可
                if (!cond) {
                    stack[m->sp] = stack[m->sp + 1];
                }
                continue;

            /*
             * 变量指令--局部变量指令（3 条）
             * 指令作用：读写函数的参数和局部变量
             *
             * 注：每个函数关联的栈帧拥有一段操作数栈（多个函数栈帧共享同一个大的操作数栈），
             * 该函数栈帧的操作数栈的开头就存储局部变量，
             * 所以可以通过【函数栈帧的操作数栈底】加上【局部变量索引】来定位到该局部变量，即 m->fp + idx
             * */
            case LocalGet:
                // 指令作用：将指定局部变量压入到操作数栈顶

                // 该指令的立即数为局部变量的索引
                idx = read_LEB_unsigned(bytes, &m->pc, 32);

                // 将指定局部变量的值压入到操作数栈顶
                stack[++m->sp] = stack[m->fp + idx];
                continue;
            case LocalSet:
                // 指令作用：将操作数栈顶的值弹出并保存到指定局部变量中

                // 该指令的立即数为局部变量的索引
                idx = read_LEB_unsigned(bytes, &m->pc, 32);

                // 弹出操作数栈顶的值，将其保存到指定局部变量中
                stack[m->fp + idx] = stack[m->sp--];
                continue;
            case LocalTee:
                // 指令作用：将操作数栈顶值保存到指定局部变量中，但不弹出栈顶值

                // 该指令的立即数为局部变量的索引
                idx = read_LEB_unsigned(bytes, &m->pc, 32);

                // 弹出操作数栈顶的值，将其保存到指定局部变量中（注意：不弹出栈顶值）
                stack[m->fp + idx] = stack[m->sp];
                continue;

            /*
             * 变量指令--全局变量指令（2 条）
             * 指令作用：读写全局变量
             * */
            case GlobalGet:
                // 指令作用：将指定全局变量压入到操作数栈顶

                // 该指令的立即数为全局变量的索引
                idx = read_LEB_unsigned(bytes, &m->pc, 32);

                // 将指定局部变量的值压入到操作数栈顶
                stack[++m->sp] = m->globals[idx];
                continue;
            case GlobalSet:
                // 指令作用：操作数栈顶的值弹出并保存到指定全局变量中

                // 该指令的立即数为全局变量的索引
                idx = read_LEB_unsigned(bytes, &m->pc, 32);

                // 弹出操作数栈顶的值，将其保存到指定全局变量中
                m->globals[idx] = stack[m->sp--];
                continue;

            /*
             * 内存指令--内存加载指令（14 条）
             * 指令作用：从内存中加载数据，转换为适当类型的值，再压入操作数栈顶
             * */
            case I32Load ... I64Load32U:
                // 内存加载和存储指令都带有两个立即数：1.对齐方式 2.内存偏移量

                // 第一个立即数表示对齐方式
                // 保存的是以 2 为底，对齐字节数的对数，占 4 个字节
                // 例如 0 表示一字节（2^0）对齐，1 表示两字节（2^1）对齐，2 表示四字节（2^2）对齐
                // 对齐方式只起提示作用，目的是帮助 JIT/AOT 编译器生成更优化的机器代码，对实际执行结果没有任何影响，暂时忽略
                read_LEB_unsigned(bytes, &m->pc, 32);

                // 第二个立即数表示内存偏移量
                // 从操作数栈顶弹出一个 i32 类型的数，和内存偏移量 offset 相加，就可以得到实际内存相对地址
                // 注：操作数栈顶弹出的数和内存偏移量都是 32 位无符号整数，所以 Wasm 实际拥有 33 比特的地址空间
                offset = read_LEB_unsigned(bytes, &m->pc, 32);
                // 从操作数栈顶弹出一个 i32 类型的数（用于获取实际内存地址）
                addr = stack[m->sp--].value.uint32;

                // 获取实际内存地址
                maddr = m->memory.bytes + offset + addr;

                // TODO: 忽略校验 offset/addr/maddr 值的合法性

                // 将 0 作为初始值压入操作数栈顶
                stack[++m->sp].value.uint64 = 0;

                // 根据具体指令将实际内存地址里保存的数值拷贝到操作数栈顶
                switch (opcode) {
                    case I32Load:
                        // 从内存拷贝 4 个字节数到操作数栈顶（栈顶类型为 32 位整数）
                        memcpy(&stack[m->sp].value, maddr, 4);
                        stack[m->sp].value_type = I32;
                        break;

                    case I64Load:
                        // 从内存拷贝 8 个字节数到操作数栈顶（栈顶类型为 64 位整数）
                        memcpy(&stack[m->sp].value, maddr, 8);
                        stack[m->sp].value_type = I64;
                        break;
                    case F32Load:
                        // 从内存拷贝 4 个字节数到操作数栈顶（栈顶类型为 32 位浮点数）
                        memcpy(&stack[m->sp].value, maddr, 4);
                        stack[m->sp].value_type = F32;
                        break;
                    case F64Load:
                        // 从内存拷贝 8 个字节数到操作数栈顶（栈顶类型为 64 位浮点数）
                        memcpy(&stack[m->sp].value, maddr, 8);
                        stack[m->sp].value_type = F64;
                        break;
                    case I32Load8S:
                        // 从内存拷贝 1 个字节有符号数到操作数栈顶（栈顶类型为 32 位整数）
                        memcpy(&stack[m->sp].value, maddr, 1);
                        sext_8_32(&stack[m->sp].value.uint32);
                        stack[m->sp].value_type = I32;
                        break;
                    case I32Load8U:
                        // 从内存拷贝 1 个字节无符号数到操作数栈顶（栈顶类型为 32 位整数）
                        // 因为是无符号数，在转换为更大的数据类型时，只需简单地在开头添加 0 占位，无需特殊转换
                        memcpy(&stack[m->sp].value, maddr, 1);
                        stack[m->sp].value_type = I32;
                        break;
                    case I32Load16S:
                        // 从内存拷贝 2 个字节有符号数到操作数栈顶（栈顶类型为 32 位整数）
                        memcpy(&stack[m->sp].value, maddr, 2);
                        sext_16_32(&stack[m->sp].value.uint32);
                        stack[m->sp].value_type = I32;
                        break;
                    case I32Load16U:
                        // 从内存拷贝 2 个字节无符号数到操作数栈顶（栈顶类型为 32 位整数）
                        // 因为是无符号数，在转换为更大的数据类型时，只需简单地在开头添加 0 占位，无需特殊转换
                        memcpy(&stack[m->sp].value, maddr, 2);
                        stack[m->sp].value_type = I32;
                        break;
                    case I64Load8S:
                        // 从内存拷贝 1 个字节有符号数到操作数栈顶（栈顶类型为 64 位整数）
                        memcpy(&stack[m->sp].value, maddr, 1);
                        sext_8_64(&stack[m->sp].value.uint64);
                        stack[m->sp].value_type = I64;
                        break;
                    case I64Load8U:
                        // 从内存拷贝 1 个字节无符号数到操作数栈顶（栈顶类型为 64 位整数）
                        // 因为是无符号数，在转换为更大的数据类型时，只需简单地在开头添加 0 占位，无需特殊转换
                        memcpy(&stack[m->sp].value, maddr, 1);
                        stack[m->sp].value_type = I64;
                        break;
                    case I64Load16S:
                        // 从内存拷贝 2 个字节有符号数到操作数栈顶（栈顶类型为 64 位整数）
                        memcpy(&stack[m->sp].value, maddr, 2);
                        sext_16_64(&stack[m->sp].value.uint64);
                        stack[m->sp].value_type = I64;
                        break;
                    case I64Load16U:
                        // 从内存拷贝 2 个字节无符号数到操作数栈顶（栈顶类型为 64 位整数）
                        // 因为是无符号数，在转换为更大的数据类型时，只需简单地在开头添加 0 占位，无需特殊转换
                        memcpy(&stack[m->sp].value, maddr, 2);
                        stack[m->sp].value_type = I64;
                        break;
                    case I64Load32S:
                        // 从内存拷贝 4 个字节有符号数到操作数栈顶（栈顶类型为 64 位整数）
                        memcpy(&stack[m->sp].value, maddr, 4);
                        sext_32_64(&stack[m->sp].value.uint64);
                        stack[m->sp].value_type = I64;
                        break;
                    case I64Load32U:
                        // 从内存拷贝 4 个字节无符号数到操作数栈顶（栈顶类型为 64 位整数）
                        // 因为是无符号数，在转换为更大的数据类型时，只需简单地在开头添加 0 占位，无需特殊转换
                        memcpy(&stack[m->sp].value, maddr, 4);
                        stack[m->sp].value_type = I64;
                        break;
                    default:
                        break;
                }
                continue;

            /*
             * 内存指令--内存存储指令（9 条）
             * 指令作用：将操作数栈顶值弹出并存储到内存中
             * */
            case I32Store ... I64Store32:
                // 内存加载和存储指令都带有两个立即数：1.对齐方式 2.内存偏移量

                // 第一个立即数表示对齐方式
                // 保存的是以 2 为底，对齐字节数的对数，占 4 个字节
                // 例如 0 表示一字节（2^0）对齐，1 表示两字节（2^1）对齐，2 表示四字节（2^2）对齐
                // 对齐方式只起提示作用，目的是帮助 JIT/AOT 编译器生成更优化的机器代码，对实际执行结果没有任何影响，暂时忽略
                read_LEB_unsigned(bytes, &m->pc, 32);

                // 第二个立即数表示内存偏移量
                // 从操作数栈顶弹出一个 i32 类型的数，和内存偏移量 offset 相加，就可以得到实际内存相对地址
                // 注：操作数栈顶弹出的数和内存偏移量都是 32 位无符号整数，所以 Wasm 实际拥有 33 比特的地址空间
                offset = read_LEB_unsigned(bytes, &m->pc, 32);

                // 获取操作数栈顶地址，并将栈顶弹出
                StackValue *sval = &stack[m->sp--];

                // 再从操作数栈顶弹出一个 i32 类型的数（用于获取实际内存地址）
                addr = stack[m->sp--].value.uint32;
                // 获取实际内存地址
                maddr = m->memory.bytes + offset + addr;

                // TODO: 忽略校验 offset/addr/maddr 值的合法性

                // 根据具体指令将数操作数栈顶值拷贝到实际内存地址
                switch (opcode) {
                    case I32Store:
                        // 将操作数栈顶值（栈顶值类型为 32 位整数）的前 4 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.uint32, 4);
                        break;
                    case I64Store:
                        // 将操作数栈顶值（栈顶值类型为 64 位整数）的前 8 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.uint64, 8);
                        break;
                    case F32Store:
                        // 将操作数栈顶值（栈顶值类型为 32 位浮点数）的前 4 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.f32, 4);
                        break;
                    case F64Store:
                        // 将操作数栈顶值（栈顶值类型为 64 位浮点数）的前 8 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.f64, 8);
                        break;
                    case I32Store8:
                        // 将操作数栈顶值（栈顶值类型为 32 位整数）的前 1 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.uint32, 1);
                        break;
                    case I32Store16:
                        // 将操作数栈顶值（栈顶值类型为 32 位整数）的前 2 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.uint32, 2);
                        break;
                    case I64Store8:
                        // 将操作数栈顶值（栈顶值类型为 64 位整数）的前 1 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.uint64, 1);
                        break;
                    case I64Store16:
                        // 将操作数栈顶值（栈顶值类型为 64 位整数）的前 2 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.uint64, 2);
                        break;
                    case I64Store32:
                        // 将操作数栈顶值（栈顶值类型为 64 位整数）的前 4 个字节拷贝到实际内存地址
                        memcpy(maddr, &sval->value.uint64, 4);
                        break;
                    default:
                        break;
                }
                continue;

            /*
             * 内存指令--size 指令
             * */
            case MemorySize:
                // 指令作用：将当前的内存页数以 i32 类型压入操作数栈顶

                // 该指令的立即数表示当前操作的是第几块内存（占 1 个内存）
                // 但由于当前 Wasm 规范规定最多只能导入或定义一块内存，所以目前必须为 0
                read_LEB_unsigned(bytes, &m->pc, 32);

                // 将当前的内存页数以 i32 类型压入操作数栈顶
                stack[++m->sp].value_type = I32;
                stack[m->sp].value.uint32 = m->memory.cur_size;
                continue;

            /*
             * 内存指令--grow 指令
             * */
            case MemoryGrow:
                // 指令作用：将内存增长若干页，并从操作数栈顶获取增长前的内存页数

                // 该指令的立即数表示当前操作的是第几块内存（占 1 个内存）
                // 但由于当前 Wasm 规范规定最多只能导入或定义一块内存，所以目前必须为 0
                read_LEB_unsigned(bytes, &m->pc, 32);

                // 先保存当前内存页数
                uint32_t prev_pages = m->memory.cur_size;

                // 将操作数栈顶值作为内存要增长的页数
                uint32_t delta = stack[m->sp].value.uint32;

                // 用刚刚保存的当前内存页数覆盖当前操作数栈顶值
                stack[m->sp].value.uint32 = prev_pages;

                // 校验内存增长页数是否合法
                if (delta == 0 || delta + prev_pages > m->memory.max_size) {
                    // 如果内存增长页数为 0，
                    // 或者内存增长页数加上当前内存页数后，超过了内存最大页数，
                    // 则什么都不做，执行下一条指令
                    continue;
                }

                // 如果内存增长页数合法，则增加 delta 页内存
                m->memory.cur_size += delta;
                m->memory.bytes = arecalloc(m->memory.bytes, prev_pages * PAGE_SIZE, m->memory.cur_size * PAGE_SIZE, sizeof(uint8_t), "Module->memory.bytes");
                continue;

            /*
             * 数值指令--常量指令（4 条）
             * 
             * 注：数值指令中除了常量指令之外，其余的数值指令都没有立即数
             * */
            case I32Const:
                // 指令作用：将指令的立即数以 i32 类型压入操作数栈顶

                stack[++m->sp].value_type = I32;
                stack[m->sp].value.uint32 = read_LEB_signed(bytes, &m->pc, 32);
                continue;
            case I64Const:
                // 指令作用：将指令的立即数以 i64 类型压入操作数栈顶

                stack[++m->sp].value_type = I64;
                stack[m->sp].value.int64 = (int64_t) read_LEB_signed(bytes, &m->pc, 64);
                continue;
            case F32Const:
                // 指令作用：将指令的立即数以 f32 类型压入操作数栈顶

                stack[++m->sp].value_type = F32;
                // LEB128 编码仅针对整数，而该指令的立即数为浮点数，并没有被编码，而是直接写入到 Wasm 二进制文件中的
                memcpy(&stack[m->sp].value.uint32, bytes + m->pc, 4);
                // 由于是直接将 4 个字节长度的立即数的值拷贝到栈顶，
                // 没有调用 read_LEB_signed（该函数会实时更新 pc 保存的值），所以程序计数器需要手动加 4
                m->pc += 4;
                continue;
            case F64Const:
                // 指令作用：将指令的立即数以 f64 类型压入操作数栈顶

                stack[++m->sp].value_type = F64;
                // LEB128 编码仅针对整数，而该指令的立即数为浮点数，并没有被编码，而是直接写入到 Wasm 二进制文件中的
                memcpy(&stack[m->sp].value.uint64, bytes + m->pc, 8);
                // 由于是直接将 8 个字节长度的立即数的值拷贝到栈顶，
                // 没有调用 read_LEB_signed（该函数会实时更新 pc 保存的值），所以程序计数器需要手动加 8
                m->pc += 8;
                continue;

            /*
             * 数值指令--测试指令（2 条）
             *
             * 注：测试指令是冗余的，完全可用常量指令和比较指令代替，
             * 但是考虑到判断一个数是否为 0 是一种相当常见的操作，使用测试指令可以节约一条常量指令
             * */
            case I32Eqz:
                // 指令作用：判断操作数栈顶值（32 位整数）是否为 0

                // 获取栈顶操作数栈顶值（32 位整数），判断是否为 0，
                // 然后用判断结果（i32 类型的布尔值）覆盖当前操作数栈顶值
                stack[m->sp].value_type = I32;
                stack[m->sp].value.uint32 = stack[m->sp].value.uint32 == 0;
                continue;
            case I64Eqz:
                // 指令作用：判断操作数栈顶值（64 位整数）是否为 0

                // 获取栈顶操作数值（64 位整数），判断是否为 0，
                // 然后用判断结果（i32 类型的布尔值）覆盖当前操作数栈顶值
                stack[m->sp].value_type = I32;
                stack[m->sp].value.uint32 = stack[m->sp].value.uint64 == 0;
                continue;

            /*
             * 数值指令--比较指令（32 条）
             * */
            case I32Eq ... I32GeU:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（32 位整数），根据具体指令对两个值进行比较，并用比较结果覆盖当前操作数栈顶值

                a = stack[m->sp - 1].value.uint32;
                b = stack[m->sp].value.uint32;
                m->sp -= 1;
                switch (opcode) {
                    case I32Eq:
                        c = a == b;
                        break;
                    case I32Ne:
                        c = a != b;
                        break;
                    case I32LtS:
                        c = (uint32_t) a < (uint32_t) b;
                        break;
                    case I32LtU:
                        c = a < b;
                        break;
                    case I32GtS:
                        c = (uint32_t) a > (uint32_t) b;
                        break;
                    case I32GtU:
                        c = a > b;
                        break;
                    case I32LeS:
                        c = (uint32_t) a <= (uint32_t) b;
                        break;
                    case I32LeU:
                        c = a <= b;
                        break;
                    case I32GeS:
                        c = (uint32_t) a >= (uint32_t) b;
                        break;
                    case I32GeU:
                        c = a >= b;
                        break;
                    default:
                        break;
                }
                // 注：比较的结果为布尔值，用 32 位整数表示
                stack[m->sp].value_type = I32;
                stack[m->sp].value.uint32 = c;
                continue;
            case I64Eq ... I64GeU:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（64 位整数），根据具体指令对两个值进行比较，并用比较结果覆盖当前操作数栈顶值

                d = stack[m->sp - 1].value.uint64;
                e = stack[m->sp].value.uint64;
                m->sp -= 1;
                switch (opcode) {
                    case I64Eq:
                        c = d == e;
                        break;
                    case I64Ne:
                        c = d != e;
                        break;
                    case I64LtS:
                        c = (uint64_t) d < (uint64_t) e;
                        break;
                    case I64LtU:
                        c = d < e;
                        break;
                    case I64GtS:
                        c = (uint64_t) d > (uint64_t) e;
                        break;
                    case I64GtU:
                        c = d > e;
                        break;
                    case I64LeS:
                        c = (uint64_t) d <= (uint64_t) e;
                        break;
                    case I64LeU:
                        c = d <= e;
                        break;
                    case I64GeS:
                        c = (uint64_t) d >= (uint64_t) e;
                        break;
                    case I64GeU:
                        c = d >= e;
                        break;
                    default:
                        break;
                }
                // 注：比较的结果为布尔值，用 32 位整数表示
                stack[m->sp].value_type = I32;
                stack[m->sp].value.uint32 = c;
                continue;
            case F32Eq ... F32Ge:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（32 位浮点数），根据具体指令对两个值进行比较，并用比较结果覆盖当前操作数栈顶值

                g = stack[m->sp - 1].value.f32;
                h = stack[m->sp].value.f32;
                m->sp -= 1;
                switch (opcode) {
                    case F32Eq:
                        c = g == h;
                        break;
                    case F32Ne:
                        c = g != h;
                        break;
                    case F32Lt:
                        c = g < h;
                        break;
                    case F32Gt:
                        c = g > h;
                        break;
                    case F32Le:
                        c = g <= h;
                        break;
                    case F32Ge:
                        c = g >= h;
                        break;
                    default:
                        break;
                }
                // 注：比较的结果为布尔值，用 32 位整数表示
                stack[m->sp].value_type = I32;
                stack[m->sp].value.uint32 = c;
                continue;
            case F64Eq ... F64Ge:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（64 位浮点数），根据具体指令对两个值进行比较，并用比较结果覆盖当前操作数栈顶值

                j = stack[m->sp - 1].value.f64;
                k = stack[m->sp].value.f64;
                m->sp -= 1;
                switch (opcode) {
                    case F64Eq:
                        c = j == k;
                        break;
                    case F64Ne:
                        c = j != k;
                        break;
                    case F64Lt:
                        c = j < k;
                        break;
                    case F64Gt:
                        c = j > k;
                        break;
                    case F64Le:
                        c = j <= k;
                        break;
                    case F64Ge:
                        c = j >= k;
                        break;
                    default:
                        break;
                }
                // 注：比较的结果为布尔值，用 32 位整数表示
                stack[m->sp].value_type = I32;
                stack[m->sp].value.uint32 = c;
                continue;

            /*
             * 数值指令--算术指令（64 条）
             * */
            case I32Clz ... I32PopCnt:
                // 指令作用：获取操作数栈顶值（32 位整数），根据指令对其进行相应计算，并用计算结果覆盖当前操作数栈顶值

                a = stack[m->sp].value.uint32;
                switch (opcode) {
                    case I32Clz:
                        // 数值的二进制表示的位数
                        c = a == 0 ? 32 : __builtin_clz(a);
                        break;
                    case I32Ctz:
                        // 数值的二进制表示的末尾后面 0 的个数
                        c = a == 0 ? 32 : __builtin_ctz(a);
                        break;
                    case I32PopCnt:
                        // 数值的二进制表示中的 1 的个数
                        c = __builtin_popcount(a);
                        break;
                    default:
                        break;
                }

                stack[m->sp].value.uint32 = c;
                continue;
            case I32Add ... I32Rotr:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（32 位整数），根据具体指令对两个值进行计算，并用计算结果覆盖当前操作数栈顶值

                a = stack[m->sp - 1].value.uint32;
                b = stack[m->sp].value.uint32;
                m->sp -= 1;

                // 执行 I32DivS 和 I32RemU 之间的指令时，栈顶值 b 不能为 0，
                // 如果为 0 则记录异常信息并返回 false 退出虚拟机执行
                if (opcode >= I32DivS && opcode <= I32RemU && b == 0) {
                    sprintf(exception, "integer divide by zero");
                    return false;
                }

                switch (opcode) {
                    case I32Add:
                        // 加法
                        c = a + b;
                        break;
                    case I32Sub:
                        // 减法
                        c = a - b;
                        break;
                    case I32Mul:
                        // 乘法
                        c = a * b;
                        break;
                    case I32DivS:
                        // 除法（有符号）
                        if (a == 0x80000000 && b == -1) {
                            sprintf(exception, "integer overflow");
                            return false;
                        }
                        c = (int32_t) a / (int32_t) b;
                        break;
                    case I32DivU:
                        // 除法（无符号）
                        c = a / b;
                        break;
                    case I32RemS:
                        // 取余（有符号）
                        if (a == 0x80000000 && b == -1) {
                            c = 0;
                        } else {
                            c = (int32_t) a % (int32_t) b;
                        }
                        break;
                    case I32RemU:
                        // 取余（无符号）
                        c = a % b;
                        break;
                    case I32And:
                        // 与
                        c = a & b;
                        break;
                    case I32Or:
                        // 或
                        c = a | b;
                        break;
                    case I32Xor:
                        // 异或
                        c = a ^ b;
                        break;
                    case I32Shl:
                        // 左移
                        c = a << b;
                        break;
                    case I32ShrS:
                        // 右移
                        c = ((int32_t) a) >> b;
                        break;
                    case I32ShrU:
                        // 右移
                        c = a >> b;
                        break;
                    case I32Rotl:
                        // 循环左移
                        c = rotl32(a, b);
                        break;
                    case I32Rotr:
                        // 循环右移
                        c = rotr32(a, b);
                        break;
                    default:
                        break;
                }

                stack[m->sp].value.uint32 = c;
                continue;
            case I64Clz ... I64PopCnt:
                // 指令作用：获取操作数栈顶值（64 位整数），根据指令对其进行相应计算，并用计算结果覆盖当前操作数栈顶值

                d = stack[m->sp].value.uint64;

                switch (opcode) {
                    case I64Clz:
                        // 数值的二进制表示的位数
                        f = d == 0 ? 64 : __builtin_clzll(d);
                        break;
                    case I64Ctz:
                        // 数值的二进制表示的末尾后面 0 的个数
                        f = d == 0 ? 64 : __builtin_ctzll(d);
                        break;
                    case I64PopCnt:
                        // 数值的二进制表示中的 1 的个数
                        f = __builtin_popcountll(d);
                        break;
                    default:
                        break;
                }

                stack[m->sp].value.uint64 = f;
                continue;
            case I64Add ... I64Rotr:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（64 位整数），根据具体指令对两个值进行计算，并用计算结果覆盖当前操作数栈顶值

                d = stack[m->sp - 1].value.uint64;
                e = stack[m->sp].value.uint64;
                m->sp -= 1;

                // 执行 I64DivS 和 I64RemU 之间的指令时，栈顶值 e 不能为 0，
                // 如果为 0 则记录异常信息并返回 false 退出虚拟机执行
                if (opcode >= I64DivS && opcode <= I64RemU && e == 0) {
                    sprintf(exception, "integer divide by zero");
                    return false;
                }

                switch (opcode) {
                    case I64Add:
                        // 加法
                        f = d + e;
                        break;
                    case I64Sub:
                        // 减法
                        f = d - e;
                        break;
                    case I64Mul:
                        // 乘法
                        f = d * e;
                        break;
                    case I64DivS:
                        // 除法（有符号）
                        if (d == 0x80000000 && e == -1) {
                            sprintf(exception, "integer overflow");
                            return false;
                        }
                        f = (int64_t) d / (int64_t) e;
                        break;
                    case I64DivU:
                        // 除法（无符号）
                        f = d / e;
                        break;
                    case I64RemS:
                        // 取余（有符号）
                        if (d == 0x80000000 && e == -1) {
                            f = 0;
                        } else {
                            f = (int64_t) d % (int64_t) e;
                        }
                        break;
                    case I64RemU:
                        // 取余（无符号）
                        f = d % e;
                        break;
                    case I64And:
                        // 与
                        f = d & e;
                        break;
                    case I64Or:
                        // 或
                        f = d | e;
                        break;
                    case I64Xor:
                        // 异或
                        f = d ^ e;
                        break;
                    case I64Shl:
                        // 左移
                        f = d << e;
                        break;
                    case I64ShrS:
                        // 右移
                        f = ((int64_t) d) >> e;
                        break;
                    case I64ShrU:
                        // 右移
                        f = d >> e;
                        break;
                    case I64Rotl:
                        // 循环左移
                        f = rotl64(d, e);
                        break;
                    case I64Rotr:
                        // 循环右移
                        f = rotr64(d, e);
                        break;
                    default:
                        break;
                }

                stack[m->sp].value.uint64 = f;
                continue;
            case F32Abs:
                // 取绝对值（32 位浮点型）
                stack[m->sp].value.f32 = fabsf(stack[m->sp].value.f32);
                continue;
            case F32Neg:
                // 取反（32 位浮点型）
                stack[m->sp].value.f32 = -stack[m->sp].value.f32;
                continue;
            case F32Ceil:
                // 获取大于或等于操作数栈顶值的最小的整数值（32 位浮点型）
                stack[m->sp].value.f32 = ceilf(stack[m->sp].value.f32);
                continue;
            case F32Floor:
                // 获取小于或等于操作数栈顶值的最小的整数值（32 位浮点型）
                stack[m->sp].value.f32 = floorf(stack[m->sp].value.f32);
                continue;
            case F32Trunc:
                // 将小数部分截去，保留整数（32 位浮点型）
                stack[m->sp].value.f32 = truncf(stack[m->sp].value.f32);
                continue;
            case F32Nearest:
                // 获取最接近操作数栈顶值的整数，如果有 2 个数同样接近，则取偶数的整数（32 位浮点型）
                stack[m->sp].value.f32 = rintf(stack[m->sp].value.f32);
                continue;
            case F32Sqrt:
                // 取平方根（32 位浮点型）
                stack[m->sp].value.f32 = sqrtf(stack[m->sp].value.f32);
                continue;
            case F32Add ... F32CopySign:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（32 位浮点数），根据具体指令对两个值进行计算，并用计算结果覆盖当前操作数栈顶值

                g = stack[m->sp - 1].value.f32;
                h = stack[m->sp].value.f32;
                m->sp -= 1;

                switch (opcode) {
                    case F32Add:
                        // 加法
                        i = g + h;
                        break;
                    case F32Sub:
                        // 减法
                        i = g - h;
                        break;
                    case F32Mul:
                        // 乘法
                        i = g * h;
                        break;
                    case F32Div:
                        // 除法
                        if (h == 0) {
                            sprintf(exception, "integer divide by zero");
                            return false;
                        }
                        i = g / h;
                        break;
                    case F32Min:
                        // 取两者之间的最小值
                        i = wa_fminf(g, h);
                        break;
                    case F32Max:
                        // 取两者之间的最大值
                        i = wa_fmaxf(g, h);
                        break;
                    case F32CopySign:
                        // 获取带有第二个浮点数符号的第一个浮点数
                        // 注：signbit 函数用于判断参数的符号位的正负，为负的时候返回 true，否则返回 false
                        i = signbit(h) ? -fabsf(g) : fabsf(g);
                        break;
                    default:
                        break;
                }

                stack[m->sp].value.f32 = i;
                continue;
            case F64Abs:
                // 取绝对值（64 位浮点型）
                stack[m->sp].value.f32 = (float) fabs(stack[m->sp].value.f64);
                continue;
            case F64Neg:
                // 取反（64 位浮点型）
                stack[m->sp].value.f64 = -stack[m->sp].value.f64;
                continue;
            case F64Ceil:
                // 获取大于或等于操作数栈顶值的最小的整数值（64 位浮点型）
                stack[m->sp].value.f64 = ceil(stack[m->sp].value.f64);
                continue;
            case F64Floor:
                // 获取小于或等于操作数栈顶值的最小的整数值（64 位浮点型）
                stack[m->sp].value.f64 = floor(stack[m->sp].value.f64);
                continue;
            case F64Trunc:
                // 将小数部分截去，保留整数（64 位浮点型）
                stack[m->sp].value.f64 = trunc(stack[m->sp].value.f64);
                continue;
            case F64Nearest:
                // 获取最接近操作数栈顶值的整数，如果有 2 个数同样接近，则取偶数的整数（64 位浮点型）
                stack[m->sp].value.f64 = rint(stack[m->sp].value.f64);
                continue;
            case F64Sqrt:
                // 取平方根（64 位浮点型）
                stack[m->sp].value.f64 = sqrt(stack[m->sp].value.f64);
                continue;
            case F64Add ... F64CopySign:
                // 指令作用：获取操作数栈的栈顶和次栈顶的值（64 位浮点数），根据具体指令对两个值进行计算，并用计算结果覆盖当前操作数栈顶值

                j = stack[m->sp - 1].value.f64;
                k = stack[m->sp].value.f64;
                m->sp -= 1;

                switch (opcode) {
                    case F64Add:
                        // 加法
                        l = j + k;
                        break;
                    case F64Sub:
                        // 减法
                        l = j - k;
                        break;
                    case F64Mul:
                        // 乘法
                        l = j * k;
                        break;
                    case F64Div:
                        // 除法
                        if (k == 0) {
                            sprintf(exception, "integer divide by zero");
                            return false;
                        }
                        l = j / k;
                        break;
                    case F64Min:
                        // 取两者之间的最小值
                        l = wa_fmin(j, k);
                        break;
                    case F64Max:
                        // 取两者之间的最大值
                        l = wa_fmax(j, k);
                        break;
                    case F64CopySign:
                        // 获取带有第二个浮点数符号的第一个浮点数
                        // 注：signbit 函数用于判断参数的符号位的正负，为负的时候返回 true，否则返回 false
                        l = signbit(k) ? -fabs(j) : fabs(j);
                        break;
                    default:
                        break;
                }

                stack[m->sp].value.f64 = l;
                continue;

            /*
             * 数值指令--类型转换指令（31 条）
             *
             * 注：类型转换指令的助记符是 t'.conv_t，
             * 其中操作数在类型转换之前的类型是 t，之后的类型是 t'，转换操作是 conv
             * */
            case I32WrapI64:
                // 指令作用：将 64 位整数截断为 32 位整数
                stack[m->sp].value.uint64 &= 0x00000000ffffffff;
                stack[m->sp].value_type = I32;
                continue;
            case I32TruncF32S:
                // 指令作用：将 32 位浮点数截断为 32 有符号位整数（截掉小数部分）
                OP_I32_TRUNC_F32(stack[m->sp].value.int32, stack[m->sp].value.f32)
                stack[m->sp].value_type = I32;
                continue;
            case I32TruncF32U:
                // 指令作用：将 32 位浮点数截断为 32 位无符号整数（截掉小数部分）
                OP_U32_TRUNC_F32(stack[m->sp].value.uint32, stack[m->sp].value.f32)
                stack[m->sp].value_type = I32;
                continue;
            case I32TruncF64S:
                // 指令作用：将 64 位浮点数截断为 32 位有符号整数（截掉小数部分）
                OP_I32_TRUNC_F64(stack[m->sp].value.int32, stack[m->sp].value.f64)
                stack[m->sp].value_type = I32;
                continue;
            case I32TruncF64U:
                // 指令作用：将 64 位浮点数截断为 32 位无符号整数（截掉小数部分）
                OP_U32_TRUNC_F64(stack[m->sp].value.uint32, stack[m->sp].value.f64)
                stack[m->sp].value_type = I32;
                continue;
            case I64ExtendI32S:
                // 指令作用：将 32 位有符号整数位数拉升为 64 位整数
                stack[m->sp].value.uint64 = stack[m->sp].value.uint32;
                sext_32_64(&stack[m->sp].value.uint64);
                stack[m->sp].value_type = I64;
                continue;
            case I64ExtendI32U:
                // 指令作用：将 32 位无符号整数位数拉升为 64 位整数
                stack[m->sp].value.uint64 = stack[m->sp].value.uint32;
                stack[m->sp].value_type = I64;
                continue;
            case I64TruncF32S:
                // 指令作用：将 32 位浮点数截断为 64 位有符号整数（截掉小数部分）
                OP_I64_TRUNC_F32(stack[m->sp].value.int64, stack[m->sp].value.f32)
                stack[m->sp].value_type = I64;
                continue;
            case I64TruncF32U:
                // 指令作用：将 32 位浮点数截断为 64 位无符号整数（截掉小数部分）
                OP_U64_TRUNC_F32(stack[m->sp].value.uint64, stack[m->sp].value.f32)
                stack[m->sp].value_type = I64;
                continue;
            case I64TruncF64S:
                // 指令作用：将 64 位浮点数截断为 64 位有符号整数（截掉小数部分）
                OP_I64_TRUNC_F64(stack[m->sp].value.int64, stack[m->sp].value.f64)
                stack[m->sp].value_type = I64;
                continue;
            case I64TruncF64U:
                // 指令作用：将 64 位无符号浮点数截断为 64 位无符号整数（截掉小数部分）
                OP_U64_TRUNC_F64(stack[m->sp].value.uint64, stack[m->sp].value.f64)
                stack[m->sp].value_type = I64;
                continue;
            case F32ConvertI32S:
                // 指令作用：将 32 位有符号整数转化为 32 位浮点数
                stack[m->sp].value.f32 = (float) stack[m->sp].value.int32;
                stack[m->sp].value_type = F32;
                continue;
            case F32ConvertI32U:
                // 指令作用：将 32 位无符号整数转化为 32 位浮点数
                stack[m->sp].value.f32 = (float) stack[m->sp].value.uint32;
                stack[m->sp].value_type = F32;
                continue;
            case F32ConvertI64S:
                // 指令作用：将 64 位有符号整数转化为 32 位浮点数
                stack[m->sp].value.f32 = (float) stack[m->sp].value.int64;
                stack[m->sp].value_type = F32;
                continue;
            case F32ConvertI64U:
                // 指令作用：将 64 位无符号整数转化为 32 位浮点数
                stack[m->sp].value.f32 = (float) stack[m->sp].value.uint64;
                stack[m->sp].value_type = F32;
                continue;
            case F32DemoteF64:
                // 指令作用：将 64 位浮点数精度降低到 32 位
                stack[m->sp].value.f32 = (float) stack[m->sp].value.f64;
                stack[m->sp].value_type = F32;
                continue;
            case F64ConvertI32S:
                // 指令作用：将 32 位有符号整数转化为 64 位浮点数
                stack[m->sp].value.f64 = stack[m->sp].value.int32;
                stack[m->sp].value_type = F64;
                continue;
            case F64ConvertI32U:
                // 指令作用：将 32 位无符号整数转化为 64 位浮点数
                stack[m->sp].value.f64 = stack[m->sp].value.uint32;
                stack[m->sp].value_type = F64;
                continue;
            case F64ConvertI64S:
                // 指令作用：将 64 位有符号整数转化为 64 位浮点数
                stack[m->sp].value.f64 = (double) stack[m->sp].value.int64;
                stack[m->sp].value_type = F64;
                continue;
            case F64ConvertI64U:
                // 指令作用：将 64 位无符号整数转化为 64 位浮点数
                stack[m->sp].value.f64 = (double) stack[m->sp].value.uint64;
                stack[m->sp].value_type = F64;
                continue;
            case F64PromoteF32:
                // 指令作用：将 32 位浮点数精度提升到 64 位
                stack[m->sp].value.f64 = stack[m->sp].value.f32;
                stack[m->sp].value_type = F64;
                continue;
            case I32ReinterpretF32:
                // 指令作用：将 64 位浮点数重新解释为 32 位整数类型，但不改变比特位
                stack[m->sp].value_type = I32;
                continue;
            case I64ReinterpretF64:
                // 指令作用：将 64 位浮点数重新解释为 64 位整数类型，但不改变比特位
                stack[m->sp].value_type = I64;
                continue;
            case F32ReinterpretI32:
                // 指令作用：将 32 位整数重新解释为 32 位浮点数类型，但不改变比特位
                stack[m->sp].value_type = F32;
                continue;
            case F64ReinterpretI64:
                // 指令作用：将 64 位整数重新解释为 64 位浮点数类型，但不改变比特位
                stack[m->sp].value_type = F64;
                continue;
            case I32Extend8S:
                // 指令作用：将 8 位有符号整数位数拉升为 32 位整数
                stack[m->sp].value.int32 = ((int32_t) (int8_t) stack[m->sp].value.int32);
                continue;
            case I32Extend16S:
                // 指令作用：将 16 位有符号整数位数拉升为 32 位整数
                stack[m->sp].value.int32 = ((int32_t) (int16_t) stack[m->sp].value.int32);
                continue;
            case I64Extend8S:
                // 指令作用：将 8 位有符号整数位数拉升为 64 位整数
                stack[m->sp].value.int64 = ((int64_t) (int8_t) stack[m->sp].value.int64);
                continue;
            case I64Extend16S:
                // 指令作用：将 16 位有符号整数位数拉升为 64 位整数
                stack[m->sp].value.int64 = ((int64_t) (int16_t) stack[m->sp].value.int64);
                continue;
            case I64Extend32S:
                // 指令作用：将 32 位有符号整数位数拉升为 64 位整数
                stack[m->sp].value.int64 = ((int64_t) (int32_t) stack[m->sp].value.int64);
                continue;
            case TruncSat: {
                // 饱和截断指令
                // Wasm 支持的 4 种基本类型都是固定长度：i32 和 f32 类型占 4 字节，i64 和 f64 类型占 8 字节
                // 定长的数据类型只能表达有限的数值，因此对 2 个某种类型的数进行计算，其结果可能会超出该类型的表达范围，也就是溢出：包括上溢和下溢
                // 针对溢出有 3 种处理方式：
                // 1. 环绕（Wrapping），整数运算通常采用这种方式。以 u32 类型为例，0xfffffffd 和 0x04 相加导致溢出，结果为 0x01
                // 2. 饱和（Saturation），浮点数运算通常采用这种方式，超出范围的值会被表示为正或负“无穷”（+Inf / -Inf）
                // 3. 异常，例如整数除 0 通常会产生异常
                // 截断指令需要将浮点数截断为整数，所以可能会产生溢出，或者无法转换（比如 NaN）情况

                // 新增的 8 条饱和截断指令和上面的 8 条非饱和截断指令是一一对应的，只是对于异常情况做了特殊处理，
                // 比如将 NaN 转换为 0。再比如如果超出了类型能表达的范围，让该变量等于一个最大值或者最小值。
                // 这 8 条指令是通过一条特殊的操作码前缀 0xFC 引入的，操作码前缀 0xFC 未来可能会用来增加其他指令。
                // 为了保持统一，我们仍将 0xFC 作为一个普通操作码，将跟在它后面的字节当作它的立即数，这样就可以认为只有一条饱和截断指令

                // 在读取一个字节，用来区分不同类型的浮点数和整数之间的转换
                uint8_t type = read_LEB_unsigned(bytes, &m->pc, 8);
                switch (type) {
                    case 0x00:
                        // 指令作用：将 32 位浮点数饱和截断为 32 有符号位整数（截掉小数部分）
                        OP_I32_TRUNC_SAT_F32(stack[m->sp].value.int32, stack[m->sp].value.f32)
                        stack[m->sp].value_type = I32;
                        break;
                    case 0x01:
                        // 指令作用：将 32 位浮点数截断为 32 位无符号整数（截掉小数部分）
                        OP_U32_TRUNC_SAT_F32(stack[m->sp].value.uint32, stack[m->sp].value.f32)
                        stack[m->sp].value_type = I32;
                        break;
                    case 0x02:
                        // 指令作用：将 64 位浮点数截断为 32 位有符号整数（截掉小数部分）
                        OP_I32_TRUNC_SAT_F64(stack[m->sp].value.int32, stack[m->sp].value.f64)
                        stack[m->sp].value_type = I32;
                        break;
                    case 0x03:
                        // 指令作用：将 64 位浮点数截断为 32 位无符号整数（截掉小数部分）
                        OP_U32_TRUNC_SAT_F64(stack[m->sp].value.uint32, stack[m->sp].value.f64)
                        stack[m->sp].value_type = I32;
                        break;
                    case 0x04:
                        // 指令作用：将 32 位浮点数截断为 64 位有符号整数（截掉小数部分）
                        OP_I64_TRUNC_SAT_F32(stack[m->sp].value.int64, stack[m->sp].value.f32)
                        stack[m->sp].value_type = I64;
                        break;
                    case 0x05:
                        // 指令作用：将 32 位浮点数截断为 64 位无符号整数（截掉小数部分）
                        OP_U64_TRUNC_SAT_F32(stack[m->sp].value.uint64, stack[m->sp].value.f32)
                        stack[m->sp].value_type = I64;
                        break;
                    case 0x06:
                        // 指令作用：将 64 位浮点数截断为 64 位有符号整数（截掉小数部分）
                        OP_I64_TRUNC_SAT_F64(stack[m->sp].value.int64, stack[m->sp].value.f64)
                        stack[m->sp].value_type = I64;
                        break;
                    case 0x07:
                        // 指令作用：将 64 位无符号浮点数截断为 64 位无符号整数（截掉小数部分）
                        OP_U64_TRUNC_SAT_F64(stack[m->sp].value.uint64, stack[m->sp].value.f64)
                        stack[m->sp].value_type = I64;
                        break;
                    default:
                        break;
                }
                continue;
            }
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
    ASSERT(m->stack[m->sp].value_type == type, "Init_expr type mismatch 0x%x != 0x%x\n", m->stack[m->sp].value_type, type)
}
