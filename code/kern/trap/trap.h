#ifndef __KERN_TRAP_TRAP_H__
#define __KERN_TRAP_TRAP_H__

#include <defs.h>

struct pushregs
{
    uintptr_t zero; // Hard-wired zero
    uintptr_t ra;   // Return address
    uintptr_t sp;   // Stack pointer
    uintptr_t gp;   // Global pointer
    uintptr_t tp;   // Thread pointer
    uintptr_t t0;   // Temporary
    uintptr_t t1;   // Temporary
    uintptr_t t2;   // Temporary
    uintptr_t s0;   // Saved register/frame pointer
    uintptr_t s1;   // Saved register
    uintptr_t a0;   // Function argument/return value
    uintptr_t a1;   // Function argument/return value
    uintptr_t a2;   // Function argument
    uintptr_t a3;   // Function argument
    uintptr_t a4;   // Function argument
    uintptr_t a5;   // Function argument
    uintptr_t a6;   // Function argument
    uintptr_t a7;   // Function argument
    uintptr_t s2;   // Saved register
    uintptr_t s3;   // Saved register
    uintptr_t s4;   // Saved register
    uintptr_t s5;   // Saved register
    uintptr_t s6;   // Saved register
    uintptr_t s7;   // Saved register
    uintptr_t s8;   // Saved register
    uintptr_t s9;   // Saved register
    uintptr_t s10;  // Saved register
    uintptr_t s11;  // Saved register
    uintptr_t t3;   // Temporary
    uintptr_t t4;   // Temporary
    uintptr_t t5;   // Temporary
    uintptr_t t6;   // Temporary
};

// 中断或异常入口保存的通用寄存器及特权状态；也用来构造线程首次运行现场。
// 布局必须与 trapentry.S 的 SAVE_ALL / RESTORE_ALL 一致，每槽占 REGBYTES。
// `trapframe` 保存发生中断或异常时的完整通用寄存器以及 `sstatus`、`sepc`、`badvaddr`、`cause` 等状态，用于从陷阱返回时恢复处理器现场。
// `context` 用于内核线程之间的函数级切换；`trapframe` 用于陷阱入口和返回，二者保存的场景与寄存器范围不同。
struct trapframe
{
    struct pushregs gpr;
    uintptr_t status;
    // 恢复到 sepc 后，sret 从这里指定的指令地址继续执行。
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};

void trap(struct trapframe *tf);
void idt_init(void);
void print_trapframe(struct trapframe *tf);
void print_regs(struct pushregs *gpr);

#endif /* !__KERN_TRAP_TRAP_H__ */
