//===- HexagonBitTracker.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONBITTRACKER_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONBITTRACKER_H

#include "BitTracker.h"
#include "llvm/ADT/DenseMap.h"
#include <cstdint>

namespace llvm {

class HexagonInstrInfo;
class HexagonRegisterInfo;
class MachineFrameInfo;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;

struct HexagonEvaluator : public BitTracker::MachineEvaluator {
  using CellMapType = BitTracker::CellMapType;
  using RegisterRef = BitTracker::RegisterRef;
  using RegisterCell = BitTracker::RegisterCell;
  using BranchTargetList = BitTracker::BranchTargetList;

  HexagonEvaluator(const HexagonRegisterInfo &tri, MachineRegisterInfo &mri,
                   const HexagonInstrInfo &tii, MachineFunction &mf);

  bool evaluate(const MachineInstr &MI, const CellMapType &Inputs,
                CellMapType &Outputs) const override;
  bool evaluate(const MachineInstr &BI, const CellMapType &Inputs,
                BranchTargetList &Targets, bool &FallsThru) const override;

  BitTracker::BitMask mask(Register Reg, unsigned Sub) const override;

  uint16_t getPhysRegBitWidth(MCRegister Reg) const override;

  const TargetRegisterClass &composeWithSubRegIndex(
        const TargetRegisterClass &RC, unsigned Idx) const override;

  MachineFunction &MF;
  MachineFrameInfo &MFI;
  const HexagonInstrInfo &TII;

private:
  unsigned getUniqueDefVReg(const MachineInstr &MI) const;
  bool evaluateLoad(const MachineInstr &MI, const CellMapType &Inputs,
                    CellMapType &Outputs) const;
  bool evaluateFormalCopy(const MachineInstr &MI, const CellMapType &Inputs,
                          CellMapType &Outputs) const;

  unsigned getNextPhysReg(unsigned PReg, unsigned Width) const;
  unsigned getVirtRegFor(unsigned PReg) const;

  // Type of formal parameter extension.
  struct ExtType {
    enum { SExt, ZExt };

    ExtType() = default;
    ExtType(char t, uint16_t w) : Type(t), Width(w) {}

    char Type = 0;
    uint16_t Width = 0;
  };
  // Map VR -> extension type.
  using RegExtMap = DenseMap<unsigned, ExtType>;
  RegExtMap VRX;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONBITTRACKER_H
