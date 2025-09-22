//===--------- HexagonCopyHoisting.cpp - Hexagon Copy Hoisting  ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// The purpose of this pass is to move the copy instructions that are
// present in all the successor of a basic block (BB) to the end of BB.
//===----------------------------------------------------------------------===//

#include "HexagonTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "CopyHoist"

using namespace llvm;

static cl::opt<std::string> CPHoistFn("cphoistfn", cl::Hidden, cl::desc(""),
                                      cl::init(""));

namespace llvm {
void initializeHexagonCopyHoistingPass(PassRegistry &Registry);
FunctionPass *createHexagonCopyHoisting();
} // namespace llvm

namespace {

class HexagonCopyHoisting : public MachineFunctionPass {

public:
  static char ID;
  HexagonCopyHoisting() : MachineFunctionPass(ID), MFN(nullptr), MRI(nullptr) {
    initializeHexagonCopyHoistingPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "Hexagon Copy Hoisting"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SlotIndexesWrapperPass>();
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;
  void collectCopyInst();
  void addMItoCopyList(MachineInstr *MI);
  bool analyzeCopy(MachineBasicBlock *BB);
  bool isSafetoMove(MachineInstr *CandMI);
  void moveCopyInstr(MachineBasicBlock *DestBB,
                     std::pair<Register, Register> Key, MachineInstr *MI);

  MachineFunction *MFN;
  MachineRegisterInfo *MRI;
  std::vector<DenseMap<std::pair<Register, Register>, MachineInstr *>>
      CopyMIList;
};

} // namespace

char HexagonCopyHoisting::ID = 0;

namespace llvm {
char &HexagonCopyHoistingID = HexagonCopyHoisting::ID;
} // namespace llvm

bool HexagonCopyHoisting::runOnMachineFunction(MachineFunction &Fn) {

  if ((CPHoistFn != "") && (CPHoistFn != Fn.getFunction().getName()))
    return false;

  MFN = &Fn;
  MRI = &Fn.getRegInfo();

  LLVM_DEBUG(dbgs() << "\nCopy Hoisting:" << "\'" << Fn.getName() << "\'\n");

  CopyMIList.clear();
  CopyMIList.resize(Fn.getNumBlockIDs());

  // Traverse through all basic blocks and collect copy instructions.
  collectCopyInst();

  // Traverse through the basic blocks again and move the COPY instructions
  // that are present in all the successors of BB to BB.
  bool Changed = false;
  for (MachineBasicBlock *BB : post_order(&Fn)) {
    if (!BB->empty()) {
      if (BB->pred_size() != 1)
        continue;
      auto &BBCopyInst = CopyMIList[BB->getNumber()];
      if (BBCopyInst.size() > 0)
        Changed |= analyzeCopy(*BB->pred_begin());
    }
  }
  // Re-compute liveness
  if (Changed) {
    LiveIntervals &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();
    SlotIndexes *SI = LIS.getSlotIndexes();
    SI->reanalyze(Fn);
    LIS.reanalyze(Fn);
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// Save all COPY instructions for each basic block in CopyMIList vector.
//===----------------------------------------------------------------------===//
void HexagonCopyHoisting::collectCopyInst() {
  for (MachineBasicBlock &BB : *MFN) {
#ifndef NDEBUG
    auto &BBCopyInst = CopyMIList[BB.getNumber()];
    LLVM_DEBUG(dbgs() << "Visiting BB#" << BB.getNumber() << ":\n");
#endif

    for (MachineInstr &MI : BB) {
      if (MI.getOpcode() == TargetOpcode::COPY)
        addMItoCopyList(&MI);
    }
    LLVM_DEBUG(dbgs() << "\tNumber of copies: " << BBCopyInst.size() << "\n");
  }
}

void HexagonCopyHoisting::addMItoCopyList(MachineInstr *MI) {
  unsigned BBNum = MI->getParent()->getNumber();
  auto &BBCopyInst = CopyMIList[BBNum];
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  if (!Register::isVirtualRegister(DstReg) ||
      !Register::isVirtualRegister(SrcReg) ||
      MRI->getRegClass(DstReg) != &Hexagon::IntRegsRegClass ||
      MRI->getRegClass(SrcReg) != &Hexagon::IntRegsRegClass)
    return;

  BBCopyInst.insert(std::pair(std::pair(SrcReg, DstReg), MI));
#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "\tAdding Copy Instr to the list: " << MI << "\n");
  for (auto II : BBCopyInst) {
    MachineInstr *TempMI = II.getSecond();
    LLVM_DEBUG(dbgs() << "\tIn the list: " << TempMI << "\n");
  }
#endif
}

//===----------------------------------------------------------------------===//
// Look at the COPY instructions of all the successors of BB. If the same
// instruction is present in every successor and can be safely moved,
// pull it into BB.
//===----------------------------------------------------------------------===//
bool HexagonCopyHoisting::analyzeCopy(MachineBasicBlock *BB) {

  bool Changed = false;
  if (BB->succ_size() < 2)
    return false;

  for (MachineBasicBlock *SB : BB->successors()) {
    if (SB->pred_size() != 1 || SB->isEHPad() || SB->hasAddressTaken())
      return false;
  }

  MachineBasicBlock *SBB1 = *BB->succ_begin();
  auto &BBCopyInst1 = CopyMIList[SBB1->getNumber()];

  for (auto II : BBCopyInst1) {
    std::pair<Register, Register> Key = II.getFirst();
    MachineInstr *MI = II.getSecond();
    bool IsSafetoMove = true;
    for (MachineBasicBlock *SuccBB : BB->successors()) {
      auto &SuccBBCopyInst = CopyMIList[SuccBB->getNumber()];
      if (!SuccBBCopyInst.count(Key)) {
        // Same copy not present in this successor
        IsSafetoMove = false;
        break;
      }
      // If present, make sure that it's safe to pull this copy instruction
      // into the predecessor.
      MachineInstr *SuccMI = SuccBBCopyInst[Key];
      if (!isSafetoMove(SuccMI)) {
        IsSafetoMove = false;
        break;
      }
    }
    // If we have come this far, this copy instruction can be safely
    // moved to the predecessor basic block.
    if (IsSafetoMove) {
      LLVM_DEBUG(dbgs() << "\t\t Moving instr to BB#" << BB->getNumber() << ": "
                        << MI << "\n");
      moveCopyInstr(BB, Key, MI);
      // Add my into BB copyMI list.
      Changed = true;
    }
  }

#ifndef NDEBUG
  auto &BBCopyInst = CopyMIList[BB->getNumber()];
  for (auto II : BBCopyInst) {
    MachineInstr *TempMI = II.getSecond();
    LLVM_DEBUG(dbgs() << "\tIn the list: " << TempMI << "\n");
  }
#endif
  return Changed;
}

bool HexagonCopyHoisting::isSafetoMove(MachineInstr *CandMI) {
  // Make sure that it's safe to move this 'copy' instruction to the predecessor
  // basic block.
  assert(CandMI->getOperand(0).isReg() && CandMI->getOperand(1).isReg());
  Register DefR = CandMI->getOperand(0).getReg();
  Register UseR = CandMI->getOperand(1).getReg();

  MachineBasicBlock *BB = CandMI->getParent();
  // There should not be a def/use of DefR between the start of BB and CandMI.
  MachineBasicBlock::iterator MII, MIE;
  for (MII = BB->begin(), MIE = CandMI; MII != MIE; ++MII) {
    MachineInstr *OtherMI = &*MII;
    for (const MachineOperand &Mo : OtherMI->operands())
      if (Mo.isReg() && Mo.getReg() == DefR)
        return false;
  }
  // There should not be a def of UseR between the start of BB and CandMI.
  for (MII = BB->begin(), MIE = CandMI; MII != MIE; ++MII) {
    MachineInstr *OtherMI = &*MII;
    for (const MachineOperand &Mo : OtherMI->operands())
      if (Mo.isReg() && Mo.isDef() && Mo.getReg() == UseR)
        return false;
  }
  return true;
}

void HexagonCopyHoisting::moveCopyInstr(MachineBasicBlock *DestBB,
                                        std::pair<Register, Register> Key,
                                        MachineInstr *MI) {
  MachineBasicBlock::iterator FirstTI = DestBB->getFirstTerminator();
  assert(FirstTI != DestBB->end());

  DestBB->splice(FirstTI, MI->getParent(), MI);

  addMItoCopyList(MI);
  for (auto I = ++(DestBB->succ_begin()), E = DestBB->succ_end(); I != E; ++I) {
    MachineBasicBlock *SuccBB = *I;
    auto &BBCopyInst = CopyMIList[SuccBB->getNumber()];
    MachineInstr *SuccMI = BBCopyInst[Key];
    SuccMI->eraseFromParent();
    BBCopyInst.erase(Key);
  }
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

INITIALIZE_PASS(HexagonCopyHoisting, "hexagon-move-phicopy",
                "Hexagon move phi copy", false, false)

FunctionPass *llvm::createHexagonCopyHoisting() {
  return new HexagonCopyHoisting();
}
