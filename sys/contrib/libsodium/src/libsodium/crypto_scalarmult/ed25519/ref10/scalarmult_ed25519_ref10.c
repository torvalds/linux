
#include <string.h>

#include "crypto_scalarmult_ed25519.h"
#include "private/ed25519_ref10.h"
#include "utils.h"

static int
_crypto_scalarmult_ed25519_is_inf(const unsigned char s[32])
{
    unsigned char c;
    unsigned int  i;

    c = s[0] ^ 0x01;
    for (i = 1; i < 31; i++) {
        c |= s[i];
    }
    c |= s[31] & 0x7f;

    return ((((unsigned int) c) - 1U) >> 8) & 1;
}

static inline void
_crypto_scalarmult_ed25519_clamp(unsigned char k[32])
{
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
}

int
crypto_scalarmult_ed25519(unsigned char *q, const unsigned char *n,
                          const unsigned char *p)
{
    unsigned char *t = q;
    ge25519_p3     Q;
    ge25519_p3     P;
    unsigned int   i;

    if (ge25519_is_canonical(p) == 0 || ge25519_has_small_order(p) != 0 ||
        ge25519_frombytes(&P, p) != 0 || ge25519_is_on_main_subgroup(&P) == 0) {
        return -1;
    }
    for (i = 0; i < 32; ++i) {
        t[i] = n[i];
    }
    _crypto_scalarmult_ed25519_clamp(t);
    ge25519_scalarmult(&Q, t, &P);
    ge25519_p3_tobytes(q, &Q);
    if (_crypto_scalarmult_ed25519_is_inf(q) != 0 || sodium_is_zero(n, 32)) {
        return -1;
    }
    return 0;
}

int
crypto_scalarmult_ed25519_base(unsigned char *q,
                               const unsigned char *n)
{
    unsigned char *t = q;
    ge25519_p3     Q;
    unsigned int   i;

    for (i = 0; i < 32; ++i) {
        t[i] = n[i];
    }
    _crypto_scalarmult_ed25519_clamp(t);
    ge25519_scalarmult_base(&Q, t);
    ge25519_p3_tobytes(q, &Q);
    if (sodium_is_zero(n, 32) != 0) {
        return -1;
    }
    return 0;
}

size_t
crypto_scalarmult_ed25519_bytes(void)
{
    return crypto_scalarmult_ed25519_BYTES;
}

size_t
crypto_scalarmult_ed25519_scalarbytes(void)
{
    return crypto_scalarmult_ed25519_SCALARBYTES;
}
