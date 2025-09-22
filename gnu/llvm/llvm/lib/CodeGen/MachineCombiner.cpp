//===---- MachineCombiner.cpp - Instcombining on SSA form machine code ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The machine combiner pass uses machine trace metrics to ensure the combined
// instructions do not lengthen the critical path or the resource depth.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/LazyMachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineCombinerPattern.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSizeOpts.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "machine-combiner"

STATISTIC(NumInstCombined, "Number of machineinst combined");

static cl::opt<unsigned>
inc_threshold("machine-combiner-inc-threshold", cl::Hidden,
              cl::desc("Incremental depth computation will be used for basic "
                       "blocks with more instructions."), cl::init(500));

static cl::opt<bool> dump_intrs("machine-combiner-dump-subst-intrs", cl::Hidden,
                                cl::desc("Dump all substituted intrs"),
                                cl::init(false));

#ifdef EXPENSIVE_CHECKS
static cl::opt<bool> VerifyPatternOrder(
    "machine-combiner-verify-pattern-order", cl::Hidden,
    cl::desc(
        "Verify that the generated patterns are ordered by increasing latency"),
    cl::init(true));
#else
static cl::opt<bool> VerifyPatternOrder(
    "machine-combiner-verify-pattern-order", cl::Hidden,
    cl::desc(
        "Verify that the generated patterns are ordered by increasing latency"),
    cl::init(false));
#endif

namespace {
class MachineCombiner : public MachineFunctionPass {
  const TargetSubtargetInfo *STI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MCSchedModel SchedModel;
  MachineRegisterInfo *MRI = nullptr;
  MachineLoopInfo *MLI = nullptr; // Current MachineLoopInfo
  MachineTraceMetrics *Traces = nullptr;
  MachineTraceMetrics::Ensemble *TraceEnsemble = nullptr;
  MachineBlockFrequencyInfo *MBFI = nullptr;
  ProfileSummaryInfo *PSI = nullptr;
  RegisterClassInfo RegClassInfo;

  TargetSchedModel TSchedModel;

  /// True if optimizing for code size.
  bool OptSize = false;

public:
  static char ID;
  MachineCombiner() : MachineFunctionPass(ID) {
    initializeMachineCombinerPass(*PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "Machine InstCombiner"; }

private:
  bool combineInstructions(MachineBasicBlock *);
  MachineInstr *getOperandDef(const MachineOperand &MO);
  bool isTransientMI(const MachineInstr *MI);
  unsigned getDepth(SmallVectorImpl<MachineInstr *> &InsInstrs,
                    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg,
                    MachineTraceMetrics::Trace BlockTrace,
                    const MachineBasicBlock &MBB);
  unsigned getLatency(MachineInstr *Root, MachineInstr *NewRoot,
                      MachineTraceMetrics::Trace BlockTrace);
  bool improvesCriticalPathLen(MachineBasicBlock *MBB, MachineInstr *Root,
                               MachineTraceMetrics::Trace BlockTrace,
                               SmallVectorImpl<MachineInstr *> &InsInstrs,
                               SmallVectorImpl<MachineInstr *> &DelInstrs,
                               DenseMap<unsigned, unsigned> &InstrIdxForVirtReg,
                               unsigned Pattern, bool SlackIsAccurate);
  bool reduceRegisterPressure(MachineInstr &Root, MachineBasicBlock *MBB,
                              SmallVectorImpl<MachineInstr *> &InsInstrs,
                              SmallVectorImpl<MachineInstr *> &DelInstrs,
                              unsigned Pattern);
  bool preservesResourceLen(MachineBasicBlock *MBB,
                            MachineTraceMetrics::Trace BlockTrace,
                            SmallVectorImpl<MachineInstr *> &InsInstrs,
                            SmallVectorImpl<MachineInstr *> &DelInstrs);
  void instr2instrSC(SmallVectorImpl<MachineInstr *> &Instrs,
                     SmallVectorImpl<const MCSchedClassDesc *> &InstrsSC);
  std::pair<unsigned, unsigned>
  getLatenciesForInstrSequences(MachineInstr &MI,
                                SmallVectorImpl<MachineInstr *> &InsInstrs,
                                SmallVectorImpl<MachineInstr *> &DelInstrs,
                                MachineTraceMetrics::Trace BlockTrace);

  void verifyPatternOrder(MachineBasicBlock *MBB, MachineInstr &Root,
                          SmallVector<unsigned, 16> &Patterns);
  CombinerObjective getCombinerObjective(unsigned Pattern);
};
}

char MachineCombiner::ID = 0;
char &llvm::MachineCombinerID = MachineCombiner::ID;

INITIALIZE_PASS_BEGIN(MachineCombiner, DEBUG_TYPE,
                      "Machine InstCombiner", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineTraceMetrics)
INITIALIZE_PASS_END(MachineCombiner, DEBUG_TYPE, "Machine InstCombiner",
                    false, false)

void MachineCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addRequired<MachineTraceMetrics>();
  AU.addPreserved<MachineTraceMetrics>();
  AU.addRequired<LazyMachineBlockFrequencyInfoPass>();
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

MachineInstr *
MachineCombiner::getOperandDef(const MachineOperand &MO) {
  MachineInstr *DefInstr = nullptr;
  // We need a virtual register definition.
  if (MO.isReg() && MO.getReg().isVirtual())
    DefInstr = MRI->getUniqueVRegDef(MO.getReg());
  return DefInstr;
}

/// Return true if MI is unlikely to generate an actual target instruction.
bool MachineCombiner::isTransientMI(const MachineInstr *MI) {
  if (!MI->isCopy())
    return MI->isTransient();

  // If MI is a COPY, check if its src and dst registers can be coalesced.
  Register Dst = MI->getOperand(0).getReg();
  Register Src = MI->getOperand(1).getReg();

  if (!MI->isFullCopy()) {
    // If src RC contains super registers of dst RC, it can also be coalesced.
    if (MI->getOperand(0).getSubReg() || Src.isPhysical() || Dst.isPhysical())
      return false;

    auto SrcSub = MI->getOperand(1).getSubReg();
    auto SrcRC = MRI->getRegClass(Src);
    auto DstRC = MRI->getRegClass(Dst);
    return TRI->getMatchingSuperRegClass(SrcRC, DstRC, SrcSub) != nullptr;
  }

  if (Src.isPhysical() && Dst.isPhysical())
    return Src == Dst;

  if (Src.isVirtual() && Dst.isVirtual()) {
    auto SrcRC = MRI->getRegClass(Src);
    auto DstRC = MRI->getRegClass(Dst);
    return SrcRC->hasSuperClassEq(DstRC) || SrcRC->hasSubClassEq(DstRC);
  }

  if (Src.isVirtual())
    std::swap(Src, Dst);

  // Now Src is physical register, Dst is virtual register.
  auto DstRC = MRI->getRegClass(Dst);
  return DstRC->contains(Src);
}

/// Computes depth of instructions in vector \InsInstr.
///
/// \param InsInstrs is a vector of machine instructions
/// \param InstrIdxForVirtReg is a dense map of virtual register to index
/// of defining machine instruction in \p InsInstrs
/// \param BlockTrace is a trace of machine instructions
///
/// \returns Depth of last instruction in \InsInstrs ("NewRoot")
unsigned
MachineCombiner::getDepth(SmallVectorImpl<MachineInstr *> &InsInstrs,
                          DenseMap<unsigned, unsigned> &InstrIdxForVirtReg,
                          MachineTraceMetrics::Trace BlockTrace,
                          const MachineBasicBlock &MBB) {
  SmallVector<unsigned, 16> InstrDepth;
  // For each instruction in the new sequence compute the depth based on the
  // operands. Use the trace information when possible. For new operands which
  // are tracked in the InstrIdxForVirtReg map depth is looked up in InstrDepth
  for (auto *InstrPtr : InsInstrs) { // for each Use
    unsigned IDepth = 0;
    for (const MachineOperand &MO : InstrPtr->all_uses()) {
      // Check for virtual register operand.
      if (!MO.getReg().isVirtual())
        continue;
      unsigned DepthOp = 0;
      unsigned LatencyOp = 0;
      DenseMap<unsigned, unsigned>::iterator II =
          InstrIdxForVirtReg.find(MO.getReg());
      if (II != InstrIdxForVirtReg.end()) {
        // Operand is new virtual register not in trace
        assert(II->second < InstrDepth.size() && "Bad Index");
        MachineInstr *DefInstr = InsInstrs[II->second];
        assert(DefInstr &&
               "There must be a definition for a new virtual register");
        DepthOp = InstrDepth[II->second];
        int DefIdx =
            DefInstr->findRegisterDefOperandIdx(MO.getReg(), /*TRI=*/nullptr);
        int UseIdx =
            InstrPtr->findRegisterUseOperandIdx(MO.getReg(), /*TRI=*/nullptr);
        LatencyOp = TSchedModel.computeOperandLatency(DefInstr, DefIdx,
                                                      InstrPtr, UseIdx);
      } else {
        MachineInstr *DefInstr = getOperandDef(MO);
        if (DefInstr && (TII->getMachineCombinerTraceStrategy() !=
                             MachineTraceStrategy::TS_Local ||
                         DefInstr->getParent() == &MBB)) {
          DepthOp = BlockTrace.getInstrCycles(*DefInstr).Depth;
          if (!isTransientMI(DefInstr))
            LatencyOp = TSchedModel.computeOperandLatency(
                DefInstr,
                DefInstr->findRegisterDefOperandIdx(MO.getReg(),
                                                    /*TRI=*/nullptr),
                InstrPtr,
                InstrPtr->findRegisterUseOperandIdx(MO.getReg(),
                                                    /*TRI=*/nullptr));
        }
      }
      IDepth = std::max(IDepth, DepthOp + LatencyOp);
    }
    InstrDepth.push_back(IDepth);
  }
  unsigned NewRootIdx = InsInstrs.size() - 1;
  return InstrDepth[NewRootIdx];
}

/// Computes instruction latency as max of latency of defined operands.
///
/// \param Root is a machine instruction that could be replaced by NewRoot.
/// It is used to compute a more accurate latency information for NewRoot in
/// case there is a dependent instruction in the same trace (\p BlockTrace)
/// \param NewRoot is the instruction for which the latency is computed
/// \param BlockTrace is a trace of machine instructions
///
/// \returns Latency of \p NewRoot
unsigned MachineCombiner::getLatency(MachineInstr *Root, MachineInstr *NewRoot,
                                     MachineTraceMetrics::Trace BlockTrace) {
  // Check each definition in NewRoot and compute the latency
  unsigned NewRootLatency = 0;

  for (const MachineOperand &MO : NewRoot->all_defs()) {
    // Check for virtual register operand.
    if (!MO.getReg().isVirtual())
      continue;
    // Get the first instruction that uses MO
    MachineRegisterInfo::reg_iterator RI = MRI->reg_begin(MO.getReg());
    RI++;
    if (RI == MRI->reg_end())
      continue;
    MachineInstr *UseMO = RI->getParent();
    unsigned LatencyOp = 0;
    if (UseMO && BlockTrace.isDepInTrace(*Root, *UseMO)) {
      LatencyOp = TSchedModel.computeOperandLatency(
          NewRoot,
          NewRoot->findRegisterDefOperandIdx(MO.getReg(), /*TRI=*/nullptr),
          UseMO,
          UseMO->findRegisterUseOperandIdx(MO.getReg(), /*TRI=*/nullptr));
    } else {
      LatencyOp = TSchedModel.computeInstrLatency(NewRoot);
    }
    NewRootLatency = std::max(NewRootLatency, LatencyOp);
  }
  return NewRootLatency;
}

CombinerObjective MachineCombiner::getCombinerObjective(unsigned Pattern) {
  // TODO: If C++ ever gets a real enum class, make this part of the
  // MachineCombinerPattern class.
  switch (Pattern) {
  case MachineCombinerPattern::REASSOC_AX_BY:
  case MachineCombinerPattern::REASSOC_AX_YB:
  case MachineCombinerPattern::REASSOC_XA_BY:
  case MachineCombinerPattern::REASSOC_XA_YB:
    return CombinerObjective::MustReduceDepth;
  default:
    return TII->getCombinerObjective(Pattern);
  }
}

/// Estimate the latency of the new and original instruction sequence by summing
/// up the latencies of the inserted and deleted instructions. This assumes
/// that the inserted and deleted instructions are dependent instruction chains,
/// which might not hold in all cases.
std::pair<unsigned, unsigned> MachineCombiner::getLatenciesForInstrSequences(
    MachineInstr &MI, SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    MachineTraceMetrics::Trace BlockTrace) {
  assert(!InsInstrs.empty() && "Only support sequences that insert instrs.");
  unsigned NewRootLatency = 0;
  // NewRoot is the last instruction in the \p InsInstrs vector.
  MachineInstr *NewRoot = InsInstrs.back();
  for (unsigned i = 0; i < InsInstrs.size() - 1; i++)
    NewRootLatency += TSchedModel.computeInstrLatency(InsInstrs[i]);
  NewRootLatency += getLatency(&MI, NewRoot, BlockTrace);

  unsigned RootLatency = 0;
  for (auto *I : DelInstrs)
    RootLatency += TSchedModel.computeInstrLatency(I);

  return {NewRootLatency, RootLatency};
}

bool MachineCombiner::reduceRegisterPressure(
    MachineInstr &Root, MachineBasicBlock *MBB,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs, unsigned Pattern) {
  // FIXME: for now, we don't do any check for the register pressure patterns.
  // We treat them as always profitable. But we can do better if we make
  // RegPressureTracker class be aware of TIE attribute. Then we can get an
  // accurate compare of register pressure with DelInstrs or InsInstrs.
  return true;
}

/// The DAGCombine code sequence ends in MI (Machine Instruction) Root.
/// The new code sequence ends in MI NewRoot. A necessary condition for the new
/// sequence to replace the old sequence is that it cannot lengthen the critical
/// path. The definition of "improve" may be restricted by specifying that the
/// new path improves the data dependency chain (MustReduceDepth).
bool MachineCombiner::improvesCriticalPathLen(
    MachineBasicBlock *MBB, MachineInstr *Root,
    MachineTraceMetrics::Trace BlockTrace,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg, unsigned Pattern,
    bool SlackIsAccurate) {
  // Get depth and latency of NewRoot and Root.
  unsigned NewRootDepth =
      getDepth(InsInstrs, InstrIdxForVirtReg, BlockTrace, *MBB);
  unsigned RootDepth = BlockTrace.getInstrCycles(*Root).Depth;

  LLVM_DEBUG(dbgs() << "  Dependence data for " << *Root << "\tNewRootDepth: "
                    << NewRootDepth << "\tRootDepth: " << RootDepth);

  // For a transform such as reassociation, the cost equation is
  // conservatively calculated so that we must improve the depth (data
  // dependency cycles) in the critical path to proceed with the transform.
  // Being conservative also protects against inaccuracies in the underlying
  // machine trace metrics and CPU models.
  if (getCombinerObjective(Pattern) == CombinerObjective::MustReduceDepth) {
    LLVM_DEBUG(dbgs() << "\tIt MustReduceDepth ");
    LLVM_DEBUG(NewRootDepth < RootDepth
                   ? dbgs() << "\t  and it does it\n"
                   : dbgs() << "\t  but it does NOT do it\n");
    return NewRootDepth < RootDepth;
  }

  // A more flexible cost calculation for the critical path includes the slack
  // of the original code sequence. This may allow the transform to proceed
  // even if the instruction depths (data dependency cycles) become worse.

  // Account for the latency of the inserted and deleted instructions by
  unsigned NewRootLatency, RootLatency;
  if (TII->accumulateInstrSeqToRootLatency(*Root)) {
    std::tie(NewRootLatency, RootLatency) =
        getLatenciesForInstrSequences(*Root, InsInstrs, DelInstrs, BlockTrace);
  } else {
    NewRootLatency = TSchedModel.computeInstrLatency(InsInstrs.back());
    RootLatency = TSchedModel.computeInstrLatency(Root);
  }

  unsigned RootSlack = BlockTrace.getInstrSlack(*Root);
  unsigned NewCycleCount = NewRootDepth + NewRootLatency;
  unsigned OldCycleCount =
      RootDepth + RootLatency + (SlackIsAccurate ? RootSlack : 0);
  LLVM_DEBUG(dbgs() << "\n\tNewRootLatency: " << NewRootLatency
                    << "\tRootLatency: " << RootLatency << "\n\tRootSlack: "
                    << RootSlack << " SlackIsAccurate=" << SlackIsAccurate
                    << "\n\tNewRootDepth + NewRootLatency = " << NewCycleCount
                    << "\n\tRootDepth + RootLatency + RootSlack = "
                    << OldCycleCount;);
  LLVM_DEBUG(NewCycleCount <= OldCycleCount
                 ? dbgs() << "\n\t  It IMPROVES PathLen because"
                 : dbgs() << "\n\t  It DOES NOT improve PathLen because");
  LLVM_DEBUG(dbgs() << "\n\t\tNewCycleCount = " << NewCycleCount
                    << ", OldCycleCount = " << OldCycleCount << "\n");

  return NewCycleCount <= OldCycleCount;
}

/// helper routine to convert instructions into SC
void MachineCombiner::instr2instrSC(
    SmallVectorImpl<MachineInstr *> &Instrs,
    SmallVectorImpl<const MCSchedClassDesc *> &InstrsSC) {
  for (auto *InstrPtr : Instrs) {
    unsigned Opc = InstrPtr->getOpcode();
    unsigned Idx = TII->get(Opc).getSchedClass();
    const MCSchedClassDesc *SC = SchedModel.getSchedClassDesc(Idx);
    InstrsSC.push_back(SC);
  }
}

/// True when the new instructions do not increase resource length
bool MachineCombiner::preservesResourceLen(
    MachineBasicBlock *MBB, MachineTraceMetrics::Trace BlockTrace,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs) {
  if (!TSchedModel.hasInstrSchedModel())
    return true;

  // Compute current resource length

  //ArrayRef<const MachineBasicBlock *> MBBarr(MBB);
  SmallVector <const MachineBasicBlock *, 1> MBBarr;
  MBBarr.push_back(MBB);
  unsigned ResLenBeforeCombine = BlockTrace.getResourceLength(MBBarr);

  // Deal with SC rather than Instructions.
  SmallVector<const MCSchedClassDesc *, 16> InsInstrsSC;
  SmallVector<const MCSchedClassDesc *, 16> DelInstrsSC;

  instr2instrSC(InsInstrs, InsInstrsSC);
  instr2instrSC(DelInstrs, DelInstrsSC);

  ArrayRef<const MCSchedClassDesc *> MSCInsArr{InsInstrsSC};
  ArrayRef<const MCSchedClassDesc *> MSCDelArr{DelInstrsSC};

  // Compute new resource length.
  unsigned ResLenAfterCombine =
      BlockTrace.getResourceLength(MBBarr, MSCInsArr, MSCDelArr);

  LLVM_DEBUG(dbgs() << "\t\tResource length before replacement: "
                    << ResLenBeforeCombine
                    << " and after: " << ResLenAfterCombine << "\n";);
  LLVM_DEBUG(
      ResLenAfterCombine <=
      ResLenBeforeCombine + TII->getExtendResourceLenLimit()
          ? dbgs() << "\t\t  As result it IMPROVES/PRESERVES Resource Length\n"
          : dbgs() << "\t\t  As result it DOES NOT improve/preserve Resource "
                      "Length\n");

  return ResLenAfterCombine <=
         ResLenBeforeCombine + TII->getExtendResourceLenLimit();
}

/// Inserts InsInstrs and deletes DelInstrs. Incrementally updates instruction
/// depths if requested.
///
/// \param MBB basic block to insert instructions in
/// \param MI current machine instruction
/// \param InsInstrs new instructions to insert in \p MBB
/// \param DelInstrs instruction to delete from \p MBB
/// \param TraceEnsemble is a pointer to the machine trace information
/// \param RegUnits set of live registers, needed to compute instruction depths
/// \param TII is target instruction info, used to call target hook
/// \param Pattern is used to call target hook finalizeInsInstrs
/// \param IncrementalUpdate if true, compute instruction depths incrementally,
///                          otherwise invalidate the trace
static void
insertDeleteInstructions(MachineBasicBlock *MBB, MachineInstr &MI,
                         SmallVectorImpl<MachineInstr *> &InsInstrs,
                         SmallVectorImpl<MachineInstr *> &DelInstrs,
                         MachineTraceMetrics::Ensemble *TraceEnsemble,
                         SparseSet<LiveRegUnit> &RegUnits,
                         const TargetInstrInfo *TII, unsigned Pattern,
                         bool IncrementalUpdate) {
  // If we want to fix up some placeholder for some target, do it now.
  // We need this because in genAlternativeCodeSequence, we have not decided the
  // better pattern InsInstrs or DelInstrs, so we don't want generate some
  // sideeffect to the function. For example we need to delay the constant pool
  // entry creation here after InsInstrs is selected as better pattern.
  // Otherwise the constant pool entry created for InsInstrs will not be deleted
  // even if InsInstrs is not the better pattern.
  TII->finalizeInsInstrs(MI, Pattern, InsInstrs);

  for (auto *InstrPtr : InsInstrs)
    MBB->insert((MachineBasicBlock::iterator)&MI, InstrPtr);

  for (auto *InstrPtr : DelInstrs) {
    InstrPtr->eraseFromParent();
    // Erase all LiveRegs defined by the removed instruction
    for (auto *I = RegUnits.begin(); I != RegUnits.end();) {
      if (I->MI == InstrPtr)
        I = RegUnits.erase(I);
      else
        I++;
    }
  }

  if (IncrementalUpdate)
    for (auto *InstrPtr : InsInstrs)
      TraceEnsemble->updateDepth(MBB, *InstrPtr, RegUnits);
  else
    TraceEnsemble->invalidate(MBB);

  NumInstCombined++;
}

// Check that the difference between original and new latency is decreasing for
// later patterns. This helps to discover sub-optimal pattern orderings.
void MachineCombiner::verifyPatternOrder(MachineBasicBlock *MBB,
                                         MachineInstr &Root,
                                         SmallVector<unsigned, 16> &Patterns) {
  long PrevLatencyDiff = std::numeric_limits<long>::max();
  (void)PrevLatencyDiff; // Variable is used in assert only.
  for (auto P : Patterns) {
    SmallVector<MachineInstr *, 16> InsInstrs;
    SmallVector<MachineInstr *, 16> DelInstrs;
    DenseMap<unsigned, unsigned> InstrIdxForVirtReg;
    TII->genAlternativeCodeSequence(Root, P, InsInstrs, DelInstrs,
                                    InstrIdxForVirtReg);
    // Found pattern, but did not generate alternative sequence.
    // This can happen e.g. when an immediate could not be materialized
    // in a single instruction.
    if (InsInstrs.empty() || !TSchedModel.hasInstrSchedModelOrItineraries())
      continue;

    unsigned NewRootLatency, RootLatency;
    std::tie(NewRootLatency, RootLatency) = getLatenciesForInstrSequences(
        Root, InsInstrs, DelInstrs, TraceEnsemble->getTrace(MBB));
    long CurrentLatencyDiff = ((long)RootLatency) - ((long)NewRootLatency);
    assert(CurrentLatencyDiff <= PrevLatencyDiff &&
           "Current pattern is better than previous pattern.");
    PrevLatencyDiff = CurrentLatencyDiff;
  }
}

/// Substitute a slow code sequence with a faster one by
/// evaluating instruction combining pattern.
/// The prototype of such a pattern is MUl + ADD -> MADD. Performs instruction
/// combining based on machine trace metrics. Only combine a sequence of
/// instructions  when this neither lengthens the critical path nor increases
/// resource pressure. When optimizing for codesize always combine when the new
/// sequence is shorter.
bool MachineCombiner::combineInstructions(MachineBasicBlock *MBB) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Combining MBB " << MBB->getName() << "\n");

  bool IncrementalUpdate = false;
  auto BlockIter = MBB->begin();
  decltype(BlockIter) LastUpdate;
  // Check if the block is in a loop.
  const MachineLoop *ML = MLI->getLoopFor(MBB);
  if (!TraceEnsemble)
    TraceEnsemble = Traces->getEnsemble(TII->getMachineCombinerTraceStrategy());

  SparseSet<LiveRegUnit> RegUnits;
  RegUnits.setUniverse(TRI->getNumRegUnits());

  bool OptForSize = OptSize || llvm::shouldOptimizeForSize(MBB, PSI, MBFI);

  bool DoRegPressureReduce =
      TII->shouldReduceRegisterPressure(MBB, &RegClassInfo);

  while (BlockIter != MBB->end()) {
    auto &MI = *BlockIter++;
    SmallVector<unsigned, 16> Patterns;
    // The motivating example is:
    //
    //     MUL  Other        MUL_op1 MUL_op2  Other
    //      \    /               \      |    /
    //      ADD/SUB      =>        MADD/MSUB
    //      (=Root)                (=NewRoot)

    // The DAGCombine code always replaced MUL + ADD/SUB by MADD. While this is
    // usually beneficial for code size it unfortunately can hurt performance
    // when the ADD is on the critical path, but the MUL is not. With the
    // substitution the MUL becomes part of the critical path (in form of the
    // MADD) and can lengthen it on architectures where the MADD latency is
    // longer than the ADD latency.
    //
    // For each instruction we check if it can be the root of a combiner
    // pattern. Then for each pattern the new code sequence in form of MI is
    // generated and evaluated. When the efficiency criteria (don't lengthen
    // critical path, don't use more resources) is met the new sequence gets
    // hooked up into the basic block before the old sequence is removed.
    //
    // The algorithm does not try to evaluate all patterns and pick the best.
    // This is only an artificial restriction though. In practice there is
    // mostly one pattern, and getMachineCombinerPatterns() can order patterns
    // based on an internal cost heuristic. If
    // machine-combiner-verify-pattern-order is enabled, all patterns are
    // checked to ensure later patterns do not provide better latency savings.

    if (!TII->getMachineCombinerPatterns(MI, Patterns, DoRegPressureReduce))
      continue;

    if (VerifyPatternOrder)
      verifyPatternOrder(MBB, MI, Patterns);

    for (const auto P : Patterns) {
      SmallVector<MachineInstr *, 16> InsInstrs;
      SmallVector<MachineInstr *, 16> DelInstrs;
      DenseMap<unsigned, unsigned> InstrIdxForVirtReg;
      TII->genAlternativeCodeSequence(MI, P, InsInstrs, DelInstrs,
                                      InstrIdxForVirtReg);
      // Found pattern, but did not generate alternative sequence.
      // This can happen e.g. when an immediate could not be materialized
      // in a single instruction.
      if (InsInstrs.empty())
        continue;

      LLVM_DEBUG(if (dump_intrs) {
        dbgs() << "\tFor the Pattern (" << (int)P
               << ") these instructions could be removed\n";
        for (auto const *InstrPtr : DelInstrs)
          InstrPtr->print(dbgs(), /*IsStandalone*/false, /*SkipOpers*/false,
                          /*SkipDebugLoc*/false, /*AddNewLine*/true, TII);
        dbgs() << "\tThese instructions could replace the removed ones\n";
        for (auto const *InstrPtr : InsInstrs)
          InstrPtr->print(dbgs(), /*IsStandalone*/false, /*SkipOpers*/false,
                          /*SkipDebugLoc*/false, /*AddNewLine*/true, TII);
      });

      if (IncrementalUpdate && LastUpdate != BlockIter) {
        // Update depths since the last incremental update.
        TraceEnsemble->updateDepths(LastUpdate, BlockIter, RegUnits);
        LastUpdate = BlockIter;
      }

      if (DoRegPressureReduce &&
          getCombinerObjective(P) ==
              CombinerObjective::MustReduceRegisterPressure) {
        if (MBB->size() > inc_threshold) {
          // Use incremental depth updates for basic blocks above threshold
          IncrementalUpdate = true;
          LastUpdate = BlockIter;
        }
        if (reduceRegisterPressure(MI, MBB, InsInstrs, DelInstrs, P)) {
          // Replace DelInstrs with InsInstrs.
          insertDeleteInstructions(MBB, MI, InsInstrs, DelInstrs, TraceEnsemble,
                                   RegUnits, TII, P, IncrementalUpdate);
          Changed |= true;

          // Go back to previous instruction as it may have ILP reassociation
          // opportunity.
          BlockIter--;
          break;
        }
      }

      if (ML && TII->isThroughputPattern(P)) {
        LLVM_DEBUG(dbgs() << "\t Replacing due to throughput pattern in loop\n");
        insertDeleteInstructions(MBB, MI, InsInstrs, DelInstrs, TraceEnsemble,
                                 RegUnits, TII, P, IncrementalUpdate);
        // Eagerly stop after the first pattern fires.
        Changed = true;
        break;
      } else if (OptForSize && InsInstrs.size() < DelInstrs.size()) {
        LLVM_DEBUG(dbgs() << "\t Replacing due to OptForSize ("
                          << InsInstrs.size() << " < "
                          << DelInstrs.size() << ")\n");
        insertDeleteInstructions(MBB, MI, InsInstrs, DelInstrs, TraceEnsemble,
                                 RegUnits, TII, P, IncrementalUpdate);
        // Eagerly stop after the first pattern fires.
        Changed = true;
        break;
      } else {
        // For big basic blocks, we only compute the full trace the first time
        // we hit this. We do not invalidate the trace, but instead update the
        // instruction depths incrementally.
        // NOTE: Only the instruction depths up to MI are accurate. All other
        // trace information is not updated.
        MachineTraceMetrics::Trace BlockTrace = TraceEnsemble->getTrace(MBB);
        Traces->verifyAnalysis();
        if (improvesCriticalPathLen(MBB, &MI, BlockTrace, InsInstrs, DelInstrs,
                                    InstrIdxForVirtReg, P,
                                    !IncrementalUpdate) &&
            preservesResourceLen(MBB, BlockTrace, InsInstrs, DelInstrs)) {
          if (MBB->size() > inc_threshold) {
            // Use incremental depth updates for basic blocks above treshold
            IncrementalUpdate = true;
            LastUpdate = BlockIter;
          }

          insertDeleteInstructions(MBB, MI, InsInstrs, DelInstrs, TraceEnsemble,
                                   RegUnits, TII, P, IncrementalUpdate);

          // Eagerly stop after the first pattern fires.
          Changed = true;
          break;
        }
        // Cleanup instructions of the alternative code sequence. There is no
        // use for them.
        MachineFunction *MF = MBB->getParent();
        for (auto *InstrPtr : InsInstrs)
          MF->deleteMachineInstr(InstrPtr);
      }
      InstrIdxForVirtReg.clear();
    }
  }

  if (Changed && IncrementalUpdate)
    Traces->invalidate(MBB);
  return Changed;
}

bool MachineCombiner::runOnMachineFunction(MachineFunction &MF) {
  STI = &MF.getSubtarget();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  SchedModel = STI->getSchedModel();
  TSchedModel.init(STI);
  MRI = &MF.getRegInfo();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  Traces = &getAnalysis<MachineTraceMetrics>();
  PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  MBFI = (PSI && PSI->hasProfileSummary()) ?
         &getAnalysis<LazyMachineBlockFrequencyInfoPass>().getBFI() :
         nullptr;
  TraceEnsemble = nullptr;
  OptSize = MF.getFunction().hasOptSize();
  RegClassInfo.runOnMachineFunction(MF);

  LLVM_DEBUG(dbgs() << getPassName() << ": " << MF.getName() << '\n');
  if (!TII->useMachineCombiner()) {
    LLVM_DEBUG(
        dbgs()
        << "  Skipping pass: Target does not support machine combiner\n");
    return false;
  }

  bool Changed = false;

  // Try to combine instructions.
  for (auto &MBB : MF)
    Changed |= combineInstructions(&MBB);

  return Changed;
}
