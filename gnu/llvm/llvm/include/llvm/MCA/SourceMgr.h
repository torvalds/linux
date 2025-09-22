//===--------------------- SourceMgr.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file contains abstract class SourceMgr and the default implementation,
/// CircularSourceMgr.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_SOURCEMGR_H
#define LLVM_MCA_SOURCEMGR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MCA/Instruction.h"

namespace llvm {
namespace mca {

// MSVC >= 19.15, < 19.20 need to see the definition of class Instruction to
// prevent compiler error C2139 about intrinsic type trait '__is_assignable'.
typedef std::pair<unsigned, const Instruction &> SourceRef;

/// Abstracting the input code sequence (a sequence of MCInst) and assigning
/// unique identifiers to every instruction in the sequence.
struct SourceMgr {
  using UniqueInst = std::unique_ptr<Instruction>;

  /// Provides a fixed range of \a UniqueInst to iterate.
  virtual ArrayRef<UniqueInst> getInstructions() const = 0;

  /// (Fixed) Number of \a UniqueInst. Returns the size of
  /// \a getInstructions by default.
  virtual size_t size() const { return getInstructions().size(); }

  /// Whether there is any \a SourceRef to inspect / peek next.
  /// Note that returning false from this doesn't mean the instruction
  /// stream has ended.
  virtual bool hasNext() const = 0;

  /// Whether the instruction stream has eneded.
  virtual bool isEnd() const = 0;

  /// The next \a SourceRef.
  virtual SourceRef peekNext() const = 0;

  /// Advance to the next \a SourceRef.
  virtual void updateNext() = 0;

  virtual ~SourceMgr() {}
};

/// The default implementation of \a SourceMgr. It always takes a fixed number
/// of instructions and provides an option to loop the given sequence for a
/// certain iterations.
class CircularSourceMgr : public SourceMgr {
  ArrayRef<UniqueInst> Sequence;
  unsigned Current;
  const unsigned Iterations;
  static const unsigned DefaultIterations = 100;

public:
  CircularSourceMgr(ArrayRef<UniqueInst> S, unsigned Iter)
      : Sequence(S), Current(0U), Iterations(Iter ? Iter : DefaultIterations) {}

  ArrayRef<UniqueInst> getInstructions() const override { return Sequence; }

  unsigned getNumIterations() const { return Iterations; }
  bool hasNext() const override {
    return Current < (Iterations * Sequence.size());
  }
  bool isEnd() const override { return !hasNext(); }

  SourceRef peekNext() const override {
    assert(hasNext() && "Already at end of sequence!");
    return SourceRef(Current, *Sequence[Current % Sequence.size()]);
  }

  void updateNext() override { ++Current; }
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_SOURCEMGR_H
