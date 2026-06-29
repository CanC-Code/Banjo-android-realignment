#include <sched.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <android/log.h>
#include <string>

#include "bka_safe_base.h"

#define LOG_TAG "NativeBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::string g_assetDir;

extern "C" void BKA_SignalResourcesReady(void);

extern "C" {

uint8_t* gN64_ROM_Base = nullptr;
static size_t g_romSize = 0; 

/**
 * Initializes the Resource Manager in Absolute Self-Building Mode.
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

            LOGW("ResourceMgr: Intercepted truncated host pointer instruction. Reconstructing: devAddr=0x%08X -> %p", 
                 devAddr, (void*)reconstructedHostPointer);

            // Nested Pointer Descriptor handling
            uintptr_t* potentialNestedPtr = reinterpret_cast<uintptr_t*>(reconstructedHostPointer);
            if (potentialNestedPtr && ((*potentialNestedPtr >> 40) == (reconstructedHostPointer >> 40))) {
                LOGW("ResourceMgr: Unwrapping nested descriptor layer reference %p -> %p", 
                     (void*)reconstructedHostPointer, (void*)*potentialNestedPtr);
                reconstructedHostPointer = *potentialNestedPtr;
            }

            memcpy(dramAddr, reinterpret_cast<void*>(reconstructedHostPointer), size);
        }
    }

    sched_yield();
}

} // extern "C"
