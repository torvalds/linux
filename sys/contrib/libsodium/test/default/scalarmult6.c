
#define TEST_NAME "scalarmult6"
#include "cmptest.h"

static unsigned char bobsk_[crypto_scalarmult_SCALARBYTES] = {
    0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b, 0x79, 0xe1, 0x7f,
    0x8b, 0x83, 0x80, 0x0e, 0xe6, 0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18,
    0xb6, 0xfd, 0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb
};

static unsigned char alicepk_[crypto_scalarmult_SCALARBYTES] = {
    0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d,
    0xdc, 0xb4, 0x3e, 0xf7, 0x5a, 0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38,
    0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a
};

int
main(void)
{
    unsigned char *k;
    unsigned char *bobsk;
    unsigned char *alicepk;
    int            i;
    int            ret;

    k       = (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
    bobsk   = (unsigned char *) sodium_malloc(crypto_scalarmult_SCALARBYTES);
    alicepk = (unsigned char *) sodium_malloc(crypto_scalarmult_SCALARBYTES);
    assert(k != NULL && bobsk != NULL && alicepk != NULL);

    memcpy(bobsk, bobsk_, crypto_scalarmult_SCALARBYTES);
    memcpy(alicepk, alicepk_, crypto_scalarmult_SCALARBYTES);

    ret = crypto_scalarmult(k, bobsk, alicepk);
    assert(ret == 0);

    sodium_free(alicepk);
    sodium_free(bobsk);

    for (i = 0; i < 32; ++i) {
        if (i > 0) {
            printf(",");
        } else {
            printf(" ");
        }
        printf("0x%02x", (unsigned int) k[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    sodium_free(k);

    return 0;
}
