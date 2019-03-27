#include "crypto_hash_sha512.h"

size_t
crypto_hash_sha512_bytes(void)
{
    return crypto_hash_sha512_BYTES;
}

size_t
crypto_hash_sha512_statebytes(void)
{
    return sizeof(crypto_hash_sha512_state);
}
