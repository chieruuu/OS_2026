#include <vmm.h>
#include <sync.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <riscv.h>
#include <kmalloc.h>

/*
  vmm design include two parts: mm_struct (mm) & vma_struct (vma)
  mm is the memory manager for the set of continuous virtual memory
  area which have the same PDT. vma is a continuous virtual memory area.
  There a linear link list for vma & a redblack link list for vma in mm.
---------------
  mm related functions:
   golbal functions
     struct mm_struct * mm_create(void)
     void mm_destroy(struct mm_struct *mm)
     int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
--------------
  vma related functions:
   global functions
     struct vma_struct * vma_create (uintptr_t vm_start, uintptr_t vm_end,...)
     void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
     struct vma_struct * find_vma(struct mm_struct *mm, uintptr_t addr)
   local functions
     inline void check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
---------------
   check correctness functions
     void check_vmm(void);
     void check_vma_struct(void);
     void check_pgfault(void);
*/

// szx func : print_vma and print_mm
/*
 * 作用：打印指定 VMA 的内容。
 * 实现：输出所属地址空间、区间边界、权限和链表节点地址。
 */
void print_vma(char *name, struct vma_struct *vma)
{
    cprintf("-- %s print_vma --\n", name);
    cprintf("   mm_struct: %p\n", vma->vm_mm);
    cprintf("   vm_start,vm_end: %x,%x\n", vma->vm_start, vma->vm_end);
    cprintf("   vm_flags: %x\n", vma->vm_flags);
    cprintf("   list_entry_t: %p\n", &vma->list_link);
}

/*
 * 作用：打印地址空间及其所有 VMA。
 * 实现：输出区域数，再沿 mmap_list 调用 print_vma 逐项显示。
 */
void print_mm(char *name, struct mm_struct *mm)
{
    cprintf("-- %s print_mm --\n", name);
    cprintf("   mmap_list: %p\n", &mm->mmap_list);
    cprintf("   map_count: %d\n", mm->map_count);
    list_entry_t *list = &mm->mmap_list;
    for (int i = 0; i < mm->map_count; i++)
    {
        list = list_next(list);
        print_vma(name, le2vma(list, list_link));
    }
}

static void check_vmm(void);
static void check_vma_struct(void);
static void check_pgfault(void);

// mm_create -  alloc a mm_struct & initialize it.
/*
 * 作用：建立一个空的地址空间描述符。
 * 实现：分配 mm、初始化 VMA 链表，将页表和缓存指针等置空，区域计数设为 0。
 */

struct mm_struct *
mm_create(void)
{
    // 建立空地址空间描述符；pgdir 尚未绑定具体页表。
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));

    if (mm != NULL)
    {
        list_init(&(mm->mmap_list));
        mm->mmap_cache = NULL;
        mm->pgdir = NULL;
        mm->map_count = 0;
        mm->sm_priv = NULL;
    }
    return mm;
}

// vma_create - alloc a vma_struct & initialize it. (addr range: vm_start~vm_end)
/*
 * 作用：创建描述左闭右开虚拟区间的 VMA。
 * 实现：分配描述符并填写区间和权限，所属 mm 和链表节点交给插入函数处理。
 */
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags)
{
    // 描述左闭右开的虚拟区间及区域权限，创建后还需插入 mm 的链表。
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));

    if (vma != NULL)
    {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
    }
    return vma;
}

// find_vma - find a vma  (vma->vm_start <= addr <= vma_vm_end)
/*
 * 作用：查找 mm 中包含 addr 的 VMA，找不到返回 NULL。
 * 实现：优先查缓存；未命中时遍历 VMA 链表，按左闭右开边界检查，找到后更新缓存。
 */

/**
 * `find_vma(mm, addr)` 怎么查找？
 * 
 * 它返回包含 `addr` 的 VMA。`mm` 为 `NULL` 时直接返回 `NULL`。否则先检查 `mmap_cache`，命中条件是 `vm_start <= addr && addr < vm_end`；
 * 未命中就遍历 `mmap_list`，通过 `le2vma(le, list_link)` 从链表节点找到 VMA，再用同样的条件判断。找到后更新缓存；
 * 没有包含该地址的区域就返回 `NULL`。例如，对于 `[0x1000, 0x2000)`，`0x1000` 属于该区域，`0x2000` 不属于。
 */

struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr)
{
    // 返回真正包含 addr 的区域；区间右端点 vm_end 不属于该区域。
    struct vma_struct *vma = NULL;
    if (mm != NULL)
    {
        // 先查最近命中缓存，连续访问同一区域时无需遍历链表。
        vma = mm->mmap_cache;
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr))
        {
            // 缓存未命中时遍历 mmap_list，用嵌入节点找到 vma_struct。
            bool found = 0;
            list_entry_t *list = &(mm->mmap_list), *le = list;
            while ((le = list_next(le)) != list)
            {
                vma = le2vma(le, list_link);
                if (vma->vm_start <= addr && addr < vma->vm_end)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                vma = NULL;
            }
        }
        if (vma != NULL)
        {
            // 只有查到包含 addr 的 VMA 才更新缓存。
            mm->mmap_cache = vma;
        }
    }
    return vma;
}

// check_vma_overlap - check if vma1 overlaps vma2 ?
/*
 * 作用：检查相邻 VMA 合法且不重叠。
 * 实现：断言两个区间都非空，且前一区间终点不大于后一区间起点。
 */
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
{
    // 相邻区间可首尾相接，但不能重叠；同时检查区间自身非空。
    assert(prev->vm_start < prev->vm_end);
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end);
}

// insert_vma_struct -insert vma in mm's list link
/*
 * 作用：将 VMA 按起始地址插入指定 mm。
 * 实现：查找插入位置，检查与前后区域是否重叠，再登记所属 mm、挂入链表并更新区域数。
 */
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
{
    // 按 vm_start 升序插入，先找到前驱，再检查与前驱和后继的边界。
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list);
    list_entry_t *le_prev = list, *le_next;

    list_entry_t *le = list;
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *mmap_prev = le2vma(le, list_link);
        if (mmap_prev->vm_start > vma->vm_start)
        {
            break;
        }
        le_prev = le;
    }

    le_next = list_next(le_prev);

    /* check overlap */
    if (le_prev != list)
    {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    if (le_next != list)
    {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    // 建立所属地址空间关系，挂入链表并统计区域数量。
    vma->vm_mm = mm;
    list_add_after(le_prev, &(vma->list_link));

    mm->map_count++;
}

// mm_destroy - free mm and mm internal fields
/*
 * 作用：释放 mm 和它管理的 VMA 描述符。
 * 实现：逐个摘下并释放 VMA，最后释放 mm；不在此处遍历页表回收物理映射。
 */
void mm_destroy(struct mm_struct *mm)
{
    // 逐个摘下并释放 VMA 描述符，再释放 mm；本函数不遍历页表回收映射。

    list_entry_t *list = &(mm->mmap_list), *le;
    while ((le = list_next(list)) != list)
    {
        list_del(le);
        kfree(le2vma(le, list_link)); // kfree vma
    }
    kfree(mm); // kfree mm
    mm = NULL;
}

// vmm_init - initialize virtual memory management
//          - now just call check_vmm to check correctness of vmm
/*
 * 作用：启动虚拟内存管理模块的自检。
 * 实现：调用 check_vmm 验证 VMA 操作并输出结果。
 */
void vmm_init(void)
{
    check_vmm();
}

// check_vmm - check correctness of vmm
/*
 * 作用：组织虚拟内存管理自检。
 * 实现：执行 check_vma_struct；当前 Lab4 中缺页检测调用保留在注释里。
 */
static void
check_vmm(void)
{
    check_vma_struct();
    // check_pgfault();

    cprintf("check_vmm() succeeded.\n");
}

/*
 * 作用：验证 VMA 排序、查找与边界处理。
 * 实现：按不同顺序建立区域，断言链表顺序和命中范围正确，再销毁测试地址空间。
 */
static void
check_vma_struct(void)
{
    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = 10, step2 = step1 * 10;

    int i;
    for (i = step1; i >= 1; i--)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    for (i = step1 + 1; i <= step2; i++)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 1; i <= step2; i++)
    {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    for (i = 5; i <= 5 * step2; i += 5)
    {
        struct vma_struct *vma1 = find_vma(mm, i);
        assert(vma1 != NULL);
        struct vma_struct *vma2 = find_vma(mm, i + 1);
        assert(vma2 != NULL);
        struct vma_struct *vma3 = find_vma(mm, i + 2);
        assert(vma3 == NULL);
        struct vma_struct *vma4 = find_vma(mm, i + 3);
        assert(vma4 == NULL);
        struct vma_struct *vma5 = find_vma(mm, i + 4);
        assert(vma5 == NULL);

        assert(vma1->vm_start == i && vma1->vm_end == i + 2);
        assert(vma2->vm_start == i && vma2->vm_end == i + 2);
    }

    for (i = 4; i >= 0; i--)
    {
        struct vma_struct *vma_below_5 = find_vma(mm, i);
        if (vma_below_5 != NULL)
        {
            cprintf("vma_below_5: i %x, start %x, end %x\n", i, vma_below_5->vm_start, vma_below_5->vm_end);
        }
        assert(vma_below_5 == NULL);
    }

    mm_destroy(mm);

    cprintf("check_vma_struct() succeeded!\n");
}
