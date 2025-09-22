//===--------------------- RegisterFileStatistics.h -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This view collects and prints register file usage statistics.
///
/// Example  (-mcpu=btver2):
/// ========================
///
/// Register File statistics:
/// Total number of mappings created:    6
/// Max number of mappings used:         3
///
/// *  Register File #1 -- FpuPRF:
///    Number of physical registers:     72
///    Total number of mappings created: 0
///    Max number of mappings used:      0
///    Number of optimizable moves:      200
///    Number of moves eliminated:       200 (100.0%)
///    Number of zero moves:             200 (100.0%)
///    Max moves eliminated per cycle:   2
///
/// *  Register File #2 -- IntegerPRF:
///    Number of physical registers:     64
///    Total number of mappings created: 6
///    Max number of mappings used:      3
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_REGISTERFILESTATISTICS_H
#define LLVM_TOOLS_LLVM_MCA_REGISTERFILESTATISTICS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/View.h"

namespace llvm {
namespace mca {

class RegisterFileStatistics : public View {
  const llvm::MCSubtargetInfo &STI;

  // Used to track the number of physical registers used in a register file.
  struct RegisterFileUsage {
    unsigned TotalMappings;
    unsigned MaxUsedMappings;
    unsigned CurrentlyUsedMappings;
  };

  struct MoveEliminationInfo {
    unsigned TotalMoveEliminationCandidates;
    unsigned TotalMovesEliminated;
    unsigned TotalMovesThatPropagateZero;
    unsigned MaxMovesEliminatedPerCycle;
    unsigned CurrentMovesEliminated;
  };

  // There is one entry for each register file implemented by the processor.
  llvm::SmallVector<RegisterFileUsage, 4> PRFUsage;
  llvm::SmallVector<MoveEliminationInfo, 4> MoveElimInfo;

  void updateRegisterFileUsage(ArrayRef<unsigned> UsedPhysRegs);
  void updateMoveElimInfo(const Instruction &Inst);

public:
  RegisterFileStatistics(const llvm::MCSubtargetInfo &sti);

  void onCycleEnd() override;
  void onEvent(const HWInstructionEvent &Event) override;
  void printView(llvm::raw_ostream &OS) const override;
  StringRef getNameAsString() const override {
    return "RegisterFileStatistics";
  }
  bool isSerializable() const override { return false; }
};
} // namespace mca
} // namespace llvm

#endif
