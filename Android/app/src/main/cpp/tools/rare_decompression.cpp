#include "rare_decompression.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <zlib.h>
#include <android/log.h>

#define LOG_TAG "BKA_DECOMP"
#define CHUNK_SIZE 32768

extern "C" {

uint8_t* decompress_rare_asset(const uint8_t* src,
                               uint32_t src_size,
                               uint32_t* out_size) {
    if (!src || src_size < 6) return nullptr;
    if (src[0] != 0x11 || src[1] != 0x72) return nullptr;

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    strm.next_in  = const_cast<Bytef*>(src + 6);
    strm.avail_in = src_size - 6;

    if (inflateInit2(&strm, -15) != Z_OK) return nullptr;

    uint32_t currentCapacity = CHUNK_SIZE;
    uint8_t* outBuf = static_cast<uint8_t*>(malloc(currentCapacity));
    if (!outBuf) {
        inflateEnd(&strm);
        return nullptr;
    }

    uint32_t totalOut = 0;
    int ret;

    do {
        if (totalOut + CHUNK_SIZE > currentCapacity) {
            if (currentCapacity >= 64u * 1024u * 1024u) {
                __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                    "Decompression exceeded 64MB cap. Aborting.");
                free(outBuf);
                inflateEnd(&strm);
                return nullptr;
            }
            currentCapacity *= 2;
            uint8_t* newBuf = static_cast<uint8_t*>(realloc(outBuf, currentCapacity));
            if (!newBuf) {
                __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                    "OOM: Failed to realloc to %u bytes.", currentCapacity);
                free(outBuf);
                inflateEnd(&strm);
                return nullptr;
            }
            outBuf = newBuf;
        }

        strm.next_out  = outBuf + totalOut;
        strm.avail_out = CHUNK_SIZE; // expansion above guarantees this fits

        ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR ||
            ret == Z_MEM_ERROR    || ret == Z_NEED_DICT) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                "Zlib inflate error: %d", ret);
            free(outBuf);
            inflateEnd(&strm);
            return nullptr;
        }

        totalOut += CHUNK_SIZE - strm.avail_out;

    } while (ret != Z_STREAM_END && ret != Z_BUF_ERROR);

    inflateEnd(&strm);

    if (totalOut == 0) {
        free(outBuf);
        return nullptr;
    }

    // Trim allocation to actual size
    uint8_t* trimmed = static_cast<uint8_t*>(realloc(outBuf, totalOut));
    if (trimmed) outBuf = trimmed;

    if (out_size) *out_size = totalOut;
    return outBuf;
}

} // extern "C"