//===- RegisterClassInfo.h - Dynamic Register Class Info --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RegisterClassInfo class which provides dynamic
// information about target register classes. Callee saved and reserved
// registers depends on calling conventions and other dynamic information, so
// some things cannot be determined statically.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERCLASSINFO_H
#define LLVM_CODEGEN_REGISTERCLASSINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegister.h"
#include <cstdint>
#include <memory>

namespace llvm {

class RegisterClassInfo {
  struct RCInfo {
    unsigned Tag = 0;
    unsigned NumRegs = 0;
    bool ProperSubClass = false;
    uint8_t MinCost = 0;
    uint16_t LastCostChange = 0;
    std::unique_ptr<MCPhysReg[]> Order;

    RCInfo() = default;

    operator ArrayRef<MCPhysReg>() const {
      return ArrayRef(Order.get(), NumRegs);
    }
  };

  // Brief cached information for each register class.
  std::unique_ptr<RCInfo[]> RegClass;

  // Tag changes whenever cached information needs to be recomputed. An RCInfo
  // entry is valid when its tag matches.
  unsigned Tag = 0;

  const MachineFunction *MF = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  // Callee saved registers of last MF.
  // Used only to determine if an update for CalleeSavedAliases is necessary.
  SmallVector<MCPhysReg, 16> LastCalleeSavedRegs;

  // Map regunit to the callee saved Register.
  SmallVector<MCPhysReg> CalleeSavedAliases;

  // Indicate if a specified callee saved register be in the allocation order
  // exactly as written in the tablegen descriptions or listed later.
  BitVector IgnoreCSRForAllocOrder;

  // Reserved registers in the current MF.
  BitVector Reserved;

  std::unique_ptr<unsigned[]> PSetLimits;

  // The register cost values.
  ArrayRef<uint8_t> RegCosts;

  // Compute all information about RC.
  void compute(const TargetRegisterClass *RC) const;

  // Return an up-to-date RCInfo for RC.
  const RCInfo &get(const TargetRegisterClass *RC) const {
    const RCInfo &RCI = RegClass[RC->getID()];
    if (Tag != RCI.Tag)
      compute(RC);
    return RCI;
  }

public:
  RegisterClassInfo();

  /// runOnFunction - Prepare to answer questions about MF. This must be called
  /// before any other methods are used.
  void runOnMachineFunction(const MachineFunction &MF);

  /// getNumAllocatableRegs - Returns the number of actually allocatable
  /// registers in RC in the current function.
  unsigned getNumAllocatableRegs(const TargetRegisterClass *RC) const {
    return get(RC).NumRegs;
  }

  /// getOrder - Returns the preferred allocation order for RC. The order
  /// contains no reserved registers, and registers that alias callee saved
  /// registers come last.
  ArrayRef<MCPhysReg> getOrder(const TargetRegisterClass *RC) const {
    return get(RC);
  }

  /// isProperSubClass - Returns true if RC has a legal super-class with more
  /// allocatable registers.
  ///
  /// Register classes like GR32_NOSP are not proper sub-classes because %esp
  /// is not allocatable.  Similarly, tGPR is not a proper sub-class in Thumb
  /// mode because the GPR super-class is not legal.
  bool isProperSubClass(const TargetRegisterClass *RC) const {
    return get(RC).ProperSubClass;
  }

  /// getLastCalleeSavedAlias - Returns the last callee saved register that
  /// overlaps PhysReg, or NoRegister if PhysReg doesn't overlap a
  /// CalleeSavedAliases.
  MCRegister getLastCalleeSavedAlias(MCRegister PhysReg) const {
    MCRegister CSR;
    for (MCRegUnitIterator UI(PhysReg, TRI); UI.isValid(); ++UI) {
      CSR = CalleeSavedAliases[*UI];
      if (CSR)
        break;
    }
    return CSR;
  }

  /// Get the minimum register cost in RC's allocation order.
  /// This is the smallest value in RegCosts[Reg] for all
  /// the registers in getOrder(RC).
  uint8_t getMinCost(const TargetRegisterClass *RC) const {
    return get(RC).MinCost;
  }

  /// Get the position of the last cost change in getOrder(RC).
  ///
  /// All registers in getOrder(RC).slice(getLastCostChange(RC)) will have the
  /// same cost according to RegCosts[Reg].
  unsigned getLastCostChange(const TargetRegisterClass *RC) const {
    return get(RC).LastCostChange;
  }

  /// Get the register unit limit for the given pressure set index.
  ///
  /// RegisterClassInfo adjusts this limit for reserved registers.
  unsigned getRegPressureSetLimit(unsigned Idx) const {
    if (!PSetLimits[Idx])
      PSetLimits[Idx] = computePSetLimit(Idx);
    return PSetLimits[Idx];
  }

protected:
  unsigned computePSetLimit(unsigned Idx) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERCLASSINFO_H
