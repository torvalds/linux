
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crypto_auth_hmacsha512.h"
#include "crypto_hash_sha512.h"
#include "crypto_verify_64.h"
#include "randombytes.h"
#include "utils.h"

size_t
crypto_auth_hmacsha512_bytes(void)
{
    return crypto_auth_hmacsha512_BYTES;
}

size_t
crypto_auth_hmacsha512_keybytes(void)
{
    return crypto_auth_hmacsha512_KEYBYTES;
}

size_t
crypto_auth_hmacsha512_statebytes(void)
{
    return sizeof(crypto_auth_hmacsha512_state);
}

void
crypto_auth_hmacsha512_keygen(unsigned char k[crypto_auth_hmacsha512_KEYBYTES])
{
    randombytes_buf(k, crypto_auth_hmacsha512_KEYBYTES);
}

int
crypto_auth_hmacsha512_init(crypto_auth_hmacsha512_state *state,
                            const unsigned char *key, size_t keylen)
{
    unsigned char pad[128];
    unsigned char khash[64];
    size_t        i;

    if (keylen > 128) {
        crypto_hash_sha512_init(&state->ictx);
        crypto_hash_sha512_update(&state->ictx, key, keylen);
        crypto_hash_sha512_final(&state->ictx, khash);
        key    = khash;
        keylen = 64;
    }
    crypto_hash_sha512_init(&state->ictx);
    memset(pad, 0x36, 128);
    for (i = 0; i < keylen; i++) {
        pad[i] ^= key[i];
    }
    crypto_hash_sha512_update(&state->ictx, pad, 128);

    crypto_hash_sha512_init(&state->octx);
    memset(pad, 0x5c, 128);
    for (i = 0; i < keylen; i++) {
        pad[i] ^= key[i];
    }
    crypto_hash_sha512_update(&state->octx, pad, 128);

    sodium_memzero((void *) pad, sizeof pad);
    sodium_memzero((void *) khash, sizeof khash);

    return 0;
}

int
crypto_auth_hmacsha512_update(crypto_auth_hmacsha512_state *state,
                              const unsigned char *in, unsigned long long inlen)
{
    crypto_hash_sha512_update(&state->ictx, in, inlen);

    return 0;
}

int
crypto_auth_hmacsha512_final(crypto_auth_hmacsha512_state *state,
                             unsigned char                *out)
{
    unsigned char ihash[64];

    crypto_hash_sha512_final(&state->ictx, ihash);
    crypto_hash_sha512_update(&state->octx, ihash, 64);
    crypto_hash_sha512_final(&state->octx, out);

    sodium_memzero((void *) ihash, sizeof ihash);

    return 0;
}

int
crypto_auth_hmacsha512(unsigned char *out, const unsigned char *in,
                       unsigned long long inlen, const unsigned char *k)
{
    crypto_auth_hmacsha512_state state;

    crypto_auth_hmacsha512_init(&state, k, crypto_auth_hmacsha512_KEYBYTES);
    crypto_auth_hmacsha512_update(&state, in, inlen);
    crypto_auth_hmacsha512_final(&state, out);

    return 0;
}

int
crypto_auth_hmacsha512_verify(const unsigned char *h, const unsigned char *in,
                              unsigned long long inlen, const unsigned char *k)
{
    unsigned char correct[64];

    crypto_auth_hmacsha512(correct, in, inlen, k);

    return crypto_verify_64(h, correct) | (-(h == correct)) |
           sodium_memcmp(correct, h, 64);
}
