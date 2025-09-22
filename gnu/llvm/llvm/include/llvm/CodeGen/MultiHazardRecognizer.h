//=- llvm/CodeGen/MultiHazardRecognizer.h - Scheduling Support ----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MultiHazardRecognizer class, which is a wrapper
// for a set of ScheduleHazardRecognizer instances
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MULTIHAZARDRECOGNIZER_H
#define LLVM_CODEGEN_MULTIHAZARDRECOGNIZER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"

namespace llvm {

class MachineInstr;
class SUnit;

class MultiHazardRecognizer : public ScheduleHazardRecognizer {
  SmallVector<std::unique_ptr<ScheduleHazardRecognizer>, 4> Recognizers;

public:
  MultiHazardRecognizer() = default;
  void AddHazardRecognizer(std::unique_ptr<ScheduleHazardRecognizer> &&);

  bool atIssueLimit() const override;
  HazardType getHazardType(SUnit *, int Stalls = 0) override;
  void Reset() override;
  void EmitInstruction(SUnit *) override;
  void EmitInstruction(MachineInstr *) override;
  unsigned PreEmitNoops(SUnit *) override;
  unsigned PreEmitNoops(MachineInstr *) override;
  bool ShouldPreferAnother(SUnit *) override;
  void AdvanceCycle() override;
  void RecedeCycle() override;
  void EmitNoop() override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MULTIHAZARDRECOGNIZER_H
