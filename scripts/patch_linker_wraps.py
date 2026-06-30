import os
import subprocess
import re

# Configuration Paths
BUILD_OBJ_DIR = "Android/app/.cxx"
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"
CMAKE_FILE = "Android/app/src/main/cpp/CMakeLists.txt"
ENGINE_SRC_FILE = "Android/app/src/main/cpp/emulator/stubs.cpp"
NEW_SOURCE = "ultra/HardwareRegs.cpp"

RENAME_MAP = {
    "__osInitialize_common": "__original___osInitialize_common",
    "__osViInit": "__original___osViInit"
}

STUB_CODE = """
/* ── HLE Stubs (Interceptor) ─────────────────────────────────────────── */
#ifdef __cplusplus
extern "C" {
#endif

void __original___osInitialize_common(void) {
    // Stubbed: Prevents N64 hardware crash
}

void __original___osViInit(void) {
    // Stubbed: Prevents VI crash
}

#ifdef __cplusplus
}
#endif
"""

def patch_objects():
    if not os.path.exists(BUILD_OBJ_DIR):
        print(f"Warning: {BUILD_OBJ_DIR} not found. Skipping objcopy.")
        return
    args = []
    for old, new in RENAME_MAP.items():
        args.extend(["--redefine-sym", f"{old}={new}"])
    for root, _, files in os.walk(BUILD_OBJ_DIR):
        for file in files:
            if file.endswith(".o"):
                path = os.path.join(root, file)
                res = subprocess.run(["nm", path], capture_output=True, text=True)
                if any(sym in res.stdout for sym in RENAME_MAP):
                    print(f"Surgically patching: {path}")
                    subprocess.run(["llvm-objcopy"] + args + [path], check=True)

def patch_header():
    if not os.path.exists(SAFE_BASE_FILE):
        print(f"Warning: {SAFE_BASE_FILE} not found. Skipping header patch.")
        return
    with open(SAFE_BASE_FILE, 'r+') as f:
        content = f.read()
        if "__original___osInitialize_common" not in content:
            f.write("\n" + STUB_CODE)
            print(f"Header updated: {SAFE_BASE_FILE}")
        else:
            print(f"Header already patched: {SAFE_BASE_FILE}")

def add_to_cmakelists():
    if not os.path.exists(CMAKE_FILE):
        print(f"Warning: {CMAKE_FILE} not found. Skipping CMakeLists update.")
        return
    with open(CMAKE_FILE, 'r+') as f:
        content = f.read()
        if NEW_SOURCE in content:
            print(f"{NEW_SOURCE} already in CMakeLists.txt.")
            return
        pattern = r"(add_library\(bkawrapper SHARED\s+[\s\S]*?)(\s*\))"
        replacement = rf"\1\n    {NEW_SOURCE}\2"
        new_content = re.sub(pattern, replacement, content)
        if new_content != content:
            f.seek(0)
            f.write(new_content)
            f.truncate()
            print(f"Updated {CMAKE_FILE}.")
        else:
            print(f"Could not find add_library(bkawrapper SHARED ...) in {CMAKE_FILE}.")

def inject_include(file_path, include_line):
    if not os.path.exists(file_path):
        return
    with open(file_path, 'r+') as f:
        content = f.read()
        if include_line not in content:
            f.seek(0, 0)
            f.write(include_line + "\n" + content)
            print(f"Injected {include_line} into {file_path}")

def inject_init_call():
    if not os.path.exists(ENGINE_SRC_FILE):
        print(f"Warning: {ENGINE_SRC_FILE} not found. Skipping init injection.")
        return
    inject_include(ENGINE_SRC_FILE, '#include "HardwareRegs.h"')
    with open(ENGINE_SRC_FILE, 'r+') as f:
        content = f.read()
        hook = "InitHardwareRegs();"
        if hook in content:
            print("InitHardwareRegs() already injected in Engine entry.")
            return
        pattern = r"(void\s+BKA_StartEngine[^{]*?\{)"
        replacement = rf"\1\n    {hook}"
        new_content = re.sub(pattern, replacement, content)
        if new_content != content:
            f.seek(0)
            f.write(new_content)
            f.truncate()
            print(f"Injected InitHardwareRegs() into {ENGINE_SRC_FILE}.")
        else:
            print(f"Could not find BKA_StartEngine definition in {ENGINE_SRC_FILE}.")

def patch_rarezip():
    rarezip_path = "src/done/rarezip.c"
    if not os.path.exists(rarezip_path):
        print(f"Warning: {rarezip_path} not found.")
        return

    with open(rarezip_path, 'r') as f:
        content = f.read()

    IDEMPOTENCY_TAG = "DYNAMIC FORMAT MULTIPLEXER (GZIP/1172/RARE-LZSS-v5)"
    if IDEMPOTENCY_TAG in content:
        print(f"{rarezip_path} already fully patched.")
        return

    # ── Step 1: replace the include block ────────────────────────────────
    macro_injection = r"""#include "rarezip.h"
#include <android/log.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

extern u8* gN64_RDRAM;

#define TO_NATIVE_PTR(n64_addr) \
    (((u32)(n64_addr) >= 0x80000000u && (u32)(n64_addr) < 0x80800000u) \
        ? (gN64_RDRAM + ((u32)(n64_addr) & 0x00FFFFFFu)) \
        : ((u8*)(uintptr_t)(n64_addr)))

/* ── Rare LZSS decompressor ─────────────────────────────────────────────
 * Used for 0x50 0x10 blocks.  The caller already knows dec_size from the
 * arg2 struct (passed in separately); this function receives the raw
 * compressed stream with NO header bytes prepended.
 *
 * Algorithm: standard 4096-byte ring buffer LZSS, pre-filled 0x00.
 * Flag byte: bit SET   = 1 literal byte follows.
 *            bit CLEAR = 2-byte back-reference follows:
 *              byte0        = low 8 bits of ring offset
 *              byte1 hi-nib = high 4 bits of ring offset  (offset = b0 | (b1>>4)<<8)
 *              byte1 lo-nib = (length - 3)               (length = (b1&0xF)+3)
 * Flags are consumed LSB-first (bit 0 first).
 * ──────────────────────────────────────────────────────────────────────*/
static uint32_t bka_rare_lzss_decompress(
        const uint8_t* src, size_t src_len,
        uint8_t*       dst, size_t dst_cap)
{
    uint8_t  ring[0x1000];
    memset(ring, 0x00, sizeof(ring));
    uint32_t ring_pos = 0xFEEu; /* Rare initialises write head at 0xFEE */

    const uint8_t* src_end = src + src_len;
    uint8_t*       dst_ptr = dst;
    const uint8_t* dst_end = dst + dst_cap;

    while (src < src_end && dst_ptr < dst_end) {
        uint8_t flags = *src++;
        int bit;
        for (bit = 0; bit < 8 && src < src_end && dst_ptr < dst_end; bit++) {
            if (flags & (1u << bit)) {
                /* Literal */
                uint8_t lit      = *src++;
                *dst_ptr++       = lit;
                ring[ring_pos]   = lit;
                ring_pos         = (ring_pos + 1u) & 0xFFFu;
            } else {
                /* Back-reference */
                if (src + 1 >= src_end) break;
                uint8_t  b0       = *src++;
                uint8_t  b1       = *src++;
                uint32_t ring_off = (uint32_t)b0 | (((uint32_t)(b1 & 0xF0u)) << 4u);
                uint32_t length   = (uint32_t)(b1 & 0x0Fu) + 3u;
                uint32_t i;
                for (i = 0; i < length && dst_ptr < dst_end; i++) {
                    uint8_t byte     = ring[(ring_off + i) & 0xFFFu];
                    *dst_ptr++       = byte;
                    ring[ring_pos]   = byte;
                    ring_pos         = (ring_pos + 1u) & 0xFFFu;
                }
            }
        }
    }
    return (uint32_t)(dst_ptr - dst);
}
"""
    content = content.replace('#include "rarezip.h"', macro_injection)

    # ── Step 2: replace func_800005C0's body ─────────────────────────────
    #
    # Key insight from log analysis:
    #   in  = 0x720bfc9e3c
    #   arg2= 0x720bfc9e34   =>  arg2 is 8 bytes BEFORE in
    #
    # The arg2 struct layout (empirically derived from offset delta):
    #   arg2+0x00 : u32  decompressed size   (what the engine allocated)
    #   arg2+0x04 : u32  compressed size (or flags)
    #   arg2+0x08 : ptr  -> compressed data  (== in)
    #
    # For 0x50 0x10 blocks the compressed stream starts at `in` with the
    # 2-byte magic already included, so the actual payload is at in+2.
    # dec_size comes from arg2[0] (big-endian u32 at arg2+0).
    #
    # For 11 72/73 blocks the stream already has the 6-byte header and
    # dec_size is embedded there — we keep reading it from the stream.
    #
    # ALL code lives inside do{}while(0) — no file-scope statements.

    unsafe_func = r"(u32\s+func_800005C0\s*\([^)]+\)\s*\{)(.*?)(return\s+wp;[^\}]*\})"

    injected_body = (
        r"\1"
        """
    /* ═══ BKA HLE: rarezip inflate hook ══ """
        + IDEMPOTENCY_TAG +
        """ ═══ */
    inbuf      = TO_NATIVE_PTR(in);
    D_80007284 = TO_NATIVE_PTR(out);
    D_80007290 = (struct huft*)TO_NATIVE_PTR(arg2);

    /* Heal 32-bit pointer truncation */
    if (inbuf != NULL && D_80007284 != NULL) {
        uintptr_t base_prefix = ((uintptr_t)inbuf) & 0xFFFFFFFF00000000ULL;
        if (((uintptr_t)D_80007284 >> 32) == 0) {
            D_80007284 = (u8*)(base_prefix | (uintptr_t)D_80007284);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
        "INFLATE: in=%p, out=%p, arg2=%p", inbuf, D_80007284, D_80007290);

    do {
        /* ── NULL guards ──────────────────────────────────────────────── */
        if (gN64_RDRAM == NULL) {
            __android_log_print(ANDROID_LOG_FATAL, "BKA_DEBUG",
                "FATAL: gN64_RDRAM is NULL");
            abort();
        }
        if (inbuf == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
                "ERROR: inbuf is NULL");
            wp = 0; inptr = 0; break;
        }
        if (D_80007284 == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
                "ERROR: out is NULL");
            wp = 0; inptr = 0; break;
        }

        uint8_t* rdram_end = gN64_RDRAM + (8u * 1024u * 1024u);

        /* ── Log the first 8 bytes of the stream and arg2 region ─────── *
         * This gives us ground truth for future header layout decisions.  */
        {
            const uint8_t* p = inbuf;
            const uint8_t* a = (const uint8_t*)D_80007290;
            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE HDR: [%02X %02X %02X %02X %02X %02X %02X %02X]",
                (p+0 < rdram_end) ? p[0] : 0xFFu,
                (p+1 < rdram_end) ? p[1] : 0xFFu,
                (p+2 < rdram_end) ? p[2] : 0xFFu,
                (p+3 < rdram_end) ? p[3] : 0xFFu,
                (p+4 < rdram_end) ? p[4] : 0xFFu,
                (p+5 < rdram_end) ? p[5] : 0xFFu,
                (p+6 < rdram_end) ? p[6] : 0xFFu,
                (p+7 < rdram_end) ? p[7] : 0xFFu);
            /* arg2 is 8 bytes before in — log it as two u32s */
            if (a != NULL && (uint8_t*)a + 8 <= rdram_end) {
                uint32_t a0 = ((uint32_t)a[0]<<24)|((uint32_t)a[1]<<16)
                             |((uint32_t)a[2]<<8)|(uint32_t)a[3];
                uint32_t a4 = ((uint32_t)a[4]<<24)|((uint32_t)a[5]<<16)
                             |((uint32_t)a[6]<<8)|(uint32_t)a[7];
                __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                    "INFLATE ARG2: [0x%08X 0x%08X]", a0, a4);
            }
        }

        uint8_t magic0 = inbuf[0];
        uint8_t magic1 = inbuf[1];

        /* ════════════════════════════════════════════════════════════════
         * PATH A: Rare LZSS  (magic 0x50 0x10)
         *
         * Header layout (confirmed from arg2 offset analysis):
         *   in[0..1]   = 0x50 0x10  (magic, 2 bytes)
         *   arg2+0x00  = u32 big-endian decompressed size  <-- from arg2 struct
         *   in[2+]     = raw LZSS bitstream (no further header)
         *
         * The dec_size is NOT in the stream bytes [2..4]; it comes from
         * the arg2 struct which the engine populated before calling us.
         * arg2 is at (in - 8), so arg2[0..3] = dec_size.
         * ════════════════════════════════════════════════════════════════*/
        if (magic0 == 0x50 && magic1 == 0x10) {
            /* Read dec_size from arg2 struct (8 bytes before in) */
            const uint8_t* arg2_bytes = (const uint8_t*)D_80007290;
            uint32_t dec_size = 0;
            if (arg2_bytes != NULL && arg2_bytes + 4 <= rdram_end) {
                dec_size = ((uint32_t)arg2_bytes[0] << 24)
                         | ((uint32_t)arg2_bytes[1] << 16)
                         | ((uint32_t)arg2_bytes[2] <<  8)
                         |  (uint32_t)arg2_bytes[3];
            }

            /* Compressed payload starts immediately after the 2-byte magic */
            const uint8_t* comp_start = inbuf + 2;
            size_t comp_avail = (comp_start < rdram_end)
                                ? (size_t)(rdram_end - comp_start) : 0u;
            size_t out_cap    = (D_80007284 < rdram_end)
                                ? (size_t)(rdram_end - D_80007284) : 0u;
            if (dec_size > 0 && out_cap > dec_size) out_cap = dec_size;

            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE RARE-LZSS: dec_size=%u comp_avail=%zu out_cap=%zu",
                dec_size, comp_avail, out_cap);

            uint32_t written = 0;
            if (out_cap > 0 && comp_avail > 0) {
                written = bka_rare_lzss_decompress(
                    comp_start, comp_avail, D_80007284, out_cap);
            }

            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE RARE-LZSS RESULT: wrote=%u expected=%u",
                written, dec_size);

            wp = written;
            inptr = 0;
            break;
        }

        /* ════════════════════════════════════════════════════════════════
         * PATH B: Rare 0x11 0x72 / 0x11 0x73  (raw deflate, length-prefixed)
         *   in[0]    = 0x11
         *   in[1]    = 0x72 or 0x73
         *   in[2..5] = u32 big-endian decompressed size
         *   in[6+]   = raw deflate bitstream (windowBits = -15)
         * ════════════════════════════════════════════════════════════════*/
        if (magic0 == 0x11 && (magic1 == 0x72 || magic1 == 0x73)) {
            uint32_t dec_size = ((uint32_t)inbuf[2] << 24)
                              | ((uint32_t)inbuf[3] << 16)
                              | ((uint32_t)inbuf[4] <<  8)
                              |  (uint32_t)inbuf[5];
            const uint8_t* comp_start = inbuf + 6;
            size_t comp_avail = (comp_start < rdram_end)
                                ? (size_t)(rdram_end - comp_start) : 0u;
            size_t out_cap    = (D_80007284 < rdram_end)
                                ? (size_t)(rdram_end - D_80007284) : 0u;
            if (out_cap > dec_size) out_cap = dec_size;

            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            zs.next_in   = (Bytef*)comp_start;
            zs.avail_in  = (uInt)comp_avail;
            zs.next_out  = (Bytef*)D_80007284;
            zs.avail_out = (uInt)out_cap;
            int zr = Z_DATA_ERROR;
            if (inflateInit2(&zs, -15) == Z_OK) {
                zr = inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
            }
            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE 1172: decSize=%u in=%u out=%u status=%d",
                dec_size, (uint32_t)zs.total_in,
                (uint32_t)zs.total_out, zr);
            wp = (u32)zs.total_out;
            inptr = 0;
            break;
        }

        /* ════════════════════════════════════════════════════════════════
         * PATH C: Standard GZIP  (0x1F 0x8B)
         * ════════════════════════════════════════════════════════════════*/
        if (magic0 == 0x1F && magic1 == 0x8B) {
            size_t avail_in = (inbuf < rdram_end)
                              ? (size_t)(rdram_end - inbuf) : 0u;
            size_t out_cap  = (D_80007284 < rdram_end)
                              ? (size_t)(rdram_end - D_80007284) : 0u;
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            zs.next_in   = (Bytef*)inbuf;
            zs.avail_in  = (uInt)avail_in;
            zs.next_out  = (Bytef*)D_80007284;
            zs.avail_out = (uInt)out_cap;
            int zr = Z_DATA_ERROR;
            if (inflateInit2(&zs, 15 + 32) == Z_OK) {
                zr = inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
            }
            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE GZIP: in=%u out=%u status=%d",
                (uint32_t)zs.total_in, (uint32_t)zs.total_out, zr);
            wp = (u32)zs.total_out;
            inptr = 0;
            break;
        }

        /* ════════════════════════════════════════════════════════════════
         * PATH D: Unknown magic — log full header dump and return 0.
         * The HDR log line above gives us the bytes we need to identify
         * the format in the next iteration.
         * ════════════════════════════════════════════════════════════════*/
        __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
            "INFLATE UNKNOWN: magic=%02X %02X — no handler, returning 0",
            (unsigned)magic0, (unsigned)magic1);
        wp = 0; inptr = 0;
    } while (0);
    """
        r"\3"
    )

    new_content = re.sub(unsafe_func, injected_body, content, flags=re.DOTALL)

    if new_content != content:
        with open(rarezip_path, 'w') as f:
            f.write(new_content)
        print(f"Patched {rarezip_path}.")
    else:
        print(f"WARNING: Could not find func_800005C0 pattern in {rarezip_path}.")

if __name__ == "__main__":
    print("Running BKA Wrapper Pre-Build Patches...")
    patch_objects()
    patch_header()
    add_to_cmakelists()
    inject_init_call()
    patch_rarezip()
    print("Pre-Build Patches Complete.")
