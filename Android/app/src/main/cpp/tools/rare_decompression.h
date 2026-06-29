#ifndef RARE_DECOMPRESSION_H
#define RARE_DECOMPRESSION_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decompresses a standalone Rare asset starting with the 0x1172 6-byte header.
 * Used primarily for asset file processing outside the active runtime pipeline.
 *
 * @param src Pointer to the start of the compressed data.
 * @param src_size Size of the input buffer (needed for bounds checking).
 * @param out_size Pointer to store the resulting decompressed size.
 * @return Pointer to the decompressed buffer (caller must free()).
 */
uint8_t* decompress_rare_asset(const uint8_t* src, uint32_t src_size, uint32_t* out_size);

/**
 * Runtime HLE Bridge: Direct override for the game's internal decompression routine.
 * Resolves 32-bit Big-Endian block sizing layers from the inline 8-byte allocation header.
 *
 * @param in Pointer to the start of the compressed bitstream payload.
 * @param out_start Pointer to the pre-allocated virtual DRAM destination target memory workspace.
 * @param arg2 Pointer to the base of the 8-byte allocation structure metadata header.
 */
void decompress_rare_runtime_hle(const uint8_t* in, uint8_t* out_start, const uint8_t* arg2);

#ifdef __cplusplus
}
#endif

#endif // RARE_DECOMPRESSION_H
