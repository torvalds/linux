//===- llvm/CodeGen/VirtRegMap.h - Virtual Register Map ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a virtual register map. This maps virtual registers to
// physical registers and virtual registers to stack slots. It is created and
// updated by a register allocator and then used by a machine code rewriter that
// adds spill code and rewrites virtual into physical register references.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_VIRTREGMAP_H
#define LLVM_CODEGEN_VIRTREGMAP_H

#include "llvm/ADT/IndexedMap.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TileShapeInfo.h"
#include "llvm/Pass.h"
#include <cassert>

namespace llvm {

class MachineFunction;
class MachineRegisterInfo;
class raw_ostream;
class TargetInstrInfo;

  class VirtRegMap : public MachineFunctionPass {
  public:
    enum {
      NO_PHYS_REG = 0,
      NO_STACK_SLOT = (1L << 30)-1,
      MAX_STACK_SLOT = (1L << 18)-1
    };

  private:
    MachineRegisterInfo *MRI = nullptr;
    const TargetInstrInfo *TII = nullptr;
    const TargetRegisterInfo *TRI = nullptr;
    MachineFunction *MF = nullptr;

    /// Virt2PhysMap - This is a virtual to physical register
    /// mapping. Each virtual register is required to have an entry in
    /// it; even spilled virtual registers (the register mapped to a
    /// spilled register is the temporary used to load it from the
    /// stack).
    IndexedMap<Register, VirtReg2IndexFunctor> Virt2PhysMap;

    /// Virt2StackSlotMap - This is virtual register to stack slot
    /// mapping. Each spilled virtual register has an entry in it
    /// which corresponds to the stack slot this register is spilled
    /// at.
    IndexedMap<int, VirtReg2IndexFunctor> Virt2StackSlotMap;

    /// Virt2SplitMap - This is virtual register to splitted virtual register
    /// mapping.
    IndexedMap<unsigned, VirtReg2IndexFunctor> Virt2SplitMap;

    /// Virt2ShapeMap - For X86 AMX register whose register is bound shape
    /// information.
    DenseMap<unsigned, ShapeT> Virt2ShapeMap;

    /// createSpillSlot - Allocate a spill slot for RC from MFI.
    unsigned createSpillSlot(const TargetRegisterClass *RC);

  public:
    static char ID;

    VirtRegMap()
        : MachineFunctionPass(ID), Virt2PhysMap(NO_PHYS_REG),
          Virt2StackSlotMap(NO_STACK_SLOT), Virt2SplitMap(0) {}
    VirtRegMap(const VirtRegMap &) = delete;
    VirtRegMap &operator=(const VirtRegMap &) = delete;

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    MachineFunction &getMachineFunction() const {
      assert(MF && "getMachineFunction called before runOnMachineFunction");
      return *MF;
    }

    MachineRegisterInfo &getRegInfo() const { return *MRI; }
    const TargetRegisterInfo &getTargetRegInfo() const { return *TRI; }

    void grow();

    /// returns true if the specified virtual register is
    /// mapped to a physical register
    bool hasPhys(Register virtReg) const {
      return getPhys(virtReg) != NO_PHYS_REG;
    }

    /// returns the physical register mapped to the specified
    /// virtual register
    MCRegister getPhys(Register virtReg) const {
      assert(virtReg.isVirtual());
      return MCRegister::from(Virt2PhysMap[virtReg.id()]);
    }

    /// creates a mapping for the specified virtual register to
    /// the specified physical register
    void assignVirt2Phys(Register virtReg, MCPhysReg physReg);

    bool isShapeMapEmpty() const { return Virt2ShapeMap.empty(); }

    bool hasShape(Register virtReg) const {
      return getShape(virtReg).isValid();
    }

    ShapeT getShape(Register virtReg) const {
      assert(virtReg.isVirtual());
      return Virt2ShapeMap.lookup(virtReg);
    }

    void assignVirt2Shape(Register virtReg, ShapeT shape) {
      Virt2ShapeMap[virtReg.id()] = shape;
    }

    /// clears the specified virtual register's, physical
    /// register mapping
    void clearVirt(Register virtReg) {
      assert(virtReg.isVirtual());
      assert(Virt2PhysMap[virtReg.id()] != NO_PHYS_REG &&
             "attempt to clear a not assigned virtual register");
      Virt2PhysMap[virtReg.id()] = NO_PHYS_REG;
    }

    /// clears all virtual to physical register mappings
    void clearAllVirt() {
      Virt2PhysMap.clear();
      grow();
    }

    /// returns true if VirtReg is assigned to its preferred physreg.
    bool hasPreferredPhys(Register VirtReg) const;

    /// returns true if VirtReg has a known preferred register.
    /// This returns false if VirtReg has a preference that is a virtual
    /// register that hasn't been assigned yet.
    bool hasKnownPreference(Register VirtReg) const;

    /// records virtReg is a split live interval from SReg.
    void setIsSplitFromReg(Register virtReg, Register SReg) {
      Virt2SplitMap[virtReg.id()] = SReg;
      if (hasShape(SReg)) {
        Virt2ShapeMap[virtReg.id()] = getShape(SReg);
      }
    }

    /// returns the live interval virtReg is split from.
    Register getPreSplitReg(Register virtReg) const {
      return Virt2SplitMap[virtReg.id()];
    }

    /// getOriginal - Return the original virtual register that VirtReg descends
    /// from through splitting.
    /// A register that was not created by splitting is its own original.
    /// This operation is idempotent.
    Register getOriginal(Register VirtReg) const {
      Register Orig = getPreSplitReg(VirtReg);
      return Orig ? Orig : VirtReg;
    }

    /// returns true if the specified virtual register is not
    /// mapped to a stack slot or rematerialized.
    bool isAssignedReg(Register virtReg) const {
      if (getStackSlot(virtReg) == NO_STACK_SLOT)
        return true;
      // Split register can be assigned a physical register as well as a
      // stack slot or remat id.
      return (Virt2SplitMap[virtReg.id()] &&
              Virt2PhysMap[virtReg.id()] != NO_PHYS_REG);
    }

    /// returns the stack slot mapped to the specified virtual
    /// register
    int getStackSlot(Register virtReg) const {
      assert(virtReg.isVirtual());
      return Virt2StackSlotMap[virtReg.id()];
    }

    /// create a mapping for the specifed virtual register to
    /// the next available stack slot
    int assignVirt2StackSlot(Register virtReg);

    /// create a mapping for the specified virtual register to
    /// the specified stack slot
    void assignVirt2StackSlot(Register virtReg, int SS);

    void print(raw_ostream &OS, const Module* M = nullptr) const override;
    void dump() const;
  };

  inline raw_ostream &operator<<(raw_ostream &OS, const VirtRegMap &VRM) {
    VRM.print(OS);
    return OS;
  }

} // end llvm namespace

#endif // LLVM_CODEGEN_VIRTREGMAP_H
