#include "rare_decompression.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <zlib.h>

#define LOG_TAG "BKA_DECOMP"

// External global declaration for ROM base pointer
extern uint8_t* gN64_ROM_Base;

extern "C" {

/**
 * Robust Raw Deflate wrapper with strict pointer validation and safe state unwinding.
 */
static uint32_t inflate_raw_deflate(const uint8_t* src, uint32_t src_size, uint8_t* dst, uint32_t dst_size) {
    if (!src || src_size < 2 || !dst || dst_size == 0) return 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // Initialize zlib with raw DEFLATE (-15)
    if (inflateInit2(&strm, -15) != Z_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "inflate_raw_deflate: inflateInit2 failed.");
        return 0;
    }

    strm.next_in = const_cast<Bytef*>(src);
    strm.avail_in = src_size;
    strm.next_out = static_cast<Bytef*>(dst);
    strm.avail_out = dst_size;

    // Guard against internal state nullification bugs
    if (!strm.next_in || !strm.next_out) {
        inflateEnd(&strm);
        return 0;
    }

    int ret = inflate(&strm, Z_FINISH);
    uint32_t totalOut = strm.total_out;

    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "inflate_raw_deflate: Stream validation warning (ret=%d). Extracted: %u / Expected: %u", ret, totalOut, dst_size);
        // If it didn't finish cleanly, return 0 to trigger the fallback copy handler
        return 0;
    }

    return totalOut;
}

uint8_t* decompress_rare_asset(const uint8_t* src, uint32_t src_size, uint32_t* out_size) {
    if (!src || src_size < 5) {
        return nullptr;
    }

    // Validate container magic bytes (0x1172)
    if (src[0] == 0x11 && src[1] == 0x72) {
        uint32_t decSize = ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 8) | (uint32_t)src[4];
        
        // Sanity check allocation limits (max 64MB per asset block)
        if (decSize == 0 || decSize > 64u * 1024u * 1024u) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "decompress_rare_asset: Invalid decoded size descriptor: %u", decSize);
            return nullptr;
        }

        uint8_t* dst = static_cast<uint8_t*>(malloc(decSize));
        if (!dst) return nullptr;

        uint32_t inflated = inflate_raw_deflate(src + 5, src_size - 5, dst, decSize);
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

    return inflate_raw_deflate(bitstream, stream_size, out_start, decSize);
}

void BKA_InflateCodeSegment(void* dramAddr, uint32_t romOffset, uint32_t size) {
    if (!gN64_ROM_Base || !dramAddr) return;

    const uint8_t* srcStream = gN64_ROM_Base + romOffset;
    
    // Safely attempt decompression; fallback to direct copy if stream parsing fails or returns 0
    uint32_t finalSize = decompress_rare_runtime_hle(srcStream, static_cast<uint8_t*>(dramAddr), nullptr);

    if (finalSize == 0 && size > 0) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BKA_InflateCodeSegment: Safe fallback triggered for offset %08X (size: %u)", romOffset, size);
        memcpy(dramAddr, srcStream, size);
    }
}

} // extern "C"
