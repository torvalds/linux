
#define TEST_NAME "secretbox7"
#include "cmptest.h"

static unsigned char k[crypto_secretbox_KEYBYTES];
static unsigned char n[crypto_secretbox_NONCEBYTES];
static unsigned char m[10000];
static unsigned char c[10000];
static unsigned char m2[10000];

int
main(void)
{
    size_t mlen;
    size_t i;

    for (mlen = 0; mlen < 1000 && mlen + crypto_secretbox_ZEROBYTES < sizeof m;
         ++mlen) {
        crypto_secretbox_keygen(k);
        randombytes_buf(n, crypto_secretbox_NONCEBYTES);
        randombytes_buf(m + crypto_secretbox_ZEROBYTES, mlen);
        crypto_secretbox(c, m, mlen + crypto_secretbox_ZEROBYTES, n, k);
        if (crypto_secretbox_open(m2, c, mlen + crypto_secretbox_ZEROBYTES, n,
                                  k) == 0) {
            for (i = 0; i < mlen + crypto_secretbox_ZEROBYTES; ++i) {
                if (m2[i] != m[i]) {
                    printf("bad decryption\n");
                    break;
                }
            }
        } else {
            printf("ciphertext fails verification\n");
        }
    }
    return 0;
}
