//===-- MVEVPTBlockPass.cpp - Insert MVE VPT blocks -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Thumb2InstrInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include <cassert>
#include <new>

using namespace llvm;

#define DEBUG_TYPE "arm-mve-vpt"

namespace {
class MVEVPTBlock : public MachineFunctionPass {
public:
  static char ID;
  const Thumb2InstrInfo *TII;
  const TargetRegisterInfo *TRI;

  MVEVPTBlock() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &Fn) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override {
    return "MVE VPT block insertion pass";
  }

private:
  bool InsertVPTBlocks(MachineBasicBlock &MBB);
};

char MVEVPTBlock::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(MVEVPTBlock, DEBUG_TYPE, "ARM MVE VPT block pass", false, false)

static MachineInstr *findVCMPToFoldIntoVPST(MachineBasicBlock::iterator MI,
                                            const TargetRegisterInfo *TRI,
                                            unsigned &NewOpcode) {
  // Search backwards to the instruction that defines VPR. This may or not
  // be a VCMP, we check that after this loop. If we find another instruction
  // that reads cpsr, we return nullptr.
  MachineBasicBlock::iterator CmpMI = MI;
  while (CmpMI != MI->getParent()->begin()) {
    --CmpMI;
    if (CmpMI->modifiesRegister(ARM::VPR, TRI))
      break;
    if (CmpMI->readsRegister(ARM::VPR, TRI))
      break;
  }

  if (CmpMI == MI)
    return nullptr;
  NewOpcode = VCMPOpcodeToVPT(CmpMI->getOpcode());
  if (NewOpcode == 0)
    return nullptr;

  // Search forward from CmpMI to MI, checking if either register was def'd
  if (registerDefinedBetween(CmpMI->getOperand(1).getReg(), std::next(CmpMI),
                             MI, TRI))
    return nullptr;
  if (registerDefinedBetween(CmpMI->getOperand(2).getReg(), std::next(CmpMI),
                             MI, TRI))
    return nullptr;
  return &*CmpMI;
}

// Advances Iter past a block of predicated instructions.
// Returns true if it successfully skipped the whole block of predicated
// instructions. Returns false when it stopped early (due to MaxSteps), or if
// Iter didn't point to a predicated instruction.
static bool StepOverPredicatedInstrs(MachineBasicBlock::instr_iterator &Iter,
                                     MachineBasicBlock::instr_iterator EndIter,
                                     unsigned MaxSteps,
                                     unsigned &NumInstrsSteppedOver) {
  ARMVCC::VPTCodes NextPred = ARMVCC::None;
  Register PredReg;
  NumInstrsSteppedOver = 0;

  while (Iter != EndIter) {
    if (Iter->isDebugInstr()) {
      // Skip debug instructions
      ++Iter;
      continue;
    }

    NextPred = getVPTInstrPredicate(*Iter, PredReg);
    assert(NextPred != ARMVCC::Else &&
           "VPT block pass does not expect Else preds");
    if (NextPred == ARMVCC::None || MaxSteps == 0)
      break;
    --MaxSteps;
    ++Iter;
    ++NumInstrsSteppedOver;
  };

  return NumInstrsSteppedOver != 0 &&
         (NextPred == ARMVCC::None || Iter == EndIter);
}

// Returns true if at least one instruction in the range [Iter, End) defines
// or kills VPR.
static bool IsVPRDefinedOrKilledByBlock(MachineBasicBlock::iterator Iter,
                                        MachineBasicBlock::iterator End) {
  for (; Iter != End; ++Iter)
    if (Iter->definesRegister(ARM::VPR, /*TRI=*/nullptr) ||
        Iter->killsRegister(ARM::VPR, /*TRI=*/nullptr))
      return true;
  return false;
}

// Creates a T, TT, TTT or TTTT BlockMask depending on BlockSize.
static ARM::PredBlockMask GetInitialBlockMask(unsigned BlockSize) {
  switch (BlockSize) {
  case 1:
    return ARM::PredBlockMask::T;
  case 2:
    return ARM::PredBlockMask::TT;
  case 3:
    return ARM::PredBlockMask::TTT;
  case 4:
    return ARM::PredBlockMask::TTTT;
  default:
    llvm_unreachable("Invalid BlockSize!");
  }
}

// Given an iterator (Iter) that points at an instruction with a "Then"
// predicate, tries to create the largest block of continuous predicated
// instructions possible, and returns the VPT Block Mask of that block.
//
// This will try to perform some minor optimization in order to maximize the
// size of the block.
static ARM::PredBlockMask
CreateVPTBlock(MachineBasicBlock::instr_iterator &Iter,
               MachineBasicBlock::instr_iterator EndIter,
               SmallVectorImpl<MachineInstr *> &DeadInstructions) {
  MachineBasicBlock::instr_iterator BlockBeg = Iter;
  (void)BlockBeg;
  assert(getVPTInstrPredicate(*Iter) == ARMVCC::Then &&
         "Expected a Predicated Instruction");

  LLVM_DEBUG(dbgs() << "VPT block created for: "; Iter->dump());

  unsigned BlockSize;
  StepOverPredicatedInstrs(Iter, EndIter, 4, BlockSize);

  LLVM_DEBUG(for (MachineBasicBlock::instr_iterator AddedInstIter =
                      std::next(BlockBeg);
                  AddedInstIter != Iter; ++AddedInstIter) {
    if (AddedInstIter->isDebugInstr())
      continue;
    dbgs() << "  adding: ";
    AddedInstIter->dump();
  });

  // Generate the initial BlockMask
  ARM::PredBlockMask BlockMask = GetInitialBlockMask(BlockSize);

  // Remove VPNOTs while there's still room in the block, so we can make the
  // largest block possible.
  ARMVCC::VPTCodes CurrentPredicate = ARMVCC::Else;
  while (BlockSize < 4 && Iter != EndIter &&
         Iter->getOpcode() == ARM::MVE_VPNOT) {

    // Try to skip all of the predicated instructions after the VPNOT, stopping
    // after (4 - BlockSize). If we can't skip them all, stop.
    unsigned ElseInstCnt = 0;
    MachineBasicBlock::instr_iterator VPNOTBlockEndIter = std::next(Iter);
    if (!StepOverPredicatedInstrs(VPNOTBlockEndIter, EndIter, (4 - BlockSize),
                                  ElseInstCnt))
      break;

    // Check if this VPNOT can be removed or not: It can only be removed if at
    // least one of the predicated instruction that follows it kills or sets
    // VPR.
    if (!IsVPRDefinedOrKilledByBlock(Iter, VPNOTBlockEndIter))
      break;

    LLVM_DEBUG(dbgs() << "  removing VPNOT: "; Iter->dump());

    // Record the new size of the block
    BlockSize += ElseInstCnt;
    assert(BlockSize <= 4 && "Block is too large!");

    // Record the VPNot to remove it later.
    DeadInstructions.push_back(&*Iter);
    ++Iter;

    // Replace the predicates of the instructions we're adding.
    // Note that we are using "Iter" to iterate over the block so we can update
    // it at the same time.
    for (; Iter != VPNOTBlockEndIter; ++Iter) {
      if (Iter->isDebugInstr())
        continue;

      // Find the register in which the predicate is
      int OpIdx = findFirstVPTPredOperandIdx(*Iter);
      assert(OpIdx != -1);

      // Change the predicate and update the mask
      Iter->getOperand(OpIdx).setImm(CurrentPredicate);
      BlockMask = expandPredBlockMask(BlockMask, CurrentPredicate);

      LLVM_DEBUG(dbgs() << "  adding : "; Iter->dump());
    }

    CurrentPredicate =
        (CurrentPredicate == ARMVCC::Then ? ARMVCC::Else : ARMVCC::Then);
  }
  return BlockMask;
}

bool MVEVPTBlock::InsertVPTBlocks(MachineBasicBlock &Block) {
  bool Modified = false;
  MachineBasicBlock::instr_iterator MBIter = Block.instr_begin();
  MachineBasicBlock::instr_iterator EndIter = Block.instr_end();

  SmallVector<MachineInstr *, 4> DeadInstructions;

  while (MBIter != EndIter) {
    MachineInstr *MI = &*MBIter;
    Register PredReg;
    DebugLoc DL = MI->getDebugLoc();

    ARMVCC::VPTCodes Pred = getVPTInstrPredicate(*MI, PredReg);

    // The idea of the predicate is that None, Then and Else are for use when
    // handling assembly language: they correspond to the three possible
    // suffixes "", "t" and "e" on the mnemonic. So when instructions are read
    // from assembly source or disassembled from object code, you expect to
    // see a mixture whenever there's a long VPT block. But in code
    // generation, we hope we'll never generate an Else as input to this pass.
    assert(Pred != ARMVCC::Else && "VPT block pass does not expect Else preds");

    if (Pred == ARMVCC::None) {
      ++MBIter;
      continue;
    }

    ARM::PredBlockMask BlockMask =
        CreateVPTBlock(MBIter, EndIter, DeadInstructions);

    // Search back for a VCMP that can be folded to create a VPT, or else
    // create a VPST directly
    MachineInstrBuilder MIBuilder;
    unsigned NewOpcode;
    LLVM_DEBUG(dbgs() << "  final block mask: " << (unsigned)BlockMask << "\n");
    if (MachineInstr *VCMP = findVCMPToFoldIntoVPST(MI, TRI, NewOpcode)) {
      LLVM_DEBUG(dbgs() << "  folding VCMP into VPST: "; VCMP->dump());
      MIBuilder = BuildMI(Block, MI, DL, TII->get(NewOpcode));
      MIBuilder.addImm((uint64_t)BlockMask);
      MIBuilder.add(VCMP->getOperand(1));
      MIBuilder.add(VCMP->getOperand(2));
      MIBuilder.add(VCMP->getOperand(3));

      // We need to remove any kill flags between the original VCMP and the new
      // insertion point.
      for (MachineInstr &MII :
           make_range(VCMP->getIterator(), MI->getIterator())) {
        MII.clearRegisterKills(VCMP->getOperand(1).getReg(), TRI);
        MII.clearRegisterKills(VCMP->getOperand(2).getReg(), TRI);
      }

      VCMP->eraseFromParent();
    } else {
      MIBuilder = BuildMI(Block, MI, DL, TII->get(ARM::MVE_VPST));
      MIBuilder.addImm((uint64_t)BlockMask);
    }

    // Erase all dead instructions (VPNOT's). Do that now so that they do not
    // mess with the bundle creation.
    for (MachineInstr *DeadMI : DeadInstructions)
      DeadMI->eraseFromParent();
    DeadInstructions.clear();

    finalizeBundle(
        Block, MachineBasicBlock::instr_iterator(MIBuilder.getInstr()), MBIter);

    Modified = true;
  }

  return Modified;
}

bool MVEVPTBlock::runOnMachineFunction(MachineFunction &Fn) {
  const ARMSubtarget &STI = Fn.getSubtarget<ARMSubtarget>();

  if (!STI.isThumb2() || !STI.hasMVEIntegerOps())
    return false;

  TII = static_cast<const Thumb2InstrInfo *>(STI.getInstrInfo());
  TRI = STI.getRegisterInfo();

  LLVM_DEBUG(dbgs() << "********** ARM MVE VPT BLOCKS **********\n"
                    << "********** Function: " << Fn.getName() << '\n');

  bool Modified = false;
  for (MachineBasicBlock &MBB : Fn)
    Modified |= InsertVPTBlocks(MBB);

  LLVM_DEBUG(dbgs() << "**************************************\n");
  return Modified;
}

/// createMVEVPTBlock - Returns an instance of the MVE VPT block
/// insertion pass.
FunctionPass *llvm::createMVEVPTBlockPass() { return new MVEVPTBlock(); }
