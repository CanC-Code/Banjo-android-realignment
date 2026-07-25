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
