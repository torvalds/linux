//===- MipsOptimizePICCall.cpp - Optimize PIC Calls -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass eliminates unnecessary instructions that set up $gp and replace
// instructions that load target function addresses with copy instructions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MipsBaseInfo.h"
#include "Mips.h"
#include "MipsRegisterInfo.h"
#include "MipsSubtarget.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/RecyclingAllocator.h"
#include <cassert>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "optimize-mips-pic-call"

static cl::opt<bool> LoadTargetFromGOT("mips-load-target-from-got",
                                       cl::init(true),
                                       cl::desc("Load target address from GOT"),
                                       cl::Hidden);

static cl::opt<bool> EraseGPOpnd("mips-erase-gp-opnd",
                                 cl::init(true), cl::desc("Erase GP Operand"),
                                 cl::Hidden);

namespace {

using ValueType = PointerUnion<const Value *, const PseudoSourceValue *>;
using CntRegP = std::pair<unsigned, unsigned>;
using AllocatorTy = RecyclingAllocator<BumpPtrAllocator,
                                       ScopedHashTableVal<ValueType, CntRegP>>;
using ScopedHTType = ScopedHashTable<ValueType, CntRegP,
                                     DenseMapInfo<ValueType>, AllocatorTy>;

class MBBInfo {
public:
  MBBInfo(MachineDomTreeNode *N);

  const MachineDomTreeNode *getNode() const;
  bool isVisited() const;
  void preVisit(ScopedHTType &ScopedHT);
  void postVisit();

private:
  MachineDomTreeNode *Node;
  ScopedHTType::ScopeTy *HTScope;
};

class OptimizePICCall : public MachineFunctionPass {
public:
  OptimizePICCall() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "Mips OptimizePICCall"; }

  bool runOnMachineFunction(MachineFunction &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  /// Visit MBB.
  bool visitNode(MBBInfo &MBBI);

  /// Test if MI jumps to a function via a register.
  ///
  /// Also, return the virtual register containing the target function's address
  /// and the underlying object in Reg and Val respectively, if the function's
  /// address can be resolved lazily.
  bool isCallViaRegister(MachineInstr &MI, unsigned &Reg,
                         ValueType &Val) const;

  /// Return the number of instructions that dominate the current
  /// instruction and load the function address from object Entry.
  unsigned getCount(ValueType Entry);

  /// Return the destination virtual register of the last instruction
  /// that loads from object Entry.
  unsigned getReg(ValueType Entry);

  /// Update ScopedHT.
  void incCntAndSetReg(ValueType Entry, unsigned Reg);

  ScopedHTType ScopedHT;

  static char ID;
};

} // end of anonymous namespace

char OptimizePICCall::ID = 0;

/// Return the first MachineOperand of MI if it is a used virtual register.
static MachineOperand *getCallTargetRegOpnd(MachineInstr &MI) {
  if (MI.getNumOperands() == 0)
    return nullptr;

  MachineOperand &MO = MI.getOperand(0);

  if (!MO.isReg() || !MO.isUse() || !MO.getReg().isVirtual())
    return nullptr;

  return &MO;
}

/// Return type of register Reg.
static MVT::SimpleValueType getRegTy(unsigned Reg, MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC = MF.getRegInfo().getRegClass(Reg);
  assert(TRI.legalclasstypes_end(*RC) - TRI.legalclasstypes_begin(*RC) == 1);
  return *TRI.legalclasstypes_begin(*RC);
}

/// Do the following transformation:
///
/// jalr $vreg
/// =>
/// copy $t9, $vreg
/// jalr $t9
static void setCallTargetReg(MachineBasicBlock *MBB,
                             MachineBasicBlock::iterator I) {
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  Register SrcReg = I->getOperand(0).getReg();
  unsigned DstReg = getRegTy(SrcReg, MF) == MVT::i32 ? Mips::T9 : Mips::T9_64;
  BuildMI(*MBB, I, I->getDebugLoc(), TII.get(TargetOpcode::COPY), DstReg)
      .addReg(SrcReg);
  I->getOperand(0).setReg(DstReg);
}

/// Search MI's operands for register GP and erase it.
static void eraseGPOpnd(MachineInstr &MI) {
  if (!EraseGPOpnd)
    return;

  MachineFunction &MF = *MI.getParent()->getParent();
  MVT::SimpleValueType Ty = getRegTy(MI.getOperand(0).getReg(), MF);
  unsigned Reg = Ty == MVT::i32 ? Mips::GP : Mips::GP_64;

  for (unsigned I = 0; I < MI.getNumOperands(); ++I) {
    MachineOperand &MO = MI.getOperand(I);
    if (MO.isReg() && MO.getReg() == Reg) {
      MI.removeOperand(I);
      return;
    }
  }

  llvm_unreachable(nullptr);
}

MBBInfo::MBBInfo(MachineDomTreeNode *N) : Node(N), HTScope(nullptr) {}

const MachineDomTreeNode *MBBInfo::getNode() const { return Node; }

bool MBBInfo::isVisited() const { return HTScope; }

void MBBInfo::preVisit(ScopedHTType &ScopedHT) {
  HTScope = new ScopedHTType::ScopeTy(ScopedHT);
}

void MBBInfo::postVisit() {
  delete HTScope;
}

// OptimizePICCall methods.
bool OptimizePICCall::runOnMachineFunction(MachineFunction &F) {
  if (F.getSubtarget<MipsSubtarget>().inMips16Mode())
    return false;

  // Do a pre-order traversal of the dominator tree.
  MachineDominatorTree *MDT =
      &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  bool Changed = false;

  SmallVector<MBBInfo, 8> WorkList(1, MBBInfo(MDT->getRootNode()));

  while (!WorkList.empty()) {
    MBBInfo &MBBI = WorkList.back();

    // If this MBB has already been visited, destroy the scope for the MBB and
    // pop it from the work list.
    if (MBBI.isVisited()) {
      MBBI.postVisit();
      WorkList.pop_back();
      continue;
    }

    // Visit the MBB and add its children to the work list.
    MBBI.preVisit(ScopedHT);
    Changed |= visitNode(MBBI);
    const MachineDomTreeNode *Node = MBBI.getNode();
    WorkList.append(Node->begin(), Node->end());
  }

  return Changed;
}

bool OptimizePICCall::visitNode(MBBInfo &MBBI) {
  bool Changed = false;
  MachineBasicBlock *MBB = MBBI.getNode()->getBlock();

  for (MachineBasicBlock::iterator I = MBB->begin(), E = MBB->end(); I != E;
       ++I) {
    unsigned Reg;
    ValueType Entry;

    // Skip instructions that are not call instructions via registers.
    if (!isCallViaRegister(*I, Reg, Entry))
      continue;

    Changed = true;
    unsigned N = getCount(Entry);

    if (N != 0) {
      // If a function has been called more than twice, we do not have to emit a
      // load instruction to get the function address from the GOT, but can
      // instead reuse the address that has been loaded before.
      if (N >= 2 && !LoadTargetFromGOT)
        getCallTargetRegOpnd(*I)->setReg(getReg(Entry));

      // Erase the $gp operand if this isn't the first time a function has
      // been called. $gp needs to be set up only if the function call can go
      // through a lazy binding stub.
      eraseGPOpnd(*I);
    }

    if (Entry)
      incCntAndSetReg(Entry, Reg);

    setCallTargetReg(MBB, I);
  }

  return Changed;
}

bool OptimizePICCall::isCallViaRegister(MachineInstr &MI, unsigned &Reg,
                                        ValueType &Val) const {
  if (!MI.isCall())
    return false;

  MachineOperand *MO = getCallTargetRegOpnd(MI);

  // Return if MI is not a function call via a register.
  if (!MO)
    return false;

  // Get the instruction that loads the function address from the GOT.
  Reg = MO->getReg();
  Val = nullptr;
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  MachineInstr *DefMI = MRI.getVRegDef(Reg);

  assert(DefMI);

  // See if DefMI is an instruction that loads from a GOT entry that holds the
  // address of a lazy binding stub.
  if (!DefMI->mayLoad() || DefMI->getNumOperands() < 3)
    return true;

  unsigned Flags = DefMI->getOperand(2).getTargetFlags();

  if (Flags != MipsII::MO_GOT_CALL && Flags != MipsII::MO_CALL_LO16)
    return true;

  // Return the underlying object for the GOT entry in Val.
  assert(DefMI->hasOneMemOperand());
  Val = (*DefMI->memoperands_begin())->getValue();
  if (!Val)
    Val = (*DefMI->memoperands_begin())->getPseudoValue();
  return true;
}

unsigned OptimizePICCall::getCount(ValueType Entry) {
  return ScopedHT.lookup(Entry).first;
}

unsigned OptimizePICCall::getReg(ValueType Entry) {
  unsigned Reg = ScopedHT.lookup(Entry).second;
  assert(Reg);
  return Reg;
}

void OptimizePICCall::incCntAndSetReg(ValueType Entry, unsigned Reg) {
  CntRegP P = ScopedHT.lookup(Entry);
  ScopedHT.insert(Entry, std::make_pair(P.first + 1, Reg));
}

/// Return an OptimizeCall object.
FunctionPass *llvm::createMipsOptimizePICCallPass() {
  return new OptimizePICCall();
}
