#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>

// process's state in his life cycle
enum proc_state
{
    // RUNNABLE 同时包含就绪和正在运行；正在运行的线程由 current 标识。
    PROC_UNINIT = 0, // uninitialized
    PROC_SLEEPING,   // sleeping
    PROC_RUNNABLE,   // runnable(maybe running)
    PROC_ZOMBIE,     // almost dead, and wait parent proc to reclaim his resource
};

// 内核函数调用边界的切换现场，与 switch.S 中 14 个寄存器的保存顺序一致。
// `context` 保存内核线程切换所需的返回地址 `ra`、栈指针 `sp` 和被调用者保存寄存器 `s0` 至 `s11`。
// 调度切换时，`switch_to()` 保存旧线程的 `context`，再恢复新线程的 `context`，使它从上次停下的位置继续执行。
struct context
{
    uintptr_t ra;
    uintptr_t sp;
    uintptr_t s0;
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};

#define PROC_NAME_LEN 15
#define MAX_PROCESS 4096
#define MAX_PID (MAX_PROCESS * 2)

extern list_entry_t proc_list;


/**
 * `proc_struct` 是进程或内核线程的控制块：
 * | 字段 | 作用 |
 * |---|---|
 * | `state` | 记录未初始化、睡眠、可运行或僵尸状态 |
 * | `pid` | 进程标识符 |
 * | `runs` | 被调度运行的次数 |
 * | `kstack` | 该进程内核栈的基地址 |
 * | `need_resched` | 标记当前进程是否需要重新调度 |
 * | `parent` | 指向父进程 |
 * | `mm` | 指向该进程的虚拟内存管理结构；本实验内核线程共享内核空间，此处为 `NULL` |
 * | `context` | 保存内核线程切换所需寄存器 |
 * | `tf` | 指向内核栈上的 `trapframe` |
 * | `pgdir` | 根页表的物理地址，切换时将其物理页号写入 `satp` |
 * | `flags` | 进程控制标志 |
 * | `name` | 进程名称 |
 * | `list_link` | 将控制块链接到进程链表，供调度遍历 |
 * | `hash_link` | 将控制块链接到 PID 哈希桶，供 PID 查找 |
 */

// 线程控制块：资源、调度状态、寄存器现场和链表索引集中在此。
struct proc_struct
{
    // alloc_proc 先设 UNINIT，wakeup_proc 再设 RUNNABLE。
    enum proc_state state;        // Process state
    // pid 唯一标识线程，runs 统计调度器选中次数。
    int pid;                      // Process ID
    int runs;                     // the running times of Proces
    // kstack 为内核栈低地址；栈从 kstack+KSTACKSIZE 向低地址增长。
    uintptr_t kstack;             // Process kernel stack
    // 调度请求标志由 cpu_idle 检查，schedule 响应后清零。
    volatile bool need_resched;   // bool value: need to be rescheduled to release CPU?
    // 父线程由 do_fork 设置，init 的父线程是 idle。
    struct proc_struct *parent;   // the parent process
    // Lab4 内核线程的 mm 为 NULL，直接共享内核页表。
    struct mm_struct *mm;         // Process's memory management field
    struct context context;       // Switch here to run process
    // tf 指向 trapframe；首次运行时由 copy_thread 在内核栈高端构造。
    struct trapframe *tf;         // Trap frame for current interrupt
    // pgdir 保存物理地址；写入 satp 前需右移 12 位得到物理页号。
    uintptr_t pgdir;              // the base addr of Page Directroy Table(PDT)
    // 通用进程控制标志初始为 0；name 最多保存 15 个字符和结尾零。
    uint32_t flags;               // Process flag
    char name[PROC_NAME_LEN + 1]; // Process name
    // 两个独立节点让同一控制块同时属于全局进程链表和 PID 哈希桶。
    list_entry_t list_link;       // Process link list
    list_entry_t hash_link;       // Process hash list
};

#define le2proc(le, member) \
    to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);

#endif /* !__KERN_PROCESS_PROC_H__ */
