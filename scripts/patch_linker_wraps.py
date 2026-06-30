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

    # Idempotency tag — bump version string if you change the injected block
    IDEMPOTENCY_TAG = "DYNAMIC FORMAT MULTIPLEXER (GZIP/1172/UNKNOWN-v3)"
    if IDEMPOTENCY_TAG in content:
        print(f"{rarezip_path} already fully patched.")
        return

    # ── Step 1: replace the include block with our macro header ──────────
    macro_injection = """#include "rarezip.h"
#include <android/log.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

extern u8* gN64_RDRAM;

#define TO_NATIVE_PTR(n64_addr) \\
    (((u32)(n64_addr) >= 0x80000000u && (u32)(n64_addr) < 0x80800000u) \\
        ? (gN64_RDRAM + ((u32)(n64_addr) & 0x00FFFFFFu)) \\
        : ((u8*)(uintptr_t)(n64_addr)))
"""
    content = content.replace('#include "rarezip.h"', macro_injection)

    # ── Step 2: locate and replace func_800005C0's body ──────────────────
    #
    # The regex captures three groups:
    #   \1  – function signature up to and including the opening brace
    #   \2  – original body (discarded)
    #   \3  – the closing "return wp;" line and closing brace
    #
    # CRITICAL: all injected code must be valid C *inside* a function body.
    # No file-scope variable initialisers, no bare if-statements at top level.
    # Early-exit guards must NOT use the \3 back-reference inline; instead we
    # use a single "do { ... } while(0)" block so all paths fall through to the
    # single "return wp;" at the bottom supplied by \3.

    unsafe_func = r"(u32\s+func_800005C0\s*\([^)]+\)\s*\{)(.*?)(return\s+wp;[^\}]*\})"

    # We write the replacement as a plain string (no raw-string escaping tricks)
    # so that Python's re.sub sees \1 and \3 as group back-references and
    # everything else as literal C source.
    injected_body = (
        # ── group 1: function signature ──────────────────────────────────
        r"\1"

        # ── pointer translation ──────────────────────────────────────────
        """
    /* ── BKA HLE: rarezip inflate hook ── """ + IDEMPOTENCY_TAG + """ ── */
    inbuf       = TO_NATIVE_PTR(in);
    D_80007284  = TO_NATIVE_PTR(out);
    D_80007290  = (struct huft*)TO_NATIVE_PTR(arg2);

    /* Heal 32-bit pointer truncation: sign-extend high bits from inbuf */
    if (inbuf != NULL && D_80007284 != NULL) {
        uintptr_t base_prefix = ((uintptr_t)inbuf) & 0xFFFFFFFF00000000ULL;
        if (((uintptr_t)D_80007284 >> 32) == 0) {
            D_80007284 = (u8*)(base_prefix | (uintptr_t)D_80007284);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
        "INFLATE: in=%p, out=%p, arg2=%p", inbuf, D_80007284, D_80007290);

    /* ── guard block: all early exits set wp=0 then fall through ─────── */
    do {
        if (gN64_RDRAM == NULL) {
            __android_log_print(ANDROID_LOG_FATAL, "BKA_DEBUG",
                "FATAL: gN64_RDRAM is NULL");
            abort();
        }
        if (inbuf == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
                "ERROR: inbuf is NULL, skipping inflate");
            wp = 0; inptr = 0; break;
        }
        if (D_80007284 == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
                "ERROR: out pointer is NULL, skipping inflate");
            wp = 0; inptr = 0; break;
        }

        /* ── format detection ─────────────────────────────────────────── */
        uint32_t decSize = 8u * 1024u * 1024u;
        uint8_t* compressed_stream = inbuf;
        int is_1172 = 0;
        int is_gzip = 0;

        if (inbuf[0] == 0x11 && (inbuf[1] == 0x72 || inbuf[1] == 0x73)) {
            is_1172 = 1;
            decSize = ((uint32_t)inbuf[2] << 24) | ((uint32_t)inbuf[3] << 16)
                    | ((uint32_t)inbuf[4] <<  8) |  (uint32_t)inbuf[5];
            compressed_stream = inbuf + 6;
            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE METADATA: 1172 Format. decSize=%u", decSize);
        } else if (inbuf[0] == 0x1F && inbuf[1] == 0x8B) {
            is_gzip = 1;
            compressed_stream = inbuf;
            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE METADATA: GZIP Format.");
        } else {
            /* Unknown magic: log and attempt both deflate modes below.
               DO NOT memcpy a fixed size — we don't know the block length. */
            compressed_stream = inbuf;
            __android_log_print(ANDROID_LOG_WARN, "BKA_DEBUG",
                "INFLATE METADATA: Unknown Format (%02X %02X). "
                "Attempting raw deflate fallback.",
                (unsigned)inbuf[0], (unsigned)inbuf[1]);
        }

        /* ── clamp I/O to RDRAM pool ──────────────────────────────────── */
        size_t avail_in_bounded  = 8u * 1024u * 1024u;
        size_t avail_out_bounded = decSize;
        {
            uint8_t* rdram_end = gN64_RDRAM + (8u * 1024u * 1024u);
            if (compressed_stream >= gN64_RDRAM && compressed_stream < rdram_end) {
                size_t in_limit = (size_t)(rdram_end - compressed_stream);
                if (avail_in_bounded > in_limit) avail_in_bounded = in_limit;
            }
            if (D_80007284 >= gN64_RDRAM && D_80007284 < rdram_end) {
                size_t out_limit = (size_t)(rdram_end - D_80007284);
                if (avail_out_bounded > out_limit) avail_out_bounded = out_limit;
            }
        }

        /* ── zlib inflate ─────────────────────────────────────────────── */
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        zs.next_in   = (Bytef*)compressed_stream;
        zs.avail_in  = (uInt)avail_in_bounded;
        zs.next_out  = (Bytef*)D_80007284;
        zs.avail_out = (uInt)avail_out_bounded;

        int z_status = Z_DATA_ERROR;

        if (is_gzip) {
            if (inflateInit2(&zs, 15 + 32) == Z_OK) {
                z_status = inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
            }
        } else if (is_1172) {
            if (inflateInit2(&zs, -15) == Z_OK) {
                z_status = inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
            }
        } else {
            /* Unknown: try raw deflate first, then zlib-wrapped deflate. */
            if (inflateInit2(&zs, -15) == Z_OK) {
                z_status = inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
            }
            if (z_status != Z_STREAM_END) {
                memset(&zs, 0, sizeof(zs));
                zs.next_in   = (Bytef*)compressed_stream;
                zs.avail_in  = (uInt)avail_in_bounded;
                zs.next_out  = (Bytef*)D_80007284;
                zs.avail_out = (uInt)avail_out_bounded;
                if (inflateInit2(&zs, 15 + 32) == Z_OK) {
                    z_status = inflate(&zs, Z_FINISH);
                    inflateEnd(&zs);
                }
            }
            if (z_status != Z_STREAM_END) {
                __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
                    "INFLATE UNKNOWN FORMAT FAILED: magic=%02X %02X, "
                    "both deflate modes rejected. Returning 0.",
                    (unsigned)inbuf[0], (unsigned)inbuf[1]);
                wp = 0; inptr = 0; break;
            }
        }

        if (z_status == Z_STREAM_END || z_status == Z_OK) {
            __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
                "INFLATE HLE SUCCESS: %u -> %u bytes. Status: %d",
                (uint32_t)zs.total_in, (uint32_t)zs.total_out, z_status);
        } else {
            __android_log_print(ANDROID_LOG_WARN, "BKA_DEBUG",
                "INFLATE PARTIAL/ERROR: in=%u out=%u status=%d",
                (uint32_t)zs.total_in, (uint32_t)zs.total_out, z_status);
        }

        wp = (u32)zs.total_out;
        inptr = 0;
    } while (0);
    """

        # ── group 3: original "return wp; }" ────────────────────────────
        r"\3"
    )

    new_content = re.sub(unsafe_func, injected_body, content, flags=re.DOTALL)

    if new_content != content:
        with open(rarezip_path, 'w') as f:
            f.write(new_content)
        print(f"Patched {rarezip_path}.")
    else:
        print(f"WARNING: Could not find func_800005C0 pattern in {rarezip_path}. "
              f"No changes made.")

if __name__ == "__main__":
    print("Running BKA Wrapper Pre-Build Patches...")
    patch_objects()
    patch_header()
    add_to_cmakelists()
    inject_init_call()
    patch_rarezip()
    print("Pre-Build Patches Complete.")
