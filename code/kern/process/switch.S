//LAB4 function3 2412940
#include <riscv.h>

.text
/*
 * 作用：切换两个内核线程的 context。
 * 实现：先通过 a0 保存旧 ra、sp 和 s0 至 s11，再通过 a1 恢复目标线程寄存器，ret 跳到目标返回地址。
 */
.globl switch_to
switch_to:
    /* a0 指向旧 context，a1 指向新 context，偏移须与 proc.h 字段顺序一致。 */
    /* 保存返回地址、栈指针及被调用者保存寄存器，RV64 每项占 8 字节。 */
    STORE ra, 0*REGBYTES(a0)
    STORE sp, 1*REGBYTES(a0)
    STORE s0, 2*REGBYTES(a0)
    STORE s1, 3*REGBYTES(a0)
    STORE s2, 4*REGBYTES(a0)
    STORE s3, 5*REGBYTES(a0)
    STORE s4, 6*REGBYTES(a0)
    STORE s5, 7*REGBYTES(a0)
    STORE s6, 8*REGBYTES(a0)
    STORE s7, 9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)
    /* 恢复目标线程的寄存器；加载 sp 后 CPU 已改用目标线程的内核栈。 */
    LOAD ra, 0*REGBYTES(a1)
    LOAD sp, 1*REGBYTES(a1)
    LOAD s0, 2*REGBYTES(a1)
    LOAD s1, 3*REGBYTES(a1)
    LOAD s2, 4*REGBYTES(a1)
    LOAD s3, 5*REGBYTES(a1)
    LOAD s4, 6*REGBYTES(a1)
    LOAD s5, 7*REGBYTES(a1)
    LOAD s6, 8*REGBYTES(a1)
    LOAD s7, 9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)
    /* ret 跳到恢复出的 ra；新线程首次为 forkret，已运行线程为旧调用返回点。 */
    ret
