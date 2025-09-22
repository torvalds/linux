//===- MachineLICM.cpp - Machine Loop Invariant Code Motion Pass ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs loop invariant code motion on machine instructions. We
// attempt to remove as much code from the body of a loop as possible.
//
// This pass is not intended to be a replacement or a complete alternative
// for the LLVM-IR-level LICM pass. It is only designed to hoist simple
// constructs that are not exposed before lowering and instruction selection.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "machinelicm"

static cl::opt<bool>
AvoidSpeculation("avoid-speculation",
                 cl::desc("MachineLICM should avoid speculation"),
                 cl::init(true), cl::Hidden);

static cl::opt<bool>
HoistCheapInsts("hoist-cheap-insts",
                cl::desc("MachineLICM should hoist even cheap instructions"),
                cl::init(false), cl::Hidden);

static cl::opt<bool>
HoistConstStores("hoist-const-stores",
                 cl::desc("Hoist invariant stores"),
                 cl::init(true), cl::Hidden);

static cl::opt<bool> HoistConstLoads("hoist-const-loads",
                                     cl::desc("Hoist invariant loads"),
                                     cl::init(true), cl::Hidden);

// The default threshold of 100 (i.e. if target block is 100 times hotter)
// is based on empirical data on a single target and is subject to tuning.
static cl::opt<unsigned>
BlockFrequencyRatioThreshold("block-freq-ratio-threshold",
                             cl::desc("Do not hoist instructions if target"
                             "block is N times hotter than the source."),
                             cl::init(100), cl::Hidden);

enum class UseBFI { None, PGO, All };

static cl::opt<UseBFI>
DisableHoistingToHotterBlocks("disable-hoisting-to-hotter-blocks",
                              cl::desc("Disable hoisting instructions to"
                              " hotter blocks"),
                              cl::init(UseBFI::PGO), cl::Hidden,
                              cl::values(clEnumValN(UseBFI::None, "none",
                              "disable the feature"),
                              clEnumValN(UseBFI::PGO, "pgo",
                              "enable the feature when using profile data"),
                              clEnumValN(UseBFI::All, "all",
                              "enable the feature with/wo profile data")));

STATISTIC(NumHoisted,
          "Number of machine instructions hoisted out of loops");
STATISTIC(NumLowRP,
          "Number of instructions hoisted in low reg pressure situation");
STATISTIC(NumHighLatency,
          "Number of high latency instructions hoisted");
STATISTIC(NumCSEed,
          "Number of hoisted machine instructions CSEed");
STATISTIC(NumPostRAHoisted,
          "Number of machine instructions hoisted out of loops post regalloc");
STATISTIC(NumStoreConst,
          "Number of stores of const phys reg hoisted out of loops");
STATISTIC(NumNotHoistedDueToHotness,
          "Number of instructions not hoisted due to block frequency");

namespace {
  enum HoistResult { NotHoisted = 1, Hoisted = 2, ErasedMI = 4 };

  class MachineLICMBase : public MachineFunctionPass {
    const TargetInstrInfo *TII = nullptr;
    const TargetLoweringBase *TLI = nullptr;
    const TargetRegisterInfo *TRI = nullptr;
    const MachineFrameInfo *MFI = nullptr;
    MachineRegisterInfo *MRI = nullptr;
    TargetSchedModel SchedModel;
    bool PreRegAlloc = false;
    bool HasProfileData = false;

    // Various analyses that we use...
    AliasAnalysis *AA = nullptr;               // Alias analysis info.
    MachineBlockFrequencyInfo *MBFI = nullptr; // Machine block frequncy info
    MachineLoopInfo *MLI = nullptr;            // Current MachineLoopInfo
    MachineDominatorTree *DT = nullptr; // Machine dominator tree for the cur loop

    // State that is updated as we process loops
    bool Changed = false;           // True if a loop is changed.
    bool FirstInLoop = false;       // True if it's the first LICM in the loop.

    // Holds information about whether it is allowed to move load instructions
    // out of the loop
    SmallDenseMap<MachineLoop *, bool> AllowedToHoistLoads;

    // Exit blocks of each Loop.
    DenseMap<MachineLoop *, SmallVector<MachineBasicBlock *, 8>> ExitBlockMap;

    bool isExitBlock(MachineLoop *CurLoop, const MachineBasicBlock *MBB) {
      if (ExitBlockMap.contains(CurLoop))
        return is_contained(ExitBlockMap[CurLoop], MBB);

      SmallVector<MachineBasicBlock *, 8> ExitBlocks;
      CurLoop->getExitBlocks(ExitBlocks);
      ExitBlockMap[CurLoop] = ExitBlocks;
      return is_contained(ExitBlocks, MBB);
    }

    // Track 'estimated' register pressure.
    SmallDenseSet<Register> RegSeen;
    SmallVector<unsigned, 8> RegPressure;

    // Register pressure "limit" per register pressure set. If the pressure
    // is higher than the limit, then it's considered high.
    SmallVector<unsigned, 8> RegLimit;

    // Register pressure on path leading from loop preheader to current BB.
    SmallVector<SmallVector<unsigned, 8>, 16> BackTrace;

    // For each opcode per preheader, keep a list of potential CSE instructions.
    DenseMap<MachineBasicBlock *,
             DenseMap<unsigned, std::vector<MachineInstr *>>>
        CSEMap;

    enum {
      SpeculateFalse   = 0,
      SpeculateTrue    = 1,
      SpeculateUnknown = 2
    };

    // If a MBB does not dominate loop exiting blocks then it may not safe
    // to hoist loads from this block.
    // Tri-state: 0 - false, 1 - true, 2 - unknown
    unsigned SpeculationState = SpeculateUnknown;

  public:
    MachineLICMBase(char &PassID, bool PreRegAlloc)
        : MachineFunctionPass(PassID), PreRegAlloc(PreRegAlloc) {}

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineLoopInfoWrapperPass>();
      if (DisableHoistingToHotterBlocks != UseBFI::None)
        AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addRequired<AAResultsWrapperPass>();
      AU.addPreserved<MachineLoopInfoWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    void releaseMemory() override {
      RegSeen.clear();
      RegPressure.clear();
      RegLimit.clear();
      BackTrace.clear();
      CSEMap.clear();
      ExitBlockMap.clear();
    }

  private:
    /// Keep track of information about hoisting candidates.
    struct CandidateInfo {
      MachineInstr *MI;
      unsigned      Def;
      int           FI;

      CandidateInfo(MachineInstr *mi, unsigned def, int fi)
        : MI(mi), Def(def), FI(fi) {}
    };

    void HoistRegionPostRA(MachineLoop *CurLoop,
                           MachineBasicBlock *CurPreheader);

    void HoistPostRA(MachineInstr *MI, unsigned Def, MachineLoop *CurLoop,
                     MachineBasicBlock *CurPreheader);

    void ProcessMI(MachineInstr *MI, BitVector &RUDefs, BitVector &RUClobbers,
                   SmallDenseSet<int> &StoredFIs,
                   SmallVectorImpl<CandidateInfo> &Candidates,
                   MachineLoop *CurLoop);

    void AddToLiveIns(MCRegister Reg, MachineLoop *CurLoop);

    bool IsLICMCandidate(MachineInstr &I, MachineLoop *CurLoop);

    bool IsLoopInvariantInst(MachineInstr &I, MachineLoop *CurLoop);

    bool HasLoopPHIUse(const MachineInstr *MI, MachineLoop *CurLoop);

    bool HasHighOperandLatency(MachineInstr &MI, unsigned DefIdx, Register Reg,
                               MachineLoop *CurLoop) const;

    bool IsCheapInstruction(MachineInstr &MI) const;

    bool CanCauseHighRegPressure(const DenseMap<unsigned, int> &Cost,
                                 bool Cheap);

    void UpdateBackTraceRegPressure(const MachineInstr *MI);

    bool IsProfitableToHoist(MachineInstr &MI, MachineLoop *CurLoop);

    bool IsGuaranteedToExecute(MachineBasicBlock *BB, MachineLoop *CurLoop);

    bool isTriviallyReMaterializable(const MachineInstr &MI) const;

    void EnterScope(MachineBasicBlock *MBB);

    void ExitScope(MachineBasicBlock *MBB);

    void ExitScopeIfDone(
        MachineDomTreeNode *Node,
        DenseMap<MachineDomTreeNode *, unsigned> &OpenChildren,
        const DenseMap<MachineDomTreeNode *, MachineDomTreeNode *> &ParentMap);

    void HoistOutOfLoop(MachineDomTreeNode *HeaderN, MachineLoop *CurLoop,
                        MachineBasicBlock *CurPreheader);

    void InitRegPressure(MachineBasicBlock *BB);

    DenseMap<unsigned, int> calcRegisterCost(const MachineInstr *MI,
                                             bool ConsiderSeen,
                                             bool ConsiderUnseenAsDef);

    void UpdateRegPressure(const MachineInstr *MI,
                           bool ConsiderUnseenAsDef = false);

    MachineInstr *ExtractHoistableLoad(MachineInstr *MI, MachineLoop *CurLoop);

    MachineInstr *LookForDuplicate(const MachineInstr *MI,
                                   std::vector<MachineInstr *> &PrevMIs);

    bool
    EliminateCSE(MachineInstr *MI,
                 DenseMap<unsigned, std::vector<MachineInstr *>>::iterator &CI);

    bool MayCSE(MachineInstr *MI);

    unsigned Hoist(MachineInstr *MI, MachineBasicBlock *Preheader,
                   MachineLoop *CurLoop);

    void InitCSEMap(MachineBasicBlock *BB);

    void InitializeLoadsHoistableLoops();

    bool isTgtHotterThanSrc(MachineBasicBlock *SrcBlock,
                            MachineBasicBlock *TgtBlock);
    MachineBasicBlock *getCurPreheader(MachineLoop *CurLoop,
                                       MachineBasicBlock *CurPreheader);
  };

  class MachineLICM : public MachineLICMBase {
  public:
    static char ID;
    MachineLICM() : MachineLICMBase(ID, false) {
      initializeMachineLICMPass(*PassRegistry::getPassRegistry());
    }
  };

  class EarlyMachineLICM : public MachineLICMBase {
  public:
    static char ID;
    EarlyMachineLICM() : MachineLICMBase(ID, true) {
      initializeEarlyMachineLICMPass(*PassRegistry::getPassRegistry());
    }
  };

} // end anonymous namespace

char MachineLICM::ID;
char EarlyMachineLICM::ID;

char &llvm::MachineLICMID = MachineLICM::ID;
char &llvm::EarlyMachineLICMID = EarlyMachineLICM::ID;

INITIALIZE_PASS_BEGIN(MachineLICM, DEBUG_TYPE,
                      "Machine Loop Invariant Code Motion", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MachineLICM, DEBUG_TYPE,
                    "Machine Loop Invariant Code Motion", false, false)

INITIALIZE_PASS_BEGIN(EarlyMachineLICM, "early-machinelicm",
                      "Early Machine Loop Invariant Code Motion", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(EarlyMachineLICM, "early-machinelicm",
                    "Early Machine Loop Invariant Code Motion", false, false)

bool MachineLICMBase::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  Changed = FirstInLoop = false;
  const TargetSubtargetInfo &ST = MF.getSubtarget();
  TII = ST.getInstrInfo();
  TLI = ST.getTargetLowering();
  TRI = ST.getRegisterInfo();
  MFI = &MF.getFrameInfo();
  MRI = &MF.getRegInfo();
  SchedModel.init(&ST);

  PreRegAlloc = MRI->isSSA();
  HasProfileData = MF.getFunction().hasProfileData();

  if (PreRegAlloc)
    LLVM_DEBUG(dbgs() << "******** Pre-regalloc Machine LICM: ");
  else
    LLVM_DEBUG(dbgs() << "******** Post-regalloc Machine LICM: ");
  LLVM_DEBUG(dbgs() << MF.getName() << " ********\n");

  if (PreRegAlloc) {
    // Estimate register pressure during pre-regalloc pass.
    unsigned NumRPS = TRI->getNumRegPressureSets();
    RegPressure.resize(NumRPS);
    std::fill(RegPressure.begin(), RegPressure.end(), 0);
    RegLimit.resize(NumRPS);
    for (unsigned i = 0, e = NumRPS; i != e; ++i)
      RegLimit[i] = TRI->getRegPressureSetLimit(MF, i);
  }

  // Get our Loop information...
  if (DisableHoistingToHotterBlocks != UseBFI::None)
    MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  DT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  if (HoistConstLoads)
    InitializeLoadsHoistableLoops();

  SmallVector<MachineLoop *, 8> Worklist(MLI->begin(), MLI->end());
  while (!Worklist.empty()) {
    MachineLoop *CurLoop = Worklist.pop_back_val();
    MachineBasicBlock *CurPreheader = nullptr;

    if (!PreRegAlloc)
      HoistRegionPostRA(CurLoop, CurPreheader);
    else {
      // CSEMap is initialized for loop header when the first instruction is
      // being hoisted.
      MachineDomTreeNode *N = DT->getNode(CurLoop->getHeader());
      FirstInLoop = true;
      HoistOutOfLoop(N, CurLoop, CurPreheader);
      CSEMap.clear();
    }
  }

  return Changed;
}

/// Return true if instruction stores to the specified frame.
static bool InstructionStoresToFI(const MachineInstr *MI, int FI) {
  // Check mayStore before memory operands so that e.g. DBG_VALUEs will return
  // true since they have no memory operands.
  if (!MI->mayStore())
     return false;
  // If we lost memory operands, conservatively assume that the instruction
  // writes to all slots.
  if (MI->memoperands_empty())
    return true;
  for (const MachineMemOperand *MemOp : MI->memoperands()) {
    if (!MemOp->isStore() || !MemOp->getPseudoValue())
      continue;
    if (const FixedStackPseudoSourceValue *Value =
        dyn_cast<FixedStackPseudoSourceValue>(MemOp->getPseudoValue())) {
      if (Value->getFrameIndex() == FI)
        return true;
    }
  }
  return false;
}

static void applyBitsNotInRegMaskToRegUnitsMask(const TargetRegisterInfo &TRI,
                                                BitVector &RUs,
                                                const uint32_t *Mask) {
  // FIXME: This intentionally works in reverse due to some issues with the
  // Register Units infrastructure.
  //
  // This is used to apply callee-saved-register masks to the clobbered regunits
  // mask.
  //
  // The right way to approach this is to start with a BitVector full of ones,
  // then reset all the bits of the regunits of each register that is set in the
  // mask (registers preserved), then OR the resulting bits with the Clobbers
  // mask. This correctly prioritizes the saved registers, so if a RU is shared
  // between a register that is preserved, and one that is NOT preserved, that
  // RU will not be set in the output vector (the clobbers).
  //
  // What we have to do for now is the opposite: we have to assume that the
  // regunits of all registers that are NOT preserved are clobbered, even if
  // those regunits are preserved by another register. So if a RU is shared
  // like described previously, that RU will be set.
  //
  // This is to work around an issue which appears in AArch64, but isn't
  // exclusive to that target: AArch64's Qn registers (128 bits) have Dn
  // register (lower 64 bits). A few Dn registers are preserved by some calling
  // conventions, but Qn and Dn share exactly the same reg units.
  //
  // If we do this the right way, Qn will be marked as NOT clobbered even though
  // its upper 64 bits are NOT preserved. The conservative approach handles this
  // correctly at the cost of some missed optimizations on other targets.
  //
  // This is caused by how RegUnits are handled within TableGen. Ideally, Qn
  // should have an extra RegUnit to model the "unknown" bits not covered by the
  // subregs.
  BitVector RUsFromRegsNotInMask(TRI.getNumRegUnits());
  const unsigned NumRegs = TRI.getNumRegs();
  const unsigned MaskWords = (NumRegs + 31) / 32;
  for (unsigned K = 0; K < MaskWords; ++K) {
    const uint32_t Word = Mask[K];
    for (unsigned Bit = 0; Bit < 32; ++Bit) {
      const unsigned PhysReg = (K * 32) + Bit;
      if (PhysReg == NumRegs)
        break;

      if (PhysReg && !((Word >> Bit) & 1)) {
        for (MCRegUnitIterator RUI(PhysReg, &TRI); RUI.isValid(); ++RUI)
          RUsFromRegsNotInMask.set(*RUI);
      }
    }
  }

  RUs |= RUsFromRegsNotInMask;
}

/// Examine the instruction for potentai LICM candidate. Also
/// gather register def and frame object update information.
void MachineLICMBase::ProcessMI(MachineInstr *MI, BitVector &RUDefs,
                                BitVector &RUClobbers,
                                SmallDenseSet<int> &StoredFIs,
                                SmallVectorImpl<CandidateInfo> &Candidates,
                                MachineLoop *CurLoop) {
  bool RuledOut = false;
  bool HasNonInvariantUse = false;
  unsigned Def = 0;
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isFI()) {
      // Remember if the instruction stores to the frame index.
      int FI = MO.getIndex();
      if (!StoredFIs.count(FI) &&
          MFI->isSpillSlotObjectIndex(FI) &&
          InstructionStoresToFI(MI, FI))
        StoredFIs.insert(FI);
      HasNonInvariantUse = true;
      continue;
    }

    // We can't hoist an instruction defining a physreg that is clobbered in
    // the loop.
    if (MO.isRegMask()) {
      applyBitsNotInRegMaskToRegUnitsMask(*TRI, RUClobbers, MO.getRegMask());
      continue;
    }

    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    assert(Reg.isPhysical() && "Not expecting virtual register!");

    if (!MO.isDef()) {
      if (!HasNonInvariantUse) {
        for (MCRegUnitIterator RUI(Reg, TRI); RUI.isValid(); ++RUI) {
          // If it's using a non-loop-invariant register, then it's obviously
          // not safe to hoist.
          if (RUDefs.test(*RUI) || RUClobbers.test(*RUI)) {
            HasNonInvariantUse = true;
            break;
          }
        }
      }
      continue;
    }

    if (MO.isImplicit()) {
      for (MCRegUnitIterator RUI(Reg, TRI); RUI.isValid(); ++RUI)
        RUClobbers.set(*RUI);
      if (!MO.isDead())
        // Non-dead implicit def? This cannot be hoisted.
        RuledOut = true;
      // No need to check if a dead implicit def is also defined by
      // another instruction.
      continue;
    }

    // FIXME: For now, avoid instructions with multiple defs, unless
    // it's a dead implicit def.
    if (Def)
      RuledOut = true;
    else
      Def = Reg;

    // If we have already seen another instruction that defines the same
    // register, then this is not safe.  Two defs is indicated by setting a
    // PhysRegClobbers bit.
    for (MCRegUnitIterator RUI(Reg, TRI); RUI.isValid(); ++RUI) {
      if (RUDefs.test(*RUI)) {
        RUClobbers.set(*RUI);
        RuledOut = true;
      } else if (RUClobbers.test(*RUI)) {
        // MI defined register is seen defined by another instruction in
        // the loop, it cannot be a LICM candidate.
        RuledOut = true;
      }

      RUDefs.set(*RUI);
    }
  }

  // Only consider reloads for now and remats which do not have register
  // operands. FIXME: Consider unfold load folding instructions.
  if (Def && !RuledOut) {
    int FI = std::numeric_limits<int>::min();
    if ((!HasNonInvariantUse && IsLICMCandidate(*MI, CurLoop)) ||
        (TII->isLoadFromStackSlot(*MI, FI) && MFI->isSpillSlotObjectIndex(FI)))
      Candidates.push_back(CandidateInfo(MI, Def, FI));
  }
}

/// Walk the specified region of the CFG and hoist loop invariants out to the
/// preheader.
void MachineLICMBase::HoistRegionPostRA(MachineLoop *CurLoop,
                                        MachineBasicBlock *CurPreheader) {
  MachineBasicBlock *Preheader = getCurPreheader(CurLoop, CurPreheader);
  if (!Preheader)
    return;

  unsigned NumRegUnits = TRI->getNumRegUnits();
  BitVector RUDefs(NumRegUnits);     // RUs defined once in the loop.
  BitVector RUClobbers(NumRegUnits); // RUs defined more than once.

  SmallVector<CandidateInfo, 32> Candidates;
  SmallDenseSet<int> StoredFIs;

  // Walk the entire region, count number of defs for each register, and
  // collect potential LICM candidates.
  for (MachineBasicBlock *BB : CurLoop->getBlocks()) {
    // If the header of the loop containing this basic block is a landing pad,
    // then don't try to hoist instructions out of this loop.
    const MachineLoop *ML = MLI->getLoopFor(BB);
    if (ML && ML->getHeader()->isEHPad()) continue;

    // Conservatively treat live-in's as an external def.
    // FIXME: That means a reload that're reused in successor block(s) will not
    // be LICM'ed.
    for (const auto &LI : BB->liveins()) {
      for (MCRegUnitIterator RUI(LI.PhysReg, TRI); RUI.isValid(); ++RUI)
        RUDefs.set(*RUI);
    }

    // Funclet entry blocks will clobber all registers
    if (const uint32_t *Mask = BB->getBeginClobberMask(TRI))
      applyBitsNotInRegMaskToRegUnitsMask(*TRI, RUClobbers, Mask);

    SpeculationState = SpeculateUnknown;
    for (MachineInstr &MI : *BB)
      ProcessMI(&MI, RUDefs, RUClobbers, StoredFIs, Candidates, CurLoop);
  }

  // Gather the registers read / clobbered by the terminator.
  BitVector TermRUs(NumRegUnits);
  MachineBasicBlock::iterator TI = Preheader->getFirstTerminator();
  if (TI != Preheader->end()) {
    for (const MachineOperand &MO : TI->operands()) {
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (!Reg)
        continue;
      for (MCRegUnitIterator RUI(Reg, TRI); RUI.isValid(); ++RUI)
        TermRUs.set(*RUI);
    }
  }

  // Now evaluate whether the potential candidates qualify.
  // 1. Check if the candidate defined register is defined by another
  //    instruction in the loop.
  // 2. If the candidate is a load from stack slot (always true for now),
  //    check if the slot is stored anywhere in the loop.
  // 3. Make sure candidate def should not clobber
  //    registers read by the terminator. Similarly its def should not be
  //    clobbered by the terminator.
  for (CandidateInfo &Candidate : Candidates) {
    if (Candidate.FI != std::numeric_limits<int>::min() &&
        StoredFIs.count(Candidate.FI))
      continue;

    unsigned Def = Candidate.Def;
    bool Safe = true;
    for (MCRegUnitIterator RUI(Def, TRI); RUI.isValid(); ++RUI) {
      if (RUClobbers.test(*RUI) || TermRUs.test(*RUI)) {
        Safe = false;
        break;
      }
    }

    if (!Safe)
      continue;

    MachineInstr *MI = Candidate.MI;
    for (const MachineOperand &MO : MI->all_uses()) {
      if (!MO.getReg())
        continue;
      for (MCRegUnitIterator RUI(MO.getReg(), TRI); RUI.isValid(); ++RUI) {
        if (RUDefs.test(*RUI) || RUClobbers.test(*RUI)) {
          // If it's using a non-loop-invariant register, then it's obviously
          // not safe to hoist.
          Safe = false;
          break;
        }
      }

      if (!Safe)
        break;
    }

    if (Safe)
      HoistPostRA(MI, Candidate.Def, CurLoop, CurPreheader);
  }
}

/// Add register 'Reg' to the livein sets of BBs in the current loop, and make
/// sure it is not killed by any instructions in the loop.
void MachineLICMBase::AddToLiveIns(MCRegister Reg, MachineLoop *CurLoop) {
  for (MachineBasicBlock *BB : CurLoop->getBlocks()) {
    if (!BB->isLiveIn(Reg))
      BB->addLiveIn(Reg);
    for (MachineInstr &MI : *BB) {
      for (MachineOperand &MO : MI.all_uses()) {
        if (!MO.getReg())
          continue;
        if (TRI->regsOverlap(Reg, MO.getReg()))
          MO.setIsKill(false);
      }
    }
  }
}

/// When an instruction is found to only use loop invariant operands that is
/// safe to hoist, this instruction is called to do the dirty work.
void MachineLICMBase::HoistPostRA(MachineInstr *MI, unsigned Def,
                                  MachineLoop *CurLoop,
                                  MachineBasicBlock *CurPreheader) {
  MachineBasicBlock *Preheader = getCurPreheader(CurLoop, CurPreheader);

  // Now move the instructions to the predecessor, inserting it before any
  // terminator instructions.
  LLVM_DEBUG(dbgs() << "Hoisting to " << printMBBReference(*Preheader)
                    << " from " << printMBBReference(*MI->getParent()) << ": "
                    << *MI);

  // Splice the instruction to the preheader.
  MachineBasicBlock *MBB = MI->getParent();
  Preheader->splice(Preheader->getFirstTerminator(), MBB, MI);

  // Since we are moving the instruction out of its basic block, we do not
  // retain its debug location. Doing so would degrade the debugging
  // experience and adversely affect the accuracy of profiling information.
  assert(!MI->isDebugInstr() && "Should not hoist debug inst");
  MI->setDebugLoc(DebugLoc());

  // Add register to livein list to all the BBs in the current loop since a
  // loop invariant must be kept live throughout the whole loop. This is
  // important to ensure later passes do not scavenge the def register.
  AddToLiveIns(Def, CurLoop);

  ++NumPostRAHoisted;
  Changed = true;
}

/// Check if this mbb is guaranteed to execute. If not then a load from this mbb
/// may not be safe to hoist.
bool MachineLICMBase::IsGuaranteedToExecute(MachineBasicBlock *BB,
                                            MachineLoop *CurLoop) {
  if (SpeculationState != SpeculateUnknown)
    return SpeculationState == SpeculateFalse;

  if (BB != CurLoop->getHeader()) {
    // Check loop exiting blocks.
    SmallVector<MachineBasicBlock*, 8> CurrentLoopExitingBlocks;
    CurLoop->getExitingBlocks(CurrentLoopExitingBlocks);
    for (MachineBasicBlock *CurrentLoopExitingBlock : CurrentLoopExitingBlocks)
      if (!DT->dominates(BB, CurrentLoopExitingBlock)) {
        SpeculationState = SpeculateTrue;
        return false;
      }
  }

  SpeculationState = SpeculateFalse;
  return true;
}

/// Check if \p MI is trivially remateralizable and if it does not have any
/// virtual register uses. Even though rematerializable RA might not actually
/// rematerialize it in this scenario. In that case we do not want to hoist such
/// instruction out of the loop in a belief RA will sink it back if needed.
bool MachineLICMBase::isTriviallyReMaterializable(
    const MachineInstr &MI) const {
  if (!TII->isTriviallyReMaterializable(MI))
    return false;

  for (const MachineOperand &MO : MI.all_uses()) {
    if (MO.getReg().isVirtual())
      return false;
  }

  return true;
}

void MachineLICMBase::EnterScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Entering " << printMBBReference(*MBB) << '\n');

  // Remember livein register pressure.
  BackTrace.push_back(RegPressure);
}

void MachineLICMBase::ExitScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Exiting " << printMBBReference(*MBB) << '\n');
  BackTrace.pop_back();
}

/// Destroy scope for the MBB that corresponds to the given dominator tree node
/// if its a leaf or all of its children are done. Walk up the dominator tree to
/// destroy ancestors which are now done.
void MachineLICMBase::ExitScopeIfDone(MachineDomTreeNode *Node,
    DenseMap<MachineDomTreeNode*, unsigned> &OpenChildren,
    const DenseMap<MachineDomTreeNode*, MachineDomTreeNode*> &ParentMap) {
  if (OpenChildren[Node])
    return;

  for(;;) {
    ExitScope(Node->getBlock());
    // Now traverse upwards to pop ancestors whose offsprings are all done.
    MachineDomTreeNode *Parent = ParentMap.lookup(Node);
    if (!Parent || --OpenChildren[Parent] != 0)
      break;
    Node = Parent;
  }
}

/// Walk the specified loop in the CFG (defined by all blocks dominated by the
/// specified header block, and that are in the current loop) in depth first
/// order w.r.t the DominatorTree. This allows us to visit definitions before
/// uses, allowing us to hoist a loop body in one pass without iteration.
void MachineLICMBase::HoistOutOfLoop(MachineDomTreeNode *HeaderN,
                                     MachineLoop *CurLoop,
                                     MachineBasicBlock *CurPreheader) {
  MachineBasicBlock *Preheader = getCurPreheader(CurLoop, CurPreheader);
  if (!Preheader)
    return;

  SmallVector<MachineDomTreeNode*, 32> Scopes;
  SmallVector<MachineDomTreeNode*, 8> WorkList;
  DenseMap<MachineDomTreeNode*, MachineDomTreeNode*> ParentMap;
  DenseMap<MachineDomTreeNode*, unsigned> OpenChildren;

  // Perform a DFS walk to determine the order of visit.
  WorkList.push_back(HeaderN);
  while (!WorkList.empty()) {
    MachineDomTreeNode *Node = WorkList.pop_back_val();
    assert(Node && "Null dominator tree node?");
    MachineBasicBlock *BB = Node->getBlock();

    // If the header of the loop containing this basic block is a landing pad,
    // then don't try to hoist instructions out of this loop.
    const MachineLoop *ML = MLI->getLoopFor(BB);
    if (ML && ML->getHeader()->isEHPad())
      continue;

    // If this subregion is not in the top level loop at all, exit.
    if (!CurLoop->contains(BB))
      continue;

    Scopes.push_back(Node);
    unsigned NumChildren = Node->getNumChildren();

    // Don't hoist things out of a large switch statement.  This often causes
    // code to be hoisted that wasn't going to be executed, and increases
    // register pressure in a situation where it's likely to matter.
    if (BB->succ_size() >= 25)
      NumChildren = 0;

    OpenChildren[Node] = NumChildren;
    if (NumChildren) {
      // Add children in reverse order as then the next popped worklist node is
      // the first child of this node.  This means we ultimately traverse the
      // DOM tree in exactly the same order as if we'd recursed.
      for (MachineDomTreeNode *Child : reverse(Node->children())) {
        ParentMap[Child] = Node;
        WorkList.push_back(Child);
      }
    }
  }

  if (Scopes.size() == 0)
    return;

  // Compute registers which are livein into the loop headers.
  RegSeen.clear();
  BackTrace.clear();
  InitRegPressure(Preheader);

  // Now perform LICM.
  for (MachineDomTreeNode *Node : Scopes) {
    MachineBasicBlock *MBB = Node->getBlock();

    EnterScope(MBB);

    // Process the block
    SpeculationState = SpeculateUnknown;
    for (MachineInstr &MI : llvm::make_early_inc_range(*MBB)) {
      unsigned HoistRes = HoistResult::NotHoisted;
      HoistRes = Hoist(&MI, Preheader, CurLoop);
      if (HoistRes & HoistResult::NotHoisted) {
        // We have failed to hoist MI to outermost loop's preheader. If MI is in
        // a subloop, try to hoist it to subloop's preheader.
        SmallVector<MachineLoop *> InnerLoopWorkList;
        for (MachineLoop *L = MLI->getLoopFor(MI.getParent()); L != CurLoop;
             L = L->getParentLoop())
          InnerLoopWorkList.push_back(L);

        while (!InnerLoopWorkList.empty()) {
          MachineLoop *InnerLoop = InnerLoopWorkList.pop_back_val();
          MachineBasicBlock *InnerLoopPreheader = InnerLoop->getLoopPreheader();
          if (InnerLoopPreheader) {
            HoistRes = Hoist(&MI, InnerLoopPreheader, InnerLoop);
            if (HoistRes & HoistResult::Hoisted)
              break;
          }
        }
      }

      if (HoistRes & HoistResult::ErasedMI)
        continue;

      UpdateRegPressure(&MI);
    }

    // If it's a leaf node, it's done. Traverse upwards to pop ancestors.
    ExitScopeIfDone(Node, OpenChildren, ParentMap);
  }
}

static bool isOperandKill(const MachineOperand &MO, MachineRegisterInfo *MRI) {
  return MO.isKill() || MRI->hasOneNonDBGUse(MO.getReg());
}

/// Find all virtual register references that are liveout of the preheader to
/// initialize the starting "register pressure". Note this does not count live
/// through (livein but not used) registers.
void MachineLICMBase::InitRegPressure(MachineBasicBlock *BB) {
  std::fill(RegPressure.begin(), RegPressure.end(), 0);

  // If the preheader has only a single predecessor and it ends with a
  // fallthrough or an unconditional branch, then scan its predecessor for live
  // defs as well. This happens whenever the preheader is created by splitting
  // the critical edge from the loop predecessor to the loop header.
  if (BB->pred_size() == 1) {
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;
    if (!TII->analyzeBranch(*BB, TBB, FBB, Cond, false) && Cond.empty())
      InitRegPressure(*BB->pred_begin());
  }

  for (const MachineInstr &MI : *BB)
    UpdateRegPressure(&MI, /*ConsiderUnseenAsDef=*/true);
}

/// Update estimate of register pressure after the specified instruction.
void MachineLICMBase::UpdateRegPressure(const MachineInstr *MI,
                                        bool ConsiderUnseenAsDef) {
  auto Cost = calcRegisterCost(MI, /*ConsiderSeen=*/true, ConsiderUnseenAsDef);
  for (const auto &RPIdAndCost : Cost) {
    unsigned Class = RPIdAndCost.first;
    if (static_cast<int>(RegPressure[Class]) < -RPIdAndCost.second)
      RegPressure[Class] = 0;
    else
      RegPressure[Class] += RPIdAndCost.second;
  }
}

/// Calculate the additional register pressure that the registers used in MI
/// cause.
///
/// If 'ConsiderSeen' is true, updates 'RegSeen' and uses the information to
/// figure out which usages are live-ins.
/// FIXME: Figure out a way to consider 'RegSeen' from all code paths.
DenseMap<unsigned, int>
MachineLICMBase::calcRegisterCost(const MachineInstr *MI, bool ConsiderSeen,
                                  bool ConsiderUnseenAsDef) {
  DenseMap<unsigned, int> Cost;
  if (MI->isImplicitDef())
    return Cost;
  for (unsigned i = 0, e = MI->getDesc().getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;

    // FIXME: It seems bad to use RegSeen only for some of these calculations.
    bool isNew = ConsiderSeen ? RegSeen.insert(Reg).second : false;
    const TargetRegisterClass *RC = MRI->getRegClass(Reg);

    RegClassWeight W = TRI->getRegClassWeight(RC);
    int RCCost = 0;
    if (MO.isDef())
      RCCost = W.RegWeight;
    else {
      bool isKill = isOperandKill(MO, MRI);
      if (isNew && !isKill && ConsiderUnseenAsDef)
        // Haven't seen this, it must be a livein.
        RCCost = W.RegWeight;
      else if (!isNew && isKill)
        RCCost = -W.RegWeight;
    }
    if (RCCost == 0)
      continue;
    const int *PS = TRI->getRegClassPressureSets(RC);
    for (; *PS != -1; ++PS) {
      if (!Cost.contains(*PS))
        Cost[*PS] = RCCost;
      else
        Cost[*PS] += RCCost;
    }
  }
  return Cost;
}

/// Return true if this machine instruction loads from global offset table or
/// constant pool.
static bool mayLoadFromGOTOrConstantPool(MachineInstr &MI) {
  assert(MI.mayLoad() && "Expected MI that loads!");

  // If we lost memory operands, conservatively assume that the instruction
  // reads from everything..
  if (MI.memoperands_empty())
    return true;

  for (MachineMemOperand *MemOp : MI.memoperands())
    if (const PseudoSourceValue *PSV = MemOp->getPseudoValue())
      if (PSV->isGOT() || PSV->isConstantPool())
        return true;

  return false;
}

// This function iterates through all the operands of the input store MI and
// checks that each register operand statisfies isCallerPreservedPhysReg.
// This means, the value being stored and the address where it is being stored
// is constant throughout the body of the function (not including prologue and
// epilogue). When called with an MI that isn't a store, it returns false.
// A future improvement can be to check if the store registers are constant
// throughout the loop rather than throughout the funtion.
static bool isInvariantStore(const MachineInstr &MI,
                             const TargetRegisterInfo *TRI,
                             const MachineRegisterInfo *MRI) {

  bool FoundCallerPresReg = false;
  if (!MI.mayStore() || MI.hasUnmodeledSideEffects() ||
      (MI.getNumOperands() == 0))
    return false;

  // Check that all register operands are caller-preserved physical registers.
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg()) {
      Register Reg = MO.getReg();
      // If operand is a virtual register, check if it comes from a copy of a
      // physical register.
      if (Reg.isVirtual())
        Reg = TRI->lookThruCopyLike(MO.getReg(), MRI);
      if (Reg.isVirtual())
        return false;
      if (!TRI->isCallerPreservedPhysReg(Reg.asMCReg(), *MI.getMF()))
        return false;
      else
        FoundCallerPresReg = true;
    } else if (!MO.isImm()) {
        return false;
    }
  }
  return FoundCallerPresReg;
}

// Return true if the input MI is a copy instruction that feeds an invariant
// store instruction. This means that the src of the copy has to satisfy
// isCallerPreservedPhysReg and atleast one of it's users should satisfy
// isInvariantStore.
static bool isCopyFeedingInvariantStore(const MachineInstr &MI,
                                        const MachineRegisterInfo *MRI,
                                        const TargetRegisterInfo *TRI) {

  // FIXME: If targets would like to look through instructions that aren't
  // pure copies, this can be updated to a query.
  if (!MI.isCopy())
    return false;

  const MachineFunction *MF = MI.getMF();
  // Check that we are copying a constant physical register.
  Register CopySrcReg = MI.getOperand(1).getReg();
  if (CopySrcReg.isVirtual())
    return false;

  if (!TRI->isCallerPreservedPhysReg(CopySrcReg.asMCReg(), *MF))
    return false;

  Register CopyDstReg = MI.getOperand(0).getReg();
  // Check if any of the uses of the copy are invariant stores.
  assert(CopyDstReg.isVirtual() && "copy dst is not a virtual reg");

  for (MachineInstr &UseMI : MRI->use_instructions(CopyDstReg)) {
    if (UseMI.mayStore() && isInvariantStore(UseMI, TRI, MRI))
      return true;
  }
  return false;
}

/// Returns true if the instruction may be a suitable candidate for LICM.
/// e.g. If the instruction is a call, then it's obviously not safe to hoist it.
bool MachineLICMBase::IsLICMCandidate(MachineInstr &I, MachineLoop *CurLoop) {
  // Check if it's safe to move the instruction.
  bool DontMoveAcrossStore = !HoistConstLoads || !AllowedToHoistLoads[CurLoop];
  if ((!I.isSafeToMove(AA, DontMoveAcrossStore)) &&
      !(HoistConstStores && isInvariantStore(I, TRI, MRI))) {
    LLVM_DEBUG(dbgs() << "LICM: Instruction not safe to move.\n");
    return false;
  }

  // If it is a load then check if it is guaranteed to execute by making sure
  // that it dominates all exiting blocks. If it doesn't, then there is a path
  // out of the loop which does not execute this load, so we can't hoist it.
  // Loads from constant memory are safe to speculate, for example indexed load
  // from a jump table.
  // Stores and side effects are already checked by isSafeToMove.
  if (I.mayLoad() && !mayLoadFromGOTOrConstantPool(I) &&
      !IsGuaranteedToExecute(I.getParent(), CurLoop)) {
    LLVM_DEBUG(dbgs() << "LICM: Load not guaranteed to execute.\n");
    return false;
  }

  // Convergent attribute has been used on operations that involve inter-thread
  // communication which results are implicitly affected by the enclosing
  // control flows. It is not safe to hoist or sink such operations across
  // control flow.
  if (I.isConvergent())
    return false;

  if (!TII->shouldHoist(I, CurLoop))
    return false;

  return true;
}

/// Returns true if the instruction is loop invariant.
bool MachineLICMBase::IsLoopInvariantInst(MachineInstr &I,
                                          MachineLoop *CurLoop) {
  if (!IsLICMCandidate(I, CurLoop)) {
    LLVM_DEBUG(dbgs() << "LICM: Instruction not a LICM candidate\n");
    return false;
  }
  return CurLoop->isLoopInvariant(I);
}

/// Return true if the specified instruction is used by a phi node and hoisting
/// it could cause a copy to be inserted.
bool MachineLICMBase::HasLoopPHIUse(const MachineInstr *MI,
                                    MachineLoop *CurLoop) {
  SmallVector<const MachineInstr *, 8> Work(1, MI);
  do {
    MI = Work.pop_back_val();
    for (const MachineOperand &MO : MI->all_defs()) {
      Register Reg = MO.getReg();
      if (!Reg.isVirtual())
        continue;
      for (MachineInstr &UseMI : MRI->use_instructions(Reg)) {
        // A PHI may cause a copy to be inserted.
        if (UseMI.isPHI()) {
          // A PHI inside the loop causes a copy because the live range of Reg is
          // extended across the PHI.
          if (CurLoop->contains(&UseMI))
            return true;
          // A PHI in an exit block can cause a copy to be inserted if the PHI
          // has multiple predecessors in the loop with different values.
          // For now, approximate by rejecting all exit blocks.
          if (isExitBlock(CurLoop, UseMI.getParent()))
            return true;
          continue;
        }
        // Look past copies as well.
        if (UseMI.isCopy() && CurLoop->contains(&UseMI))
          Work.push_back(&UseMI);
      }
    }
  } while (!Work.empty());
  return false;
}

/// Compute operand latency between a def of 'Reg' and an use in the current
/// loop, return true if the target considered it high.
bool MachineLICMBase::HasHighOperandLatency(MachineInstr &MI, unsigned DefIdx,
                                            Register Reg,
                                            MachineLoop *CurLoop) const {
  if (MRI->use_nodbg_empty(Reg))
    return false;

  for (MachineInstr &UseMI : MRI->use_nodbg_instructions(Reg)) {
    if (UseMI.isCopyLike())
      continue;
    if (!CurLoop->contains(UseMI.getParent()))
      continue;
    for (unsigned i = 0, e = UseMI.getNumOperands(); i != e; ++i) {
      const MachineOperand &MO = UseMI.getOperand(i);
      if (!MO.isReg() || !MO.isUse())
        continue;
      Register MOReg = MO.getReg();
      if (MOReg != Reg)
        continue;

      if (TII->hasHighOperandLatency(SchedModel, MRI, MI, DefIdx, UseMI, i))
        return true;
    }

    // Only look at the first in loop use.
    break;
  }

  return false;
}

/// Return true if the instruction is marked "cheap" or the operand latency
/// between its def and a use is one or less.
bool MachineLICMBase::IsCheapInstruction(MachineInstr &MI) const {
  if (TII->isAsCheapAsAMove(MI) || MI.isCopyLike())
    return true;

  bool isCheap = false;
  unsigned NumDefs = MI.getDesc().getNumDefs();
  for (unsigned i = 0, e = MI.getNumOperands(); NumDefs && i != e; ++i) {
    MachineOperand &DefMO = MI.getOperand(i);
    if (!DefMO.isReg() || !DefMO.isDef())
      continue;
    --NumDefs;
    Register Reg = DefMO.getReg();
    if (Reg.isPhysical())
      continue;

    if (!TII->hasLowDefLatency(SchedModel, MI, i))
      return false;
    isCheap = true;
  }

  return isCheap;
}

/// Visit BBs from header to current BB, check if hoisting an instruction of the
/// given cost matrix can cause high register pressure.
bool
MachineLICMBase::CanCauseHighRegPressure(const DenseMap<unsigned, int>& Cost,
                                         bool CheapInstr) {
  for (const auto &RPIdAndCost : Cost) {
    if (RPIdAndCost.second <= 0)
      continue;

    unsigned Class = RPIdAndCost.first;
    int Limit = RegLimit[Class];

    // Don't hoist cheap instructions if they would increase register pressure,
    // even if we're under the limit.
    if (CheapInstr && !HoistCheapInsts)
      return true;

    for (const auto &RP : BackTrace)
      if (static_cast<int>(RP[Class]) + RPIdAndCost.second >= Limit)
        return true;
  }

  return false;
}

/// Traverse the back trace from header to the current block and update their
/// register pressures to reflect the effect of hoisting MI from the current
/// block to the preheader.
void MachineLICMBase::UpdateBackTraceRegPressure(const MachineInstr *MI) {
  // First compute the 'cost' of the instruction, i.e. its contribution
  // to register pressure.
  auto Cost = calcRegisterCost(MI, /*ConsiderSeen=*/false,
                               /*ConsiderUnseenAsDef=*/false);

  // Update register pressure of blocks from loop header to current block.
  for (auto &RP : BackTrace)
    for (const auto &RPIdAndCost : Cost)
      RP[RPIdAndCost.first] += RPIdAndCost.second;
}

/// Return true if it is potentially profitable to hoist the given loop
/// invariant.
bool MachineLICMBase::IsProfitableToHoist(MachineInstr &MI,
                                          MachineLoop *CurLoop) {
  if (MI.isImplicitDef())
    return true;

  // Besides removing computation from the loop, hoisting an instruction has
  // these effects:
  //
  // - The value defined by the instruction becomes live across the entire
  //   loop. This increases register pressure in the loop.
  //
  // - If the value is used by a PHI in the loop, a copy will be required for
  //   lowering the PHI after extending the live range.
  //
  // - When hoisting the last use of a value in the loop, that value no longer
  //   needs to be live in the loop. This lowers register pressure in the loop.

  if (HoistConstStores &&  isCopyFeedingInvariantStore(MI, MRI, TRI))
    return true;

  bool CheapInstr = IsCheapInstruction(MI);
  bool CreatesCopy = HasLoopPHIUse(&MI, CurLoop);

  // Don't hoist a cheap instruction if it would create a copy in the loop.
  if (CheapInstr && CreatesCopy) {
    LLVM_DEBUG(dbgs() << "Won't hoist cheap instr with loop PHI use: " << MI);
    return false;
  }

  // Rematerializable instructions should always be hoisted providing the
  // register allocator can just pull them down again when needed.
  if (isTriviallyReMaterializable(MI))
    return true;

  // FIXME: If there are long latency loop-invariant instructions inside the
  // loop at this point, why didn't the optimizer's LICM hoist them?
  for (unsigned i = 0, e = MI.getDesc().getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || MO.isImplicit())
      continue;
    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;
    if (MO.isDef() && HasHighOperandLatency(MI, i, Reg, CurLoop)) {
      LLVM_DEBUG(dbgs() << "Hoist High Latency: " << MI);
      ++NumHighLatency;
      return true;
    }
  }

  // Estimate register pressure to determine whether to LICM the instruction.
  // In low register pressure situation, we can be more aggressive about
  // hoisting. Also, favors hoisting long latency instructions even in
  // moderately high pressure situation.
  // Cheap instructions will only be hoisted if they don't increase register
  // pressure at all.
  auto Cost = calcRegisterCost(&MI, /*ConsiderSeen=*/false,
                               /*ConsiderUnseenAsDef=*/false);

  // Visit BBs from header to current BB, if hoisting this doesn't cause
  // high register pressure, then it's safe to proceed.
  if (!CanCauseHighRegPressure(Cost, CheapInstr)) {
    LLVM_DEBUG(dbgs() << "Hoist non-reg-pressure: " << MI);
    ++NumLowRP;
    return true;
  }

  // Don't risk increasing register pressure if it would create copies.
  if (CreatesCopy) {
    LLVM_DEBUG(dbgs() << "Won't hoist instr with loop PHI use: " << MI);
    return false;
  }

  // Do not "speculate" in high register pressure situation. If an
  // instruction is not guaranteed to be executed in the loop, it's best to be
  // conservative.
  if (AvoidSpeculation &&
      (!IsGuaranteedToExecute(MI.getParent(), CurLoop) && !MayCSE(&MI))) {
    LLVM_DEBUG(dbgs() << "Won't speculate: " << MI);
    return false;
  }

  // If we have a COPY with other uses in the loop, hoist to allow the users to
  // also be hoisted.
  // TODO: Handle all isCopyLike?
  if (MI.isCopy() || MI.isRegSequence()) {
    Register DefReg = MI.getOperand(0).getReg();
    if (DefReg.isVirtual() &&
        all_of(MI.uses(),
               [this](const MachineOperand &UseOp) {
                 return !UseOp.isReg() || UseOp.getReg().isVirtual() ||
                        MRI->isConstantPhysReg(UseOp.getReg());
               }) &&
        IsLoopInvariantInst(MI, CurLoop) &&
        any_of(MRI->use_nodbg_instructions(DefReg),
               [&CurLoop, this, DefReg, Cost](MachineInstr &UseMI) {
                 if (!CurLoop->contains(&UseMI))
                   return false;

                 // COPY is a cheap instruction, but if moving it won't cause
                 // high RP we're fine to hoist it even if the user can't be
                 // hoisted later Otherwise we want to check the user if it's
                 // hoistable
                 if (CanCauseHighRegPressure(Cost, false) &&
                     !CurLoop->isLoopInvariant(UseMI, DefReg))
                   return false;

                 return true;
               }))
      return true;
  }

  // High register pressure situation, only hoist if the instruction is going
  // to be remat'ed.
  if (!isTriviallyReMaterializable(MI) &&
      !MI.isDereferenceableInvariantLoad()) {
    LLVM_DEBUG(dbgs() << "Can't remat / high reg-pressure: " << MI);
    return false;
  }

  return true;
}

/// Unfold a load from the given machineinstr if the load itself could be
/// hoisted. Return the unfolded and hoistable load, or null if the load
/// couldn't be unfolded or if it wouldn't be hoistable.
MachineInstr *MachineLICMBase::ExtractHoistableLoad(MachineInstr *MI,
                                                    MachineLoop *CurLoop) {
  // Don't unfold simple loads.
  if (MI->canFoldAsLoad())
    return nullptr;

  // If not, we may be able to unfold a load and hoist that.
  // First test whether the instruction is loading from an amenable
  // memory location.
  if (!MI->isDereferenceableInvariantLoad())
    return nullptr;

  // Next determine the register class for a temporary register.
  unsigned LoadRegIndex;
  unsigned NewOpc =
    TII->getOpcodeAfterMemoryUnfold(MI->getOpcode(),
                                    /*UnfoldLoad=*/true,
                                    /*UnfoldStore=*/false,
                                    &LoadRegIndex);
  if (NewOpc == 0) return nullptr;
  const MCInstrDesc &MID = TII->get(NewOpc);
  MachineFunction &MF = *MI->getMF();
  const TargetRegisterClass *RC = TII->getRegClass(MID, LoadRegIndex, TRI, MF);
  // Ok, we're unfolding. Create a temporary register and do the unfold.
  Register Reg = MRI->createVirtualRegister(RC);

  SmallVector<MachineInstr *, 2> NewMIs;
  bool Success = TII->unfoldMemoryOperand(MF, *MI, Reg,
                                          /*UnfoldLoad=*/true,
                                          /*UnfoldStore=*/false, NewMIs);
  (void)Success;
  assert(Success &&
         "unfoldMemoryOperand failed when getOpcodeAfterMemoryUnfold "
         "succeeded!");
  assert(NewMIs.size() == 2 &&
         "Unfolded a load into multiple instructions!");
  MachineBasicBlock *MBB = MI->getParent();
  MachineBasicBlock::iterator Pos = MI;
  MBB->insert(Pos, NewMIs[0]);
  MBB->insert(Pos, NewMIs[1]);
  // If unfolding produced a load that wasn't loop-invariant or profitable to
  // hoist, discard the new instructions and bail.
  if (!IsLoopInvariantInst(*NewMIs[0], CurLoop) ||
      !IsProfitableToHoist(*NewMIs[0], CurLoop)) {
    NewMIs[0]->eraseFromParent();
    NewMIs[1]->eraseFromParent();
    return nullptr;
  }

  // Update register pressure for the unfolded instruction.
  UpdateRegPressure(NewMIs[1]);

  // Otherwise we successfully unfolded a load that we can hoist.

  // Update the call site info.
  if (MI->shouldUpdateCallSiteInfo())
    MF.eraseCallSiteInfo(MI);

  MI->eraseFromParent();
  return NewMIs[0];
}

/// Initialize the CSE map with instructions that are in the current loop
/// preheader that may become duplicates of instructions that are hoisted
/// out of the loop.
void MachineLICMBase::InitCSEMap(MachineBasicBlock *BB) {
  for (MachineInstr &MI : *BB)
    CSEMap[BB][MI.getOpcode()].push_back(&MI);
}

/// Initialize AllowedToHoistLoads with information about whether invariant
/// loads can be moved outside a given loop
void MachineLICMBase::InitializeLoadsHoistableLoops() {
  SmallVector<MachineLoop *, 8> Worklist(MLI->begin(), MLI->end());
  SmallVector<MachineLoop *, 8> LoopsInPreOrder;

  // Mark all loops as hoistable initially and prepare a list of loops in
  // pre-order DFS.
  while (!Worklist.empty()) {
    auto *L = Worklist.pop_back_val();
    AllowedToHoistLoads[L] = true;
    LoopsInPreOrder.push_back(L);
    Worklist.insert(Worklist.end(), L->getSubLoops().begin(),
                    L->getSubLoops().end());
  }

  // Going from the innermost to outermost loops, check if a loop has
  // instructions preventing invariant load hoisting. If such instruction is
  // found, mark this loop and its parent as non-hoistable and continue
  // investigating the next loop.
  // Visiting in a reversed pre-ordered DFS manner
  // allows us to not process all the instructions of the outer loop if the
  // inner loop is proved to be non-load-hoistable.
  for (auto *Loop : reverse(LoopsInPreOrder)) {
    for (auto *MBB : Loop->blocks()) {
      // If this loop has already been marked as non-hoistable, skip it.
      if (!AllowedToHoistLoads[Loop])
        continue;
      for (auto &MI : *MBB) {
        if (!MI.isLoadFoldBarrier() && !MI.mayStore() && !MI.isCall() &&
            !(MI.mayLoad() && MI.hasOrderedMemoryRef()))
          continue;
        for (MachineLoop *L = Loop; L != nullptr; L = L->getParentLoop())
          AllowedToHoistLoads[L] = false;
        break;
      }
    }
  }
}

/// Find an instruction amount PrevMIs that is a duplicate of MI.
/// Return this instruction if it's found.
MachineInstr *
MachineLICMBase::LookForDuplicate(const MachineInstr *MI,
                                  std::vector<MachineInstr *> &PrevMIs) {
  for (MachineInstr *PrevMI : PrevMIs)
    if (TII->produceSameValue(*MI, *PrevMI, (PreRegAlloc ? MRI : nullptr)))
      return PrevMI;

  return nullptr;
}

/// Given a LICM'ed instruction, look for an instruction on the preheader that
/// computes the same value. If it's found, do a RAU on with the definition of
/// the existing instruction rather than hoisting the instruction to the
/// preheader.
bool MachineLICMBase::EliminateCSE(
    MachineInstr *MI,
    DenseMap<unsigned, std::vector<MachineInstr *>>::iterator &CI) {
  // Do not CSE implicit_def so ProcessImplicitDefs can properly propagate
  // the undef property onto uses.
  if (MI->isImplicitDef())
    return false;

  // Do not CSE normal loads because between them could be store instructions
  // that change the loaded value
  if (MI->mayLoad() && !MI->isDereferenceableInvariantLoad())
    return false;

  if (MachineInstr *Dup = LookForDuplicate(MI, CI->second)) {
    LLVM_DEBUG(dbgs() << "CSEing " << *MI << " with " << *Dup);

    // Replace virtual registers defined by MI by their counterparts defined
    // by Dup.
    SmallVector<unsigned, 2> Defs;
    for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
      const MachineOperand &MO = MI->getOperand(i);

      // Physical registers may not differ here.
      assert((!MO.isReg() || MO.getReg() == 0 || !MO.getReg().isPhysical() ||
              MO.getReg() == Dup->getOperand(i).getReg()) &&
             "Instructions with different phys regs are not identical!");

      if (MO.isReg() && MO.isDef() && !MO.getReg().isPhysical())
        Defs.push_back(i);
    }

    SmallVector<const TargetRegisterClass*, 2> OrigRCs;
    for (unsigned i = 0, e = Defs.size(); i != e; ++i) {
      unsigned Idx = Defs[i];
      Register Reg = MI->getOperand(Idx).getReg();
      Register DupReg = Dup->getOperand(Idx).getReg();
      OrigRCs.push_back(MRI->getRegClass(DupReg));

      if (!MRI->constrainRegClass(DupReg, MRI->getRegClass(Reg))) {
        // Restore old RCs if more than one defs.
        for (unsigned j = 0; j != i; ++j)
          MRI->setRegClass(Dup->getOperand(Defs[j]).getReg(), OrigRCs[j]);
        return false;
      }
    }

    for (unsigned Idx : Defs) {
      Register Reg = MI->getOperand(Idx).getReg();
      Register DupReg = Dup->getOperand(Idx).getReg();
      MRI->replaceRegWith(Reg, DupReg);
      MRI->clearKillFlags(DupReg);
      // Clear Dup dead flag if any, we reuse it for Reg.
      if (!MRI->use_nodbg_empty(DupReg))
        Dup->getOperand(Idx).setIsDead(false);
    }

    MI->eraseFromParent();
    ++NumCSEed;
    return true;
  }
  return false;
}

/// Return true if the given instruction will be CSE'd if it's hoisted out of
/// the loop.
bool MachineLICMBase::MayCSE(MachineInstr *MI) {
  if (MI->mayLoad() && !MI->isDereferenceableInvariantLoad())
    return false;

  unsigned Opcode = MI->getOpcode();
  for (auto &Map : CSEMap) {
    // Check this CSEMap's preheader dominates MI's basic block.
    if (DT->dominates(Map.first, MI->getParent())) {
      DenseMap<unsigned, std::vector<MachineInstr *>>::iterator CI =
          Map.second.find(Opcode);
      // Do not CSE implicit_def so ProcessImplicitDefs can properly propagate
      // the undef property onto uses.
      if (CI == Map.second.end() || MI->isImplicitDef())
        continue;
      if (LookForDuplicate(MI, CI->second) != nullptr)
        return true;
    }
  }

  return false;
}

/// When an instruction is found to use only loop invariant operands
/// that are safe to hoist, this instruction is called to do the dirty work.
/// It returns true if the instruction is hoisted.
unsigned MachineLICMBase::Hoist(MachineInstr *MI, MachineBasicBlock *Preheader,
                                MachineLoop *CurLoop) {
  MachineBasicBlock *SrcBlock = MI->getParent();

  // Disable the instruction hoisting due to block hotness
  if ((DisableHoistingToHotterBlocks == UseBFI::All ||
      (DisableHoistingToHotterBlocks == UseBFI::PGO && HasProfileData)) &&
      isTgtHotterThanSrc(SrcBlock, Preheader)) {
    ++NumNotHoistedDueToHotness;
    return HoistResult::NotHoisted;
  }
  // First check whether we should hoist this instruction.
  bool HasExtractHoistableLoad = false;
  if (!IsLoopInvariantInst(*MI, CurLoop) ||
      !IsProfitableToHoist(*MI, CurLoop)) {
    // If not, try unfolding a hoistable load.
    MI = ExtractHoistableLoad(MI, CurLoop);
    if (!MI)
      return HoistResult::NotHoisted;
    HasExtractHoistableLoad = true;
  }

  // If we have hoisted an instruction that may store, it can only be a constant
  // store.
  if (MI->mayStore())
    NumStoreConst++;

  // Now move the instructions to the predecessor, inserting it before any
  // terminator instructions.
  LLVM_DEBUG({
    dbgs() << "Hoisting " << *MI;
    if (MI->getParent()->getBasicBlock())
      dbgs() << " from " << printMBBReference(*MI->getParent());
    if (Preheader->getBasicBlock())
      dbgs() << " to " << printMBBReference(*Preheader);
    dbgs() << "\n";
  });

  // If this is the first instruction being hoisted to the preheader,
  // initialize the CSE map with potential common expressions.
  if (FirstInLoop) {
    InitCSEMap(Preheader);
    FirstInLoop = false;
  }

  // Look for opportunity to CSE the hoisted instruction.
  unsigned Opcode = MI->getOpcode();
  bool HasCSEDone = false;
  for (auto &Map : CSEMap) {
    // Check this CSEMap's preheader dominates MI's basic block.
    if (DT->dominates(Map.first, MI->getParent())) {
      DenseMap<unsigned, std::vector<MachineInstr *>>::iterator CI =
          Map.second.find(Opcode);
      if (CI != Map.second.end()) {
        if (EliminateCSE(MI, CI)) {
          HasCSEDone = true;
          break;
        }
      }
    }
  }

  if (!HasCSEDone) {
    // Otherwise, splice the instruction to the preheader.
    Preheader->splice(Preheader->getFirstTerminator(),MI->getParent(),MI);

    // Since we are moving the instruction out of its basic block, we do not
    // retain its debug location. Doing so would degrade the debugging
    // experience and adversely affect the accuracy of profiling information.
    assert(!MI->isDebugInstr() && "Should not hoist debug inst");
    MI->setDebugLoc(DebugLoc());

    // Update register pressure for BBs from header to this block.
    UpdateBackTraceRegPressure(MI);

    // Clear the kill flags of any register this instruction defines,
    // since they may need to be live throughout the entire loop
    // rather than just live for part of it.
    for (MachineOperand &MO : MI->all_defs())
      if (!MO.isDead())
        MRI->clearKillFlags(MO.getReg());

    CSEMap[Preheader][Opcode].push_back(MI);
  }

  ++NumHoisted;
  Changed = true;

  if (HasCSEDone || HasExtractHoistableLoad)
    return HoistResult::Hoisted | HoistResult::ErasedMI;
  return HoistResult::Hoisted;
}

/// Get the preheader for the current loop, splitting a critical edge if needed.
MachineBasicBlock *
MachineLICMBase::getCurPreheader(MachineLoop *CurLoop,
                                 MachineBasicBlock *CurPreheader) {
  // Determine the block to which to hoist instructions. If we can't find a
  // suitable loop predecessor, we can't do any hoisting.

  // If we've tried to get a preheader and failed, don't try again.
  if (CurPreheader == reinterpret_cast<MachineBasicBlock *>(-1))
    return nullptr;

  if (!CurPreheader) {
    CurPreheader = CurLoop->getLoopPreheader();
    if (!CurPreheader) {
      MachineBasicBlock *Pred = CurLoop->getLoopPredecessor();
      if (!Pred) {
        CurPreheader = reinterpret_cast<MachineBasicBlock *>(-1);
        return nullptr;
      }

      CurPreheader = Pred->SplitCriticalEdge(CurLoop->getHeader(), *this);
      if (!CurPreheader) {
        CurPreheader = reinterpret_cast<MachineBasicBlock *>(-1);
        return nullptr;
      }
    }
  }
  return CurPreheader;
}

/// Is the target basic block at least "BlockFrequencyRatioThreshold"
/// times hotter than the source basic block.
bool MachineLICMBase::isTgtHotterThanSrc(MachineBasicBlock *SrcBlock,
                                         MachineBasicBlock *TgtBlock) {
  // Parse source and target basic block frequency from MBFI
  uint64_t SrcBF = MBFI->getBlockFreq(SrcBlock).getFrequency();
  uint64_t DstBF = MBFI->getBlockFreq(TgtBlock).getFrequency();

  // Disable the hoisting if source block frequency is zero
  if (!SrcBF)
    return true;

  double Ratio = (double)DstBF / SrcBF;

  // Compare the block frequency ratio with the threshold
  return Ratio > BlockFrequencyRatioThreshold;
}
