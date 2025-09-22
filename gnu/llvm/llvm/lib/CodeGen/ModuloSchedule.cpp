//===- ModuloSchedule.cpp - Software pipeline schedule expansion ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ModuloSchedule.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "pipeliner"
using namespace llvm;

static cl::opt<bool> SwapBranchTargetsMVE(
    "pipeliner-swap-branch-targets-mve", cl::Hidden, cl::init(false),
    cl::desc("Swap target blocks of a conditional branch for MVE expander"));

void ModuloSchedule::print(raw_ostream &OS) {
  for (MachineInstr *MI : ScheduledInstrs)
    OS << "[stage " << getStage(MI) << " @" << getCycle(MI) << "c] " << *MI;
}

//===----------------------------------------------------------------------===//
// ModuloScheduleExpander implementation
//===----------------------------------------------------------------------===//

/// Return the register values for  the operands of a Phi instruction.
/// This function assume the instruction is a Phi.
static void getPhiRegs(MachineInstr &Phi, MachineBasicBlock *Loop,
                       unsigned &InitVal, unsigned &LoopVal) {
  assert(Phi.isPHI() && "Expecting a Phi.");

  InitVal = 0;
  LoopVal = 0;
  for (unsigned i = 1, e = Phi.getNumOperands(); i != e; i += 2)
    if (Phi.getOperand(i + 1).getMBB() != Loop)
      InitVal = Phi.getOperand(i).getReg();
    else
      LoopVal = Phi.getOperand(i).getReg();

  assert(InitVal != 0 && LoopVal != 0 && "Unexpected Phi structure.");
}

/// Return the Phi register value that comes from the incoming block.
static unsigned getInitPhiReg(MachineInstr &Phi, MachineBasicBlock *LoopBB) {
  for (unsigned i = 1, e = Phi.getNumOperands(); i != e; i += 2)
    if (Phi.getOperand(i + 1).getMBB() != LoopBB)
      return Phi.getOperand(i).getReg();
  return 0;
}

/// Return the Phi register value that comes the loop block.
static unsigned getLoopPhiReg(MachineInstr &Phi, MachineBasicBlock *LoopBB) {
  for (unsigned i = 1, e = Phi.getNumOperands(); i != e; i += 2)
    if (Phi.getOperand(i + 1).getMBB() == LoopBB)
      return Phi.getOperand(i).getReg();
  return 0;
}

void ModuloScheduleExpander::expand() {
  BB = Schedule.getLoop()->getTopBlock();
  Preheader = *BB->pred_begin();
  if (Preheader == BB)
    Preheader = *std::next(BB->pred_begin());

  // Iterate over the definitions in each instruction, and compute the
  // stage difference for each use.  Keep the maximum value.
  for (MachineInstr *MI : Schedule.getInstructions()) {
    int DefStage = Schedule.getStage(MI);
    for (const MachineOperand &Op : MI->all_defs()) {
      Register Reg = Op.getReg();
      unsigned MaxDiff = 0;
      bool PhiIsSwapped = false;
      for (MachineOperand &UseOp : MRI.use_operands(Reg)) {
        MachineInstr *UseMI = UseOp.getParent();
        int UseStage = Schedule.getStage(UseMI);
        unsigned Diff = 0;
        if (UseStage != -1 && UseStage >= DefStage)
          Diff = UseStage - DefStage;
        if (MI->isPHI()) {
          if (isLoopCarried(*MI))
            ++Diff;
          else
            PhiIsSwapped = true;
        }
        MaxDiff = std::max(Diff, MaxDiff);
      }
      RegToStageDiff[Reg] = std::make_pair(MaxDiff, PhiIsSwapped);
    }
  }

  generatePipelinedLoop();
}

void ModuloScheduleExpander::generatePipelinedLoop() {
  LoopInfo = TII->analyzeLoopForPipelining(BB);
  assert(LoopInfo && "Must be able to analyze loop!");

  // Create a new basic block for the kernel and add it to the CFG.
  MachineBasicBlock *KernelBB = MF.CreateMachineBasicBlock(BB->getBasicBlock());

  unsigned MaxStageCount = Schedule.getNumStages() - 1;

  // Remember the registers that are used in different stages. The index is
  // the iteration, or stage, that the instruction is scheduled in.  This is
  // a map between register names in the original block and the names created
  // in each stage of the pipelined loop.
  ValueMapTy *VRMap = new ValueMapTy[(MaxStageCount + 1) * 2];

  // The renaming destination by Phis for the registers across stages.
  // This map is updated during Phis generation to point to the most recent
  // renaming destination.
  ValueMapTy *VRMapPhi = new ValueMapTy[(MaxStageCount + 1) * 2];

  InstrMapTy InstrMap;

  SmallVector<MachineBasicBlock *, 4> PrologBBs;

  // Generate the prolog instructions that set up the pipeline.
  generateProlog(MaxStageCount, KernelBB, VRMap, PrologBBs);
  MF.insert(BB->getIterator(), KernelBB);
  LIS.insertMBBInMaps(KernelBB);

  // Rearrange the instructions to generate the new, pipelined loop,
  // and update register names as needed.
  for (MachineInstr *CI : Schedule.getInstructions()) {
    if (CI->isPHI())
      continue;
    unsigned StageNum = Schedule.getStage(CI);
    MachineInstr *NewMI = cloneInstr(CI, MaxStageCount, StageNum);
    updateInstruction(NewMI, false, MaxStageCount, StageNum, VRMap);
    KernelBB->push_back(NewMI);
    InstrMap[NewMI] = CI;
  }

  // Copy any terminator instructions to the new kernel, and update
  // names as needed.
  for (MachineInstr &MI : BB->terminators()) {
    MachineInstr *NewMI = MF.CloneMachineInstr(&MI);
    updateInstruction(NewMI, false, MaxStageCount, 0, VRMap);
    KernelBB->push_back(NewMI);
    InstrMap[NewMI] = &MI;
  }

  NewKernel = KernelBB;
  KernelBB->transferSuccessors(BB);
  KernelBB->replaceSuccessor(BB, KernelBB);

  generateExistingPhis(KernelBB, PrologBBs.back(), KernelBB, KernelBB, VRMap,
                       InstrMap, MaxStageCount, MaxStageCount, false);
  generatePhis(KernelBB, PrologBBs.back(), KernelBB, KernelBB, VRMap, VRMapPhi,
               InstrMap, MaxStageCount, MaxStageCount, false);

  LLVM_DEBUG(dbgs() << "New block\n"; KernelBB->dump(););

  SmallVector<MachineBasicBlock *, 4> EpilogBBs;
  // Generate the epilog instructions to complete the pipeline.
  generateEpilog(MaxStageCount, KernelBB, BB, VRMap, VRMapPhi, EpilogBBs,
                 PrologBBs);

  // We need this step because the register allocation doesn't handle some
  // situations well, so we insert copies to help out.
  splitLifetimes(KernelBB, EpilogBBs);

  // Remove dead instructions due to loop induction variables.
  removeDeadInstructions(KernelBB, EpilogBBs);

  // Add branches between prolog and epilog blocks.
  addBranches(*Preheader, PrologBBs, KernelBB, EpilogBBs, VRMap);

  delete[] VRMap;
  delete[] VRMapPhi;
}

void ModuloScheduleExpander::cleanup() {
  // Remove the original loop since it's no longer referenced.
  for (auto &I : *BB)
    LIS.RemoveMachineInstrFromMaps(I);
  BB->clear();
  BB->eraseFromParent();
}

/// Generate the pipeline prolog code.
void ModuloScheduleExpander::generateProlog(unsigned LastStage,
                                            MachineBasicBlock *KernelBB,
                                            ValueMapTy *VRMap,
                                            MBBVectorTy &PrologBBs) {
  MachineBasicBlock *PredBB = Preheader;
  InstrMapTy InstrMap;

  // Generate a basic block for each stage, not including the last stage,
  // which will be generated in the kernel. Each basic block may contain
  // instructions from multiple stages/iterations.
  for (unsigned i = 0; i < LastStage; ++i) {
    // Create and insert the prolog basic block prior to the original loop
    // basic block.  The original loop is removed later.
    MachineBasicBlock *NewBB = MF.CreateMachineBasicBlock(BB->getBasicBlock());
    PrologBBs.push_back(NewBB);
    MF.insert(BB->getIterator(), NewBB);
    NewBB->transferSuccessors(PredBB);
    PredBB->addSuccessor(NewBB);
    PredBB = NewBB;
    LIS.insertMBBInMaps(NewBB);

    // Generate instructions for each appropriate stage. Process instructions
    // in original program order.
    for (int StageNum = i; StageNum >= 0; --StageNum) {
      for (MachineBasicBlock::iterator BBI = BB->instr_begin(),
                                       BBE = BB->getFirstTerminator();
           BBI != BBE; ++BBI) {
        if (Schedule.getStage(&*BBI) == StageNum) {
          if (BBI->isPHI())
            continue;
          MachineInstr *NewMI =
              cloneAndChangeInstr(&*BBI, i, (unsigned)StageNum);
          updateInstruction(NewMI, false, i, (unsigned)StageNum, VRMap);
          NewBB->push_back(NewMI);
          InstrMap[NewMI] = &*BBI;
        }
      }
    }
    rewritePhiValues(NewBB, i, VRMap, InstrMap);
    LLVM_DEBUG({
      dbgs() << "prolog:\n";
      NewBB->dump();
    });
  }

  PredBB->replaceSuccessor(BB, KernelBB);

  // Check if we need to remove the branch from the preheader to the original
  // loop, and replace it with a branch to the new loop.
  unsigned numBranches = TII->removeBranch(*Preheader);
  if (numBranches) {
    SmallVector<MachineOperand, 0> Cond;
    TII->insertBranch(*Preheader, PrologBBs[0], nullptr, Cond, DebugLoc());
  }
}

/// Generate the pipeline epilog code. The epilog code finishes the iterations
/// that were started in either the prolog or the kernel.  We create a basic
/// block for each stage that needs to complete.
void ModuloScheduleExpander::generateEpilog(
    unsigned LastStage, MachineBasicBlock *KernelBB, MachineBasicBlock *OrigBB,
    ValueMapTy *VRMap, ValueMapTy *VRMapPhi, MBBVectorTy &EpilogBBs,
    MBBVectorTy &PrologBBs) {
  // We need to change the branch from the kernel to the first epilog block, so
  // this call to analyze branch uses the kernel rather than the original BB.
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  bool checkBranch = TII->analyzeBranch(*KernelBB, TBB, FBB, Cond);
  assert(!checkBranch && "generateEpilog must be able to analyze the branch");
  if (checkBranch)
    return;

  MachineBasicBlock::succ_iterator LoopExitI = KernelBB->succ_begin();
  if (*LoopExitI == KernelBB)
    ++LoopExitI;
  assert(LoopExitI != KernelBB->succ_end() && "Expecting a successor");
  MachineBasicBlock *LoopExitBB = *LoopExitI;

  MachineBasicBlock *PredBB = KernelBB;
  MachineBasicBlock *EpilogStart = LoopExitBB;
  InstrMapTy InstrMap;

  // Generate a basic block for each stage, not including the last stage,
  // which was generated for the kernel.  Each basic block may contain
  // instructions from multiple stages/iterations.
  int EpilogStage = LastStage + 1;
  for (unsigned i = LastStage; i >= 1; --i, ++EpilogStage) {
    MachineBasicBlock *NewBB = MF.CreateMachineBasicBlock();
    EpilogBBs.push_back(NewBB);
    MF.insert(BB->getIterator(), NewBB);

    PredBB->replaceSuccessor(LoopExitBB, NewBB);
    NewBB->addSuccessor(LoopExitBB);
    LIS.insertMBBInMaps(NewBB);

    if (EpilogStart == LoopExitBB)
      EpilogStart = NewBB;

    // Add instructions to the epilog depending on the current block.
    // Process instructions in original program order.
    for (unsigned StageNum = i; StageNum <= LastStage; ++StageNum) {
      for (auto &BBI : *BB) {
        if (BBI.isPHI())
          continue;
        MachineInstr *In = &BBI;
        if ((unsigned)Schedule.getStage(In) == StageNum) {
          // Instructions with memoperands in the epilog are updated with
          // conservative values.
          MachineInstr *NewMI = cloneInstr(In, UINT_MAX, 0);
          updateInstruction(NewMI, i == 1, EpilogStage, 0, VRMap);
          NewBB->push_back(NewMI);
          InstrMap[NewMI] = In;
        }
      }
    }
    generateExistingPhis(NewBB, PrologBBs[i - 1], PredBB, KernelBB, VRMap,
                         InstrMap, LastStage, EpilogStage, i == 1);
    generatePhis(NewBB, PrologBBs[i - 1], PredBB, KernelBB, VRMap, VRMapPhi,
                 InstrMap, LastStage, EpilogStage, i == 1);
    PredBB = NewBB;

    LLVM_DEBUG({
      dbgs() << "epilog:\n";
      NewBB->dump();
    });
  }

  // Fix any Phi nodes in the loop exit block.
  LoopExitBB->replacePhiUsesWith(BB, PredBB);

  // Create a branch to the new epilog from the kernel.
  // Remove the original branch and add a new branch to the epilog.
  TII->removeBranch(*KernelBB);
  assert((OrigBB == TBB || OrigBB == FBB) &&
         "Unable to determine looping branch direction");
  if (OrigBB != TBB)
    TII->insertBranch(*KernelBB, EpilogStart, KernelBB, Cond, DebugLoc());
  else
    TII->insertBranch(*KernelBB, KernelBB, EpilogStart, Cond, DebugLoc());
  // Add a branch to the loop exit.
  if (EpilogBBs.size() > 0) {
    MachineBasicBlock *LastEpilogBB = EpilogBBs.back();
    SmallVector<MachineOperand, 4> Cond1;
    TII->insertBranch(*LastEpilogBB, LoopExitBB, nullptr, Cond1, DebugLoc());
  }
}

/// Replace all uses of FromReg that appear outside the specified
/// basic block with ToReg.
static void replaceRegUsesAfterLoop(unsigned FromReg, unsigned ToReg,
                                    MachineBasicBlock *MBB,
                                    MachineRegisterInfo &MRI,
                                    LiveIntervals &LIS) {
  for (MachineOperand &O :
       llvm::make_early_inc_range(MRI.use_operands(FromReg)))
    if (O.getParent()->getParent() != MBB)
      O.setReg(ToReg);
  if (!LIS.hasInterval(ToReg))
    LIS.createEmptyInterval(ToReg);
}

/// Return true if the register has a use that occurs outside the
/// specified loop.
static bool hasUseAfterLoop(unsigned Reg, MachineBasicBlock *BB,
                            MachineRegisterInfo &MRI) {
  for (const MachineOperand &MO : MRI.use_operands(Reg))
    if (MO.getParent()->getParent() != BB)
      return true;
  return false;
}

/// Generate Phis for the specific block in the generated pipelined code.
/// This function looks at the Phis from the original code to guide the
/// creation of new Phis.
void ModuloScheduleExpander::generateExistingPhis(
    MachineBasicBlock *NewBB, MachineBasicBlock *BB1, MachineBasicBlock *BB2,
    MachineBasicBlock *KernelBB, ValueMapTy *VRMap, InstrMapTy &InstrMap,
    unsigned LastStageNum, unsigned CurStageNum, bool IsLast) {
  // Compute the stage number for the initial value of the Phi, which
  // comes from the prolog. The prolog to use depends on to which kernel/
  // epilog that we're adding the Phi.
  unsigned PrologStage = 0;
  unsigned PrevStage = 0;
  bool InKernel = (LastStageNum == CurStageNum);
  if (InKernel) {
    PrologStage = LastStageNum - 1;
    PrevStage = CurStageNum;
  } else {
    PrologStage = LastStageNum - (CurStageNum - LastStageNum);
    PrevStage = LastStageNum + (CurStageNum - LastStageNum) - 1;
  }

  for (MachineBasicBlock::iterator BBI = BB->instr_begin(),
                                   BBE = BB->getFirstNonPHI();
       BBI != BBE; ++BBI) {
    Register Def = BBI->getOperand(0).getReg();

    unsigned InitVal = 0;
    unsigned LoopVal = 0;
    getPhiRegs(*BBI, BB, InitVal, LoopVal);

    unsigned PhiOp1 = 0;
    // The Phi value from the loop body typically is defined in the loop, but
    // not always. So, we need to check if the value is defined in the loop.
    unsigned PhiOp2 = LoopVal;
    if (VRMap[LastStageNum].count(LoopVal))
      PhiOp2 = VRMap[LastStageNum][LoopVal];

    int StageScheduled = Schedule.getStage(&*BBI);
    int LoopValStage = Schedule.getStage(MRI.getVRegDef(LoopVal));
    unsigned NumStages = getStagesForReg(Def, CurStageNum);
    if (NumStages == 0) {
      // We don't need to generate a Phi anymore, but we need to rename any uses
      // of the Phi value.
      unsigned NewReg = VRMap[PrevStage][LoopVal];
      rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, 0, &*BBI, Def,
                            InitVal, NewReg);
      if (VRMap[CurStageNum].count(LoopVal))
        VRMap[CurStageNum][Def] = VRMap[CurStageNum][LoopVal];
    }
    // Adjust the number of Phis needed depending on the number of prologs left,
    // and the distance from where the Phi is first scheduled. The number of
    // Phis cannot exceed the number of prolog stages. Each stage can
    // potentially define two values.
    unsigned MaxPhis = PrologStage + 2;
    if (!InKernel && (int)PrologStage <= LoopValStage)
      MaxPhis = std::max((int)MaxPhis - (int)LoopValStage, 1);
    unsigned NumPhis = std::min(NumStages, MaxPhis);

    unsigned NewReg = 0;
    unsigned AccessStage = (LoopValStage != -1) ? LoopValStage : StageScheduled;
    // In the epilog, we may need to look back one stage to get the correct
    // Phi name, because the epilog and prolog blocks execute the same stage.
    // The correct name is from the previous block only when the Phi has
    // been completely scheduled prior to the epilog, and Phi value is not
    // needed in multiple stages.
    int StageDiff = 0;
    if (!InKernel && StageScheduled >= LoopValStage && AccessStage == 0 &&
        NumPhis == 1)
      StageDiff = 1;
    // Adjust the computations below when the phi and the loop definition
    // are scheduled in different stages.
    if (InKernel && LoopValStage != -1 && StageScheduled > LoopValStage)
      StageDiff = StageScheduled - LoopValStage;
    for (unsigned np = 0; np < NumPhis; ++np) {
      // If the Phi hasn't been scheduled, then use the initial Phi operand
      // value. Otherwise, use the scheduled version of the instruction. This
      // is a little complicated when a Phi references another Phi.
      if (np > PrologStage || StageScheduled >= (int)LastStageNum)
        PhiOp1 = InitVal;
      // Check if the Phi has already been scheduled in a prolog stage.
      else if (PrologStage >= AccessStage + StageDiff + np &&
               VRMap[PrologStage - StageDiff - np].count(LoopVal) != 0)
        PhiOp1 = VRMap[PrologStage - StageDiff - np][LoopVal];
      // Check if the Phi has already been scheduled, but the loop instruction
      // is either another Phi, or doesn't occur in the loop.
      else if (PrologStage >= AccessStage + StageDiff + np) {
        // If the Phi references another Phi, we need to examine the other
        // Phi to get the correct value.
        PhiOp1 = LoopVal;
        MachineInstr *InstOp1 = MRI.getVRegDef(PhiOp1);
        int Indirects = 1;
        while (InstOp1 && InstOp1->isPHI() && InstOp1->getParent() == BB) {
          int PhiStage = Schedule.getStage(InstOp1);
          if ((int)(PrologStage - StageDiff - np) < PhiStage + Indirects)
            PhiOp1 = getInitPhiReg(*InstOp1, BB);
          else
            PhiOp1 = getLoopPhiReg(*InstOp1, BB);
          InstOp1 = MRI.getVRegDef(PhiOp1);
          int PhiOpStage = Schedule.getStage(InstOp1);
          int StageAdj = (PhiOpStage != -1 ? PhiStage - PhiOpStage : 0);
          if (PhiOpStage != -1 && PrologStage - StageAdj >= Indirects + np &&
              VRMap[PrologStage - StageAdj - Indirects - np].count(PhiOp1)) {
            PhiOp1 = VRMap[PrologStage - StageAdj - Indirects - np][PhiOp1];
            break;
          }
          ++Indirects;
        }
      } else
        PhiOp1 = InitVal;
      // If this references a generated Phi in the kernel, get the Phi operand
      // from the incoming block.
      if (MachineInstr *InstOp1 = MRI.getVRegDef(PhiOp1))
        if (InstOp1->isPHI() && InstOp1->getParent() == KernelBB)
          PhiOp1 = getInitPhiReg(*InstOp1, KernelBB);

      MachineInstr *PhiInst = MRI.getVRegDef(LoopVal);
      bool LoopDefIsPhi = PhiInst && PhiInst->isPHI();
      // In the epilog, a map lookup is needed to get the value from the kernel,
      // or previous epilog block. How is does this depends on if the
      // instruction is scheduled in the previous block.
      if (!InKernel) {
        int StageDiffAdj = 0;
        if (LoopValStage != -1 && StageScheduled > LoopValStage)
          StageDiffAdj = StageScheduled - LoopValStage;
        // Use the loop value defined in the kernel, unless the kernel
        // contains the last definition of the Phi.
        if (np == 0 && PrevStage == LastStageNum &&
            (StageScheduled != 0 || LoopValStage != 0) &&
            VRMap[PrevStage - StageDiffAdj].count(LoopVal))
          PhiOp2 = VRMap[PrevStage - StageDiffAdj][LoopVal];
        // Use the value defined by the Phi. We add one because we switch
        // from looking at the loop value to the Phi definition.
        else if (np > 0 && PrevStage == LastStageNum &&
                 VRMap[PrevStage - np + 1].count(Def))
          PhiOp2 = VRMap[PrevStage - np + 1][Def];
        // Use the loop value defined in the kernel.
        else if (static_cast<unsigned>(LoopValStage) > PrologStage + 1 &&
                 VRMap[PrevStage - StageDiffAdj - np].count(LoopVal))
          PhiOp2 = VRMap[PrevStage - StageDiffAdj - np][LoopVal];
        // Use the value defined by the Phi, unless we're generating the first
        // epilog and the Phi refers to a Phi in a different stage.
        else if (VRMap[PrevStage - np].count(Def) &&
                 (!LoopDefIsPhi || (PrevStage != LastStageNum) ||
                  (LoopValStage == StageScheduled)))
          PhiOp2 = VRMap[PrevStage - np][Def];
      }

      // Check if we can reuse an existing Phi. This occurs when a Phi
      // references another Phi, and the other Phi is scheduled in an
      // earlier stage. We can try to reuse an existing Phi up until the last
      // stage of the current Phi.
      if (LoopDefIsPhi) {
        if (static_cast<int>(PrologStage - np) >= StageScheduled) {
          int LVNumStages = getStagesForPhi(LoopVal);
          int StageDiff = (StageScheduled - LoopValStage);
          LVNumStages -= StageDiff;
          // Make sure the loop value Phi has been processed already.
          if (LVNumStages > (int)np && VRMap[CurStageNum].count(LoopVal)) {
            NewReg = PhiOp2;
            unsigned ReuseStage = CurStageNum;
            if (isLoopCarried(*PhiInst))
              ReuseStage -= LVNumStages;
            // Check if the Phi to reuse has been generated yet. If not, then
            // there is nothing to reuse.
            if (VRMap[ReuseStage - np].count(LoopVal)) {
              NewReg = VRMap[ReuseStage - np][LoopVal];

              rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, np, &*BBI,
                                    Def, NewReg);
              // Update the map with the new Phi name.
              VRMap[CurStageNum - np][Def] = NewReg;
              PhiOp2 = NewReg;
              if (VRMap[LastStageNum - np - 1].count(LoopVal))
                PhiOp2 = VRMap[LastStageNum - np - 1][LoopVal];

              if (IsLast && np == NumPhis - 1)
                replaceRegUsesAfterLoop(Def, NewReg, BB, MRI, LIS);
              continue;
            }
          }
        }
        if (InKernel && StageDiff > 0 &&
            VRMap[CurStageNum - StageDiff - np].count(LoopVal))
          PhiOp2 = VRMap[CurStageNum - StageDiff - np][LoopVal];
      }

      const TargetRegisterClass *RC = MRI.getRegClass(Def);
      NewReg = MRI.createVirtualRegister(RC);

      MachineInstrBuilder NewPhi =
          BuildMI(*NewBB, NewBB->getFirstNonPHI(), DebugLoc(),
                  TII->get(TargetOpcode::PHI), NewReg);
      NewPhi.addReg(PhiOp1).addMBB(BB1);
      NewPhi.addReg(PhiOp2).addMBB(BB2);
      if (np == 0)
        InstrMap[NewPhi] = &*BBI;

      // We define the Phis after creating the new pipelined code, so
      // we need to rename the Phi values in scheduled instructions.

      unsigned PrevReg = 0;
      if (InKernel && VRMap[PrevStage - np].count(LoopVal))
        PrevReg = VRMap[PrevStage - np][LoopVal];
      rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, np, &*BBI, Def,
                            NewReg, PrevReg);
      // If the Phi has been scheduled, use the new name for rewriting.
      if (VRMap[CurStageNum - np].count(Def)) {
        unsigned R = VRMap[CurStageNum - np][Def];
        rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, np, &*BBI, R,
                              NewReg);
      }

      // Check if we need to rename any uses that occurs after the loop. The
      // register to replace depends on whether the Phi is scheduled in the
      // epilog.
      if (IsLast && np == NumPhis - 1)
        replaceRegUsesAfterLoop(Def, NewReg, BB, MRI, LIS);

      // In the kernel, a dependent Phi uses the value from this Phi.
      if (InKernel)
        PhiOp2 = NewReg;

      // Update the map with the new Phi name.
      VRMap[CurStageNum - np][Def] = NewReg;
    }

    while (NumPhis++ < NumStages) {
      rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, NumPhis, &*BBI, Def,
                            NewReg, 0);
    }

    // Check if we need to rename a Phi that has been eliminated due to
    // scheduling.
    if (NumStages == 0 && IsLast && VRMap[CurStageNum].count(LoopVal))
      replaceRegUsesAfterLoop(Def, VRMap[CurStageNum][LoopVal], BB, MRI, LIS);
  }
}

/// Generate Phis for the specified block in the generated pipelined code.
/// These are new Phis needed because the definition is scheduled after the
/// use in the pipelined sequence.
void ModuloScheduleExpander::generatePhis(
    MachineBasicBlock *NewBB, MachineBasicBlock *BB1, MachineBasicBlock *BB2,
    MachineBasicBlock *KernelBB, ValueMapTy *VRMap, ValueMapTy *VRMapPhi,
    InstrMapTy &InstrMap, unsigned LastStageNum, unsigned CurStageNum,
    bool IsLast) {
  // Compute the stage number that contains the initial Phi value, and
  // the Phi from the previous stage.
  unsigned PrologStage = 0;
  unsigned PrevStage = 0;
  unsigned StageDiff = CurStageNum - LastStageNum;
  bool InKernel = (StageDiff == 0);
  if (InKernel) {
    PrologStage = LastStageNum - 1;
    PrevStage = CurStageNum;
  } else {
    PrologStage = LastStageNum - StageDiff;
    PrevStage = LastStageNum + StageDiff - 1;
  }

  for (MachineBasicBlock::iterator BBI = BB->getFirstNonPHI(),
                                   BBE = BB->instr_end();
       BBI != BBE; ++BBI) {
    for (unsigned i = 0, e = BBI->getNumOperands(); i != e; ++i) {
      MachineOperand &MO = BBI->getOperand(i);
      if (!MO.isReg() || !MO.isDef() || !MO.getReg().isVirtual())
        continue;

      int StageScheduled = Schedule.getStage(&*BBI);
      assert(StageScheduled != -1 && "Expecting scheduled instruction.");
      Register Def = MO.getReg();
      unsigned NumPhis = getStagesForReg(Def, CurStageNum);
      // An instruction scheduled in stage 0 and is used after the loop
      // requires a phi in the epilog for the last definition from either
      // the kernel or prolog.
      if (!InKernel && NumPhis == 0 && StageScheduled == 0 &&
          hasUseAfterLoop(Def, BB, MRI))
        NumPhis = 1;
      if (!InKernel && (unsigned)StageScheduled > PrologStage)
        continue;

      unsigned PhiOp2;
      if (InKernel) {
        PhiOp2 = VRMap[PrevStage][Def];
        if (MachineInstr *InstOp2 = MRI.getVRegDef(PhiOp2))
          if (InstOp2->isPHI() && InstOp2->getParent() == NewBB)
            PhiOp2 = getLoopPhiReg(*InstOp2, BB2);
      }
      // The number of Phis can't exceed the number of prolog stages. The
      // prolog stage number is zero based.
      if (NumPhis > PrologStage + 1 - StageScheduled)
        NumPhis = PrologStage + 1 - StageScheduled;
      for (unsigned np = 0; np < NumPhis; ++np) {
        // Example for
        // Org:
        //   %Org = ... (Scheduled at Stage#0, NumPhi = 2)
        //
        // Prolog0 (Stage0):
        //   %Clone0 = ...
        // Prolog1 (Stage1):
        //   %Clone1 = ...
        // Kernel (Stage2):
        //   %Phi0 = Phi %Clone1, Prolog1, %Clone2, Kernel
        //   %Phi1 = Phi %Clone0, Prolog1, %Phi0, Kernel
        //   %Clone2 = ...
        // Epilog0 (Stage3):
        //   %Phi2 = Phi %Clone1, Prolog1, %Clone2, Kernel
        //   %Phi3 = Phi %Clone0, Prolog1, %Phi0, Kernel
        // Epilog1 (Stage4):
        //   %Phi4 = Phi %Clone0, Prolog0, %Phi2, Epilog0
        //
        // VRMap = {0: %Clone0, 1: %Clone1, 2: %Clone2}
        // VRMapPhi (after Kernel) = {0: %Phi1, 1: %Phi0}
        // VRMapPhi (after Epilog0) = {0: %Phi3, 1: %Phi2}

        unsigned PhiOp1 = VRMap[PrologStage][Def];
        if (np <= PrologStage)
          PhiOp1 = VRMap[PrologStage - np][Def];
        if (!InKernel) {
          if (PrevStage == LastStageNum && np == 0)
            PhiOp2 = VRMap[LastStageNum][Def];
          else
            PhiOp2 = VRMapPhi[PrevStage - np][Def];
        }

        const TargetRegisterClass *RC = MRI.getRegClass(Def);
        Register NewReg = MRI.createVirtualRegister(RC);

        MachineInstrBuilder NewPhi =
            BuildMI(*NewBB, NewBB->getFirstNonPHI(), DebugLoc(),
                    TII->get(TargetOpcode::PHI), NewReg);
        NewPhi.addReg(PhiOp1).addMBB(BB1);
        NewPhi.addReg(PhiOp2).addMBB(BB2);
        if (np == 0)
          InstrMap[NewPhi] = &*BBI;

        // Rewrite uses and update the map. The actions depend upon whether
        // we generating code for the kernel or epilog blocks.
        if (InKernel) {
          rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, np, &*BBI, PhiOp1,
                                NewReg);
          rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, np, &*BBI, PhiOp2,
                                NewReg);

          PhiOp2 = NewReg;
          VRMapPhi[PrevStage - np - 1][Def] = NewReg;
        } else {
          VRMapPhi[CurStageNum - np][Def] = NewReg;
          if (np == NumPhis - 1)
            rewriteScheduledInstr(NewBB, InstrMap, CurStageNum, np, &*BBI, Def,
                                  NewReg);
        }
        if (IsLast && np == NumPhis - 1)
          replaceRegUsesAfterLoop(Def, NewReg, BB, MRI, LIS);
      }
    }
  }
}

/// Remove instructions that generate values with no uses.
/// Typically, these are induction variable operations that generate values
/// used in the loop itself.  A dead instruction has a definition with
/// no uses, or uses that occur in the original loop only.
void ModuloScheduleExpander::removeDeadInstructions(MachineBasicBlock *KernelBB,
                                                    MBBVectorTy &EpilogBBs) {
  // For each epilog block, check that the value defined by each instruction
  // is used.  If not, delete it.
  for (MachineBasicBlock *MBB : llvm::reverse(EpilogBBs))
    for (MachineBasicBlock::reverse_instr_iterator MI = MBB->instr_rbegin(),
                                                   ME = MBB->instr_rend();
         MI != ME;) {
      // From DeadMachineInstructionElem. Don't delete inline assembly.
      if (MI->isInlineAsm()) {
        ++MI;
        continue;
      }
      bool SawStore = false;
      // Check if it's safe to remove the instruction due to side effects.
      // We can, and want to, remove Phis here.
      if (!MI->isSafeToMove(nullptr, SawStore) && !MI->isPHI()) {
        ++MI;
        continue;
      }
      bool used = true;
      for (const MachineOperand &MO : MI->all_defs()) {
        Register reg = MO.getReg();
        // Assume physical registers are used, unless they are marked dead.
        if (reg.isPhysical()) {
          used = !MO.isDead();
          if (used)
            break;
          continue;
        }
        unsigned realUses = 0;
        for (const MachineOperand &U : MRI.use_operands(reg)) {
          // Check if there are any uses that occur only in the original
          // loop.  If so, that's not a real use.
          if (U.getParent()->getParent() != BB) {
            realUses++;
            used = true;
            break;
          }
        }
        if (realUses > 0)
          break;
        used = false;
      }
      if (!used) {
        LIS.RemoveMachineInstrFromMaps(*MI);
        MI++->eraseFromParent();
        continue;
      }
      ++MI;
    }
  // In the kernel block, check if we can remove a Phi that generates a value
  // used in an instruction removed in the epilog block.
  for (MachineInstr &MI : llvm::make_early_inc_range(KernelBB->phis())) {
    Register reg = MI.getOperand(0).getReg();
    if (MRI.use_begin(reg) == MRI.use_end()) {
      LIS.RemoveMachineInstrFromMaps(MI);
      MI.eraseFromParent();
    }
  }
}

/// For loop carried definitions, we split the lifetime of a virtual register
/// that has uses past the definition in the next iteration. A copy with a new
/// virtual register is inserted before the definition, which helps with
/// generating a better register assignment.
///
///   v1 = phi(a, v2)     v1 = phi(a, v2)
///   v2 = phi(b, v3)     v2 = phi(b, v3)
///   v3 = ..             v4 = copy v1
///   .. = V1             v3 = ..
///                       .. = v4
void ModuloScheduleExpander::splitLifetimes(MachineBasicBlock *KernelBB,
                                            MBBVectorTy &EpilogBBs) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  for (auto &PHI : KernelBB->phis()) {
    Register Def = PHI.getOperand(0).getReg();
    // Check for any Phi definition that used as an operand of another Phi
    // in the same block.
    for (MachineRegisterInfo::use_instr_iterator I = MRI.use_instr_begin(Def),
                                                 E = MRI.use_instr_end();
         I != E; ++I) {
      if (I->isPHI() && I->getParent() == KernelBB) {
        // Get the loop carried definition.
        unsigned LCDef = getLoopPhiReg(PHI, KernelBB);
        if (!LCDef)
          continue;
        MachineInstr *MI = MRI.getVRegDef(LCDef);
        if (!MI || MI->getParent() != KernelBB || MI->isPHI())
          continue;
        // Search through the rest of the block looking for uses of the Phi
        // definition. If one occurs, then split the lifetime.
        unsigned SplitReg = 0;
        for (auto &BBJ : make_range(MachineBasicBlock::instr_iterator(MI),
                                    KernelBB->instr_end()))
          if (BBJ.readsRegister(Def, /*TRI=*/nullptr)) {
            // We split the lifetime when we find the first use.
            if (SplitReg == 0) {
              SplitReg = MRI.createVirtualRegister(MRI.getRegClass(Def));
              BuildMI(*KernelBB, MI, MI->getDebugLoc(),
                      TII->get(TargetOpcode::COPY), SplitReg)
                  .addReg(Def);
            }
            BBJ.substituteRegister(Def, SplitReg, 0, *TRI);
          }
        if (!SplitReg)
          continue;
        // Search through each of the epilog blocks for any uses to be renamed.
        for (auto &Epilog : EpilogBBs)
          for (auto &I : *Epilog)
            if (I.readsRegister(Def, /*TRI=*/nullptr))
              I.substituteRegister(Def, SplitReg, 0, *TRI);
        break;
      }
    }
  }
}

/// Remove the incoming block from the Phis in a basic block.
static void removePhis(MachineBasicBlock *BB, MachineBasicBlock *Incoming) {
  for (MachineInstr &MI : *BB) {
    if (!MI.isPHI())
      break;
    for (unsigned i = 1, e = MI.getNumOperands(); i != e; i += 2)
      if (MI.getOperand(i + 1).getMBB() == Incoming) {
        MI.removeOperand(i + 1);
        MI.removeOperand(i);
        break;
      }
  }
}

/// Create branches from each prolog basic block to the appropriate epilog
/// block.  These edges are needed if the loop ends before reaching the
/// kernel.
void ModuloScheduleExpander::addBranches(MachineBasicBlock &PreheaderBB,
                                         MBBVectorTy &PrologBBs,
                                         MachineBasicBlock *KernelBB,
                                         MBBVectorTy &EpilogBBs,
                                         ValueMapTy *VRMap) {
  assert(PrologBBs.size() == EpilogBBs.size() && "Prolog/Epilog mismatch");
  MachineBasicBlock *LastPro = KernelBB;
  MachineBasicBlock *LastEpi = KernelBB;

  // Start from the blocks connected to the kernel and work "out"
  // to the first prolog and the last epilog blocks.
  SmallVector<MachineInstr *, 4> PrevInsts;
  unsigned MaxIter = PrologBBs.size() - 1;
  for (unsigned i = 0, j = MaxIter; i <= MaxIter; ++i, --j) {
    // Add branches to the prolog that go to the corresponding
    // epilog, and the fall-thru prolog/kernel block.
    MachineBasicBlock *Prolog = PrologBBs[j];
    MachineBasicBlock *Epilog = EpilogBBs[i];

    SmallVector<MachineOperand, 4> Cond;
    std::optional<bool> StaticallyGreater =
        LoopInfo->createTripCountGreaterCondition(j + 1, *Prolog, Cond);
    unsigned numAdded = 0;
    if (!StaticallyGreater) {
      Prolog->addSuccessor(Epilog);
      numAdded = TII->insertBranch(*Prolog, Epilog, LastPro, Cond, DebugLoc());
    } else if (*StaticallyGreater == false) {
      Prolog->addSuccessor(Epilog);
      Prolog->removeSuccessor(LastPro);
      LastEpi->removeSuccessor(Epilog);
      numAdded = TII->insertBranch(*Prolog, Epilog, nullptr, Cond, DebugLoc());
      removePhis(Epilog, LastEpi);
      // Remove the blocks that are no longer referenced.
      if (LastPro != LastEpi) {
        LastEpi->clear();
        LastEpi->eraseFromParent();
      }
      if (LastPro == KernelBB) {
        LoopInfo->disposed();
        NewKernel = nullptr;
      }
      LastPro->clear();
      LastPro->eraseFromParent();
    } else {
      numAdded = TII->insertBranch(*Prolog, LastPro, nullptr, Cond, DebugLoc());
      removePhis(Epilog, Prolog);
    }
    LastPro = Prolog;
    LastEpi = Epilog;
    for (MachineBasicBlock::reverse_instr_iterator I = Prolog->instr_rbegin(),
                                                   E = Prolog->instr_rend();
         I != E && numAdded > 0; ++I, --numAdded)
      updateInstruction(&*I, false, j, 0, VRMap);
  }

  if (NewKernel) {
    LoopInfo->setPreheader(PrologBBs[MaxIter]);
    LoopInfo->adjustTripCount(-(MaxIter + 1));
  }
}

/// Return true if we can compute the amount the instruction changes
/// during each iteration. Set Delta to the amount of the change.
bool ModuloScheduleExpander::computeDelta(MachineInstr &MI, unsigned &Delta) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const MachineOperand *BaseOp;
  int64_t Offset;
  bool OffsetIsScalable;
  if (!TII->getMemOperandWithOffset(MI, BaseOp, Offset, OffsetIsScalable, TRI))
    return false;

  // FIXME: This algorithm assumes instructions have fixed-size offsets.
  if (OffsetIsScalable)
    return false;

  if (!BaseOp->isReg())
    return false;

  Register BaseReg = BaseOp->getReg();

  MachineRegisterInfo &MRI = MF.getRegInfo();
  // Check if there is a Phi. If so, get the definition in the loop.
  MachineInstr *BaseDef = MRI.getVRegDef(BaseReg);
  if (BaseDef && BaseDef->isPHI()) {
    BaseReg = getLoopPhiReg(*BaseDef, MI.getParent());
    BaseDef = MRI.getVRegDef(BaseReg);
  }
  if (!BaseDef)
    return false;

  int D = 0;
  if (!TII->getIncrementValue(*BaseDef, D) && D >= 0)
    return false;

  Delta = D;
  return true;
}

/// Update the memory operand with a new offset when the pipeliner
/// generates a new copy of the instruction that refers to a
/// different memory location.
void ModuloScheduleExpander::updateMemOperands(MachineInstr &NewMI,
                                               MachineInstr &OldMI,
                                               unsigned Num) {
  if (Num == 0)
    return;
  // If the instruction has memory operands, then adjust the offset
  // when the instruction appears in different stages.
  if (NewMI.memoperands_empty())
    return;
  SmallVector<MachineMemOperand *, 2> NewMMOs;
  for (MachineMemOperand *MMO : NewMI.memoperands()) {
    // TODO: Figure out whether isAtomic is really necessary (see D57601).
    if (MMO->isVolatile() || MMO->isAtomic() ||
        (MMO->isInvariant() && MMO->isDereferenceable()) ||
        (!MMO->getValue())) {
      NewMMOs.push_back(MMO);
      continue;
    }
    unsigned Delta;
    if (Num != UINT_MAX && computeDelta(OldMI, Delta)) {
      int64_t AdjOffset = Delta * Num;
      NewMMOs.push_back(
          MF.getMachineMemOperand(MMO, AdjOffset, MMO->getSize()));
    } else {
      NewMMOs.push_back(MF.getMachineMemOperand(
          MMO, 0, LocationSize::beforeOrAfterPointer()));
    }
  }
  NewMI.setMemRefs(MF, NewMMOs);
}

/// Clone the instruction for the new pipelined loop and update the
/// memory operands, if needed.
MachineInstr *ModuloScheduleExpander::cloneInstr(MachineInstr *OldMI,
                                                 unsigned CurStageNum,
                                                 unsigned InstStageNum) {
  MachineInstr *NewMI = MF.CloneMachineInstr(OldMI);
  updateMemOperands(*NewMI, *OldMI, CurStageNum - InstStageNum);
  return NewMI;
}

/// Clone the instruction for the new pipelined loop. If needed, this
/// function updates the instruction using the values saved in the
/// InstrChanges structure.
MachineInstr *ModuloScheduleExpander::cloneAndChangeInstr(
    MachineInstr *OldMI, unsigned CurStageNum, unsigned InstStageNum) {
  MachineInstr *NewMI = MF.CloneMachineInstr(OldMI);
  auto It = InstrChanges.find(OldMI);
  if (It != InstrChanges.end()) {
    std::pair<unsigned, int64_t> RegAndOffset = It->second;
    unsigned BasePos, OffsetPos;
    if (!TII->getBaseAndOffsetPosition(*OldMI, BasePos, OffsetPos))
      return nullptr;
    int64_t NewOffset = OldMI->getOperand(OffsetPos).getImm();
    MachineInstr *LoopDef = findDefInLoop(RegAndOffset.first);
    if (Schedule.getStage(LoopDef) > (signed)InstStageNum)
      NewOffset += RegAndOffset.second * (CurStageNum - InstStageNum);
    NewMI->getOperand(OffsetPos).setImm(NewOffset);
  }
  updateMemOperands(*NewMI, *OldMI, CurStageNum - InstStageNum);
  return NewMI;
}

/// Update the machine instruction with new virtual registers.  This
/// function may change the definitions and/or uses.
void ModuloScheduleExpander::updateInstruction(MachineInstr *NewMI,
                                               bool LastDef,
                                               unsigned CurStageNum,
                                               unsigned InstrStageNum,
                                               ValueMapTy *VRMap) {
  for (MachineOperand &MO : NewMI->operands()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    Register reg = MO.getReg();
    if (MO.isDef()) {
      // Create a new virtual register for the definition.
      const TargetRegisterClass *RC = MRI.getRegClass(reg);
      Register NewReg = MRI.createVirtualRegister(RC);
      MO.setReg(NewReg);
      VRMap[CurStageNum][reg] = NewReg;
      if (LastDef)
        replaceRegUsesAfterLoop(reg, NewReg, BB, MRI, LIS);
    } else if (MO.isUse()) {
      MachineInstr *Def = MRI.getVRegDef(reg);
      // Compute the stage that contains the last definition for instruction.
      int DefStageNum = Schedule.getStage(Def);
      unsigned StageNum = CurStageNum;
      if (DefStageNum != -1 && (int)InstrStageNum > DefStageNum) {
        // Compute the difference in stages between the defintion and the use.
        unsigned StageDiff = (InstrStageNum - DefStageNum);
        // Make an adjustment to get the last definition.
        StageNum -= StageDiff;
      }
      if (VRMap[StageNum].count(reg))
        MO.setReg(VRMap[StageNum][reg]);
    }
  }
}

/// Return the instruction in the loop that defines the register.
/// If the definition is a Phi, then follow the Phi operand to
/// the instruction in the loop.
MachineInstr *ModuloScheduleExpander::findDefInLoop(unsigned Reg) {
  SmallPtrSet<MachineInstr *, 8> Visited;
  MachineInstr *Def = MRI.getVRegDef(Reg);
  while (Def->isPHI()) {
    if (!Visited.insert(Def).second)
      break;
    for (unsigned i = 1, e = Def->getNumOperands(); i < e; i += 2)
      if (Def->getOperand(i + 1).getMBB() == BB) {
        Def = MRI.getVRegDef(Def->getOperand(i).getReg());
        break;
      }
  }
  return Def;
}

/// Return the new name for the value from the previous stage.
unsigned ModuloScheduleExpander::getPrevMapVal(
    unsigned StageNum, unsigned PhiStage, unsigned LoopVal, unsigned LoopStage,
    ValueMapTy *VRMap, MachineBasicBlock *BB) {
  unsigned PrevVal = 0;
  if (StageNum > PhiStage) {
    MachineInstr *LoopInst = MRI.getVRegDef(LoopVal);
    if (PhiStage == LoopStage && VRMap[StageNum - 1].count(LoopVal))
      // The name is defined in the previous stage.
      PrevVal = VRMap[StageNum - 1][LoopVal];
    else if (VRMap[StageNum].count(LoopVal))
      // The previous name is defined in the current stage when the instruction
      // order is swapped.
      PrevVal = VRMap[StageNum][LoopVal];
    else if (!LoopInst->isPHI() || LoopInst->getParent() != BB)
      // The loop value hasn't yet been scheduled.
      PrevVal = LoopVal;
    else if (StageNum == PhiStage + 1)
      // The loop value is another phi, which has not been scheduled.
      PrevVal = getInitPhiReg(*LoopInst, BB);
    else if (StageNum > PhiStage + 1 && LoopInst->getParent() == BB)
      // The loop value is another phi, which has been scheduled.
      PrevVal =
          getPrevMapVal(StageNum - 1, PhiStage, getLoopPhiReg(*LoopInst, BB),
                        LoopStage, VRMap, BB);
  }
  return PrevVal;
}

/// Rewrite the Phi values in the specified block to use the mappings
/// from the initial operand. Once the Phi is scheduled, we switch
/// to using the loop value instead of the Phi value, so those names
/// do not need to be rewritten.
void ModuloScheduleExpander::rewritePhiValues(MachineBasicBlock *NewBB,
                                              unsigned StageNum,
                                              ValueMapTy *VRMap,
                                              InstrMapTy &InstrMap) {
  for (auto &PHI : BB->phis()) {
    unsigned InitVal = 0;
    unsigned LoopVal = 0;
    getPhiRegs(PHI, BB, InitVal, LoopVal);
    Register PhiDef = PHI.getOperand(0).getReg();

    unsigned PhiStage = (unsigned)Schedule.getStage(MRI.getVRegDef(PhiDef));
    unsigned LoopStage = (unsigned)Schedule.getStage(MRI.getVRegDef(LoopVal));
    unsigned NumPhis = getStagesForPhi(PhiDef);
    if (NumPhis > StageNum)
      NumPhis = StageNum;
    for (unsigned np = 0; np <= NumPhis; ++np) {
      unsigned NewVal =
          getPrevMapVal(StageNum - np, PhiStage, LoopVal, LoopStage, VRMap, BB);
      if (!NewVal)
        NewVal = InitVal;
      rewriteScheduledInstr(NewBB, InstrMap, StageNum - np, np, &PHI, PhiDef,
                            NewVal);
    }
  }
}

/// Rewrite a previously scheduled instruction to use the register value
/// from the new instruction. Make sure the instruction occurs in the
/// basic block, and we don't change the uses in the new instruction.
void ModuloScheduleExpander::rewriteScheduledInstr(
    MachineBasicBlock *BB, InstrMapTy &InstrMap, unsigned CurStageNum,
    unsigned PhiNum, MachineInstr *Phi, unsigned OldReg, unsigned NewReg,
    unsigned PrevReg) {
  bool InProlog = (CurStageNum < (unsigned)Schedule.getNumStages() - 1);
  int StagePhi = Schedule.getStage(Phi) + PhiNum;
  // Rewrite uses that have been scheduled already to use the new
  // Phi register.
  for (MachineOperand &UseOp :
       llvm::make_early_inc_range(MRI.use_operands(OldReg))) {
    MachineInstr *UseMI = UseOp.getParent();
    if (UseMI->getParent() != BB)
      continue;
    if (UseMI->isPHI()) {
      if (!Phi->isPHI() && UseMI->getOperand(0).getReg() == NewReg)
        continue;
      if (getLoopPhiReg(*UseMI, BB) != OldReg)
        continue;
    }
    InstrMapTy::iterator OrigInstr = InstrMap.find(UseMI);
    assert(OrigInstr != InstrMap.end() && "Instruction not scheduled.");
    MachineInstr *OrigMI = OrigInstr->second;
    int StageSched = Schedule.getStage(OrigMI);
    int CycleSched = Schedule.getCycle(OrigMI);
    unsigned ReplaceReg = 0;
    // This is the stage for the scheduled instruction.
    if (StagePhi == StageSched && Phi->isPHI()) {
      int CyclePhi = Schedule.getCycle(Phi);
      if (PrevReg && InProlog)
        ReplaceReg = PrevReg;
      else if (PrevReg && !isLoopCarried(*Phi) &&
               (CyclePhi <= CycleSched || OrigMI->isPHI()))
        ReplaceReg = PrevReg;
      else
        ReplaceReg = NewReg;
    }
    // The scheduled instruction occurs before the scheduled Phi, and the
    // Phi is not loop carried.
    if (!InProlog && StagePhi + 1 == StageSched && !isLoopCarried(*Phi))
      ReplaceReg = NewReg;
    if (StagePhi > StageSched && Phi->isPHI())
      ReplaceReg = NewReg;
    if (!InProlog && !Phi->isPHI() && StagePhi < StageSched)
      ReplaceReg = NewReg;
    if (ReplaceReg) {
      const TargetRegisterClass *NRC =
          MRI.constrainRegClass(ReplaceReg, MRI.getRegClass(OldReg));
      if (NRC)
        UseOp.setReg(ReplaceReg);
      else {
        Register SplitReg = MRI.createVirtualRegister(MRI.getRegClass(OldReg));
        BuildMI(*BB, UseMI, UseMI->getDebugLoc(), TII->get(TargetOpcode::COPY),
                SplitReg)
            .addReg(ReplaceReg);
        UseOp.setReg(SplitReg);
      }
    }
  }
}

bool ModuloScheduleExpander::isLoopCarried(MachineInstr &Phi) {
  if (!Phi.isPHI())
    return false;
  int DefCycle = Schedule.getCycle(&Phi);
  int DefStage = Schedule.getStage(&Phi);

  unsigned InitVal = 0;
  unsigned LoopVal = 0;
  getPhiRegs(Phi, Phi.getParent(), InitVal, LoopVal);
  MachineInstr *Use = MRI.getVRegDef(LoopVal);
  if (!Use || Use->isPHI())
    return true;
  int LoopCycle = Schedule.getCycle(Use);
  int LoopStage = Schedule.getStage(Use);
  return (LoopCycle > DefCycle) || (LoopStage <= DefStage);
}

//===----------------------------------------------------------------------===//
// PeelingModuloScheduleExpander implementation
//===----------------------------------------------------------------------===//
// This is a reimplementation of ModuloScheduleExpander that works by creating
// a fully correct steady-state kernel and peeling off the prolog and epilogs.
//===----------------------------------------------------------------------===//

namespace {
// Remove any dead phis in MBB. Dead phis either have only one block as input
// (in which case they are the identity) or have no uses.
void EliminateDeadPhis(MachineBasicBlock *MBB, MachineRegisterInfo &MRI,
                       LiveIntervals *LIS, bool KeepSingleSrcPhi = false) {
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB->phis())) {
      assert(MI.isPHI());
      if (MRI.use_empty(MI.getOperand(0).getReg())) {
        if (LIS)
          LIS->RemoveMachineInstrFromMaps(MI);
        MI.eraseFromParent();
        Changed = true;
      } else if (!KeepSingleSrcPhi && MI.getNumExplicitOperands() == 3) {
        const TargetRegisterClass *ConstrainRegClass =
            MRI.constrainRegClass(MI.getOperand(1).getReg(),
                                  MRI.getRegClass(MI.getOperand(0).getReg()));
        assert(ConstrainRegClass &&
               "Expected a valid constrained register class!");
        (void)ConstrainRegClass;
        MRI.replaceRegWith(MI.getOperand(0).getReg(),
                           MI.getOperand(1).getReg());
        if (LIS)
          LIS->RemoveMachineInstrFromMaps(MI);
        MI.eraseFromParent();
        Changed = true;
      }
    }
  }
}

/// Rewrites the kernel block in-place to adhere to the given schedule.
/// KernelRewriter holds all of the state required to perform the rewriting.
class KernelRewriter {
  ModuloSchedule &S;
  MachineBasicBlock *BB;
  MachineBasicBlock *PreheaderBB, *ExitBB;
  MachineRegisterInfo &MRI;
  const TargetInstrInfo *TII;
  LiveIntervals *LIS;

  // Map from register class to canonical undef register for that class.
  DenseMap<const TargetRegisterClass *, Register> Undefs;
  // Map from <LoopReg, InitReg> to phi register for all created phis. Note that
  // this map is only used when InitReg is non-undef.
  DenseMap<std::pair<unsigned, unsigned>, Register> Phis;
  // Map from LoopReg to phi register where the InitReg is undef.
  DenseMap<Register, Register> UndefPhis;

  // Reg is used by MI. Return the new register MI should use to adhere to the
  // schedule. Insert phis as necessary.
  Register remapUse(Register Reg, MachineInstr &MI);
  // Insert a phi that carries LoopReg from the loop body and InitReg otherwise.
  // If InitReg is not given it is chosen arbitrarily. It will either be undef
  // or will be chosen so as to share another phi.
  Register phi(Register LoopReg, std::optional<Register> InitReg = {},
               const TargetRegisterClass *RC = nullptr);
  // Create an undef register of the given register class.
  Register undef(const TargetRegisterClass *RC);

public:
  KernelRewriter(MachineLoop &L, ModuloSchedule &S, MachineBasicBlock *LoopBB,
                 LiveIntervals *LIS = nullptr);
  void rewrite();
};
} // namespace

KernelRewriter::KernelRewriter(MachineLoop &L, ModuloSchedule &S,
                               MachineBasicBlock *LoopBB, LiveIntervals *LIS)
    : S(S), BB(LoopBB), PreheaderBB(L.getLoopPreheader()),
      ExitBB(L.getExitBlock()), MRI(BB->getParent()->getRegInfo()),
      TII(BB->getParent()->getSubtarget().getInstrInfo()), LIS(LIS) {
  PreheaderBB = *BB->pred_begin();
  if (PreheaderBB == BB)
    PreheaderBB = *std::next(BB->pred_begin());
}

void KernelRewriter::rewrite() {
  // Rearrange the loop to be in schedule order. Note that the schedule may
  // contain instructions that are not owned by the loop block (InstrChanges and
  // friends), so we gracefully handle unowned instructions and delete any
  // instructions that weren't in the schedule.
  auto InsertPt = BB->getFirstTerminator();
  MachineInstr *FirstMI = nullptr;
  for (MachineInstr *MI : S.getInstructions()) {
    if (MI->isPHI())
      continue;
    if (MI->getParent())
      MI->removeFromParent();
    BB->insert(InsertPt, MI);
    if (!FirstMI)
      FirstMI = MI;
  }
  assert(FirstMI && "Failed to find first MI in schedule");

  // At this point all of the scheduled instructions are between FirstMI
  // and the end of the block. Kill from the first non-phi to FirstMI.
  for (auto I = BB->getFirstNonPHI(); I != FirstMI->getIterator();) {
    if (LIS)
      LIS->RemoveMachineInstrFromMaps(*I);
    (I++)->eraseFromParent();
  }

  // Now remap every instruction in the loop.
  for (MachineInstr &MI : *BB) {
    if (MI.isPHI() || MI.isTerminator())
      continue;
    for (MachineOperand &MO : MI.uses()) {
      if (!MO.isReg() || MO.getReg().isPhysical() || MO.isImplicit())
        continue;
      Register Reg = remapUse(MO.getReg(), MI);
      MO.setReg(Reg);
    }
  }
  EliminateDeadPhis(BB, MRI, LIS);

  // Ensure a phi exists for all instructions that are either referenced by
  // an illegal phi or by an instruction outside the loop. This allows us to
  // treat remaps of these values the same as "normal" values that come from
  // loop-carried phis.
  for (auto MI = BB->getFirstNonPHI(); MI != BB->end(); ++MI) {
    if (MI->isPHI()) {
      Register R = MI->getOperand(0).getReg();
      phi(R);
      continue;
    }

    for (MachineOperand &Def : MI->defs()) {
      for (MachineInstr &MI : MRI.use_instructions(Def.getReg())) {
        if (MI.getParent() != BB) {
          phi(Def.getReg());
          break;
        }
      }
    }
  }
}

Register KernelRewriter::remapUse(Register Reg, MachineInstr &MI) {
  MachineInstr *Producer = MRI.getUniqueVRegDef(Reg);
  if (!Producer)
    return Reg;

  int ConsumerStage = S.getStage(&MI);
  if (!Producer->isPHI()) {
    // Non-phi producers are simple to remap. Insert as many phis as the
    // difference between the consumer and producer stages.
    if (Producer->getParent() != BB)
      // Producer was not inside the loop. Use the register as-is.
      return Reg;
    int ProducerStage = S.getStage(Producer);
    assert(ConsumerStage != -1 &&
           "In-loop consumer should always be scheduled!");
    assert(ConsumerStage >= ProducerStage);
    unsigned StageDiff = ConsumerStage - ProducerStage;

    for (unsigned I = 0; I < StageDiff; ++I)
      Reg = phi(Reg);
    return Reg;
  }

  // First, dive through the phi chain to find the defaults for the generated
  // phis.
  SmallVector<std::optional<Register>, 4> Defaults;
  Register LoopReg = Reg;
  auto LoopProducer = Producer;
  while (LoopProducer->isPHI() && LoopProducer->getParent() == BB) {
    LoopReg = getLoopPhiReg(*LoopProducer, BB);
    Defaults.emplace_back(getInitPhiReg(*LoopProducer, BB));
    LoopProducer = MRI.getUniqueVRegDef(LoopReg);
    assert(LoopProducer);
  }
  int LoopProducerStage = S.getStage(LoopProducer);

  std::optional<Register> IllegalPhiDefault;

  if (LoopProducerStage == -1) {
    // Do nothing.
  } else if (LoopProducerStage > ConsumerStage) {
    // This schedule is only representable if ProducerStage == ConsumerStage+1.
    // In addition, Consumer's cycle must be scheduled after Producer in the
    // rescheduled loop. This is enforced by the pipeliner's ASAP and ALAP
    // functions.
#ifndef NDEBUG // Silence unused variables in non-asserts mode.
    int LoopProducerCycle = S.getCycle(LoopProducer);
    int ConsumerCycle = S.getCycle(&MI);
#endif
    assert(LoopProducerCycle <= ConsumerCycle);
    assert(LoopProducerStage == ConsumerStage + 1);
    // Peel off the first phi from Defaults and insert a phi between producer
    // and consumer. This phi will not be at the front of the block so we
    // consider it illegal. It will only exist during the rewrite process; it
    // needs to exist while we peel off prologs because these could take the
    // default value. After that we can replace all uses with the loop producer
    // value.
    IllegalPhiDefault = Defaults.front();
    Defaults.erase(Defaults.begin());
  } else {
    assert(ConsumerStage >= LoopProducerStage);
    int StageDiff = ConsumerStage - LoopProducerStage;
    if (StageDiff > 0) {
      LLVM_DEBUG(dbgs() << " -- padding defaults array from " << Defaults.size()
                        << " to " << (Defaults.size() + StageDiff) << "\n");
      // If we need more phis than we have defaults for, pad out with undefs for
      // the earliest phis, which are at the end of the defaults chain (the
      // chain is in reverse order).
      Defaults.resize(Defaults.size() + StageDiff,
                      Defaults.empty() ? std::optional<Register>()
                                       : Defaults.back());
    }
  }

  // Now we know the number of stages to jump back, insert the phi chain.
  auto DefaultI = Defaults.rbegin();
  while (DefaultI != Defaults.rend())
    LoopReg = phi(LoopReg, *DefaultI++, MRI.getRegClass(Reg));

  if (IllegalPhiDefault) {
    // The consumer optionally consumes LoopProducer in the same iteration
    // (because the producer is scheduled at an earlier cycle than the consumer)
    // or the initial value. To facilitate this we create an illegal block here
    // by embedding a phi in the middle of the block. We will fix this up
    // immediately prior to pruning.
    auto RC = MRI.getRegClass(Reg);
    Register R = MRI.createVirtualRegister(RC);
    MachineInstr *IllegalPhi =
        BuildMI(*BB, MI, DebugLoc(), TII->get(TargetOpcode::PHI), R)
            .addReg(*IllegalPhiDefault)
            .addMBB(PreheaderBB) // Block choice is arbitrary and has no effect.
            .addReg(LoopReg)
            .addMBB(BB); // Block choice is arbitrary and has no effect.
    // Illegal phi should belong to the producer stage so that it can be
    // filtered correctly during peeling.
    S.setStage(IllegalPhi, LoopProducerStage);
    return R;
  }

  return LoopReg;
}

Register KernelRewriter::phi(Register LoopReg, std::optional<Register> InitReg,
                             const TargetRegisterClass *RC) {
  // If the init register is not undef, try and find an existing phi.
  if (InitReg) {
    auto I = Phis.find({LoopReg, *InitReg});
    if (I != Phis.end())
      return I->second;
  } else {
    for (auto &KV : Phis) {
      if (KV.first.first == LoopReg)
        return KV.second;
    }
  }

  // InitReg is either undef or no existing phi takes InitReg as input. Try and
  // find a phi that takes undef as input.
  auto I = UndefPhis.find(LoopReg);
  if (I != UndefPhis.end()) {
    Register R = I->second;
    if (!InitReg)
      // Found a phi taking undef as input, and this input is undef so return
      // without any more changes.
      return R;
    // Found a phi taking undef as input, so rewrite it to take InitReg.
    MachineInstr *MI = MRI.getVRegDef(R);
    MI->getOperand(1).setReg(*InitReg);
    Phis.insert({{LoopReg, *InitReg}, R});
    const TargetRegisterClass *ConstrainRegClass =
        MRI.constrainRegClass(R, MRI.getRegClass(*InitReg));
    assert(ConstrainRegClass && "Expected a valid constrained register class!");
    (void)ConstrainRegClass;
    UndefPhis.erase(I);
    return R;
  }

  // Failed to find any existing phi to reuse, so create a new one.
  if (!RC)
    RC = MRI.getRegClass(LoopReg);
  Register R = MRI.createVirtualRegister(RC);
  if (InitReg) {
    const TargetRegisterClass *ConstrainRegClass =
        MRI.constrainRegClass(R, MRI.getRegClass(*InitReg));
    assert(ConstrainRegClass && "Expected a valid constrained register class!");
    (void)ConstrainRegClass;
  }
  BuildMI(*BB, BB->getFirstNonPHI(), DebugLoc(), TII->get(TargetOpcode::PHI), R)
      .addReg(InitReg ? *InitReg : undef(RC))
      .addMBB(PreheaderBB)
      .addReg(LoopReg)
      .addMBB(BB);
  if (!InitReg)
    UndefPhis[LoopReg] = R;
  else
    Phis[{LoopReg, *InitReg}] = R;
  return R;
}

Register KernelRewriter::undef(const TargetRegisterClass *RC) {
  Register &R = Undefs[RC];
  if (R == 0) {
    // Create an IMPLICIT_DEF that defines this register if we need it.
    // All uses of this should be removed by the time we have finished unrolling
    // prologs and epilogs.
    R = MRI.createVirtualRegister(RC);
    auto *InsertBB = &PreheaderBB->getParent()->front();
    BuildMI(*InsertBB, InsertBB->getFirstTerminator(), DebugLoc(),
            TII->get(TargetOpcode::IMPLICIT_DEF), R);
  }
  return R;
}

namespace {
/// Describes an operand in the kernel of a pipelined loop. Characteristics of
/// the operand are discovered, such as how many in-loop PHIs it has to jump
/// through and defaults for these phis.
class KernelOperandInfo {
  MachineBasicBlock *BB;
  MachineRegisterInfo &MRI;
  SmallVector<Register, 4> PhiDefaults;
  MachineOperand *Source;
  MachineOperand *Target;

public:
  KernelOperandInfo(MachineOperand *MO, MachineRegisterInfo &MRI,
                    const SmallPtrSetImpl<MachineInstr *> &IllegalPhis)
      : MRI(MRI) {
    Source = MO;
    BB = MO->getParent()->getParent();
    while (isRegInLoop(MO)) {
      MachineInstr *MI = MRI.getVRegDef(MO->getReg());
      if (MI->isFullCopy()) {
        MO = &MI->getOperand(1);
        continue;
      }
      if (!MI->isPHI())
        break;
      // If this is an illegal phi, don't count it in distance.
      if (IllegalPhis.count(MI)) {
        MO = &MI->getOperand(3);
        continue;
      }

      Register Default = getInitPhiReg(*MI, BB);
      MO = MI->getOperand(2).getMBB() == BB ? &MI->getOperand(1)
                                            : &MI->getOperand(3);
      PhiDefaults.push_back(Default);
    }
    Target = MO;
  }

  bool operator==(const KernelOperandInfo &Other) const {
    return PhiDefaults.size() == Other.PhiDefaults.size();
  }

  void print(raw_ostream &OS) const {
    OS << "use of " << *Source << ": distance(" << PhiDefaults.size() << ") in "
       << *Source->getParent();
  }

private:
  bool isRegInLoop(MachineOperand *MO) {
    return MO->isReg() && MO->getReg().isVirtual() &&
           MRI.getVRegDef(MO->getReg())->getParent() == BB;
  }
};
} // namespace

MachineBasicBlock *
PeelingModuloScheduleExpander::peelKernel(LoopPeelDirection LPD) {
  MachineBasicBlock *NewBB = PeelSingleBlockLoop(LPD, BB, MRI, TII);
  if (LPD == LPD_Front)
    PeeledFront.push_back(NewBB);
  else
    PeeledBack.push_front(NewBB);
  for (auto I = BB->begin(), NI = NewBB->begin(); !I->isTerminator();
       ++I, ++NI) {
    CanonicalMIs[&*I] = &*I;
    CanonicalMIs[&*NI] = &*I;
    BlockMIs[{NewBB, &*I}] = &*NI;
    BlockMIs[{BB, &*I}] = &*I;
  }
  return NewBB;
}

void PeelingModuloScheduleExpander::filterInstructions(MachineBasicBlock *MB,
                                                       int MinStage) {
  for (auto I = MB->getFirstInstrTerminator()->getReverseIterator();
       I != std::next(MB->getFirstNonPHI()->getReverseIterator());) {
    MachineInstr *MI = &*I++;
    int Stage = getStage(MI);
    if (Stage == -1 || Stage >= MinStage)
      continue;

    for (MachineOperand &DefMO : MI->defs()) {
      SmallVector<std::pair<MachineInstr *, Register>, 4> Subs;
      for (MachineInstr &UseMI : MRI.use_instructions(DefMO.getReg())) {
        // Only PHIs can use values from this block by construction.
        // Match with the equivalent PHI in B.
        assert(UseMI.isPHI());
        Register Reg = getEquivalentRegisterIn(UseMI.getOperand(0).getReg(),
                                               MI->getParent());
        Subs.emplace_back(&UseMI, Reg);
      }
      for (auto &Sub : Subs)
        Sub.first->substituteRegister(DefMO.getReg(), Sub.second, /*SubIdx=*/0,
                                      *MRI.getTargetRegisterInfo());
    }
    if (LIS)
      LIS->RemoveMachineInstrFromMaps(*MI);
    MI->eraseFromParent();
  }
}

void PeelingModuloScheduleExpander::moveStageBetweenBlocks(
    MachineBasicBlock *DestBB, MachineBasicBlock *SourceBB, unsigned Stage) {
  auto InsertPt = DestBB->getFirstNonPHI();
  DenseMap<Register, Register> Remaps;
  for (MachineInstr &MI : llvm::make_early_inc_range(
           llvm::make_range(SourceBB->getFirstNonPHI(), SourceBB->end()))) {
    if (MI.isPHI()) {
      // This is an illegal PHI. If we move any instructions using an illegal
      // PHI, we need to create a legal Phi.
      if (getStage(&MI) != Stage) {
        // The legal Phi is not necessary if the illegal phi's stage
        // is being moved.
        Register PhiR = MI.getOperand(0).getReg();
        auto RC = MRI.getRegClass(PhiR);
        Register NR = MRI.createVirtualRegister(RC);
        MachineInstr *NI = BuildMI(*DestBB, DestBB->getFirstNonPHI(),
                                   DebugLoc(), TII->get(TargetOpcode::PHI), NR)
                               .addReg(PhiR)
                               .addMBB(SourceBB);
        BlockMIs[{DestBB, CanonicalMIs[&MI]}] = NI;
        CanonicalMIs[NI] = CanonicalMIs[&MI];
        Remaps[PhiR] = NR;
      }
    }
    if (getStage(&MI) != Stage)
      continue;
    MI.removeFromParent();
    DestBB->insert(InsertPt, &MI);
    auto *KernelMI = CanonicalMIs[&MI];
    BlockMIs[{DestBB, KernelMI}] = &MI;
    BlockMIs.erase({SourceBB, KernelMI});
  }
  SmallVector<MachineInstr *, 4> PhiToDelete;
  for (MachineInstr &MI : DestBB->phis()) {
    assert(MI.getNumOperands() == 3);
    MachineInstr *Def = MRI.getVRegDef(MI.getOperand(1).getReg());
    // If the instruction referenced by the phi is moved inside the block
    // we don't need the phi anymore.
    if (getStage(Def) == Stage) {
      Register PhiReg = MI.getOperand(0).getReg();
      assert(Def->findRegisterDefOperandIdx(MI.getOperand(1).getReg(),
                                            /*TRI=*/nullptr) != -1);
      MRI.replaceRegWith(MI.getOperand(0).getReg(), MI.getOperand(1).getReg());
      MI.getOperand(0).setReg(PhiReg);
      PhiToDelete.push_back(&MI);
    }
  }
  for (auto *P : PhiToDelete)
    P->eraseFromParent();
  InsertPt = DestBB->getFirstNonPHI();
  // Helper to clone Phi instructions into the destination block. We clone Phi
  // greedily to avoid combinatorial explosion of Phi instructions.
  auto clonePhi = [&](MachineInstr *Phi) {
    MachineInstr *NewMI = MF.CloneMachineInstr(Phi);
    DestBB->insert(InsertPt, NewMI);
    Register OrigR = Phi->getOperand(0).getReg();
    Register R = MRI.createVirtualRegister(MRI.getRegClass(OrigR));
    NewMI->getOperand(0).setReg(R);
    NewMI->getOperand(1).setReg(OrigR);
    NewMI->getOperand(2).setMBB(*DestBB->pred_begin());
    Remaps[OrigR] = R;
    CanonicalMIs[NewMI] = CanonicalMIs[Phi];
    BlockMIs[{DestBB, CanonicalMIs[Phi]}] = NewMI;
    PhiNodeLoopIteration[NewMI] = PhiNodeLoopIteration[Phi];
    return R;
  };
  for (auto I = DestBB->getFirstNonPHI(); I != DestBB->end(); ++I) {
    for (MachineOperand &MO : I->uses()) {
      if (!MO.isReg())
        continue;
      if (Remaps.count(MO.getReg()))
        MO.setReg(Remaps[MO.getReg()]);
      else {
        // If we are using a phi from the source block we need to add a new phi
        // pointing to the old one.
        MachineInstr *Use = MRI.getUniqueVRegDef(MO.getReg());
        if (Use && Use->isPHI() && Use->getParent() == SourceBB) {
          Register R = clonePhi(Use);
          MO.setReg(R);
        }
      }
    }
  }
}

Register
PeelingModuloScheduleExpander::getPhiCanonicalReg(MachineInstr *CanonicalPhi,
                                                  MachineInstr *Phi) {
  unsigned distance = PhiNodeLoopIteration[Phi];
  MachineInstr *CanonicalUse = CanonicalPhi;
  Register CanonicalUseReg = CanonicalUse->getOperand(0).getReg();
  for (unsigned I = 0; I < distance; ++I) {
    assert(CanonicalUse->isPHI());
    assert(CanonicalUse->getNumOperands() == 5);
    unsigned LoopRegIdx = 3, InitRegIdx = 1;
    if (CanonicalUse->getOperand(2).getMBB() == CanonicalUse->getParent())
      std::swap(LoopRegIdx, InitRegIdx);
    CanonicalUseReg = CanonicalUse->getOperand(LoopRegIdx).getReg();
    CanonicalUse = MRI.getVRegDef(CanonicalUseReg);
  }
  return CanonicalUseReg;
}

void PeelingModuloScheduleExpander::peelPrologAndEpilogs() {
  BitVector LS(Schedule.getNumStages(), true);
  BitVector AS(Schedule.getNumStages(), true);
  LiveStages[BB] = LS;
  AvailableStages[BB] = AS;

  // Peel out the prologs.
  LS.reset();
  for (int I = 0; I < Schedule.getNumStages() - 1; ++I) {
    LS[I] = true;
    Prologs.push_back(peelKernel(LPD_Front));
    LiveStages[Prologs.back()] = LS;
    AvailableStages[Prologs.back()] = LS;
  }

  // Create a block that will end up as the new loop exiting block (dominated by
  // all prologs and epilogs). It will only contain PHIs, in the same order as
  // BB's PHIs. This gives us a poor-man's LCSSA with the inductive property
  // that the exiting block is a (sub) clone of BB. This in turn gives us the
  // property that any value deffed in BB but used outside of BB is used by a
  // PHI in the exiting block.
  MachineBasicBlock *ExitingBB = CreateLCSSAExitingBlock();
  EliminateDeadPhis(ExitingBB, MRI, LIS, /*KeepSingleSrcPhi=*/true);
  // Push out the epilogs, again in reverse order.
  // We can't assume anything about the minumum loop trip count at this point,
  // so emit a fairly complex epilog.

  // We first peel number of stages minus one epilogue. Then we remove dead
  // stages and reorder instructions based on their stage. If we have 3 stages
  // we generate first:
  // E0[3, 2, 1]
  // E1[3', 2']
  // E2[3'']
  // And then we move instructions based on their stages to have:
  // E0[3]
  // E1[2, 3']
  // E2[1, 2', 3'']
  // The transformation is legal because we only move instructions past
  // instructions of a previous loop iteration.
  for (int I = 1; I <= Schedule.getNumStages() - 1; ++I) {
    Epilogs.push_back(peelKernel(LPD_Back));
    MachineBasicBlock *B = Epilogs.back();
    filterInstructions(B, Schedule.getNumStages() - I);
    // Keep track at which iteration each phi belongs to. We need it to know
    // what version of the variable to use during prologue/epilogue stitching.
    EliminateDeadPhis(B, MRI, LIS, /*KeepSingleSrcPhi=*/true);
    for (MachineInstr &Phi : B->phis())
      PhiNodeLoopIteration[&Phi] = Schedule.getNumStages() - I;
  }
  for (size_t I = 0; I < Epilogs.size(); I++) {
    LS.reset();
    for (size_t J = I; J < Epilogs.size(); J++) {
      int Iteration = J;
      unsigned Stage = Schedule.getNumStages() - 1 + I - J;
      // Move stage one block at a time so that Phi nodes are updated correctly.
      for (size_t K = Iteration; K > I; K--)
        moveStageBetweenBlocks(Epilogs[K - 1], Epilogs[K], Stage);
      LS[Stage] = true;
    }
    LiveStages[Epilogs[I]] = LS;
    AvailableStages[Epilogs[I]] = AS;
  }

  // Now we've defined all the prolog and epilog blocks as a fallthrough
  // sequence, add the edges that will be followed if the loop trip count is
  // lower than the number of stages (connecting prologs directly with epilogs).
  auto PI = Prologs.begin();
  auto EI = Epilogs.begin();
  assert(Prologs.size() == Epilogs.size());
  for (; PI != Prologs.end(); ++PI, ++EI) {
    MachineBasicBlock *Pred = *(*EI)->pred_begin();
    (*PI)->addSuccessor(*EI);
    for (MachineInstr &MI : (*EI)->phis()) {
      Register Reg = MI.getOperand(1).getReg();
      MachineInstr *Use = MRI.getUniqueVRegDef(Reg);
      if (Use && Use->getParent() == Pred) {
        MachineInstr *CanonicalUse = CanonicalMIs[Use];
        if (CanonicalUse->isPHI()) {
          // If the use comes from a phi we need to skip as many phi as the
          // distance between the epilogue and the kernel. Trace through the phi
          // chain to find the right value.
          Reg = getPhiCanonicalReg(CanonicalUse, Use);
        }
        Reg = getEquivalentRegisterIn(Reg, *PI);
      }
      MI.addOperand(MachineOperand::CreateReg(Reg, /*isDef=*/false));
      MI.addOperand(MachineOperand::CreateMBB(*PI));
    }
  }

  // Create a list of all blocks in order.
  SmallVector<MachineBasicBlock *, 8> Blocks;
  llvm::copy(PeeledFront, std::back_inserter(Blocks));
  Blocks.push_back(BB);
  llvm::copy(PeeledBack, std::back_inserter(Blocks));

  // Iterate in reverse order over all instructions, remapping as we go.
  for (MachineBasicBlock *B : reverse(Blocks)) {
    for (auto I = B->instr_rbegin();
         I != std::next(B->getFirstNonPHI()->getReverseIterator());) {
      MachineBasicBlock::reverse_instr_iterator MI = I++;
      rewriteUsesOf(&*MI);
    }
  }
  for (auto *MI : IllegalPhisToDelete) {
    if (LIS)
      LIS->RemoveMachineInstrFromMaps(*MI);
    MI->eraseFromParent();
  }
  IllegalPhisToDelete.clear();

  // Now all remapping has been done, we're free to optimize the generated code.
  for (MachineBasicBlock *B : reverse(Blocks))
    EliminateDeadPhis(B, MRI, LIS);
  EliminateDeadPhis(ExitingBB, MRI, LIS);
}

MachineBasicBlock *PeelingModuloScheduleExpander::CreateLCSSAExitingBlock() {
  MachineFunction &MF = *BB->getParent();
  MachineBasicBlock *Exit = *BB->succ_begin();
  if (Exit == BB)
    Exit = *std::next(BB->succ_begin());

  MachineBasicBlock *NewBB = MF.CreateMachineBasicBlock(BB->getBasicBlock());
  MF.insert(std::next(BB->getIterator()), NewBB);

  // Clone all phis in BB into NewBB and rewrite.
  for (MachineInstr &MI : BB->phis()) {
    auto RC = MRI.getRegClass(MI.getOperand(0).getReg());
    Register OldR = MI.getOperand(3).getReg();
    Register R = MRI.createVirtualRegister(RC);
    SmallVector<MachineInstr *, 4> Uses;
    for (MachineInstr &Use : MRI.use_instructions(OldR))
      if (Use.getParent() != BB)
        Uses.push_back(&Use);
    for (MachineInstr *Use : Uses)
      Use->substituteRegister(OldR, R, /*SubIdx=*/0,
                              *MRI.getTargetRegisterInfo());
    MachineInstr *NI = BuildMI(NewBB, DebugLoc(), TII->get(TargetOpcode::PHI), R)
        .addReg(OldR)
        .addMBB(BB);
    BlockMIs[{NewBB, &MI}] = NI;
    CanonicalMIs[NI] = &MI;
  }
  BB->replaceSuccessor(Exit, NewBB);
  Exit->replacePhiUsesWith(BB, NewBB);
  NewBB->addSuccessor(Exit);

  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  bool CanAnalyzeBr = !TII->analyzeBranch(*BB, TBB, FBB, Cond);
  (void)CanAnalyzeBr;
  assert(CanAnalyzeBr && "Must be able to analyze the loop branch!");
  TII->removeBranch(*BB);
  TII->insertBranch(*BB, TBB == Exit ? NewBB : TBB, FBB == Exit ? NewBB : FBB,
                    Cond, DebugLoc());
  TII->insertUnconditionalBranch(*NewBB, Exit, DebugLoc());
  return NewBB;
}

Register
PeelingModuloScheduleExpander::getEquivalentRegisterIn(Register Reg,
                                                       MachineBasicBlock *BB) {
  MachineInstr *MI = MRI.getUniqueVRegDef(Reg);
  unsigned OpIdx = MI->findRegisterDefOperandIdx(Reg, /*TRI=*/nullptr);
  return BlockMIs[{BB, CanonicalMIs[MI]}]->getOperand(OpIdx).getReg();
}

void PeelingModuloScheduleExpander::rewriteUsesOf(MachineInstr *MI) {
  if (MI->isPHI()) {
    // This is an illegal PHI. The loop-carried (desired) value is operand 3,
    // and it is produced by this block.
    Register PhiR = MI->getOperand(0).getReg();
    Register R = MI->getOperand(3).getReg();
    int RMIStage = getStage(MRI.getUniqueVRegDef(R));
    if (RMIStage != -1 && !AvailableStages[MI->getParent()].test(RMIStage))
      R = MI->getOperand(1).getReg();
    MRI.setRegClass(R, MRI.getRegClass(PhiR));
    MRI.replaceRegWith(PhiR, R);
    // Postpone deleting the Phi as it may be referenced by BlockMIs and used
    // later to figure out how to remap registers.
    MI->getOperand(0).setReg(PhiR);
    IllegalPhisToDelete.push_back(MI);
    return;
  }

  int Stage = getStage(MI);
  if (Stage == -1 || LiveStages.count(MI->getParent()) == 0 ||
      LiveStages[MI->getParent()].test(Stage))
    // Instruction is live, no rewriting to do.
    return;

  for (MachineOperand &DefMO : MI->defs()) {
    SmallVector<std::pair<MachineInstr *, Register>, 4> Subs;
    for (MachineInstr &UseMI : MRI.use_instructions(DefMO.getReg())) {
      // Only PHIs can use values from this block by construction.
      // Match with the equivalent PHI in B.
      assert(UseMI.isPHI());
      Register Reg = getEquivalentRegisterIn(UseMI.getOperand(0).getReg(),
                                             MI->getParent());
      Subs.emplace_back(&UseMI, Reg);
    }
    for (auto &Sub : Subs)
      Sub.first->substituteRegister(DefMO.getReg(), Sub.second, /*SubIdx=*/0,
                                    *MRI.getTargetRegisterInfo());
  }
  if (LIS)
    LIS->RemoveMachineInstrFromMaps(*MI);
  MI->eraseFromParent();
}

void PeelingModuloScheduleExpander::fixupBranches() {
  // Work outwards from the kernel.
  bool KernelDisposed = false;
  int TC = Schedule.getNumStages() - 1;
  for (auto PI = Prologs.rbegin(), EI = Epilogs.rbegin(); PI != Prologs.rend();
       ++PI, ++EI, --TC) {
    MachineBasicBlock *Prolog = *PI;
    MachineBasicBlock *Fallthrough = *Prolog->succ_begin();
    MachineBasicBlock *Epilog = *EI;
    SmallVector<MachineOperand, 4> Cond;
    TII->removeBranch(*Prolog);
    std::optional<bool> StaticallyGreater =
        LoopInfo->createTripCountGreaterCondition(TC, *Prolog, Cond);
    if (!StaticallyGreater) {
      LLVM_DEBUG(dbgs() << "Dynamic: TC > " << TC << "\n");
      // Dynamically branch based on Cond.
      TII->insertBranch(*Prolog, Epilog, Fallthrough, Cond, DebugLoc());
    } else if (*StaticallyGreater == false) {
      LLVM_DEBUG(dbgs() << "Static-false: TC > " << TC << "\n");
      // Prolog never falls through; branch to epilog and orphan interior
      // blocks. Leave it to unreachable-block-elim to clean up.
      Prolog->removeSuccessor(Fallthrough);
      for (MachineInstr &P : Fallthrough->phis()) {
        P.removeOperand(2);
        P.removeOperand(1);
      }
      TII->insertUnconditionalBranch(*Prolog, Epilog, DebugLoc());
      KernelDisposed = true;
    } else {
      LLVM_DEBUG(dbgs() << "Static-true: TC > " << TC << "\n");
      // Prolog always falls through; remove incoming values in epilog.
      Prolog->removeSuccessor(Epilog);
      for (MachineInstr &P : Epilog->phis()) {
        P.removeOperand(4);
        P.removeOperand(3);
      }
    }
  }

  if (!KernelDisposed) {
    LoopInfo->adjustTripCount(-(Schedule.getNumStages() - 1));
    LoopInfo->setPreheader(Prologs.back());
  } else {
    LoopInfo->disposed();
  }
}

void PeelingModuloScheduleExpander::rewriteKernel() {
  KernelRewriter KR(*Schedule.getLoop(), Schedule, BB);
  KR.rewrite();
}

void PeelingModuloScheduleExpander::expand() {
  BB = Schedule.getLoop()->getTopBlock();
  Preheader = Schedule.getLoop()->getLoopPreheader();
  LLVM_DEBUG(Schedule.dump());
  LoopInfo = TII->analyzeLoopForPipelining(BB);
  assert(LoopInfo);

  rewriteKernel();
  peelPrologAndEpilogs();
  fixupBranches();
}

void PeelingModuloScheduleExpander::validateAgainstModuloScheduleExpander() {
  BB = Schedule.getLoop()->getTopBlock();
  Preheader = Schedule.getLoop()->getLoopPreheader();

  // Dump the schedule before we invalidate and remap all its instructions.
  // Stash it in a string so we can print it if we found an error.
  std::string ScheduleDump;
  raw_string_ostream OS(ScheduleDump);
  Schedule.print(OS);
  OS.flush();

  // First, run the normal ModuleScheduleExpander. We don't support any
  // InstrChanges.
  assert(LIS && "Requires LiveIntervals!");
  ModuloScheduleExpander MSE(MF, Schedule, *LIS,
                             ModuloScheduleExpander::InstrChangesTy());
  MSE.expand();
  MachineBasicBlock *ExpandedKernel = MSE.getRewrittenKernel();
  if (!ExpandedKernel) {
    // The expander optimized away the kernel. We can't do any useful checking.
    MSE.cleanup();
    return;
  }
  // Before running the KernelRewriter, re-add BB into the CFG.
  Preheader->addSuccessor(BB);

  // Now run the new expansion algorithm.
  KernelRewriter KR(*Schedule.getLoop(), Schedule, BB);
  KR.rewrite();
  peelPrologAndEpilogs();

  // Collect all illegal phis that the new algorithm created. We'll give these
  // to KernelOperandInfo.
  SmallPtrSet<MachineInstr *, 4> IllegalPhis;
  for (auto NI = BB->getFirstNonPHI(); NI != BB->end(); ++NI) {
    if (NI->isPHI())
      IllegalPhis.insert(&*NI);
  }

  // Co-iterate across both kernels. We expect them to be identical apart from
  // phis and full COPYs (we look through both).
  SmallVector<std::pair<KernelOperandInfo, KernelOperandInfo>, 8> KOIs;
  auto OI = ExpandedKernel->begin();
  auto NI = BB->begin();
  for (; !OI->isTerminator() && !NI->isTerminator(); ++OI, ++NI) {
    while (OI->isPHI() || OI->isFullCopy())
      ++OI;
    while (NI->isPHI() || NI->isFullCopy())
      ++NI;
    assert(OI->getOpcode() == NI->getOpcode() && "Opcodes don't match?!");
    // Analyze every operand separately.
    for (auto OOpI = OI->operands_begin(), NOpI = NI->operands_begin();
         OOpI != OI->operands_end(); ++OOpI, ++NOpI)
      KOIs.emplace_back(KernelOperandInfo(&*OOpI, MRI, IllegalPhis),
                        KernelOperandInfo(&*NOpI, MRI, IllegalPhis));
  }

  bool Failed = false;
  for (auto &OldAndNew : KOIs) {
    if (OldAndNew.first == OldAndNew.second)
      continue;
    Failed = true;
    errs() << "Modulo kernel validation error: [\n";
    errs() << " [golden] ";
    OldAndNew.first.print(errs());
    errs() << "          ";
    OldAndNew.second.print(errs());
    errs() << "]\n";
  }

  if (Failed) {
    errs() << "Golden reference kernel:\n";
    ExpandedKernel->print(errs());
    errs() << "New kernel:\n";
    BB->print(errs());
    errs() << ScheduleDump;
    report_fatal_error(
        "Modulo kernel validation (-pipeliner-experimental-cg) failed");
  }

  // Cleanup by removing BB from the CFG again as the original
  // ModuloScheduleExpander intended.
  Preheader->removeSuccessor(BB);
  MSE.cleanup();
}

MachineInstr *ModuloScheduleExpanderMVE::cloneInstr(MachineInstr *OldMI) {
  MachineInstr *NewMI = MF.CloneMachineInstr(OldMI);

  // TODO: Offset information needs to be corrected.
  NewMI->dropMemRefs(MF);

  return NewMI;
}

/// Create a dedicated exit for Loop. Exit is the original exit for Loop.
/// If it is already dedicated exit, return it. Otherwise, insert a new
/// block between them and return the new block.
static MachineBasicBlock *createDedicatedExit(MachineBasicBlock *Loop,
                                              MachineBasicBlock *Exit) {
  if (Exit->pred_size() == 1)
    return Exit;

  MachineFunction *MF = Loop->getParent();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

  MachineBasicBlock *NewExit =
      MF->CreateMachineBasicBlock(Loop->getBasicBlock());
  MF->insert(Loop->getIterator(), NewExit);

  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  TII->analyzeBranch(*Loop, TBB, FBB, Cond);
  if (TBB == Loop)
    FBB = NewExit;
  else if (FBB == Loop)
    TBB = NewExit;
  else
    llvm_unreachable("unexpected loop structure");
  TII->removeBranch(*Loop);
  TII->insertBranch(*Loop, TBB, FBB, Cond, DebugLoc());
  Loop->replaceSuccessor(Exit, NewExit);
  TII->insertUnconditionalBranch(*NewExit, Exit, DebugLoc());
  NewExit->addSuccessor(Exit);

  Exit->replacePhiUsesWith(Loop, NewExit);

  return NewExit;
}

/// Insert branch code into the end of MBB. It branches to GreaterThan if the
/// remaining trip count for instructions in LastStage0Insts is greater than
/// RequiredTC, and to Otherwise otherwise.
void ModuloScheduleExpanderMVE::insertCondBranch(MachineBasicBlock &MBB,
                                                 int RequiredTC,
                                                 InstrMapTy &LastStage0Insts,
                                                 MachineBasicBlock &GreaterThan,
                                                 MachineBasicBlock &Otherwise) {
  SmallVector<MachineOperand, 4> Cond;
  LoopInfo->createRemainingIterationsGreaterCondition(RequiredTC, MBB, Cond,
                                                      LastStage0Insts);

  if (SwapBranchTargetsMVE) {
    // Set SwapBranchTargetsMVE to true if a target prefers to replace TBB and
    // FBB for optimal performance.
    if (TII->reverseBranchCondition(Cond))
      llvm_unreachable("can not reverse branch condition");
    TII->insertBranch(MBB, &Otherwise, &GreaterThan, Cond, DebugLoc());
  } else {
    TII->insertBranch(MBB, &GreaterThan, &Otherwise, Cond, DebugLoc());
  }
}

/// Generate a pipelined loop that is unrolled by using MVE algorithm and any
/// other necessary blocks. The control flow is modified to execute the
/// pipelined loop if the trip count satisfies the condition, otherwise the
/// original loop. The original loop is also used to execute the remainder
/// iterations which occur due to unrolling.
void ModuloScheduleExpanderMVE::generatePipelinedLoop() {
  // The control flow for pipelining with MVE:
  //
  // OrigPreheader:
  //   // The block that is originally the loop preheader
  //   goto Check
  //
  // Check:
  //   // Check whether the trip count satisfies the requirements to pipeline.
  //   if (LoopCounter > NumStages + NumUnroll - 2)
  //     // The minimum number of iterations to pipeline =
  //     //   iterations executed in prolog/epilog (NumStages-1) +
  //     //   iterations executed in one kernel run (NumUnroll)
  //     goto Prolog
  //   // fallback to the original loop
  //   goto NewPreheader
  //
  // Prolog:
  //   // All prolog stages. There are no direct branches to the epilogue.
  //   goto NewKernel
  //
  // NewKernel:
  //   // NumUnroll copies of the kernel
  //   if (LoopCounter > MVE-1)
  //     goto NewKernel
  //   goto Epilog
  //
  // Epilog:
  //   // All epilog stages.
  //   if (LoopCounter > 0)
  //     // The remainder is executed in the original loop
  //     goto NewPreheader
  //   goto NewExit
  //
  // NewPreheader:
  //   // Newly created preheader for the original loop.
  //   // The initial values of the phis in the loop are merged from two paths.
  //   NewInitVal = Phi OrigInitVal, Check, PipelineLastVal, Epilog
  //   goto OrigKernel
  //
  // OrigKernel:
  //   // The original loop block.
  //   if (LoopCounter != 0)
  //     goto OrigKernel
  //   goto NewExit
  //
  // NewExit:
  //   // Newly created dedicated exit for the original loop.
  //   // Merge values which are referenced after the loop
  //   Merged = Phi OrigVal, OrigKernel, PipelineVal, Epilog
  //   goto OrigExit
  //
  // OrigExit:
  //   // The block that is originally the loop exit.
  //   // If it is already deicated exit, NewExit is not created.

  // An example of where each stage is executed:
  // Assume #Stages 3, #MVE 4, #Iterations 12
  // Iter   0 1 2 3 4 5 6 7 8 9 10-11
  // -------------------------------------------------
  // Stage  0                          Prolog#0
  // Stage  1 0                        Prolog#1
  // Stage  2 1 0                      Kernel Unroll#0 Iter#0
  // Stage    2 1 0                    Kernel Unroll#1 Iter#0
  // Stage      2 1 0                  Kernel Unroll#2 Iter#0
  // Stage        2 1 0                Kernel Unroll#3 Iter#0
  // Stage          2 1 0              Kernel Unroll#0 Iter#1
  // Stage            2 1 0            Kernel Unroll#1 Iter#1
  // Stage              2 1 0          Kernel Unroll#2 Iter#1
  // Stage                2 1 0        Kernel Unroll#3 Iter#1
  // Stage                  2 1        Epilog#0
  // Stage                    2        Epilog#1
  // Stage                      0-2    OrigKernel

  LoopInfo = TII->analyzeLoopForPipelining(OrigKernel);
  assert(LoopInfo && "Must be able to analyze loop!");

  calcNumUnroll();

  Check = MF.CreateMachineBasicBlock(OrigKernel->getBasicBlock());
  Prolog = MF.CreateMachineBasicBlock(OrigKernel->getBasicBlock());
  NewKernel = MF.CreateMachineBasicBlock(OrigKernel->getBasicBlock());
  Epilog = MF.CreateMachineBasicBlock(OrigKernel->getBasicBlock());
  NewPreheader = MF.CreateMachineBasicBlock(OrigKernel->getBasicBlock());

  MF.insert(OrigKernel->getIterator(), Check);
  MF.insert(OrigKernel->getIterator(), Prolog);
  MF.insert(OrigKernel->getIterator(), NewKernel);
  MF.insert(OrigKernel->getIterator(), Epilog);
  MF.insert(OrigKernel->getIterator(), NewPreheader);

  NewExit = createDedicatedExit(OrigKernel, OrigExit);

  NewPreheader->transferSuccessorsAndUpdatePHIs(OrigPreheader);
  TII->insertUnconditionalBranch(*NewPreheader, OrigKernel, DebugLoc());

  OrigPreheader->addSuccessor(Check);
  TII->removeBranch(*OrigPreheader);
  TII->insertUnconditionalBranch(*OrigPreheader, Check, DebugLoc());

  Check->addSuccessor(Prolog);
  Check->addSuccessor(NewPreheader);

  Prolog->addSuccessor(NewKernel);

  NewKernel->addSuccessor(NewKernel);
  NewKernel->addSuccessor(Epilog);

  Epilog->addSuccessor(NewPreheader);
  Epilog->addSuccessor(NewExit);

  InstrMapTy LastStage0Insts;
  insertCondBranch(*Check, Schedule.getNumStages() + NumUnroll - 2,
                   LastStage0Insts, *Prolog, *NewPreheader);

  // VRMaps map (prolog/kernel/epilog phase#, original register#) to new
  // register#
  SmallVector<ValueMapTy> PrologVRMap, KernelVRMap, EpilogVRMap;
  generateProlog(PrologVRMap);
  generateKernel(PrologVRMap, KernelVRMap, LastStage0Insts);
  generateEpilog(KernelVRMap, EpilogVRMap, LastStage0Insts);
}

/// Replace MI's use operands according to the maps.
void ModuloScheduleExpanderMVE::updateInstrUse(
    MachineInstr *MI, int StageNum, int PhaseNum,
    SmallVectorImpl<ValueMapTy> &CurVRMap,
    SmallVectorImpl<ValueMapTy> *PrevVRMap) {
  // If MI is in the prolog/kernel/epilog block, CurVRMap is
  // PrologVRMap/KernelVRMap/EpilogVRMap respectively.
  // PrevVRMap is nullptr/PhiVRMap/KernelVRMap respectively.
  // Refer to the appropriate map according to the stage difference between
  // MI and the definition of an operand.

  for (MachineOperand &UseMO : MI->uses()) {
    if (!UseMO.isReg() || !UseMO.getReg().isVirtual())
      continue;
    int DiffStage = 0;
    Register OrigReg = UseMO.getReg();
    MachineInstr *DefInst = MRI.getVRegDef(OrigReg);
    if (!DefInst || DefInst->getParent() != OrigKernel)
      continue;
    unsigned InitReg = 0;
    unsigned DefReg = OrigReg;
    if (DefInst->isPHI()) {
      ++DiffStage;
      unsigned LoopReg;
      getPhiRegs(*DefInst, OrigKernel, InitReg, LoopReg);
      // LoopReg is guaranteed to be defined within the loop by canApply()
      DefReg = LoopReg;
      DefInst = MRI.getVRegDef(LoopReg);
    }
    unsigned DefStageNum = Schedule.getStage(DefInst);
    DiffStage += StageNum - DefStageNum;
    Register NewReg;
    if (PhaseNum >= DiffStage && CurVRMap[PhaseNum - DiffStage].count(DefReg))
      // NewReg is defined in a previous phase of the same block
      NewReg = CurVRMap[PhaseNum - DiffStage][DefReg];
    else if (!PrevVRMap)
      // Since this is the first iteration, refer the initial register of the
      // loop
      NewReg = InitReg;
    else
      // Cases where DiffStage is larger than PhaseNum.
      // If MI is in the kernel block, the value is defined by the previous
      // iteration and PhiVRMap is referenced. If MI is in the epilog block, the
      // value is defined in the kernel block and KernelVRMap is referenced.
      NewReg = (*PrevVRMap)[PrevVRMap->size() - (DiffStage - PhaseNum)][DefReg];

    const TargetRegisterClass *NRC =
        MRI.constrainRegClass(NewReg, MRI.getRegClass(OrigReg));
    if (NRC)
      UseMO.setReg(NewReg);
    else {
      Register SplitReg = MRI.createVirtualRegister(MRI.getRegClass(OrigReg));
      BuildMI(*OrigKernel, MI, MI->getDebugLoc(), TII->get(TargetOpcode::COPY),
              SplitReg)
          .addReg(NewReg);
      UseMO.setReg(SplitReg);
    }
  }
}

/// Return a phi if Reg is referenced by the phi.
/// canApply() guarantees that at most only one such phi exists.
static MachineInstr *getLoopPhiUser(Register Reg, MachineBasicBlock *Loop) {
  for (MachineInstr &Phi : Loop->phis()) {
    unsigned InitVal, LoopVal;
    getPhiRegs(Phi, Loop, InitVal, LoopVal);
    if (LoopVal == Reg)
      return &Phi;
  }
  return nullptr;
}

/// Generate phis for registers defined by OrigMI.
void ModuloScheduleExpanderMVE::generatePhi(
    MachineInstr *OrigMI, int UnrollNum,
    SmallVectorImpl<ValueMapTy> &PrologVRMap,
    SmallVectorImpl<ValueMapTy> &KernelVRMap,
    SmallVectorImpl<ValueMapTy> &PhiVRMap) {
  int StageNum = Schedule.getStage(OrigMI);
  bool UsePrologReg;
  if (Schedule.getNumStages() - NumUnroll + UnrollNum - 1 >= StageNum)
    UsePrologReg = true;
  else if (Schedule.getNumStages() - NumUnroll + UnrollNum == StageNum)
    UsePrologReg = false;
  else
    return;

  // Examples that show which stages are merged by phi.
  // Meaning of the symbol following the stage number:
  //   a/b: Stages with the same letter are merged (UsePrologReg == true)
  //   +: Merged with the initial value (UsePrologReg == false)
  //   *: No phis required
  //
  // #Stages 3, #MVE 4
  // Iter   0 1 2 3 4 5 6 7 8
  // -----------------------------------------
  // Stage  0a                 Prolog#0
  // Stage  1a 0b              Prolog#1
  // Stage  2* 1* 0*           Kernel Unroll#0
  // Stage     2* 1* 0+        Kernel Unroll#1
  // Stage        2* 1+ 0a     Kernel Unroll#2
  // Stage           2+ 1a 0b  Kernel Unroll#3
  //
  // #Stages 3, #MVE 2
  // Iter   0 1 2 3 4 5 6 7 8
  // -----------------------------------------
  // Stage  0a                 Prolog#0
  // Stage  1a 0b              Prolog#1
  // Stage  2* 1+ 0a           Kernel Unroll#0
  // Stage     2+ 1a 0b        Kernel Unroll#1
  //
  // #Stages 3, #MVE 1
  // Iter   0 1 2 3 4 5 6 7 8
  // -----------------------------------------
  // Stage  0*                 Prolog#0
  // Stage  1a 0b              Prolog#1
  // Stage  2+ 1a 0b           Kernel Unroll#0

  for (MachineOperand &DefMO : OrigMI->defs()) {
    if (!DefMO.isReg() || DefMO.isDead())
      continue;
    Register OrigReg = DefMO.getReg();
    auto NewReg = KernelVRMap[UnrollNum].find(OrigReg);
    if (NewReg == KernelVRMap[UnrollNum].end())
      continue;
    Register CorrespondReg;
    if (UsePrologReg) {
      int PrologNum = Schedule.getNumStages() - NumUnroll + UnrollNum - 1;
      CorrespondReg = PrologVRMap[PrologNum][OrigReg];
    } else {
      MachineInstr *Phi = getLoopPhiUser(OrigReg, OrigKernel);
      if (!Phi)
        continue;
      CorrespondReg = getInitPhiReg(*Phi, OrigKernel);
    }

    assert(CorrespondReg.isValid());
    Register PhiReg = MRI.createVirtualRegister(MRI.getRegClass(OrigReg));
    BuildMI(*NewKernel, NewKernel->getFirstNonPHI(), DebugLoc(),
            TII->get(TargetOpcode::PHI), PhiReg)
        .addReg(NewReg->second)
        .addMBB(NewKernel)
        .addReg(CorrespondReg)
        .addMBB(Prolog);
    PhiVRMap[UnrollNum][OrigReg] = PhiReg;
  }
}

static void replacePhiSrc(MachineInstr &Phi, Register OrigReg, Register NewReg,
                          MachineBasicBlock *NewMBB) {
  for (unsigned Idx = 1; Idx < Phi.getNumOperands(); Idx += 2) {
    if (Phi.getOperand(Idx).getReg() == OrigReg) {
      Phi.getOperand(Idx).setReg(NewReg);
      Phi.getOperand(Idx + 1).setMBB(NewMBB);
      return;
    }
  }
}

/// Generate phis that merge values from multiple routes
void ModuloScheduleExpanderMVE::mergeRegUsesAfterPipeline(Register OrigReg,
                                                          Register NewReg) {
  SmallVector<MachineOperand *> UsesAfterLoop;
  SmallVector<MachineInstr *> LoopPhis;
  for (MachineRegisterInfo::use_iterator I = MRI.use_begin(OrigReg),
                                         E = MRI.use_end();
       I != E; ++I) {
    MachineOperand &O = *I;
    if (O.getParent()->getParent() != OrigKernel &&
        O.getParent()->getParent() != Prolog &&
        O.getParent()->getParent() != NewKernel &&
        O.getParent()->getParent() != Epilog)
      UsesAfterLoop.push_back(&O);
    if (O.getParent()->getParent() == OrigKernel && O.getParent()->isPHI())
      LoopPhis.push_back(O.getParent());
  }

  // Merge the route that only execute the pipelined loop (when there are no
  // remaining iterations) with the route that execute the original loop.
  if (!UsesAfterLoop.empty()) {
    Register PhiReg = MRI.createVirtualRegister(MRI.getRegClass(OrigReg));
    BuildMI(*NewExit, NewExit->getFirstNonPHI(), DebugLoc(),
            TII->get(TargetOpcode::PHI), PhiReg)
        .addReg(OrigReg)
        .addMBB(OrigKernel)
        .addReg(NewReg)
        .addMBB(Epilog);

    for (MachineOperand *MO : UsesAfterLoop)
      MO->setReg(PhiReg);

    if (!LIS.hasInterval(PhiReg))
      LIS.createEmptyInterval(PhiReg);
  }

  // Merge routes from the pipelined loop and the bypassed route before the
  // original loop
  if (!LoopPhis.empty()) {
    for (MachineInstr *Phi : LoopPhis) {
      unsigned InitReg, LoopReg;
      getPhiRegs(*Phi, OrigKernel, InitReg, LoopReg);
      Register NewInit = MRI.createVirtualRegister(MRI.getRegClass(InitReg));
      BuildMI(*NewPreheader, NewPreheader->getFirstNonPHI(), Phi->getDebugLoc(),
              TII->get(TargetOpcode::PHI), NewInit)
          .addReg(InitReg)
          .addMBB(Check)
          .addReg(NewReg)
          .addMBB(Epilog);
      replacePhiSrc(*Phi, InitReg, NewInit, NewPreheader);
    }
  }
}

void ModuloScheduleExpanderMVE::generateProlog(
    SmallVectorImpl<ValueMapTy> &PrologVRMap) {
  PrologVRMap.clear();
  PrologVRMap.resize(Schedule.getNumStages() - 1);
  DenseMap<MachineInstr *, std::pair<int, int>> NewMIMap;
  for (int PrologNum = 0; PrologNum < Schedule.getNumStages() - 1;
       ++PrologNum) {
    for (MachineInstr *MI : Schedule.getInstructions()) {
      if (MI->isPHI())
        continue;
      int StageNum = Schedule.getStage(MI);
      if (StageNum > PrologNum)
        continue;
      MachineInstr *NewMI = cloneInstr(MI);
      updateInstrDef(NewMI, PrologVRMap[PrologNum], false);
      NewMIMap[NewMI] = {PrologNum, StageNum};
      Prolog->push_back(NewMI);
    }
  }

  for (auto I : NewMIMap) {
    MachineInstr *MI = I.first;
    int PrologNum = I.second.first;
    int StageNum = I.second.second;
    updateInstrUse(MI, StageNum, PrologNum, PrologVRMap, nullptr);
  }

  LLVM_DEBUG({
    dbgs() << "prolog:\n";
    Prolog->dump();
  });
}

void ModuloScheduleExpanderMVE::generateKernel(
    SmallVectorImpl<ValueMapTy> &PrologVRMap,
    SmallVectorImpl<ValueMapTy> &KernelVRMap, InstrMapTy &LastStage0Insts) {
  KernelVRMap.clear();
  KernelVRMap.resize(NumUnroll);
  SmallVector<ValueMapTy> PhiVRMap;
  PhiVRMap.resize(NumUnroll);
  DenseMap<MachineInstr *, std::pair<int, int>> NewMIMap;
  DenseMap<MachineInstr *, MachineInstr *> MIMapLastStage0;
  for (int UnrollNum = 0; UnrollNum < NumUnroll; ++UnrollNum) {
    for (MachineInstr *MI : Schedule.getInstructions()) {
      if (MI->isPHI())
        continue;
      int StageNum = Schedule.getStage(MI);
      MachineInstr *NewMI = cloneInstr(MI);
      if (UnrollNum == NumUnroll - 1)
        LastStage0Insts[MI] = NewMI;
      updateInstrDef(NewMI, KernelVRMap[UnrollNum],
                     (UnrollNum == NumUnroll - 1 && StageNum == 0));
      generatePhi(MI, UnrollNum, PrologVRMap, KernelVRMap, PhiVRMap);
      NewMIMap[NewMI] = {UnrollNum, StageNum};
      NewKernel->push_back(NewMI);
    }
  }

  for (auto I : NewMIMap) {
    MachineInstr *MI = I.first;
    int UnrollNum = I.second.first;
    int StageNum = I.second.second;
    updateInstrUse(MI, StageNum, UnrollNum, KernelVRMap, &PhiVRMap);
  }

  // If remaining trip count is greater than NumUnroll-1, loop continues
  insertCondBranch(*NewKernel, NumUnroll - 1, LastStage0Insts, *NewKernel,
                   *Epilog);

  LLVM_DEBUG({
    dbgs() << "kernel:\n";
    NewKernel->dump();
  });
}

void ModuloScheduleExpanderMVE::generateEpilog(
    SmallVectorImpl<ValueMapTy> &KernelVRMap,
    SmallVectorImpl<ValueMapTy> &EpilogVRMap, InstrMapTy &LastStage0Insts) {
  EpilogVRMap.clear();
  EpilogVRMap.resize(Schedule.getNumStages() - 1);
  DenseMap<MachineInstr *, std::pair<int, int>> NewMIMap;
  for (int EpilogNum = 0; EpilogNum < Schedule.getNumStages() - 1;
       ++EpilogNum) {
    for (MachineInstr *MI : Schedule.getInstructions()) {
      if (MI->isPHI())
        continue;
      int StageNum = Schedule.getStage(MI);
      if (StageNum <= EpilogNum)
        continue;
      MachineInstr *NewMI = cloneInstr(MI);
      updateInstrDef(NewMI, EpilogVRMap[EpilogNum], StageNum - 1 == EpilogNum);
      NewMIMap[NewMI] = {EpilogNum, StageNum};
      Epilog->push_back(NewMI);
    }
  }

  for (auto I : NewMIMap) {
    MachineInstr *MI = I.first;
    int EpilogNum = I.second.first;
    int StageNum = I.second.second;
    updateInstrUse(MI, StageNum, EpilogNum, EpilogVRMap, &KernelVRMap);
  }

  // If there are remaining iterations, they are executed in the original loop.
  // Instructions related to loop control, such as loop counter comparison,
  // are indicated by shouldIgnoreForPipelining() and are assumed to be placed
  // in stage 0. Thus, the map is for the last one in the kernel.
  insertCondBranch(*Epilog, 0, LastStage0Insts, *NewPreheader, *NewExit);

  LLVM_DEBUG({
    dbgs() << "epilog:\n";
    Epilog->dump();
  });
}

/// Calculate the number of unroll required and set it to NumUnroll
void ModuloScheduleExpanderMVE::calcNumUnroll() {
  DenseMap<MachineInstr *, unsigned> Inst2Idx;
  NumUnroll = 1;
  for (unsigned I = 0; I < Schedule.getInstructions().size(); ++I)
    Inst2Idx[Schedule.getInstructions()[I]] = I;

  for (MachineInstr *MI : Schedule.getInstructions()) {
    if (MI->isPHI())
      continue;
    int StageNum = Schedule.getStage(MI);
    for (const MachineOperand &MO : MI->uses()) {
      if (!MO.isReg() || !MO.getReg().isVirtual())
        continue;
      MachineInstr *DefMI = MRI.getVRegDef(MO.getReg());
      if (DefMI->getParent() != OrigKernel)
        continue;

      int NumUnrollLocal = 1;
      if (DefMI->isPHI()) {
        ++NumUnrollLocal;
        // canApply() guarantees that DefMI is not phi and is an instruction in
        // the loop
        DefMI = MRI.getVRegDef(getLoopPhiReg(*DefMI, OrigKernel));
      }
      NumUnrollLocal += StageNum - Schedule.getStage(DefMI);
      if (Inst2Idx[MI] <= Inst2Idx[DefMI])
        --NumUnrollLocal;
      NumUnroll = std::max(NumUnroll, NumUnrollLocal);
    }
  }
  LLVM_DEBUG(dbgs() << "NumUnroll: " << NumUnroll << "\n");
}

/// Create new virtual registers for definitions of NewMI and update NewMI.
/// If the definitions are referenced after the pipelined loop, phis are
/// created to merge with other routes.
void ModuloScheduleExpanderMVE::updateInstrDef(MachineInstr *NewMI,
                                               ValueMapTy &VRMap,
                                               bool LastDef) {
  for (MachineOperand &MO : NewMI->operands()) {
    if (!MO.isReg() || !MO.getReg().isVirtual() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    const TargetRegisterClass *RC = MRI.getRegClass(Reg);
    Register NewReg = MRI.createVirtualRegister(RC);
    MO.setReg(NewReg);
    VRMap[Reg] = NewReg;
    if (LastDef)
      mergeRegUsesAfterPipeline(Reg, NewReg);
  }
}

void ModuloScheduleExpanderMVE::expand() {
  OrigKernel = Schedule.getLoop()->getTopBlock();
  OrigPreheader = Schedule.getLoop()->getLoopPreheader();
  OrigExit = Schedule.getLoop()->getExitBlock();

  LLVM_DEBUG(Schedule.dump());

  generatePipelinedLoop();
}

/// Check if ModuloScheduleExpanderMVE can be applied to L
bool ModuloScheduleExpanderMVE::canApply(MachineLoop &L) {
  if (!L.getExitBlock()) {
    LLVM_DEBUG(
        dbgs() << "Can not apply MVE expander: No single exit block.\n";);
    return false;
  }

  MachineBasicBlock *BB = L.getTopBlock();
  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();

  // Put some constraints on the operands of the phis to simplify the
  // transformation
  DenseSet<unsigned> UsedByPhi;
  for (MachineInstr &MI : BB->phis()) {
    // Registers defined by phis must be used only inside the loop and be never
    // used by phis.
    for (MachineOperand &MO : MI.defs())
      if (MO.isReg())
        for (MachineInstr &Ref : MRI.use_instructions(MO.getReg()))
          if (Ref.getParent() != BB || Ref.isPHI()) {
            LLVM_DEBUG(dbgs()
                           << "Can not apply MVE expander: A phi result is "
                              "referenced outside of the loop or by phi.\n";);
            return false;
          }

    // A source register from the loop block must be defined inside the loop.
    // A register defined inside the loop must be referenced by only one phi at
    // most.
    unsigned InitVal, LoopVal;
    getPhiRegs(MI, MI.getParent(), InitVal, LoopVal);
    if (!Register(LoopVal).isVirtual() ||
        MRI.getVRegDef(LoopVal)->getParent() != BB) {
      LLVM_DEBUG(
          dbgs() << "Can not apply MVE expander: A phi source value coming "
                    "from the loop is not defined in the loop.\n";);
      return false;
    }
    if (UsedByPhi.count(LoopVal)) {
      LLVM_DEBUG(dbgs() << "Can not apply MVE expander: A value defined in the "
                           "loop is referenced by two or more phis.\n";);
      return false;
    }
    UsedByPhi.insert(LoopVal);
  }

  return true;
}

//===----------------------------------------------------------------------===//
// ModuloScheduleTestPass implementation
//===----------------------------------------------------------------------===//
// This pass constructs a ModuloSchedule from its module and runs
// ModuloScheduleExpander.
//
// The module is expected to contain a single-block analyzable loop.
// The total order of instructions is taken from the loop as-is.
// Instructions are expected to be annotated with a PostInstrSymbol.
// This PostInstrSymbol must have the following format:
//  "Stage=%d Cycle=%d".
//===----------------------------------------------------------------------===//

namespace {
class ModuloScheduleTest : public MachineFunctionPass {
public:
  static char ID;

  ModuloScheduleTest() : MachineFunctionPass(ID) {
    initializeModuloScheduleTestPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void runOnLoop(MachineFunction &MF, MachineLoop &L);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<LiveIntervalsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // namespace

char ModuloScheduleTest::ID = 0;

INITIALIZE_PASS_BEGIN(ModuloScheduleTest, "modulo-schedule-test",
                      "Modulo Schedule test pass", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_END(ModuloScheduleTest, "modulo-schedule-test",
                    "Modulo Schedule test pass", false, false)

bool ModuloScheduleTest::runOnMachineFunction(MachineFunction &MF) {
  MachineLoopInfo &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  for (auto *L : MLI) {
    if (L->getTopBlock() != L->getBottomBlock())
      continue;
    runOnLoop(MF, *L);
    return false;
  }
  return false;
}

static void parseSymbolString(StringRef S, int &Cycle, int &Stage) {
  std::pair<StringRef, StringRef> StageAndCycle = getToken(S, "_");
  std::pair<StringRef, StringRef> StageTokenAndValue =
      getToken(StageAndCycle.first, "-");
  std::pair<StringRef, StringRef> CycleTokenAndValue =
      getToken(StageAndCycle.second, "-");
  if (StageTokenAndValue.first != "Stage" ||
      CycleTokenAndValue.first != "_Cycle") {
    llvm_unreachable(
        "Bad post-instr symbol syntax: see comment in ModuloScheduleTest");
    return;
  }

  StageTokenAndValue.second.drop_front().getAsInteger(10, Stage);
  CycleTokenAndValue.second.drop_front().getAsInteger(10, Cycle);

  dbgs() << "  Stage=" << Stage << ", Cycle=" << Cycle << "\n";
}

void ModuloScheduleTest::runOnLoop(MachineFunction &MF, MachineLoop &L) {
  LiveIntervals &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  MachineBasicBlock *BB = L.getTopBlock();
  dbgs() << "--- ModuloScheduleTest running on BB#" << BB->getNumber() << "\n";

  DenseMap<MachineInstr *, int> Cycle, Stage;
  std::vector<MachineInstr *> Instrs;
  for (MachineInstr &MI : *BB) {
    if (MI.isTerminator())
      continue;
    Instrs.push_back(&MI);
    if (MCSymbol *Sym = MI.getPostInstrSymbol()) {
      dbgs() << "Parsing post-instr symbol for " << MI;
      parseSymbolString(Sym->getName(), Cycle[&MI], Stage[&MI]);
    }
  }

  ModuloSchedule MS(MF, &L, std::move(Instrs), std::move(Cycle),
                    std::move(Stage));
  ModuloScheduleExpander MSE(
      MF, MS, LIS, /*InstrChanges=*/ModuloScheduleExpander::InstrChangesTy());
  MSE.expand();
  MSE.cleanup();
}

//===----------------------------------------------------------------------===//
// ModuloScheduleTestAnnotater implementation
//===----------------------------------------------------------------------===//

void ModuloScheduleTestAnnotater::annotate() {
  for (MachineInstr *MI : S.getInstructions()) {
    SmallVector<char, 16> SV;
    raw_svector_ostream OS(SV);
    OS << "Stage-" << S.getStage(MI) << "_Cycle-" << S.getCycle(MI);
    MCSymbol *Sym = MF.getContext().getOrCreateSymbol(OS.str());
    MI->setPostInstrSymbol(MF, Sym);
  }
}
