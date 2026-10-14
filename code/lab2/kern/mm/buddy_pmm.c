#include <buddy_pmm.h>
#include <stdio.h>
#include <string.h>

//lab2 function2-3 2412940
// Capacity covers the framework's physical window, including reserved pages.
#define BUDDY_CAPACITY ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
#define BUDDY_ORDER_COUNT (sizeof(size_t) * 8)

enum buddy_page_state {
    BUDDY_UNREGISTERED,
    BUDDY_FREE_BODY,
    BUDDY_FREE_HEAD,
    BUDDY_ALLOC_BODY,
    BUDDY_ALLOC_HEAD,
};

struct buddy_page_info {
    unsigned int request;
    unsigned char state;
    unsigned char order;
};

static struct buddy_page_info buddy_info[BUDDY_CAPACITY];
static list_entry_t buddy_lists[BUDDY_ORDER_COUNT];
static size_t buddy_free_count;
static unsigned int buddy_max_order;
static unsigned char buddy_seen[BUDDY_CAPACITY];

static size_t buddy_page_count(void) {
    assert(pages != NULL && npage > nbase);
    assert(npage - nbase <= BUDDY_CAPACITY);
    return npage - nbase;
}

// Validate addresses as integers before doing descriptor pointer arithmetic.
static size_t buddy_index(struct Page *page) {
    size_t count = buddy_page_count();
    uintptr_t address = (uintptr_t)page;
    uintptr_t first = (uintptr_t)pages;
    assert(address >= first);
    uintptr_t offset = address - first;
    assert(offset % sizeof(struct Page) == 0);
    size_t index = offset / sizeof(struct Page);
    assert(index < count);
    return index;
}

static size_t buddy_block_size(unsigned int order) {
    assert(order <= buddy_max_order);
    return (size_t)1 << order;
}

static bool buddy_request_order(size_t n, unsigned int *order) {
    assert(n > 0);
    if (n > buddy_block_size(buddy_max_order)) {
        return 0;
    }
    unsigned int k = 0;
    while (buddy_block_size(k) < n) {
        k++;
    }
    *order = k;
    return 1;
}

static void buddy_init(void) {
    memset(buddy_info, 0, sizeof(buddy_info));
    for (size_t k = 0; k < BUDDY_ORDER_COUNT; k++) {
        list_init(&buddy_lists[k]);
    }
    buddy_free_count = 0;
    buddy_max_order = 0;
    for (size_t limit = BUDDY_CAPACITY; limit > 1; limit >>= 1) {
        buddy_max_order++;
    }
}

static void buddy_link_block(size_t index, unsigned int order) {
    struct Page *base = pages + index;
    list_entry_t *head = &buddy_lists[order];
    list_entry_t *le = list_next(head);
    while (le != head && le2page(le, page_link) < base) {
        le = list_next(le);
    }
    buddy_info[index].state = BUDDY_FREE_HEAD;
    buddy_info[index].order = order;
    base->property = buddy_block_size(order);
    SetPageProperty(base);
    list_add_before(le, &base->page_link);
}

static void buddy_unlink_block(size_t index) {
    assert(buddy_info[index].state == BUDDY_FREE_HEAD);
    struct Page *base = pages + index;
    list_del_init(&base->page_link);
    ClearPageProperty(base);
    base->property = 0;
    buddy_info[index].state = BUDDY_FREE_BODY;
    buddy_info[index].order = 0;
}

// Count changes belong to allocation/registration/free, never to merging.
static void buddy_merge_block(size_t index, unsigned int order) {
    while (order < buddy_max_order) {
        size_t block_size = buddy_block_size(order);
        ppn_t partner_ppn = (nbase + index) ^ block_size;
        if (partner_ppn < nbase || partner_ppn >= npage ||
            block_size > npage - partner_ppn) {
            break;
        }
        size_t partner = partner_ppn - nbase;
        if (buddy_info[partner].state != BUDDY_FREE_HEAD ||
            buddy_info[partner].order != order) {
            break;
        }
        buddy_unlink_block(partner);
        if (partner < index) {
            index = partner;
        }
        order++;
    }
    buddy_link_block(index, order);
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    size_t index = buddy_index(base);
    assert(n <= buddy_page_count() - index);
    for (size_t i = index; i < index + n; i++) {
        assert(buddy_info[i].state == BUDDY_UNREGISTERED);
        assert(PageReserved(pages + i));
    }
    for (size_t i = index; i < index + n; i++) {
        struct Page *p = pages + i;
        p->flags = p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
        buddy_info[i].state = BUDDY_FREE_BODY;
    }
    buddy_free_count += n;

    size_t remaining = n;
    while (remaining > 0) {
        unsigned int order = 0;
        while (order < buddy_max_order) {
            size_t larger = buddy_block_size(order + 1);
            if (larger > remaining || (nbase + index) % larger != 0) {
                break;
            }
            order++;
        }
        size_t block_size = buddy_block_size(order);
        buddy_merge_block(index, order);
        index += block_size;
        remaining -= block_size;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    unsigned int target;
    if (!buddy_request_order(n, &target)) {
        return NULL;
    }
    size_t block_size = buddy_block_size(target);
    if (block_size > buddy_free_count) {
        return NULL;
    }
    unsigned int order = target;
    while (order <= buddy_max_order && list_empty(&buddy_lists[order])) {
        order++;
    }
    if (order > buddy_max_order) {
        return NULL;
    }
    struct Page *base = le2page(list_next(&buddy_lists[order]), page_link);
    size_t index = buddy_index(base);
    buddy_unlink_block(index);
    while (order > target) {
        order--;
        buddy_link_block(index + buddy_block_size(order), order);
    }
    for (size_t i = index; i < index + block_size; i++) {
        buddy_info[i].state = BUDDY_ALLOC_BODY;
        buddy_info[i].order = 0;
        buddy_info[i].request = 0;
        set_page_ref(pages + i, 0);
    }
    buddy_info[index].state = BUDDY_ALLOC_HEAD;
    buddy_info[index].order = target;
    buddy_info[index].request = n;
    buddy_free_count -= block_size;
    return base;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    size_t index = buddy_index(base);
    assert(buddy_info[index].state == BUDDY_ALLOC_HEAD);
    assert(buddy_info[index].request == n);
    unsigned int order = buddy_info[index].order;
    size_t block_size = buddy_block_size(order);
    assert(block_size <= buddy_page_count() - index);
    assert((nbase + index) % block_size == 0);
    for (size_t i = index; i < index + block_size; i++) {
        assert(buddy_info[i].state ==
               (i == index ? BUDDY_ALLOC_HEAD : BUDDY_ALLOC_BODY));
        assert(!PageReserved(pages + i) && !PageProperty(pages + i));
    }
    for (size_t i = index; i < index + block_size; i++) {
        struct Page *p = pages + i;
        p->flags = p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
        buddy_info[i].state = BUDDY_FREE_BODY;
        buddy_info[i].order = 0;
        buddy_info[i].request = 0;
    }
    buddy_free_count += block_size;
    buddy_merge_block(index, order);
}

static size_t buddy_nr_free_pages(void) {
    return buddy_free_count;
}

// Verify exact coverage, links, alignment, accounting, and maximal coalescing.
static void buddy_validate(void) {
    size_t count = buddy_page_count();
    memset(buddy_seen, 0, count);
    size_t total = 0;
    for (unsigned int order = 0; order <= buddy_max_order; order++) {
        list_entry_t *head = &buddy_lists[order];
        list_entry_t *prev = head;
        size_t previous_index = 0, entries = 0;
        size_t block_size = buddy_block_size(order);
        for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
            assert(++entries <= count);
            assert(le->prev == prev);
            struct Page *p = le2page(le, page_link);
            size_t index = buddy_index(p);
            assert(entries == 1 || index > previous_index);
            assert(buddy_info[index].state == BUDDY_FREE_HEAD);
            assert(buddy_info[index].order == order);
            assert(PageProperty(p) && p->property == block_size);
            assert((nbase + index) % block_size == 0);
            assert(block_size <= count - index);
            for (size_t i = index; i < index + block_size; i++) {
                assert(buddy_seen[i] == 0);
                assert(buddy_info[i].state ==
                       (i == index ? BUDDY_FREE_HEAD : BUDDY_FREE_BODY));
                assert(!PageReserved(pages + i));
                assert(PageProperty(pages + i) == (i == index));
                assert(page_ref(pages + i) == 0);
                buddy_seen[i] = 1;
            }
            if (order < buddy_max_order) {
                ppn_t partner_ppn = (nbase + index) ^ block_size;
                if (partner_ppn >= nbase && partner_ppn < npage) {
                    size_t partner = partner_ppn - nbase;
                    assert(buddy_info[partner].state != BUDDY_FREE_HEAD ||
                           buddy_info[partner].order != order);
                }
            }
            total += block_size;
            previous_index = index;
            prev = le;
        }
        assert(head->prev == prev);
    }
    assert(total == buddy_free_count);
    for (size_t index = 0; index < count; index++) {
        if (buddy_info[index].state != BUDDY_ALLOC_HEAD) {
            continue;
        }
        unsigned int order = buddy_info[index].order, requested_order;
        size_t block_size = buddy_block_size(order);
        assert(buddy_request_order(buddy_info[index].request, &requested_order));
        assert(order == requested_order && (nbase + index) % block_size == 0);
        assert(block_size <= count - index);
        for (size_t i = index; i < index + block_size; i++) {
            assert(buddy_seen[i] == 0);
            assert(buddy_info[i].state ==
                   (i == index ? BUDDY_ALLOC_HEAD : BUDDY_ALLOC_BODY));
            assert(!PageReserved(pages + i) && !PageProperty(pages + i));
            buddy_seen[i] = 2;
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (buddy_info[i].state == BUDDY_UNREGISTERED) {
            assert(buddy_seen[i] == 0 && PageReserved(pages + i));
        } else {
            assert(buddy_seen[i] != 0);
        }
    }
}

// The startup tests use public allocation semantics and an independent bitmap.
// They never reinitialize the live allocator or register synthetic kernel pages.
#define BUDDY_TEST_SLOTS 32
static unsigned char buddy_test_model[BUDDY_CAPACITY];

static size_t buddy_test_round(size_t n) {
    size_t rounded = 1;
    while (rounded < n) {
        rounded *= 2;
    }
    return rounded;
}

static void buddy_test_validate(void) {
    buddy_validate();
    for (size_t i = 0; i < buddy_page_count(); i++) {
        if (buddy_test_model[i] == 1) {
            assert(buddy_seen[i] == 1);
        } else if (buddy_test_model[i] == 2) {
            assert(buddy_seen[i] == 2);
        } else {
            assert(buddy_seen[i] != 1);
        }
    }
}

static struct Page *buddy_test_allocate(size_t n) {
    size_t before = buddy_nr_free_pages();
    size_t rounded = buddy_test_round(n);
    struct Page *p = buddy_alloc_pages(n);
    if (p != NULL) {
        size_t index = buddy_index(p);
        assert(page2ppn(p) % rounded == 0);
        assert(rounded <= buddy_page_count() - index);
        for (size_t i = index; i < index + rounded; i++) {
            assert(buddy_test_model[i] == 1);
            assert(!PageReserved(pages + i) && !PageProperty(pages + i));
            assert(page_ref(pages + i) == 0);
            buddy_test_model[i] = 2;
        }
        assert(buddy_nr_free_pages() == before - rounded);
    } else {
        assert(buddy_nr_free_pages() == before);
        // A failed request must have no aligned all-free interval in the model.
        size_t count = buddy_page_count();
        for (size_t index = 0; rounded <= count - index; index++) {
            if ((nbase + index) % rounded != 0) {
                continue;
            }
            size_t i = index;
            while (i < index + rounded && buddy_test_model[i] == 1) {
                i++;
            }
            assert(i != index + rounded);
        }
    }
    buddy_test_validate();
    return p;
}

static void buddy_test_release(struct Page *p, size_t n) {
    size_t before = buddy_nr_free_pages();
    size_t index = buddy_index(p);
    size_t rounded = buddy_test_round(n);
    for (size_t i = index; i < index + rounded; i++) {
        assert(buddy_test_model[i] == 2);
        set_page_ref(pages + i, 1);
    }
    buddy_free_pages(p, n);
    for (size_t i = index; i < index + rounded; i++) {
        buddy_test_model[i] = 1;
    }
    assert(buddy_nr_free_pages() == before + rounded);
    buddy_test_validate();
}

static unsigned int buddy_test_random(unsigned int *seed) {
    *seed = *seed * 1664525U + 1013904223U;
    return *seed >> 8;
}

static void buddy_check(void) {
    buddy_validate();
    size_t initial_free = buddy_nr_free_pages();
    for (size_t i = 0; i < buddy_page_count(); i++) {
        buddy_test_model[i] = buddy_seen[i] == 1 ? 1 : 0;
    }

    const size_t requests[] = {1, 2, 4, 8, 3, 5, 9};
    struct Page *blocks[BUDDY_TEST_SLOTS] = {0};
    size_t sizes[BUDDY_TEST_SLOTS] = {0};
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        struct Page *p = buddy_test_allocate(requests[i]);
        if (p != NULL) {
            buddy_test_release(p, requests[i]);
        }
    }
    assert(buddy_alloc_pages(buddy_block_size(buddy_max_order) + 1) == NULL);
    assert(buddy_alloc_pages(~(size_t)0) == NULL);
    buddy_test_validate();
    assert(buddy_nr_free_pages() == initial_free);

    for (size_t i = 0; i < BUDDY_TEST_SLOTS; i++) {
        blocks[i] = buddy_test_allocate(1);
        sizes[i] = 1;
    }
    for (size_t i = BUDDY_TEST_SLOTS; i > 0; i--) {
        if (blocks[i - 1] != NULL) {
            buddy_test_release(blocks[i - 1], sizes[i - 1]);
            blocks[i - 1] = NULL;
        }
    }

    unsigned int seed = 0x20261013U;
    for (size_t step = 0; step < 1024; step++) {
        size_t slot = buddy_test_random(&seed) % BUDDY_TEST_SLOTS;
        if (blocks[slot] != NULL) {
            buddy_test_release(blocks[slot], sizes[slot]);
            blocks[slot] = NULL;
        } else {
            sizes[slot] = buddy_test_random(&seed) % 17 + 1;
            blocks[slot] = buddy_test_allocate(sizes[slot]);
        }
    }
    for (size_t i = 0; i < BUDDY_TEST_SLOTS; i++) {
        if (blocks[i] != NULL) {
            buddy_test_release(blocks[i], sizes[i]);
        }
    }
    assert(buddy_nr_free_pages() == initial_free);
    for (size_t i = 0; i < buddy_page_count(); i++) {
        assert(buddy_test_model[i] != 2);
    }
    buddy_test_validate();
    cprintf("[Buddy Test] PASS (1024 randomized operations, state restored)\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
