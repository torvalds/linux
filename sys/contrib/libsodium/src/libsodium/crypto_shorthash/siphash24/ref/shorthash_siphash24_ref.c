#include "crypto_shorthash_siphash24.h"
#include "private/common.h"
#include "shorthash_siphash_ref.h"

int
crypto_shorthash_siphash24(unsigned char *out, const unsigned char *in,
                           unsigned long long inlen, const unsigned char *k)
{
    /* "somepseudorandomlygeneratedbytes" */
    uint64_t       v0 = 0x736f6d6570736575ULL;
    uint64_t       v1 = 0x646f72616e646f6dULL;
    uint64_t       v2 = 0x6c7967656e657261ULL;
    uint64_t       v3 = 0x7465646279746573ULL;
    uint64_t       b;
    uint64_t       k0 = LOAD64_LE(k);
    uint64_t       k1 = LOAD64_LE(k + 8);
    uint64_t       m;
    const uint8_t *end  = in + inlen - (inlen % sizeof(uint64_t));
    const int      left = inlen & 7;

    b = ((uint64_t) inlen) << 56;
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;
    for (; in != end; in += 8) {
        m = LOAD64_LE(in);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }
    switch (left) {
    case 7:
        b |= ((uint64_t) in[6]) << 48;
        /* FALLTHRU */
    case 6:
        b |= ((uint64_t) in[5]) << 40;
        /* FALLTHRU */
    case 5:
        b |= ((uint64_t) in[4]) << 32;
        /* FALLTHRU */
    case 4:
        b |= ((uint64_t) in[3]) << 24;
        /* FALLTHRU */
    case 3:
        b |= ((uint64_t) in[2]) << 16;
        /* FALLTHRU */
    case 2:
        b |= ((uint64_t) in[1]) << 8;
        /* FALLTHRU */
    case 1:
        b |= ((uint64_t) in[0]);
        break;
    case 0:
        break;
    }
    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;
    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    b = v0 ^ v1 ^ v2 ^ v3;
    STORE64_LE(out, b);

    return 0;
}
