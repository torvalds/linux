//===- SIFixSGPRCopies.cpp - Remove potential VGPR => SGPR copies ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Copies from VGPR to SGPR registers are illegal and the register coalescer
/// will sometimes generate these illegal copies in situations like this:
///
///  Register Class <vsrc> is the union of <vgpr> and <sgpr>
///
/// BB0:
///   %0 <sgpr> = SCALAR_INST
///   %1 <vsrc> = COPY %0 <sgpr>
///    ...
///    BRANCH %cond BB1, BB2
///  BB1:
///    %2 <vgpr> = VECTOR_INST
///    %3 <vsrc> = COPY %2 <vgpr>
///  BB2:
///    %4 <vsrc> = PHI %1 <vsrc>, <%bb.0>, %3 <vrsc>, <%bb.1>
///    %5 <vgpr> = VECTOR_INST %4 <vsrc>
///
///
/// The coalescer will begin at BB0 and eliminate its copy, then the resulting
/// code will look like this:
///
/// BB0:
///   %0 <sgpr> = SCALAR_INST
///    ...
///    BRANCH %cond BB1, BB2
/// BB1:
///   %2 <vgpr> = VECTOR_INST
///   %3 <vsrc> = COPY %2 <vgpr>
/// BB2:
///   %4 <sgpr> = PHI %0 <sgpr>, <%bb.0>, %3 <vsrc>, <%bb.1>
///   %5 <vgpr> = VECTOR_INST %4 <sgpr>
///
/// Now that the result of the PHI instruction is an SGPR, the register
/// allocator is now forced to constrain the register class of %3 to
/// <sgpr> so we end up with final code like this:
///
/// BB0:
///   %0 <sgpr> = SCALAR_INST
///    ...
///    BRANCH %cond BB1, BB2
/// BB1:
///   %2 <vgpr> = VECTOR_INST
///   %3 <sgpr> = COPY %2 <vgpr>
/// BB2:
///   %4 <sgpr> = PHI %0 <sgpr>, <%bb.0>, %3 <sgpr>, <%bb.1>
///   %5 <vgpr> = VECTOR_INST %4 <sgpr>
///
/// Now this code contains an illegal copy from a VGPR to an SGPR.
///
/// In order to avoid this problem, this pass searches for PHI instructions
/// which define a <vsrc> register and constrains its definition class to
/// <vgpr> if the user of the PHI's definition register is a vector instruction.
/// If the PHI's definition class is constrained to <vgpr> then the coalescer
/// will be unable to perform the COPY removal from the above example  which
/// ultimately led to the creation of an illegal COPY.
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/InitializePasses.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "si-fix-sgpr-copies"

static cl::opt<bool> EnableM0Merge(
  "amdgpu-enable-merge-m0",
  cl::desc("Merge and hoist M0 initializations"),
  cl::init(true));

namespace {

class V2SCopyInfo {
public:
  // VGPR to SGPR copy being processed
  MachineInstr *Copy;
  // All SALU instructions reachable from this copy in SSA graph
  SetVector<MachineInstr *> SChain;
  // Number of SGPR to VGPR copies that are used to put the SALU computation
  // results back to VALU.
  unsigned NumSVCopies;

  unsigned Score;
  // Actual count of v_readfirstlane_b32
  // which need to be inserted to keep SChain SALU
  unsigned NumReadfirstlanes;
  // Current score state. To speedup selection V2SCopyInfos for processing
  bool NeedToBeConvertedToVALU = false;
  // Unique ID. Used as a key for mapping to keep permanent order.
  unsigned ID;

  // Count of another VGPR to SGPR copies that contribute to the
  // current copy SChain
  unsigned SiblingPenalty = 0;
  SetVector<unsigned> Siblings;
  V2SCopyInfo() : Copy(nullptr), ID(0){};
  V2SCopyInfo(unsigned Id, MachineInstr *C, unsigned Width)
      : Copy(C), NumSVCopies(0), NumReadfirstlanes(Width / 32), ID(Id){};
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() {
    dbgs() << ID << " : " << *Copy << "\n\tS:" << SChain.size()
           << "\n\tSV:" << NumSVCopies << "\n\tSP: " << SiblingPenalty
           << "\nScore: " << Score << "\n";
  }
#endif
};

class SIFixSGPRCopies : public MachineFunctionPass {
  MachineDominatorTree *MDT;
  SmallVector<MachineInstr*, 4> SCCCopies;
  SmallVector<MachineInstr*, 4> RegSequences;
  SmallVector<MachineInstr*, 4> PHINodes;
  SmallVector<MachineInstr*, 4> S2VCopies;
  unsigned NextVGPRToSGPRCopyID = 0;
  MapVector<unsigned, V2SCopyInfo> V2SCopies;
  DenseMap<MachineInstr *, SetVector<unsigned>> SiblingPenalty;

public:
  static char ID;

  MachineRegisterInfo *MRI;
  const SIRegisterInfo *TRI;
  const SIInstrInfo *TII;

  SIFixSGPRCopies() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  void fixSCCCopies(MachineFunction &MF);
  void prepareRegSequenceAndPHIs(MachineFunction &MF);
  unsigned getNextVGPRToSGPRCopyId() { return ++NextVGPRToSGPRCopyID; }
  bool needToBeConvertedToVALU(V2SCopyInfo *I);
  void analyzeVGPRToSGPRCopy(MachineInstr *MI);
  void lowerVGPR2SGPRCopies(MachineFunction &MF);
  // Handles copies which source register is:
  // 1. Physical register
  // 2. AGPR
  // 3. Defined by the instruction the merely moves the immediate
  bool lowerSpecialCase(MachineInstr &MI, MachineBasicBlock::iterator &I);

  void processPHINode(MachineInstr &MI);

  // Check if MO is an immediate materialized into a VGPR, and if so replace it
  // with an SGPR immediate. The VGPR immediate is also deleted if it does not
  // have any other uses.
  bool tryMoveVGPRConstToSGPR(MachineOperand &MO, Register NewDst,
                              MachineBasicBlock *BlockToInsertTo,
                              MachineBasicBlock::iterator PointToInsertTo);

  StringRef getPassName() const override { return "SI Fix SGPR copies"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(SIFixSGPRCopies, DEBUG_TYPE,
                     "SI Fix SGPR copies", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(SIFixSGPRCopies, DEBUG_TYPE,
                     "SI Fix SGPR copies", false, false)

char SIFixSGPRCopies::ID = 0;

char &llvm::SIFixSGPRCopiesID = SIFixSGPRCopies::ID;

FunctionPass *llvm::createSIFixSGPRCopiesPass() {
  return new SIFixSGPRCopies();
}

static std::pair<const TargetRegisterClass *, const TargetRegisterClass *>
getCopyRegClasses(const MachineInstr &Copy,
                  const SIRegisterInfo &TRI,
                  const MachineRegisterInfo &MRI) {
  Register DstReg = Copy.getOperand(0).getReg();
  Register SrcReg = Copy.getOperand(1).getReg();

  const TargetRegisterClass *SrcRC = SrcReg.isVirtual()
                                         ? MRI.getRegClass(SrcReg)
                                         : TRI.getPhysRegBaseClass(SrcReg);

  // We don't really care about the subregister here.
  // SrcRC = TRI.getSubRegClass(SrcRC, Copy.getOperand(1).getSubReg());

  const TargetRegisterClass *DstRC = DstReg.isVirtual()
                                         ? MRI.getRegClass(DstReg)
                                         : TRI.getPhysRegBaseClass(DstReg);

  return std::pair(SrcRC, DstRC);
}

static bool isVGPRToSGPRCopy(const TargetRegisterClass *SrcRC,
                             const TargetRegisterClass *DstRC,
                             const SIRegisterInfo &TRI) {
  return SrcRC != &AMDGPU::VReg_1RegClass && TRI.isSGPRClass(DstRC) &&
         TRI.hasVectorRegisters(SrcRC);
}

static bool isSGPRToVGPRCopy(const TargetRegisterClass *SrcRC,
                             const TargetRegisterClass *DstRC,
                             const SIRegisterInfo &TRI) {
  return DstRC != &AMDGPU::VReg_1RegClass && TRI.isSGPRClass(SrcRC) &&
         TRI.hasVectorRegisters(DstRC);
}

static bool tryChangeVGPRtoSGPRinCopy(MachineInstr &MI,
                                      const SIRegisterInfo *TRI,
                                      const SIInstrInfo *TII) {
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  auto &Src = MI.getOperand(1);
  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = Src.getReg();
  if (!SrcReg.isVirtual() || !DstReg.isVirtual())
    return false;

  for (const auto &MO : MRI.reg_nodbg_operands(DstReg)) {
    const auto *UseMI = MO.getParent();
    if (UseMI == &MI)
      continue;
    if (MO.isDef() || UseMI->getParent() != MI.getParent() ||
        UseMI->getOpcode() <= TargetOpcode::GENERIC_OP_END)
      return false;

    unsigned OpIdx = MO.getOperandNo();
    if (OpIdx >= UseMI->getDesc().getNumOperands() ||
        !TII->isOperandLegal(*UseMI, OpIdx, &Src))
      return false;
  }
  // Change VGPR to SGPR destination.
  MRI.setRegClass(DstReg, TRI->getEquivalentSGPRClass(MRI.getRegClass(DstReg)));
  return true;
}

// Distribute an SGPR->VGPR copy of a REG_SEQUENCE into a VGPR REG_SEQUENCE.
//
// SGPRx = ...
// SGPRy = REG_SEQUENCE SGPRx, sub0 ...
// VGPRz = COPY SGPRy
//
// ==>
//
// VGPRx = COPY SGPRx
// VGPRz = REG_SEQUENCE VGPRx, sub0
//
// This exposes immediate folding opportunities when materializing 64-bit
// immediates.
static bool foldVGPRCopyIntoRegSequence(MachineInstr &MI,
                                        const SIRegisterInfo *TRI,
                                        const SIInstrInfo *TII,
                                        MachineRegisterInfo &MRI) {
  assert(MI.isRegSequence());

  Register DstReg = MI.getOperand(0).getReg();
  if (!TRI->isSGPRClass(MRI.getRegClass(DstReg)))
    return false;

  if (!MRI.hasOneUse(DstReg))
    return false;

  MachineInstr &CopyUse = *MRI.use_instr_begin(DstReg);
  if (!CopyUse.isCopy())
    return false;

  // It is illegal to have vreg inputs to a physreg defining reg_sequence.
  if (CopyUse.getOperand(0).getReg().isPhysical())
    return false;

  const TargetRegisterClass *SrcRC, *DstRC;
  std::tie(SrcRC, DstRC) = getCopyRegClasses(CopyUse, *TRI, MRI);

  if (!isSGPRToVGPRCopy(SrcRC, DstRC, *TRI))
    return false;

  if (tryChangeVGPRtoSGPRinCopy(CopyUse, TRI, TII))
    return true;

  // TODO: Could have multiple extracts?
  unsigned SubReg = CopyUse.getOperand(1).getSubReg();
  if (SubReg != AMDGPU::NoSubRegister)
    return false;

  MRI.setRegClass(DstReg, DstRC);

  // SGPRx = ...
  // SGPRy = REG_SEQUENCE SGPRx, sub0 ...
  // VGPRz = COPY SGPRy

  // =>
  // VGPRx = COPY SGPRx
  // VGPRz = REG_SEQUENCE VGPRx, sub0

  MI.getOperand(0).setReg(CopyUse.getOperand(0).getReg());
  bool IsAGPR = TRI->isAGPRClass(DstRC);

  for (unsigned I = 1, N = MI.getNumOperands(); I != N; I += 2) {
    const TargetRegisterClass *SrcRC =
        TRI->getRegClassForOperandReg(MRI, MI.getOperand(I));
    assert(TRI->isSGPRClass(SrcRC) &&
           "Expected SGPR REG_SEQUENCE to only have SGPR inputs");
    const TargetRegisterClass *NewSrcRC = TRI->getEquivalentVGPRClass(SrcRC);

    Register TmpReg = MRI.createVirtualRegister(NewSrcRC);

    BuildMI(*MI.getParent(), &MI, MI.getDebugLoc(), TII->get(AMDGPU::COPY),
            TmpReg)
        .add(MI.getOperand(I));

    if (IsAGPR) {
      const TargetRegisterClass *NewSrcRC = TRI->getEquivalentAGPRClass(SrcRC);
      Register TmpAReg = MRI.createVirtualRegister(NewSrcRC);
      unsigned Opc = NewSrcRC == &AMDGPU::AGPR_32RegClass ?
        AMDGPU::V_ACCVGPR_WRITE_B32_e64 : AMDGPU::COPY;
      BuildMI(*MI.getParent(), &MI, MI.getDebugLoc(), TII->get(Opc),
            TmpAReg)
        .addReg(TmpReg, RegState::Kill);
      TmpReg = TmpAReg;
    }

    MI.getOperand(I).setReg(TmpReg);
  }

  CopyUse.eraseFromParent();
  return true;
}

static bool isSafeToFoldImmIntoCopy(const MachineInstr *Copy,
                                    const MachineInstr *MoveImm,
                                    const SIInstrInfo *TII,
                                    unsigned &SMovOp,
                                    int64_t &Imm) {
  if (Copy->getOpcode() != AMDGPU::COPY)
    return false;

  if (!MoveImm->isMoveImmediate())
    return false;

  const MachineOperand *ImmOp =
      TII->getNamedOperand(*MoveImm, AMDGPU::OpName::src0);
  if (!ImmOp->isImm())
    return false;

  // FIXME: Handle copies with sub-regs.
  if (Copy->getOperand(1).getSubReg())
    return false;

  switch (MoveImm->getOpcode()) {
  default:
    return false;
  case AMDGPU::V_MOV_B32_e32:
    SMovOp = AMDGPU::S_MOV_B32;
    break;
  case AMDGPU::V_MOV_B64_PSEUDO:
    SMovOp = AMDGPU::S_MOV_B64_IMM_PSEUDO;
    break;
  }
  Imm = ImmOp->getImm();
  return true;
}

template <class UnaryPredicate>
bool searchPredecessors(const MachineBasicBlock *MBB,
                        const MachineBasicBlock *CutOff,
                        UnaryPredicate Predicate) {
  if (MBB == CutOff)
    return false;

  DenseSet<const MachineBasicBlock *> Visited;
  SmallVector<MachineBasicBlock *, 4> Worklist(MBB->predecessors());

  while (!Worklist.empty()) {
    MachineBasicBlock *MBB = Worklist.pop_back_val();

    if (!Visited.insert(MBB).second)
      continue;
    if (MBB == CutOff)
      continue;
    if (Predicate(MBB))
      return true;

    Worklist.append(MBB->pred_begin(), MBB->pred_end());
  }

  return false;
}

// Checks if there is potential path From instruction To instruction.
// If CutOff is specified and it sits in between of that path we ignore
// a higher portion of the path and report it is not reachable.
static bool isReachable(const MachineInstr *From,
                        const MachineInstr *To,
                        const MachineBasicBlock *CutOff,
                        MachineDominatorTree &MDT) {
  if (MDT.dominates(From, To))
    return true;

  const MachineBasicBlock *MBBFrom = From->getParent();
  const MachineBasicBlock *MBBTo = To->getParent();

  // Do predecessor search.
  // We should almost never get here since we do not usually produce M0 stores
  // other than -1.
  return searchPredecessors(MBBTo, CutOff, [MBBFrom]
           (const MachineBasicBlock *MBB) { return MBB == MBBFrom; });
}

// Return the first non-prologue instruction in the block.
static MachineBasicBlock::iterator
getFirstNonPrologue(MachineBasicBlock *MBB, const TargetInstrInfo *TII) {
  MachineBasicBlock::iterator I = MBB->getFirstNonPHI();
  while (I != MBB->end() && TII->isBasicBlockPrologue(*I))
    ++I;

  return I;
}

// Hoist and merge identical SGPR initializations into a common predecessor.
// This is intended to combine M0 initializations, but can work with any
// SGPR. A VGPR cannot be processed since we cannot guarantee vector
// executioon.
static bool hoistAndMergeSGPRInits(unsigned Reg,
                                   const MachineRegisterInfo &MRI,
                                   const TargetRegisterInfo *TRI,
                                   MachineDominatorTree &MDT,
                                   const TargetInstrInfo *TII) {
  // List of inits by immediate value.
  using InitListMap = std::map<unsigned, std::list<MachineInstr *>>;
  InitListMap Inits;
  // List of clobbering instructions.
  SmallVector<MachineInstr*, 8> Clobbers;
  // List of instructions marked for deletion.
  SmallSet<MachineInstr*, 8> MergedInstrs;

  bool Changed = false;

  for (auto &MI : MRI.def_instructions(Reg)) {
    MachineOperand *Imm = nullptr;
    for (auto &MO : MI.operands()) {
      if ((MO.isReg() && ((MO.isDef() && MO.getReg() != Reg) || !MO.isDef())) ||
          (!MO.isImm() && !MO.isReg()) || (MO.isImm() && Imm)) {
        Imm = nullptr;
        break;
      }
      if (MO.isImm())
        Imm = &MO;
    }
    if (Imm)
      Inits[Imm->getImm()].push_front(&MI);
    else
      Clobbers.push_back(&MI);
  }

  for (auto &Init : Inits) {
    auto &Defs = Init.second;

    for (auto I1 = Defs.begin(), E = Defs.end(); I1 != E; ) {
      MachineInstr *MI1 = *I1;

      for (auto I2 = std::next(I1); I2 != E; ) {
        MachineInstr *MI2 = *I2;

        // Check any possible interference
        auto interferes = [&](MachineBasicBlock::iterator From,
                              MachineBasicBlock::iterator To) -> bool {

          assert(MDT.dominates(&*To, &*From));

          auto interferes = [&MDT, From, To](MachineInstr* &Clobber) -> bool {
            const MachineBasicBlock *MBBFrom = From->getParent();
            const MachineBasicBlock *MBBTo = To->getParent();
            bool MayClobberFrom = isReachable(Clobber, &*From, MBBTo, MDT);
            bool MayClobberTo = isReachable(Clobber, &*To, MBBTo, MDT);
            if (!MayClobberFrom && !MayClobberTo)
              return false;
            if ((MayClobberFrom && !MayClobberTo) ||
                (!MayClobberFrom && MayClobberTo))
              return true;
            // Both can clobber, this is not an interference only if both are
            // dominated by Clobber and belong to the same block or if Clobber
            // properly dominates To, given that To >> From, so it dominates
            // both and located in a common dominator.
            return !((MBBFrom == MBBTo &&
                      MDT.dominates(Clobber, &*From) &&
                      MDT.dominates(Clobber, &*To)) ||
                     MDT.properlyDominates(Clobber->getParent(), MBBTo));
          };

          return (llvm::any_of(Clobbers, interferes)) ||
                 (llvm::any_of(Inits, [&](InitListMap::value_type &C) {
                    return C.first != Init.first &&
                           llvm::any_of(C.second, interferes);
                  }));
        };

        if (MDT.dominates(MI1, MI2)) {
          if (!interferes(MI2, MI1)) {
            LLVM_DEBUG(dbgs()
                       << "Erasing from "
                       << printMBBReference(*MI2->getParent()) << " " << *MI2);
            MergedInstrs.insert(MI2);
            Changed = true;
            ++I2;
            continue;
          }
        } else if (MDT.dominates(MI2, MI1)) {
          if (!interferes(MI1, MI2)) {
            LLVM_DEBUG(dbgs()
                       << "Erasing from "
                       << printMBBReference(*MI1->getParent()) << " " << *MI1);
            MergedInstrs.insert(MI1);
            Changed = true;
            ++I1;
            break;
          }
        } else {
          auto *MBB = MDT.findNearestCommonDominator(MI1->getParent(),
                                                     MI2->getParent());
          if (!MBB) {
            ++I2;
            continue;
          }

          MachineBasicBlock::iterator I = getFirstNonPrologue(MBB, TII);
          if (!interferes(MI1, I) && !interferes(MI2, I)) {
            LLVM_DEBUG(dbgs()
                       << "Erasing from "
                       << printMBBReference(*MI1->getParent()) << " " << *MI1
                       << "and moving from "
                       << printMBBReference(*MI2->getParent()) << " to "
                       << printMBBReference(*I->getParent()) << " " << *MI2);
            I->getParent()->splice(I, MI2->getParent(), MI2);
            MergedInstrs.insert(MI1);
            Changed = true;
            ++I1;
            break;
          }
        }
        ++I2;
      }
      ++I1;
    }
  }

  // Remove initializations that were merged into another.
  for (auto &Init : Inits) {
    auto &Defs = Init.second;
    auto I = Defs.begin();
    while (I != Defs.end()) {
      if (MergedInstrs.count(*I)) {
        (*I)->eraseFromParent();
        I = Defs.erase(I);
      } else
        ++I;
    }
  }

  // Try to schedule SGPR initializations as early as possible in the MBB.
  for (auto &Init : Inits) {
    auto &Defs = Init.second;
    for (auto *MI : Defs) {
      auto MBB = MI->getParent();
      MachineInstr &BoundaryMI = *getFirstNonPrologue(MBB, TII);
      MachineBasicBlock::reverse_iterator B(BoundaryMI);
      // Check if B should actually be a boundary. If not set the previous
      // instruction as the boundary instead.
      if (!TII->isBasicBlockPrologue(*B))
        B++;

      auto R = std::next(MI->getReverseIterator());
      const unsigned Threshold = 50;
      // Search until B or Threshold for a place to insert the initialization.
      for (unsigned I = 0; R != B && I < Threshold; ++R, ++I)
        if (R->readsRegister(Reg, TRI) || R->definesRegister(Reg, TRI) ||
            TII->isSchedulingBoundary(*R, MBB, *MBB->getParent()))
          break;

      // Move to directly after R.
      if (&*--R != MI)
        MBB->splice(*R, MBB, MI);
    }
  }

  if (Changed)
    MRI.clearKillFlags(Reg);

  return Changed;
}

bool SIFixSGPRCopies::runOnMachineFunction(MachineFunction &MF) {
  // Only need to run this in SelectionDAG path.
  if (MF.getProperties().hasProperty(
        MachineFunctionProperties::Property::Selected))
    return false;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  MRI = &MF.getRegInfo();
  TRI = ST.getRegisterInfo();
  TII = ST.getInstrInfo();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;
         ++I) {
      MachineInstr &MI = *I;

      switch (MI.getOpcode()) {
      default:
        continue;
      case AMDGPU::COPY:
      case AMDGPU::WQM:
      case AMDGPU::STRICT_WQM:
      case AMDGPU::SOFT_WQM:
      case AMDGPU::STRICT_WWM: {
        const TargetRegisterClass *SrcRC, *DstRC;
        std::tie(SrcRC, DstRC) = getCopyRegClasses(MI, *TRI, *MRI);

        if (isSGPRToVGPRCopy(SrcRC, DstRC, *TRI)) {
          // Since VGPR to SGPR copies affect VGPR to SGPR copy
          // score and, hence the lowering decision, let's try to get rid of
          // them as early as possible
          if (tryChangeVGPRtoSGPRinCopy(MI, TRI, TII))
            continue;

          // Collect those not changed to try them after VGPR to SGPR copies
          // lowering as there will be more opportunities.
          S2VCopies.push_back(&MI);
        }
        if (!isVGPRToSGPRCopy(SrcRC, DstRC, *TRI))
          continue;
        if (lowerSpecialCase(MI, I))
          continue;

        analyzeVGPRToSGPRCopy(&MI);

        break;
      }
      case AMDGPU::INSERT_SUBREG:
      case AMDGPU::PHI:
      case AMDGPU::REG_SEQUENCE: {
        if (TRI->isSGPRClass(TII->getOpRegClass(MI, 0))) {
          for (MachineOperand &MO : MI.operands()) {
            if (!MO.isReg() || !MO.getReg().isVirtual())
              continue;
            const TargetRegisterClass *SrcRC = MRI->getRegClass(MO.getReg());
            if (TRI->hasVectorRegisters(SrcRC)) {
              const TargetRegisterClass *DestRC =
                  TRI->getEquivalentSGPRClass(SrcRC);
              Register NewDst = MRI->createVirtualRegister(DestRC);
              MachineBasicBlock *BlockToInsertCopy =
                  MI.isPHI() ? MI.getOperand(MO.getOperandNo() + 1).getMBB()
                             : &MBB;
              MachineBasicBlock::iterator PointToInsertCopy =
                  MI.isPHI() ? BlockToInsertCopy->getFirstInstrTerminator() : I;

              if (!tryMoveVGPRConstToSGPR(MO, NewDst, BlockToInsertCopy,
                                          PointToInsertCopy)) {
                MachineInstr *NewCopy =
                    BuildMI(*BlockToInsertCopy, PointToInsertCopy,
                            PointToInsertCopy->getDebugLoc(),
                            TII->get(AMDGPU::COPY), NewDst)
                        .addReg(MO.getReg());
                MO.setReg(NewDst);
                analyzeVGPRToSGPRCopy(NewCopy);
              }
            }
          }
        }

        if (MI.isPHI())
          PHINodes.push_back(&MI);
        else if (MI.isRegSequence())
          RegSequences.push_back(&MI);

        break;
      }
      case AMDGPU::V_WRITELANE_B32: {
        // Some architectures allow more than one constant bus access without
        // SGPR restriction
        if (ST.getConstantBusLimit(MI.getOpcode()) != 1)
          break;

        // Writelane is special in that it can use SGPR and M0 (which would
        // normally count as using the constant bus twice - but in this case it
        // is allowed since the lane selector doesn't count as a use of the
        // constant bus). However, it is still required to abide by the 1 SGPR
        // rule. Apply a fix here as we might have multiple SGPRs after
        // legalizing VGPRs to SGPRs
        int Src0Idx =
            AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::src0);
        int Src1Idx =
            AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::src1);
        MachineOperand &Src0 = MI.getOperand(Src0Idx);
        MachineOperand &Src1 = MI.getOperand(Src1Idx);

        // Check to see if the instruction violates the 1 SGPR rule
        if ((Src0.isReg() && TRI->isSGPRReg(*MRI, Src0.getReg()) &&
             Src0.getReg() != AMDGPU::M0) &&
            (Src1.isReg() && TRI->isSGPRReg(*MRI, Src1.getReg()) &&
             Src1.getReg() != AMDGPU::M0)) {

          // Check for trivially easy constant prop into one of the operands
          // If this is the case then perform the operation now to resolve SGPR
          // issue. If we don't do that here we will always insert a mov to m0
          // that can't be resolved in later operand folding pass
          bool Resolved = false;
          for (MachineOperand *MO : {&Src0, &Src1}) {
            if (MO->getReg().isVirtual()) {
              MachineInstr *DefMI = MRI->getVRegDef(MO->getReg());
              if (DefMI && TII->isFoldableCopy(*DefMI)) {
                const MachineOperand &Def = DefMI->getOperand(0);
                if (Def.isReg() &&
                    MO->getReg() == Def.getReg() &&
                    MO->getSubReg() == Def.getSubReg()) {
                  const MachineOperand &Copied = DefMI->getOperand(1);
                  if (Copied.isImm() &&
                      TII->isInlineConstant(APInt(64, Copied.getImm(), true))) {
                    MO->ChangeToImmediate(Copied.getImm());
                    Resolved = true;
                    break;
                  }
                }
              }
            }
          }

          if (!Resolved) {
            // Haven't managed to resolve by replacing an SGPR with an immediate
            // Move src1 to be in M0
            BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                    TII->get(AMDGPU::COPY), AMDGPU::M0)
                .add(Src1);
            Src1.ChangeToRegister(AMDGPU::M0, false);
          }
        }
        break;
      }
      }
    }
  }

  lowerVGPR2SGPRCopies(MF);
  // Postprocessing
  fixSCCCopies(MF);
  for (auto MI : S2VCopies) {
    // Check if it is still valid
    if (MI->isCopy()) {
      const TargetRegisterClass *SrcRC, *DstRC;
      std::tie(SrcRC, DstRC) = getCopyRegClasses(*MI, *TRI, *MRI);
      if (isSGPRToVGPRCopy(SrcRC, DstRC, *TRI))
        tryChangeVGPRtoSGPRinCopy(*MI, TRI, TII);
    }
  }
  for (auto MI : RegSequences) {
    // Check if it is still valid
    if (MI->isRegSequence())
      foldVGPRCopyIntoRegSequence(*MI, TRI, TII, *MRI);
  }
  for (auto MI : PHINodes) {
    processPHINode(*MI);
  }
  if (MF.getTarget().getOptLevel() > CodeGenOptLevel::None && EnableM0Merge)
    hoistAndMergeSGPRInits(AMDGPU::M0, *MRI, TRI, *MDT, TII);

  SiblingPenalty.clear();
  V2SCopies.clear();
  SCCCopies.clear();
  RegSequences.clear();
  PHINodes.clear();
  S2VCopies.clear();

  return true;
}

void SIFixSGPRCopies::processPHINode(MachineInstr &MI) {
  bool AllAGPRUses = true;
  SetVector<const MachineInstr *> worklist;
  SmallSet<const MachineInstr *, 4> Visited;
  SetVector<MachineInstr *> PHIOperands;
  worklist.insert(&MI);
  Visited.insert(&MI);
  // HACK to make MIR tests with no uses happy
  bool HasUses = false;
  while (!worklist.empty()) {
    const MachineInstr *Instr = worklist.pop_back_val();
    Register Reg = Instr->getOperand(0).getReg();
    for (const auto &Use : MRI->use_operands(Reg)) {
      HasUses = true;
      const MachineInstr *UseMI = Use.getParent();
      AllAGPRUses &= (UseMI->isCopy() &&
                      TRI->isAGPR(*MRI, UseMI->getOperand(0).getReg())) ||
                     TRI->isAGPR(*MRI, Use.getReg());
      if (UseMI->isCopy() || UseMI->isRegSequence()) {
        if (Visited.insert(UseMI).second)
          worklist.insert(UseMI);

        continue;
      }
    }
  }

  Register PHIRes = MI.getOperand(0).getReg();
  const TargetRegisterClass *RC0 = MRI->getRegClass(PHIRes);
  if (HasUses && AllAGPRUses && !TRI->isAGPRClass(RC0)) {
    LLVM_DEBUG(dbgs() << "Moving PHI to AGPR: " << MI);
    MRI->setRegClass(PHIRes, TRI->getEquivalentAGPRClass(RC0));
    for (unsigned I = 1, N = MI.getNumOperands(); I != N; I += 2) {
      MachineInstr *DefMI = MRI->getVRegDef(MI.getOperand(I).getReg());
      if (DefMI && DefMI->isPHI())
        PHIOperands.insert(DefMI);
    }
  }

  if (TRI->isVectorRegister(*MRI, PHIRes) ||
       RC0 == &AMDGPU::VReg_1RegClass) {
    LLVM_DEBUG(dbgs() << "Legalizing PHI: " << MI);
    TII->legalizeOperands(MI, MDT);
  }

  // Propagate register class back to PHI operands which are PHI themselves.
  while (!PHIOperands.empty()) {
    processPHINode(*PHIOperands.pop_back_val());
  }
}

bool SIFixSGPRCopies::tryMoveVGPRConstToSGPR(
    MachineOperand &MaybeVGPRConstMO, Register DstReg,
    MachineBasicBlock *BlockToInsertTo,
    MachineBasicBlock::iterator PointToInsertTo) {

  MachineInstr *DefMI = MRI->getVRegDef(MaybeVGPRConstMO.getReg());
  if (!DefMI || !DefMI->isMoveImmediate())
    return false;

  MachineOperand *SrcConst = TII->getNamedOperand(*DefMI, AMDGPU::OpName::src0);
  if (SrcConst->isReg())
    return false;

  const TargetRegisterClass *SrcRC =
      MRI->getRegClass(MaybeVGPRConstMO.getReg());
  unsigned MoveSize = TRI->getRegSizeInBits(*SrcRC);
  unsigned MoveOp = MoveSize == 64 ? AMDGPU::S_MOV_B64 : AMDGPU::S_MOV_B32;
  BuildMI(*BlockToInsertTo, PointToInsertTo, PointToInsertTo->getDebugLoc(),
          TII->get(MoveOp), DstReg)
      .add(*SrcConst);
  if (MRI->hasOneUse(MaybeVGPRConstMO.getReg()))
    DefMI->eraseFromParent();
  MaybeVGPRConstMO.setReg(DstReg);
  return true;
}

bool SIFixSGPRCopies::lowerSpecialCase(MachineInstr &MI,
                                       MachineBasicBlock::iterator &I) {
  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  if (!DstReg.isVirtual()) {
    // If the destination register is a physical register there isn't
    // really much we can do to fix this.
    // Some special instructions use M0 as an input. Some even only use
    // the first lane. Insert a readfirstlane and hope for the best.
    if (DstReg == AMDGPU::M0 &&
        TRI->hasVectorRegisters(MRI->getRegClass(SrcReg))) {
      Register TmpReg =
          MRI->createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
              TII->get(AMDGPU::V_READFIRSTLANE_B32), TmpReg)
          .add(MI.getOperand(1));
      MI.getOperand(1).setReg(TmpReg);
    } else if (tryMoveVGPRConstToSGPR(MI.getOperand(1), DstReg, MI.getParent(),
                                      MI)) {
      I = std::next(I);
      MI.eraseFromParent();
    }
    return true;
  }
  if (!SrcReg.isVirtual() || TRI->isAGPR(*MRI, SrcReg)) {
    SIInstrWorklist worklist;
    worklist.insert(&MI);
    TII->moveToVALU(worklist, MDT);
    return true;
  }

  unsigned SMovOp;
  int64_t Imm;
  // If we are just copying an immediate, we can replace the copy with
  // s_mov_b32.
  if (isSafeToFoldImmIntoCopy(&MI, MRI->getVRegDef(SrcReg), TII, SMovOp, Imm)) {
    MI.getOperand(1).ChangeToImmediate(Imm);
    MI.addImplicitDefUseOperands(*MI.getParent()->getParent());
    MI.setDesc(TII->get(SMovOp));
    return true;
  }
  return false;
}

void SIFixSGPRCopies::analyzeVGPRToSGPRCopy(MachineInstr* MI) {
  Register DstReg = MI->getOperand(0).getReg();
  const TargetRegisterClass *DstRC = MRI->getRegClass(DstReg);

  V2SCopyInfo Info(getNextVGPRToSGPRCopyId(), MI,
                      TRI->getRegSizeInBits(*DstRC));
  SmallVector<MachineInstr *, 8> AnalysisWorklist;
  // Needed because the SSA is not a tree but a graph and may have
  // forks and joins. We should not then go same way twice.
  DenseSet<MachineInstr *> Visited;
  AnalysisWorklist.push_back(Info.Copy);
  while (!AnalysisWorklist.empty()) {

    MachineInstr *Inst = AnalysisWorklist.pop_back_val();

    if (!Visited.insert(Inst).second)
      continue;

    // Copies and REG_SEQUENCE do not contribute to the final assembly
    // So, skip them but take care of the SGPR to VGPR copies bookkeeping.
    if (Inst->isCopy() || Inst->isRegSequence()) {
      if (TRI->isVGPR(*MRI, Inst->getOperand(0).getReg())) {
        if (!Inst->isCopy() ||
            !tryChangeVGPRtoSGPRinCopy(*Inst, TRI, TII)) {
          Info.NumSVCopies++;
          continue;
        }
      }
    }

    SiblingPenalty[Inst].insert(Info.ID);

    SmallVector<MachineInstr *, 4> Users;
    if ((TII->isSALU(*Inst) && Inst->isCompare()) ||
        (Inst->isCopy() && Inst->getOperand(0).getReg() == AMDGPU::SCC)) {
      auto I = Inst->getIterator();
      auto E = Inst->getParent()->end();
      while (++I != E &&
             !I->findRegisterDefOperand(AMDGPU::SCC, /*TRI=*/nullptr)) {
        if (I->readsRegister(AMDGPU::SCC, /*TRI=*/nullptr))
          Users.push_back(&*I);
      }
    } else if (Inst->getNumExplicitDefs() != 0) {
      Register Reg = Inst->getOperand(0).getReg();
      if (TRI->isSGPRReg(*MRI, Reg) && !TII->isVALU(*Inst))
        for (auto &U : MRI->use_instructions(Reg))
          Users.push_back(&U);
    }
    for (auto U : Users) {
      if (TII->isSALU(*U))
        Info.SChain.insert(U);
      AnalysisWorklist.push_back(U);
    }
  }
  V2SCopies[Info.ID] = Info;
}

// The main function that computes the VGPR to SGPR copy score
// and determines copy further lowering way: v_readfirstlane_b32 or moveToVALU
bool SIFixSGPRCopies::needToBeConvertedToVALU(V2SCopyInfo *Info) {
  if (Info->SChain.empty()) {
    Info->Score = 0;
    return true;
  }
  Info->Siblings = SiblingPenalty[*llvm::max_element(
      Info->SChain, [&](MachineInstr *A, MachineInstr *B) -> bool {
        return SiblingPenalty[A].size() < SiblingPenalty[B].size();
      })];
  Info->Siblings.remove_if([&](unsigned ID) { return ID == Info->ID; });
  // The loop below computes the number of another VGPR to SGPR V2SCopies
  // which contribute to the current copy SALU chain. We assume that all the
  // V2SCopies with the same source virtual register will be squashed to one
  // by regalloc. Also we take care of the V2SCopies of the differnt subregs
  // of the same register.
  SmallSet<std::pair<Register, unsigned>, 4> SrcRegs;
  for (auto J : Info->Siblings) {
    auto InfoIt = V2SCopies.find(J);
    if (InfoIt != V2SCopies.end()) {
      MachineInstr *SiblingCopy = InfoIt->second.Copy;
      if (SiblingCopy->isImplicitDef())
        // the COPY has already been MoveToVALUed
        continue;

      SrcRegs.insert(std::pair(SiblingCopy->getOperand(1).getReg(),
                               SiblingCopy->getOperand(1).getSubReg()));
    }
  }
  Info->SiblingPenalty = SrcRegs.size();

  unsigned Penalty =
      Info->NumSVCopies + Info->SiblingPenalty + Info->NumReadfirstlanes;
  unsigned Profit = Info->SChain.size();
  Info->Score = Penalty > Profit ? 0 : Profit - Penalty;
  Info->NeedToBeConvertedToVALU = Info->Score < 3;
  return Info->NeedToBeConvertedToVALU;
}

void SIFixSGPRCopies::lowerVGPR2SGPRCopies(MachineFunction &MF) {

  SmallVector<unsigned, 8> LoweringWorklist;
  for (auto &C : V2SCopies) {
    if (needToBeConvertedToVALU(&C.second))
      LoweringWorklist.push_back(C.second.ID);
  }

  // Store all the V2S copy instructions that need to be moved to VALU
  // in the Copies worklist.
  SIInstrWorklist Copies;

  while (!LoweringWorklist.empty()) {
    unsigned CurID = LoweringWorklist.pop_back_val();
    auto CurInfoIt = V2SCopies.find(CurID);
    if (CurInfoIt != V2SCopies.end()) {
      V2SCopyInfo C = CurInfoIt->second;
      LLVM_DEBUG(dbgs() << "Processing ...\n"; C.dump());
      for (auto S : C.Siblings) {
        auto SibInfoIt = V2SCopies.find(S);
        if (SibInfoIt != V2SCopies.end()) {
          V2SCopyInfo &SI = SibInfoIt->second;
          LLVM_DEBUG(dbgs() << "Sibling:\n"; SI.dump());
          if (!SI.NeedToBeConvertedToVALU) {
            SI.SChain.set_subtract(C.SChain);
            if (needToBeConvertedToVALU(&SI))
              LoweringWorklist.push_back(SI.ID);
          }
          SI.Siblings.remove_if([&](unsigned ID) { return ID == C.ID; });
        }
      }
      LLVM_DEBUG(dbgs() << "V2S copy " << *C.Copy
                        << " is being turned to VALU\n");
      // TODO: MapVector::erase is inefficient. Do bulk removal with remove_if
      // instead.
      V2SCopies.erase(C.ID);
      Copies.insert(C.Copy);
    }
  }

  TII->moveToVALU(Copies, MDT);
  Copies.clear();

  // Now do actual lowering
  for (auto C : V2SCopies) {
    MachineInstr *MI = C.second.Copy;
    MachineBasicBlock *MBB = MI->getParent();
    // We decide to turn V2S copy to v_readfirstlane_b32
    // remove it from the V2SCopies and remove it from all its siblings
    LLVM_DEBUG(dbgs() << "V2S copy " << *MI
                      << " is being turned to v_readfirstlane_b32"
                      << " Score: " << C.second.Score << "\n");
    Register DstReg = MI->getOperand(0).getReg();
    Register SrcReg = MI->getOperand(1).getReg();
    unsigned SubReg = MI->getOperand(1).getSubReg();
    const TargetRegisterClass *SrcRC =
        TRI->getRegClassForOperandReg(*MRI, MI->getOperand(1));
    size_t SrcSize = TRI->getRegSizeInBits(*SrcRC);
    if (SrcSize == 16) {
      // HACK to handle possible 16bit VGPR source
      auto MIB = BuildMI(*MBB, MI, MI->getDebugLoc(),
                         TII->get(AMDGPU::V_READFIRSTLANE_B32), DstReg);
      MIB.addReg(SrcReg, 0, AMDGPU::NoSubRegister);
    } else if (SrcSize == 32) {
      auto MIB = BuildMI(*MBB, MI, MI->getDebugLoc(),
                         TII->get(AMDGPU::V_READFIRSTLANE_B32), DstReg);
      MIB.addReg(SrcReg, 0, SubReg);
    } else {
      auto Result = BuildMI(*MBB, MI, MI->getDebugLoc(),
                            TII->get(AMDGPU::REG_SEQUENCE), DstReg);
      int N = TRI->getRegSizeInBits(*SrcRC) / 32;
      for (int i = 0; i < N; i++) {
        Register PartialSrc = TII->buildExtractSubReg(
            Result, *MRI, MI->getOperand(1), SrcRC,
            TRI->getSubRegFromChannel(i), &AMDGPU::VGPR_32RegClass);
        Register PartialDst =
            MRI->createVirtualRegister(&AMDGPU::SReg_32RegClass);
        BuildMI(*MBB, *Result, Result->getDebugLoc(),
                TII->get(AMDGPU::V_READFIRSTLANE_B32), PartialDst)
            .addReg(PartialSrc);
        Result.addReg(PartialDst).addImm(TRI->getSubRegFromChannel(i));
      }
    }
    MI->eraseFromParent();
  }
}

void SIFixSGPRCopies::fixSCCCopies(MachineFunction &MF) {
  bool IsWave32 = MF.getSubtarget<GCNSubtarget>().isWave32();
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;
         ++I) {
      MachineInstr &MI = *I;
      // May already have been lowered.
      if (!MI.isCopy())
        continue;
      Register SrcReg = MI.getOperand(1).getReg();
      Register DstReg = MI.getOperand(0).getReg();
      if (SrcReg == AMDGPU::SCC) {
        Register SCCCopy = MRI->createVirtualRegister(
            TRI->getRegClass(AMDGPU::SReg_1_XEXECRegClassID));
        I = BuildMI(*MI.getParent(), std::next(MachineBasicBlock::iterator(MI)),
                    MI.getDebugLoc(),
                    TII->get(IsWave32 ? AMDGPU::S_CSELECT_B32
                                      : AMDGPU::S_CSELECT_B64),
                    SCCCopy)
                .addImm(-1)
                .addImm(0);
        I = BuildMI(*MI.getParent(), std::next(I), I->getDebugLoc(),
                    TII->get(AMDGPU::COPY), DstReg)
                .addReg(SCCCopy);
        MI.eraseFromParent();
        continue;
      }
      if (DstReg == AMDGPU::SCC) {
        unsigned Opcode = IsWave32 ? AMDGPU::S_AND_B32 : AMDGPU::S_AND_B64;
        Register Exec = IsWave32 ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
        Register Tmp = MRI->createVirtualRegister(TRI->getBoolRC());
        I = BuildMI(*MI.getParent(), std::next(MachineBasicBlock::iterator(MI)),
                    MI.getDebugLoc(), TII->get(Opcode))
                .addReg(Tmp, getDefRegState(true))
                .addReg(SrcReg)
                .addReg(Exec);
        MI.eraseFromParent();
      }
    }
  }
}
