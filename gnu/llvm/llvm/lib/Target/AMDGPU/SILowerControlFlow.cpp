//===-- SILowerControlFlow.cpp - Use predicates for control flow ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass lowers the pseudo control flow instructions to real
/// machine instructions.
///
/// All control flow is handled using predicated instructions and
/// a predicate stack.  Each Scalar ALU controls the operations of 64 Vector
/// ALUs.  The Scalar ALU can update the predicate for any of the Vector ALUs
/// by writing to the 64-bit EXEC register (each bit corresponds to a
/// single vector ALU).  Typically, for predicates, a vector ALU will write
/// to its bit of the VCC register (like EXEC VCC is 64-bits, one for each
/// Vector ALU) and then the ScalarALU will AND the VCC register with the
/// EXEC to update the predicates.
///
/// For example:
/// %vcc = V_CMP_GT_F32 %vgpr1, %vgpr2
/// %sgpr0 = SI_IF %vcc
///   %vgpr0 = V_ADD_F32 %vgpr0, %vgpr0
/// %sgpr0 = SI_ELSE %sgpr0
///   %vgpr0 = V_SUB_F32 %vgpr0, %vgpr0
/// SI_END_CF %sgpr0
///
/// becomes:
///
/// %sgpr0 = S_AND_SAVEEXEC_B64 %vcc  // Save and update the exec mask
/// %sgpr0 = S_XOR_B64 %sgpr0, %exec  // Clear live bits from saved exec mask
/// S_CBRANCH_EXECZ label0            // This instruction is an optional
///                                   // optimization which allows us to
///                                   // branch if all the bits of
///                                   // EXEC are zero.
/// %vgpr0 = V_ADD_F32 %vgpr0, %vgpr0 // Do the IF block of the branch
///
/// label0:
/// %sgpr0 = S_OR_SAVEEXEC_B64 %sgpr0  // Restore the exec mask for the Then
///                                    // block
/// %exec = S_XOR_B64 %sgpr0, %exec    // Update the exec mask
/// S_BRANCH_EXECZ label1              // Use our branch optimization
///                                    // instruction again.
/// %vgpr0 = V_SUB_F32 %vgpr0, %vgpr   // Do the THEN block
/// label1:
/// %exec = S_OR_B64 %exec, %sgpr0     // Re-enable saved exec mask bits
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "si-lower-control-flow"

static cl::opt<bool>
RemoveRedundantEndcf("amdgpu-remove-redundant-endcf",
    cl::init(true), cl::ReallyHidden);

namespace {

class SILowerControlFlow : public MachineFunctionPass {
private:
  const SIRegisterInfo *TRI = nullptr;
  const SIInstrInfo *TII = nullptr;
  LiveIntervals *LIS = nullptr;
  LiveVariables *LV = nullptr;
  MachineDominatorTree *MDT = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  SetVector<MachineInstr*> LoweredEndCf;
  DenseSet<Register> LoweredIf;
  SmallSet<MachineBasicBlock *, 4> KillBlocks;
  SmallSet<Register, 8> RecomputeRegs;

  const TargetRegisterClass *BoolRC = nullptr;
  unsigned AndOpc;
  unsigned OrOpc;
  unsigned XorOpc;
  unsigned MovTermOpc;
  unsigned Andn2TermOpc;
  unsigned XorTermrOpc;
  unsigned OrTermrOpc;
  unsigned OrSaveExecOpc;
  unsigned Exec;

  bool EnableOptimizeEndCf = false;

  bool hasKill(const MachineBasicBlock *Begin, const MachineBasicBlock *End);

  void emitIf(MachineInstr &MI);
  void emitElse(MachineInstr &MI);
  void emitIfBreak(MachineInstr &MI);
  void emitLoop(MachineInstr &MI);

  MachineBasicBlock *emitEndCf(MachineInstr &MI);

  void findMaskOperands(MachineInstr &MI, unsigned OpNo,
                        SmallVectorImpl<MachineOperand> &Src) const;

  void combineMasks(MachineInstr &MI);

  bool removeMBBifRedundant(MachineBasicBlock &MBB);

  MachineBasicBlock *process(MachineInstr &MI);

  // Skip to the next instruction, ignoring debug instructions, and trivial
  // block boundaries (blocks that have one (typically fallthrough) successor,
  // and the successor has one predecessor.
  MachineBasicBlock::iterator
  skipIgnoreExecInstsTrivialSucc(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator It) const;

  /// Find the insertion point for a new conditional branch.
  MachineBasicBlock::iterator
  skipToUncondBrOrEnd(MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator I) const {
    assert(I->isTerminator());

    // FIXME: What if we had multiple pre-existing conditional branches?
    MachineBasicBlock::iterator End = MBB.end();
    while (I != End && !I->isUnconditionalBranch())
      ++I;
    return I;
  }

  // Remove redundant SI_END_CF instructions.
  void optimizeEndCf();

public:
  static char ID;

  SILowerControlFlow() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI Lower control flow pseudo instructions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addUsedIfAvailable<LiveIntervalsWrapperPass>();
    // Should preserve the same set that TwoAddressInstructions does.
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addPreservedID(LiveVariablesID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char SILowerControlFlow::ID = 0;

INITIALIZE_PASS(SILowerControlFlow, DEBUG_TYPE,
               "SI lower control flow", false, false)

static void setImpSCCDefDead(MachineInstr &MI, bool IsDead) {
  MachineOperand &ImpDefSCC = MI.getOperand(3);
  assert(ImpDefSCC.getReg() == AMDGPU::SCC && ImpDefSCC.isDef());

  ImpDefSCC.setIsDead(IsDead);
}

char &llvm::SILowerControlFlowID = SILowerControlFlow::ID;

bool SILowerControlFlow::hasKill(const MachineBasicBlock *Begin,
                                 const MachineBasicBlock *End) {
  DenseSet<const MachineBasicBlock*> Visited;
  SmallVector<MachineBasicBlock *, 4> Worklist(Begin->successors());

  while (!Worklist.empty()) {
    MachineBasicBlock *MBB = Worklist.pop_back_val();

    if (MBB == End || !Visited.insert(MBB).second)
      continue;
    if (KillBlocks.contains(MBB))
      return true;

    Worklist.append(MBB->succ_begin(), MBB->succ_end());
  }

  return false;
}

static bool isSimpleIf(const MachineInstr &MI, const MachineRegisterInfo *MRI) {
  Register SaveExecReg = MI.getOperand(0).getReg();
  auto U = MRI->use_instr_nodbg_begin(SaveExecReg);

  if (U == MRI->use_instr_nodbg_end() ||
      std::next(U) != MRI->use_instr_nodbg_end() ||
      U->getOpcode() != AMDGPU::SI_END_CF)
    return false;

  return true;
}

void SILowerControlFlow::emitIf(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator I(&MI);
  Register SaveExecReg = MI.getOperand(0).getReg();
  MachineOperand& Cond = MI.getOperand(1);
  assert(Cond.getSubReg() == AMDGPU::NoSubRegister);

  MachineOperand &ImpDefSCC = MI.getOperand(4);
  assert(ImpDefSCC.getReg() == AMDGPU::SCC && ImpDefSCC.isDef());

  // If there is only one use of save exec register and that use is SI_END_CF,
  // we can optimize SI_IF by returning the full saved exec mask instead of
  // just cleared bits.
  bool SimpleIf = isSimpleIf(MI, MRI);

  if (SimpleIf) {
    // Check for SI_KILL_*_TERMINATOR on path from if to endif.
    // if there is any such terminator simplifications are not safe.
    auto UseMI = MRI->use_instr_nodbg_begin(SaveExecReg);
    SimpleIf = !hasKill(MI.getParent(), UseMI->getParent());
  }

  // Add an implicit def of exec to discourage scheduling VALU after this which
  // will interfere with trying to form s_and_saveexec_b64 later.
  Register CopyReg = SimpleIf ? SaveExecReg
                       : MRI->createVirtualRegister(BoolRC);
  MachineInstr *CopyExec =
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), CopyReg)
    .addReg(Exec)
    .addReg(Exec, RegState::ImplicitDefine);
  LoweredIf.insert(CopyReg);

  Register Tmp = MRI->createVirtualRegister(BoolRC);

  MachineInstr *And =
    BuildMI(MBB, I, DL, TII->get(AndOpc), Tmp)
    .addReg(CopyReg)
    .add(Cond);
  if (LV)
    LV->replaceKillInstruction(Cond.getReg(), MI, *And);

  setImpSCCDefDead(*And, true);

  MachineInstr *Xor = nullptr;
  if (!SimpleIf) {
    Xor =
      BuildMI(MBB, I, DL, TII->get(XorOpc), SaveExecReg)
      .addReg(Tmp)
      .addReg(CopyReg);
    setImpSCCDefDead(*Xor, ImpDefSCC.isDead());
  }

  // Use a copy that is a terminator to get correct spill code placement it with
  // fast regalloc.
  MachineInstr *SetExec =
    BuildMI(MBB, I, DL, TII->get(MovTermOpc), Exec)
    .addReg(Tmp, RegState::Kill);
  if (LV)
    LV->getVarInfo(Tmp).Kills.push_back(SetExec);

  // Skip ahead to the unconditional branch in case there are other terminators
  // present.
  I = skipToUncondBrOrEnd(MBB, I);

  // Insert the S_CBRANCH_EXECZ instruction which will be optimized later
  // during SIRemoveShortExecBranches.
  MachineInstr *NewBr = BuildMI(MBB, I, DL, TII->get(AMDGPU::S_CBRANCH_EXECZ))
                            .add(MI.getOperand(2));

  if (!LIS) {
    MI.eraseFromParent();
    return;
  }

  LIS->InsertMachineInstrInMaps(*CopyExec);

  // Replace with and so we don't need to fix the live interval for condition
  // register.
  LIS->ReplaceMachineInstrInMaps(MI, *And);

  if (!SimpleIf)
    LIS->InsertMachineInstrInMaps(*Xor);
  LIS->InsertMachineInstrInMaps(*SetExec);
  LIS->InsertMachineInstrInMaps(*NewBr);

  LIS->removeAllRegUnitsForPhysReg(AMDGPU::EXEC);
  MI.eraseFromParent();

  // FIXME: Is there a better way of adjusting the liveness? It shouldn't be
  // hard to add another def here but I'm not sure how to correctly update the
  // valno.
  RecomputeRegs.insert(SaveExecReg);
  LIS->createAndComputeVirtRegInterval(Tmp);
  if (!SimpleIf)
    LIS->createAndComputeVirtRegInterval(CopyReg);
}

void SILowerControlFlow::emitElse(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();

  MachineBasicBlock::iterator Start = MBB.begin();

  // This must be inserted before phis and any spill code inserted before the
  // else.
  Register SaveReg = MRI->createVirtualRegister(BoolRC);
  MachineInstr *OrSaveExec =
    BuildMI(MBB, Start, DL, TII->get(OrSaveExecOpc), SaveReg)
    .add(MI.getOperand(1)); // Saved EXEC
  if (LV)
    LV->replaceKillInstruction(SrcReg, MI, *OrSaveExec);

  MachineBasicBlock *DestBB = MI.getOperand(2).getMBB();

  MachineBasicBlock::iterator ElsePt(MI);

  // This accounts for any modification of the EXEC mask within the block and
  // can be optimized out pre-RA when not required.
  MachineInstr *And = BuildMI(MBB, ElsePt, DL, TII->get(AndOpc), DstReg)
                          .addReg(Exec)
                          .addReg(SaveReg);

  MachineInstr *Xor =
    BuildMI(MBB, ElsePt, DL, TII->get(XorTermrOpc), Exec)
    .addReg(Exec)
    .addReg(DstReg);

  // Skip ahead to the unconditional branch in case there are other terminators
  // present.
  ElsePt = skipToUncondBrOrEnd(MBB, ElsePt);

  MachineInstr *Branch =
      BuildMI(MBB, ElsePt, DL, TII->get(AMDGPU::S_CBRANCH_EXECZ))
          .addMBB(DestBB);

  if (!LIS) {
    MI.eraseFromParent();
    return;
  }

  LIS->RemoveMachineInstrFromMaps(MI);
  MI.eraseFromParent();

  LIS->InsertMachineInstrInMaps(*OrSaveExec);
  LIS->InsertMachineInstrInMaps(*And);

  LIS->InsertMachineInstrInMaps(*Xor);
  LIS->InsertMachineInstrInMaps(*Branch);

  RecomputeRegs.insert(SrcReg);
  RecomputeRegs.insert(DstReg);
  LIS->createAndComputeVirtRegInterval(SaveReg);

  // Let this be recomputed.
  LIS->removeAllRegUnitsForPhysReg(AMDGPU::EXEC);
}

void SILowerControlFlow::emitIfBreak(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  auto Dst = MI.getOperand(0).getReg();

  // Skip ANDing with exec if the break condition is already masked by exec
  // because it is a V_CMP in the same basic block. (We know the break
  // condition operand was an i1 in IR, so if it is a VALU instruction it must
  // be one with a carry-out.)
  bool SkipAnding = false;
  if (MI.getOperand(1).isReg()) {
    if (MachineInstr *Def = MRI->getUniqueVRegDef(MI.getOperand(1).getReg())) {
      SkipAnding = Def->getParent() == MI.getParent()
          && SIInstrInfo::isVALU(*Def);
    }
  }

  // AND the break condition operand with exec, then OR that into the "loop
  // exit" mask.
  MachineInstr *And = nullptr, *Or = nullptr;
  Register AndReg;
  if (!SkipAnding) {
    AndReg = MRI->createVirtualRegister(BoolRC);
    And = BuildMI(MBB, &MI, DL, TII->get(AndOpc), AndReg)
             .addReg(Exec)
             .add(MI.getOperand(1));
    if (LV)
      LV->replaceKillInstruction(MI.getOperand(1).getReg(), MI, *And);
    Or = BuildMI(MBB, &MI, DL, TII->get(OrOpc), Dst)
             .addReg(AndReg)
             .add(MI.getOperand(2));
  } else {
    Or = BuildMI(MBB, &MI, DL, TII->get(OrOpc), Dst)
             .add(MI.getOperand(1))
             .add(MI.getOperand(2));
    if (LV)
      LV->replaceKillInstruction(MI.getOperand(1).getReg(), MI, *Or);
  }
  if (LV)
    LV->replaceKillInstruction(MI.getOperand(2).getReg(), MI, *Or);

  if (LIS) {
    LIS->ReplaceMachineInstrInMaps(MI, *Or);
    if (And) {
      // Read of original operand 1 is on And now not Or.
      RecomputeRegs.insert(And->getOperand(2).getReg());
      LIS->InsertMachineInstrInMaps(*And);
      LIS->createAndComputeVirtRegInterval(AndReg);
    }
  }

  MI.eraseFromParent();
}

void SILowerControlFlow::emitLoop(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  MachineInstr *AndN2 =
      BuildMI(MBB, &MI, DL, TII->get(Andn2TermOpc), Exec)
          .addReg(Exec)
          .add(MI.getOperand(0));
  if (LV)
    LV->replaceKillInstruction(MI.getOperand(0).getReg(), MI, *AndN2);

  auto BranchPt = skipToUncondBrOrEnd(MBB, MI.getIterator());
  MachineInstr *Branch =
      BuildMI(MBB, BranchPt, DL, TII->get(AMDGPU::S_CBRANCH_EXECNZ))
          .add(MI.getOperand(1));

  if (LIS) {
    RecomputeRegs.insert(MI.getOperand(0).getReg());
    LIS->ReplaceMachineInstrInMaps(MI, *AndN2);
    LIS->InsertMachineInstrInMaps(*Branch);
  }

  MI.eraseFromParent();
}

MachineBasicBlock::iterator
SILowerControlFlow::skipIgnoreExecInstsTrivialSucc(
  MachineBasicBlock &MBB, MachineBasicBlock::iterator It) const {

  SmallSet<const MachineBasicBlock *, 4> Visited;
  MachineBasicBlock *B = &MBB;
  do {
    if (!Visited.insert(B).second)
      return MBB.end();

    auto E = B->end();
    for ( ; It != E; ++It) {
      if (TII->mayReadEXEC(*MRI, *It))
        break;
    }

    if (It != E)
      return It;

    if (B->succ_size() != 1)
      return MBB.end();

    // If there is one trivial successor, advance to the next block.
    MachineBasicBlock *Succ = *B->succ_begin();

    It = Succ->begin();
    B = Succ;
  } while (true);
}

MachineBasicBlock *SILowerControlFlow::emitEndCf(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  MachineBasicBlock::iterator InsPt = MBB.begin();

  // If we have instructions that aren't prolog instructions, split the block
  // and emit a terminator instruction. This ensures correct spill placement.
  // FIXME: We should unconditionally split the block here.
  bool NeedBlockSplit = false;
  Register DataReg = MI.getOperand(0).getReg();
  for (MachineBasicBlock::iterator I = InsPt, E = MI.getIterator();
       I != E; ++I) {
    if (I->modifiesRegister(DataReg, TRI)) {
      NeedBlockSplit = true;
      break;
    }
  }

  unsigned Opcode = OrOpc;
  MachineBasicBlock *SplitBB = &MBB;
  if (NeedBlockSplit) {
    SplitBB = MBB.splitAt(MI, /*UpdateLiveIns*/true, LIS);
    if (MDT && SplitBB != &MBB) {
      MachineDomTreeNode *MBBNode = (*MDT)[&MBB];
      SmallVector<MachineDomTreeNode *> Children(MBBNode->begin(),
                                                 MBBNode->end());
      MachineDomTreeNode *SplitBBNode = MDT->addNewBlock(SplitBB, &MBB);
      for (MachineDomTreeNode *Child : Children)
        MDT->changeImmediateDominator(Child, SplitBBNode);
    }
    Opcode = OrTermrOpc;
    InsPt = MI;
  }

  MachineInstr *NewMI =
    BuildMI(MBB, InsPt, DL, TII->get(Opcode), Exec)
    .addReg(Exec)
    .add(MI.getOperand(0));
  if (LV) {
    LV->replaceKillInstruction(DataReg, MI, *NewMI);

    if (SplitBB != &MBB) {
      // Track the set of registers defined in the original block so we don't
      // accidentally add the original block to AliveBlocks. AliveBlocks only
      // includes blocks which are live through, which excludes live outs and
      // local defs.
      DenseSet<Register> DefInOrigBlock;

      for (MachineBasicBlock *BlockPiece : {&MBB, SplitBB}) {
        for (MachineInstr &X : *BlockPiece) {
          for (MachineOperand &Op : X.all_defs()) {
            if (Op.getReg().isVirtual())
              DefInOrigBlock.insert(Op.getReg());
          }
        }
      }

      for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
        Register Reg = Register::index2VirtReg(i);
        LiveVariables::VarInfo &VI = LV->getVarInfo(Reg);

        if (VI.AliveBlocks.test(MBB.getNumber()))
          VI.AliveBlocks.set(SplitBB->getNumber());
        else {
          for (MachineInstr *Kill : VI.Kills) {
            if (Kill->getParent() == SplitBB && !DefInOrigBlock.contains(Reg))
              VI.AliveBlocks.set(MBB.getNumber());
          }
        }
      }
    }
  }

  LoweredEndCf.insert(NewMI);

  if (LIS)
    LIS->ReplaceMachineInstrInMaps(MI, *NewMI);

  MI.eraseFromParent();

  if (LIS)
    LIS->handleMove(*NewMI);
  return SplitBB;
}

// Returns replace operands for a logical operation, either single result
// for exec or two operands if source was another equivalent operation.
void SILowerControlFlow::findMaskOperands(MachineInstr &MI, unsigned OpNo,
       SmallVectorImpl<MachineOperand> &Src) const {
  MachineOperand &Op = MI.getOperand(OpNo);
  if (!Op.isReg() || !Op.getReg().isVirtual()) {
    Src.push_back(Op);
    return;
  }

  MachineInstr *Def = MRI->getUniqueVRegDef(Op.getReg());
  if (!Def || Def->getParent() != MI.getParent() ||
      !(Def->isFullCopy() || (Def->getOpcode() == MI.getOpcode())))
    return;

  // Make sure we do not modify exec between def and use.
  // A copy with implicitly defined exec inserted earlier is an exclusion, it
  // does not really modify exec.
  for (auto I = Def->getIterator(); I != MI.getIterator(); ++I)
    if (I->modifiesRegister(AMDGPU::EXEC, TRI) &&
        !(I->isCopy() && I->getOperand(0).getReg() != Exec))
      return;

  for (const auto &SrcOp : Def->explicit_operands())
    if (SrcOp.isReg() && SrcOp.isUse() &&
        (SrcOp.getReg().isVirtual() || SrcOp.getReg() == Exec))
      Src.push_back(SrcOp);
}

// Search and combine pairs of equivalent instructions, like
// S_AND_B64 x, (S_AND_B64 x, y) => S_AND_B64 x, y
// S_OR_B64  x, (S_OR_B64  x, y) => S_OR_B64  x, y
// One of the operands is exec mask.
void SILowerControlFlow::combineMasks(MachineInstr &MI) {
  assert(MI.getNumExplicitOperands() == 3);
  SmallVector<MachineOperand, 4> Ops;
  unsigned OpToReplace = 1;
  findMaskOperands(MI, 1, Ops);
  if (Ops.size() == 1) OpToReplace = 2; // First operand can be exec or its copy
  findMaskOperands(MI, 2, Ops);
  if (Ops.size() != 3) return;

  unsigned UniqueOpndIdx;
  if (Ops[0].isIdenticalTo(Ops[1])) UniqueOpndIdx = 2;
  else if (Ops[0].isIdenticalTo(Ops[2])) UniqueOpndIdx = 1;
  else if (Ops[1].isIdenticalTo(Ops[2])) UniqueOpndIdx = 1;
  else return;

  Register Reg = MI.getOperand(OpToReplace).getReg();
  MI.removeOperand(OpToReplace);
  MI.addOperand(Ops[UniqueOpndIdx]);
  if (MRI->use_empty(Reg))
    MRI->getUniqueVRegDef(Reg)->eraseFromParent();
}

void SILowerControlFlow::optimizeEndCf() {
  // If the only instruction immediately following this END_CF is another
  // END_CF in the only successor we can avoid emitting exec mask restore here.
  if (!EnableOptimizeEndCf)
    return;

  for (MachineInstr *MI : reverse(LoweredEndCf)) {
    MachineBasicBlock &MBB = *MI->getParent();
    auto Next =
      skipIgnoreExecInstsTrivialSucc(MBB, std::next(MI->getIterator()));
    if (Next == MBB.end() || !LoweredEndCf.count(&*Next))
      continue;
    // Only skip inner END_CF if outer ENDCF belongs to SI_IF.
    // If that belongs to SI_ELSE then saved mask has an inverted value.
    Register SavedExec
      = TII->getNamedOperand(*Next, AMDGPU::OpName::src1)->getReg();
    assert(SavedExec.isVirtual() && "Expected saved exec to be src1!");

    const MachineInstr *Def = MRI->getUniqueVRegDef(SavedExec);
    if (Def && LoweredIf.count(SavedExec)) {
      LLVM_DEBUG(dbgs() << "Skip redundant "; MI->dump());
      if (LIS)
        LIS->RemoveMachineInstrFromMaps(*MI);
      Register Reg;
      if (LV)
        Reg = TII->getNamedOperand(*MI, AMDGPU::OpName::src1)->getReg();
      MI->eraseFromParent();
      if (LV)
        LV->recomputeForSingleDefVirtReg(Reg);
      removeMBBifRedundant(MBB);
    }
  }
}

MachineBasicBlock *SILowerControlFlow::process(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::iterator I(MI);
  MachineInstr *Prev = (I != MBB.begin()) ? &*(std::prev(I)) : nullptr;

  MachineBasicBlock *SplitBB = &MBB;

  switch (MI.getOpcode()) {
  case AMDGPU::SI_IF:
    emitIf(MI);
    break;

  case AMDGPU::SI_ELSE:
    emitElse(MI);
    break;

  case AMDGPU::SI_IF_BREAK:
    emitIfBreak(MI);
    break;

  case AMDGPU::SI_LOOP:
    emitLoop(MI);
    break;

  case AMDGPU::SI_WATERFALL_LOOP:
    MI.setDesc(TII->get(AMDGPU::S_CBRANCH_EXECNZ));
    break;

  case AMDGPU::SI_END_CF:
    SplitBB = emitEndCf(MI);
    break;

  default:
    assert(false && "Attempt to process unsupported instruction");
    break;
  }

  MachineBasicBlock::iterator Next;
  for (I = Prev ? Prev->getIterator() : MBB.begin(); I != MBB.end(); I = Next) {
    Next = std::next(I);
    MachineInstr &MaskMI = *I;
    switch (MaskMI.getOpcode()) {
    case AMDGPU::S_AND_B64:
    case AMDGPU::S_OR_B64:
    case AMDGPU::S_AND_B32:
    case AMDGPU::S_OR_B32:
      // Cleanup bit manipulations on exec mask
      combineMasks(MaskMI);
      break;
    default:
      I = MBB.end();
      break;
    }
  }

  return SplitBB;
}

bool SILowerControlFlow::removeMBBifRedundant(MachineBasicBlock &MBB) {
  for (auto &I : MBB.instrs()) {
    if (!I.isDebugInstr() && !I.isUnconditionalBranch())
      return false;
  }

  assert(MBB.succ_size() == 1 && "MBB has more than one successor");

  MachineBasicBlock *Succ = *MBB.succ_begin();
  MachineBasicBlock *FallThrough = nullptr;

  while (!MBB.predecessors().empty()) {
    MachineBasicBlock *P = *MBB.pred_begin();
    if (P->getFallThrough(false) == &MBB)
      FallThrough = P;
    P->ReplaceUsesOfBlockWith(&MBB, Succ);
  }
  MBB.removeSuccessor(Succ);
  if (LIS) {
    for (auto &I : MBB.instrs())
      LIS->RemoveMachineInstrFromMaps(I);
  }
  if (MDT) {
    // If Succ, the single successor of MBB, is dominated by MBB, MDT needs
    // updating by changing Succ's idom to the one of MBB; otherwise, MBB must
    // be a leaf node in MDT and could be erased directly.
    if (MDT->dominates(&MBB, Succ))
      MDT->changeImmediateDominator(MDT->getNode(Succ),
                                    MDT->getNode(&MBB)->getIDom());
    MDT->eraseNode(&MBB);
  }
  MBB.clear();
  MBB.eraseFromParent();
  if (FallThrough && !FallThrough->isLayoutSuccessor(Succ)) {
    // Note: we cannot update block layout and preserve live intervals;
    // hence we must insert a branch.
    MachineInstr *BranchMI = BuildMI(*FallThrough, FallThrough->end(),
            FallThrough->findBranchDebugLoc(), TII->get(AMDGPU::S_BRANCH))
        .addMBB(Succ);
    if (LIS)
      LIS->InsertMachineInstrInMaps(*BranchMI);
  }

  return true;
}

bool SILowerControlFlow::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  TII = ST.getInstrInfo();
  TRI = &TII->getRegisterInfo();
  EnableOptimizeEndCf = RemoveRedundantEndcf &&
                        MF.getTarget().getOptLevel() > CodeGenOptLevel::None;

  // This doesn't actually need LiveIntervals, but we can preserve them.
  auto *LISWrapper = getAnalysisIfAvailable<LiveIntervalsWrapperPass>();
  LIS = LISWrapper ? &LISWrapper->getLIS() : nullptr;
  // This doesn't actually need LiveVariables, but we can preserve them.
  auto *LVWrapper = getAnalysisIfAvailable<LiveVariablesWrapperPass>();
  LV = LVWrapper ? &LVWrapper->getLV() : nullptr;
  auto *MDTWrapper = getAnalysisIfAvailable<MachineDominatorTreeWrapperPass>();
  MDT = MDTWrapper ? &MDTWrapper->getDomTree() : nullptr;
  MRI = &MF.getRegInfo();
  BoolRC = TRI->getBoolRC();

  if (ST.isWave32()) {
    AndOpc = AMDGPU::S_AND_B32;
    OrOpc = AMDGPU::S_OR_B32;
    XorOpc = AMDGPU::S_XOR_B32;
    MovTermOpc = AMDGPU::S_MOV_B32_term;
    Andn2TermOpc = AMDGPU::S_ANDN2_B32_term;
    XorTermrOpc = AMDGPU::S_XOR_B32_term;
    OrTermrOpc = AMDGPU::S_OR_B32_term;
    OrSaveExecOpc = AMDGPU::S_OR_SAVEEXEC_B32;
    Exec = AMDGPU::EXEC_LO;
  } else {
    AndOpc = AMDGPU::S_AND_B64;
    OrOpc = AMDGPU::S_OR_B64;
    XorOpc = AMDGPU::S_XOR_B64;
    MovTermOpc = AMDGPU::S_MOV_B64_term;
    Andn2TermOpc = AMDGPU::S_ANDN2_B64_term;
    XorTermrOpc = AMDGPU::S_XOR_B64_term;
    OrTermrOpc = AMDGPU::S_OR_B64_term;
    OrSaveExecOpc = AMDGPU::S_OR_SAVEEXEC_B64;
    Exec = AMDGPU::EXEC;
  }

  // Compute set of blocks with kills
  const bool CanDemote =
      MF.getFunction().getCallingConv() == CallingConv::AMDGPU_PS;
  for (auto &MBB : MF) {
    bool IsKillBlock = false;
    for (auto &Term : MBB.terminators()) {
      if (TII->isKillTerminator(Term.getOpcode())) {
        KillBlocks.insert(&MBB);
        IsKillBlock = true;
        break;
      }
    }
    if (CanDemote && !IsKillBlock) {
      for (auto &MI : MBB) {
        if (MI.getOpcode() == AMDGPU::SI_DEMOTE_I1) {
          KillBlocks.insert(&MBB);
          break;
        }
      }
    }
  }

  bool Changed = false;
  MachineFunction::iterator NextBB;
  for (MachineFunction::iterator BI = MF.begin();
       BI != MF.end(); BI = NextBB) {
    NextBB = std::next(BI);
    MachineBasicBlock *MBB = &*BI;

    MachineBasicBlock::iterator I, E, Next;
    E = MBB->end();
    for (I = MBB->begin(); I != E; I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;
      MachineBasicBlock *SplitMBB = MBB;

      switch (MI.getOpcode()) {
      case AMDGPU::SI_IF:
      case AMDGPU::SI_ELSE:
      case AMDGPU::SI_IF_BREAK:
      case AMDGPU::SI_WATERFALL_LOOP:
      case AMDGPU::SI_LOOP:
      case AMDGPU::SI_END_CF:
        SplitMBB = process(MI);
        Changed = true;
        break;
      }

      if (SplitMBB != MBB) {
        MBB = Next->getParent();
        E = MBB->end();
      }
    }
  }

  optimizeEndCf();

  if (LIS) {
    for (Register Reg : RecomputeRegs) {
      LIS->removeInterval(Reg);
      LIS->createAndComputeVirtRegInterval(Reg);
    }
  }

  RecomputeRegs.clear();
  LoweredEndCf.clear();
  LoweredIf.clear();
  KillBlocks.clear();

  return Changed;
}
