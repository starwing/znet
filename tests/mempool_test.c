/* mempool_test.c -- zn_MemPool: pages, free list, pool reset.
 *
 * The pool is an intrusive free list over ZN_MAX_PAGESIZE-byte pages.  The
 * last sizeof(void*) bytes of a page link the page list, and each page is
 * carved into `size`-byte objects; unused objects are chained through their
 * own first sizeof(void*) bytes.  These are pure memory tests -- no zn_State
 * is needed.
 *
 * znM_initpool asserts its precondition (size >= sizeof(void*),
 * size % sizeof(void*) == 0 and ZN_MAX_PAGESIZE / size > 2), so every size
 * used here satisfies it: an invalid size aborts, and that abort path must
 * not be exercised here (TODO L10 wants a non-assert failure instead).
 */

#include "ztest.h"

/* one page holds (page size - link) / object size objects */
#define PAGE_OBJS \
    ((ZN_MAX_PAGESIZE - (size_t)sizeof(void*)) / sizeof(void*))

/* more than one page worth of sizeof(void*)-sized objects */
#define MAXOBJ ((size_t)3 * ((size_t)ZN_MAX_PAGESIZE / sizeof(void*)))

/* the page link lives in the last sizeof(void*) bytes of the page */
static int mempool_pages(const zn_MemPool *pool) {
    const size_t offset = (size_t)ZN_MAX_PAGESIZE - sizeof(void*);
    void *page = pool->pages;
    int n = 0;
    while (page != NULL) {
        page = *(void**)((char*)page + offset);
        ++n;
    }
    return n;
}

/* ---- init ---- */

TEST(mempool_init) {
    const size_t sizes[] = {sizeof(void*), 16, 32, 64, 1024};
    size_t i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        zn_MemPool pool;
        memset(&pool, 0x5A, sizeof(pool));
        znM_initpool(&pool, sizes[i]);
        assertok(pool.pages == NULL);
        assertok(pool.freed == NULL);
        asserteq(pool.size, sizes[i]);
        asserteq(mempool_pages(&pool), 0);
    }
}

/* ---- allocate ---- */

TEST(mempool_alloc_one) {
    zn_MemPool pool;
    void *obj;

    znM_initpool(&pool, sizeof(void*));
    asserteq(pool.pages, NULL);
    obj = znM_getobject(&pool);
    assertok(obj != NULL);
    assertok(pool.pages != NULL);
    asserteq(mempool_pages(&pool), 1);
    /* the returned object is writable for its full size */
    memset(obj, 0xA5, sizeof(void*));
    assertok(((unsigned char*)obj)[0] == 0xA5);
    assertok(((unsigned char*)obj)[sizeof(void*) - 1] == 0xA5);
    znM_freepool(&pool);
}

TEST(mempool_alloc_many_pages) {
    zn_MemPool pool;
    void *objs[MAXOBJ];
    size_t i, j;

    /* the test only proves growth if it needs more than one page */
    assertok(MAXOBJ > PAGE_OBJS);

    znM_initpool(&pool, sizeof(void*));
    for (i = 0; i < MAXOBJ; ++i) {
        objs[i] = znM_getobject(&pool);
        assertok(objs[i] != NULL);
    }

    assertok(pool.pages != NULL);
    assertok(mempool_pages(&pool) > 1);

    /* every object is distinct */
    for (i = 0; i < MAXOBJ; ++i)
        for (j = i + 1; j < MAXOBJ; ++j)
            assertok(objs[i] != objs[j]);

    /* and every object accepts a full-size write */
    for (i = 0; i < MAXOBJ; ++i) {
        memset(objs[i], 0x33, sizeof(void*));
        assertok(((unsigned char*)objs[i])[sizeof(void*) - 1] == 0x33);
    }
    znM_freepool(&pool);
}

/* ---- free list is LIFO ---- */

TEST(mempool_put_get_lifo) {
    zn_MemPool pool;
    void *a, *b;

    znM_initpool(&pool, 32);
    a = znM_getobject(&pool);
    b = znM_getobject(&pool);
    assertok(a != NULL);
    assertok(b != NULL);
    assertok(a != b);

    znM_putobject(&pool, a);
    znM_putobject(&pool, b);
    assertok(znM_getobject(&pool) == b); /* last put is first out */
    assertok(znM_getobject(&pool) == a);
    znM_freepool(&pool);
}

/* ---- freepool ---- */

TEST(mempool_freepool) {
    zn_MemPool pool;
    void *obj;

    znM_initpool(&pool, 32);
    obj = znM_getobject(&pool);
    assertok(obj != NULL);
    assertok(pool.pages != NULL);

    znM_freepool(&pool);
    assertok(pool.pages == NULL);
    assertok(pool.freed == NULL);
    asserteq(pool.size, 32); /* the size survives the reset */

    /* and the pool is usable again afterwards */
    obj = znM_getobject(&pool);
    assertok(obj != NULL);
    assertok(pool.pages != NULL);
    memset(obj, 0x11, 32);
    assertok(((unsigned char*)obj)[31] == 0x11);
    znM_freepool(&pool);
}

/* ---- pools are independent ---- */

TEST(mempool_independent) {
    zn_MemPool p1, p2;
    void *o1[PAGE_OBJS], *o2[PAGE_OBJS];
    void *a, *b;
    size_t i, j;

    znM_initpool(&p1, sizeof(void*));
    znM_initpool(&p2, sizeof(void*));

    /* a put back into one pool is returned by that pool, and the two pools
     * never hand out the same object */
    a = znM_getobject(&p1);
    b = znM_getobject(&p2);
    assertok(a != NULL);
    assertok(b != NULL);
    assertok(a != b);
    znM_putobject(&p1, a);
    assertok(znM_getobject(&p1) == a);
    znM_putobject(&p2, b);
    assertok(znM_getobject(&p2) == b);

    /* restore both pools, then take a whole page from each: no overlap */
    znM_putobject(&p1, a);
    znM_putobject(&p2, b);
    for (i = 0; i < PAGE_OBJS; ++i) {
        o1[i] = znM_getobject(&p1);
        o2[i] = znM_getobject(&p2);
        assertok(o1[i] != NULL);
        assertok(o2[i] != NULL);
    }
    /* a page each: neither pool borrowed the other's memory */
    asserteq(mempool_pages(&p1), 1);
    asserteq(mempool_pages(&p2), 1);
    for (i = 0; i < PAGE_OBJS; ++i)
        for (j = 0; j < PAGE_OBJS; ++j)
            assertok(o1[i] != o2[j]);

    znM_freepool(&p1);
    znM_freepool(&p2);
}

/* ---- the ZN_MAX_PAGESIZE constraints znM_initpool asserts ---- */

TEST(mempool_pagesize_preconditions) {
    const size_t sizes[] = {sizeof(void*), 16, 32, 64, 1024};
    const size_t sp = sizeof(void*);
    size_t i;

    assertok((sp & (sp - 1)) == 0);         /* power of two pointer */
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        const size_t sz = sizes[i];
        assertok(sz >= sp);
        asserteq(sz % sp, 0);
        asserteq((size_t)ZN_MAX_PAGESIZE % sz, 0);
        assertok((size_t)ZN_MAX_PAGESIZE > 2 * sz);
    }
}

#include "mempool_test.gen.inc"
