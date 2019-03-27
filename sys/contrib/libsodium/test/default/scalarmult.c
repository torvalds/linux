
#define TEST_NAME "scalarmult"
#include "cmptest.h"

static const unsigned char alicesk[crypto_scalarmult_BYTES] = {
    0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1,
    0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0,
    0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a
};

static const unsigned char bobsk[crypto_scalarmult_BYTES] = {
    0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b, 0x79, 0xe1, 0x7f,
    0x8b, 0x83, 0x80, 0x0e, 0xe6, 0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18,
    0xb6, 0xfd, 0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb
};

static const unsigned char small_order_p[crypto_scalarmult_BYTES] = {
    0xe0, 0xeb, 0x7a, 0x7c, 0x3b, 0x41, 0xb8, 0xae, 0x16, 0x56, 0xe3,
    0xfa, 0xf1, 0x9f, 0xc4, 0x6a, 0xda, 0x09, 0x8d, 0xeb, 0x9c, 0x32,
    0xb1, 0xfd, 0x86, 0x62, 0x05, 0x16, 0x5f, 0x49, 0xb8, 0x00
};

static char hex[crypto_scalarmult_BYTES * 2 + 1];

int
main(void)
{
    unsigned char *alicepk =
        (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
    unsigned char *bobpk =
        (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
    unsigned char *k = (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
    int            ret;

    assert(alicepk != NULL && bobpk != NULL && k != NULL);

    crypto_scalarmult_base(alicepk, alicesk);
    sodium_bin2hex(hex, sizeof hex, alicepk, crypto_scalarmult_BYTES);
    printf("%s\n", hex);

    crypto_scalarmult_base(bobpk, bobsk);
    sodium_bin2hex(hex, sizeof hex, bobpk, crypto_scalarmult_BYTES);
    printf("%s\n", hex);

    ret = crypto_scalarmult(k, alicesk, bobpk);
    assert(ret == 0);
    sodium_bin2hex(hex, sizeof hex, k, crypto_scalarmult_BYTES);
    printf("%s\n", hex);

    ret = crypto_scalarmult(k, bobsk, alicepk);
    assert(ret == 0);
    sodium_bin2hex(hex, sizeof hex, k, crypto_scalarmult_BYTES);
    printf("%s\n", hex);

    ret = crypto_scalarmult(k, bobsk, small_order_p);
    assert(ret == -1);

    sodium_free(bobpk);
    sodium_free(alicepk);
    sodium_free(k);

    assert(crypto_scalarmult_bytes() > 0U);
    assert(crypto_scalarmult_scalarbytes() > 0U);
    assert(strcmp(crypto_scalarmult_primitive(), "curve25519") == 0);
    assert(crypto_scalarmult_bytes() == crypto_scalarmult_curve25519_bytes());
    assert(crypto_scalarmult_scalarbytes() ==
           crypto_scalarmult_curve25519_scalarbytes());
    assert(crypto_scalarmult_bytes() == crypto_scalarmult_scalarbytes());

    return 0;
}
