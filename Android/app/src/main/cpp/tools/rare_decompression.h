// File: Android/app/src/main/cpp/tools/rare_decompression.h

#ifndef RARE_DECOMPRESSION_H
#define RARE_DECOMPRESSION_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decompresses a standalone Rare compressed asset.
 *
 * Expected input format:
 *   [0x11][0x72][4-byte big-endian decompressed size][raw DEFLATE payload]
 *
 * The returned buffer is allocated with malloc().
 * The caller MUST release it with free().
 *
 * @param src
 *      Pointer to the beginning of the compressed asset buffer.
 *
 * @param src_size
 *      Total size of the compressed asset buffer in bytes.
 *
 * @param out_size
 *      Output pointer receiving the number of decompressed bytes.
 *      Set to 0 on failure.
 *
 * @return
 *      malloc allocated decompressed buffer,
 *      or NULL on failure.
 */
uint8_t* decompress_rare_asset(
        const uint8_t* src,
        uint32_t src_size,
        uint32_t* out_size);


/**
 * Runtime HLE decompression bridge.
 *
 * Used when the emulator runtime requests decompression directly
 * into a pre-allocated virtual DRAM workspace.
 *
 * Input:
 *   - in:
 *       Pointer to raw DEFLATE payload.
 *
 *   - out_start:
 *       Destination workspace.
 *
 *   - arg2:
 *       8-byte big-endian metadata header:
 *
 *       [0..3] compressed payload size
 *       [4..7] expected decompressed size
 *
 * Returns:
 *   Number of bytes written to out_start.
 *
 * Returns 0 on invalid parameters or decompression failure.
 *
 * @param in
 *      Compressed payload pointer.
 *
 * @param out_start
 *      Destination memory buffer.
 *
 * @param arg2
 *      Metadata header describing sizes.
 *
 * @return
 *      Number of decompressed bytes written.
 */
uint32_t decompress_rare_runtime_hle(
        const uint8_t* in,
        uint8_t* out_start,
        const uint8_t* arg2);


#ifdef __cplusplus
}
#endif

#endif // RARE_DECOMPRESSION_H