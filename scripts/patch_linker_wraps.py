"""
patch_linker_wraps.py  –  BKA Surgical Linker Patch
=====================================================
1. Renames internal libultra symbols in .o files to force external resolution.
2. Defines the renamed symbols in bka_safe_base.h.
"""

import os
import subprocess
import re

# IMPORTANT: Point this to your Android build directory where the .o files live
# Use 'find . -name "*.o" | grep initialize.o' to find the path
BUILD_OBJ_DIR = "Android/app/.cxx" 
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"

# The symbols to surgically rename
SYMBOLS_TO_RENAME = [
    "__osInitialize_common",
    "__osViInit"
]

STUB_CODE = """
/* ── HLE Stubs (Redefined to intercept internal calls) ─────────────── */
#ifdef __cplusplus
extern "C" {
#endif

void __original___osInitialize_common(void) {
    // Intercepted: Prevents N64 hardware init crash.
}

void __original___osViInit(void) {
    // Intercepted: Prevents VI init crash.
}

#ifdef __cplusplus
}
#endif
"""

def patch_object_files():
    """Rename internal symbols in object files."""
    if not os.path.exists(BUILD_OBJ_DIR):
        print(f"Error: Build directory {BUILD_OBJ_DIR} not found.")
        return

    for root, _, files in os.walk(BUILD_OBJ_DIR):
        for file in files:
            if file.endswith(".o"):
                path = os.path.join(root, file)
                
                # Check if this object file contains the problematic symbols
                result = subprocess.run(["nm", path], capture_output=True, text=True)
                
                needs_patch = False
                rename_args = []
                for sym in SYMBOLS_TO_RENAME:
                    if sym in result.stdout:
                        rename_args.extend(["--redefine-sym", f"{sym}=__original_{sym}"])
                        needs_patch = True
                
                if needs_patch:
                    print(f"Patching: {path}")
                    subprocess.run(["llvm-objcopy"] + rename_args + [path], check=True)

def patch_safe_base():
    with open(SAFE_BASE_FILE, 'r') as f:
        content = f.read()

    # Avoid duplicate definitions
    if "__original___osInitialize_common" in content:
        return

    with open(SAFE_BASE_FILE, 'a') as f:
        f.write("\n" + STUB_CODE)
    print("bka_safe_base.h updated with intercepted definitions.")

if __name__ == "__main__":
    patch_object_files()
    patch_safe_base()
