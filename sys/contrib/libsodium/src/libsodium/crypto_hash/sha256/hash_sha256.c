#include "crypto_hash_sha256.h"

size_t
crypto_hash_sha256_bytes(void)
{
    return crypto_hash_sha256_BYTES;
}

size_t
crypto_hash_sha256_statebytes(void)
{
    return sizeof(crypto_hash_sha256_state);
}
