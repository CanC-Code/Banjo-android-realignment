"""
patch_linker_wraps.py  –  BKA Linker Sanitizer (HLE Stub Enforcer)
==================================================================
1. Injects __wrap linker flags into the project's main CMakeLists.txt.
2. Appends necessary HLE stub definitions to the bka_safe_base.h file 
   to ensure the linker has concrete implementations to redirect to.
"""

import os
import re
import sys

# Path to the root CMakeLists.txt (adjust if your project structure differs)
CMAKE_FILE = "Android/app/src/main/cpp/CMakeLists.txt"
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"

WRAP_FLAGS = [
    "-Wl,--wrap=__osInitialize_common",
    "-Wl,--wrap=__osViInit"
]

STUB_CODE = """
/* ── HLE Stubs to prevent libultra crashes ────────────────────────────── */
#ifdef __cplusplus
extern "C" {
#endif

void __wrap___osInitialize_common(void) {
    // No-op: Prevents execution of N64 hardware initialization 
    // that targets illegal process-space memory (0x80000000).
}

void __wrap___osViInit(void) {
    // No-op: Prevents Video Interface init conflicts.
}

#ifdef __cplusplus
}
#endif
"""

def patch_cmake():
    if not os.path.exists(CMAKE_FILE):
        print(f"Error: {CMAKE_FILE} not found.")
        return

    with open(CMAKE_FILE, 'r') as f:
        content = f.read()

    # Avoid duplicate injection
    if "--wrap=__osInitialize_common" in content:
        print("CMake already patched.")
        return

    # Look for the target_link_options block, or inject it after project()
    pattern = r'(target_link_options\s*\(\s*bkawrapper\s+PRIVATE\s+)'
    if re.search(pattern, content):
        new_content = re.sub(pattern, r'\1\n    ' + '\n    '.join(WRAP_FLAGS) + '\n', content)
    else:
        # Fallback: append if block not found
        new_content = content + "\ntarget_link_options(bkawrapper PRIVATE\n    " + \
                      "\n    ".join(WRAP_FLAGS) + "\n)\n"

    with open(CMAKE_FILE, 'w') as f:
        f.write(new_content)
    print("CMakeLists.txt updated with linker wrap flags.")

def patch_safe_base():
    if not os.path.exists(SAFE_BASE_FILE):
        print(f"Error: {SAFE_BASE_FILE} not found.")
        return

    with open(SAFE_BASE_FILE, 'r') as f:
        content = f.read()

    if "__wrap___osInitialize_common" in content:
        print("bka_safe_base.h already patched.")
        return

    # Append to end of file
    with open(SAFE_BASE_FILE, 'a') as f:
        f.write("\n" + STUB_CODE)
    print("bka_safe_base.h updated with HLE stubs.")

if __name__ == "__main__":
    patch_cmake()
    patch_safe_base()
