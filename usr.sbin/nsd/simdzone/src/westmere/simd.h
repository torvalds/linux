/*
 * simd.h -- SIMD abstractions targeting SSE4.2
 *
 * Copyright (c) 2022, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#ifndef SIMD_H
#define SIMD_H

#include <stdint.h>
#include <immintrin.h>

#define SIMD_8X_SIZE (16)

typedef uint8_t simd_table_t[SIMD_8X_SIZE];

#define SIMD_TABLE(v00, v01, v02, v03, v04, v05, v06, v07, \
                   v08, v09, v0a, v0b, v0c, v0d, v0e, v0f) \
  {                                                        \
    v00, v01, v02, v03, v04, v05, v06, v07,                \
    v08, v09, v0a, v0b, v0c, v0d, v0e, v0f                 \
  }

typedef struct { __m128i chunks[1]; } simd_8x_t;

typedef simd_8x_t simd_8x16_t;

typedef struct { __m128i chunks[2]; } simd_8x32_t;

typedef struct { __m128i chunks[4]; } simd_8x64_t;

nonnull_all
static really_inline void simd_loadu_8x(simd_8x_t *simd, const uint8_t *address)
{
  simd->chunks[0] = _mm_loadu_si128((const __m128i *)address);
}

nonnull_all
static really_inline void simd_storeu_8x(uint8_t *address, const simd_8x_t *simd)
{
  _mm_storeu_si128((__m128i *)address, simd->chunks[0]);
}

nonnull_all
static really_inline uint64_t simd_find_8x(const simd_8x_t *simd, char key)
{
  const __m128i k = _mm_set1_epi8(key);
  const __m128i r = _mm_cmpeq_epi8(simd->chunks[0], k);
  return (uint16_t)_mm_movemask_epi8(r);
}

nonnull_all
static really_inline uint64_t simd_find_any_8x(
  const simd_8x_t *simd, const simd_table_t table)
{
  const __m128i t = _mm_loadu_si128((const __m128i *)table);
  const __m128i r = _mm_cmpeq_epi8(
    _mm_shuffle_epi8(t, simd->chunks[0]), simd->chunks[0]);
  return (uint16_t)_mm_movemask_epi8(r);
}

#define simd_loadu_8x16(simd, address) simd_loadu_8x(simd, address)
#define simd_find_8x16(simd, key) simd_find_8x(simd, key)

nonnull_all
static really_inline void simd_loadu_8x32(simd_8x32_t *simd, const char *address)
{
  simd->chunks[0] = _mm_loadu_si128((const __m128i *)(address));
  simd->chunks[1] = _mm_loadu_si128((const __m128i *)(address+16));
}

nonnull_all
static really_inline void simd_storeu_8x32(uint8_t *address, const simd_8x32_t *simd)
{
  _mm_storeu_si128((__m128i *)(address), simd->chunks[0]);
  _mm_storeu_si128((__m128i *)(address+16), simd->chunks[1]);
}

nonnull_all
static really_inline uint64_t simd_find_8x32(const simd_8x32_t *simd, char key)
{
  const __m128i k = _mm_set1_epi8(key);
  const __m128i r0 = _mm_cmpeq_epi8(simd->chunks[0], k);
  const __m128i r1 = _mm_cmpeq_epi8(simd->chunks[1], k);
  const uint32_t m0 = (uint16_t)_mm_movemask_epi8(r0);
  const uint32_t m1 = (uint16_t)_mm_movemask_epi8(r1);
  return m0 | (m1 << 16);
}

nonnull_all
static really_inline void simd_loadu_8x64(simd_8x64_t *simd, const uint8_t *address)
{
  simd->chunks[0] = _mm_loadu_si128((const __m128i *)(address));
  simd->chunks[1] = _mm_loadu_si128((const __m128i *)(address+16));
  simd->chunks[2] = _mm_loadu_si128((const __m128i *)(address+32));
  simd->chunks[3] = _mm_loadu_si128((const __m128i *)(address+48));
}

nonnull_all
static really_inline uint64_t simd_find_8x64(const simd_8x64_t *simd, char key)
{
  const __m128i k = _mm_set1_epi8(key);

  const __m128i r0 = _mm_cmpeq_epi8(simd->chunks[0], k);
  const __m128i r1 = _mm_cmpeq_epi8(simd->chunks[1], k);
  const __m128i r2 = _mm_cmpeq_epi8(simd->chunks[2], k);
  const __m128i r3 = _mm_cmpeq_epi8(simd->chunks[3], k);

  const uint64_t m0 = (uint16_t)_mm_movemask_epi8(r0);
  const uint64_t m1 = (uint16_t)_mm_movemask_epi8(r1);
  const uint64_t m2 = (uint16_t)_mm_movemask_epi8(r2);
  const uint64_t m3 = (uint16_t)_mm_movemask_epi8(r3);

  return m0 | (m1 << 16) | (m2 << 32) | (m3 << 48);
}

nonnull_all
static really_inline uint64_t simd_find_any_8x64(
  const simd_8x64_t *simd, const simd_table_t table)
{
  const __m128i t = _mm_loadu_si128((const __m128i *)table);

  const __m128i r0 = _mm_cmpeq_epi8(
    _mm_shuffle_epi8(t, simd->chunks[0]), simd->chunks[0]);
  const __m128i r1 = _mm_cmpeq_epi8(
    _mm_shuffle_epi8(t, simd->chunks[1]), simd->chunks[1]);
  const __m128i r2 = _mm_cmpeq_epi8(
    _mm_shuffle_epi8(t, simd->chunks[2]), simd->chunks[2]);
  const __m128i r3 = _mm_cmpeq_epi8(
    _mm_shuffle_epi8(t, simd->chunks[3]), simd->chunks[3]);

  const uint64_t m0 = (uint16_t)_mm_movemask_epi8(r0);
  const uint64_t m1 = (uint16_t)_mm_movemask_epi8(r1);
  const uint64_t m2 = (uint16_t)_mm_movemask_epi8(r2);
  const uint64_t m3 = (uint16_t)_mm_movemask_epi8(r3);

  return m0 | (m1 << 16) | (m2 << 32) | (m3 << 48);
}

#endif // SIMD_H
