// File: Android/app/src/main/cpp/otr_builder.cpp

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


#define LOG_TAG "BKA_OTR"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LOGW(...) \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)


// ---------------------------------------------------------------------------
// Manifest format
//
// Must match generate_manifest.py:
//
// Offset(u32)
// Size(u32)
// Name(32 bytes)
// Type(8 bytes)
//
// Total = 48 bytes
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct ManifestEntry
{
    uint32_t offset;
    uint32_t size;

    char name[32];
    char type[8];
};

#pragma pack(pop)


static_assert(
        sizeof(ManifestEntry) == 48,
        "ManifestEntry layout mismatch");


// ---------------------------------------------------------------------------
// Safety constants
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_MANIFEST_ENTRIES = 100000;

static constexpr uint32_t MAX_ASSET_SIZE =
        0x10000000; // 256 MB


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline uint32_t swap_uint32(uint32_t val) {
    return ((val & 0xFF000000u) >> 24) |
           ((val & 0x00FF0000u) >>  8) |
           ((val & 0x0000FF00u) <<  8) |
           ((val & 0x000000FFu) << 24);
}

static bool debug_ui(
        JNIEnv* env,
        jobject callbackObj,
        jmethodID progressMid,
        int percent,
        const char* message)
{
    if (!env ||
        !callbackObj ||
        !progressMid)
    {
        return true;
    }


    jstring jMsg =
            env->NewStringUTF(
                    message ? message : "");


    if (!jMsg)
    {
        LOGE(
            "Failed creating JNI status string");

        return false;
    }


    env->CallVoidMethod(
            callbackObj,
            progressMid,
            static_cast<jint>(percent),
            jMsg);


    env->DeleteLocalRef(jMsg);


    if (env->ExceptionCheck())
    {
        LOGE(
            "Java progress callback threw exception");

        env->ExceptionClear();

        return false;
    }


    return true;
}


// ---------------------------------------------------------------------------
// ROM byte order normalization
// ---------------------------------------------------------------------------

// v64 format:
// Byte pair swap (BADC -> ABCD)
static void byteswap_v64(uint8_t* data, size_t size) {
    if (!data) return;
    uint16_t* d16 = reinterpret_cast<uint16_t*>(data);
    size_t count = size / 2;
    for (size_t i = 0; i < count; ++i) {
        uint16_t val = d16[i];
        d16[i] = static_cast<uint16_t>((val >> 8) | (val << 8));
    }
}

// n64 format (Little Endian):
// 32-bit word swap (DCBA -> ABCD)
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
        const char* outDir)
{
    if (!romData ||
        romSize == 0 ||
        !outDir)
    {
        LOGE(
            "write_rom_base invalid arguments");

        return false;
    }


    char path[512];


    snprintf(
            path,
            sizeof(path),
            "%s/rom_base.bin",
            outDir);



    FILE* file =
            fopen(path, "wb");


    if (!file)
    {
        LOGE(
            "Unable to create rom_base.bin errno=%d",
            errno);

        return false;
    }



    size_t written =
            fwrite(
                    romData,
                    1,
                    romSize,
                    file);

    
    // Force disk synchronization so the game thread doesn't miss the file
    fflush(file);
    fsync(fileno(file));
    fclose(file);



    if (written != romSize)
    {
        LOGE(
            "ROM write incomplete %zu/%zu",
            written,
            romSize);

        return false;
    }



    LOGI(
        "write_rom_base: wrote %zu bytes -> %s",
        written,
        path);


    return true;
}


// ---------------------------------------------------------------------------
// Memory Mapping Verification Structures & Utilities
// ---------------------------------------------------------------------------

struct MemoryMapEntry {
    uint32_t romOffset;
    uint32_t compressedSize;
    uint32_t decompressedSize;
    uint16_t flags;
};

static bool parse_memory_map(
        const uint8_t* romData,
        size_t romSize,
        uint32_t mapOffset,
        MemoryMapEntry* entries,
        size_t maxEntries,
        size_t* outCount)
{
    if (!romData || !entries || !outCount || mapOffset + 8 > romSize) {
        return false;
    }

    size_t count = 0;
    uint32_t currOffset = mapOffset;

    while (currOffset + sizeof(MemoryMapEntry) <= romSize && count < maxEntries) {
        const MemoryMapEntry* src = reinterpret_cast<const MemoryMapEntry*>(romData + currOffset);

        // Check for table end marker
        if (src->romOffset == 0xFFFFFFFF || src->romOffset == 0) {
            break;
        }

        entries[count].romOffset = swap_uint32(src->romOffset);
        entries[count].compressedSize = swap_uint32(src->compressedSize);
        entries[count].decompressedSize = swap_uint32(src->decompressedSize);

        currOffset += sizeof(MemoryMapEntry);
        count++;
    }

    *outCount = count;
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
        jstring manifestPath)
{
    (void)thiz;

    // Variable Hoisting block
    off_t romSizeOff = 0;
    size_t romSize = 0;
    uint8_t* romData = nullptr;
    size_t totalRead = 0;
    FILE* mFile = nullptr;
    uint32_t entryCount = 0;
    uint32_t extracted = 0;
    uint32_t compressed = 0;
    uint32_t failed = 0;
    int lastPercent = -1;
    bool manifestNeedsSwap = false;


    if (!env ||
        romFd < 0 ||
        !outDir ||
        !manifestPath)
    {
        LOGE(
            "Invalid JNI arguments");

        return;
    }



    const char* cOutDir =
            env->GetStringUTFChars(
                    outDir,
                    nullptr);


    const char* cManifestPath =
            env->GetStringUTFChars(
                    manifestPath,
                    nullptr);



    if (!cOutDir ||
        !cManifestPath)
    {
        LOGE(
            "Failed obtaining JNI strings");


        if (cOutDir)
        {
            env->ReleaseStringUTFChars(
                    outDir,
                    cOutDir);
        }


        if (cManifestPath)
        {
            env->ReleaseStringUTFChars(
                    manifestPath,
                    cManifestPath);
        }


        return;
    }



    jclass callbackClass = nullptr;

    jmethodID progressMid = nullptr;



    if (callback)
    {
        callbackClass =
                env->GetObjectClass(callback);


        if (callbackClass)
        {
            progressMid =
                    env->GetMethodID(
                            callbackClass,
                            "onProgressUpdate",
                            "(ILjava/lang/String;)V");
        }


        if (!progressMid)
        {
            LOGW(
                "Progress callback unavailable");
        }
    }



    // -----------------------------------------------------------------------
    // STEP 1:
    // Load ROM
    // -----------------------------------------------------------------------

    debug_ui(
            env,
            callback,
            progressMid,
            0,
            "Loading ROM into memory...");



    if (lseek(
            romFd,
            0,
            SEEK_SET) < 0)
    {
        LOGE(
            "Unable to seek ROM fd errno=%d",
            errno);

        goto cleanup_strings;
    }



    romSizeOff =
            lseek(
                    romFd,
                    0,
                    SEEK_END);



    if (romSizeOff <= 0)
    {
        LOGE(
            "Unable determining ROM size errno=%d",
            errno);

        goto cleanup_strings;
    }



    romSize =
            static_cast<size_t>(
                    romSizeOff);



    if (romSize > MAX_ASSET_SIZE)
    {
        LOGE(
            "ROM exceeds safety limit: %zu bytes",
            romSize);

        goto cleanup_strings;
    }



    if (lseek(
            romFd,
            0,
            SEEK_SET) < 0)
    {
        LOGE(
            "Unable resetting ROM position");

        goto cleanup_strings;
    }



    romData =
            static_cast<uint8_t*>(
                    malloc(romSize));



    if (!romData)
    {
        LOGE(
            "ROM allocation failed size=%zu",
            romSize);

        goto cleanup_strings;
    }



    totalRead = 0;



    while (totalRead < romSize)
    {
        ssize_t count =
                read(
                        romFd,
                        romData + totalRead,
                        romSize - totalRead);



        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }


            LOGE(
                "ROM read failed errno=%d",
                errno);


            free(romData);

            goto cleanup_strings;
        }



        if (count == 0)
        {
            break;
        }



        totalRead +=
                static_cast<size_t>(count);
    }



    if (totalRead != romSize)
    {
        LOGE(
            "Incomplete ROM read %zu/%zu",
            totalRead,
            romSize);


        free(romData);

        goto cleanup_strings;
    }



    LOGI(
        "Loaded ROM size=%zu",
        romSize);



    // -----------------------------------------------------------------------
    // Normalize ROM byte order
    // -----------------------------------------------------------------------

    if (romSize >= 4)
    {
        if (romData[0] == 0x37 &&
            romData[1] == 0x80 &&
            romData[2] == 0x40 &&
            romData[3] == 0x12)
        {
            LOGI(
                "Detected v64 ROM. Swapping bytes.");


            byteswap_v64(
                    romData,
                    romSize);
        }
        else if (
                romData[0] == 0x40 &&
                romData[1] == 0x12 &&
                romData[2] == 0x37 &&
                romData[3] == 0x80)
        {
            LOGI(
                "Detected n64 ROM. Swapping bytes.");


            byteswap_n64(
                    romData,
                    romSize);
        }
        else if (
                romData[0] == 0x80 &&
                romData[1] == 0x37 &&
                romData[2] == 0x12 &&
                romData[3] == 0x40)
        {
            LOGI(
                "Detected z64 ROM. No swap needed.");
        }
        else
        {
            LOGW(
                "Unknown ROM magic %02X %02X %02X %02X",
                romData[0],
                romData[1],
                romData[2],
                romData[3]);
        }
    }



    debug_ui(
            env,
            callback,
            progressMid,
            10,
            "Writing normalized ROM base...");



    if (!write_rom_base_from_memory(
            romData,
            romSize,
            cOutDir))
    {
        LOGE(
            "Failed writing rom_base.bin");


        free(romData);

        goto cleanup_strings;
    }



    // -----------------------------------------------------------------------
    // STEP 2:
    // Open manifest
    // -----------------------------------------------------------------------

    mFile =
            fopen(
                    cManifestPath,
                    "rb");


    if (!mFile)
    {
        LOGW(
            "Manifest missing: %s",
            cManifestPath);


        debug_ui(
                env,
                callback,
                progressMid,
                100,
                "Extraction complete (ROM-only mode)");


        free(romData);

        goto cleanup_strings;
    }



    entryCount = 0;


    if (fread(
            &entryCount,
            sizeof(uint32_t),
            1,
            mFile) != 1)
    {
        LOGE(
            "Unable reading manifest header");


        fclose(mFile);

        free(romData);

        goto cleanup_strings;
    }

    // Convert manifest entry count from Big-Endian if needed and set global swap flag
    if (entryCount > MAX_MANIFEST_ENTRIES)
    {
        uint32_t swappedCount = swap_uint32(entryCount);
        if (swappedCount <= MAX_MANIFEST_ENTRIES)
        {
            entryCount = swappedCount;
            manifestNeedsSwap = true;
        }
    }

    if (entryCount == 0 ||
        entryCount > MAX_MANIFEST_ENTRIES)
    {
        LOGE(
            "Invalid manifest entry count: %u",
            entryCount);


        fclose(mFile);

        free(romData);

        goto cleanup_strings;
    }



    LOGI(
        "Processing %u manifest entries",
        entryCount);



    // -----------------------------------------------------------------------
    // STEP 3:
    // Extract assets
    // -----------------------------------------------------------------------

    extracted = 0;
    compressed = 0;
    failed = 0;

    lastPercent = -1;



    for (uint32_t i = 0;
         i < entryCount;
         i++)
    {
        ManifestEntry entry;


        if (fread(
                &entry,
                sizeof(ManifestEntry),
                1,
                mFile) != 1)
        {
            LOGE(
                "Manifest read failed at entry %u",
                i);

            failed++;

            break;
        }

        // Apply swap globally based on header determination
        if (manifestNeedsSwap)
        {
            entry.offset = swap_uint32(entry.offset);
            entry.size = swap_uint32(entry.size);
        }

        // Ensure strings are terminated before logging
        entry.name[sizeof(entry.name)-1] = '\0';
        entry.type[sizeof(entry.type)-1] = '\0';



        /*
         * Validate ROM location.
         *
         * Prevent:
         *  - integer overflow
         *  - reading beyond ROM buffer
         */

        if (entry.offset >= romSize)
        {
            LOGW(
                "Skipping invalid offset asset %.32s offset=%u",
                entry.name,
                entry.offset);

            failed++;

            continue;
        }



        if (entry.size == 0)
        {
            LOGW(
                "Skipping empty asset %.32s",
                entry.name);

            failed++;

            continue;
        }



        uint64_t endOffset =
                static_cast<uint64_t>(entry.offset) +
                static_cast<uint64_t>(entry.size);



        if (endOffset > romSize)
        {
            LOGW(
                "Clamping oversized asset %.32s",
                entry.name);


            entry.size =
                static_cast<uint32_t>(
                        romSize - entry.offset);
        }



        if (entry.size == 0)
        {
            failed++;
            continue;
        }



        uint8_t* assetBuffer =
                romData + entry.offset;



        char outputPath[512];


        snprintf(
                outputPath,
                sizeof(outputPath),
                "%s/asset_%08X.bin",
                cOutDir,
                entry.offset);



        LOGI(
            "Asset %u/%u %.32s offset=%08X size=%u",
            i + 1,
            entryCount,
            entry.name,
            entry.offset,
            entry.size);



        bool isRareCompressed =
                false;


        if (entry.size >= 8 &&
            assetBuffer[0] == 0x11 &&
            assetBuffer[1] == 0x72)
        {
            uint32_t declaredSize =
                    ((uint32_t)assetBuffer[2] << 24) |
                    ((uint32_t)assetBuffer[3] << 16) |
                    ((uint32_t)assetBuffer[4] << 8) |
                    ((uint32_t)assetBuffer[5]);


            if (declaredSize > 0 &&
                declaredSize <= MAX_ASSET_SIZE)
            {
                isRareCompressed = true;


                LOGI(
                    "Rare compression detected %.32s output=%u",
                    entry.name,
                    declaredSize);
            }
            else
            {
                LOGW(
                    "Ignoring invalid Rare header %.32s declared=%u",
                    entry.name,
                    declaredSize);
            }
        }



        if (isRareCompressed)
        {
            uint32_t outputSize = 0;

            uint64_t remainingInRom =
                    (romSize > entry.offset)
                            ? (romSize - entry.offset)
                            : 0;

            uint32_t availableSize =
                    static_cast<uint32_t>(
                            remainingInRom > MAX_ASSET_SIZE
                                    ? MAX_ASSET_SIZE
                                    : remainingInRom);


            uint8_t* output =
                    decompress_rare_asset(
                            assetBuffer,
                            availableSize,
                            &outputSize);



            if (output &&
                outputSize > 0)
            {
                FILE* out =
                        fopen(
                                outputPath,
                                "wb");



                if (out)
                {
                    size_t written =
                            fwrite(
                                    output,
                                    1,
                                    outputSize,
                                    out);


                    fclose(out);



                    if (written == outputSize)
                    {
                        extracted++;
                        compressed++;


                        LOGI(
                            "Extracted compressed asset %.32s bytes=%u",
                            entry.name,
                            outputSize);
                    }
                    else
                    {
                        LOGE(
                            "Short write %.32s %zu/%u",
                            entry.name,
                            written,
                            outputSize);

                        failed++;
                    }
                }
                else
                {
                    LOGE(
                        "Cannot open output %.32s errno=%d",
                        entry.name,
                        errno);

                    failed++;
                }


                free(output);
            }
            else
            {
                LOGE(
                    "Rare decompression failed %.32s",
                    entry.name);

                failed++;
            }
        }
        else
        {
            FILE* out =
                    fopen(
                            outputPath,
                            "wb");


            if (out)
            {
                size_t written =
                        fwrite(
                                assetBuffer,
                                1,
                                entry.size,
                                out);


                fclose(out);



                if (written == entry.size)
                {
                    extracted++;


                    LOGI(
                        "Extracted raw asset %.32s bytes=%u",
                        entry.name,
                        entry.size);
                }
                else
                {
                    LOGE(
                        "Raw asset write failed %.32s",
                        entry.name);

                    failed++;
                }
            }
            else
            {
                LOGE(
                    "Cannot create raw asset %.32s errno=%d",
                    entry.name,
                    errno);

                failed++;
            }
        }



        // Progress update
        int percent =
                10 +
                static_cast<int>(
                        ((uint64_t)i * 89) /
                        entryCount);



        if (percent != lastPercent)
        {
            char status[128];


            snprintf(
                    status,
                    sizeof(status),
                    "Extracting: %.32s",
                    entry.name);



            if (!debug_ui(
                    env,
                    callback,
                    progressMid,
                    percent,
                    status))
            {
                LOGW(
                    "UI callback stopped extraction");

                break;
            }


            lastPercent = percent;
        }
    }

    // -----------------------------------------------------------------------
    // STEP 4:
    // Finalize extraction
    // -----------------------------------------------------------------------

    fclose(mFile);

    free(romData);


    LOGI(
        "Extraction complete: extracted=%u compressed=%u failed=%u total=%u",
        extracted,
        compressed,
        failed,
        entryCount);



    char summary[256];


    snprintf(
            summary,
            sizeof(summary),
            "Extraction complete! %u assets extracted, %u failed",
            extracted,
            failed);



    debug_ui(
            env,
            callback,
            progressMid,
            100,
            summary);



cleanup_strings:


    if (cOutDir)
    {
        env->ReleaseStringUTFChars(
                outDir,
                cOutDir);
    }


    if (cManifestPath)
    {
        env->ReleaseStringUTFChars(
                manifestPath,
                cManifestPath);
    }


    if (callbackClass)
    {
        env->DeleteLocalRef(
                callbackClass);
    }
}
