
#include <stdlib.h>
#include <sys/types.h>

#include <limits.h>
#include <signal.h>

#define TEST_NAME "sodium_utils2"
#include "cmptest.h"

#ifdef __SANITIZE_ADDRESS__
# warning The sodium_utils2 test is expected to fail with address sanitizer
#endif

#undef sodium_malloc
#undef sodium_free
#undef sodium_allocarray

__attribute__((noreturn)) static void
segv_handler(int sig)
{
    (void) sig;

    printf("Intentional segfault / bus error caught\n");
    printf("OK\n");
#ifdef SIGSEGV
    signal(SIGSEGV, SIG_DFL);
#endif
#ifdef SIGBUS
    signal(SIGBUS, SIG_DFL);
#endif
#ifdef SIGABRT
    signal(SIGABRT, SIG_DFL);
#endif
    exit(0);
}

int
main(void)
{
    void *       buf;
    size_t       size;
    unsigned int i;

    if (sodium_malloc(SIZE_MAX - 1U) != NULL) {
        return 1;
    }
    if (sodium_malloc(0U) == NULL) {
        return 1;
    }
    if (sodium_allocarray(SIZE_MAX / 2U + 1U, SIZE_MAX / 2U) != NULL) {
        return 1;
    }
    sodium_free(sodium_allocarray(0U, 0U));
    sodium_free(sodium_allocarray(0U, 1U));
    sodium_free(sodium_allocarray(1U, 0U));

    buf = sodium_allocarray(1000U, 50U);
    memset(buf, 0, 50000U);
    sodium_free(buf);

    sodium_free(sodium_malloc(0U));
    sodium_free(NULL);
    for (i = 0U; i < 10000U; i++) {
        size = 1U + randombytes_uniform(100000U);
        buf  = sodium_malloc(size);
        assert(buf != NULL);
        memset(buf, i, size);
        sodium_mprotect_noaccess(buf);
        sodium_free(buf);
    }
    printf("OK\n");

#ifdef SIGSEGV
    signal(SIGSEGV, segv_handler);
#endif
#ifdef SIGBUS
    signal(SIGBUS, segv_handler);
#endif
#ifdef SIGABRT
    signal(SIGABRT, segv_handler);
#endif
    size = 1U + randombytes_uniform(100000U);
    buf  = sodium_malloc(size);
    assert(buf != NULL);

/* old versions of asan emit a warning because they don't support mlock*() */
#ifndef __SANITIZE_ADDRESS__
    sodium_mprotect_readonly(buf);
    sodium_mprotect_readwrite(buf);
#endif

#if defined(HAVE_CATCHABLE_SEGV) && !defined(__EMSCRIPTEN__) && !defined(__SANITIZE_ADDRESS__)
    sodium_memzero(((unsigned char *) buf) + size, 1U);
    sodium_mprotect_noaccess(buf);
    sodium_free(buf);
    printf("Overflow not caught\n");
#else
    segv_handler(0);
#endif
    return 0;
}
