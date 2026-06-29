#include "rare_decompression.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "BKA_DECOMP"

extern "C" {

/**
 * Standalone entry point: Decompresses raw asset files using a 6-byte magic header.
 */
uint8_t* decompress_rare_asset(const uint8_t* src,
                               uint32_t       src_size,
                               uint32_t* out_size) {
    if (!src || src_size < 6) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
            "decompress_rare_asset: src is null or too small (%u bytes)", src_size);
        return nullptr;
    }
    if (src[0] != 0x11 || src[1] != 0x72) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
            "decompress_rare_asset: bad magic %02X %02X", src[0], src[1]);
        return nullptr;
    }

    uint32_t decSize = ((uint32_t)src[2] << 16)
                     | ((uint32_t)src[3] <<  8)
                     |  (uint32_t)src[4];

    if (decSize == 0 || decSize > 64u * 1024u * 1024u) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
            "decompress_rare_asset: implausible decSize %u", decSize);
        return nullptr;
    }

    uint8_t* dst = static_cast<uint8_t*>(malloc(decSize));
    if (!dst) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
            "decompress_rare_asset: OOM allocating %u bytes", decSize);
        return nullptr;
    }

    const uint8_t* in      = src + 5;
    const uint8_t* in_end  = src + src_size;
    uint8_t* out     = dst;
    const uint8_t* out_end = dst + decSize;

    while (out < out_end) {
        if (in >= in_end) {
            free(dst);
            return nullptr;
        }

        uint8_t cmd = *in++;

        for (int bit = 7; bit >= 0 && out < out_end; bit--) {
            if ((cmd >> bit) & 1) {
                if (in >= in_end) {
                    free(dst);
                    return nullptr;
                }
                *out++ = *in++;
            } else {
                if (in + 2 > in_end) {
                    free(dst);
                    return nullptr;
                }
                uint8_t b0 = *in++;
                uint8_t b1 = *in++;

                uint32_t count  = ((uint32_t)(b0 >> 4) & 0x0Fu) + 3u;
                uint32_t offset = (((uint32_t)(b0 & 0x0Fu) << 8) | (uint32_t)b1) + 1u;

                if (offset > (uint32_t)(out - dst)) {
                    free(dst);
                    return nullptr;
                }

                for (uint32_t j = 0; j < count && out < out_end; j++) {
                    *out = *(out - offset);
                    out++;
                }
            }
        }
    }

    if (out_size) *out_size = decSize;
    return dst;
}

/**
 * Runtime HLE Bridge: Direct override for the game's internal decompression routine.
 * Employs a hybrid strategy to extract sizes from the payload stream or block metadata structure safely.
 */
void decompress_rare_runtime_hle(const uint8_t* in, uint8_t* out_start, const uint8_t* arg2) {
    if (!in || !out_start) {
        return;
    }

    uint32_t decSize = 0;

    // Strategy A: If the stream points to a raw 0x1172 asset file block, extract the 24-bit size
    if (in[0] == 0x11 && in[1] == 0x72) {
        decSize = ((uint32_t)in[2] << 16)
                | ((uint32_t)in[3] <<  8)
                |  (uint32_t)in[4];
    }
    // Strategy B: Fallback to reading the 32-bit Big-Endian header allocation size field via arg2
    else if (arg2) {
        decSize = ((uint32_t)arg2[0] << 24)
                | ((uint32_t)arg2[1] << 16)
                | ((uint32_t)arg2[2] <<  8)
                |  (uint32_t)arg2[3];
    }

    // Validation boundary guard check
    if (decSize == 0 || decSize > 32u * 1024u * 1024u) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "decompress_rare_runtime_hle: Implausible size calculation rejected: %u bytes", decSize);
        return;
    }

    // Skip the 6-byte header if reading directly from an intact standalone payload segment block
    const uint8_t* bitstream = (in[0] == 0x11 && in[1] == 0x72) ? (in + 5) : in;

    uint8_t* out = out_start;
    const uint8_t* out_end = out_start + decSize;

    while (out < out_end) {
        uint8_t cmd = *bitstream++;

        for (int bit = 7; bit >= 0 && out < out_end; bit--) {
            if ((cmd >> bit) & 1) {
                *out++ = *bitstream++;
            } else {
                uint8_t b0 = *bitstream++;
                uint8_t b1 = *bitstream++;

                uint32_t count  = ((uint32_t)(b0 >> 4) & 0x0Fu) + 3u;
                uint32_t offset = (((uint32_t)(b0 & 0x0Fu) << 8) | (uint32_t)b1) + 1u;

                if (offset > (uint32_t)(out - out_start)) {
                    memset(out, 0, out_end - out);
                    return;
                }

                for (uint32_t j = 0; j < count && out < out_end; j++) {
                    *out = *(out - offset);
                    out++;
                }
            }
        }
    }
}

} // extern "C"
