//===--------------------- CodeEmitter.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CodeEmitter API.
//
//===----------------------------------------------------------------------===//

#include "llvm/MCA/CodeEmitter.h"

namespace llvm {
namespace mca {

CodeEmitter::EncodingInfo CodeEmitter::getOrCreateEncodingInfo(unsigned MCID) {
  EncodingInfo &EI = Encodings[MCID];
  if (EI.second)
    return EI;

  SmallVector<llvm::MCFixup, 2> Fixups;
  const MCInst &Inst = Sequence[MCID];
  MCInst Relaxed(Sequence[MCID]);
  if (MAB.mayNeedRelaxation(Inst, STI))
    MAB.relaxInstruction(Relaxed, STI);

  EI.first = Code.size();
  MCE.encodeInstruction(Relaxed, Code, Fixups, STI);
  EI.second = Code.size() - EI.first;
  return EI;
}

} // namespace mca
} // namespace llvm
