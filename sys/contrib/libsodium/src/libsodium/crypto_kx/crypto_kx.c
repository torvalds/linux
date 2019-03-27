
#include <stddef.h>

#include "core.h"
#include "crypto_generichash.h"
#include "crypto_kx.h"
#include "crypto_scalarmult.h"
#include "private/common.h"
#include "randombytes.h"
#include "utils.h"

int
crypto_kx_seed_keypair(unsigned char pk[crypto_kx_PUBLICKEYBYTES],
                       unsigned char sk[crypto_kx_SECRETKEYBYTES],
                       const unsigned char seed[crypto_kx_SEEDBYTES])
{
    crypto_generichash(sk, crypto_kx_SECRETKEYBYTES,
                       seed, crypto_kx_SEEDBYTES, NULL, 0);
    return crypto_scalarmult_base(pk, sk);
}

int
crypto_kx_keypair(unsigned char pk[crypto_kx_PUBLICKEYBYTES],
                  unsigned char sk[crypto_kx_SECRETKEYBYTES])
{
    COMPILER_ASSERT(crypto_kx_SECRETKEYBYTES == crypto_scalarmult_SCALARBYTES);
    COMPILER_ASSERT(crypto_kx_PUBLICKEYBYTES == crypto_scalarmult_BYTES);

    randombytes_buf(sk, crypto_kx_SECRETKEYBYTES);
    return crypto_scalarmult_base(pk, sk);
}

int
crypto_kx_client_session_keys(unsigned char rx[crypto_kx_SESSIONKEYBYTES],
                              unsigned char tx[crypto_kx_SESSIONKEYBYTES],
                              const unsigned char client_pk[crypto_kx_PUBLICKEYBYTES],
                              const unsigned char client_sk[crypto_kx_SECRETKEYBYTES],
                              const unsigned char server_pk[crypto_kx_PUBLICKEYBYTES])
{
    crypto_generichash_state h;
    unsigned char            q[crypto_scalarmult_BYTES];
    unsigned char            keys[2 * crypto_kx_SESSIONKEYBYTES];
    int                      i;

    if (rx == NULL) {
        rx = tx;
    }
    if (tx == NULL) {
        tx = rx;
    }
    if (rx == NULL) {
        sodium_misuse(); /* LCOV_EXCL_LINE */
    }
    if (crypto_scalarmult(q, client_sk, server_pk) != 0) {
        return -1;
    }
    COMPILER_ASSERT(sizeof keys <= crypto_generichash_BYTES_MAX);
    crypto_generichash_init(&h, NULL, 0U, sizeof keys);
    crypto_generichash_update(&h, q, crypto_scalarmult_BYTES);
    sodium_memzero(q, sizeof q);
    crypto_generichash_update(&h, client_pk, crypto_kx_PUBLICKEYBYTES);
    crypto_generichash_update(&h, server_pk, crypto_kx_PUBLICKEYBYTES);
    crypto_generichash_final(&h, keys, sizeof keys);
    sodium_memzero(&h, sizeof h);
    for (i = 0; i < crypto_kx_SESSIONKEYBYTES; i++) {
        rx[i] = keys[i];
        tx[i] = keys[i + crypto_kx_SESSIONKEYBYTES];
    }
    sodium_memzero(keys, sizeof keys);

    return 0;
}

int
crypto_kx_server_session_keys(unsigned char rx[crypto_kx_SESSIONKEYBYTES],
                              unsigned char tx[crypto_kx_SESSIONKEYBYTES],
                              const unsigned char server_pk[crypto_kx_PUBLICKEYBYTES],
                              const unsigned char server_sk[crypto_kx_SECRETKEYBYTES],
                              const unsigned char client_pk[crypto_kx_PUBLICKEYBYTES])
{
    crypto_generichash_state h;
    unsigned char            q[crypto_scalarmult_BYTES];
    unsigned char            keys[2 * crypto_kx_SESSIONKEYBYTES];
    int                      i;

    if (rx == NULL) {
        rx = tx;
    }
    if (tx == NULL) {
        tx = rx;
    }
    if (rx == NULL) {
        sodium_misuse(); /* LCOV_EXCL_LINE */
    }
    if (crypto_scalarmult(q, server_sk, client_pk) != 0) {
        return -1;
    }
    COMPILER_ASSERT(sizeof keys <= crypto_generichash_BYTES_MAX);
    crypto_generichash_init(&h, NULL, 0U, sizeof keys);
    crypto_generichash_update(&h, q, crypto_scalarmult_BYTES);
    sodium_memzero(q, sizeof q);
    crypto_generichash_update(&h, client_pk, crypto_kx_PUBLICKEYBYTES);
    crypto_generichash_update(&h, server_pk, crypto_kx_PUBLICKEYBYTES);
    crypto_generichash_final(&h, keys, sizeof keys);
    sodium_memzero(&h, sizeof h);
    for (i = 0; i < crypto_kx_SESSIONKEYBYTES; i++) {
        tx[i] = keys[i];
        rx[i] = keys[i + crypto_kx_SESSIONKEYBYTES];
    }
    sodium_memzero(keys, sizeof keys);

    return 0;
}

size_t
crypto_kx_publickeybytes(void)
{
    return crypto_kx_PUBLICKEYBYTES;
}

size_t
crypto_kx_secretkeybytes(void)
{
    return crypto_kx_SECRETKEYBYTES;
}

size_t
crypto_kx_seedbytes(void)
{
    return crypto_kx_SEEDBYTES;
}

size_t
crypto_kx_sessionkeybytes(void)
{
    return crypto_kx_SESSIONKEYBYTES;
}

const char *
crypto_kx_primitive(void)
{
    return crypto_kx_PRIMITIVE;
}
