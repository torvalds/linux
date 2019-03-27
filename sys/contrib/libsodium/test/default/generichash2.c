
#define TEST_NAME "generichash2"
#include "cmptest.h"

int
main(void)
{
#define MAXLEN 64
    crypto_generichash_state *st;
    unsigned char            in[MAXLEN];
    unsigned char            out[crypto_generichash_BYTES_MAX];
    unsigned char            k[crypto_generichash_KEYBYTES_MAX];
    size_t                   h, i, j;

    assert(crypto_generichash_statebytes() >= sizeof *st);
    st = (crypto_generichash_state *)
        sodium_malloc(crypto_generichash_statebytes());
    for (h = 0; h < crypto_generichash_KEYBYTES_MAX; ++h) {
        k[h] = (unsigned char) h;
    }
    for (i = 0; i < MAXLEN; ++i) {
        in[i] = (unsigned char) i;
        if (crypto_generichash_init(st, k,
                                    1 + i % crypto_generichash_KEYBYTES_MAX,
                                    1 + i % crypto_generichash_BYTES_MAX) != 0) {
            printf("crypto_generichash_init()\n");
            return 1;
        }
        crypto_generichash_update(st, in, i);
        crypto_generichash_update(st, in, i);
        crypto_generichash_update(st, in, i);
        if (crypto_generichash_final(st, out,
                                     1 + i % crypto_generichash_BYTES_MAX) != 0) {
            printf("crypto_generichash_final() should have returned 0\n");
        }
        for (j = 0; j < 1 + i % crypto_generichash_BYTES_MAX; ++j) {
            printf("%02x", (unsigned int) out[j]);
        }
        printf("\n");
        if (crypto_generichash_final(st, out,
                                     1 + i % crypto_generichash_BYTES_MAX) != -1) {
            printf("crypto_generichash_final() should have returned -1\n");
        }
    }

    assert(crypto_generichash_init(st, k, sizeof k, 0U) == -1);
    assert(crypto_generichash_init(st, k, sizeof k,
                                   crypto_generichash_BYTES_MAX + 1U) == -1);
    assert(crypto_generichash_init(st, k, crypto_generichash_KEYBYTES_MAX + 1U,
                                   sizeof out) == -1);
    assert(crypto_generichash_init(st, k, 0U, sizeof out) == 0);
    assert(crypto_generichash_init(st, k, 1U, sizeof out) == 0);
    assert(crypto_generichash_init(st, NULL, 1U, 0U) == -1);
    assert(crypto_generichash_init(st, NULL, crypto_generichash_KEYBYTES,
                                   1U) == 0);
    assert(crypto_generichash_init(st, NULL, crypto_generichash_KEYBYTES,
                                   0U) == -1);

    sodium_free(st);

    return 0;
}
