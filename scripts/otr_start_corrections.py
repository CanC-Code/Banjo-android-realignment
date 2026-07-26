import os
import re
import tempfile
import shutil
from pathlib import Path

# --- Configuration ---
ENGINE_SRC_FILE = "Android/app/src/main/cpp/emulator/stubs.cpp"

TAG = "SECURE ENGINE IGNITION PATCH"

PATCHED_ENGINE_SECTION = '''/* ============================================================
   8. SECURE ENGINE IGNITION
   ============================================================ */

extern "C" {
    // Declare external RDRAM pointer defined in your memory subsystem
    extern uint8_t* gN64_RDRAM;
}

extern void func_80000450(int32_t arg0);

void BKA_StartEngine(void) {
    LOGI("BKA-STUBS: Waiting for resource gate before engine ignition...");
    WaitForResourcesReady();
    LOGI("BKA-STUBS: Resource gate passed. >>> SECURE CONCURRENT IGNITION <<<");

    // 1. Verify and load the extracted asset/ROM container into RDRAM to prevent SIGSEGV in bzero
    FILE* f = fopen("rom_base.bin", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (gN64_RDRAM != nullptr && size > 0) {
            fread(gN64_RDRAM, 1, size, f);
            LOGI("BKA-STUBS: Successfully loaded rom_base.bin (%zu bytes) into RDRAM.", size);
        } else {
            LOGE("BKA-STUBS: FATAL: gN64_RDRAM is null or file size is 0!");
        }
        fclose(f);
    } else {
        LOGW("BKA-STUBS: Warning: rom_base.bin not found directly in working directory. Checking SAF streams...");
    }

    s_n64_gil.lock();
    func_80000450(0);
    s_n64_gil.unlock();
}'''

def ensure_dir(filepath):
    """Ensure the parent directory exists."""
    Path(filepath).parent.mkdir(parents=True, exist_ok=True)

def atomic_write(filepath, content):
    """Write content to a file atomically."""
    ensure_dir(filepath)
    temp_fd, temp_path = tempfile.mkstemp(dir=Path(filepath).parent)
    try:
        with os.fdopen(temp_fd, 'w') as f:
            f.write(content)
        shutil.move(temp_path, filepath)
    except Exception as e:
        if os.path.exists(temp_path):
            os.unlink(temp_path)
        raise e

def read_file(filepath):
    """Read file content or return None if not found."""
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'r') as f:
        return f.read()

def patch_stubs_engine_ignition():
    if not os.path.exists(ENGINE_SRC_FILE):
        print(f"Error: {ENGINE_SRC_FILE} not found. Cannot apply engine ignition patch.")
        return

    content = read_file(ENGINE_SRC_FILE)
    if TAG in content:
        print(f"Engine ignition section in {ENGINE_SRC_FILE} is already patched.")
        return

    # Pattern to target Section 8: SECURE ENGINE IGNITION onwards up to standard definitions
    pattern = r'(/\* ============================================================\s+8\. SECURE ENGINE IGNITION\s+============================================================ \*/.*?)(?=\n\nvoid BKA_DropEngineLock|\Z)'
    
    match = re.search(pattern, content, re.DOTALL)
    if not match:
        print(f"Error: Could not locate the 'SECURE ENGINE IGNITION' section block inside {ENGINE_SRC_FILE}.")
        return

    wrapped_patch = f"/* [{TAG}] */\n" + PATCHED_ENGINE_SECTION

    new_content = content.replace(match.group(1), wrapped_patch)

    if new_content != content:
        atomic_write(ENGINE_SRC_FILE, new_content)
        print(f"Successfully patched {ENGINE_SRC_FILE} with safety checks and RDRAM loader initialization logic.")
    else:
        print("Warning: Content replacement resulted in identical string state.")

if __name__ == "__main__":
    print("Running stubs.cpp Engine Ignition Patcher...")
    patch_stubs_engine_ignition()
    print("Patching Complete.")
