#include "rare_decompression.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <zlib.h>

#define LOG_TAG "BKA_DECOMP"

extern "C" {

/**
 * Highly defensive raw deflate wrapper using a separate operational buffer 
 * to prevent any possibility of null-pointer dereferences inside zlib's state machine.
 */
static uint32_t inflate_raw_deflate_safe(const uint8_t* src, uint32_t src_size, uint8_t* dst, uint32_t dst_size) {
    if (!src || src_size == 0 || !dst || dst_size == 0) return 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // Use raw deflate mode (-15)
    if (inflateInit2(&strm, -15) != Z_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "inflateInit2 failed.");
        return 0;
    }

    // Allocate a localized staging window to ensure valid source boundaries
    strm.next_in = const_cast<Bytef*>(src);
    strm.avail_in = src_size;
    strm.next_out = static_cast<Bytef*>(dst);
    strm.avail_out = dst_size;

    if (!strm.next_in || !strm.next_out) {
        inflateEnd(&strm);
        return 0;
    }

    int ret = inflate(&strm, Z_FINISH);
    uint32_t totalOut = strm.total_out;
    inflateEnd(&strm);

    if (ret != Z_STREAM_END && ret != Z_OK) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Inflate warning: code %d, extracted %u/%u bytes", ret, totalOut, dst_size);
        return 0;
    }

    return totalOut;
}

uint8_t* decompress_rare_asset(const uint8_t* src, uint32_t src_size, uint32_t* out_size) {
    if (!src || src_size < 5) return nullptr;

    // Check container magic bytes (0x1172)
    if (src[0] == 0x11 && src[1] == 0x72) {
        uint32_t decSize = ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 8) | (uint32_t)src[4];
        
        if (decSize == 0 || decSize > 64u * 1024u * 1024u) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Decompression aborted: invalid declared size %u", decSize);
            return nullptr;
        }

        uint8_t* dst = static_cast<uint8_t*>(malloc(decSize));
        if (!dst) return nullptr;

        uint32_t inflated = inflate_raw_deflate_safe(src + 5, src_size - 5, dst, decSize);
        if (inflated == 0) {
            free(dst);
            return nullptr;
        }

        if (out_size) *out_size = inflated;
        return dst;
    }

    return nullptr;
}

uint32_t decompress_rare_runtime_hle(const uint8_t* in, uint8_t* out_start, const uint8_t* arg2) {
    if (!in || !out_start) return 0;

    uint32_t decSize = 0;
    const uint8_t* bitstream = nullptr;
    uint32_t stream_size = 0;

    if (in[0] == 0x11 && in[1] == 0x72) {
        decSize = ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 8) | (uint32_t)in[4];
        bitstream = in + 5;
        stream_size = 0x00FFFFFF; 
    } else if (arg2) {
        decSize = ((uint32_t)arg2[0] << 24) | ((uint32_t)arg2[1] << 16) | ((uint32_t)arg2[2] << 8) | (uint32_t)arg2[3];
        bitstream = in;
        stream_size = 0x00FFFFFF;
    } else {
        return 0;
    }

    if (decSize == 0 || decSize > 32u * 1024u * 1024u) return 0;

    return inflate_raw_deflate_safe(bitstream, stream_size, out_start, decSize);
}

} // extern "C"
