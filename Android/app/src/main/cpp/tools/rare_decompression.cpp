#include "rare_decompression.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "BKA_DECOMP"

extern "C" {

/*
 * Rare Ltd. proprietary LZ compression used in Banjo-Kazooie (N64).
 * Magic: 0x11 0x72
 *
 * Header layout (6 bytes total):
 *   [0]     = 0x11  (magic byte 0)
 *   [1]     = 0x72  (magic byte 1)
 *   [2..4]  = uncompressed size, big-endian 24-bit (3 bytes)
 *   [5]     = first byte of compressed bitstream
 *
 * Bitstream format (cmd-byte driven):
 *   Each iteration reads one cmd byte. Bits are tested MSB→LSB (bit 7 first).
 *   For each bit:
 *     1 → literal: copy one byte from input to output.
 *     0 → back-reference:
 *           read two bytes b0, b1:
 *           count  = ((b0 >> 4) & 0x0F) + 3
 *           offset = (((b0 & 0x0F) << 8) | b1) + 1
 *           copy `count` bytes from (out_ptr - offset).
 *
 * Decompression stops when `decSize` output bytes have been written.
 * Any remaining bits in the current cmd byte are discarded.
 */
uint8_t* decompress_rare_asset(const uint8_t* src,
                               uint32_t       src_size,
                               uint32_t*      out_size) {
    // Minimum valid stream: 2 magic + 3 size bytes + 1 cmd byte = 6
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

    // Uncompressed size is a big-endian 24-bit value in bytes [2..4]
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

    // Input cursor starts at byte 5 (first byte of compressed bitstream)
    const uint8_t* in      = src + 5;
    const uint8_t* in_end  = src + src_size;
    uint8_t*       out     = dst;
    const uint8_t* out_end = dst + decSize;

    while (out < out_end) {
        // Guard: need at least one cmd byte
        if (in >= in_end) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                "decompress_rare_asset: input exhausted reading cmd byte "
                "(out=%zu decSize=%u)", (size_t)(out - dst), decSize);
            free(dst);
            return nullptr;
        }

        uint8_t cmd = *in++;

        for (int bit = 7; bit >= 0 && out < out_end; bit--) {
            if ((cmd >> bit) & 1) {
                // ── Literal ──────────────────────────────────────────────
                if (in >= in_end) {
                    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "decompress_rare_asset: input exhausted reading literal");
                    free(dst);
                    return nullptr;
                }
                *out++ = *in++;
            } else {
                // ── Back-reference ────────────────────────────────────────
                if (in + 2 > in_end) {
                    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "decompress_rare_asset: input exhausted reading back-ref");
                    free(dst);
                    return nullptr;
                }
                uint8_t b0 = *in++;
                uint8_t b1 = *in++;

                uint32_t count  = ((uint32_t)(b0 >> 4) & 0x0Fu) + 3u;
                uint32_t offset = (((uint32_t)(b0 & 0x0Fu) << 8) | (uint32_t)b1) + 1u;

                // Guard: offset must not point before the output buffer
                if (offset > (uint32_t)(out - dst)) {
                    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "decompress_rare_asset: invalid back-ref offset %u "
                        "(only %zu bytes written)", offset, (size_t)(out - dst));
                    free(dst);
                    return nullptr;
                }

                // Copy byte-by-byte to handle overlapping runs correctly
                for (uint32_t j = 0; j < count && out < out_end; j++) {
                    *out = *(out - offset);
                    out++;
                }
            }
        }
    }

    if (out_size) *out_size = decSize;

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
        "decompress_rare_asset: OK — %u bytes decompressed", decSize);
    return dst;
}

} // extern "C"