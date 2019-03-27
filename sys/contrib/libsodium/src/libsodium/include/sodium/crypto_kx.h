#ifndef crypto_kx_H
#define crypto_kx_H

#include <stddef.h>

#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_kx_PUBLICKEYBYTES 32
SODIUM_EXPORT
size_t crypto_kx_publickeybytes(void);

#define crypto_kx_SECRETKEYBYTES 32
SODIUM_EXPORT
size_t crypto_kx_secretkeybytes(void);

#define crypto_kx_SEEDBYTES 32
SODIUM_EXPORT
size_t crypto_kx_seedbytes(void);

#define crypto_kx_SESSIONKEYBYTES 32
SODIUM_EXPORT
size_t crypto_kx_sessionkeybytes(void);

#define crypto_kx_PRIMITIVE "x25519blake2b"
SODIUM_EXPORT
const char *crypto_kx_primitive(void);

SODIUM_EXPORT
int crypto_kx_seed_keypair(unsigned char pk[crypto_kx_PUBLICKEYBYTES],
                           unsigned char sk[crypto_kx_SECRETKEYBYTES],
                           const unsigned char seed[crypto_kx_SEEDBYTES]);

SODIUM_EXPORT
int crypto_kx_keypair(unsigned char pk[crypto_kx_PUBLICKEYBYTES],
                      unsigned char sk[crypto_kx_SECRETKEYBYTES]);

SODIUM_EXPORT
int crypto_kx_client_session_keys(unsigned char rx[crypto_kx_SESSIONKEYBYTES],
                                  unsigned char tx[crypto_kx_SESSIONKEYBYTES],
                                  const unsigned char client_pk[crypto_kx_PUBLICKEYBYTES],
                                  const unsigned char client_sk[crypto_kx_SECRETKEYBYTES],
                                  const unsigned char server_pk[crypto_kx_PUBLICKEYBYTES])
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_kx_server_session_keys(unsigned char rx[crypto_kx_SESSIONKEYBYTES],
                                  unsigned char tx[crypto_kx_SESSIONKEYBYTES],
                                  const unsigned char server_pk[crypto_kx_PUBLICKEYBYTES],
                                  const unsigned char server_sk[crypto_kx_SECRETKEYBYTES],
                                  const unsigned char client_pk[crypto_kx_PUBLICKEYBYTES])
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif
