//===- RegisterCoalescer.h - Register Coalescing Interface ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the abstract interface for register coalescers,
// allowing them to interact with and query register allocators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_REGISTERCOALESCER_H
#define LLVM_LIB_CODEGEN_REGISTERCOALESCER_H

#include "llvm/CodeGen/Register.h"

namespace llvm {

class MachineInstr;
class TargetRegisterClass;
class TargetRegisterInfo;

  /// A helper class for register coalescers. When deciding if
  /// two registers can be coalesced, CoalescerPair can determine if a copy
  /// instruction would become an identity copy after coalescing.
  class CoalescerPair {
    const TargetRegisterInfo &TRI;

    /// The register that will be left after coalescing. It can be a
    /// virtual or physical register.
    Register DstReg;

    /// The virtual register that will be coalesced into dstReg.
    Register SrcReg;

    /// The sub-register index of the old DstReg in the new coalesced register.
    unsigned DstIdx = 0;

    /// The sub-register index of the old SrcReg in the new coalesced register.
    unsigned SrcIdx = 0;

    /// True when the original copy was a partial subregister copy.
    bool Partial = false;

    /// True when both regs are virtual and newRC is constrained.
    bool CrossClass = false;

    /// True when DstReg and SrcReg are reversed from the original
    /// copy instruction.
    bool Flipped = false;

    /// The register class of the coalesced register, or NULL if DstReg
    /// is a physreg. This register class may be a super-register of both
    /// SrcReg and DstReg.
    const TargetRegisterClass *NewRC = nullptr;

  public:
    CoalescerPair(const TargetRegisterInfo &tri) : TRI(tri) {}

    /// Create a CoalescerPair representing a virtreg-to-physreg copy.
    /// No need to call setRegisters().
    CoalescerPair(Register VirtReg, MCRegister PhysReg,
                  const TargetRegisterInfo &tri)
        : TRI(tri), DstReg(PhysReg), SrcReg(VirtReg) {}

    /// Set registers to match the copy instruction MI. Return
    /// false if MI is not a coalescable copy instruction.
    bool setRegisters(const MachineInstr*);

    /// Swap SrcReg and DstReg. Return false if swapping is impossible
    /// because DstReg is a physical register, or SubIdx is set.
    bool flip();

    /// Return true if MI is a copy instruction that will become
    /// an identity copy after coalescing.
    bool isCoalescable(const MachineInstr*) const;

    /// Return true if DstReg is a physical register.
    bool isPhys() const { return !NewRC; }

    /// Return true if the original copy instruction did not copy
    /// the full register, but was a subreg operation.
    bool isPartial() const { return Partial; }

    /// Return true if DstReg is virtual and NewRC is a smaller
    /// register class than DstReg's.
    bool isCrossClass() const { return CrossClass; }

    /// Return true when getSrcReg is the register being defined by
    /// the original copy instruction.
    bool isFlipped() const { return Flipped; }

    /// Return the register (virtual or physical) that will remain
    /// after coalescing.
    Register getDstReg() const { return DstReg; }

    /// Return the virtual register that will be coalesced away.
    Register getSrcReg() const { return SrcReg; }

    /// Return the subregister index that DstReg will be coalesced into, or 0.
    unsigned getDstIdx() const { return DstIdx; }

    /// Return the subregister index that SrcReg will be coalesced into, or 0.
    unsigned getSrcIdx() const { return SrcIdx; }

    /// Return the register class of the coalesced register.
    const TargetRegisterClass *getNewRC() const { return NewRC; }
  };

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_REGISTERCOALESCER_H
