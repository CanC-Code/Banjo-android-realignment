#include <PR/sched.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <android/log.h>
#include <string>

#include "bka_safe_base.h"
#include "rare_decompression.h"

#define LOG_TAG "NativeBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::string g_assetDir;

// Binary manifest record structure matching generator layout (48 bytes)
struct ManifestRecord {
    uint32_t offset;
    uint32_t size;
    char name[32];
    char type[8];
};

// Global manifest registry cache
static ManifestRecord* g_manifestRecords = nullptr;
static uint32_t g_manifestCount = 0;

// Define global ROM base pointer instance referenced across modules outside anonymous linkage blocks
uint8_t* gN64_ROM_Base = nullptr;
static size_t g_romSize = 0; 

extern "C" void BKA_SignalResourcesReady(void);

// External implementation of BKA_InflateCodeSegment to satisfy linker requirements
extern "C" void BKA_InflateCodeSegment(void* dramAddr, uint32_t romOffset, uint32_t size) {
    if (!gN64_ROM_Base || !dramAddr) {
        LOGE("BKA_InflateCodeSegment: Invalid base pointers for inflation.");
        return;
    }

    uint8_t* srcStream = gN64_ROM_Base + romOffset;

    // Construct inline metadata workspace expectation headers for rare decompression
    // Structural layout: [0..3] Compressed Size, [4..7] Expected Uncompressed Workspace Size
    uint8_t headerMeta[8];
    headerMeta[0] = (size >> 24) & 0xFF;
    headerMeta[1] = (size >> 16) & 0xFF;
    headerMeta[2] = (size >> 8) & 0xFF;
    headerMeta[3] = size & 0xFF;

    // Allocate 8MB default expansion workspace bound for code segments
    uint32_t expectedWorkspaceSize = 0x800000; 
    headerMeta[4] = (expectedWorkspaceSize >> 24) & 0xFF;
    headerMeta[5] = (expectedWorkspaceSize >> 16) & 0xFF;
    headerMeta[6] = (expectedWorkspaceSize >> 8) & 0xFF;
    headerMeta[7] = expectedWorkspaceSize & 0xFF;

    uint32_t decompressedBytes = decompress_rare_runtime_hle(
        srcStream, 
        static_cast<uint8_t*>(dramAddr), 
        headerMeta
    );

    if (decompressedBytes == 0) {
        LOGW("BKA_InflateCodeSegment: Decompression returned 0 bytes. Falling back to direct memory copy.");
        memcpy(dramAddr, srcStream, size);
    } else {
        LOGI("BKA_InflateCodeSegment: Successfully inflated %u bytes into DRAM destination %p.", decompressedBytes, dramAddr);
    }
}

extern "C" {

/**
 * Initializes the Resource Manager in Absolute Self-Building Mode and parses manifest_us.bin.
 */
void ResourceMgr_Init(const char* assetDir) {
    if (!assetDir) {
        LOGE("ResourceMgr: Received an uninitialized null pointer for assetDir configuration.");
        return;
    }

    g_assetDir = assetDir;
    if (!g_assetDir.empty() && g_assetDir.back() != '/') {
        g_assetDir += "/";
    }

    LOGI("ResourceMgr: Activated in Absolute Self-Building Mode at location %s", g_assetDir.c_str());

    // --- Load manifest_us.bin ---
    char manifestPath[512];
    snprintf(manifestPath, sizeof(manifestPath), "%smanifest_us.bin", g_assetDir.c_str());
    FILE* mf = fopen(manifestPath, "rb");
    if (mf) {
        if (fread(&g_manifestCount, sizeof(uint32_t), 1, mf) == 1) {
            g_manifestRecords = static_cast<ManifestRecord*>(malloc(g_manifestCount * sizeof(ManifestRecord)));
            if (g_manifestRecords) {
                size_t readCount = fread(g_manifestRecords, sizeof(ManifestRecord), g_manifestCount, mf);
                LOGI("ResourceMgr: Successfully loaded %zu / %u manifest records from %s", readCount, g_manifestCount, manifestPath);
            }
        }
        fclose(mf);
    } else {
        LOGW("ResourceMgr: Warning - Could not open manifest file at %s. Falling back to default routing.", manifestPath);
    }

    char romPath[512];
    snprintf(romPath, sizeof(romPath), "%srom_base.bin", g_assetDir.c_str());

    LOGI("ResourceMgr: Open file pointer tracking target: %s", romPath);
    FILE* f = fopen(romPath, "rb");

    if (!f) {
        LOGE("ResourceMgr: FATAL ERROR - System fallback dependency file missing. Path: %s", romPath);
        return;
    }

    fseek(f, 0, SEEK_END);
    g_romSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (g_romSize == 0 || g_romSize > 128 * 1024 * 1024) {
        LOGE("ResourceMgr: FATAL ERROR - Invalid rom_base.bin size detected: %zu bytes", g_romSize);
        fclose(f);
        g_romSize = 0;
        return;
    }

    if (gN64_ROM_Base == nullptr) {
        LOGI("ResourceMgr: Pointer is null. Allocating independent buffer block of %zu bytes.", g_romSize);
        gN64_ROM_Base = static_cast<uint8_t*>(malloc(g_romSize));
    } else {
        LOGI("ResourceMgr: Pointer pre-allocated by engine core at %p. Safely retaining structure layout.", gN64_ROM_Base);
    }

    if (!gN64_ROM_Base) {
        LOGE("ResourceMgr: FATAL ERROR - Memory pointer verification failed. Cannot parse ROM stream.");
        fclose(f);
        return;
    }

    LOGI("ResourceMgr: Streaming binary database targets into virtual memory locations...");
    size_t bytesRead = fread(gN64_ROM_Base, 1, g_romSize, f);
    LOGI("ResourceMgr: Verification validation sequence populated %zu bytes into ROM base block.", bytesRead);

    fclose(f);
}

/**
 * Handles N64 DMA requests by isolating segmented structures from native host allocations.
 */
void ResourceMgr_HandleDma(void* dramAddr, uint32_t devAddr, uint32_t size) {
    char path[512];
    bool fileFound = false;
    FILE* f = nullptr;

    // Isolate clean 28-bit relative offset mapping layers for tracking extraction files
    uint32_t relativeRomOffset = devAddr & 0x0FFFFFFF;

    // --- Check Manifest Records for Specialized Handlers ---
    if (g_manifestRecords) {
        for (uint32_t i = 0; i < g_manifestCount; ++i) {
            // Match record by ROM offset range or exact entry offset
            if (g_manifestRecords[i].offset == relativeRomOffset || g_manifestRecords[i].offset == devAddr) {
                // Check if type matches our specialized code identifier
                if (strncmp(g_manifestRecords[i].type, "code_bin", 8) == 0) {
                    LOGI("ResourceMgr: Intercepted specialized code_bin record '%s' at offset %08X. Routing to decompression flow.", 
                         g_manifestRecords[i].name, relativeRomOffset);

                    // Route away from standard memcpy/raw asset loading to custom decompression handler
                    if (gN64_ROM_Base != nullptr) {
                        BKA_InflateCodeSegment(dramAddr, relativeRomOffset, g_manifestRecords[i].size);
                    }
                    sched_yield();
                    return;
                }
            }
        }
    }

    snprintf(path, sizeof(path), "%sasset_%08X.bin", g_assetDir.c_str(), relativeRomOffset);
    f = fopen(path, "rb");

    if (!f) {
        snprintf(path, sizeof(path), "%sasset_%08X.bin", g_assetDir.c_str(), devAddr);
        f = fopen(path, "rb");
    }

    if (f) {
        size_t bytesRead = fread(dramAddr, 1, size, f);
        fclose(f);

        if (bytesRead < size) {
            memset(static_cast<uint8_t*>(dramAddr) + bytesRead, 0, size - bytesRead);
        }
        fileFound = true;
    }

    if (!fileFound) {
        // STRICT SPECIFICATION FILTER:
        // Genuine N64 ROM access maps under raw boundaries (< g_romSize) or targets the Cartridge segment (0x10000000)
        bool isGenuineRom = (devAddr < g_romSize) || ((devAddr >> 24) == 0x10 && (relativeRomOffset + size) <= g_romSize);

        if (isGenuineRom && gN64_ROM_Base != nullptr) {
            memcpy(dramAddr, gN64_ROM_Base + relativeRomOffset, size);
        } else {
            // TRUNCATED 64-BIT HOST POINTER HEALING
            // Safely anchor upper memory page bits using the destination block address context directly
            uintptr_t dramContext = reinterpret_cast<uintptr_t>(dramAddr);
            uint64_t upper32BitsSign = dramContext & 0xFFFFFFFF00000000ULL;
            uintptr_t reconstructedHostPointer = upper32BitsSign | devAddr;

            // Nested Pointer Descriptor Validation
            uintptr_t* potentialNestedPtr = reinterpret_cast<uintptr_t*>(reconstructedHostPointer);

            // Check for 8-byte pointer alignment before dereferencing to prevent platform exceptions
            if (potentialNestedPtr && ((reconstructedHostPointer & 0x7) == 0)) {
                uintptr_t nestedVal = *potentialNestedPtr;

                // Explicit 32-bit heap page validation to verify matching host address structures
                if ((nestedVal >> 32) == (reconstructedHostPointer >> 32)) {
                    LOGW("ResourceMgr: Unwrapping nested descriptor layer reference %p -> %p", 
                         (void*)reconstructedHostPointer, (void*)nestedVal);
                    reconstructedHostPointer = nestedVal;
                }
            }

            memcpy(dramAddr, reinterpret_cast<void*>(reconstructedHostPointer), size);
        }
    }

    sched_yield();
}

} // extern "C"
