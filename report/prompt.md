## 功能模块一：虚拟内存页操作

[PROMPT]
**任务**：在 pmm.c 的 LAB4 function1 标记处补全 page_insert、page_remove 及必要的内部撤销辅助函数。

**操作要求**：只在要求补全的位置添加实现，保留原框架、所有已有注释和函数接口，将 YOUR CODE 替换为 2412940；不修改测试、不实现后续实验功能。

**输出要求**：直接修改代码，并以框架测试验证行为。

[RELY]
get_pte、pte_create、pte2page、page_ref_inc/dec、free_page、tlb_invalidate，以及现有 check_pgdir/check_boot_pgdir。

[GUARANTEE]
实现指导书列出的本模块全部接口，维持已有调用链和数据布局。

[SPECIFICATION]
**Pre-Condition**：内存管理和本模块依赖的结构、接口已准备好；输入指针来自框架的合法对象。

**Post-Condition**：建立或更新指定地址的映射；撤销映射后引用计数和 TLB 同步。

**Cases**：同页重复插入只更新权限，引用计数不变；不同页替换释放旧引用；删除不存在的映射安静返回；get_pte 失败返回 -E_NO_MEM，不修改目标页引用计数。

**Requirements**：使用当前工程的 RISC-V 64 位定义，临界区恢复进入前的中断状态；保留原有测试和注释。

## 功能模块二：进程创建及初始化

[PROMPT]
**任务**：在 proc.c 的 LAB4 function2 标记处实现 alloc_proc、copy_mm、copy_thread、do_fork、kernel_thread、proc_init。

**操作要求**：只在要求补全的位置添加实现，保留原框架、所有已有注释和函数接口，将 YOUR CODE 替换为 2412940；不修改测试、不实现后续实验功能。

**输出要求**：直接修改代码，并以框架测试验证行为。

[RELY]
proc_struct、trapframe、context、setup_kstack/put_kstack、hash_proc/get_pid、kmalloc/kfree、wakeup_proc、forkret、kernel_thread_entry。

[GUARANTEE]
实现指导书列出的本模块全部接口，维持已有调用链和数据布局。

[SPECIFICATION]
**Pre-Condition**：内存管理和本模块依赖的结构、接口已准备好；输入指针来自框架的合法对象。

**Post-Condition**：初始化 idle/current/init，线程登记到 PID 哈希表和进程链表，唤醒后为 PROC_RUNNABLE；返回 PID 或负错误码。

**Cases**：PCB 清零且 pid=-1，pgdir 为 boot_pgdir_pa；idle PID=0 且复用 bootstack；init PID=1，名字 init，参数 Hello world!!；申请失败释放已申请资源；tf 放栈顶，context.ra=forkret；内核线程不复制用户地址空间。

**Requirements**：使用当前工程的 RISC-V 64 位定义，临界区恢复进入前的中断状态；保留原有测试和注释。

## 功能模块三：调度与上下文切换

[PROMPT]
**任务**：在 function3 标记处补全 cpu_idle、proc_run、schedule 和 switch_to 汇编。

**操作要求**：只在要求补全的位置添加实现，保留原框架、所有已有注释和函数接口，将 YOUR CODE 替换为 2412940；不修改测试、不实现后续实验功能。

**输出要求**：直接修改代码，并以框架测试验证行为。

[RELY]
proc_list、current、idleproc、local_intr_save/restore、PROC_RUNNABLE、context 中 ra/sp/s0-s11 布局及 Sv39 页表。

[GUARANTEE]
实现指导书列出的本模块全部接口，维持已有调用链和数据布局。

[SPECIFICATION]
**Pre-Condition**：内存管理和本模块依赖的结构、接口已准备好；输入指针来自框架的合法对象。

**Post-Condition**：轮转选择可运行线程，原子更新 current、satp 和上下文，首次切换经 forkret 到目标函数。

**Cases**：current 为 idle 从表头找，否则从其后继开始；遍历一圈无就绪线程则选 idle；清除 need_resched 并累加 runs；目标相同时不切换；保存恢复 14 个上下文寄存器。

**Requirements**：使用当前工程的 RISC-V 64 位定义，临界区恢复进入前的中断状态；保留原有测试和注释。
