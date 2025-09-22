//===------------------- X86CustomBehaviour.cpp -----------------*-C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods from the X86CustomBehaviour class.
///
//===----------------------------------------------------------------------===//

#include "X86CustomBehaviour.h"
#include "TargetInfo/X86TargetInfo.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/WithColor.h"

namespace llvm {
namespace mca {

void X86InstrPostProcess::setMemBarriers(std::unique_ptr<Instruction> &Inst,
                                         const MCInst &MCI) {
  switch (MCI.getOpcode()) {
  case X86::MFENCE:
    Inst->setLoadBarrier(true);
    Inst->setStoreBarrier(true);
    break;
  case X86::LFENCE:
    Inst->setLoadBarrier(true);
    break;
  case X86::SFENCE:
    Inst->setStoreBarrier(true);
    break;
  }
}

void X86InstrPostProcess::postProcessInstruction(
    std::unique_ptr<Instruction> &Inst, const MCInst &MCI) {
  // Currently, we only modify certain instructions' IsALoadBarrier and
  // IsAStoreBarrier flags.
  setMemBarriers(Inst, MCI);
}

} // namespace mca
} // namespace llvm

using namespace llvm;
using namespace mca;

static InstrPostProcess *createX86InstrPostProcess(const MCSubtargetInfo &STI,
                                                   const MCInstrInfo &MCII) {
  return new X86InstrPostProcess(STI, MCII);
}

/// Extern function to initialize the targets for the X86 backend

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeX86TargetMCA() {
  TargetRegistry::RegisterInstrPostProcess(getTheX86_32Target(),
                                           createX86InstrPostProcess);
  TargetRegistry::RegisterInstrPostProcess(getTheX86_64Target(),
                                           createX86InstrPostProcess);
}
