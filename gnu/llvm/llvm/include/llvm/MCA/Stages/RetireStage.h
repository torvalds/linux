//===---------------------- RetireStage.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the retire stage of a default instruction pipeline.
/// The RetireStage represents the process logic that interacts with the
/// simulated RetireControlUnit hardware.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_RETIRESTAGE_H
#define LLVM_MCA_STAGES_RETIRESTAGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/MCA/HardwareUnits/RegisterFile.h"
#include "llvm/MCA/HardwareUnits/RetireControlUnit.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

class RetireStage final : public Stage {
  // Owner will go away when we move listeners/eventing to the stages.
  RetireControlUnit &RCU;
  RegisterFile &PRF;
  LSUnitBase &LSU;

  RetireStage(const RetireStage &Other) = delete;
  RetireStage &operator=(const RetireStage &Other) = delete;

public:
  RetireStage(RetireControlUnit &R, RegisterFile &F, LSUnitBase &LS)
      : RCU(R), PRF(F), LSU(LS) {}

  bool hasWorkToComplete() const override { return !RCU.isEmpty(); }
  Error cycleStart() override;
  Error cycleEnd() override;
  Error execute(InstRef &IR) override;
  void notifyInstructionRetired(const InstRef &IR) const;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_RETIRESTAGE_H
