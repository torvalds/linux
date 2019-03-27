
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "argon2-core.h"
#include "argon2-encoding.h"
#include "argon2.h"
#include "crypto_pwhash.h"
#include "crypto_pwhash_argon2i.h"
#include "crypto_pwhash_argon2id.h"
#include "private/common.h"
#include "randombytes.h"
#include "utils.h"

#define STR_HASHBYTES 32U

int
crypto_pwhash_argon2i_alg_argon2i13(void)
{
    return crypto_pwhash_argon2i_ALG_ARGON2I13;
}

size_t
crypto_pwhash_argon2i_bytes_min(void)
{
    COMPILER_ASSERT(crypto_pwhash_argon2i_BYTES_MIN >= ARGON2_MIN_OUTLEN);
    return crypto_pwhash_argon2i_BYTES_MIN;
}

size_t
crypto_pwhash_argon2i_bytes_max(void)
{
    COMPILER_ASSERT(crypto_pwhash_argon2i_BYTES_MAX <= ARGON2_MAX_OUTLEN);
    return crypto_pwhash_argon2i_BYTES_MAX;
}

size_t
crypto_pwhash_argon2i_passwd_min(void)
{
    COMPILER_ASSERT(crypto_pwhash_argon2i_PASSWD_MIN >= ARGON2_MIN_PWD_LENGTH);
    return crypto_pwhash_argon2i_PASSWD_MIN;
}

size_t
crypto_pwhash_argon2i_passwd_max(void)
{
    COMPILER_ASSERT(crypto_pwhash_argon2i_PASSWD_MAX <= ARGON2_MAX_PWD_LENGTH);
    return crypto_pwhash_argon2i_PASSWD_MAX;
}

size_t
crypto_pwhash_argon2i_saltbytes(void)
{
    COMPILER_ASSERT(crypto_pwhash_argon2i_SALTBYTES >= ARGON2_MIN_SALT_LENGTH);
    COMPILER_ASSERT(crypto_pwhash_argon2i_SALTBYTES <= ARGON2_MAX_SALT_LENGTH);
    return crypto_pwhash_argon2i_SALTBYTES;
}

size_t
crypto_pwhash_argon2i_strbytes(void)
{
    return crypto_pwhash_argon2i_STRBYTES;
}

const char*
crypto_pwhash_argon2i_strprefix(void)
{
    return crypto_pwhash_argon2i_STRPREFIX;
}

size_t
crypto_pwhash_argon2i_opslimit_min(void)
{
    COMPILER_ASSERT(crypto_pwhash_argon2i_OPSLIMIT_MIN >= ARGON2_MIN_TIME);
    return crypto_pwhash_argon2i_OPSLIMIT_MIN;
}

size_t
crypto_pwhash_argon2i_opslimit_max(void)
{
    COMPILER_ASSERT(crypto_pwhash_argon2i_OPSLIMIT_MAX <= ARGON2_MAX_TIME);
    return crypto_pwhash_argon2i_OPSLIMIT_MAX;
}

size_t
crypto_pwhash_argon2i_memlimit_min(void)
{
    COMPILER_ASSERT((crypto_pwhash_argon2i_MEMLIMIT_MIN / 1024U) >= ARGON2_MIN_MEMORY);
    return crypto_pwhash_argon2i_MEMLIMIT_MIN;
}

size_t
crypto_pwhash_argon2i_memlimit_max(void)
{
    COMPILER_ASSERT((crypto_pwhash_argon2i_MEMLIMIT_MAX / 1024U) <= ARGON2_MAX_MEMORY);
    return crypto_pwhash_argon2i_MEMLIMIT_MAX;
}

size_t
crypto_pwhash_argon2i_opslimit_interactive(void)
{
    return crypto_pwhash_argon2i_OPSLIMIT_INTERACTIVE;
}

size_t
crypto_pwhash_argon2i_memlimit_interactive(void)
{
    return crypto_pwhash_argon2i_MEMLIMIT_INTERACTIVE;
}

size_t
crypto_pwhash_argon2i_opslimit_moderate(void)
{
    return crypto_pwhash_argon2i_OPSLIMIT_MODERATE;
}

size_t
crypto_pwhash_argon2i_memlimit_moderate(void)
{
    return crypto_pwhash_argon2i_MEMLIMIT_MODERATE;
}

size_t
crypto_pwhash_argon2i_opslimit_sensitive(void)
{
    return crypto_pwhash_argon2i_OPSLIMIT_SENSITIVE;
}

size_t
crypto_pwhash_argon2i_memlimit_sensitive(void)
{
    return crypto_pwhash_argon2i_MEMLIMIT_SENSITIVE;
}

int
crypto_pwhash_argon2i(unsigned char *const out, unsigned long long outlen,
                      const char *const passwd, unsigned long long passwdlen,
                      const unsigned char *const salt,
                      unsigned long long opslimit, size_t memlimit, int alg)
{
    memset(out, 0, outlen);
    if (outlen > crypto_pwhash_argon2i_BYTES_MAX) {
        errno = EFBIG;
        return -1;
    }
    if (outlen < crypto_pwhash_argon2i_BYTES_MIN) {
        errno = EINVAL;
        return -1;
    }
    if (passwdlen > crypto_pwhash_argon2i_PASSWD_MAX ||
        opslimit > crypto_pwhash_argon2i_OPSLIMIT_MAX ||
        memlimit > crypto_pwhash_argon2i_MEMLIMIT_MAX) {
        errno = EFBIG;
        return -1;
    }
    if (passwdlen < crypto_pwhash_argon2i_PASSWD_MIN ||
        opslimit < crypto_pwhash_argon2i_OPSLIMIT_MIN ||
        memlimit < crypto_pwhash_argon2i_MEMLIMIT_MIN) {
        errno = EINVAL;
        return -1;
    }
    switch (alg) {
    case crypto_pwhash_argon2i_ALG_ARGON2I13:
        if (argon2i_hash_raw((uint32_t) opslimit, (uint32_t) (memlimit / 1024U),
                             (uint32_t) 1U, passwd, (size_t) passwdlen, salt,
                             (size_t) crypto_pwhash_argon2i_SALTBYTES, out,
                             (size_t) outlen) != ARGON2_OK) {
            return -1; /* LCOV_EXCL_LINE */
        }
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int
crypto_pwhash_argon2i_str(char out[crypto_pwhash_argon2i_STRBYTES],
                          const char *const passwd,
                          unsigned long long passwdlen,
                          unsigned long long opslimit, size_t memlimit)
{
    unsigned char salt[crypto_pwhash_argon2i_SALTBYTES];

    memset(out, 0, crypto_pwhash_argon2i_STRBYTES);
    if (passwdlen > crypto_pwhash_argon2i_PASSWD_MAX ||
        opslimit > crypto_pwhash_argon2i_OPSLIMIT_MAX ||
        memlimit > crypto_pwhash_argon2i_MEMLIMIT_MAX) {
        errno = EFBIG;
        return -1;
    }
    if (passwdlen < crypto_pwhash_argon2i_PASSWD_MIN ||
        opslimit < crypto_pwhash_argon2i_OPSLIMIT_MIN ||
        memlimit < crypto_pwhash_argon2i_MEMLIMIT_MIN) {
        errno = EINVAL;
        return -1;
    }
    randombytes_buf(salt, sizeof salt);
    if (argon2i_hash_encoded((uint32_t) opslimit, (uint32_t) (memlimit / 1024U),
                             (uint32_t) 1U, passwd, (size_t) passwdlen, salt,
                             sizeof salt, STR_HASHBYTES, out,
                             crypto_pwhash_argon2i_STRBYTES) != ARGON2_OK) {
        return -1; /* LCOV_EXCL_LINE */
    }
    return 0;
}

int
crypto_pwhash_argon2i_str_verify(const char str[crypto_pwhash_argon2i_STRBYTES],
                                 const char *const  passwd,
                                 unsigned long long passwdlen)
{
    int verify_ret;

    if (passwdlen > crypto_pwhash_argon2i_PASSWD_MAX) {
        errno = EFBIG;
        return -1;
    }
    /* LCOV_EXCL_START */
    if (passwdlen < crypto_pwhash_argon2i_PASSWD_MIN) {
        errno = EINVAL;
        return -1;
    }
    /* LCOV_EXCL_STOP */

    verify_ret = argon2i_verify(str, passwd, (size_t) passwdlen);
    if (verify_ret == ARGON2_OK) {
        return 0;
    }
    if (verify_ret == ARGON2_VERIFY_MISMATCH) {
        errno = EINVAL;
    }
    return -1;
}

static int
_needs_rehash(const char *str, unsigned long long opslimit, size_t memlimit,
              argon2_type type)
{
    unsigned char  *fodder;
    argon2_context  ctx;
    size_t          fodder_len;
    int             ret = -1;

    fodder_len = strlen(str);
    memlimit /= 1024U;
    if (opslimit > UINT32_MAX || memlimit > UINT32_MAX ||
        fodder_len >= crypto_pwhash_STRBYTES) {
        errno = EINVAL;
        return -1;
    }
    memset(&ctx, 0, sizeof ctx);
    if ((fodder = (unsigned char *) calloc(fodder_len, 1U)) == NULL) {
        return -1; /* LCOV_EXCL_LINE */
    }
    ctx.out    = ctx.pwd       = ctx.salt    = fodder;
    ctx.outlen = ctx.pwdlen    = ctx.saltlen = (uint32_t) fodder_len;
    ctx.ad     = ctx.secret    = NULL;
    ctx.adlen  = ctx.secretlen = 0U;
    if (decode_string(&ctx, str, type) != 0) {
        errno = EINVAL;
        ret = -1;
    } else if (ctx.t_cost != (uint32_t) opslimit ||
               ctx.m_cost != (uint32_t) memlimit) {
        ret = 1;
    } else {
        ret = 0;
    }
    free(fodder);

    return ret;
}

int
crypto_pwhash_argon2i_str_needs_rehash(const char str[crypto_pwhash_argon2i_STRBYTES],
                                       unsigned long long opslimit, size_t memlimit)
{
    return _needs_rehash(str, opslimit, memlimit, Argon2_i);
}

int
crypto_pwhash_argon2id_str_needs_rehash(const char str[crypto_pwhash_argon2id_STRBYTES],
                                        unsigned long long opslimit, size_t memlimit)
{
    return _needs_rehash(str, opslimit, memlimit, Argon2_id);
}
