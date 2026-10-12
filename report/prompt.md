[PROMPT]
任务：完成 Lab1最小可执行内核的构建、启动和调试验证。阅读当前项目中的实验文档和源代码，确认内核能够被交叉编译、链接并生成 QEMU 可运行的镜像；使用 QEMU 启动内核，并使用 RISC-V GDB 跟踪 CPU 从复位地址 0x1000 到内核入口 0x80200000 的过程。

操作要求：你必须在当前项目中执行实际的编译、运行和调试命令，并根据真实输出判断结果。可以在确有必要时修改 Makefile 的 QEMU 启动参数以适配当前 QEMU/OpenSBI 版本，但不要修改实验核心代码或引入与本实验无关的功能。所有关键地址、寄存器值和函数位置都必须通过 QEMU、GDB 或符号表实际验证，不能凭空假设。

输出要求：说明实验的启动流程，回答 entry.S 中 la sp, bootstacktop 和 tail kern_init 的作用；记录 GDB 的关键命令、观察结果和问题答案；最后给出简洁的测试结论。报告内容以当前项目的真实代码和实际运行结果为准。

[RELY]
lab1下所有源文件

[GUARANTEE]
本任务必须完成和验证的主要接口、目标和结果：

make
make qemu
make debug
make gdb

必须满足：

成功生成 bin/kernel 和 bin/ucore.img
QEMU 启动后能够输出 (THU.CST) os is loading ...
GDB 连接后初始 PC 为 0x1000
程序能够从复位代码经过 OpenSBI 到达内核入口 0x80200000
能够验证 kern_entry 设置了有效的内核栈，并跳转到 kern_init
能够记录复位代码的主要指令、关键寄存器值和最终调试结论

[SPECIFICATION]
