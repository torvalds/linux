#define TEST_NAME "scalarmult_ed25519"
#include "cmptest.h"

static const unsigned char non_canonical_p[32] = {
    0xf6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
};
static const unsigned char non_canonical_invalid_p[32] = {
    0xf5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
};
static const unsigned char max_canonical_p[32] = {
    0xe4, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
};

static const unsigned char B[32] = {
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
};

int
main(void)
{
    unsigned char *n, *p, *q, *q2;

    n = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_SCALARBYTES);
    p = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_BYTES);
    q = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_BYTES);
    q2 = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_BYTES);

    randombytes_buf(n, crypto_scalarmult_ed25519_SCALARBYTES);
    if (crypto_scalarmult_ed25519_base(q, n) != 0) {
        printf("crypto_scalarmult_ed25519_base() failed\n");
    }
    memcpy(p, B, crypto_scalarmult_ed25519_BYTES);
    if (crypto_scalarmult_ed25519(q2, n, p) != 0) {
        printf("crypto_scalarmult_ed25519() failed\n");
    }
    if (memcmp(q, q2, crypto_scalarmult_ed25519_BYTES) != 0) {
        printf("crypto_scalarmult_ed25519_base(n) != crypto_scalarmult_ed25519(n, 9)\n");
    }

    memset(n, 0, crypto_scalarmult_ed25519_SCALARBYTES);
    if (crypto_scalarmult_ed25519_base(q, n) != -1) {
        printf("crypto_scalarmult_ed25519_base(0) failed\n");
    }
    if (crypto_scalarmult_ed25519(q2, n, p) != -1) {
        printf("crypto_scalarmult_ed25519(0) passed\n");
    }

    n[0] = 1;
    if (crypto_scalarmult_ed25519_base(q, n) != 0) {
        printf("crypto_scalarmult_ed25519_base() failed\n");
    }
    if (crypto_scalarmult_ed25519(q2, n, p) != 0) {
        printf("crypto_scalarmult_ed25519() passed\n");
    }

    if (crypto_scalarmult_ed25519(q, n, non_canonical_p) != -1) {
        printf("crypto_scalarmult_ed25519() didn't fail\n");
    }
    if (crypto_scalarmult_ed25519(q, n, non_canonical_invalid_p) != -1) {
        printf("crypto_scalarmult_ed25519() didn't fail\n");
    }
    if (crypto_scalarmult_ed25519(q, n, max_canonical_p) != 0) {
        printf("crypto_scalarmult_ed25519() failed\n");
    }

    memset(p, 0, crypto_scalarmult_ed25519_BYTES);
    if (crypto_scalarmult_ed25519(q, n, p) != -1) {
        printf("crypto_scalarmult_ed25519() didn't fail\n");
    }
    n[0] = 8;
    if (crypto_scalarmult_ed25519(q, n, p) != -1) {
        printf("crypto_scalarmult_ed25519() didn't fail\n");
    }

    sodium_free(q2);
    sodium_free(q);
    sodium_free(p);
    sodium_free(n);

    assert(crypto_scalarmult_ed25519_BYTES == crypto_scalarmult_ed25519_bytes());
    assert(crypto_scalarmult_ed25519_SCALARBYTES == crypto_scalarmult_ed25519_scalarbytes());

    printf("OK\n");

    return 0;
}
