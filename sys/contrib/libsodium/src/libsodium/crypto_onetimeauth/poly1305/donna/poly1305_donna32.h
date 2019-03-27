/*
   poly1305 implementation using 32 bit * 32 bit = 64 bit multiplication
   and 64 bit addition
*/

#if defined(_MSC_VER)
# define POLY1305_NOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
# define POLY1305_NOINLINE __attribute__((noinline))
#else
# define POLY1305_NOINLINE
#endif

#include "private/common.h"

#define poly1305_block_size 16

/* 17 + sizeof(unsigned long long) + 14*sizeof(unsigned long) */
typedef struct poly1305_state_internal_t {
    unsigned long      r[5];
    unsigned long      h[5];
    unsigned long      pad[4];
    unsigned long long leftover;
    unsigned char      buffer[poly1305_block_size];
    unsigned char      final;
} poly1305_state_internal_t;

static void
poly1305_init(poly1305_state_internal_t *st, const unsigned char key[32])
{
    /* r &= 0xffffffc0ffffffc0ffffffc0fffffff - wiped after finalization */
    st->r[0] = (LOAD32_LE(&key[0])) & 0x3ffffff;
    st->r[1] = (LOAD32_LE(&key[3]) >> 2) & 0x3ffff03;
    st->r[2] = (LOAD32_LE(&key[6]) >> 4) & 0x3ffc0ff;
    st->r[3] = (LOAD32_LE(&key[9]) >> 6) & 0x3f03fff;
    st->r[4] = (LOAD32_LE(&key[12]) >> 8) & 0x00fffff;

    /* h = 0 */
    st->h[0] = 0;
    st->h[1] = 0;
    st->h[2] = 0;
    st->h[3] = 0;
    st->h[4] = 0;

    /* save pad for later */
    st->pad[0] = LOAD32_LE(&key[16]);
    st->pad[1] = LOAD32_LE(&key[20]);
    st->pad[2] = LOAD32_LE(&key[24]);
    st->pad[3] = LOAD32_LE(&key[28]);

    st->leftover = 0;
    st->final    = 0;
}

static void
poly1305_blocks(poly1305_state_internal_t *st, const unsigned char *m,
                unsigned long long bytes)
{
    const unsigned long hibit = (st->final) ? 0UL : (1UL << 24); /* 1 << 128 */
    unsigned long       r0, r1, r2, r3, r4;
    unsigned long       s1, s2, s3, s4;
    unsigned long       h0, h1, h2, h3, h4;
    unsigned long long  d0, d1, d2, d3, d4;
    unsigned long       c;

    r0 = st->r[0];
    r1 = st->r[1];
    r2 = st->r[2];
    r3 = st->r[3];
    r4 = st->r[4];

    s1 = r1 * 5;
    s2 = r2 * 5;
    s3 = r3 * 5;
    s4 = r4 * 5;

    h0 = st->h[0];
    h1 = st->h[1];
    h2 = st->h[2];
    h3 = st->h[3];
    h4 = st->h[4];

    while (bytes >= poly1305_block_size) {
        /* h += m[i] */
        h0 += (LOAD32_LE(m + 0)) & 0x3ffffff;
        h1 += (LOAD32_LE(m + 3) >> 2) & 0x3ffffff;
        h2 += (LOAD32_LE(m + 6) >> 4) & 0x3ffffff;
        h3 += (LOAD32_LE(m + 9) >> 6) & 0x3ffffff;
        h4 += (LOAD32_LE(m + 12) >> 8) | hibit;

        /* h *= r */
        d0 = ((unsigned long long) h0 * r0) + ((unsigned long long) h1 * s4) +
             ((unsigned long long) h2 * s3) + ((unsigned long long) h3 * s2) +
             ((unsigned long long) h4 * s1);
        d1 = ((unsigned long long) h0 * r1) + ((unsigned long long) h1 * r0) +
             ((unsigned long long) h2 * s4) + ((unsigned long long) h3 * s3) +
             ((unsigned long long) h4 * s2);
        d2 = ((unsigned long long) h0 * r2) + ((unsigned long long) h1 * r1) +
             ((unsigned long long) h2 * r0) + ((unsigned long long) h3 * s4) +
             ((unsigned long long) h4 * s3);
        d3 = ((unsigned long long) h0 * r3) + ((unsigned long long) h1 * r2) +
             ((unsigned long long) h2 * r1) + ((unsigned long long) h3 * r0) +
             ((unsigned long long) h4 * s4);
        d4 = ((unsigned long long) h0 * r4) + ((unsigned long long) h1 * r3) +
             ((unsigned long long) h2 * r2) + ((unsigned long long) h3 * r1) +
             ((unsigned long long) h4 * r0);

        /* (partial) h %= p */
        c  = (unsigned long) (d0 >> 26);
        h0 = (unsigned long) d0 & 0x3ffffff;
        d1 += c;
        c  = (unsigned long) (d1 >> 26);
        h1 = (unsigned long) d1 & 0x3ffffff;
        d2 += c;
        c  = (unsigned long) (d2 >> 26);
        h2 = (unsigned long) d2 & 0x3ffffff;
        d3 += c;
        c  = (unsigned long) (d3 >> 26);
        h3 = (unsigned long) d3 & 0x3ffffff;
        d4 += c;
        c  = (unsigned long) (d4 >> 26);
        h4 = (unsigned long) d4 & 0x3ffffff;
        h0 += c * 5;
        c  = (h0 >> 26);
        h0 = h0 & 0x3ffffff;
        h1 += c;

        m += poly1305_block_size;
        bytes -= poly1305_block_size;
    }

    st->h[0] = h0;
    st->h[1] = h1;
    st->h[2] = h2;
    st->h[3] = h3;
    st->h[4] = h4;
}

static POLY1305_NOINLINE void
poly1305_finish(poly1305_state_internal_t *st, unsigned char mac[16])
{
    unsigned long      h0, h1, h2, h3, h4, c;
    unsigned long      g0, g1, g2, g3, g4;
    unsigned long long f;
    unsigned long      mask;

    /* process the remaining block */
    if (st->leftover) {
        unsigned long long i = st->leftover;

        st->buffer[i++] = 1;
        for (; i < poly1305_block_size; i++) {
            st->buffer[i] = 0;
        }
        st->final = 1;
        poly1305_blocks(st, st->buffer, poly1305_block_size);
    }

    /* fully carry h */
    h0 = st->h[0];
    h1 = st->h[1];
    h2 = st->h[2];
    h3 = st->h[3];
    h4 = st->h[4];

    c  = h1 >> 26;
    h1 = h1 & 0x3ffffff;
    h2 += c;
    c  = h2 >> 26;
    h2 = h2 & 0x3ffffff;
    h3 += c;
    c  = h3 >> 26;
    h3 = h3 & 0x3ffffff;
    h4 += c;
    c  = h4 >> 26;
    h4 = h4 & 0x3ffffff;
    h0 += c * 5;
    c  = h0 >> 26;
    h0 = h0 & 0x3ffffff;
    h1 += c;

    /* compute h + -p */
    g0 = h0 + 5;
    c  = g0 >> 26;
    g0 &= 0x3ffffff;
    g1 = h1 + c;
    c  = g1 >> 26;
    g1 &= 0x3ffffff;
    g2 = h2 + c;
    c  = g2 >> 26;
    g2 &= 0x3ffffff;
    g3 = h3 + c;
    c  = g3 >> 26;
    g3 &= 0x3ffffff;
    g4 = h4 + c - (1UL << 26);

    /* select h if h < p, or h + -p if h >= p */
    mask = (g4 >> ((sizeof(unsigned long) * 8) - 1)) - 1;
    g0 &= mask;
    g1 &= mask;
    g2 &= mask;
    g3 &= mask;
    g4 &= mask;
    mask = ~mask;

    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    /* h = h % (2^128) */
    h0 = ((h0) | (h1 << 26)) & 0xffffffff;
    h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffff;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
    h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffff;

    /* mac = (h + pad) % (2^128) */
    f  = (unsigned long long) h0 + st->pad[0];
    h0 = (unsigned long) f;
    f  = (unsigned long long) h1 + st->pad[1] + (f >> 32);
    h1 = (unsigned long) f;
    f  = (unsigned long long) h2 + st->pad[2] + (f >> 32);
    h2 = (unsigned long) f;
    f  = (unsigned long long) h3 + st->pad[3] + (f >> 32);
    h3 = (unsigned long) f;

    STORE32_LE(mac + 0, (uint32_t) h0);
    STORE32_LE(mac + 4, (uint32_t) h1);
    STORE32_LE(mac + 8, (uint32_t) h2);
    STORE32_LE(mac + 12, (uint32_t) h3);

    /* zero out the state */
    sodium_memzero((void *) st, sizeof *st);
}
