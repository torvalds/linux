//===- ImplicitNullChecks.cpp - Fold null checks into memory accesses -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass turns explicit null checks of the form
//
//   test %r10, %r10
//   je throw_npe
//   movl (%r10), %esi
//   ...
//
// to
//
//   faulting_load_op("movl (%r10), %esi", throw_npe)
//   ...
//
// With the help of a runtime that understands the .fault_maps section,
// faulting_load_op branches to throw_npe if executing movl (%r10), %esi incurs
// a page fault.
// Store and LoadStore are also supported.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/FaultMaps.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

static cl::opt<int> PageSize("imp-null-check-page-size",
                             cl::desc("The page size of the target in bytes"),
                             cl::init(4096), cl::Hidden);

static cl::opt<unsigned> MaxInstsToConsider(
    "imp-null-max-insts-to-consider",
    cl::desc("The max number of instructions to consider hoisting loads over "
             "(the algorithm is quadratic over this number)"),
    cl::Hidden, cl::init(8));

#define DEBUG_TYPE "implicit-null-checks"

STATISTIC(NumImplicitNullChecks,
          "Number of explicit null checks made implicit");

namespace {

class ImplicitNullChecks : public MachineFunctionPass {
  /// Return true if \c computeDependence can process \p MI.
  static bool canHandle(const MachineInstr *MI);

  /// Helper function for \c computeDependence.  Return true if \p A
  /// and \p B do not have any dependences between them, and can be
  /// re-ordered without changing program semantics.
  bool canReorder(const MachineInstr *A, const MachineInstr *B);

  /// A data type for representing the result computed by \c
  /// computeDependence.  States whether it is okay to reorder the
  /// instruction passed to \c computeDependence with at most one
  /// dependency.
  struct DependenceResult {
    /// Can we actually re-order \p MI with \p Insts (see \c
    /// computeDependence).
    bool CanReorder;

    /// If non-std::nullopt, then an instruction in \p Insts that also must be
    /// hoisted.
    std::optional<ArrayRef<MachineInstr *>::iterator> PotentialDependence;

    /*implicit*/ DependenceResult(
        bool CanReorder,
        std::optional<ArrayRef<MachineInstr *>::iterator> PotentialDependence)
        : CanReorder(CanReorder), PotentialDependence(PotentialDependence) {
      assert((!PotentialDependence || CanReorder) &&
             "!CanReorder && PotentialDependence.hasValue() not allowed!");
    }
  };

  /// Compute a result for the following question: can \p MI be
  /// re-ordered from after \p Insts to before it.
  ///
  /// \c canHandle should return true for all instructions in \p
  /// Insts.
  DependenceResult computeDependence(const MachineInstr *MI,
                                     ArrayRef<MachineInstr *> Block);

  /// Represents one null check that can be made implicit.
  class NullCheck {
    // The memory operation the null check can be folded into.
    MachineInstr *MemOperation;

    // The instruction actually doing the null check (Ptr != 0).
    MachineInstr *CheckOperation;

    // The block the check resides in.
    MachineBasicBlock *CheckBlock;

    // The block branched to if the pointer is non-null.
    MachineBasicBlock *NotNullSucc;

    // The block branched to if the pointer is null.
    MachineBasicBlock *NullSucc;

    // If this is non-null, then MemOperation has a dependency on this
    // instruction; and it needs to be hoisted to execute before MemOperation.
    MachineInstr *OnlyDependency;

  public:
    explicit NullCheck(MachineInstr *memOperation, MachineInstr *checkOperation,
                       MachineBasicBlock *checkBlock,
                       MachineBasicBlock *notNullSucc,
                       MachineBasicBlock *nullSucc,
                       MachineInstr *onlyDependency)
        : MemOperation(memOperation), CheckOperation(checkOperation),
          CheckBlock(checkBlock), NotNullSucc(notNullSucc), NullSucc(nullSucc),
          OnlyDependency(onlyDependency) {}

    MachineInstr *getMemOperation() const { return MemOperation; }

    MachineInstr *getCheckOperation() const { return CheckOperation; }

    MachineBasicBlock *getCheckBlock() const { return CheckBlock; }

    MachineBasicBlock *getNotNullSucc() const { return NotNullSucc; }

    MachineBasicBlock *getNullSucc() const { return NullSucc; }

    MachineInstr *getOnlyDependency() const { return OnlyDependency; }
  };

  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  AliasAnalysis *AA = nullptr;
  MachineFrameInfo *MFI = nullptr;

  bool analyzeBlockForNullChecks(MachineBasicBlock &MBB,
                                 SmallVectorImpl<NullCheck> &NullCheckList);
  MachineInstr *insertFaultingInstr(MachineInstr *MI, MachineBasicBlock *MBB,
                                    MachineBasicBlock *HandlerMBB);
  void rewriteNullChecks(ArrayRef<NullCheck> NullCheckList);

  enum AliasResult {
    AR_NoAlias,
    AR_MayAlias,
    AR_WillAliasEverything
  };

  /// Returns AR_NoAlias if \p MI memory operation does not alias with
  /// \p PrevMI, AR_MayAlias if they may alias and AR_WillAliasEverything if
  /// they may alias and any further memory operation may alias with \p PrevMI.
  AliasResult areMemoryOpsAliased(const MachineInstr &MI,
                                  const MachineInstr *PrevMI) const;

  enum SuitabilityResult {
    SR_Suitable,
    SR_Unsuitable,
    SR_Impossible
  };

  /// Return SR_Suitable if \p MI a memory operation that can be used to
  /// implicitly null check the value in \p PointerReg, SR_Unsuitable if
  /// \p MI cannot be used to null check and SR_Impossible if there is
  /// no sense to continue lookup due to any other instruction will not be able
  /// to be used. \p PrevInsts is the set of instruction seen since
  /// the explicit null check on \p PointerReg.
  SuitabilityResult isSuitableMemoryOp(const MachineInstr &MI,
                                       unsigned PointerReg,
                                       ArrayRef<MachineInstr *> PrevInsts);

  /// Returns true if \p DependenceMI can clobber the liveIns in NullSucc block
  /// if it was hoisted to the NullCheck block. This is used by caller
  /// canHoistInst to decide if DependenceMI can be hoisted safely.
  bool canDependenceHoistingClobberLiveIns(MachineInstr *DependenceMI,
                                           MachineBasicBlock *NullSucc);

  /// Return true if \p FaultingMI can be hoisted from after the
  /// instructions in \p InstsSeenSoFar to before them.  Set \p Dependence to a
  /// non-null value if we also need to (and legally can) hoist a dependency.
  bool canHoistInst(MachineInstr *FaultingMI,
                    ArrayRef<MachineInstr *> InstsSeenSoFar,
                    MachineBasicBlock *NullSucc, MachineInstr *&Dependence);

public:
  static char ID;

  ImplicitNullChecks() : MachineFunctionPass(ID) {
    initializeImplicitNullChecksPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AAResultsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }
};

} // end anonymous namespace

bool ImplicitNullChecks::canHandle(const MachineInstr *MI) {
  if (MI->isCall() || MI->mayRaiseFPException() ||
      MI->hasUnmodeledSideEffects())
    return false;
  auto IsRegMask = [](const MachineOperand &MO) { return MO.isRegMask(); };
  (void)IsRegMask;

  assert(llvm::none_of(MI->operands(), IsRegMask) &&
         "Calls were filtered out above!");

  auto IsUnordered = [](MachineMemOperand *MMO) { return MMO->isUnordered(); };
  return llvm::all_of(MI->memoperands(), IsUnordered);
}

ImplicitNullChecks::DependenceResult
ImplicitNullChecks::computeDependence(const MachineInstr *MI,
                                      ArrayRef<MachineInstr *> Block) {
  assert(llvm::all_of(Block, canHandle) && "Check this first!");
  assert(!is_contained(Block, MI) && "Block must be exclusive of MI!");

  std::optional<ArrayRef<MachineInstr *>::iterator> Dep;

  for (auto I = Block.begin(), E = Block.end(); I != E; ++I) {
    if (canReorder(*I, MI))
      continue;

    if (Dep == std::nullopt) {
      // Found one possible dependency, keep track of it.
      Dep = I;
    } else {
      // We found two dependencies, so bail out.
      return {false, std::nullopt};
    }
  }

  return {true, Dep};
}

bool ImplicitNullChecks::canReorder(const MachineInstr *A,
                                    const MachineInstr *B) {
  assert(canHandle(A) && canHandle(B) && "Precondition!");

  // canHandle makes sure that we _can_ correctly analyze the dependencies
  // between A and B here -- for instance, we should not be dealing with heap
  // load-store dependencies here.

  for (const auto &MOA : A->operands()) {
    if (!(MOA.isReg() && MOA.getReg()))
      continue;

    Register RegA = MOA.getReg();
    for (const auto &MOB : B->operands()) {
      if (!(MOB.isReg() && MOB.getReg()))
        continue;

      Register RegB = MOB.getReg();

      if (TRI->regsOverlap(RegA, RegB) && (MOA.isDef() || MOB.isDef()))
        return false;
    }
  }

  return true;
}

bool ImplicitNullChecks::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getRegInfo().getTargetRegisterInfo();
  MFI = &MF.getFrameInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  SmallVector<NullCheck, 16> NullCheckList;

  for (auto &MBB : MF)
    analyzeBlockForNullChecks(MBB, NullCheckList);

  if (!NullCheckList.empty())
    rewriteNullChecks(NullCheckList);

  return !NullCheckList.empty();
}

// Return true if any register aliasing \p Reg is live-in into \p MBB.
static bool AnyAliasLiveIn(const TargetRegisterInfo *TRI,
                           MachineBasicBlock *MBB, unsigned Reg) {
  for (MCRegAliasIterator AR(Reg, TRI, /*IncludeSelf*/ true); AR.isValid();
       ++AR)
    if (MBB->isLiveIn(*AR))
      return true;
  return false;
}

ImplicitNullChecks::AliasResult
ImplicitNullChecks::areMemoryOpsAliased(const MachineInstr &MI,
                                        const MachineInstr *PrevMI) const {
  // If it is not memory access, skip the check.
  if (!(PrevMI->mayStore() || PrevMI->mayLoad()))
    return AR_NoAlias;
  // Load-Load may alias
  if (!(MI.mayStore() || PrevMI->mayStore()))
    return AR_NoAlias;
  // We lost info, conservatively alias. If it was store then no sense to
  // continue because we won't be able to check against it further.
  if (MI.memoperands_empty())
    return MI.mayStore() ? AR_WillAliasEverything : AR_MayAlias;
  if (PrevMI->memoperands_empty())
    return PrevMI->mayStore() ? AR_WillAliasEverything : AR_MayAlias;

  for (MachineMemOperand *MMO1 : MI.memoperands()) {
    // MMO1 should have a value due it comes from operation we'd like to use
    // as implicit null check.
    assert(MMO1->getValue() && "MMO1 should have a Value!");
    for (MachineMemOperand *MMO2 : PrevMI->memoperands()) {
      if (const PseudoSourceValue *PSV = MMO2->getPseudoValue()) {
        if (PSV->mayAlias(MFI))
          return AR_MayAlias;
        continue;
      }
      if (!AA->isNoAlias(
              MemoryLocation::getAfter(MMO1->getValue(), MMO1->getAAInfo()),
              MemoryLocation::getAfter(MMO2->getValue(), MMO2->getAAInfo())))
        return AR_MayAlias;
    }
  }
  return AR_NoAlias;
}

ImplicitNullChecks::SuitabilityResult
ImplicitNullChecks::isSuitableMemoryOp(const MachineInstr &MI,
                                       unsigned PointerReg,
                                       ArrayRef<MachineInstr *> PrevInsts) {
  // Implementation restriction for faulting_op insertion
  // TODO: This could be relaxed if we find a test case which warrants it.
  if (MI.getDesc().getNumDefs() > 1)
   return SR_Unsuitable;

  if (!MI.mayLoadOrStore() || MI.isPredicable())
    return SR_Unsuitable;
  auto AM = TII->getAddrModeFromMemoryOp(MI, TRI);
  if (!AM || AM->Form != ExtAddrMode::Formula::Basic)
    return SR_Unsuitable;
  auto AddrMode = *AM;
  const Register BaseReg = AddrMode.BaseReg, ScaledReg = AddrMode.ScaledReg;
  int64_t Displacement = AddrMode.Displacement;

  // We need the base of the memory instruction to be same as the register
  // where the null check is performed (i.e. PointerReg).
  if (BaseReg != PointerReg && ScaledReg != PointerReg)
    return SR_Unsuitable;
  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  unsigned PointerRegSizeInBits = TRI->getRegSizeInBits(PointerReg, MRI);
  // Bail out of the sizes of BaseReg, ScaledReg and PointerReg are not the
  // same.
  if ((BaseReg &&
       TRI->getRegSizeInBits(BaseReg, MRI) != PointerRegSizeInBits) ||
      (ScaledReg &&
       TRI->getRegSizeInBits(ScaledReg, MRI) != PointerRegSizeInBits))
    return SR_Unsuitable;

  // Returns true if RegUsedInAddr is used for calculating the displacement
  // depending on addressing mode. Also calculates the Displacement.
  auto CalculateDisplacementFromAddrMode = [&](Register RegUsedInAddr,
                                               int64_t Multiplier) {
    // The register can be NoRegister, which is defined as zero for all targets.
    // Consider instruction of interest as `movq 8(,%rdi,8), %rax`. Here the
    // ScaledReg is %rdi, while there is no BaseReg.
    if (!RegUsedInAddr)
      return false;
    assert(Multiplier && "expected to be non-zero!");
    MachineInstr *ModifyingMI = nullptr;
    for (auto It = std::next(MachineBasicBlock::const_reverse_iterator(&MI));
         It != MI.getParent()->rend(); It++) {
      const MachineInstr *CurrMI = &*It;
      if (CurrMI->modifiesRegister(RegUsedInAddr, TRI)) {
        ModifyingMI = const_cast<MachineInstr *>(CurrMI);
        break;
      }
    }
    if (!ModifyingMI)
      return false;
    // Check for the const value defined in register by ModifyingMI. This means
    // all other previous values for that register has been invalidated.
    int64_t ImmVal;
    if (!TII->getConstValDefinedInReg(*ModifyingMI, RegUsedInAddr, ImmVal))
      return false;
    // Calculate the reg size in bits, since this is needed for bailing out in
    // case of overflow.
    int32_t RegSizeInBits = TRI->getRegSizeInBits(RegUsedInAddr, MRI);
    APInt ImmValC(RegSizeInBits, ImmVal, true /*IsSigned*/);
    APInt MultiplierC(RegSizeInBits, Multiplier);
    assert(MultiplierC.isStrictlyPositive() &&
           "expected to be a positive value!");
    bool IsOverflow;
    // Sign of the product depends on the sign of the ImmVal, since Multiplier
    // is always positive.
    APInt Product = ImmValC.smul_ov(MultiplierC, IsOverflow);
    if (IsOverflow)
      return false;
    APInt DisplacementC(64, Displacement, true /*isSigned*/);
    DisplacementC = Product.sadd_ov(DisplacementC, IsOverflow);
    if (IsOverflow)
      return false;

    // We only handle diplacements upto 64 bits wide.
    if (DisplacementC.getActiveBits() > 64)
      return false;
    Displacement = DisplacementC.getSExtValue();
    return true;
  };

  // If a register used in the address is constant, fold it's effect into the
  // displacement for ease of analysis.
  bool BaseRegIsConstVal = false, ScaledRegIsConstVal = false;
  if (CalculateDisplacementFromAddrMode(BaseReg, 1))
    BaseRegIsConstVal = true;
  if (CalculateDisplacementFromAddrMode(ScaledReg, AddrMode.Scale))
    ScaledRegIsConstVal = true;

  // The register which is not null checked should be part of the Displacement
  // calculation, otherwise we do not know whether the Displacement is made up
  // by some symbolic values.
  // This matters because we do not want to incorrectly assume that load from
  // falls in the zeroth faulting page in the "sane offset check" below.
  if ((BaseReg && BaseReg != PointerReg && !BaseRegIsConstVal) ||
      (ScaledReg && ScaledReg != PointerReg && !ScaledRegIsConstVal))
    return SR_Unsuitable;

  // We want the mem access to be issued at a sane offset from PointerReg,
  // so that if PointerReg is null then the access reliably page faults.
  if (!(-PageSize < Displacement && Displacement < PageSize))
    return SR_Unsuitable;

  // Finally, check whether the current memory access aliases with previous one.
  for (auto *PrevMI : PrevInsts) {
    AliasResult AR = areMemoryOpsAliased(MI, PrevMI);
    if (AR == AR_WillAliasEverything)
      return SR_Impossible;
    if (AR == AR_MayAlias)
      return SR_Unsuitable;
  }
  return SR_Suitable;
}

bool ImplicitNullChecks::canDependenceHoistingClobberLiveIns(
    MachineInstr *DependenceMI, MachineBasicBlock *NullSucc) {
  for (const auto &DependenceMO : DependenceMI->operands()) {
    if (!(DependenceMO.isReg() && DependenceMO.getReg()))
      continue;

    // Make sure that we won't clobber any live ins to the sibling block by
    // hoisting Dependency.  For instance, we can't hoist INST to before the
    // null check (even if it safe, and does not violate any dependencies in
    // the non_null_block) if %rdx is live in to _null_block.
    //
    //    test %rcx, %rcx
    //    je _null_block
    //  _non_null_block:
    //    %rdx = INST
    //    ...
    //
    // This restriction does not apply to the faulting load inst because in
    // case the pointer loaded from is in the null page, the load will not
    // semantically execute, and affect machine state.  That is, if the load
    // was loading into %rax and it faults, the value of %rax should stay the
    // same as it would have been had the load not have executed and we'd have
    // branched to NullSucc directly.
    if (AnyAliasLiveIn(TRI, NullSucc, DependenceMO.getReg()))
      return true;

  }

  // The dependence does not clobber live-ins in NullSucc block.
  return false;
}

bool ImplicitNullChecks::canHoistInst(MachineInstr *FaultingMI,
                                      ArrayRef<MachineInstr *> InstsSeenSoFar,
                                      MachineBasicBlock *NullSucc,
                                      MachineInstr *&Dependence) {
  auto DepResult = computeDependence(FaultingMI, InstsSeenSoFar);
  if (!DepResult.CanReorder)
    return false;

  if (!DepResult.PotentialDependence) {
    Dependence = nullptr;
    return true;
  }

  auto DependenceItr = *DepResult.PotentialDependence;
  auto *DependenceMI = *DependenceItr;

  // We don't want to reason about speculating loads.  Note -- at this point
  // we should have already filtered out all of the other non-speculatable
  // things, like calls and stores.
  // We also do not want to hoist stores because it might change the memory
  // while the FaultingMI may result in faulting.
  assert(canHandle(DependenceMI) && "Should never have reached here!");
  if (DependenceMI->mayLoadOrStore())
    return false;

  if (canDependenceHoistingClobberLiveIns(DependenceMI, NullSucc))
    return false;

  auto DepDepResult =
      computeDependence(DependenceMI, {InstsSeenSoFar.begin(), DependenceItr});

  if (!DepDepResult.CanReorder || DepDepResult.PotentialDependence)
    return false;

  Dependence = DependenceMI;
  return true;
}

/// Analyze MBB to check if its terminating branch can be turned into an
/// implicit null check.  If yes, append a description of the said null check to
/// NullCheckList and return true, else return false.
bool ImplicitNullChecks::analyzeBlockForNullChecks(
    MachineBasicBlock &MBB, SmallVectorImpl<NullCheck> &NullCheckList) {
  using MachineBranchPredicate = TargetInstrInfo::MachineBranchPredicate;

  MDNode *BranchMD = nullptr;
  if (auto *BB = MBB.getBasicBlock())
    BranchMD = BB->getTerminator()->getMetadata(LLVMContext::MD_make_implicit);

  if (!BranchMD)
    return false;

  MachineBranchPredicate MBP;

  if (TII->analyzeBranchPredicate(MBB, MBP, true))
    return false;

  // Is the predicate comparing an integer to zero?
  if (!(MBP.LHS.isReg() && MBP.RHS.isImm() && MBP.RHS.getImm() == 0 &&
        (MBP.Predicate == MachineBranchPredicate::PRED_NE ||
         MBP.Predicate == MachineBranchPredicate::PRED_EQ)))
    return false;

  // If there is a separate condition generation instruction, we chose not to
  // transform unless we can remove both condition and consuming branch.
  if (MBP.ConditionDef && !MBP.SingleUseCondition)
    return false;

  MachineBasicBlock *NotNullSucc, *NullSucc;

  if (MBP.Predicate == MachineBranchPredicate::PRED_NE) {
    NotNullSucc = MBP.TrueDest;
    NullSucc = MBP.FalseDest;
  } else {
    NotNullSucc = MBP.FalseDest;
    NullSucc = MBP.TrueDest;
  }

  // We handle the simplest case for now.  We can potentially do better by using
  // the machine dominator tree.
  if (NotNullSucc->pred_size() != 1)
    return false;

  const Register PointerReg = MBP.LHS.getReg();

  if (MBP.ConditionDef) {
    // To prevent the invalid transformation of the following code:
    //
    //   mov %rax, %rcx
    //   test %rax, %rax
    //   %rax = ...
    //   je throw_npe
    //   mov(%rcx), %r9
    //   mov(%rax), %r10
    //
    // into:
    //
    //   mov %rax, %rcx
    //   %rax = ....
    //   faulting_load_op("movl (%rax), %r10", throw_npe)
    //   mov(%rcx), %r9
    //
    // we must ensure that there are no instructions between the 'test' and
    // conditional jump that modify %rax.
    assert(MBP.ConditionDef->getParent() ==  &MBB &&
           "Should be in basic block");

    for (auto I = MBB.rbegin(); MBP.ConditionDef != &*I; ++I)
      if (I->modifiesRegister(PointerReg, TRI))
        return false;
  }
  // Starting with a code fragment like:
  //
  //   test %rax, %rax
  //   jne LblNotNull
  //
  //  LblNull:
  //   callq throw_NullPointerException
  //
  //  LblNotNull:
  //   Inst0
  //   Inst1
  //   ...
  //   Def = Load (%rax + <offset>)
  //   ...
  //
  //
  // we want to end up with
  //
  //   Def = FaultingLoad (%rax + <offset>), LblNull
  //   jmp LblNotNull ;; explicit or fallthrough
  //
  //  LblNotNull:
  //   Inst0
  //   Inst1
  //   ...
  //
  //  LblNull:
  //   callq throw_NullPointerException
  //
  //
  // To see why this is legal, consider the two possibilities:
  //
  //  1. %rax is null: since we constrain <offset> to be less than PageSize, the
  //     load instruction dereferences the null page, causing a segmentation
  //     fault.
  //
  //  2. %rax is not null: in this case we know that the load cannot fault, as
  //     otherwise the load would've faulted in the original program too and the
  //     original program would've been undefined.
  //
  // This reasoning cannot be extended to justify hoisting through arbitrary
  // control flow.  For instance, in the example below (in pseudo-C)
  //
  //    if (ptr == null) { throw_npe(); unreachable; }
  //    if (some_cond) { return 42; }
  //    v = ptr->field;  // LD
  //    ...
  //
  // we cannot (without code duplication) use the load marked "LD" to null check
  // ptr -- clause (2) above does not apply in this case.  In the above program
  // the safety of ptr->field can be dependent on some_cond; and, for instance,
  // ptr could be some non-null invalid reference that never gets loaded from
  // because some_cond is always true.

  SmallVector<MachineInstr *, 8> InstsSeenSoFar;

  for (auto &MI : *NotNullSucc) {
    if (!canHandle(&MI) || InstsSeenSoFar.size() >= MaxInstsToConsider)
      return false;

    MachineInstr *Dependence;
    SuitabilityResult SR = isSuitableMemoryOp(MI, PointerReg, InstsSeenSoFar);
    if (SR == SR_Impossible)
      return false;
    if (SR == SR_Suitable &&
        canHoistInst(&MI, InstsSeenSoFar, NullSucc, Dependence)) {
      NullCheckList.emplace_back(&MI, MBP.ConditionDef, &MBB, NotNullSucc,
                                 NullSucc, Dependence);
      return true;
    }

    // If MI re-defines the PointerReg in a way that changes the value of
    // PointerReg if it was null, then we cannot move further.
    if (!TII->preservesZeroValueInReg(&MI, PointerReg, TRI))
      return false;
    InstsSeenSoFar.push_back(&MI);
  }

  return false;
}

/// Wrap a machine instruction, MI, into a FAULTING machine instruction.
/// The FAULTING instruction does the same load/store as MI
/// (defining the same register), and branches to HandlerMBB if the mem access
/// faults.  The FAULTING instruction is inserted at the end of MBB.
MachineInstr *ImplicitNullChecks::insertFaultingInstr(
    MachineInstr *MI, MachineBasicBlock *MBB, MachineBasicBlock *HandlerMBB) {
  const unsigned NoRegister = 0; // Guaranteed to be the NoRegister value for
                                 // all targets.

  DebugLoc DL;
  unsigned NumDefs = MI->getDesc().getNumDefs();
  assert(NumDefs <= 1 && "other cases unhandled!");

  unsigned DefReg = NoRegister;
  if (NumDefs != 0) {
    DefReg = MI->getOperand(0).getReg();
    assert(NumDefs == 1 && "expected exactly one def!");
  }

  FaultMaps::FaultKind FK;
  if (MI->mayLoad())
    FK =
        MI->mayStore() ? FaultMaps::FaultingLoadStore : FaultMaps::FaultingLoad;
  else
    FK = FaultMaps::FaultingStore;

  auto MIB = BuildMI(MBB, DL, TII->get(TargetOpcode::FAULTING_OP), DefReg)
                 .addImm(FK)
                 .addMBB(HandlerMBB)
                 .addImm(MI->getOpcode());

  for (auto &MO : MI->uses()) {
    if (MO.isReg()) {
      MachineOperand NewMO = MO;
      if (MO.isUse()) {
        NewMO.setIsKill(false);
      } else {
        assert(MO.isDef() && "Expected def or use");
        NewMO.setIsDead(false);
      }
      MIB.add(NewMO);
    } else {
      MIB.add(MO);
    }
  }

  MIB.setMemRefs(MI->memoperands());

  return MIB;
}

/// Rewrite the null checks in NullCheckList into implicit null checks.
void ImplicitNullChecks::rewriteNullChecks(
    ArrayRef<ImplicitNullChecks::NullCheck> NullCheckList) {
  DebugLoc DL;

  for (const auto &NC : NullCheckList) {
    // Remove the conditional branch dependent on the null check.
    unsigned BranchesRemoved = TII->removeBranch(*NC.getCheckBlock());
    (void)BranchesRemoved;
    assert(BranchesRemoved > 0 && "expected at least one branch!");

    if (auto *DepMI = NC.getOnlyDependency()) {
      DepMI->removeFromParent();
      NC.getCheckBlock()->insert(NC.getCheckBlock()->end(), DepMI);
    }

    // Insert a faulting instruction where the conditional branch was
    // originally. We check earlier ensures that this bit of code motion
    // is legal.  We do not touch the successors list for any basic block
    // since we haven't changed control flow, we've just made it implicit.
    MachineInstr *FaultingInstr = insertFaultingInstr(
        NC.getMemOperation(), NC.getCheckBlock(), NC.getNullSucc());
    // Now the values defined by MemOperation, if any, are live-in of
    // the block of MemOperation.
    // The original operation may define implicit-defs alongside
    // the value.
    MachineBasicBlock *MBB = NC.getMemOperation()->getParent();
    for (const MachineOperand &MO : FaultingInstr->all_defs()) {
      Register Reg = MO.getReg();
      if (!Reg || MBB->isLiveIn(Reg))
        continue;
      MBB->addLiveIn(Reg);
    }

    if (auto *DepMI = NC.getOnlyDependency()) {
      for (auto &MO : DepMI->all_defs()) {
        if (!MO.getReg() || MO.isDead())
          continue;
        if (!NC.getNotNullSucc()->isLiveIn(MO.getReg()))
          NC.getNotNullSucc()->addLiveIn(MO.getReg());
      }
    }

    NC.getMemOperation()->eraseFromParent();
    if (auto *CheckOp = NC.getCheckOperation())
      CheckOp->eraseFromParent();

    // Insert an *unconditional* branch to not-null successor - we expect
    // block placement to remove fallthroughs later.
    TII->insertBranch(*NC.getCheckBlock(), NC.getNotNullSucc(), nullptr,
                      /*Cond=*/std::nullopt, DL);

    NumImplicitNullChecks++;
  }
}

char ImplicitNullChecks::ID = 0;

char &llvm::ImplicitNullChecksID = ImplicitNullChecks::ID;

INITIALIZE_PASS_BEGIN(ImplicitNullChecks, DEBUG_TYPE,
                      "Implicit null checks", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(ImplicitNullChecks, DEBUG_TYPE,
                    "Implicit null checks", false, false)
