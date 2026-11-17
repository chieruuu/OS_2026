# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 4：进程管理 |
| **小组成员** | 2412940-曹皓轩 |
| **完成日期** | 2026-11-10 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2412940-曹皓轩 | 虚拟内存页操作、进程创建与初始化、调度与上下文切换、测试与报告 |

---

## 一、实验目的

1. 理解虚拟地址、页表项、物理页引用计数和 TLB 的关系，实现页映射的建立、更新与撤销。
2. 理解 PCB、内核栈、Trap frame 和 context 的分工，完成内核线程创建与首次运行现场构造。
3. 实现可运行线程的轮转选择和 RISC-V 上下文切换，贯通 idle 到 init 的执行链。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2412940-曹皓轩 | Codex | GPT-6 |  |

---

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

本实验先准备线程运行需要的内存和中断环境，再创建线程，最后通过调度器切换执行。启动后，`pmm_init` 初始化物理页分配器并检查页表映射，`idt_init` 设置 Trap 入口，`vmm_init` 检查虚拟内存区域结构。接着，`proc_init` 将当前执行现场设为 idle 线程，并创建 init 内核线程。开启时钟和中断后，`cpu_idle` 检查 `need_resched`，依次调用 `schedule`、`proc_run` 和 `switch_to`，让 init 开始运行。

新线程第一次运行时还没有可恢复的执行现场，需要在创建时填写 `context` 和 `trapframe`。`context.ra` 指向 `forkret`，因此 `switch_to` 恢复上下文后会先执行 `forkret`。它调用 `forkrets`，再由 `__trapret` 恢复 Trap frame，执行 `sret` 后进入 `kernel_thread_entry`，最终调用线程函数。创建阶段填写的寄存器值，决定了调度后从哪里执行、使用哪一块栈，以及向线程函数传递什么参数。

### 2.2 功能的逐步实现

1. 实现 `page_insert`、`page_remove`，维护映射、引用计数和 TLB 的一致性。
2. 初始化 PCB、内核栈和首次运行现场，完成 idle/init 的创建及线程登记。
3. 实现可运行线程轮转选择和寄存器上下文切换，使 init 开始执行。
4. 通过页表自检、VMA 自检及三个评分项目验证完整执行链。

---

## 四、实验内容与实现

### 功能模块一：虚拟内存页操作

**负责人：** 2412940-曹皓轩

#### 模块功能描述

`page_insert` 负责建立或更新页映射，`page_remove` 负责撤销映射。两者通过内部函数 `page_remove_pte` 处理旧映射的引用计数和物理页释放。代码如下：

```c
static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if (*ptep & PTE_V) {
        struct Page *page = pte2page(*ptep);
        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}

int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }
    page_ref_inc(page);
    if (*ptep & PTE_V) {
        if (pte2page(*ptep) == page) {
            page_ref_dec(page);
        } else {
            page_remove_pte(pgdir, la, ptep);
        }
    }
    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(pgdir, la);
    return 0;
}

void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL) {
        page_remove_pte(pgdir, la, ptep);
    }
}
```

`page_insert` 先调用 `get_pte` 查找或创建目标页表项。如果无法取得 PTE，就返回 `-E_NO_MEM`。取得 PTE 后先增加新物理页的引用计数，再检查原映射：如果映射的仍是同一页，就抵消刚增加的引用，只更新权限；如果映射的是另一页，就减少旧页引用，在引用归零时释放旧页。最后写入新 PTE，并刷新对应地址的 TLB。

先增加引用、再检查旧映射，可以避免同页重映射时误释放该页。`page_remove` 查找 PTE 时传入 `create=0`，因此不会为删除操作分配页表。目标地址没有页表或有效映射时直接返回，重复删除也不会再次减少引用计数。

#### 最终提示词

完整提示词见 `prompt.md` 的功能模块一。Spec 分别规定了空地址建立映射、同页更新权限、异页替换和页表分配失败时的结果，重点写清引用计数如何变化。Prompt 指定修改位置和可用函数，并要求保留框架测试。

#### 实现迭代过程

本模块的代码实现经历了 1 次迭代。

##### 第一次迭代

根据提示词补全 `page_insert` 和 `page_remove`，并用 `page_remove_pte` 处理旧映射的撤销。实现时先增加新页引用，再判断原映射是否指向同一页；同页重映射时抵消新增引用，替换其他页时减少旧页引用。撤销映射后清空 PTE，并刷新对应地址的 TLB。

**最终结果：**

经过 1 次实现迭代，该模块：

- 编译通过。
- 通过页表自检和 PMM 评分测试，得分为 10/10。
- 同页权限更新、异页替换、共享页引用计数和重复删除的测试结果符合预期。

### 功能模块二：进程创建及初始化

**负责人：** 2412940-曹皓轩

#### 模块功能描述

本模块实现 `alloc_proc`、`copy_mm`、`copy_thread`、`do_fork`、`kernel_thread` 和 `proc_init`：

```c
static struct proc_struct *alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        memset(proc, 0, sizeof(*proc));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->pgdir = boot_pgdir_pa;
        list_init(&proc->list_link);
        list_init(&proc->hash_link);
    }
    return proc;
}

static int copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    assert(current->mm == NULL);
    return 0;
}

static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    *proc->tf = *tf;
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = esp ? esp : (uintptr_t)proc->tf;
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}

int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    if (nr_process >= MAX_PROCESS) {
        return -E_NO_FREE_PROC;
    }
    struct proc_struct *proc = alloc_proc();
    if (proc == NULL) {
        return -E_NO_MEM;
    }
    int ret = -E_NO_MEM;
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
    proc->pid = get_pid();
    hash_proc(proc);
    list_add_before(&proc_list, &proc->list_link);
    nr_process++;
    local_intr_restore(intr_flag);
    wakeup_proc(proc);
    return proc->pid;

bad_stack:
    put_kstack(proc);
bad_proc:
    kfree(proc);
    return ret;
}

int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

void proc_init(void)
{
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++) {
        list_init(&hash_list[i]);
    }
    idleproc = alloc_proc();
    if (idleproc == NULL) {
        panic("cannot allocate idleproc.\n");
    }
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;
    current = idleproc;
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0 || (initproc = find_proc(pid)) == NULL) {
        panic("cannot create initproc.\n");
    }
    set_proc_name(initproc, "init");
}
```

`alloc_proc` 分配 PCB 后先整体清零，再设置 `PROC_UNINIT`、PID 初值 `-1`、内核页表物理地址，并初始化两个链表节点。清零后，`runs`、`need_resched`、`flags` 和上下文寄存器为 0，`mm`、`tf` 等指针为空。

`kernel_thread` 将线程函数放入 `s0`，参数放入 `s1`，并让 `epc` 指向 `kernel_thread_entry`。`SPP=1` 使 `sret` 返回 S 态，`SPIE=1` 使返回后允许中断，构造现场时则清除 `SIE`。`copy_thread` 将 Trap frame 放在新线程内核栈顶部，设置 `a0=0`，并将栈指针指向该现场。`context.ra` 设置为 `forkret`，供第一次上下文切换后使用。

`do_fork` 先检查进程数，再依次申请 PCB、内核栈，准备地址空间和执行现场。栈申请失败时释放 PCB；地址空间准备失败时先释放栈，再释放 PCB。资源准备完成后，函数在关中断期间分配 PID，将线程加入哈希表和全局链表，增加进程数，最后调用 `wakeup_proc`。本实验创建的是共享内核页表的内核线程，`copy_mm` 检查父线程的 `mm` 为空后返回。

`proc_init` 初始化进程链表和 PID 哈希桶，并把当前执行现场设为 idle 线程。idle 的 PID 为 0，复用 `bootstack`，状态设为 `PROC_RUNNABLE`，同时设置 `need_resched`。随后令 `current` 指向 idle，创建 PID 为 1、名称为 `init` 的内核线程。idle 不加入普通进程链表，调度器找不到可运行线程时会直接选中它。

#### 最终提示词

完整提示词见 `prompt.md` 的功能模块二。Spec 列出 PCB 的初值、内核栈和执行现场的位置，以及创建失败时需要释放的资源。Prompt 要求使用已有辅助函数完成线程创建，并保留 `do_exit` 的原有实现。

#### 实现迭代过程

本模块的代码实现经历了 1 次迭代。

##### 第一次迭代

根据提示词补全 PCB 初始化、内核栈申请、线程现场构造和进程登记。构造现场时，将 `context.ra` 设为 `forkret`，将 `context.sp` 指向新线程的 Trap frame，再将 `tf->epc` 设为 `kernel_thread_entry`。创建过程中的资源申请失败时，按申请顺序的逆序释放已取得的资源；完成现场准备后再登记 PID、加入链表并唤醒线程。

**最终结果：**

经过 1 次实现迭代，该模块：

- 编译通过。
- 通过 Process Create 评分测试，得分为 10/10。
- idle 的 PID、内核栈和状态，以及 init 的 PID、父线程、上下文和可运行状态均通过检查。

### 功能模块三：进程调度与上下文切换

**负责人：** 2412940-曹皓轩

#### 模块功能描述

`cpu_idle` 和 `proc_run` 的实现如下：

```c
void cpu_idle(void)
{
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

void proc_run(struct proc_struct *proc)
{
    if (proc != current) {
        bool intr_flag;
        local_intr_save(intr_flag);
        struct proc_struct *prev = current;
        current = proc;
        // Sv39 uses MODE=8 and the physical root page number in satp.
        write_csr(satp, ((uintptr_t)8 << 60) | (proc->pgdir >> RISCV_PGSHIFT));
        flush_tlb();
        switch_to(&prev->context, &proc->context);
        local_intr_restore(intr_flag);
    }
}
```

`cpu_idle` 在循环中检查调度标志，标志置位时调用 `schedule`。`proc_run` 在目标线程与当前线程不同时执行切换：先保存中断状态并关闭中断，再更新 `current`，写入 Sv39 模式和目标页表物理页号，刷新 TLB，最后调用 `switch_to`。旧线程以后再次被调度时，会从 `switch_to` 返回后的语句继续执行，并恢复原来的中断状态。

调度器实现如下：

```c
void schedule(void)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    current->need_resched = 0;
    list_entry_t *last = current == idleproc ? &proc_list : &current->list_link;
    list_entry_t *le = last;
    struct proc_struct *next = NULL;
    do {
        le = list_next(le);
        if (le != &proc_list) {
            struct proc_struct *proc = le2proc(le, list_link);
            if (proc->state == PROC_RUNNABLE) {
                next = proc;
                break;
            }
        }
    } while (le != last);
    if (next == NULL) {
        next = idleproc;
    }
    next->runs++;
    if (next != current) {
        proc_run(next);
    }
    local_intr_restore(intr_flag);
}
```

`schedule` 先清除当前线程的 `need_resched`。当前线程为 idle 时，从进程链表头部开始查找；否则从当前节点的后继开始，最多遍历一圈，跳过表头，只选择 `PROC_RUNNABLE` 节点。没有找到时选中 idle。选定线程后增加其 `runs`，若目标与当前线程不同，就调用 `proc_run`。这种查找顺序使可运行线程按链表顺序轮流执行；本实验通过空闲循环或显式调用触发调度。

底层上下文切换代码如下：

```asm
#include <riscv.h>

.text
.globl switch_to
switch_to:
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
    ret
```

`a0` 指向旧线程的 `context`，`a1` 指向新线程的 `context`。汇编按照结构体字段顺序保存和恢复 `ra`、`sp`、`s0` 到 `s11`，每个字段占 8 字节，共 112 字节。恢复新线程的 `ra` 后执行 `ret`，就会跳到该线程保存的返回位置。`switch_to` 在函数调用处切换，因此保存被调用者保存寄存器；中断发生时的完整寄存器现场由 Trap frame 保存。

#### 最终提示词

完整提示词见 `prompt.md` 的功能模块三。Spec 写明链表从哪里开始遍历、何时结束，以及找不到可运行线程时如何选择 idle。汇编部分按 `struct context` 列出寄存器偏移。Prompt 分别要求 `schedule` 选择线程、`proc_run` 切换当前线程和页表、`switch_to` 保存与恢复寄存器。

#### 实现迭代过程

本模块的代码实现经历了 1 次迭代。

##### 第一次迭代

根据提示词补全空闲循环、调度器、进程切换和 `switch_to` 汇编。调度器从当前线程的后继开始查找可运行线程，找不到时选择 idle。`proc_run` 在关中断期间更新 `current`、按 Sv39 格式切换页表，再调用 `switch_to`。汇编按 `struct context` 的字段顺序保存和恢复 `ra`、`sp`、`s0` 到 `s11`。

**最终结果：**

经过 1 次实现迭代，该模块：

- 编译通过。
- 通过 Schedule 评分测试，得分为 10/10。
- 评分程序创建的两个额外线程均能经过调度运行并恢复执行。
- init 线程打印了预期的 PID、名称和 `Hello world!!`，完成从 idle 到 init 的切换。

### 扩展练习 Challenge

**负责人：** 2412940-曹皓轩

#### Challenge 1：`local_intr_save` 与 `local_intr_restore` 如何实现开关中断？

`local_intr_save` 记录当前 CPU 的 S 态中断使能状态并关闭中断，`local_intr_restore` 根据保存的状态恢复。`kern/sync/sync.h` 中的实现如下：

```c
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

`SSTATUS_SIE` 对应 `sstatus` 的 SIE 位。CPU 在 S 态执行时，SIE 为 1 允许响应已使能的 S 态中断，为 0 时屏蔽这些中断。具体中断还需要满足 `sie` 使能、挂起和委派条件。`kern/driver/intr.c` 通过以下函数修改 SIE：

```c
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
```

`set_csr` 用 `csrrs` 设置指定的位，`clear_csr` 用 `csrrc` 清除指定的位，其他位保持原值。临界区中的执行过程如下：

1. `local_intr_save(intr_flag)` 读取 SIE。原来允许中断时关闭中断并保存 `intr_flag=1`；原来已关闭时保存 `intr_flag=0`。
2. 在中断关闭期间执行临界区代码，防止当前 CPU 的可屏蔽 S 态中断处理流程打断共享状态更新。
3. `local_intr_restore(intr_flag)` 在标志为 1 时重新开启中断；标志为 0 时不操作，维持原来的关闭状态。该用法要求临界区内不自行开启中断。

`proc_run` 需要连续更新 `current`、页表和寄存器现场。如果中途进入中断处理，这几个状态可能还未对应到同一个线程。关中断后再执行切换，可以避免本核中断处理打断这些更新。

嵌套临界区需要分别保存进入时的状态。假设进入外层前 SIE=1，内外层各使用一个标志变量：

| 执行位置 | 保存的标志 | 执行后 SIE |
|------|------|------|
| 外层 `local_intr_save(outer_flag)` | `outer_flag=1` | 0 |
| 内层 `local_intr_save(inner_flag)` | `inner_flag=0` | 0 |
| 内层 `local_intr_restore(inner_flag)` | 0，不开启 | 0 |
| 外层 `local_intr_restore(outer_flag)` | 1，开启 | 1 |

内层进入时中断已经关闭，因此 `inner_flag=0`，退出时也保持关闭。只有外层退出时，才根据 `outer_flag=1` 重新开启中断。如果退出内层时无条件开启中断，外层尚未完成的操作就失去了保护。

本实验用关中断保护单核环境中的内存管理和进程切换操作。同步异常仍可发生，其他 CPU 也不受本核 SIE 控制，因此多核共享数据还需要锁等同步机制。

#### Challenge 2：不同分页模式及 `get_pte()` 的设计分析

##### 1. Sv32、Sv39、Sv48 有何异同？

Sv32、Sv39 和 Sv48 都通过多级页表完成地址转换。虚拟地址中的各级索引用来逐层查找页表，页内偏移确定页中的具体位置。页表项保存下一级页表地址或物理页地址，并记录权限。三种模式的主要区别如下：

| 比较项 | Sv32 | Sv39 | Sv48 |
|------|------|------|------|
| 对应架构 | RV32 | RV64 | RV64 |
| 虚拟地址有效宽度 | 32 位 | 39 位 | 48 位 |
| 页表级数 | 2 级 | 3 级 | 4 级 |
| 每级索引位数 | 10 位 | 9 位 | 9 位 |
| 页内偏移 | 12 位 | 12 位 | 12 位 |
| 页表项宽度 | 4 字节 | 8 字节 | 8 字节 |
| 每张页表的项数 | 1024 | 512 | 512 |
| 4 KiB 页的地址划分 | 10 + 10 + 12 | 9 + 9 + 9 + 12 | 9 + 9 + 9 + 9 + 12 |

每张页表占 4 KiB。Sv32 的页表项为 4 字节，一页可容纳 `4096 / 4 = 1024 = 2^10` 项，因此每级索引占 10 位。Sv39 和 Sv48 的页表项为 8 字节，一页容纳 `4096 / 8 = 512 = 2^9` 项，因此每级索引占 9 位。Sv39 要求虚拟地址的位 63–39 与位 38 相同，Sv48 要求位 63–48 与位 47 相同。三种模式都支持在较高层设置叶子项来映射大页，当前函数则逐层查找 4 KiB 页对应的 PTE。

##### 2. 为什么 `get_pte()` 中两段代码如此相像？

当前工程采用 Sv39。`mmu.h` 中的索引定义对应以下三级划分：

```text
虚拟地址 la
    PDX1(la) = 位 38–30：定位根页表中的目录项
    PDX0(la) = 位 29–21：定位中间页表中的目录项
    PTX(la)  = 位 20–12：定位末级页表中的 PTE
    PGOFF(la)= 位 11–0 ：页内偏移
```

`kern/mm/pmm.c` 中的现有函数如下：

```c
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}
```

第一段代码查找根页表项 `pdep1`，缺少下一级页表时分配中间页表；第二段查找中间页表项 `pdep0`，缺少下一级页表时分配末级页表。两段都要检查有效位、判断 `create`、申请物理页、设置引用计数、清零页表，最后将新页表的物理页号写入父目录项。

两段代码相似，是因为从根页表到末级页表，每下降一级都要根据地址索引找到目录项，并取得下一级页表地址。两处使用的层级和索引不同，分配及初始化方法相同。函数最后根据 `PTX(la)` 返回末级 PTE 的内核虚拟地址，数据页的映射则由 `page_insert` 完成。

对于 4 KiB 页，Sv32 从根页表下降一次就到达末级页表，所以需要一段这样的处理；Sv39 下降两次，Sv48 下降三次。N 级页表需要经过 N−1 次下级页表查找或创建。

##### 3. 查找与页表页分配放在同一个函数中是否合适？是否需要拆开？

`create` 参数让调用者可以选择只查找，或在查找时创建缺少的页表：

- `get_pte(pgdir, la, 0)` 只查找，缺少任一级页表就返回 `NULL`。`page_remove` 使用这种方式，避免撤销映射时申请新页表。
- `get_pte(pgdir, la, 1)` 查找并按需创建下层页表。`page_insert` 使用这种方式，无需在调用侧重复处理目录遍历和页表初始化。

我认为本实验可以保留这种写法。页表层数固定，`page_insert` 和 `page_remove` 又能通过 `create` 选择各自需要的行为，将遍历和初始化放在同一个函数中能减少调用处的重复代码。

这种写法需要注意分配失败后的状态。`create=0` 时，`NULL` 表示缺少页表；`create=1` 时，`NULL` 也可能表示内存不足。如果第一段已经创建中间页表，而第二段申请末级页表失败，函数会保留刚挂接的空中间页表。虽然目标数据页还没有映射，页表结构已经发生变化，调用者处理失败时需要考虑这一点。

如果要区分查找失败和分配失败，或者统一处理页表分配后的回收，可以拆成两个接口。例如，`lookup_pte` 只查找，`ensure_pte` 负责创建缺少的页表并返回错误码：

```c
/* 设计示意，不改变本实验现有接口。 */
pte_t *lookup_pte(pde_t *pgdir, uintptr_t la);
int ensure_pte(pde_t *pgdir, uintptr_t la, pte_t **ptep_store);
```

拆分后，调用者可以直接从接口判断是否会分配内存，创建接口也能集中处理错误和页表回收。两个函数可以共用内部遍历代码。本实验的两个调用点使用 `create` 已经足够清楚，因此保留现有函数；后续需要更细的错误处理时，再拆分这两个操作。

---

## 五、测试与验证

编译并在 QEMU 中运行后，内核依次通过 `check_alloc_page`、`check_pgdir`、`check_boot_pgdir`、`kmalloc_init`、`check_vma_struct` 和 `check_vmm`，随后建立线程并完成 idle 到 init 的切换。

实验提供的评分程序检查结果如下：

| 测试项目 | 验证内容 | 结果 |
|------|------|------|
| PMM | 同页重复映射、权限更新、异页替换、引用计数与重复删除 | 10/10 |
| Process Create | idle/init 字段、线程状态、父线程和首次运行上下文 | 10/10 |
| Schedule | 两个附加线程轮转执行，以及 init 预期输出 | 10/10 |
| 合计 | 三项评分全部通过 | 30/30 |

**测试截图：**

下图根据实际评分日志生成，对应文本保存在 `grade.log`，三项测试得分均为 10/10。

![Lab4 评分输出](./image/grade.png)

下图根据普通内核运行日志生成，对应文本保存在 `qemu.log`。init 打印了 PID、名称和传入参数，说明调度器已切换到新线程并执行线程函数。init 返回后，框架中的 `do_exit` 调用 `panic`，打印 `process exit!!` 并进入调试监视器。

![Lab4 QEMU 运行输出](./image/qemu.png)

---

## 六、实验总结与收获

### 对操作系统的理解

| 实验知识点 | 对应 OS 原理 | 联系和差异 |
|------|------|------|
| PTE、引用计数与 TLB 同步 | 分页、共享页和地址翻译缓存一致性 | 修改内存中的页表后，还需要使硬件缓存失效；实验使用单核本地 TLB 刷新，多核还需要跨核同步。 |
| PCB、内核栈、context、trapframe | 进程表示与处理器现场 | PCB 管理线程资源，context 供函数调用边界切换，trapframe 供中断返回；线程对象与 CPU 现场各有职责。 |
| 内核线程共享页表 | 进程与线程的资源共享 | 实验中的线程共享内核地址空间；一般用户进程还需要独立地址空间与权限隔离。 |
| runnable 链表轮转 | CPU 调度策略 | 实验按链表循环选取线程，调度点由空闲循环或显式调用提供；时间片抢占还需要定时器驱动调度。 |
| 保存并恢复中断状态 | 临界区互斥与原子性 | 单核关中断避免本核被打断，不能直接代替多核自旋锁。 |

本实验之外，OS 原理中还包括用户态地址空间隔离、系统调用、按需调页、页面置换、写时复制、时间片抢占、多核调度，以及进程退出后的等待和资源回收。用户进程需要独立的地址空间和系统调用入口；按需调页、页面置换和写时复制依赖缺页异常处理；时间片抢占需要时钟中断触发调度；进程回收则要记录退出状态，并由父进程等待后释放资源。

### AI 协作开发的经验

写 Spec 时，我把要求落实到具体字段和执行结果上。页映射要写清引用计数如何变化，线程创建要确定 Trap frame 在栈中的位置和创建失败后的清理顺序，调度要确定链表遍历的起点、终点和寄存器偏移。相比笼统地要求完成一个模块，这些条件更容易检查，也能让生成的代码与现有框架对应。

检查生成代码时，我重点核对了同页重映射是否误减引用、线程创建失败是否释放已申请资源，以及汇编保存顺序是否与结构体一致。随后结合自检和评分结果确认这些代码能够完成页映射、线程创建与调度。

---
