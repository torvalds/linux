
#define TEST_NAME "auth5"
#include "cmptest.h"

static unsigned char key[32];
static unsigned char c[1000];
static unsigned char a[32];

int
main(void)
{
    size_t clen;

    for (clen = 0; clen < 1000; ++clen) {
        crypto_auth_keygen(key);
        randombytes_buf(c, clen);
        crypto_auth(a, c, clen, key);
        if (crypto_auth_verify(a, c, clen, key) != 0) {
            printf("fail %u\n", (unsigned int) clen);
            return 100;
        }
        if (clen > 0) {
            c[rand() % clen] += 1 + (rand() % 255);
            if (crypto_auth_verify(a, c, clen, key) == 0) {
                printf("forgery %u\n", (unsigned int) clen);
                return 100;
            }
            a[rand() % sizeof a] += 1 + (rand() % 255);
            if (crypto_auth_verify(a, c, clen, key) == 0) {
                printf("forgery %u\n", (unsigned int) clen);
                return 100;
            }
        }
    }
    return 0;
}
