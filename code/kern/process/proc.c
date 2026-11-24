#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

/**
 * | 结构或字段 | 用途 |
 * |---|---|
 * | `hash_list` / `hash_link` | 按 PID 把线程分到不同桶，供 `find_proc()` 查找。 |
 * | `proc_list` / `list_link` | 把 `do_fork()` 创建的线程串起来，供 `schedule()` 遍历；`get_pid()` 也用它检查 PID 是否已被占用。 |
 * | `parent` | 记录父子关系。`do_fork()` 设置 `proc->parent = current`，所以 `initproc->parent` 是 `idleproc`。 |
 */


// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
// PID 映射到 1024 个桶之一；桶内仍需比较 PID 来处理哈希冲突。
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// set_proc_name - set the name of proc
/*
 * 作用：设置线程名称。
 * 实现：清零名称数组，复制固定长度的名称并保留结尾的零字符。
 */
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
/*
 * 作用：取得线程名称供输出。
 * 实现：清零静态缓冲区，复制线程名称后返回该缓冲区。
 */
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// get_pid - alloc a unique pid for process
/*
 * 作用：为新线程分配未占用的正 PID。
 * 实现：递增候选 PID，必要时扫描进程链表避开冲突，并记录下一个已占用的边界。
 */
static int
get_pid(void)
{
    // 在 1 到 MAX_PID-1 内循环分配，next_safe 记录下一处已占用的边界。
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}


// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
/*
 * 作用：衔接新线程的第一次上下文切换。
 * 实现：将 current->tf 交给 forkrets，经 __trapret 恢复 trapframe 后进入线程入口。
 */
static void
forkret(void)
{
    // 新线程首次被 switch_to 选中后到达这里，转入 trapframe 恢复路径。
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
/*
 * 作用：将线程登记到 PID 哈希表。
 * 实现：计算 PID 对应的桶，再把控制块的 hash_link 挂入桶内链表。
 */
static void
hash_proc(struct proc_struct *proc)
{
    // 使用 hash_link 挂入 PID 桶；list_link 独立用于全局进程链表。
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
/*
 * 作用：按 PID 查找线程控制块，找不到返回 NULL。
 * 实现：校验 PID 范围，算出哈希桶，遍历桶内节点并逐个比较 PID。
 */

 /**
  * `find_proc()` 怎么根据 PID 找到线程？哈希函数做了什么？
  * 
  * `hash_proc()` 在创建线程时按 PID 把 `hash_link` 挂入对应的桶。
  * `find_proc(pid)` 先判断 `0 < pid && pid < MAX_PID`，再用同一个 `pid_hashfn(pid)` 算出桶下标，只遍历这个桶的链表。
  * 每个节点用 `le2proc(le, hash_link)` 找回所属的 `proc_struct`，然后比较 `proc->pid`；相同就返回该指针，遍历完没找到就返回 `NULL`。
  * `idleproc` 的 PID 为 0，直接用 `idleproc` 指针访问。
  * `pid_hashfn(pid)` 调用 `hash32(pid, 10)`。`hash32()` 用 PID 乘以 `0x9e370001`，把乘积存入 32 位无符号变量，再取高 10 位，
  * 因此桶下标在 `0` 到 `1023` 之间。多个 PID 可以落到同一个桶，所以还要逐个比较 PID。
  */

struct proc_struct *
find_proc(int pid)
{
    // PID=0 的 idleproc 单独保存，不通过此哈希表查找。
    if (0 < pid && pid < MAX_PID)
    {
        // 先计算桶，再遍历桶内链表；同一桶可能包含多个不同 PID。
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            // 从嵌入的 hash_link 节点地址反推整个控制块地址。
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
/*
 * 作用：为线程分配独立的内核栈。
 * 实现：申请 KSTACKPAGE 个连续物理页，成功时将内核虚拟地址写到 kstack，失败返回错误码。
 */
static int
setup_kstack(struct proc_struct *proc)
{
    // 每个新线程申请 KSTACKPAGE=2 个连续物理页，内核栈大小为 8 KiB。
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
/*
 * 作用：归还线程占用的内核栈。
 * 实现：将 kstack 转成 Page 描述符，再把 KSTACKPAGE 个物理页交还分配器。
 */
static void
put_kstack(struct proc_struct *proc)
{
    // kstack 保存内核虚拟地址，释放前先转回物理页描述符。
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}


// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
/*
 * 作用：处理线程函数执行完毕后的退出。
 * 实现：本版直接调用 panic 输出退出信息并进入调试监视器；error_code 尚未用于资源回收。
 */
int do_exit(int error_code)
{
    // Lab4 的退出路径停在 panic；尚不执行上方注释中的完整退出流程。
    panic("process exit!!.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
/*
 * 作用：运行 init 线程的示例任务。
 * 实现：打印当前线程的 PID、名称和传入参数，返回 0 后由汇编入口调用 do_exit。
 */
static int
init_main(void *arg)
{
    // init 线程的实际函数，输出身份和参数后返回到 kernel_thread_entry。
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

//LAB4 function2 2412940
/*
 * 作用：分配并初始化线程控制块，失败返回 NULL。
 * 实现：申请内存并清零，设置初始状态、无效 PID 和共享根页表，再初始化两个链表节点。
 */

/**
 * `alloc_proc()` 用 `kmalloc()` 分配一个进程控制块，失败就返回 `NULL`。
 * 成功后先把结构体清零，再设置 `state = PROC_UNINIT`、`pid = -1`、`pgdir = boot_pgdir_pa`，并初始化 `list_link` 和 `hash_link`。
 * 之后由 `do_fork()` 设置父进程、分配内核栈、准备首次运行的现场、分配 PID 并登记线程，最后调用 `wakeup_proc()` 把它变成可运行状态。
 */

static struct proc_struct *alloc_proc(void)
{
    // 只分配控制块并设置初值，内核栈、PID 和首次运行现场由 do_fork 准备。
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        // mm、tf、parent、context 等先清零，state 尚未变为可运行。
        memset(proc, 0, sizeof(*proc));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        // Lab4 内核线程共享启动根页表，这里保存其物理地址。
        proc->pgdir = boot_pgdir_pa;
        list_init(&proc->list_link);
        list_init(&proc->hash_link);
    }
    return proc;
}

/*
 * 作用：处理新线程的地址空间。
 * 实现：确认当前线程的 mm 为 NULL；新线程沿用 alloc_proc 设置的启动页表，不复制用户地址空间。
 */
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    // 当前实验创建内核线程，mm 为 NULL，直接沿用 alloc_proc 设置的页表。
    assert(current->mm == NULL);
    return 0;
}

/*
 * 作用：准备新线程首次运行所需的 context 和 trapframe。
 * 实现：在内核栈高端复制 trapframe，设置初始 a0 和 sp，再让 context 的返回地址指向 forkret。
 */
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // 内核栈向低地址增长，在高端预留并复制一份初始 trapframe。
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    *proc->tf = *tf;
    // a0 是返回值寄存器；子线程的初始返回值设为 0。
    proc->tf->gpr.a0 = 0;
    // 未指定栈指针时，线程从 trapframe 所在地址开始向低地址使用栈。
    proc->tf->gpr.sp = esp ? esp : (uintptr_t)proc->tf;
    // switch_to 的 ret 先到 forkret，再经 forkrets 恢复完整 trapframe。
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}

/*
 * 作用：创建线程，成功返回 PID，失败返回负错误码。
 * 实现：先分配控制块和内核栈，再准备地址空间和执行现场，最后分配 PID、登记链表并唤醒；失败则释放已分配的资源。
 */
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    // clone_flags 控制资源共享，stack 是初始栈指针，tf 是待复制的现场。
    // 创建成功返回子线程 PID；失败返回负错误码。
    if (nr_process >= MAX_PROCESS) {
        return -E_NO_FREE_PROC;
    }
    struct proc_struct *proc = alloc_proc();
    if (proc == NULL) {
        return -E_NO_MEM;
    }
    int ret = -E_NO_MEM;
    // 创建者为父线程；先准备资源，完成后才登记并设为可运行。
    proc->parent = current;
    if (setup_kstack(proc) != 0) {
        goto bad_proc;
    }
    if ((ret = copy_mm(clone_flags, proc)) != 0) {
        goto bad_stack;
    }
    copy_thread(proc, stack, tf);
    bool intr_flag;
    local_intr_save(intr_flag);
    // 关中断保护 PID 分配、哈希表和全局链表的一组更新。
    proc->pid = get_pid();
    hash_proc(proc);
    list_add_before(&proc_list, &proc->list_link);
    nr_process++;
    local_intr_restore(intr_flag);
    // 资源和索引均已就绪，允许调度器选中它；此处并不立即切换。
    wakeup_proc(proc);
    return proc->pid;

bad_stack:
    // 按资源申请的相反顺序回收：先内核栈，后控制块。
    put_kstack(proc);
bad_proc:
    kfree(proc);
    return ret;
}

/*
 * 作用：创建执行 fn(arg) 的内核线程。
 * 实现：构造初始 trapframe，将函数和参数放入 s0、s1，设置 S 态返回状态和汇编入口，再调用 do_fork。
 */

/**
 * `kernel_thread()` 做了什么？
 * 
 * `kernel_thread(fn, arg, clone_flags)` 为内核线程构造初始 `trapframe`，将 `fn` 和 `arg` 放入约定寄存器，
 * 将首次执行入口设为 `kernel_thread_entry`，并通过 `do_fork(clone_flags | CLONE_VM, ...)` 建立线程。
 * `CLONE_VM` 表示线程共享现有地址空间；本实验中的内核线程使用同一个内核页目录。
 */

int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    // fn 为线程函数，arg 为参数；构造现场后交给 do_fork 分配线程资源。
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    // process/entry.S 约定 s0 保存函数地址、s1 保存参数。
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    // SPP=1 让 sret 返回 S 态；SPIE=1 让返回后开启中断，恢复前 SIE=0。
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

/*
 * 作用：初始化线程管理并建立 idleproc 和 initproc。
 * 实现：初始化进程链表和哈希桶，用启动栈建立 idle 并设为 current，再创建和登记 init。
 */


/**
 * `idleproc` 是怎样创建的？
 * 
 * `proc_init()` 初始化 `proc_list` 和所有 PID 哈希桶后调用 `alloc_proc()` 分配 `idleproc` 的控制块。
 * 随后设置 PID 为 0、状态为 `PROC_RUNNABLE`、内核栈为启动阶段已经建立的 `bootstack`、`need_resched` 为 1、名称为 `idle`，
 * 并把 `current` 指向它。idle 使用启动栈，不再申请单独的内核栈；它也没有通过 `do_fork()` 创建，因此不经过普通线程的 PID 和哈希登记流程。
 */

/**
 * `initproc` 是怎样加入进程链表和哈希表的？
 * 
 * `proc_init()` 调用 `kernel_thread()`，后者构造初始现场并进入 `do_fork()`。
 * `do_fork()` 分配控制块和内核栈、准备线程现场，然后在保存并关闭中断的临界区内分配 PID、
 * 调用 `hash_proc()` 将 `hash_link` 插入 `hash_list[pid_hashfn(pid)]`，再将 `list_link` 插入 `proc_list` 并增加进程数。
 * 登记完成后恢复中断，调用 `wakeup_proc()` 将状态改为 `PROC_RUNNABLE`。
 * `proc_init()` 再通过 `find_proc(pid)` 从哈希表查回控制块，赋给 `initproc` 并设置名称。
 */

void proc_init(void)
{
    // 初始化全局线程链表和每个 PID 哈希桶的双向循环链表表头。
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++) {
        list_init(&hash_list[i]);
    }
    idleproc = alloc_proc();
    if (idleproc == NULL) {
        panic("cannot allocate idleproc.\n");
    }
    // idle 代表当前启动执行流，直接使用 bootstack，不另分配内核栈。
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;
    current = idleproc;
    // idle 的 need_resched=1，进入 cpu_idle 后会触发首次调度到 init。
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0 || (initproc = find_proc(pid)) == NULL) {
        panic("cannot create initproc.\n");
    }
    set_proc_name(initproc, "init");
}



//LAB4 function3 2412940
/*
 * 作用：为空闲线程提供执行循环。
 * 实现：持续检查 need_resched，置位时调用 schedule，让可运行线程获得 CPU。
 */
void cpu_idle(void)
{
    // 空闲循环检查调度标志；Lab4 在此显式调用调度器。
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

/*
 * 作用：切换到调度器选定的线程。
 * 实现：目标变化时关中断，更新 current 和 satp、刷新 TLB，再调用 switch_to；旧线程恢复执行后还原自己的中断状态。
 */
void proc_run(struct proc_struct *proc)
{
    // proc 是调度器选定的目标；相同线程无需再切换。
    if (proc != current) {
        bool intr_flag;
        local_intr_save(intr_flag);
        // 更新 current、页表和寄存器期间保持本核中断关闭。
        struct proc_struct *prev = current;
        current = proc;
        // Sv39 uses MODE=8 and the physical root page number in satp.
        write_csr(satp, ((uintptr_t)8 << 60) | (proc->pgdir >> RISCV_PGSHIFT));
        flush_tlb();
        // 保存 prev 的内核执行位置并恢复 proc 的执行位置。
        switch_to(&prev->context, &proc->context);
        // 旧线程将来再次被切回时才执行到这里，使用旧线程栈上的 intr_flag。
        local_intr_restore(intr_flag);
    }
}
