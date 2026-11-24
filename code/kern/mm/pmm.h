#ifndef __KERN_MM_PMM_H__
#define __KERN_MM_PMM_H__

#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <atomic.h>
#include <assert.h>

/* fork flags used in do_fork*/
#define CLONE_VM 0x00000100     // set if VM shared between processes
#define CLONE_THREAD 0x00000200 // thread group

// pmm_manager is a physical memory management class. A special pmm manager - XXX_pmm_manager
// only needs to implement the methods in pmm_manager class, then XXX_pmm_manager can be used
// by ucore to manage the total physical memory space.
struct pmm_manager
{
    const char *name;                                 // XXX_pmm_manager's name
    void (*init)(void);                               // initialize internal description&management data structure
                                                      // (free block list, number of free block) of XXX_pmm_manager
    void (*init_memmap)(struct Page *base, size_t n); // setup description&management data structcure according to
                                                      // the initial free physical memory space
    struct Page *(*alloc_pages)(size_t n);            // allocate >=n pages, depend on the allocation algorithm
    void (*free_pages)(struct Page *base, size_t n);  // free >=n pages with "base" addr of Page descriptor structures(memlayout.h)
    size_t (*nr_free_pages)(void);                    // return the number of free pages
    void (*check)(void);                              // check the correctness of XXX_pmm_manager
};

extern const struct pmm_manager *pmm_manager;
extern pde_t *boot_pgdir_va;
extern const size_t nbase;
extern uintptr_t boot_pgdir_pa;

void pmm_init(void);

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);

#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store);
void page_remove(pde_t *pgdir, uintptr_t la);
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);

void tlb_invalidate(pde_t *pgdir, uintptr_t la);
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm);

void print_pgdir(void);

/* *
 * PADDR - takes a kernel virtual address (an address that points above KERNBASE),
 * where the machine's maximum 256MB of physical memory is mapped and returns the
 * corresponding physical address.  It panics if you pass it a non-kernel virtual address.
 * */
#define PADDR(kva)                                                 \
    ({                                                             \
        uintptr_t __m_kva = (uintptr_t)(kva);                      \
        if (__m_kva < KERNBASE)                                    \
        {                                                          \
            panic("PADDR called with invalid kva %08lx", __m_kva); \
        }                                                          \
        __m_kva - va_pa_offset;                                    \
    })

/* *
 * KADDR - takes a physical address and returns the corresponding kernel virtual
 * address. It panics if you pass an invalid physical address.
 * */
#define KADDR(pa)                                                \
    ({                                                           \
        uintptr_t __m_pa = (pa);                                 \
        size_t __m_ppn = PPN(__m_pa);                            \
        if (__m_ppn >= npage)                                    \
        {                                                        \
            panic("KADDR called with invalid pa %08lx", __m_pa); \
        }                                                        \
        (void *)(__m_pa + va_pa_offset);                         \
    })

extern struct Page *pages;
extern size_t npage;
extern uint_t va_pa_offset;

/*
 * 作用：将 Page 描述符转换为物理页号。
 * 实现：求它在 pages 数组中的下标，再加上物理内存起始页号 nbase。
 */
static inline ppn_t
page2ppn(struct Page *page)
{
    // pages 数组从物理内存基址对应页开始，故数组下标要加 nbase。
    return page - pages + nbase;
}

/*
 * 作用：取得物理页的起始地址。
 * 实现：将 Page 转为页号，再左移 PGSHIFT 位。
 */
static inline uintptr_t
page2pa(struct Page *page)
{
    return page2ppn(page) << PGSHIFT;
}

/*
 * 作用：根据物理地址找到对应的 Page。
 * 实现：取得页号并检查上界，再减去 nbase 作为 pages 数组下标。
 */
static inline struct Page *
pa2page(uintptr_t pa)
{
    if (PPN(pa) >= npage)
    {
        panic("pa2page called with invalid pa");
    }
    return &pages[PPN(pa) - nbase];
}

/*
 * 作用：取得指向物理页的内核虚拟地址。
 * 实现：先从 Page 算出物理基址，再通过 KADDR 转换。
 */
static inline void *
page2kva(struct Page *page)
{
    return KADDR(page2pa(page));
}

/*
 * 作用：根据内核虚拟地址找到其物理页描述符。
 * 实现：先用 PADDR 还原物理地址，再通过 pa2page 查找。
 */
static inline struct Page *
kva2page(void *kva)
{
    return pa2page(PADDR(kva));
}

/*
 * 作用：从有效页表项得到它指向的物理页描述符。
 * 实现：检查 PTE_V，再提取物理地址并用 pa2page 转换。
 */
static inline struct Page *
pte2page(pte_t pte)
{
    // 有效 PTE 中提取物理地址，再定位到全局 pages 数组中的描述符。
    if (!(pte & PTE_V))
    {
        panic("pte2page called with invalid pte");
    }
    return pa2page(PTE_ADDR(pte));
}

/*
 * 作用：从目录项得到下级页表的物理页描述符。
 * 实现：用 PDE_ADDR 解出物理地址，再调用 pa2page。
 */
static inline struct Page *
pde2page(pde_t pde)
{
    return pa2page(PDE_ADDR(pde));
}

/*
 * 作用：读取物理页的引用计数。
 * 实现：直接返回 Page 中的 ref 字段。
 */
static inline int
page_ref(struct Page *page)
{
    return page->ref;
}

/*
 * 作用：初始化或设置物理页引用计数。
 * 实现：将传入的值写入 Page 的 ref 字段。
 */
static inline void
set_page_ref(struct Page *page, int val)
{
    page->ref = val;
}

/*
 * 作用：给物理页增加一次引用。
 * 实现：将 ref 加一并返回新计数。
 */
static inline int
page_ref_inc(struct Page *page)
{
    // page_insert 为新增映射增加引用，同页替换时会抵消这一次增加。
    page->ref += 1;
    return page->ref;
}

/*
 * 作用：撤销物理页的一次引用。
 * 实现：将 ref 减一并返回新计数，供调用者判断能否释放物理页。
 */
static inline int
page_ref_dec(struct Page *page)
{
    // 返回减少后的计数，page_remove_pte 用它判断数据页能否释放。
    page->ref -= 1;
    return page->ref;
}

/*
 * 作用：刷新本核全部地址翻译。
 * 实现：执行无地址参数的 sfence.vma，供页表切换等操作使用。
 */
static inline void flush_tlb()
{
    // 不传地址的 sfence.vma 刷新本核全部地址翻译，供页表切换使用。
    asm volatile("sfence.vma");
}

// construct PTE from a page and permission bits
/*
 * 作用：按物理页号和权限构造有效页表项。
 * 实现：将页号移入 PPN 字段，再合并标志位和 PTE_V。
 */
static inline pte_t pte_create(uintptr_t ppn, int type)
{
    // PPN 从 PTE 的第 10 位开始，低位保存权限及有效位。
    return (ppn << PTE_PPN_SHIFT) | PTE_V | type;
}

/*
 * 作用：构造指向下级页表的有效目录项。
 * 实现：调用 pte_create，设置有效位而不设置读、写或执行位。
 */
static inline pte_t ptd_create(uintptr_t ppn)
{
    return pte_create(ppn, PTE_V);
}

extern char bootstack[], bootstacktop[];

#endif /* !__KERN_MM_PMM_H__ */
