//==- RegAllocFast.h ----------- fast register allocator  ----------*-C++-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCFAST_H
#define LLVM_CODEGEN_REGALLOCFAST_H

#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/RegAllocCommon.h"

namespace llvm {

struct RegAllocFastPassOptions {
  RegAllocFilterFunc Filter = nullptr;
  StringRef FilterName = "all";
  bool ClearVRegs = true;
};

class RegAllocFastPass : public PassInfoMixin<RegAllocFastPass> {
  RegAllocFastPassOptions Opts;

public:
  RegAllocFastPass(RegAllocFastPassOptions Opts = RegAllocFastPassOptions())
      : Opts(Opts) {}

  MachineFunctionProperties getRequiredProperties() {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }

  MachineFunctionProperties getSetProperties() {
    if (Opts.ClearVRegs) {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    return MachineFunctionProperties();
  }

  MachineFunctionProperties getClearedProperties() {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }

  PreservedAnalyses run(MachineFunction &MF, MachineFunctionAnalysisManager &);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // namespace llvm

#endif // LLVM_CODEGEN_REGALLOCFAST_H
