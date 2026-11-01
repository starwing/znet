/* buffer_test.c -- zn_buffer.h: buffer growth, the buffer cache, recv
 * framing and send queueing.
 *
 * Pure in-memory suite: no zn_State, no sockets, no event loop, so it cannot
 * hang and needs no watchdog.
 *
 * Recv framing used below: a packet is a 2-byte big-endian payload length
 * followed by that many payload bytes.  zn_recvfinish compares the header
 * handler's return value with the bytes it has, so the handler returns the
 * whole frame length (prefix + payload), and the packet handler sees the
 * frame including its prefix.
 *
 * zn_recvfinish returns 1 while more bytes are needed and 0 once everything
 * handed in was consumed.
 *
 * The split-header (U5) and bucket-bound (U2) regressions are last on
 * purpose: an abort kills the rest of a single-process run (tests/README.md).
 */

#include "ztest.h"
#include "zn_buffer.h"

/* ---- recv recorder --------------------------------------------------- */

#define REC_MAXPKT 8
#define REC_MAXLEN 64

typedef struct rec_Recorder {
    int    packets;
    size_t len[REC_MAXPKT];
    char   data[REC_MAXPKT][REC_MAXLEN];
} rec_Recorder;

static void rec_reset(rec_Recorder *r) { memset(r, 0, sizeof(*r)); }

static void rec_packet(void *ud, const char *buff, size_t len) {
    rec_Recorder *r = (rec_Recorder *)ud;
    size_t        n = len < REC_MAXLEN ? len : REC_MAXLEN - 1;

    if (r->packets < REC_MAXPKT) {
        r->len[r->packets] = len;
        memcpy(r->data[r->packets], buff, n);
        r->data[r->packets][n] = 0;
    }
    r->packets++;
}

/* check one delivered packet against `frame` */
static void rec_assert(const rec_Recorder *r, int idx, const char *frame,
                       size_t len) {
    if (idx >= r->packets)
        zt_fail("packet %d missing (delivered %d)", idx, r->packets);
    if (r->len[idx] != len || memcmp(r->data[idx], frame, len) != 0)
        zt_fail("packet %d is %lu bytes, expected %lu", idx,
                (unsigned long)r->len[idx], (unsigned long)len);
}

/* 2-byte big-endian payload length -> whole frame length */
static size_t rec_header(void *ud, const char *buff, size_t len) {
    size_t payload;

    (void)ud;
    if (len < 2) return 0;
    payload = ((size_t)(unsigned char)buff[0] << 8)
            | (size_t)(unsigned char)buff[1];
    return payload + 2;
}

/* big-endian 16-bit write: a constant (char)0xB6 is C4310 on MSVC */
static void put_be16(char *out, unsigned v) {
    out[0] = (char)((v >> 8) & 0xFF);
    out[1] = (char)(v & 0xFF);
}

/* write one frame into out and return its length */
static size_t rec_frame(char *out, size_t payloadlen, const char *payload) {
    put_be16(out, (unsigned)payloadlen);
    memcpy(out + 2, payload, payloadlen);
    return payloadlen + 2;
}

/* hand one chunk to the parser; the parser reads it from b->buff */
static int rec_feed(zn_RecvBuffer *b, const char *chunk, size_t len) {
    memcpy(b->buff, chunk, len);
    return zn_recvfinish(b, len);
}

/* ---- plain buffer ---------------------------------------------------- */

TEST(buffer_basic) {
    zn_Buffer b;

    zn_initbuffer(&b, NULL);
    assertok(b.buff == b.init_buff);
    asserteq(b.size, ZN_BUFFERSIZE);
    asserteq(b.used, 0);

    zn_addlstring(&b, "hello", 5);
    asserteq(b.used, 5);
    assertok(memcmp(b.buff, "hello", 5) == 0);
    assertok(b.buff == b.init_buff); /* still the inline storage */
    asserteq(b.size, ZN_BUFFERSIZE);

    zn_addchar(&b, '!');
    asserteq(b.used, 6);
    asserteq(b.buff[5], '!');
    asserteq(b.size, ZN_BUFFERSIZE);

    zn_resetbuffer(&b);
    assertok(b.buff == b.init_buff);
    asserteq(b.used, 0);
    asserteq(b.size, ZN_BUFFERSIZE);
}

TEST(buffer_grow) {
    zn_Buffer b;
    char     *p;
    size_t    need, sz;

    zn_initbuffer(&b, NULL);
    zn_addlstring(&b, "head:", 5);

    need = 3000;
    p = zn_prepbuffsize(&b, need);
    assertok(p != NULL);
    assertok(b.buff != b.init_buff); /* left the inline storage */
    assertok(b.size >= b.used + need);
    asserteq(b.used, 5); /* reserving does not consume */
    assertok(p == &b.buff[5]);
    assertok(memcmp(b.buff, "head:", 5) == 0); /* data preserved */

    memset(p, 'x', need); /* write through the returned pointer */
    zn_addsize(&b, need);
    asserteq(b.used, 5 + need);
    assertok(b.buff[5] == 'x');
    assertok(b.buff[b.used - 1] == 'x');

    /* a bigger resize returns the new size and keeps the content */
    sz = zn_resizebuffer(&b, 8000);
    assertok(sz >= 8000);
    asserteq(b.size, sz);
    assertok(memcmp(b.buff, "head:", 5) == 0);
    assertok(b.buff[5] == 'x');
    assertok(b.buff[3004] == 'x');
    asserteq(b.used, 5 + need);
    zn_resetbuffer(&b);
}

TEST(buffer_resize_boundaries) {
    zn_Buffer b;
    size_t    lens[3];
    int       i;

    lens[0] = ZN_MIN_ALLOCBUFFSIZE - 1;
    lens[1] = ZN_MIN_ALLOCBUFFSIZE;
    lens[2] = 10000;
    for (i = 0; i < 3; ++i) {
        size_t sz;

        zn_initbuffer(&b, NULL);
        zn_addlstring(&b, "0123456789", 10);
        sz = zn_resizebuffer(&b, lens[i]);
        assertok(sz >= lens[i]);
        asserteq(b.size, sz);
        asserteq(b.used, 10);
        assertok(memcmp(b.buff, "0123456789", 10) == 0);
        /* the buffer must really be that big: the last byte is writable */
        b.buff[lens[i] - 1] = 'q';
        assertok(b.buff[lens[i] - 1] == 'q');
        zn_resetbuffer(&b);
    }
}

/* A buffer without a cache must free its heap block on reset; a
 * double free or a kept pointer shows up under ASan. */
TEST(buffer_no_cache_reset) {
    zn_Buffer b;

    zn_initbuffer(&b, NULL);
    assertok(zn_prepbuffsize(&b, 4 * ZN_BUFFERSIZE) != NULL);
    assertok(b.buff != b.init_buff);

    zn_resetbuffer(&b);
    assertok(b.buff == b.init_buff);
    asserteq(b.used, 0);
    asserteq(b.size, ZN_BUFFERSIZE);

    zn_resetbuffer(&b); /* nothing heap-allocated left: a no-op */
    assertok(b.buff == b.init_buff);

    /* grow again after the reset: a fresh block, freed by the next reset */
    assertok(zn_prepbuffsize(&b, ZN_BUFFERSIZE + 1) != NULL);
    assertok(b.buff != b.init_buff);
    zn_resetbuffer(&b);
    assertok(b.buff == b.init_buff);
}

/* ---- buffer cache ---------------------------------------------------- */

TEST(buffcache_alloc_free) {
    zn_BufferCache *bc = zn_newbuffcache(NULL, NULL);

    assertok(bc != NULL);
    /* U6: the result counts the buffers actually cached */
    asserteq(zn_prepbuffcache(bc, 4096, 2), 2);
    asserteq(zn_prepbuffcache(bc, 10000, 1), 1);
    asserteq(zn_prepbuffcache(NULL, 4096, 2), 0);
    zn_delbuffcache(bc);
    zn_delbuffcache(NULL); /* documented no-op */
}

/* The cache owns every block allocated for it, including the one that
 * bypasses the buckets when znZ_getcache() fails (OOM or >2 GiB).  The
 * fallback therefore has to use the cache allocator too; otherwise
 * zn_resetbuffer()/zn_delbuffcache() would free a libc pointer through a
 * custom allocator. */
#define ALLOC_LOG_MAX 8

typedef struct alloc_log {
    void *blocks[ALLOC_LOG_MAX];
    int   n;
    int   bad_free;
    int   fail_count; /* fail this many allocations, then recover */
} alloc_log;

static void *alloc_log_f(void *ud, void *ptr, size_t ns, size_t os) {
    alloc_log *log = (alloc_log *)ud;
    void      *p;
    int        i;
    (void)os;

    if (ns == 0) {
        for (i = 0; i < log->n; ++i)
            if (log->blocks[i] == ptr) {
                log->blocks[i] = NULL;
                free(ptr);
                return NULL;
            }
        log->bad_free = 1;
        return NULL;
    }
    if (log->fail_count > 0) {
        --log->fail_count;
        return NULL; /* force the getcache fallback */
    }
    p = realloc(ptr, ns);
    if (p == NULL) return NULL;
    for (i = 0; i < log->n; ++i)
        if (log->blocks[i] == ptr) {
            log->blocks[i] = p;
            return p;
        }
    if (ptr == NULL && log->n < ALLOC_LOG_MAX) log->blocks[log->n++] = p;
    return p;
}

TEST(buffcache_fallback_alloc) {
    alloc_log       log;
    zn_BufferCache *bc;
    zn_Buffer       b;

    memset(&log, 0, sizeof(log));
    bc = zn_newbuffcache(alloc_log_f, &log);
    assertok(bc != NULL);
    zn_initbuffer(&b, bc);

    /* the bucket allocation fails once, so zn_resizebuffer() takes the
     * non-cached path; the block must still belong to the cache allocator */
    log.fail_count = 1;
    assertok(zn_prepbuffsize(&b, 4096) != NULL);
    assertok(b.buff != b.init_buff);

    zn_resetbuffer(&b);  /* parks the block in the cache */
    zn_delbuffcache(bc); /* frees it through alloc_log_f */
    asserteq(log.bad_free, 0);
}

/* the cache may only hold blocks of exactly one bucket size: a fallback block
 * (OOM on the 16 KiB bucket -> plan 4/8 KiB) parked in the smallest bucket
 * would be handed out as 16 KiB and overflowed. */
TEST(buffcache_keeps_only_bucket_blocks) {
    alloc_log       log;
    zn_BufferCache *bc;
    zn_Buffer       small, big;
    char           *p;

    memset(&log, 0, sizeof(log));
    bc = zn_newbuffcache(alloc_log_f, &log);
    assertok(bc != NULL);

    zn_initbuffer(&small, bc);
    log.fail_count = 1; /* the 16 KiB bucket allocation fails */
    assertok(zn_prepbuffsize(&small, 4096) != NULL);
    asserteq(small.size, 4096);
    zn_resetbuffer(&small);
    /* the block is 4 KiB, so it must be handed back, not parked in the 16 KiB
     * bucket -- a pointer comparison would be idle: the allocator may reuse
     * that address for the bigger block either way */
    asserteq(bc->buffs[0], NULL);

    /* and the next 16 KiB request gets a real 16 KiB block */
    zn_initbuffer(&big, bc);
    p = zn_prepbuffsize(&big, 16000);
    assertok(p != NULL);
    memset(p, 0, 16000); /* ASan trap if a 4 KiB block came back */

    zn_resetbuffer(&big);
    zn_delbuffcache(bc);
}

TEST(buffer_append_oom) {
    alloc_log       log;
    zn_BufferCache *bc;
    zn_Buffer       b;
    char            big[3000];

    memset(&log, 0, sizeof(log));
    memset(big, 'x', sizeof(big));
    bc = zn_newbuffcache(alloc_log_f, &log);
    assertok(bc != NULL);
    zn_initbuffer(&b, bc);

    /* both the bucket allocation and the fallback fail: nothing is stored */
    log.fail_count = 2;
    asserteq(zn_addlstring(&b, big, sizeof(big)), 0);
    asserteq(b.used, 0);
    assertok(b.buff == b.init_buff);

    /* a full inline buffer refuses an extra char without writing past it */
    assertok(zn_addlstring(&b, big, ZN_BUFFERSIZE) == 1);
    asserteq(b.used, ZN_BUFFERSIZE);
    log.fail_count = 2;
    asserteq(zn_addchar(&b, 'z'), 0);
    asserteq(b.used, ZN_BUFFERSIZE);

    /* the allocator recovers: the same append now succeeds */
    log.fail_count = 0;
    assertok(zn_addchar(&b, 'z') == 1);
    asserteq(b.used, ZN_BUFFERSIZE + 1);

    zn_resetbuffer(&b);
    zn_delbuffcache(bc);
    asserteq(log.bad_free, 0);
}

TEST(buffcache_reuse) {
    zn_BufferCache *bc = zn_newbuffcache(NULL, NULL);
    zn_Buffer       a, b;
    char           *first;

    assertok(bc != NULL);
    zn_initbuffer(&a, bc);
    assertok(zn_prepbuffsize(&a, 4096) != NULL);
    assertok(a.buff != a.init_buff);
    assertok(bc->buffs[0] == NULL); /* straight from the allocator */
    first = a.buff;

    /* reset parks the block in the cache instead of freeing it */
    zn_resetbuffer(&a);
    assertok(a.buff == a.init_buff);
    asserteq(a.used, 0);
    assertok(bc->buffs[0] == first);

    /* the next buffer in the same size bracket reuses that exact block */
    zn_initbuffer(&b, bc);
    assertok(zn_prepbuffsize(&b, 4096) != NULL);
    assertok(b.buff == first);
    assertok(bc->buffs[0] == NULL); /* popped from the cache */
    assertok(b.size >= 4096);

    zn_resetbuffer(&b);
    assertok(bc->buffs[0] == first);
    zn_delbuffcache(bc);
}

/* Bucket arithmetic: the index must stay inside buffs[] (0..17) and the
 * returned size must agree with the index.  Nothing is allocated here. */
TEST(buffcache_boundaries) {
    size_t sizes[6];
    size_t minb = (size_t)1 << ZN_BUFFCACHE_MINLSIZE;
    size_t nslots = (size_t)ZN_BUFFCACHE_COUNT;
    int    i;

    sizes[0] = 1;
    sizes[1] = (size_t)1 << ZN_BUFFCACHE_MINLSIZE;
    sizes[2] = ((size_t)1 << ZN_BUFFCACHE_MINLSIZE) + 1;
    sizes[3] = (size_t)1 << 17;
    sizes[4] = (size_t)1 << 20;
    sizes[5] = (size_t)1 << 31;

    for (i = 0; i < 6; ++i) {
        size_t upos = 0, lpos = 0;
        size_t up = znZ_upperbound(sizes[i], &upos);
        size_t lo = znZ_lowerbound(sizes[i], &lpos);

        /* upperbound has to cover the request with a whole bucket */
        assertok(up >= sizes[i]);
        assertok(up % minb == 0);
        assertok(upos <= nslots);
        asserteq(up, minb << upos);

        /* lowerbound is the round-down inverse: it never claims a bucket
         * bigger than the block it is given, and never goes below the
         * smallest bucket. */
        assertok(lo >= minb);
        assertok(lo % minb == 0);
        assertok(lpos <= nslots);
        asserteq(lo, minb << lpos);
        if (sizes[i] >= minb) assertok(lo <= sizes[i]);

        /* exact bucket sizes are the ones the cache actually stores, so
         * they must round-trip */
        if (sizes[i] >= minb && (sizes[i] & (sizes[i] - 1)) == 0)
            asserteq(lo, sizes[i]);
    }
}

/* ---- recv ------------------------------------------------------------ */

TEST(recv_default_handlers) {
    zn_RecvBuffer rb;
    rec_Recorder  rec;
    int           ret;

    rec_reset(&rec);
    zn_initrecvbuffer(&rb, NULL);
    assertok(rb.header_handler != NULL);
    /* the default is "no handler": frames are consumed and dropped, which is
     * the documented behaviour (TODO D1 closed 2026-11-01), not a gap.  The
     * user-data pointer must be cleared even so: it used to be uninitialised.
     */
    assertok(rb.packet_ud == NULL);
    zn_recvonpacket(&rb, rec_packet, &rec);
    assertok(rb.packet_ud == &rec);

    /* a NULL handler restores the built-in one */
    zn_recvonheader(&rb, NULL, NULL);
    assertok(rb.header_handler == znX_onheader);
    zn_recvonpacket(&rb, NULL, NULL);
    assertok(rb.packet_handler == znX_onpacket);

    /* the built-in packet handler is a no-op: bytes are consumed and nothing
     * reaches the recorder */
    memcpy(rb.buff, "xy", 2);
    ret = zn_recvfinish(&rb, 2);
    assertok(ret == 0 || ret == 1);
    asserteq(rec.packets, 0);
    asserteq(rb.readed.used, 0);

    zn_recvonpacket(&rb, rec_packet, &rec);

    /* the default header hands over the whole chunk as one packet */
    memcpy(rb.buff, "abc", 3);
    ret = zn_recvfinish(&rb, 3);
    assertok(ret == 0 || ret == 1); /* must not report an error */
    asserteq(rec.packets, 1);
    asserteq(rec.len[0], 3);
    assertok(memcmp(rec.data[0], "abc", 3) == 0);
    asserteq(rb.expected, 0);
    asserteq(rb.readed.used, 0);

    /* and again: no state from the previous packet leaks into this one */
    memcpy(rb.buff, "de", 2);
    ret = zn_recvfinish(&rb, 2);
    assertok(ret == 0 || ret == 1);
    asserteq(rec.packets, 2);
    asserteq(rec.len[1], 2);
    assertok(memcmp(rec.data[1], "de", 2) == 0);
    asserteq(rb.readed.used, 0);

    /* reset must drop the handler data the buffer carried (U4) */
    zn_resetrecvbuffer(&rb);
    assertok(rb.packet_ud == NULL);
    assertok(rb.header_handler == znX_onheader);
    asserteq(rb.expected, 0);
}

TEST(recv_lenprefix) {
    zn_RecvBuffer rb;
    rec_Recorder  rec;
    char          stream[9];
    size_t        n = 0;
    int           ret;

    rec_reset(&rec);
    n += rec_frame(stream + n, 2, "AB");
    n += rec_frame(stream + n, 3, "xyz");
    asserteq(n, sizeof(stream));

    zn_initrecvbuffer(&rb, NULL);
    zn_recvonheader(&rb, rec_header, NULL);
    zn_recvonpacket(&rb, rec_packet, &rec);

    /* two complete packets in one call, delivered in stream order */
    ret = rec_feed(&rb, stream, n);
    asserteq(ret, 0);
    asserteq(rec.packets, 2);
    rec_assert(&rec, 0, stream, 4);
    rec_assert(&rec, 1, stream + 4, 5);
    asserteq(rb.readed.used, 0);
}

TEST(recv_split_packet) {
    zn_RecvBuffer rb;
    rec_Recorder  rec;
    char          frame[6];
    int           ret;

    rec_reset(&rec);
    /* header says 6 bytes total (2 prefix + 4 payload) */
    asserteq(rec_frame(frame, 4, "WXYZ"), sizeof(frame));

    zn_initrecvbuffer(&rb, NULL);
    zn_recvonheader(&rb, rec_header, NULL);
    zn_recvonpacket(&rb, rec_packet, &rec);

    /* prefix + 1 payload byte: the frame is known but not complete */
    ret = rec_feed(&rb, frame, 3);
    asserteq(ret, 1);
    asserteq(rec.packets, 0);
    asserteq(rb.expected, sizeof(frame));
    asserteq(rb.readed.used, 3);

    /* the last 3 payload bytes complete the frame, once */
    ret = rec_feed(&rb, frame + 3, 3);
    asserteq(ret, 0);
    asserteq(rec.packets, 1);
    asserteq(rec.len[0], sizeof(frame));
    rec_assert(&rec, 0, frame, sizeof(frame));
    asserteq(rb.expected, 0);
    asserteq(rb.readed.used, 0);

    /* nothing is delivered twice on a later call */
    ret = zn_recvfinish(&rb, 0);
    asserteq(ret, 0);
    asserteq(rec.packets, 1);
}

TEST(recv_multi_batches) {
    zn_RecvBuffer rb;
    rec_Recorder  rec;
    char          stream[15];
    size_t        n = 0;
    int           ret;

    rec_reset(&rec);
    n += rec_frame(stream + n, 2, "AB");   /* frame 0: 4 bytes */
    n += rec_frame(stream + n, 3, "xyz");  /* frame 1: 5 bytes */
    n += rec_frame(stream + n, 4, "qrst"); /* frame 2: 6 bytes */
    asserteq(n, sizeof(stream));

    zn_initrecvbuffer(&rb, NULL);
    zn_recvonheader(&rb, rec_header, NULL);
    zn_recvonpacket(&rb, rec_packet, &rec);

    /* uneven chunks that never split a 2-byte prefix: 6, 3, 6 bytes */
    ret = rec_feed(&rb, stream, 6); /* frame 0 + frame 1 prefix */
    asserteq(ret, 1);
    asserteq(rec.packets, 1);
    rec_assert(&rec, 0, stream, 4);

    ret = rec_feed(&rb, stream + 6, 3); /* rest of frame 1 */
    asserteq(ret, 0);
    asserteq(rec.packets, 2);
    rec_assert(&rec, 1, stream + 4, 5);

    ret = rec_feed(&rb, stream + 9, 6); /* frame 2 */
    asserteq(ret, 0);
    asserteq(rec.packets, 3);
    rec_assert(&rec, 2, stream + 9, 6);

    /* a trailing zero-length call changes nothing */
    ret = zn_recvfinish(&rb, 0);
    asserteq(ret, 0);
    asserteq(rec.packets, 3);
}

/* A header that completes in the slow path must not reset the parser: the
 * same call may still deliver several whole frames after it. */
TEST(recv_partial_header_then_frames) {
    zn_RecvBuffer rb;
    rec_Recorder  rec;
    char          stream[15];
    size_t        n = 0;
    int           ret;

    rec_reset(&rec);
    n += rec_frame(stream + n, 2, "AB");   /* frame 0: 4 bytes */
    n += rec_frame(stream + n, 3, "xyz");  /* frame 1: 5 bytes */
    n += rec_frame(stream + n, 4, "qrst"); /* frame 2: 6 bytes */
    asserteq(n, sizeof(stream));

    zn_initrecvbuffer(&rb, NULL);
    zn_recvonheader(&rb, rec_header, NULL);
    zn_recvonpacket(&rb, rec_packet, &rec);

    /* one byte of the first prefix: the header needs one more byte */
    ret = rec_feed(&rb, stream, 1);
    asserteq(ret, 1);
    asserteq(rb.readed.used, 1);

    /* the rest of frame 0 plus all of frame 1: the buffered byte takes part
     * in the header, then both frames are delivered without another split */
    ret = rec_feed(&rb, stream + 1, 3 + 5);
    asserteq(ret, 0);
    asserteq(rec.packets, 2);
    rec_assert(&rec, 0, stream, 4);
    rec_assert(&rec, 1, stream + 4, 5);
    asserteq(rb.expected, 0);
    asserteq(rb.readed.used, 0);

    /* frame 2 still parses normally */
    ret = rec_feed(&rb, stream + 9, 6);
    asserteq(ret, 0);
    asserteq(rec.packets, 3);
    rec_assert(&rec, 2, stream + 9, 6);
}

static size_t g_recv_big_len;
static int    g_recv_big_packets;

static void recv_big(void *ud, const char *buff, size_t len) {
    (void)ud;
    (void)buff;
    g_recv_big_len = len;
    g_recv_big_packets++;
}

/* A failed append must not consume the new chunk: returning -1 leaves the
 * accumulator intact so the caller can retry after the allocator recovers. */
TEST(recv_oom_slow_path) {
    alloc_log       log;
    zn_BufferCache *bc;
    zn_RecvBuffer   rb;
    char            frame[3000];
    int             ret;

    memset(&log, 0, sizeof(log));
    memset(frame, 'F', sizeof(frame));
    put_be16(frame, 2998);
    g_recv_big_len = 0;
    g_recv_big_packets = 0;

    bc = zn_newbuffcache(alloc_log_f, &log);
    assertok(bc != NULL);
    zn_initrecvbuffer(&rb, bc);
    zn_recvonheader(&rb, rec_header, NULL);
    zn_recvonpacket(&rb, recv_big, NULL);

    /* 1500 bytes fit the inline accumulator; the header expected 3000 */
    ret = rec_feed(&rb, frame, 1500);
    asserteq(ret, 1);
    asserteq(rb.expected, sizeof(frame));
    asserteq(rb.readed.used, 1500);

    /* appending 1000 more needs a bigger block; both allocations fail */
    log.fail_count = 2;
    ret = rec_feed(&rb, frame + 1500, 1000);
    asserteq(ret, -1);
    asserteq(rb.readed.used, 1500); /* nothing was consumed */

    /* the retry after the allocator recovers completes the frame */
    log.fail_count = 0;
    ret = rec_feed(&rb, frame + 1500, 1500);
    asserteq(ret, 0);
    asserteq(g_recv_big_packets, 1);
    asserteq(g_recv_big_len, sizeof(frame));
    asserteq(rb.readed.used, 0);

    zn_resetrecvbuffer(&rb);
    zn_delbuffcache(bc);
    asserteq(log.bad_free, 0);
}

TEST(recv_zero_length) {
    zn_RecvBuffer rb;
    rec_Recorder  rec;
    char          frame[4];
    int           ret;

    rec_reset(&rec);
    asserteq(rec_frame(frame, 2, "AB"), sizeof(frame));

    zn_initrecvbuffer(&rb, NULL);
    zn_recvonheader(&rb, rec_header, NULL);
    zn_recvonpacket(&rb, rec_packet, &rec);

    /* leave a half header buffered so "changed nothing" is observable */
    ret = rec_feed(&rb, frame, 1);
    asserteq(ret, 1);
    asserteq(rb.readed.used, 1);

    /* a zero-length call is a no-op, even mid-header: it must not consume
     * the buffered byte, deliver anything, or arm a frame length */
    ret = zn_recvfinish(&rb, 0);
    asserteq(ret, 0);
    asserteq(rec.packets, 0);
    asserteq(rb.expected, 0);
    asserteq(rb.readed.used, 1); /* untouched */
    assertok(memcmp(rb.readed.buff, frame, 1) == 0);

    ret = zn_recvfinish(&rb, 0);
    asserteq(ret, 0);
    asserteq(rb.readed.used, 1);
}

/* ---- send ------------------------------------------------------------ */

TEST(sendbuffer_basic) {
    zn_SendBuffer sb;

    zn_initsendbuffer(&sb, NULL);
    assertok(sb.sending == &sb.buffers[0]);
    assertok(sb.pending == &sb.buffers[1]);
    asserteq(zn_sendsize(&sb), 0);
    asserteq(sb.sent_count, 0);
    /* curbuffer is where the next payload would be appended */
    assertok(zn_curbuffer(&sb) == sb.sending);
    asserteq(zn_needsend(&sb, sb.sending), 1);
    asserteq(zn_needsend(&sb, sb.pending), 0);

    asserteq(zn_sendprepare(&sb, "abc", 3), 1); /* nothing in flight */
    asserteq(zn_sendsize(&sb), 3);
    asserteq(sb.sent_count, 0);
    assertok(memcmp(zn_sendbuff(&sb), "abc", 3) == 0);

    /* sending is busy: the next payload queues in pending and cannot be
     * handed to the socket yet */
    assertok(zn_curbuffer(&sb) == sb.pending);
    asserteq(zn_sendprepare(&sb, "de", 2), 0);
    asserteq(zn_sendsize(&sb), 3);
    asserteq(sb.pending->used, 2);
    assertok(memcmp(zn_sendbuff(&sb), "abc", 3) == 0);
    assertok(memcmp(sb.pending->buff, "de", 2) == 0);

    zn_resetsendbuffer(&sb);
}

TEST(sendbuffer_partial) {
    zn_SendBuffer sb;

    /* full send with an empty pending buffer: the swap leaves sending
     * empty and nothing is left to send */
    zn_initsendbuffer(&sb, NULL);
    asserteq(zn_sendprepare(&sb, "hello", 5), 1);
    asserteq(zn_sendfinish(&sb, 5), 0);
    assertok(sb.sending == &sb.buffers[1]); /* swapped */
    assertok(sb.pending == &sb.buffers[0]);
    asserteq(zn_sendsize(&sb), 0);
    asserteq(sb.sent_count, 0);

    /* a full send with queued data promotes pending to current */
    asserteq(zn_sendprepare(&sb, "abc", 3), 1);
    asserteq(zn_sendprepare(&sb, "de", 2), 0);
    asserteq(zn_sendfinish(&sb, 3), 1); /* "de" still to send */
    assertok(sb.sending == &sb.buffers[0]);
    assertok(sb.pending == &sb.buffers[1]);
    asserteq(zn_sendsize(&sb), 2);
    assertok(memcmp(zn_sendbuff(&sb), "de", 2) == 0);
    asserteq(sb.sent_count, 0);

    asserteq(zn_sendfinish(&sb, 2), 0); /* queue drained */
    assertok(sb.sending == &sb.buffers[1]);
    asserteq(zn_sendsize(&sb), 0);

    /* partial writes: sent_count advances and the payload stays put until
     * more than half of it has been sent */
    zn_initsendbuffer(&sb, NULL);
    asserteq(zn_sendprepare(&sb, "0123456789", 10), 1);
    asserteq(zn_sendfinish(&sb, 3), 1);
    asserteq(sb.sent_count, 3);
    asserteq(sb.sending->used, 10);
    assertok(memcmp(zn_sendbuff(&sb), "0123456789", 10) == 0);

    /* 6 of 10 sent: past half, so the unsent tail is compacted in place */
    asserteq(zn_sendfinish(&sb, 3), 1);
    asserteq(sb.sent_count, 0);
    asserteq(sb.sending->used, 4);
    assertok(memcmp(zn_sendbuff(&sb), "6789", 4) == 0);

    asserteq(zn_sendfinish(&sb, 4), 0);
    assertok(sb.sending == &sb.buffers[1]);
    asserteq(zn_sendsize(&sb), 0);
    zn_resetsendbuffer(&sb);
}

TEST(sendbuffer_oom) {
    alloc_log       log;
    zn_BufferCache *bc;
    zn_SendBuffer   sb;
    char            big[3000];

    memset(&log, 0, sizeof(log));
    memset(big, 'x', sizeof(big));
    bc = zn_newbuffcache(alloc_log_f, &log);
    assertok(bc != NULL);
    zn_initsendbuffer(&sb, bc);

    /* the payload is not accepted and nothing stale is left to send */
    log.fail_count = 2;
    asserteq(zn_sendprepare(&sb, big, sizeof(big)), -1);
    asserteq(zn_sendsize(&sb), 0);

    /* small payloads still fit the inline buffer */
    asserteq(zn_sendprepare(&sb, "a", 1), 1);
    asserteq(zn_sendsize(&sb), 1);

    zn_resetsendbuffer(&sb);
    zn_delbuffcache(bc);
    asserteq(log.bad_free, 0);
}

/* A partial send has queued a payload in `pending`; merging it into
 * `sending` can need a bigger block.  When that allocation fails the merge is
 * retried on the next finish call, so `pending` must stay intact and the
 * return value is still "something left to send". */
TEST(sendbuffer_merge_oom) {
    alloc_log       log;
    zn_BufferCache *bc;
    zn_SendBuffer   sb;
    char            big[10000];

    memset(&log, 0, sizeof(log));
    memset(big, 'y', sizeof(big));
    bc = zn_newbuffcache(alloc_log_f, &log);
    assertok(bc != NULL);
    zn_initsendbuffer(&sb, bc);

    asserteq(zn_sendprepare(&sb, big, sizeof(big)), 1);
    asserteq(zn_sendprepare(&sb, big, sizeof(big)), 0);
    asserteq(sb.sending->used, sizeof(big));
    asserteq(sb.pending->used, sizeof(big));

    log.fail_count = 2;
    asserteq(zn_sendfinish(&sb, 10), 1);
    asserteq(sb.pending->used, sizeof(big)); /* kept for the retry */

    log.fail_count = 0;
    asserteq(zn_sendfinish(&sb, 10), 1);
    asserteq(sb.pending->used, 0); /* merged on the retry */

    zn_resetsendbuffer(&sb);
    zn_delbuffcache(bc);
    asserteq(log.bad_free, 0);
}

TEST(sendbuffer_reset) {
    zn_SendBuffer sb;
    char          big[ZN_BUFFERSIZE * 2];
    int           i;

    memset(big, 'q', sizeof(big));
    zn_initsendbuffer(&sb, NULL);
    asserteq(zn_sendprepare(&sb, big, sizeof(big)), 1);
    asserteq(zn_sendprepare(&sb, big, sizeof(big)), 0);
    /* both buffers are off the inline storage; reset must free them */
    assertok(sb.buffers[0].buff != sb.buffers[0].init_buff);
    assertok(sb.buffers[1].buff != sb.buffers[1].init_buff);
    asserteq(zn_sendfinish(&sb, 10), 1);
    asserteq(sb.sent_count, 10);

    zn_resetsendbuffer(&sb);
    assertok(sb.sending == &sb.buffers[0]);
    assertok(sb.pending == &sb.buffers[1]);
    asserteq(sb.sent_count, 0);
    asserteq(zn_sendsize(&sb), 0);
    for (i = 0; i < 2; ++i) {
        assertok(sb.buffers[i].buff == sb.buffers[i].init_buff);
        asserteq(sb.buffers[i].used, 0);
        asserteq(sb.buffers[i].size, ZN_BUFFERSIZE);
    }
}

/* U5 (zn_recvfinish): a header split across reads used to be broken.  The
 * bytes were buffered in readed, but the next call handed the header handler
 * only the *new* chunk, so the 2-byte prefix was decoded from the wrong bytes
 * (here: 0x0241 == 577 instead of 0x0002).  The buffered bytes are now part
 * of the header handler's input, so the prefix decodes and the call that
 * completes the frame delivers it.  This test stays last-but-one because an
 * abort kills the rest of a single-process run (see the file comment). */
TEST(recv_split_header) {
    zn_RecvBuffer rb;
    rec_Recorder  rec;
    char          frame[4];
    int           ret;

    rec_reset(&rec);
    asserteq(rec_frame(frame, 2, "AB"), sizeof(frame));

    zn_initrecvbuffer(&rb, NULL);
    zn_recvonheader(&rb, rec_header, NULL);
    zn_recvonpacket(&rb, rec_packet, &rec);

    /* one byte of the 2-byte prefix: needs more, nothing delivered */
    ret = rec_feed(&rb, frame, 1);
    asserteq(ret, 1);
    asserteq(rec.packets, 0);
    asserteq(rb.readed.used, 1);

    /* a second single byte still leaves the prefix incomplete: the slow path
     * has to keep the buffered tail instead of consuming it */
    ret = rec_feed(&rb, frame + 1, 1);
    asserteq(ret, 1);
    asserteq(rec.packets, 0);
    asserteq(rb.readed.used, 2);

    /* the rest of the prefix plus the payload completes the frame */
    ret = rec_feed(&rb, frame + 2, 2);
    assertok(ret == 0 || ret == 1);
    asserteq(rec.packets, 1);
    rec_assert(&rec, 0, frame, sizeof(frame));
}

/* U2 (zn_buffer.h: znZ_upperbound/znZ_lowerbound): the bucket index must
 * stay inside buffs[] (0..ZN_BUFFCACHE_SLOTS-1) and the returned size must
 * agree with that index.  A request above the last bucket (2 GiB) cannot be
 * served by the cache, so both helpers return 0 and the callers fall back to
 * a plain allocation; clamping the index instead would hand a 2 GiB block to
 * a larger request.
 *
 * Only the helpers are called: nothing is allocated, so the >2 GiB case is
 * safe to assert.  Assumes a 64-bit size_t (the suite's hosts). */
TEST(buffcache_huge_index) {
    size_t pos = 99;
    size_t sz;

    /* above the last bucket: refused, and the out parameter is reset */
    sz = znZ_upperbound((size_t)1 << 33, &pos);
    asserteq(sz, 0);
    asserteq(pos, 0);
    asserteq(znZ_lowerbound((size_t)1 << 33, &pos), 0);
    asserteq(pos, 0);

    /* the last bucket itself is still served */
    sz = znZ_upperbound((size_t)1 << ZN_BUFFCACHE_MAXLSIZE, &pos);
    asserteq(sz, (size_t)1 << ZN_BUFFCACHE_MAXLSIZE);
    asserteq(pos, (size_t)ZN_BUFFCACHE_COUNT);

    /* and its index is the last valid slot */
    assertok(pos < (size_t)ZN_BUFFCACHE_SLOTS);
}

#include "buffer_test.gen.inc"
