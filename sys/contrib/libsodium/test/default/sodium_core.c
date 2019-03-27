
#define TEST_NAME "sodium_core"
#include "cmptest.h"

static void
misuse_handler(void)
{
    printf("misuse_handler()\n");
    exit(0);
}

int
main(void)
{
    sodium_set_misuse_handler(NULL);
    sodium_set_misuse_handler(misuse_handler);
    sodium_set_misuse_handler(NULL);

    assert(sodium_init() == 1);

    (void) sodium_runtime_has_neon();
    (void) sodium_runtime_has_sse2();
    (void) sodium_runtime_has_sse3();
    (void) sodium_runtime_has_ssse3();
    (void) sodium_runtime_has_sse41();
    (void) sodium_runtime_has_avx();
    (void) sodium_runtime_has_avx2();
    (void) sodium_runtime_has_avx512f();
    (void) sodium_runtime_has_pclmul();
    (void) sodium_runtime_has_aesni();
    (void) sodium_runtime_has_rdrand();

    sodium_set_misuse_handler(misuse_handler);
#ifndef __EMSCRIPTEN__
    sodium_misuse();
    printf("Misuse handler returned\n");
#else
    printf("misuse_handler()\n");
#endif

    return 0;
}
