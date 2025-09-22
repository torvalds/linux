//===- FuzzerValueBitMap.h - INTERNAL - Bit map -----------------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// ValueBitMap.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_VALUE_BIT_MAP_H
#define LLVM_FUZZER_VALUE_BIT_MAP_H

#include "FuzzerPlatform.h"
#include <cstdint>

namespace fuzzer {

// A bit map containing kMapSizeInWords bits.
struct ValueBitMap {
  static const size_t kMapSizeInBits = 1 << 16;
  static const size_t kMapPrimeMod = 65371;  // Largest Prime < kMapSizeInBits;
  static const size_t kBitsInWord = (sizeof(uintptr_t) * 8);
  static const size_t kMapSizeInWords = kMapSizeInBits / kBitsInWord;
 public:

  // Clears all bits.
  void Reset() { memset(Map, 0, sizeof(Map)); }

  // Computes a hash function of Value and sets the corresponding bit.
  // Returns true if the bit was changed from 0 to 1.
  ATTRIBUTE_NO_SANITIZE_ALL
  inline bool AddValue(uintptr_t Value) {
    uintptr_t Idx = Value % kMapSizeInBits;
    uintptr_t WordIdx = Idx / kBitsInWord;
    uintptr_t BitIdx = Idx % kBitsInWord;
    uintptr_t Old = Map[WordIdx];
    uintptr_t New = Old | (1ULL << BitIdx);
    Map[WordIdx] = New;
    return New != Old;
  }

  ATTRIBUTE_NO_SANITIZE_ALL
  inline bool AddValueModPrime(uintptr_t Value) {
    return AddValue(Value % kMapPrimeMod);
  }

  inline bool Get(uintptr_t Idx) {
    assert(Idx < kMapSizeInBits);
    uintptr_t WordIdx = Idx / kBitsInWord;
    uintptr_t BitIdx = Idx % kBitsInWord;
    return Map[WordIdx] & (1ULL << BitIdx);
  }

  size_t SizeInBits() const { return kMapSizeInBits; }

  template <class Callback>
  ATTRIBUTE_NO_SANITIZE_ALL
  void ForEach(Callback CB) const {
    for (size_t i = 0; i < kMapSizeInWords; i++)
      if (uintptr_t M = Map[i])
        for (size_t j = 0; j < sizeof(M) * 8; j++)
          if (M & ((uintptr_t)1 << j))
            CB(i * sizeof(M) * 8 + j);
  }

 private:
  ATTRIBUTE_ALIGNED(512) uintptr_t Map[kMapSizeInWords];
};

}  // namespace fuzzer

#endif  // LLVM_FUZZER_VALUE_BIT_MAP_H
