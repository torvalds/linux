//===- ARMSLSHardening.cpp - Harden Straight Line Missspeculation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass to insert code to mitigate against side channel
// vulnerabilities that may happen under straight line miss-speculation.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMInstrInfo.h"
#include "ARMSubtarget.h"
#include "llvm/CodeGen/IndirectThunks.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/DebugLoc.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "arm-sls-hardening"

#define ARM_SLS_HARDENING_NAME "ARM sls hardening pass"

namespace {

class ARMSLSHardening : public MachineFunctionPass {
public:
  const TargetInstrInfo *TII;
  const ARMSubtarget *ST;

  static char ID;

  ARMSLSHardening() : MachineFunctionPass(ID) {
    initializeARMSLSHardeningPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override { return ARM_SLS_HARDENING_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool hardenReturnsAndBRs(MachineBasicBlock &MBB) const;
  bool hardenIndirectCalls(MachineBasicBlock &MBB) const;
  MachineBasicBlock &
  ConvertIndirectCallToIndirectJump(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator) const;
};

} // end anonymous namespace

char ARMSLSHardening::ID = 0;

INITIALIZE_PASS(ARMSLSHardening, "arm-sls-hardening",
                ARM_SLS_HARDENING_NAME, false, false)

static void insertSpeculationBarrier(const ARMSubtarget *ST,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     DebugLoc DL,
                                     bool AlwaysUseISBDSB = false) {
  assert(MBBI != MBB.begin() &&
         "Must not insert SpeculationBarrierEndBB as only instruction in MBB.");
  assert(std::prev(MBBI)->isBarrier() &&
         "SpeculationBarrierEndBB must only follow unconditional control flow "
         "instructions.");
  assert(std::prev(MBBI)->isTerminator() &&
         "SpeculationBarrierEndBB must only follow terminators.");
  const TargetInstrInfo *TII = ST->getInstrInfo();
  assert(ST->hasDataBarrier() || ST->hasSB());
  bool ProduceSB = ST->hasSB() && !AlwaysUseISBDSB;
  unsigned BarrierOpc =
      ProduceSB ? (ST->isThumb() ? ARM::t2SpeculationBarrierSBEndBB
                                 : ARM::SpeculationBarrierSBEndBB)
                : (ST->isThumb() ? ARM::t2SpeculationBarrierISBDSBEndBB
                                 : ARM::SpeculationBarrierISBDSBEndBB);
  if (MBBI == MBB.end() || !isSpeculationBarrierEndBBOpcode(MBBI->getOpcode()))
    BuildMI(MBB, MBBI, DL, TII->get(BarrierOpc));
}

bool ARMSLSHardening::runOnMachineFunction(MachineFunction &MF) {
  ST = &MF.getSubtarget<ARMSubtarget>();
  TII = MF.getSubtarget().getInstrInfo();

  bool Modified = false;
  for (auto &MBB : MF) {
    Modified |= hardenReturnsAndBRs(MBB);
    Modified |= hardenIndirectCalls(MBB);
  }

  return Modified;
}

bool ARMSLSHardening::hardenReturnsAndBRs(MachineBasicBlock &MBB) const {
  if (!ST->hardenSlsRetBr())
    return false;
  assert(!ST->isThumb1Only());
  bool Modified = false;
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator(), E = MBB.end();
  MachineBasicBlock::iterator NextMBBI;
  for (; MBBI != E; MBBI = NextMBBI) {
    MachineInstr &MI = *MBBI;
    NextMBBI = std::next(MBBI);
    if (isIndirectControlFlowNotComingBack(MI)) {
      assert(MI.isTerminator());
      assert(!TII->isPredicated(MI));
      insertSpeculationBarrier(ST, MBB, std::next(MBBI), MI.getDebugLoc());
      Modified = true;
    }
  }
  return Modified;
}

static const char SLSBLRNamePrefix[] = "__llvm_slsblr_thunk_";

static const struct ThunkNameRegMode {
  const char* Name;
  Register Reg;
  bool isThumb;
} SLSBLRThunks[] = {
    {"__llvm_slsblr_thunk_arm_r0", ARM::R0, false},
    {"__llvm_slsblr_thunk_arm_r1", ARM::R1, false},
    {"__llvm_slsblr_thunk_arm_r2", ARM::R2, false},
    {"__llvm_slsblr_thunk_arm_r3", ARM::R3, false},
    {"__llvm_slsblr_thunk_arm_r4", ARM::R4, false},
    {"__llvm_slsblr_thunk_arm_r5", ARM::R5, false},
    {"__llvm_slsblr_thunk_arm_r6", ARM::R6, false},
    {"__llvm_slsblr_thunk_arm_r7", ARM::R7, false},
    {"__llvm_slsblr_thunk_arm_r8", ARM::R8, false},
    {"__llvm_slsblr_thunk_arm_r9", ARM::R9, false},
    {"__llvm_slsblr_thunk_arm_r10", ARM::R10, false},
    {"__llvm_slsblr_thunk_arm_r11", ARM::R11, false},
    {"__llvm_slsblr_thunk_arm_sp", ARM::SP, false},
    {"__llvm_slsblr_thunk_arm_pc", ARM::PC, false},
    {"__llvm_slsblr_thunk_thumb_r0", ARM::R0, true},
    {"__llvm_slsblr_thunk_thumb_r1", ARM::R1, true},
    {"__llvm_slsblr_thunk_thumb_r2", ARM::R2, true},
    {"__llvm_slsblr_thunk_thumb_r3", ARM::R3, true},
    {"__llvm_slsblr_thunk_thumb_r4", ARM::R4, true},
    {"__llvm_slsblr_thunk_thumb_r5", ARM::R5, true},
    {"__llvm_slsblr_thunk_thumb_r6", ARM::R6, true},
    {"__llvm_slsblr_thunk_thumb_r7", ARM::R7, true},
    {"__llvm_slsblr_thunk_thumb_r8", ARM::R8, true},
    {"__llvm_slsblr_thunk_thumb_r9", ARM::R9, true},
    {"__llvm_slsblr_thunk_thumb_r10", ARM::R10, true},
    {"__llvm_slsblr_thunk_thumb_r11", ARM::R11, true},
    {"__llvm_slsblr_thunk_thumb_sp", ARM::SP, true},
    {"__llvm_slsblr_thunk_thumb_pc", ARM::PC, true},
};

// An enum for tracking whether Arm and Thumb thunks have been inserted into the
// current module so far.
enum ArmInsertedThunks { NoThunk = 0, ArmThunk = 1, ThumbThunk = 2 };

inline ArmInsertedThunks &operator|=(ArmInsertedThunks &X,
                                     ArmInsertedThunks Y) {
  return X = static_cast<ArmInsertedThunks>(X | Y);
}

namespace {
struct SLSBLRThunkInserter
    : ThunkInserter<SLSBLRThunkInserter, ArmInsertedThunks> {
  const char *getThunkPrefix() { return SLSBLRNamePrefix; }
  bool mayUseThunk(const MachineFunction &MF) {
    ComdatThunks &= !MF.getSubtarget<ARMSubtarget>().hardenSlsNoComdat();
    return MF.getSubtarget<ARMSubtarget>().hardenSlsBlr();
  }
  ArmInsertedThunks insertThunks(MachineModuleInfo &MMI, MachineFunction &MF,
                                 ArmInsertedThunks InsertedThunks);
  void populateThunk(MachineFunction &MF);

private:
  bool ComdatThunks = true;
};
} // namespace

ArmInsertedThunks
SLSBLRThunkInserter::insertThunks(MachineModuleInfo &MMI, MachineFunction &MF,
                                  ArmInsertedThunks InsertedThunks) {
  if ((InsertedThunks & ArmThunk &&
       !MF.getSubtarget<ARMSubtarget>().isThumb()) ||
      (InsertedThunks & ThumbThunk &&
       MF.getSubtarget<ARMSubtarget>().isThumb()))
    return NoThunk;
  // FIXME: It probably would be possible to filter which thunks to produce
  // based on which registers are actually used in indirect calls in this
  // function. But would that be a worthwhile optimization?
  const ARMSubtarget *ST = &MF.getSubtarget<ARMSubtarget>();
  for (auto T : SLSBLRThunks)
    if (ST->isThumb() == T.isThumb)
      createThunkFunction(MMI, T.Name, ComdatThunks,
                          T.isThumb ? "+thumb-mode" : "");
  return ST->isThumb() ? ThumbThunk : ArmThunk;
}

void SLSBLRThunkInserter::populateThunk(MachineFunction &MF) {
  assert(MF.getFunction().hasComdat() == ComdatThunks &&
         "ComdatThunks value changed since MF creation");
  // FIXME: How to better communicate Register number, rather than through
  // name and lookup table?
  assert(MF.getName().starts_with(getThunkPrefix()));
  auto ThunkIt = llvm::find_if(
      SLSBLRThunks, [&MF](auto T) { return T.Name == MF.getName(); });
  assert(ThunkIt != std::end(SLSBLRThunks));
  Register ThunkReg = ThunkIt->Reg;
  bool isThumb = ThunkIt->isThumb;

  const TargetInstrInfo *TII = MF.getSubtarget<ARMSubtarget>().getInstrInfo();
  MachineBasicBlock *Entry = &MF.front();
  Entry->clear();

  //  These thunks need to consist of the following instructions:
  //  __llvm_slsblr_thunk_(arm/thumb)_rN:
  //      bx  rN
  //      barrierInsts
  Entry->addLiveIn(ThunkReg);
  if (isThumb)
    BuildMI(Entry, DebugLoc(), TII->get(ARM::tBX))
        .addReg(ThunkReg)
        .add(predOps(ARMCC::AL));
  else
    BuildMI(Entry, DebugLoc(), TII->get(ARM::BX))
        .addReg(ThunkReg);

  // Make sure the thunks do not make use of the SB extension in case there is
  // a function somewhere that will call to it that for some reason disabled
  // the SB extension locally on that function, even though it's enabled for
  // the module otherwise. Therefore set AlwaysUseISBSDB to true.
  insertSpeculationBarrier(&MF.getSubtarget<ARMSubtarget>(), *Entry,
                           Entry->end(), DebugLoc(), true /*AlwaysUseISBDSB*/);
}

MachineBasicBlock &ARMSLSHardening::ConvertIndirectCallToIndirectJump(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI) const {
  // Transform an indirect call to an indirect jump as follows:
  // Before:
  //   |-----------------------------|
  //   |      ...                    |
  //   |  instI                      |
  //   |  BLX rN                     |
  //   |  instJ                      |
  //   |      ...                    |
  //   |-----------------------------|
  //
  // After:
  //   |----------   -------------------------|
  //   |      ...                             |
  //   |  instI                               |
  //   |  *call* __llvm_slsblr_thunk_mode_xN  |
  //   |  instJ                               |
  //   |      ...                             |
  //   |--------------------------------------|
  //
  //   __llvm_slsblr_thunk_mode_xN:
  //   |-----------------------------|
  //   |  BX rN                      |
  //   |  barrierInsts               |
  //   |-----------------------------|
  //
  // The __llvm_slsblr_thunk_mode_xN thunks are created by the
  // SLSBLRThunkInserter.
  // This function merely needs to transform an indirect call to a direct call
  // to __llvm_slsblr_thunk_xN.
  MachineInstr &IndirectCall = *MBBI;
  assert(isIndirectCall(IndirectCall) && !IndirectCall.isReturn());
  int RegOpIdxOnIndirectCall = -1;
  bool isThumb;
  switch (IndirectCall.getOpcode()) {
  case ARM::BLX:   // !isThumb2
  case ARM::BLX_noip:   // !isThumb2
    isThumb = false;
    RegOpIdxOnIndirectCall = 0;
    break;
  case ARM::tBLXr:      // isThumb2
  case ARM::tBLXr_noip: // isThumb2
    isThumb = true;
    RegOpIdxOnIndirectCall = 2;
    break;
  default:
    llvm_unreachable("unhandled Indirect Call");
  }

  Register Reg = IndirectCall.getOperand(RegOpIdxOnIndirectCall).getReg();
  // Since linkers are allowed to clobber R12 on function calls, the above
  // mitigation only works if the original indirect call instruction was not
  // using R12. Code generation before must make sure that no indirect call
  // using R12 was produced if the mitigation is enabled.
  // Also, the transformation is incorrect if the indirect call uses LR, so
  // also have to avoid that.
  assert(Reg != ARM::R12 && Reg != ARM::LR);
  bool RegIsKilled = IndirectCall.getOperand(RegOpIdxOnIndirectCall).isKill();

  DebugLoc DL = IndirectCall.getDebugLoc();

  MachineFunction &MF = *MBBI->getMF();
  auto ThunkIt = llvm::find_if(SLSBLRThunks, [Reg, isThumb](auto T) {
    return T.Reg == Reg && T.isThumb == isThumb;
  });
  assert(ThunkIt != std::end(SLSBLRThunks));
  Module *M = MF.getFunction().getParent();
  const GlobalValue *GV = cast<GlobalValue>(M->getNamedValue(ThunkIt->Name));

  MachineInstr *BL =
      isThumb ? BuildMI(MBB, MBBI, DL, TII->get(ARM::tBL))
                    .addImm(IndirectCall.getOperand(0).getImm())
                    .addReg(IndirectCall.getOperand(1).getReg())
                    .addGlobalAddress(GV)
              : BuildMI(MBB, MBBI, DL, TII->get(ARM::BL)).addGlobalAddress(GV);

  // Now copy the implicit operands from IndirectCall to BL and copy other
  // necessary info.
  // However, both IndirectCall and BL instructions implictly use SP and
  // implicitly define LR. Blindly copying implicit operands would result in SP
  // and LR operands to be present multiple times. While this may not be too
  // much of an issue, let's avoid that for cleanliness, by removing those
  // implicit operands from the BL created above before we copy over all
  // implicit operands from the IndirectCall.
  int ImpLROpIdx = -1;
  int ImpSPOpIdx = -1;
  for (unsigned OpIdx = BL->getNumExplicitOperands();
       OpIdx < BL->getNumOperands(); OpIdx++) {
    MachineOperand Op = BL->getOperand(OpIdx);
    if (!Op.isReg())
      continue;
    if (Op.getReg() == ARM::LR && Op.isDef())
      ImpLROpIdx = OpIdx;
    if (Op.getReg() == ARM::SP && !Op.isDef())
      ImpSPOpIdx = OpIdx;
  }
  assert(ImpLROpIdx != -1);
  assert(ImpSPOpIdx != -1);
  int FirstOpIdxToRemove = std::max(ImpLROpIdx, ImpSPOpIdx);
  int SecondOpIdxToRemove = std::min(ImpLROpIdx, ImpSPOpIdx);
  BL->removeOperand(FirstOpIdxToRemove);
  BL->removeOperand(SecondOpIdxToRemove);
  // Now copy over the implicit operands from the original IndirectCall
  BL->copyImplicitOps(MF, IndirectCall);
  MF.moveCallSiteInfo(&IndirectCall, BL);
  // Also add the register called in the IndirectCall as being used in the
  // called thunk.
  BL->addOperand(MachineOperand::CreateReg(Reg, false /*isDef*/, true /*isImp*/,
                                           RegIsKilled /*isKill*/));
  // Remove IndirectCallinstruction
  MBB.erase(MBBI);
  return MBB;
}

bool ARMSLSHardening::hardenIndirectCalls(MachineBasicBlock &MBB) const {
  if (!ST->hardenSlsBlr())
    return false;
  bool Modified = false;
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  MachineBasicBlock::iterator NextMBBI;
  for (; MBBI != E; MBBI = NextMBBI) {
    MachineInstr &MI = *MBBI;
    NextMBBI = std::next(MBBI);
    // Tail calls are both indirect calls and "returns".
    // They are also indirect jumps, so should be handled by sls-harden-retbr,
    // rather than sls-harden-blr.
    if (isIndirectCall(MI) && !MI.isReturn()) {
      ConvertIndirectCallToIndirectJump(MBB, MBBI);
      Modified = true;
    }
  }
  return Modified;
}



FunctionPass *llvm::createARMSLSHardeningPass() {
  return new ARMSLSHardening();
}

namespace {
class ARMIndirectThunks : public ThunkInserterPass<SLSBLRThunkInserter> {
public:
  static char ID;

  ARMIndirectThunks() : ThunkInserterPass(ID) {}

  StringRef getPassName() const override { return "ARM Indirect Thunks"; }
};
} // end anonymous namespace

char ARMIndirectThunks::ID = 0;

FunctionPass *llvm::createARMIndirectThunks() {
  return new ARMIndirectThunks();
}
