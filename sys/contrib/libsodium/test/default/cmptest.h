
#ifndef __CMPTEST_H__
#define __CMPTEST_H__

#ifdef NDEBUG
#/**/undef/**/ NDEBUG
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sodium.h"
#include "quirks.h"

#ifdef __EMSCRIPTEN__
# undef TEST_SRCDIR
# define TEST_SRCDIR "/test-data"
#endif
#ifndef TEST_SRCDIR
# define TEST_SRCDIR "."
#endif

#define TEST_NAME_RES TEST_NAME ".res"
#define TEST_NAME_OUT TEST_SRCDIR "/" TEST_NAME ".exp"

#ifdef HAVE_ARC4RANDOM
# undef rand
# define rand(X) arc4random(X)
#endif

int xmain(void);

#ifdef BENCHMARKS

# include <sys/time.h>

# ifndef ITERATIONS
#  define ITERATIONS 128
# endif

struct {
    void   *pnt;
    size_t  size;
} mempool[1024];

static size_t mempool_idx;

static __attribute__((malloc)) void *mempool_alloc(size_t size)
{
    size_t i;
    if (size >= (size_t) 0x80000000 - (size_t) 0x00000fff) {
        return NULL;
    }
    size = (size + (size_t) 0x00000fff) & ~ (size_t) 0x00000fff;
    for (i = 0U; i < mempool_idx; i++) {
        if (mempool[i].size >= (size | (size_t) 0x80000000)) {
            mempool[i].size &= ~ (size_t) 0x80000000;
            return mempool[i].pnt;
        }
    }
    if (mempool_idx >= sizeof mempool / sizeof mempool[0]) {
        return NULL;
    }
    mempool[mempool_idx].size = size;
    return (mempool[mempool_idx++].pnt = (void *) malloc(size));
}

static void mempool_free(void *pnt)
{
    size_t i;
    for (i = 0U; i < mempool_idx; i++) {
        if (mempool[i].pnt == pnt) {
            if ((mempool[i].size & (size_t) 0x80000000) != (size_t) 0x0) {
                break;
            }
            mempool[i].size |= (size_t) 0x80000000;
            return;
        }
    }
    abort();
}

static __attribute__((malloc)) void *mempool_allocarray(size_t count, size_t size)
{
    if (count > (size_t) 0U && size >= (size_t) SIZE_MAX / count) {
        return NULL;
    }
    return mempool_alloc(count * size);
}

static int mempool_free_all(void)
{
    size_t i;
    int    ret = 0;

    for (i = 0U; i < mempool_idx; i++) {
        if ((mempool[i].size & (size_t) 0x80000000) == (size_t) 0x0) {
            ret = -1;
        }
        free(mempool[i].pnt);
        mempool[i].pnt = NULL;
    }
    mempool_idx = (size_t) 0U;

    return ret;
}

#define sodium_malloc(X)        mempool_alloc(X)
#define sodium_free(X)          mempool_free(X)
#define sodium_allocarray(X, Y) mempool_allocarray((X), (Y))

static unsigned long long now(void)
{
    struct             timeval tp;
    unsigned long long now;

    if (gettimeofday(&tp, NULL) != 0) {
        abort();
    }
    now = ((unsigned long long) tp.tv_sec * 1000000ULL) +
        (unsigned long long) tp.tv_usec;

    return now;
}

int main(void)
{
    unsigned long long ts_start;
    unsigned long long ts_end;
    unsigned int       i;

    if (sodium_init() != 0) {
        return 99;
    }

#ifndef __EMSCRIPTEN__
    randombytes_set_implementation(&randombytes_salsa20_implementation);
#endif
    ts_start = now();
    for (i = 0; i < ITERATIONS; i++) {
        if (xmain() != 0) {
            abort();
        }
    }
    ts_end = now();
    printf("%llu\n", 1000000ULL * (ts_end - ts_start) / ITERATIONS);
    if (mempool_free_all() != 0) {
        fprintf(stderr, "** memory leaks detected **\n");
        return 99;
    }
    return 0;
}

#undef  printf
#define printf(...) do { } while(0)

#elif !defined(BROWSER_TESTS)

static FILE *fp_res;

int main(void)
{
    FILE *fp_out;
    int   c;

    if ((fp_res = fopen(TEST_NAME_RES, "w+")) == NULL) {
        perror("fopen(" TEST_NAME_RES ")");
        return 99;
    }
    if (sodium_init() != 0) {
        return 99;
    }
    if (xmain() != 0) {
        return 99;
    }
    rewind(fp_res);
    if ((fp_out = fopen(TEST_NAME_OUT, "r")) == NULL) {
        perror("fopen(" TEST_NAME_OUT ")");
        return 99;
    }
    do {
        if ((c = fgetc(fp_res)) != fgetc(fp_out)) {
            return 99;
        }
    } while (c != EOF);

    return 0;
}

#undef  printf
#define printf(...) fprintf(fp_res, __VA_ARGS__)

#else

int main(void)
{
    if (sodium_init() != 0) {
        return 99;
    }
    if (xmain() != 0) {
        return 99;
    }
    printf("--- SUCCESS ---\n");

    return 0;
}

#endif

#define main xmain

#endif
