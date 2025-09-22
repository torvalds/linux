//===-------------------- X86CustomBehaviour.h ------------------*-C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the X86CustomBehaviour class which inherits from
/// CustomBehaviour. This class is used by the tool llvm-mca to enforce
/// target specific behaviour that is not expressed well enough in the
/// scheduling model for mca to enforce it automatically.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_MCA_X86CUSTOMBEHAVIOUR_H
#define LLVM_LIB_TARGET_X86_MCA_X86CUSTOMBEHAVIOUR_H

#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/TargetParser/TargetParser.h"

namespace llvm {
namespace mca {

class X86InstrPostProcess : public InstrPostProcess {
  /// Called within X86InstrPostProcess to specify certain instructions
  /// as load and store barriers.
  void setMemBarriers(std::unique_ptr<Instruction> &Inst, const MCInst &MCI);

public:
  X86InstrPostProcess(const MCSubtargetInfo &STI, const MCInstrInfo &MCII)
      : InstrPostProcess(STI, MCII) {}

  ~X86InstrPostProcess() = default;

  void postProcessInstruction(std::unique_ptr<Instruction> &Inst,
                              const MCInst &MCI) override;
};

} // namespace mca
} // namespace llvm

#endif
