
#include "crypto_onetimeauth.h"
#include "randombytes.h"

size_t
crypto_onetimeauth_statebytes(void)
{
    return sizeof(crypto_onetimeauth_state);
}

size_t
crypto_onetimeauth_bytes(void)
{
    return crypto_onetimeauth_BYTES;
}

size_t
crypto_onetimeauth_keybytes(void)
{
    return crypto_onetimeauth_KEYBYTES;
}

int
crypto_onetimeauth(unsigned char *out, const unsigned char *in,
                   unsigned long long inlen, const unsigned char *k)
{
    return crypto_onetimeauth_poly1305(out, in, inlen, k);
}

int
crypto_onetimeauth_verify(const unsigned char *h, const unsigned char *in,
                          unsigned long long inlen, const unsigned char *k)
{
    return crypto_onetimeauth_poly1305_verify(h, in, inlen, k);
}

int
crypto_onetimeauth_init(crypto_onetimeauth_state *state,
                        const unsigned char *key)
{
    return crypto_onetimeauth_poly1305_init
        ((crypto_onetimeauth_poly1305_state *) state, key);
}

int
crypto_onetimeauth_update(crypto_onetimeauth_state *state,
                          const unsigned char *in,
                          unsigned long long inlen)
{
    return crypto_onetimeauth_poly1305_update
        ((crypto_onetimeauth_poly1305_state *) state, in, inlen);
}

int
crypto_onetimeauth_final(crypto_onetimeauth_state *state,
                         unsigned char *out)
{
    return crypto_onetimeauth_poly1305_final
        ((crypto_onetimeauth_poly1305_state *) state, out);
}

const char *
crypto_onetimeauth_primitive(void)
{
    return crypto_onetimeauth_PRIMITIVE;
}

void crypto_onetimeauth_keygen(unsigned char k[crypto_onetimeauth_KEYBYTES])
{
    randombytes_buf(k, crypto_onetimeauth_KEYBYTES);
}
