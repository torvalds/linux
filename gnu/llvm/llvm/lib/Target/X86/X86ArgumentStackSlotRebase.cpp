//===---- X86ArgumentStackSlotRebase.cpp - rebase argument stack slot -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass replace the frame register with a GPR virtual register and set
// the stack offset for each instruction which reference argument from stack.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "x86argumentstackrebase"

namespace {

class X86ArgumentStackSlotPass : public MachineFunctionPass {

public:
  static char ID; // Pass identification, replacement for typeid

  explicit X86ArgumentStackSlotPass() : MachineFunctionPass(ID) {
    initializeX86ArgumentStackSlotPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char X86ArgumentStackSlotPass::ID = 0;

INITIALIZE_PASS(X86ArgumentStackSlotPass, DEBUG_TYPE, "Argument Stack Rebase",
                false, false)

FunctionPass *llvm::createX86ArgumentStackSlotPass() {
  return new X86ArgumentStackSlotPass();
}

static Register getArgBaseReg(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const Function &F = MF.getFunction();
  CallingConv::ID CC = F.getCallingConv();
  Register NoReg;
  const TargetRegisterClass *RC = nullptr;
  switch (CC) {
  // We need a virtual register in case there is inline assembly
  // clobber argument base register.
  case CallingConv::C:
    RC = STI.is64Bit() ? &X86::GR64_ArgRefRegClass : &X86::GR32_ArgRefRegClass;
    break;
  case CallingConv::X86_RegCall:
    // FIXME: For regcall there is no scratch register on 32-bit target.
    // We may use a callee saved register as argument base register and
    // save it before being changed as base pointer. We need DW_CFA to
    // indicate where the callee saved register is saved, so that it can
    // be correctly unwind.
    // push      ebx
    // mov       ebx, esp
    // and       esp, -128
    // ...
    // pop       ebx
    // ret
    RC = STI.is64Bit() ? &X86::GR64_ArgRefRegClass : nullptr;
    break;
  // TODO: Refine register class for each calling convention.
  default:
    break;
  }
  if (RC)
    return MRI.createVirtualRegister(RC);
  else
    return NoReg;
}

bool X86ArgumentStackSlotPass::runOnMachineFunction(MachineFunction &MF) {
  const Function &F = MF.getFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86RegisterInfo *TRI = STI.getRegisterInfo();
  const X86InstrInfo *TII = STI.getInstrInfo();
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  bool Changed = false;

  if (F.hasFnAttribute(Attribute::Naked))
    return false;
  // Only support Linux and ELF.
  if (!STI.isTargetLinux() && !STI.isTargetELF())
    return false;
  if (!TRI->hasBasePointer(MF))
    return false;
  // Don't support X32
  if (STI.isTarget64BitILP32())
    return false;

  Register BasePtr = TRI->getBaseRegister();
  auto IsBaseRegisterClobbered = [&]() {
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (!MI.isInlineAsm())
          continue;
        for (MachineOperand &MO : MI.operands()) {
          if (!MO.isReg())
            continue;
          Register Reg = MO.getReg();
          if (!Register::isPhysicalRegister(Reg))
            continue;
          if (TRI->isSuperOrSubRegisterEq(BasePtr, Reg))
            return true;
        }
      }
    }
    return false;
  };
  if (!IsBaseRegisterClobbered())
    return false;

  Register ArgBaseReg = getArgBaseReg(MF);
  if (!ArgBaseReg.isValid())
    return false;
  // leal    4(%esp), %reg
  MachineBasicBlock &MBB = MF.front();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;
  // Emit instruction to copy get stack pointer to a virtual register
  // and save the instruction to x86 machine functon info. We can get
  // physical register of ArgBaseReg after register allocation. The
  // stack slot is used to save/restore argument base pointer. We can
  // get the index from the instruction.
  unsigned SlotSize = TRI->getSlotSize();
  int FI = MFI.CreateSpillStackObject(SlotSize, Align(SlotSize));
  // Use pseudo LEA to prevent the instruction from being eliminated.
  // TODO: if it is duplicated we can expand it to lea.
  MachineInstr *LEA =
      BuildMI(MBB, MBBI, DL,
              TII->get(STI.is64Bit() ? X86::PLEA64r : X86::PLEA32r), ArgBaseReg)
          .addFrameIndex(FI)
          .addImm(1)
          .addUse(X86::NoRegister)
          .addImm(SlotSize)
          .addUse(X86::NoRegister)
          .setMIFlag(MachineInstr::FrameSetup);
  X86FI->setStackPtrSaveMI(LEA);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      int I = 0;
      for (MachineOperand &MO : MI.operands()) {
        if (MO.isFI()) {
          int Idx = MO.getIndex();
          if (!MFI.isFixedObjectIndex(Idx))
            continue;
          int64_t Offset = MFI.getObjectOffset(Idx);
          if (Offset < 0)
            continue;
          // TODO replace register for debug instruction
          if (MI.isDebugInstr())
            continue;
          // Replace frame register with argument base pointer and its offset.
          TRI->eliminateFrameIndex(MI.getIterator(), I, ArgBaseReg, Offset);
          Changed = true;
        }
        ++I;
      }
    }
  }

  return Changed;
}
