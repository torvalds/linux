#include <errno.h>

#include "crypto_kdf_blake2b.h"
#include "crypto_generichash_blake2b.h"
#include "private/common.h"

size_t
crypto_kdf_blake2b_bytes_min(void)
{
    return crypto_kdf_blake2b_BYTES_MIN;
}

size_t
crypto_kdf_blake2b_bytes_max(void)
{
    return crypto_kdf_blake2b_BYTES_MAX;
}

size_t
crypto_kdf_blake2b_contextbytes(void)
{
    return crypto_kdf_blake2b_CONTEXTBYTES;
}

size_t
crypto_kdf_blake2b_keybytes(void)
{
    return crypto_kdf_blake2b_KEYBYTES;
}

int crypto_kdf_blake2b_derive_from_key(unsigned char *subkey, size_t subkey_len,
                                       uint64_t subkey_id,
                                       const char ctx[crypto_kdf_blake2b_CONTEXTBYTES],
                                       const unsigned char key[crypto_kdf_blake2b_KEYBYTES])
{
    unsigned char ctx_padded[crypto_generichash_blake2b_PERSONALBYTES];
    unsigned char salt[crypto_generichash_blake2b_SALTBYTES];

    memcpy(ctx_padded, ctx, crypto_kdf_blake2b_CONTEXTBYTES);
    memset(ctx_padded + crypto_kdf_blake2b_CONTEXTBYTES, 0, sizeof ctx_padded - crypto_kdf_blake2b_CONTEXTBYTES);
    STORE64_LE(salt, subkey_id);
    memset(salt + 8, 0, (sizeof salt) - 8);
    if (subkey_len < crypto_kdf_blake2b_BYTES_MIN ||
        subkey_len > crypto_kdf_blake2b_BYTES_MAX) {
        errno = EINVAL;
        return -1;
    }
    return crypto_generichash_blake2b_salt_personal(subkey, subkey_len,
                                                    NULL, 0,
                                                    key, crypto_kdf_blake2b_KEYBYTES,
                                                    salt, ctx_padded);
}
