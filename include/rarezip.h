#ifndef RAREZIP_H
#define RAREZIP_H

#ifdef __cplusplus
extern "C" {
#endif

// Global decompression state
extern struct huft *D_80007270;
extern struct huft *D_80007290;

extern u8 *inbuf;       // Input buffer (source data)
extern u8 *D_80007284;  // Output buffer (decompressed data)
extern u32 inptr;       // Current read position in inbuf
extern u32 wp;          // Current write position in D_80007284
extern u32 bb;          // Bit buffer
extern u32 bk;          // Bit count
extern u32 crc1;        // CRC1
extern u32 crc2;        // CRC2
extern u32 hufts;       // Huffman table usage tracker
extern u32 g_decomp_out_cap; // Output buffer capacity

#ifndef WSIZE
#define WSIZE 0x8000     /* window size--must be a power of two, and at least 32K for zip's deflate method */
#endif

#define get_byte() (inbuf[inptr++])

#ifdef CRYPT
  uch cc;
#define NEXTBYTE() (decrypt ? (cc = get_byte(), zdecode(cc), cc) : get_byte())
#else
#define NEXTBYTE() (u8)get_byte()
#endif

#define NEEDBITS(n) { while(k < (n)) { b |= ((u32)NEXTBYTE()) << k; k += 8; } }
#define DUMPBITS(n) { b >>= (n); k -= (n); }

struct huft {
    u8 e;                /* number of extra bits or operation */
    u8 b;                /* number of bits in this code or subcode */
    union {
        u16 n;          /* literal, length base, or distance base */
        struct huft *t; /* pointer to next level of table */
    } v;
};

#define BMAX 16         /* maximum bit length of any code (16 for explode) */
#define N_MAX 288       /* maximum number of codes in any set */

// Renamed to match the wrapper in stubs.cpp
int bkboot_inflate_unlocked(void);

#ifdef __cplusplus
}
#endif

#endif // RAREZIP_H
