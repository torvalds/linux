//===- AArch64MachineScheduler.h - Custom AArch64 MI scheduler --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Custom AArch64 MI scheduler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64MACHINESCHEDULER_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64MACHINESCHEDULER_H

#include "llvm/CodeGen/MachineScheduler.h"

namespace llvm {

/// A MachineSchedStrategy implementation for AArch64 post RA scheduling.
class AArch64PostRASchedStrategy : public PostGenericScheduler {
public:
  AArch64PostRASchedStrategy(const MachineSchedContext *C) :
    PostGenericScheduler(C) {}

protected:
  bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand) override;
};

} // end namespace llvm

#endif

