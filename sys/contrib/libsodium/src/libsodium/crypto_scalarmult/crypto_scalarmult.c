
#include "crypto_scalarmult.h"

const char *
crypto_scalarmult_primitive(void)
{
    return crypto_scalarmult_PRIMITIVE;
}

int
crypto_scalarmult_base(unsigned char *q, const unsigned char *n)
{
    return crypto_scalarmult_curve25519_base(q, n);
}

int
crypto_scalarmult(unsigned char *q, const unsigned char *n,
                  const unsigned char *p)
{
    return crypto_scalarmult_curve25519(q, n, p);
}

size_t
crypto_scalarmult_bytes(void)
{
    return crypto_scalarmult_BYTES;
}

size_t
crypto_scalarmult_scalarbytes(void)
{
    return crypto_scalarmult_SCALARBYTES;
}
