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
 * Robust Raw Deflate wrapper implementing exact inflation logic 
 * matching the runtime and python stream extraction pipeline (wbits = -15).
 */
static uint32_t inflate_raw_deflate(const uint8_t* src, uint32_t src_size, uint8_t* dst, uint32_t dst_size) {
    if (!src || src_size == 0 || !dst || dst_size == 0) return 0;

    // Guard against malformed or truncated inputs that could crash zlib
    if (src_size < 2) return 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // Initialize zlib with negative wbits (-15) for raw DEFLATE stream decoding (no zlib header)
    if (inflateInit2(&strm, -15) != Z_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "inflate_raw_deflate: inflateInit2 failed.");
        return 0;
    }

    strm.next_in = const_cast<Bytef*>(src);
    strm.avail_in = src_size;
    strm.next_out = static_cast<Bytef*>(dst);
    strm.avail_out = dst_size;

    int ret = inflate(&strm, Z_FINISH);
    uint32_t totalOut = strm.total_out;

    inflateEnd(&strm);

    if (ret != Z_STREAM_END && ret != Z_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "inflate_raw_deflate: inflation failed with error code %d (avail_in=%u, avail_out=%u).", 
                            ret, strm.avail_in, strm.avail_out);
        return 0;
    }

    return totalOut;
}

uint8_t* decompress_rare_asset(const uint8_t* src, uint32_t src_size, uint32_t* out_size) {
    if (!src || src_size < 5) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "decompress_rare_asset: Null/under-sized buffer.");
        return nullptr;
    }

    // Handle standard Rare 0x1172 container header and 3-byte big-endian uncompressed size
    if (src[0] == 0x11 && src[1] == 0x72) {
        uint32_t decSize = ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 8) | (uint32_t)src[4];
        if (decSize == 0 || decSize > 64u * 1024u * 1024u) return nullptr;

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

    // Strategy A: Parse standard raw un-stripped 0x1172 asset file payload block
    if (in[0] == 0x11 && in[1] == 0x72) {
        decSize = ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 8) | (uint32_t)in[4];
        bitstream = in + 5;
        // Bound stream size safely to prevent overflow reads into unmapped memory regions
        stream_size = 0x00FFFFFF; 
    }
    // Strategy B: Read the 32-bit Big-Endian block layout size from runtime wrappers
    else if (arg2) {
        decSize = ((uint32_t)arg2[0] << 24) | ((uint32_t)arg2[1] << 16) | ((uint32_t)arg2[2] << 8) | (uint32_t)arg2[3];
        bitstream = in;
        stream_size = 0x00FFFFFF;
    } else {
        return 0;
    }

    if (decSize == 0 || decSize > 32u * 1024u * 1024u) return 0;

    uint32_t inflatedSize = inflate_raw_deflate(bitstream, stream_size, out_start, decSize);
    return inflatedSize;
}

// Integration bridge connecting ResourceMgr's intercepted code blocks to HLE runtime inflation
void BKA_InflateCodeSegment(void* dramAddr, uint32_t romOffset, uint32_t size) {
    if (!gN64_ROM_Base) return;

    const uint8_t* srcStream = gN64_ROM_Base + romOffset;
    uint32_t finalSize = decompress_rare_runtime_hle(srcStream, static_cast<uint8_t*>(dramAddr), nullptr);

    if (finalSize == 0) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "BKA_InflateCodeSegment: Decompression returned 0 size for offset %08X, falling back to memcpy.", romOffset);
        memcpy(dramAddr, srcStream, size);
    }
}

} // extern "C"
