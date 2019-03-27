
#define TEST_NAME "box_seed"
#include "cmptest.h"

static unsigned char seed[32] = { 0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5,
                                  0x7d, 0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2,
                                  0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb,
                                  0xc0, 0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5,
                                  0x1d, 0xb9, 0x2c, 0x2a };

int
main(void)
{
    int           i;
    unsigned char sk[32];
    unsigned char pk[32];

    crypto_box_seed_keypair(pk, sk, seed);
    for (i = 0; i < 32; ++i) {
        printf(",0x%02x", (unsigned int) pk[i]);
        if (i % 8 == 7)
            printf("\n");
    }
    for (i = 0; i < 32; ++i) {
        printf(",0x%02x", (unsigned int) sk[i]);
        if (i % 8 == 7)
            printf("\n");
    }
    return 0;
}
