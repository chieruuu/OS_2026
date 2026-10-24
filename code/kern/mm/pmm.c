#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &buddy_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2412940
/*
 *page_init() 主要完成四件事：
 *1. 读取并检查设备树提供的物理内存范围。
 *2. 计算实际可管理的物理页范围，并设置虚拟地址到物理地址的偏移。
 *3. 在内核结束地址之后建立并初始化 Page 描述符数组，同时保留内核和描述符占用的页面。
 *4. 将描述符数组之后的完整物理页面通过 init_memmap() 注册给当前物理内存管理器。
 *最终，npage、pages、页面保留状态和初始空闲页列表都被正确建立。
 */
static void page_init(void) {
    // end 是链接器提供的符号，表示内核镜像末尾的内核虚拟地址。
    extern char end[];

    // DTB 初始化阶段已经解析设备树；这两个接口返回探测到的 DRAM
    // 物理基址和字节数。这里不把它们当作可信输入，后面会检查范围。
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();

    // uintptr_t 是本机地址运算类型。先取得其最大值，以便在做加法、
    // 对齐和布局计算前显式检查溢出。
    uintptr_t address_max = ~(uintptr_t)0;

    // 本实验假定单段 DRAM 从 DRAM_BASE 开始。基址不符、大小为零，
    // 或 base + size 会超出地址类型可表示范围，都不能继续建立页表描述。
    if (mem_base != DRAM_BASE || mem_size == 0 ||
        mem_size > address_max - mem_base) {
        panic("invalid physical memory range from device tree");
    }

    // 设备树内存按左闭右开区间 [mem_base, mem_end) 表示。
    // 打印给用户看的闭区间末地址因此是 mem_end - 1。
    uintptr_t mem_end = mem_base + mem_size;
    cprintf("physical memory map:\n"
            "  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            (unsigned long)mem_size, (unsigned long)mem_base,
            (unsigned long)(mem_end - 1));

    // 管理器只处理实验框架允许的物理范围：不能越过设备树报告末端，
    // 也不能越过 PHYSICAL_MEMORY_END。向下取整，丢弃末尾不完整的页。
    uintptr_t maxpa = ROUNDDOWN(
        mem_end < PHYSICAL_MEMORY_END ? mem_end : PHYSICAL_MEMORY_END,
        PGSIZE);

    // nbase 是 DRAM 第一个物理页号。若 maxpa 对应的页号不大于 nbase，
    // 则固定 DRAM 起点之前没有任何可纳入管理的完整物理页。
    if (PPN(maxpa) <= nbase) {
        panic("no manageable physical pages");
    }

    // 内核启动时已经建立了固定线性映射；PADDR() 依赖该偏移换算，
    // 因此使用工程定义的启动映射偏移，而不是从 DTB 基址重新推导。
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // end 是虚拟地址，必须先确认它落在内核映射区域，且向上按页对齐
    // 时不会溢出。随后 pages_va 将成为 Page 描述符数组的虚拟起点。
    uintptr_t kernel_end = (uintptr_t)end;
    if (kernel_end < KERNBASE || kernel_end > address_max - (PGSIZE - 1)) {
        panic("invalid kernel end address");
    }

    // 内核镜像末尾可能不在页边界。描述符数组从内核结束后的第一个
    // 页边界开始，内核末尾到这里的间隙仍然属于已占用/保留区域。
    uintptr_t pages_va = ROUNDUP(kernel_end, PGSIZE);

    // pages_va 是内核虚拟地址；先通过启动映射换算成物理地址，后续
    // 才能判断描述符数组是否完全落在可管理物理范围内。
    uintptr_t pages_pa = PADDR(pages_va);

    // npage 是可管理物理页号的排他上界，不是描述符数量。
    // 描述符数组包含 npage - nbase 个元素，对应页号 [nbase, npage)。
    size_t page_count = PPN(maxpa) - nbase;

    // 在写入任何描述符前，先验证整段数组的物理占用空间。
    // pages_pa > maxpa 表示数组起点已越过可管理末端；除法形式的比较
    // 避免直接计算 page_count * sizeof(struct Page) 时发生乘法溢出。
    if (pages_pa > maxpa ||
        page_count > (maxpa - pages_pa) / sizeof(struct Page)) {
        panic("physical memory cannot hold the page descriptor array");
    }

    // 通过上述除法检查后，乘法结果可安全表示；这是描述符数组实际
    // 占用的字节数。另检查后续虚拟地址加法，避免描述符末端回绕。
    size_t descriptor_size = page_count * sizeof(struct Page);
    if (descriptor_size > address_max - pages_va) {
        panic("page descriptor virtual address overflow");
    }

    // descriptors_end_pa 是描述符数组在物理内存中的右开端点。
    // 它可能落在某个页的中间，该页剩余部分也不能交给分配器。
    uintptr_t descriptors_end_pa = pages_pa + descriptor_size;

    // 到此布局验证完成，发布全局页号上界和描述符数组虚拟地址。
    // pages[i] 对应物理页号 nbase + i；数组中每个描述符需先自行初始化，
    // 不能假设该内存原有内容为零。
    npage = PPN(maxpa);
    pages = (struct Page *)pages_va;
    for (size_t i = 0; i < page_count; i++) {
        struct Page *p = &pages[i];

        // 清除可能的随机标志后显式设置保留位，保证注册空闲区之前，
        // 所有页面都处于保留状态；ref、property 和链表节点也逐项初始化。
        p->flags = 0;
        SetPageReserved(p);
        set_page_ref(p, 0);
        p->property = 0;
        list_init(&p->page_link);
    }

    // 描述符数组末尾按页向上对齐，得到第一个可作为空闲页的物理地址。
    // 内核末尾至描述符起点、描述符数组末尾至此处的对齐空隙都保留，
    // 描述符数组占用的页面也不会交给分配器。
    uintptr_t free_start = ROUNDUP(descriptors_end_pa, PGSIZE);

    // 只有 free_start 严格小于 maxpa 才存在完整空闲页。
    // 两端均页对齐，因此差值除以 PGSIZE 就是可注册的页数。
    // pa2page 只在此处转换属于 [nbase, npage) 的有效物理页地址；
    // init_memmap 再由当前 pmm_manager 接管页面状态和空闲页计数。
    if (free_start < maxpa) {
        init_memmap(pa2page(free_start), (maxpa - free_start) / PGSIZE);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
