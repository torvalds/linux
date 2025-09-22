//===---------------------- InOrderIssueStage.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// InOrderIssueStage implements an in-order execution pipeline.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_INORDERISSUESTAGE_H
#define LLVM_MCA_STAGES_INORDERISSUESTAGE_H

#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/MCA/HardwareUnits/ResourceManager.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {
class LSUnit;
class RegisterFile;

struct StallInfo {
  enum class StallKind {
    DEFAULT,
    REGISTER_DEPS,
    DISPATCH,
    DELAY,
    LOAD_STORE,
    CUSTOM_STALL
  };

  InstRef IR;
  unsigned CyclesLeft = 0;
  StallKind Kind = StallKind::DEFAULT;

  StallInfo() = default;

  StallKind getStallKind() const { return Kind; }
  unsigned getCyclesLeft() const { return CyclesLeft; }
  const InstRef &getInstruction() const { return IR; }
  InstRef &getInstruction() { return IR; }

  bool isValid() const { return (bool)IR; }
  void clear();
  void update(const InstRef &Inst, unsigned Cycles, StallKind SK);
  void cycleEnd();
};

class InOrderIssueStage final : public Stage {
  const MCSubtargetInfo &STI;
  RegisterFile &PRF;
  ResourceManager RM;
  CustomBehaviour &CB;
  LSUnit &LSU;

  /// Instructions that were issued, but not executed yet.
  SmallVector<InstRef, 4> IssuedInst;

  /// Number of instructions issued in the current cycle.
  unsigned NumIssued;

  StallInfo SI;

  /// Instruction that is issued in more than 1 cycle.
  InstRef CarriedOver;
  /// Number of CarriedOver uops left to issue.
  unsigned CarryOver;

  /// Number of instructions that can be issued in the current cycle.
  unsigned Bandwidth;

  /// Number of cycles (counted from the current cycle) until the last write is
  /// committed. This is taken into account to ensure that writes commit in the
  /// program order.
  unsigned LastWriteBackCycle;

  InOrderIssueStage(const InOrderIssueStage &Other) = delete;
  InOrderIssueStage &operator=(const InOrderIssueStage &Other) = delete;

  /// Returns true if IR can execute during this cycle.
  /// In case of stall, it updates SI with information about the stalled
  /// instruction and the stall reason.
  bool canExecute(const InstRef &IR);

  /// Issue the instruction, or update the StallInfo.
  Error tryIssue(InstRef &IR);

  /// Update status of instructions from IssuedInst.
  void updateIssuedInst();

  /// Continue to issue the CarriedOver instruction.
  void updateCarriedOver();

  /// Notifies a stall event to the Stage listener. Stall information is
  /// obtained from the internal StallInfo field.
  void notifyStallEvent();

  void notifyInstructionIssued(const InstRef &IR,
                               ArrayRef<ResourceUse> UsedRes);
  void notifyInstructionDispatched(const InstRef &IR, unsigned Ops,
                                   ArrayRef<unsigned> UsedRegs);
  void notifyInstructionExecuted(const InstRef &IR);
  void notifyInstructionRetired(const InstRef &IR,
                                ArrayRef<unsigned> FreedRegs);

  /// Retire instruction once it is executed.
  void retireInstruction(InstRef &IR);

public:
  InOrderIssueStage(const MCSubtargetInfo &STI, RegisterFile &PRF,
                    CustomBehaviour &CB, LSUnit &LSU);

  unsigned getIssueWidth() const;
  bool isAvailable(const InstRef &) const override;
  bool hasWorkToComplete() const override;
  Error execute(InstRef &IR) override;
  Error cycleStart() override;
  Error cycleEnd() override;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_INORDERISSUESTAGE_H
