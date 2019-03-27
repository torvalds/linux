
#define TEST_NAME "core6"
#include "cmptest.h"

static unsigned char k[32] = { 0xee, 0x30, 0x4f, 0xca, 0x27, 0x00, 0x8d, 0x8c,
                               0x12, 0x6f, 0x90, 0x02, 0x79, 0x01, 0xd8, 0x0f,
                               0x7f, 0x1d, 0x8b, 0x8d, 0xc9, 0x36, 0xcf, 0x3b,
                               0x9f, 0x81, 0x96, 0x92, 0x82, 0x7e, 0x57, 0x77 };

static unsigned char in[16] = {
    0x81, 0x91, 0x8e, 0xf2, 0xa5, 0xe0, 0xda, 0x9b,
    0x3e, 0x90, 0x60, 0x52, 0x1e, 0x4b, 0xb3, 0x52
};

static unsigned char c[16] = { 101, 120, 112, 97,  110, 100, 32, 51,
                               50,  45,  98,  121, 116, 101, 32, 107 };

static unsigned char out[64];

static void
print(unsigned char *x, unsigned char *y)
{
    int          i;
    unsigned int borrow = 0;

    for (i = 0; i < 4; ++i) {
        unsigned int xi = x[i];
        unsigned int yi = y[i];
        printf(",0x%02x", 255 & (xi - yi - borrow));
        borrow = (xi < yi + borrow);
    }
}

int
main(void)
{
    crypto_core_salsa20(out, in, k, c);
    print(out, c);
    print(out + 20, c + 4);
    printf("\n");
    print(out + 40, c + 8);
    print(out + 60, c + 12);
    printf("\n");
    print(out + 24, in);
    print(out + 28, in + 4);
    printf("\n");
    print(out + 32, in + 8);
    print(out + 36, in + 12);
    printf("\n");

    return 0;
}
