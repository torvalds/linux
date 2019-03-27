
#include "crypto_hash.h"

size_t
crypto_hash_bytes(void)
{
    return crypto_hash_BYTES;
}

int
crypto_hash(unsigned char *out, const unsigned char *in,
            unsigned long long inlen)
{
    return crypto_hash_sha512(out, in, inlen);
}

const char *
crypto_hash_primitive(void) {
    return crypto_hash_PRIMITIVE;
}
