
#define TEST_NAME "auth7"
#include "cmptest.h"

static unsigned char key[32];
static unsigned char c[600];
static unsigned char a[64];

int
main(void)
{
    size_t clen;

    for (clen = 0; clen < sizeof c; ++clen) {
        crypto_auth_keygen(key);
        randombytes_buf(c, clen);
        crypto_auth_hmacsha512(a, c, clen, key);
        if (crypto_auth_hmacsha512_verify(a, c, clen, key) != 0) {
            printf("fail %u\n", (unsigned int) clen);
            return 100;
        }
        if (clen > 0) {
            c[(size_t) rand() % clen] += 1 + (rand() % 255);
            if (crypto_auth_hmacsha512_verify(a, c, clen, key) == 0) {
                printf("forgery %u\n", (unsigned int) clen);
                return 100;
            }
            a[rand() % sizeof a] += 1 + (rand() % 255);
            if (crypto_auth_hmacsha512_verify(a, c, clen, key) == 0) {
                printf("forgery %u\n", (unsigned int) clen);
                return 100;
            }
        }
    }
    return 0;
}
