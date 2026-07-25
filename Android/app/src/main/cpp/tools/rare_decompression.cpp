// File: Android/app/src/main/cpp/tools/rare_decompression.cpp
#include "rare_decompression.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <android/log.h>
#include <zlib.h>

#define LOG_TAG "BKA_DECOMP"

// Explicit allocation wrappers to prevent null-pointer execution 
// in statically compiled zlib/DEFLATE engines.
static void* zlib_alloc(void* opaque, unsigned items, unsigned size) {
    return malloc(items * size);
}

static void zlib_free(void* opaque, void* ptr) {
    free(ptr);
}

static uint32_t inflate_raw_deflate_safe(const uint8_t* src, uint32_t src_size, uint8_t* dst, uint32_t dst_size) {
    if (!src || src_size == 0 || !dst || dst_size == 0) {
        return 0;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // Bind explicit allocators before initialization
    strm.zalloc = zlib_alloc;
    strm.zfree = zlib_free;
    strm.opaque = Z_NULL;

    // -15 enables raw deflate (no zlib header)
    if (inflateInit2(&strm, -15) != Z_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "inflateInit2 failed.");
        return 0;
    }

    strm.next_in = const_cast<Bytef*>(src);
    strm.avail_in = src_size;
    strm.next_out = static_cast<Bytef*>(dst);
    strm.avail_out = dst_size;

    int ret = Z_OK;
    while (strm.avail_out > 0) {
        ret = inflate(&strm, Z_SYNC_FLUSH);
        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret != Z_OK) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Inflate encountered error code: %d (avail_in: %u, avail_out: %u)", ret, strm.avail_in, strm.avail_out);
            break;
        }
        if (strm.avail_in == 0 && ret == Z_OK) {
            break;
        }
    }

    uint32_t totalOut = strm.total_out;
    inflateEnd(&strm);

    if (totalOut == 0 && ret != Z_STREAM_END) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Decompression failed to extract any payload bytes.");
        return 0;
    }

    return totalOut;
}

// C-Linkage enforced for ABI compatibility with the caller files
extern "C" {

uint8_t* decompress_rare_asset(const uint8_t* src, uint32_t src_size, uint32_t* out_size) {
    if (!src || src_size <= 6 || !out_size) {
        if (out_size) *out_size = 0;
        return nullptr;
    }

    // Verify the 0x1172 Rare compression magic bytes
    if (src[0] != 0x11 || src[1] != 0x72) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Missing 0x1172 magic header. Found: 0x%02X%02X", src[0], src[1]);
        *out_size = 0;
        return nullptr;
    }

    // Isolate and resolve 32-bit Big-Endian uncompressed size from header indices [2..5]
    uint32_t uncompressed_size = (src[2] << 24) | (src[3] << 16) | (src[4] << 8) | src[5];
    
    if (uncompressed_size == 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Header indicated an uncompressed size of 0.");
        *out_size = 0;
        return nullptr;
    }

    uint8_t* dst = static_cast<uint8_t*>(malloc(uncompressed_size));
    if (!dst) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to allocate %u bytes for asset.", uncompressed_size);
        *out_size = 0;
        return nullptr;
    }

    // Push payload to safe inflater, bypassing the 6-byte header offset
    uint32_t result_size = inflate_raw_deflate_safe(src + 6, src_size - 6, dst, uncompressed_size);

    if (result_size == 0) {
        free(dst);
        *out_size = 0;
        return nullptr;
    }

    *out_size = result_size;
    return dst;
}

uint32_t decompress_rare_runtime_hle(const uint8_t* in, uint8_t* out_start, const uint8_t* arg2) {
    if (!in || !out_start || !arg2) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Invalid pointers passed to HLE bridge.");
        return 0;
    }

    // Resolve 32-bit Big-Endian block sizing layers from the inline 8-byte allocation header metadata.
    // Structural layout: [0..3] Expected Compressed Size, [4..7] Pre-allocated Workspace Target Size.
    uint32_t compressed_size = (arg2[0] << 24) | (arg2[1] << 16) | (arg2[2] << 8) | arg2[3];
    uint32_t expected_size   = (arg2[4] << 24) | (arg2[5] << 16) | (arg2[6] << 8) | arg2[7];

    // Establish failsafe bounds in the event of malformed inline structures
    if (expected_size == 0) {
        expected_size = 0x800000; // 8MB safety constraint limit for virtual DRAM workspace allocation
    }
    if (compressed_size == 0) {
        compressed_size = 0xFFFFFF; // Unbounded fallback processing
    }

    // Execute decompression directly targeting the virtual DRAM buffer
    return inflate_raw_deflate_safe(in, compressed_size, out_start, expected_size);
}

} // extern "C"
