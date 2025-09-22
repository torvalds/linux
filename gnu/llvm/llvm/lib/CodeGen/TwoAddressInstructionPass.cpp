//===- TwoAddressInstructionPass.cpp - Two-Address instruction pass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TwoAddress instruction pass which is used
// by most register allocators. Two-Address instructions are rewritten
// from:
//
//     A = B op C
//
// to:
//
//     A = B
//     A op= C
//
// Note that if a register allocator chooses to use this pass, that it
// has to be capable of handling the non-SSA nature of these rewritten
// virtual registers.
//
// It is also worth noting that the duplicate operand of the two
// address instruction is removed.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TwoAddressInstructionPass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "twoaddressinstruction"

STATISTIC(NumTwoAddressInstrs, "Number of two-address instructions");
STATISTIC(NumCommuted        , "Number of instructions commuted to coalesce");
STATISTIC(NumAggrCommuted    , "Number of instructions aggressively commuted");
STATISTIC(NumConvertedTo3Addr, "Number of instructions promoted to 3-address");
STATISTIC(NumReSchedUps,       "Number of instructions re-scheduled up");
STATISTIC(NumReSchedDowns,     "Number of instructions re-scheduled down");

// Temporary flag to disable rescheduling.
static cl::opt<bool>
EnableRescheduling("twoaddr-reschedule",
                   cl::desc("Coalesce copies by rescheduling (default=true)"),
                   cl::init(true), cl::Hidden);

// Limit the number of dataflow edges to traverse when evaluating the benefit
// of commuting operands.
static cl::opt<unsigned> MaxDataFlowEdge(
    "dataflow-edge-limit", cl::Hidden, cl::init(3),
    cl::desc("Maximum number of dataflow edges to traverse when evaluating "
             "the benefit of commuting operands"));

namespace {

class TwoAddressInstructionImpl {
  MachineFunction *MF = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const InstrItineraryData *InstrItins = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  LiveVariables *LV = nullptr;
  LiveIntervals *LIS = nullptr;
  AliasAnalysis *AA = nullptr;
  CodeGenOptLevel OptLevel = CodeGenOptLevel::None;

  // The current basic block being processed.
  MachineBasicBlock *MBB = nullptr;

  // Keep track the distance of a MI from the start of the current basic block.
  DenseMap<MachineInstr*, unsigned> DistanceMap;

  // Set of already processed instructions in the current block.
  SmallPtrSet<MachineInstr*, 8> Processed;

  // A map from virtual registers to physical registers which are likely targets
  // to be coalesced to due to copies from physical registers to virtual
  // registers. e.g. v1024 = move r0.
  DenseMap<Register, Register> SrcRegMap;

  // A map from virtual registers to physical registers which are likely targets
  // to be coalesced to due to copies to physical registers from virtual
  // registers. e.g. r1 = move v1024.
  DenseMap<Register, Register> DstRegMap;

  MachineInstr *getSingleDef(Register Reg, MachineBasicBlock *BB) const;

  bool isRevCopyChain(Register FromReg, Register ToReg, int Maxlen);

  bool noUseAfterLastDef(Register Reg, unsigned Dist, unsigned &LastDef);

  bool isCopyToReg(MachineInstr &MI, Register &SrcReg, Register &DstReg,
                   bool &IsSrcPhys, bool &IsDstPhys) const;

  bool isPlainlyKilled(const MachineInstr *MI, LiveRange &LR) const;
  bool isPlainlyKilled(const MachineInstr *MI, Register Reg) const;
  bool isPlainlyKilled(const MachineOperand &MO) const;

  bool isKilled(MachineInstr &MI, Register Reg, bool allowFalsePositives) const;

  MachineInstr *findOnlyInterestingUse(Register Reg, MachineBasicBlock *MBB,
                                       bool &IsCopy, Register &DstReg,
                                       bool &IsDstPhys) const;

  bool regsAreCompatible(Register RegA, Register RegB) const;

  void removeMapRegEntry(const MachineOperand &MO,
                         DenseMap<Register, Register> &RegMap) const;

  void removeClobberedSrcRegMap(MachineInstr *MI);

  bool regOverlapsSet(const SmallVectorImpl<Register> &Set, Register Reg) const;

  bool isProfitableToCommute(Register RegA, Register RegB, Register RegC,
                             MachineInstr *MI, unsigned Dist);

  bool commuteInstruction(MachineInstr *MI, unsigned DstIdx,
                          unsigned RegBIdx, unsigned RegCIdx, unsigned Dist);

  bool isProfitableToConv3Addr(Register RegA, Register RegB);

  bool convertInstTo3Addr(MachineBasicBlock::iterator &mi,
                          MachineBasicBlock::iterator &nmi, Register RegA,
                          Register RegB, unsigned &Dist);

  bool isDefTooClose(Register Reg, unsigned Dist, MachineInstr *MI);

  bool rescheduleMIBelowKill(MachineBasicBlock::iterator &mi,
                             MachineBasicBlock::iterator &nmi, Register Reg);
  bool rescheduleKillAboveMI(MachineBasicBlock::iterator &mi,
                             MachineBasicBlock::iterator &nmi, Register Reg);

  bool tryInstructionTransform(MachineBasicBlock::iterator &mi,
                               MachineBasicBlock::iterator &nmi,
                               unsigned SrcIdx, unsigned DstIdx,
                               unsigned &Dist, bool shouldOnlyCommute);

  bool tryInstructionCommute(MachineInstr *MI,
                             unsigned DstOpIdx,
                             unsigned BaseOpIdx,
                             bool BaseOpKilled,
                             unsigned Dist);
  void scanUses(Register DstReg);

  void processCopy(MachineInstr *MI);

  using TiedPairList = SmallVector<std::pair<unsigned, unsigned>, 4>;
  using TiedOperandMap = SmallDenseMap<unsigned, TiedPairList>;

  bool collectTiedOperands(MachineInstr *MI, TiedOperandMap&);
  void processTiedPairs(MachineInstr *MI, TiedPairList&, unsigned &Dist);
  void eliminateRegSequence(MachineBasicBlock::iterator&);
  bool processStatepoint(MachineInstr *MI, TiedOperandMap &TiedOperands);

public:
  TwoAddressInstructionImpl(MachineFunction &MF, MachineFunctionPass *P);
  TwoAddressInstructionImpl(MachineFunction &MF,
                            MachineFunctionAnalysisManager &MFAM);
  void setOptLevel(CodeGenOptLevel Level) { OptLevel = Level; }
  bool run();
};

class TwoAddressInstructionLegacyPass : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  TwoAddressInstructionLegacyPass() : MachineFunctionPass(ID) {
    initializeTwoAddressInstructionLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  /// Pass entry point.
  bool runOnMachineFunction(MachineFunction &MF) override {
    TwoAddressInstructionImpl Impl(MF, this);
    // Disable optimizations if requested. We cannot skip the whole pass as some
    // fixups are necessary for correctness.
    if (skipFunction(MF.getFunction()))
      Impl.setOptLevel(CodeGenOptLevel::None);
    return Impl.run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addUsedIfAvailable<AAResultsWrapperPass>();
    AU.addUsedIfAvailable<LiveVariablesWrapperPass>();
    AU.addPreserved<LiveVariablesWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

PreservedAnalyses
TwoAddressInstructionPass::run(MachineFunction &MF,
                               MachineFunctionAnalysisManager &MFAM) {
  // Disable optimizations if requested. We cannot skip the whole pass as some
  // fixups are necessary for correctness.
  TwoAddressInstructionImpl Impl(MF, MFAM);
  if (MF.getFunction().hasOptNone())
    Impl.setOptLevel(CodeGenOptLevel::None);

  MFPropsModifier _(*this, MF);
  bool Changed = Impl.run();
  if (!Changed)
    return PreservedAnalyses::all();
  auto PA = getMachineFunctionPassPreservedAnalyses();
  PA.preserve<LiveIntervalsAnalysis>();
  PA.preserve<LiveVariablesAnalysis>();
  PA.preserve<MachineDominatorTreeAnalysis>();
  PA.preserve<MachineLoopAnalysis>();
  PA.preserve<SlotIndexesAnalysis>();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

char TwoAddressInstructionLegacyPass::ID = 0;

char &llvm::TwoAddressInstructionPassID = TwoAddressInstructionLegacyPass::ID;

INITIALIZE_PASS_BEGIN(TwoAddressInstructionLegacyPass, DEBUG_TYPE,
                      "Two-Address instruction pass", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(TwoAddressInstructionLegacyPass, DEBUG_TYPE,
                    "Two-Address instruction pass", false, false)

TwoAddressInstructionImpl::TwoAddressInstructionImpl(
    MachineFunction &Func, MachineFunctionAnalysisManager &MFAM)
    : MF(&Func), TII(Func.getSubtarget().getInstrInfo()),
      TRI(Func.getSubtarget().getRegisterInfo()),
      InstrItins(Func.getSubtarget().getInstrItineraryData()),
      MRI(&Func.getRegInfo()),
      LV(MFAM.getCachedResult<LiveVariablesAnalysis>(Func)),
      LIS(MFAM.getCachedResult<LiveIntervalsAnalysis>(Func)),
      OptLevel(Func.getTarget().getOptLevel()) {
  auto &FAM = MFAM.getResult<FunctionAnalysisManagerMachineFunctionProxy>(Func)
                  .getManager();
  AA = FAM.getCachedResult<AAManager>(Func.getFunction());
}

TwoAddressInstructionImpl::TwoAddressInstructionImpl(MachineFunction &Func,
                                                     MachineFunctionPass *P)
    : MF(&Func), TII(Func.getSubtarget().getInstrInfo()),
      TRI(Func.getSubtarget().getRegisterInfo()),
      InstrItins(Func.getSubtarget().getInstrItineraryData()),
      MRI(&Func.getRegInfo()), OptLevel(Func.getTarget().getOptLevel()) {
  auto *LVWrapper = P->getAnalysisIfAvailable<LiveVariablesWrapperPass>();
  LV = LVWrapper ? &LVWrapper->getLV() : nullptr;
  auto *LISWrapper = P->getAnalysisIfAvailable<LiveIntervalsWrapperPass>();
  LIS = LISWrapper ? &LISWrapper->getLIS() : nullptr;
  if (auto *AAPass = P->getAnalysisIfAvailable<AAResultsWrapperPass>())
    AA = &AAPass->getAAResults();
  else
    AA = nullptr;
}

/// Return the MachineInstr* if it is the single def of the Reg in current BB.
MachineInstr *
TwoAddressInstructionImpl::getSingleDef(Register Reg,
                                        MachineBasicBlock *BB) const {
  MachineInstr *Ret = nullptr;
  for (MachineInstr &DefMI : MRI->def_instructions(Reg)) {
    if (DefMI.getParent() != BB || DefMI.isDebugValue())
      continue;
    if (!Ret)
      Ret = &DefMI;
    else if (Ret != &DefMI)
      return nullptr;
  }
  return Ret;
}

/// Check if there is a reversed copy chain from FromReg to ToReg:
/// %Tmp1 = copy %Tmp2;
/// %FromReg = copy %Tmp1;
/// %ToReg = add %FromReg ...
/// %Tmp2 = copy %ToReg;
/// MaxLen specifies the maximum length of the copy chain the func
/// can walk through.
bool TwoAddressInstructionImpl::isRevCopyChain(Register FromReg, Register ToReg,
                                               int Maxlen) {
  Register TmpReg = FromReg;
  for (int i = 0; i < Maxlen; i++) {
    MachineInstr *Def = getSingleDef(TmpReg, MBB);
    if (!Def || !Def->isCopy())
      return false;

    TmpReg = Def->getOperand(1).getReg();

    if (TmpReg == ToReg)
      return true;
  }
  return false;
}

/// Return true if there are no intervening uses between the last instruction
/// in the MBB that defines the specified register and the two-address
/// instruction which is being processed. It also returns the last def location
/// by reference.
bool TwoAddressInstructionImpl::noUseAfterLastDef(Register Reg, unsigned Dist,
                                                  unsigned &LastDef) {
  LastDef = 0;
  unsigned LastUse = Dist;
  for (MachineOperand &MO : MRI->reg_operands(Reg)) {
    MachineInstr *MI = MO.getParent();
    if (MI->getParent() != MBB || MI->isDebugValue())
      continue;
    DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
    if (DI == DistanceMap.end())
      continue;
    if (MO.isUse() && DI->second < LastUse)
      LastUse = DI->second;
    if (MO.isDef() && DI->second > LastDef)
      LastDef = DI->second;
  }

  return !(LastUse > LastDef && LastUse < Dist);
}

/// Return true if the specified MI is a copy instruction or an extract_subreg
/// instruction. It also returns the source and destination registers and
/// whether they are physical registers by reference.
bool TwoAddressInstructionImpl::isCopyToReg(MachineInstr &MI, Register &SrcReg,
                                            Register &DstReg, bool &IsSrcPhys,
                                            bool &IsDstPhys) const {
  SrcReg = 0;
  DstReg = 0;
  if (MI.isCopy()) {
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();
  } else if (MI.isInsertSubreg() || MI.isSubregToReg()) {
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(2).getReg();
  } else {
    return false;
  }

  IsSrcPhys = SrcReg.isPhysical();
  IsDstPhys = DstReg.isPhysical();
  return true;
}

bool TwoAddressInstructionImpl::isPlainlyKilled(const MachineInstr *MI,
                                                LiveRange &LR) const {
  // This is to match the kill flag version where undefs don't have kill flags.
  if (!LR.hasAtLeastOneValue())
    return false;

  SlotIndex useIdx = LIS->getInstructionIndex(*MI);
  LiveInterval::const_iterator I = LR.find(useIdx);
  assert(I != LR.end() && "Reg must be live-in to use.");
  return !I->end.isBlock() && SlotIndex::isSameInstr(I->end, useIdx);
}

/// Test if the given register value, which is used by the
/// given instruction, is killed by the given instruction.
bool TwoAddressInstructionImpl::isPlainlyKilled(const MachineInstr *MI,
                                                Register Reg) const {
  // FIXME: Sometimes tryInstructionTransform() will add instructions and
  // test whether they can be folded before keeping them. In this case it
  // sets a kill before recursively calling tryInstructionTransform() again.
  // If there is no interval available, we assume that this instruction is
  // one of those. A kill flag is manually inserted on the operand so the
  // check below will handle it.
  if (LIS && !LIS->isNotInMIMap(*MI)) {
    if (Reg.isVirtual())
      return isPlainlyKilled(MI, LIS->getInterval(Reg));
    // Reserved registers are considered always live.
    if (MRI->isReserved(Reg))
      return false;
    return all_of(TRI->regunits(Reg), [&](MCRegUnit U) {
      return isPlainlyKilled(MI, LIS->getRegUnit(U));
    });
  }

  return MI->killsRegister(Reg, /*TRI=*/nullptr);
}

/// Test if the register used by the given operand is killed by the operand's
/// instruction.
bool TwoAddressInstructionImpl::isPlainlyKilled(
    const MachineOperand &MO) const {
  return MO.isKill() || isPlainlyKilled(MO.getParent(), MO.getReg());
}

/// Test if the given register value, which is used by the given
/// instruction, is killed by the given instruction. This looks through
/// coalescable copies to see if the original value is potentially not killed.
///
/// For example, in this code:
///
///   %reg1034 = copy %reg1024
///   %reg1035 = copy killed %reg1025
///   %reg1036 = add killed %reg1034, killed %reg1035
///
/// %reg1034 is not considered to be killed, since it is copied from a
/// register which is not killed. Treating it as not killed lets the
/// normal heuristics commute the (two-address) add, which lets
/// coalescing eliminate the extra copy.
///
/// If allowFalsePositives is true then likely kills are treated as kills even
/// if it can't be proven that they are kills.
bool TwoAddressInstructionImpl::isKilled(MachineInstr &MI, Register Reg,
                                         bool allowFalsePositives) const {
  MachineInstr *DefMI = &MI;
  while (true) {
    // All uses of physical registers are likely to be kills.
    if (Reg.isPhysical() && (allowFalsePositives || MRI->hasOneUse(Reg)))
      return true;
    if (!isPlainlyKilled(DefMI, Reg))
      return false;
    if (Reg.isPhysical())
      return true;
    MachineRegisterInfo::def_iterator Begin = MRI->def_begin(Reg);
    // If there are multiple defs, we can't do a simple analysis, so just
    // go with what the kill flag says.
    if (std::next(Begin) != MRI->def_end())
      return true;
    DefMI = Begin->getParent();
    bool IsSrcPhys, IsDstPhys;
    Register SrcReg, DstReg;
    // If the def is something other than a copy, then it isn't going to
    // be coalesced, so follow the kill flag.
    if (!isCopyToReg(*DefMI, SrcReg, DstReg, IsSrcPhys, IsDstPhys))
      return true;
    Reg = SrcReg;
  }
}

/// Return true if the specified MI uses the specified register as a two-address
/// use. If so, return the destination register by reference.
static bool isTwoAddrUse(MachineInstr &MI, Register Reg, Register &DstReg) {
  for (unsigned i = 0, NumOps = MI.getNumOperands(); i != NumOps; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isUse() || MO.getReg() != Reg)
      continue;
    unsigned ti;
    if (MI.isRegTiedToDefOperand(i, &ti)) {
      DstReg = MI.getOperand(ti).getReg();
      return true;
    }
  }
  return false;
}

/// Given a register, if all its uses are in the same basic block, return the
/// last use instruction if it's a copy or a two-address use.
MachineInstr *TwoAddressInstructionImpl::findOnlyInterestingUse(
    Register Reg, MachineBasicBlock *MBB, bool &IsCopy, Register &DstReg,
    bool &IsDstPhys) const {
  MachineOperand *UseOp = nullptr;
  for (MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
    MachineInstr *MI = MO.getParent();
    if (MI->getParent() != MBB)
      return nullptr;
    if (isPlainlyKilled(MI, Reg))
      UseOp = &MO;
  }
  if (!UseOp)
    return nullptr;
  MachineInstr &UseMI = *UseOp->getParent();

  Register SrcReg;
  bool IsSrcPhys;
  if (isCopyToReg(UseMI, SrcReg, DstReg, IsSrcPhys, IsDstPhys)) {
    IsCopy = true;
    return &UseMI;
  }
  IsDstPhys = false;
  if (isTwoAddrUse(UseMI, Reg, DstReg)) {
    IsDstPhys = DstReg.isPhysical();
    return &UseMI;
  }
  if (UseMI.isCommutable()) {
    unsigned Src1 = TargetInstrInfo::CommuteAnyOperandIndex;
    unsigned Src2 = UseOp->getOperandNo();
    if (TII->findCommutedOpIndices(UseMI, Src1, Src2)) {
      MachineOperand &MO = UseMI.getOperand(Src1);
      if (MO.isReg() && MO.isUse() &&
          isTwoAddrUse(UseMI, MO.getReg(), DstReg)) {
        IsDstPhys = DstReg.isPhysical();
        return &UseMI;
      }
    }
  }
  return nullptr;
}

/// Return the physical register the specified virtual register might be mapped
/// to.
static MCRegister getMappedReg(Register Reg,
                               DenseMap<Register, Register> &RegMap) {
  while (Reg.isVirtual()) {
    DenseMap<Register, Register>::iterator SI = RegMap.find(Reg);
    if (SI == RegMap.end())
      return 0;
    Reg = SI->second;
  }
  if (Reg.isPhysical())
    return Reg;
  return 0;
}

/// Return true if the two registers are equal or aliased.
bool TwoAddressInstructionImpl::regsAreCompatible(Register RegA,
                                                  Register RegB) const {
  if (RegA == RegB)
    return true;
  if (!RegA || !RegB)
    return false;
  return TRI->regsOverlap(RegA, RegB);
}

/// From RegMap remove entries mapped to a physical register which overlaps MO.
void TwoAddressInstructionImpl::removeMapRegEntry(
    const MachineOperand &MO, DenseMap<Register, Register> &RegMap) const {
  assert(
      (MO.isReg() || MO.isRegMask()) &&
      "removeMapRegEntry must be called with a register or regmask operand.");

  SmallVector<Register, 2> Srcs;
  for (auto SI : RegMap) {
    Register ToReg = SI.second;
    if (ToReg.isVirtual())
      continue;

    if (MO.isReg()) {
      Register Reg = MO.getReg();
      if (TRI->regsOverlap(ToReg, Reg))
        Srcs.push_back(SI.first);
    } else if (MO.clobbersPhysReg(ToReg))
      Srcs.push_back(SI.first);
  }

  for (auto SrcReg : Srcs)
    RegMap.erase(SrcReg);
}

/// If a physical register is clobbered, old entries mapped to it should be
/// deleted. For example
///
///     %2:gr64 = COPY killed $rdx
///     MUL64r %3:gr64, implicit-def $rax, implicit-def $rdx
///
/// After the MUL instruction, $rdx contains different value than in the COPY
/// instruction. So %2 should not map to $rdx after MUL.
void TwoAddressInstructionImpl::removeClobberedSrcRegMap(MachineInstr *MI) {
  if (MI->isCopy()) {
    // If a virtual register is copied to its mapped physical register, it
    // doesn't change the potential coalescing between them, so we don't remove
    // entries mapped to the physical register. For example
    //
    // %100 = COPY $r8
    //     ...
    // $r8  = COPY %100
    //
    // The first copy constructs SrcRegMap[%100] = $r8, the second copy doesn't
    // destroy the content of $r8, and should not impact SrcRegMap.
    Register Dst = MI->getOperand(0).getReg();
    if (!Dst || Dst.isVirtual())
      return;

    Register Src = MI->getOperand(1).getReg();
    if (regsAreCompatible(Dst, getMappedReg(Src, SrcRegMap)))
      return;
  }

  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isRegMask()) {
      removeMapRegEntry(MO, SrcRegMap);
      continue;
    }
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || Reg.isVirtual())
      continue;
    removeMapRegEntry(MO, SrcRegMap);
  }
}

// Returns true if Reg is equal or aliased to at least one register in Set.
bool TwoAddressInstructionImpl::regOverlapsSet(
    const SmallVectorImpl<Register> &Set, Register Reg) const {
  for (unsigned R : Set)
    if (TRI->regsOverlap(R, Reg))
      return true;

  return false;
}

/// Return true if it's potentially profitable to commute the two-address
/// instruction that's being processed.
bool TwoAddressInstructionImpl::isProfitableToCommute(Register RegA,
                                                      Register RegB,
                                                      Register RegC,
                                                      MachineInstr *MI,
                                                      unsigned Dist) {
  if (OptLevel == CodeGenOptLevel::None)
    return false;

  // Determine if it's profitable to commute this two address instruction. In
  // general, we want no uses between this instruction and the definition of
  // the two-address register.
  // e.g.
  // %reg1028 = EXTRACT_SUBREG killed %reg1027, 1
  // %reg1029 = COPY %reg1028
  // %reg1029 = SHR8ri %reg1029, 7, implicit dead %eflags
  // insert => %reg1030 = COPY %reg1028
  // %reg1030 = ADD8rr killed %reg1028, killed %reg1029, implicit dead %eflags
  // In this case, it might not be possible to coalesce the second COPY
  // instruction if the first one is coalesced. So it would be profitable to
  // commute it:
  // %reg1028 = EXTRACT_SUBREG killed %reg1027, 1
  // %reg1029 = COPY %reg1028
  // %reg1029 = SHR8ri %reg1029, 7, implicit dead %eflags
  // insert => %reg1030 = COPY %reg1029
  // %reg1030 = ADD8rr killed %reg1029, killed %reg1028, implicit dead %eflags

  if (!isPlainlyKilled(MI, RegC))
    return false;

  // Ok, we have something like:
  // %reg1030 = ADD8rr killed %reg1028, killed %reg1029, implicit dead %eflags
  // let's see if it's worth commuting it.

  // Look for situations like this:
  // %reg1024 = MOV r1
  // %reg1025 = MOV r0
  // %reg1026 = ADD %reg1024, %reg1025
  // r0            = MOV %reg1026
  // Commute the ADD to hopefully eliminate an otherwise unavoidable copy.
  MCRegister ToRegA = getMappedReg(RegA, DstRegMap);
  if (ToRegA) {
    MCRegister FromRegB = getMappedReg(RegB, SrcRegMap);
    MCRegister FromRegC = getMappedReg(RegC, SrcRegMap);
    bool CompB = FromRegB && regsAreCompatible(FromRegB, ToRegA);
    bool CompC = FromRegC && regsAreCompatible(FromRegC, ToRegA);

    // Compute if any of the following are true:
    // -RegB is not tied to a register and RegC is compatible with RegA.
    // -RegB is tied to the wrong physical register, but RegC is.
    // -RegB is tied to the wrong physical register, and RegC isn't tied.
    if ((!FromRegB && CompC) || (FromRegB && !CompB && (!FromRegC || CompC)))
      return true;
    // Don't compute if any of the following are true:
    // -RegC is not tied to a register and RegB is compatible with RegA.
    // -RegC is tied to the wrong physical register, but RegB is.
    // -RegC is tied to the wrong physical register, and RegB isn't tied.
    if ((!FromRegC && CompB) || (FromRegC && !CompC && (!FromRegB || CompB)))
      return false;
  }

  // If there is a use of RegC between its last def (could be livein) and this
  // instruction, then bail.
  unsigned LastDefC = 0;
  if (!noUseAfterLastDef(RegC, Dist, LastDefC))
    return false;

  // If there is a use of RegB between its last def (could be livein) and this
  // instruction, then go ahead and make this transformation.
  unsigned LastDefB = 0;
  if (!noUseAfterLastDef(RegB, Dist, LastDefB))
    return true;

  // Look for situation like this:
  // %reg101 = MOV %reg100
  // %reg102 = ...
  // %reg103 = ADD %reg102, %reg101
  // ... = %reg103 ...
  // %reg100 = MOV %reg103
  // If there is a reversed copy chain from reg101 to reg103, commute the ADD
  // to eliminate an otherwise unavoidable copy.
  // FIXME:
  // We can extend the logic further: If an pair of operands in an insn has
  // been merged, the insn could be regarded as a virtual copy, and the virtual
  // copy could also be used to construct a copy chain.
  // To more generally minimize register copies, ideally the logic of two addr
  // instruction pass should be integrated with register allocation pass where
  // interference graph is available.
  if (isRevCopyChain(RegC, RegA, MaxDataFlowEdge))
    return true;

  if (isRevCopyChain(RegB, RegA, MaxDataFlowEdge))
    return false;

  // Look for other target specific commute preference.
  bool Commute;
  if (TII->hasCommutePreference(*MI, Commute))
    return Commute;

  // Since there are no intervening uses for both registers, then commute
  // if the def of RegC is closer. Its live interval is shorter.
  return LastDefB && LastDefC && LastDefC > LastDefB;
}

/// Commute a two-address instruction and update the basic block, distance map,
/// and live variables if needed. Return true if it is successful.
bool TwoAddressInstructionImpl::commuteInstruction(MachineInstr *MI,
                                                   unsigned DstIdx,
                                                   unsigned RegBIdx,
                                                   unsigned RegCIdx,
                                                   unsigned Dist) {
  Register RegC = MI->getOperand(RegCIdx).getReg();
  LLVM_DEBUG(dbgs() << "2addr: COMMUTING  : " << *MI);
  MachineInstr *NewMI = TII->commuteInstruction(*MI, false, RegBIdx, RegCIdx);

  if (NewMI == nullptr) {
    LLVM_DEBUG(dbgs() << "2addr: COMMUTING FAILED!\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "2addr: COMMUTED TO: " << *NewMI);
  assert(NewMI == MI &&
         "TargetInstrInfo::commuteInstruction() should not return a new "
         "instruction unless it was requested.");

  // Update source register map.
  MCRegister FromRegC = getMappedReg(RegC, SrcRegMap);
  if (FromRegC) {
    Register RegA = MI->getOperand(DstIdx).getReg();
    SrcRegMap[RegA] = FromRegC;
  }

  return true;
}

/// Return true if it is profitable to convert the given 2-address instruction
/// to a 3-address one.
bool TwoAddressInstructionImpl::isProfitableToConv3Addr(Register RegA,
                                                        Register RegB) {
  // Look for situations like this:
  // %reg1024 = MOV r1
  // %reg1025 = MOV r0
  // %reg1026 = ADD %reg1024, %reg1025
  // r2            = MOV %reg1026
  // Turn ADD into a 3-address instruction to avoid a copy.
  MCRegister FromRegB = getMappedReg(RegB, SrcRegMap);
  if (!FromRegB)
    return false;
  MCRegister ToRegA = getMappedReg(RegA, DstRegMap);
  return (ToRegA && !regsAreCompatible(FromRegB, ToRegA));
}

/// Convert the specified two-address instruction into a three address one.
/// Return true if this transformation was successful.
bool TwoAddressInstructionImpl::convertInstTo3Addr(
    MachineBasicBlock::iterator &mi, MachineBasicBlock::iterator &nmi,
    Register RegA, Register RegB, unsigned &Dist) {
  MachineInstrSpan MIS(mi, MBB);
  MachineInstr *NewMI = TII->convertToThreeAddress(*mi, LV, LIS);
  if (!NewMI)
    return false;

  LLVM_DEBUG(dbgs() << "2addr: CONVERTING 2-ADDR: " << *mi);
  LLVM_DEBUG(dbgs() << "2addr:         TO 3-ADDR: " << *NewMI);

  // If the old instruction is debug value tracked, an update is required.
  if (auto OldInstrNum = mi->peekDebugInstrNum()) {
    assert(mi->getNumExplicitDefs() == 1);
    assert(NewMI->getNumExplicitDefs() == 1);

    // Find the old and new def location.
    unsigned OldIdx = mi->defs().begin()->getOperandNo();
    unsigned NewIdx = NewMI->defs().begin()->getOperandNo();

    // Record that one def has been replaced by the other.
    unsigned NewInstrNum = NewMI->getDebugInstrNum();
    MF->makeDebugValueSubstitution(std::make_pair(OldInstrNum, OldIdx),
                                   std::make_pair(NewInstrNum, NewIdx));
  }

  MBB->erase(mi); // Nuke the old inst.

  for (MachineInstr &MI : MIS)
    DistanceMap.insert(std::make_pair(&MI, Dist++));
  Dist--;
  mi = NewMI;
  nmi = std::next(mi);

  // Update source and destination register maps.
  SrcRegMap.erase(RegA);
  DstRegMap.erase(RegB);
  return true;
}

/// Scan forward recursively for only uses, update maps if the use is a copy or
/// a two-address instruction.
void TwoAddressInstructionImpl::scanUses(Register DstReg) {
  SmallVector<Register, 4> VirtRegPairs;
  bool IsDstPhys;
  bool IsCopy = false;
  Register NewReg;
  Register Reg = DstReg;
  while (MachineInstr *UseMI =
             findOnlyInterestingUse(Reg, MBB, IsCopy, NewReg, IsDstPhys)) {
    if (IsCopy && !Processed.insert(UseMI).second)
      break;

    DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(UseMI);
    if (DI != DistanceMap.end())
      // Earlier in the same MBB.Reached via a back edge.
      break;

    if (IsDstPhys) {
      VirtRegPairs.push_back(NewReg);
      break;
    }
    SrcRegMap[NewReg] = Reg;
    VirtRegPairs.push_back(NewReg);
    Reg = NewReg;
  }

  if (!VirtRegPairs.empty()) {
    unsigned ToReg = VirtRegPairs.back();
    VirtRegPairs.pop_back();
    while (!VirtRegPairs.empty()) {
      unsigned FromReg = VirtRegPairs.pop_back_val();
      bool isNew = DstRegMap.insert(std::make_pair(FromReg, ToReg)).second;
      if (!isNew)
        assert(DstRegMap[FromReg] == ToReg &&"Can't map to two dst registers!");
      ToReg = FromReg;
    }
    bool isNew = DstRegMap.insert(std::make_pair(DstReg, ToReg)).second;
    if (!isNew)
      assert(DstRegMap[DstReg] == ToReg && "Can't map to two dst registers!");
  }
}

/// If the specified instruction is not yet processed, process it if it's a
/// copy. For a copy instruction, we find the physical registers the
/// source and destination registers might be mapped to. These are kept in
/// point-to maps used to determine future optimizations. e.g.
/// v1024 = mov r0
/// v1025 = mov r1
/// v1026 = add v1024, v1025
/// r1    = mov r1026
/// If 'add' is a two-address instruction, v1024, v1026 are both potentially
/// coalesced to r0 (from the input side). v1025 is mapped to r1. v1026 is
/// potentially joined with r1 on the output side. It's worthwhile to commute
/// 'add' to eliminate a copy.
void TwoAddressInstructionImpl::processCopy(MachineInstr *MI) {
  if (Processed.count(MI))
    return;

  bool IsSrcPhys, IsDstPhys;
  Register SrcReg, DstReg;
  if (!isCopyToReg(*MI, SrcReg, DstReg, IsSrcPhys, IsDstPhys))
    return;

  if (IsDstPhys && !IsSrcPhys) {
    DstRegMap.insert(std::make_pair(SrcReg, DstReg));
  } else if (!IsDstPhys && IsSrcPhys) {
    bool isNew = SrcRegMap.insert(std::make_pair(DstReg, SrcReg)).second;
    if (!isNew)
      assert(SrcRegMap[DstReg] == SrcReg &&
             "Can't map to two src physical registers!");

    scanUses(DstReg);
  }

  Processed.insert(MI);
}

/// If there is one more local instruction that reads 'Reg' and it kills 'Reg,
/// consider moving the instruction below the kill instruction in order to
/// eliminate the need for the copy.
bool TwoAddressInstructionImpl::rescheduleMIBelowKill(
    MachineBasicBlock::iterator &mi, MachineBasicBlock::iterator &nmi,
    Register Reg) {
  // Bail immediately if we don't have LV or LIS available. We use them to find
  // kills efficiently.
  if (!LV && !LIS)
    return false;

  MachineInstr *MI = &*mi;
  DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
  if (DI == DistanceMap.end())
    // Must be created from unfolded load. Don't waste time trying this.
    return false;

  MachineInstr *KillMI = nullptr;
  if (LIS) {
    LiveInterval &LI = LIS->getInterval(Reg);
    assert(LI.end() != LI.begin() &&
           "Reg should not have empty live interval.");

    SlotIndex MBBEndIdx = LIS->getMBBEndIdx(MBB).getPrevSlot();
    LiveInterval::const_iterator I = LI.find(MBBEndIdx);
    if (I != LI.end() && I->start < MBBEndIdx)
      return false;

    --I;
    KillMI = LIS->getInstructionFromIndex(I->end);
  } else {
    KillMI = LV->getVarInfo(Reg).findKill(MBB);
  }
  if (!KillMI || MI == KillMI || KillMI->isCopy() || KillMI->isCopyLike())
    // Don't mess with copies, they may be coalesced later.
    return false;

  if (KillMI->hasUnmodeledSideEffects() || KillMI->isCall() ||
      KillMI->isBranch() || KillMI->isTerminator())
    // Don't move pass calls, etc.
    return false;

  Register DstReg;
  if (isTwoAddrUse(*KillMI, Reg, DstReg))
    return false;

  bool SeenStore = true;
  if (!MI->isSafeToMove(AA, SeenStore))
    return false;

  if (TII->getInstrLatency(InstrItins, *MI) > 1)
    // FIXME: Needs more sophisticated heuristics.
    return false;

  SmallVector<Register, 2> Uses;
  SmallVector<Register, 2> Kills;
  SmallVector<Register, 2> Defs;
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg())
      continue;
    Register MOReg = MO.getReg();
    if (!MOReg)
      continue;
    if (MO.isDef())
      Defs.push_back(MOReg);
    else {
      Uses.push_back(MOReg);
      if (MOReg != Reg && isPlainlyKilled(MO))
        Kills.push_back(MOReg);
    }
  }

  // Move the copies connected to MI down as well.
  MachineBasicBlock::iterator Begin = MI;
  MachineBasicBlock::iterator AfterMI = std::next(Begin);
  MachineBasicBlock::iterator End = AfterMI;
  while (End != MBB->end()) {
    End = skipDebugInstructionsForward(End, MBB->end());
    if (End->isCopy() && regOverlapsSet(Defs, End->getOperand(1).getReg()))
      Defs.push_back(End->getOperand(0).getReg());
    else
      break;
    ++End;
  }

  // Check if the reschedule will not break dependencies.
  unsigned NumVisited = 0;
  MachineBasicBlock::iterator KillPos = KillMI;
  ++KillPos;
  for (MachineInstr &OtherMI : make_range(End, KillPos)) {
    // Debug or pseudo instructions cannot be counted against the limit.
    if (OtherMI.isDebugOrPseudoInstr())
      continue;
    if (NumVisited > 10)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    if (OtherMI.hasUnmodeledSideEffects() || OtherMI.isCall() ||
        OtherMI.isBranch() || OtherMI.isTerminator())
      // Don't move pass calls, etc.
      return false;
    for (const MachineOperand &MO : OtherMI.operands()) {
      if (!MO.isReg())
        continue;
      Register MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (MO.isDef()) {
        if (regOverlapsSet(Uses, MOReg))
          // Physical register use would be clobbered.
          return false;
        if (!MO.isDead() && regOverlapsSet(Defs, MOReg))
          // May clobber a physical register def.
          // FIXME: This may be too conservative. It's ok if the instruction
          // is sunken completely below the use.
          return false;
      } else {
        if (regOverlapsSet(Defs, MOReg))
          return false;
        bool isKill = isPlainlyKilled(MO);
        if (MOReg != Reg && ((isKill && regOverlapsSet(Uses, MOReg)) ||
                             regOverlapsSet(Kills, MOReg)))
          // Don't want to extend other live ranges and update kills.
          return false;
        if (MOReg == Reg && !isKill)
          // We can't schedule across a use of the register in question.
          return false;
        // Ensure that if this is register in question, its the kill we expect.
        assert((MOReg != Reg || &OtherMI == KillMI) &&
               "Found multiple kills of a register in a basic block");
      }
    }
  }

  // Move debug info as well.
  while (Begin != MBB->begin() && std::prev(Begin)->isDebugInstr())
    --Begin;

  nmi = End;
  MachineBasicBlock::iterator InsertPos = KillPos;
  if (LIS) {
    // We have to move the copies (and any interleaved debug instructions)
    // first so that the MBB is still well-formed when calling handleMove().
    for (MachineBasicBlock::iterator MBBI = AfterMI; MBBI != End;) {
      auto CopyMI = MBBI++;
      MBB->splice(InsertPos, MBB, CopyMI);
      if (!CopyMI->isDebugOrPseudoInstr())
        LIS->handleMove(*CopyMI);
      InsertPos = CopyMI;
    }
    End = std::next(MachineBasicBlock::iterator(MI));
  }

  // Copies following MI may have been moved as well.
  MBB->splice(InsertPos, MBB, Begin, End);
  DistanceMap.erase(DI);

  // Update live variables
  if (LIS) {
    LIS->handleMove(*MI);
  } else {
    LV->removeVirtualRegisterKilled(Reg, *KillMI);
    LV->addVirtualRegisterKilled(Reg, *MI);
  }

  LLVM_DEBUG(dbgs() << "\trescheduled below kill: " << *KillMI);
  return true;
}

/// Return true if the re-scheduling will put the given instruction too close
/// to the defs of its register dependencies.
bool TwoAddressInstructionImpl::isDefTooClose(Register Reg, unsigned Dist,
                                              MachineInstr *MI) {
  for (MachineInstr &DefMI : MRI->def_instructions(Reg)) {
    if (DefMI.getParent() != MBB || DefMI.isCopy() || DefMI.isCopyLike())
      continue;
    if (&DefMI == MI)
      return true; // MI is defining something KillMI uses
    DenseMap<MachineInstr*, unsigned>::iterator DDI = DistanceMap.find(&DefMI);
    if (DDI == DistanceMap.end())
      return true;  // Below MI
    unsigned DefDist = DDI->second;
    assert(Dist > DefDist && "Visited def already?");
    if (TII->getInstrLatency(InstrItins, DefMI) > (Dist - DefDist))
      return true;
  }
  return false;
}

/// If there is one more local instruction that reads 'Reg' and it kills 'Reg,
/// consider moving the kill instruction above the current two-address
/// instruction in order to eliminate the need for the copy.
bool TwoAddressInstructionImpl::rescheduleKillAboveMI(
    MachineBasicBlock::iterator &mi, MachineBasicBlock::iterator &nmi,
    Register Reg) {
  // Bail immediately if we don't have LV or LIS available. We use them to find
  // kills efficiently.
  if (!LV && !LIS)
    return false;

  MachineInstr *MI = &*mi;
  DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
  if (DI == DistanceMap.end())
    // Must be created from unfolded load. Don't waste time trying this.
    return false;

  MachineInstr *KillMI = nullptr;
  if (LIS) {
    LiveInterval &LI = LIS->getInterval(Reg);
    assert(LI.end() != LI.begin() &&
           "Reg should not have empty live interval.");

    SlotIndex MBBEndIdx = LIS->getMBBEndIdx(MBB).getPrevSlot();
    LiveInterval::const_iterator I = LI.find(MBBEndIdx);
    if (I != LI.end() && I->start < MBBEndIdx)
      return false;

    --I;
    KillMI = LIS->getInstructionFromIndex(I->end);
  } else {
    KillMI = LV->getVarInfo(Reg).findKill(MBB);
  }
  if (!KillMI || MI == KillMI || KillMI->isCopy() || KillMI->isCopyLike())
    // Don't mess with copies, they may be coalesced later.
    return false;

  Register DstReg;
  if (isTwoAddrUse(*KillMI, Reg, DstReg))
    return false;

  bool SeenStore = true;
  if (!KillMI->isSafeToMove(AA, SeenStore))
    return false;

  SmallVector<Register, 2> Uses;
  SmallVector<Register, 2> Kills;
  SmallVector<Register, 2> Defs;
  SmallVector<Register, 2> LiveDefs;
  for (const MachineOperand &MO : KillMI->operands()) {
    if (!MO.isReg())
      continue;
    Register MOReg = MO.getReg();
    if (MO.isUse()) {
      if (!MOReg)
        continue;
      if (isDefTooClose(MOReg, DI->second, MI))
        return false;
      bool isKill = isPlainlyKilled(MO);
      if (MOReg == Reg && !isKill)
        return false;
      Uses.push_back(MOReg);
      if (isKill && MOReg != Reg)
        Kills.push_back(MOReg);
    } else if (MOReg.isPhysical()) {
      Defs.push_back(MOReg);
      if (!MO.isDead())
        LiveDefs.push_back(MOReg);
    }
  }

  // Check if the reschedule will not break depedencies.
  unsigned NumVisited = 0;
  for (MachineInstr &OtherMI :
       make_range(mi, MachineBasicBlock::iterator(KillMI))) {
    // Debug or pseudo instructions cannot be counted against the limit.
    if (OtherMI.isDebugOrPseudoInstr())
      continue;
    if (NumVisited > 10)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    if (OtherMI.hasUnmodeledSideEffects() || OtherMI.isCall() ||
        OtherMI.isBranch() || OtherMI.isTerminator())
      // Don't move pass calls, etc.
      return false;
    SmallVector<Register, 2> OtherDefs;
    for (const MachineOperand &MO : OtherMI.operands()) {
      if (!MO.isReg())
        continue;
      Register MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (MO.isUse()) {
        if (regOverlapsSet(Defs, MOReg))
          // Moving KillMI can clobber the physical register if the def has
          // not been seen.
          return false;
        if (regOverlapsSet(Kills, MOReg))
          // Don't want to extend other live ranges and update kills.
          return false;
        if (&OtherMI != MI && MOReg == Reg && !isPlainlyKilled(MO))
          // We can't schedule across a use of the register in question.
          return false;
      } else {
        OtherDefs.push_back(MOReg);
      }
    }

    for (Register MOReg : OtherDefs) {
      if (regOverlapsSet(Uses, MOReg))
        return false;
      if (MOReg.isPhysical() && regOverlapsSet(LiveDefs, MOReg))
        return false;
      // Physical register def is seen.
      llvm::erase(Defs, MOReg);
    }
  }

  // Move the old kill above MI, don't forget to move debug info as well.
  MachineBasicBlock::iterator InsertPos = mi;
  while (InsertPos != MBB->begin() && std::prev(InsertPos)->isDebugInstr())
    --InsertPos;
  MachineBasicBlock::iterator From = KillMI;
  MachineBasicBlock::iterator To = std::next(From);
  while (std::prev(From)->isDebugInstr())
    --From;
  MBB->splice(InsertPos, MBB, From, To);

  nmi = std::prev(InsertPos); // Backtrack so we process the moved instr.
  DistanceMap.erase(DI);

  // Update live variables
  if (LIS) {
    LIS->handleMove(*KillMI);
  } else {
    LV->removeVirtualRegisterKilled(Reg, *KillMI);
    LV->addVirtualRegisterKilled(Reg, *MI);
  }

  LLVM_DEBUG(dbgs() << "\trescheduled kill: " << *KillMI);
  return true;
}

/// Tries to commute the operand 'BaseOpIdx' and some other operand in the
/// given machine instruction to improve opportunities for coalescing and
/// elimination of a register to register copy.
///
/// 'DstOpIdx' specifies the index of MI def operand.
/// 'BaseOpKilled' specifies if the register associated with 'BaseOpIdx'
/// operand is killed by the given instruction.
/// The 'Dist' arguments provides the distance of MI from the start of the
/// current basic block and it is used to determine if it is profitable
/// to commute operands in the instruction.
///
/// Returns true if the transformation happened. Otherwise, returns false.
bool TwoAddressInstructionImpl::tryInstructionCommute(MachineInstr *MI,
                                                      unsigned DstOpIdx,
                                                      unsigned BaseOpIdx,
                                                      bool BaseOpKilled,
                                                      unsigned Dist) {
  if (!MI->isCommutable())
    return false;

  bool MadeChange = false;
  Register DstOpReg = MI->getOperand(DstOpIdx).getReg();
  Register BaseOpReg = MI->getOperand(BaseOpIdx).getReg();
  unsigned OpsNum = MI->getDesc().getNumOperands();
  unsigned OtherOpIdx = MI->getDesc().getNumDefs();
  for (; OtherOpIdx < OpsNum; OtherOpIdx++) {
    // The call of findCommutedOpIndices below only checks if BaseOpIdx
    // and OtherOpIdx are commutable, it does not really search for
    // other commutable operands and does not change the values of passed
    // variables.
    if (OtherOpIdx == BaseOpIdx || !MI->getOperand(OtherOpIdx).isReg() ||
        !TII->findCommutedOpIndices(*MI, BaseOpIdx, OtherOpIdx))
      continue;

    Register OtherOpReg = MI->getOperand(OtherOpIdx).getReg();
    bool AggressiveCommute = false;

    // If OtherOp dies but BaseOp does not, swap the OtherOp and BaseOp
    // operands. This makes the live ranges of DstOp and OtherOp joinable.
    bool OtherOpKilled = isKilled(*MI, OtherOpReg, false);
    bool DoCommute = !BaseOpKilled && OtherOpKilled;

    if (!DoCommute &&
        isProfitableToCommute(DstOpReg, BaseOpReg, OtherOpReg, MI, Dist)) {
      DoCommute = true;
      AggressiveCommute = true;
    }

    // If it's profitable to commute, try to do so.
    if (DoCommute && commuteInstruction(MI, DstOpIdx, BaseOpIdx, OtherOpIdx,
                                        Dist)) {
      MadeChange = true;
      ++NumCommuted;
      if (AggressiveCommute)
        ++NumAggrCommuted;

      // There might be more than two commutable operands, update BaseOp and
      // continue scanning.
      // FIXME: This assumes that the new instruction's operands are in the
      // same positions and were simply swapped.
      BaseOpReg = OtherOpReg;
      BaseOpKilled = OtherOpKilled;
      // Resamples OpsNum in case the number of operands was reduced. This
      // happens with X86.
      OpsNum = MI->getDesc().getNumOperands();
    }
  }
  return MadeChange;
}

/// For the case where an instruction has a single pair of tied register
/// operands, attempt some transformations that may either eliminate the tied
/// operands or improve the opportunities for coalescing away the register copy.
/// Returns true if no copy needs to be inserted to untie mi's operands
/// (either because they were untied, or because mi was rescheduled, and will
/// be visited again later). If the shouldOnlyCommute flag is true, only
/// instruction commutation is attempted.
bool TwoAddressInstructionImpl::tryInstructionTransform(
    MachineBasicBlock::iterator &mi, MachineBasicBlock::iterator &nmi,
    unsigned SrcIdx, unsigned DstIdx, unsigned &Dist, bool shouldOnlyCommute) {
  if (OptLevel == CodeGenOptLevel::None)
    return false;

  MachineInstr &MI = *mi;
  Register regA = MI.getOperand(DstIdx).getReg();
  Register regB = MI.getOperand(SrcIdx).getReg();

  assert(regB.isVirtual() && "cannot make instruction into two-address form");
  bool regBKilled = isKilled(MI, regB, true);

  if (regA.isVirtual())
    scanUses(regA);

  bool Commuted = tryInstructionCommute(&MI, DstIdx, SrcIdx, regBKilled, Dist);

  // If the instruction is convertible to 3 Addr, instead
  // of returning try 3 Addr transformation aggressively and
  // use this variable to check later. Because it might be better.
  // For example, we can just use `leal (%rsi,%rdi), %eax` and `ret`
  // instead of the following code.
  //   addl     %esi, %edi
  //   movl     %edi, %eax
  //   ret
  if (Commuted && !MI.isConvertibleTo3Addr())
    return false;

  if (shouldOnlyCommute)
    return false;

  // If there is one more use of regB later in the same MBB, consider
  // re-schedule this MI below it.
  if (!Commuted && EnableRescheduling && rescheduleMIBelowKill(mi, nmi, regB)) {
    ++NumReSchedDowns;
    return true;
  }

  // If we commuted, regB may have changed so we should re-sample it to avoid
  // confusing the three address conversion below.
  if (Commuted) {
    regB = MI.getOperand(SrcIdx).getReg();
    regBKilled = isKilled(MI, regB, true);
  }

  if (MI.isConvertibleTo3Addr()) {
    // This instruction is potentially convertible to a true
    // three-address instruction.  Check if it is profitable.
    if (!regBKilled || isProfitableToConv3Addr(regA, regB)) {
      // Try to convert it.
      if (convertInstTo3Addr(mi, nmi, regA, regB, Dist)) {
        ++NumConvertedTo3Addr;
        return true; // Done with this instruction.
      }
    }
  }

  // Return if it is commuted but 3 addr conversion is failed.
  if (Commuted)
    return false;

  // If there is one more use of regB later in the same MBB, consider
  // re-schedule it before this MI if it's legal.
  if (EnableRescheduling && rescheduleKillAboveMI(mi, nmi, regB)) {
    ++NumReSchedUps;
    return true;
  }

  // If this is an instruction with a load folded into it, try unfolding
  // the load, e.g. avoid this:
  //   movq %rdx, %rcx
  //   addq (%rax), %rcx
  // in favor of this:
  //   movq (%rax), %rcx
  //   addq %rdx, %rcx
  // because it's preferable to schedule a load than a register copy.
  if (MI.mayLoad() && !regBKilled) {
    // Determine if a load can be unfolded.
    unsigned LoadRegIndex;
    unsigned NewOpc =
      TII->getOpcodeAfterMemoryUnfold(MI.getOpcode(),
                                      /*UnfoldLoad=*/true,
                                      /*UnfoldStore=*/false,
                                      &LoadRegIndex);
    if (NewOpc != 0) {
      const MCInstrDesc &UnfoldMCID = TII->get(NewOpc);
      if (UnfoldMCID.getNumDefs() == 1) {
        // Unfold the load.
        LLVM_DEBUG(dbgs() << "2addr:   UNFOLDING: " << MI);
        const TargetRegisterClass *RC =
          TRI->getAllocatableClass(
            TII->getRegClass(UnfoldMCID, LoadRegIndex, TRI, *MF));
        Register Reg = MRI->createVirtualRegister(RC);
        SmallVector<MachineInstr *, 2> NewMIs;
        if (!TII->unfoldMemoryOperand(*MF, MI, Reg,
                                      /*UnfoldLoad=*/true,
                                      /*UnfoldStore=*/false, NewMIs)) {
          LLVM_DEBUG(dbgs() << "2addr: ABANDONING UNFOLD\n");
          return false;
        }
        assert(NewMIs.size() == 2 &&
               "Unfolded a load into multiple instructions!");
        // The load was previously folded, so this is the only use.
        NewMIs[1]->addRegisterKilled(Reg, TRI);

        // Tentatively insert the instructions into the block so that they
        // look "normal" to the transformation logic.
        MBB->insert(mi, NewMIs[0]);
        MBB->insert(mi, NewMIs[1]);
        DistanceMap.insert(std::make_pair(NewMIs[0], Dist++));
        DistanceMap.insert(std::make_pair(NewMIs[1], Dist));

        LLVM_DEBUG(dbgs() << "2addr:    NEW LOAD: " << *NewMIs[0]
                          << "2addr:    NEW INST: " << *NewMIs[1]);

        // Transform the instruction, now that it no longer has a load.
        unsigned NewDstIdx =
            NewMIs[1]->findRegisterDefOperandIdx(regA, /*TRI=*/nullptr);
        unsigned NewSrcIdx =
            NewMIs[1]->findRegisterUseOperandIdx(regB, /*TRI=*/nullptr);
        MachineBasicBlock::iterator NewMI = NewMIs[1];
        bool TransformResult =
          tryInstructionTransform(NewMI, mi, NewSrcIdx, NewDstIdx, Dist, true);
        (void)TransformResult;
        assert(!TransformResult &&
               "tryInstructionTransform() should return false.");
        if (NewMIs[1]->getOperand(NewSrcIdx).isKill()) {
          // Success, or at least we made an improvement. Keep the unfolded
          // instructions and discard the original.
          if (LV) {
            for (const MachineOperand &MO : MI.operands()) {
              if (MO.isReg() && MO.getReg().isVirtual()) {
                if (MO.isUse()) {
                  if (MO.isKill()) {
                    if (NewMIs[0]->killsRegister(MO.getReg(), /*TRI=*/nullptr))
                      LV->replaceKillInstruction(MO.getReg(), MI, *NewMIs[0]);
                    else {
                      assert(NewMIs[1]->killsRegister(MO.getReg(),
                                                      /*TRI=*/nullptr) &&
                             "Kill missing after load unfold!");
                      LV->replaceKillInstruction(MO.getReg(), MI, *NewMIs[1]);
                    }
                  }
                } else if (LV->removeVirtualRegisterDead(MO.getReg(), MI)) {
                  if (NewMIs[1]->registerDefIsDead(MO.getReg(),
                                                   /*TRI=*/nullptr))
                    LV->addVirtualRegisterDead(MO.getReg(), *NewMIs[1]);
                  else {
                    assert(NewMIs[0]->registerDefIsDead(MO.getReg(),
                                                        /*TRI=*/nullptr) &&
                           "Dead flag missing after load unfold!");
                    LV->addVirtualRegisterDead(MO.getReg(), *NewMIs[0]);
                  }
                }
              }
            }
            LV->addVirtualRegisterKilled(Reg, *NewMIs[1]);
          }

          SmallVector<Register, 4> OrigRegs;
          if (LIS) {
            for (const MachineOperand &MO : MI.operands()) {
              if (MO.isReg())
                OrigRegs.push_back(MO.getReg());
            }

            LIS->RemoveMachineInstrFromMaps(MI);
          }

          MI.eraseFromParent();
          DistanceMap.erase(&MI);

          // Update LiveIntervals.
          if (LIS) {
            MachineBasicBlock::iterator Begin(NewMIs[0]);
            MachineBasicBlock::iterator End(NewMIs[1]);
            LIS->repairIntervalsInRange(MBB, Begin, End, OrigRegs);
          }

          mi = NewMIs[1];
        } else {
          // Transforming didn't eliminate the tie and didn't lead to an
          // improvement. Clean up the unfolded instructions and keep the
          // original.
          LLVM_DEBUG(dbgs() << "2addr: ABANDONING UNFOLD\n");
          NewMIs[0]->eraseFromParent();
          NewMIs[1]->eraseFromParent();
          DistanceMap.erase(NewMIs[0]);
          DistanceMap.erase(NewMIs[1]);
          Dist--;
        }
      }
    }
  }

  return false;
}

// Collect tied operands of MI that need to be handled.
// Rewrite trivial cases immediately.
// Return true if any tied operands where found, including the trivial ones.
bool TwoAddressInstructionImpl::collectTiedOperands(
    MachineInstr *MI, TiedOperandMap &TiedOperands) {
  bool AnyOps = false;
  unsigned NumOps = MI->getNumOperands();

  for (unsigned SrcIdx = 0; SrcIdx < NumOps; ++SrcIdx) {
    unsigned DstIdx = 0;
    if (!MI->isRegTiedToDefOperand(SrcIdx, &DstIdx))
      continue;
    AnyOps = true;
    MachineOperand &SrcMO = MI->getOperand(SrcIdx);
    MachineOperand &DstMO = MI->getOperand(DstIdx);
    Register SrcReg = SrcMO.getReg();
    Register DstReg = DstMO.getReg();
    // Tied constraint already satisfied?
    if (SrcReg == DstReg)
      continue;

    assert(SrcReg && SrcMO.isUse() && "two address instruction invalid");

    // Deal with undef uses immediately - simply rewrite the src operand.
    if (SrcMO.isUndef() && !DstMO.getSubReg()) {
      // Constrain the DstReg register class if required.
      if (DstReg.isVirtual()) {
        const TargetRegisterClass *RC = MRI->getRegClass(SrcReg);
        MRI->constrainRegClass(DstReg, RC);
      }
      SrcMO.setReg(DstReg);
      SrcMO.setSubReg(0);
      LLVM_DEBUG(dbgs() << "\t\trewrite undef:\t" << *MI);
      continue;
    }
    TiedOperands[SrcReg].push_back(std::make_pair(SrcIdx, DstIdx));
  }
  return AnyOps;
}

// Process a list of tied MI operands that all use the same source register.
// The tied pairs are of the form (SrcIdx, DstIdx).
void TwoAddressInstructionImpl::processTiedPairs(MachineInstr *MI,
                                                 TiedPairList &TiedPairs,
                                                 unsigned &Dist) {
  bool IsEarlyClobber = llvm::any_of(TiedPairs, [MI](auto const &TP) {
    return MI->getOperand(TP.second).isEarlyClobber();
  });

  bool RemovedKillFlag = false;
  bool AllUsesCopied = true;
  unsigned LastCopiedReg = 0;
  SlotIndex LastCopyIdx;
  Register RegB = 0;
  unsigned SubRegB = 0;
  for (auto &TP : TiedPairs) {
    unsigned SrcIdx = TP.first;
    unsigned DstIdx = TP.second;

    const MachineOperand &DstMO = MI->getOperand(DstIdx);
    Register RegA = DstMO.getReg();

    // Grab RegB from the instruction because it may have changed if the
    // instruction was commuted.
    RegB = MI->getOperand(SrcIdx).getReg();
    SubRegB = MI->getOperand(SrcIdx).getSubReg();

    if (RegA == RegB) {
      // The register is tied to multiple destinations (or else we would
      // not have continued this far), but this use of the register
      // already matches the tied destination.  Leave it.
      AllUsesCopied = false;
      continue;
    }
    LastCopiedReg = RegA;

    assert(RegB.isVirtual() && "cannot make instruction into two-address form");

#ifndef NDEBUG
    // First, verify that we don't have a use of "a" in the instruction
    // (a = b + a for example) because our transformation will not
    // work. This should never occur because we are in SSA form.
    for (unsigned i = 0; i != MI->getNumOperands(); ++i)
      assert(i == DstIdx ||
             !MI->getOperand(i).isReg() ||
             MI->getOperand(i).getReg() != RegA);
#endif

    // Emit a copy.
    MachineInstrBuilder MIB = BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
                                      TII->get(TargetOpcode::COPY), RegA);
    // If this operand is folding a truncation, the truncation now moves to the
    // copy so that the register classes remain valid for the operands.
    MIB.addReg(RegB, 0, SubRegB);
    const TargetRegisterClass *RC = MRI->getRegClass(RegB);
    if (SubRegB) {
      if (RegA.isVirtual()) {
        assert(TRI->getMatchingSuperRegClass(RC, MRI->getRegClass(RegA),
                                             SubRegB) &&
               "tied subregister must be a truncation");
        // The superreg class will not be used to constrain the subreg class.
        RC = nullptr;
      } else {
        assert(TRI->getMatchingSuperReg(RegA, SubRegB, MRI->getRegClass(RegB))
               && "tied subregister must be a truncation");
      }
    }

    // Update DistanceMap.
    MachineBasicBlock::iterator PrevMI = MI;
    --PrevMI;
    DistanceMap.insert(std::make_pair(&*PrevMI, Dist));
    DistanceMap[MI] = ++Dist;

    if (LIS) {
      LastCopyIdx = LIS->InsertMachineInstrInMaps(*PrevMI).getRegSlot();

      SlotIndex endIdx =
          LIS->getInstructionIndex(*MI).getRegSlot(IsEarlyClobber);
      if (RegA.isVirtual()) {
        LiveInterval &LI = LIS->getInterval(RegA);
        VNInfo *VNI = LI.getNextValue(LastCopyIdx, LIS->getVNInfoAllocator());
        LI.addSegment(LiveRange::Segment(LastCopyIdx, endIdx, VNI));
        for (auto &S : LI.subranges()) {
          VNI = S.getNextValue(LastCopyIdx, LIS->getVNInfoAllocator());
          S.addSegment(LiveRange::Segment(LastCopyIdx, endIdx, VNI));
        }
      } else {
        for (MCRegUnit Unit : TRI->regunits(RegA)) {
          if (LiveRange *LR = LIS->getCachedRegUnit(Unit)) {
            VNInfo *VNI =
                LR->getNextValue(LastCopyIdx, LIS->getVNInfoAllocator());
            LR->addSegment(LiveRange::Segment(LastCopyIdx, endIdx, VNI));
          }
        }
      }
    }

    LLVM_DEBUG(dbgs() << "\t\tprepend:\t" << *MIB);

    MachineOperand &MO = MI->getOperand(SrcIdx);
    assert(MO.isReg() && MO.getReg() == RegB && MO.isUse() &&
           "inconsistent operand info for 2-reg pass");
    if (isPlainlyKilled(MO)) {
      MO.setIsKill(false);
      RemovedKillFlag = true;
    }

    // Make sure regA is a legal regclass for the SrcIdx operand.
    if (RegA.isVirtual() && RegB.isVirtual())
      MRI->constrainRegClass(RegA, RC);
    MO.setReg(RegA);
    // The getMatchingSuper asserts guarantee that the register class projected
    // by SubRegB is compatible with RegA with no subregister. So regardless of
    // whether the dest oper writes a subreg, the source oper should not.
    MO.setSubReg(0);
  }

  if (AllUsesCopied) {
    LaneBitmask RemainingUses = LaneBitmask::getNone();
    // Replace other (un-tied) uses of regB with LastCopiedReg.
    for (MachineOperand &MO : MI->all_uses()) {
      if (MO.getReg() == RegB) {
        if (MO.getSubReg() == SubRegB && !IsEarlyClobber) {
          if (isPlainlyKilled(MO)) {
            MO.setIsKill(false);
            RemovedKillFlag = true;
          }
          MO.setReg(LastCopiedReg);
          MO.setSubReg(0);
        } else {
          RemainingUses |= TRI->getSubRegIndexLaneMask(MO.getSubReg());
        }
      }
    }

    // Update live variables for regB.
    if (RemovedKillFlag && RemainingUses.none() && LV &&
        LV->getVarInfo(RegB).removeKill(*MI)) {
      MachineBasicBlock::iterator PrevMI = MI;
      --PrevMI;
      LV->addVirtualRegisterKilled(RegB, *PrevMI);
    }

    if (RemovedKillFlag && RemainingUses.none())
      SrcRegMap[LastCopiedReg] = RegB;

    // Update LiveIntervals.
    if (LIS) {
      SlotIndex UseIdx = LIS->getInstructionIndex(*MI);
      auto Shrink = [=](LiveRange &LR, LaneBitmask LaneMask) {
        LiveRange::Segment *S = LR.getSegmentContaining(LastCopyIdx);
        if (!S)
          return true;
        if ((LaneMask & RemainingUses).any())
          return false;
        if (S->end.getBaseIndex() != UseIdx)
          return false;
        S->end = LastCopyIdx;
        return true;
      };

      LiveInterval &LI = LIS->getInterval(RegB);
      bool ShrinkLI = true;
      for (auto &S : LI.subranges())
        ShrinkLI &= Shrink(S, S.LaneMask);
      if (ShrinkLI)
        Shrink(LI, LaneBitmask::getAll());
    }
  } else if (RemovedKillFlag) {
    // Some tied uses of regB matched their destination registers, so
    // regB is still used in this instruction, but a kill flag was
    // removed from a different tied use of regB, so now we need to add
    // a kill flag to one of the remaining uses of regB.
    for (MachineOperand &MO : MI->all_uses()) {
      if (MO.getReg() == RegB) {
        MO.setIsKill(true);
        break;
      }
    }
  }
}

// For every tied operand pair this function transforms statepoint from
//    RegA = STATEPOINT ... RegB(tied-def N)
// to
//    RegB = STATEPOINT ... RegB(tied-def N)
// and replaces all uses of RegA with RegB.
// No extra COPY instruction is necessary because tied use is killed at
// STATEPOINT.
bool TwoAddressInstructionImpl::processStatepoint(
    MachineInstr *MI, TiedOperandMap &TiedOperands) {

  bool NeedCopy = false;
  for (auto &TO : TiedOperands) {
    Register RegB = TO.first;
    if (TO.second.size() != 1) {
      NeedCopy = true;
      continue;
    }

    unsigned SrcIdx = TO.second[0].first;
    unsigned DstIdx = TO.second[0].second;

    MachineOperand &DstMO = MI->getOperand(DstIdx);
    Register RegA = DstMO.getReg();

    assert(RegB == MI->getOperand(SrcIdx).getReg());

    if (RegA == RegB)
      continue;

    // CodeGenPrepare can sink pointer compare past statepoint, which
    // breaks assumption that statepoint kills tied-use register when
    // in SSA form (see note in IR/SafepointIRVerifier.cpp). Fall back
    // to generic tied register handling to avoid assertion failures.
    // TODO: Recompute LIS/LV information for new range here.
    if (LIS) {
      const auto &UseLI = LIS->getInterval(RegB);
      const auto &DefLI = LIS->getInterval(RegA);
      if (DefLI.overlaps(UseLI)) {
        LLVM_DEBUG(dbgs() << "LIS: " << printReg(RegB, TRI, 0)
                          << " UseLI overlaps with DefLI\n");
        NeedCopy = true;
        continue;
      }
    } else if (LV && LV->getVarInfo(RegB).findKill(MI->getParent()) != MI) {
      // Note that MachineOperand::isKill does not work here, because it
      // is set only on first register use in instruction and for statepoint
      // tied-use register will usually be found in preceeding deopt bundle.
      LLVM_DEBUG(dbgs() << "LV: " << printReg(RegB, TRI, 0)
                        << " not killed by statepoint\n");
      NeedCopy = true;
      continue;
    }

    if (!MRI->constrainRegClass(RegB, MRI->getRegClass(RegA))) {
      LLVM_DEBUG(dbgs() << "MRI: couldn't constrain" << printReg(RegB, TRI, 0)
                        << " to register class of " << printReg(RegA, TRI, 0)
                        << '\n');
      NeedCopy = true;
      continue;
    }
    MRI->replaceRegWith(RegA, RegB);

    if (LIS) {
      VNInfo::Allocator &A = LIS->getVNInfoAllocator();
      LiveInterval &LI = LIS->getInterval(RegB);
      LiveInterval &Other = LIS->getInterval(RegA);
      SmallVector<VNInfo *> NewVNIs;
      for (const VNInfo *VNI : Other.valnos) {
        assert(VNI->id == NewVNIs.size() && "assumed");
        NewVNIs.push_back(LI.createValueCopy(VNI, A));
      }
      for (auto &S : Other) {
        VNInfo *VNI = NewVNIs[S.valno->id];
        LiveRange::Segment NewSeg(S.start, S.end, VNI);
        LI.addSegment(NewSeg);
      }
      LIS->removeInterval(RegA);
    }

    if (LV) {
      if (MI->getOperand(SrcIdx).isKill())
        LV->removeVirtualRegisterKilled(RegB, *MI);
      LiveVariables::VarInfo &SrcInfo = LV->getVarInfo(RegB);
      LiveVariables::VarInfo &DstInfo = LV->getVarInfo(RegA);
      SrcInfo.AliveBlocks |= DstInfo.AliveBlocks;
      DstInfo.AliveBlocks.clear();
      for (auto *KillMI : DstInfo.Kills)
        LV->addVirtualRegisterKilled(RegB, *KillMI, false);
    }
  }
  return !NeedCopy;
}

/// Reduce two-address instructions to two operands.
bool TwoAddressInstructionImpl::run() {
  bool MadeChange = false;

  LLVM_DEBUG(dbgs() << "********** REWRITING TWO-ADDR INSTRS **********\n");
  LLVM_DEBUG(dbgs() << "********** Function: " << MF->getName() << '\n');

  // This pass takes the function out of SSA form.
  MRI->leaveSSA();

  // This pass will rewrite the tied-def to meet the RegConstraint.
  MF->getProperties()
      .set(MachineFunctionProperties::Property::TiedOpsRewritten);

  TiedOperandMap TiedOperands;
  for (MachineBasicBlock &MBBI : *MF) {
    MBB = &MBBI;
    unsigned Dist = 0;
    DistanceMap.clear();
    SrcRegMap.clear();
    DstRegMap.clear();
    Processed.clear();
    for (MachineBasicBlock::iterator mi = MBB->begin(), me = MBB->end();
         mi != me; ) {
      MachineBasicBlock::iterator nmi = std::next(mi);
      // Skip debug instructions.
      if (mi->isDebugInstr()) {
        mi = nmi;
        continue;
      }

      // Expand REG_SEQUENCE instructions. This will position mi at the first
      // expanded instruction.
      if (mi->isRegSequence())
        eliminateRegSequence(mi);

      DistanceMap.insert(std::make_pair(&*mi, ++Dist));

      processCopy(&*mi);

      // First scan through all the tied register uses in this instruction
      // and record a list of pairs of tied operands for each register.
      if (!collectTiedOperands(&*mi, TiedOperands)) {
        removeClobberedSrcRegMap(&*mi);
        mi = nmi;
        continue;
      }

      ++NumTwoAddressInstrs;
      MadeChange = true;
      LLVM_DEBUG(dbgs() << '\t' << *mi);

      // If the instruction has a single pair of tied operands, try some
      // transformations that may either eliminate the tied operands or
      // improve the opportunities for coalescing away the register copy.
      if (TiedOperands.size() == 1) {
        SmallVectorImpl<std::pair<unsigned, unsigned>> &TiedPairs
          = TiedOperands.begin()->second;
        if (TiedPairs.size() == 1) {
          unsigned SrcIdx = TiedPairs[0].first;
          unsigned DstIdx = TiedPairs[0].second;
          Register SrcReg = mi->getOperand(SrcIdx).getReg();
          Register DstReg = mi->getOperand(DstIdx).getReg();
          if (SrcReg != DstReg &&
              tryInstructionTransform(mi, nmi, SrcIdx, DstIdx, Dist, false)) {
            // The tied operands have been eliminated or shifted further down
            // the block to ease elimination. Continue processing with 'nmi'.
            TiedOperands.clear();
            removeClobberedSrcRegMap(&*mi);
            mi = nmi;
            continue;
          }
        }
      }

      if (mi->getOpcode() == TargetOpcode::STATEPOINT &&
          processStatepoint(&*mi, TiedOperands)) {
        TiedOperands.clear();
        LLVM_DEBUG(dbgs() << "\t\trewrite to:\t" << *mi);
        mi = nmi;
        continue;
      }

      // Now iterate over the information collected above.
      for (auto &TO : TiedOperands) {
        processTiedPairs(&*mi, TO.second, Dist);
        LLVM_DEBUG(dbgs() << "\t\trewrite to:\t" << *mi);
      }

      // Rewrite INSERT_SUBREG as COPY now that we no longer need SSA form.
      if (mi->isInsertSubreg()) {
        // From %reg = INSERT_SUBREG %reg, %subreg, subidx
        // To   %reg:subidx = COPY %subreg
        unsigned SubIdx = mi->getOperand(3).getImm();
        mi->removeOperand(3);
        assert(mi->getOperand(0).getSubReg() == 0 && "Unexpected subreg idx");
        mi->getOperand(0).setSubReg(SubIdx);
        mi->getOperand(0).setIsUndef(mi->getOperand(1).isUndef());
        mi->removeOperand(1);
        mi->setDesc(TII->get(TargetOpcode::COPY));
        LLVM_DEBUG(dbgs() << "\t\tconvert to:\t" << *mi);

        // Update LiveIntervals.
        if (LIS) {
          Register Reg = mi->getOperand(0).getReg();
          LiveInterval &LI = LIS->getInterval(Reg);
          if (LI.hasSubRanges()) {
            // The COPY no longer defines subregs of %reg except for
            // %reg.subidx.
            LaneBitmask LaneMask =
                TRI->getSubRegIndexLaneMask(mi->getOperand(0).getSubReg());
            SlotIndex Idx = LIS->getInstructionIndex(*mi).getRegSlot();
            for (auto &S : LI.subranges()) {
              if ((S.LaneMask & LaneMask).none()) {
                LiveRange::iterator DefSeg = S.FindSegmentContaining(Idx);
                if (mi->getOperand(0).isUndef()) {
                  S.removeValNo(DefSeg->valno);
                } else {
                  LiveRange::iterator UseSeg = std::prev(DefSeg);
                  S.MergeValueNumberInto(DefSeg->valno, UseSeg->valno);
                }
              }
            }

            // The COPY no longer has a use of %reg.
            LIS->shrinkToUses(&LI);
          } else {
            // The live interval for Reg did not have subranges but now it needs
            // them because we have introduced a subreg def. Recompute it.
            LIS->removeInterval(Reg);
            LIS->createAndComputeVirtRegInterval(Reg);
          }
        }
      }

      // Clear TiedOperands here instead of at the top of the loop
      // since most instructions do not have tied operands.
      TiedOperands.clear();
      removeClobberedSrcRegMap(&*mi);
      mi = nmi;
    }
  }

  return MadeChange;
}

/// Eliminate a REG_SEQUENCE instruction as part of the de-ssa process.
///
/// The instruction is turned into a sequence of sub-register copies:
///
///   %dst = REG_SEQUENCE %v1, ssub0, %v2, ssub1
///
/// Becomes:
///
///   undef %dst:ssub0 = COPY %v1
///   %dst:ssub1 = COPY %v2
void TwoAddressInstructionImpl::eliminateRegSequence(
    MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  Register DstReg = MI.getOperand(0).getReg();

  SmallVector<Register, 4> OrigRegs;
  VNInfo *DefVN = nullptr;
  if (LIS) {
    OrigRegs.push_back(MI.getOperand(0).getReg());
    for (unsigned i = 1, e = MI.getNumOperands(); i < e; i += 2)
      OrigRegs.push_back(MI.getOperand(i).getReg());
    if (LIS->hasInterval(DstReg)) {
      DefVN = LIS->getInterval(DstReg)
                  .Query(LIS->getInstructionIndex(MI))
                  .valueOut();
    }
  }

  LaneBitmask UndefLanes = LaneBitmask::getNone();
  bool DefEmitted = false;
  for (unsigned i = 1, e = MI.getNumOperands(); i < e; i += 2) {
    MachineOperand &UseMO = MI.getOperand(i);
    Register SrcReg = UseMO.getReg();
    unsigned SubIdx = MI.getOperand(i+1).getImm();
    // Nothing needs to be inserted for undef operands.
    if (UseMO.isUndef()) {
      UndefLanes |= TRI->getSubRegIndexLaneMask(SubIdx);
      continue;
    }

    // Defer any kill flag to the last operand using SrcReg. Otherwise, we
    // might insert a COPY that uses SrcReg after is was killed.
    bool isKill = UseMO.isKill();
    if (isKill)
      for (unsigned j = i + 2; j < e; j += 2)
        if (MI.getOperand(j).getReg() == SrcReg) {
          MI.getOperand(j).setIsKill();
          UseMO.setIsKill(false);
          isKill = false;
          break;
        }

    // Insert the sub-register copy.
    MachineInstr *CopyMI = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                                   TII->get(TargetOpcode::COPY))
                               .addReg(DstReg, RegState::Define, SubIdx)
                               .add(UseMO);

    // The first def needs an undef flag because there is no live register
    // before it.
    if (!DefEmitted) {
      CopyMI->getOperand(0).setIsUndef(true);
      // Return an iterator pointing to the first inserted instr.
      MBBI = CopyMI;
    }
    DefEmitted = true;

    // Update LiveVariables' kill info.
    if (LV && isKill && !SrcReg.isPhysical())
      LV->replaceKillInstruction(SrcReg, MI, *CopyMI);

    LLVM_DEBUG(dbgs() << "Inserted: " << *CopyMI);
  }

  MachineBasicBlock::iterator EndMBBI =
      std::next(MachineBasicBlock::iterator(MI));

  if (!DefEmitted) {
    LLVM_DEBUG(dbgs() << "Turned: " << MI << " into an IMPLICIT_DEF");
    MI.setDesc(TII->get(TargetOpcode::IMPLICIT_DEF));
    for (int j = MI.getNumOperands() - 1, ee = 0; j > ee; --j)
      MI.removeOperand(j);
  } else {
    if (LIS) {
      // Force live interval recomputation if we moved to a partial definition
      // of the register.  Undef flags must be propagate to uses of undefined
      // subregister for accurate interval computation.
      if (UndefLanes.any() && DefVN && MRI->shouldTrackSubRegLiveness(DstReg)) {
        auto &LI = LIS->getInterval(DstReg);
        for (MachineOperand &UseOp : MRI->use_operands(DstReg)) {
          unsigned SubReg = UseOp.getSubReg();
          if (UseOp.isUndef() || !SubReg)
            continue;
          auto *VN =
              LI.getVNInfoAt(LIS->getInstructionIndex(*UseOp.getParent()));
          if (DefVN != VN)
            continue;
          LaneBitmask LaneMask = TRI->getSubRegIndexLaneMask(SubReg);
          if ((UndefLanes & LaneMask).any())
            UseOp.setIsUndef(true);
        }
        LIS->removeInterval(DstReg);
      }
      LIS->RemoveMachineInstrFromMaps(MI);
    }

    LLVM_DEBUG(dbgs() << "Eliminated: " << MI);
    MI.eraseFromParent();
  }

  // Udpate LiveIntervals.
  if (LIS)
    LIS->repairIntervalsInRange(MBB, MBBI, EndMBBI, OrigRegs);
}
