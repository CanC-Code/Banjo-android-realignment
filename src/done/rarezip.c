#include <ultra64.h>
#include "rarezip.h"
#include <android/log.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>

#define LOG_TAG "BKA_RAREZIP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* FIX: must match HUFT_POOL_CAPACITY in inflate.c */
#define HUFT_POOL_CAPACITY 4096

extern u8* gN64_RDRAM;
u8 *inbuf = NULL;               // Input buffer (source data)
u8 *D_80007284 = NULL;          // Output buffer (decompressed data)
struct huft *D_80007290 = NULL;  // Huffman table pool
u32 inptr = 0;                    // Current read position in inbuf
u32 wp = 0;                       // Current write position in D_80007284
u32 bb = 0;                       // Bit buffer
u32 bk = 0;                       // Bit count
u32 crc1 = 0;                     // CRC1
u32 crc2 = -1;                    // CRC2
u32 hufts = 0;                    // Huffman table usage tracker
u32 g_decomp_out_cap = 0xFFFFFFFFu; // Output buffer capacity (default: unbounded)

/* FIX: Ensure the huft pool actually has capacity, avoiding out-of-bounds writes. */
static struct huft s_huft_pool[HUFT_POOL_CAPACITY];
struct huft *D_80007270 = s_huft_pool;

/* FIX: Mutex for thread-safe decompression */
static pthread_mutex_t g_decomp_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Forward Declarations */
u32 func_800005C0(u8* in, u8* out, struct huft *arg2);
u32 func_80000618(u8 **inPtr, u8 **outPtr, struct huft *arg2);
static u32 func_800005C0_locked(u8* in, u8* out, struct huft *arg2);

/* Universal Address Resolver */
static u8* bka_resolve_ptr(uintptr_t addr) {
    if (addr == 0) return NULL;
    u8* rdram = gN64_RDRAM;

    /* Case A: Standard N64 KSEG0 (0x80000000) or KSEG1 (0xA0000000) virtual pointers */
    if ((addr >= 0x80000000u && addr < 0x80800000u) ||
        (addr >= 0xA0000000u && addr < 0xA0800000u)) {
        return rdram + (addr & 0x00FFFFFFu);
    }

    /* Case B: Valid 64-bit Host Native Heap Pointer */
    if (addr > 0xFFFFFFFFull) {
        return (u8*)addr;
    }

    /* Case C: Low Segment / Physical RDRAM Offsets (e.g., 0x01f7f480) */
    if (rdram && addr < 0x80000000u) {
        uint32_t offset = (uint32_t)(addr & 0x00FFFFFFu);
        if (offset < (8u * 1024u * 1024u)) {
            return rdram + offset;
        }
    }

    /* Case D: Direct Host Memory Range Fallback */
    if (rdram) {
        uintptr_t rdram_start = (uintptr_t)rdram;
        uintptr_t rdram_end = rdram_start + (8u * 1024u * 1024u);
        if (addr >= rdram_start && addr < rdram_end) {
            return (u8*)addr;
        }
    }

    return (u8*)addr;
}

/* Rare LZSS Decompressor Implementation */
static uint32_t bka_rare_lzss_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
    uint8_t ring[0x1000];
    memset(ring, 0x00, sizeof(ring));
    uint32_t ring_pos = 0xFEEu;
    const uint8_t* src_end = src + src_len;
    uint8_t* dst_ptr = dst;
    const uint8_t* dst_end = dst + dst_cap;

    while (src < src_end && dst_ptr < dst_end) {
        uint8_t flags = *src++;
        for (int bit = 0; bit < 8 && src < src_end && dst_ptr < dst_end; bit++) {
            if (flags & (1u << bit)) {
                if (src >= src_end) break;
                uint8_t lit = *src++;
                *dst_ptr++ = lit;
                ring[ring_pos] = lit;
                ring_pos = (ring_pos + 1u) & 0xFFFu;
            } else {
                if (src + 1 >= src_end) break;
                uint8_t b0 = *src++;
                uint8_t b1 = *src++;
                uint32_t ring_off = (uint32_t)b0 | (((uint32_t)(b1 & 0xF0u)) << 4u);
                uint32_t length = (uint32_t)(b1 & 0x0Fu) + 3u;
                for (uint32_t i = 0; i < length && dst_ptr < dst_end; i++) {
                    uint8_t byte = ring[(ring_off + i) & 0xFFFu];
                    *dst_ptr++ = byte;
                    ring[ring_pos] = byte;
                    ring_pos = (ring_pos + 1u) & 0xFFFu;
                }
            }
        }
    }
    return (uint32_t)(dst_ptr - dst);
}

/* High-Level Hook Interfaces */
u32 func_80000550(u8* arg0) {
    if (!arg0) return 0;
    u8* p_arg0 = bka_resolve_ptr((uintptr_t)arg0);

    /* FIX: Decode big-endian 32-bit size safely for little-endian aarch64 */
    return ((uint32_t)p_arg0[2] << 24) | ((uint32_t)p_arg0[3] << 16) |
           ((uint32_t)p_arg0[4] << 8)  | ((uint32_t)p_arg0[5]);
}

void func_8000055C(void) {
    D_80007270 = s_huft_pool;
}

u32 func_80000570(u8 *inPtr, u8 *outPtr) {
    return func_800005C0(inPtr, outPtr, D_80007270);
}

u32 func_80000594(u8 **inPtr, u8 **outPtr) {
    /* FIX: Resolve the locations storing the N64 pointers */
    u32* p_inPtr_addr  = (u32*)bka_resolve_ptr((uintptr_t)inPtr);
    u32* p_outPtr_addr = (u32*)bka_resolve_ptr((uintptr_t)outPtr);

    if (!p_inPtr_addr || !p_outPtr_addr) return 0;

    /* Read the actual 32-bit N64 memory addresses */
    u32 n64_in_addr  = *p_inPtr_addr;
    u32 n64_out_addr = *p_outPtr_addr;

    /* Resolve buffer data locations to valid 64-bit Host pointers */
    u8* p_in  = bka_resolve_ptr((uintptr_t)n64_in_addr);
    u8* p_out = bka_resolve_ptr((uintptr_t)n64_out_addr);

    u8* temp_in = p_in;
    u8* temp_out = p_out;

    /* Securely lock the state and invoke the legacy inflater with host pointers */
    pthread_mutex_lock(&g_decomp_mutex);
    u32 result = func_80000618(&temp_in, &temp_out, D_80007270);
    pthread_mutex_unlock(&g_decomp_mutex);

    /* FIX: Safely calculate delta and write updated 32-bit N64 pointers back */
    *p_inPtr_addr  = n64_in_addr  + (u32)(temp_in - p_in);
    *p_outPtr_addr = n64_out_addr + (u32)(temp_out - p_out);

    return result;
}

void func_800005B8(void) {}

/* GZIP/Deflate Bridge Implementation */
u32 func_80000618(u8 **inPtr, u8 **outPtr, struct huft *arg2) {
    /* 1. Map local pointers to the global state variables required by inflate.c */
    inbuf = *inPtr;
    D_80007284 = *outPtr;
    D_80007290 = arg2;

    /* 2. Reset the read/write cursor offsets for the new block */
    inptr = 0;
    wp = 0;

    /* 3. Execute the low-level GZIP/Deflate routine */
    bkboot_inflate_unlocked();

    /* 4. Advance the original buffer pointers by the consumed/emitted byte counts */
    *inPtr += inptr;
    *outPtr += wp;

    /* 5. Return the total number of decompressed bytes written */
    return wp;
}

/* Primary Format Multiplexer and Decompression Pipeline */
u32 func_800005C0(u8* in, u8* out, struct huft *arg2) {
    u32 result;
    pthread_mutex_lock(&g_decomp_mutex);
    result = func_800005C0_locked(in, out, arg2);
    pthread_mutex_unlock(&g_decomp_mutex);
    return result;
}

static u32 func_800005C0_locked(u8* in, u8* out, struct huft *arg2) {
    u8* p_in = bka_resolve_ptr((uintptr_t)in);
    u8* p_out = bka_resolve_ptr((uintptr_t)out);

    /* Abort cleanly on invalid buffer pointers */
    if (!p_in || !p_out) {
        wp = 0;
        inptr = 0;
        return wp;
    }

    /* Guard against empty uninitialized memory blocks */
    if (p_in[0] == 0x00 && p_in[1] == 0x00 && p_in[2] == 0x00 && p_in[3] == 0x00) {
        wp = 0;
        inptr = 0;
        return wp;
    }

    uint8_t magic0 = p_in[0];
    uint8_t magic1 = p_in[1];
    u8* rdram_end = gN64_RDRAM + (8u * 1024u * 1024u);

    uint32_t dec_size = ((uint32_t)p_in[2] << 24) | ((uint32_t)p_in[3] << 16)
                      | ((uint32_t)p_in[4] << 8)  | (uint32_t)p_in[5];

    /* PATH A: Rare LZSS Format (0x50 0x10) */
    if (magic0 == 0x50 && magic1 == 0x10) {
        if (dec_size == 0) { wp = 0; inptr = 0; return wp; }
        if (dec_size < 0x04000000) {
            size_t comp_avail = (p_in + 6 >= gN64_RDRAM && p_in + 6 < rdram_end) ? (size_t)(rdram_end - (p_in + 6)) : 0x4000000;
            size_t out_cap = (p_out >= gN64_RDRAM && p_out < rdram_end) ? (size_t)(rdram_end - p_out) : dec_size;

            wp = bka_rare_lzss_decompress(p_in + 6, comp_avail, p_out, out_cap);
            return wp;
        }
    }

    /* PATH B: Standard GZIP/Deflate Format (1172 structure fallback) */
    u8* temp_in = p_in;
    u8* temp_out = p_out;
    return func_80000618(&temp_in, &temp_out, arg2);
}

/* ============================================================
   INFLATE IMPLEMENTATION (from inflate.c)
   ============================================================ */

u8 border[] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

u16 cplens[] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0
};

u8 cplext[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99
};

u16 cpdist[] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};

u8 cpdext[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

u16 mask_bits[] = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};

s32 lbits = 9;
s32 dbits = 6;

int huft_build(unsigned *b, unsigned n, unsigned s, u16 *d, u16 *e, struct huft **t, int *m) {
    unsigned a;
    unsigned c[BMAX+1];
    unsigned f;
    int g;
    int h;
    register unsigned i;
    register unsigned j;
    register int k;
    int l;
    register unsigned *p;
    register struct huft *q;
    struct huft r;
    struct huft *u[BMAX];
    unsigned v[N_MAX];
    register int w;
    unsigned x[BMAX+1];
    unsigned *xp;
    int y;
    unsigned z;

    bzero(c, sizeof(c));
    p = b; i = n;
    do {
        c[*p]++;
        p++;
    } while (--i);

    if (c[0] == n) {
        *t = (struct huft *)NULL;
        *m = 0;
        return 0;
    }

    l = *m;
    for (j = 1; j <= BMAX; j++)
        if (c[j]) break;
    k = j;
    if ((unsigned)l < j) l = j;
    for (i = BMAX; i; i--)
        if (c[i]) break;
    g = i;
    if ((unsigned)l > i) l = i;
    *m = l;

    for (y = 1 << j; j < i; j++, y <<= 1) {
        y -= c[j];
    }
    y -= c[i];
    c[i] += y;

    x[1] = j = 0;
    p = c + 1; xp = x + 2;
    while (--i) {
        *xp++ = (j += *p++);
    }

    p = b; i = 0;
    do {
        if ((j = *p++) != 0)
            v[x[j]++] = i;
    } while (++i < n);

    x[0] = i = 0;
    p = v;
    h = -1;
    w = -l;
    u[0] = (struct huft *)NULL;
    q = (struct huft *)NULL;
    z = 0;

    for (; k <= g; k++) {
        a = c[k];
        while (a--) {
            while (k > w + l) {
                h++;
                w += l;
                z = (z = g - w) > (unsigned)l ? l : z;
                if ((f = 1 << (j = k - w)) > a + 1) {
                    f -= a + 1;
                    xp = c + k;
                    while (++j < z) {
                        if ((f <<= 1) <= *++xp) break;
                        f -= *xp;
                    }
                }
                z = 1 << j;
                if (hufts + z + 1 > HUFT_POOL_CAPACITY) {
                    LOGE("huft_build: pool exhausted (need %u, capacity %u)", hufts + z + 1, (unsigned)HUFT_POOL_CAPACITY);
                    return 3;
                }
                q = D_80007290 + hufts;
                hufts += z + 1;
                *t = q + 1;
                *(t = &(q->v.t)) = (struct huft *)NULL;
                u[h] = ++q;
                if (h) {
                    x[h] = i;
                    r.b = (u8)l;
                    r.e = (u8)(16 + j);
                    r.v.t = q;
                    j = i >> (w - l);
                    u[h-1][j] = r;
                }
            }

            r.b = (u8)(k - w);
            if (p >= v + n)
                r.e = 99;
            else if (*p < s) {
                r.e = (u8)(*p < 256 ? 16 : 15);
                r.v.n = *p;
                p++;
            } else {
                r.e = *((u8 *)e + (*p - s));
                r.v.n = d[*p++ - s];
            }

            f = 1 << (k - w);
            for (j = i >> w; j < z; j += f)
                q[j] = r;

            for (j = 1 << (k - 1); i & j; j >>= 1)
                i ^= j;
            i ^= j;

            while ((i & ((1 << w) - 1)) != x[h]) {
                h--;
                w -= l;
            }
        }
    }

    return y != 0 && g != 1;
}

int inflate_codes(struct huft *tl, struct huft *td, s32 bl, s32 bd) {
    register unsigned e;
    unsigned n, d;
    unsigned w;
    struct huft *t;
    unsigned ml, md;
    register u32 b;
    register unsigned k;
    register u8 tmp;

    b = bb;
    k = bk;
    w = wp;

    ml = mask_bits[bl];
    md = mask_bits[bd];

    for (;;) {
        NEEDBITS((unsigned)bl)
        if ((e = (t = tl + ((unsigned)b & ml))->e) > 16)
            do {
                DUMPBITS(t->b)
                e -= 16;
                NEEDBITS(e)
            } while ((e = (t = t->v.t + ((unsigned)b & mask_bits[e]))->e) > 16);
        DUMPBITS(t->b)
        if (e == 16) {
            if (w >= g_decomp_out_cap) {
                LOGE("inflate_codes: output overflow prevented (w=%u cap=%u)", w, g_decomp_out_cap);
                wp = w; bb = b; bk = k;
                return 4;
            }
            tmp = (u8)t->v.n;
            D_80007284[w++] = tmp;
            crc1 += tmp;
            crc2 ^= tmp << (crc1 & 0x17);
        } else {
            if (e == 15) break;
            NEEDBITS(e)
            n = t->v.n + ((unsigned)b & mask_bits[e]);
            DUMPBITS(e);
            NEEDBITS((unsigned)bd)
            if ((e = (t = td + ((unsigned)b & md))->e) > 16)
                do {
                    DUMPBITS(t->b)
                    e -= 16;
                    NEEDBITS(e)
                } while ((e = (t = t->v.t + ((unsigned)b & mask_bits[e]))->e) > 16);
            DUMPBITS(t->b)
            NEEDBITS(e)
            d = w - t->v.n - ((unsigned)b & mask_bits[e]);
            DUMPBITS(e)
            do {
                if (w >= g_decomp_out_cap) {
                    LOGE("inflate_codes: output overflow prevented during copy (w=%u cap=%u)", w, g_decomp_out_cap);
                    wp = w; bb = b; bk = k;
                    return 4;
                }
                tmp = D_80007284[d++];
                D_80007284[w++] = tmp;
                crc1 += tmp;
                crc2 ^= tmp << (crc1 & 0x17);
            } while (--n);
        }
    }

    wp = w;
    bb = b;
    bk = k;
    return 0;
}

int inflate_stored(void) {
    unsigned n;
    unsigned w;
    register u32 b;
    register unsigned k;

    b = bb;
    k = bk;
    w = wp;

    n = k & 7;
    DUMPBITS(n);

    NEEDBITS(16)
    n = ((unsigned)b & 0xffff);
    DUMPBITS(16)
    NEEDBITS(16)
    DUMPBITS(16)

    while (n--) {
        if (w >= g_decomp_out_cap) {
            LOGE("inflate_stored: output overflow prevented (w=%u cap=%u)", w, g_decomp_out_cap);
            wp = w; bb = b; bk = k;
            return 4;
        }
        NEEDBITS(8)
        D_80007284[w++] = (u8)b;
        crc1 += b & 0xFF;
        crc2 ^= (b & 0xFF) << (crc1 & 0x17);
        DUMPBITS(8)
    }

    wp = w;
    bb = b;
    bk = k;
    return 0;
}

int inflate_fixed(void) {
    int i;
    struct huft *tl;
    struct huft *td;
    int bl;
    int bd;
    unsigned l[288];
    int r;

    for (i = 0; i < 144; i++) l[i] = 8;
    for (; i < 256; i++) l[i] = 9;
    for (; i < 280; i++) l[i] = 7;
    for (; i < 288; i++) l[i] = 8;
    bl = 7;
    r = huft_build(l, 288, 257, cplens, cplext, &tl, &bl);
    if (r == 3) {
        LOGE("inflate_fixed: literal/length huft_build failed (pool exhausted)");
        return 3;
    }

    for (i = 0; i < 30; i++) l[i] = 5;
    bd = 5;
    r = huft_build(l, 30, 0, cpdist, cpdext, &td, &bd);
    if (r == 3) {
        LOGE("inflate_fixed: distance huft_build failed (pool exhausted)");
        return 3;
    }

    return inflate_codes(tl, td, bl, bd);
}

int inflate_dynamic(void) {
    int i;
    unsigned j;
    unsigned l;
    unsigned m;
    unsigned n;
    struct huft *tl;
    struct huft *td;
    int bl;
    int bd;
    unsigned nb;
    unsigned nl;
    unsigned nd;
    int r;

    register unsigned k;
    register u32 b;

    unsigned ll[286 + 30];

    b = bb;
    k = bk;

    NEEDBITS(5)
    nl = 257 + ((unsigned)b & 0x1f);
    DUMPBITS(5)
    NEEDBITS(5)
    nd = 1 + ((unsigned)b & 0x1f);
    DUMPBITS(5)
    NEEDBITS(4)
    nb = 4 + ((unsigned)b & 0xf);
    DUMPBITS(4)

    for (j = 0; j < nb; j++) {
        NEEDBITS(3)
        ll[border[j]] = (unsigned)b & 7;
        DUMPBITS(3)
    }
    for (; j < 19; j++) ll[border[j]] = 0;

    bl = 7;
    r = huft_build(ll, 19, 19, NULL, NULL, &tl, &bl);
    if (r == 3) {
        LOGE("inflate_dynamic: code-length huft_build failed (pool exhausted)");
        bb = b; bk = k;
        return 3;
    }

    n = nl + nd;
    m = mask_bits[bl];
    i = l = 0;
    while ((unsigned)i < n) {
        NEEDBITS((unsigned)bl)
        j = (td = tl + ((unsigned)b & m))->b;
        DUMPBITS(j)
        j = td->v.n;
        if (j < 16) {
            ll[i++] = l = j;
        } else if (j == 16) {
            NEEDBITS(2)
            j = 3 + ((unsigned)b & 3);
            DUMPBITS(2)
            while (j--) ll[i++] = l;
        } else if (j == 17) {
            NEEDBITS(3)
            j = 3 + ((unsigned)b & 7);
            DUMPBITS(3)
            while (j--) ll[i++] = 0;
            l = 0;
        } else {
            NEEDBITS(7)
            j = 11 + ((unsigned)b & 0x7f);
            DUMPBITS(7)
            while (j--) ll[i++] = 0;
            l = 0;
        }
    }

    bb = b;
    bk = k;

    bl = lbits;
    r = huft_build(ll, nl, 257, cplens, cplext, &tl, &bl);
    if (r == 3) {
        LOGE("inflate_dynamic: literal/length huft_build failed (pool exhausted)");
        return 3;
    }
    bd = dbits;
    r = huft_build(ll + nl, nd, 0, cpdist, cpdext, &td, &bd);
    if (r == 3) {
        LOGE("inflate_dynamic: distance huft_build failed (pool exhausted)");
        return 3;
    }

    return inflate_codes(tl, td, bl, bd);
}

int inflate_block(int *e) {
    u32 t;
    register u32 b;
    register unsigned k;

    b = bb;
    k = bk;

    NEEDBITS(1)
    *e = (int)b & 1;
    DUMPBITS(1)

    NEEDBITS(2)
    t = (unsigned)b & 3;
    DUMPBITS(2)

    bb = b;
    bk = k;

    if (t == 2) return inflate_dynamic();
    if (t == 0) return inflate_stored();
    if (t == 1) return inflate_fixed();

    return 2;
}

// Renamed to match the wrapper in stubs.cpp
int bkboot_inflate_unlocked(void) {
    int e;
    int r;
    unsigned h;

    if (!D_80007290) {
        LOGE("bkboot_inflate_unlocked: D_80007290 (huft pool) is NULL -- aborting");
        return 3;
    }
    if (!inbuf) {
        LOGE("bkboot_inflate_unlocked: inbuf is NULL -- aborting");
        return 2;
    }
    if (!D_80007284) {
        LOGE("bkboot_inflate_unlocked: D_80007284 (output buffer) is NULL -- aborting");
        return 2;
    }

    wp = 0;
    bk = 0;
    bb = 0;

    crc1 = 0;
    crc2 = -1;

    h = 0;
    do {
        hufts = 0;
        if ((r = inflate_block(&e)) != 0)
            return r;
        if (hufts > h)
            h = hufts;
    } while (!e);

    while (bk >= 8) {
        bk -= 8;
        inptr--;
    }

    return 0;
}
