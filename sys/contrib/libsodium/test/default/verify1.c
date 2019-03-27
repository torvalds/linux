
#define TEST_NAME "verify1"
#include "cmptest.h"

int
main(void)
{
    unsigned char *v16, *v16x;
    unsigned char *v32, *v32x;
    unsigned char *v64, *v64x;
    uint32_t       r;
    uint8_t        o;
    int            i;

    v16  = (unsigned char *) sodium_malloc(16);
    v16x = (unsigned char *) sodium_malloc(16);
    v32  = (unsigned char *) sodium_malloc(32);
    v32x = (unsigned char *) sodium_malloc(32);
    v64  = (unsigned char *) sodium_malloc(64);
    v64x = (unsigned char *) sodium_malloc(64);
    for (i = 0; i < 10000; i++) {
        randombytes_buf(v16, 16);
        randombytes_buf(v32, 32);
        randombytes_buf(v64, 64);

        memcpy(v16x, v16, 16);
        memcpy(v32x, v32, 32);
        memcpy(v64x, v64, 64);

        if (crypto_verify_16(v16, v16x) != 0 ||
            crypto_verify_32(v32, v32x) != 0 ||
            crypto_verify_64(v64, v64x) != 0 ||
            sodium_memcmp(v16, v16x, 16) != 0 ||
            sodium_memcmp(v32, v32x, 32) != 0 ||
            sodium_memcmp(v64, v64x, 64) != 0) {
            printf("Failed\n");
        }
    }
    printf("OK\n");

    for (i = 0; i < 100000; i++) {
        r = randombytes_random();
        o = (uint8_t) randombytes_random();
        if (o == 0) {
            continue;
        }
        v16x[r & 15U] ^= o;
        v32x[r & 31U] ^= o;
        v64x[r & 63U] ^= o;
        if (crypto_verify_16(v16, v16x) != -1 ||
            crypto_verify_32(v32, v32x) != -1 ||
            crypto_verify_64(v64, v64x) != -1 ||
            sodium_memcmp(v16, v16x, 16) != -1 ||
            sodium_memcmp(v32, v32x, 32) != -1 ||
            sodium_memcmp(v64, v64x, 64) != -1) {
            printf("Failed\n");
        }
        v16x[r & 15U] ^= o;
        v32x[r & 31U] ^= o;
        v64x[r & 63U] ^= o;
    }
    printf("OK\n");

    assert(crypto_verify_16_bytes() == 16U);
    assert(crypto_verify_32_bytes() == 32U);
    assert(crypto_verify_64_bytes() == 64U);

    sodium_free(v16);
    sodium_free(v16x);
    sodium_free(v32);
    sodium_free(v32x);
    sodium_free(v64);
    sodium_free(v64x);

    return 0;
}
