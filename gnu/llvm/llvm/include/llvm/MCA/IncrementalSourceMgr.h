//===---------------- IncrementalSourceMgr.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file contains IncrementalSourceMgr, an implementation of SourceMgr
/// that allows users to add new instructions incrementally / dynamically.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_INCREMENTALSOURCEMGR_H
#define LLVM_MCA_INCREMENTALSOURCEMGR_H

#include "llvm/MCA/SourceMgr.h"
#include <deque>

namespace llvm {
namespace mca {

/// An implementation of \a SourceMgr that allows users to add new instructions
/// incrementally / dynamically.
/// Note that this SourceMgr takes ownership of all \a mca::Instruction.
class IncrementalSourceMgr : public SourceMgr {
  /// Owner of all mca::Instruction instances. Note that we use std::deque here
  /// to have a better throughput, in comparison to std::vector or
  /// llvm::SmallVector, as they usually pay a higher re-allocation cost when
  /// there is a large number of instructions.
  std::deque<UniqueInst> InstStorage;

  /// Instructions that are ready to be used. Each of them is a pointer of an
  /// \a UniqueInst inside InstStorage.
  std::deque<Instruction *> Staging;

  /// Current instruction index.
  unsigned TotalCounter = 0U;

  /// End-of-stream flag.
  bool EOS = false;

  /// Called when an instruction is no longer needed.
  using InstFreedCallback = std::function<void(Instruction *)>;
  InstFreedCallback InstFreedCB;

public:
  IncrementalSourceMgr() = default;

  void clear();

  /// Set a callback that is invoked when a mca::Instruction is
  /// no longer needed. This is usually used for recycling the
  /// instruction.
  void setOnInstFreedCallback(InstFreedCallback CB) { InstFreedCB = CB; }

  ArrayRef<UniqueInst> getInstructions() const override {
    llvm_unreachable("Not applicable");
  }

  bool hasNext() const override { return !Staging.empty(); }
  bool isEnd() const override { return EOS; }

  SourceRef peekNext() const override {
    assert(hasNext());
    return SourceRef(TotalCounter, *Staging.front());
  }

  /// Add a new instruction.
  void addInst(UniqueInst &&Inst) {
    InstStorage.emplace_back(std::move(Inst));
    Staging.push_back(InstStorage.back().get());
  }

  /// Add a recycled instruction.
  void addRecycledInst(Instruction *Inst) { Staging.push_back(Inst); }

  void updateNext() override;

  /// Mark the end of instruction stream.
  void endOfStream() { EOS = true; }

#ifndef NDEBUG
  /// Print statistic about instruction recycling stats.
  void printStatistic(raw_ostream &OS);
#endif
};

} // end namespace mca
} // end namespace llvm

#endif // LLVM_MCA_INCREMENTALSOURCEMGR_H
