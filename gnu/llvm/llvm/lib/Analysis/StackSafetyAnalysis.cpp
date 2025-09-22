//===- StackSafetyAnalysis.cpp - Stack memory safety analysis -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/StackLifetime.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <memory>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "stack-safety"

STATISTIC(NumAllocaStackSafe, "Number of safe allocas");
STATISTIC(NumAllocaTotal, "Number of total allocas");

STATISTIC(NumCombinedCalleeLookupTotal,
          "Number of total callee lookups on combined index.");
STATISTIC(NumCombinedCalleeLookupFailed,
          "Number of failed callee lookups on combined index.");
STATISTIC(NumModuleCalleeLookupTotal,
          "Number of total callee lookups on module index.");
STATISTIC(NumModuleCalleeLookupFailed,
          "Number of failed callee lookups on module index.");
STATISTIC(NumCombinedParamAccessesBefore,
          "Number of total param accesses before generateParamAccessSummary.");
STATISTIC(NumCombinedParamAccessesAfter,
          "Number of total param accesses after generateParamAccessSummary.");
STATISTIC(NumCombinedDataFlowNodes,
          "Number of total nodes in combined index for dataflow processing.");
STATISTIC(NumIndexCalleeUnhandled, "Number of index callee which are unhandled.");
STATISTIC(NumIndexCalleeMultipleWeak, "Number of index callee non-unique weak.");
STATISTIC(NumIndexCalleeMultipleExternal, "Number of index callee non-unique external.");


static cl::opt<int> StackSafetyMaxIterations("stack-safety-max-iterations",
                                             cl::init(20), cl::Hidden);

static cl::opt<bool> StackSafetyPrint("stack-safety-print", cl::init(false),
                                      cl::Hidden);

static cl::opt<bool> StackSafetyRun("stack-safety-run", cl::init(false),
                                    cl::Hidden);

namespace {

// Check if we should bailout for such ranges.
bool isUnsafe(const ConstantRange &R) {
  return R.isEmptySet() || R.isFullSet() || R.isUpperSignWrapped();
}

ConstantRange addOverflowNever(const ConstantRange &L, const ConstantRange &R) {
  assert(!L.isSignWrappedSet());
  assert(!R.isSignWrappedSet());
  if (L.signedAddMayOverflow(R) !=
      ConstantRange::OverflowResult::NeverOverflows)
    return ConstantRange::getFull(L.getBitWidth());
  ConstantRange Result = L.add(R);
  assert(!Result.isSignWrappedSet());
  return Result;
}

ConstantRange unionNoWrap(const ConstantRange &L, const ConstantRange &R) {
  assert(!L.isSignWrappedSet());
  assert(!R.isSignWrappedSet());
  auto Result = L.unionWith(R);
  // Two non-wrapped sets can produce wrapped.
  if (Result.isSignWrappedSet())
    Result = ConstantRange::getFull(Result.getBitWidth());
  return Result;
}

/// Describes use of address in as a function call argument.
template <typename CalleeTy> struct CallInfo {
  /// Function being called.
  const CalleeTy *Callee = nullptr;
  /// Index of argument which pass address.
  size_t ParamNo = 0;

  CallInfo(const CalleeTy *Callee, size_t ParamNo)
      : Callee(Callee), ParamNo(ParamNo) {}

  struct Less {
    bool operator()(const CallInfo &L, const CallInfo &R) const {
      return std::tie(L.ParamNo, L.Callee) < std::tie(R.ParamNo, R.Callee);
    }
  };
};

/// Describe uses of address (alloca or parameter) inside of the function.
template <typename CalleeTy> struct UseInfo {
  // Access range if the address (alloca or parameters).
  // It is allowed to be empty-set when there are no known accesses.
  ConstantRange Range;
  std::set<const Instruction *> UnsafeAccesses;

  // List of calls which pass address as an argument.
  // Value is offset range of address from base address (alloca or calling
  // function argument). Range should never set to empty-set, that is an invalid
  // access range that can cause empty-set to be propagated with
  // ConstantRange::add
  using CallsTy = std::map<CallInfo<CalleeTy>, ConstantRange,
                           typename CallInfo<CalleeTy>::Less>;
  CallsTy Calls;

  UseInfo(unsigned PointerSize) : Range{PointerSize, false} {}

  void updateRange(const ConstantRange &R) { Range = unionNoWrap(Range, R); }
  void addRange(const Instruction *I, const ConstantRange &R, bool IsSafe) {
    if (!IsSafe)
      UnsafeAccesses.insert(I);
    updateRange(R);
  }
};

template <typename CalleeTy>
raw_ostream &operator<<(raw_ostream &OS, const UseInfo<CalleeTy> &U) {
  OS << U.Range;
  for (auto &Call : U.Calls)
    OS << ", "
       << "@" << Call.first.Callee->getName() << "(arg" << Call.first.ParamNo
       << ", " << Call.second << ")";
  return OS;
}

/// Calculate the allocation size of a given alloca. Returns empty range
// in case of confution.
ConstantRange getStaticAllocaSizeRange(const AllocaInst &AI) {
  const DataLayout &DL = AI.getDataLayout();
  TypeSize TS = DL.getTypeAllocSize(AI.getAllocatedType());
  unsigned PointerSize = DL.getPointerTypeSizeInBits(AI.getType());
  // Fallback to empty range for alloca size.
  ConstantRange R = ConstantRange::getEmpty(PointerSize);
  if (TS.isScalable())
    return R;
  APInt APSize(PointerSize, TS.getFixedValue(), true);
  if (APSize.isNonPositive())
    return R;
  if (AI.isArrayAllocation()) {
    const auto *C = dyn_cast<ConstantInt>(AI.getArraySize());
    if (!C)
      return R;
    bool Overflow = false;
    APInt Mul = C->getValue();
    if (Mul.isNonPositive())
      return R;
    Mul = Mul.sextOrTrunc(PointerSize);
    APSize = APSize.smul_ov(Mul, Overflow);
    if (Overflow)
      return R;
  }
  R = ConstantRange(APInt::getZero(PointerSize), APSize);
  assert(!isUnsafe(R));
  return R;
}

template <typename CalleeTy> struct FunctionInfo {
  std::map<const AllocaInst *, UseInfo<CalleeTy>> Allocas;
  std::map<uint32_t, UseInfo<CalleeTy>> Params;
  // TODO: describe return value as depending on one or more of its arguments.

  // StackSafetyDataFlowAnalysis counter stored here for faster access.
  int UpdateCount = 0;

  void print(raw_ostream &O, StringRef Name, const Function *F) const {
    // TODO: Consider different printout format after
    // StackSafetyDataFlowAnalysis. Calls and parameters are irrelevant then.
    O << "  @" << Name << ((F && F->isDSOLocal()) ? "" : " dso_preemptable")
      << ((F && F->isInterposable()) ? " interposable" : "") << "\n";

    O << "    args uses:\n";
    for (auto &KV : Params) {
      O << "      ";
      if (F)
        O << F->getArg(KV.first)->getName();
      else
        O << formatv("arg{0}", KV.first);
      O << "[]: " << KV.second << "\n";
    }

    O << "    allocas uses:\n";
    if (F) {
      for (const auto &I : instructions(F)) {
        if (const AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
          auto &AS = Allocas.find(AI)->second;
          O << "      " << AI->getName() << "["
            << getStaticAllocaSizeRange(*AI).getUpper() << "]: " << AS << "\n";
        }
      }
    } else {
      assert(Allocas.empty());
    }
  }
};

using GVToSSI = std::map<const GlobalValue *, FunctionInfo<GlobalValue>>;

} // namespace

struct StackSafetyInfo::InfoTy {
  FunctionInfo<GlobalValue> Info;
};

struct StackSafetyGlobalInfo::InfoTy {
  GVToSSI Info;
  SmallPtrSet<const AllocaInst *, 8> SafeAllocas;
  std::set<const Instruction *> UnsafeAccesses;
};

namespace {

class StackSafetyLocalAnalysis {
  Function &F;
  const DataLayout &DL;
  ScalarEvolution &SE;
  unsigned PointerSize = 0;

  const ConstantRange UnknownRange;

  /// FIXME: This function is a bandaid, it's only needed
  /// because this pass doesn't handle address spaces of different pointer
  /// sizes.
  ///
  /// \returns \p Val's SCEV as a pointer of AS zero, or nullptr if it can't be
  /// converted to AS 0.
  const SCEV *getSCEVAsPointer(Value *Val);

  ConstantRange offsetFrom(Value *Addr, Value *Base);
  ConstantRange getAccessRange(Value *Addr, Value *Base,
                               const ConstantRange &SizeRange);
  ConstantRange getAccessRange(Value *Addr, Value *Base, TypeSize Size);
  ConstantRange getMemIntrinsicAccessRange(const MemIntrinsic *MI, const Use &U,
                                           Value *Base);

  void analyzeAllUses(Value *Ptr, UseInfo<GlobalValue> &AS,
                      const StackLifetime &SL);


  bool isSafeAccess(const Use &U, AllocaInst *AI, const SCEV *AccessSize);
  bool isSafeAccess(const Use &U, AllocaInst *AI, Value *V);
  bool isSafeAccess(const Use &U, AllocaInst *AI, TypeSize AccessSize);

public:
  StackSafetyLocalAnalysis(Function &F, ScalarEvolution &SE)
      : F(F), DL(F.getDataLayout()), SE(SE),
        PointerSize(DL.getPointerSizeInBits()),
        UnknownRange(PointerSize, true) {}

  // Run the transformation on the associated function.
  FunctionInfo<GlobalValue> run();
};

const SCEV *StackSafetyLocalAnalysis::getSCEVAsPointer(Value *Val) {
  Type *ValTy = Val->getType();

  // We don't handle targets with multiple address spaces.
  if (!ValTy->isPointerTy()) {
    auto *PtrTy = PointerType::getUnqual(SE.getContext());
    return SE.getTruncateOrZeroExtend(SE.getSCEV(Val), PtrTy);
  }

  if (ValTy->getPointerAddressSpace() != 0)
    return nullptr;
  return SE.getSCEV(Val);
}

ConstantRange StackSafetyLocalAnalysis::offsetFrom(Value *Addr, Value *Base) {
  if (!SE.isSCEVable(Addr->getType()) || !SE.isSCEVable(Base->getType()))
    return UnknownRange;

  const SCEV *AddrExp = getSCEVAsPointer(Addr);
  const SCEV *BaseExp = getSCEVAsPointer(Base);
  if (!AddrExp || !BaseExp)
    return UnknownRange;

  const SCEV *Diff = SE.getMinusSCEV(AddrExp, BaseExp);
  if (isa<SCEVCouldNotCompute>(Diff))
    return UnknownRange;

  ConstantRange Offset = SE.getSignedRange(Diff);
  if (isUnsafe(Offset))
    return UnknownRange;
  return Offset.sextOrTrunc(PointerSize);
}

ConstantRange
StackSafetyLocalAnalysis::getAccessRange(Value *Addr, Value *Base,
                                         const ConstantRange &SizeRange) {
  // Zero-size loads and stores do not access memory.
  if (SizeRange.isEmptySet())
    return ConstantRange::getEmpty(PointerSize);
  assert(!isUnsafe(SizeRange));

  ConstantRange Offsets = offsetFrom(Addr, Base);
  if (isUnsafe(Offsets))
    return UnknownRange;

  Offsets = addOverflowNever(Offsets, SizeRange);
  if (isUnsafe(Offsets))
    return UnknownRange;
  return Offsets;
}

ConstantRange StackSafetyLocalAnalysis::getAccessRange(Value *Addr, Value *Base,
                                                       TypeSize Size) {
  if (Size.isScalable())
    return UnknownRange;
  APInt APSize(PointerSize, Size.getFixedValue(), true);
  if (APSize.isNegative())
    return UnknownRange;
  return getAccessRange(Addr, Base,
                        ConstantRange(APInt::getZero(PointerSize), APSize));
}

ConstantRange StackSafetyLocalAnalysis::getMemIntrinsicAccessRange(
    const MemIntrinsic *MI, const Use &U, Value *Base) {
  if (const auto *MTI = dyn_cast<MemTransferInst>(MI)) {
    if (MTI->getRawSource() != U && MTI->getRawDest() != U)
      return ConstantRange::getEmpty(PointerSize);
  } else {
    if (MI->getRawDest() != U)
      return ConstantRange::getEmpty(PointerSize);
  }

  auto *CalculationTy = IntegerType::getIntNTy(SE.getContext(), PointerSize);
  if (!SE.isSCEVable(MI->getLength()->getType()))
    return UnknownRange;

  const SCEV *Expr =
      SE.getTruncateOrZeroExtend(SE.getSCEV(MI->getLength()), CalculationTy);
  ConstantRange Sizes = SE.getSignedRange(Expr);
  if (!Sizes.getUpper().isStrictlyPositive() || isUnsafe(Sizes))
    return UnknownRange;
  Sizes = Sizes.sextOrTrunc(PointerSize);
  ConstantRange SizeRange(APInt::getZero(PointerSize), Sizes.getUpper() - 1);
  return getAccessRange(U, Base, SizeRange);
}

bool StackSafetyLocalAnalysis::isSafeAccess(const Use &U, AllocaInst *AI,
                                            Value *V) {
  return isSafeAccess(U, AI, SE.getSCEV(V));
}

bool StackSafetyLocalAnalysis::isSafeAccess(const Use &U, AllocaInst *AI,
                                            TypeSize TS) {
  if (TS.isScalable())
    return false;
  auto *CalculationTy = IntegerType::getIntNTy(SE.getContext(), PointerSize);
  const SCEV *SV = SE.getConstant(CalculationTy, TS.getFixedValue());
  return isSafeAccess(U, AI, SV);
}

bool StackSafetyLocalAnalysis::isSafeAccess(const Use &U, AllocaInst *AI,
                                            const SCEV *AccessSize) {

  if (!AI)
    return true; // This only judges whether it is a safe *stack* access.
  if (isa<SCEVCouldNotCompute>(AccessSize))
    return false;

  const auto *I = cast<Instruction>(U.getUser());

  const SCEV *AddrExp = getSCEVAsPointer(U.get());
  const SCEV *BaseExp = getSCEVAsPointer(AI);
  if (!AddrExp || !BaseExp)
    return false;

  const SCEV *Diff = SE.getMinusSCEV(AddrExp, BaseExp);
  if (isa<SCEVCouldNotCompute>(Diff))
    return false;

  auto Size = getStaticAllocaSizeRange(*AI);

  auto *CalculationTy = IntegerType::getIntNTy(SE.getContext(), PointerSize);
  auto ToDiffTy = [&](const SCEV *V) {
    return SE.getTruncateOrZeroExtend(V, CalculationTy);
  };
  const SCEV *Min = ToDiffTy(SE.getConstant(Size.getLower()));
  const SCEV *Max = SE.getMinusSCEV(ToDiffTy(SE.getConstant(Size.getUpper())),
                                    ToDiffTy(AccessSize));
  return SE.evaluatePredicateAt(ICmpInst::Predicate::ICMP_SGE, Diff, Min, I)
             .value_or(false) &&
         SE.evaluatePredicateAt(ICmpInst::Predicate::ICMP_SLE, Diff, Max, I)
             .value_or(false);
}

/// The function analyzes all local uses of Ptr (alloca or argument) and
/// calculates local access range and all function calls where it was used.
void StackSafetyLocalAnalysis::analyzeAllUses(Value *Ptr,
                                              UseInfo<GlobalValue> &US,
                                              const StackLifetime &SL) {
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 8> WorkList;
  WorkList.push_back(Ptr);
  AllocaInst *AI = dyn_cast<AllocaInst>(Ptr);

  // A DFS search through all uses of the alloca in bitcasts/PHI/GEPs/etc.
  while (!WorkList.empty()) {
    const Value *V = WorkList.pop_back_val();
    for (const Use &UI : V->uses()) {
      const auto *I = cast<Instruction>(UI.getUser());
      if (!SL.isReachable(I))
        continue;

      assert(V == UI.get());

      auto RecordStore = [&](const Value* StoredVal) {
        if (V == StoredVal) {
          // Stored the pointer - conservatively assume it may be unsafe.
          US.addRange(I, UnknownRange, /*IsSafe=*/false);
          return;
        }
        if (AI && !SL.isAliveAfter(AI, I)) {
          US.addRange(I, UnknownRange, /*IsSafe=*/false);
          return;
        }
        auto TypeSize = DL.getTypeStoreSize(StoredVal->getType());
        auto AccessRange = getAccessRange(UI, Ptr, TypeSize);
        bool Safe = isSafeAccess(UI, AI, TypeSize);
        US.addRange(I, AccessRange, Safe);
        return;
      };

      switch (I->getOpcode()) {
      case Instruction::Load: {
        if (AI && !SL.isAliveAfter(AI, I)) {
          US.addRange(I, UnknownRange, /*IsSafe=*/false);
          break;
        }
        auto TypeSize = DL.getTypeStoreSize(I->getType());
        auto AccessRange = getAccessRange(UI, Ptr, TypeSize);
        bool Safe = isSafeAccess(UI, AI, TypeSize);
        US.addRange(I, AccessRange, Safe);
        break;
      }

      case Instruction::VAArg:
        // "va-arg" from a pointer is safe.
        break;
      case Instruction::Store:
        RecordStore(cast<StoreInst>(I)->getValueOperand());
        break;
      case Instruction::AtomicCmpXchg:
        RecordStore(cast<AtomicCmpXchgInst>(I)->getNewValOperand());
        break;
      case Instruction::AtomicRMW:
        RecordStore(cast<AtomicRMWInst>(I)->getValOperand());
        break;

      case Instruction::Ret:
        // Information leak.
        // FIXME: Process parameters correctly. This is a leak only if we return
        // alloca.
        US.addRange(I, UnknownRange, /*IsSafe=*/false);
        break;

      case Instruction::Call:
      case Instruction::Invoke: {
        if (I->isLifetimeStartOrEnd())
          break;

        if (AI && !SL.isAliveAfter(AI, I)) {
          US.addRange(I, UnknownRange, /*IsSafe=*/false);
          break;
        }
        if (const MemIntrinsic *MI = dyn_cast<MemIntrinsic>(I)) {
          auto AccessRange = getMemIntrinsicAccessRange(MI, UI, Ptr);
          bool Safe = false;
          if (const auto *MTI = dyn_cast<MemTransferInst>(MI)) {
            if (MTI->getRawSource() != UI && MTI->getRawDest() != UI)
              Safe = true;
          } else if (MI->getRawDest() != UI) {
            Safe = true;
          }
          Safe = Safe || isSafeAccess(UI, AI, MI->getLength());
          US.addRange(I, AccessRange, Safe);
          break;
        }

        const auto &CB = cast<CallBase>(*I);
        if (CB.getReturnedArgOperand() == V) {
          if (Visited.insert(I).second)
            WorkList.push_back(cast<const Instruction>(I));
        }

        if (!CB.isArgOperand(&UI)) {
          US.addRange(I, UnknownRange, /*IsSafe=*/false);
          break;
        }

        unsigned ArgNo = CB.getArgOperandNo(&UI);
        if (CB.isByValArgument(ArgNo)) {
          auto TypeSize = DL.getTypeStoreSize(CB.getParamByValType(ArgNo));
          auto AccessRange = getAccessRange(UI, Ptr, TypeSize);
          bool Safe = isSafeAccess(UI, AI, TypeSize);
          US.addRange(I, AccessRange, Safe);
          break;
        }

        // FIXME: consult devirt?
        // Do not follow aliases, otherwise we could inadvertently follow
        // dso_preemptable aliases or aliases with interposable linkage.
        const GlobalValue *Callee =
            dyn_cast<GlobalValue>(CB.getCalledOperand()->stripPointerCasts());
        if (!Callee) {
          US.addRange(I, UnknownRange, /*IsSafe=*/false);
          break;
        }

        assert(isa<Function>(Callee) || isa<GlobalAlias>(Callee));
        ConstantRange Offsets = offsetFrom(UI, Ptr);
        auto Insert =
            US.Calls.emplace(CallInfo<GlobalValue>(Callee, ArgNo), Offsets);
        if (!Insert.second)
          Insert.first->second = Insert.first->second.unionWith(Offsets);
        break;
      }

      default:
        if (Visited.insert(I).second)
          WorkList.push_back(cast<const Instruction>(I));
      }
    }
  }
}

FunctionInfo<GlobalValue> StackSafetyLocalAnalysis::run() {
  FunctionInfo<GlobalValue> Info;
  assert(!F.isDeclaration() &&
         "Can't run StackSafety on a function declaration");

  LLVM_DEBUG(dbgs() << "[StackSafety] " << F.getName() << "\n");

  SmallVector<AllocaInst *, 64> Allocas;
  for (auto &I : instructions(F))
    if (auto *AI = dyn_cast<AllocaInst>(&I))
      Allocas.push_back(AI);
  StackLifetime SL(F, Allocas, StackLifetime::LivenessType::Must);
  SL.run();

  for (auto *AI : Allocas) {
    auto &UI = Info.Allocas.emplace(AI, PointerSize).first->second;
    analyzeAllUses(AI, UI, SL);
  }

  for (Argument &A : F.args()) {
    // Non pointers and bypass arguments are not going to be used in any global
    // processing.
    if (A.getType()->isPointerTy() && !A.hasByValAttr()) {
      auto &UI = Info.Params.emplace(A.getArgNo(), PointerSize).first->second;
      analyzeAllUses(&A, UI, SL);
    }
  }

  LLVM_DEBUG(Info.print(dbgs(), F.getName(), &F));
  LLVM_DEBUG(dbgs() << "\n[StackSafety] done\n");
  return Info;
}

template <typename CalleeTy> class StackSafetyDataFlowAnalysis {
  using FunctionMap = std::map<const CalleeTy *, FunctionInfo<CalleeTy>>;

  FunctionMap Functions;
  const ConstantRange UnknownRange;

  // Callee-to-Caller multimap.
  DenseMap<const CalleeTy *, SmallVector<const CalleeTy *, 4>> Callers;
  SetVector<const CalleeTy *> WorkList;

  bool updateOneUse(UseInfo<CalleeTy> &US, bool UpdateToFullSet);
  void updateOneNode(const CalleeTy *Callee, FunctionInfo<CalleeTy> &FS);
  void updateOneNode(const CalleeTy *Callee) {
    updateOneNode(Callee, Functions.find(Callee)->second);
  }
  void updateAllNodes() {
    for (auto &F : Functions)
      updateOneNode(F.first, F.second);
  }
  void runDataFlow();
#ifndef NDEBUG
  void verifyFixedPoint();
#endif

public:
  StackSafetyDataFlowAnalysis(uint32_t PointerBitWidth, FunctionMap Functions)
      : Functions(std::move(Functions)),
        UnknownRange(ConstantRange::getFull(PointerBitWidth)) {}

  const FunctionMap &run();

  ConstantRange getArgumentAccessRange(const CalleeTy *Callee, unsigned ParamNo,
                                       const ConstantRange &Offsets) const;
};

template <typename CalleeTy>
ConstantRange StackSafetyDataFlowAnalysis<CalleeTy>::getArgumentAccessRange(
    const CalleeTy *Callee, unsigned ParamNo,
    const ConstantRange &Offsets) const {
  auto FnIt = Functions.find(Callee);
  // Unknown callee (outside of LTO domain or an indirect call).
  if (FnIt == Functions.end())
    return UnknownRange;
  auto &FS = FnIt->second;
  auto ParamIt = FS.Params.find(ParamNo);
  if (ParamIt == FS.Params.end())
    return UnknownRange;
  auto &Access = ParamIt->second.Range;
  if (Access.isEmptySet())
    return Access;
  if (Access.isFullSet())
    return UnknownRange;
  return addOverflowNever(Access, Offsets);
}

template <typename CalleeTy>
bool StackSafetyDataFlowAnalysis<CalleeTy>::updateOneUse(UseInfo<CalleeTy> &US,
                                                         bool UpdateToFullSet) {
  bool Changed = false;
  for (auto &KV : US.Calls) {
    assert(!KV.second.isEmptySet() &&
           "Param range can't be empty-set, invalid offset range");

    ConstantRange CalleeRange =
        getArgumentAccessRange(KV.first.Callee, KV.first.ParamNo, KV.second);
    if (!US.Range.contains(CalleeRange)) {
      Changed = true;
      if (UpdateToFullSet)
        US.Range = UnknownRange;
      else
        US.updateRange(CalleeRange);
    }
  }
  return Changed;
}

template <typename CalleeTy>
void StackSafetyDataFlowAnalysis<CalleeTy>::updateOneNode(
    const CalleeTy *Callee, FunctionInfo<CalleeTy> &FS) {
  bool UpdateToFullSet = FS.UpdateCount > StackSafetyMaxIterations;
  bool Changed = false;
  for (auto &KV : FS.Params)
    Changed |= updateOneUse(KV.second, UpdateToFullSet);

  if (Changed) {
    LLVM_DEBUG(dbgs() << "=== update [" << FS.UpdateCount
                      << (UpdateToFullSet ? ", full-set" : "") << "] " << &FS
                      << "\n");
    // Callers of this function may need updating.
    for (auto &CallerID : Callers[Callee])
      WorkList.insert(CallerID);

    ++FS.UpdateCount;
  }
}

template <typename CalleeTy>
void StackSafetyDataFlowAnalysis<CalleeTy>::runDataFlow() {
  SmallVector<const CalleeTy *, 16> Callees;
  for (auto &F : Functions) {
    Callees.clear();
    auto &FS = F.second;
    for (auto &KV : FS.Params)
      for (auto &CS : KV.second.Calls)
        Callees.push_back(CS.first.Callee);

    llvm::sort(Callees);
    Callees.erase(llvm::unique(Callees), Callees.end());

    for (auto &Callee : Callees)
      Callers[Callee].push_back(F.first);
  }

  updateAllNodes();

  while (!WorkList.empty()) {
    const CalleeTy *Callee = WorkList.pop_back_val();
    updateOneNode(Callee);
  }
}

#ifndef NDEBUG
template <typename CalleeTy>
void StackSafetyDataFlowAnalysis<CalleeTy>::verifyFixedPoint() {
  WorkList.clear();
  updateAllNodes();
  assert(WorkList.empty());
}
#endif

template <typename CalleeTy>
const typename StackSafetyDataFlowAnalysis<CalleeTy>::FunctionMap &
StackSafetyDataFlowAnalysis<CalleeTy>::run() {
  runDataFlow();
  LLVM_DEBUG(verifyFixedPoint());
  return Functions;
}

FunctionSummary *findCalleeFunctionSummary(ValueInfo VI, StringRef ModuleId) {
  if (!VI)
    return nullptr;
  auto SummaryList = VI.getSummaryList();
  GlobalValueSummary* S = nullptr;
  for (const auto& GVS : SummaryList) {
    if (!GVS->isLive())
      continue;
    if (const AliasSummary *AS = dyn_cast<AliasSummary>(GVS.get()))
      if (!AS->hasAliasee())
        continue;
    if (!isa<FunctionSummary>(GVS->getBaseObject()))
      continue;
    if (GlobalValue::isLocalLinkage(GVS->linkage())) {
      if (GVS->modulePath() == ModuleId) {
        S = GVS.get();
        break;
      }
    } else if (GlobalValue::isExternalLinkage(GVS->linkage())) {
      if (S) {
        ++NumIndexCalleeMultipleExternal;
        return nullptr;
      }
      S = GVS.get();
    } else if (GlobalValue::isWeakLinkage(GVS->linkage())) {
      if (S) {
        ++NumIndexCalleeMultipleWeak;
        return nullptr;
      }
      S = GVS.get();
    } else if (GlobalValue::isAvailableExternallyLinkage(GVS->linkage()) ||
               GlobalValue::isLinkOnceLinkage(GVS->linkage())) {
      if (SummaryList.size() == 1)
        S = GVS.get();
      // According thinLTOResolvePrevailingGUID these are unlikely prevailing.
    } else {
      ++NumIndexCalleeUnhandled;
    }
  };
  while (S) {
    if (!S->isLive() || !S->isDSOLocal())
      return nullptr;
    if (FunctionSummary *FS = dyn_cast<FunctionSummary>(S))
      return FS;
    AliasSummary *AS = dyn_cast<AliasSummary>(S);
    if (!AS || !AS->hasAliasee())
      return nullptr;
    S = AS->getBaseObject();
    if (S == AS)
      return nullptr;
  }
  return nullptr;
}

const Function *findCalleeInModule(const GlobalValue *GV) {
  while (GV) {
    if (GV->isDeclaration() || GV->isInterposable() || !GV->isDSOLocal())
      return nullptr;
    if (const Function *F = dyn_cast<Function>(GV))
      return F;
    const GlobalAlias *A = dyn_cast<GlobalAlias>(GV);
    if (!A)
      return nullptr;
    GV = A->getAliaseeObject();
    if (GV == A)
      return nullptr;
  }
  return nullptr;
}

const ConstantRange *findParamAccess(const FunctionSummary &FS,
                                     uint32_t ParamNo) {
  assert(FS.isLive());
  assert(FS.isDSOLocal());
  for (const auto &PS : FS.paramAccesses())
    if (ParamNo == PS.ParamNo)
      return &PS.Use;
  return nullptr;
}

void resolveAllCalls(UseInfo<GlobalValue> &Use,
                     const ModuleSummaryIndex *Index) {
  ConstantRange FullSet(Use.Range.getBitWidth(), true);
  // Move Use.Calls to a temp storage and repopulate - don't use std::move as it
  // leaves Use.Calls in an undefined state.
  UseInfo<GlobalValue>::CallsTy TmpCalls;
  std::swap(TmpCalls, Use.Calls);
  for (const auto &C : TmpCalls) {
    const Function *F = findCalleeInModule(C.first.Callee);
    if (F) {
      Use.Calls.emplace(CallInfo<GlobalValue>(F, C.first.ParamNo), C.second);
      continue;
    }

    if (!Index)
      return Use.updateRange(FullSet);
    FunctionSummary *FS =
        findCalleeFunctionSummary(Index->getValueInfo(C.first.Callee->getGUID()),
                                  C.first.Callee->getParent()->getModuleIdentifier());
    ++NumModuleCalleeLookupTotal;
    if (!FS) {
      ++NumModuleCalleeLookupFailed;
      return Use.updateRange(FullSet);
    }
    const ConstantRange *Found = findParamAccess(*FS, C.first.ParamNo);
    if (!Found || Found->isFullSet())
      return Use.updateRange(FullSet);
    ConstantRange Access = Found->sextOrTrunc(Use.Range.getBitWidth());
    if (!Access.isEmptySet())
      Use.updateRange(addOverflowNever(Access, C.second));
  }
}

GVToSSI createGlobalStackSafetyInfo(
    std::map<const GlobalValue *, FunctionInfo<GlobalValue>> Functions,
    const ModuleSummaryIndex *Index) {
  GVToSSI SSI;
  if (Functions.empty())
    return SSI;

  // FIXME: Simplify printing and remove copying here.
  auto Copy = Functions;

  for (auto &FnKV : Copy)
    for (auto &KV : FnKV.second.Params) {
      resolveAllCalls(KV.second, Index);
      if (KV.second.Range.isFullSet())
        KV.second.Calls.clear();
    }

  uint32_t PointerSize =
      Copy.begin()->first->getDataLayout().getPointerSizeInBits();
  StackSafetyDataFlowAnalysis<GlobalValue> SSDFA(PointerSize, std::move(Copy));

  for (const auto &F : SSDFA.run()) {
    auto FI = F.second;
    auto &SrcF = Functions[F.first];
    for (auto &KV : FI.Allocas) {
      auto &A = KV.second;
      resolveAllCalls(A, Index);
      for (auto &C : A.Calls) {
        A.updateRange(SSDFA.getArgumentAccessRange(C.first.Callee,
                                                   C.first.ParamNo, C.second));
      }
      // FIXME: This is needed only to preserve calls in print() results.
      A.Calls = SrcF.Allocas.find(KV.first)->second.Calls;
    }
    for (auto &KV : FI.Params) {
      auto &P = KV.second;
      P.Calls = SrcF.Params.find(KV.first)->second.Calls;
    }
    SSI[F.first] = std::move(FI);
  }

  return SSI;
}

} // end anonymous namespace

StackSafetyInfo::StackSafetyInfo() = default;

StackSafetyInfo::StackSafetyInfo(Function *F,
                                 std::function<ScalarEvolution &()> GetSE)
    : F(F), GetSE(GetSE) {}

StackSafetyInfo::StackSafetyInfo(StackSafetyInfo &&) = default;

StackSafetyInfo &StackSafetyInfo::operator=(StackSafetyInfo &&) = default;

StackSafetyInfo::~StackSafetyInfo() = default;

const StackSafetyInfo::InfoTy &StackSafetyInfo::getInfo() const {
  if (!Info) {
    StackSafetyLocalAnalysis SSLA(*F, GetSE());
    Info.reset(new InfoTy{SSLA.run()});
  }
  return *Info;
}

void StackSafetyInfo::print(raw_ostream &O) const {
  getInfo().Info.print(O, F->getName(), dyn_cast<Function>(F));
  O << "\n";
}

const StackSafetyGlobalInfo::InfoTy &StackSafetyGlobalInfo::getInfo() const {
  if (!Info) {
    std::map<const GlobalValue *, FunctionInfo<GlobalValue>> Functions;
    for (auto &F : M->functions()) {
      if (!F.isDeclaration()) {
        auto FI = GetSSI(F).getInfo().Info;
        Functions.emplace(&F, std::move(FI));
      }
    }
    Info.reset(new InfoTy{
        createGlobalStackSafetyInfo(std::move(Functions), Index), {}, {}});

    for (auto &FnKV : Info->Info) {
      for (auto &KV : FnKV.second.Allocas) {
        ++NumAllocaTotal;
        const AllocaInst *AI = KV.first;
        auto AIRange = getStaticAllocaSizeRange(*AI);
        if (AIRange.contains(KV.second.Range)) {
          Info->SafeAllocas.insert(AI);
          ++NumAllocaStackSafe;
        }
        Info->UnsafeAccesses.insert(KV.second.UnsafeAccesses.begin(),
                                    KV.second.UnsafeAccesses.end());
      }
    }

    if (StackSafetyPrint)
      print(errs());
  }
  return *Info;
}

std::vector<FunctionSummary::ParamAccess>
StackSafetyInfo::getParamAccesses(ModuleSummaryIndex &Index) const {
  // Implementation transforms internal representation of parameter information
  // into FunctionSummary format.
  std::vector<FunctionSummary::ParamAccess> ParamAccesses;
  for (const auto &KV : getInfo().Info.Params) {
    auto &PS = KV.second;
    // Parameter accessed by any or unknown offset, represented as FullSet by
    // StackSafety, is handled as the parameter for which we have no
    // StackSafety info at all. So drop it to reduce summary size.
    if (PS.Range.isFullSet())
      continue;

    ParamAccesses.emplace_back(KV.first, PS.Range);
    FunctionSummary::ParamAccess &Param = ParamAccesses.back();

    Param.Calls.reserve(PS.Calls.size());
    for (const auto &C : PS.Calls) {
      // Parameter forwarded into another function by any or unknown offset
      // will make ParamAccess::Range as FullSet anyway. So we can drop the
      // entire parameter like we did above.
      // TODO(vitalybuka): Return already filtered parameters from getInfo().
      if (C.second.isFullSet()) {
        ParamAccesses.pop_back();
        break;
      }
      Param.Calls.emplace_back(C.first.ParamNo,
                               Index.getOrInsertValueInfo(C.first.Callee),
                               C.second);
    }
  }
  for (FunctionSummary::ParamAccess &Param : ParamAccesses) {
    sort(Param.Calls, [](const FunctionSummary::ParamAccess::Call &L,
                         const FunctionSummary::ParamAccess::Call &R) {
      return std::tie(L.ParamNo, L.Callee) < std::tie(R.ParamNo, R.Callee);
    });
  }
  return ParamAccesses;
}

StackSafetyGlobalInfo::StackSafetyGlobalInfo() = default;

StackSafetyGlobalInfo::StackSafetyGlobalInfo(
    Module *M, std::function<const StackSafetyInfo &(Function &F)> GetSSI,
    const ModuleSummaryIndex *Index)
    : M(M), GetSSI(GetSSI), Index(Index) {
  if (StackSafetyRun)
    getInfo();
}

StackSafetyGlobalInfo::StackSafetyGlobalInfo(StackSafetyGlobalInfo &&) =
    default;

StackSafetyGlobalInfo &
StackSafetyGlobalInfo::operator=(StackSafetyGlobalInfo &&) = default;

StackSafetyGlobalInfo::~StackSafetyGlobalInfo() = default;

bool StackSafetyGlobalInfo::isSafe(const AllocaInst &AI) const {
  const auto &Info = getInfo();
  return Info.SafeAllocas.count(&AI);
}

bool StackSafetyGlobalInfo::stackAccessIsSafe(const Instruction &I) const {
  const auto &Info = getInfo();
  return Info.UnsafeAccesses.find(&I) == Info.UnsafeAccesses.end();
}

void StackSafetyGlobalInfo::print(raw_ostream &O) const {
  auto &SSI = getInfo().Info;
  if (SSI.empty())
    return;
  const Module &M = *SSI.begin()->first->getParent();
  for (const auto &F : M.functions()) {
    if (!F.isDeclaration()) {
      SSI.find(&F)->second.print(O, F.getName(), &F);
      O << "    safe accesses:"
        << "\n";
      for (const auto &I : instructions(F)) {
        const CallInst *Call = dyn_cast<CallInst>(&I);
        if ((isa<StoreInst>(I) || isa<LoadInst>(I) || isa<MemIntrinsic>(I) ||
             isa<AtomicCmpXchgInst>(I) || isa<AtomicRMWInst>(I) ||
             (Call && Call->hasByValArgument())) &&
            stackAccessIsSafe(I)) {
          O << "     " << I << "\n";
        }
      }
      O << "\n";
    }
  }
}

LLVM_DUMP_METHOD void StackSafetyGlobalInfo::dump() const { print(dbgs()); }

AnalysisKey StackSafetyAnalysis::Key;

StackSafetyInfo StackSafetyAnalysis::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  return StackSafetyInfo(&F, [&AM, &F]() -> ScalarEvolution & {
    return AM.getResult<ScalarEvolutionAnalysis>(F);
  });
}

PreservedAnalyses StackSafetyPrinterPass::run(Function &F,
                                              FunctionAnalysisManager &AM) {
  OS << "'Stack Safety Local Analysis' for function '" << F.getName() << "'\n";
  AM.getResult<StackSafetyAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}

char StackSafetyInfoWrapperPass::ID = 0;

StackSafetyInfoWrapperPass::StackSafetyInfoWrapperPass() : FunctionPass(ID) {
  initializeStackSafetyInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

void StackSafetyInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequiredTransitive<ScalarEvolutionWrapperPass>();
  AU.setPreservesAll();
}

void StackSafetyInfoWrapperPass::print(raw_ostream &O, const Module *M) const {
  SSI.print(O);
}

bool StackSafetyInfoWrapperPass::runOnFunction(Function &F) {
  auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  SSI = {&F, [SE]() -> ScalarEvolution & { return *SE; }};
  return false;
}

AnalysisKey StackSafetyGlobalAnalysis::Key;

StackSafetyGlobalInfo
StackSafetyGlobalAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
  // FIXME: Lookup Module Summary.
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  return {&M,
          [&FAM](Function &F) -> const StackSafetyInfo & {
            return FAM.getResult<StackSafetyAnalysis>(F);
          },
          nullptr};
}

PreservedAnalyses StackSafetyGlobalPrinterPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  OS << "'Stack Safety Analysis' for module '" << M.getName() << "'\n";
  AM.getResult<StackSafetyGlobalAnalysis>(M).print(OS);
  return PreservedAnalyses::all();
}

char StackSafetyGlobalInfoWrapperPass::ID = 0;

StackSafetyGlobalInfoWrapperPass::StackSafetyGlobalInfoWrapperPass()
    : ModulePass(ID) {
  initializeStackSafetyGlobalInfoWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

StackSafetyGlobalInfoWrapperPass::~StackSafetyGlobalInfoWrapperPass() = default;

void StackSafetyGlobalInfoWrapperPass::print(raw_ostream &O,
                                             const Module *M) const {
  SSGI.print(O);
}

void StackSafetyGlobalInfoWrapperPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<StackSafetyInfoWrapperPass>();
}

bool StackSafetyGlobalInfoWrapperPass::runOnModule(Module &M) {
  const ModuleSummaryIndex *ImportSummary = nullptr;
  if (auto *IndexWrapperPass =
          getAnalysisIfAvailable<ImmutableModuleSummaryIndexWrapperPass>())
    ImportSummary = IndexWrapperPass->getIndex();

  SSGI = {&M,
          [this](Function &F) -> const StackSafetyInfo & {
            return getAnalysis<StackSafetyInfoWrapperPass>(F).getResult();
          },
          ImportSummary};
  return false;
}

bool llvm::needsParamAccessSummary(const Module &M) {
  if (StackSafetyRun)
    return true;
  for (const auto &F : M.functions())
    if (F.hasFnAttribute(Attribute::SanitizeMemTag))
      return true;
  return false;
}

void llvm::generateParamAccessSummary(ModuleSummaryIndex &Index) {
  if (!Index.hasParamAccess())
    return;
  const ConstantRange FullSet(FunctionSummary::ParamAccess::RangeWidth, true);

  auto CountParamAccesses = [&](auto &Stat) {
    if (!AreStatisticsEnabled())
      return;
    for (auto &GVS : Index)
      for (auto &GV : GVS.second.SummaryList)
        if (FunctionSummary *FS = dyn_cast<FunctionSummary>(GV.get()))
          Stat += FS->paramAccesses().size();
  };

  CountParamAccesses(NumCombinedParamAccessesBefore);

  std::map<const FunctionSummary *, FunctionInfo<FunctionSummary>> Functions;

  // Convert the ModuleSummaryIndex to a FunctionMap
  for (auto &GVS : Index) {
    for (auto &GV : GVS.second.SummaryList) {
      FunctionSummary *FS = dyn_cast<FunctionSummary>(GV.get());
      if (!FS || FS->paramAccesses().empty())
        continue;
      if (FS->isLive() && FS->isDSOLocal()) {
        FunctionInfo<FunctionSummary> FI;
        for (const auto &PS : FS->paramAccesses()) {
          auto &US =
              FI.Params
                  .emplace(PS.ParamNo, FunctionSummary::ParamAccess::RangeWidth)
                  .first->second;
          US.Range = PS.Use;
          for (const auto &Call : PS.Calls) {
            assert(!Call.Offsets.isFullSet());
            FunctionSummary *S =
                findCalleeFunctionSummary(Call.Callee, FS->modulePath());
            ++NumCombinedCalleeLookupTotal;
            if (!S) {
              ++NumCombinedCalleeLookupFailed;
              US.Range = FullSet;
              US.Calls.clear();
              break;
            }
            US.Calls.emplace(CallInfo<FunctionSummary>(S, Call.ParamNo),
                             Call.Offsets);
          }
        }
        Functions.emplace(FS, std::move(FI));
      }
      // Reset data for all summaries. Alive and DSO local will be set back from
      // of data flow results below. Anything else will not be accessed
      // by ThinLTO backend, so we can save on bitcode size.
      FS->setParamAccesses({});
    }
  }
  NumCombinedDataFlowNodes += Functions.size();
  StackSafetyDataFlowAnalysis<FunctionSummary> SSDFA(
      FunctionSummary::ParamAccess::RangeWidth, std::move(Functions));
  for (const auto &KV : SSDFA.run()) {
    std::vector<FunctionSummary::ParamAccess> NewParams;
    NewParams.reserve(KV.second.Params.size());
    for (const auto &Param : KV.second.Params) {
      // It's not needed as FullSet is processed the same as a missing value.
      if (Param.second.Range.isFullSet())
        continue;
      NewParams.emplace_back();
      FunctionSummary::ParamAccess &New = NewParams.back();
      New.ParamNo = Param.first;
      New.Use = Param.second.Range; // Only range is needed.
    }
    const_cast<FunctionSummary *>(KV.first)->setParamAccesses(
        std::move(NewParams));
  }

  CountParamAccesses(NumCombinedParamAccessesAfter);
}

static const char LocalPassArg[] = "stack-safety-local";
static const char LocalPassName[] = "Stack Safety Local Analysis";
INITIALIZE_PASS_BEGIN(StackSafetyInfoWrapperPass, LocalPassArg, LocalPassName,
                      false, true)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(StackSafetyInfoWrapperPass, LocalPassArg, LocalPassName,
                    false, true)

static const char GlobalPassName[] = "Stack Safety Analysis";
INITIALIZE_PASS_BEGIN(StackSafetyGlobalInfoWrapperPass, DEBUG_TYPE,
                      GlobalPassName, false, true)
INITIALIZE_PASS_DEPENDENCY(StackSafetyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ImmutableModuleSummaryIndexWrapperPass)
INITIALIZE_PASS_END(StackSafetyGlobalInfoWrapperPass, DEBUG_TYPE,
                    GlobalPassName, false, true)
