//===- HardwareLoops.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Defines an IR pass for the creation of hardware loops.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_HARDWARELOOPS_H
#define LLVM_CODEGEN_HARDWARELOOPS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct HardwareLoopOptions {
  std::optional<unsigned> Decrement;
  std::optional<unsigned> Bitwidth;
  std::optional<bool> Force;
  std::optional<bool> ForcePhi;
  std::optional<bool> ForceNested;
  std::optional<bool> ForceGuard;

  HardwareLoopOptions &setDecrement(unsigned Count) {
    Decrement = Count;
    return *this;
  }
  HardwareLoopOptions &setCounterBitwidth(unsigned Width) {
    Bitwidth = Width;
    return *this;
  }
  HardwareLoopOptions &setForce(bool Force) {
    this->Force = Force;
    return *this;
  }
  HardwareLoopOptions &setForcePhi(bool Force) {
    ForcePhi = Force;
    return *this;
  }
  HardwareLoopOptions &setForceNested(bool Force) {
    ForceNested = Force;
    return *this;
  }
  HardwareLoopOptions &setForceGuard(bool Force) {
    ForceGuard = Force;
    return *this;
  }
  bool getForcePhi() const {
    return ForcePhi.has_value() && ForcePhi.value();
  }
  bool getForceNested() const {
    return ForceNested.has_value() && ForceNested.value();
  }
  bool getForceGuard() const {
    return ForceGuard.has_value() && ForceGuard.value();
  }
};

class HardwareLoopsPass : public PassInfoMixin<HardwareLoopsPass> {
  HardwareLoopOptions Opts;

public:
  explicit HardwareLoopsPass(HardwareLoopOptions Opts = {})
    : Opts(Opts) { }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_HARDWARELOOPS_H
