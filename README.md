# wasmc

[中文文档](https://github.com/mcuking/wasmc/blob/master/README_zh-CN.md)

A WebAssembly interpreter written in C for demonstration.

This repository implements a WebAssembly interpreter. It is written to clarify how a WebAssembly interpreter works and try out the latest proposal. So the code is easy to understand, and there are lots of code comments to explain the intention behind the code.

## Build

You can get the executable via Makefile or CMake.

Makefile:

```sh
make
```

CMake:

```sh
cmake ./
make
```

## Usage

You can call the executable with

```sh
[wasmc executable path] [wasm file path]
```

Wasmc loads the wasm file and return a REPL(read-eval-print-loop). You can invoke some exported function of the wasm file as shown below.

<img src="https://i.loli.net/2021/08/06/XNqoMYnQplBh8JV.png" width=600/>

> **Note:** the interpreter now only supports the wasm file compiled from wat file.

## Examples

Here is description for `./examples`.

| File                | Description                                  |
|---------------------|----------------------------------------------|
| ./examples/arith.wasm | Export add/sub/mul/div_u function with two i32 type of parameters |
| ./examples/fib.wasm | Export recursive fibonacci function with an i32 type of parameter |
| ./examples/fac.wasm | Export recursive factorial function with an i32 type of parameter|

## Implementation

Here are core modules.

```sh
├── cli.c          // the entry of interpreter
├── module.c       // decode from binary format to memory format
├── interpreter.c  // stack based virtual machine 
├── opcode.h       // webassembly opcode enum
└── utils.c        // utility libraries
```

Learn more via article below.

[深入 WebAssembly 之解释器实现篇](https://github.com/mcuking/blog/issues/96)

## Inspiration

- [https://github.com/kanaka/wac](https://github.com/kanaka/wac)

- [WebAssembly 原理与核心技术](https://book.douban.com/subject/35233448/)

- [WebAssembly 实战](https://book.douban.com/subject/35459649/)
