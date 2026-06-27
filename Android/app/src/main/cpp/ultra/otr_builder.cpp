#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <cerrno>
#include "rare_decompression.h"

#define LOG_TAG "BKA_OTR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Must match generate_manifest.py RECORD_FORMAT exactly.
// Offset(u32) + Size(u32) + Name(32s) + Type(8s) = 48 bytes
#pragma pack(push, 1)
struct ManifestEntry {
    uint32_t offset;
    uint32_t size;
    char     name[32];
    char     type[8];
};
#pragma pack(pop)

static_assert(sizeof(ManifestEntry) == 48, "ManifestEntry layout mismatch");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void debug_ui(JNIEnv* env, jobject callbackObj, jmethodID progressMid,
                     int percent, const char* msg) {
    jstring jMsg = env->NewStringUTF(msg ? msg : "");
    env->CallVoidMethod(callbackObj, progressMid, (jint)percent, jMsg);
    env->DeleteLocalRef(jMsg);
}

// In-place byte swap for v64 (BADC -> ABCD)
static void byteswap_v64(uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i += 2) {
        uint8_t temp = data[i];
        data[i] = data[i+1];
        data[i+1] = temp;
    }
}

// In-place byte swap for n64/Little Endian (DCBA -> ABCD)
static void byteswap_n64(uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i += 4) {
        uint8_t temp0 = data[i];
        uint8_t temp1 = data[i+1];
        data[i] = data[i+3];
        data[i+1] = data[i+2];
        data[i+2] = temp1;
        data[i+3] = temp0;
    }
}

/**
 * Write the normalized raw ROM memory buffer to <outDir>/rom_base.bin.
 *
 * ResourceMgr_Init() maps this file into gN64_ROM_Base at boot. Without it,
 * every HandleDma() call that misses the per-asset cache returns zeroed memory,
 * which corrupts all code segments, audio tables, and uncompressed data —
 * producing the white-screen hang.
 *
 * Returns true on success, false on failure.
 */
static bool write_rom_base_from_memory(const uint8_t* romData, size_t romSize, const char* outDir) {
    char romBasePath[512];
    snprintf(romBasePath, sizeof(romBasePath), "%s/rom_base.bin", outDir);

    FILE* out = fopen(romBasePath, "wb");
    if (!out) {
        LOGE("write_rom_base: fopen(%s) failed (errno=%d)", romBasePath, errno);
        return false;
    }

    size_t written = fwrite(romData, 1, romSize, out);
    fclose(out);

    if (written != romSize) {
        LOGE("write_rom_base: wrote %zu bytes but ROM is %zu bytes", written, romSize);
        return false;
    }

    LOGI("write_rom_base: wrote %zu bytes → %s", written, romBasePath);
    return true;
}

// ---------------------------------------------------------------------------
// JNI entry point
// ---------------------------------------------------------------------------

extern "C"
JNIEXPORT void JNICALL
Java_com_bkawrapper_OtrService_runNativeOtrGeneration(JNIEnv* env, jobject thiz,
                                                      jobject callback, jint romFd,
                                                      jstring outDir, jstring manifestPath) {
    const char* cOutDir       = env->GetStringUTFChars(outDir,       nullptr);
    const char* cManifestPath = env->GetStringUTFChars(manifestPath, nullptr);

    jclass    callbackClass = env->GetObjectClass(callback);
    jmethodID progressMid   = env->GetMethodID(callbackClass, "onProgressUpdate",
                                               "(ILjava/lang/String;)V");

    // ------------------------------------------------------------------
    // STEP 1: Load ROM into memory and normalize endianness
    // ------------------------------------------------------------------
    debug_ui(env, callback, progressMid, 0, "Loading ROM into memory...");

    off_t romSizeOff = lseek((int)romFd, 0, SEEK_END);
    if (romSizeOff <= 0) {
        LOGE("Failed to determine ROM size (errno=%d)", errno);
        debug_ui(env, callback, progressMid, 0, "ERROR: Failed to determine ROM size");
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }
    size_t romSize = (size_t)romSizeOff;
    lseek((int)romFd, 0, SEEK_SET);

    uint8_t* romData = (uint8_t*)malloc(romSize);
    if (!romData) {
        LOGE("Failed to allocate %zu bytes for ROM", romSize);
        debug_ui(env, callback, progressMid, 0, "ERROR: Out of memory loading ROM");
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    // Read full ROM into buffer
    size_t totalRead = 0;
    while (totalRead < romSize) {
        ssize_t n = read((int)romFd, romData + totalRead, romSize - totalRead);
        if (n <= 0) break;
        totalRead += n;
    }

    if (totalRead != romSize) {
        LOGE("Failed to read full ROM. Read %zu / %zu bytes", totalRead, romSize);
        debug_ui(env, callback, progressMid, 0, "ERROR: Failed to read ROM file completely");
        free(romData);
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    // Detect and correct endianness
    if (romSize >= 4) {
        if (romData[0] == 0x37 && romData[1] == 0x80 && romData[2] == 0x40 && romData[3] == 0x12) {
            LOGI("Detected v64 (BADC) ROM. Byte-swapping to z64...");
            debug_ui(env, callback, progressMid, 5, "Normalizing v64 ROM format...");
            byteswap_v64(romData, romSize);
        } else if (romData[0] == 0x40 && romData[1] == 0x12 && romData[2] == 0x37 && romData[3] == 0x80) {
            LOGI("Detected n64 (DCBA) ROM. Byte-swapping to z64...");
            debug_ui(env, callback, progressMid, 5, "Normalizing n64 ROM format...");
            byteswap_n64(romData, romSize);
        } else if (romData[0] == 0x80 && romData[1] == 0x37 && romData[2] == 0x12 && romData[3] == 0x40) {
            LOGI("Detected z64 (ABCD) ROM. No swapping needed.");
        } else {
            LOGE("Unknown ROM magic %02X %02X %02X %02X. Proceeding without byte-swapping...",
                 romData[0], romData[1], romData[2], romData[3]);
        }
    }

    // ------------------------------------------------------------------
    // STEP 2: Write normalized rom_base.bin BEFORE anything else.
    // ------------------------------------------------------------------
    debug_ui(env, callback, progressMid, 10, "Writing normalized ROM base...");
    if (!write_rom_base_from_memory(romData, romSize, cOutDir)) {
        debug_ui(env, callback, progressMid, 0, "ERROR: Failed to write rom_base.bin");
        free(romData);
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    // ------------------------------------------------------------------
    // STEP 3: Open the manifest
    // ------------------------------------------------------------------
    FILE* mFile = fopen(cManifestPath, "rb");
    if (!mFile) {
        LOGE("Manifest not found at %s — skipping per-asset extraction", cManifestPath);
        debug_ui(env, callback, progressMid, 100, "Extraction Complete (ROM-only mode)");
        free(romData);
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    uint32_t entryCount = 0;
    if (fread(&entryCount, sizeof(uint32_t), 1, mFile) != 1 || entryCount == 0) {
        LOGE("Manifest header read failed or empty");
        fclose(mFile);
        debug_ui(env, callback, progressMid, 100, "Extraction Complete (ROM-only mode)");
        free(romData);
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    LOGI("Processing %u manifest entries", entryCount);

    // ------------------------------------------------------------------
    // STEP 4: Per-asset extraction
    //
    // Using the normalized in-memory ROM buffer to prevent thousands
    // of pread() calls and to ensure the extracted assets have correct
    // endianness.
    // ------------------------------------------------------------------
    uint32_t extracted   = 0;
    uint32_t compressed  = 0;
    uint32_t failed      = 0;

    for (uint32_t i = 0; i < entryCount; i++) {
        ManifestEntry entry;
        if (fread(&entry, sizeof(ManifestEntry), 1, mFile) != 1) {
            LOGE("Manifest read failed at entry %u", i);
            break;
        }

        // Guard: zero-size or obviously invalid entries
        if (entry.size == 0 || entry.offset >= (uint32_t)romSize) {
            continue;
        }
        // Clamp size to prevent reading past ROM end
        uint32_t readSize = entry.size;
        if ((uint64_t)entry.offset + readSize > (uint64_t)romSize) {
            readSize = (uint32_t)(romSize - entry.offset);
        }

        uint8_t* assetBuffer = romData + entry.offset;

        char outPath[512];
        snprintf(outPath, sizeof(outPath), "%s/asset_%08X.bin", cOutDir, entry.offset);

        // Check for Rare compression magic: 0x1172
        if (readSize >= 2 && assetBuffer[0] == 0x11 && assetBuffer[1] == 0x72) {
            uint32_t outSize = 0;
            uint8_t* outBuf  = decompress_rare_asset(assetBuffer, readSize, &outSize);

            if (outBuf && outSize > 0) {
                FILE* out = fopen(outPath, "wb");
                if (out) {
                    fwrite(outBuf, 1, outSize, out);
                    fclose(out);
                    compressed++;
                    extracted++;
                } else {
                    LOGE("fopen failed for compressed output: %s", outPath);
                    failed++;
                }
                free(outBuf);
            } else {
                LOGE("Decompression failed for '%s'", entry.name);
                failed++;
            }
        } else {
            // Uncompressed — write raw bytes directly from normalized buffer.
            FILE* out = fopen(outPath, "wb");
            if (out) {
                fwrite(assetBuffer, 1, readSize, out);
                fclose(out);
                extracted++;
            } else {
                LOGE("fopen failed for raw output: %s", outPath);
                failed++;
            }
        }

        // Progress update every 10 entries to avoid flooding the UI thread
        if (i % 10 == 0) {
            char status[64];
            snprintf(status, sizeof(status), "Extracting: %.28s", entry.name);
            // Map progress from 10% to 99%
            int progressPercent = 10 + (int)((i * 89) / entryCount);
            debug_ui(env, callback, progressMid, progressPercent, status);
        }
    }

    fclose(mFile);
    free(romData);

    LOGI("Extraction complete: %u extracted (%u compressed), %u failed of %u total",
         extracted, compressed, failed, entryCount);

    char summary[128];
    snprintf(summary, sizeof(summary),
             "Extraction Complete! (%u assets, %u failed)", extracted, failed);
    debug_ui(env, callback, progressMid, 100, summary);

    env->ReleaseStringUTFChars(outDir,       cOutDir);
    env->ReleaseStringUTFChars(manifestPath, cManifestPath);
}
