#include <vmm.h>
#include <pmm.h>
#include <string.h>
#include <assert.h>
#include <error.h>
#include <riscv.h>
#include <stdio.h>

static struct mm_struct *test_mm(void)
{
    struct mm_struct *mm = mm_create();
    assert(mm != NULL);
    struct Page *root = alloc_page();
    assert(root != NULL);
    mm->pgdir = page2kva(root);
    memcpy(mm->pgdir, boot_pgdir_va, PGSIZE);
    assert(mm_map(mm, UTEXT, 2 * PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    assert(mm_map(mm, UTEXT + 2 * PGSIZE, PGSIZE, VM_READ, NULL) == 0);
    return mm;
}

static void destroy_test_mm(struct mm_struct *mm)
{
    exit_mmap(mm);
    free_page(kva2page(mm->pgdir));
    mm_destroy(mm);
}

void check_cow(void)
{
    size_t before = nr_free_pages();
    struct mm_struct *parent = test_mm(), *child = test_mm(), *other = test_mm();
    for (int i = 0; i < 3; i++) {
        struct Page *p = pgdir_alloc_page(parent->pgdir, UTEXT + i * PGSIZE,
                            PTE_U | PTE_R | (i < 2 ? PTE_W : 0));
        assert(p != NULL);
        memset(page2kva(p), 0x31 + i, PGSIZE);
    }
    assert(copy_range(child->pgdir, parent->pgdir, UTEXT, UTEXT + 3 * PGSIZE, 1) == 0);
    assert(copy_range(other->pgdir, parent->pgdir, UTEXT, UTEXT + 3 * PGSIZE, 1) == 0);
    pte_t *pp = get_pte(parent->pgdir, UTEXT, 0);
    pte_t *cp = get_pte(child->pgdir, UTEXT, 0);
    struct Page *shared = pte2page(*pp);
    assert(page_ref(shared) == 3 && (*pp & PTE_COW) && !(*pp & PTE_W));
    assert(pte2page(*cp) == shared);
    cprintf("[COW] fork/re-fork: shared PFN, W=0 COW=1 ref=3 PASS\n");

    // Model the stale writable physical alias that bypasses COW permission checks.
    char *stale = page2kva(shared);
    stale[0] = 'X';
    assert(*(char *)page2kva(pte2page(*cp)) == 'X');
    stale[0] = 0x31;
    cprintf("[DirtyCOW model] unchecked physical alias changes shared page: reproduced\n");

    size_t pages = nr_free_pages();
    assert(do_pgfault(child, CAUSE_STORE_PAGE_FAULT, UTEXT) == 0);
    assert(nr_free_pages() + 1 == pages && pte2page(*cp) != shared);
    *(char *)page2kva(pte2page(*cp)) = 'C';
    assert(stale[0] == 0x31 && page_ref(shared) == 2);
    assert((*cp & PTE_W) && !(*cp & PTE_COW));
    cprintf("[COW] write fault: one private copy, parent unchanged PASS\n");

    char bytes[] = {'A', 'B', 'C', 'D'};
    assert(copy_to_user(other, (void *)(UTEXT + PGSIZE - 2), bytes, sizeof(bytes)));
    assert(stale[PGSIZE - 2] == 0x31);
    assert(*(char *)page2kva(get_page(parent->pgdir, UTEXT + PGSIZE, NULL)) == 0x32);
    assert(memcmp(page2kva(get_page(other->pgdir, UTEXT, NULL)) + PGSIZE - 2, bytes, 2) == 0);
    assert(memcmp(page2kva(get_page(other->pgdir, UTEXT + PGSIZE, NULL)), bytes + 2, 2) == 0);
    assert(!copy_to_user(other, (void *)(UTEXT + 2 * PGSIZE), bytes, 1));
    assert(do_pgfault(other, CAUSE_STORE_PAGE_FAULT, UTEXT + 2 * PGSIZE) == -E_INVAL);
    cprintf("[DirtyCOW model] checked copy_to_user: cross-page split, read-only rejected PASS\n");

    pages = nr_free_pages();
    assert(page_ref(shared) == 1 && cow_resolve(parent, UTEXT) == 0);
    assert(nr_free_pages() == pages && pte2page(*pp) == shared);
    assert((*pp & PTE_W) && !(*pp & PTE_COW));
    cprintf("[COW] sole owner: restore W without allocation PASS\n");

    // Exhaust the allocator to check that a failed split retains the shared mapping.
    list_entry_t held;
    list_init(&held);
    struct Page *p;
    while ((p = alloc_page()) != NULL) list_add(&held, &p->page_link);
    pte_t saved = *get_pte(parent->pgdir, UTEXT + PGSIZE, 0);
    assert(cow_resolve(parent, UTEXT + PGSIZE) == -E_NO_MEM);
    assert(saved == *get_pte(parent->pgdir, UTEXT + PGSIZE, 0));
    while (!list_empty(&held)) {
        list_entry_t *le = list_next(&held);
        list_del(le);
        free_page(le2page(le, page_link));
    }
    cprintf("[COW] allocation failure: original PTE and reference retained PASS\n");
    destroy_test_mm(other);
    destroy_test_mm(child);
    destroy_test_mm(parent);
    assert(nr_free_pages() == before);
    cprintf("[COW] all mappings/page tables reclaimed PASS\n");
}
