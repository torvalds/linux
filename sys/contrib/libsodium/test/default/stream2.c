
#define TEST_NAME "stream2"
#include "cmptest.h"

static unsigned char secondkey[32] = { 0xdc, 0x90, 0x8d, 0xda, 0x0b, 0x93, 0x44,
                                       0xa9, 0x53, 0x62, 0x9b, 0x73, 0x38, 0x20,
                                       0x77, 0x88, 0x80, 0xf3, 0xce, 0xb4, 0x21,
                                       0xbb, 0x61, 0xb9, 0x1c, 0xbd, 0x4c, 0x3e,
                                       0x66, 0x25, 0x6c, 0xe4 };

static unsigned char noncesuffix[8] = { 0x82, 0x19, 0xe0, 0x03,
                                        0x6b, 0x7a, 0x0b, 0x37 };

static unsigned char output[4194304];

static unsigned char h[32];

int
main(void)
{
    int i;
    crypto_stream_salsa20(output, sizeof output, noncesuffix, secondkey);
    crypto_hash_sha256(h, output, sizeof output);
    for (i = 0; i < 32; ++i)
        printf("%02x", h[i]);
    printf("\n");

    assert(sizeof output > 4000);

    crypto_stream_salsa20_xor_ic(output, output, 4000, noncesuffix, 0U,
                                 secondkey);
    for (i = 0; i < 4000; ++i)
        assert(output[i] == 0);

    crypto_stream_salsa20_xor_ic(output, output, 4000, noncesuffix, 1U,
                                 secondkey);
    crypto_hash_sha256(h, output, sizeof output);
    for (i = 0; i < 32; ++i)
        printf("%02x", h[i]);
    printf("\n");

    assert(crypto_stream_salsa20_keybytes() > 0U);
    assert(crypto_stream_salsa20_noncebytes() > 0U);
    assert(crypto_stream_salsa20_messagebytes_max() > 0U);

    return 0;
}
