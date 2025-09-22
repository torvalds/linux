/*
 * bits.h -- bit manipulation instructions
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef BITS_H
#define BITS_H

#if _MSC_VER
#include <intrin.h>

static really_inline uint64_t trailing_zeroes(uint64_t mask)
{
  unsigned long index;
  if (_BitScanForward64(&index, mask))
    return index;
  else
    return 64;
}

static really_inline uint64_t leading_zeroes(uint64_t mask)
{
  unsigned long index;
  if (_BitScanReverse64(&index, mask))
    return 63 - index;
  else
    return 64;
}

#else

static really_inline uint64_t trailing_zeroes(uint64_t mask)
{
#if has_builtin(__builtin_ctzll)
  return (uint64_t)__builtin_ctzll(mask);
#else
  // Code by Kim Walish from https://www.chessprogramming.org/BitScan.
  // Distributed under CC BY-SA 3.0.
  static const uint64_t magic = 0x03f79d71b4cb0a89ull;
  const int magictable[64] = {
      0, 47,  1, 56, 48, 27,  2, 60,
     57, 49, 41, 37, 28, 16,  3, 61,
     54, 58, 35, 52, 50, 42, 21, 44,
     38, 32, 29, 23, 17, 11,  4, 62,
     46, 55, 26, 59, 40, 36, 15, 53,
     34, 51, 20, 43, 31, 22, 10, 45,
     25, 39, 14, 33, 19, 30,  9, 24,
     13, 18,  8, 12,  7,  6,  5, 63
  };

  return magictable[((mask ^ (mask - 1)) * magic) >> 58];
#endif
}

static really_inline uint64_t leading_zeroes(uint64_t mask)
{
#if has_builtin(__builtin_clzll)
  return (uint64_t)__builtin_clzll(mask);
#else
  // Code by Kim Walish from https://www.chessprogramming.org/BitScan.
  // Distributed under CC BY-SA 3.0.
  static const uint64_t magic = 0x03f79d71b4cb0a89ull;
  const int magictable[64] = {
     63, 16, 62,  7, 15, 36, 61,  3,
      6, 14, 22, 26, 35, 47, 60,  2,
      9,  5, 28, 11, 13, 21, 42, 19,
     25, 31, 34, 40, 46, 52, 59,  1,
     17,  8, 37,  4, 23, 27, 48, 10,
     29, 12, 43, 20, 32, 41, 53, 18,
     38, 24, 49, 30, 44, 33, 54, 39,
     50, 45, 55, 51, 56, 57, 58,  0
  };

  mask |= mask >> 1;
  mask |= mask >> 2;
  mask |= mask >> 4;
  mask |= mask >> 8;
  mask |= mask >> 16;
  mask |= mask >> 32;

  return magictable[(mask * magic) >> 58];
#endif
}
#endif // _MSC_VER
#endif // BITS_H
