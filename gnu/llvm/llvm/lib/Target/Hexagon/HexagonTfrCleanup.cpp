//===------- HexagonTfrCleanup.cpp - Hexagon Transfer Cleanup Pass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This pass is to address a situation that appears after register allocaion
// evey now and then, namely a register copy from a source that was defined
// as an immediate value in the same block (usually just before the copy).
//
// Here is an example of actual code emitted that shows this problem:
//
//  .LBB0_5:
//  {
//    r5 = zxtb(r8)
//    r6 = or(r6, ##12345)
//  }
//  {
//    r3 = xor(r1, r2)
//    r1 = #0               <-- r1 set to #0
//  }
//  {
//    r7 = r1               <-- r7 set to r1
//    r0 = zxtb(r3)
//  }

#define DEBUG_TYPE "tfr-cleanup"
#include "HexagonTargetMachine.h"

#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace llvm {
FunctionPass *createHexagonTfrCleanup();
void initializeHexagonTfrCleanupPass(PassRegistry &);
} // namespace llvm

namespace {
class HexagonTfrCleanup : public MachineFunctionPass {
public:
  static char ID;
  HexagonTfrCleanup() : MachineFunctionPass(ID), HII(0), TRI(0) {
    PassRegistry &R = *PassRegistry::getPassRegistry();
    initializeHexagonTfrCleanupPass(R);
  }
  StringRef getPassName() const override { return "Hexagon TFR Cleanup"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  const HexagonInstrInfo *HII;
  const TargetRegisterInfo *TRI;

  typedef DenseMap<unsigned, uint64_t> ImmediateMap;

  bool isIntReg(unsigned Reg, bool &Is32);
  void setReg(unsigned R32, uint32_t V32, ImmediateMap &IMap);
  bool getReg(unsigned Reg, uint64_t &Val, ImmediateMap &IMap);
  bool updateImmMap(MachineInstr *MI, ImmediateMap &IMap);
  bool rewriteIfImm(MachineInstr *MI, ImmediateMap &IMap, SlotIndexes *Indexes);
  bool eraseIfRedundant(MachineInstr *MI, SlotIndexes *Indexes);
};
} // namespace

char HexagonTfrCleanup::ID = 0;

namespace llvm {
char &HexagonTfrCleanupID = HexagonTfrCleanup::ID;
}

bool HexagonTfrCleanup::isIntReg(unsigned Reg, bool &Is32) {
  Is32 = Hexagon::IntRegsRegClass.contains(Reg);
  return Is32 || Hexagon::DoubleRegsRegClass.contains(Reg);
}

// Assign given value V32 to the specified the register R32 in the map. Only
// 32-bit registers are valid arguments.
void HexagonTfrCleanup::setReg(unsigned R32, uint32_t V32, ImmediateMap &IMap) {
  ImmediateMap::iterator F = IMap.find(R32);
  if (F == IMap.end())
    IMap.insert(std::make_pair(R32, V32));
  else
    F->second = V32;
}

// Retrieve a value of the provided register Reg and store it into Val.
// Return "true" if a value was found, "false" otherwise.
bool HexagonTfrCleanup::getReg(unsigned Reg, uint64_t &Val,
                               ImmediateMap &IMap) {
  bool Is32;
  if (!isIntReg(Reg, Is32))
    return false;

  if (Is32) {
    ImmediateMap::iterator F = IMap.find(Reg);
    if (F == IMap.end())
      return false;
    Val = F->second;
    return true;
  }

  // For 64-bit registers, compose the value from the values of its
  // subregisters.
  unsigned SubL = TRI->getSubReg(Reg, Hexagon::isub_lo);
  unsigned SubH = TRI->getSubReg(Reg, Hexagon::isub_hi);
  ImmediateMap::iterator FL = IMap.find(SubL), FH = IMap.find(SubH);
  if (FL == IMap.end() || FH == IMap.end())
    return false;
  Val = (FH->second << 32) | FL->second;
  return true;
}

// Process an instruction and record the relevant information in the imme-
// diate map.
bool HexagonTfrCleanup::updateImmMap(MachineInstr *MI, ImmediateMap &IMap) {
  using namespace Hexagon;

  if (MI->isCall()) {
    IMap.clear();
    return true;
  }

  // If this is an instruction that loads a constant into a register,
  // record this information in IMap.
  unsigned Opc = MI->getOpcode();
  if (Opc == A2_tfrsi || Opc == A2_tfrpi) {
    unsigned DefR = MI->getOperand(0).getReg();
    bool Is32;
    if (!isIntReg(DefR, Is32))
      return false;
    if (!MI->getOperand(1).isImm()) {
      if (!Is32) {
        IMap.erase(TRI->getSubReg(DefR, isub_lo));
        IMap.erase(TRI->getSubReg(DefR, isub_hi));
      } else {
        IMap.erase(DefR);
      }
      return false;
    }
    uint64_t Val = MI->getOperand(1).getImm();
    // If it's a 64-bit register, break it up into subregisters.
    if (!Is32) {
      uint32_t VH = (Val >> 32), VL = (Val & 0xFFFFFFFFU);
      setReg(TRI->getSubReg(DefR, isub_lo), VL, IMap);
      setReg(TRI->getSubReg(DefR, isub_hi), VH, IMap);
    } else {
      setReg(DefR, Val, IMap);
    }
    return true;
  }

  // Not a A2_tfr[sp]i. Invalidate all modified registers in IMap.
  for (MachineInstr::mop_iterator Mo = MI->operands_begin(),
                                  E = MI->operands_end();
       Mo != E; ++Mo) {
    if (Mo->isRegMask()) {
      IMap.clear();
      return true;
    }
    if (!Mo->isReg() || !Mo->isDef())
      continue;
    unsigned R = Mo->getReg();
    for (MCRegAliasIterator AR(R, TRI, true); AR.isValid(); ++AR) {
      ImmediateMap::iterator F = IMap.find(*AR);
      if (F != IMap.end())
        IMap.erase(F);
    }
  }
  return true;
}

// Rewrite the instruction as A2_tfrsi/A2_tfrpi, it is a copy of a source that
// has a known constant value.
bool HexagonTfrCleanup::rewriteIfImm(MachineInstr *MI, ImmediateMap &IMap,
                                     SlotIndexes *Indexes) {
  using namespace Hexagon;
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
  case A2_tfr:
  case A2_tfrp:
  case COPY:
    break;
  default:
    return false;
  }

  unsigned DstR = MI->getOperand(0).getReg();
  unsigned SrcR = MI->getOperand(1).getReg();
  bool Tmp, Is32;
  if (!isIntReg(DstR, Is32) || !isIntReg(SrcR, Tmp))
    return false;
  assert(Tmp == Is32 && "Register size mismatch");
  uint64_t Val;
  bool Found = getReg(SrcR, Val, IMap);
  if (!Found)
    return false;

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  int64_t SVal = Is32 ? int32_t(Val) : Val;
  auto &HST = B.getParent()->getSubtarget<HexagonSubtarget>();
  MachineInstr *NewMI;
  if (Is32)
    NewMI = BuildMI(B, MI, DL, HII->get(A2_tfrsi), DstR).addImm(SVal);
  else if (isInt<8>(SVal))
    NewMI = BuildMI(B, MI, DL, HII->get(A2_tfrpi), DstR).addImm(SVal);
  else if (isInt<8>(SVal >> 32) && isInt<8>(int32_t(Val & 0xFFFFFFFFLL)))
    NewMI = BuildMI(B, MI, DL, HII->get(A2_combineii), DstR)
                .addImm(int32_t(SVal >> 32))
                .addImm(int32_t(Val & 0xFFFFFFFFLL));
  else if (HST.isTinyCore())
    // Disable generating CONST64 since it requires load resource.
    return false;
  else
    NewMI = BuildMI(B, MI, DL, HII->get(CONST64), DstR).addImm(Val);

  // Replace the MI to reuse the same slot index
  if (Indexes)
    Indexes->replaceMachineInstrInMaps(*MI, *NewMI);
  MI->eraseFromParent();
  return true;
}

// Remove the instruction if it is a self-assignment.
bool HexagonTfrCleanup::eraseIfRedundant(MachineInstr *MI,
                                         SlotIndexes *Indexes) {
  unsigned Opc = MI->getOpcode();
  unsigned DefR, SrcR;
  bool IsUndef = false;
  switch (Opc) {
  case Hexagon::A2_tfr:
    // Rd = Rd
    DefR = MI->getOperand(0).getReg();
    SrcR = MI->getOperand(1).getReg();
    IsUndef = MI->getOperand(1).isUndef();
    break;
  case Hexagon::A2_tfrt:
  case Hexagon::A2_tfrf:
    // if ([!]Pu) Rd = Rd
    DefR = MI->getOperand(0).getReg();
    SrcR = MI->getOperand(2).getReg();
    IsUndef = MI->getOperand(2).isUndef();
    break;
  default:
    return false;
  }
  if (DefR != SrcR)
    return false;
  if (IsUndef) {
    MachineBasicBlock &B = *MI->getParent();
    DebugLoc DL = MI->getDebugLoc();
    auto DefI = BuildMI(B, MI, DL, HII->get(TargetOpcode::IMPLICIT_DEF), DefR);
    for (auto &Op : MI->operands())
      if (Op.isReg() && Op.isDef() && Op.isImplicit())
        DefI->addOperand(Op);
  }

  if (Indexes)
    Indexes->removeMachineInstrFromMaps(*MI);
  MI->eraseFromParent();
  return true;
}

bool HexagonTfrCleanup::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  // Map: 32-bit register -> immediate value.
  // 64-bit registers are stored through their subregisters.
  ImmediateMap IMap;
  auto *SIWrapper = getAnalysisIfAvailable<SlotIndexesWrapperPass>();
  SlotIndexes *Indexes = SIWrapper ? &SIWrapper->getSI() : nullptr;

  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  HII = HST.getInstrInfo();
  TRI = HST.getRegisterInfo();

  for (MachineBasicBlock &B : MF) {
    MachineBasicBlock::iterator J, F, NextJ;
    IMap.clear();
    bool Inserted = false, Erased = false;
    for (J = B.begin(), F = B.end(); J != F; J = NextJ) {
      NextJ = std::next(J);
      MachineInstr *MI = &*J;
      bool E = eraseIfRedundant(MI, Indexes);
      Erased |= E;
      if (E)
        continue;
      Inserted |= rewriteIfImm(MI, IMap, Indexes);
      MachineBasicBlock::iterator NewJ = std::prev(NextJ);
      updateImmMap(&*NewJ, IMap);
    }
    bool BlockC = Inserted | Erased;
    Changed |= BlockC;
    if (BlockC && Indexes)
      Indexes->repairIndexesInRange(&B, B.begin(), B.end());
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//
INITIALIZE_PASS(HexagonTfrCleanup, "tfr-cleanup", "Hexagon TFR Cleanup", false,
                false)

FunctionPass *llvm::createHexagonTfrCleanup() {
  return new HexagonTfrCleanup();
}
