#include <sched.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <errno.h>
#include <android/log.h>
#include <string>

#include "bka_safe_base.h"

#define LOG_TAG "ResourceMgr"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::string g_assetDir;

extern "C" void BKA_SignalResourcesReady(void);

extern "C" {

uint8_t* gN64_ROM_Base = nullptr;

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
    
    LOGI("ResourceMgr: Attempting raw binary stream read at: %s", romPath);
    FILE* f = fopen(romPath, "rb");

    if (f) {
        fseek(f, 0, SEEK_END);
        size_t romSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (gN64_ROM_Base) {
            free(gN64_ROM_Base);
            gN64_ROM_Base = nullptr;
        }

        LOGI("ResourceMgr: Allocating contiguous tracking buffer space of size: %zu bytes.", romSize);
        gN64_ROM_Base = static_cast<uint8_t*>(malloc(romSize));
        if (gN64_ROM_Base) {
            size_t bytesRead = fread(gN64_ROM_Base, 1, romSize, f);
            LOGI("ResourceMgr: Verification validation sequence populated %zu bytes into host virtual RAM addresses.", bytesRead);
        } else {
            LOGE("ResourceMgr: FATAL ERROR - Cartridge physical memory mirror buffer generation allocation failed.");
        }
        fclose(f);
    } else {
        LOGE("ResourceMgr: FATAL ERROR - System fallback dependency file missing. Path: %s", romPath);
    }
}

/**
 * Handles N64 DMA requests by intercepting decompressed targets or falling back to raw ROM.
 */
void ResourceMgr_HandleDma(void* dramAddr, uint32_t devAddr, uint32_t size) {
    uint32_t relativeRomOffset = devAddr & 0x0FFFFFFF;

    char path[512];
    bool fileFound = false;
    FILE* f = nullptr;

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
        if (gN64_ROM_Base != nullptr) {
            if (relativeRomOffset + size <= 0x04000000) {
                memcpy(dramAddr, gN64_ROM_Base + relativeRomOffset, size);
            } else {
                LOGE("DMA OUT OF BOUNDS: Attempted absolute offset address reading violations at: 0x%08X", relativeRomOffset);
                memset(dramAddr, 0, size);
            }
        } else {
            LOGE("DMA CRITICAL FAILURE: rom_base.bin storage reference target unmapped while fallback lookup execution requested.");
            memset(dramAddr, 0, size);
        }
    }

    sched_yield();
}

} // extern "C"
