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

/**
 * Write the raw ROM file descriptor contents to <outDir>/rom_base.bin.
 *
 * ResourceMgr_Init() maps this file into gN64_ROM_Base at boot. Without it,
 * every HandleDma() call that misses the per-asset cache returns zeroed memory,
 * which corrupts all code segments, audio tables, and uncompressed data —
 * producing the white-screen hang.
 *
 * We write it as the FIRST step so that even if per-asset extraction fails
 * partway through, the fallback DMA path still works correctly.
 *
 * Returns the ROM size in bytes, or 0 on failure.
 */
static size_t write_rom_base(int romFd, const char* outDir) {
    // Determine ROM size via lseek
    off_t romSize = lseek(romFd, 0, SEEK_END);
    if (romSize <= 0) {
        LOGE("write_rom_base: lseek SEEK_END failed (errno=%d)", errno);
        return 0;
    }
    lseek(romFd, 0, SEEK_SET);

    char romBasePath[512];
    snprintf(romBasePath, sizeof(romBasePath), "%s/rom_base.bin", outDir);

    FILE* out = fopen(romBasePath, "wb");
    if (!out) {
        LOGE("write_rom_base: fopen(%s) failed (errno=%d)", romBasePath, errno);
        return 0;
    }

    // Stream copy in 1 MiB chunks to avoid a single large malloc
    const size_t CHUNK = 1024 * 1024;
    uint8_t* buf = (uint8_t*)malloc(CHUNK);
    if (!buf) {
        LOGE("write_rom_base: malloc failed for copy buffer");
        fclose(out);
        return 0;
    }

    size_t total   = 0;
    ssize_t nread;
    while ((nread = read(romFd, buf, CHUNK)) > 0) {
        fwrite(buf, 1, (size_t)nread, out);
        total += (size_t)nread;
    }

    free(buf);
    fclose(out);

    if ((off_t)total != romSize) {
        LOGE("write_rom_base: wrote %zu bytes but ROM is %lld bytes",
             total, (long long)romSize);
        return 0;
    }

    LOGI("write_rom_base: wrote %zu bytes → %s", total, romBasePath);
    return total;
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
    // STEP 1: Write rom_base.bin BEFORE anything else.
    //
    // ResourceMgr_Init() needs this file to exist so it can map the full
    // ROM into gN64_ROM_Base. This is the DMA fallback for every asset
    // that is NOT individually extracted below (uncompressed code, audio,
    // headers, etc.).  Writing it first means a partial extraction still
    // results in a bootable engine.
    // ------------------------------------------------------------------
    debug_ui(env, callback, progressMid, 0, "Copying ROM base...");

    size_t romSize = write_rom_base((int)romFd, cOutDir);
    if (romSize == 0) {
        // Rom base copy failed — extraction cannot proceed safely.
        debug_ui(env, callback, progressMid, 0, "ERROR: Failed to write rom_base.bin");
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    // Rewind the fd for per-asset pread() calls below
    lseek((int)romFd, 0, SEEK_SET);

    // ------------------------------------------------------------------
    // STEP 2: Open the manifest
    // ------------------------------------------------------------------
    FILE* mFile = fopen(cManifestPath, "rb");
    if (!mFile) {
        // Manifest missing is non-fatal: rom_base.bin is already written,
        // so the DMA fallback path will handle all reads. Log and exit cleanly.
        LOGE("Manifest not found at %s — skipping per-asset extraction", cManifestPath);
        debug_ui(env, callback, progressMid, 100, "Extraction Complete (ROM-only mode)");
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    uint32_t entryCount = 0;
    if (fread(&entryCount, sizeof(uint32_t), 1, mFile) != 1 || entryCount == 0) {
        LOGE("Manifest header read failed or empty");
        fclose(mFile);
        debug_ui(env, callback, progressMid, 100, "Extraction Complete (ROM-only mode)");
        env->ReleaseStringUTFChars(outDir,       cOutDir);
        env->ReleaseStringUTFChars(manifestPath, cManifestPath);
        return;
    }

    LOGI("Processing %u manifest entries", entryCount);

    // ------------------------------------------------------------------
    // STEP 3: Per-asset extraction
    //
    // For Rare-compressed assets: decompress and write asset_XXXXXXXX.bin.
    // For uncompressed assets:    write raw bytes as asset_XXXXXXXX.bin.
    //
    // Both cases write a file so HandleDma's direct-file-lookup path hits
    // before falling back to the full ROM buffer — this avoids a memcpy of
    // the entire ROM on every DMA call for hot assets.
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

        uint8_t* buffer = (uint8_t*)malloc(readSize);
        if (!buffer) {
            LOGE("malloc failed for entry %u ('%s') size=%u", i, entry.name, readSize);
            failed++;
            continue;
        }

        ssize_t got = pread((int)romFd, buffer, readSize, (off_t)entry.offset);
        if (got <= 0) {
            LOGE("pread failed for entry %u ('%s') offset=0x%08X", i, entry.name, entry.offset);
            free(buffer);
            failed++;
            continue;
        }

        char outPath[512];
        snprintf(outPath, sizeof(outPath), "%s/asset_%08X.bin", cOutDir, entry.offset);

        // Check for Rare compression magic: 0x1172
        if ((size_t)got >= 2 && buffer[0] == 0x11 && buffer[1] == 0x72) {
            uint32_t outSize = 0;
            uint8_t* outBuf  = decompress_rare_asset(buffer, (uint32_t)got, &outSize);
            free(buffer);

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
            // Uncompressed — write raw bytes directly.
            // Previously these were silently discarded, meaning all code
            // segments, audio tables, and bin assets were never extracted.
            FILE* out = fopen(outPath, "wb");
            if (out) {
                fwrite(buffer, 1, (size_t)got, out);
                fclose(out);
                extracted++;
            } else {
                LOGE("fopen failed for raw output: %s", outPath);
                failed++;
            }
            free(buffer);
        }

        // Progress update every 10 entries to avoid flooding the UI thread
        if (i % 10 == 0) {
            char status[64];
            snprintf(status, sizeof(status), "Extracting: %.28s", entry.name);
            debug_ui(env, callback, progressMid, (int)((i * 99) / entryCount), status);
        }
    }

    fclose(mFile);

    LOGI("Extraction complete: %u extracted (%u compressed), %u failed of %u total",
         extracted, compressed, failed, entryCount);

    char summary[128];
    snprintf(summary, sizeof(summary),
             "Extraction Complete! (%u assets, %u failed)", extracted, failed);
    debug_ui(env, callback, progressMid, 100, summary);

    env->ReleaseStringUTFChars(outDir,       cOutDir);
    env->ReleaseStringUTFChars(manifestPath, cManifestPath);
}
