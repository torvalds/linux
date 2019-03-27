#ifndef blamka_round_ref_H
#define blamka_round_ref_H

#include "private/common.h"

/*designed by the Lyra PHC team */
static inline uint64_t
fBlaMka(uint64_t x, uint64_t y)
{
    const uint64_t m  = UINT64_C(0xFFFFFFFF);
    const uint64_t xy = (x & m) * (y & m);
    return x + y + 2 * xy;
}

#define G(a, b, c, d)          \
    do {                       \
        a = fBlaMka(a, b);     \
        d = ROTR64(d ^ a, 32); \
        c = fBlaMka(c, d);     \
        b = ROTR64(b ^ c, 24); \
        a = fBlaMka(a, b);     \
        d = ROTR64(d ^ a, 16); \
        c = fBlaMka(c, d);     \
        b = ROTR64(b ^ c, 63); \
    } while ((void) 0, 0)

#define BLAKE2_ROUND_NOMSG(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, \
                           v12, v13, v14, v15)                               \
    do {                                                                     \
        G(v0, v4, v8, v12);                                                  \
        G(v1, v5, v9, v13);                                                  \
        G(v2, v6, v10, v14);                                                 \
        G(v3, v7, v11, v15);                                                 \
        G(v0, v5, v10, v15);                                                 \
        G(v1, v6, v11, v12);                                                 \
        G(v2, v7, v8, v13);                                                  \
        G(v3, v4, v9, v14);                                                  \
    } while ((void) 0, 0)

#endif
