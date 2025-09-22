//===- FuzzerCrossOver.cpp - Cross over two test inputs -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Cross over test inputs.
//===----------------------------------------------------------------------===//

#include "FuzzerDefs.h"
#include "FuzzerMutate.h"
#include "FuzzerRandom.h"
#include <cstring>

namespace fuzzer {

// Cross Data1 and Data2, store the result (up to MaxOutSize bytes) in Out.
size_t MutationDispatcher::CrossOver(const uint8_t *Data1, size_t Size1,
                                     const uint8_t *Data2, size_t Size2,
                                     uint8_t *Out, size_t MaxOutSize) {
  assert(Size1 || Size2);
  MaxOutSize = Rand(MaxOutSize) + 1;
  size_t OutPos = 0;
  size_t Pos1 = 0;
  size_t Pos2 = 0;
  size_t *InPos = &Pos1;
  size_t InSize = Size1;
  const uint8_t *Data = Data1;
  bool CurrentlyUsingFirstData = true;
  while (OutPos < MaxOutSize && (Pos1 < Size1 || Pos2 < Size2)) {
    // Merge a part of Data into Out.
    size_t OutSizeLeft = MaxOutSize - OutPos;
    if (*InPos < InSize) {
      size_t InSizeLeft = InSize - *InPos;
      size_t MaxExtraSize = std::min(OutSizeLeft, InSizeLeft);
      size_t ExtraSize = Rand(MaxExtraSize) + 1;
      memcpy(Out + OutPos, Data + *InPos, ExtraSize);
      OutPos += ExtraSize;
      (*InPos) += ExtraSize;
    }
    // Use the other input data on the next iteration.
    InPos  = CurrentlyUsingFirstData ? &Pos2 : &Pos1;
    InSize = CurrentlyUsingFirstData ? Size2 : Size1;
    Data   = CurrentlyUsingFirstData ? Data2 : Data1;
    CurrentlyUsingFirstData = !CurrentlyUsingFirstData;
  }
  return OutPos;
}

}  // namespace fuzzer
