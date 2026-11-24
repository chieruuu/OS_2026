.text
/*
 * 作用：运行新内核线程的入口。
 * 实现：把 s1 移到 a0 作为参数，通过 s0 调用线程函数，再把返回值交给 do_exit。
 */

/* 定义了新内核线程的汇编入口 `kernel_thread_entry`。
 * `kernel_thread()` 把线程函数和参数分别存到初始 `trapframe` 的 `s0` 和 `s1` 中，再把 `epc` 设为 `kernel_thread_entry`。
 * 线程首次被调度时，`__trapret` 恢复这些寄存器，`sret` 跳到该入口。
 */
 
.globl kernel_thread_entry
kernel_thread_entry:        # void kernel_thread(void)
    /* 初始 trapframe 恢复后 s0=线程函数、s1=参数；a0 是第一个参数寄存器。 */
	move a0, s1
    /* 调用线程函数 fn(arg)，返回值按约定保留在 a0。 */
	jalr s0

    /* 将线程函数返回值传给 do_exit，Lab4 的 do_exit 会进入 panic。 */
	jal do_exit
