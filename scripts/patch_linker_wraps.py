import os
import subprocess
import re
import shutil
import tempfile
from pathlib import Path

# --- Configuration ---
BUILD_OBJ_DIR = "Android/app/.cxx"
SAFE_BASE_FILE = "Android/app/src/main/cpp/bka_safe_base.h"
CMAKE_FILE = "Android/app/src/main/cpp/CMakeLists.txt"
ENGINE_SRC_FILE = "Android/app/src/main/cpp/emulator/stubs.cpp"
NEW_SOURCE = "ultra/HardwareRegs.cpp"
RAREZIP_PATH = "src/done/rarezip.c"

RENAME_MAP = {
    "__osInitialize_common": "__original___osInitialize_common",
    "__osViInit": "__original___osViInit",
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

# --- Helper Functions ---
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

# --- Patching Functions ---
def patch_objects():
    if not os.path.exists(BUILD_OBJ_DIR):
        print(f"Warning: {BUILD_OBJ_DIR} not found. Skipping objcopy.")
        return
    try:
        subprocess.run(["llvm-objcopy", "--version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("Error: 'llvm-objcopy' not found. Skipping object patching.")
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
                    print(f"Patching symbols in: {path}")
                    try:
                        subprocess.run(["llvm-objcopy"] + args + [path], check=True)
                    except subprocess.CalledProcessError as e:
                        print(f"Error patching {path}: {e}")

def patch_header():
    if not os.path.exists(SAFE_BASE_FILE):
        print(f"Warning: {SAFE_BASE_FILE} not found. Skipping header patch.")
        return
    content = read_file(SAFE_BASE_FILE)
    if "__original___osInitialize_common" in content:
        print(f"Header already patched: {SAFE_BASE_FILE}")
        return
    new_content = content + "\n" + STUB_CODE
    atomic_write(SAFE_BASE_FILE, new_content)
    print(f"Header updated: {SAFE_BASE_FILE}")

def add_to_cmakelists():
    if not os.path.exists(CMAKE_FILE):
        print(f"Warning: {CMAKE_FILE} not found. Skipping CMakeLists update.")
        return
    content = read_file(CMAKE_FILE)
    if NEW_SOURCE in content:
        print(f"{NEW_SOURCE} already in CMakeLists.txt.")
        return
    pattern = r'(add_library\(bkawrapper\s+SHARED\s+)([^)]*)(\s*\))'
    replacement = rf'\1\2\n    {NEW_SOURCE}\3'
    new_content = re.sub(pattern, replacement, content)
    if new_content != content:
        atomic_write(CMAKE_FILE, new_content)
        print(f"Updated {CMAKE_FILE}.")
    else:
        print(f"Could not find add_library(bkawrapper SHARED ...) in {CMAKE_FILE}.")

def inject_include(file_path, include_line):
    if not os.path.exists(file_path):
        print(f"Warning: {file_path} not found. Skipping include injection.")
        return
    content = read_file(file_path)
    if include_line in content:
        print(f"Include already present in {file_path}.")
        return
    new_content = include_line + "\n" + content
    atomic_write(file_path, new_content)
    print(f"Injected {include_line} into {file_path}.")

def inject_init_call():
    if not os.path.exists(ENGINE_SRC_FILE):
        print(f"Warning: {ENGINE_SRC_FILE} not found. Skipping init injection.")
        return
    inject_include(ENGINE_SRC_FILE, '#include "HardwareRegs.h"')
    content = read_file(ENGINE_SRC_FILE)
    hook = "InitHardwareRegs();"
    if hook in content:
        print("InitHardwareRegs() already injected in Engine entry.")
        return
    pattern = r'(void\s+BKA_StartEngine\s*\([^)]*\)\s*\{)'
    replacement = rf'\1\n    {hook}'
    new_content = re.sub(pattern, replacement, content)
    if new_content != content:
        atomic_write(ENGINE_SRC_FILE, new_content)
        print(f"Injected InitHardwareRegs() into {ENGINE_SRC_FILE}.")
    else:
        print(f"Could not find BKA_StartEngine definition in {ENGINE_SRC_FILE}.")

def patch_rarezip():
    if not os.path.exists(RAREZIP_PATH):
        print(f"Warning: {RAREZIP_PATH} not found.")
        return

    content = read_file(RAREZIP_PATH)
    IDEMPOTENCY_TAG = "DYNAMIC FORMAT MULTIPLEXER (GZIP/1172/RARE-LZSS-v7)"
    if IDEMPOTENCY_TAG in content:
        print(f"{RAREZIP_PATH} already fully patched.")
        return

    # --- Helper Functions for rarezip ---
    helper_code = '''#include "rarezip.h"
#include <android/log.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

extern u8* gN64_RDRAM;
extern u8* inbuf;
extern u8* D_80007284;
extern struct huft* D_80007290;
extern u32 wp;
extern u32 inptr;
extern int bkboot_inflate(void);

/* ── Pointer classification and translation ───────────────────────────── */
static u8* bka_resolve_ptr(uintptr_t addr) {
    if (addr == 0) return NULL;
    u8* rdram = gN64_RDRAM;
    u8* rdram_end = rdram + (8u * 1024u * 1024u);

    /* Case A: N64 KSEG0 virtual address (0x80000000-0x807FFFFF) */
    if (addr >= 0x80000000u && addr < 0x80800000u) {
        return rdram + (addr & 0x00FFFFFFu);
    }
    /* Case B: Already inside the current RDRAM window */
    if ((u8*)addr >= rdram && (u8*)addr < rdram_end) {
        return (u8*)addr;
    }
    /* Case C: ROM base block (0x7100000000-0x7200000000) */
    if (addr >= 0x7100000000u && addr < 0x7200000000u) {
        return (u8*)addr;  // Treat as valid host pointer
    }
    /* Case D: Stale host pointer — recover N64 offset from low 24 bits */
    u8* recovered = rdram + (addr & 0x00FFFFFFu);
    __android_log_print(ANDROID_LOG_WARN, "BKA_DEBUG",
        "PTR_RECOVER: stale=0x%014llX rdram_base=0x%014llX recovered=%p (offset=0x%06X)",
        (unsigned long long)addr, (unsigned long long)(uintptr_t)rdram, recovered, (unsigned)(addr & 0x00FFFFFFu));
    return recovered;
}

/* ── Rare LZSS decompressor ───────────────────────────────────────────── */
static uint32_t bka_rare_lzss_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
    uint8_t ring[0x1000];
    memset(ring, 0x00, sizeof(ring));
    uint32_t ring_pos = 0xFEEu;
    const uint8_t* src_end = src + src_len;
    uint8_t* dst_ptr = dst;
    const uint8_t* dst_end = dst + dst_cap;

    while (src < src_end && dst_ptr < dst_end) {
        uint8_t flags = *src++;
        for (int bit = 0; bit < 8 && src < src_end && dst_ptr < dst_end; bit++) {
            if (flags & (1u << bit)) {
                uint8_t lit = *src++;
                *dst_ptr++ = lit;
                ring[ring_pos] = lit;
                ring_pos = (ring_pos + 1u) & 0xFFFu;
            } else {
                if (src + 1 >= src_end) break;
                uint8_t b0 = *src++;
                uint8_t b1 = *src++;
                uint32_t ring_off = (uint32_t)b0 | (((uint32_t)(b1 & 0xF0u)) << 4u);
                uint32_t length = (uint32_t)(b1 & 0x0Fu) + 3u;
                for (uint32_t i = 0; i < length && dst_ptr < dst_end; i++) {
                    uint8_t byte = ring[(ring_off + i) & 0xFFFu];
                    *dst_ptr++ = byte;
                    ring[ring_pos] = byte;
                    ring_pos = (ring_pos + 1u) & 0xFFFu;
                }
            }
        }
    }
    return (uint32_t)(dst_ptr - dst);
}
'''

    # --- Main Patch for func_800005C0 ---
    unsafe_func_pattern = r'(u32\s+func_800005C0\s*\([^)]*\)\s*\{)(.*?)(\})'
    injected_code = f'''\
\\1
    /* ═══ BKA HLE: rarezip inflate hook ══ {IDEMPOTENCY_TAG} ═══ */
    /* Resolve all three pointers */
    inbuf = bka_resolve_ptr((uintptr_t)in);
    D_80007284 = bka_resolve_ptr((uintptr_t)out);
    D_80007290 = (struct huft*)bka_resolve_ptr((uintptr_t)arg2);

    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
        "INFLATE: in_raw=0x%llX out_raw=0x%llX arg2_raw=0x%llX",
        (unsigned long long)(uintptr_t)in,
        (unsigned long long)(uintptr_t)out,
        (unsigned long long)(uintptr_t)arg2);
    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
        "INFLATE: in=%p out=%p arg2=%p rdram=%p",
        inbuf, D_80007284, D_80007290, gN64_RDRAM);

    /* Check for zeroed input buffer */
    if (inbuf[0] == 0x00 && inbuf[1] == 0x00 && inbuf[2] == 0x00 && inbuf[3] == 0x00) {{
        __android_log_print(ANDROID_LOG_ERROR, "BKA_DEBUG",
            "INFLATE: Zeroed input buffer detected (in=%p). Skipping decompression.", inbuf);
        wp = 0;
        inptr = 0;
        return wp;
    }}

    /* Log first 8 bytes of resolved stream */
    __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
        "INFLATE HDR: [%02X %02X %02X %02X %02X %02X %02X %02X]",
        inbuf[0], inbuf[1], inbuf[2], inbuf[3], inbuf[4], inbuf[5], inbuf[6], inbuf[7]);

    uint8_t magic0 = inbuf[0];
    uint8_t magic1 = inbuf[1];

    /* PATH A: Rare LZSS (0x50 0x10) - Read size from header (bytes 2-5) */
    if (magic0 == 0x50 && magic1 == 0x10) {{
        uint32_t dec_size = ((uint32_t)inbuf[2] << 24) | ((uint32_t)inbuf[3] << 16)
                         | ((uint32_t)inbuf[4] <<  8) |  (uint32_t)inbuf[5];
        inbuf += 6;  // Skip 6-byte header (magic + size)
        u8* rdram_end = gN64_RDRAM + (8u * 1024u * 1024u);
        size_t comp_avail = (size_t)(rdram_end - inbuf);
        size_t out_cap = (size_t)(rdram_end - D_80007284);
        if (dec_size > 0 && out_cap > dec_size) out_cap = dec_size;

        __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
            "INFLATE RARE-LZSS: dec_size=%u comp_avail=%zu out_cap=%zu",
            dec_size, comp_avail, out_cap);

        uint32_t written = 0;
        if (out_cap > 0 && comp_avail > 0) {{
            written = bka_rare_lzss_decompress(inbuf, comp_avail, D_80007284, out_cap);
        }}
        __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
            "INFLATE RARE-LZSS RESULT: wrote=%u expected=%u", written, dec_size);
        wp = written;
        inptr = 0;
        return wp;
    }}

    /* PATH B: Rare deflate (0x11 0x72 / 0x11 0x73) */
    if (magic0 == 0x11 && (magic1 == 0x72 || magic1 == 0x73)) {{
        uint32_t dec_size = ((uint32_t)inbuf[2] << 24) | ((uint32_t)inbuf[3] << 16)
                          | ((uint32_t)inbuf[4] <<  8) |  (uint32_t)inbuf[5];
        inbuf += 6;  // Skip 6-byte header
        u8* rdram_end = gN64_RDRAM + (8u * 1024u * 1024u);
        size_t comp_avail = (size_t)(rdram_end - inbuf);
        size_t out_cap = (size_t)(rdram_end - D_80007284);
        if (out_cap > dec_size) out_cap = dec_size;

        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        zs.next_in = (Bytef*)inbuf;
        zs.avail_in = (uInt)comp_avail;
        zs.next_out = (Bytef*)D_80007284;
        zs.avail_out = (uInt)out_cap;
        int zr = Z_DATA_ERROR;
        if (inflateInit2(&zs, -15) == Z_OK) {{
            zr = inflate(&zs, Z_FINISH);
            inflateEnd(&zs);
        }}
        __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
            "INFLATE 1172: decSize=%u in=%u out=%u status=%d",
            dec_size, (uint32_t)zs.total_in, (uint32_t)zs.total_out, zr);
        wp = (u32)zs.total_out;
        inptr = 0;
        return wp;
    }}

    /* PATH C: Standard GZIP (0x1F 0x8B) */
    if (magic0 == 0x1F && magic1 == 0x8B) {{
        inbuf += 2;  // Skip GZIP header (10 bytes total, but we handle it in inflate)
        u8* rdram_end = gN64_RDRAM + (8u * 1024u * 1024u);
        size_t avail_in = (size_t)(rdram_end - inbuf);
        size_t out_cap = (size_t)(rdram_end - D_80007284);

        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        zs.next_in = (Bytef*)inbuf;
        zs.avail_in = (uInt)avail_in;
        zs.next_out = (Bytef*)D_80007284;
        zs.avail_out = (uInt)out_cap;
        int zr = Z_DATA_ERROR;
        if (inflateInit2(&zs, 15 + 32) == Z_OK) {{
            zr = inflate(&zs, Z_FINISH);
            inflateEnd(&zs);
        }}
        __android_log_print(ANDROID_LOG_INFO, "BKA_DEBUG",
            "INFLATE GZIP: in=%u out=%u status=%d",
            (uint32_t)zs.total_in, (uint32_t)zs.total_out, zr);
        wp = (u32)zs.total_out;
        inptr = 0;
        return wp;
    }}

    /* PATH D: Fallback to original behavior (skip 6 bytes + call bkboot_inflate) */
    inbuf += 6;
    wp = 0;
    inptr = 0;
    bkboot_inflate();
\\3
'''

    # Apply helper code (insert after #include <ultra64.h>)
    new_content = content.replace('#include <ultra64.h>', '#include <ultra64.h>\n' + helper_code, 1)

    # Apply main patch
    new_content = re.sub(unsafe_func_pattern, injected_code, new_content, flags=re.DOTALL)

    if new_content != content:
        atomic_write(RAREZIP_PATH, new_content)
        print(f"Patched {RAREZIP_PATH}.")
    else:
        print(f"WARNING: Could not find func_800005C0 pattern in {RAREZIP_PATH}.")

# --- Main ---
if __name__ == "__main__":
    print("Running BKA Wrapper Pre-Build Patches...")
    patch_objects()
    patch_header()
    add_to_cmakelists()
    inject_init_call()
    patch_rarezip()
    print("Pre-Build Patches Complete.")
