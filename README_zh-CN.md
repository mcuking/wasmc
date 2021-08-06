# wasmc

一个使用 C 语言实现的演示版本的 WebAssembly 解释器

本仓库实现了一个 WebAssembly 解释器。编写该解释器的目的是阐述 WebAssembly 解释器是如何工作的，以及后续尝试实现最新的提案。所以源码非常易懂，而且会有丰富的注释来解释代码背后的意图。

## 构建

支持使用 Makefile 或者 CMake 构建获得可执行文件。

Makefile:

```sh
make
```

CMake:

```sh
cmake ./
make
```

## 使用

按照下方式调用可执行文件

```sh
// 第一个参数可执行文件 wasmc 的路径
// 第二个参数是需要被解释执行的 wasm 文件路径

[wasmc executable path] [wasm file path]
```

wasmc 加载 wasm 文件后，会返回一个交互式解释器 REPL(read-eval-print-loop)。可以如下图所示在其中调用 wasm 文件导出的函数。

<img src="https://i.loli.net/2021/08/06/XNqoMYnQplBh8JV.png" width=600/>

> **Note:** 目前解释器仅支持解释执行从 wat 文件编译得到的 wasm 文件

## 示例

下面是针对 `./examples` 下文件的描述：

| File                | Description                                  |
|---------------------|----------------------------------------------|
| ./examples/arith.wasm | 导出参数为 i32 类型的加减乘除函数 |
| ./examples/fib.wasm | 导出参数为 i32 类型的基于递归实现的斐波那契函数 |
| ./examples/fac.wasm | 导出参数为 i32 类型的基于递归实现的阶乘函数|

## 实现原理

下面是核心模块：

```sh
├── cli.c          // 解释器入口
├── module.c       // 解码二进制格式到内存格式
├── interpreter.c  // 栈式虚拟机
├── opcode.h       // webassembly 操作码枚举
└── utils.c        // 公共方法
```

可以通过下面文章了解更详细的实现原理阐述：

[深入 WebAssembly 之解释器实现篇](https://github.com/mcuking/blog/issues/96)

## 参考资料

- [https://github.com/kanaka/wac](https://github.com/kanaka/wac)

- [WebAssembly 原理与核心技术](https://book.douban.com/subject/35233448/)

- [WebAssembly 实战](https://book.douban.com/subject/35459649/)
