#include <intr.h>
#include <riscv.h>

/* intr_enable - enable irq interrupt */
// 设置 S 态全局中断使能位，具体中断源还受各自使能位控制。
/*
 * 作用：开启 S 态全局中断。
 * 实现：调用 set_csr 设置 sstatus 的 SIE 位。
 */
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }

/* intr_disable - disable irq interrupt */
// 清除 SIE，不改动 sstatus 的其他位。
/*
 * 作用：关闭 S 态全局中断。
 * 实现：调用 clear_csr 清除 sstatus 的 SIE 位。
 */
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
