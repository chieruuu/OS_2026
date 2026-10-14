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
static void page_init(void) {
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
        mem_end < PHYSICAL_MEMORY_END ? mem_end : PHYSICAL_MEMORY_END,
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
