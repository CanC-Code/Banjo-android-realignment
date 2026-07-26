// File: Android/app/src/main/cpp/ultra/otr_builder.cpp

#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>

#include "rare_decompression.h"

// ---------------------------------------------------------------------------
// Explicit Forward Declarations
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif
uint8_t* decompress_rare_asset(uint8_t* srcBuffer, uint32_t srcSize, uint32_t* bytesWritten);
#ifdef __cplusplus
}
#endif

#define LOG_TAG "BKA_OTR"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Manifest format
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct ManifestEntry {
    uint32_t offset;
    uint32_t size;
    char name[32];
    char type[8];
};

#pragma pack(pop)

static_assert(sizeof(ManifestEntry) == 48, "ManifestEntry layout mismatch");

// ---------------------------------------------------------------------------
// Safety constants
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_MANIFEST_ENTRIES = 100000;
static constexpr uint32_t MAX_ASSET_SIZE = 0x10000000; // 256 MB

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline uint32_t swap_uint32(uint32_t val) {
    return ((val & 0xFF000000u) >> 24) |
           ((val & 0x00FF0000u) >> 8) |
           ((val & 0x0000FF00u) << 8) |
           ((val & 0x000000FFu) << 24);
}

static bool debug_ui(
    JNIEnv* env,
    jobject callbackObj,
    jmethodID progressMid,
    int percent,
    const char* message) {
    if (!env || !callbackObj || !progressMid) {
        return true;
    }

    jstring jMsg = env->NewStringUTF(message ? message : "");
    if (!jMsg) {
        LOGE("Failed creating JNI status string");
        return false;
    }

    env->CallVoidMethod(callbackObj, progressMid, static_cast<jint>(percent), jMsg);
    env->DeleteLocalRef(jMsg);

    if (env->ExceptionCheck()) {
        LOGE("Java progress callback threw exception");
        env->ExceptionClear();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// ROM byte order normalization
// ---------------------------------------------------------------------------

static void byteswap_v64(uint8_t* data, size_t size) {
    if (!data) return;
    uint16_t* d16 = reinterpret_cast<uint16_t*>(data);
    size_t count = size / 2;
    for (size_t i = 0; i < count; ++i) {
        uint16_t val = d16[i];
        d16[i] = static_cast<uint16_t>((val >> 8) | (val << 8));
    }
}

static void byteswap_n64(uint8_t* data, size_t size) {
    if (!data) return;
    uint32_t* d32 = reinterpret_cast<uint32_t*>(data);
    size_t count = size / 4;
    for (size_t i = 0; i < count; ++i) {
        uint32_t val = d32[i];
        d32[i] = swap_uint32(val);
    }
}

// ---------------------------------------------------------------------------
// ROM writer
// ---------------------------------------------------------------------------

static bool write_rom_base_from_memory(
    const uint8_t* romData,
    size_t romSize,
    const char* outDir) {
    if (!romData || romSize == 0 || !outDir) {
        LOGE("write_rom_base invalid arguments");
        return false;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/rom_base.bin", outDir);

    FILE* file = fopen(path, "wb");
    if (!file) {
        LOGE("Unable to create rom_base.bin errno=%d", errno);
        return false;
    }

    size_t written = fwrite(romData, 1, romSize, file);
    fflush(file);
    fsync(fileno(file));
    fclose(file);

    if (written != romSize) {
        LOGE("ROM write incomplete %zu/%zu", written, romSize);
        return false;
    }

    LOGI("write_rom_base: wrote %zu bytes -> %s", written, path);
    return true;
}

// ---------------------------------------------------------------------------
// JNI entry point
// ---------------------------------------------------------------------------

extern "C"
JNIEXPORT void JNICALL
Java_com_bkawrapper_OtrService_runNativeOtrGeneration(
    JNIEnv* env,
    jobject thiz,
    jobject callback,
    jint romFd,
    jstring outDir,
    jstring manifestPath) {

    (void)thiz;

    off_t romSizeOff = 0;
    size_t romSize = 0;
    uint8_t* romData = nullptr;
    uint8_t* romBaseBuffer = nullptr;
    size_t totalRead = 0;
    FILE* mFile = nullptr;
    uint32_t entryCount = 0;
    uint32_t extracted = 0;
    uint32_t compressed = 0;
    uint32_t failed = 0;
    int lastPercent = -1;
    bool manifestNeedsSwap = false;
    const char* cOutDir = nullptr;
    const char* cManifestPath = nullptr;
    jclass callbackClass = nullptr;
    jmethodID progressMid = nullptr;

    if (!env || romFd < 0 || !outDir || !manifestPath) {
        LOGE("Invalid JNI arguments");
        return;
    }

    cOutDir = env->GetStringUTFChars(outDir, nullptr);
    cManifestPath = env->GetStringUTFChars(manifestPath, nullptr);

    if (!cOutDir || !cManifestPath) {
        LOGE("Failed obtaining JNI strings");
        goto cleanup;
    }

    if (callback) {
        callbackClass = env->GetObjectClass(callback);
        if (callbackClass) {
            progressMid = env->GetMethodID(callbackClass, "onProgressUpdate", "(ILjava/lang/String;)V");
        }
        if (!progressMid) {
            LOGW("Progress callback unavailable");
        }
    }

    // -----------------------------------------------------------------------
    // STEP 1: Load ROM
    // -----------------------------------------------------------------------
    debug_ui(env, callback, progressMid, 0, "Loading ROM into memory...");

    if (lseek(romFd, 0, SEEK_SET) < 0) {
        LOGE("Unable to seek ROM fd errno=%d", errno);
        goto cleanup;
    }

    romSizeOff = lseek(romFd, 0, SEEK_END);
    if (romSizeOff <= 0) {
        LOGE("Unable determining ROM size errno=%d", errno);
        goto cleanup;
    }

    romSize = static_cast<size_t>(romSizeOff);
    if (romSize > MAX_ASSET_SIZE) {
        LOGE("ROM exceeds safety limit: %zu bytes", romSize);
        goto cleanup;
    }

    if (lseek(romFd, 0, SEEK_SET) < 0) {
        LOGE("Unable resetting ROM position");
        goto cleanup;
    }

    romData = static_cast<uint8_t*>(malloc(romSize));
    if (!romData) {
        LOGE("ROM allocation failed size=%zu", romSize);
        goto cleanup;
    }

    totalRead = 0;
    while (totalRead < romSize) {
        ssize_t count = read(romFd, romData + totalRead, romSize - totalRead);
        if (count < 0) {
            if (errno == EINTR) continue;
            LOGE("ROM read failed errno=%d", errno);
            goto cleanup;
        }
        if (count == 0) break;
        totalRead += static_cast<size_t>(count);
    }

    if (totalRead != romSize) {
        LOGE("Incomplete ROM read %zu/%zu", totalRead, romSize);
        goto cleanup;
    }

    LOGI("Loaded ROM size=%zu", romSize);

    // Normalize ROM byte order
    if (romSize >= 4) {
        if (romData[0] == 0x37 && romData[1] == 0x80 && romData[2] == 0x40 && romData[3] == 0x12) {
            LOGI("Detected v64 ROM. Swapping bytes.");
            byteswap_v64(romData, romSize);
        } else if (romData[0] == 0x40 && romData[1] == 0x12 && romData[2] == 0x37 && romData[3] == 0x80) {
            LOGI("Detected n64 ROM. Swapping bytes.");
            byteswap_n64(romData, romSize);
        } else if (romData[0] == 0x80 && romData[1] == 0x37 && romData[2] == 0x12 && romData[3] == 0x40) {
            LOGI("Detected z64 ROM. No swap needed.");
        }
    }

    // -----------------------------------------------------------------------
    // STEP 2: Allocate output buffer for rom_base.bin
    // -----------------------------------------------------------------------
    debug_ui(env, callback, progressMid, 10, "Allocating output buffer...");

    romBaseBuffer = static_cast<uint8_t*>(calloc(romSize, 1));
    if (!romBaseBuffer) {
        LOGE("Failed to allocate rom_base buffer (size=%zu)", romSize);
        goto cleanup;
    }

    if (romSize >= 4) {
        memcpy(romBaseBuffer, romData, 4);
    }

    // -----------------------------------------------------------------------
    // STEP 3: Open manifest
    // -----------------------------------------------------------------------
    mFile = fopen(cManifestPath, "rb");
    if (!mFile) {
        LOGW("Manifest missing: %s", cManifestPath);
        debug_ui(env, callback, progressMid, 100, "Extraction complete (ROM-only mode)");
        goto cleanup;
    }

    if (fread(&entryCount, sizeof(uint32_t), 1, mFile) != 1) {
        LOGE("Unable reading manifest header");
        goto cleanup;
    }

    // Robust Endianness detection
    {
        uint32_t rawCount = entryCount;
        uint32_t swappedCount = swap_uint32(rawCount);
        if (swappedCount > 0 && swappedCount <= MAX_MANIFEST_ENTRIES && rawCount > MAX_MANIFEST_ENTRIES) {
            entryCount = swappedCount;
            manifestNeedsSwap = true;
        } else if (rawCount > MAX_MANIFEST_ENTRIES && swappedCount <= MAX_MANIFEST_ENTRIES) {
            entryCount = swappedCount;
            manifestNeedsSwap = true;
        } else {
            entryCount = rawCount;
            manifestNeedsSwap = false;
        }
    }

    if (entryCount == 0 || entryCount > MAX_MANIFEST_ENTRIES) {
        LOGE("Invalid manifest entry count: %u (raw read)", entryCount);
        goto cleanup;
    }

    LOGI("Processing %u manifest entries (Needs Swap: %s)", entryCount, manifestNeedsSwap ? "Yes" : "No");

    // -----------------------------------------------------------------------
    // STEP 4: Extract assets in-place with robust offset verification
    // -----------------------------------------------------------------------
    extracted = 0;
    compressed = 0;
    failed = 0;
    lastPercent = -1;

    for (uint32_t i = 0; i < entryCount; i++) {
        ManifestEntry entry;
        if (fread(&entry, sizeof(ManifestEntry), 1, mFile) != 1) {
            LOGE("Manifest read failed at entry %u", i);
            failed++;
            break;
        }

        if (manifestNeedsSwap) {
            entry.offset = swap_uint32(entry.offset);
            entry.size = swap_uint32(entry.size);
        }

        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.type[sizeof(entry.type) - 1] = '\0';

        // Fix: Handle unaligned or redirected structural table offsets safely
        if (entry.offset >= romSize) {
            // Attempt fallback normalization if offset references absolute address space vs relative
            if (entry.offset >= 0x10000000 && (entry.offset - 0x10000000) < romSize) {
                entry.offset -= 0x10000000;
            } else {
                LOGW("Skipping invalid offset asset %.32s offset=%u", entry.name, entry.offset);
                failed++;
                continue;
            }
        }

        if (entry.size == 0) {
            failed++;
            continue;
        }

        uint64_t endOffset = static_cast<uint64_t>(entry.offset) + entry.size;
        if (endOffset > romSize) {
            LOGW("Clamping oversized asset %.32s", entry.name);
            entry.size = static_cast<uint32_t>(romSize - entry.offset);
        }

        uint8_t* srcBuffer = romData + entry.offset;
        uint8_t* destBuffer = romBaseBuffer + entry.offset;

        bool isRareCompressed = false;
        if (entry.size >= 8 && srcBuffer[0] == 0x11 && srcBuffer[1] == 0x72) {
            uint32_t declaredSize = (srcBuffer[2] << 24) | (srcBuffer[3] << 16) |
                                   (srcBuffer[4] << 8) | srcBuffer[5];
            if (declaredSize > 0 && declaredSize <= MAX_ASSET_SIZE) {
                isRareCompressed = true;
            }
        }

        if (isRareCompressed) {
            uint32_t written = 0;
            uint8_t* decompressedData = decompress_rare_asset(srcBuffer, entry.size, &written);
            if (decompressedData && written > 0) {
                if (written <= entry.size || (entry.offset + written <= romSize)) {
                    memcpy(destBuffer, decompressedData, written);
                    extracted++;
                    compressed++;
                } else {
                    LOGE("Decompressed size exceeds buffer for %.32s", entry.name);
                    failed++;
                }
                free(decompressedData);
            } else {
                LOGE("Decompression failed for %.32s", entry.name);
                failed++;
            }
        } else {
            memcpy(destBuffer, srcBuffer, entry.size);
            extracted++;
        }

        int percent = 10 + static_cast<int>(((uint64_t)i * 89) / entryCount);
        if (percent != lastPercent) {
            char status[128];
            snprintf(status, sizeof(status), "Extracting: %.32s", entry.name);
            if (!debug_ui(env, callback, progressMid, percent, status)) {
                break;
            }
            lastPercent = percent;
        }
    }

    // -----------------------------------------------------------------------
    // STEP 5: Write rom_base.bin to disk
    // -----------------------------------------------------------------------
    debug_ui(env, callback, progressMid, 90, "Writing rom_base.bin...");

    if (!write_rom_base_from_memory(romBaseBuffer, romSize, cOutDir)) {
        LOGE("Failed writing rom_base.bin");
        goto cleanup;
    }

    LOGI("Extraction complete: extracted=%u compressed=%u failed=%u total=%u",
         extracted, compressed, failed, entryCount);

    char summary[256];
    snprintf(summary, sizeof(summary),
             "Extraction complete! %u assets extracted, %u failed", extracted, failed);
    debug_ui(env, callback, progressMid, 100, summary);

cleanup:
    if (mFile) fclose(mFile);
    if (romBaseBuffer) free(romBaseBuffer);
    if (romData) free(romData);
    if (cOutDir) env->ReleaseStringUTFChars(outDir, cOutDir);
    if (cManifestPath) env->ReleaseStringUTFChars(manifestPath, cManifestPath);
    if (callbackClass) env->DeleteLocalRef(callbackClass);
}
