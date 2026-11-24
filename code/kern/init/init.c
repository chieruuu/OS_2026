#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <kdebug.h>
#include <picirq.h>
#include <trap.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <vmm.h>
#include <proc.h>
#include <kmonitor.h>
#include <dtb.h>

int kern_init(void) __attribute__((noreturn));
void grade_backtrace(void);

/*
 * 作用：完成内核的 C 语言初始化并进入 idle 循环。
 * 实现：清零 BSS、解析设备树，再初始化控制台、内存、中断和线程，开启时钟中断后调用 cpu_idle。
 */

/**
 * 第一个内核线程 `initproc` 是怎样创建并首次运行的？
 * 
 * `kern_init()` 初始化物理内存、虚拟内存后调用 `proc_init()`。`proc_init()` 初始化进程链表和 PID 哈希表，创建 `idleproc`，
 * 并令 `current = idleproc`。随后它调用 `kernel_thread(init_main, "Hello world!!", 0)` 创建 `initproc`。
 * 
 * `kernel_thread()` 构造初始 `trapframe`：把线程函数放入 `s0`、参数放入 `s1`，并把入口设为 `kernel_thread_entry`，
 * 再调用 `do_fork()`。`do_fork()` 分配并初始化 `proc_struct` 和内核栈，`copy_thread()` 将初始 `trapframe` 放到栈顶，
 * 并令 `context.ra` 指向 `forkret`、`context.sp` 指向该 `trapframe`。最后进程进入 `PROC_RUNNABLE` 并加入进程链表和 PID 哈希表。
 * 
 * `cpu_idle()` 发现需要调度后调用 `schedule()`。调度器选中 `initproc` 后，`proc_run()` 更新 `current`、写入 `satp` 并切换上下文。
 * 首次切换通过 `forkret → forkrets → __trapret` 恢复初始 `trapframe`，`sret` 返回到 `kernel_thread_entry`，再调用 `init_main(arg)`。
 * 函数返回后汇编入口调用 `do_exit()`。
 */

/**
 * Lab4 的整体执行流是什么？
 * 
 * Lab4 启动顺序是：`kern_init()` 初始化物理内存与页表，运行页表检查；初始化中断和虚拟内存；
 * `proc_init()` 建立 idle 和 init 线程；随后初始化时钟并开启中断；CPU 进入 `cpu_idle()`。
 * 需要调度时，`schedule()` 选择可运行线程，`proc_run()` 和 `switch_to()` 完成切换，init 线程从构造好的 `trapframe` 开始执行。
 */


 /**
  * 整个实验上电流程
  * 
  * 上电后，CPU 先执行 OpenSBI，由它完成底层初始化并进入 ucore。
  * `kern_entry` 开启 Sv39 分页、设置内核栈，然后跳到 `kern_init()`，依次初始化内存、中断和线程管理。
  * `proc_init()` 建立 `idleproc` 和 `initproc`，随后开启时钟中断，进入 `cpu_idle()`。
  * idle 检测到调度请求后调用 `schedule()`，通过 `proc_run()` 和 `switch_to()` 切到 init，
  * 恢复它预先构造的 `trapframe`，最终执行 `init_main()`。
  */


int kern_init(void)
{
    // 清零未初始化全局数据，再读取设备树提供的物理内存等平台信息。
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    dtb_init();
    cons_init(); // init the console

    const char *message = "(THU.CST) os is loading ...";
    cprintf("%s\n\n", message);

    print_kerninfo();

    // grade_backtrace();

    // 先准备物理页、页表和 kmalloc，后续 VMA 与线程控制块依赖这些分配接口。
    pmm_init(); // init physical memory management

    pic_init(); // init interrupt controller
    idt_init(); // init interrupt descriptor table

    // VMA 自检后建立 idle/init；时钟和中断开启后进入 idle 循环触发首次调度。
    vmm_init();  // init virtual memory management
    proc_init(); // init process table

    clock_init();  // init clock interrupt
    intr_enable(); // enable irq interrupt

    cpu_idle(); // run idle process
}

static void
lab1_print_cur_status(void)
{
    static int round = 0;
    round++;
}
