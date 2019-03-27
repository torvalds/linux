
#define TEST_NAME "randombytes"
#include "cmptest.h"

static unsigned char      x[65536];
static unsigned long long freq[256];

static int
compat_tests(void)
{
    size_t i;

    memset(x, 0, sizeof x);
    randombytes(x, sizeof x);
    for (i = 0; i < 256; ++i) {
        freq[i] = 0;
    }
    for (i = 0; i < sizeof x; ++i) {
        ++freq[255 & (int) x[i]];
    }
    for (i = 0; i < 256; ++i) {
        if (!freq[i]) {
            printf("nacl_tests failed\n");
        }
    }
    return 0;
}

static int
randombytes_tests(void)
{
    static const unsigned char seed[randombytes_SEEDBYTES] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    unsigned char out[100];
    unsigned int  f = 0U;
    unsigned int  i;
    uint32_t      n;

#ifndef BENCHMARKS
# ifdef __EMSCRIPTEN__
    assert(strcmp(randombytes_implementation_name(), "js") == 0);
# elif defined(__native_client__)
    assert(strcmp(randombytes_implementation_name(), "nativeclient") == 0);
# else
    assert(strcmp(randombytes_implementation_name(), "sysrandom") == 0);
# endif
#endif
    randombytes(x, 1U);
    do {
        n = randombytes_random();
        f |= ((n >> 24) > 1);
        f |= ((n >> 16) > 1) << 1;
        f |= ((n >> 8) > 1) << 2;
        f |= ((n) > 1) << 3;
        f |= (n > 0x7fffffff) << 4;
    } while (f != 0x1f);
    randombytes_close();

    for (i = 0; i < 256; ++i) {
        freq[i] = 0;
    }
    for (i = 0; i < 65536; ++i) {
        ++freq[randombytes_uniform(256)];
    }
    for (i = 0; i < 256; ++i) {
        if (!freq[i]) {
            printf("randombytes_uniform() test failed\n");
        }
    }
    assert(randombytes_uniform(1U) == 0U);
    randombytes_close();
#ifndef __EMSCRIPTEN__
    randombytes_set_implementation(&randombytes_salsa20_implementation);
    assert(strcmp(randombytes_implementation_name(), "salsa20") == 0);
#endif
    randombytes_stir();
    for (i = 0; i < 256; ++i) {
        freq[i] = 0;
    }
    for (i = 0; i < 65536; ++i) {
        ++freq[randombytes_uniform(256)];
    }
    for (i = 0; i < 256; ++i) {
        if (!freq[i]) {
            printf("randombytes_uniform() test failed\n");
        }
    }
    memset(x, 0, sizeof x);
    randombytes_buf(x, sizeof x);
    for (i = 0; i < 256; ++i) {
        freq[i] = 0;
    }
    for (i = 0; i < sizeof x; ++i) {
        ++freq[255 & (int) x[i]];
    }
    for (i = 0; i < 256; ++i) {
        if (!freq[i]) {
            printf("randombytes_buf() test failed\n");
        }
    }
    assert(randombytes_uniform(1U) == 0U);

    randombytes_buf_deterministic(out, sizeof out, seed);
    for (i = 0; i < sizeof out; ++i) {
        printf("%02x", out[i]);
    }
    printf(" (deterministic)\n");

    randombytes_close();

    randombytes(x, 1U);
    randombytes_close();

    assert(randombytes_SEEDBYTES > 0);
    assert(randombytes_seedbytes() == randombytes_SEEDBYTES);

    return 0;
}

static uint32_t
randombytes_uniform_impl(const uint32_t upper_bound)
{
    return upper_bound;
}

static int
impl_tests(void)
{
#ifndef __native_client__
    randombytes_implementation impl = randombytes_sysrandom_implementation;
#else
    randombytes_implementation impl = randombytes_nativeclient_implementation;
#endif
    uint32_t                   v = randombytes_random();

    impl.uniform = randombytes_uniform_impl;
    randombytes_close();
    randombytes_set_implementation(&impl);
    assert(randombytes_uniform(1) == 1);
    assert(randombytes_uniform(v) == v);
    assert(randombytes_uniform(v) == v);
    assert(randombytes_uniform(v) == v);
    assert(randombytes_uniform(v) == v);
    randombytes_close();
    impl.close = NULL;
    randombytes_close();

    return 0;
}

int
main(void)
{
    compat_tests();
    randombytes_tests();
#ifndef __EMSCRIPTEN__
    impl_tests();
#endif
    printf("OK\n");

#ifndef __EMSCRIPTEN__
    randombytes_set_implementation(&randombytes_salsa20_implementation);
#endif

    return 0;
}
