/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

/**
 * Helper functions for fuzzing.
 */

#ifndef FUZZ_HELPERS_H
#define FUZZ_HELPERS_H

#include "fuzz.h"
#include "xxhash.h"
#include "zstd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define FUZZ_QUOTE_IMPL(str) #str
#define FUZZ_QUOTE(str) FUZZ_QUOTE_IMPL(str)

/**
 * Asserts for fuzzing that are always enabled.
 */
#define FUZZ_ASSERT_MSG(cond, msg)                                             \
  ((cond) ? (void)0                                                            \
          : (fprintf(stderr, "%s: %u: Assertion: `%s' failed. %s\n", __FILE__, \
                     __LINE__, FUZZ_QUOTE(cond), (msg)),                       \
             abort()))
#define FUZZ_ASSERT(cond) FUZZ_ASSERT_MSG((cond), "");
#define FUZZ_ZASSERT(code)                                                     \
  FUZZ_ASSERT_MSG(!ZSTD_isError(code), ZSTD_getErrorName(code))

#if defined(__GNUC__)
#define FUZZ_STATIC static __inline __attribute__((unused))
#elif defined(__cplusplus) ||                                                  \
    (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#define FUZZ_STATIC static inline
#elif defined(_MSC_VER)
#define FUZZ_STATIC static __inline
#else
#define FUZZ_STATIC static
#endif

/**
 * Determininistically constructs a seed based on the fuzz input.
 * Consumes up to the first FUZZ_RNG_SEED_SIZE bytes of the input.
 */
FUZZ_STATIC uint32_t FUZZ_seed(uint8_t const **src, size_t* size) {
    uint8_t const *data = *src;
    size_t const toHash = MIN(FUZZ_RNG_SEED_SIZE, *size);
    *size -= toHash;
    *src += toHash;
    return XXH32(data, toHash, 0);
}

#define FUZZ_rotl32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))

FUZZ_STATIC uint32_t FUZZ_rand(uint32_t *state) {
    static const uint32_t prime1 = 2654435761U;
    static const uint32_t prime2 = 2246822519U;
    uint32_t rand32 = *state;
    rand32 *= prime1;
    rand32 += prime2;
    rand32 = FUZZ_rotl32(rand32, 13);
    *state = rand32;
    return rand32 >> 5;
}

/* Returns a random numer in the range [min, max]. */
FUZZ_STATIC uint32_t FUZZ_rand32(uint32_t *state, uint32_t min, uint32_t max) {
    uint32_t random = FUZZ_rand(state);
    return min + (random % (max - min + 1));
}

#ifdef __cplusplus
}
#endif

#endif
