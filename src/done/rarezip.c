#include <ultra64.h>
#include "rarezip.h"
#include <android/log.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

extern u8* gN64_RDRAM;
extern u8* inbuf;
extern u8* D_80007284;
extern struct huft* D_80007290;
extern u32 wp;
extern u32 inptr;
extern int bkboot_inflate(void);

/* ── Pointer classification and translation ───────────────────────────── */
static u8* bka_resolve_ptr(uintptr_t addr) {
    if (addr == 0) return NULL;
    u8* rdram = gN64_RDRAM;

    /* Case A: N64 KSEG0 or KSEG1 virtual address */
    if ((addr >= 0x80000000u && addr < 0x80800000u) || 
        (addr >= 0xA0000000u && addr < 0xA0800000u)) {
        return rdram + (addr & 0x00FFFFFFu);
    }
    
    /* Case B: Valid 64-bit Host Pointer (Untruncated AArch64 alloc) */
    if (addr > 0xFFFFFFFFull) {
        return (u8*)addr; 
    }
    
    /* Case C: Truncated 64-bit Host Pointer */
    uintptr_t host_upper = ((uintptr_t)rdram) & 0xFFFFFFFF00000000ull;
    u8* reconstructed = (u8*)(host_upper | addr);
    
    return reconstructed;
}

/* ── Rare LZSS decompressor ───────────────────────────────────────────── */
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

#include "bka_safe_base.h"
#include "rarezip.h"

struct huft *D_80007270;

u32 func_800005C0(u8*, u8*, struct huft *);
u32 func_80000618(u8**, u8**, struct huft *);

extern void *D_803FBE00;

u32 func_80000550(u8*arg0){
    return *((u32*)(arg0 +2));
}

void func_8000055C(void){
    D_80007270 = (struct huft*) &D_803FBE00;
}

u32 func_80000570(u8 *inPtr, u8 *outPtr){
    return func_800005C0(inPtr, outPtr, D_80007270);
}

//rareunzip
u32 func_80000594(u8 **inPtr, u8 **outPtr){
    return func_80000618(inPtr, outPtr, D_80007270);
}

void func_800005B8(void){}

u32 func_800005C0(u8* in, u8* out, struct huft *arg2){
    /* ═══ BKA HLE: rarezip inflate hook ══ DYNAMIC FORMAT MULTIPLEXER (GZIP/1172/RARE-LZSS-v11) ═══ */
    u8* p_in = bka_resolve_ptr((uintptr_t)in);
    u8* p_out = bka_resolve_ptr((uintptr_t)out);
    u8* p_arg2 = bka_resolve_ptr((uintptr_t)arg2);

    /* Safely catch NULL pointers or blank memory boundaries */
    if (!p_in || (p_in[0] == 0x00 && p_in[1] == 0x00 && p_in[2] == 0x00 && p_in[3] == 0x00)) {
        wp = 0;
        inptr = 0;
        return wp;
    }

    uint8_t magic0 = p_in[0];
    uint8_t magic1 = p_in[1];
    u8* rdram_end = gN64_RDRAM + (8u * 1024u * 1024u);

    /* Extract size from standard header */
    uint32_t dec_size = ((uint32_t)p_in[2] << 24) | ((uint32_t)p_in[3] << 16)
                      | ((uint32_t)p_in[4] <<  8) |  (uint32_t)p_in[5];

    /* PATH A: Rare LZSS (0x50 0x10) */
    if (magic0 == 0x50 && magic1 == 0x10) {
        if (dec_size == 0) { wp = 0; inptr = 0; return wp; }
        if (dec_size < 0x04000000) {
            size_t comp_avail = (p_in + 6 >= gN64_RDRAM && p_in + 6 < rdram_end) ? (size_t)(rdram_end - (p_in + 6)) : 0x4000000;
            size_t out_cap = (p_out >= gN64_RDRAM && p_out < rdram_end) ? (size_t)(rdram_end - p_out) : 0x4000000;
            if (out_cap > dec_size) out_cap = dec_size;

            wp = bka_rare_lzss_decompress(p_in + 6, comp_avail, p_out, out_cap);
            inptr = 0;
            return wp;
        }
    }

    /* PATH B: Rare deflate (0x11 0x72 / 0x11 0x73) */
    else if (magic0 == 0x11 && (magic1 == 0x72 || magic1 == 0x73)) {
        if (dec_size == 0) { wp = 0; inptr = 0; return wp; }
        if (dec_size < 0x04000000) {
            size_t comp_avail = (p_in + 6 >= gN64_RDRAM && p_in + 6 < rdram_end) ? (size_t)(rdram_end - (p_in + 6)) : 0x4000000;
            size_t out_cap = (p_out >= gN64_RDRAM && p_out < rdram_end) ? (size_t)(rdram_end - p_out) : 0x4000000;
            if (out_cap > dec_size) out_cap = dec_size;

            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            zs.next_in = (Bytef*)(p_in + 6);
            zs.avail_in = (uInt)comp_avail;
            zs.next_out = (Bytef*)p_out;
            zs.avail_out = (uInt)out_cap;
            if (inflateInit2(&zs, -15) == Z_OK) {
                inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
            }
            wp = (u32)zs.total_out;
            inptr = 0;
            return wp;
        }
    }

    /* PATH C: Standard GZIP (0x1F 0x8B) */
    else if (magic0 == 0x1F && magic1 == 0x8B) {
        size_t comp_avail = (p_in + 2 >= gN64_RDRAM && p_in + 2 < rdram_end) ? (size_t)(rdram_end - (p_in + 2)) : 0x4000000;
        size_t out_cap = (p_out >= gN64_RDRAM && p_out < rdram_end) ? (size_t)(rdram_end - p_out) : 0x4000000;

        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        zs.next_in = (Bytef*)(p_in + 2);
        zs.avail_in = (uInt)comp_avail;
        zs.next_out = (Bytef*)p_out;
        zs.avail_out = (uInt)out_cap;
        if (inflateInit2(&zs, 15 + 32) == Z_OK) {
            inflate(&zs, Z_FINISH);
            inflateEnd(&zs);
        }
        wp = (u32)zs.total_out;
        inptr = 0;
        return wp;
    }

    /* PATH D: Fallback - Rebuild expected state sequence EXACTLY for bkboot_inflate */
    inbuf = p_in + 6;
    D_80007284 = p_out;
    D_80007290 = (struct huft*)p_arg2;
    wp = 0;
    inptr = 0;
    bkboot_inflate();
    return wp;
}


u32 func_80000618(u8 **inPtr, u8 **outPtr, struct huft *arg2){
    u32 size = func_800005C0(*inPtr, *outPtr, arg2);
    *outPtr += wp;
    *outPtr = ((u32)(*outPtr) & 0xF) ? (u8 *)BKA_TRANSLATE_ADDR(((u32)(*outPtr) & ~0xF)) + 0x10 : *outPtr;
    *inPtr += inptr + 6;
    return size;
}
