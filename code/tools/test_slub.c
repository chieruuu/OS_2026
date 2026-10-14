/* Compile the actual Buddy and SLUB implementations in an isolated host arena. */
#include "../kern/mm/buddy_pmm.c"
#include "../kern/mm/slub.c"

extern int printf(const char *, ...);
extern int vprintf(const char *, __builtin_va_list);
extern int fflush(void *);
extern void _Exit(int) __attribute__((noreturn));
extern void abort(void) __attribute__((noreturn));

#define TEST_PAGE_COUNT 512
static struct Page test_descriptors[TEST_PAGE_COUNT];
static unsigned char test_ram[TEST_PAGE_COUNT * PGSIZE] __attribute__((aligned(PGSIZE)));
struct Page *pages = test_descriptors;
size_t npage = DRAM_BASE / PGSIZE + TEST_PAGE_COUNT;
const size_t nbase = DRAM_BASE / PGSIZE;
uint64_t va_pa_offset;
const struct pmm_manager *pmm_manager = &buddy_pmm_manager;
static bool expect_panic;

int cprintf(const char *format, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, format);
    int n = vprintf(format, ap);
    __builtin_va_end(ap);
    return n;
}
void __panic(const char *file, int line, const char *format, ...) {
    printf("%s %s:%d: ", expect_panic ? "EXPECTED PANIC" : "PANIC", file, line);
    __builtin_va_list ap;
    __builtin_va_start(ap, format);
    vprintf(format, ap);
    __builtin_va_end(ap);
    printf("\n");
    fflush(NULL);
    if (expect_panic) _Exit(77);
    abort();
}
struct Page *alloc_pages(size_t n) { return buddy_alloc_pages(n); }
void free_pages(struct Page *p, size_t n) { buddy_free_pages(p, n); }
size_t nr_free_pages(void) { return buddy_nr_free_pages(); }

static void reset(void) {
    pages = test_descriptors;
    va_pa_offset = (uintptr_t)test_ram - DRAM_BASE;
    for (size_t i = 0; i < TEST_PAGE_COUNT; i++) {
        pages[i].flags = 1UL << PG_reserved;
        pages[i].property = 0;
        pages[i].ref = 0;
        list_init(&pages[i].page_link);
    }
    buddy_init();
    buddy_init_memmap(pages + 16, TEST_PAGE_COUNT - 16);
    slub_ready = 0;
    slub_init();
}

static void validate(void) {
    buddy_validate();
    slub_validate();
}

static void test_full_slab(void) {
    reset();
    size_t initial = nr_free_pages();
    void *objects[PGSIZE / 16 + 1];
    objects[0] = kmalloc(16);
    assert(objects[0] != NULL);
    struct slub_slab *slab = (struct slub_slab *)ROUNDDOWN((uintptr_t)objects[0], PGSIZE);
    size_t capacity = slab->capacity;
    for (size_t i = 1; i <= capacity; i++) {
        objects[i] = kmalloc(16);
        assert(objects[i] != NULL);
    }
    assert(nr_free_pages() == initial - 2);
    assert(!list_empty(&slub_caches[0].full));
    assert(ROUNDDOWN((uintptr_t)objects[capacity], PGSIZE) != (uintptr_t)slab);
    for (size_t i = 0; i <= capacity; i++) {
        slub_test_pattern(objects[i], 16, i % 251 + 1, 1);
    }
    kfree(objects[13]);
    void *reused = kmalloc(16);
    assert(reused == objects[13]);
    slub_test_pattern(reused, 16, 14, 1);
    validate();
    for (size_t i = 0; i <= capacity; i++) {
        slub_test_pattern(objects[i], 16, i % 251 + 1, 0);
        kfree(objects[i]);
        validate();
    }
    assert(nr_free_pages() == initial);
    printf("PASS: full slab expansion, shared pages, full/partial transitions, slot reuse, empty slab reclaim\n");
}

static void test_large_requests(void) {
    reset();
    size_t initial = nr_free_pages();
    size_t sizes[] = {2049, 4096, 4097, 3 * PGSIZE - slub_header_size(),
                     3 * PGSIZE, 4 * PGSIZE - slub_header_size() + 1};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        unsigned char *p = kmalloc(sizes[i]);
        assert(p != NULL);
        struct slub_slab *slab = (struct slub_slab *)ROUNDDOWN((uintptr_t)p, PGSIZE);
        size_t expect_pages = (sizes[i] + slub_header_size() + PGSIZE - 1) / PGSIZE;
        assert(slab->request_pages == expect_pages);
        size_t occupied = 1;
        while (occupied < expect_pages) occupied *= 2;
        assert(nr_free_pages() == initial - occupied);
        slub_test_pattern(p, sizes[i], 0x5a, 1);
        slub_test_pattern(p, sizes[i], 0x5a, 0);
        validate();
        kfree(p);
        assert(nr_free_pages() == initial);
    }
    printf("PASS: multi-page payloads, metadata space, original page request, rounded Buddy block reclamation\n");
}

static void test_exhaustion(void) {
    reset();
    size_t initial = nr_free_pages();
    void *live = kmalloc(24);
    assert(live != NULL);
    slub_test_pattern(live, 24, 0x6d, 1);
    struct Page *held[TEST_PAGE_COUNT];
    size_t count = 0;
    struct Page *p;
    while ((p = alloc_pages(1)) != NULL) held[count++] = p;
    assert(nr_free_pages() == 0);
    assert(kmalloc(2048) == NULL);
    assert(kmalloc(4097) == NULL);
    assert(kmalloc(~(size_t)0) == NULL);
    assert(kmalloc(~(size_t)0 - slub_header_size()) == NULL);
    assert(kmalloc(0) == NULL);
    kfree(NULL);
    // Existing partial slabs remain useful even when no physical page is free.
    void *other = kmalloc(24);
    assert(other != NULL && other != live);
    slub_test_pattern(live, 24, 0x6d, 0);
    validate();
    kfree(other);
    kfree(live);
    for (size_t i = 0; i < count; i++) free_pages(held[i], 1);
    assert(nr_free_pages() == initial);
    validate();
    printf("PASS: low-memory failures, existing-slab allocation, content preservation, overflow, no leaks\n");
}

static void test_kernel_check(void) {
    reset();
    void *live = kmalloc(100);
    assert(live != NULL);
    slub_test_pattern(live, 100, 0x3c, 1);
    size_t before = nr_free_pages();
    slub_check();
    assert(nr_free_pages() == before);
    slub_test_pattern(live, 100, 0x3c, 0);
    kfree(live);
    validate();
    printf("PASS: kernel self-test preserves pre-existing objects and page counts\n");
}

static void test_invalid(const char *name) {
    reset();
    void *a = kmalloc(32), *b = kmalloc(32);
    assert(a != NULL && b != NULL);
    expect_panic = 1;
    if (strcmp(name, "double-free") == 0) {
        kfree(a); kfree(a);
    } else if (strcmp(name, "interior-pointer") == 0) {
        kfree((char *)a + 1);
    } else if (strcmp(name, "unallocated-slot") == 0) {
        kfree((char *)b + 32);
    } else if (strcmp(name, "slab-header") == 0) {
        kfree((void *)ROUNDDOWN((uintptr_t)a, PGSIZE));
    } else if (strcmp(name, "foreign-pointer") == 0) {
        kfree((void *)1);
    } else if (strcmp(name, "reserved-page") == 0) {
        kfree(test_ram);
    } else if (strcmp(name, "freed-slab") == 0) {
        kfree(a); kfree(b); kfree(b);
    } else if (strcmp(name, "large-interior") == 0) {
        void *p = kmalloc(3 * PGSIZE);
        assert(p != NULL);
        kfree((char *)p + PGSIZE);
    } else if (strcmp(name, "large-double-free") == 0) {
        void *p = kmalloc(3 * PGSIZE);
        assert(p != NULL);
        kfree(p); kfree(p);
    } else if (strcmp(name, "reinitialize") == 0) {
        slub_init();
    } else {
        expect_panic = 0;
        panic("unknown negative test");
    }
    expect_panic = 0;
    panic("invalid call was accepted");
}

int main(int argc, char **argv) {
    if (argc == 2) {
        test_invalid(argv[1]);
        return 1;
    }
    test_full_slab();
    test_large_requests();
    test_exhaustion();
    test_kernel_check();
    printf("[SLUB Host Test] PASS\n");
    return 0;
}
