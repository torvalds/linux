//===- ExtractRanges.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include "llvm/DebugInfo/GSYM/FileWriter.h"
#include "llvm/Support/DataExtractor.h"
#include <algorithm>
#include <inttypes.h>

namespace llvm {
namespace gsym {

void encodeRange(const AddressRange &Range, FileWriter &O, uint64_t BaseAddr) {
  assert(Range.start() >= BaseAddr);
  O.writeULEB(Range.start() - BaseAddr);
  O.writeULEB(Range.size());
}

AddressRange decodeRange(DataExtractor &Data, uint64_t BaseAddr,
                         uint64_t &Offset) {
  const uint64_t AddrOffset = Data.getULEB128(&Offset);
  const uint64_t Size = Data.getULEB128(&Offset);
  const uint64_t StartAddr = BaseAddr + AddrOffset;

  return {StartAddr, StartAddr + Size};
}

void encodeRanges(const AddressRanges &Ranges, FileWriter &O,
                  uint64_t BaseAddr) {
  O.writeULEB(Ranges.size());
  if (Ranges.empty())
    return;
  for (auto Range : Ranges)
    encodeRange(Range, O, BaseAddr);
}

void decodeRanges(AddressRanges &Ranges, DataExtractor &Data, uint64_t BaseAddr,
                  uint64_t &Offset) {
  Ranges.clear();
  uint64_t NumRanges = Data.getULEB128(&Offset);
  Ranges.reserve(NumRanges);
  for (uint64_t RangeIdx = 0; RangeIdx < NumRanges; RangeIdx++)
    Ranges.insert(decodeRange(Data, BaseAddr, Offset));
}

void skipRange(DataExtractor &Data, uint64_t &Offset) {
  Data.getULEB128(&Offset);
  Data.getULEB128(&Offset);
}

uint64_t skipRanges(DataExtractor &Data, uint64_t &Offset) {
  uint64_t NumRanges = Data.getULEB128(&Offset);
  for (uint64_t I = 0; I < NumRanges; ++I)
    skipRange(Data, Offset);
  return NumRanges;
}

} // namespace gsym

raw_ostream &operator<<(raw_ostream &OS, const AddressRange &R) {
  return OS << '[' << HEX64(R.start()) << " - " << HEX64(R.end()) << ")";
}

raw_ostream &operator<<(raw_ostream &OS, const AddressRanges &AR) {
  size_t Size = AR.size();
  for (size_t I = 0; I < Size; ++I) {
    if (I)
      OS << ' ';
    OS << AR[I];
  }
  return OS;
}

} // namespace llvm
