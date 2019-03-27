#ifndef crypto_pwhash_argon2id_H
#define crypto_pwhash_argon2id_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_pwhash_argon2id_ALG_ARGON2ID13 2
SODIUM_EXPORT
int crypto_pwhash_argon2id_alg_argon2id13(void);

#define crypto_pwhash_argon2id_BYTES_MIN 16U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_bytes_min(void);

#define crypto_pwhash_argon2id_BYTES_MAX SODIUM_MIN(SODIUM_SIZE_MAX, 4294967295U)
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_bytes_max(void);

#define crypto_pwhash_argon2id_PASSWD_MIN 0U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_passwd_min(void);

#define crypto_pwhash_argon2id_PASSWD_MAX 4294967295U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_passwd_max(void);

#define crypto_pwhash_argon2id_SALTBYTES 16U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_saltbytes(void);

#define crypto_pwhash_argon2id_STRBYTES 128U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_strbytes(void);

#define crypto_pwhash_argon2id_STRPREFIX "$argon2id$"
SODIUM_EXPORT
const char *crypto_pwhash_argon2id_strprefix(void);

#define crypto_pwhash_argon2id_OPSLIMIT_MIN 1U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_opslimit_min(void);

#define crypto_pwhash_argon2id_OPSLIMIT_MAX 4294967295U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_opslimit_max(void);

#define crypto_pwhash_argon2id_MEMLIMIT_MIN 8192U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_memlimit_min(void);

#define crypto_pwhash_argon2id_MEMLIMIT_MAX \
    ((SIZE_MAX >= 4398046510080U) ? 4398046510080U : (SIZE_MAX >= 2147483648U) ? 2147483648U : 32768U)
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_memlimit_max(void);

#define crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE 2U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_opslimit_interactive(void);

#define crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE 67108864U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_memlimit_interactive(void);

#define crypto_pwhash_argon2id_OPSLIMIT_MODERATE 3U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_opslimit_moderate(void);

#define crypto_pwhash_argon2id_MEMLIMIT_MODERATE 268435456U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_memlimit_moderate(void);

#define crypto_pwhash_argon2id_OPSLIMIT_SENSITIVE 4U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_opslimit_sensitive(void);

#define crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE 1073741824U
SODIUM_EXPORT
size_t crypto_pwhash_argon2id_memlimit_sensitive(void);

SODIUM_EXPORT
int crypto_pwhash_argon2id(unsigned char * const out,
                           unsigned long long outlen,
                           const char * const passwd,
                           unsigned long long passwdlen,
                           const unsigned char * const salt,
                           unsigned long long opslimit, size_t memlimit,
                           int alg)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_pwhash_argon2id_str(char out[crypto_pwhash_argon2id_STRBYTES],
                               const char * const passwd,
                               unsigned long long passwdlen,
                               unsigned long long opslimit, size_t memlimit)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_pwhash_argon2id_str_verify(const char str[crypto_pwhash_argon2id_STRBYTES],
                                      const char * const passwd,
                                      unsigned long long passwdlen)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_pwhash_argon2id_str_needs_rehash(const char str[crypto_pwhash_argon2id_STRBYTES],
                                            unsigned long long opslimit, size_t memlimit)
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif
