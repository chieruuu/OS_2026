#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <sync.h>

//pre define
struct mm_struct;

// the virtual continuous memory area(vma), [vm_start, vm_end), 
// addr belong to a vma means  vma.vm_start<= addr <vma.vm_end 
// 每个 VMA 描述 [vm_start, vm_end) 内一致的区域权限，不直接保存物理页号。



/**
 * `struct vma_struct` 描述什么？
 * 
 * 一个 VMA 描述一段连续的虚拟地址区间 `[vm_start, vm_end)`，包含起点，不包含终点。
 * `vm_flags` 记录 `VM_READ`、`VM_WRITE` 和 `VM_EXEC` 等区域权限，`vm_mm` 指向所属的 `mm_struct`，`list_link` 把这个区域挂入 `mmap_list`。
 * `vma_create()` 设置区间和权限。`insert_vma_struct()` 按起始地址找到插入位置，检查新区域与前后区域没有重叠，
 * 再设置 `vm_mm`、挂入链表并增加 `map_count`。VMA 记录整个区域的范围和权限；其中某一页映射到哪个物理页，由该页的 PTE 记录。
 */

struct vma_struct {
    struct mm_struct *vm_mm; // the set of vma using the same PDT 
    uintptr_t vm_start;      // start addr of vma      
    uintptr_t vm_end;        // end addr of vma, not include the vm_end itself
    uint32_t vm_flags;       // flags of vma
    list_entry_t list_link;  // linear list link which sorted by start addr of vma
};

#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

#define VM_READ                 0x00000001
#define VM_WRITE                0x00000002
#define VM_EXEC                 0x00000004


// `mm_struct` 管理一个地址空间的虚拟内存区域，包含 VMA 链表、VMA 查找缓存、页目录指针和区域数量等信息，属于内存管理状态。
/**
 * 一个 `mm_struct` 管理一个虚拟地址空间，记录这个地址空间有哪些虚拟内存区域，以及使用哪张根页表。
 * | 字段 | 作用 |
 * |---|---|
 * | `mmap_list` | VMA 链表的表头，区域按起始地址升序排列。 |
 * | `mmap_cache` | 缓存上一次成功查到的 VMA，连续访问同一区域时可直接命中。 |
 * | `pgdir` | 根页表的内核虚拟地址，供内核访问页表。`proc_struct.pgdir` 保存的是根页表的物理地址。 |
 * | `map_count` | 该地址空间的 VMA 数量。 |
 * | `sm_priv` | 换页管理器的私有数据指针，`mm_create()` 将它初始化为 `NULL`。 |

 * `mm_create()` 建立空链表并把 `map_count` 设为 0。Lab4 的 `idleproc` 和 `initproc` 使用同一张启动页表，各自的 `proc->mm` 为 `NULL`。

 */


// the control struct for a set of vma using the same PDT
// 一个 mm 组织一个地址空间的所有 VMA；区域之间的实际映射由页表记录。
struct mm_struct {
    list_entry_t mmap_list;        // linear list link which sorted by start addr of vma
    struct vma_struct *mmap_cache; // current accessed vma, used for speed purpose
    // 这里是内核可解引用的根页表指针，与 proc_struct.pgdir 的物理地址不同。
    pde_t *pgdir;                  // the PDT of these vma
    int map_count;                 // the count of these vma
    void *sm_priv;                 // the private data for swap manager
};

struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags);
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);

void vmm_init(void);

int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);

extern volatile unsigned int pgfault_num;
extern struct mm_struct *check_mm_struct;
#endif /* !__KERN_MM_VMM_H__ */

