//===--- SipHash.cpp - An ABI-stable string hash --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements an ABI-stable string hash based on SipHash, used to
//  compute ptrauth discriminators.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SipHash.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include <cstdint>

using namespace llvm;
using namespace support;

#define DEBUG_TYPE "llvm-siphash"

// Lightly adapted from the SipHash reference C implementation:
//   https://github.com/veorq/SipHash
// by Jean-Philippe Aumasson and Daniel J. Bernstein

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND                                                               \
  do {                                                                         \
    v0 += v1;                                                                  \
    v1 = ROTL(v1, 13);                                                         \
    v1 ^= v0;                                                                  \
    v0 = ROTL(v0, 32);                                                         \
    v2 += v3;                                                                  \
    v3 = ROTL(v3, 16);                                                         \
    v3 ^= v2;                                                                  \
    v0 += v3;                                                                  \
    v3 = ROTL(v3, 21);                                                         \
    v3 ^= v0;                                                                  \
    v2 += v1;                                                                  \
    v1 = ROTL(v1, 17);                                                         \
    v1 ^= v2;                                                                  \
    v2 = ROTL(v2, 32);                                                         \
  } while (0)

namespace {

/// Computes a SipHash value
///
/// \param in: pointer to input data (read-only)
/// \param inlen: input data length in bytes (any size_t value)
/// \param k: reference to the key data 16-byte array (read-only)
/// \returns output data, must be 8 or 16 bytes
///
template <int cROUNDS, int dROUNDS, size_t outlen>
void siphash(const unsigned char *in, uint64_t inlen,
             const unsigned char (&k)[16], unsigned char (&out)[outlen]) {

  const unsigned char *ni = (const unsigned char *)in;
  const unsigned char *kk = (const unsigned char *)k;

  static_assert(outlen == 8 || outlen == 16, "result should be 8 or 16 bytes");

  uint64_t v0 = UINT64_C(0x736f6d6570736575);
  uint64_t v1 = UINT64_C(0x646f72616e646f6d);
  uint64_t v2 = UINT64_C(0x6c7967656e657261);
  uint64_t v3 = UINT64_C(0x7465646279746573);
  uint64_t k0 = endian::read64le(kk);
  uint64_t k1 = endian::read64le(kk + 8);
  uint64_t m;
  int i;
  const unsigned char *end = ni + inlen - (inlen % sizeof(uint64_t));
  const int left = inlen & 7;
  uint64_t b = ((uint64_t)inlen) << 56;
  v3 ^= k1;
  v2 ^= k0;
  v1 ^= k1;
  v0 ^= k0;

  if (outlen == 16)
    v1 ^= 0xee;

  for (; ni != end; ni += 8) {
    m = endian::read64le(ni);
    v3 ^= m;

    for (i = 0; i < cROUNDS; ++i)
      SIPROUND;

    v0 ^= m;
  }

  switch (left) {
  case 7:
    b |= ((uint64_t)ni[6]) << 48;
    LLVM_FALLTHROUGH;
  case 6:
    b |= ((uint64_t)ni[5]) << 40;
    LLVM_FALLTHROUGH;
  case 5:
    b |= ((uint64_t)ni[4]) << 32;
    LLVM_FALLTHROUGH;
  case 4:
    b |= ((uint64_t)ni[3]) << 24;
    LLVM_FALLTHROUGH;
  case 3:
    b |= ((uint64_t)ni[2]) << 16;
    LLVM_FALLTHROUGH;
  case 2:
    b |= ((uint64_t)ni[1]) << 8;
    LLVM_FALLTHROUGH;
  case 1:
    b |= ((uint64_t)ni[0]);
    break;
  case 0:
    break;
  }

  v3 ^= b;

  for (i = 0; i < cROUNDS; ++i)
    SIPROUND;

  v0 ^= b;

  if (outlen == 16)
    v2 ^= 0xee;
  else
    v2 ^= 0xff;

  for (i = 0; i < dROUNDS; ++i)
    SIPROUND;

  b = v0 ^ v1 ^ v2 ^ v3;
  endian::write64le(out, b);

  if (outlen == 8)
    return;

  v1 ^= 0xdd;

  for (i = 0; i < dROUNDS; ++i)
    SIPROUND;

  b = v0 ^ v1 ^ v2 ^ v3;
  endian::write64le(out + 8, b);
}

} // end anonymous namespace

void llvm::getSipHash_2_4_64(ArrayRef<uint8_t> In, const uint8_t (&K)[16],
                             uint8_t (&Out)[8]) {
  siphash<2, 4>(In.data(), In.size(), K, Out);
}

void llvm::getSipHash_2_4_128(ArrayRef<uint8_t> In, const uint8_t (&K)[16],
                              uint8_t (&Out)[16]) {
  siphash<2, 4>(In.data(), In.size(), K, Out);
}

/// Compute an ABI-stable 16-bit hash of the given string.
uint16_t llvm::getPointerAuthStableSipHash(StringRef Str) {
  static const uint8_t K[16] = {0xb5, 0xd4, 0xc9, 0xeb, 0x79, 0x10, 0x4a, 0x79,
                                0x6f, 0xec, 0x8b, 0x1b, 0x42, 0x87, 0x81, 0xd4};

  uint8_t RawHashBytes[8];
  getSipHash_2_4_64(arrayRefFromStringRef(Str), K, RawHashBytes);
  uint64_t RawHash = endian::read64le(RawHashBytes);

  // Produce a non-zero 16-bit discriminator.
  uint16_t Discriminator = (RawHash % 0xFFFF) + 1;
  LLVM_DEBUG(
      dbgs() << "ptrauth stable hash discriminator: " << utostr(Discriminator)
             << " (0x"
             << utohexstr(Discriminator, /*Lowercase=*/false, /*Width=*/4)
             << ")"
             << " of: " << Str << "\n");
  return Discriminator;
}
