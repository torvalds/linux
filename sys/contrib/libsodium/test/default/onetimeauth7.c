
#define TEST_NAME "onetimeauth7"
#include "cmptest.h"

static unsigned char key[32];
static unsigned char c[1000];
static unsigned char a[16];

int
main(void)
{
    int clen;

    for (clen = 0; clen < 1000; ++clen) {
        crypto_onetimeauth_keygen(key);
        randombytes_buf(c, clen);
        crypto_onetimeauth(a, c, clen, key);
        if (crypto_onetimeauth_verify(a, c, clen, key) != 0) {
            printf("fail %d\n", clen);
            return 100;
        }
        if (clen > 0) {
            c[rand() % clen] += 1 + (rand() % 255);
            if (crypto_onetimeauth_verify(a, c, clen, key) == 0) {
                printf("forgery %d\n", clen);
                return 100;
            }
            a[rand() % sizeof a] += 1 + (rand() % 255);
            if (crypto_onetimeauth_verify(a, c, clen, key) == 0) {
                printf("forgery %d\n", clen);
                return 100;
            }
        }
    }
    return 0;
}
