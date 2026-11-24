#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

// 将已初始化或睡眠的线程设为可运行；这里只改状态，不立即抢占 CPU。
/*
 * 作用：将指定线程设为可运行。
 * 实现：检查当前状态不是僵尸或已可运行，再写入 PROC_RUNNABLE，等待调度器选中。
 */
void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2412940
/*
 * 作用：选择下一个可运行线程并按需切换。
 * 实现：关本地中断后清除调度请求，从当前节点之后循环查找；没有候选就选 idle，目标变化时调用 proc_run。
 */

 /**
  * 线程切换的执行过程是什么？
  * 
  * `schedule()` 先保存并关闭本地中断，清除当前线程的 `need_resched`，从 `proc_list` 中按链表顺序寻找下一个 `PROC_RUNNABLE` 线程；
  * 找不到时选择 `idleproc`。如果目标线程不同于当前线程，就调用 `proc_run()`。
  * 
  * `proc_run()` 保存中断状态，更新 `current`，把 Sv39 模式和目标线程根页表的物理页号写入 `satp` 并刷新 TLB，
  * 然后调用汇编函数 `switch_to()`。`switch_to()` 把旧线程的 `ra`、`sp`、`s0` 至 `s11` 写入旧 `context`，
  * 再从新 `context` 恢复这些寄存器并返回。新线程由此从保存位置继续执行；首次运行的线程则从预设的 `forkret` 开始。
  * 切换返回后按保存值恢复中断状态。
  */

  /**
   * 进程被调度后，到执行目标函数的完整路径是什么？
   * 
   * 调度器调用 `proc_run()`，它更新 `current`、切换 `satp` 并刷新 TLB，
   * 然后调用汇编 `switch_to()` 保存旧进程的 `context`、恢复目标进程的 `context`。
   * 对已经运行过的进程，`switch_to()` 的 `ret` 返回到它上次暂停的调用位置，继续执行原来的代码。
   * 对首次运行的新内核线程，初始 `context.ra` 被设为 `forkret`，
   * 因此路径是 `switch_to → forkret → forkrets → __trapret → sret → kernel_thread_entry → 目标线程函数`。
   * `kernel_thread_entry` 从 `s1` 取参数放入 `a0`，再通过 `s0` 调用目标函数。
   */

void schedule(void)
{
    // 保存中断状态，防止选择目标和切换现场期间被本核中断打断。
    bool intr_flag;
    local_intr_save(intr_flag);
    // 本次调度已响应请求，清除旧标志。
    current->need_resched = 0;
    // idle 不挂入 proc_list，所以从表头起步；其他线程从自己的节点起步。
    list_entry_t *last = current == idleproc ? &proc_list : &current->list_link;
    list_entry_t *le = last;
    struct proc_struct *next = NULL;
    // 从下一个节点开始循环扫描，遇到第一个可运行线程即选中。
    do {
        le = list_next(le);
        // 表头只是哨兵，不属于任何 proc_struct。
        if (le != &proc_list) {
            struct proc_struct *proc = le2proc(le, list_link);
            if (proc->state == PROC_RUNNABLE) {
                next = proc;
                break;
            }
        }
    } while (le != last);
    // 完整扫描一圈无可运行线程时回到 idle；也可能重新选中当前线程。
    if (next == NULL) {
        next = idleproc;
    }
    // 记录被选中次数；只有目标不同于当前线程时才执行寄存器切换。
    next->runs++;
    if (next != current) {
        proc_run(next);
    }
    // 按进入调度器前的状态恢复，嵌套关中断不会被提前打开。
    local_intr_restore(intr_flag);
}
