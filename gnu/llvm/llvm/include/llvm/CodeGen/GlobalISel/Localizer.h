//== llvm/CodeGen/GlobalISel/Localizer.h - Localizer -------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file describes the interface of the Localizer pass.
/// This pass moves/duplicates constant-like instructions close to their uses.
/// Its primarily goal is to workaround the deficiencies of the fast register
/// allocator.
/// With GlobalISel constants are all materialized in the entry block of
/// a function. However, the fast allocator cannot rematerialize constants and
/// has a lot more live-ranges to deal with and will most likely end up
/// spilling a lot.
/// By pushing the constants close to their use, we only create small
/// live-ranges.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LOCALIZER_H
#define LLVM_CODEGEN_GLOBALISEL_LOCALIZER_H

#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {
// Forward declarations.
class AnalysisUsage;
class MachineBasicBlock;
class MachineInstr;
class MachineOperand;
class MachineRegisterInfo;
class TargetTransformInfo;

/// This pass implements the localization mechanism described at the
/// top of this file. One specificity of the implementation is that
/// it will materialize one and only one instance of a constant per
/// basic block, thus enabling reuse of that constant within that block.
/// Moreover, it only materializes constants in blocks where they
/// are used. PHI uses are considered happening at the end of the
/// related predecessor.
class Localizer : public MachineFunctionPass {
public:
  static char ID;

private:
  /// An input function to decide if the pass should run or not
  /// on the given MachineFunction.
  std::function<bool(const MachineFunction &)> DoNotRunPass;

  /// MRI contains all the register class/bank information that this
  /// pass uses and updates.
  MachineRegisterInfo *MRI = nullptr;
  /// TTI used for getting remat costs for instructions.
  TargetTransformInfo *TTI = nullptr;

  /// Check if \p MOUse is used in the same basic block as \p Def.
  /// If the use is in the same block, we say it is local.
  /// When the use is not local, \p InsertMBB will contain the basic
  /// block when to insert \p Def to have a local use.
  static bool isLocalUse(MachineOperand &MOUse, const MachineInstr &Def,
                         MachineBasicBlock *&InsertMBB);

  /// Initialize the field members using \p MF.
  void init(MachineFunction &MF);

  typedef SmallSetVector<MachineInstr *, 32> LocalizedSetVecT;

  /// If \p Op is a reg operand of a PHI, return the number of total
  /// operands in the PHI that are the same as \p Op, including itself.
  unsigned getNumPhiUses(MachineOperand &Op) const;

  /// Do inter-block localization from the entry block.
  bool localizeInterBlock(MachineFunction &MF,
                          LocalizedSetVecT &LocalizedInstrs);

  /// Do intra-block localization of already localized instructions.
  bool localizeIntraBlock(LocalizedSetVecT &LocalizedInstrs);

public:
  Localizer();
  Localizer(std::function<bool(const MachineFunction &)>);

  StringRef getPassName() const override { return "Localizer"; }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties()
        .set(MachineFunctionProperties::Property::IsSSA);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // End namespace llvm.

#endif
