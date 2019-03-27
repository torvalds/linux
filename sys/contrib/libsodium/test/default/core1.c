
#define TEST_NAME "core1"
#include "cmptest.h"

static unsigned char shared[32] = { 0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d,
                                    0xe1, 0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35,
                                    0x0f, 0x25, 0xe0, 0x7e, 0x21, 0xc9, 0x47,
                                    0xd1, 0x9e, 0x33, 0x76, 0xf0, 0x9b, 0x3c,
                                    0x1e, 0x16, 0x17, 0x42 };

static unsigned char zero[32];

static unsigned char c[16] = { 0x65, 0x78, 0x70, 0x61, 0x6e, 0x64, 0x20, 0x33,
                               0x32, 0x2d, 0x62, 0x79, 0x74, 0x65, 0x20, 0x6b };

static unsigned char firstkey[32];

int
main(void)
{
    int i;

    crypto_core_hsalsa20(firstkey, zero, shared, c);
    for (i = 0; i < 32; ++i) {
        if (i > 0) {
            printf(",");
        } else {
            printf(" ");
        }
        printf("0x%02x", (unsigned int) firstkey[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    assert(crypto_core_hsalsa20_outputbytes() > 0U);
    assert(crypto_core_hsalsa20_inputbytes() > 0U);
    assert(crypto_core_hsalsa20_keybytes() > 0U);
    assert(crypto_core_hsalsa20_constbytes() > 0U);

    return 0;
}
