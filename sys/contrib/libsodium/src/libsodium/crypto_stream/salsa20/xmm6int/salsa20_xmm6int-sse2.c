
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_stream_salsa20.h"
#include "private/common.h"
#include "private/sse2_64_32.h"
#include "utils.h"

#ifdef HAVE_EMMINTRIN_H

# ifdef __GNUC__
#  pragma GCC target("sse2")
# endif
# include <emmintrin.h>

# include "../stream_salsa20.h"
# include "salsa20_xmm6int-sse2.h"

# define ROUNDS 20

typedef struct salsa_ctx {
    uint32_t input[16];
} salsa_ctx;

static const int TR[16] = {
    0, 5, 10, 15, 12, 1, 6, 11, 8, 13, 2, 7, 4, 9, 14, 3
};

static void
salsa_keysetup(salsa_ctx *ctx, const uint8_t *k)
{
    ctx->input[TR[1]]  = LOAD32_LE(k + 0);
    ctx->input[TR[2]]  = LOAD32_LE(k + 4);
    ctx->input[TR[3]]  = LOAD32_LE(k + 8);
    ctx->input[TR[4]]  = LOAD32_LE(k + 12);
    ctx->input[TR[11]] = LOAD32_LE(k + 16);
    ctx->input[TR[12]] = LOAD32_LE(k + 20);
    ctx->input[TR[13]] = LOAD32_LE(k + 24);
    ctx->input[TR[14]] = LOAD32_LE(k + 28);
    ctx->input[TR[0]]  = 0x61707865;
    ctx->input[TR[5]]  = 0x3320646e;
    ctx->input[TR[10]] = 0x79622d32;
    ctx->input[TR[15]] = 0x6b206574;
}

static void
salsa_ivsetup(salsa_ctx *ctx, const uint8_t *iv, const uint8_t *counter)
{
    ctx->input[TR[6]] = LOAD32_LE(iv + 0);
    ctx->input[TR[7]] = LOAD32_LE(iv + 4);
    ctx->input[TR[8]] = counter == NULL ? 0 : LOAD32_LE(counter + 0);
    ctx->input[TR[9]] = counter == NULL ? 0 : LOAD32_LE(counter + 4);
}

static void
salsa20_encrypt_bytes(salsa_ctx *ctx, const uint8_t *m, uint8_t *c,
                      unsigned long long bytes)
{
    uint32_t * const x = &ctx->input[0];

    if (!bytes) {
        return; /* LCOV_EXCL_LINE */
    }

#include "u4.h"
#include "u1.h"
#include "u0.h"
}

static int
stream_sse2(unsigned char *c, unsigned long long clen, const unsigned char *n,
            const unsigned char *k)
{
    struct salsa_ctx ctx;

    if (!clen) {
        return 0;
    }
    COMPILER_ASSERT(crypto_stream_salsa20_KEYBYTES == 256 / 8);
    salsa_keysetup(&ctx, k);
    salsa_ivsetup(&ctx, n, NULL);
    memset(c, 0, clen);
    salsa20_encrypt_bytes(&ctx, c, c, clen);
    sodium_memzero(&ctx, sizeof ctx);

    return 0;
}

static int
stream_sse2_xor_ic(unsigned char *c, const unsigned char *m,
                   unsigned long long mlen, const unsigned char *n, uint64_t ic,
                   const unsigned char *k)
{
    struct salsa_ctx ctx;
    uint8_t          ic_bytes[8];
    uint32_t         ic_high;
    uint32_t         ic_low;

    if (!mlen) {
        return 0;
    }
    ic_high = (uint32_t) (ic >> 32);
    ic_low  = (uint32_t) (ic);
    STORE32_LE(&ic_bytes[0], ic_low);
    STORE32_LE(&ic_bytes[4], ic_high);
    salsa_keysetup(&ctx, k);
    salsa_ivsetup(&ctx, n, ic_bytes);
    salsa20_encrypt_bytes(&ctx, m, c, mlen);
    sodium_memzero(&ctx, sizeof ctx);

    return 0;
}

struct crypto_stream_salsa20_implementation
    crypto_stream_salsa20_xmm6int_sse2_implementation = {
        SODIUM_C99(.stream =) stream_sse2,
        SODIUM_C99(.stream_xor_ic =) stream_sse2_xor_ic
    };

#endif
