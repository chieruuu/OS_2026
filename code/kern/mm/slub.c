#include <slub.h>
#include <pmm.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

//lab2 challenge slub 2412940
#define SLUB_ALIGNMENT 16
#define SLUB_CLASSES 8
#define SLUB_PAGE_CAPACITY ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)
#define SLUB_BITMAP_WORDS (PGSIZE / SLUB_ALIGNMENT / 64)

struct slub_slab {
    list_entry_t link;
    struct Page *page;
    size_t request_pages;
    size_t object_size;
    size_t requested_bytes;
    unsigned int class_id;
    unsigned int capacity;
    unsigned int inuse;
    uint64_t allocated[SLUB_BITMAP_WORDS];
};

struct slub_cache {
    list_entry_t partial;
    list_entry_t full;
};

static struct slub_cache slub_caches[SLUB_CLASSES];
static list_entry_t slub_large;
static struct slub_slab *slub_owner[SLUB_PAGE_CAPACITY];
static bool slub_ready;

// 返回 slab 元数据按 16 字节对齐后占用的头部大小。
static size_t slub_header_size(void) {
    return ROUNDUP(sizeof(struct slub_slab), SLUB_ALIGNMENT);
}

// 根据大小类别编号返回该类别的标准对象大小。
static size_t slub_class_size(unsigned int id) {
    assert(id < SLUB_CLASSES);
    return (size_t)SLUB_ALIGNMENT << id;
}

// 返回 slab 头部之后、实际用于存放对象的起始地址。
static unsigned char *slub_data(struct slub_slab *slab) {
    return (unsigned char *)slab + slub_header_size();
}

// 查询对象槽位在位图中是否已被占用。
static bool slub_slot_used(struct slub_slab *slab, unsigned int slot) {
    return (slab->allocated[slot / 64] >> (slot % 64)) & 1;
}

// 设置或清除对象槽位对应的位图标志。
static void slub_mark_slot(struct slub_slab *slab, unsigned int slot, bool used) {
    uint64_t mask = (uint64_t)1 << (slot % 64);
    if (used) {
        slab->allocated[slot / 64] |= mask;
    } else {
        slab->allocated[slot / 64] &= ~mask;
    }
}

// 初始化各大小类别的 partial/full 链表和大对象链表。
void slub_init(void) {
    assert(!slub_ready);
    assert(pages != NULL && npage > nbase);
    assert(npage - nbase <= SLUB_PAGE_CAPACITY);
    for (unsigned int i = 0; i < SLUB_CLASSES; i++) {
        list_init(&slub_caches[i].partial);
        list_init(&slub_caches[i].full);
    }
    list_init(&slub_large);
    memset(slub_owner, 0, sizeof(slub_owner));
    slub_ready = 1;
}

// 从底层页分配器取得页面并建立一个新的 slab；小对象加入类别链表，
// 大对象加入专用链表，并为其覆盖的每个物理页记录归属。
static struct slub_slab *slub_new_slab(unsigned int id, size_t request_pages,
                                       size_t requested_bytes) {
    struct Page *page = alloc_pages(request_pages);
    if (page == NULL) {
        return NULL;
    }
    size_t index = page - pages;
    assert(index < npage - nbase && request_pages <= npage - nbase - index);
    struct slub_slab *slab = (struct slub_slab *)(page2pa(page) + va_pa_offset);
    memset(slab, 0, sizeof(*slab));
    list_init(&slab->link);
    slab->page = page;
    slab->request_pages = request_pages;
    slab->requested_bytes = requested_bytes;
    slab->class_id = id;
    if (id < SLUB_CLASSES) {
        slab->object_size = slub_class_size(id);
        slab->capacity = (PGSIZE - slub_header_size()) / slab->object_size;
        assert(slab->capacity > 0 && slab->capacity <= SLUB_BITMAP_WORDS * 64);
        list_add(&slub_caches[id].partial, &slab->link);
    } else {
        slab->object_size = requested_bytes;
        slab->capacity = slab->inuse = 1;
        list_add(&slub_large, &slab->link);
    }
    for (size_t i = index; i < index + request_pages; i++) {
        assert(slub_owner[i] == NULL);
        slub_owner[i] = slab;
    }
    return slab;
}

// 分配对象：小对象从对应类别的 slab 槽位取得，大对象申请连续页面。
void *kmalloc(size_t size) {
    assert(slub_ready);
    if (size == 0) {
        return NULL;
    }
    unsigned int id = 0;
    while (id < SLUB_CLASSES && size > slub_class_size(id)) {
        id++;
    }
    if (id == SLUB_CLASSES) {
        size_t maximum = (npage - nbase) * PGSIZE;
        size_t header = slub_header_size();
        if (size > maximum || header > maximum - size) {
            return NULL;
        }
        size_t bytes = size + header;
        size_t request_pages = bytes / PGSIZE + (bytes % PGSIZE != 0);
        struct slub_slab *slab = slub_new_slab(id, request_pages, size);
        return slab == NULL ? NULL : slub_data(slab);
    }

    list_entry_t *head = &slub_caches[id].partial;
    struct slub_slab *slab;
    if (list_empty(head)) {
        slab = slub_new_slab(id, 1, 0);
        if (slab == NULL) {
            return NULL;
        }
    } else {
        slab = to_struct(list_next(head), struct slub_slab, link);
    }
    unsigned int slot = 0;
    while (slot < slab->capacity && slub_slot_used(slab, slot)) {
        slot++;
    }
    assert(slot < slab->capacity);
    slub_mark_slot(slab, slot, 1);
    slab->inuse++;
    if (slab->inuse == slab->capacity) {
        list_del_init(&slab->link);
        list_add(&slub_caches[id].full, &slab->link);
    }
    return slub_data(slab) + slot * slab->object_size;
}

// 销毁空 slab，清除页面归属并将底层物理页归还给页面分配器。
static void slub_destroy_slab(struct slub_slab *slab) {
    struct Page *page = slab->page;
    size_t index = page - pages;
    size_t request_pages = slab->request_pages;
    list_del_init(&slab->link);
    for (size_t i = index; i < index + request_pages; i++) {
        assert(slub_owner[i] == slab);
        slub_owner[i] = NULL;
    }
    // Buddy requires the original page request, not its rounded block size.
    free_pages(page, request_pages);
}

// 释放对象：验证地址和槽位归属，必要时销毁空 slab 并归还页面。
void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    assert(slub_ready);
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t first = DRAM_BASE + va_pa_offset;
    size_t memory_size = (npage - nbase) * PGSIZE;
    assert(address >= first && address - first < memory_size);
    size_t index = (address - first) / PGSIZE;
    struct slub_slab *slab = slub_owner[index];
    // Resolve ownership before reading any metadata through the supplied address.
    assert(slab != NULL);
    uintptr_t start = (uintptr_t)slub_data(slab);
    if (slab->class_id == SLUB_CLASSES) {
        assert(address == start && slab->inuse == 1);
        slub_destroy_slab(slab);
        return;
    }
    assert(address >= start);
    size_t offset = address - start;
    assert(offset % slab->object_size == 0);
    size_t slot = offset / slab->object_size;
    assert(slot < slab->capacity && slub_slot_used(slab, slot));
    bool was_full = slab->inuse == slab->capacity;
    slub_mark_slot(slab, slot, 0);
    slab->inuse--;
    if (slab->inuse == 0) {
        slub_destroy_slab(slab);
    } else if (was_full) {
        list_del_init(&slab->link);
        list_add(&slub_caches[slab->class_id].partial, &slab->link);
    }
}

// 校验一个 slab 链表中的所有 slab、对象位图和页面归属关系。
static size_t slub_validate_list(list_entry_t *head, unsigned int id, bool full) {
    size_t used_pages = 0, entries = 0;
    list_entry_t *prev = head;
    for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
        assert(++entries <= npage - nbase);
        assert(le->prev == prev);
        struct slub_slab *slab = to_struct(le, struct slub_slab, link);
        assert(slab->class_id == id);
        size_t index = slab->page - pages;
        assert(index < npage - nbase);
        assert(slab->request_pages <= npage - nbase - index);
        assert((uintptr_t)slab == page2pa(slab->page) + va_pa_offset);
        for (size_t i = index; i < index + slab->request_pages; i++) {
            assert(slub_owner[i] == slab);
        }
        if (id < SLUB_CLASSES) {
            assert(slab->request_pages == 1);
            assert(slab->object_size == slub_class_size(id));
            assert(slab->capacity == (PGSIZE - slub_header_size()) / slab->object_size);
            unsigned int allocated = 0;
            for (unsigned int i = 0; i < SLUB_BITMAP_WORDS * 64; i++) {
                if (slub_slot_used(slab, i)) {
                    assert(i < slab->capacity);
                    allocated++;
                }
            }
            assert(allocated == slab->inuse && allocated > 0);
            assert(full ? allocated == slab->capacity : allocated < slab->capacity);
        } else {
            assert(slab->inuse == 1 && slab->capacity == 1);
            assert(slab->requested_bytes > slub_class_size(SLUB_CLASSES - 1));
            assert(slab->requested_bytes <= slab->request_pages * PGSIZE - slub_header_size());
        }
        used_pages += slab->request_pages;
        prev = le;
    }
    assert(head->prev == prev);
    return used_pages;
}

// 汇总检查所有 slab 链表与页面归属表是否一致。
static void slub_validate(void) {
    size_t listed = slub_validate_list(&slub_large, SLUB_CLASSES, 1);
    for (unsigned int i = 0; i < SLUB_CLASSES; i++) {
        listed += slub_validate_list(&slub_caches[i].partial, i, 0);
        listed += slub_validate_list(&slub_caches[i].full, i, 1);
    }
    size_t owned = 0;
    for (size_t i = 0; i < npage - nbase; i++) {
        owned += slub_owner[i] != NULL;
    }
    assert(owned == listed);
}

// 按指定模式写入或检查对象内容，用于验证分配后的内存可访问性。
static void slub_test_pattern(void *ptr, size_t size, unsigned char pattern, bool write) {
    unsigned char *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        if (write) {
            bytes[i] = pattern;
        } else {
            assert(bytes[i] == pattern);
        }
    }
}

// 执行 SLUB 的边界、共享页面、随机分配释放和无泄漏自检。
void slub_check(void) {
    assert(slub_ready);
    size_t initial = nr_free_pages();
    const size_t boundaries[] = {1, 15, 16, 17, 31, 32, 33, 63, 64, 65,
        127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
        2047, 2048, 2049, 4095, 4096, 4097, 12287, 12288, 12289};
    assert(kmalloc(0) == NULL && kmalloc(~(size_t)0) == NULL);
    kfree(NULL);
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); i++) {
        void *p = kmalloc(boundaries[i]);
        assert(p != NULL && (uintptr_t)p % SLUB_ALIGNMENT == 0);
        slub_test_pattern(p, boundaries[i], 0xa5, 1);
        slub_test_pattern(p, boundaries[i], 0xa5, 0);
        kfree(p);
        assert(nr_free_pages() == initial);
        slub_validate();
    }
    void *a = kmalloc(24), *b = kmalloc(24);
    assert(a != NULL && b != NULL && a != b);
    assert(ROUNDDOWN((uintptr_t)a, PGSIZE) == ROUNDDOWN((uintptr_t)b, PGSIZE));
    assert(nr_free_pages() == initial - 1);
    kfree(a);
    void *c = kmalloc(24);
    assert(c == a);
    kfree(c);
    kfree(b);
    assert(nr_free_pages() == initial);

    void *active[24] = {0};
    size_t requests[24] = {0};
    unsigned char patterns[24] = {0};
    unsigned int seed = 0x2412940;
    for (size_t step = 0; step < 1200; step++) {
        for (size_t j = 0; j < 24; j++) {
            if (active[j] != NULL) {
                slub_test_pattern(active[j], requests[j], patterns[j], 0);
            }
        }
        seed = seed * 1664525U + 1013904223U;
        unsigned int slot = (seed >> 8) % 24;
        if (active[slot] != NULL) {
            kfree(active[slot]);
            active[slot] = NULL;
        } else {
            seed = seed * 1664525U + 1013904223U;
            requests[slot] = (seed >> 8) % 13000 + 1;
            active[slot] = kmalloc(requests[slot]);
            assert(active[slot] != NULL);
            assert((uintptr_t)active[slot] % SLUB_ALIGNMENT == 0);
            patterns[slot] = step % 251 + 1;
            for (size_t j = 0; j < 24; j++) {
                if (j != slot && active[j] != NULL) {
                    uintptr_t x = (uintptr_t)active[slot], y = (uintptr_t)active[j];
                    assert(x + requests[slot] <= y || y + requests[j] <= x);
                }
            }
            slub_test_pattern(active[slot], requests[slot], patterns[slot], 1);
        }
        slub_validate();
    }
    for (size_t i = 0; i < 24; i++) {
        if (active[i] != NULL) {
            slub_test_pattern(active[i], requests[i], patterns[i], 0);
            kfree(active[i]);
        }
    }
    slub_validate();
    assert(nr_free_pages() == initial);
    cprintf("[SLUB Test] PASS (31 boundaries, shared pages, 1200 random operations, no leaks)\n");
}
