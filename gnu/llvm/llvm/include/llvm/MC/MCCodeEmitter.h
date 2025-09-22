//===- llvm/MC/MCCodeEmitter.h - Instruction Encoding -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCODEEMITTER_H
#define LLVM_MC_MCCODEEMITTER_H

namespace llvm {

class MCFixup;
class MCInst;
class MCSubtargetInfo;
class raw_ostream;
template<typename T> class SmallVectorImpl;

/// MCCodeEmitter - Generic instruction encoding interface.
class MCCodeEmitter {
protected: // Can only create subclasses.
  MCCodeEmitter();

public:
  MCCodeEmitter(const MCCodeEmitter &) = delete;
  MCCodeEmitter &operator=(const MCCodeEmitter &) = delete;
  virtual ~MCCodeEmitter();

  /// Lifetime management
  virtual void reset() {}

  /// Encode the given \p Inst to bytes and append to \p CB.
  virtual void encodeInstruction(const MCInst &Inst, SmallVectorImpl<char> &CB,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const = 0;
};

} // end namespace llvm

#endif // LLVM_MC_MCCODEEMITTER_H
