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

    # Updated idempotency validation trace tag
    if "SAFE BOUNDS EXTRACTOR RUNNING" in content:
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
        if (((uintptr_t)D_80007290 >> 32) == 0 && D_80007290 != NULL) {
            D_80007290 = (struct huft*)(base_prefix | (uintptr_t)D_80007290);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG", "INFLATE: in=%p, out=%p, arg2=%p", inbuf, D_80007284, D_80007290);
    
    if (gN64_RDRAM == NULL) { __android_log_print(ANDROID_LOG_FATAL, "BKA_DEBUG", "FATAL: gN64_RDRAM is NULL"); abort(); }

    // SAFE BOUNDS EXTRACTOR RUNNING
    uint32_t decSize = 0;
    if (inbuf != NULL) {
        // Unpack Big-Endian sizes from the 6-byte segment payload wrapper cleanly
        uint32_t size24 = ((uint32_t)inbuf[2] << 16) | ((uint32_t)inbuf[3] << 8) | inbuf[4];
        uint32_t size32 = ((uint32_t)inbuf[2] << 24) | ((uint32_t)inbuf[3] << 16) | ((uint32_t)inbuf[4] << 8) | inbuf[5];
        
        if (size24 > 0 && size24 < 16u * 1024u * 1024u) {
            decSize = size24;
        } else if (size32 > 0 && size32 < 32u * 1024u * 1024u) {
            decSize = size32;
        }
    }

    // Defensive buffer constraint fallback mapping
    if (decSize == 0) {
        decSize = 512u * 1024u; 
    }

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in   = (Bytef*)(inbuf + 6); 
    stream.avail_in  = 4u * 1024u * 1024u; // Safe maximum reading envelope step limit to prevent page overruns
    stream.next_out  = (Bytef*)D_80007284;
    stream.avail_out = decSize;

    // Execution Pass A: Raw Deflate Sequence Mapping Configuration
    int z_status = inflateInit2(&stream, -15);
    if (z_status == Z_OK) {
        z_status = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
    }

    // Execution Pass B: Alternative Standard Zlib/Gzip Validation Sequence Fallback
    if (z_status != Z_STREAM_END) {
        memset(&stream, 0, sizeof(stream));
        stream.next_in   = (Bytef*)(inbuf + 6);
        stream.avail_in  = 4u * 1024u * 1024u;
        stream.next_out  = (Bytef*)D_80007284;
        stream.avail_out = decSize;
        if (inflateInit2(&stream, 15 + 32) == Z_OK) {
            z_status = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG", "INFLATE HLE SUCCESS: Decompressed %u bytes. Code: %d", (uint32_t)stream.total_out, z_status);

    wp = stream.total_out; 
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
