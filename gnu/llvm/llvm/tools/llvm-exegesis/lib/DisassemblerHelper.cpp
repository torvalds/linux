//===-- DisassemblerHelper.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DisassemblerHelper.h"

#include "llvm/MC/TargetRegistry.h"

namespace llvm {
namespace exegesis {

DisassemblerHelper::DisassemblerHelper(const LLVMState &State) : State_(State) {
  MCTargetOptions MCOptions;
  const auto &TM = State.getTargetMachine();
  const auto &Triple = TM.getTargetTriple();
  AsmInfo_.reset(TM.getTarget().createMCAsmInfo(State_.getRegInfo(),
                                                Triple.str(), MCOptions));
  InstPrinter_.reset(TM.getTarget().createMCInstPrinter(
      Triple, 0 /*default variant*/, *AsmInfo_, State_.getInstrInfo(),
      State_.getRegInfo()));

  Context_ = std::make_unique<MCContext>(
      Triple, AsmInfo_.get(), &State_.getRegInfo(), &State_.getSubtargetInfo());
  Disasm_.reset(TM.getTarget().createMCDisassembler(State_.getSubtargetInfo(),
                                                    *Context_));
  assert(Disasm_ && "cannot create MCDisassembler. missing call to "
                    "InitializeXXXTargetDisassembler ?");
}

} // namespace exegesis
} // namespace llvm
