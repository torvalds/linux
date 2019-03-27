
#include "crypto_secretbox.h"
#include "randombytes.h"

size_t
crypto_secretbox_keybytes(void)
{
    return crypto_secretbox_KEYBYTES;
}

size_t
crypto_secretbox_noncebytes(void)
{
    return crypto_secretbox_NONCEBYTES;
}

size_t
crypto_secretbox_zerobytes(void)
{
    return crypto_secretbox_ZEROBYTES;
}

size_t
crypto_secretbox_boxzerobytes(void)
{
    return crypto_secretbox_BOXZEROBYTES;
}

size_t
crypto_secretbox_macbytes(void)
{
    return crypto_secretbox_MACBYTES;
}

size_t
crypto_secretbox_messagebytes_max(void)
{
    return crypto_secretbox_MESSAGEBYTES_MAX;
}

const char *
crypto_secretbox_primitive(void)
{
    return crypto_secretbox_PRIMITIVE;
}

int
crypto_secretbox(unsigned char *c, const unsigned char *m,
                 unsigned long long mlen, const unsigned char *n,
                 const unsigned char *k)
{
    return crypto_secretbox_xsalsa20poly1305(c, m, mlen, n, k);
}

int
crypto_secretbox_open(unsigned char *m, const unsigned char *c,
                      unsigned long long clen, const unsigned char *n,
                      const unsigned char *k)
{
    return crypto_secretbox_xsalsa20poly1305_open(m, c, clen, n, k);
}

void
crypto_secretbox_keygen(unsigned char k[crypto_secretbox_KEYBYTES])
{
    randombytes_buf(k, crypto_secretbox_KEYBYTES);
}
