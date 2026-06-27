"""
patch_linker_wraps.py  –  BKA Linker Sanitizer (HLE Stub Enforcer)
==================================================================
1. Injects __wrap linker flags into CMakeLists.txt using LINKER: syntax.
2. Appends HLE stub definitions to bka_safe_base.h.
3. [NEW] Uses objcopy to rename internal symbols in compiled N64 objects
   to force the linker to route calls to our stubs.
"""

import os
import re
import subprocess

# CONFIGURATION: Set this to the directory containing your recompiled .o files
# e.g., "Android/app/src/main/cpp/libultra" or your build/obj folder
TARGET_OBJ_DIR = "Android/app/src/main/cpp/libultra" 

CMAKE_FILE = "Android/app/src/main/cpp/CMakeLists.txt"
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"

# The flags that force the linker to redirect
WRAP_FLAGS = [
    "LINKER:--wrap=__osInitialize_common",
    "LINKER:--wrap=__osViInit"
]

STUB_CODE = """
/* ── HLE Stubs ───────────────────────────────────────────────────────── */
#ifdef __cplusplus
extern "C" {
#endif

void __wrap___osInitialize_common(void) {
    // Stubbed: Prevents hardware init crash
}

void __wrap___osViInit(void) {
    // Stubbed: Prevents VI init crash
}

#ifdef __cplusplus
}
#endif
"""

def patch_object_files():
    """Rename original symbols in object files to force external resolution."""
    if not os.path.exists(TARGET_OBJ_DIR):
        print(f"Warning: Object directory {TARGET_OBJ_DIR} not found. Skipping objcopy.")
        return

    # Assuming llvm-objcopy (NDK standard)
    for root, _, files in os.walk(TARGET_OBJ_DIR):
        for file in files:
            if file.endswith(".o"):
                path = os.path.join(root, file)
                # Rename the symbols so they are no longer "found" by internal calls
                # This breaks the internal link and forces the linker to find the --wrap version
                subprocess.run([
                    "llvm-objcopy",
                    "--redefine-sym", "__osInitialize_common=__original___osInitialize_common",
                    "--redefine-sym", "__osViInit=__original___osViInit",
                    path
                ], check=False)
    print("Object files patched (symbols renamed).")

def patch_cmake():
    with open(CMAKE_FILE, 'r') as f:
        content = f.read()

    # Use the LINKER: prefix for modern CMake
    flags_block = "\n    " + "\n    ".join(WRAP_FLAGS) + "\n"
    
    if "--wrap=__osInitialize_common" in content:
        print("CMake already patched.")
    else:
        pattern = r'(target_link_options\s*\(\s*bkawrapper\s+PRIVATE\s+)'
        if re.search(pattern, content):
            new_content = re.sub(pattern, r'\1' + flags_block, content)
        else:
            new_content = content + "\ntarget_link_options(bkawrapper PRIVATE" + flags_block + ")\n"
        
        with open(CMAKE_FILE, 'w') as f:
            f.write(new_content)
        print("CMakeLists.txt updated.")

def patch_safe_base():
    with open(SAFE_BASE_FILE, 'r') as f:
        content = f.read()

    if "__wrap___osInitialize_common" in content:
        print("bka_safe_base.h already patched.")
    else:
        with open(SAFE_BASE_FILE, 'a') as f:
            f.write("\n" + STUB_CODE)
        print("bka_safe_base.h updated.")

if __name__ == "__main__":
    patch_cmake()
    patch_safe_base()
    patch_object_files()
