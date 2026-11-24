#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <defs.h>
#include <intr.h>
#include <riscv.h>

// 读取本核 S 态中断开关；返回 true 表示此前开启，随后将其关闭。
/*
 * 作用：保存本核 S 态中断开关并关闭中断。
 * 实现：读取 SIE；原来开启时清除该位并返回 true，原来关闭时返回 false。
 */
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    // 原来已经关闭时保存 false，内层临界区退出后仍应保持关闭。
    return 0;
}

// 仅在此前开启时重新设置 SIE，恢复进入临界区前的状态。
/*
 * 作用：依据保存值恢复本核 S 态中断状态。
 * 实现：flag 为 true 时设置 SIE，为 false 时保持关闭，避免嵌套临界区提前开中断。
 */
static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

// 调用者提供 bool 变量 x 保存旧状态，并将同一变量传给 restore。

/**
 * `local_intr_save()` 和 `local_intr_restore()` 如何开关中断？
 * 操作 RISC-V `sstatus` 寄存器的 `SIE` 位。`__intr_save()` 先读取原来的 SIE：原来为 1 时关闭中断并返回 `true`；原来为 0 时返回 `false`。
 * `__intr_restore(flag)` 只在 `flag` 为 `true` 时重新开启中断。因此它恢复进入临界区前的状态，不会把原本关闭的中断意外打开。
 * 
 * 具体地，`intr_disable()` 用 `clear_csr(sstatus, SSTATUS_SIE)` 清除 SIE，`intr_enable()` 用 `set_csr(sstatus, SSTATUS_SIE)` 设置 SIE。
 * 保存旧状态也使嵌套临界区能够正确工作：内层保存到 `false`，退出时不会提前开启中断。
 */


#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */
