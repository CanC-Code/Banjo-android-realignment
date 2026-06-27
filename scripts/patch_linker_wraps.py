import os
import subprocess

# Points to the directory where CMake generates the object files
BUILD_OBJ_DIR = "Android/app/.cxx"
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"

# Symbols to rename in the object files to force external linking
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
        print(f"Warning: {BUILD_OBJ_DIR} not found. Skipping objcopy.")
        return

    # Prepare llvm-objcopy arguments
    args = []
    for old, new in RENAME_MAP.items():
        args.extend(["--redefine-sym", f"{old}={new}"])

    for root, _, files in os.walk(BUILD_OBJ_DIR):
        for file in files:
            if file.endswith(".o"):
                path = os.path.join(root, file)
                # Only process if the file actually contains the symbols
                res = subprocess.run(["nm", path], capture_output=True, text=True)
                if any(sym in res.stdout for sym in RENAME_MAP):
                    print(f"Surgically patching: {path}")
                    subprocess.run(["llvm-objcopy"] + args + [path], check=True)

def patch_header():
    with open(SAFE_BASE_FILE, 'r+') as f:
        content = f.read()
        if "__original___osInitialize_common" not in content:
            f.write("\n" + STUB_CODE)
            print("Header updated.")

if __name__ == "__main__":
    patch_objects()
    patch_header()
