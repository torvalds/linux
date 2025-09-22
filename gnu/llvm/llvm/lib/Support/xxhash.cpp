/*
 *  xxHash - Extremely Fast Hash algorithm
 *  Copyright (C) 2012-2023, Yann Collet
 *
 *  BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following disclaimer
 *  in the documentation and/or other materials provided with the
 *  distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You can contact the author at :
 *  - xxHash homepage: http://www.xxhash.com
 *  - xxHash source repository : https://github.com/Cyan4973/xxHash
 */

// xxhash64 is based on commit d2df04efcbef7d7f6886d345861e5dfda4edacc1. Removed
// everything but a simple interface for computing xxh64.

// xxh3_64bits is based on commit d5891596637d21366b9b1dcf2c0007a3edb26a9e (July
// 2023).

// xxh3_128bits is based on commit b0adcc54188c3130b1793e7b19c62eb1e669f7df
// (June 2024).

#include "llvm/Support/xxhash.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

#include <stdlib.h>

#if !defined(LLVM_XXH_USE_NEON)
#if (defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)) &&      \
    !defined(__ARM_BIG_ENDIAN)
#define LLVM_XXH_USE_NEON 1
#else
#define LLVM_XXH_USE_NEON 0
#endif
#endif

#if LLVM_XXH_USE_NEON
#include <arm_neon.h>
#endif

using namespace llvm;
using namespace support;

static uint64_t rotl64(uint64_t X, size_t R) {
  return (X << R) | (X >> (64 - R));
}

constexpr uint32_t PRIME32_1 = 0x9E3779B1;
constexpr uint32_t PRIME32_2 = 0x85EBCA77;
constexpr uint32_t PRIME32_3 = 0xC2B2AE3D;

static const uint64_t PRIME64_1 = 11400714785074694791ULL;
static const uint64_t PRIME64_2 = 14029467366897019727ULL;
static const uint64_t PRIME64_3 = 1609587929392839161ULL;
static const uint64_t PRIME64_4 = 9650029242287828579ULL;
static const uint64_t PRIME64_5 = 2870177450012600261ULL;

static uint64_t round(uint64_t Acc, uint64_t Input) {
  Acc += Input * PRIME64_2;
  Acc = rotl64(Acc, 31);
  Acc *= PRIME64_1;
  return Acc;
}

static uint64_t mergeRound(uint64_t Acc, uint64_t Val) {
  Val = round(0, Val);
  Acc ^= Val;
  Acc = Acc * PRIME64_1 + PRIME64_4;
  return Acc;
}

static uint64_t XXH64_avalanche(uint64_t hash) {
  hash ^= hash >> 33;
  hash *= PRIME64_2;
  hash ^= hash >> 29;
  hash *= PRIME64_3;
  hash ^= hash >> 32;
  return hash;
}

uint64_t llvm::xxHash64(StringRef Data) {
  size_t Len = Data.size();
  uint64_t Seed = 0;
  const unsigned char *P = Data.bytes_begin();
  const unsigned char *const BEnd = Data.bytes_end();
  uint64_t H64;

  if (Len >= 32) {
    const unsigned char *const Limit = BEnd - 32;
    uint64_t V1 = Seed + PRIME64_1 + PRIME64_2;
    uint64_t V2 = Seed + PRIME64_2;
    uint64_t V3 = Seed + 0;
    uint64_t V4 = Seed - PRIME64_1;

    do {
      V1 = round(V1, endian::read64le(P));
      P += 8;
      V2 = round(V2, endian::read64le(P));
      P += 8;
      V3 = round(V3, endian::read64le(P));
      P += 8;
      V4 = round(V4, endian::read64le(P));
      P += 8;
    } while (P <= Limit);

    H64 = rotl64(V1, 1) + rotl64(V2, 7) + rotl64(V3, 12) + rotl64(V4, 18);
    H64 = mergeRound(H64, V1);
    H64 = mergeRound(H64, V2);
    H64 = mergeRound(H64, V3);
    H64 = mergeRound(H64, V4);

  } else {
    H64 = Seed + PRIME64_5;
  }

  H64 += (uint64_t)Len;

  while (reinterpret_cast<uintptr_t>(P) + 8 <=
         reinterpret_cast<uintptr_t>(BEnd)) {
    uint64_t const K1 = round(0, endian::read64le(P));
    H64 ^= K1;
    H64 = rotl64(H64, 27) * PRIME64_1 + PRIME64_4;
    P += 8;
  }

  if (reinterpret_cast<uintptr_t>(P) + 4 <= reinterpret_cast<uintptr_t>(BEnd)) {
    H64 ^= (uint64_t)(endian::read32le(P)) * PRIME64_1;
    H64 = rotl64(H64, 23) * PRIME64_2 + PRIME64_3;
    P += 4;
  }

  while (P < BEnd) {
    H64 ^= (*P) * PRIME64_5;
    H64 = rotl64(H64, 11) * PRIME64_1;
    P++;
  }

  return XXH64_avalanche(H64);
}

uint64_t llvm::xxHash64(ArrayRef<uint8_t> Data) {
  return xxHash64({(const char *)Data.data(), Data.size()});
}

constexpr size_t XXH3_SECRETSIZE_MIN = 136;
constexpr size_t XXH_SECRET_DEFAULT_SIZE = 192;

/* Pseudorandom data taken directly from FARSH */
// clang-format off
constexpr uint8_t kSecret[XXH_SECRET_DEFAULT_SIZE] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};
// clang-format on

constexpr uint64_t PRIME_MX1 = 0x165667919E3779F9;
constexpr uint64_t PRIME_MX2 = 0x9FB21C651E98DF25;

// Calculates a 64-bit to 128-bit multiply, then XOR folds it.
static uint64_t XXH3_mul128_fold64(uint64_t lhs, uint64_t rhs) {
#if defined(__SIZEOF_INT128__) ||                                              \
    (defined(_INTEGRAL_MAX_BITS) && _INTEGRAL_MAX_BITS >= 128)
  __uint128_t product = (__uint128_t)lhs * (__uint128_t)rhs;
  return uint64_t(product) ^ uint64_t(product >> 64);

#else
  /* First calculate all of the cross products. */
  const uint64_t lo_lo = (lhs & 0xFFFFFFFF) * (rhs & 0xFFFFFFFF);
  const uint64_t hi_lo = (lhs >> 32) * (rhs & 0xFFFFFFFF);
  const uint64_t lo_hi = (lhs & 0xFFFFFFFF) * (rhs >> 32);
  const uint64_t hi_hi = (lhs >> 32) * (rhs >> 32);

  /* Now add the products together. These will never overflow. */
  const uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFF) + lo_hi;
  const uint64_t upper = (hi_lo >> 32) + (cross >> 32) + hi_hi;
  const uint64_t lower = (cross << 32) | (lo_lo & 0xFFFFFFFF);

  return upper ^ lower;
#endif
}

constexpr size_t XXH_STRIPE_LEN = 64;
constexpr size_t XXH_SECRET_CONSUME_RATE = 8;
constexpr size_t XXH_ACC_NB = XXH_STRIPE_LEN / sizeof(uint64_t);

static uint64_t XXH3_avalanche(uint64_t hash) {
  hash ^= hash >> 37;
  hash *= PRIME_MX1;
  hash ^= hash >> 32;
  return hash;
}

static uint64_t XXH3_len_1to3_64b(const uint8_t *input, size_t len,
                                  const uint8_t *secret, uint64_t seed) {
  const uint8_t c1 = input[0];
  const uint8_t c2 = input[len >> 1];
  const uint8_t c3 = input[len - 1];
  uint32_t combined = ((uint32_t)c1 << 16) | ((uint32_t)c2 << 24) |
                      ((uint32_t)c3 << 0) | ((uint32_t)len << 8);
  uint64_t bitflip =
      (uint64_t)(endian::read32le(secret) ^ endian::read32le(secret + 4)) +
      seed;
  return XXH64_avalanche(uint64_t(combined) ^ bitflip);
}

static uint64_t XXH3_len_4to8_64b(const uint8_t *input, size_t len,
                                  const uint8_t *secret, uint64_t seed) {
  seed ^= (uint64_t)byteswap(uint32_t(seed)) << 32;
  const uint32_t input1 = endian::read32le(input);
  const uint32_t input2 = endian::read32le(input + len - 4);
  uint64_t acc =
      (endian::read64le(secret + 8) ^ endian::read64le(secret + 16)) - seed;
  const uint64_t input64 = (uint64_t)input2 | ((uint64_t)input1 << 32);
  acc ^= input64;
  // XXH3_rrmxmx(acc, len)
  acc ^= rotl64(acc, 49) ^ rotl64(acc, 24);
  acc *= PRIME_MX2;
  acc ^= (acc >> 35) + (uint64_t)len;
  acc *= PRIME_MX2;
  return acc ^ (acc >> 28);
}

static uint64_t XXH3_len_9to16_64b(const uint8_t *input, size_t len,
                                   const uint8_t *secret, uint64_t const seed) {
  uint64_t input_lo =
      (endian::read64le(secret + 24) ^ endian::read64le(secret + 32)) + seed;
  uint64_t input_hi =
      (endian::read64le(secret + 40) ^ endian::read64le(secret + 48)) - seed;
  input_lo ^= endian::read64le(input);
  input_hi ^= endian::read64le(input + len - 8);
  uint64_t acc = uint64_t(len) + byteswap(input_lo) + input_hi +
                 XXH3_mul128_fold64(input_lo, input_hi);
  return XXH3_avalanche(acc);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static uint64_t XXH3_len_0to16_64b(const uint8_t *input, size_t len,
                                   const uint8_t *secret, uint64_t const seed) {
  if (LLVM_LIKELY(len > 8))
    return XXH3_len_9to16_64b(input, len, secret, seed);
  if (LLVM_LIKELY(len >= 4))
    return XXH3_len_4to8_64b(input, len, secret, seed);
  if (len != 0)
    return XXH3_len_1to3_64b(input, len, secret, seed);
  return XXH64_avalanche(seed ^ endian::read64le(secret + 56) ^
                         endian::read64le(secret + 64));
}

static uint64_t XXH3_mix16B(const uint8_t *input, uint8_t const *secret,
                            uint64_t seed) {
  uint64_t lhs = seed;
  uint64_t rhs = 0U - seed;
  lhs += endian::read64le(secret);
  rhs += endian::read64le(secret + 8);
  lhs ^= endian::read64le(input);
  rhs ^= endian::read64le(input + 8);
  return XXH3_mul128_fold64(lhs, rhs);
}

/* For mid range keys, XXH3 uses a Mum-hash variant. */
LLVM_ATTRIBUTE_ALWAYS_INLINE
static uint64_t XXH3_len_17to128_64b(const uint8_t *input, size_t len,
                                     const uint8_t *secret,
                                     uint64_t const seed) {
  uint64_t acc = len * PRIME64_1, acc_end;
  acc += XXH3_mix16B(input + 0, secret + 0, seed);
  acc_end = XXH3_mix16B(input + len - 16, secret + 16, seed);
  if (len > 32) {
    acc += XXH3_mix16B(input + 16, secret + 32, seed);
    acc_end += XXH3_mix16B(input + len - 32, secret + 48, seed);
    if (len > 64) {
      acc += XXH3_mix16B(input + 32, secret + 64, seed);
      acc_end += XXH3_mix16B(input + len - 48, secret + 80, seed);
      if (len > 96) {
        acc += XXH3_mix16B(input + 48, secret + 96, seed);
        acc_end += XXH3_mix16B(input + len - 64, secret + 112, seed);
      }
    }
  }
  return XXH3_avalanche(acc + acc_end);
}

constexpr size_t XXH3_MIDSIZE_MAX = 240;
constexpr size_t XXH3_MIDSIZE_STARTOFFSET = 3;
constexpr size_t XXH3_MIDSIZE_LASTOFFSET = 17;

LLVM_ATTRIBUTE_NOINLINE
static uint64_t XXH3_len_129to240_64b(const uint8_t *input, size_t len,
                                      const uint8_t *secret, uint64_t seed) {
  uint64_t acc = (uint64_t)len * PRIME64_1;
  const unsigned nbRounds = len / 16;
  for (unsigned i = 0; i < 8; ++i)
    acc += XXH3_mix16B(input + 16 * i, secret + 16 * i, seed);
  acc = XXH3_avalanche(acc);

  for (unsigned i = 8; i < nbRounds; ++i) {
    acc += XXH3_mix16B(input + 16 * i,
                       secret + 16 * (i - 8) + XXH3_MIDSIZE_STARTOFFSET, seed);
  }
  /* last bytes */
  acc +=
      XXH3_mix16B(input + len - 16,
                  secret + XXH3_SECRETSIZE_MIN - XXH3_MIDSIZE_LASTOFFSET, seed);
  return XXH3_avalanche(acc);
}

#if LLVM_XXH_USE_NEON

#define XXH3_accumulate_512 XXH3_accumulate_512_neon
#define XXH3_scrambleAcc XXH3_scrambleAcc_neon

// NEON implementation based on commit a57f6cce2698049863af8c25787084ae0489d849
// (July 2024), with the following removed:
// - workaround for suboptimal codegen on older GCC
// - compiler barriers against instruction reordering
// - WebAssembly SIMD support
// - configurable split between NEON and scalar lanes (benchmarking shows no
//   penalty when fully doing SIMD on the Apple M1)

#if defined(__GNUC__) || defined(__clang__)
#define XXH_ALIASING __attribute__((__may_alias__))
#else
#define XXH_ALIASING /* nothing */
#endif

typedef uint64x2_t xxh_aliasing_uint64x2_t XXH_ALIASING;

LLVM_ATTRIBUTE_ALWAYS_INLINE static uint64x2_t XXH_vld1q_u64(void const *ptr) {
  return vreinterpretq_u64_u8(vld1q_u8((uint8_t const *)ptr));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static void XXH3_accumulate_512_neon(uint64_t *acc, const uint8_t *input,
                                     const uint8_t *secret) {
  xxh_aliasing_uint64x2_t *const xacc = (xxh_aliasing_uint64x2_t *)acc;

#ifdef __clang__
#pragma clang loop unroll(full)
#endif
  for (size_t i = 0; i < XXH_ACC_NB / 2; i += 2) {
    /* data_vec = input[i]; */
    uint64x2_t data_vec_1 = XXH_vld1q_u64(input + (i * 16));
    uint64x2_t data_vec_2 = XXH_vld1q_u64(input + ((i + 1) * 16));

    /* key_vec  = secret[i];  */
    uint64x2_t key_vec_1 = XXH_vld1q_u64(secret + (i * 16));
    uint64x2_t key_vec_2 = XXH_vld1q_u64(secret + ((i + 1) * 16));

    /* data_swap = swap(data_vec) */
    uint64x2_t data_swap_1 = vextq_u64(data_vec_1, data_vec_1, 1);
    uint64x2_t data_swap_2 = vextq_u64(data_vec_2, data_vec_2, 1);

    /* data_key = data_vec ^ key_vec; */
    uint64x2_t data_key_1 = veorq_u64(data_vec_1, key_vec_1);
    uint64x2_t data_key_2 = veorq_u64(data_vec_2, key_vec_2);

    /*
     * If we reinterpret the 64x2 vectors as 32x4 vectors, we can use a
     * de-interleave operation for 4 lanes in 1 step with `vuzpq_u32` to
     * get one vector with the low 32 bits of each lane, and one vector
     * with the high 32 bits of each lane.
     *
     * The intrinsic returns a double vector because the original ARMv7-a
     * instruction modified both arguments in place. AArch64 and SIMD128 emit
     * two instructions from this intrinsic.
     *
     *  [ dk11L | dk11H | dk12L | dk12H ] -> [ dk11L | dk12L | dk21L | dk22L ]
     *  [ dk21L | dk21H | dk22L | dk22H ] -> [ dk11H | dk12H | dk21H | dk22H ]
     */
    uint32x4x2_t unzipped = vuzpq_u32(vreinterpretq_u32_u64(data_key_1),
                                      vreinterpretq_u32_u64(data_key_2));

    /* data_key_lo = data_key & 0xFFFFFFFF */
    uint32x4_t data_key_lo = unzipped.val[0];
    /* data_key_hi = data_key >> 32 */
    uint32x4_t data_key_hi = unzipped.val[1];

    /*
     * Then, we can split the vectors horizontally and multiply which, as for
     * most widening intrinsics, have a variant that works on both high half
     * vectors for free on AArch64. A similar instruction is available on
     * SIMD128.
     *
     * sum = data_swap + (u64x2) data_key_lo * (u64x2) data_key_hi
     */
    uint64x2_t sum_1 = vmlal_u32(data_swap_1, vget_low_u32(data_key_lo),
                                 vget_low_u32(data_key_hi));
    uint64x2_t sum_2 = vmlal_u32(data_swap_2, vget_high_u32(data_key_lo),
                                 vget_high_u32(data_key_hi));

    /* xacc[i] = acc_vec + sum; */
    xacc[i] = vaddq_u64(xacc[i], sum_1);
    xacc[i + 1] = vaddq_u64(xacc[i + 1], sum_2);
  }
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static void XXH3_scrambleAcc_neon(uint64_t *acc, const uint8_t *secret) {
  xxh_aliasing_uint64x2_t *const xacc = (xxh_aliasing_uint64x2_t *)acc;

  /* { prime32_1, prime32_1 } */
  uint32x2_t const kPrimeLo = vdup_n_u32(PRIME32_1);
  /* { 0, prime32_1, 0, prime32_1 } */
  uint32x4_t const kPrimeHi =
      vreinterpretq_u32_u64(vdupq_n_u64((uint64_t)PRIME32_1 << 32));

  for (size_t i = 0; i < XXH_ACC_NB / 2; ++i) {
    /* xacc[i] ^= (xacc[i] >> 47); */
    uint64x2_t acc_vec = XXH_vld1q_u64(acc + (2 * i));
    uint64x2_t shifted = vshrq_n_u64(acc_vec, 47);
    uint64x2_t data_vec = veorq_u64(acc_vec, shifted);

    /* xacc[i] ^= secret[i]; */
    uint64x2_t key_vec = XXH_vld1q_u64(secret + (i * 16));
    uint64x2_t data_key = veorq_u64(data_vec, key_vec);

    /*
     * xacc[i] *= XXH_PRIME32_1
     *
     * Expanded version with portable NEON intrinsics
     *
     *    lo(x) * lo(y) + (hi(x) * lo(y) << 32)
     *
     * prod_hi = hi(data_key) * lo(prime) << 32
     *
     * Since we only need 32 bits of this multiply a trick can be used,
     * reinterpreting the vector as a uint32x4_t and multiplying by
     * { 0, prime, 0, prime } to cancel out the unwanted bits and avoid the
     * shift.
     */
    uint32x4_t prod_hi = vmulq_u32(vreinterpretq_u32_u64(data_key), kPrimeHi);

    /* Extract low bits for vmlal_u32  */
    uint32x2_t data_key_lo = vmovn_u64(data_key);

    /* xacc[i] = prod_hi + lo(data_key) * XXH_PRIME32_1; */
    xacc[i] = vmlal_u32(vreinterpretq_u64_u32(prod_hi), data_key_lo, kPrimeLo);
  }
}
#else

#define XXH3_accumulate_512 XXH3_accumulate_512_scalar
#define XXH3_scrambleAcc XXH3_scrambleAcc_scalar

LLVM_ATTRIBUTE_ALWAYS_INLINE
static void XXH3_accumulate_512_scalar(uint64_t *acc, const uint8_t *input,
                                       const uint8_t *secret) {
  for (size_t i = 0; i < XXH_ACC_NB; ++i) {
    uint64_t data_val = endian::read64le(input + 8 * i);
    uint64_t data_key = data_val ^ endian::read64le(secret + 8 * i);
    acc[i ^ 1] += data_val;
    acc[i] += uint32_t(data_key) * (data_key >> 32);
  }
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static void XXH3_scrambleAcc_scalar(uint64_t *acc, const uint8_t *secret) {
  for (size_t i = 0; i < XXH_ACC_NB; ++i) {
    acc[i] ^= acc[i] >> 47;
    acc[i] ^= endian::read64le(secret + 8 * i);
    acc[i] *= PRIME32_1;
  }
}
#endif

LLVM_ATTRIBUTE_ALWAYS_INLINE
static void XXH3_accumulate(uint64_t *acc, const uint8_t *input,
                            const uint8_t *secret, size_t nbStripes) {
  for (size_t n = 0; n < nbStripes; ++n) {
    XXH3_accumulate_512(acc, input + n * XXH_STRIPE_LEN,
                        secret + n * XXH_SECRET_CONSUME_RATE);
  }
}

static uint64_t XXH3_mix2Accs(const uint64_t *acc, const uint8_t *secret) {
  return XXH3_mul128_fold64(acc[0] ^ endian::read64le(secret),
                            acc[1] ^ endian::read64le(secret + 8));
}

static uint64_t XXH3_mergeAccs(const uint64_t *acc, const uint8_t *key,
                               uint64_t start) {
  uint64_t result64 = start;
  for (size_t i = 0; i < 4; ++i)
    result64 += XXH3_mix2Accs(acc + 2 * i, key + 16 * i);
  return XXH3_avalanche(result64);
}

LLVM_ATTRIBUTE_NOINLINE
static uint64_t XXH3_hashLong_64b(const uint8_t *input, size_t len,
                                  const uint8_t *secret, size_t secretSize) {
  const size_t nbStripesPerBlock =
      (secretSize - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
  const size_t block_len = XXH_STRIPE_LEN * nbStripesPerBlock;
  const size_t nb_blocks = (len - 1) / block_len;
  alignas(16) uint64_t acc[XXH_ACC_NB] = {
      PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3,
      PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1,
  };
  for (size_t n = 0; n < nb_blocks; ++n) {
    XXH3_accumulate(acc, input + n * block_len, secret, nbStripesPerBlock);
    XXH3_scrambleAcc(acc, secret + secretSize - XXH_STRIPE_LEN);
  }

  /* last partial block */
  const size_t nbStripes = (len - 1 - (block_len * nb_blocks)) / XXH_STRIPE_LEN;
  assert(nbStripes <= secretSize / XXH_SECRET_CONSUME_RATE);
  XXH3_accumulate(acc, input + nb_blocks * block_len, secret, nbStripes);

  /* last stripe */
  constexpr size_t XXH_SECRET_LASTACC_START = 7;
  XXH3_accumulate_512(acc, input + len - XXH_STRIPE_LEN,
                      secret + secretSize - XXH_STRIPE_LEN -
                          XXH_SECRET_LASTACC_START);

  /* converge into final hash */
  constexpr size_t XXH_SECRET_MERGEACCS_START = 11;
  return XXH3_mergeAccs(acc, secret + XXH_SECRET_MERGEACCS_START,
                        (uint64_t)len * PRIME64_1);
}

uint64_t llvm::xxh3_64bits(ArrayRef<uint8_t> data) {
  auto *in = data.data();
  size_t len = data.size();
  if (len <= 16)
    return XXH3_len_0to16_64b(in, len, kSecret, 0);
  if (len <= 128)
    return XXH3_len_17to128_64b(in, len, kSecret, 0);
  if (len <= XXH3_MIDSIZE_MAX)
    return XXH3_len_129to240_64b(in, len, kSecret, 0);
  return XXH3_hashLong_64b(in, len, kSecret, sizeof(kSecret));
}

/* ==========================================
 * XXH3 128 bits (a.k.a XXH128)
 * ==========================================
 * XXH3's 128-bit variant has better mixing and strength than the 64-bit
 * variant, even without counting the significantly larger output size.
 *
 * For example, extra steps are taken to avoid the seed-dependent collisions
 * in 17-240 byte inputs (See XXH3_mix16B and XXH128_mix32B).
 *
 * This strength naturally comes at the cost of some speed, especially on short
 * lengths. Note that longer hashes are about as fast as the 64-bit version
 * due to it using only a slight modification of the 64-bit loop.
 *
 * XXH128 is also more oriented towards 64-bit machines. It is still extremely
 * fast for a _128-bit_ hash on 32-bit (it usually clears XXH64).
 */

/*!
 * @internal
 * @def XXH_rotl32(x,r)
 * @brief 32-bit rotate left.
 *
 * @param x The 32-bit integer to be rotated.
 * @param r The number of bits to rotate.
 * @pre
 *   @p r > 0 && @p r < 32
 * @note
 *   @p x and @p r may be evaluated multiple times.
 * @return The rotated result.
 */
#if __has_builtin(__builtin_rotateleft32) &&                                   \
    __has_builtin(__builtin_rotateleft64)
#define XXH_rotl32 __builtin_rotateleft32
#define XXH_rotl64 __builtin_rotateleft64
/* Note: although _rotl exists for minGW (GCC under windows), performance seems
 * poor */
#elif defined(_MSC_VER)
#define XXH_rotl32(x, r) _rotl(x, r)
#define XXH_rotl64(x, r) _rotl64(x, r)
#else
#define XXH_rotl32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))
#define XXH_rotl64(x, r) (((x) << (r)) | ((x) >> (64 - (r))))
#endif

#define XXH_mult32to64(x, y) ((uint64_t)(uint32_t)(x) * (uint64_t)(uint32_t)(y))

/*!
 * @brief Calculates a 64->128-bit long multiply.
 *
 * Uses `__uint128_t` and `_umul128` if available, otherwise uses a scalar
 * version.
 *
 * @param lhs , rhs The 64-bit integers to be multiplied
 * @return The 128-bit result represented in an @ref XXH128_hash_t.
 */
static XXH128_hash_t XXH_mult64to128(uint64_t lhs, uint64_t rhs) {
  /*
   * GCC/Clang __uint128_t method.
   *
   * On most 64-bit targets, GCC and Clang define a __uint128_t type.
   * This is usually the best way as it usually uses a native long 64-bit
   * multiply, such as MULQ on x86_64 or MUL + UMULH on aarch64.
   *
   * Usually.
   *
   * Despite being a 32-bit platform, Clang (and emscripten) define this type
   * despite not having the arithmetic for it. This results in a laggy
   * compiler builtin call which calculates a full 128-bit multiply.
   * In that case it is best to use the portable one.
   * https://github.com/Cyan4973/xxHash/issues/211#issuecomment-515575677
   */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__wasm__) &&         \
        defined(__SIZEOF_INT128__) ||                                          \
    (defined(_INTEGRAL_MAX_BITS) && _INTEGRAL_MAX_BITS >= 128)

  __uint128_t const product = (__uint128_t)lhs * (__uint128_t)rhs;
  XXH128_hash_t r128;
  r128.low64 = (uint64_t)(product);
  r128.high64 = (uint64_t)(product >> 64);
  return r128;

  /*
   * MSVC for x64's _umul128 method.
   *
   * uint64_t _umul128(uint64_t Multiplier, uint64_t Multiplicand, uint64_t
   * *HighProduct);
   *
   * This compiles to single operand MUL on x64.
   */
#elif (defined(_M_X64) || defined(_M_IA64)) && !defined(_M_ARM64EC)

#ifndef _MSC_VER
#pragma intrinsic(_umul128)
#endif
  uint64_t product_high;
  uint64_t const product_low = _umul128(lhs, rhs, &product_high);
  XXH128_hash_t r128;
  r128.low64 = product_low;
  r128.high64 = product_high;
  return r128;

  /*
   * MSVC for ARM64's __umulh method.
   *
   * This compiles to the same MUL + UMULH as GCC/Clang's __uint128_t method.
   */
#elif defined(_M_ARM64) || defined(_M_ARM64EC)

#ifndef _MSC_VER
#pragma intrinsic(__umulh)
#endif
  XXH128_hash_t r128;
  r128.low64 = lhs * rhs;
  r128.high64 = __umulh(lhs, rhs);
  return r128;

#else
  /*
   * Portable scalar method. Optimized for 32-bit and 64-bit ALUs.
   *
   * This is a fast and simple grade school multiply, which is shown below
   * with base 10 arithmetic instead of base 0x100000000.
   *
   *           9 3 // D2 lhs = 93
   *         x 7 5 // D2 rhs = 75
   *     ----------
   *           1 5 // D2 lo_lo = (93 % 10) * (75 % 10) = 15
   *         4 5 | // D2 hi_lo = (93 / 10) * (75 % 10) = 45
   *         2 1 | // D2 lo_hi = (93 % 10) * (75 / 10) = 21
   *     + 6 3 | | // D2 hi_hi = (93 / 10) * (75 / 10) = 63
   *     ---------
   *         2 7 | // D2 cross = (15 / 10) + (45 % 10) + 21 = 27
   *     + 6 7 | | // D2 upper = (27 / 10) + (45 / 10) + 63 = 67
   *     ---------
   *       6 9 7 5 // D4 res = (27 * 10) + (15 % 10) + (67 * 100) = 6975
   *
   * The reasons for adding the products like this are:
   *  1. It avoids manual carry tracking. Just like how
   *     (9 * 9) + 9 + 9 = 99, the same applies with this for UINT64_MAX.
   *     This avoids a lot of complexity.
   *
   *  2. It hints for, and on Clang, compiles to, the powerful UMAAL
   *     instruction available in ARM's Digital Signal Processing extension
   *     in 32-bit ARMv6 and later, which is shown below:
   *
   *         void UMAAL(xxh_u32 *RdLo, xxh_u32 *RdHi, xxh_u32 Rn, xxh_u32 Rm)
   *         {
   *             uint64_t product = (uint64_t)*RdLo * (uint64_t)*RdHi + Rn + Rm;
   *             *RdLo = (xxh_u32)(product & 0xFFFFFFFF);
   *             *RdHi = (xxh_u32)(product >> 32);
   *         }
   *
   *     This instruction was designed for efficient long multiplication, and
   *     allows this to be calculated in only 4 instructions at speeds
   *     comparable to some 64-bit ALUs.
   *
   *  3. It isn't terrible on other platforms. Usually this will be a couple
   *     of 32-bit ADD/ADCs.
   */

  /* First calculate all of the cross products. */
  uint64_t const lo_lo = XXH_mult32to64(lhs & 0xFFFFFFFF, rhs & 0xFFFFFFFF);
  uint64_t const hi_lo = XXH_mult32to64(lhs >> 32, rhs & 0xFFFFFFFF);
  uint64_t const lo_hi = XXH_mult32to64(lhs & 0xFFFFFFFF, rhs >> 32);
  uint64_t const hi_hi = XXH_mult32to64(lhs >> 32, rhs >> 32);

  /* Now add the products together. These will never overflow. */
  uint64_t const cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFF) + lo_hi;
  uint64_t const upper = (hi_lo >> 32) + (cross >> 32) + hi_hi;
  uint64_t const lower = (cross << 32) | (lo_lo & 0xFFFFFFFF);

  XXH128_hash_t r128;
  r128.low64 = lower;
  r128.high64 = upper;
  return r128;
#endif
}

/*! Seems to produce slightly better code on GCC for some reason. */
LLVM_ATTRIBUTE_ALWAYS_INLINE constexpr uint64_t XXH_xorshift64(uint64_t v64,
                                                               int shift) {
  return v64 ^ (v64 >> shift);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static XXH128_hash_t
XXH3_len_1to3_128b(const uint8_t *input, size_t len, const uint8_t *secret,
                   uint64_t seed) {
  /* A doubled version of 1to3_64b with different constants. */
  /*
   * len = 1: combinedl = { input[0], 0x01, input[0], input[0] }
   * len = 2: combinedl = { input[1], 0x02, input[0], input[1] }
   * len = 3: combinedl = { input[2], 0x03, input[0], input[1] }
   */
  uint8_t const c1 = input[0];
  uint8_t const c2 = input[len >> 1];
  uint8_t const c3 = input[len - 1];
  uint32_t const combinedl = ((uint32_t)c1 << 16) | ((uint32_t)c2 << 24) |
                             ((uint32_t)c3 << 0) | ((uint32_t)len << 8);
  uint32_t const combinedh = XXH_rotl32(byteswap(combinedl), 13);
  uint64_t const bitflipl =
      (endian::read32le(secret) ^ endian::read32le(secret + 4)) + seed;
  uint64_t const bitfliph =
      (endian::read32le(secret + 8) ^ endian::read32le(secret + 12)) - seed;
  uint64_t const keyed_lo = (uint64_t)combinedl ^ bitflipl;
  uint64_t const keyed_hi = (uint64_t)combinedh ^ bitfliph;
  XXH128_hash_t h128;
  h128.low64 = XXH64_avalanche(keyed_lo);
  h128.high64 = XXH64_avalanche(keyed_hi);
  return h128;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static XXH128_hash_t
XXH3_len_4to8_128b(const uint8_t *input, size_t len, const uint8_t *secret,
                   uint64_t seed) {
  seed ^= (uint64_t)byteswap((uint32_t)seed) << 32;
  uint32_t const input_lo = endian::read32le(input);
  uint32_t const input_hi = endian::read32le(input + len - 4);
  uint64_t const input_64 = input_lo + ((uint64_t)input_hi << 32);
  uint64_t const bitflip =
      (endian::read64le(secret + 16) ^ endian::read64le(secret + 24)) + seed;
  uint64_t const keyed = input_64 ^ bitflip;

  /* Shift len to the left to ensure it is even, this avoids even multiplies.
   */
  XXH128_hash_t m128 = XXH_mult64to128(keyed, PRIME64_1 + (len << 2));

  m128.high64 += (m128.low64 << 1);
  m128.low64 ^= (m128.high64 >> 3);

  m128.low64 = XXH_xorshift64(m128.low64, 35);
  m128.low64 *= PRIME_MX2;
  m128.low64 = XXH_xorshift64(m128.low64, 28);
  m128.high64 = XXH3_avalanche(m128.high64);
  return m128;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static XXH128_hash_t
XXH3_len_9to16_128b(const uint8_t *input, size_t len, const uint8_t *secret,
                    uint64_t seed) {
  uint64_t const bitflipl =
      (endian::read64le(secret + 32) ^ endian::read64le(secret + 40)) - seed;
  uint64_t const bitfliph =
      (endian::read64le(secret + 48) ^ endian::read64le(secret + 56)) + seed;
  uint64_t const input_lo = endian::read64le(input);
  uint64_t input_hi = endian::read64le(input + len - 8);
  XXH128_hash_t m128 =
      XXH_mult64to128(input_lo ^ input_hi ^ bitflipl, PRIME64_1);
  /*
   * Put len in the middle of m128 to ensure that the length gets mixed to
   * both the low and high bits in the 128x64 multiply below.
   */
  m128.low64 += (uint64_t)(len - 1) << 54;
  input_hi ^= bitfliph;
  /*
   * Add the high 32 bits of input_hi to the high 32 bits of m128, then
   * add the long product of the low 32 bits of input_hi and PRIME32_2 to
   * the high 64 bits of m128.
   *
   * The best approach to this operation is different on 32-bit and 64-bit.
   */
  if (sizeof(void *) < sizeof(uint64_t)) { /* 32-bit */
    /*
     * 32-bit optimized version, which is more readable.
     *
     * On 32-bit, it removes an ADC and delays a dependency between the two
     * halves of m128.high64, but it generates an extra mask on 64-bit.
     */
    m128.high64 += (input_hi & 0xFFFFFFFF00000000ULL) +
                   XXH_mult32to64((uint32_t)input_hi, PRIME32_2);
  } else {
    /*
     * 64-bit optimized (albeit more confusing) version.
     *
     * Uses some properties of addition and multiplication to remove the mask:
     *
     * Let:
     *    a = input_hi.lo = (input_hi & 0x00000000FFFFFFFF)
     *    b = input_hi.hi = (input_hi & 0xFFFFFFFF00000000)
     *    c = PRIME32_2
     *
     *    a + (b * c)
     * Inverse Property: x + y - x == y
     *    a + (b * (1 + c - 1))
     * Distributive Property: x * (y + z) == (x * y) + (x * z)
     *    a + (b * 1) + (b * (c - 1))
     * Identity Property: x * 1 == x
     *    a + b + (b * (c - 1))
     *
     * Substitute a, b, and c:
     *    input_hi.hi + input_hi.lo + ((uint64_t)input_hi.lo * (PRIME32_2
     * - 1))
     *
     * Since input_hi.hi + input_hi.lo == input_hi, we get this:
     *    input_hi + ((uint64_t)input_hi.lo * (PRIME32_2 - 1))
     */
    m128.high64 += input_hi + XXH_mult32to64((uint32_t)input_hi, PRIME32_2 - 1);
  }
  /* m128 ^= XXH_swap64(m128 >> 64); */
  m128.low64 ^= byteswap(m128.high64);

  /* 128x64 multiply: h128 = m128 * PRIME64_2; */
  XXH128_hash_t h128 = XXH_mult64to128(m128.low64, PRIME64_2);
  h128.high64 += m128.high64 * PRIME64_2;

  h128.low64 = XXH3_avalanche(h128.low64);
  h128.high64 = XXH3_avalanche(h128.high64);
  return h128;
}

/*
 * Assumption: `secret` size is >= XXH3_SECRET_SIZE_MIN
 */
LLVM_ATTRIBUTE_ALWAYS_INLINE static XXH128_hash_t
XXH3_len_0to16_128b(const uint8_t *input, size_t len, const uint8_t *secret,
                    uint64_t seed) {
  if (len > 8)
    return XXH3_len_9to16_128b(input, len, secret, seed);
  if (len >= 4)
    return XXH3_len_4to8_128b(input, len, secret, seed);
  if (len)
    return XXH3_len_1to3_128b(input, len, secret, seed);
  XXH128_hash_t h128;
  uint64_t const bitflipl =
      endian::read64le(secret + 64) ^ endian::read64le(secret + 72);
  uint64_t const bitfliph =
      endian::read64le(secret + 80) ^ endian::read64le(secret + 88);
  h128.low64 = XXH64_avalanche(seed ^ bitflipl);
  h128.high64 = XXH64_avalanche(seed ^ bitfliph);
  return h128;
}

/*
 * A bit slower than XXH3_mix16B, but handles multiply by zero better.
 */
LLVM_ATTRIBUTE_ALWAYS_INLINE static XXH128_hash_t
XXH128_mix32B(XXH128_hash_t acc, const uint8_t *input_1, const uint8_t *input_2,
              const uint8_t *secret, uint64_t seed) {
  acc.low64 += XXH3_mix16B(input_1, secret + 0, seed);
  acc.low64 ^= endian::read64le(input_2) + endian::read64le(input_2 + 8);
  acc.high64 += XXH3_mix16B(input_2, secret + 16, seed);
  acc.high64 ^= endian::read64le(input_1) + endian::read64le(input_1 + 8);
  return acc;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static XXH128_hash_t
XXH3_len_17to128_128b(const uint8_t *input, size_t len, const uint8_t *secret,
                      size_t secretSize, uint64_t seed) {
  (void)secretSize;

  XXH128_hash_t acc;
  acc.low64 = len * PRIME64_1;
  acc.high64 = 0;

  if (len > 32) {
    if (len > 64) {
      if (len > 96) {
        acc =
            XXH128_mix32B(acc, input + 48, input + len - 64, secret + 96, seed);
      }
      acc = XXH128_mix32B(acc, input + 32, input + len - 48, secret + 64, seed);
    }
    acc = XXH128_mix32B(acc, input + 16, input + len - 32, secret + 32, seed);
  }
  acc = XXH128_mix32B(acc, input, input + len - 16, secret, seed);
  XXH128_hash_t h128;
  h128.low64 = acc.low64 + acc.high64;
  h128.high64 = (acc.low64 * PRIME64_1) + (acc.high64 * PRIME64_4) +
                ((len - seed) * PRIME64_2);
  h128.low64 = XXH3_avalanche(h128.low64);
  h128.high64 = (uint64_t)0 - XXH3_avalanche(h128.high64);
  return h128;
}

LLVM_ATTRIBUTE_NOINLINE static XXH128_hash_t
XXH3_len_129to240_128b(const uint8_t *input, size_t len, const uint8_t *secret,
                       size_t secretSize, uint64_t seed) {
  (void)secretSize;

  XXH128_hash_t acc;
  unsigned i;
  acc.low64 = len * PRIME64_1;
  acc.high64 = 0;
  /*
   *  We set as `i` as offset + 32. We do this so that unchanged
   * `len` can be used as upper bound. This reaches a sweet spot
   * where both x86 and aarch64 get simple agen and good codegen
   * for the loop.
   */
  for (i = 32; i < 160; i += 32) {
    acc = XXH128_mix32B(acc, input + i - 32, input + i - 16, secret + i - 32,
                        seed);
  }
  acc.low64 = XXH3_avalanche(acc.low64);
  acc.high64 = XXH3_avalanche(acc.high64);
  /*
   * NB: `i <= len` will duplicate the last 32-bytes if
   * len % 32 was zero. This is an unfortunate necessity to keep
   * the hash result stable.
   */
  for (i = 160; i <= len; i += 32) {
    acc = XXH128_mix32B(acc, input + i - 32, input + i - 16,
                        secret + XXH3_MIDSIZE_STARTOFFSET + i - 160, seed);
  }
  /* last bytes */
  acc =
      XXH128_mix32B(acc, input + len - 16, input + len - 32,
                    secret + XXH3_SECRETSIZE_MIN - XXH3_MIDSIZE_LASTOFFSET - 16,
                    (uint64_t)0 - seed);

  XXH128_hash_t h128;
  h128.low64 = acc.low64 + acc.high64;
  h128.high64 = (acc.low64 * PRIME64_1) + (acc.high64 * PRIME64_4) +
                ((len - seed) * PRIME64_2);
  h128.low64 = XXH3_avalanche(h128.low64);
  h128.high64 = (uint64_t)0 - XXH3_avalanche(h128.high64);
  return h128;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE XXH128_hash_t
XXH3_hashLong_128b(const uint8_t *input, size_t len, const uint8_t *secret,
                   size_t secretSize) {
  const size_t nbStripesPerBlock =
      (secretSize - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
  const size_t block_len = XXH_STRIPE_LEN * nbStripesPerBlock;
  const size_t nb_blocks = (len - 1) / block_len;
  alignas(16) uint64_t acc[XXH_ACC_NB] = {
      PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3,
      PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1,
  };

  for (size_t n = 0; n < nb_blocks; ++n) {
    XXH3_accumulate(acc, input + n * block_len, secret, nbStripesPerBlock);
    XXH3_scrambleAcc(acc, secret + secretSize - XXH_STRIPE_LEN);
  }

  /* last partial block */
  const size_t nbStripes = (len - 1 - (block_len * nb_blocks)) / XXH_STRIPE_LEN;
  assert(nbStripes <= secretSize / XXH_SECRET_CONSUME_RATE);
  XXH3_accumulate(acc, input + nb_blocks * block_len, secret, nbStripes);

  /* last stripe */
  constexpr size_t XXH_SECRET_LASTACC_START = 7;
  XXH3_accumulate_512(acc, input + len - XXH_STRIPE_LEN,
                      secret + secretSize - XXH_STRIPE_LEN -
                          XXH_SECRET_LASTACC_START);

  /* converge into final hash */
  static_assert(sizeof(acc) == 64);
  XXH128_hash_t h128;
  constexpr size_t XXH_SECRET_MERGEACCS_START = 11;
  h128.low64 = XXH3_mergeAccs(acc, secret + XXH_SECRET_MERGEACCS_START,
                              (uint64_t)len * PRIME64_1);
  h128.high64 = XXH3_mergeAccs(
      acc, secret + secretSize - sizeof(acc) - XXH_SECRET_MERGEACCS_START,
      ~((uint64_t)len * PRIME64_2));
  return h128;
}

llvm::XXH128_hash_t llvm::xxh3_128bits(ArrayRef<uint8_t> data) {
  size_t len = data.size();
  const uint8_t *input = data.data();

  /*
   * If an action is to be taken if `secret` conditions are not respected,
   * it should be done here.
   * For now, it's a contract pre-condition.
   * Adding a check and a branch here would cost performance at every hash.
   */
  if (len <= 16)
    return XXH3_len_0to16_128b(input, len, kSecret, /*seed64=*/0);
  if (len <= 128)
    return XXH3_len_17to128_128b(input, len, kSecret, sizeof(kSecret),
                                 /*seed64=*/0);
  if (len <= XXH3_MIDSIZE_MAX)
    return XXH3_len_129to240_128b(input, len, kSecret, sizeof(kSecret),
                                  /*seed64=*/0);
  return XXH3_hashLong_128b(input, len, kSecret, sizeof(kSecret));
}
