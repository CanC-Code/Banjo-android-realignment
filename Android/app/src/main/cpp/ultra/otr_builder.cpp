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
#include <vector>
#include <string>

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
// Parsed Segment Structure (Direct Splat YAML Representation)
// ---------------------------------------------------------------------------

struct SplatSegment {
    uint32_t start;
    uint32_t end;
    char name[64];
    char type[32];
};

// ---------------------------------------------------------------------------
// Safety constants
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_SPLIT_SEGMENTS = 100000;
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
// Minimal Line-Oriented Splat YAML Parser
// Avoids heavy external dependencies by parsing standard splat layouts directly.
// ---------------------------------------------------------------------------

static std::vector<SplatSegment> parse_splat_yaml(const char* yamlPath) {
    std::vector<SplatSegment> segments;
    FILE* file = fopen(yamlPath, "r");
    if (!file) {
        LOGW("Splat YAML missing or unreadable: %s", yamlPath);
        return segments;
    }

    char line[512];
    SplatSegment currentSeg = {};
    bool inSegmentsBlock = false;
    bool hasActiveSeg = false;

    while (fgets(line, sizeof(line), file)) {
        // Trim leading spaces
        char* ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;

        // Skip comments and empty lines
        if (*ptr == '#' || *ptr == '\n' || *ptr == '\r' || *ptr == '\0') {
            continue;
        }

        // Check block header
        if (strncmp(ptr, "segments:", 9) == 0) {
            inSegmentsBlock = true;
            continue;
        }

        if (!inSegmentsBlock) {
            continue;
        }

        // Detect list item start '-'
        if (*ptr == '-') {
            if (hasActiveSeg) {
                if (currentSeg.end > currentSeg.start) {
                    currentSeg.end = currentSeg.end; // calculated or explicit
                }
                segments.push_back(currentSeg);
            }
            currentSeg = {};
            hasActiveSeg = true;
            ptr++;
            while (*ptr == ' ' || *ptr == '\t') ptr++;
        }

        // Parse key-value attributes inside segment items or list entries
        // Splat formats can be '[start, type, name]' or explicit mapping keys
        if (*ptr == '[') {
            // Inline array format: - [0x00000000, code, main] or similar
            unsigned int startVal = 0;
            char typeBuf[32] = {0};
            char nameBuf[64] = {0};

            if (sscanf(ptr, "[%x, %31s , %63[^]]", &startVal, typeBuf, nameBuf) >= 2 ||
                sscanf(ptr, "[%x, %31s]", &startVal, typeBuf) >= 2) {

                // Clean trailing formatting or quotes from type/name
                for(int i = 0; typeBuf[i]; i++) if(typeBuf[i] == ',' || typeBuf[i] == ' ') typeBuf[i] = '\0';
                for(int i = 0; nameBuf[i]; i++) if(nameBuf[i] == ' ' || nameBuf[i] == '\'' || nameBuf[i] == '\"') nameBuf[i] = '\0';

                currentSeg.start = startVal;
                snprintf(currentSeg.type, sizeof(currentSeg.type), "%s", typeBuf);
                if (nameBuf[0] != '\0') {
                    snprintf(currentSeg.name, sizeof(currentSeg.name), "%s", nameBuf);
                } else {
                    snprintf(currentSeg.name, sizeof(currentSeg.name), "seg_%08X", startVal);
                }
                hasActiveSeg = true;
            }
        } else {
            // Key-value mapping format: start: 0x... / type: ... / name: ...
            char key[64] = {0};
            char val[256] = {0};
            if (sscanf(ptr, "%63[^:]: %255[^\n]", key, val) == 2) {
                // Trim trailing CR/LF
                size_t vlen = strlen(val);
                while (vlen > 0 && (val[vlen-1] == '\r' || val[vlen-1] == '\n' || val[vlen-1] == ' ')) {
                    val[--vlen] = '\0';
                }
                // Trim leading spaces in val
                char* vptr = val;
                while (*vptr == ' ' || *vptr == '\'' || *vptr == '\"') vptr++;

                if (strstr(key, "start")) {
                    currentSeg.start = static_cast<uint32_t>(strtoul(vptr, nullptr, 0));
                    hasActiveSeg = true;
                } else if (strstr(key, "end")) {
                    currentSeg.end = static_cast<uint32_t>(strtoul(vptr, nullptr, 0));
                } else if (strstr(key, "type")) {
                    snprintf(currentSeg.type, sizeof(currentSeg.type), "%s", vptr);
                } else if (strstr(key, "name")) {
                    snprintf(currentSeg.name, sizeof(currentSeg.name), "%s", vptr);
                }
            }
        }
    }

    if (hasActiveSeg) {
        segments.push_back(currentSeg);
    }

    fclose(file);

    // Calculate dynamic ends if missing
    for (size_t i = 0; i < segments.size(); i++) {
        if (segments[i].end == 0 || segments[i].end <= segments[i].start) {
            if (i + 1 < segments.size()) {
                segments[i].end = segments[i + 1].start;
            } else {
                segments[i].end = segments[i].start + 0x1000; // Default fallback slice size
            }
        }
        if (segments[i].name[0] == '\0') {
            snprintf(segments[i].name, sizeof(segments[i].name), "seg_%08X", segments[i].start);
        }
    }

    LOGI("Parsed %zu segments directly from Splat YAML: %s", segments.size(), yamlPath);
    return segments;
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
    uint32_t extracted = 0;
    uint32_t compressed = 0;
    uint32_t failed = 0;
    int lastPercent = -1;
    const char* cOutDir = nullptr;
    const char* cYamlPath = nullptr;
    jclass callbackClass = nullptr;
    jmethodID progressMid = nullptr;

    if (!env || romFd < 0 || !outDir || !manifestPath) {
        LOGE("Invalid JNI arguments");
        return;
    }

    cOutDir = env->GetStringUTFChars(outDir, nullptr);
    cYamlPath = env->GetStringUTFChars(manifestPath, nullptr);

    if (!cOutDir || !cYamlPath) {
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
    // STEP 3: Parse Splat YAML directly instead of binary manifest
    // -----------------------------------------------------------------------
    {
        std::vector<SplatSegment> segments = parse_splat_yaml(cYamlPath);
        if (segments.empty()) {
            LOGW("No segments parsed from YAML: %s (Running ROM-only base mode)", cYamlPath);
            debug_ui(env, callback, progressMid, 100, "Extraction complete (ROM-only mode)");
        } else {
            uint32_t segCount = static_cast<uint32_t>(segments.size());
            LOGI("Processing %u direct Splat YAML segments", segCount);

            // -----------------------------------------------------------------------
            // STEP 4: Extract assets in-place using YAML segment configurations
            // -----------------------------------------------------------------------
            extracted = 0;
            compressed = 0;
            failed = 0;
            lastPercent = -1;

            for (uint32_t i = 0; i < segCount; i++) {
                const auto& seg = segments[i];
                uint32_t offset = seg.start;
                uint32_t size = (seg.end > seg.start) ? (seg.end - seg.start) : 0;

                // Handle RAM-mapped virtual addresses and Splat segment offset shifts
                if (offset >= 0x80000000) {
                    offset &= 0x0FFFFFFF; // Strip KSEG0/KSEG1 base bits
                } else if (offset >= 0x04000000) {
                    offset -= 0x04000000;
                }

                if (offset >= romSize) {
                    if (offset >= 0x10000000 && (offset - 0x10000000) < romSize) {
                        offset -= 0x10000000;
                    } else {
                        LOGW("Skipping out-of-bounds segment %s (offset: 0x%X)", seg.name, seg.start);
                        failed++;
                        continue;
                    }
                }

                if (size == 0) {
                    // Calculate size based on next segment or bounds if 0
                    if (i + 1 < segCount) {
                        size = segments[i + 1].start - offset;
                    } else {
                        size = static_cast<uint32_t>(romSize - offset);
                    }
                }

                uint64_t endOffset = static_cast<uint64_t>(offset) + size;
                if (endOffset > romSize) {
                    LOGW("Clamping oversized segment %s", seg.name);
                    size = static_cast<uint32_t>(romSize - offset);
                }

                uint8_t* srcBuffer = romData + offset;
                uint8_t* destBuffer = romBaseBuffer + offset;

                // Replicate exact decompression byte matching and header check rules (wbits=-15 stream unpack / 0x1172 magic)
                bool isRareCompressed = false;
                if (size >= 8 && srcBuffer[0] == 0x11 && srcBuffer[1] == 0x72) {
                    uint32_t declaredSize = (static_cast<uint32_t>(srcBuffer[2]) << 24) | 
                                           (static_cast<uint32_t>(srcBuffer[3]) << 16) |
                                           (static_cast<uint32_t>(srcBuffer[4]) << 8)  | 
                                           static_cast<uint32_t>(srcBuffer[5]);
                    if (declaredSize > 0 && declaredSize <= MAX_ASSET_SIZE) {
                        isRareCompressed = true;
                    }
                }

                if (isRareCompressed) {
                    uint32_t written = 0;
                    uint8_t* decompressedData = decompress_rare_asset(srcBuffer, size, &written);
                    if (decompressedData && written > 0) {
                        if (written <= size || (offset + written <= romSize)) {
                            memcpy(destBuffer, decompressedData, written);
                            extracted++;
                            compressed++;
                        } else {
                            LOGE("Decompressed size exceeds buffer for segment %s", seg.name);
                            failed++;
                        }
                        free(decompressedData);
                    } else {
                        LOGE("Decompression failed for segment %s", seg.name);
                        failed++;
                    }
                } else {
                    memcpy(destBuffer, srcBuffer, size);
                    extracted++;
                }

                int percent = 10 + static_cast<int>(((uint64_t)i * 89) / segCount);
                if (percent != lastPercent) {
                    char status[128];
                    snprintf(status, sizeof(status), "Processing: %.64s", seg.name);
                    if (!debug_ui(env, callback, progressMid, percent, status)) {
                        break;
                    }
                    lastPercent = percent;
                }
            }

            LOGI("YAML Segment extraction complete: extracted=%u compressed=%u failed=%u total=%u",
                 extracted, compressed, failed, segCount);
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

    char summary[256];
    snprintf(summary, sizeof(summary),
             "Extraction complete! %u segments parsed & processed", extracted);
    debug_ui(env, callback, progressMid, 100, summary);

cleanup:
    if (romBaseBuffer) free(romBaseBuffer);
    if (romData) free(romData);
    if (cOutDir) env->ReleaseStringUTFChars(outDir, cOutDir);
    if (cYamlPath) env->ReleaseStringUTFChars(manifestPath, cYamlPath);
    if (callbackClass) env->DeleteLocalRef(callbackClass);
}
