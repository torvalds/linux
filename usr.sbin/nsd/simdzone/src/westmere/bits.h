/*
 * bits.h -- Westmere specific implementation of bit manipulation instructions
 *
 * Copyright (c) 2018-2022 The simdjson authors
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef BITS_H
#define BITS_H

#include <stdbool.h>
#include <stdint.h>
#include <immintrin.h>

static inline bool add_overflow(uint64_t value1, uint64_t value2, uint64_t *result) {
#if has_builtin(__builtin_uaddll_overflow)
  return __builtin_uaddll_overflow(value1, value2, (unsigned long long *)result);
#else
  *result = value1 + value2;
  return *result < value1;
#endif
}

static inline uint64_t count_ones(uint64_t input_num) {
  return (uint64_t)_mm_popcnt_u64(input_num);
}

no_sanitize_undefined
static inline uint64_t trailing_zeroes(uint64_t mask) {
#if has_builtin(__builtin_ctzll)
  return (uint64_t)__builtin_ctzll(mask);
#else
  uint64_t result;
  asm("bsfq %[mask], %[result]"
      : [result] "=r" (result)
      : [mask] "mr" (mask));
  return result;
#endif
}

// result might be undefined when input_num is zero
static inline uint64_t clear_lowest_bit(uint64_t input_num) {
  return input_num & (input_num-1);
}

static inline uint64_t leading_zeroes(uint64_t mask) {
#if has_builtin(__builtin_clzll)
  return (uint64_t)__builtin_clzll(mask);
#else
  uint64_t result;
  asm("bsrq %[mask], %[result]" :
      [result] "=r" (result) :
      [mask] "mr" (mask));
  return 63 ^ (int)result;
#endif
}

static inline uint64_t prefix_xor(const uint64_t bitmask) {
  __m128i all_ones = _mm_set1_epi8('\xFF');
  __m128i result = _mm_clmulepi64_si128(_mm_set_epi64x(0ULL, (long long)bitmask), all_ones, 0);
  return (uint64_t)_mm_cvtsi128_si64(result);
}

#endif // BITS_H
