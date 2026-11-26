# Lab 5 最终提示词

以下提示词按照实验指导书的四个功能模块、Challenge 和 GDB 分支任务组织。统一要求：沿用框架接口，把 Lab 5 标记中的 YOUR CODE 替换为 2412940；保留已导入的 Lab 2–4 实现；直接修改代码，在 WSL 编译并运行测试。报告沿用当前模板，以实际输出说明结果。

## 功能模块一：用户态系统调用

[PROMPT]
**任务**：补全 kern/syscall/syscall.c 的各个 sys_* 包装和 syscall 分发表入口，更新 exception_handler，接通用户 ecall、内核处理与 sret 返回。按当前 RV64 ABI 检查用户库内联汇编。

**操作要求**：沿用已有接口，将 YOUR CODE 替换为 2412940，使用当前工程中的结构与辅助函数。

**输出要求**：直接修改对应文件，在 WSL 中验证，并在报告中解释核心代码及实际结果。

[RELY]
current->tf、trapframe、libs/unistd.h、do_exit/do_fork/do_wait/do_execve/do_yield/do_kill、cputchar、print_pgdir、__alltraps、__trapret。

[GUARANTEE]
用户通过 a0 传编号、a1–a5 传参数；内核在 a0 返回结果。系统调用返回从 ecall 后一条指令继续。

[SPECIFICATION]
**Pre-Condition**：陷入现场已保存到当前进程的 trapframe。

**Post-Condition**：用户 ecall 的 epc 增加 4；合法编号分发到对应函数，空槽和越界编号返回 -E_INVAL；返回值不越界写入用户栈。

**Cases**：getpid 返回当前 PID；fork 父进程返回子 PID，子进程返回 0；wait/exec 的指针由用户内存检查约束；exit 不返回；非法用户访存结束该进程。

**Requirements**：保持编号约定；返回 U 态前在 sscratch 保存内核栈顶，S 态陷入期间 sscratch 为 0。

## 功能模块二：创建与加载第一个用户进程

[PROMPT]
**任务**：更新 alloc_proc、kernel_thread、copy_thread、proc_init，完成 init_main、user_main、kernel_execve、do_execve、load_icode、do_wait。让默认 exit 程序从嵌入内核的 ELF 进入 U 态，再由父进程回收。

**操作要求**：沿用已有接口，将 YOUR CODE 替换为 2412940，使用当前工程中的结构与辅助函数。

**输出要求**：直接修改对应文件，在 WSL 中验证，并在报告中解释核心代码及实际结果。

[RELY]
KERNEL_EXECVE/KERNEL_EXECVE2、elfhdr/proghdr、mm_create/mm_map、setup_pgdir、pgdir_alloc_page、USTACKTOP/USTACKSIZE、forkrets、SSTATUS_SPP/SPIE、set_links/remove_links。

[GUARANTEE]
idle PID=0、init PID=1、首个用户进程 PID=2。用户程序使用独立页表、用户栈和内核栈；wait 回收僵尸的 PCB 和内核栈。

[SPECIFICATION]
**Pre-Condition**：物理内存、内核映射和调度器已初始化，输入 ELF 的长度可核对。

**Post-Condition**：按 PT_LOAD 创建 VMA、分配清零页面、复制文件字节，BSS 保持为零；tf.epc 为 ELF 入口，tf.sp 为 USTACKTOP，SPP=0、SPIE=1；kernel_execve 经 forkrets 到 sret。

**Cases**：无效 ELF、越界段和分配失败清理已建立的地址空间；用户 exec 先保存名称和 ELF，再释放旧 mm；wait(pid) 只接收直接子进程，wait(0) 等待任一子进程，无子进程返回 -E_BAD_PROC，无效退出码指针返回 -E_INVAL。

**Requirements**：用户栈最低页作为保护区，首次预分配顶部四页；用户 exec 成功后进入新入口，init 等待全部后代结束并检查物理页回收。

## 功能模块三：复制用户进程

[PROMPT]
**任务**：完成 copy_mm、do_fork、dup_mmap、copy_range，保留普通页复制路径，并将 COW 作为默认 fork 策略。登记 PID、哈希表、进程链表和父子关系，复制 trapframe 和内核栈现场。

**操作要求**：沿用已有接口，将 YOUR CODE 替换为 2412940，使用当前工程中的结构与辅助函数。

**输出要求**：直接修改对应文件，在 WSL 中验证，并在报告中解释核心代码及实际结果。

[RELY]
alloc_proc、setup_kstack、copy_thread、mm_count、CLONE_VM、lock_mm/unlock_mm、set_links、page_insert、get_pte、page2kva、page_ref、tlb_invalidate。

[GUARANTEE]
CLONE_VM 共享整个 mm；普通 fork 建立独立 mm 和页表。两种情况下子进程都获得独立 PCB、内核栈和上下文。

[SPECIFICATION]
**Pre-Condition**：父进程为 current；传入有效 trapframe。

**Post-Condition**：父子继承相同虚拟地址及内容，fork 返回值区分父子。VMA 顺序和权限保持；失败路径释放子进程已申请资源。

**Cases**：内核线程 mm=NULL；共享 mm 时增加 mm_count；普通 fork 复制 VMA，并通过 share 参数选择 COW 或深拷贝；原本只读页共享后仍只读；嵌套 fork 保留 COW 位。

**Requirements**：LAB5_EAGER_COPY 可切回深拷贝验证；页表权限变化后刷新 TLB；回收页目录前确认整个目录已无有效项。

## 功能模块四：其余系统调用

[PROMPT]
**任务**：实现 do_exit、do_yield、do_kill，配合 do_wait、时钟中断和调度器，完成进程生命周期。

**操作要求**：沿用已有接口，将 YOUR CODE 替换为 2412940，使用当前工程中的结构与辅助函数。

**输出要求**：直接修改对应文件，在 WSL 中验证，并在报告中解释核心代码及实际结果。

[RELY]
PROC_ZOMBIE、PROC_SLEEPING、WT_CHILD、WT_INTERRUPTED、PF_EXITING、exit_mmap、put_pgdir、mm_destroy、wakeup_proc、schedule、local_intr_save/restore。

[GUARANTEE]
退出释放地址空间，wait 完成 PCB 和内核栈回收；父进程等待可以被子进程退出或 kill 唤醒。

[SPECIFICATION]
**Pre-Condition**：进程树关系已由 set_links 建立。

**Post-Condition**：exit 记录退出码、转僵尸、唤醒等待父进程，子进程托管到 init，再调度离开；yield 设置 need_resched；kill 设置 PF_EXITING 并唤醒可中断等待。

**Cases**：idle/init 不能退出；无效 PID 和重复 kill 返回错误；父进程先退出时 init 接管子进程；正在自旋的用户程序可由时钟抢占并进入退出检查。

**Requirements**：进程状态和树关系更新放在关中断临界区；清理 mm 前切换到内核页表；全部用户进程结束后进入内核监视器，供评分 GDB 在 readline 停止。

## Challenge：COW 与 Dirty COW 原理模拟

[PROMPT]
**任务**：用 PTE 软件位实现 COW，补上写缺页处理、内核 copy_to_user 拆页、引用计数和 TLB 更新。编写内核状态测试与真实用户 fork 测试，并提供状态转换说明。参考 Dirty COW 修复，模拟绕过 COW 的错误物理别名写入，再验证修复路径。

**操作要求**：沿用已有接口，将 YOUR CODE 替换为 2412940，使用当前工程中的结构与辅助函数。

**输出要求**：直接修改对应文件，在 WSL 中验证，并在报告中解释核心代码及实际结果。

[RELY]
PTE_COW/PTE_W/PTE_U、VM_WRITE、page_ref、copy_range、cow_resolve、do_pgfault、copy_to_user、page_insert、nr_free_pages。

[GUARANTEE]
fork 后父子共享物理页并清除 W；任一进程写入时只修改自己的私有页；原本只读区域不能因缺页而获得写权限。

[SPECIFICATION]
**Pre-Condition**：每个可写私有映射都有准确引用计数；COW 标志只用于原本可写的页。

**Post-Condition**：ref>1 的 COW 写分配一页并复制 4096 字节；ref=1 直接恢复 W；撤销最后引用释放页；分配失败保留原 PTE。

**Cases**：重复 fork、父子分别写、跨页内核回填、wait 退出码位于共享数据页、只读 VMA 写拒绝、内存耗尽、进程退出全量回收。

**Requirements**：正式 copy_to_user 每页重新查找 PTE，拆页后才获取可写物理别名；错误路径只在内核测试中以受控映射演示。准确说明模拟与 Linux 原漏洞的接口差异。

## 分支任务：双重 GDB

[PROMPT]
**任务**：在 WSL 按指导书使用三个终端：A 通过 make debug 独立运行带符号的 QEMU，B 通过 make gdb 调试 ucore，C 用 host GDB 附加到 QEMU 的 Linux 进程。加载 hello 用户 ELF 符号，选择 getpid 的 ecall，记录 trans_ecall、riscv_cpu_do_interrupt、内核 syscall、trans_sret 和 helper_sret，最后断言回到 ecall+4 且 a0=2。

**操作要求**：沿用已有接口，将 YOUR CODE 替换为 2412940，使用当前工程中的结构与辅助函数。

**输出要求**：直接修改对应文件，在 WSL 中验证，并在报告中解释核心代码及实际结果。

[RELY]
QEMU 6.2 调试构建、riscv64-unknown-elf-gdb、gdb、pexpect、objdump、add-symbol-file、条件断点、寄存器和调用栈。

[GUARANTEE]
列出三个终端的启动命令、进程号获取与 attach 操作；各阶段逐条列出 GDB 命令、终端切换顺序和观察结果。根据当前反汇编确定指令地址，每个阶段分别截图。

[SPECIFICATION]
**Pre-Condition**：QEMU 调试二进制及对应源码已存在，端口 1234 可用，host GDB 具备 ptrace 附加权限。

**Post-Condition**：记录 ecall 前 priv=0、a0=18，异常后 priv=1、scause=8；返回前 SPP=0，返回后 priv=0、PC=ecall+4、a0=2。

**Cases**：用户符号与内核 syscall 同名时按文件定位；只在目标用户调用处停止 host；观察译码阶段和 helper 执行阶段的区别。

**Requirements**：解释 TCG 翻译与执行、与上次地址翻译调试的联系；报告真实调试问题和解决过程。

## 报告格式与测试截图

[PROMPT]
**任务**：按 lab4 分支的 report/report.md 模板编写。每个功能模块依次包含负责人、模块功能描述、核心代码及解释、最终提示词、实现迭代过程和最终结果。基础模块以实际 make qemu 和 make grade 展示，图片集中放在测试与验证。Challenge 先核查框架自带检测，没有专项检测时再补充测试。GDB 按每个观察阶段分别截图。

**操作要求**：沿用已有接口，将 YOUR CODE 替换为 2412940，使用当前工程中的结构与辅助函数。

**输出要求**：直接修改对应文件，在 WSL 中验证，并在报告中解释核心代码及实际结果。

[RELY]
Lab4 原报告、原框架 tools/test_lab5.c 与 tools/grade.sh、make qemu、make grade、COW 专项测试、双重 GDB 的实际运行。

[GUARANTEE]
基础功能由框架运行和评分结果展示；Challenge 说明框架检查范围和补充测试内容；GDB 的八个阶段各有对应现场和原始日志。

[SPECIFICATION]
**Pre-Condition**：读取 Lab4 原报告，核查框架的测试入口和断言，完成对应运行。

**Post-Condition**：报告结构与 Lab4 一致，基础功能使用运行图和评分图，Challenge 使用专项运行图，GDB 分阶段展示，Windows 与 WSL 同步。

**Cases**：make qemu 的默认 exit、make grade 的 13 个程序、框架未覆盖的 COW 专项断言、GDB 的 ecall 与 sret 各阶段。

**Requirements**：截取实际命令执行后的终端窗口；代码与解释对应；使用具体运行记录说明问题与修正。
