/*
 * haswell.h -- SIMD abstractions targeting AVX2
 *
 * Copyright (c) 2022, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef SIMD_H
#define SIMD_H

#include <stdint.h>
#include <immintrin.h>

#define SIMD_8X_SIZE (32)

typedef uint8_t simd_table_t[SIMD_8X_SIZE];

#define SIMD_TABLE(v00, v01, v02, v03, v04, v05, v06, v07, \
                   v08, v09, v0a, v0b, v0c, v0d, v0e, v0f) \
  {                                                        \
    v00, v01, v02, v03, v04, v05, v06, v07,                \
    v08, v09, v0a, v0b, v0c, v0d, v0e, v0f,                \
    v00, v01, v02, v03, v04, v05, v06, v07,                \
    v08, v09, v0a, v0b, v0c, v0d, v0e, v0f                 \
  }

typedef struct { __m256i chunks[1]; } simd_8x_t;

typedef struct { __m128i chunks[1]; } simd_8x16_t;

typedef simd_8x_t simd_8x32_t;

typedef struct { __m256i chunks[2]; } simd_8x64_t;


nonnull_all
static really_inline void simd_loadu_8x(simd_8x_t *simd, const void *address)
{
  simd->chunks[0] = _mm256_loadu_si256((const __m256i *)(address));
}

nonnull_all
static really_inline void simd_storeu_8x(void *address, simd_8x_t *simd)
{
  _mm256_storeu_si256((__m256i *)address, simd->chunks[0]);
}

nonnull_all
static really_inline uint64_t simd_find_8x(const simd_8x_t *simd, char key)
{
  const __m256i k = _mm256_set1_epi8(key);
  const __m256i r = _mm256_cmpeq_epi8(simd->chunks[0], k);
  return (uint32_t)_mm256_movemask_epi8(r);
}

nonnull_all
static really_inline uint64_t simd_find_any_8x(
  const simd_8x_t *simd, const simd_table_t table)
{
  const __m256i t = _mm256_loadu_si256((const __m256i *)table);
  const __m256i r = _mm256_cmpeq_epi8(
    _mm256_shuffle_epi8(t, simd->chunks[0]), simd->chunks[0]);
  return (uint32_t)_mm256_movemask_epi8(r);
}

nonnull_all
static really_inline void simd_loadu_8x16(simd_8x16_t *simd, const uint8_t *address)
{
  simd->chunks[0] = _mm_loadu_si128((const __m128i *)address);
}

nonnull_all
static really_inline uint64_t simd_find_8x16(const simd_8x16_t *simd, char key)
{
  const __m128i k = _mm_set1_epi8(key);
  const __m128i r = _mm_cmpeq_epi8(simd->chunks[0], k);
  const uint64_t m = (uint16_t)_mm_movemask_epi8(r);
  return m;
}

#define simd_loadu_8x32(simd, address) simd_loadu_8x(simd, address)
#define simd_storeu_8x32(address, simd) simd_storeu_8x(address, simd)
#define simd_find_8x32(simd, key) simd_find_8x(simd, key)

nonnull_all
static really_inline void simd_loadu_8x64(simd_8x64_t *simd, const uint8_t *address)
{
  simd->chunks[0] = _mm256_loadu_si256((const __m256i *)(address));
  simd->chunks[1] = _mm256_loadu_si256((const __m256i *)(address+32));
}

nonnull_all
static really_inline uint64_t simd_find_8x64(const simd_8x64_t *simd, char key)
{
  const __m256i k = _mm256_set1_epi8(key);

  const __m256i r0 = _mm256_cmpeq_epi8(simd->chunks[0], k);
  const __m256i r1 = _mm256_cmpeq_epi8(simd->chunks[1], k);

  const uint64_t m0 = (uint32_t)_mm256_movemask_epi8(r0);
  const uint64_t m1 = (uint32_t)_mm256_movemask_epi8(r1);

  return m0 | (m1 << 32);
}

nonnull_all
static really_inline uint64_t simd_find_any_8x64(
  const simd_8x64_t *simd, const simd_table_t table)
{
  const __m256i t = _mm256_loadu_si256((const __m256i *)table);

  const __m256i r0 = _mm256_cmpeq_epi8(
    _mm256_shuffle_epi8(t, simd->chunks[0]), simd->chunks[0]);
  const __m256i r1 = _mm256_cmpeq_epi8(
    _mm256_shuffle_epi8(t, simd->chunks[1]), simd->chunks[1]);

  const uint64_t m0 = (uint32_t)_mm256_movemask_epi8(r0);
  const uint64_t m1 = (uint32_t)_mm256_movemask_epi8(r1);

  return m0 | (m1 << 32);
}

#endif // SIMD_H
