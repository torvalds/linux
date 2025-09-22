//===- X86FixupSetCC.cpp - fix zero-extension of setcc patterns -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that fixes zero-extension of setcc patterns.
// X86 setcc instructions are modeled to have no input arguments, and a single
// GR8 output argument. This is consistent with other similar instructions
// (e.g. movb), but means it is impossible to directly generate a setcc into
// the lower GR8 of a specified GR32.
// This means that ISel must select (zext (setcc)) into something like
// seta %al; movzbl %al, %eax.
// Unfortunately, this can cause a stall due to the partial register write
// performed by the setcc. Instead, we can use:
// xor %eax, %eax; seta %al
// This both avoids the stall, and encodes shorter.
//
// Furthurmore, we can use:
// setzua %al
// if feature zero-upper is available. It's faster than the xor+setcc sequence.
// When r16-r31 is used, it even encodes shorter.
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "x86-fixup-setcc"

STATISTIC(NumSubstZexts, "Number of setcc + zext pairs substituted");

namespace {
class X86FixupSetCCPass : public MachineFunctionPass {
public:
  static char ID;

  X86FixupSetCCPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "X86 Fixup SetCC"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  MachineRegisterInfo *MRI = nullptr;
  const X86Subtarget *ST = nullptr;
  const X86InstrInfo *TII = nullptr;

  enum { SearchBound = 16 };
};
} // end anonymous namespace

char X86FixupSetCCPass::ID = 0;

INITIALIZE_PASS(X86FixupSetCCPass, DEBUG_TYPE, DEBUG_TYPE, false, false)

FunctionPass *llvm::createX86FixupSetCC() { return new X86FixupSetCCPass(); }

bool X86FixupSetCCPass::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  MRI = &MF.getRegInfo();
  ST = &MF.getSubtarget<X86Subtarget>();
  TII = ST->getInstrInfo();

  SmallVector<MachineInstr*, 4> ToErase;

  for (auto &MBB : MF) {
    MachineInstr *FlagsDefMI = nullptr;
    for (auto &MI : MBB) {
      // Remember the most recent preceding eflags defining instruction.
      if (MI.definesRegister(X86::EFLAGS, /*TRI=*/nullptr))
        FlagsDefMI = &MI;

      // Find a setcc that is used by a zext.
      // This doesn't have to be the only use, the transformation is safe
      // regardless.
      if (MI.getOpcode() != X86::SETCCr)
        continue;

      MachineInstr *ZExt = nullptr;
      Register Reg0 = MI.getOperand(0).getReg();
      for (auto &Use : MRI->use_instructions(Reg0))
        if (Use.getOpcode() == X86::MOVZX32rr8)
          ZExt = &Use;

      if (!ZExt)
        continue;

      if (!FlagsDefMI)
        continue;

      // We'd like to put something that clobbers eflags directly before
      // FlagsDefMI. This can't hurt anything after FlagsDefMI, because
      // it, itself, by definition, clobbers eflags. But it may happen that
      // FlagsDefMI also *uses* eflags, in which case the transformation is
      // invalid.
      if (FlagsDefMI->readsRegister(X86::EFLAGS, /*TRI=*/nullptr))
        continue;

      // On 32-bit, we need to be careful to force an ABCD register.
      const TargetRegisterClass *RC =
          ST->is64Bit() ? &X86::GR32RegClass : &X86::GR32_ABCDRegClass;
      if (!MRI->constrainRegClass(ZExt->getOperand(0).getReg(), RC)) {
        // If we cannot constrain the register, we would need an additional copy
        // and are better off keeping the MOVZX32rr8 we have now.
        continue;
      }

      ++NumSubstZexts;
      Changed = true;

      // X86 setcc/setzucc only takes an output GR8, so fake a GR32 input by
      // inserting the setcc/setzucc result into the low byte of the zeroed
      // register.
      Register ZeroReg = MRI->createVirtualRegister(RC);
      if (ST->hasZU()) {
        MI.setDesc(TII->get(X86::SETZUCCr));
        BuildMI(*ZExt->getParent(), ZExt, ZExt->getDebugLoc(),
                TII->get(TargetOpcode::IMPLICIT_DEF), ZeroReg);
      } else {
        // Initialize a register with 0. This must go before the eflags def
        BuildMI(MBB, FlagsDefMI, MI.getDebugLoc(), TII->get(X86::MOV32r0),
                ZeroReg);
      }

      BuildMI(*ZExt->getParent(), ZExt, ZExt->getDebugLoc(),
              TII->get(X86::INSERT_SUBREG), ZExt->getOperand(0).getReg())
          .addReg(ZeroReg)
          .addReg(Reg0)
          .addImm(X86::sub_8bit);
      ToErase.push_back(ZExt);
    }
  }

  for (auto &I : ToErase)
    I->eraseFromParent();

  return Changed;
}
