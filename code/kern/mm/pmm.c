#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
pde_t *boot_pgdir_va = NULL;
// physical address of boot-time page directory
uintptr_t boot_pgdir_pa;

// physical memory management
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - initialize a pmm_manager instance
/*
 * 作用：选择并初始化物理页分配器。
 * 实现：把 pmm_manager 指向默认分配器，输出名称，再调用它的 init。
 */
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
/*
 * 作用：登记从 base 开始的 n 个空闲物理页。
 * 实现：通过分配器的 init_memmap 初始化描述符，并把这段内存纳入分配器管理。
 */
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
/*
 * 作用：申请 n 个连续物理页。
 * 实现：在本地关中断期间调用分配器，返回首个 Page 描述符或 NULL，再恢复中断状态。
 */
struct Page *alloc_pages(size_t n)
{
    // 将连续页申请委托给具体物理页分配器，关中断保护其空闲链表。
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
/*
 * 作用：归还从 base 开始的 n 个连续物理页。
 * 实现：关本地中断后调用分配器更新空闲块，再恢复原中断状态。
 */
void free_pages(struct Page *base, size_t n)
{
    // 从 base 开始的 n 页归还分配器，合并空闲块由具体算法完成。
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
/*
 * 作用：取得当前空闲物理页总数。
 * 实现：关本地中断保护查询，从分配器读取页数，恢复中断后返回。
 */
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
/*
 * 作用：建立物理页描述符数组并划分保留区和空闲区。
 * 实现：从设备树读取内存范围，安置 pages 数组，先将页面标记为保留，再登记内核和描述符之后的空闲整页。
 */
static void page_init(void) {
    // 从设备树取得物理内存范围，预留内核和 Page 数组，再登记剩余空闲页。
    extern char end[];
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uintptr_t address_max = ~(uintptr_t)0;

    if (mem_base != DRAM_BASE || mem_size == 0 ||
        mem_size > address_max - mem_base) {
        panic("invalid physical memory range from device tree");
    }
    uintptr_t mem_end = mem_base + mem_size;
    cprintf("physical memory map:\n"
            "  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            (unsigned long)mem_size, (unsigned long)mem_base,
            (unsigned long)(mem_end - 1));

    uintptr_t maxpa = ROUNDDOWN(
        mem_end < (KERNTOP - PHYSICAL_MEMORY_OFFSET) ? mem_end : (KERNTOP - PHYSICAL_MEMORY_OFFSET),
        PGSIZE);
    if (PPN(maxpa) <= nbase) {
        panic("no manageable physical pages");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    uintptr_t kernel_end = (uintptr_t)end;
    if (kernel_end < KERNBASE || kernel_end > address_max - (PGSIZE - 1)) {
        panic("invalid kernel end address");
    }
    uintptr_t pages_va = ROUNDUP(kernel_end, PGSIZE);
    uintptr_t pages_pa = PADDR(pages_va);
    size_t page_count = PPN(maxpa) - nbase;

    // Validate the complete descriptor layout before writing any descriptors.
    if (pages_pa > maxpa ||
        page_count > (maxpa - pages_pa) / sizeof(struct Page)) {
        panic("physical memory cannot hold the page descriptor array");
    }
    size_t descriptor_size = page_count * sizeof(struct Page);
    if (descriptor_size > address_max - pages_va) {
        panic("page descriptor virtual address overflow");
    }
    uintptr_t descriptors_end_pa = pages_pa + descriptor_size;

    npage = PPN(maxpa);
    pages = (struct Page *)pages_va;
    for (size_t i = 0; i < page_count; i++) {
        struct Page *p = &pages[i];
        p->flags = 0;
        SetPageReserved(p);
        set_page_ref(p, 0);
        p->property = 0;
        list_init(&p->page_link);
    }

    // The descriptor array and both alignment gaps remain reserved.
    uintptr_t free_start = ROUNDUP(descriptors_end_pa, PGSIZE);
    if (free_start < maxpa) {
        init_memmap(pa2page(free_start), (maxpa - free_start) / PGSIZE);
    }
}



/*
 * 作用：将启动根页表装入 satp 并选择 Sv39。
 * 实现：将根页表物理地址右移得到页号，与 MODE=8 合并后写入 satp；实际启动入口在 init/entry.S 中完成这一步。
 */
static void enable_paging(void)
{
    write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
}

// boot_map_segment - setup&enable the paging mechanism
// parameters
//  la:   linear address of this memory need to map (after x86 segment map)
//  size: memory size
//  pa:   physical address of this memory
//  perm: permission of this memory
/*
 * 作用：为指定地址区间建立启动映射。
 * 实现：检查虚拟与物理地址的页内偏移一致，对齐区间后逐页取得 PTE 并填写页号和权限。
 */
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    // 将区间按页对齐后逐页填入 PTE；这是启动映射，不通过 page_insert 计数。
    assert(PGOFF(la) == PGOFF(pa));
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - allocate one page using pmm->alloc_pages(1)
// return value: the kernel virtual address of this allocated page
// note: this function is used to get the memory for PDT(Page Directory
// Table)&PT(Page Table)
/*
 * 作用：取一页内存供启动代码使用。
 * 实现：调用 alloc_page，失败时 panic，成功时将 Page 转成内核虚拟地址。
 */
static void *boot_alloc_page(void)
{
    // 分配一页并返回内核可访问地址，供启动期构建页表使用。
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup
// paging mechanism
//         - check the correctness of pmm & paging mechanism, print PDT&PT
/*
 * 作用：初始化物理内存和启动页表管理。
 * 实现：初始化分配器和 Page 数组，绑定预置根页表并执行自检，最后初始化 kmalloc。
 */
void pmm_init(void)
{
    // 依次初始化分配器、物理页描述符、启动根页表和 kmalloc，并执行自检。
    // We need to alloc/free the physical memory (granularity is 4KB or other
    // size).
    // So a framework of physical memory manager (struct pmm_manager)is defined
    // in pmm.h
    // First we should init a physical memory manager(pmm) based on the
    // framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a
    // pmm
    check_alloc_page();
    // create boot_pgdir, an initial page directory(Page Directory Table, PDT)
    extern char boot_page_table_sv39[];
    boot_pgdir_va = (pte_t *)boot_page_table_sv39;
    boot_pgdir_pa = PADDR(boot_pgdir_va);

    check_pgdir();

    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // now the basic virtual memory map(see memalyout.h) is established.
    // check the correctness of the basic virtual memory map.
    check_boot_pgdir();

    kmalloc_init();
}

// get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
/* pgdir 是根页表的内核虚拟地址，la 是待映射地址。
 * create=0 只查找；create=1 允许创建缺失的下级页表。
 * 返回末级 PTE 槽位的地址；槽位存在不代表其中已有有效映射。 */
/*
 * 作用：取得虚拟地址 la 对应的末级 PTE 槽位地址。
 * 实现：按 PDX1、PDX0 逐级查表，create=1 时允许分配下级表，再按 PTX 取槽位；下级表缺失且无法创建时返回 NULL。
 */


/**
 * Sv32、Sv39、Sv48 有什么区别？为什么 `get_pte()` 中两段代码相似？
 * 
 * 三种模式的页大小均可为 4 KiB，页内偏移占 12 位；区别在页表级数和虚拟地址宽度。Sv32 用两级页表和两个 10 位索引，虚拟地址宽度为 32 位；
 * Sv39 用三级页表和三个 9 位索引，虚拟地址宽度为 39 位；Sv48 用四级页表和四个 9 位索引，虚拟地址宽度为 48 位。
 * 页表层级越多，可覆盖的虚拟地址范围越大。
 * 
 * `get_pte()` 中两段相似代码分别处理 Sv39 的根页表到中间页表，以及中间页表到末级页表。
 * 每段都检查目录项的有效位；若无效且 `create` 为 1，就分配一个物理页、清零、设置引用计数，再把新页表地址写进目录项；
 * 若 `create` 为 0，则返回 `NULL`。层级索引不同，逐级检查和创建的步骤相同，所以代码结构相似。
 * Sv32 需要这类下级页表创建一步，Sv39 两步，Sv48 三步。
 */


/**
 * `get_pte()` 的作用是什么？
 * 
 * 它返回末级页表中 `PTX(la)` 对应槽位的内核虚拟地址，类型是 `pte_t *`。`ptep` 是槽位地址，`*ptep` 才是页表项的值。
 * 只要能走到末级页表，即使目标槽位的 `PTE_V` 为 0，也可以返回非空指针。`page_insert()` 正是通过这个指针写入新映射。
 * 缺少下级页表且 `create=0`，或者 `create=1` 但分配失败时，才会在遍历过程中返回 `NULL`。
 */

pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    // Sv39 的第一级索引取 la[38:30]，根目录项指向中间页表。
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        // 逻辑或短路：create=0 时不会执行 alloc_page。
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        // 新分配的是页表页；清零后所有目录项初始均无效。
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    // 取出中间页表物理地址，经 KADDR 转为内核地址，用 la[29:21] 索引。
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
    // 末级页表使用 la[20:12] 索引；页内偏移不参与 PTE 定位。
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - get related Page struct for linear address la using PDT pgdir
/*
 * 作用：查询 la 当前映射的物理页，可顺带返回 PTE 地址。
 * 实现：只查找现有槽位，有效时将 PTE 转换为 Page，没有有效映射就返回 NULL。
 */
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    // 仅查询已有映射；可通过输出参数一并返回 PTE 槽位地址。
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;
    }
    // 同时检查槽位存在和映射有效，再把 PTE 中的物理页号转为 Page。
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

//LAB4 function1 2412940
/*
 * 作用：撤销已定位的 PTE 对应的有效映射。
 * 实现：减少物理页引用，归零时释放，再清零 PTE 并刷新该地址的 TLB；无效项直接跳过。
 */
static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    // 只撤销有效映射；重复删除无效项不会再次减少引用计数。
    if (*ptep & PTE_V) {
        struct Page *page = pte2page(*ptep);
        // 删除一个映射减少一次引用，其他映射仍引用该页时不能释放。
        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
        // 清除页表项后刷新地址翻译缓存，避免继续使用旧映射。
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}

// 将 la 所在虚拟页映射到 page，perm 指定权限；成功返回 0。
/*
 * 作用：建立或更新虚拟页 la 到物理页 page 的映射。
 * 实现：先取得 PTE 并增加新页引用；同页替换抵消新增引用，异页替换移除旧映射，最后写入页号和权限并刷新 TLB。
 */
/**
 * `page_insert()` 和 `page_remove()` 如何更新映射？
 * `page_insert(pgdir, page, la, perm)` 先调用 `get_pte(..., 1)` 查找或创建目标 PTE。若页表分配失败，返回 `-E_NO_MEM`。
 * 成功取得 PTE 后，函数增加新物理页的引用计数；若原映射指向同一物理页，则抵消这次增加并只更新权限；
 * 若原映射指向另一物理页，则先移除旧映射，再写入新 PTE。最后刷新目标地址的 TLB。
 */
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    // 允许分配中间页表；分配失败时尚未增加数据页引用。
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }
    // 先增加新页引用，避免同页重映射时先撤销旧映射导致误释放。
    page_ref_inc(page);
    if (*ptep & PTE_V) {
        if (pte2page(*ptep) == page) {
            // 新旧物理页相同：抵消刚增加的引用，随后更新权限。
            page_ref_dec(page);
        } else {
            // 新旧物理页不同：撤销旧映射并按引用计数回收旧页。
            page_remove_pte(pgdir, la, ptep);
        }
    }
    // pte_create 把物理页号、权限和 PTE_V 组合成新页表项。
    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(pgdir, la);
    return 0;
}

/*
 * 作用：删除 la 上已有的页映射。
 * 实现：以 create=0 查找槽位，找到后交给 page_remove_pte 处理引用计数、回收和 TLB。
 */
// `page_remove(pgdir, la)` 调用 `get_pte(..., 0)` 查找现有 PTE，不创建页表。
// 若 PTE 有效，就减少物理页引用计数，计数归零时释放物理页，再清零 PTE 并刷新 TLB。地址没有有效映射时不做修改。
void page_remove(pde_t *pgdir, uintptr_t la)
{
    // 删除时不创建页表；真正的引用计数和 TLB 操作交给辅助函数。
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL) {
        page_remove_pte(pgdir, la, ptep);
    }
}








// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
/*
 * 作用：使本核上 la 的旧地址翻译失效。
 * 实现：执行带虚拟地址的 sfence.vma；本实现不使用 pgdir 作判断。
 */
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    // flush_tlb();
    // The flush_tlb flush the entire TLB, is there any better way?
    // 本实现按 la 刷新本核所有 ASID 下的对应翻译，pgdir 未参与判断。
    asm volatile("sfence.vma %0" : : "r"(la));
}

/*
 * 作用：检验物理页分配和释放。
 * 实现：运行当前分配器的自检，成功后输出通过信息。
 */
static void check_alloc_page(void)
{
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

/*
 * 作用：检验页表定位、映射替换、权限和引用计数。
 * 实现：构造并替换映射，以断言核对结果，再撤销映射、清理测试页表并检查空闲页数。
 */
static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir_va[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

/*
 * 作用：检验启动映射及同一物理页的共享映射。
 * 实现：检查内核区间的地址转换，用两个地址映射同一页验证写入可见性，再回收测试资源。
 */
static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// perm2str - use string 'u,r,w,-' to present the permission
/*
 * 作用：将页表权限转成显示字符串。
 * 实现：根据权限位设置用户及写入标记，固定填入读标记并返回静态缓冲区。
 */
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

// get_pgtable_items - In [left, right] range of PDT or PT, find a continuous
// linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
//  return value: 0 - not a invalid item range, perm - a valid item range with
//  perm permission
/*
 * 作用：找出页表中下一段权限相同的连续有效项。
 * 实现：跳过无效项后扩展区间，遇到权限不同的项或到达右界时停止，返回权限并可输出区间两端。
 */
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;
    }
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    if (start < right)
    {
        if (left_store != NULL)
        {
            *left_store = start;
        }
        int perm = (table[start++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        if (right_store != NULL)
        {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}
