//===- VarLenCodeEmitterGen.h - CEG for variable-length insts ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declare the CodeEmitterGen component for variable-length
// instructions. See the .cpp file for more details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_VARLENCODEEMITTERGEN_H
#define LLVM_UTILS_TABLEGEN_VARLENCODEEMITTERGEN_H

#include "llvm/TableGen/Record.h"

namespace llvm {

struct EncodingSegment {
  unsigned BitWidth;
  const Init *Value;
  StringRef CustomEncoder = "";
  StringRef CustomDecoder = "";
};

class VarLenInst {
  const RecordVal *TheDef;
  size_t NumBits;

  // Set if any of the segment is not fixed value.
  bool HasDynamicSegment;

  SmallVector<EncodingSegment, 4> Segments;

  void buildRec(const DagInit *DI);

public:
  VarLenInst() : TheDef(nullptr), NumBits(0U), HasDynamicSegment(false) {}

  explicit VarLenInst(const DagInit *DI, const RecordVal *TheDef);

  /// Number of bits
  size_t size() const { return NumBits; }

  using const_iterator = decltype(Segments)::const_iterator;

  const_iterator begin() const { return Segments.begin(); }
  const_iterator end() const { return Segments.end(); }
  size_t getNumSegments() const { return Segments.size(); }

  bool isFixedValueOnly() const { return !HasDynamicSegment; }
};

void emitVarLenCodeEmitter(RecordKeeper &R, raw_ostream &OS);

} // end namespace llvm
#endif
