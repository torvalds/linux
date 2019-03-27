#define TEST_NAME "core_ed25519"
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

static void
add_P(unsigned char * const S)
{
    static const unsigned char P[32] = {
        0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f
    };
    unsigned char c = 0U;
    unsigned int  i;
    unsigned int  s;

    for (i = 0U; i < 32U; i++) {
        s = S[i] + P[i] + c;
        S[i] = (unsigned char) s;
        c = (s >> 8) & 1;
    }
}

int
main(void)
{
    unsigned char *h;
    unsigned char *p, *p2, *p3;
    unsigned char *sc;
    int            i, j;

    h = (unsigned char *) sodium_malloc(crypto_core_ed25519_UNIFORMBYTES);
    p = (unsigned char *) sodium_malloc(crypto_core_ed25519_BYTES);
    for (i = 0; i < 1000; i++) {
        randombytes_buf(h, crypto_core_ed25519_UNIFORMBYTES);
        if (crypto_core_ed25519_from_uniform(p, h) != 0) {
            printf("crypto_core_ed25519_from_uniform() failed\n");
        }
        if (crypto_core_ed25519_is_valid_point(p) == 0) {
            printf("crypto_core_ed25519_from_uniform() returned an invalid point\n");
        }
    }

    p2 = (unsigned char *) sodium_malloc(crypto_core_ed25519_BYTES);
    p3 = (unsigned char *) sodium_malloc(crypto_core_ed25519_BYTES);
    randombytes_buf(h, crypto_core_ed25519_UNIFORMBYTES);
    crypto_core_ed25519_from_uniform(p2, h);

    j = 1 + (int) randombytes_uniform(100);
    memcpy(p3, p, crypto_core_ed25519_BYTES);
    for (i = 0; i < j; i++) {
        crypto_core_ed25519_add(p, p, p2);
        if (crypto_core_ed25519_is_valid_point(p) != 1) {
            printf("crypto_core_add() returned an invalid point\n");
        }
    }
    if (memcmp(p, p3, crypto_core_ed25519_BYTES) == 0) {
        printf("crypto_core_add() failed\n");
    }
    for (i = 0; i < j; i++) {
        crypto_core_ed25519_sub(p, p, p2);
    }
    if (memcmp(p, p3, crypto_core_ed25519_BYTES) != 0) {
        printf("crypto_core_add() or crypto_core_sub() failed\n");
    }
    sc = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_SCALARBYTES);
    memset(sc, 0, crypto_scalarmult_ed25519_SCALARBYTES);
    sc[0] = 8;
    memcpy(p2, p, crypto_core_ed25519_BYTES);
    memcpy(p3, p, crypto_core_ed25519_BYTES);

    for (i = 0; i < 254; i++) {
        crypto_core_ed25519_add(p2, p2, p2);
    }
    for (i = 0; i < 8; i++) {
        crypto_core_ed25519_add(p2, p2, p);
    }
    if (crypto_scalarmult_ed25519(p3, sc, p) != 0) {
        printf("crypto_scalarmult_ed25519() failed\n");
    }
    if (memcmp(p2, p3, crypto_core_ed25519_BYTES) != 0) {
        printf("crypto_scalarmult_ed25519() is inconsistent with crypto_core_ed25519_add()\n");
    }

    assert(crypto_core_ed25519_is_valid_point(p) == 1);

    memset(p, 0, crypto_core_ed25519_BYTES);
    assert(crypto_core_ed25519_is_valid_point(p) == 0);

    p[0] = 1;
    assert(crypto_core_ed25519_is_valid_point(p) == 0);

    p[0] = 2;
    assert(crypto_core_ed25519_is_valid_point(p) == 0);

    p[0] = 9;
    assert(crypto_core_ed25519_is_valid_point(p) == 1);

    assert(crypto_core_ed25519_is_valid_point(max_canonical_p) == 1);
    assert(crypto_core_ed25519_is_valid_point(non_canonical_invalid_p) == 0);
    assert(crypto_core_ed25519_is_valid_point(non_canonical_p) == 0);

    memcpy(p2, p, crypto_core_ed25519_BYTES);
    add_P(p2);
    crypto_core_ed25519_add(p3, p2, p2);
    crypto_core_ed25519_sub(p3, p3, p2);
    assert(memcmp(p2, p, crypto_core_ed25519_BYTES) != 0);
    assert(memcmp(p3, p, crypto_core_ed25519_BYTES) == 0);

    p[0] = 2;
    assert(crypto_core_ed25519_add(p3, p2, p) == -1);
    assert(crypto_core_ed25519_add(p3, p2, non_canonical_p) == 0);
    assert(crypto_core_ed25519_add(p3, p2, non_canonical_invalid_p) == -1);
    assert(crypto_core_ed25519_add(p3, p, p3) == -1);
    assert(crypto_core_ed25519_add(p3, non_canonical_p, p3) == 0);
    assert(crypto_core_ed25519_add(p3, non_canonical_invalid_p, p3) == -1);

    assert(crypto_core_ed25519_sub(p3, p2, p) == -1);
    assert(crypto_core_ed25519_sub(p3, p2, non_canonical_p) == 0);
    assert(crypto_core_ed25519_sub(p3, p2, non_canonical_invalid_p) == -1);
    assert(crypto_core_ed25519_sub(p3, p, p3) == -1);
    assert(crypto_core_ed25519_sub(p3, non_canonical_p, p3) == 0);
    assert(crypto_core_ed25519_sub(p3, non_canonical_invalid_p, p3) == -1);

    sodium_free(sc);
    sodium_free(p3);
    sodium_free(p2);
    sodium_free(p);
    sodium_free(h);

    assert(crypto_core_ed25519_BYTES == crypto_core_ed25519_bytes());
    assert(crypto_core_ed25519_UNIFORMBYTES == crypto_core_ed25519_uniformbytes());
    assert(crypto_core_ed25519_UNIFORMBYTES >= crypto_core_ed25519_BYTES);

    printf("OK\n");

    return 0;
}
