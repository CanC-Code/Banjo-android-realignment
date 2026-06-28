import os
import subprocess
import re

# Configuration Paths
BUILD_OBJ_DIR = "Android/app/.cxx"
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"
CMAKE_FILE = "Android/app/src/main/cpp/CMakeLists.txt"
# Corrected: BKA_StartEngine lives in stubs.cpp
ENGINE_SRC_FILE = "Android/app/src/main/cpp/emulator/stubs.cpp"
# Corrected: Must be relative to CMakeLists.txt
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

        # Pattern matches the add_library source list
        # Using a flexible regex that looks for the closing parenthesis of add_library
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

    # Ensure header is included so InitHardwareRegs() is declared in scope
    inject_include(ENGINE_SRC_FILE, '#include "HardwareRegs.h"')

    with open(ENGINE_SRC_FILE, 'r+') as f:
        content = f.read()
        hook = "InitHardwareRegs();"
        
        if hook in content:
            print("InitHardwareRegs() already injected in Engine entry.")
            return

        # Locate BKA_StartEngine function body
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

    # Idempotency check: don't patch multiple times
    if "BKA_DEBUG_DATA" in content:
        print(f"{rarezip_path} already fully patched.")
        return

    # 1. Inject the macro and the gN64_RDRAM extern
    macro_injection = """#include "rarezip.h"
#include <android/log.h>

extern u8* gN64_RDRAM;

#define TO_NATIVE_PTR(n64_addr) \\
    (((u32)(n64_addr) >= 0x80000000 && (u32)(n64_addr) < 0x80800000) \\
        ? (gN64_RDRAM + ((u32)(n64_addr) & 0x00FFFFFF)) \\
        : (n64_addr))
"""
    content = content.replace('#include "rarezip.h"', macro_injection)

    # 2. Replace the unsafe func_800005C0 implementation with logging + translation
    # Regex logic updated to be safe against trailing comments and whitespace
    unsafe_func = r"(u32\s+func_800005C0\s*\([^)]+\)\s*\{)(.*?)(return\s+wp;[^\}]*\})"
    safe_func = r"""\1
    inbuf = TO_NATIVE_PTR(in);
    D_80007284 = TO_NATIVE_PTR(out); 
    D_80007290 = (struct huft*)TO_NATIVE_PTR(arg2); 
    
    __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG", "INFLATE: in=%p, out=%p, arg2=%p", inbuf, D_80007284, D_80007290);
    
    // Check if the input buffer has data
    if (inbuf != NULL) { 
        __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG_DATA", "HEADER: %02x %02x %02x %02x", inbuf[0], inbuf[1], inbuf[2], inbuf[3]); 
    }
    
    if (gN64_RDRAM == NULL) { __android_log_print(ANDROID_LOG_FATAL, "BKA_DEBUG", "FATAL: gN64_RDRAM is NULL"); abort(); }

    inbuf += 6; // skip 6 byte bk header 
    wp = 0; //wp
    inptr = 0; //inptr
    
    bkboot_inflate(); //inflate
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
