
#define TEST_NAME "box_easy2"
#include "cmptest.h"

static const unsigned char small_order_p[crypto_box_PUBLICKEYBYTES] = {
    0xe0, 0xeb, 0x7a, 0x7c, 0x3b, 0x41, 0xb8, 0xae, 0x16, 0x56, 0xe3,
    0xfa, 0xf1, 0x9f, 0xc4, 0x6a, 0xda, 0x09, 0x8d, 0xeb, 0x9c, 0x32,
    0xb1, 0xfd, 0x86, 0x62, 0x05, 0x16, 0x5f, 0x49, 0xb8, 0x00
};

int
main(void)
{
    unsigned char *alicepk;
    unsigned char *alicesk;
    unsigned char *bobpk;
    unsigned char *bobsk;
    unsigned char *mac;
    unsigned char *nonce;
    unsigned char *k1;
    unsigned char *k2;
    unsigned char *m;
    unsigned char *m2;
    unsigned char *c;
    size_t         mlen;
    size_t         i;
    size_t         m_size;
    size_t         m2_size;
    size_t         c_size;
    int            ret;

    m2_size = m_size = 7U + randombytes_uniform(1000);
    c_size           = crypto_box_MACBYTES + m_size;
    m                = (unsigned char *) sodium_malloc(m_size);
    m2               = (unsigned char *) sodium_malloc(m2_size);
    c                = (unsigned char *) sodium_malloc(c_size);
    alicepk = (unsigned char *) sodium_malloc(crypto_box_PUBLICKEYBYTES);
    alicesk = (unsigned char *) sodium_malloc(crypto_box_SECRETKEYBYTES);
    bobpk   = (unsigned char *) sodium_malloc(crypto_box_PUBLICKEYBYTES);
    bobsk   = (unsigned char *) sodium_malloc(crypto_box_SECRETKEYBYTES);
    mac     = (unsigned char *) sodium_malloc(crypto_box_MACBYTES);
    nonce   = (unsigned char *) sodium_malloc(crypto_box_NONCEBYTES);
    k1      = (unsigned char *) sodium_malloc(crypto_box_BEFORENMBYTES);
    k2      = (unsigned char *) sodium_malloc(crypto_box_BEFORENMBYTES);
    crypto_box_keypair(alicepk, alicesk);
    crypto_box_keypair(bobpk, bobsk);
    mlen = (size_t) randombytes_uniform((uint32_t) m_size) + 1U;
    randombytes_buf(m, mlen);
    randombytes_buf(nonce, crypto_box_NONCEBYTES);
    ret = crypto_box_easy(c, m, mlen, nonce, bobpk, alicesk);
    assert(ret == 0);
    if (crypto_box_open_easy(m2, c,
                             (unsigned long long) mlen + crypto_box_MACBYTES,
                             nonce, alicepk, bobsk) != 0) {
        printf("open() failed");
        return 1;
    }
    printf("%d\n", memcmp(m, m2, mlen));

    for (i = 0; i < mlen + crypto_box_MACBYTES - 1; i++) {
        if (crypto_box_open_easy(m2, c, (unsigned long long) i, nonce, alicepk,
                                 bobsk) == 0) {
            printf("short open() should have failed");
            return 1;
        }
    }
    memcpy(c, m, mlen);
    ret =
        crypto_box_easy(c, c, (unsigned long long) mlen, nonce, bobpk, alicesk);
    assert(ret == 0);
    printf("%d\n", memcmp(m, c, mlen) == 0);
    printf("%d\n", memcmp(m, c + crypto_box_MACBYTES, mlen) == 0);
    if (crypto_box_open_easy(c, c,
                             (unsigned long long) mlen + crypto_box_MACBYTES,
                             nonce, alicepk, bobsk) != 0) {
        printf("crypto_box_open_easy() failed\n");
    }

    ret = crypto_box_beforenm(k1, small_order_p, bobsk);
    assert(ret == -1);
    ret = crypto_box_beforenm(k2, small_order_p, alicesk);
    assert(ret == -1);

    ret = crypto_box_beforenm(k1, alicepk, bobsk);
    assert(ret == 0);
    ret = crypto_box_beforenm(k2, bobpk, alicesk);
    assert(ret == 0);

    memset(m2, 0, m2_size);

    if (crypto_box_easy_afternm(c, m, 0, nonce, k1) != 0) {
        printf(
            "crypto_box_easy_afternm() with a null ciphertext should have "
            "worked\n");
    }
    crypto_box_easy_afternm(c, m, (unsigned long long) mlen, nonce, k1);
    if (crypto_box_open_easy_afternm(
            m2, c, (unsigned long long) mlen + crypto_box_MACBYTES, nonce,
            k2) != 0) {
        printf("crypto_box_open_easy_afternm() failed\n");
    }
    printf("%d\n", memcmp(m, m2, mlen));
    if (crypto_box_open_easy_afternm(m2, c, crypto_box_MACBYTES - 1U, nonce,
                                     k2) == 0) {
        printf(
            "crypto_box_open_easy_afternm() with a huge ciphertext should have "
            "failed\n");
    }
    memset(m2, 0, m2_size);
    ret = crypto_box_detached(c, mac, m, (unsigned long long) mlen, nonce,
                              small_order_p, bobsk);
    assert(ret == -1);
    ret = crypto_box_detached(c, mac, m, (unsigned long long) mlen, nonce,
                              alicepk, bobsk);
    assert(ret == 0);
    if (crypto_box_open_detached(m2, c, mac, (unsigned long long) mlen, nonce,
                                 small_order_p, alicesk) != -1) {
        printf("crypto_box_open_detached() with a weak key passed\n");
    }
    if (crypto_box_open_detached(m2, c, mac, (unsigned long long) mlen, nonce,
                                 bobpk, alicesk) != 0) {
        printf("crypto_box_open_detached() failed\n");
    }
    printf("%d\n", memcmp(m, m2, mlen));

    memset(m2, 0, m2_size);
    crypto_box_detached_afternm(c, mac, m, (unsigned long long) mlen, nonce,
                                k1);
    if (crypto_box_open_detached_afternm(m2, c, mac, (unsigned long long) mlen,
                                         nonce, k2) != 0) {
        printf("crypto_box_open_detached_afternm() failed\n");
    }
    printf("%d\n", memcmp(m, m2, mlen));

    sodium_free(alicepk);
    sodium_free(alicesk);
    sodium_free(bobpk);
    sodium_free(bobsk);
    sodium_free(mac);
    sodium_free(nonce);
    sodium_free(k1);
    sodium_free(k2);
    sodium_free(m);
    sodium_free(m2);
    sodium_free(c);
    printf("OK\n");

    return 0;
}
