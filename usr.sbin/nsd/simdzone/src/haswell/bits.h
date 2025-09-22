/*
 * bits.h -- Haswell specific implementation of bit manipulation instructions
 *
 * Copyright (c) 2018-2023 The simdjson authors
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

static inline uint64_t count_ones(uint64_t bits) {
  return (uint64_t)_mm_popcnt_u64(bits);
}

no_sanitize_undefined
static inline uint64_t trailing_zeroes(uint64_t bits) {
  return (uint64_t)_tzcnt_u64(bits);
}

// result might be undefined when bits is zero
static inline uint64_t clear_lowest_bit(uint64_t bits) {
  return bits & (bits - 1);
}

static inline uint64_t leading_zeroes(uint64_t bits) {
  return (uint64_t)_lzcnt_u64(bits);
}

static inline uint64_t prefix_xor(const uint64_t bitmask) {
  __m128i all_ones = _mm_set1_epi8('\xFF');
  __m128i mask = _mm_set_epi64x(0ULL, (long long)bitmask);
#if defined __SUNPRO_C
  // Oracle Developer Studio has issues generating vpclmulqdq
  // Oracle Solaris and Intel assembler use the opposite order for source and
  //   destination operands. See x86 Assemble Language Reference Manual.
  __asm volatile ("vpclmulqdq $0,%[all_ones],%[mask],%[mask]"
    : [mask] "+x" (mask)
    : [all_ones] "x" (all_ones));
#else
  // There should be no such thing with a processor supporting avx2
  // but not clmul.
  mask = _mm_clmulepi64_si128(mask, all_ones, 0);
#endif
  return (uint64_t)_mm_cvtsi128_si64(mask);
}

#endif // BITS_H
