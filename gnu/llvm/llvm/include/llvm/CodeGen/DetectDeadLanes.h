//===- DetectDeadLanes.h - SubRegister Lane Usage Analysis --*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Analysis that tracks defined/used subregister lanes across COPY instructions
/// and instructions that get lowered to a COPY (PHI, REG_SEQUENCE,
/// INSERT_SUBREG, EXTRACT_SUBREG).
/// The information is used to detect dead definitions and the usage of
/// (completely) undefined values and mark the operands as such.
/// This pass is necessary because the dead/undef status is not obvious anymore
/// when subregisters are involved.
///
/// Example:
///    %0 = some definition
///    %1 = IMPLICIT_DEF
///    %2 = REG_SEQUENCE %0, sub0, %1, sub1
///    %3 = EXTRACT_SUBREG %2, sub1
///       = use %3
/// The %0 definition is dead and %3 contains an undefined value.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DETECTDEADLANES_H
#define LLVM_CODEGEN_DETECTDEADLANES_H

#include "llvm/ADT/BitVector.h"
#include "llvm/MC/LaneBitmask.h"
#include <deque>

namespace llvm {

class MachineInstr;
class MachineOperand;
class MachineRegisterInfo;
class TargetRegisterInfo;

class DeadLaneDetector {
public:
  /// Contains a bitmask of which lanes of a given virtual register are
  /// defined and which ones are actually used.
  struct VRegInfo {
    LaneBitmask UsedLanes;
    LaneBitmask DefinedLanes;
  };

  DeadLaneDetector(const MachineRegisterInfo *MRI,
                   const TargetRegisterInfo *TRI);

  /// Update the \p DefinedLanes and the \p UsedLanes for all virtual registers.
  void computeSubRegisterLaneBitInfo();

  const VRegInfo &getVRegInfo(unsigned RegIdx) const {
    return VRegInfos[RegIdx];
  }

  bool isDefinedByCopy(unsigned RegIdx) const {
    return DefinedByCopy.test(RegIdx);
  }

private:
  /// Add used lane bits on the register used by operand \p MO. This translates
  /// the bitmask based on the operands subregister, and puts the register into
  /// the worklist if any new bits were added.
  void addUsedLanesOnOperand(const MachineOperand &MO, LaneBitmask UsedLanes);

  /// Given a bitmask \p UsedLanes for the used lanes on a def output of a
  /// COPY-like instruction determine the lanes used on the use operands
  /// and call addUsedLanesOnOperand() for them.
  void transferUsedLanesStep(const MachineInstr &MI, LaneBitmask UsedLanes);

  /// Given a use regiser operand \p Use and a mask of defined lanes, check
  /// if the operand belongs to a lowersToCopies() instruction, transfer the
  /// mask to the def and put the instruction into the worklist.
  void transferDefinedLanesStep(const MachineOperand &Use,
                                LaneBitmask DefinedLanes);

public:
  /// Given a mask \p DefinedLanes of lanes defined at operand \p OpNum
  /// of COPY-like instruction, determine which lanes are defined at the output
  /// operand \p Def.
  LaneBitmask transferDefinedLanes(const MachineOperand &Def, unsigned OpNum,
                                   LaneBitmask DefinedLanes) const;

  /// Given a mask \p UsedLanes used from the output of instruction \p MI
  /// determine which lanes are used from operand \p MO of this instruction.
  LaneBitmask transferUsedLanes(const MachineInstr &MI, LaneBitmask UsedLanes,
                                const MachineOperand &MO) const;

private:
  LaneBitmask determineInitialDefinedLanes(unsigned Reg);
  LaneBitmask determineInitialUsedLanes(unsigned Reg);

  const MachineRegisterInfo *MRI;
  const TargetRegisterInfo *TRI;

  void PutInWorklist(unsigned RegIdx) {
    if (WorklistMembers.test(RegIdx))
      return;
    WorklistMembers.set(RegIdx);
    Worklist.push_back(RegIdx);
  }

  std::unique_ptr<VRegInfo[]> VRegInfos;
  /// Worklist containing virtreg indexes.
  std::deque<unsigned> Worklist;
  BitVector WorklistMembers;
  /// This bitvector is set for each vreg index where the vreg is defined
  /// by an instruction where lowersToCopies()==true.
  BitVector DefinedByCopy;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_DETECTDEADLANES_H
