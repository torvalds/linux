#include "crypto_generichash_blake2b.h"
#include "randombytes.h"

size_t
crypto_generichash_blake2b_bytes_min(void) {
    return crypto_generichash_blake2b_BYTES_MIN;
}

size_t
crypto_generichash_blake2b_bytes_max(void) {
    return crypto_generichash_blake2b_BYTES_MAX;
}

size_t
crypto_generichash_blake2b_bytes(void) {
    return crypto_generichash_blake2b_BYTES;
}

size_t
crypto_generichash_blake2b_keybytes_min(void) {
    return crypto_generichash_blake2b_KEYBYTES_MIN;
}

size_t
crypto_generichash_blake2b_keybytes_max(void) {
    return crypto_generichash_blake2b_KEYBYTES_MAX;
}

size_t
crypto_generichash_blake2b_keybytes(void) {
    return crypto_generichash_blake2b_KEYBYTES;
}

size_t
crypto_generichash_blake2b_saltbytes(void) {
    return crypto_generichash_blake2b_SALTBYTES;
}

size_t
crypto_generichash_blake2b_personalbytes(void) {
    return crypto_generichash_blake2b_PERSONALBYTES;
}

size_t
crypto_generichash_blake2b_statebytes(void)
{
    return (sizeof(crypto_generichash_blake2b_state) + (size_t) 63U)
        & ~(size_t) 63U;
}

void
crypto_generichash_blake2b_keygen(unsigned char k[crypto_generichash_blake2b_KEYBYTES])
{
    randombytes_buf(k, crypto_generichash_blake2b_KEYBYTES);
}
