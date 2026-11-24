#include <riscv.h>

    .macro SAVE_ALL
    /* 为通用寄存器和四个 CSR 状态预留 36 个槽，原 sp 暂存 sscratch。 */
    .align 2

    csrw sscratch, sp

    addi sp, sp, -36 * REGBYTES
    # save gprs
    STORE  x1,1*REGBYTES(sp)
    STORE  x3,3*REGBYTES(sp)
    STORE  x4,4*REGBYTES(sp)
    STORE  x5,5*REGBYTES(sp)
    STORE  x6,6*REGBYTES(sp)
    STORE  x7,7*REGBYTES(sp)
    STORE  x8,8*REGBYTES(sp)
    STORE  x9,9*REGBYTES(sp)
    STORE  x10,10*REGBYTES(sp)
    STORE  x11,11*REGBYTES(sp)
    STORE  x12,12*REGBYTES(sp)
    STORE  x13,13*REGBYTES(sp)
    STORE  x14,14*REGBYTES(sp)
    STORE  x15,15*REGBYTES(sp)
    STORE  x16,16*REGBYTES(sp)
    STORE  x17,17*REGBYTES(sp)
    STORE  x18,18*REGBYTES(sp)
    STORE  x19,19*REGBYTES(sp)
    STORE  x20,20*REGBYTES(sp)
    STORE  x21,21*REGBYTES(sp)
    STORE  x22,22*REGBYTES(sp)
    STORE  x23,23*REGBYTES(sp)
    STORE  x24,24*REGBYTES(sp)
    STORE  x25,25*REGBYTES(sp)
    STORE  x26,26*REGBYTES(sp)
    STORE  x27,27*REGBYTES(sp)
    STORE  x28,28*REGBYTES(sp)
    STORE  x29,29*REGBYTES(sp)
    STORE  x30,30*REGBYTES(sp)
    STORE  x31,31*REGBYTES(sp)

    # get sr, epc, badvaddr, cause
    csrr s0, sscratch
    csrr s1, sstatus
    csrr s2, sepc
    csrr s3, 0x143
    csrr s4, scause

    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
    .endm

    .macro RESTORE_ALL
    /* 按 trapframe 布局恢复状态；先用旧 sp 访问各槽，最后才恢复 sp。 */
    #lab3:填写你在lab3中实现的代码


    # Restore privileged state while sp still points at the trap frame.
    LOAD t0, 32*REGBYTES(sp)
    LOAD t1, 33*REGBYTES(sp)
    csrw sstatus, t0
    csrw sepc, t1

    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    LOAD x4, 4*REGBYTES(sp)
    LOAD x5, 5*REGBYTES(sp)
    LOAD x6, 6*REGBYTES(sp)
    LOAD x7, 7*REGBYTES(sp)
    LOAD x8, 8*REGBYTES(sp)
    LOAD x9, 9*REGBYTES(sp)
    LOAD x10, 10*REGBYTES(sp)
    LOAD x11, 11*REGBYTES(sp)
    LOAD x12, 12*REGBYTES(sp)
    LOAD x13, 13*REGBYTES(sp)
    LOAD x14, 14*REGBYTES(sp)
    LOAD x15, 15*REGBYTES(sp)
    LOAD x16, 16*REGBYTES(sp)
    LOAD x17, 17*REGBYTES(sp)
    LOAD x18, 18*REGBYTES(sp)
    LOAD x19, 19*REGBYTES(sp)
    LOAD x20, 20*REGBYTES(sp)
    LOAD x21, 21*REGBYTES(sp)
    LOAD x22, 22*REGBYTES(sp)
    LOAD x23, 23*REGBYTES(sp)
    LOAD x24, 24*REGBYTES(sp)
    LOAD x25, 25*REGBYTES(sp)
    LOAD x26, 26*REGBYTES(sp)
    LOAD x27, 27*REGBYTES(sp)
    LOAD x28, 28*REGBYTES(sp)
    LOAD x29, 29*REGBYTES(sp)
    LOAD x30, 30*REGBYTES(sp)
    LOAD x31, 31*REGBYTES(sp)
    LOAD x2, 2*REGBYTES(sp)

    .endm

/*
 * 作用：进入中断或异常的通用处理路径。
 * 实现：保存寄存器及状态，将 trapframe 指针作为参数调用 trap，然后进入返回路径。
 */
    .globl __alltraps
__alltraps:
    /* 进入 C 函数前保存现场，把 trapframe 指针作为第一个参数传给 trap。 */
    SAVE_ALL

    #lab3:填写你在lab3中实现的代码
    mv a0, sp
    jal trap
    # sp should be the same as before "jal trap"

/*
 * 作用：恢复 trapframe 并返回指定执行位置。
 * 实现：恢复通用寄存器、sstatus 和 sepc，最后用 sret 恢复执行。
 */
    .globl __trapret
__trapret:
    /* 正常中断返回和新线程首次运行共用此路径。 */
    RESTORE_ALL
    # go back from supervisor call
    sret
 
/*
 * 作用：让新线程复用中断返回路径开始执行。
 * 实现：把 a0 指向的初始 trapframe 用作当前栈上的现场，再跳到 __trapret。
 */
    .globl forkrets
forkrets:
    /* a0=current->tf，切到新线程的 trapframe，随后由 sret 转到 tf->epc。 */
    # set stack to this new process's trapframe
    move sp, a0
    j __trapret
