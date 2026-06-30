import os
import subprocess
import re

# Configuration Paths
BUILD_OBJ_DIR = "Android/app/.cxx"
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"
CMAKE_FILE = "Android/app/src/main/cpp/CMakeLists.txt"
ENGINE_SRC_FILE = "Android/app/src/main/cpp/emulator/stubs.cpp"
NEW_SOURCE = "ultra/HardwareRegs.cpp"

# Symbols to rename in the object files to force external linking (HLE Stubs)
RENAME_MAP = {
    "__osInitialize_common": "__original___osInitialize_common",
    "__osViInit": "__original___osViInit"
}

# The definitions to inject into your header
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
        print(f"Warning: {BUILD_OBJ_DIR} not found. Skipping objcopy (will run on subsequent builds).")
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

    # Updated idempotency tag — bump this if you change the injected block
    if "DYNAMIC FORMAT MULTIPLEXER (GZIP/1172/UNKNOWN-v2)" in content:
        print(f"{rarezip_path} already fully patched.")
        return

    macro_injection = """#include "rarezip.h"
#include <android/log.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

extern u8* gN64_RDRAM;

#define TO_NATIVE_PTR(n64_addr) \\
    (((u32)(n64_addr) >= 0x80000000 && (u32)(n64_addr) < 0x80800000) \\
        ? (gN64_RDRAM + ((u32)(n64_addr) & 0x00FFFFFF)) \\
        : (n64_addr))
"""
    content = content.replace('#include "rarezip.h"', macro_injection)

    unsafe_func = r"(u32\s+func_800005C0\s*\([^)]+\)\s*\{)(.*?)(return\s+wp;[^\}]*\})"
    safe_func = r"""\1
    inbuf = TO_NATIVE_PTR(in);
    D_80007284 = TO_NATIVE_PTR(out);
    D_80007290 = (struct huft*)TO_NATIVE_PTR(arg2);

    // RUNTIME AUTOMATIC HOST POINTER RECONSTRUCTION (HEAL 32-BIT TRUNCATION)
    if (inbuf != NULL) {
        uintptr_t base_prefix = ((uintptr_t)inbuf) & 0xFFFFFFFF00000000ULL;
        if (((uintptr_t)D_80007284 >> 32) == 0 && D_80007284 != NULL) {
            D_80007284 = (u8*)(base_prefix | (uintptr_t)D_80007284);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG", "INFLATE: in=%p, out=%p, arg2=%p", inbuf, D_80007284, D_80007290);

    if (gN64_RDRAM == NULL) { __android_log_print(ANDROID_LOG_FATAL, "BKA_DEBUG", "FATAL: gN64_RDRAM is NULL"); abort(); }
    if (inbuf == NULL)       { __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG", "ERROR: inbuf is NULL, skipping inflate"); wp = 0; inptr = 0; \3 }
    if (D_80007284 == NULL)  { __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG", "ERROR: D_80007284 (out) is NULL, skipping inflate"); wp = 0; inptr = 0; \3 }

    // DYNAMIC FORMAT MULTIPLEXER (GZIP/1172/UNKNOWN-v2)
    uint32_t decSize = 8u * 1024u * 1024u;
    uint8_t* compressed_stream = inbuf;
    int is_1172 = 0;
    int is_gzip = 0;

    if (inbuf[0] == 0x11 && (inbuf[1] == 0x72 || inbuf[1] == 0x73)) {
        is_1172 = 1;
        decSize = ((uint32_t)inbuf[2] << 24) | ((uint32_t)inbuf[3] << 16)
                | ((uint32_t)inbuf[4] <<  8) |  (uint32_t)inbuf[5];
        compressed_stream = inbuf + 6;
        uint32_t calced_compSize = 0;
        // Walk the stream to find actual compressed byte count (best-effort)
        {
            z_stream probe;
            memset(&probe, 0, sizeof(probe));
            probe.next_in   = (Bytef*)compressed_stream;
            probe.avail_in  = 8u * 1024u * 1024u;
            probe.next_out  = (Bytef*)D_80007284;
            probe.avail_out = decSize;
            if (inflateInit2(&probe, -15) == Z_OK) {
                inflate(&probe, Z_FINISH);
                calced_compSize = (uint32_t)probe.total_in;
                inflateEnd(&probe);
            }
        }
        __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
            "INFLATE METADATA: decSize=%u, calced_compSize=%u", decSize, calced_compSize);
    } else if (inbuf[0] == 0x1F && inbuf[1] == 0x8B) {
        is_gzip = 1;
        compressed_stream = inbuf;
        __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
            "INFLATE METADATA: Standard GZIP Format Detected.");
    } else {
        // Unknown magic — log the bytes and attempt both deflate modes.
        // DO NOT memcpy a hardcoded size: we don't know the block's actual length.
        // Attempting raw deflate first is safer; if it fails we log and return 0
        // so the engine can decide how to recover rather than silently corrupting memory.
        compressed_stream = inbuf;
        __android_log_print(ANDROID_LOG_WARN, "BKA_DEBUG",
            "INFLATE METADATA: Unknown Format (%02X %02X). Attempting raw deflate fallback.",
            inbuf[0], inbuf[1]);
    }

    size_t avail_in_bounded  = 8u * 1024u * 1024u;
    size_t avail_out_bounded = decSize;

    // Clamp I/O bounds to RDRAM pool to prevent out-of-bounds reads/writes
    if (gN64_RDRAM != NULL) {
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

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in   = (Bytef*)compressed_stream;
    stream.avail_in  = (uInt)avail_in_bounded;
    stream.next_out  = (Bytef*)D_80007284;
    stream.avail_out = (uInt)avail_out_bounded;

    int z_status = Z_DATA_ERROR;

    if (is_gzip) {
        if (inflateInit2(&stream, 15 + 32) == Z_OK) {
            z_status = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
        }
    } else if (is_1172) {
        if (inflateInit2(&stream, -15) == Z_OK) {
            z_status = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
        }
    } else {
        // Unknown format: try raw deflate first, then zlib-wrapped deflate.
        // Both attempts write to the same output buffer; reset stream between tries.
        if (inflateInit2(&stream, -15) == Z_OK) {
            z_status = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
        }
        if (z_status != Z_STREAM_END) {
            memset(&stream, 0, sizeof(stream));
            stream.next_in   = (Bytef*)compressed_stream;
            stream.avail_in  = (uInt)avail_in_bounded;
            stream.next_out  = (Bytef*)D_80007284;
            stream.avail_out = (uInt)avail_out_bounded;
            if (inflateInit2(&stream, 15 + 32) == Z_OK) {
                z_status = inflate(&stream, Z_FINISH);
                inflateEnd(&stream);
            }
        }
        if (z_status != Z_STREAM_END) {
            // Both deflate modes failed. Log and return 0 — do NOT memcpy
            // a hardcoded size, as that would silently corrupt the output buffer
            // with whatever garbage bytes the engine fed us.
            __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
                "INFLATE UNKNOWN FORMAT FAILED: magic=%02X %02X, both deflate modes rejected. "
                "Returning 0 — engine must handle gracefully.",
                inbuf[0], inbuf[1]);
            wp = 0;
            inptr = 0;
            \3
        }
    }

    if (z_status == Z_STREAM_END || z_status == Z_OK) {
        __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
            "INFLATE HLE SUCCESS: Unpacked %u -> %u bytes. Status: %d",
            (uint32_t)stream.total_in, (uint32_t)stream.total_out, z_status);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "BKA_DEBUG",
            "INFLATE PARTIAL/ERROR: in=%u out=%u status=%d",
            (uint32_t)stream.total_in, (uint32_t)stream.total_out, z_status);
    }

    wp = (u32)stream.total_out;
    inptr = 0;
    \3"""

    new_content = re.sub(unsafe_func, safe_func, content, flags=re.DOTALL)

    if new_content != content:
        with open(rarezip_path, 'w') as f:
            f.write(new_content)
        print(f"Dynamically injected logging and translation into {rarezip_path}.")
    else:
        print(f"Failed to find func_800005C0 pattern in {rarezip_path}.")

if __name__ == "__main__":
    print("Running BKA Wrapper Pre-Build Patches...")
    patch_objects()
    patch_header()
    add_to_cmakelists()
    inject_init_call()
    patch_rarezip()
    print("Pre-Build Patches Complete.")
