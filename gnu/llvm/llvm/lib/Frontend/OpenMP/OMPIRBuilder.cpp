//===- OpenMPIRBuilder.cpp - Builder for LLVM-IR for OpenMP directives ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the OpenMPIRBuilder class, which is used as a
/// convenient way to create LLVM instructions for OpenMP directives.
///
//===----------------------------------------------------------------------===//

#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Frontend/Offloading/Utility.h"
#include "llvm/Frontend/OpenMP/OMPGridValues.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/ReplaceConstant.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/LoopPeel.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"

#include <cstdint>
#include <optional>
#include <stack>

#define DEBUG_TYPE "openmp-ir-builder"

using namespace llvm;
using namespace omp;

static cl::opt<bool>
    OptimisticAttributes("openmp-ir-builder-optimistic-attributes", cl::Hidden,
                         cl::desc("Use optimistic attributes describing "
                                  "'as-if' properties of runtime calls."),
                         cl::init(false));

static cl::opt<double> UnrollThresholdFactor(
    "openmp-ir-builder-unroll-threshold-factor", cl::Hidden,
    cl::desc("Factor for the unroll threshold to account for code "
             "simplifications still taking place"),
    cl::init(1.5));

#ifndef NDEBUG
/// Return whether IP1 and IP2 are ambiguous, i.e. that inserting instructions
/// at position IP1 may change the meaning of IP2 or vice-versa. This is because
/// an InsertPoint stores the instruction before something is inserted. For
/// instance, if both point to the same instruction, two IRBuilders alternating
/// creating instruction will cause the instructions to be interleaved.
static bool isConflictIP(IRBuilder<>::InsertPoint IP1,
                         IRBuilder<>::InsertPoint IP2) {
  if (!IP1.isSet() || !IP2.isSet())
    return false;
  return IP1.getBlock() == IP2.getBlock() && IP1.getPoint() == IP2.getPoint();
}

static bool isValidWorkshareLoopScheduleType(OMPScheduleType SchedType) {
  // Valid ordered/unordered and base algorithm combinations.
  switch (SchedType & ~OMPScheduleType::MonotonicityMask) {
  case OMPScheduleType::UnorderedStaticChunked:
  case OMPScheduleType::UnorderedStatic:
  case OMPScheduleType::UnorderedDynamicChunked:
  case OMPScheduleType::UnorderedGuidedChunked:
  case OMPScheduleType::UnorderedRuntime:
  case OMPScheduleType::UnorderedAuto:
  case OMPScheduleType::UnorderedTrapezoidal:
  case OMPScheduleType::UnorderedGreedy:
  case OMPScheduleType::UnorderedBalanced:
  case OMPScheduleType::UnorderedGuidedIterativeChunked:
  case OMPScheduleType::UnorderedGuidedAnalyticalChunked:
  case OMPScheduleType::UnorderedSteal:
  case OMPScheduleType::UnorderedStaticBalancedChunked:
  case OMPScheduleType::UnorderedGuidedSimd:
  case OMPScheduleType::UnorderedRuntimeSimd:
  case OMPScheduleType::OrderedStaticChunked:
  case OMPScheduleType::OrderedStatic:
  case OMPScheduleType::OrderedDynamicChunked:
  case OMPScheduleType::OrderedGuidedChunked:
  case OMPScheduleType::OrderedRuntime:
  case OMPScheduleType::OrderedAuto:
  case OMPScheduleType::OrderdTrapezoidal:
  case OMPScheduleType::NomergeUnorderedStaticChunked:
  case OMPScheduleType::NomergeUnorderedStatic:
  case OMPScheduleType::NomergeUnorderedDynamicChunked:
  case OMPScheduleType::NomergeUnorderedGuidedChunked:
  case OMPScheduleType::NomergeUnorderedRuntime:
  case OMPScheduleType::NomergeUnorderedAuto:
  case OMPScheduleType::NomergeUnorderedTrapezoidal:
  case OMPScheduleType::NomergeUnorderedGreedy:
  case OMPScheduleType::NomergeUnorderedBalanced:
  case OMPScheduleType::NomergeUnorderedGuidedIterativeChunked:
  case OMPScheduleType::NomergeUnorderedGuidedAnalyticalChunked:
  case OMPScheduleType::NomergeUnorderedSteal:
  case OMPScheduleType::NomergeOrderedStaticChunked:
  case OMPScheduleType::NomergeOrderedStatic:
  case OMPScheduleType::NomergeOrderedDynamicChunked:
  case OMPScheduleType::NomergeOrderedGuidedChunked:
  case OMPScheduleType::NomergeOrderedRuntime:
  case OMPScheduleType::NomergeOrderedAuto:
  case OMPScheduleType::NomergeOrderedTrapezoidal:
    break;
  default:
    return false;
  }

  // Must not set both monotonicity modifiers at the same time.
  OMPScheduleType MonotonicityFlags =
      SchedType & OMPScheduleType::MonotonicityMask;
  if (MonotonicityFlags == OMPScheduleType::MonotonicityMask)
    return false;

  return true;
}
#endif

static const omp::GV &getGridValue(const Triple &T, Function *Kernel) {
  if (T.isAMDGPU()) {
    StringRef Features =
        Kernel->getFnAttribute("target-features").getValueAsString();
    if (Features.count("+wavefrontsize64"))
      return omp::getAMDGPUGridValues<64>();
    return omp::getAMDGPUGridValues<32>();
  }
  if (T.isNVPTX())
    return omp::NVPTXGridValues;
  llvm_unreachable("No grid value available for this architecture!");
}

/// Determine which scheduling algorithm to use, determined from schedule clause
/// arguments.
static OMPScheduleType
getOpenMPBaseScheduleType(llvm::omp::ScheduleKind ClauseKind, bool HasChunks,
                          bool HasSimdModifier) {
  // Currently, the default schedule it static.
  switch (ClauseKind) {
  case OMP_SCHEDULE_Default:
  case OMP_SCHEDULE_Static:
    return HasChunks ? OMPScheduleType::BaseStaticChunked
                     : OMPScheduleType::BaseStatic;
  case OMP_SCHEDULE_Dynamic:
    return OMPScheduleType::BaseDynamicChunked;
  case OMP_SCHEDULE_Guided:
    return HasSimdModifier ? OMPScheduleType::BaseGuidedSimd
                           : OMPScheduleType::BaseGuidedChunked;
  case OMP_SCHEDULE_Auto:
    return llvm::omp::OMPScheduleType::BaseAuto;
  case OMP_SCHEDULE_Runtime:
    return HasSimdModifier ? OMPScheduleType::BaseRuntimeSimd
                           : OMPScheduleType::BaseRuntime;
  }
  llvm_unreachable("unhandled schedule clause argument");
}

/// Adds ordering modifier flags to schedule type.
static OMPScheduleType
getOpenMPOrderingScheduleType(OMPScheduleType BaseScheduleType,
                              bool HasOrderedClause) {
  assert((BaseScheduleType & OMPScheduleType::ModifierMask) ==
             OMPScheduleType::None &&
         "Must not have ordering nor monotonicity flags already set");

  OMPScheduleType OrderingModifier = HasOrderedClause
                                         ? OMPScheduleType::ModifierOrdered
                                         : OMPScheduleType::ModifierUnordered;
  OMPScheduleType OrderingScheduleType = BaseScheduleType | OrderingModifier;

  // Unsupported combinations
  if (OrderingScheduleType ==
      (OMPScheduleType::BaseGuidedSimd | OMPScheduleType::ModifierOrdered))
    return OMPScheduleType::OrderedGuidedChunked;
  else if (OrderingScheduleType == (OMPScheduleType::BaseRuntimeSimd |
                                    OMPScheduleType::ModifierOrdered))
    return OMPScheduleType::OrderedRuntime;

  return OrderingScheduleType;
}

/// Adds monotonicity modifier flags to schedule type.
static OMPScheduleType
getOpenMPMonotonicityScheduleType(OMPScheduleType ScheduleType,
                                  bool HasSimdModifier, bool HasMonotonic,
                                  bool HasNonmonotonic, bool HasOrderedClause) {
  assert((ScheduleType & OMPScheduleType::MonotonicityMask) ==
             OMPScheduleType::None &&
         "Must not have monotonicity flags already set");
  assert((!HasMonotonic || !HasNonmonotonic) &&
         "Monotonic and Nonmonotonic are contradicting each other");

  if (HasMonotonic) {
    return ScheduleType | OMPScheduleType::ModifierMonotonic;
  } else if (HasNonmonotonic) {
    return ScheduleType | OMPScheduleType::ModifierNonmonotonic;
  } else {
    // OpenMP 5.1, 2.11.4 Worksharing-Loop Construct, Description.
    // If the static schedule kind is specified or if the ordered clause is
    // specified, and if the nonmonotonic modifier is not specified, the
    // effect is as if the monotonic modifier is specified. Otherwise, unless
    // the monotonic modifier is specified, the effect is as if the
    // nonmonotonic modifier is specified.
    OMPScheduleType BaseScheduleType =
        ScheduleType & ~OMPScheduleType::ModifierMask;
    if ((BaseScheduleType == OMPScheduleType::BaseStatic) ||
        (BaseScheduleType == OMPScheduleType::BaseStaticChunked) ||
        HasOrderedClause) {
      // The monotonic is used by default in openmp runtime library, so no need
      // to set it.
      return ScheduleType;
    } else {
      return ScheduleType | OMPScheduleType::ModifierNonmonotonic;
    }
  }
}

/// Determine the schedule type using schedule and ordering clause arguments.
static OMPScheduleType
computeOpenMPScheduleType(ScheduleKind ClauseKind, bool HasChunks,
                          bool HasSimdModifier, bool HasMonotonicModifier,
                          bool HasNonmonotonicModifier, bool HasOrderedClause) {
  OMPScheduleType BaseSchedule =
      getOpenMPBaseScheduleType(ClauseKind, HasChunks, HasSimdModifier);
  OMPScheduleType OrderedSchedule =
      getOpenMPOrderingScheduleType(BaseSchedule, HasOrderedClause);
  OMPScheduleType Result = getOpenMPMonotonicityScheduleType(
      OrderedSchedule, HasSimdModifier, HasMonotonicModifier,
      HasNonmonotonicModifier, HasOrderedClause);

  assert(isValidWorkshareLoopScheduleType(Result));
  return Result;
}

/// Make \p Source branch to \p Target.
///
/// Handles two situations:
/// * \p Source already has an unconditional branch.
/// * \p Source is a degenerate block (no terminator because the BB is
///             the current head of the IR construction).
static void redirectTo(BasicBlock *Source, BasicBlock *Target, DebugLoc DL) {
  if (Instruction *Term = Source->getTerminator()) {
    auto *Br = cast<BranchInst>(Term);
    assert(!Br->isConditional() &&
           "BB's terminator must be an unconditional branch (or degenerate)");
    BasicBlock *Succ = Br->getSuccessor(0);
    Succ->removePredecessor(Source, /*KeepOneInputPHIs=*/true);
    Br->setSuccessor(0, Target);
    return;
  }

  auto *NewBr = BranchInst::Create(Target, Source);
  NewBr->setDebugLoc(DL);
}

void llvm::spliceBB(IRBuilderBase::InsertPoint IP, BasicBlock *New,
                    bool CreateBranch) {
  assert(New->getFirstInsertionPt() == New->begin() &&
         "Target BB must not have PHI nodes");

  // Move instructions to new block.
  BasicBlock *Old = IP.getBlock();
  New->splice(New->begin(), Old, IP.getPoint(), Old->end());

  if (CreateBranch)
    BranchInst::Create(New, Old);
}

void llvm::spliceBB(IRBuilder<> &Builder, BasicBlock *New, bool CreateBranch) {
  DebugLoc DebugLoc = Builder.getCurrentDebugLocation();
  BasicBlock *Old = Builder.GetInsertBlock();

  spliceBB(Builder.saveIP(), New, CreateBranch);
  if (CreateBranch)
    Builder.SetInsertPoint(Old->getTerminator());
  else
    Builder.SetInsertPoint(Old);

  // SetInsertPoint also updates the Builder's debug location, but we want to
  // keep the one the Builder was configured to use.
  Builder.SetCurrentDebugLocation(DebugLoc);
}

BasicBlock *llvm::splitBB(IRBuilderBase::InsertPoint IP, bool CreateBranch,
                          llvm::Twine Name) {
  BasicBlock *Old = IP.getBlock();
  BasicBlock *New = BasicBlock::Create(
      Old->getContext(), Name.isTriviallyEmpty() ? Old->getName() : Name,
      Old->getParent(), Old->getNextNode());
  spliceBB(IP, New, CreateBranch);
  New->replaceSuccessorsPhiUsesWith(Old, New);
  return New;
}

BasicBlock *llvm::splitBB(IRBuilderBase &Builder, bool CreateBranch,
                          llvm::Twine Name) {
  DebugLoc DebugLoc = Builder.getCurrentDebugLocation();
  BasicBlock *New = splitBB(Builder.saveIP(), CreateBranch, Name);
  if (CreateBranch)
    Builder.SetInsertPoint(Builder.GetInsertBlock()->getTerminator());
  else
    Builder.SetInsertPoint(Builder.GetInsertBlock());
  // SetInsertPoint also updates the Builder's debug location, but we want to
  // keep the one the Builder was configured to use.
  Builder.SetCurrentDebugLocation(DebugLoc);
  return New;
}

BasicBlock *llvm::splitBB(IRBuilder<> &Builder, bool CreateBranch,
                          llvm::Twine Name) {
  DebugLoc DebugLoc = Builder.getCurrentDebugLocation();
  BasicBlock *New = splitBB(Builder.saveIP(), CreateBranch, Name);
  if (CreateBranch)
    Builder.SetInsertPoint(Builder.GetInsertBlock()->getTerminator());
  else
    Builder.SetInsertPoint(Builder.GetInsertBlock());
  // SetInsertPoint also updates the Builder's debug location, but we want to
  // keep the one the Builder was configured to use.
  Builder.SetCurrentDebugLocation(DebugLoc);
  return New;
}

BasicBlock *llvm::splitBBWithSuffix(IRBuilderBase &Builder, bool CreateBranch,
                                    llvm::Twine Suffix) {
  BasicBlock *Old = Builder.GetInsertBlock();
  return splitBB(Builder, CreateBranch, Old->getName() + Suffix);
}

// This function creates a fake integer value and a fake use for the integer
// value. It returns the fake value created. This is useful in modeling the
// extra arguments to the outlined functions.
Value *createFakeIntVal(IRBuilderBase &Builder,
                        OpenMPIRBuilder::InsertPointTy OuterAllocaIP,
                        llvm::SmallVectorImpl<Instruction *> &ToBeDeleted,
                        OpenMPIRBuilder::InsertPointTy InnerAllocaIP,
                        const Twine &Name = "", bool AsPtr = true) {
  Builder.restoreIP(OuterAllocaIP);
  Instruction *FakeVal;
  AllocaInst *FakeValAddr =
      Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, Name + ".addr");
  ToBeDeleted.push_back(FakeValAddr);

  if (AsPtr) {
    FakeVal = FakeValAddr;
  } else {
    FakeVal =
        Builder.CreateLoad(Builder.getInt32Ty(), FakeValAddr, Name + ".val");
    ToBeDeleted.push_back(FakeVal);
  }

  // Generate a fake use of this value
  Builder.restoreIP(InnerAllocaIP);
  Instruction *UseFakeVal;
  if (AsPtr) {
    UseFakeVal =
        Builder.CreateLoad(Builder.getInt32Ty(), FakeVal, Name + ".use");
  } else {
    UseFakeVal =
        cast<BinaryOperator>(Builder.CreateAdd(FakeVal, Builder.getInt32(10)));
  }
  ToBeDeleted.push_back(UseFakeVal);
  return FakeVal;
}

//===----------------------------------------------------------------------===//
// OpenMPIRBuilderConfig
//===----------------------------------------------------------------------===//

namespace {
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();
/// Values for bit flags for marking which requires clauses have been used.
enum OpenMPOffloadingRequiresDirFlags {
  /// flag undefined.
  OMP_REQ_UNDEFINED = 0x000,
  /// no requires directive present.
  OMP_REQ_NONE = 0x001,
  /// reverse_offload clause.
  OMP_REQ_REVERSE_OFFLOAD = 0x002,
  /// unified_address clause.
  OMP_REQ_UNIFIED_ADDRESS = 0x004,
  /// unified_shared_memory clause.
  OMP_REQ_UNIFIED_SHARED_MEMORY = 0x008,
  /// dynamic_allocators clause.
  OMP_REQ_DYNAMIC_ALLOCATORS = 0x010,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/OMP_REQ_DYNAMIC_ALLOCATORS)
};

} // anonymous namespace

OpenMPIRBuilderConfig::OpenMPIRBuilderConfig()
    : RequiresFlags(OMP_REQ_UNDEFINED) {}

OpenMPIRBuilderConfig::OpenMPIRBuilderConfig(
    bool IsTargetDevice, bool IsGPU, bool OpenMPOffloadMandatory,
    bool HasRequiresReverseOffload, bool HasRequiresUnifiedAddress,
    bool HasRequiresUnifiedSharedMemory, bool HasRequiresDynamicAllocators)
    : IsTargetDevice(IsTargetDevice), IsGPU(IsGPU),
      OpenMPOffloadMandatory(OpenMPOffloadMandatory),
      RequiresFlags(OMP_REQ_UNDEFINED) {
  if (HasRequiresReverseOffload)
    RequiresFlags |= OMP_REQ_REVERSE_OFFLOAD;
  if (HasRequiresUnifiedAddress)
    RequiresFlags |= OMP_REQ_UNIFIED_ADDRESS;
  if (HasRequiresUnifiedSharedMemory)
    RequiresFlags |= OMP_REQ_UNIFIED_SHARED_MEMORY;
  if (HasRequiresDynamicAllocators)
    RequiresFlags |= OMP_REQ_DYNAMIC_ALLOCATORS;
}

bool OpenMPIRBuilderConfig::hasRequiresReverseOffload() const {
  return RequiresFlags & OMP_REQ_REVERSE_OFFLOAD;
}

bool OpenMPIRBuilderConfig::hasRequiresUnifiedAddress() const {
  return RequiresFlags & OMP_REQ_UNIFIED_ADDRESS;
}

bool OpenMPIRBuilderConfig::hasRequiresUnifiedSharedMemory() const {
  return RequiresFlags & OMP_REQ_UNIFIED_SHARED_MEMORY;
}

bool OpenMPIRBuilderConfig::hasRequiresDynamicAllocators() const {
  return RequiresFlags & OMP_REQ_DYNAMIC_ALLOCATORS;
}

int64_t OpenMPIRBuilderConfig::getRequiresFlags() const {
  return hasRequiresFlags() ? RequiresFlags
                            : static_cast<int64_t>(OMP_REQ_NONE);
}

void OpenMPIRBuilderConfig::setHasRequiresReverseOffload(bool Value) {
  if (Value)
    RequiresFlags |= OMP_REQ_REVERSE_OFFLOAD;
  else
    RequiresFlags &= ~OMP_REQ_REVERSE_OFFLOAD;
}

void OpenMPIRBuilderConfig::setHasRequiresUnifiedAddress(bool Value) {
  if (Value)
    RequiresFlags |= OMP_REQ_UNIFIED_ADDRESS;
  else
    RequiresFlags &= ~OMP_REQ_UNIFIED_ADDRESS;
}

void OpenMPIRBuilderConfig::setHasRequiresUnifiedSharedMemory(bool Value) {
  if (Value)
    RequiresFlags |= OMP_REQ_UNIFIED_SHARED_MEMORY;
  else
    RequiresFlags &= ~OMP_REQ_UNIFIED_SHARED_MEMORY;
}

void OpenMPIRBuilderConfig::setHasRequiresDynamicAllocators(bool Value) {
  if (Value)
    RequiresFlags |= OMP_REQ_DYNAMIC_ALLOCATORS;
  else
    RequiresFlags &= ~OMP_REQ_DYNAMIC_ALLOCATORS;
}

//===----------------------------------------------------------------------===//
// OpenMPIRBuilder
//===----------------------------------------------------------------------===//

void OpenMPIRBuilder::getKernelArgsVector(TargetKernelArgs &KernelArgs,
                                          IRBuilderBase &Builder,
                                          SmallVector<Value *> &ArgsVector) {
  Value *Version = Builder.getInt32(OMP_KERNEL_ARG_VERSION);
  Value *PointerNum = Builder.getInt32(KernelArgs.NumTargetItems);
  auto Int32Ty = Type::getInt32Ty(Builder.getContext());
  Value *ZeroArray = Constant::getNullValue(ArrayType::get(Int32Ty, 3));
  Value *Flags = Builder.getInt64(KernelArgs.HasNoWait);

  Value *NumTeams3D =
      Builder.CreateInsertValue(ZeroArray, KernelArgs.NumTeams, {0});
  Value *NumThreads3D =
      Builder.CreateInsertValue(ZeroArray, KernelArgs.NumThreads, {0});

  ArgsVector = {Version,
                PointerNum,
                KernelArgs.RTArgs.BasePointersArray,
                KernelArgs.RTArgs.PointersArray,
                KernelArgs.RTArgs.SizesArray,
                KernelArgs.RTArgs.MapTypesArray,
                KernelArgs.RTArgs.MapNamesArray,
                KernelArgs.RTArgs.MappersArray,
                KernelArgs.NumIterations,
                Flags,
                NumTeams3D,
                NumThreads3D,
                KernelArgs.DynCGGroupMem};
}

void OpenMPIRBuilder::addAttributes(omp::RuntimeFunction FnID, Function &Fn) {
  LLVMContext &Ctx = Fn.getContext();

  // Get the function's current attributes.
  auto Attrs = Fn.getAttributes();
  auto FnAttrs = Attrs.getFnAttrs();
  auto RetAttrs = Attrs.getRetAttrs();
  SmallVector<AttributeSet, 4> ArgAttrs;
  for (size_t ArgNo = 0; ArgNo < Fn.arg_size(); ++ArgNo)
    ArgAttrs.emplace_back(Attrs.getParamAttrs(ArgNo));

  // Add AS to FnAS while taking special care with integer extensions.
  auto addAttrSet = [&](AttributeSet &FnAS, const AttributeSet &AS,
                        bool Param = true) -> void {
    bool HasSignExt = AS.hasAttribute(Attribute::SExt);
    bool HasZeroExt = AS.hasAttribute(Attribute::ZExt);
    if (HasSignExt || HasZeroExt) {
      assert(AS.getNumAttributes() == 1 &&
             "Currently not handling extension attr combined with others.");
      if (Param) {
        if (auto AK = TargetLibraryInfo::getExtAttrForI32Param(T, HasSignExt))
          FnAS = FnAS.addAttribute(Ctx, AK);
      } else if (auto AK =
                     TargetLibraryInfo::getExtAttrForI32Return(T, HasSignExt))
        FnAS = FnAS.addAttribute(Ctx, AK);
    } else {
      FnAS = FnAS.addAttributes(Ctx, AS);
    }
  };

#define OMP_ATTRS_SET(VarName, AttrSet) AttributeSet VarName = AttrSet;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

  // Add attributes to the function declaration.
  switch (FnID) {
#define OMP_RTL_ATTRS(Enum, FnAttrSet, RetAttrSet, ArgAttrSets)                \
  case Enum:                                                                   \
    FnAttrs = FnAttrs.addAttributes(Ctx, FnAttrSet);                           \
    addAttrSet(RetAttrs, RetAttrSet, /*Param*/ false);                         \
    for (size_t ArgNo = 0; ArgNo < ArgAttrSets.size(); ++ArgNo)                \
      addAttrSet(ArgAttrs[ArgNo], ArgAttrSets[ArgNo]);                         \
    Fn.setAttributes(AttributeList::get(Ctx, FnAttrs, RetAttrs, ArgAttrs));    \
    break;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  default:
    // Attributes are optional.
    break;
  }
}

FunctionCallee
OpenMPIRBuilder::getOrCreateRuntimeFunction(Module &M, RuntimeFunction FnID) {
  FunctionType *FnTy = nullptr;
  Function *Fn = nullptr;

  // Try to find the declation in the module first.
  switch (FnID) {
#define OMP_RTL(Enum, Str, IsVarArg, ReturnType, ...)                          \
  case Enum:                                                                   \
    FnTy = FunctionType::get(ReturnType, ArrayRef<Type *>{__VA_ARGS__},        \
                             IsVarArg);                                        \
    Fn = M.getFunction(Str);                                                   \
    break;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }

  if (!Fn) {
    // Create a new declaration if we need one.
    switch (FnID) {
#define OMP_RTL(Enum, Str, ...)                                                \
  case Enum:                                                                   \
    Fn = Function::Create(FnTy, GlobalValue::ExternalLinkage, Str, M);         \
    break;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
    }

    // Add information if the runtime function takes a callback function
    if (FnID == OMPRTL___kmpc_fork_call || FnID == OMPRTL___kmpc_fork_teams) {
      if (!Fn->hasMetadata(LLVMContext::MD_callback)) {
        LLVMContext &Ctx = Fn->getContext();
        MDBuilder MDB(Ctx);
        // Annotate the callback behavior of the runtime function:
        //  - The callback callee is argument number 2 (microtask).
        //  - The first two arguments of the callback callee are unknown (-1).
        //  - All variadic arguments to the runtime function are passed to the
        //    callback callee.
        Fn->addMetadata(
            LLVMContext::MD_callback,
            *MDNode::get(Ctx, {MDB.createCallbackEncoding(
                                  2, {-1, -1}, /* VarArgsArePassed */ true)}));
      }
    }

    LLVM_DEBUG(dbgs() << "Created OpenMP runtime function " << Fn->getName()
                      << " with type " << *Fn->getFunctionType() << "\n");
    addAttributes(FnID, *Fn);

  } else {
    LLVM_DEBUG(dbgs() << "Found OpenMP runtime function " << Fn->getName()
                      << " with type " << *Fn->getFunctionType() << "\n");
  }

  assert(Fn && "Failed to create OpenMP runtime function");

  return {FnTy, Fn};
}

Function *OpenMPIRBuilder::getOrCreateRuntimeFunctionPtr(RuntimeFunction FnID) {
  FunctionCallee RTLFn = getOrCreateRuntimeFunction(M, FnID);
  auto *Fn = dyn_cast<llvm::Function>(RTLFn.getCallee());
  assert(Fn && "Failed to create OpenMP runtime function pointer");
  return Fn;
}

void OpenMPIRBuilder::initialize() { initializeTypes(M); }

static void raiseUserConstantDataAllocasToEntryBlock(IRBuilderBase &Builder,
                                                     Function *Function) {
  BasicBlock &EntryBlock = Function->getEntryBlock();
  Instruction *MoveLocInst = EntryBlock.getFirstNonPHI();

  // Loop over blocks looking for constant allocas, skipping the entry block
  // as any allocas there are already in the desired location.
  for (auto Block = std::next(Function->begin(), 1); Block != Function->end();
       Block++) {
    for (auto Inst = Block->getReverseIterator()->begin();
         Inst != Block->getReverseIterator()->end();) {
      if (auto *AllocaInst = dyn_cast_if_present<llvm::AllocaInst>(Inst)) {
        Inst++;
        if (!isa<ConstantData>(AllocaInst->getArraySize()))
          continue;
        AllocaInst->moveBeforePreserving(MoveLocInst);
      } else {
        Inst++;
      }
    }
  }
}

void OpenMPIRBuilder::finalize(Function *Fn) {
  SmallPtrSet<BasicBlock *, 32> ParallelRegionBlockSet;
  SmallVector<BasicBlock *, 32> Blocks;
  SmallVector<OutlineInfo, 16> DeferredOutlines;
  for (OutlineInfo &OI : OutlineInfos) {
    // Skip functions that have not finalized yet; may happen with nested
    // function generation.
    if (Fn && OI.getFunction() != Fn) {
      DeferredOutlines.push_back(OI);
      continue;
    }

    ParallelRegionBlockSet.clear();
    Blocks.clear();
    OI.collectBlocks(ParallelRegionBlockSet, Blocks);

    Function *OuterFn = OI.getFunction();
    CodeExtractorAnalysisCache CEAC(*OuterFn);
    // If we generate code for the target device, we need to allocate
    // struct for aggregate params in the device default alloca address space.
    // OpenMP runtime requires that the params of the extracted functions are
    // passed as zero address space pointers. This flag ensures that
    // CodeExtractor generates correct code for extracted functions
    // which are used by OpenMP runtime.
    bool ArgsInZeroAddressSpace = Config.isTargetDevice();
    CodeExtractor Extractor(Blocks, /* DominatorTree */ nullptr,
                            /* AggregateArgs */ true,
                            /* BlockFrequencyInfo */ nullptr,
                            /* BranchProbabilityInfo */ nullptr,
                            /* AssumptionCache */ nullptr,
                            /* AllowVarArgs */ true,
                            /* AllowAlloca */ true,
                            /* AllocaBlock*/ OI.OuterAllocaBB,
                            /* Suffix */ ".omp_par", ArgsInZeroAddressSpace);

    LLVM_DEBUG(dbgs() << "Before     outlining: " << *OuterFn << "\n");
    LLVM_DEBUG(dbgs() << "Entry " << OI.EntryBB->getName()
                      << " Exit: " << OI.ExitBB->getName() << "\n");
    assert(Extractor.isEligible() &&
           "Expected OpenMP outlining to be possible!");

    for (auto *V : OI.ExcludeArgsFromAggregate)
      Extractor.excludeArgFromAggregate(V);

    Function *OutlinedFn = Extractor.extractCodeRegion(CEAC);

    // Forward target-cpu, target-features attributes to the outlined function.
    auto TargetCpuAttr = OuterFn->getFnAttribute("target-cpu");
    if (TargetCpuAttr.isStringAttribute())
      OutlinedFn->addFnAttr(TargetCpuAttr);

    auto TargetFeaturesAttr = OuterFn->getFnAttribute("target-features");
    if (TargetFeaturesAttr.isStringAttribute())
      OutlinedFn->addFnAttr(TargetFeaturesAttr);

    LLVM_DEBUG(dbgs() << "After      outlining: " << *OuterFn << "\n");
    LLVM_DEBUG(dbgs() << "   Outlined function: " << *OutlinedFn << "\n");
    assert(OutlinedFn->getReturnType()->isVoidTy() &&
           "OpenMP outlined functions should not return a value!");

    // For compability with the clang CG we move the outlined function after the
    // one with the parallel region.
    OutlinedFn->removeFromParent();
    M.getFunctionList().insertAfter(OuterFn->getIterator(), OutlinedFn);

    // Remove the artificial entry introduced by the extractor right away, we
    // made our own entry block after all.
    {
      BasicBlock &ArtificialEntry = OutlinedFn->getEntryBlock();
      assert(ArtificialEntry.getUniqueSuccessor() == OI.EntryBB);
      assert(OI.EntryBB->getUniquePredecessor() == &ArtificialEntry);
      // Move instructions from the to-be-deleted ArtificialEntry to the entry
      // basic block of the parallel region. CodeExtractor generates
      // instructions to unwrap the aggregate argument and may sink
      // allocas/bitcasts for values that are solely used in the outlined region
      // and do not escape.
      assert(!ArtificialEntry.empty() &&
             "Expected instructions to add in the outlined region entry");
      for (BasicBlock::reverse_iterator It = ArtificialEntry.rbegin(),
                                        End = ArtificialEntry.rend();
           It != End;) {
        Instruction &I = *It;
        It++;

        if (I.isTerminator())
          continue;

        I.moveBeforePreserving(*OI.EntryBB, OI.EntryBB->getFirstInsertionPt());
      }

      OI.EntryBB->moveBefore(&ArtificialEntry);
      ArtificialEntry.eraseFromParent();
    }
    assert(&OutlinedFn->getEntryBlock() == OI.EntryBB);
    assert(OutlinedFn && OutlinedFn->getNumUses() == 1);

    // Run a user callback, e.g. to add attributes.
    if (OI.PostOutlineCB)
      OI.PostOutlineCB(*OutlinedFn);
  }

  // Remove work items that have been completed.
  OutlineInfos = std::move(DeferredOutlines);

  // The createTarget functions embeds user written code into
  // the target region which may inject allocas which need to
  // be moved to the entry block of our target or risk malformed
  // optimisations by later passes, this is only relevant for
  // the device pass which appears to be a little more delicate
  // when it comes to optimisations (however, we do not block on
  // that here, it's up to the inserter to the list to do so).
  // This notbaly has to occur after the OutlinedInfo candidates
  // have been extracted so we have an end product that will not
  // be implicitly adversely affected by any raises unless
  // intentionally appended to the list.
  // NOTE: This only does so for ConstantData, it could be extended
  // to ConstantExpr's with further effort, however, they should
  // largely be folded when they get here. Extending it to runtime
  // defined/read+writeable allocation sizes would be non-trivial
  // (need to factor in movement of any stores to variables the
  // allocation size depends on, as well as the usual loads,
  // otherwise it'll yield the wrong result after movement) and
  // likely be more suitable as an LLVM optimisation pass.
  for (Function *F : ConstantAllocaRaiseCandidates)
    raiseUserConstantDataAllocasToEntryBlock(Builder, F);

  EmitMetadataErrorReportFunctionTy &&ErrorReportFn =
      [](EmitMetadataErrorKind Kind,
         const TargetRegionEntryInfo &EntryInfo) -> void {
    errs() << "Error of kind: " << Kind
           << " when emitting offload entries and metadata during "
              "OMPIRBuilder finalization \n";
  };

  if (!OffloadInfoManager.empty())
    createOffloadEntriesAndInfoMetadata(ErrorReportFn);

  if (Config.EmitLLVMUsedMetaInfo.value_or(false)) {
    std::vector<WeakTrackingVH> LLVMCompilerUsed = {
        M.getGlobalVariable("__openmp_nvptx_data_transfer_temporary_storage")};
    emitUsed("llvm.compiler.used", LLVMCompilerUsed);
  }
}

OpenMPIRBuilder::~OpenMPIRBuilder() {
  assert(OutlineInfos.empty() && "There must be no outstanding outlinings");
}

GlobalValue *OpenMPIRBuilder::createGlobalFlag(unsigned Value, StringRef Name) {
  IntegerType *I32Ty = Type::getInt32Ty(M.getContext());
  auto *GV =
      new GlobalVariable(M, I32Ty,
                         /* isConstant = */ true, GlobalValue::WeakODRLinkage,
                         ConstantInt::get(I32Ty, Value), Name);
  GV->setVisibility(GlobalValue::HiddenVisibility);

  return GV;
}

Constant *OpenMPIRBuilder::getOrCreateIdent(Constant *SrcLocStr,
                                            uint32_t SrcLocStrSize,
                                            IdentFlag LocFlags,
                                            unsigned Reserve2Flags) {
  // Enable "C-mode".
  LocFlags |= OMP_IDENT_FLAG_KMPC;

  Constant *&Ident =
      IdentMap[{SrcLocStr, uint64_t(LocFlags) << 31 | Reserve2Flags}];
  if (!Ident) {
    Constant *I32Null = ConstantInt::getNullValue(Int32);
    Constant *IdentData[] = {I32Null,
                             ConstantInt::get(Int32, uint32_t(LocFlags)),
                             ConstantInt::get(Int32, Reserve2Flags),
                             ConstantInt::get(Int32, SrcLocStrSize), SrcLocStr};
    Constant *Initializer =
        ConstantStruct::get(OpenMPIRBuilder::Ident, IdentData);

    // Look for existing encoding of the location + flags, not needed but
    // minimizes the difference to the existing solution while we transition.
    for (GlobalVariable &GV : M.globals())
      if (GV.getValueType() == OpenMPIRBuilder::Ident && GV.hasInitializer())
        if (GV.getInitializer() == Initializer)
          Ident = &GV;

    if (!Ident) {
      auto *GV = new GlobalVariable(
          M, OpenMPIRBuilder::Ident,
          /* isConstant = */ true, GlobalValue::PrivateLinkage, Initializer, "",
          nullptr, GlobalValue::NotThreadLocal,
          M.getDataLayout().getDefaultGlobalsAddressSpace());
      GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
      GV->setAlignment(Align(8));
      Ident = GV;
    }
  }

  return ConstantExpr::getPointerBitCastOrAddrSpaceCast(Ident, IdentPtr);
}

Constant *OpenMPIRBuilder::getOrCreateSrcLocStr(StringRef LocStr,
                                                uint32_t &SrcLocStrSize) {
  SrcLocStrSize = LocStr.size();
  Constant *&SrcLocStr = SrcLocStrMap[LocStr];
  if (!SrcLocStr) {
    Constant *Initializer =
        ConstantDataArray::getString(M.getContext(), LocStr);

    // Look for existing encoding of the location, not needed but minimizes the
    // difference to the existing solution while we transition.
    for (GlobalVariable &GV : M.globals())
      if (GV.isConstant() && GV.hasInitializer() &&
          GV.getInitializer() == Initializer)
        return SrcLocStr = ConstantExpr::getPointerCast(&GV, Int8Ptr);

    SrcLocStr = Builder.CreateGlobalStringPtr(LocStr, /* Name */ "",
                                              /* AddressSpace */ 0, &M);
  }
  return SrcLocStr;
}

Constant *OpenMPIRBuilder::getOrCreateSrcLocStr(StringRef FunctionName,
                                                StringRef FileName,
                                                unsigned Line, unsigned Column,
                                                uint32_t &SrcLocStrSize) {
  SmallString<128> Buffer;
  Buffer.push_back(';');
  Buffer.append(FileName);
  Buffer.push_back(';');
  Buffer.append(FunctionName);
  Buffer.push_back(';');
  Buffer.append(std::to_string(Line));
  Buffer.push_back(';');
  Buffer.append(std::to_string(Column));
  Buffer.push_back(';');
  Buffer.push_back(';');
  return getOrCreateSrcLocStr(Buffer.str(), SrcLocStrSize);
}

Constant *
OpenMPIRBuilder::getOrCreateDefaultSrcLocStr(uint32_t &SrcLocStrSize) {
  StringRef UnknownLoc = ";unknown;unknown;0;0;;";
  return getOrCreateSrcLocStr(UnknownLoc, SrcLocStrSize);
}

Constant *OpenMPIRBuilder::getOrCreateSrcLocStr(DebugLoc DL,
                                                uint32_t &SrcLocStrSize,
                                                Function *F) {
  DILocation *DIL = DL.get();
  if (!DIL)
    return getOrCreateDefaultSrcLocStr(SrcLocStrSize);
  StringRef FileName = M.getName();
  if (DIFile *DIF = DIL->getFile())
    if (std::optional<StringRef> Source = DIF->getSource())
      FileName = *Source;
  StringRef Function = DIL->getScope()->getSubprogram()->getName();
  if (Function.empty() && F)
    Function = F->getName();
  return getOrCreateSrcLocStr(Function, FileName, DIL->getLine(),
                              DIL->getColumn(), SrcLocStrSize);
}

Constant *OpenMPIRBuilder::getOrCreateSrcLocStr(const LocationDescription &Loc,
                                                uint32_t &SrcLocStrSize) {
  return getOrCreateSrcLocStr(Loc.DL, SrcLocStrSize,
                              Loc.IP.getBlock()->getParent());
}

Value *OpenMPIRBuilder::getOrCreateThreadID(Value *Ident) {
  return Builder.CreateCall(
      getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_global_thread_num), Ident,
      "omp_global_thread_num");
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createBarrier(const LocationDescription &Loc, Directive Kind,
                               bool ForceSimpleCall, bool CheckCancelFlag) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  // Build call __kmpc_cancel_barrier(loc, thread_id) or
  //            __kmpc_barrier(loc, thread_id);

  IdentFlag BarrierLocFlags;
  switch (Kind) {
  case OMPD_for:
    BarrierLocFlags = OMP_IDENT_FLAG_BARRIER_IMPL_FOR;
    break;
  case OMPD_sections:
    BarrierLocFlags = OMP_IDENT_FLAG_BARRIER_IMPL_SECTIONS;
    break;
  case OMPD_single:
    BarrierLocFlags = OMP_IDENT_FLAG_BARRIER_IMPL_SINGLE;
    break;
  case OMPD_barrier:
    BarrierLocFlags = OMP_IDENT_FLAG_BARRIER_EXPL;
    break;
  default:
    BarrierLocFlags = OMP_IDENT_FLAG_BARRIER_IMPL;
    break;
  }

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Args[] = {
      getOrCreateIdent(SrcLocStr, SrcLocStrSize, BarrierLocFlags),
      getOrCreateThreadID(getOrCreateIdent(SrcLocStr, SrcLocStrSize))};

  // If we are in a cancellable parallel region, barriers are cancellation
  // points.
  // TODO: Check why we would force simple calls or to ignore the cancel flag.
  bool UseCancelBarrier =
      !ForceSimpleCall && isLastFinalizationInfoCancellable(OMPD_parallel);

  Value *Result =
      Builder.CreateCall(getOrCreateRuntimeFunctionPtr(
                             UseCancelBarrier ? OMPRTL___kmpc_cancel_barrier
                                              : OMPRTL___kmpc_barrier),
                         Args);

  if (UseCancelBarrier && CheckCancelFlag)
    emitCancelationCheckImpl(Result, OMPD_parallel);

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createCancel(const LocationDescription &Loc,
                              Value *IfCondition,
                              omp::Directive CanceledDirective) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  // LLVM utilities like blocks with terminators.
  auto *UI = Builder.CreateUnreachable();

  Instruction *ThenTI = UI, *ElseTI = nullptr;
  if (IfCondition)
    SplitBlockAndInsertIfThenElse(IfCondition, UI, &ThenTI, &ElseTI);
  Builder.SetInsertPoint(ThenTI);

  Value *CancelKind = nullptr;
  switch (CanceledDirective) {
#define OMP_CANCEL_KIND(Enum, Str, DirectiveEnum, Value)                       \
  case DirectiveEnum:                                                          \
    CancelKind = Builder.getInt32(Value);                                      \
    break;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  default:
    llvm_unreachable("Unknown cancel kind!");
  }

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *Args[] = {Ident, getOrCreateThreadID(Ident), CancelKind};
  Value *Result = Builder.CreateCall(
      getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_cancel), Args);
  auto ExitCB = [this, CanceledDirective, Loc](InsertPointTy IP) {
    if (CanceledDirective == OMPD_parallel) {
      IRBuilder<>::InsertPointGuard IPG(Builder);
      Builder.restoreIP(IP);
      createBarrier(LocationDescription(Builder.saveIP(), Loc.DL),
                    omp::Directive::OMPD_unknown, /* ForceSimpleCall */ false,
                    /* CheckCancelFlag */ false);
    }
  };

  // The actual cancel logic is shared with others, e.g., cancel_barriers.
  emitCancelationCheckImpl(Result, CanceledDirective, ExitCB);

  // Update the insertion point and remove the terminator we introduced.
  Builder.SetInsertPoint(UI->getParent());
  UI->eraseFromParent();

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::emitTargetKernel(
    const LocationDescription &Loc, InsertPointTy AllocaIP, Value *&Return,
    Value *Ident, Value *DeviceID, Value *NumTeams, Value *NumThreads,
    Value *HostPtr, ArrayRef<Value *> KernelArgs) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  Builder.restoreIP(AllocaIP);
  auto *KernelArgsPtr =
      Builder.CreateAlloca(OpenMPIRBuilder::KernelArgs, nullptr, "kernel_args");
  Builder.restoreIP(Loc.IP);

  for (unsigned I = 0, Size = KernelArgs.size(); I != Size; ++I) {
    llvm::Value *Arg =
        Builder.CreateStructGEP(OpenMPIRBuilder::KernelArgs, KernelArgsPtr, I);
    Builder.CreateAlignedStore(
        KernelArgs[I], Arg,
        M.getDataLayout().getPrefTypeAlign(KernelArgs[I]->getType()));
  }

  SmallVector<Value *> OffloadingArgs{Ident,      DeviceID, NumTeams,
                                      NumThreads, HostPtr,  KernelArgsPtr};

  Return = Builder.CreateCall(
      getOrCreateRuntimeFunction(M, OMPRTL___tgt_target_kernel),
      OffloadingArgs);

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::emitKernelLaunch(
    const LocationDescription &Loc, Function *OutlinedFn, Value *OutlinedFnID,
    EmitFallbackCallbackTy emitTargetCallFallbackCB, TargetKernelArgs &Args,
    Value *DeviceID, Value *RTLoc, InsertPointTy AllocaIP) {

  if (!updateToLocation(Loc))
    return Loc.IP;

  Builder.restoreIP(Loc.IP);
  // On top of the arrays that were filled up, the target offloading call
  // takes as arguments the device id as well as the host pointer. The host
  // pointer is used by the runtime library to identify the current target
  // region, so it only has to be unique and not necessarily point to
  // anything. It could be the pointer to the outlined function that
  // implements the target region, but we aren't using that so that the
  // compiler doesn't need to keep that, and could therefore inline the host
  // function if proven worthwhile during optimization.

  // From this point on, we need to have an ID of the target region defined.
  assert(OutlinedFnID && "Invalid outlined function ID!");
  (void)OutlinedFnID;

  // Return value of the runtime offloading call.
  Value *Return = nullptr;

  // Arguments for the target kernel.
  SmallVector<Value *> ArgsVector;
  getKernelArgsVector(Args, Builder, ArgsVector);

  // The target region is an outlined function launched by the runtime
  // via calls to __tgt_target_kernel().
  //
  // Note that on the host and CPU targets, the runtime implementation of
  // these calls simply call the outlined function without forking threads.
  // The outlined functions themselves have runtime calls to
  // __kmpc_fork_teams() and __kmpc_fork() for this purpose, codegen'd by
  // the compiler in emitTeamsCall() and emitParallelCall().
  //
  // In contrast, on the NVPTX target, the implementation of
  // __tgt_target_teams() launches a GPU kernel with the requested number
  // of teams and threads so no additional calls to the runtime are required.
  // Check the error code and execute the host version if required.
  Builder.restoreIP(emitTargetKernel(Builder, AllocaIP, Return, RTLoc, DeviceID,
                                     Args.NumTeams, Args.NumThreads,
                                     OutlinedFnID, ArgsVector));

  BasicBlock *OffloadFailedBlock =
      BasicBlock::Create(Builder.getContext(), "omp_offload.failed");
  BasicBlock *OffloadContBlock =
      BasicBlock::Create(Builder.getContext(), "omp_offload.cont");
  Value *Failed = Builder.CreateIsNotNull(Return);
  Builder.CreateCondBr(Failed, OffloadFailedBlock, OffloadContBlock);

  auto CurFn = Builder.GetInsertBlock()->getParent();
  emitBlock(OffloadFailedBlock, CurFn);
  Builder.restoreIP(emitTargetCallFallbackCB(Builder.saveIP()));
  emitBranch(OffloadContBlock);
  emitBlock(OffloadContBlock, CurFn, /*IsFinished=*/true);
  return Builder.saveIP();
}

void OpenMPIRBuilder::emitCancelationCheckImpl(Value *CancelFlag,
                                               omp::Directive CanceledDirective,
                                               FinalizeCallbackTy ExitCB) {
  assert(isLastFinalizationInfoCancellable(CanceledDirective) &&
         "Unexpected cancellation!");

  // For a cancel barrier we create two new blocks.
  BasicBlock *BB = Builder.GetInsertBlock();
  BasicBlock *NonCancellationBlock;
  if (Builder.GetInsertPoint() == BB->end()) {
    // TODO: This branch will not be needed once we moved to the
    // OpenMPIRBuilder codegen completely.
    NonCancellationBlock = BasicBlock::Create(
        BB->getContext(), BB->getName() + ".cont", BB->getParent());
  } else {
    NonCancellationBlock = SplitBlock(BB, &*Builder.GetInsertPoint());
    BB->getTerminator()->eraseFromParent();
    Builder.SetInsertPoint(BB);
  }
  BasicBlock *CancellationBlock = BasicBlock::Create(
      BB->getContext(), BB->getName() + ".cncl", BB->getParent());

  // Jump to them based on the return value.
  Value *Cmp = Builder.CreateIsNull(CancelFlag);
  Builder.CreateCondBr(Cmp, NonCancellationBlock, CancellationBlock,
                       /* TODO weight */ nullptr, nullptr);

  // From the cancellation block we finalize all variables and go to the
  // post finalization block that is known to the FiniCB callback.
  Builder.SetInsertPoint(CancellationBlock);
  if (ExitCB)
    ExitCB(Builder.saveIP());
  auto &FI = FinalizationStack.back();
  FI.FiniCB(Builder.saveIP());

  // The continuation block is where code generation continues.
  Builder.SetInsertPoint(NonCancellationBlock, NonCancellationBlock->begin());
}

// Callback used to create OpenMP runtime calls to support
// omp parallel clause for the device.
// We need to use this callback to replace call to the OutlinedFn in OuterFn
// by the call to the OpenMP DeviceRTL runtime function (kmpc_parallel_51)
static void targetParallelCallback(
    OpenMPIRBuilder *OMPIRBuilder, Function &OutlinedFn, Function *OuterFn,
    BasicBlock *OuterAllocaBB, Value *Ident, Value *IfCondition,
    Value *NumThreads, Instruction *PrivTID, AllocaInst *PrivTIDAddr,
    Value *ThreadID, const SmallVector<Instruction *, 4> &ToBeDeleted) {
  // Add some known attributes.
  IRBuilder<> &Builder = OMPIRBuilder->Builder;
  OutlinedFn.addParamAttr(0, Attribute::NoAlias);
  OutlinedFn.addParamAttr(1, Attribute::NoAlias);
  OutlinedFn.addParamAttr(0, Attribute::NoUndef);
  OutlinedFn.addParamAttr(1, Attribute::NoUndef);
  OutlinedFn.addFnAttr(Attribute::NoUnwind);

  assert(OutlinedFn.arg_size() >= 2 &&
         "Expected at least tid and bounded tid as arguments");
  unsigned NumCapturedVars = OutlinedFn.arg_size() - /* tid & bounded tid */ 2;

  CallInst *CI = cast<CallInst>(OutlinedFn.user_back());
  assert(CI && "Expected call instruction to outlined function");
  CI->getParent()->setName("omp_parallel");

  Builder.SetInsertPoint(CI);
  Type *PtrTy = OMPIRBuilder->VoidPtr;
  Value *NullPtrValue = Constant::getNullValue(PtrTy);

  // Add alloca for kernel args
  OpenMPIRBuilder ::InsertPointTy CurrentIP = Builder.saveIP();
  Builder.SetInsertPoint(OuterAllocaBB, OuterAllocaBB->getFirstInsertionPt());
  AllocaInst *ArgsAlloca =
      Builder.CreateAlloca(ArrayType::get(PtrTy, NumCapturedVars));
  Value *Args = ArgsAlloca;
  // Add address space cast if array for storing arguments is not allocated
  // in address space 0
  if (ArgsAlloca->getAddressSpace())
    Args = Builder.CreatePointerCast(ArgsAlloca, PtrTy);
  Builder.restoreIP(CurrentIP);

  // Store captured vars which are used by kmpc_parallel_51
  for (unsigned Idx = 0; Idx < NumCapturedVars; Idx++) {
    Value *V = *(CI->arg_begin() + 2 + Idx);
    Value *StoreAddress = Builder.CreateConstInBoundsGEP2_64(
        ArrayType::get(PtrTy, NumCapturedVars), Args, 0, Idx);
    Builder.CreateStore(V, StoreAddress);
  }

  Value *Cond =
      IfCondition ? Builder.CreateSExtOrTrunc(IfCondition, OMPIRBuilder->Int32)
                  : Builder.getInt32(1);

  // Build kmpc_parallel_51 call
  Value *Parallel51CallArgs[] = {
      /* identifier*/ Ident,
      /* global thread num*/ ThreadID,
      /* if expression */ Cond,
      /* number of threads */ NumThreads ? NumThreads : Builder.getInt32(-1),
      /* Proc bind */ Builder.getInt32(-1),
      /* outlined function */
      Builder.CreateBitCast(&OutlinedFn, OMPIRBuilder->ParallelTaskPtr),
      /* wrapper function */ NullPtrValue,
      /* arguments of the outlined funciton*/ Args,
      /* number of arguments */ Builder.getInt64(NumCapturedVars)};

  FunctionCallee RTLFn =
      OMPIRBuilder->getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_parallel_51);

  Builder.CreateCall(RTLFn, Parallel51CallArgs);

  LLVM_DEBUG(dbgs() << "With kmpc_parallel_51 placed: "
                    << *Builder.GetInsertBlock()->getParent() << "\n");

  // Initialize the local TID stack location with the argument value.
  Builder.SetInsertPoint(PrivTID);
  Function::arg_iterator OutlinedAI = OutlinedFn.arg_begin();
  Builder.CreateStore(Builder.CreateLoad(OMPIRBuilder->Int32, OutlinedAI),
                      PrivTIDAddr);

  // Remove redundant call to the outlined function.
  CI->eraseFromParent();

  for (Instruction *I : ToBeDeleted) {
    I->eraseFromParent();
  }
}

// Callback used to create OpenMP runtime calls to support
// omp parallel clause for the host.
// We need to use this callback to replace call to the OutlinedFn in OuterFn
// by the call to the OpenMP host runtime function ( __kmpc_fork_call[_if])
static void
hostParallelCallback(OpenMPIRBuilder *OMPIRBuilder, Function &OutlinedFn,
                     Function *OuterFn, Value *Ident, Value *IfCondition,
                     Instruction *PrivTID, AllocaInst *PrivTIDAddr,
                     const SmallVector<Instruction *, 4> &ToBeDeleted) {
  IRBuilder<> &Builder = OMPIRBuilder->Builder;
  FunctionCallee RTLFn;
  if (IfCondition) {
    RTLFn =
        OMPIRBuilder->getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_fork_call_if);
  } else {
    RTLFn =
        OMPIRBuilder->getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_fork_call);
  }
  if (auto *F = dyn_cast<Function>(RTLFn.getCallee())) {
    if (!F->hasMetadata(LLVMContext::MD_callback)) {
      LLVMContext &Ctx = F->getContext();
      MDBuilder MDB(Ctx);
      // Annotate the callback behavior of the __kmpc_fork_call:
      //  - The callback callee is argument number 2 (microtask).
      //  - The first two arguments of the callback callee are unknown (-1).
      //  - All variadic arguments to the __kmpc_fork_call are passed to the
      //    callback callee.
      F->addMetadata(LLVMContext::MD_callback,
                     *MDNode::get(Ctx, {MDB.createCallbackEncoding(
                                           2, {-1, -1},
                                           /* VarArgsArePassed */ true)}));
    }
  }
  // Add some known attributes.
  OutlinedFn.addParamAttr(0, Attribute::NoAlias);
  OutlinedFn.addParamAttr(1, Attribute::NoAlias);
  OutlinedFn.addFnAttr(Attribute::NoUnwind);

  assert(OutlinedFn.arg_size() >= 2 &&
         "Expected at least tid and bounded tid as arguments");
  unsigned NumCapturedVars = OutlinedFn.arg_size() - /* tid & bounded tid */ 2;

  CallInst *CI = cast<CallInst>(OutlinedFn.user_back());
  CI->getParent()->setName("omp_parallel");
  Builder.SetInsertPoint(CI);

  // Build call __kmpc_fork_call[_if](Ident, n, microtask, var1, .., varn);
  Value *ForkCallArgs[] = {
      Ident, Builder.getInt32(NumCapturedVars),
      Builder.CreateBitCast(&OutlinedFn, OMPIRBuilder->ParallelTaskPtr)};

  SmallVector<Value *, 16> RealArgs;
  RealArgs.append(std::begin(ForkCallArgs), std::end(ForkCallArgs));
  if (IfCondition) {
    Value *Cond = Builder.CreateSExtOrTrunc(IfCondition, OMPIRBuilder->Int32);
    RealArgs.push_back(Cond);
  }
  RealArgs.append(CI->arg_begin() + /* tid & bound tid */ 2, CI->arg_end());

  // __kmpc_fork_call_if always expects a void ptr as the last argument
  // If there are no arguments, pass a null pointer.
  auto PtrTy = OMPIRBuilder->VoidPtr;
  if (IfCondition && NumCapturedVars == 0) {
    Value *NullPtrValue = Constant::getNullValue(PtrTy);
    RealArgs.push_back(NullPtrValue);
  }
  if (IfCondition && RealArgs.back()->getType() != PtrTy)
    RealArgs.back() = Builder.CreateBitCast(RealArgs.back(), PtrTy);

  Builder.CreateCall(RTLFn, RealArgs);

  LLVM_DEBUG(dbgs() << "With fork_call placed: "
                    << *Builder.GetInsertBlock()->getParent() << "\n");

  // Initialize the local TID stack location with the argument value.
  Builder.SetInsertPoint(PrivTID);
  Function::arg_iterator OutlinedAI = OutlinedFn.arg_begin();
  Builder.CreateStore(Builder.CreateLoad(OMPIRBuilder->Int32, OutlinedAI),
                      PrivTIDAddr);

  // Remove redundant call to the outlined function.
  CI->eraseFromParent();

  for (Instruction *I : ToBeDeleted) {
    I->eraseFromParent();
  }
}

IRBuilder<>::InsertPoint OpenMPIRBuilder::createParallel(
    const LocationDescription &Loc, InsertPointTy OuterAllocaIP,
    BodyGenCallbackTy BodyGenCB, PrivatizeCallbackTy PrivCB,
    FinalizeCallbackTy FiniCB, Value *IfCondition, Value *NumThreads,
    omp::ProcBindKind ProcBind, bool IsCancellable) {
  assert(!isConflictIP(Loc.IP, OuterAllocaIP) && "IPs must not be ambiguous");

  if (!updateToLocation(Loc))
    return Loc.IP;

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadID = getOrCreateThreadID(Ident);
  // If we generate code for the target device, we need to allocate
  // struct for aggregate params in the device default alloca address space.
  // OpenMP runtime requires that the params of the extracted functions are
  // passed as zero address space pointers. This flag ensures that extracted
  // function arguments are declared in zero address space
  bool ArgsInZeroAddressSpace = Config.isTargetDevice();

  // Build call __kmpc_push_num_threads(&Ident, global_tid, num_threads)
  // only if we compile for host side.
  if (NumThreads && !Config.isTargetDevice()) {
    Value *Args[] = {
        Ident, ThreadID,
        Builder.CreateIntCast(NumThreads, Int32, /*isSigned*/ false)};
    Builder.CreateCall(
        getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_push_num_threads), Args);
  }

  if (ProcBind != OMP_PROC_BIND_default) {
    // Build call __kmpc_push_proc_bind(&Ident, global_tid, proc_bind)
    Value *Args[] = {
        Ident, ThreadID,
        ConstantInt::get(Int32, unsigned(ProcBind), /*isSigned=*/true)};
    Builder.CreateCall(
        getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_push_proc_bind), Args);
  }

  BasicBlock *InsertBB = Builder.GetInsertBlock();
  Function *OuterFn = InsertBB->getParent();

  // Save the outer alloca block because the insertion iterator may get
  // invalidated and we still need this later.
  BasicBlock *OuterAllocaBlock = OuterAllocaIP.getBlock();

  // Vector to remember instructions we used only during the modeling but which
  // we want to delete at the end.
  SmallVector<Instruction *, 4> ToBeDeleted;

  // Change the location to the outer alloca insertion point to create and
  // initialize the allocas we pass into the parallel region.
  InsertPointTy NewOuter(OuterAllocaBlock, OuterAllocaBlock->begin());
  Builder.restoreIP(NewOuter);
  AllocaInst *TIDAddrAlloca = Builder.CreateAlloca(Int32, nullptr, "tid.addr");
  AllocaInst *ZeroAddrAlloca =
      Builder.CreateAlloca(Int32, nullptr, "zero.addr");
  Instruction *TIDAddr = TIDAddrAlloca;
  Instruction *ZeroAddr = ZeroAddrAlloca;
  if (ArgsInZeroAddressSpace && M.getDataLayout().getAllocaAddrSpace() != 0) {
    // Add additional casts to enforce pointers in zero address space
    TIDAddr = new AddrSpaceCastInst(
        TIDAddrAlloca, PointerType ::get(M.getContext(), 0), "tid.addr.ascast");
    TIDAddr->insertAfter(TIDAddrAlloca);
    ToBeDeleted.push_back(TIDAddr);
    ZeroAddr = new AddrSpaceCastInst(ZeroAddrAlloca,
                                     PointerType ::get(M.getContext(), 0),
                                     "zero.addr.ascast");
    ZeroAddr->insertAfter(ZeroAddrAlloca);
    ToBeDeleted.push_back(ZeroAddr);
  }

  // We only need TIDAddr and ZeroAddr for modeling purposes to get the
  // associated arguments in the outlined function, so we delete them later.
  ToBeDeleted.push_back(TIDAddrAlloca);
  ToBeDeleted.push_back(ZeroAddrAlloca);

  // Create an artificial insertion point that will also ensure the blocks we
  // are about to split are not degenerated.
  auto *UI = new UnreachableInst(Builder.getContext(), InsertBB);

  BasicBlock *EntryBB = UI->getParent();
  BasicBlock *PRegEntryBB = EntryBB->splitBasicBlock(UI, "omp.par.entry");
  BasicBlock *PRegBodyBB = PRegEntryBB->splitBasicBlock(UI, "omp.par.region");
  BasicBlock *PRegPreFiniBB =
      PRegBodyBB->splitBasicBlock(UI, "omp.par.pre_finalize");
  BasicBlock *PRegExitBB = PRegPreFiniBB->splitBasicBlock(UI, "omp.par.exit");

  auto FiniCBWrapper = [&](InsertPointTy IP) {
    // Hide "open-ended" blocks from the given FiniCB by setting the right jump
    // target to the region exit block.
    if (IP.getBlock()->end() == IP.getPoint()) {
      IRBuilder<>::InsertPointGuard IPG(Builder);
      Builder.restoreIP(IP);
      Instruction *I = Builder.CreateBr(PRegExitBB);
      IP = InsertPointTy(I->getParent(), I->getIterator());
    }
    assert(IP.getBlock()->getTerminator()->getNumSuccessors() == 1 &&
           IP.getBlock()->getTerminator()->getSuccessor(0) == PRegExitBB &&
           "Unexpected insertion point for finalization call!");
    return FiniCB(IP);
  };

  FinalizationStack.push_back({FiniCBWrapper, OMPD_parallel, IsCancellable});

  // Generate the privatization allocas in the block that will become the entry
  // of the outlined function.
  Builder.SetInsertPoint(PRegEntryBB->getTerminator());
  InsertPointTy InnerAllocaIP = Builder.saveIP();

  AllocaInst *PrivTIDAddr =
      Builder.CreateAlloca(Int32, nullptr, "tid.addr.local");
  Instruction *PrivTID = Builder.CreateLoad(Int32, PrivTIDAddr, "tid");

  // Add some fake uses for OpenMP provided arguments.
  ToBeDeleted.push_back(Builder.CreateLoad(Int32, TIDAddr, "tid.addr.use"));
  Instruction *ZeroAddrUse =
      Builder.CreateLoad(Int32, ZeroAddr, "zero.addr.use");
  ToBeDeleted.push_back(ZeroAddrUse);

  // EntryBB
  //   |
  //   V
  // PRegionEntryBB         <- Privatization allocas are placed here.
  //   |
  //   V
  // PRegionBodyBB          <- BodeGen is invoked here.
  //   |
  //   V
  // PRegPreFiniBB          <- The block we will start finalization from.
  //   |
  //   V
  // PRegionExitBB          <- A common exit to simplify block collection.
  //

  LLVM_DEBUG(dbgs() << "Before body codegen: " << *OuterFn << "\n");

  // Let the caller create the body.
  assert(BodyGenCB && "Expected body generation callback!");
  InsertPointTy CodeGenIP(PRegBodyBB, PRegBodyBB->begin());
  BodyGenCB(InnerAllocaIP, CodeGenIP);

  LLVM_DEBUG(dbgs() << "After  body codegen: " << *OuterFn << "\n");

  OutlineInfo OI;
  if (Config.isTargetDevice()) {
    // Generate OpenMP target specific runtime call
    OI.PostOutlineCB = [=, ToBeDeletedVec =
                               std::move(ToBeDeleted)](Function &OutlinedFn) {
      targetParallelCallback(this, OutlinedFn, OuterFn, OuterAllocaBlock, Ident,
                             IfCondition, NumThreads, PrivTID, PrivTIDAddr,
                             ThreadID, ToBeDeletedVec);
    };
  } else {
    // Generate OpenMP host runtime call
    OI.PostOutlineCB = [=, ToBeDeletedVec =
                               std::move(ToBeDeleted)](Function &OutlinedFn) {
      hostParallelCallback(this, OutlinedFn, OuterFn, Ident, IfCondition,
                           PrivTID, PrivTIDAddr, ToBeDeletedVec);
    };
  }

  OI.OuterAllocaBB = OuterAllocaBlock;
  OI.EntryBB = PRegEntryBB;
  OI.ExitBB = PRegExitBB;

  SmallPtrSet<BasicBlock *, 32> ParallelRegionBlockSet;
  SmallVector<BasicBlock *, 32> Blocks;
  OI.collectBlocks(ParallelRegionBlockSet, Blocks);

  // Ensure a single exit node for the outlined region by creating one.
  // We might have multiple incoming edges to the exit now due to finalizations,
  // e.g., cancel calls that cause the control flow to leave the region.
  BasicBlock *PRegOutlinedExitBB = PRegExitBB;
  PRegExitBB = SplitBlock(PRegExitBB, &*PRegExitBB->getFirstInsertionPt());
  PRegOutlinedExitBB->setName("omp.par.outlined.exit");
  Blocks.push_back(PRegOutlinedExitBB);

  CodeExtractorAnalysisCache CEAC(*OuterFn);
  CodeExtractor Extractor(Blocks, /* DominatorTree */ nullptr,
                          /* AggregateArgs */ false,
                          /* BlockFrequencyInfo */ nullptr,
                          /* BranchProbabilityInfo */ nullptr,
                          /* AssumptionCache */ nullptr,
                          /* AllowVarArgs */ true,
                          /* AllowAlloca */ true,
                          /* AllocationBlock */ OuterAllocaBlock,
                          /* Suffix */ ".omp_par", ArgsInZeroAddressSpace);

  // Find inputs to, outputs from the code region.
  BasicBlock *CommonExit = nullptr;
  SetVector<Value *> Inputs, Outputs, SinkingCands, HoistingCands;
  Extractor.findAllocas(CEAC, SinkingCands, HoistingCands, CommonExit);
  Extractor.findInputsOutputs(Inputs, Outputs, SinkingCands);

  LLVM_DEBUG(dbgs() << "Before privatization: " << *OuterFn << "\n");

  FunctionCallee TIDRTLFn =
      getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_global_thread_num);

  auto PrivHelper = [&](Value &V) {
    if (&V == TIDAddr || &V == ZeroAddr) {
      OI.ExcludeArgsFromAggregate.push_back(&V);
      return;
    }

    SetVector<Use *> Uses;
    for (Use &U : V.uses())
      if (auto *UserI = dyn_cast<Instruction>(U.getUser()))
        if (ParallelRegionBlockSet.count(UserI->getParent()))
          Uses.insert(&U);

    // __kmpc_fork_call expects extra arguments as pointers. If the input
    // already has a pointer type, everything is fine. Otherwise, store the
    // value onto stack and load it back inside the to-be-outlined region. This
    // will ensure only the pointer will be passed to the function.
    // FIXME: if there are more than 15 trailing arguments, they must be
    // additionally packed in a struct.
    Value *Inner = &V;
    if (!V.getType()->isPointerTy()) {
      IRBuilder<>::InsertPointGuard Guard(Builder);
      LLVM_DEBUG(llvm::dbgs() << "Forwarding input as pointer: " << V << "\n");

      Builder.restoreIP(OuterAllocaIP);
      Value *Ptr =
          Builder.CreateAlloca(V.getType(), nullptr, V.getName() + ".reloaded");

      // Store to stack at end of the block that currently branches to the entry
      // block of the to-be-outlined region.
      Builder.SetInsertPoint(InsertBB,
                             InsertBB->getTerminator()->getIterator());
      Builder.CreateStore(&V, Ptr);

      // Load back next to allocations in the to-be-outlined region.
      Builder.restoreIP(InnerAllocaIP);
      Inner = Builder.CreateLoad(V.getType(), Ptr);
    }

    Value *ReplacementValue = nullptr;
    CallInst *CI = dyn_cast<CallInst>(&V);
    if (CI && CI->getCalledFunction() == TIDRTLFn.getCallee()) {
      ReplacementValue = PrivTID;
    } else {
      Builder.restoreIP(
          PrivCB(InnerAllocaIP, Builder.saveIP(), V, *Inner, ReplacementValue));
      InnerAllocaIP = {
          InnerAllocaIP.getBlock(),
          InnerAllocaIP.getBlock()->getTerminator()->getIterator()};

      assert(ReplacementValue &&
             "Expected copy/create callback to set replacement value!");
      if (ReplacementValue == &V)
        return;
    }

    for (Use *UPtr : Uses)
      UPtr->set(ReplacementValue);
  };

  // Reset the inner alloca insertion as it will be used for loading the values
  // wrapped into pointers before passing them into the to-be-outlined region.
  // Configure it to insert immediately after the fake use of zero address so
  // that they are available in the generated body and so that the
  // OpenMP-related values (thread ID and zero address pointers) remain leading
  // in the argument list.
  InnerAllocaIP = IRBuilder<>::InsertPoint(
      ZeroAddrUse->getParent(), ZeroAddrUse->getNextNode()->getIterator());

  // Reset the outer alloca insertion point to the entry of the relevant block
  // in case it was invalidated.
  OuterAllocaIP = IRBuilder<>::InsertPoint(
      OuterAllocaBlock, OuterAllocaBlock->getFirstInsertionPt());

  for (Value *Input : Inputs) {
    LLVM_DEBUG(dbgs() << "Captured input: " << *Input << "\n");
    PrivHelper(*Input);
  }
  LLVM_DEBUG({
    for (Value *Output : Outputs)
      LLVM_DEBUG(dbgs() << "Captured output: " << *Output << "\n");
  });
  assert(Outputs.empty() &&
         "OpenMP outlining should not produce live-out values!");

  LLVM_DEBUG(dbgs() << "After  privatization: " << *OuterFn << "\n");
  LLVM_DEBUG({
    for (auto *BB : Blocks)
      dbgs() << " PBR: " << BB->getName() << "\n";
  });

  // Adjust the finalization stack, verify the adjustment, and call the
  // finalize function a last time to finalize values between the pre-fini
  // block and the exit block if we left the parallel "the normal way".
  auto FiniInfo = FinalizationStack.pop_back_val();
  (void)FiniInfo;
  assert(FiniInfo.DK == OMPD_parallel &&
         "Unexpected finalization stack state!");

  Instruction *PRegPreFiniTI = PRegPreFiniBB->getTerminator();

  InsertPointTy PreFiniIP(PRegPreFiniBB, PRegPreFiniTI->getIterator());
  FiniCB(PreFiniIP);

  // Register the outlined info.
  addOutlineInfo(std::move(OI));

  InsertPointTy AfterIP(UI->getParent(), UI->getParent()->end());
  UI->eraseFromParent();

  return AfterIP;
}

void OpenMPIRBuilder::emitFlush(const LocationDescription &Loc) {
  // Build call void __kmpc_flush(ident_t *loc)
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Args[] = {getOrCreateIdent(SrcLocStr, SrcLocStrSize)};

  Builder.CreateCall(getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_flush), Args);
}

void OpenMPIRBuilder::createFlush(const LocationDescription &Loc) {
  if (!updateToLocation(Loc))
    return;
  emitFlush(Loc);
}

void OpenMPIRBuilder::emitTaskwaitImpl(const LocationDescription &Loc) {
  // Build call kmp_int32 __kmpc_omp_taskwait(ident_t *loc, kmp_int32
  // global_tid);
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *Args[] = {Ident, getOrCreateThreadID(Ident)};

  // Ignore return result until untied tasks are supported.
  Builder.CreateCall(getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_taskwait),
                     Args);
}

void OpenMPIRBuilder::createTaskwait(const LocationDescription &Loc) {
  if (!updateToLocation(Loc))
    return;
  emitTaskwaitImpl(Loc);
}

void OpenMPIRBuilder::emitTaskyieldImpl(const LocationDescription &Loc) {
  // Build call __kmpc_omp_taskyield(loc, thread_id, 0);
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Constant *I32Null = ConstantInt::getNullValue(Int32);
  Value *Args[] = {Ident, getOrCreateThreadID(Ident), I32Null};

  Builder.CreateCall(getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_taskyield),
                     Args);
}

void OpenMPIRBuilder::createTaskyield(const LocationDescription &Loc) {
  if (!updateToLocation(Loc))
    return;
  emitTaskyieldImpl(Loc);
}

// Processes the dependencies in Dependencies and does the following
// - Allocates space on the stack of an array of DependInfo objects
// - Populates each DependInfo object with relevant information of
//   the corresponding dependence.
// - All code is inserted in the entry block of the current function.
static Value *emitTaskDependencies(
    OpenMPIRBuilder &OMPBuilder,
    SmallVectorImpl<OpenMPIRBuilder::DependData> &Dependencies) {
  // Early return if we have no dependencies to process
  if (Dependencies.empty())
    return nullptr;

  // Given a vector of DependData objects, in this function we create an
  // array on the stack that holds kmp_dep_info objects corresponding
  // to each dependency. This is then passed to the OpenMP runtime.
  // For example, if there are 'n' dependencies then the following psedo
  // code is generated. Assume the first dependence is on a variable 'a'
  //
  // \code{c}
  // DepArray = alloc(n x sizeof(kmp_depend_info);
  // idx = 0;
  // DepArray[idx].base_addr = ptrtoint(&a);
  // DepArray[idx].len = 8;
  // DepArray[idx].flags = Dep.DepKind; /*(See OMPContants.h for DepKind)*/
  // ++idx;
  // DepArray[idx].base_addr = ...;
  // \endcode

  IRBuilderBase &Builder = OMPBuilder.Builder;
  Type *DependInfo = OMPBuilder.DependInfo;
  Module &M = OMPBuilder.M;

  Value *DepArray = nullptr;
  OpenMPIRBuilder::InsertPointTy OldIP = Builder.saveIP();
  Builder.SetInsertPoint(
      OldIP.getBlock()->getParent()->getEntryBlock().getTerminator());

  Type *DepArrayTy = ArrayType::get(DependInfo, Dependencies.size());
  DepArray = Builder.CreateAlloca(DepArrayTy, nullptr, ".dep.arr.addr");

  for (const auto &[DepIdx, Dep] : enumerate(Dependencies)) {
    Value *Base =
        Builder.CreateConstInBoundsGEP2_64(DepArrayTy, DepArray, 0, DepIdx);
    // Store the pointer to the variable
    Value *Addr = Builder.CreateStructGEP(
        DependInfo, Base,
        static_cast<unsigned int>(RTLDependInfoFields::BaseAddr));
    Value *DepValPtr = Builder.CreatePtrToInt(Dep.DepVal, Builder.getInt64Ty());
    Builder.CreateStore(DepValPtr, Addr);
    // Store the size of the variable
    Value *Size = Builder.CreateStructGEP(
        DependInfo, Base, static_cast<unsigned int>(RTLDependInfoFields::Len));
    Builder.CreateStore(
        Builder.getInt64(M.getDataLayout().getTypeStoreSize(Dep.DepValueType)),
        Size);
    // Store the dependency kind
    Value *Flags = Builder.CreateStructGEP(
        DependInfo, Base,
        static_cast<unsigned int>(RTLDependInfoFields::Flags));
    Builder.CreateStore(
        ConstantInt::get(Builder.getInt8Ty(),
                         static_cast<unsigned int>(Dep.DepKind)),
        Flags);
  }
  Builder.restoreIP(OldIP);
  return DepArray;
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createTask(const LocationDescription &Loc,
                            InsertPointTy AllocaIP, BodyGenCallbackTy BodyGenCB,
                            bool Tied, Value *Final, Value *IfCondition,
                            SmallVector<DependData> Dependencies) {

  if (!updateToLocation(Loc))
    return InsertPointTy();

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  // The current basic block is split into four basic blocks. After outlining,
  // they will be mapped as follows:
  // ```
  // def current_fn() {
  //   current_basic_block:
  //     br label %task.exit
  //   task.exit:
  //     ; instructions after task
  // }
  // def outlined_fn() {
  //   task.alloca:
  //     br label %task.body
  //   task.body:
  //     ret void
  // }
  // ```
  BasicBlock *TaskExitBB = splitBB(Builder, /*CreateBranch=*/true, "task.exit");
  BasicBlock *TaskBodyBB = splitBB(Builder, /*CreateBranch=*/true, "task.body");
  BasicBlock *TaskAllocaBB =
      splitBB(Builder, /*CreateBranch=*/true, "task.alloca");

  InsertPointTy TaskAllocaIP =
      InsertPointTy(TaskAllocaBB, TaskAllocaBB->begin());
  InsertPointTy TaskBodyIP = InsertPointTy(TaskBodyBB, TaskBodyBB->begin());
  BodyGenCB(TaskAllocaIP, TaskBodyIP);

  OutlineInfo OI;
  OI.EntryBB = TaskAllocaBB;
  OI.OuterAllocaBB = AllocaIP.getBlock();
  OI.ExitBB = TaskExitBB;

  // Add the thread ID argument.
  SmallVector<Instruction *, 4> ToBeDeleted;
  OI.ExcludeArgsFromAggregate.push_back(createFakeIntVal(
      Builder, AllocaIP, ToBeDeleted, TaskAllocaIP, "global.tid", false));

  OI.PostOutlineCB = [this, Ident, Tied, Final, IfCondition, Dependencies,
                      TaskAllocaBB, ToBeDeleted](Function &OutlinedFn) mutable {
    // Replace the Stale CI by appropriate RTL function call.
    assert(OutlinedFn.getNumUses() == 1 &&
           "there must be a single user for the outlined function");
    CallInst *StaleCI = cast<CallInst>(OutlinedFn.user_back());

    // HasShareds is true if any variables are captured in the outlined region,
    // false otherwise.
    bool HasShareds = StaleCI->arg_size() > 1;
    Builder.SetInsertPoint(StaleCI);

    // Gather the arguments for emitting the runtime call for
    // @__kmpc_omp_task_alloc
    Function *TaskAllocFn =
        getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_alloc);

    // Arguments - `loc_ref` (Ident) and `gtid` (ThreadID)
    // call.
    Value *ThreadID = getOrCreateThreadID(Ident);

    // Argument - `flags`
    // Task is tied iff (Flags & 1) == 1.
    // Task is untied iff (Flags & 1) == 0.
    // Task is final iff (Flags & 2) == 2.
    // Task is not final iff (Flags & 2) == 0.
    // TODO: Handle the other flags.
    Value *Flags = Builder.getInt32(Tied);
    if (Final) {
      Value *FinalFlag =
          Builder.CreateSelect(Final, Builder.getInt32(2), Builder.getInt32(0));
      Flags = Builder.CreateOr(FinalFlag, Flags);
    }

    // Argument - `sizeof_kmp_task_t` (TaskSize)
    // Tasksize refers to the size in bytes of kmp_task_t data structure
    // including private vars accessed in task.
    // TODO: add kmp_task_t_with_privates (privates)
    Value *TaskSize = Builder.getInt64(
        divideCeil(M.getDataLayout().getTypeSizeInBits(Task), 8));

    // Argument - `sizeof_shareds` (SharedsSize)
    // SharedsSize refers to the shareds array size in the kmp_task_t data
    // structure.
    Value *SharedsSize = Builder.getInt64(0);
    if (HasShareds) {
      AllocaInst *ArgStructAlloca =
          dyn_cast<AllocaInst>(StaleCI->getArgOperand(1));
      assert(ArgStructAlloca &&
             "Unable to find the alloca instruction corresponding to arguments "
             "for extracted function");
      StructType *ArgStructType =
          dyn_cast<StructType>(ArgStructAlloca->getAllocatedType());
      assert(ArgStructType && "Unable to find struct type corresponding to "
                              "arguments for extracted function");
      SharedsSize =
          Builder.getInt64(M.getDataLayout().getTypeStoreSize(ArgStructType));
    }
    // Emit the @__kmpc_omp_task_alloc runtime call
    // The runtime call returns a pointer to an area where the task captured
    // variables must be copied before the task is run (TaskData)
    CallInst *TaskData = Builder.CreateCall(
        TaskAllocFn, {/*loc_ref=*/Ident, /*gtid=*/ThreadID, /*flags=*/Flags,
                      /*sizeof_task=*/TaskSize, /*sizeof_shared=*/SharedsSize,
                      /*task_func=*/&OutlinedFn});

    // Copy the arguments for outlined function
    if (HasShareds) {
      Value *Shareds = StaleCI->getArgOperand(1);
      Align Alignment = TaskData->getPointerAlignment(M.getDataLayout());
      Value *TaskShareds = Builder.CreateLoad(VoidPtr, TaskData);
      Builder.CreateMemCpy(TaskShareds, Alignment, Shareds, Alignment,
                           SharedsSize);
    }

    Value *DepArray = nullptr;
    if (Dependencies.size()) {
      InsertPointTy OldIP = Builder.saveIP();
      Builder.SetInsertPoint(
          &OldIP.getBlock()->getParent()->getEntryBlock().back());

      Type *DepArrayTy = ArrayType::get(DependInfo, Dependencies.size());
      DepArray = Builder.CreateAlloca(DepArrayTy, nullptr, ".dep.arr.addr");

      unsigned P = 0;
      for (const DependData &Dep : Dependencies) {
        Value *Base =
            Builder.CreateConstInBoundsGEP2_64(DepArrayTy, DepArray, 0, P);
        // Store the pointer to the variable
        Value *Addr = Builder.CreateStructGEP(
            DependInfo, Base,
            static_cast<unsigned int>(RTLDependInfoFields::BaseAddr));
        Value *DepValPtr =
            Builder.CreatePtrToInt(Dep.DepVal, Builder.getInt64Ty());
        Builder.CreateStore(DepValPtr, Addr);
        // Store the size of the variable
        Value *Size = Builder.CreateStructGEP(
            DependInfo, Base,
            static_cast<unsigned int>(RTLDependInfoFields::Len));
        Builder.CreateStore(Builder.getInt64(M.getDataLayout().getTypeStoreSize(
                                Dep.DepValueType)),
                            Size);
        // Store the dependency kind
        Value *Flags = Builder.CreateStructGEP(
            DependInfo, Base,
            static_cast<unsigned int>(RTLDependInfoFields::Flags));
        Builder.CreateStore(
            ConstantInt::get(Builder.getInt8Ty(),
                             static_cast<unsigned int>(Dep.DepKind)),
            Flags);
        ++P;
      }

      Builder.restoreIP(OldIP);
    }

    // In the presence of the `if` clause, the following IR is generated:
    //    ...
    //    %data = call @__kmpc_omp_task_alloc(...)
    //    br i1 %if_condition, label %then, label %else
    //  then:
    //    call @__kmpc_omp_task(...)
    //    br label %exit
    //  else:
    //    ;; Wait for resolution of dependencies, if any, before
    //    ;; beginning the task
    //    call @__kmpc_omp_wait_deps(...)
    //    call @__kmpc_omp_task_begin_if0(...)
    //    call @outlined_fn(...)
    //    call @__kmpc_omp_task_complete_if0(...)
    //    br label %exit
    //  exit:
    //    ...
    if (IfCondition) {
      // `SplitBlockAndInsertIfThenElse` requires the block to have a
      // terminator.
      splitBB(Builder, /*CreateBranch=*/true, "if.end");
      Instruction *IfTerminator =
          Builder.GetInsertPoint()->getParent()->getTerminator();
      Instruction *ThenTI = IfTerminator, *ElseTI = nullptr;
      Builder.SetInsertPoint(IfTerminator);
      SplitBlockAndInsertIfThenElse(IfCondition, IfTerminator, &ThenTI,
                                    &ElseTI);
      Builder.SetInsertPoint(ElseTI);

      if (Dependencies.size()) {
        Function *TaskWaitFn =
            getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_wait_deps);
        Builder.CreateCall(
            TaskWaitFn,
            {Ident, ThreadID, Builder.getInt32(Dependencies.size()), DepArray,
             ConstantInt::get(Builder.getInt32Ty(), 0),
             ConstantPointerNull::get(PointerType::getUnqual(M.getContext()))});
      }
      Function *TaskBeginFn =
          getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_begin_if0);
      Function *TaskCompleteFn =
          getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_complete_if0);
      Builder.CreateCall(TaskBeginFn, {Ident, ThreadID, TaskData});
      CallInst *CI = nullptr;
      if (HasShareds)
        CI = Builder.CreateCall(&OutlinedFn, {ThreadID, TaskData});
      else
        CI = Builder.CreateCall(&OutlinedFn, {ThreadID});
      CI->setDebugLoc(StaleCI->getDebugLoc());
      Builder.CreateCall(TaskCompleteFn, {Ident, ThreadID, TaskData});
      Builder.SetInsertPoint(ThenTI);
    }

    if (Dependencies.size()) {
      Function *TaskFn =
          getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_with_deps);
      Builder.CreateCall(
          TaskFn,
          {Ident, ThreadID, TaskData, Builder.getInt32(Dependencies.size()),
           DepArray, ConstantInt::get(Builder.getInt32Ty(), 0),
           ConstantPointerNull::get(PointerType::getUnqual(M.getContext()))});

    } else {
      // Emit the @__kmpc_omp_task runtime call to spawn the task
      Function *TaskFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task);
      Builder.CreateCall(TaskFn, {Ident, ThreadID, TaskData});
    }

    StaleCI->eraseFromParent();

    Builder.SetInsertPoint(TaskAllocaBB, TaskAllocaBB->begin());
    if (HasShareds) {
      LoadInst *Shareds = Builder.CreateLoad(VoidPtr, OutlinedFn.getArg(1));
      OutlinedFn.getArg(1)->replaceUsesWithIf(
          Shareds, [Shareds](Use &U) { return U.getUser() != Shareds; });
    }

    llvm::for_each(llvm::reverse(ToBeDeleted),
                   [](Instruction *I) { I->eraseFromParent(); });
  };

  addOutlineInfo(std::move(OI));
  Builder.SetInsertPoint(TaskExitBB, TaskExitBB->begin());

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createTaskgroup(const LocationDescription &Loc,
                                 InsertPointTy AllocaIP,
                                 BodyGenCallbackTy BodyGenCB) {
  if (!updateToLocation(Loc))
    return InsertPointTy();

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadID = getOrCreateThreadID(Ident);

  // Emit the @__kmpc_taskgroup runtime call to start the taskgroup
  Function *TaskgroupFn =
      getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_taskgroup);
  Builder.CreateCall(TaskgroupFn, {Ident, ThreadID});

  BasicBlock *TaskgroupExitBB = splitBB(Builder, true, "taskgroup.exit");
  BodyGenCB(AllocaIP, Builder.saveIP());

  Builder.SetInsertPoint(TaskgroupExitBB);
  // Emit the @__kmpc_end_taskgroup runtime call to end the taskgroup
  Function *EndTaskgroupFn =
      getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_end_taskgroup);
  Builder.CreateCall(EndTaskgroupFn, {Ident, ThreadID});

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createSections(
    const LocationDescription &Loc, InsertPointTy AllocaIP,
    ArrayRef<StorableBodyGenCallbackTy> SectionCBs, PrivatizeCallbackTy PrivCB,
    FinalizeCallbackTy FiniCB, bool IsCancellable, bool IsNowait) {
  assert(!isConflictIP(AllocaIP, Loc.IP) && "Dedicated IP allocas required");

  if (!updateToLocation(Loc))
    return Loc.IP;

  auto FiniCBWrapper = [&](InsertPointTy IP) {
    if (IP.getBlock()->end() != IP.getPoint())
      return FiniCB(IP);
    // This must be done otherwise any nested constructs using FinalizeOMPRegion
    // will fail because that function requires the Finalization Basic Block to
    // have a terminator, which is already removed by EmitOMPRegionBody.
    // IP is currently at cancelation block.
    // We need to backtrack to the condition block to fetch
    // the exit block and create a branch from cancelation
    // to exit block.
    IRBuilder<>::InsertPointGuard IPG(Builder);
    Builder.restoreIP(IP);
    auto *CaseBB = IP.getBlock()->getSinglePredecessor();
    auto *CondBB = CaseBB->getSinglePredecessor()->getSinglePredecessor();
    auto *ExitBB = CondBB->getTerminator()->getSuccessor(1);
    Instruction *I = Builder.CreateBr(ExitBB);
    IP = InsertPointTy(I->getParent(), I->getIterator());
    return FiniCB(IP);
  };

  FinalizationStack.push_back({FiniCBWrapper, OMPD_sections, IsCancellable});

  // Each section is emitted as a switch case
  // Each finalization callback is handled from clang.EmitOMPSectionDirective()
  // -> OMP.createSection() which generates the IR for each section
  // Iterate through all sections and emit a switch construct:
  // switch (IV) {
  //   case 0:
  //     <SectionStmt[0]>;
  //     break;
  // ...
  //   case <NumSection> - 1:
  //     <SectionStmt[<NumSection> - 1]>;
  //     break;
  // }
  // ...
  // section_loop.after:
  // <FiniCB>;
  auto LoopBodyGenCB = [&](InsertPointTy CodeGenIP, Value *IndVar) {
    Builder.restoreIP(CodeGenIP);
    BasicBlock *Continue =
        splitBBWithSuffix(Builder, /*CreateBranch=*/false, ".sections.after");
    Function *CurFn = Continue->getParent();
    SwitchInst *SwitchStmt = Builder.CreateSwitch(IndVar, Continue);

    unsigned CaseNumber = 0;
    for (auto SectionCB : SectionCBs) {
      BasicBlock *CaseBB = BasicBlock::Create(
          M.getContext(), "omp_section_loop.body.case", CurFn, Continue);
      SwitchStmt->addCase(Builder.getInt32(CaseNumber), CaseBB);
      Builder.SetInsertPoint(CaseBB);
      BranchInst *CaseEndBr = Builder.CreateBr(Continue);
      SectionCB(InsertPointTy(),
                {CaseEndBr->getParent(), CaseEndBr->getIterator()});
      CaseNumber++;
    }
    // remove the existing terminator from body BB since there can be no
    // terminators after switch/case
  };
  // Loop body ends here
  // LowerBound, UpperBound, and STride for createCanonicalLoop
  Type *I32Ty = Type::getInt32Ty(M.getContext());
  Value *LB = ConstantInt::get(I32Ty, 0);
  Value *UB = ConstantInt::get(I32Ty, SectionCBs.size());
  Value *ST = ConstantInt::get(I32Ty, 1);
  llvm::CanonicalLoopInfo *LoopInfo = createCanonicalLoop(
      Loc, LoopBodyGenCB, LB, UB, ST, true, false, AllocaIP, "section_loop");
  InsertPointTy AfterIP =
      applyStaticWorkshareLoop(Loc.DL, LoopInfo, AllocaIP, !IsNowait);

  // Apply the finalization callback in LoopAfterBB
  auto FiniInfo = FinalizationStack.pop_back_val();
  assert(FiniInfo.DK == OMPD_sections &&
         "Unexpected finalization stack state!");
  if (FinalizeCallbackTy &CB = FiniInfo.FiniCB) {
    Builder.restoreIP(AfterIP);
    BasicBlock *FiniBB =
        splitBBWithSuffix(Builder, /*CreateBranch=*/true, "sections.fini");
    CB(Builder.saveIP());
    AfterIP = {FiniBB, FiniBB->begin()};
  }

  return AfterIP;
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createSection(const LocationDescription &Loc,
                               BodyGenCallbackTy BodyGenCB,
                               FinalizeCallbackTy FiniCB) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  auto FiniCBWrapper = [&](InsertPointTy IP) {
    if (IP.getBlock()->end() != IP.getPoint())
      return FiniCB(IP);
    // This must be done otherwise any nested constructs using FinalizeOMPRegion
    // will fail because that function requires the Finalization Basic Block to
    // have a terminator, which is already removed by EmitOMPRegionBody.
    // IP is currently at cancelation block.
    // We need to backtrack to the condition block to fetch
    // the exit block and create a branch from cancelation
    // to exit block.
    IRBuilder<>::InsertPointGuard IPG(Builder);
    Builder.restoreIP(IP);
    auto *CaseBB = Loc.IP.getBlock();
    auto *CondBB = CaseBB->getSinglePredecessor()->getSinglePredecessor();
    auto *ExitBB = CondBB->getTerminator()->getSuccessor(1);
    Instruction *I = Builder.CreateBr(ExitBB);
    IP = InsertPointTy(I->getParent(), I->getIterator());
    return FiniCB(IP);
  };

  Directive OMPD = Directive::OMPD_sections;
  // Since we are using Finalization Callback here, HasFinalize
  // and IsCancellable have to be true
  return EmitOMPInlinedRegion(OMPD, nullptr, nullptr, BodyGenCB, FiniCBWrapper,
                              /*Conditional*/ false, /*hasFinalize*/ true,
                              /*IsCancellable*/ true);
}

static OpenMPIRBuilder::InsertPointTy getInsertPointAfterInstr(Instruction *I) {
  BasicBlock::iterator IT(I);
  IT++;
  return OpenMPIRBuilder::InsertPointTy(I->getParent(), IT);
}

void OpenMPIRBuilder::emitUsed(StringRef Name,
                               std::vector<WeakTrackingVH> &List) {
  if (List.empty())
    return;

  // Convert List to what ConstantArray needs.
  SmallVector<Constant *, 8> UsedArray;
  UsedArray.resize(List.size());
  for (unsigned I = 0, E = List.size(); I != E; ++I)
    UsedArray[I] = ConstantExpr::getPointerBitCastOrAddrSpaceCast(
        cast<Constant>(&*List[I]), Builder.getPtrTy());

  if (UsedArray.empty())
    return;
  ArrayType *ATy = ArrayType::get(Builder.getPtrTy(), UsedArray.size());

  auto *GV = new GlobalVariable(M, ATy, false, GlobalValue::AppendingLinkage,
                                ConstantArray::get(ATy, UsedArray), Name);

  GV->setSection("llvm.metadata");
}

Value *OpenMPIRBuilder::getGPUThreadID() {
  return Builder.CreateCall(
      getOrCreateRuntimeFunction(M,
                                 OMPRTL___kmpc_get_hardware_thread_id_in_block),
      {});
}

Value *OpenMPIRBuilder::getGPUWarpSize() {
  return Builder.CreateCall(
      getOrCreateRuntimeFunction(M, OMPRTL___kmpc_get_warp_size), {});
}

Value *OpenMPIRBuilder::getNVPTXWarpID() {
  unsigned LaneIDBits = Log2_32(Config.getGridValue().GV_Warp_Size);
  return Builder.CreateAShr(getGPUThreadID(), LaneIDBits, "nvptx_warp_id");
}

Value *OpenMPIRBuilder::getNVPTXLaneID() {
  unsigned LaneIDBits = Log2_32(Config.getGridValue().GV_Warp_Size);
  assert(LaneIDBits < 32 && "Invalid LaneIDBits size in NVPTX device.");
  unsigned LaneIDMask = ~0u >> (32u - LaneIDBits);
  return Builder.CreateAnd(getGPUThreadID(), Builder.getInt32(LaneIDMask),
                           "nvptx_lane_id");
}

Value *OpenMPIRBuilder::castValueToType(InsertPointTy AllocaIP, Value *From,
                                        Type *ToType) {
  Type *FromType = From->getType();
  uint64_t FromSize = M.getDataLayout().getTypeStoreSize(FromType);
  uint64_t ToSize = M.getDataLayout().getTypeStoreSize(ToType);
  assert(FromSize > 0 && "From size must be greater than zero");
  assert(ToSize > 0 && "To size must be greater than zero");
  if (FromType == ToType)
    return From;
  if (FromSize == ToSize)
    return Builder.CreateBitCast(From, ToType);
  if (ToType->isIntegerTy() && FromType->isIntegerTy())
    return Builder.CreateIntCast(From, ToType, /*isSigned*/ true);
  InsertPointTy SaveIP = Builder.saveIP();
  Builder.restoreIP(AllocaIP);
  Value *CastItem = Builder.CreateAlloca(ToType);
  Builder.restoreIP(SaveIP);

  Value *ValCastItem = Builder.CreatePointerBitCastOrAddrSpaceCast(
      CastItem, FromType->getPointerTo());
  Builder.CreateStore(From, ValCastItem);
  return Builder.CreateLoad(ToType, CastItem);
}

Value *OpenMPIRBuilder::createRuntimeShuffleFunction(InsertPointTy AllocaIP,
                                                     Value *Element,
                                                     Type *ElementType,
                                                     Value *Offset) {
  uint64_t Size = M.getDataLayout().getTypeStoreSize(ElementType);
  assert(Size <= 8 && "Unsupported bitwidth in shuffle instruction");

  // Cast all types to 32- or 64-bit values before calling shuffle routines.
  Type *CastTy = Builder.getIntNTy(Size <= 4 ? 32 : 64);
  Value *ElemCast = castValueToType(AllocaIP, Element, CastTy);
  Value *WarpSize =
      Builder.CreateIntCast(getGPUWarpSize(), Builder.getInt16Ty(), true);
  Function *ShuffleFunc = getOrCreateRuntimeFunctionPtr(
      Size <= 4 ? RuntimeFunction::OMPRTL___kmpc_shuffle_int32
                : RuntimeFunction::OMPRTL___kmpc_shuffle_int64);
  Value *WarpSizeCast =
      Builder.CreateIntCast(WarpSize, Builder.getInt16Ty(), /*isSigned=*/true);
  Value *ShuffleCall =
      Builder.CreateCall(ShuffleFunc, {ElemCast, Offset, WarpSizeCast});
  return castValueToType(AllocaIP, ShuffleCall, CastTy);
}

void OpenMPIRBuilder::shuffleAndStore(InsertPointTy AllocaIP, Value *SrcAddr,
                                      Value *DstAddr, Type *ElemType,
                                      Value *Offset, Type *ReductionArrayTy) {
  uint64_t Size = M.getDataLayout().getTypeStoreSize(ElemType);
  // Create the loop over the big sized data.
  // ptr = (void*)Elem;
  // ptrEnd = (void*) Elem + 1;
  // Step = 8;
  // while (ptr + Step < ptrEnd)
  //   shuffle((int64_t)*ptr);
  // Step = 4;
  // while (ptr + Step < ptrEnd)
  //   shuffle((int32_t)*ptr);
  // ...
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  Value *ElemPtr = DstAddr;
  Value *Ptr = SrcAddr;
  for (unsigned IntSize = 8; IntSize >= 1; IntSize /= 2) {
    if (Size < IntSize)
      continue;
    Type *IntType = Builder.getIntNTy(IntSize * 8);
    Ptr = Builder.CreatePointerBitCastOrAddrSpaceCast(
        Ptr, IntType->getPointerTo(), Ptr->getName() + ".ascast");
    Value *SrcAddrGEP =
        Builder.CreateGEP(ElemType, SrcAddr, {ConstantInt::get(IndexTy, 1)});
    ElemPtr = Builder.CreatePointerBitCastOrAddrSpaceCast(
        ElemPtr, IntType->getPointerTo(), ElemPtr->getName() + ".ascast");

    Function *CurFunc = Builder.GetInsertBlock()->getParent();
    if ((Size / IntSize) > 1) {
      Value *PtrEnd = Builder.CreatePointerBitCastOrAddrSpaceCast(
          SrcAddrGEP, Builder.getPtrTy());
      BasicBlock *PreCondBB =
          BasicBlock::Create(M.getContext(), ".shuffle.pre_cond");
      BasicBlock *ThenBB = BasicBlock::Create(M.getContext(), ".shuffle.then");
      BasicBlock *ExitBB = BasicBlock::Create(M.getContext(), ".shuffle.exit");
      BasicBlock *CurrentBB = Builder.GetInsertBlock();
      emitBlock(PreCondBB, CurFunc);
      PHINode *PhiSrc =
          Builder.CreatePHI(Ptr->getType(), /*NumReservedValues=*/2);
      PhiSrc->addIncoming(Ptr, CurrentBB);
      PHINode *PhiDest =
          Builder.CreatePHI(ElemPtr->getType(), /*NumReservedValues=*/2);
      PhiDest->addIncoming(ElemPtr, CurrentBB);
      Ptr = PhiSrc;
      ElemPtr = PhiDest;
      Value *PtrDiff = Builder.CreatePtrDiff(
          Builder.getInt8Ty(), PtrEnd,
          Builder.CreatePointerBitCastOrAddrSpaceCast(Ptr, Builder.getPtrTy()));
      Builder.CreateCondBr(
          Builder.CreateICmpSGT(PtrDiff, Builder.getInt64(IntSize - 1)), ThenBB,
          ExitBB);
      emitBlock(ThenBB, CurFunc);
      Value *Res = createRuntimeShuffleFunction(
          AllocaIP,
          Builder.CreateAlignedLoad(
              IntType, Ptr, M.getDataLayout().getPrefTypeAlign(ElemType)),
          IntType, Offset);
      Builder.CreateAlignedStore(Res, ElemPtr,
                                 M.getDataLayout().getPrefTypeAlign(ElemType));
      Value *LocalPtr =
          Builder.CreateGEP(IntType, Ptr, {ConstantInt::get(IndexTy, 1)});
      Value *LocalElemPtr =
          Builder.CreateGEP(IntType, ElemPtr, {ConstantInt::get(IndexTy, 1)});
      PhiSrc->addIncoming(LocalPtr, ThenBB);
      PhiDest->addIncoming(LocalElemPtr, ThenBB);
      emitBranch(PreCondBB);
      emitBlock(ExitBB, CurFunc);
    } else {
      Value *Res = createRuntimeShuffleFunction(
          AllocaIP, Builder.CreateLoad(IntType, Ptr), IntType, Offset);
      if (ElemType->isIntegerTy() && ElemType->getScalarSizeInBits() <
                                         Res->getType()->getScalarSizeInBits())
        Res = Builder.CreateTrunc(Res, ElemType);
      Builder.CreateStore(Res, ElemPtr);
      Ptr = Builder.CreateGEP(IntType, Ptr, {ConstantInt::get(IndexTy, 1)});
      ElemPtr =
          Builder.CreateGEP(IntType, ElemPtr, {ConstantInt::get(IndexTy, 1)});
    }
    Size = Size % IntSize;
  }
}

void OpenMPIRBuilder::emitReductionListCopy(
    InsertPointTy AllocaIP, CopyAction Action, Type *ReductionArrayTy,
    ArrayRef<ReductionInfo> ReductionInfos, Value *SrcBase, Value *DestBase,
    CopyOptionsTy CopyOptions) {
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  Value *RemoteLaneOffset = CopyOptions.RemoteLaneOffset;

  // Iterates, element-by-element, through the source Reduce list and
  // make a copy.
  for (auto En : enumerate(ReductionInfos)) {
    const ReductionInfo &RI = En.value();
    Value *SrcElementAddr = nullptr;
    Value *DestElementAddr = nullptr;
    Value *DestElementPtrAddr = nullptr;
    // Should we shuffle in an element from a remote lane?
    bool ShuffleInElement = false;
    // Set to true to update the pointer in the dest Reduce list to a
    // newly created element.
    bool UpdateDestListPtr = false;

    // Step 1.1: Get the address for the src element in the Reduce list.
    Value *SrcElementPtrAddr = Builder.CreateInBoundsGEP(
        ReductionArrayTy, SrcBase,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    SrcElementAddr = Builder.CreateLoad(Builder.getPtrTy(), SrcElementPtrAddr);

    // Step 1.2: Create a temporary to store the element in the destination
    // Reduce list.
    DestElementPtrAddr = Builder.CreateInBoundsGEP(
        ReductionArrayTy, DestBase,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    switch (Action) {
    case CopyAction::RemoteLaneToThread: {
      InsertPointTy CurIP = Builder.saveIP();
      Builder.restoreIP(AllocaIP);
      AllocaInst *DestAlloca = Builder.CreateAlloca(RI.ElementType, nullptr,
                                                    ".omp.reduction.element");
      DestAlloca->setAlignment(
          M.getDataLayout().getPrefTypeAlign(RI.ElementType));
      DestElementAddr = DestAlloca;
      DestElementAddr =
          Builder.CreateAddrSpaceCast(DestElementAddr, Builder.getPtrTy(),
                                      DestElementAddr->getName() + ".ascast");
      Builder.restoreIP(CurIP);
      ShuffleInElement = true;
      UpdateDestListPtr = true;
      break;
    }
    case CopyAction::ThreadCopy: {
      DestElementAddr =
          Builder.CreateLoad(Builder.getPtrTy(), DestElementPtrAddr);
      break;
    }
    }

    // Now that all active lanes have read the element in the
    // Reduce list, shuffle over the value from the remote lane.
    if (ShuffleInElement) {
      shuffleAndStore(AllocaIP, SrcElementAddr, DestElementAddr, RI.ElementType,
                      RemoteLaneOffset, ReductionArrayTy);
    } else {
      switch (RI.EvaluationKind) {
      case EvalKind::Scalar: {
        Value *Elem = Builder.CreateLoad(RI.ElementType, SrcElementAddr);
        // Store the source element value to the dest element address.
        Builder.CreateStore(Elem, DestElementAddr);
        break;
      }
      case EvalKind::Complex: {
        Value *SrcRealPtr = Builder.CreateConstInBoundsGEP2_32(
            RI.ElementType, SrcElementAddr, 0, 0, ".realp");
        Value *SrcReal = Builder.CreateLoad(
            RI.ElementType->getStructElementType(0), SrcRealPtr, ".real");
        Value *SrcImgPtr = Builder.CreateConstInBoundsGEP2_32(
            RI.ElementType, SrcElementAddr, 0, 1, ".imagp");
        Value *SrcImg = Builder.CreateLoad(
            RI.ElementType->getStructElementType(1), SrcImgPtr, ".imag");

        Value *DestRealPtr = Builder.CreateConstInBoundsGEP2_32(
            RI.ElementType, DestElementAddr, 0, 0, ".realp");
        Value *DestImgPtr = Builder.CreateConstInBoundsGEP2_32(
            RI.ElementType, DestElementAddr, 0, 1, ".imagp");
        Builder.CreateStore(SrcReal, DestRealPtr);
        Builder.CreateStore(SrcImg, DestImgPtr);
        break;
      }
      case EvalKind::Aggregate: {
        Value *SizeVal = Builder.getInt64(
            M.getDataLayout().getTypeStoreSize(RI.ElementType));
        Builder.CreateMemCpy(
            DestElementAddr, M.getDataLayout().getPrefTypeAlign(RI.ElementType),
            SrcElementAddr, M.getDataLayout().getPrefTypeAlign(RI.ElementType),
            SizeVal, false);
        break;
      }
      };
    }

    // Step 3.1: Modify reference in dest Reduce list as needed.
    // Modifying the reference in Reduce list to point to the newly
    // created element.  The element is live in the current function
    // scope and that of functions it invokes (i.e., reduce_function).
    // RemoteReduceData[i] = (void*)&RemoteElem
    if (UpdateDestListPtr) {
      Value *CastDestAddr = Builder.CreatePointerBitCastOrAddrSpaceCast(
          DestElementAddr, Builder.getPtrTy(),
          DestElementAddr->getName() + ".ascast");
      Builder.CreateStore(CastDestAddr, DestElementPtrAddr);
    }
  }
}

Function *OpenMPIRBuilder::emitInterWarpCopyFunction(
    const LocationDescription &Loc, ArrayRef<ReductionInfo> ReductionInfos,
    AttributeList FuncAttrs) {
  InsertPointTy SavedIP = Builder.saveIP();
  LLVMContext &Ctx = M.getContext();
  FunctionType *FuncTy = FunctionType::get(
      Builder.getVoidTy(), {Builder.getPtrTy(), Builder.getInt32Ty()},
      /* IsVarArg */ false);
  Function *WcFunc =
      Function::Create(FuncTy, GlobalVariable::InternalLinkage,
                       "_omp_reduction_inter_warp_copy_func", &M);
  WcFunc->setAttributes(FuncAttrs);
  WcFunc->addParamAttr(0, Attribute::NoUndef);
  WcFunc->addParamAttr(1, Attribute::NoUndef);
  BasicBlock *EntryBB = BasicBlock::Create(M.getContext(), "entry", WcFunc);
  Builder.SetInsertPoint(EntryBB);

  // ReduceList: thread local Reduce list.
  // At the stage of the computation when this function is called, partially
  // aggregated values reside in the first lane of every active warp.
  Argument *ReduceListArg = WcFunc->getArg(0);
  // NumWarps: number of warps active in the parallel region.  This could
  // be smaller than 32 (max warps in a CTA) for partial block reduction.
  Argument *NumWarpsArg = WcFunc->getArg(1);

  // This array is used as a medium to transfer, one reduce element at a time,
  // the data from the first lane of every warp to lanes in the first warp
  // in order to perform the final step of a reduction in a parallel region
  // (reduction across warps).  The array is placed in NVPTX __shared__ memory
  // for reduced latency, as well as to have a distinct copy for concurrently
  // executing target regions.  The array is declared with common linkage so
  // as to be shared across compilation units.
  StringRef TransferMediumName =
      "__openmp_nvptx_data_transfer_temporary_storage";
  GlobalVariable *TransferMedium = M.getGlobalVariable(TransferMediumName);
  unsigned WarpSize = Config.getGridValue().GV_Warp_Size;
  ArrayType *ArrayTy = ArrayType::get(Builder.getInt32Ty(), WarpSize);
  if (!TransferMedium) {
    TransferMedium = new GlobalVariable(
        M, ArrayTy, /*isConstant=*/false, GlobalVariable::WeakAnyLinkage,
        UndefValue::get(ArrayTy), TransferMediumName,
        /*InsertBefore=*/nullptr, GlobalVariable::NotThreadLocal,
        /*AddressSpace=*/3);
  }

  // Get the CUDA thread id of the current OpenMP thread on the GPU.
  Value *GPUThreadID = getGPUThreadID();
  // nvptx_lane_id = nvptx_id % warpsize
  Value *LaneID = getNVPTXLaneID();
  // nvptx_warp_id = nvptx_id / warpsize
  Value *WarpID = getNVPTXWarpID();

  InsertPointTy AllocaIP =
      InsertPointTy(Builder.GetInsertBlock(),
                    Builder.GetInsertBlock()->getFirstInsertionPt());
  Type *Arg0Type = ReduceListArg->getType();
  Type *Arg1Type = NumWarpsArg->getType();
  Builder.restoreIP(AllocaIP);
  AllocaInst *ReduceListAlloca = Builder.CreateAlloca(
      Arg0Type, nullptr, ReduceListArg->getName() + ".addr");
  AllocaInst *NumWarpsAlloca =
      Builder.CreateAlloca(Arg1Type, nullptr, NumWarpsArg->getName() + ".addr");
  Value *ReduceListAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReduceListAlloca, Arg0Type, ReduceListAlloca->getName() + ".ascast");
  Value *NumWarpsAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      NumWarpsAlloca, Arg1Type->getPointerTo(),
      NumWarpsAlloca->getName() + ".ascast");
  Builder.CreateStore(ReduceListArg, ReduceListAddrCast);
  Builder.CreateStore(NumWarpsArg, NumWarpsAddrCast);
  AllocaIP = getInsertPointAfterInstr(NumWarpsAlloca);
  InsertPointTy CodeGenIP =
      getInsertPointAfterInstr(&Builder.GetInsertBlock()->back());
  Builder.restoreIP(CodeGenIP);

  Value *ReduceList =
      Builder.CreateLoad(Builder.getPtrTy(), ReduceListAddrCast);

  for (auto En : enumerate(ReductionInfos)) {
    //
    // Warp master copies reduce element to transfer medium in __shared__
    // memory.
    //
    const ReductionInfo &RI = En.value();
    unsigned RealTySize = M.getDataLayout().getTypeAllocSize(RI.ElementType);
    for (unsigned TySize = 4; TySize > 0 && RealTySize > 0; TySize /= 2) {
      Type *CType = Builder.getIntNTy(TySize * 8);

      unsigned NumIters = RealTySize / TySize;
      if (NumIters == 0)
        continue;
      Value *Cnt = nullptr;
      Value *CntAddr = nullptr;
      BasicBlock *PrecondBB = nullptr;
      BasicBlock *ExitBB = nullptr;
      if (NumIters > 1) {
        CodeGenIP = Builder.saveIP();
        Builder.restoreIP(AllocaIP);
        CntAddr =
            Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, ".cnt.addr");

        CntAddr = Builder.CreateAddrSpaceCast(CntAddr, Builder.getPtrTy(),
                                              CntAddr->getName() + ".ascast");
        Builder.restoreIP(CodeGenIP);
        Builder.CreateStore(Constant::getNullValue(Builder.getInt32Ty()),
                            CntAddr,
                            /*Volatile=*/false);
        PrecondBB = BasicBlock::Create(Ctx, "precond");
        ExitBB = BasicBlock::Create(Ctx, "exit");
        BasicBlock *BodyBB = BasicBlock::Create(Ctx, "body");
        emitBlock(PrecondBB, Builder.GetInsertBlock()->getParent());
        Cnt = Builder.CreateLoad(Builder.getInt32Ty(), CntAddr,
                                 /*Volatile=*/false);
        Value *Cmp = Builder.CreateICmpULT(
            Cnt, ConstantInt::get(Builder.getInt32Ty(), NumIters));
        Builder.CreateCondBr(Cmp, BodyBB, ExitBB);
        emitBlock(BodyBB, Builder.GetInsertBlock()->getParent());
      }

      // kmpc_barrier.
      createBarrier(LocationDescription(Builder.saveIP(), Loc.DL),
                    omp::Directive::OMPD_unknown,
                    /* ForceSimpleCall */ false,
                    /* CheckCancelFlag */ true);
      BasicBlock *ThenBB = BasicBlock::Create(Ctx, "then");
      BasicBlock *ElseBB = BasicBlock::Create(Ctx, "else");
      BasicBlock *MergeBB = BasicBlock::Create(Ctx, "ifcont");

      // if (lane_id  == 0)
      Value *IsWarpMaster = Builder.CreateIsNull(LaneID, "warp_master");
      Builder.CreateCondBr(IsWarpMaster, ThenBB, ElseBB);
      emitBlock(ThenBB, Builder.GetInsertBlock()->getParent());

      // Reduce element = LocalReduceList[i]
      auto *RedListArrayTy =
          ArrayType::get(Builder.getPtrTy(), ReductionInfos.size());
      Type *IndexTy = Builder.getIndexTy(
          M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
      Value *ElemPtrPtr =
          Builder.CreateInBoundsGEP(RedListArrayTy, ReduceList,
                                    {ConstantInt::get(IndexTy, 0),
                                     ConstantInt::get(IndexTy, En.index())});
      // elemptr = ((CopyType*)(elemptrptr)) + I
      Value *ElemPtr = Builder.CreateLoad(Builder.getPtrTy(), ElemPtrPtr);
      if (NumIters > 1)
        ElemPtr = Builder.CreateGEP(Builder.getInt32Ty(), ElemPtr, Cnt);

      // Get pointer to location in transfer medium.
      // MediumPtr = &medium[warp_id]
      Value *MediumPtr = Builder.CreateInBoundsGEP(
          ArrayTy, TransferMedium, {Builder.getInt64(0), WarpID});
      // elem = *elemptr
      //*MediumPtr = elem
      Value *Elem = Builder.CreateLoad(CType, ElemPtr);
      // Store the source element value to the dest element address.
      Builder.CreateStore(Elem, MediumPtr,
                          /*IsVolatile*/ true);
      Builder.CreateBr(MergeBB);

      // else
      emitBlock(ElseBB, Builder.GetInsertBlock()->getParent());
      Builder.CreateBr(MergeBB);

      // endif
      emitBlock(MergeBB, Builder.GetInsertBlock()->getParent());
      createBarrier(LocationDescription(Builder.saveIP(), Loc.DL),
                    omp::Directive::OMPD_unknown,
                    /* ForceSimpleCall */ false,
                    /* CheckCancelFlag */ true);

      // Warp 0 copies reduce element from transfer medium
      BasicBlock *W0ThenBB = BasicBlock::Create(Ctx, "then");
      BasicBlock *W0ElseBB = BasicBlock::Create(Ctx, "else");
      BasicBlock *W0MergeBB = BasicBlock::Create(Ctx, "ifcont");

      Value *NumWarpsVal =
          Builder.CreateLoad(Builder.getInt32Ty(), NumWarpsAddrCast);
      // Up to 32 threads in warp 0 are active.
      Value *IsActiveThread =
          Builder.CreateICmpULT(GPUThreadID, NumWarpsVal, "is_active_thread");
      Builder.CreateCondBr(IsActiveThread, W0ThenBB, W0ElseBB);

      emitBlock(W0ThenBB, Builder.GetInsertBlock()->getParent());

      // SecMediumPtr = &medium[tid]
      // SrcMediumVal = *SrcMediumPtr
      Value *SrcMediumPtrVal = Builder.CreateInBoundsGEP(
          ArrayTy, TransferMedium, {Builder.getInt64(0), GPUThreadID});
      // TargetElemPtr = (CopyType*)(SrcDataAddr[i]) + I
      Value *TargetElemPtrPtr =
          Builder.CreateInBoundsGEP(RedListArrayTy, ReduceList,
                                    {ConstantInt::get(IndexTy, 0),
                                     ConstantInt::get(IndexTy, En.index())});
      Value *TargetElemPtrVal =
          Builder.CreateLoad(Builder.getPtrTy(), TargetElemPtrPtr);
      Value *TargetElemPtr = TargetElemPtrVal;
      if (NumIters > 1)
        TargetElemPtr =
            Builder.CreateGEP(Builder.getInt32Ty(), TargetElemPtr, Cnt);

      // *TargetElemPtr = SrcMediumVal;
      Value *SrcMediumValue =
          Builder.CreateLoad(CType, SrcMediumPtrVal, /*IsVolatile*/ true);
      Builder.CreateStore(SrcMediumValue, TargetElemPtr);
      Builder.CreateBr(W0MergeBB);

      emitBlock(W0ElseBB, Builder.GetInsertBlock()->getParent());
      Builder.CreateBr(W0MergeBB);

      emitBlock(W0MergeBB, Builder.GetInsertBlock()->getParent());

      if (NumIters > 1) {
        Cnt = Builder.CreateNSWAdd(
            Cnt, ConstantInt::get(Builder.getInt32Ty(), /*V=*/1));
        Builder.CreateStore(Cnt, CntAddr, /*Volatile=*/false);

        auto *CurFn = Builder.GetInsertBlock()->getParent();
        emitBranch(PrecondBB);
        emitBlock(ExitBB, CurFn);
      }
      RealTySize %= TySize;
    }
  }

  Builder.CreateRetVoid();
  Builder.restoreIP(SavedIP);

  return WcFunc;
}

Function *OpenMPIRBuilder::emitShuffleAndReduceFunction(
    ArrayRef<ReductionInfo> ReductionInfos, Function *ReduceFn,
    AttributeList FuncAttrs) {
  LLVMContext &Ctx = M.getContext();
  FunctionType *FuncTy =
      FunctionType::get(Builder.getVoidTy(),
                        {Builder.getPtrTy(), Builder.getInt16Ty(),
                         Builder.getInt16Ty(), Builder.getInt16Ty()},
                        /* IsVarArg */ false);
  Function *SarFunc =
      Function::Create(FuncTy, GlobalVariable::InternalLinkage,
                       "_omp_reduction_shuffle_and_reduce_func", &M);
  SarFunc->setAttributes(FuncAttrs);
  SarFunc->addParamAttr(0, Attribute::NoUndef);
  SarFunc->addParamAttr(1, Attribute::NoUndef);
  SarFunc->addParamAttr(2, Attribute::NoUndef);
  SarFunc->addParamAttr(3, Attribute::NoUndef);
  SarFunc->addParamAttr(1, Attribute::SExt);
  SarFunc->addParamAttr(2, Attribute::SExt);
  SarFunc->addParamAttr(3, Attribute::SExt);
  BasicBlock *EntryBB = BasicBlock::Create(M.getContext(), "entry", SarFunc);
  Builder.SetInsertPoint(EntryBB);

  // Thread local Reduce list used to host the values of data to be reduced.
  Argument *ReduceListArg = SarFunc->getArg(0);
  // Current lane id; could be logical.
  Argument *LaneIDArg = SarFunc->getArg(1);
  // Offset of the remote source lane relative to the current lane.
  Argument *RemoteLaneOffsetArg = SarFunc->getArg(2);
  // Algorithm version.  This is expected to be known at compile time.
  Argument *AlgoVerArg = SarFunc->getArg(3);

  Type *ReduceListArgType = ReduceListArg->getType();
  Type *LaneIDArgType = LaneIDArg->getType();
  Type *LaneIDArgPtrType = LaneIDArg->getType()->getPointerTo();
  Value *ReduceListAlloca = Builder.CreateAlloca(
      ReduceListArgType, nullptr, ReduceListArg->getName() + ".addr");
  Value *LaneIdAlloca = Builder.CreateAlloca(LaneIDArgType, nullptr,
                                             LaneIDArg->getName() + ".addr");
  Value *RemoteLaneOffsetAlloca = Builder.CreateAlloca(
      LaneIDArgType, nullptr, RemoteLaneOffsetArg->getName() + ".addr");
  Value *AlgoVerAlloca = Builder.CreateAlloca(LaneIDArgType, nullptr,
                                              AlgoVerArg->getName() + ".addr");
  ArrayType *RedListArrayTy =
      ArrayType::get(Builder.getPtrTy(), ReductionInfos.size());

  // Create a local thread-private variable to host the Reduce list
  // from a remote lane.
  Instruction *RemoteReductionListAlloca = Builder.CreateAlloca(
      RedListArrayTy, nullptr, ".omp.reduction.remote_reduce_list");

  Value *ReduceListAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReduceListAlloca, ReduceListArgType,
      ReduceListAlloca->getName() + ".ascast");
  Value *LaneIdAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      LaneIdAlloca, LaneIDArgPtrType, LaneIdAlloca->getName() + ".ascast");
  Value *RemoteLaneOffsetAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      RemoteLaneOffsetAlloca, LaneIDArgPtrType,
      RemoteLaneOffsetAlloca->getName() + ".ascast");
  Value *AlgoVerAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      AlgoVerAlloca, LaneIDArgPtrType, AlgoVerAlloca->getName() + ".ascast");
  Value *RemoteListAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      RemoteReductionListAlloca, Builder.getPtrTy(),
      RemoteReductionListAlloca->getName() + ".ascast");

  Builder.CreateStore(ReduceListArg, ReduceListAddrCast);
  Builder.CreateStore(LaneIDArg, LaneIdAddrCast);
  Builder.CreateStore(RemoteLaneOffsetArg, RemoteLaneOffsetAddrCast);
  Builder.CreateStore(AlgoVerArg, AlgoVerAddrCast);

  Value *ReduceList = Builder.CreateLoad(ReduceListArgType, ReduceListAddrCast);
  Value *LaneId = Builder.CreateLoad(LaneIDArgType, LaneIdAddrCast);
  Value *RemoteLaneOffset =
      Builder.CreateLoad(LaneIDArgType, RemoteLaneOffsetAddrCast);
  Value *AlgoVer = Builder.CreateLoad(LaneIDArgType, AlgoVerAddrCast);

  InsertPointTy AllocaIP = getInsertPointAfterInstr(RemoteReductionListAlloca);

  // This loop iterates through the list of reduce elements and copies,
  // element by element, from a remote lane in the warp to RemoteReduceList,
  // hosted on the thread's stack.
  emitReductionListCopy(
      AllocaIP, CopyAction::RemoteLaneToThread, RedListArrayTy, ReductionInfos,
      ReduceList, RemoteListAddrCast, {RemoteLaneOffset, nullptr, nullptr});

  // The actions to be performed on the Remote Reduce list is dependent
  // on the algorithm version.
  //
  //  if (AlgoVer==0) || (AlgoVer==1 && (LaneId < Offset)) || (AlgoVer==2 &&
  //  LaneId % 2 == 0 && Offset > 0):
  //    do the reduction value aggregation
  //
  //  The thread local variable Reduce list is mutated in place to host the
  //  reduced data, which is the aggregated value produced from local and
  //  remote lanes.
  //
  //  Note that AlgoVer is expected to be a constant integer known at compile
  //  time.
  //  When AlgoVer==0, the first conjunction evaluates to true, making
  //    the entire predicate true during compile time.
  //  When AlgoVer==1, the second conjunction has only the second part to be
  //    evaluated during runtime.  Other conjunctions evaluates to false
  //    during compile time.
  //  When AlgoVer==2, the third conjunction has only the second part to be
  //    evaluated during runtime.  Other conjunctions evaluates to false
  //    during compile time.
  Value *CondAlgo0 = Builder.CreateIsNull(AlgoVer);
  Value *Algo1 = Builder.CreateICmpEQ(AlgoVer, Builder.getInt16(1));
  Value *LaneComp = Builder.CreateICmpULT(LaneId, RemoteLaneOffset);
  Value *CondAlgo1 = Builder.CreateAnd(Algo1, LaneComp);
  Value *Algo2 = Builder.CreateICmpEQ(AlgoVer, Builder.getInt16(2));
  Value *LaneIdAnd1 = Builder.CreateAnd(LaneId, Builder.getInt16(1));
  Value *LaneIdComp = Builder.CreateIsNull(LaneIdAnd1);
  Value *Algo2AndLaneIdComp = Builder.CreateAnd(Algo2, LaneIdComp);
  Value *RemoteOffsetComp =
      Builder.CreateICmpSGT(RemoteLaneOffset, Builder.getInt16(0));
  Value *CondAlgo2 = Builder.CreateAnd(Algo2AndLaneIdComp, RemoteOffsetComp);
  Value *CA0OrCA1 = Builder.CreateOr(CondAlgo0, CondAlgo1);
  Value *CondReduce = Builder.CreateOr(CA0OrCA1, CondAlgo2);

  BasicBlock *ThenBB = BasicBlock::Create(Ctx, "then");
  BasicBlock *ElseBB = BasicBlock::Create(Ctx, "else");
  BasicBlock *MergeBB = BasicBlock::Create(Ctx, "ifcont");

  Builder.CreateCondBr(CondReduce, ThenBB, ElseBB);
  emitBlock(ThenBB, Builder.GetInsertBlock()->getParent());
  Value *LocalReduceListPtr = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReduceList, Builder.getPtrTy());
  Value *RemoteReduceListPtr = Builder.CreatePointerBitCastOrAddrSpaceCast(
      RemoteListAddrCast, Builder.getPtrTy());
  Builder.CreateCall(ReduceFn, {LocalReduceListPtr, RemoteReduceListPtr})
      ->addFnAttr(Attribute::NoUnwind);
  Builder.CreateBr(MergeBB);

  emitBlock(ElseBB, Builder.GetInsertBlock()->getParent());
  Builder.CreateBr(MergeBB);

  emitBlock(MergeBB, Builder.GetInsertBlock()->getParent());

  // if (AlgoVer==1 && (LaneId >= Offset)) copy Remote Reduce list to local
  // Reduce list.
  Algo1 = Builder.CreateICmpEQ(AlgoVer, Builder.getInt16(1));
  Value *LaneIdGtOffset = Builder.CreateICmpUGE(LaneId, RemoteLaneOffset);
  Value *CondCopy = Builder.CreateAnd(Algo1, LaneIdGtOffset);

  BasicBlock *CpyThenBB = BasicBlock::Create(Ctx, "then");
  BasicBlock *CpyElseBB = BasicBlock::Create(Ctx, "else");
  BasicBlock *CpyMergeBB = BasicBlock::Create(Ctx, "ifcont");
  Builder.CreateCondBr(CondCopy, CpyThenBB, CpyElseBB);

  emitBlock(CpyThenBB, Builder.GetInsertBlock()->getParent());
  emitReductionListCopy(AllocaIP, CopyAction::ThreadCopy, RedListArrayTy,
                        ReductionInfos, RemoteListAddrCast, ReduceList);
  Builder.CreateBr(CpyMergeBB);

  emitBlock(CpyElseBB, Builder.GetInsertBlock()->getParent());
  Builder.CreateBr(CpyMergeBB);

  emitBlock(CpyMergeBB, Builder.GetInsertBlock()->getParent());

  Builder.CreateRetVoid();

  return SarFunc;
}

Function *OpenMPIRBuilder::emitListToGlobalCopyFunction(
    ArrayRef<ReductionInfo> ReductionInfos, Type *ReductionsBufferTy,
    AttributeList FuncAttrs) {
  OpenMPIRBuilder::InsertPointTy OldIP = Builder.saveIP();
  LLVMContext &Ctx = M.getContext();
  FunctionType *FuncTy = FunctionType::get(
      Builder.getVoidTy(),
      {Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getPtrTy()},
      /* IsVarArg */ false);
  Function *LtGCFunc =
      Function::Create(FuncTy, GlobalVariable::InternalLinkage,
                       "_omp_reduction_list_to_global_copy_func", &M);
  LtGCFunc->setAttributes(FuncAttrs);
  LtGCFunc->addParamAttr(0, Attribute::NoUndef);
  LtGCFunc->addParamAttr(1, Attribute::NoUndef);
  LtGCFunc->addParamAttr(2, Attribute::NoUndef);

  BasicBlock *EntryBlock = BasicBlock::Create(Ctx, "entry", LtGCFunc);
  Builder.SetInsertPoint(EntryBlock);

  // Buffer: global reduction buffer.
  Argument *BufferArg = LtGCFunc->getArg(0);
  // Idx: index of the buffer.
  Argument *IdxArg = LtGCFunc->getArg(1);
  // ReduceList: thread local Reduce list.
  Argument *ReduceListArg = LtGCFunc->getArg(2);

  Value *BufferArgAlloca = Builder.CreateAlloca(Builder.getPtrTy(), nullptr,
                                                BufferArg->getName() + ".addr");
  Value *IdxArgAlloca = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr,
                                             IdxArg->getName() + ".addr");
  Value *ReduceListArgAlloca = Builder.CreateAlloca(
      Builder.getPtrTy(), nullptr, ReduceListArg->getName() + ".addr");
  Value *BufferArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      BufferArgAlloca, Builder.getPtrTy(),
      BufferArgAlloca->getName() + ".ascast");
  Value *IdxArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      IdxArgAlloca, Builder.getPtrTy(), IdxArgAlloca->getName() + ".ascast");
  Value *ReduceListArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReduceListArgAlloca, Builder.getPtrTy(),
      ReduceListArgAlloca->getName() + ".ascast");

  Builder.CreateStore(BufferArg, BufferArgAddrCast);
  Builder.CreateStore(IdxArg, IdxArgAddrCast);
  Builder.CreateStore(ReduceListArg, ReduceListArgAddrCast);

  Value *LocalReduceList =
      Builder.CreateLoad(Builder.getPtrTy(), ReduceListArgAddrCast);
  Value *BufferArgVal =
      Builder.CreateLoad(Builder.getPtrTy(), BufferArgAddrCast);
  Value *Idxs[] = {Builder.CreateLoad(Builder.getInt32Ty(), IdxArgAddrCast)};
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  for (auto En : enumerate(ReductionInfos)) {
    const ReductionInfo &RI = En.value();
    auto *RedListArrayTy =
        ArrayType::get(Builder.getPtrTy(), ReductionInfos.size());
    // Reduce element = LocalReduceList[i]
    Value *ElemPtrPtr = Builder.CreateInBoundsGEP(
        RedListArrayTy, LocalReduceList,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    // elemptr = ((CopyType*)(elemptrptr)) + I
    Value *ElemPtr = Builder.CreateLoad(Builder.getPtrTy(), ElemPtrPtr);

    // Global = Buffer.VD[Idx];
    Value *BufferVD =
        Builder.CreateInBoundsGEP(ReductionsBufferTy, BufferArgVal, Idxs);
    Value *GlobVal = Builder.CreateConstInBoundsGEP2_32(
        ReductionsBufferTy, BufferVD, 0, En.index());

    switch (RI.EvaluationKind) {
    case EvalKind::Scalar: {
      Value *TargetElement = Builder.CreateLoad(RI.ElementType, ElemPtr);
      Builder.CreateStore(TargetElement, GlobVal);
      break;
    }
    case EvalKind::Complex: {
      Value *SrcRealPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, ElemPtr, 0, 0, ".realp");
      Value *SrcReal = Builder.CreateLoad(
          RI.ElementType->getStructElementType(0), SrcRealPtr, ".real");
      Value *SrcImgPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, ElemPtr, 0, 1, ".imagp");
      Value *SrcImg = Builder.CreateLoad(
          RI.ElementType->getStructElementType(1), SrcImgPtr, ".imag");

      Value *DestRealPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, GlobVal, 0, 0, ".realp");
      Value *DestImgPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, GlobVal, 0, 1, ".imagp");
      Builder.CreateStore(SrcReal, DestRealPtr);
      Builder.CreateStore(SrcImg, DestImgPtr);
      break;
    }
    case EvalKind::Aggregate: {
      Value *SizeVal =
          Builder.getInt64(M.getDataLayout().getTypeStoreSize(RI.ElementType));
      Builder.CreateMemCpy(
          GlobVal, M.getDataLayout().getPrefTypeAlign(RI.ElementType), ElemPtr,
          M.getDataLayout().getPrefTypeAlign(RI.ElementType), SizeVal, false);
      break;
    }
    }
  }

  Builder.CreateRetVoid();
  Builder.restoreIP(OldIP);
  return LtGCFunc;
}

Function *OpenMPIRBuilder::emitListToGlobalReduceFunction(
    ArrayRef<ReductionInfo> ReductionInfos, Function *ReduceFn,
    Type *ReductionsBufferTy, AttributeList FuncAttrs) {
  OpenMPIRBuilder::InsertPointTy OldIP = Builder.saveIP();
  LLVMContext &Ctx = M.getContext();
  FunctionType *FuncTy = FunctionType::get(
      Builder.getVoidTy(),
      {Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getPtrTy()},
      /* IsVarArg */ false);
  Function *LtGRFunc =
      Function::Create(FuncTy, GlobalVariable::InternalLinkage,
                       "_omp_reduction_list_to_global_reduce_func", &M);
  LtGRFunc->setAttributes(FuncAttrs);
  LtGRFunc->addParamAttr(0, Attribute::NoUndef);
  LtGRFunc->addParamAttr(1, Attribute::NoUndef);
  LtGRFunc->addParamAttr(2, Attribute::NoUndef);

  BasicBlock *EntryBlock = BasicBlock::Create(Ctx, "entry", LtGRFunc);
  Builder.SetInsertPoint(EntryBlock);

  // Buffer: global reduction buffer.
  Argument *BufferArg = LtGRFunc->getArg(0);
  // Idx: index of the buffer.
  Argument *IdxArg = LtGRFunc->getArg(1);
  // ReduceList: thread local Reduce list.
  Argument *ReduceListArg = LtGRFunc->getArg(2);

  Value *BufferArgAlloca = Builder.CreateAlloca(Builder.getPtrTy(), nullptr,
                                                BufferArg->getName() + ".addr");
  Value *IdxArgAlloca = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr,
                                             IdxArg->getName() + ".addr");
  Value *ReduceListArgAlloca = Builder.CreateAlloca(
      Builder.getPtrTy(), nullptr, ReduceListArg->getName() + ".addr");
  auto *RedListArrayTy =
      ArrayType::get(Builder.getPtrTy(), ReductionInfos.size());

  // 1. Build a list of reduction variables.
  // void *RedList[<n>] = {<ReductionVars>[0], ..., <ReductionVars>[<n>-1]};
  Value *LocalReduceList =
      Builder.CreateAlloca(RedListArrayTy, nullptr, ".omp.reduction.red_list");

  Value *BufferArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      BufferArgAlloca, Builder.getPtrTy(),
      BufferArgAlloca->getName() + ".ascast");
  Value *IdxArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      IdxArgAlloca, Builder.getPtrTy(), IdxArgAlloca->getName() + ".ascast");
  Value *ReduceListArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReduceListArgAlloca, Builder.getPtrTy(),
      ReduceListArgAlloca->getName() + ".ascast");
  Value *LocalReduceListAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      LocalReduceList, Builder.getPtrTy(),
      LocalReduceList->getName() + ".ascast");

  Builder.CreateStore(BufferArg, BufferArgAddrCast);
  Builder.CreateStore(IdxArg, IdxArgAddrCast);
  Builder.CreateStore(ReduceListArg, ReduceListArgAddrCast);

  Value *BufferVal = Builder.CreateLoad(Builder.getPtrTy(), BufferArgAddrCast);
  Value *Idxs[] = {Builder.CreateLoad(Builder.getInt32Ty(), IdxArgAddrCast)};
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  for (auto En : enumerate(ReductionInfos)) {
    Value *TargetElementPtrPtr = Builder.CreateInBoundsGEP(
        RedListArrayTy, LocalReduceListAddrCast,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    Value *BufferVD =
        Builder.CreateInBoundsGEP(ReductionsBufferTy, BufferVal, Idxs);
    // Global = Buffer.VD[Idx];
    Value *GlobValPtr = Builder.CreateConstInBoundsGEP2_32(
        ReductionsBufferTy, BufferVD, 0, En.index());
    Builder.CreateStore(GlobValPtr, TargetElementPtrPtr);
  }

  // Call reduce_function(GlobalReduceList, ReduceList)
  Value *ReduceList =
      Builder.CreateLoad(Builder.getPtrTy(), ReduceListArgAddrCast);
  Builder.CreateCall(ReduceFn, {LocalReduceListAddrCast, ReduceList})
      ->addFnAttr(Attribute::NoUnwind);
  Builder.CreateRetVoid();
  Builder.restoreIP(OldIP);
  return LtGRFunc;
}

Function *OpenMPIRBuilder::emitGlobalToListCopyFunction(
    ArrayRef<ReductionInfo> ReductionInfos, Type *ReductionsBufferTy,
    AttributeList FuncAttrs) {
  OpenMPIRBuilder::InsertPointTy OldIP = Builder.saveIP();
  LLVMContext &Ctx = M.getContext();
  FunctionType *FuncTy = FunctionType::get(
      Builder.getVoidTy(),
      {Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getPtrTy()},
      /* IsVarArg */ false);
  Function *LtGCFunc =
      Function::Create(FuncTy, GlobalVariable::InternalLinkage,
                       "_omp_reduction_global_to_list_copy_func", &M);
  LtGCFunc->setAttributes(FuncAttrs);
  LtGCFunc->addParamAttr(0, Attribute::NoUndef);
  LtGCFunc->addParamAttr(1, Attribute::NoUndef);
  LtGCFunc->addParamAttr(2, Attribute::NoUndef);

  BasicBlock *EntryBlock = BasicBlock::Create(Ctx, "entry", LtGCFunc);
  Builder.SetInsertPoint(EntryBlock);

  // Buffer: global reduction buffer.
  Argument *BufferArg = LtGCFunc->getArg(0);
  // Idx: index of the buffer.
  Argument *IdxArg = LtGCFunc->getArg(1);
  // ReduceList: thread local Reduce list.
  Argument *ReduceListArg = LtGCFunc->getArg(2);

  Value *BufferArgAlloca = Builder.CreateAlloca(Builder.getPtrTy(), nullptr,
                                                BufferArg->getName() + ".addr");
  Value *IdxArgAlloca = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr,
                                             IdxArg->getName() + ".addr");
  Value *ReduceListArgAlloca = Builder.CreateAlloca(
      Builder.getPtrTy(), nullptr, ReduceListArg->getName() + ".addr");
  Value *BufferArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      BufferArgAlloca, Builder.getPtrTy(),
      BufferArgAlloca->getName() + ".ascast");
  Value *IdxArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      IdxArgAlloca, Builder.getPtrTy(), IdxArgAlloca->getName() + ".ascast");
  Value *ReduceListArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReduceListArgAlloca, Builder.getPtrTy(),
      ReduceListArgAlloca->getName() + ".ascast");
  Builder.CreateStore(BufferArg, BufferArgAddrCast);
  Builder.CreateStore(IdxArg, IdxArgAddrCast);
  Builder.CreateStore(ReduceListArg, ReduceListArgAddrCast);

  Value *LocalReduceList =
      Builder.CreateLoad(Builder.getPtrTy(), ReduceListArgAddrCast);
  Value *BufferVal = Builder.CreateLoad(Builder.getPtrTy(), BufferArgAddrCast);
  Value *Idxs[] = {Builder.CreateLoad(Builder.getInt32Ty(), IdxArgAddrCast)};
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  for (auto En : enumerate(ReductionInfos)) {
    const OpenMPIRBuilder::ReductionInfo &RI = En.value();
    auto *RedListArrayTy =
        ArrayType::get(Builder.getPtrTy(), ReductionInfos.size());
    // Reduce element = LocalReduceList[i]
    Value *ElemPtrPtr = Builder.CreateInBoundsGEP(
        RedListArrayTy, LocalReduceList,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    // elemptr = ((CopyType*)(elemptrptr)) + I
    Value *ElemPtr = Builder.CreateLoad(Builder.getPtrTy(), ElemPtrPtr);
    // Global = Buffer.VD[Idx];
    Value *BufferVD =
        Builder.CreateInBoundsGEP(ReductionsBufferTy, BufferVal, Idxs);
    Value *GlobValPtr = Builder.CreateConstInBoundsGEP2_32(
        ReductionsBufferTy, BufferVD, 0, En.index());

    switch (RI.EvaluationKind) {
    case EvalKind::Scalar: {
      Value *TargetElement = Builder.CreateLoad(RI.ElementType, GlobValPtr);
      Builder.CreateStore(TargetElement, ElemPtr);
      break;
    }
    case EvalKind::Complex: {
      Value *SrcRealPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, GlobValPtr, 0, 0, ".realp");
      Value *SrcReal = Builder.CreateLoad(
          RI.ElementType->getStructElementType(0), SrcRealPtr, ".real");
      Value *SrcImgPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, GlobValPtr, 0, 1, ".imagp");
      Value *SrcImg = Builder.CreateLoad(
          RI.ElementType->getStructElementType(1), SrcImgPtr, ".imag");

      Value *DestRealPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, ElemPtr, 0, 0, ".realp");
      Value *DestImgPtr = Builder.CreateConstInBoundsGEP2_32(
          RI.ElementType, ElemPtr, 0, 1, ".imagp");
      Builder.CreateStore(SrcReal, DestRealPtr);
      Builder.CreateStore(SrcImg, DestImgPtr);
      break;
    }
    case EvalKind::Aggregate: {
      Value *SizeVal =
          Builder.getInt64(M.getDataLayout().getTypeStoreSize(RI.ElementType));
      Builder.CreateMemCpy(
          ElemPtr, M.getDataLayout().getPrefTypeAlign(RI.ElementType),
          GlobValPtr, M.getDataLayout().getPrefTypeAlign(RI.ElementType),
          SizeVal, false);
      break;
    }
    }
  }

  Builder.CreateRetVoid();
  Builder.restoreIP(OldIP);
  return LtGCFunc;
}

Function *OpenMPIRBuilder::emitGlobalToListReduceFunction(
    ArrayRef<ReductionInfo> ReductionInfos, Function *ReduceFn,
    Type *ReductionsBufferTy, AttributeList FuncAttrs) {
  OpenMPIRBuilder::InsertPointTy OldIP = Builder.saveIP();
  LLVMContext &Ctx = M.getContext();
  auto *FuncTy = FunctionType::get(
      Builder.getVoidTy(),
      {Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getPtrTy()},
      /* IsVarArg */ false);
  Function *LtGRFunc =
      Function::Create(FuncTy, GlobalVariable::InternalLinkage,
                       "_omp_reduction_global_to_list_reduce_func", &M);
  LtGRFunc->setAttributes(FuncAttrs);
  LtGRFunc->addParamAttr(0, Attribute::NoUndef);
  LtGRFunc->addParamAttr(1, Attribute::NoUndef);
  LtGRFunc->addParamAttr(2, Attribute::NoUndef);

  BasicBlock *EntryBlock = BasicBlock::Create(Ctx, "entry", LtGRFunc);
  Builder.SetInsertPoint(EntryBlock);

  // Buffer: global reduction buffer.
  Argument *BufferArg = LtGRFunc->getArg(0);
  // Idx: index of the buffer.
  Argument *IdxArg = LtGRFunc->getArg(1);
  // ReduceList: thread local Reduce list.
  Argument *ReduceListArg = LtGRFunc->getArg(2);

  Value *BufferArgAlloca = Builder.CreateAlloca(Builder.getPtrTy(), nullptr,
                                                BufferArg->getName() + ".addr");
  Value *IdxArgAlloca = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr,
                                             IdxArg->getName() + ".addr");
  Value *ReduceListArgAlloca = Builder.CreateAlloca(
      Builder.getPtrTy(), nullptr, ReduceListArg->getName() + ".addr");
  ArrayType *RedListArrayTy =
      ArrayType::get(Builder.getPtrTy(), ReductionInfos.size());

  // 1. Build a list of reduction variables.
  // void *RedList[<n>] = {<ReductionVars>[0], ..., <ReductionVars>[<n>-1]};
  Value *LocalReduceList =
      Builder.CreateAlloca(RedListArrayTy, nullptr, ".omp.reduction.red_list");

  Value *BufferArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      BufferArgAlloca, Builder.getPtrTy(),
      BufferArgAlloca->getName() + ".ascast");
  Value *IdxArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      IdxArgAlloca, Builder.getPtrTy(), IdxArgAlloca->getName() + ".ascast");
  Value *ReduceListArgAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReduceListArgAlloca, Builder.getPtrTy(),
      ReduceListArgAlloca->getName() + ".ascast");
  Value *ReductionList = Builder.CreatePointerBitCastOrAddrSpaceCast(
      LocalReduceList, Builder.getPtrTy(),
      LocalReduceList->getName() + ".ascast");

  Builder.CreateStore(BufferArg, BufferArgAddrCast);
  Builder.CreateStore(IdxArg, IdxArgAddrCast);
  Builder.CreateStore(ReduceListArg, ReduceListArgAddrCast);

  Value *BufferVal = Builder.CreateLoad(Builder.getPtrTy(), BufferArgAddrCast);
  Value *Idxs[] = {Builder.CreateLoad(Builder.getInt32Ty(), IdxArgAddrCast)};
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  for (auto En : enumerate(ReductionInfos)) {
    Value *TargetElementPtrPtr = Builder.CreateInBoundsGEP(
        RedListArrayTy, ReductionList,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    // Global = Buffer.VD[Idx];
    Value *BufferVD =
        Builder.CreateInBoundsGEP(ReductionsBufferTy, BufferVal, Idxs);
    Value *GlobValPtr = Builder.CreateConstInBoundsGEP2_32(
        ReductionsBufferTy, BufferVD, 0, En.index());
    Builder.CreateStore(GlobValPtr, TargetElementPtrPtr);
  }

  // Call reduce_function(ReduceList, GlobalReduceList)
  Value *ReduceList =
      Builder.CreateLoad(Builder.getPtrTy(), ReduceListArgAddrCast);
  Builder.CreateCall(ReduceFn, {ReduceList, ReductionList})
      ->addFnAttr(Attribute::NoUnwind);
  Builder.CreateRetVoid();
  Builder.restoreIP(OldIP);
  return LtGRFunc;
}

std::string OpenMPIRBuilder::getReductionFuncName(StringRef Name) const {
  std::string Suffix =
      createPlatformSpecificName({"omp", "reduction", "reduction_func"});
  return (Name + Suffix).str();
}

Function *OpenMPIRBuilder::createReductionFunction(
    StringRef ReducerName, ArrayRef<ReductionInfo> ReductionInfos,
    ReductionGenCBKind ReductionGenCBKind, AttributeList FuncAttrs) {
  auto *FuncTy = FunctionType::get(Builder.getVoidTy(),
                                   {Builder.getPtrTy(), Builder.getPtrTy()},
                                   /* IsVarArg */ false);
  std::string Name = getReductionFuncName(ReducerName);
  Function *ReductionFunc =
      Function::Create(FuncTy, GlobalVariable::InternalLinkage, Name, &M);
  ReductionFunc->setAttributes(FuncAttrs);
  ReductionFunc->addParamAttr(0, Attribute::NoUndef);
  ReductionFunc->addParamAttr(1, Attribute::NoUndef);
  BasicBlock *EntryBB =
      BasicBlock::Create(M.getContext(), "entry", ReductionFunc);
  Builder.SetInsertPoint(EntryBB);

  // Need to alloca memory here and deal with the pointers before getting
  // LHS/RHS pointers out
  Value *LHSArrayPtr = nullptr;
  Value *RHSArrayPtr = nullptr;
  Argument *Arg0 = ReductionFunc->getArg(0);
  Argument *Arg1 = ReductionFunc->getArg(1);
  Type *Arg0Type = Arg0->getType();
  Type *Arg1Type = Arg1->getType();

  Value *LHSAlloca =
      Builder.CreateAlloca(Arg0Type, nullptr, Arg0->getName() + ".addr");
  Value *RHSAlloca =
      Builder.CreateAlloca(Arg1Type, nullptr, Arg1->getName() + ".addr");
  Value *LHSAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      LHSAlloca, Arg0Type, LHSAlloca->getName() + ".ascast");
  Value *RHSAddrCast = Builder.CreatePointerBitCastOrAddrSpaceCast(
      RHSAlloca, Arg1Type, RHSAlloca->getName() + ".ascast");
  Builder.CreateStore(Arg0, LHSAddrCast);
  Builder.CreateStore(Arg1, RHSAddrCast);
  LHSArrayPtr = Builder.CreateLoad(Arg0Type, LHSAddrCast);
  RHSArrayPtr = Builder.CreateLoad(Arg1Type, RHSAddrCast);

  Type *RedArrayTy = ArrayType::get(Builder.getPtrTy(), ReductionInfos.size());
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  SmallVector<Value *> LHSPtrs, RHSPtrs;
  for (auto En : enumerate(ReductionInfos)) {
    const ReductionInfo &RI = En.value();
    Value *RHSI8PtrPtr = Builder.CreateInBoundsGEP(
        RedArrayTy, RHSArrayPtr,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    Value *RHSI8Ptr = Builder.CreateLoad(Builder.getPtrTy(), RHSI8PtrPtr);
    Value *RHSPtr = Builder.CreatePointerBitCastOrAddrSpaceCast(
        RHSI8Ptr, RI.PrivateVariable->getType(),
        RHSI8Ptr->getName() + ".ascast");

    Value *LHSI8PtrPtr = Builder.CreateInBoundsGEP(
        RedArrayTy, LHSArrayPtr,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    Value *LHSI8Ptr = Builder.CreateLoad(Builder.getPtrTy(), LHSI8PtrPtr);
    Value *LHSPtr = Builder.CreatePointerBitCastOrAddrSpaceCast(
        LHSI8Ptr, RI.Variable->getType(), LHSI8Ptr->getName() + ".ascast");

    if (ReductionGenCBKind == ReductionGenCBKind::Clang) {
      LHSPtrs.emplace_back(LHSPtr);
      RHSPtrs.emplace_back(RHSPtr);
    } else {
      Value *LHS = Builder.CreateLoad(RI.ElementType, LHSPtr);
      Value *RHS = Builder.CreateLoad(RI.ElementType, RHSPtr);
      Value *Reduced;
      RI.ReductionGen(Builder.saveIP(), LHS, RHS, Reduced);
      if (!Builder.GetInsertBlock())
        return ReductionFunc;
      Builder.CreateStore(Reduced, LHSPtr);
    }
  }

  if (ReductionGenCBKind == ReductionGenCBKind::Clang)
    for (auto En : enumerate(ReductionInfos)) {
      unsigned Index = En.index();
      const ReductionInfo &RI = En.value();
      Value *LHSFixupPtr, *RHSFixupPtr;
      Builder.restoreIP(RI.ReductionGenClang(
          Builder.saveIP(), Index, &LHSFixupPtr, &RHSFixupPtr, ReductionFunc));

      // Fix the CallBack code genereated to use the correct Values for the LHS
      // and RHS
      LHSFixupPtr->replaceUsesWithIf(
          LHSPtrs[Index], [ReductionFunc](const Use &U) {
            return cast<Instruction>(U.getUser())->getParent()->getParent() ==
                   ReductionFunc;
          });
      RHSFixupPtr->replaceUsesWithIf(
          RHSPtrs[Index], [ReductionFunc](const Use &U) {
            return cast<Instruction>(U.getUser())->getParent()->getParent() ==
                   ReductionFunc;
          });
    }

  Builder.CreateRetVoid();
  return ReductionFunc;
}

static void
checkReductionInfos(ArrayRef<OpenMPIRBuilder::ReductionInfo> ReductionInfos,
                    bool IsGPU) {
  for (const OpenMPIRBuilder::ReductionInfo &RI : ReductionInfos) {
    (void)RI;
    assert(RI.Variable && "expected non-null variable");
    assert(RI.PrivateVariable && "expected non-null private variable");
    assert((RI.ReductionGen || RI.ReductionGenClang) &&
           "expected non-null reduction generator callback");
    if (!IsGPU) {
      assert(
          RI.Variable->getType() == RI.PrivateVariable->getType() &&
          "expected variables and their private equivalents to have the same "
          "type");
    }
    assert(RI.Variable->getType()->isPointerTy() &&
           "expected variables to be pointers");
  }
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createReductionsGPU(
    const LocationDescription &Loc, InsertPointTy AllocaIP,
    InsertPointTy CodeGenIP, ArrayRef<ReductionInfo> ReductionInfos,
    bool IsNoWait, bool IsTeamsReduction, bool HasDistribute,
    ReductionGenCBKind ReductionGenCBKind, std::optional<omp::GV> GridValue,
    unsigned ReductionBufNum, Value *SrcLocInfo) {
  if (!updateToLocation(Loc))
    return InsertPointTy();
  Builder.restoreIP(CodeGenIP);
  checkReductionInfos(ReductionInfos, /*IsGPU*/ true);
  LLVMContext &Ctx = M.getContext();

  // Source location for the ident struct
  if (!SrcLocInfo) {
    uint32_t SrcLocStrSize;
    Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
    SrcLocInfo = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  }

  if (ReductionInfos.size() == 0)
    return Builder.saveIP();

  Function *CurFunc = Builder.GetInsertBlock()->getParent();
  AttributeList FuncAttrs;
  AttrBuilder AttrBldr(Ctx);
  for (auto Attr : CurFunc->getAttributes().getFnAttrs())
    AttrBldr.addAttribute(Attr);
  AttrBldr.removeAttribute(Attribute::OptimizeNone);
  FuncAttrs = FuncAttrs.addFnAttributes(Ctx, AttrBldr);

  Function *ReductionFunc = nullptr;
  CodeGenIP = Builder.saveIP();
  ReductionFunc =
      createReductionFunction(Builder.GetInsertBlock()->getParent()->getName(),
                              ReductionInfos, ReductionGenCBKind, FuncAttrs);
  Builder.restoreIP(CodeGenIP);

  // Set the grid value in the config needed for lowering later on
  if (GridValue.has_value())
    Config.setGridValue(GridValue.value());
  else
    Config.setGridValue(getGridValue(T, ReductionFunc));

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateDefaultSrcLocStr(SrcLocStrSize);
  Value *RTLoc =
      getOrCreateIdent(SrcLocStr, SrcLocStrSize, omp::IdentFlag(0), 0);

  // Build res = __kmpc_reduce{_nowait}(<gtid>, <n>, sizeof(RedList),
  // RedList, shuffle_reduce_func, interwarp_copy_func);
  // or
  // Build res = __kmpc_reduce_teams_nowait_simple(<loc>, <gtid>, <lck>);
  Value *Res;

  // 1. Build a list of reduction variables.
  // void *RedList[<n>] = {<ReductionVars>[0], ..., <ReductionVars>[<n>-1]};
  auto Size = ReductionInfos.size();
  Type *PtrTy = PointerType::getUnqual(Ctx);
  Type *RedArrayTy = ArrayType::get(PtrTy, Size);
  CodeGenIP = Builder.saveIP();
  Builder.restoreIP(AllocaIP);
  Value *ReductionListAlloca =
      Builder.CreateAlloca(RedArrayTy, nullptr, ".omp.reduction.red_list");
  Value *ReductionList = Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReductionListAlloca, PtrTy, ReductionListAlloca->getName() + ".ascast");
  Builder.restoreIP(CodeGenIP);
  Type *IndexTy = Builder.getIndexTy(
      M.getDataLayout(), M.getDataLayout().getDefaultGlobalsAddressSpace());
  for (auto En : enumerate(ReductionInfos)) {
    const ReductionInfo &RI = En.value();
    Value *ElemPtr = Builder.CreateInBoundsGEP(
        RedArrayTy, ReductionList,
        {ConstantInt::get(IndexTy, 0), ConstantInt::get(IndexTy, En.index())});
    Value *CastElem =
        Builder.CreatePointerBitCastOrAddrSpaceCast(RI.PrivateVariable, PtrTy);
    Builder.CreateStore(CastElem, ElemPtr);
  }
  CodeGenIP = Builder.saveIP();
  Function *SarFunc =
      emitShuffleAndReduceFunction(ReductionInfos, ReductionFunc, FuncAttrs);
  Function *WcFunc = emitInterWarpCopyFunction(Loc, ReductionInfos, FuncAttrs);
  Builder.restoreIP(CodeGenIP);

  Value *RL = Builder.CreatePointerBitCastOrAddrSpaceCast(ReductionList, PtrTy);

  unsigned MaxDataSize = 0;
  SmallVector<Type *> ReductionTypeArgs;
  for (auto En : enumerate(ReductionInfos)) {
    auto Size = M.getDataLayout().getTypeStoreSize(En.value().ElementType);
    if (Size > MaxDataSize)
      MaxDataSize = Size;
    ReductionTypeArgs.emplace_back(En.value().ElementType);
  }
  Value *ReductionDataSize =
      Builder.getInt64(MaxDataSize * ReductionInfos.size());
  if (!IsTeamsReduction) {
    Value *SarFuncCast =
        Builder.CreatePointerBitCastOrAddrSpaceCast(SarFunc, PtrTy);
    Value *WcFuncCast =
        Builder.CreatePointerBitCastOrAddrSpaceCast(WcFunc, PtrTy);
    Value *Args[] = {RTLoc, ReductionDataSize, RL, SarFuncCast, WcFuncCast};
    Function *Pv2Ptr = getOrCreateRuntimeFunctionPtr(
        RuntimeFunction::OMPRTL___kmpc_nvptx_parallel_reduce_nowait_v2);
    Res = Builder.CreateCall(Pv2Ptr, Args);
  } else {
    CodeGenIP = Builder.saveIP();
    StructType *ReductionsBufferTy = StructType::create(
        Ctx, ReductionTypeArgs, "struct._globalized_locals_ty");
    Function *RedFixedBuferFn = getOrCreateRuntimeFunctionPtr(
        RuntimeFunction::OMPRTL___kmpc_reduction_get_fixed_buffer);
    Function *LtGCFunc = emitListToGlobalCopyFunction(
        ReductionInfos, ReductionsBufferTy, FuncAttrs);
    Function *LtGRFunc = emitListToGlobalReduceFunction(
        ReductionInfos, ReductionFunc, ReductionsBufferTy, FuncAttrs);
    Function *GtLCFunc = emitGlobalToListCopyFunction(
        ReductionInfos, ReductionsBufferTy, FuncAttrs);
    Function *GtLRFunc = emitGlobalToListReduceFunction(
        ReductionInfos, ReductionFunc, ReductionsBufferTy, FuncAttrs);
    Builder.restoreIP(CodeGenIP);

    Value *KernelTeamsReductionPtr = Builder.CreateCall(
        RedFixedBuferFn, {}, "_openmp_teams_reductions_buffer_$_$ptr");

    Value *Args3[] = {RTLoc,
                      KernelTeamsReductionPtr,
                      Builder.getInt32(ReductionBufNum),
                      ReductionDataSize,
                      RL,
                      SarFunc,
                      WcFunc,
                      LtGCFunc,
                      LtGRFunc,
                      GtLCFunc,
                      GtLRFunc};

    Function *TeamsReduceFn = getOrCreateRuntimeFunctionPtr(
        RuntimeFunction::OMPRTL___kmpc_nvptx_teams_reduce_nowait_v2);
    Res = Builder.CreateCall(TeamsReduceFn, Args3);
  }

  // 5. Build if (res == 1)
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, ".omp.reduction.done");
  BasicBlock *ThenBB = BasicBlock::Create(Ctx, ".omp.reduction.then");
  Value *Cond = Builder.CreateICmpEQ(Res, Builder.getInt32(1));
  Builder.CreateCondBr(Cond, ThenBB, ExitBB);

  // 6. Build then branch: where we have reduced values in the master
  //    thread in each team.
  //    __kmpc_end_reduce{_nowait}(<gtid>);
  //    break;
  emitBlock(ThenBB, CurFunc);

  // Add emission of __kmpc_end_reduce{_nowait}(<gtid>);
  for (auto En : enumerate(ReductionInfos)) {
    const ReductionInfo &RI = En.value();
    Value *LHS = RI.Variable;
    Value *RHS =
        Builder.CreatePointerBitCastOrAddrSpaceCast(RI.PrivateVariable, PtrTy);

    if (ReductionGenCBKind == ReductionGenCBKind::Clang) {
      Value *LHSPtr, *RHSPtr;
      Builder.restoreIP(RI.ReductionGenClang(Builder.saveIP(), En.index(),
                                             &LHSPtr, &RHSPtr, CurFunc));

      // Fix the CallBack code genereated to use the correct Values for the LHS
      // and RHS
      LHSPtr->replaceUsesWithIf(LHS, [ReductionFunc](const Use &U) {
        return cast<Instruction>(U.getUser())->getParent()->getParent() ==
               ReductionFunc;
      });
      RHSPtr->replaceUsesWithIf(RHS, [ReductionFunc](const Use &U) {
        return cast<Instruction>(U.getUser())->getParent()->getParent() ==
               ReductionFunc;
      });
    } else {
      assert(false && "Unhandled ReductionGenCBKind");
    }
  }
  emitBlock(ExitBB, CurFunc);

  Config.setEmitLLVMUsed();

  return Builder.saveIP();
}

static Function *getFreshReductionFunc(Module &M) {
  Type *VoidTy = Type::getVoidTy(M.getContext());
  Type *Int8PtrTy = PointerType::getUnqual(M.getContext());
  auto *FuncTy =
      FunctionType::get(VoidTy, {Int8PtrTy, Int8PtrTy}, /* IsVarArg */ false);
  return Function::Create(FuncTy, GlobalVariable::InternalLinkage,
                          ".omp.reduction.func", &M);
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createReductions(const LocationDescription &Loc,
                                  InsertPointTy AllocaIP,
                                  ArrayRef<ReductionInfo> ReductionInfos,
                                  ArrayRef<bool> IsByRef, bool IsNoWait) {
  assert(ReductionInfos.size() == IsByRef.size());
  for (const ReductionInfo &RI : ReductionInfos) {
    (void)RI;
    assert(RI.Variable && "expected non-null variable");
    assert(RI.PrivateVariable && "expected non-null private variable");
    assert(RI.ReductionGen && "expected non-null reduction generator callback");
    assert(RI.Variable->getType() == RI.PrivateVariable->getType() &&
           "expected variables and their private equivalents to have the same "
           "type");
    assert(RI.Variable->getType()->isPointerTy() &&
           "expected variables to be pointers");
  }

  if (!updateToLocation(Loc))
    return InsertPointTy();

  BasicBlock *InsertBlock = Loc.IP.getBlock();
  BasicBlock *ContinuationBlock =
      InsertBlock->splitBasicBlock(Loc.IP.getPoint(), "reduce.finalize");
  InsertBlock->getTerminator()->eraseFromParent();

  // Create and populate array of type-erased pointers to private reduction
  // values.
  unsigned NumReductions = ReductionInfos.size();
  Type *RedArrayTy = ArrayType::get(Builder.getPtrTy(), NumReductions);
  Builder.SetInsertPoint(AllocaIP.getBlock()->getTerminator());
  Value *RedArray = Builder.CreateAlloca(RedArrayTy, nullptr, "red.array");

  Builder.SetInsertPoint(InsertBlock, InsertBlock->end());

  for (auto En : enumerate(ReductionInfos)) {
    unsigned Index = En.index();
    const ReductionInfo &RI = En.value();
    Value *RedArrayElemPtr = Builder.CreateConstInBoundsGEP2_64(
        RedArrayTy, RedArray, 0, Index, "red.array.elem." + Twine(Index));
    Builder.CreateStore(RI.PrivateVariable, RedArrayElemPtr);
  }

  // Emit a call to the runtime function that orchestrates the reduction.
  // Declare the reduction function in the process.
  Function *Func = Builder.GetInsertBlock()->getParent();
  Module *Module = Func->getParent();
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  bool CanGenerateAtomic = all_of(ReductionInfos, [](const ReductionInfo &RI) {
    return RI.AtomicReductionGen;
  });
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize,
                                  CanGenerateAtomic
                                      ? IdentFlag::OMP_IDENT_FLAG_ATOMIC_REDUCE
                                      : IdentFlag(0));
  Value *ThreadId = getOrCreateThreadID(Ident);
  Constant *NumVariables = Builder.getInt32(NumReductions);
  const DataLayout &DL = Module->getDataLayout();
  unsigned RedArrayByteSize = DL.getTypeStoreSize(RedArrayTy);
  Constant *RedArraySize = Builder.getInt64(RedArrayByteSize);
  Function *ReductionFunc = getFreshReductionFunc(*Module);
  Value *Lock = getOMPCriticalRegionLock(".reduction");
  Function *ReduceFunc = getOrCreateRuntimeFunctionPtr(
      IsNoWait ? RuntimeFunction::OMPRTL___kmpc_reduce_nowait
               : RuntimeFunction::OMPRTL___kmpc_reduce);
  CallInst *ReduceCall =
      Builder.CreateCall(ReduceFunc,
                         {Ident, ThreadId, NumVariables, RedArraySize, RedArray,
                          ReductionFunc, Lock},
                         "reduce");

  // Create final reduction entry blocks for the atomic and non-atomic case.
  // Emit IR that dispatches control flow to one of the blocks based on the
  // reduction supporting the atomic mode.
  BasicBlock *NonAtomicRedBlock =
      BasicBlock::Create(Module->getContext(), "reduce.switch.nonatomic", Func);
  BasicBlock *AtomicRedBlock =
      BasicBlock::Create(Module->getContext(), "reduce.switch.atomic", Func);
  SwitchInst *Switch =
      Builder.CreateSwitch(ReduceCall, ContinuationBlock, /* NumCases */ 2);
  Switch->addCase(Builder.getInt32(1), NonAtomicRedBlock);
  Switch->addCase(Builder.getInt32(2), AtomicRedBlock);

  // Populate the non-atomic reduction using the elementwise reduction function.
  // This loads the elements from the global and private variables and reduces
  // them before storing back the result to the global variable.
  Builder.SetInsertPoint(NonAtomicRedBlock);
  for (auto En : enumerate(ReductionInfos)) {
    const ReductionInfo &RI = En.value();
    Type *ValueType = RI.ElementType;
    // We have one less load for by-ref case because that load is now inside of
    // the reduction region
    Value *RedValue = nullptr;
    if (!IsByRef[En.index()]) {
      RedValue = Builder.CreateLoad(ValueType, RI.Variable,
                                    "red.value." + Twine(En.index()));
    }
    Value *PrivateRedValue =
        Builder.CreateLoad(ValueType, RI.PrivateVariable,
                           "red.private.value." + Twine(En.index()));
    Value *Reduced;
    if (IsByRef[En.index()]) {
      Builder.restoreIP(RI.ReductionGen(Builder.saveIP(), RI.Variable,
                                        PrivateRedValue, Reduced));
    } else {
      Builder.restoreIP(RI.ReductionGen(Builder.saveIP(), RedValue,
                                        PrivateRedValue, Reduced));
    }
    if (!Builder.GetInsertBlock())
      return InsertPointTy();
    // for by-ref case, the load is inside of the reduction region
    if (!IsByRef[En.index()])
      Builder.CreateStore(Reduced, RI.Variable);
  }
  Function *EndReduceFunc = getOrCreateRuntimeFunctionPtr(
      IsNoWait ? RuntimeFunction::OMPRTL___kmpc_end_reduce_nowait
               : RuntimeFunction::OMPRTL___kmpc_end_reduce);
  Builder.CreateCall(EndReduceFunc, {Ident, ThreadId, Lock});
  Builder.CreateBr(ContinuationBlock);

  // Populate the atomic reduction using the atomic elementwise reduction
  // function. There are no loads/stores here because they will be happening
  // inside the atomic elementwise reduction.
  Builder.SetInsertPoint(AtomicRedBlock);
  if (CanGenerateAtomic && llvm::none_of(IsByRef, [](bool P) { return P; })) {
    for (const ReductionInfo &RI : ReductionInfos) {
      Builder.restoreIP(RI.AtomicReductionGen(Builder.saveIP(), RI.ElementType,
                                              RI.Variable, RI.PrivateVariable));
      if (!Builder.GetInsertBlock())
        return InsertPointTy();
    }
    Builder.CreateBr(ContinuationBlock);
  } else {
    Builder.CreateUnreachable();
  }

  // Populate the outlined reduction function using the elementwise reduction
  // function. Partial values are extracted from the type-erased array of
  // pointers to private variables.
  BasicBlock *ReductionFuncBlock =
      BasicBlock::Create(Module->getContext(), "", ReductionFunc);
  Builder.SetInsertPoint(ReductionFuncBlock);
  Value *LHSArrayPtr = ReductionFunc->getArg(0);
  Value *RHSArrayPtr = ReductionFunc->getArg(1);

  for (auto En : enumerate(ReductionInfos)) {
    const ReductionInfo &RI = En.value();
    Value *LHSI8PtrPtr = Builder.CreateConstInBoundsGEP2_64(
        RedArrayTy, LHSArrayPtr, 0, En.index());
    Value *LHSI8Ptr = Builder.CreateLoad(Builder.getPtrTy(), LHSI8PtrPtr);
    Value *LHSPtr = Builder.CreateBitCast(LHSI8Ptr, RI.Variable->getType());
    Value *LHS = Builder.CreateLoad(RI.ElementType, LHSPtr);
    Value *RHSI8PtrPtr = Builder.CreateConstInBoundsGEP2_64(
        RedArrayTy, RHSArrayPtr, 0, En.index());
    Value *RHSI8Ptr = Builder.CreateLoad(Builder.getPtrTy(), RHSI8PtrPtr);
    Value *RHSPtr =
        Builder.CreateBitCast(RHSI8Ptr, RI.PrivateVariable->getType());
    Value *RHS = Builder.CreateLoad(RI.ElementType, RHSPtr);
    Value *Reduced;
    Builder.restoreIP(RI.ReductionGen(Builder.saveIP(), LHS, RHS, Reduced));
    if (!Builder.GetInsertBlock())
      return InsertPointTy();
    // store is inside of the reduction region when using by-ref
    if (!IsByRef[En.index()])
      Builder.CreateStore(Reduced, LHSPtr);
  }
  Builder.CreateRetVoid();

  Builder.SetInsertPoint(ContinuationBlock);
  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createMaster(const LocationDescription &Loc,
                              BodyGenCallbackTy BodyGenCB,
                              FinalizeCallbackTy FiniCB) {

  if (!updateToLocation(Loc))
    return Loc.IP;

  Directive OMPD = Directive::OMPD_master;
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Value *Args[] = {Ident, ThreadId};

  Function *EntryRTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_master);
  Instruction *EntryCall = Builder.CreateCall(EntryRTLFn, Args);

  Function *ExitRTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_end_master);
  Instruction *ExitCall = Builder.CreateCall(ExitRTLFn, Args);

  return EmitOMPInlinedRegion(OMPD, EntryCall, ExitCall, BodyGenCB, FiniCB,
                              /*Conditional*/ true, /*hasFinalize*/ true);
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createMasked(const LocationDescription &Loc,
                              BodyGenCallbackTy BodyGenCB,
                              FinalizeCallbackTy FiniCB, Value *Filter) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  Directive OMPD = Directive::OMPD_masked;
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Value *Args[] = {Ident, ThreadId, Filter};
  Value *ArgsEnd[] = {Ident, ThreadId};

  Function *EntryRTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_masked);
  Instruction *EntryCall = Builder.CreateCall(EntryRTLFn, Args);

  Function *ExitRTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_end_masked);
  Instruction *ExitCall = Builder.CreateCall(ExitRTLFn, ArgsEnd);

  return EmitOMPInlinedRegion(OMPD, EntryCall, ExitCall, BodyGenCB, FiniCB,
                              /*Conditional*/ true, /*hasFinalize*/ true);
}

CanonicalLoopInfo *OpenMPIRBuilder::createLoopSkeleton(
    DebugLoc DL, Value *TripCount, Function *F, BasicBlock *PreInsertBefore,
    BasicBlock *PostInsertBefore, const Twine &Name) {
  Module *M = F->getParent();
  LLVMContext &Ctx = M->getContext();
  Type *IndVarTy = TripCount->getType();

  // Create the basic block structure.
  BasicBlock *Preheader =
      BasicBlock::Create(Ctx, "omp_" + Name + ".preheader", F, PreInsertBefore);
  BasicBlock *Header =
      BasicBlock::Create(Ctx, "omp_" + Name + ".header", F, PreInsertBefore);
  BasicBlock *Cond =
      BasicBlock::Create(Ctx, "omp_" + Name + ".cond", F, PreInsertBefore);
  BasicBlock *Body =
      BasicBlock::Create(Ctx, "omp_" + Name + ".body", F, PreInsertBefore);
  BasicBlock *Latch =
      BasicBlock::Create(Ctx, "omp_" + Name + ".inc", F, PostInsertBefore);
  BasicBlock *Exit =
      BasicBlock::Create(Ctx, "omp_" + Name + ".exit", F, PostInsertBefore);
  BasicBlock *After =
      BasicBlock::Create(Ctx, "omp_" + Name + ".after", F, PostInsertBefore);

  // Use specified DebugLoc for new instructions.
  Builder.SetCurrentDebugLocation(DL);

  Builder.SetInsertPoint(Preheader);
  Builder.CreateBr(Header);

  Builder.SetInsertPoint(Header);
  PHINode *IndVarPHI = Builder.CreatePHI(IndVarTy, 2, "omp_" + Name + ".iv");
  IndVarPHI->addIncoming(ConstantInt::get(IndVarTy, 0), Preheader);
  Builder.CreateBr(Cond);

  Builder.SetInsertPoint(Cond);
  Value *Cmp =
      Builder.CreateICmpULT(IndVarPHI, TripCount, "omp_" + Name + ".cmp");
  Builder.CreateCondBr(Cmp, Body, Exit);

  Builder.SetInsertPoint(Body);
  Builder.CreateBr(Latch);

  Builder.SetInsertPoint(Latch);
  Value *Next = Builder.CreateAdd(IndVarPHI, ConstantInt::get(IndVarTy, 1),
                                  "omp_" + Name + ".next", /*HasNUW=*/true);
  Builder.CreateBr(Header);
  IndVarPHI->addIncoming(Next, Latch);

  Builder.SetInsertPoint(Exit);
  Builder.CreateBr(After);

  // Remember and return the canonical control flow.
  LoopInfos.emplace_front();
  CanonicalLoopInfo *CL = &LoopInfos.front();

  CL->Header = Header;
  CL->Cond = Cond;
  CL->Latch = Latch;
  CL->Exit = Exit;

#ifndef NDEBUG
  CL->assertOK();
#endif
  return CL;
}

CanonicalLoopInfo *
OpenMPIRBuilder::createCanonicalLoop(const LocationDescription &Loc,
                                     LoopBodyGenCallbackTy BodyGenCB,
                                     Value *TripCount, const Twine &Name) {
  BasicBlock *BB = Loc.IP.getBlock();
  BasicBlock *NextBB = BB->getNextNode();

  CanonicalLoopInfo *CL = createLoopSkeleton(Loc.DL, TripCount, BB->getParent(),
                                             NextBB, NextBB, Name);
  BasicBlock *After = CL->getAfter();

  // If location is not set, don't connect the loop.
  if (updateToLocation(Loc)) {
    // Split the loop at the insertion point: Branch to the preheader and move
    // every following instruction to after the loop (the After BB). Also, the
    // new successor is the loop's after block.
    spliceBB(Builder, After, /*CreateBranch=*/false);
    Builder.CreateBr(CL->getPreheader());
  }

  // Emit the body content. We do it after connecting the loop to the CFG to
  // avoid that the callback encounters degenerate BBs.
  BodyGenCB(CL->getBodyIP(), CL->getIndVar());

#ifndef NDEBUG
  CL->assertOK();
#endif
  return CL;
}

CanonicalLoopInfo *OpenMPIRBuilder::createCanonicalLoop(
    const LocationDescription &Loc, LoopBodyGenCallbackTy BodyGenCB,
    Value *Start, Value *Stop, Value *Step, bool IsSigned, bool InclusiveStop,
    InsertPointTy ComputeIP, const Twine &Name) {

  // Consider the following difficulties (assuming 8-bit signed integers):
  //  * Adding \p Step to the loop counter which passes \p Stop may overflow:
  //      DO I = 1, 100, 50
  ///  * A \p Step of INT_MIN cannot not be normalized to a positive direction:
  //      DO I = 100, 0, -128

  // Start, Stop and Step must be of the same integer type.
  auto *IndVarTy = cast<IntegerType>(Start->getType());
  assert(IndVarTy == Stop->getType() && "Stop type mismatch");
  assert(IndVarTy == Step->getType() && "Step type mismatch");

  LocationDescription ComputeLoc =
      ComputeIP.isSet() ? LocationDescription(ComputeIP, Loc.DL) : Loc;
  updateToLocation(ComputeLoc);

  ConstantInt *Zero = ConstantInt::get(IndVarTy, 0);
  ConstantInt *One = ConstantInt::get(IndVarTy, 1);

  // Like Step, but always positive.
  Value *Incr = Step;

  // Distance between Start and Stop; always positive.
  Value *Span;

  // Condition whether there are no iterations are executed at all, e.g. because
  // UB < LB.
  Value *ZeroCmp;

  if (IsSigned) {
    // Ensure that increment is positive. If not, negate and invert LB and UB.
    Value *IsNeg = Builder.CreateICmpSLT(Step, Zero);
    Incr = Builder.CreateSelect(IsNeg, Builder.CreateNeg(Step), Step);
    Value *LB = Builder.CreateSelect(IsNeg, Stop, Start);
    Value *UB = Builder.CreateSelect(IsNeg, Start, Stop);
    Span = Builder.CreateSub(UB, LB, "", false, true);
    ZeroCmp = Builder.CreateICmp(
        InclusiveStop ? CmpInst::ICMP_SLT : CmpInst::ICMP_SLE, UB, LB);
  } else {
    Span = Builder.CreateSub(Stop, Start, "", true);
    ZeroCmp = Builder.CreateICmp(
        InclusiveStop ? CmpInst::ICMP_ULT : CmpInst::ICMP_ULE, Stop, Start);
  }

  Value *CountIfLooping;
  if (InclusiveStop) {
    CountIfLooping = Builder.CreateAdd(Builder.CreateUDiv(Span, Incr), One);
  } else {
    // Avoid incrementing past stop since it could overflow.
    Value *CountIfTwo = Builder.CreateAdd(
        Builder.CreateUDiv(Builder.CreateSub(Span, One), Incr), One);
    Value *OneCmp = Builder.CreateICmp(CmpInst::ICMP_ULE, Span, Incr);
    CountIfLooping = Builder.CreateSelect(OneCmp, One, CountIfTwo);
  }
  Value *TripCount = Builder.CreateSelect(ZeroCmp, Zero, CountIfLooping,
                                          "omp_" + Name + ".tripcount");

  auto BodyGen = [=](InsertPointTy CodeGenIP, Value *IV) {
    Builder.restoreIP(CodeGenIP);
    Value *Span = Builder.CreateMul(IV, Step);
    Value *IndVar = Builder.CreateAdd(Span, Start);
    BodyGenCB(Builder.saveIP(), IndVar);
  };
  LocationDescription LoopLoc = ComputeIP.isSet() ? Loc.IP : Builder.saveIP();
  return createCanonicalLoop(LoopLoc, BodyGen, TripCount, Name);
}

// Returns an LLVM function to call for initializing loop bounds using OpenMP
// static scheduling depending on `type`. Only i32 and i64 are supported by the
// runtime. Always interpret integers as unsigned similarly to
// CanonicalLoopInfo.
static FunctionCallee getKmpcForStaticInitForType(Type *Ty, Module &M,
                                                  OpenMPIRBuilder &OMPBuilder) {
  unsigned Bitwidth = Ty->getIntegerBitWidth();
  if (Bitwidth == 32)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_for_static_init_4u);
  if (Bitwidth == 64)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_for_static_init_8u);
  llvm_unreachable("unknown OpenMP loop iterator bitwidth");
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::applyStaticWorkshareLoop(DebugLoc DL, CanonicalLoopInfo *CLI,
                                          InsertPointTy AllocaIP,
                                          bool NeedsBarrier) {
  assert(CLI->isValid() && "Requires a valid canonical loop");
  assert(!isConflictIP(AllocaIP, CLI->getPreheaderIP()) &&
         "Require dedicated allocate IP");

  // Set up the source location value for OpenMP runtime.
  Builder.restoreIP(CLI->getPreheaderIP());
  Builder.SetCurrentDebugLocation(DL);

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(DL, SrcLocStrSize);
  Value *SrcLoc = getOrCreateIdent(SrcLocStr, SrcLocStrSize);

  // Declare useful OpenMP runtime functions.
  Value *IV = CLI->getIndVar();
  Type *IVTy = IV->getType();
  FunctionCallee StaticInit = getKmpcForStaticInitForType(IVTy, M, *this);
  FunctionCallee StaticFini =
      getOrCreateRuntimeFunction(M, omp::OMPRTL___kmpc_for_static_fini);

  // Allocate space for computed loop bounds as expected by the "init" function.
  Builder.SetInsertPoint(AllocaIP.getBlock()->getFirstNonPHIOrDbgOrAlloca());

  Type *I32Type = Type::getInt32Ty(M.getContext());
  Value *PLastIter = Builder.CreateAlloca(I32Type, nullptr, "p.lastiter");
  Value *PLowerBound = Builder.CreateAlloca(IVTy, nullptr, "p.lowerbound");
  Value *PUpperBound = Builder.CreateAlloca(IVTy, nullptr, "p.upperbound");
  Value *PStride = Builder.CreateAlloca(IVTy, nullptr, "p.stride");

  // At the end of the preheader, prepare for calling the "init" function by
  // storing the current loop bounds into the allocated space. A canonical loop
  // always iterates from 0 to trip-count with step 1. Note that "init" expects
  // and produces an inclusive upper bound.
  Builder.SetInsertPoint(CLI->getPreheader()->getTerminator());
  Constant *Zero = ConstantInt::get(IVTy, 0);
  Constant *One = ConstantInt::get(IVTy, 1);
  Builder.CreateStore(Zero, PLowerBound);
  Value *UpperBound = Builder.CreateSub(CLI->getTripCount(), One);
  Builder.CreateStore(UpperBound, PUpperBound);
  Builder.CreateStore(One, PStride);

  Value *ThreadNum = getOrCreateThreadID(SrcLoc);

  Constant *SchedulingType = ConstantInt::get(
      I32Type, static_cast<int>(OMPScheduleType::UnorderedStatic));

  // Call the "init" function and update the trip count of the loop with the
  // value it produced.
  Builder.CreateCall(StaticInit,
                     {SrcLoc, ThreadNum, SchedulingType, PLastIter, PLowerBound,
                      PUpperBound, PStride, One, Zero});
  Value *LowerBound = Builder.CreateLoad(IVTy, PLowerBound);
  Value *InclusiveUpperBound = Builder.CreateLoad(IVTy, PUpperBound);
  Value *TripCountMinusOne = Builder.CreateSub(InclusiveUpperBound, LowerBound);
  Value *TripCount = Builder.CreateAdd(TripCountMinusOne, One);
  CLI->setTripCount(TripCount);

  // Update all uses of the induction variable except the one in the condition
  // block that compares it with the actual upper bound, and the increment in
  // the latch block.

  CLI->mapIndVar([&](Instruction *OldIV) -> Value * {
    Builder.SetInsertPoint(CLI->getBody(),
                           CLI->getBody()->getFirstInsertionPt());
    Builder.SetCurrentDebugLocation(DL);
    return Builder.CreateAdd(OldIV, LowerBound);
  });

  // In the "exit" block, call the "fini" function.
  Builder.SetInsertPoint(CLI->getExit(),
                         CLI->getExit()->getTerminator()->getIterator());
  Builder.CreateCall(StaticFini, {SrcLoc, ThreadNum});

  // Add the barrier if requested.
  if (NeedsBarrier)
    createBarrier(LocationDescription(Builder.saveIP(), DL),
                  omp::Directive::OMPD_for, /* ForceSimpleCall */ false,
                  /* CheckCancelFlag */ false);

  InsertPointTy AfterIP = CLI->getAfterIP();
  CLI->invalidate();

  return AfterIP;
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::applyStaticChunkedWorkshareLoop(
    DebugLoc DL, CanonicalLoopInfo *CLI, InsertPointTy AllocaIP,
    bool NeedsBarrier, Value *ChunkSize) {
  assert(CLI->isValid() && "Requires a valid canonical loop");
  assert(ChunkSize && "Chunk size is required");

  LLVMContext &Ctx = CLI->getFunction()->getContext();
  Value *IV = CLI->getIndVar();
  Value *OrigTripCount = CLI->getTripCount();
  Type *IVTy = IV->getType();
  assert(IVTy->getIntegerBitWidth() <= 64 &&
         "Max supported tripcount bitwidth is 64 bits");
  Type *InternalIVTy = IVTy->getIntegerBitWidth() <= 32 ? Type::getInt32Ty(Ctx)
                                                        : Type::getInt64Ty(Ctx);
  Type *I32Type = Type::getInt32Ty(M.getContext());
  Constant *Zero = ConstantInt::get(InternalIVTy, 0);
  Constant *One = ConstantInt::get(InternalIVTy, 1);

  // Declare useful OpenMP runtime functions.
  FunctionCallee StaticInit =
      getKmpcForStaticInitForType(InternalIVTy, M, *this);
  FunctionCallee StaticFini =
      getOrCreateRuntimeFunction(M, omp::OMPRTL___kmpc_for_static_fini);

  // Allocate space for computed loop bounds as expected by the "init" function.
  Builder.restoreIP(AllocaIP);
  Builder.SetCurrentDebugLocation(DL);
  Value *PLastIter = Builder.CreateAlloca(I32Type, nullptr, "p.lastiter");
  Value *PLowerBound =
      Builder.CreateAlloca(InternalIVTy, nullptr, "p.lowerbound");
  Value *PUpperBound =
      Builder.CreateAlloca(InternalIVTy, nullptr, "p.upperbound");
  Value *PStride = Builder.CreateAlloca(InternalIVTy, nullptr, "p.stride");

  // Set up the source location value for the OpenMP runtime.
  Builder.restoreIP(CLI->getPreheaderIP());
  Builder.SetCurrentDebugLocation(DL);

  // TODO: Detect overflow in ubsan or max-out with current tripcount.
  Value *CastedChunkSize =
      Builder.CreateZExtOrTrunc(ChunkSize, InternalIVTy, "chunksize");
  Value *CastedTripCount =
      Builder.CreateZExt(OrigTripCount, InternalIVTy, "tripcount");

  Constant *SchedulingType = ConstantInt::get(
      I32Type, static_cast<int>(OMPScheduleType::UnorderedStaticChunked));
  Builder.CreateStore(Zero, PLowerBound);
  Value *OrigUpperBound = Builder.CreateSub(CastedTripCount, One);
  Builder.CreateStore(OrigUpperBound, PUpperBound);
  Builder.CreateStore(One, PStride);

  // Call the "init" function and update the trip count of the loop with the
  // value it produced.
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(DL, SrcLocStrSize);
  Value *SrcLoc = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadNum = getOrCreateThreadID(SrcLoc);
  Builder.CreateCall(StaticInit,
                     {/*loc=*/SrcLoc, /*global_tid=*/ThreadNum,
                      /*schedtype=*/SchedulingType, /*plastiter=*/PLastIter,
                      /*plower=*/PLowerBound, /*pupper=*/PUpperBound,
                      /*pstride=*/PStride, /*incr=*/One,
                      /*chunk=*/CastedChunkSize});

  // Load values written by the "init" function.
  Value *FirstChunkStart =
      Builder.CreateLoad(InternalIVTy, PLowerBound, "omp_firstchunk.lb");
  Value *FirstChunkStop =
      Builder.CreateLoad(InternalIVTy, PUpperBound, "omp_firstchunk.ub");
  Value *FirstChunkEnd = Builder.CreateAdd(FirstChunkStop, One);
  Value *ChunkRange =
      Builder.CreateSub(FirstChunkEnd, FirstChunkStart, "omp_chunk.range");
  Value *NextChunkStride =
      Builder.CreateLoad(InternalIVTy, PStride, "omp_dispatch.stride");

  // Create outer "dispatch" loop for enumerating the chunks.
  BasicBlock *DispatchEnter = splitBB(Builder, true);
  Value *DispatchCounter;
  CanonicalLoopInfo *DispatchCLI = createCanonicalLoop(
      {Builder.saveIP(), DL},
      [&](InsertPointTy BodyIP, Value *Counter) { DispatchCounter = Counter; },
      FirstChunkStart, CastedTripCount, NextChunkStride,
      /*IsSigned=*/false, /*InclusiveStop=*/false, /*ComputeIP=*/{},
      "dispatch");

  // Remember the BasicBlocks of the dispatch loop we need, then invalidate to
  // not have to preserve the canonical invariant.
  BasicBlock *DispatchBody = DispatchCLI->getBody();
  BasicBlock *DispatchLatch = DispatchCLI->getLatch();
  BasicBlock *DispatchExit = DispatchCLI->getExit();
  BasicBlock *DispatchAfter = DispatchCLI->getAfter();
  DispatchCLI->invalidate();

  // Rewire the original loop to become the chunk loop inside the dispatch loop.
  redirectTo(DispatchAfter, CLI->getAfter(), DL);
  redirectTo(CLI->getExit(), DispatchLatch, DL);
  redirectTo(DispatchBody, DispatchEnter, DL);

  // Prepare the prolog of the chunk loop.
  Builder.restoreIP(CLI->getPreheaderIP());
  Builder.SetCurrentDebugLocation(DL);

  // Compute the number of iterations of the chunk loop.
  Builder.SetInsertPoint(CLI->getPreheader()->getTerminator());
  Value *ChunkEnd = Builder.CreateAdd(DispatchCounter, ChunkRange);
  Value *IsLastChunk =
      Builder.CreateICmpUGE(ChunkEnd, CastedTripCount, "omp_chunk.is_last");
  Value *CountUntilOrigTripCount =
      Builder.CreateSub(CastedTripCount, DispatchCounter);
  Value *ChunkTripCount = Builder.CreateSelect(
      IsLastChunk, CountUntilOrigTripCount, ChunkRange, "omp_chunk.tripcount");
  Value *BackcastedChunkTC =
      Builder.CreateTrunc(ChunkTripCount, IVTy, "omp_chunk.tripcount.trunc");
  CLI->setTripCount(BackcastedChunkTC);

  // Update all uses of the induction variable except the one in the condition
  // block that compares it with the actual upper bound, and the increment in
  // the latch block.
  Value *BackcastedDispatchCounter =
      Builder.CreateTrunc(DispatchCounter, IVTy, "omp_dispatch.iv.trunc");
  CLI->mapIndVar([&](Instruction *) -> Value * {
    Builder.restoreIP(CLI->getBodyIP());
    return Builder.CreateAdd(IV, BackcastedDispatchCounter);
  });

  // In the "exit" block, call the "fini" function.
  Builder.SetInsertPoint(DispatchExit, DispatchExit->getFirstInsertionPt());
  Builder.CreateCall(StaticFini, {SrcLoc, ThreadNum});

  // Add the barrier if requested.
  if (NeedsBarrier)
    createBarrier(LocationDescription(Builder.saveIP(), DL), OMPD_for,
                  /*ForceSimpleCall=*/false, /*CheckCancelFlag=*/false);

#ifndef NDEBUG
  // Even though we currently do not support applying additional methods to it,
  // the chunk loop should remain a canonical loop.
  CLI->assertOK();
#endif

  return {DispatchAfter, DispatchAfter->getFirstInsertionPt()};
}

// Returns an LLVM function to call for executing an OpenMP static worksharing
// for loop depending on `type`. Only i32 and i64 are supported by the runtime.
// Always interpret integers as unsigned similarly to CanonicalLoopInfo.
static FunctionCallee
getKmpcForStaticLoopForType(Type *Ty, OpenMPIRBuilder *OMPBuilder,
                            WorksharingLoopType LoopType) {
  unsigned Bitwidth = Ty->getIntegerBitWidth();
  Module &M = OMPBuilder->M;
  switch (LoopType) {
  case WorksharingLoopType::ForStaticLoop:
    if (Bitwidth == 32)
      return OMPBuilder->getOrCreateRuntimeFunction(
          M, omp::RuntimeFunction::OMPRTL___kmpc_for_static_loop_4u);
    if (Bitwidth == 64)
      return OMPBuilder->getOrCreateRuntimeFunction(
          M, omp::RuntimeFunction::OMPRTL___kmpc_for_static_loop_8u);
    break;
  case WorksharingLoopType::DistributeStaticLoop:
    if (Bitwidth == 32)
      return OMPBuilder->getOrCreateRuntimeFunction(
          M, omp::RuntimeFunction::OMPRTL___kmpc_distribute_static_loop_4u);
    if (Bitwidth == 64)
      return OMPBuilder->getOrCreateRuntimeFunction(
          M, omp::RuntimeFunction::OMPRTL___kmpc_distribute_static_loop_8u);
    break;
  case WorksharingLoopType::DistributeForStaticLoop:
    if (Bitwidth == 32)
      return OMPBuilder->getOrCreateRuntimeFunction(
          M, omp::RuntimeFunction::OMPRTL___kmpc_distribute_for_static_loop_4u);
    if (Bitwidth == 64)
      return OMPBuilder->getOrCreateRuntimeFunction(
          M, omp::RuntimeFunction::OMPRTL___kmpc_distribute_for_static_loop_8u);
    break;
  }
  if (Bitwidth != 32 && Bitwidth != 64) {
    llvm_unreachable("Unknown OpenMP loop iterator bitwidth");
  }
  llvm_unreachable("Unknown type of OpenMP worksharing loop");
}

// Inserts a call to proper OpenMP Device RTL function which handles
// loop worksharing.
static void createTargetLoopWorkshareCall(
    OpenMPIRBuilder *OMPBuilder, WorksharingLoopType LoopType,
    BasicBlock *InsertBlock, Value *Ident, Value *LoopBodyArg,
    Type *ParallelTaskPtr, Value *TripCount, Function &LoopBodyFn) {
  Type *TripCountTy = TripCount->getType();
  Module &M = OMPBuilder->M;
  IRBuilder<> &Builder = OMPBuilder->Builder;
  FunctionCallee RTLFn =
      getKmpcForStaticLoopForType(TripCountTy, OMPBuilder, LoopType);
  SmallVector<Value *, 8> RealArgs;
  RealArgs.push_back(Ident);
  RealArgs.push_back(Builder.CreateBitCast(&LoopBodyFn, ParallelTaskPtr));
  RealArgs.push_back(LoopBodyArg);
  RealArgs.push_back(TripCount);
  if (LoopType == WorksharingLoopType::DistributeStaticLoop) {
    RealArgs.push_back(ConstantInt::get(TripCountTy, 0));
    Builder.CreateCall(RTLFn, RealArgs);
    return;
  }
  FunctionCallee RTLNumThreads = OMPBuilder->getOrCreateRuntimeFunction(
      M, omp::RuntimeFunction::OMPRTL_omp_get_num_threads);
  Builder.restoreIP({InsertBlock, std::prev(InsertBlock->end())});
  Value *NumThreads = Builder.CreateCall(RTLNumThreads, {});

  RealArgs.push_back(
      Builder.CreateZExtOrTrunc(NumThreads, TripCountTy, "num.threads.cast"));
  RealArgs.push_back(ConstantInt::get(TripCountTy, 0));
  if (LoopType == WorksharingLoopType::DistributeForStaticLoop) {
    RealArgs.push_back(ConstantInt::get(TripCountTy, 0));
  }

  Builder.CreateCall(RTLFn, RealArgs);
}

static void
workshareLoopTargetCallback(OpenMPIRBuilder *OMPIRBuilder,
                            CanonicalLoopInfo *CLI, Value *Ident,
                            Function &OutlinedFn, Type *ParallelTaskPtr,
                            const SmallVector<Instruction *, 4> &ToBeDeleted,
                            WorksharingLoopType LoopType) {
  IRBuilder<> &Builder = OMPIRBuilder->Builder;
  BasicBlock *Preheader = CLI->getPreheader();
  Value *TripCount = CLI->getTripCount();

  // After loop body outling, the loop body contains only set up
  // of loop body argument structure and the call to the outlined
  // loop body function. Firstly, we need to move setup of loop body args
  // into loop preheader.
  Preheader->splice(std::prev(Preheader->end()), CLI->getBody(),
                    CLI->getBody()->begin(), std::prev(CLI->getBody()->end()));

  // The next step is to remove the whole loop. We do not it need anymore.
  // That's why make an unconditional branch from loop preheader to loop
  // exit block
  Builder.restoreIP({Preheader, Preheader->end()});
  Preheader->getTerminator()->eraseFromParent();
  Builder.CreateBr(CLI->getExit());

  // Delete dead loop blocks
  OpenMPIRBuilder::OutlineInfo CleanUpInfo;
  SmallPtrSet<BasicBlock *, 32> RegionBlockSet;
  SmallVector<BasicBlock *, 32> BlocksToBeRemoved;
  CleanUpInfo.EntryBB = CLI->getHeader();
  CleanUpInfo.ExitBB = CLI->getExit();
  CleanUpInfo.collectBlocks(RegionBlockSet, BlocksToBeRemoved);
  DeleteDeadBlocks(BlocksToBeRemoved);

  // Find the instruction which corresponds to loop body argument structure
  // and remove the call to loop body function instruction.
  Value *LoopBodyArg;
  User *OutlinedFnUser = OutlinedFn.getUniqueUndroppableUser();
  assert(OutlinedFnUser &&
         "Expected unique undroppable user of outlined function");
  CallInst *OutlinedFnCallInstruction = dyn_cast<CallInst>(OutlinedFnUser);
  assert(OutlinedFnCallInstruction && "Expected outlined function call");
  assert((OutlinedFnCallInstruction->getParent() == Preheader) &&
         "Expected outlined function call to be located in loop preheader");
  // Check in case no argument structure has been passed.
  if (OutlinedFnCallInstruction->arg_size() > 1)
    LoopBodyArg = OutlinedFnCallInstruction->getArgOperand(1);
  else
    LoopBodyArg = Constant::getNullValue(Builder.getPtrTy());
  OutlinedFnCallInstruction->eraseFromParent();

  createTargetLoopWorkshareCall(OMPIRBuilder, LoopType, Preheader, Ident,
                                LoopBodyArg, ParallelTaskPtr, TripCount,
                                OutlinedFn);

  for (auto &ToBeDeletedItem : ToBeDeleted)
    ToBeDeletedItem->eraseFromParent();
  CLI->invalidate();
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::applyWorkshareLoopTarget(DebugLoc DL, CanonicalLoopInfo *CLI,
                                          InsertPointTy AllocaIP,
                                          WorksharingLoopType LoopType) {
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(DL, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);

  OutlineInfo OI;
  OI.OuterAllocaBB = CLI->getPreheader();
  Function *OuterFn = CLI->getPreheader()->getParent();

  // Instructions which need to be deleted at the end of code generation
  SmallVector<Instruction *, 4> ToBeDeleted;

  OI.OuterAllocaBB = AllocaIP.getBlock();

  // Mark the body loop as region which needs to be extracted
  OI.EntryBB = CLI->getBody();
  OI.ExitBB = CLI->getLatch()->splitBasicBlock(CLI->getLatch()->begin(),
                                               "omp.prelatch", true);

  // Prepare loop body for extraction
  Builder.restoreIP({CLI->getPreheader(), CLI->getPreheader()->begin()});

  // Insert new loop counter variable which will be used only in loop
  // body.
  AllocaInst *NewLoopCnt = Builder.CreateAlloca(CLI->getIndVarType(), 0, "");
  Instruction *NewLoopCntLoad =
      Builder.CreateLoad(CLI->getIndVarType(), NewLoopCnt);
  // New loop counter instructions are redundant in the loop preheader when
  // code generation for workshare loop is finshed. That's why mark them as
  // ready for deletion.
  ToBeDeleted.push_back(NewLoopCntLoad);
  ToBeDeleted.push_back(NewLoopCnt);

  // Analyse loop body region. Find all input variables which are used inside
  // loop body region.
  SmallPtrSet<BasicBlock *, 32> ParallelRegionBlockSet;
  SmallVector<BasicBlock *, 32> Blocks;
  OI.collectBlocks(ParallelRegionBlockSet, Blocks);
  SmallVector<BasicBlock *, 32> BlocksT(ParallelRegionBlockSet.begin(),
                                        ParallelRegionBlockSet.end());

  CodeExtractorAnalysisCache CEAC(*OuterFn);
  CodeExtractor Extractor(Blocks,
                          /* DominatorTree */ nullptr,
                          /* AggregateArgs */ true,
                          /* BlockFrequencyInfo */ nullptr,
                          /* BranchProbabilityInfo */ nullptr,
                          /* AssumptionCache */ nullptr,
                          /* AllowVarArgs */ true,
                          /* AllowAlloca */ true,
                          /* AllocationBlock */ CLI->getPreheader(),
                          /* Suffix */ ".omp_wsloop",
                          /* AggrArgsIn0AddrSpace */ true);

  BasicBlock *CommonExit = nullptr;
  SetVector<Value *> Inputs, Outputs, SinkingCands, HoistingCands;

  // Find allocas outside the loop body region which are used inside loop
  // body
  Extractor.findAllocas(CEAC, SinkingCands, HoistingCands, CommonExit);

  // We need to model loop body region as the function f(cnt, loop_arg).
  // That's why we replace loop induction variable by the new counter
  // which will be one of loop body function argument
  SmallVector<User *> Users(CLI->getIndVar()->user_begin(),
                            CLI->getIndVar()->user_end());
  for (auto Use : Users) {
    if (Instruction *Inst = dyn_cast<Instruction>(Use)) {
      if (ParallelRegionBlockSet.count(Inst->getParent())) {
        Inst->replaceUsesOfWith(CLI->getIndVar(), NewLoopCntLoad);
      }
    }
  }
  // Make sure that loop counter variable is not merged into loop body
  // function argument structure and it is passed as separate variable
  OI.ExcludeArgsFromAggregate.push_back(NewLoopCntLoad);

  // PostOutline CB is invoked when loop body function is outlined and
  // loop body is replaced by call to outlined function. We need to add
  // call to OpenMP device rtl inside loop preheader. OpenMP device rtl
  // function will handle loop control logic.
  //
  OI.PostOutlineCB = [=, ToBeDeletedVec =
                             std::move(ToBeDeleted)](Function &OutlinedFn) {
    workshareLoopTargetCallback(this, CLI, Ident, OutlinedFn, ParallelTaskPtr,
                                ToBeDeletedVec, LoopType);
  };
  addOutlineInfo(std::move(OI));
  return CLI->getAfterIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::applyWorkshareLoop(
    DebugLoc DL, CanonicalLoopInfo *CLI, InsertPointTy AllocaIP,
    bool NeedsBarrier, omp::ScheduleKind SchedKind, Value *ChunkSize,
    bool HasSimdModifier, bool HasMonotonicModifier,
    bool HasNonmonotonicModifier, bool HasOrderedClause,
    WorksharingLoopType LoopType) {
  if (Config.isTargetDevice())
    return applyWorkshareLoopTarget(DL, CLI, AllocaIP, LoopType);
  OMPScheduleType EffectiveScheduleType = computeOpenMPScheduleType(
      SchedKind, ChunkSize, HasSimdModifier, HasMonotonicModifier,
      HasNonmonotonicModifier, HasOrderedClause);

  bool IsOrdered = (EffectiveScheduleType & OMPScheduleType::ModifierOrdered) ==
                   OMPScheduleType::ModifierOrdered;
  switch (EffectiveScheduleType & ~OMPScheduleType::ModifierMask) {
  case OMPScheduleType::BaseStatic:
    assert(!ChunkSize && "No chunk size with static-chunked schedule");
    if (IsOrdered)
      return applyDynamicWorkshareLoop(DL, CLI, AllocaIP, EffectiveScheduleType,
                                       NeedsBarrier, ChunkSize);
    // FIXME: Monotonicity ignored?
    return applyStaticWorkshareLoop(DL, CLI, AllocaIP, NeedsBarrier);

  case OMPScheduleType::BaseStaticChunked:
    if (IsOrdered)
      return applyDynamicWorkshareLoop(DL, CLI, AllocaIP, EffectiveScheduleType,
                                       NeedsBarrier, ChunkSize);
    // FIXME: Monotonicity ignored?
    return applyStaticChunkedWorkshareLoop(DL, CLI, AllocaIP, NeedsBarrier,
                                           ChunkSize);

  case OMPScheduleType::BaseRuntime:
  case OMPScheduleType::BaseAuto:
  case OMPScheduleType::BaseGreedy:
  case OMPScheduleType::BaseBalanced:
  case OMPScheduleType::BaseSteal:
  case OMPScheduleType::BaseGuidedSimd:
  case OMPScheduleType::BaseRuntimeSimd:
    assert(!ChunkSize &&
           "schedule type does not support user-defined chunk sizes");
    [[fallthrough]];
  case OMPScheduleType::BaseDynamicChunked:
  case OMPScheduleType::BaseGuidedChunked:
  case OMPScheduleType::BaseGuidedIterativeChunked:
  case OMPScheduleType::BaseGuidedAnalyticalChunked:
  case OMPScheduleType::BaseStaticBalancedChunked:
    return applyDynamicWorkshareLoop(DL, CLI, AllocaIP, EffectiveScheduleType,
                                     NeedsBarrier, ChunkSize);

  default:
    llvm_unreachable("Unknown/unimplemented schedule kind");
  }
}

/// Returns an LLVM function to call for initializing loop bounds using OpenMP
/// dynamic scheduling depending on `type`. Only i32 and i64 are supported by
/// the runtime. Always interpret integers as unsigned similarly to
/// CanonicalLoopInfo.
static FunctionCallee
getKmpcForDynamicInitForType(Type *Ty, Module &M, OpenMPIRBuilder &OMPBuilder) {
  unsigned Bitwidth = Ty->getIntegerBitWidth();
  if (Bitwidth == 32)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_dispatch_init_4u);
  if (Bitwidth == 64)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_dispatch_init_8u);
  llvm_unreachable("unknown OpenMP loop iterator bitwidth");
}

/// Returns an LLVM function to call for updating the next loop using OpenMP
/// dynamic scheduling depending on `type`. Only i32 and i64 are supported by
/// the runtime. Always interpret integers as unsigned similarly to
/// CanonicalLoopInfo.
static FunctionCallee
getKmpcForDynamicNextForType(Type *Ty, Module &M, OpenMPIRBuilder &OMPBuilder) {
  unsigned Bitwidth = Ty->getIntegerBitWidth();
  if (Bitwidth == 32)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_dispatch_next_4u);
  if (Bitwidth == 64)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_dispatch_next_8u);
  llvm_unreachable("unknown OpenMP loop iterator bitwidth");
}

/// Returns an LLVM function to call for finalizing the dynamic loop using
/// depending on `type`. Only i32 and i64 are supported by the runtime. Always
/// interpret integers as unsigned similarly to CanonicalLoopInfo.
static FunctionCallee
getKmpcForDynamicFiniForType(Type *Ty, Module &M, OpenMPIRBuilder &OMPBuilder) {
  unsigned Bitwidth = Ty->getIntegerBitWidth();
  if (Bitwidth == 32)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_dispatch_fini_4u);
  if (Bitwidth == 64)
    return OMPBuilder.getOrCreateRuntimeFunction(
        M, omp::RuntimeFunction::OMPRTL___kmpc_dispatch_fini_8u);
  llvm_unreachable("unknown OpenMP loop iterator bitwidth");
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::applyDynamicWorkshareLoop(
    DebugLoc DL, CanonicalLoopInfo *CLI, InsertPointTy AllocaIP,
    OMPScheduleType SchedType, bool NeedsBarrier, Value *Chunk) {
  assert(CLI->isValid() && "Requires a valid canonical loop");
  assert(!isConflictIP(AllocaIP, CLI->getPreheaderIP()) &&
         "Require dedicated allocate IP");
  assert(isValidWorkshareLoopScheduleType(SchedType) &&
         "Require valid schedule type");

  bool Ordered = (SchedType & OMPScheduleType::ModifierOrdered) ==
                 OMPScheduleType::ModifierOrdered;

  // Set up the source location value for OpenMP runtime.
  Builder.SetCurrentDebugLocation(DL);

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(DL, SrcLocStrSize);
  Value *SrcLoc = getOrCreateIdent(SrcLocStr, SrcLocStrSize);

  // Declare useful OpenMP runtime functions.
  Value *IV = CLI->getIndVar();
  Type *IVTy = IV->getType();
  FunctionCallee DynamicInit = getKmpcForDynamicInitForType(IVTy, M, *this);
  FunctionCallee DynamicNext = getKmpcForDynamicNextForType(IVTy, M, *this);

  // Allocate space for computed loop bounds as expected by the "init" function.
  Builder.SetInsertPoint(AllocaIP.getBlock()->getFirstNonPHIOrDbgOrAlloca());
  Type *I32Type = Type::getInt32Ty(M.getContext());
  Value *PLastIter = Builder.CreateAlloca(I32Type, nullptr, "p.lastiter");
  Value *PLowerBound = Builder.CreateAlloca(IVTy, nullptr, "p.lowerbound");
  Value *PUpperBound = Builder.CreateAlloca(IVTy, nullptr, "p.upperbound");
  Value *PStride = Builder.CreateAlloca(IVTy, nullptr, "p.stride");

  // At the end of the preheader, prepare for calling the "init" function by
  // storing the current loop bounds into the allocated space. A canonical loop
  // always iterates from 0 to trip-count with step 1. Note that "init" expects
  // and produces an inclusive upper bound.
  BasicBlock *PreHeader = CLI->getPreheader();
  Builder.SetInsertPoint(PreHeader->getTerminator());
  Constant *One = ConstantInt::get(IVTy, 1);
  Builder.CreateStore(One, PLowerBound);
  Value *UpperBound = CLI->getTripCount();
  Builder.CreateStore(UpperBound, PUpperBound);
  Builder.CreateStore(One, PStride);

  BasicBlock *Header = CLI->getHeader();
  BasicBlock *Exit = CLI->getExit();
  BasicBlock *Cond = CLI->getCond();
  BasicBlock *Latch = CLI->getLatch();
  InsertPointTy AfterIP = CLI->getAfterIP();

  // The CLI will be "broken" in the code below, as the loop is no longer
  // a valid canonical loop.

  if (!Chunk)
    Chunk = One;

  Value *ThreadNum = getOrCreateThreadID(SrcLoc);

  Constant *SchedulingType =
      ConstantInt::get(I32Type, static_cast<int>(SchedType));

  // Call the "init" function.
  Builder.CreateCall(DynamicInit,
                     {SrcLoc, ThreadNum, SchedulingType, /* LowerBound */ One,
                      UpperBound, /* step */ One, Chunk});

  // An outer loop around the existing one.
  BasicBlock *OuterCond = BasicBlock::Create(
      PreHeader->getContext(), Twine(PreHeader->getName()) + ".outer.cond",
      PreHeader->getParent());
  // This needs to be 32-bit always, so can't use the IVTy Zero above.
  Builder.SetInsertPoint(OuterCond, OuterCond->getFirstInsertionPt());
  Value *Res =
      Builder.CreateCall(DynamicNext, {SrcLoc, ThreadNum, PLastIter,
                                       PLowerBound, PUpperBound, PStride});
  Constant *Zero32 = ConstantInt::get(I32Type, 0);
  Value *MoreWork = Builder.CreateCmp(CmpInst::ICMP_NE, Res, Zero32);
  Value *LowerBound =
      Builder.CreateSub(Builder.CreateLoad(IVTy, PLowerBound), One, "lb");
  Builder.CreateCondBr(MoreWork, Header, Exit);

  // Change PHI-node in loop header to use outer cond rather than preheader,
  // and set IV to the LowerBound.
  Instruction *Phi = &Header->front();
  auto *PI = cast<PHINode>(Phi);
  PI->setIncomingBlock(0, OuterCond);
  PI->setIncomingValue(0, LowerBound);

  // Then set the pre-header to jump to the OuterCond
  Instruction *Term = PreHeader->getTerminator();
  auto *Br = cast<BranchInst>(Term);
  Br->setSuccessor(0, OuterCond);

  // Modify the inner condition:
  // * Use the UpperBound returned from the DynamicNext call.
  // * jump to the loop outer loop when done with one of the inner loops.
  Builder.SetInsertPoint(Cond, Cond->getFirstInsertionPt());
  UpperBound = Builder.CreateLoad(IVTy, PUpperBound, "ub");
  Instruction *Comp = &*Builder.GetInsertPoint();
  auto *CI = cast<CmpInst>(Comp);
  CI->setOperand(1, UpperBound);
  // Redirect the inner exit to branch to outer condition.
  Instruction *Branch = &Cond->back();
  auto *BI = cast<BranchInst>(Branch);
  assert(BI->getSuccessor(1) == Exit);
  BI->setSuccessor(1, OuterCond);

  // Call the "fini" function if "ordered" is present in wsloop directive.
  if (Ordered) {
    Builder.SetInsertPoint(&Latch->back());
    FunctionCallee DynamicFini = getKmpcForDynamicFiniForType(IVTy, M, *this);
    Builder.CreateCall(DynamicFini, {SrcLoc, ThreadNum});
  }

  // Add the barrier if requested.
  if (NeedsBarrier) {
    Builder.SetInsertPoint(&Exit->back());
    createBarrier(LocationDescription(Builder.saveIP(), DL),
                  omp::Directive::OMPD_for, /* ForceSimpleCall */ false,
                  /* CheckCancelFlag */ false);
  }

  CLI->invalidate();
  return AfterIP;
}

/// Redirect all edges that branch to \p OldTarget to \p NewTarget. That is,
/// after this \p OldTarget will be orphaned.
static void redirectAllPredecessorsTo(BasicBlock *OldTarget,
                                      BasicBlock *NewTarget, DebugLoc DL) {
  for (BasicBlock *Pred : make_early_inc_range(predecessors(OldTarget)))
    redirectTo(Pred, NewTarget, DL);
}

/// Determine which blocks in \p BBs are reachable from outside and remove the
/// ones that are not reachable from the function.
static void removeUnusedBlocksFromParent(ArrayRef<BasicBlock *> BBs) {
  SmallPtrSet<BasicBlock *, 6> BBsToErase{BBs.begin(), BBs.end()};
  auto HasRemainingUses = [&BBsToErase](BasicBlock *BB) {
    for (Use &U : BB->uses()) {
      auto *UseInst = dyn_cast<Instruction>(U.getUser());
      if (!UseInst)
        continue;
      if (BBsToErase.count(UseInst->getParent()))
        continue;
      return true;
    }
    return false;
  };

  while (BBsToErase.remove_if(HasRemainingUses)) {
    // Try again if anything was removed.
  }

  SmallVector<BasicBlock *, 7> BBVec(BBsToErase.begin(), BBsToErase.end());
  DeleteDeadBlocks(BBVec);
}

CanonicalLoopInfo *
OpenMPIRBuilder::collapseLoops(DebugLoc DL, ArrayRef<CanonicalLoopInfo *> Loops,
                               InsertPointTy ComputeIP) {
  assert(Loops.size() >= 1 && "At least one loop required");
  size_t NumLoops = Loops.size();

  // Nothing to do if there is already just one loop.
  if (NumLoops == 1)
    return Loops.front();

  CanonicalLoopInfo *Outermost = Loops.front();
  CanonicalLoopInfo *Innermost = Loops.back();
  BasicBlock *OrigPreheader = Outermost->getPreheader();
  BasicBlock *OrigAfter = Outermost->getAfter();
  Function *F = OrigPreheader->getParent();

  // Loop control blocks that may become orphaned later.
  SmallVector<BasicBlock *, 12> OldControlBBs;
  OldControlBBs.reserve(6 * Loops.size());
  for (CanonicalLoopInfo *Loop : Loops)
    Loop->collectControlBlocks(OldControlBBs);

  // Setup the IRBuilder for inserting the trip count computation.
  Builder.SetCurrentDebugLocation(DL);
  if (ComputeIP.isSet())
    Builder.restoreIP(ComputeIP);
  else
    Builder.restoreIP(Outermost->getPreheaderIP());

  // Derive the collapsed' loop trip count.
  // TODO: Find common/largest indvar type.
  Value *CollapsedTripCount = nullptr;
  for (CanonicalLoopInfo *L : Loops) {
    assert(L->isValid() &&
           "All loops to collapse must be valid canonical loops");
    Value *OrigTripCount = L->getTripCount();
    if (!CollapsedTripCount) {
      CollapsedTripCount = OrigTripCount;
      continue;
    }

    // TODO: Enable UndefinedSanitizer to diagnose an overflow here.
    CollapsedTripCount = Builder.CreateMul(CollapsedTripCount, OrigTripCount,
                                           {}, /*HasNUW=*/true);
  }

  // Create the collapsed loop control flow.
  CanonicalLoopInfo *Result =
      createLoopSkeleton(DL, CollapsedTripCount, F,
                         OrigPreheader->getNextNode(), OrigAfter, "collapsed");

  // Build the collapsed loop body code.
  // Start with deriving the input loop induction variables from the collapsed
  // one, using a divmod scheme. To preserve the original loops' order, the
  // innermost loop use the least significant bits.
  Builder.restoreIP(Result->getBodyIP());

  Value *Leftover = Result->getIndVar();
  SmallVector<Value *> NewIndVars;
  NewIndVars.resize(NumLoops);
  for (int i = NumLoops - 1; i >= 1; --i) {
    Value *OrigTripCount = Loops[i]->getTripCount();

    Value *NewIndVar = Builder.CreateURem(Leftover, OrigTripCount);
    NewIndVars[i] = NewIndVar;

    Leftover = Builder.CreateUDiv(Leftover, OrigTripCount);
  }
  // Outermost loop gets all the remaining bits.
  NewIndVars[0] = Leftover;

  // Construct the loop body control flow.
  // We progressively construct the branch structure following in direction of
  // the control flow, from the leading in-between code, the loop nest body, the
  // trailing in-between code, and rejoining the collapsed loop's latch.
  // ContinueBlock and ContinuePred keep track of the source(s) of next edge. If
  // the ContinueBlock is set, continue with that block. If ContinuePred, use
  // its predecessors as sources.
  BasicBlock *ContinueBlock = Result->getBody();
  BasicBlock *ContinuePred = nullptr;
  auto ContinueWith = [&ContinueBlock, &ContinuePred, DL](BasicBlock *Dest,
                                                          BasicBlock *NextSrc) {
    if (ContinueBlock)
      redirectTo(ContinueBlock, Dest, DL);
    else
      redirectAllPredecessorsTo(ContinuePred, Dest, DL);

    ContinueBlock = nullptr;
    ContinuePred = NextSrc;
  };

  // The code before the nested loop of each level.
  // Because we are sinking it into the nest, it will be executed more often
  // that the original loop. More sophisticated schemes could keep track of what
  // the in-between code is and instantiate it only once per thread.
  for (size_t i = 0; i < NumLoops - 1; ++i)
    ContinueWith(Loops[i]->getBody(), Loops[i + 1]->getHeader());

  // Connect the loop nest body.
  ContinueWith(Innermost->getBody(), Innermost->getLatch());

  // The code after the nested loop at each level.
  for (size_t i = NumLoops - 1; i > 0; --i)
    ContinueWith(Loops[i]->getAfter(), Loops[i - 1]->getLatch());

  // Connect the finished loop to the collapsed loop latch.
  ContinueWith(Result->getLatch(), nullptr);

  // Replace the input loops with the new collapsed loop.
  redirectTo(Outermost->getPreheader(), Result->getPreheader(), DL);
  redirectTo(Result->getAfter(), Outermost->getAfter(), DL);

  // Replace the input loop indvars with the derived ones.
  for (size_t i = 0; i < NumLoops; ++i)
    Loops[i]->getIndVar()->replaceAllUsesWith(NewIndVars[i]);

  // Remove unused parts of the input loops.
  removeUnusedBlocksFromParent(OldControlBBs);

  for (CanonicalLoopInfo *L : Loops)
    L->invalidate();

#ifndef NDEBUG
  Result->assertOK();
#endif
  return Result;
}

std::vector<CanonicalLoopInfo *>
OpenMPIRBuilder::tileLoops(DebugLoc DL, ArrayRef<CanonicalLoopInfo *> Loops,
                           ArrayRef<Value *> TileSizes) {
  assert(TileSizes.size() == Loops.size() &&
         "Must pass as many tile sizes as there are loops");
  int NumLoops = Loops.size();
  assert(NumLoops >= 1 && "At least one loop to tile required");

  CanonicalLoopInfo *OutermostLoop = Loops.front();
  CanonicalLoopInfo *InnermostLoop = Loops.back();
  Function *F = OutermostLoop->getBody()->getParent();
  BasicBlock *InnerEnter = InnermostLoop->getBody();
  BasicBlock *InnerLatch = InnermostLoop->getLatch();

  // Loop control blocks that may become orphaned later.
  SmallVector<BasicBlock *, 12> OldControlBBs;
  OldControlBBs.reserve(6 * Loops.size());
  for (CanonicalLoopInfo *Loop : Loops)
    Loop->collectControlBlocks(OldControlBBs);

  // Collect original trip counts and induction variable to be accessible by
  // index. Also, the structure of the original loops is not preserved during
  // the construction of the tiled loops, so do it before we scavenge the BBs of
  // any original CanonicalLoopInfo.
  SmallVector<Value *, 4> OrigTripCounts, OrigIndVars;
  for (CanonicalLoopInfo *L : Loops) {
    assert(L->isValid() && "All input loops must be valid canonical loops");
    OrigTripCounts.push_back(L->getTripCount());
    OrigIndVars.push_back(L->getIndVar());
  }

  // Collect the code between loop headers. These may contain SSA definitions
  // that are used in the loop nest body. To be usable with in the innermost
  // body, these BasicBlocks will be sunk into the loop nest body. That is,
  // these instructions may be executed more often than before the tiling.
  // TODO: It would be sufficient to only sink them into body of the
  // corresponding tile loop.
  SmallVector<std::pair<BasicBlock *, BasicBlock *>, 4> InbetweenCode;
  for (int i = 0; i < NumLoops - 1; ++i) {
    CanonicalLoopInfo *Surrounding = Loops[i];
    CanonicalLoopInfo *Nested = Loops[i + 1];

    BasicBlock *EnterBB = Surrounding->getBody();
    BasicBlock *ExitBB = Nested->getHeader();
    InbetweenCode.emplace_back(EnterBB, ExitBB);
  }

  // Compute the trip counts of the floor loops.
  Builder.SetCurrentDebugLocation(DL);
  Builder.restoreIP(OutermostLoop->getPreheaderIP());
  SmallVector<Value *, 4> FloorCount, FloorRems;
  for (int i = 0; i < NumLoops; ++i) {
    Value *TileSize = TileSizes[i];
    Value *OrigTripCount = OrigTripCounts[i];
    Type *IVType = OrigTripCount->getType();

    Value *FloorTripCount = Builder.CreateUDiv(OrigTripCount, TileSize);
    Value *FloorTripRem = Builder.CreateURem(OrigTripCount, TileSize);

    // 0 if tripcount divides the tilesize, 1 otherwise.
    // 1 means we need an additional iteration for a partial tile.
    //
    // Unfortunately we cannot just use the roundup-formula
    //   (tripcount + tilesize - 1)/tilesize
    // because the summation might overflow. We do not want introduce undefined
    // behavior when the untiled loop nest did not.
    Value *FloorTripOverflow =
        Builder.CreateICmpNE(FloorTripRem, ConstantInt::get(IVType, 0));

    FloorTripOverflow = Builder.CreateZExt(FloorTripOverflow, IVType);
    FloorTripCount =
        Builder.CreateAdd(FloorTripCount, FloorTripOverflow,
                          "omp_floor" + Twine(i) + ".tripcount", true);

    // Remember some values for later use.
    FloorCount.push_back(FloorTripCount);
    FloorRems.push_back(FloorTripRem);
  }

  // Generate the new loop nest, from the outermost to the innermost.
  std::vector<CanonicalLoopInfo *> Result;
  Result.reserve(NumLoops * 2);

  // The basic block of the surrounding loop that enters the nest generated
  // loop.
  BasicBlock *Enter = OutermostLoop->getPreheader();

  // The basic block of the surrounding loop where the inner code should
  // continue.
  BasicBlock *Continue = OutermostLoop->getAfter();

  // Where the next loop basic block should be inserted.
  BasicBlock *OutroInsertBefore = InnermostLoop->getExit();

  auto EmbeddNewLoop =
      [this, DL, F, InnerEnter, &Enter, &Continue, &OutroInsertBefore](
          Value *TripCount, const Twine &Name) -> CanonicalLoopInfo * {
    CanonicalLoopInfo *EmbeddedLoop = createLoopSkeleton(
        DL, TripCount, F, InnerEnter, OutroInsertBefore, Name);
    redirectTo(Enter, EmbeddedLoop->getPreheader(), DL);
    redirectTo(EmbeddedLoop->getAfter(), Continue, DL);

    // Setup the position where the next embedded loop connects to this loop.
    Enter = EmbeddedLoop->getBody();
    Continue = EmbeddedLoop->getLatch();
    OutroInsertBefore = EmbeddedLoop->getLatch();
    return EmbeddedLoop;
  };

  auto EmbeddNewLoops = [&Result, &EmbeddNewLoop](ArrayRef<Value *> TripCounts,
                                                  const Twine &NameBase) {
    for (auto P : enumerate(TripCounts)) {
      CanonicalLoopInfo *EmbeddedLoop =
          EmbeddNewLoop(P.value(), NameBase + Twine(P.index()));
      Result.push_back(EmbeddedLoop);
    }
  };

  EmbeddNewLoops(FloorCount, "floor");

  // Within the innermost floor loop, emit the code that computes the tile
  // sizes.
  Builder.SetInsertPoint(Enter->getTerminator());
  SmallVector<Value *, 4> TileCounts;
  for (int i = 0; i < NumLoops; ++i) {
    CanonicalLoopInfo *FloorLoop = Result[i];
    Value *TileSize = TileSizes[i];

    Value *FloorIsEpilogue =
        Builder.CreateICmpEQ(FloorLoop->getIndVar(), FloorCount[i]);
    Value *TileTripCount =
        Builder.CreateSelect(FloorIsEpilogue, FloorRems[i], TileSize);

    TileCounts.push_back(TileTripCount);
  }

  // Create the tile loops.
  EmbeddNewLoops(TileCounts, "tile");

  // Insert the inbetween code into the body.
  BasicBlock *BodyEnter = Enter;
  BasicBlock *BodyEntered = nullptr;
  for (std::pair<BasicBlock *, BasicBlock *> P : InbetweenCode) {
    BasicBlock *EnterBB = P.first;
    BasicBlock *ExitBB = P.second;

    if (BodyEnter)
      redirectTo(BodyEnter, EnterBB, DL);
    else
      redirectAllPredecessorsTo(BodyEntered, EnterBB, DL);

    BodyEnter = nullptr;
    BodyEntered = ExitBB;
  }

  // Append the original loop nest body into the generated loop nest body.
  if (BodyEnter)
    redirectTo(BodyEnter, InnerEnter, DL);
  else
    redirectAllPredecessorsTo(BodyEntered, InnerEnter, DL);
  redirectAllPredecessorsTo(InnerLatch, Continue, DL);

  // Replace the original induction variable with an induction variable computed
  // from the tile and floor induction variables.
  Builder.restoreIP(Result.back()->getBodyIP());
  for (int i = 0; i < NumLoops; ++i) {
    CanonicalLoopInfo *FloorLoop = Result[i];
    CanonicalLoopInfo *TileLoop = Result[NumLoops + i];
    Value *OrigIndVar = OrigIndVars[i];
    Value *Size = TileSizes[i];

    Value *Scale =
        Builder.CreateMul(Size, FloorLoop->getIndVar(), {}, /*HasNUW=*/true);
    Value *Shift =
        Builder.CreateAdd(Scale, TileLoop->getIndVar(), {}, /*HasNUW=*/true);
    OrigIndVar->replaceAllUsesWith(Shift);
  }

  // Remove unused parts of the original loops.
  removeUnusedBlocksFromParent(OldControlBBs);

  for (CanonicalLoopInfo *L : Loops)
    L->invalidate();

#ifndef NDEBUG
  for (CanonicalLoopInfo *GenL : Result)
    GenL->assertOK();
#endif
  return Result;
}

/// Attach metadata \p Properties to the basic block described by \p BB. If the
/// basic block already has metadata, the basic block properties are appended.
static void addBasicBlockMetadata(BasicBlock *BB,
                                  ArrayRef<Metadata *> Properties) {
  // Nothing to do if no property to attach.
  if (Properties.empty())
    return;

  LLVMContext &Ctx = BB->getContext();
  SmallVector<Metadata *> NewProperties;
  NewProperties.push_back(nullptr);

  // If the basic block already has metadata, prepend it to the new metadata.
  MDNode *Existing = BB->getTerminator()->getMetadata(LLVMContext::MD_loop);
  if (Existing)
    append_range(NewProperties, drop_begin(Existing->operands(), 1));

  append_range(NewProperties, Properties);
  MDNode *BasicBlockID = MDNode::getDistinct(Ctx, NewProperties);
  BasicBlockID->replaceOperandWith(0, BasicBlockID);

  BB->getTerminator()->setMetadata(LLVMContext::MD_loop, BasicBlockID);
}

/// Attach loop metadata \p Properties to the loop described by \p Loop. If the
/// loop already has metadata, the loop properties are appended.
static void addLoopMetadata(CanonicalLoopInfo *Loop,
                            ArrayRef<Metadata *> Properties) {
  assert(Loop->isValid() && "Expecting a valid CanonicalLoopInfo");

  // Attach metadata to the loop's latch
  BasicBlock *Latch = Loop->getLatch();
  assert(Latch && "A valid CanonicalLoopInfo must have a unique latch");
  addBasicBlockMetadata(Latch, Properties);
}

/// Attach llvm.access.group metadata to the memref instructions of \p Block
static void addSimdMetadata(BasicBlock *Block, MDNode *AccessGroup,
                            LoopInfo &LI) {
  for (Instruction &I : *Block) {
    if (I.mayReadOrWriteMemory()) {
      // TODO: This instruction may already have access group from
      // other pragmas e.g. #pragma clang loop vectorize.  Append
      // so that the existing metadata is not overwritten.
      I.setMetadata(LLVMContext::MD_access_group, AccessGroup);
    }
  }
}

void OpenMPIRBuilder::unrollLoopFull(DebugLoc, CanonicalLoopInfo *Loop) {
  LLVMContext &Ctx = Builder.getContext();
  addLoopMetadata(
      Loop, {MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.enable")),
             MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.full"))});
}

void OpenMPIRBuilder::unrollLoopHeuristic(DebugLoc, CanonicalLoopInfo *Loop) {
  LLVMContext &Ctx = Builder.getContext();
  addLoopMetadata(
      Loop, {
                MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.enable")),
            });
}

void OpenMPIRBuilder::createIfVersion(CanonicalLoopInfo *CanonicalLoop,
                                      Value *IfCond, ValueToValueMapTy &VMap,
                                      const Twine &NamePrefix) {
  Function *F = CanonicalLoop->getFunction();

  // Define where if branch should be inserted
  Instruction *SplitBefore;
  if (Instruction::classof(IfCond)) {
    SplitBefore = dyn_cast<Instruction>(IfCond);
  } else {
    SplitBefore = CanonicalLoop->getPreheader()->getTerminator();
  }

  // TODO: We should not rely on pass manager. Currently we use pass manager
  // only for getting llvm::Loop which corresponds to given CanonicalLoopInfo
  // object. We should have a method  which returns all blocks between
  // CanonicalLoopInfo::getHeader() and CanonicalLoopInfo::getAfter()
  FunctionAnalysisManager FAM;
  FAM.registerPass([]() { return DominatorTreeAnalysis(); });
  FAM.registerPass([]() { return LoopAnalysis(); });
  FAM.registerPass([]() { return PassInstrumentationAnalysis(); });

  // Get the loop which needs to be cloned
  LoopAnalysis LIA;
  LoopInfo &&LI = LIA.run(*F, FAM);
  Loop *L = LI.getLoopFor(CanonicalLoop->getHeader());

  // Create additional blocks for the if statement
  BasicBlock *Head = SplitBefore->getParent();
  Instruction *HeadOldTerm = Head->getTerminator();
  llvm::LLVMContext &C = Head->getContext();
  llvm::BasicBlock *ThenBlock = llvm::BasicBlock::Create(
      C, NamePrefix + ".if.then", Head->getParent(), Head->getNextNode());
  llvm::BasicBlock *ElseBlock = llvm::BasicBlock::Create(
      C, NamePrefix + ".if.else", Head->getParent(), CanonicalLoop->getExit());

  // Create if condition branch.
  Builder.SetInsertPoint(HeadOldTerm);
  Instruction *BrInstr =
      Builder.CreateCondBr(IfCond, ThenBlock, /*ifFalse*/ ElseBlock);
  InsertPointTy IP{BrInstr->getParent(), ++BrInstr->getIterator()};
  // Then block contains branch to omp loop which needs to be vectorized
  spliceBB(IP, ThenBlock, false);
  ThenBlock->replaceSuccessorsPhiUsesWith(Head, ThenBlock);

  Builder.SetInsertPoint(ElseBlock);

  // Clone loop for the else branch
  SmallVector<BasicBlock *, 8> NewBlocks;

  VMap[CanonicalLoop->getPreheader()] = ElseBlock;
  for (BasicBlock *Block : L->getBlocks()) {
    BasicBlock *NewBB = CloneBasicBlock(Block, VMap, "", F);
    NewBB->moveBefore(CanonicalLoop->getExit());
    VMap[Block] = NewBB;
    NewBlocks.push_back(NewBB);
  }
  remapInstructionsInBlocks(NewBlocks, VMap);
  Builder.CreateBr(NewBlocks.front());
}

unsigned
OpenMPIRBuilder::getOpenMPDefaultSimdAlign(const Triple &TargetTriple,
                                           const StringMap<bool> &Features) {
  if (TargetTriple.isX86()) {
    if (Features.lookup("avx512f"))
      return 512;
    else if (Features.lookup("avx"))
      return 256;
    return 128;
  }
  if (TargetTriple.isPPC())
    return 128;
  if (TargetTriple.isWasm())
    return 128;
  return 0;
}

void OpenMPIRBuilder::applySimd(CanonicalLoopInfo *CanonicalLoop,
                                MapVector<Value *, Value *> AlignedVars,
                                Value *IfCond, OrderKind Order,
                                ConstantInt *Simdlen, ConstantInt *Safelen) {
  LLVMContext &Ctx = Builder.getContext();

  Function *F = CanonicalLoop->getFunction();

  // TODO: We should not rely on pass manager. Currently we use pass manager
  // only for getting llvm::Loop which corresponds to given CanonicalLoopInfo
  // object. We should have a method  which returns all blocks between
  // CanonicalLoopInfo::getHeader() and CanonicalLoopInfo::getAfter()
  FunctionAnalysisManager FAM;
  FAM.registerPass([]() { return DominatorTreeAnalysis(); });
  FAM.registerPass([]() { return LoopAnalysis(); });
  FAM.registerPass([]() { return PassInstrumentationAnalysis(); });

  LoopAnalysis LIA;
  LoopInfo &&LI = LIA.run(*F, FAM);

  Loop *L = LI.getLoopFor(CanonicalLoop->getHeader());
  if (AlignedVars.size()) {
    InsertPointTy IP = Builder.saveIP();
    Builder.SetInsertPoint(CanonicalLoop->getPreheader()->getTerminator());
    for (auto &AlignedItem : AlignedVars) {
      Value *AlignedPtr = AlignedItem.first;
      Value *Alignment = AlignedItem.second;
      Builder.CreateAlignmentAssumption(F->getDataLayout(),
                                        AlignedPtr, Alignment);
    }
    Builder.restoreIP(IP);
  }

  if (IfCond) {
    ValueToValueMapTy VMap;
    createIfVersion(CanonicalLoop, IfCond, VMap, "simd");
    // Add metadata to the cloned loop which disables vectorization
    Value *MappedLatch = VMap.lookup(CanonicalLoop->getLatch());
    assert(MappedLatch &&
           "Cannot find value which corresponds to original loop latch");
    assert(isa<BasicBlock>(MappedLatch) &&
           "Cannot cast mapped latch block value to BasicBlock");
    BasicBlock *NewLatchBlock = dyn_cast<BasicBlock>(MappedLatch);
    ConstantAsMetadata *BoolConst =
        ConstantAsMetadata::get(ConstantInt::getFalse(Type::getInt1Ty(Ctx)));
    addBasicBlockMetadata(
        NewLatchBlock,
        {MDNode::get(Ctx, {MDString::get(Ctx, "llvm.loop.vectorize.enable"),
                           BoolConst})});
  }

  SmallSet<BasicBlock *, 8> Reachable;

  // Get the basic blocks from the loop in which memref instructions
  // can be found.
  // TODO: Generalize getting all blocks inside a CanonicalizeLoopInfo,
  // preferably without running any passes.
  for (BasicBlock *Block : L->getBlocks()) {
    if (Block == CanonicalLoop->getCond() ||
        Block == CanonicalLoop->getHeader())
      continue;
    Reachable.insert(Block);
  }

  SmallVector<Metadata *> LoopMDList;

  // In presence of finite 'safelen', it may be unsafe to mark all
  // the memory instructions parallel, because loop-carried
  // dependences of 'safelen' iterations are possible.
  // If clause order(concurrent) is specified then the memory instructions
  // are marked parallel even if 'safelen' is finite.
  if ((Safelen == nullptr) || (Order == OrderKind::OMP_ORDER_concurrent)) {
    // Add access group metadata to memory-access instructions.
    MDNode *AccessGroup = MDNode::getDistinct(Ctx, {});
    for (BasicBlock *BB : Reachable)
      addSimdMetadata(BB, AccessGroup, LI);
    // TODO:  If the loop has existing parallel access metadata, have
    // to combine two lists.
    LoopMDList.push_back(MDNode::get(
        Ctx, {MDString::get(Ctx, "llvm.loop.parallel_accesses"), AccessGroup}));
  }

  // Use the above access group metadata to create loop level
  // metadata, which should be distinct for each loop.
  ConstantAsMetadata *BoolConst =
      ConstantAsMetadata::get(ConstantInt::getTrue(Type::getInt1Ty(Ctx)));
  LoopMDList.push_back(MDNode::get(
      Ctx, {MDString::get(Ctx, "llvm.loop.vectorize.enable"), BoolConst}));

  if (Simdlen || Safelen) {
    // If both simdlen and safelen clauses are specified, the value of the
    // simdlen parameter must be less than or equal to the value of the safelen
    // parameter. Therefore, use safelen only in the absence of simdlen.
    ConstantInt *VectorizeWidth = Simdlen == nullptr ? Safelen : Simdlen;
    LoopMDList.push_back(
        MDNode::get(Ctx, {MDString::get(Ctx, "llvm.loop.vectorize.width"),
                          ConstantAsMetadata::get(VectorizeWidth)}));
  }

  addLoopMetadata(CanonicalLoop, LoopMDList);
}

/// Create the TargetMachine object to query the backend for optimization
/// preferences.
///
/// Ideally, this would be passed from the front-end to the OpenMPBuilder, but
/// e.g. Clang does not pass it to its CodeGen layer and creates it only when
/// needed for the LLVM pass pipline. We use some default options to avoid
/// having to pass too many settings from the frontend that probably do not
/// matter.
///
/// Currently, TargetMachine is only used sometimes by the unrollLoopPartial
/// method. If we are going to use TargetMachine for more purposes, especially
/// those that are sensitive to TargetOptions, RelocModel and CodeModel, it
/// might become be worth requiring front-ends to pass on their TargetMachine,
/// or at least cache it between methods. Note that while fontends such as Clang
/// have just a single main TargetMachine per translation unit, "target-cpu" and
/// "target-features" that determine the TargetMachine are per-function and can
/// be overrided using __attribute__((target("OPTIONS"))).
static std::unique_ptr<TargetMachine>
createTargetMachine(Function *F, CodeGenOptLevel OptLevel) {
  Module *M = F->getParent();

  StringRef CPU = F->getFnAttribute("target-cpu").getValueAsString();
  StringRef Features = F->getFnAttribute("target-features").getValueAsString();
  const std::string &Triple = M->getTargetTriple();

  std::string Error;
  const llvm::Target *TheTarget = TargetRegistry::lookupTarget(Triple, Error);
  if (!TheTarget)
    return {};

  llvm::TargetOptions Options;
  return std::unique_ptr<TargetMachine>(TheTarget->createTargetMachine(
      Triple, CPU, Features, Options, /*RelocModel=*/std::nullopt,
      /*CodeModel=*/std::nullopt, OptLevel));
}

/// Heuristically determine the best-performant unroll factor for \p CLI. This
/// depends on the target processor. We are re-using the same heuristics as the
/// LoopUnrollPass.
static int32_t computeHeuristicUnrollFactor(CanonicalLoopInfo *CLI) {
  Function *F = CLI->getFunction();

  // Assume the user requests the most aggressive unrolling, even if the rest of
  // the code is optimized using a lower setting.
  CodeGenOptLevel OptLevel = CodeGenOptLevel::Aggressive;
  std::unique_ptr<TargetMachine> TM = createTargetMachine(F, OptLevel);

  FunctionAnalysisManager FAM;
  FAM.registerPass([]() { return TargetLibraryAnalysis(); });
  FAM.registerPass([]() { return AssumptionAnalysis(); });
  FAM.registerPass([]() { return DominatorTreeAnalysis(); });
  FAM.registerPass([]() { return LoopAnalysis(); });
  FAM.registerPass([]() { return ScalarEvolutionAnalysis(); });
  FAM.registerPass([]() { return PassInstrumentationAnalysis(); });
  TargetIRAnalysis TIRA;
  if (TM)
    TIRA = TargetIRAnalysis(
        [&](const Function &F) { return TM->getTargetTransformInfo(F); });
  FAM.registerPass([&]() { return TIRA; });

  TargetIRAnalysis::Result &&TTI = TIRA.run(*F, FAM);
  ScalarEvolutionAnalysis SEA;
  ScalarEvolution &&SE = SEA.run(*F, FAM);
  DominatorTreeAnalysis DTA;
  DominatorTree &&DT = DTA.run(*F, FAM);
  LoopAnalysis LIA;
  LoopInfo &&LI = LIA.run(*F, FAM);
  AssumptionAnalysis ACT;
  AssumptionCache &&AC = ACT.run(*F, FAM);
  OptimizationRemarkEmitter ORE{F};

  Loop *L = LI.getLoopFor(CLI->getHeader());
  assert(L && "Expecting CanonicalLoopInfo to be recognized as a loop");

  TargetTransformInfo::UnrollingPreferences UP =
      gatherUnrollingPreferences(L, SE, TTI,
                                 /*BlockFrequencyInfo=*/nullptr,
                                 /*ProfileSummaryInfo=*/nullptr, ORE, static_cast<int>(OptLevel),
                                 /*UserThreshold=*/std::nullopt,
                                 /*UserCount=*/std::nullopt,
                                 /*UserAllowPartial=*/true,
                                 /*UserAllowRuntime=*/true,
                                 /*UserUpperBound=*/std::nullopt,
                                 /*UserFullUnrollMaxCount=*/std::nullopt);

  UP.Force = true;

  // Account for additional optimizations taking place before the LoopUnrollPass
  // would unroll the loop.
  UP.Threshold *= UnrollThresholdFactor;
  UP.PartialThreshold *= UnrollThresholdFactor;

  // Use normal unroll factors even if the rest of the code is optimized for
  // size.
  UP.OptSizeThreshold = UP.Threshold;
  UP.PartialOptSizeThreshold = UP.PartialThreshold;

  LLVM_DEBUG(dbgs() << "Unroll heuristic thresholds:\n"
                    << "  Threshold=" << UP.Threshold << "\n"
                    << "  PartialThreshold=" << UP.PartialThreshold << "\n"
                    << "  OptSizeThreshold=" << UP.OptSizeThreshold << "\n"
                    << "  PartialOptSizeThreshold="
                    << UP.PartialOptSizeThreshold << "\n");

  // Disable peeling.
  TargetTransformInfo::PeelingPreferences PP =
      gatherPeelingPreferences(L, SE, TTI,
                               /*UserAllowPeeling=*/false,
                               /*UserAllowProfileBasedPeeling=*/false,
                               /*UnrollingSpecficValues=*/false);

  SmallPtrSet<const Value *, 32> EphValues;
  CodeMetrics::collectEphemeralValues(L, &AC, EphValues);

  // Assume that reads and writes to stack variables can be eliminated by
  // Mem2Reg, SROA or LICM. That is, don't count them towards the loop body's
  // size.
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      Value *Ptr;
      if (auto *Load = dyn_cast<LoadInst>(&I)) {
        Ptr = Load->getPointerOperand();
      } else if (auto *Store = dyn_cast<StoreInst>(&I)) {
        Ptr = Store->getPointerOperand();
      } else
        continue;

      Ptr = Ptr->stripPointerCasts();

      if (auto *Alloca = dyn_cast<AllocaInst>(Ptr)) {
        if (Alloca->getParent() == &F->getEntryBlock())
          EphValues.insert(&I);
      }
    }
  }

  UnrollCostEstimator UCE(L, TTI, EphValues, UP.BEInsns);

  // Loop is not unrollable if the loop contains certain instructions.
  if (!UCE.canUnroll()) {
    LLVM_DEBUG(dbgs() << "Loop not considered unrollable\n");
    return 1;
  }

  LLVM_DEBUG(dbgs() << "Estimated loop size is " << UCE.getRolledLoopSize()
                    << "\n");

  // TODO: Determine trip count of \p CLI if constant, computeUnrollCount might
  // be able to use it.
  int TripCount = 0;
  int MaxTripCount = 0;
  bool MaxOrZero = false;
  unsigned TripMultiple = 0;

  bool UseUpperBound = false;
  computeUnrollCount(L, TTI, DT, &LI, &AC, SE, EphValues, &ORE, TripCount,
                     MaxTripCount, MaxOrZero, TripMultiple, UCE, UP, PP,
                     UseUpperBound);
  unsigned Factor = UP.Count;
  LLVM_DEBUG(dbgs() << "Suggesting unroll factor of " << Factor << "\n");

  // This function returns 1 to signal to not unroll a loop.
  if (Factor == 0)
    return 1;
  return Factor;
}

void OpenMPIRBuilder::unrollLoopPartial(DebugLoc DL, CanonicalLoopInfo *Loop,
                                        int32_t Factor,
                                        CanonicalLoopInfo **UnrolledCLI) {
  assert(Factor >= 0 && "Unroll factor must not be negative");

  Function *F = Loop->getFunction();
  LLVMContext &Ctx = F->getContext();

  // If the unrolled loop is not used for another loop-associated directive, it
  // is sufficient to add metadata for the LoopUnrollPass.
  if (!UnrolledCLI) {
    SmallVector<Metadata *, 2> LoopMetadata;
    LoopMetadata.push_back(
        MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.enable")));

    if (Factor >= 1) {
      ConstantAsMetadata *FactorConst = ConstantAsMetadata::get(
          ConstantInt::get(Type::getInt32Ty(Ctx), APInt(32, Factor)));
      LoopMetadata.push_back(MDNode::get(
          Ctx, {MDString::get(Ctx, "llvm.loop.unroll.count"), FactorConst}));
    }

    addLoopMetadata(Loop, LoopMetadata);
    return;
  }

  // Heuristically determine the unroll factor.
  if (Factor == 0)
    Factor = computeHeuristicUnrollFactor(Loop);

  // No change required with unroll factor 1.
  if (Factor == 1) {
    *UnrolledCLI = Loop;
    return;
  }

  assert(Factor >= 2 &&
         "unrolling only makes sense with a factor of 2 or larger");

  Type *IndVarTy = Loop->getIndVarType();

  // Apply partial unrolling by tiling the loop by the unroll-factor, then fully
  // unroll the inner loop.
  Value *FactorVal =
      ConstantInt::get(IndVarTy, APInt(IndVarTy->getIntegerBitWidth(), Factor,
                                       /*isSigned=*/false));
  std::vector<CanonicalLoopInfo *> LoopNest =
      tileLoops(DL, {Loop}, {FactorVal});
  assert(LoopNest.size() == 2 && "Expect 2 loops after tiling");
  *UnrolledCLI = LoopNest[0];
  CanonicalLoopInfo *InnerLoop = LoopNest[1];

  // LoopUnrollPass can only fully unroll loops with constant trip count.
  // Unroll by the unroll factor with a fallback epilog for the remainder
  // iterations if necessary.
  ConstantAsMetadata *FactorConst = ConstantAsMetadata::get(
      ConstantInt::get(Type::getInt32Ty(Ctx), APInt(32, Factor)));
  addLoopMetadata(
      InnerLoop,
      {MDNode::get(Ctx, MDString::get(Ctx, "llvm.loop.unroll.enable")),
       MDNode::get(
           Ctx, {MDString::get(Ctx, "llvm.loop.unroll.count"), FactorConst})});

#ifndef NDEBUG
  (*UnrolledCLI)->assertOK();
#endif
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createCopyPrivate(const LocationDescription &Loc,
                                   llvm::Value *BufSize, llvm::Value *CpyBuf,
                                   llvm::Value *CpyFn, llvm::Value *DidIt) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);

  llvm::Value *DidItLD = Builder.CreateLoad(Builder.getInt32Ty(), DidIt);

  Value *Args[] = {Ident, ThreadId, BufSize, CpyBuf, CpyFn, DidItLD};

  Function *Fn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_copyprivate);
  Builder.CreateCall(Fn, Args);

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createSingle(
    const LocationDescription &Loc, BodyGenCallbackTy BodyGenCB,
    FinalizeCallbackTy FiniCB, bool IsNowait, ArrayRef<llvm::Value *> CPVars,
    ArrayRef<llvm::Function *> CPFuncs) {

  if (!updateToLocation(Loc))
    return Loc.IP;

  // If needed allocate and initialize `DidIt` with 0.
  // DidIt: flag variable: 1=single thread; 0=not single thread.
  llvm::Value *DidIt = nullptr;
  if (!CPVars.empty()) {
    DidIt = Builder.CreateAlloca(llvm::Type::getInt32Ty(Builder.getContext()));
    Builder.CreateStore(Builder.getInt32(0), DidIt);
  }

  Directive OMPD = Directive::OMPD_single;
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Value *Args[] = {Ident, ThreadId};

  Function *EntryRTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_single);
  Instruction *EntryCall = Builder.CreateCall(EntryRTLFn, Args);

  Function *ExitRTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_end_single);
  Instruction *ExitCall = Builder.CreateCall(ExitRTLFn, Args);

  auto FiniCBWrapper = [&](InsertPointTy IP) {
    FiniCB(IP);

    // The thread that executes the single region must set `DidIt` to 1.
    // This is used by __kmpc_copyprivate, to know if the caller is the
    // single thread or not.
    if (DidIt)
      Builder.CreateStore(Builder.getInt32(1), DidIt);
  };

  // generates the following:
  // if (__kmpc_single()) {
  //		.... single region ...
  // 		__kmpc_end_single
  // }
  // __kmpc_copyprivate
  // __kmpc_barrier

  EmitOMPInlinedRegion(OMPD, EntryCall, ExitCall, BodyGenCB, FiniCBWrapper,
                       /*Conditional*/ true,
                       /*hasFinalize*/ true);

  if (DidIt) {
    for (size_t I = 0, E = CPVars.size(); I < E; ++I)
      // NOTE BufSize is currently unused, so just pass 0.
      createCopyPrivate(LocationDescription(Builder.saveIP(), Loc.DL),
                        /*BufSize=*/ConstantInt::get(Int64, 0), CPVars[I],
                        CPFuncs[I], DidIt);
    // NOTE __kmpc_copyprivate already inserts a barrier
  } else if (!IsNowait)
    createBarrier(LocationDescription(Builder.saveIP(), Loc.DL),
                  omp::Directive::OMPD_unknown, /* ForceSimpleCall */ false,
                  /* CheckCancelFlag */ false);
  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createCritical(
    const LocationDescription &Loc, BodyGenCallbackTy BodyGenCB,
    FinalizeCallbackTy FiniCB, StringRef CriticalName, Value *HintInst) {

  if (!updateToLocation(Loc))
    return Loc.IP;

  Directive OMPD = Directive::OMPD_critical;
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Value *LockVar = getOMPCriticalRegionLock(CriticalName);
  Value *Args[] = {Ident, ThreadId, LockVar};

  SmallVector<llvm::Value *, 4> EnterArgs(std::begin(Args), std::end(Args));
  Function *RTFn = nullptr;
  if (HintInst) {
    // Add Hint to entry Args and create call
    EnterArgs.push_back(HintInst);
    RTFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_critical_with_hint);
  } else {
    RTFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_critical);
  }
  Instruction *EntryCall = Builder.CreateCall(RTFn, EnterArgs);

  Function *ExitRTLFn =
      getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_end_critical);
  Instruction *ExitCall = Builder.CreateCall(ExitRTLFn, Args);

  return EmitOMPInlinedRegion(OMPD, EntryCall, ExitCall, BodyGenCB, FiniCB,
                              /*Conditional*/ false, /*hasFinalize*/ true);
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createOrderedDepend(const LocationDescription &Loc,
                                     InsertPointTy AllocaIP, unsigned NumLoops,
                                     ArrayRef<llvm::Value *> StoreValues,
                                     const Twine &Name, bool IsDependSource) {
  assert(
      llvm::all_of(StoreValues,
                   [](Value *SV) { return SV->getType()->isIntegerTy(64); }) &&
      "OpenMP runtime requires depend vec with i64 type");

  if (!updateToLocation(Loc))
    return Loc.IP;

  // Allocate space for vector and generate alloc instruction.
  auto *ArrI64Ty = ArrayType::get(Int64, NumLoops);
  Builder.restoreIP(AllocaIP);
  AllocaInst *ArgsBase = Builder.CreateAlloca(ArrI64Ty, nullptr, Name);
  ArgsBase->setAlignment(Align(8));
  Builder.restoreIP(Loc.IP);

  // Store the index value with offset in depend vector.
  for (unsigned I = 0; I < NumLoops; ++I) {
    Value *DependAddrGEPIter = Builder.CreateInBoundsGEP(
        ArrI64Ty, ArgsBase, {Builder.getInt64(0), Builder.getInt64(I)});
    StoreInst *STInst = Builder.CreateStore(StoreValues[I], DependAddrGEPIter);
    STInst->setAlignment(Align(8));
  }

  Value *DependBaseAddrGEP = Builder.CreateInBoundsGEP(
      ArrI64Ty, ArgsBase, {Builder.getInt64(0), Builder.getInt64(0)});

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Value *Args[] = {Ident, ThreadId, DependBaseAddrGEP};

  Function *RTLFn = nullptr;
  if (IsDependSource)
    RTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_doacross_post);
  else
    RTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_doacross_wait);
  Builder.CreateCall(RTLFn, Args);

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createOrderedThreadsSimd(
    const LocationDescription &Loc, BodyGenCallbackTy BodyGenCB,
    FinalizeCallbackTy FiniCB, bool IsThreads) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  Directive OMPD = Directive::OMPD_ordered;
  Instruction *EntryCall = nullptr;
  Instruction *ExitCall = nullptr;

  if (IsThreads) {
    uint32_t SrcLocStrSize;
    Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
    Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
    Value *ThreadId = getOrCreateThreadID(Ident);
    Value *Args[] = {Ident, ThreadId};

    Function *EntryRTLFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_ordered);
    EntryCall = Builder.CreateCall(EntryRTLFn, Args);

    Function *ExitRTLFn =
        getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_end_ordered);
    ExitCall = Builder.CreateCall(ExitRTLFn, Args);
  }

  return EmitOMPInlinedRegion(OMPD, EntryCall, ExitCall, BodyGenCB, FiniCB,
                              /*Conditional*/ false, /*hasFinalize*/ true);
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::EmitOMPInlinedRegion(
    Directive OMPD, Instruction *EntryCall, Instruction *ExitCall,
    BodyGenCallbackTy BodyGenCB, FinalizeCallbackTy FiniCB, bool Conditional,
    bool HasFinalize, bool IsCancellable) {

  if (HasFinalize)
    FinalizationStack.push_back({FiniCB, OMPD, IsCancellable});

  // Create inlined region's entry and body blocks, in preparation
  // for conditional creation
  BasicBlock *EntryBB = Builder.GetInsertBlock();
  Instruction *SplitPos = EntryBB->getTerminator();
  if (!isa_and_nonnull<BranchInst>(SplitPos))
    SplitPos = new UnreachableInst(Builder.getContext(), EntryBB);
  BasicBlock *ExitBB = EntryBB->splitBasicBlock(SplitPos, "omp_region.end");
  BasicBlock *FiniBB =
      EntryBB->splitBasicBlock(EntryBB->getTerminator(), "omp_region.finalize");

  Builder.SetInsertPoint(EntryBB->getTerminator());
  emitCommonDirectiveEntry(OMPD, EntryCall, ExitBB, Conditional);

  // generate body
  BodyGenCB(/* AllocaIP */ InsertPointTy(),
            /* CodeGenIP */ Builder.saveIP());

  // emit exit call and do any needed finalization.
  auto FinIP = InsertPointTy(FiniBB, FiniBB->getFirstInsertionPt());
  assert(FiniBB->getTerminator()->getNumSuccessors() == 1 &&
         FiniBB->getTerminator()->getSuccessor(0) == ExitBB &&
         "Unexpected control flow graph state!!");
  emitCommonDirectiveExit(OMPD, FinIP, ExitCall, HasFinalize);
  assert(FiniBB->getUniquePredecessor()->getUniqueSuccessor() == FiniBB &&
         "Unexpected Control Flow State!");
  MergeBlockIntoPredecessor(FiniBB);

  // If we are skipping the region of a non conditional, remove the exit
  // block, and clear the builder's insertion point.
  assert(SplitPos->getParent() == ExitBB &&
         "Unexpected Insertion point location!");
  auto merged = MergeBlockIntoPredecessor(ExitBB);
  BasicBlock *ExitPredBB = SplitPos->getParent();
  auto InsertBB = merged ? ExitPredBB : ExitBB;
  if (!isa_and_nonnull<BranchInst>(SplitPos))
    SplitPos->eraseFromParent();
  Builder.SetInsertPoint(InsertBB);

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::emitCommonDirectiveEntry(
    Directive OMPD, Value *EntryCall, BasicBlock *ExitBB, bool Conditional) {
  // if nothing to do, Return current insertion point.
  if (!Conditional || !EntryCall)
    return Builder.saveIP();

  BasicBlock *EntryBB = Builder.GetInsertBlock();
  Value *CallBool = Builder.CreateIsNotNull(EntryCall);
  auto *ThenBB = BasicBlock::Create(M.getContext(), "omp_region.body");
  auto *UI = new UnreachableInst(Builder.getContext(), ThenBB);

  // Emit thenBB and set the Builder's insertion point there for
  // body generation next. Place the block after the current block.
  Function *CurFn = EntryBB->getParent();
  CurFn->insert(std::next(EntryBB->getIterator()), ThenBB);

  // Move Entry branch to end of ThenBB, and replace with conditional
  // branch (If-stmt)
  Instruction *EntryBBTI = EntryBB->getTerminator();
  Builder.CreateCondBr(CallBool, ThenBB, ExitBB);
  EntryBBTI->removeFromParent();
  Builder.SetInsertPoint(UI);
  Builder.Insert(EntryBBTI);
  UI->eraseFromParent();
  Builder.SetInsertPoint(ThenBB->getTerminator());

  // return an insertion point to ExitBB.
  return IRBuilder<>::InsertPoint(ExitBB, ExitBB->getFirstInsertionPt());
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::emitCommonDirectiveExit(
    omp::Directive OMPD, InsertPointTy FinIP, Instruction *ExitCall,
    bool HasFinalize) {

  Builder.restoreIP(FinIP);

  // If there is finalization to do, emit it before the exit call
  if (HasFinalize) {
    assert(!FinalizationStack.empty() &&
           "Unexpected finalization stack state!");

    FinalizationInfo Fi = FinalizationStack.pop_back_val();
    assert(Fi.DK == OMPD && "Unexpected Directive for Finalization call!");

    Fi.FiniCB(FinIP);

    BasicBlock *FiniBB = FinIP.getBlock();
    Instruction *FiniBBTI = FiniBB->getTerminator();

    // set Builder IP for call creation
    Builder.SetInsertPoint(FiniBBTI);
  }

  if (!ExitCall)
    return Builder.saveIP();

  // place the Exitcall as last instruction before Finalization block terminator
  ExitCall->removeFromParent();
  Builder.Insert(ExitCall);

  return IRBuilder<>::InsertPoint(ExitCall->getParent(),
                                  ExitCall->getIterator());
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createCopyinClauseBlocks(
    InsertPointTy IP, Value *MasterAddr, Value *PrivateAddr,
    llvm::IntegerType *IntPtrTy, bool BranchtoEnd) {
  if (!IP.isSet())
    return IP;

  IRBuilder<>::InsertPointGuard IPG(Builder);

  // creates the following CFG structure
  //	   OMP_Entry : (MasterAddr != PrivateAddr)?
  //       F     T
  //       |      \
  //       |     copin.not.master
  //       |      /
  //       v     /
  //   copyin.not.master.end
  //		     |
  //         v
  //   OMP.Entry.Next

  BasicBlock *OMP_Entry = IP.getBlock();
  Function *CurFn = OMP_Entry->getParent();
  BasicBlock *CopyBegin =
      BasicBlock::Create(M.getContext(), "copyin.not.master", CurFn);
  BasicBlock *CopyEnd = nullptr;

  // If entry block is terminated, split to preserve the branch to following
  // basic block (i.e. OMP.Entry.Next), otherwise, leave everything as is.
  if (isa_and_nonnull<BranchInst>(OMP_Entry->getTerminator())) {
    CopyEnd = OMP_Entry->splitBasicBlock(OMP_Entry->getTerminator(),
                                         "copyin.not.master.end");
    OMP_Entry->getTerminator()->eraseFromParent();
  } else {
    CopyEnd =
        BasicBlock::Create(M.getContext(), "copyin.not.master.end", CurFn);
  }

  Builder.SetInsertPoint(OMP_Entry);
  Value *MasterPtr = Builder.CreatePtrToInt(MasterAddr, IntPtrTy);
  Value *PrivatePtr = Builder.CreatePtrToInt(PrivateAddr, IntPtrTy);
  Value *cmp = Builder.CreateICmpNE(MasterPtr, PrivatePtr);
  Builder.CreateCondBr(cmp, CopyBegin, CopyEnd);

  Builder.SetInsertPoint(CopyBegin);
  if (BranchtoEnd)
    Builder.SetInsertPoint(Builder.CreateBr(CopyEnd));

  return Builder.saveIP();
}

CallInst *OpenMPIRBuilder::createOMPAlloc(const LocationDescription &Loc,
                                          Value *Size, Value *Allocator,
                                          std::string Name) {
  IRBuilder<>::InsertPointGuard IPG(Builder);
  updateToLocation(Loc);

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Value *Args[] = {ThreadId, Size, Allocator};

  Function *Fn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_alloc);

  return Builder.CreateCall(Fn, Args, Name);
}

CallInst *OpenMPIRBuilder::createOMPFree(const LocationDescription &Loc,
                                         Value *Addr, Value *Allocator,
                                         std::string Name) {
  IRBuilder<>::InsertPointGuard IPG(Builder);
  updateToLocation(Loc);

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Value *Args[] = {ThreadId, Addr, Allocator};
  Function *Fn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_free);
  return Builder.CreateCall(Fn, Args, Name);
}

CallInst *OpenMPIRBuilder::createOMPInteropInit(
    const LocationDescription &Loc, Value *InteropVar,
    omp::OMPInteropType InteropType, Value *Device, Value *NumDependences,
    Value *DependenceAddress, bool HaveNowaitClause) {
  IRBuilder<>::InsertPointGuard IPG(Builder);
  updateToLocation(Loc);

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  if (Device == nullptr)
    Device = ConstantInt::get(Int32, -1);
  Constant *InteropTypeVal = ConstantInt::get(Int32, (int)InteropType);
  if (NumDependences == nullptr) {
    NumDependences = ConstantInt::get(Int32, 0);
    PointerType *PointerTypeVar = PointerType::getUnqual(M.getContext());
    DependenceAddress = ConstantPointerNull::get(PointerTypeVar);
  }
  Value *HaveNowaitClauseVal = ConstantInt::get(Int32, HaveNowaitClause);
  Value *Args[] = {
      Ident,  ThreadId,       InteropVar,        InteropTypeVal,
      Device, NumDependences, DependenceAddress, HaveNowaitClauseVal};

  Function *Fn = getOrCreateRuntimeFunctionPtr(OMPRTL___tgt_interop_init);

  return Builder.CreateCall(Fn, Args);
}

CallInst *OpenMPIRBuilder::createOMPInteropDestroy(
    const LocationDescription &Loc, Value *InteropVar, Value *Device,
    Value *NumDependences, Value *DependenceAddress, bool HaveNowaitClause) {
  IRBuilder<>::InsertPointGuard IPG(Builder);
  updateToLocation(Loc);

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  if (Device == nullptr)
    Device = ConstantInt::get(Int32, -1);
  if (NumDependences == nullptr) {
    NumDependences = ConstantInt::get(Int32, 0);
    PointerType *PointerTypeVar = PointerType::getUnqual(M.getContext());
    DependenceAddress = ConstantPointerNull::get(PointerTypeVar);
  }
  Value *HaveNowaitClauseVal = ConstantInt::get(Int32, HaveNowaitClause);
  Value *Args[] = {
      Ident,          ThreadId,          InteropVar,         Device,
      NumDependences, DependenceAddress, HaveNowaitClauseVal};

  Function *Fn = getOrCreateRuntimeFunctionPtr(OMPRTL___tgt_interop_destroy);

  return Builder.CreateCall(Fn, Args);
}

CallInst *OpenMPIRBuilder::createOMPInteropUse(const LocationDescription &Loc,
                                               Value *InteropVar, Value *Device,
                                               Value *NumDependences,
                                               Value *DependenceAddress,
                                               bool HaveNowaitClause) {
  IRBuilder<>::InsertPointGuard IPG(Builder);
  updateToLocation(Loc);
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  if (Device == nullptr)
    Device = ConstantInt::get(Int32, -1);
  if (NumDependences == nullptr) {
    NumDependences = ConstantInt::get(Int32, 0);
    PointerType *PointerTypeVar = PointerType::getUnqual(M.getContext());
    DependenceAddress = ConstantPointerNull::get(PointerTypeVar);
  }
  Value *HaveNowaitClauseVal = ConstantInt::get(Int32, HaveNowaitClause);
  Value *Args[] = {
      Ident,          ThreadId,          InteropVar,         Device,
      NumDependences, DependenceAddress, HaveNowaitClauseVal};

  Function *Fn = getOrCreateRuntimeFunctionPtr(OMPRTL___tgt_interop_use);

  return Builder.CreateCall(Fn, Args);
}

CallInst *OpenMPIRBuilder::createCachedThreadPrivate(
    const LocationDescription &Loc, llvm::Value *Pointer,
    llvm::ConstantInt *Size, const llvm::Twine &Name) {
  IRBuilder<>::InsertPointGuard IPG(Builder);
  updateToLocation(Loc);

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Value *ThreadId = getOrCreateThreadID(Ident);
  Constant *ThreadPrivateCache =
      getOrCreateInternalVariable(Int8PtrPtr, Name.str());
  llvm::Value *Args[] = {Ident, ThreadId, Pointer, Size, ThreadPrivateCache};

  Function *Fn =
      getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_threadprivate_cached);

  return Builder.CreateCall(Fn, Args);
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createTargetInit(const LocationDescription &Loc, bool IsSPMD,
                                  int32_t MinThreadsVal, int32_t MaxThreadsVal,
                                  int32_t MinTeamsVal, int32_t MaxTeamsVal) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Constant *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Constant *IsSPMDVal = ConstantInt::getSigned(
      Int8, IsSPMD ? OMP_TGT_EXEC_MODE_SPMD : OMP_TGT_EXEC_MODE_GENERIC);
  Constant *UseGenericStateMachineVal = ConstantInt::getSigned(Int8, !IsSPMD);
  Constant *MayUseNestedParallelismVal = ConstantInt::getSigned(Int8, true);
  Constant *DebugIndentionLevelVal = ConstantInt::getSigned(Int16, 0);

  Function *Kernel = Builder.GetInsertBlock()->getParent();

  // Manifest the launch configuration in the metadata matching the kernel
  // environment.
  if (MinTeamsVal > 1 || MaxTeamsVal > 0)
    writeTeamsForKernel(T, *Kernel, MinTeamsVal, MaxTeamsVal);

  // For max values, < 0 means unset, == 0 means set but unknown.
  if (MaxThreadsVal < 0)
    MaxThreadsVal = std::max(
        int32_t(getGridValue(T, Kernel).GV_Default_WG_Size), MinThreadsVal);

  if (MaxThreadsVal > 0)
    writeThreadBoundsForKernel(T, *Kernel, MinThreadsVal, MaxThreadsVal);

  Constant *MinThreads = ConstantInt::getSigned(Int32, MinThreadsVal);
  Constant *MaxThreads = ConstantInt::getSigned(Int32, MaxThreadsVal);
  Constant *MinTeams = ConstantInt::getSigned(Int32, MinTeamsVal);
  Constant *MaxTeams = ConstantInt::getSigned(Int32, MaxTeamsVal);
  Constant *ReductionDataSize = ConstantInt::getSigned(Int32, 0);
  Constant *ReductionBufferLength = ConstantInt::getSigned(Int32, 0);

  // We need to strip the debug prefix to get the correct kernel name.
  StringRef KernelName = Kernel->getName();
  const std::string DebugPrefix = "_debug__";
  if (KernelName.ends_with(DebugPrefix))
    KernelName = KernelName.drop_back(DebugPrefix.length());

  Function *Fn = getOrCreateRuntimeFunctionPtr(
      omp::RuntimeFunction::OMPRTL___kmpc_target_init);
  const DataLayout &DL = Fn->getDataLayout();

  Twine DynamicEnvironmentName = KernelName + "_dynamic_environment";
  Constant *DynamicEnvironmentInitializer =
      ConstantStruct::get(DynamicEnvironment, {DebugIndentionLevelVal});
  GlobalVariable *DynamicEnvironmentGV = new GlobalVariable(
      M, DynamicEnvironment, /*IsConstant=*/false, GlobalValue::WeakODRLinkage,
      DynamicEnvironmentInitializer, DynamicEnvironmentName,
      /*InsertBefore=*/nullptr, GlobalValue::NotThreadLocal,
      DL.getDefaultGlobalsAddressSpace());
  DynamicEnvironmentGV->setVisibility(GlobalValue::ProtectedVisibility);

  Constant *DynamicEnvironment =
      DynamicEnvironmentGV->getType() == DynamicEnvironmentPtr
          ? DynamicEnvironmentGV
          : ConstantExpr::getAddrSpaceCast(DynamicEnvironmentGV,
                                           DynamicEnvironmentPtr);

  Constant *ConfigurationEnvironmentInitializer = ConstantStruct::get(
      ConfigurationEnvironment, {
                                    UseGenericStateMachineVal,
                                    MayUseNestedParallelismVal,
                                    IsSPMDVal,
                                    MinThreads,
                                    MaxThreads,
                                    MinTeams,
                                    MaxTeams,
                                    ReductionDataSize,
                                    ReductionBufferLength,
                                });
  Constant *KernelEnvironmentInitializer = ConstantStruct::get(
      KernelEnvironment, {
                             ConfigurationEnvironmentInitializer,
                             Ident,
                             DynamicEnvironment,
                         });
  std::string KernelEnvironmentName =
      (KernelName + "_kernel_environment").str();
  GlobalVariable *KernelEnvironmentGV = new GlobalVariable(
      M, KernelEnvironment, /*IsConstant=*/true, GlobalValue::WeakODRLinkage,
      KernelEnvironmentInitializer, KernelEnvironmentName,
      /*InsertBefore=*/nullptr, GlobalValue::NotThreadLocal,
      DL.getDefaultGlobalsAddressSpace());
  KernelEnvironmentGV->setVisibility(GlobalValue::ProtectedVisibility);

  Constant *KernelEnvironment =
      KernelEnvironmentGV->getType() == KernelEnvironmentPtr
          ? KernelEnvironmentGV
          : ConstantExpr::getAddrSpaceCast(KernelEnvironmentGV,
                                           KernelEnvironmentPtr);
  Value *KernelLaunchEnvironment = Kernel->getArg(0);
  CallInst *ThreadKind =
      Builder.CreateCall(Fn, {KernelEnvironment, KernelLaunchEnvironment});

  Value *ExecUserCode = Builder.CreateICmpEQ(
      ThreadKind, ConstantInt::get(ThreadKind->getType(), -1),
      "exec_user_code");

  // ThreadKind = __kmpc_target_init(...)
  // if (ThreadKind == -1)
  //   user_code
  // else
  //   return;

  auto *UI = Builder.CreateUnreachable();
  BasicBlock *CheckBB = UI->getParent();
  BasicBlock *UserCodeEntryBB = CheckBB->splitBasicBlock(UI, "user_code.entry");

  BasicBlock *WorkerExitBB = BasicBlock::Create(
      CheckBB->getContext(), "worker.exit", CheckBB->getParent());
  Builder.SetInsertPoint(WorkerExitBB);
  Builder.CreateRetVoid();

  auto *CheckBBTI = CheckBB->getTerminator();
  Builder.SetInsertPoint(CheckBBTI);
  Builder.CreateCondBr(ExecUserCode, UI->getParent(), WorkerExitBB);

  CheckBBTI->eraseFromParent();
  UI->eraseFromParent();

  // Continue in the "user_code" block, see diagram above and in
  // openmp/libomptarget/deviceRTLs/common/include/target.h .
  return InsertPointTy(UserCodeEntryBB, UserCodeEntryBB->getFirstInsertionPt());
}

void OpenMPIRBuilder::createTargetDeinit(const LocationDescription &Loc,
                                         int32_t TeamsReductionDataSize,
                                         int32_t TeamsReductionBufferLength) {
  if (!updateToLocation(Loc))
    return;

  Function *Fn = getOrCreateRuntimeFunctionPtr(
      omp::RuntimeFunction::OMPRTL___kmpc_target_deinit);

  Builder.CreateCall(Fn, {});

  if (!TeamsReductionBufferLength || !TeamsReductionDataSize)
    return;

  Function *Kernel = Builder.GetInsertBlock()->getParent();
  // We need to strip the debug prefix to get the correct kernel name.
  StringRef KernelName = Kernel->getName();
  const std::string DebugPrefix = "_debug__";
  if (KernelName.ends_with(DebugPrefix))
    KernelName = KernelName.drop_back(DebugPrefix.length());
  auto *KernelEnvironmentGV =
      M.getNamedGlobal((KernelName + "_kernel_environment").str());
  assert(KernelEnvironmentGV && "Expected kernel environment global\n");
  auto *KernelEnvironmentInitializer = KernelEnvironmentGV->getInitializer();
  auto *NewInitializer = ConstantFoldInsertValueInstruction(
      KernelEnvironmentInitializer,
      ConstantInt::get(Int32, TeamsReductionDataSize), {0, 7});
  NewInitializer = ConstantFoldInsertValueInstruction(
      NewInitializer, ConstantInt::get(Int32, TeamsReductionBufferLength),
      {0, 8});
  KernelEnvironmentGV->setInitializer(NewInitializer);
}

static MDNode *getNVPTXMDNode(Function &Kernel, StringRef Name) {
  Module &M = *Kernel.getParent();
  NamedMDNode *MD = M.getOrInsertNamedMetadata("nvvm.annotations");
  for (auto *Op : MD->operands()) {
    if (Op->getNumOperands() != 3)
      continue;
    auto *KernelOp = dyn_cast<ConstantAsMetadata>(Op->getOperand(0));
    if (!KernelOp || KernelOp->getValue() != &Kernel)
      continue;
    auto *Prop = dyn_cast<MDString>(Op->getOperand(1));
    if (!Prop || Prop->getString() != Name)
      continue;
    return Op;
  }
  return nullptr;
}

static void updateNVPTXMetadata(Function &Kernel, StringRef Name, int32_t Value,
                                bool Min) {
  // Update the "maxntidx" metadata for NVIDIA, or add it.
  MDNode *ExistingOp = getNVPTXMDNode(Kernel, Name);
  if (ExistingOp) {
    auto *OldVal = cast<ConstantAsMetadata>(ExistingOp->getOperand(2));
    int32_t OldLimit = cast<ConstantInt>(OldVal->getValue())->getZExtValue();
    ExistingOp->replaceOperandWith(
        2, ConstantAsMetadata::get(ConstantInt::get(
               OldVal->getValue()->getType(),
               Min ? std::min(OldLimit, Value) : std::max(OldLimit, Value))));
  } else {
    LLVMContext &Ctx = Kernel.getContext();
    Metadata *MDVals[] = {ConstantAsMetadata::get(&Kernel),
                          MDString::get(Ctx, Name),
                          ConstantAsMetadata::get(
                              ConstantInt::get(Type::getInt32Ty(Ctx), Value))};
    // Append metadata to nvvm.annotations
    Module &M = *Kernel.getParent();
    NamedMDNode *MD = M.getOrInsertNamedMetadata("nvvm.annotations");
    MD->addOperand(MDNode::get(Ctx, MDVals));
  }
}

std::pair<int32_t, int32_t>
OpenMPIRBuilder::readThreadBoundsForKernel(const Triple &T, Function &Kernel) {
  int32_t ThreadLimit =
      Kernel.getFnAttributeAsParsedInteger("omp_target_thread_limit");

  if (T.isAMDGPU()) {
    const auto &Attr = Kernel.getFnAttribute("amdgpu-flat-work-group-size");
    if (!Attr.isValid() || !Attr.isStringAttribute())
      return {0, ThreadLimit};
    auto [LBStr, UBStr] = Attr.getValueAsString().split(',');
    int32_t LB, UB;
    if (!llvm::to_integer(UBStr, UB, 10))
      return {0, ThreadLimit};
    UB = ThreadLimit ? std::min(ThreadLimit, UB) : UB;
    if (!llvm::to_integer(LBStr, LB, 10))
      return {0, UB};
    return {LB, UB};
  }

  if (MDNode *ExistingOp = getNVPTXMDNode(Kernel, "maxntidx")) {
    auto *OldVal = cast<ConstantAsMetadata>(ExistingOp->getOperand(2));
    int32_t UB = cast<ConstantInt>(OldVal->getValue())->getZExtValue();
    return {0, ThreadLimit ? std::min(ThreadLimit, UB) : UB};
  }
  return {0, ThreadLimit};
}

void OpenMPIRBuilder::writeThreadBoundsForKernel(const Triple &T,
                                                 Function &Kernel, int32_t LB,
                                                 int32_t UB) {
  Kernel.addFnAttr("omp_target_thread_limit", std::to_string(UB));

  if (T.isAMDGPU()) {
    Kernel.addFnAttr("amdgpu-flat-work-group-size",
                     llvm::utostr(LB) + "," + llvm::utostr(UB));
    return;
  }

  updateNVPTXMetadata(Kernel, "maxntidx", UB, true);
}

std::pair<int32_t, int32_t>
OpenMPIRBuilder::readTeamBoundsForKernel(const Triple &, Function &Kernel) {
  // TODO: Read from backend annotations if available.
  return {0, Kernel.getFnAttributeAsParsedInteger("omp_target_num_teams")};
}

void OpenMPIRBuilder::writeTeamsForKernel(const Triple &T, Function &Kernel,
                                          int32_t LB, int32_t UB) {
  if (T.isNVPTX())
    if (UB > 0)
      updateNVPTXMetadata(Kernel, "maxclusterrank", UB, true);
  if (T.isAMDGPU())
    Kernel.addFnAttr("amdgpu-max-num-workgroups", llvm::utostr(LB) + ",1,1");

  Kernel.addFnAttr("omp_target_num_teams", std::to_string(LB));
}

void OpenMPIRBuilder::setOutlinedTargetRegionFunctionAttributes(
    Function *OutlinedFn) {
  if (Config.isTargetDevice()) {
    OutlinedFn->setLinkage(GlobalValue::WeakODRLinkage);
    // TODO: Determine if DSO local can be set to true.
    OutlinedFn->setDSOLocal(false);
    OutlinedFn->setVisibility(GlobalValue::ProtectedVisibility);
    if (T.isAMDGCN())
      OutlinedFn->setCallingConv(CallingConv::AMDGPU_KERNEL);
  }
}

Constant *OpenMPIRBuilder::createOutlinedFunctionID(Function *OutlinedFn,
                                                    StringRef EntryFnIDName) {
  if (Config.isTargetDevice()) {
    assert(OutlinedFn && "The outlined function must exist if embedded");
    return OutlinedFn;
  }

  return new GlobalVariable(
      M, Builder.getInt8Ty(), /*isConstant=*/true, GlobalValue::WeakAnyLinkage,
      Constant::getNullValue(Builder.getInt8Ty()), EntryFnIDName);
}

Constant *OpenMPIRBuilder::createTargetRegionEntryAddr(Function *OutlinedFn,
                                                       StringRef EntryFnName) {
  if (OutlinedFn)
    return OutlinedFn;

  assert(!M.getGlobalVariable(EntryFnName, true) &&
         "Named kernel already exists?");
  return new GlobalVariable(
      M, Builder.getInt8Ty(), /*isConstant=*/true, GlobalValue::InternalLinkage,
      Constant::getNullValue(Builder.getInt8Ty()), EntryFnName);
}

void OpenMPIRBuilder::emitTargetRegionFunction(
    TargetRegionEntryInfo &EntryInfo,
    FunctionGenCallback &GenerateFunctionCallback, bool IsOffloadEntry,
    Function *&OutlinedFn, Constant *&OutlinedFnID) {

  SmallString<64> EntryFnName;
  OffloadInfoManager.getTargetRegionEntryFnName(EntryFnName, EntryInfo);

  OutlinedFn = Config.isTargetDevice() || !Config.openMPOffloadMandatory()
                   ? GenerateFunctionCallback(EntryFnName)
                   : nullptr;

  // If this target outline function is not an offload entry, we don't need to
  // register it. This may be in the case of a false if clause, or if there are
  // no OpenMP targets.
  if (!IsOffloadEntry)
    return;

  std::string EntryFnIDName =
      Config.isTargetDevice()
          ? std::string(EntryFnName)
          : createPlatformSpecificName({EntryFnName, "region_id"});

  OutlinedFnID = registerTargetRegionFunction(EntryInfo, OutlinedFn,
                                              EntryFnName, EntryFnIDName);
}

Constant *OpenMPIRBuilder::registerTargetRegionFunction(
    TargetRegionEntryInfo &EntryInfo, Function *OutlinedFn,
    StringRef EntryFnName, StringRef EntryFnIDName) {
  if (OutlinedFn)
    setOutlinedTargetRegionFunctionAttributes(OutlinedFn);
  auto OutlinedFnID = createOutlinedFunctionID(OutlinedFn, EntryFnIDName);
  auto EntryAddr = createTargetRegionEntryAddr(OutlinedFn, EntryFnName);
  OffloadInfoManager.registerTargetRegionEntryInfo(
      EntryInfo, EntryAddr, OutlinedFnID,
      OffloadEntriesInfoManager::OMPTargetRegionEntryTargetRegion);
  return OutlinedFnID;
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createTargetData(
    const LocationDescription &Loc, InsertPointTy AllocaIP,
    InsertPointTy CodeGenIP, Value *DeviceID, Value *IfCond,
    TargetDataInfo &Info, GenMapInfoCallbackTy GenMapInfoCB,
    omp::RuntimeFunction *MapperFunc,
    function_ref<InsertPointTy(InsertPointTy CodeGenIP, BodyGenTy BodyGenType)>
        BodyGenCB,
    function_ref<void(unsigned int, Value *)> DeviceAddrCB,
    function_ref<Value *(unsigned int)> CustomMapperCB, Value *SrcLocInfo) {
  if (!updateToLocation(Loc))
    return InsertPointTy();

  // Disable TargetData CodeGen on Device pass.
  if (Config.IsTargetDevice.value_or(false)) {
    if (BodyGenCB)
      Builder.restoreIP(BodyGenCB(Builder.saveIP(), BodyGenTy::NoPriv));
    return Builder.saveIP();
  }

  Builder.restoreIP(CodeGenIP);
  bool IsStandAlone = !BodyGenCB;
  MapInfosTy *MapInfo;
  // Generate the code for the opening of the data environment. Capture all the
  // arguments of the runtime call by reference because they are used in the
  // closing of the region.
  auto BeginThenGen = [&](InsertPointTy AllocaIP, InsertPointTy CodeGenIP) {
    MapInfo = &GenMapInfoCB(Builder.saveIP());
    emitOffloadingArrays(AllocaIP, Builder.saveIP(), *MapInfo, Info,
                         /*IsNonContiguous=*/true, DeviceAddrCB,
                         CustomMapperCB);

    TargetDataRTArgs RTArgs;
    emitOffloadingArraysArgument(Builder, RTArgs, Info,
                                 !MapInfo->Names.empty());

    // Emit the number of elements in the offloading arrays.
    Value *PointerNum = Builder.getInt32(Info.NumberOfPtrs);

    // Source location for the ident struct
    if (!SrcLocInfo) {
      uint32_t SrcLocStrSize;
      Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
      SrcLocInfo = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
    }

    Value *OffloadingArgs[] = {SrcLocInfo,           DeviceID,
                               PointerNum,           RTArgs.BasePointersArray,
                               RTArgs.PointersArray, RTArgs.SizesArray,
                               RTArgs.MapTypesArray, RTArgs.MapNamesArray,
                               RTArgs.MappersArray};

    if (IsStandAlone) {
      assert(MapperFunc && "MapperFunc missing for standalone target data");
      Builder.CreateCall(getOrCreateRuntimeFunctionPtr(*MapperFunc),
                         OffloadingArgs);
    } else {
      Function *BeginMapperFunc = getOrCreateRuntimeFunctionPtr(
          omp::OMPRTL___tgt_target_data_begin_mapper);

      Builder.CreateCall(BeginMapperFunc, OffloadingArgs);

      for (auto DeviceMap : Info.DevicePtrInfoMap) {
        if (isa<AllocaInst>(DeviceMap.second.second)) {
          auto *LI =
              Builder.CreateLoad(Builder.getPtrTy(), DeviceMap.second.first);
          Builder.CreateStore(LI, DeviceMap.second.second);
        }
      }

      // If device pointer privatization is required, emit the body of the
      // region here. It will have to be duplicated: with and without
      // privatization.
      Builder.restoreIP(BodyGenCB(Builder.saveIP(), BodyGenTy::Priv));
    }
  };

  // If we need device pointer privatization, we need to emit the body of the
  // region with no privatization in the 'else' branch of the conditional.
  // Otherwise, we don't have to do anything.
  auto BeginElseGen = [&](InsertPointTy AllocaIP, InsertPointTy CodeGenIP) {
    Builder.restoreIP(BodyGenCB(Builder.saveIP(), BodyGenTy::DupNoPriv));
  };

  // Generate code for the closing of the data region.
  auto EndThenGen = [&](InsertPointTy AllocaIP, InsertPointTy CodeGenIP) {
    TargetDataRTArgs RTArgs;
    emitOffloadingArraysArgument(Builder, RTArgs, Info, !MapInfo->Names.empty(),
                                 /*ForEndCall=*/true);

    // Emit the number of elements in the offloading arrays.
    Value *PointerNum = Builder.getInt32(Info.NumberOfPtrs);

    // Source location for the ident struct
    if (!SrcLocInfo) {
      uint32_t SrcLocStrSize;
      Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
      SrcLocInfo = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
    }

    Value *OffloadingArgs[] = {SrcLocInfo,           DeviceID,
                               PointerNum,           RTArgs.BasePointersArray,
                               RTArgs.PointersArray, RTArgs.SizesArray,
                               RTArgs.MapTypesArray, RTArgs.MapNamesArray,
                               RTArgs.MappersArray};
    Function *EndMapperFunc =
        getOrCreateRuntimeFunctionPtr(omp::OMPRTL___tgt_target_data_end_mapper);

    Builder.CreateCall(EndMapperFunc, OffloadingArgs);
  };

  // We don't have to do anything to close the region if the if clause evaluates
  // to false.
  auto EndElseGen = [&](InsertPointTy AllocaIP, InsertPointTy CodeGenIP) {};

  if (BodyGenCB) {
    if (IfCond) {
      emitIfClause(IfCond, BeginThenGen, BeginElseGen, AllocaIP);
    } else {
      BeginThenGen(AllocaIP, Builder.saveIP());
    }

    // If we don't require privatization of device pointers, we emit the body in
    // between the runtime calls. This avoids duplicating the body code.
    Builder.restoreIP(BodyGenCB(Builder.saveIP(), BodyGenTy::NoPriv));

    if (IfCond) {
      emitIfClause(IfCond, EndThenGen, EndElseGen, AllocaIP);
    } else {
      EndThenGen(AllocaIP, Builder.saveIP());
    }
  } else {
    if (IfCond) {
      emitIfClause(IfCond, BeginThenGen, EndElseGen, AllocaIP);
    } else {
      BeginThenGen(AllocaIP, Builder.saveIP());
    }
  }

  return Builder.saveIP();
}

FunctionCallee
OpenMPIRBuilder::createForStaticInitFunction(unsigned IVSize, bool IVSigned,
                                             bool IsGPUDistribute) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  RuntimeFunction Name;
  if (IsGPUDistribute)
    Name = IVSize == 32
               ? (IVSigned ? omp::OMPRTL___kmpc_distribute_static_init_4
                           : omp::OMPRTL___kmpc_distribute_static_init_4u)
               : (IVSigned ? omp::OMPRTL___kmpc_distribute_static_init_8
                           : omp::OMPRTL___kmpc_distribute_static_init_8u);
  else
    Name = IVSize == 32 ? (IVSigned ? omp::OMPRTL___kmpc_for_static_init_4
                                    : omp::OMPRTL___kmpc_for_static_init_4u)
                        : (IVSigned ? omp::OMPRTL___kmpc_for_static_init_8
                                    : omp::OMPRTL___kmpc_for_static_init_8u);

  return getOrCreateRuntimeFunction(M, Name);
}

FunctionCallee OpenMPIRBuilder::createDispatchInitFunction(unsigned IVSize,
                                                           bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  RuntimeFunction Name = IVSize == 32
                             ? (IVSigned ? omp::OMPRTL___kmpc_dispatch_init_4
                                         : omp::OMPRTL___kmpc_dispatch_init_4u)
                             : (IVSigned ? omp::OMPRTL___kmpc_dispatch_init_8
                                         : omp::OMPRTL___kmpc_dispatch_init_8u);

  return getOrCreateRuntimeFunction(M, Name);
}

FunctionCallee OpenMPIRBuilder::createDispatchNextFunction(unsigned IVSize,
                                                           bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  RuntimeFunction Name = IVSize == 32
                             ? (IVSigned ? omp::OMPRTL___kmpc_dispatch_next_4
                                         : omp::OMPRTL___kmpc_dispatch_next_4u)
                             : (IVSigned ? omp::OMPRTL___kmpc_dispatch_next_8
                                         : omp::OMPRTL___kmpc_dispatch_next_8u);

  return getOrCreateRuntimeFunction(M, Name);
}

FunctionCallee OpenMPIRBuilder::createDispatchFiniFunction(unsigned IVSize,
                                                           bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  RuntimeFunction Name = IVSize == 32
                             ? (IVSigned ? omp::OMPRTL___kmpc_dispatch_fini_4
                                         : omp::OMPRTL___kmpc_dispatch_fini_4u)
                             : (IVSigned ? omp::OMPRTL___kmpc_dispatch_fini_8
                                         : omp::OMPRTL___kmpc_dispatch_fini_8u);

  return getOrCreateRuntimeFunction(M, Name);
}

FunctionCallee OpenMPIRBuilder::createDispatchDeinitFunction() {
  return getOrCreateRuntimeFunction(M, omp::OMPRTL___kmpc_dispatch_deinit);
}

static Function *createOutlinedFunction(
    OpenMPIRBuilder &OMPBuilder, IRBuilderBase &Builder, StringRef FuncName,
    SmallVectorImpl<Value *> &Inputs,
    OpenMPIRBuilder::TargetBodyGenCallbackTy &CBFunc,
    OpenMPIRBuilder::TargetGenArgAccessorsCallbackTy &ArgAccessorFuncCB) {
  SmallVector<Type *> ParameterTypes;
  if (OMPBuilder.Config.isTargetDevice()) {
    // Add the "implicit" runtime argument we use to provide launch specific
    // information for target devices.
    auto *Int8PtrTy = PointerType::getUnqual(Builder.getContext());
    ParameterTypes.push_back(Int8PtrTy);

    // All parameters to target devices are passed as pointers
    // or i64. This assumes 64-bit address spaces/pointers.
    for (auto &Arg : Inputs)
      ParameterTypes.push_back(Arg->getType()->isPointerTy()
                                   ? Arg->getType()
                                   : Type::getInt64Ty(Builder.getContext()));
  } else {
    for (auto &Arg : Inputs)
      ParameterTypes.push_back(Arg->getType());
  }

  auto FuncType = FunctionType::get(Builder.getVoidTy(), ParameterTypes,
                                    /*isVarArg*/ false);
  auto Func = Function::Create(FuncType, GlobalValue::InternalLinkage, FuncName,
                               Builder.GetInsertBlock()->getModule());

  // Save insert point.
  auto OldInsertPoint = Builder.saveIP();

  // Generate the region into the function.
  BasicBlock *EntryBB = BasicBlock::Create(Builder.getContext(), "entry", Func);
  Builder.SetInsertPoint(EntryBB);

  // Insert target init call in the device compilation pass.
  if (OMPBuilder.Config.isTargetDevice())
    Builder.restoreIP(OMPBuilder.createTargetInit(Builder, /*IsSPMD*/ false));

  BasicBlock *UserCodeEntryBB = Builder.GetInsertBlock();

  // As we embed the user code in the middle of our target region after we
  // generate entry code, we must move what allocas we can into the entry
  // block to avoid possible breaking optimisations for device
  if (OMPBuilder.Config.isTargetDevice())
    OMPBuilder.ConstantAllocaRaiseCandidates.emplace_back(Func);

  // Insert target deinit call in the device compilation pass.
  Builder.restoreIP(CBFunc(Builder.saveIP(), Builder.saveIP()));
  if (OMPBuilder.Config.isTargetDevice())
    OMPBuilder.createTargetDeinit(Builder);

  // Insert return instruction.
  Builder.CreateRetVoid();

  // New Alloca IP at entry point of created device function.
  Builder.SetInsertPoint(EntryBB->getFirstNonPHI());
  auto AllocaIP = Builder.saveIP();

  Builder.SetInsertPoint(UserCodeEntryBB->getFirstNonPHIOrDbg());

  // Skip the artificial dyn_ptr on the device.
  const auto &ArgRange =
      OMPBuilder.Config.isTargetDevice()
          ? make_range(Func->arg_begin() + 1, Func->arg_end())
          : Func->args();

  auto ReplaceValue = [](Value *Input, Value *InputCopy, Function *Func) {
    // Things like GEP's can come in the form of Constants. Constants and
    // ConstantExpr's do not have access to the knowledge of what they're
    // contained in, so we must dig a little to find an instruction so we
    // can tell if they're used inside of the function we're outlining. We
    // also replace the original constant expression with a new instruction
    // equivalent; an instruction as it allows easy modification in the
    // following loop, as we can now know the constant (instruction) is
    // owned by our target function and replaceUsesOfWith can now be invoked
    // on it (cannot do this with constants it seems). A brand new one also
    // allows us to be cautious as it is perhaps possible the old expression
    // was used inside of the function but exists and is used externally
    // (unlikely by the nature of a Constant, but still).
    // NOTE: We cannot remove dead constants that have been rewritten to
    // instructions at this stage, we run the risk of breaking later lowering
    // by doing so as we could still be in the process of lowering the module
    // from MLIR to LLVM-IR and the MLIR lowering may still require the original
    // constants we have created rewritten versions of.
    if (auto *Const = dyn_cast<Constant>(Input))
      convertUsersOfConstantsToInstructions(Const, Func, false);

    // Collect all the instructions
    for (User *User : make_early_inc_range(Input->users()))
      if (auto *Instr = dyn_cast<Instruction>(User))
        if (Instr->getFunction() == Func)
          Instr->replaceUsesOfWith(Input, InputCopy);
  };

  SmallVector<std::pair<Value *, Value *>> DeferredReplacement;

  // Rewrite uses of input valus to parameters.
  for (auto InArg : zip(Inputs, ArgRange)) {
    Value *Input = std::get<0>(InArg);
    Argument &Arg = std::get<1>(InArg);
    Value *InputCopy = nullptr;

    Builder.restoreIP(
        ArgAccessorFuncCB(Arg, Input, InputCopy, AllocaIP, Builder.saveIP()));

    // In certain cases a Global may be set up for replacement, however, this
    // Global may be used in multiple arguments to the kernel, just segmented
    // apart, for example, if we have a global array, that is sectioned into
    // multiple mappings (technically not legal in OpenMP, but there is a case
    // in Fortran for Common Blocks where this is neccesary), we will end up
    // with GEP's into this array inside the kernel, that refer to the Global
    // but are technically seperate arguments to the kernel for all intents and
    // purposes. If we have mapped a segment that requires a GEP into the 0-th
    // index, it will fold into an referal to the Global, if we then encounter
    // this folded GEP during replacement all of the references to the
    // Global in the kernel will be replaced with the argument we have generated
    // that corresponds to it, including any other GEP's that refer to the
    // Global that may be other arguments. This will invalidate all of the other
    // preceding mapped arguments that refer to the same global that may be
    // seperate segments. To prevent this, we defer global processing until all
    // other processing has been performed.
    if (llvm::isa<llvm::GlobalValue>(std::get<0>(InArg)) ||
        llvm::isa<llvm::GlobalObject>(std::get<0>(InArg)) ||
        llvm::isa<llvm::GlobalVariable>(std::get<0>(InArg))) {
      DeferredReplacement.push_back(std::make_pair(Input, InputCopy));
      continue;
    }

    ReplaceValue(Input, InputCopy, Func);
  }

  // Replace all of our deferred Input values, currently just Globals.
  for (auto Deferred : DeferredReplacement)
    ReplaceValue(std::get<0>(Deferred), std::get<1>(Deferred), Func);

  // Restore insert point.
  Builder.restoreIP(OldInsertPoint);

  return Func;
}

/// Create an entry point for a target task with the following.
/// It'll have the following signature
/// void @.omp_target_task_proxy_func(i32 %thread.id, ptr %task)
/// This function is called from emitTargetTask once the
/// code to launch the target kernel has been outlined already.
static Function *emitTargetTaskProxyFunction(OpenMPIRBuilder &OMPBuilder,
                                             IRBuilderBase &Builder,
                                             CallInst *StaleCI) {
  Module &M = OMPBuilder.M;
  // KernelLaunchFunction is the target launch function, i.e.
  // the function that sets up kernel arguments and calls
  // __tgt_target_kernel to launch the kernel on the device.
  //
  Function *KernelLaunchFunction = StaleCI->getCalledFunction();

  // StaleCI is the CallInst which is the call to the outlined
  // target kernel launch function. If there are values that the
  // outlined function uses then these are aggregated into a structure
  // which is passed as the second argument. If not, then there's
  // only one argument, the threadID. So, StaleCI can be
  //
  // %structArg = alloca { ptr, ptr }, align 8
  // %gep_ = getelementptr { ptr, ptr }, ptr %structArg, i32 0, i32 0
  // store ptr %20, ptr %gep_, align 8
  // %gep_8 = getelementptr { ptr, ptr }, ptr %structArg, i32 0, i32 1
  // store ptr %21, ptr %gep_8, align 8
  // call void @_QQmain..omp_par.1(i32 %global.tid.val6, ptr %structArg)
  //
  // OR
  //
  // call void @_QQmain..omp_par.1(i32 %global.tid.val6)
  OpenMPIRBuilder::InsertPointTy IP(StaleCI->getParent(),
                                    StaleCI->getIterator());
  LLVMContext &Ctx = StaleCI->getParent()->getContext();
  Type *ThreadIDTy = Type::getInt32Ty(Ctx);
  Type *TaskPtrTy = OMPBuilder.TaskPtr;
  Type *TaskTy = OMPBuilder.Task;
  auto ProxyFnTy =
      FunctionType::get(Builder.getVoidTy(), {ThreadIDTy, TaskPtrTy},
                        /* isVarArg */ false);
  auto ProxyFn = Function::Create(ProxyFnTy, GlobalValue::InternalLinkage,
                                  ".omp_target_task_proxy_func",
                                  Builder.GetInsertBlock()->getModule());
  ProxyFn->getArg(0)->setName("thread.id");
  ProxyFn->getArg(1)->setName("task");

  BasicBlock *EntryBB =
      BasicBlock::Create(Builder.getContext(), "entry", ProxyFn);
  Builder.SetInsertPoint(EntryBB);

  bool HasShareds = StaleCI->arg_size() > 1;
  // TODO: This is a temporary assert to prove to ourselves that
  // the outlined target launch function is always going to have
  // atmost two arguments if there is any data shared between
  // host and device.
  assert((!HasShareds || (StaleCI->arg_size() == 2)) &&
         "StaleCI with shareds should have exactly two arguments.");
  if (HasShareds) {
    auto *ArgStructAlloca = dyn_cast<AllocaInst>(StaleCI->getArgOperand(1));
    assert(ArgStructAlloca &&
           "Unable to find the alloca instruction corresponding to arguments "
           "for extracted function");
    auto *ArgStructType =
        dyn_cast<StructType>(ArgStructAlloca->getAllocatedType());

    AllocaInst *NewArgStructAlloca =
        Builder.CreateAlloca(ArgStructType, nullptr, "structArg");
    Value *TaskT = ProxyFn->getArg(1);
    Value *ThreadId = ProxyFn->getArg(0);
    Value *SharedsSize =
        Builder.getInt64(M.getDataLayout().getTypeStoreSize(ArgStructType));

    Value *Shareds = Builder.CreateStructGEP(TaskTy, TaskT, 0);
    LoadInst *LoadShared =
        Builder.CreateLoad(PointerType::getUnqual(Ctx), Shareds);

    Builder.CreateMemCpy(
        NewArgStructAlloca, NewArgStructAlloca->getAlign(), LoadShared,
        LoadShared->getPointerAlignment(M.getDataLayout()), SharedsSize);

    Builder.CreateCall(KernelLaunchFunction, {ThreadId, NewArgStructAlloca});
  }
  Builder.CreateRetVoid();
  return ProxyFn;
}
static void emitTargetOutlinedFunction(
    OpenMPIRBuilder &OMPBuilder, IRBuilderBase &Builder,
    TargetRegionEntryInfo &EntryInfo, Function *&OutlinedFn,
    Constant *&OutlinedFnID, SmallVectorImpl<Value *> &Inputs,
    OpenMPIRBuilder::TargetBodyGenCallbackTy &CBFunc,
    OpenMPIRBuilder::TargetGenArgAccessorsCallbackTy &ArgAccessorFuncCB) {

  OpenMPIRBuilder::FunctionGenCallback &&GenerateOutlinedFunction =
      [&OMPBuilder, &Builder, &Inputs, &CBFunc,
       &ArgAccessorFuncCB](StringRef EntryFnName) {
        return createOutlinedFunction(OMPBuilder, Builder, EntryFnName, Inputs,
                                      CBFunc, ArgAccessorFuncCB);
      };

  OMPBuilder.emitTargetRegionFunction(EntryInfo, GenerateOutlinedFunction, true,
                                      OutlinedFn, OutlinedFnID);
}
OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::emitTargetTask(
    Function *OutlinedFn, Value *OutlinedFnID,
    EmitFallbackCallbackTy EmitTargetCallFallbackCB, TargetKernelArgs &Args,
    Value *DeviceID, Value *RTLoc, OpenMPIRBuilder::InsertPointTy AllocaIP,
    SmallVector<llvm::OpenMPIRBuilder::DependData> &Dependencies,
    bool HasNoWait) {

  // When we arrive at this function, the target region itself has been
  // outlined into the function OutlinedFn.
  // So at ths point, for
  // --------------------------------------------------
  //   void user_code_that_offloads(...) {
  //     omp target depend(..) map(from:a) map(to:b, c)
  //        a = b + c
  //   }
  //
  // --------------------------------------------------
  //
  // we have
  //
  // --------------------------------------------------
  //
  //   void user_code_that_offloads(...) {
  //     %.offload_baseptrs = alloca [3 x ptr], align 8
  //     %.offload_ptrs = alloca [3 x ptr], align 8
  //     %.offload_mappers = alloca [3 x ptr], align 8
  //     ;; target region has been outlined and now we need to
  //     ;; offload to it via a target task.
  //   }
  //   void outlined_device_function(ptr a, ptr b, ptr c) {
  //     *a = *b + *c
  //   }
  //
  // We have to now do the following
  // (i)   Make an offloading call to outlined_device_function using the OpenMP
  //       RTL. See 'kernel_launch_function' in the pseudo code below. This is
  //       emitted by emitKernelLaunch
  // (ii)  Create a task entry point function that calls kernel_launch_function
  //       and is the entry point for the target task. See
  //       '@.omp_target_task_proxy_func in the pseudocode below.
  // (iii) Create a task with the task entry point created in (ii)
  //
  // That is we create the following
  //
  //   void user_code_that_offloads(...) {
  //     %.offload_baseptrs = alloca [3 x ptr], align 8
  //     %.offload_ptrs = alloca [3 x ptr], align 8
  //     %.offload_mappers = alloca [3 x ptr], align 8
  //
  //     %structArg = alloca { ptr, ptr, ptr }, align 8
  //     %strucArg[0] = %.offload_baseptrs
  //     %strucArg[1] = %.offload_ptrs
  //     %strucArg[2] = %.offload_mappers
  //     proxy_target_task = @__kmpc_omp_task_alloc(...,
  //                                               @.omp_target_task_proxy_func)
  //     memcpy(proxy_target_task->shareds, %structArg, sizeof(structArg))
  //     dependencies_array = ...
  //     ;; if nowait not present
  //     call @__kmpc_omp_wait_deps(..., dependencies_array)
  //     call @__kmpc_omp_task_begin_if0(...)
  //     call @ @.omp_target_task_proxy_func(i32 thread_id, ptr
  //     %proxy_target_task) call @__kmpc_omp_task_complete_if0(...)
  //   }
  //
  //   define internal void @.omp_target_task_proxy_func(i32 %thread.id,
  //                                                     ptr %task) {
  //       %structArg = alloca {ptr, ptr, ptr}
  //       %shared_data = load (getelementptr %task, 0, 0)
  //       mempcy(%structArg, %shared_data, sizeof(structArg))
  //       kernel_launch_function(%thread.id, %structArg)
  //   }
  //
  //   We need the proxy function because the signature of the task entry point
  //   expected by kmpc_omp_task is always the same and will be different from
  //   that of the kernel_launch function.
  //
  //   kernel_launch_function is generated by emitKernelLaunch and has the
  //   always_inline attribute.
  //   void kernel_launch_function(thread_id,
  //                               structArg) alwaysinline {
  //       %kernel_args = alloca %struct.__tgt_kernel_arguments, align 8
  //       offload_baseptrs = load(getelementptr structArg, 0, 0)
  //       offload_ptrs = load(getelementptr structArg, 0, 1)
  //       offload_mappers = load(getelementptr structArg, 0, 2)
  //       ; setup kernel_args using offload_baseptrs, offload_ptrs and
  //       ; offload_mappers
  //       call i32 @__tgt_target_kernel(...,
  //                                     outlined_device_function,
  //                                     ptr %kernel_args)
  //   }
  //   void outlined_device_function(ptr a, ptr b, ptr c) {
  //      *a = *b + *c
  //   }
  //
  BasicBlock *TargetTaskBodyBB =
      splitBB(Builder, /*CreateBranch=*/true, "target.task.body");
  BasicBlock *TargetTaskAllocaBB =
      splitBB(Builder, /*CreateBranch=*/true, "target.task.alloca");

  InsertPointTy TargetTaskAllocaIP(TargetTaskAllocaBB,
                                   TargetTaskAllocaBB->begin());
  InsertPointTy TargetTaskBodyIP(TargetTaskBodyBB, TargetTaskBodyBB->begin());

  OutlineInfo OI;
  OI.EntryBB = TargetTaskAllocaBB;
  OI.OuterAllocaBB = AllocaIP.getBlock();

  // Add the thread ID argument.
  SmallVector<Instruction *, 4> ToBeDeleted;
  OI.ExcludeArgsFromAggregate.push_back(createFakeIntVal(
      Builder, AllocaIP, ToBeDeleted, TargetTaskAllocaIP, "global.tid", false));

  Builder.restoreIP(TargetTaskBodyIP);

  // emitKernelLaunch makes the necessary runtime call to offload the kernel.
  // We then outline all that code into a separate function
  // ('kernel_launch_function' in the pseudo code above). This function is then
  // called by the target task proxy function (see
  // '@.omp_target_task_proxy_func' in the pseudo code above)
  // "@.omp_target_task_proxy_func' is generated by emitTargetTaskProxyFunction
  Builder.restoreIP(emitKernelLaunch(Builder, OutlinedFn, OutlinedFnID,
                                     EmitTargetCallFallbackCB, Args, DeviceID,
                                     RTLoc, TargetTaskAllocaIP));

  OI.ExitBB = Builder.saveIP().getBlock();
  OI.PostOutlineCB = [this, ToBeDeleted, Dependencies,
                      HasNoWait](Function &OutlinedFn) mutable {
    assert(OutlinedFn.getNumUses() == 1 &&
           "there must be a single user for the outlined function");

    CallInst *StaleCI = cast<CallInst>(OutlinedFn.user_back());
    bool HasShareds = StaleCI->arg_size() > 1;

    Function *ProxyFn = emitTargetTaskProxyFunction(*this, Builder, StaleCI);

    LLVM_DEBUG(dbgs() << "Proxy task entry function created: " << *ProxyFn
                      << "\n");

    Builder.SetInsertPoint(StaleCI);

    // Gather the arguments for emitting the runtime call.
    uint32_t SrcLocStrSize;
    Constant *SrcLocStr =
        getOrCreateSrcLocStr(LocationDescription(Builder), SrcLocStrSize);
    Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);

    // @__kmpc_omp_task_alloc
    Function *TaskAllocFn =
        getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_alloc);

    // Arguments - `loc_ref` (Ident) and `gtid` (ThreadID)
    // call.
    Value *ThreadID = getOrCreateThreadID(Ident);

    // Argument - `sizeof_kmp_task_t` (TaskSize)
    // Tasksize refers to the size in bytes of kmp_task_t data structure
    // including private vars accessed in task.
    // TODO: add kmp_task_t_with_privates (privates)
    Value *TaskSize =
        Builder.getInt64(M.getDataLayout().getTypeStoreSize(Task));

    // Argument - `sizeof_shareds` (SharedsSize)
    // SharedsSize refers to the shareds array size in the kmp_task_t data
    // structure.
    Value *SharedsSize = Builder.getInt64(0);
    if (HasShareds) {
      auto *ArgStructAlloca = dyn_cast<AllocaInst>(StaleCI->getArgOperand(1));
      assert(ArgStructAlloca &&
             "Unable to find the alloca instruction corresponding to arguments "
             "for extracted function");
      auto *ArgStructType =
          dyn_cast<StructType>(ArgStructAlloca->getAllocatedType());
      assert(ArgStructType && "Unable to find struct type corresponding to "
                              "arguments for extracted function");
      SharedsSize =
          Builder.getInt64(M.getDataLayout().getTypeStoreSize(ArgStructType));
    }

    // Argument - `flags`
    // Task is tied iff (Flags & 1) == 1.
    // Task is untied iff (Flags & 1) == 0.
    // Task is final iff (Flags & 2) == 2.
    // Task is not final iff (Flags & 2) == 0.
    // A target task is not final and is untied.
    Value *Flags = Builder.getInt32(0);

    // Emit the @__kmpc_omp_task_alloc runtime call
    // The runtime call returns a pointer to an area where the task captured
    // variables must be copied before the task is run (TaskData)
    CallInst *TaskData = Builder.CreateCall(
        TaskAllocFn, {/*loc_ref=*/Ident, /*gtid=*/ThreadID, /*flags=*/Flags,
                      /*sizeof_task=*/TaskSize, /*sizeof_shared=*/SharedsSize,
                      /*task_func=*/ProxyFn});

    if (HasShareds) {
      Value *Shareds = StaleCI->getArgOperand(1);
      Align Alignment = TaskData->getPointerAlignment(M.getDataLayout());
      Value *TaskShareds = Builder.CreateLoad(VoidPtr, TaskData);
      Builder.CreateMemCpy(TaskShareds, Alignment, Shareds, Alignment,
                           SharedsSize);
    }

    Value *DepArray = emitTaskDependencies(*this, Dependencies);

    // ---------------------------------------------------------------
    // V5.2 13.8 target construct
    // If the nowait clause is present, execution of the target task
    // may be deferred. If the nowait clause is not present, the target task is
    // an included task.
    // ---------------------------------------------------------------
    // The above means that the lack of a nowait on the target construct
    // translates to '#pragma omp task if(0)'
    if (!HasNoWait) {
      if (DepArray) {
        Function *TaskWaitFn =
            getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_wait_deps);
        Builder.CreateCall(
            TaskWaitFn,
            {/*loc_ref=*/Ident, /*gtid=*/ThreadID,
             /*ndeps=*/Builder.getInt32(Dependencies.size()),
             /*dep_list=*/DepArray,
             /*ndeps_noalias=*/ConstantInt::get(Builder.getInt32Ty(), 0),
             /*noalias_dep_list=*/
             ConstantPointerNull::get(PointerType::getUnqual(M.getContext()))});
      }
      // Included task.
      Function *TaskBeginFn =
          getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_begin_if0);
      Function *TaskCompleteFn =
          getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_complete_if0);
      Builder.CreateCall(TaskBeginFn, {Ident, ThreadID, TaskData});
      CallInst *CI = nullptr;
      if (HasShareds)
        CI = Builder.CreateCall(ProxyFn, {ThreadID, TaskData});
      else
        CI = Builder.CreateCall(ProxyFn, {ThreadID});
      CI->setDebugLoc(StaleCI->getDebugLoc());
      Builder.CreateCall(TaskCompleteFn, {Ident, ThreadID, TaskData});
    } else if (DepArray) {
      // HasNoWait - meaning the task may be deferred. Call
      // __kmpc_omp_task_with_deps if there are dependencies,
      // else call __kmpc_omp_task
      Function *TaskFn =
          getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task_with_deps);
      Builder.CreateCall(
          TaskFn,
          {Ident, ThreadID, TaskData, Builder.getInt32(Dependencies.size()),
           DepArray, ConstantInt::get(Builder.getInt32Ty(), 0),
           ConstantPointerNull::get(PointerType::getUnqual(M.getContext()))});
    } else {
      // Emit the @__kmpc_omp_task runtime call to spawn the task
      Function *TaskFn = getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_omp_task);
      Builder.CreateCall(TaskFn, {Ident, ThreadID, TaskData});
    }

    StaleCI->eraseFromParent();
    llvm::for_each(llvm::reverse(ToBeDeleted),
                   [](Instruction *I) { I->eraseFromParent(); });
  };
  addOutlineInfo(std::move(OI));

  LLVM_DEBUG(dbgs() << "Insert block after emitKernelLaunch = \n"
                    << *(Builder.GetInsertBlock()) << "\n");
  LLVM_DEBUG(dbgs() << "Module after emitKernelLaunch = \n"
                    << *(Builder.GetInsertBlock()->getParent()->getParent())
                    << "\n");
  return Builder.saveIP();
}
static void emitTargetCall(
    OpenMPIRBuilder &OMPBuilder, IRBuilderBase &Builder,
    OpenMPIRBuilder::InsertPointTy AllocaIP, Function *OutlinedFn,
    Constant *OutlinedFnID, int32_t NumTeams, int32_t NumThreads,
    SmallVectorImpl<Value *> &Args,
    OpenMPIRBuilder::GenMapInfoCallbackTy GenMapInfoCB,
    SmallVector<llvm::OpenMPIRBuilder::DependData> Dependencies = {}) {

  OpenMPIRBuilder::TargetDataInfo Info(
      /*RequiresDevicePointerInfo=*/false,
      /*SeparateBeginEndCalls=*/true);

  OpenMPIRBuilder::MapInfosTy &MapInfo = GenMapInfoCB(Builder.saveIP());
  OMPBuilder.emitOffloadingArrays(AllocaIP, Builder.saveIP(), MapInfo, Info,
                                  /*IsNonContiguous=*/true);

  OpenMPIRBuilder::TargetDataRTArgs RTArgs;
  OMPBuilder.emitOffloadingArraysArgument(Builder, RTArgs, Info,
                                          !MapInfo.Names.empty());

  //  emitKernelLaunch
  auto &&EmitTargetCallFallbackCB =
      [&](OpenMPIRBuilder::InsertPointTy IP) -> OpenMPIRBuilder::InsertPointTy {
    Builder.restoreIP(IP);
    Builder.CreateCall(OutlinedFn, Args);
    return Builder.saveIP();
  };

  unsigned NumTargetItems = MapInfo.BasePointers.size();
  // TODO: Use correct device ID
  Value *DeviceID = Builder.getInt64(OMP_DEVICEID_UNDEF);
  Value *NumTeamsVal = Builder.getInt32(NumTeams);
  Value *NumThreadsVal = Builder.getInt32(NumThreads);
  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = OMPBuilder.getOrCreateDefaultSrcLocStr(SrcLocStrSize);
  Value *RTLoc = OMPBuilder.getOrCreateIdent(SrcLocStr, SrcLocStrSize,
                                             llvm::omp::IdentFlag(0), 0);
  // TODO: Use correct NumIterations
  Value *NumIterations = Builder.getInt64(0);
  // TODO: Use correct DynCGGroupMem
  Value *DynCGGroupMem = Builder.getInt32(0);

  bool HasNoWait = false;
  bool HasDependencies = Dependencies.size() > 0;
  bool RequiresOuterTargetTask = HasNoWait || HasDependencies;

  OpenMPIRBuilder::TargetKernelArgs KArgs(NumTargetItems, RTArgs, NumIterations,
                                          NumTeamsVal, NumThreadsVal,
                                          DynCGGroupMem, HasNoWait);

  // The presence of certain clauses on the target directive require the
  // explicit generation of the target task.
  if (RequiresOuterTargetTask) {
    Builder.restoreIP(OMPBuilder.emitTargetTask(
        OutlinedFn, OutlinedFnID, EmitTargetCallFallbackCB, KArgs, DeviceID,
        RTLoc, AllocaIP, Dependencies, HasNoWait));
  } else {
    Builder.restoreIP(OMPBuilder.emitKernelLaunch(
        Builder, OutlinedFn, OutlinedFnID, EmitTargetCallFallbackCB, KArgs,
        DeviceID, RTLoc, AllocaIP));
  }
}
OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createTarget(
    const LocationDescription &Loc, InsertPointTy AllocaIP,
    InsertPointTy CodeGenIP, TargetRegionEntryInfo &EntryInfo, int32_t NumTeams,
    int32_t NumThreads, SmallVectorImpl<Value *> &Args,
    GenMapInfoCallbackTy GenMapInfoCB,
    OpenMPIRBuilder::TargetBodyGenCallbackTy CBFunc,
    OpenMPIRBuilder::TargetGenArgAccessorsCallbackTy ArgAccessorFuncCB,
    SmallVector<DependData> Dependencies) {

  if (!updateToLocation(Loc))
    return InsertPointTy();

  Builder.restoreIP(CodeGenIP);

  Function *OutlinedFn;
  Constant *OutlinedFnID;
  // The target region is outlined into its own function. The LLVM IR for
  // the target region itself is generated using the callbacks CBFunc
  // and ArgAccessorFuncCB
  emitTargetOutlinedFunction(*this, Builder, EntryInfo, OutlinedFn,
                             OutlinedFnID, Args, CBFunc, ArgAccessorFuncCB);

  // If we are not on the target device, then we need to generate code
  // to make a remote call (offload) to the previously outlined function
  // that represents the target region. Do that now.
  if (!Config.isTargetDevice())
    emitTargetCall(*this, Builder, AllocaIP, OutlinedFn, OutlinedFnID, NumTeams,
                   NumThreads, Args, GenMapInfoCB, Dependencies);
  return Builder.saveIP();
}

std::string OpenMPIRBuilder::getNameWithSeparators(ArrayRef<StringRef> Parts,
                                                   StringRef FirstSeparator,
                                                   StringRef Separator) {
  SmallString<128> Buffer;
  llvm::raw_svector_ostream OS(Buffer);
  StringRef Sep = FirstSeparator;
  for (StringRef Part : Parts) {
    OS << Sep << Part;
    Sep = Separator;
  }
  return OS.str().str();
}

std::string
OpenMPIRBuilder::createPlatformSpecificName(ArrayRef<StringRef> Parts) const {
  return OpenMPIRBuilder::getNameWithSeparators(Parts, Config.firstSeparator(),
                                                Config.separator());
}

GlobalVariable *
OpenMPIRBuilder::getOrCreateInternalVariable(Type *Ty, const StringRef &Name,
                                             unsigned AddressSpace) {
  auto &Elem = *InternalVars.try_emplace(Name, nullptr).first;
  if (Elem.second) {
    assert(Elem.second->getValueType() == Ty &&
           "OMP internal variable has different type than requested");
  } else {
    // TODO: investigate the appropriate linkage type used for the global
    // variable for possibly changing that to internal or private, or maybe
    // create different versions of the function for different OMP internal
    // variables.
    auto Linkage = this->M.getTargetTriple().rfind("wasm32") == 0
                       ? GlobalValue::ExternalLinkage
                       : GlobalValue::CommonLinkage;
    auto *GV = new GlobalVariable(M, Ty, /*IsConstant=*/false, Linkage,
                                  Constant::getNullValue(Ty), Elem.first(),
                                  /*InsertBefore=*/nullptr,
                                  GlobalValue::NotThreadLocal, AddressSpace);
    const DataLayout &DL = M.getDataLayout();
    const llvm::Align TypeAlign = DL.getABITypeAlign(Ty);
    const llvm::Align PtrAlign = DL.getPointerABIAlignment(AddressSpace);
    GV->setAlignment(std::max(TypeAlign, PtrAlign));
    Elem.second = GV;
  }

  return Elem.second;
}

Value *OpenMPIRBuilder::getOMPCriticalRegionLock(StringRef CriticalName) {
  std::string Prefix = Twine("gomp_critical_user_", CriticalName).str();
  std::string Name = getNameWithSeparators({Prefix, "var"}, ".", ".");
  return getOrCreateInternalVariable(KmpCriticalNameTy, Name);
}

Value *OpenMPIRBuilder::getSizeInBytes(Value *BasePtr) {
  LLVMContext &Ctx = Builder.getContext();
  Value *Null =
      Constant::getNullValue(PointerType::getUnqual(BasePtr->getContext()));
  Value *SizeGep =
      Builder.CreateGEP(BasePtr->getType(), Null, Builder.getInt32(1));
  Value *SizePtrToInt = Builder.CreatePtrToInt(SizeGep, Type::getInt64Ty(Ctx));
  return SizePtrToInt;
}

GlobalVariable *
OpenMPIRBuilder::createOffloadMaptypes(SmallVectorImpl<uint64_t> &Mappings,
                                       std::string VarName) {
  llvm::Constant *MaptypesArrayInit =
      llvm::ConstantDataArray::get(M.getContext(), Mappings);
  auto *MaptypesArrayGlobal = new llvm::GlobalVariable(
      M, MaptypesArrayInit->getType(),
      /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage, MaptypesArrayInit,
      VarName);
  MaptypesArrayGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  return MaptypesArrayGlobal;
}

void OpenMPIRBuilder::createMapperAllocas(const LocationDescription &Loc,
                                          InsertPointTy AllocaIP,
                                          unsigned NumOperands,
                                          struct MapperAllocas &MapperAllocas) {
  if (!updateToLocation(Loc))
    return;

  auto *ArrI8PtrTy = ArrayType::get(Int8Ptr, NumOperands);
  auto *ArrI64Ty = ArrayType::get(Int64, NumOperands);
  Builder.restoreIP(AllocaIP);
  AllocaInst *ArgsBase = Builder.CreateAlloca(
      ArrI8PtrTy, /* ArraySize = */ nullptr, ".offload_baseptrs");
  AllocaInst *Args = Builder.CreateAlloca(ArrI8PtrTy, /* ArraySize = */ nullptr,
                                          ".offload_ptrs");
  AllocaInst *ArgSizes = Builder.CreateAlloca(
      ArrI64Ty, /* ArraySize = */ nullptr, ".offload_sizes");
  Builder.restoreIP(Loc.IP);
  MapperAllocas.ArgsBase = ArgsBase;
  MapperAllocas.Args = Args;
  MapperAllocas.ArgSizes = ArgSizes;
}

void OpenMPIRBuilder::emitMapperCall(const LocationDescription &Loc,
                                     Function *MapperFunc, Value *SrcLocInfo,
                                     Value *MaptypesArg, Value *MapnamesArg,
                                     struct MapperAllocas &MapperAllocas,
                                     int64_t DeviceID, unsigned NumOperands) {
  if (!updateToLocation(Loc))
    return;

  auto *ArrI8PtrTy = ArrayType::get(Int8Ptr, NumOperands);
  auto *ArrI64Ty = ArrayType::get(Int64, NumOperands);
  Value *ArgsBaseGEP =
      Builder.CreateInBoundsGEP(ArrI8PtrTy, MapperAllocas.ArgsBase,
                                {Builder.getInt32(0), Builder.getInt32(0)});
  Value *ArgsGEP =
      Builder.CreateInBoundsGEP(ArrI8PtrTy, MapperAllocas.Args,
                                {Builder.getInt32(0), Builder.getInt32(0)});
  Value *ArgSizesGEP =
      Builder.CreateInBoundsGEP(ArrI64Ty, MapperAllocas.ArgSizes,
                                {Builder.getInt32(0), Builder.getInt32(0)});
  Value *NullPtr =
      Constant::getNullValue(PointerType::getUnqual(Int8Ptr->getContext()));
  Builder.CreateCall(MapperFunc,
                     {SrcLocInfo, Builder.getInt64(DeviceID),
                      Builder.getInt32(NumOperands), ArgsBaseGEP, ArgsGEP,
                      ArgSizesGEP, MaptypesArg, MapnamesArg, NullPtr});
}

void OpenMPIRBuilder::emitOffloadingArraysArgument(IRBuilderBase &Builder,
                                                   TargetDataRTArgs &RTArgs,
                                                   TargetDataInfo &Info,
                                                   bool EmitDebug,
                                                   bool ForEndCall) {
  assert((!ForEndCall || Info.separateBeginEndCalls()) &&
         "expected region end call to runtime only when end call is separate");
  auto UnqualPtrTy = PointerType::getUnqual(M.getContext());
  auto VoidPtrTy = UnqualPtrTy;
  auto VoidPtrPtrTy = UnqualPtrTy;
  auto Int64Ty = Type::getInt64Ty(M.getContext());
  auto Int64PtrTy = UnqualPtrTy;

  if (!Info.NumberOfPtrs) {
    RTArgs.BasePointersArray = ConstantPointerNull::get(VoidPtrPtrTy);
    RTArgs.PointersArray = ConstantPointerNull::get(VoidPtrPtrTy);
    RTArgs.SizesArray = ConstantPointerNull::get(Int64PtrTy);
    RTArgs.MapTypesArray = ConstantPointerNull::get(Int64PtrTy);
    RTArgs.MapNamesArray = ConstantPointerNull::get(VoidPtrPtrTy);
    RTArgs.MappersArray = ConstantPointerNull::get(VoidPtrPtrTy);
    return;
  }

  RTArgs.BasePointersArray = Builder.CreateConstInBoundsGEP2_32(
      ArrayType::get(VoidPtrTy, Info.NumberOfPtrs),
      Info.RTArgs.BasePointersArray,
      /*Idx0=*/0, /*Idx1=*/0);
  RTArgs.PointersArray = Builder.CreateConstInBoundsGEP2_32(
      ArrayType::get(VoidPtrTy, Info.NumberOfPtrs), Info.RTArgs.PointersArray,
      /*Idx0=*/0,
      /*Idx1=*/0);
  RTArgs.SizesArray = Builder.CreateConstInBoundsGEP2_32(
      ArrayType::get(Int64Ty, Info.NumberOfPtrs), Info.RTArgs.SizesArray,
      /*Idx0=*/0, /*Idx1=*/0);
  RTArgs.MapTypesArray = Builder.CreateConstInBoundsGEP2_32(
      ArrayType::get(Int64Ty, Info.NumberOfPtrs),
      ForEndCall && Info.RTArgs.MapTypesArrayEnd ? Info.RTArgs.MapTypesArrayEnd
                                                 : Info.RTArgs.MapTypesArray,
      /*Idx0=*/0,
      /*Idx1=*/0);

  // Only emit the mapper information arrays if debug information is
  // requested.
  if (!EmitDebug)
    RTArgs.MapNamesArray = ConstantPointerNull::get(VoidPtrPtrTy);
  else
    RTArgs.MapNamesArray = Builder.CreateConstInBoundsGEP2_32(
        ArrayType::get(VoidPtrTy, Info.NumberOfPtrs), Info.RTArgs.MapNamesArray,
        /*Idx0=*/0,
        /*Idx1=*/0);
  // If there is no user-defined mapper, set the mapper array to nullptr to
  // avoid an unnecessary data privatization
  if (!Info.HasMapper)
    RTArgs.MappersArray = ConstantPointerNull::get(VoidPtrPtrTy);
  else
    RTArgs.MappersArray =
        Builder.CreatePointerCast(Info.RTArgs.MappersArray, VoidPtrPtrTy);
}

void OpenMPIRBuilder::emitNonContiguousDescriptor(InsertPointTy AllocaIP,
                                                  InsertPointTy CodeGenIP,
                                                  MapInfosTy &CombinedInfo,
                                                  TargetDataInfo &Info) {
  MapInfosTy::StructNonContiguousInfo &NonContigInfo =
      CombinedInfo.NonContigInfo;

  // Build an array of struct descriptor_dim and then assign it to
  // offload_args.
  //
  // struct descriptor_dim {
  //  uint64_t offset;
  //  uint64_t count;
  //  uint64_t stride
  // };
  Type *Int64Ty = Builder.getInt64Ty();
  StructType *DimTy = StructType::create(
      M.getContext(), ArrayRef<Type *>({Int64Ty, Int64Ty, Int64Ty}),
      "struct.descriptor_dim");

  enum { OffsetFD = 0, CountFD, StrideFD };
  // We need two index variable here since the size of "Dims" is the same as
  // the size of Components, however, the size of offset, count, and stride is
  // equal to the size of base declaration that is non-contiguous.
  for (unsigned I = 0, L = 0, E = NonContigInfo.Dims.size(); I < E; ++I) {
    // Skip emitting ir if dimension size is 1 since it cannot be
    // non-contiguous.
    if (NonContigInfo.Dims[I] == 1)
      continue;
    Builder.restoreIP(AllocaIP);
    ArrayType *ArrayTy = ArrayType::get(DimTy, NonContigInfo.Dims[I]);
    AllocaInst *DimsAddr =
        Builder.CreateAlloca(ArrayTy, /* ArraySize = */ nullptr, "dims");
    Builder.restoreIP(CodeGenIP);
    for (unsigned II = 0, EE = NonContigInfo.Dims[I]; II < EE; ++II) {
      unsigned RevIdx = EE - II - 1;
      Value *DimsLVal = Builder.CreateInBoundsGEP(
          DimsAddr->getAllocatedType(), DimsAddr,
          {Builder.getInt64(0), Builder.getInt64(II)});
      // Offset
      Value *OffsetLVal = Builder.CreateStructGEP(DimTy, DimsLVal, OffsetFD);
      Builder.CreateAlignedStore(
          NonContigInfo.Offsets[L][RevIdx], OffsetLVal,
          M.getDataLayout().getPrefTypeAlign(OffsetLVal->getType()));
      // Count
      Value *CountLVal = Builder.CreateStructGEP(DimTy, DimsLVal, CountFD);
      Builder.CreateAlignedStore(
          NonContigInfo.Counts[L][RevIdx], CountLVal,
          M.getDataLayout().getPrefTypeAlign(CountLVal->getType()));
      // Stride
      Value *StrideLVal = Builder.CreateStructGEP(DimTy, DimsLVal, StrideFD);
      Builder.CreateAlignedStore(
          NonContigInfo.Strides[L][RevIdx], StrideLVal,
          M.getDataLayout().getPrefTypeAlign(CountLVal->getType()));
    }
    // args[I] = &dims
    Builder.restoreIP(CodeGenIP);
    Value *DAddr = Builder.CreatePointerBitCastOrAddrSpaceCast(
        DimsAddr, Builder.getPtrTy());
    Value *P = Builder.CreateConstInBoundsGEP2_32(
        ArrayType::get(Builder.getPtrTy(), Info.NumberOfPtrs),
        Info.RTArgs.PointersArray, 0, I);
    Builder.CreateAlignedStore(
        DAddr, P, M.getDataLayout().getPrefTypeAlign(Builder.getPtrTy()));
    ++L;
  }
}

void OpenMPIRBuilder::emitOffloadingArrays(
    InsertPointTy AllocaIP, InsertPointTy CodeGenIP, MapInfosTy &CombinedInfo,
    TargetDataInfo &Info, bool IsNonContiguous,
    function_ref<void(unsigned int, Value *)> DeviceAddrCB,
    function_ref<Value *(unsigned int)> CustomMapperCB) {

  // Reset the array information.
  Info.clearArrayInfo();
  Info.NumberOfPtrs = CombinedInfo.BasePointers.size();

  if (Info.NumberOfPtrs == 0)
    return;

  Builder.restoreIP(AllocaIP);
  // Detect if we have any capture size requiring runtime evaluation of the
  // size so that a constant array could be eventually used.
  ArrayType *PointerArrayType =
      ArrayType::get(Builder.getPtrTy(), Info.NumberOfPtrs);

  Info.RTArgs.BasePointersArray = Builder.CreateAlloca(
      PointerArrayType, /* ArraySize = */ nullptr, ".offload_baseptrs");

  Info.RTArgs.PointersArray = Builder.CreateAlloca(
      PointerArrayType, /* ArraySize = */ nullptr, ".offload_ptrs");
  AllocaInst *MappersArray = Builder.CreateAlloca(
      PointerArrayType, /* ArraySize = */ nullptr, ".offload_mappers");
  Info.RTArgs.MappersArray = MappersArray;

  // If we don't have any VLA types or other types that require runtime
  // evaluation, we can use a constant array for the map sizes, otherwise we
  // need to fill up the arrays as we do for the pointers.
  Type *Int64Ty = Builder.getInt64Ty();
  SmallVector<Constant *> ConstSizes(CombinedInfo.Sizes.size(),
                                     ConstantInt::get(Int64Ty, 0));
  SmallBitVector RuntimeSizes(CombinedInfo.Sizes.size());
  for (unsigned I = 0, E = CombinedInfo.Sizes.size(); I < E; ++I) {
    if (auto *CI = dyn_cast<Constant>(CombinedInfo.Sizes[I])) {
      if (!isa<ConstantExpr>(CI) && !isa<GlobalValue>(CI)) {
        if (IsNonContiguous &&
            static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                CombinedInfo.Types[I] &
                OpenMPOffloadMappingFlags::OMP_MAP_NON_CONTIG))
          ConstSizes[I] =
              ConstantInt::get(Int64Ty, CombinedInfo.NonContigInfo.Dims[I]);
        else
          ConstSizes[I] = CI;
        continue;
      }
    }
    RuntimeSizes.set(I);
  }

  if (RuntimeSizes.all()) {
    ArrayType *SizeArrayType = ArrayType::get(Int64Ty, Info.NumberOfPtrs);
    Info.RTArgs.SizesArray = Builder.CreateAlloca(
        SizeArrayType, /* ArraySize = */ nullptr, ".offload_sizes");
    Builder.restoreIP(CodeGenIP);
  } else {
    auto *SizesArrayInit = ConstantArray::get(
        ArrayType::get(Int64Ty, ConstSizes.size()), ConstSizes);
    std::string Name = createPlatformSpecificName({"offload_sizes"});
    auto *SizesArrayGbl =
        new GlobalVariable(M, SizesArrayInit->getType(), /*isConstant=*/true,
                           GlobalValue::PrivateLinkage, SizesArrayInit, Name);
    SizesArrayGbl->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

    if (!RuntimeSizes.any()) {
      Info.RTArgs.SizesArray = SizesArrayGbl;
    } else {
      unsigned IndexSize = M.getDataLayout().getIndexSizeInBits(0);
      Align OffloadSizeAlign = M.getDataLayout().getABIIntegerTypeAlignment(64);
      ArrayType *SizeArrayType = ArrayType::get(Int64Ty, Info.NumberOfPtrs);
      AllocaInst *Buffer = Builder.CreateAlloca(
          SizeArrayType, /* ArraySize = */ nullptr, ".offload_sizes");
      Buffer->setAlignment(OffloadSizeAlign);
      Builder.restoreIP(CodeGenIP);
      Builder.CreateMemCpy(
          Buffer, M.getDataLayout().getPrefTypeAlign(Buffer->getType()),
          SizesArrayGbl, OffloadSizeAlign,
          Builder.getIntN(
              IndexSize,
              Buffer->getAllocationSize(M.getDataLayout())->getFixedValue()));

      Info.RTArgs.SizesArray = Buffer;
    }
    Builder.restoreIP(CodeGenIP);
  }

  // The map types are always constant so we don't need to generate code to
  // fill arrays. Instead, we create an array constant.
  SmallVector<uint64_t, 4> Mapping;
  for (auto mapFlag : CombinedInfo.Types)
    Mapping.push_back(
        static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
            mapFlag));
  std::string MaptypesName = createPlatformSpecificName({"offload_maptypes"});
  auto *MapTypesArrayGbl = createOffloadMaptypes(Mapping, MaptypesName);
  Info.RTArgs.MapTypesArray = MapTypesArrayGbl;

  // The information types are only built if provided.
  if (!CombinedInfo.Names.empty()) {
    std::string MapnamesName = createPlatformSpecificName({"offload_mapnames"});
    auto *MapNamesArrayGbl =
        createOffloadMapnames(CombinedInfo.Names, MapnamesName);
    Info.RTArgs.MapNamesArray = MapNamesArrayGbl;
  } else {
    Info.RTArgs.MapNamesArray =
        Constant::getNullValue(PointerType::getUnqual(Builder.getContext()));
  }

  // If there's a present map type modifier, it must not be applied to the end
  // of a region, so generate a separate map type array in that case.
  if (Info.separateBeginEndCalls()) {
    bool EndMapTypesDiffer = false;
    for (uint64_t &Type : Mapping) {
      if (Type & static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                     OpenMPOffloadMappingFlags::OMP_MAP_PRESENT)) {
        Type &= ~static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
            OpenMPOffloadMappingFlags::OMP_MAP_PRESENT);
        EndMapTypesDiffer = true;
      }
    }
    if (EndMapTypesDiffer) {
      MapTypesArrayGbl = createOffloadMaptypes(Mapping, MaptypesName);
      Info.RTArgs.MapTypesArrayEnd = MapTypesArrayGbl;
    }
  }

  PointerType *PtrTy = Builder.getPtrTy();
  for (unsigned I = 0; I < Info.NumberOfPtrs; ++I) {
    Value *BPVal = CombinedInfo.BasePointers[I];
    Value *BP = Builder.CreateConstInBoundsGEP2_32(
        ArrayType::get(PtrTy, Info.NumberOfPtrs), Info.RTArgs.BasePointersArray,
        0, I);
    Builder.CreateAlignedStore(BPVal, BP,
                               M.getDataLayout().getPrefTypeAlign(PtrTy));

    if (Info.requiresDevicePointerInfo()) {
      if (CombinedInfo.DevicePointers[I] == DeviceInfoTy::Pointer) {
        CodeGenIP = Builder.saveIP();
        Builder.restoreIP(AllocaIP);
        Info.DevicePtrInfoMap[BPVal] = {BP, Builder.CreateAlloca(PtrTy)};
        Builder.restoreIP(CodeGenIP);
        if (DeviceAddrCB)
          DeviceAddrCB(I, Info.DevicePtrInfoMap[BPVal].second);
      } else if (CombinedInfo.DevicePointers[I] == DeviceInfoTy::Address) {
        Info.DevicePtrInfoMap[BPVal] = {BP, BP};
        if (DeviceAddrCB)
          DeviceAddrCB(I, BP);
      }
    }

    Value *PVal = CombinedInfo.Pointers[I];
    Value *P = Builder.CreateConstInBoundsGEP2_32(
        ArrayType::get(PtrTy, Info.NumberOfPtrs), Info.RTArgs.PointersArray, 0,
        I);
    // TODO: Check alignment correct.
    Builder.CreateAlignedStore(PVal, P,
                               M.getDataLayout().getPrefTypeAlign(PtrTy));

    if (RuntimeSizes.test(I)) {
      Value *S = Builder.CreateConstInBoundsGEP2_32(
          ArrayType::get(Int64Ty, Info.NumberOfPtrs), Info.RTArgs.SizesArray,
          /*Idx0=*/0,
          /*Idx1=*/I);
      Builder.CreateAlignedStore(Builder.CreateIntCast(CombinedInfo.Sizes[I],
                                                       Int64Ty,
                                                       /*isSigned=*/true),
                                 S, M.getDataLayout().getPrefTypeAlign(PtrTy));
    }
    // Fill up the mapper array.
    unsigned IndexSize = M.getDataLayout().getIndexSizeInBits(0);
    Value *MFunc = ConstantPointerNull::get(PtrTy);
    if (CustomMapperCB)
      if (Value *CustomMFunc = CustomMapperCB(I))
        MFunc = Builder.CreatePointerCast(CustomMFunc, PtrTy);
    Value *MAddr = Builder.CreateInBoundsGEP(
        MappersArray->getAllocatedType(), MappersArray,
        {Builder.getIntN(IndexSize, 0), Builder.getIntN(IndexSize, I)});
    Builder.CreateAlignedStore(
        MFunc, MAddr, M.getDataLayout().getPrefTypeAlign(MAddr->getType()));
  }

  if (!IsNonContiguous || CombinedInfo.NonContigInfo.Offsets.empty() ||
      Info.NumberOfPtrs == 0)
    return;
  emitNonContiguousDescriptor(AllocaIP, CodeGenIP, CombinedInfo, Info);
}

void OpenMPIRBuilder::emitBranch(BasicBlock *Target) {
  BasicBlock *CurBB = Builder.GetInsertBlock();

  if (!CurBB || CurBB->getTerminator()) {
    // If there is no insert point or the previous block is already
    // terminated, don't touch it.
  } else {
    // Otherwise, create a fall-through branch.
    Builder.CreateBr(Target);
  }

  Builder.ClearInsertionPoint();
}

void OpenMPIRBuilder::emitBlock(BasicBlock *BB, Function *CurFn,
                                bool IsFinished) {
  BasicBlock *CurBB = Builder.GetInsertBlock();

  // Fall out of the current block (if necessary).
  emitBranch(BB);

  if (IsFinished && BB->use_empty()) {
    BB->eraseFromParent();
    return;
  }

  // Place the block after the current block, if possible, or else at
  // the end of the function.
  if (CurBB && CurBB->getParent())
    CurFn->insert(std::next(CurBB->getIterator()), BB);
  else
    CurFn->insert(CurFn->end(), BB);
  Builder.SetInsertPoint(BB);
}

void OpenMPIRBuilder::emitIfClause(Value *Cond, BodyGenCallbackTy ThenGen,
                                   BodyGenCallbackTy ElseGen,
                                   InsertPointTy AllocaIP) {
  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm of the if/else.
  if (auto *CI = dyn_cast<ConstantInt>(Cond)) {
    auto CondConstant = CI->getSExtValue();
    if (CondConstant)
      ThenGen(AllocaIP, Builder.saveIP());
    else
      ElseGen(AllocaIP, Builder.saveIP());
    return;
  }

  Function *CurFn = Builder.GetInsertBlock()->getParent();

  // Otherwise, the condition did not fold, or we couldn't elide it.  Just
  // emit the conditional branch.
  BasicBlock *ThenBlock = BasicBlock::Create(M.getContext(), "omp_if.then");
  BasicBlock *ElseBlock = BasicBlock::Create(M.getContext(), "omp_if.else");
  BasicBlock *ContBlock = BasicBlock::Create(M.getContext(), "omp_if.end");
  Builder.CreateCondBr(Cond, ThenBlock, ElseBlock);
  // Emit the 'then' code.
  emitBlock(ThenBlock, CurFn);
  ThenGen(AllocaIP, Builder.saveIP());
  emitBranch(ContBlock);
  // Emit the 'else' code if present.
  // There is no need to emit line number for unconditional branch.
  emitBlock(ElseBlock, CurFn);
  ElseGen(AllocaIP, Builder.saveIP());
  // There is no need to emit line number for unconditional branch.
  emitBranch(ContBlock);
  // Emit the continuation block for code after the if.
  emitBlock(ContBlock, CurFn, /*IsFinished=*/true);
}

bool OpenMPIRBuilder::checkAndEmitFlushAfterAtomic(
    const LocationDescription &Loc, llvm::AtomicOrdering AO, AtomicKind AK) {
  assert(!(AO == AtomicOrdering::NotAtomic ||
           AO == llvm::AtomicOrdering::Unordered) &&
         "Unexpected Atomic Ordering.");

  bool Flush = false;
  llvm::AtomicOrdering FlushAO = AtomicOrdering::Monotonic;

  switch (AK) {
  case Read:
    if (AO == AtomicOrdering::Acquire || AO == AtomicOrdering::AcquireRelease ||
        AO == AtomicOrdering::SequentiallyConsistent) {
      FlushAO = AtomicOrdering::Acquire;
      Flush = true;
    }
    break;
  case Write:
  case Compare:
  case Update:
    if (AO == AtomicOrdering::Release || AO == AtomicOrdering::AcquireRelease ||
        AO == AtomicOrdering::SequentiallyConsistent) {
      FlushAO = AtomicOrdering::Release;
      Flush = true;
    }
    break;
  case Capture:
    switch (AO) {
    case AtomicOrdering::Acquire:
      FlushAO = AtomicOrdering::Acquire;
      Flush = true;
      break;
    case AtomicOrdering::Release:
      FlushAO = AtomicOrdering::Release;
      Flush = true;
      break;
    case AtomicOrdering::AcquireRelease:
    case AtomicOrdering::SequentiallyConsistent:
      FlushAO = AtomicOrdering::AcquireRelease;
      Flush = true;
      break;
    default:
      // do nothing - leave silently.
      break;
    }
  }

  if (Flush) {
    // Currently Flush RT call still doesn't take memory_ordering, so for when
    // that happens, this tries to do the resolution of which atomic ordering
    // to use with but issue the flush call
    // TODO: pass `FlushAO` after memory ordering support is added
    (void)FlushAO;
    emitFlush(Loc);
  }

  // for AO == AtomicOrdering::Monotonic and  all other case combinations
  // do nothing
  return Flush;
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createAtomicRead(const LocationDescription &Loc,
                                  AtomicOpValue &X, AtomicOpValue &V,
                                  AtomicOrdering AO) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  assert(X.Var->getType()->isPointerTy() &&
         "OMP Atomic expects a pointer to target memory");
  Type *XElemTy = X.ElemTy;
  assert((XElemTy->isFloatingPointTy() || XElemTy->isIntegerTy() ||
          XElemTy->isPointerTy()) &&
         "OMP atomic read expected a scalar type");

  Value *XRead = nullptr;

  if (XElemTy->isIntegerTy()) {
    LoadInst *XLD =
        Builder.CreateLoad(XElemTy, X.Var, X.IsVolatile, "omp.atomic.read");
    XLD->setAtomic(AO);
    XRead = cast<Value>(XLD);
  } else {
    // We need to perform atomic op as integer
    IntegerType *IntCastTy =
        IntegerType::get(M.getContext(), XElemTy->getScalarSizeInBits());
    LoadInst *XLoad =
        Builder.CreateLoad(IntCastTy, X.Var, X.IsVolatile, "omp.atomic.load");
    XLoad->setAtomic(AO);
    if (XElemTy->isFloatingPointTy()) {
      XRead = Builder.CreateBitCast(XLoad, XElemTy, "atomic.flt.cast");
    } else {
      XRead = Builder.CreateIntToPtr(XLoad, XElemTy, "atomic.ptr.cast");
    }
  }
  checkAndEmitFlushAfterAtomic(Loc, AO, AtomicKind::Read);
  Builder.CreateStore(XRead, V.Var, V.IsVolatile);
  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createAtomicWrite(const LocationDescription &Loc,
                                   AtomicOpValue &X, Value *Expr,
                                   AtomicOrdering AO) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  assert(X.Var->getType()->isPointerTy() &&
         "OMP Atomic expects a pointer to target memory");
  Type *XElemTy = X.ElemTy;
  assert((XElemTy->isFloatingPointTy() || XElemTy->isIntegerTy() ||
          XElemTy->isPointerTy()) &&
         "OMP atomic write expected a scalar type");

  if (XElemTy->isIntegerTy()) {
    StoreInst *XSt = Builder.CreateStore(Expr, X.Var, X.IsVolatile);
    XSt->setAtomic(AO);
  } else {
    // We need to bitcast and perform atomic op as integers
    IntegerType *IntCastTy =
        IntegerType::get(M.getContext(), XElemTy->getScalarSizeInBits());
    Value *ExprCast =
        Builder.CreateBitCast(Expr, IntCastTy, "atomic.src.int.cast");
    StoreInst *XSt = Builder.CreateStore(ExprCast, X.Var, X.IsVolatile);
    XSt->setAtomic(AO);
  }

  checkAndEmitFlushAfterAtomic(Loc, AO, AtomicKind::Write);
  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createAtomicUpdate(
    const LocationDescription &Loc, InsertPointTy AllocaIP, AtomicOpValue &X,
    Value *Expr, AtomicOrdering AO, AtomicRMWInst::BinOp RMWOp,
    AtomicUpdateCallbackTy &UpdateOp, bool IsXBinopExpr) {
  assert(!isConflictIP(Loc.IP, AllocaIP) && "IPs must not be ambiguous");
  if (!updateToLocation(Loc))
    return Loc.IP;

  LLVM_DEBUG({
    Type *XTy = X.Var->getType();
    assert(XTy->isPointerTy() &&
           "OMP Atomic expects a pointer to target memory");
    Type *XElemTy = X.ElemTy;
    assert((XElemTy->isFloatingPointTy() || XElemTy->isIntegerTy() ||
            XElemTy->isPointerTy()) &&
           "OMP atomic update expected a scalar type");
    assert((RMWOp != AtomicRMWInst::Max) && (RMWOp != AtomicRMWInst::Min) &&
           (RMWOp != AtomicRMWInst::UMax) && (RMWOp != AtomicRMWInst::UMin) &&
           "OpenMP atomic does not support LT or GT operations");
  });

  emitAtomicUpdate(AllocaIP, X.Var, X.ElemTy, Expr, AO, RMWOp, UpdateOp,
                   X.IsVolatile, IsXBinopExpr);
  checkAndEmitFlushAfterAtomic(Loc, AO, AtomicKind::Update);
  return Builder.saveIP();
}

// FIXME: Duplicating AtomicExpand
Value *OpenMPIRBuilder::emitRMWOpAsInstruction(Value *Src1, Value *Src2,
                                               AtomicRMWInst::BinOp RMWOp) {
  switch (RMWOp) {
  case AtomicRMWInst::Add:
    return Builder.CreateAdd(Src1, Src2);
  case AtomicRMWInst::Sub:
    return Builder.CreateSub(Src1, Src2);
  case AtomicRMWInst::And:
    return Builder.CreateAnd(Src1, Src2);
  case AtomicRMWInst::Nand:
    return Builder.CreateNeg(Builder.CreateAnd(Src1, Src2));
  case AtomicRMWInst::Or:
    return Builder.CreateOr(Src1, Src2);
  case AtomicRMWInst::Xor:
    return Builder.CreateXor(Src1, Src2);
  case AtomicRMWInst::Xchg:
  case AtomicRMWInst::FAdd:
  case AtomicRMWInst::FSub:
  case AtomicRMWInst::BAD_BINOP:
  case AtomicRMWInst::Max:
  case AtomicRMWInst::Min:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::UMin:
  case AtomicRMWInst::FMax:
  case AtomicRMWInst::FMin:
  case AtomicRMWInst::UIncWrap:
  case AtomicRMWInst::UDecWrap:
    llvm_unreachable("Unsupported atomic update operation");
  }
  llvm_unreachable("Unsupported atomic update operation");
}

std::pair<Value *, Value *> OpenMPIRBuilder::emitAtomicUpdate(
    InsertPointTy AllocaIP, Value *X, Type *XElemTy, Value *Expr,
    AtomicOrdering AO, AtomicRMWInst::BinOp RMWOp,
    AtomicUpdateCallbackTy &UpdateOp, bool VolatileX, bool IsXBinopExpr) {
  // TODO: handle the case where XElemTy is not byte-sized or not a power of 2
  // or a complex datatype.
  bool emitRMWOp = false;
  switch (RMWOp) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::And:
  case AtomicRMWInst::Nand:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::Xchg:
    emitRMWOp = XElemTy;
    break;
  case AtomicRMWInst::Sub:
    emitRMWOp = (IsXBinopExpr && XElemTy);
    break;
  default:
    emitRMWOp = false;
  }
  emitRMWOp &= XElemTy->isIntegerTy();

  std::pair<Value *, Value *> Res;
  if (emitRMWOp) {
    Res.first = Builder.CreateAtomicRMW(RMWOp, X, Expr, llvm::MaybeAlign(), AO);
    // not needed except in case of postfix captures. Generate anyway for
    // consistency with the else part. Will be removed with any DCE pass.
    // AtomicRMWInst::Xchg does not have a coressponding instruction.
    if (RMWOp == AtomicRMWInst::Xchg)
      Res.second = Res.first;
    else
      Res.second = emitRMWOpAsInstruction(Res.first, Expr, RMWOp);
  } else {
    IntegerType *IntCastTy =
        IntegerType::get(M.getContext(), XElemTy->getScalarSizeInBits());
    LoadInst *OldVal =
        Builder.CreateLoad(IntCastTy, X, X->getName() + ".atomic.load");
    OldVal->setAtomic(AO);
    // CurBB
    // |     /---\
		// ContBB    |
    // |     \---/
    // ExitBB
    BasicBlock *CurBB = Builder.GetInsertBlock();
    Instruction *CurBBTI = CurBB->getTerminator();
    CurBBTI = CurBBTI ? CurBBTI : Builder.CreateUnreachable();
    BasicBlock *ExitBB =
        CurBB->splitBasicBlock(CurBBTI, X->getName() + ".atomic.exit");
    BasicBlock *ContBB = CurBB->splitBasicBlock(CurBB->getTerminator(),
                                                X->getName() + ".atomic.cont");
    ContBB->getTerminator()->eraseFromParent();
    Builder.restoreIP(AllocaIP);
    AllocaInst *NewAtomicAddr = Builder.CreateAlloca(XElemTy);
    NewAtomicAddr->setName(X->getName() + "x.new.val");
    Builder.SetInsertPoint(ContBB);
    llvm::PHINode *PHI = Builder.CreatePHI(OldVal->getType(), 2);
    PHI->addIncoming(OldVal, CurBB);
    bool IsIntTy = XElemTy->isIntegerTy();
    Value *OldExprVal = PHI;
    if (!IsIntTy) {
      if (XElemTy->isFloatingPointTy()) {
        OldExprVal = Builder.CreateBitCast(PHI, XElemTy,
                                           X->getName() + ".atomic.fltCast");
      } else {
        OldExprVal = Builder.CreateIntToPtr(PHI, XElemTy,
                                            X->getName() + ".atomic.ptrCast");
      }
    }

    Value *Upd = UpdateOp(OldExprVal, Builder);
    Builder.CreateStore(Upd, NewAtomicAddr);
    LoadInst *DesiredVal = Builder.CreateLoad(IntCastTy, NewAtomicAddr);
    AtomicOrdering Failure =
        llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);
    AtomicCmpXchgInst *Result = Builder.CreateAtomicCmpXchg(
        X, PHI, DesiredVal, llvm::MaybeAlign(), AO, Failure);
    Result->setVolatile(VolatileX);
    Value *PreviousVal = Builder.CreateExtractValue(Result, /*Idxs=*/0);
    Value *SuccessFailureVal = Builder.CreateExtractValue(Result, /*Idxs=*/1);
    PHI->addIncoming(PreviousVal, Builder.GetInsertBlock());
    Builder.CreateCondBr(SuccessFailureVal, ExitBB, ContBB);

    Res.first = OldExprVal;
    Res.second = Upd;

    // set Insertion point in exit block
    if (UnreachableInst *ExitTI =
            dyn_cast<UnreachableInst>(ExitBB->getTerminator())) {
      CurBBTI->eraseFromParent();
      Builder.SetInsertPoint(ExitBB);
    } else {
      Builder.SetInsertPoint(ExitTI);
    }
  }

  return Res;
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createAtomicCapture(
    const LocationDescription &Loc, InsertPointTy AllocaIP, AtomicOpValue &X,
    AtomicOpValue &V, Value *Expr, AtomicOrdering AO,
    AtomicRMWInst::BinOp RMWOp, AtomicUpdateCallbackTy &UpdateOp,
    bool UpdateExpr, bool IsPostfixUpdate, bool IsXBinopExpr) {
  if (!updateToLocation(Loc))
    return Loc.IP;

  LLVM_DEBUG({
    Type *XTy = X.Var->getType();
    assert(XTy->isPointerTy() &&
           "OMP Atomic expects a pointer to target memory");
    Type *XElemTy = X.ElemTy;
    assert((XElemTy->isFloatingPointTy() || XElemTy->isIntegerTy() ||
            XElemTy->isPointerTy()) &&
           "OMP atomic capture expected a scalar type");
    assert((RMWOp != AtomicRMWInst::Max) && (RMWOp != AtomicRMWInst::Min) &&
           "OpenMP atomic does not support LT or GT operations");
  });

  // If UpdateExpr is 'x' updated with some `expr` not based on 'x',
  // 'x' is simply atomically rewritten with 'expr'.
  AtomicRMWInst::BinOp AtomicOp = (UpdateExpr ? RMWOp : AtomicRMWInst::Xchg);
  std::pair<Value *, Value *> Result =
      emitAtomicUpdate(AllocaIP, X.Var, X.ElemTy, Expr, AO, AtomicOp, UpdateOp,
                       X.IsVolatile, IsXBinopExpr);

  Value *CapturedVal = (IsPostfixUpdate ? Result.first : Result.second);
  Builder.CreateStore(CapturedVal, V.Var, V.IsVolatile);

  checkAndEmitFlushAfterAtomic(Loc, AO, AtomicKind::Capture);
  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createAtomicCompare(
    const LocationDescription &Loc, AtomicOpValue &X, AtomicOpValue &V,
    AtomicOpValue &R, Value *E, Value *D, AtomicOrdering AO,
    omp::OMPAtomicCompareOp Op, bool IsXBinopExpr, bool IsPostfixUpdate,
    bool IsFailOnly) {

  AtomicOrdering Failure = AtomicCmpXchgInst::getStrongestFailureOrdering(AO);
  return createAtomicCompare(Loc, X, V, R, E, D, AO, Op, IsXBinopExpr,
                             IsPostfixUpdate, IsFailOnly, Failure);
}

OpenMPIRBuilder::InsertPointTy OpenMPIRBuilder::createAtomicCompare(
    const LocationDescription &Loc, AtomicOpValue &X, AtomicOpValue &V,
    AtomicOpValue &R, Value *E, Value *D, AtomicOrdering AO,
    omp::OMPAtomicCompareOp Op, bool IsXBinopExpr, bool IsPostfixUpdate,
    bool IsFailOnly, AtomicOrdering Failure) {

  if (!updateToLocation(Loc))
    return Loc.IP;

  assert(X.Var->getType()->isPointerTy() &&
         "OMP atomic expects a pointer to target memory");
  // compare capture
  if (V.Var) {
    assert(V.Var->getType()->isPointerTy() && "v.var must be of pointer type");
    assert(V.ElemTy == X.ElemTy && "x and v must be of same type");
  }

  bool IsInteger = E->getType()->isIntegerTy();

  if (Op == OMPAtomicCompareOp::EQ) {
    AtomicCmpXchgInst *Result = nullptr;
    if (!IsInteger) {
      IntegerType *IntCastTy =
          IntegerType::get(M.getContext(), X.ElemTy->getScalarSizeInBits());
      Value *EBCast = Builder.CreateBitCast(E, IntCastTy);
      Value *DBCast = Builder.CreateBitCast(D, IntCastTy);
      Result = Builder.CreateAtomicCmpXchg(X.Var, EBCast, DBCast, MaybeAlign(),
                                           AO, Failure);
    } else {
      Result =
          Builder.CreateAtomicCmpXchg(X.Var, E, D, MaybeAlign(), AO, Failure);
    }

    if (V.Var) {
      Value *OldValue = Builder.CreateExtractValue(Result, /*Idxs=*/0);
      if (!IsInteger)
        OldValue = Builder.CreateBitCast(OldValue, X.ElemTy);
      assert(OldValue->getType() == V.ElemTy &&
             "OldValue and V must be of same type");
      if (IsPostfixUpdate) {
        Builder.CreateStore(OldValue, V.Var, V.IsVolatile);
      } else {
        Value *SuccessOrFail = Builder.CreateExtractValue(Result, /*Idxs=*/1);
        if (IsFailOnly) {
          // CurBB----
          //   |     |
          //   v     |
          // ContBB  |
          //   |     |
          //   v     |
          // ExitBB <-
          //
          // where ContBB only contains the store of old value to 'v'.
          BasicBlock *CurBB = Builder.GetInsertBlock();
          Instruction *CurBBTI = CurBB->getTerminator();
          CurBBTI = CurBBTI ? CurBBTI : Builder.CreateUnreachable();
          BasicBlock *ExitBB = CurBB->splitBasicBlock(
              CurBBTI, X.Var->getName() + ".atomic.exit");
          BasicBlock *ContBB = CurBB->splitBasicBlock(
              CurBB->getTerminator(), X.Var->getName() + ".atomic.cont");
          ContBB->getTerminator()->eraseFromParent();
          CurBB->getTerminator()->eraseFromParent();

          Builder.CreateCondBr(SuccessOrFail, ExitBB, ContBB);

          Builder.SetInsertPoint(ContBB);
          Builder.CreateStore(OldValue, V.Var);
          Builder.CreateBr(ExitBB);

          if (UnreachableInst *ExitTI =
                  dyn_cast<UnreachableInst>(ExitBB->getTerminator())) {
            CurBBTI->eraseFromParent();
            Builder.SetInsertPoint(ExitBB);
          } else {
            Builder.SetInsertPoint(ExitTI);
          }
        } else {
          Value *CapturedValue =
              Builder.CreateSelect(SuccessOrFail, E, OldValue);
          Builder.CreateStore(CapturedValue, V.Var, V.IsVolatile);
        }
      }
    }
    // The comparison result has to be stored.
    if (R.Var) {
      assert(R.Var->getType()->isPointerTy() &&
             "r.var must be of pointer type");
      assert(R.ElemTy->isIntegerTy() && "r must be of integral type");

      Value *SuccessFailureVal = Builder.CreateExtractValue(Result, /*Idxs=*/1);
      Value *ResultCast = R.IsSigned
                              ? Builder.CreateSExt(SuccessFailureVal, R.ElemTy)
                              : Builder.CreateZExt(SuccessFailureVal, R.ElemTy);
      Builder.CreateStore(ResultCast, R.Var, R.IsVolatile);
    }
  } else {
    assert((Op == OMPAtomicCompareOp::MAX || Op == OMPAtomicCompareOp::MIN) &&
           "Op should be either max or min at this point");
    assert(!IsFailOnly && "IsFailOnly is only valid when the comparison is ==");

    // Reverse the ordop as the OpenMP forms are different from LLVM forms.
    // Let's take max as example.
    // OpenMP form:
    // x = x > expr ? expr : x;
    // LLVM form:
    // *ptr = *ptr > val ? *ptr : val;
    // We need to transform to LLVM form.
    // x = x <= expr ? x : expr;
    AtomicRMWInst::BinOp NewOp;
    if (IsXBinopExpr) {
      if (IsInteger) {
        if (X.IsSigned)
          NewOp = Op == OMPAtomicCompareOp::MAX ? AtomicRMWInst::Min
                                                : AtomicRMWInst::Max;
        else
          NewOp = Op == OMPAtomicCompareOp::MAX ? AtomicRMWInst::UMin
                                                : AtomicRMWInst::UMax;
      } else {
        NewOp = Op == OMPAtomicCompareOp::MAX ? AtomicRMWInst::FMin
                                              : AtomicRMWInst::FMax;
      }
    } else {
      if (IsInteger) {
        if (X.IsSigned)
          NewOp = Op == OMPAtomicCompareOp::MAX ? AtomicRMWInst::Max
                                                : AtomicRMWInst::Min;
        else
          NewOp = Op == OMPAtomicCompareOp::MAX ? AtomicRMWInst::UMax
                                                : AtomicRMWInst::UMin;
      } else {
        NewOp = Op == OMPAtomicCompareOp::MAX ? AtomicRMWInst::FMax
                                              : AtomicRMWInst::FMin;
      }
    }

    AtomicRMWInst *OldValue =
        Builder.CreateAtomicRMW(NewOp, X.Var, E, MaybeAlign(), AO);
    if (V.Var) {
      Value *CapturedValue = nullptr;
      if (IsPostfixUpdate) {
        CapturedValue = OldValue;
      } else {
        CmpInst::Predicate Pred;
        switch (NewOp) {
        case AtomicRMWInst::Max:
          Pred = CmpInst::ICMP_SGT;
          break;
        case AtomicRMWInst::UMax:
          Pred = CmpInst::ICMP_UGT;
          break;
        case AtomicRMWInst::FMax:
          Pred = CmpInst::FCMP_OGT;
          break;
        case AtomicRMWInst::Min:
          Pred = CmpInst::ICMP_SLT;
          break;
        case AtomicRMWInst::UMin:
          Pred = CmpInst::ICMP_ULT;
          break;
        case AtomicRMWInst::FMin:
          Pred = CmpInst::FCMP_OLT;
          break;
        default:
          llvm_unreachable("unexpected comparison op");
        }
        Value *NonAtomicCmp = Builder.CreateCmp(Pred, OldValue, E);
        CapturedValue = Builder.CreateSelect(NonAtomicCmp, E, OldValue);
      }
      Builder.CreateStore(CapturedValue, V.Var, V.IsVolatile);
    }
  }

  checkAndEmitFlushAfterAtomic(Loc, AO, AtomicKind::Compare);

  return Builder.saveIP();
}

OpenMPIRBuilder::InsertPointTy
OpenMPIRBuilder::createTeams(const LocationDescription &Loc,
                             BodyGenCallbackTy BodyGenCB, Value *NumTeamsLower,
                             Value *NumTeamsUpper, Value *ThreadLimit,
                             Value *IfExpr) {
  if (!updateToLocation(Loc))
    return InsertPointTy();

  uint32_t SrcLocStrSize;
  Constant *SrcLocStr = getOrCreateSrcLocStr(Loc, SrcLocStrSize);
  Value *Ident = getOrCreateIdent(SrcLocStr, SrcLocStrSize);
  Function *CurrentFunction = Builder.GetInsertBlock()->getParent();

  // Outer allocation basicblock is the entry block of the current function.
  BasicBlock &OuterAllocaBB = CurrentFunction->getEntryBlock();
  if (&OuterAllocaBB == Builder.GetInsertBlock()) {
    BasicBlock *BodyBB = splitBB(Builder, /*CreateBranch=*/true, "teams.entry");
    Builder.SetInsertPoint(BodyBB, BodyBB->begin());
  }

  // The current basic block is split into four basic blocks. After outlining,
  // they will be mapped as follows:
  // ```
  // def current_fn() {
  //   current_basic_block:
  //     br label %teams.exit
  //   teams.exit:
  //     ; instructions after teams
  // }
  //
  // def outlined_fn() {
  //   teams.alloca:
  //     br label %teams.body
  //   teams.body:
  //     ; instructions within teams body
  // }
  // ```
  BasicBlock *ExitBB = splitBB(Builder, /*CreateBranch=*/true, "teams.exit");
  BasicBlock *BodyBB = splitBB(Builder, /*CreateBranch=*/true, "teams.body");
  BasicBlock *AllocaBB =
      splitBB(Builder, /*CreateBranch=*/true, "teams.alloca");

  bool SubClausesPresent =
      (NumTeamsLower || NumTeamsUpper || ThreadLimit || IfExpr);
  // Push num_teams
  if (!Config.isTargetDevice() && SubClausesPresent) {
    assert((NumTeamsLower == nullptr || NumTeamsUpper != nullptr) &&
           "if lowerbound is non-null, then upperbound must also be non-null "
           "for bounds on num_teams");

    if (NumTeamsUpper == nullptr)
      NumTeamsUpper = Builder.getInt32(0);

    if (NumTeamsLower == nullptr)
      NumTeamsLower = NumTeamsUpper;

    if (IfExpr) {
      assert(IfExpr->getType()->isIntegerTy() &&
             "argument to if clause must be an integer value");

      // upper = ifexpr ? upper : 1
      if (IfExpr->getType() != Int1)
        IfExpr = Builder.CreateICmpNE(IfExpr,
                                      ConstantInt::get(IfExpr->getType(), 0));
      NumTeamsUpper = Builder.CreateSelect(
          IfExpr, NumTeamsUpper, Builder.getInt32(1), "numTeamsUpper");

      // lower = ifexpr ? lower : 1
      NumTeamsLower = Builder.CreateSelect(
          IfExpr, NumTeamsLower, Builder.getInt32(1), "numTeamsLower");
    }

    if (ThreadLimit == nullptr)
      ThreadLimit = Builder.getInt32(0);

    Value *ThreadNum = getOrCreateThreadID(Ident);
    Builder.CreateCall(
        getOrCreateRuntimeFunctionPtr(OMPRTL___kmpc_push_num_teams_51),
        {Ident, ThreadNum, NumTeamsLower, NumTeamsUpper, ThreadLimit});
  }
  // Generate the body of teams.
  InsertPointTy AllocaIP(AllocaBB, AllocaBB->begin());
  InsertPointTy CodeGenIP(BodyBB, BodyBB->begin());
  BodyGenCB(AllocaIP, CodeGenIP);

  OutlineInfo OI;
  OI.EntryBB = AllocaBB;
  OI.ExitBB = ExitBB;
  OI.OuterAllocaBB = &OuterAllocaBB;

  // Insert fake values for global tid and bound tid.
  SmallVector<Instruction *, 8> ToBeDeleted;
  InsertPointTy OuterAllocaIP(&OuterAllocaBB, OuterAllocaBB.begin());
  OI.ExcludeArgsFromAggregate.push_back(createFakeIntVal(
      Builder, OuterAllocaIP, ToBeDeleted, AllocaIP, "gid", true));
  OI.ExcludeArgsFromAggregate.push_back(createFakeIntVal(
      Builder, OuterAllocaIP, ToBeDeleted, AllocaIP, "tid", true));

  auto HostPostOutlineCB = [this, Ident,
                            ToBeDeleted](Function &OutlinedFn) mutable {
    // The stale call instruction will be replaced with a new call instruction
    // for runtime call with the outlined function.

    assert(OutlinedFn.getNumUses() == 1 &&
           "there must be a single user for the outlined function");
    CallInst *StaleCI = cast<CallInst>(OutlinedFn.user_back());
    ToBeDeleted.push_back(StaleCI);

    assert((OutlinedFn.arg_size() == 2 || OutlinedFn.arg_size() == 3) &&
           "Outlined function must have two or three arguments only");

    bool HasShared = OutlinedFn.arg_size() == 3;

    OutlinedFn.getArg(0)->setName("global.tid.ptr");
    OutlinedFn.getArg(1)->setName("bound.tid.ptr");
    if (HasShared)
      OutlinedFn.getArg(2)->setName("data");

    // Call to the runtime function for teams in the current function.
    assert(StaleCI && "Error while outlining - no CallInst user found for the "
                      "outlined function.");
    Builder.SetInsertPoint(StaleCI);
    SmallVector<Value *> Args = {
        Ident, Builder.getInt32(StaleCI->arg_size() - 2), &OutlinedFn};
    if (HasShared)
      Args.push_back(StaleCI->getArgOperand(2));
    Builder.CreateCall(getOrCreateRuntimeFunctionPtr(
                           omp::RuntimeFunction::OMPRTL___kmpc_fork_teams),
                       Args);

    llvm::for_each(llvm::reverse(ToBeDeleted),
                   [](Instruction *I) { I->eraseFromParent(); });

  };

  if (!Config.isTargetDevice())
    OI.PostOutlineCB = HostPostOutlineCB;

  addOutlineInfo(std::move(OI));

  Builder.SetInsertPoint(ExitBB, ExitBB->begin());

  return Builder.saveIP();
}

GlobalVariable *
OpenMPIRBuilder::createOffloadMapnames(SmallVectorImpl<llvm::Constant *> &Names,
                                       std::string VarName) {
  llvm::Constant *MapNamesArrayInit = llvm::ConstantArray::get(
      llvm::ArrayType::get(llvm::PointerType::getUnqual(M.getContext()),
                           Names.size()),
      Names);
  auto *MapNamesArrayGlobal = new llvm::GlobalVariable(
      M, MapNamesArrayInit->getType(),
      /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage, MapNamesArrayInit,
      VarName);
  return MapNamesArrayGlobal;
}

// Create all simple and struct types exposed by the runtime and remember
// the llvm::PointerTypes of them for easy access later.
void OpenMPIRBuilder::initializeTypes(Module &M) {
  LLVMContext &Ctx = M.getContext();
  StructType *T;
#define OMP_TYPE(VarName, InitValue) VarName = InitValue;
#define OMP_ARRAY_TYPE(VarName, ElemTy, ArraySize)                             \
  VarName##Ty = ArrayType::get(ElemTy, ArraySize);                             \
  VarName##PtrTy = PointerType::getUnqual(VarName##Ty);
#define OMP_FUNCTION_TYPE(VarName, IsVarArg, ReturnType, ...)                  \
  VarName = FunctionType::get(ReturnType, {__VA_ARGS__}, IsVarArg);            \
  VarName##Ptr = PointerType::getUnqual(VarName);
#define OMP_STRUCT_TYPE(VarName, StructName, Packed, ...)                      \
  T = StructType::getTypeByName(Ctx, StructName);                              \
  if (!T)                                                                      \
    T = StructType::create(Ctx, {__VA_ARGS__}, StructName, Packed);            \
  VarName = T;                                                                 \
  VarName##Ptr = PointerType::getUnqual(T);
#include "llvm/Frontend/OpenMP/OMPKinds.def"
}

void OpenMPIRBuilder::OutlineInfo::collectBlocks(
    SmallPtrSetImpl<BasicBlock *> &BlockSet,
    SmallVectorImpl<BasicBlock *> &BlockVector) {
  SmallVector<BasicBlock *, 32> Worklist;
  BlockSet.insert(EntryBB);
  BlockSet.insert(ExitBB);

  Worklist.push_back(EntryBB);
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    BlockVector.push_back(BB);
    for (BasicBlock *SuccBB : successors(BB))
      if (BlockSet.insert(SuccBB).second)
        Worklist.push_back(SuccBB);
  }
}

void OpenMPIRBuilder::createOffloadEntry(Constant *ID, Constant *Addr,
                                         uint64_t Size, int32_t Flags,
                                         GlobalValue::LinkageTypes,
                                         StringRef Name) {
  if (!Config.isGPU()) {
    llvm::offloading::emitOffloadingEntry(
        M, ID, Name.empty() ? Addr->getName() : Name, Size, Flags, /*Data=*/0,
        "omp_offloading_entries");
    return;
  }
  // TODO: Add support for global variables on the device after declare target
  // support.
  Function *Fn = dyn_cast<Function>(Addr);
  if (!Fn)
    return;

  Module &M = *(Fn->getParent());
  LLVMContext &Ctx = M.getContext();

  // Get "nvvm.annotations" metadata node.
  NamedMDNode *MD = M.getOrInsertNamedMetadata("nvvm.annotations");

  Metadata *MDVals[] = {
      ConstantAsMetadata::get(Fn), MDString::get(Ctx, "kernel"),
      ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(Ctx), 1))};
  // Append metadata to nvvm.annotations.
  MD->addOperand(MDNode::get(Ctx, MDVals));

  // Add a function attribute for the kernel.
  Fn->addFnAttr(Attribute::get(Ctx, "kernel"));
  if (T.isAMDGCN())
    Fn->addFnAttr("uniform-work-group-size", "true");
  Fn->addFnAttr(Attribute::MustProgress);
}

// We only generate metadata for function that contain target regions.
void OpenMPIRBuilder::createOffloadEntriesAndInfoMetadata(
    EmitMetadataErrorReportFunctionTy &ErrorFn) {

  // If there are no entries, we don't need to do anything.
  if (OffloadInfoManager.empty())
    return;

  LLVMContext &C = M.getContext();
  SmallVector<std::pair<const OffloadEntriesInfoManager::OffloadEntryInfo *,
                        TargetRegionEntryInfo>,
              16>
      OrderedEntries(OffloadInfoManager.size());

  // Auxiliary methods to create metadata values and strings.
  auto &&GetMDInt = [this](unsigned V) {
    return ConstantAsMetadata::get(ConstantInt::get(Builder.getInt32Ty(), V));
  };

  auto &&GetMDString = [&C](StringRef V) { return MDString::get(C, V); };

  // Create the offloading info metadata node.
  NamedMDNode *MD = M.getOrInsertNamedMetadata("omp_offload.info");
  auto &&TargetRegionMetadataEmitter =
      [&C, MD, &OrderedEntries, &GetMDInt, &GetMDString](
          const TargetRegionEntryInfo &EntryInfo,
          const OffloadEntriesInfoManager::OffloadEntryInfoTargetRegion &E) {
        // Generate metadata for target regions. Each entry of this metadata
        // contains:
        // - Entry 0 -> Kind of this type of metadata (0).
        // - Entry 1 -> Device ID of the file where the entry was identified.
        // - Entry 2 -> File ID of the file where the entry was identified.
        // - Entry 3 -> Mangled name of the function where the entry was
        // identified.
        // - Entry 4 -> Line in the file where the entry was identified.
        // - Entry 5 -> Count of regions at this DeviceID/FilesID/Line.
        // - Entry 6 -> Order the entry was created.
        // The first element of the metadata node is the kind.
        Metadata *Ops[] = {
            GetMDInt(E.getKind()),      GetMDInt(EntryInfo.DeviceID),
            GetMDInt(EntryInfo.FileID), GetMDString(EntryInfo.ParentName),
            GetMDInt(EntryInfo.Line),   GetMDInt(EntryInfo.Count),
            GetMDInt(E.getOrder())};

        // Save this entry in the right position of the ordered entries array.
        OrderedEntries[E.getOrder()] = std::make_pair(&E, EntryInfo);

        // Add metadata to the named metadata node.
        MD->addOperand(MDNode::get(C, Ops));
      };

  OffloadInfoManager.actOnTargetRegionEntriesInfo(TargetRegionMetadataEmitter);

  // Create function that emits metadata for each device global variable entry;
  auto &&DeviceGlobalVarMetadataEmitter =
      [&C, &OrderedEntries, &GetMDInt, &GetMDString, MD](
          StringRef MangledName,
          const OffloadEntriesInfoManager::OffloadEntryInfoDeviceGlobalVar &E) {
        // Generate metadata for global variables. Each entry of this metadata
        // contains:
        // - Entry 0 -> Kind of this type of metadata (1).
        // - Entry 1 -> Mangled name of the variable.
        // - Entry 2 -> Declare target kind.
        // - Entry 3 -> Order the entry was created.
        // The first element of the metadata node is the kind.
        Metadata *Ops[] = {GetMDInt(E.getKind()), GetMDString(MangledName),
                           GetMDInt(E.getFlags()), GetMDInt(E.getOrder())};

        // Save this entry in the right position of the ordered entries array.
        TargetRegionEntryInfo varInfo(MangledName, 0, 0, 0);
        OrderedEntries[E.getOrder()] = std::make_pair(&E, varInfo);

        // Add metadata to the named metadata node.
        MD->addOperand(MDNode::get(C, Ops));
      };

  OffloadInfoManager.actOnDeviceGlobalVarEntriesInfo(
      DeviceGlobalVarMetadataEmitter);

  for (const auto &E : OrderedEntries) {
    assert(E.first && "All ordered entries must exist!");
    if (const auto *CE =
            dyn_cast<OffloadEntriesInfoManager::OffloadEntryInfoTargetRegion>(
                E.first)) {
      if (!CE->getID() || !CE->getAddress()) {
        // Do not blame the entry if the parent funtion is not emitted.
        TargetRegionEntryInfo EntryInfo = E.second;
        StringRef FnName = EntryInfo.ParentName;
        if (!M.getNamedValue(FnName))
          continue;
        ErrorFn(EMIT_MD_TARGET_REGION_ERROR, EntryInfo);
        continue;
      }
      createOffloadEntry(CE->getID(), CE->getAddress(),
                         /*Size=*/0, CE->getFlags(),
                         GlobalValue::WeakAnyLinkage);
    } else if (const auto *CE = dyn_cast<
                   OffloadEntriesInfoManager::OffloadEntryInfoDeviceGlobalVar>(
                   E.first)) {
      OffloadEntriesInfoManager::OMPTargetGlobalVarEntryKind Flags =
          static_cast<OffloadEntriesInfoManager::OMPTargetGlobalVarEntryKind>(
              CE->getFlags());
      switch (Flags) {
      case OffloadEntriesInfoManager::OMPTargetGlobalVarEntryEnter:
      case OffloadEntriesInfoManager::OMPTargetGlobalVarEntryTo:
        if (Config.isTargetDevice() && Config.hasRequiresUnifiedSharedMemory())
          continue;
        if (!CE->getAddress()) {
          ErrorFn(EMIT_MD_DECLARE_TARGET_ERROR, E.second);
          continue;
        }
        // The vaiable has no definition - no need to add the entry.
        if (CE->getVarSize() == 0)
          continue;
        break;
      case OffloadEntriesInfoManager::OMPTargetGlobalVarEntryLink:
        assert(((Config.isTargetDevice() && !CE->getAddress()) ||
                (!Config.isTargetDevice() && CE->getAddress())) &&
               "Declaret target link address is set.");
        if (Config.isTargetDevice())
          continue;
        if (!CE->getAddress()) {
          ErrorFn(EMIT_MD_GLOBAL_VAR_LINK_ERROR, TargetRegionEntryInfo());
          continue;
        }
        break;
      default:
        break;
      }

      // Hidden or internal symbols on the device are not externally visible.
      // We should not attempt to register them by creating an offloading
      // entry. Indirect variables are handled separately on the device.
      if (auto *GV = dyn_cast<GlobalValue>(CE->getAddress()))
        if ((GV->hasLocalLinkage() || GV->hasHiddenVisibility()) &&
            Flags != OffloadEntriesInfoManager::OMPTargetGlobalVarEntryIndirect)
          continue;

      // Indirect globals need to use a special name that doesn't match the name
      // of the associated host global.
      if (Flags == OffloadEntriesInfoManager::OMPTargetGlobalVarEntryIndirect)
        createOffloadEntry(CE->getAddress(), CE->getAddress(), CE->getVarSize(),
                           Flags, CE->getLinkage(), CE->getVarName());
      else
        createOffloadEntry(CE->getAddress(), CE->getAddress(), CE->getVarSize(),
                           Flags, CE->getLinkage());

    } else {
      llvm_unreachable("Unsupported entry kind.");
    }
  }

  // Emit requires directive globals to a special entry so the runtime can
  // register them when the device image is loaded.
  // TODO: This reduces the offloading entries to a 32-bit integer. Offloading
  //       entries should be redesigned to better suit this use-case.
  if (Config.hasRequiresFlags() && !Config.isTargetDevice())
    offloading::emitOffloadingEntry(
        M, Constant::getNullValue(PointerType::getUnqual(M.getContext())),
        /*Name=*/"",
        /*Size=*/0, OffloadEntriesInfoManager::OMPTargetGlobalRegisterRequires,
        Config.getRequiresFlags(), "omp_offloading_entries");
}

void TargetRegionEntryInfo::getTargetRegionEntryFnName(
    SmallVectorImpl<char> &Name, StringRef ParentName, unsigned DeviceID,
    unsigned FileID, unsigned Line, unsigned Count) {
  raw_svector_ostream OS(Name);
  OS << "__omp_offloading" << llvm::format("_%x", DeviceID)
     << llvm::format("_%x_", FileID) << ParentName << "_l" << Line;
  if (Count)
    OS << "_" << Count;
}

void OffloadEntriesInfoManager::getTargetRegionEntryFnName(
    SmallVectorImpl<char> &Name, const TargetRegionEntryInfo &EntryInfo) {
  unsigned NewCount = getTargetRegionEntryInfoCount(EntryInfo);
  TargetRegionEntryInfo::getTargetRegionEntryFnName(
      Name, EntryInfo.ParentName, EntryInfo.DeviceID, EntryInfo.FileID,
      EntryInfo.Line, NewCount);
}

TargetRegionEntryInfo
OpenMPIRBuilder::getTargetEntryUniqueInfo(FileIdentifierInfoCallbackTy CallBack,
                                          StringRef ParentName) {
  sys::fs::UniqueID ID;
  auto FileIDInfo = CallBack();
  if (auto EC = sys::fs::getUniqueID(std::get<0>(FileIDInfo), ID)) {
    report_fatal_error(("Unable to get unique ID for file, during "
                        "getTargetEntryUniqueInfo, error message: " +
                        EC.message())
                           .c_str());
  }

  return TargetRegionEntryInfo(ParentName, ID.getDevice(), ID.getFile(),
                               std::get<1>(FileIDInfo));
}

unsigned OpenMPIRBuilder::getFlagMemberOffset() {
  unsigned Offset = 0;
  for (uint64_t Remain =
           static_cast<std::underlying_type_t<omp::OpenMPOffloadMappingFlags>>(
               omp::OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF);
       !(Remain & 1); Remain = Remain >> 1)
    Offset++;
  return Offset;
}

omp::OpenMPOffloadMappingFlags
OpenMPIRBuilder::getMemberOfFlag(unsigned Position) {
  // Rotate by getFlagMemberOffset() bits.
  return static_cast<omp::OpenMPOffloadMappingFlags>(((uint64_t)Position + 1)
                                                     << getFlagMemberOffset());
}

void OpenMPIRBuilder::setCorrectMemberOfFlag(
    omp::OpenMPOffloadMappingFlags &Flags,
    omp::OpenMPOffloadMappingFlags MemberOfFlag) {
  // If the entry is PTR_AND_OBJ but has not been marked with the special
  // placeholder value 0xFFFF in the MEMBER_OF field, then it should not be
  // marked as MEMBER_OF.
  if (static_cast<std::underlying_type_t<omp::OpenMPOffloadMappingFlags>>(
          Flags & omp::OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ) &&
      static_cast<std::underlying_type_t<omp::OpenMPOffloadMappingFlags>>(
          (Flags & omp::OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF) !=
          omp::OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF))
    return;

  // Reset the placeholder value to prepare the flag for the assignment of the
  // proper MEMBER_OF value.
  Flags &= ~omp::OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF;
  Flags |= MemberOfFlag;
}

Constant *OpenMPIRBuilder::getAddrOfDeclareTargetVar(
    OffloadEntriesInfoManager::OMPTargetGlobalVarEntryKind CaptureClause,
    OffloadEntriesInfoManager::OMPTargetDeviceClauseKind DeviceClause,
    bool IsDeclaration, bool IsExternallyVisible,
    TargetRegionEntryInfo EntryInfo, StringRef MangledName,
    std::vector<GlobalVariable *> &GeneratedRefs, bool OpenMPSIMD,
    std::vector<Triple> TargetTriple, Type *LlvmPtrTy,
    std::function<Constant *()> GlobalInitializer,
    std::function<GlobalValue::LinkageTypes()> VariableLinkage) {
  // TODO: convert this to utilise the IRBuilder Config rather than
  // a passed down argument.
  if (OpenMPSIMD)
    return nullptr;

  if (CaptureClause == OffloadEntriesInfoManager::OMPTargetGlobalVarEntryLink ||
      ((CaptureClause == OffloadEntriesInfoManager::OMPTargetGlobalVarEntryTo ||
        CaptureClause ==
            OffloadEntriesInfoManager::OMPTargetGlobalVarEntryEnter) &&
       Config.hasRequiresUnifiedSharedMemory())) {
    SmallString<64> PtrName;
    {
      raw_svector_ostream OS(PtrName);
      OS << MangledName;
      if (!IsExternallyVisible)
        OS << format("_%x", EntryInfo.FileID);
      OS << "_decl_tgt_ref_ptr";
    }

    Value *Ptr = M.getNamedValue(PtrName);

    if (!Ptr) {
      GlobalValue *GlobalValue = M.getNamedValue(MangledName);
      Ptr = getOrCreateInternalVariable(LlvmPtrTy, PtrName);

      auto *GV = cast<GlobalVariable>(Ptr);
      GV->setLinkage(GlobalValue::WeakAnyLinkage);

      if (!Config.isTargetDevice()) {
        if (GlobalInitializer)
          GV->setInitializer(GlobalInitializer());
        else
          GV->setInitializer(GlobalValue);
      }

      registerTargetGlobalVariable(
          CaptureClause, DeviceClause, IsDeclaration, IsExternallyVisible,
          EntryInfo, MangledName, GeneratedRefs, OpenMPSIMD, TargetTriple,
          GlobalInitializer, VariableLinkage, LlvmPtrTy, cast<Constant>(Ptr));
    }

    return cast<Constant>(Ptr);
  }

  return nullptr;
}

void OpenMPIRBuilder::registerTargetGlobalVariable(
    OffloadEntriesInfoManager::OMPTargetGlobalVarEntryKind CaptureClause,
    OffloadEntriesInfoManager::OMPTargetDeviceClauseKind DeviceClause,
    bool IsDeclaration, bool IsExternallyVisible,
    TargetRegionEntryInfo EntryInfo, StringRef MangledName,
    std::vector<GlobalVariable *> &GeneratedRefs, bool OpenMPSIMD,
    std::vector<Triple> TargetTriple,
    std::function<Constant *()> GlobalInitializer,
    std::function<GlobalValue::LinkageTypes()> VariableLinkage, Type *LlvmPtrTy,
    Constant *Addr) {
  if (DeviceClause != OffloadEntriesInfoManager::OMPTargetDeviceClauseAny ||
      (TargetTriple.empty() && !Config.isTargetDevice()))
    return;

  OffloadEntriesInfoManager::OMPTargetGlobalVarEntryKind Flags;
  StringRef VarName;
  int64_t VarSize;
  GlobalValue::LinkageTypes Linkage;

  if ((CaptureClause == OffloadEntriesInfoManager::OMPTargetGlobalVarEntryTo ||
       CaptureClause ==
           OffloadEntriesInfoManager::OMPTargetGlobalVarEntryEnter) &&
      !Config.hasRequiresUnifiedSharedMemory()) {
    Flags = OffloadEntriesInfoManager::OMPTargetGlobalVarEntryTo;
    VarName = MangledName;
    GlobalValue *LlvmVal = M.getNamedValue(VarName);

    if (!IsDeclaration)
      VarSize = divideCeil(
          M.getDataLayout().getTypeSizeInBits(LlvmVal->getValueType()), 8);
    else
      VarSize = 0;
    Linkage = (VariableLinkage) ? VariableLinkage() : LlvmVal->getLinkage();

    // This is a workaround carried over from Clang which prevents undesired
    // optimisation of internal variables.
    if (Config.isTargetDevice() &&
        (!IsExternallyVisible || Linkage == GlobalValue::LinkOnceODRLinkage)) {
      // Do not create a "ref-variable" if the original is not also available
      // on the host.
      if (!OffloadInfoManager.hasDeviceGlobalVarEntryInfo(VarName))
        return;

      std::string RefName = createPlatformSpecificName({VarName, "ref"});

      if (!M.getNamedValue(RefName)) {
        Constant *AddrRef =
            getOrCreateInternalVariable(Addr->getType(), RefName);
        auto *GvAddrRef = cast<GlobalVariable>(AddrRef);
        GvAddrRef->setConstant(true);
        GvAddrRef->setLinkage(GlobalValue::InternalLinkage);
        GvAddrRef->setInitializer(Addr);
        GeneratedRefs.push_back(GvAddrRef);
      }
    }
  } else {
    if (CaptureClause == OffloadEntriesInfoManager::OMPTargetGlobalVarEntryLink)
      Flags = OffloadEntriesInfoManager::OMPTargetGlobalVarEntryLink;
    else
      Flags = OffloadEntriesInfoManager::OMPTargetGlobalVarEntryTo;

    if (Config.isTargetDevice()) {
      VarName = (Addr) ? Addr->getName() : "";
      Addr = nullptr;
    } else {
      Addr = getAddrOfDeclareTargetVar(
          CaptureClause, DeviceClause, IsDeclaration, IsExternallyVisible,
          EntryInfo, MangledName, GeneratedRefs, OpenMPSIMD, TargetTriple,
          LlvmPtrTy, GlobalInitializer, VariableLinkage);
      VarName = (Addr) ? Addr->getName() : "";
    }
    VarSize = M.getDataLayout().getPointerSize();
    Linkage = GlobalValue::WeakAnyLinkage;
  }

  OffloadInfoManager.registerDeviceGlobalVarEntryInfo(VarName, Addr, VarSize,
                                                      Flags, Linkage);
}

/// Loads all the offload entries information from the host IR
/// metadata.
void OpenMPIRBuilder::loadOffloadInfoMetadata(Module &M) {
  // If we are in target mode, load the metadata from the host IR. This code has
  // to match the metadata creation in createOffloadEntriesAndInfoMetadata().

  NamedMDNode *MD = M.getNamedMetadata(ompOffloadInfoName);
  if (!MD)
    return;

  for (MDNode *MN : MD->operands()) {
    auto &&GetMDInt = [MN](unsigned Idx) {
      auto *V = cast<ConstantAsMetadata>(MN->getOperand(Idx));
      return cast<ConstantInt>(V->getValue())->getZExtValue();
    };

    auto &&GetMDString = [MN](unsigned Idx) {
      auto *V = cast<MDString>(MN->getOperand(Idx));
      return V->getString();
    };

    switch (GetMDInt(0)) {
    default:
      llvm_unreachable("Unexpected metadata!");
      break;
    case OffloadEntriesInfoManager::OffloadEntryInfo::
        OffloadingEntryInfoTargetRegion: {
      TargetRegionEntryInfo EntryInfo(/*ParentName=*/GetMDString(3),
                                      /*DeviceID=*/GetMDInt(1),
                                      /*FileID=*/GetMDInt(2),
                                      /*Line=*/GetMDInt(4),
                                      /*Count=*/GetMDInt(5));
      OffloadInfoManager.initializeTargetRegionEntryInfo(EntryInfo,
                                                         /*Order=*/GetMDInt(6));
      break;
    }
    case OffloadEntriesInfoManager::OffloadEntryInfo::
        OffloadingEntryInfoDeviceGlobalVar:
      OffloadInfoManager.initializeDeviceGlobalVarEntryInfo(
          /*MangledName=*/GetMDString(1),
          static_cast<OffloadEntriesInfoManager::OMPTargetGlobalVarEntryKind>(
              /*Flags=*/GetMDInt(2)),
          /*Order=*/GetMDInt(3));
      break;
    }
  }
}

void OpenMPIRBuilder::loadOffloadInfoMetadata(StringRef HostFilePath) {
  if (HostFilePath.empty())
    return;

  auto Buf = MemoryBuffer::getFile(HostFilePath);
  if (std::error_code Err = Buf.getError()) {
    report_fatal_error(("error opening host file from host file path inside of "
                        "OpenMPIRBuilder: " +
                        Err.message())
                           .c_str());
  }

  LLVMContext Ctx;
  auto M = expectedToErrorOrAndEmitErrors(
      Ctx, parseBitcodeFile(Buf.get()->getMemBufferRef(), Ctx));
  if (std::error_code Err = M.getError()) {
    report_fatal_error(
        ("error parsing host file inside of OpenMPIRBuilder: " + Err.message())
            .c_str());
  }

  loadOffloadInfoMetadata(*M.get());
}

//===----------------------------------------------------------------------===//
// OffloadEntriesInfoManager
//===----------------------------------------------------------------------===//

bool OffloadEntriesInfoManager::empty() const {
  return OffloadEntriesTargetRegion.empty() &&
         OffloadEntriesDeviceGlobalVar.empty();
}

unsigned OffloadEntriesInfoManager::getTargetRegionEntryInfoCount(
    const TargetRegionEntryInfo &EntryInfo) const {
  auto It = OffloadEntriesTargetRegionCount.find(
      getTargetRegionEntryCountKey(EntryInfo));
  if (It == OffloadEntriesTargetRegionCount.end())
    return 0;
  return It->second;
}

void OffloadEntriesInfoManager::incrementTargetRegionEntryInfoCount(
    const TargetRegionEntryInfo &EntryInfo) {
  OffloadEntriesTargetRegionCount[getTargetRegionEntryCountKey(EntryInfo)] =
      EntryInfo.Count + 1;
}

/// Initialize target region entry.
void OffloadEntriesInfoManager::initializeTargetRegionEntryInfo(
    const TargetRegionEntryInfo &EntryInfo, unsigned Order) {
  OffloadEntriesTargetRegion[EntryInfo] =
      OffloadEntryInfoTargetRegion(Order, /*Addr=*/nullptr, /*ID=*/nullptr,
                                   OMPTargetRegionEntryTargetRegion);
  ++OffloadingEntriesNum;
}

void OffloadEntriesInfoManager::registerTargetRegionEntryInfo(
    TargetRegionEntryInfo EntryInfo, Constant *Addr, Constant *ID,
    OMPTargetRegionEntryKind Flags) {
  assert(EntryInfo.Count == 0 && "expected default EntryInfo");

  // Update the EntryInfo with the next available count for this location.
  EntryInfo.Count = getTargetRegionEntryInfoCount(EntryInfo);

  // If we are emitting code for a target, the entry is already initialized,
  // only has to be registered.
  if (OMPBuilder->Config.isTargetDevice()) {
    // This could happen if the device compilation is invoked standalone.
    if (!hasTargetRegionEntryInfo(EntryInfo)) {
      return;
    }
    auto &Entry = OffloadEntriesTargetRegion[EntryInfo];
    Entry.setAddress(Addr);
    Entry.setID(ID);
    Entry.setFlags(Flags);
  } else {
    if (Flags == OffloadEntriesInfoManager::OMPTargetRegionEntryTargetRegion &&
        hasTargetRegionEntryInfo(EntryInfo, /*IgnoreAddressId*/ true))
      return;
    assert(!hasTargetRegionEntryInfo(EntryInfo) &&
           "Target region entry already registered!");
    OffloadEntryInfoTargetRegion Entry(OffloadingEntriesNum, Addr, ID, Flags);
    OffloadEntriesTargetRegion[EntryInfo] = Entry;
    ++OffloadingEntriesNum;
  }
  incrementTargetRegionEntryInfoCount(EntryInfo);
}

bool OffloadEntriesInfoManager::hasTargetRegionEntryInfo(
    TargetRegionEntryInfo EntryInfo, bool IgnoreAddressId) const {

  // Update the EntryInfo with the next available count for this location.
  EntryInfo.Count = getTargetRegionEntryInfoCount(EntryInfo);

  auto It = OffloadEntriesTargetRegion.find(EntryInfo);
  if (It == OffloadEntriesTargetRegion.end()) {
    return false;
  }
  // Fail if this entry is already registered.
  if (!IgnoreAddressId && (It->second.getAddress() || It->second.getID()))
    return false;
  return true;
}

void OffloadEntriesInfoManager::actOnTargetRegionEntriesInfo(
    const OffloadTargetRegionEntryInfoActTy &Action) {
  // Scan all target region entries and perform the provided action.
  for (const auto &It : OffloadEntriesTargetRegion) {
    Action(It.first, It.second);
  }
}

void OffloadEntriesInfoManager::initializeDeviceGlobalVarEntryInfo(
    StringRef Name, OMPTargetGlobalVarEntryKind Flags, unsigned Order) {
  OffloadEntriesDeviceGlobalVar.try_emplace(Name, Order, Flags);
  ++OffloadingEntriesNum;
}

void OffloadEntriesInfoManager::registerDeviceGlobalVarEntryInfo(
    StringRef VarName, Constant *Addr, int64_t VarSize,
    OMPTargetGlobalVarEntryKind Flags, GlobalValue::LinkageTypes Linkage) {
  if (OMPBuilder->Config.isTargetDevice()) {
    // This could happen if the device compilation is invoked standalone.
    if (!hasDeviceGlobalVarEntryInfo(VarName))
      return;
    auto &Entry = OffloadEntriesDeviceGlobalVar[VarName];
    if (Entry.getAddress() && hasDeviceGlobalVarEntryInfo(VarName)) {
      if (Entry.getVarSize() == 0) {
        Entry.setVarSize(VarSize);
        Entry.setLinkage(Linkage);
      }
      return;
    }
    Entry.setVarSize(VarSize);
    Entry.setLinkage(Linkage);
    Entry.setAddress(Addr);
  } else {
    if (hasDeviceGlobalVarEntryInfo(VarName)) {
      auto &Entry = OffloadEntriesDeviceGlobalVar[VarName];
      assert(Entry.isValid() && Entry.getFlags() == Flags &&
             "Entry not initialized!");
      if (Entry.getVarSize() == 0) {
        Entry.setVarSize(VarSize);
        Entry.setLinkage(Linkage);
      }
      return;
    }
    if (Flags == OffloadEntriesInfoManager::OMPTargetGlobalVarEntryIndirect)
      OffloadEntriesDeviceGlobalVar.try_emplace(VarName, OffloadingEntriesNum,
                                                Addr, VarSize, Flags, Linkage,
                                                VarName.str());
    else
      OffloadEntriesDeviceGlobalVar.try_emplace(
          VarName, OffloadingEntriesNum, Addr, VarSize, Flags, Linkage, "");
    ++OffloadingEntriesNum;
  }
}

void OffloadEntriesInfoManager::actOnDeviceGlobalVarEntriesInfo(
    const OffloadDeviceGlobalVarEntryInfoActTy &Action) {
  // Scan all target region entries and perform the provided action.
  for (const auto &E : OffloadEntriesDeviceGlobalVar)
    Action(E.getKey(), E.getValue());
}

//===----------------------------------------------------------------------===//
// CanonicalLoopInfo
//===----------------------------------------------------------------------===//

void CanonicalLoopInfo::collectControlBlocks(
    SmallVectorImpl<BasicBlock *> &BBs) {
  // We only count those BBs as control block for which we do not need to
  // reverse the CFG, i.e. not the loop body which can contain arbitrary control
  // flow. For consistency, this also means we do not add the Body block, which
  // is just the entry to the body code.
  BBs.reserve(BBs.size() + 6);
  BBs.append({getPreheader(), Header, Cond, Latch, Exit, getAfter()});
}

BasicBlock *CanonicalLoopInfo::getPreheader() const {
  assert(isValid() && "Requires a valid canonical loop");
  for (BasicBlock *Pred : predecessors(Header)) {
    if (Pred != Latch)
      return Pred;
  }
  llvm_unreachable("Missing preheader");
}

void CanonicalLoopInfo::setTripCount(Value *TripCount) {
  assert(isValid() && "Requires a valid canonical loop");

  Instruction *CmpI = &getCond()->front();
  assert(isa<CmpInst>(CmpI) && "First inst must compare IV with TripCount");
  CmpI->setOperand(1, TripCount);

#ifndef NDEBUG
  assertOK();
#endif
}

void CanonicalLoopInfo::mapIndVar(
    llvm::function_ref<Value *(Instruction *)> Updater) {
  assert(isValid() && "Requires a valid canonical loop");

  Instruction *OldIV = getIndVar();

  // Record all uses excluding those introduced by the updater. Uses by the
  // CanonicalLoopInfo itself to keep track of the number of iterations are
  // excluded.
  SmallVector<Use *> ReplacableUses;
  for (Use &U : OldIV->uses()) {
    auto *User = dyn_cast<Instruction>(U.getUser());
    if (!User)
      continue;
    if (User->getParent() == getCond())
      continue;
    if (User->getParent() == getLatch())
      continue;
    ReplacableUses.push_back(&U);
  }

  // Run the updater that may introduce new uses
  Value *NewIV = Updater(OldIV);

  // Replace the old uses with the value returned by the updater.
  for (Use *U : ReplacableUses)
    U->set(NewIV);

#ifndef NDEBUG
  assertOK();
#endif
}

void CanonicalLoopInfo::assertOK() const {
#ifndef NDEBUG
  // No constraints if this object currently does not describe a loop.
  if (!isValid())
    return;

  BasicBlock *Preheader = getPreheader();
  BasicBlock *Body = getBody();
  BasicBlock *After = getAfter();

  // Verify standard control-flow we use for OpenMP loops.
  assert(Preheader);
  assert(isa<BranchInst>(Preheader->getTerminator()) &&
         "Preheader must terminate with unconditional branch");
  assert(Preheader->getSingleSuccessor() == Header &&
         "Preheader must jump to header");

  assert(Header);
  assert(isa<BranchInst>(Header->getTerminator()) &&
         "Header must terminate with unconditional branch");
  assert(Header->getSingleSuccessor() == Cond &&
         "Header must jump to exiting block");

  assert(Cond);
  assert(Cond->getSinglePredecessor() == Header &&
         "Exiting block only reachable from header");

  assert(isa<BranchInst>(Cond->getTerminator()) &&
         "Exiting block must terminate with conditional branch");
  assert(size(successors(Cond)) == 2 &&
         "Exiting block must have two successors");
  assert(cast<BranchInst>(Cond->getTerminator())->getSuccessor(0) == Body &&
         "Exiting block's first successor jump to the body");
  assert(cast<BranchInst>(Cond->getTerminator())->getSuccessor(1) == Exit &&
         "Exiting block's second successor must exit the loop");

  assert(Body);
  assert(Body->getSinglePredecessor() == Cond &&
         "Body only reachable from exiting block");
  assert(!isa<PHINode>(Body->front()));

  assert(Latch);
  assert(isa<BranchInst>(Latch->getTerminator()) &&
         "Latch must terminate with unconditional branch");
  assert(Latch->getSingleSuccessor() == Header && "Latch must jump to header");
  // TODO: To support simple redirecting of the end of the body code that has
  // multiple; introduce another auxiliary basic block like preheader and after.
  assert(Latch->getSinglePredecessor() != nullptr);
  assert(!isa<PHINode>(Latch->front()));

  assert(Exit);
  assert(isa<BranchInst>(Exit->getTerminator()) &&
         "Exit block must terminate with unconditional branch");
  assert(Exit->getSingleSuccessor() == After &&
         "Exit block must jump to after block");

  assert(After);
  assert(After->getSinglePredecessor() == Exit &&
         "After block only reachable from exit block");
  assert(After->empty() || !isa<PHINode>(After->front()));

  Instruction *IndVar = getIndVar();
  assert(IndVar && "Canonical induction variable not found?");
  assert(isa<IntegerType>(IndVar->getType()) &&
         "Induction variable must be an integer");
  assert(cast<PHINode>(IndVar)->getParent() == Header &&
         "Induction variable must be a PHI in the loop header");
  assert(cast<PHINode>(IndVar)->getIncomingBlock(0) == Preheader);
  assert(
      cast<ConstantInt>(cast<PHINode>(IndVar)->getIncomingValue(0))->isZero());
  assert(cast<PHINode>(IndVar)->getIncomingBlock(1) == Latch);

  auto *NextIndVar = cast<PHINode>(IndVar)->getIncomingValue(1);
  assert(cast<Instruction>(NextIndVar)->getParent() == Latch);
  assert(cast<BinaryOperator>(NextIndVar)->getOpcode() == BinaryOperator::Add);
  assert(cast<BinaryOperator>(NextIndVar)->getOperand(0) == IndVar);
  assert(cast<ConstantInt>(cast<BinaryOperator>(NextIndVar)->getOperand(1))
             ->isOne());

  Value *TripCount = getTripCount();
  assert(TripCount && "Loop trip count not found?");
  assert(IndVar->getType() == TripCount->getType() &&
         "Trip count and induction variable must have the same type");

  auto *CmpI = cast<CmpInst>(&Cond->front());
  assert(CmpI->getPredicate() == CmpInst::ICMP_ULT &&
         "Exit condition must be a signed less-than comparison");
  assert(CmpI->getOperand(0) == IndVar &&
         "Exit condition must compare the induction variable");
  assert(CmpI->getOperand(1) == TripCount &&
         "Exit condition must compare with the trip count");
#endif
}

void CanonicalLoopInfo::invalidate() {
  Header = nullptr;
  Cond = nullptr;
  Latch = nullptr;
  Exit = nullptr;
}
