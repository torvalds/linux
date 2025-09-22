//===- RegisterCoalescer.cpp - Generic Register Coalescing Interface ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the generic RegisterCoalescer interface which
// is used as the common interface used by all clients and
// implementations of register coalescing.
//
//===----------------------------------------------------------------------===//

#include "RegisterCoalescer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(numJoins    , "Number of interval joins performed");
STATISTIC(numCrossRCs , "Number of cross class joins performed");
STATISTIC(numCommutes , "Number of instruction commuting performed");
STATISTIC(numExtends  , "Number of copies extended");
STATISTIC(NumReMats   , "Number of instructions re-materialized");
STATISTIC(NumInflated , "Number of register classes inflated");
STATISTIC(NumLaneConflicts, "Number of dead lane conflicts tested");
STATISTIC(NumLaneResolves,  "Number of dead lane conflicts resolved");
STATISTIC(NumShrinkToUses,  "Number of shrinkToUses called");

static cl::opt<bool> EnableJoining("join-liveintervals",
                                   cl::desc("Coalesce copies (default=true)"),
                                   cl::init(true), cl::Hidden);

static cl::opt<bool> UseTerminalRule("terminal-rule",
                                     cl::desc("Apply the terminal rule"),
                                     cl::init(false), cl::Hidden);

/// Temporary flag to test critical edge unsplitting.
static cl::opt<bool>
EnableJoinSplits("join-splitedges",
  cl::desc("Coalesce copies on split edges (default=subtarget)"), cl::Hidden);

/// Temporary flag to test global copy optimization.
static cl::opt<cl::boolOrDefault>
EnableGlobalCopies("join-globalcopies",
  cl::desc("Coalesce copies that span blocks (default=subtarget)"),
  cl::init(cl::BOU_UNSET), cl::Hidden);

static cl::opt<bool>
VerifyCoalescing("verify-coalescing",
         cl::desc("Verify machine instrs before and after register coalescing"),
         cl::Hidden);

static cl::opt<unsigned> LateRematUpdateThreshold(
    "late-remat-update-threshold", cl::Hidden,
    cl::desc("During rematerialization for a copy, if the def instruction has "
             "many other copy uses to be rematerialized, delay the multiple "
             "separate live interval update work and do them all at once after "
             "all those rematerialization are done. It will save a lot of "
             "repeated work. "),
    cl::init(100));

static cl::opt<unsigned> LargeIntervalSizeThreshold(
    "large-interval-size-threshold", cl::Hidden,
    cl::desc("If the valnos size of an interval is larger than the threshold, "
             "it is regarded as a large interval. "),
    cl::init(100));

static cl::opt<unsigned> LargeIntervalFreqThreshold(
    "large-interval-freq-threshold", cl::Hidden,
    cl::desc("For a large interval, if it is coalesed with other live "
             "intervals many times more than the threshold, stop its "
             "coalescing to control the compile time. "),
    cl::init(256));

namespace {

  class JoinVals;

  class RegisterCoalescer : public MachineFunctionPass,
                            private LiveRangeEdit::Delegate {
    MachineFunction* MF = nullptr;
    MachineRegisterInfo* MRI = nullptr;
    const TargetRegisterInfo* TRI = nullptr;
    const TargetInstrInfo* TII = nullptr;
    LiveIntervals *LIS = nullptr;
    const MachineLoopInfo* Loops = nullptr;
    AliasAnalysis *AA = nullptr;
    RegisterClassInfo RegClassInfo;

    /// Position and VReg of a PHI instruction during coalescing.
    struct PHIValPos {
      SlotIndex SI;    ///< Slot where this PHI occurs.
      Register Reg;    ///< VReg the PHI occurs in.
      unsigned SubReg; ///< Qualifying subregister for Reg.
    };

    /// Map from debug instruction number to PHI position during coalescing.
    DenseMap<unsigned, PHIValPos> PHIValToPos;
    /// Index of, for each VReg, which debug instruction numbers and
    /// corresponding PHIs are sensitive to coalescing. Each VReg may have
    /// multiple PHI defs, at different positions.
    DenseMap<Register, SmallVector<unsigned, 2>> RegToPHIIdx;

    /// Debug variable location tracking -- for each VReg, maintain an
    /// ordered-by-slot-index set of DBG_VALUEs, to help quick
    /// identification of whether coalescing may change location validity.
    using DbgValueLoc = std::pair<SlotIndex, MachineInstr*>;
    DenseMap<Register, std::vector<DbgValueLoc>> DbgVRegToValues;

    /// A LaneMask to remember on which subregister live ranges we need to call
    /// shrinkToUses() later.
    LaneBitmask ShrinkMask;

    /// True if the main range of the currently coalesced intervals should be
    /// checked for smaller live intervals.
    bool ShrinkMainRange = false;

    /// True if the coalescer should aggressively coalesce global copies
    /// in favor of keeping local copies.
    bool JoinGlobalCopies = false;

    /// True if the coalescer should aggressively coalesce fall-thru
    /// blocks exclusively containing copies.
    bool JoinSplitEdges = false;

    /// Copy instructions yet to be coalesced.
    SmallVector<MachineInstr*, 8> WorkList;
    SmallVector<MachineInstr*, 8> LocalWorkList;

    /// Set of instruction pointers that have been erased, and
    /// that may be present in WorkList.
    SmallPtrSet<MachineInstr*, 8> ErasedInstrs;

    /// Dead instructions that are about to be deleted.
    SmallVector<MachineInstr*, 8> DeadDefs;

    /// Virtual registers to be considered for register class inflation.
    SmallVector<Register, 8> InflateRegs;

    /// The collection of live intervals which should have been updated
    /// immediately after rematerialiation but delayed until
    /// lateLiveIntervalUpdate is called.
    DenseSet<Register> ToBeUpdated;

    /// Record how many times the large live interval with many valnos
    /// has been tried to join with other live interval.
    DenseMap<Register, unsigned long> LargeLIVisitCounter;

    /// Recursively eliminate dead defs in DeadDefs.
    void eliminateDeadDefs(LiveRangeEdit *Edit = nullptr);

    /// LiveRangeEdit callback for eliminateDeadDefs().
    void LRE_WillEraseInstruction(MachineInstr *MI) override;

    /// Coalesce the LocalWorkList.
    void coalesceLocals();

    /// Join compatible live intervals
    void joinAllIntervals();

    /// Coalesce copies in the specified MBB, putting
    /// copies that cannot yet be coalesced into WorkList.
    void copyCoalesceInMBB(MachineBasicBlock *MBB);

    /// Tries to coalesce all copies in CurrList. Returns true if any progress
    /// was made.
    bool copyCoalesceWorkList(MutableArrayRef<MachineInstr*> CurrList);

    /// If one def has many copy like uses, and those copy uses are all
    /// rematerialized, the live interval update needed for those
    /// rematerializations will be delayed and done all at once instead
    /// of being done multiple times. This is to save compile cost because
    /// live interval update is costly.
    void lateLiveIntervalUpdate();

    /// Check if the incoming value defined by a COPY at \p SLRQ in the subrange
    /// has no value defined in the predecessors. If the incoming value is the
    /// same as defined by the copy itself, the value is considered undefined.
    bool copyValueUndefInPredecessors(LiveRange &S,
                                      const MachineBasicBlock *MBB,
                                      LiveQueryResult SLRQ);

    /// Set necessary undef flags on subregister uses after pruning out undef
    /// lane segments from the subrange.
    void setUndefOnPrunedSubRegUses(LiveInterval &LI, Register Reg,
                                    LaneBitmask PrunedLanes);

    /// Attempt to join intervals corresponding to SrcReg/DstReg, which are the
    /// src/dst of the copy instruction CopyMI.  This returns true if the copy
    /// was successfully coalesced away. If it is not currently possible to
    /// coalesce this interval, but it may be possible if other things get
    /// coalesced, then it returns true by reference in 'Again'.
    bool joinCopy(MachineInstr *CopyMI, bool &Again,
                  SmallPtrSetImpl<MachineInstr *> &CurrentErasedInstrs);

    /// Attempt to join these two intervals.  On failure, this
    /// returns false.  The output "SrcInt" will not have been modified, so we
    /// can use this information below to update aliases.
    bool joinIntervals(CoalescerPair &CP);

    /// Attempt joining two virtual registers. Return true on success.
    bool joinVirtRegs(CoalescerPair &CP);

    /// If a live interval has many valnos and is coalesced with other
    /// live intervals many times, we regard such live interval as having
    /// high compile time cost.
    bool isHighCostLiveInterval(LiveInterval &LI);

    /// Attempt joining with a reserved physreg.
    bool joinReservedPhysReg(CoalescerPair &CP);

    /// Add the LiveRange @p ToMerge as a subregister liverange of @p LI.
    /// Subranges in @p LI which only partially interfere with the desired
    /// LaneMask are split as necessary. @p LaneMask are the lanes that
    /// @p ToMerge will occupy in the coalescer register. @p LI has its subrange
    /// lanemasks already adjusted to the coalesced register.
    void mergeSubRangeInto(LiveInterval &LI, const LiveRange &ToMerge,
                           LaneBitmask LaneMask, CoalescerPair &CP,
                           unsigned DstIdx);

    /// Join the liveranges of two subregisters. Joins @p RRange into
    /// @p LRange, @p RRange may be invalid afterwards.
    void joinSubRegRanges(LiveRange &LRange, LiveRange &RRange,
                          LaneBitmask LaneMask, const CoalescerPair &CP);

    /// We found a non-trivially-coalescable copy. If the source value number is
    /// defined by a copy from the destination reg see if we can merge these two
    /// destination reg valno# into a single value number, eliminating a copy.
    /// This returns true if an interval was modified.
    bool adjustCopiesBackFrom(const CoalescerPair &CP, MachineInstr *CopyMI);

    /// Return true if there are definitions of IntB
    /// other than BValNo val# that can reach uses of AValno val# of IntA.
    bool hasOtherReachingDefs(LiveInterval &IntA, LiveInterval &IntB,
                              VNInfo *AValNo, VNInfo *BValNo);

    /// We found a non-trivially-coalescable copy.
    /// If the source value number is defined by a commutable instruction and
    /// its other operand is coalesced to the copy dest register, see if we
    /// can transform the copy into a noop by commuting the definition.
    /// This returns a pair of two flags:
    /// - the first element is true if an interval was modified,
    /// - the second element is true if the destination interval needs
    ///   to be shrunk after deleting the copy.
    std::pair<bool,bool> removeCopyByCommutingDef(const CoalescerPair &CP,
                                                  MachineInstr *CopyMI);

    /// We found a copy which can be moved to its less frequent predecessor.
    bool removePartialRedundancy(const CoalescerPair &CP, MachineInstr &CopyMI);

    /// If the source of a copy is defined by a
    /// trivial computation, replace the copy by rematerialize the definition.
    bool reMaterializeTrivialDef(const CoalescerPair &CP, MachineInstr *CopyMI,
                                 bool &IsDefCopy);

    /// Return true if a copy involving a physreg should be joined.
    bool canJoinPhys(const CoalescerPair &CP);

    /// Replace all defs and uses of SrcReg to DstReg and update the subregister
    /// number if it is not zero. If DstReg is a physical register and the
    /// existing subregister number of the def / use being updated is not zero,
    /// make sure to set it to the correct physical subregister.
    void updateRegDefsUses(Register SrcReg, Register DstReg, unsigned SubIdx);

    /// If the given machine operand reads only undefined lanes add an undef
    /// flag.
    /// This can happen when undef uses were previously concealed by a copy
    /// which we coalesced. Example:
    ///    %0:sub0<def,read-undef> = ...
    ///    %1 = COPY %0           <-- Coalescing COPY reveals undef
    ///       = use %1:sub1       <-- hidden undef use
    void addUndefFlag(const LiveInterval &Int, SlotIndex UseIdx,
                      MachineOperand &MO, unsigned SubRegIdx);

    /// Handle copies of undef values. If the undef value is an incoming
    /// PHI value, it will convert @p CopyMI to an IMPLICIT_DEF.
    /// Returns nullptr if @p CopyMI was not in any way eliminable. Otherwise,
    /// it returns @p CopyMI (which could be an IMPLICIT_DEF at this point).
    MachineInstr *eliminateUndefCopy(MachineInstr *CopyMI);

    /// Check whether or not we should apply the terminal rule on the
    /// destination (Dst) of \p Copy.
    /// When the terminal rule applies, Copy is not profitable to
    /// coalesce.
    /// Dst is terminal if it has exactly one affinity (Dst, Src) and
    /// at least one interference (Dst, Dst2). If Dst is terminal, the
    /// terminal rule consists in checking that at least one of
    /// interfering node, say Dst2, has an affinity of equal or greater
    /// weight with Src.
    /// In that case, Dst2 and Dst will not be able to be both coalesced
    /// with Src. Since Dst2 exposes more coalescing opportunities than
    /// Dst, we can drop \p Copy.
    bool applyTerminalRule(const MachineInstr &Copy) const;

    /// Wrapper method for \see LiveIntervals::shrinkToUses.
    /// This method does the proper fixing of the live-ranges when the afore
    /// mentioned method returns true.
    void shrinkToUses(LiveInterval *LI,
                      SmallVectorImpl<MachineInstr * > *Dead = nullptr) {
      NumShrinkToUses++;
      if (LIS->shrinkToUses(LI, Dead)) {
        /// Check whether or not \p LI is composed by multiple connected
        /// components and if that is the case, fix that.
        SmallVector<LiveInterval*, 8> SplitLIs;
        LIS->splitSeparateComponents(*LI, SplitLIs);
      }
    }

    /// Wrapper Method to do all the necessary work when an Instruction is
    /// deleted.
    /// Optimizations should use this to make sure that deleted instructions
    /// are always accounted for.
    void deleteInstr(MachineInstr* MI) {
      ErasedInstrs.insert(MI);
      LIS->RemoveMachineInstrFromMaps(*MI);
      MI->eraseFromParent();
    }

    /// Walk over function and initialize the DbgVRegToValues map.
    void buildVRegToDbgValueMap(MachineFunction &MF);

    /// Test whether, after merging, any DBG_VALUEs would refer to a
    /// different value number than before merging, and whether this can
    /// be resolved. If not, mark the DBG_VALUE as being undef.
    void checkMergingChangesDbgValues(CoalescerPair &CP, LiveRange &LHS,
                                      JoinVals &LHSVals, LiveRange &RHS,
                                      JoinVals &RHSVals);

    void checkMergingChangesDbgValuesImpl(Register Reg, LiveRange &OtherRange,
                                          LiveRange &RegRange, JoinVals &Vals2);

  public:
    static char ID; ///< Class identification, replacement for typeinfo

    RegisterCoalescer() : MachineFunctionPass(ID) {
      initializeRegisterCoalescerPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override;

    MachineFunctionProperties getClearedProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::IsSSA);
    }

    void releaseMemory() override;

    /// This is the pass entry point.
    bool runOnMachineFunction(MachineFunction&) override;

    /// Implement the dump method.
    void print(raw_ostream &O, const Module* = nullptr) const override;
  };

} // end anonymous namespace

char RegisterCoalescer::ID = 0;

char &llvm::RegisterCoalescerID = RegisterCoalescer::ID;

INITIALIZE_PASS_BEGIN(RegisterCoalescer, "register-coalescer",
                      "Register Coalescer", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(RegisterCoalescer, "register-coalescer",
                    "Register Coalescer", false, false)

[[nodiscard]] static bool isMoveInstr(const TargetRegisterInfo &tri,
                                      const MachineInstr *MI, Register &Src,
                                      Register &Dst, unsigned &SrcSub,
                                      unsigned &DstSub) {
    if (MI->isCopy()) {
      Dst = MI->getOperand(0).getReg();
      DstSub = MI->getOperand(0).getSubReg();
      Src = MI->getOperand(1).getReg();
      SrcSub = MI->getOperand(1).getSubReg();
    } else if (MI->isSubregToReg()) {
      Dst = MI->getOperand(0).getReg();
      DstSub = tri.composeSubRegIndices(MI->getOperand(0).getSubReg(),
                                        MI->getOperand(3).getImm());
      Src = MI->getOperand(2).getReg();
      SrcSub = MI->getOperand(2).getSubReg();
    } else
      return false;
    return true;
}

/// Return true if this block should be vacated by the coalescer to eliminate
/// branches. The important cases to handle in the coalescer are critical edges
/// split during phi elimination which contain only copies. Simple blocks that
/// contain non-branches should also be vacated, but this can be handled by an
/// earlier pass similar to early if-conversion.
static bool isSplitEdge(const MachineBasicBlock *MBB) {
  if (MBB->pred_size() != 1 || MBB->succ_size() != 1)
    return false;

  for (const auto &MI : *MBB) {
    if (!MI.isCopyLike() && !MI.isUnconditionalBranch())
      return false;
  }
  return true;
}

bool CoalescerPair::setRegisters(const MachineInstr *MI) {
  SrcReg = DstReg = Register();
  SrcIdx = DstIdx = 0;
  NewRC = nullptr;
  Flipped = CrossClass = false;

  Register Src, Dst;
  unsigned SrcSub = 0, DstSub = 0;
  if (!isMoveInstr(TRI, MI, Src, Dst, SrcSub, DstSub))
    return false;
  Partial = SrcSub || DstSub;

  // If one register is a physreg, it must be Dst.
  if (Src.isPhysical()) {
    if (Dst.isPhysical())
      return false;
    std::swap(Src, Dst);
    std::swap(SrcSub, DstSub);
    Flipped = true;
  }

  const MachineRegisterInfo &MRI = MI->getMF()->getRegInfo();

  if (Dst.isPhysical()) {
    // Eliminate DstSub on a physreg.
    if (DstSub) {
      Dst = TRI.getSubReg(Dst, DstSub);
      if (!Dst) return false;
      DstSub = 0;
    }

    // Eliminate SrcSub by picking a corresponding Dst superregister.
    if (SrcSub) {
      Dst = TRI.getMatchingSuperReg(Dst, SrcSub, MRI.getRegClass(Src));
      if (!Dst) return false;
    } else if (!MRI.getRegClass(Src)->contains(Dst)) {
      return false;
    }
  } else {
    // Both registers are virtual.
    const TargetRegisterClass *SrcRC = MRI.getRegClass(Src);
    const TargetRegisterClass *DstRC = MRI.getRegClass(Dst);

    // Both registers have subreg indices.
    if (SrcSub && DstSub) {
      // Copies between different sub-registers are never coalescable.
      if (Src == Dst && SrcSub != DstSub)
        return false;

      NewRC = TRI.getCommonSuperRegClass(SrcRC, SrcSub, DstRC, DstSub,
                                         SrcIdx, DstIdx);
      if (!NewRC)
        return false;
    } else if (DstSub) {
      // SrcReg will be merged with a sub-register of DstReg.
      SrcIdx = DstSub;
      NewRC = TRI.getMatchingSuperRegClass(DstRC, SrcRC, DstSub);
    } else if (SrcSub) {
      // DstReg will be merged with a sub-register of SrcReg.
      DstIdx = SrcSub;
      NewRC = TRI.getMatchingSuperRegClass(SrcRC, DstRC, SrcSub);
    } else {
      // This is a straight copy without sub-registers.
      NewRC = TRI.getCommonSubClass(DstRC, SrcRC);
    }

    // The combined constraint may be impossible to satisfy.
    if (!NewRC)
      return false;

    // Prefer SrcReg to be a sub-register of DstReg.
    // FIXME: Coalescer should support subregs symmetrically.
    if (DstIdx && !SrcIdx) {
      std::swap(Src, Dst);
      std::swap(SrcIdx, DstIdx);
      Flipped = !Flipped;
    }

    CrossClass = NewRC != DstRC || NewRC != SrcRC;
  }
  // Check our invariants
  assert(Src.isVirtual() && "Src must be virtual");
  assert(!(Dst.isPhysical() && DstSub) && "Cannot have a physical SubIdx");
  SrcReg = Src;
  DstReg = Dst;
  return true;
}

bool CoalescerPair::flip() {
  if (DstReg.isPhysical())
    return false;
  std::swap(SrcReg, DstReg);
  std::swap(SrcIdx, DstIdx);
  Flipped = !Flipped;
  return true;
}

bool CoalescerPair::isCoalescable(const MachineInstr *MI) const {
  if (!MI)
    return false;
  Register Src, Dst;
  unsigned SrcSub = 0, DstSub = 0;
  if (!isMoveInstr(TRI, MI, Src, Dst, SrcSub, DstSub))
    return false;

  // Find the virtual register that is SrcReg.
  if (Dst == SrcReg) {
    std::swap(Src, Dst);
    std::swap(SrcSub, DstSub);
  } else if (Src != SrcReg) {
    return false;
  }

  // Now check that Dst matches DstReg.
  if (DstReg.isPhysical()) {
    if (!Dst.isPhysical())
      return false;
    assert(!DstIdx && !SrcIdx && "Inconsistent CoalescerPair state.");
    // DstSub could be set for a physreg from INSERT_SUBREG.
    if (DstSub)
      Dst = TRI.getSubReg(Dst, DstSub);
    // Full copy of Src.
    if (!SrcSub)
      return DstReg == Dst;
    // This is a partial register copy. Check that the parts match.
    return Register(TRI.getSubReg(DstReg, SrcSub)) == Dst;
  } else {
    // DstReg is virtual.
    if (DstReg != Dst)
      return false;
    // Registers match, do the subregisters line up?
    return TRI.composeSubRegIndices(SrcIdx, SrcSub) ==
           TRI.composeSubRegIndices(DstIdx, DstSub);
  }
}

void RegisterCoalescer::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<LiveIntervalsWrapperPass>();
  AU.addPreserved<LiveIntervalsWrapperPass>();
  AU.addPreserved<SlotIndexesWrapperPass>();
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addPreservedID(MachineDominatorsID);
  MachineFunctionPass::getAnalysisUsage(AU);
}

void RegisterCoalescer::eliminateDeadDefs(LiveRangeEdit *Edit) {
  if (Edit) {
    Edit->eliminateDeadDefs(DeadDefs);
    return;
  }
  SmallVector<Register, 8> NewRegs;
  LiveRangeEdit(nullptr, NewRegs, *MF, *LIS,
                nullptr, this).eliminateDeadDefs(DeadDefs);
}

void RegisterCoalescer::LRE_WillEraseInstruction(MachineInstr *MI) {
  // MI may be in WorkList. Make sure we don't visit it.
  ErasedInstrs.insert(MI);
}

bool RegisterCoalescer::adjustCopiesBackFrom(const CoalescerPair &CP,
                                             MachineInstr *CopyMI) {
  assert(!CP.isPartial() && "This doesn't work for partial copies.");
  assert(!CP.isPhys() && "This doesn't work for physreg copies.");

  LiveInterval &IntA =
    LIS->getInterval(CP.isFlipped() ? CP.getDstReg() : CP.getSrcReg());
  LiveInterval &IntB =
    LIS->getInterval(CP.isFlipped() ? CP.getSrcReg() : CP.getDstReg());
  SlotIndex CopyIdx = LIS->getInstructionIndex(*CopyMI).getRegSlot();

  // We have a non-trivially-coalescable copy with IntA being the source and
  // IntB being the dest, thus this defines a value number in IntB.  If the
  // source value number (in IntA) is defined by a copy from B, see if we can
  // merge these two pieces of B into a single value number, eliminating a copy.
  // For example:
  //
  //  A3 = B0
  //    ...
  //  B1 = A3      <- this copy
  //
  // In this case, B0 can be extended to where the B1 copy lives, allowing the
  // B1 value number to be replaced with B0 (which simplifies the B
  // liveinterval).

  // BValNo is a value number in B that is defined by a copy from A.  'B1' in
  // the example above.
  LiveInterval::iterator BS = IntB.FindSegmentContaining(CopyIdx);
  if (BS == IntB.end()) return false;
  VNInfo *BValNo = BS->valno;

  // Get the location that B is defined at.  Two options: either this value has
  // an unknown definition point or it is defined at CopyIdx.  If unknown, we
  // can't process it.
  if (BValNo->def != CopyIdx) return false;

  // AValNo is the value number in A that defines the copy, A3 in the example.
  SlotIndex CopyUseIdx = CopyIdx.getRegSlot(true);
  LiveInterval::iterator AS = IntA.FindSegmentContaining(CopyUseIdx);
  // The live segment might not exist after fun with physreg coalescing.
  if (AS == IntA.end()) return false;
  VNInfo *AValNo = AS->valno;

  // If AValNo is defined as a copy from IntB, we can potentially process this.
  // Get the instruction that defines this value number.
  MachineInstr *ACopyMI = LIS->getInstructionFromIndex(AValNo->def);
  // Don't allow any partial copies, even if isCoalescable() allows them.
  if (!CP.isCoalescable(ACopyMI) || !ACopyMI->isFullCopy())
    return false;

  // Get the Segment in IntB that this value number starts with.
  LiveInterval::iterator ValS =
    IntB.FindSegmentContaining(AValNo->def.getPrevSlot());
  if (ValS == IntB.end())
    return false;

  // Make sure that the end of the live segment is inside the same block as
  // CopyMI.
  MachineInstr *ValSEndInst =
    LIS->getInstructionFromIndex(ValS->end.getPrevSlot());
  if (!ValSEndInst || ValSEndInst->getParent() != CopyMI->getParent())
    return false;

  // Okay, we now know that ValS ends in the same block that the CopyMI
  // live-range starts.  If there are no intervening live segments between them
  // in IntB, we can merge them.
  if (ValS+1 != BS) return false;

  LLVM_DEBUG(dbgs() << "Extending: " << printReg(IntB.reg(), TRI));

  SlotIndex FillerStart = ValS->end, FillerEnd = BS->start;
  // We are about to delete CopyMI, so need to remove it as the 'instruction
  // that defines this value #'. Update the valnum with the new defining
  // instruction #.
  BValNo->def = FillerStart;

  // Okay, we can merge them.  We need to insert a new liverange:
  // [ValS.end, BS.begin) of either value number, then we merge the
  // two value numbers.
  IntB.addSegment(LiveInterval::Segment(FillerStart, FillerEnd, BValNo));

  // Okay, merge "B1" into the same value number as "B0".
  if (BValNo != ValS->valno)
    IntB.MergeValueNumberInto(BValNo, ValS->valno);

  // Do the same for the subregister segments.
  for (LiveInterval::SubRange &S : IntB.subranges()) {
    // Check for SubRange Segments of the form [1234r,1234d:0) which can be
    // removed to prevent creating bogus SubRange Segments.
    LiveInterval::iterator SS = S.FindSegmentContaining(CopyIdx);
    if (SS != S.end() && SlotIndex::isSameInstr(SS->start, SS->end)) {
      S.removeSegment(*SS, true);
      continue;
    }
    // The subrange may have ended before FillerStart. If so, extend it.
    if (!S.getVNInfoAt(FillerStart)) {
      SlotIndex BBStart =
          LIS->getMBBStartIdx(LIS->getMBBFromIndex(FillerStart));
      S.extendInBlock(BBStart, FillerStart);
    }
    VNInfo *SubBValNo = S.getVNInfoAt(CopyIdx);
    S.addSegment(LiveInterval::Segment(FillerStart, FillerEnd, SubBValNo));
    VNInfo *SubValSNo = S.getVNInfoAt(AValNo->def.getPrevSlot());
    if (SubBValNo != SubValSNo)
      S.MergeValueNumberInto(SubBValNo, SubValSNo);
  }

  LLVM_DEBUG(dbgs() << "   result = " << IntB << '\n');

  // If the source instruction was killing the source register before the
  // merge, unset the isKill marker given the live range has been extended.
  int UIdx =
      ValSEndInst->findRegisterUseOperandIdx(IntB.reg(), /*TRI=*/nullptr, true);
  if (UIdx != -1) {
    ValSEndInst->getOperand(UIdx).setIsKill(false);
  }

  // Rewrite the copy.
  CopyMI->substituteRegister(IntA.reg(), IntB.reg(), 0, *TRI);
  // If the copy instruction was killing the destination register or any
  // subrange before the merge trim the live range.
  bool RecomputeLiveRange = AS->end == CopyIdx;
  if (!RecomputeLiveRange) {
    for (LiveInterval::SubRange &S : IntA.subranges()) {
      LiveInterval::iterator SS = S.FindSegmentContaining(CopyUseIdx);
      if (SS != S.end() && SS->end == CopyIdx) {
        RecomputeLiveRange = true;
        break;
      }
    }
  }
  if (RecomputeLiveRange)
    shrinkToUses(&IntA);

  ++numExtends;
  return true;
}

bool RegisterCoalescer::hasOtherReachingDefs(LiveInterval &IntA,
                                             LiveInterval &IntB,
                                             VNInfo *AValNo,
                                             VNInfo *BValNo) {
  // If AValNo has PHI kills, conservatively assume that IntB defs can reach
  // the PHI values.
  if (LIS->hasPHIKill(IntA, AValNo))
    return true;

  for (LiveRange::Segment &ASeg : IntA.segments) {
    if (ASeg.valno != AValNo) continue;
    LiveInterval::iterator BI = llvm::upper_bound(IntB, ASeg.start);
    if (BI != IntB.begin())
      --BI;
    for (; BI != IntB.end() && ASeg.end >= BI->start; ++BI) {
      if (BI->valno == BValNo)
        continue;
      if (BI->start <= ASeg.start && BI->end > ASeg.start)
        return true;
      if (BI->start > ASeg.start && BI->start < ASeg.end)
        return true;
    }
  }
  return false;
}

/// Copy segments with value number @p SrcValNo from liverange @p Src to live
/// range @Dst and use value number @p DstValNo there.
static std::pair<bool,bool>
addSegmentsWithValNo(LiveRange &Dst, VNInfo *DstValNo, const LiveRange &Src,
                     const VNInfo *SrcValNo) {
  bool Changed = false;
  bool MergedWithDead = false;
  for (const LiveRange::Segment &S : Src.segments) {
    if (S.valno != SrcValNo)
      continue;
    // This is adding a segment from Src that ends in a copy that is about
    // to be removed. This segment is going to be merged with a pre-existing
    // segment in Dst. This works, except in cases when the corresponding
    // segment in Dst is dead. For example: adding [192r,208r:1) from Src
    // to [208r,208d:1) in Dst would create [192r,208d:1) in Dst.
    // Recognized such cases, so that the segments can be shrunk.
    LiveRange::Segment Added = LiveRange::Segment(S.start, S.end, DstValNo);
    LiveRange::Segment &Merged = *Dst.addSegment(Added);
    if (Merged.end.isDead())
      MergedWithDead = true;
    Changed = true;
  }
  return std::make_pair(Changed, MergedWithDead);
}

std::pair<bool,bool>
RegisterCoalescer::removeCopyByCommutingDef(const CoalescerPair &CP,
                                            MachineInstr *CopyMI) {
  assert(!CP.isPhys());

  LiveInterval &IntA =
      LIS->getInterval(CP.isFlipped() ? CP.getDstReg() : CP.getSrcReg());
  LiveInterval &IntB =
      LIS->getInterval(CP.isFlipped() ? CP.getSrcReg() : CP.getDstReg());

  // We found a non-trivially-coalescable copy with IntA being the source and
  // IntB being the dest, thus this defines a value number in IntB.  If the
  // source value number (in IntA) is defined by a commutable instruction and
  // its other operand is coalesced to the copy dest register, see if we can
  // transform the copy into a noop by commuting the definition. For example,
  //
  //  A3 = op A2 killed B0
  //    ...
  //  B1 = A3      <- this copy
  //    ...
  //     = op A3   <- more uses
  //
  // ==>
  //
  //  B2 = op B0 killed A2
  //    ...
  //  B1 = B2      <- now an identity copy
  //    ...
  //     = op B2   <- more uses

  // BValNo is a value number in B that is defined by a copy from A. 'B1' in
  // the example above.
  SlotIndex CopyIdx = LIS->getInstructionIndex(*CopyMI).getRegSlot();
  VNInfo *BValNo = IntB.getVNInfoAt(CopyIdx);
  assert(BValNo != nullptr && BValNo->def == CopyIdx);

  // AValNo is the value number in A that defines the copy, A3 in the example.
  VNInfo *AValNo = IntA.getVNInfoAt(CopyIdx.getRegSlot(true));
  assert(AValNo && !AValNo->isUnused() && "COPY source not live");
  if (AValNo->isPHIDef())
    return { false, false };
  MachineInstr *DefMI = LIS->getInstructionFromIndex(AValNo->def);
  if (!DefMI)
    return { false, false };
  if (!DefMI->isCommutable())
    return { false, false };
  // If DefMI is a two-address instruction then commuting it will change the
  // destination register.
  int DefIdx = DefMI->findRegisterDefOperandIdx(IntA.reg(), /*TRI=*/nullptr);
  assert(DefIdx != -1);
  unsigned UseOpIdx;
  if (!DefMI->isRegTiedToUseOperand(DefIdx, &UseOpIdx))
    return { false, false };

  // FIXME: The code below tries to commute 'UseOpIdx' operand with some other
  // commutable operand which is expressed by 'CommuteAnyOperandIndex'value
  // passed to the method. That _other_ operand is chosen by
  // the findCommutedOpIndices() method.
  //
  // That is obviously an area for improvement in case of instructions having
  // more than 2 operands. For example, if some instruction has 3 commutable
  // operands then all possible variants (i.e. op#1<->op#2, op#1<->op#3,
  // op#2<->op#3) of commute transformation should be considered/tried here.
  unsigned NewDstIdx = TargetInstrInfo::CommuteAnyOperandIndex;
  if (!TII->findCommutedOpIndices(*DefMI, UseOpIdx, NewDstIdx))
    return { false, false };

  MachineOperand &NewDstMO = DefMI->getOperand(NewDstIdx);
  Register NewReg = NewDstMO.getReg();
  if (NewReg != IntB.reg() || !IntB.Query(AValNo->def).isKill())
    return { false, false };

  // Make sure there are no other definitions of IntB that would reach the
  // uses which the new definition can reach.
  if (hasOtherReachingDefs(IntA, IntB, AValNo, BValNo))
    return { false, false };

  // If some of the uses of IntA.reg is already coalesced away, return false.
  // It's not possible to determine whether it's safe to perform the coalescing.
  for (MachineOperand &MO : MRI->use_nodbg_operands(IntA.reg())) {
    MachineInstr *UseMI = MO.getParent();
    unsigned OpNo = &MO - &UseMI->getOperand(0);
    SlotIndex UseIdx = LIS->getInstructionIndex(*UseMI);
    LiveInterval::iterator US = IntA.FindSegmentContaining(UseIdx);
    if (US == IntA.end() || US->valno != AValNo)
      continue;
    // If this use is tied to a def, we can't rewrite the register.
    if (UseMI->isRegTiedToDefOperand(OpNo))
      return { false, false };
  }

  LLVM_DEBUG(dbgs() << "\tremoveCopyByCommutingDef: " << AValNo->def << '\t'
                    << *DefMI);

  // At this point we have decided that it is legal to do this
  // transformation.  Start by commuting the instruction.
  MachineBasicBlock *MBB = DefMI->getParent();
  MachineInstr *NewMI =
      TII->commuteInstruction(*DefMI, false, UseOpIdx, NewDstIdx);
  if (!NewMI)
    return { false, false };
  if (IntA.reg().isVirtual() && IntB.reg().isVirtual() &&
      !MRI->constrainRegClass(IntB.reg(), MRI->getRegClass(IntA.reg())))
    return { false, false };
  if (NewMI != DefMI) {
    LIS->ReplaceMachineInstrInMaps(*DefMI, *NewMI);
    MachineBasicBlock::iterator Pos = DefMI;
    MBB->insert(Pos, NewMI);
    MBB->erase(DefMI);
  }

  // If ALR and BLR overlaps and end of BLR extends beyond end of ALR, e.g.
  // A = or A, B
  // ...
  // B = A
  // ...
  // C = killed A
  // ...
  //   = B

  // Update uses of IntA of the specific Val# with IntB.
  for (MachineOperand &UseMO :
       llvm::make_early_inc_range(MRI->use_operands(IntA.reg()))) {
    if (UseMO.isUndef())
      continue;
    MachineInstr *UseMI = UseMO.getParent();
    if (UseMI->isDebugInstr()) {
      // FIXME These don't have an instruction index.  Not clear we have enough
      // info to decide whether to do this replacement or not.  For now do it.
      UseMO.setReg(NewReg);
      continue;
    }
    SlotIndex UseIdx = LIS->getInstructionIndex(*UseMI).getRegSlot(true);
    LiveInterval::iterator US = IntA.FindSegmentContaining(UseIdx);
    assert(US != IntA.end() && "Use must be live");
    if (US->valno != AValNo)
      continue;
    // Kill flags are no longer accurate. They are recomputed after RA.
    UseMO.setIsKill(false);
    if (NewReg.isPhysical())
      UseMO.substPhysReg(NewReg, *TRI);
    else
      UseMO.setReg(NewReg);
    if (UseMI == CopyMI)
      continue;
    if (!UseMI->isCopy())
      continue;
    if (UseMI->getOperand(0).getReg() != IntB.reg() ||
        UseMI->getOperand(0).getSubReg())
      continue;

    // This copy will become a noop. If it's defining a new val#, merge it into
    // BValNo.
    SlotIndex DefIdx = UseIdx.getRegSlot();
    VNInfo *DVNI = IntB.getVNInfoAt(DefIdx);
    if (!DVNI)
      continue;
    LLVM_DEBUG(dbgs() << "\t\tnoop: " << DefIdx << '\t' << *UseMI);
    assert(DVNI->def == DefIdx);
    BValNo = IntB.MergeValueNumberInto(DVNI, BValNo);
    for (LiveInterval::SubRange &S : IntB.subranges()) {
      VNInfo *SubDVNI = S.getVNInfoAt(DefIdx);
      if (!SubDVNI)
        continue;
      VNInfo *SubBValNo = S.getVNInfoAt(CopyIdx);
      assert(SubBValNo->def == CopyIdx);
      S.MergeValueNumberInto(SubDVNI, SubBValNo);
    }

    deleteInstr(UseMI);
  }

  // Extend BValNo by merging in IntA live segments of AValNo. Val# definition
  // is updated.
  bool ShrinkB = false;
  BumpPtrAllocator &Allocator = LIS->getVNInfoAllocator();
  if (IntA.hasSubRanges() || IntB.hasSubRanges()) {
    if (!IntA.hasSubRanges()) {
      LaneBitmask Mask = MRI->getMaxLaneMaskForVReg(IntA.reg());
      IntA.createSubRangeFrom(Allocator, Mask, IntA);
    } else if (!IntB.hasSubRanges()) {
      LaneBitmask Mask = MRI->getMaxLaneMaskForVReg(IntB.reg());
      IntB.createSubRangeFrom(Allocator, Mask, IntB);
    }
    SlotIndex AIdx = CopyIdx.getRegSlot(true);
    LaneBitmask MaskA;
    const SlotIndexes &Indexes = *LIS->getSlotIndexes();
    for (LiveInterval::SubRange &SA : IntA.subranges()) {
      VNInfo *ASubValNo = SA.getVNInfoAt(AIdx);
      // Even if we are dealing with a full copy, some lanes can
      // still be undefined.
      // E.g.,
      // undef A.subLow = ...
      // B = COPY A <== A.subHigh is undefined here and does
      //                not have a value number.
      if (!ASubValNo)
        continue;
      MaskA |= SA.LaneMask;

      IntB.refineSubRanges(
          Allocator, SA.LaneMask,
          [&Allocator, &SA, CopyIdx, ASubValNo,
           &ShrinkB](LiveInterval::SubRange &SR) {
            VNInfo *BSubValNo = SR.empty() ? SR.getNextValue(CopyIdx, Allocator)
                                           : SR.getVNInfoAt(CopyIdx);
            assert(BSubValNo != nullptr);
            auto P = addSegmentsWithValNo(SR, BSubValNo, SA, ASubValNo);
            ShrinkB |= P.second;
            if (P.first)
              BSubValNo->def = ASubValNo->def;
          },
          Indexes, *TRI);
    }
    // Go over all subranges of IntB that have not been covered by IntA,
    // and delete the segments starting at CopyIdx. This can happen if
    // IntA has undef lanes that are defined in IntB.
    for (LiveInterval::SubRange &SB : IntB.subranges()) {
      if ((SB.LaneMask & MaskA).any())
        continue;
      if (LiveRange::Segment *S = SB.getSegmentContaining(CopyIdx))
        if (S->start.getBaseIndex() == CopyIdx.getBaseIndex())
          SB.removeSegment(*S, true);
    }
  }

  BValNo->def = AValNo->def;
  auto P = addSegmentsWithValNo(IntB, BValNo, IntA, AValNo);
  ShrinkB |= P.second;
  LLVM_DEBUG(dbgs() << "\t\textended: " << IntB << '\n');

  LIS->removeVRegDefAt(IntA, AValNo->def);

  LLVM_DEBUG(dbgs() << "\t\ttrimmed:  " << IntA << '\n');
  ++numCommutes;
  return { true, ShrinkB };
}

/// For copy B = A in BB2, if A is defined by A = B in BB0 which is a
/// predecessor of BB2, and if B is not redefined on the way from A = B
/// in BB0 to B = A in BB2, B = A in BB2 is partially redundant if the
/// execution goes through the path from BB0 to BB2. We may move B = A
/// to the predecessor without such reversed copy.
/// So we will transform the program from:
///   BB0:
///      A = B;    BB1:
///       ...         ...
///     /     \      /
///             BB2:
///               ...
///               B = A;
///
/// to:
///
///   BB0:         BB1:
///      A = B;        ...
///       ...          B = A;
///     /     \       /
///             BB2:
///               ...
///
/// A special case is when BB0 and BB2 are the same BB which is the only
/// BB in a loop:
///   BB1:
///        ...
///   BB0/BB2:  ----
///        B = A;   |
///        ...      |
///        A = B;   |
///          |-------
///          |
/// We may hoist B = A from BB0/BB2 to BB1.
///
/// The major preconditions for correctness to remove such partial
/// redundancy include:
/// 1. A in B = A in BB2 is defined by a PHI in BB2, and one operand of
///    the PHI is defined by the reversed copy A = B in BB0.
/// 2. No B is referenced from the start of BB2 to B = A.
/// 3. No B is defined from A = B to the end of BB0.
/// 4. BB1 has only one successor.
///
/// 2 and 4 implicitly ensure B is not live at the end of BB1.
/// 4 guarantees BB2 is hotter than BB1, so we can only move a copy to a
/// colder place, which not only prevent endless loop, but also make sure
/// the movement of copy is beneficial.
bool RegisterCoalescer::removePartialRedundancy(const CoalescerPair &CP,
                                                MachineInstr &CopyMI) {
  assert(!CP.isPhys());
  if (!CopyMI.isFullCopy())
    return false;

  MachineBasicBlock &MBB = *CopyMI.getParent();
  // If this block is the target of an invoke/inlineasm_br, moving the copy into
  // the predecessor is tricker, and we don't handle it.
  if (MBB.isEHPad() || MBB.isInlineAsmBrIndirectTarget())
    return false;

  if (MBB.pred_size() != 2)
    return false;

  LiveInterval &IntA =
      LIS->getInterval(CP.isFlipped() ? CP.getDstReg() : CP.getSrcReg());
  LiveInterval &IntB =
      LIS->getInterval(CP.isFlipped() ? CP.getSrcReg() : CP.getDstReg());

  // A is defined by PHI at the entry of MBB.
  SlotIndex CopyIdx = LIS->getInstructionIndex(CopyMI).getRegSlot(true);
  VNInfo *AValNo = IntA.getVNInfoAt(CopyIdx);
  assert(AValNo && !AValNo->isUnused() && "COPY source not live");
  if (!AValNo->isPHIDef())
    return false;

  // No B is referenced before CopyMI in MBB.
  if (IntB.overlaps(LIS->getMBBStartIdx(&MBB), CopyIdx))
    return false;

  // MBB has two predecessors: one contains A = B so no copy will be inserted
  // for it. The other one will have a copy moved from MBB.
  bool FoundReverseCopy = false;
  MachineBasicBlock *CopyLeftBB = nullptr;
  for (MachineBasicBlock *Pred : MBB.predecessors()) {
    VNInfo *PVal = IntA.getVNInfoBefore(LIS->getMBBEndIdx(Pred));
    MachineInstr *DefMI = LIS->getInstructionFromIndex(PVal->def);
    if (!DefMI || !DefMI->isFullCopy()) {
      CopyLeftBB = Pred;
      continue;
    }
    // Check DefMI is a reverse copy and it is in BB Pred.
    if (DefMI->getOperand(0).getReg() != IntA.reg() ||
        DefMI->getOperand(1).getReg() != IntB.reg() ||
        DefMI->getParent() != Pred) {
      CopyLeftBB = Pred;
      continue;
    }
    // If there is any other def of B after DefMI and before the end of Pred,
    // we need to keep the copy of B = A at the end of Pred if we remove
    // B = A from MBB.
    bool ValB_Changed = false;
    for (auto *VNI : IntB.valnos) {
      if (VNI->isUnused())
        continue;
      if (PVal->def < VNI->def && VNI->def < LIS->getMBBEndIdx(Pred)) {
        ValB_Changed = true;
        break;
      }
    }
    if (ValB_Changed) {
      CopyLeftBB = Pred;
      continue;
    }
    FoundReverseCopy = true;
  }

  // If no reverse copy is found in predecessors, nothing to do.
  if (!FoundReverseCopy)
    return false;

  // If CopyLeftBB is nullptr, it means every predecessor of MBB contains
  // reverse copy, CopyMI can be removed trivially if only IntA/IntB is updated.
  // If CopyLeftBB is not nullptr, move CopyMI from MBB to CopyLeftBB and
  // update IntA/IntB.
  //
  // If CopyLeftBB is not nullptr, ensure CopyLeftBB has a single succ so
  // MBB is hotter than CopyLeftBB.
  if (CopyLeftBB && CopyLeftBB->succ_size() > 1)
    return false;

  // Now (almost sure it's) ok to move copy.
  if (CopyLeftBB) {
    // Position in CopyLeftBB where we should insert new copy.
    auto InsPos = CopyLeftBB->getFirstTerminator();

    // Make sure that B isn't referenced in the terminators (if any) at the end
    // of the predecessor since we're about to insert a new definition of B
    // before them.
    if (InsPos != CopyLeftBB->end()) {
      SlotIndex InsPosIdx = LIS->getInstructionIndex(*InsPos).getRegSlot(true);
      if (IntB.overlaps(InsPosIdx, LIS->getMBBEndIdx(CopyLeftBB)))
        return false;
    }

    LLVM_DEBUG(dbgs() << "\tremovePartialRedundancy: Move the copy to "
                      << printMBBReference(*CopyLeftBB) << '\t' << CopyMI);

    // Insert new copy to CopyLeftBB.
    MachineInstr *NewCopyMI = BuildMI(*CopyLeftBB, InsPos, CopyMI.getDebugLoc(),
                                      TII->get(TargetOpcode::COPY), IntB.reg())
                                  .addReg(IntA.reg());
    SlotIndex NewCopyIdx =
        LIS->InsertMachineInstrInMaps(*NewCopyMI).getRegSlot();
    IntB.createDeadDef(NewCopyIdx, LIS->getVNInfoAllocator());
    for (LiveInterval::SubRange &SR : IntB.subranges())
      SR.createDeadDef(NewCopyIdx, LIS->getVNInfoAllocator());

    // If the newly created Instruction has an address of an instruction that was
    // deleted before (object recycled by the allocator) it needs to be removed from
    // the deleted list.
    ErasedInstrs.erase(NewCopyMI);
  } else {
    LLVM_DEBUG(dbgs() << "\tremovePartialRedundancy: Remove the copy from "
                      << printMBBReference(MBB) << '\t' << CopyMI);
  }

  const bool IsUndefCopy = CopyMI.getOperand(1).isUndef();

  // Remove CopyMI.
  // Note: This is fine to remove the copy before updating the live-ranges.
  // While updating the live-ranges, we only look at slot indices and
  // never go back to the instruction.
  // Mark instructions as deleted.
  deleteInstr(&CopyMI);

  // Update the liveness.
  SmallVector<SlotIndex, 8> EndPoints;
  VNInfo *BValNo = IntB.Query(CopyIdx).valueOutOrDead();
  LIS->pruneValue(*static_cast<LiveRange *>(&IntB), CopyIdx.getRegSlot(),
                  &EndPoints);
  BValNo->markUnused();

  if (IsUndefCopy) {
    // We're introducing an undef phi def, and need to set undef on any users of
    // the previously local def to avoid artifically extending the lifetime
    // through the block.
    for (MachineOperand &MO : MRI->use_nodbg_operands(IntB.reg())) {
      const MachineInstr &MI = *MO.getParent();
      SlotIndex UseIdx = LIS->getInstructionIndex(MI);
      if (!IntB.liveAt(UseIdx))
        MO.setIsUndef(true);
    }
  }

  // Extend IntB to the EndPoints of its original live interval.
  LIS->extendToIndices(IntB, EndPoints);

  // Now, do the same for its subranges.
  for (LiveInterval::SubRange &SR : IntB.subranges()) {
    EndPoints.clear();
    VNInfo *BValNo = SR.Query(CopyIdx).valueOutOrDead();
    assert(BValNo && "All sublanes should be live");
    LIS->pruneValue(SR, CopyIdx.getRegSlot(), &EndPoints);
    BValNo->markUnused();
    // We can have a situation where the result of the original copy is live,
    // but is immediately dead in this subrange, e.g. [336r,336d:0). That makes
    // the copy appear as an endpoint from pruneValue(), but we don't want it
    // to because the copy has been removed.  We can go ahead and remove that
    // endpoint; there is no other situation here that there could be a use at
    // the same place as we know that the copy is a full copy.
    for (unsigned I = 0; I != EndPoints.size(); ) {
      if (SlotIndex::isSameInstr(EndPoints[I], CopyIdx)) {
        EndPoints[I] = EndPoints.back();
        EndPoints.pop_back();
        continue;
      }
      ++I;
    }
    SmallVector<SlotIndex, 8> Undefs;
    IntB.computeSubRangeUndefs(Undefs, SR.LaneMask, *MRI,
                               *LIS->getSlotIndexes());
    LIS->extendToIndices(SR, EndPoints, Undefs);
  }
  // If any dead defs were extended, truncate them.
  shrinkToUses(&IntB);

  // Finally, update the live-range of IntA.
  shrinkToUses(&IntA);
  return true;
}

/// Returns true if @p MI defines the full vreg @p Reg, as opposed to just
/// defining a subregister.
static bool definesFullReg(const MachineInstr &MI, Register Reg) {
  assert(!Reg.isPhysical() && "This code cannot handle physreg aliasing");

  for (const MachineOperand &Op : MI.all_defs()) {
    if (Op.getReg() != Reg)
      continue;
    // Return true if we define the full register or don't care about the value
    // inside other subregisters.
    if (Op.getSubReg() == 0 || Op.isUndef())
      return true;
  }
  return false;
}

bool RegisterCoalescer::reMaterializeTrivialDef(const CoalescerPair &CP,
                                                MachineInstr *CopyMI,
                                                bool &IsDefCopy) {
  IsDefCopy = false;
  Register SrcReg = CP.isFlipped() ? CP.getDstReg() : CP.getSrcReg();
  unsigned SrcIdx = CP.isFlipped() ? CP.getDstIdx() : CP.getSrcIdx();
  Register DstReg = CP.isFlipped() ? CP.getSrcReg() : CP.getDstReg();
  unsigned DstIdx = CP.isFlipped() ? CP.getSrcIdx() : CP.getDstIdx();
  if (SrcReg.isPhysical())
    return false;

  LiveInterval &SrcInt = LIS->getInterval(SrcReg);
  SlotIndex CopyIdx = LIS->getInstructionIndex(*CopyMI);
  VNInfo *ValNo = SrcInt.Query(CopyIdx).valueIn();
  if (!ValNo)
    return false;
  if (ValNo->isPHIDef() || ValNo->isUnused())
    return false;
  MachineInstr *DefMI = LIS->getInstructionFromIndex(ValNo->def);
  if (!DefMI)
    return false;
  if (DefMI->isCopyLike()) {
    IsDefCopy = true;
    return false;
  }
  if (!TII->isAsCheapAsAMove(*DefMI))
    return false;

  SmallVector<Register, 8> NewRegs;
  LiveRangeEdit Edit(&SrcInt, NewRegs, *MF, *LIS, nullptr, this);
  if (!Edit.checkRematerializable(ValNo, DefMI))
    return false;

  if (!definesFullReg(*DefMI, SrcReg))
    return false;
  bool SawStore = false;
  if (!DefMI->isSafeToMove(AA, SawStore))
    return false;
  const MCInstrDesc &MCID = DefMI->getDesc();
  if (MCID.getNumDefs() != 1)
    return false;
  // Only support subregister destinations when the def is read-undef.
  MachineOperand &DstOperand = CopyMI->getOperand(0);
  Register CopyDstReg = DstOperand.getReg();
  if (DstOperand.getSubReg() && !DstOperand.isUndef())
    return false;

  // If both SrcIdx and DstIdx are set, correct rematerialization would widen
  // the register substantially (beyond both source and dest size). This is bad
  // for performance since it can cascade through a function, introducing many
  // extra spills and fills (e.g. ARM can easily end up copying QQQQPR registers
  // around after a few subreg copies).
  if (SrcIdx && DstIdx)
    return false;

  const unsigned DefSubIdx = DefMI->getOperand(0).getSubReg();
  const TargetRegisterClass *DefRC = TII->getRegClass(MCID, 0, TRI, *MF);
  if (!DefMI->isImplicitDef()) {
    if (DstReg.isPhysical()) {
      Register NewDstReg = DstReg;

      unsigned NewDstIdx = TRI->composeSubRegIndices(CP.getSrcIdx(), DefSubIdx);
      if (NewDstIdx)
        NewDstReg = TRI->getSubReg(DstReg, NewDstIdx);

      // Finally, make sure that the physical subregister that will be
      // constructed later is permitted for the instruction.
      if (!DefRC->contains(NewDstReg))
        return false;
    } else {
      // Theoretically, some stack frame reference could exist. Just make sure
      // it hasn't actually happened.
      assert(DstReg.isVirtual() &&
             "Only expect to deal with virtual or physical registers");
    }
  }

  LiveRangeEdit::Remat RM(ValNo);
  RM.OrigMI = DefMI;
  if (!Edit.canRematerializeAt(RM, ValNo, CopyIdx, true))
    return false;

  DebugLoc DL = CopyMI->getDebugLoc();
  MachineBasicBlock *MBB = CopyMI->getParent();
  MachineBasicBlock::iterator MII =
    std::next(MachineBasicBlock::iterator(CopyMI));
  Edit.rematerializeAt(*MBB, MII, DstReg, RM, *TRI, false, SrcIdx, CopyMI);
  MachineInstr &NewMI = *std::prev(MII);
  NewMI.setDebugLoc(DL);

  // In a situation like the following:
  //     %0:subreg = instr              ; DefMI, subreg = DstIdx
  //     %1        = copy %0:subreg ; CopyMI, SrcIdx = 0
  // instead of widening %1 to the register class of %0 simply do:
  //     %1 = instr
  const TargetRegisterClass *NewRC = CP.getNewRC();
  if (DstIdx != 0) {
    MachineOperand &DefMO = NewMI.getOperand(0);
    if (DefMO.getSubReg() == DstIdx) {
      assert(SrcIdx == 0 && CP.isFlipped()
             && "Shouldn't have SrcIdx+DstIdx at this point");
      const TargetRegisterClass *DstRC = MRI->getRegClass(DstReg);
      const TargetRegisterClass *CommonRC =
        TRI->getCommonSubClass(DefRC, DstRC);
      if (CommonRC != nullptr) {
        NewRC = CommonRC;

        // Instruction might contain "undef %0:subreg" as use operand:
        //   %0:subreg = instr op_1, ..., op_N, undef %0:subreg, op_N+2, ...
        //
        // Need to check all operands.
        for (MachineOperand &MO : NewMI.operands()) {
          if (MO.isReg() && MO.getReg() == DstReg && MO.getSubReg() == DstIdx) {
            MO.setSubReg(0);
          }
        }

        DstIdx = 0;
        DefMO.setIsUndef(false); // Only subregs can have def+undef.
      }
    }
  }

  // CopyMI may have implicit operands, save them so that we can transfer them
  // over to the newly materialized instruction after CopyMI is removed.
  SmallVector<MachineOperand, 4> ImplicitOps;
  ImplicitOps.reserve(CopyMI->getNumOperands() -
                      CopyMI->getDesc().getNumOperands());
  for (unsigned I = CopyMI->getDesc().getNumOperands(),
                E = CopyMI->getNumOperands();
       I != E; ++I) {
    MachineOperand &MO = CopyMI->getOperand(I);
    if (MO.isReg()) {
      assert(MO.isImplicit() && "No explicit operands after implicit operands.");
      assert((MO.getReg().isPhysical() ||
              (MO.getSubReg() == 0 && MO.getReg() == DstOperand.getReg())) &&
             "unexpected implicit virtual register def");
      ImplicitOps.push_back(MO);
    }
  }

  CopyMI->eraseFromParent();
  ErasedInstrs.insert(CopyMI);

  // NewMI may have dead implicit defs (E.g. EFLAGS for MOV<bits>r0 on X86).
  // We need to remember these so we can add intervals once we insert
  // NewMI into SlotIndexes.
  //
  // We also expect to have tied implicit-defs of super registers originating
  // from SUBREG_TO_REG, such as:
  // $edi = MOV32r0 implicit-def dead $eflags, implicit-def $rdi
  // undef %0.sub_32bit = MOV32r0 implicit-def dead $eflags, implicit-def %0
  //
  // The implicit-def of the super register may have been reduced to
  // subregisters depending on the uses.

  bool NewMIDefinesFullReg = false;

  SmallVector<MCRegister, 4> NewMIImplDefs;
  for (unsigned i = NewMI.getDesc().getNumOperands(),
                e = NewMI.getNumOperands();
       i != e; ++i) {
    MachineOperand &MO = NewMI.getOperand(i);
    if (MO.isReg() && MO.isDef()) {
      assert(MO.isImplicit());
      if (MO.getReg().isPhysical()) {
        if (MO.getReg() == DstReg)
          NewMIDefinesFullReg = true;

        assert(MO.isImplicit() && MO.getReg().isPhysical() &&
               (MO.isDead() ||
                (DefSubIdx &&
                 ((TRI->getSubReg(MO.getReg(), DefSubIdx) ==
                   MCRegister((unsigned)NewMI.getOperand(0).getReg())) ||
                  TRI->isSubRegisterEq(NewMI.getOperand(0).getReg(),
                                       MO.getReg())))));
        NewMIImplDefs.push_back(MO.getReg().asMCReg());
      } else {
        assert(MO.getReg() == NewMI.getOperand(0).getReg());

        // We're only expecting another def of the main output, so the range
        // should get updated with the regular output range.
        //
        // FIXME: The range updating below probably needs updating to look at
        // the super register if subranges are tracked.
        assert(!MRI->shouldTrackSubRegLiveness(DstReg) &&
               "subrange update for implicit-def of super register may not be "
               "properly handled");
      }
    }
  }

  if (DstReg.isVirtual()) {
    unsigned NewIdx = NewMI.getOperand(0).getSubReg();

    if (DefRC != nullptr) {
      if (NewIdx)
        NewRC = TRI->getMatchingSuperRegClass(NewRC, DefRC, NewIdx);
      else
        NewRC = TRI->getCommonSubClass(NewRC, DefRC);
      assert(NewRC && "subreg chosen for remat incompatible with instruction");
    }
    // Remap subranges to new lanemask and change register class.
    LiveInterval &DstInt = LIS->getInterval(DstReg);
    for (LiveInterval::SubRange &SR : DstInt.subranges()) {
      SR.LaneMask = TRI->composeSubRegIndexLaneMask(DstIdx, SR.LaneMask);
    }
    MRI->setRegClass(DstReg, NewRC);

    // Update machine operands and add flags.
    updateRegDefsUses(DstReg, DstReg, DstIdx);
    NewMI.getOperand(0).setSubReg(NewIdx);
    // updateRegDefUses can add an "undef" flag to the definition, since
    // it will replace DstReg with DstReg.DstIdx. If NewIdx is 0, make
    // sure that "undef" is not set.
    if (NewIdx == 0)
      NewMI.getOperand(0).setIsUndef(false);
    // Add dead subregister definitions if we are defining the whole register
    // but only part of it is live.
    // This could happen if the rematerialization instruction is rematerializing
    // more than actually is used in the register.
    // An example would be:
    // %1 = LOAD CONSTANTS 5, 8 ; Loading both 5 and 8 in different subregs
    // ; Copying only part of the register here, but the rest is undef.
    // %2:sub_16bit<def, read-undef> = COPY %1:sub_16bit
    // ==>
    // ; Materialize all the constants but only using one
    // %2 = LOAD_CONSTANTS 5, 8
    //
    // at this point for the part that wasn't defined before we could have
    // subranges missing the definition.
    if (NewIdx == 0 && DstInt.hasSubRanges()) {
      SlotIndex CurrIdx = LIS->getInstructionIndex(NewMI);
      SlotIndex DefIndex =
          CurrIdx.getRegSlot(NewMI.getOperand(0).isEarlyClobber());
      LaneBitmask MaxMask = MRI->getMaxLaneMaskForVReg(DstReg);
      VNInfo::Allocator& Alloc = LIS->getVNInfoAllocator();
      for (LiveInterval::SubRange &SR : DstInt.subranges()) {
        if (!SR.liveAt(DefIndex))
          SR.createDeadDef(DefIndex, Alloc);
        MaxMask &= ~SR.LaneMask;
      }
      if (MaxMask.any()) {
        LiveInterval::SubRange *SR = DstInt.createSubRange(Alloc, MaxMask);
        SR->createDeadDef(DefIndex, Alloc);
      }
    }

    // Make sure that the subrange for resultant undef is removed
    // For example:
    //   %1:sub1<def,read-undef> = LOAD CONSTANT 1
    //   %2 = COPY %1
    // ==>
    //   %2:sub1<def, read-undef> = LOAD CONSTANT 1
    //     ; Correct but need to remove the subrange for %2:sub0
    //     ; as it is now undef
    if (NewIdx != 0 && DstInt.hasSubRanges()) {
      // The affected subregister segments can be removed.
      SlotIndex CurrIdx = LIS->getInstructionIndex(NewMI);
      LaneBitmask DstMask = TRI->getSubRegIndexLaneMask(NewIdx);
      bool UpdatedSubRanges = false;
      SlotIndex DefIndex =
          CurrIdx.getRegSlot(NewMI.getOperand(0).isEarlyClobber());
      VNInfo::Allocator &Alloc = LIS->getVNInfoAllocator();
      for (LiveInterval::SubRange &SR : DstInt.subranges()) {
        if ((SR.LaneMask & DstMask).none()) {
          LLVM_DEBUG(dbgs()
                     << "Removing undefined SubRange "
                     << PrintLaneMask(SR.LaneMask) << " : " << SR << "\n");

          if (VNInfo *RmValNo = SR.getVNInfoAt(CurrIdx.getRegSlot())) {
            // VNI is in ValNo - remove any segments in this SubRange that have
            // this ValNo
            SR.removeValNo(RmValNo);
          }

          // We may not have a defined value at this point, but still need to
          // clear out any empty subranges tentatively created by
          // updateRegDefUses. The original subrange def may have only undefed
          // some lanes.
          UpdatedSubRanges = true;
        } else {
          // We know that this lane is defined by this instruction,
          // but at this point it may be empty because it is not used by
          // anything. This happens when updateRegDefUses adds the missing
          // lanes. Assign that lane a dead def so that the interferences
          // are properly modeled.
          if (SR.empty())
            SR.createDeadDef(DefIndex, Alloc);
        }
      }
      if (UpdatedSubRanges)
        DstInt.removeEmptySubRanges();
    }
  } else if (NewMI.getOperand(0).getReg() != CopyDstReg) {
    // The New instruction may be defining a sub-register of what's actually
    // been asked for. If so it must implicitly define the whole thing.
    assert(DstReg.isPhysical() &&
           "Only expect virtual or physical registers in remat");
    NewMI.getOperand(0).setIsDead(true);

    if (!NewMIDefinesFullReg) {
      NewMI.addOperand(MachineOperand::CreateReg(
          CopyDstReg, true /*IsDef*/, true /*IsImp*/, false /*IsKill*/));
    }

    // Record small dead def live-ranges for all the subregisters
    // of the destination register.
    // Otherwise, variables that live through may miss some
    // interferences, thus creating invalid allocation.
    // E.g., i386 code:
    // %1 = somedef ; %1 GR8
    // %2 = remat ; %2 GR32
    // CL = COPY %2.sub_8bit
    // = somedef %1 ; %1 GR8
    // =>
    // %1 = somedef ; %1 GR8
    // dead ECX = remat ; implicit-def CL
    // = somedef %1 ; %1 GR8
    // %1 will see the interferences with CL but not with CH since
    // no live-ranges would have been created for ECX.
    // Fix that!
    SlotIndex NewMIIdx = LIS->getInstructionIndex(NewMI);
    for (MCRegUnit Unit : TRI->regunits(NewMI.getOperand(0).getReg()))
      if (LiveRange *LR = LIS->getCachedRegUnit(Unit))
        LR->createDeadDef(NewMIIdx.getRegSlot(), LIS->getVNInfoAllocator());
  }

  NewMI.setRegisterDefReadUndef(NewMI.getOperand(0).getReg());

  // Transfer over implicit operands to the rematerialized instruction.
  for (MachineOperand &MO : ImplicitOps)
    NewMI.addOperand(MO);

  SlotIndex NewMIIdx = LIS->getInstructionIndex(NewMI);
  for (MCRegister Reg : NewMIImplDefs) {
    for (MCRegUnit Unit : TRI->regunits(Reg))
      if (LiveRange *LR = LIS->getCachedRegUnit(Unit))
        LR->createDeadDef(NewMIIdx.getRegSlot(), LIS->getVNInfoAllocator());
  }

  LLVM_DEBUG(dbgs() << "Remat: " << NewMI);
  ++NumReMats;

  // If the virtual SrcReg is completely eliminated, update all DBG_VALUEs
  // to describe DstReg instead.
  if (MRI->use_nodbg_empty(SrcReg)) {
    for (MachineOperand &UseMO :
         llvm::make_early_inc_range(MRI->use_operands(SrcReg))) {
      MachineInstr *UseMI = UseMO.getParent();
      if (UseMI->isDebugInstr()) {
        if (DstReg.isPhysical())
          UseMO.substPhysReg(DstReg, *TRI);
        else
          UseMO.setReg(DstReg);
        // Move the debug value directly after the def of the rematerialized
        // value in DstReg.
        MBB->splice(std::next(NewMI.getIterator()), UseMI->getParent(), UseMI);
        LLVM_DEBUG(dbgs() << "\t\tupdated: " << *UseMI);
      }
    }
  }

  if (ToBeUpdated.count(SrcReg))
    return true;

  unsigned NumCopyUses = 0;
  for (MachineOperand &UseMO : MRI->use_nodbg_operands(SrcReg)) {
    if (UseMO.getParent()->isCopyLike())
      NumCopyUses++;
  }
  if (NumCopyUses < LateRematUpdateThreshold) {
    // The source interval can become smaller because we removed a use.
    shrinkToUses(&SrcInt, &DeadDefs);
    if (!DeadDefs.empty())
      eliminateDeadDefs(&Edit);
  } else {
    ToBeUpdated.insert(SrcReg);
  }
  return true;
}

MachineInstr *RegisterCoalescer::eliminateUndefCopy(MachineInstr *CopyMI) {
  // ProcessImplicitDefs may leave some copies of <undef> values, it only
  // removes local variables. When we have a copy like:
  //
  //   %1 = COPY undef %2
  //
  // We delete the copy and remove the corresponding value number from %1.
  // Any uses of that value number are marked as <undef>.

  // Note that we do not query CoalescerPair here but redo isMoveInstr as the
  // CoalescerPair may have a new register class with adjusted subreg indices
  // at this point.
  Register SrcReg, DstReg;
  unsigned SrcSubIdx = 0, DstSubIdx = 0;
  if(!isMoveInstr(*TRI, CopyMI, SrcReg, DstReg, SrcSubIdx, DstSubIdx))
    return nullptr;

  SlotIndex Idx = LIS->getInstructionIndex(*CopyMI);
  const LiveInterval &SrcLI = LIS->getInterval(SrcReg);
  // CopyMI is undef iff SrcReg is not live before the instruction.
  if (SrcSubIdx != 0 && SrcLI.hasSubRanges()) {
    LaneBitmask SrcMask = TRI->getSubRegIndexLaneMask(SrcSubIdx);
    for (const LiveInterval::SubRange &SR : SrcLI.subranges()) {
      if ((SR.LaneMask & SrcMask).none())
        continue;
      if (SR.liveAt(Idx))
        return nullptr;
    }
  } else if (SrcLI.liveAt(Idx))
    return nullptr;

  // If the undef copy defines a live-out value (i.e. an input to a PHI def),
  // then replace it with an IMPLICIT_DEF.
  LiveInterval &DstLI = LIS->getInterval(DstReg);
  SlotIndex RegIndex = Idx.getRegSlot();
  LiveRange::Segment *Seg = DstLI.getSegmentContaining(RegIndex);
  assert(Seg != nullptr && "No segment for defining instruction");
  VNInfo *V = DstLI.getVNInfoAt(Seg->end);

  // The source interval may also have been on an undef use, in which case the
  // copy introduced a live value.
  if (((V && V->isPHIDef()) || (!V && !DstLI.liveAt(Idx)))) {
    for (unsigned i = CopyMI->getNumOperands(); i != 0; --i) {
      MachineOperand &MO = CopyMI->getOperand(i-1);
      if (MO.isReg()) {
        if (MO.isUse())
          CopyMI->removeOperand(i - 1);
      } else {
        assert(MO.isImm() &&
               CopyMI->getOpcode() == TargetOpcode::SUBREG_TO_REG);
        CopyMI->removeOperand(i-1);
      }
    }

    CopyMI->setDesc(TII->get(TargetOpcode::IMPLICIT_DEF));
    LLVM_DEBUG(dbgs() << "\tReplaced copy of <undef> value with an "
               "implicit def\n");
    return CopyMI;
  }

  // Remove any DstReg segments starting at the instruction.
  LLVM_DEBUG(dbgs() << "\tEliminating copy of <undef> value\n");

  // Remove value or merge with previous one in case of a subregister def.
  if (VNInfo *PrevVNI = DstLI.getVNInfoAt(Idx)) {
    VNInfo *VNI = DstLI.getVNInfoAt(RegIndex);
    DstLI.MergeValueNumberInto(VNI, PrevVNI);

    // The affected subregister segments can be removed.
    LaneBitmask DstMask = TRI->getSubRegIndexLaneMask(DstSubIdx);
    for (LiveInterval::SubRange &SR : DstLI.subranges()) {
      if ((SR.LaneMask & DstMask).none())
        continue;

      VNInfo *SVNI = SR.getVNInfoAt(RegIndex);
      assert(SVNI != nullptr && SlotIndex::isSameInstr(SVNI->def, RegIndex));
      SR.removeValNo(SVNI);
    }
    DstLI.removeEmptySubRanges();
  } else
    LIS->removeVRegDefAt(DstLI, RegIndex);

  // Mark uses as undef.
  for (MachineOperand &MO : MRI->reg_nodbg_operands(DstReg)) {
    if (MO.isDef() /*|| MO.isUndef()*/)
      continue;
    const MachineInstr &MI = *MO.getParent();
    SlotIndex UseIdx = LIS->getInstructionIndex(MI);
    LaneBitmask UseMask = TRI->getSubRegIndexLaneMask(MO.getSubReg());
    bool isLive;
    if (!UseMask.all() && DstLI.hasSubRanges()) {
      isLive = false;
      for (const LiveInterval::SubRange &SR : DstLI.subranges()) {
        if ((SR.LaneMask & UseMask).none())
          continue;
        if (SR.liveAt(UseIdx)) {
          isLive = true;
          break;
        }
      }
    } else
      isLive = DstLI.liveAt(UseIdx);
    if (isLive)
      continue;
    MO.setIsUndef(true);
    LLVM_DEBUG(dbgs() << "\tnew undef: " << UseIdx << '\t' << MI);
  }

  // A def of a subregister may be a use of the other subregisters, so
  // deleting a def of a subregister may also remove uses. Since CopyMI
  // is still part of the function (but about to be erased), mark all
  // defs of DstReg in it as <undef>, so that shrinkToUses would
  // ignore them.
  for (MachineOperand &MO : CopyMI->all_defs())
    if (MO.getReg() == DstReg)
      MO.setIsUndef(true);
  LIS->shrinkToUses(&DstLI);

  return CopyMI;
}

void RegisterCoalescer::addUndefFlag(const LiveInterval &Int, SlotIndex UseIdx,
                                     MachineOperand &MO, unsigned SubRegIdx) {
  LaneBitmask Mask = TRI->getSubRegIndexLaneMask(SubRegIdx);
  if (MO.isDef())
    Mask = ~Mask;
  bool IsUndef = true;
  for (const LiveInterval::SubRange &S : Int.subranges()) {
    if ((S.LaneMask & Mask).none())
      continue;
    if (S.liveAt(UseIdx)) {
      IsUndef = false;
      break;
    }
  }
  if (IsUndef) {
    MO.setIsUndef(true);
    // We found out some subregister use is actually reading an undefined
    // value. In some cases the whole vreg has become undefined at this
    // point so we have to potentially shrink the main range if the
    // use was ending a live segment there.
    LiveQueryResult Q = Int.Query(UseIdx);
    if (Q.valueOut() == nullptr)
      ShrinkMainRange = true;
  }
}

void RegisterCoalescer::updateRegDefsUses(Register SrcReg, Register DstReg,
                                          unsigned SubIdx) {
  bool DstIsPhys = DstReg.isPhysical();
  LiveInterval *DstInt = DstIsPhys ? nullptr : &LIS->getInterval(DstReg);

  if (DstInt && DstInt->hasSubRanges() && DstReg != SrcReg) {
    for (MachineOperand &MO : MRI->reg_operands(DstReg)) {
      unsigned SubReg = MO.getSubReg();
      if (SubReg == 0 || MO.isUndef())
        continue;
      MachineInstr &MI = *MO.getParent();
      if (MI.isDebugInstr())
        continue;
      SlotIndex UseIdx = LIS->getInstructionIndex(MI).getRegSlot(true);
      addUndefFlag(*DstInt, UseIdx, MO, SubReg);
    }
  }

  SmallPtrSet<MachineInstr*, 8> Visited;
  for (MachineRegisterInfo::reg_instr_iterator
       I = MRI->reg_instr_begin(SrcReg), E = MRI->reg_instr_end();
       I != E; ) {
    MachineInstr *UseMI = &*(I++);

    // Each instruction can only be rewritten once because sub-register
    // composition is not always idempotent. When SrcReg != DstReg, rewriting
    // the UseMI operands removes them from the SrcReg use-def chain, but when
    // SrcReg is DstReg we could encounter UseMI twice if it has multiple
    // operands mentioning the virtual register.
    if (SrcReg == DstReg && !Visited.insert(UseMI).second)
      continue;

    SmallVector<unsigned,8> Ops;
    bool Reads, Writes;
    std::tie(Reads, Writes) = UseMI->readsWritesVirtualRegister(SrcReg, &Ops);

    // If SrcReg wasn't read, it may still be the case that DstReg is live-in
    // because SrcReg is a sub-register.
    if (DstInt && !Reads && SubIdx && !UseMI->isDebugInstr())
      Reads = DstInt->liveAt(LIS->getInstructionIndex(*UseMI));

    // Replace SrcReg with DstReg in all UseMI operands.
    for (unsigned Op : Ops) {
      MachineOperand &MO = UseMI->getOperand(Op);

      // Adjust <undef> flags in case of sub-register joins. We don't want to
      // turn a full def into a read-modify-write sub-register def and vice
      // versa.
      if (SubIdx && MO.isDef())
        MO.setIsUndef(!Reads);

      // A subreg use of a partially undef (super) register may be a complete
      // undef use now and then has to be marked that way.
      if (MO.isUse() && !DstIsPhys) {
        unsigned SubUseIdx = TRI->composeSubRegIndices(SubIdx, MO.getSubReg());
        if (SubUseIdx != 0 && MRI->shouldTrackSubRegLiveness(DstReg)) {
          if (!DstInt->hasSubRanges()) {
            BumpPtrAllocator &Allocator = LIS->getVNInfoAllocator();
            LaneBitmask FullMask = MRI->getMaxLaneMaskForVReg(DstInt->reg());
            LaneBitmask UsedLanes = TRI->getSubRegIndexLaneMask(SubIdx);
            LaneBitmask UnusedLanes = FullMask & ~UsedLanes;
            DstInt->createSubRangeFrom(Allocator, UsedLanes, *DstInt);
            // The unused lanes are just empty live-ranges at this point.
            // It is the caller responsibility to set the proper
            // dead segments if there is an actual dead def of the
            // unused lanes. This may happen with rematerialization.
            DstInt->createSubRange(Allocator, UnusedLanes);
          }
          SlotIndex MIIdx = UseMI->isDebugInstr()
            ? LIS->getSlotIndexes()->getIndexBefore(*UseMI)
            : LIS->getInstructionIndex(*UseMI);
          SlotIndex UseIdx = MIIdx.getRegSlot(true);
          addUndefFlag(*DstInt, UseIdx, MO, SubUseIdx);
        }
      }

      if (DstIsPhys)
        MO.substPhysReg(DstReg, *TRI);
      else
        MO.substVirtReg(DstReg, SubIdx, *TRI);
    }

    LLVM_DEBUG({
      dbgs() << "\t\tupdated: ";
      if (!UseMI->isDebugInstr())
        dbgs() << LIS->getInstructionIndex(*UseMI) << "\t";
      dbgs() << *UseMI;
    });
  }
}

bool RegisterCoalescer::canJoinPhys(const CoalescerPair &CP) {
  // Always join simple intervals that are defined by a single copy from a
  // reserved register. This doesn't increase register pressure, so it is
  // always beneficial.
  if (!MRI->isReserved(CP.getDstReg())) {
    LLVM_DEBUG(dbgs() << "\tCan only merge into reserved registers.\n");
    return false;
  }

  LiveInterval &JoinVInt = LIS->getInterval(CP.getSrcReg());
  if (JoinVInt.containsOneValue())
    return true;

  LLVM_DEBUG(
      dbgs() << "\tCannot join complex intervals into reserved register.\n");
  return false;
}

bool RegisterCoalescer::copyValueUndefInPredecessors(
    LiveRange &S, const MachineBasicBlock *MBB, LiveQueryResult SLRQ) {
  for (const MachineBasicBlock *Pred : MBB->predecessors()) {
    SlotIndex PredEnd = LIS->getMBBEndIdx(Pred);
    if (VNInfo *V = S.getVNInfoAt(PredEnd.getPrevSlot())) {
      // If this is a self loop, we may be reading the same value.
      if (V->id != SLRQ.valueOutOrDead()->id)
        return false;
    }
  }

  return true;
}

void RegisterCoalescer::setUndefOnPrunedSubRegUses(LiveInterval &LI,
                                                   Register Reg,
                                                   LaneBitmask PrunedLanes) {
  // If we had other instructions in the segment reading the undef sublane
  // value, we need to mark them with undef.
  for (MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
    unsigned SubRegIdx = MO.getSubReg();
    if (SubRegIdx == 0 || MO.isUndef())
      continue;

    LaneBitmask SubRegMask = TRI->getSubRegIndexLaneMask(SubRegIdx);
    SlotIndex Pos = LIS->getInstructionIndex(*MO.getParent());
    for (LiveInterval::SubRange &S : LI.subranges()) {
      if (!S.liveAt(Pos) && (PrunedLanes & SubRegMask).any()) {
        MO.setIsUndef();
        break;
      }
    }
  }

  LI.removeEmptySubRanges();

  // A def of a subregister may be a use of other register lanes. Replacing
  // such a def with a def of a different register will eliminate the use,
  // and may cause the recorded live range to be larger than the actual
  // liveness in the program IR.
  LIS->shrinkToUses(&LI);
}

bool RegisterCoalescer::joinCopy(
    MachineInstr *CopyMI, bool &Again,
    SmallPtrSetImpl<MachineInstr *> &CurrentErasedInstrs) {
  Again = false;
  LLVM_DEBUG(dbgs() << LIS->getInstructionIndex(*CopyMI) << '\t' << *CopyMI);

  CoalescerPair CP(*TRI);
  if (!CP.setRegisters(CopyMI)) {
    LLVM_DEBUG(dbgs() << "\tNot coalescable.\n");
    return false;
  }

  if (CP.getNewRC()) {
    auto SrcRC = MRI->getRegClass(CP.getSrcReg());
    auto DstRC = MRI->getRegClass(CP.getDstReg());
    unsigned SrcIdx = CP.getSrcIdx();
    unsigned DstIdx = CP.getDstIdx();
    if (CP.isFlipped()) {
      std::swap(SrcIdx, DstIdx);
      std::swap(SrcRC, DstRC);
    }
    if (!TRI->shouldCoalesce(CopyMI, SrcRC, SrcIdx, DstRC, DstIdx,
                             CP.getNewRC(), *LIS)) {
      LLVM_DEBUG(dbgs() << "\tSubtarget bailed on coalescing.\n");
      return false;
    }
  }

  // Dead code elimination. This really should be handled by MachineDCE, but
  // sometimes dead copies slip through, and we can't generate invalid live
  // ranges.
  if (!CP.isPhys() && CopyMI->allDefsAreDead()) {
    LLVM_DEBUG(dbgs() << "\tCopy is dead.\n");
    DeadDefs.push_back(CopyMI);
    eliminateDeadDefs();
    return true;
  }

  // Eliminate undefs.
  if (!CP.isPhys()) {
    // If this is an IMPLICIT_DEF, leave it alone, but don't try to coalesce.
    if (MachineInstr *UndefMI = eliminateUndefCopy(CopyMI)) {
      if (UndefMI->isImplicitDef())
        return false;
      deleteInstr(CopyMI);
      return false;  // Not coalescable.
    }
  }

  // Coalesced copies are normally removed immediately, but transformations
  // like removeCopyByCommutingDef() can inadvertently create identity copies.
  // When that happens, just join the values and remove the copy.
  if (CP.getSrcReg() == CP.getDstReg()) {
    LiveInterval &LI = LIS->getInterval(CP.getSrcReg());
    LLVM_DEBUG(dbgs() << "\tCopy already coalesced: " << LI << '\n');
    const SlotIndex CopyIdx = LIS->getInstructionIndex(*CopyMI);
    LiveQueryResult LRQ = LI.Query(CopyIdx);
    if (VNInfo *DefVNI = LRQ.valueDefined()) {
      VNInfo *ReadVNI = LRQ.valueIn();
      assert(ReadVNI && "No value before copy and no <undef> flag.");
      assert(ReadVNI != DefVNI && "Cannot read and define the same value.");

      // Track incoming undef lanes we need to eliminate from the subrange.
      LaneBitmask PrunedLanes;
      MachineBasicBlock *MBB = CopyMI->getParent();

      // Process subregister liveranges.
      for (LiveInterval::SubRange &S : LI.subranges()) {
        LiveQueryResult SLRQ = S.Query(CopyIdx);
        if (VNInfo *SDefVNI = SLRQ.valueDefined()) {
          if (VNInfo *SReadVNI = SLRQ.valueIn())
            SDefVNI = S.MergeValueNumberInto(SDefVNI, SReadVNI);

          // If this copy introduced an undef subrange from an incoming value,
          // we need to eliminate the undef live in values from the subrange.
          if (copyValueUndefInPredecessors(S, MBB, SLRQ)) {
            LLVM_DEBUG(dbgs() << "Incoming sublane value is undef at copy\n");
            PrunedLanes |= S.LaneMask;
            S.removeValNo(SDefVNI);
          }
        }
      }

      LI.MergeValueNumberInto(DefVNI, ReadVNI);
      if (PrunedLanes.any()) {
        LLVM_DEBUG(dbgs() << "Pruning undef incoming lanes: "
                          << PrunedLanes << '\n');
        setUndefOnPrunedSubRegUses(LI, CP.getSrcReg(), PrunedLanes);
      }

      LLVM_DEBUG(dbgs() << "\tMerged values:          " << LI << '\n');
    }
    deleteInstr(CopyMI);
    return true;
  }

  // Enforce policies.
  if (CP.isPhys()) {
    LLVM_DEBUG(dbgs() << "\tConsidering merging "
                      << printReg(CP.getSrcReg(), TRI) << " with "
                      << printReg(CP.getDstReg(), TRI, CP.getSrcIdx()) << '\n');
    if (!canJoinPhys(CP)) {
      // Before giving up coalescing, if definition of source is defined by
      // trivial computation, try rematerializing it.
      bool IsDefCopy = false;
      if (reMaterializeTrivialDef(CP, CopyMI, IsDefCopy))
        return true;
      if (IsDefCopy)
        Again = true;  // May be possible to coalesce later.
      return false;
    }
  } else {
    // When possible, let DstReg be the larger interval.
    if (!CP.isPartial() && LIS->getInterval(CP.getSrcReg()).size() >
                           LIS->getInterval(CP.getDstReg()).size())
      CP.flip();

    LLVM_DEBUG({
      dbgs() << "\tConsidering merging to "
             << TRI->getRegClassName(CP.getNewRC()) << " with ";
      if (CP.getDstIdx() && CP.getSrcIdx())
        dbgs() << printReg(CP.getDstReg()) << " in "
               << TRI->getSubRegIndexName(CP.getDstIdx()) << " and "
               << printReg(CP.getSrcReg()) << " in "
               << TRI->getSubRegIndexName(CP.getSrcIdx()) << '\n';
      else
        dbgs() << printReg(CP.getSrcReg(), TRI) << " in "
               << printReg(CP.getDstReg(), TRI, CP.getSrcIdx()) << '\n';
    });
  }

  ShrinkMask = LaneBitmask::getNone();
  ShrinkMainRange = false;

  // Okay, attempt to join these two intervals.  On failure, this returns false.
  // Otherwise, if one of the intervals being joined is a physreg, this method
  // always canonicalizes DstInt to be it.  The output "SrcInt" will not have
  // been modified, so we can use this information below to update aliases.
  if (!joinIntervals(CP)) {
    // Coalescing failed.

    // If definition of source is defined by trivial computation, try
    // rematerializing it.
    bool IsDefCopy = false;
    if (reMaterializeTrivialDef(CP, CopyMI, IsDefCopy))
      return true;

    // If we can eliminate the copy without merging the live segments, do so
    // now.
    if (!CP.isPartial() && !CP.isPhys()) {
      bool Changed = adjustCopiesBackFrom(CP, CopyMI);
      bool Shrink = false;
      if (!Changed)
        std::tie(Changed, Shrink) = removeCopyByCommutingDef(CP, CopyMI);
      if (Changed) {
        deleteInstr(CopyMI);
        if (Shrink) {
          Register DstReg = CP.isFlipped() ? CP.getSrcReg() : CP.getDstReg();
          LiveInterval &DstLI = LIS->getInterval(DstReg);
          shrinkToUses(&DstLI);
          LLVM_DEBUG(dbgs() << "\t\tshrunk:   " << DstLI << '\n');
        }
        LLVM_DEBUG(dbgs() << "\tTrivial!\n");
        return true;
      }
    }

    // Try and see if we can partially eliminate the copy by moving the copy to
    // its predecessor.
    if (!CP.isPartial() && !CP.isPhys())
      if (removePartialRedundancy(CP, *CopyMI))
        return true;

    // Otherwise, we are unable to join the intervals.
    LLVM_DEBUG(dbgs() << "\tInterference!\n");
    Again = true;  // May be possible to coalesce later.
    return false;
  }

  // Coalescing to a virtual register that is of a sub-register class of the
  // other. Make sure the resulting register is set to the right register class.
  if (CP.isCrossClass()) {
    ++numCrossRCs;
    MRI->setRegClass(CP.getDstReg(), CP.getNewRC());
  }

  // Removing sub-register copies can ease the register class constraints.
  // Make sure we attempt to inflate the register class of DstReg.
  if (!CP.isPhys() && RegClassInfo.isProperSubClass(CP.getNewRC()))
    InflateRegs.push_back(CP.getDstReg());

  // CopyMI has been erased by joinIntervals at this point. Remove it from
  // ErasedInstrs since copyCoalesceWorkList() won't add a successful join back
  // to the work list. This keeps ErasedInstrs from growing needlessly.
  if (ErasedInstrs.erase(CopyMI))
    // But we may encounter the instruction again in this iteration.
    CurrentErasedInstrs.insert(CopyMI);

  // Rewrite all SrcReg operands to DstReg.
  // Also update DstReg operands to include DstIdx if it is set.
  if (CP.getDstIdx())
    updateRegDefsUses(CP.getDstReg(), CP.getDstReg(), CP.getDstIdx());
  updateRegDefsUses(CP.getSrcReg(), CP.getDstReg(), CP.getSrcIdx());

  // Shrink subregister ranges if necessary.
  if (ShrinkMask.any()) {
    LiveInterval &LI = LIS->getInterval(CP.getDstReg());
    for (LiveInterval::SubRange &S : LI.subranges()) {
      if ((S.LaneMask & ShrinkMask).none())
        continue;
      LLVM_DEBUG(dbgs() << "Shrink LaneUses (Lane " << PrintLaneMask(S.LaneMask)
                        << ")\n");
      LIS->shrinkToUses(S, LI.reg());
      ShrinkMainRange = true;
    }
    LI.removeEmptySubRanges();
  }

  // CP.getSrcReg()'s live interval has been merged into CP.getDstReg's live
  // interval. Since CP.getSrcReg() is in ToBeUpdated set and its live interval
  // is not up-to-date, need to update the merged live interval here.
  if (ToBeUpdated.count(CP.getSrcReg()))
    ShrinkMainRange = true;

  if (ShrinkMainRange) {
    LiveInterval &LI = LIS->getInterval(CP.getDstReg());
    shrinkToUses(&LI);
  }

  // SrcReg is guaranteed to be the register whose live interval that is
  // being merged.
  LIS->removeInterval(CP.getSrcReg());

  // Update regalloc hint.
  TRI->updateRegAllocHint(CP.getSrcReg(), CP.getDstReg(), *MF);

  LLVM_DEBUG({
    dbgs() << "\tSuccess: " << printReg(CP.getSrcReg(), TRI, CP.getSrcIdx())
           << " -> " << printReg(CP.getDstReg(), TRI, CP.getDstIdx()) << '\n';
    dbgs() << "\tResult = ";
    if (CP.isPhys())
      dbgs() << printReg(CP.getDstReg(), TRI);
    else
      dbgs() << LIS->getInterval(CP.getDstReg());
    dbgs() << '\n';
  });

  ++numJoins;
  return true;
}

bool RegisterCoalescer::joinReservedPhysReg(CoalescerPair &CP) {
  Register DstReg = CP.getDstReg();
  Register SrcReg = CP.getSrcReg();
  assert(CP.isPhys() && "Must be a physreg copy");
  assert(MRI->isReserved(DstReg) && "Not a reserved register");
  LiveInterval &RHS = LIS->getInterval(SrcReg);
  LLVM_DEBUG(dbgs() << "\t\tRHS = " << RHS << '\n');

  assert(RHS.containsOneValue() && "Invalid join with reserved register");

  // Optimization for reserved registers like ESP. We can only merge with a
  // reserved physreg if RHS has a single value that is a copy of DstReg.
  // The live range of the reserved register will look like a set of dead defs
  // - we don't properly track the live range of reserved registers.

  // Deny any overlapping intervals.  This depends on all the reserved
  // register live ranges to look like dead defs.
  if (!MRI->isConstantPhysReg(DstReg)) {
    for (MCRegUnit Unit : TRI->regunits(DstReg)) {
      // Abort if not all the regunits are reserved.
      for (MCRegUnitRootIterator RI(Unit, TRI); RI.isValid(); ++RI) {
        if (!MRI->isReserved(*RI))
          return false;
      }
      if (RHS.overlaps(LIS->getRegUnit(Unit))) {
        LLVM_DEBUG(dbgs() << "\t\tInterference: " << printRegUnit(Unit, TRI)
                          << '\n');
        return false;
      }
    }

    // We must also check for overlaps with regmask clobbers.
    BitVector RegMaskUsable;
    if (LIS->checkRegMaskInterference(RHS, RegMaskUsable) &&
        !RegMaskUsable.test(DstReg)) {
      LLVM_DEBUG(dbgs() << "\t\tRegMask interference\n");
      return false;
    }
  }

  // Skip any value computations, we are not adding new values to the
  // reserved register.  Also skip merging the live ranges, the reserved
  // register live range doesn't need to be accurate as long as all the
  // defs are there.

  // Delete the identity copy.
  MachineInstr *CopyMI;
  if (CP.isFlipped()) {
    // Physreg is copied into vreg
    //   %y = COPY %physreg_x
    //   ...  //< no other def of %physreg_x here
    //   use %y
    // =>
    //   ...
    //   use %physreg_x
    CopyMI = MRI->getVRegDef(SrcReg);
    deleteInstr(CopyMI);
  } else {
    // VReg is copied into physreg:
    //   %y = def
    //   ... //< no other def or use of %physreg_x here
    //   %physreg_x = COPY %y
    // =>
    //   %physreg_x = def
    //   ...
    if (!MRI->hasOneNonDBGUse(SrcReg)) {
      LLVM_DEBUG(dbgs() << "\t\tMultiple vreg uses!\n");
      return false;
    }

    if (!LIS->intervalIsInOneMBB(RHS)) {
      LLVM_DEBUG(dbgs() << "\t\tComplex control flow!\n");
      return false;
    }

    MachineInstr &DestMI = *MRI->getVRegDef(SrcReg);
    CopyMI = &*MRI->use_instr_nodbg_begin(SrcReg);
    SlotIndex CopyRegIdx = LIS->getInstructionIndex(*CopyMI).getRegSlot();
    SlotIndex DestRegIdx = LIS->getInstructionIndex(DestMI).getRegSlot();

    if (!MRI->isConstantPhysReg(DstReg)) {
      // We checked above that there are no interfering defs of the physical
      // register. However, for this case, where we intend to move up the def of
      // the physical register, we also need to check for interfering uses.
      SlotIndexes *Indexes = LIS->getSlotIndexes();
      for (SlotIndex SI = Indexes->getNextNonNullIndex(DestRegIdx);
           SI != CopyRegIdx; SI = Indexes->getNextNonNullIndex(SI)) {
        MachineInstr *MI = LIS->getInstructionFromIndex(SI);
        if (MI->readsRegister(DstReg, TRI)) {
          LLVM_DEBUG(dbgs() << "\t\tInterference (read): " << *MI);
          return false;
        }
      }
    }

    // We're going to remove the copy which defines a physical reserved
    // register, so remove its valno, etc.
    LLVM_DEBUG(dbgs() << "\t\tRemoving phys reg def of "
                      << printReg(DstReg, TRI) << " at " << CopyRegIdx << "\n");

    LIS->removePhysRegDefAt(DstReg.asMCReg(), CopyRegIdx);
    deleteInstr(CopyMI);

    // Create a new dead def at the new def location.
    for (MCRegUnit Unit : TRI->regunits(DstReg)) {
      LiveRange &LR = LIS->getRegUnit(Unit);
      LR.createDeadDef(DestRegIdx, LIS->getVNInfoAllocator());
    }
  }

  // We don't track kills for reserved registers.
  MRI->clearKillFlags(CP.getSrcReg());

  return true;
}

//===----------------------------------------------------------------------===//
//                 Interference checking and interval joining
//===----------------------------------------------------------------------===//
//
// In the easiest case, the two live ranges being joined are disjoint, and
// there is no interference to consider. It is quite common, though, to have
// overlapping live ranges, and we need to check if the interference can be
// resolved.
//
// The live range of a single SSA value forms a sub-tree of the dominator tree.
// This means that two SSA values overlap if and only if the def of one value
// is contained in the live range of the other value. As a special case, the
// overlapping values can be defined at the same index.
//
// The interference from an overlapping def can be resolved in these cases:
//
// 1. Coalescable copies. The value is defined by a copy that would become an
//    identity copy after joining SrcReg and DstReg. The copy instruction will
//    be removed, and the value will be merged with the source value.
//
//    There can be several copies back and forth, causing many values to be
//    merged into one. We compute a list of ultimate values in the joined live
//    range as well as a mappings from the old value numbers.
//
// 2. IMPLICIT_DEF. This instruction is only inserted to ensure all PHI
//    predecessors have a live out value. It doesn't cause real interference,
//    and can be merged into the value it overlaps. Like a coalescable copy, it
//    can be erased after joining.
//
// 3. Copy of external value. The overlapping def may be a copy of a value that
//    is already in the other register. This is like a coalescable copy, but
//    the live range of the source register must be trimmed after erasing the
//    copy instruction:
//
//      %src = COPY %ext
//      %dst = COPY %ext  <-- Remove this COPY, trim the live range of %ext.
//
// 4. Clobbering undefined lanes. Vector registers are sometimes built by
//    defining one lane at a time:
//
//      %dst:ssub0<def,read-undef> = FOO
//      %src = BAR
//      %dst:ssub1 = COPY %src
//
//    The live range of %src overlaps the %dst value defined by FOO, but
//    merging %src into %dst:ssub1 is only going to clobber the ssub1 lane
//    which was undef anyway.
//
//    The value mapping is more complicated in this case. The final live range
//    will have different value numbers for both FOO and BAR, but there is no
//    simple mapping from old to new values. It may even be necessary to add
//    new PHI values.
//
// 5. Clobbering dead lanes. A def may clobber a lane of a vector register that
//    is live, but never read. This can happen because we don't compute
//    individual live ranges per lane.
//
//      %dst = FOO
//      %src = BAR
//      %dst:ssub1 = COPY %src
//
//    This kind of interference is only resolved locally. If the clobbered
//    lane value escapes the block, the join is aborted.

namespace {

/// Track information about values in a single virtual register about to be
/// joined. Objects of this class are always created in pairs - one for each
/// side of the CoalescerPair (or one for each lane of a side of the coalescer
/// pair)
class JoinVals {
  /// Live range we work on.
  LiveRange &LR;

  /// (Main) register we work on.
  const Register Reg;

  /// Reg (and therefore the values in this liverange) will end up as
  /// subregister SubIdx in the coalesced register. Either CP.DstIdx or
  /// CP.SrcIdx.
  const unsigned SubIdx;

  /// The LaneMask that this liverange will occupy the coalesced register. May
  /// be smaller than the lanemask produced by SubIdx when merging subranges.
  const LaneBitmask LaneMask;

  /// This is true when joining sub register ranges, false when joining main
  /// ranges.
  const bool SubRangeJoin;

  /// Whether the current LiveInterval tracks subregister liveness.
  const bool TrackSubRegLiveness;

  /// Values that will be present in the final live range.
  SmallVectorImpl<VNInfo*> &NewVNInfo;

  const CoalescerPair &CP;
  LiveIntervals *LIS;
  SlotIndexes *Indexes;
  const TargetRegisterInfo *TRI;

  /// Value number assignments. Maps value numbers in LI to entries in
  /// NewVNInfo. This is suitable for passing to LiveInterval::join().
  SmallVector<int, 8> Assignments;

  public:
  /// Conflict resolution for overlapping values.
  enum ConflictResolution {
    /// No overlap, simply keep this value.
    CR_Keep,

    /// Merge this value into OtherVNI and erase the defining instruction.
    /// Used for IMPLICIT_DEF, coalescable copies, and copies from external
    /// values.
    CR_Erase,

    /// Merge this value into OtherVNI but keep the defining instruction.
    /// This is for the special case where OtherVNI is defined by the same
    /// instruction.
    CR_Merge,

    /// Keep this value, and have it replace OtherVNI where possible. This
    /// complicates value mapping since OtherVNI maps to two different values
    /// before and after this def.
    /// Used when clobbering undefined or dead lanes.
    CR_Replace,

    /// Unresolved conflict. Visit later when all values have been mapped.
    CR_Unresolved,

    /// Unresolvable conflict. Abort the join.
    CR_Impossible
  };

  private:
  /// Per-value info for LI. The lane bit masks are all relative to the final
  /// joined register, so they can be compared directly between SrcReg and
  /// DstReg.
  struct Val {
    ConflictResolution Resolution = CR_Keep;

    /// Lanes written by this def, 0 for unanalyzed values.
    LaneBitmask WriteLanes;

    /// Lanes with defined values in this register. Other lanes are undef and
    /// safe to clobber.
    LaneBitmask ValidLanes;

    /// Value in LI being redefined by this def.
    VNInfo *RedefVNI = nullptr;

    /// Value in the other live range that overlaps this def, if any.
    VNInfo *OtherVNI = nullptr;

    /// Is this value an IMPLICIT_DEF that can be erased?
    ///
    /// IMPLICIT_DEF values should only exist at the end of a basic block that
    /// is a predecessor to a phi-value. These IMPLICIT_DEF instructions can be
    /// safely erased if they are overlapping a live value in the other live
    /// interval.
    ///
    /// Weird control flow graphs and incomplete PHI handling in
    /// ProcessImplicitDefs can very rarely create IMPLICIT_DEF values with
    /// longer live ranges. Such IMPLICIT_DEF values should be treated like
    /// normal values.
    bool ErasableImplicitDef = false;

    /// True when the live range of this value will be pruned because of an
    /// overlapping CR_Replace value in the other live range.
    bool Pruned = false;

    /// True once Pruned above has been computed.
    bool PrunedComputed = false;

    /// True if this value is determined to be identical to OtherVNI
    /// (in valuesIdentical). This is used with CR_Erase where the erased
    /// copy is redundant, i.e. the source value is already the same as
    /// the destination. In such cases the subranges need to be updated
    /// properly. See comment at pruneSubRegValues for more info.
    bool Identical = false;

    Val() = default;

    bool isAnalyzed() const { return WriteLanes.any(); }

    /// Mark this value as an IMPLICIT_DEF which must be kept as if it were an
    /// ordinary value.
    void mustKeepImplicitDef(const TargetRegisterInfo &TRI,
                             const MachineInstr &ImpDef) {
      assert(ImpDef.isImplicitDef());
      ErasableImplicitDef = false;
      ValidLanes = TRI.getSubRegIndexLaneMask(ImpDef.getOperand(0).getSubReg());
    }
  };

  /// One entry per value number in LI.
  SmallVector<Val, 8> Vals;

  /// Compute the bitmask of lanes actually written by DefMI.
  /// Set Redef if there are any partial register definitions that depend on the
  /// previous value of the register.
  LaneBitmask computeWriteLanes(const MachineInstr *DefMI, bool &Redef) const;

  /// Find the ultimate value that VNI was copied from.
  std::pair<const VNInfo *, Register> followCopyChain(const VNInfo *VNI) const;

  bool valuesIdentical(VNInfo *Value0, VNInfo *Value1, const JoinVals &Other) const;

  /// Analyze ValNo in this live range, and set all fields of Vals[ValNo].
  /// Return a conflict resolution when possible, but leave the hard cases as
  /// CR_Unresolved.
  /// Recursively calls computeAssignment() on this and Other, guaranteeing that
  /// both OtherVNI and RedefVNI have been analyzed and mapped before returning.
  /// The recursion always goes upwards in the dominator tree, making loops
  /// impossible.
  ConflictResolution analyzeValue(unsigned ValNo, JoinVals &Other);

  /// Compute the value assignment for ValNo in RI.
  /// This may be called recursively by analyzeValue(), but never for a ValNo on
  /// the stack.
  void computeAssignment(unsigned ValNo, JoinVals &Other);

  /// Assuming ValNo is going to clobber some valid lanes in Other.LR, compute
  /// the extent of the tainted lanes in the block.
  ///
  /// Multiple values in Other.LR can be affected since partial redefinitions
  /// can preserve previously tainted lanes.
  ///
  ///   1 %dst = VLOAD           <-- Define all lanes in %dst
  ///   2 %src = FOO             <-- ValNo to be joined with %dst:ssub0
  ///   3 %dst:ssub1 = BAR       <-- Partial redef doesn't clear taint in ssub0
  ///   4 %dst:ssub0 = COPY %src <-- Conflict resolved, ssub0 wasn't read
  ///
  /// For each ValNo in Other that is affected, add an (EndIndex, TaintedLanes)
  /// entry to TaintedVals.
  ///
  /// Returns false if the tainted lanes extend beyond the basic block.
  bool
  taintExtent(unsigned ValNo, LaneBitmask TaintedLanes, JoinVals &Other,
              SmallVectorImpl<std::pair<SlotIndex, LaneBitmask>> &TaintExtent);

  /// Return true if MI uses any of the given Lanes from Reg.
  /// This does not include partial redefinitions of Reg.
  bool usesLanes(const MachineInstr &MI, Register, unsigned, LaneBitmask) const;

  /// Determine if ValNo is a copy of a value number in LR or Other.LR that will
  /// be pruned:
  ///
  ///   %dst = COPY %src
  ///   %src = COPY %dst  <-- This value to be pruned.
  ///   %dst = COPY %src  <-- This value is a copy of a pruned value.
  bool isPrunedValue(unsigned ValNo, JoinVals &Other);

public:
  JoinVals(LiveRange &LR, Register Reg, unsigned SubIdx, LaneBitmask LaneMask,
           SmallVectorImpl<VNInfo *> &newVNInfo, const CoalescerPair &cp,
           LiveIntervals *lis, const TargetRegisterInfo *TRI, bool SubRangeJoin,
           bool TrackSubRegLiveness)
      : LR(LR), Reg(Reg), SubIdx(SubIdx), LaneMask(LaneMask),
        SubRangeJoin(SubRangeJoin), TrackSubRegLiveness(TrackSubRegLiveness),
        NewVNInfo(newVNInfo), CP(cp), LIS(lis), Indexes(LIS->getSlotIndexes()),
        TRI(TRI), Assignments(LR.getNumValNums(), -1),
        Vals(LR.getNumValNums()) {}

  /// Analyze defs in LR and compute a value mapping in NewVNInfo.
  /// Returns false if any conflicts were impossible to resolve.
  bool mapValues(JoinVals &Other);

  /// Try to resolve conflicts that require all values to be mapped.
  /// Returns false if any conflicts were impossible to resolve.
  bool resolveConflicts(JoinVals &Other);

  /// Prune the live range of values in Other.LR where they would conflict with
  /// CR_Replace values in LR. Collect end points for restoring the live range
  /// after joining.
  void pruneValues(JoinVals &Other, SmallVectorImpl<SlotIndex> &EndPoints,
                   bool changeInstrs);

  /// Removes subranges starting at copies that get removed. This sometimes
  /// happens when undefined subranges are copied around. These ranges contain
  /// no useful information and can be removed.
  void pruneSubRegValues(LiveInterval &LI, LaneBitmask &ShrinkMask);

  /// Pruning values in subranges can lead to removing segments in these
  /// subranges started by IMPLICIT_DEFs. The corresponding segments in
  /// the main range also need to be removed. This function will mark
  /// the corresponding values in the main range as pruned, so that
  /// eraseInstrs can do the final cleanup.
  /// The parameter @p LI must be the interval whose main range is the
  /// live range LR.
  void pruneMainSegments(LiveInterval &LI, bool &ShrinkMainRange);

  /// Erase any machine instructions that have been coalesced away.
  /// Add erased instructions to ErasedInstrs.
  /// Add foreign virtual registers to ShrinkRegs if their live range ended at
  /// the erased instrs.
  void eraseInstrs(SmallPtrSetImpl<MachineInstr*> &ErasedInstrs,
                   SmallVectorImpl<Register> &ShrinkRegs,
                   LiveInterval *LI = nullptr);

  /// Remove liverange defs at places where implicit defs will be removed.
  void removeImplicitDefs();

  /// Get the value assignments suitable for passing to LiveInterval::join.
  const int *getAssignments() const { return Assignments.data(); }

  /// Get the conflict resolution for a value number.
  ConflictResolution getResolution(unsigned Num) const {
    return Vals[Num].Resolution;
  }
};

} // end anonymous namespace

LaneBitmask JoinVals::computeWriteLanes(const MachineInstr *DefMI, bool &Redef)
  const {
  LaneBitmask L;
  for (const MachineOperand &MO : DefMI->all_defs()) {
    if (MO.getReg() != Reg)
      continue;
    L |= TRI->getSubRegIndexLaneMask(
           TRI->composeSubRegIndices(SubIdx, MO.getSubReg()));
    if (MO.readsReg())
      Redef = true;
  }
  return L;
}

std::pair<const VNInfo *, Register>
JoinVals::followCopyChain(const VNInfo *VNI) const {
  Register TrackReg = Reg;

  while (!VNI->isPHIDef()) {
    SlotIndex Def = VNI->def;
    MachineInstr *MI = Indexes->getInstructionFromIndex(Def);
    assert(MI && "No defining instruction");
    if (!MI->isFullCopy())
      return std::make_pair(VNI, TrackReg);
    Register SrcReg = MI->getOperand(1).getReg();
    if (!SrcReg.isVirtual())
      return std::make_pair(VNI, TrackReg);

    const LiveInterval &LI = LIS->getInterval(SrcReg);
    const VNInfo *ValueIn;
    // No subrange involved.
    if (!SubRangeJoin || !LI.hasSubRanges()) {
      LiveQueryResult LRQ = LI.Query(Def);
      ValueIn = LRQ.valueIn();
    } else {
      // Query subranges. Ensure that all matching ones take us to the same def
      // (allowing some of them to be undef).
      ValueIn = nullptr;
      for (const LiveInterval::SubRange &S : LI.subranges()) {
        // Transform lanemask to a mask in the joined live interval.
        LaneBitmask SMask = TRI->composeSubRegIndexLaneMask(SubIdx, S.LaneMask);
        if ((SMask & LaneMask).none())
          continue;
        LiveQueryResult LRQ = S.Query(Def);
        if (!ValueIn) {
          ValueIn = LRQ.valueIn();
          continue;
        }
        if (LRQ.valueIn() && ValueIn != LRQ.valueIn())
          return std::make_pair(VNI, TrackReg);
      }
    }
    if (ValueIn == nullptr) {
      // Reaching an undefined value is legitimate, for example:
      //
      // 1   undef %0.sub1 = ...  ;; %0.sub0 == undef
      // 2   %1 = COPY %0         ;; %1 is defined here.
      // 3   %0 = COPY %1         ;; Now %0.sub0 has a definition,
      //                          ;; but it's equivalent to "undef".
      return std::make_pair(nullptr, SrcReg);
    }
    VNI = ValueIn;
    TrackReg = SrcReg;
  }
  return std::make_pair(VNI, TrackReg);
}

bool JoinVals::valuesIdentical(VNInfo *Value0, VNInfo *Value1,
                               const JoinVals &Other) const {
  const VNInfo *Orig0;
  Register Reg0;
  std::tie(Orig0, Reg0) = followCopyChain(Value0);
  if (Orig0 == Value1 && Reg0 == Other.Reg)
    return true;

  const VNInfo *Orig1;
  Register Reg1;
  std::tie(Orig1, Reg1) = Other.followCopyChain(Value1);
  // If both values are undefined, and the source registers are the same
  // register, the values are identical. Filter out cases where only one
  // value is defined.
  if (Orig0 == nullptr || Orig1 == nullptr)
    return Orig0 == Orig1 && Reg0 == Reg1;

  // The values are equal if they are defined at the same place and use the
  // same register. Note that we cannot compare VNInfos directly as some of
  // them might be from a copy created in mergeSubRangeInto()  while the other
  // is from the original LiveInterval.
  return Orig0->def == Orig1->def && Reg0 == Reg1;
}

JoinVals::ConflictResolution
JoinVals::analyzeValue(unsigned ValNo, JoinVals &Other) {
  Val &V = Vals[ValNo];
  assert(!V.isAnalyzed() && "Value has already been analyzed!");
  VNInfo *VNI = LR.getValNumInfo(ValNo);
  if (VNI->isUnused()) {
    V.WriteLanes = LaneBitmask::getAll();
    return CR_Keep;
  }

  // Get the instruction defining this value, compute the lanes written.
  const MachineInstr *DefMI = nullptr;
  if (VNI->isPHIDef()) {
    // Conservatively assume that all lanes in a PHI are valid.
    LaneBitmask Lanes = SubRangeJoin ? LaneBitmask::getLane(0)
                                     : TRI->getSubRegIndexLaneMask(SubIdx);
    V.ValidLanes = V.WriteLanes = Lanes;
  } else {
    DefMI = Indexes->getInstructionFromIndex(VNI->def);
    assert(DefMI != nullptr);
    if (SubRangeJoin) {
      // We don't care about the lanes when joining subregister ranges.
      V.WriteLanes = V.ValidLanes = LaneBitmask::getLane(0);
      if (DefMI->isImplicitDef()) {
        V.ValidLanes = LaneBitmask::getNone();
        V.ErasableImplicitDef = true;
      }
    } else {
      bool Redef = false;
      V.ValidLanes = V.WriteLanes = computeWriteLanes(DefMI, Redef);

      // If this is a read-modify-write instruction, there may be more valid
      // lanes than the ones written by this instruction.
      // This only covers partial redef operands. DefMI may have normal use
      // operands reading the register. They don't contribute valid lanes.
      //
      // This adds ssub1 to the set of valid lanes in %src:
      //
      //   %src:ssub1 = FOO
      //
      // This leaves only ssub1 valid, making any other lanes undef:
      //
      //   %src:ssub1<def,read-undef> = FOO %src:ssub2
      //
      // The <read-undef> flag on the def operand means that old lane values are
      // not important.
      if (Redef) {
        V.RedefVNI = LR.Query(VNI->def).valueIn();
        assert((TrackSubRegLiveness || V.RedefVNI) &&
               "Instruction is reading nonexistent value");
        if (V.RedefVNI != nullptr) {
          computeAssignment(V.RedefVNI->id, Other);
          V.ValidLanes |= Vals[V.RedefVNI->id].ValidLanes;
        }
      }

      // An IMPLICIT_DEF writes undef values.
      if (DefMI->isImplicitDef()) {
        // We normally expect IMPLICIT_DEF values to be live only until the end
        // of their block. If the value is really live longer and gets pruned in
        // another block, this flag is cleared again.
        //
        // Clearing the valid lanes is deferred until it is sure this can be
        // erased.
        V.ErasableImplicitDef = true;
      }
    }
  }

  // Find the value in Other that overlaps VNI->def, if any.
  LiveQueryResult OtherLRQ = Other.LR.Query(VNI->def);

  // It is possible that both values are defined by the same instruction, or
  // the values are PHIs defined in the same block. When that happens, the two
  // values should be merged into one, but not into any preceding value.
  // The first value defined or visited gets CR_Keep, the other gets CR_Merge.
  if (VNInfo *OtherVNI = OtherLRQ.valueDefined()) {
    assert(SlotIndex::isSameInstr(VNI->def, OtherVNI->def) && "Broken LRQ");

    // One value stays, the other is merged. Keep the earlier one, or the first
    // one we see.
    if (OtherVNI->def < VNI->def)
      Other.computeAssignment(OtherVNI->id, *this);
    else if (VNI->def < OtherVNI->def && OtherLRQ.valueIn()) {
      // This is an early-clobber def overlapping a live-in value in the other
      // register. Not mergeable.
      V.OtherVNI = OtherLRQ.valueIn();
      return CR_Impossible;
    }
    V.OtherVNI = OtherVNI;
    Val &OtherV = Other.Vals[OtherVNI->id];
    // Keep this value, check for conflicts when analyzing OtherVNI. Avoid
    // revisiting OtherVNI->id in JoinVals::computeAssignment() below before it
    // is assigned.
    if (!OtherV.isAnalyzed() || Other.Assignments[OtherVNI->id] == -1)
      return CR_Keep;
    // Both sides have been analyzed now.
    // Allow overlapping PHI values. Any real interference would show up in a
    // predecessor, the PHI itself can't introduce any conflicts.
    if (VNI->isPHIDef())
      return CR_Merge;
    if ((V.ValidLanes & OtherV.ValidLanes).any())
      // Overlapping lanes can't be resolved.
      return CR_Impossible;
    else
      return CR_Merge;
  }

  // No simultaneous def. Is Other live at the def?
  V.OtherVNI = OtherLRQ.valueIn();
  if (!V.OtherVNI)
    // No overlap, no conflict.
    return CR_Keep;

  assert(!SlotIndex::isSameInstr(VNI->def, V.OtherVNI->def) && "Broken LRQ");

  // We have overlapping values, or possibly a kill of Other.
  // Recursively compute assignments up the dominator tree.
  Other.computeAssignment(V.OtherVNI->id, *this);
  Val &OtherV = Other.Vals[V.OtherVNI->id];

  if (OtherV.ErasableImplicitDef) {
    // Check if OtherV is an IMPLICIT_DEF that extends beyond its basic block.
    // This shouldn't normally happen, but ProcessImplicitDefs can leave such
    // IMPLICIT_DEF instructions behind, and there is nothing wrong with it
    // technically.
    //
    // When it happens, treat that IMPLICIT_DEF as a normal value, and don't try
    // to erase the IMPLICIT_DEF instruction.
    //
    // Additionally we must keep an IMPLICIT_DEF if we're redefining an incoming
    // value.

    MachineInstr *OtherImpDef =
        Indexes->getInstructionFromIndex(V.OtherVNI->def);
    MachineBasicBlock *OtherMBB = OtherImpDef->getParent();
    if (DefMI &&
        (DefMI->getParent() != OtherMBB || LIS->isLiveInToMBB(LR, OtherMBB))) {
      LLVM_DEBUG(dbgs() << "IMPLICIT_DEF defined at " << V.OtherVNI->def
                 << " extends into "
                 << printMBBReference(*DefMI->getParent())
                 << ", keeping it.\n");
      OtherV.mustKeepImplicitDef(*TRI, *OtherImpDef);
    } else if (OtherMBB->hasEHPadSuccessor()) {
      // If OtherV is defined in a basic block that has EH pad successors then
      // we get the same problem not just if OtherV is live beyond its basic
      // block, but beyond the last call instruction in its basic block. Handle
      // this case conservatively.
      LLVM_DEBUG(
          dbgs() << "IMPLICIT_DEF defined at " << V.OtherVNI->def
                 << " may be live into EH pad successors, keeping it.\n");
      OtherV.mustKeepImplicitDef(*TRI, *OtherImpDef);
    } else {
      // We deferred clearing these lanes in case we needed to save them
      OtherV.ValidLanes &= ~OtherV.WriteLanes;
    }
  }

  // Allow overlapping PHI values. Any real interference would show up in a
  // predecessor, the PHI itself can't introduce any conflicts.
  if (VNI->isPHIDef())
    return CR_Replace;

  // Check for simple erasable conflicts.
  if (DefMI->isImplicitDef())
    return CR_Erase;

  // Include the non-conflict where DefMI is a coalescable copy that kills
  // OtherVNI. We still want the copy erased and value numbers merged.
  if (CP.isCoalescable(DefMI)) {
    // Some of the lanes copied from OtherVNI may be undef, making them undef
    // here too.
    V.ValidLanes &= ~V.WriteLanes | OtherV.ValidLanes;
    return CR_Erase;
  }

  // This may not be a real conflict if DefMI simply kills Other and defines
  // VNI.
  if (OtherLRQ.isKill() && OtherLRQ.endPoint() <= VNI->def)
    return CR_Keep;

  // Handle the case where VNI and OtherVNI can be proven to be identical:
  //
  //   %other = COPY %ext
  //   %this  = COPY %ext <-- Erase this copy
  //
  if (DefMI->isFullCopy() && !CP.isPartial() &&
      valuesIdentical(VNI, V.OtherVNI, Other)) {
    V.Identical = true;
    return CR_Erase;
  }

  // The remaining checks apply to the lanes, which aren't tracked here.  This
  // was already decided to be OK via the following CR_Replace condition.
  // CR_Replace.
  if (SubRangeJoin)
    return CR_Replace;

  // If the lanes written by this instruction were all undef in OtherVNI, it is
  // still safe to join the live ranges. This can't be done with a simple value
  // mapping, though - OtherVNI will map to multiple values:
  //
  //   1 %dst:ssub0 = FOO                <-- OtherVNI
  //   2 %src = BAR                      <-- VNI
  //   3 %dst:ssub1 = COPY killed %src    <-- Eliminate this copy.
  //   4 BAZ killed %dst
  //   5 QUUX killed %src
  //
  // Here OtherVNI will map to itself in [1;2), but to VNI in [2;5). CR_Replace
  // handles this complex value mapping.
  if ((V.WriteLanes & OtherV.ValidLanes).none())
    return CR_Replace;

  // If the other live range is killed by DefMI and the live ranges are still
  // overlapping, it must be because we're looking at an early clobber def:
  //
  //   %dst<def,early-clobber> = ASM killed %src
  //
  // In this case, it is illegal to merge the two live ranges since the early
  // clobber def would clobber %src before it was read.
  if (OtherLRQ.isKill()) {
    // This case where the def doesn't overlap the kill is handled above.
    assert(VNI->def.isEarlyClobber() &&
           "Only early clobber defs can overlap a kill");
    return CR_Impossible;
  }

  // VNI is clobbering live lanes in OtherVNI, but there is still the
  // possibility that no instructions actually read the clobbered lanes.
  // If we're clobbering all the lanes in OtherVNI, at least one must be read.
  // Otherwise Other.RI wouldn't be live here.
  if ((TRI->getSubRegIndexLaneMask(Other.SubIdx) & ~V.WriteLanes).none())
    return CR_Impossible;

  if (TrackSubRegLiveness) {
    auto &OtherLI = LIS->getInterval(Other.Reg);
    // If OtherVNI does not have subranges, it means all the lanes of OtherVNI
    // share the same live range, so we just need to check whether they have
    // any conflict bit in their LaneMask.
    if (!OtherLI.hasSubRanges()) {
      LaneBitmask OtherMask = TRI->getSubRegIndexLaneMask(Other.SubIdx);
      return (OtherMask & V.WriteLanes).none() ? CR_Replace : CR_Impossible;
    }

    // If we are clobbering some active lanes of OtherVNI at VNI->def, it is
    // impossible to resolve the conflict. Otherwise, we can just replace
    // OtherVNI because of no real conflict.
    for (LiveInterval::SubRange &OtherSR : OtherLI.subranges()) {
      LaneBitmask OtherMask =
          TRI->composeSubRegIndexLaneMask(Other.SubIdx, OtherSR.LaneMask);
      if ((OtherMask & V.WriteLanes).none())
        continue;

      auto OtherSRQ = OtherSR.Query(VNI->def);
      if (OtherSRQ.valueIn() && OtherSRQ.endPoint() > VNI->def) {
        // VNI is clobbering some lanes of OtherVNI, they have real conflict.
        return CR_Impossible;
      }
    }

    // VNI is NOT clobbering any lane of OtherVNI, just replace OtherVNI.
    return CR_Replace;
  }

  // We need to verify that no instructions are reading the clobbered lanes.
  // To save compile time, we'll only check that locally. Don't allow the
  // tainted value to escape the basic block.
  MachineBasicBlock *MBB = Indexes->getMBBFromIndex(VNI->def);
  if (OtherLRQ.endPoint() >= Indexes->getMBBEndIdx(MBB))
    return CR_Impossible;

  // There are still some things that could go wrong besides clobbered lanes
  // being read, for example OtherVNI may be only partially redefined in MBB,
  // and some clobbered lanes could escape the block. Save this analysis for
  // resolveConflicts() when all values have been mapped. We need to know
  // RedefVNI and WriteLanes for any later defs in MBB, and we can't compute
  // that now - the recursive analyzeValue() calls must go upwards in the
  // dominator tree.
  return CR_Unresolved;
}

void JoinVals::computeAssignment(unsigned ValNo, JoinVals &Other) {
  Val &V = Vals[ValNo];
  if (V.isAnalyzed()) {
    // Recursion should always move up the dominator tree, so ValNo is not
    // supposed to reappear before it has been assigned.
    assert(Assignments[ValNo] != -1 && "Bad recursion?");
    return;
  }
  switch ((V.Resolution = analyzeValue(ValNo, Other))) {
  case CR_Erase:
  case CR_Merge:
    // Merge this ValNo into OtherVNI.
    assert(V.OtherVNI && "OtherVNI not assigned, can't merge.");
    assert(Other.Vals[V.OtherVNI->id].isAnalyzed() && "Missing recursion");
    Assignments[ValNo] = Other.Assignments[V.OtherVNI->id];
    LLVM_DEBUG(dbgs() << "\t\tmerge " << printReg(Reg) << ':' << ValNo << '@'
                      << LR.getValNumInfo(ValNo)->def << " into "
                      << printReg(Other.Reg) << ':' << V.OtherVNI->id << '@'
                      << V.OtherVNI->def << " --> @"
                      << NewVNInfo[Assignments[ValNo]]->def << '\n');
    break;
  case CR_Replace:
  case CR_Unresolved: {
    // The other value is going to be pruned if this join is successful.
    assert(V.OtherVNI && "OtherVNI not assigned, can't prune");
    Val &OtherV = Other.Vals[V.OtherVNI->id];
    OtherV.Pruned = true;
    [[fallthrough]];
  }
  default:
    // This value number needs to go in the final joined live range.
    Assignments[ValNo] = NewVNInfo.size();
    NewVNInfo.push_back(LR.getValNumInfo(ValNo));
    break;
  }
}

bool JoinVals::mapValues(JoinVals &Other) {
  for (unsigned i = 0, e = LR.getNumValNums(); i != e; ++i) {
    computeAssignment(i, Other);
    if (Vals[i].Resolution == CR_Impossible) {
      LLVM_DEBUG(dbgs() << "\t\tinterference at " << printReg(Reg) << ':' << i
                        << '@' << LR.getValNumInfo(i)->def << '\n');
      return false;
    }
  }
  return true;
}

bool JoinVals::
taintExtent(unsigned ValNo, LaneBitmask TaintedLanes, JoinVals &Other,
            SmallVectorImpl<std::pair<SlotIndex, LaneBitmask>> &TaintExtent) {
  VNInfo *VNI = LR.getValNumInfo(ValNo);
  MachineBasicBlock *MBB = Indexes->getMBBFromIndex(VNI->def);
  SlotIndex MBBEnd = Indexes->getMBBEndIdx(MBB);

  // Scan Other.LR from VNI.def to MBBEnd.
  LiveInterval::iterator OtherI = Other.LR.find(VNI->def);
  assert(OtherI != Other.LR.end() && "No conflict?");
  do {
    // OtherI is pointing to a tainted value. Abort the join if the tainted
    // lanes escape the block.
    SlotIndex End = OtherI->end;
    if (End >= MBBEnd) {
      LLVM_DEBUG(dbgs() << "\t\ttaints global " << printReg(Other.Reg) << ':'
                        << OtherI->valno->id << '@' << OtherI->start << '\n');
      return false;
    }
    LLVM_DEBUG(dbgs() << "\t\ttaints local " << printReg(Other.Reg) << ':'
                      << OtherI->valno->id << '@' << OtherI->start << " to "
                      << End << '\n');
    // A dead def is not a problem.
    if (End.isDead())
      break;
    TaintExtent.push_back(std::make_pair(End, TaintedLanes));

    // Check for another def in the MBB.
    if (++OtherI == Other.LR.end() || OtherI->start >= MBBEnd)
      break;

    // Lanes written by the new def are no longer tainted.
    const Val &OV = Other.Vals[OtherI->valno->id];
    TaintedLanes &= ~OV.WriteLanes;
    if (!OV.RedefVNI)
      break;
  } while (TaintedLanes.any());
  return true;
}

bool JoinVals::usesLanes(const MachineInstr &MI, Register Reg, unsigned SubIdx,
                         LaneBitmask Lanes) const {
  if (MI.isDebugOrPseudoInstr())
    return false;
  for (const MachineOperand &MO : MI.all_uses()) {
    if (MO.getReg() != Reg)
      continue;
    if (!MO.readsReg())
      continue;
    unsigned S = TRI->composeSubRegIndices(SubIdx, MO.getSubReg());
    if ((Lanes & TRI->getSubRegIndexLaneMask(S)).any())
      return true;
  }
  return false;
}

bool JoinVals::resolveConflicts(JoinVals &Other) {
  for (unsigned i = 0, e = LR.getNumValNums(); i != e; ++i) {
    Val &V = Vals[i];
    assert(V.Resolution != CR_Impossible && "Unresolvable conflict");
    if (V.Resolution != CR_Unresolved)
      continue;
    LLVM_DEBUG(dbgs() << "\t\tconflict at " << printReg(Reg) << ':' << i << '@'
                      << LR.getValNumInfo(i)->def
                      << ' ' << PrintLaneMask(LaneMask) << '\n');
    if (SubRangeJoin)
      return false;

    ++NumLaneConflicts;
    assert(V.OtherVNI && "Inconsistent conflict resolution.");
    VNInfo *VNI = LR.getValNumInfo(i);
    const Val &OtherV = Other.Vals[V.OtherVNI->id];

    // VNI is known to clobber some lanes in OtherVNI. If we go ahead with the
    // join, those lanes will be tainted with a wrong value. Get the extent of
    // the tainted lanes.
    LaneBitmask TaintedLanes = V.WriteLanes & OtherV.ValidLanes;
    SmallVector<std::pair<SlotIndex, LaneBitmask>, 8> TaintExtent;
    if (!taintExtent(i, TaintedLanes, Other, TaintExtent))
      // Tainted lanes would extend beyond the basic block.
      return false;

    assert(!TaintExtent.empty() && "There should be at least one conflict.");

    // Now look at the instructions from VNI->def to TaintExtent (inclusive).
    MachineBasicBlock *MBB = Indexes->getMBBFromIndex(VNI->def);
    MachineBasicBlock::iterator MI = MBB->begin();
    if (!VNI->isPHIDef()) {
      MI = Indexes->getInstructionFromIndex(VNI->def);
      if (!VNI->def.isEarlyClobber()) {
        // No need to check the instruction defining VNI for reads.
        ++MI;
      }
    }
    assert(!SlotIndex::isSameInstr(VNI->def, TaintExtent.front().first) &&
           "Interference ends on VNI->def. Should have been handled earlier");
    MachineInstr *LastMI =
      Indexes->getInstructionFromIndex(TaintExtent.front().first);
    assert(LastMI && "Range must end at a proper instruction");
    unsigned TaintNum = 0;
    while (true) {
      assert(MI != MBB->end() && "Bad LastMI");
      if (usesLanes(*MI, Other.Reg, Other.SubIdx, TaintedLanes)) {
        LLVM_DEBUG(dbgs() << "\t\ttainted lanes used by: " << *MI);
        return false;
      }
      // LastMI is the last instruction to use the current value.
      if (&*MI == LastMI) {
        if (++TaintNum == TaintExtent.size())
          break;
        LastMI = Indexes->getInstructionFromIndex(TaintExtent[TaintNum].first);
        assert(LastMI && "Range must end at a proper instruction");
        TaintedLanes = TaintExtent[TaintNum].second;
      }
      ++MI;
    }

    // The tainted lanes are unused.
    V.Resolution = CR_Replace;
    ++NumLaneResolves;
  }
  return true;
}

bool JoinVals::isPrunedValue(unsigned ValNo, JoinVals &Other) {
  Val &V = Vals[ValNo];
  if (V.Pruned || V.PrunedComputed)
    return V.Pruned;

  if (V.Resolution != CR_Erase && V.Resolution != CR_Merge)
    return V.Pruned;

  // Follow copies up the dominator tree and check if any intermediate value
  // has been pruned.
  V.PrunedComputed = true;
  V.Pruned = Other.isPrunedValue(V.OtherVNI->id, *this);
  return V.Pruned;
}

void JoinVals::pruneValues(JoinVals &Other,
                           SmallVectorImpl<SlotIndex> &EndPoints,
                           bool changeInstrs) {
  for (unsigned i = 0, e = LR.getNumValNums(); i != e; ++i) {
    SlotIndex Def = LR.getValNumInfo(i)->def;
    switch (Vals[i].Resolution) {
    case CR_Keep:
      break;
    case CR_Replace: {
      // This value takes precedence over the value in Other.LR.
      LIS->pruneValue(Other.LR, Def, &EndPoints);
      // Check if we're replacing an IMPLICIT_DEF value. The IMPLICIT_DEF
      // instructions are only inserted to provide a live-out value for PHI
      // predecessors, so the instruction should simply go away once its value
      // has been replaced.
      Val &OtherV = Other.Vals[Vals[i].OtherVNI->id];
      bool EraseImpDef = OtherV.ErasableImplicitDef &&
                         OtherV.Resolution == CR_Keep;
      if (!Def.isBlock()) {
        if (changeInstrs) {
          // Remove <def,read-undef> flags. This def is now a partial redef.
          // Also remove dead flags since the joined live range will
          // continue past this instruction.
          for (MachineOperand &MO :
               Indexes->getInstructionFromIndex(Def)->operands()) {
            if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
              if (MO.getSubReg() != 0 && MO.isUndef() && !EraseImpDef)
                MO.setIsUndef(false);
              MO.setIsDead(false);
            }
          }
        }
        // This value will reach instructions below, but we need to make sure
        // the live range also reaches the instruction at Def.
        if (!EraseImpDef)
          EndPoints.push_back(Def);
      }
      LLVM_DEBUG(dbgs() << "\t\tpruned " << printReg(Other.Reg) << " at " << Def
                        << ": " << Other.LR << '\n');
      break;
    }
    case CR_Erase:
    case CR_Merge:
      if (isPrunedValue(i, Other)) {
        // This value is ultimately a copy of a pruned value in LR or Other.LR.
        // We can no longer trust the value mapping computed by
        // computeAssignment(), the value that was originally copied could have
        // been replaced.
        LIS->pruneValue(LR, Def, &EndPoints);
        LLVM_DEBUG(dbgs() << "\t\tpruned all of " << printReg(Reg) << " at "
                          << Def << ": " << LR << '\n');
      }
      break;
    case CR_Unresolved:
    case CR_Impossible:
      llvm_unreachable("Unresolved conflicts");
    }
  }
}

// Check if the segment consists of a copied live-through value (i.e. the copy
// in the block only extended the liveness, of an undef value which we may need
// to handle).
static bool isLiveThrough(const LiveQueryResult Q) {
  return Q.valueIn() && Q.valueIn()->isPHIDef() && Q.valueIn() == Q.valueOut();
}

/// Consider the following situation when coalescing the copy between
/// %31 and %45 at 800. (The vertical lines represent live range segments.)
///
///                              Main range         Subrange 0004 (sub2)
///                              %31    %45           %31    %45
///  544    %45 = COPY %28               +                    +
///                                      | v1                 | v1
///  560B bb.1:                          +                    +
///  624        = %45.sub2               | v2                 | v2
///  800    %31 = COPY %45        +      +             +      +
///                               | v0                 | v0
///  816    %31.sub1 = ...        +                    |
///  880    %30 = COPY %31        | v1                 +
///  928    %45 = COPY %30        |      +                    +
///                               |      | v0                 | v0  <--+
///  992B   ; backedge -> bb.1    |      +                    +        |
/// 1040        = %31.sub0        +                                    |
///                                                 This value must remain
///                                                 live-out!
///
/// Assuming that %31 is coalesced into %45, the copy at 928 becomes
/// redundant, since it copies the value from %45 back into it. The
/// conflict resolution for the main range determines that %45.v0 is
/// to be erased, which is ok since %31.v1 is identical to it.
/// The problem happens with the subrange for sub2: it has to be live
/// on exit from the block, but since 928 was actually a point of
/// definition of %45.sub2, %45.sub2 was not live immediately prior
/// to that definition. As a result, when 928 was erased, the value v0
/// for %45.sub2 was pruned in pruneSubRegValues. Consequently, an
/// IMPLICIT_DEF was inserted as a "backedge" definition for %45.sub2,
/// providing an incorrect value to the use at 624.
///
/// Since the main-range values %31.v1 and %45.v0 were proved to be
/// identical, the corresponding values in subranges must also be the
/// same. A redundant copy is removed because it's not needed, and not
/// because it copied an undefined value, so any liveness that originated
/// from that copy cannot disappear. When pruning a value that started
/// at the removed copy, the corresponding identical value must be
/// extended to replace it.
void JoinVals::pruneSubRegValues(LiveInterval &LI, LaneBitmask &ShrinkMask) {
  // Look for values being erased.
  bool DidPrune = false;
  for (unsigned i = 0, e = LR.getNumValNums(); i != e; ++i) {
    Val &V = Vals[i];
    // We should trigger in all cases in which eraseInstrs() does something.
    // match what eraseInstrs() is doing, print a message so
    if (V.Resolution != CR_Erase &&
        (V.Resolution != CR_Keep || !V.ErasableImplicitDef || !V.Pruned))
      continue;

    // Check subranges at the point where the copy will be removed.
    SlotIndex Def = LR.getValNumInfo(i)->def;
    SlotIndex OtherDef;
    if (V.Identical)
      OtherDef = V.OtherVNI->def;

    // Print message so mismatches with eraseInstrs() can be diagnosed.
    LLVM_DEBUG(dbgs() << "\t\tExpecting instruction removal at " << Def
                      << '\n');
    for (LiveInterval::SubRange &S : LI.subranges()) {
      LiveQueryResult Q = S.Query(Def);

      // If a subrange starts at the copy then an undefined value has been
      // copied and we must remove that subrange value as well.
      VNInfo *ValueOut = Q.valueOutOrDead();
      if (ValueOut != nullptr && (Q.valueIn() == nullptr ||
                                  (V.Identical && V.Resolution == CR_Erase &&
                                   ValueOut->def == Def))) {
        LLVM_DEBUG(dbgs() << "\t\tPrune sublane " << PrintLaneMask(S.LaneMask)
                          << " at " << Def << "\n");
        SmallVector<SlotIndex,8> EndPoints;
        LIS->pruneValue(S, Def, &EndPoints);
        DidPrune = true;
        // Mark value number as unused.
        ValueOut->markUnused();

        if (V.Identical && S.Query(OtherDef).valueOutOrDead()) {
          // If V is identical to V.OtherVNI (and S was live at OtherDef),
          // then we can't simply prune V from S. V needs to be replaced
          // with V.OtherVNI.
          LIS->extendToIndices(S, EndPoints);
        }

        // We may need to eliminate the subrange if the copy introduced a live
        // out undef value.
        if (ValueOut->isPHIDef())
          ShrinkMask |= S.LaneMask;
        continue;
      }

      // If a subrange ends at the copy, then a value was copied but only
      // partially used later. Shrink the subregister range appropriately.
      //
      // Ultimately this calls shrinkToUses, so assuming ShrinkMask is
      // conservatively correct.
      if ((Q.valueIn() != nullptr && Q.valueOut() == nullptr) ||
          (V.Resolution == CR_Erase && isLiveThrough(Q))) {
        LLVM_DEBUG(dbgs() << "\t\tDead uses at sublane "
                          << PrintLaneMask(S.LaneMask) << " at " << Def
                          << "\n");
        ShrinkMask |= S.LaneMask;
      }
    }
  }
  if (DidPrune)
    LI.removeEmptySubRanges();
}

/// Check if any of the subranges of @p LI contain a definition at @p Def.
static bool isDefInSubRange(LiveInterval &LI, SlotIndex Def) {
  for (LiveInterval::SubRange &SR : LI.subranges()) {
    if (VNInfo *VNI = SR.Query(Def).valueOutOrDead())
      if (VNI->def == Def)
        return true;
  }
  return false;
}

void JoinVals::pruneMainSegments(LiveInterval &LI, bool &ShrinkMainRange) {
  assert(&static_cast<LiveRange&>(LI) == &LR);

  for (unsigned i = 0, e = LR.getNumValNums(); i != e; ++i) {
    if (Vals[i].Resolution != CR_Keep)
      continue;
    VNInfo *VNI = LR.getValNumInfo(i);
    if (VNI->isUnused() || VNI->isPHIDef() || isDefInSubRange(LI, VNI->def))
      continue;
    Vals[i].Pruned = true;
    ShrinkMainRange = true;
  }
}

void JoinVals::removeImplicitDefs() {
  for (unsigned i = 0, e = LR.getNumValNums(); i != e; ++i) {
    Val &V = Vals[i];
    if (V.Resolution != CR_Keep || !V.ErasableImplicitDef || !V.Pruned)
      continue;

    VNInfo *VNI = LR.getValNumInfo(i);
    VNI->markUnused();
    LR.removeValNo(VNI);
  }
}

void JoinVals::eraseInstrs(SmallPtrSetImpl<MachineInstr*> &ErasedInstrs,
                           SmallVectorImpl<Register> &ShrinkRegs,
                           LiveInterval *LI) {
  for (unsigned i = 0, e = LR.getNumValNums(); i != e; ++i) {
    // Get the def location before markUnused() below invalidates it.
    VNInfo *VNI = LR.getValNumInfo(i);
    SlotIndex Def = VNI->def;
    switch (Vals[i].Resolution) {
    case CR_Keep: {
      // If an IMPLICIT_DEF value is pruned, it doesn't serve a purpose any
      // longer. The IMPLICIT_DEF instructions are only inserted by
      // PHIElimination to guarantee that all PHI predecessors have a value.
      if (!Vals[i].ErasableImplicitDef || !Vals[i].Pruned)
        break;
      // Remove value number i from LR.
      // For intervals with subranges, removing a segment from the main range
      // may require extending the previous segment: for each definition of
      // a subregister, there will be a corresponding def in the main range.
      // That def may fall in the middle of a segment from another subrange.
      // In such cases, removing this def from the main range must be
      // complemented by extending the main range to account for the liveness
      // of the other subrange.
      // The new end point of the main range segment to be extended.
      SlotIndex NewEnd;
      if (LI != nullptr) {
        LiveRange::iterator I = LR.FindSegmentContaining(Def);
        assert(I != LR.end());
        // Do not extend beyond the end of the segment being removed.
        // The segment may have been pruned in preparation for joining
        // live ranges.
        NewEnd = I->end;
      }

      LR.removeValNo(VNI);
      // Note that this VNInfo is reused and still referenced in NewVNInfo,
      // make it appear like an unused value number.
      VNI->markUnused();

      if (LI != nullptr && LI->hasSubRanges()) {
        assert(static_cast<LiveRange*>(LI) == &LR);
        // Determine the end point based on the subrange information:
        // minimum of (earliest def of next segment,
        //             latest end point of containing segment)
        SlotIndex ED, LE;
        for (LiveInterval::SubRange &SR : LI->subranges()) {
          LiveRange::iterator I = SR.find(Def);
          if (I == SR.end())
            continue;
          if (I->start > Def)
            ED = ED.isValid() ? std::min(ED, I->start) : I->start;
          else
            LE = LE.isValid() ? std::max(LE, I->end) : I->end;
        }
        if (LE.isValid())
          NewEnd = std::min(NewEnd, LE);
        if (ED.isValid())
          NewEnd = std::min(NewEnd, ED);

        // We only want to do the extension if there was a subrange that
        // was live across Def.
        if (LE.isValid()) {
          LiveRange::iterator S = LR.find(Def);
          if (S != LR.begin())
            std::prev(S)->end = NewEnd;
        }
      }
      LLVM_DEBUG({
        dbgs() << "\t\tremoved " << i << '@' << Def << ": " << LR << '\n';
        if (LI != nullptr)
          dbgs() << "\t\t  LHS = " << *LI << '\n';
      });
      [[fallthrough]];
    }

    case CR_Erase: {
      MachineInstr *MI = Indexes->getInstructionFromIndex(Def);
      assert(MI && "No instruction to erase");
      if (MI->isCopy()) {
        Register Reg = MI->getOperand(1).getReg();
        if (Reg.isVirtual() && Reg != CP.getSrcReg() && Reg != CP.getDstReg())
          ShrinkRegs.push_back(Reg);
      }
      ErasedInstrs.insert(MI);
      LLVM_DEBUG(dbgs() << "\t\terased:\t" << Def << '\t' << *MI);
      LIS->RemoveMachineInstrFromMaps(*MI);
      MI->eraseFromParent();
      break;
    }
    default:
      break;
    }
  }
}

void RegisterCoalescer::joinSubRegRanges(LiveRange &LRange, LiveRange &RRange,
                                         LaneBitmask LaneMask,
                                         const CoalescerPair &CP) {
  SmallVector<VNInfo*, 16> NewVNInfo;
  JoinVals RHSVals(RRange, CP.getSrcReg(), CP.getSrcIdx(), LaneMask,
                   NewVNInfo, CP, LIS, TRI, true, true);
  JoinVals LHSVals(LRange, CP.getDstReg(), CP.getDstIdx(), LaneMask,
                   NewVNInfo, CP, LIS, TRI, true, true);

  // Compute NewVNInfo and resolve conflicts (see also joinVirtRegs())
  // We should be able to resolve all conflicts here as we could successfully do
  // it on the mainrange already. There is however a problem when multiple
  // ranges get mapped to the "overflow" lane mask bit which creates unexpected
  // interferences.
  if (!LHSVals.mapValues(RHSVals) || !RHSVals.mapValues(LHSVals)) {
    // We already determined that it is legal to merge the intervals, so this
    // should never fail.
    llvm_unreachable("*** Couldn't join subrange!\n");
  }
  if (!LHSVals.resolveConflicts(RHSVals) ||
      !RHSVals.resolveConflicts(LHSVals)) {
    // We already determined that it is legal to merge the intervals, so this
    // should never fail.
    llvm_unreachable("*** Couldn't join subrange!\n");
  }

  // The merging algorithm in LiveInterval::join() can't handle conflicting
  // value mappings, so we need to remove any live ranges that overlap a
  // CR_Replace resolution. Collect a set of end points that can be used to
  // restore the live range after joining.
  SmallVector<SlotIndex, 8> EndPoints;
  LHSVals.pruneValues(RHSVals, EndPoints, false);
  RHSVals.pruneValues(LHSVals, EndPoints, false);

  LHSVals.removeImplicitDefs();
  RHSVals.removeImplicitDefs();

  LRange.verify();
  RRange.verify();

  // Join RRange into LHS.
  LRange.join(RRange, LHSVals.getAssignments(), RHSVals.getAssignments(),
              NewVNInfo);

  LLVM_DEBUG(dbgs() << "\t\tjoined lanes: " << PrintLaneMask(LaneMask)
                    << ' ' << LRange << "\n");
  if (EndPoints.empty())
    return;

  // Recompute the parts of the live range we had to remove because of
  // CR_Replace conflicts.
  LLVM_DEBUG({
    dbgs() << "\t\trestoring liveness to " << EndPoints.size() << " points: ";
    for (unsigned i = 0, n = EndPoints.size(); i != n; ++i) {
      dbgs() << EndPoints[i];
      if (i != n-1)
        dbgs() << ',';
    }
    dbgs() << ":  " << LRange << '\n';
  });
  LIS->extendToIndices(LRange, EndPoints);
}

void RegisterCoalescer::mergeSubRangeInto(LiveInterval &LI,
                                          const LiveRange &ToMerge,
                                          LaneBitmask LaneMask,
                                          CoalescerPair &CP,
                                          unsigned ComposeSubRegIdx) {
  BumpPtrAllocator &Allocator = LIS->getVNInfoAllocator();
  LI.refineSubRanges(
      Allocator, LaneMask,
      [this, &Allocator, &ToMerge, &CP](LiveInterval::SubRange &SR) {
        if (SR.empty()) {
          SR.assign(ToMerge, Allocator);
        } else {
          // joinSubRegRange() destroys the merged range, so we need a copy.
          LiveRange RangeCopy(ToMerge, Allocator);
          joinSubRegRanges(SR, RangeCopy, SR.LaneMask, CP);
        }
      },
      *LIS->getSlotIndexes(), *TRI, ComposeSubRegIdx);
}

bool RegisterCoalescer::isHighCostLiveInterval(LiveInterval &LI) {
  if (LI.valnos.size() < LargeIntervalSizeThreshold)
    return false;
  auto &Counter = LargeLIVisitCounter[LI.reg()];
  if (Counter < LargeIntervalFreqThreshold) {
    Counter++;
    return false;
  }
  return true;
}

bool RegisterCoalescer::joinVirtRegs(CoalescerPair &CP) {
  SmallVector<VNInfo*, 16> NewVNInfo;
  LiveInterval &RHS = LIS->getInterval(CP.getSrcReg());
  LiveInterval &LHS = LIS->getInterval(CP.getDstReg());
  bool TrackSubRegLiveness = MRI->shouldTrackSubRegLiveness(*CP.getNewRC());
  JoinVals RHSVals(RHS, CP.getSrcReg(), CP.getSrcIdx(), LaneBitmask::getNone(),
                   NewVNInfo, CP, LIS, TRI, false, TrackSubRegLiveness);
  JoinVals LHSVals(LHS, CP.getDstReg(), CP.getDstIdx(), LaneBitmask::getNone(),
                   NewVNInfo, CP, LIS, TRI, false, TrackSubRegLiveness);

  LLVM_DEBUG(dbgs() << "\t\tRHS = " << RHS << "\n\t\tLHS = " << LHS << '\n');

  if (isHighCostLiveInterval(LHS) || isHighCostLiveInterval(RHS))
    return false;

  // First compute NewVNInfo and the simple value mappings.
  // Detect impossible conflicts early.
  if (!LHSVals.mapValues(RHSVals) || !RHSVals.mapValues(LHSVals))
    return false;

  // Some conflicts can only be resolved after all values have been mapped.
  if (!LHSVals.resolveConflicts(RHSVals) || !RHSVals.resolveConflicts(LHSVals))
    return false;

  // All clear, the live ranges can be merged.
  if (RHS.hasSubRanges() || LHS.hasSubRanges()) {
    BumpPtrAllocator &Allocator = LIS->getVNInfoAllocator();

    // Transform lanemasks from the LHS to masks in the coalesced register and
    // create initial subranges if necessary.
    unsigned DstIdx = CP.getDstIdx();
    if (!LHS.hasSubRanges()) {
      LaneBitmask Mask = DstIdx == 0 ? CP.getNewRC()->getLaneMask()
                                     : TRI->getSubRegIndexLaneMask(DstIdx);
      // LHS must support subregs or we wouldn't be in this codepath.
      assert(Mask.any());
      LHS.createSubRangeFrom(Allocator, Mask, LHS);
    } else if (DstIdx != 0) {
      // Transform LHS lanemasks to new register class if necessary.
      for (LiveInterval::SubRange &R : LHS.subranges()) {
        LaneBitmask Mask = TRI->composeSubRegIndexLaneMask(DstIdx, R.LaneMask);
        R.LaneMask = Mask;
      }
    }
    LLVM_DEBUG(dbgs() << "\t\tLHST = " << printReg(CP.getDstReg()) << ' ' << LHS
                      << '\n');

    // Determine lanemasks of RHS in the coalesced register and merge subranges.
    unsigned SrcIdx = CP.getSrcIdx();
    if (!RHS.hasSubRanges()) {
      LaneBitmask Mask = SrcIdx == 0 ? CP.getNewRC()->getLaneMask()
                                     : TRI->getSubRegIndexLaneMask(SrcIdx);
      mergeSubRangeInto(LHS, RHS, Mask, CP, DstIdx);
    } else {
      // Pair up subranges and merge.
      for (LiveInterval::SubRange &R : RHS.subranges()) {
        LaneBitmask Mask = TRI->composeSubRegIndexLaneMask(SrcIdx, R.LaneMask);
        mergeSubRangeInto(LHS, R, Mask, CP, DstIdx);
      }
    }
    LLVM_DEBUG(dbgs() << "\tJoined SubRanges " << LHS << "\n");

    // Pruning implicit defs from subranges may result in the main range
    // having stale segments.
    LHSVals.pruneMainSegments(LHS, ShrinkMainRange);

    LHSVals.pruneSubRegValues(LHS, ShrinkMask);
    RHSVals.pruneSubRegValues(LHS, ShrinkMask);
  } else if (TrackSubRegLiveness && !CP.getDstIdx() && CP.getSrcIdx()) {
    LHS.createSubRangeFrom(LIS->getVNInfoAllocator(),
                           CP.getNewRC()->getLaneMask(), LHS);
    mergeSubRangeInto(LHS, RHS, TRI->getSubRegIndexLaneMask(CP.getSrcIdx()), CP,
                      CP.getDstIdx());
    LHSVals.pruneMainSegments(LHS, ShrinkMainRange);
    LHSVals.pruneSubRegValues(LHS, ShrinkMask);
  }

  // The merging algorithm in LiveInterval::join() can't handle conflicting
  // value mappings, so we need to remove any live ranges that overlap a
  // CR_Replace resolution. Collect a set of end points that can be used to
  // restore the live range after joining.
  SmallVector<SlotIndex, 8> EndPoints;
  LHSVals.pruneValues(RHSVals, EndPoints, true);
  RHSVals.pruneValues(LHSVals, EndPoints, true);

  // Erase COPY and IMPLICIT_DEF instructions. This may cause some external
  // registers to require trimming.
  SmallVector<Register, 8> ShrinkRegs;
  LHSVals.eraseInstrs(ErasedInstrs, ShrinkRegs, &LHS);
  RHSVals.eraseInstrs(ErasedInstrs, ShrinkRegs);
  while (!ShrinkRegs.empty())
    shrinkToUses(&LIS->getInterval(ShrinkRegs.pop_back_val()));

  // Scan and mark undef any DBG_VALUEs that would refer to a different value.
  checkMergingChangesDbgValues(CP, LHS, LHSVals, RHS, RHSVals);

  // If the RHS covers any PHI locations that were tracked for debug-info, we
  // must update tracking information to reflect the join.
  auto RegIt = RegToPHIIdx.find(CP.getSrcReg());
  if (RegIt != RegToPHIIdx.end()) {
    // Iterate over all the debug instruction numbers assigned this register.
    for (unsigned InstID : RegIt->second) {
      auto PHIIt = PHIValToPos.find(InstID);
      assert(PHIIt != PHIValToPos.end());
      const SlotIndex &SI = PHIIt->second.SI;

      // Does the RHS cover the position of this PHI?
      auto LII = RHS.find(SI);
      if (LII == RHS.end() || LII->start > SI)
        continue;

      // Accept two kinds of subregister movement:
      //  * When we merge from one register class into a larger register:
      //        %1:gr16 = some-inst
      //                ->
      //        %2:gr32.sub_16bit = some-inst
      //  * When the PHI is already in a subregister, and the larger class
      //    is coalesced:
      //        %2:gr32.sub_16bit = some-inst
      //        %3:gr32 = COPY %2
      //                ->
      //        %3:gr32.sub_16bit = some-inst
      // Test for subregister move:
      if (CP.getSrcIdx() != 0 || CP.getDstIdx() != 0)
        // If we're moving between different subregisters, ignore this join.
        // The PHI will not get a location, dropping variable locations.
        if (PHIIt->second.SubReg && PHIIt->second.SubReg != CP.getSrcIdx())
          continue;

      // Update our tracking of where the PHI is.
      PHIIt->second.Reg = CP.getDstReg();

      // If we merge into a sub-register of a larger class (test above),
      // update SubReg.
      if (CP.getSrcIdx() != 0)
        PHIIt->second.SubReg = CP.getSrcIdx();
    }

    // Rebuild the register index in RegToPHIIdx to account for PHIs tracking
    // different VRegs now. Copy old collection of debug instruction numbers and
    // erase the old one:
    auto InstrNums = RegIt->second;
    RegToPHIIdx.erase(RegIt);

    // There might already be PHIs being tracked in the destination VReg. Insert
    // into an existing tracking collection, or insert a new one.
    RegIt = RegToPHIIdx.find(CP.getDstReg());
    if (RegIt != RegToPHIIdx.end())
      RegIt->second.insert(RegIt->second.end(), InstrNums.begin(),
                           InstrNums.end());
    else
      RegToPHIIdx.insert({CP.getDstReg(), InstrNums});
  }

  // Join RHS into LHS.
  LHS.join(RHS, LHSVals.getAssignments(), RHSVals.getAssignments(), NewVNInfo);

  // Kill flags are going to be wrong if the live ranges were overlapping.
  // Eventually, we should simply clear all kill flags when computing live
  // ranges. They are reinserted after register allocation.
  MRI->clearKillFlags(LHS.reg());
  MRI->clearKillFlags(RHS.reg());

  if (!EndPoints.empty()) {
    // Recompute the parts of the live range we had to remove because of
    // CR_Replace conflicts.
    LLVM_DEBUG({
      dbgs() << "\t\trestoring liveness to " << EndPoints.size() << " points: ";
      for (unsigned i = 0, n = EndPoints.size(); i != n; ++i) {
        dbgs() << EndPoints[i];
        if (i != n-1)
          dbgs() << ',';
      }
      dbgs() << ":  " << LHS << '\n';
    });
    LIS->extendToIndices((LiveRange&)LHS, EndPoints);
  }

  return true;
}

bool RegisterCoalescer::joinIntervals(CoalescerPair &CP) {
  return CP.isPhys() ? joinReservedPhysReg(CP) : joinVirtRegs(CP);
}

void RegisterCoalescer::buildVRegToDbgValueMap(MachineFunction &MF)
{
  const SlotIndexes &Slots = *LIS->getSlotIndexes();
  SmallVector<MachineInstr *, 8> ToInsert;

  // After collecting a block of DBG_VALUEs into ToInsert, enter them into the
  // vreg => DbgValueLoc map.
  auto CloseNewDVRange = [this, &ToInsert](SlotIndex Slot) {
    for (auto *X : ToInsert) {
      for (const auto &Op : X->debug_operands()) {
        if (Op.isReg() && Op.getReg().isVirtual())
          DbgVRegToValues[Op.getReg()].push_back({Slot, X});
      }
    }

    ToInsert.clear();
  };

  // Iterate over all instructions, collecting them into the ToInsert vector.
  // Once a non-debug instruction is found, record the slot index of the
  // collected DBG_VALUEs.
  for (auto &MBB : MF) {
    SlotIndex CurrentSlot = Slots.getMBBStartIdx(&MBB);

    for (auto &MI : MBB) {
      if (MI.isDebugValue()) {
        if (any_of(MI.debug_operands(), [](const MachineOperand &MO) {
              return MO.isReg() && MO.getReg().isVirtual();
            }))
          ToInsert.push_back(&MI);
      } else if (!MI.isDebugOrPseudoInstr()) {
        CurrentSlot = Slots.getInstructionIndex(MI);
        CloseNewDVRange(CurrentSlot);
      }
    }

    // Close range of DBG_VALUEs at the end of blocks.
    CloseNewDVRange(Slots.getMBBEndIdx(&MBB));
  }

  // Sort all DBG_VALUEs we've seen by slot number.
  for (auto &Pair : DbgVRegToValues)
    llvm::sort(Pair.second);
}

void RegisterCoalescer::checkMergingChangesDbgValues(CoalescerPair &CP,
                                                     LiveRange &LHS,
                                                     JoinVals &LHSVals,
                                                     LiveRange &RHS,
                                                     JoinVals &RHSVals) {
  auto ScanForDstReg = [&](Register Reg) {
    checkMergingChangesDbgValuesImpl(Reg, RHS, LHS, LHSVals);
  };

  auto ScanForSrcReg = [&](Register Reg) {
    checkMergingChangesDbgValuesImpl(Reg, LHS, RHS, RHSVals);
  };

  // Scan for unsound updates of both the source and destination register.
  ScanForSrcReg(CP.getSrcReg());
  ScanForDstReg(CP.getDstReg());
}

void RegisterCoalescer::checkMergingChangesDbgValuesImpl(Register Reg,
                                                         LiveRange &OtherLR,
                                                         LiveRange &RegLR,
                                                         JoinVals &RegVals) {
  // Are there any DBG_VALUEs to examine?
  auto VRegMapIt = DbgVRegToValues.find(Reg);
  if (VRegMapIt == DbgVRegToValues.end())
    return;

  auto &DbgValueSet = VRegMapIt->second;
  auto DbgValueSetIt = DbgValueSet.begin();
  auto SegmentIt = OtherLR.begin();

  bool LastUndefResult = false;
  SlotIndex LastUndefIdx;

  // If the "Other" register is live at a slot Idx, test whether Reg can
  // safely be merged with it, or should be marked undef.
  auto ShouldUndef = [&RegVals, &RegLR, &LastUndefResult,
                      &LastUndefIdx](SlotIndex Idx) -> bool {
    // Our worst-case performance typically happens with asan, causing very
    // many DBG_VALUEs of the same location. Cache a copy of the most recent
    // result for this edge-case.
    if (LastUndefIdx == Idx)
      return LastUndefResult;

    // If the other range was live, and Reg's was not, the register coalescer
    // will not have tried to resolve any conflicts. We don't know whether
    // the DBG_VALUE will refer to the same value number, so it must be made
    // undef.
    auto OtherIt = RegLR.find(Idx);
    if (OtherIt == RegLR.end())
      return true;

    // Both the registers were live: examine the conflict resolution record for
    // the value number Reg refers to. CR_Keep meant that this value number
    // "won" and the merged register definitely refers to that value. CR_Erase
    // means the value number was a redundant copy of the other value, which
    // was coalesced and Reg deleted. It's safe to refer to the other register
    // (which will be the source of the copy).
    auto Resolution = RegVals.getResolution(OtherIt->valno->id);
    LastUndefResult = Resolution != JoinVals::CR_Keep &&
                      Resolution != JoinVals::CR_Erase;
    LastUndefIdx = Idx;
    return LastUndefResult;
  };

  // Iterate over both the live-range of the "Other" register, and the set of
  // DBG_VALUEs for Reg at the same time. Advance whichever one has the lowest
  // slot index. This relies on the DbgValueSet being ordered.
  while (DbgValueSetIt != DbgValueSet.end() && SegmentIt != OtherLR.end()) {
    if (DbgValueSetIt->first < SegmentIt->end) {
      // "Other" is live and there is a DBG_VALUE of Reg: test if we should
      // set it undef.
      if (DbgValueSetIt->first >= SegmentIt->start) {
        bool HasReg = DbgValueSetIt->second->hasDebugOperandForReg(Reg);
        bool ShouldUndefReg = ShouldUndef(DbgValueSetIt->first);
        if (HasReg && ShouldUndefReg) {
          // Mark undef, erase record of this DBG_VALUE to avoid revisiting.
          DbgValueSetIt->second->setDebugValueUndef();
          continue;
        }
      }
      ++DbgValueSetIt;
    } else {
      ++SegmentIt;
    }
  }
}

namespace {

/// Information concerning MBB coalescing priority.
struct MBBPriorityInfo {
  MachineBasicBlock *MBB;
  unsigned Depth;
  bool IsSplit;

  MBBPriorityInfo(MachineBasicBlock *mbb, unsigned depth, bool issplit)
    : MBB(mbb), Depth(depth), IsSplit(issplit) {}
};

} // end anonymous namespace

/// C-style comparator that sorts first based on the loop depth of the basic
/// block (the unsigned), and then on the MBB number.
///
/// EnableGlobalCopies assumes that the primary sort key is loop depth.
static int compareMBBPriority(const MBBPriorityInfo *LHS,
                              const MBBPriorityInfo *RHS) {
  // Deeper loops first
  if (LHS->Depth != RHS->Depth)
    return LHS->Depth > RHS->Depth ? -1 : 1;

  // Try to unsplit critical edges next.
  if (LHS->IsSplit != RHS->IsSplit)
    return LHS->IsSplit ? -1 : 1;

  // Prefer blocks that are more connected in the CFG. This takes care of
  // the most difficult copies first while intervals are short.
  unsigned cl = LHS->MBB->pred_size() + LHS->MBB->succ_size();
  unsigned cr = RHS->MBB->pred_size() + RHS->MBB->succ_size();
  if (cl != cr)
    return cl > cr ? -1 : 1;

  // As a last resort, sort by block number.
  return LHS->MBB->getNumber() < RHS->MBB->getNumber() ? -1 : 1;
}

/// \returns true if the given copy uses or defines a local live range.
static bool isLocalCopy(MachineInstr *Copy, const LiveIntervals *LIS) {
  if (!Copy->isCopy())
    return false;

  if (Copy->getOperand(1).isUndef())
    return false;

  Register SrcReg = Copy->getOperand(1).getReg();
  Register DstReg = Copy->getOperand(0).getReg();
  if (SrcReg.isPhysical() || DstReg.isPhysical())
    return false;

  return LIS->intervalIsInOneMBB(LIS->getInterval(SrcReg))
    || LIS->intervalIsInOneMBB(LIS->getInterval(DstReg));
}

void RegisterCoalescer::lateLiveIntervalUpdate() {
  for (Register reg : ToBeUpdated) {
    if (!LIS->hasInterval(reg))
      continue;
    LiveInterval &LI = LIS->getInterval(reg);
    shrinkToUses(&LI, &DeadDefs);
    if (!DeadDefs.empty())
      eliminateDeadDefs();
  }
  ToBeUpdated.clear();
}

bool RegisterCoalescer::
copyCoalesceWorkList(MutableArrayRef<MachineInstr*> CurrList) {
  bool Progress = false;
  SmallPtrSet<MachineInstr *, 4> CurrentErasedInstrs;
  for (MachineInstr *&MI : CurrList) {
    if (!MI)
      continue;
    // Skip instruction pointers that have already been erased, for example by
    // dead code elimination.
    if (ErasedInstrs.count(MI) || CurrentErasedInstrs.count(MI)) {
      MI = nullptr;
      continue;
    }
    bool Again = false;
    bool Success = joinCopy(MI, Again, CurrentErasedInstrs);
    Progress |= Success;
    if (Success || !Again)
      MI = nullptr;
  }
  // Clear instructions not recorded in `ErasedInstrs` but erased.
  if (!CurrentErasedInstrs.empty()) {
    for (MachineInstr *&MI : CurrList) {
      if (MI && CurrentErasedInstrs.count(MI))
        MI = nullptr;
    }
    for (MachineInstr *&MI : WorkList) {
      if (MI && CurrentErasedInstrs.count(MI))
        MI = nullptr;
    }
  }
  return Progress;
}

/// Check if DstReg is a terminal node.
/// I.e., it does not have any affinity other than \p Copy.
static bool isTerminalReg(Register DstReg, const MachineInstr &Copy,
                          const MachineRegisterInfo *MRI) {
  assert(Copy.isCopyLike());
  // Check if the destination of this copy as any other affinity.
  for (const MachineInstr &MI : MRI->reg_nodbg_instructions(DstReg))
    if (&MI != &Copy && MI.isCopyLike())
      return false;
  return true;
}

bool RegisterCoalescer::applyTerminalRule(const MachineInstr &Copy) const {
  assert(Copy.isCopyLike());
  if (!UseTerminalRule)
    return false;
  Register SrcReg, DstReg;
  unsigned SrcSubReg = 0, DstSubReg = 0;
  if (!isMoveInstr(*TRI, &Copy, SrcReg, DstReg, SrcSubReg, DstSubReg))
    return false;
  // Check if the destination of this copy has any other affinity.
  if (DstReg.isPhysical() ||
      // If SrcReg is a physical register, the copy won't be coalesced.
      // Ignoring it may have other side effect (like missing
      // rematerialization). So keep it.
      SrcReg.isPhysical() || !isTerminalReg(DstReg, Copy, MRI))
    return false;

  // DstReg is a terminal node. Check if it interferes with any other
  // copy involving SrcReg.
  const MachineBasicBlock *OrigBB = Copy.getParent();
  const LiveInterval &DstLI = LIS->getInterval(DstReg);
  for (const MachineInstr &MI : MRI->reg_nodbg_instructions(SrcReg)) {
    // Technically we should check if the weight of the new copy is
    // interesting compared to the other one and update the weight
    // of the copies accordingly. However, this would only work if
    // we would gather all the copies first then coalesce, whereas
    // right now we interleave both actions.
    // For now, just consider the copies that are in the same block.
    if (&MI == &Copy || !MI.isCopyLike() || MI.getParent() != OrigBB)
      continue;
    Register OtherSrcReg, OtherReg;
    unsigned OtherSrcSubReg = 0, OtherSubReg = 0;
    if (!isMoveInstr(*TRI, &Copy, OtherSrcReg, OtherReg, OtherSrcSubReg,
                OtherSubReg))
      return false;
    if (OtherReg == SrcReg)
      OtherReg = OtherSrcReg;
    // Check if OtherReg is a non-terminal.
    if (OtherReg.isPhysical() || isTerminalReg(OtherReg, MI, MRI))
      continue;
    // Check that OtherReg interfere with DstReg.
    if (LIS->getInterval(OtherReg).overlaps(DstLI)) {
      LLVM_DEBUG(dbgs() << "Apply terminal rule for: " << printReg(DstReg)
                        << '\n');
      return true;
    }
  }
  return false;
}

void
RegisterCoalescer::copyCoalesceInMBB(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << MBB->getName() << ":\n");

  // Collect all copy-like instructions in MBB. Don't start coalescing anything
  // yet, it might invalidate the iterator.
  const unsigned PrevSize = WorkList.size();
  if (JoinGlobalCopies) {
    SmallVector<MachineInstr*, 2> LocalTerminals;
    SmallVector<MachineInstr*, 2> GlobalTerminals;
    // Coalesce copies bottom-up to coalesce local defs before local uses. They
    // are not inherently easier to resolve, but slightly preferable until we
    // have local live range splitting. In particular this is required by
    // cmp+jmp macro fusion.
    for (MachineInstr &MI : *MBB) {
      if (!MI.isCopyLike())
        continue;
      bool ApplyTerminalRule = applyTerminalRule(MI);
      if (isLocalCopy(&MI, LIS)) {
        if (ApplyTerminalRule)
          LocalTerminals.push_back(&MI);
        else
          LocalWorkList.push_back(&MI);
      } else {
        if (ApplyTerminalRule)
          GlobalTerminals.push_back(&MI);
        else
          WorkList.push_back(&MI);
      }
    }
    // Append the copies evicted by the terminal rule at the end of the list.
    LocalWorkList.append(LocalTerminals.begin(), LocalTerminals.end());
    WorkList.append(GlobalTerminals.begin(), GlobalTerminals.end());
  }
  else {
    SmallVector<MachineInstr*, 2> Terminals;
    for (MachineInstr &MII : *MBB)
      if (MII.isCopyLike()) {
        if (applyTerminalRule(MII))
          Terminals.push_back(&MII);
        else
          WorkList.push_back(&MII);
      }
    // Append the copies evicted by the terminal rule at the end of the list.
    WorkList.append(Terminals.begin(), Terminals.end());
  }
  // Try coalescing the collected copies immediately, and remove the nulls.
  // This prevents the WorkList from getting too large since most copies are
  // joinable on the first attempt.
  MutableArrayRef<MachineInstr*>
    CurrList(WorkList.begin() + PrevSize, WorkList.end());
  if (copyCoalesceWorkList(CurrList))
    WorkList.erase(std::remove(WorkList.begin() + PrevSize, WorkList.end(),
                               nullptr), WorkList.end());
}

void RegisterCoalescer::coalesceLocals() {
  copyCoalesceWorkList(LocalWorkList);
  for (MachineInstr *MI : LocalWorkList) {
    if (MI)
      WorkList.push_back(MI);
  }
  LocalWorkList.clear();
}

void RegisterCoalescer::joinAllIntervals() {
  LLVM_DEBUG(dbgs() << "********** JOINING INTERVALS ***********\n");
  assert(WorkList.empty() && LocalWorkList.empty() && "Old data still around.");

  std::vector<MBBPriorityInfo> MBBs;
  MBBs.reserve(MF->size());
  for (MachineBasicBlock &MBB : *MF) {
    MBBs.push_back(MBBPriorityInfo(&MBB, Loops->getLoopDepth(&MBB),
                                   JoinSplitEdges && isSplitEdge(&MBB)));
  }
  array_pod_sort(MBBs.begin(), MBBs.end(), compareMBBPriority);

  // Coalesce intervals in MBB priority order.
  unsigned CurrDepth = std::numeric_limits<unsigned>::max();
  for (MBBPriorityInfo &MBB : MBBs) {
    // Try coalescing the collected local copies for deeper loops.
    if (JoinGlobalCopies && MBB.Depth < CurrDepth) {
      coalesceLocals();
      CurrDepth = MBB.Depth;
    }
    copyCoalesceInMBB(MBB.MBB);
  }
  lateLiveIntervalUpdate();
  coalesceLocals();

  // Joining intervals can allow other intervals to be joined.  Iteratively join
  // until we make no progress.
  while (copyCoalesceWorkList(WorkList))
    /* empty */ ;
  lateLiveIntervalUpdate();
}

void RegisterCoalescer::releaseMemory() {
  ErasedInstrs.clear();
  WorkList.clear();
  DeadDefs.clear();
  InflateRegs.clear();
  LargeLIVisitCounter.clear();
}

bool RegisterCoalescer::runOnMachineFunction(MachineFunction &fn) {
  LLVM_DEBUG(dbgs() << "********** REGISTER COALESCER **********\n"
                    << "********** Function: " << fn.getName() << '\n');

  // Variables changed between a setjmp and a longjump can have undefined value
  // after the longjmp. This behaviour can be observed if such a variable is
  // spilled, so longjmp won't restore the value in the spill slot.
  // RegisterCoalescer should not run in functions with a setjmp to avoid
  // merging such undefined variables with predictable ones.
  //
  // TODO: Could specifically disable coalescing registers live across setjmp
  // calls
  if (fn.exposesReturnsTwice()) {
    LLVM_DEBUG(
        dbgs() << "* Skipped as it exposes functions that returns twice.\n");
    return false;
  }

  MF = &fn;
  MRI = &fn.getRegInfo();
  const TargetSubtargetInfo &STI = fn.getSubtarget();
  TRI = STI.getRegisterInfo();
  TII = STI.getInstrInfo();
  LIS = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  Loops = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  if (EnableGlobalCopies == cl::BOU_UNSET)
    JoinGlobalCopies = STI.enableJoinGlobalCopies();
  else
    JoinGlobalCopies = (EnableGlobalCopies == cl::BOU_TRUE);

  // If there are PHIs tracked by debug-info, they will need updating during
  // coalescing. Build an index of those PHIs to ease updating.
  SlotIndexes *Slots = LIS->getSlotIndexes();
  for (const auto &DebugPHI : MF->DebugPHIPositions) {
    MachineBasicBlock *MBB = DebugPHI.second.MBB;
    Register Reg = DebugPHI.second.Reg;
    unsigned SubReg = DebugPHI.second.SubReg;
    SlotIndex SI = Slots->getMBBStartIdx(MBB);
    PHIValPos P = {SI, Reg, SubReg};
    PHIValToPos.insert(std::make_pair(DebugPHI.first, P));
    RegToPHIIdx[Reg].push_back(DebugPHI.first);
  }

  // The MachineScheduler does not currently require JoinSplitEdges. This will
  // either be enabled unconditionally or replaced by a more general live range
  // splitting optimization.
  JoinSplitEdges = EnableJoinSplits;

  if (VerifyCoalescing)
    MF->verify(this, "Before register coalescing");

  DbgVRegToValues.clear();
  buildVRegToDbgValueMap(fn);

  RegClassInfo.runOnMachineFunction(fn);

  // Join (coalesce) intervals if requested.
  if (EnableJoining)
    joinAllIntervals();

  // After deleting a lot of copies, register classes may be less constrained.
  // Removing sub-register operands may allow GR32_ABCD -> GR32 and DPR_VFP2 ->
  // DPR inflation.
  array_pod_sort(InflateRegs.begin(), InflateRegs.end());
  InflateRegs.erase(llvm::unique(InflateRegs), InflateRegs.end());
  LLVM_DEBUG(dbgs() << "Trying to inflate " << InflateRegs.size()
                    << " regs.\n");
  for (Register Reg : InflateRegs) {
    if (MRI->reg_nodbg_empty(Reg))
      continue;
    if (MRI->recomputeRegClass(Reg)) {
      LLVM_DEBUG(dbgs() << printReg(Reg) << " inflated to "
                        << TRI->getRegClassName(MRI->getRegClass(Reg)) << '\n');
      ++NumInflated;

      LiveInterval &LI = LIS->getInterval(Reg);
      if (LI.hasSubRanges()) {
        // If the inflated register class does not support subregisters anymore
        // remove the subranges.
        if (!MRI->shouldTrackSubRegLiveness(Reg)) {
          LI.clearSubRanges();
        } else {
#ifndef NDEBUG
          LaneBitmask MaxMask = MRI->getMaxLaneMaskForVReg(Reg);
          // If subranges are still supported, then the same subregs
          // should still be supported.
          for (LiveInterval::SubRange &S : LI.subranges()) {
            assert((S.LaneMask & ~MaxMask).none());
          }
#endif
        }
      }
    }
  }

  // After coalescing, update any PHIs that are being tracked by debug-info
  // with their new VReg locations.
  for (auto &p : MF->DebugPHIPositions) {
    auto it = PHIValToPos.find(p.first);
    assert(it != PHIValToPos.end());
    p.second.Reg = it->second.Reg;
    p.second.SubReg = it->second.SubReg;
  }

  PHIValToPos.clear();
  RegToPHIIdx.clear();

  LLVM_DEBUG(dump());
  if (VerifyCoalescing)
    MF->verify(this, "After register coalescing");
  return true;
}

void RegisterCoalescer::print(raw_ostream &O, const Module* m) const {
  LIS->print(O);
}
