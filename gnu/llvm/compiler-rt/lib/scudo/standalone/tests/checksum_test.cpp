//===-- checksum_test.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "checksum.h"

#include <string.h>

static scudo::u16 computeSoftwareChecksum(scudo::u32 Seed, scudo::uptr *Array,
                                          scudo::uptr ArraySize) {
  scudo::u16 Checksum = static_cast<scudo::u16>(Seed & 0xffff);
  for (scudo::uptr I = 0; I < ArraySize; I++)
    Checksum = scudo::computeBSDChecksum(Checksum, Array[I]);
  return Checksum;
}

static scudo::u16 computeHardwareChecksum(scudo::u32 Seed, scudo::uptr *Array,
                                          scudo::uptr ArraySize) {
  scudo::u32 Crc = Seed;
  for (scudo::uptr I = 0; I < ArraySize; I++)
    Crc = scudo::computeHardwareCRC32(Crc, Array[I]);
  return static_cast<scudo::u16>((Crc & 0xffff) ^ (Crc >> 16));
}

typedef scudo::u16 (*ComputeChecksum)(scudo::u32, scudo::uptr *, scudo::uptr);

// This verifies that flipping bits in the data being checksummed produces a
// different checksum. We do not use random data to avoid flakyness.
template <ComputeChecksum F> static void verifyChecksumFunctionBitFlip() {
  scudo::uptr Array[sizeof(scudo::u64) / sizeof(scudo::uptr)];
  const scudo::uptr ArraySize = ARRAY_SIZE(Array);
  memset(Array, 0xaa, sizeof(Array));
  const scudo::u32 Seed = 0x41424343U;
  const scudo::u16 Reference = F(Seed, Array, ArraySize);
  scudo::u8 IdenticalChecksums = 0;
  for (scudo::uptr I = 0; I < ArraySize; I++) {
    for (scudo::uptr J = 0; J < SCUDO_WORDSIZE; J++) {
      Array[I] ^= scudo::uptr{1} << J;
      if (F(Seed, Array, ArraySize) == Reference)
        IdenticalChecksums++;
      Array[I] ^= scudo::uptr{1} << J;
    }
  }
  // Allow for a couple of identical checksums over the whole set of flips.
  EXPECT_LE(IdenticalChecksums, 2);
}

TEST(ScudoChecksumTest, ChecksumFunctions) {
  verifyChecksumFunctionBitFlip<computeSoftwareChecksum>();
  if (&scudo::computeHardwareCRC32 && scudo::hasHardwareCRC32())
    verifyChecksumFunctionBitFlip<computeHardwareChecksum>();
}
