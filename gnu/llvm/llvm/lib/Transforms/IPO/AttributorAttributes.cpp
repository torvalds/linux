//===- AttributorAttributes.cpp - Attributes for Attributor deduction -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See the Attributor.h file comment and the class descriptions in that file for
// more information.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/Attributor.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Assumptions.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CallPromotionUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <numeric>
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "attributor"

static cl::opt<bool> ManifestInternal(
    "attributor-manifest-internal", cl::Hidden,
    cl::desc("Manifest Attributor internal string attributes."),
    cl::init(false));

static cl::opt<int> MaxHeapToStackSize("max-heap-to-stack-size", cl::init(128),
                                       cl::Hidden);

template <>
unsigned llvm::PotentialConstantIntValuesState::MaxPotentialValues = 0;

template <> unsigned llvm::PotentialLLVMValuesState::MaxPotentialValues = -1;

static cl::opt<unsigned, true> MaxPotentialValues(
    "attributor-max-potential-values", cl::Hidden,
    cl::desc("Maximum number of potential values to be "
             "tracked for each position."),
    cl::location(llvm::PotentialConstantIntValuesState::MaxPotentialValues),
    cl::init(7));

static cl::opt<int> MaxPotentialValuesIterations(
    "attributor-max-potential-values-iterations", cl::Hidden,
    cl::desc(
        "Maximum number of iterations we keep dismantling potential values."),
    cl::init(64));

STATISTIC(NumAAs, "Number of abstract attributes created");

// Some helper macros to deal with statistics tracking.
//
// Usage:
// For simple IR attribute tracking overload trackStatistics in the abstract
// attribute and choose the right STATS_DECLTRACK_********* macro,
// e.g.,:
//  void trackStatistics() const override {
//    STATS_DECLTRACK_ARG_ATTR(returned)
//  }
// If there is a single "increment" side one can use the macro
// STATS_DECLTRACK with a custom message. If there are multiple increment
// sides, STATS_DECL and STATS_TRACK can also be used separately.
//
#define BUILD_STAT_MSG_IR_ATTR(TYPE, NAME)                                     \
  ("Number of " #TYPE " marked '" #NAME "'")
#define BUILD_STAT_NAME(NAME, TYPE) NumIR##TYPE##_##NAME
#define STATS_DECL_(NAME, MSG) STATISTIC(NAME, MSG);
#define STATS_DECL(NAME, TYPE, MSG)                                            \
  STATS_DECL_(BUILD_STAT_NAME(NAME, TYPE), MSG);
#define STATS_TRACK(NAME, TYPE) ++(BUILD_STAT_NAME(NAME, TYPE));
#define STATS_DECLTRACK(NAME, TYPE, MSG)                                       \
  {                                                                            \
    STATS_DECL(NAME, TYPE, MSG)                                                \
    STATS_TRACK(NAME, TYPE)                                                    \
  }
#define STATS_DECLTRACK_ARG_ATTR(NAME)                                         \
  STATS_DECLTRACK(NAME, Arguments, BUILD_STAT_MSG_IR_ATTR(arguments, NAME))
#define STATS_DECLTRACK_CSARG_ATTR(NAME)                                       \
  STATS_DECLTRACK(NAME, CSArguments,                                           \
                  BUILD_STAT_MSG_IR_ATTR(call site arguments, NAME))
#define STATS_DECLTRACK_FN_ATTR(NAME)                                          \
  STATS_DECLTRACK(NAME, Function, BUILD_STAT_MSG_IR_ATTR(functions, NAME))
#define STATS_DECLTRACK_CS_ATTR(NAME)                                          \
  STATS_DECLTRACK(NAME, CS, BUILD_STAT_MSG_IR_ATTR(call site, NAME))
#define STATS_DECLTRACK_FNRET_ATTR(NAME)                                       \
  STATS_DECLTRACK(NAME, FunctionReturn,                                        \
                  BUILD_STAT_MSG_IR_ATTR(function returns, NAME))
#define STATS_DECLTRACK_CSRET_ATTR(NAME)                                       \
  STATS_DECLTRACK(NAME, CSReturn,                                              \
                  BUILD_STAT_MSG_IR_ATTR(call site returns, NAME))
#define STATS_DECLTRACK_FLOATING_ATTR(NAME)                                    \
  STATS_DECLTRACK(NAME, Floating,                                              \
                  ("Number of floating values known to be '" #NAME "'"))

// Specialization of the operator<< for abstract attributes subclasses. This
// disambiguates situations where multiple operators are applicable.
namespace llvm {
#define PIPE_OPERATOR(CLASS)                                                   \
  raw_ostream &operator<<(raw_ostream &OS, const CLASS &AA) {                  \
    return OS << static_cast<const AbstractAttribute &>(AA);                   \
  }

PIPE_OPERATOR(AAIsDead)
PIPE_OPERATOR(AANoUnwind)
PIPE_OPERATOR(AANoSync)
PIPE_OPERATOR(AANoRecurse)
PIPE_OPERATOR(AANonConvergent)
PIPE_OPERATOR(AAWillReturn)
PIPE_OPERATOR(AANoReturn)
PIPE_OPERATOR(AANonNull)
PIPE_OPERATOR(AAMustProgress)
PIPE_OPERATOR(AANoAlias)
PIPE_OPERATOR(AADereferenceable)
PIPE_OPERATOR(AAAlign)
PIPE_OPERATOR(AAInstanceInfo)
PIPE_OPERATOR(AANoCapture)
PIPE_OPERATOR(AAValueSimplify)
PIPE_OPERATOR(AANoFree)
PIPE_OPERATOR(AAHeapToStack)
PIPE_OPERATOR(AAIntraFnReachability)
PIPE_OPERATOR(AAMemoryBehavior)
PIPE_OPERATOR(AAMemoryLocation)
PIPE_OPERATOR(AAValueConstantRange)
PIPE_OPERATOR(AAPrivatizablePtr)
PIPE_OPERATOR(AAUndefinedBehavior)
PIPE_OPERATOR(AAPotentialConstantValues)
PIPE_OPERATOR(AAPotentialValues)
PIPE_OPERATOR(AANoUndef)
PIPE_OPERATOR(AANoFPClass)
PIPE_OPERATOR(AACallEdges)
PIPE_OPERATOR(AAInterFnReachability)
PIPE_OPERATOR(AAPointerInfo)
PIPE_OPERATOR(AAAssumptionInfo)
PIPE_OPERATOR(AAUnderlyingObjects)
PIPE_OPERATOR(AAAddressSpace)
PIPE_OPERATOR(AAAllocationInfo)
PIPE_OPERATOR(AAIndirectCallInfo)
PIPE_OPERATOR(AAGlobalValueInfo)
PIPE_OPERATOR(AADenormalFPMath)

#undef PIPE_OPERATOR

template <>
ChangeStatus clampStateAndIndicateChange<DerefState>(DerefState &S,
                                                     const DerefState &R) {
  ChangeStatus CS0 =
      clampStateAndIndicateChange(S.DerefBytesState, R.DerefBytesState);
  ChangeStatus CS1 = clampStateAndIndicateChange(S.GlobalState, R.GlobalState);
  return CS0 | CS1;
}

} // namespace llvm

static bool mayBeInCycle(const CycleInfo *CI, const Instruction *I,
                         bool HeaderOnly, Cycle **CPtr = nullptr) {
  if (!CI)
    return true;
  auto *BB = I->getParent();
  auto *C = CI->getCycle(BB);
  if (!C)
    return false;
  if (CPtr)
    *CPtr = C;
  return !HeaderOnly || BB == C->getHeader();
}

/// Checks if a type could have padding bytes.
static bool isDenselyPacked(Type *Ty, const DataLayout &DL) {
  // There is no size information, so be conservative.
  if (!Ty->isSized())
    return false;

  // If the alloc size is not equal to the storage size, then there are padding
  // bytes. For x86_fp80 on x86-64, size: 80 alloc size: 128.
  if (DL.getTypeSizeInBits(Ty) != DL.getTypeAllocSizeInBits(Ty))
    return false;

  // FIXME: This isn't the right way to check for padding in vectors with
  // non-byte-size elements.
  if (VectorType *SeqTy = dyn_cast<VectorType>(Ty))
    return isDenselyPacked(SeqTy->getElementType(), DL);

  // For array types, check for padding within members.
  if (ArrayType *SeqTy = dyn_cast<ArrayType>(Ty))
    return isDenselyPacked(SeqTy->getElementType(), DL);

  if (!isa<StructType>(Ty))
    return true;

  // Check for padding within and between elements of a struct.
  StructType *StructTy = cast<StructType>(Ty);
  const StructLayout *Layout = DL.getStructLayout(StructTy);
  uint64_t StartPos = 0;
  for (unsigned I = 0, E = StructTy->getNumElements(); I < E; ++I) {
    Type *ElTy = StructTy->getElementType(I);
    if (!isDenselyPacked(ElTy, DL))
      return false;
    if (StartPos != Layout->getElementOffsetInBits(I))
      return false;
    StartPos += DL.getTypeAllocSizeInBits(ElTy);
  }

  return true;
}

/// Get pointer operand of memory accessing instruction. If \p I is
/// not a memory accessing instruction, return nullptr. If \p AllowVolatile,
/// is set to false and the instruction is volatile, return nullptr.
static const Value *getPointerOperand(const Instruction *I,
                                      bool AllowVolatile) {
  if (!AllowVolatile && I->isVolatile())
    return nullptr;

  if (auto *LI = dyn_cast<LoadInst>(I)) {
    return LI->getPointerOperand();
  }

  if (auto *SI = dyn_cast<StoreInst>(I)) {
    return SI->getPointerOperand();
  }

  if (auto *CXI = dyn_cast<AtomicCmpXchgInst>(I)) {
    return CXI->getPointerOperand();
  }

  if (auto *RMWI = dyn_cast<AtomicRMWInst>(I)) {
    return RMWI->getPointerOperand();
  }

  return nullptr;
}

/// Helper function to create a pointer based on \p Ptr, and advanced by \p
/// Offset bytes.
static Value *constructPointer(Value *Ptr, int64_t Offset,
                               IRBuilder<NoFolder> &IRB) {
  LLVM_DEBUG(dbgs() << "Construct pointer: " << *Ptr << " + " << Offset
                    << "-bytes\n");

  if (Offset)
    Ptr = IRB.CreatePtrAdd(Ptr, IRB.getInt64(Offset),
                           Ptr->getName() + ".b" + Twine(Offset));
  return Ptr;
}

static const Value *
stripAndAccumulateOffsets(Attributor &A, const AbstractAttribute &QueryingAA,
                          const Value *Val, const DataLayout &DL, APInt &Offset,
                          bool GetMinOffset, bool AllowNonInbounds,
                          bool UseAssumed = false) {

  auto AttributorAnalysis = [&](Value &V, APInt &ROffset) -> bool {
    const IRPosition &Pos = IRPosition::value(V);
    // Only track dependence if we are going to use the assumed info.
    const AAValueConstantRange *ValueConstantRangeAA =
        A.getAAFor<AAValueConstantRange>(QueryingAA, Pos,
                                         UseAssumed ? DepClassTy::OPTIONAL
                                                    : DepClassTy::NONE);
    if (!ValueConstantRangeAA)
      return false;
    ConstantRange Range = UseAssumed ? ValueConstantRangeAA->getAssumed()
                                     : ValueConstantRangeAA->getKnown();
    if (Range.isFullSet())
      return false;

    // We can only use the lower part of the range because the upper part can
    // be higher than what the value can really be.
    if (GetMinOffset)
      ROffset = Range.getSignedMin();
    else
      ROffset = Range.getSignedMax();
    return true;
  };

  return Val->stripAndAccumulateConstantOffsets(DL, Offset, AllowNonInbounds,
                                                /* AllowInvariant */ true,
                                                AttributorAnalysis);
}

static const Value *
getMinimalBaseOfPointer(Attributor &A, const AbstractAttribute &QueryingAA,
                        const Value *Ptr, int64_t &BytesOffset,
                        const DataLayout &DL, bool AllowNonInbounds = false) {
  APInt OffsetAPInt(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
  const Value *Base =
      stripAndAccumulateOffsets(A, QueryingAA, Ptr, DL, OffsetAPInt,
                                /* GetMinOffset */ true, AllowNonInbounds);

  BytesOffset = OffsetAPInt.getSExtValue();
  return Base;
}

/// Clamp the information known for all returned values of a function
/// (identified by \p QueryingAA) into \p S.
template <typename AAType, typename StateType = typename AAType::StateType,
          Attribute::AttrKind IRAttributeKind = AAType::IRAttributeKind,
          bool RecurseForSelectAndPHI = true>
static void clampReturnedValueStates(
    Attributor &A, const AAType &QueryingAA, StateType &S,
    const IRPosition::CallBaseContext *CBContext = nullptr) {
  LLVM_DEBUG(dbgs() << "[Attributor] Clamp return value states for "
                    << QueryingAA << " into " << S << "\n");

  assert((QueryingAA.getIRPosition().getPositionKind() ==
              IRPosition::IRP_RETURNED ||
          QueryingAA.getIRPosition().getPositionKind() ==
              IRPosition::IRP_CALL_SITE_RETURNED) &&
         "Can only clamp returned value states for a function returned or call "
         "site returned position!");

  // Use an optional state as there might not be any return values and we want
  // to join (IntegerState::operator&) the state of all there are.
  std::optional<StateType> T;

  // Callback for each possibly returned value.
  auto CheckReturnValue = [&](Value &RV) -> bool {
    const IRPosition &RVPos = IRPosition::value(RV, CBContext);
    // If possible, use the hasAssumedIRAttr interface.
    if (Attribute::isEnumAttrKind(IRAttributeKind)) {
      bool IsKnown;
      return AA::hasAssumedIRAttr<IRAttributeKind>(
          A, &QueryingAA, RVPos, DepClassTy::REQUIRED, IsKnown);
    }

    const AAType *AA =
        A.getAAFor<AAType>(QueryingAA, RVPos, DepClassTy::REQUIRED);
    if (!AA)
      return false;
    LLVM_DEBUG(dbgs() << "[Attributor] RV: " << RV
                      << " AA: " << AA->getAsStr(&A) << " @ " << RVPos << "\n");
    const StateType &AAS = AA->getState();
    if (!T)
      T = StateType::getBestState(AAS);
    *T &= AAS;
    LLVM_DEBUG(dbgs() << "[Attributor] AA State: " << AAS << " RV State: " << T
                      << "\n");
    return T->isValidState();
  };

  if (!A.checkForAllReturnedValues(CheckReturnValue, QueryingAA,
                                   AA::ValueScope::Intraprocedural,
                                   RecurseForSelectAndPHI))
    S.indicatePessimisticFixpoint();
  else if (T)
    S ^= *T;
}

namespace {
/// Helper class for generic deduction: return value -> returned position.
template <typename AAType, typename BaseType,
          typename StateType = typename BaseType::StateType,
          bool PropagateCallBaseContext = false,
          Attribute::AttrKind IRAttributeKind = AAType::IRAttributeKind,
          bool RecurseForSelectAndPHI = true>
struct AAReturnedFromReturnedValues : public BaseType {
  AAReturnedFromReturnedValues(const IRPosition &IRP, Attributor &A)
      : BaseType(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    StateType S(StateType::getBestState(this->getState()));
    clampReturnedValueStates<AAType, StateType, IRAttributeKind,
                             RecurseForSelectAndPHI>(
        A, *this, S,
        PropagateCallBaseContext ? this->getCallBaseContext() : nullptr);
    // TODO: If we know we visited all returned values, thus no are assumed
    // dead, we can take the known information from the state T.
    return clampStateAndIndicateChange<StateType>(this->getState(), S);
  }
};

/// Clamp the information known at all call sites for a given argument
/// (identified by \p QueryingAA) into \p S.
template <typename AAType, typename StateType = typename AAType::StateType,
          Attribute::AttrKind IRAttributeKind = AAType::IRAttributeKind>
static void clampCallSiteArgumentStates(Attributor &A, const AAType &QueryingAA,
                                        StateType &S) {
  LLVM_DEBUG(dbgs() << "[Attributor] Clamp call site argument states for "
                    << QueryingAA << " into " << S << "\n");

  assert(QueryingAA.getIRPosition().getPositionKind() ==
             IRPosition::IRP_ARGUMENT &&
         "Can only clamp call site argument states for an argument position!");

  // Use an optional state as there might not be any return values and we want
  // to join (IntegerState::operator&) the state of all there are.
  std::optional<StateType> T;

  // The argument number which is also the call site argument number.
  unsigned ArgNo = QueryingAA.getIRPosition().getCallSiteArgNo();

  auto CallSiteCheck = [&](AbstractCallSite ACS) {
    const IRPosition &ACSArgPos = IRPosition::callsite_argument(ACS, ArgNo);
    // Check if a coresponding argument was found or if it is on not associated
    // (which can happen for callback calls).
    if (ACSArgPos.getPositionKind() == IRPosition::IRP_INVALID)
      return false;

    // If possible, use the hasAssumedIRAttr interface.
    if (Attribute::isEnumAttrKind(IRAttributeKind)) {
      bool IsKnown;
      return AA::hasAssumedIRAttr<IRAttributeKind>(
          A, &QueryingAA, ACSArgPos, DepClassTy::REQUIRED, IsKnown);
    }

    const AAType *AA =
        A.getAAFor<AAType>(QueryingAA, ACSArgPos, DepClassTy::REQUIRED);
    if (!AA)
      return false;
    LLVM_DEBUG(dbgs() << "[Attributor] ACS: " << *ACS.getInstruction()
                      << " AA: " << AA->getAsStr(&A) << " @" << ACSArgPos
                      << "\n");
    const StateType &AAS = AA->getState();
    if (!T)
      T = StateType::getBestState(AAS);
    *T &= AAS;
    LLVM_DEBUG(dbgs() << "[Attributor] AA State: " << AAS << " CSA State: " << T
                      << "\n");
    return T->isValidState();
  };

  bool UsedAssumedInformation = false;
  if (!A.checkForAllCallSites(CallSiteCheck, QueryingAA, true,
                              UsedAssumedInformation))
    S.indicatePessimisticFixpoint();
  else if (T)
    S ^= *T;
}

/// This function is the bridge between argument position and the call base
/// context.
template <typename AAType, typename BaseType,
          typename StateType = typename AAType::StateType,
          Attribute::AttrKind IRAttributeKind = AAType::IRAttributeKind>
bool getArgumentStateFromCallBaseContext(Attributor &A,
                                         BaseType &QueryingAttribute,
                                         IRPosition &Pos, StateType &State) {
  assert((Pos.getPositionKind() == IRPosition::IRP_ARGUMENT) &&
         "Expected an 'argument' position !");
  const CallBase *CBContext = Pos.getCallBaseContext();
  if (!CBContext)
    return false;

  int ArgNo = Pos.getCallSiteArgNo();
  assert(ArgNo >= 0 && "Invalid Arg No!");
  const IRPosition CBArgPos = IRPosition::callsite_argument(*CBContext, ArgNo);

  // If possible, use the hasAssumedIRAttr interface.
  if (Attribute::isEnumAttrKind(IRAttributeKind)) {
    bool IsKnown;
    return AA::hasAssumedIRAttr<IRAttributeKind>(
        A, &QueryingAttribute, CBArgPos, DepClassTy::REQUIRED, IsKnown);
  }

  const auto *AA =
      A.getAAFor<AAType>(QueryingAttribute, CBArgPos, DepClassTy::REQUIRED);
  if (!AA)
    return false;
  const StateType &CBArgumentState =
      static_cast<const StateType &>(AA->getState());

  LLVM_DEBUG(dbgs() << "[Attributor] Briding Call site context to argument"
                    << "Position:" << Pos << "CB Arg state:" << CBArgumentState
                    << "\n");

  // NOTE: If we want to do call site grouping it should happen here.
  State ^= CBArgumentState;
  return true;
}

/// Helper class for generic deduction: call site argument -> argument position.
template <typename AAType, typename BaseType,
          typename StateType = typename AAType::StateType,
          bool BridgeCallBaseContext = false,
          Attribute::AttrKind IRAttributeKind = AAType::IRAttributeKind>
struct AAArgumentFromCallSiteArguments : public BaseType {
  AAArgumentFromCallSiteArguments(const IRPosition &IRP, Attributor &A)
      : BaseType(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    StateType S = StateType::getBestState(this->getState());

    if (BridgeCallBaseContext) {
      bool Success =
          getArgumentStateFromCallBaseContext<AAType, BaseType, StateType,
                                              IRAttributeKind>(
              A, *this, this->getIRPosition(), S);
      if (Success)
        return clampStateAndIndicateChange<StateType>(this->getState(), S);
    }
    clampCallSiteArgumentStates<AAType, StateType, IRAttributeKind>(A, *this,
                                                                    S);

    // TODO: If we know we visited all incoming values, thus no are assumed
    // dead, we can take the known information from the state T.
    return clampStateAndIndicateChange<StateType>(this->getState(), S);
  }
};

/// Helper class for generic replication: function returned -> cs returned.
template <typename AAType, typename BaseType,
          typename StateType = typename BaseType::StateType,
          bool IntroduceCallBaseContext = false,
          Attribute::AttrKind IRAttributeKind = AAType::IRAttributeKind>
struct AACalleeToCallSite : public BaseType {
  AACalleeToCallSite(const IRPosition &IRP, Attributor &A) : BaseType(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto IRPKind = this->getIRPosition().getPositionKind();
    assert((IRPKind == IRPosition::IRP_CALL_SITE_RETURNED ||
            IRPKind == IRPosition::IRP_CALL_SITE) &&
           "Can only wrap function returned positions for call site "
           "returned positions!");
    auto &S = this->getState();

    CallBase &CB = cast<CallBase>(this->getAnchorValue());
    if (IntroduceCallBaseContext)
      LLVM_DEBUG(dbgs() << "[Attributor] Introducing call base context:" << CB
                        << "\n");

    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    auto CalleePred = [&](ArrayRef<const Function *> Callees) {
      for (const Function *Callee : Callees) {
        IRPosition FnPos =
            IRPKind == llvm::IRPosition::IRP_CALL_SITE_RETURNED
                ? IRPosition::returned(*Callee,
                                       IntroduceCallBaseContext ? &CB : nullptr)
                : IRPosition::function(
                      *Callee, IntroduceCallBaseContext ? &CB : nullptr);
        // If possible, use the hasAssumedIRAttr interface.
        if (Attribute::isEnumAttrKind(IRAttributeKind)) {
          bool IsKnown;
          if (!AA::hasAssumedIRAttr<IRAttributeKind>(
                  A, this, FnPos, DepClassTy::REQUIRED, IsKnown))
            return false;
          continue;
        }

        const AAType *AA =
            A.getAAFor<AAType>(*this, FnPos, DepClassTy::REQUIRED);
        if (!AA)
          return false;
        Changed |= clampStateAndIndicateChange(S, AA->getState());
        if (S.isAtFixpoint())
          return S.isValidState();
      }
      return true;
    };
    if (!A.checkForAllCallees(CalleePred, *this, CB))
      return S.indicatePessimisticFixpoint();
    return Changed;
  }
};

/// Helper function to accumulate uses.
template <class AAType, typename StateType = typename AAType::StateType>
static void followUsesInContext(AAType &AA, Attributor &A,
                                MustBeExecutedContextExplorer &Explorer,
                                const Instruction *CtxI,
                                SetVector<const Use *> &Uses,
                                StateType &State) {
  auto EIt = Explorer.begin(CtxI), EEnd = Explorer.end(CtxI);
  for (unsigned u = 0; u < Uses.size(); ++u) {
    const Use *U = Uses[u];
    if (const Instruction *UserI = dyn_cast<Instruction>(U->getUser())) {
      bool Found = Explorer.findInContextOf(UserI, EIt, EEnd);
      if (Found && AA.followUseInMBEC(A, U, UserI, State))
        for (const Use &Us : UserI->uses())
          Uses.insert(&Us);
    }
  }
}

/// Use the must-be-executed-context around \p I to add information into \p S.
/// The AAType class is required to have `followUseInMBEC` method with the
/// following signature and behaviour:
///
/// bool followUseInMBEC(Attributor &A, const Use *U, const Instruction *I)
/// U - Underlying use.
/// I - The user of the \p U.
/// Returns true if the value should be tracked transitively.
///
template <class AAType, typename StateType = typename AAType::StateType>
static void followUsesInMBEC(AAType &AA, Attributor &A, StateType &S,
                             Instruction &CtxI) {
  MustBeExecutedContextExplorer *Explorer =
      A.getInfoCache().getMustBeExecutedContextExplorer();
  if (!Explorer)
    return;

  // Container for (transitive) uses of the associated value.
  SetVector<const Use *> Uses;
  for (const Use &U : AA.getIRPosition().getAssociatedValue().uses())
    Uses.insert(&U);

  followUsesInContext<AAType>(AA, A, *Explorer, &CtxI, Uses, S);

  if (S.isAtFixpoint())
    return;

  SmallVector<const BranchInst *, 4> BrInsts;
  auto Pred = [&](const Instruction *I) {
    if (const BranchInst *Br = dyn_cast<BranchInst>(I))
      if (Br->isConditional())
        BrInsts.push_back(Br);
    return true;
  };

  // Here, accumulate conditional branch instructions in the context. We
  // explore the child paths and collect the known states. The disjunction of
  // those states can be merged to its own state. Let ParentState_i be a state
  // to indicate the known information for an i-th branch instruction in the
  // context. ChildStates are created for its successors respectively.
  //
  // ParentS_1 = ChildS_{1, 1} /\ ChildS_{1, 2} /\ ... /\ ChildS_{1, n_1}
  // ParentS_2 = ChildS_{2, 1} /\ ChildS_{2, 2} /\ ... /\ ChildS_{2, n_2}
  //      ...
  // ParentS_m = ChildS_{m, 1} /\ ChildS_{m, 2} /\ ... /\ ChildS_{m, n_m}
  //
  // Known State |= ParentS_1 \/ ParentS_2 \/... \/ ParentS_m
  //
  // FIXME: Currently, recursive branches are not handled. For example, we
  // can't deduce that ptr must be dereferenced in below function.
  //
  // void f(int a, int c, int *ptr) {
  //    if(a)
  //      if (b) {
  //        *ptr = 0;
  //      } else {
  //        *ptr = 1;
  //      }
  //    else {
  //      if (b) {
  //        *ptr = 0;
  //      } else {
  //        *ptr = 1;
  //      }
  //    }
  // }

  Explorer->checkForAllContext(&CtxI, Pred);
  for (const BranchInst *Br : BrInsts) {
    StateType ParentState;

    // The known state of the parent state is a conjunction of children's
    // known states so it is initialized with a best state.
    ParentState.indicateOptimisticFixpoint();

    for (const BasicBlock *BB : Br->successors()) {
      StateType ChildState;

      size_t BeforeSize = Uses.size();
      followUsesInContext(AA, A, *Explorer, &BB->front(), Uses, ChildState);

      // Erase uses which only appear in the child.
      for (auto It = Uses.begin() + BeforeSize; It != Uses.end();)
        It = Uses.erase(It);

      ParentState &= ChildState;
    }

    // Use only known state.
    S += ParentState;
  }
}
} // namespace

/// ------------------------ PointerInfo ---------------------------------------

namespace llvm {
namespace AA {
namespace PointerInfo {

struct State;

} // namespace PointerInfo
} // namespace AA

/// Helper for AA::PointerInfo::Access DenseMap/Set usage.
template <>
struct DenseMapInfo<AAPointerInfo::Access> : DenseMapInfo<Instruction *> {
  using Access = AAPointerInfo::Access;
  static inline Access getEmptyKey();
  static inline Access getTombstoneKey();
  static unsigned getHashValue(const Access &A);
  static bool isEqual(const Access &LHS, const Access &RHS);
};

/// Helper that allows RangeTy as a key in a DenseMap.
template <> struct DenseMapInfo<AA::RangeTy> {
  static inline AA::RangeTy getEmptyKey() {
    auto EmptyKey = DenseMapInfo<int64_t>::getEmptyKey();
    return AA::RangeTy{EmptyKey, EmptyKey};
  }

  static inline AA::RangeTy getTombstoneKey() {
    auto TombstoneKey = DenseMapInfo<int64_t>::getTombstoneKey();
    return AA::RangeTy{TombstoneKey, TombstoneKey};
  }

  static unsigned getHashValue(const AA::RangeTy &Range) {
    return detail::combineHashValue(
        DenseMapInfo<int64_t>::getHashValue(Range.Offset),
        DenseMapInfo<int64_t>::getHashValue(Range.Size));
  }

  static bool isEqual(const AA::RangeTy &A, const AA::RangeTy B) {
    return A == B;
  }
};

/// Helper for AA::PointerInfo::Access DenseMap/Set usage ignoring everythign
/// but the instruction
struct AccessAsInstructionInfo : DenseMapInfo<Instruction *> {
  using Base = DenseMapInfo<Instruction *>;
  using Access = AAPointerInfo::Access;
  static inline Access getEmptyKey();
  static inline Access getTombstoneKey();
  static unsigned getHashValue(const Access &A);
  static bool isEqual(const Access &LHS, const Access &RHS);
};

} // namespace llvm

/// A type to track pointer/struct usage and accesses for AAPointerInfo.
struct AA::PointerInfo::State : public AbstractState {
  /// Return the best possible representable state.
  static State getBestState(const State &SIS) { return State(); }

  /// Return the worst possible representable state.
  static State getWorstState(const State &SIS) {
    State R;
    R.indicatePessimisticFixpoint();
    return R;
  }

  State() = default;
  State(State &&SIS) = default;

  const State &getAssumed() const { return *this; }

  /// See AbstractState::isValidState().
  bool isValidState() const override { return BS.isValidState(); }

  /// See AbstractState::isAtFixpoint().
  bool isAtFixpoint() const override { return BS.isAtFixpoint(); }

  /// See AbstractState::indicateOptimisticFixpoint().
  ChangeStatus indicateOptimisticFixpoint() override {
    BS.indicateOptimisticFixpoint();
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractState::indicatePessimisticFixpoint().
  ChangeStatus indicatePessimisticFixpoint() override {
    BS.indicatePessimisticFixpoint();
    return ChangeStatus::CHANGED;
  }

  State &operator=(const State &R) {
    if (this == &R)
      return *this;
    BS = R.BS;
    AccessList = R.AccessList;
    OffsetBins = R.OffsetBins;
    RemoteIMap = R.RemoteIMap;
    return *this;
  }

  State &operator=(State &&R) {
    if (this == &R)
      return *this;
    std::swap(BS, R.BS);
    std::swap(AccessList, R.AccessList);
    std::swap(OffsetBins, R.OffsetBins);
    std::swap(RemoteIMap, R.RemoteIMap);
    return *this;
  }

  /// Add a new Access to the state at offset \p Offset and with size \p Size.
  /// The access is associated with \p I, writes \p Content (if anything), and
  /// is of kind \p Kind. If an Access already exists for the same \p I and same
  /// \p RemoteI, the two are combined, potentially losing information about
  /// offset and size. The resulting access must now be moved from its original
  /// OffsetBin to the bin for its new offset.
  ///
  /// \Returns CHANGED, if the state changed, UNCHANGED otherwise.
  ChangeStatus addAccess(Attributor &A, const AAPointerInfo::RangeList &Ranges,
                         Instruction &I, std::optional<Value *> Content,
                         AAPointerInfo::AccessKind Kind, Type *Ty,
                         Instruction *RemoteI = nullptr);

  AAPointerInfo::const_bin_iterator begin() const { return OffsetBins.begin(); }
  AAPointerInfo::const_bin_iterator end() const { return OffsetBins.end(); }
  int64_t numOffsetBins() const { return OffsetBins.size(); }

  const AAPointerInfo::Access &getAccess(unsigned Index) const {
    return AccessList[Index];
  }

protected:
  // Every memory instruction results in an Access object. We maintain a list of
  // all Access objects that we own, along with the following maps:
  //
  // - OffsetBins: RangeTy -> { Access }
  // - RemoteIMap: RemoteI x LocalI -> Access
  //
  // A RemoteI is any instruction that accesses memory. RemoteI is different
  // from LocalI if and only if LocalI is a call; then RemoteI is some
  // instruction in the callgraph starting from LocalI. Multiple paths in the
  // callgraph from LocalI to RemoteI may produce multiple accesses, but these
  // are all combined into a single Access object. This may result in loss of
  // information in RangeTy in the Access object.
  SmallVector<AAPointerInfo::Access> AccessList;
  AAPointerInfo::OffsetBinsTy OffsetBins;
  DenseMap<const Instruction *, SmallVector<unsigned>> RemoteIMap;

  /// See AAPointerInfo::forallInterferingAccesses.
  bool forallInterferingAccesses(
      AA::RangeTy Range,
      function_ref<bool(const AAPointerInfo::Access &, bool)> CB) const {
    if (!isValidState())
      return false;

    for (const auto &It : OffsetBins) {
      AA::RangeTy ItRange = It.getFirst();
      if (!Range.mayOverlap(ItRange))
        continue;
      bool IsExact = Range == ItRange && !Range.offsetOrSizeAreUnknown();
      for (auto Index : It.getSecond()) {
        auto &Access = AccessList[Index];
        if (!CB(Access, IsExact))
          return false;
      }
    }
    return true;
  }

  /// See AAPointerInfo::forallInterferingAccesses.
  bool forallInterferingAccesses(
      Instruction &I,
      function_ref<bool(const AAPointerInfo::Access &, bool)> CB,
      AA::RangeTy &Range) const {
    if (!isValidState())
      return false;

    auto LocalList = RemoteIMap.find(&I);
    if (LocalList == RemoteIMap.end()) {
      return true;
    }

    for (unsigned Index : LocalList->getSecond()) {
      for (auto &R : AccessList[Index]) {
        Range &= R;
        if (Range.offsetAndSizeAreUnknown())
          break;
      }
    }
    return forallInterferingAccesses(Range, CB);
  }

private:
  /// State to track fixpoint and validity.
  BooleanState BS;
};

ChangeStatus AA::PointerInfo::State::addAccess(
    Attributor &A, const AAPointerInfo::RangeList &Ranges, Instruction &I,
    std::optional<Value *> Content, AAPointerInfo::AccessKind Kind, Type *Ty,
    Instruction *RemoteI) {
  RemoteI = RemoteI ? RemoteI : &I;

  // Check if we have an access for this instruction, if not, simply add it.
  auto &LocalList = RemoteIMap[RemoteI];
  bool AccExists = false;
  unsigned AccIndex = AccessList.size();
  for (auto Index : LocalList) {
    auto &A = AccessList[Index];
    if (A.getLocalInst() == &I) {
      AccExists = true;
      AccIndex = Index;
      break;
    }
  }

  auto AddToBins = [&](const AAPointerInfo::RangeList &ToAdd) {
    LLVM_DEBUG(if (ToAdd.size()) dbgs()
                   << "[AAPointerInfo] Inserting access in new offset bins\n";);

    for (auto Key : ToAdd) {
      LLVM_DEBUG(dbgs() << "    key " << Key << "\n");
      OffsetBins[Key].insert(AccIndex);
    }
  };

  if (!AccExists) {
    AccessList.emplace_back(&I, RemoteI, Ranges, Content, Kind, Ty);
    assert((AccessList.size() == AccIndex + 1) &&
           "New Access should have been at AccIndex");
    LocalList.push_back(AccIndex);
    AddToBins(AccessList[AccIndex].getRanges());
    return ChangeStatus::CHANGED;
  }

  // Combine the new Access with the existing Access, and then update the
  // mapping in the offset bins.
  AAPointerInfo::Access Acc(&I, RemoteI, Ranges, Content, Kind, Ty);
  auto &Current = AccessList[AccIndex];
  auto Before = Current;
  Current &= Acc;
  if (Current == Before)
    return ChangeStatus::UNCHANGED;

  auto &ExistingRanges = Before.getRanges();
  auto &NewRanges = Current.getRanges();

  // Ranges that are in the old access but not the new access need to be removed
  // from the offset bins.
  AAPointerInfo::RangeList ToRemove;
  AAPointerInfo::RangeList::set_difference(ExistingRanges, NewRanges, ToRemove);
  LLVM_DEBUG(if (ToRemove.size()) dbgs()
                 << "[AAPointerInfo] Removing access from old offset bins\n";);

  for (auto Key : ToRemove) {
    LLVM_DEBUG(dbgs() << "    key " << Key << "\n");
    assert(OffsetBins.count(Key) && "Existing Access must be in some bin.");
    auto &Bin = OffsetBins[Key];
    assert(Bin.count(AccIndex) &&
           "Expected bin to actually contain the Access.");
    Bin.erase(AccIndex);
  }

  // Ranges that are in the new access but not the old access need to be added
  // to the offset bins.
  AAPointerInfo::RangeList ToAdd;
  AAPointerInfo::RangeList::set_difference(NewRanges, ExistingRanges, ToAdd);
  AddToBins(ToAdd);
  return ChangeStatus::CHANGED;
}

namespace {

/// A helper containing a list of offsets computed for a Use. Ideally this
/// list should be strictly ascending, but we ensure that only when we
/// actually translate the list of offsets to a RangeList.
struct OffsetInfo {
  using VecTy = SmallVector<int64_t>;
  using const_iterator = VecTy::const_iterator;
  VecTy Offsets;

  const_iterator begin() const { return Offsets.begin(); }
  const_iterator end() const { return Offsets.end(); }

  bool operator==(const OffsetInfo &RHS) const {
    return Offsets == RHS.Offsets;
  }

  bool operator!=(const OffsetInfo &RHS) const { return !(*this == RHS); }

  void insert(int64_t Offset) { Offsets.push_back(Offset); }
  bool isUnassigned() const { return Offsets.size() == 0; }

  bool isUnknown() const {
    if (isUnassigned())
      return false;
    if (Offsets.size() == 1)
      return Offsets.front() == AA::RangeTy::Unknown;
    return false;
  }

  void setUnknown() {
    Offsets.clear();
    Offsets.push_back(AA::RangeTy::Unknown);
  }

  void addToAll(int64_t Inc) {
    for (auto &Offset : Offsets) {
      Offset += Inc;
    }
  }

  /// Copy offsets from \p R into the current list.
  ///
  /// Ideally all lists should be strictly ascending, but we defer that to the
  /// actual use of the list. So we just blindly append here.
  void merge(const OffsetInfo &R) { Offsets.append(R.Offsets); }
};

#ifndef NDEBUG
static raw_ostream &operator<<(raw_ostream &OS, const OffsetInfo &OI) {
  ListSeparator LS;
  OS << "[";
  for (auto Offset : OI) {
    OS << LS << Offset;
  }
  OS << "]";
  return OS;
}
#endif // NDEBUG

struct AAPointerInfoImpl
    : public StateWrapper<AA::PointerInfo::State, AAPointerInfo> {
  using BaseTy = StateWrapper<AA::PointerInfo::State, AAPointerInfo>;
  AAPointerInfoImpl(const IRPosition &IRP, Attributor &A) : BaseTy(IRP) {}

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return std::string("PointerInfo ") +
           (isValidState() ? (std::string("#") +
                              std::to_string(OffsetBins.size()) + " bins")
                           : "<invalid>");
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    return AAPointerInfo::manifest(A);
  }

  virtual const_bin_iterator begin() const override { return State::begin(); }
  virtual const_bin_iterator end() const override { return State::end(); }
  virtual int64_t numOffsetBins() const override {
    return State::numOffsetBins();
  }

  bool forallInterferingAccesses(
      AA::RangeTy Range,
      function_ref<bool(const AAPointerInfo::Access &, bool)> CB)
      const override {
    return State::forallInterferingAccesses(Range, CB);
  }

  bool forallInterferingAccesses(
      Attributor &A, const AbstractAttribute &QueryingAA, Instruction &I,
      bool FindInterferingWrites, bool FindInterferingReads,
      function_ref<bool(const Access &, bool)> UserCB, bool &HasBeenWrittenTo,
      AA::RangeTy &Range,
      function_ref<bool(const Access &)> SkipCB) const override {
    HasBeenWrittenTo = false;

    SmallPtrSet<const Access *, 8> DominatingWrites;
    SmallVector<std::pair<const Access *, bool>, 8> InterferingAccesses;

    Function &Scope = *I.getFunction();
    bool IsKnownNoSync;
    bool IsAssumedNoSync = AA::hasAssumedIRAttr<Attribute::NoSync>(
        A, &QueryingAA, IRPosition::function(Scope), DepClassTy::OPTIONAL,
        IsKnownNoSync);
    const auto *ExecDomainAA = A.lookupAAFor<AAExecutionDomain>(
        IRPosition::function(Scope), &QueryingAA, DepClassTy::NONE);
    bool AllInSameNoSyncFn = IsAssumedNoSync;
    bool InstIsExecutedByInitialThreadOnly =
        ExecDomainAA && ExecDomainAA->isExecutedByInitialThreadOnly(I);

    // If the function is not ending in aligned barriers, we need the stores to
    // be in aligned barriers. The load being in one is not sufficient since the
    // store might be executed by a thread that disappears after, causing the
    // aligned barrier guarding the load to unblock and the load to read a value
    // that has no CFG path to the load.
    bool InstIsExecutedInAlignedRegion =
        FindInterferingReads && ExecDomainAA &&
        ExecDomainAA->isExecutedInAlignedRegion(A, I);

    if (InstIsExecutedInAlignedRegion || InstIsExecutedByInitialThreadOnly)
      A.recordDependence(*ExecDomainAA, QueryingAA, DepClassTy::OPTIONAL);

    InformationCache &InfoCache = A.getInfoCache();
    bool IsThreadLocalObj =
        AA::isAssumedThreadLocalObject(A, getAssociatedValue(), *this);

    // Helper to determine if we need to consider threading, which we cannot
    // right now. However, if the function is (assumed) nosync or the thread
    // executing all instructions is the main thread only we can ignore
    // threading. Also, thread-local objects do not require threading reasoning.
    // Finally, we can ignore threading if either access is executed in an
    // aligned region.
    auto CanIgnoreThreadingForInst = [&](const Instruction &I) -> bool {
      if (IsThreadLocalObj || AllInSameNoSyncFn)
        return true;
      const auto *FnExecDomainAA =
          I.getFunction() == &Scope
              ? ExecDomainAA
              : A.lookupAAFor<AAExecutionDomain>(
                    IRPosition::function(*I.getFunction()), &QueryingAA,
                    DepClassTy::NONE);
      if (!FnExecDomainAA)
        return false;
      if (InstIsExecutedInAlignedRegion ||
          (FindInterferingWrites &&
           FnExecDomainAA->isExecutedInAlignedRegion(A, I))) {
        A.recordDependence(*FnExecDomainAA, QueryingAA, DepClassTy::OPTIONAL);
        return true;
      }
      if (InstIsExecutedByInitialThreadOnly &&
          FnExecDomainAA->isExecutedByInitialThreadOnly(I)) {
        A.recordDependence(*FnExecDomainAA, QueryingAA, DepClassTy::OPTIONAL);
        return true;
      }
      return false;
    };

    // Helper to determine if the access is executed by the same thread as the
    // given instruction, for now it is sufficient to avoid any potential
    // threading effects as we cannot deal with them anyway.
    auto CanIgnoreThreading = [&](const Access &Acc) -> bool {
      return CanIgnoreThreadingForInst(*Acc.getRemoteInst()) ||
             (Acc.getRemoteInst() != Acc.getLocalInst() &&
              CanIgnoreThreadingForInst(*Acc.getLocalInst()));
    };

    // TODO: Use inter-procedural reachability and dominance.
    bool IsKnownNoRecurse;
    AA::hasAssumedIRAttr<Attribute::NoRecurse>(
        A, this, IRPosition::function(Scope), DepClassTy::OPTIONAL,
        IsKnownNoRecurse);

    // TODO: Use reaching kernels from AAKernelInfo (or move it to
    // AAExecutionDomain) such that we allow scopes other than kernels as long
    // as the reaching kernels are disjoint.
    bool InstInKernel = Scope.hasFnAttribute("kernel");
    bool ObjHasKernelLifetime = false;
    const bool UseDominanceReasoning =
        FindInterferingWrites && IsKnownNoRecurse;
    const DominatorTree *DT =
        InfoCache.getAnalysisResultForFunction<DominatorTreeAnalysis>(Scope);

    // Helper to check if a value has "kernel lifetime", that is it will not
    // outlive a GPU kernel. This is true for shared, constant, and local
    // globals on AMD and NVIDIA GPUs.
    auto HasKernelLifetime = [&](Value *V, Module &M) {
      if (!AA::isGPU(M))
        return false;
      switch (AA::GPUAddressSpace(V->getType()->getPointerAddressSpace())) {
      case AA::GPUAddressSpace::Shared:
      case AA::GPUAddressSpace::Constant:
      case AA::GPUAddressSpace::Local:
        return true;
      default:
        return false;
      };
    };

    // The IsLiveInCalleeCB will be used by the AA::isPotentiallyReachable query
    // to determine if we should look at reachability from the callee. For
    // certain pointers we know the lifetime and we do not have to step into the
    // callee to determine reachability as the pointer would be dead in the
    // callee. See the conditional initialization below.
    std::function<bool(const Function &)> IsLiveInCalleeCB;

    if (auto *AI = dyn_cast<AllocaInst>(&getAssociatedValue())) {
      // If the alloca containing function is not recursive the alloca
      // must be dead in the callee.
      const Function *AIFn = AI->getFunction();
      ObjHasKernelLifetime = AIFn->hasFnAttribute("kernel");
      bool IsKnownNoRecurse;
      if (AA::hasAssumedIRAttr<Attribute::NoRecurse>(
              A, this, IRPosition::function(*AIFn), DepClassTy::OPTIONAL,
              IsKnownNoRecurse)) {
        IsLiveInCalleeCB = [AIFn](const Function &Fn) { return AIFn != &Fn; };
      }
    } else if (auto *GV = dyn_cast<GlobalValue>(&getAssociatedValue())) {
      // If the global has kernel lifetime we can stop if we reach a kernel
      // as it is "dead" in the (unknown) callees.
      ObjHasKernelLifetime = HasKernelLifetime(GV, *GV->getParent());
      if (ObjHasKernelLifetime)
        IsLiveInCalleeCB = [](const Function &Fn) {
          return !Fn.hasFnAttribute("kernel");
        };
    }

    // Set of accesses/instructions that will overwrite the result and are
    // therefore blockers in the reachability traversal.
    AA::InstExclusionSetTy ExclusionSet;

    auto AccessCB = [&](const Access &Acc, bool Exact) {
      Function *AccScope = Acc.getRemoteInst()->getFunction();
      bool AccInSameScope = AccScope == &Scope;

      // If the object has kernel lifetime we can ignore accesses only reachable
      // by other kernels. For now we only skip accesses *in* other kernels.
      if (InstInKernel && ObjHasKernelLifetime && !AccInSameScope &&
          AccScope->hasFnAttribute("kernel"))
        return true;

      if (Exact && Acc.isMustAccess() && Acc.getRemoteInst() != &I) {
        if (Acc.isWrite() || (isa<LoadInst>(I) && Acc.isWriteOrAssumption()))
          ExclusionSet.insert(Acc.getRemoteInst());
      }

      if ((!FindInterferingWrites || !Acc.isWriteOrAssumption()) &&
          (!FindInterferingReads || !Acc.isRead()))
        return true;

      bool Dominates = FindInterferingWrites && DT && Exact &&
                       Acc.isMustAccess() && AccInSameScope &&
                       DT->dominates(Acc.getRemoteInst(), &I);
      if (Dominates)
        DominatingWrites.insert(&Acc);

      // Track if all interesting accesses are in the same `nosync` function as
      // the given instruction.
      AllInSameNoSyncFn &= Acc.getRemoteInst()->getFunction() == &Scope;

      InterferingAccesses.push_back({&Acc, Exact});
      return true;
    };
    if (!State::forallInterferingAccesses(I, AccessCB, Range))
      return false;

    HasBeenWrittenTo = !DominatingWrites.empty();

    // Dominating writes form a chain, find the least/lowest member.
    Instruction *LeastDominatingWriteInst = nullptr;
    for (const Access *Acc : DominatingWrites) {
      if (!LeastDominatingWriteInst) {
        LeastDominatingWriteInst = Acc->getRemoteInst();
      } else if (DT->dominates(LeastDominatingWriteInst,
                               Acc->getRemoteInst())) {
        LeastDominatingWriteInst = Acc->getRemoteInst();
      }
    }

    // Helper to determine if we can skip a specific write access.
    auto CanSkipAccess = [&](const Access &Acc, bool Exact) {
      if (SkipCB && SkipCB(Acc))
        return true;
      if (!CanIgnoreThreading(Acc))
        return false;

      // Check read (RAW) dependences and write (WAR) dependences as necessary.
      // If we successfully excluded all effects we are interested in, the
      // access can be skipped.
      bool ReadChecked = !FindInterferingReads;
      bool WriteChecked = !FindInterferingWrites;

      // If the instruction cannot reach the access, the former does not
      // interfere with what the access reads.
      if (!ReadChecked) {
        if (!AA::isPotentiallyReachable(A, I, *Acc.getRemoteInst(), QueryingAA,
                                        &ExclusionSet, IsLiveInCalleeCB))
          ReadChecked = true;
      }
      // If the instruction cannot be reach from the access, the latter does not
      // interfere with what the instruction reads.
      if (!WriteChecked) {
        if (!AA::isPotentiallyReachable(A, *Acc.getRemoteInst(), I, QueryingAA,
                                        &ExclusionSet, IsLiveInCalleeCB))
          WriteChecked = true;
      }

      // If we still might be affected by the write of the access but there are
      // dominating writes in the function of the instruction
      // (HasBeenWrittenTo), we can try to reason that the access is overwritten
      // by them. This would have happend above if they are all in the same
      // function, so we only check the inter-procedural case. Effectively, we
      // want to show that there is no call after the dominting write that might
      // reach the access, and when it returns reach the instruction with the
      // updated value. To this end, we iterate all call sites, check if they
      // might reach the instruction without going through another access
      // (ExclusionSet) and at the same time might reach the access. However,
      // that is all part of AAInterFnReachability.
      if (!WriteChecked && HasBeenWrittenTo &&
          Acc.getRemoteInst()->getFunction() != &Scope) {

        const auto *FnReachabilityAA = A.getAAFor<AAInterFnReachability>(
            QueryingAA, IRPosition::function(Scope), DepClassTy::OPTIONAL);

        // Without going backwards in the call tree, can we reach the access
        // from the least dominating write. Do not allow to pass the instruction
        // itself either.
        bool Inserted = ExclusionSet.insert(&I).second;

        if (!FnReachabilityAA ||
            !FnReachabilityAA->instructionCanReach(
                A, *LeastDominatingWriteInst,
                *Acc.getRemoteInst()->getFunction(), &ExclusionSet))
          WriteChecked = true;

        if (Inserted)
          ExclusionSet.erase(&I);
      }

      if (ReadChecked && WriteChecked)
        return true;

      if (!DT || !UseDominanceReasoning)
        return false;
      if (!DominatingWrites.count(&Acc))
        return false;
      return LeastDominatingWriteInst != Acc.getRemoteInst();
    };

    // Run the user callback on all accesses we cannot skip and return if
    // that succeeded for all or not.
    for (auto &It : InterferingAccesses) {
      if ((!AllInSameNoSyncFn && !IsThreadLocalObj && !ExecDomainAA) ||
          !CanSkipAccess(*It.first, It.second)) {
        if (!UserCB(*It.first, It.second))
          return false;
      }
    }
    return true;
  }

  ChangeStatus translateAndAddStateFromCallee(Attributor &A,
                                              const AAPointerInfo &OtherAA,
                                              CallBase &CB) {
    using namespace AA::PointerInfo;
    if (!OtherAA.getState().isValidState() || !isValidState())
      return indicatePessimisticFixpoint();

    const auto &OtherAAImpl = static_cast<const AAPointerInfoImpl &>(OtherAA);
    bool IsByval = OtherAAImpl.getAssociatedArgument()->hasByValAttr();

    // Combine the accesses bin by bin.
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    const auto &State = OtherAAImpl.getState();
    for (const auto &It : State) {
      for (auto Index : It.getSecond()) {
        const auto &RAcc = State.getAccess(Index);
        if (IsByval && !RAcc.isRead())
          continue;
        bool UsedAssumedInformation = false;
        AccessKind AK = RAcc.getKind();
        auto Content = A.translateArgumentToCallSiteContent(
            RAcc.getContent(), CB, *this, UsedAssumedInformation);
        AK = AccessKind(AK & (IsByval ? AccessKind::AK_R : AccessKind::AK_RW));
        AK = AccessKind(AK | (RAcc.isMayAccess() ? AK_MAY : AK_MUST));

        Changed |= addAccess(A, RAcc.getRanges(), CB, Content, AK,
                             RAcc.getType(), RAcc.getRemoteInst());
      }
    }
    return Changed;
  }

  ChangeStatus translateAndAddState(Attributor &A, const AAPointerInfo &OtherAA,
                                    const OffsetInfo &Offsets, CallBase &CB) {
    using namespace AA::PointerInfo;
    if (!OtherAA.getState().isValidState() || !isValidState())
      return indicatePessimisticFixpoint();

    const auto &OtherAAImpl = static_cast<const AAPointerInfoImpl &>(OtherAA);

    // Combine the accesses bin by bin.
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    const auto &State = OtherAAImpl.getState();
    for (const auto &It : State) {
      for (auto Index : It.getSecond()) {
        const auto &RAcc = State.getAccess(Index);
        for (auto Offset : Offsets) {
          auto NewRanges = Offset == AA::RangeTy::Unknown
                               ? AA::RangeTy::getUnknown()
                               : RAcc.getRanges();
          if (!NewRanges.isUnknown()) {
            NewRanges.addToAllOffsets(Offset);
          }
          Changed |=
              addAccess(A, NewRanges, CB, RAcc.getContent(), RAcc.getKind(),
                        RAcc.getType(), RAcc.getRemoteInst());
        }
      }
    }
    return Changed;
  }

  /// Statistic tracking for all AAPointerInfo implementations.
  /// See AbstractAttribute::trackStatistics().
  void trackPointerInfoStatistics(const IRPosition &IRP) const {}

  /// Dump the state into \p O.
  void dumpState(raw_ostream &O) {
    for (auto &It : OffsetBins) {
      O << "[" << It.first.Offset << "-" << It.first.Offset + It.first.Size
        << "] : " << It.getSecond().size() << "\n";
      for (auto AccIndex : It.getSecond()) {
        auto &Acc = AccessList[AccIndex];
        O << "     - " << Acc.getKind() << " - " << *Acc.getLocalInst() << "\n";
        if (Acc.getLocalInst() != Acc.getRemoteInst())
          O << "     -->                         " << *Acc.getRemoteInst()
            << "\n";
        if (!Acc.isWrittenValueYetUndetermined()) {
          if (isa_and_nonnull<Function>(Acc.getWrittenValue()))
            O << "       - c: func " << Acc.getWrittenValue()->getName()
              << "\n";
          else if (Acc.getWrittenValue())
            O << "       - c: " << *Acc.getWrittenValue() << "\n";
          else
            O << "       - c: <unknown>\n";
        }
      }
    }
  }
};

struct AAPointerInfoFloating : public AAPointerInfoImpl {
  using AccessKind = AAPointerInfo::AccessKind;
  AAPointerInfoFloating(const IRPosition &IRP, Attributor &A)
      : AAPointerInfoImpl(IRP, A) {}

  /// Deal with an access and signal if it was handled successfully.
  bool handleAccess(Attributor &A, Instruction &I,
                    std::optional<Value *> Content, AccessKind Kind,
                    SmallVectorImpl<int64_t> &Offsets, ChangeStatus &Changed,
                    Type &Ty) {
    using namespace AA::PointerInfo;
    auto Size = AA::RangeTy::Unknown;
    const DataLayout &DL = A.getDataLayout();
    TypeSize AccessSize = DL.getTypeStoreSize(&Ty);
    if (!AccessSize.isScalable())
      Size = AccessSize.getFixedValue();

    // Make a strictly ascending list of offsets as required by addAccess()
    llvm::sort(Offsets);
    auto *Last = llvm::unique(Offsets);
    Offsets.erase(Last, Offsets.end());

    VectorType *VT = dyn_cast<VectorType>(&Ty);
    if (!VT || VT->getElementCount().isScalable() ||
        !Content.value_or(nullptr) || !isa<Constant>(*Content) ||
        (*Content)->getType() != VT ||
        DL.getTypeStoreSize(VT->getElementType()).isScalable()) {
      Changed = Changed | addAccess(A, {Offsets, Size}, I, Content, Kind, &Ty);
    } else {
      // Handle vector stores with constant content element-wise.
      // TODO: We could look for the elements or create instructions
      //       representing them.
      // TODO: We need to push the Content into the range abstraction
      //       (AA::RangeTy) to allow different content values for different
      //       ranges. ranges. Hence, support vectors storing different values.
      Type *ElementType = VT->getElementType();
      int64_t ElementSize = DL.getTypeStoreSize(ElementType).getFixedValue();
      auto *ConstContent = cast<Constant>(*Content);
      Type *Int32Ty = Type::getInt32Ty(ElementType->getContext());
      SmallVector<int64_t> ElementOffsets(Offsets.begin(), Offsets.end());

      for (int i = 0, e = VT->getElementCount().getFixedValue(); i != e; ++i) {
        Value *ElementContent = ConstantExpr::getExtractElement(
            ConstContent, ConstantInt::get(Int32Ty, i));

        // Add the element access.
        Changed = Changed | addAccess(A, {ElementOffsets, ElementSize}, I,
                                      ElementContent, Kind, ElementType);

        // Advance the offsets for the next element.
        for (auto &ElementOffset : ElementOffsets)
          ElementOffset += ElementSize;
      }
    }
    return true;
  };

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override;

  /// If the indices to \p GEP can be traced to constants, incorporate all
  /// of these into \p UsrOI.
  ///
  /// \return true iff \p UsrOI is updated.
  bool collectConstantsForGEP(Attributor &A, const DataLayout &DL,
                              OffsetInfo &UsrOI, const OffsetInfo &PtrOI,
                              const GEPOperator *GEP);

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    AAPointerInfoImpl::trackPointerInfoStatistics(getIRPosition());
  }
};

bool AAPointerInfoFloating::collectConstantsForGEP(Attributor &A,
                                                   const DataLayout &DL,
                                                   OffsetInfo &UsrOI,
                                                   const OffsetInfo &PtrOI,
                                                   const GEPOperator *GEP) {
  unsigned BitWidth = DL.getIndexTypeSizeInBits(GEP->getType());
  MapVector<Value *, APInt> VariableOffsets;
  APInt ConstantOffset(BitWidth, 0);

  assert(!UsrOI.isUnknown() && !PtrOI.isUnknown() &&
         "Don't look for constant values if the offset has already been "
         "determined to be unknown.");

  if (!GEP->collectOffset(DL, BitWidth, VariableOffsets, ConstantOffset)) {
    UsrOI.setUnknown();
    return true;
  }

  LLVM_DEBUG(dbgs() << "[AAPointerInfo] GEP offset is "
                    << (VariableOffsets.empty() ? "" : "not") << " constant "
                    << *GEP << "\n");

  auto Union = PtrOI;
  Union.addToAll(ConstantOffset.getSExtValue());

  // Each VI in VariableOffsets has a set of potential constant values. Every
  // combination of elements, picked one each from these sets, is separately
  // added to the original set of offsets, thus resulting in more offsets.
  for (const auto &VI : VariableOffsets) {
    auto *PotentialConstantsAA = A.getAAFor<AAPotentialConstantValues>(
        *this, IRPosition::value(*VI.first), DepClassTy::OPTIONAL);
    if (!PotentialConstantsAA || !PotentialConstantsAA->isValidState()) {
      UsrOI.setUnknown();
      return true;
    }

    // UndefValue is treated as a zero, which leaves Union as is.
    if (PotentialConstantsAA->undefIsContained())
      continue;

    // We need at least one constant in every set to compute an actual offset.
    // Otherwise, we end up pessimizing AAPointerInfo by respecting offsets that
    // don't actually exist. In other words, the absence of constant values
    // implies that the operation can be assumed dead for now.
    auto &AssumedSet = PotentialConstantsAA->getAssumedSet();
    if (AssumedSet.empty())
      return false;

    OffsetInfo Product;
    for (const auto &ConstOffset : AssumedSet) {
      auto CopyPerOffset = Union;
      CopyPerOffset.addToAll(ConstOffset.getSExtValue() *
                             VI.second.getZExtValue());
      Product.merge(CopyPerOffset);
    }
    Union = Product;
  }

  UsrOI = std::move(Union);
  return true;
}

ChangeStatus AAPointerInfoFloating::updateImpl(Attributor &A) {
  using namespace AA::PointerInfo;
  ChangeStatus Changed = ChangeStatus::UNCHANGED;
  const DataLayout &DL = A.getDataLayout();
  Value &AssociatedValue = getAssociatedValue();

  DenseMap<Value *, OffsetInfo> OffsetInfoMap;
  OffsetInfoMap[&AssociatedValue].insert(0);

  auto HandlePassthroughUser = [&](Value *Usr, Value *CurPtr, bool &Follow) {
    // One does not simply walk into a map and assign a reference to a possibly
    // new location. That can cause an invalidation before the assignment
    // happens, like so:
    //
    //   OffsetInfoMap[Usr] = OffsetInfoMap[CurPtr]; /* bad idea! */
    //
    // The RHS is a reference that may be invalidated by an insertion caused by
    // the LHS. So we ensure that the side-effect of the LHS happens first.

    assert(OffsetInfoMap.contains(CurPtr) &&
           "CurPtr does not exist in the map!");

    auto &UsrOI = OffsetInfoMap[Usr];
    auto &PtrOI = OffsetInfoMap[CurPtr];
    assert(!PtrOI.isUnassigned() &&
           "Cannot pass through if the input Ptr was not visited!");
    UsrOI.merge(PtrOI);
    Follow = true;
    return true;
  };

  auto UsePred = [&](const Use &U, bool &Follow) -> bool {
    Value *CurPtr = U.get();
    User *Usr = U.getUser();
    LLVM_DEBUG(dbgs() << "[AAPointerInfo] Analyze " << *CurPtr << " in " << *Usr
                      << "\n");
    assert(OffsetInfoMap.count(CurPtr) &&
           "The current pointer offset should have been seeded!");

    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Usr)) {
      if (CE->isCast())
        return HandlePassthroughUser(Usr, CurPtr, Follow);
      if (!isa<GEPOperator>(CE)) {
        LLVM_DEBUG(dbgs() << "[AAPointerInfo] Unhandled constant user " << *CE
                          << "\n");
        return false;
      }
    }
    if (auto *GEP = dyn_cast<GEPOperator>(Usr)) {
      // Note the order here, the Usr access might change the map, CurPtr is
      // already in it though.
      auto &UsrOI = OffsetInfoMap[Usr];
      auto &PtrOI = OffsetInfoMap[CurPtr];

      if (UsrOI.isUnknown())
        return true;

      if (PtrOI.isUnknown()) {
        Follow = true;
        UsrOI.setUnknown();
        return true;
      }

      Follow = collectConstantsForGEP(A, DL, UsrOI, PtrOI, GEP);
      return true;
    }
    if (isa<PtrToIntInst>(Usr))
      return false;
    if (isa<CastInst>(Usr) || isa<SelectInst>(Usr) || isa<ReturnInst>(Usr))
      return HandlePassthroughUser(Usr, CurPtr, Follow);

    // For PHIs we need to take care of the recurrence explicitly as the value
    // might change while we iterate through a loop. For now, we give up if
    // the PHI is not invariant.
    if (auto *PHI = dyn_cast<PHINode>(Usr)) {
      // Note the order here, the Usr access might change the map, CurPtr is
      // already in it though.
      bool IsFirstPHIUser = !OffsetInfoMap.count(PHI);
      auto &UsrOI = OffsetInfoMap[PHI];
      auto &PtrOI = OffsetInfoMap[CurPtr];

      // Check if the PHI operand has already an unknown offset as we can't
      // improve on that anymore.
      if (PtrOI.isUnknown()) {
        LLVM_DEBUG(dbgs() << "[AAPointerInfo] PHI operand offset unknown "
                          << *CurPtr << " in " << *PHI << "\n");
        Follow = !UsrOI.isUnknown();
        UsrOI.setUnknown();
        return true;
      }

      // Check if the PHI is invariant (so far).
      if (UsrOI == PtrOI) {
        assert(!PtrOI.isUnassigned() &&
               "Cannot assign if the current Ptr was not visited!");
        LLVM_DEBUG(dbgs() << "[AAPointerInfo] PHI is invariant (so far)");
        return true;
      }

      // Check if the PHI operand can be traced back to AssociatedValue.
      APInt Offset(
          DL.getIndexSizeInBits(CurPtr->getType()->getPointerAddressSpace()),
          0);
      Value *CurPtrBase = CurPtr->stripAndAccumulateConstantOffsets(
          DL, Offset, /* AllowNonInbounds */ true);
      auto It = OffsetInfoMap.find(CurPtrBase);
      if (It == OffsetInfoMap.end()) {
        LLVM_DEBUG(dbgs() << "[AAPointerInfo] PHI operand is too complex "
                          << *CurPtr << " in " << *PHI
                          << " (base: " << *CurPtrBase << ")\n");
        UsrOI.setUnknown();
        Follow = true;
        return true;
      }

      // Check if the PHI operand is not dependent on the PHI itself. Every
      // recurrence is a cyclic net of PHIs in the data flow, and has an
      // equivalent Cycle in the control flow. One of those PHIs must be in the
      // header of that control flow Cycle. This is independent of the choice of
      // Cycles reported by CycleInfo. It is sufficient to check the PHIs in
      // every Cycle header; if such a node is marked unknown, this will
      // eventually propagate through the whole net of PHIs in the recurrence.
      const auto *CI =
          A.getInfoCache().getAnalysisResultForFunction<CycleAnalysis>(
              *PHI->getFunction());
      if (mayBeInCycle(CI, cast<Instruction>(Usr), /* HeaderOnly */ true)) {
        auto BaseOI = It->getSecond();
        BaseOI.addToAll(Offset.getZExtValue());
        if (IsFirstPHIUser || BaseOI == UsrOI) {
          LLVM_DEBUG(dbgs() << "[AAPointerInfo] PHI is invariant " << *CurPtr
                            << " in " << *Usr << "\n");
          return HandlePassthroughUser(Usr, CurPtr, Follow);
        }

        LLVM_DEBUG(
            dbgs() << "[AAPointerInfo] PHI operand pointer offset mismatch "
                   << *CurPtr << " in " << *PHI << "\n");
        UsrOI.setUnknown();
        Follow = true;
        return true;
      }

      UsrOI.merge(PtrOI);
      Follow = true;
      return true;
    }

    if (auto *LoadI = dyn_cast<LoadInst>(Usr)) {
      // If the access is to a pointer that may or may not be the associated
      // value, e.g. due to a PHI, we cannot assume it will be read.
      AccessKind AK = AccessKind::AK_R;
      if (getUnderlyingObject(CurPtr) == &AssociatedValue)
        AK = AccessKind(AK | AccessKind::AK_MUST);
      else
        AK = AccessKind(AK | AccessKind::AK_MAY);
      if (!handleAccess(A, *LoadI, /* Content */ nullptr, AK,
                        OffsetInfoMap[CurPtr].Offsets, Changed,
                        *LoadI->getType()))
        return false;

      auto IsAssumption = [](Instruction &I) {
        if (auto *II = dyn_cast<IntrinsicInst>(&I))
          return II->isAssumeLikeIntrinsic();
        return false;
      };

      auto IsImpactedInRange = [&](Instruction *FromI, Instruction *ToI) {
        // Check if the assumption and the load are executed together without
        // memory modification.
        do {
          if (FromI->mayWriteToMemory() && !IsAssumption(*FromI))
            return true;
          FromI = FromI->getNextNonDebugInstruction();
        } while (FromI && FromI != ToI);
        return false;
      };

      BasicBlock *BB = LoadI->getParent();
      auto IsValidAssume = [&](IntrinsicInst &IntrI) {
        if (IntrI.getIntrinsicID() != Intrinsic::assume)
          return false;
        BasicBlock *IntrBB = IntrI.getParent();
        if (IntrI.getParent() == BB) {
          if (IsImpactedInRange(LoadI->getNextNonDebugInstruction(), &IntrI))
            return false;
        } else {
          auto PredIt = pred_begin(IntrBB);
          if (PredIt == pred_end(IntrBB))
            return false;
          if ((*PredIt) != BB)
            return false;
          if (++PredIt != pred_end(IntrBB))
            return false;
          for (auto *SuccBB : successors(BB)) {
            if (SuccBB == IntrBB)
              continue;
            if (isa<UnreachableInst>(SuccBB->getTerminator()))
              continue;
            return false;
          }
          if (IsImpactedInRange(LoadI->getNextNonDebugInstruction(),
                                BB->getTerminator()))
            return false;
          if (IsImpactedInRange(&IntrBB->front(), &IntrI))
            return false;
        }
        return true;
      };

      std::pair<Value *, IntrinsicInst *> Assumption;
      for (const Use &LoadU : LoadI->uses()) {
        if (auto *CmpI = dyn_cast<CmpInst>(LoadU.getUser())) {
          if (!CmpI->isEquality() || !CmpI->isTrueWhenEqual())
            continue;
          for (const Use &CmpU : CmpI->uses()) {
            if (auto *IntrI = dyn_cast<IntrinsicInst>(CmpU.getUser())) {
              if (!IsValidAssume(*IntrI))
                continue;
              int Idx = CmpI->getOperandUse(0) == LoadU;
              Assumption = {CmpI->getOperand(Idx), IntrI};
              break;
            }
          }
        }
        if (Assumption.first)
          break;
      }

      // Check if we found an assumption associated with this load.
      if (!Assumption.first || !Assumption.second)
        return true;

      LLVM_DEBUG(dbgs() << "[AAPointerInfo] Assumption found "
                        << *Assumption.second << ": " << *LoadI
                        << " == " << *Assumption.first << "\n");
      bool UsedAssumedInformation = false;
      std::optional<Value *> Content = nullptr;
      if (Assumption.first)
        Content =
            A.getAssumedSimplified(*Assumption.first, *this,
                                   UsedAssumedInformation, AA::Interprocedural);
      return handleAccess(
          A, *Assumption.second, Content, AccessKind::AK_ASSUMPTION,
          OffsetInfoMap[CurPtr].Offsets, Changed, *LoadI->getType());
    }

    auto HandleStoreLike = [&](Instruction &I, Value *ValueOp, Type &ValueTy,
                               ArrayRef<Value *> OtherOps, AccessKind AK) {
      for (auto *OtherOp : OtherOps) {
        if (OtherOp == CurPtr) {
          LLVM_DEBUG(
              dbgs()
              << "[AAPointerInfo] Escaping use in store like instruction " << I
              << "\n");
          return false;
        }
      }

      // If the access is to a pointer that may or may not be the associated
      // value, e.g. due to a PHI, we cannot assume it will be written.
      if (getUnderlyingObject(CurPtr) == &AssociatedValue)
        AK = AccessKind(AK | AccessKind::AK_MUST);
      else
        AK = AccessKind(AK | AccessKind::AK_MAY);
      bool UsedAssumedInformation = false;
      std::optional<Value *> Content = nullptr;
      if (ValueOp)
        Content = A.getAssumedSimplified(
            *ValueOp, *this, UsedAssumedInformation, AA::Interprocedural);
      return handleAccess(A, I, Content, AK, OffsetInfoMap[CurPtr].Offsets,
                          Changed, ValueTy);
    };

    if (auto *StoreI = dyn_cast<StoreInst>(Usr))
      return HandleStoreLike(*StoreI, StoreI->getValueOperand(),
                             *StoreI->getValueOperand()->getType(),
                             {StoreI->getValueOperand()}, AccessKind::AK_W);
    if (auto *RMWI = dyn_cast<AtomicRMWInst>(Usr))
      return HandleStoreLike(*RMWI, nullptr, *RMWI->getValOperand()->getType(),
                             {RMWI->getValOperand()}, AccessKind::AK_RW);
    if (auto *CXI = dyn_cast<AtomicCmpXchgInst>(Usr))
      return HandleStoreLike(
          *CXI, nullptr, *CXI->getNewValOperand()->getType(),
          {CXI->getCompareOperand(), CXI->getNewValOperand()},
          AccessKind::AK_RW);

    if (auto *CB = dyn_cast<CallBase>(Usr)) {
      if (CB->isLifetimeStartOrEnd())
        return true;
      const auto *TLI =
          A.getInfoCache().getTargetLibraryInfoForFunction(*CB->getFunction());
      if (getFreedOperand(CB, TLI) == U)
        return true;
      if (CB->isArgOperand(&U)) {
        unsigned ArgNo = CB->getArgOperandNo(&U);
        const auto *CSArgPI = A.getAAFor<AAPointerInfo>(
            *this, IRPosition::callsite_argument(*CB, ArgNo),
            DepClassTy::REQUIRED);
        if (!CSArgPI)
          return false;
        Changed =
            translateAndAddState(A, *CSArgPI, OffsetInfoMap[CurPtr], *CB) |
            Changed;
        return isValidState();
      }
      LLVM_DEBUG(dbgs() << "[AAPointerInfo] Call user not handled " << *CB
                        << "\n");
      // TODO: Allow some call uses
      return false;
    }

    LLVM_DEBUG(dbgs() << "[AAPointerInfo] User not handled " << *Usr << "\n");
    return false;
  };
  auto EquivalentUseCB = [&](const Use &OldU, const Use &NewU) {
    assert(OffsetInfoMap.count(OldU) && "Old use should be known already!");
    if (OffsetInfoMap.count(NewU)) {
      LLVM_DEBUG({
        if (!(OffsetInfoMap[NewU] == OffsetInfoMap[OldU])) {
          dbgs() << "[AAPointerInfo] Equivalent use callback failed: "
                 << OffsetInfoMap[NewU] << " vs " << OffsetInfoMap[OldU]
                 << "\n";
        }
      });
      return OffsetInfoMap[NewU] == OffsetInfoMap[OldU];
    }
    OffsetInfoMap[NewU] = OffsetInfoMap[OldU];
    return true;
  };
  if (!A.checkForAllUses(UsePred, *this, AssociatedValue,
                         /* CheckBBLivenessOnly */ true, DepClassTy::OPTIONAL,
                         /* IgnoreDroppableUses */ true, EquivalentUseCB)) {
    LLVM_DEBUG(dbgs() << "[AAPointerInfo] Check for all uses failed, abort!\n");
    return indicatePessimisticFixpoint();
  }

  LLVM_DEBUG({
    dbgs() << "Accesses by bin after update:\n";
    dumpState(dbgs());
  });

  return Changed;
}

struct AAPointerInfoReturned final : AAPointerInfoImpl {
  AAPointerInfoReturned(const IRPosition &IRP, Attributor &A)
      : AAPointerInfoImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    AAPointerInfoImpl::trackPointerInfoStatistics(getIRPosition());
  }
};

struct AAPointerInfoArgument final : AAPointerInfoFloating {
  AAPointerInfoArgument(const IRPosition &IRP, Attributor &A)
      : AAPointerInfoFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    AAPointerInfoImpl::trackPointerInfoStatistics(getIRPosition());
  }
};

struct AAPointerInfoCallSiteArgument final : AAPointerInfoFloating {
  AAPointerInfoCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAPointerInfoFloating(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    using namespace AA::PointerInfo;
    // We handle memory intrinsics explicitly, at least the first (=
    // destination) and second (=source) arguments as we know how they are
    // accessed.
    if (auto *MI = dyn_cast_or_null<MemIntrinsic>(getCtxI())) {
      ConstantInt *Length = dyn_cast<ConstantInt>(MI->getLength());
      int64_t LengthVal = AA::RangeTy::Unknown;
      if (Length)
        LengthVal = Length->getSExtValue();
      unsigned ArgNo = getIRPosition().getCallSiteArgNo();
      ChangeStatus Changed = ChangeStatus::UNCHANGED;
      if (ArgNo > 1) {
        LLVM_DEBUG(dbgs() << "[AAPointerInfo] Unhandled memory intrinsic "
                          << *MI << "\n");
        return indicatePessimisticFixpoint();
      } else {
        auto Kind =
            ArgNo == 0 ? AccessKind::AK_MUST_WRITE : AccessKind::AK_MUST_READ;
        Changed =
            Changed | addAccess(A, {0, LengthVal}, *MI, nullptr, Kind, nullptr);
      }
      LLVM_DEBUG({
        dbgs() << "Accesses by bin after update:\n";
        dumpState(dbgs());
      });

      return Changed;
    }

    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    Argument *Arg = getAssociatedArgument();
    if (Arg) {
      const IRPosition &ArgPos = IRPosition::argument(*Arg);
      auto *ArgAA =
          A.getAAFor<AAPointerInfo>(*this, ArgPos, DepClassTy::REQUIRED);
      if (ArgAA && ArgAA->getState().isValidState())
        return translateAndAddStateFromCallee(A, *ArgAA,
                                              *cast<CallBase>(getCtxI()));
      if (!Arg->getParent()->isDeclaration())
        return indicatePessimisticFixpoint();
    }

    bool IsKnownNoCapture;
    if (!AA::hasAssumedIRAttr<Attribute::NoCapture>(
            A, this, getIRPosition(), DepClassTy::OPTIONAL, IsKnownNoCapture))
      return indicatePessimisticFixpoint();

    bool IsKnown = false;
    if (AA::isAssumedReadNone(A, getIRPosition(), *this, IsKnown))
      return ChangeStatus::UNCHANGED;
    bool ReadOnly = AA::isAssumedReadOnly(A, getIRPosition(), *this, IsKnown);
    auto Kind =
        ReadOnly ? AccessKind::AK_MAY_READ : AccessKind::AK_MAY_READ_WRITE;
    return addAccess(A, AA::RangeTy::getUnknown(), *getCtxI(), nullptr, Kind,
                     nullptr);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    AAPointerInfoImpl::trackPointerInfoStatistics(getIRPosition());
  }
};

struct AAPointerInfoCallSiteReturned final : AAPointerInfoFloating {
  AAPointerInfoCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAPointerInfoFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    AAPointerInfoImpl::trackPointerInfoStatistics(getIRPosition());
  }
};
} // namespace

/// -----------------------NoUnwind Function Attribute--------------------------

namespace {
struct AANoUnwindImpl : AANoUnwind {
  AANoUnwindImpl(const IRPosition &IRP, Attributor &A) : AANoUnwind(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::NoUnwind>(
        A, nullptr, getIRPosition(), DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "nounwind" : "may-unwind";
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto Opcodes = {
        (unsigned)Instruction::Invoke,      (unsigned)Instruction::CallBr,
        (unsigned)Instruction::Call,        (unsigned)Instruction::CleanupRet,
        (unsigned)Instruction::CatchSwitch, (unsigned)Instruction::Resume};

    auto CheckForNoUnwind = [&](Instruction &I) {
      if (!I.mayThrow(/* IncludePhaseOneUnwind */ true))
        return true;

      if (const auto *CB = dyn_cast<CallBase>(&I)) {
        bool IsKnownNoUnwind;
        return AA::hasAssumedIRAttr<Attribute::NoUnwind>(
            A, this, IRPosition::callsite_function(*CB), DepClassTy::REQUIRED,
            IsKnownNoUnwind);
      }
      return false;
    };

    bool UsedAssumedInformation = false;
    if (!A.checkForAllInstructions(CheckForNoUnwind, *this, Opcodes,
                                   UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }
};

struct AANoUnwindFunction final : public AANoUnwindImpl {
  AANoUnwindFunction(const IRPosition &IRP, Attributor &A)
      : AANoUnwindImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(nounwind) }
};

/// NoUnwind attribute deduction for a call sites.
struct AANoUnwindCallSite final
    : AACalleeToCallSite<AANoUnwind, AANoUnwindImpl> {
  AANoUnwindCallSite(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoUnwind, AANoUnwindImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(nounwind); }
};
} // namespace

/// ------------------------ NoSync Function Attribute -------------------------

bool AANoSync::isAlignedBarrier(const CallBase &CB, bool ExecutedAligned) {
  switch (CB.getIntrinsicID()) {
  case Intrinsic::nvvm_barrier0:
  case Intrinsic::nvvm_barrier0_and:
  case Intrinsic::nvvm_barrier0_or:
  case Intrinsic::nvvm_barrier0_popc:
    return true;
  case Intrinsic::amdgcn_s_barrier:
    if (ExecutedAligned)
      return true;
    break;
  default:
    break;
  }
  return hasAssumption(CB, KnownAssumptionString("ompx_aligned_barrier"));
}

bool AANoSync::isNonRelaxedAtomic(const Instruction *I) {
  if (!I->isAtomic())
    return false;

  if (auto *FI = dyn_cast<FenceInst>(I))
    // All legal orderings for fence are stronger than monotonic.
    return FI->getSyncScopeID() != SyncScope::SingleThread;
  if (auto *AI = dyn_cast<AtomicCmpXchgInst>(I)) {
    // Unordered is not a legal ordering for cmpxchg.
    return (AI->getSuccessOrdering() != AtomicOrdering::Monotonic ||
            AI->getFailureOrdering() != AtomicOrdering::Monotonic);
  }

  AtomicOrdering Ordering;
  switch (I->getOpcode()) {
  case Instruction::AtomicRMW:
    Ordering = cast<AtomicRMWInst>(I)->getOrdering();
    break;
  case Instruction::Store:
    Ordering = cast<StoreInst>(I)->getOrdering();
    break;
  case Instruction::Load:
    Ordering = cast<LoadInst>(I)->getOrdering();
    break;
  default:
    llvm_unreachable(
        "New atomic operations need to be known in the attributor.");
  }

  return (Ordering != AtomicOrdering::Unordered &&
          Ordering != AtomicOrdering::Monotonic);
}

/// Return true if this intrinsic is nosync.  This is only used for intrinsics
/// which would be nosync except that they have a volatile flag.  All other
/// intrinsics are simply annotated with the nosync attribute in Intrinsics.td.
bool AANoSync::isNoSyncIntrinsic(const Instruction *I) {
  if (auto *MI = dyn_cast<MemIntrinsic>(I))
    return !MI->isVolatile();
  return false;
}

namespace {
struct AANoSyncImpl : AANoSync {
  AANoSyncImpl(const IRPosition &IRP, Attributor &A) : AANoSync(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::NoSync>(A, nullptr, getIRPosition(),
                                                    DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "nosync" : "may-sync";
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override;
};

ChangeStatus AANoSyncImpl::updateImpl(Attributor &A) {

  auto CheckRWInstForNoSync = [&](Instruction &I) {
    return AA::isNoSyncInst(A, I, *this);
  };

  auto CheckForNoSync = [&](Instruction &I) {
    // At this point we handled all read/write effects and they are all
    // nosync, so they can be skipped.
    if (I.mayReadOrWriteMemory())
      return true;

    bool IsKnown;
    CallBase &CB = cast<CallBase>(I);
    if (AA::hasAssumedIRAttr<Attribute::NoSync>(
            A, this, IRPosition::callsite_function(CB), DepClassTy::OPTIONAL,
            IsKnown))
      return true;

    // non-convergent and readnone imply nosync.
    return !CB.isConvergent();
  };

  bool UsedAssumedInformation = false;
  if (!A.checkForAllReadWriteInstructions(CheckRWInstForNoSync, *this,
                                          UsedAssumedInformation) ||
      !A.checkForAllCallLikeInstructions(CheckForNoSync, *this,
                                         UsedAssumedInformation))
    return indicatePessimisticFixpoint();

  return ChangeStatus::UNCHANGED;
}

struct AANoSyncFunction final : public AANoSyncImpl {
  AANoSyncFunction(const IRPosition &IRP, Attributor &A)
      : AANoSyncImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(nosync) }
};

/// NoSync attribute deduction for a call sites.
struct AANoSyncCallSite final : AACalleeToCallSite<AANoSync, AANoSyncImpl> {
  AANoSyncCallSite(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoSync, AANoSyncImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(nosync); }
};
} // namespace

/// ------------------------ No-Free Attributes ----------------------------

namespace {
struct AANoFreeImpl : public AANoFree {
  AANoFreeImpl(const IRPosition &IRP, Attributor &A) : AANoFree(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::NoFree>(A, nullptr, getIRPosition(),
                                                    DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto CheckForNoFree = [&](Instruction &I) {
      bool IsKnown;
      return AA::hasAssumedIRAttr<Attribute::NoFree>(
          A, this, IRPosition::callsite_function(cast<CallBase>(I)),
          DepClassTy::REQUIRED, IsKnown);
    };

    bool UsedAssumedInformation = false;
    if (!A.checkForAllCallLikeInstructions(CheckForNoFree, *this,
                                           UsedAssumedInformation))
      return indicatePessimisticFixpoint();
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "nofree" : "may-free";
  }
};

struct AANoFreeFunction final : public AANoFreeImpl {
  AANoFreeFunction(const IRPosition &IRP, Attributor &A)
      : AANoFreeImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(nofree) }
};

/// NoFree attribute deduction for a call sites.
struct AANoFreeCallSite final : AACalleeToCallSite<AANoFree, AANoFreeImpl> {
  AANoFreeCallSite(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoFree, AANoFreeImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(nofree); }
};

/// NoFree attribute for floating values.
struct AANoFreeFloating : AANoFreeImpl {
  AANoFreeFloating(const IRPosition &IRP, Attributor &A)
      : AANoFreeImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override{STATS_DECLTRACK_FLOATING_ATTR(nofree)}

  /// See Abstract Attribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    const IRPosition &IRP = getIRPosition();

    bool IsKnown;
    if (AA::hasAssumedIRAttr<Attribute::NoFree>(A, this,
                                                IRPosition::function_scope(IRP),
                                                DepClassTy::OPTIONAL, IsKnown))
      return ChangeStatus::UNCHANGED;

    Value &AssociatedValue = getIRPosition().getAssociatedValue();
    auto Pred = [&](const Use &U, bool &Follow) -> bool {
      Instruction *UserI = cast<Instruction>(U.getUser());
      if (auto *CB = dyn_cast<CallBase>(UserI)) {
        if (CB->isBundleOperand(&U))
          return false;
        if (!CB->isArgOperand(&U))
          return true;
        unsigned ArgNo = CB->getArgOperandNo(&U);

        bool IsKnown;
        return AA::hasAssumedIRAttr<Attribute::NoFree>(
            A, this, IRPosition::callsite_argument(*CB, ArgNo),
            DepClassTy::REQUIRED, IsKnown);
      }

      if (isa<GetElementPtrInst>(UserI) || isa<BitCastInst>(UserI) ||
          isa<PHINode>(UserI) || isa<SelectInst>(UserI)) {
        Follow = true;
        return true;
      }
      if (isa<StoreInst>(UserI) || isa<LoadInst>(UserI) ||
          isa<ReturnInst>(UserI))
        return true;

      // Unknown user.
      return false;
    };
    if (!A.checkForAllUses(Pred, *this, AssociatedValue))
      return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }
};

/// NoFree attribute for a call site argument.
struct AANoFreeArgument final : AANoFreeFloating {
  AANoFreeArgument(const IRPosition &IRP, Attributor &A)
      : AANoFreeFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(nofree) }
};

/// NoFree attribute for call site arguments.
struct AANoFreeCallSiteArgument final : AANoFreeFloating {
  AANoFreeCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AANoFreeFloating(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    Argument *Arg = getAssociatedArgument();
    if (!Arg)
      return indicatePessimisticFixpoint();
    const IRPosition &ArgPos = IRPosition::argument(*Arg);
    bool IsKnown;
    if (AA::hasAssumedIRAttr<Attribute::NoFree>(A, this, ArgPos,
                                                DepClassTy::REQUIRED, IsKnown))
      return ChangeStatus::UNCHANGED;
    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override{STATS_DECLTRACK_CSARG_ATTR(nofree)};
};

/// NoFree attribute for function return value.
struct AANoFreeReturned final : AANoFreeFloating {
  AANoFreeReturned(const IRPosition &IRP, Attributor &A)
      : AANoFreeFloating(IRP, A) {
    llvm_unreachable("NoFree is not applicable to function returns!");
  }

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    llvm_unreachable("NoFree is not applicable to function returns!");
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable("NoFree is not applicable to function returns!");
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}
};

/// NoFree attribute deduction for a call site return value.
struct AANoFreeCallSiteReturned final : AANoFreeFloating {
  AANoFreeCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AANoFreeFloating(IRP, A) {}

  ChangeStatus manifest(Attributor &A) override {
    return ChangeStatus::UNCHANGED;
  }
  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSRET_ATTR(nofree) }
};
} // namespace

/// ------------------------ NonNull Argument Attribute ------------------------

bool AANonNull::isImpliedByIR(Attributor &A, const IRPosition &IRP,
                              Attribute::AttrKind ImpliedAttributeKind,
                              bool IgnoreSubsumingPositions) {
  SmallVector<Attribute::AttrKind, 2> AttrKinds;
  AttrKinds.push_back(Attribute::NonNull);
  if (!NullPointerIsDefined(IRP.getAnchorScope(),
                            IRP.getAssociatedType()->getPointerAddressSpace()))
    AttrKinds.push_back(Attribute::Dereferenceable);
  if (A.hasAttr(IRP, AttrKinds, IgnoreSubsumingPositions, Attribute::NonNull))
    return true;

  DominatorTree *DT = nullptr;
  AssumptionCache *AC = nullptr;
  InformationCache &InfoCache = A.getInfoCache();
  if (const Function *Fn = IRP.getAnchorScope()) {
    if (!Fn->isDeclaration()) {
      DT = InfoCache.getAnalysisResultForFunction<DominatorTreeAnalysis>(*Fn);
      AC = InfoCache.getAnalysisResultForFunction<AssumptionAnalysis>(*Fn);
    }
  }

  SmallVector<AA::ValueAndContext> Worklist;
  if (IRP.getPositionKind() != IRP_RETURNED) {
    Worklist.push_back({IRP.getAssociatedValue(), IRP.getCtxI()});
  } else {
    bool UsedAssumedInformation = false;
    if (!A.checkForAllInstructions(
            [&](Instruction &I) {
              Worklist.push_back({*cast<ReturnInst>(I).getReturnValue(), &I});
              return true;
            },
            IRP.getAssociatedFunction(), nullptr, {Instruction::Ret},
            UsedAssumedInformation, false, /*CheckPotentiallyDead=*/true))
      return false;
  }

  if (llvm::any_of(Worklist, [&](AA::ValueAndContext VAC) {
        return !isKnownNonZero(
            VAC.getValue(),
            SimplifyQuery(A.getDataLayout(), DT, AC, VAC.getCtxI()));
      }))
    return false;

  A.manifestAttrs(IRP, {Attribute::get(IRP.getAnchorValue().getContext(),
                                       Attribute::NonNull)});
  return true;
}

namespace {
static int64_t getKnownNonNullAndDerefBytesForUse(
    Attributor &A, const AbstractAttribute &QueryingAA, Value &AssociatedValue,
    const Use *U, const Instruction *I, bool &IsNonNull, bool &TrackUse) {
  TrackUse = false;

  const Value *UseV = U->get();
  if (!UseV->getType()->isPointerTy())
    return 0;

  // We need to follow common pointer manipulation uses to the accesses they
  // feed into. We can try to be smart to avoid looking through things we do not
  // like for now, e.g., non-inbounds GEPs.
  if (isa<CastInst>(I)) {
    TrackUse = true;
    return 0;
  }

  if (isa<GetElementPtrInst>(I)) {
    TrackUse = true;
    return 0;
  }

  Type *PtrTy = UseV->getType();
  const Function *F = I->getFunction();
  bool NullPointerIsDefined =
      F ? llvm::NullPointerIsDefined(F, PtrTy->getPointerAddressSpace()) : true;
  const DataLayout &DL = A.getInfoCache().getDL();
  if (const auto *CB = dyn_cast<CallBase>(I)) {
    if (CB->isBundleOperand(U)) {
      if (RetainedKnowledge RK = getKnowledgeFromUse(
              U, {Attribute::NonNull, Attribute::Dereferenceable})) {
        IsNonNull |=
            (RK.AttrKind == Attribute::NonNull || !NullPointerIsDefined);
        return RK.ArgValue;
      }
      return 0;
    }

    if (CB->isCallee(U)) {
      IsNonNull |= !NullPointerIsDefined;
      return 0;
    }

    unsigned ArgNo = CB->getArgOperandNo(U);
    IRPosition IRP = IRPosition::callsite_argument(*CB, ArgNo);
    // As long as we only use known information there is no need to track
    // dependences here.
    bool IsKnownNonNull;
    AA::hasAssumedIRAttr<Attribute::NonNull>(A, &QueryingAA, IRP,
                                             DepClassTy::NONE, IsKnownNonNull);
    IsNonNull |= IsKnownNonNull;
    auto *DerefAA =
        A.getAAFor<AADereferenceable>(QueryingAA, IRP, DepClassTy::NONE);
    return DerefAA ? DerefAA->getKnownDereferenceableBytes() : 0;
  }

  std::optional<MemoryLocation> Loc = MemoryLocation::getOrNone(I);
  if (!Loc || Loc->Ptr != UseV || !Loc->Size.isPrecise() ||
      Loc->Size.isScalable() || I->isVolatile())
    return 0;

  int64_t Offset;
  const Value *Base =
      getMinimalBaseOfPointer(A, QueryingAA, Loc->Ptr, Offset, DL);
  if (Base && Base == &AssociatedValue) {
    int64_t DerefBytes = Loc->Size.getValue() + Offset;
    IsNonNull |= !NullPointerIsDefined;
    return std::max(int64_t(0), DerefBytes);
  }

  /// Corner case when an offset is 0.
  Base = GetPointerBaseWithConstantOffset(Loc->Ptr, Offset, DL,
                                          /*AllowNonInbounds*/ true);
  if (Base && Base == &AssociatedValue && Offset == 0) {
    int64_t DerefBytes = Loc->Size.getValue();
    IsNonNull |= !NullPointerIsDefined;
    return std::max(int64_t(0), DerefBytes);
  }

  return 0;
}

struct AANonNullImpl : AANonNull {
  AANonNullImpl(const IRPosition &IRP, Attributor &A) : AANonNull(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    Value &V = *getAssociatedValue().stripPointerCasts();
    if (isa<ConstantPointerNull>(V)) {
      indicatePessimisticFixpoint();
      return;
    }

    if (Instruction *CtxI = getCtxI())
      followUsesInMBEC(*this, A, getState(), *CtxI);
  }

  /// See followUsesInMBEC
  bool followUseInMBEC(Attributor &A, const Use *U, const Instruction *I,
                       AANonNull::StateType &State) {
    bool IsNonNull = false;
    bool TrackUse = false;
    getKnownNonNullAndDerefBytesForUse(A, *this, getAssociatedValue(), U, I,
                                       IsNonNull, TrackUse);
    State.setKnown(IsNonNull);
    return TrackUse;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "nonnull" : "may-null";
  }
};

/// NonNull attribute for a floating value.
struct AANonNullFloating : public AANonNullImpl {
  AANonNullFloating(const IRPosition &IRP, Attributor &A)
      : AANonNullImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto CheckIRP = [&](const IRPosition &IRP) {
      bool IsKnownNonNull;
      return AA::hasAssumedIRAttr<Attribute::NonNull>(
          A, *this, IRP, DepClassTy::OPTIONAL, IsKnownNonNull);
    };

    bool Stripped;
    bool UsedAssumedInformation = false;
    Value *AssociatedValue = &getAssociatedValue();
    SmallVector<AA::ValueAndContext> Values;
    if (!A.getAssumedSimplifiedValues(getIRPosition(), *this, Values,
                                      AA::AnyScope, UsedAssumedInformation))
      Stripped = false;
    else
      Stripped =
          Values.size() != 1 || Values.front().getValue() != AssociatedValue;

    if (!Stripped) {
      bool IsKnown;
      if (auto *PHI = dyn_cast<PHINode>(AssociatedValue))
        if (llvm::all_of(PHI->incoming_values(), [&](Value *Op) {
              return AA::hasAssumedIRAttr<Attribute::NonNull>(
                  A, this, IRPosition::value(*Op), DepClassTy::OPTIONAL,
                  IsKnown);
            }))
          return ChangeStatus::UNCHANGED;
      if (auto *Select = dyn_cast<SelectInst>(AssociatedValue))
        if (AA::hasAssumedIRAttr<Attribute::NonNull>(
                A, this, IRPosition::value(*Select->getFalseValue()),
                DepClassTy::OPTIONAL, IsKnown) &&
            AA::hasAssumedIRAttr<Attribute::NonNull>(
                A, this, IRPosition::value(*Select->getTrueValue()),
                DepClassTy::OPTIONAL, IsKnown))
          return ChangeStatus::UNCHANGED;

      // If we haven't stripped anything we might still be able to use a
      // different AA, but only if the IRP changes. Effectively when we
      // interpret this not as a call site value but as a floating/argument
      // value.
      const IRPosition AVIRP = IRPosition::value(*AssociatedValue);
      if (AVIRP == getIRPosition() || !CheckIRP(AVIRP))
        return indicatePessimisticFixpoint();
      return ChangeStatus::UNCHANGED;
    }

    for (const auto &VAC : Values)
      if (!CheckIRP(IRPosition::value(*VAC.getValue())))
        return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FNRET_ATTR(nonnull) }
};

/// NonNull attribute for function return value.
struct AANonNullReturned final
    : AAReturnedFromReturnedValues<AANonNull, AANonNull, AANonNull::StateType,
                                   false, AANonNull::IRAttributeKind, false> {
  AANonNullReturned(const IRPosition &IRP, Attributor &A)
      : AAReturnedFromReturnedValues<AANonNull, AANonNull, AANonNull::StateType,
                                     false, Attribute::NonNull, false>(IRP, A) {
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "nonnull" : "may-null";
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FNRET_ATTR(nonnull) }
};

/// NonNull attribute for function argument.
struct AANonNullArgument final
    : AAArgumentFromCallSiteArguments<AANonNull, AANonNullImpl> {
  AANonNullArgument(const IRPosition &IRP, Attributor &A)
      : AAArgumentFromCallSiteArguments<AANonNull, AANonNullImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(nonnull) }
};

struct AANonNullCallSiteArgument final : AANonNullFloating {
  AANonNullCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AANonNullFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSARG_ATTR(nonnull) }
};

/// NonNull attribute for a call site return position.
struct AANonNullCallSiteReturned final
    : AACalleeToCallSite<AANonNull, AANonNullImpl> {
  AANonNullCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANonNull, AANonNullImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSRET_ATTR(nonnull) }
};
} // namespace

/// ------------------------ Must-Progress Attributes --------------------------
namespace {
struct AAMustProgressImpl : public AAMustProgress {
  AAMustProgressImpl(const IRPosition &IRP, Attributor &A)
      : AAMustProgress(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::MustProgress>(
        A, nullptr, getIRPosition(), DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "mustprogress" : "may-not-progress";
  }
};

struct AAMustProgressFunction final : AAMustProgressImpl {
  AAMustProgressFunction(const IRPosition &IRP, Attributor &A)
      : AAMustProgressImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    bool IsKnown;
    if (AA::hasAssumedIRAttr<Attribute::WillReturn>(
            A, this, getIRPosition(), DepClassTy::OPTIONAL, IsKnown)) {
      if (IsKnown)
        return indicateOptimisticFixpoint();
      return ChangeStatus::UNCHANGED;
    }

    auto CheckForMustProgress = [&](AbstractCallSite ACS) {
      IRPosition IPos = IRPosition::callsite_function(*ACS.getInstruction());
      bool IsKnownMustProgress;
      return AA::hasAssumedIRAttr<Attribute::MustProgress>(
          A, this, IPos, DepClassTy::REQUIRED, IsKnownMustProgress,
          /* IgnoreSubsumingPositions */ true);
    };

    bool AllCallSitesKnown = true;
    if (!A.checkForAllCallSites(CheckForMustProgress, *this,
                                /* RequireAllCallSites */ true,
                                AllCallSitesKnown))
      return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FN_ATTR(mustprogress)
  }
};

/// MustProgress attribute deduction for a call sites.
struct AAMustProgressCallSite final : AAMustProgressImpl {
  AAMustProgressCallSite(const IRPosition &IRP, Attributor &A)
      : AAMustProgressImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    const IRPosition &FnPos = IRPosition::function(*getAnchorScope());
    bool IsKnownMustProgress;
    if (!AA::hasAssumedIRAttr<Attribute::MustProgress>(
            A, this, FnPos, DepClassTy::REQUIRED, IsKnownMustProgress))
      return indicatePessimisticFixpoint();
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CS_ATTR(mustprogress);
  }
};
} // namespace

/// ------------------------ No-Recurse Attributes ----------------------------

namespace {
struct AANoRecurseImpl : public AANoRecurse {
  AANoRecurseImpl(const IRPosition &IRP, Attributor &A) : AANoRecurse(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::NoRecurse>(
        A, nullptr, getIRPosition(), DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "norecurse" : "may-recurse";
  }
};

struct AANoRecurseFunction final : AANoRecurseImpl {
  AANoRecurseFunction(const IRPosition &IRP, Attributor &A)
      : AANoRecurseImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {

    // If all live call sites are known to be no-recurse, we are as well.
    auto CallSitePred = [&](AbstractCallSite ACS) {
      bool IsKnownNoRecurse;
      if (!AA::hasAssumedIRAttr<Attribute::NoRecurse>(
              A, this,
              IRPosition::function(*ACS.getInstruction()->getFunction()),
              DepClassTy::NONE, IsKnownNoRecurse))
        return false;
      return IsKnownNoRecurse;
    };
    bool UsedAssumedInformation = false;
    if (A.checkForAllCallSites(CallSitePred, *this, true,
                               UsedAssumedInformation)) {
      // If we know all call sites and all are known no-recurse, we are done.
      // If all known call sites, which might not be all that exist, are known
      // to be no-recurse, we are not done but we can continue to assume
      // no-recurse. If one of the call sites we have not visited will become
      // live, another update is triggered.
      if (!UsedAssumedInformation)
        indicateOptimisticFixpoint();
      return ChangeStatus::UNCHANGED;
    }

    const AAInterFnReachability *EdgeReachability =
        A.getAAFor<AAInterFnReachability>(*this, getIRPosition(),
                                          DepClassTy::REQUIRED);
    if (EdgeReachability && EdgeReachability->canReach(A, *getAnchorScope()))
      return indicatePessimisticFixpoint();
    return ChangeStatus::UNCHANGED;
  }

  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(norecurse) }
};

/// NoRecurse attribute deduction for a call sites.
struct AANoRecurseCallSite final
    : AACalleeToCallSite<AANoRecurse, AANoRecurseImpl> {
  AANoRecurseCallSite(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoRecurse, AANoRecurseImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(norecurse); }
};
} // namespace

/// ------------------------ No-Convergent Attribute --------------------------

namespace {
struct AANonConvergentImpl : public AANonConvergent {
  AANonConvergentImpl(const IRPosition &IRP, Attributor &A)
      : AANonConvergent(IRP, A) {}

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "non-convergent" : "may-be-convergent";
  }
};

struct AANonConvergentFunction final : AANonConvergentImpl {
  AANonConvergentFunction(const IRPosition &IRP, Attributor &A)
      : AANonConvergentImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // If all function calls are known to not be convergent, we are not
    // convergent.
    auto CalleeIsNotConvergent = [&](Instruction &Inst) {
      CallBase &CB = cast<CallBase>(Inst);
      auto *Callee = dyn_cast_if_present<Function>(CB.getCalledOperand());
      if (!Callee || Callee->isIntrinsic()) {
        return false;
      }
      if (Callee->isDeclaration()) {
        return !Callee->hasFnAttribute(Attribute::Convergent);
      }
      const auto *ConvergentAA = A.getAAFor<AANonConvergent>(
          *this, IRPosition::function(*Callee), DepClassTy::REQUIRED);
      return ConvergentAA && ConvergentAA->isAssumedNotConvergent();
    };

    bool UsedAssumedInformation = false;
    if (!A.checkForAllCallLikeInstructions(CalleeIsNotConvergent, *this,
                                           UsedAssumedInformation)) {
      return indicatePessimisticFixpoint();
    }
    return ChangeStatus::UNCHANGED;
  }

  ChangeStatus manifest(Attributor &A) override {
    if (isKnownNotConvergent() &&
        A.hasAttr(getIRPosition(), Attribute::Convergent)) {
      A.removeAttrs(getIRPosition(), {Attribute::Convergent});
      return ChangeStatus::CHANGED;
    }
    return ChangeStatus::UNCHANGED;
  }

  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(convergent) }
};
} // namespace

/// -------------------- Undefined-Behavior Attributes ------------------------

namespace {
struct AAUndefinedBehaviorImpl : public AAUndefinedBehavior {
  AAUndefinedBehaviorImpl(const IRPosition &IRP, Attributor &A)
      : AAUndefinedBehavior(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  // through a pointer (i.e. also branches etc.)
  ChangeStatus updateImpl(Attributor &A) override {
    const size_t UBPrevSize = KnownUBInsts.size();
    const size_t NoUBPrevSize = AssumedNoUBInsts.size();

    auto InspectMemAccessInstForUB = [&](Instruction &I) {
      // Lang ref now states volatile store is not UB, let's skip them.
      if (I.isVolatile() && I.mayWriteToMemory())
        return true;

      // Skip instructions that are already saved.
      if (AssumedNoUBInsts.count(&I) || KnownUBInsts.count(&I))
        return true;

      // If we reach here, we know we have an instruction
      // that accesses memory through a pointer operand,
      // for which getPointerOperand() should give it to us.
      Value *PtrOp =
          const_cast<Value *>(getPointerOperand(&I, /* AllowVolatile */ true));
      assert(PtrOp &&
             "Expected pointer operand of memory accessing instruction");

      // Either we stopped and the appropriate action was taken,
      // or we got back a simplified value to continue.
      std::optional<Value *> SimplifiedPtrOp =
          stopOnUndefOrAssumed(A, PtrOp, &I);
      if (!SimplifiedPtrOp || !*SimplifiedPtrOp)
        return true;
      const Value *PtrOpVal = *SimplifiedPtrOp;

      // A memory access through a pointer is considered UB
      // only if the pointer has constant null value.
      // TODO: Expand it to not only check constant values.
      if (!isa<ConstantPointerNull>(PtrOpVal)) {
        AssumedNoUBInsts.insert(&I);
        return true;
      }
      const Type *PtrTy = PtrOpVal->getType();

      // Because we only consider instructions inside functions,
      // assume that a parent function exists.
      const Function *F = I.getFunction();

      // A memory access using constant null pointer is only considered UB
      // if null pointer is _not_ defined for the target platform.
      if (llvm::NullPointerIsDefined(F, PtrTy->getPointerAddressSpace()))
        AssumedNoUBInsts.insert(&I);
      else
        KnownUBInsts.insert(&I);
      return true;
    };

    auto InspectBrInstForUB = [&](Instruction &I) {
      // A conditional branch instruction is considered UB if it has `undef`
      // condition.

      // Skip instructions that are already saved.
      if (AssumedNoUBInsts.count(&I) || KnownUBInsts.count(&I))
        return true;

      // We know we have a branch instruction.
      auto *BrInst = cast<BranchInst>(&I);

      // Unconditional branches are never considered UB.
      if (BrInst->isUnconditional())
        return true;

      // Either we stopped and the appropriate action was taken,
      // or we got back a simplified value to continue.
      std::optional<Value *> SimplifiedCond =
          stopOnUndefOrAssumed(A, BrInst->getCondition(), BrInst);
      if (!SimplifiedCond || !*SimplifiedCond)
        return true;
      AssumedNoUBInsts.insert(&I);
      return true;
    };

    auto InspectCallSiteForUB = [&](Instruction &I) {
      // Check whether a callsite always cause UB or not

      // Skip instructions that are already saved.
      if (AssumedNoUBInsts.count(&I) || KnownUBInsts.count(&I))
        return true;

      // Check nonnull and noundef argument attribute violation for each
      // callsite.
      CallBase &CB = cast<CallBase>(I);
      auto *Callee = dyn_cast_if_present<Function>(CB.getCalledOperand());
      if (!Callee)
        return true;
      for (unsigned idx = 0; idx < CB.arg_size(); idx++) {
        // If current argument is known to be simplified to null pointer and the
        // corresponding argument position is known to have nonnull attribute,
        // the argument is poison. Furthermore, if the argument is poison and
        // the position is known to have noundef attriubte, this callsite is
        // considered UB.
        if (idx >= Callee->arg_size())
          break;
        Value *ArgVal = CB.getArgOperand(idx);
        if (!ArgVal)
          continue;
        // Here, we handle three cases.
        //   (1) Not having a value means it is dead. (we can replace the value
        //       with undef)
        //   (2) Simplified to undef. The argument violate noundef attriubte.
        //   (3) Simplified to null pointer where known to be nonnull.
        //       The argument is a poison value and violate noundef attribute.
        IRPosition CalleeArgumentIRP = IRPosition::callsite_argument(CB, idx);
        bool IsKnownNoUndef;
        AA::hasAssumedIRAttr<Attribute::NoUndef>(
            A, this, CalleeArgumentIRP, DepClassTy::NONE, IsKnownNoUndef);
        if (!IsKnownNoUndef)
          continue;
        bool UsedAssumedInformation = false;
        std::optional<Value *> SimplifiedVal =
            A.getAssumedSimplified(IRPosition::value(*ArgVal), *this,
                                   UsedAssumedInformation, AA::Interprocedural);
        if (UsedAssumedInformation)
          continue;
        if (SimplifiedVal && !*SimplifiedVal)
          return true;
        if (!SimplifiedVal || isa<UndefValue>(**SimplifiedVal)) {
          KnownUBInsts.insert(&I);
          continue;
        }
        if (!ArgVal->getType()->isPointerTy() ||
            !isa<ConstantPointerNull>(**SimplifiedVal))
          continue;
        bool IsKnownNonNull;
        AA::hasAssumedIRAttr<Attribute::NonNull>(
            A, this, CalleeArgumentIRP, DepClassTy::NONE, IsKnownNonNull);
        if (IsKnownNonNull)
          KnownUBInsts.insert(&I);
      }
      return true;
    };

    auto InspectReturnInstForUB = [&](Instruction &I) {
      auto &RI = cast<ReturnInst>(I);
      // Either we stopped and the appropriate action was taken,
      // or we got back a simplified return value to continue.
      std::optional<Value *> SimplifiedRetValue =
          stopOnUndefOrAssumed(A, RI.getReturnValue(), &I);
      if (!SimplifiedRetValue || !*SimplifiedRetValue)
        return true;

      // Check if a return instruction always cause UB or not
      // Note: It is guaranteed that the returned position of the anchor
      //       scope has noundef attribute when this is called.
      //       We also ensure the return position is not "assumed dead"
      //       because the returned value was then potentially simplified to
      //       `undef` in AAReturnedValues without removing the `noundef`
      //       attribute yet.

      // When the returned position has noundef attriubte, UB occurs in the
      // following cases.
      //   (1) Returned value is known to be undef.
      //   (2) The value is known to be a null pointer and the returned
      //       position has nonnull attribute (because the returned value is
      //       poison).
      if (isa<ConstantPointerNull>(*SimplifiedRetValue)) {
        bool IsKnownNonNull;
        AA::hasAssumedIRAttr<Attribute::NonNull>(
            A, this, IRPosition::returned(*getAnchorScope()), DepClassTy::NONE,
            IsKnownNonNull);
        if (IsKnownNonNull)
          KnownUBInsts.insert(&I);
      }

      return true;
    };

    bool UsedAssumedInformation = false;
    A.checkForAllInstructions(InspectMemAccessInstForUB, *this,
                              {Instruction::Load, Instruction::Store,
                               Instruction::AtomicCmpXchg,
                               Instruction::AtomicRMW},
                              UsedAssumedInformation,
                              /* CheckBBLivenessOnly */ true);
    A.checkForAllInstructions(InspectBrInstForUB, *this, {Instruction::Br},
                              UsedAssumedInformation,
                              /* CheckBBLivenessOnly */ true);
    A.checkForAllCallLikeInstructions(InspectCallSiteForUB, *this,
                                      UsedAssumedInformation);

    // If the returned position of the anchor scope has noundef attriubte, check
    // all returned instructions.
    if (!getAnchorScope()->getReturnType()->isVoidTy()) {
      const IRPosition &ReturnIRP = IRPosition::returned(*getAnchorScope());
      if (!A.isAssumedDead(ReturnIRP, this, nullptr, UsedAssumedInformation)) {
        bool IsKnownNoUndef;
        AA::hasAssumedIRAttr<Attribute::NoUndef>(
            A, this, ReturnIRP, DepClassTy::NONE, IsKnownNoUndef);
        if (IsKnownNoUndef)
          A.checkForAllInstructions(InspectReturnInstForUB, *this,
                                    {Instruction::Ret}, UsedAssumedInformation,
                                    /* CheckBBLivenessOnly */ true);
      }
    }

    if (NoUBPrevSize != AssumedNoUBInsts.size() ||
        UBPrevSize != KnownUBInsts.size())
      return ChangeStatus::CHANGED;
    return ChangeStatus::UNCHANGED;
  }

  bool isKnownToCauseUB(Instruction *I) const override {
    return KnownUBInsts.count(I);
  }

  bool isAssumedToCauseUB(Instruction *I) const override {
    // In simple words, if an instruction is not in the assumed to _not_
    // cause UB, then it is assumed UB (that includes those
    // in the KnownUBInsts set). The rest is boilerplate
    // is to ensure that it is one of the instructions we test
    // for UB.

    switch (I->getOpcode()) {
    case Instruction::Load:
    case Instruction::Store:
    case Instruction::AtomicCmpXchg:
    case Instruction::AtomicRMW:
      return !AssumedNoUBInsts.count(I);
    case Instruction::Br: {
      auto *BrInst = cast<BranchInst>(I);
      if (BrInst->isUnconditional())
        return false;
      return !AssumedNoUBInsts.count(I);
    } break;
    default:
      return false;
    }
    return false;
  }

  ChangeStatus manifest(Attributor &A) override {
    if (KnownUBInsts.empty())
      return ChangeStatus::UNCHANGED;
    for (Instruction *I : KnownUBInsts)
      A.changeToUnreachableAfterManifest(I);
    return ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "undefined-behavior" : "no-ub";
  }

  /// Note: The correctness of this analysis depends on the fact that the
  /// following 2 sets will stop changing after some point.
  /// "Change" here means that their size changes.
  /// The size of each set is monotonically increasing
  /// (we only add items to them) and it is upper bounded by the number of
  /// instructions in the processed function (we can never save more
  /// elements in either set than this number). Hence, at some point,
  /// they will stop increasing.
  /// Consequently, at some point, both sets will have stopped
  /// changing, effectively making the analysis reach a fixpoint.

  /// Note: These 2 sets are disjoint and an instruction can be considered
  /// one of 3 things:
  /// 1) Known to cause UB (AAUndefinedBehavior could prove it) and put it in
  ///    the KnownUBInsts set.
  /// 2) Assumed to cause UB (in every updateImpl, AAUndefinedBehavior
  ///    has a reason to assume it).
  /// 3) Assumed to not cause UB. very other instruction - AAUndefinedBehavior
  ///    could not find a reason to assume or prove that it can cause UB,
  ///    hence it assumes it doesn't. We have a set for these instructions
  ///    so that we don't reprocess them in every update.
  ///    Note however that instructions in this set may cause UB.

protected:
  /// A set of all live instructions _known_ to cause UB.
  SmallPtrSet<Instruction *, 8> KnownUBInsts;

private:
  /// A set of all the (live) instructions that are assumed to _not_ cause UB.
  SmallPtrSet<Instruction *, 8> AssumedNoUBInsts;

  // Should be called on updates in which if we're processing an instruction
  // \p I that depends on a value \p V, one of the following has to happen:
  // - If the value is assumed, then stop.
  // - If the value is known but undef, then consider it UB.
  // - Otherwise, do specific processing with the simplified value.
  // We return std::nullopt in the first 2 cases to signify that an appropriate
  // action was taken and the caller should stop.
  // Otherwise, we return the simplified value that the caller should
  // use for specific processing.
  std::optional<Value *> stopOnUndefOrAssumed(Attributor &A, Value *V,
                                              Instruction *I) {
    bool UsedAssumedInformation = false;
    std::optional<Value *> SimplifiedV =
        A.getAssumedSimplified(IRPosition::value(*V), *this,
                               UsedAssumedInformation, AA::Interprocedural);
    if (!UsedAssumedInformation) {
      // Don't depend on assumed values.
      if (!SimplifiedV) {
        // If it is known (which we tested above) but it doesn't have a value,
        // then we can assume `undef` and hence the instruction is UB.
        KnownUBInsts.insert(I);
        return std::nullopt;
      }
      if (!*SimplifiedV)
        return nullptr;
      V = *SimplifiedV;
    }
    if (isa<UndefValue>(V)) {
      KnownUBInsts.insert(I);
      return std::nullopt;
    }
    return V;
  }
};

struct AAUndefinedBehaviorFunction final : AAUndefinedBehaviorImpl {
  AAUndefinedBehaviorFunction(const IRPosition &IRP, Attributor &A)
      : AAUndefinedBehaviorImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECL(UndefinedBehaviorInstruction, Instruction,
               "Number of instructions known to have UB");
    BUILD_STAT_NAME(UndefinedBehaviorInstruction, Instruction) +=
        KnownUBInsts.size();
  }
};
} // namespace

/// ------------------------ Will-Return Attributes ----------------------------

namespace {
// Helper function that checks whether a function has any cycle which we don't
// know if it is bounded or not.
// Loops with maximum trip count are considered bounded, any other cycle not.
static bool mayContainUnboundedCycle(Function &F, Attributor &A) {
  ScalarEvolution *SE =
      A.getInfoCache().getAnalysisResultForFunction<ScalarEvolutionAnalysis>(F);
  LoopInfo *LI = A.getInfoCache().getAnalysisResultForFunction<LoopAnalysis>(F);
  // If either SCEV or LoopInfo is not available for the function then we assume
  // any cycle to be unbounded cycle.
  // We use scc_iterator which uses Tarjan algorithm to find all the maximal
  // SCCs.To detect if there's a cycle, we only need to find the maximal ones.
  if (!SE || !LI) {
    for (scc_iterator<Function *> SCCI = scc_begin(&F); !SCCI.isAtEnd(); ++SCCI)
      if (SCCI.hasCycle())
        return true;
    return false;
  }

  // If there's irreducible control, the function may contain non-loop cycles.
  if (mayContainIrreducibleControl(F, LI))
    return true;

  // Any loop that does not have a max trip count is considered unbounded cycle.
  for (auto *L : LI->getLoopsInPreorder()) {
    if (!SE->getSmallConstantMaxTripCount(L))
      return true;
  }
  return false;
}

struct AAWillReturnImpl : public AAWillReturn {
  AAWillReturnImpl(const IRPosition &IRP, Attributor &A)
      : AAWillReturn(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::WillReturn>(
        A, nullptr, getIRPosition(), DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  /// Check for `mustprogress` and `readonly` as they imply `willreturn`.
  bool isImpliedByMustprogressAndReadonly(Attributor &A, bool KnownOnly) {
    if (!A.hasAttr(getIRPosition(), {Attribute::MustProgress}))
      return false;

    bool IsKnown;
    if (AA::isAssumedReadOnly(A, getIRPosition(), *this, IsKnown))
      return IsKnown || !KnownOnly;
    return false;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    if (isImpliedByMustprogressAndReadonly(A, /* KnownOnly */ false))
      return ChangeStatus::UNCHANGED;

    auto CheckForWillReturn = [&](Instruction &I) {
      IRPosition IPos = IRPosition::callsite_function(cast<CallBase>(I));
      bool IsKnown;
      if (AA::hasAssumedIRAttr<Attribute::WillReturn>(
              A, this, IPos, DepClassTy::REQUIRED, IsKnown)) {
        if (IsKnown)
          return true;
      } else {
        return false;
      }
      bool IsKnownNoRecurse;
      return AA::hasAssumedIRAttr<Attribute::NoRecurse>(
          A, this, IPos, DepClassTy::REQUIRED, IsKnownNoRecurse);
    };

    bool UsedAssumedInformation = false;
    if (!A.checkForAllCallLikeInstructions(CheckForWillReturn, *this,
                                           UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "willreturn" : "may-noreturn";
  }
};

struct AAWillReturnFunction final : AAWillReturnImpl {
  AAWillReturnFunction(const IRPosition &IRP, Attributor &A)
      : AAWillReturnImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAWillReturnImpl::initialize(A);

    Function *F = getAnchorScope();
    assert(F && "Did expect an anchor function");
    if (F->isDeclaration() || mayContainUnboundedCycle(*F, A))
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(willreturn) }
};

/// WillReturn attribute deduction for a call sites.
struct AAWillReturnCallSite final
    : AACalleeToCallSite<AAWillReturn, AAWillReturnImpl> {
  AAWillReturnCallSite(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AAWillReturn, AAWillReturnImpl>(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    if (isImpliedByMustprogressAndReadonly(A, /* KnownOnly */ false))
      return ChangeStatus::UNCHANGED;

    return AACalleeToCallSite::updateImpl(A);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(willreturn); }
};
} // namespace

/// -------------------AAIntraFnReachability Attribute--------------------------

/// All information associated with a reachability query. This boilerplate code
/// is used by both AAIntraFnReachability and AAInterFnReachability, with
/// different \p ToTy values.
template <typename ToTy> struct ReachabilityQueryInfo {
  enum class Reachable {
    No,
    Yes,
  };

  /// Start here,
  const Instruction *From = nullptr;
  /// reach this place,
  const ToTy *To = nullptr;
  /// without going through any of these instructions,
  const AA::InstExclusionSetTy *ExclusionSet = nullptr;
  /// and remember if it worked:
  Reachable Result = Reachable::No;

  /// Precomputed hash for this RQI.
  unsigned Hash = 0;

  unsigned computeHashValue() const {
    assert(Hash == 0 && "Computed hash twice!");
    using InstSetDMI = DenseMapInfo<const AA::InstExclusionSetTy *>;
    using PairDMI = DenseMapInfo<std::pair<const Instruction *, const ToTy *>>;
    return const_cast<ReachabilityQueryInfo<ToTy> *>(this)->Hash =
               detail::combineHashValue(PairDMI ::getHashValue({From, To}),
                                        InstSetDMI::getHashValue(ExclusionSet));
  }

  ReachabilityQueryInfo(const Instruction *From, const ToTy *To)
      : From(From), To(To) {}

  /// Constructor replacement to ensure unique and stable sets are used for the
  /// cache.
  ReachabilityQueryInfo(Attributor &A, const Instruction &From, const ToTy &To,
                        const AA::InstExclusionSetTy *ES, bool MakeUnique)
      : From(&From), To(&To), ExclusionSet(ES) {

    if (!ES || ES->empty()) {
      ExclusionSet = nullptr;
    } else if (MakeUnique) {
      ExclusionSet = A.getInfoCache().getOrCreateUniqueBlockExecutionSet(ES);
    }
  }

  ReachabilityQueryInfo(const ReachabilityQueryInfo &RQI)
      : From(RQI.From), To(RQI.To), ExclusionSet(RQI.ExclusionSet) {}
};

namespace llvm {
template <typename ToTy> struct DenseMapInfo<ReachabilityQueryInfo<ToTy> *> {
  using InstSetDMI = DenseMapInfo<const AA::InstExclusionSetTy *>;
  using PairDMI = DenseMapInfo<std::pair<const Instruction *, const ToTy *>>;

  static ReachabilityQueryInfo<ToTy> EmptyKey;
  static ReachabilityQueryInfo<ToTy> TombstoneKey;

  static inline ReachabilityQueryInfo<ToTy> *getEmptyKey() { return &EmptyKey; }
  static inline ReachabilityQueryInfo<ToTy> *getTombstoneKey() {
    return &TombstoneKey;
  }
  static unsigned getHashValue(const ReachabilityQueryInfo<ToTy> *RQI) {
    return RQI->Hash ? RQI->Hash : RQI->computeHashValue();
  }
  static bool isEqual(const ReachabilityQueryInfo<ToTy> *LHS,
                      const ReachabilityQueryInfo<ToTy> *RHS) {
    if (!PairDMI::isEqual({LHS->From, LHS->To}, {RHS->From, RHS->To}))
      return false;
    return InstSetDMI::isEqual(LHS->ExclusionSet, RHS->ExclusionSet);
  }
};

#define DefineKeys(ToTy)                                                       \
  template <>                                                                  \
  ReachabilityQueryInfo<ToTy>                                                  \
      DenseMapInfo<ReachabilityQueryInfo<ToTy> *>::EmptyKey =                  \
          ReachabilityQueryInfo<ToTy>(                                         \
              DenseMapInfo<const Instruction *>::getEmptyKey(),                \
              DenseMapInfo<const ToTy *>::getEmptyKey());                      \
  template <>                                                                  \
  ReachabilityQueryInfo<ToTy>                                                  \
      DenseMapInfo<ReachabilityQueryInfo<ToTy> *>::TombstoneKey =              \
          ReachabilityQueryInfo<ToTy>(                                         \
              DenseMapInfo<const Instruction *>::getTombstoneKey(),            \
              DenseMapInfo<const ToTy *>::getTombstoneKey());

DefineKeys(Instruction) DefineKeys(Function)
#undef DefineKeys

} // namespace llvm

namespace {

template <typename BaseTy, typename ToTy>
struct CachedReachabilityAA : public BaseTy {
  using RQITy = ReachabilityQueryInfo<ToTy>;

  CachedReachabilityAA(const IRPosition &IRP, Attributor &A) : BaseTy(IRP, A) {}

  /// See AbstractAttribute::isQueryAA.
  bool isQueryAA() const override { return true; }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    for (unsigned u = 0, e = QueryVector.size(); u < e; ++u) {
      RQITy *RQI = QueryVector[u];
      if (RQI->Result == RQITy::Reachable::No &&
          isReachableImpl(A, *RQI, /*IsTemporaryRQI=*/false))
        Changed = ChangeStatus::CHANGED;
    }
    return Changed;
  }

  virtual bool isReachableImpl(Attributor &A, RQITy &RQI,
                               bool IsTemporaryRQI) = 0;

  bool rememberResult(Attributor &A, typename RQITy::Reachable Result,
                      RQITy &RQI, bool UsedExclusionSet, bool IsTemporaryRQI) {
    RQI.Result = Result;

    // Remove the temporary RQI from the cache.
    if (IsTemporaryRQI)
      QueryCache.erase(&RQI);

    // Insert a plain RQI (w/o exclusion set) if that makes sense. Two options:
    // 1) If it is reachable, it doesn't matter if we have an exclusion set for
    // this query. 2) We did not use the exclusion set, potentially because
    // there is none.
    if (Result == RQITy::Reachable::Yes || !UsedExclusionSet) {
      RQITy PlainRQI(RQI.From, RQI.To);
      if (!QueryCache.count(&PlainRQI)) {
        RQITy *RQIPtr = new (A.Allocator) RQITy(RQI.From, RQI.To);
        RQIPtr->Result = Result;
        QueryVector.push_back(RQIPtr);
        QueryCache.insert(RQIPtr);
      }
    }

    // Check if we need to insert a new permanent RQI with the exclusion set.
    if (IsTemporaryRQI && Result != RQITy::Reachable::Yes && UsedExclusionSet) {
      assert((!RQI.ExclusionSet || !RQI.ExclusionSet->empty()) &&
             "Did not expect empty set!");
      RQITy *RQIPtr = new (A.Allocator)
          RQITy(A, *RQI.From, *RQI.To, RQI.ExclusionSet, true);
      assert(RQIPtr->Result == RQITy::Reachable::No && "Already reachable?");
      RQIPtr->Result = Result;
      assert(!QueryCache.count(RQIPtr));
      QueryVector.push_back(RQIPtr);
      QueryCache.insert(RQIPtr);
    }

    if (Result == RQITy::Reachable::No && IsTemporaryRQI)
      A.registerForUpdate(*this);
    return Result == RQITy::Reachable::Yes;
  }

  const std::string getAsStr(Attributor *A) const override {
    // TODO: Return the number of reachable queries.
    return "#queries(" + std::to_string(QueryVector.size()) + ")";
  }

  bool checkQueryCache(Attributor &A, RQITy &StackRQI,
                       typename RQITy::Reachable &Result) {
    if (!this->getState().isValidState()) {
      Result = RQITy::Reachable::Yes;
      return true;
    }

    // If we have an exclusion set we might be able to find our answer by
    // ignoring it first.
    if (StackRQI.ExclusionSet) {
      RQITy PlainRQI(StackRQI.From, StackRQI.To);
      auto It = QueryCache.find(&PlainRQI);
      if (It != QueryCache.end() && (*It)->Result == RQITy::Reachable::No) {
        Result = RQITy::Reachable::No;
        return true;
      }
    }

    auto It = QueryCache.find(&StackRQI);
    if (It != QueryCache.end()) {
      Result = (*It)->Result;
      return true;
    }

    // Insert a temporary for recursive queries. We will replace it with a
    // permanent entry later.
    QueryCache.insert(&StackRQI);
    return false;
  }

private:
  SmallVector<RQITy *> QueryVector;
  DenseSet<RQITy *> QueryCache;
};

struct AAIntraFnReachabilityFunction final
    : public CachedReachabilityAA<AAIntraFnReachability, Instruction> {
  using Base = CachedReachabilityAA<AAIntraFnReachability, Instruction>;
  AAIntraFnReachabilityFunction(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {
    DT = A.getInfoCache().getAnalysisResultForFunction<DominatorTreeAnalysis>(
        *IRP.getAssociatedFunction());
  }

  bool isAssumedReachable(
      Attributor &A, const Instruction &From, const Instruction &To,
      const AA::InstExclusionSetTy *ExclusionSet) const override {
    auto *NonConstThis = const_cast<AAIntraFnReachabilityFunction *>(this);
    if (&From == &To)
      return true;

    RQITy StackRQI(A, From, To, ExclusionSet, false);
    typename RQITy::Reachable Result;
    if (!NonConstThis->checkQueryCache(A, StackRQI, Result))
      return NonConstThis->isReachableImpl(A, StackRQI,
                                           /*IsTemporaryRQI=*/true);
    return Result == RQITy::Reachable::Yes;
  }

  ChangeStatus updateImpl(Attributor &A) override {
    // We only depend on liveness. DeadEdges is all we care about, check if any
    // of them changed.
    auto *LivenessAA =
        A.getAAFor<AAIsDead>(*this, getIRPosition(), DepClassTy::OPTIONAL);
    if (LivenessAA &&
        llvm::all_of(DeadEdges,
                     [&](const auto &DeadEdge) {
                       return LivenessAA->isEdgeDead(DeadEdge.first,
                                                     DeadEdge.second);
                     }) &&
        llvm::all_of(DeadBlocks, [&](const BasicBlock *BB) {
          return LivenessAA->isAssumedDead(BB);
        })) {
      return ChangeStatus::UNCHANGED;
    }
    DeadEdges.clear();
    DeadBlocks.clear();
    return Base::updateImpl(A);
  }

  bool isReachableImpl(Attributor &A, RQITy &RQI,
                       bool IsTemporaryRQI) override {
    const Instruction *Origin = RQI.From;
    bool UsedExclusionSet = false;

    auto WillReachInBlock = [&](const Instruction &From, const Instruction &To,
                                const AA::InstExclusionSetTy *ExclusionSet) {
      const Instruction *IP = &From;
      while (IP && IP != &To) {
        if (ExclusionSet && IP != Origin && ExclusionSet->count(IP)) {
          UsedExclusionSet = true;
          break;
        }
        IP = IP->getNextNode();
      }
      return IP == &To;
    };

    const BasicBlock *FromBB = RQI.From->getParent();
    const BasicBlock *ToBB = RQI.To->getParent();
    assert(FromBB->getParent() == ToBB->getParent() &&
           "Not an intra-procedural query!");

    // Check intra-block reachability, however, other reaching paths are still
    // possible.
    if (FromBB == ToBB &&
        WillReachInBlock(*RQI.From, *RQI.To, RQI.ExclusionSet))
      return rememberResult(A, RQITy::Reachable::Yes, RQI, UsedExclusionSet,
                            IsTemporaryRQI);

    // Check if reaching the ToBB block is sufficient or if even that would not
    // ensure reaching the target. In the latter case we are done.
    if (!WillReachInBlock(ToBB->front(), *RQI.To, RQI.ExclusionSet))
      return rememberResult(A, RQITy::Reachable::No, RQI, UsedExclusionSet,
                            IsTemporaryRQI);

    const Function *Fn = FromBB->getParent();
    SmallPtrSet<const BasicBlock *, 16> ExclusionBlocks;
    if (RQI.ExclusionSet)
      for (auto *I : *RQI.ExclusionSet)
        if (I->getFunction() == Fn)
          ExclusionBlocks.insert(I->getParent());

    // Check if we make it out of the FromBB block at all.
    if (ExclusionBlocks.count(FromBB) &&
        !WillReachInBlock(*RQI.From, *FromBB->getTerminator(),
                          RQI.ExclusionSet))
      return rememberResult(A, RQITy::Reachable::No, RQI, true, IsTemporaryRQI);

    auto *LivenessAA =
        A.getAAFor<AAIsDead>(*this, getIRPosition(), DepClassTy::OPTIONAL);
    if (LivenessAA && LivenessAA->isAssumedDead(ToBB)) {
      DeadBlocks.insert(ToBB);
      return rememberResult(A, RQITy::Reachable::No, RQI, UsedExclusionSet,
                            IsTemporaryRQI);
    }

    SmallPtrSet<const BasicBlock *, 16> Visited;
    SmallVector<const BasicBlock *, 16> Worklist;
    Worklist.push_back(FromBB);

    DenseSet<std::pair<const BasicBlock *, const BasicBlock *>> LocalDeadEdges;
    while (!Worklist.empty()) {
      const BasicBlock *BB = Worklist.pop_back_val();
      if (!Visited.insert(BB).second)
        continue;
      for (const BasicBlock *SuccBB : successors(BB)) {
        if (LivenessAA && LivenessAA->isEdgeDead(BB, SuccBB)) {
          LocalDeadEdges.insert({BB, SuccBB});
          continue;
        }
        // We checked before if we just need to reach the ToBB block.
        if (SuccBB == ToBB)
          return rememberResult(A, RQITy::Reachable::Yes, RQI, UsedExclusionSet,
                                IsTemporaryRQI);
        if (DT && ExclusionBlocks.empty() && DT->dominates(BB, ToBB))
          return rememberResult(A, RQITy::Reachable::Yes, RQI, UsedExclusionSet,
                                IsTemporaryRQI);

        if (ExclusionBlocks.count(SuccBB)) {
          UsedExclusionSet = true;
          continue;
        }
        Worklist.push_back(SuccBB);
      }
    }

    DeadEdges.insert(LocalDeadEdges.begin(), LocalDeadEdges.end());
    return rememberResult(A, RQITy::Reachable::No, RQI, UsedExclusionSet,
                          IsTemporaryRQI);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}

private:
  // Set of assumed dead blocks we used in the last query. If any changes we
  // update the state.
  DenseSet<const BasicBlock *> DeadBlocks;

  // Set of assumed dead edges we used in the last query. If any changes we
  // update the state.
  DenseSet<std::pair<const BasicBlock *, const BasicBlock *>> DeadEdges;

  /// The dominator tree of the function to short-circuit reasoning.
  const DominatorTree *DT = nullptr;
};
} // namespace

/// ------------------------ NoAlias Argument Attribute ------------------------

bool AANoAlias::isImpliedByIR(Attributor &A, const IRPosition &IRP,
                              Attribute::AttrKind ImpliedAttributeKind,
                              bool IgnoreSubsumingPositions) {
  assert(ImpliedAttributeKind == Attribute::NoAlias &&
         "Unexpected attribute kind");
  Value *Val = &IRP.getAssociatedValue();
  if (IRP.getPositionKind() != IRP_CALL_SITE_ARGUMENT) {
    if (isa<AllocaInst>(Val))
      return true;
  } else {
    IgnoreSubsumingPositions = true;
  }

  if (isa<UndefValue>(Val))
    return true;

  if (isa<ConstantPointerNull>(Val) &&
      !NullPointerIsDefined(IRP.getAnchorScope(),
                            Val->getType()->getPointerAddressSpace()))
    return true;

  if (A.hasAttr(IRP, {Attribute::ByVal, Attribute::NoAlias},
                IgnoreSubsumingPositions, Attribute::NoAlias))
    return true;

  return false;
}

namespace {
struct AANoAliasImpl : AANoAlias {
  AANoAliasImpl(const IRPosition &IRP, Attributor &A) : AANoAlias(IRP, A) {
    assert(getAssociatedType()->isPointerTy() &&
           "Noalias is a pointer attribute");
  }

  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "noalias" : "may-alias";
  }
};

/// NoAlias attribute for a floating value.
struct AANoAliasFloating final : AANoAliasImpl {
  AANoAliasFloating(const IRPosition &IRP, Attributor &A)
      : AANoAliasImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Implement this.
    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(noalias)
  }
};

/// NoAlias attribute for an argument.
struct AANoAliasArgument final
    : AAArgumentFromCallSiteArguments<AANoAlias, AANoAliasImpl> {
  using Base = AAArgumentFromCallSiteArguments<AANoAlias, AANoAliasImpl>;
  AANoAliasArgument(const IRPosition &IRP, Attributor &A) : Base(IRP, A) {}

  /// See AbstractAttribute::update(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // We have to make sure no-alias on the argument does not break
    // synchronization when this is a callback argument, see also [1] below.
    // If synchronization cannot be affected, we delegate to the base updateImpl
    // function, otherwise we give up for now.

    // If the function is no-sync, no-alias cannot break synchronization.
    bool IsKnownNoSycn;
    if (AA::hasAssumedIRAttr<Attribute::NoSync>(
            A, this, IRPosition::function_scope(getIRPosition()),
            DepClassTy::OPTIONAL, IsKnownNoSycn))
      return Base::updateImpl(A);

    // If the argument is read-only, no-alias cannot break synchronization.
    bool IsKnown;
    if (AA::isAssumedReadOnly(A, getIRPosition(), *this, IsKnown))
      return Base::updateImpl(A);

    // If the argument is never passed through callbacks, no-alias cannot break
    // synchronization.
    bool UsedAssumedInformation = false;
    if (A.checkForAllCallSites(
            [](AbstractCallSite ACS) { return !ACS.isCallbackCall(); }, *this,
            true, UsedAssumedInformation))
      return Base::updateImpl(A);

    // TODO: add no-alias but make sure it doesn't break synchronization by
    // introducing fake uses. See:
    // [1] Compiler Optimizations for OpenMP, J. Doerfert and H. Finkel,
    //     International Workshop on OpenMP 2018,
    //     http://compilers.cs.uni-saarland.de/people/doerfert/par_opt18.pdf

    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(noalias) }
};

struct AANoAliasCallSiteArgument final : AANoAliasImpl {
  AANoAliasCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AANoAliasImpl(IRP, A) {}

  /// Determine if the underlying value may alias with the call site argument
  /// \p OtherArgNo of \p ICS (= the underlying call site).
  bool mayAliasWithArgument(Attributor &A, AAResults *&AAR,
                            const AAMemoryBehavior &MemBehaviorAA,
                            const CallBase &CB, unsigned OtherArgNo) {
    // We do not need to worry about aliasing with the underlying IRP.
    if (this->getCalleeArgNo() == (int)OtherArgNo)
      return false;

    // If it is not a pointer or pointer vector we do not alias.
    const Value *ArgOp = CB.getArgOperand(OtherArgNo);
    if (!ArgOp->getType()->isPtrOrPtrVectorTy())
      return false;

    auto *CBArgMemBehaviorAA = A.getAAFor<AAMemoryBehavior>(
        *this, IRPosition::callsite_argument(CB, OtherArgNo), DepClassTy::NONE);

    // If the argument is readnone, there is no read-write aliasing.
    if (CBArgMemBehaviorAA && CBArgMemBehaviorAA->isAssumedReadNone()) {
      A.recordDependence(*CBArgMemBehaviorAA, *this, DepClassTy::OPTIONAL);
      return false;
    }

    // If the argument is readonly and the underlying value is readonly, there
    // is no read-write aliasing.
    bool IsReadOnly = MemBehaviorAA.isAssumedReadOnly();
    if (CBArgMemBehaviorAA && CBArgMemBehaviorAA->isAssumedReadOnly() &&
        IsReadOnly) {
      A.recordDependence(MemBehaviorAA, *this, DepClassTy::OPTIONAL);
      A.recordDependence(*CBArgMemBehaviorAA, *this, DepClassTy::OPTIONAL);
      return false;
    }

    // We have to utilize actual alias analysis queries so we need the object.
    if (!AAR)
      AAR = A.getInfoCache().getAnalysisResultForFunction<AAManager>(
          *getAnchorScope());

    // Try to rule it out at the call site.
    bool IsAliasing = !AAR || !AAR->isNoAlias(&getAssociatedValue(), ArgOp);
    LLVM_DEBUG(dbgs() << "[NoAliasCSArg] Check alias between "
                         "callsite arguments: "
                      << getAssociatedValue() << " " << *ArgOp << " => "
                      << (IsAliasing ? "" : "no-") << "alias \n");

    return IsAliasing;
  }

  bool isKnownNoAliasDueToNoAliasPreservation(
      Attributor &A, AAResults *&AAR, const AAMemoryBehavior &MemBehaviorAA) {
    // We can deduce "noalias" if the following conditions hold.
    // (i)   Associated value is assumed to be noalias in the definition.
    // (ii)  Associated value is assumed to be no-capture in all the uses
    //       possibly executed before this callsite.
    // (iii) There is no other pointer argument which could alias with the
    //       value.

    auto IsDereferenceableOrNull = [&](Value *O, const DataLayout &DL) {
      const auto *DerefAA = A.getAAFor<AADereferenceable>(
          *this, IRPosition::value(*O), DepClassTy::OPTIONAL);
      return DerefAA ? DerefAA->getAssumedDereferenceableBytes() : 0;
    };

    const IRPosition &VIRP = IRPosition::value(getAssociatedValue());
    const Function *ScopeFn = VIRP.getAnchorScope();
    // Check whether the value is captured in the scope using AANoCapture.
    // Look at CFG and check only uses possibly executed before this
    // callsite.
    auto UsePred = [&](const Use &U, bool &Follow) -> bool {
      Instruction *UserI = cast<Instruction>(U.getUser());

      // If UserI is the curr instruction and there is a single potential use of
      // the value in UserI we allow the use.
      // TODO: We should inspect the operands and allow those that cannot alias
      //       with the value.
      if (UserI == getCtxI() && UserI->getNumOperands() == 1)
        return true;

      if (ScopeFn) {
        if (auto *CB = dyn_cast<CallBase>(UserI)) {
          if (CB->isArgOperand(&U)) {

            unsigned ArgNo = CB->getArgOperandNo(&U);

            bool IsKnownNoCapture;
            if (AA::hasAssumedIRAttr<Attribute::NoCapture>(
                    A, this, IRPosition::callsite_argument(*CB, ArgNo),
                    DepClassTy::OPTIONAL, IsKnownNoCapture))
              return true;
          }
        }

        if (!AA::isPotentiallyReachable(
                A, *UserI, *getCtxI(), *this, /* ExclusionSet */ nullptr,
                [ScopeFn](const Function &Fn) { return &Fn != ScopeFn; }))
          return true;
      }

      // TODO: We should track the capturing uses in AANoCapture but the problem
      //       is CGSCC runs. For those we would need to "allow" AANoCapture for
      //       a value in the module slice.
      switch (DetermineUseCaptureKind(U, IsDereferenceableOrNull)) {
      case UseCaptureKind::NO_CAPTURE:
        return true;
      case UseCaptureKind::MAY_CAPTURE:
        LLVM_DEBUG(dbgs() << "[AANoAliasCSArg] Unknown user: " << *UserI
                          << "\n");
        return false;
      case UseCaptureKind::PASSTHROUGH:
        Follow = true;
        return true;
      }
      llvm_unreachable("unknown UseCaptureKind");
    };

    bool IsKnownNoCapture;
    const AANoCapture *NoCaptureAA = nullptr;
    bool IsAssumedNoCapture = AA::hasAssumedIRAttr<Attribute::NoCapture>(
        A, this, VIRP, DepClassTy::NONE, IsKnownNoCapture, false, &NoCaptureAA);
    if (!IsAssumedNoCapture &&
        (!NoCaptureAA || !NoCaptureAA->isAssumedNoCaptureMaybeReturned())) {
      if (!A.checkForAllUses(UsePred, *this, getAssociatedValue())) {
        LLVM_DEBUG(
            dbgs() << "[AANoAliasCSArg] " << getAssociatedValue()
                   << " cannot be noalias as it is potentially captured\n");
        return false;
      }
    }
    if (NoCaptureAA)
      A.recordDependence(*NoCaptureAA, *this, DepClassTy::OPTIONAL);

    // Check there is no other pointer argument which could alias with the
    // value passed at this call site.
    // TODO: AbstractCallSite
    const auto &CB = cast<CallBase>(getAnchorValue());
    for (unsigned OtherArgNo = 0; OtherArgNo < CB.arg_size(); OtherArgNo++)
      if (mayAliasWithArgument(A, AAR, MemBehaviorAA, CB, OtherArgNo))
        return false;

    return true;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // If the argument is readnone we are done as there are no accesses via the
    // argument.
    auto *MemBehaviorAA =
        A.getAAFor<AAMemoryBehavior>(*this, getIRPosition(), DepClassTy::NONE);
    if (MemBehaviorAA && MemBehaviorAA->isAssumedReadNone()) {
      A.recordDependence(*MemBehaviorAA, *this, DepClassTy::OPTIONAL);
      return ChangeStatus::UNCHANGED;
    }

    bool IsKnownNoAlias;
    const IRPosition &VIRP = IRPosition::value(getAssociatedValue());
    if (!AA::hasAssumedIRAttr<Attribute::NoAlias>(
            A, this, VIRP, DepClassTy::REQUIRED, IsKnownNoAlias)) {
      LLVM_DEBUG(dbgs() << "[AANoAlias] " << getAssociatedValue()
                        << " is not no-alias at the definition\n");
      return indicatePessimisticFixpoint();
    }

    AAResults *AAR = nullptr;
    if (MemBehaviorAA &&
        isKnownNoAliasDueToNoAliasPreservation(A, AAR, *MemBehaviorAA)) {
      LLVM_DEBUG(
          dbgs() << "[AANoAlias] No-Alias deduced via no-alias preservation\n");
      return ChangeStatus::UNCHANGED;
    }

    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSARG_ATTR(noalias) }
};

/// NoAlias attribute for function return value.
struct AANoAliasReturned final : AANoAliasImpl {
  AANoAliasReturned(const IRPosition &IRP, Attributor &A)
      : AANoAliasImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {

    auto CheckReturnValue = [&](Value &RV) -> bool {
      if (Constant *C = dyn_cast<Constant>(&RV))
        if (C->isNullValue() || isa<UndefValue>(C))
          return true;

      /// For now, we can only deduce noalias if we have call sites.
      /// FIXME: add more support.
      if (!isa<CallBase>(&RV))
        return false;

      const IRPosition &RVPos = IRPosition::value(RV);
      bool IsKnownNoAlias;
      if (!AA::hasAssumedIRAttr<Attribute::NoAlias>(
              A, this, RVPos, DepClassTy::REQUIRED, IsKnownNoAlias))
        return false;

      bool IsKnownNoCapture;
      const AANoCapture *NoCaptureAA = nullptr;
      bool IsAssumedNoCapture = AA::hasAssumedIRAttr<Attribute::NoCapture>(
          A, this, RVPos, DepClassTy::REQUIRED, IsKnownNoCapture, false,
          &NoCaptureAA);
      return IsAssumedNoCapture ||
             (NoCaptureAA && NoCaptureAA->isAssumedNoCaptureMaybeReturned());
    };

    if (!A.checkForAllReturnedValues(CheckReturnValue, *this))
      return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FNRET_ATTR(noalias) }
};

/// NoAlias attribute deduction for a call site return value.
struct AANoAliasCallSiteReturned final
    : AACalleeToCallSite<AANoAlias, AANoAliasImpl> {
  AANoAliasCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoAlias, AANoAliasImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSRET_ATTR(noalias); }
};
} // namespace

/// -------------------AAIsDead Function Attribute-----------------------

namespace {
struct AAIsDeadValueImpl : public AAIsDead {
  AAIsDeadValueImpl(const IRPosition &IRP, Attributor &A) : AAIsDead(IRP, A) {}

  /// See AAIsDead::isAssumedDead().
  bool isAssumedDead() const override { return isAssumed(IS_DEAD); }

  /// See AAIsDead::isKnownDead().
  bool isKnownDead() const override { return isKnown(IS_DEAD); }

  /// See AAIsDead::isAssumedDead(BasicBlock *).
  bool isAssumedDead(const BasicBlock *BB) const override { return false; }

  /// See AAIsDead::isKnownDead(BasicBlock *).
  bool isKnownDead(const BasicBlock *BB) const override { return false; }

  /// See AAIsDead::isAssumedDead(Instruction *I).
  bool isAssumedDead(const Instruction *I) const override {
    return I == getCtxI() && isAssumedDead();
  }

  /// See AAIsDead::isKnownDead(Instruction *I).
  bool isKnownDead(const Instruction *I) const override {
    return isAssumedDead(I) && isKnownDead();
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return isAssumedDead() ? "assumed-dead" : "assumed-live";
  }

  /// Check if all uses are assumed dead.
  bool areAllUsesAssumedDead(Attributor &A, Value &V) {
    // Callers might not check the type, void has no uses.
    if (V.getType()->isVoidTy() || V.use_empty())
      return true;

    // If we replace a value with a constant there are no uses left afterwards.
    if (!isa<Constant>(V)) {
      if (auto *I = dyn_cast<Instruction>(&V))
        if (!A.isRunOn(*I->getFunction()))
          return false;
      bool UsedAssumedInformation = false;
      std::optional<Constant *> C =
          A.getAssumedConstant(V, *this, UsedAssumedInformation);
      if (!C || *C)
        return true;
    }

    auto UsePred = [&](const Use &U, bool &Follow) { return false; };
    // Explicitly set the dependence class to required because we want a long
    // chain of N dependent instructions to be considered live as soon as one is
    // without going through N update cycles. This is not required for
    // correctness.
    return A.checkForAllUses(UsePred, *this, V, /* CheckBBLivenessOnly */ false,
                             DepClassTy::REQUIRED,
                             /* IgnoreDroppableUses */ false);
  }

  /// Determine if \p I is assumed to be side-effect free.
  bool isAssumedSideEffectFree(Attributor &A, Instruction *I) {
    if (!I || wouldInstructionBeTriviallyDead(I))
      return true;

    auto *CB = dyn_cast<CallBase>(I);
    if (!CB || isa<IntrinsicInst>(CB))
      return false;

    const IRPosition &CallIRP = IRPosition::callsite_function(*CB);

    bool IsKnownNoUnwind;
    if (!AA::hasAssumedIRAttr<Attribute::NoUnwind>(
            A, this, CallIRP, DepClassTy::OPTIONAL, IsKnownNoUnwind))
      return false;

    bool IsKnown;
    return AA::isAssumedReadOnly(A, CallIRP, *this, IsKnown);
  }
};

struct AAIsDeadFloating : public AAIsDeadValueImpl {
  AAIsDeadFloating(const IRPosition &IRP, Attributor &A)
      : AAIsDeadValueImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAIsDeadValueImpl::initialize(A);

    if (isa<UndefValue>(getAssociatedValue())) {
      indicatePessimisticFixpoint();
      return;
    }

    Instruction *I = dyn_cast<Instruction>(&getAssociatedValue());
    if (!isAssumedSideEffectFree(A, I)) {
      if (!isa_and_nonnull<StoreInst>(I) && !isa_and_nonnull<FenceInst>(I))
        indicatePessimisticFixpoint();
      else
        removeAssumedBits(HAS_NO_EFFECT);
    }
  }

  bool isDeadFence(Attributor &A, FenceInst &FI) {
    const auto *ExecDomainAA = A.lookupAAFor<AAExecutionDomain>(
        IRPosition::function(*FI.getFunction()), *this, DepClassTy::NONE);
    if (!ExecDomainAA || !ExecDomainAA->isNoOpFence(FI))
      return false;
    A.recordDependence(*ExecDomainAA, *this, DepClassTy::OPTIONAL);
    return true;
  }

  bool isDeadStore(Attributor &A, StoreInst &SI,
                   SmallSetVector<Instruction *, 8> *AssumeOnlyInst = nullptr) {
    // Lang ref now states volatile store is not UB/dead, let's skip them.
    if (SI.isVolatile())
      return false;

    // If we are collecting assumes to be deleted we are in the manifest stage.
    // It's problematic to collect the potential copies again now so we use the
    // cached ones.
    bool UsedAssumedInformation = false;
    if (!AssumeOnlyInst) {
      PotentialCopies.clear();
      if (!AA::getPotentialCopiesOfStoredValue(A, SI, PotentialCopies, *this,
                                               UsedAssumedInformation)) {
        LLVM_DEBUG(
            dbgs()
            << "[AAIsDead] Could not determine potential copies of store!\n");
        return false;
      }
    }
    LLVM_DEBUG(dbgs() << "[AAIsDead] Store has " << PotentialCopies.size()
                      << " potential copies.\n");

    InformationCache &InfoCache = A.getInfoCache();
    return llvm::all_of(PotentialCopies, [&](Value *V) {
      if (A.isAssumedDead(IRPosition::value(*V), this, nullptr,
                          UsedAssumedInformation))
        return true;
      if (auto *LI = dyn_cast<LoadInst>(V)) {
        if (llvm::all_of(LI->uses(), [&](const Use &U) {
              auto &UserI = cast<Instruction>(*U.getUser());
              if (InfoCache.isOnlyUsedByAssume(UserI)) {
                if (AssumeOnlyInst)
                  AssumeOnlyInst->insert(&UserI);
                return true;
              }
              return A.isAssumedDead(U, this, nullptr, UsedAssumedInformation);
            })) {
          return true;
        }
      }
      LLVM_DEBUG(dbgs() << "[AAIsDead] Potential copy " << *V
                        << " is assumed live!\n");
      return false;
    });
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    Instruction *I = dyn_cast<Instruction>(&getAssociatedValue());
    if (isa_and_nonnull<StoreInst>(I))
      if (isValidState())
        return "assumed-dead-store";
    if (isa_and_nonnull<FenceInst>(I))
      if (isValidState())
        return "assumed-dead-fence";
    return AAIsDeadValueImpl::getAsStr(A);
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    Instruction *I = dyn_cast<Instruction>(&getAssociatedValue());
    if (auto *SI = dyn_cast_or_null<StoreInst>(I)) {
      if (!isDeadStore(A, *SI))
        return indicatePessimisticFixpoint();
    } else if (auto *FI = dyn_cast_or_null<FenceInst>(I)) {
      if (!isDeadFence(A, *FI))
        return indicatePessimisticFixpoint();
    } else {
      if (!isAssumedSideEffectFree(A, I))
        return indicatePessimisticFixpoint();
      if (!areAllUsesAssumedDead(A, getAssociatedValue()))
        return indicatePessimisticFixpoint();
    }
    return ChangeStatus::UNCHANGED;
  }

  bool isRemovableStore() const override {
    return isAssumed(IS_REMOVABLE) && isa<StoreInst>(&getAssociatedValue());
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    Value &V = getAssociatedValue();
    if (auto *I = dyn_cast<Instruction>(&V)) {
      // If we get here we basically know the users are all dead. We check if
      // isAssumedSideEffectFree returns true here again because it might not be
      // the case and only the users are dead but the instruction (=call) is
      // still needed.
      if (auto *SI = dyn_cast<StoreInst>(I)) {
        SmallSetVector<Instruction *, 8> AssumeOnlyInst;
        bool IsDead = isDeadStore(A, *SI, &AssumeOnlyInst);
        (void)IsDead;
        assert(IsDead && "Store was assumed to be dead!");
        A.deleteAfterManifest(*I);
        for (size_t i = 0; i < AssumeOnlyInst.size(); ++i) {
          Instruction *AOI = AssumeOnlyInst[i];
          for (auto *Usr : AOI->users())
            AssumeOnlyInst.insert(cast<Instruction>(Usr));
          A.deleteAfterManifest(*AOI);
        }
        return ChangeStatus::CHANGED;
      }
      if (auto *FI = dyn_cast<FenceInst>(I)) {
        assert(isDeadFence(A, *FI));
        A.deleteAfterManifest(*FI);
        return ChangeStatus::CHANGED;
      }
      if (isAssumedSideEffectFree(A, I) && !isa<InvokeInst>(I)) {
        A.deleteAfterManifest(*I);
        return ChangeStatus::CHANGED;
      }
    }
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(IsDead)
  }

private:
  // The potential copies of a dead store, used for deletion during manifest.
  SmallSetVector<Value *, 4> PotentialCopies;
};

struct AAIsDeadArgument : public AAIsDeadFloating {
  AAIsDeadArgument(const IRPosition &IRP, Attributor &A)
      : AAIsDeadFloating(IRP, A) {}

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    Argument &Arg = *getAssociatedArgument();
    if (A.isValidFunctionSignatureRewrite(Arg, /* ReplacementTypes */ {}))
      if (A.registerFunctionSignatureRewrite(
              Arg, /* ReplacementTypes */ {},
              Attributor::ArgumentReplacementInfo::CalleeRepairCBTy{},
              Attributor::ArgumentReplacementInfo::ACSRepairCBTy{})) {
        return ChangeStatus::CHANGED;
      }
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(IsDead) }
};

struct AAIsDeadCallSiteArgument : public AAIsDeadValueImpl {
  AAIsDeadCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAIsDeadValueImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAIsDeadValueImpl::initialize(A);
    if (isa<UndefValue>(getAssociatedValue()))
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    Argument *Arg = getAssociatedArgument();
    if (!Arg)
      return indicatePessimisticFixpoint();
    const IRPosition &ArgPos = IRPosition::argument(*Arg);
    auto *ArgAA = A.getAAFor<AAIsDead>(*this, ArgPos, DepClassTy::REQUIRED);
    if (!ArgAA)
      return indicatePessimisticFixpoint();
    return clampStateAndIndicateChange(getState(), ArgAA->getState());
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    CallBase &CB = cast<CallBase>(getAnchorValue());
    Use &U = CB.getArgOperandUse(getCallSiteArgNo());
    assert(!isa<UndefValue>(U.get()) &&
           "Expected undef values to be filtered out!");
    UndefValue &UV = *UndefValue::get(U->getType());
    if (A.changeUseAfterManifest(U, UV))
      return ChangeStatus::CHANGED;
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSARG_ATTR(IsDead) }
};

struct AAIsDeadCallSiteReturned : public AAIsDeadFloating {
  AAIsDeadCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAIsDeadFloating(IRP, A) {}

  /// See AAIsDead::isAssumedDead().
  bool isAssumedDead() const override {
    return AAIsDeadFloating::isAssumedDead() && IsAssumedSideEffectFree;
  }

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAIsDeadFloating::initialize(A);
    if (isa<UndefValue>(getAssociatedValue())) {
      indicatePessimisticFixpoint();
      return;
    }

    // We track this separately as a secondary state.
    IsAssumedSideEffectFree = isAssumedSideEffectFree(A, getCtxI());
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    if (IsAssumedSideEffectFree && !isAssumedSideEffectFree(A, getCtxI())) {
      IsAssumedSideEffectFree = false;
      Changed = ChangeStatus::CHANGED;
    }
    if (!areAllUsesAssumedDead(A, getAssociatedValue()))
      return indicatePessimisticFixpoint();
    return Changed;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (IsAssumedSideEffectFree)
      STATS_DECLTRACK_CSRET_ATTR(IsDead)
    else
      STATS_DECLTRACK_CSRET_ATTR(UnusedResult)
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return isAssumedDead()
               ? "assumed-dead"
               : (getAssumed() ? "assumed-dead-users" : "assumed-live");
  }

private:
  bool IsAssumedSideEffectFree = true;
};

struct AAIsDeadReturned : public AAIsDeadValueImpl {
  AAIsDeadReturned(const IRPosition &IRP, Attributor &A)
      : AAIsDeadValueImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {

    bool UsedAssumedInformation = false;
    A.checkForAllInstructions([](Instruction &) { return true; }, *this,
                              {Instruction::Ret}, UsedAssumedInformation);

    auto PredForCallSite = [&](AbstractCallSite ACS) {
      if (ACS.isCallbackCall() || !ACS.getInstruction())
        return false;
      return areAllUsesAssumedDead(A, *ACS.getInstruction());
    };

    if (!A.checkForAllCallSites(PredForCallSite, *this, true,
                                UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // TODO: Rewrite the signature to return void?
    bool AnyChange = false;
    UndefValue &UV = *UndefValue::get(getAssociatedFunction()->getReturnType());
    auto RetInstPred = [&](Instruction &I) {
      ReturnInst &RI = cast<ReturnInst>(I);
      if (!isa<UndefValue>(RI.getReturnValue()))
        AnyChange |= A.changeUseAfterManifest(RI.getOperandUse(0), UV);
      return true;
    };
    bool UsedAssumedInformation = false;
    A.checkForAllInstructions(RetInstPred, *this, {Instruction::Ret},
                              UsedAssumedInformation);
    return AnyChange ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FNRET_ATTR(IsDead) }
};

struct AAIsDeadFunction : public AAIsDead {
  AAIsDeadFunction(const IRPosition &IRP, Attributor &A) : AAIsDead(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    Function *F = getAnchorScope();
    assert(F && "Did expect an anchor function");
    if (!isAssumedDeadInternalFunction(A)) {
      ToBeExploredFrom.insert(&F->getEntryBlock().front());
      assumeLive(A, F->getEntryBlock());
    }
  }

  bool isAssumedDeadInternalFunction(Attributor &A) {
    if (!getAnchorScope()->hasLocalLinkage())
      return false;
    bool UsedAssumedInformation = false;
    return A.checkForAllCallSites([](AbstractCallSite) { return false; }, *this,
                                  true, UsedAssumedInformation);
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return "Live[#BB " + std::to_string(AssumedLiveBlocks.size()) + "/" +
           std::to_string(getAnchorScope()->size()) + "][#TBEP " +
           std::to_string(ToBeExploredFrom.size()) + "][#KDE " +
           std::to_string(KnownDeadEnds.size()) + "]";
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    assert(getState().isValidState() &&
           "Attempted to manifest an invalid state!");

    ChangeStatus HasChanged = ChangeStatus::UNCHANGED;
    Function &F = *getAnchorScope();

    if (AssumedLiveBlocks.empty()) {
      A.deleteAfterManifest(F);
      return ChangeStatus::CHANGED;
    }

    // Flag to determine if we can change an invoke to a call assuming the
    // callee is nounwind. This is not possible if the personality of the
    // function allows to catch asynchronous exceptions.
    bool Invoke2CallAllowed = !mayCatchAsynchronousExceptions(F);

    KnownDeadEnds.set_union(ToBeExploredFrom);
    for (const Instruction *DeadEndI : KnownDeadEnds) {
      auto *CB = dyn_cast<CallBase>(DeadEndI);
      if (!CB)
        continue;
      bool IsKnownNoReturn;
      bool MayReturn = !AA::hasAssumedIRAttr<Attribute::NoReturn>(
          A, this, IRPosition::callsite_function(*CB), DepClassTy::OPTIONAL,
          IsKnownNoReturn);
      if (MayReturn && (!Invoke2CallAllowed || !isa<InvokeInst>(CB)))
        continue;

      if (auto *II = dyn_cast<InvokeInst>(DeadEndI))
        A.registerInvokeWithDeadSuccessor(const_cast<InvokeInst &>(*II));
      else
        A.changeToUnreachableAfterManifest(
            const_cast<Instruction *>(DeadEndI->getNextNode()));
      HasChanged = ChangeStatus::CHANGED;
    }

    STATS_DECL(AAIsDead, BasicBlock, "Number of dead basic blocks deleted.");
    for (BasicBlock &BB : F)
      if (!AssumedLiveBlocks.count(&BB)) {
        A.deleteAfterManifest(BB);
        ++BUILD_STAT_NAME(AAIsDead, BasicBlock);
        HasChanged = ChangeStatus::CHANGED;
      }

    return HasChanged;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override;

  bool isEdgeDead(const BasicBlock *From, const BasicBlock *To) const override {
    assert(From->getParent() == getAnchorScope() &&
           To->getParent() == getAnchorScope() &&
           "Used AAIsDead of the wrong function");
    return isValidState() && !AssumedLiveEdges.count(std::make_pair(From, To));
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}

  /// Returns true if the function is assumed dead.
  bool isAssumedDead() const override { return false; }

  /// See AAIsDead::isKnownDead().
  bool isKnownDead() const override { return false; }

  /// See AAIsDead::isAssumedDead(BasicBlock *).
  bool isAssumedDead(const BasicBlock *BB) const override {
    assert(BB->getParent() == getAnchorScope() &&
           "BB must be in the same anchor scope function.");

    if (!getAssumed())
      return false;
    return !AssumedLiveBlocks.count(BB);
  }

  /// See AAIsDead::isKnownDead(BasicBlock *).
  bool isKnownDead(const BasicBlock *BB) const override {
    return getKnown() && isAssumedDead(BB);
  }

  /// See AAIsDead::isAssumed(Instruction *I).
  bool isAssumedDead(const Instruction *I) const override {
    assert(I->getParent()->getParent() == getAnchorScope() &&
           "Instruction must be in the same anchor scope function.");

    if (!getAssumed())
      return false;

    // If it is not in AssumedLiveBlocks then it for sure dead.
    // Otherwise, it can still be after noreturn call in a live block.
    if (!AssumedLiveBlocks.count(I->getParent()))
      return true;

    // If it is not after a liveness barrier it is live.
    const Instruction *PrevI = I->getPrevNode();
    while (PrevI) {
      if (KnownDeadEnds.count(PrevI) || ToBeExploredFrom.count(PrevI))
        return true;
      PrevI = PrevI->getPrevNode();
    }
    return false;
  }

  /// See AAIsDead::isKnownDead(Instruction *I).
  bool isKnownDead(const Instruction *I) const override {
    return getKnown() && isAssumedDead(I);
  }

  /// Assume \p BB is (partially) live now and indicate to the Attributor \p A
  /// that internal function called from \p BB should now be looked at.
  bool assumeLive(Attributor &A, const BasicBlock &BB) {
    if (!AssumedLiveBlocks.insert(&BB).second)
      return false;

    // We assume that all of BB is (probably) live now and if there are calls to
    // internal functions we will assume that those are now live as well. This
    // is a performance optimization for blocks with calls to a lot of internal
    // functions. It can however cause dead functions to be treated as live.
    for (const Instruction &I : BB)
      if (const auto *CB = dyn_cast<CallBase>(&I))
        if (auto *F = dyn_cast_if_present<Function>(CB->getCalledOperand()))
          if (F->hasLocalLinkage())
            A.markLiveInternalFunction(*F);
    return true;
  }

  /// Collection of instructions that need to be explored again, e.g., we
  /// did assume they do not transfer control to (one of their) successors.
  SmallSetVector<const Instruction *, 8> ToBeExploredFrom;

  /// Collection of instructions that are known to not transfer control.
  SmallSetVector<const Instruction *, 8> KnownDeadEnds;

  /// Collection of all assumed live edges
  DenseSet<std::pair<const BasicBlock *, const BasicBlock *>> AssumedLiveEdges;

  /// Collection of all assumed live BasicBlocks.
  DenseSet<const BasicBlock *> AssumedLiveBlocks;
};

static bool
identifyAliveSuccessors(Attributor &A, const CallBase &CB,
                        AbstractAttribute &AA,
                        SmallVectorImpl<const Instruction *> &AliveSuccessors) {
  const IRPosition &IPos = IRPosition::callsite_function(CB);

  bool IsKnownNoReturn;
  if (AA::hasAssumedIRAttr<Attribute::NoReturn>(
          A, &AA, IPos, DepClassTy::OPTIONAL, IsKnownNoReturn))
    return !IsKnownNoReturn;
  if (CB.isTerminator())
    AliveSuccessors.push_back(&CB.getSuccessor(0)->front());
  else
    AliveSuccessors.push_back(CB.getNextNode());
  return false;
}

static bool
identifyAliveSuccessors(Attributor &A, const InvokeInst &II,
                        AbstractAttribute &AA,
                        SmallVectorImpl<const Instruction *> &AliveSuccessors) {
  bool UsedAssumedInformation =
      identifyAliveSuccessors(A, cast<CallBase>(II), AA, AliveSuccessors);

  // First, determine if we can change an invoke to a call assuming the
  // callee is nounwind. This is not possible if the personality of the
  // function allows to catch asynchronous exceptions.
  if (AAIsDeadFunction::mayCatchAsynchronousExceptions(*II.getFunction())) {
    AliveSuccessors.push_back(&II.getUnwindDest()->front());
  } else {
    const IRPosition &IPos = IRPosition::callsite_function(II);

    bool IsKnownNoUnwind;
    if (AA::hasAssumedIRAttr<Attribute::NoUnwind>(
            A, &AA, IPos, DepClassTy::OPTIONAL, IsKnownNoUnwind)) {
      UsedAssumedInformation |= !IsKnownNoUnwind;
    } else {
      AliveSuccessors.push_back(&II.getUnwindDest()->front());
    }
  }
  return UsedAssumedInformation;
}

static bool
identifyAliveSuccessors(Attributor &A, const BranchInst &BI,
                        AbstractAttribute &AA,
                        SmallVectorImpl<const Instruction *> &AliveSuccessors) {
  bool UsedAssumedInformation = false;
  if (BI.getNumSuccessors() == 1) {
    AliveSuccessors.push_back(&BI.getSuccessor(0)->front());
  } else {
    std::optional<Constant *> C =
        A.getAssumedConstant(*BI.getCondition(), AA, UsedAssumedInformation);
    if (!C || isa_and_nonnull<UndefValue>(*C)) {
      // No value yet, assume both edges are dead.
    } else if (isa_and_nonnull<ConstantInt>(*C)) {
      const BasicBlock *SuccBB =
          BI.getSuccessor(1 - cast<ConstantInt>(*C)->getValue().getZExtValue());
      AliveSuccessors.push_back(&SuccBB->front());
    } else {
      AliveSuccessors.push_back(&BI.getSuccessor(0)->front());
      AliveSuccessors.push_back(&BI.getSuccessor(1)->front());
      UsedAssumedInformation = false;
    }
  }
  return UsedAssumedInformation;
}

static bool
identifyAliveSuccessors(Attributor &A, const SwitchInst &SI,
                        AbstractAttribute &AA,
                        SmallVectorImpl<const Instruction *> &AliveSuccessors) {
  bool UsedAssumedInformation = false;
  SmallVector<AA::ValueAndContext> Values;
  if (!A.getAssumedSimplifiedValues(IRPosition::value(*SI.getCondition()), &AA,
                                    Values, AA::AnyScope,
                                    UsedAssumedInformation)) {
    // Something went wrong, assume all successors are live.
    for (const BasicBlock *SuccBB : successors(SI.getParent()))
      AliveSuccessors.push_back(&SuccBB->front());
    return false;
  }

  if (Values.empty() ||
      (Values.size() == 1 &&
       isa_and_nonnull<UndefValue>(Values.front().getValue()))) {
    // No valid value yet, assume all edges are dead.
    return UsedAssumedInformation;
  }

  Type &Ty = *SI.getCondition()->getType();
  SmallPtrSet<ConstantInt *, 8> Constants;
  auto CheckForConstantInt = [&](Value *V) {
    if (auto *CI = dyn_cast_if_present<ConstantInt>(AA::getWithType(*V, Ty))) {
      Constants.insert(CI);
      return true;
    }
    return false;
  };

  if (!all_of(Values, [&](AA::ValueAndContext &VAC) {
        return CheckForConstantInt(VAC.getValue());
      })) {
    for (const BasicBlock *SuccBB : successors(SI.getParent()))
      AliveSuccessors.push_back(&SuccBB->front());
    return UsedAssumedInformation;
  }

  unsigned MatchedCases = 0;
  for (const auto &CaseIt : SI.cases()) {
    if (Constants.count(CaseIt.getCaseValue())) {
      ++MatchedCases;
      AliveSuccessors.push_back(&CaseIt.getCaseSuccessor()->front());
    }
  }

  // If all potential values have been matched, we will not visit the default
  // case.
  if (MatchedCases < Constants.size())
    AliveSuccessors.push_back(&SI.getDefaultDest()->front());
  return UsedAssumedInformation;
}

ChangeStatus AAIsDeadFunction::updateImpl(Attributor &A) {
  ChangeStatus Change = ChangeStatus::UNCHANGED;

  if (AssumedLiveBlocks.empty()) {
    if (isAssumedDeadInternalFunction(A))
      return ChangeStatus::UNCHANGED;

    Function *F = getAnchorScope();
    ToBeExploredFrom.insert(&F->getEntryBlock().front());
    assumeLive(A, F->getEntryBlock());
    Change = ChangeStatus::CHANGED;
  }

  LLVM_DEBUG(dbgs() << "[AAIsDead] Live [" << AssumedLiveBlocks.size() << "/"
                    << getAnchorScope()->size() << "] BBs and "
                    << ToBeExploredFrom.size() << " exploration points and "
                    << KnownDeadEnds.size() << " known dead ends\n");

  // Copy and clear the list of instructions we need to explore from. It is
  // refilled with instructions the next update has to look at.
  SmallVector<const Instruction *, 8> Worklist(ToBeExploredFrom.begin(),
                                               ToBeExploredFrom.end());
  decltype(ToBeExploredFrom) NewToBeExploredFrom;

  SmallVector<const Instruction *, 8> AliveSuccessors;
  while (!Worklist.empty()) {
    const Instruction *I = Worklist.pop_back_val();
    LLVM_DEBUG(dbgs() << "[AAIsDead] Exploration inst: " << *I << "\n");

    // Fast forward for uninteresting instructions. We could look for UB here
    // though.
    while (!I->isTerminator() && !isa<CallBase>(I))
      I = I->getNextNode();

    AliveSuccessors.clear();

    bool UsedAssumedInformation = false;
    switch (I->getOpcode()) {
    // TODO: look for (assumed) UB to backwards propagate "deadness".
    default:
      assert(I->isTerminator() &&
             "Expected non-terminators to be handled already!");
      for (const BasicBlock *SuccBB : successors(I->getParent()))
        AliveSuccessors.push_back(&SuccBB->front());
      break;
    case Instruction::Call:
      UsedAssumedInformation = identifyAliveSuccessors(A, cast<CallInst>(*I),
                                                       *this, AliveSuccessors);
      break;
    case Instruction::Invoke:
      UsedAssumedInformation = identifyAliveSuccessors(A, cast<InvokeInst>(*I),
                                                       *this, AliveSuccessors);
      break;
    case Instruction::Br:
      UsedAssumedInformation = identifyAliveSuccessors(A, cast<BranchInst>(*I),
                                                       *this, AliveSuccessors);
      break;
    case Instruction::Switch:
      UsedAssumedInformation = identifyAliveSuccessors(A, cast<SwitchInst>(*I),
                                                       *this, AliveSuccessors);
      break;
    }

    if (UsedAssumedInformation) {
      NewToBeExploredFrom.insert(I);
    } else if (AliveSuccessors.empty() ||
               (I->isTerminator() &&
                AliveSuccessors.size() < I->getNumSuccessors())) {
      if (KnownDeadEnds.insert(I))
        Change = ChangeStatus::CHANGED;
    }

    LLVM_DEBUG(dbgs() << "[AAIsDead] #AliveSuccessors: "
                      << AliveSuccessors.size() << " UsedAssumedInformation: "
                      << UsedAssumedInformation << "\n");

    for (const Instruction *AliveSuccessor : AliveSuccessors) {
      if (!I->isTerminator()) {
        assert(AliveSuccessors.size() == 1 &&
               "Non-terminator expected to have a single successor!");
        Worklist.push_back(AliveSuccessor);
      } else {
        // record the assumed live edge
        auto Edge = std::make_pair(I->getParent(), AliveSuccessor->getParent());
        if (AssumedLiveEdges.insert(Edge).second)
          Change = ChangeStatus::CHANGED;
        if (assumeLive(A, *AliveSuccessor->getParent()))
          Worklist.push_back(AliveSuccessor);
      }
    }
  }

  // Check if the content of ToBeExploredFrom changed, ignore the order.
  if (NewToBeExploredFrom.size() != ToBeExploredFrom.size() ||
      llvm::any_of(NewToBeExploredFrom, [&](const Instruction *I) {
        return !ToBeExploredFrom.count(I);
      })) {
    Change = ChangeStatus::CHANGED;
    ToBeExploredFrom = std::move(NewToBeExploredFrom);
  }

  // If we know everything is live there is no need to query for liveness.
  // Instead, indicating a pessimistic fixpoint will cause the state to be
  // "invalid" and all queries to be answered conservatively without lookups.
  // To be in this state we have to (1) finished the exploration and (3) not
  // discovered any non-trivial dead end and (2) not ruled unreachable code
  // dead.
  if (ToBeExploredFrom.empty() &&
      getAnchorScope()->size() == AssumedLiveBlocks.size() &&
      llvm::all_of(KnownDeadEnds, [](const Instruction *DeadEndI) {
        return DeadEndI->isTerminator() && DeadEndI->getNumSuccessors() == 0;
      }))
    return indicatePessimisticFixpoint();
  return Change;
}

/// Liveness information for a call sites.
struct AAIsDeadCallSite final : AAIsDeadFunction {
  AAIsDeadCallSite(const IRPosition &IRP, Attributor &A)
      : AAIsDeadFunction(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites instead of
    //       redirecting requests to the callee.
    llvm_unreachable("Abstract attributes for liveness are not "
                     "supported for call sites yet!");
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}
};
} // namespace

/// -------------------- Dereferenceable Argument Attribute --------------------

namespace {
struct AADereferenceableImpl : AADereferenceable {
  AADereferenceableImpl(const IRPosition &IRP, Attributor &A)
      : AADereferenceable(IRP, A) {}
  using StateType = DerefState;

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    Value &V = *getAssociatedValue().stripPointerCasts();
    SmallVector<Attribute, 4> Attrs;
    A.getAttrs(getIRPosition(),
               {Attribute::Dereferenceable, Attribute::DereferenceableOrNull},
               Attrs, /* IgnoreSubsumingPositions */ false);
    for (const Attribute &Attr : Attrs)
      takeKnownDerefBytesMaximum(Attr.getValueAsInt());

    // Ensure we initialize the non-null AA (if necessary).
    bool IsKnownNonNull;
    AA::hasAssumedIRAttr<Attribute::NonNull>(
        A, this, getIRPosition(), DepClassTy::OPTIONAL, IsKnownNonNull);

    bool CanBeNull, CanBeFreed;
    takeKnownDerefBytesMaximum(V.getPointerDereferenceableBytes(
        A.getDataLayout(), CanBeNull, CanBeFreed));

    if (Instruction *CtxI = getCtxI())
      followUsesInMBEC(*this, A, getState(), *CtxI);
  }

  /// See AbstractAttribute::getState()
  /// {
  StateType &getState() override { return *this; }
  const StateType &getState() const override { return *this; }
  /// }

  /// Helper function for collecting accessed bytes in must-be-executed-context
  void addAccessedBytesForUse(Attributor &A, const Use *U, const Instruction *I,
                              DerefState &State) {
    const Value *UseV = U->get();
    if (!UseV->getType()->isPointerTy())
      return;

    std::optional<MemoryLocation> Loc = MemoryLocation::getOrNone(I);
    if (!Loc || Loc->Ptr != UseV || !Loc->Size.isPrecise() || I->isVolatile())
      return;

    int64_t Offset;
    const Value *Base = GetPointerBaseWithConstantOffset(
        Loc->Ptr, Offset, A.getDataLayout(), /*AllowNonInbounds*/ true);
    if (Base && Base == &getAssociatedValue())
      State.addAccessedBytes(Offset, Loc->Size.getValue());
  }

  /// See followUsesInMBEC
  bool followUseInMBEC(Attributor &A, const Use *U, const Instruction *I,
                       AADereferenceable::StateType &State) {
    bool IsNonNull = false;
    bool TrackUse = false;
    int64_t DerefBytes = getKnownNonNullAndDerefBytesForUse(
        A, *this, getAssociatedValue(), U, I, IsNonNull, TrackUse);
    LLVM_DEBUG(dbgs() << "[AADereferenceable] Deref bytes: " << DerefBytes
                      << " for instruction " << *I << "\n");

    addAccessedBytesForUse(A, U, I, State);
    State.takeKnownDerefBytesMaximum(DerefBytes);
    return TrackUse;
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    ChangeStatus Change = AADereferenceable::manifest(A);
    bool IsKnownNonNull;
    bool IsAssumedNonNull = AA::hasAssumedIRAttr<Attribute::NonNull>(
        A, this, getIRPosition(), DepClassTy::NONE, IsKnownNonNull);
    if (IsAssumedNonNull &&
        A.hasAttr(getIRPosition(), Attribute::DereferenceableOrNull)) {
      A.removeAttrs(getIRPosition(), {Attribute::DereferenceableOrNull});
      return ChangeStatus::CHANGED;
    }
    return Change;
  }

  void getDeducedAttributes(Attributor &A, LLVMContext &Ctx,
                            SmallVectorImpl<Attribute> &Attrs) const override {
    // TODO: Add *_globally support
    bool IsKnownNonNull;
    bool IsAssumedNonNull = AA::hasAssumedIRAttr<Attribute::NonNull>(
        A, this, getIRPosition(), DepClassTy::NONE, IsKnownNonNull);
    if (IsAssumedNonNull)
      Attrs.emplace_back(Attribute::getWithDereferenceableBytes(
          Ctx, getAssumedDereferenceableBytes()));
    else
      Attrs.emplace_back(Attribute::getWithDereferenceableOrNullBytes(
          Ctx, getAssumedDereferenceableBytes()));
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    if (!getAssumedDereferenceableBytes())
      return "unknown-dereferenceable";
    bool IsKnownNonNull;
    bool IsAssumedNonNull = false;
    if (A)
      IsAssumedNonNull = AA::hasAssumedIRAttr<Attribute::NonNull>(
          *A, this, getIRPosition(), DepClassTy::NONE, IsKnownNonNull);
    return std::string("dereferenceable") +
           (IsAssumedNonNull ? "" : "_or_null") +
           (isAssumedGlobal() ? "_globally" : "") + "<" +
           std::to_string(getKnownDereferenceableBytes()) + "-" +
           std::to_string(getAssumedDereferenceableBytes()) + ">" +
           (!A ? " [non-null is unknown]" : "");
  }
};

/// Dereferenceable attribute for a floating value.
struct AADereferenceableFloating : AADereferenceableImpl {
  AADereferenceableFloating(const IRPosition &IRP, Attributor &A)
      : AADereferenceableImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    bool Stripped;
    bool UsedAssumedInformation = false;
    SmallVector<AA::ValueAndContext> Values;
    if (!A.getAssumedSimplifiedValues(getIRPosition(), *this, Values,
                                      AA::AnyScope, UsedAssumedInformation)) {
      Values.push_back({getAssociatedValue(), getCtxI()});
      Stripped = false;
    } else {
      Stripped = Values.size() != 1 ||
                 Values.front().getValue() != &getAssociatedValue();
    }

    const DataLayout &DL = A.getDataLayout();
    DerefState T;

    auto VisitValueCB = [&](const Value &V) -> bool {
      unsigned IdxWidth =
          DL.getIndexSizeInBits(V.getType()->getPointerAddressSpace());
      APInt Offset(IdxWidth, 0);
      const Value *Base = stripAndAccumulateOffsets(
          A, *this, &V, DL, Offset, /* GetMinOffset */ false,
          /* AllowNonInbounds */ true);

      const auto *AA = A.getAAFor<AADereferenceable>(
          *this, IRPosition::value(*Base), DepClassTy::REQUIRED);
      int64_t DerefBytes = 0;
      if (!AA || (!Stripped && this == AA)) {
        // Use IR information if we did not strip anything.
        // TODO: track globally.
        bool CanBeNull, CanBeFreed;
        DerefBytes =
            Base->getPointerDereferenceableBytes(DL, CanBeNull, CanBeFreed);
        T.GlobalState.indicatePessimisticFixpoint();
      } else {
        const DerefState &DS = AA->getState();
        DerefBytes = DS.DerefBytesState.getAssumed();
        T.GlobalState &= DS.GlobalState;
      }

      // For now we do not try to "increase" dereferenceability due to negative
      // indices as we first have to come up with code to deal with loops and
      // for overflows of the dereferenceable bytes.
      int64_t OffsetSExt = Offset.getSExtValue();
      if (OffsetSExt < 0)
        OffsetSExt = 0;

      T.takeAssumedDerefBytesMinimum(
          std::max(int64_t(0), DerefBytes - OffsetSExt));

      if (this == AA) {
        if (!Stripped) {
          // If nothing was stripped IR information is all we got.
          T.takeKnownDerefBytesMaximum(
              std::max(int64_t(0), DerefBytes - OffsetSExt));
          T.indicatePessimisticFixpoint();
        } else if (OffsetSExt > 0) {
          // If something was stripped but there is circular reasoning we look
          // for the offset. If it is positive we basically decrease the
          // dereferenceable bytes in a circular loop now, which will simply
          // drive them down to the known value in a very slow way which we
          // can accelerate.
          T.indicatePessimisticFixpoint();
        }
      }

      return T.isValidState();
    };

    for (const auto &VAC : Values)
      if (!VisitValueCB(*VAC.getValue()))
        return indicatePessimisticFixpoint();

    return clampStateAndIndicateChange(getState(), T);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(dereferenceable)
  }
};

/// Dereferenceable attribute for a return value.
struct AADereferenceableReturned final
    : AAReturnedFromReturnedValues<AADereferenceable, AADereferenceableImpl> {
  using Base =
      AAReturnedFromReturnedValues<AADereferenceable, AADereferenceableImpl>;
  AADereferenceableReturned(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(dereferenceable)
  }
};

/// Dereferenceable attribute for an argument
struct AADereferenceableArgument final
    : AAArgumentFromCallSiteArguments<AADereferenceable,
                                      AADereferenceableImpl> {
  using Base =
      AAArgumentFromCallSiteArguments<AADereferenceable, AADereferenceableImpl>;
  AADereferenceableArgument(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_ARG_ATTR(dereferenceable)
  }
};

/// Dereferenceable attribute for a call site argument.
struct AADereferenceableCallSiteArgument final : AADereferenceableFloating {
  AADereferenceableCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AADereferenceableFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(dereferenceable)
  }
};

/// Dereferenceable attribute deduction for a call site return value.
struct AADereferenceableCallSiteReturned final
    : AACalleeToCallSite<AADereferenceable, AADereferenceableImpl> {
  using Base = AACalleeToCallSite<AADereferenceable, AADereferenceableImpl>;
  AADereferenceableCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CS_ATTR(dereferenceable);
  }
};
} // namespace

// ------------------------ Align Argument Attribute ------------------------

namespace {
static unsigned getKnownAlignForUse(Attributor &A, AAAlign &QueryingAA,
                                    Value &AssociatedValue, const Use *U,
                                    const Instruction *I, bool &TrackUse) {
  // We need to follow common pointer manipulation uses to the accesses they
  // feed into.
  if (isa<CastInst>(I)) {
    // Follow all but ptr2int casts.
    TrackUse = !isa<PtrToIntInst>(I);
    return 0;
  }
  if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
    if (GEP->hasAllConstantIndices())
      TrackUse = true;
    return 0;
  }

  MaybeAlign MA;
  if (const auto *CB = dyn_cast<CallBase>(I)) {
    if (CB->isBundleOperand(U) || CB->isCallee(U))
      return 0;

    unsigned ArgNo = CB->getArgOperandNo(U);
    IRPosition IRP = IRPosition::callsite_argument(*CB, ArgNo);
    // As long as we only use known information there is no need to track
    // dependences here.
    auto *AlignAA = A.getAAFor<AAAlign>(QueryingAA, IRP, DepClassTy::NONE);
    if (AlignAA)
      MA = MaybeAlign(AlignAA->getKnownAlign());
  }

  const DataLayout &DL = A.getDataLayout();
  const Value *UseV = U->get();
  if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (SI->getPointerOperand() == UseV)
      MA = SI->getAlign();
  } else if (auto *LI = dyn_cast<LoadInst>(I)) {
    if (LI->getPointerOperand() == UseV)
      MA = LI->getAlign();
  } else if (auto *AI = dyn_cast<AtomicRMWInst>(I)) {
    if (AI->getPointerOperand() == UseV)
      MA = AI->getAlign();
  } else if (auto *AI = dyn_cast<AtomicCmpXchgInst>(I)) {
    if (AI->getPointerOperand() == UseV)
      MA = AI->getAlign();
  }

  if (!MA || *MA <= QueryingAA.getKnownAlign())
    return 0;

  unsigned Alignment = MA->value();
  int64_t Offset;

  if (const Value *Base = GetPointerBaseWithConstantOffset(UseV, Offset, DL)) {
    if (Base == &AssociatedValue) {
      // BasePointerAddr + Offset = Alignment * Q for some integer Q.
      // So we can say that the maximum power of two which is a divisor of
      // gcd(Offset, Alignment) is an alignment.

      uint32_t gcd = std::gcd(uint32_t(abs((int32_t)Offset)), Alignment);
      Alignment = llvm::bit_floor(gcd);
    }
  }

  return Alignment;
}

struct AAAlignImpl : AAAlign {
  AAAlignImpl(const IRPosition &IRP, Attributor &A) : AAAlign(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    SmallVector<Attribute, 4> Attrs;
    A.getAttrs(getIRPosition(), {Attribute::Alignment}, Attrs);
    for (const Attribute &Attr : Attrs)
      takeKnownMaximum(Attr.getValueAsInt());

    Value &V = *getAssociatedValue().stripPointerCasts();
    takeKnownMaximum(V.getPointerAlignment(A.getDataLayout()).value());

    if (Instruction *CtxI = getCtxI())
      followUsesInMBEC(*this, A, getState(), *CtxI);
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    ChangeStatus LoadStoreChanged = ChangeStatus::UNCHANGED;

    // Check for users that allow alignment annotations.
    Value &AssociatedValue = getAssociatedValue();
    for (const Use &U : AssociatedValue.uses()) {
      if (auto *SI = dyn_cast<StoreInst>(U.getUser())) {
        if (SI->getPointerOperand() == &AssociatedValue)
          if (SI->getAlign() < getAssumedAlign()) {
            STATS_DECLTRACK(AAAlign, Store,
                            "Number of times alignment added to a store");
            SI->setAlignment(getAssumedAlign());
            LoadStoreChanged = ChangeStatus::CHANGED;
          }
      } else if (auto *LI = dyn_cast<LoadInst>(U.getUser())) {
        if (LI->getPointerOperand() == &AssociatedValue)
          if (LI->getAlign() < getAssumedAlign()) {
            LI->setAlignment(getAssumedAlign());
            STATS_DECLTRACK(AAAlign, Load,
                            "Number of times alignment added to a load");
            LoadStoreChanged = ChangeStatus::CHANGED;
          }
      }
    }

    ChangeStatus Changed = AAAlign::manifest(A);

    Align InheritAlign =
        getAssociatedValue().getPointerAlignment(A.getDataLayout());
    if (InheritAlign >= getAssumedAlign())
      return LoadStoreChanged;
    return Changed | LoadStoreChanged;
  }

  // TODO: Provide a helper to determine the implied ABI alignment and check in
  //       the existing manifest method and a new one for AAAlignImpl that value
  //       to avoid making the alignment explicit if it did not improve.

  /// See AbstractAttribute::getDeducedAttributes
  void getDeducedAttributes(Attributor &A, LLVMContext &Ctx,
                            SmallVectorImpl<Attribute> &Attrs) const override {
    if (getAssumedAlign() > 1)
      Attrs.emplace_back(
          Attribute::getWithAlignment(Ctx, Align(getAssumedAlign())));
  }

  /// See followUsesInMBEC
  bool followUseInMBEC(Attributor &A, const Use *U, const Instruction *I,
                       AAAlign::StateType &State) {
    bool TrackUse = false;

    unsigned int KnownAlign =
        getKnownAlignForUse(A, *this, getAssociatedValue(), U, I, TrackUse);
    State.takeKnownMaximum(KnownAlign);

    return TrackUse;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return "align<" + std::to_string(getKnownAlign().value()) + "-" +
           std::to_string(getAssumedAlign().value()) + ">";
  }
};

/// Align attribute for a floating value.
struct AAAlignFloating : AAAlignImpl {
  AAAlignFloating(const IRPosition &IRP, Attributor &A) : AAAlignImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    const DataLayout &DL = A.getDataLayout();

    bool Stripped;
    bool UsedAssumedInformation = false;
    SmallVector<AA::ValueAndContext> Values;
    if (!A.getAssumedSimplifiedValues(getIRPosition(), *this, Values,
                                      AA::AnyScope, UsedAssumedInformation)) {
      Values.push_back({getAssociatedValue(), getCtxI()});
      Stripped = false;
    } else {
      Stripped = Values.size() != 1 ||
                 Values.front().getValue() != &getAssociatedValue();
    }

    StateType T;
    auto VisitValueCB = [&](Value &V) -> bool {
      if (isa<UndefValue>(V) || isa<ConstantPointerNull>(V))
        return true;
      const auto *AA = A.getAAFor<AAAlign>(*this, IRPosition::value(V),
                                           DepClassTy::REQUIRED);
      if (!AA || (!Stripped && this == AA)) {
        int64_t Offset;
        unsigned Alignment = 1;
        if (const Value *Base =
                GetPointerBaseWithConstantOffset(&V, Offset, DL)) {
          // TODO: Use AAAlign for the base too.
          Align PA = Base->getPointerAlignment(DL);
          // BasePointerAddr + Offset = Alignment * Q for some integer Q.
          // So we can say that the maximum power of two which is a divisor of
          // gcd(Offset, Alignment) is an alignment.

          uint32_t gcd =
              std::gcd(uint32_t(abs((int32_t)Offset)), uint32_t(PA.value()));
          Alignment = llvm::bit_floor(gcd);
        } else {
          Alignment = V.getPointerAlignment(DL).value();
        }
        // Use only IR information if we did not strip anything.
        T.takeKnownMaximum(Alignment);
        T.indicatePessimisticFixpoint();
      } else {
        // Use abstract attribute information.
        const AAAlign::StateType &DS = AA->getState();
        T ^= DS;
      }
      return T.isValidState();
    };

    for (const auto &VAC : Values) {
      if (!VisitValueCB(*VAC.getValue()))
        return indicatePessimisticFixpoint();
    }

    //  TODO: If we know we visited all incoming values, thus no are assumed
    //  dead, we can take the known information from the state T.
    return clampStateAndIndicateChange(getState(), T);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FLOATING_ATTR(align) }
};

/// Align attribute for function return value.
struct AAAlignReturned final
    : AAReturnedFromReturnedValues<AAAlign, AAAlignImpl> {
  using Base = AAReturnedFromReturnedValues<AAAlign, AAAlignImpl>;
  AAAlignReturned(const IRPosition &IRP, Attributor &A) : Base(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FNRET_ATTR(aligned) }
};

/// Align attribute for function argument.
struct AAAlignArgument final
    : AAArgumentFromCallSiteArguments<AAAlign, AAAlignImpl> {
  using Base = AAArgumentFromCallSiteArguments<AAAlign, AAAlignImpl>;
  AAAlignArgument(const IRPosition &IRP, Attributor &A) : Base(IRP, A) {}

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // If the associated argument is involved in a must-tail call we give up
    // because we would need to keep the argument alignments of caller and
    // callee in-sync. Just does not seem worth the trouble right now.
    if (A.getInfoCache().isInvolvedInMustTailCall(*getAssociatedArgument()))
      return ChangeStatus::UNCHANGED;
    return Base::manifest(A);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(aligned) }
};

struct AAAlignCallSiteArgument final : AAAlignFloating {
  AAAlignCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAAlignFloating(IRP, A) {}

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // If the associated argument is involved in a must-tail call we give up
    // because we would need to keep the argument alignments of caller and
    // callee in-sync. Just does not seem worth the trouble right now.
    if (Argument *Arg = getAssociatedArgument())
      if (A.getInfoCache().isInvolvedInMustTailCall(*Arg))
        return ChangeStatus::UNCHANGED;
    ChangeStatus Changed = AAAlignImpl::manifest(A);
    Align InheritAlign =
        getAssociatedValue().getPointerAlignment(A.getDataLayout());
    if (InheritAlign >= getAssumedAlign())
      Changed = ChangeStatus::UNCHANGED;
    return Changed;
  }

  /// See AbstractAttribute::updateImpl(Attributor &A).
  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Changed = AAAlignFloating::updateImpl(A);
    if (Argument *Arg = getAssociatedArgument()) {
      // We only take known information from the argument
      // so we do not need to track a dependence.
      const auto *ArgAlignAA = A.getAAFor<AAAlign>(
          *this, IRPosition::argument(*Arg), DepClassTy::NONE);
      if (ArgAlignAA)
        takeKnownMaximum(ArgAlignAA->getKnownAlign().value());
    }
    return Changed;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSARG_ATTR(aligned) }
};

/// Align attribute deduction for a call site return value.
struct AAAlignCallSiteReturned final
    : AACalleeToCallSite<AAAlign, AAAlignImpl> {
  using Base = AACalleeToCallSite<AAAlign, AAAlignImpl>;
  AAAlignCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(align); }
};
} // namespace

/// ------------------ Function No-Return Attribute ----------------------------
namespace {
struct AANoReturnImpl : public AANoReturn {
  AANoReturnImpl(const IRPosition &IRP, Attributor &A) : AANoReturn(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::NoReturn>(
        A, nullptr, getIRPosition(), DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "noreturn" : "may-return";
  }

  /// See AbstractAttribute::updateImpl(Attributor &A).
  ChangeStatus updateImpl(Attributor &A) override {
    auto CheckForNoReturn = [](Instruction &) { return false; };
    bool UsedAssumedInformation = false;
    if (!A.checkForAllInstructions(CheckForNoReturn, *this,
                                   {(unsigned)Instruction::Ret},
                                   UsedAssumedInformation))
      return indicatePessimisticFixpoint();
    return ChangeStatus::UNCHANGED;
  }
};

struct AANoReturnFunction final : AANoReturnImpl {
  AANoReturnFunction(const IRPosition &IRP, Attributor &A)
      : AANoReturnImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(noreturn) }
};

/// NoReturn attribute deduction for a call sites.
struct AANoReturnCallSite final
    : AACalleeToCallSite<AANoReturn, AANoReturnImpl> {
  AANoReturnCallSite(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoReturn, AANoReturnImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(noreturn); }
};
} // namespace

/// ----------------------- Instance Info ---------------------------------

namespace {
/// A class to hold the state of for no-capture attributes.
struct AAInstanceInfoImpl : public AAInstanceInfo {
  AAInstanceInfoImpl(const IRPosition &IRP, Attributor &A)
      : AAInstanceInfo(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    Value &V = getAssociatedValue();
    if (auto *C = dyn_cast<Constant>(&V)) {
      if (C->isThreadDependent())
        indicatePessimisticFixpoint();
      else
        indicateOptimisticFixpoint();
      return;
    }
    if (auto *CB = dyn_cast<CallBase>(&V))
      if (CB->arg_size() == 0 && !CB->mayHaveSideEffects() &&
          !CB->mayReadFromMemory()) {
        indicateOptimisticFixpoint();
        return;
      }
    if (auto *I = dyn_cast<Instruction>(&V)) {
      const auto *CI =
          A.getInfoCache().getAnalysisResultForFunction<CycleAnalysis>(
              *I->getFunction());
      if (mayBeInCycle(CI, I, /* HeaderOnly */ false)) {
        indicatePessimisticFixpoint();
        return;
      }
    }
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;

    Value &V = getAssociatedValue();
    const Function *Scope = nullptr;
    if (auto *I = dyn_cast<Instruction>(&V))
      Scope = I->getFunction();
    if (auto *A = dyn_cast<Argument>(&V)) {
      Scope = A->getParent();
      if (!Scope->hasLocalLinkage())
        return Changed;
    }
    if (!Scope)
      return indicateOptimisticFixpoint();

    bool IsKnownNoRecurse;
    if (AA::hasAssumedIRAttr<Attribute::NoRecurse>(
            A, this, IRPosition::function(*Scope), DepClassTy::OPTIONAL,
            IsKnownNoRecurse))
      return Changed;

    auto UsePred = [&](const Use &U, bool &Follow) {
      const Instruction *UserI = dyn_cast<Instruction>(U.getUser());
      if (!UserI || isa<GetElementPtrInst>(UserI) || isa<CastInst>(UserI) ||
          isa<PHINode>(UserI) || isa<SelectInst>(UserI)) {
        Follow = true;
        return true;
      }
      if (isa<LoadInst>(UserI) || isa<CmpInst>(UserI) ||
          (isa<StoreInst>(UserI) &&
           cast<StoreInst>(UserI)->getValueOperand() != U.get()))
        return true;
      if (auto *CB = dyn_cast<CallBase>(UserI)) {
        // This check is not guaranteeing uniqueness but for now that we cannot
        // end up with two versions of \p U thinking it was one.
        auto *Callee = dyn_cast_if_present<Function>(CB->getCalledOperand());
        if (!Callee || !Callee->hasLocalLinkage())
          return true;
        if (!CB->isArgOperand(&U))
          return false;
        const auto *ArgInstanceInfoAA = A.getAAFor<AAInstanceInfo>(
            *this, IRPosition::callsite_argument(*CB, CB->getArgOperandNo(&U)),
            DepClassTy::OPTIONAL);
        if (!ArgInstanceInfoAA ||
            !ArgInstanceInfoAA->isAssumedUniqueForAnalysis())
          return false;
        // If this call base might reach the scope again we might forward the
        // argument back here. This is very conservative.
        if (AA::isPotentiallyReachable(
                A, *CB, *Scope, *this, /* ExclusionSet */ nullptr,
                [Scope](const Function &Fn) { return &Fn != Scope; }))
          return false;
        return true;
      }
      return false;
    };

    auto EquivalentUseCB = [&](const Use &OldU, const Use &NewU) {
      if (auto *SI = dyn_cast<StoreInst>(OldU.getUser())) {
        auto *Ptr = SI->getPointerOperand()->stripPointerCasts();
        if ((isa<AllocaInst>(Ptr) || isNoAliasCall(Ptr)) &&
            AA::isDynamicallyUnique(A, *this, *Ptr))
          return true;
      }
      return false;
    };

    if (!A.checkForAllUses(UsePred, *this, V, /* CheckBBLivenessOnly */ true,
                           DepClassTy::OPTIONAL,
                           /* IgnoreDroppableUses */ true, EquivalentUseCB))
      return indicatePessimisticFixpoint();

    return Changed;
  }

  /// See AbstractState::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return isAssumedUniqueForAnalysis() ? "<unique [fAa]>" : "<unknown>";
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}
};

/// InstanceInfo attribute for floating values.
struct AAInstanceInfoFloating : AAInstanceInfoImpl {
  AAInstanceInfoFloating(const IRPosition &IRP, Attributor &A)
      : AAInstanceInfoImpl(IRP, A) {}
};

/// NoCapture attribute for function arguments.
struct AAInstanceInfoArgument final : AAInstanceInfoFloating {
  AAInstanceInfoArgument(const IRPosition &IRP, Attributor &A)
      : AAInstanceInfoFloating(IRP, A) {}
};

/// InstanceInfo attribute for call site arguments.
struct AAInstanceInfoCallSiteArgument final : AAInstanceInfoImpl {
  AAInstanceInfoCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAInstanceInfoImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    Argument *Arg = getAssociatedArgument();
    if (!Arg)
      return indicatePessimisticFixpoint();
    const IRPosition &ArgPos = IRPosition::argument(*Arg);
    auto *ArgAA =
        A.getAAFor<AAInstanceInfo>(*this, ArgPos, DepClassTy::REQUIRED);
    if (!ArgAA)
      return indicatePessimisticFixpoint();
    return clampStateAndIndicateChange(getState(), ArgAA->getState());
  }
};

/// InstanceInfo attribute for function return value.
struct AAInstanceInfoReturned final : AAInstanceInfoImpl {
  AAInstanceInfoReturned(const IRPosition &IRP, Attributor &A)
      : AAInstanceInfoImpl(IRP, A) {
    llvm_unreachable("InstanceInfo is not applicable to function returns!");
  }

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    llvm_unreachable("InstanceInfo is not applicable to function returns!");
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable("InstanceInfo is not applicable to function returns!");
  }
};

/// InstanceInfo attribute deduction for a call site return value.
struct AAInstanceInfoCallSiteReturned final : AAInstanceInfoFloating {
  AAInstanceInfoCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAInstanceInfoFloating(IRP, A) {}
};
} // namespace

/// ----------------------- Variable Capturing ---------------------------------
bool AANoCapture::isImpliedByIR(Attributor &A, const IRPosition &IRP,
                                Attribute::AttrKind ImpliedAttributeKind,
                                bool IgnoreSubsumingPositions) {
  assert(ImpliedAttributeKind == Attribute::NoCapture &&
         "Unexpected attribute kind");
  Value &V = IRP.getAssociatedValue();
  if (!IRP.isArgumentPosition())
    return V.use_empty();

  // You cannot "capture" null in the default address space.
  //
  // FIXME: This should use NullPointerIsDefined to account for the function
  // attribute.
  if (isa<UndefValue>(V) || (isa<ConstantPointerNull>(V) &&
                             V.getType()->getPointerAddressSpace() == 0)) {
    return true;
  }

  if (A.hasAttr(IRP, {Attribute::NoCapture},
                /* IgnoreSubsumingPositions */ true, Attribute::NoCapture))
    return true;

  if (IRP.getPositionKind() == IRP_CALL_SITE_ARGUMENT)
    if (Argument *Arg = IRP.getAssociatedArgument())
      if (A.hasAttr(IRPosition::argument(*Arg),
                    {Attribute::NoCapture, Attribute::ByVal},
                    /* IgnoreSubsumingPositions */ true)) {
        A.manifestAttrs(IRP,
                        Attribute::get(V.getContext(), Attribute::NoCapture));
        return true;
      }

  if (const Function *F = IRP.getAssociatedFunction()) {
    // Check what state the associated function can actually capture.
    AANoCapture::StateType State;
    determineFunctionCaptureCapabilities(IRP, *F, State);
    if (State.isKnown(NO_CAPTURE)) {
      A.manifestAttrs(IRP,
                      Attribute::get(V.getContext(), Attribute::NoCapture));
      return true;
    }
  }

  return false;
}

/// Set the NOT_CAPTURED_IN_MEM and NOT_CAPTURED_IN_RET bits in \p Known
/// depending on the ability of the function associated with \p IRP to capture
/// state in memory and through "returning/throwing", respectively.
void AANoCapture::determineFunctionCaptureCapabilities(const IRPosition &IRP,
                                                       const Function &F,
                                                       BitIntegerState &State) {
  // TODO: Once we have memory behavior attributes we should use them here.

  // If we know we cannot communicate or write to memory, we do not care about
  // ptr2int anymore.
  bool ReadOnly = F.onlyReadsMemory();
  bool NoThrow = F.doesNotThrow();
  bool IsVoidReturn = F.getReturnType()->isVoidTy();
  if (ReadOnly && NoThrow && IsVoidReturn) {
    State.addKnownBits(NO_CAPTURE);
    return;
  }

  // A function cannot capture state in memory if it only reads memory, it can
  // however return/throw state and the state might be influenced by the
  // pointer value, e.g., loading from a returned pointer might reveal a bit.
  if (ReadOnly)
    State.addKnownBits(NOT_CAPTURED_IN_MEM);

  // A function cannot communicate state back if it does not through
  // exceptions and doesn not return values.
  if (NoThrow && IsVoidReturn)
    State.addKnownBits(NOT_CAPTURED_IN_RET);

  // Check existing "returned" attributes.
  int ArgNo = IRP.getCalleeArgNo();
  if (!NoThrow || ArgNo < 0 ||
      !F.getAttributes().hasAttrSomewhere(Attribute::Returned))
    return;

  for (unsigned U = 0, E = F.arg_size(); U < E; ++U)
    if (F.hasParamAttribute(U, Attribute::Returned)) {
      if (U == unsigned(ArgNo))
        State.removeAssumedBits(NOT_CAPTURED_IN_RET);
      else if (ReadOnly)
        State.addKnownBits(NO_CAPTURE);
      else
        State.addKnownBits(NOT_CAPTURED_IN_RET);
      break;
    }
}

namespace {
/// A class to hold the state of for no-capture attributes.
struct AANoCaptureImpl : public AANoCapture {
  AANoCaptureImpl(const IRPosition &IRP, Attributor &A) : AANoCapture(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    bool IsKnown;
    assert(!AA::hasAssumedIRAttr<Attribute::NoCapture>(
        A, nullptr, getIRPosition(), DepClassTy::NONE, IsKnown));
    (void)IsKnown;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override;

  /// see AbstractAttribute::isAssumedNoCaptureMaybeReturned(...).
  void getDeducedAttributes(Attributor &A, LLVMContext &Ctx,
                            SmallVectorImpl<Attribute> &Attrs) const override {
    if (!isAssumedNoCaptureMaybeReturned())
      return;

    if (isArgumentPosition()) {
      if (isAssumedNoCapture())
        Attrs.emplace_back(Attribute::get(Ctx, Attribute::NoCapture));
      else if (ManifestInternal)
        Attrs.emplace_back(Attribute::get(Ctx, "no-capture-maybe-returned"));
    }
  }

  /// See AbstractState::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    if (isKnownNoCapture())
      return "known not-captured";
    if (isAssumedNoCapture())
      return "assumed not-captured";
    if (isKnownNoCaptureMaybeReturned())
      return "known not-captured-maybe-returned";
    if (isAssumedNoCaptureMaybeReturned())
      return "assumed not-captured-maybe-returned";
    return "assumed-captured";
  }

  /// Check the use \p U and update \p State accordingly. Return true if we
  /// should continue to update the state.
  bool checkUse(Attributor &A, AANoCapture::StateType &State, const Use &U,
                bool &Follow) {
    Instruction *UInst = cast<Instruction>(U.getUser());
    LLVM_DEBUG(dbgs() << "[AANoCapture] Check use: " << *U.get() << " in "
                      << *UInst << "\n");

    // Deal with ptr2int by following uses.
    if (isa<PtrToIntInst>(UInst)) {
      LLVM_DEBUG(dbgs() << " - ptr2int assume the worst!\n");
      return isCapturedIn(State, /* Memory */ true, /* Integer */ true,
                          /* Return */ true);
    }

    // For stores we already checked if we can follow them, if they make it
    // here we give up.
    if (isa<StoreInst>(UInst))
      return isCapturedIn(State, /* Memory */ true, /* Integer */ true,
                          /* Return */ true);

    // Explicitly catch return instructions.
    if (isa<ReturnInst>(UInst)) {
      if (UInst->getFunction() == getAnchorScope())
        return isCapturedIn(State, /* Memory */ false, /* Integer */ false,
                            /* Return */ true);
      return isCapturedIn(State, /* Memory */ true, /* Integer */ true,
                          /* Return */ true);
    }

    // For now we only use special logic for call sites. However, the tracker
    // itself knows about a lot of other non-capturing cases already.
    auto *CB = dyn_cast<CallBase>(UInst);
    if (!CB || !CB->isArgOperand(&U))
      return isCapturedIn(State, /* Memory */ true, /* Integer */ true,
                          /* Return */ true);

    unsigned ArgNo = CB->getArgOperandNo(&U);
    const IRPosition &CSArgPos = IRPosition::callsite_argument(*CB, ArgNo);
    // If we have a abstract no-capture attribute for the argument we can use
    // it to justify a non-capture attribute here. This allows recursion!
    bool IsKnownNoCapture;
    const AANoCapture *ArgNoCaptureAA = nullptr;
    bool IsAssumedNoCapture = AA::hasAssumedIRAttr<Attribute::NoCapture>(
        A, this, CSArgPos, DepClassTy::REQUIRED, IsKnownNoCapture, false,
        &ArgNoCaptureAA);
    if (IsAssumedNoCapture)
      return isCapturedIn(State, /* Memory */ false, /* Integer */ false,
                          /* Return */ false);
    if (ArgNoCaptureAA && ArgNoCaptureAA->isAssumedNoCaptureMaybeReturned()) {
      Follow = true;
      return isCapturedIn(State, /* Memory */ false, /* Integer */ false,
                          /* Return */ false);
    }

    // Lastly, we could not find a reason no-capture can be assumed so we don't.
    return isCapturedIn(State, /* Memory */ true, /* Integer */ true,
                        /* Return */ true);
  }

  /// Update \p State according to \p CapturedInMem, \p CapturedInInt, and
  /// \p CapturedInRet, then return true if we should continue updating the
  /// state.
  static bool isCapturedIn(AANoCapture::StateType &State, bool CapturedInMem,
                           bool CapturedInInt, bool CapturedInRet) {
    LLVM_DEBUG(dbgs() << " - captures [Mem " << CapturedInMem << "|Int "
                      << CapturedInInt << "|Ret " << CapturedInRet << "]\n");
    if (CapturedInMem)
      State.removeAssumedBits(AANoCapture::NOT_CAPTURED_IN_MEM);
    if (CapturedInInt)
      State.removeAssumedBits(AANoCapture::NOT_CAPTURED_IN_INT);
    if (CapturedInRet)
      State.removeAssumedBits(AANoCapture::NOT_CAPTURED_IN_RET);
    return State.isAssumed(AANoCapture::NO_CAPTURE_MAYBE_RETURNED);
  }
};

ChangeStatus AANoCaptureImpl::updateImpl(Attributor &A) {
  const IRPosition &IRP = getIRPosition();
  Value *V = isArgumentPosition() ? IRP.getAssociatedArgument()
                                  : &IRP.getAssociatedValue();
  if (!V)
    return indicatePessimisticFixpoint();

  const Function *F =
      isArgumentPosition() ? IRP.getAssociatedFunction() : IRP.getAnchorScope();

  // TODO: Is the checkForAllUses below useful for constants?
  if (!F)
    return indicatePessimisticFixpoint();

  AANoCapture::StateType T;
  const IRPosition &FnPos = IRPosition::function(*F);

  // Readonly means we cannot capture through memory.
  bool IsKnown;
  if (AA::isAssumedReadOnly(A, FnPos, *this, IsKnown)) {
    T.addKnownBits(NOT_CAPTURED_IN_MEM);
    if (IsKnown)
      addKnownBits(NOT_CAPTURED_IN_MEM);
  }

  // Make sure all returned values are different than the underlying value.
  // TODO: we could do this in a more sophisticated way inside
  //       AAReturnedValues, e.g., track all values that escape through returns
  //       directly somehow.
  auto CheckReturnedArgs = [&](bool &UsedAssumedInformation) {
    SmallVector<AA::ValueAndContext> Values;
    if (!A.getAssumedSimplifiedValues(IRPosition::returned(*F), this, Values,
                                      AA::ValueScope::Intraprocedural,
                                      UsedAssumedInformation))
      return false;
    bool SeenConstant = false;
    for (const AA::ValueAndContext &VAC : Values) {
      if (isa<Constant>(VAC.getValue())) {
        if (SeenConstant)
          return false;
        SeenConstant = true;
      } else if (!isa<Argument>(VAC.getValue()) ||
                 VAC.getValue() == getAssociatedArgument())
        return false;
    }
    return true;
  };

  bool IsKnownNoUnwind;
  if (AA::hasAssumedIRAttr<Attribute::NoUnwind>(
          A, this, FnPos, DepClassTy::OPTIONAL, IsKnownNoUnwind)) {
    bool IsVoidTy = F->getReturnType()->isVoidTy();
    bool UsedAssumedInformation = false;
    if (IsVoidTy || CheckReturnedArgs(UsedAssumedInformation)) {
      T.addKnownBits(NOT_CAPTURED_IN_RET);
      if (T.isKnown(NOT_CAPTURED_IN_MEM))
        return ChangeStatus::UNCHANGED;
      if (IsKnownNoUnwind && (IsVoidTy || !UsedAssumedInformation)) {
        addKnownBits(NOT_CAPTURED_IN_RET);
        if (isKnown(NOT_CAPTURED_IN_MEM))
          return indicateOptimisticFixpoint();
      }
    }
  }

  auto IsDereferenceableOrNull = [&](Value *O, const DataLayout &DL) {
    const auto *DerefAA = A.getAAFor<AADereferenceable>(
        *this, IRPosition::value(*O), DepClassTy::OPTIONAL);
    return DerefAA && DerefAA->getAssumedDereferenceableBytes();
  };

  auto UseCheck = [&](const Use &U, bool &Follow) -> bool {
    switch (DetermineUseCaptureKind(U, IsDereferenceableOrNull)) {
    case UseCaptureKind::NO_CAPTURE:
      return true;
    case UseCaptureKind::MAY_CAPTURE:
      return checkUse(A, T, U, Follow);
    case UseCaptureKind::PASSTHROUGH:
      Follow = true;
      return true;
    }
    llvm_unreachable("Unexpected use capture kind!");
  };

  if (!A.checkForAllUses(UseCheck, *this, *V))
    return indicatePessimisticFixpoint();

  AANoCapture::StateType &S = getState();
  auto Assumed = S.getAssumed();
  S.intersectAssumedBits(T.getAssumed());
  if (!isAssumedNoCaptureMaybeReturned())
    return indicatePessimisticFixpoint();
  return Assumed == S.getAssumed() ? ChangeStatus::UNCHANGED
                                   : ChangeStatus::CHANGED;
}

/// NoCapture attribute for function arguments.
struct AANoCaptureArgument final : AANoCaptureImpl {
  AANoCaptureArgument(const IRPosition &IRP, Attributor &A)
      : AANoCaptureImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(nocapture) }
};

/// NoCapture attribute for call site arguments.
struct AANoCaptureCallSiteArgument final : AANoCaptureImpl {
  AANoCaptureCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AANoCaptureImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    Argument *Arg = getAssociatedArgument();
    if (!Arg)
      return indicatePessimisticFixpoint();
    const IRPosition &ArgPos = IRPosition::argument(*Arg);
    bool IsKnownNoCapture;
    const AANoCapture *ArgAA = nullptr;
    if (AA::hasAssumedIRAttr<Attribute::NoCapture>(
            A, this, ArgPos, DepClassTy::REQUIRED, IsKnownNoCapture, false,
            &ArgAA))
      return ChangeStatus::UNCHANGED;
    if (!ArgAA || !ArgAA->isAssumedNoCaptureMaybeReturned())
      return indicatePessimisticFixpoint();
    return clampStateAndIndicateChange(getState(), ArgAA->getState());
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override{STATS_DECLTRACK_CSARG_ATTR(nocapture)};
};

/// NoCapture attribute for floating values.
struct AANoCaptureFloating final : AANoCaptureImpl {
  AANoCaptureFloating(const IRPosition &IRP, Attributor &A)
      : AANoCaptureImpl(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(nocapture)
  }
};

/// NoCapture attribute for function return value.
struct AANoCaptureReturned final : AANoCaptureImpl {
  AANoCaptureReturned(const IRPosition &IRP, Attributor &A)
      : AANoCaptureImpl(IRP, A) {
    llvm_unreachable("NoCapture is not applicable to function returns!");
  }

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    llvm_unreachable("NoCapture is not applicable to function returns!");
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable("NoCapture is not applicable to function returns!");
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}
};

/// NoCapture attribute deduction for a call site return value.
struct AANoCaptureCallSiteReturned final : AANoCaptureImpl {
  AANoCaptureCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AANoCaptureImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    const Function *F = getAnchorScope();
    // Check what state the associated function can actually capture.
    determineFunctionCaptureCapabilities(getIRPosition(), *F, *this);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(nocapture)
  }
};
} // namespace

/// ------------------ Value Simplify Attribute ----------------------------

bool ValueSimplifyStateType::unionAssumed(std::optional<Value *> Other) {
  // FIXME: Add a typecast support.
  SimplifiedAssociatedValue = AA::combineOptionalValuesInAAValueLatice(
      SimplifiedAssociatedValue, Other, Ty);
  if (SimplifiedAssociatedValue == std::optional<Value *>(nullptr))
    return false;

  LLVM_DEBUG({
    if (SimplifiedAssociatedValue)
      dbgs() << "[ValueSimplify] is assumed to be "
             << **SimplifiedAssociatedValue << "\n";
    else
      dbgs() << "[ValueSimplify] is assumed to be <none>\n";
  });
  return true;
}

namespace {
struct AAValueSimplifyImpl : AAValueSimplify {
  AAValueSimplifyImpl(const IRPosition &IRP, Attributor &A)
      : AAValueSimplify(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    if (getAssociatedValue().getType()->isVoidTy())
      indicatePessimisticFixpoint();
    if (A.hasSimplificationCallback(getIRPosition()))
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    LLVM_DEBUG({
      dbgs() << "SAV: " << (bool)SimplifiedAssociatedValue << " ";
      if (SimplifiedAssociatedValue && *SimplifiedAssociatedValue)
        dbgs() << "SAV: " << **SimplifiedAssociatedValue << " ";
    });
    return isValidState() ? (isAtFixpoint() ? "simplified" : "maybe-simple")
                          : "not-simple";
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}

  /// See AAValueSimplify::getAssumedSimplifiedValue()
  std::optional<Value *>
  getAssumedSimplifiedValue(Attributor &A) const override {
    return SimplifiedAssociatedValue;
  }

  /// Ensure the return value is \p V with type \p Ty, if not possible return
  /// nullptr. If \p Check is true we will only verify such an operation would
  /// suceed and return a non-nullptr value if that is the case. No IR is
  /// generated or modified.
  static Value *ensureType(Attributor &A, Value &V, Type &Ty, Instruction *CtxI,
                           bool Check) {
    if (auto *TypedV = AA::getWithType(V, Ty))
      return TypedV;
    if (CtxI && V.getType()->canLosslesslyBitCastTo(&Ty))
      return Check ? &V
                   : BitCastInst::CreatePointerBitCastOrAddrSpaceCast(
                         &V, &Ty, "", CtxI->getIterator());
    return nullptr;
  }

  /// Reproduce \p I with type \p Ty or return nullptr if that is not posisble.
  /// If \p Check is true we will only verify such an operation would suceed and
  /// return a non-nullptr value if that is the case. No IR is generated or
  /// modified.
  static Value *reproduceInst(Attributor &A,
                              const AbstractAttribute &QueryingAA,
                              Instruction &I, Type &Ty, Instruction *CtxI,
                              bool Check, ValueToValueMapTy &VMap) {
    assert(CtxI && "Cannot reproduce an instruction without context!");
    if (Check && (I.mayReadFromMemory() ||
                  !isSafeToSpeculativelyExecute(&I, CtxI, /* DT */ nullptr,
                                                /* TLI */ nullptr)))
      return nullptr;
    for (Value *Op : I.operands()) {
      Value *NewOp = reproduceValue(A, QueryingAA, *Op, Ty, CtxI, Check, VMap);
      if (!NewOp) {
        assert(Check && "Manifest of new value unexpectedly failed!");
        return nullptr;
      }
      if (!Check)
        VMap[Op] = NewOp;
    }
    if (Check)
      return &I;

    Instruction *CloneI = I.clone();
    // TODO: Try to salvage debug information here.
    CloneI->setDebugLoc(DebugLoc());
    VMap[&I] = CloneI;
    CloneI->insertBefore(CtxI);
    RemapInstruction(CloneI, VMap);
    return CloneI;
  }

  /// Reproduce \p V with type \p Ty or return nullptr if that is not posisble.
  /// If \p Check is true we will only verify such an operation would suceed and
  /// return a non-nullptr value if that is the case. No IR is generated or
  /// modified.
  static Value *reproduceValue(Attributor &A,
                               const AbstractAttribute &QueryingAA, Value &V,
                               Type &Ty, Instruction *CtxI, bool Check,
                               ValueToValueMapTy &VMap) {
    if (const auto &NewV = VMap.lookup(&V))
      return NewV;
    bool UsedAssumedInformation = false;
    std::optional<Value *> SimpleV = A.getAssumedSimplified(
        V, QueryingAA, UsedAssumedInformation, AA::Interprocedural);
    if (!SimpleV.has_value())
      return PoisonValue::get(&Ty);
    Value *EffectiveV = &V;
    if (*SimpleV)
      EffectiveV = *SimpleV;
    if (auto *C = dyn_cast<Constant>(EffectiveV))
      return C;
    if (CtxI && AA::isValidAtPosition(AA::ValueAndContext(*EffectiveV, *CtxI),
                                      A.getInfoCache()))
      return ensureType(A, *EffectiveV, Ty, CtxI, Check);
    if (auto *I = dyn_cast<Instruction>(EffectiveV))
      if (Value *NewV = reproduceInst(A, QueryingAA, *I, Ty, CtxI, Check, VMap))
        return ensureType(A, *NewV, Ty, CtxI, Check);
    return nullptr;
  }

  /// Return a value we can use as replacement for the associated one, or
  /// nullptr if we don't have one that makes sense.
  Value *manifestReplacementValue(Attributor &A, Instruction *CtxI) const {
    Value *NewV = SimplifiedAssociatedValue
                      ? *SimplifiedAssociatedValue
                      : UndefValue::get(getAssociatedType());
    if (NewV && NewV != &getAssociatedValue()) {
      ValueToValueMapTy VMap;
      // First verify we can reprduce the value with the required type at the
      // context location before we actually start modifying the IR.
      if (reproduceValue(A, *this, *NewV, *getAssociatedType(), CtxI,
                         /* CheckOnly */ true, VMap))
        return reproduceValue(A, *this, *NewV, *getAssociatedType(), CtxI,
                              /* CheckOnly */ false, VMap);
    }
    return nullptr;
  }

  /// Helper function for querying AAValueSimplify and updating candidate.
  /// \param IRP The value position we are trying to unify with SimplifiedValue
  bool checkAndUpdate(Attributor &A, const AbstractAttribute &QueryingAA,
                      const IRPosition &IRP, bool Simplify = true) {
    bool UsedAssumedInformation = false;
    std::optional<Value *> QueryingValueSimplified = &IRP.getAssociatedValue();
    if (Simplify)
      QueryingValueSimplified = A.getAssumedSimplified(
          IRP, QueryingAA, UsedAssumedInformation, AA::Interprocedural);
    return unionAssumed(QueryingValueSimplified);
  }

  /// Returns a candidate is found or not
  template <typename AAType> bool askSimplifiedValueFor(Attributor &A) {
    if (!getAssociatedValue().getType()->isIntegerTy())
      return false;

    // This will also pass the call base context.
    const auto *AA =
        A.getAAFor<AAType>(*this, getIRPosition(), DepClassTy::NONE);
    if (!AA)
      return false;

    std::optional<Constant *> COpt = AA->getAssumedConstant(A);

    if (!COpt) {
      SimplifiedAssociatedValue = std::nullopt;
      A.recordDependence(*AA, *this, DepClassTy::OPTIONAL);
      return true;
    }
    if (auto *C = *COpt) {
      SimplifiedAssociatedValue = C;
      A.recordDependence(*AA, *this, DepClassTy::OPTIONAL);
      return true;
    }
    return false;
  }

  bool askSimplifiedValueForOtherAAs(Attributor &A) {
    if (askSimplifiedValueFor<AAValueConstantRange>(A))
      return true;
    if (askSimplifiedValueFor<AAPotentialConstantValues>(A))
      return true;
    return false;
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    for (auto &U : getAssociatedValue().uses()) {
      // Check if we need to adjust the insertion point to make sure the IR is
      // valid.
      Instruction *IP = dyn_cast<Instruction>(U.getUser());
      if (auto *PHI = dyn_cast_or_null<PHINode>(IP))
        IP = PHI->getIncomingBlock(U)->getTerminator();
      if (auto *NewV = manifestReplacementValue(A, IP)) {
        LLVM_DEBUG(dbgs() << "[ValueSimplify] " << getAssociatedValue()
                          << " -> " << *NewV << " :: " << *this << "\n");
        if (A.changeUseAfterManifest(U, *NewV))
          Changed = ChangeStatus::CHANGED;
      }
    }

    return Changed | AAValueSimplify::manifest(A);
  }

  /// See AbstractState::indicatePessimisticFixpoint(...).
  ChangeStatus indicatePessimisticFixpoint() override {
    SimplifiedAssociatedValue = &getAssociatedValue();
    return AAValueSimplify::indicatePessimisticFixpoint();
  }
};

struct AAValueSimplifyArgument final : AAValueSimplifyImpl {
  AAValueSimplifyArgument(const IRPosition &IRP, Attributor &A)
      : AAValueSimplifyImpl(IRP, A) {}

  void initialize(Attributor &A) override {
    AAValueSimplifyImpl::initialize(A);
    if (A.hasAttr(getIRPosition(),
                  {Attribute::InAlloca, Attribute::Preallocated,
                   Attribute::StructRet, Attribute::Nest, Attribute::ByVal},
                  /* IgnoreSubsumingPositions */ true))
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // Byval is only replacable if it is readonly otherwise we would write into
    // the replaced value and not the copy that byval creates implicitly.
    Argument *Arg = getAssociatedArgument();
    if (Arg->hasByValAttr()) {
      // TODO: We probably need to verify synchronization is not an issue, e.g.,
      //       there is no race by not copying a constant byval.
      bool IsKnown;
      if (!AA::isAssumedReadOnly(A, getIRPosition(), *this, IsKnown))
        return indicatePessimisticFixpoint();
    }

    auto Before = SimplifiedAssociatedValue;

    auto PredForCallSite = [&](AbstractCallSite ACS) {
      const IRPosition &ACSArgPos =
          IRPosition::callsite_argument(ACS, getCallSiteArgNo());
      // Check if a coresponding argument was found or if it is on not
      // associated (which can happen for callback calls).
      if (ACSArgPos.getPositionKind() == IRPosition::IRP_INVALID)
        return false;

      // Simplify the argument operand explicitly and check if the result is
      // valid in the current scope. This avoids refering to simplified values
      // in other functions, e.g., we don't want to say a an argument in a
      // static function is actually an argument in a different function.
      bool UsedAssumedInformation = false;
      std::optional<Constant *> SimpleArgOp =
          A.getAssumedConstant(ACSArgPos, *this, UsedAssumedInformation);
      if (!SimpleArgOp)
        return true;
      if (!*SimpleArgOp)
        return false;
      if (!AA::isDynamicallyUnique(A, *this, **SimpleArgOp))
        return false;
      return unionAssumed(*SimpleArgOp);
    };

    // Generate a answer specific to a call site context.
    bool Success;
    bool UsedAssumedInformation = false;
    if (hasCallBaseContext() &&
        getCallBaseContext()->getCalledOperand() == Arg->getParent())
      Success = PredForCallSite(
          AbstractCallSite(&getCallBaseContext()->getCalledOperandUse()));
    else
      Success = A.checkForAllCallSites(PredForCallSite, *this, true,
                                       UsedAssumedInformation);

    if (!Success)
      if (!askSimplifiedValueForOtherAAs(A))
        return indicatePessimisticFixpoint();

    // If a candidate was found in this update, return CHANGED.
    return Before == SimplifiedAssociatedValue ? ChangeStatus::UNCHANGED
                                               : ChangeStatus ::CHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_ARG_ATTR(value_simplify)
  }
};

struct AAValueSimplifyReturned : AAValueSimplifyImpl {
  AAValueSimplifyReturned(const IRPosition &IRP, Attributor &A)
      : AAValueSimplifyImpl(IRP, A) {}

  /// See AAValueSimplify::getAssumedSimplifiedValue()
  std::optional<Value *>
  getAssumedSimplifiedValue(Attributor &A) const override {
    if (!isValidState())
      return nullptr;
    return SimplifiedAssociatedValue;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto Before = SimplifiedAssociatedValue;

    auto ReturnInstCB = [&](Instruction &I) {
      auto &RI = cast<ReturnInst>(I);
      return checkAndUpdate(
          A, *this,
          IRPosition::value(*RI.getReturnValue(), getCallBaseContext()));
    };

    bool UsedAssumedInformation = false;
    if (!A.checkForAllInstructions(ReturnInstCB, *this, {Instruction::Ret},
                                   UsedAssumedInformation))
      if (!askSimplifiedValueForOtherAAs(A))
        return indicatePessimisticFixpoint();

    // If a candidate was found in this update, return CHANGED.
    return Before == SimplifiedAssociatedValue ? ChangeStatus::UNCHANGED
                                               : ChangeStatus ::CHANGED;
  }

  ChangeStatus manifest(Attributor &A) override {
    // We queried AAValueSimplify for the returned values so they will be
    // replaced if a simplified form was found. Nothing to do here.
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(value_simplify)
  }
};

struct AAValueSimplifyFloating : AAValueSimplifyImpl {
  AAValueSimplifyFloating(const IRPosition &IRP, Attributor &A)
      : AAValueSimplifyImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAValueSimplifyImpl::initialize(A);
    Value &V = getAnchorValue();

    // TODO: add other stuffs
    if (isa<Constant>(V))
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto Before = SimplifiedAssociatedValue;
    if (!askSimplifiedValueForOtherAAs(A))
      return indicatePessimisticFixpoint();

    // If a candidate was found in this update, return CHANGED.
    return Before == SimplifiedAssociatedValue ? ChangeStatus::UNCHANGED
                                               : ChangeStatus ::CHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(value_simplify)
  }
};

struct AAValueSimplifyFunction : AAValueSimplifyImpl {
  AAValueSimplifyFunction(const IRPosition &IRP, Attributor &A)
      : AAValueSimplifyImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    SimplifiedAssociatedValue = nullptr;
    indicateOptimisticFixpoint();
  }
  /// See AbstractAttribute::initialize(...).
  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable(
        "AAValueSimplify(Function|CallSite)::updateImpl will not be called");
  }
  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FN_ATTR(value_simplify)
  }
};

struct AAValueSimplifyCallSite : AAValueSimplifyFunction {
  AAValueSimplifyCallSite(const IRPosition &IRP, Attributor &A)
      : AAValueSimplifyFunction(IRP, A) {}
  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CS_ATTR(value_simplify)
  }
};

struct AAValueSimplifyCallSiteReturned : AAValueSimplifyImpl {
  AAValueSimplifyCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAValueSimplifyImpl(IRP, A) {}

  void initialize(Attributor &A) override {
    AAValueSimplifyImpl::initialize(A);
    Function *Fn = getAssociatedFunction();
    assert(Fn && "Did expect an associted function");
    for (Argument &Arg : Fn->args()) {
      if (Arg.hasReturnedAttr()) {
        auto IRP = IRPosition::callsite_argument(*cast<CallBase>(getCtxI()),
                                                 Arg.getArgNo());
        if (IRP.getPositionKind() == IRPosition::IRP_CALL_SITE_ARGUMENT &&
            checkAndUpdate(A, *this, IRP))
          indicateOptimisticFixpoint();
        else
          indicatePessimisticFixpoint();
        return;
      }
    }
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    return indicatePessimisticFixpoint();
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(value_simplify)
  }
};

struct AAValueSimplifyCallSiteArgument : AAValueSimplifyFloating {
  AAValueSimplifyCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAValueSimplifyFloating(IRP, A) {}

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    // TODO: We should avoid simplification duplication to begin with.
    auto *FloatAA = A.lookupAAFor<AAValueSimplify>(
        IRPosition::value(getAssociatedValue()), this, DepClassTy::NONE);
    if (FloatAA && FloatAA->getState().isValidState())
      return Changed;

    if (auto *NewV = manifestReplacementValue(A, getCtxI())) {
      Use &U = cast<CallBase>(&getAnchorValue())
                   ->getArgOperandUse(getCallSiteArgNo());
      if (A.changeUseAfterManifest(U, *NewV))
        Changed = ChangeStatus::CHANGED;
    }

    return Changed | AAValueSimplify::manifest(A);
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(value_simplify)
  }
};
} // namespace

/// ----------------------- Heap-To-Stack Conversion ---------------------------
namespace {
struct AAHeapToStackFunction final : public AAHeapToStack {

  struct AllocationInfo {
    /// The call that allocates the memory.
    CallBase *const CB;

    /// The library function id for the allocation.
    LibFunc LibraryFunctionId = NotLibFunc;

    /// The status wrt. a rewrite.
    enum {
      STACK_DUE_TO_USE,
      STACK_DUE_TO_FREE,
      INVALID,
    } Status = STACK_DUE_TO_USE;

    /// Flag to indicate if we encountered a use that might free this allocation
    /// but which is not in the deallocation infos.
    bool HasPotentiallyFreeingUnknownUses = false;

    /// Flag to indicate that we should place the new alloca in the function
    /// entry block rather than where the call site (CB) is.
    bool MoveAllocaIntoEntry = true;

    /// The set of free calls that use this allocation.
    SmallSetVector<CallBase *, 1> PotentialFreeCalls{};
  };

  struct DeallocationInfo {
    /// The call that deallocates the memory.
    CallBase *const CB;
    /// The value freed by the call.
    Value *FreedOp;

    /// Flag to indicate if we don't know all objects this deallocation might
    /// free.
    bool MightFreeUnknownObjects = false;

    /// The set of allocation calls that are potentially freed.
    SmallSetVector<CallBase *, 1> PotentialAllocationCalls{};
  };

  AAHeapToStackFunction(const IRPosition &IRP, Attributor &A)
      : AAHeapToStack(IRP, A) {}

  ~AAHeapToStackFunction() {
    // Ensure we call the destructor so we release any memory allocated in the
    // sets.
    for (auto &It : AllocationInfos)
      It.second->~AllocationInfo();
    for (auto &It : DeallocationInfos)
      It.second->~DeallocationInfo();
  }

  void initialize(Attributor &A) override {
    AAHeapToStack::initialize(A);

    const Function *F = getAnchorScope();
    const auto *TLI = A.getInfoCache().getTargetLibraryInfoForFunction(*F);

    auto AllocationIdentifierCB = [&](Instruction &I) {
      CallBase *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        return true;
      if (Value *FreedOp = getFreedOperand(CB, TLI)) {
        DeallocationInfos[CB] = new (A.Allocator) DeallocationInfo{CB, FreedOp};
        return true;
      }
      // To do heap to stack, we need to know that the allocation itself is
      // removable once uses are rewritten, and that we can initialize the
      // alloca to the same pattern as the original allocation result.
      if (isRemovableAlloc(CB, TLI)) {
        auto *I8Ty = Type::getInt8Ty(CB->getParent()->getContext());
        if (nullptr != getInitialValueOfAllocation(CB, TLI, I8Ty)) {
          AllocationInfo *AI = new (A.Allocator) AllocationInfo{CB};
          AllocationInfos[CB] = AI;
          if (TLI)
            TLI->getLibFunc(*CB, AI->LibraryFunctionId);
        }
      }
      return true;
    };

    bool UsedAssumedInformation = false;
    bool Success = A.checkForAllCallLikeInstructions(
        AllocationIdentifierCB, *this, UsedAssumedInformation,
        /* CheckBBLivenessOnly */ false,
        /* CheckPotentiallyDead */ true);
    (void)Success;
    assert(Success && "Did not expect the call base visit callback to fail!");

    Attributor::SimplifictionCallbackTy SCB =
        [](const IRPosition &, const AbstractAttribute *,
           bool &) -> std::optional<Value *> { return nullptr; };
    for (const auto &It : AllocationInfos)
      A.registerSimplificationCallback(IRPosition::callsite_returned(*It.first),
                                       SCB);
    for (const auto &It : DeallocationInfos)
      A.registerSimplificationCallback(IRPosition::callsite_returned(*It.first),
                                       SCB);
  }

  const std::string getAsStr(Attributor *A) const override {
    unsigned NumH2SMallocs = 0, NumInvalidMallocs = 0;
    for (const auto &It : AllocationInfos) {
      if (It.second->Status == AllocationInfo::INVALID)
        ++NumInvalidMallocs;
      else
        ++NumH2SMallocs;
    }
    return "[H2S] Mallocs Good/Bad: " + std::to_string(NumH2SMallocs) + "/" +
           std::to_string(NumInvalidMallocs);
  }

  /// See AbstractAttribute::trackStatistics().
  void trackStatistics() const override {
    STATS_DECL(
        MallocCalls, Function,
        "Number of malloc/calloc/aligned_alloc calls converted to allocas");
    for (const auto &It : AllocationInfos)
      if (It.second->Status != AllocationInfo::INVALID)
        ++BUILD_STAT_NAME(MallocCalls, Function);
  }

  bool isAssumedHeapToStack(const CallBase &CB) const override {
    if (isValidState())
      if (AllocationInfo *AI =
              AllocationInfos.lookup(const_cast<CallBase *>(&CB)))
        return AI->Status != AllocationInfo::INVALID;
    return false;
  }

  bool isAssumedHeapToStackRemovedFree(CallBase &CB) const override {
    if (!isValidState())
      return false;

    for (const auto &It : AllocationInfos) {
      AllocationInfo &AI = *It.second;
      if (AI.Status == AllocationInfo::INVALID)
        continue;

      if (AI.PotentialFreeCalls.count(&CB))
        return true;
    }

    return false;
  }

  ChangeStatus manifest(Attributor &A) override {
    assert(getState().isValidState() &&
           "Attempted to manifest an invalid state!");

    ChangeStatus HasChanged = ChangeStatus::UNCHANGED;
    Function *F = getAnchorScope();
    const auto *TLI = A.getInfoCache().getTargetLibraryInfoForFunction(*F);

    for (auto &It : AllocationInfos) {
      AllocationInfo &AI = *It.second;
      if (AI.Status == AllocationInfo::INVALID)
        continue;

      for (CallBase *FreeCall : AI.PotentialFreeCalls) {
        LLVM_DEBUG(dbgs() << "H2S: Removing free call: " << *FreeCall << "\n");
        A.deleteAfterManifest(*FreeCall);
        HasChanged = ChangeStatus::CHANGED;
      }

      LLVM_DEBUG(dbgs() << "H2S: Removing malloc-like call: " << *AI.CB
                        << "\n");

      auto Remark = [&](OptimizationRemark OR) {
        LibFunc IsAllocShared;
        if (TLI->getLibFunc(*AI.CB, IsAllocShared))
          if (IsAllocShared == LibFunc___kmpc_alloc_shared)
            return OR << "Moving globalized variable to the stack.";
        return OR << "Moving memory allocation from the heap to the stack.";
      };
      if (AI.LibraryFunctionId == LibFunc___kmpc_alloc_shared)
        A.emitRemark<OptimizationRemark>(AI.CB, "OMP110", Remark);
      else
        A.emitRemark<OptimizationRemark>(AI.CB, "HeapToStack", Remark);

      const DataLayout &DL = A.getInfoCache().getDL();
      Value *Size;
      std::optional<APInt> SizeAPI = getSize(A, *this, AI);
      if (SizeAPI) {
        Size = ConstantInt::get(AI.CB->getContext(), *SizeAPI);
      } else {
        LLVMContext &Ctx = AI.CB->getContext();
        ObjectSizeOpts Opts;
        ObjectSizeOffsetEvaluator Eval(DL, TLI, Ctx, Opts);
        SizeOffsetValue SizeOffsetPair = Eval.compute(AI.CB);
        assert(SizeOffsetPair != ObjectSizeOffsetEvaluator::unknown() &&
               cast<ConstantInt>(SizeOffsetPair.Offset)->isZero());
        Size = SizeOffsetPair.Size;
      }

      BasicBlock::iterator IP = AI.MoveAllocaIntoEntry
                                    ? F->getEntryBlock().begin()
                                    : AI.CB->getIterator();

      Align Alignment(1);
      if (MaybeAlign RetAlign = AI.CB->getRetAlign())
        Alignment = std::max(Alignment, *RetAlign);
      if (Value *Align = getAllocAlignment(AI.CB, TLI)) {
        std::optional<APInt> AlignmentAPI = getAPInt(A, *this, *Align);
        assert(AlignmentAPI && AlignmentAPI->getZExtValue() > 0 &&
               "Expected an alignment during manifest!");
        Alignment =
            std::max(Alignment, assumeAligned(AlignmentAPI->getZExtValue()));
      }

      // TODO: Hoist the alloca towards the function entry.
      unsigned AS = DL.getAllocaAddrSpace();
      Instruction *Alloca =
          new AllocaInst(Type::getInt8Ty(F->getContext()), AS, Size, Alignment,
                         AI.CB->getName() + ".h2s", IP);

      if (Alloca->getType() != AI.CB->getType())
        Alloca = BitCastInst::CreatePointerBitCastOrAddrSpaceCast(
            Alloca, AI.CB->getType(), "malloc_cast", AI.CB->getIterator());

      auto *I8Ty = Type::getInt8Ty(F->getContext());
      auto *InitVal = getInitialValueOfAllocation(AI.CB, TLI, I8Ty);
      assert(InitVal &&
             "Must be able to materialize initial memory state of allocation");

      A.changeAfterManifest(IRPosition::inst(*AI.CB), *Alloca);

      if (auto *II = dyn_cast<InvokeInst>(AI.CB)) {
        auto *NBB = II->getNormalDest();
        BranchInst::Create(NBB, AI.CB->getParent());
        A.deleteAfterManifest(*AI.CB);
      } else {
        A.deleteAfterManifest(*AI.CB);
      }

      // Initialize the alloca with the same value as used by the allocation
      // function.  We can skip undef as the initial value of an alloc is
      // undef, and the memset would simply end up being DSEd.
      if (!isa<UndefValue>(InitVal)) {
        IRBuilder<> Builder(Alloca->getNextNode());
        // TODO: Use alignment above if align!=1
        Builder.CreateMemSet(Alloca, InitVal, Size, std::nullopt);
      }
      HasChanged = ChangeStatus::CHANGED;
    }

    return HasChanged;
  }

  std::optional<APInt> getAPInt(Attributor &A, const AbstractAttribute &AA,
                                Value &V) {
    bool UsedAssumedInformation = false;
    std::optional<Constant *> SimpleV =
        A.getAssumedConstant(V, AA, UsedAssumedInformation);
    if (!SimpleV)
      return APInt(64, 0);
    if (auto *CI = dyn_cast_or_null<ConstantInt>(*SimpleV))
      return CI->getValue();
    return std::nullopt;
  }

  std::optional<APInt> getSize(Attributor &A, const AbstractAttribute &AA,
                               AllocationInfo &AI) {
    auto Mapper = [&](const Value *V) -> const Value * {
      bool UsedAssumedInformation = false;
      if (std::optional<Constant *> SimpleV =
              A.getAssumedConstant(*V, AA, UsedAssumedInformation))
        if (*SimpleV)
          return *SimpleV;
      return V;
    };

    const Function *F = getAnchorScope();
    const auto *TLI = A.getInfoCache().getTargetLibraryInfoForFunction(*F);
    return getAllocSize(AI.CB, TLI, Mapper);
  }

  /// Collection of all malloc-like calls in a function with associated
  /// information.
  MapVector<CallBase *, AllocationInfo *> AllocationInfos;

  /// Collection of all free-like calls in a function with associated
  /// information.
  MapVector<CallBase *, DeallocationInfo *> DeallocationInfos;

  ChangeStatus updateImpl(Attributor &A) override;
};

ChangeStatus AAHeapToStackFunction::updateImpl(Attributor &A) {
  ChangeStatus Changed = ChangeStatus::UNCHANGED;
  const Function *F = getAnchorScope();
  const auto *TLI = A.getInfoCache().getTargetLibraryInfoForFunction(*F);

  const auto *LivenessAA =
      A.getAAFor<AAIsDead>(*this, IRPosition::function(*F), DepClassTy::NONE);

  MustBeExecutedContextExplorer *Explorer =
      A.getInfoCache().getMustBeExecutedContextExplorer();

  bool StackIsAccessibleByOtherThreads =
      A.getInfoCache().stackIsAccessibleByOtherThreads();

  LoopInfo *LI =
      A.getInfoCache().getAnalysisResultForFunction<LoopAnalysis>(*F);
  std::optional<bool> MayContainIrreducibleControl;
  auto IsInLoop = [&](BasicBlock &BB) {
    if (&F->getEntryBlock() == &BB)
      return false;
    if (!MayContainIrreducibleControl.has_value())
      MayContainIrreducibleControl = mayContainIrreducibleControl(*F, LI);
    if (*MayContainIrreducibleControl)
      return true;
    if (!LI)
      return true;
    return LI->getLoopFor(&BB) != nullptr;
  };

  // Flag to ensure we update our deallocation information at most once per
  // updateImpl call and only if we use the free check reasoning.
  bool HasUpdatedFrees = false;

  auto UpdateFrees = [&]() {
    HasUpdatedFrees = true;

    for (auto &It : DeallocationInfos) {
      DeallocationInfo &DI = *It.second;
      // For now we cannot use deallocations that have unknown inputs, skip
      // them.
      if (DI.MightFreeUnknownObjects)
        continue;

      // No need to analyze dead calls, ignore them instead.
      bool UsedAssumedInformation = false;
      if (A.isAssumedDead(*DI.CB, this, LivenessAA, UsedAssumedInformation,
                          /* CheckBBLivenessOnly */ true))
        continue;

      // Use the non-optimistic version to get the freed object.
      Value *Obj = getUnderlyingObject(DI.FreedOp);
      if (!Obj) {
        LLVM_DEBUG(dbgs() << "[H2S] Unknown underlying object for free!\n");
        DI.MightFreeUnknownObjects = true;
        continue;
      }

      // Free of null and undef can be ignored as no-ops (or UB in the latter
      // case).
      if (isa<ConstantPointerNull>(Obj) || isa<UndefValue>(Obj))
        continue;

      CallBase *ObjCB = dyn_cast<CallBase>(Obj);
      if (!ObjCB) {
        LLVM_DEBUG(dbgs() << "[H2S] Free of a non-call object: " << *Obj
                          << "\n");
        DI.MightFreeUnknownObjects = true;
        continue;
      }

      AllocationInfo *AI = AllocationInfos.lookup(ObjCB);
      if (!AI) {
        LLVM_DEBUG(dbgs() << "[H2S] Free of a non-allocation object: " << *Obj
                          << "\n");
        DI.MightFreeUnknownObjects = true;
        continue;
      }

      DI.PotentialAllocationCalls.insert(ObjCB);
    }
  };

  auto FreeCheck = [&](AllocationInfo &AI) {
    // If the stack is not accessible by other threads, the "must-free" logic
    // doesn't apply as the pointer could be shared and needs to be places in
    // "shareable" memory.
    if (!StackIsAccessibleByOtherThreads) {
      bool IsKnownNoSycn;
      if (!AA::hasAssumedIRAttr<Attribute::NoSync>(
              A, this, getIRPosition(), DepClassTy::OPTIONAL, IsKnownNoSycn)) {
        LLVM_DEBUG(
            dbgs() << "[H2S] found an escaping use, stack is not accessible by "
                      "other threads and function is not nosync:\n");
        return false;
      }
    }
    if (!HasUpdatedFrees)
      UpdateFrees();

    // TODO: Allow multi exit functions that have different free calls.
    if (AI.PotentialFreeCalls.size() != 1) {
      LLVM_DEBUG(dbgs() << "[H2S] did not find one free call but "
                        << AI.PotentialFreeCalls.size() << "\n");
      return false;
    }
    CallBase *UniqueFree = *AI.PotentialFreeCalls.begin();
    DeallocationInfo *DI = DeallocationInfos.lookup(UniqueFree);
    if (!DI) {
      LLVM_DEBUG(
          dbgs() << "[H2S] unique free call was not known as deallocation call "
                 << *UniqueFree << "\n");
      return false;
    }
    if (DI->MightFreeUnknownObjects) {
      LLVM_DEBUG(
          dbgs() << "[H2S] unique free call might free unknown allocations\n");
      return false;
    }
    if (DI->PotentialAllocationCalls.empty())
      return true;
    if (DI->PotentialAllocationCalls.size() > 1) {
      LLVM_DEBUG(dbgs() << "[H2S] unique free call might free "
                        << DI->PotentialAllocationCalls.size()
                        << " different allocations\n");
      return false;
    }
    if (*DI->PotentialAllocationCalls.begin() != AI.CB) {
      LLVM_DEBUG(
          dbgs()
          << "[H2S] unique free call not known to free this allocation but "
          << **DI->PotentialAllocationCalls.begin() << "\n");
      return false;
    }

    // __kmpc_alloc_shared and __kmpc_alloc_free are by construction matched.
    if (AI.LibraryFunctionId != LibFunc___kmpc_alloc_shared) {
      Instruction *CtxI = isa<InvokeInst>(AI.CB) ? AI.CB : AI.CB->getNextNode();
      if (!Explorer || !Explorer->findInContextOf(UniqueFree, CtxI)) {
        LLVM_DEBUG(dbgs() << "[H2S] unique free call might not be executed "
                             "with the allocation "
                          << *UniqueFree << "\n");
        return false;
      }
    }
    return true;
  };

  auto UsesCheck = [&](AllocationInfo &AI) {
    bool ValidUsesOnly = true;

    auto Pred = [&](const Use &U, bool &Follow) -> bool {
      Instruction *UserI = cast<Instruction>(U.getUser());
      if (isa<LoadInst>(UserI))
        return true;
      if (auto *SI = dyn_cast<StoreInst>(UserI)) {
        if (SI->getValueOperand() == U.get()) {
          LLVM_DEBUG(dbgs()
                     << "[H2S] escaping store to memory: " << *UserI << "\n");
          ValidUsesOnly = false;
        } else {
          // A store into the malloc'ed memory is fine.
        }
        return true;
      }
      if (auto *CB = dyn_cast<CallBase>(UserI)) {
        if (!CB->isArgOperand(&U) || CB->isLifetimeStartOrEnd())
          return true;
        if (DeallocationInfos.count(CB)) {
          AI.PotentialFreeCalls.insert(CB);
          return true;
        }

        unsigned ArgNo = CB->getArgOperandNo(&U);
        auto CBIRP = IRPosition::callsite_argument(*CB, ArgNo);

        bool IsKnownNoCapture;
        bool IsAssumedNoCapture = AA::hasAssumedIRAttr<Attribute::NoCapture>(
            A, this, CBIRP, DepClassTy::OPTIONAL, IsKnownNoCapture);

        // If a call site argument use is nofree, we are fine.
        bool IsKnownNoFree;
        bool IsAssumedNoFree = AA::hasAssumedIRAttr<Attribute::NoFree>(
            A, this, CBIRP, DepClassTy::OPTIONAL, IsKnownNoFree);

        if (!IsAssumedNoCapture ||
            (AI.LibraryFunctionId != LibFunc___kmpc_alloc_shared &&
             !IsAssumedNoFree)) {
          AI.HasPotentiallyFreeingUnknownUses |= !IsAssumedNoFree;

          // Emit a missed remark if this is missed OpenMP globalization.
          auto Remark = [&](OptimizationRemarkMissed ORM) {
            return ORM
                   << "Could not move globalized variable to the stack. "
                      "Variable is potentially captured in call. Mark "
                      "parameter as `__attribute__((noescape))` to override.";
          };

          if (ValidUsesOnly &&
              AI.LibraryFunctionId == LibFunc___kmpc_alloc_shared)
            A.emitRemark<OptimizationRemarkMissed>(CB, "OMP113", Remark);

          LLVM_DEBUG(dbgs() << "[H2S] Bad user: " << *UserI << "\n");
          ValidUsesOnly = false;
        }
        return true;
      }

      if (isa<GetElementPtrInst>(UserI) || isa<BitCastInst>(UserI) ||
          isa<PHINode>(UserI) || isa<SelectInst>(UserI)) {
        Follow = true;
        return true;
      }
      // Unknown user for which we can not track uses further (in a way that
      // makes sense).
      LLVM_DEBUG(dbgs() << "[H2S] Unknown user: " << *UserI << "\n");
      ValidUsesOnly = false;
      return true;
    };
    if (!A.checkForAllUses(Pred, *this, *AI.CB, /* CheckBBLivenessOnly */ false,
                           DepClassTy::OPTIONAL, /* IgnoreDroppableUses */ true,
                           [&](const Use &OldU, const Use &NewU) {
                             auto *SI = dyn_cast<StoreInst>(OldU.getUser());
                             return !SI || StackIsAccessibleByOtherThreads ||
                                    AA::isAssumedThreadLocalObject(
                                        A, *SI->getPointerOperand(), *this);
                           }))
      return false;
    return ValidUsesOnly;
  };

  // The actual update starts here. We look at all allocations and depending on
  // their status perform the appropriate check(s).
  for (auto &It : AllocationInfos) {
    AllocationInfo &AI = *It.second;
    if (AI.Status == AllocationInfo::INVALID)
      continue;

    if (Value *Align = getAllocAlignment(AI.CB, TLI)) {
      std::optional<APInt> APAlign = getAPInt(A, *this, *Align);
      if (!APAlign) {
        // Can't generate an alloca which respects the required alignment
        // on the allocation.
        LLVM_DEBUG(dbgs() << "[H2S] Unknown allocation alignment: " << *AI.CB
                          << "\n");
        AI.Status = AllocationInfo::INVALID;
        Changed = ChangeStatus::CHANGED;
        continue;
      }
      if (APAlign->ugt(llvm::Value::MaximumAlignment) ||
          !APAlign->isPowerOf2()) {
        LLVM_DEBUG(dbgs() << "[H2S] Invalid allocation alignment: " << APAlign
                          << "\n");
        AI.Status = AllocationInfo::INVALID;
        Changed = ChangeStatus::CHANGED;
        continue;
      }
    }

    std::optional<APInt> Size = getSize(A, *this, AI);
    if (AI.LibraryFunctionId != LibFunc___kmpc_alloc_shared &&
        MaxHeapToStackSize != -1) {
      if (!Size || Size->ugt(MaxHeapToStackSize)) {
        LLVM_DEBUG({
          if (!Size)
            dbgs() << "[H2S] Unknown allocation size: " << *AI.CB << "\n";
          else
            dbgs() << "[H2S] Allocation size too large: " << *AI.CB << " vs. "
                   << MaxHeapToStackSize << "\n";
        });

        AI.Status = AllocationInfo::INVALID;
        Changed = ChangeStatus::CHANGED;
        continue;
      }
    }

    switch (AI.Status) {
    case AllocationInfo::STACK_DUE_TO_USE:
      if (UsesCheck(AI))
        break;
      AI.Status = AllocationInfo::STACK_DUE_TO_FREE;
      [[fallthrough]];
    case AllocationInfo::STACK_DUE_TO_FREE:
      if (FreeCheck(AI))
        break;
      AI.Status = AllocationInfo::INVALID;
      Changed = ChangeStatus::CHANGED;
      break;
    case AllocationInfo::INVALID:
      llvm_unreachable("Invalid allocations should never reach this point!");
    };

    // Check if we still think we can move it into the entry block. If the
    // alloca comes from a converted __kmpc_alloc_shared then we can usually
    // ignore the potential compilations associated with loops.
    bool IsGlobalizedLocal =
        AI.LibraryFunctionId == LibFunc___kmpc_alloc_shared;
    if (AI.MoveAllocaIntoEntry &&
        (!Size.has_value() ||
         (!IsGlobalizedLocal && IsInLoop(*AI.CB->getParent()))))
      AI.MoveAllocaIntoEntry = false;
  }

  return Changed;
}
} // namespace

/// ----------------------- Privatizable Pointers ------------------------------
namespace {
struct AAPrivatizablePtrImpl : public AAPrivatizablePtr {
  AAPrivatizablePtrImpl(const IRPosition &IRP, Attributor &A)
      : AAPrivatizablePtr(IRP, A), PrivatizableType(std::nullopt) {}

  ChangeStatus indicatePessimisticFixpoint() override {
    AAPrivatizablePtr::indicatePessimisticFixpoint();
    PrivatizableType = nullptr;
    return ChangeStatus::CHANGED;
  }

  /// Identify the type we can chose for a private copy of the underlying
  /// argument. std::nullopt means it is not clear yet, nullptr means there is
  /// none.
  virtual std::optional<Type *> identifyPrivatizableType(Attributor &A) = 0;

  /// Return a privatizable type that encloses both T0 and T1.
  /// TODO: This is merely a stub for now as we should manage a mapping as well.
  std::optional<Type *> combineTypes(std::optional<Type *> T0,
                                     std::optional<Type *> T1) {
    if (!T0)
      return T1;
    if (!T1)
      return T0;
    if (T0 == T1)
      return T0;
    return nullptr;
  }

  std::optional<Type *> getPrivatizableType() const override {
    return PrivatizableType;
  }

  const std::string getAsStr(Attributor *A) const override {
    return isAssumedPrivatizablePtr() ? "[priv]" : "[no-priv]";
  }

protected:
  std::optional<Type *> PrivatizableType;
};

// TODO: Do this for call site arguments (probably also other values) as well.

struct AAPrivatizablePtrArgument final : public AAPrivatizablePtrImpl {
  AAPrivatizablePtrArgument(const IRPosition &IRP, Attributor &A)
      : AAPrivatizablePtrImpl(IRP, A) {}

  /// See AAPrivatizablePtrImpl::identifyPrivatizableType(...)
  std::optional<Type *> identifyPrivatizableType(Attributor &A) override {
    // If this is a byval argument and we know all the call sites (so we can
    // rewrite them), there is no need to check them explicitly.
    bool UsedAssumedInformation = false;
    SmallVector<Attribute, 1> Attrs;
    A.getAttrs(getIRPosition(), {Attribute::ByVal}, Attrs,
               /* IgnoreSubsumingPositions */ true);
    if (!Attrs.empty() &&
        A.checkForAllCallSites([](AbstractCallSite ACS) { return true; }, *this,
                               true, UsedAssumedInformation))
      return Attrs[0].getValueAsType();

    std::optional<Type *> Ty;
    unsigned ArgNo = getIRPosition().getCallSiteArgNo();

    // Make sure the associated call site argument has the same type at all call
    // sites and it is an allocation we know is safe to privatize, for now that
    // means we only allow alloca instructions.
    // TODO: We can additionally analyze the accesses in the callee to  create
    //       the type from that information instead. That is a little more
    //       involved and will be done in a follow up patch.
    auto CallSiteCheck = [&](AbstractCallSite ACS) {
      IRPosition ACSArgPos = IRPosition::callsite_argument(ACS, ArgNo);
      // Check if a coresponding argument was found or if it is one not
      // associated (which can happen for callback calls).
      if (ACSArgPos.getPositionKind() == IRPosition::IRP_INVALID)
        return false;

      // Check that all call sites agree on a type.
      auto *PrivCSArgAA =
          A.getAAFor<AAPrivatizablePtr>(*this, ACSArgPos, DepClassTy::REQUIRED);
      if (!PrivCSArgAA)
        return false;
      std::optional<Type *> CSTy = PrivCSArgAA->getPrivatizableType();

      LLVM_DEBUG({
        dbgs() << "[AAPrivatizablePtr] ACSPos: " << ACSArgPos << ", CSTy: ";
        if (CSTy && *CSTy)
          (*CSTy)->print(dbgs());
        else if (CSTy)
          dbgs() << "<nullptr>";
        else
          dbgs() << "<none>";
      });

      Ty = combineTypes(Ty, CSTy);

      LLVM_DEBUG({
        dbgs() << " : New Type: ";
        if (Ty && *Ty)
          (*Ty)->print(dbgs());
        else if (Ty)
          dbgs() << "<nullptr>";
        else
          dbgs() << "<none>";
        dbgs() << "\n";
      });

      return !Ty || *Ty;
    };

    if (!A.checkForAllCallSites(CallSiteCheck, *this, true,
                                UsedAssumedInformation))
      return nullptr;
    return Ty;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    PrivatizableType = identifyPrivatizableType(A);
    if (!PrivatizableType)
      return ChangeStatus::UNCHANGED;
    if (!*PrivatizableType)
      return indicatePessimisticFixpoint();

    // The dependence is optional so we don't give up once we give up on the
    // alignment.
    A.getAAFor<AAAlign>(*this, IRPosition::value(getAssociatedValue()),
                        DepClassTy::OPTIONAL);

    // Avoid arguments with padding for now.
    if (!A.hasAttr(getIRPosition(), Attribute::ByVal) &&
        !isDenselyPacked(*PrivatizableType, A.getInfoCache().getDL())) {
      LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] Padding detected\n");
      return indicatePessimisticFixpoint();
    }

    // Collect the types that will replace the privatizable type in the function
    // signature.
    SmallVector<Type *, 16> ReplacementTypes;
    identifyReplacementTypes(*PrivatizableType, ReplacementTypes);

    // Verify callee and caller agree on how the promoted argument would be
    // passed.
    Function &Fn = *getIRPosition().getAnchorScope();
    const auto *TTI =
        A.getInfoCache().getAnalysisResultForFunction<TargetIRAnalysis>(Fn);
    if (!TTI) {
      LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] Missing TTI for function "
                        << Fn.getName() << "\n");
      return indicatePessimisticFixpoint();
    }

    auto CallSiteCheck = [&](AbstractCallSite ACS) {
      CallBase *CB = ACS.getInstruction();
      return TTI->areTypesABICompatible(
          CB->getCaller(),
          dyn_cast_if_present<Function>(CB->getCalledOperand()),
          ReplacementTypes);
    };
    bool UsedAssumedInformation = false;
    if (!A.checkForAllCallSites(CallSiteCheck, *this, true,
                                UsedAssumedInformation)) {
      LLVM_DEBUG(
          dbgs() << "[AAPrivatizablePtr] ABI incompatibility detected for "
                 << Fn.getName() << "\n");
      return indicatePessimisticFixpoint();
    }

    // Register a rewrite of the argument.
    Argument *Arg = getAssociatedArgument();
    if (!A.isValidFunctionSignatureRewrite(*Arg, ReplacementTypes)) {
      LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] Rewrite not valid\n");
      return indicatePessimisticFixpoint();
    }

    unsigned ArgNo = Arg->getArgNo();

    // Helper to check if for the given call site the associated argument is
    // passed to a callback where the privatization would be different.
    auto IsCompatiblePrivArgOfCallback = [&](CallBase &CB) {
      SmallVector<const Use *, 4> CallbackUses;
      AbstractCallSite::getCallbackUses(CB, CallbackUses);
      for (const Use *U : CallbackUses) {
        AbstractCallSite CBACS(U);
        assert(CBACS && CBACS.isCallbackCall());
        for (Argument &CBArg : CBACS.getCalledFunction()->args()) {
          int CBArgNo = CBACS.getCallArgOperandNo(CBArg);

          LLVM_DEBUG({
            dbgs()
                << "[AAPrivatizablePtr] Argument " << *Arg
                << "check if can be privatized in the context of its parent ("
                << Arg->getParent()->getName()
                << ")\n[AAPrivatizablePtr] because it is an argument in a "
                   "callback ("
                << CBArgNo << "@" << CBACS.getCalledFunction()->getName()
                << ")\n[AAPrivatizablePtr] " << CBArg << " : "
                << CBACS.getCallArgOperand(CBArg) << " vs "
                << CB.getArgOperand(ArgNo) << "\n"
                << "[AAPrivatizablePtr] " << CBArg << " : "
                << CBACS.getCallArgOperandNo(CBArg) << " vs " << ArgNo << "\n";
          });

          if (CBArgNo != int(ArgNo))
            continue;
          const auto *CBArgPrivAA = A.getAAFor<AAPrivatizablePtr>(
              *this, IRPosition::argument(CBArg), DepClassTy::REQUIRED);
          if (CBArgPrivAA && CBArgPrivAA->isValidState()) {
            auto CBArgPrivTy = CBArgPrivAA->getPrivatizableType();
            if (!CBArgPrivTy)
              continue;
            if (*CBArgPrivTy == PrivatizableType)
              continue;
          }

          LLVM_DEBUG({
            dbgs() << "[AAPrivatizablePtr] Argument " << *Arg
                   << " cannot be privatized in the context of its parent ("
                   << Arg->getParent()->getName()
                   << ")\n[AAPrivatizablePtr] because it is an argument in a "
                      "callback ("
                   << CBArgNo << "@" << CBACS.getCalledFunction()->getName()
                   << ").\n[AAPrivatizablePtr] for which the argument "
                      "privatization is not compatible.\n";
          });
          return false;
        }
      }
      return true;
    };

    // Helper to check if for the given call site the associated argument is
    // passed to a direct call where the privatization would be different.
    auto IsCompatiblePrivArgOfDirectCS = [&](AbstractCallSite ACS) {
      CallBase *DC = cast<CallBase>(ACS.getInstruction());
      int DCArgNo = ACS.getCallArgOperandNo(ArgNo);
      assert(DCArgNo >= 0 && unsigned(DCArgNo) < DC->arg_size() &&
             "Expected a direct call operand for callback call operand");

      Function *DCCallee =
          dyn_cast_if_present<Function>(DC->getCalledOperand());
      LLVM_DEBUG({
        dbgs() << "[AAPrivatizablePtr] Argument " << *Arg
               << " check if be privatized in the context of its parent ("
               << Arg->getParent()->getName()
               << ")\n[AAPrivatizablePtr] because it is an argument in a "
                  "direct call of ("
               << DCArgNo << "@" << DCCallee->getName() << ").\n";
      });

      if (unsigned(DCArgNo) < DCCallee->arg_size()) {
        const auto *DCArgPrivAA = A.getAAFor<AAPrivatizablePtr>(
            *this, IRPosition::argument(*DCCallee->getArg(DCArgNo)),
            DepClassTy::REQUIRED);
        if (DCArgPrivAA && DCArgPrivAA->isValidState()) {
          auto DCArgPrivTy = DCArgPrivAA->getPrivatizableType();
          if (!DCArgPrivTy)
            return true;
          if (*DCArgPrivTy == PrivatizableType)
            return true;
        }
      }

      LLVM_DEBUG({
        dbgs() << "[AAPrivatizablePtr] Argument " << *Arg
               << " cannot be privatized in the context of its parent ("
               << Arg->getParent()->getName()
               << ")\n[AAPrivatizablePtr] because it is an argument in a "
                  "direct call of ("
               << ACS.getInstruction()->getCalledOperand()->getName()
               << ").\n[AAPrivatizablePtr] for which the argument "
                  "privatization is not compatible.\n";
      });
      return false;
    };

    // Helper to check if the associated argument is used at the given abstract
    // call site in a way that is incompatible with the privatization assumed
    // here.
    auto IsCompatiblePrivArgOfOtherCallSite = [&](AbstractCallSite ACS) {
      if (ACS.isDirectCall())
        return IsCompatiblePrivArgOfCallback(*ACS.getInstruction());
      if (ACS.isCallbackCall())
        return IsCompatiblePrivArgOfDirectCS(ACS);
      return false;
    };

    if (!A.checkForAllCallSites(IsCompatiblePrivArgOfOtherCallSite, *this, true,
                                UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }

  /// Given a type to private \p PrivType, collect the constituates (which are
  /// used) in \p ReplacementTypes.
  static void
  identifyReplacementTypes(Type *PrivType,
                           SmallVectorImpl<Type *> &ReplacementTypes) {
    // TODO: For now we expand the privatization type to the fullest which can
    //       lead to dead arguments that need to be removed later.
    assert(PrivType && "Expected privatizable type!");

    // Traverse the type, extract constituate types on the outermost level.
    if (auto *PrivStructType = dyn_cast<StructType>(PrivType)) {
      for (unsigned u = 0, e = PrivStructType->getNumElements(); u < e; u++)
        ReplacementTypes.push_back(PrivStructType->getElementType(u));
    } else if (auto *PrivArrayType = dyn_cast<ArrayType>(PrivType)) {
      ReplacementTypes.append(PrivArrayType->getNumElements(),
                              PrivArrayType->getElementType());
    } else {
      ReplacementTypes.push_back(PrivType);
    }
  }

  /// Initialize \p Base according to the type \p PrivType at position \p IP.
  /// The values needed are taken from the arguments of \p F starting at
  /// position \p ArgNo.
  static void createInitialization(Type *PrivType, Value &Base, Function &F,
                                   unsigned ArgNo, BasicBlock::iterator IP) {
    assert(PrivType && "Expected privatizable type!");

    IRBuilder<NoFolder> IRB(IP->getParent(), IP);
    const DataLayout &DL = F.getDataLayout();

    // Traverse the type, build GEPs and stores.
    if (auto *PrivStructType = dyn_cast<StructType>(PrivType)) {
      const StructLayout *PrivStructLayout = DL.getStructLayout(PrivStructType);
      for (unsigned u = 0, e = PrivStructType->getNumElements(); u < e; u++) {
        Value *Ptr =
            constructPointer(&Base, PrivStructLayout->getElementOffset(u), IRB);
        new StoreInst(F.getArg(ArgNo + u), Ptr, IP);
      }
    } else if (auto *PrivArrayType = dyn_cast<ArrayType>(PrivType)) {
      Type *PointeeTy = PrivArrayType->getElementType();
      uint64_t PointeeTySize = DL.getTypeStoreSize(PointeeTy);
      for (unsigned u = 0, e = PrivArrayType->getNumElements(); u < e; u++) {
        Value *Ptr = constructPointer(&Base, u * PointeeTySize, IRB);
        new StoreInst(F.getArg(ArgNo + u), Ptr, IP);
      }
    } else {
      new StoreInst(F.getArg(ArgNo), &Base, IP);
    }
  }

  /// Extract values from \p Base according to the type \p PrivType at the
  /// call position \p ACS. The values are appended to \p ReplacementValues.
  void createReplacementValues(Align Alignment, Type *PrivType,
                               AbstractCallSite ACS, Value *Base,
                               SmallVectorImpl<Value *> &ReplacementValues) {
    assert(Base && "Expected base value!");
    assert(PrivType && "Expected privatizable type!");
    Instruction *IP = ACS.getInstruction();

    IRBuilder<NoFolder> IRB(IP);
    const DataLayout &DL = IP->getDataLayout();

    // Traverse the type, build GEPs and loads.
    if (auto *PrivStructType = dyn_cast<StructType>(PrivType)) {
      const StructLayout *PrivStructLayout = DL.getStructLayout(PrivStructType);
      for (unsigned u = 0, e = PrivStructType->getNumElements(); u < e; u++) {
        Type *PointeeTy = PrivStructType->getElementType(u);
        Value *Ptr =
            constructPointer(Base, PrivStructLayout->getElementOffset(u), IRB);
        LoadInst *L = new LoadInst(PointeeTy, Ptr, "", IP->getIterator());
        L->setAlignment(Alignment);
        ReplacementValues.push_back(L);
      }
    } else if (auto *PrivArrayType = dyn_cast<ArrayType>(PrivType)) {
      Type *PointeeTy = PrivArrayType->getElementType();
      uint64_t PointeeTySize = DL.getTypeStoreSize(PointeeTy);
      for (unsigned u = 0, e = PrivArrayType->getNumElements(); u < e; u++) {
        Value *Ptr = constructPointer(Base, u * PointeeTySize, IRB);
        LoadInst *L = new LoadInst(PointeeTy, Ptr, "", IP->getIterator());
        L->setAlignment(Alignment);
        ReplacementValues.push_back(L);
      }
    } else {
      LoadInst *L = new LoadInst(PrivType, Base, "", IP->getIterator());
      L->setAlignment(Alignment);
      ReplacementValues.push_back(L);
    }
  }

  /// See AbstractAttribute::manifest(...)
  ChangeStatus manifest(Attributor &A) override {
    if (!PrivatizableType)
      return ChangeStatus::UNCHANGED;
    assert(*PrivatizableType && "Expected privatizable type!");

    // Collect all tail calls in the function as we cannot allow new allocas to
    // escape into tail recursion.
    // TODO: Be smarter about new allocas escaping into tail calls.
    SmallVector<CallInst *, 16> TailCalls;
    bool UsedAssumedInformation = false;
    if (!A.checkForAllInstructions(
            [&](Instruction &I) {
              CallInst &CI = cast<CallInst>(I);
              if (CI.isTailCall())
                TailCalls.push_back(&CI);
              return true;
            },
            *this, {Instruction::Call}, UsedAssumedInformation))
      return ChangeStatus::UNCHANGED;

    Argument *Arg = getAssociatedArgument();
    // Query AAAlign attribute for alignment of associated argument to
    // determine the best alignment of loads.
    const auto *AlignAA =
        A.getAAFor<AAAlign>(*this, IRPosition::value(*Arg), DepClassTy::NONE);

    // Callback to repair the associated function. A new alloca is placed at the
    // beginning and initialized with the values passed through arguments. The
    // new alloca replaces the use of the old pointer argument.
    Attributor::ArgumentReplacementInfo::CalleeRepairCBTy FnRepairCB =
        [=](const Attributor::ArgumentReplacementInfo &ARI,
            Function &ReplacementFn, Function::arg_iterator ArgIt) {
          BasicBlock &EntryBB = ReplacementFn.getEntryBlock();
          BasicBlock::iterator IP = EntryBB.getFirstInsertionPt();
          const DataLayout &DL = IP->getDataLayout();
          unsigned AS = DL.getAllocaAddrSpace();
          Instruction *AI = new AllocaInst(*PrivatizableType, AS,
                                           Arg->getName() + ".priv", IP);
          createInitialization(*PrivatizableType, *AI, ReplacementFn,
                               ArgIt->getArgNo(), IP);

          if (AI->getType() != Arg->getType())
            AI = BitCastInst::CreatePointerBitCastOrAddrSpaceCast(
                AI, Arg->getType(), "", IP);
          Arg->replaceAllUsesWith(AI);

          for (CallInst *CI : TailCalls)
            CI->setTailCall(false);
        };

    // Callback to repair a call site of the associated function. The elements
    // of the privatizable type are loaded prior to the call and passed to the
    // new function version.
    Attributor::ArgumentReplacementInfo::ACSRepairCBTy ACSRepairCB =
        [=](const Attributor::ArgumentReplacementInfo &ARI,
            AbstractCallSite ACS, SmallVectorImpl<Value *> &NewArgOperands) {
          // When no alignment is specified for the load instruction,
          // natural alignment is assumed.
          createReplacementValues(
              AlignAA ? AlignAA->getAssumedAlign() : Align(0),
              *PrivatizableType, ACS,
              ACS.getCallArgOperand(ARI.getReplacedArg().getArgNo()),
              NewArgOperands);
        };

    // Collect the types that will replace the privatizable type in the function
    // signature.
    SmallVector<Type *, 16> ReplacementTypes;
    identifyReplacementTypes(*PrivatizableType, ReplacementTypes);

    // Register a rewrite of the argument.
    if (A.registerFunctionSignatureRewrite(*Arg, ReplacementTypes,
                                           std::move(FnRepairCB),
                                           std::move(ACSRepairCB)))
      return ChangeStatus::CHANGED;
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_ARG_ATTR(privatizable_ptr);
  }
};

struct AAPrivatizablePtrFloating : public AAPrivatizablePtrImpl {
  AAPrivatizablePtrFloating(const IRPosition &IRP, Attributor &A)
      : AAPrivatizablePtrImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // TODO: We can privatize more than arguments.
    indicatePessimisticFixpoint();
  }

  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable("AAPrivatizablePtr(Floating|Returned|CallSiteReturned)::"
                     "updateImpl will not be called");
  }

  /// See AAPrivatizablePtrImpl::identifyPrivatizableType(...)
  std::optional<Type *> identifyPrivatizableType(Attributor &A) override {
    Value *Obj = getUnderlyingObject(&getAssociatedValue());
    if (!Obj) {
      LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] No underlying object found!\n");
      return nullptr;
    }

    if (auto *AI = dyn_cast<AllocaInst>(Obj))
      if (auto *CI = dyn_cast<ConstantInt>(AI->getArraySize()))
        if (CI->isOne())
          return AI->getAllocatedType();
    if (auto *Arg = dyn_cast<Argument>(Obj)) {
      auto *PrivArgAA = A.getAAFor<AAPrivatizablePtr>(
          *this, IRPosition::argument(*Arg), DepClassTy::REQUIRED);
      if (PrivArgAA && PrivArgAA->isAssumedPrivatizablePtr())
        return PrivArgAA->getPrivatizableType();
    }

    LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] Underlying object neither valid "
                         "alloca nor privatizable argument: "
                      << *Obj << "!\n");
    return nullptr;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(privatizable_ptr);
  }
};

struct AAPrivatizablePtrCallSiteArgument final
    : public AAPrivatizablePtrFloating {
  AAPrivatizablePtrCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAPrivatizablePtrFloating(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    if (A.hasAttr(getIRPosition(), Attribute::ByVal))
      indicateOptimisticFixpoint();
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    PrivatizableType = identifyPrivatizableType(A);
    if (!PrivatizableType)
      return ChangeStatus::UNCHANGED;
    if (!*PrivatizableType)
      return indicatePessimisticFixpoint();

    const IRPosition &IRP = getIRPosition();
    bool IsKnownNoCapture;
    bool IsAssumedNoCapture = AA::hasAssumedIRAttr<Attribute::NoCapture>(
        A, this, IRP, DepClassTy::REQUIRED, IsKnownNoCapture);
    if (!IsAssumedNoCapture) {
      LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] pointer might be captured!\n");
      return indicatePessimisticFixpoint();
    }

    bool IsKnownNoAlias;
    if (!AA::hasAssumedIRAttr<Attribute::NoAlias>(
            A, this, IRP, DepClassTy::REQUIRED, IsKnownNoAlias)) {
      LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] pointer might alias!\n");
      return indicatePessimisticFixpoint();
    }

    bool IsKnown;
    if (!AA::isAssumedReadOnly(A, IRP, *this, IsKnown)) {
      LLVM_DEBUG(dbgs() << "[AAPrivatizablePtr] pointer is written!\n");
      return indicatePessimisticFixpoint();
    }

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(privatizable_ptr);
  }
};

struct AAPrivatizablePtrCallSiteReturned final
    : public AAPrivatizablePtrFloating {
  AAPrivatizablePtrCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAPrivatizablePtrFloating(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // TODO: We can privatize more than arguments.
    indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(privatizable_ptr);
  }
};

struct AAPrivatizablePtrReturned final : public AAPrivatizablePtrFloating {
  AAPrivatizablePtrReturned(const IRPosition &IRP, Attributor &A)
      : AAPrivatizablePtrFloating(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // TODO: We can privatize more than arguments.
    indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(privatizable_ptr);
  }
};
} // namespace

/// -------------------- Memory Behavior Attributes ----------------------------
/// Includes read-none, read-only, and write-only.
/// ----------------------------------------------------------------------------
namespace {
struct AAMemoryBehaviorImpl : public AAMemoryBehavior {
  AAMemoryBehaviorImpl(const IRPosition &IRP, Attributor &A)
      : AAMemoryBehavior(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    intersectAssumedBits(BEST_STATE);
    getKnownStateFromValue(A, getIRPosition(), getState());
    AAMemoryBehavior::initialize(A);
  }

  /// Return the memory behavior information encoded in the IR for \p IRP.
  static void getKnownStateFromValue(Attributor &A, const IRPosition &IRP,
                                     BitIntegerState &State,
                                     bool IgnoreSubsumingPositions = false) {
    SmallVector<Attribute, 2> Attrs;
    A.getAttrs(IRP, AttrKinds, Attrs, IgnoreSubsumingPositions);
    for (const Attribute &Attr : Attrs) {
      switch (Attr.getKindAsEnum()) {
      case Attribute::ReadNone:
        State.addKnownBits(NO_ACCESSES);
        break;
      case Attribute::ReadOnly:
        State.addKnownBits(NO_WRITES);
        break;
      case Attribute::WriteOnly:
        State.addKnownBits(NO_READS);
        break;
      default:
        llvm_unreachable("Unexpected attribute!");
      }
    }

    if (auto *I = dyn_cast<Instruction>(&IRP.getAnchorValue())) {
      if (!I->mayReadFromMemory())
        State.addKnownBits(NO_READS);
      if (!I->mayWriteToMemory())
        State.addKnownBits(NO_WRITES);
    }
  }

  /// See AbstractAttribute::getDeducedAttributes(...).
  void getDeducedAttributes(Attributor &A, LLVMContext &Ctx,
                            SmallVectorImpl<Attribute> &Attrs) const override {
    assert(Attrs.size() == 0);
    if (isAssumedReadNone())
      Attrs.push_back(Attribute::get(Ctx, Attribute::ReadNone));
    else if (isAssumedReadOnly())
      Attrs.push_back(Attribute::get(Ctx, Attribute::ReadOnly));
    else if (isAssumedWriteOnly())
      Attrs.push_back(Attribute::get(Ctx, Attribute::WriteOnly));
    assert(Attrs.size() <= 1);
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    const IRPosition &IRP = getIRPosition();

    if (A.hasAttr(IRP, Attribute::ReadNone,
                  /* IgnoreSubsumingPositions */ true))
      return ChangeStatus::UNCHANGED;

    // Check if we would improve the existing attributes first.
    SmallVector<Attribute, 4> DeducedAttrs;
    getDeducedAttributes(A, IRP.getAnchorValue().getContext(), DeducedAttrs);
    if (llvm::all_of(DeducedAttrs, [&](const Attribute &Attr) {
          return A.hasAttr(IRP, Attr.getKindAsEnum(),
                           /* IgnoreSubsumingPositions */ true);
        }))
      return ChangeStatus::UNCHANGED;

    // Clear existing attributes.
    A.removeAttrs(IRP, AttrKinds);
    // Clear conflicting writable attribute.
    if (isAssumedReadOnly())
      A.removeAttrs(IRP, Attribute::Writable);

    // Use the generic manifest method.
    return IRAttribute::manifest(A);
  }

  /// See AbstractState::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    if (isAssumedReadNone())
      return "readnone";
    if (isAssumedReadOnly())
      return "readonly";
    if (isAssumedWriteOnly())
      return "writeonly";
    return "may-read/write";
  }

  /// The set of IR attributes AAMemoryBehavior deals with.
  static const Attribute::AttrKind AttrKinds[3];
};

const Attribute::AttrKind AAMemoryBehaviorImpl::AttrKinds[] = {
    Attribute::ReadNone, Attribute::ReadOnly, Attribute::WriteOnly};

/// Memory behavior attribute for a floating value.
struct AAMemoryBehaviorFloating : AAMemoryBehaviorImpl {
  AAMemoryBehaviorFloating(const IRPosition &IRP, Attributor &A)
      : AAMemoryBehaviorImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override;

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (isAssumedReadNone())
      STATS_DECLTRACK_FLOATING_ATTR(readnone)
    else if (isAssumedReadOnly())
      STATS_DECLTRACK_FLOATING_ATTR(readonly)
    else if (isAssumedWriteOnly())
      STATS_DECLTRACK_FLOATING_ATTR(writeonly)
  }

private:
  /// Return true if users of \p UserI might access the underlying
  /// variable/location described by \p U and should therefore be analyzed.
  bool followUsersOfUseIn(Attributor &A, const Use &U,
                          const Instruction *UserI);

  /// Update the state according to the effect of use \p U in \p UserI.
  void analyzeUseIn(Attributor &A, const Use &U, const Instruction *UserI);
};

/// Memory behavior attribute for function argument.
struct AAMemoryBehaviorArgument : AAMemoryBehaviorFloating {
  AAMemoryBehaviorArgument(const IRPosition &IRP, Attributor &A)
      : AAMemoryBehaviorFloating(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    intersectAssumedBits(BEST_STATE);
    const IRPosition &IRP = getIRPosition();
    // TODO: Make IgnoreSubsumingPositions a property of an IRAttribute so we
    // can query it when we use has/getAttr. That would allow us to reuse the
    // initialize of the base class here.
    bool HasByVal = A.hasAttr(IRP, {Attribute::ByVal},
                              /* IgnoreSubsumingPositions */ true);
    getKnownStateFromValue(A, IRP, getState(),
                           /* IgnoreSubsumingPositions */ HasByVal);
  }

  ChangeStatus manifest(Attributor &A) override {
    // TODO: Pointer arguments are not supported on vectors of pointers yet.
    if (!getAssociatedValue().getType()->isPointerTy())
      return ChangeStatus::UNCHANGED;

    // TODO: From readattrs.ll: "inalloca parameters are always
    //                           considered written"
    if (A.hasAttr(getIRPosition(),
                  {Attribute::InAlloca, Attribute::Preallocated})) {
      removeKnownBits(NO_WRITES);
      removeAssumedBits(NO_WRITES);
    }
    A.removeAttrs(getIRPosition(), AttrKinds);
    return AAMemoryBehaviorFloating::manifest(A);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (isAssumedReadNone())
      STATS_DECLTRACK_ARG_ATTR(readnone)
    else if (isAssumedReadOnly())
      STATS_DECLTRACK_ARG_ATTR(readonly)
    else if (isAssumedWriteOnly())
      STATS_DECLTRACK_ARG_ATTR(writeonly)
  }
};

struct AAMemoryBehaviorCallSiteArgument final : AAMemoryBehaviorArgument {
  AAMemoryBehaviorCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAMemoryBehaviorArgument(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // If we don't have an associated attribute this is either a variadic call
    // or an indirect call, either way, nothing to do here.
    Argument *Arg = getAssociatedArgument();
    if (!Arg) {
      indicatePessimisticFixpoint();
      return;
    }
    if (Arg->hasByValAttr()) {
      addKnownBits(NO_WRITES);
      removeKnownBits(NO_READS);
      removeAssumedBits(NO_READS);
    }
    AAMemoryBehaviorArgument::initialize(A);
    if (getAssociatedFunction()->isDeclaration())
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    Argument *Arg = getAssociatedArgument();
    const IRPosition &ArgPos = IRPosition::argument(*Arg);
    auto *ArgAA =
        A.getAAFor<AAMemoryBehavior>(*this, ArgPos, DepClassTy::REQUIRED);
    if (!ArgAA)
      return indicatePessimisticFixpoint();
    return clampStateAndIndicateChange(getState(), ArgAA->getState());
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (isAssumedReadNone())
      STATS_DECLTRACK_CSARG_ATTR(readnone)
    else if (isAssumedReadOnly())
      STATS_DECLTRACK_CSARG_ATTR(readonly)
    else if (isAssumedWriteOnly())
      STATS_DECLTRACK_CSARG_ATTR(writeonly)
  }
};

/// Memory behavior attribute for a call site return position.
struct AAMemoryBehaviorCallSiteReturned final : AAMemoryBehaviorFloating {
  AAMemoryBehaviorCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAMemoryBehaviorFloating(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAMemoryBehaviorImpl::initialize(A);
  }
  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // We do not annotate returned values.
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}
};

/// An AA to represent the memory behavior function attributes.
struct AAMemoryBehaviorFunction final : public AAMemoryBehaviorImpl {
  AAMemoryBehaviorFunction(const IRPosition &IRP, Attributor &A)
      : AAMemoryBehaviorImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(Attributor &A).
  ChangeStatus updateImpl(Attributor &A) override;

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // TODO: It would be better to merge this with AAMemoryLocation, so that
    // we could determine read/write per location. This would also have the
    // benefit of only one place trying to manifest the memory attribute.
    Function &F = cast<Function>(getAnchorValue());
    MemoryEffects ME = MemoryEffects::unknown();
    if (isAssumedReadNone())
      ME = MemoryEffects::none();
    else if (isAssumedReadOnly())
      ME = MemoryEffects::readOnly();
    else if (isAssumedWriteOnly())
      ME = MemoryEffects::writeOnly();

    A.removeAttrs(getIRPosition(), AttrKinds);
    // Clear conflicting writable attribute.
    if (ME.onlyReadsMemory())
      for (Argument &Arg : F.args())
        A.removeAttrs(IRPosition::argument(Arg), Attribute::Writable);
    return A.manifestAttrs(getIRPosition(),
                           Attribute::getWithMemoryEffects(F.getContext(), ME));
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (isAssumedReadNone())
      STATS_DECLTRACK_FN_ATTR(readnone)
    else if (isAssumedReadOnly())
      STATS_DECLTRACK_FN_ATTR(readonly)
    else if (isAssumedWriteOnly())
      STATS_DECLTRACK_FN_ATTR(writeonly)
  }
};

/// AAMemoryBehavior attribute for call sites.
struct AAMemoryBehaviorCallSite final
    : AACalleeToCallSite<AAMemoryBehavior, AAMemoryBehaviorImpl> {
  AAMemoryBehaviorCallSite(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AAMemoryBehavior, AAMemoryBehaviorImpl>(IRP, A) {}

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // TODO: Deduplicate this with AAMemoryBehaviorFunction.
    CallBase &CB = cast<CallBase>(getAnchorValue());
    MemoryEffects ME = MemoryEffects::unknown();
    if (isAssumedReadNone())
      ME = MemoryEffects::none();
    else if (isAssumedReadOnly())
      ME = MemoryEffects::readOnly();
    else if (isAssumedWriteOnly())
      ME = MemoryEffects::writeOnly();

    A.removeAttrs(getIRPosition(), AttrKinds);
    // Clear conflicting writable attribute.
    if (ME.onlyReadsMemory())
      for (Use &U : CB.args())
        A.removeAttrs(IRPosition::callsite_argument(CB, U.getOperandNo()),
                      Attribute::Writable);
    return A.manifestAttrs(
        getIRPosition(), Attribute::getWithMemoryEffects(CB.getContext(), ME));
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (isAssumedReadNone())
      STATS_DECLTRACK_CS_ATTR(readnone)
    else if (isAssumedReadOnly())
      STATS_DECLTRACK_CS_ATTR(readonly)
    else if (isAssumedWriteOnly())
      STATS_DECLTRACK_CS_ATTR(writeonly)
  }
};

ChangeStatus AAMemoryBehaviorFunction::updateImpl(Attributor &A) {

  // The current assumed state used to determine a change.
  auto AssumedState = getAssumed();

  auto CheckRWInst = [&](Instruction &I) {
    // If the instruction has an own memory behavior state, use it to restrict
    // the local state. No further analysis is required as the other memory
    // state is as optimistic as it gets.
    if (const auto *CB = dyn_cast<CallBase>(&I)) {
      const auto *MemBehaviorAA = A.getAAFor<AAMemoryBehavior>(
          *this, IRPosition::callsite_function(*CB), DepClassTy::REQUIRED);
      if (MemBehaviorAA) {
        intersectAssumedBits(MemBehaviorAA->getAssumed());
        return !isAtFixpoint();
      }
    }

    // Remove access kind modifiers if necessary.
    if (I.mayReadFromMemory())
      removeAssumedBits(NO_READS);
    if (I.mayWriteToMemory())
      removeAssumedBits(NO_WRITES);
    return !isAtFixpoint();
  };

  bool UsedAssumedInformation = false;
  if (!A.checkForAllReadWriteInstructions(CheckRWInst, *this,
                                          UsedAssumedInformation))
    return indicatePessimisticFixpoint();

  return (AssumedState != getAssumed()) ? ChangeStatus::CHANGED
                                        : ChangeStatus::UNCHANGED;
}

ChangeStatus AAMemoryBehaviorFloating::updateImpl(Attributor &A) {

  const IRPosition &IRP = getIRPosition();
  const IRPosition &FnPos = IRPosition::function_scope(IRP);
  AAMemoryBehavior::StateType &S = getState();

  // First, check the function scope. We take the known information and we avoid
  // work if the assumed information implies the current assumed information for
  // this attribute. This is a valid for all but byval arguments.
  Argument *Arg = IRP.getAssociatedArgument();
  AAMemoryBehavior::base_t FnMemAssumedState =
      AAMemoryBehavior::StateType::getWorstState();
  if (!Arg || !Arg->hasByValAttr()) {
    const auto *FnMemAA =
        A.getAAFor<AAMemoryBehavior>(*this, FnPos, DepClassTy::OPTIONAL);
    if (FnMemAA) {
      FnMemAssumedState = FnMemAA->getAssumed();
      S.addKnownBits(FnMemAA->getKnown());
      if ((S.getAssumed() & FnMemAA->getAssumed()) == S.getAssumed())
        return ChangeStatus::UNCHANGED;
    }
  }

  // The current assumed state used to determine a change.
  auto AssumedState = S.getAssumed();

  // Make sure the value is not captured (except through "return"), if
  // it is, any information derived would be irrelevant anyway as we cannot
  // check the potential aliases introduced by the capture. However, no need
  // to fall back to anythign less optimistic than the function state.
  bool IsKnownNoCapture;
  const AANoCapture *ArgNoCaptureAA = nullptr;
  bool IsAssumedNoCapture = AA::hasAssumedIRAttr<Attribute::NoCapture>(
      A, this, IRP, DepClassTy::OPTIONAL, IsKnownNoCapture, false,
      &ArgNoCaptureAA);

  if (!IsAssumedNoCapture &&
      (!ArgNoCaptureAA || !ArgNoCaptureAA->isAssumedNoCaptureMaybeReturned())) {
    S.intersectAssumedBits(FnMemAssumedState);
    return (AssumedState != getAssumed()) ? ChangeStatus::CHANGED
                                          : ChangeStatus::UNCHANGED;
  }

  // Visit and expand uses until all are analyzed or a fixpoint is reached.
  auto UsePred = [&](const Use &U, bool &Follow) -> bool {
    Instruction *UserI = cast<Instruction>(U.getUser());
    LLVM_DEBUG(dbgs() << "[AAMemoryBehavior] Use: " << *U << " in " << *UserI
                      << " \n");

    // Droppable users, e.g., llvm::assume does not actually perform any action.
    if (UserI->isDroppable())
      return true;

    // Check if the users of UserI should also be visited.
    Follow = followUsersOfUseIn(A, U, UserI);

    // If UserI might touch memory we analyze the use in detail.
    if (UserI->mayReadOrWriteMemory())
      analyzeUseIn(A, U, UserI);

    return !isAtFixpoint();
  };

  if (!A.checkForAllUses(UsePred, *this, getAssociatedValue()))
    return indicatePessimisticFixpoint();

  return (AssumedState != getAssumed()) ? ChangeStatus::CHANGED
                                        : ChangeStatus::UNCHANGED;
}

bool AAMemoryBehaviorFloating::followUsersOfUseIn(Attributor &A, const Use &U,
                                                  const Instruction *UserI) {
  // The loaded value is unrelated to the pointer argument, no need to
  // follow the users of the load.
  if (isa<LoadInst>(UserI) || isa<ReturnInst>(UserI))
    return false;

  // By default we follow all uses assuming UserI might leak information on U,
  // we have special handling for call sites operands though.
  const auto *CB = dyn_cast<CallBase>(UserI);
  if (!CB || !CB->isArgOperand(&U))
    return true;

  // If the use is a call argument known not to be captured, the users of
  // the call do not need to be visited because they have to be unrelated to
  // the input. Note that this check is not trivial even though we disallow
  // general capturing of the underlying argument. The reason is that the
  // call might the argument "through return", which we allow and for which we
  // need to check call users.
  if (U.get()->getType()->isPointerTy()) {
    unsigned ArgNo = CB->getArgOperandNo(&U);
    bool IsKnownNoCapture;
    return !AA::hasAssumedIRAttr<Attribute::NoCapture>(
        A, this, IRPosition::callsite_argument(*CB, ArgNo),
        DepClassTy::OPTIONAL, IsKnownNoCapture);
  }

  return true;
}

void AAMemoryBehaviorFloating::analyzeUseIn(Attributor &A, const Use &U,
                                            const Instruction *UserI) {
  assert(UserI->mayReadOrWriteMemory());

  switch (UserI->getOpcode()) {
  default:
    // TODO: Handle all atomics and other side-effect operations we know of.
    break;
  case Instruction::Load:
    // Loads cause the NO_READS property to disappear.
    removeAssumedBits(NO_READS);
    return;

  case Instruction::Store:
    // Stores cause the NO_WRITES property to disappear if the use is the
    // pointer operand. Note that while capturing was taken care of somewhere
    // else we need to deal with stores of the value that is not looked through.
    if (cast<StoreInst>(UserI)->getPointerOperand() == U.get())
      removeAssumedBits(NO_WRITES);
    else
      indicatePessimisticFixpoint();
    return;

  case Instruction::Call:
  case Instruction::CallBr:
  case Instruction::Invoke: {
    // For call sites we look at the argument memory behavior attribute (this
    // could be recursive!) in order to restrict our own state.
    const auto *CB = cast<CallBase>(UserI);

    // Give up on operand bundles.
    if (CB->isBundleOperand(&U)) {
      indicatePessimisticFixpoint();
      return;
    }

    // Calling a function does read the function pointer, maybe write it if the
    // function is self-modifying.
    if (CB->isCallee(&U)) {
      removeAssumedBits(NO_READS);
      break;
    }

    // Adjust the possible access behavior based on the information on the
    // argument.
    IRPosition Pos;
    if (U.get()->getType()->isPointerTy())
      Pos = IRPosition::callsite_argument(*CB, CB->getArgOperandNo(&U));
    else
      Pos = IRPosition::callsite_function(*CB);
    const auto *MemBehaviorAA =
        A.getAAFor<AAMemoryBehavior>(*this, Pos, DepClassTy::OPTIONAL);
    if (!MemBehaviorAA)
      break;
    // "assumed" has at most the same bits as the MemBehaviorAA assumed
    // and at least "known".
    intersectAssumedBits(MemBehaviorAA->getAssumed());
    return;
  }
  };

  // Generally, look at the "may-properties" and adjust the assumed state if we
  // did not trigger special handling before.
  if (UserI->mayReadFromMemory())
    removeAssumedBits(NO_READS);
  if (UserI->mayWriteToMemory())
    removeAssumedBits(NO_WRITES);
}
} // namespace

/// -------------------- Memory Locations Attributes ---------------------------
/// Includes read-none, argmemonly, inaccessiblememonly,
/// inaccessiblememorargmemonly
/// ----------------------------------------------------------------------------

std::string AAMemoryLocation::getMemoryLocationsAsStr(
    AAMemoryLocation::MemoryLocationsKind MLK) {
  if (0 == (MLK & AAMemoryLocation::NO_LOCATIONS))
    return "all memory";
  if (MLK == AAMemoryLocation::NO_LOCATIONS)
    return "no memory";
  std::string S = "memory:";
  if (0 == (MLK & AAMemoryLocation::NO_LOCAL_MEM))
    S += "stack,";
  if (0 == (MLK & AAMemoryLocation::NO_CONST_MEM))
    S += "constant,";
  if (0 == (MLK & AAMemoryLocation::NO_GLOBAL_INTERNAL_MEM))
    S += "internal global,";
  if (0 == (MLK & AAMemoryLocation::NO_GLOBAL_EXTERNAL_MEM))
    S += "external global,";
  if (0 == (MLK & AAMemoryLocation::NO_ARGUMENT_MEM))
    S += "argument,";
  if (0 == (MLK & AAMemoryLocation::NO_INACCESSIBLE_MEM))
    S += "inaccessible,";
  if (0 == (MLK & AAMemoryLocation::NO_MALLOCED_MEM))
    S += "malloced,";
  if (0 == (MLK & AAMemoryLocation::NO_UNKOWN_MEM))
    S += "unknown,";
  S.pop_back();
  return S;
}

namespace {
struct AAMemoryLocationImpl : public AAMemoryLocation {

  AAMemoryLocationImpl(const IRPosition &IRP, Attributor &A)
      : AAMemoryLocation(IRP, A), Allocator(A.Allocator) {
    AccessKind2Accesses.fill(nullptr);
  }

  ~AAMemoryLocationImpl() {
    // The AccessSets are allocated via a BumpPtrAllocator, we call
    // the destructor manually.
    for (AccessSet *AS : AccessKind2Accesses)
      if (AS)
        AS->~AccessSet();
  }

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    intersectAssumedBits(BEST_STATE);
    getKnownStateFromValue(A, getIRPosition(), getState());
    AAMemoryLocation::initialize(A);
  }

  /// Return the memory behavior information encoded in the IR for \p IRP.
  static void getKnownStateFromValue(Attributor &A, const IRPosition &IRP,
                                     BitIntegerState &State,
                                     bool IgnoreSubsumingPositions = false) {
    // For internal functions we ignore `argmemonly` and
    // `inaccessiblememorargmemonly` as we might break it via interprocedural
    // constant propagation. It is unclear if this is the best way but it is
    // unlikely this will cause real performance problems. If we are deriving
    // attributes for the anchor function we even remove the attribute in
    // addition to ignoring it.
    // TODO: A better way to handle this would be to add ~NO_GLOBAL_MEM /
    // MemoryEffects::Other as a possible location.
    bool UseArgMemOnly = true;
    Function *AnchorFn = IRP.getAnchorScope();
    if (AnchorFn && A.isRunOn(*AnchorFn))
      UseArgMemOnly = !AnchorFn->hasLocalLinkage();

    SmallVector<Attribute, 2> Attrs;
    A.getAttrs(IRP, {Attribute::Memory}, Attrs, IgnoreSubsumingPositions);
    for (const Attribute &Attr : Attrs) {
      // TODO: We can map MemoryEffects to Attributor locations more precisely.
      MemoryEffects ME = Attr.getMemoryEffects();
      if (ME.doesNotAccessMemory()) {
        State.addKnownBits(NO_LOCAL_MEM | NO_CONST_MEM);
        continue;
      }
      if (ME.onlyAccessesInaccessibleMem()) {
        State.addKnownBits(inverseLocation(NO_INACCESSIBLE_MEM, true, true));
        continue;
      }
      if (ME.onlyAccessesArgPointees()) {
        if (UseArgMemOnly)
          State.addKnownBits(inverseLocation(NO_ARGUMENT_MEM, true, true));
        else {
          // Remove location information, only keep read/write info.
          ME = MemoryEffects(ME.getModRef());
          A.manifestAttrs(IRP,
                          Attribute::getWithMemoryEffects(
                              IRP.getAnchorValue().getContext(), ME),
                          /*ForceReplace*/ true);
        }
        continue;
      }
      if (ME.onlyAccessesInaccessibleOrArgMem()) {
        if (UseArgMemOnly)
          State.addKnownBits(inverseLocation(
              NO_INACCESSIBLE_MEM | NO_ARGUMENT_MEM, true, true));
        else {
          // Remove location information, only keep read/write info.
          ME = MemoryEffects(ME.getModRef());
          A.manifestAttrs(IRP,
                          Attribute::getWithMemoryEffects(
                              IRP.getAnchorValue().getContext(), ME),
                          /*ForceReplace*/ true);
        }
        continue;
      }
    }
  }

  /// See AbstractAttribute::getDeducedAttributes(...).
  void getDeducedAttributes(Attributor &A, LLVMContext &Ctx,
                            SmallVectorImpl<Attribute> &Attrs) const override {
    // TODO: We can map Attributor locations to MemoryEffects more precisely.
    assert(Attrs.size() == 0);
    if (getIRPosition().getPositionKind() == IRPosition::IRP_FUNCTION) {
      if (isAssumedReadNone())
        Attrs.push_back(
            Attribute::getWithMemoryEffects(Ctx, MemoryEffects::none()));
      else if (isAssumedInaccessibleMemOnly())
        Attrs.push_back(Attribute::getWithMemoryEffects(
            Ctx, MemoryEffects::inaccessibleMemOnly()));
      else if (isAssumedArgMemOnly())
        Attrs.push_back(
            Attribute::getWithMemoryEffects(Ctx, MemoryEffects::argMemOnly()));
      else if (isAssumedInaccessibleOrArgMemOnly())
        Attrs.push_back(Attribute::getWithMemoryEffects(
            Ctx, MemoryEffects::inaccessibleOrArgMemOnly()));
    }
    assert(Attrs.size() <= 1);
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // TODO: If AAMemoryLocation and AAMemoryBehavior are merged, we could
    // provide per-location modref information here.
    const IRPosition &IRP = getIRPosition();

    SmallVector<Attribute, 1> DeducedAttrs;
    getDeducedAttributes(A, IRP.getAnchorValue().getContext(), DeducedAttrs);
    if (DeducedAttrs.size() != 1)
      return ChangeStatus::UNCHANGED;
    MemoryEffects ME = DeducedAttrs[0].getMemoryEffects();

    return A.manifestAttrs(IRP, Attribute::getWithMemoryEffects(
                                    IRP.getAnchorValue().getContext(), ME));
  }

  /// See AAMemoryLocation::checkForAllAccessesToMemoryKind(...).
  bool checkForAllAccessesToMemoryKind(
      function_ref<bool(const Instruction *, const Value *, AccessKind,
                        MemoryLocationsKind)>
          Pred,
      MemoryLocationsKind RequestedMLK) const override {
    if (!isValidState())
      return false;

    MemoryLocationsKind AssumedMLK = getAssumedNotAccessedLocation();
    if (AssumedMLK == NO_LOCATIONS)
      return true;

    unsigned Idx = 0;
    for (MemoryLocationsKind CurMLK = 1; CurMLK < NO_LOCATIONS;
         CurMLK *= 2, ++Idx) {
      if (CurMLK & RequestedMLK)
        continue;

      if (const AccessSet *Accesses = AccessKind2Accesses[Idx])
        for (const AccessInfo &AI : *Accesses)
          if (!Pred(AI.I, AI.Ptr, AI.Kind, CurMLK))
            return false;
    }

    return true;
  }

  ChangeStatus indicatePessimisticFixpoint() override {
    // If we give up and indicate a pessimistic fixpoint this instruction will
    // become an access for all potential access kinds:
    // TODO: Add pointers for argmemonly and globals to improve the results of
    //       checkForAllAccessesToMemoryKind.
    bool Changed = false;
    MemoryLocationsKind KnownMLK = getKnown();
    Instruction *I = dyn_cast<Instruction>(&getAssociatedValue());
    for (MemoryLocationsKind CurMLK = 1; CurMLK < NO_LOCATIONS; CurMLK *= 2)
      if (!(CurMLK & KnownMLK))
        updateStateAndAccessesMap(getState(), CurMLK, I, nullptr, Changed,
                                  getAccessKindFromInst(I));
    return AAMemoryLocation::indicatePessimisticFixpoint();
  }

protected:
  /// Helper struct to tie together an instruction that has a read or write
  /// effect with the pointer it accesses (if any).
  struct AccessInfo {

    /// The instruction that caused the access.
    const Instruction *I;

    /// The base pointer that is accessed, or null if unknown.
    const Value *Ptr;

    /// The kind of access (read/write/read+write).
    AccessKind Kind;

    bool operator==(const AccessInfo &RHS) const {
      return I == RHS.I && Ptr == RHS.Ptr && Kind == RHS.Kind;
    }
    bool operator()(const AccessInfo &LHS, const AccessInfo &RHS) const {
      if (LHS.I != RHS.I)
        return LHS.I < RHS.I;
      if (LHS.Ptr != RHS.Ptr)
        return LHS.Ptr < RHS.Ptr;
      if (LHS.Kind != RHS.Kind)
        return LHS.Kind < RHS.Kind;
      return false;
    }
  };

  /// Mapping from *single* memory location kinds, e.g., LOCAL_MEM with the
  /// value of NO_LOCAL_MEM, to the accesses encountered for this memory kind.
  using AccessSet = SmallSet<AccessInfo, 2, AccessInfo>;
  std::array<AccessSet *, llvm::CTLog2<VALID_STATE>()> AccessKind2Accesses;

  /// Categorize the pointer arguments of CB that might access memory in
  /// AccessedLoc and update the state and access map accordingly.
  void
  categorizeArgumentPointerLocations(Attributor &A, CallBase &CB,
                                     AAMemoryLocation::StateType &AccessedLocs,
                                     bool &Changed);

  /// Return the kind(s) of location that may be accessed by \p V.
  AAMemoryLocation::MemoryLocationsKind
  categorizeAccessedLocations(Attributor &A, Instruction &I, bool &Changed);

  /// Return the access kind as determined by \p I.
  AccessKind getAccessKindFromInst(const Instruction *I) {
    AccessKind AK = READ_WRITE;
    if (I) {
      AK = I->mayReadFromMemory() ? READ : NONE;
      AK = AccessKind(AK | (I->mayWriteToMemory() ? WRITE : NONE));
    }
    return AK;
  }

  /// Update the state \p State and the AccessKind2Accesses given that \p I is
  /// an access of kind \p AK to a \p MLK memory location with the access
  /// pointer \p Ptr.
  void updateStateAndAccessesMap(AAMemoryLocation::StateType &State,
                                 MemoryLocationsKind MLK, const Instruction *I,
                                 const Value *Ptr, bool &Changed,
                                 AccessKind AK = READ_WRITE) {

    assert(isPowerOf2_32(MLK) && "Expected a single location set!");
    auto *&Accesses = AccessKind2Accesses[llvm::Log2_32(MLK)];
    if (!Accesses)
      Accesses = new (Allocator) AccessSet();
    Changed |= Accesses->insert(AccessInfo{I, Ptr, AK}).second;
    if (MLK == NO_UNKOWN_MEM)
      MLK = NO_LOCATIONS;
    State.removeAssumedBits(MLK);
  }

  /// Determine the underlying locations kinds for \p Ptr, e.g., globals or
  /// arguments, and update the state and access map accordingly.
  void categorizePtrValue(Attributor &A, const Instruction &I, const Value &Ptr,
                          AAMemoryLocation::StateType &State, bool &Changed,
                          unsigned AccessAS = 0);

  /// Used to allocate access sets.
  BumpPtrAllocator &Allocator;
};

void AAMemoryLocationImpl::categorizePtrValue(
    Attributor &A, const Instruction &I, const Value &Ptr,
    AAMemoryLocation::StateType &State, bool &Changed, unsigned AccessAS) {
  LLVM_DEBUG(dbgs() << "[AAMemoryLocation] Categorize pointer locations for "
                    << Ptr << " ["
                    << getMemoryLocationsAsStr(State.getAssumed()) << "]\n");

  auto Pred = [&](Value &Obj) {
    unsigned ObjectAS = Obj.getType()->getPointerAddressSpace();
    // TODO: recognize the TBAA used for constant accesses.
    MemoryLocationsKind MLK = NO_LOCATIONS;

    // Filter accesses to constant (GPU) memory if we have an AS at the access
    // site or the object is known to actually have the associated AS.
    if ((AccessAS == (unsigned)AA::GPUAddressSpace::Constant ||
         (ObjectAS == (unsigned)AA::GPUAddressSpace::Constant &&
          isIdentifiedObject(&Obj))) &&
        AA::isGPU(*I.getModule()))
      return true;

    if (isa<UndefValue>(&Obj))
      return true;
    if (isa<Argument>(&Obj)) {
      // TODO: For now we do not treat byval arguments as local copies performed
      // on the call edge, though, we should. To make that happen we need to
      // teach various passes, e.g., DSE, about the copy effect of a byval. That
      // would also allow us to mark functions only accessing byval arguments as
      // readnone again, arguably their accesses have no effect outside of the
      // function, like accesses to allocas.
      MLK = NO_ARGUMENT_MEM;
    } else if (auto *GV = dyn_cast<GlobalValue>(&Obj)) {
      // Reading constant memory is not treated as a read "effect" by the
      // function attr pass so we won't neither. Constants defined by TBAA are
      // similar. (We know we do not write it because it is constant.)
      if (auto *GVar = dyn_cast<GlobalVariable>(GV))
        if (GVar->isConstant())
          return true;

      if (GV->hasLocalLinkage())
        MLK = NO_GLOBAL_INTERNAL_MEM;
      else
        MLK = NO_GLOBAL_EXTERNAL_MEM;
    } else if (isa<ConstantPointerNull>(&Obj) &&
               (!NullPointerIsDefined(getAssociatedFunction(), AccessAS) ||
                !NullPointerIsDefined(getAssociatedFunction(), ObjectAS))) {
      return true;
    } else if (isa<AllocaInst>(&Obj)) {
      MLK = NO_LOCAL_MEM;
    } else if (const auto *CB = dyn_cast<CallBase>(&Obj)) {
      bool IsKnownNoAlias;
      if (AA::hasAssumedIRAttr<Attribute::NoAlias>(
              A, this, IRPosition::callsite_returned(*CB), DepClassTy::OPTIONAL,
              IsKnownNoAlias))
        MLK = NO_MALLOCED_MEM;
      else
        MLK = NO_UNKOWN_MEM;
    } else {
      MLK = NO_UNKOWN_MEM;
    }

    assert(MLK != NO_LOCATIONS && "No location specified!");
    LLVM_DEBUG(dbgs() << "[AAMemoryLocation] Ptr value can be categorized: "
                      << Obj << " -> " << getMemoryLocationsAsStr(MLK) << "\n");
    updateStateAndAccessesMap(State, MLK, &I, &Obj, Changed,
                              getAccessKindFromInst(&I));

    return true;
  };

  const auto *AA = A.getAAFor<AAUnderlyingObjects>(
      *this, IRPosition::value(Ptr), DepClassTy::OPTIONAL);
  if (!AA || !AA->forallUnderlyingObjects(Pred, AA::Intraprocedural)) {
    LLVM_DEBUG(
        dbgs() << "[AAMemoryLocation] Pointer locations not categorized\n");
    updateStateAndAccessesMap(State, NO_UNKOWN_MEM, &I, nullptr, Changed,
                              getAccessKindFromInst(&I));
    return;
  }

  LLVM_DEBUG(
      dbgs() << "[AAMemoryLocation] Accessed locations with pointer locations: "
             << getMemoryLocationsAsStr(State.getAssumed()) << "\n");
}

void AAMemoryLocationImpl::categorizeArgumentPointerLocations(
    Attributor &A, CallBase &CB, AAMemoryLocation::StateType &AccessedLocs,
    bool &Changed) {
  for (unsigned ArgNo = 0, E = CB.arg_size(); ArgNo < E; ++ArgNo) {

    // Skip non-pointer arguments.
    const Value *ArgOp = CB.getArgOperand(ArgNo);
    if (!ArgOp->getType()->isPtrOrPtrVectorTy())
      continue;

    // Skip readnone arguments.
    const IRPosition &ArgOpIRP = IRPosition::callsite_argument(CB, ArgNo);
    const auto *ArgOpMemLocationAA =
        A.getAAFor<AAMemoryBehavior>(*this, ArgOpIRP, DepClassTy::OPTIONAL);

    if (ArgOpMemLocationAA && ArgOpMemLocationAA->isAssumedReadNone())
      continue;

    // Categorize potentially accessed pointer arguments as if there was an
    // access instruction with them as pointer.
    categorizePtrValue(A, CB, *ArgOp, AccessedLocs, Changed);
  }
}

AAMemoryLocation::MemoryLocationsKind
AAMemoryLocationImpl::categorizeAccessedLocations(Attributor &A, Instruction &I,
                                                  bool &Changed) {
  LLVM_DEBUG(dbgs() << "[AAMemoryLocation] Categorize accessed locations for "
                    << I << "\n");

  AAMemoryLocation::StateType AccessedLocs;
  AccessedLocs.intersectAssumedBits(NO_LOCATIONS);

  if (auto *CB = dyn_cast<CallBase>(&I)) {

    // First check if we assume any memory is access is visible.
    const auto *CBMemLocationAA = A.getAAFor<AAMemoryLocation>(
        *this, IRPosition::callsite_function(*CB), DepClassTy::OPTIONAL);
    LLVM_DEBUG(dbgs() << "[AAMemoryLocation] Categorize call site: " << I
                      << " [" << CBMemLocationAA << "]\n");
    if (!CBMemLocationAA) {
      updateStateAndAccessesMap(AccessedLocs, NO_UNKOWN_MEM, &I, nullptr,
                                Changed, getAccessKindFromInst(&I));
      return NO_UNKOWN_MEM;
    }

    if (CBMemLocationAA->isAssumedReadNone())
      return NO_LOCATIONS;

    if (CBMemLocationAA->isAssumedInaccessibleMemOnly()) {
      updateStateAndAccessesMap(AccessedLocs, NO_INACCESSIBLE_MEM, &I, nullptr,
                                Changed, getAccessKindFromInst(&I));
      return AccessedLocs.getAssumed();
    }

    uint32_t CBAssumedNotAccessedLocs =
        CBMemLocationAA->getAssumedNotAccessedLocation();

    // Set the argmemonly and global bit as we handle them separately below.
    uint32_t CBAssumedNotAccessedLocsNoArgMem =
        CBAssumedNotAccessedLocs | NO_ARGUMENT_MEM | NO_GLOBAL_MEM;

    for (MemoryLocationsKind CurMLK = 1; CurMLK < NO_LOCATIONS; CurMLK *= 2) {
      if (CBAssumedNotAccessedLocsNoArgMem & CurMLK)
        continue;
      updateStateAndAccessesMap(AccessedLocs, CurMLK, &I, nullptr, Changed,
                                getAccessKindFromInst(&I));
    }

    // Now handle global memory if it might be accessed. This is slightly tricky
    // as NO_GLOBAL_MEM has multiple bits set.
    bool HasGlobalAccesses = ((~CBAssumedNotAccessedLocs) & NO_GLOBAL_MEM);
    if (HasGlobalAccesses) {
      auto AccessPred = [&](const Instruction *, const Value *Ptr,
                            AccessKind Kind, MemoryLocationsKind MLK) {
        updateStateAndAccessesMap(AccessedLocs, MLK, &I, Ptr, Changed,
                                  getAccessKindFromInst(&I));
        return true;
      };
      if (!CBMemLocationAA->checkForAllAccessesToMemoryKind(
              AccessPred, inverseLocation(NO_GLOBAL_MEM, false, false)))
        return AccessedLocs.getWorstState();
    }

    LLVM_DEBUG(
        dbgs() << "[AAMemoryLocation] Accessed state before argument handling: "
               << getMemoryLocationsAsStr(AccessedLocs.getAssumed()) << "\n");

    // Now handle argument memory if it might be accessed.
    bool HasArgAccesses = ((~CBAssumedNotAccessedLocs) & NO_ARGUMENT_MEM);
    if (HasArgAccesses)
      categorizeArgumentPointerLocations(A, *CB, AccessedLocs, Changed);

    LLVM_DEBUG(
        dbgs() << "[AAMemoryLocation] Accessed state after argument handling: "
               << getMemoryLocationsAsStr(AccessedLocs.getAssumed()) << "\n");

    return AccessedLocs.getAssumed();
  }

  if (const Value *Ptr = getPointerOperand(&I, /* AllowVolatile */ true)) {
    LLVM_DEBUG(
        dbgs() << "[AAMemoryLocation] Categorize memory access with pointer: "
               << I << " [" << *Ptr << "]\n");
    categorizePtrValue(A, I, *Ptr, AccessedLocs, Changed,
                       Ptr->getType()->getPointerAddressSpace());
    return AccessedLocs.getAssumed();
  }

  LLVM_DEBUG(dbgs() << "[AAMemoryLocation] Failed to categorize instruction: "
                    << I << "\n");
  updateStateAndAccessesMap(AccessedLocs, NO_UNKOWN_MEM, &I, nullptr, Changed,
                            getAccessKindFromInst(&I));
  return AccessedLocs.getAssumed();
}

/// An AA to represent the memory behavior function attributes.
struct AAMemoryLocationFunction final : public AAMemoryLocationImpl {
  AAMemoryLocationFunction(const IRPosition &IRP, Attributor &A)
      : AAMemoryLocationImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(Attributor &A).
  ChangeStatus updateImpl(Attributor &A) override {

    const auto *MemBehaviorAA =
        A.getAAFor<AAMemoryBehavior>(*this, getIRPosition(), DepClassTy::NONE);
    if (MemBehaviorAA && MemBehaviorAA->isAssumedReadNone()) {
      if (MemBehaviorAA->isKnownReadNone())
        return indicateOptimisticFixpoint();
      assert(isAssumedReadNone() &&
             "AAMemoryLocation was not read-none but AAMemoryBehavior was!");
      A.recordDependence(*MemBehaviorAA, *this, DepClassTy::OPTIONAL);
      return ChangeStatus::UNCHANGED;
    }

    // The current assumed state used to determine a change.
    auto AssumedState = getAssumed();
    bool Changed = false;

    auto CheckRWInst = [&](Instruction &I) {
      MemoryLocationsKind MLK = categorizeAccessedLocations(A, I, Changed);
      LLVM_DEBUG(dbgs() << "[AAMemoryLocation] Accessed locations for " << I
                        << ": " << getMemoryLocationsAsStr(MLK) << "\n");
      removeAssumedBits(inverseLocation(MLK, false, false));
      // Stop once only the valid bit set in the *not assumed location*, thus
      // once we don't actually exclude any memory locations in the state.
      return getAssumedNotAccessedLocation() != VALID_STATE;
    };

    bool UsedAssumedInformation = false;
    if (!A.checkForAllReadWriteInstructions(CheckRWInst, *this,
                                            UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    Changed |= AssumedState != getAssumed();
    return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (isAssumedReadNone())
      STATS_DECLTRACK_FN_ATTR(readnone)
    else if (isAssumedArgMemOnly())
      STATS_DECLTRACK_FN_ATTR(argmemonly)
    else if (isAssumedInaccessibleMemOnly())
      STATS_DECLTRACK_FN_ATTR(inaccessiblememonly)
    else if (isAssumedInaccessibleOrArgMemOnly())
      STATS_DECLTRACK_FN_ATTR(inaccessiblememorargmemonly)
  }
};

/// AAMemoryLocation attribute for call sites.
struct AAMemoryLocationCallSite final : AAMemoryLocationImpl {
  AAMemoryLocationCallSite(const IRPosition &IRP, Attributor &A)
      : AAMemoryLocationImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    Function *F = getAssociatedFunction();
    const IRPosition &FnPos = IRPosition::function(*F);
    auto *FnAA =
        A.getAAFor<AAMemoryLocation>(*this, FnPos, DepClassTy::REQUIRED);
    if (!FnAA)
      return indicatePessimisticFixpoint();
    bool Changed = false;
    auto AccessPred = [&](const Instruction *I, const Value *Ptr,
                          AccessKind Kind, MemoryLocationsKind MLK) {
      updateStateAndAccessesMap(getState(), MLK, I, Ptr, Changed,
                                getAccessKindFromInst(I));
      return true;
    };
    if (!FnAA->checkForAllAccessesToMemoryKind(AccessPred, ALL_LOCATIONS))
      return indicatePessimisticFixpoint();
    return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    if (isAssumedReadNone())
      STATS_DECLTRACK_CS_ATTR(readnone)
  }
};
} // namespace

/// ------------------ denormal-fp-math Attribute -------------------------

namespace {
struct AADenormalFPMathImpl : public AADenormalFPMath {
  AADenormalFPMathImpl(const IRPosition &IRP, Attributor &A)
      : AADenormalFPMath(IRP, A) {}

  const std::string getAsStr(Attributor *A) const override {
    std::string Str("AADenormalFPMath[");
    raw_string_ostream OS(Str);

    DenormalState Known = getKnown();
    if (Known.Mode.isValid())
      OS << "denormal-fp-math=" << Known.Mode;
    else
      OS << "invalid";

    if (Known.ModeF32.isValid())
      OS << " denormal-fp-math-f32=" << Known.ModeF32;
    OS << ']';
    return Str;
  }
};

struct AADenormalFPMathFunction final : AADenormalFPMathImpl {
  AADenormalFPMathFunction(const IRPosition &IRP, Attributor &A)
      : AADenormalFPMathImpl(IRP, A) {}

  void initialize(Attributor &A) override {
    const Function *F = getAnchorScope();
    DenormalMode Mode = F->getDenormalModeRaw();
    DenormalMode ModeF32 = F->getDenormalModeF32Raw();

    // TODO: Handling this here prevents handling the case where a callee has a
    // fixed denormal-fp-math with dynamic denormal-fp-math-f32, but called from
    // a function with a fully fixed mode.
    if (ModeF32 == DenormalMode::getInvalid())
      ModeF32 = Mode;
    Known = DenormalState{Mode, ModeF32};
    if (isModeFixed())
      indicateFixpoint();
  }

  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Change = ChangeStatus::UNCHANGED;

    auto CheckCallSite = [=, &Change, &A](AbstractCallSite CS) {
      Function *Caller = CS.getInstruction()->getFunction();
      LLVM_DEBUG(dbgs() << "[AADenormalFPMath] Call " << Caller->getName()
                        << "->" << getAssociatedFunction()->getName() << '\n');

      const auto *CallerInfo = A.getAAFor<AADenormalFPMath>(
          *this, IRPosition::function(*Caller), DepClassTy::REQUIRED);
      if (!CallerInfo)
        return false;

      Change = Change | clampStateAndIndicateChange(this->getState(),
                                                    CallerInfo->getState());
      return true;
    };

    bool AllCallSitesKnown = true;
    if (!A.checkForAllCallSites(CheckCallSite, *this, true, AllCallSitesKnown))
      return indicatePessimisticFixpoint();

    if (Change == ChangeStatus::CHANGED && isModeFixed())
      indicateFixpoint();
    return Change;
  }

  ChangeStatus manifest(Attributor &A) override {
    LLVMContext &Ctx = getAssociatedFunction()->getContext();

    SmallVector<Attribute, 2> AttrToAdd;
    SmallVector<StringRef, 2> AttrToRemove;
    if (Known.Mode == DenormalMode::getDefault()) {
      AttrToRemove.push_back("denormal-fp-math");
    } else {
      AttrToAdd.push_back(
          Attribute::get(Ctx, "denormal-fp-math", Known.Mode.str()));
    }

    if (Known.ModeF32 != Known.Mode) {
      AttrToAdd.push_back(
          Attribute::get(Ctx, "denormal-fp-math-f32", Known.ModeF32.str()));
    } else {
      AttrToRemove.push_back("denormal-fp-math-f32");
    }

    auto &IRP = getIRPosition();

    // TODO: There should be a combined add and remove API.
    return A.removeAttrs(IRP, AttrToRemove) |
           A.manifestAttrs(IRP, AttrToAdd, /*ForceReplace=*/true);
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_FN_ATTR(denormal_fp_math)
  }
};
} // namespace

/// ------------------ Value Constant Range Attribute -------------------------

namespace {
struct AAValueConstantRangeImpl : AAValueConstantRange {
  using StateType = IntegerRangeState;
  AAValueConstantRangeImpl(const IRPosition &IRP, Attributor &A)
      : AAValueConstantRange(IRP, A) {}

  /// See AbstractAttribute::initialize(..).
  void initialize(Attributor &A) override {
    if (A.hasSimplificationCallback(getIRPosition())) {
      indicatePessimisticFixpoint();
      return;
    }

    // Intersect a range given by SCEV.
    intersectKnown(getConstantRangeFromSCEV(A, getCtxI()));

    // Intersect a range given by LVI.
    intersectKnown(getConstantRangeFromLVI(A, getCtxI()));
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << "range(" << getBitWidth() << ")<";
    getKnown().print(OS);
    OS << " / ";
    getAssumed().print(OS);
    OS << ">";
    return Str;
  }

  /// Helper function to get a SCEV expr for the associated value at program
  /// point \p I.
  const SCEV *getSCEV(Attributor &A, const Instruction *I = nullptr) const {
    if (!getAnchorScope())
      return nullptr;

    ScalarEvolution *SE =
        A.getInfoCache().getAnalysisResultForFunction<ScalarEvolutionAnalysis>(
            *getAnchorScope());

    LoopInfo *LI = A.getInfoCache().getAnalysisResultForFunction<LoopAnalysis>(
        *getAnchorScope());

    if (!SE || !LI)
      return nullptr;

    const SCEV *S = SE->getSCEV(&getAssociatedValue());
    if (!I)
      return S;

    return SE->getSCEVAtScope(S, LI->getLoopFor(I->getParent()));
  }

  /// Helper function to get a range from SCEV for the associated value at
  /// program point \p I.
  ConstantRange getConstantRangeFromSCEV(Attributor &A,
                                         const Instruction *I = nullptr) const {
    if (!getAnchorScope())
      return getWorstState(getBitWidth());

    ScalarEvolution *SE =
        A.getInfoCache().getAnalysisResultForFunction<ScalarEvolutionAnalysis>(
            *getAnchorScope());

    const SCEV *S = getSCEV(A, I);
    if (!SE || !S)
      return getWorstState(getBitWidth());

    return SE->getUnsignedRange(S);
  }

  /// Helper function to get a range from LVI for the associated value at
  /// program point \p I.
  ConstantRange
  getConstantRangeFromLVI(Attributor &A,
                          const Instruction *CtxI = nullptr) const {
    if (!getAnchorScope())
      return getWorstState(getBitWidth());

    LazyValueInfo *LVI =
        A.getInfoCache().getAnalysisResultForFunction<LazyValueAnalysis>(
            *getAnchorScope());

    if (!LVI || !CtxI)
      return getWorstState(getBitWidth());
    return LVI->getConstantRange(&getAssociatedValue(),
                                 const_cast<Instruction *>(CtxI),
                                 /*UndefAllowed*/ false);
  }

  /// Return true if \p CtxI is valid for querying outside analyses.
  /// This basically makes sure we do not ask intra-procedural analysis
  /// about a context in the wrong function or a context that violates
  /// dominance assumptions they might have. The \p AllowAACtxI flag indicates
  /// if the original context of this AA is OK or should be considered invalid.
  bool isValidCtxInstructionForOutsideAnalysis(Attributor &A,
                                               const Instruction *CtxI,
                                               bool AllowAACtxI) const {
    if (!CtxI || (!AllowAACtxI && CtxI == getCtxI()))
      return false;

    // Our context might be in a different function, neither intra-procedural
    // analysis (ScalarEvolution nor LazyValueInfo) can handle that.
    if (!AA::isValidInScope(getAssociatedValue(), CtxI->getFunction()))
      return false;

    // If the context is not dominated by the value there are paths to the
    // context that do not define the value. This cannot be handled by
    // LazyValueInfo so we need to bail.
    if (auto *I = dyn_cast<Instruction>(&getAssociatedValue())) {
      InformationCache &InfoCache = A.getInfoCache();
      const DominatorTree *DT =
          InfoCache.getAnalysisResultForFunction<DominatorTreeAnalysis>(
              *I->getFunction());
      return DT && DT->dominates(I, CtxI);
    }

    return true;
  }

  /// See AAValueConstantRange::getKnownConstantRange(..).
  ConstantRange
  getKnownConstantRange(Attributor &A,
                        const Instruction *CtxI = nullptr) const override {
    if (!isValidCtxInstructionForOutsideAnalysis(A, CtxI,
                                                 /* AllowAACtxI */ false))
      return getKnown();

    ConstantRange LVIR = getConstantRangeFromLVI(A, CtxI);
    ConstantRange SCEVR = getConstantRangeFromSCEV(A, CtxI);
    return getKnown().intersectWith(SCEVR).intersectWith(LVIR);
  }

  /// See AAValueConstantRange::getAssumedConstantRange(..).
  ConstantRange
  getAssumedConstantRange(Attributor &A,
                          const Instruction *CtxI = nullptr) const override {
    // TODO: Make SCEV use Attributor assumption.
    //       We may be able to bound a variable range via assumptions in
    //       Attributor. ex.) If x is assumed to be in [1, 3] and y is known to
    //       evolve to x^2 + x, then we can say that y is in [2, 12].
    if (!isValidCtxInstructionForOutsideAnalysis(A, CtxI,
                                                 /* AllowAACtxI */ false))
      return getAssumed();

    ConstantRange LVIR = getConstantRangeFromLVI(A, CtxI);
    ConstantRange SCEVR = getConstantRangeFromSCEV(A, CtxI);
    return getAssumed().intersectWith(SCEVR).intersectWith(LVIR);
  }

  /// Helper function to create MDNode for range metadata.
  static MDNode *
  getMDNodeForConstantRange(Type *Ty, LLVMContext &Ctx,
                            const ConstantRange &AssumedConstantRange) {
    Metadata *LowAndHigh[] = {ConstantAsMetadata::get(ConstantInt::get(
                                  Ty, AssumedConstantRange.getLower())),
                              ConstantAsMetadata::get(ConstantInt::get(
                                  Ty, AssumedConstantRange.getUpper()))};
    return MDNode::get(Ctx, LowAndHigh);
  }

  /// Return true if \p Assumed is included in \p KnownRanges.
  static bool isBetterRange(const ConstantRange &Assumed, MDNode *KnownRanges) {

    if (Assumed.isFullSet())
      return false;

    if (!KnownRanges)
      return true;

    // If multiple ranges are annotated in IR, we give up to annotate assumed
    // range for now.

    // TODO:  If there exists a known range which containts assumed range, we
    // can say assumed range is better.
    if (KnownRanges->getNumOperands() > 2)
      return false;

    ConstantInt *Lower =
        mdconst::extract<ConstantInt>(KnownRanges->getOperand(0));
    ConstantInt *Upper =
        mdconst::extract<ConstantInt>(KnownRanges->getOperand(1));

    ConstantRange Known(Lower->getValue(), Upper->getValue());
    return Known.contains(Assumed) && Known != Assumed;
  }

  /// Helper function to set range metadata.
  static bool
  setRangeMetadataIfisBetterRange(Instruction *I,
                                  const ConstantRange &AssumedConstantRange) {
    auto *OldRangeMD = I->getMetadata(LLVMContext::MD_range);
    if (isBetterRange(AssumedConstantRange, OldRangeMD)) {
      if (!AssumedConstantRange.isEmptySet()) {
        I->setMetadata(LLVMContext::MD_range,
                       getMDNodeForConstantRange(I->getType(), I->getContext(),
                                                 AssumedConstantRange));
        return true;
      }
    }
    return false;
  }

  /// See AbstractAttribute::manifest()
  ChangeStatus manifest(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    ConstantRange AssumedConstantRange = getAssumedConstantRange(A);
    assert(!AssumedConstantRange.isFullSet() && "Invalid state");

    auto &V = getAssociatedValue();
    if (!AssumedConstantRange.isEmptySet() &&
        !AssumedConstantRange.isSingleElement()) {
      if (Instruction *I = dyn_cast<Instruction>(&V)) {
        assert(I == getCtxI() && "Should not annotate an instruction which is "
                                 "not the context instruction");
        if (isa<CallInst>(I) || isa<LoadInst>(I))
          if (setRangeMetadataIfisBetterRange(I, AssumedConstantRange))
            Changed = ChangeStatus::CHANGED;
      }
    }

    return Changed;
  }
};

struct AAValueConstantRangeArgument final
    : AAArgumentFromCallSiteArguments<
          AAValueConstantRange, AAValueConstantRangeImpl, IntegerRangeState,
          true /* BridgeCallBaseContext */> {
  using Base = AAArgumentFromCallSiteArguments<
      AAValueConstantRange, AAValueConstantRangeImpl, IntegerRangeState,
      true /* BridgeCallBaseContext */>;
  AAValueConstantRangeArgument(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_ARG_ATTR(value_range)
  }
};

struct AAValueConstantRangeReturned
    : AAReturnedFromReturnedValues<AAValueConstantRange,
                                   AAValueConstantRangeImpl,
                                   AAValueConstantRangeImpl::StateType,
                                   /* PropogateCallBaseContext */ true> {
  using Base =
      AAReturnedFromReturnedValues<AAValueConstantRange,
                                   AAValueConstantRangeImpl,
                                   AAValueConstantRangeImpl::StateType,
                                   /* PropogateCallBaseContext */ true>;
  AAValueConstantRangeReturned(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    if (!A.isFunctionIPOAmendable(*getAssociatedFunction()))
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(value_range)
  }
};

struct AAValueConstantRangeFloating : AAValueConstantRangeImpl {
  AAValueConstantRangeFloating(const IRPosition &IRP, Attributor &A)
      : AAValueConstantRangeImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAValueConstantRangeImpl::initialize(A);
    if (isAtFixpoint())
      return;

    Value &V = getAssociatedValue();

    if (auto *C = dyn_cast<ConstantInt>(&V)) {
      unionAssumed(ConstantRange(C->getValue()));
      indicateOptimisticFixpoint();
      return;
    }

    if (isa<UndefValue>(&V)) {
      // Collapse the undef state to 0.
      unionAssumed(ConstantRange(APInt(getBitWidth(), 0)));
      indicateOptimisticFixpoint();
      return;
    }

    if (isa<CallBase>(&V))
      return;

    if (isa<BinaryOperator>(&V) || isa<CmpInst>(&V) || isa<CastInst>(&V))
      return;

    // If it is a load instruction with range metadata, use it.
    if (LoadInst *LI = dyn_cast<LoadInst>(&V))
      if (auto *RangeMD = LI->getMetadata(LLVMContext::MD_range)) {
        intersectKnown(getConstantRangeFromMetadata(*RangeMD));
        return;
      }

    // We can work with PHI and select instruction as we traverse their operands
    // during update.
    if (isa<SelectInst>(V) || isa<PHINode>(V))
      return;

    // Otherwise we give up.
    indicatePessimisticFixpoint();

    LLVM_DEBUG(dbgs() << "[AAValueConstantRange] We give up: "
                      << getAssociatedValue() << "\n");
  }

  bool calculateBinaryOperator(
      Attributor &A, BinaryOperator *BinOp, IntegerRangeState &T,
      const Instruction *CtxI,
      SmallVectorImpl<const AAValueConstantRange *> &QuerriedAAs) {
    Value *LHS = BinOp->getOperand(0);
    Value *RHS = BinOp->getOperand(1);

    // Simplify the operands first.
    bool UsedAssumedInformation = false;
    const auto &SimplifiedLHS = A.getAssumedSimplified(
        IRPosition::value(*LHS, getCallBaseContext()), *this,
        UsedAssumedInformation, AA::Interprocedural);
    if (!SimplifiedLHS.has_value())
      return true;
    if (!*SimplifiedLHS)
      return false;
    LHS = *SimplifiedLHS;

    const auto &SimplifiedRHS = A.getAssumedSimplified(
        IRPosition::value(*RHS, getCallBaseContext()), *this,
        UsedAssumedInformation, AA::Interprocedural);
    if (!SimplifiedRHS.has_value())
      return true;
    if (!*SimplifiedRHS)
      return false;
    RHS = *SimplifiedRHS;

    // TODO: Allow non integers as well.
    if (!LHS->getType()->isIntegerTy() || !RHS->getType()->isIntegerTy())
      return false;

    auto *LHSAA = A.getAAFor<AAValueConstantRange>(
        *this, IRPosition::value(*LHS, getCallBaseContext()),
        DepClassTy::REQUIRED);
    if (!LHSAA)
      return false;
    QuerriedAAs.push_back(LHSAA);
    auto LHSAARange = LHSAA->getAssumedConstantRange(A, CtxI);

    auto *RHSAA = A.getAAFor<AAValueConstantRange>(
        *this, IRPosition::value(*RHS, getCallBaseContext()),
        DepClassTy::REQUIRED);
    if (!RHSAA)
      return false;
    QuerriedAAs.push_back(RHSAA);
    auto RHSAARange = RHSAA->getAssumedConstantRange(A, CtxI);

    auto AssumedRange = LHSAARange.binaryOp(BinOp->getOpcode(), RHSAARange);

    T.unionAssumed(AssumedRange);

    // TODO: Track a known state too.

    return T.isValidState();
  }

  bool calculateCastInst(
      Attributor &A, CastInst *CastI, IntegerRangeState &T,
      const Instruction *CtxI,
      SmallVectorImpl<const AAValueConstantRange *> &QuerriedAAs) {
    assert(CastI->getNumOperands() == 1 && "Expected cast to be unary!");
    // TODO: Allow non integers as well.
    Value *OpV = CastI->getOperand(0);

    // Simplify the operand first.
    bool UsedAssumedInformation = false;
    const auto &SimplifiedOpV = A.getAssumedSimplified(
        IRPosition::value(*OpV, getCallBaseContext()), *this,
        UsedAssumedInformation, AA::Interprocedural);
    if (!SimplifiedOpV.has_value())
      return true;
    if (!*SimplifiedOpV)
      return false;
    OpV = *SimplifiedOpV;

    if (!OpV->getType()->isIntegerTy())
      return false;

    auto *OpAA = A.getAAFor<AAValueConstantRange>(
        *this, IRPosition::value(*OpV, getCallBaseContext()),
        DepClassTy::REQUIRED);
    if (!OpAA)
      return false;
    QuerriedAAs.push_back(OpAA);
    T.unionAssumed(OpAA->getAssumed().castOp(CastI->getOpcode(),
                                             getState().getBitWidth()));
    return T.isValidState();
  }

  bool
  calculateCmpInst(Attributor &A, CmpInst *CmpI, IntegerRangeState &T,
                   const Instruction *CtxI,
                   SmallVectorImpl<const AAValueConstantRange *> &QuerriedAAs) {
    Value *LHS = CmpI->getOperand(0);
    Value *RHS = CmpI->getOperand(1);

    // Simplify the operands first.
    bool UsedAssumedInformation = false;
    const auto &SimplifiedLHS = A.getAssumedSimplified(
        IRPosition::value(*LHS, getCallBaseContext()), *this,
        UsedAssumedInformation, AA::Interprocedural);
    if (!SimplifiedLHS.has_value())
      return true;
    if (!*SimplifiedLHS)
      return false;
    LHS = *SimplifiedLHS;

    const auto &SimplifiedRHS = A.getAssumedSimplified(
        IRPosition::value(*RHS, getCallBaseContext()), *this,
        UsedAssumedInformation, AA::Interprocedural);
    if (!SimplifiedRHS.has_value())
      return true;
    if (!*SimplifiedRHS)
      return false;
    RHS = *SimplifiedRHS;

    // TODO: Allow non integers as well.
    if (!LHS->getType()->isIntegerTy() || !RHS->getType()->isIntegerTy())
      return false;

    auto *LHSAA = A.getAAFor<AAValueConstantRange>(
        *this, IRPosition::value(*LHS, getCallBaseContext()),
        DepClassTy::REQUIRED);
    if (!LHSAA)
      return false;
    QuerriedAAs.push_back(LHSAA);
    auto *RHSAA = A.getAAFor<AAValueConstantRange>(
        *this, IRPosition::value(*RHS, getCallBaseContext()),
        DepClassTy::REQUIRED);
    if (!RHSAA)
      return false;
    QuerriedAAs.push_back(RHSAA);
    auto LHSAARange = LHSAA->getAssumedConstantRange(A, CtxI);
    auto RHSAARange = RHSAA->getAssumedConstantRange(A, CtxI);

    // If one of them is empty set, we can't decide.
    if (LHSAARange.isEmptySet() || RHSAARange.isEmptySet())
      return true;

    bool MustTrue = false, MustFalse = false;

    auto AllowedRegion =
        ConstantRange::makeAllowedICmpRegion(CmpI->getPredicate(), RHSAARange);

    if (AllowedRegion.intersectWith(LHSAARange).isEmptySet())
      MustFalse = true;

    if (LHSAARange.icmp(CmpI->getPredicate(), RHSAARange))
      MustTrue = true;

    assert((!MustTrue || !MustFalse) &&
           "Either MustTrue or MustFalse should be false!");

    if (MustTrue)
      T.unionAssumed(ConstantRange(APInt(/* numBits */ 1, /* val */ 1)));
    else if (MustFalse)
      T.unionAssumed(ConstantRange(APInt(/* numBits */ 1, /* val */ 0)));
    else
      T.unionAssumed(ConstantRange(/* BitWidth */ 1, /* isFullSet */ true));

    LLVM_DEBUG(dbgs() << "[AAValueConstantRange] " << *CmpI << " after "
                      << (MustTrue ? "true" : (MustFalse ? "false" : "unknown"))
                      << ": " << T << "\n\t" << *LHSAA << "\t<op>\n\t"
                      << *RHSAA);

    // TODO: Track a known state too.
    return T.isValidState();
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {

    IntegerRangeState T(getBitWidth());
    auto VisitValueCB = [&](Value &V, const Instruction *CtxI) -> bool {
      Instruction *I = dyn_cast<Instruction>(&V);
      if (!I || isa<CallBase>(I)) {

        // Simplify the operand first.
        bool UsedAssumedInformation = false;
        const auto &SimplifiedOpV = A.getAssumedSimplified(
            IRPosition::value(V, getCallBaseContext()), *this,
            UsedAssumedInformation, AA::Interprocedural);
        if (!SimplifiedOpV.has_value())
          return true;
        if (!*SimplifiedOpV)
          return false;
        Value *VPtr = *SimplifiedOpV;

        // If the value is not instruction, we query AA to Attributor.
        const auto *AA = A.getAAFor<AAValueConstantRange>(
            *this, IRPosition::value(*VPtr, getCallBaseContext()),
            DepClassTy::REQUIRED);

        // Clamp operator is not used to utilize a program point CtxI.
        if (AA)
          T.unionAssumed(AA->getAssumedConstantRange(A, CtxI));
        else
          return false;

        return T.isValidState();
      }

      SmallVector<const AAValueConstantRange *, 4> QuerriedAAs;
      if (auto *BinOp = dyn_cast<BinaryOperator>(I)) {
        if (!calculateBinaryOperator(A, BinOp, T, CtxI, QuerriedAAs))
          return false;
      } else if (auto *CmpI = dyn_cast<CmpInst>(I)) {
        if (!calculateCmpInst(A, CmpI, T, CtxI, QuerriedAAs))
          return false;
      } else if (auto *CastI = dyn_cast<CastInst>(I)) {
        if (!calculateCastInst(A, CastI, T, CtxI, QuerriedAAs))
          return false;
      } else {
        // Give up with other instructions.
        // TODO: Add other instructions

        T.indicatePessimisticFixpoint();
        return false;
      }

      // Catch circular reasoning in a pessimistic way for now.
      // TODO: Check how the range evolves and if we stripped anything, see also
      //       AADereferenceable or AAAlign for similar situations.
      for (const AAValueConstantRange *QueriedAA : QuerriedAAs) {
        if (QueriedAA != this)
          continue;
        // If we are in a stady state we do not need to worry.
        if (T.getAssumed() == getState().getAssumed())
          continue;
        T.indicatePessimisticFixpoint();
      }

      return T.isValidState();
    };

    if (!VisitValueCB(getAssociatedValue(), getCtxI()))
      return indicatePessimisticFixpoint();

    // Ensure that long def-use chains can't cause circular reasoning either by
    // introducing a cutoff below.
    if (clampStateAndIndicateChange(getState(), T) == ChangeStatus::UNCHANGED)
      return ChangeStatus::UNCHANGED;
    if (++NumChanges > MaxNumChanges) {
      LLVM_DEBUG(dbgs() << "[AAValueConstantRange] performed " << NumChanges
                        << " but only " << MaxNumChanges
                        << " are allowed to avoid cyclic reasoning.");
      return indicatePessimisticFixpoint();
    }
    return ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(value_range)
  }

  /// Tracker to bail after too many widening steps of the constant range.
  int NumChanges = 0;

  /// Upper bound for the number of allowed changes (=widening steps) for the
  /// constant range before we give up.
  static constexpr int MaxNumChanges = 5;
};

struct AAValueConstantRangeFunction : AAValueConstantRangeImpl {
  AAValueConstantRangeFunction(const IRPosition &IRP, Attributor &A)
      : AAValueConstantRangeImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable("AAValueConstantRange(Function|CallSite)::updateImpl will "
                     "not be called");
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FN_ATTR(value_range) }
};

struct AAValueConstantRangeCallSite : AAValueConstantRangeFunction {
  AAValueConstantRangeCallSite(const IRPosition &IRP, Attributor &A)
      : AAValueConstantRangeFunction(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CS_ATTR(value_range) }
};

struct AAValueConstantRangeCallSiteReturned
    : AACalleeToCallSite<AAValueConstantRange, AAValueConstantRangeImpl,
                         AAValueConstantRangeImpl::StateType,
                         /* IntroduceCallBaseContext */ true> {
  AAValueConstantRangeCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AAValueConstantRange, AAValueConstantRangeImpl,
                           AAValueConstantRangeImpl::StateType,
                           /* IntroduceCallBaseContext */ true>(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // If it is a load instruction with range metadata, use the metadata.
    if (CallInst *CI = dyn_cast<CallInst>(&getAssociatedValue()))
      if (auto *RangeMD = CI->getMetadata(LLVMContext::MD_range))
        intersectKnown(getConstantRangeFromMetadata(*RangeMD));

    AAValueConstantRangeImpl::initialize(A);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(value_range)
  }
};
struct AAValueConstantRangeCallSiteArgument : AAValueConstantRangeFloating {
  AAValueConstantRangeCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAValueConstantRangeFloating(IRP, A) {}

  /// See AbstractAttribute::manifest()
  ChangeStatus manifest(Attributor &A) override {
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(value_range)
  }
};
} // namespace

/// ------------------ Potential Values Attribute -------------------------

namespace {
struct AAPotentialConstantValuesImpl : AAPotentialConstantValues {
  using StateType = PotentialConstantIntValuesState;

  AAPotentialConstantValuesImpl(const IRPosition &IRP, Attributor &A)
      : AAPotentialConstantValues(IRP, A) {}

  /// See AbstractAttribute::initialize(..).
  void initialize(Attributor &A) override {
    if (A.hasSimplificationCallback(getIRPosition()))
      indicatePessimisticFixpoint();
    else
      AAPotentialConstantValues::initialize(A);
  }

  bool fillSetWithConstantValues(Attributor &A, const IRPosition &IRP, SetTy &S,
                                 bool &ContainsUndef, bool ForSelf) {
    SmallVector<AA::ValueAndContext> Values;
    bool UsedAssumedInformation = false;
    if (!A.getAssumedSimplifiedValues(IRP, *this, Values, AA::Interprocedural,
                                      UsedAssumedInformation)) {
      // Avoid recursion when the caller is computing constant values for this
      // IRP itself.
      if (ForSelf)
        return false;
      if (!IRP.getAssociatedType()->isIntegerTy())
        return false;
      auto *PotentialValuesAA = A.getAAFor<AAPotentialConstantValues>(
          *this, IRP, DepClassTy::REQUIRED);
      if (!PotentialValuesAA || !PotentialValuesAA->getState().isValidState())
        return false;
      ContainsUndef = PotentialValuesAA->getState().undefIsContained();
      S = PotentialValuesAA->getState().getAssumedSet();
      return true;
    }

    // Copy all the constant values, except UndefValue. ContainsUndef is true
    // iff Values contains only UndefValue instances. If there are other known
    // constants, then UndefValue is dropped.
    ContainsUndef = false;
    for (auto &It : Values) {
      if (isa<UndefValue>(It.getValue())) {
        ContainsUndef = true;
        continue;
      }
      auto *CI = dyn_cast<ConstantInt>(It.getValue());
      if (!CI)
        return false;
      S.insert(CI->getValue());
    }
    ContainsUndef &= S.empty();

    return true;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << getState();
    return Str;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    return indicatePessimisticFixpoint();
  }
};

struct AAPotentialConstantValuesArgument final
    : AAArgumentFromCallSiteArguments<AAPotentialConstantValues,
                                      AAPotentialConstantValuesImpl,
                                      PotentialConstantIntValuesState> {
  using Base = AAArgumentFromCallSiteArguments<AAPotentialConstantValues,
                                               AAPotentialConstantValuesImpl,
                                               PotentialConstantIntValuesState>;
  AAPotentialConstantValuesArgument(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_ARG_ATTR(potential_values)
  }
};

struct AAPotentialConstantValuesReturned
    : AAReturnedFromReturnedValues<AAPotentialConstantValues,
                                   AAPotentialConstantValuesImpl> {
  using Base = AAReturnedFromReturnedValues<AAPotentialConstantValues,
                                            AAPotentialConstantValuesImpl>;
  AAPotentialConstantValuesReturned(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  void initialize(Attributor &A) override {
    if (!A.isFunctionIPOAmendable(*getAssociatedFunction()))
      indicatePessimisticFixpoint();
    Base::initialize(A);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(potential_values)
  }
};

struct AAPotentialConstantValuesFloating : AAPotentialConstantValuesImpl {
  AAPotentialConstantValuesFloating(const IRPosition &IRP, Attributor &A)
      : AAPotentialConstantValuesImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(..).
  void initialize(Attributor &A) override {
    AAPotentialConstantValuesImpl::initialize(A);
    if (isAtFixpoint())
      return;

    Value &V = getAssociatedValue();

    if (auto *C = dyn_cast<ConstantInt>(&V)) {
      unionAssumed(C->getValue());
      indicateOptimisticFixpoint();
      return;
    }

    if (isa<UndefValue>(&V)) {
      unionAssumedWithUndef();
      indicateOptimisticFixpoint();
      return;
    }

    if (isa<BinaryOperator>(&V) || isa<ICmpInst>(&V) || isa<CastInst>(&V))
      return;

    if (isa<SelectInst>(V) || isa<PHINode>(V) || isa<LoadInst>(V))
      return;

    indicatePessimisticFixpoint();

    LLVM_DEBUG(dbgs() << "[AAPotentialConstantValues] We give up: "
                      << getAssociatedValue() << "\n");
  }

  static bool calculateICmpInst(const ICmpInst *ICI, const APInt &LHS,
                                const APInt &RHS) {
    return ICmpInst::compare(LHS, RHS, ICI->getPredicate());
  }

  static APInt calculateCastInst(const CastInst *CI, const APInt &Src,
                                 uint32_t ResultBitWidth) {
    Instruction::CastOps CastOp = CI->getOpcode();
    switch (CastOp) {
    default:
      llvm_unreachable("unsupported or not integer cast");
    case Instruction::Trunc:
      return Src.trunc(ResultBitWidth);
    case Instruction::SExt:
      return Src.sext(ResultBitWidth);
    case Instruction::ZExt:
      return Src.zext(ResultBitWidth);
    case Instruction::BitCast:
      return Src;
    }
  }

  static APInt calculateBinaryOperator(const BinaryOperator *BinOp,
                                       const APInt &LHS, const APInt &RHS,
                                       bool &SkipOperation, bool &Unsupported) {
    Instruction::BinaryOps BinOpcode = BinOp->getOpcode();
    // Unsupported is set to true when the binary operator is not supported.
    // SkipOperation is set to true when UB occur with the given operand pair
    // (LHS, RHS).
    // TODO: we should look at nsw and nuw keywords to handle operations
    //       that create poison or undef value.
    switch (BinOpcode) {
    default:
      Unsupported = true;
      return LHS;
    case Instruction::Add:
      return LHS + RHS;
    case Instruction::Sub:
      return LHS - RHS;
    case Instruction::Mul:
      return LHS * RHS;
    case Instruction::UDiv:
      if (RHS.isZero()) {
        SkipOperation = true;
        return LHS;
      }
      return LHS.udiv(RHS);
    case Instruction::SDiv:
      if (RHS.isZero()) {
        SkipOperation = true;
        return LHS;
      }
      return LHS.sdiv(RHS);
    case Instruction::URem:
      if (RHS.isZero()) {
        SkipOperation = true;
        return LHS;
      }
      return LHS.urem(RHS);
    case Instruction::SRem:
      if (RHS.isZero()) {
        SkipOperation = true;
        return LHS;
      }
      return LHS.srem(RHS);
    case Instruction::Shl:
      return LHS.shl(RHS);
    case Instruction::LShr:
      return LHS.lshr(RHS);
    case Instruction::AShr:
      return LHS.ashr(RHS);
    case Instruction::And:
      return LHS & RHS;
    case Instruction::Or:
      return LHS | RHS;
    case Instruction::Xor:
      return LHS ^ RHS;
    }
  }

  bool calculateBinaryOperatorAndTakeUnion(const BinaryOperator *BinOp,
                                           const APInt &LHS, const APInt &RHS) {
    bool SkipOperation = false;
    bool Unsupported = false;
    APInt Result =
        calculateBinaryOperator(BinOp, LHS, RHS, SkipOperation, Unsupported);
    if (Unsupported)
      return false;
    // If SkipOperation is true, we can ignore this operand pair (L, R).
    if (!SkipOperation)
      unionAssumed(Result);
    return isValidState();
  }

  ChangeStatus updateWithICmpInst(Attributor &A, ICmpInst *ICI) {
    auto AssumedBefore = getAssumed();
    Value *LHS = ICI->getOperand(0);
    Value *RHS = ICI->getOperand(1);

    bool LHSContainsUndef = false, RHSContainsUndef = false;
    SetTy LHSAAPVS, RHSAAPVS;
    if (!fillSetWithConstantValues(A, IRPosition::value(*LHS), LHSAAPVS,
                                   LHSContainsUndef, /* ForSelf */ false) ||
        !fillSetWithConstantValues(A, IRPosition::value(*RHS), RHSAAPVS,
                                   RHSContainsUndef, /* ForSelf */ false))
      return indicatePessimisticFixpoint();

    // TODO: make use of undef flag to limit potential values aggressively.
    bool MaybeTrue = false, MaybeFalse = false;
    const APInt Zero(RHS->getType()->getIntegerBitWidth(), 0);
    if (LHSContainsUndef && RHSContainsUndef) {
      // The result of any comparison between undefs can be soundly replaced
      // with undef.
      unionAssumedWithUndef();
    } else if (LHSContainsUndef) {
      for (const APInt &R : RHSAAPVS) {
        bool CmpResult = calculateICmpInst(ICI, Zero, R);
        MaybeTrue |= CmpResult;
        MaybeFalse |= !CmpResult;
        if (MaybeTrue & MaybeFalse)
          return indicatePessimisticFixpoint();
      }
    } else if (RHSContainsUndef) {
      for (const APInt &L : LHSAAPVS) {
        bool CmpResult = calculateICmpInst(ICI, L, Zero);
        MaybeTrue |= CmpResult;
        MaybeFalse |= !CmpResult;
        if (MaybeTrue & MaybeFalse)
          return indicatePessimisticFixpoint();
      }
    } else {
      for (const APInt &L : LHSAAPVS) {
        for (const APInt &R : RHSAAPVS) {
          bool CmpResult = calculateICmpInst(ICI, L, R);
          MaybeTrue |= CmpResult;
          MaybeFalse |= !CmpResult;
          if (MaybeTrue & MaybeFalse)
            return indicatePessimisticFixpoint();
        }
      }
    }
    if (MaybeTrue)
      unionAssumed(APInt(/* numBits */ 1, /* val */ 1));
    if (MaybeFalse)
      unionAssumed(APInt(/* numBits */ 1, /* val */ 0));
    return AssumedBefore == getAssumed() ? ChangeStatus::UNCHANGED
                                         : ChangeStatus::CHANGED;
  }

  ChangeStatus updateWithSelectInst(Attributor &A, SelectInst *SI) {
    auto AssumedBefore = getAssumed();
    Value *LHS = SI->getTrueValue();
    Value *RHS = SI->getFalseValue();

    bool UsedAssumedInformation = false;
    std::optional<Constant *> C = A.getAssumedConstant(
        *SI->getCondition(), *this, UsedAssumedInformation);

    // Check if we only need one operand.
    bool OnlyLeft = false, OnlyRight = false;
    if (C && *C && (*C)->isOneValue())
      OnlyLeft = true;
    else if (C && *C && (*C)->isZeroValue())
      OnlyRight = true;

    bool LHSContainsUndef = false, RHSContainsUndef = false;
    SetTy LHSAAPVS, RHSAAPVS;
    if (!OnlyRight &&
        !fillSetWithConstantValues(A, IRPosition::value(*LHS), LHSAAPVS,
                                   LHSContainsUndef, /* ForSelf */ false))
      return indicatePessimisticFixpoint();

    if (!OnlyLeft &&
        !fillSetWithConstantValues(A, IRPosition::value(*RHS), RHSAAPVS,
                                   RHSContainsUndef, /* ForSelf */ false))
      return indicatePessimisticFixpoint();

    if (OnlyLeft || OnlyRight) {
      // select (true/false), lhs, rhs
      auto *OpAA = OnlyLeft ? &LHSAAPVS : &RHSAAPVS;
      auto Undef = OnlyLeft ? LHSContainsUndef : RHSContainsUndef;

      if (Undef)
        unionAssumedWithUndef();
      else {
        for (const auto &It : *OpAA)
          unionAssumed(It);
      }

    } else if (LHSContainsUndef && RHSContainsUndef) {
      // select i1 *, undef , undef => undef
      unionAssumedWithUndef();
    } else {
      for (const auto &It : LHSAAPVS)
        unionAssumed(It);
      for (const auto &It : RHSAAPVS)
        unionAssumed(It);
    }
    return AssumedBefore == getAssumed() ? ChangeStatus::UNCHANGED
                                         : ChangeStatus::CHANGED;
  }

  ChangeStatus updateWithCastInst(Attributor &A, CastInst *CI) {
    auto AssumedBefore = getAssumed();
    if (!CI->isIntegerCast())
      return indicatePessimisticFixpoint();
    assert(CI->getNumOperands() == 1 && "Expected cast to be unary!");
    uint32_t ResultBitWidth = CI->getDestTy()->getIntegerBitWidth();
    Value *Src = CI->getOperand(0);

    bool SrcContainsUndef = false;
    SetTy SrcPVS;
    if (!fillSetWithConstantValues(A, IRPosition::value(*Src), SrcPVS,
                                   SrcContainsUndef, /* ForSelf */ false))
      return indicatePessimisticFixpoint();

    if (SrcContainsUndef)
      unionAssumedWithUndef();
    else {
      for (const APInt &S : SrcPVS) {
        APInt T = calculateCastInst(CI, S, ResultBitWidth);
        unionAssumed(T);
      }
    }
    return AssumedBefore == getAssumed() ? ChangeStatus::UNCHANGED
                                         : ChangeStatus::CHANGED;
  }

  ChangeStatus updateWithBinaryOperator(Attributor &A, BinaryOperator *BinOp) {
    auto AssumedBefore = getAssumed();
    Value *LHS = BinOp->getOperand(0);
    Value *RHS = BinOp->getOperand(1);

    bool LHSContainsUndef = false, RHSContainsUndef = false;
    SetTy LHSAAPVS, RHSAAPVS;
    if (!fillSetWithConstantValues(A, IRPosition::value(*LHS), LHSAAPVS,
                                   LHSContainsUndef, /* ForSelf */ false) ||
        !fillSetWithConstantValues(A, IRPosition::value(*RHS), RHSAAPVS,
                                   RHSContainsUndef, /* ForSelf */ false))
      return indicatePessimisticFixpoint();

    const APInt Zero = APInt(LHS->getType()->getIntegerBitWidth(), 0);

    // TODO: make use of undef flag to limit potential values aggressively.
    if (LHSContainsUndef && RHSContainsUndef) {
      if (!calculateBinaryOperatorAndTakeUnion(BinOp, Zero, Zero))
        return indicatePessimisticFixpoint();
    } else if (LHSContainsUndef) {
      for (const APInt &R : RHSAAPVS) {
        if (!calculateBinaryOperatorAndTakeUnion(BinOp, Zero, R))
          return indicatePessimisticFixpoint();
      }
    } else if (RHSContainsUndef) {
      for (const APInt &L : LHSAAPVS) {
        if (!calculateBinaryOperatorAndTakeUnion(BinOp, L, Zero))
          return indicatePessimisticFixpoint();
      }
    } else {
      for (const APInt &L : LHSAAPVS) {
        for (const APInt &R : RHSAAPVS) {
          if (!calculateBinaryOperatorAndTakeUnion(BinOp, L, R))
            return indicatePessimisticFixpoint();
        }
      }
    }
    return AssumedBefore == getAssumed() ? ChangeStatus::UNCHANGED
                                         : ChangeStatus::CHANGED;
  }

  ChangeStatus updateWithInstruction(Attributor &A, Instruction *Inst) {
    auto AssumedBefore = getAssumed();
    SetTy Incoming;
    bool ContainsUndef;
    if (!fillSetWithConstantValues(A, IRPosition::value(*Inst), Incoming,
                                   ContainsUndef, /* ForSelf */ true))
      return indicatePessimisticFixpoint();
    if (ContainsUndef) {
      unionAssumedWithUndef();
    } else {
      for (const auto &It : Incoming)
        unionAssumed(It);
    }
    return AssumedBefore == getAssumed() ? ChangeStatus::UNCHANGED
                                         : ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    Value &V = getAssociatedValue();
    Instruction *I = dyn_cast<Instruction>(&V);

    if (auto *ICI = dyn_cast<ICmpInst>(I))
      return updateWithICmpInst(A, ICI);

    if (auto *SI = dyn_cast<SelectInst>(I))
      return updateWithSelectInst(A, SI);

    if (auto *CI = dyn_cast<CastInst>(I))
      return updateWithCastInst(A, CI);

    if (auto *BinOp = dyn_cast<BinaryOperator>(I))
      return updateWithBinaryOperator(A, BinOp);

    if (isa<PHINode>(I) || isa<LoadInst>(I))
      return updateWithInstruction(A, I);

    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(potential_values)
  }
};

struct AAPotentialConstantValuesFunction : AAPotentialConstantValuesImpl {
  AAPotentialConstantValuesFunction(const IRPosition &IRP, Attributor &A)
      : AAPotentialConstantValuesImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable(
        "AAPotentialConstantValues(Function|CallSite)::updateImpl will "
        "not be called");
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FN_ATTR(potential_values)
  }
};

struct AAPotentialConstantValuesCallSite : AAPotentialConstantValuesFunction {
  AAPotentialConstantValuesCallSite(const IRPosition &IRP, Attributor &A)
      : AAPotentialConstantValuesFunction(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CS_ATTR(potential_values)
  }
};

struct AAPotentialConstantValuesCallSiteReturned
    : AACalleeToCallSite<AAPotentialConstantValues,
                         AAPotentialConstantValuesImpl> {
  AAPotentialConstantValuesCallSiteReturned(const IRPosition &IRP,
                                            Attributor &A)
      : AACalleeToCallSite<AAPotentialConstantValues,
                           AAPotentialConstantValuesImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(potential_values)
  }
};

struct AAPotentialConstantValuesCallSiteArgument
    : AAPotentialConstantValuesFloating {
  AAPotentialConstantValuesCallSiteArgument(const IRPosition &IRP,
                                            Attributor &A)
      : AAPotentialConstantValuesFloating(IRP, A) {}

  /// See AbstractAttribute::initialize(..).
  void initialize(Attributor &A) override {
    AAPotentialConstantValuesImpl::initialize(A);
    if (isAtFixpoint())
      return;

    Value &V = getAssociatedValue();

    if (auto *C = dyn_cast<ConstantInt>(&V)) {
      unionAssumed(C->getValue());
      indicateOptimisticFixpoint();
      return;
    }

    if (isa<UndefValue>(&V)) {
      unionAssumedWithUndef();
      indicateOptimisticFixpoint();
      return;
    }
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    Value &V = getAssociatedValue();
    auto AssumedBefore = getAssumed();
    auto *AA = A.getAAFor<AAPotentialConstantValues>(
        *this, IRPosition::value(V), DepClassTy::REQUIRED);
    if (!AA)
      return indicatePessimisticFixpoint();
    const auto &S = AA->getAssumed();
    unionAssumed(S);
    return AssumedBefore == getAssumed() ? ChangeStatus::UNCHANGED
                                         : ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(potential_values)
  }
};
} // namespace

/// ------------------------ NoUndef Attribute ---------------------------------
bool AANoUndef::isImpliedByIR(Attributor &A, const IRPosition &IRP,
                              Attribute::AttrKind ImpliedAttributeKind,
                              bool IgnoreSubsumingPositions) {
  assert(ImpliedAttributeKind == Attribute::NoUndef &&
         "Unexpected attribute kind");
  if (A.hasAttr(IRP, {Attribute::NoUndef}, IgnoreSubsumingPositions,
                Attribute::NoUndef))
    return true;

  Value &Val = IRP.getAssociatedValue();
  if (IRP.getPositionKind() != IRPosition::IRP_RETURNED &&
      isGuaranteedNotToBeUndefOrPoison(&Val)) {
    LLVMContext &Ctx = Val.getContext();
    A.manifestAttrs(IRP, Attribute::get(Ctx, Attribute::NoUndef));
    return true;
  }

  return false;
}

namespace {
struct AANoUndefImpl : AANoUndef {
  AANoUndefImpl(const IRPosition &IRP, Attributor &A) : AANoUndef(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    Value &V = getAssociatedValue();
    if (isa<UndefValue>(V))
      indicatePessimisticFixpoint();
    assert(!isImpliedByIR(A, getIRPosition(), Attribute::NoUndef));
  }

  /// See followUsesInMBEC
  bool followUseInMBEC(Attributor &A, const Use *U, const Instruction *I,
                       AANoUndef::StateType &State) {
    const Value *UseV = U->get();
    const DominatorTree *DT = nullptr;
    AssumptionCache *AC = nullptr;
    InformationCache &InfoCache = A.getInfoCache();
    if (Function *F = getAnchorScope()) {
      DT = InfoCache.getAnalysisResultForFunction<DominatorTreeAnalysis>(*F);
      AC = InfoCache.getAnalysisResultForFunction<AssumptionAnalysis>(*F);
    }
    State.setKnown(isGuaranteedNotToBeUndefOrPoison(UseV, AC, I, DT));
    bool TrackUse = false;
    // Track use for instructions which must produce undef or poison bits when
    // at least one operand contains such bits.
    if (isa<CastInst>(*I) || isa<GetElementPtrInst>(*I))
      TrackUse = true;
    return TrackUse;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return getAssumed() ? "noundef" : "may-undef-or-poison";
  }

  ChangeStatus manifest(Attributor &A) override {
    // We don't manifest noundef attribute for dead positions because the
    // associated values with dead positions would be replaced with undef
    // values.
    bool UsedAssumedInformation = false;
    if (A.isAssumedDead(getIRPosition(), nullptr, nullptr,
                        UsedAssumedInformation))
      return ChangeStatus::UNCHANGED;
    // A position whose simplified value does not have any value is
    // considered to be dead. We don't manifest noundef in such positions for
    // the same reason above.
    if (!A.getAssumedSimplified(getIRPosition(), *this, UsedAssumedInformation,
                                AA::Interprocedural)
             .has_value())
      return ChangeStatus::UNCHANGED;
    return AANoUndef::manifest(A);
  }
};

struct AANoUndefFloating : public AANoUndefImpl {
  AANoUndefFloating(const IRPosition &IRP, Attributor &A)
      : AANoUndefImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AANoUndefImpl::initialize(A);
    if (!getState().isAtFixpoint() && getAnchorScope() &&
        !getAnchorScope()->isDeclaration())
      if (Instruction *CtxI = getCtxI())
        followUsesInMBEC(*this, A, getState(), *CtxI);
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto VisitValueCB = [&](const IRPosition &IRP) -> bool {
      bool IsKnownNoUndef;
      return AA::hasAssumedIRAttr<Attribute::NoUndef>(
          A, this, IRP, DepClassTy::REQUIRED, IsKnownNoUndef);
    };

    bool Stripped;
    bool UsedAssumedInformation = false;
    Value *AssociatedValue = &getAssociatedValue();
    SmallVector<AA::ValueAndContext> Values;
    if (!A.getAssumedSimplifiedValues(getIRPosition(), *this, Values,
                                      AA::AnyScope, UsedAssumedInformation))
      Stripped = false;
    else
      Stripped =
          Values.size() != 1 || Values.front().getValue() != AssociatedValue;

    if (!Stripped) {
      // If we haven't stripped anything we might still be able to use a
      // different AA, but only if the IRP changes. Effectively when we
      // interpret this not as a call site value but as a floating/argument
      // value.
      const IRPosition AVIRP = IRPosition::value(*AssociatedValue);
      if (AVIRP == getIRPosition() || !VisitValueCB(AVIRP))
        return indicatePessimisticFixpoint();
      return ChangeStatus::UNCHANGED;
    }

    for (const auto &VAC : Values)
      if (!VisitValueCB(IRPosition::value(*VAC.getValue())))
        return indicatePessimisticFixpoint();

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FNRET_ATTR(noundef) }
};

struct AANoUndefReturned final
    : AAReturnedFromReturnedValues<AANoUndef, AANoUndefImpl> {
  AANoUndefReturned(const IRPosition &IRP, Attributor &A)
      : AAReturnedFromReturnedValues<AANoUndef, AANoUndefImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_FNRET_ATTR(noundef) }
};

struct AANoUndefArgument final
    : AAArgumentFromCallSiteArguments<AANoUndef, AANoUndefImpl> {
  AANoUndefArgument(const IRPosition &IRP, Attributor &A)
      : AAArgumentFromCallSiteArguments<AANoUndef, AANoUndefImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(noundef) }
};

struct AANoUndefCallSiteArgument final : AANoUndefFloating {
  AANoUndefCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AANoUndefFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSARG_ATTR(noundef) }
};

struct AANoUndefCallSiteReturned final
    : AACalleeToCallSite<AANoUndef, AANoUndefImpl> {
  AANoUndefCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoUndef, AANoUndefImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_CSRET_ATTR(noundef) }
};

/// ------------------------ NoFPClass Attribute -------------------------------

struct AANoFPClassImpl : AANoFPClass {
  AANoFPClassImpl(const IRPosition &IRP, Attributor &A) : AANoFPClass(IRP, A) {}

  void initialize(Attributor &A) override {
    const IRPosition &IRP = getIRPosition();

    Value &V = IRP.getAssociatedValue();
    if (isa<UndefValue>(V)) {
      indicateOptimisticFixpoint();
      return;
    }

    SmallVector<Attribute> Attrs;
    A.getAttrs(getIRPosition(), {Attribute::NoFPClass}, Attrs, false);
    for (const auto &Attr : Attrs) {
      addKnownBits(Attr.getNoFPClass());
    }

    const DataLayout &DL = A.getDataLayout();
    if (getPositionKind() != IRPosition::IRP_RETURNED) {
      KnownFPClass KnownFPClass = computeKnownFPClass(&V, DL);
      addKnownBits(~KnownFPClass.KnownFPClasses);
    }

    if (Instruction *CtxI = getCtxI())
      followUsesInMBEC(*this, A, getState(), *CtxI);
  }

  /// See followUsesInMBEC
  bool followUseInMBEC(Attributor &A, const Use *U, const Instruction *I,
                       AANoFPClass::StateType &State) {
    // TODO: Determine what instructions can be looked through.
    auto *CB = dyn_cast<CallBase>(I);
    if (!CB)
      return false;

    if (!CB->isArgOperand(U))
      return false;

    unsigned ArgNo = CB->getArgOperandNo(U);
    IRPosition IRP = IRPosition::callsite_argument(*CB, ArgNo);
    if (auto *NoFPAA = A.getAAFor<AANoFPClass>(*this, IRP, DepClassTy::NONE))
      State.addKnownBits(NoFPAA->getState().getKnown());
    return false;
  }

  const std::string getAsStr(Attributor *A) const override {
    std::string Result = "nofpclass";
    raw_string_ostream OS(Result);
    OS << getKnownNoFPClass() << '/' << getAssumedNoFPClass();
    return Result;
  }

  void getDeducedAttributes(Attributor &A, LLVMContext &Ctx,
                            SmallVectorImpl<Attribute> &Attrs) const override {
    Attrs.emplace_back(Attribute::getWithNoFPClass(Ctx, getAssumedNoFPClass()));
  }
};

struct AANoFPClassFloating : public AANoFPClassImpl {
  AANoFPClassFloating(const IRPosition &IRP, Attributor &A)
      : AANoFPClassImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    SmallVector<AA::ValueAndContext> Values;
    bool UsedAssumedInformation = false;
    if (!A.getAssumedSimplifiedValues(getIRPosition(), *this, Values,
                                      AA::AnyScope, UsedAssumedInformation)) {
      Values.push_back({getAssociatedValue(), getCtxI()});
    }

    StateType T;
    auto VisitValueCB = [&](Value &V, const Instruction *CtxI) -> bool {
      const auto *AA = A.getAAFor<AANoFPClass>(*this, IRPosition::value(V),
                                               DepClassTy::REQUIRED);
      if (!AA || this == AA) {
        T.indicatePessimisticFixpoint();
      } else {
        const AANoFPClass::StateType &S =
            static_cast<const AANoFPClass::StateType &>(AA->getState());
        T ^= S;
      }
      return T.isValidState();
    };

    for (const auto &VAC : Values)
      if (!VisitValueCB(*VAC.getValue(), VAC.getCtxI()))
        return indicatePessimisticFixpoint();

    return clampStateAndIndicateChange(getState(), T);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(nofpclass)
  }
};

struct AANoFPClassReturned final
    : AAReturnedFromReturnedValues<AANoFPClass, AANoFPClassImpl,
                                   AANoFPClassImpl::StateType, false,
                                   Attribute::None, false> {
  AANoFPClassReturned(const IRPosition &IRP, Attributor &A)
      : AAReturnedFromReturnedValues<AANoFPClass, AANoFPClassImpl,
                                     AANoFPClassImpl::StateType, false,
                                     Attribute::None, false>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(nofpclass)
  }
};

struct AANoFPClassArgument final
    : AAArgumentFromCallSiteArguments<AANoFPClass, AANoFPClassImpl> {
  AANoFPClassArgument(const IRPosition &IRP, Attributor &A)
      : AAArgumentFromCallSiteArguments<AANoFPClass, AANoFPClassImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(nofpclass) }
};

struct AANoFPClassCallSiteArgument final : AANoFPClassFloating {
  AANoFPClassCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AANoFPClassFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(nofpclass)
  }
};

struct AANoFPClassCallSiteReturned final
    : AACalleeToCallSite<AANoFPClass, AANoFPClassImpl> {
  AANoFPClassCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AACalleeToCallSite<AANoFPClass, AANoFPClassImpl>(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(nofpclass)
  }
};

struct AACallEdgesImpl : public AACallEdges {
  AACallEdgesImpl(const IRPosition &IRP, Attributor &A) : AACallEdges(IRP, A) {}

  const SetVector<Function *> &getOptimisticEdges() const override {
    return CalledFunctions;
  }

  bool hasUnknownCallee() const override { return HasUnknownCallee; }

  bool hasNonAsmUnknownCallee() const override {
    return HasUnknownCalleeNonAsm;
  }

  const std::string getAsStr(Attributor *A) const override {
    return "CallEdges[" + std::to_string(HasUnknownCallee) + "," +
           std::to_string(CalledFunctions.size()) + "]";
  }

  void trackStatistics() const override {}

protected:
  void addCalledFunction(Function *Fn, ChangeStatus &Change) {
    if (CalledFunctions.insert(Fn)) {
      Change = ChangeStatus::CHANGED;
      LLVM_DEBUG(dbgs() << "[AACallEdges] New call edge: " << Fn->getName()
                        << "\n");
    }
  }

  void setHasUnknownCallee(bool NonAsm, ChangeStatus &Change) {
    if (!HasUnknownCallee)
      Change = ChangeStatus::CHANGED;
    if (NonAsm && !HasUnknownCalleeNonAsm)
      Change = ChangeStatus::CHANGED;
    HasUnknownCalleeNonAsm |= NonAsm;
    HasUnknownCallee = true;
  }

private:
  /// Optimistic set of functions that might be called by this position.
  SetVector<Function *> CalledFunctions;

  /// Is there any call with a unknown callee.
  bool HasUnknownCallee = false;

  /// Is there any call with a unknown callee, excluding any inline asm.
  bool HasUnknownCalleeNonAsm = false;
};

struct AACallEdgesCallSite : public AACallEdgesImpl {
  AACallEdgesCallSite(const IRPosition &IRP, Attributor &A)
      : AACallEdgesImpl(IRP, A) {}
  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Change = ChangeStatus::UNCHANGED;

    auto VisitValue = [&](Value &V, const Instruction *CtxI) -> bool {
      if (Function *Fn = dyn_cast<Function>(&V)) {
        addCalledFunction(Fn, Change);
      } else {
        LLVM_DEBUG(dbgs() << "[AACallEdges] Unrecognized value: " << V << "\n");
        setHasUnknownCallee(true, Change);
      }

      // Explore all values.
      return true;
    };

    SmallVector<AA::ValueAndContext> Values;
    // Process any value that we might call.
    auto ProcessCalledOperand = [&](Value *V, Instruction *CtxI) {
      if (isa<Constant>(V)) {
        VisitValue(*V, CtxI);
        return;
      }

      bool UsedAssumedInformation = false;
      Values.clear();
      if (!A.getAssumedSimplifiedValues(IRPosition::value(*V), *this, Values,
                                        AA::AnyScope, UsedAssumedInformation)) {
        Values.push_back({*V, CtxI});
      }
      for (auto &VAC : Values)
        VisitValue(*VAC.getValue(), VAC.getCtxI());
    };

    CallBase *CB = cast<CallBase>(getCtxI());

    if (auto *IA = dyn_cast<InlineAsm>(CB->getCalledOperand())) {
      if (IA->hasSideEffects() &&
          !hasAssumption(*CB->getCaller(), "ompx_no_call_asm") &&
          !hasAssumption(*CB, "ompx_no_call_asm")) {
        setHasUnknownCallee(false, Change);
      }
      return Change;
    }

    if (CB->isIndirectCall())
      if (auto *IndirectCallAA = A.getAAFor<AAIndirectCallInfo>(
              *this, getIRPosition(), DepClassTy::OPTIONAL))
        if (IndirectCallAA->foreachCallee(
                [&](Function *Fn) { return VisitValue(*Fn, CB); }))
          return Change;

    // The most simple case.
    ProcessCalledOperand(CB->getCalledOperand(), CB);

    // Process callback functions.
    SmallVector<const Use *, 4u> CallbackUses;
    AbstractCallSite::getCallbackUses(*CB, CallbackUses);
    for (const Use *U : CallbackUses)
      ProcessCalledOperand(U->get(), CB);

    return Change;
  }
};

struct AACallEdgesFunction : public AACallEdgesImpl {
  AACallEdgesFunction(const IRPosition &IRP, Attributor &A)
      : AACallEdgesImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Change = ChangeStatus::UNCHANGED;

    auto ProcessCallInst = [&](Instruction &Inst) {
      CallBase &CB = cast<CallBase>(Inst);

      auto *CBEdges = A.getAAFor<AACallEdges>(
          *this, IRPosition::callsite_function(CB), DepClassTy::REQUIRED);
      if (!CBEdges)
        return false;
      if (CBEdges->hasNonAsmUnknownCallee())
        setHasUnknownCallee(true, Change);
      if (CBEdges->hasUnknownCallee())
        setHasUnknownCallee(false, Change);

      for (Function *F : CBEdges->getOptimisticEdges())
        addCalledFunction(F, Change);

      return true;
    };

    // Visit all callable instructions.
    bool UsedAssumedInformation = false;
    if (!A.checkForAllCallLikeInstructions(ProcessCallInst, *this,
                                           UsedAssumedInformation,
                                           /* CheckBBLivenessOnly */ true)) {
      // If we haven't looked at all call like instructions, assume that there
      // are unknown callees.
      setHasUnknownCallee(true, Change);
    }

    return Change;
  }
};

/// -------------------AAInterFnReachability Attribute--------------------------

struct AAInterFnReachabilityFunction
    : public CachedReachabilityAA<AAInterFnReachability, Function> {
  using Base = CachedReachabilityAA<AAInterFnReachability, Function>;
  AAInterFnReachabilityFunction(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  bool instructionCanReach(
      Attributor &A, const Instruction &From, const Function &To,
      const AA::InstExclusionSetTy *ExclusionSet) const override {
    assert(From.getFunction() == getAnchorScope() && "Queried the wrong AA!");
    auto *NonConstThis = const_cast<AAInterFnReachabilityFunction *>(this);

    RQITy StackRQI(A, From, To, ExclusionSet, false);
    typename RQITy::Reachable Result;
    if (!NonConstThis->checkQueryCache(A, StackRQI, Result))
      return NonConstThis->isReachableImpl(A, StackRQI,
                                           /*IsTemporaryRQI=*/true);
    return Result == RQITy::Reachable::Yes;
  }

  bool isReachableImpl(Attributor &A, RQITy &RQI,
                       bool IsTemporaryRQI) override {
    const Instruction *EntryI =
        &RQI.From->getFunction()->getEntryBlock().front();
    if (EntryI != RQI.From &&
        !instructionCanReach(A, *EntryI, *RQI.To, nullptr))
      return rememberResult(A, RQITy::Reachable::No, RQI, false,
                            IsTemporaryRQI);

    auto CheckReachableCallBase = [&](CallBase *CB) {
      auto *CBEdges = A.getAAFor<AACallEdges>(
          *this, IRPosition::callsite_function(*CB), DepClassTy::OPTIONAL);
      if (!CBEdges || !CBEdges->getState().isValidState())
        return false;
      // TODO Check To backwards in this case.
      if (CBEdges->hasUnknownCallee())
        return false;

      for (Function *Fn : CBEdges->getOptimisticEdges()) {
        if (Fn == RQI.To)
          return false;

        if (Fn->isDeclaration()) {
          if (Fn->hasFnAttribute(Attribute::NoCallback))
            continue;
          // TODO Check To backwards in this case.
          return false;
        }

        if (Fn == getAnchorScope()) {
          if (EntryI == RQI.From)
            continue;
          return false;
        }

        const AAInterFnReachability *InterFnReachability =
            A.getAAFor<AAInterFnReachability>(*this, IRPosition::function(*Fn),
                                              DepClassTy::OPTIONAL);

        const Instruction &FnFirstInst = Fn->getEntryBlock().front();
        if (!InterFnReachability ||
            InterFnReachability->instructionCanReach(A, FnFirstInst, *RQI.To,
                                                     RQI.ExclusionSet))
          return false;
      }
      return true;
    };

    const auto *IntraFnReachability = A.getAAFor<AAIntraFnReachability>(
        *this, IRPosition::function(*RQI.From->getFunction()),
        DepClassTy::OPTIONAL);

    // Determine call like instructions that we can reach from the inst.
    auto CheckCallBase = [&](Instruction &CBInst) {
      // There are usually less nodes in the call graph, check inter function
      // reachability first.
      if (CheckReachableCallBase(cast<CallBase>(&CBInst)))
        return true;
      return IntraFnReachability && !IntraFnReachability->isAssumedReachable(
                                        A, *RQI.From, CBInst, RQI.ExclusionSet);
    };

    bool UsedExclusionSet = /* conservative */ true;
    bool UsedAssumedInformation = false;
    if (!A.checkForAllCallLikeInstructions(CheckCallBase, *this,
                                           UsedAssumedInformation,
                                           /* CheckBBLivenessOnly */ true))
      return rememberResult(A, RQITy::Reachable::Yes, RQI, UsedExclusionSet,
                            IsTemporaryRQI);

    return rememberResult(A, RQITy::Reachable::No, RQI, UsedExclusionSet,
                          IsTemporaryRQI);
  }

  void trackStatistics() const override {}
};
} // namespace

template <typename AAType>
static std::optional<Constant *>
askForAssumedConstant(Attributor &A, const AbstractAttribute &QueryingAA,
                      const IRPosition &IRP, Type &Ty) {
  if (!Ty.isIntegerTy())
    return nullptr;

  // This will also pass the call base context.
  const auto *AA = A.getAAFor<AAType>(QueryingAA, IRP, DepClassTy::NONE);
  if (!AA)
    return nullptr;

  std::optional<Constant *> COpt = AA->getAssumedConstant(A);

  if (!COpt.has_value()) {
    A.recordDependence(*AA, QueryingAA, DepClassTy::OPTIONAL);
    return std::nullopt;
  }
  if (auto *C = *COpt) {
    A.recordDependence(*AA, QueryingAA, DepClassTy::OPTIONAL);
    return C;
  }
  return nullptr;
}

Value *AAPotentialValues::getSingleValue(
    Attributor &A, const AbstractAttribute &AA, const IRPosition &IRP,
    SmallVectorImpl<AA::ValueAndContext> &Values) {
  Type &Ty = *IRP.getAssociatedType();
  std::optional<Value *> V;
  for (auto &It : Values) {
    V = AA::combineOptionalValuesInAAValueLatice(V, It.getValue(), &Ty);
    if (V.has_value() && !*V)
      break;
  }
  if (!V.has_value())
    return UndefValue::get(&Ty);
  return *V;
}

namespace {
struct AAPotentialValuesImpl : AAPotentialValues {
  using StateType = PotentialLLVMValuesState;

  AAPotentialValuesImpl(const IRPosition &IRP, Attributor &A)
      : AAPotentialValues(IRP, A) {}

  /// See AbstractAttribute::initialize(..).
  void initialize(Attributor &A) override {
    if (A.hasSimplificationCallback(getIRPosition())) {
      indicatePessimisticFixpoint();
      return;
    }
    Value *Stripped = getAssociatedValue().stripPointerCasts();
    if (isa<Constant>(Stripped) && !isa<ConstantExpr>(Stripped)) {
      addValue(A, getState(), *Stripped, getCtxI(), AA::AnyScope,
               getAnchorScope());
      indicateOptimisticFixpoint();
      return;
    }
    AAPotentialValues::initialize(A);
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << getState();
    return Str;
  }

  template <typename AAType>
  static std::optional<Value *> askOtherAA(Attributor &A,
                                           const AbstractAttribute &AA,
                                           const IRPosition &IRP, Type &Ty) {
    if (isa<Constant>(IRP.getAssociatedValue()))
      return &IRP.getAssociatedValue();
    std::optional<Constant *> C = askForAssumedConstant<AAType>(A, AA, IRP, Ty);
    if (!C)
      return std::nullopt;
    if (*C)
      if (auto *CC = AA::getWithType(**C, Ty))
        return CC;
    return nullptr;
  }

  virtual void addValue(Attributor &A, StateType &State, Value &V,
                        const Instruction *CtxI, AA::ValueScope S,
                        Function *AnchorScope) const {

    IRPosition ValIRP = IRPosition::value(V);
    if (auto *CB = dyn_cast_or_null<CallBase>(CtxI)) {
      for (const auto &U : CB->args()) {
        if (U.get() != &V)
          continue;
        ValIRP = IRPosition::callsite_argument(*CB, CB->getArgOperandNo(&U));
        break;
      }
    }

    Value *VPtr = &V;
    if (ValIRP.getAssociatedType()->isIntegerTy()) {
      Type &Ty = *getAssociatedType();
      std::optional<Value *> SimpleV =
          askOtherAA<AAValueConstantRange>(A, *this, ValIRP, Ty);
      if (SimpleV.has_value() && !*SimpleV) {
        auto *PotentialConstantsAA = A.getAAFor<AAPotentialConstantValues>(
            *this, ValIRP, DepClassTy::OPTIONAL);
        if (PotentialConstantsAA && PotentialConstantsAA->isValidState()) {
          for (const auto &It : PotentialConstantsAA->getAssumedSet())
            State.unionAssumed({{*ConstantInt::get(&Ty, It), nullptr}, S});
          if (PotentialConstantsAA->undefIsContained())
            State.unionAssumed({{*UndefValue::get(&Ty), nullptr}, S});
          return;
        }
      }
      if (!SimpleV.has_value())
        return;

      if (*SimpleV)
        VPtr = *SimpleV;
    }

    if (isa<ConstantInt>(VPtr))
      CtxI = nullptr;
    if (!AA::isValidInScope(*VPtr, AnchorScope))
      S = AA::ValueScope(S | AA::Interprocedural);

    State.unionAssumed({{*VPtr, CtxI}, S});
  }

  /// Helper struct to tie a value+context pair together with the scope for
  /// which this is the simplified version.
  struct ItemInfo {
    AA::ValueAndContext I;
    AA::ValueScope S;

    bool operator==(const ItemInfo &II) const {
      return II.I == I && II.S == S;
    };
    bool operator<(const ItemInfo &II) const {
      if (I == II.I)
        return S < II.S;
      return I < II.I;
    };
  };

  bool recurseForValue(Attributor &A, const IRPosition &IRP, AA::ValueScope S) {
    SmallMapVector<AA::ValueAndContext, int, 8> ValueScopeMap;
    for (auto CS : {AA::Intraprocedural, AA::Interprocedural}) {
      if (!(CS & S))
        continue;

      bool UsedAssumedInformation = false;
      SmallVector<AA::ValueAndContext> Values;
      if (!A.getAssumedSimplifiedValues(IRP, this, Values, CS,
                                        UsedAssumedInformation))
        return false;

      for (auto &It : Values)
        ValueScopeMap[It] += CS;
    }
    for (auto &It : ValueScopeMap)
      addValue(A, getState(), *It.first.getValue(), It.first.getCtxI(),
               AA::ValueScope(It.second), getAnchorScope());

    return true;
  }

  void giveUpOnIntraprocedural(Attributor &A) {
    auto NewS = StateType::getBestState(getState());
    for (const auto &It : getAssumedSet()) {
      if (It.second == AA::Intraprocedural)
        continue;
      addValue(A, NewS, *It.first.getValue(), It.first.getCtxI(),
               AA::Interprocedural, getAnchorScope());
    }
    assert(!undefIsContained() && "Undef should be an explicit value!");
    addValue(A, NewS, getAssociatedValue(), getCtxI(), AA::Intraprocedural,
             getAnchorScope());
    getState() = NewS;
  }

  /// See AbstractState::indicatePessimisticFixpoint(...).
  ChangeStatus indicatePessimisticFixpoint() override {
    getState() = StateType::getBestState(getState());
    getState().unionAssumed({{getAssociatedValue(), getCtxI()}, AA::AnyScope});
    AAPotentialValues::indicateOptimisticFixpoint();
    return ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    return indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    SmallVector<AA::ValueAndContext> Values;
    for (AA::ValueScope S : {AA::Interprocedural, AA::Intraprocedural}) {
      Values.clear();
      if (!getAssumedSimplifiedValues(A, Values, S))
        continue;
      Value &OldV = getAssociatedValue();
      if (isa<UndefValue>(OldV))
        continue;
      Value *NewV = getSingleValue(A, *this, getIRPosition(), Values);
      if (!NewV || NewV == &OldV)
        continue;
      if (getCtxI() &&
          !AA::isValidAtPosition({*NewV, *getCtxI()}, A.getInfoCache()))
        continue;
      if (A.changeAfterManifest(getIRPosition(), *NewV))
        return ChangeStatus::CHANGED;
    }
    return ChangeStatus::UNCHANGED;
  }

  bool getAssumedSimplifiedValues(
      Attributor &A, SmallVectorImpl<AA::ValueAndContext> &Values,
      AA::ValueScope S, bool RecurseForSelectAndPHI = false) const override {
    if (!isValidState())
      return false;
    bool UsedAssumedInformation = false;
    for (const auto &It : getAssumedSet())
      if (It.second & S) {
        if (RecurseForSelectAndPHI && (isa<PHINode>(It.first.getValue()) ||
                                       isa<SelectInst>(It.first.getValue()))) {
          if (A.getAssumedSimplifiedValues(
                  IRPosition::inst(*cast<Instruction>(It.first.getValue())),
                  this, Values, S, UsedAssumedInformation))
            continue;
        }
        Values.push_back(It.first);
      }
    assert(!undefIsContained() && "Undef should be an explicit value!");
    return true;
  }
};

struct AAPotentialValuesFloating : AAPotentialValuesImpl {
  AAPotentialValuesFloating(const IRPosition &IRP, Attributor &A)
      : AAPotentialValuesImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto AssumedBefore = getAssumed();

    genericValueTraversal(A, &getAssociatedValue());

    return (AssumedBefore == getAssumed()) ? ChangeStatus::UNCHANGED
                                           : ChangeStatus::CHANGED;
  }

  /// Helper struct to remember which AAIsDead instances we actually used.
  struct LivenessInfo {
    const AAIsDead *LivenessAA = nullptr;
    bool AnyDead = false;
  };

  /// Check if \p Cmp is a comparison we can simplify.
  ///
  /// We handle multiple cases, one in which at least one operand is an
  /// (assumed) nullptr. If so, try to simplify it using AANonNull on the other
  /// operand. Return true if successful, in that case Worklist will be updated.
  bool handleCmp(Attributor &A, Value &Cmp, Value *LHS, Value *RHS,
                 CmpInst::Predicate Pred, ItemInfo II,
                 SmallVectorImpl<ItemInfo> &Worklist) {

    // Simplify the operands first.
    bool UsedAssumedInformation = false;
    SmallVector<AA::ValueAndContext> LHSValues, RHSValues;
    auto GetSimplifiedValues = [&](Value &V,
                                   SmallVector<AA::ValueAndContext> &Values) {
      if (!A.getAssumedSimplifiedValues(
              IRPosition::value(V, getCallBaseContext()), this, Values,
              AA::Intraprocedural, UsedAssumedInformation)) {
        Values.clear();
        Values.push_back(AA::ValueAndContext{V, II.I.getCtxI()});
      }
      return Values.empty();
    };
    if (GetSimplifiedValues(*LHS, LHSValues))
      return true;
    if (GetSimplifiedValues(*RHS, RHSValues))
      return true;

    LLVMContext &Ctx = LHS->getContext();

    InformationCache &InfoCache = A.getInfoCache();
    Instruction *CmpI = dyn_cast<Instruction>(&Cmp);
    Function *F = CmpI ? CmpI->getFunction() : nullptr;
    const auto *DT =
        F ? InfoCache.getAnalysisResultForFunction<DominatorTreeAnalysis>(*F)
          : nullptr;
    const auto *TLI =
        F ? A.getInfoCache().getTargetLibraryInfoForFunction(*F) : nullptr;
    auto *AC =
        F ? InfoCache.getAnalysisResultForFunction<AssumptionAnalysis>(*F)
          : nullptr;

    const DataLayout &DL = A.getDataLayout();
    SimplifyQuery Q(DL, TLI, DT, AC, CmpI);

    auto CheckPair = [&](Value &LHSV, Value &RHSV) {
      if (isa<UndefValue>(LHSV) || isa<UndefValue>(RHSV)) {
        addValue(A, getState(), *UndefValue::get(Cmp.getType()),
                 /* CtxI */ nullptr, II.S, getAnchorScope());
        return true;
      }

      // Handle the trivial case first in which we don't even need to think
      // about null or non-null.
      if (&LHSV == &RHSV &&
          (CmpInst::isTrueWhenEqual(Pred) || CmpInst::isFalseWhenEqual(Pred))) {
        Constant *NewV = ConstantInt::get(Type::getInt1Ty(Ctx),
                                          CmpInst::isTrueWhenEqual(Pred));
        addValue(A, getState(), *NewV, /* CtxI */ nullptr, II.S,
                 getAnchorScope());
        return true;
      }

      auto *TypedLHS = AA::getWithType(LHSV, *LHS->getType());
      auto *TypedRHS = AA::getWithType(RHSV, *RHS->getType());
      if (TypedLHS && TypedRHS) {
        Value *NewV = simplifyCmpInst(Pred, TypedLHS, TypedRHS, Q);
        if (NewV && NewV != &Cmp) {
          addValue(A, getState(), *NewV, /* CtxI */ nullptr, II.S,
                   getAnchorScope());
          return true;
        }
      }

      // From now on we only handle equalities (==, !=).
      if (!CmpInst::isEquality(Pred))
        return false;

      bool LHSIsNull = isa<ConstantPointerNull>(LHSV);
      bool RHSIsNull = isa<ConstantPointerNull>(RHSV);
      if (!LHSIsNull && !RHSIsNull)
        return false;

      // Left is the nullptr ==/!= non-nullptr case. We'll use AANonNull on the
      // non-nullptr operand and if we assume it's non-null we can conclude the
      // result of the comparison.
      assert((LHSIsNull || RHSIsNull) &&
             "Expected nullptr versus non-nullptr comparison at this point");

      // The index is the operand that we assume is not null.
      unsigned PtrIdx = LHSIsNull;
      bool IsKnownNonNull;
      bool IsAssumedNonNull = AA::hasAssumedIRAttr<Attribute::NonNull>(
          A, this, IRPosition::value(*(PtrIdx ? &RHSV : &LHSV)),
          DepClassTy::REQUIRED, IsKnownNonNull);
      if (!IsAssumedNonNull)
        return false;

      // The new value depends on the predicate, true for != and false for ==.
      Constant *NewV =
          ConstantInt::get(Type::getInt1Ty(Ctx), Pred == CmpInst::ICMP_NE);
      addValue(A, getState(), *NewV, /* CtxI */ nullptr, II.S,
               getAnchorScope());
      return true;
    };

    for (auto &LHSValue : LHSValues)
      for (auto &RHSValue : RHSValues)
        if (!CheckPair(*LHSValue.getValue(), *RHSValue.getValue()))
          return false;
    return true;
  }

  bool handleSelectInst(Attributor &A, SelectInst &SI, ItemInfo II,
                        SmallVectorImpl<ItemInfo> &Worklist) {
    const Instruction *CtxI = II.I.getCtxI();
    bool UsedAssumedInformation = false;

    std::optional<Constant *> C =
        A.getAssumedConstant(*SI.getCondition(), *this, UsedAssumedInformation);
    bool NoValueYet = !C.has_value();
    if (NoValueYet || isa_and_nonnull<UndefValue>(*C))
      return true;
    if (auto *CI = dyn_cast_or_null<ConstantInt>(*C)) {
      if (CI->isZero())
        Worklist.push_back({{*SI.getFalseValue(), CtxI}, II.S});
      else
        Worklist.push_back({{*SI.getTrueValue(), CtxI}, II.S});
    } else if (&SI == &getAssociatedValue()) {
      // We could not simplify the condition, assume both values.
      Worklist.push_back({{*SI.getTrueValue(), CtxI}, II.S});
      Worklist.push_back({{*SI.getFalseValue(), CtxI}, II.S});
    } else {
      std::optional<Value *> SimpleV = A.getAssumedSimplified(
          IRPosition::inst(SI), *this, UsedAssumedInformation, II.S);
      if (!SimpleV.has_value())
        return true;
      if (*SimpleV) {
        addValue(A, getState(), **SimpleV, CtxI, II.S, getAnchorScope());
        return true;
      }
      return false;
    }
    return true;
  }

  bool handleLoadInst(Attributor &A, LoadInst &LI, ItemInfo II,
                      SmallVectorImpl<ItemInfo> &Worklist) {
    SmallSetVector<Value *, 4> PotentialCopies;
    SmallSetVector<Instruction *, 4> PotentialValueOrigins;
    bool UsedAssumedInformation = false;
    if (!AA::getPotentiallyLoadedValues(A, LI, PotentialCopies,
                                        PotentialValueOrigins, *this,
                                        UsedAssumedInformation,
                                        /* OnlyExact */ true)) {
      LLVM_DEBUG(dbgs() << "[AAPotentialValues] Failed to get potentially "
                           "loaded values for load instruction "
                        << LI << "\n");
      return false;
    }

    // Do not simplify loads that are only used in llvm.assume if we cannot also
    // remove all stores that may feed into the load. The reason is that the
    // assume is probably worth something as long as the stores are around.
    InformationCache &InfoCache = A.getInfoCache();
    if (InfoCache.isOnlyUsedByAssume(LI)) {
      if (!llvm::all_of(PotentialValueOrigins, [&](Instruction *I) {
            if (!I || isa<AssumeInst>(I))
              return true;
            if (auto *SI = dyn_cast<StoreInst>(I))
              return A.isAssumedDead(SI->getOperandUse(0), this,
                                     /* LivenessAA */ nullptr,
                                     UsedAssumedInformation,
                                     /* CheckBBLivenessOnly */ false);
            return A.isAssumedDead(*I, this, /* LivenessAA */ nullptr,
                                   UsedAssumedInformation,
                                   /* CheckBBLivenessOnly */ false);
          })) {
        LLVM_DEBUG(dbgs() << "[AAPotentialValues] Load is onl used by assumes "
                             "and we cannot delete all the stores: "
                          << LI << "\n");
        return false;
      }
    }

    // Values have to be dynamically unique or we loose the fact that a
    // single llvm::Value might represent two runtime values (e.g.,
    // stack locations in different recursive calls).
    const Instruction *CtxI = II.I.getCtxI();
    bool ScopeIsLocal = (II.S & AA::Intraprocedural);
    bool AllLocal = ScopeIsLocal;
    bool DynamicallyUnique = llvm::all_of(PotentialCopies, [&](Value *PC) {
      AllLocal &= AA::isValidInScope(*PC, getAnchorScope());
      return AA::isDynamicallyUnique(A, *this, *PC);
    });
    if (!DynamicallyUnique) {
      LLVM_DEBUG(dbgs() << "[AAPotentialValues] Not all potentially loaded "
                           "values are dynamically unique: "
                        << LI << "\n");
      return false;
    }

    for (auto *PotentialCopy : PotentialCopies) {
      if (AllLocal) {
        Worklist.push_back({{*PotentialCopy, CtxI}, II.S});
      } else {
        Worklist.push_back({{*PotentialCopy, CtxI}, AA::Interprocedural});
      }
    }
    if (!AllLocal && ScopeIsLocal)
      addValue(A, getState(), LI, CtxI, AA::Intraprocedural, getAnchorScope());
    return true;
  }

  bool handlePHINode(
      Attributor &A, PHINode &PHI, ItemInfo II,
      SmallVectorImpl<ItemInfo> &Worklist,
      SmallMapVector<const Function *, LivenessInfo, 4> &LivenessAAs) {
    auto GetLivenessInfo = [&](const Function &F) -> LivenessInfo & {
      LivenessInfo &LI = LivenessAAs[&F];
      if (!LI.LivenessAA)
        LI.LivenessAA = A.getAAFor<AAIsDead>(*this, IRPosition::function(F),
                                             DepClassTy::NONE);
      return LI;
    };

    if (&PHI == &getAssociatedValue()) {
      LivenessInfo &LI = GetLivenessInfo(*PHI.getFunction());
      const auto *CI =
          A.getInfoCache().getAnalysisResultForFunction<CycleAnalysis>(
              *PHI.getFunction());

      Cycle *C = nullptr;
      bool CyclePHI = mayBeInCycle(CI, &PHI, /* HeaderOnly */ true, &C);
      for (unsigned u = 0, e = PHI.getNumIncomingValues(); u < e; u++) {
        BasicBlock *IncomingBB = PHI.getIncomingBlock(u);
        if (LI.LivenessAA &&
            LI.LivenessAA->isEdgeDead(IncomingBB, PHI.getParent())) {
          LI.AnyDead = true;
          continue;
        }
        Value *V = PHI.getIncomingValue(u);
        if (V == &PHI)
          continue;

        // If the incoming value is not the PHI but an instruction in the same
        // cycle we might have multiple versions of it flying around.
        if (CyclePHI && isa<Instruction>(V) &&
            (!C || C->contains(cast<Instruction>(V)->getParent())))
          return false;

        Worklist.push_back({{*V, IncomingBB->getTerminator()}, II.S});
      }
      return true;
    }

    bool UsedAssumedInformation = false;
    std::optional<Value *> SimpleV = A.getAssumedSimplified(
        IRPosition::inst(PHI), *this, UsedAssumedInformation, II.S);
    if (!SimpleV.has_value())
      return true;
    if (!(*SimpleV))
      return false;
    addValue(A, getState(), **SimpleV, &PHI, II.S, getAnchorScope());
    return true;
  }

  /// Use the generic, non-optimistic InstSimplfy functionality if we managed to
  /// simplify any operand of the instruction \p I. Return true if successful,
  /// in that case Worklist will be updated.
  bool handleGenericInst(Attributor &A, Instruction &I, ItemInfo II,
                         SmallVectorImpl<ItemInfo> &Worklist) {
    bool SomeSimplified = false;
    bool UsedAssumedInformation = false;

    SmallVector<Value *, 8> NewOps(I.getNumOperands());
    int Idx = 0;
    for (Value *Op : I.operands()) {
      const auto &SimplifiedOp = A.getAssumedSimplified(
          IRPosition::value(*Op, getCallBaseContext()), *this,
          UsedAssumedInformation, AA::Intraprocedural);
      // If we are not sure about any operand we are not sure about the entire
      // instruction, we'll wait.
      if (!SimplifiedOp.has_value())
        return true;

      if (*SimplifiedOp)
        NewOps[Idx] = *SimplifiedOp;
      else
        NewOps[Idx] = Op;

      SomeSimplified |= (NewOps[Idx] != Op);
      ++Idx;
    }

    // We won't bother with the InstSimplify interface if we didn't simplify any
    // operand ourselves.
    if (!SomeSimplified)
      return false;

    InformationCache &InfoCache = A.getInfoCache();
    Function *F = I.getFunction();
    const auto *DT =
        InfoCache.getAnalysisResultForFunction<DominatorTreeAnalysis>(*F);
    const auto *TLI = A.getInfoCache().getTargetLibraryInfoForFunction(*F);
    auto *AC = InfoCache.getAnalysisResultForFunction<AssumptionAnalysis>(*F);

    const DataLayout &DL = I.getDataLayout();
    SimplifyQuery Q(DL, TLI, DT, AC, &I);
    Value *NewV = simplifyInstructionWithOperands(&I, NewOps, Q);
    if (!NewV || NewV == &I)
      return false;

    LLVM_DEBUG(dbgs() << "Generic inst " << I << " assumed simplified to "
                      << *NewV << "\n");
    Worklist.push_back({{*NewV, II.I.getCtxI()}, II.S});
    return true;
  }

  bool simplifyInstruction(
      Attributor &A, Instruction &I, ItemInfo II,
      SmallVectorImpl<ItemInfo> &Worklist,
      SmallMapVector<const Function *, LivenessInfo, 4> &LivenessAAs) {
    if (auto *CI = dyn_cast<CmpInst>(&I))
      return handleCmp(A, *CI, CI->getOperand(0), CI->getOperand(1),
                       CI->getPredicate(), II, Worklist);

    switch (I.getOpcode()) {
    case Instruction::Select:
      return handleSelectInst(A, cast<SelectInst>(I), II, Worklist);
    case Instruction::PHI:
      return handlePHINode(A, cast<PHINode>(I), II, Worklist, LivenessAAs);
    case Instruction::Load:
      return handleLoadInst(A, cast<LoadInst>(I), II, Worklist);
    default:
      return handleGenericInst(A, I, II, Worklist);
    };
    return false;
  }

  void genericValueTraversal(Attributor &A, Value *InitialV) {
    SmallMapVector<const Function *, LivenessInfo, 4> LivenessAAs;

    SmallSet<ItemInfo, 16> Visited;
    SmallVector<ItemInfo, 16> Worklist;
    Worklist.push_back({{*InitialV, getCtxI()}, AA::AnyScope});

    int Iteration = 0;
    do {
      ItemInfo II = Worklist.pop_back_val();
      Value *V = II.I.getValue();
      assert(V);
      const Instruction *CtxI = II.I.getCtxI();
      AA::ValueScope S = II.S;

      // Check if we should process the current value. To prevent endless
      // recursion keep a record of the values we followed!
      if (!Visited.insert(II).second)
        continue;

      // Make sure we limit the compile time for complex expressions.
      if (Iteration++ >= MaxPotentialValuesIterations) {
        LLVM_DEBUG(dbgs() << "Generic value traversal reached iteration limit: "
                          << Iteration << "!\n");
        addValue(A, getState(), *V, CtxI, S, getAnchorScope());
        continue;
      }

      // Explicitly look through calls with a "returned" attribute if we do
      // not have a pointer as stripPointerCasts only works on them.
      Value *NewV = nullptr;
      if (V->getType()->isPointerTy()) {
        NewV = AA::getWithType(*V->stripPointerCasts(), *V->getType());
      } else {
        if (auto *CB = dyn_cast<CallBase>(V))
          if (auto *Callee =
                  dyn_cast_if_present<Function>(CB->getCalledOperand())) {
            for (Argument &Arg : Callee->args())
              if (Arg.hasReturnedAttr()) {
                NewV = CB->getArgOperand(Arg.getArgNo());
                break;
              }
          }
      }
      if (NewV && NewV != V) {
        Worklist.push_back({{*NewV, CtxI}, S});
        continue;
      }

      if (auto *I = dyn_cast<Instruction>(V)) {
        if (simplifyInstruction(A, *I, II, Worklist, LivenessAAs))
          continue;
      }

      if (V != InitialV || isa<Argument>(V))
        if (recurseForValue(A, IRPosition::value(*V), II.S))
          continue;

      // If we haven't stripped anything we give up.
      if (V == InitialV && CtxI == getCtxI()) {
        indicatePessimisticFixpoint();
        return;
      }

      addValue(A, getState(), *V, CtxI, S, getAnchorScope());
    } while (!Worklist.empty());

    // If we actually used liveness information so we have to record a
    // dependence.
    for (auto &It : LivenessAAs)
      if (It.second.AnyDead)
        A.recordDependence(*It.second.LivenessAA, *this, DepClassTy::OPTIONAL);
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(potential_values)
  }
};

struct AAPotentialValuesArgument final : AAPotentialValuesImpl {
  using Base = AAPotentialValuesImpl;
  AAPotentialValuesArgument(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::initialize(..).
  void initialize(Attributor &A) override {
    auto &Arg = cast<Argument>(getAssociatedValue());
    if (Arg.hasPointeeInMemoryValueAttr())
      indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto AssumedBefore = getAssumed();

    unsigned ArgNo = getCalleeArgNo();

    bool UsedAssumedInformation = false;
    SmallVector<AA::ValueAndContext> Values;
    auto CallSitePred = [&](AbstractCallSite ACS) {
      const auto CSArgIRP = IRPosition::callsite_argument(ACS, ArgNo);
      if (CSArgIRP.getPositionKind() == IRP_INVALID)
        return false;

      if (!A.getAssumedSimplifiedValues(CSArgIRP, this, Values,
                                        AA::Interprocedural,
                                        UsedAssumedInformation))
        return false;

      return isValidState();
    };

    if (!A.checkForAllCallSites(CallSitePred, *this,
                                /* RequireAllCallSites */ true,
                                UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    Function *Fn = getAssociatedFunction();
    bool AnyNonLocal = false;
    for (auto &It : Values) {
      if (isa<Constant>(It.getValue())) {
        addValue(A, getState(), *It.getValue(), It.getCtxI(), AA::AnyScope,
                 getAnchorScope());
        continue;
      }
      if (!AA::isDynamicallyUnique(A, *this, *It.getValue()))
        return indicatePessimisticFixpoint();

      if (auto *Arg = dyn_cast<Argument>(It.getValue()))
        if (Arg->getParent() == Fn) {
          addValue(A, getState(), *It.getValue(), It.getCtxI(), AA::AnyScope,
                   getAnchorScope());
          continue;
        }
      addValue(A, getState(), *It.getValue(), It.getCtxI(), AA::Interprocedural,
               getAnchorScope());
      AnyNonLocal = true;
    }
    assert(!undefIsContained() && "Undef should be an explicit value!");
    if (AnyNonLocal)
      giveUpOnIntraprocedural(A);

    return (AssumedBefore == getAssumed()) ? ChangeStatus::UNCHANGED
                                           : ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_ARG_ATTR(potential_values)
  }
};

struct AAPotentialValuesReturned : public AAPotentialValuesFloating {
  using Base = AAPotentialValuesFloating;
  AAPotentialValuesReturned(const IRPosition &IRP, Attributor &A)
      : Base(IRP, A) {}

  /// See AbstractAttribute::initialize(..).
  void initialize(Attributor &A) override {
    Function *F = getAssociatedFunction();
    if (!F || F->isDeclaration() || F->getReturnType()->isVoidTy()) {
      indicatePessimisticFixpoint();
      return;
    }

    for (Argument &Arg : F->args())
      if (Arg.hasReturnedAttr()) {
        addValue(A, getState(), Arg, nullptr, AA::AnyScope, F);
        ReturnedArg = &Arg;
        break;
      }
    if (!A.isFunctionIPOAmendable(*F) ||
        A.hasSimplificationCallback(getIRPosition())) {
      if (!ReturnedArg)
        indicatePessimisticFixpoint();
      else
        indicateOptimisticFixpoint();
    }
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto AssumedBefore = getAssumed();
    bool UsedAssumedInformation = false;

    SmallVector<AA::ValueAndContext> Values;
    Function *AnchorScope = getAnchorScope();
    auto HandleReturnedValue = [&](Value &V, Instruction *CtxI,
                                   bool AddValues) {
      for (AA::ValueScope S : {AA::Interprocedural, AA::Intraprocedural}) {
        Values.clear();
        if (!A.getAssumedSimplifiedValues(IRPosition::value(V), this, Values, S,
                                          UsedAssumedInformation,
                                          /* RecurseForSelectAndPHI */ true))
          return false;
        if (!AddValues)
          continue;
        for (const AA::ValueAndContext &VAC : Values)
          addValue(A, getState(), *VAC.getValue(),
                   VAC.getCtxI() ? VAC.getCtxI() : CtxI, S, AnchorScope);
      }
      return true;
    };

    if (ReturnedArg) {
      HandleReturnedValue(*ReturnedArg, nullptr, true);
    } else {
      auto RetInstPred = [&](Instruction &RetI) {
        bool AddValues = true;
        if (isa<PHINode>(RetI.getOperand(0)) ||
            isa<SelectInst>(RetI.getOperand(0))) {
          addValue(A, getState(), *RetI.getOperand(0), &RetI, AA::AnyScope,
                   AnchorScope);
          AddValues = false;
        }
        return HandleReturnedValue(*RetI.getOperand(0), &RetI, AddValues);
      };

      if (!A.checkForAllInstructions(RetInstPred, *this, {Instruction::Ret},
                                     UsedAssumedInformation,
                                     /* CheckBBLivenessOnly */ true))
        return indicatePessimisticFixpoint();
    }

    return (AssumedBefore == getAssumed()) ? ChangeStatus::UNCHANGED
                                           : ChangeStatus::CHANGED;
  }

  void addValue(Attributor &A, StateType &State, Value &V,
                const Instruction *CtxI, AA::ValueScope S,
                Function *AnchorScope) const override {
    Function *F = getAssociatedFunction();
    if (auto *CB = dyn_cast<CallBase>(&V))
      if (CB->getCalledOperand() == F)
        return;
    Base::addValue(A, State, V, CtxI, S, AnchorScope);
  }

  ChangeStatus manifest(Attributor &A) override {
    if (ReturnedArg)
      return ChangeStatus::UNCHANGED;
    SmallVector<AA::ValueAndContext> Values;
    if (!getAssumedSimplifiedValues(A, Values, AA::ValueScope::Intraprocedural,
                                    /* RecurseForSelectAndPHI */ true))
      return ChangeStatus::UNCHANGED;
    Value *NewVal = getSingleValue(A, *this, getIRPosition(), Values);
    if (!NewVal)
      return ChangeStatus::UNCHANGED;

    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    if (auto *Arg = dyn_cast<Argument>(NewVal)) {
      STATS_DECLTRACK(UniqueReturnValue, FunctionReturn,
                      "Number of function with unique return");
      Changed |= A.manifestAttrs(
          IRPosition::argument(*Arg),
          {Attribute::get(Arg->getContext(), Attribute::Returned)});
      STATS_DECLTRACK_ARG_ATTR(returned);
    }

    auto RetInstPred = [&](Instruction &RetI) {
      Value *RetOp = RetI.getOperand(0);
      if (isa<UndefValue>(RetOp) || RetOp == NewVal)
        return true;
      if (AA::isValidAtPosition({*NewVal, RetI}, A.getInfoCache()))
        if (A.changeUseAfterManifest(RetI.getOperandUse(0), *NewVal))
          Changed = ChangeStatus::CHANGED;
      return true;
    };
    bool UsedAssumedInformation = false;
    (void)A.checkForAllInstructions(RetInstPred, *this, {Instruction::Ret},
                                    UsedAssumedInformation,
                                    /* CheckBBLivenessOnly */ true);
    return Changed;
  }

  ChangeStatus indicatePessimisticFixpoint() override {
    return AAPotentialValues::indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override{
      STATS_DECLTRACK_FNRET_ATTR(potential_values)}

  /// The argumented with an existing `returned` attribute.
  Argument *ReturnedArg = nullptr;
};

struct AAPotentialValuesFunction : AAPotentialValuesImpl {
  AAPotentialValuesFunction(const IRPosition &IRP, Attributor &A)
      : AAPotentialValuesImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    llvm_unreachable("AAPotentialValues(Function|CallSite)::updateImpl will "
                     "not be called");
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_FN_ATTR(potential_values)
  }
};

struct AAPotentialValuesCallSite : AAPotentialValuesFunction {
  AAPotentialValuesCallSite(const IRPosition &IRP, Attributor &A)
      : AAPotentialValuesFunction(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CS_ATTR(potential_values)
  }
};

struct AAPotentialValuesCallSiteReturned : AAPotentialValuesImpl {
  AAPotentialValuesCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAPotentialValuesImpl(IRP, A) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto AssumedBefore = getAssumed();

    Function *Callee = getAssociatedFunction();
    if (!Callee)
      return indicatePessimisticFixpoint();

    bool UsedAssumedInformation = false;
    auto *CB = cast<CallBase>(getCtxI());
    if (CB->isMustTailCall() &&
        !A.isAssumedDead(IRPosition::inst(*CB), this, nullptr,
                         UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    SmallVector<AA::ValueAndContext> Values;
    if (!A.getAssumedSimplifiedValues(IRPosition::returned(*Callee), this,
                                      Values, AA::Intraprocedural,
                                      UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    Function *Caller = CB->getCaller();

    bool AnyNonLocal = false;
    for (auto &It : Values) {
      Value *V = It.getValue();
      std::optional<Value *> CallerV = A.translateArgumentToCallSiteContent(
          V, *CB, *this, UsedAssumedInformation);
      if (!CallerV.has_value()) {
        // Nothing to do as long as no value was determined.
        continue;
      }
      V = *CallerV ? *CallerV : V;
      if (AA::isDynamicallyUnique(A, *this, *V) &&
          AA::isValidInScope(*V, Caller)) {
        if (*CallerV) {
          SmallVector<AA::ValueAndContext> ArgValues;
          IRPosition IRP = IRPosition::value(*V);
          if (auto *Arg = dyn_cast<Argument>(V))
            if (Arg->getParent() == CB->getCalledOperand())
              IRP = IRPosition::callsite_argument(*CB, Arg->getArgNo());
          if (recurseForValue(A, IRP, AA::AnyScope))
            continue;
        }
        addValue(A, getState(), *V, CB, AA::AnyScope, getAnchorScope());
      } else {
        AnyNonLocal = true;
        break;
      }
    }
    if (AnyNonLocal) {
      Values.clear();
      if (!A.getAssumedSimplifiedValues(IRPosition::returned(*Callee), this,
                                        Values, AA::Interprocedural,
                                        UsedAssumedInformation))
        return indicatePessimisticFixpoint();
      AnyNonLocal = false;
      getState() = PotentialLLVMValuesState::getBestState();
      for (auto &It : Values) {
        Value *V = It.getValue();
        if (!AA::isDynamicallyUnique(A, *this, *V))
          return indicatePessimisticFixpoint();
        if (AA::isValidInScope(*V, Caller)) {
          addValue(A, getState(), *V, CB, AA::AnyScope, getAnchorScope());
        } else {
          AnyNonLocal = true;
          addValue(A, getState(), *V, CB, AA::Interprocedural,
                   getAnchorScope());
        }
      }
      if (AnyNonLocal)
        giveUpOnIntraprocedural(A);
    }
    return (AssumedBefore == getAssumed()) ? ChangeStatus::UNCHANGED
                                           : ChangeStatus::CHANGED;
  }

  ChangeStatus indicatePessimisticFixpoint() override {
    return AAPotentialValues::indicatePessimisticFixpoint();
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(potential_values)
  }
};

struct AAPotentialValuesCallSiteArgument : AAPotentialValuesFloating {
  AAPotentialValuesCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAPotentialValuesFloating(IRP, A) {}

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(potential_values)
  }
};
} // namespace

/// ---------------------- Assumption Propagation ------------------------------
namespace {
struct AAAssumptionInfoImpl : public AAAssumptionInfo {
  AAAssumptionInfoImpl(const IRPosition &IRP, Attributor &A,
                       const DenseSet<StringRef> &Known)
      : AAAssumptionInfo(IRP, A, Known) {}

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // Don't manifest a universal set if it somehow made it here.
    if (getKnown().isUniversal())
      return ChangeStatus::UNCHANGED;

    const IRPosition &IRP = getIRPosition();
    SmallVector<StringRef, 0> Set(getAssumed().getSet().begin(),
                                  getAssumed().getSet().end());
    llvm::sort(Set);
    return A.manifestAttrs(IRP,
                           Attribute::get(IRP.getAnchorValue().getContext(),
                                          AssumptionAttrKey,
                                          llvm::join(Set, ",")),
                           /*ForceReplace=*/true);
  }

  bool hasAssumption(const StringRef Assumption) const override {
    return isValidState() && setContains(Assumption);
  }

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *A) const override {
    const SetContents &Known = getKnown();
    const SetContents &Assumed = getAssumed();

    SmallVector<StringRef, 0> Set(Known.getSet().begin(), Known.getSet().end());
    llvm::sort(Set);
    const std::string KnownStr = llvm::join(Set, ",");

    std::string AssumedStr = "Universal";
    if (!Assumed.isUniversal()) {
      Set.assign(Assumed.getSet().begin(), Assumed.getSet().end());
      AssumedStr = llvm::join(Set, ",");
    }
    return "Known [" + KnownStr + "]," + " Assumed [" + AssumedStr + "]";
  }
};

/// Propagates assumption information from parent functions to all of their
/// successors. An assumption can be propagated if the containing function
/// dominates the called function.
///
/// We start with a "known" set of assumptions already valid for the associated
/// function and an "assumed" set that initially contains all possible
/// assumptions. The assumed set is inter-procedurally updated by narrowing its
/// contents as concrete values are known. The concrete values are seeded by the
/// first nodes that are either entries into the call graph, or contains no
/// assumptions. Each node is updated as the intersection of the assumed state
/// with all of its predecessors.
struct AAAssumptionInfoFunction final : AAAssumptionInfoImpl {
  AAAssumptionInfoFunction(const IRPosition &IRP, Attributor &A)
      : AAAssumptionInfoImpl(IRP, A,
                             getAssumptions(*IRP.getAssociatedFunction())) {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    bool Changed = false;

    auto CallSitePred = [&](AbstractCallSite ACS) {
      const auto *AssumptionAA = A.getAAFor<AAAssumptionInfo>(
          *this, IRPosition::callsite_function(*ACS.getInstruction()),
          DepClassTy::REQUIRED);
      if (!AssumptionAA)
        return false;
      // Get the set of assumptions shared by all of this function's callers.
      Changed |= getIntersection(AssumptionAA->getAssumed());
      return !getAssumed().empty() || !getKnown().empty();
    };

    bool UsedAssumedInformation = false;
    // Get the intersection of all assumptions held by this node's predecessors.
    // If we don't know all the call sites then this is either an entry into the
    // call graph or an empty node. This node is known to only contain its own
    // assumptions and can be propagated to its successors.
    if (!A.checkForAllCallSites(CallSitePred, *this, true,
                                UsedAssumedInformation))
      return indicatePessimisticFixpoint();

    return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  void trackStatistics() const override {}
};

/// Assumption Info defined for call sites.
struct AAAssumptionInfoCallSite final : AAAssumptionInfoImpl {

  AAAssumptionInfoCallSite(const IRPosition &IRP, Attributor &A)
      : AAAssumptionInfoImpl(IRP, A, getInitialAssumptions(IRP)) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    const IRPosition &FnPos = IRPosition::function(*getAnchorScope());
    A.getAAFor<AAAssumptionInfo>(*this, FnPos, DepClassTy::REQUIRED);
  }

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    const IRPosition &FnPos = IRPosition::function(*getAnchorScope());
    auto *AssumptionAA =
        A.getAAFor<AAAssumptionInfo>(*this, FnPos, DepClassTy::REQUIRED);
    if (!AssumptionAA)
      return indicatePessimisticFixpoint();
    bool Changed = getIntersection(AssumptionAA->getAssumed());
    return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}

private:
  /// Helper to initialized the known set as all the assumptions this call and
  /// the callee contain.
  DenseSet<StringRef> getInitialAssumptions(const IRPosition &IRP) {
    const CallBase &CB = cast<CallBase>(IRP.getAssociatedValue());
    auto Assumptions = getAssumptions(CB);
    if (const Function *F = CB.getCaller())
      set_union(Assumptions, getAssumptions(*F));
    if (Function *F = IRP.getAssociatedFunction())
      set_union(Assumptions, getAssumptions(*F));
    return Assumptions;
  }
};
} // namespace

AACallGraphNode *AACallEdgeIterator::operator*() const {
  return static_cast<AACallGraphNode *>(const_cast<AACallEdges *>(
      A.getOrCreateAAFor<AACallEdges>(IRPosition::function(**I))));
}

void AttributorCallGraph::print() { llvm::WriteGraph(outs(), this); }

/// ------------------------ UnderlyingObjects ---------------------------------

namespace {
struct AAUnderlyingObjectsImpl
    : StateWrapper<BooleanState, AAUnderlyingObjects> {
  using BaseTy = StateWrapper<BooleanState, AAUnderlyingObjects>;
  AAUnderlyingObjectsImpl(const IRPosition &IRP, Attributor &A) : BaseTy(IRP) {}

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return std::string("UnderlyingObjects ") +
           (isValidState()
                ? (std::string("inter #") +
                   std::to_string(InterAssumedUnderlyingObjects.size()) +
                   " objs" + std::string(", intra #") +
                   std::to_string(IntraAssumedUnderlyingObjects.size()) +
                   " objs")
                : "<invalid>");
  }

  /// See AbstractAttribute::trackStatistics()
  void trackStatistics() const override {}

  /// See AbstractAttribute::updateImpl(...).
  ChangeStatus updateImpl(Attributor &A) override {
    auto &Ptr = getAssociatedValue();

    auto DoUpdate = [&](SmallSetVector<Value *, 8> &UnderlyingObjects,
                        AA::ValueScope Scope) {
      bool UsedAssumedInformation = false;
      SmallPtrSet<Value *, 8> SeenObjects;
      SmallVector<AA::ValueAndContext> Values;

      if (!A.getAssumedSimplifiedValues(IRPosition::value(Ptr), *this, Values,
                                        Scope, UsedAssumedInformation))
        return UnderlyingObjects.insert(&Ptr);

      bool Changed = false;

      for (unsigned I = 0; I < Values.size(); ++I) {
        auto &VAC = Values[I];
        auto *Obj = VAC.getValue();
        Value *UO = getUnderlyingObject(Obj);
        if (UO && UO != VAC.getValue() && SeenObjects.insert(UO).second) {
          const auto *OtherAA = A.getAAFor<AAUnderlyingObjects>(
              *this, IRPosition::value(*UO), DepClassTy::OPTIONAL);
          auto Pred = [&Values](Value &V) {
            Values.emplace_back(V, nullptr);
            return true;
          };

          if (!OtherAA || !OtherAA->forallUnderlyingObjects(Pred, Scope))
            llvm_unreachable(
                "The forall call should not return false at this position");

          continue;
        }

        if (isa<SelectInst>(Obj)) {
          Changed |= handleIndirect(A, *Obj, UnderlyingObjects, Scope);
          continue;
        }
        if (auto *PHI = dyn_cast<PHINode>(Obj)) {
          // Explicitly look through PHIs as we do not care about dynamically
          // uniqueness.
          for (unsigned u = 0, e = PHI->getNumIncomingValues(); u < e; u++) {
            Changed |= handleIndirect(A, *PHI->getIncomingValue(u),
                                      UnderlyingObjects, Scope);
          }
          continue;
        }

        Changed |= UnderlyingObjects.insert(Obj);
      }

      return Changed;
    };

    bool Changed = false;
    Changed |= DoUpdate(IntraAssumedUnderlyingObjects, AA::Intraprocedural);
    Changed |= DoUpdate(InterAssumedUnderlyingObjects, AA::Interprocedural);

    return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  bool forallUnderlyingObjects(
      function_ref<bool(Value &)> Pred,
      AA::ValueScope Scope = AA::Interprocedural) const override {
    if (!isValidState())
      return Pred(getAssociatedValue());

    auto &AssumedUnderlyingObjects = Scope == AA::Intraprocedural
                                         ? IntraAssumedUnderlyingObjects
                                         : InterAssumedUnderlyingObjects;
    for (Value *Obj : AssumedUnderlyingObjects)
      if (!Pred(*Obj))
        return false;

    return true;
  }

private:
  /// Handle the case where the value is not the actual underlying value, such
  /// as a phi node or a select instruction.
  bool handleIndirect(Attributor &A, Value &V,
                      SmallSetVector<Value *, 8> &UnderlyingObjects,
                      AA::ValueScope Scope) {
    bool Changed = false;
    const auto *AA = A.getAAFor<AAUnderlyingObjects>(
        *this, IRPosition::value(V), DepClassTy::OPTIONAL);
    auto Pred = [&](Value &V) {
      Changed |= UnderlyingObjects.insert(&V);
      return true;
    };
    if (!AA || !AA->forallUnderlyingObjects(Pred, Scope))
      llvm_unreachable(
          "The forall call should not return false at this position");
    return Changed;
  }

  /// All the underlying objects collected so far via intra procedural scope.
  SmallSetVector<Value *, 8> IntraAssumedUnderlyingObjects;
  /// All the underlying objects collected so far via inter procedural scope.
  SmallSetVector<Value *, 8> InterAssumedUnderlyingObjects;
};

struct AAUnderlyingObjectsFloating final : AAUnderlyingObjectsImpl {
  AAUnderlyingObjectsFloating(const IRPosition &IRP, Attributor &A)
      : AAUnderlyingObjectsImpl(IRP, A) {}
};

struct AAUnderlyingObjectsArgument final : AAUnderlyingObjectsImpl {
  AAUnderlyingObjectsArgument(const IRPosition &IRP, Attributor &A)
      : AAUnderlyingObjectsImpl(IRP, A) {}
};

struct AAUnderlyingObjectsCallSite final : AAUnderlyingObjectsImpl {
  AAUnderlyingObjectsCallSite(const IRPosition &IRP, Attributor &A)
      : AAUnderlyingObjectsImpl(IRP, A) {}
};

struct AAUnderlyingObjectsCallSiteArgument final : AAUnderlyingObjectsImpl {
  AAUnderlyingObjectsCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAUnderlyingObjectsImpl(IRP, A) {}
};

struct AAUnderlyingObjectsReturned final : AAUnderlyingObjectsImpl {
  AAUnderlyingObjectsReturned(const IRPosition &IRP, Attributor &A)
      : AAUnderlyingObjectsImpl(IRP, A) {}
};

struct AAUnderlyingObjectsCallSiteReturned final : AAUnderlyingObjectsImpl {
  AAUnderlyingObjectsCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAUnderlyingObjectsImpl(IRP, A) {}
};

struct AAUnderlyingObjectsFunction final : AAUnderlyingObjectsImpl {
  AAUnderlyingObjectsFunction(const IRPosition &IRP, Attributor &A)
      : AAUnderlyingObjectsImpl(IRP, A) {}
};
} // namespace

/// ------------------------ Global Value Info  -------------------------------
namespace {
struct AAGlobalValueInfoFloating : public AAGlobalValueInfo {
  AAGlobalValueInfoFloating(const IRPosition &IRP, Attributor &A)
      : AAGlobalValueInfo(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {}

  bool checkUse(Attributor &A, const Use &U, bool &Follow,
                SmallVectorImpl<const Value *> &Worklist) {
    Instruction *UInst = dyn_cast<Instruction>(U.getUser());
    if (!UInst) {
      Follow = true;
      return true;
    }

    LLVM_DEBUG(dbgs() << "[AAGlobalValueInfo] Check use: " << *U.get() << " in "
                      << *UInst << "\n");

    if (auto *Cmp = dyn_cast<ICmpInst>(U.getUser())) {
      int Idx = &Cmp->getOperandUse(0) == &U;
      if (isa<Constant>(Cmp->getOperand(Idx)))
        return true;
      return U == &getAnchorValue();
    }

    // Explicitly catch return instructions.
    if (isa<ReturnInst>(UInst)) {
      auto CallSitePred = [&](AbstractCallSite ACS) {
        Worklist.push_back(ACS.getInstruction());
        return true;
      };
      bool UsedAssumedInformation = false;
      // TODO: We should traverse the uses or add a "non-call-site" CB.
      if (!A.checkForAllCallSites(CallSitePred, *UInst->getFunction(),
                                  /*RequireAllCallSites=*/true, this,
                                  UsedAssumedInformation))
        return false;
      return true;
    }

    // For now we only use special logic for call sites. However, the tracker
    // itself knows about a lot of other non-capturing cases already.
    auto *CB = dyn_cast<CallBase>(UInst);
    if (!CB)
      return false;
    // Direct calls are OK uses.
    if (CB->isCallee(&U))
      return true;
    // Non-argument uses are scary.
    if (!CB->isArgOperand(&U))
      return false;
    // TODO: Iterate callees.
    auto *Fn = dyn_cast<Function>(CB->getCalledOperand());
    if (!Fn || !A.isFunctionIPOAmendable(*Fn))
      return false;

    unsigned ArgNo = CB->getArgOperandNo(&U);
    Worklist.push_back(Fn->getArg(ArgNo));
    return true;
  }

  ChangeStatus updateImpl(Attributor &A) override {
    unsigned NumUsesBefore = Uses.size();

    SmallPtrSet<const Value *, 8> Visited;
    SmallVector<const Value *> Worklist;
    Worklist.push_back(&getAnchorValue());

    auto UsePred = [&](const Use &U, bool &Follow) -> bool {
      Uses.insert(&U);
      switch (DetermineUseCaptureKind(U, nullptr)) {
      case UseCaptureKind::NO_CAPTURE:
        return checkUse(A, U, Follow, Worklist);
      case UseCaptureKind::MAY_CAPTURE:
        return checkUse(A, U, Follow, Worklist);
      case UseCaptureKind::PASSTHROUGH:
        Follow = true;
        return true;
      }
      return true;
    };
    auto EquivalentUseCB = [&](const Use &OldU, const Use &NewU) {
      Uses.insert(&OldU);
      return true;
    };

    while (!Worklist.empty()) {
      const Value *V = Worklist.pop_back_val();
      if (!Visited.insert(V).second)
        continue;
      if (!A.checkForAllUses(UsePred, *this, *V,
                             /* CheckBBLivenessOnly */ true,
                             DepClassTy::OPTIONAL,
                             /* IgnoreDroppableUses */ true, EquivalentUseCB)) {
        return indicatePessimisticFixpoint();
      }
    }

    return Uses.size() == NumUsesBefore ? ChangeStatus::UNCHANGED
                                        : ChangeStatus::CHANGED;
  }

  bool isPotentialUse(const Use &U) const override {
    return !isValidState() || Uses.contains(&U);
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return "[" + std::to_string(Uses.size()) + " uses]";
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(GlobalValuesTracked);
  }

private:
  /// Set of (transitive) uses of this GlobalValue.
  SmallPtrSet<const Use *, 8> Uses;
};
} // namespace

/// ------------------------ Indirect Call Info  -------------------------------
namespace {
struct AAIndirectCallInfoCallSite : public AAIndirectCallInfo {
  AAIndirectCallInfoCallSite(const IRPosition &IRP, Attributor &A)
      : AAIndirectCallInfo(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    auto *MD = getCtxI()->getMetadata(LLVMContext::MD_callees);
    if (!MD && !A.isClosedWorldModule())
      return;

    if (MD) {
      for (const auto &Op : MD->operands())
        if (Function *Callee = mdconst::dyn_extract_or_null<Function>(Op))
          PotentialCallees.insert(Callee);
    } else if (A.isClosedWorldModule()) {
      ArrayRef<Function *> IndirectlyCallableFunctions =
          A.getInfoCache().getIndirectlyCallableFunctions(A);
      PotentialCallees.insert(IndirectlyCallableFunctions.begin(),
                              IndirectlyCallableFunctions.end());
    }

    if (PotentialCallees.empty())
      indicateOptimisticFixpoint();
  }

  ChangeStatus updateImpl(Attributor &A) override {
    CallBase *CB = cast<CallBase>(getCtxI());
    const Use &CalleeUse = CB->getCalledOperandUse();
    Value *FP = CB->getCalledOperand();

    SmallSetVector<Function *, 4> AssumedCalleesNow;
    bool AllCalleesKnownNow = AllCalleesKnown;

    auto CheckPotentialCalleeUse = [&](Function &PotentialCallee,
                                       bool &UsedAssumedInformation) {
      const auto *GIAA = A.getAAFor<AAGlobalValueInfo>(
          *this, IRPosition::value(PotentialCallee), DepClassTy::OPTIONAL);
      if (!GIAA || GIAA->isPotentialUse(CalleeUse))
        return true;
      UsedAssumedInformation = !GIAA->isAtFixpoint();
      return false;
    };

    auto AddPotentialCallees = [&]() {
      for (auto *PotentialCallee : PotentialCallees) {
        bool UsedAssumedInformation = false;
        if (CheckPotentialCalleeUse(*PotentialCallee, UsedAssumedInformation))
          AssumedCalleesNow.insert(PotentialCallee);
      }
    };

    // Use simplification to find potential callees, if !callees was present,
    // fallback to that set if necessary.
    bool UsedAssumedInformation = false;
    SmallVector<AA::ValueAndContext> Values;
    if (!A.getAssumedSimplifiedValues(IRPosition::value(*FP), this, Values,
                                      AA::ValueScope::AnyScope,
                                      UsedAssumedInformation)) {
      if (PotentialCallees.empty())
        return indicatePessimisticFixpoint();
      AddPotentialCallees();
    }

    // Try to find a reason for \p Fn not to be a potential callee. If none was
    // found, add it to the assumed callees set.
    auto CheckPotentialCallee = [&](Function &Fn) {
      if (!PotentialCallees.empty() && !PotentialCallees.count(&Fn))
        return false;

      auto &CachedResult = FilterResults[&Fn];
      if (CachedResult.has_value())
        return CachedResult.value();

      bool UsedAssumedInformation = false;
      if (!CheckPotentialCalleeUse(Fn, UsedAssumedInformation)) {
        if (!UsedAssumedInformation)
          CachedResult = false;
        return false;
      }

      int NumFnArgs = Fn.arg_size();
      int NumCBArgs = CB->arg_size();

      // Check if any excess argument (which we fill up with poison) is known to
      // be UB on undef.
      for (int I = NumCBArgs; I < NumFnArgs; ++I) {
        bool IsKnown = false;
        if (AA::hasAssumedIRAttr<Attribute::NoUndef>(
                A, this, IRPosition::argument(*Fn.getArg(I)),
                DepClassTy::OPTIONAL, IsKnown)) {
          if (IsKnown)
            CachedResult = false;
          return false;
        }
      }

      CachedResult = true;
      return true;
    };

    // Check simplification result, prune known UB callees, also restrict it to
    // the !callees set, if present.
    for (auto &VAC : Values) {
      if (isa<UndefValue>(VAC.getValue()))
        continue;
      if (isa<ConstantPointerNull>(VAC.getValue()) &&
          VAC.getValue()->getType()->getPointerAddressSpace() == 0)
        continue;
      // TODO: Check for known UB, e.g., poison + noundef.
      if (auto *VACFn = dyn_cast<Function>(VAC.getValue())) {
        if (CheckPotentialCallee(*VACFn))
          AssumedCalleesNow.insert(VACFn);
        continue;
      }
      if (!PotentialCallees.empty()) {
        AddPotentialCallees();
        break;
      }
      AllCalleesKnownNow = false;
    }

    if (AssumedCalleesNow == AssumedCallees &&
        AllCalleesKnown == AllCalleesKnownNow)
      return ChangeStatus::UNCHANGED;

    std::swap(AssumedCallees, AssumedCalleesNow);
    AllCalleesKnown = AllCalleesKnownNow;
    return ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    // If we can't specialize at all, give up now.
    if (!AllCalleesKnown && AssumedCallees.empty())
      return ChangeStatus::UNCHANGED;

    CallBase *CB = cast<CallBase>(getCtxI());
    bool UsedAssumedInformation = false;
    if (A.isAssumedDead(*CB, this, /*LivenessAA=*/nullptr,
                        UsedAssumedInformation))
      return ChangeStatus::UNCHANGED;

    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    Value *FP = CB->getCalledOperand();
    if (FP->getType()->getPointerAddressSpace())
      FP = new AddrSpaceCastInst(FP, PointerType::get(FP->getType(), 0),
                                 FP->getName() + ".as0", CB->getIterator());

    bool CBIsVoid = CB->getType()->isVoidTy();
    BasicBlock::iterator IP = CB->getIterator();
    FunctionType *CSFT = CB->getFunctionType();
    SmallVector<Value *> CSArgs(CB->arg_begin(), CB->arg_end());

    // If we know all callees and there are none, the call site is (effectively)
    // dead (or UB).
    if (AssumedCallees.empty()) {
      assert(AllCalleesKnown &&
             "Expected all callees to be known if there are none.");
      A.changeToUnreachableAfterManifest(CB);
      return ChangeStatus::CHANGED;
    }

    // Special handling for the single callee case.
    if (AllCalleesKnown && AssumedCallees.size() == 1) {
      auto *NewCallee = AssumedCallees.front();
      if (isLegalToPromote(*CB, NewCallee)) {
        promoteCall(*CB, NewCallee, nullptr);
        return ChangeStatus::CHANGED;
      }
      Instruction *NewCall =
          CallInst::Create(FunctionCallee(CSFT, NewCallee), CSArgs,
                           CB->getName(), CB->getIterator());
      if (!CBIsVoid)
        A.changeAfterManifest(IRPosition::callsite_returned(*CB), *NewCall);
      A.deleteAfterManifest(*CB);
      return ChangeStatus::CHANGED;
    }

    // For each potential value we create a conditional
    //
    // ```
    // if (ptr == value) value(args);
    // else ...
    // ```
    //
    bool SpecializedForAnyCallees = false;
    bool SpecializedForAllCallees = AllCalleesKnown;
    ICmpInst *LastCmp = nullptr;
    SmallVector<Function *, 8> SkippedAssumedCallees;
    SmallVector<std::pair<CallInst *, Instruction *>> NewCalls;
    for (Function *NewCallee : AssumedCallees) {
      if (!A.shouldSpecializeCallSiteForCallee(*this, *CB, *NewCallee)) {
        SkippedAssumedCallees.push_back(NewCallee);
        SpecializedForAllCallees = false;
        continue;
      }
      SpecializedForAnyCallees = true;

      LastCmp = new ICmpInst(IP, llvm::CmpInst::ICMP_EQ, FP, NewCallee);
      Instruction *ThenTI =
          SplitBlockAndInsertIfThen(LastCmp, IP, /* Unreachable */ false);
      BasicBlock *CBBB = CB->getParent();
      A.registerManifestAddedBasicBlock(*ThenTI->getParent());
      A.registerManifestAddedBasicBlock(*IP->getParent());
      auto *SplitTI = cast<BranchInst>(LastCmp->getNextNode());
      BasicBlock *ElseBB;
      if (&*IP == CB) {
        ElseBB = BasicBlock::Create(ThenTI->getContext(), "",
                                    ThenTI->getFunction(), CBBB);
        A.registerManifestAddedBasicBlock(*ElseBB);
        IP = BranchInst::Create(CBBB, ElseBB)->getIterator();
        SplitTI->replaceUsesOfWith(CBBB, ElseBB);
      } else {
        ElseBB = IP->getParent();
        ThenTI->replaceUsesOfWith(ElseBB, CBBB);
      }
      CastInst *RetBC = nullptr;
      CallInst *NewCall = nullptr;
      if (isLegalToPromote(*CB, NewCallee)) {
        auto *CBClone = cast<CallBase>(CB->clone());
        CBClone->insertBefore(ThenTI);
        NewCall = &cast<CallInst>(promoteCall(*CBClone, NewCallee, &RetBC));
      } else {
        NewCall = CallInst::Create(FunctionCallee(CSFT, NewCallee), CSArgs,
                                   CB->getName(), ThenTI->getIterator());
      }
      NewCalls.push_back({NewCall, RetBC});
    }

    auto AttachCalleeMetadata = [&](CallBase &IndirectCB) {
      if (!AllCalleesKnown)
        return ChangeStatus::UNCHANGED;
      MDBuilder MDB(IndirectCB.getContext());
      MDNode *Callees = MDB.createCallees(SkippedAssumedCallees);
      IndirectCB.setMetadata(LLVMContext::MD_callees, Callees);
      return ChangeStatus::CHANGED;
    };

    if (!SpecializedForAnyCallees)
      return AttachCalleeMetadata(*CB);

    // Check if we need the fallback indirect call still.
    if (SpecializedForAllCallees) {
      LastCmp->replaceAllUsesWith(ConstantInt::getTrue(LastCmp->getContext()));
      LastCmp->eraseFromParent();
      new UnreachableInst(IP->getContext(), IP);
      IP->eraseFromParent();
    } else {
      auto *CBClone = cast<CallInst>(CB->clone());
      CBClone->setName(CB->getName());
      CBClone->insertBefore(*IP->getParent(), IP);
      NewCalls.push_back({CBClone, nullptr});
      AttachCalleeMetadata(*CBClone);
    }

    // Check if we need a PHI to merge the results.
    if (!CBIsVoid) {
      auto *PHI = PHINode::Create(CB->getType(), NewCalls.size(),
                                  CB->getName() + ".phi",
                                  CB->getParent()->getFirstInsertionPt());
      for (auto &It : NewCalls) {
        CallBase *NewCall = It.first;
        Instruction *CallRet = It.second ? It.second : It.first;
        if (CallRet->getType() == CB->getType())
          PHI->addIncoming(CallRet, CallRet->getParent());
        else if (NewCall->getType()->isVoidTy())
          PHI->addIncoming(PoisonValue::get(CB->getType()),
                           NewCall->getParent());
        else
          llvm_unreachable("Call return should match or be void!");
      }
      A.changeAfterManifest(IRPosition::callsite_returned(*CB), *PHI);
    }

    A.deleteAfterManifest(*CB);
    Changed = ChangeStatus::CHANGED;

    return Changed;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    return std::string(AllCalleesKnown ? "eliminate" : "specialize") +
           " indirect call site with " + std::to_string(AssumedCallees.size()) +
           " functions";
  }

  void trackStatistics() const override {
    if (AllCalleesKnown) {
      STATS_DECLTRACK(
          Eliminated, CallSites,
          "Number of indirect call sites eliminated via specialization")
    } else {
      STATS_DECLTRACK(Specialized, CallSites,
                      "Number of indirect call sites specialized")
    }
  }

  bool foreachCallee(function_ref<bool(Function *)> CB) const override {
    return isValidState() && AllCalleesKnown && all_of(AssumedCallees, CB);
  }

private:
  /// Map to remember filter results.
  DenseMap<Function *, std::optional<bool>> FilterResults;

  /// If the !callee metadata was present, this set will contain all potential
  /// callees (superset).
  SmallSetVector<Function *, 4> PotentialCallees;

  /// This set contains all currently assumed calllees, which might grow over
  /// time.
  SmallSetVector<Function *, 4> AssumedCallees;

  /// Flag to indicate if all possible callees are in the AssumedCallees set or
  /// if there could be others.
  bool AllCalleesKnown = true;
};
} // namespace

/// ------------------------ Address Space  ------------------------------------
namespace {
struct AAAddressSpaceImpl : public AAAddressSpace {
  AAAddressSpaceImpl(const IRPosition &IRP, Attributor &A)
      : AAAddressSpace(IRP, A) {}

  int32_t getAddressSpace() const override {
    assert(isValidState() && "the AA is invalid");
    return AssumedAddressSpace;
  }

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    assert(getAssociatedType()->isPtrOrPtrVectorTy() &&
           "Associated value is not a pointer");
  }

  ChangeStatus updateImpl(Attributor &A) override {
    int32_t OldAddressSpace = AssumedAddressSpace;
    auto *AUO = A.getOrCreateAAFor<AAUnderlyingObjects>(getIRPosition(), this,
                                                        DepClassTy::REQUIRED);
    auto Pred = [&](Value &Obj) {
      if (isa<UndefValue>(&Obj))
        return true;
      return takeAddressSpace(Obj.getType()->getPointerAddressSpace());
    };

    if (!AUO->forallUnderlyingObjects(Pred))
      return indicatePessimisticFixpoint();

    return OldAddressSpace == AssumedAddressSpace ? ChangeStatus::UNCHANGED
                                                  : ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {
    Value *AssociatedValue = &getAssociatedValue();
    Value *OriginalValue = peelAddrspacecast(AssociatedValue);
    if (getAddressSpace() == NoAddressSpace ||
        static_cast<uint32_t>(getAddressSpace()) ==
            getAssociatedType()->getPointerAddressSpace())
      return ChangeStatus::UNCHANGED;

    Type *NewPtrTy = PointerType::get(getAssociatedType()->getContext(),
                                      static_cast<uint32_t>(getAddressSpace()));
    bool UseOriginalValue =
        OriginalValue->getType()->getPointerAddressSpace() ==
        static_cast<uint32_t>(getAddressSpace());

    bool Changed = false;

    auto MakeChange = [&](Instruction *I, Use &U) {
      Changed = true;
      if (UseOriginalValue) {
        A.changeUseAfterManifest(U, *OriginalValue);
        return;
      }
      Instruction *CastInst = new AddrSpaceCastInst(OriginalValue, NewPtrTy);
      CastInst->insertBefore(cast<Instruction>(I));
      A.changeUseAfterManifest(U, *CastInst);
    };

    auto Pred = [&](const Use &U, bool &) {
      if (U.get() != AssociatedValue)
        return true;
      auto *Inst = dyn_cast<Instruction>(U.getUser());
      if (!Inst)
        return true;
      // This is a WA to make sure we only change uses from the corresponding
      // CGSCC if the AA is run on CGSCC instead of the entire module.
      if (!A.isRunOn(Inst->getFunction()))
        return true;
      if (isa<LoadInst>(Inst))
        MakeChange(Inst, const_cast<Use &>(U));
      if (isa<StoreInst>(Inst)) {
        // We only make changes if the use is the pointer operand.
        if (U.getOperandNo() == 1)
          MakeChange(Inst, const_cast<Use &>(U));
      }
      return true;
    };

    // It doesn't matter if we can't check all uses as we can simply
    // conservatively ignore those that can not be visited.
    (void)A.checkForAllUses(Pred, *this, getAssociatedValue(),
                            /* CheckBBLivenessOnly */ true);

    return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    if (!isValidState())
      return "addrspace(<invalid>)";
    return "addrspace(" +
           (AssumedAddressSpace == NoAddressSpace
                ? "none"
                : std::to_string(AssumedAddressSpace)) +
           ")";
  }

private:
  int32_t AssumedAddressSpace = NoAddressSpace;

  bool takeAddressSpace(int32_t AS) {
    if (AssumedAddressSpace == NoAddressSpace) {
      AssumedAddressSpace = AS;
      return true;
    }
    return AssumedAddressSpace == AS;
  }

  static Value *peelAddrspacecast(Value *V) {
    if (auto *I = dyn_cast<AddrSpaceCastInst>(V))
      return peelAddrspacecast(I->getPointerOperand());
    if (auto *C = dyn_cast<ConstantExpr>(V))
      if (C->getOpcode() == Instruction::AddrSpaceCast)
        return peelAddrspacecast(C->getOperand(0));
    return V;
  }
};

struct AAAddressSpaceFloating final : AAAddressSpaceImpl {
  AAAddressSpaceFloating(const IRPosition &IRP, Attributor &A)
      : AAAddressSpaceImpl(IRP, A) {}

  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(addrspace);
  }
};

struct AAAddressSpaceReturned final : AAAddressSpaceImpl {
  AAAddressSpaceReturned(const IRPosition &IRP, Attributor &A)
      : AAAddressSpaceImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // TODO: we don't rewrite function argument for now because it will need to
    // rewrite the function signature and all call sites.
    (void)indicatePessimisticFixpoint();
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(addrspace);
  }
};

struct AAAddressSpaceCallSiteReturned final : AAAddressSpaceImpl {
  AAAddressSpaceCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAAddressSpaceImpl(IRP, A) {}

  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(addrspace);
  }
};

struct AAAddressSpaceArgument final : AAAddressSpaceImpl {
  AAAddressSpaceArgument(const IRPosition &IRP, Attributor &A)
      : AAAddressSpaceImpl(IRP, A) {}

  void trackStatistics() const override { STATS_DECLTRACK_ARG_ATTR(addrspace); }
};

struct AAAddressSpaceCallSiteArgument final : AAAddressSpaceImpl {
  AAAddressSpaceCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAAddressSpaceImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // TODO: we don't rewrite call site argument for now because it will need to
    // rewrite the function signature of the callee.
    (void)indicatePessimisticFixpoint();
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(addrspace);
  }
};
} // namespace

/// ----------- Allocation Info ----------
namespace {
struct AAAllocationInfoImpl : public AAAllocationInfo {
  AAAllocationInfoImpl(const IRPosition &IRP, Attributor &A)
      : AAAllocationInfo(IRP, A) {}

  std::optional<TypeSize> getAllocatedSize() const override {
    assert(isValidState() && "the AA is invalid");
    return AssumedAllocatedSize;
  }

  std::optional<TypeSize> findInitialAllocationSize(Instruction *I,
                                                    const DataLayout &DL) {

    // TODO: implement case for malloc like instructions
    switch (I->getOpcode()) {
    case Instruction::Alloca: {
      AllocaInst *AI = cast<AllocaInst>(I);
      return AI->getAllocationSize(DL);
    }
    default:
      return std::nullopt;
    }
  }

  ChangeStatus updateImpl(Attributor &A) override {

    const IRPosition &IRP = getIRPosition();
    Instruction *I = IRP.getCtxI();

    // TODO: update check for malloc like calls
    if (!isa<AllocaInst>(I))
      return indicatePessimisticFixpoint();

    bool IsKnownNoCapture;
    if (!AA::hasAssumedIRAttr<Attribute::NoCapture>(
            A, this, IRP, DepClassTy::OPTIONAL, IsKnownNoCapture))
      return indicatePessimisticFixpoint();

    const AAPointerInfo *PI =
        A.getOrCreateAAFor<AAPointerInfo>(IRP, *this, DepClassTy::REQUIRED);

    if (!PI)
      return indicatePessimisticFixpoint();

    if (!PI->getState().isValidState())
      return indicatePessimisticFixpoint();

    const DataLayout &DL = A.getDataLayout();
    const auto AllocationSize = findInitialAllocationSize(I, DL);

    // If allocation size is nullopt, we give up.
    if (!AllocationSize)
      return indicatePessimisticFixpoint();

    // For zero sized allocations, we give up.
    // Since we can't reduce further
    if (*AllocationSize == 0)
      return indicatePessimisticFixpoint();

    int64_t BinSize = PI->numOffsetBins();

    // TODO: implement for multiple bins
    if (BinSize > 1)
      return indicatePessimisticFixpoint();

    if (BinSize == 0) {
      auto NewAllocationSize = std::optional<TypeSize>(TypeSize(0, false));
      if (!changeAllocationSize(NewAllocationSize))
        return ChangeStatus::UNCHANGED;
      return ChangeStatus::CHANGED;
    }

    // TODO: refactor this to be part of multiple bin case
    const auto &It = PI->begin();

    // TODO: handle if Offset is not zero
    if (It->first.Offset != 0)
      return indicatePessimisticFixpoint();

    uint64_t SizeOfBin = It->first.Offset + It->first.Size;

    if (SizeOfBin >= *AllocationSize)
      return indicatePessimisticFixpoint();

    auto NewAllocationSize =
        std::optional<TypeSize>(TypeSize(SizeOfBin * 8, false));

    if (!changeAllocationSize(NewAllocationSize))
      return ChangeStatus::UNCHANGED;

    return ChangeStatus::CHANGED;
  }

  /// See AbstractAttribute::manifest(...).
  ChangeStatus manifest(Attributor &A) override {

    assert(isValidState() &&
           "Manifest should only be called if the state is valid.");

    Instruction *I = getIRPosition().getCtxI();

    auto FixedAllocatedSizeInBits = getAllocatedSize()->getFixedValue();

    unsigned long NumBytesToAllocate = (FixedAllocatedSizeInBits + 7) / 8;

    switch (I->getOpcode()) {
    // TODO: add case for malloc like calls
    case Instruction::Alloca: {

      AllocaInst *AI = cast<AllocaInst>(I);

      Type *CharType = Type::getInt8Ty(I->getContext());

      auto *NumBytesToValue =
          ConstantInt::get(I->getContext(), APInt(32, NumBytesToAllocate));

      BasicBlock::iterator insertPt = AI->getIterator();
      insertPt = std::next(insertPt);
      AllocaInst *NewAllocaInst =
          new AllocaInst(CharType, AI->getAddressSpace(), NumBytesToValue,
                         AI->getAlign(), AI->getName(), insertPt);

      if (A.changeAfterManifest(IRPosition::inst(*AI), *NewAllocaInst))
        return ChangeStatus::CHANGED;

      break;
    }
    default:
      break;
    }

    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractAttribute::getAsStr().
  const std::string getAsStr(Attributor *A) const override {
    if (!isValidState())
      return "allocationinfo(<invalid>)";
    return "allocationinfo(" +
           (AssumedAllocatedSize == HasNoAllocationSize
                ? "none"
                : std::to_string(AssumedAllocatedSize->getFixedValue())) +
           ")";
  }

private:
  std::optional<TypeSize> AssumedAllocatedSize = HasNoAllocationSize;

  // Maintain the computed allocation size of the object.
  // Returns (bool) weather the size of the allocation was modified or not.
  bool changeAllocationSize(std::optional<TypeSize> Size) {
    if (AssumedAllocatedSize == HasNoAllocationSize ||
        AssumedAllocatedSize != Size) {
      AssumedAllocatedSize = Size;
      return true;
    }
    return false;
  }
};

struct AAAllocationInfoFloating : AAAllocationInfoImpl {
  AAAllocationInfoFloating(const IRPosition &IRP, Attributor &A)
      : AAAllocationInfoImpl(IRP, A) {}

  void trackStatistics() const override {
    STATS_DECLTRACK_FLOATING_ATTR(allocationinfo);
  }
};

struct AAAllocationInfoReturned : AAAllocationInfoImpl {
  AAAllocationInfoReturned(const IRPosition &IRP, Attributor &A)
      : AAAllocationInfoImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // TODO: we don't rewrite function argument for now because it will need to
    // rewrite the function signature and all call sites
    (void)indicatePessimisticFixpoint();
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_FNRET_ATTR(allocationinfo);
  }
};

struct AAAllocationInfoCallSiteReturned : AAAllocationInfoImpl {
  AAAllocationInfoCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAAllocationInfoImpl(IRP, A) {}

  void trackStatistics() const override {
    STATS_DECLTRACK_CSRET_ATTR(allocationinfo);
  }
};

struct AAAllocationInfoArgument : AAAllocationInfoImpl {
  AAAllocationInfoArgument(const IRPosition &IRP, Attributor &A)
      : AAAllocationInfoImpl(IRP, A) {}

  void trackStatistics() const override {
    STATS_DECLTRACK_ARG_ATTR(allocationinfo);
  }
};

struct AAAllocationInfoCallSiteArgument : AAAllocationInfoImpl {
  AAAllocationInfoCallSiteArgument(const IRPosition &IRP, Attributor &A)
      : AAAllocationInfoImpl(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {

    (void)indicatePessimisticFixpoint();
  }

  void trackStatistics() const override {
    STATS_DECLTRACK_CSARG_ATTR(allocationinfo);
  }
};
} // namespace

const char AANoUnwind::ID = 0;
const char AANoSync::ID = 0;
const char AANoFree::ID = 0;
const char AANonNull::ID = 0;
const char AAMustProgress::ID = 0;
const char AANoRecurse::ID = 0;
const char AANonConvergent::ID = 0;
const char AAWillReturn::ID = 0;
const char AAUndefinedBehavior::ID = 0;
const char AANoAlias::ID = 0;
const char AAIntraFnReachability::ID = 0;
const char AANoReturn::ID = 0;
const char AAIsDead::ID = 0;
const char AADereferenceable::ID = 0;
const char AAAlign::ID = 0;
const char AAInstanceInfo::ID = 0;
const char AANoCapture::ID = 0;
const char AAValueSimplify::ID = 0;
const char AAHeapToStack::ID = 0;
const char AAPrivatizablePtr::ID = 0;
const char AAMemoryBehavior::ID = 0;
const char AAMemoryLocation::ID = 0;
const char AAValueConstantRange::ID = 0;
const char AAPotentialConstantValues::ID = 0;
const char AAPotentialValues::ID = 0;
const char AANoUndef::ID = 0;
const char AANoFPClass::ID = 0;
const char AACallEdges::ID = 0;
const char AAInterFnReachability::ID = 0;
const char AAPointerInfo::ID = 0;
const char AAAssumptionInfo::ID = 0;
const char AAUnderlyingObjects::ID = 0;
const char AAAddressSpace::ID = 0;
const char AAAllocationInfo::ID = 0;
const char AAIndirectCallInfo::ID = 0;
const char AAGlobalValueInfo::ID = 0;
const char AADenormalFPMath::ID = 0;

// Macro magic to create the static generator function for attributes that
// follow the naming scheme.

#define SWITCH_PK_INV(CLASS, PK, POS_NAME)                                     \
  case IRPosition::PK:                                                         \
    llvm_unreachable("Cannot create " #CLASS " for a " POS_NAME " position!");

#define SWITCH_PK_CREATE(CLASS, IRP, PK, SUFFIX)                               \
  case IRPosition::PK:                                                         \
    AA = new (A.Allocator) CLASS##SUFFIX(IRP, A);                              \
    ++NumAAs;                                                                  \
    break;

#define CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(CLASS)                 \
  CLASS &CLASS::createForPosition(const IRPosition &IRP, Attributor &A) {      \
    CLASS *AA = nullptr;                                                       \
    switch (IRP.getPositionKind()) {                                           \
      SWITCH_PK_INV(CLASS, IRP_INVALID, "invalid")                             \
      SWITCH_PK_INV(CLASS, IRP_FLOAT, "floating")                              \
      SWITCH_PK_INV(CLASS, IRP_ARGUMENT, "argument")                           \
      SWITCH_PK_INV(CLASS, IRP_RETURNED, "returned")                           \
      SWITCH_PK_INV(CLASS, IRP_CALL_SITE_RETURNED, "call site returned")       \
      SWITCH_PK_INV(CLASS, IRP_CALL_SITE_ARGUMENT, "call site argument")       \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_FUNCTION, Function)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE, CallSite)                    \
    }                                                                          \
    return *AA;                                                                \
  }

#define CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(CLASS)                    \
  CLASS &CLASS::createForPosition(const IRPosition &IRP, Attributor &A) {      \
    CLASS *AA = nullptr;                                                       \
    switch (IRP.getPositionKind()) {                                           \
      SWITCH_PK_INV(CLASS, IRP_INVALID, "invalid")                             \
      SWITCH_PK_INV(CLASS, IRP_FUNCTION, "function")                           \
      SWITCH_PK_INV(CLASS, IRP_CALL_SITE, "call site")                         \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_FLOAT, Floating)                        \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_ARGUMENT, Argument)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_RETURNED, Returned)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE_RETURNED, CallSiteReturned)   \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE_ARGUMENT, CallSiteArgument)   \
    }                                                                          \
    return *AA;                                                                \
  }

#define CREATE_ABSTRACT_ATTRIBUTE_FOR_ONE_POSITION(POS, SUFFIX, CLASS)         \
  CLASS &CLASS::createForPosition(const IRPosition &IRP, Attributor &A) {      \
    CLASS *AA = nullptr;                                                       \
    switch (IRP.getPositionKind()) {                                           \
      SWITCH_PK_CREATE(CLASS, IRP, POS, SUFFIX)                                \
    default:                                                                   \
      llvm_unreachable("Cannot create " #CLASS " for position otherthan " #POS \
                       " position!");                                          \
    }                                                                          \
    return *AA;                                                                \
  }

#define CREATE_ALL_ABSTRACT_ATTRIBUTE_FOR_POSITION(CLASS)                      \
  CLASS &CLASS::createForPosition(const IRPosition &IRP, Attributor &A) {      \
    CLASS *AA = nullptr;                                                       \
    switch (IRP.getPositionKind()) {                                           \
      SWITCH_PK_INV(CLASS, IRP_INVALID, "invalid")                             \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_FUNCTION, Function)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE, CallSite)                    \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_FLOAT, Floating)                        \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_ARGUMENT, Argument)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_RETURNED, Returned)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE_RETURNED, CallSiteReturned)   \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE_ARGUMENT, CallSiteArgument)   \
    }                                                                          \
    return *AA;                                                                \
  }

#define CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION(CLASS)            \
  CLASS &CLASS::createForPosition(const IRPosition &IRP, Attributor &A) {      \
    CLASS *AA = nullptr;                                                       \
    switch (IRP.getPositionKind()) {                                           \
      SWITCH_PK_INV(CLASS, IRP_INVALID, "invalid")                             \
      SWITCH_PK_INV(CLASS, IRP_ARGUMENT, "argument")                           \
      SWITCH_PK_INV(CLASS, IRP_FLOAT, "floating")                              \
      SWITCH_PK_INV(CLASS, IRP_RETURNED, "returned")                           \
      SWITCH_PK_INV(CLASS, IRP_CALL_SITE_RETURNED, "call site returned")       \
      SWITCH_PK_INV(CLASS, IRP_CALL_SITE_ARGUMENT, "call site argument")       \
      SWITCH_PK_INV(CLASS, IRP_CALL_SITE, "call site")                         \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_FUNCTION, Function)                     \
    }                                                                          \
    return *AA;                                                                \
  }

#define CREATE_NON_RET_ABSTRACT_ATTRIBUTE_FOR_POSITION(CLASS)                  \
  CLASS &CLASS::createForPosition(const IRPosition &IRP, Attributor &A) {      \
    CLASS *AA = nullptr;                                                       \
    switch (IRP.getPositionKind()) {                                           \
      SWITCH_PK_INV(CLASS, IRP_INVALID, "invalid")                             \
      SWITCH_PK_INV(CLASS, IRP_RETURNED, "returned")                           \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_FUNCTION, Function)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE, CallSite)                    \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_FLOAT, Floating)                        \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_ARGUMENT, Argument)                     \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE_RETURNED, CallSiteReturned)   \
      SWITCH_PK_CREATE(CLASS, IRP, IRP_CALL_SITE_ARGUMENT, CallSiteArgument)   \
    }                                                                          \
    return *AA;                                                                \
  }

CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoUnwind)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoSync)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoRecurse)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAWillReturn)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoReturn)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAMemoryLocation)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AACallEdges)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAAssumptionInfo)
CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAMustProgress)

CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANonNull)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoAlias)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAPrivatizablePtr)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AADereferenceable)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAAlign)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAInstanceInfo)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoCapture)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAValueConstantRange)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAPotentialConstantValues)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAPotentialValues)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoUndef)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoFPClass)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAPointerInfo)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAAddressSpace)
CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAAllocationInfo)

CREATE_ALL_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAValueSimplify)
CREATE_ALL_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAIsDead)
CREATE_ALL_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANoFree)
CREATE_ALL_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAUnderlyingObjects)

CREATE_ABSTRACT_ATTRIBUTE_FOR_ONE_POSITION(IRP_CALL_SITE, CallSite,
                                           AAIndirectCallInfo)
CREATE_ABSTRACT_ATTRIBUTE_FOR_ONE_POSITION(IRP_FLOAT, Floating,
                                           AAGlobalValueInfo)

CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAHeapToStack)
CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAUndefinedBehavior)
CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION(AANonConvergent)
CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAIntraFnReachability)
CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAInterFnReachability)
CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION(AADenormalFPMath)

CREATE_NON_RET_ABSTRACT_ATTRIBUTE_FOR_POSITION(AAMemoryBehavior)

#undef CREATE_FUNCTION_ONLY_ABSTRACT_ATTRIBUTE_FOR_POSITION
#undef CREATE_FUNCTION_ABSTRACT_ATTRIBUTE_FOR_POSITION
#undef CREATE_NON_RET_ABSTRACT_ATTRIBUTE_FOR_POSITION
#undef CREATE_VALUE_ABSTRACT_ATTRIBUTE_FOR_POSITION
#undef CREATE_ALL_ABSTRACT_ATTRIBUTE_FOR_POSITION
#undef CREATE_ABSTRACT_ATTRIBUTE_FOR_ONE_POSITION
#undef SWITCH_PK_CREATE
#undef SWITCH_PK_INV
