[PROMPT]
**任务**：修改 `code/kern/trap/trapentry.S`，完成 Trap 入口、上下文保存后的 C 函数调用和 Trap 返回时的寄存器恢复。保留 `SAVE_ALL`、`struct trapframe` 和既有入口标签，只补全标记位置。

**操作要求**：直接修改实际文件；根据 `SAVE_ALL` 的真实布局实现对称恢复，遵守 RISC-V 调用约定，并保持 `trap(struct trapframe *tf)` 接口不变。

**输出要求**：完成代码后检查汇编能编译链接，不引入无关功能。

[RELY]
`SAVE_ALL` 在当前 `sp` 指向的 Trap frame 中依次保存 x0、x1、x3...x31，再保存原始 sp、sstatus、sepc、stval 和 scause；`struct trapframe` 的前 32 个槽是 `struct pushregs`，之后是 status、epc、badvaddr、cause。`trap` 的签名为 `void trap(struct trapframe *tf)`，`__trapret` 调用 `RESTORE_ALL` 后执行 `sret`。

[GUARANTEE]
必须实现或修改的接口：
```asm
RESTORE_ALL
__alltraps
```

[SPECIFICATION]
**Pre-Condition**：`SAVE_ALL` 已建立完整 Trap frame，`sp` 指向其起始地址。

**Post-Condition**：`__alltraps` 将当前 Trap frame 地址作为第一个参数传给 `trap`；`trap` 返回后，`RESTORE_ALL` 恢复所有通用寄存器及 `sstatus`、`sepc`，恢复原始栈指针，并使 `sret` 返回被打断的执行流。

**Case 1**：Trap 入口保存完成后，调用 C 函数时不得破坏 Trap frame；调用返回后仍从同一 frame 恢复。

**Case 2**：恢复 `sp` 时使用保存的原始 x2 值，不能把当前 Trap frame 地址误当成原始栈指针。

**Case 3**：恢复顺序不能覆盖尚未读取的 frame 数据，且 `sret` 使用恢复后的 `sepc` 和状态寄存器。

**Requirements**：不得修改 `SAVE_ALL`、Trap frame 布局或 `trap` 签名；所有寄存器（x0 除外）恢复后应与 Trap 前一致。

[PROMPT]
**任务**：修改 `code/kern/trap/trap.c`，实现中断与异常处理模块，包括 `interrupt_handler` 和 `exception_handler`，接入已有的 Trap 分发流程。

**操作要求**：直接修改实际文件，保留既有公共接口和辅助函数；使用工程中真实的宏、结构体和硬件接口，不修改无关模块。

**输出要求**：实现后完成构建，并验证时钟中断能够重新设定下一次事件、累计 ticks，非法指令和断点能够前进正确长度的 `epc`。

[RELY]
可使用 `struct trapframe`、`print_trapframe`、`clock_set_next_event`、全局 `ticks`、`TICK_NUM`、`IRQ_S_TIMER`、`CAUSE_ILLEGAL_INSTRUCTION`、`CAUSE_BREAKPOINT`、`sbi_shutdown`、`cprintf` 和 RISC-V `cause` 编码。中断最高位表示 interrupt，异常 cause 为非负值。

[GUARANTEE]
必须实现或修改的接口：
```c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```

[SPECIFICATION]
**Pre-Condition**：函数收到由 Trap 入口建立的有效 `struct trapframe`。

**Post-Condition**：处理完成后函数返回统一 Trap 返回路径；时钟中断可持续触发，已处理异常不会因 `epc` 未更新而重复陷入。

**Case 1**：Supervisor timer interrupt 每次先设置下一次时钟事件，再使 `ticks` 增加一次；每达到 `TICK_NUM` 的整数倍输出规定统计信息，并在达到实验规定次数后调用关机接口。

**Case 2**：Supervisor software、external 等已知中断输出对应提示；未知中断输出原因并调用 `print_trapframe`。

**Case 3**：非法指令和断点输出规定信息，并依据 `epc` 处指令的低两位判断 16 位或 32 位长度后前移 `tf->epc`；其他异常调用 `print_trapframe`。

**Requirements**：不得假定所有异常都是 4 字节指令；不得只计数而不重新设置定时器；不得修改函数签名或绕过真实 Trap 分发流程。
