
#define TEST_NAME "box7"
#include "cmptest.h"

static unsigned char alicesk[crypto_box_SECRETKEYBYTES];
static unsigned char alicepk[crypto_box_PUBLICKEYBYTES];
static unsigned char bobsk[crypto_box_SECRETKEYBYTES];
static unsigned char bobpk[crypto_box_PUBLICKEYBYTES];
static unsigned char n[crypto_box_NONCEBYTES];

int
main(void)
{
    unsigned char *m;
    unsigned char *c;
    unsigned char *m2;
    size_t         mlen;
    size_t         mlen_max = 1000;
    size_t         i;
    int            ret;

    m  = (unsigned char *) sodium_malloc(mlen_max);
    c  = (unsigned char *) sodium_malloc(mlen_max);
    m2 = (unsigned char *) sodium_malloc(mlen_max);
    memset(m, 0, crypto_box_ZEROBYTES);
    crypto_box_keypair(alicepk, alicesk);
    crypto_box_keypair(bobpk, bobsk);
    for (mlen = 0; mlen + crypto_box_ZEROBYTES <= mlen_max; mlen++) {
        randombytes_buf(n, crypto_box_NONCEBYTES);
        randombytes_buf(m + crypto_box_ZEROBYTES, mlen);
        ret = crypto_box(c, m, mlen + crypto_box_ZEROBYTES, n, bobpk, alicesk);
        assert(ret == 0);
        if (crypto_box_open(m2, c, mlen + crypto_box_ZEROBYTES, n, alicepk,
                            bobsk) == 0) {
            for (i = 0; i < mlen + crypto_box_ZEROBYTES; ++i) {
                if (m2[i] != m[i]) {
                    printf("bad decryption\n");
                    break;
                }
            }
        } else {
            printf("ciphertext fails verification\n");
        }
    }
    sodium_free(m);
    sodium_free(c);
    sodium_free(m2);

    return 0;
}
