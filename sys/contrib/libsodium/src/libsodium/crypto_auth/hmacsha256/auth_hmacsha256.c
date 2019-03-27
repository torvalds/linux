
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crypto_auth_hmacsha256.h"
#include "crypto_hash_sha256.h"
#include "crypto_verify_32.h"
#include "randombytes.h"
#include "utils.h"

size_t
crypto_auth_hmacsha256_bytes(void)
{
    return crypto_auth_hmacsha256_BYTES;
}

size_t
crypto_auth_hmacsha256_keybytes(void)
{
    return crypto_auth_hmacsha256_KEYBYTES;
}

size_t
crypto_auth_hmacsha256_statebytes(void)
{
    return sizeof(crypto_auth_hmacsha256_state);
}

void
crypto_auth_hmacsha256_keygen(unsigned char k[crypto_auth_hmacsha256_KEYBYTES])
{
    randombytes_buf(k, crypto_auth_hmacsha256_KEYBYTES);
}

int
crypto_auth_hmacsha256_init(crypto_auth_hmacsha256_state *state,
                            const unsigned char *key, size_t keylen)
{
    unsigned char pad[64];
    unsigned char khash[32];
    size_t        i;

    if (keylen > 64) {
        crypto_hash_sha256_init(&state->ictx);
        crypto_hash_sha256_update(&state->ictx, key, keylen);
        crypto_hash_sha256_final(&state->ictx, khash);
        key    = khash;
        keylen = 32;
    }
    crypto_hash_sha256_init(&state->ictx);
    memset(pad, 0x36, 64);
    for (i = 0; i < keylen; i++) {
        pad[i] ^= key[i];
    }
    crypto_hash_sha256_update(&state->ictx, pad, 64);

    crypto_hash_sha256_init(&state->octx);
    memset(pad, 0x5c, 64);
    for (i = 0; i < keylen; i++) {
        pad[i] ^= key[i];
    }
    crypto_hash_sha256_update(&state->octx, pad, 64);

    sodium_memzero((void *) pad, sizeof pad);
    sodium_memzero((void *) khash, sizeof khash);

    return 0;
}

int
crypto_auth_hmacsha256_update(crypto_auth_hmacsha256_state *state,
                              const unsigned char *in, unsigned long long inlen)
{
    crypto_hash_sha256_update(&state->ictx, in, inlen);

    return 0;
}

int
crypto_auth_hmacsha256_final(crypto_auth_hmacsha256_state *state,
                             unsigned char                *out)
{
    unsigned char ihash[32];

    crypto_hash_sha256_final(&state->ictx, ihash);
    crypto_hash_sha256_update(&state->octx, ihash, 32);
    crypto_hash_sha256_final(&state->octx, out);

    sodium_memzero((void *) ihash, sizeof ihash);

    return 0;
}

int
crypto_auth_hmacsha256(unsigned char *out, const unsigned char *in,
                       unsigned long long inlen, const unsigned char *k)
{
    crypto_auth_hmacsha256_state state;

    crypto_auth_hmacsha256_init(&state, k, crypto_auth_hmacsha256_KEYBYTES);
    crypto_auth_hmacsha256_update(&state, in, inlen);
    crypto_auth_hmacsha256_final(&state, out);

    return 0;
}

int
crypto_auth_hmacsha256_verify(const unsigned char *h, const unsigned char *in,
                              unsigned long long inlen, const unsigned char *k)
{
    unsigned char correct[32];

    crypto_auth_hmacsha256(correct, in, inlen, k);

    return crypto_verify_32(h, correct) | (-(h == correct)) |
           sodium_memcmp(correct, h, 32);
}
