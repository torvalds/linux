/*
version 20080912
D. J. Bernstein
Public domain.
*/

#include <stdint.h>
#include <stdlib.h>

#include "crypto_core_hsalsa20.h"
#include "private/common.h"

#define ROUNDS 20
#define U32C(v) (v##U)

int
crypto_core_hsalsa20(unsigned char *out,
                     const unsigned char *in,
                     const unsigned char *k,
                     const unsigned char *c)
{
    uint32_t x0, x1, x2, x3, x4, x5, x6, x7, x8,
             x9, x10, x11, x12, x13, x14,  x15;
    int      i;

    if (c == NULL) {
        x0 = U32C(0x61707865);
        x5 = U32C(0x3320646e);
        x10 = U32C(0x79622d32);
        x15 = U32C(0x6b206574);
    } else {
        x0 = LOAD32_LE(c + 0);
        x5 = LOAD32_LE(c + 4);
        x10 = LOAD32_LE(c + 8);
        x15 = LOAD32_LE(c + 12);
    }
    x1 = LOAD32_LE(k + 0);
    x2 = LOAD32_LE(k + 4);
    x3 = LOAD32_LE(k + 8);
    x4 = LOAD32_LE(k + 12);
    x11 = LOAD32_LE(k + 16);
    x12 = LOAD32_LE(k + 20);
    x13 = LOAD32_LE(k + 24);
    x14 = LOAD32_LE(k + 28);
    x6 = LOAD32_LE(in + 0);
    x7 = LOAD32_LE(in + 4);
    x8 = LOAD32_LE(in + 8);
    x9 = LOAD32_LE(in + 12);

    for (i = ROUNDS; i > 0; i -= 2) {
        x4 ^= ROTL32(x0 + x12, 7);
        x8 ^= ROTL32(x4 + x0, 9);
        x12 ^= ROTL32(x8 + x4, 13);
        x0 ^= ROTL32(x12 + x8, 18);
        x9 ^= ROTL32(x5 + x1, 7);
        x13 ^= ROTL32(x9 + x5, 9);
        x1 ^= ROTL32(x13 + x9, 13);
        x5 ^= ROTL32(x1 + x13, 18);
        x14 ^= ROTL32(x10 + x6, 7);
        x2 ^= ROTL32(x14 + x10, 9);
        x6 ^= ROTL32(x2 + x14, 13);
        x10 ^= ROTL32(x6 + x2, 18);
        x3 ^= ROTL32(x15 + x11, 7);
        x7 ^= ROTL32(x3 + x15, 9);
        x11 ^= ROTL32(x7 + x3, 13);
        x15 ^= ROTL32(x11 + x7, 18);
        x1 ^= ROTL32(x0 + x3, 7);
        x2 ^= ROTL32(x1 + x0, 9);
        x3 ^= ROTL32(x2 + x1, 13);
        x0 ^= ROTL32(x3 + x2, 18);
        x6 ^= ROTL32(x5 + x4, 7);
        x7 ^= ROTL32(x6 + x5, 9);
        x4 ^= ROTL32(x7 + x6, 13);
        x5 ^= ROTL32(x4 + x7, 18);
        x11 ^= ROTL32(x10 + x9, 7);
        x8 ^= ROTL32(x11 + x10, 9);
        x9 ^= ROTL32(x8 + x11, 13);
        x10 ^= ROTL32(x9 + x8, 18);
        x12 ^= ROTL32(x15 + x14, 7);
        x13 ^= ROTL32(x12 + x15, 9);
        x14 ^= ROTL32(x13 + x12, 13);
        x15 ^= ROTL32(x14 + x13, 18);
    }

    STORE32_LE(out + 0, x0);
    STORE32_LE(out + 4, x5);
    STORE32_LE(out + 8, x10);
    STORE32_LE(out + 12, x15);
    STORE32_LE(out + 16, x6);
    STORE32_LE(out + 20, x7);
    STORE32_LE(out + 24, x8);
    STORE32_LE(out + 28, x9);

    return 0;
}
