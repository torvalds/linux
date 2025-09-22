//===-- ARMBaseInstrInfo.cpp - ARM Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Base ARM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMConstantPoolValue.h"
#include "ARMFeatures.h"
#include "ARMHazardRecognizer.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "MVETailPredUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachinePipeliner.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/MultiHazardRecognizer.h"
#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <new>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "arm-instrinfo"

#define GET_INSTRINFO_CTOR_DTOR
#include "ARMGenInstrInfo.inc"

static cl::opt<bool>
EnableARM3Addr("enable-arm-3-addr-conv", cl::Hidden,
               cl::desc("Enable ARM 2-addr to 3-addr conv"));

/// ARM_MLxEntry - Record information about MLA / MLS instructions.
struct ARM_MLxEntry {
  uint16_t MLxOpc;     // MLA / MLS opcode
  uint16_t MulOpc;     // Expanded multiplication opcode
  uint16_t AddSubOpc;  // Expanded add / sub opcode
  bool NegAcc;         // True if the acc is negated before the add / sub.
  bool HasLane;        // True if instruction has an extra "lane" operand.
};

static const ARM_MLxEntry ARM_MLxTable[] = {
  // MLxOpc,          MulOpc,           AddSubOpc,       NegAcc, HasLane
  // fp scalar ops
  { ARM::VMLAS,       ARM::VMULS,       ARM::VADDS,      false,  false },
  { ARM::VMLSS,       ARM::VMULS,       ARM::VSUBS,      false,  false },
  { ARM::VMLAD,       ARM::VMULD,       ARM::VADDD,      false,  false },
  { ARM::VMLSD,       ARM::VMULD,       ARM::VSUBD,      false,  false },
  { ARM::VNMLAS,      ARM::VNMULS,      ARM::VSUBS,      true,   false },
  { ARM::VNMLSS,      ARM::VMULS,       ARM::VSUBS,      true,   false },
  { ARM::VNMLAD,      ARM::VNMULD,      ARM::VSUBD,      true,   false },
  { ARM::VNMLSD,      ARM::VMULD,       ARM::VSUBD,      true,   false },

  // fp SIMD ops
  { ARM::VMLAfd,      ARM::VMULfd,      ARM::VADDfd,     false,  false },
  { ARM::VMLSfd,      ARM::VMULfd,      ARM::VSUBfd,     false,  false },
  { ARM::VMLAfq,      ARM::VMULfq,      ARM::VADDfq,     false,  false },
  { ARM::VMLSfq,      ARM::VMULfq,      ARM::VSUBfq,     false,  false },
  { ARM::VMLAslfd,    ARM::VMULslfd,    ARM::VADDfd,     false,  true  },
  { ARM::VMLSslfd,    ARM::VMULslfd,    ARM::VSUBfd,     false,  true  },
  { ARM::VMLAslfq,    ARM::VMULslfq,    ARM::VADDfq,     false,  true  },
  { ARM::VMLSslfq,    ARM::VMULslfq,    ARM::VSUBfq,     false,  true  },
};

ARMBaseInstrInfo::ARMBaseInstrInfo(const ARMSubtarget& STI)
  : ARMGenInstrInfo(ARM::ADJCALLSTACKDOWN, ARM::ADJCALLSTACKUP),
    Subtarget(STI) {
  for (unsigned i = 0, e = std::size(ARM_MLxTable); i != e; ++i) {
    if (!MLxEntryMap.insert(std::make_pair(ARM_MLxTable[i].MLxOpc, i)).second)
      llvm_unreachable("Duplicated entries?");
    MLxHazardOpcodes.insert(ARM_MLxTable[i].AddSubOpc);
    MLxHazardOpcodes.insert(ARM_MLxTable[i].MulOpc);
  }
}

// Use a ScoreboardHazardRecognizer for prepass ARM scheduling. TargetInstrImpl
// currently defaults to no prepass hazard recognizer.
ScheduleHazardRecognizer *
ARMBaseInstrInfo::CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                                               const ScheduleDAG *DAG) const {
  if (usePreRAHazardRecognizer()) {
    const InstrItineraryData *II =
        static_cast<const ARMSubtarget *>(STI)->getInstrItineraryData();
    return new ScoreboardHazardRecognizer(II, DAG, "pre-RA-sched");
  }
  return TargetInstrInfo::CreateTargetHazardRecognizer(STI, DAG);
}

// Called during:
// - pre-RA scheduling
// - post-RA scheduling when FeatureUseMISched is set
ScheduleHazardRecognizer *ARMBaseInstrInfo::CreateTargetMIHazardRecognizer(
    const InstrItineraryData *II, const ScheduleDAGMI *DAG) const {
  MultiHazardRecognizer *MHR = new MultiHazardRecognizer();

  // We would like to restrict this hazard recognizer to only
  // post-RA scheduling; we can tell that we're post-RA because we don't
  // track VRegLiveness.
  // Cortex-M7: TRM indicates that there is a single ITCM bank and two DTCM
  //            banks banked on bit 2.  Assume that TCMs are in use.
  if (Subtarget.isCortexM7() && !DAG->hasVRegLiveness())
    MHR->AddHazardRecognizer(
        std::make_unique<ARMBankConflictHazardRecognizer>(DAG, 0x4, true));

  // Not inserting ARMHazardRecognizerFPMLx because that would change
  // legacy behavior

  auto BHR = TargetInstrInfo::CreateTargetMIHazardRecognizer(II, DAG);
  MHR->AddHazardRecognizer(std::unique_ptr<ScheduleHazardRecognizer>(BHR));
  return MHR;
}

// Called during post-RA scheduling when FeatureUseMISched is not set
ScheduleHazardRecognizer *ARMBaseInstrInfo::
CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                   const ScheduleDAG *DAG) const {
  MultiHazardRecognizer *MHR = new MultiHazardRecognizer();

  if (Subtarget.isThumb2() || Subtarget.hasVFP2Base())
    MHR->AddHazardRecognizer(std::make_unique<ARMHazardRecognizerFPMLx>());

  auto BHR = TargetInstrInfo::CreateTargetPostRAHazardRecognizer(II, DAG);
  if (BHR)
    MHR->AddHazardRecognizer(std::unique_ptr<ScheduleHazardRecognizer>(BHR));
  return MHR;
}

MachineInstr *
ARMBaseInstrInfo::convertToThreeAddress(MachineInstr &MI, LiveVariables *LV,
                                        LiveIntervals *LIS) const {
  // FIXME: Thumb2 support.

  if (!EnableARM3Addr)
    return nullptr;

  MachineFunction &MF = *MI.getParent()->getParent();
  uint64_t TSFlags = MI.getDesc().TSFlags;
  bool isPre = false;
  switch ((TSFlags & ARMII::IndexModeMask) >> ARMII::IndexModeShift) {
  default: return nullptr;
  case ARMII::IndexModePre:
    isPre = true;
    break;
  case ARMII::IndexModePost:
    break;
  }

  // Try splitting an indexed load/store to an un-indexed one plus an add/sub
  // operation.
  unsigned MemOpc = getUnindexedOpcode(MI.getOpcode());
  if (MemOpc == 0)
    return nullptr;

  MachineInstr *UpdateMI = nullptr;
  MachineInstr *MemMI = nullptr;
  unsigned AddrMode = (TSFlags & ARMII::AddrModeMask);
  const MCInstrDesc &MCID = MI.getDesc();
  unsigned NumOps = MCID.getNumOperands();
  bool isLoad = !MI.mayStore();
  const MachineOperand &WB = isLoad ? MI.getOperand(1) : MI.getOperand(0);
  const MachineOperand &Base = MI.getOperand(2);
  const MachineOperand &Offset = MI.getOperand(NumOps - 3);
  Register WBReg = WB.getReg();
  Register BaseReg = Base.getReg();
  Register OffReg = Offset.getReg();
  unsigned OffImm = MI.getOperand(NumOps - 2).getImm();
  ARMCC::CondCodes Pred = (ARMCC::CondCodes)MI.getOperand(NumOps - 1).getImm();
  switch (AddrMode) {
  default: llvm_unreachable("Unknown indexed op!");
  case ARMII::AddrMode2: {
    bool isSub = ARM_AM::getAM2Op(OffImm) == ARM_AM::sub;
    unsigned Amt = ARM_AM::getAM2Offset(OffImm);
    if (OffReg == 0) {
      if (ARM_AM::getSOImmVal(Amt) == -1)
        // Can't encode it in a so_imm operand. This transformation will
        // add more than 1 instruction. Abandon!
        return nullptr;
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBri : ARM::ADDri), WBReg)
                     .addReg(BaseReg)
                     .addImm(Amt)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    } else if (Amt != 0) {
      ARM_AM::ShiftOpc ShOpc = ARM_AM::getAM2ShiftOpc(OffImm);
      unsigned SOOpc = ARM_AM::getSORegOpc(ShOpc, Amt);
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBrsi : ARM::ADDrsi), WBReg)
                     .addReg(BaseReg)
                     .addReg(OffReg)
                     .addReg(0)
                     .addImm(SOOpc)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    } else
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBrr : ARM::ADDrr), WBReg)
                     .addReg(BaseReg)
                     .addReg(OffReg)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    break;
  }
  case ARMII::AddrMode3 : {
    bool isSub = ARM_AM::getAM3Op(OffImm) == ARM_AM::sub;
    unsigned Amt = ARM_AM::getAM3Offset(OffImm);
    if (OffReg == 0)
      // Immediate is 8-bits. It's guaranteed to fit in a so_imm operand.
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBri : ARM::ADDri), WBReg)
                     .addReg(BaseReg)
                     .addImm(Amt)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    else
      UpdateMI = BuildMI(MF, MI.getDebugLoc(),
                         get(isSub ? ARM::SUBrr : ARM::ADDrr), WBReg)
                     .addReg(BaseReg)
                     .addReg(OffReg)
                     .add(predOps(Pred))
                     .add(condCodeOp());
    break;
  }
  }

  std::vector<MachineInstr*> NewMIs;
  if (isPre) {
    if (isLoad)
      MemMI =
          BuildMI(MF, MI.getDebugLoc(), get(MemOpc), MI.getOperand(0).getReg())
              .addReg(WBReg)
              .addImm(0)
              .addImm(Pred);
    else
      MemMI = BuildMI(MF, MI.getDebugLoc(), get(MemOpc))
                  .addReg(MI.getOperand(1).getReg())
                  .addReg(WBReg)
                  .addReg(0)
                  .addImm(0)
                  .addImm(Pred);
    NewMIs.push_back(MemMI);
    NewMIs.push_back(UpdateMI);
  } else {
    if (isLoad)
      MemMI =
          BuildMI(MF, MI.getDebugLoc(), get(MemOpc), MI.getOperand(0).getReg())
              .addReg(BaseReg)
              .addImm(0)
              .addImm(Pred);
    else
      MemMI = BuildMI(MF, MI.getDebugLoc(), get(MemOpc))
                  .addReg(MI.getOperand(1).getReg())
                  .addReg(BaseReg)
                  .addReg(0)
                  .addImm(0)
                  .addImm(Pred);
    if (WB.isDead())
      UpdateMI->getOperand(0).setIsDead();
    NewMIs.push_back(UpdateMI);
    NewMIs.push_back(MemMI);
  }

  // Transfer LiveVariables states, kill / dead info.
  if (LV) {
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.getReg().isVirtual()) {
        Register Reg = MO.getReg();

        LiveVariables::VarInfo &VI = LV->getVarInfo(Reg);
        if (MO.isDef()) {
          MachineInstr *NewMI = (Reg == WBReg) ? UpdateMI : MemMI;
          if (MO.isDead())
            LV->addVirtualRegisterDead(Reg, *NewMI);
        }
        if (MO.isUse() && MO.isKill()) {
          for (unsigned j = 0; j < 2; ++j) {
            // Look at the two new MI's in reverse order.
            MachineInstr *NewMI = NewMIs[j];
            if (!NewMI->readsRegister(Reg, /*TRI=*/nullptr))
              continue;
            LV->addVirtualRegisterKilled(Reg, *NewMI);
            if (VI.removeKill(MI))
              VI.Kills.push_back(NewMI);
            break;
          }
        }
      }
    }
  }

  MachineBasicBlock &MBB = *MI.getParent();
  MBB.insert(MI, NewMIs[1]);
  MBB.insert(MI, NewMIs[0]);
  return NewMIs[0];
}

// Branch analysis.
// Cond vector output format:
//   0 elements indicates an unconditional branch
//   2 elements indicates a conditional branch; the elements are
//     the condition to check and the CPSR.
//   3 elements indicates a hardware loop end; the elements
//     are the opcode, the operand value to test, and a dummy
//     operand used to pad out to 3 operands.
bool ARMBaseInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;

  MachineBasicBlock::instr_iterator I = MBB.instr_end();
  if (I == MBB.instr_begin())
    return false; // Empty blocks are easy.
  --I;

  // Walk backwards from the end of the basic block until the branch is
  // analyzed or we give up.
  while (isPredicated(*I) || I->isTerminator() || I->isDebugValue()) {
    // Flag to be raised on unanalyzeable instructions. This is useful in cases
    // where we want to clean up on the end of the basic block before we bail
    // out.
    bool CantAnalyze = false;

    // Skip over DEBUG values, predicated nonterminators and speculation
    // barrier terminators.
    while (I->isDebugInstr() || !I->isTerminator() ||
           isSpeculationBarrierEndBBOpcode(I->getOpcode()) ||
           I->getOpcode() == ARM::t2DoLoopStartTP){
      if (I == MBB.instr_begin())
        return false;
      --I;
    }

    if (isIndirectBranchOpcode(I->getOpcode()) ||
        isJumpTableBranchOpcode(I->getOpcode())) {
      // Indirect branches and jump tables can't be analyzed, but we still want
      // to clean up any instructions at the tail of the basic block.
      CantAnalyze = true;
    } else if (isUncondBranchOpcode(I->getOpcode())) {
      TBB = I->getOperand(0).getMBB();
    } else if (isCondBranchOpcode(I->getOpcode())) {
      // Bail out if we encounter multiple conditional branches.
      if (!Cond.empty())
        return true;

      assert(!FBB && "FBB should have been null.");
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(I->getOperand(1));
      Cond.push_back(I->getOperand(2));
    } else if (I->isReturn()) {
      // Returns can't be analyzed, but we should run cleanup.
      CantAnalyze = true;
    } else if (I->getOpcode() == ARM::t2LoopEnd &&
               MBB.getParent()
                   ->getSubtarget<ARMSubtarget>()
                   .enableMachinePipeliner()) {
      if (!Cond.empty())
        return true;
      FBB = TBB;
      TBB = I->getOperand(1).getMBB();
      Cond.push_back(MachineOperand::CreateImm(I->getOpcode()));
      Cond.push_back(I->getOperand(0));
      Cond.push_back(MachineOperand::CreateImm(0));
    } else {
      // We encountered other unrecognized terminator. Bail out immediately.
      return true;
    }

    // Cleanup code - to be run for unpredicated unconditional branches and
    //                returns.
    if (!isPredicated(*I) &&
          (isUncondBranchOpcode(I->getOpcode()) ||
           isIndirectBranchOpcode(I->getOpcode()) ||
           isJumpTableBranchOpcode(I->getOpcode()) ||
           I->isReturn())) {
      // Forget any previous condition branch information - it no longer applies.
      Cond.clear();
      FBB = nullptr;

      // If we can modify the function, delete everything below this
      // unconditional branch.
      if (AllowModify) {
        MachineBasicBlock::iterator DI = std::next(I);
        while (DI != MBB.instr_end()) {
          MachineInstr &InstToDelete = *DI;
          ++DI;
          // Speculation barriers must not be deleted.
          if (isSpeculationBarrierEndBBOpcode(InstToDelete.getOpcode()))
            continue;
          InstToDelete.eraseFromParent();
        }
      }
    }

    if (CantAnalyze) {
      // We may not be able to analyze the block, but we could still have
      // an unconditional branch as the last instruction in the block, which
      // just branches to layout successor. If this is the case, then just
      // remove it if we're allowed to make modifications.
      if (AllowModify && !isPredicated(MBB.back()) &&
          isUncondBranchOpcode(MBB.back().getOpcode()) &&
          TBB && MBB.isLayoutSuccessor(TBB))
        removeBranch(MBB);
      return true;
    }

    if (I == MBB.instr_begin())
      return false;

    --I;
  }

  // We made it past the terminators without bailing out - we must have
  // analyzed this branch successfully.
  return false;
}

unsigned ARMBaseInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                        int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (!isUncondBranchOpcode(I->getOpcode()) &&
      !isCondBranchOpcode(I->getOpcode()) && I->getOpcode() != ARM::t2LoopEnd)
    return 0;

  // Remove the branch.
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin()) return 1;
  --I;
  if (!isCondBranchOpcode(I->getOpcode()) && I->getOpcode() != ARM::t2LoopEnd)
    return 1;

  // Remove the branch.
  I->eraseFromParent();
  return 2;
}

unsigned ARMBaseInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock *TBB,
                                        MachineBasicBlock *FBB,
                                        ArrayRef<MachineOperand> Cond,
                                        const DebugLoc &DL,
                                        int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");
  ARMFunctionInfo *AFI = MBB.getParent()->getInfo<ARMFunctionInfo>();
  int BOpc   = !AFI->isThumbFunction()
    ? ARM::B : (AFI->isThumb2Function() ? ARM::t2B : ARM::tB);
  int BccOpc = !AFI->isThumbFunction()
    ? ARM::Bcc : (AFI->isThumb2Function() ? ARM::t2Bcc : ARM::tBcc);
  bool isThumb = AFI->isThumbFunction() || AFI->isThumb2Function();

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0 || Cond.size() == 3) &&
         "ARM branch conditions have two or three components!");

  // For conditional branches, we use addOperand to preserve CPSR flags.

  if (!FBB) {
    if (Cond.empty()) { // Unconditional branch?
      if (isThumb)
        BuildMI(&MBB, DL, get(BOpc)).addMBB(TBB).add(predOps(ARMCC::AL));
      else
        BuildMI(&MBB, DL, get(BOpc)).addMBB(TBB);
    } else if (Cond.size() == 2) {
      BuildMI(&MBB, DL, get(BccOpc))
          .addMBB(TBB)
          .addImm(Cond[0].getImm())
          .add(Cond[1]);
    } else
      BuildMI(&MBB, DL, get(Cond[0].getImm())).add(Cond[1]).addMBB(TBB);
    return 1;
  }

  // Two-way conditional branch.
  if (Cond.size() == 2)
    BuildMI(&MBB, DL, get(BccOpc))
        .addMBB(TBB)
        .addImm(Cond[0].getImm())
        .add(Cond[1]);
  else if (Cond.size() == 3)
    BuildMI(&MBB, DL, get(Cond[0].getImm())).add(Cond[1]).addMBB(TBB);
  if (isThumb)
    BuildMI(&MBB, DL, get(BOpc)).addMBB(FBB).add(predOps(ARMCC::AL));
  else
    BuildMI(&MBB, DL, get(BOpc)).addMBB(FBB);
  return 2;
}

bool ARMBaseInstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.size() == 2) {
    ARMCC::CondCodes CC = (ARMCC::CondCodes)(int)Cond[0].getImm();
    Cond[0].setImm(ARMCC::getOppositeCondition(CC));
    return false;
  }
  return true;
}

bool ARMBaseInstrInfo::isPredicated(const MachineInstr &MI) const {
  if (MI.isBundle()) {
    MachineBasicBlock::const_instr_iterator I = MI.getIterator();
    MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
    while (++I != E && I->isInsideBundle()) {
      int PIdx = I->findFirstPredOperandIdx();
      if (PIdx != -1 && I->getOperand(PIdx).getImm() != ARMCC::AL)
        return true;
    }
    return false;
  }

  int PIdx = MI.findFirstPredOperandIdx();
  return PIdx != -1 && MI.getOperand(PIdx).getImm() != ARMCC::AL;
}

std::string ARMBaseInstrInfo::createMIROperandComment(
    const MachineInstr &MI, const MachineOperand &Op, unsigned OpIdx,
    const TargetRegisterInfo *TRI) const {

  // First, let's see if there is a generic comment for this operand
  std::string GenericComment =
      TargetInstrInfo::createMIROperandComment(MI, Op, OpIdx, TRI);
  if (!GenericComment.empty())
    return GenericComment;

  // If not, check if we have an immediate operand.
  if (!Op.isImm())
    return std::string();

  // And print its corresponding condition code if the immediate is a
  // predicate.
  int FirstPredOp = MI.findFirstPredOperandIdx();
  if (FirstPredOp != (int) OpIdx)
    return std::string();

  std::string CC = "CC::";
  CC += ARMCondCodeToString((ARMCC::CondCodes)Op.getImm());
  return CC;
}

bool ARMBaseInstrInfo::PredicateInstruction(
    MachineInstr &MI, ArrayRef<MachineOperand> Pred) const {
  unsigned Opc = MI.getOpcode();
  if (isUncondBranchOpcode(Opc)) {
    MI.setDesc(get(getMatchingCondBranchOpcode(Opc)));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
      .addImm(Pred[0].getImm())
      .addReg(Pred[1].getReg());
    return true;
  }

  int PIdx = MI.findFirstPredOperandIdx();
  if (PIdx != -1) {
    MachineOperand &PMO = MI.getOperand(PIdx);
    PMO.setImm(Pred[0].getImm());
    MI.getOperand(PIdx+1).setReg(Pred[1].getReg());

    // Thumb 1 arithmetic instructions do not set CPSR when executed inside an
    // IT block. This affects how they are printed.
    const MCInstrDesc &MCID = MI.getDesc();
    if (MCID.TSFlags & ARMII::ThumbArithFlagSetting) {
      assert(MCID.operands()[1].isOptionalDef() &&
             "CPSR def isn't expected operand");
      assert((MI.getOperand(1).isDead() ||
              MI.getOperand(1).getReg() != ARM::CPSR) &&
             "if conversion tried to stop defining used CPSR");
      MI.getOperand(1).setReg(ARM::NoRegister);
    }

    return true;
  }
  return false;
}

bool ARMBaseInstrInfo::SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                                         ArrayRef<MachineOperand> Pred2) const {
  if (Pred1.size() > 2 || Pred2.size() > 2)
    return false;

  ARMCC::CondCodes CC1 = (ARMCC::CondCodes)Pred1[0].getImm();
  ARMCC::CondCodes CC2 = (ARMCC::CondCodes)Pred2[0].getImm();
  if (CC1 == CC2)
    return true;

  switch (CC1) {
  default:
    return false;
  case ARMCC::AL:
    return true;
  case ARMCC::HS:
    return CC2 == ARMCC::HI;
  case ARMCC::LS:
    return CC2 == ARMCC::LO || CC2 == ARMCC::EQ;
  case ARMCC::GE:
    return CC2 == ARMCC::GT;
  case ARMCC::LE:
    return CC2 == ARMCC::LT;
  }
}

bool ARMBaseInstrInfo::ClobbersPredicate(MachineInstr &MI,
                                         std::vector<MachineOperand> &Pred,
                                         bool SkipDead) const {
  bool Found = false;
  for (const MachineOperand &MO : MI.operands()) {
    bool ClobbersCPSR = MO.isRegMask() && MO.clobbersPhysReg(ARM::CPSR);
    bool IsCPSR = MO.isReg() && MO.isDef() && MO.getReg() == ARM::CPSR;
    if (ClobbersCPSR || IsCPSR) {

      // Filter out T1 instructions that have a dead CPSR,
      // allowing IT blocks to be generated containing T1 instructions
      const MCInstrDesc &MCID = MI.getDesc();
      if (MCID.TSFlags & ARMII::ThumbArithFlagSetting && MO.isDead() &&
          SkipDead)
        continue;

      Pred.push_back(MO);
      Found = true;
    }
  }

  return Found;
}

bool ARMBaseInstrInfo::isCPSRDefined(const MachineInstr &MI) {
  for (const auto &MO : MI.operands())
    if (MO.isReg() && MO.getReg() == ARM::CPSR && MO.isDef() && !MO.isDead())
      return true;
  return false;
}

static bool isEligibleForITBlock(const MachineInstr *MI) {
  switch (MI->getOpcode()) {
  default: return true;
  case ARM::tADC:   // ADC (register) T1
  case ARM::tADDi3: // ADD (immediate) T1
  case ARM::tADDi8: // ADD (immediate) T2
  case ARM::tADDrr: // ADD (register) T1
  case ARM::tAND:   // AND (register) T1
  case ARM::tASRri: // ASR (immediate) T1
  case ARM::tASRrr: // ASR (register) T1
  case ARM::tBIC:   // BIC (register) T1
  case ARM::tEOR:   // EOR (register) T1
  case ARM::tLSLri: // LSL (immediate) T1
  case ARM::tLSLrr: // LSL (register) T1
  case ARM::tLSRri: // LSR (immediate) T1
  case ARM::tLSRrr: // LSR (register) T1
  case ARM::tMUL:   // MUL T1
  case ARM::tMVN:   // MVN (register) T1
  case ARM::tORR:   // ORR (register) T1
  case ARM::tROR:   // ROR (register) T1
  case ARM::tRSB:   // RSB (immediate) T1
  case ARM::tSBC:   // SBC (register) T1
  case ARM::tSUBi3: // SUB (immediate) T1
  case ARM::tSUBi8: // SUB (immediate) T2
  case ARM::tSUBrr: // SUB (register) T1
    return !ARMBaseInstrInfo::isCPSRDefined(*MI);
  }
}

/// isPredicable - Return true if the specified instruction can be predicated.
/// By default, this returns true for every instruction with a
/// PredicateOperand.
bool ARMBaseInstrInfo::isPredicable(const MachineInstr &MI) const {
  if (!MI.isPredicable())
    return false;

  if (MI.isBundle())
    return false;

  if (!isEligibleForITBlock(&MI))
    return false;

  const MachineFunction *MF = MI.getParent()->getParent();
  const ARMFunctionInfo *AFI =
      MF->getInfo<ARMFunctionInfo>();

  // Neon instructions in Thumb2 IT blocks are deprecated, see ARMARM.
  // In their ARM encoding, they can't be encoded in a conditional form.
  if ((MI.getDesc().TSFlags & ARMII::DomainMask) == ARMII::DomainNEON)
    return false;

  // Make indirect control flow changes unpredicable when SLS mitigation is
  // enabled.
  const ARMSubtarget &ST = MF->getSubtarget<ARMSubtarget>();
  if (ST.hardenSlsRetBr() && isIndirectControlFlowNotComingBack(MI))
    return false;
  if (ST.hardenSlsBlr() && isIndirectCall(MI))
    return false;

  if (AFI->isThumb2Function()) {
    if (getSubtarget().restrictIT())
      return isV8EligibleForIT(&MI);
  }

  return true;
}

namespace llvm {

template <> bool IsCPSRDead<MachineInstr>(const MachineInstr *MI) {
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || MO.isUndef() || MO.isUse())
      continue;
    if (MO.getReg() != ARM::CPSR)
      continue;
    if (!MO.isDead())
      return false;
  }
  // all definitions of CPSR are dead
  return true;
}

} // end namespace llvm

/// GetInstSize - Return the size of the specified MachineInstr.
///
unsigned ARMBaseInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  const MachineBasicBlock &MBB = *MI.getParent();
  const MachineFunction *MF = MBB.getParent();
  const MCAsmInfo *MAI = MF->getTarget().getMCAsmInfo();

  const MCInstrDesc &MCID = MI.getDesc();

  switch (MI.getOpcode()) {
  default:
    // Return the size specified in .td file. If there's none, return 0, as we
    // can't define a default size (Thumb1 instructions are 2 bytes, Thumb2
    // instructions are 2-4 bytes, and ARM instructions are 4 bytes), in
    // contrast to AArch64 instructions which have a default size of 4 bytes for
    // example.
    return MCID.getSize();
  case TargetOpcode::BUNDLE:
    return getInstBundleLength(MI);
  case ARM::CONSTPOOL_ENTRY:
  case ARM::JUMPTABLE_INSTS:
  case ARM::JUMPTABLE_ADDRS:
  case ARM::JUMPTABLE_TBB:
  case ARM::JUMPTABLE_TBH:
    // If this machine instr is a constant pool entry, its size is recorded as
    // operand #2.
    return MI.getOperand(2).getImm();
  case ARM::SPACE:
    return MI.getOperand(1).getImm();
  case ARM::INLINEASM:
  case ARM::INLINEASM_BR: {
    // If this machine instr is an inline asm, measure it.
    unsigned Size = getInlineAsmLength(MI.getOperand(0).getSymbolName(), *MAI);
    if (!MF->getInfo<ARMFunctionInfo>()->isThumbFunction())
      Size = alignTo(Size, 4);
    return Size;
  }
  }
}

unsigned ARMBaseInstrInfo::getInstBundleLength(const MachineInstr &MI) const {
  unsigned Size = 0;
  MachineBasicBlock::const_instr_iterator I = MI.getIterator();
  MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
  while (++I != E && I->isInsideBundle()) {
    assert(!I->isBundle() && "No nested bundle!");
    Size += getInstSizeInBytes(*I);
  }
  return Size;
}

void ARMBaseInstrInfo::copyFromCPSR(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    unsigned DestReg, bool KillSrc,
                                    const ARMSubtarget &Subtarget) const {
  unsigned Opc = Subtarget.isThumb()
                     ? (Subtarget.isMClass() ? ARM::t2MRS_M : ARM::t2MRS_AR)
                     : ARM::MRS;

  MachineInstrBuilder MIB =
      BuildMI(MBB, I, I->getDebugLoc(), get(Opc), DestReg);

  // There is only 1 A/R class MRS instruction, and it always refers to
  // APSR. However, there are lots of other possibilities on M-class cores.
  if (Subtarget.isMClass())
    MIB.addImm(0x800);

  MIB.add(predOps(ARMCC::AL))
     .addReg(ARM::CPSR, RegState::Implicit | getKillRegState(KillSrc));
}

void ARMBaseInstrInfo::copyToCPSR(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  unsigned SrcReg, bool KillSrc,
                                  const ARMSubtarget &Subtarget) const {
  unsigned Opc = Subtarget.isThumb()
                     ? (Subtarget.isMClass() ? ARM::t2MSR_M : ARM::t2MSR_AR)
                     : ARM::MSR;

  MachineInstrBuilder MIB = BuildMI(MBB, I, I->getDebugLoc(), get(Opc));

  if (Subtarget.isMClass())
    MIB.addImm(0x800);
  else
    MIB.addImm(8);

  MIB.addReg(SrcReg, getKillRegState(KillSrc))
     .add(predOps(ARMCC::AL))
     .addReg(ARM::CPSR, RegState::Implicit | RegState::Define);
}

void llvm::addUnpredicatedMveVpredNOp(MachineInstrBuilder &MIB) {
  MIB.addImm(ARMVCC::None);
  MIB.addReg(0);
  MIB.addReg(0); // tp_reg
}

void llvm::addUnpredicatedMveVpredROp(MachineInstrBuilder &MIB,
                                      Register DestReg) {
  addUnpredicatedMveVpredNOp(MIB);
  MIB.addReg(DestReg, RegState::Undef);
}

void llvm::addPredicatedMveVpredNOp(MachineInstrBuilder &MIB, unsigned Cond) {
  MIB.addImm(Cond);
  MIB.addReg(ARM::VPR, RegState::Implicit);
  MIB.addReg(0); // tp_reg
}

void llvm::addPredicatedMveVpredROp(MachineInstrBuilder &MIB,
                                    unsigned Cond, unsigned Inactive) {
  addPredicatedMveVpredNOp(MIB, Cond);
  MIB.addReg(Inactive);
}

void ARMBaseInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, MCRegister DestReg,
                                   MCRegister SrcReg, bool KillSrc) const {
  bool GPRDest = ARM::GPRRegClass.contains(DestReg);
  bool GPRSrc = ARM::GPRRegClass.contains(SrcReg);

  if (GPRDest && GPRSrc) {
    BuildMI(MBB, I, DL, get(ARM::MOVr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    return;
  }

  bool SPRDest = ARM::SPRRegClass.contains(DestReg);
  bool SPRSrc = ARM::SPRRegClass.contains(SrcReg);

  unsigned Opc = 0;
  if (SPRDest && SPRSrc)
    Opc = ARM::VMOVS;
  else if (GPRDest && SPRSrc)
    Opc = ARM::VMOVRS;
  else if (SPRDest && GPRSrc)
    Opc = ARM::VMOVSR;
  else if (ARM::DPRRegClass.contains(DestReg, SrcReg) && Subtarget.hasFP64())
    Opc = ARM::VMOVD;
  else if (ARM::QPRRegClass.contains(DestReg, SrcReg))
    Opc = Subtarget.hasNEON() ? ARM::VORRq : ARM::MQPRCopy;

  if (Opc) {
    MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opc), DestReg);
    MIB.addReg(SrcReg, getKillRegState(KillSrc));
    if (Opc == ARM::VORRq || Opc == ARM::MVE_VORR)
      MIB.addReg(SrcReg, getKillRegState(KillSrc));
    if (Opc == ARM::MVE_VORR)
      addUnpredicatedMveVpredROp(MIB, DestReg);
    else if (Opc != ARM::MQPRCopy)
      MIB.add(predOps(ARMCC::AL));
    return;
  }

  // Handle register classes that require multiple instructions.
  unsigned BeginIdx = 0;
  unsigned SubRegs = 0;
  int Spacing = 1;

  // Use VORRq when possible.
  if (ARM::QQPRRegClass.contains(DestReg, SrcReg)) {
    Opc = Subtarget.hasNEON() ? ARM::VORRq : ARM::MVE_VORR;
    BeginIdx = ARM::qsub_0;
    SubRegs = 2;
  } else if (ARM::QQQQPRRegClass.contains(DestReg, SrcReg)) {
    Opc = Subtarget.hasNEON() ? ARM::VORRq : ARM::MVE_VORR;
    BeginIdx = ARM::qsub_0;
    SubRegs = 4;
  // Fall back to VMOVD.
  } else if (ARM::DPairRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 2;
  } else if (ARM::DTripleRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 3;
  } else if (ARM::DQuadRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 4;
  } else if (ARM::GPRPairRegClass.contains(DestReg, SrcReg)) {
    Opc = Subtarget.isThumb2() ? ARM::tMOVr : ARM::MOVr;
    BeginIdx = ARM::gsub_0;
    SubRegs = 2;
  } else if (ARM::DPairSpcRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 2;
    Spacing = 2;
  } else if (ARM::DTripleSpcRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 3;
    Spacing = 2;
  } else if (ARM::DQuadSpcRegClass.contains(DestReg, SrcReg)) {
    Opc = ARM::VMOVD;
    BeginIdx = ARM::dsub_0;
    SubRegs = 4;
    Spacing = 2;
  } else if (ARM::DPRRegClass.contains(DestReg, SrcReg) &&
             !Subtarget.hasFP64()) {
    Opc = ARM::VMOVS;
    BeginIdx = ARM::ssub_0;
    SubRegs = 2;
  } else if (SrcReg == ARM::CPSR) {
    copyFromCPSR(MBB, I, DestReg, KillSrc, Subtarget);
    return;
  } else if (DestReg == ARM::CPSR) {
    copyToCPSR(MBB, I, SrcReg, KillSrc, Subtarget);
    return;
  } else if (DestReg == ARM::VPR) {
    assert(ARM::GPRRegClass.contains(SrcReg));
    BuildMI(MBB, I, I->getDebugLoc(), get(ARM::VMSR_P0), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .add(predOps(ARMCC::AL));
    return;
  } else if (SrcReg == ARM::VPR) {
    assert(ARM::GPRRegClass.contains(DestReg));
    BuildMI(MBB, I, I->getDebugLoc(), get(ARM::VMRS_P0), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .add(predOps(ARMCC::AL));
    return;
  } else if (DestReg == ARM::FPSCR_NZCV) {
    assert(ARM::GPRRegClass.contains(SrcReg));
    BuildMI(MBB, I, I->getDebugLoc(), get(ARM::VMSR_FPSCR_NZCVQC), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .add(predOps(ARMCC::AL));
    return;
  } else if (SrcReg == ARM::FPSCR_NZCV) {
    assert(ARM::GPRRegClass.contains(DestReg));
    BuildMI(MBB, I, I->getDebugLoc(), get(ARM::VMRS_FPSCR_NZCVQC), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .add(predOps(ARMCC::AL));
    return;
  }

  assert(Opc && "Impossible reg-to-reg copy");

  const TargetRegisterInfo *TRI = &getRegisterInfo();
  MachineInstrBuilder Mov;

  // Copy register tuples backward when the first Dest reg overlaps with SrcReg.
  if (TRI->regsOverlap(SrcReg, TRI->getSubReg(DestReg, BeginIdx))) {
    BeginIdx = BeginIdx + ((SubRegs - 1) * Spacing);
    Spacing = -Spacing;
  }
#ifndef NDEBUG
  SmallSet<unsigned, 4> DstRegs;
#endif
  for (unsigned i = 0; i != SubRegs; ++i) {
    Register Dst = TRI->getSubReg(DestReg, BeginIdx + i * Spacing);
    Register Src = TRI->getSubReg(SrcReg, BeginIdx + i * Spacing);
    assert(Dst && Src && "Bad sub-register");
#ifndef NDEBUG
    assert(!DstRegs.count(Src) && "destructive vector copy");
    DstRegs.insert(Dst);
#endif
    Mov = BuildMI(MBB, I, I->getDebugLoc(), get(Opc), Dst).addReg(Src);
    // VORR (NEON or MVE) takes two source operands.
    if (Opc == ARM::VORRq || Opc == ARM::MVE_VORR) {
      Mov.addReg(Src);
    }
    // MVE VORR takes predicate operands in place of an ordinary condition.
    if (Opc == ARM::MVE_VORR)
      addUnpredicatedMveVpredROp(Mov, Dst);
    else
      Mov = Mov.add(predOps(ARMCC::AL));
    // MOVr can set CC.
    if (Opc == ARM::MOVr)
      Mov = Mov.add(condCodeOp());
  }
  // Add implicit super-register defs and kills to the last instruction.
  Mov->addRegisterDefined(DestReg, TRI);
  if (KillSrc)
    Mov->addRegisterKilled(SrcReg, TRI);
}

std::optional<DestSourcePair>
ARMBaseInstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {
  // VMOVRRD is also a copy instruction but it requires
  // special way of handling. It is more complex copy version
  // and since that we are not considering it. For recognition
  // of such instruction isExtractSubregLike MI interface fuction
  // could be used.
  // VORRq is considered as a move only if two inputs are
  // the same register.
  if (!MI.isMoveReg() ||
      (MI.getOpcode() == ARM::VORRq &&
       MI.getOperand(1).getReg() != MI.getOperand(2).getReg()))
    return std::nullopt;
  return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};
}

std::optional<ParamLoadedValue>
ARMBaseInstrInfo::describeLoadedValue(const MachineInstr &MI,
                                      Register Reg) const {
  if (auto DstSrcPair = isCopyInstrImpl(MI)) {
    Register DstReg = DstSrcPair->Destination->getReg();

    // TODO: We don't handle cases where the forwarding reg is narrower/wider
    // than the copy registers. Consider for example:
    //
    //   s16 = VMOVS s0
    //   s17 = VMOVS s1
    //   call @callee(d0)
    //
    // We'd like to describe the call site value of d0 as d8, but this requires
    // gathering and merging the descriptions for the two VMOVS instructions.
    //
    // We also don't handle the reverse situation, where the forwarding reg is
    // narrower than the copy destination:
    //
    //   d8 = VMOVD d0
    //   call @callee(s1)
    //
    // We need to produce a fragment description (the call site value of s1 is
    // /not/ just d8).
    if (DstReg != Reg)
      return std::nullopt;
  }
  return TargetInstrInfo::describeLoadedValue(MI, Reg);
}

const MachineInstrBuilder &
ARMBaseInstrInfo::AddDReg(MachineInstrBuilder &MIB, unsigned Reg,
                          unsigned SubIdx, unsigned State,
                          const TargetRegisterInfo *TRI) const {
  if (!SubIdx)
    return MIB.addReg(Reg, State);

  if (Register::isPhysicalRegister(Reg))
    return MIB.addReg(TRI->getSubReg(Reg, SubIdx), State);
  return MIB.addReg(Reg, State, SubIdx);
}

void ARMBaseInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator I,
                                           Register SrcReg, bool isKill, int FI,
                                           const TargetRegisterClass *RC,
                                           const TargetRegisterInfo *TRI,
                                           Register VReg) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  Align Alignment = MFI.getObjectAlign(FI);

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), Alignment);

  switch (TRI->getSpillSize(*RC)) {
    case 2:
      if (ARM::HPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::VSTRH))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 4:
      if (ARM::GPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::STRi12))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else if (ARM::SPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::VSTRS))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else if (ARM::VCCRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::VSTR_P0_off))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 8:
      if (ARM::DPRRegClass.hasSubClassEq(RC)) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::VSTRD))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addImm(0)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else if (ARM::GPRPairRegClass.hasSubClassEq(RC)) {
        if (Subtarget.hasV5TEOps()) {
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(), get(ARM::STRD));
          AddDReg(MIB, SrcReg, ARM::gsub_0, getKillRegState(isKill), TRI);
          AddDReg(MIB, SrcReg, ARM::gsub_1, 0, TRI);
          MIB.addFrameIndex(FI).addReg(0).addImm(0).addMemOperand(MMO)
             .add(predOps(ARMCC::AL));
        } else {
          // Fallback to STM instruction, which has existed since the dawn of
          // time.
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(), get(ARM::STMIA))
                                        .addFrameIndex(FI)
                                        .addMemOperand(MMO)
                                        .add(predOps(ARMCC::AL));
          AddDReg(MIB, SrcReg, ARM::gsub_0, getKillRegState(isKill), TRI);
          AddDReg(MIB, SrcReg, ARM::gsub_1, 0, TRI);
        }
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 16:
      if (ARM::DPairRegClass.hasSubClassEq(RC) && Subtarget.hasNEON()) {
        // Use aligned spills if the stack can be realigned.
        if (Alignment >= 16 && getRegisterInfo().canRealignStack(MF)) {
          BuildMI(MBB, I, DebugLoc(), get(ARM::VST1q64))
              .addFrameIndex(FI)
              .addImm(16)
              .addReg(SrcReg, getKillRegState(isKill))
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        } else {
          BuildMI(MBB, I, DebugLoc(), get(ARM::VSTMQIA))
              .addReg(SrcReg, getKillRegState(isKill))
              .addFrameIndex(FI)
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        }
      } else if (ARM::QPRRegClass.hasSubClassEq(RC) &&
                 Subtarget.hasMVEIntegerOps()) {
        auto MIB = BuildMI(MBB, I, DebugLoc(), get(ARM::MVE_VSTRWU32));
        MIB.addReg(SrcReg, getKillRegState(isKill))
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO);
        addUnpredicatedMveVpredNOp(MIB);
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 24:
      if (ARM::DTripleRegClass.hasSubClassEq(RC)) {
        // Use aligned spills if the stack can be realigned.
        if (Alignment >= 16 && getRegisterInfo().canRealignStack(MF) &&
            Subtarget.hasNEON()) {
          BuildMI(MBB, I, DebugLoc(), get(ARM::VST1d64TPseudo))
              .addFrameIndex(FI)
              .addImm(16)
              .addReg(SrcReg, getKillRegState(isKill))
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        } else {
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(),
                                            get(ARM::VSTMDIA))
                                        .addFrameIndex(FI)
                                        .add(predOps(ARMCC::AL))
                                        .addMemOperand(MMO);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_0, getKillRegState(isKill), TRI);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_1, 0, TRI);
          AddDReg(MIB, SrcReg, ARM::dsub_2, 0, TRI);
        }
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 32:
      if (ARM::QQPRRegClass.hasSubClassEq(RC) ||
          ARM::MQQPRRegClass.hasSubClassEq(RC) ||
          ARM::DQuadRegClass.hasSubClassEq(RC)) {
        if (Alignment >= 16 && getRegisterInfo().canRealignStack(MF) &&
            Subtarget.hasNEON()) {
          // FIXME: It's possible to only store part of the QQ register if the
          // spilled def has a sub-register index.
          BuildMI(MBB, I, DebugLoc(), get(ARM::VST1d64QPseudo))
              .addFrameIndex(FI)
              .addImm(16)
              .addReg(SrcReg, getKillRegState(isKill))
              .addMemOperand(MMO)
              .add(predOps(ARMCC::AL));
        } else if (Subtarget.hasMVEIntegerOps()) {
          BuildMI(MBB, I, DebugLoc(), get(ARM::MQQPRStore))
              .addReg(SrcReg, getKillRegState(isKill))
              .addFrameIndex(FI)
              .addMemOperand(MMO);
        } else {
          MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(),
                                            get(ARM::VSTMDIA))
                                        .addFrameIndex(FI)
                                        .add(predOps(ARMCC::AL))
                                        .addMemOperand(MMO);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_0, getKillRegState(isKill), TRI);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_1, 0, TRI);
          MIB = AddDReg(MIB, SrcReg, ARM::dsub_2, 0, TRI);
                AddDReg(MIB, SrcReg, ARM::dsub_3, 0, TRI);
        }
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    case 64:
      if (ARM::MQQQQPRRegClass.hasSubClassEq(RC) &&
          Subtarget.hasMVEIntegerOps()) {
        BuildMI(MBB, I, DebugLoc(), get(ARM::MQQQQPRStore))
            .addReg(SrcReg, getKillRegState(isKill))
            .addFrameIndex(FI)
            .addMemOperand(MMO);
      } else if (ARM::QQQQPRRegClass.hasSubClassEq(RC)) {
        MachineInstrBuilder MIB = BuildMI(MBB, I, DebugLoc(), get(ARM::VSTMDIA))
                                      .addFrameIndex(FI)
                                      .add(predOps(ARMCC::AL))
                                      .addMemOperand(MMO);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_0, getKillRegState(isKill), TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_1, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_2, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_3, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_4, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_5, 0, TRI);
        MIB = AddDReg(MIB, SrcReg, ARM::dsub_6, 0, TRI);
              AddDReg(MIB, SrcReg, ARM::dsub_7, 0, TRI);
      } else
        llvm_unreachable("Unknown reg class!");
      break;
    default:
      llvm_unreachable("Unknown reg class!");
  }
}

Register ARMBaseInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                              int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default: break;
  case ARM::STRrs:
  case ARM::t2STRs: // FIXME: don't use t2STRs to access frame.
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isReg() &&
        MI.getOperand(3).isImm() && MI.getOperand(2).getReg() == 0 &&
        MI.getOperand(3).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::STRi12:
  case ARM::t2STRi12:
  case ARM::tSTRspi:
  case ARM::VSTRD:
  case ARM::VSTRS:
  case ARM::VSTR_P0_off:
  case ARM::MVE_VSTRWU32:
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
        MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::VST1q64:
  case ARM::VST1d64TPseudo:
  case ARM::VST1d64QPseudo:
    if (MI.getOperand(0).isFI() && MI.getOperand(2).getSubReg() == 0) {
      FrameIndex = MI.getOperand(0).getIndex();
      return MI.getOperand(2).getReg();
    }
    break;
  case ARM::VSTMQIA:
    if (MI.getOperand(1).isFI() && MI.getOperand(0).getSubReg() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::MQQPRStore:
  case ARM::MQQQQPRStore:
    if (MI.getOperand(1).isFI()) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }

  return 0;
}

Register ARMBaseInstrInfo::isStoreToStackSlotPostFE(const MachineInstr &MI,
                                                    int &FrameIndex) const {
  SmallVector<const MachineMemOperand *, 1> Accesses;
  if (MI.mayStore() && hasStoreToStackSlot(MI, Accesses) &&
      Accesses.size() == 1) {
    FrameIndex =
        cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
            ->getFrameIndex();
    return true;
  }
  return false;
}

void ARMBaseInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator I,
                                            Register DestReg, int FI,
                                            const TargetRegisterClass *RC,
                                            const TargetRegisterInfo *TRI,
                                            Register VReg) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const Align Alignment = MFI.getObjectAlign(FI);
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), Alignment);

  switch (TRI->getSpillSize(*RC)) {
  case 2:
    if (ARM::HPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::VLDRH), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 4:
    if (ARM::GPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::LDRi12), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else if (ARM::SPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::VLDRS), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else if (ARM::VCCRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::VLDR_P0_off), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 8:
    if (ARM::DPRRegClass.hasSubClassEq(RC)) {
      BuildMI(MBB, I, DL, get(ARM::VLDRD), DestReg)
          .addFrameIndex(FI)
          .addImm(0)
          .addMemOperand(MMO)
          .add(predOps(ARMCC::AL));
    } else if (ARM::GPRPairRegClass.hasSubClassEq(RC)) {
      MachineInstrBuilder MIB;

      if (Subtarget.hasV5TEOps()) {
        MIB = BuildMI(MBB, I, DL, get(ARM::LDRD));
        AddDReg(MIB, DestReg, ARM::gsub_0, RegState::DefineNoRead, TRI);
        AddDReg(MIB, DestReg, ARM::gsub_1, RegState::DefineNoRead, TRI);
        MIB.addFrameIndex(FI).addReg(0).addImm(0).addMemOperand(MMO)
           .add(predOps(ARMCC::AL));
      } else {
        // Fallback to LDM instruction, which has existed since the dawn of
        // time.
        MIB = BuildMI(MBB, I, DL, get(ARM::LDMIA))
                  .addFrameIndex(FI)
                  .addMemOperand(MMO)
                  .add(predOps(ARMCC::AL));
        MIB = AddDReg(MIB, DestReg, ARM::gsub_0, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::gsub_1, RegState::DefineNoRead, TRI);
      }

      if (DestReg.isPhysical())
        MIB.addReg(DestReg, RegState::ImplicitDefine);
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 16:
    if (ARM::DPairRegClass.hasSubClassEq(RC) && Subtarget.hasNEON()) {
      if (Alignment >= 16 && getRegisterInfo().canRealignStack(MF)) {
        BuildMI(MBB, I, DL, get(ARM::VLD1q64), DestReg)
            .addFrameIndex(FI)
            .addImm(16)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else {
        BuildMI(MBB, I, DL, get(ARM::VLDMQIA), DestReg)
            .addFrameIndex(FI)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      }
    } else if (ARM::QPRRegClass.hasSubClassEq(RC) &&
               Subtarget.hasMVEIntegerOps()) {
      auto MIB = BuildMI(MBB, I, DL, get(ARM::MVE_VLDRWU32), DestReg);
      MIB.addFrameIndex(FI)
        .addImm(0)
        .addMemOperand(MMO);
      addUnpredicatedMveVpredNOp(MIB);
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  case 24:
    if (ARM::DTripleRegClass.hasSubClassEq(RC)) {
      if (Alignment >= 16 && getRegisterInfo().canRealignStack(MF) &&
          Subtarget.hasNEON()) {
        BuildMI(MBB, I, DL, get(ARM::VLD1d64TPseudo), DestReg)
            .addFrameIndex(FI)
            .addImm(16)
            .addMemOperand(MMO)
            .add(predOps(ARMCC::AL));
      } else {
        MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::VLDMDIA))
                                      .addFrameIndex(FI)
                                      .addMemOperand(MMO)
                                      .add(predOps(ARMCC::AL));
        MIB = AddDReg(MIB, DestReg, ARM::dsub_0, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_1, RegState::DefineNoRead, TRI);
        MIB = AddDReg(MIB, DestReg, ARM::dsub_2, RegState::DefineNoRead, TRI);
        if (DestReg.isPhysical())
          MIB.addReg(DestReg, RegState::ImplicitDefine);
      }
    } else
      llvm_unreachable("Unknown reg class!");
    break;
   case 32:
     if (ARM::QQPRRegClass.hasSubClassEq(RC) ||
         ARM::MQQPRRegClass.hasSubClassEq(RC) ||
         ARM::DQuadRegClass.hasSubClassEq(RC)) {
       if (Alignment >= 16 && getRegisterInfo().canRealignStack(MF) &&
           Subtarget.hasNEON()) {
         BuildMI(MBB, I, DL, get(ARM::VLD1d64QPseudo), DestReg)
             .addFrameIndex(FI)
             .addImm(16)
             .addMemOperand(MMO)
             .add(predOps(ARMCC::AL));
       } else if (Subtarget.hasMVEIntegerOps()) {
         BuildMI(MBB, I, DL, get(ARM::MQQPRLoad), DestReg)
             .addFrameIndex(FI)
             .addMemOperand(MMO);
       } else {
         MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::VLDMDIA))
                                       .addFrameIndex(FI)
                                       .add(predOps(ARMCC::AL))
                                       .addMemOperand(MMO);
         MIB = AddDReg(MIB, DestReg, ARM::dsub_0, RegState::DefineNoRead, TRI);
         MIB = AddDReg(MIB, DestReg, ARM::dsub_1, RegState::DefineNoRead, TRI);
         MIB = AddDReg(MIB, DestReg, ARM::dsub_2, RegState::DefineNoRead, TRI);
         MIB = AddDReg(MIB, DestReg, ARM::dsub_3, RegState::DefineNoRead, TRI);
         if (DestReg.isPhysical())
           MIB.addReg(DestReg, RegState::ImplicitDefine);
       }
     } else
       llvm_unreachable("Unknown reg class!");
     break;
  case 64:
    if (ARM::MQQQQPRRegClass.hasSubClassEq(RC) &&
        Subtarget.hasMVEIntegerOps()) {
      BuildMI(MBB, I, DL, get(ARM::MQQQQPRLoad), DestReg)
          .addFrameIndex(FI)
          .addMemOperand(MMO);
    } else if (ARM::QQQQPRRegClass.hasSubClassEq(RC)) {
      MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::VLDMDIA))
                                    .addFrameIndex(FI)
                                    .add(predOps(ARMCC::AL))
                                    .addMemOperand(MMO);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_0, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_1, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_2, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_3, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_4, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_5, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_6, RegState::DefineNoRead, TRI);
      MIB = AddDReg(MIB, DestReg, ARM::dsub_7, RegState::DefineNoRead, TRI);
      if (DestReg.isPhysical())
        MIB.addReg(DestReg, RegState::ImplicitDefine);
    } else
      llvm_unreachable("Unknown reg class!");
    break;
  default:
    llvm_unreachable("Unknown regclass!");
  }
}

Register ARMBaseInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                               int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default: break;
  case ARM::LDRrs:
  case ARM::t2LDRs:  // FIXME: don't use t2LDRs to access frame.
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isReg() &&
        MI.getOperand(3).isImm() && MI.getOperand(2).getReg() == 0 &&
        MI.getOperand(3).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::LDRi12:
  case ARM::t2LDRi12:
  case ARM::tLDRspi:
  case ARM::VLDRD:
  case ARM::VLDRS:
  case ARM::VLDR_P0_off:
  case ARM::MVE_VLDRWU32:
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
        MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::VLD1q64:
  case ARM::VLD1d8TPseudo:
  case ARM::VLD1d16TPseudo:
  case ARM::VLD1d32TPseudo:
  case ARM::VLD1d64TPseudo:
  case ARM::VLD1d8QPseudo:
  case ARM::VLD1d16QPseudo:
  case ARM::VLD1d32QPseudo:
  case ARM::VLD1d64QPseudo:
    if (MI.getOperand(1).isFI() && MI.getOperand(0).getSubReg() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::VLDMQIA:
    if (MI.getOperand(1).isFI() && MI.getOperand(0).getSubReg() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  case ARM::MQQPRLoad:
  case ARM::MQQQQPRLoad:
    if (MI.getOperand(1).isFI()) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }

  return 0;
}

Register ARMBaseInstrInfo::isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                                     int &FrameIndex) const {
  SmallVector<const MachineMemOperand *, 1> Accesses;
  if (MI.mayLoad() && hasLoadFromStackSlot(MI, Accesses) &&
      Accesses.size() == 1) {
    FrameIndex =
        cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
            ->getFrameIndex();
    return true;
  }
  return false;
}

/// Expands MEMCPY to either LDMIA/STMIA or LDMIA_UPD/STMID_UPD
/// depending on whether the result is used.
void ARMBaseInstrInfo::expandMEMCPY(MachineBasicBlock::iterator MI) const {
  bool isThumb1 = Subtarget.isThumb1Only();
  bool isThumb2 = Subtarget.isThumb2();
  const ARMBaseInstrInfo *TII = Subtarget.getInstrInfo();

  DebugLoc dl = MI->getDebugLoc();
  MachineBasicBlock *BB = MI->getParent();

  MachineInstrBuilder LDM, STM;
  if (isThumb1 || !MI->getOperand(1).isDead()) {
    MachineOperand LDWb(MI->getOperand(1));
    LDM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2LDMIA_UPD
                                                 : isThumb1 ? ARM::tLDMIA_UPD
                                                            : ARM::LDMIA_UPD))
              .add(LDWb);
  } else {
    LDM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2LDMIA : ARM::LDMIA));
  }

  if (isThumb1 || !MI->getOperand(0).isDead()) {
    MachineOperand STWb(MI->getOperand(0));
    STM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2STMIA_UPD
                                                 : isThumb1 ? ARM::tSTMIA_UPD
                                                            : ARM::STMIA_UPD))
              .add(STWb);
  } else {
    STM = BuildMI(*BB, MI, dl, TII->get(isThumb2 ? ARM::t2STMIA : ARM::STMIA));
  }

  MachineOperand LDBase(MI->getOperand(3));
  LDM.add(LDBase).add(predOps(ARMCC::AL));

  MachineOperand STBase(MI->getOperand(2));
  STM.add(STBase).add(predOps(ARMCC::AL));

  // Sort the scratch registers into ascending order.
  const TargetRegisterInfo &TRI = getRegisterInfo();
  SmallVector<unsigned, 6> ScratchRegs;
  for (MachineOperand &MO : llvm::drop_begin(MI->operands(), 5))
    ScratchRegs.push_back(MO.getReg());
  llvm::sort(ScratchRegs,
             [&TRI](const unsigned &Reg1, const unsigned &Reg2) -> bool {
               return TRI.getEncodingValue(Reg1) <
                      TRI.getEncodingValue(Reg2);
             });

  for (const auto &Reg : ScratchRegs) {
    LDM.addReg(Reg, RegState::Define);
    STM.addReg(Reg, RegState::Kill);
  }

  BB->erase(MI);
}

bool ARMBaseInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() == TargetOpcode::LOAD_STACK_GUARD) {
    expandLoadStackGuard(MI);
    MI.getParent()->erase(MI);
    return true;
  }

  if (MI.getOpcode() == ARM::MEMCPY) {
    expandMEMCPY(MI);
    return true;
  }

  // This hook gets to expand COPY instructions before they become
  // copyPhysReg() calls.  Look for VMOVS instructions that can legally be
  // widened to VMOVD.  We prefer the VMOVD when possible because it may be
  // changed into a VORR that can go down the NEON pipeline.
  if (!MI.isCopy() || Subtarget.dontWidenVMOVS() || !Subtarget.hasFP64())
    return false;

  // Look for a copy between even S-registers.  That is where we keep floats
  // when using NEON v2f32 instructions for f32 arithmetic.
  Register DstRegS = MI.getOperand(0).getReg();
  Register SrcRegS = MI.getOperand(1).getReg();
  if (!ARM::SPRRegClass.contains(DstRegS, SrcRegS))
    return false;

  const TargetRegisterInfo *TRI = &getRegisterInfo();
  unsigned DstRegD = TRI->getMatchingSuperReg(DstRegS, ARM::ssub_0,
                                              &ARM::DPRRegClass);
  unsigned SrcRegD = TRI->getMatchingSuperReg(SrcRegS, ARM::ssub_0,
                                              &ARM::DPRRegClass);
  if (!DstRegD || !SrcRegD)
    return false;

  // We want to widen this into a DstRegD = VMOVD SrcRegD copy.  This is only
  // legal if the COPY already defines the full DstRegD, and it isn't a
  // sub-register insertion.
  if (!MI.definesRegister(DstRegD, TRI) || MI.readsRegister(DstRegD, TRI))
    return false;

  // A dead copy shouldn't show up here, but reject it just in case.
  if (MI.getOperand(0).isDead())
    return false;

  // All clear, widen the COPY.
  LLVM_DEBUG(dbgs() << "widening:    " << MI);
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);

  // Get rid of the old implicit-def of DstRegD.  Leave it if it defines a Q-reg
  // or some other super-register.
  int ImpDefIdx = MI.findRegisterDefOperandIdx(DstRegD, /*TRI=*/nullptr);
  if (ImpDefIdx != -1)
    MI.removeOperand(ImpDefIdx);

  // Change the opcode and operands.
  MI.setDesc(get(ARM::VMOVD));
  MI.getOperand(0).setReg(DstRegD);
  MI.getOperand(1).setReg(SrcRegD);
  MIB.add(predOps(ARMCC::AL));

  // We are now reading SrcRegD instead of SrcRegS.  This may upset the
  // register scavenger and machine verifier, so we need to indicate that we
  // are reading an undefined value from SrcRegD, but a proper value from
  // SrcRegS.
  MI.getOperand(1).setIsUndef();
  MIB.addReg(SrcRegS, RegState::Implicit);

  // SrcRegD may actually contain an unrelated value in the ssub_1
  // sub-register.  Don't kill it.  Only kill the ssub_0 sub-register.
  if (MI.getOperand(1).isKill()) {
    MI.getOperand(1).setIsKill(false);
    MI.addRegisterKilled(SrcRegS, TRI, true);
  }

  LLVM_DEBUG(dbgs() << "replaced by: " << MI);
  return true;
}

/// Create a copy of a const pool value. Update CPI to the new index and return
/// the label UID.
static unsigned duplicateCPV(MachineFunction &MF, unsigned &CPI) {
  MachineConstantPool *MCP = MF.getConstantPool();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  const MachineConstantPoolEntry &MCPE = MCP->getConstants()[CPI];
  assert(MCPE.isMachineConstantPoolEntry() &&
         "Expecting a machine constantpool entry!");
  ARMConstantPoolValue *ACPV =
    static_cast<ARMConstantPoolValue*>(MCPE.Val.MachineCPVal);

  unsigned PCLabelId = AFI->createPICLabelUId();
  ARMConstantPoolValue *NewCPV = nullptr;

  // FIXME: The below assumes PIC relocation model and that the function
  // is Thumb mode (t1 or t2). PCAdjustment would be 8 for ARM mode PIC, and
  // zero for non-PIC in ARM or Thumb. The callers are all of thumb LDR
  // instructions, so that's probably OK, but is PIC always correct when
  // we get here?
  if (ACPV->isGlobalValue())
    NewCPV = ARMConstantPoolConstant::Create(
        cast<ARMConstantPoolConstant>(ACPV)->getGV(), PCLabelId, ARMCP::CPValue,
        4, ACPV->getModifier(), ACPV->mustAddCurrentAddress());
  else if (ACPV->isExtSymbol())
    NewCPV = ARMConstantPoolSymbol::
      Create(MF.getFunction().getContext(),
             cast<ARMConstantPoolSymbol>(ACPV)->getSymbol(), PCLabelId, 4);
  else if (ACPV->isBlockAddress())
    NewCPV = ARMConstantPoolConstant::
      Create(cast<ARMConstantPoolConstant>(ACPV)->getBlockAddress(), PCLabelId,
             ARMCP::CPBlockAddress, 4);
  else if (ACPV->isLSDA())
    NewCPV = ARMConstantPoolConstant::Create(&MF.getFunction(), PCLabelId,
                                             ARMCP::CPLSDA, 4);
  else if (ACPV->isMachineBasicBlock())
    NewCPV = ARMConstantPoolMBB::
      Create(MF.getFunction().getContext(),
             cast<ARMConstantPoolMBB>(ACPV)->getMBB(), PCLabelId, 4);
  else
    llvm_unreachable("Unexpected ARM constantpool value type!!");
  CPI = MCP->getConstantPoolIndex(NewCPV, MCPE.getAlign());
  return PCLabelId;
}

void ARMBaseInstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     Register DestReg, unsigned SubIdx,
                                     const MachineInstr &Orig,
                                     const TargetRegisterInfo &TRI) const {
  unsigned Opcode = Orig.getOpcode();
  switch (Opcode) {
  default: {
    MachineInstr *MI = MBB.getParent()->CloneMachineInstr(&Orig);
    MI->substituteRegister(Orig.getOperand(0).getReg(), DestReg, SubIdx, TRI);
    MBB.insert(I, MI);
    break;
  }
  case ARM::tLDRpci_pic:
  case ARM::t2LDRpci_pic: {
    MachineFunction &MF = *MBB.getParent();
    unsigned CPI = Orig.getOperand(1).getIndex();
    unsigned PCLabelId = duplicateCPV(MF, CPI);
    BuildMI(MBB, I, Orig.getDebugLoc(), get(Opcode), DestReg)
        .addConstantPoolIndex(CPI)
        .addImm(PCLabelId)
        .cloneMemRefs(Orig);
    break;
  }
  }
}

MachineInstr &
ARMBaseInstrInfo::duplicate(MachineBasicBlock &MBB,
    MachineBasicBlock::iterator InsertBefore,
    const MachineInstr &Orig) const {
  MachineInstr &Cloned = TargetInstrInfo::duplicate(MBB, InsertBefore, Orig);
  MachineBasicBlock::instr_iterator I = Cloned.getIterator();
  for (;;) {
    switch (I->getOpcode()) {
    case ARM::tLDRpci_pic:
    case ARM::t2LDRpci_pic: {
      MachineFunction &MF = *MBB.getParent();
      unsigned CPI = I->getOperand(1).getIndex();
      unsigned PCLabelId = duplicateCPV(MF, CPI);
      I->getOperand(1).setIndex(CPI);
      I->getOperand(2).setImm(PCLabelId);
      break;
    }
    }
    if (!I->isBundledWithSucc())
      break;
    ++I;
  }
  return Cloned;
}

bool ARMBaseInstrInfo::produceSameValue(const MachineInstr &MI0,
                                        const MachineInstr &MI1,
                                        const MachineRegisterInfo *MRI) const {
  unsigned Opcode = MI0.getOpcode();
  if (Opcode == ARM::t2LDRpci || Opcode == ARM::t2LDRpci_pic ||
      Opcode == ARM::tLDRpci || Opcode == ARM::tLDRpci_pic ||
      Opcode == ARM::LDRLIT_ga_pcrel || Opcode == ARM::LDRLIT_ga_pcrel_ldr ||
      Opcode == ARM::tLDRLIT_ga_pcrel || Opcode == ARM::t2LDRLIT_ga_pcrel ||
      Opcode == ARM::MOV_ga_pcrel || Opcode == ARM::MOV_ga_pcrel_ldr ||
      Opcode == ARM::t2MOV_ga_pcrel) {
    if (MI1.getOpcode() != Opcode)
      return false;
    if (MI0.getNumOperands() != MI1.getNumOperands())
      return false;

    const MachineOperand &MO0 = MI0.getOperand(1);
    const MachineOperand &MO1 = MI1.getOperand(1);
    if (MO0.getOffset() != MO1.getOffset())
      return false;

    if (Opcode == ARM::LDRLIT_ga_pcrel || Opcode == ARM::LDRLIT_ga_pcrel_ldr ||
        Opcode == ARM::tLDRLIT_ga_pcrel || Opcode == ARM::t2LDRLIT_ga_pcrel ||
        Opcode == ARM::MOV_ga_pcrel || Opcode == ARM::MOV_ga_pcrel_ldr ||
        Opcode == ARM::t2MOV_ga_pcrel)
      // Ignore the PC labels.
      return MO0.getGlobal() == MO1.getGlobal();

    const MachineFunction *MF = MI0.getParent()->getParent();
    const MachineConstantPool *MCP = MF->getConstantPool();
    int CPI0 = MO0.getIndex();
    int CPI1 = MO1.getIndex();
    const MachineConstantPoolEntry &MCPE0 = MCP->getConstants()[CPI0];
    const MachineConstantPoolEntry &MCPE1 = MCP->getConstants()[CPI1];
    bool isARMCP0 = MCPE0.isMachineConstantPoolEntry();
    bool isARMCP1 = MCPE1.isMachineConstantPoolEntry();
    if (isARMCP0 && isARMCP1) {
      ARMConstantPoolValue *ACPV0 =
        static_cast<ARMConstantPoolValue*>(MCPE0.Val.MachineCPVal);
      ARMConstantPoolValue *ACPV1 =
        static_cast<ARMConstantPoolValue*>(MCPE1.Val.MachineCPVal);
      return ACPV0->hasSameValue(ACPV1);
    } else if (!isARMCP0 && !isARMCP1) {
      return MCPE0.Val.ConstVal == MCPE1.Val.ConstVal;
    }
    return false;
  } else if (Opcode == ARM::PICLDR) {
    if (MI1.getOpcode() != Opcode)
      return false;
    if (MI0.getNumOperands() != MI1.getNumOperands())
      return false;

    Register Addr0 = MI0.getOperand(1).getReg();
    Register Addr1 = MI1.getOperand(1).getReg();
    if (Addr0 != Addr1) {
      if (!MRI || !Addr0.isVirtual() || !Addr1.isVirtual())
        return false;

      // This assumes SSA form.
      MachineInstr *Def0 = MRI->getVRegDef(Addr0);
      MachineInstr *Def1 = MRI->getVRegDef(Addr1);
      // Check if the loaded value, e.g. a constantpool of a global address, are
      // the same.
      if (!produceSameValue(*Def0, *Def1, MRI))
        return false;
    }

    for (unsigned i = 3, e = MI0.getNumOperands(); i != e; ++i) {
      // %12 = PICLDR %11, 0, 14, %noreg
      const MachineOperand &MO0 = MI0.getOperand(i);
      const MachineOperand &MO1 = MI1.getOperand(i);
      if (!MO0.isIdenticalTo(MO1))
        return false;
    }
    return true;
  }

  return MI0.isIdenticalTo(MI1, MachineInstr::IgnoreVRegDefs);
}

/// areLoadsFromSameBasePtr - This is used by the pre-regalloc scheduler to
/// determine if two loads are loading from the same base address. It should
/// only return true if the base pointers are the same and the only differences
/// between the two addresses is the offset. It also returns the offsets by
/// reference.
///
/// FIXME: remove this in favor of the MachineInstr interface once pre-RA-sched
/// is permanently disabled.
bool ARMBaseInstrInfo::areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2,
                                               int64_t &Offset1,
                                               int64_t &Offset2) const {
  // Don't worry about Thumb: just ARM and Thumb2.
  if (Subtarget.isThumb1Only()) return false;

  if (!Load1->isMachineOpcode() || !Load2->isMachineOpcode())
    return false;

  auto IsLoadOpcode = [&](unsigned Opcode) {
    switch (Opcode) {
    default:
      return false;
    case ARM::LDRi12:
    case ARM::LDRBi12:
    case ARM::LDRD:
    case ARM::LDRH:
    case ARM::LDRSB:
    case ARM::LDRSH:
    case ARM::VLDRD:
    case ARM::VLDRS:
    case ARM::t2LDRi8:
    case ARM::t2LDRBi8:
    case ARM::t2LDRDi8:
    case ARM::t2LDRSHi8:
    case ARM::t2LDRi12:
    case ARM::t2LDRBi12:
    case ARM::t2LDRSHi12:
      return true;
    }
  };

  if (!IsLoadOpcode(Load1->getMachineOpcode()) ||
      !IsLoadOpcode(Load2->getMachineOpcode()))
    return false;

  // Check if base addresses and chain operands match.
  if (Load1->getOperand(0) != Load2->getOperand(0) ||
      Load1->getOperand(4) != Load2->getOperand(4))
    return false;

  // Index should be Reg0.
  if (Load1->getOperand(3) != Load2->getOperand(3))
    return false;

  // Determine the offsets.
  if (isa<ConstantSDNode>(Load1->getOperand(1)) &&
      isa<ConstantSDNode>(Load2->getOperand(1))) {
    Offset1 = cast<ConstantSDNode>(Load1->getOperand(1))->getSExtValue();
    Offset2 = cast<ConstantSDNode>(Load2->getOperand(1))->getSExtValue();
    return true;
  }

  return false;
}

/// shouldScheduleLoadsNear - This is a used by the pre-regalloc scheduler to
/// determine (in conjunction with areLoadsFromSameBasePtr) if two loads should
/// be scheduled togther. On some targets if two loads are loading from
/// addresses in the same cache line, it's better if they are scheduled
/// together. This function takes two integers that represent the load offsets
/// from the common base address. It returns true if it decides it's desirable
/// to schedule the two loads together. "NumLoads" is the number of loads that
/// have already been scheduled after Load1.
///
/// FIXME: remove this in favor of the MachineInstr interface once pre-RA-sched
/// is permanently disabled.
bool ARMBaseInstrInfo::shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2,
                                               int64_t Offset1, int64_t Offset2,
                                               unsigned NumLoads) const {
  // Don't worry about Thumb: just ARM and Thumb2.
  if (Subtarget.isThumb1Only()) return false;

  assert(Offset2 > Offset1);

  if ((Offset2 - Offset1) / 8 > 64)
    return false;

  // Check if the machine opcodes are different. If they are different
  // then we consider them to not be of the same base address,
  // EXCEPT in the case of Thumb2 byte loads where one is LDRBi8 and the other LDRBi12.
  // In this case, they are considered to be the same because they are different
  // encoding forms of the same basic instruction.
  if ((Load1->getMachineOpcode() != Load2->getMachineOpcode()) &&
      !((Load1->getMachineOpcode() == ARM::t2LDRBi8 &&
         Load2->getMachineOpcode() == ARM::t2LDRBi12) ||
        (Load1->getMachineOpcode() == ARM::t2LDRBi12 &&
         Load2->getMachineOpcode() == ARM::t2LDRBi8)))
    return false;  // FIXME: overly conservative?

  // Four loads in a row should be sufficient.
  if (NumLoads >= 3)
    return false;

  return true;
}

bool ARMBaseInstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                            const MachineBasicBlock *MBB,
                                            const MachineFunction &MF) const {
  // Debug info is never a scheduling boundary. It's necessary to be explicit
  // due to the special treatment of IT instructions below, otherwise a
  // dbg_value followed by an IT will result in the IT instruction being
  // considered a scheduling hazard, which is wrong. It should be the actual
  // instruction preceding the dbg_value instruction(s), just like it is
  // when debug info is not present.
  if (MI.isDebugInstr())
    return false;

  // Terminators and labels can't be scheduled around.
  if (MI.isTerminator() || MI.isPosition())
    return true;

  // INLINEASM_BR can jump to another block
  if (MI.getOpcode() == TargetOpcode::INLINEASM_BR)
    return true;

  if (isSEHInstruction(MI))
    return true;

  // Treat the start of the IT block as a scheduling boundary, but schedule
  // t2IT along with all instructions following it.
  // FIXME: This is a big hammer. But the alternative is to add all potential
  // true and anti dependencies to IT block instructions as implicit operands
  // to the t2IT instruction. The added compile time and complexity does not
  // seem worth it.
  MachineBasicBlock::const_iterator I = MI;
  // Make sure to skip any debug instructions
  while (++I != MBB->end() && I->isDebugInstr())
    ;
  if (I != MBB->end() && I->getOpcode() == ARM::t2IT)
    return true;

  // Don't attempt to schedule around any instruction that defines
  // a stack-oriented pointer, as it's unlikely to be profitable. This
  // saves compile time, because it doesn't require every single
  // stack slot reference to depend on the instruction that does the
  // modification.
  // Calls don't actually change the stack pointer, even if they have imp-defs.
  // No ARM calling conventions change the stack pointer. (X86 calling
  // conventions sometimes do).
  if (!MI.isCall() && MI.definesRegister(ARM::SP, /*TRI=*/nullptr))
    return true;

  return false;
}

bool ARMBaseInstrInfo::
isProfitableToIfCvt(MachineBasicBlock &MBB,
                    unsigned NumCycles, unsigned ExtraPredCycles,
                    BranchProbability Probability) const {
  if (!NumCycles)
    return false;

  // If we are optimizing for size, see if the branch in the predecessor can be
  // lowered to cbn?z by the constant island lowering pass, and return false if
  // so. This results in a shorter instruction sequence.
  if (MBB.getParent()->getFunction().hasOptSize()) {
    MachineBasicBlock *Pred = *MBB.pred_begin();
    if (!Pred->empty()) {
      MachineInstr *LastMI = &*Pred->rbegin();
      if (LastMI->getOpcode() == ARM::t2Bcc) {
        const TargetRegisterInfo *TRI = &getRegisterInfo();
        MachineInstr *CmpMI = findCMPToFoldIntoCBZ(LastMI, TRI);
        if (CmpMI)
          return false;
      }
    }
  }
  return isProfitableToIfCvt(MBB, NumCycles, ExtraPredCycles,
                             MBB, 0, 0, Probability);
}

bool ARMBaseInstrInfo::
isProfitableToIfCvt(MachineBasicBlock &TBB,
                    unsigned TCycles, unsigned TExtra,
                    MachineBasicBlock &FBB,
                    unsigned FCycles, unsigned FExtra,
                    BranchProbability Probability) const {
  if (!TCycles)
    return false;

  // In thumb code we often end up trading one branch for a IT block, and
  // if we are cloning the instruction can increase code size. Prevent
  // blocks with multiple predecesors from being ifcvted to prevent this
  // cloning.
  if (Subtarget.isThumb2() && TBB.getParent()->getFunction().hasMinSize()) {
    if (TBB.pred_size() != 1 || FBB.pred_size() != 1)
      return false;
  }

  // Attempt to estimate the relative costs of predication versus branching.
  // Here we scale up each component of UnpredCost to avoid precision issue when
  // scaling TCycles/FCycles by Probability.
  const unsigned ScalingUpFactor = 1024;

  unsigned PredCost = (TCycles + FCycles + TExtra + FExtra) * ScalingUpFactor;
  unsigned UnpredCost;
  if (!Subtarget.hasBranchPredictor()) {
    // When we don't have a branch predictor it's always cheaper to not take a
    // branch than take it, so we have to take that into account.
    unsigned NotTakenBranchCost = 1;
    unsigned TakenBranchCost = Subtarget.getMispredictionPenalty();
    unsigned TUnpredCycles, FUnpredCycles;
    if (!FCycles) {
      // Triangle: TBB is the fallthrough
      TUnpredCycles = TCycles + NotTakenBranchCost;
      FUnpredCycles = TakenBranchCost;
    } else {
      // Diamond: TBB is the block that is branched to, FBB is the fallthrough
      TUnpredCycles = TCycles + TakenBranchCost;
      FUnpredCycles = FCycles + NotTakenBranchCost;
      // The branch at the end of FBB will disappear when it's predicated, so
      // discount it from PredCost.
      PredCost -= 1 * ScalingUpFactor;
    }
    // The total cost is the cost of each path scaled by their probabilites
    unsigned TUnpredCost = Probability.scale(TUnpredCycles * ScalingUpFactor);
    unsigned FUnpredCost = Probability.getCompl().scale(FUnpredCycles * ScalingUpFactor);
    UnpredCost = TUnpredCost + FUnpredCost;
    // When predicating assume that the first IT can be folded away but later
    // ones cost one cycle each
    if (Subtarget.isThumb2() && TCycles + FCycles > 4) {
      PredCost += ((TCycles + FCycles - 4) / 4) * ScalingUpFactor;
    }
  } else {
    unsigned TUnpredCost = Probability.scale(TCycles * ScalingUpFactor);
    unsigned FUnpredCost =
      Probability.getCompl().scale(FCycles * ScalingUpFactor);
    UnpredCost = TUnpredCost + FUnpredCost;
    UnpredCost += 1 * ScalingUpFactor; // The branch itself
    UnpredCost += Subtarget.getMispredictionPenalty() * ScalingUpFactor / 10;
  }

  return PredCost <= UnpredCost;
}

unsigned
ARMBaseInstrInfo::extraSizeToPredicateInstructions(const MachineFunction &MF,
                                                   unsigned NumInsts) const {
  // Thumb2 needs a 2-byte IT instruction to predicate up to 4 instructions.
  // ARM has a condition code field in every predicable instruction, using it
  // doesn't change code size.
  if (!Subtarget.isThumb2())
    return 0;

  // It's possible that the size of the IT is restricted to a single block.
  unsigned MaxInsts = Subtarget.restrictIT() ? 1 : 4;
  return divideCeil(NumInsts, MaxInsts) * 2;
}

unsigned
ARMBaseInstrInfo::predictBranchSizeForIfCvt(MachineInstr &MI) const {
  // If this branch is likely to be folded into the comparison to form a
  // CB(N)Z, then removing it won't reduce code size at all, because that will
  // just replace the CB(N)Z with a CMP.
  if (MI.getOpcode() == ARM::t2Bcc &&
      findCMPToFoldIntoCBZ(&MI, &getRegisterInfo()))
    return 0;

  unsigned Size = getInstSizeInBytes(MI);

  // For Thumb2, all branches are 32-bit instructions during the if conversion
  // pass, but may be replaced with 16-bit instructions during size reduction.
  // Since the branches considered by if conversion tend to be forward branches
  // over small basic blocks, they are very likely to be in range for the
  // narrow instructions, so we assume the final code size will be half what it
  // currently is.
  if (Subtarget.isThumb2())
    Size /= 2;

  return Size;
}

bool
ARMBaseInstrInfo::isProfitableToUnpredicate(MachineBasicBlock &TMBB,
                                            MachineBasicBlock &FMBB) const {
  // Reduce false anti-dependencies to let the target's out-of-order execution
  // engine do its thing.
  return Subtarget.isProfitableToUnpredicate();
}

/// getInstrPredicate - If instruction is predicated, returns its predicate
/// condition, otherwise returns AL. It also returns the condition code
/// register by reference.
ARMCC::CondCodes llvm::getInstrPredicate(const MachineInstr &MI,
                                         Register &PredReg) {
  int PIdx = MI.findFirstPredOperandIdx();
  if (PIdx == -1) {
    PredReg = 0;
    return ARMCC::AL;
  }

  PredReg = MI.getOperand(PIdx+1).getReg();
  return (ARMCC::CondCodes)MI.getOperand(PIdx).getImm();
}

unsigned llvm::getMatchingCondBranchOpcode(unsigned Opc) {
  if (Opc == ARM::B)
    return ARM::Bcc;
  if (Opc == ARM::tB)
    return ARM::tBcc;
  if (Opc == ARM::t2B)
    return ARM::t2Bcc;

  llvm_unreachable("Unknown unconditional branch opcode!");
}

MachineInstr *ARMBaseInstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                       bool NewMI,
                                                       unsigned OpIdx1,
                                                       unsigned OpIdx2) const {
  switch (MI.getOpcode()) {
  case ARM::MOVCCr:
  case ARM::t2MOVCCr: {
    // MOVCC can be commuted by inverting the condition.
    Register PredReg;
    ARMCC::CondCodes CC = getInstrPredicate(MI, PredReg);
    // MOVCC AL can't be inverted. Shouldn't happen.
    if (CC == ARMCC::AL || PredReg != ARM::CPSR)
      return nullptr;
    MachineInstr *CommutedMI =
        TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
    if (!CommutedMI)
      return nullptr;
    // After swapping the MOVCC operands, also invert the condition.
    CommutedMI->getOperand(CommutedMI->findFirstPredOperandIdx())
        .setImm(ARMCC::getOppositeCondition(CC));
    return CommutedMI;
  }
  }
  return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

/// Identify instructions that can be folded into a MOVCC instruction, and
/// return the defining instruction.
MachineInstr *
ARMBaseInstrInfo::canFoldIntoMOVCC(Register Reg, const MachineRegisterInfo &MRI,
                                   const TargetInstrInfo *TII) const {
  if (!Reg.isVirtual())
    return nullptr;
  if (!MRI.hasOneNonDBGUse(Reg))
    return nullptr;
  MachineInstr *MI = MRI.getVRegDef(Reg);
  if (!MI)
    return nullptr;
  // Check if MI can be predicated and folded into the MOVCC.
  if (!isPredicable(*MI))
    return nullptr;
  // Check if MI has any non-dead defs or physreg uses. This also detects
  // predicated instructions which will be reading CPSR.
  for (const MachineOperand &MO : llvm::drop_begin(MI->operands(), 1)) {
    // Reject frame index operands, PEI can't handle the predicated pseudos.
    if (MO.isFI() || MO.isCPI() || MO.isJTI())
      return nullptr;
    if (!MO.isReg())
      continue;
    // MI can't have any tied operands, that would conflict with predication.
    if (MO.isTied())
      return nullptr;
    if (MO.getReg().isPhysical())
      return nullptr;
    if (MO.isDef() && !MO.isDead())
      return nullptr;
  }
  bool DontMoveAcrossStores = true;
  if (!MI->isSafeToMove(/* AliasAnalysis = */ nullptr, DontMoveAcrossStores))
    return nullptr;
  return MI;
}

bool ARMBaseInstrInfo::analyzeSelect(const MachineInstr &MI,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     unsigned &TrueOp, unsigned &FalseOp,
                                     bool &Optimizable) const {
  assert((MI.getOpcode() == ARM::MOVCCr || MI.getOpcode() == ARM::t2MOVCCr) &&
         "Unknown select instruction");
  // MOVCC operands:
  // 0: Def.
  // 1: True use.
  // 2: False use.
  // 3: Condition code.
  // 4: CPSR use.
  TrueOp = 1;
  FalseOp = 2;
  Cond.push_back(MI.getOperand(3));
  Cond.push_back(MI.getOperand(4));
  // We can always fold a def.
  Optimizable = true;
  return false;
}

MachineInstr *
ARMBaseInstrInfo::optimizeSelect(MachineInstr &MI,
                                 SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                                 bool PreferFalse) const {
  assert((MI.getOpcode() == ARM::MOVCCr || MI.getOpcode() == ARM::t2MOVCCr) &&
         "Unknown select instruction");
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  MachineInstr *DefMI = canFoldIntoMOVCC(MI.getOperand(2).getReg(), MRI, this);
  bool Invert = !DefMI;
  if (!DefMI)
    DefMI = canFoldIntoMOVCC(MI.getOperand(1).getReg(), MRI, this);
  if (!DefMI)
    return nullptr;

  // Find new register class to use.
  MachineOperand FalseReg = MI.getOperand(Invert ? 2 : 1);
  MachineOperand TrueReg = MI.getOperand(Invert ? 1 : 2);
  Register DestReg = MI.getOperand(0).getReg();
  const TargetRegisterClass *FalseClass = MRI.getRegClass(FalseReg.getReg());
  const TargetRegisterClass *TrueClass = MRI.getRegClass(TrueReg.getReg());
  if (!MRI.constrainRegClass(DestReg, FalseClass))
    return nullptr;
  if (!MRI.constrainRegClass(DestReg, TrueClass))
    return nullptr;

  // Create a new predicated version of DefMI.
  // Rfalse is the first use.
  MachineInstrBuilder NewMI =
      BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), DefMI->getDesc(), DestReg);

  // Copy all the DefMI operands, excluding its (null) predicate.
  const MCInstrDesc &DefDesc = DefMI->getDesc();
  for (unsigned i = 1, e = DefDesc.getNumOperands();
       i != e && !DefDesc.operands()[i].isPredicate(); ++i)
    NewMI.add(DefMI->getOperand(i));

  unsigned CondCode = MI.getOperand(3).getImm();
  if (Invert)
    NewMI.addImm(ARMCC::getOppositeCondition(ARMCC::CondCodes(CondCode)));
  else
    NewMI.addImm(CondCode);
  NewMI.add(MI.getOperand(4));

  // DefMI is not the -S version that sets CPSR, so add an optional %noreg.
  if (NewMI->hasOptionalDef())
    NewMI.add(condCodeOp());

  // The output register value when the predicate is false is an implicit
  // register operand tied to the first def.
  // The tie makes the register allocator ensure the FalseReg is allocated the
  // same register as operand 0.
  FalseReg.setImplicit();
  NewMI.add(FalseReg);
  NewMI->tieOperands(0, NewMI->getNumOperands() - 1);

  // Update SeenMIs set: register newly created MI and erase removed DefMI.
  SeenMIs.insert(NewMI);
  SeenMIs.erase(DefMI);

  // If MI is inside a loop, and DefMI is outside the loop, then kill flags on
  // DefMI would be invalid when tranferred inside the loop.  Checking for a
  // loop is expensive, but at least remove kill flags if they are in different
  // BBs.
  if (DefMI->getParent() != MI.getParent())
    NewMI->clearKillInfo();

  // The caller will erase MI, but not DefMI.
  DefMI->eraseFromParent();
  return NewMI;
}

/// Map pseudo instructions that imply an 'S' bit onto real opcodes. Whether the
/// instruction is encoded with an 'S' bit is determined by the optional CPSR
/// def operand.
///
/// This will go away once we can teach tblgen how to set the optional CPSR def
/// operand itself.
struct AddSubFlagsOpcodePair {
  uint16_t PseudoOpc;
  uint16_t MachineOpc;
};

static const AddSubFlagsOpcodePair AddSubFlagsOpcodeMap[] = {
  {ARM::ADDSri, ARM::ADDri},
  {ARM::ADDSrr, ARM::ADDrr},
  {ARM::ADDSrsi, ARM::ADDrsi},
  {ARM::ADDSrsr, ARM::ADDrsr},

  {ARM::SUBSri, ARM::SUBri},
  {ARM::SUBSrr, ARM::SUBrr},
  {ARM::SUBSrsi, ARM::SUBrsi},
  {ARM::SUBSrsr, ARM::SUBrsr},

  {ARM::RSBSri, ARM::RSBri},
  {ARM::RSBSrsi, ARM::RSBrsi},
  {ARM::RSBSrsr, ARM::RSBrsr},

  {ARM::tADDSi3, ARM::tADDi3},
  {ARM::tADDSi8, ARM::tADDi8},
  {ARM::tADDSrr, ARM::tADDrr},
  {ARM::tADCS, ARM::tADC},

  {ARM::tSUBSi3, ARM::tSUBi3},
  {ARM::tSUBSi8, ARM::tSUBi8},
  {ARM::tSUBSrr, ARM::tSUBrr},
  {ARM::tSBCS, ARM::tSBC},
  {ARM::tRSBS, ARM::tRSB},
  {ARM::tLSLSri, ARM::tLSLri},

  {ARM::t2ADDSri, ARM::t2ADDri},
  {ARM::t2ADDSrr, ARM::t2ADDrr},
  {ARM::t2ADDSrs, ARM::t2ADDrs},

  {ARM::t2SUBSri, ARM::t2SUBri},
  {ARM::t2SUBSrr, ARM::t2SUBrr},
  {ARM::t2SUBSrs, ARM::t2SUBrs},

  {ARM::t2RSBSri, ARM::t2RSBri},
  {ARM::t2RSBSrs, ARM::t2RSBrs},
};

unsigned llvm::convertAddSubFlagsOpcode(unsigned OldOpc) {
  for (const auto &Entry : AddSubFlagsOpcodeMap)
    if (OldOpc == Entry.PseudoOpc)
      return Entry.MachineOpc;
  return 0;
}

void llvm::emitARMRegPlusImmediate(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator &MBBI,
                                   const DebugLoc &dl, Register DestReg,
                                   Register BaseReg, int NumBytes,
                                   ARMCC::CondCodes Pred, Register PredReg,
                                   const ARMBaseInstrInfo &TII,
                                   unsigned MIFlags) {
  if (NumBytes == 0 && DestReg != BaseReg) {
    BuildMI(MBB, MBBI, dl, TII.get(ARM::MOVr), DestReg)
        .addReg(BaseReg, RegState::Kill)
        .add(predOps(Pred, PredReg))
        .add(condCodeOp())
        .setMIFlags(MIFlags);
    return;
  }

  bool isSub = NumBytes < 0;
  if (isSub) NumBytes = -NumBytes;

  while (NumBytes) {
    unsigned RotAmt = ARM_AM::getSOImmValRotate(NumBytes);
    unsigned ThisVal = NumBytes & llvm::rotr<uint32_t>(0xFF, RotAmt);
    assert(ThisVal && "Didn't extract field correctly");

    // We will handle these bits from offset, clear them.
    NumBytes &= ~ThisVal;

    assert(ARM_AM::getSOImmVal(ThisVal) != -1 && "Bit extraction didn't work?");

    // Build the new ADD / SUB.
    unsigned Opc = isSub ? ARM::SUBri : ARM::ADDri;
    BuildMI(MBB, MBBI, dl, TII.get(Opc), DestReg)
        .addReg(BaseReg, RegState::Kill)
        .addImm(ThisVal)
        .add(predOps(Pred, PredReg))
        .add(condCodeOp())
        .setMIFlags(MIFlags);
    BaseReg = DestReg;
  }
}

bool llvm::tryFoldSPUpdateIntoPushPop(const ARMSubtarget &Subtarget,
                                      MachineFunction &MF, MachineInstr *MI,
                                      unsigned NumBytes) {
  // This optimisation potentially adds lots of load and store
  // micro-operations, it's only really a great benefit to code-size.
  if (!Subtarget.hasMinSize())
    return false;

  // If only one register is pushed/popped, LLVM can use an LDR/STR
  // instead. We can't modify those so make sure we're dealing with an
  // instruction we understand.
  bool IsPop = isPopOpcode(MI->getOpcode());
  bool IsPush = isPushOpcode(MI->getOpcode());
  if (!IsPush && !IsPop)
    return false;

  bool IsVFPPushPop = MI->getOpcode() == ARM::VSTMDDB_UPD ||
                      MI->getOpcode() == ARM::VLDMDIA_UPD;
  bool IsT1PushPop = MI->getOpcode() == ARM::tPUSH ||
                     MI->getOpcode() == ARM::tPOP ||
                     MI->getOpcode() == ARM::tPOP_RET;

  assert((IsT1PushPop || (MI->getOperand(0).getReg() == ARM::SP &&
                          MI->getOperand(1).getReg() == ARM::SP)) &&
         "trying to fold sp update into non-sp-updating push/pop");

  // The VFP push & pop act on D-registers, so we can only fold an adjustment
  // by a multiple of 8 bytes in correctly. Similarly rN is 4-bytes. Don't try
  // if this is violated.
  if (NumBytes % (IsVFPPushPop ? 8 : 4) != 0)
    return false;

  // ARM and Thumb2 push/pop insts have explicit "sp, sp" operands (+
  // pred) so the list starts at 4. Thumb1 starts after the predicate.
  int RegListIdx = IsT1PushPop ? 2 : 4;

  // Calculate the space we'll need in terms of registers.
  unsigned RegsNeeded;
  const TargetRegisterClass *RegClass;
  if (IsVFPPushPop) {
    RegsNeeded = NumBytes / 8;
    RegClass = &ARM::DPRRegClass;
  } else {
    RegsNeeded = NumBytes / 4;
    RegClass = &ARM::GPRRegClass;
  }

  // We're going to have to strip all list operands off before
  // re-adding them since the order matters, so save the existing ones
  // for later.
  SmallVector<MachineOperand, 4> RegList;

  // We're also going to need the first register transferred by this
  // instruction, which won't necessarily be the first register in the list.
  unsigned FirstRegEnc = -1;

  const TargetRegisterInfo *TRI = MF.getRegInfo().getTargetRegisterInfo();
  for (int i = MI->getNumOperands() - 1; i >= RegListIdx; --i) {
    MachineOperand &MO = MI->getOperand(i);
    RegList.push_back(MO);

    if (MO.isReg() && !MO.isImplicit() &&
        TRI->getEncodingValue(MO.getReg()) < FirstRegEnc)
      FirstRegEnc = TRI->getEncodingValue(MO.getReg());
  }

  const MCPhysReg *CSRegs = TRI->getCalleeSavedRegs(&MF);

  // Now try to find enough space in the reglist to allocate NumBytes.
  for (int CurRegEnc = FirstRegEnc - 1; CurRegEnc >= 0 && RegsNeeded;
       --CurRegEnc) {
    unsigned CurReg = RegClass->getRegister(CurRegEnc);
    if (IsT1PushPop && CurRegEnc > TRI->getEncodingValue(ARM::R7))
      continue;
    if (!IsPop) {
      // Pushing any register is completely harmless, mark the register involved
      // as undef since we don't care about its value and must not restore it
      // during stack unwinding.
      RegList.push_back(MachineOperand::CreateReg(CurReg, false, false,
                                                  false, false, true));
      --RegsNeeded;
      continue;
    }

    // However, we can only pop an extra register if it's not live. For
    // registers live within the function we might clobber a return value
    // register; the other way a register can be live here is if it's
    // callee-saved.
    if (isCalleeSavedRegister(CurReg, CSRegs) ||
        MI->getParent()->computeRegisterLiveness(TRI, CurReg, MI) !=
        MachineBasicBlock::LQR_Dead) {
      // VFP pops don't allow holes in the register list, so any skip is fatal
      // for our transformation. GPR pops do, so we should just keep looking.
      if (IsVFPPushPop)
        return false;
      else
        continue;
    }

    // Mark the unimportant registers as <def,dead> in the POP.
    RegList.push_back(MachineOperand::CreateReg(CurReg, true, false, false,
                                                true));
    --RegsNeeded;
  }

  if (RegsNeeded > 0)
    return false;

  // Finally we know we can profitably perform the optimisation so go
  // ahead: strip all existing registers off and add them back again
  // in the right order.
  for (int i = MI->getNumOperands() - 1; i >= RegListIdx; --i)
    MI->removeOperand(i);

  // Add the complete list back in.
  MachineInstrBuilder MIB(MF, &*MI);
  for (const MachineOperand &MO : llvm::reverse(RegList))
    MIB.add(MO);

  return true;
}

bool llvm::rewriteARMFrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                                Register FrameReg, int &Offset,
                                const ARMBaseInstrInfo &TII) {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MI.getDesc();
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);
  bool isSub = false;

  // Memory operands in inline assembly always use AddrMode2.
  if (Opcode == ARM::INLINEASM || Opcode == ARM::INLINEASM_BR)
    AddrMode = ARMII::AddrMode2;

  if (Opcode == ARM::ADDri) {
    Offset += MI.getOperand(FrameRegIdx+1).getImm();
    if (Offset == 0) {
      // Turn it into a move.
      MI.setDesc(TII.get(ARM::MOVr));
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      MI.removeOperand(FrameRegIdx+1);
      Offset = 0;
      return true;
    } else if (Offset < 0) {
      Offset = -Offset;
      isSub = true;
      MI.setDesc(TII.get(ARM::SUBri));
    }

    // Common case: small offset, fits into instruction.
    if (ARM_AM::getSOImmVal(Offset) != -1) {
      // Replace the FrameIndex with sp / fp
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      MI.getOperand(FrameRegIdx+1).ChangeToImmediate(Offset);
      Offset = 0;
      return true;
    }

    // Otherwise, pull as much of the immedidate into this ADDri/SUBri
    // as possible.
    unsigned RotAmt = ARM_AM::getSOImmValRotate(Offset);
    unsigned ThisImmVal = Offset & llvm::rotr<uint32_t>(0xFF, RotAmt);

    // We will handle these bits from offset, clear them.
    Offset &= ~ThisImmVal;

    // Get the properly encoded SOImmVal field.
    assert(ARM_AM::getSOImmVal(ThisImmVal) != -1 &&
           "Bit extraction didn't work?");
    MI.getOperand(FrameRegIdx+1).ChangeToImmediate(ThisImmVal);
 } else {
    unsigned ImmIdx = 0;
    int InstrOffs = 0;
    unsigned NumBits = 0;
    unsigned Scale = 1;
    switch (AddrMode) {
    case ARMII::AddrMode_i12:
      ImmIdx = FrameRegIdx + 1;
      InstrOffs = MI.getOperand(ImmIdx).getImm();
      NumBits = 12;
      break;
    case ARMII::AddrMode2:
      ImmIdx = FrameRegIdx+2;
      InstrOffs = ARM_AM::getAM2Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM2Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 12;
      break;
    case ARMII::AddrMode3:
      ImmIdx = FrameRegIdx+2;
      InstrOffs = ARM_AM::getAM3Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM3Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      break;
    case ARMII::AddrMode4:
    case ARMII::AddrMode6:
      // Can't fold any offset even if it's zero.
      return false;
    case ARMII::AddrMode5:
      ImmIdx = FrameRegIdx+1;
      InstrOffs = ARM_AM::getAM5Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM5Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      Scale = 4;
      break;
    case ARMII::AddrMode5FP16:
      ImmIdx = FrameRegIdx+1;
      InstrOffs = ARM_AM::getAM5Offset(MI.getOperand(ImmIdx).getImm());
      if (ARM_AM::getAM5Op(MI.getOperand(ImmIdx).getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      Scale = 2;
      break;
    case ARMII::AddrModeT2_i7:
    case ARMII::AddrModeT2_i7s2:
    case ARMII::AddrModeT2_i7s4:
      ImmIdx = FrameRegIdx+1;
      InstrOffs = MI.getOperand(ImmIdx).getImm();
      NumBits = 7;
      Scale = (AddrMode == ARMII::AddrModeT2_i7s2 ? 2 :
               AddrMode == ARMII::AddrModeT2_i7s4 ? 4 : 1);
      break;
    default:
      llvm_unreachable("Unsupported addressing mode!");
    }

    Offset += InstrOffs * Scale;
    assert((Offset & (Scale-1)) == 0 && "Can't encode this offset!");
    if (Offset < 0) {
      Offset = -Offset;
      isSub = true;
    }

    // Attempt to fold address comp. if opcode has offset bits
    if (NumBits > 0) {
      // Common case: small offset, fits into instruction.
      MachineOperand &ImmOp = MI.getOperand(ImmIdx);
      int ImmedOffset = Offset / Scale;
      unsigned Mask = (1 << NumBits) - 1;
      if ((unsigned)Offset <= Mask * Scale) {
        // Replace the FrameIndex with sp
        MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
        // FIXME: When addrmode2 goes away, this will simplify (like the
        // T2 version), as the LDR.i12 versions don't need the encoding
        // tricks for the offset value.
        if (isSub) {
          if (AddrMode == ARMII::AddrMode_i12)
            ImmedOffset = -ImmedOffset;
          else
            ImmedOffset |= 1 << NumBits;
        }
        ImmOp.ChangeToImmediate(ImmedOffset);
        Offset = 0;
        return true;
      }

      // Otherwise, it didn't fit. Pull in what we can to simplify the immed.
      ImmedOffset = ImmedOffset & Mask;
      if (isSub) {
        if (AddrMode == ARMII::AddrMode_i12)
          ImmedOffset = -ImmedOffset;
        else
          ImmedOffset |= 1 << NumBits;
      }
      ImmOp.ChangeToImmediate(ImmedOffset);
      Offset &= ~(Mask*Scale);
    }
  }

  Offset = (isSub) ? -Offset : Offset;
  return Offset == 0;
}

/// analyzeCompare - For a comparison instruction, return the source registers
/// in SrcReg and SrcReg2 if having two register operands, and the value it
/// compares against in CmpValue. Return true if the comparison instruction
/// can be analyzed.
bool ARMBaseInstrInfo::analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                                      Register &SrcReg2, int64_t &CmpMask,
                                      int64_t &CmpValue) const {
  switch (MI.getOpcode()) {
  default: break;
  case ARM::CMPri:
  case ARM::t2CMPri:
  case ARM::tCMPi8:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = 0;
    CmpMask = ~0;
    CmpValue = MI.getOperand(1).getImm();
    return true;
  case ARM::CMPrr:
  case ARM::t2CMPrr:
  case ARM::tCMPr:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = MI.getOperand(1).getReg();
    CmpMask = ~0;
    CmpValue = 0;
    return true;
  case ARM::TSTri:
  case ARM::t2TSTri:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = 0;
    CmpMask = MI.getOperand(1).getImm();
    CmpValue = 0;
    return true;
  }

  return false;
}

/// isSuitableForMask - Identify a suitable 'and' instruction that
/// operates on the given source register and applies the same mask
/// as a 'tst' instruction. Provide a limited look-through for copies.
/// When successful, MI will hold the found instruction.
static bool isSuitableForMask(MachineInstr *&MI, Register SrcReg,
                              int CmpMask, bool CommonUse) {
  switch (MI->getOpcode()) {
    case ARM::ANDri:
    case ARM::t2ANDri:
      if (CmpMask != MI->getOperand(2).getImm())
        return false;
      if (SrcReg == MI->getOperand(CommonUse ? 1 : 0).getReg())
        return true;
      break;
  }

  return false;
}

/// getCmpToAddCondition - assume the flags are set by CMP(a,b), return
/// the condition code if we modify the instructions such that flags are
/// set by ADD(a,b,X).
inline static ARMCC::CondCodes getCmpToAddCondition(ARMCC::CondCodes CC) {
  switch (CC) {
  default: return ARMCC::AL;
  case ARMCC::HS: return ARMCC::LO;
  case ARMCC::LO: return ARMCC::HS;
  case ARMCC::VS: return ARMCC::VS;
  case ARMCC::VC: return ARMCC::VC;
  }
}

/// isRedundantFlagInstr - check whether the first instruction, whose only
/// purpose is to update flags, can be made redundant.
/// CMPrr can be made redundant by SUBrr if the operands are the same.
/// CMPri can be made redundant by SUBri if the operands are the same.
/// CMPrr(r0, r1) can be made redundant by ADDr[ri](r0, r1, X).
/// This function can be extended later on.
inline static bool isRedundantFlagInstr(const MachineInstr *CmpI,
                                        Register SrcReg, Register SrcReg2,
                                        int64_t ImmValue,
                                        const MachineInstr *OI,
                                        bool &IsThumb1) {
  if ((CmpI->getOpcode() == ARM::CMPrr || CmpI->getOpcode() == ARM::t2CMPrr) &&
      (OI->getOpcode() == ARM::SUBrr || OI->getOpcode() == ARM::t2SUBrr) &&
      ((OI->getOperand(1).getReg() == SrcReg &&
        OI->getOperand(2).getReg() == SrcReg2) ||
       (OI->getOperand(1).getReg() == SrcReg2 &&
        OI->getOperand(2).getReg() == SrcReg))) {
    IsThumb1 = false;
    return true;
  }

  if (CmpI->getOpcode() == ARM::tCMPr && OI->getOpcode() == ARM::tSUBrr &&
      ((OI->getOperand(2).getReg() == SrcReg &&
        OI->getOperand(3).getReg() == SrcReg2) ||
       (OI->getOperand(2).getReg() == SrcReg2 &&
        OI->getOperand(3).getReg() == SrcReg))) {
    IsThumb1 = true;
    return true;
  }

  if ((CmpI->getOpcode() == ARM::CMPri || CmpI->getOpcode() == ARM::t2CMPri) &&
      (OI->getOpcode() == ARM::SUBri || OI->getOpcode() == ARM::t2SUBri) &&
      OI->getOperand(1).getReg() == SrcReg &&
      OI->getOperand(2).getImm() == ImmValue) {
    IsThumb1 = false;
    return true;
  }

  if (CmpI->getOpcode() == ARM::tCMPi8 &&
      (OI->getOpcode() == ARM::tSUBi8 || OI->getOpcode() == ARM::tSUBi3) &&
      OI->getOperand(2).getReg() == SrcReg &&
      OI->getOperand(3).getImm() == ImmValue) {
    IsThumb1 = true;
    return true;
  }

  if ((CmpI->getOpcode() == ARM::CMPrr || CmpI->getOpcode() == ARM::t2CMPrr) &&
      (OI->getOpcode() == ARM::ADDrr || OI->getOpcode() == ARM::t2ADDrr ||
       OI->getOpcode() == ARM::ADDri || OI->getOpcode() == ARM::t2ADDri) &&
      OI->getOperand(0).isReg() && OI->getOperand(1).isReg() &&
      OI->getOperand(0).getReg() == SrcReg &&
      OI->getOperand(1).getReg() == SrcReg2) {
    IsThumb1 = false;
    return true;
  }

  if (CmpI->getOpcode() == ARM::tCMPr &&
      (OI->getOpcode() == ARM::tADDi3 || OI->getOpcode() == ARM::tADDi8 ||
       OI->getOpcode() == ARM::tADDrr) &&
      OI->getOperand(0).getReg() == SrcReg &&
      OI->getOperand(2).getReg() == SrcReg2) {
    IsThumb1 = true;
    return true;
  }

  return false;
}

static bool isOptimizeCompareCandidate(MachineInstr *MI, bool &IsThumb1) {
  switch (MI->getOpcode()) {
  default: return false;
  case ARM::tLSLri:
  case ARM::tLSRri:
  case ARM::tLSLrr:
  case ARM::tLSRrr:
  case ARM::tSUBrr:
  case ARM::tADDrr:
  case ARM::tADDi3:
  case ARM::tADDi8:
  case ARM::tSUBi3:
  case ARM::tSUBi8:
  case ARM::tMUL:
  case ARM::tADC:
  case ARM::tSBC:
  case ARM::tRSB:
  case ARM::tAND:
  case ARM::tORR:
  case ARM::tEOR:
  case ARM::tBIC:
  case ARM::tMVN:
  case ARM::tASRri:
  case ARM::tASRrr:
  case ARM::tROR:
    IsThumb1 = true;
    [[fallthrough]];
  case ARM::RSBrr:
  case ARM::RSBri:
  case ARM::RSCrr:
  case ARM::RSCri:
  case ARM::ADDrr:
  case ARM::ADDri:
  case ARM::ADCrr:
  case ARM::ADCri:
  case ARM::SUBrr:
  case ARM::SUBri:
  case ARM::SBCrr:
  case ARM::SBCri:
  case ARM::t2RSBri:
  case ARM::t2ADDrr:
  case ARM::t2ADDri:
  case ARM::t2ADCrr:
  case ARM::t2ADCri:
  case ARM::t2SUBrr:
  case ARM::t2SUBri:
  case ARM::t2SBCrr:
  case ARM::t2SBCri:
  case ARM::ANDrr:
  case ARM::ANDri:
  case ARM::ANDrsr:
  case ARM::ANDrsi:
  case ARM::t2ANDrr:
  case ARM::t2ANDri:
  case ARM::t2ANDrs:
  case ARM::ORRrr:
  case ARM::ORRri:
  case ARM::ORRrsr:
  case ARM::ORRrsi:
  case ARM::t2ORRrr:
  case ARM::t2ORRri:
  case ARM::t2ORRrs:
  case ARM::EORrr:
  case ARM::EORri:
  case ARM::EORrsr:
  case ARM::EORrsi:
  case ARM::t2EORrr:
  case ARM::t2EORri:
  case ARM::t2EORrs:
  case ARM::BICri:
  case ARM::BICrr:
  case ARM::BICrsi:
  case ARM::BICrsr:
  case ARM::t2BICri:
  case ARM::t2BICrr:
  case ARM::t2BICrs:
  case ARM::t2LSRri:
  case ARM::t2LSRrr:
  case ARM::t2LSLri:
  case ARM::t2LSLrr:
  case ARM::MOVsr:
  case ARM::MOVsi:
    return true;
  }
}

/// optimizeCompareInstr - Convert the instruction supplying the argument to the
/// comparison into one that sets the zero bit in the flags register;
/// Remove a redundant Compare instruction if an earlier instruction can set the
/// flags in the same way as Compare.
/// E.g. SUBrr(r1,r2) and CMPrr(r1,r2). We also handle the case where two
/// operands are swapped: SUBrr(r1,r2) and CMPrr(r2,r1), by updating the
/// condition code of instructions which use the flags.
bool ARMBaseInstrInfo::optimizeCompareInstr(
    MachineInstr &CmpInstr, Register SrcReg, Register SrcReg2, int64_t CmpMask,
    int64_t CmpValue, const MachineRegisterInfo *MRI) const {
  // Get the unique definition of SrcReg.
  MachineInstr *MI = MRI->getUniqueVRegDef(SrcReg);
  if (!MI) return false;

  // Masked compares sometimes use the same register as the corresponding 'and'.
  if (CmpMask != ~0) {
    if (!isSuitableForMask(MI, SrcReg, CmpMask, false) || isPredicated(*MI)) {
      MI = nullptr;
      for (MachineRegisterInfo::use_instr_iterator
           UI = MRI->use_instr_begin(SrcReg), UE = MRI->use_instr_end();
           UI != UE; ++UI) {
        if (UI->getParent() != CmpInstr.getParent())
          continue;
        MachineInstr *PotentialAND = &*UI;
        if (!isSuitableForMask(PotentialAND, SrcReg, CmpMask, true) ||
            isPredicated(*PotentialAND))
          continue;
        MI = PotentialAND;
        break;
      }
      if (!MI) return false;
    }
  }

  // Get ready to iterate backward from CmpInstr.
  MachineBasicBlock::iterator I = CmpInstr, E = MI,
                              B = CmpInstr.getParent()->begin();

  // Early exit if CmpInstr is at the beginning of the BB.
  if (I == B) return false;

  // There are two possible candidates which can be changed to set CPSR:
  // One is MI, the other is a SUB or ADD instruction.
  // For CMPrr(r1,r2), we are looking for SUB(r1,r2), SUB(r2,r1), or
  // ADDr[ri](r1, r2, X).
  // For CMPri(r1, CmpValue), we are looking for SUBri(r1, CmpValue).
  MachineInstr *SubAdd = nullptr;
  if (SrcReg2 != 0)
    // MI is not a candidate for CMPrr.
    MI = nullptr;
  else if (MI->getParent() != CmpInstr.getParent() || CmpValue != 0) {
    // Conservatively refuse to convert an instruction which isn't in the same
    // BB as the comparison.
    // For CMPri w/ CmpValue != 0, a SubAdd may still be a candidate.
    // Thus we cannot return here.
    if (CmpInstr.getOpcode() == ARM::CMPri ||
        CmpInstr.getOpcode() == ARM::t2CMPri ||
        CmpInstr.getOpcode() == ARM::tCMPi8)
      MI = nullptr;
    else
      return false;
  }

  bool IsThumb1 = false;
  if (MI && !isOptimizeCompareCandidate(MI, IsThumb1))
    return false;

  // We also want to do this peephole for cases like this: if (a*b == 0),
  // and optimise away the CMP instruction from the generated code sequence:
  // MULS, MOVS, MOVS, CMP. Here the MOVS instructions load the boolean values
  // resulting from the select instruction, but these MOVS instructions for
  // Thumb1 (V6M) are flag setting and are thus preventing this optimisation.
  // However, if we only have MOVS instructions in between the CMP and the
  // other instruction (the MULS in this example), then the CPSR is dead so we
  // can safely reorder the sequence into: MOVS, MOVS, MULS, CMP. We do this
  // reordering and then continue the analysis hoping we can eliminate the
  // CMP. This peephole works on the vregs, so is still in SSA form. As a
  // consequence, the movs won't redefine/kill the MUL operands which would
  // make this reordering illegal.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  if (MI && IsThumb1) {
    --I;
    if (I != E && !MI->readsRegister(ARM::CPSR, TRI)) {
      bool CanReorder = true;
      for (; I != E; --I) {
        if (I->getOpcode() != ARM::tMOVi8) {
          CanReorder = false;
          break;
        }
      }
      if (CanReorder) {
        MI = MI->removeFromParent();
        E = CmpInstr;
        CmpInstr.getParent()->insert(E, MI);
      }
    }
    I = CmpInstr;
    E = MI;
  }

  // Check that CPSR isn't set between the comparison instruction and the one we
  // want to change. At the same time, search for SubAdd.
  bool SubAddIsThumb1 = false;
  do {
    const MachineInstr &Instr = *--I;

    // Check whether CmpInstr can be made redundant by the current instruction.
    if (isRedundantFlagInstr(&CmpInstr, SrcReg, SrcReg2, CmpValue, &Instr,
                             SubAddIsThumb1)) {
      SubAdd = &*I;
      break;
    }

    // Allow E (which was initially MI) to be SubAdd but do not search before E.
    if (I == E)
      break;

    if (Instr.modifiesRegister(ARM::CPSR, TRI) ||
        Instr.readsRegister(ARM::CPSR, TRI))
      // This instruction modifies or uses CPSR after the one we want to
      // change. We can't do this transformation.
      return false;

    if (I == B) {
      // In some cases, we scan the use-list of an instruction for an AND;
      // that AND is in the same BB, but may not be scheduled before the
      // corresponding TST.  In that case, bail out.
      //
      // FIXME: We could try to reschedule the AND.
      return false;
    }
  } while (true);

  // Return false if no candidates exist.
  if (!MI && !SubAdd)
    return false;

  // If we found a SubAdd, use it as it will be closer to the CMP
  if (SubAdd) {
    MI = SubAdd;
    IsThumb1 = SubAddIsThumb1;
  }

  // We can't use a predicated instruction - it doesn't always write the flags.
  if (isPredicated(*MI))
    return false;

  // Scan forward for the use of CPSR
  // When checking against MI: if it's a conditional code that requires
  // checking of the V bit or C bit, then this is not safe to do.
  // It is safe to remove CmpInstr if CPSR is redefined or killed.
  // If we are done with the basic block, we need to check whether CPSR is
  // live-out.
  SmallVector<std::pair<MachineOperand*, ARMCC::CondCodes>, 4>
      OperandsToUpdate;
  bool isSafe = false;
  I = CmpInstr;
  E = CmpInstr.getParent()->end();
  while (!isSafe && ++I != E) {
    const MachineInstr &Instr = *I;
    for (unsigned IO = 0, EO = Instr.getNumOperands();
         !isSafe && IO != EO; ++IO) {
      const MachineOperand &MO = Instr.getOperand(IO);
      if (MO.isRegMask() && MO.clobbersPhysReg(ARM::CPSR)) {
        isSafe = true;
        break;
      }
      if (!MO.isReg() || MO.getReg() != ARM::CPSR)
        continue;
      if (MO.isDef()) {
        isSafe = true;
        break;
      }
      // Condition code is after the operand before CPSR except for VSELs.
      ARMCC::CondCodes CC;
      bool IsInstrVSel = true;
      switch (Instr.getOpcode()) {
      default:
        IsInstrVSel = false;
        CC = (ARMCC::CondCodes)Instr.getOperand(IO - 1).getImm();
        break;
      case ARM::VSELEQD:
      case ARM::VSELEQS:
      case ARM::VSELEQH:
        CC = ARMCC::EQ;
        break;
      case ARM::VSELGTD:
      case ARM::VSELGTS:
      case ARM::VSELGTH:
        CC = ARMCC::GT;
        break;
      case ARM::VSELGED:
      case ARM::VSELGES:
      case ARM::VSELGEH:
        CC = ARMCC::GE;
        break;
      case ARM::VSELVSD:
      case ARM::VSELVSS:
      case ARM::VSELVSH:
        CC = ARMCC::VS;
        break;
      }

      if (SubAdd) {
        // If we have SUB(r1, r2) and CMP(r2, r1), the condition code based
        // on CMP needs to be updated to be based on SUB.
        // If we have ADD(r1, r2, X) and CMP(r1, r2), the condition code also
        // needs to be modified.
        // Push the condition code operands to OperandsToUpdate.
        // If it is safe to remove CmpInstr, the condition code of these
        // operands will be modified.
        unsigned Opc = SubAdd->getOpcode();
        bool IsSub = Opc == ARM::SUBrr || Opc == ARM::t2SUBrr ||
                     Opc == ARM::SUBri || Opc == ARM::t2SUBri ||
                     Opc == ARM::tSUBrr || Opc == ARM::tSUBi3 ||
                     Opc == ARM::tSUBi8;
        unsigned OpI = Opc != ARM::tSUBrr ? 1 : 2;
        if (!IsSub ||
            (SrcReg2 != 0 && SubAdd->getOperand(OpI).getReg() == SrcReg2 &&
             SubAdd->getOperand(OpI + 1).getReg() == SrcReg)) {
          // VSel doesn't support condition code update.
          if (IsInstrVSel)
            return false;
          // Ensure we can swap the condition.
          ARMCC::CondCodes NewCC = (IsSub ? getSwappedCondition(CC) : getCmpToAddCondition(CC));
          if (NewCC == ARMCC::AL)
            return false;
          OperandsToUpdate.push_back(
              std::make_pair(&((*I).getOperand(IO - 1)), NewCC));
        }
      } else {
        // No SubAdd, so this is x = <op> y, z; cmp x, 0.
        switch (CC) {
        case ARMCC::EQ: // Z
        case ARMCC::NE: // Z
        case ARMCC::MI: // N
        case ARMCC::PL: // N
        case ARMCC::AL: // none
          // CPSR can be used multiple times, we should continue.
          break;
        case ARMCC::HS: // C
        case ARMCC::LO: // C
        case ARMCC::VS: // V
        case ARMCC::VC: // V
        case ARMCC::HI: // C Z
        case ARMCC::LS: // C Z
        case ARMCC::GE: // N V
        case ARMCC::LT: // N V
        case ARMCC::GT: // Z N V
        case ARMCC::LE: // Z N V
          // The instruction uses the V bit or C bit which is not safe.
          return false;
        }
      }
    }
  }

  // If CPSR is not killed nor re-defined, we should check whether it is
  // live-out. If it is live-out, do not optimize.
  if (!isSafe) {
    MachineBasicBlock *MBB = CmpInstr.getParent();
    for (MachineBasicBlock *Succ : MBB->successors())
      if (Succ->isLiveIn(ARM::CPSR))
        return false;
  }

  // Toggle the optional operand to CPSR (if it exists - in Thumb1 we always
  // set CPSR so this is represented as an explicit output)
  if (!IsThumb1) {
    unsigned CPSRRegNum = MI->getNumExplicitOperands() - 1;
    MI->getOperand(CPSRRegNum).setReg(ARM::CPSR);
    MI->getOperand(CPSRRegNum).setIsDef(true);
  }
  assert(!isPredicated(*MI) && "Can't use flags from predicated instruction");
  CmpInstr.eraseFromParent();

  // Modify the condition code of operands in OperandsToUpdate.
  // Since we have SUB(r1, r2) and CMP(r2, r1), the condition code needs to
  // be changed from r2 > r1 to r1 < r2, from r2 < r1 to r1 > r2, etc.
  for (unsigned i = 0, e = OperandsToUpdate.size(); i < e; i++)
    OperandsToUpdate[i].first->setImm(OperandsToUpdate[i].second);

  MI->clearRegisterDeads(ARM::CPSR);

  return true;
}

bool ARMBaseInstrInfo::shouldSink(const MachineInstr &MI) const {
  // Do not sink MI if it might be used to optimize a redundant compare.
  // We heuristically only look at the instruction immediately following MI to
  // avoid potentially searching the entire basic block.
  if (isPredicated(MI))
    return true;
  MachineBasicBlock::const_iterator Next = &MI;
  ++Next;
  Register SrcReg, SrcReg2;
  int64_t CmpMask, CmpValue;
  bool IsThumb1;
  if (Next != MI.getParent()->end() &&
      analyzeCompare(*Next, SrcReg, SrcReg2, CmpMask, CmpValue) &&
      isRedundantFlagInstr(&*Next, SrcReg, SrcReg2, CmpValue, &MI, IsThumb1))
    return false;
  return true;
}

bool ARMBaseInstrInfo::foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                     Register Reg,
                                     MachineRegisterInfo *MRI) const {
  // Fold large immediates into add, sub, or, xor.
  unsigned DefOpc = DefMI.getOpcode();
  if (DefOpc != ARM::t2MOVi32imm && DefOpc != ARM::MOVi32imm &&
      DefOpc != ARM::tMOVi32imm)
    return false;
  if (!DefMI.getOperand(1).isImm())
    // Could be t2MOVi32imm @xx
    return false;

  if (!MRI->hasOneNonDBGUse(Reg))
    return false;

  const MCInstrDesc &DefMCID = DefMI.getDesc();
  if (DefMCID.hasOptionalDef()) {
    unsigned NumOps = DefMCID.getNumOperands();
    const MachineOperand &MO = DefMI.getOperand(NumOps - 1);
    if (MO.getReg() == ARM::CPSR && !MO.isDead())
      // If DefMI defines CPSR and it is not dead, it's obviously not safe
      // to delete DefMI.
      return false;
  }

  const MCInstrDesc &UseMCID = UseMI.getDesc();
  if (UseMCID.hasOptionalDef()) {
    unsigned NumOps = UseMCID.getNumOperands();
    if (UseMI.getOperand(NumOps - 1).getReg() == ARM::CPSR)
      // If the instruction sets the flag, do not attempt this optimization
      // since it may change the semantics of the code.
      return false;
  }

  unsigned UseOpc = UseMI.getOpcode();
  unsigned NewUseOpc = 0;
  uint32_t ImmVal = (uint32_t)DefMI.getOperand(1).getImm();
  uint32_t SOImmValV1 = 0, SOImmValV2 = 0;
  bool Commute = false;
  switch (UseOpc) {
  default: return false;
  case ARM::SUBrr:
  case ARM::ADDrr:
  case ARM::ORRrr:
  case ARM::EORrr:
  case ARM::t2SUBrr:
  case ARM::t2ADDrr:
  case ARM::t2ORRrr:
  case ARM::t2EORrr: {
    Commute = UseMI.getOperand(2).getReg() != Reg;
    switch (UseOpc) {
    default: break;
    case ARM::ADDrr:
    case ARM::SUBrr:
      if (UseOpc == ARM::SUBrr && Commute)
        return false;

      // ADD/SUB are special because they're essentially the same operation, so
      // we can handle a larger range of immediates.
      if (ARM_AM::isSOImmTwoPartVal(ImmVal))
        NewUseOpc = UseOpc == ARM::ADDrr ? ARM::ADDri : ARM::SUBri;
      else if (ARM_AM::isSOImmTwoPartVal(-ImmVal)) {
        ImmVal = -ImmVal;
        NewUseOpc = UseOpc == ARM::ADDrr ? ARM::SUBri : ARM::ADDri;
      } else
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getSOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getSOImmTwoPartSecond(ImmVal);
      break;
    case ARM::ORRrr:
    case ARM::EORrr:
      if (!ARM_AM::isSOImmTwoPartVal(ImmVal))
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getSOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getSOImmTwoPartSecond(ImmVal);
      switch (UseOpc) {
      default: break;
      case ARM::ORRrr: NewUseOpc = ARM::ORRri; break;
      case ARM::EORrr: NewUseOpc = ARM::EORri; break;
      }
      break;
    case ARM::t2ADDrr:
    case ARM::t2SUBrr: {
      if (UseOpc == ARM::t2SUBrr && Commute)
        return false;

      // ADD/SUB are special because they're essentially the same operation, so
      // we can handle a larger range of immediates.
      const bool ToSP = DefMI.getOperand(0).getReg() == ARM::SP;
      const unsigned t2ADD = ToSP ? ARM::t2ADDspImm : ARM::t2ADDri;
      const unsigned t2SUB = ToSP ? ARM::t2SUBspImm : ARM::t2SUBri;
      if (ARM_AM::isT2SOImmTwoPartVal(ImmVal))
        NewUseOpc = UseOpc == ARM::t2ADDrr ? t2ADD : t2SUB;
      else if (ARM_AM::isT2SOImmTwoPartVal(-ImmVal)) {
        ImmVal = -ImmVal;
        NewUseOpc = UseOpc == ARM::t2ADDrr ? t2SUB : t2ADD;
      } else
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getT2SOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getT2SOImmTwoPartSecond(ImmVal);
      break;
    }
    case ARM::t2ORRrr:
    case ARM::t2EORrr:
      if (!ARM_AM::isT2SOImmTwoPartVal(ImmVal))
        return false;
      SOImmValV1 = (uint32_t)ARM_AM::getT2SOImmTwoPartFirst(ImmVal);
      SOImmValV2 = (uint32_t)ARM_AM::getT2SOImmTwoPartSecond(ImmVal);
      switch (UseOpc) {
      default: break;
      case ARM::t2ORRrr: NewUseOpc = ARM::t2ORRri; break;
      case ARM::t2EORrr: NewUseOpc = ARM::t2EORri; break;
      }
      break;
    }
  }
  }

  unsigned OpIdx = Commute ? 2 : 1;
  Register Reg1 = UseMI.getOperand(OpIdx).getReg();
  bool isKill = UseMI.getOperand(OpIdx).isKill();
  const TargetRegisterClass *TRC = MRI->getRegClass(Reg);
  Register NewReg = MRI->createVirtualRegister(TRC);
  BuildMI(*UseMI.getParent(), UseMI, UseMI.getDebugLoc(), get(NewUseOpc),
          NewReg)
      .addReg(Reg1, getKillRegState(isKill))
      .addImm(SOImmValV1)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());
  UseMI.setDesc(get(NewUseOpc));
  UseMI.getOperand(1).setReg(NewReg);
  UseMI.getOperand(1).setIsKill();
  UseMI.getOperand(2).ChangeToImmediate(SOImmValV2);
  DefMI.eraseFromParent();
  // FIXME: t2ADDrr should be split, as different rulles apply when writing to SP.
  // Just as t2ADDri, that was split to [t2ADDri, t2ADDspImm].
  // Then the below code will not be needed, as the input/output register
  // classes will be rgpr or gprSP.
  // For now, we fix the UseMI operand explicitly here:
  switch(NewUseOpc){
    case ARM::t2ADDspImm:
    case ARM::t2SUBspImm:
    case ARM::t2ADDri:
    case ARM::t2SUBri:
      MRI->constrainRegClass(UseMI.getOperand(0).getReg(), TRC);
  }
  return true;
}

static unsigned getNumMicroOpsSwiftLdSt(const InstrItineraryData *ItinData,
                                        const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default: {
    const MCInstrDesc &Desc = MI.getDesc();
    int UOps = ItinData->getNumMicroOps(Desc.getSchedClass());
    assert(UOps >= 0 && "bad # UOps");
    return UOps;
  }

  case ARM::LDRrs:
  case ARM::LDRBrs:
  case ARM::STRrs:
  case ARM::STRBrs: {
    unsigned ShOpVal = MI.getOperand(3).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 1;
    return 2;
  }

  case ARM::LDRH:
  case ARM::STRH: {
    if (!MI.getOperand(2).getReg())
      return 1;

    unsigned ShOpVal = MI.getOperand(3).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 1;
    return 2;
  }

  case ARM::LDRSB:
  case ARM::LDRSH:
    return (ARM_AM::getAM3Op(MI.getOperand(3).getImm()) == ARM_AM::sub) ? 3 : 2;

  case ARM::LDRSB_POST:
  case ARM::LDRSH_POST: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rm = MI.getOperand(3).getReg();
    return (Rt == Rm) ? 4 : 3;
  }

  case ARM::LDR_PRE_REG:
  case ARM::LDRB_PRE_REG: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rm = MI.getOperand(3).getReg();
    if (Rt == Rm)
      return 3;
    unsigned ShOpVal = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 2;
    return 3;
  }

  case ARM::STR_PRE_REG:
  case ARM::STRB_PRE_REG: {
    unsigned ShOpVal = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 2;
    return 3;
  }

  case ARM::LDRH_PRE:
  case ARM::STRH_PRE: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rm = MI.getOperand(3).getReg();
    if (!Rm)
      return 2;
    if (Rt == Rm)
      return 3;
    return (ARM_AM::getAM3Op(MI.getOperand(4).getImm()) == ARM_AM::sub) ? 3 : 2;
  }

  case ARM::LDR_POST_REG:
  case ARM::LDRB_POST_REG:
  case ARM::LDRH_POST: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rm = MI.getOperand(3).getReg();
    return (Rt == Rm) ? 3 : 2;
  }

  case ARM::LDR_PRE_IMM:
  case ARM::LDRB_PRE_IMM:
  case ARM::LDR_POST_IMM:
  case ARM::LDRB_POST_IMM:
  case ARM::STRB_POST_IMM:
  case ARM::STRB_POST_REG:
  case ARM::STRB_PRE_IMM:
  case ARM::STRH_POST:
  case ARM::STR_POST_IMM:
  case ARM::STR_POST_REG:
  case ARM::STR_PRE_IMM:
    return 2;

  case ARM::LDRSB_PRE:
  case ARM::LDRSH_PRE: {
    Register Rm = MI.getOperand(3).getReg();
    if (Rm == 0)
      return 3;
    Register Rt = MI.getOperand(0).getReg();
    if (Rt == Rm)
      return 4;
    unsigned ShOpVal = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
    unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
    if (!isSub &&
        (ShImm == 0 ||
         ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
          ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
      return 3;
    return 4;
  }

  case ARM::LDRD: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rn = MI.getOperand(2).getReg();
    Register Rm = MI.getOperand(3).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(4).getImm()) == ARM_AM::sub) ? 4
                                                                          : 3;
    return (Rt == Rn) ? 3 : 2;
  }

  case ARM::STRD: {
    Register Rm = MI.getOperand(3).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(4).getImm()) == ARM_AM::sub) ? 4
                                                                          : 3;
    return 2;
  }

  case ARM::LDRD_POST:
  case ARM::t2LDRD_POST:
    return 3;

  case ARM::STRD_POST:
  case ARM::t2STRD_POST:
    return 4;

  case ARM::LDRD_PRE: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rn = MI.getOperand(3).getReg();
    Register Rm = MI.getOperand(4).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(5).getImm()) == ARM_AM::sub) ? 5
                                                                          : 4;
    return (Rt == Rn) ? 4 : 3;
  }

  case ARM::t2LDRD_PRE: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rn = MI.getOperand(3).getReg();
    return (Rt == Rn) ? 4 : 3;
  }

  case ARM::STRD_PRE: {
    Register Rm = MI.getOperand(4).getReg();
    if (Rm)
      return (ARM_AM::getAM3Op(MI.getOperand(5).getImm()) == ARM_AM::sub) ? 5
                                                                          : 4;
    return 3;
  }

  case ARM::t2STRD_PRE:
    return 3;

  case ARM::t2LDR_POST:
  case ARM::t2LDRB_POST:
  case ARM::t2LDRB_PRE:
  case ARM::t2LDRSBi12:
  case ARM::t2LDRSBi8:
  case ARM::t2LDRSBpci:
  case ARM::t2LDRSBs:
  case ARM::t2LDRH_POST:
  case ARM::t2LDRH_PRE:
  case ARM::t2LDRSBT:
  case ARM::t2LDRSB_POST:
  case ARM::t2LDRSB_PRE:
  case ARM::t2LDRSH_POST:
  case ARM::t2LDRSH_PRE:
  case ARM::t2LDRSHi12:
  case ARM::t2LDRSHi8:
  case ARM::t2LDRSHpci:
  case ARM::t2LDRSHs:
    return 2;

  case ARM::t2LDRDi8: {
    Register Rt = MI.getOperand(0).getReg();
    Register Rn = MI.getOperand(2).getReg();
    return (Rt == Rn) ? 3 : 2;
  }

  case ARM::t2STRB_POST:
  case ARM::t2STRB_PRE:
  case ARM::t2STRBs:
  case ARM::t2STRDi8:
  case ARM::t2STRH_POST:
  case ARM::t2STRH_PRE:
  case ARM::t2STRHs:
  case ARM::t2STR_POST:
  case ARM::t2STR_PRE:
  case ARM::t2STRs:
    return 2;
  }
}

// Return the number of 32-bit words loaded by LDM or stored by STM. If this
// can't be easily determined return 0 (missing MachineMemOperand).
//
// FIXME: The current MachineInstr design does not support relying on machine
// mem operands to determine the width of a memory access. Instead, we expect
// the target to provide this information based on the instruction opcode and
// operands. However, using MachineMemOperand is the best solution now for
// two reasons:
//
// 1) getNumMicroOps tries to infer LDM memory width from the total number of MI
// operands. This is much more dangerous than using the MachineMemOperand
// sizes because CodeGen passes can insert/remove optional machine operands. In
// fact, it's totally incorrect for preRA passes and appears to be wrong for
// postRA passes as well.
//
// 2) getNumLDMAddresses is only used by the scheduling machine model and any
// machine model that calls this should handle the unknown (zero size) case.
//
// Long term, we should require a target hook that verifies MachineMemOperand
// sizes during MC lowering. That target hook should be local to MC lowering
// because we can't ensure that it is aware of other MI forms. Doing this will
// ensure that MachineMemOperands are correctly propagated through all passes.
unsigned ARMBaseInstrInfo::getNumLDMAddresses(const MachineInstr &MI) const {
  unsigned Size = 0;
  for (MachineInstr::mmo_iterator I = MI.memoperands_begin(),
                                  E = MI.memoperands_end();
       I != E; ++I) {
    Size += (*I)->getSize().getValue();
  }
  // FIXME: The scheduler currently can't handle values larger than 16. But
  // the values can actually go up to 32 for floating-point load/store
  // multiple (VLDMIA etc.). Also, the way this code is reasoning about memory
  // operations isn't right; we could end up with "extra" memory operands for
  // various reasons, like tail merge merging two memory operations.
  return std::min(Size / 4, 16U);
}

static unsigned getNumMicroOpsSingleIssuePlusExtras(unsigned Opc,
                                                    unsigned NumRegs) {
  unsigned UOps = 1 + NumRegs; // 1 for address computation.
  switch (Opc) {
  default:
    break;
  case ARM::VLDMDIA_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VLDMSIA_UPD:
  case ARM::VLDMSDB_UPD:
  case ARM::VSTMDIA_UPD:
  case ARM::VSTMDDB_UPD:
  case ARM::VSTMSIA_UPD:
  case ARM::VSTMSDB_UPD:
  case ARM::LDMIA_UPD:
  case ARM::LDMDA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::STMIA_UPD:
  case ARM::STMDA_UPD:
  case ARM::STMDB_UPD:
  case ARM::STMIB_UPD:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD:
    ++UOps; // One for base register writeback.
    break;
  case ARM::LDMIA_RET:
  case ARM::tPOP_RET:
  case ARM::t2LDMIA_RET:
    UOps += 2; // One for base reg wb, one for write to pc.
    break;
  }
  return UOps;
}

unsigned ARMBaseInstrInfo::getNumMicroOps(const InstrItineraryData *ItinData,
                                          const MachineInstr &MI) const {
  if (!ItinData || ItinData->isEmpty())
    return 1;

  const MCInstrDesc &Desc = MI.getDesc();
  unsigned Class = Desc.getSchedClass();
  int ItinUOps = ItinData->getNumMicroOps(Class);
  if (ItinUOps >= 0) {
    if (Subtarget.isSwift() && (Desc.mayLoad() || Desc.mayStore()))
      return getNumMicroOpsSwiftLdSt(ItinData, MI);

    return ItinUOps;
  }

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    llvm_unreachable("Unexpected multi-uops instruction!");
  case ARM::VLDMQIA:
  case ARM::VSTMQIA:
    return 2;

  // The number of uOps for load / store multiple are determined by the number
  // registers.
  //
  // On Cortex-A8, each pair of register loads / stores can be scheduled on the
  // same cycle. The scheduling for the first load / store must be done
  // separately by assuming the address is not 64-bit aligned.
  //
  // On Cortex-A9, the formula is simply (#reg / 2) + (#reg % 2). If the address
  // is not 64-bit aligned, then AGU would take an extra cycle.  For VFP / NEON
  // load / store multiple, the formula is (#reg / 2) + (#reg % 2) + 1.
  case ARM::VLDMDIA:
  case ARM::VLDMDIA_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VLDMSIA:
  case ARM::VLDMSIA_UPD:
  case ARM::VLDMSDB_UPD:
  case ARM::VSTMDIA:
  case ARM::VSTMDIA_UPD:
  case ARM::VSTMDDB_UPD:
  case ARM::VSTMSIA:
  case ARM::VSTMSIA_UPD:
  case ARM::VSTMSDB_UPD: {
    unsigned NumRegs = MI.getNumOperands() - Desc.getNumOperands();
    return (NumRegs / 2) + (NumRegs % 2) + 1;
  }

  case ARM::LDMIA_RET:
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
  case ARM::LDMIA_UPD:
  case ARM::LDMDA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
  case ARM::STMIA_UPD:
  case ARM::STMDA_UPD:
  case ARM::STMDB_UPD:
  case ARM::STMIB_UPD:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::tPOP_RET:
  case ARM::tPOP:
  case ARM::tPUSH:
  case ARM::t2LDMIA_RET:
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMIA:
  case ARM::t2STMDB:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD: {
    unsigned NumRegs = MI.getNumOperands() - Desc.getNumOperands() + 1;
    switch (Subtarget.getLdStMultipleTiming()) {
    case ARMSubtarget::SingleIssuePlusExtras:
      return getNumMicroOpsSingleIssuePlusExtras(Opc, NumRegs);
    case ARMSubtarget::SingleIssue:
      // Assume the worst.
      return NumRegs;
    case ARMSubtarget::DoubleIssue: {
      if (NumRegs < 4)
        return 2;
      // 4 registers would be issued: 2, 2.
      // 5 registers would be issued: 2, 2, 1.
      unsigned UOps = (NumRegs / 2);
      if (NumRegs % 2)
        ++UOps;
      return UOps;
    }
    case ARMSubtarget::DoubleIssueCheckUnalignedAccess: {
      unsigned UOps = (NumRegs / 2);
      // If there are odd number of registers or if it's not 64-bit aligned,
      // then it takes an extra AGU (Address Generation Unit) cycle.
      if ((NumRegs % 2) || !MI.hasOneMemOperand() ||
          (*MI.memoperands_begin())->getAlign() < Align(8))
        ++UOps;
      return UOps;
      }
    }
  }
  }
  llvm_unreachable("Didn't find the number of microops");
}

std::optional<unsigned>
ARMBaseInstrInfo::getVLDMDefCycle(const InstrItineraryData *ItinData,
                                  const MCInstrDesc &DefMCID, unsigned DefClass,
                                  unsigned DefIdx, unsigned DefAlign) const {
  int RegNo = (int)(DefIdx+1) - DefMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    // Def is the address writeback.
    return ItinData->getOperandCycle(DefClass, DefIdx);

  unsigned DefCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    // (regno / 2) + (regno % 2) + 1
    DefCycle = RegNo / 2 + 1;
    if (RegNo % 2)
      ++DefCycle;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    DefCycle = RegNo;
    bool isSLoad = false;

    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::VLDMSIA:
    case ARM::VLDMSIA_UPD:
    case ARM::VLDMSDB_UPD:
      isSLoad = true;
      break;
    }

    // If there are odd number of 'S' registers or if it's not 64-bit aligned,
    // then it takes an extra cycle.
    if ((isSLoad && (RegNo % 2)) || DefAlign < 8)
      ++DefCycle;
  } else {
    // Assume the worst.
    DefCycle = RegNo + 2;
  }

  return DefCycle;
}

std::optional<unsigned>
ARMBaseInstrInfo::getLDMDefCycle(const InstrItineraryData *ItinData,
                                 const MCInstrDesc &DefMCID, unsigned DefClass,
                                 unsigned DefIdx, unsigned DefAlign) const {
  int RegNo = (int)(DefIdx+1) - DefMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    // Def is the address writeback.
    return ItinData->getOperandCycle(DefClass, DefIdx);

  unsigned DefCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    // 4 registers would be issued: 1, 2, 1.
    // 5 registers would be issued: 1, 2, 2.
    DefCycle = RegNo / 2;
    if (DefCycle < 1)
      DefCycle = 1;
    // Result latency is issue cycle + 2: E2.
    DefCycle += 2;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    DefCycle = (RegNo / 2);
    // If there are odd number of registers or if it's not 64-bit aligned,
    // then it takes an extra AGU (Address Generation Unit) cycle.
    if ((RegNo % 2) || DefAlign < 8)
      ++DefCycle;
    // Result latency is AGU cycles + 2.
    DefCycle += 2;
  } else {
    // Assume the worst.
    DefCycle = RegNo + 2;
  }

  return DefCycle;
}

std::optional<unsigned>
ARMBaseInstrInfo::getVSTMUseCycle(const InstrItineraryData *ItinData,
                                  const MCInstrDesc &UseMCID, unsigned UseClass,
                                  unsigned UseIdx, unsigned UseAlign) const {
  int RegNo = (int)(UseIdx+1) - UseMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    return ItinData->getOperandCycle(UseClass, UseIdx);

  unsigned UseCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    // (regno / 2) + (regno % 2) + 1
    UseCycle = RegNo / 2 + 1;
    if (RegNo % 2)
      ++UseCycle;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    UseCycle = RegNo;
    bool isSStore = false;

    switch (UseMCID.getOpcode()) {
    default: break;
    case ARM::VSTMSIA:
    case ARM::VSTMSIA_UPD:
    case ARM::VSTMSDB_UPD:
      isSStore = true;
      break;
    }

    // If there are odd number of 'S' registers or if it's not 64-bit aligned,
    // then it takes an extra cycle.
    if ((isSStore && (RegNo % 2)) || UseAlign < 8)
      ++UseCycle;
  } else {
    // Assume the worst.
    UseCycle = RegNo + 2;
  }

  return UseCycle;
}

std::optional<unsigned>
ARMBaseInstrInfo::getSTMUseCycle(const InstrItineraryData *ItinData,
                                 const MCInstrDesc &UseMCID, unsigned UseClass,
                                 unsigned UseIdx, unsigned UseAlign) const {
  int RegNo = (int)(UseIdx+1) - UseMCID.getNumOperands() + 1;
  if (RegNo <= 0)
    return ItinData->getOperandCycle(UseClass, UseIdx);

  unsigned UseCycle;
  if (Subtarget.isCortexA8() || Subtarget.isCortexA7()) {
    UseCycle = RegNo / 2;
    if (UseCycle < 2)
      UseCycle = 2;
    // Read in E3.
    UseCycle += 2;
  } else if (Subtarget.isLikeA9() || Subtarget.isSwift()) {
    UseCycle = (RegNo / 2);
    // If there are odd number of registers or if it's not 64-bit aligned,
    // then it takes an extra AGU (Address Generation Unit) cycle.
    if ((RegNo % 2) || UseAlign < 8)
      ++UseCycle;
  } else {
    // Assume the worst.
    UseCycle = 1;
  }
  return UseCycle;
}

std::optional<unsigned> ARMBaseInstrInfo::getOperandLatency(
    const InstrItineraryData *ItinData, const MCInstrDesc &DefMCID,
    unsigned DefIdx, unsigned DefAlign, const MCInstrDesc &UseMCID,
    unsigned UseIdx, unsigned UseAlign) const {
  unsigned DefClass = DefMCID.getSchedClass();
  unsigned UseClass = UseMCID.getSchedClass();

  if (DefIdx < DefMCID.getNumDefs() && UseIdx < UseMCID.getNumOperands())
    return ItinData->getOperandLatency(DefClass, DefIdx, UseClass, UseIdx);

  // This may be a def / use of a variable_ops instruction, the operand
  // latency might be determinable dynamically. Let the target try to
  // figure it out.
  std::optional<unsigned> DefCycle;
  bool LdmBypass = false;
  switch (DefMCID.getOpcode()) {
  default:
    DefCycle = ItinData->getOperandCycle(DefClass, DefIdx);
    break;

  case ARM::VLDMDIA:
  case ARM::VLDMDIA_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VLDMSIA:
  case ARM::VLDMSIA_UPD:
  case ARM::VLDMSDB_UPD:
    DefCycle = getVLDMDefCycle(ItinData, DefMCID, DefClass, DefIdx, DefAlign);
    break;

  case ARM::LDMIA_RET:
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
  case ARM::LDMIA_UPD:
  case ARM::LDMDA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tPUSH:
  case ARM::t2LDMIA_RET:
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
    LdmBypass = true;
    DefCycle = getLDMDefCycle(ItinData, DefMCID, DefClass, DefIdx, DefAlign);
    break;
  }

  if (!DefCycle)
    // We can't seem to determine the result latency of the def, assume it's 2.
    DefCycle = 2;

  std::optional<unsigned> UseCycle;
  switch (UseMCID.getOpcode()) {
  default:
    UseCycle = ItinData->getOperandCycle(UseClass, UseIdx);
    break;

  case ARM::VSTMDIA:
  case ARM::VSTMDIA_UPD:
  case ARM::VSTMDDB_UPD:
  case ARM::VSTMSIA:
  case ARM::VSTMSIA_UPD:
  case ARM::VSTMSDB_UPD:
    UseCycle = getVSTMUseCycle(ItinData, UseMCID, UseClass, UseIdx, UseAlign);
    break;

  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
  case ARM::STMIA_UPD:
  case ARM::STMDA_UPD:
  case ARM::STMDB_UPD:
  case ARM::STMIB_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::tPOP_RET:
  case ARM::tPOP:
  case ARM::t2STMIA:
  case ARM::t2STMDB:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD:
    UseCycle = getSTMUseCycle(ItinData, UseMCID, UseClass, UseIdx, UseAlign);
    break;
  }

  if (!UseCycle)
    // Assume it's read in the first stage.
    UseCycle = 1;

  if (UseCycle > *DefCycle + 1)
    return std::nullopt;

  UseCycle = *DefCycle - *UseCycle + 1;
  if (UseCycle > 0u) {
    if (LdmBypass) {
      // It's a variable_ops instruction so we can't use DefIdx here. Just use
      // first def operand.
      if (ItinData->hasPipelineForwarding(DefClass, DefMCID.getNumOperands()-1,
                                          UseClass, UseIdx))
        UseCycle = *UseCycle - 1;
    } else if (ItinData->hasPipelineForwarding(DefClass, DefIdx,
                                               UseClass, UseIdx)) {
      UseCycle = *UseCycle - 1;
    }
  }

  return UseCycle;
}

static const MachineInstr *getBundledDefMI(const TargetRegisterInfo *TRI,
                                           const MachineInstr *MI, unsigned Reg,
                                           unsigned &DefIdx, unsigned &Dist) {
  Dist = 0;

  MachineBasicBlock::const_iterator I = MI; ++I;
  MachineBasicBlock::const_instr_iterator II = std::prev(I.getInstrIterator());
  assert(II->isInsideBundle() && "Empty bundle?");

  int Idx = -1;
  while (II->isInsideBundle()) {
    Idx = II->findRegisterDefOperandIdx(Reg, TRI, false, true);
    if (Idx != -1)
      break;
    --II;
    ++Dist;
  }

  assert(Idx != -1 && "Cannot find bundled definition!");
  DefIdx = Idx;
  return &*II;
}

static const MachineInstr *getBundledUseMI(const TargetRegisterInfo *TRI,
                                           const MachineInstr &MI, unsigned Reg,
                                           unsigned &UseIdx, unsigned &Dist) {
  Dist = 0;

  MachineBasicBlock::const_instr_iterator II = ++MI.getIterator();
  assert(II->isInsideBundle() && "Empty bundle?");
  MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();

  // FIXME: This doesn't properly handle multiple uses.
  int Idx = -1;
  while (II != E && II->isInsideBundle()) {
    Idx = II->findRegisterUseOperandIdx(Reg, TRI, false);
    if (Idx != -1)
      break;
    if (II->getOpcode() != ARM::t2IT)
      ++Dist;
    ++II;
  }

  if (Idx == -1) {
    Dist = 0;
    return nullptr;
  }

  UseIdx = Idx;
  return &*II;
}

/// Return the number of cycles to add to (or subtract from) the static
/// itinerary based on the def opcode and alignment. The caller will ensure that
/// adjusted latency is at least one cycle.
static int adjustDefLatency(const ARMSubtarget &Subtarget,
                            const MachineInstr &DefMI,
                            const MCInstrDesc &DefMCID, unsigned DefAlign) {
  int Adjust = 0;
  if (Subtarget.isCortexA8() || Subtarget.isLikeA9() || Subtarget.isCortexA7()) {
    // FIXME: Shifter op hack: no shift (i.e. [r +/- r]) or [r + r << 2]
    // variants are one cycle cheaper.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal = DefMI.getOperand(3).getImm();
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (ShImm == 0 ||
          (ShImm == 2 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl))
        --Adjust;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs: {
      // Thumb2 mode: lsl only.
      unsigned ShAmt = DefMI.getOperand(3).getImm();
      if (ShAmt == 0 || ShAmt == 2)
        --Adjust;
      break;
    }
    }
  } else if (Subtarget.isSwift()) {
    // FIXME: Properly handle all of the latency adjustments for address
    // writeback.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal = DefMI.getOperand(3).getImm();
      bool isSub = ARM_AM::getAM2Op(ShOpVal) == ARM_AM::sub;
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (!isSub &&
          (ShImm == 0 ||
           ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
            ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl)))
        Adjust -= 2;
      else if (!isSub &&
               ShImm == 1 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsr)
        --Adjust;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs: {
      // Thumb2 mode: lsl only.
      unsigned ShAmt = DefMI.getOperand(3).getImm();
      if (ShAmt == 0 || ShAmt == 1 || ShAmt == 2 || ShAmt == 3)
        Adjust -= 2;
      break;
    }
    }
  }

  if (DefAlign < 8 && Subtarget.checkVLDnAccessAlignment()) {
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::VLD1q8:
    case ARM::VLD1q16:
    case ARM::VLD1q32:
    case ARM::VLD1q64:
    case ARM::VLD1q8wb_fixed:
    case ARM::VLD1q16wb_fixed:
    case ARM::VLD1q32wb_fixed:
    case ARM::VLD1q64wb_fixed:
    case ARM::VLD1q8wb_register:
    case ARM::VLD1q16wb_register:
    case ARM::VLD1q32wb_register:
    case ARM::VLD1q64wb_register:
    case ARM::VLD2d8:
    case ARM::VLD2d16:
    case ARM::VLD2d32:
    case ARM::VLD2q8:
    case ARM::VLD2q16:
    case ARM::VLD2q32:
    case ARM::VLD2d8wb_fixed:
    case ARM::VLD2d16wb_fixed:
    case ARM::VLD2d32wb_fixed:
    case ARM::VLD2q8wb_fixed:
    case ARM::VLD2q16wb_fixed:
    case ARM::VLD2q32wb_fixed:
    case ARM::VLD2d8wb_register:
    case ARM::VLD2d16wb_register:
    case ARM::VLD2d32wb_register:
    case ARM::VLD2q8wb_register:
    case ARM::VLD2q16wb_register:
    case ARM::VLD2q32wb_register:
    case ARM::VLD3d8:
    case ARM::VLD3d16:
    case ARM::VLD3d32:
    case ARM::VLD1d64T:
    case ARM::VLD3d8_UPD:
    case ARM::VLD3d16_UPD:
    case ARM::VLD3d32_UPD:
    case ARM::VLD1d64Twb_fixed:
    case ARM::VLD1d64Twb_register:
    case ARM::VLD3q8_UPD:
    case ARM::VLD3q16_UPD:
    case ARM::VLD3q32_UPD:
    case ARM::VLD4d8:
    case ARM::VLD4d16:
    case ARM::VLD4d32:
    case ARM::VLD1d64Q:
    case ARM::VLD4d8_UPD:
    case ARM::VLD4d16_UPD:
    case ARM::VLD4d32_UPD:
    case ARM::VLD1d64Qwb_fixed:
    case ARM::VLD1d64Qwb_register:
    case ARM::VLD4q8_UPD:
    case ARM::VLD4q16_UPD:
    case ARM::VLD4q32_UPD:
    case ARM::VLD1DUPq8:
    case ARM::VLD1DUPq16:
    case ARM::VLD1DUPq32:
    case ARM::VLD1DUPq8wb_fixed:
    case ARM::VLD1DUPq16wb_fixed:
    case ARM::VLD1DUPq32wb_fixed:
    case ARM::VLD1DUPq8wb_register:
    case ARM::VLD1DUPq16wb_register:
    case ARM::VLD1DUPq32wb_register:
    case ARM::VLD2DUPd8:
    case ARM::VLD2DUPd16:
    case ARM::VLD2DUPd32:
    case ARM::VLD2DUPd8wb_fixed:
    case ARM::VLD2DUPd16wb_fixed:
    case ARM::VLD2DUPd32wb_fixed:
    case ARM::VLD2DUPd8wb_register:
    case ARM::VLD2DUPd16wb_register:
    case ARM::VLD2DUPd32wb_register:
    case ARM::VLD4DUPd8:
    case ARM::VLD4DUPd16:
    case ARM::VLD4DUPd32:
    case ARM::VLD4DUPd8_UPD:
    case ARM::VLD4DUPd16_UPD:
    case ARM::VLD4DUPd32_UPD:
    case ARM::VLD1LNd8:
    case ARM::VLD1LNd16:
    case ARM::VLD1LNd32:
    case ARM::VLD1LNd8_UPD:
    case ARM::VLD1LNd16_UPD:
    case ARM::VLD1LNd32_UPD:
    case ARM::VLD2LNd8:
    case ARM::VLD2LNd16:
    case ARM::VLD2LNd32:
    case ARM::VLD2LNq16:
    case ARM::VLD2LNq32:
    case ARM::VLD2LNd8_UPD:
    case ARM::VLD2LNd16_UPD:
    case ARM::VLD2LNd32_UPD:
    case ARM::VLD2LNq16_UPD:
    case ARM::VLD2LNq32_UPD:
    case ARM::VLD4LNd8:
    case ARM::VLD4LNd16:
    case ARM::VLD4LNd32:
    case ARM::VLD4LNq16:
    case ARM::VLD4LNq32:
    case ARM::VLD4LNd8_UPD:
    case ARM::VLD4LNd16_UPD:
    case ARM::VLD4LNd32_UPD:
    case ARM::VLD4LNq16_UPD:
    case ARM::VLD4LNq32_UPD:
      // If the address is not 64-bit aligned, the latencies of these
      // instructions increases by one.
      ++Adjust;
      break;
    }
  }
  return Adjust;
}

std::optional<unsigned> ARMBaseInstrInfo::getOperandLatency(
    const InstrItineraryData *ItinData, const MachineInstr &DefMI,
    unsigned DefIdx, const MachineInstr &UseMI, unsigned UseIdx) const {
  // No operand latency. The caller may fall back to getInstrLatency.
  if (!ItinData || ItinData->isEmpty())
    return std::nullopt;

  const MachineOperand &DefMO = DefMI.getOperand(DefIdx);
  Register Reg = DefMO.getReg();

  const MachineInstr *ResolvedDefMI = &DefMI;
  unsigned DefAdj = 0;
  if (DefMI.isBundle())
    ResolvedDefMI =
        getBundledDefMI(&getRegisterInfo(), &DefMI, Reg, DefIdx, DefAdj);
  if (ResolvedDefMI->isCopyLike() || ResolvedDefMI->isInsertSubreg() ||
      ResolvedDefMI->isRegSequence() || ResolvedDefMI->isImplicitDef()) {
    return 1;
  }

  const MachineInstr *ResolvedUseMI = &UseMI;
  unsigned UseAdj = 0;
  if (UseMI.isBundle()) {
    ResolvedUseMI =
        getBundledUseMI(&getRegisterInfo(), UseMI, Reg, UseIdx, UseAdj);
    if (!ResolvedUseMI)
      return std::nullopt;
  }

  return getOperandLatencyImpl(
      ItinData, *ResolvedDefMI, DefIdx, ResolvedDefMI->getDesc(), DefAdj, DefMO,
      Reg, *ResolvedUseMI, UseIdx, ResolvedUseMI->getDesc(), UseAdj);
}

std::optional<unsigned> ARMBaseInstrInfo::getOperandLatencyImpl(
    const InstrItineraryData *ItinData, const MachineInstr &DefMI,
    unsigned DefIdx, const MCInstrDesc &DefMCID, unsigned DefAdj,
    const MachineOperand &DefMO, unsigned Reg, const MachineInstr &UseMI,
    unsigned UseIdx, const MCInstrDesc &UseMCID, unsigned UseAdj) const {
  if (Reg == ARM::CPSR) {
    if (DefMI.getOpcode() == ARM::FMSTAT) {
      // fpscr -> cpsr stalls over 20 cycles on A8 (and earlier?)
      return Subtarget.isLikeA9() ? 1 : 20;
    }

    // CPSR set and branch can be paired in the same cycle.
    if (UseMI.isBranch())
      return 0;

    // Otherwise it takes the instruction latency (generally one).
    unsigned Latency = getInstrLatency(ItinData, DefMI);

    // For Thumb2 and -Os, prefer scheduling CPSR setting instruction close to
    // its uses. Instructions which are otherwise scheduled between them may
    // incur a code size penalty (not able to use the CPSR setting 16-bit
    // instructions).
    if (Latency > 0 && Subtarget.isThumb2()) {
      const MachineFunction *MF = DefMI.getParent()->getParent();
      // FIXME: Use Function::hasOptSize().
      if (MF->getFunction().hasFnAttribute(Attribute::OptimizeForSize))
        --Latency;
    }
    return Latency;
  }

  if (DefMO.isImplicit() || UseMI.getOperand(UseIdx).isImplicit())
    return std::nullopt;

  unsigned DefAlign = DefMI.hasOneMemOperand()
                          ? (*DefMI.memoperands_begin())->getAlign().value()
                          : 0;
  unsigned UseAlign = UseMI.hasOneMemOperand()
                          ? (*UseMI.memoperands_begin())->getAlign().value()
                          : 0;

  // Get the itinerary's latency if possible, and handle variable_ops.
  std::optional<unsigned> Latency = getOperandLatency(
      ItinData, DefMCID, DefIdx, DefAlign, UseMCID, UseIdx, UseAlign);
  // Unable to find operand latency. The caller may resort to getInstrLatency.
  if (!Latency)
    return std::nullopt;

  // Adjust for IT block position.
  int Adj = DefAdj + UseAdj;

  // Adjust for dynamic def-side opcode variants not captured by the itinerary.
  Adj += adjustDefLatency(Subtarget, DefMI, DefMCID, DefAlign);
  if (Adj >= 0 || (int)*Latency > -Adj) {
    return *Latency + Adj;
  }
  // Return the itinerary latency, which may be zero but not less than zero.
  return Latency;
}

std::optional<unsigned>
ARMBaseInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                    SDNode *DefNode, unsigned DefIdx,
                                    SDNode *UseNode, unsigned UseIdx) const {
  if (!DefNode->isMachineOpcode())
    return 1;

  const MCInstrDesc &DefMCID = get(DefNode->getMachineOpcode());

  if (isZeroCost(DefMCID.Opcode))
    return 0;

  if (!ItinData || ItinData->isEmpty())
    return DefMCID.mayLoad() ? 3 : 1;

  if (!UseNode->isMachineOpcode()) {
    std::optional<unsigned> Latency =
        ItinData->getOperandCycle(DefMCID.getSchedClass(), DefIdx);
    int Adj = Subtarget.getPreISelOperandLatencyAdjustment();
    int Threshold = 1 + Adj;
    return !Latency || Latency <= (unsigned)Threshold ? 1 : *Latency - Adj;
  }

  const MCInstrDesc &UseMCID = get(UseNode->getMachineOpcode());
  auto *DefMN = cast<MachineSDNode>(DefNode);
  unsigned DefAlign = !DefMN->memoperands_empty()
                          ? (*DefMN->memoperands_begin())->getAlign().value()
                          : 0;
  auto *UseMN = cast<MachineSDNode>(UseNode);
  unsigned UseAlign = !UseMN->memoperands_empty()
                          ? (*UseMN->memoperands_begin())->getAlign().value()
                          : 0;
  std::optional<unsigned> Latency = getOperandLatency(
      ItinData, DefMCID, DefIdx, DefAlign, UseMCID, UseIdx, UseAlign);
  if (!Latency)
    return std::nullopt;

  if (Latency > 1U &&
      (Subtarget.isCortexA8() || Subtarget.isLikeA9() ||
       Subtarget.isCortexA7())) {
    // FIXME: Shifter op hack: no shift (i.e. [r +/- r]) or [r + r << 2]
    // variants are one cycle cheaper.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal = DefNode->getConstantOperandVal(2);
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (ShImm == 0 ||
          (ShImm == 2 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl))
        Latency = *Latency - 1;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs: {
      // Thumb2 mode: lsl only.
      unsigned ShAmt = DefNode->getConstantOperandVal(2);
      if (ShAmt == 0 || ShAmt == 2)
        Latency = *Latency - 1;
      break;
    }
    }
  } else if (DefIdx == 0 && Latency > 2U && Subtarget.isSwift()) {
    // FIXME: Properly handle all of the latency adjustments for address
    // writeback.
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::LDRrs:
    case ARM::LDRBrs: {
      unsigned ShOpVal = DefNode->getConstantOperandVal(2);
      unsigned ShImm = ARM_AM::getAM2Offset(ShOpVal);
      if (ShImm == 0 ||
          ((ShImm == 1 || ShImm == 2 || ShImm == 3) &&
           ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsl))
        Latency = *Latency - 2;
      else if (ShImm == 1 && ARM_AM::getAM2ShiftOpc(ShOpVal) == ARM_AM::lsr)
        Latency = *Latency - 1;
      break;
    }
    case ARM::t2LDRs:
    case ARM::t2LDRBs:
    case ARM::t2LDRHs:
    case ARM::t2LDRSHs:
      // Thumb2 mode: lsl 0-3 only.
      Latency = *Latency - 2;
      break;
    }
  }

  if (DefAlign < 8 && Subtarget.checkVLDnAccessAlignment())
    switch (DefMCID.getOpcode()) {
    default: break;
    case ARM::VLD1q8:
    case ARM::VLD1q16:
    case ARM::VLD1q32:
    case ARM::VLD1q64:
    case ARM::VLD1q8wb_register:
    case ARM::VLD1q16wb_register:
    case ARM::VLD1q32wb_register:
    case ARM::VLD1q64wb_register:
    case ARM::VLD1q8wb_fixed:
    case ARM::VLD1q16wb_fixed:
    case ARM::VLD1q32wb_fixed:
    case ARM::VLD1q64wb_fixed:
    case ARM::VLD2d8:
    case ARM::VLD2d16:
    case ARM::VLD2d32:
    case ARM::VLD2q8Pseudo:
    case ARM::VLD2q16Pseudo:
    case ARM::VLD2q32Pseudo:
    case ARM::VLD2d8wb_fixed:
    case ARM::VLD2d16wb_fixed:
    case ARM::VLD2d32wb_fixed:
    case ARM::VLD2q8PseudoWB_fixed:
    case ARM::VLD2q16PseudoWB_fixed:
    case ARM::VLD2q32PseudoWB_fixed:
    case ARM::VLD2d8wb_register:
    case ARM::VLD2d16wb_register:
    case ARM::VLD2d32wb_register:
    case ARM::VLD2q8PseudoWB_register:
    case ARM::VLD2q16PseudoWB_register:
    case ARM::VLD2q32PseudoWB_register:
    case ARM::VLD3d8Pseudo:
    case ARM::VLD3d16Pseudo:
    case ARM::VLD3d32Pseudo:
    case ARM::VLD1d8TPseudo:
    case ARM::VLD1d16TPseudo:
    case ARM::VLD1d32TPseudo:
    case ARM::VLD1d64TPseudo:
    case ARM::VLD1d64TPseudoWB_fixed:
    case ARM::VLD1d64TPseudoWB_register:
    case ARM::VLD3d8Pseudo_UPD:
    case ARM::VLD3d16Pseudo_UPD:
    case ARM::VLD3d32Pseudo_UPD:
    case ARM::VLD3q8Pseudo_UPD:
    case ARM::VLD3q16Pseudo_UPD:
    case ARM::VLD3q32Pseudo_UPD:
    case ARM::VLD3q8oddPseudo:
    case ARM::VLD3q16oddPseudo:
    case ARM::VLD3q32oddPseudo:
    case ARM::VLD3q8oddPseudo_UPD:
    case ARM::VLD3q16oddPseudo_UPD:
    case ARM::VLD3q32oddPseudo_UPD:
    case ARM::VLD4d8Pseudo:
    case ARM::VLD4d16Pseudo:
    case ARM::VLD4d32Pseudo:
    case ARM::VLD1d8QPseudo:
    case ARM::VLD1d16QPseudo:
    case ARM::VLD1d32QPseudo:
    case ARM::VLD1d64QPseudo:
    case ARM::VLD1d64QPseudoWB_fixed:
    case ARM::VLD1d64QPseudoWB_register:
    case ARM::VLD1q8HighQPseudo:
    case ARM::VLD1q8LowQPseudo_UPD:
    case ARM::VLD1q8HighTPseudo:
    case ARM::VLD1q8LowTPseudo_UPD:
    case ARM::VLD1q16HighQPseudo:
    case ARM::VLD1q16LowQPseudo_UPD:
    case ARM::VLD1q16HighTPseudo:
    case ARM::VLD1q16LowTPseudo_UPD:
    case ARM::VLD1q32HighQPseudo:
    case ARM::VLD1q32LowQPseudo_UPD:
    case ARM::VLD1q32HighTPseudo:
    case ARM::VLD1q32LowTPseudo_UPD:
    case ARM::VLD1q64HighQPseudo:
    case ARM::VLD1q64LowQPseudo_UPD:
    case ARM::VLD1q64HighTPseudo:
    case ARM::VLD1q64LowTPseudo_UPD:
    case ARM::VLD4d8Pseudo_UPD:
    case ARM::VLD4d16Pseudo_UPD:
    case ARM::VLD4d32Pseudo_UPD:
    case ARM::VLD4q8Pseudo_UPD:
    case ARM::VLD4q16Pseudo_UPD:
    case ARM::VLD4q32Pseudo_UPD:
    case ARM::VLD4q8oddPseudo:
    case ARM::VLD4q16oddPseudo:
    case ARM::VLD4q32oddPseudo:
    case ARM::VLD4q8oddPseudo_UPD:
    case ARM::VLD4q16oddPseudo_UPD:
    case ARM::VLD4q32oddPseudo_UPD:
    case ARM::VLD1DUPq8:
    case ARM::VLD1DUPq16:
    case ARM::VLD1DUPq32:
    case ARM::VLD1DUPq8wb_fixed:
    case ARM::VLD1DUPq16wb_fixed:
    case ARM::VLD1DUPq32wb_fixed:
    case ARM::VLD1DUPq8wb_register:
    case ARM::VLD1DUPq16wb_register:
    case ARM::VLD1DUPq32wb_register:
    case ARM::VLD2DUPd8:
    case ARM::VLD2DUPd16:
    case ARM::VLD2DUPd32:
    case ARM::VLD2DUPd8wb_fixed:
    case ARM::VLD2DUPd16wb_fixed:
    case ARM::VLD2DUPd32wb_fixed:
    case ARM::VLD2DUPd8wb_register:
    case ARM::VLD2DUPd16wb_register:
    case ARM::VLD2DUPd32wb_register:
    case ARM::VLD2DUPq8EvenPseudo:
    case ARM::VLD2DUPq8OddPseudo:
    case ARM::VLD2DUPq16EvenPseudo:
    case ARM::VLD2DUPq16OddPseudo:
    case ARM::VLD2DUPq32EvenPseudo:
    case ARM::VLD2DUPq32OddPseudo:
    case ARM::VLD3DUPq8EvenPseudo:
    case ARM::VLD3DUPq8OddPseudo:
    case ARM::VLD3DUPq16EvenPseudo:
    case ARM::VLD3DUPq16OddPseudo:
    case ARM::VLD3DUPq32EvenPseudo:
    case ARM::VLD3DUPq32OddPseudo:
    case ARM::VLD4DUPd8Pseudo:
    case ARM::VLD4DUPd16Pseudo:
    case ARM::VLD4DUPd32Pseudo:
    case ARM::VLD4DUPd8Pseudo_UPD:
    case ARM::VLD4DUPd16Pseudo_UPD:
    case ARM::VLD4DUPd32Pseudo_UPD:
    case ARM::VLD4DUPq8EvenPseudo:
    case ARM::VLD4DUPq8OddPseudo:
    case ARM::VLD4DUPq16EvenPseudo:
    case ARM::VLD4DUPq16OddPseudo:
    case ARM::VLD4DUPq32EvenPseudo:
    case ARM::VLD4DUPq32OddPseudo:
    case ARM::VLD1LNq8Pseudo:
    case ARM::VLD1LNq16Pseudo:
    case ARM::VLD1LNq32Pseudo:
    case ARM::VLD1LNq8Pseudo_UPD:
    case ARM::VLD1LNq16Pseudo_UPD:
    case ARM::VLD1LNq32Pseudo_UPD:
    case ARM::VLD2LNd8Pseudo:
    case ARM::VLD2LNd16Pseudo:
    case ARM::VLD2LNd32Pseudo:
    case ARM::VLD2LNq16Pseudo:
    case ARM::VLD2LNq32Pseudo:
    case ARM::VLD2LNd8Pseudo_UPD:
    case ARM::VLD2LNd16Pseudo_UPD:
    case ARM::VLD2LNd32Pseudo_UPD:
    case ARM::VLD2LNq16Pseudo_UPD:
    case ARM::VLD2LNq32Pseudo_UPD:
    case ARM::VLD4LNd8Pseudo:
    case ARM::VLD4LNd16Pseudo:
    case ARM::VLD4LNd32Pseudo:
    case ARM::VLD4LNq16Pseudo:
    case ARM::VLD4LNq32Pseudo:
    case ARM::VLD4LNd8Pseudo_UPD:
    case ARM::VLD4LNd16Pseudo_UPD:
    case ARM::VLD4LNd32Pseudo_UPD:
    case ARM::VLD4LNq16Pseudo_UPD:
    case ARM::VLD4LNq32Pseudo_UPD:
      // If the address is not 64-bit aligned, the latencies of these
      // instructions increases by one.
      Latency = *Latency + 1;
      break;
    }

  return Latency;
}

unsigned ARMBaseInstrInfo::getPredicationCost(const MachineInstr &MI) const {
  if (MI.isCopyLike() || MI.isInsertSubreg() || MI.isRegSequence() ||
      MI.isImplicitDef())
    return 0;

  if (MI.isBundle())
    return 0;

  const MCInstrDesc &MCID = MI.getDesc();

  if (MCID.isCall() || (MCID.hasImplicitDefOfPhysReg(ARM::CPSR) &&
                        !Subtarget.cheapPredicableCPSRDef())) {
    // When predicated, CPSR is an additional source operand for CPSR updating
    // instructions, this apparently increases their latencies.
    return 1;
  }
  return 0;
}

unsigned ARMBaseInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                           const MachineInstr &MI,
                                           unsigned *PredCost) const {
  if (MI.isCopyLike() || MI.isInsertSubreg() || MI.isRegSequence() ||
      MI.isImplicitDef())
    return 1;

  // An instruction scheduler typically runs on unbundled instructions, however
  // other passes may query the latency of a bundled instruction.
  if (MI.isBundle()) {
    unsigned Latency = 0;
    MachineBasicBlock::const_instr_iterator I = MI.getIterator();
    MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
    while (++I != E && I->isInsideBundle()) {
      if (I->getOpcode() != ARM::t2IT)
        Latency += getInstrLatency(ItinData, *I, PredCost);
    }
    return Latency;
  }

  const MCInstrDesc &MCID = MI.getDesc();
  if (PredCost && (MCID.isCall() || (MCID.hasImplicitDefOfPhysReg(ARM::CPSR) &&
                                     !Subtarget.cheapPredicableCPSRDef()))) {
    // When predicated, CPSR is an additional source operand for CPSR updating
    // instructions, this apparently increases their latencies.
    *PredCost = 1;
  }
  // Be sure to call getStageLatency for an empty itinerary in case it has a
  // valid MinLatency property.
  if (!ItinData)
    return MI.mayLoad() ? 3 : 1;

  unsigned Class = MCID.getSchedClass();

  // For instructions with variable uops, use uops as latency.
  if (!ItinData->isEmpty() && ItinData->getNumMicroOps(Class) < 0)
    return getNumMicroOps(ItinData, MI);

  // For the common case, fall back on the itinerary's latency.
  unsigned Latency = ItinData->getStageLatency(Class);

  // Adjust for dynamic def-side opcode variants not captured by the itinerary.
  unsigned DefAlign =
      MI.hasOneMemOperand() ? (*MI.memoperands_begin())->getAlign().value() : 0;
  int Adj = adjustDefLatency(Subtarget, MI, MCID, DefAlign);
  if (Adj >= 0 || (int)Latency > -Adj) {
    return Latency + Adj;
  }
  return Latency;
}

unsigned ARMBaseInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                           SDNode *Node) const {
  if (!Node->isMachineOpcode())
    return 1;

  if (!ItinData || ItinData->isEmpty())
    return 1;

  unsigned Opcode = Node->getMachineOpcode();
  switch (Opcode) {
  default:
    return ItinData->getStageLatency(get(Opcode).getSchedClass());
  case ARM::VLDMQIA:
  case ARM::VSTMQIA:
    return 2;
  }
}

bool ARMBaseInstrInfo::hasHighOperandLatency(const TargetSchedModel &SchedModel,
                                             const MachineRegisterInfo *MRI,
                                             const MachineInstr &DefMI,
                                             unsigned DefIdx,
                                             const MachineInstr &UseMI,
                                             unsigned UseIdx) const {
  unsigned DDomain = DefMI.getDesc().TSFlags & ARMII::DomainMask;
  unsigned UDomain = UseMI.getDesc().TSFlags & ARMII::DomainMask;
  if (Subtarget.nonpipelinedVFP() &&
      (DDomain == ARMII::DomainVFP || UDomain == ARMII::DomainVFP))
    return true;

  // Hoist VFP / NEON instructions with 4 or higher latency.
  unsigned Latency =
      SchedModel.computeOperandLatency(&DefMI, DefIdx, &UseMI, UseIdx);
  if (Latency <= 3)
    return false;
  return DDomain == ARMII::DomainVFP || DDomain == ARMII::DomainNEON ||
         UDomain == ARMII::DomainVFP || UDomain == ARMII::DomainNEON;
}

bool ARMBaseInstrInfo::hasLowDefLatency(const TargetSchedModel &SchedModel,
                                        const MachineInstr &DefMI,
                                        unsigned DefIdx) const {
  const InstrItineraryData *ItinData = SchedModel.getInstrItineraries();
  if (!ItinData || ItinData->isEmpty())
    return false;

  unsigned DDomain = DefMI.getDesc().TSFlags & ARMII::DomainMask;
  if (DDomain == ARMII::DomainGeneral) {
    unsigned DefClass = DefMI.getDesc().getSchedClass();
    std::optional<unsigned> DefCycle =
        ItinData->getOperandCycle(DefClass, DefIdx);
    return DefCycle && DefCycle <= 2U;
  }
  return false;
}

bool ARMBaseInstrInfo::verifyInstruction(const MachineInstr &MI,
                                         StringRef &ErrInfo) const {
  if (convertAddSubFlagsOpcode(MI.getOpcode())) {
    ErrInfo = "Pseudo flag setting opcodes only exist in Selection DAG";
    return false;
  }
  if (MI.getOpcode() == ARM::tMOVr && !Subtarget.hasV6Ops()) {
    // Make sure we don't generate a lo-lo mov that isn't supported.
    if (!ARM::hGPRRegClass.contains(MI.getOperand(0).getReg()) &&
        !ARM::hGPRRegClass.contains(MI.getOperand(1).getReg())) {
      ErrInfo = "Non-flag-setting Thumb1 mov is v6-only";
      return false;
    }
  }
  if (MI.getOpcode() == ARM::tPUSH ||
      MI.getOpcode() == ARM::tPOP ||
      MI.getOpcode() == ARM::tPOP_RET) {
    for (const MachineOperand &MO : llvm::drop_begin(MI.operands(), 2)) {
      if (MO.isImplicit() || !MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (Reg < ARM::R0 || Reg > ARM::R7) {
        if (!(MI.getOpcode() == ARM::tPUSH && Reg == ARM::LR) &&
            !(MI.getOpcode() == ARM::tPOP_RET && Reg == ARM::PC)) {
          ErrInfo = "Unsupported register in Thumb1 push/pop";
          return false;
        }
      }
    }
  }
  if (MI.getOpcode() == ARM::MVE_VMOV_q_rr) {
    assert(MI.getOperand(4).isImm() && MI.getOperand(5).isImm());
    if ((MI.getOperand(4).getImm() != 2 && MI.getOperand(4).getImm() != 3) ||
        MI.getOperand(4).getImm() != MI.getOperand(5).getImm() + 2) {
      ErrInfo = "Incorrect array index for MVE_VMOV_q_rr";
      return false;
    }
  }

  // Check the address model by taking the first Imm operand and checking it is
  // legal for that addressing mode.
  ARMII::AddrMode AddrMode =
      (ARMII::AddrMode)(MI.getDesc().TSFlags & ARMII::AddrModeMask);
  switch (AddrMode) {
  default:
    break;
  case ARMII::AddrModeT2_i7:
  case ARMII::AddrModeT2_i7s2:
  case ARMII::AddrModeT2_i7s4:
  case ARMII::AddrModeT2_i8:
  case ARMII::AddrModeT2_i8pos:
  case ARMII::AddrModeT2_i8neg:
  case ARMII::AddrModeT2_i8s4:
  case ARMII::AddrModeT2_i12: {
    uint32_t Imm = 0;
    for (auto Op : MI.operands()) {
      if (Op.isImm()) {
        Imm = Op.getImm();
        break;
      }
    }
    if (!isLegalAddressImm(MI.getOpcode(), Imm, this)) {
      ErrInfo = "Incorrect AddrMode Imm for instruction";
      return false;
    }
    break;
  }
  }
  return true;
}

void ARMBaseInstrInfo::expandLoadStackGuardBase(MachineBasicBlock::iterator MI,
                                                unsigned LoadImmOpc,
                                                unsigned LoadOpc) const {
  assert(!Subtarget.isROPI() && !Subtarget.isRWPI() &&
         "ROPI/RWPI not currently supported with stack guard");

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  Register Reg = MI->getOperand(0).getReg();
  MachineInstrBuilder MIB;
  unsigned int Offset = 0;

  if (LoadImmOpc == ARM::MRC || LoadImmOpc == ARM::t2MRC) {
    assert(!Subtarget.isReadTPSoft() &&
           "TLS stack protector requires hardware TLS register");

    BuildMI(MBB, MI, DL, get(LoadImmOpc), Reg)
        .addImm(15)
        .addImm(0)
        .addImm(13)
        .addImm(0)
        .addImm(3)
        .add(predOps(ARMCC::AL));

    Module &M = *MBB.getParent()->getFunction().getParent();
    Offset = M.getStackProtectorGuardOffset();
    if (Offset & ~0xfffU) {
      // The offset won't fit in the LDR's 12-bit immediate field, so emit an
      // extra ADD to cover the delta. This gives us a guaranteed 8 additional
      // bits, resulting in a range of 0 to +1 MiB for the guard offset.
      unsigned AddOpc = (LoadImmOpc == ARM::MRC) ? ARM::ADDri : ARM::t2ADDri;
      BuildMI(MBB, MI, DL, get(AddOpc), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(Offset & ~0xfffU)
          .add(predOps(ARMCC::AL))
          .addReg(0);
      Offset &= 0xfffU;
    }
  } else {
    const GlobalValue *GV =
        cast<GlobalValue>((*MI->memoperands_begin())->getValue());
    bool IsIndirect = Subtarget.isGVIndirectSymbol(GV);

    unsigned TargetFlags = ARMII::MO_NO_FLAG;
    if (Subtarget.isTargetMachO()) {
      TargetFlags |= ARMII::MO_NONLAZY;
    } else if (Subtarget.isTargetCOFF()) {
      if (GV->hasDLLImportStorageClass())
        TargetFlags |= ARMII::MO_DLLIMPORT;
      else if (IsIndirect)
        TargetFlags |= ARMII::MO_COFFSTUB;
    } else if (IsIndirect) {
      TargetFlags |= ARMII::MO_GOT;
    }

    if (LoadImmOpc == ARM::tMOVi32imm) { // Thumb-1 execute-only
      Register CPSRSaveReg = ARM::R12; // Use R12 as scratch register
      auto APSREncoding =
          ARMSysReg::lookupMClassSysRegByName("apsr_nzcvq")->Encoding;
      BuildMI(MBB, MI, DL, get(ARM::t2MRS_M), CPSRSaveReg)
          .addImm(APSREncoding)
          .add(predOps(ARMCC::AL));
      BuildMI(MBB, MI, DL, get(LoadImmOpc), Reg)
          .addGlobalAddress(GV, 0, TargetFlags);
      BuildMI(MBB, MI, DL, get(ARM::t2MSR_M))
          .addImm(APSREncoding)
          .addReg(CPSRSaveReg, RegState::Kill)
          .add(predOps(ARMCC::AL));
    } else {
      BuildMI(MBB, MI, DL, get(LoadImmOpc), Reg)
          .addGlobalAddress(GV, 0, TargetFlags);
    }

    if (IsIndirect) {
      MIB = BuildMI(MBB, MI, DL, get(LoadOpc), Reg);
      MIB.addReg(Reg, RegState::Kill).addImm(0);
      auto Flags = MachineMemOperand::MOLoad |
                   MachineMemOperand::MODereferenceable |
                   MachineMemOperand::MOInvariant;
      MachineMemOperand *MMO = MBB.getParent()->getMachineMemOperand(
          MachinePointerInfo::getGOT(*MBB.getParent()), Flags, 4, Align(4));
      MIB.addMemOperand(MMO).add(predOps(ARMCC::AL));
    }
  }

  MIB = BuildMI(MBB, MI, DL, get(LoadOpc), Reg);
  MIB.addReg(Reg, RegState::Kill)
      .addImm(Offset)
      .cloneMemRefs(*MI)
      .add(predOps(ARMCC::AL));
}

bool
ARMBaseInstrInfo::isFpMLxInstruction(unsigned Opcode, unsigned &MulOpc,
                                     unsigned &AddSubOpc,
                                     bool &NegAcc, bool &HasLane) const {
  DenseMap<unsigned, unsigned>::const_iterator I = MLxEntryMap.find(Opcode);
  if (I == MLxEntryMap.end())
    return false;

  const ARM_MLxEntry &Entry = ARM_MLxTable[I->second];
  MulOpc = Entry.MulOpc;
  AddSubOpc = Entry.AddSubOpc;
  NegAcc = Entry.NegAcc;
  HasLane = Entry.HasLane;
  return true;
}

//===----------------------------------------------------------------------===//
// Execution domains.
//===----------------------------------------------------------------------===//
//
// Some instructions go down the NEON pipeline, some go down the VFP pipeline,
// and some can go down both.  The vmov instructions go down the VFP pipeline,
// but they can be changed to vorr equivalents that are executed by the NEON
// pipeline.
//
// We use the following execution domain numbering:
//
enum ARMExeDomain {
  ExeGeneric = 0,
  ExeVFP = 1,
  ExeNEON = 2
};

//
// Also see ARMInstrFormats.td and Domain* enums in ARMBaseInfo.h
//
std::pair<uint16_t, uint16_t>
ARMBaseInstrInfo::getExecutionDomain(const MachineInstr &MI) const {
  // If we don't have access to NEON instructions then we won't be able
  // to swizzle anything to the NEON domain. Check to make sure.
  if (Subtarget.hasNEON()) {
    // VMOVD, VMOVRS and VMOVSR are VFP instructions, but can be changed to NEON
    // if they are not predicated.
    if (MI.getOpcode() == ARM::VMOVD && !isPredicated(MI))
      return std::make_pair(ExeVFP, (1 << ExeVFP) | (1 << ExeNEON));

    // CortexA9 is particularly picky about mixing the two and wants these
    // converted.
    if (Subtarget.useNEONForFPMovs() && !isPredicated(MI) &&
        (MI.getOpcode() == ARM::VMOVRS || MI.getOpcode() == ARM::VMOVSR ||
         MI.getOpcode() == ARM::VMOVS))
      return std::make_pair(ExeVFP, (1 << ExeVFP) | (1 << ExeNEON));
  }
  // No other instructions can be swizzled, so just determine their domain.
  unsigned Domain = MI.getDesc().TSFlags & ARMII::DomainMask;

  if (Domain & ARMII::DomainNEON)
    return std::make_pair(ExeNEON, 0);

  // Certain instructions can go either way on Cortex-A8.
  // Treat them as NEON instructions.
  if ((Domain & ARMII::DomainNEONA8) && Subtarget.isCortexA8())
    return std::make_pair(ExeNEON, 0);

  if (Domain & ARMII::DomainVFP)
    return std::make_pair(ExeVFP, 0);

  return std::make_pair(ExeGeneric, 0);
}

static unsigned getCorrespondingDRegAndLane(const TargetRegisterInfo *TRI,
                                            unsigned SReg, unsigned &Lane) {
  unsigned DReg = TRI->getMatchingSuperReg(SReg, ARM::ssub_0, &ARM::DPRRegClass);
  Lane = 0;

  if (DReg != ARM::NoRegister)
   return DReg;

  Lane = 1;
  DReg = TRI->getMatchingSuperReg(SReg, ARM::ssub_1, &ARM::DPRRegClass);

  assert(DReg && "S-register with no D super-register?");
  return DReg;
}

/// getImplicitSPRUseForDPRUse - Given a use of a DPR register and lane,
/// set ImplicitSReg to a register number that must be marked as implicit-use or
/// zero if no register needs to be defined as implicit-use.
///
/// If the function cannot determine if an SPR should be marked implicit use or
/// not, it returns false.
///
/// This function handles cases where an instruction is being modified from taking
/// an SPR to a DPR[Lane]. A use of the DPR is being added, which may conflict
/// with an earlier def of an SPR corresponding to DPR[Lane^1] (i.e. the other
/// lane of the DPR).
///
/// If the other SPR is defined, an implicit-use of it should be added. Else,
/// (including the case where the DPR itself is defined), it should not.
///
static bool getImplicitSPRUseForDPRUse(const TargetRegisterInfo *TRI,
                                       MachineInstr &MI, unsigned DReg,
                                       unsigned Lane, unsigned &ImplicitSReg) {
  // If the DPR is defined or used already, the other SPR lane will be chained
  // correctly, so there is nothing to be done.
  if (MI.definesRegister(DReg, TRI) || MI.readsRegister(DReg, TRI)) {
    ImplicitSReg = 0;
    return true;
  }

  // Otherwise we need to go searching to see if the SPR is set explicitly.
  ImplicitSReg = TRI->getSubReg(DReg,
                                (Lane & 1) ? ARM::ssub_0 : ARM::ssub_1);
  MachineBasicBlock::LivenessQueryResult LQR =
      MI.getParent()->computeRegisterLiveness(TRI, ImplicitSReg, MI);

  if (LQR == MachineBasicBlock::LQR_Live)
    return true;
  else if (LQR == MachineBasicBlock::LQR_Unknown)
    return false;

  // If the register is known not to be live, there is no need to add an
  // implicit-use.
  ImplicitSReg = 0;
  return true;
}

void ARMBaseInstrInfo::setExecutionDomain(MachineInstr &MI,
                                          unsigned Domain) const {
  unsigned DstReg, SrcReg, DReg;
  unsigned Lane;
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("cannot handle opcode!");
    break;
  case ARM::VMOVD:
    if (Domain != ExeNEON)
      break;

    // Zap the predicate operands.
    assert(!isPredicated(MI) && "Cannot predicate a VORRd");

    // Make sure we've got NEON instructions.
    assert(Subtarget.hasNEON() && "VORRd requires NEON");

    // Source instruction is %DDst = VMOVD %DSrc, 14, %noreg (; implicits)
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();

    for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
      MI.removeOperand(i - 1);

    // Change to a %DDst = VORRd %DSrc, %DSrc, 14, %noreg (; implicits)
    MI.setDesc(get(ARM::VORRd));
    MIB.addReg(DstReg, RegState::Define)
        .addReg(SrcReg)
        .addReg(SrcReg)
        .add(predOps(ARMCC::AL));
    break;
  case ARM::VMOVRS:
    if (Domain != ExeNEON)
      break;
    assert(!isPredicated(MI) && "Cannot predicate a VGETLN");

    // Source instruction is %RDst = VMOVRS %SSrc, 14, %noreg (; implicits)
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();

    for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
      MI.removeOperand(i - 1);

    DReg = getCorrespondingDRegAndLane(TRI, SrcReg, Lane);

    // Convert to %RDst = VGETLNi32 %DSrc, Lane, 14, %noreg (; imps)
    // Note that DSrc has been widened and the other lane may be undef, which
    // contaminates the entire register.
    MI.setDesc(get(ARM::VGETLNi32));
    MIB.addReg(DstReg, RegState::Define)
        .addReg(DReg, RegState::Undef)
        .addImm(Lane)
        .add(predOps(ARMCC::AL));

    // The old source should be an implicit use, otherwise we might think it
    // was dead before here.
    MIB.addReg(SrcReg, RegState::Implicit);
    break;
  case ARM::VMOVSR: {
    if (Domain != ExeNEON)
      break;
    assert(!isPredicated(MI) && "Cannot predicate a VSETLN");

    // Source instruction is %SDst = VMOVSR %RSrc, 14, %noreg (; implicits)
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();

    DReg = getCorrespondingDRegAndLane(TRI, DstReg, Lane);

    unsigned ImplicitSReg;
    if (!getImplicitSPRUseForDPRUse(TRI, MI, DReg, Lane, ImplicitSReg))
      break;

    for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
      MI.removeOperand(i - 1);

    // Convert to %DDst = VSETLNi32 %DDst, %RSrc, Lane, 14, %noreg (; imps)
    // Again DDst may be undefined at the beginning of this instruction.
    MI.setDesc(get(ARM::VSETLNi32));
    MIB.addReg(DReg, RegState::Define)
        .addReg(DReg, getUndefRegState(!MI.readsRegister(DReg, TRI)))
        .addReg(SrcReg)
        .addImm(Lane)
        .add(predOps(ARMCC::AL));

    // The narrower destination must be marked as set to keep previous chains
    // in place.
    MIB.addReg(DstReg, RegState::Define | RegState::Implicit);
    if (ImplicitSReg != 0)
      MIB.addReg(ImplicitSReg, RegState::Implicit);
    break;
    }
    case ARM::VMOVS: {
      if (Domain != ExeNEON)
        break;

      // Source instruction is %SDst = VMOVS %SSrc, 14, %noreg (; implicits)
      DstReg = MI.getOperand(0).getReg();
      SrcReg = MI.getOperand(1).getReg();

      unsigned DstLane = 0, SrcLane = 0, DDst, DSrc;
      DDst = getCorrespondingDRegAndLane(TRI, DstReg, DstLane);
      DSrc = getCorrespondingDRegAndLane(TRI, SrcReg, SrcLane);

      unsigned ImplicitSReg;
      if (!getImplicitSPRUseForDPRUse(TRI, MI, DSrc, SrcLane, ImplicitSReg))
        break;

      for (unsigned i = MI.getDesc().getNumOperands(); i; --i)
        MI.removeOperand(i - 1);

      if (DSrc == DDst) {
        // Destination can be:
        //     %DDst = VDUPLN32d %DDst, Lane, 14, %noreg (; implicits)
        MI.setDesc(get(ARM::VDUPLN32d));
        MIB.addReg(DDst, RegState::Define)
            .addReg(DDst, getUndefRegState(!MI.readsRegister(DDst, TRI)))
            .addImm(SrcLane)
            .add(predOps(ARMCC::AL));

        // Neither the source or the destination are naturally represented any
        // more, so add them in manually.
        MIB.addReg(DstReg, RegState::Implicit | RegState::Define);
        MIB.addReg(SrcReg, RegState::Implicit);
        if (ImplicitSReg != 0)
          MIB.addReg(ImplicitSReg, RegState::Implicit);
        break;
      }

      // In general there's no single instruction that can perform an S <-> S
      // move in NEON space, but a pair of VEXT instructions *can* do the
      // job. It turns out that the VEXTs needed will only use DSrc once, with
      // the position based purely on the combination of lane-0 and lane-1
      // involved. For example
      //     vmov s0, s2 -> vext.32 d0, d0, d1, #1  vext.32 d0, d0, d0, #1
      //     vmov s1, s3 -> vext.32 d0, d1, d0, #1  vext.32 d0, d0, d0, #1
      //     vmov s0, s3 -> vext.32 d0, d0, d0, #1  vext.32 d0, d1, d0, #1
      //     vmov s1, s2 -> vext.32 d0, d0, d0, #1  vext.32 d0, d0, d1, #1
      //
      // Pattern of the MachineInstrs is:
      //     %DDst = VEXTd32 %DSrc1, %DSrc2, Lane, 14, %noreg (;implicits)
      MachineInstrBuilder NewMIB;
      NewMIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(ARM::VEXTd32),
                       DDst);

      // On the first instruction, both DSrc and DDst may be undef if present.
      // Specifically when the original instruction didn't have them as an
      // <imp-use>.
      unsigned CurReg = SrcLane == 1 && DstLane == 1 ? DSrc : DDst;
      bool CurUndef = !MI.readsRegister(CurReg, TRI);
      NewMIB.addReg(CurReg, getUndefRegState(CurUndef));

      CurReg = SrcLane == 0 && DstLane == 0 ? DSrc : DDst;
      CurUndef = !MI.readsRegister(CurReg, TRI);
      NewMIB.addReg(CurReg, getUndefRegState(CurUndef))
            .addImm(1)
            .add(predOps(ARMCC::AL));

      if (SrcLane == DstLane)
        NewMIB.addReg(SrcReg, RegState::Implicit);

      MI.setDesc(get(ARM::VEXTd32));
      MIB.addReg(DDst, RegState::Define);

      // On the second instruction, DDst has definitely been defined above, so
      // it is not undef. DSrc, if present, can be undef as above.
      CurReg = SrcLane == 1 && DstLane == 0 ? DSrc : DDst;
      CurUndef = CurReg == DSrc && !MI.readsRegister(CurReg, TRI);
      MIB.addReg(CurReg, getUndefRegState(CurUndef));

      CurReg = SrcLane == 0 && DstLane == 1 ? DSrc : DDst;
      CurUndef = CurReg == DSrc && !MI.readsRegister(CurReg, TRI);
      MIB.addReg(CurReg, getUndefRegState(CurUndef))
         .addImm(1)
         .add(predOps(ARMCC::AL));

      if (SrcLane != DstLane)
        MIB.addReg(SrcReg, RegState::Implicit);

      // As before, the original destination is no longer represented, add it
      // implicitly.
      MIB.addReg(DstReg, RegState::Define | RegState::Implicit);
      if (ImplicitSReg != 0)
        MIB.addReg(ImplicitSReg, RegState::Implicit);
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// Partial register updates
//===----------------------------------------------------------------------===//
//
// Swift renames NEON registers with 64-bit granularity.  That means any
// instruction writing an S-reg implicitly reads the containing D-reg.  The
// problem is mostly avoided by translating f32 operations to v2f32 operations
// on D-registers, but f32 loads are still a problem.
//
// These instructions can load an f32 into a NEON register:
//
// VLDRS - Only writes S, partial D update.
// VLD1LNd32 - Writes all D-regs, explicit partial D update, 2 uops.
// VLD1DUPd32 - Writes all D-regs, no partial reg update, 2 uops.
//
// FCONSTD can be used as a dependency-breaking instruction.
unsigned ARMBaseInstrInfo::getPartialRegUpdateClearance(
    const MachineInstr &MI, unsigned OpNum,
    const TargetRegisterInfo *TRI) const {
  auto PartialUpdateClearance = Subtarget.getPartialUpdateClearance();
  if (!PartialUpdateClearance)
    return 0;

  assert(TRI && "Need TRI instance");

  const MachineOperand &MO = MI.getOperand(OpNum);
  if (MO.readsReg())
    return 0;
  Register Reg = MO.getReg();
  int UseOp = -1;

  switch (MI.getOpcode()) {
  // Normal instructions writing only an S-register.
  case ARM::VLDRS:
  case ARM::FCONSTS:
  case ARM::VMOVSR:
  case ARM::VMOVv8i8:
  case ARM::VMOVv4i16:
  case ARM::VMOVv2i32:
  case ARM::VMOVv2f32:
  case ARM::VMOVv1i64:
    UseOp = MI.findRegisterUseOperandIdx(Reg, TRI, false);
    break;

    // Explicitly reads the dependency.
  case ARM::VLD1LNd32:
    UseOp = 3;
    break;
  default:
    return 0;
  }

  // If this instruction actually reads a value from Reg, there is no unwanted
  // dependency.
  if (UseOp != -1 && MI.getOperand(UseOp).readsReg())
    return 0;

  // We must be able to clobber the whole D-reg.
  if (Reg.isVirtual()) {
    // Virtual register must be a def undef foo:ssub_0 operand.
    if (!MO.getSubReg() || MI.readsVirtualRegister(Reg))
      return 0;
  } else if (ARM::SPRRegClass.contains(Reg)) {
    // Physical register: MI must define the full D-reg.
    unsigned DReg = TRI->getMatchingSuperReg(Reg, ARM::ssub_0,
                                             &ARM::DPRRegClass);
    if (!DReg || !MI.definesRegister(DReg, TRI))
      return 0;
  }

  // MI has an unwanted D-register dependency.
  // Avoid defs in the previous N instructrions.
  return PartialUpdateClearance;
}

// Break a partial register dependency after getPartialRegUpdateClearance
// returned non-zero.
void ARMBaseInstrInfo::breakPartialRegDependency(
    MachineInstr &MI, unsigned OpNum, const TargetRegisterInfo *TRI) const {
  assert(OpNum < MI.getDesc().getNumDefs() && "OpNum is not a def");
  assert(TRI && "Need TRI instance");

  const MachineOperand &MO = MI.getOperand(OpNum);
  Register Reg = MO.getReg();
  assert(Reg.isPhysical() && "Can't break virtual register dependencies.");
  unsigned DReg = Reg;

  // If MI defines an S-reg, find the corresponding D super-register.
  if (ARM::SPRRegClass.contains(Reg)) {
    DReg = ARM::D0 + (Reg - ARM::S0) / 2;
    assert(TRI->isSuperRegister(Reg, DReg) && "Register enums broken");
  }

  assert(ARM::DPRRegClass.contains(DReg) && "Can only break D-reg deps");
  assert(MI.definesRegister(DReg, TRI) && "MI doesn't clobber full D-reg");

  // FIXME: In some cases, VLDRS can be changed to a VLD1DUPd32 which defines
  // the full D-register by loading the same value to both lanes.  The
  // instruction is micro-coded with 2 uops, so don't do this until we can
  // properly schedule micro-coded instructions.  The dispatcher stalls cause
  // too big regressions.

  // Insert the dependency-breaking FCONSTD before MI.
  // 96 is the encoding of 0.5, but the actual value doesn't matter here.
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(ARM::FCONSTD), DReg)
      .addImm(96)
      .add(predOps(ARMCC::AL));
  MI.addRegisterKilled(DReg, TRI, true);
}

bool ARMBaseInstrInfo::hasNOP() const {
  return Subtarget.hasFeature(ARM::HasV6KOps);
}

bool ARMBaseInstrInfo::isSwiftFastImmShift(const MachineInstr *MI) const {
  if (MI->getNumOperands() < 4)
    return true;
  unsigned ShOpVal = MI->getOperand(3).getImm();
  unsigned ShImm = ARM_AM::getSORegOffset(ShOpVal);
  // Swift supports faster shifts for: lsl 2, lsl 1, and lsr 1.
  if ((ShImm == 1 && ARM_AM::getSORegShOp(ShOpVal) == ARM_AM::lsr) ||
      ((ShImm == 1 || ShImm == 2) &&
       ARM_AM::getSORegShOp(ShOpVal) == ARM_AM::lsl))
    return true;

  return false;
}

bool ARMBaseInstrInfo::getRegSequenceLikeInputs(
    const MachineInstr &MI, unsigned DefIdx,
    SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const {
  assert(DefIdx < MI.getDesc().getNumDefs() && "Invalid definition index");
  assert(MI.isRegSequenceLike() && "Invalid kind of instruction");

  switch (MI.getOpcode()) {
  case ARM::VMOVDRR:
    // dX = VMOVDRR rY, rZ
    // is the same as:
    // dX = REG_SEQUENCE rY, ssub_0, rZ, ssub_1
    // Populate the InputRegs accordingly.
    // rY
    const MachineOperand *MOReg = &MI.getOperand(1);
    if (!MOReg->isUndef())
      InputRegs.push_back(RegSubRegPairAndIdx(MOReg->getReg(),
                                              MOReg->getSubReg(), ARM::ssub_0));
    // rZ
    MOReg = &MI.getOperand(2);
    if (!MOReg->isUndef())
      InputRegs.push_back(RegSubRegPairAndIdx(MOReg->getReg(),
                                              MOReg->getSubReg(), ARM::ssub_1));
    return true;
  }
  llvm_unreachable("Target dependent opcode missing");
}

bool ARMBaseInstrInfo::getExtractSubregLikeInputs(
    const MachineInstr &MI, unsigned DefIdx,
    RegSubRegPairAndIdx &InputReg) const {
  assert(DefIdx < MI.getDesc().getNumDefs() && "Invalid definition index");
  assert(MI.isExtractSubregLike() && "Invalid kind of instruction");

  switch (MI.getOpcode()) {
  case ARM::VMOVRRD:
    // rX, rY = VMOVRRD dZ
    // is the same as:
    // rX = EXTRACT_SUBREG dZ, ssub_0
    // rY = EXTRACT_SUBREG dZ, ssub_1
    const MachineOperand &MOReg = MI.getOperand(2);
    if (MOReg.isUndef())
      return false;
    InputReg.Reg = MOReg.getReg();
    InputReg.SubReg = MOReg.getSubReg();
    InputReg.SubIdx = DefIdx == 0 ? ARM::ssub_0 : ARM::ssub_1;
    return true;
  }
  llvm_unreachable("Target dependent opcode missing");
}

bool ARMBaseInstrInfo::getInsertSubregLikeInputs(
    const MachineInstr &MI, unsigned DefIdx, RegSubRegPair &BaseReg,
    RegSubRegPairAndIdx &InsertedReg) const {
  assert(DefIdx < MI.getDesc().getNumDefs() && "Invalid definition index");
  assert(MI.isInsertSubregLike() && "Invalid kind of instruction");

  switch (MI.getOpcode()) {
  case ARM::VSETLNi32:
  case ARM::MVE_VMOV_to_lane_32:
    // dX = VSETLNi32 dY, rZ, imm
    // qX = MVE_VMOV_to_lane_32 qY, rZ, imm
    const MachineOperand &MOBaseReg = MI.getOperand(1);
    const MachineOperand &MOInsertedReg = MI.getOperand(2);
    if (MOInsertedReg.isUndef())
      return false;
    const MachineOperand &MOIndex = MI.getOperand(3);
    BaseReg.Reg = MOBaseReg.getReg();
    BaseReg.SubReg = MOBaseReg.getSubReg();

    InsertedReg.Reg = MOInsertedReg.getReg();
    InsertedReg.SubReg = MOInsertedReg.getSubReg();
    InsertedReg.SubIdx = ARM::ssub_0 + MOIndex.getImm();
    return true;
  }
  llvm_unreachable("Target dependent opcode missing");
}

std::pair<unsigned, unsigned>
ARMBaseInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  const unsigned Mask = ARMII::MO_OPTION_MASK;
  return std::make_pair(TF & Mask, TF & ~Mask);
}

ArrayRef<std::pair<unsigned, const char *>>
ARMBaseInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace ARMII;

  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_LO16, "arm-lo16"},       {MO_HI16, "arm-hi16"},
      {MO_LO_0_7, "arm-lo-0-7"},   {MO_HI_0_7, "arm-hi-0-7"},
      {MO_LO_8_15, "arm-lo-8-15"}, {MO_HI_8_15, "arm-hi-8-15"},
  };
  return ArrayRef(TargetFlags);
}

ArrayRef<std::pair<unsigned, const char *>>
ARMBaseInstrInfo::getSerializableBitmaskMachineOperandTargetFlags() const {
  using namespace ARMII;

  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_COFFSTUB, "arm-coffstub"},
      {MO_GOT, "arm-got"},
      {MO_SBREL, "arm-sbrel"},
      {MO_DLLIMPORT, "arm-dllimport"},
      {MO_SECREL, "arm-secrel"},
      {MO_NONLAZY, "arm-nonlazy"}};
  return ArrayRef(TargetFlags);
}

std::optional<RegImmPair>
ARMBaseInstrInfo::isAddImmediate(const MachineInstr &MI, Register Reg) const {
  int Sign = 1;
  unsigned Opcode = MI.getOpcode();
  int64_t Offset = 0;

  // TODO: Handle cases where Reg is a super- or sub-register of the
  // destination register.
  const MachineOperand &Op0 = MI.getOperand(0);
  if (!Op0.isReg() || Reg != Op0.getReg())
    return std::nullopt;

  // We describe SUBri or ADDri instructions.
  if (Opcode == ARM::SUBri)
    Sign = -1;
  else if (Opcode != ARM::ADDri)
    return std::nullopt;

  // TODO: Third operand can be global address (usually some string). Since
  //       strings can be relocated we cannot calculate their offsets for
  //       now.
  if (!MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
    return std::nullopt;

  Offset = MI.getOperand(2).getImm() * Sign;
  return RegImmPair{MI.getOperand(1).getReg(), Offset};
}

bool llvm::registerDefinedBetween(unsigned Reg,
                                  MachineBasicBlock::iterator From,
                                  MachineBasicBlock::iterator To,
                                  const TargetRegisterInfo *TRI) {
  for (auto I = From; I != To; ++I)
    if (I->modifiesRegister(Reg, TRI))
      return true;
  return false;
}

MachineInstr *llvm::findCMPToFoldIntoCBZ(MachineInstr *Br,
                                         const TargetRegisterInfo *TRI) {
  // Search backwards to the instruction that defines CSPR. This may or not
  // be a CMP, we check that after this loop. If we find another instruction
  // that reads cpsr, we return nullptr.
  MachineBasicBlock::iterator CmpMI = Br;
  while (CmpMI != Br->getParent()->begin()) {
    --CmpMI;
    if (CmpMI->modifiesRegister(ARM::CPSR, TRI))
      break;
    if (CmpMI->readsRegister(ARM::CPSR, TRI))
      break;
  }

  // Check that this inst is a CMP r[0-7], #0 and that the register
  // is not redefined between the cmp and the br.
  if (CmpMI->getOpcode() != ARM::tCMPi8 && CmpMI->getOpcode() != ARM::t2CMPri)
    return nullptr;
  Register Reg = CmpMI->getOperand(0).getReg();
  Register PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(*CmpMI, PredReg);
  if (Pred != ARMCC::AL || CmpMI->getOperand(1).getImm() != 0)
    return nullptr;
  if (!isARMLowRegister(Reg))
    return nullptr;
  if (registerDefinedBetween(Reg, CmpMI->getNextNode(), Br, TRI))
    return nullptr;

  return &*CmpMI;
}

unsigned llvm::ConstantMaterializationCost(unsigned Val,
                                           const ARMSubtarget *Subtarget,
                                           bool ForCodesize) {
  if (Subtarget->isThumb()) {
    if (Val <= 255) // MOV
      return ForCodesize ? 2 : 1;
    if (Subtarget->hasV6T2Ops() && (Val <= 0xffff ||                    // MOV
                                    ARM_AM::getT2SOImmVal(Val) != -1 || // MOVW
                                    ARM_AM::getT2SOImmVal(~Val) != -1)) // MVN
      return ForCodesize ? 4 : 1;
    if (Val <= 510) // MOV + ADDi8
      return ForCodesize ? 4 : 2;
    if (~Val <= 255) // MOV + MVN
      return ForCodesize ? 4 : 2;
    if (ARM_AM::isThumbImmShiftedVal(Val)) // MOV + LSL
      return ForCodesize ? 4 : 2;
  } else {
    if (ARM_AM::getSOImmVal(Val) != -1) // MOV
      return ForCodesize ? 4 : 1;
    if (ARM_AM::getSOImmVal(~Val) != -1) // MVN
      return ForCodesize ? 4 : 1;
    if (Subtarget->hasV6T2Ops() && Val <= 0xffff) // MOVW
      return ForCodesize ? 4 : 1;
    if (ARM_AM::isSOImmTwoPartVal(Val)) // two instrs
      return ForCodesize ? 8 : 2;
    if (ARM_AM::isSOImmTwoPartValNeg(Val)) // two instrs
      return ForCodesize ? 8 : 2;
  }
  if (Subtarget->useMovt()) // MOVW + MOVT
    return ForCodesize ? 8 : 2;
  return ForCodesize ? 8 : 3; // Literal pool load
}

bool llvm::HasLowerConstantMaterializationCost(unsigned Val1, unsigned Val2,
                                               const ARMSubtarget *Subtarget,
                                               bool ForCodesize) {
  // Check with ForCodesize
  unsigned Cost1 = ConstantMaterializationCost(Val1, Subtarget, ForCodesize);
  unsigned Cost2 = ConstantMaterializationCost(Val2, Subtarget, ForCodesize);
  if (Cost1 < Cost2)
    return true;
  if (Cost1 > Cost2)
    return false;

  // If they are equal, try with !ForCodesize
  return ConstantMaterializationCost(Val1, Subtarget, !ForCodesize) <
         ConstantMaterializationCost(Val2, Subtarget, !ForCodesize);
}

/// Constants defining how certain sequences should be outlined.
/// This encompasses how an outlined function should be called, and what kind of
/// frame should be emitted for that outlined function.
///
/// \p MachineOutlinerTailCall implies that the function is being created from
/// a sequence of instructions ending in a return.
///
/// That is,
///
/// I1                                OUTLINED_FUNCTION:
/// I2    --> B OUTLINED_FUNCTION     I1
/// BX LR                             I2
///                                   BX LR
///
/// +-------------------------+--------+-----+
/// |                         | Thumb2 | ARM |
/// +-------------------------+--------+-----+
/// | Call overhead in Bytes  |      4 |   4 |
/// | Frame overhead in Bytes |      0 |   0 |
/// | Stack fixup required    |     No |  No |
/// +-------------------------+--------+-----+
///
/// \p MachineOutlinerThunk implies that the function is being created from
/// a sequence of instructions ending in a call. The outlined function is
/// called with a BL instruction, and the outlined function tail-calls the
/// original call destination.
///
/// That is,
///
/// I1                                OUTLINED_FUNCTION:
/// I2   --> BL OUTLINED_FUNCTION     I1
/// BL f                              I2
///                                   B f
///
/// +-------------------------+--------+-----+
/// |                         | Thumb2 | ARM |
/// +-------------------------+--------+-----+
/// | Call overhead in Bytes  |      4 |   4 |
/// | Frame overhead in Bytes |      0 |   0 |
/// | Stack fixup required    |     No |  No |
/// +-------------------------+--------+-----+
///
/// \p MachineOutlinerNoLRSave implies that the function should be called using
/// a BL instruction, but doesn't require LR to be saved and restored. This
/// happens when LR is known to be dead.
///
/// That is,
///
/// I1                                OUTLINED_FUNCTION:
/// I2 --> BL OUTLINED_FUNCTION       I1
/// I3                                I2
///                                   I3
///                                   BX LR
///
/// +-------------------------+--------+-----+
/// |                         | Thumb2 | ARM |
/// +-------------------------+--------+-----+
/// | Call overhead in Bytes  |      4 |   4 |
/// | Frame overhead in Bytes |      2 |   4 |
/// | Stack fixup required    |     No |  No |
/// +-------------------------+--------+-----+
///
/// \p MachineOutlinerRegSave implies that the function should be called with a
/// save and restore of LR to an available register. This allows us to avoid
/// stack fixups. Note that this outlining variant is compatible with the
/// NoLRSave case.
///
/// That is,
///
/// I1     Save LR                    OUTLINED_FUNCTION:
/// I2 --> BL OUTLINED_FUNCTION       I1
/// I3     Restore LR                 I2
///                                   I3
///                                   BX LR
///
/// +-------------------------+--------+-----+
/// |                         | Thumb2 | ARM |
/// +-------------------------+--------+-----+
/// | Call overhead in Bytes  |      8 |  12 |
/// | Frame overhead in Bytes |      2 |   4 |
/// | Stack fixup required    |     No |  No |
/// +-------------------------+--------+-----+
///
/// \p MachineOutlinerDefault implies that the function should be called with
/// a save and restore of LR to the stack.
///
/// That is,
///
/// I1     Save LR                    OUTLINED_FUNCTION:
/// I2 --> BL OUTLINED_FUNCTION       I1
/// I3     Restore LR                 I2
///                                   I3
///                                   BX LR
///
/// +-------------------------+--------+-----+
/// |                         | Thumb2 | ARM |
/// +-------------------------+--------+-----+
/// | Call overhead in Bytes  |      8 |  12 |
/// | Frame overhead in Bytes |      2 |   4 |
/// | Stack fixup required    |    Yes | Yes |
/// +-------------------------+--------+-----+

enum MachineOutlinerClass {
  MachineOutlinerTailCall,
  MachineOutlinerThunk,
  MachineOutlinerNoLRSave,
  MachineOutlinerRegSave,
  MachineOutlinerDefault
};

enum MachineOutlinerMBBFlags {
  LRUnavailableSomewhere = 0x2,
  HasCalls = 0x4,
  UnsafeRegsDead = 0x8
};

struct OutlinerCosts {
  int CallTailCall;
  int FrameTailCall;
  int CallThunk;
  int FrameThunk;
  int CallNoLRSave;
  int FrameNoLRSave;
  int CallRegSave;
  int FrameRegSave;
  int CallDefault;
  int FrameDefault;
  int SaveRestoreLROnStack;

  OutlinerCosts(const ARMSubtarget &target)
      : CallTailCall(target.isThumb() ? 4 : 4),
        FrameTailCall(target.isThumb() ? 0 : 0),
        CallThunk(target.isThumb() ? 4 : 4),
        FrameThunk(target.isThumb() ? 0 : 0),
        CallNoLRSave(target.isThumb() ? 4 : 4),
        FrameNoLRSave(target.isThumb() ? 2 : 4),
        CallRegSave(target.isThumb() ? 8 : 12),
        FrameRegSave(target.isThumb() ? 2 : 4),
        CallDefault(target.isThumb() ? 8 : 12),
        FrameDefault(target.isThumb() ? 2 : 4),
        SaveRestoreLROnStack(target.isThumb() ? 8 : 8) {}
};

Register
ARMBaseInstrInfo::findRegisterToSaveLRTo(outliner::Candidate &C) const {
  MachineFunction *MF = C.getMF();
  const TargetRegisterInfo &TRI = *MF->getSubtarget().getRegisterInfo();
  const ARMBaseRegisterInfo *ARI =
      static_cast<const ARMBaseRegisterInfo *>(&TRI);

  BitVector regsReserved = ARI->getReservedRegs(*MF);
  // Check if there is an available register across the sequence that we can
  // use.
  for (Register Reg : ARM::rGPRRegClass) {
    if (!(Reg < regsReserved.size() && regsReserved.test(Reg)) &&
        Reg != ARM::LR &&  // LR is not reserved, but don't use it.
        Reg != ARM::R12 && // R12 is not guaranteed to be preserved.
        C.isAvailableAcrossAndOutOfSeq(Reg, TRI) &&
        C.isAvailableInsideSeq(Reg, TRI))
      return Reg;
  }
  return Register();
}

// Compute liveness of LR at the point after the interval [I, E), which
// denotes a *backward* iteration through instructions. Used only for return
// basic blocks, which do not end with a tail call.
static bool isLRAvailable(const TargetRegisterInfo &TRI,
                          MachineBasicBlock::reverse_iterator I,
                          MachineBasicBlock::reverse_iterator E) {
  // At the end of the function LR dead.
  bool Live = false;
  for (; I != E; ++I) {
    const MachineInstr &MI = *I;

    // Check defs of LR.
    if (MI.modifiesRegister(ARM::LR, &TRI))
      Live = false;

    // Check uses of LR.
    unsigned Opcode = MI.getOpcode();
    if (Opcode == ARM::BX_RET || Opcode == ARM::MOVPCLR ||
        Opcode == ARM::SUBS_PC_LR || Opcode == ARM::tBX_RET ||
        Opcode == ARM::tBXNS_RET) {
      // These instructions use LR, but it's not an (explicit or implicit)
      // operand.
      Live = true;
      continue;
    }
    if (MI.readsRegister(ARM::LR, &TRI))
      Live = true;
  }
  return !Live;
}

std::optional<outliner::OutlinedFunction>
ARMBaseInstrInfo::getOutliningCandidateInfo(
    std::vector<outliner::Candidate> &RepeatedSequenceLocs) const {
  unsigned SequenceSize = 0;
  for (auto &MI : RepeatedSequenceLocs[0])
    SequenceSize += getInstSizeInBytes(MI);

  // Properties about candidate MBBs that hold for all of them.
  unsigned FlagsSetInAll = 0xF;

  // Compute liveness information for each candidate, and set FlagsSetInAll.
  const TargetRegisterInfo &TRI = getRegisterInfo();
  for (outliner::Candidate &C : RepeatedSequenceLocs)
    FlagsSetInAll &= C.Flags;

  // According to the ARM Procedure Call Standard, the following are
  // undefined on entry/exit from a function call:
  //
  // * Register R12(IP),
  // * Condition codes (and thus the CPSR register)
  //
  // Since we control the instructions which are part of the outlined regions
  // we don't need to be fully compliant with the AAPCS, but we have to
  // guarantee that if a veneer is inserted at link time the code is still
  // correct.  Because of this, we can't outline any sequence of instructions
  // where one of these registers is live into/across it. Thus, we need to
  // delete those candidates.
  auto CantGuaranteeValueAcrossCall = [&TRI](outliner::Candidate &C) {
    // If the unsafe registers in this block are all dead, then we don't need
    // to compute liveness here.
    if (C.Flags & UnsafeRegsDead)
      return false;
    return C.isAnyUnavailableAcrossOrOutOfSeq({ARM::R12, ARM::CPSR}, TRI);
  };

  // Are there any candidates where those registers are live?
  if (!(FlagsSetInAll & UnsafeRegsDead)) {
    // Erase every candidate that violates the restrictions above. (It could be
    // true that we have viable candidates, so it's not worth bailing out in
    // the case that, say, 1 out of 20 candidates violate the restructions.)
    llvm::erase_if(RepeatedSequenceLocs, CantGuaranteeValueAcrossCall);

    // If the sequence doesn't have enough candidates left, then we're done.
    if (RepeatedSequenceLocs.size() < 2)
      return std::nullopt;
  }

  // We expect the majority of the outlining candidates to be in consensus with
  // regard to return address sign and authentication, and branch target
  // enforcement, in other words, partitioning according to all the four
  // possible combinations of PAC-RET and BTI is going to yield one big subset
  // and three small (likely empty) subsets. That allows us to cull incompatible
  // candidates separately for PAC-RET and BTI.

  // Partition the candidates in two sets: one with BTI enabled and one with BTI
  // disabled. Remove the candidates from the smaller set. If they are the same
  // number prefer the non-BTI ones for outlining, since they have less
  // overhead.
  auto NoBTI =
      llvm::partition(RepeatedSequenceLocs, [](const outliner::Candidate &C) {
        const ARMFunctionInfo &AFI = *C.getMF()->getInfo<ARMFunctionInfo>();
        return AFI.branchTargetEnforcement();
      });
  if (std::distance(RepeatedSequenceLocs.begin(), NoBTI) >
      std::distance(NoBTI, RepeatedSequenceLocs.end()))
    RepeatedSequenceLocs.erase(NoBTI, RepeatedSequenceLocs.end());
  else
    RepeatedSequenceLocs.erase(RepeatedSequenceLocs.begin(), NoBTI);

  if (RepeatedSequenceLocs.size() < 2)
    return std::nullopt;

  // Likewise, partition the candidates according to PAC-RET enablement.
  auto NoPAC =
      llvm::partition(RepeatedSequenceLocs, [](const outliner::Candidate &C) {
        const ARMFunctionInfo &AFI = *C.getMF()->getInfo<ARMFunctionInfo>();
        // If the function happens to not spill the LR, do not disqualify it
        // from the outlining.
        return AFI.shouldSignReturnAddress(true);
      });
  if (std::distance(RepeatedSequenceLocs.begin(), NoPAC) >
      std::distance(NoPAC, RepeatedSequenceLocs.end()))
    RepeatedSequenceLocs.erase(NoPAC, RepeatedSequenceLocs.end());
  else
    RepeatedSequenceLocs.erase(RepeatedSequenceLocs.begin(), NoPAC);

  if (RepeatedSequenceLocs.size() < 2)
    return std::nullopt;

  // At this point, we have only "safe" candidates to outline. Figure out
  // frame + call instruction information.

  unsigned LastInstrOpcode = RepeatedSequenceLocs[0].back().getOpcode();

  // Helper lambda which sets call information for every candidate.
  auto SetCandidateCallInfo =
      [&RepeatedSequenceLocs](unsigned CallID, unsigned NumBytesForCall) {
        for (outliner::Candidate &C : RepeatedSequenceLocs)
          C.setCallInfo(CallID, NumBytesForCall);
      };

  OutlinerCosts Costs(Subtarget);

  const auto &SomeMFI =
      *RepeatedSequenceLocs.front().getMF()->getInfo<ARMFunctionInfo>();
  // Adjust costs to account for the BTI instructions.
  if (SomeMFI.branchTargetEnforcement()) {
    Costs.FrameDefault += 4;
    Costs.FrameNoLRSave += 4;
    Costs.FrameRegSave += 4;
    Costs.FrameTailCall += 4;
    Costs.FrameThunk += 4;
  }

  // Adjust costs to account for sign and authentication instructions.
  if (SomeMFI.shouldSignReturnAddress(true)) {
    Costs.CallDefault += 8;          // +PAC instr, +AUT instr
    Costs.SaveRestoreLROnStack += 8; // +PAC instr, +AUT instr
  }

  unsigned FrameID = MachineOutlinerDefault;
  unsigned NumBytesToCreateFrame = Costs.FrameDefault;

  // If the last instruction in any candidate is a terminator, then we should
  // tail call all of the candidates.
  if (RepeatedSequenceLocs[0].back().isTerminator()) {
    FrameID = MachineOutlinerTailCall;
    NumBytesToCreateFrame = Costs.FrameTailCall;
    SetCandidateCallInfo(MachineOutlinerTailCall, Costs.CallTailCall);
  } else if (LastInstrOpcode == ARM::BL || LastInstrOpcode == ARM::BLX ||
             LastInstrOpcode == ARM::BLX_noip || LastInstrOpcode == ARM::tBL ||
             LastInstrOpcode == ARM::tBLXr ||
             LastInstrOpcode == ARM::tBLXr_noip ||
             LastInstrOpcode == ARM::tBLXi) {
    FrameID = MachineOutlinerThunk;
    NumBytesToCreateFrame = Costs.FrameThunk;
    SetCandidateCallInfo(MachineOutlinerThunk, Costs.CallThunk);
  } else {
    // We need to decide how to emit calls + frames. We can always emit the same
    // frame if we don't need to save to the stack. If we have to save to the
    // stack, then we need a different frame.
    unsigned NumBytesNoStackCalls = 0;
    std::vector<outliner::Candidate> CandidatesWithoutStackFixups;

    for (outliner::Candidate &C : RepeatedSequenceLocs) {
      // LR liveness is overestimated in return blocks, unless they end with a
      // tail call.
      const auto Last = C.getMBB()->rbegin();
      const bool LRIsAvailable =
          C.getMBB()->isReturnBlock() && !Last->isCall()
              ? isLRAvailable(TRI, Last,
                              (MachineBasicBlock::reverse_iterator)C.begin())
              : C.isAvailableAcrossAndOutOfSeq(ARM::LR, TRI);
      if (LRIsAvailable) {
        FrameID = MachineOutlinerNoLRSave;
        NumBytesNoStackCalls += Costs.CallNoLRSave;
        C.setCallInfo(MachineOutlinerNoLRSave, Costs.CallNoLRSave);
        CandidatesWithoutStackFixups.push_back(C);
      }

      // Is an unused register available? If so, we won't modify the stack, so
      // we can outline with the same frame type as those that don't save LR.
      else if (findRegisterToSaveLRTo(C)) {
        FrameID = MachineOutlinerRegSave;
        NumBytesNoStackCalls += Costs.CallRegSave;
        C.setCallInfo(MachineOutlinerRegSave, Costs.CallRegSave);
        CandidatesWithoutStackFixups.push_back(C);
      }

      // Is SP used in the sequence at all? If not, we don't have to modify
      // the stack, so we are guaranteed to get the same frame.
      else if (C.isAvailableInsideSeq(ARM::SP, TRI)) {
        NumBytesNoStackCalls += Costs.CallDefault;
        C.setCallInfo(MachineOutlinerDefault, Costs.CallDefault);
        CandidatesWithoutStackFixups.push_back(C);
      }

      // If we outline this, we need to modify the stack. Pretend we don't
      // outline this by saving all of its bytes.
      else
        NumBytesNoStackCalls += SequenceSize;
    }

    // If there are no places where we have to save LR, then note that we don't
    // have to update the stack. Otherwise, give every candidate the default
    // call type
    if (NumBytesNoStackCalls <=
        RepeatedSequenceLocs.size() * Costs.CallDefault) {
      RepeatedSequenceLocs = CandidatesWithoutStackFixups;
      FrameID = MachineOutlinerNoLRSave;
      if (RepeatedSequenceLocs.size() < 2)
        return std::nullopt;
    } else
      SetCandidateCallInfo(MachineOutlinerDefault, Costs.CallDefault);
  }

  // Does every candidate's MBB contain a call?  If so, then we might have a
  // call in the range.
  if (FlagsSetInAll & MachineOutlinerMBBFlags::HasCalls) {
    // check if the range contains a call.  These require a save + restore of
    // the link register.
    outliner::Candidate &FirstCand = RepeatedSequenceLocs[0];
    if (std::any_of(FirstCand.begin(), std::prev(FirstCand.end()),
                    [](const MachineInstr &MI) { return MI.isCall(); }))
      NumBytesToCreateFrame += Costs.SaveRestoreLROnStack;

    // Handle the last instruction separately.  If it is tail call, then the
    // last instruction is a call, we don't want to save + restore in this
    // case.  However, it could be possible that the last instruction is a
    // call without it being valid to tail call this sequence.  We should
    // consider this as well.
    else if (FrameID != MachineOutlinerThunk &&
             FrameID != MachineOutlinerTailCall && FirstCand.back().isCall())
      NumBytesToCreateFrame += Costs.SaveRestoreLROnStack;
  }

  return outliner::OutlinedFunction(RepeatedSequenceLocs, SequenceSize,
                                    NumBytesToCreateFrame, FrameID);
}

bool ARMBaseInstrInfo::checkAndUpdateStackOffset(MachineInstr *MI,
                                                 int64_t Fixup,
                                                 bool Updt) const {
  int SPIdx = MI->findRegisterUseOperandIdx(ARM::SP, /*TRI=*/nullptr);
  unsigned AddrMode = (MI->getDesc().TSFlags & ARMII::AddrModeMask);
  if (SPIdx < 0)
    // No SP operand
    return true;
  else if (SPIdx != 1 && (AddrMode != ARMII::AddrModeT2_i8s4 || SPIdx != 2))
    // If SP is not the base register we can't do much
    return false;

  // Stack might be involved but addressing mode doesn't handle any offset.
  // Rq: AddrModeT1_[1|2|4] don't operate on SP
  if (AddrMode == ARMII::AddrMode1 ||       // Arithmetic instructions
      AddrMode == ARMII::AddrMode4 ||       // Load/Store Multiple
      AddrMode == ARMII::AddrMode6 ||       // Neon Load/Store Multiple
      AddrMode == ARMII::AddrModeT2_so ||   // SP can't be used as based register
      AddrMode == ARMII::AddrModeT2_pc ||   // PCrel access
      AddrMode == ARMII::AddrMode2 ||       // Used by PRE and POST indexed LD/ST
      AddrMode == ARMII::AddrModeT2_i7 ||   // v8.1-M MVE
      AddrMode == ARMII::AddrModeT2_i7s2 || // v8.1-M MVE
      AddrMode == ARMII::AddrModeT2_i7s4 || // v8.1-M sys regs VLDR/VSTR
      AddrMode == ARMII::AddrModeNone ||
      AddrMode == ARMII::AddrModeT2_i8 ||   // Pre/Post inc instructions
      AddrMode == ARMII::AddrModeT2_i8neg)  // Always negative imm
    return false;

  unsigned NumOps = MI->getDesc().getNumOperands();
  unsigned ImmIdx = NumOps - 3;

  const MachineOperand &Offset = MI->getOperand(ImmIdx);
  assert(Offset.isImm() && "Is not an immediate");
  int64_t OffVal = Offset.getImm();

  if (OffVal < 0)
    // Don't override data if the are below SP.
    return false;

  unsigned NumBits = 0;
  unsigned Scale = 1;

  switch (AddrMode) {
  case ARMII::AddrMode3:
    if (ARM_AM::getAM3Op(OffVal) == ARM_AM::sub)
      return false;
    OffVal = ARM_AM::getAM3Offset(OffVal);
    NumBits = 8;
    break;
  case ARMII::AddrMode5:
    if (ARM_AM::getAM5Op(OffVal) == ARM_AM::sub)
      return false;
    OffVal = ARM_AM::getAM5Offset(OffVal);
    NumBits = 8;
    Scale = 4;
    break;
  case ARMII::AddrMode5FP16:
    if (ARM_AM::getAM5FP16Op(OffVal) == ARM_AM::sub)
      return false;
    OffVal = ARM_AM::getAM5FP16Offset(OffVal);
    NumBits = 8;
    Scale = 2;
    break;
  case ARMII::AddrModeT2_i8pos:
    NumBits = 8;
    break;
  case ARMII::AddrModeT2_i8s4:
    // FIXME: Values are already scaled in this addressing mode.
    assert((Fixup & 3) == 0 && "Can't encode this offset!");
    NumBits = 10;
    break;
  case ARMII::AddrModeT2_ldrex:
    NumBits = 8;
    Scale = 4;
    break;
  case ARMII::AddrModeT2_i12:
  case ARMII::AddrMode_i12:
    NumBits = 12;
    break;
  case ARMII::AddrModeT1_s: // SP-relative LD/ST
    NumBits = 8;
    Scale = 4;
    break;
  default:
    llvm_unreachable("Unsupported addressing mode!");
  }
  // Make sure the offset is encodable for instructions that scale the
  // immediate.
  assert(((OffVal * Scale + Fixup) & (Scale - 1)) == 0 &&
         "Can't encode this offset!");
  OffVal += Fixup / Scale;

  unsigned Mask = (1 << NumBits) - 1;

  if (OffVal <= Mask) {
    if (Updt)
      MI->getOperand(ImmIdx).setImm(OffVal);
    return true;
  }

  return false;
}

void ARMBaseInstrInfo::mergeOutliningCandidateAttributes(
    Function &F, std::vector<outliner::Candidate> &Candidates) const {
  outliner::Candidate &C = Candidates.front();
  // branch-target-enforcement is guaranteed to be consistent between all
  // candidates, so we only need to look at one.
  const Function &CFn = C.getMF()->getFunction();
  if (CFn.hasFnAttribute("branch-target-enforcement"))
    F.addFnAttr(CFn.getFnAttribute("branch-target-enforcement"));

  ARMGenInstrInfo::mergeOutliningCandidateAttributes(F, Candidates);
}

bool ARMBaseInstrInfo::isFunctionSafeToOutlineFrom(
    MachineFunction &MF, bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();

  // Can F be deduplicated by the linker? If it can, don't outline from it.
  if (!OutlineFromLinkOnceODRs && F.hasLinkOnceODRLinkage())
    return false;

  // Don't outline from functions with section markings; the program could
  // expect that all the code is in the named section.
  // FIXME: Allow outlining from multiple functions with the same section
  // marking.
  if (F.hasSection())
    return false;

  // FIXME: Thumb1 outlining is not handled
  if (MF.getInfo<ARMFunctionInfo>()->isThumb1OnlyFunction())
    return false;

  // It's safe to outline from MF.
  return true;
}

bool ARMBaseInstrInfo::isMBBSafeToOutlineFrom(MachineBasicBlock &MBB,
                                              unsigned &Flags) const {
  // Check if LR is available through all of the MBB. If it's not, then set
  // a flag.
  assert(MBB.getParent()->getRegInfo().tracksLiveness() &&
         "Suitable Machine Function for outlining must track liveness");

  LiveRegUnits LRU(getRegisterInfo());

  for (MachineInstr &MI : llvm::reverse(MBB))
    LRU.accumulate(MI);

  // Check if each of the unsafe registers are available...
  bool R12AvailableInBlock = LRU.available(ARM::R12);
  bool CPSRAvailableInBlock = LRU.available(ARM::CPSR);

  // If all of these are dead (and not live out), we know we don't have to check
  // them later.
  if (R12AvailableInBlock && CPSRAvailableInBlock)
    Flags |= MachineOutlinerMBBFlags::UnsafeRegsDead;

  // Now, add the live outs to the set.
  LRU.addLiveOuts(MBB);

  // If any of these registers is available in the MBB, but also a live out of
  // the block, then we know outlining is unsafe.
  if (R12AvailableInBlock && !LRU.available(ARM::R12))
    return false;
  if (CPSRAvailableInBlock && !LRU.available(ARM::CPSR))
    return false;

  // Check if there's a call inside this MachineBasicBlock.  If there is, then
  // set a flag.
  if (any_of(MBB, [](MachineInstr &MI) { return MI.isCall(); }))
    Flags |= MachineOutlinerMBBFlags::HasCalls;

  // LR liveness is overestimated in return blocks.

  bool LRIsAvailable =
      MBB.isReturnBlock() && !MBB.back().isCall()
          ? isLRAvailable(getRegisterInfo(), MBB.rbegin(), MBB.rend())
          : LRU.available(ARM::LR);
  if (!LRIsAvailable)
    Flags |= MachineOutlinerMBBFlags::LRUnavailableSomewhere;

  return true;
}

outliner::InstrType
ARMBaseInstrInfo::getOutliningTypeImpl(MachineBasicBlock::iterator &MIT,
                                   unsigned Flags) const {
  MachineInstr &MI = *MIT;
  const TargetRegisterInfo *TRI = &getRegisterInfo();

  // PIC instructions contain labels, outlining them would break offset
  // computing.  unsigned Opc = MI.getOpcode();
  unsigned Opc = MI.getOpcode();
  if (Opc == ARM::tPICADD || Opc == ARM::PICADD || Opc == ARM::PICSTR ||
      Opc == ARM::PICSTRB || Opc == ARM::PICSTRH || Opc == ARM::PICLDR ||
      Opc == ARM::PICLDRB || Opc == ARM::PICLDRH || Opc == ARM::PICLDRSB ||
      Opc == ARM::PICLDRSH || Opc == ARM::t2LDRpci_pic ||
      Opc == ARM::t2MOVi16_ga_pcrel || Opc == ARM::t2MOVTi16_ga_pcrel ||
      Opc == ARM::t2MOV_ga_pcrel)
    return outliner::InstrType::Illegal;

  // Be conservative with ARMv8.1 MVE instructions.
  if (Opc == ARM::t2BF_LabelPseudo || Opc == ARM::t2DoLoopStart ||
      Opc == ARM::t2DoLoopStartTP || Opc == ARM::t2WhileLoopStart ||
      Opc == ARM::t2WhileLoopStartLR || Opc == ARM::t2WhileLoopStartTP ||
      Opc == ARM::t2LoopDec || Opc == ARM::t2LoopEnd ||
      Opc == ARM::t2LoopEndDec)
    return outliner::InstrType::Illegal;

  const MCInstrDesc &MCID = MI.getDesc();
  uint64_t MIFlags = MCID.TSFlags;
  if ((MIFlags & ARMII::DomainMask) == ARMII::DomainMVE)
    return outliner::InstrType::Illegal;

  // Is this a terminator for a basic block?
  if (MI.isTerminator())
    // TargetInstrInfo::getOutliningType has already filtered out anything
    // that would break this, so we can allow it here.
    return outliner::InstrType::Legal;

  // Don't outline if link register or program counter value are used.
  if (MI.readsRegister(ARM::LR, TRI) || MI.readsRegister(ARM::PC, TRI))
    return outliner::InstrType::Illegal;

  if (MI.isCall()) {
    // Get the function associated with the call.  Look at each operand and find
    // the one that represents the calle and get its name.
    const Function *Callee = nullptr;
    for (const MachineOperand &MOP : MI.operands()) {
      if (MOP.isGlobal()) {
        Callee = dyn_cast<Function>(MOP.getGlobal());
        break;
      }
    }

    // Dont't outline calls to "mcount" like functions, in particular Linux
    // kernel function tracing relies on it.
    if (Callee &&
        (Callee->getName() == "\01__gnu_mcount_nc" ||
         Callee->getName() == "\01mcount" || Callee->getName() == "__mcount"))
      return outliner::InstrType::Illegal;

    // If we don't know anything about the callee, assume it depends on the
    // stack layout of the caller. In that case, it's only legal to outline
    // as a tail-call. Explicitly list the call instructions we know about so
    // we don't get unexpected results with call pseudo-instructions.
    auto UnknownCallOutlineType = outliner::InstrType::Illegal;
    if (Opc == ARM::BL || Opc == ARM::tBL || Opc == ARM::BLX ||
        Opc == ARM::BLX_noip || Opc == ARM::tBLXr || Opc == ARM::tBLXr_noip ||
        Opc == ARM::tBLXi)
      UnknownCallOutlineType = outliner::InstrType::LegalTerminator;

    if (!Callee)
      return UnknownCallOutlineType;

    // We have a function we have information about.  Check if it's something we
    // can safely outline.
    MachineFunction *MF = MI.getParent()->getParent();
    MachineFunction *CalleeMF = MF->getMMI().getMachineFunction(*Callee);

    // We don't know what's going on with the callee at all.  Don't touch it.
    if (!CalleeMF)
      return UnknownCallOutlineType;

    // Check if we know anything about the callee saves on the function. If we
    // don't, then don't touch it, since that implies that we haven't computed
    // anything about its stack frame yet.
    MachineFrameInfo &MFI = CalleeMF->getFrameInfo();
    if (!MFI.isCalleeSavedInfoValid() || MFI.getStackSize() > 0 ||
        MFI.getNumObjects() > 0)
      return UnknownCallOutlineType;

    // At this point, we can say that CalleeMF ought to not pass anything on the
    // stack. Therefore, we can outline it.
    return outliner::InstrType::Legal;
  }

  // Since calls are handled, don't touch LR or PC
  if (MI.modifiesRegister(ARM::LR, TRI) || MI.modifiesRegister(ARM::PC, TRI))
    return outliner::InstrType::Illegal;

  // Does this use the stack?
  if (MI.modifiesRegister(ARM::SP, TRI) || MI.readsRegister(ARM::SP, TRI)) {
    // True if there is no chance that any outlined candidate from this range
    // could require stack fixups. That is, both
    // * LR is available in the range (No save/restore around call)
    // * The range doesn't include calls (No save/restore in outlined frame)
    // are true.
    // These conditions also ensure correctness of the return address
    // authentication - we insert sign and authentication instructions only if
    // we save/restore LR on stack, but then this condition ensures that the
    // outlined range does not modify the SP, therefore the SP value used for
    // signing is the same as the one used for authentication.
    // FIXME: This is very restrictive; the flags check the whole block,
    // not just the bit we will try to outline.
    bool MightNeedStackFixUp =
        (Flags & (MachineOutlinerMBBFlags::LRUnavailableSomewhere |
                  MachineOutlinerMBBFlags::HasCalls));

    if (!MightNeedStackFixUp)
      return outliner::InstrType::Legal;

    // Any modification of SP will break our code to save/restore LR.
    // FIXME: We could handle some instructions which add a constant offset to
    // SP, with a bit more work.
    if (MI.modifiesRegister(ARM::SP, TRI))
      return outliner::InstrType::Illegal;

    // At this point, we have a stack instruction that we might need to fix up.
    // up. We'll handle it if it's a load or store.
    if (checkAndUpdateStackOffset(&MI, Subtarget.getStackAlignment().value(),
                                  false))
      return outliner::InstrType::Legal;

    // We can't fix it up, so don't outline it.
    return outliner::InstrType::Illegal;
  }

  // Be conservative with IT blocks.
  if (MI.readsRegister(ARM::ITSTATE, TRI) ||
      MI.modifiesRegister(ARM::ITSTATE, TRI))
    return outliner::InstrType::Illegal;

  // Don't outline CFI instructions.
  if (MI.isCFIInstruction())
    return outliner::InstrType::Illegal;

  return outliner::InstrType::Legal;
}

void ARMBaseInstrInfo::fixupPostOutline(MachineBasicBlock &MBB) const {
  for (MachineInstr &MI : MBB) {
    checkAndUpdateStackOffset(&MI, Subtarget.getStackAlignment().value(), true);
  }
}

void ARMBaseInstrInfo::saveLROnStack(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator It, bool CFI,
                                     bool Auth) const {
  int Align = std::max(Subtarget.getStackAlignment().value(), uint64_t(8));
  unsigned MIFlags = CFI ? MachineInstr::FrameSetup : 0;
  assert(Align >= 8 && Align <= 256);
  if (Auth) {
    assert(Subtarget.isThumb2());
    // Compute PAC in R12. Outlining ensures R12 is dead across the outlined
    // sequence.
    BuildMI(MBB, It, DebugLoc(), get(ARM::t2PAC)).setMIFlags(MIFlags);
    BuildMI(MBB, It, DebugLoc(), get(ARM::t2STRD_PRE), ARM::SP)
        .addReg(ARM::R12, RegState::Kill)
        .addReg(ARM::LR, RegState::Kill)
        .addReg(ARM::SP)
        .addImm(-Align)
        .add(predOps(ARMCC::AL))
        .setMIFlags(MIFlags);
  } else {
    unsigned Opc = Subtarget.isThumb() ? ARM::t2STR_PRE : ARM::STR_PRE_IMM;
    BuildMI(MBB, It, DebugLoc(), get(Opc), ARM::SP)
        .addReg(ARM::LR, RegState::Kill)
        .addReg(ARM::SP)
        .addImm(-Align)
        .add(predOps(ARMCC::AL))
        .setMIFlags(MIFlags);
  }

  if (!CFI)
    return;

  MachineFunction &MF = *MBB.getParent();

  // Add a CFI, saying CFA is offset by Align bytes from SP.
  int64_t StackPosEntry =
      MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, Align));
  BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
      .addCFIIndex(StackPosEntry)
      .setMIFlags(MachineInstr::FrameSetup);

  // Add a CFI saying that the LR that we want to find is now higher than
  // before.
  int LROffset = Auth ? Align - 4 : Align;
  const MCRegisterInfo *MRI = Subtarget.getRegisterInfo();
  unsigned DwarfLR = MRI->getDwarfRegNum(ARM::LR, true);
  int64_t LRPosEntry = MF.addFrameInst(
      MCCFIInstruction::createOffset(nullptr, DwarfLR, -LROffset));
  BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
      .addCFIIndex(LRPosEntry)
      .setMIFlags(MachineInstr::FrameSetup);
  if (Auth) {
    // Add a CFI for the location of the return adddress PAC.
    unsigned DwarfRAC = MRI->getDwarfRegNum(ARM::RA_AUTH_CODE, true);
    int64_t RACPosEntry = MF.addFrameInst(
        MCCFIInstruction::createOffset(nullptr, DwarfRAC, -Align));
    BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
        .addCFIIndex(RACPosEntry)
        .setMIFlags(MachineInstr::FrameSetup);
  }
}

void ARMBaseInstrInfo::emitCFIForLRSaveToReg(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator It,
                                             Register Reg) const {
  MachineFunction &MF = *MBB.getParent();
  const MCRegisterInfo *MRI = Subtarget.getRegisterInfo();
  unsigned DwarfLR = MRI->getDwarfRegNum(ARM::LR, true);
  unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);

  int64_t LRPosEntry = MF.addFrameInst(
      MCCFIInstruction::createRegister(nullptr, DwarfLR, DwarfReg));
  BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
      .addCFIIndex(LRPosEntry)
      .setMIFlags(MachineInstr::FrameSetup);
}

void ARMBaseInstrInfo::restoreLRFromStack(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator It,
                                          bool CFI, bool Auth) const {
  int Align = Subtarget.getStackAlignment().value();
  unsigned MIFlags = CFI ? MachineInstr::FrameDestroy : 0;
  if (Auth) {
    assert(Subtarget.isThumb2());
    // Restore return address PAC and LR.
    BuildMI(MBB, It, DebugLoc(), get(ARM::t2LDRD_POST))
        .addReg(ARM::R12, RegState::Define)
        .addReg(ARM::LR, RegState::Define)
        .addReg(ARM::SP, RegState::Define)
        .addReg(ARM::SP)
        .addImm(Align)
        .add(predOps(ARMCC::AL))
        .setMIFlags(MIFlags);
    // LR authentication is after the CFI instructions, below.
  } else {
    unsigned Opc = Subtarget.isThumb() ? ARM::t2LDR_POST : ARM::LDR_POST_IMM;
    MachineInstrBuilder MIB = BuildMI(MBB, It, DebugLoc(), get(Opc), ARM::LR)
                                  .addReg(ARM::SP, RegState::Define)
                                  .addReg(ARM::SP);
    if (!Subtarget.isThumb())
      MIB.addReg(0);
    MIB.addImm(Subtarget.getStackAlignment().value())
        .add(predOps(ARMCC::AL))
        .setMIFlags(MIFlags);
  }

  if (CFI) {
    // Now stack has moved back up...
    MachineFunction &MF = *MBB.getParent();
    const MCRegisterInfo *MRI = Subtarget.getRegisterInfo();
    unsigned DwarfLR = MRI->getDwarfRegNum(ARM::LR, true);
    int64_t StackPosEntry =
        MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, 0));
    BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
        .addCFIIndex(StackPosEntry)
        .setMIFlags(MachineInstr::FrameDestroy);

    // ... and we have restored LR.
    int64_t LRPosEntry =
        MF.addFrameInst(MCCFIInstruction::createRestore(nullptr, DwarfLR));
    BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
        .addCFIIndex(LRPosEntry)
        .setMIFlags(MachineInstr::FrameDestroy);

    if (Auth) {
      unsigned DwarfRAC = MRI->getDwarfRegNum(ARM::RA_AUTH_CODE, true);
      int64_t Entry =
          MF.addFrameInst(MCCFIInstruction::createUndefined(nullptr, DwarfRAC));
      BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
          .addCFIIndex(Entry)
          .setMIFlags(MachineInstr::FrameDestroy);
    }
  }

  if (Auth)
    BuildMI(MBB, It, DebugLoc(), get(ARM::t2AUT));
}

void ARMBaseInstrInfo::emitCFIForLRRestoreFromReg(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator It) const {
  MachineFunction &MF = *MBB.getParent();
  const MCRegisterInfo *MRI = Subtarget.getRegisterInfo();
  unsigned DwarfLR = MRI->getDwarfRegNum(ARM::LR, true);

  int64_t LRPosEntry =
      MF.addFrameInst(MCCFIInstruction::createRestore(nullptr, DwarfLR));
  BuildMI(MBB, It, DebugLoc(), get(ARM::CFI_INSTRUCTION))
      .addCFIIndex(LRPosEntry)
      .setMIFlags(MachineInstr::FrameDestroy);
}

void ARMBaseInstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &OF) const {
  // For thunk outlining, rewrite the last instruction from a call to a
  // tail-call.
  if (OF.FrameConstructionID == MachineOutlinerThunk) {
    MachineInstr *Call = &*--MBB.instr_end();
    bool isThumb = Subtarget.isThumb();
    unsigned FuncOp = isThumb ? 2 : 0;
    unsigned Opc = Call->getOperand(FuncOp).isReg()
                       ? isThumb ? ARM::tTAILJMPr : ARM::TAILJMPr
                       : isThumb ? Subtarget.isTargetMachO() ? ARM::tTAILJMPd
                                                             : ARM::tTAILJMPdND
                                 : ARM::TAILJMPd;
    MachineInstrBuilder MIB = BuildMI(MBB, MBB.end(), DebugLoc(), get(Opc))
                                  .add(Call->getOperand(FuncOp));
    if (isThumb && !Call->getOperand(FuncOp).isReg())
      MIB.add(predOps(ARMCC::AL));
    Call->eraseFromParent();
  }

  // Is there a call in the outlined range?
  auto IsNonTailCall = [](MachineInstr &MI) {
    return MI.isCall() && !MI.isReturn();
  };
  if (llvm::any_of(MBB.instrs(), IsNonTailCall)) {
    MachineBasicBlock::iterator It = MBB.begin();
    MachineBasicBlock::iterator Et = MBB.end();

    if (OF.FrameConstructionID == MachineOutlinerTailCall ||
        OF.FrameConstructionID == MachineOutlinerThunk)
      Et = std::prev(MBB.end());

    // We have to save and restore LR, we need to add it to the liveins if it
    // is not already part of the set.  This is suffient since outlined
    // functions only have one block.
    if (!MBB.isLiveIn(ARM::LR))
      MBB.addLiveIn(ARM::LR);

    // Insert a save before the outlined region
    bool Auth = OF.Candidates.front()
                    .getMF()
                    ->getInfo<ARMFunctionInfo>()
                    ->shouldSignReturnAddress(true);
    saveLROnStack(MBB, It, true, Auth);

    // Fix up the instructions in the range, since we're going to modify the
    // stack.
    assert(OF.FrameConstructionID != MachineOutlinerDefault &&
           "Can only fix up stack references once");
    fixupPostOutline(MBB);

    // Insert a restore before the terminator for the function.  Restore LR.
    restoreLRFromStack(MBB, Et, true, Auth);
  }

  // If this is a tail call outlined function, then there's already a return.
  if (OF.FrameConstructionID == MachineOutlinerTailCall ||
      OF.FrameConstructionID == MachineOutlinerThunk)
    return;

  // Here we have to insert the return ourselves.  Get the correct opcode from
  // current feature set.
  BuildMI(MBB, MBB.end(), DebugLoc(), get(Subtarget.getReturnOpcode()))
      .add(predOps(ARMCC::AL));

  // Did we have to modify the stack by saving the link register?
  if (OF.FrameConstructionID != MachineOutlinerDefault &&
      OF.Candidates[0].CallConstructionID != MachineOutlinerDefault)
    return;

  // We modified the stack.
  // Walk over the basic block and fix up all the stack accesses.
  fixupPostOutline(MBB);
}

MachineBasicBlock::iterator ARMBaseInstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &It,
    MachineFunction &MF, outliner::Candidate &C) const {
  MachineInstrBuilder MIB;
  MachineBasicBlock::iterator CallPt;
  unsigned Opc;
  bool isThumb = Subtarget.isThumb();

  // Are we tail calling?
  if (C.CallConstructionID == MachineOutlinerTailCall) {
    // If yes, then we can just branch to the label.
    Opc = isThumb
              ? Subtarget.isTargetMachO() ? ARM::tTAILJMPd : ARM::tTAILJMPdND
              : ARM::TAILJMPd;
    MIB = BuildMI(MF, DebugLoc(), get(Opc))
              .addGlobalAddress(M.getNamedValue(MF.getName()));
    if (isThumb)
      MIB.add(predOps(ARMCC::AL));
    It = MBB.insert(It, MIB);
    return It;
  }

  // Create the call instruction.
  Opc = isThumb ? ARM::tBL : ARM::BL;
  MachineInstrBuilder CallMIB = BuildMI(MF, DebugLoc(), get(Opc));
  if (isThumb)
    CallMIB.add(predOps(ARMCC::AL));
  CallMIB.addGlobalAddress(M.getNamedValue(MF.getName()));

  if (C.CallConstructionID == MachineOutlinerNoLRSave ||
      C.CallConstructionID == MachineOutlinerThunk) {
    // No, so just insert the call.
    It = MBB.insert(It, CallMIB);
    return It;
  }

  const ARMFunctionInfo &AFI = *C.getMF()->getInfo<ARMFunctionInfo>();
  // Can we save to a register?
  if (C.CallConstructionID == MachineOutlinerRegSave) {
    Register Reg = findRegisterToSaveLRTo(C);
    assert(Reg != 0 && "No callee-saved register available?");

    // Save and restore LR from that register.
    copyPhysReg(MBB, It, DebugLoc(), Reg, ARM::LR, true);
    if (!AFI.isLRSpilled())
      emitCFIForLRSaveToReg(MBB, It, Reg);
    CallPt = MBB.insert(It, CallMIB);
    copyPhysReg(MBB, It, DebugLoc(), ARM::LR, Reg, true);
    if (!AFI.isLRSpilled())
      emitCFIForLRRestoreFromReg(MBB, It);
    It--;
    return CallPt;
  }
  // We have the default case. Save and restore from SP.
  if (!MBB.isLiveIn(ARM::LR))
    MBB.addLiveIn(ARM::LR);
  bool Auth = !AFI.isLRSpilled() && AFI.shouldSignReturnAddress(true);
  saveLROnStack(MBB, It, !AFI.isLRSpilled(), Auth);
  CallPt = MBB.insert(It, CallMIB);
  restoreLRFromStack(MBB, It, !AFI.isLRSpilled(), Auth);
  It--;
  return CallPt;
}

bool ARMBaseInstrInfo::shouldOutlineFromFunctionByDefault(
    MachineFunction &MF) const {
  return Subtarget.isMClass() && MF.getFunction().hasMinSize();
}

bool ARMBaseInstrInfo::isReallyTriviallyReMaterializable(
    const MachineInstr &MI) const {
  // Try hard to rematerialize any VCTPs because if we spill P0, it will block
  // the tail predication conversion. This means that the element count
  // register has to be live for longer, but that has to be better than
  // spill/restore and VPT predication.
  return (isVCTP(&MI) && !isPredicated(MI)) ||
         TargetInstrInfo::isReallyTriviallyReMaterializable(MI);
}

unsigned llvm::getBLXOpcode(const MachineFunction &MF) {
  return (MF.getSubtarget<ARMSubtarget>().hardenSlsBlr()) ? ARM::BLX_noip
                                                          : ARM::BLX;
}

unsigned llvm::gettBLXrOpcode(const MachineFunction &MF) {
  return (MF.getSubtarget<ARMSubtarget>().hardenSlsBlr()) ? ARM::tBLXr_noip
                                                          : ARM::tBLXr;
}

unsigned llvm::getBLXpredOpcode(const MachineFunction &MF) {
  return (MF.getSubtarget<ARMSubtarget>().hardenSlsBlr()) ? ARM::BLX_pred_noip
                                                          : ARM::BLX_pred;
}

namespace {
class ARMPipelinerLoopInfo : public TargetInstrInfo::PipelinerLoopInfo {
  MachineInstr *EndLoop, *LoopCount;
  MachineFunction *MF;
  const TargetInstrInfo *TII;

  // Bitset[0 .. MAX_STAGES-1] ... iterations needed
  //       [LAST_IS_USE] : last reference to register in schedule is a use
  //       [SEEN_AS_LIVE] : Normal pressure algorithm believes register is live
  static int constexpr MAX_STAGES = 30;
  static int constexpr LAST_IS_USE = MAX_STAGES;
  static int constexpr SEEN_AS_LIVE = MAX_STAGES + 1;
  typedef std::bitset<MAX_STAGES + 2> IterNeed;
  typedef std::map<unsigned, IterNeed> IterNeeds;

  void bumpCrossIterationPressure(RegPressureTracker &RPT,
                                  const IterNeeds &CIN);
  bool tooMuchRegisterPressure(SwingSchedulerDAG &SSD, SMSchedule &SMS);

  // Meanings of the various stuff with loop types:
  // t2Bcc:
  //   EndLoop = branch at end of original BB that will become a kernel
  //   LoopCount = CC setter live into branch
  // t2LoopEnd:
  //   EndLoop = branch at end of original BB
  //   LoopCount = t2LoopDec
public:
  ARMPipelinerLoopInfo(MachineInstr *EndLoop, MachineInstr *LoopCount)
      : EndLoop(EndLoop), LoopCount(LoopCount),
        MF(EndLoop->getParent()->getParent()),
        TII(MF->getSubtarget().getInstrInfo()) {}

  bool shouldIgnoreForPipelining(const MachineInstr *MI) const override {
    // Only ignore the terminator.
    return MI == EndLoop || MI == LoopCount;
  }

  bool shouldUseSchedule(SwingSchedulerDAG &SSD, SMSchedule &SMS) override {
    if (tooMuchRegisterPressure(SSD, SMS))
      return false;

    return true;
  }

  std::optional<bool> createTripCountGreaterCondition(
      int TC, MachineBasicBlock &MBB,
      SmallVectorImpl<MachineOperand> &Cond) override {

    if (isCondBranchOpcode(EndLoop->getOpcode())) {
      Cond.push_back(EndLoop->getOperand(1));
      Cond.push_back(EndLoop->getOperand(2));
      if (EndLoop->getOperand(0).getMBB() == EndLoop->getParent()) {
        TII->reverseBranchCondition(Cond);
      }
      return {};
    } else if (EndLoop->getOpcode() == ARM::t2LoopEnd) {
      // General case just lets the unrolled t2LoopDec do the subtraction and
      // therefore just needs to check if zero has been reached.
      MachineInstr *LoopDec = nullptr;
      for (auto &I : MBB.instrs())
        if (I.getOpcode() == ARM::t2LoopDec)
          LoopDec = &I;
      assert(LoopDec && "Unable to find copied LoopDec");
      // Check if we're done with the loop.
      BuildMI(&MBB, LoopDec->getDebugLoc(), TII->get(ARM::t2CMPri))
          .addReg(LoopDec->getOperand(0).getReg())
          .addImm(0)
          .addImm(ARMCC::AL)
          .addReg(ARM::NoRegister);
      Cond.push_back(MachineOperand::CreateImm(ARMCC::EQ));
      Cond.push_back(MachineOperand::CreateReg(ARM::CPSR, false));
      return {};
    } else
      llvm_unreachable("Unknown EndLoop");
  }

  void setPreheader(MachineBasicBlock *NewPreheader) override {}

  void adjustTripCount(int TripCountAdjust) override {}

  void disposed() override {}
};

void ARMPipelinerLoopInfo::bumpCrossIterationPressure(RegPressureTracker &RPT,
                                                      const IterNeeds &CIN) {
  // Increase pressure by the amounts in CrossIterationNeeds
  for (const auto &N : CIN) {
    int Cnt = N.second.count() - N.second[SEEN_AS_LIVE] * 2;
    for (int I = 0; I < Cnt; ++I)
      RPT.increaseRegPressure(Register(N.first), LaneBitmask::getNone(),
                              LaneBitmask::getAll());
  }
  // Decrease pressure by the amounts in CrossIterationNeeds
  for (const auto &N : CIN) {
    int Cnt = N.second.count() - N.second[SEEN_AS_LIVE] * 2;
    for (int I = 0; I < Cnt; ++I)
      RPT.decreaseRegPressure(Register(N.first), LaneBitmask::getAll(),
                              LaneBitmask::getNone());
  }
}

bool ARMPipelinerLoopInfo::tooMuchRegisterPressure(SwingSchedulerDAG &SSD,
                                                   SMSchedule &SMS) {
  IterNeeds CrossIterationNeeds;

  // Determine which values will be loop-carried after the schedule is
  // applied

  for (auto &SU : SSD.SUnits) {
    const MachineInstr *MI = SU.getInstr();
    int Stg = SMS.stageScheduled(const_cast<SUnit *>(&SU));
    for (auto &S : SU.Succs)
      if (MI->isPHI() && S.getKind() == SDep::Anti) {
        Register Reg = S.getReg();
        if (Reg.isVirtual())
          CrossIterationNeeds.insert(std::make_pair(Reg.id(), IterNeed()))
              .first->second.set(0);
      } else if (S.isAssignedRegDep()) {
        int OStg = SMS.stageScheduled(S.getSUnit());
        if (OStg >= 0 && OStg != Stg) {
          Register Reg = S.getReg();
          if (Reg.isVirtual())
            CrossIterationNeeds.insert(std::make_pair(Reg.id(), IterNeed()))
                .first->second |= ((1 << (OStg - Stg)) - 1);
        }
      }
  }

  // Determine more-or-less what the proposed schedule (reversed) is going to
  // be; it might not be quite the same because the within-cycle ordering
  // created by SMSchedule depends upon changes to help with address offsets and
  // the like.
  std::vector<SUnit *> ProposedSchedule;
  for (int Cycle = SMS.getFinalCycle(); Cycle >= SMS.getFirstCycle(); --Cycle)
    for (int Stage = 0, StageEnd = SMS.getMaxStageCount(); Stage <= StageEnd;
         ++Stage) {
      std::deque<SUnit *> Instrs =
          SMS.getInstructions(Cycle + Stage * SMS.getInitiationInterval());
      std::sort(Instrs.begin(), Instrs.end(),
                [](SUnit *A, SUnit *B) { return A->NodeNum > B->NodeNum; });
      for (SUnit *SU : Instrs)
        ProposedSchedule.push_back(SU);
    }

  // Learn whether the last use/def of each cross-iteration register is a use or
  // def. If it is a def, RegisterPressure will implicitly increase max pressure
  // and we do not have to add the pressure.
  for (auto *SU : ProposedSchedule)
    for (ConstMIBundleOperands OperI(*SU->getInstr()); OperI.isValid();
         ++OperI) {
      auto MO = *OperI;
      if (!MO.isReg() || !MO.getReg())
        continue;
      Register Reg = MO.getReg();
      auto CIter = CrossIterationNeeds.find(Reg.id());
      if (CIter == CrossIterationNeeds.end() || CIter->second[LAST_IS_USE] ||
          CIter->second[SEEN_AS_LIVE])
        continue;
      if (MO.isDef() && !MO.isDead())
        CIter->second.set(SEEN_AS_LIVE);
      else if (MO.isUse())
        CIter->second.set(LAST_IS_USE);
    }
  for (auto &CI : CrossIterationNeeds)
    CI.second.reset(LAST_IS_USE);

  RegionPressure RecRegPressure;
  RegPressureTracker RPTracker(RecRegPressure);
  RegisterClassInfo RegClassInfo;
  RegClassInfo.runOnMachineFunction(*MF);
  RPTracker.init(MF, &RegClassInfo, nullptr, EndLoop->getParent(),
                 EndLoop->getParent()->end(), false, false);
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();

  bumpCrossIterationPressure(RPTracker, CrossIterationNeeds);

  for (auto *SU : ProposedSchedule) {
    MachineBasicBlock::const_iterator CurInstI = SU->getInstr();
    RPTracker.setPos(std::next(CurInstI));
    RPTracker.recede();

    // Track what cross-iteration registers would be seen as live
    for (ConstMIBundleOperands OperI(*CurInstI); OperI.isValid(); ++OperI) {
      auto MO = *OperI;
      if (!MO.isReg() || !MO.getReg())
        continue;
      Register Reg = MO.getReg();
      if (MO.isDef() && !MO.isDead()) {
        auto CIter = CrossIterationNeeds.find(Reg.id());
        if (CIter != CrossIterationNeeds.end()) {
          CIter->second.reset(0);
          CIter->second.reset(SEEN_AS_LIVE);
        }
      }
    }
    for (auto &S : SU->Preds) {
      auto Stg = SMS.stageScheduled(SU);
      if (S.isAssignedRegDep()) {
        Register Reg = S.getReg();
        auto CIter = CrossIterationNeeds.find(Reg.id());
        if (CIter != CrossIterationNeeds.end()) {
          auto Stg2 = SMS.stageScheduled(const_cast<SUnit *>(S.getSUnit()));
          assert(Stg2 <= Stg && "Data dependence upon earlier stage");
          if (Stg - Stg2 < MAX_STAGES)
            CIter->second.set(Stg - Stg2);
          CIter->second.set(SEEN_AS_LIVE);
        }
      }
    }

    bumpCrossIterationPressure(RPTracker, CrossIterationNeeds);
  }

  auto &P = RPTracker.getPressure().MaxSetPressure;
  for (unsigned I = 0, E = P.size(); I < E; ++I)
    if (P[I] > TRI->getRegPressureSetLimit(*MF, I)) {
      return true;
    }
  return false;
}

} // namespace

std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo>
ARMBaseInstrInfo::analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const {
  MachineBasicBlock::iterator I = LoopBB->getFirstTerminator();
  MachineBasicBlock *Preheader = *LoopBB->pred_begin();
  if (Preheader == LoopBB)
    Preheader = *std::next(LoopBB->pred_begin());

  if (I != LoopBB->end() && I->getOpcode() == ARM::t2Bcc) {
    // If the branch is a Bcc, then the CPSR should be set somewhere within the
    // block.  We need to determine the reaching definition of CPSR so that
    // it can be marked as non-pipelineable, allowing the pipeliner to force
    // it into stage 0 or give up if it cannot or will not do so.
    MachineInstr *CCSetter = nullptr;
    for (auto &L : LoopBB->instrs()) {
      if (L.isCall())
        return nullptr;
      if (isCPSRDefined(L))
        CCSetter = &L;
    }
    if (CCSetter)
      return std::make_unique<ARMPipelinerLoopInfo>(&*I, CCSetter);
    else
      return nullptr; // Unable to find the CC setter, so unable to guarantee
                      // that pipeline will work
  }

  // Recognize:
  //   preheader:
  //     %1 = t2DoopLoopStart %0
  //   loop:
  //     %2 = phi %1, <not loop>, %..., %loop
  //     %3 = t2LoopDec %2, <imm>
  //     t2LoopEnd %3, %loop

  if (I != LoopBB->end() && I->getOpcode() == ARM::t2LoopEnd) {
    for (auto &L : LoopBB->instrs())
      if (L.isCall())
        return nullptr;
      else if (isVCTP(&L))
        return nullptr;
    Register LoopDecResult = I->getOperand(0).getReg();
    MachineRegisterInfo &MRI = LoopBB->getParent()->getRegInfo();
    MachineInstr *LoopDec = MRI.getUniqueVRegDef(LoopDecResult);
    if (!LoopDec || LoopDec->getOpcode() != ARM::t2LoopDec)
      return nullptr;
    MachineInstr *LoopStart = nullptr;
    for (auto &J : Preheader->instrs())
      if (J.getOpcode() == ARM::t2DoLoopStart)
        LoopStart = &J;
    if (!LoopStart)
      return nullptr;
    return std::make_unique<ARMPipelinerLoopInfo>(&*I, LoopDec);
  }
  return nullptr;
}
