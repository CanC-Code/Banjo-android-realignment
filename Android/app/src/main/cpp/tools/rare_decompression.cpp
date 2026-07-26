// File: Android/app/src/main/cpp/tools/rare_decompression.cpp

#include "rare_decompression.h"

#include <android/log.h>
#include <zlib.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#define LOG_TAG "BKA_DECOMP"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LOGW(...) \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)


// ---------------------------------------------------------------------------
// Safety limits
// ---------------------------------------------------------------------------

// Prevent corrupted headers from requesting absurd allocations.
static constexpr uint32_t MAX_RARE_OUTPUT_SIZE = 0x10000000; // 256 MB

// Prevent malformed HLE metadata from reading unlimited memory.
static constexpr uint32_t MAX_HLE_COMPRESSED_SIZE = 0x10000000;


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t read_be32(const uint8_t* p)
{
    return
        (static_cast<uint32_t>(p[0]) << 24) |
        (static_cast<uint32_t>(p[1]) << 16) |
        (static_cast<uint32_t>(p[2]) << 8)  |
        static_cast<uint32_t>(p[3]);
}


// ---------------------------------------------------------------------------
// Raw DEFLATE decompression
// ---------------------------------------------------------------------------

static uint32_t inflate_raw_deflate_safe(
        const uint8_t* src,
        uint32_t src_size,
        uint8_t* dst,
        uint32_t dst_size)
{
    if (!src || src_size == 0 || !dst || dst_size == 0)
    {
        LOGE(
            "inflate rejected invalid buffer src=%p dst=%p src_size=%u dst_size=%u",
            src,
            dst,
            src_size,
            dst_size);

        return 0;
    }


    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // Force the statically linked zlib/miniz library to use its default 
    // internal memory allocators to prevent ABI and struct offset mismatches.
    strm.zalloc = Z_NULL;
    strm.zfree  = Z_NULL;
    strm.opaque = Z_NULL;


    int init =
        inflateInit2(&strm, -15);


    if (init != Z_OK)
    {
        LOGE(
            "inflateInit2 failed: %d",
            init);

        return 0;
    }


    strm.next_in =
        const_cast<Bytef*>(
            reinterpret_cast<const Bytef*>(src));

    strm.avail_in =
        src_size;


    strm.next_out =
        reinterpret_cast<Bytef*>(dst);

    strm.avail_out =
        dst_size;


    int ret = Z_OK;


    while (true)
    {
        ret = inflate(
                &strm,
                Z_NO_FLUSH);


        if (ret == Z_STREAM_END)
        {
            break;
        }


        if (ret != Z_OK)
        {
            LOGE(
                "inflate failed ret=%d avail_in=%u avail_out=%u",
                ret,
                strm.avail_in,
                strm.avail_out);

            inflateEnd(&strm);
            return 0;
        }


        if (strm.avail_out == 0)
        {
            LOGW(
                "inflate output buffer exhausted");

            break;
        }


        if (strm.avail_in == 0)
        {
            LOGW(
                "inflate input exhausted before stream end");

            break;
        }
    }


    uint32_t total_out =
        static_cast<uint32_t>(strm.total_out);


    inflateEnd(&strm);


    if (total_out == 0)
    {
        LOGE(
            "inflate produced zero bytes");

        return 0;
    }


    return total_out;
}



// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C"
{

uint32_t decompress_rare_to_offset(
    const uint8_t* src,
    uint32_t src_size,
    uint8_t* out_buffer,
    uint32_t out_offset,
    uint32_t out_size)
{
    if (!src || !out_buffer)
    {
        LOGE("decompress_rare_to_offset invalid pointer");
        return 0;
    }

    LOGI("Decompressing to offset %u (compressed=%u, expected=%u)", out_offset, src_size, out_size);

    if (src_size == 0 || src_size > MAX_HLE_COMPRESSED_SIZE)
    {
        LOGE("Invalid compressed size=%u", src_size);
        return 0;
    }

    if (out_size == 0 || out_size > MAX_RARE_OUTPUT_SIZE)
    {
        LOGE("Invalid output size=%u", out_size);
        return 0;
    }

    // Decompress raw deflate payload directly to the pre-allocated buffer at the specified offset
    return inflate_raw_deflate_safe(
            src,
            src_size,
            out_buffer + out_offset,
            out_size);
}

}
