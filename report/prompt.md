[PROMPT]

任务：完成 Lab1“最小可执行内核”的构建、启动和调试验证。请阅读当前项目中的实验文档和源代码，确认这个 RISC-V 内核能够被交叉编译、链接并生成可以由 QEMU 启动的内核镜像。使用 QEMU 启动内核，并使用 RISC-V GDB 跟踪 CPU 从复位地址 0x1000 到内核入口 0x80200000 的执行过程。

操作要求：你必须在当前项目中执行真实的编译、运行和调试命令，并根据实际输出判断结果。可以在确有必要时修改 Makefile 中的 QEMU 启动参数，以适配当前环境中的 QEMU 和 OpenSBI 版本，但不要修改实验核心代码，也不要增加与本实验无关的功能。所有关键地址、寄存器值和函数位置都必须通过 QEMU、GDB、objdump 或符号表实际验证，不能根据猜测填写结果。

输出要求：说明本实验从 CPU 复位到内核运行的完整启动流程，解释 entry.S 中 la sp, bootstacktop 和 tail kern_init 的作用，记录 GDB 使用的关键命令和实际观察结果，回答 RISC-V 硬件加电后最初执行的指令位于什么地址以及这些指令完成了什么功能，最后给出简洁明确的测试结论。所有结论必须以当前项目的真实代码和实际运行结果为依据。

[RELY]

当前项目包含以下与任务直接相关的文件和模块。

Makefile 负责编译 C 语言和汇编源文件、链接目标文件、生成 bin/kernel 和 bin/ucore.img，并提供 qemu、debug 和 gdb 目标。

tools/kernel.ld 是内核链接脚本，指定输出架构为 RISC-V，指定 kern_entry 为程序入口，并将内核基地址设置为 0x80200000。

kern/init/entry.S 定义内核入口 kern_entry。它通过 la sp, bootstacktop 设置内核栈指针，并通过 tail kern_init 将控制权交给 C 语言初始化函数。

kern/init/init.c 定义 kern_init。这个函数负责清零 edata 到 end 之间的区域，调用 cprintf 输出内核启动信息，然后进入无限循环。

libs/sbi.c 通过 RISC-V 的 ecall 指令封装 SBI 调用，并提供字符输出功能。

kern/driver/console.c 负责封装底层控制台字符输出。

kern/libs/stdio.c 和 libs/printfmt.c 负责实现 cprintf 等格式化输出功能。

PGSIZE 的值为 4096，PGSHIFT 的值为 12。KSTACKPAGE 的值为 2，KSTACKSIZE 等于 KSTACKPAGE 乘以 PGSIZE。bootstack 表示内核栈起始位置，bootstacktop 表示内核栈顶位置。RISC-V 栈通常从高地址向低地址增长。

链接脚本提供 edata 和 end 符号，kern_init 使用它们清零内核的未初始化数据区域。当前环境使用 riscv64-unknown-elf-gcc、riscv64-unknown-elf-gdb 和 qemu-system-riscv64。QEMU 调试模式使用 -s 和 -S 等待 GDB 通过 localhost:1234 连接。

[GUARANTEE]

必须完成并验证以下任务。

第一，执行 make，成功生成 bin/kernel 和 bin/ucore.img，并确认内核 ELF 的入口地址为 0x80200000。

第二，执行 make qemu，确认 OpenSBI 能够启动，并且内核输出 THU.CST os is loading ...。内核输出后进入无限循环属于预期行为。

第三，执行 make debug 和 make gdb，确认 GDB 能够连接 QEMU，连接后初始程序计数器为 0x1000。

第四，使用 GDB 查看 0x1000 附近的指令，说明这些指令如何读取 hart 信息、启动参数和 OpenSBI 入口地址，并说明它们如何将控制权交给 OpenSBI。

第五，在 0x80200000 设置断点并继续运行，确认程序最终停在 kern_entry，并且当前程序计数器为 0x80200000。

第六，单步执行 kern_entry，确认 sp 被设置为 bootstacktop。当前环境中应观察到 sp 为 0x80203000。

第七，继续执行到 kern_init，确认汇编入口已经完成工作，程序已经进入 C 语言内核初始化函数。

第八，记录实际使用的关键命令、关键地址、寄存器值、函数位置和测试结论，并将这些内容整理到实验报告中。

可以根据需要增加只用于验证的命令，但不得虚构未实际观察到的结果。

[SPECIFICATION]

内核构建与启动

前置条件：当前目录包含完整的 Lab1 源代码、Makefile 和链接脚本，RISC-V 交叉编译器和 QEMU 已安装并能够从命令行调用。

后置条件：内核能够完成编译、链接和镜像转换；QEMU 能够启动 OpenSBI 并将控制权交给 0x80200000 的内核；内核能够输出启动信息并按照代码设计进入无限循环。

情况一：执行 make 成功后，应生成 bin/kernel 和 bin/ucore.img，内核 ELF 的入口地址应为 0x80200000。

情况二：执行 make qemu 后，OpenSBI 的下一跳地址应为 0x80200000，下一运行模式应为 S-mode，随后应出现 THU.CST os is loading ...。

情况三：如果当前 QEMU 和 OpenSBI 版本使用原有加载参数不能跳转到内核，只能在必要范围内调整 QEMU 的加载参数。调整后必须重新编译、启动并验证结果，同时记录产生问题的现象和修正后的结果。

内核入口初始化

前置条件：OpenSBI 已将控制权交给 kern_entry，当前程序计数器为 0x80200000，bootstack 和 bootstacktop 已经由 entry.S 定义，内核栈空间已经预留。

后置条件：sp 指向 bootstacktop，控制流进入 kern_init，kern_entry 不需要返回。

情况一：执行 la sp, bootstacktop 后，GDB 观察到 sp 等于 bootstacktop 的地址。当前环境中这个地址应为 0x80203000。

情况二：执行 tail kern_init 后，控制流到达 kern_init。反汇编结果中应能看到从 kern_entry 跳转到 kern_init 的指令。

GDB 启动流程验证

前置条件：QEMU 以调试模式启动并在 localhost:1234 等待连接，GDB 已加载带调试信息的 bin/kernel。

后置条件：能够观察 CPU 从 0x1000 开始执行复位代码，经过 OpenSBI 后到达 0x80200000，并最终进入 kern_init。

情况一：GDB 连接后，当前程序计数器应为 0x1000。使用 x/8i $pc 查看复位代码附近的指令，这些指令应负责读取 hart 信息、读取启动参数和 OpenSBI 入口地址，并通过跳转指令进入 OpenSBI。

情况二：在 0x80200000 设置断点并执行 continue 后，GDB 应停在 kern_entry，当前程序计数器应为 0x80200000。

情况三：使用 si 单步执行入口指令，再查看 pc 和 sp，sp 应已经设置为 0x80203000。继续执行到 kern_init 断点后，GDB 应显示当前函数为 kern_init。

特殊要求：关键结论必须来自实际的 GDB 或 QEMU 输出。不能把 OpenSBI 的启动信息误认为内核输出，THU.CST os is loading ... 才是本实验内核通过 SBI 输出的结果。内核输出后进入无限循环属于预期行为，不能将其判断为启动失败。报告中的命令、地址和输出必须与当前环境的实际结果保持一致。
