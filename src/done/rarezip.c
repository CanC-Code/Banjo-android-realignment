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
extern u8* inbuf;
extern u8* D_80007284;
extern struct huft* D_80007290;
extern u32 wp;
extern u32 inptr;
extern u32 g_decomp_out_cap;   /* FIX: defined in inflate.c, enforced there */
extern int bkboot_inflate(void);

/* FIX: Ensure the huft pool actually has capacity, avoiding out-of-bounds 
 * writes. This is a self-contained, properly-sized pool instead of a single 
 * aliased pointer variable. */
static struct huft s_huft_pool[HUFT_POOL_CAPACITY];

/* FIX: Statically initialize directly to the pool to guarantee readiness 
 * without relying on compiler-specific JNI/constructor load orders. */
struct huft *D_80007270 = s_huft_pool;

/* FIX: Mutex implementation to secure the pipeline state. */
static pthread_mutex_t g_decomp_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Forward Declarations ─────────────────────────────────────────────── */
u32 func_800005C0(u8* in, u8* out, struct huft *arg2);
u32 func_80000618(u8 **inPtr, u8 **outPtr, struct huft *arg2);
static u32 func_800005C0_locked(u8* in, u8* out, struct huft *arg2);

/* ── Universal Address Resolver ────────────────────────────────────────── */
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

    /* Case C: Low Segment / Physical RDRAM Offsets (e.g. 0x01f7f480) */
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

/* ── Rare LZSS Decompressor Implementation ────────────────────────────── */
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

/* ── High-Level Hook Interfaces ───────────────────────────────────────── */
u32 func_80000550(u8* arg0) {
    if (!arg0) return 0;
    u8* p_arg0 = bka_resolve_ptr((uintptr_t)arg0);
    
    /* FIX: Decode big-endian 32-bit size safely for little-endian aarch64 
     * without unaligned pointer casts */
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

    /* FIX: Safely calculate delta and write updated 32-bit N64 pointers back 
     * to the emulated RDRAM space. */
    *p_inPtr_addr  = n64_in_addr  + (u32)(temp_in - p_in);
    *p_outPtr_addr = n64_out_addr + (u32)(temp_out - p_out);

    return result;
}

void func_800005B8(void) {}

/* ── Primary Format Multiplexer and Decompression Pipeline ──────────── */
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
                      | ((uint32_t)p_in[4] <<  8) |  (uint32_t)p_in[5];

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
