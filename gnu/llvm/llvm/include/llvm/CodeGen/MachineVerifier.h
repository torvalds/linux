//===- llvm/CodeGen/MachineVerifier.h - Machine Code Verifier ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEVERIFIER_H
#define LLVM_CODEGEN_MACHINEVERIFIER_H

#include "llvm/CodeGen/MachinePassManager.h"
#include <string>

namespace llvm {
class MachineVerifierPass : public PassInfoMixin<MachineVerifierPass> {
  std::string Banner;

public:
  MachineVerifierPass(const std::string &Banner = std::string())
      : Banner(Banner) {}
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEVERIFIER_H
