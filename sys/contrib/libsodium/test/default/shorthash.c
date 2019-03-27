
#define TEST_NAME "shorthash"
#include "cmptest.h"

#define MAXLEN 64

int
main(void)
{
    unsigned char in[MAXLEN];
    unsigned char out[crypto_shorthash_BYTES];
    unsigned char k[crypto_shorthash_KEYBYTES];
    size_t        i;
    size_t        j;

    for (i = 0; i < crypto_shorthash_KEYBYTES; ++i) {
        k[i] = (unsigned char) i;
    }
    for (i = 0; i < MAXLEN; ++i) {
        in[i] = (unsigned char) i;
        crypto_shorthash(out, in, (unsigned long long) i, k);
        for (j = 0; j < crypto_shorthash_BYTES; ++j) {
            printf("%02x", (unsigned int) out[j]);
        }
        printf("\n");
    }
    assert(crypto_shorthash_bytes() > 0);
    assert(crypto_shorthash_keybytes() > 0);
    assert(strcmp(crypto_shorthash_primitive(), "siphash24") == 0);
    assert(crypto_shorthash_bytes() == crypto_shorthash_siphash24_bytes());
    assert(crypto_shorthash_keybytes() ==
           crypto_shorthash_siphash24_keybytes());

    return 0;
}
