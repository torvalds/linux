//===- Hash.cpp - PDB Hash Functions --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/Endian.h"
#include <cstdint>

using namespace llvm;
using namespace llvm::support;

// Corresponds to `Hasher::lhashPbCb` in PDB/include/misc.h.
// Used for name hash table and TPI/IPI hashes.
uint32_t pdb::hashStringV1(StringRef Str) {
  uint32_t Result = 0;
  uint32_t Size = Str.size();

  ArrayRef<ulittle32_t> Longs(reinterpret_cast<const ulittle32_t *>(Str.data()),
                              Size / 4);

  for (auto Value : Longs)
    Result ^= Value;

  const uint8_t *Remainder = reinterpret_cast<const uint8_t *>(Longs.end());
  uint32_t RemainderSize = Size % 4;

  // Maximum of 3 bytes left.  Hash a 2 byte word if possible, then hash the
  // possibly remaining 1 byte.
  if (RemainderSize >= 2) {
    uint16_t Value = *reinterpret_cast<const ulittle16_t *>(Remainder);
    Result ^= static_cast<uint32_t>(Value);
    Remainder += 2;
    RemainderSize -= 2;
  }

  // hash possible odd byte
  if (RemainderSize == 1) {
    Result ^= *(Remainder++);
  }

  const uint32_t toLowerMask = 0x20202020;
  Result |= toLowerMask;
  Result ^= (Result >> 11);

  return Result ^ (Result >> 16);
}

// Corresponds to `HasherV2::HashULONG` in PDB/include/misc.h.
// Used for name hash table.
uint32_t pdb::hashStringV2(StringRef Str) {
  uint32_t Hash = 0xb170a1bf;

  ArrayRef<char> Buffer(Str.begin(), Str.end());

  ArrayRef<ulittle32_t> Items(
      reinterpret_cast<const ulittle32_t *>(Buffer.data()),
      Buffer.size() / sizeof(ulittle32_t));
  for (ulittle32_t Item : Items) {
    Hash += Item;
    Hash += (Hash << 10);
    Hash ^= (Hash >> 6);
  }
  Buffer = Buffer.slice(Items.size() * sizeof(ulittle32_t));
  for (uint8_t Item : Buffer) {
    Hash += Item;
    Hash += (Hash << 10);
    Hash ^= (Hash >> 6);
  }

  return Hash * 1664525U + 1013904223U;
}

// Corresponds to `SigForPbCb` in langapi/shared/crc32.h.
uint32_t pdb::hashBufferV8(ArrayRef<uint8_t> Buf) {
  JamCRC JC(/*Init=*/0U);
  JC.update(Buf);
  return JC.getCRC();
}
