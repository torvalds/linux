
#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include "blake2.h"
#include "crypto_generichash_blake2b.h"
#include "private/implementations.h"

int
crypto_generichash_blake2b(unsigned char *out, size_t outlen,
                           const unsigned char *in, unsigned long long inlen,
                           const unsigned char *key, size_t keylen)
{
    if (outlen <= 0U || outlen > BLAKE2B_OUTBYTES ||
        keylen > BLAKE2B_KEYBYTES || inlen > UINT64_MAX) {
        return -1;
    }
    assert(outlen <= UINT8_MAX);
    assert(keylen <= UINT8_MAX);

    return blake2b((uint8_t *) out, in, key, (uint8_t) outlen, (uint64_t) inlen,
                   (uint8_t) keylen);
}

int
crypto_generichash_blake2b_salt_personal(
    unsigned char *out, size_t outlen, const unsigned char *in,
    unsigned long long inlen, const unsigned char *key, size_t keylen,
    const unsigned char *salt, const unsigned char *personal)
{
    if (outlen <= 0U || outlen > BLAKE2B_OUTBYTES ||
        keylen > BLAKE2B_KEYBYTES || inlen > UINT64_MAX) {
        return -1;
    }
    assert(outlen <= UINT8_MAX);
    assert(keylen <= UINT8_MAX);

    return blake2b_salt_personal((uint8_t *) out, in, key, (uint8_t) outlen,
                                 (uint64_t) inlen, (uint8_t) keylen, salt,
                                 personal);
}

int
crypto_generichash_blake2b_init(crypto_generichash_blake2b_state *state,
                                const unsigned char *key, const size_t keylen,
                                const size_t outlen)
{
    if (outlen <= 0U || outlen > BLAKE2B_OUTBYTES ||
        keylen > BLAKE2B_KEYBYTES) {
        return -1;
    }
    assert(outlen <= UINT8_MAX);
    assert(keylen <= UINT8_MAX);
    if (key == NULL || keylen <= 0U) {
        if (blake2b_init(state, (uint8_t) outlen) != 0) {
            return -1; /* LCOV_EXCL_LINE */
        }
    } else if (blake2b_init_key(state, (uint8_t) outlen, key,
                                (uint8_t) keylen) != 0) {
        return -1; /* LCOV_EXCL_LINE */
    }
    return 0;
}

int
crypto_generichash_blake2b_init_salt_personal(
    crypto_generichash_blake2b_state *state, const unsigned char *key,
    const size_t keylen, const size_t outlen, const unsigned char *salt,
    const unsigned char *personal)
{
    if (outlen <= 0U || outlen > BLAKE2B_OUTBYTES ||
        keylen > BLAKE2B_KEYBYTES) {
        return -1;
    }
    assert(outlen <= UINT8_MAX);
    assert(keylen <= UINT8_MAX);
    if (key == NULL || keylen <= 0U) {
        if (blake2b_init_salt_personal(state, (uint8_t) outlen, salt,
                                       personal) != 0) {
            return -1; /* LCOV_EXCL_LINE */
        }
    } else if (blake2b_init_key_salt_personal(state, (uint8_t) outlen, key,
                                              (uint8_t) keylen, salt,
                                              personal) != 0) {
        return -1; /* LCOV_EXCL_LINE */
    }
    return 0;
}

int
crypto_generichash_blake2b_update(crypto_generichash_blake2b_state *state,
                                  const unsigned char *in,
                                  unsigned long long inlen)
{
    return blake2b_update(state, (const uint8_t *) in, (uint64_t) inlen);
}

int
crypto_generichash_blake2b_final(crypto_generichash_blake2b_state *state,
                                 unsigned char *out, const size_t outlen)
{
    assert(outlen <= UINT8_MAX);
    return blake2b_final(state, (uint8_t *) out, (uint8_t) outlen);
}

int
_crypto_generichash_blake2b_pick_best_implementation(void)
{
    return blake2b_pick_best_implementation();
}
