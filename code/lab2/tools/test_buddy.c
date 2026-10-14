/* Standalone host tests: include the actual implementation to inspect private
 * state without adding public kernel interfaces or changing the build files. */
#include "../kern/mm/buddy_pmm.c"

extern int printf(const char *format, ...);
extern int vprintf(const char *format, __builtin_va_list args);
extern int fflush(void *stream);
extern void _Exit(int status) __attribute__((noreturn));
extern void abort(void) __attribute__((noreturn));

static struct Page test_pages[BUDDY_CAPACITY];
struct Page *pages = test_pages;
size_t npage;
const size_t nbase = DRAM_BASE / PGSIZE;
uint64_t va_pa_offset;
const struct pmm_manager *pmm_manager = &buddy_pmm_manager;

// This bitmap is independent of the allocator's lists and head metadata.
enum { MODEL_RESERVED, MODEL_FREE, MODEL_ALLOC };
static unsigned char model[BUDDY_CAPACITY];
static bool expect_panic;

int cprintf(const char *format, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, format);
    int result = vprintf(format, args);
    __builtin_va_end(args);
    return result;
}

void __panic(const char *file, int line, const char *format, ...) {
    printf("%s: %s:%d: ", expect_panic ? "EXPECTED PANIC" : "PANIC", file, line);
    __builtin_va_list args;
    __builtin_va_start(args, format);
    vprintf(format, args);
    __builtin_va_end(args);
    printf("\n");
    fflush(NULL);
    if (expect_panic) {
        _Exit(77);
    }
    abort();
}

static void reset(size_t count) {
    assert(count > 0 && count <= BUDDY_CAPACITY);
    npage = nbase + count;
    pages = test_pages;
    memset(model, MODEL_RESERVED, sizeof(model));
    for (size_t i = 0; i < count; i++) {
        test_pages[i].flags = 1UL << PG_reserved;
        test_pages[i].property = 123;
        test_pages[i].ref = 17;
        list_init(&test_pages[i].page_link);
    }
    buddy_init();
}

static void verify_model(void) {
    buddy_validate();
    size_t free_count = 0;
    for (size_t i = 0; i < buddy_page_count(); i++) {
        if (model[i] == MODEL_RESERVED) {
            assert(buddy_seen[i] == 0 && PageReserved(pages + i));
        } else if (model[i] == MODEL_FREE) {
            assert(buddy_seen[i] == 1);
            free_count++;
        } else {
            assert(buddy_seen[i] == 2);
        }
    }
    assert(buddy_nr_free_pages() == free_count);
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    return hash;
}

static uint64_t state_hash(void) {
    uint64_t hash = 1469598103934665603ULL;
    size_t count = buddy_page_count();
    hash = hash_bytes(hash, pages, count * sizeof(*pages));
    hash = hash_bytes(hash, buddy_info, count * sizeof(*buddy_info));
    hash = hash_bytes(hash, buddy_lists, sizeof(buddy_lists));
    return hash_bytes(hash, &buddy_free_count, sizeof(buddy_free_count));
}

static void register_range(size_t index, size_t count) {
    assert(count > 0 && count <= buddy_page_count() - index);
    for (size_t i = index; i < index + count; i++) {
        assert(model[i] == MODEL_RESERVED);
    }
    buddy_init_memmap(pages + index, count);
    for (size_t i = index; i < index + count; i++) {
        model[i] = MODEL_FREE;
    }
    verify_model();
}

static size_t rounded_request(size_t n) {
    if (n > BUDDY_CAPACITY) {
        return 0;
    }
    size_t rounded = 1;
    while (rounded < n) {
        rounded *= 2;
    }
    return rounded;
}

// Derive maximal aligned intervals from the bitmap, not from buddy metadata.
static struct Page *expected_allocation(size_t rounded) {
    if (rounded == 0) {
        return NULL;
    }
    size_t count = buddy_page_count(), best_size = BUDDY_CAPACITY + 1;
    struct Page *best = NULL;
    for (size_t index = 0; index < count;) {
        if (model[index] != MODEL_FREE) {
            index++;
            continue;
        }
        size_t end = index;
        while (end < count && model[end] == MODEL_FREE) {
            end++;
        }
        while (index < end) {
            size_t size = 1;
            while (size <= BUDDY_CAPACITY / 2 && size * 2 <= end - index &&
                   (nbase + index) % (size * 2) == 0) {
                size *= 2;
            }
            if (size >= rounded && size < best_size) {
                best = pages + index;
                best_size = size;
            }
            index += size;
        }
    }
    return best;
}

static struct Page *allocate(size_t n) {
    size_t rounded = rounded_request(n);
    struct Page *expected = expected_allocation(rounded);
    size_t before = buddy_nr_free_pages();
    uint64_t before_hash = state_hash();
    struct Page *p = buddy_alloc_pages(n);
    assert(p == expected);
    if (p == NULL) {
        assert(state_hash() == before_hash);
        assert(buddy_nr_free_pages() == before);
    } else {
        size_t index = p - pages;
        assert(page2ppn(p) % rounded == 0);
        assert(rounded <= buddy_page_count() - index);
        for (size_t i = index; i < index + rounded; i++) {
            assert(model[i] == MODEL_FREE);
            assert(page_ref(pages + i) == 0);
            model[i] = MODEL_ALLOC;
        }
        assert(buddy_nr_free_pages() == before - rounded);
    }
    verify_model();
    return p;
}

static void release(struct Page *p, size_t request) {
    size_t index = p - pages;
    size_t rounded = rounded_request(request);
    size_t before = buddy_nr_free_pages();
    for (size_t i = index; i < index + rounded; i++) {
        assert(model[i] == MODEL_ALLOC);
        set_page_ref(pages + i, 7);
    }
    buddy_free_pages(p, request);
    for (size_t i = index; i < index + rounded; i++) {
        model[i] = MODEL_FREE;
        assert(page_ref(pages + i) == 0);
    }
    assert(buddy_nr_free_pages() == before + rounded);
    verify_model();
}

static void test_layout(void) {
    reset(128);
    register_range(3, 29);
    register_range(64, 16);
    register_range(48, 8);
    assert(buddy_nr_free_pages() == 53);
    assert(allocate(32) == NULL);
    register_range(0, 3);
    assert(allocate(32) == pages);
    printf("PASS: unaligned/non-power-of-two registration, disjoint ranges, filling a hole\n");

    reset(128);
    register_range(0, 4);
    register_range(8, 4);
    assert(allocate(8) == NULL);
    register_range(4, 4);
    struct Page *p = allocate(8);
    assert(p == pages);
    release(p, 8);
    assert(allocate(16) == NULL);
    printf("PASS: registered gap versus unregistered hole\n");

    reset(127);
    register_range(120, 7);
    assert(allocate(8) == NULL);
    assert(allocate(4) == pages + 120);
    assert(allocate(2) == pages + 124);
    assert(allocate(1) == pages + 126);
    printf("PASS: management upper boundary and complete tail-page coverage\n");
}

static void test_allocation(void) {
    const size_t requests[] = {1, 2, 4, 8, 3, 5, 9};
    reset(128);
    register_range(0, 128);
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        struct Page *p = allocate(requests[i]);
        assert(p == pages);
        release(p, requests[i]);
        assert(buddy_nr_free_pages() == 128);
    }
    printf("PASS: power-of-two and rounded requests, recursive splitting, original-size free\n");

    reset(128);
    register_range(32, 4);
    register_range(8, 4);
    register_range(64, 8);
    assert(allocate(3) == pages + 8);
    assert(allocate(3) == pages + 32);
    assert(allocate(3) == pages + 64);
    printf("PASS: lowest address in the smallest available order\n");

    reset(BUDDY_CAPACITY);
    register_range(0, BUDDY_CAPACITY);
    size_t maximum = buddy_block_size(buddy_max_order);
    struct Page *p = allocate(maximum);
    assert(p == pages);
    assert(allocate(1) == NULL);
    release(p, maximum);
    assert(allocate(maximum + 1) == NULL);
    assert(allocate(~(size_t)0) == NULL);
    printf("PASS: maximum supported order, exhaustion, oversized and integer-limit requests\n");
}

static void test_merging(void) {
    reset(64);
    register_range(0, 16);
    struct Page *p[16];
    for (size_t i = 0; i < 16; i++) {
        p[i] = allocate(1);
        assert(p[i] == pages + i);
    }
    release(p[0], 1);
    assert(allocate(2) == NULL);
    release(p[1], 1);
    struct Page *pair = allocate(2);
    assert(pair == pages);
    release(pair, 2);
    // An odd stride permutes all remaining leaf blocks before final coalescing.
    for (size_t step = 0; step < 16; step++) {
        size_t i = (step * 5) % 16;
        if (i >= 2) {
            release(p[i], 1);
        }
    }
    assert(allocate(16) == pages);
    printf("PASS: occupied partner, partner merging, multi-level merging in permuted order\n");

    reset(64);
    register_range(4, 4);
    register_range(8, 4);
    assert(allocate(8) == NULL);
    assert(buddy_nr_free_pages() == 8);
    printf("PASS: adjacent same-order blocks that are not partners stay separate\n");

    reset(64);
    register_range(0, 4);
    struct Page *single = allocate(1);
    assert(single == pages);
    assert(allocate(4) == NULL);
    release(single, 1);
    assert(allocate(4) == pages);
    printf("PASS: free partner in a different order cannot be merged prematurely\n");
}

static void test_random(void) {
    reset(1024);
    register_range(3, 501);
    register_range(600, 424);
    struct Page *p[64] = {0};
    size_t request[64] = {0};
    unsigned int seed = 0x20261013U;
    for (size_t step = 0; step < 5000; step++) {
        size_t slot = buddy_test_random(&seed) % 64;
        if (p[slot] != NULL) {
            release(p[slot], request[slot]);
            p[slot] = NULL;
        } else {
            request[slot] = buddy_test_random(&seed) % 65 + 1;
            p[slot] = allocate(request[slot]);
        }
    }
    for (size_t i = 0; i < 64; i++) {
        if (p[i] != NULL) {
            release(p[i], request[i]);
        }
    }
    assert(buddy_nr_free_pages() == 925);
    printf("PASS: 5000 mixed operations checked against an independent bitmap oracle\n");
}

static void test_startup_check(void) {
    reset(1024);
    register_range(3, 1021);
    // Ensure the self-test also preserves allocations that predate the test.
    struct Page *held = allocate(3);
    uint64_t before = state_hash();
    buddy_check();
    assert(state_hash() == before);
    verify_model();
    release(held, 3);
    printf("PASS: live startup check restores all allocator state and pre-existing allocations\n");

    reset(1);
    buddy_check();
    register_range(0, 1);
    buddy_check();
    verify_model();
    printf("PASS: startup check with zero or one free page\n");
}

static void test_invalid(const char *name) {
    reset(64);
    register_range(0, 32);
    struct Page *p = allocate(3);
    expect_panic = 1;
    if (strcmp(name, "alloc-zero") == 0) {
        buddy_alloc_pages(0);
    } else if (strcmp(name, "free-zero") == 0) {
        buddy_free_pages(p, 0);
    } else if (strcmp(name, "double-free") == 0) {
        buddy_free_pages(p, 3);
        buddy_free_pages(p, 3);
    } else if (strcmp(name, "interior-free") == 0) {
        buddy_free_pages(p + 1, 3);
    } else if (strcmp(name, "wrong-request") == 0) {
        buddy_free_pages(p, 4);
    } else if (strcmp(name, "reserved-free") == 0) {
        buddy_free_pages(pages + 40, 1);
    } else if (strcmp(name, "free-page-free") == 0) {
        buddy_free_pages(pages + 16, 1);
    } else if (strcmp(name, "out-of-range-free") == 0) {
        buddy_free_pages(pages + 64, 1);
    } else if (strcmp(name, "unaligned-descriptor") == 0) {
        buddy_free_pages((struct Page *)((uintptr_t)p + 1), 3);
    } else if (strcmp(name, "register-zero") == 0) {
        buddy_init_memmap(pages + 40, 0);
    } else if (strcmp(name, "register-overlap") == 0) {
        buddy_init_memmap(pages + 16, 4);
    } else if (strcmp(name, "register-out-of-range") == 0) {
        buddy_init_memmap(pages + 60, 8);
    } else if (strcmp(name, "metadata-capacity") == 0) {
        npage = nbase + BUDDY_CAPACITY + 1;
        buddy_init_memmap(pages + 40, 1);
    } else {
        expect_panic = 0;
        panic("unknown negative test");
    }
    // A normal return is a test failure, not a passing negative case.
    expect_panic = 0;
    panic("invalid call was accepted");
}

int main(int argc, char **argv) {
    if (argc == 2) {
        test_invalid(argv[1]);
        return 1;
    }
    test_layout();
    test_allocation();
    test_merging();
    test_random();
    test_startup_check();
    printf("[Buddy Host Test] PASS\n");
    return 0;
}
