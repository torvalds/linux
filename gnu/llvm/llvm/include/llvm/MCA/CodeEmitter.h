//===--------------------- CodeEmitter.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A utility class used to compute instruction encodings. It buffers encodings
/// for later usage. It exposes a simple API to compute and get the encodings as
/// StringRef.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_CODEEMITTER_H
#define LLVM_MCA_CODEEMITTER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {
namespace mca {

/// A utility class used to compute instruction encodings for a code region.
///
/// It provides a simple API to compute and return instruction encodings as
/// strings. Encodings are cached internally for later usage.
class CodeEmitter {
  const MCSubtargetInfo &STI;
  const MCAsmBackend &MAB;
  const MCCodeEmitter &MCE;

  SmallString<256> Code;
  ArrayRef<MCInst> Sequence;

  // An EncodingInfo pair stores <base, length> information.  Base (i.e. first)
  // is an index to the `Code`. Length (i.e. second) is the encoding size.
  using EncodingInfo = std::pair<unsigned, unsigned>;

  // A cache of encodings.
  SmallVector<EncodingInfo, 16> Encodings;

  EncodingInfo getOrCreateEncodingInfo(unsigned MCID);

public:
  CodeEmitter(const MCSubtargetInfo &ST, const MCAsmBackend &AB,
              const MCCodeEmitter &CE, ArrayRef<MCInst> S)
      : STI(ST), MAB(AB), MCE(CE), Sequence(S), Encodings(S.size()) {}

  StringRef getEncoding(unsigned MCID) {
    EncodingInfo EI = getOrCreateEncodingInfo(MCID);
    return StringRef(&Code[EI.first], EI.second);
  }
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_CODEEMITTER_H
