//===----- X86DynAllocaExpander.cpp - Expand DynAlloca pseudo instruction -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that expands DynAlloca pseudo-instructions.
//
// It performs a conservative analysis to determine whether each allocation
// falls within a region of the stack that is safe to use, or whether stack
// probes must be emitted.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class X86DynAllocaExpander : public MachineFunctionPass {
public:
  X86DynAllocaExpander() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  /// Strategies for lowering a DynAlloca.
  enum Lowering { TouchAndSub, Sub, Probe };

  /// Deterministic-order map from DynAlloca instruction to desired lowering.
  typedef MapVector<MachineInstr*, Lowering> LoweringMap;

  /// Compute which lowering to use for each DynAlloca instruction.
  void computeLowerings(MachineFunction &MF, LoweringMap& Lowerings);

  /// Get the appropriate lowering based on current offset and amount.
  Lowering getLowering(int64_t CurrentOffset, int64_t AllocaAmount);

  /// Lower a DynAlloca instruction.
  void lower(MachineInstr* MI, Lowering L);

  MachineRegisterInfo *MRI = nullptr;
  const X86Subtarget *STI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const X86RegisterInfo *TRI = nullptr;
  unsigned StackPtr = 0;
  unsigned SlotSize = 0;
  int64_t StackProbeSize = 0;
  bool NoStackArgProbe = false;

  StringRef getPassName() const override { return "X86 DynAlloca Expander"; }
  static char ID;
};

char X86DynAllocaExpander::ID = 0;

} // end anonymous namespace

FunctionPass *llvm::createX86DynAllocaExpander() {
  return new X86DynAllocaExpander();
}

/// Return the allocation amount for a DynAlloca instruction, or -1 if unknown.
static int64_t getDynAllocaAmount(MachineInstr *MI, MachineRegisterInfo *MRI) {
  assert(MI->getOpcode() == X86::DYN_ALLOCA_32 ||
         MI->getOpcode() == X86::DYN_ALLOCA_64);
  assert(MI->getOperand(0).isReg());

  Register AmountReg = MI->getOperand(0).getReg();
  MachineInstr *Def = MRI->getUniqueVRegDef(AmountReg);

  if (!Def ||
      (Def->getOpcode() != X86::MOV32ri && Def->getOpcode() != X86::MOV64ri) ||
      !Def->getOperand(1).isImm())
    return -1;

  return Def->getOperand(1).getImm();
}

X86DynAllocaExpander::Lowering
X86DynAllocaExpander::getLowering(int64_t CurrentOffset,
                                  int64_t AllocaAmount) {
  // For a non-constant amount or a large amount, we have to probe.
  if (AllocaAmount < 0 || AllocaAmount > StackProbeSize)
    return Probe;

  // If it fits within the safe region of the stack, just subtract.
  if (CurrentOffset + AllocaAmount <= StackProbeSize)
    return Sub;

  // Otherwise, touch the current tip of the stack, then subtract.
  return TouchAndSub;
}

static bool isPushPop(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case X86::PUSH32r:
  case X86::PUSH32rmm:
  case X86::PUSH32rmr:
  case X86::PUSH32i:
  case X86::PUSH64r:
  case X86::PUSH64rmm:
  case X86::PUSH64rmr:
  case X86::PUSH64i32:
  case X86::POP32r:
  case X86::POP64r:
    return true;
  default:
    return false;
  }
}

void X86DynAllocaExpander::computeLowerings(MachineFunction &MF,
                                            LoweringMap &Lowerings) {
  // Do a one-pass reverse post-order walk of the CFG to conservatively estimate
  // the offset between the stack pointer and the lowest touched part of the
  // stack, and use that to decide how to lower each DynAlloca instruction.

  // Initialize OutOffset[B], the stack offset at exit from B, to something big.
  DenseMap<MachineBasicBlock *, int64_t> OutOffset;
  for (MachineBasicBlock &MBB : MF)
    OutOffset[&MBB] = INT32_MAX;

  // Note: we don't know the offset at the start of the entry block since the
  // prologue hasn't been inserted yet, and how much that will adjust the stack
  // pointer depends on register spills, which have not been computed yet.

  // Compute the reverse post-order.
  ReversePostOrderTraversal<MachineFunction*> RPO(&MF);

  for (MachineBasicBlock *MBB : RPO) {
    int64_t Offset = -1;
    for (MachineBasicBlock *Pred : MBB->predecessors())
      Offset = std::max(Offset, OutOffset[Pred]);
    if (Offset == -1) Offset = INT32_MAX;

    for (MachineInstr &MI : *MBB) {
      if (MI.getOpcode() == X86::DYN_ALLOCA_32 ||
          MI.getOpcode() == X86::DYN_ALLOCA_64) {
        // A DynAlloca moves StackPtr, and potentially touches it.
        int64_t Amount = getDynAllocaAmount(&MI, MRI);
        Lowering L = getLowering(Offset, Amount);
        Lowerings[&MI] = L;
        switch (L) {
        case Sub:
          Offset += Amount;
          break;
        case TouchAndSub:
          Offset = Amount;
          break;
        case Probe:
          Offset = 0;
          break;
        }
      } else if (MI.isCall() || isPushPop(MI)) {
        // Calls, pushes and pops touch the tip of the stack.
        Offset = 0;
      } else if (MI.getOpcode() == X86::ADJCALLSTACKUP32 ||
                 MI.getOpcode() == X86::ADJCALLSTACKUP64) {
        Offset -= MI.getOperand(0).getImm();
      } else if (MI.getOpcode() == X86::ADJCALLSTACKDOWN32 ||
                 MI.getOpcode() == X86::ADJCALLSTACKDOWN64) {
        Offset += MI.getOperand(0).getImm();
      } else if (MI.modifiesRegister(StackPtr, TRI)) {
        // Any other modification of SP means we've lost track of it.
        Offset = INT32_MAX;
      }
    }

    OutOffset[MBB] = Offset;
  }
}

static unsigned getSubOpcode(bool Is64Bit) {
  if (Is64Bit)
    return X86::SUB64ri32;
  return X86::SUB32ri;
}

void X86DynAllocaExpander::lower(MachineInstr *MI, Lowering L) {
  const DebugLoc &DL = MI->getDebugLoc();
  MachineBasicBlock *MBB = MI->getParent();
  MachineBasicBlock::iterator I = *MI;

  int64_t Amount = getDynAllocaAmount(MI, MRI);
  if (Amount == 0) {
    MI->eraseFromParent();
    return;
  }

  // These two variables differ on x32, which is a 64-bit target with a
  // 32-bit alloca.
  bool Is64Bit = STI->is64Bit();
  bool Is64BitAlloca = MI->getOpcode() == X86::DYN_ALLOCA_64;
  assert(SlotSize == 4 || SlotSize == 8);

  std::optional<MachineFunction::DebugInstrOperandPair> InstrNum;
  if (unsigned Num = MI->peekDebugInstrNum()) {
    // Operand 2 of DYN_ALLOCAs contains the stack def.
    InstrNum = {Num, 2};
  }

  switch (L) {
  case TouchAndSub: {
    assert(Amount >= SlotSize);

    // Use a push to touch the top of the stack.
    unsigned RegA = Is64Bit ? X86::RAX : X86::EAX;
    BuildMI(*MBB, I, DL, TII->get(Is64Bit ? X86::PUSH64r : X86::PUSH32r))
        .addReg(RegA, RegState::Undef);
    Amount -= SlotSize;
    if (!Amount)
      break;

    // Fall through to make any remaining adjustment.
    [[fallthrough]];
  }
  case Sub:
    assert(Amount > 0);
    if (Amount == SlotSize) {
      // Use push to save size.
      unsigned RegA = Is64Bit ? X86::RAX : X86::EAX;
      BuildMI(*MBB, I, DL, TII->get(Is64Bit ? X86::PUSH64r : X86::PUSH32r))
          .addReg(RegA, RegState::Undef);
    } else {
      // Sub.
      BuildMI(*MBB, I, DL, TII->get(getSubOpcode(Is64BitAlloca)), StackPtr)
          .addReg(StackPtr)
          .addImm(Amount);
    }
    break;
  case Probe:
    if (!NoStackArgProbe) {
      // The probe lowering expects the amount in RAX/EAX.
      unsigned RegA = Is64BitAlloca ? X86::RAX : X86::EAX;
      BuildMI(*MBB, MI, DL, TII->get(TargetOpcode::COPY), RegA)
          .addReg(MI->getOperand(0).getReg());

      // Do the probe.
      STI->getFrameLowering()->emitStackProbe(*MBB->getParent(), *MBB, MI, DL,
                                              /*InProlog=*/false, InstrNum);
    } else {
      // Sub
      BuildMI(*MBB, I, DL,
              TII->get(Is64BitAlloca ? X86::SUB64rr : X86::SUB32rr), StackPtr)
          .addReg(StackPtr)
          .addReg(MI->getOperand(0).getReg());
    }
    break;
  }

  Register AmountReg = MI->getOperand(0).getReg();
  MI->eraseFromParent();

  // Delete the definition of AmountReg.
  if (MRI->use_empty(AmountReg))
    if (MachineInstr *AmountDef = MRI->getUniqueVRegDef(AmountReg))
      AmountDef->eraseFromParent();
}

bool X86DynAllocaExpander::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getInfo<X86MachineFunctionInfo>()->hasDynAlloca())
    return false;

  MRI = &MF.getRegInfo();
  STI = &MF.getSubtarget<X86Subtarget>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  StackPtr = TRI->getStackRegister();
  SlotSize = TRI->getSlotSize();
  StackProbeSize = STI->getTargetLowering()->getStackProbeSize(MF);
  NoStackArgProbe = MF.getFunction().hasFnAttribute("no-stack-arg-probe");
  if (NoStackArgProbe)
    StackProbeSize = INT64_MAX;

  LoweringMap Lowerings;
  computeLowerings(MF, Lowerings);
  for (auto &P : Lowerings)
    lower(P.first, P.second);

  return true;
}
