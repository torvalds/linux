//===-- IPO/OpenMPOpt.cpp - Collection of OpenMP specific optimizations ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// OpenMP specific optimizations:
//
// - Deduplication of runtime calls, e.g., omp_get_thread_num.
// - Replacing globalized device memory with stack memory.
// - Replacing globalized device memory with shared memory.
// - Parallel region merging.
// - Transforming generic-mode device kernels to SPMD mode.
// - Specializing the state machine for generic-mode device kernels.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/OpenMPOpt.h"

#include "llvm/ADT/EnumeratedArray.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"
#include "llvm/Frontend/OpenMP/OMPDeviceConstants.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/Assumptions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/Attributor.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CallGraphUpdater.h"

#include <algorithm>
#include <optional>
#include <string>

using namespace llvm;
using namespace omp;

#define DEBUG_TYPE "openmp-opt"

static cl::opt<bool> DisableOpenMPOptimizations(
    "openmp-opt-disable", cl::desc("Disable OpenMP specific optimizations."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> EnableParallelRegionMerging(
    "openmp-opt-enable-merging",
    cl::desc("Enable the OpenMP region merging optimization."), cl::Hidden,
    cl::init(false));

static cl::opt<bool>
    DisableInternalization("openmp-opt-disable-internalization",
                           cl::desc("Disable function internalization."),
                           cl::Hidden, cl::init(false));

static cl::opt<bool> DeduceICVValues("openmp-deduce-icv-values",
                                     cl::init(false), cl::Hidden);
static cl::opt<bool> PrintICVValues("openmp-print-icv-values", cl::init(false),
                                    cl::Hidden);
static cl::opt<bool> PrintOpenMPKernels("openmp-print-gpu-kernels",
                                        cl::init(false), cl::Hidden);

static cl::opt<bool> HideMemoryTransferLatency(
    "openmp-hide-memory-transfer-latency",
    cl::desc("[WIP] Tries to hide the latency of host to device memory"
             " transfers"),
    cl::Hidden, cl::init(false));

static cl::opt<bool> DisableOpenMPOptDeglobalization(
    "openmp-opt-disable-deglobalization",
    cl::desc("Disable OpenMP optimizations involving deglobalization."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> DisableOpenMPOptSPMDization(
    "openmp-opt-disable-spmdization",
    cl::desc("Disable OpenMP optimizations involving SPMD-ization."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> DisableOpenMPOptFolding(
    "openmp-opt-disable-folding",
    cl::desc("Disable OpenMP optimizations involving folding."), cl::Hidden,
    cl::init(false));

static cl::opt<bool> DisableOpenMPOptStateMachineRewrite(
    "openmp-opt-disable-state-machine-rewrite",
    cl::desc("Disable OpenMP optimizations that replace the state machine."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> DisableOpenMPOptBarrierElimination(
    "openmp-opt-disable-barrier-elimination",
    cl::desc("Disable OpenMP optimizations that eliminate barriers."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> PrintModuleAfterOptimizations(
    "openmp-opt-print-module-after",
    cl::desc("Print the current module after OpenMP optimizations."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> PrintModuleBeforeOptimizations(
    "openmp-opt-print-module-before",
    cl::desc("Print the current module before OpenMP optimizations."),
    cl::Hidden, cl::init(false));

static cl::opt<bool> AlwaysInlineDeviceFunctions(
    "openmp-opt-inline-device",
    cl::desc("Inline all applicible functions on the device."), cl::Hidden,
    cl::init(false));

static cl::opt<bool>
    EnableVerboseRemarks("openmp-opt-verbose-remarks",
                         cl::desc("Enables more verbose remarks."), cl::Hidden,
                         cl::init(false));

static cl::opt<unsigned>
    SetFixpointIterations("openmp-opt-max-iterations", cl::Hidden,
                          cl::desc("Maximal number of attributor iterations."),
                          cl::init(256));

static cl::opt<unsigned>
    SharedMemoryLimit("openmp-opt-shared-limit", cl::Hidden,
                      cl::desc("Maximum amount of shared memory to use."),
                      cl::init(std::numeric_limits<unsigned>::max()));

STATISTIC(NumOpenMPRuntimeCallsDeduplicated,
          "Number of OpenMP runtime calls deduplicated");
STATISTIC(NumOpenMPParallelRegionsDeleted,
          "Number of OpenMP parallel regions deleted");
STATISTIC(NumOpenMPRuntimeFunctionsIdentified,
          "Number of OpenMP runtime functions identified");
STATISTIC(NumOpenMPRuntimeFunctionUsesIdentified,
          "Number of OpenMP runtime function uses identified");
STATISTIC(NumOpenMPTargetRegionKernels,
          "Number of OpenMP target region entry points (=kernels) identified");
STATISTIC(NumNonOpenMPTargetRegionKernels,
          "Number of non-OpenMP target region kernels identified");
STATISTIC(NumOpenMPTargetRegionKernelsSPMD,
          "Number of OpenMP target region entry points (=kernels) executed in "
          "SPMD-mode instead of generic-mode");
STATISTIC(NumOpenMPTargetRegionKernelsWithoutStateMachine,
          "Number of OpenMP target region entry points (=kernels) executed in "
          "generic-mode without a state machines");
STATISTIC(NumOpenMPTargetRegionKernelsCustomStateMachineWithFallback,
          "Number of OpenMP target region entry points (=kernels) executed in "
          "generic-mode with customized state machines with fallback");
STATISTIC(NumOpenMPTargetRegionKernelsCustomStateMachineWithoutFallback,
          "Number of OpenMP target region entry points (=kernels) executed in "
          "generic-mode with customized state machines without fallback");
STATISTIC(
    NumOpenMPParallelRegionsReplacedInGPUStateMachine,
    "Number of OpenMP parallel regions replaced with ID in GPU state machines");
STATISTIC(NumOpenMPParallelRegionsMerged,
          "Number of OpenMP parallel regions merged");
STATISTIC(NumBytesMovedToSharedMemory,
          "Amount of memory pushed to shared memory");
STATISTIC(NumBarriersEliminated, "Number of redundant barriers eliminated");

#if !defined(NDEBUG)
static constexpr auto TAG = "[" DEBUG_TYPE "]";
#endif

namespace KernelInfo {

// struct ConfigurationEnvironmentTy {
//   uint8_t UseGenericStateMachine;
//   uint8_t MayUseNestedParallelism;
//   llvm::omp::OMPTgtExecModeFlags ExecMode;
//   int32_t MinThreads;
//   int32_t MaxThreads;
//   int32_t MinTeams;
//   int32_t MaxTeams;
// };

// struct DynamicEnvironmentTy {
//   uint16_t DebugIndentionLevel;
// };

// struct KernelEnvironmentTy {
//   ConfigurationEnvironmentTy Configuration;
//   IdentTy *Ident;
//   DynamicEnvironmentTy *DynamicEnv;
// };

#define KERNEL_ENVIRONMENT_IDX(MEMBER, IDX)                                    \
  constexpr const unsigned MEMBER##Idx = IDX;

KERNEL_ENVIRONMENT_IDX(Configuration, 0)
KERNEL_ENVIRONMENT_IDX(Ident, 1)

#undef KERNEL_ENVIRONMENT_IDX

#define KERNEL_ENVIRONMENT_CONFIGURATION_IDX(MEMBER, IDX)                      \
  constexpr const unsigned MEMBER##Idx = IDX;

KERNEL_ENVIRONMENT_CONFIGURATION_IDX(UseGenericStateMachine, 0)
KERNEL_ENVIRONMENT_CONFIGURATION_IDX(MayUseNestedParallelism, 1)
KERNEL_ENVIRONMENT_CONFIGURATION_IDX(ExecMode, 2)
KERNEL_ENVIRONMENT_CONFIGURATION_IDX(MinThreads, 3)
KERNEL_ENVIRONMENT_CONFIGURATION_IDX(MaxThreads, 4)
KERNEL_ENVIRONMENT_CONFIGURATION_IDX(MinTeams, 5)
KERNEL_ENVIRONMENT_CONFIGURATION_IDX(MaxTeams, 6)

#undef KERNEL_ENVIRONMENT_CONFIGURATION_IDX

#define KERNEL_ENVIRONMENT_GETTER(MEMBER, RETURNTYPE)                          \
  RETURNTYPE *get##MEMBER##FromKernelEnvironment(ConstantStruct *KernelEnvC) { \
    return cast<RETURNTYPE>(KernelEnvC->getAggregateElement(MEMBER##Idx));     \
  }

KERNEL_ENVIRONMENT_GETTER(Ident, Constant)
KERNEL_ENVIRONMENT_GETTER(Configuration, ConstantStruct)

#undef KERNEL_ENVIRONMENT_GETTER

#define KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(MEMBER)                        \
  ConstantInt *get##MEMBER##FromKernelEnvironment(                             \
      ConstantStruct *KernelEnvC) {                                            \
    ConstantStruct *ConfigC =                                                  \
        getConfigurationFromKernelEnvironment(KernelEnvC);                     \
    return dyn_cast<ConstantInt>(ConfigC->getAggregateElement(MEMBER##Idx));   \
  }

KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(UseGenericStateMachine)
KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(MayUseNestedParallelism)
KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(ExecMode)
KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(MinThreads)
KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(MaxThreads)
KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(MinTeams)
KERNEL_ENVIRONMENT_CONFIGURATION_GETTER(MaxTeams)

#undef KERNEL_ENVIRONMENT_CONFIGURATION_GETTER

GlobalVariable *
getKernelEnvironementGVFromKernelInitCB(CallBase *KernelInitCB) {
  constexpr const int InitKernelEnvironmentArgNo = 0;
  return cast<GlobalVariable>(
      KernelInitCB->getArgOperand(InitKernelEnvironmentArgNo)
          ->stripPointerCasts());
}

ConstantStruct *getKernelEnvironementFromKernelInitCB(CallBase *KernelInitCB) {
  GlobalVariable *KernelEnvGV =
      getKernelEnvironementGVFromKernelInitCB(KernelInitCB);
  return cast<ConstantStruct>(KernelEnvGV->getInitializer());
}
} // namespace KernelInfo

namespace {

struct AAHeapToShared;

struct AAICVTracker;

/// OpenMP specific information. For now, stores RFIs and ICVs also needed for
/// Attributor runs.
struct OMPInformationCache : public InformationCache {
  OMPInformationCache(Module &M, AnalysisGetter &AG,
                      BumpPtrAllocator &Allocator, SetVector<Function *> *CGSCC,
                      bool OpenMPPostLink)
      : InformationCache(M, AG, Allocator, CGSCC), OMPBuilder(M),
        OpenMPPostLink(OpenMPPostLink) {

    OMPBuilder.Config.IsTargetDevice = isOpenMPDevice(OMPBuilder.M);
    OMPBuilder.initialize();
    initializeRuntimeFunctions(M);
    initializeInternalControlVars();
  }

  /// Generic information that describes an internal control variable.
  struct InternalControlVarInfo {
    /// The kind, as described by InternalControlVar enum.
    InternalControlVar Kind;

    /// The name of the ICV.
    StringRef Name;

    /// Environment variable associated with this ICV.
    StringRef EnvVarName;

    /// Initial value kind.
    ICVInitValue InitKind;

    /// Initial value.
    ConstantInt *InitValue;

    /// Setter RTL function associated with this ICV.
    RuntimeFunction Setter;

    /// Getter RTL function associated with this ICV.
    RuntimeFunction Getter;

    /// RTL Function corresponding to the override clause of this ICV
    RuntimeFunction Clause;
  };

  /// Generic information that describes a runtime function
  struct RuntimeFunctionInfo {

    /// The kind, as described by the RuntimeFunction enum.
    RuntimeFunction Kind;

    /// The name of the function.
    StringRef Name;

    /// Flag to indicate a variadic function.
    bool IsVarArg;

    /// The return type of the function.
    Type *ReturnType;

    /// The argument types of the function.
    SmallVector<Type *, 8> ArgumentTypes;

    /// The declaration if available.
    Function *Declaration = nullptr;

    /// Uses of this runtime function per function containing the use.
    using UseVector = SmallVector<Use *, 16>;

    /// Clear UsesMap for runtime function.
    void clearUsesMap() { UsesMap.clear(); }

    /// Boolean conversion that is true if the runtime function was found.
    operator bool() const { return Declaration; }

    /// Return the vector of uses in function \p F.
    UseVector &getOrCreateUseVector(Function *F) {
      std::shared_ptr<UseVector> &UV = UsesMap[F];
      if (!UV)
        UV = std::make_shared<UseVector>();
      return *UV;
    }

    /// Return the vector of uses in function \p F or `nullptr` if there are
    /// none.
    const UseVector *getUseVector(Function &F) const {
      auto I = UsesMap.find(&F);
      if (I != UsesMap.end())
        return I->second.get();
      return nullptr;
    }

    /// Return how many functions contain uses of this runtime function.
    size_t getNumFunctionsWithUses() const { return UsesMap.size(); }

    /// Return the number of arguments (or the minimal number for variadic
    /// functions).
    size_t getNumArgs() const { return ArgumentTypes.size(); }

    /// Run the callback \p CB on each use and forget the use if the result is
    /// true. The callback will be fed the function in which the use was
    /// encountered as second argument.
    void foreachUse(SmallVectorImpl<Function *> &SCC,
                    function_ref<bool(Use &, Function &)> CB) {
      for (Function *F : SCC)
        foreachUse(CB, F);
    }

    /// Run the callback \p CB on each use within the function \p F and forget
    /// the use if the result is true.
    void foreachUse(function_ref<bool(Use &, Function &)> CB, Function *F) {
      SmallVector<unsigned, 8> ToBeDeleted;
      ToBeDeleted.clear();

      unsigned Idx = 0;
      UseVector &UV = getOrCreateUseVector(F);

      for (Use *U : UV) {
        if (CB(*U, *F))
          ToBeDeleted.push_back(Idx);
        ++Idx;
      }

      // Remove the to-be-deleted indices in reverse order as prior
      // modifications will not modify the smaller indices.
      while (!ToBeDeleted.empty()) {
        unsigned Idx = ToBeDeleted.pop_back_val();
        UV[Idx] = UV.back();
        UV.pop_back();
      }
    }

  private:
    /// Map from functions to all uses of this runtime function contained in
    /// them.
    DenseMap<Function *, std::shared_ptr<UseVector>> UsesMap;

  public:
    /// Iterators for the uses of this runtime function.
    decltype(UsesMap)::iterator begin() { return UsesMap.begin(); }
    decltype(UsesMap)::iterator end() { return UsesMap.end(); }
  };

  /// An OpenMP-IR-Builder instance
  OpenMPIRBuilder OMPBuilder;

  /// Map from runtime function kind to the runtime function description.
  EnumeratedArray<RuntimeFunctionInfo, RuntimeFunction,
                  RuntimeFunction::OMPRTL___last>
      RFIs;

  /// Map from function declarations/definitions to their runtime enum type.
  DenseMap<Function *, RuntimeFunction> RuntimeFunctionIDMap;

  /// Map from ICV kind to the ICV description.
  EnumeratedArray<InternalControlVarInfo, InternalControlVar,
                  InternalControlVar::ICV___last>
      ICVs;

  /// Helper to initialize all internal control variable information for those
  /// defined in OMPKinds.def.
  void initializeInternalControlVars() {
#define ICV_RT_SET(_Name, RTL)                                                 \
  {                                                                            \
    auto &ICV = ICVs[_Name];                                                   \
    ICV.Setter = RTL;                                                          \
  }
#define ICV_RT_GET(Name, RTL)                                                  \
  {                                                                            \
    auto &ICV = ICVs[Name];                                                    \
    ICV.Getter = RTL;                                                          \
  }
#define ICV_DATA_ENV(Enum, _Name, _EnvVarName, Init)                           \
  {                                                                            \
    auto &ICV = ICVs[Enum];                                                    \
    ICV.Name = _Name;                                                          \
    ICV.Kind = Enum;                                                           \
    ICV.InitKind = Init;                                                       \
    ICV.EnvVarName = _EnvVarName;                                              \
    switch (ICV.InitKind) {                                                    \
    case ICV_IMPLEMENTATION_DEFINED:                                           \
      ICV.InitValue = nullptr;                                                 \
      break;                                                                   \
    case ICV_ZERO:                                                             \
      ICV.InitValue = ConstantInt::get(                                        \
          Type::getInt32Ty(OMPBuilder.Int32->getContext()), 0);                \
      break;                                                                   \
    case ICV_FALSE:                                                            \
      ICV.InitValue = ConstantInt::getFalse(OMPBuilder.Int1->getContext());    \
      break;                                                                   \
    case ICV_LAST:                                                             \
      break;                                                                   \
    }                                                                          \
  }
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }

  /// Returns true if the function declaration \p F matches the runtime
  /// function types, that is, return type \p RTFRetType, and argument types
  /// \p RTFArgTypes.
  static bool declMatchesRTFTypes(Function *F, Type *RTFRetType,
                                  SmallVector<Type *, 8> &RTFArgTypes) {
    // TODO: We should output information to the user (under debug output
    //       and via remarks).

    if (!F)
      return false;
    if (F->getReturnType() != RTFRetType)
      return false;
    if (F->arg_size() != RTFArgTypes.size())
      return false;

    auto *RTFTyIt = RTFArgTypes.begin();
    for (Argument &Arg : F->args()) {
      if (Arg.getType() != *RTFTyIt)
        return false;

      ++RTFTyIt;
    }

    return true;
  }

  // Helper to collect all uses of the declaration in the UsesMap.
  unsigned collectUses(RuntimeFunctionInfo &RFI, bool CollectStats = true) {
    unsigned NumUses = 0;
    if (!RFI.Declaration)
      return NumUses;
    OMPBuilder.addAttributes(RFI.Kind, *RFI.Declaration);

    if (CollectStats) {
      NumOpenMPRuntimeFunctionsIdentified += 1;
      NumOpenMPRuntimeFunctionUsesIdentified += RFI.Declaration->getNumUses();
    }

    // TODO: We directly convert uses into proper calls and unknown uses.
    for (Use &U : RFI.Declaration->uses()) {
      if (Instruction *UserI = dyn_cast<Instruction>(U.getUser())) {
        if (!CGSCC || CGSCC->empty() || CGSCC->contains(UserI->getFunction())) {
          RFI.getOrCreateUseVector(UserI->getFunction()).push_back(&U);
          ++NumUses;
        }
      } else {
        RFI.getOrCreateUseVector(nullptr).push_back(&U);
        ++NumUses;
      }
    }
    return NumUses;
  }

  // Helper function to recollect uses of a runtime function.
  void recollectUsesForFunction(RuntimeFunction RTF) {
    auto &RFI = RFIs[RTF];
    RFI.clearUsesMap();
    collectUses(RFI, /*CollectStats*/ false);
  }

  // Helper function to recollect uses of all runtime functions.
  void recollectUses() {
    for (int Idx = 0; Idx < RFIs.size(); ++Idx)
      recollectUsesForFunction(static_cast<RuntimeFunction>(Idx));
  }

  // Helper function to inherit the calling convention of the function callee.
  void setCallingConvention(FunctionCallee Callee, CallInst *CI) {
    if (Function *Fn = dyn_cast<Function>(Callee.getCallee()))
      CI->setCallingConv(Fn->getCallingConv());
  }

  // Helper function to determine if it's legal to create a call to the runtime
  // functions.
  bool runtimeFnsAvailable(ArrayRef<RuntimeFunction> Fns) {
    // We can always emit calls if we haven't yet linked in the runtime.
    if (!OpenMPPostLink)
      return true;

    // Once the runtime has been already been linked in we cannot emit calls to
    // any undefined functions.
    for (RuntimeFunction Fn : Fns) {
      RuntimeFunctionInfo &RFI = RFIs[Fn];

      if (RFI.Declaration && RFI.Declaration->isDeclaration())
        return false;
    }
    return true;
  }

  /// Helper to initialize all runtime function information for those defined
  /// in OpenMPKinds.def.
  void initializeRuntimeFunctions(Module &M) {

    // Helper macros for handling __VA_ARGS__ in OMP_RTL
#define OMP_TYPE(VarName, ...)                                                 \
  Type *VarName = OMPBuilder.VarName;                                          \
  (void)VarName;

#define OMP_ARRAY_TYPE(VarName, ...)                                           \
  ArrayType *VarName##Ty = OMPBuilder.VarName##Ty;                             \
  (void)VarName##Ty;                                                           \
  PointerType *VarName##PtrTy = OMPBuilder.VarName##PtrTy;                     \
  (void)VarName##PtrTy;

#define OMP_FUNCTION_TYPE(VarName, ...)                                        \
  FunctionType *VarName = OMPBuilder.VarName;                                  \
  (void)VarName;                                                               \
  PointerType *VarName##Ptr = OMPBuilder.VarName##Ptr;                         \
  (void)VarName##Ptr;

#define OMP_STRUCT_TYPE(VarName, ...)                                          \
  StructType *VarName = OMPBuilder.VarName;                                    \
  (void)VarName;                                                               \
  PointerType *VarName##Ptr = OMPBuilder.VarName##Ptr;                         \
  (void)VarName##Ptr;

#define OMP_RTL(_Enum, _Name, _IsVarArg, _ReturnType, ...)                     \
  {                                                                            \
    SmallVector<Type *, 8> ArgsTypes({__VA_ARGS__});                           \
    Function *F = M.getFunction(_Name);                                        \
    RTLFunctions.insert(F);                                                    \
    if (declMatchesRTFTypes(F, OMPBuilder._ReturnType, ArgsTypes)) {           \
      RuntimeFunctionIDMap[F] = _Enum;                                         \
      auto &RFI = RFIs[_Enum];                                                 \
      RFI.Kind = _Enum;                                                        \
      RFI.Name = _Name;                                                        \
      RFI.IsVarArg = _IsVarArg;                                                \
      RFI.ReturnType = OMPBuilder._ReturnType;                                 \
      RFI.ArgumentTypes = std::move(ArgsTypes);                                \
      RFI.Declaration = F;                                                     \
      unsigned NumUses = collectUses(RFI);                                     \
      (void)NumUses;                                                           \
      LLVM_DEBUG({                                                             \
        dbgs() << TAG << RFI.Name << (RFI.Declaration ? "" : " not")           \
               << " found\n";                                                  \
        if (RFI.Declaration)                                                   \
          dbgs() << TAG << "-> got " << NumUses << " uses in "                 \
                 << RFI.getNumFunctionsWithUses()                              \
                 << " different functions.\n";                                 \
      });                                                                      \
    }                                                                          \
  }
#include "llvm/Frontend/OpenMP/OMPKinds.def"

    // Remove the `noinline` attribute from `__kmpc`, `ompx::` and `omp_`
    // functions, except if `optnone` is present.
    if (isOpenMPDevice(M)) {
      for (Function &F : M) {
        for (StringRef Prefix : {"__kmpc", "_ZN4ompx", "omp_"})
          if (F.hasFnAttribute(Attribute::NoInline) &&
              F.getName().starts_with(Prefix) &&
              !F.hasFnAttribute(Attribute::OptimizeNone))
            F.removeFnAttr(Attribute::NoInline);
      }
    }

    // TODO: We should attach the attributes defined in OMPKinds.def.
  }

  /// Collection of known OpenMP runtime functions..
  DenseSet<const Function *> RTLFunctions;

  /// Indicates if we have already linked in the OpenMP device library.
  bool OpenMPPostLink = false;
};

template <typename Ty, bool InsertInvalidates = true>
struct BooleanStateWithSetVector : public BooleanState {
  bool contains(const Ty &Elem) const { return Set.contains(Elem); }
  bool insert(const Ty &Elem) {
    if (InsertInvalidates)
      BooleanState::indicatePessimisticFixpoint();
    return Set.insert(Elem);
  }

  const Ty &operator[](int Idx) const { return Set[Idx]; }
  bool operator==(const BooleanStateWithSetVector &RHS) const {
    return BooleanState::operator==(RHS) && Set == RHS.Set;
  }
  bool operator!=(const BooleanStateWithSetVector &RHS) const {
    return !(*this == RHS);
  }

  bool empty() const { return Set.empty(); }
  size_t size() const { return Set.size(); }

  /// "Clamp" this state with \p RHS.
  BooleanStateWithSetVector &operator^=(const BooleanStateWithSetVector &RHS) {
    BooleanState::operator^=(RHS);
    Set.insert(RHS.Set.begin(), RHS.Set.end());
    return *this;
  }

private:
  /// A set to keep track of elements.
  SetVector<Ty> Set;

public:
  typename decltype(Set)::iterator begin() { return Set.begin(); }
  typename decltype(Set)::iterator end() { return Set.end(); }
  typename decltype(Set)::const_iterator begin() const { return Set.begin(); }
  typename decltype(Set)::const_iterator end() const { return Set.end(); }
};

template <typename Ty, bool InsertInvalidates = true>
using BooleanStateWithPtrSetVector =
    BooleanStateWithSetVector<Ty *, InsertInvalidates>;

struct KernelInfoState : AbstractState {
  /// Flag to track if we reached a fixpoint.
  bool IsAtFixpoint = false;

  /// The parallel regions (identified by the outlined parallel functions) that
  /// can be reached from the associated function.
  BooleanStateWithPtrSetVector<CallBase, /* InsertInvalidates */ false>
      ReachedKnownParallelRegions;

  /// State to track what parallel region we might reach.
  BooleanStateWithPtrSetVector<CallBase> ReachedUnknownParallelRegions;

  /// State to track if we are in SPMD-mode, assumed or know, and why we decided
  /// we cannot be. If it is assumed, then RequiresFullRuntime should also be
  /// false.
  BooleanStateWithPtrSetVector<Instruction, false> SPMDCompatibilityTracker;

  /// The __kmpc_target_init call in this kernel, if any. If we find more than
  /// one we abort as the kernel is malformed.
  CallBase *KernelInitCB = nullptr;

  /// The constant kernel environement as taken from and passed to
  /// __kmpc_target_init.
  ConstantStruct *KernelEnvC = nullptr;

  /// The __kmpc_target_deinit call in this kernel, if any. If we find more than
  /// one we abort as the kernel is malformed.
  CallBase *KernelDeinitCB = nullptr;

  /// Flag to indicate if the associated function is a kernel entry.
  bool IsKernelEntry = false;

  /// State to track what kernel entries can reach the associated function.
  BooleanStateWithPtrSetVector<Function, false> ReachingKernelEntries;

  /// State to indicate if we can track parallel level of the associated
  /// function. We will give up tracking if we encounter unknown caller or the
  /// caller is __kmpc_parallel_51.
  BooleanStateWithSetVector<uint8_t> ParallelLevels;

  /// Flag that indicates if the kernel has nested Parallelism
  bool NestedParallelism = false;

  /// Abstract State interface
  ///{

  KernelInfoState() = default;
  KernelInfoState(bool BestState) {
    if (!BestState)
      indicatePessimisticFixpoint();
  }

  /// See AbstractState::isValidState(...)
  bool isValidState() const override { return true; }

  /// See AbstractState::isAtFixpoint(...)
  bool isAtFixpoint() const override { return IsAtFixpoint; }

  /// See AbstractState::indicatePessimisticFixpoint(...)
  ChangeStatus indicatePessimisticFixpoint() override {
    IsAtFixpoint = true;
    ParallelLevels.indicatePessimisticFixpoint();
    ReachingKernelEntries.indicatePessimisticFixpoint();
    SPMDCompatibilityTracker.indicatePessimisticFixpoint();
    ReachedKnownParallelRegions.indicatePessimisticFixpoint();
    ReachedUnknownParallelRegions.indicatePessimisticFixpoint();
    NestedParallelism = true;
    return ChangeStatus::CHANGED;
  }

  /// See AbstractState::indicateOptimisticFixpoint(...)
  ChangeStatus indicateOptimisticFixpoint() override {
    IsAtFixpoint = true;
    ParallelLevels.indicateOptimisticFixpoint();
    ReachingKernelEntries.indicateOptimisticFixpoint();
    SPMDCompatibilityTracker.indicateOptimisticFixpoint();
    ReachedKnownParallelRegions.indicateOptimisticFixpoint();
    ReachedUnknownParallelRegions.indicateOptimisticFixpoint();
    return ChangeStatus::UNCHANGED;
  }

  /// Return the assumed state
  KernelInfoState &getAssumed() { return *this; }
  const KernelInfoState &getAssumed() const { return *this; }

  bool operator==(const KernelInfoState &RHS) const {
    if (SPMDCompatibilityTracker != RHS.SPMDCompatibilityTracker)
      return false;
    if (ReachedKnownParallelRegions != RHS.ReachedKnownParallelRegions)
      return false;
    if (ReachedUnknownParallelRegions != RHS.ReachedUnknownParallelRegions)
      return false;
    if (ReachingKernelEntries != RHS.ReachingKernelEntries)
      return false;
    if (ParallelLevels != RHS.ParallelLevels)
      return false;
    if (NestedParallelism != RHS.NestedParallelism)
      return false;
    return true;
  }

  /// Returns true if this kernel contains any OpenMP parallel regions.
  bool mayContainParallelRegion() {
    return !ReachedKnownParallelRegions.empty() ||
           !ReachedUnknownParallelRegions.empty();
  }

  /// Return empty set as the best state of potential values.
  static KernelInfoState getBestState() { return KernelInfoState(true); }

  static KernelInfoState getBestState(KernelInfoState &KIS) {
    return getBestState();
  }

  /// Return full set as the worst state of potential values.
  static KernelInfoState getWorstState() { return KernelInfoState(false); }

  /// "Clamp" this state with \p KIS.
  KernelInfoState operator^=(const KernelInfoState &KIS) {
    // Do not merge two different _init and _deinit call sites.
    if (KIS.KernelInitCB) {
      if (KernelInitCB && KernelInitCB != KIS.KernelInitCB)
        llvm_unreachable("Kernel that calls another kernel violates OpenMP-Opt "
                         "assumptions.");
      KernelInitCB = KIS.KernelInitCB;
    }
    if (KIS.KernelDeinitCB) {
      if (KernelDeinitCB && KernelDeinitCB != KIS.KernelDeinitCB)
        llvm_unreachable("Kernel that calls another kernel violates OpenMP-Opt "
                         "assumptions.");
      KernelDeinitCB = KIS.KernelDeinitCB;
    }
    if (KIS.KernelEnvC) {
      if (KernelEnvC && KernelEnvC != KIS.KernelEnvC)
        llvm_unreachable("Kernel that calls another kernel violates OpenMP-Opt "
                         "assumptions.");
      KernelEnvC = KIS.KernelEnvC;
    }
    SPMDCompatibilityTracker ^= KIS.SPMDCompatibilityTracker;
    ReachedKnownParallelRegions ^= KIS.ReachedKnownParallelRegions;
    ReachedUnknownParallelRegions ^= KIS.ReachedUnknownParallelRegions;
    NestedParallelism |= KIS.NestedParallelism;
    return *this;
  }

  KernelInfoState operator&=(const KernelInfoState &KIS) {
    return (*this ^= KIS);
  }

  ///}
};

/// Used to map the values physically (in the IR) stored in an offload
/// array, to a vector in memory.
struct OffloadArray {
  /// Physical array (in the IR).
  AllocaInst *Array = nullptr;
  /// Mapped values.
  SmallVector<Value *, 8> StoredValues;
  /// Last stores made in the offload array.
  SmallVector<StoreInst *, 8> LastAccesses;

  OffloadArray() = default;

  /// Initializes the OffloadArray with the values stored in \p Array before
  /// instruction \p Before is reached. Returns false if the initialization
  /// fails.
  /// This MUST be used immediately after the construction of the object.
  bool initialize(AllocaInst &Array, Instruction &Before) {
    if (!Array.getAllocatedType()->isArrayTy())
      return false;

    if (!getValues(Array, Before))
      return false;

    this->Array = &Array;
    return true;
  }

  static const unsigned DeviceIDArgNum = 1;
  static const unsigned BasePtrsArgNum = 3;
  static const unsigned PtrsArgNum = 4;
  static const unsigned SizesArgNum = 5;

private:
  /// Traverses the BasicBlock where \p Array is, collecting the stores made to
  /// \p Array, leaving StoredValues with the values stored before the
  /// instruction \p Before is reached.
  bool getValues(AllocaInst &Array, Instruction &Before) {
    // Initialize container.
    const uint64_t NumValues = Array.getAllocatedType()->getArrayNumElements();
    StoredValues.assign(NumValues, nullptr);
    LastAccesses.assign(NumValues, nullptr);

    // TODO: This assumes the instruction \p Before is in the same
    //  BasicBlock as Array. Make it general, for any control flow graph.
    BasicBlock *BB = Array.getParent();
    if (BB != Before.getParent())
      return false;

    const DataLayout &DL = Array.getDataLayout();
    const unsigned int PointerSize = DL.getPointerSize();

    for (Instruction &I : *BB) {
      if (&I == &Before)
        break;

      if (!isa<StoreInst>(&I))
        continue;

      auto *S = cast<StoreInst>(&I);
      int64_t Offset = -1;
      auto *Dst =
          GetPointerBaseWithConstantOffset(S->getPointerOperand(), Offset, DL);
      if (Dst == &Array) {
        int64_t Idx = Offset / PointerSize;
        StoredValues[Idx] = getUnderlyingObject(S->getValueOperand());
        LastAccesses[Idx] = S;
      }
    }

    return isFilled();
  }

  /// Returns true if all values in StoredValues and
  /// LastAccesses are not nullptrs.
  bool isFilled() {
    const unsigned NumValues = StoredValues.size();
    for (unsigned I = 0; I < NumValues; ++I) {
      if (!StoredValues[I] || !LastAccesses[I])
        return false;
    }

    return true;
  }
};

struct OpenMPOpt {

  using OptimizationRemarkGetter =
      function_ref<OptimizationRemarkEmitter &(Function *)>;

  OpenMPOpt(SmallVectorImpl<Function *> &SCC, CallGraphUpdater &CGUpdater,
            OptimizationRemarkGetter OREGetter,
            OMPInformationCache &OMPInfoCache, Attributor &A)
      : M(*(*SCC.begin())->getParent()), SCC(SCC), CGUpdater(CGUpdater),
        OREGetter(OREGetter), OMPInfoCache(OMPInfoCache), A(A) {}

  /// Check if any remarks are enabled for openmp-opt
  bool remarksEnabled() {
    auto &Ctx = M.getContext();
    return Ctx.getDiagHandlerPtr()->isAnyRemarkEnabled(DEBUG_TYPE);
  }

  /// Run all OpenMP optimizations on the underlying SCC.
  bool run(bool IsModulePass) {
    if (SCC.empty())
      return false;

    bool Changed = false;

    LLVM_DEBUG(dbgs() << TAG << "Run on SCC with " << SCC.size()
                      << " functions\n");

    if (IsModulePass) {
      Changed |= runAttributor(IsModulePass);

      // Recollect uses, in case Attributor deleted any.
      OMPInfoCache.recollectUses();

      // TODO: This should be folded into buildCustomStateMachine.
      Changed |= rewriteDeviceCodeStateMachine();

      if (remarksEnabled())
        analysisGlobalization();
    } else {
      if (PrintICVValues)
        printICVs();
      if (PrintOpenMPKernels)
        printKernels();

      Changed |= runAttributor(IsModulePass);

      // Recollect uses, in case Attributor deleted any.
      OMPInfoCache.recollectUses();

      Changed |= deleteParallelRegions();

      if (HideMemoryTransferLatency)
        Changed |= hideMemTransfersLatency();
      Changed |= deduplicateRuntimeCalls();
      if (EnableParallelRegionMerging) {
        if (mergeParallelRegions()) {
          deduplicateRuntimeCalls();
          Changed = true;
        }
      }
    }

    if (OMPInfoCache.OpenMPPostLink)
      Changed |= removeRuntimeSymbols();

    return Changed;
  }

  /// Print initial ICV values for testing.
  /// FIXME: This should be done from the Attributor once it is added.
  void printICVs() const {
    InternalControlVar ICVs[] = {ICV_nthreads, ICV_active_levels, ICV_cancel,
                                 ICV_proc_bind};

    for (Function *F : SCC) {
      for (auto ICV : ICVs) {
        auto ICVInfo = OMPInfoCache.ICVs[ICV];
        auto Remark = [&](OptimizationRemarkAnalysis ORA) {
          return ORA << "OpenMP ICV " << ore::NV("OpenMPICV", ICVInfo.Name)
                     << " Value: "
                     << (ICVInfo.InitValue
                             ? toString(ICVInfo.InitValue->getValue(), 10, true)
                             : "IMPLEMENTATION_DEFINED");
        };

        emitRemark<OptimizationRemarkAnalysis>(F, "OpenMPICVTracker", Remark);
      }
    }
  }

  /// Print OpenMP GPU kernels for testing.
  void printKernels() const {
    for (Function *F : SCC) {
      if (!omp::isOpenMPKernel(*F))
        continue;

      auto Remark = [&](OptimizationRemarkAnalysis ORA) {
        return ORA << "OpenMP GPU kernel "
                   << ore::NV("OpenMPGPUKernel", F->getName()) << "\n";
      };

      emitRemark<OptimizationRemarkAnalysis>(F, "OpenMPGPU", Remark);
    }
  }

  /// Return the call if \p U is a callee use in a regular call. If \p RFI is
  /// given it has to be the callee or a nullptr is returned.
  static CallInst *getCallIfRegularCall(
      Use &U, OMPInformationCache::RuntimeFunctionInfo *RFI = nullptr) {
    CallInst *CI = dyn_cast<CallInst>(U.getUser());
    if (CI && CI->isCallee(&U) && !CI->hasOperandBundles() &&
        (!RFI ||
         (RFI->Declaration && CI->getCalledFunction() == RFI->Declaration)))
      return CI;
    return nullptr;
  }

  /// Return the call if \p V is a regular call. If \p RFI is given it has to be
  /// the callee or a nullptr is returned.
  static CallInst *getCallIfRegularCall(
      Value &V, OMPInformationCache::RuntimeFunctionInfo *RFI = nullptr) {
    CallInst *CI = dyn_cast<CallInst>(&V);
    if (CI && !CI->hasOperandBundles() &&
        (!RFI ||
         (RFI->Declaration && CI->getCalledFunction() == RFI->Declaration)))
      return CI;
    return nullptr;
  }

private:
  /// Merge parallel regions when it is safe.
  bool mergeParallelRegions() {
    const unsigned CallbackCalleeOperand = 2;
    const unsigned CallbackFirstArgOperand = 3;
    using InsertPointTy = OpenMPIRBuilder::InsertPointTy;

    // Check if there are any __kmpc_fork_call calls to merge.
    OMPInformationCache::RuntimeFunctionInfo &RFI =
        OMPInfoCache.RFIs[OMPRTL___kmpc_fork_call];

    if (!RFI.Declaration)
      return false;

    // Unmergable calls that prevent merging a parallel region.
    OMPInformationCache::RuntimeFunctionInfo UnmergableCallsInfo[] = {
        OMPInfoCache.RFIs[OMPRTL___kmpc_push_proc_bind],
        OMPInfoCache.RFIs[OMPRTL___kmpc_push_num_threads],
    };

    bool Changed = false;
    LoopInfo *LI = nullptr;
    DominatorTree *DT = nullptr;

    SmallDenseMap<BasicBlock *, SmallPtrSet<Instruction *, 4>> BB2PRMap;

    BasicBlock *StartBB = nullptr, *EndBB = nullptr;
    auto BodyGenCB = [&](InsertPointTy AllocaIP, InsertPointTy CodeGenIP) {
      BasicBlock *CGStartBB = CodeGenIP.getBlock();
      BasicBlock *CGEndBB =
          SplitBlock(CGStartBB, &*CodeGenIP.getPoint(), DT, LI);
      assert(StartBB != nullptr && "StartBB should not be null");
      CGStartBB->getTerminator()->setSuccessor(0, StartBB);
      assert(EndBB != nullptr && "EndBB should not be null");
      EndBB->getTerminator()->setSuccessor(0, CGEndBB);
    };

    auto PrivCB = [&](InsertPointTy AllocaIP, InsertPointTy CodeGenIP, Value &,
                      Value &Inner, Value *&ReplacementValue) -> InsertPointTy {
      ReplacementValue = &Inner;
      return CodeGenIP;
    };

    auto FiniCB = [&](InsertPointTy CodeGenIP) {};

    /// Create a sequential execution region within a merged parallel region,
    /// encapsulated in a master construct with a barrier for synchronization.
    auto CreateSequentialRegion = [&](Function *OuterFn,
                                      BasicBlock *OuterPredBB,
                                      Instruction *SeqStartI,
                                      Instruction *SeqEndI) {
      // Isolate the instructions of the sequential region to a separate
      // block.
      BasicBlock *ParentBB = SeqStartI->getParent();
      BasicBlock *SeqEndBB =
          SplitBlock(ParentBB, SeqEndI->getNextNode(), DT, LI);
      BasicBlock *SeqAfterBB =
          SplitBlock(SeqEndBB, &*SeqEndBB->getFirstInsertionPt(), DT, LI);
      BasicBlock *SeqStartBB =
          SplitBlock(ParentBB, SeqStartI, DT, LI, nullptr, "seq.par.merged");

      assert(ParentBB->getUniqueSuccessor() == SeqStartBB &&
             "Expected a different CFG");
      const DebugLoc DL = ParentBB->getTerminator()->getDebugLoc();
      ParentBB->getTerminator()->eraseFromParent();

      auto BodyGenCB = [&](InsertPointTy AllocaIP, InsertPointTy CodeGenIP) {
        BasicBlock *CGStartBB = CodeGenIP.getBlock();
        BasicBlock *CGEndBB =
            SplitBlock(CGStartBB, &*CodeGenIP.getPoint(), DT, LI);
        assert(SeqStartBB != nullptr && "SeqStartBB should not be null");
        CGStartBB->getTerminator()->setSuccessor(0, SeqStartBB);
        assert(SeqEndBB != nullptr && "SeqEndBB should not be null");
        SeqEndBB->getTerminator()->setSuccessor(0, CGEndBB);
      };
      auto FiniCB = [&](InsertPointTy CodeGenIP) {};

      // Find outputs from the sequential region to outside users and
      // broadcast their values to them.
      for (Instruction &I : *SeqStartBB) {
        SmallPtrSet<Instruction *, 4> OutsideUsers;
        for (User *Usr : I.users()) {
          Instruction &UsrI = *cast<Instruction>(Usr);
          // Ignore outputs to LT intrinsics, code extraction for the merged
          // parallel region will fix them.
          if (UsrI.isLifetimeStartOrEnd())
            continue;

          if (UsrI.getParent() != SeqStartBB)
            OutsideUsers.insert(&UsrI);
        }

        if (OutsideUsers.empty())
          continue;

        // Emit an alloca in the outer region to store the broadcasted
        // value.
        const DataLayout &DL = M.getDataLayout();
        AllocaInst *AllocaI = new AllocaInst(
            I.getType(), DL.getAllocaAddrSpace(), nullptr,
            I.getName() + ".seq.output.alloc", OuterFn->front().begin());

        // Emit a store instruction in the sequential BB to update the
        // value.
        new StoreInst(&I, AllocaI, SeqStartBB->getTerminator()->getIterator());

        // Emit a load instruction and replace the use of the output value
        // with it.
        for (Instruction *UsrI : OutsideUsers) {
          LoadInst *LoadI = new LoadInst(I.getType(), AllocaI,
                                         I.getName() + ".seq.output.load",
                                         UsrI->getIterator());
          UsrI->replaceUsesOfWith(&I, LoadI);
        }
      }

      OpenMPIRBuilder::LocationDescription Loc(
          InsertPointTy(ParentBB, ParentBB->end()), DL);
      InsertPointTy SeqAfterIP =
          OMPInfoCache.OMPBuilder.createMaster(Loc, BodyGenCB, FiniCB);

      OMPInfoCache.OMPBuilder.createBarrier(SeqAfterIP, OMPD_parallel);

      BranchInst::Create(SeqAfterBB, SeqAfterIP.getBlock());

      LLVM_DEBUG(dbgs() << TAG << "After sequential inlining " << *OuterFn
                        << "\n");
    };

    // Helper to merge the __kmpc_fork_call calls in MergableCIs. They are all
    // contained in BB and only separated by instructions that can be
    // redundantly executed in parallel. The block BB is split before the first
    // call (in MergableCIs) and after the last so the entire region we merge
    // into a single parallel region is contained in a single basic block
    // without any other instructions. We use the OpenMPIRBuilder to outline
    // that block and call the resulting function via __kmpc_fork_call.
    auto Merge = [&](const SmallVectorImpl<CallInst *> &MergableCIs,
                     BasicBlock *BB) {
      // TODO: Change the interface to allow single CIs expanded, e.g, to
      // include an outer loop.
      assert(MergableCIs.size() > 1 && "Assumed multiple mergable CIs");

      auto Remark = [&](OptimizationRemark OR) {
        OR << "Parallel region merged with parallel region"
           << (MergableCIs.size() > 2 ? "s" : "") << " at ";
        for (auto *CI : llvm::drop_begin(MergableCIs)) {
          OR << ore::NV("OpenMPParallelMerge", CI->getDebugLoc());
          if (CI != MergableCIs.back())
            OR << ", ";
        }
        return OR << ".";
      };

      emitRemark<OptimizationRemark>(MergableCIs.front(), "OMP150", Remark);

      Function *OriginalFn = BB->getParent();
      LLVM_DEBUG(dbgs() << TAG << "Merge " << MergableCIs.size()
                        << " parallel regions in " << OriginalFn->getName()
                        << "\n");

      // Isolate the calls to merge in a separate block.
      EndBB = SplitBlock(BB, MergableCIs.back()->getNextNode(), DT, LI);
      BasicBlock *AfterBB =
          SplitBlock(EndBB, &*EndBB->getFirstInsertionPt(), DT, LI);
      StartBB = SplitBlock(BB, MergableCIs.front(), DT, LI, nullptr,
                           "omp.par.merged");

      assert(BB->getUniqueSuccessor() == StartBB && "Expected a different CFG");
      const DebugLoc DL = BB->getTerminator()->getDebugLoc();
      BB->getTerminator()->eraseFromParent();

      // Create sequential regions for sequential instructions that are
      // in-between mergable parallel regions.
      for (auto *It = MergableCIs.begin(), *End = MergableCIs.end() - 1;
           It != End; ++It) {
        Instruction *ForkCI = *It;
        Instruction *NextForkCI = *(It + 1);

        // Continue if there are not in-between instructions.
        if (ForkCI->getNextNode() == NextForkCI)
          continue;

        CreateSequentialRegion(OriginalFn, BB, ForkCI->getNextNode(),
                               NextForkCI->getPrevNode());
      }

      OpenMPIRBuilder::LocationDescription Loc(InsertPointTy(BB, BB->end()),
                                               DL);
      IRBuilder<>::InsertPoint AllocaIP(
          &OriginalFn->getEntryBlock(),
          OriginalFn->getEntryBlock().getFirstInsertionPt());
      // Create the merged parallel region with default proc binding, to
      // avoid overriding binding settings, and without explicit cancellation.
      InsertPointTy AfterIP = OMPInfoCache.OMPBuilder.createParallel(
          Loc, AllocaIP, BodyGenCB, PrivCB, FiniCB, nullptr, nullptr,
          OMP_PROC_BIND_default, /* IsCancellable */ false);
      BranchInst::Create(AfterBB, AfterIP.getBlock());

      // Perform the actual outlining.
      OMPInfoCache.OMPBuilder.finalize(OriginalFn);

      Function *OutlinedFn = MergableCIs.front()->getCaller();

      // Replace the __kmpc_fork_call calls with direct calls to the outlined
      // callbacks.
      SmallVector<Value *, 8> Args;
      for (auto *CI : MergableCIs) {
        Value *Callee = CI->getArgOperand(CallbackCalleeOperand);
        FunctionType *FT = OMPInfoCache.OMPBuilder.ParallelTask;
        Args.clear();
        Args.push_back(OutlinedFn->getArg(0));
        Args.push_back(OutlinedFn->getArg(1));
        for (unsigned U = CallbackFirstArgOperand, E = CI->arg_size(); U < E;
             ++U)
          Args.push_back(CI->getArgOperand(U));

        CallInst *NewCI =
            CallInst::Create(FT, Callee, Args, "", CI->getIterator());
        if (CI->getDebugLoc())
          NewCI->setDebugLoc(CI->getDebugLoc());

        // Forward parameter attributes from the callback to the callee.
        for (unsigned U = CallbackFirstArgOperand, E = CI->arg_size(); U < E;
             ++U)
          for (const Attribute &A : CI->getAttributes().getParamAttrs(U))
            NewCI->addParamAttr(
                U - (CallbackFirstArgOperand - CallbackCalleeOperand), A);

        // Emit an explicit barrier to replace the implicit fork-join barrier.
        if (CI != MergableCIs.back()) {
          // TODO: Remove barrier if the merged parallel region includes the
          // 'nowait' clause.
          OMPInfoCache.OMPBuilder.createBarrier(
              InsertPointTy(NewCI->getParent(),
                            NewCI->getNextNode()->getIterator()),
              OMPD_parallel);
        }

        CI->eraseFromParent();
      }

      assert(OutlinedFn != OriginalFn && "Outlining failed");
      CGUpdater.registerOutlinedFunction(*OriginalFn, *OutlinedFn);
      CGUpdater.reanalyzeFunction(*OriginalFn);

      NumOpenMPParallelRegionsMerged += MergableCIs.size();

      return true;
    };

    // Helper function that identifes sequences of
    // __kmpc_fork_call uses in a basic block.
    auto DetectPRsCB = [&](Use &U, Function &F) {
      CallInst *CI = getCallIfRegularCall(U, &RFI);
      BB2PRMap[CI->getParent()].insert(CI);

      return false;
    };

    BB2PRMap.clear();
    RFI.foreachUse(SCC, DetectPRsCB);
    SmallVector<SmallVector<CallInst *, 4>, 4> MergableCIsVector;
    // Find mergable parallel regions within a basic block that are
    // safe to merge, that is any in-between instructions can safely
    // execute in parallel after merging.
    // TODO: support merging across basic-blocks.
    for (auto &It : BB2PRMap) {
      auto &CIs = It.getSecond();
      if (CIs.size() < 2)
        continue;

      BasicBlock *BB = It.getFirst();
      SmallVector<CallInst *, 4> MergableCIs;

      /// Returns true if the instruction is mergable, false otherwise.
      /// A terminator instruction is unmergable by definition since merging
      /// works within a BB. Instructions before the mergable region are
      /// mergable if they are not calls to OpenMP runtime functions that may
      /// set different execution parameters for subsequent parallel regions.
      /// Instructions in-between parallel regions are mergable if they are not
      /// calls to any non-intrinsic function since that may call a non-mergable
      /// OpenMP runtime function.
      auto IsMergable = [&](Instruction &I, bool IsBeforeMergableRegion) {
        // We do not merge across BBs, hence return false (unmergable) if the
        // instruction is a terminator.
        if (I.isTerminator())
          return false;

        if (!isa<CallInst>(&I))
          return true;

        CallInst *CI = cast<CallInst>(&I);
        if (IsBeforeMergableRegion) {
          Function *CalledFunction = CI->getCalledFunction();
          if (!CalledFunction)
            return false;
          // Return false (unmergable) if the call before the parallel
          // region calls an explicit affinity (proc_bind) or number of
          // threads (num_threads) compiler-generated function. Those settings
          // may be incompatible with following parallel regions.
          // TODO: ICV tracking to detect compatibility.
          for (const auto &RFI : UnmergableCallsInfo) {
            if (CalledFunction == RFI.Declaration)
              return false;
          }
        } else {
          // Return false (unmergable) if there is a call instruction
          // in-between parallel regions when it is not an intrinsic. It
          // may call an unmergable OpenMP runtime function in its callpath.
          // TODO: Keep track of possible OpenMP calls in the callpath.
          if (!isa<IntrinsicInst>(CI))
            return false;
        }

        return true;
      };
      // Find maximal number of parallel region CIs that are safe to merge.
      for (auto It = BB->begin(), End = BB->end(); It != End;) {
        Instruction &I = *It;
        ++It;

        if (CIs.count(&I)) {
          MergableCIs.push_back(cast<CallInst>(&I));
          continue;
        }

        // Continue expanding if the instruction is mergable.
        if (IsMergable(I, MergableCIs.empty()))
          continue;

        // Forward the instruction iterator to skip the next parallel region
        // since there is an unmergable instruction which can affect it.
        for (; It != End; ++It) {
          Instruction &SkipI = *It;
          if (CIs.count(&SkipI)) {
            LLVM_DEBUG(dbgs() << TAG << "Skip parallel region " << SkipI
                              << " due to " << I << "\n");
            ++It;
            break;
          }
        }

        // Store mergable regions found.
        if (MergableCIs.size() > 1) {
          MergableCIsVector.push_back(MergableCIs);
          LLVM_DEBUG(dbgs() << TAG << "Found " << MergableCIs.size()
                            << " parallel regions in block " << BB->getName()
                            << " of function " << BB->getParent()->getName()
                            << "\n";);
        }

        MergableCIs.clear();
      }

      if (!MergableCIsVector.empty()) {
        Changed = true;

        for (auto &MergableCIs : MergableCIsVector)
          Merge(MergableCIs, BB);
        MergableCIsVector.clear();
      }
    }

    if (Changed) {
      /// Re-collect use for fork calls, emitted barrier calls, and
      /// any emitted master/end_master calls.
      OMPInfoCache.recollectUsesForFunction(OMPRTL___kmpc_fork_call);
      OMPInfoCache.recollectUsesForFunction(OMPRTL___kmpc_barrier);
      OMPInfoCache.recollectUsesForFunction(OMPRTL___kmpc_master);
      OMPInfoCache.recollectUsesForFunction(OMPRTL___kmpc_end_master);
    }

    return Changed;
  }

  /// Try to delete parallel regions if possible.
  bool deleteParallelRegions() {
    const unsigned CallbackCalleeOperand = 2;

    OMPInformationCache::RuntimeFunctionInfo &RFI =
        OMPInfoCache.RFIs[OMPRTL___kmpc_fork_call];

    if (!RFI.Declaration)
      return false;

    bool Changed = false;
    auto DeleteCallCB = [&](Use &U, Function &) {
      CallInst *CI = getCallIfRegularCall(U);
      if (!CI)
        return false;
      auto *Fn = dyn_cast<Function>(
          CI->getArgOperand(CallbackCalleeOperand)->stripPointerCasts());
      if (!Fn)
        return false;
      if (!Fn->onlyReadsMemory())
        return false;
      if (!Fn->hasFnAttribute(Attribute::WillReturn))
        return false;

      LLVM_DEBUG(dbgs() << TAG << "Delete read-only parallel region in "
                        << CI->getCaller()->getName() << "\n");

      auto Remark = [&](OptimizationRemark OR) {
        return OR << "Removing parallel region with no side-effects.";
      };
      emitRemark<OptimizationRemark>(CI, "OMP160", Remark);

      CI->eraseFromParent();
      Changed = true;
      ++NumOpenMPParallelRegionsDeleted;
      return true;
    };

    RFI.foreachUse(SCC, DeleteCallCB);

    return Changed;
  }

  /// Try to eliminate runtime calls by reusing existing ones.
  bool deduplicateRuntimeCalls() {
    bool Changed = false;

    RuntimeFunction DeduplicableRuntimeCallIDs[] = {
        OMPRTL_omp_get_num_threads,
        OMPRTL_omp_in_parallel,
        OMPRTL_omp_get_cancellation,
        OMPRTL_omp_get_supported_active_levels,
        OMPRTL_omp_get_level,
        OMPRTL_omp_get_ancestor_thread_num,
        OMPRTL_omp_get_team_size,
        OMPRTL_omp_get_active_level,
        OMPRTL_omp_in_final,
        OMPRTL_omp_get_proc_bind,
        OMPRTL_omp_get_num_places,
        OMPRTL_omp_get_num_procs,
        OMPRTL_omp_get_place_num,
        OMPRTL_omp_get_partition_num_places,
        OMPRTL_omp_get_partition_place_nums};

    // Global-tid is handled separately.
    SmallSetVector<Value *, 16> GTIdArgs;
    collectGlobalThreadIdArguments(GTIdArgs);
    LLVM_DEBUG(dbgs() << TAG << "Found " << GTIdArgs.size()
                      << " global thread ID arguments\n");

    for (Function *F : SCC) {
      for (auto DeduplicableRuntimeCallID : DeduplicableRuntimeCallIDs)
        Changed |= deduplicateRuntimeCalls(
            *F, OMPInfoCache.RFIs[DeduplicableRuntimeCallID]);

      // __kmpc_global_thread_num is special as we can replace it with an
      // argument in enough cases to make it worth trying.
      Value *GTIdArg = nullptr;
      for (Argument &Arg : F->args())
        if (GTIdArgs.count(&Arg)) {
          GTIdArg = &Arg;
          break;
        }
      Changed |= deduplicateRuntimeCalls(
          *F, OMPInfoCache.RFIs[OMPRTL___kmpc_global_thread_num], GTIdArg);
    }

    return Changed;
  }

  /// Tries to remove known runtime symbols that are optional from the module.
  bool removeRuntimeSymbols() {
    // The RPC client symbol is defined in `libc` and indicates that something
    // required an RPC server. If its users were all optimized out then we can
    // safely remove it.
    // TODO: This should be somewhere more common in the future.
    if (GlobalVariable *GV = M.getNamedGlobal("__llvm_libc_rpc_client")) {
      if (!GV->getType()->isPointerTy())
        return false;

      Constant *C = GV->getInitializer();
      if (!C)
        return false;

      // Check to see if the only user of the RPC client is the external handle.
      GlobalVariable *Client = dyn_cast<GlobalVariable>(C->stripPointerCasts());
      if (!Client || Client->getNumUses() > 1 ||
          Client->user_back() != GV->getInitializer())
        return false;

      Client->replaceAllUsesWith(PoisonValue::get(Client->getType()));
      Client->eraseFromParent();

      GV->replaceAllUsesWith(PoisonValue::get(GV->getType()));
      GV->eraseFromParent();

      return true;
    }
    return false;
  }

  /// Tries to hide the latency of runtime calls that involve host to
  /// device memory transfers by splitting them into their "issue" and "wait"
  /// versions. The "issue" is moved upwards as much as possible. The "wait" is
  /// moved downards as much as possible. The "issue" issues the memory transfer
  /// asynchronously, returning a handle. The "wait" waits in the returned
  /// handle for the memory transfer to finish.
  bool hideMemTransfersLatency() {
    auto &RFI = OMPInfoCache.RFIs[OMPRTL___tgt_target_data_begin_mapper];
    bool Changed = false;
    auto SplitMemTransfers = [&](Use &U, Function &Decl) {
      auto *RTCall = getCallIfRegularCall(U, &RFI);
      if (!RTCall)
        return false;

      OffloadArray OffloadArrays[3];
      if (!getValuesInOffloadArrays(*RTCall, OffloadArrays))
        return false;

      LLVM_DEBUG(dumpValuesInOffloadArrays(OffloadArrays));

      // TODO: Check if can be moved upwards.
      bool WasSplit = false;
      Instruction *WaitMovementPoint = canBeMovedDownwards(*RTCall);
      if (WaitMovementPoint)
        WasSplit = splitTargetDataBeginRTC(*RTCall, *WaitMovementPoint);

      Changed |= WasSplit;
      return WasSplit;
    };
    if (OMPInfoCache.runtimeFnsAvailable(
            {OMPRTL___tgt_target_data_begin_mapper_issue,
             OMPRTL___tgt_target_data_begin_mapper_wait}))
      RFI.foreachUse(SCC, SplitMemTransfers);

    return Changed;
  }

  void analysisGlobalization() {
    auto &RFI = OMPInfoCache.RFIs[OMPRTL___kmpc_alloc_shared];

    auto CheckGlobalization = [&](Use &U, Function &Decl) {
      if (CallInst *CI = getCallIfRegularCall(U, &RFI)) {
        auto Remark = [&](OptimizationRemarkMissed ORM) {
          return ORM
                 << "Found thread data sharing on the GPU. "
                 << "Expect degraded performance due to data globalization.";
        };
        emitRemark<OptimizationRemarkMissed>(CI, "OMP112", Remark);
      }

      return false;
    };

    RFI.foreachUse(SCC, CheckGlobalization);
  }

  /// Maps the values stored in the offload arrays passed as arguments to
  /// \p RuntimeCall into the offload arrays in \p OAs.
  bool getValuesInOffloadArrays(CallInst &RuntimeCall,
                                MutableArrayRef<OffloadArray> OAs) {
    assert(OAs.size() == 3 && "Need space for three offload arrays!");

    // A runtime call that involves memory offloading looks something like:
    // call void @__tgt_target_data_begin_mapper(arg0, arg1,
    //   i8** %offload_baseptrs, i8** %offload_ptrs, i64* %offload_sizes,
    // ...)
    // So, the idea is to access the allocas that allocate space for these
    // offload arrays, offload_baseptrs, offload_ptrs, offload_sizes.
    // Therefore:
    // i8** %offload_baseptrs.
    Value *BasePtrsArg =
        RuntimeCall.getArgOperand(OffloadArray::BasePtrsArgNum);
    // i8** %offload_ptrs.
    Value *PtrsArg = RuntimeCall.getArgOperand(OffloadArray::PtrsArgNum);
    // i8** %offload_sizes.
    Value *SizesArg = RuntimeCall.getArgOperand(OffloadArray::SizesArgNum);

    // Get values stored in **offload_baseptrs.
    auto *V = getUnderlyingObject(BasePtrsArg);
    if (!isa<AllocaInst>(V))
      return false;
    auto *BasePtrsArray = cast<AllocaInst>(V);
    if (!OAs[0].initialize(*BasePtrsArray, RuntimeCall))
      return false;

    // Get values stored in **offload_baseptrs.
    V = getUnderlyingObject(PtrsArg);
    if (!isa<AllocaInst>(V))
      return false;
    auto *PtrsArray = cast<AllocaInst>(V);
    if (!OAs[1].initialize(*PtrsArray, RuntimeCall))
      return false;

    // Get values stored in **offload_sizes.
    V = getUnderlyingObject(SizesArg);
    // If it's a [constant] global array don't analyze it.
    if (isa<GlobalValue>(V))
      return isa<Constant>(V);
    if (!isa<AllocaInst>(V))
      return false;

    auto *SizesArray = cast<AllocaInst>(V);
    if (!OAs[2].initialize(*SizesArray, RuntimeCall))
      return false;

    return true;
  }

  /// Prints the values in the OffloadArrays \p OAs using LLVM_DEBUG.
  /// For now this is a way to test that the function getValuesInOffloadArrays
  /// is working properly.
  /// TODO: Move this to a unittest when unittests are available for OpenMPOpt.
  void dumpValuesInOffloadArrays(ArrayRef<OffloadArray> OAs) {
    assert(OAs.size() == 3 && "There are three offload arrays to debug!");

    LLVM_DEBUG(dbgs() << TAG << " Successfully got offload values:\n");
    std::string ValuesStr;
    raw_string_ostream Printer(ValuesStr);
    std::string Separator = " --- ";

    for (auto *BP : OAs[0].StoredValues) {
      BP->print(Printer);
      Printer << Separator;
    }
    LLVM_DEBUG(dbgs() << "\t\toffload_baseptrs: " << ValuesStr << "\n");
    ValuesStr.clear();

    for (auto *P : OAs[1].StoredValues) {
      P->print(Printer);
      Printer << Separator;
    }
    LLVM_DEBUG(dbgs() << "\t\toffload_ptrs: " << ValuesStr << "\n");
    ValuesStr.clear();

    for (auto *S : OAs[2].StoredValues) {
      S->print(Printer);
      Printer << Separator;
    }
    LLVM_DEBUG(dbgs() << "\t\toffload_sizes: " << ValuesStr << "\n");
  }

  /// Returns the instruction where the "wait" counterpart \p RuntimeCall can be
  /// moved. Returns nullptr if the movement is not possible, or not worth it.
  Instruction *canBeMovedDownwards(CallInst &RuntimeCall) {
    // FIXME: This traverses only the BasicBlock where RuntimeCall is.
    //  Make it traverse the CFG.

    Instruction *CurrentI = &RuntimeCall;
    bool IsWorthIt = false;
    while ((CurrentI = CurrentI->getNextNode())) {

      // TODO: Once we detect the regions to be offloaded we should use the
      //  alias analysis manager to check if CurrentI may modify one of
      //  the offloaded regions.
      if (CurrentI->mayHaveSideEffects() || CurrentI->mayReadFromMemory()) {
        if (IsWorthIt)
          return CurrentI;

        return nullptr;
      }

      // FIXME: For now if we move it over anything without side effect
      //  is worth it.
      IsWorthIt = true;
    }

    // Return end of BasicBlock.
    return RuntimeCall.getParent()->getTerminator();
  }

  /// Splits \p RuntimeCall into its "issue" and "wait" counterparts.
  bool splitTargetDataBeginRTC(CallInst &RuntimeCall,
                               Instruction &WaitMovementPoint) {
    // Create stack allocated handle (__tgt_async_info) at the beginning of the
    // function. Used for storing information of the async transfer, allowing to
    // wait on it later.
    auto &IRBuilder = OMPInfoCache.OMPBuilder;
    Function *F = RuntimeCall.getCaller();
    BasicBlock &Entry = F->getEntryBlock();
    IRBuilder.Builder.SetInsertPoint(&Entry,
                                     Entry.getFirstNonPHIOrDbgOrAlloca());
    Value *Handle = IRBuilder.Builder.CreateAlloca(
        IRBuilder.AsyncInfo, /*ArraySize=*/nullptr, "handle");
    Handle =
        IRBuilder.Builder.CreateAddrSpaceCast(Handle, IRBuilder.AsyncInfoPtr);

    // Add "issue" runtime call declaration:
    // declare %struct.tgt_async_info @__tgt_target_data_begin_issue(i64, i32,
    //   i8**, i8**, i64*, i64*)
    FunctionCallee IssueDecl = IRBuilder.getOrCreateRuntimeFunction(
        M, OMPRTL___tgt_target_data_begin_mapper_issue);

    // Change RuntimeCall call site for its asynchronous version.
    SmallVector<Value *, 16> Args;
    for (auto &Arg : RuntimeCall.args())
      Args.push_back(Arg.get());
    Args.push_back(Handle);

    CallInst *IssueCallsite = CallInst::Create(IssueDecl, Args, /*NameStr=*/"",
                                               RuntimeCall.getIterator());
    OMPInfoCache.setCallingConvention(IssueDecl, IssueCallsite);
    RuntimeCall.eraseFromParent();

    // Add "wait" runtime call declaration:
    // declare void @__tgt_target_data_begin_wait(i64, %struct.__tgt_async_info)
    FunctionCallee WaitDecl = IRBuilder.getOrCreateRuntimeFunction(
        M, OMPRTL___tgt_target_data_begin_mapper_wait);

    Value *WaitParams[2] = {
        IssueCallsite->getArgOperand(
            OffloadArray::DeviceIDArgNum), // device_id.
        Handle                             // handle to wait on.
    };
    CallInst *WaitCallsite = CallInst::Create(
        WaitDecl, WaitParams, /*NameStr=*/"", WaitMovementPoint.getIterator());
    OMPInfoCache.setCallingConvention(WaitDecl, WaitCallsite);

    return true;
  }

  static Value *combinedIdentStruct(Value *CurrentIdent, Value *NextIdent,
                                    bool GlobalOnly, bool &SingleChoice) {
    if (CurrentIdent == NextIdent)
      return CurrentIdent;

    // TODO: Figure out how to actually combine multiple debug locations. For
    //       now we just keep an existing one if there is a single choice.
    if (!GlobalOnly || isa<GlobalValue>(NextIdent)) {
      SingleChoice = !CurrentIdent;
      return NextIdent;
    }
    return nullptr;
  }

  /// Return an `struct ident_t*` value that represents the ones used in the
  /// calls of \p RFI inside of \p F. If \p GlobalOnly is true, we will not
  /// return a local `struct ident_t*`. For now, if we cannot find a suitable
  /// return value we create one from scratch. We also do not yet combine
  /// information, e.g., the source locations, see combinedIdentStruct.
  Value *
  getCombinedIdentFromCallUsesIn(OMPInformationCache::RuntimeFunctionInfo &RFI,
                                 Function &F, bool GlobalOnly) {
    bool SingleChoice = true;
    Value *Ident = nullptr;
    auto CombineIdentStruct = [&](Use &U, Function &Caller) {
      CallInst *CI = getCallIfRegularCall(U, &RFI);
      if (!CI || &F != &Caller)
        return false;
      Ident = combinedIdentStruct(Ident, CI->getArgOperand(0),
                                  /* GlobalOnly */ true, SingleChoice);
      return false;
    };
    RFI.foreachUse(SCC, CombineIdentStruct);

    if (!Ident || !SingleChoice) {
      // The IRBuilder uses the insertion block to get to the module, this is
      // unfortunate but we work around it for now.
      if (!OMPInfoCache.OMPBuilder.getInsertionPoint().getBlock())
        OMPInfoCache.OMPBuilder.updateToLocation(OpenMPIRBuilder::InsertPointTy(
            &F.getEntryBlock(), F.getEntryBlock().begin()));
      // Create a fallback location if non was found.
      // TODO: Use the debug locations of the calls instead.
      uint32_t SrcLocStrSize;
      Constant *Loc =
          OMPInfoCache.OMPBuilder.getOrCreateDefaultSrcLocStr(SrcLocStrSize);
      Ident = OMPInfoCache.OMPBuilder.getOrCreateIdent(Loc, SrcLocStrSize);
    }
    return Ident;
  }

  /// Try to eliminate calls of \p RFI in \p F by reusing an existing one or
  /// \p ReplVal if given.
  bool deduplicateRuntimeCalls(Function &F,
                               OMPInformationCache::RuntimeFunctionInfo &RFI,
                               Value *ReplVal = nullptr) {
    auto *UV = RFI.getUseVector(F);
    if (!UV || UV->size() + (ReplVal != nullptr) < 2)
      return false;

    LLVM_DEBUG(
        dbgs() << TAG << "Deduplicate " << UV->size() << " uses of " << RFI.Name
               << (ReplVal ? " with an existing value\n" : "\n") << "\n");

    assert((!ReplVal || (isa<Argument>(ReplVal) &&
                         cast<Argument>(ReplVal)->getParent() == &F)) &&
           "Unexpected replacement value!");

    // TODO: Use dominance to find a good position instead.
    auto CanBeMoved = [this](CallBase &CB) {
      unsigned NumArgs = CB.arg_size();
      if (NumArgs == 0)
        return true;
      if (CB.getArgOperand(0)->getType() != OMPInfoCache.OMPBuilder.IdentPtr)
        return false;
      for (unsigned U = 1; U < NumArgs; ++U)
        if (isa<Instruction>(CB.getArgOperand(U)))
          return false;
      return true;
    };

    if (!ReplVal) {
      auto *DT =
          OMPInfoCache.getAnalysisResultForFunction<DominatorTreeAnalysis>(F);
      if (!DT)
        return false;
      Instruction *IP = nullptr;
      for (Use *U : *UV) {
        if (CallInst *CI = getCallIfRegularCall(*U, &RFI)) {
          if (IP)
            IP = DT->findNearestCommonDominator(IP, CI);
          else
            IP = CI;
          if (!CanBeMoved(*CI))
            continue;
          if (!ReplVal)
            ReplVal = CI;
        }
      }
      if (!ReplVal)
        return false;
      assert(IP && "Expected insertion point!");
      cast<Instruction>(ReplVal)->moveBefore(IP);
    }

    // If we use a call as a replacement value we need to make sure the ident is
    // valid at the new location. For now we just pick a global one, either
    // existing and used by one of the calls, or created from scratch.
    if (CallBase *CI = dyn_cast<CallBase>(ReplVal)) {
      if (!CI->arg_empty() &&
          CI->getArgOperand(0)->getType() == OMPInfoCache.OMPBuilder.IdentPtr) {
        Value *Ident = getCombinedIdentFromCallUsesIn(RFI, F,
                                                      /* GlobalOnly */ true);
        CI->setArgOperand(0, Ident);
      }
    }

    bool Changed = false;
    auto ReplaceAndDeleteCB = [&](Use &U, Function &Caller) {
      CallInst *CI = getCallIfRegularCall(U, &RFI);
      if (!CI || CI == ReplVal || &F != &Caller)
        return false;
      assert(CI->getCaller() == &F && "Unexpected call!");

      auto Remark = [&](OptimizationRemark OR) {
        return OR << "OpenMP runtime call "
                  << ore::NV("OpenMPOptRuntime", RFI.Name) << " deduplicated.";
      };
      if (CI->getDebugLoc())
        emitRemark<OptimizationRemark>(CI, "OMP170", Remark);
      else
        emitRemark<OptimizationRemark>(&F, "OMP170", Remark);

      CI->replaceAllUsesWith(ReplVal);
      CI->eraseFromParent();
      ++NumOpenMPRuntimeCallsDeduplicated;
      Changed = true;
      return true;
    };
    RFI.foreachUse(SCC, ReplaceAndDeleteCB);

    return Changed;
  }

  /// Collect arguments that represent the global thread id in \p GTIdArgs.
  void collectGlobalThreadIdArguments(SmallSetVector<Value *, 16> &GTIdArgs) {
    // TODO: Below we basically perform a fixpoint iteration with a pessimistic
    //       initialization. We could define an AbstractAttribute instead and
    //       run the Attributor here once it can be run as an SCC pass.

    // Helper to check the argument \p ArgNo at all call sites of \p F for
    // a GTId.
    auto CallArgOpIsGTId = [&](Function &F, unsigned ArgNo, CallInst &RefCI) {
      if (!F.hasLocalLinkage())
        return false;
      for (Use &U : F.uses()) {
        if (CallInst *CI = getCallIfRegularCall(U)) {
          Value *ArgOp = CI->getArgOperand(ArgNo);
          if (CI == &RefCI || GTIdArgs.count(ArgOp) ||
              getCallIfRegularCall(
                  *ArgOp, &OMPInfoCache.RFIs[OMPRTL___kmpc_global_thread_num]))
            continue;
        }
        return false;
      }
      return true;
    };

    // Helper to identify uses of a GTId as GTId arguments.
    auto AddUserArgs = [&](Value &GTId) {
      for (Use &U : GTId.uses())
        if (CallInst *CI = dyn_cast<CallInst>(U.getUser()))
          if (CI->isArgOperand(&U))
            if (Function *Callee = CI->getCalledFunction())
              if (CallArgOpIsGTId(*Callee, U.getOperandNo(), *CI))
                GTIdArgs.insert(Callee->getArg(U.getOperandNo()));
    };

    // The argument users of __kmpc_global_thread_num calls are GTIds.
    OMPInformationCache::RuntimeFunctionInfo &GlobThreadNumRFI =
        OMPInfoCache.RFIs[OMPRTL___kmpc_global_thread_num];

    GlobThreadNumRFI.foreachUse(SCC, [&](Use &U, Function &F) {
      if (CallInst *CI = getCallIfRegularCall(U, &GlobThreadNumRFI))
        AddUserArgs(*CI);
      return false;
    });

    // Transitively search for more arguments by looking at the users of the
    // ones we know already. During the search the GTIdArgs vector is extended
    // so we cannot cache the size nor can we use a range based for.
    for (unsigned U = 0; U < GTIdArgs.size(); ++U)
      AddUserArgs(*GTIdArgs[U]);
  }

  /// Kernel (=GPU) optimizations and utility functions
  ///
  ///{{

  /// Cache to remember the unique kernel for a function.
  DenseMap<Function *, std::optional<Kernel>> UniqueKernelMap;

  /// Find the unique kernel that will execute \p F, if any.
  Kernel getUniqueKernelFor(Function &F);

  /// Find the unique kernel that will execute \p I, if any.
  Kernel getUniqueKernelFor(Instruction &I) {
    return getUniqueKernelFor(*I.getFunction());
  }

  /// Rewrite the device (=GPU) code state machine create in non-SPMD mode in
  /// the cases we can avoid taking the address of a function.
  bool rewriteDeviceCodeStateMachine();

  ///
  ///}}

  /// Emit a remark generically
  ///
  /// This template function can be used to generically emit a remark. The
  /// RemarkKind should be one of the following:
  ///   - OptimizationRemark to indicate a successful optimization attempt
  ///   - OptimizationRemarkMissed to report a failed optimization attempt
  ///   - OptimizationRemarkAnalysis to provide additional information about an
  ///     optimization attempt
  ///
  /// The remark is built using a callback function provided by the caller that
  /// takes a RemarkKind as input and returns a RemarkKind.
  template <typename RemarkKind, typename RemarkCallBack>
  void emitRemark(Instruction *I, StringRef RemarkName,
                  RemarkCallBack &&RemarkCB) const {
    Function *F = I->getParent()->getParent();
    auto &ORE = OREGetter(F);

    if (RemarkName.starts_with("OMP"))
      ORE.emit([&]() {
        return RemarkCB(RemarkKind(DEBUG_TYPE, RemarkName, I))
               << " [" << RemarkName << "]";
      });
    else
      ORE.emit(
          [&]() { return RemarkCB(RemarkKind(DEBUG_TYPE, RemarkName, I)); });
  }

  /// Emit a remark on a function.
  template <typename RemarkKind, typename RemarkCallBack>
  void emitRemark(Function *F, StringRef RemarkName,
                  RemarkCallBack &&RemarkCB) const {
    auto &ORE = OREGetter(F);

    if (RemarkName.starts_with("OMP"))
      ORE.emit([&]() {
        return RemarkCB(RemarkKind(DEBUG_TYPE, RemarkName, F))
               << " [" << RemarkName << "]";
      });
    else
      ORE.emit(
          [&]() { return RemarkCB(RemarkKind(DEBUG_TYPE, RemarkName, F)); });
  }

  /// The underlying module.
  Module &M;

  /// The SCC we are operating on.
  SmallVectorImpl<Function *> &SCC;

  /// Callback to update the call graph, the first argument is a removed call,
  /// the second an optional replacement call.
  CallGraphUpdater &CGUpdater;

  /// Callback to get an OptimizationRemarkEmitter from a Function *
  OptimizationRemarkGetter OREGetter;

  /// OpenMP-specific information cache. Also Used for Attributor runs.
  OMPInformationCache &OMPInfoCache;

  /// Attributor instance.
  Attributor &A;

  /// Helper function to run Attributor on SCC.
  bool runAttributor(bool IsModulePass) {
    if (SCC.empty())
      return false;

    registerAAs(IsModulePass);

    ChangeStatus Changed = A.run();

    LLVM_DEBUG(dbgs() << "[Attributor] Done with " << SCC.size()
                      << " functions, result: " << Changed << ".\n");

    if (Changed == ChangeStatus::CHANGED)
      OMPInfoCache.invalidateAnalyses();

    return Changed == ChangeStatus::CHANGED;
  }

  void registerFoldRuntimeCall(RuntimeFunction RF);

  /// Populate the Attributor with abstract attribute opportunities in the
  /// functions.
  void registerAAs(bool IsModulePass);

public:
  /// Callback to register AAs for live functions, including internal functions
  /// marked live during the traversal.
  static void registerAAsForFunction(Attributor &A, const Function &F);
};

Kernel OpenMPOpt::getUniqueKernelFor(Function &F) {
  if (OMPInfoCache.CGSCC && !OMPInfoCache.CGSCC->empty() &&
      !OMPInfoCache.CGSCC->contains(&F))
    return nullptr;

  // Use a scope to keep the lifetime of the CachedKernel short.
  {
    std::optional<Kernel> &CachedKernel = UniqueKernelMap[&F];
    if (CachedKernel)
      return *CachedKernel;

    // TODO: We should use an AA to create an (optimistic and callback
    //       call-aware) call graph. For now we stick to simple patterns that
    //       are less powerful, basically the worst fixpoint.
    if (isOpenMPKernel(F)) {
      CachedKernel = Kernel(&F);
      return *CachedKernel;
    }

    CachedKernel = nullptr;
    if (!F.hasLocalLinkage()) {

      // See https://openmp.llvm.org/remarks/OptimizationRemarks.html
      auto Remark = [&](OptimizationRemarkAnalysis ORA) {
        return ORA << "Potentially unknown OpenMP target region caller.";
      };
      emitRemark<OptimizationRemarkAnalysis>(&F, "OMP100", Remark);

      return nullptr;
    }
  }

  auto GetUniqueKernelForUse = [&](const Use &U) -> Kernel {
    if (auto *Cmp = dyn_cast<ICmpInst>(U.getUser())) {
      // Allow use in equality comparisons.
      if (Cmp->isEquality())
        return getUniqueKernelFor(*Cmp);
      return nullptr;
    }
    if (auto *CB = dyn_cast<CallBase>(U.getUser())) {
      // Allow direct calls.
      if (CB->isCallee(&U))
        return getUniqueKernelFor(*CB);

      OMPInformationCache::RuntimeFunctionInfo &KernelParallelRFI =
          OMPInfoCache.RFIs[OMPRTL___kmpc_parallel_51];
      // Allow the use in __kmpc_parallel_51 calls.
      if (OpenMPOpt::getCallIfRegularCall(*U.getUser(), &KernelParallelRFI))
        return getUniqueKernelFor(*CB);
      return nullptr;
    }
    // Disallow every other use.
    return nullptr;
  };

  // TODO: In the future we want to track more than just a unique kernel.
  SmallPtrSet<Kernel, 2> PotentialKernels;
  OMPInformationCache::foreachUse(F, [&](const Use &U) {
    PotentialKernels.insert(GetUniqueKernelForUse(U));
  });

  Kernel K = nullptr;
  if (PotentialKernels.size() == 1)
    K = *PotentialKernels.begin();

  // Cache the result.
  UniqueKernelMap[&F] = K;

  return K;
}

bool OpenMPOpt::rewriteDeviceCodeStateMachine() {
  OMPInformationCache::RuntimeFunctionInfo &KernelParallelRFI =
      OMPInfoCache.RFIs[OMPRTL___kmpc_parallel_51];

  bool Changed = false;
  if (!KernelParallelRFI)
    return Changed;

  // If we have disabled state machine changes, exit
  if (DisableOpenMPOptStateMachineRewrite)
    return Changed;

  for (Function *F : SCC) {

    // Check if the function is a use in a __kmpc_parallel_51 call at
    // all.
    bool UnknownUse = false;
    bool KernelParallelUse = false;
    unsigned NumDirectCalls = 0;

    SmallVector<Use *, 2> ToBeReplacedStateMachineUses;
    OMPInformationCache::foreachUse(*F, [&](Use &U) {
      if (auto *CB = dyn_cast<CallBase>(U.getUser()))
        if (CB->isCallee(&U)) {
          ++NumDirectCalls;
          return;
        }

      if (isa<ICmpInst>(U.getUser())) {
        ToBeReplacedStateMachineUses.push_back(&U);
        return;
      }

      // Find wrapper functions that represent parallel kernels.
      CallInst *CI =
          OpenMPOpt::getCallIfRegularCall(*U.getUser(), &KernelParallelRFI);
      const unsigned int WrapperFunctionArgNo = 6;
      if (!KernelParallelUse && CI &&
          CI->getArgOperandNo(&U) == WrapperFunctionArgNo) {
        KernelParallelUse = true;
        ToBeReplacedStateMachineUses.push_back(&U);
        return;
      }
      UnknownUse = true;
    });

    // Do not emit a remark if we haven't seen a __kmpc_parallel_51
    // use.
    if (!KernelParallelUse)
      continue;

    // If this ever hits, we should investigate.
    // TODO: Checking the number of uses is not a necessary restriction and
    // should be lifted.
    if (UnknownUse || NumDirectCalls != 1 ||
        ToBeReplacedStateMachineUses.size() > 2) {
      auto Remark = [&](OptimizationRemarkAnalysis ORA) {
        return ORA << "Parallel region is used in "
                   << (UnknownUse ? "unknown" : "unexpected")
                   << " ways. Will not attempt to rewrite the state machine.";
      };
      emitRemark<OptimizationRemarkAnalysis>(F, "OMP101", Remark);
      continue;
    }

    // Even if we have __kmpc_parallel_51 calls, we (for now) give
    // up if the function is not called from a unique kernel.
    Kernel K = getUniqueKernelFor(*F);
    if (!K) {
      auto Remark = [&](OptimizationRemarkAnalysis ORA) {
        return ORA << "Parallel region is not called from a unique kernel. "
                      "Will not attempt to rewrite the state machine.";
      };
      emitRemark<OptimizationRemarkAnalysis>(F, "OMP102", Remark);
      continue;
    }

    // We now know F is a parallel body function called only from the kernel K.
    // We also identified the state machine uses in which we replace the
    // function pointer by a new global symbol for identification purposes. This
    // ensures only direct calls to the function are left.

    Module &M = *F->getParent();
    Type *Int8Ty = Type::getInt8Ty(M.getContext());

    auto *ID = new GlobalVariable(
        M, Int8Ty, /* isConstant */ true, GlobalValue::PrivateLinkage,
        UndefValue::get(Int8Ty), F->getName() + ".ID");

    for (Use *U : ToBeReplacedStateMachineUses)
      U->set(ConstantExpr::getPointerBitCastOrAddrSpaceCast(
          ID, U->get()->getType()));

    ++NumOpenMPParallelRegionsReplacedInGPUStateMachine;

    Changed = true;
  }

  return Changed;
}

/// Abstract Attribute for tracking ICV values.
struct AAICVTracker : public StateWrapper<BooleanState, AbstractAttribute> {
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  AAICVTracker(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Returns true if value is assumed to be tracked.
  bool isAssumedTracked() const { return getAssumed(); }

  /// Returns true if value is known to be tracked.
  bool isKnownTracked() const { return getAssumed(); }

  /// Create an abstract attribute biew for the position \p IRP.
  static AAICVTracker &createForPosition(const IRPosition &IRP, Attributor &A);

  /// Return the value with which \p I can be replaced for specific \p ICV.
  virtual std::optional<Value *> getReplacementValue(InternalControlVar ICV,
                                                     const Instruction *I,
                                                     Attributor &A) const {
    return std::nullopt;
  }

  /// Return an assumed unique ICV value if a single candidate is found. If
  /// there cannot be one, return a nullptr. If it is not clear yet, return
  /// std::nullopt.
  virtual std::optional<Value *>
  getUniqueReplacementValue(InternalControlVar ICV) const = 0;

  // Currently only nthreads is being tracked.
  // this array will only grow with time.
  InternalControlVar TrackableICVs[1] = {ICV_nthreads};

  /// See AbstractAttribute::getName()
  const std::string getName() const override { return "AAICVTracker"; }

  /// See AbstractAttribute::getIdAddr()
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AAICVTracker
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  static const char ID;
};

struct AAICVTrackerFunction : public AAICVTracker {
  AAICVTrackerFunction(const IRPosition &IRP, Attributor &A)
      : AAICVTracker(IRP, A) {}

  // FIXME: come up with better string.
  const std::string getAsStr(Attributor *) const override {
    return "ICVTrackerFunction";
  }

  // FIXME: come up with some stats.
  void trackStatistics() const override {}

  /// We don't manifest anything for this AA.
  ChangeStatus manifest(Attributor &A) override {
    return ChangeStatus::UNCHANGED;
  }

  // Map of ICV to their values at specific program point.
  EnumeratedArray<DenseMap<Instruction *, Value *>, InternalControlVar,
                  InternalControlVar::ICV___last>
      ICVReplacementValuesMap;

  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus HasChanged = ChangeStatus::UNCHANGED;

    Function *F = getAnchorScope();

    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());

    for (InternalControlVar ICV : TrackableICVs) {
      auto &SetterRFI = OMPInfoCache.RFIs[OMPInfoCache.ICVs[ICV].Setter];

      auto &ValuesMap = ICVReplacementValuesMap[ICV];
      auto TrackValues = [&](Use &U, Function &) {
        CallInst *CI = OpenMPOpt::getCallIfRegularCall(U);
        if (!CI)
          return false;

        // FIXME: handle setters with more that 1 arguments.
        /// Track new value.
        if (ValuesMap.insert(std::make_pair(CI, CI->getArgOperand(0))).second)
          HasChanged = ChangeStatus::CHANGED;

        return false;
      };

      auto CallCheck = [&](Instruction &I) {
        std::optional<Value *> ReplVal = getValueForCall(A, I, ICV);
        if (ReplVal && ValuesMap.insert(std::make_pair(&I, *ReplVal)).second)
          HasChanged = ChangeStatus::CHANGED;

        return true;
      };

      // Track all changes of an ICV.
      SetterRFI.foreachUse(TrackValues, F);

      bool UsedAssumedInformation = false;
      A.checkForAllInstructions(CallCheck, *this, {Instruction::Call},
                                UsedAssumedInformation,
                                /* CheckBBLivenessOnly */ true);

      /// TODO: Figure out a way to avoid adding entry in
      /// ICVReplacementValuesMap
      Instruction *Entry = &F->getEntryBlock().front();
      if (HasChanged == ChangeStatus::CHANGED && !ValuesMap.count(Entry))
        ValuesMap.insert(std::make_pair(Entry, nullptr));
    }

    return HasChanged;
  }

  /// Helper to check if \p I is a call and get the value for it if it is
  /// unique.
  std::optional<Value *> getValueForCall(Attributor &A, const Instruction &I,
                                         InternalControlVar &ICV) const {

    const auto *CB = dyn_cast<CallBase>(&I);
    if (!CB || CB->hasFnAttr("no_openmp") ||
        CB->hasFnAttr("no_openmp_routines"))
      return std::nullopt;

    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    auto &GetterRFI = OMPInfoCache.RFIs[OMPInfoCache.ICVs[ICV].Getter];
    auto &SetterRFI = OMPInfoCache.RFIs[OMPInfoCache.ICVs[ICV].Setter];
    Function *CalledFunction = CB->getCalledFunction();

    // Indirect call, assume ICV changes.
    if (CalledFunction == nullptr)
      return nullptr;
    if (CalledFunction == GetterRFI.Declaration)
      return std::nullopt;
    if (CalledFunction == SetterRFI.Declaration) {
      if (ICVReplacementValuesMap[ICV].count(&I))
        return ICVReplacementValuesMap[ICV].lookup(&I);

      return nullptr;
    }

    // Since we don't know, assume it changes the ICV.
    if (CalledFunction->isDeclaration())
      return nullptr;

    const auto *ICVTrackingAA = A.getAAFor<AAICVTracker>(
        *this, IRPosition::callsite_returned(*CB), DepClassTy::REQUIRED);

    if (ICVTrackingAA->isAssumedTracked()) {
      std::optional<Value *> URV =
          ICVTrackingAA->getUniqueReplacementValue(ICV);
      if (!URV || (*URV && AA::isValidAtPosition(AA::ValueAndContext(**URV, I),
                                                 OMPInfoCache)))
        return URV;
    }

    // If we don't know, assume it changes.
    return nullptr;
  }

  // We don't check unique value for a function, so return std::nullopt.
  std::optional<Value *>
  getUniqueReplacementValue(InternalControlVar ICV) const override {
    return std::nullopt;
  }

  /// Return the value with which \p I can be replaced for specific \p ICV.
  std::optional<Value *> getReplacementValue(InternalControlVar ICV,
                                             const Instruction *I,
                                             Attributor &A) const override {
    const auto &ValuesMap = ICVReplacementValuesMap[ICV];
    if (ValuesMap.count(I))
      return ValuesMap.lookup(I);

    SmallVector<const Instruction *, 16> Worklist;
    SmallPtrSet<const Instruction *, 16> Visited;
    Worklist.push_back(I);

    std::optional<Value *> ReplVal;

    while (!Worklist.empty()) {
      const Instruction *CurrInst = Worklist.pop_back_val();
      if (!Visited.insert(CurrInst).second)
        continue;

      const BasicBlock *CurrBB = CurrInst->getParent();

      // Go up and look for all potential setters/calls that might change the
      // ICV.
      while ((CurrInst = CurrInst->getPrevNode())) {
        if (ValuesMap.count(CurrInst)) {
          std::optional<Value *> NewReplVal = ValuesMap.lookup(CurrInst);
          // Unknown value, track new.
          if (!ReplVal) {
            ReplVal = NewReplVal;
            break;
          }

          // If we found a new value, we can't know the icv value anymore.
          if (NewReplVal)
            if (ReplVal != NewReplVal)
              return nullptr;

          break;
        }

        std::optional<Value *> NewReplVal = getValueForCall(A, *CurrInst, ICV);
        if (!NewReplVal)
          continue;

        // Unknown value, track new.
        if (!ReplVal) {
          ReplVal = NewReplVal;
          break;
        }

        // if (NewReplVal.hasValue())
        // We found a new value, we can't know the icv value anymore.
        if (ReplVal != NewReplVal)
          return nullptr;
      }

      // If we are in the same BB and we have a value, we are done.
      if (CurrBB == I->getParent() && ReplVal)
        return ReplVal;

      // Go through all predecessors and add terminators for analysis.
      for (const BasicBlock *Pred : predecessors(CurrBB))
        if (const Instruction *Terminator = Pred->getTerminator())
          Worklist.push_back(Terminator);
    }

    return ReplVal;
  }
};

struct AAICVTrackerFunctionReturned : AAICVTracker {
  AAICVTrackerFunctionReturned(const IRPosition &IRP, Attributor &A)
      : AAICVTracker(IRP, A) {}

  // FIXME: come up with better string.
  const std::string getAsStr(Attributor *) const override {
    return "ICVTrackerFunctionReturned";
  }

  // FIXME: come up with some stats.
  void trackStatistics() const override {}

  /// We don't manifest anything for this AA.
  ChangeStatus manifest(Attributor &A) override {
    return ChangeStatus::UNCHANGED;
  }

  // Map of ICV to their values at specific program point.
  EnumeratedArray<std::optional<Value *>, InternalControlVar,
                  InternalControlVar::ICV___last>
      ICVReplacementValuesMap;

  /// Return the value with which \p I can be replaced for specific \p ICV.
  std::optional<Value *>
  getUniqueReplacementValue(InternalControlVar ICV) const override {
    return ICVReplacementValuesMap[ICV];
  }

  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    const auto *ICVTrackingAA = A.getAAFor<AAICVTracker>(
        *this, IRPosition::function(*getAnchorScope()), DepClassTy::REQUIRED);

    if (!ICVTrackingAA->isAssumedTracked())
      return indicatePessimisticFixpoint();

    for (InternalControlVar ICV : TrackableICVs) {
      std::optional<Value *> &ReplVal = ICVReplacementValuesMap[ICV];
      std::optional<Value *> UniqueICVValue;

      auto CheckReturnInst = [&](Instruction &I) {
        std::optional<Value *> NewReplVal =
            ICVTrackingAA->getReplacementValue(ICV, &I, A);

        // If we found a second ICV value there is no unique returned value.
        if (UniqueICVValue && UniqueICVValue != NewReplVal)
          return false;

        UniqueICVValue = NewReplVal;

        return true;
      };

      bool UsedAssumedInformation = false;
      if (!A.checkForAllInstructions(CheckReturnInst, *this, {Instruction::Ret},
                                     UsedAssumedInformation,
                                     /* CheckBBLivenessOnly */ true))
        UniqueICVValue = nullptr;

      if (UniqueICVValue == ReplVal)
        continue;

      ReplVal = UniqueICVValue;
      Changed = ChangeStatus::CHANGED;
    }

    return Changed;
  }
};

struct AAICVTrackerCallSite : AAICVTracker {
  AAICVTrackerCallSite(const IRPosition &IRP, Attributor &A)
      : AAICVTracker(IRP, A) {}

  void initialize(Attributor &A) override {
    assert(getAnchorScope() && "Expected anchor function");

    // We only initialize this AA for getters, so we need to know which ICV it
    // gets.
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    for (InternalControlVar ICV : TrackableICVs) {
      auto ICVInfo = OMPInfoCache.ICVs[ICV];
      auto &Getter = OMPInfoCache.RFIs[ICVInfo.Getter];
      if (Getter.Declaration == getAssociatedFunction()) {
        AssociatedICV = ICVInfo.Kind;
        return;
      }
    }

    /// Unknown ICV.
    indicatePessimisticFixpoint();
  }

  ChangeStatus manifest(Attributor &A) override {
    if (!ReplVal || !*ReplVal)
      return ChangeStatus::UNCHANGED;

    A.changeAfterManifest(IRPosition::inst(*getCtxI()), **ReplVal);
    A.deleteAfterManifest(*getCtxI());

    return ChangeStatus::CHANGED;
  }

  // FIXME: come up with better string.
  const std::string getAsStr(Attributor *) const override {
    return "ICVTrackerCallSite";
  }

  // FIXME: come up with some stats.
  void trackStatistics() const override {}

  InternalControlVar AssociatedICV;
  std::optional<Value *> ReplVal;

  ChangeStatus updateImpl(Attributor &A) override {
    const auto *ICVTrackingAA = A.getAAFor<AAICVTracker>(
        *this, IRPosition::function(*getAnchorScope()), DepClassTy::REQUIRED);

    // We don't have any information, so we assume it changes the ICV.
    if (!ICVTrackingAA->isAssumedTracked())
      return indicatePessimisticFixpoint();

    std::optional<Value *> NewReplVal =
        ICVTrackingAA->getReplacementValue(AssociatedICV, getCtxI(), A);

    if (ReplVal == NewReplVal)
      return ChangeStatus::UNCHANGED;

    ReplVal = NewReplVal;
    return ChangeStatus::CHANGED;
  }

  // Return the value with which associated value can be replaced for specific
  // \p ICV.
  std::optional<Value *>
  getUniqueReplacementValue(InternalControlVar ICV) const override {
    return ReplVal;
  }
};

struct AAICVTrackerCallSiteReturned : AAICVTracker {
  AAICVTrackerCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAICVTracker(IRP, A) {}

  // FIXME: come up with better string.
  const std::string getAsStr(Attributor *) const override {
    return "ICVTrackerCallSiteReturned";
  }

  // FIXME: come up with some stats.
  void trackStatistics() const override {}

  /// We don't manifest anything for this AA.
  ChangeStatus manifest(Attributor &A) override {
    return ChangeStatus::UNCHANGED;
  }

  // Map of ICV to their values at specific program point.
  EnumeratedArray<std::optional<Value *>, InternalControlVar,
                  InternalControlVar::ICV___last>
      ICVReplacementValuesMap;

  /// Return the value with which associated value can be replaced for specific
  /// \p ICV.
  std::optional<Value *>
  getUniqueReplacementValue(InternalControlVar ICV) const override {
    return ICVReplacementValuesMap[ICV];
  }

  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    const auto *ICVTrackingAA = A.getAAFor<AAICVTracker>(
        *this, IRPosition::returned(*getAssociatedFunction()),
        DepClassTy::REQUIRED);

    // We don't have any information, so we assume it changes the ICV.
    if (!ICVTrackingAA->isAssumedTracked())
      return indicatePessimisticFixpoint();

    for (InternalControlVar ICV : TrackableICVs) {
      std::optional<Value *> &ReplVal = ICVReplacementValuesMap[ICV];
      std::optional<Value *> NewReplVal =
          ICVTrackingAA->getUniqueReplacementValue(ICV);

      if (ReplVal == NewReplVal)
        continue;

      ReplVal = NewReplVal;
      Changed = ChangeStatus::CHANGED;
    }
    return Changed;
  }
};

/// Determines if \p BB exits the function unconditionally itself or reaches a
/// block that does through only unique successors.
static bool hasFunctionEndAsUniqueSuccessor(const BasicBlock *BB) {
  if (succ_empty(BB))
    return true;
  const BasicBlock *const Successor = BB->getUniqueSuccessor();
  if (!Successor)
    return false;
  return hasFunctionEndAsUniqueSuccessor(Successor);
}

struct AAExecutionDomainFunction : public AAExecutionDomain {
  AAExecutionDomainFunction(const IRPosition &IRP, Attributor &A)
      : AAExecutionDomain(IRP, A) {}

  ~AAExecutionDomainFunction() { delete RPOT; }

  void initialize(Attributor &A) override {
    Function *F = getAnchorScope();
    assert(F && "Expected anchor function");
    RPOT = new ReversePostOrderTraversal<Function *>(F);
  }

  const std::string getAsStr(Attributor *) const override {
    unsigned TotalBlocks = 0, InitialThreadBlocks = 0, AlignedBlocks = 0;
    for (auto &It : BEDMap) {
      if (!It.getFirst())
        continue;
      TotalBlocks++;
      InitialThreadBlocks += It.getSecond().IsExecutedByInitialThreadOnly;
      AlignedBlocks += It.getSecond().IsReachedFromAlignedBarrierOnly &&
                       It.getSecond().IsReachingAlignedBarrierOnly;
    }
    return "[AAExecutionDomain] " + std::to_string(InitialThreadBlocks) + "/" +
           std::to_string(AlignedBlocks) + " of " +
           std::to_string(TotalBlocks) +
           " executed by initial thread / aligned";
  }

  /// See AbstractAttribute::trackStatistics().
  void trackStatistics() const override {}

  ChangeStatus manifest(Attributor &A) override {
    LLVM_DEBUG({
      for (const BasicBlock &BB : *getAnchorScope()) {
        if (!isExecutedByInitialThreadOnly(BB))
          continue;
        dbgs() << TAG << " Basic block @" << getAnchorScope()->getName() << " "
               << BB.getName() << " is executed by a single thread.\n";
      }
    });

    ChangeStatus Changed = ChangeStatus::UNCHANGED;

    if (DisableOpenMPOptBarrierElimination)
      return Changed;

    SmallPtrSet<CallBase *, 16> DeletedBarriers;
    auto HandleAlignedBarrier = [&](CallBase *CB) {
      const ExecutionDomainTy &ED = CB ? CEDMap[{CB, PRE}] : BEDMap[nullptr];
      if (!ED.IsReachedFromAlignedBarrierOnly ||
          ED.EncounteredNonLocalSideEffect)
        return;
      if (!ED.EncounteredAssumes.empty() && !A.isModulePass())
        return;

      // We can remove this barrier, if it is one, or aligned barriers reaching
      // the kernel end (if CB is nullptr). Aligned barriers reaching the kernel
      // end should only be removed if the kernel end is their unique successor;
      // otherwise, they may have side-effects that aren't accounted for in the
      // kernel end in their other successors. If those barriers have other
      // barriers reaching them, those can be transitively removed as well as
      // long as the kernel end is also their unique successor.
      if (CB) {
        DeletedBarriers.insert(CB);
        A.deleteAfterManifest(*CB);
        ++NumBarriersEliminated;
        Changed = ChangeStatus::CHANGED;
      } else if (!ED.AlignedBarriers.empty()) {
        Changed = ChangeStatus::CHANGED;
        SmallVector<CallBase *> Worklist(ED.AlignedBarriers.begin(),
                                         ED.AlignedBarriers.end());
        SmallSetVector<CallBase *, 16> Visited;
        while (!Worklist.empty()) {
          CallBase *LastCB = Worklist.pop_back_val();
          if (!Visited.insert(LastCB))
            continue;
          if (LastCB->getFunction() != getAnchorScope())
            continue;
          if (!hasFunctionEndAsUniqueSuccessor(LastCB->getParent()))
            continue;
          if (!DeletedBarriers.count(LastCB)) {
            ++NumBarriersEliminated;
            A.deleteAfterManifest(*LastCB);
            continue;
          }
          // The final aligned barrier (LastCB) reaching the kernel end was
          // removed already. This means we can go one step further and remove
          // the barriers encoutered last before (LastCB).
          const ExecutionDomainTy &LastED = CEDMap[{LastCB, PRE}];
          Worklist.append(LastED.AlignedBarriers.begin(),
                          LastED.AlignedBarriers.end());
        }
      }

      // If we actually eliminated a barrier we need to eliminate the associated
      // llvm.assumes as well to avoid creating UB.
      if (!ED.EncounteredAssumes.empty() && (CB || !ED.AlignedBarriers.empty()))
        for (auto *AssumeCB : ED.EncounteredAssumes)
          A.deleteAfterManifest(*AssumeCB);
    };

    for (auto *CB : AlignedBarriers)
      HandleAlignedBarrier(CB);

    // Handle the "kernel end barrier" for kernels too.
    if (omp::isOpenMPKernel(*getAnchorScope()))
      HandleAlignedBarrier(nullptr);

    return Changed;
  }

  bool isNoOpFence(const FenceInst &FI) const override {
    return getState().isValidState() && !NonNoOpFences.count(&FI);
  }

  /// Merge barrier and assumption information from \p PredED into the successor
  /// \p ED.
  void
  mergeInPredecessorBarriersAndAssumptions(Attributor &A, ExecutionDomainTy &ED,
                                           const ExecutionDomainTy &PredED);

  /// Merge all information from \p PredED into the successor \p ED. If
  /// \p InitialEdgeOnly is set, only the initial edge will enter the block
  /// represented by \p ED from this predecessor.
  bool mergeInPredecessor(Attributor &A, ExecutionDomainTy &ED,
                          const ExecutionDomainTy &PredED,
                          bool InitialEdgeOnly = false);

  /// Accumulate information for the entry block in \p EntryBBED.
  bool handleCallees(Attributor &A, ExecutionDomainTy &EntryBBED);

  /// See AbstractAttribute::updateImpl.
  ChangeStatus updateImpl(Attributor &A) override;

  /// Query interface, see AAExecutionDomain
  ///{
  bool isExecutedByInitialThreadOnly(const BasicBlock &BB) const override {
    if (!isValidState())
      return false;
    assert(BB.getParent() == getAnchorScope() && "Block is out of scope!");
    return BEDMap.lookup(&BB).IsExecutedByInitialThreadOnly;
  }

  bool isExecutedInAlignedRegion(Attributor &A,
                                 const Instruction &I) const override {
    assert(I.getFunction() == getAnchorScope() &&
           "Instruction is out of scope!");
    if (!isValidState())
      return false;

    bool ForwardIsOk = true;
    const Instruction *CurI;

    // Check forward until a call or the block end is reached.
    CurI = &I;
    do {
      auto *CB = dyn_cast<CallBase>(CurI);
      if (!CB)
        continue;
      if (CB != &I && AlignedBarriers.contains(const_cast<CallBase *>(CB)))
        return true;
      const auto &It = CEDMap.find({CB, PRE});
      if (It == CEDMap.end())
        continue;
      if (!It->getSecond().IsReachingAlignedBarrierOnly)
        ForwardIsOk = false;
      break;
    } while ((CurI = CurI->getNextNonDebugInstruction()));

    if (!CurI && !BEDMap.lookup(I.getParent()).IsReachingAlignedBarrierOnly)
      ForwardIsOk = false;

    // Check backward until a call or the block beginning is reached.
    CurI = &I;
    do {
      auto *CB = dyn_cast<CallBase>(CurI);
      if (!CB)
        continue;
      if (CB != &I && AlignedBarriers.contains(const_cast<CallBase *>(CB)))
        return true;
      const auto &It = CEDMap.find({CB, POST});
      if (It == CEDMap.end())
        continue;
      if (It->getSecond().IsReachedFromAlignedBarrierOnly)
        break;
      return false;
    } while ((CurI = CurI->getPrevNonDebugInstruction()));

    // Delayed decision on the forward pass to allow aligned barrier detection
    // in the backwards traversal.
    if (!ForwardIsOk)
      return false;

    if (!CurI) {
      const BasicBlock *BB = I.getParent();
      if (BB == &BB->getParent()->getEntryBlock())
        return BEDMap.lookup(nullptr).IsReachedFromAlignedBarrierOnly;
      if (!llvm::all_of(predecessors(BB), [&](const BasicBlock *PredBB) {
            return BEDMap.lookup(PredBB).IsReachedFromAlignedBarrierOnly;
          })) {
        return false;
      }
    }

    // On neither traversal we found a anything but aligned barriers.
    return true;
  }

  ExecutionDomainTy getExecutionDomain(const BasicBlock &BB) const override {
    assert(isValidState() &&
           "No request should be made against an invalid state!");
    return BEDMap.lookup(&BB);
  }
  std::pair<ExecutionDomainTy, ExecutionDomainTy>
  getExecutionDomain(const CallBase &CB) const override {
    assert(isValidState() &&
           "No request should be made against an invalid state!");
    return {CEDMap.lookup({&CB, PRE}), CEDMap.lookup({&CB, POST})};
  }
  ExecutionDomainTy getFunctionExecutionDomain() const override {
    assert(isValidState() &&
           "No request should be made against an invalid state!");
    return InterProceduralED;
  }
  ///}

  // Check if the edge into the successor block contains a condition that only
  // lets the main thread execute it.
  static bool isInitialThreadOnlyEdge(Attributor &A, BranchInst *Edge,
                                      BasicBlock &SuccessorBB) {
    if (!Edge || !Edge->isConditional())
      return false;
    if (Edge->getSuccessor(0) != &SuccessorBB)
      return false;

    auto *Cmp = dyn_cast<CmpInst>(Edge->getCondition());
    if (!Cmp || !Cmp->isTrueWhenEqual() || !Cmp->isEquality())
      return false;

    ConstantInt *C = dyn_cast<ConstantInt>(Cmp->getOperand(1));
    if (!C)
      return false;

    // Match: -1 == __kmpc_target_init (for non-SPMD kernels only!)
    if (C->isAllOnesValue()) {
      auto *CB = dyn_cast<CallBase>(Cmp->getOperand(0));
      auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
      auto &RFI = OMPInfoCache.RFIs[OMPRTL___kmpc_target_init];
      CB = CB ? OpenMPOpt::getCallIfRegularCall(*CB, &RFI) : nullptr;
      if (!CB)
        return false;
      ConstantStruct *KernelEnvC =
          KernelInfo::getKernelEnvironementFromKernelInitCB(CB);
      ConstantInt *ExecModeC =
          KernelInfo::getExecModeFromKernelEnvironment(KernelEnvC);
      return ExecModeC->getSExtValue() & OMP_TGT_EXEC_MODE_GENERIC;
    }

    if (C->isZero()) {
      // Match: 0 == llvm.nvvm.read.ptx.sreg.tid.x()
      if (auto *II = dyn_cast<IntrinsicInst>(Cmp->getOperand(0)))
        if (II->getIntrinsicID() == Intrinsic::nvvm_read_ptx_sreg_tid_x)
          return true;

      // Match: 0 == llvm.amdgcn.workitem.id.x()
      if (auto *II = dyn_cast<IntrinsicInst>(Cmp->getOperand(0)))
        if (II->getIntrinsicID() == Intrinsic::amdgcn_workitem_id_x)
          return true;
    }

    return false;
  };

  /// Mapping containing information about the function for other AAs.
  ExecutionDomainTy InterProceduralED;

  enum Direction { PRE = 0, POST = 1 };
  /// Mapping containing information per block.
  DenseMap<const BasicBlock *, ExecutionDomainTy> BEDMap;
  DenseMap<PointerIntPair<const CallBase *, 1, Direction>, ExecutionDomainTy>
      CEDMap;
  SmallSetVector<CallBase *, 16> AlignedBarriers;

  ReversePostOrderTraversal<Function *> *RPOT = nullptr;

  /// Set \p R to \V and report true if that changed \p R.
  static bool setAndRecord(bool &R, bool V) {
    bool Eq = (R == V);
    R = V;
    return !Eq;
  }

  /// Collection of fences known to be non-no-opt. All fences not in this set
  /// can be assumed no-opt.
  SmallPtrSet<const FenceInst *, 8> NonNoOpFences;
};

void AAExecutionDomainFunction::mergeInPredecessorBarriersAndAssumptions(
    Attributor &A, ExecutionDomainTy &ED, const ExecutionDomainTy &PredED) {
  for (auto *EA : PredED.EncounteredAssumes)
    ED.addAssumeInst(A, *EA);

  for (auto *AB : PredED.AlignedBarriers)
    ED.addAlignedBarrier(A, *AB);
}

bool AAExecutionDomainFunction::mergeInPredecessor(
    Attributor &A, ExecutionDomainTy &ED, const ExecutionDomainTy &PredED,
    bool InitialEdgeOnly) {

  bool Changed = false;
  Changed |=
      setAndRecord(ED.IsExecutedByInitialThreadOnly,
                   InitialEdgeOnly || (PredED.IsExecutedByInitialThreadOnly &&
                                       ED.IsExecutedByInitialThreadOnly));

  Changed |= setAndRecord(ED.IsReachedFromAlignedBarrierOnly,
                          ED.IsReachedFromAlignedBarrierOnly &&
                              PredED.IsReachedFromAlignedBarrierOnly);
  Changed |= setAndRecord(ED.EncounteredNonLocalSideEffect,
                          ED.EncounteredNonLocalSideEffect |
                              PredED.EncounteredNonLocalSideEffect);
  // Do not track assumptions and barriers as part of Changed.
  if (ED.IsReachedFromAlignedBarrierOnly)
    mergeInPredecessorBarriersAndAssumptions(A, ED, PredED);
  else
    ED.clearAssumeInstAndAlignedBarriers();
  return Changed;
}

bool AAExecutionDomainFunction::handleCallees(Attributor &A,
                                              ExecutionDomainTy &EntryBBED) {
  SmallVector<std::pair<ExecutionDomainTy, ExecutionDomainTy>, 4> CallSiteEDs;
  auto PredForCallSite = [&](AbstractCallSite ACS) {
    const auto *EDAA = A.getAAFor<AAExecutionDomain>(
        *this, IRPosition::function(*ACS.getInstruction()->getFunction()),
        DepClassTy::OPTIONAL);
    if (!EDAA || !EDAA->getState().isValidState())
      return false;
    CallSiteEDs.emplace_back(
        EDAA->getExecutionDomain(*cast<CallBase>(ACS.getInstruction())));
    return true;
  };

  ExecutionDomainTy ExitED;
  bool AllCallSitesKnown;
  if (A.checkForAllCallSites(PredForCallSite, *this,
                             /* RequiresAllCallSites */ true,
                             AllCallSitesKnown)) {
    for (const auto &[CSInED, CSOutED] : CallSiteEDs) {
      mergeInPredecessor(A, EntryBBED, CSInED);
      ExitED.IsReachingAlignedBarrierOnly &=
          CSOutED.IsReachingAlignedBarrierOnly;
    }

  } else {
    // We could not find all predecessors, so this is either a kernel or a
    // function with external linkage (or with some other weird uses).
    if (omp::isOpenMPKernel(*getAnchorScope())) {
      EntryBBED.IsExecutedByInitialThreadOnly = false;
      EntryBBED.IsReachedFromAlignedBarrierOnly = true;
      EntryBBED.EncounteredNonLocalSideEffect = false;
      ExitED.IsReachingAlignedBarrierOnly = false;
    } else {
      EntryBBED.IsExecutedByInitialThreadOnly = false;
      EntryBBED.IsReachedFromAlignedBarrierOnly = false;
      EntryBBED.EncounteredNonLocalSideEffect = true;
      ExitED.IsReachingAlignedBarrierOnly = false;
    }
  }

  bool Changed = false;
  auto &FnED = BEDMap[nullptr];
  Changed |= setAndRecord(FnED.IsReachedFromAlignedBarrierOnly,
                          FnED.IsReachedFromAlignedBarrierOnly &
                              EntryBBED.IsReachedFromAlignedBarrierOnly);
  Changed |= setAndRecord(FnED.IsReachingAlignedBarrierOnly,
                          FnED.IsReachingAlignedBarrierOnly &
                              ExitED.IsReachingAlignedBarrierOnly);
  Changed |= setAndRecord(FnED.IsExecutedByInitialThreadOnly,
                          EntryBBED.IsExecutedByInitialThreadOnly);
  return Changed;
}

ChangeStatus AAExecutionDomainFunction::updateImpl(Attributor &A) {

  bool Changed = false;

  // Helper to deal with an aligned barrier encountered during the forward
  // traversal. \p CB is the aligned barrier, \p ED is the execution domain when
  // it was encountered.
  auto HandleAlignedBarrier = [&](CallBase &CB, ExecutionDomainTy &ED) {
    Changed |= AlignedBarriers.insert(&CB);
    // First, update the barrier ED kept in the separate CEDMap.
    auto &CallInED = CEDMap[{&CB, PRE}];
    Changed |= mergeInPredecessor(A, CallInED, ED);
    CallInED.IsReachingAlignedBarrierOnly = true;
    // Next adjust the ED we use for the traversal.
    ED.EncounteredNonLocalSideEffect = false;
    ED.IsReachedFromAlignedBarrierOnly = true;
    // Aligned barrier collection has to come last.
    ED.clearAssumeInstAndAlignedBarriers();
    ED.addAlignedBarrier(A, CB);
    auto &CallOutED = CEDMap[{&CB, POST}];
    Changed |= mergeInPredecessor(A, CallOutED, ED);
  };

  auto *LivenessAA =
      A.getAAFor<AAIsDead>(*this, getIRPosition(), DepClassTy::OPTIONAL);

  Function *F = getAnchorScope();
  BasicBlock &EntryBB = F->getEntryBlock();
  bool IsKernel = omp::isOpenMPKernel(*F);

  SmallVector<Instruction *> SyncInstWorklist;
  for (auto &RIt : *RPOT) {
    BasicBlock &BB = *RIt;

    bool IsEntryBB = &BB == &EntryBB;
    // TODO: We use local reasoning since we don't have a divergence analysis
    // 	     running as well. We could basically allow uniform branches here.
    bool AlignedBarrierLastInBlock = IsEntryBB && IsKernel;
    bool IsExplicitlyAligned = IsEntryBB && IsKernel;
    ExecutionDomainTy ED;
    // Propagate "incoming edges" into information about this block.
    if (IsEntryBB) {
      Changed |= handleCallees(A, ED);
    } else {
      // For live non-entry blocks we only propagate
      // information via live edges.
      if (LivenessAA && LivenessAA->isAssumedDead(&BB))
        continue;

      for (auto *PredBB : predecessors(&BB)) {
        if (LivenessAA && LivenessAA->isEdgeDead(PredBB, &BB))
          continue;
        bool InitialEdgeOnly = isInitialThreadOnlyEdge(
            A, dyn_cast<BranchInst>(PredBB->getTerminator()), BB);
        mergeInPredecessor(A, ED, BEDMap[PredBB], InitialEdgeOnly);
      }
    }

    // Now we traverse the block, accumulate effects in ED and attach
    // information to calls.
    for (Instruction &I : BB) {
      bool UsedAssumedInformation;
      if (A.isAssumedDead(I, *this, LivenessAA, UsedAssumedInformation,
                          /* CheckBBLivenessOnly */ false, DepClassTy::OPTIONAL,
                          /* CheckForDeadStore */ true))
        continue;

      // Asummes and "assume-like" (dbg, lifetime, ...) are handled first, the
      // former is collected the latter is ignored.
      if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
        if (auto *AI = dyn_cast_or_null<AssumeInst>(II)) {
          ED.addAssumeInst(A, *AI);
          continue;
        }
        // TODO: Should we also collect and delete lifetime markers?
        if (II->isAssumeLikeIntrinsic())
          continue;
      }

      if (auto *FI = dyn_cast<FenceInst>(&I)) {
        if (!ED.EncounteredNonLocalSideEffect) {
          // An aligned fence without non-local side-effects is a no-op.
          if (ED.IsReachedFromAlignedBarrierOnly)
            continue;
          // A non-aligned fence without non-local side-effects is a no-op
          // if the ordering only publishes non-local side-effects (or less).
          switch (FI->getOrdering()) {
          case AtomicOrdering::NotAtomic:
            continue;
          case AtomicOrdering::Unordered:
            continue;
          case AtomicOrdering::Monotonic:
            continue;
          case AtomicOrdering::Acquire:
            break;
          case AtomicOrdering::Release:
            continue;
          case AtomicOrdering::AcquireRelease:
            break;
          case AtomicOrdering::SequentiallyConsistent:
            break;
          };
        }
        NonNoOpFences.insert(FI);
      }

      auto *CB = dyn_cast<CallBase>(&I);
      bool IsNoSync = AA::isNoSyncInst(A, I, *this);
      bool IsAlignedBarrier =
          !IsNoSync && CB &&
          AANoSync::isAlignedBarrier(*CB, AlignedBarrierLastInBlock);

      AlignedBarrierLastInBlock &= IsNoSync;
      IsExplicitlyAligned &= IsNoSync;

      // Next we check for calls. Aligned barriers are handled
      // explicitly, everything else is kept for the backward traversal and will
      // also affect our state.
      if (CB) {
        if (IsAlignedBarrier) {
          HandleAlignedBarrier(*CB, ED);
          AlignedBarrierLastInBlock = true;
          IsExplicitlyAligned = true;
          continue;
        }

        // Check the pointer(s) of a memory intrinsic explicitly.
        if (isa<MemIntrinsic>(&I)) {
          if (!ED.EncounteredNonLocalSideEffect &&
              AA::isPotentiallyAffectedByBarrier(A, I, *this))
            ED.EncounteredNonLocalSideEffect = true;
          if (!IsNoSync) {
            ED.IsReachedFromAlignedBarrierOnly = false;
            SyncInstWorklist.push_back(&I);
          }
          continue;
        }

        // Record how we entered the call, then accumulate the effect of the
        // call in ED for potential use by the callee.
        auto &CallInED = CEDMap[{CB, PRE}];
        Changed |= mergeInPredecessor(A, CallInED, ED);

        // If we have a sync-definition we can check if it starts/ends in an
        // aligned barrier. If we are unsure we assume any sync breaks
        // alignment.
        Function *Callee = CB->getCalledFunction();
        if (!IsNoSync && Callee && !Callee->isDeclaration()) {
          const auto *EDAA = A.getAAFor<AAExecutionDomain>(
              *this, IRPosition::function(*Callee), DepClassTy::OPTIONAL);
          if (EDAA && EDAA->getState().isValidState()) {
            const auto &CalleeED = EDAA->getFunctionExecutionDomain();
            ED.IsReachedFromAlignedBarrierOnly =
                CalleeED.IsReachedFromAlignedBarrierOnly;
            AlignedBarrierLastInBlock = ED.IsReachedFromAlignedBarrierOnly;
            if (IsNoSync || !CalleeED.IsReachedFromAlignedBarrierOnly)
              ED.EncounteredNonLocalSideEffect |=
                  CalleeED.EncounteredNonLocalSideEffect;
            else
              ED.EncounteredNonLocalSideEffect =
                  CalleeED.EncounteredNonLocalSideEffect;
            if (!CalleeED.IsReachingAlignedBarrierOnly) {
              Changed |=
                  setAndRecord(CallInED.IsReachingAlignedBarrierOnly, false);
              SyncInstWorklist.push_back(&I);
            }
            if (CalleeED.IsReachedFromAlignedBarrierOnly)
              mergeInPredecessorBarriersAndAssumptions(A, ED, CalleeED);
            auto &CallOutED = CEDMap[{CB, POST}];
            Changed |= mergeInPredecessor(A, CallOutED, ED);
            continue;
          }
        }
        if (!IsNoSync) {
          ED.IsReachedFromAlignedBarrierOnly = false;
          Changed |= setAndRecord(CallInED.IsReachingAlignedBarrierOnly, false);
          SyncInstWorklist.push_back(&I);
        }
        AlignedBarrierLastInBlock &= ED.IsReachedFromAlignedBarrierOnly;
        ED.EncounteredNonLocalSideEffect |= !CB->doesNotAccessMemory();
        auto &CallOutED = CEDMap[{CB, POST}];
        Changed |= mergeInPredecessor(A, CallOutED, ED);
      }

      if (!I.mayHaveSideEffects() && !I.mayReadFromMemory())
        continue;

      // If we have a callee we try to use fine-grained information to
      // determine local side-effects.
      if (CB) {
        const auto *MemAA = A.getAAFor<AAMemoryLocation>(
            *this, IRPosition::callsite_function(*CB), DepClassTy::OPTIONAL);

        auto AccessPred = [&](const Instruction *I, const Value *Ptr,
                              AAMemoryLocation::AccessKind,
                              AAMemoryLocation::MemoryLocationsKind) {
          return !AA::isPotentiallyAffectedByBarrier(A, {Ptr}, *this, I);
        };
        if (MemAA && MemAA->getState().isValidState() &&
            MemAA->checkForAllAccessesToMemoryKind(
                AccessPred, AAMemoryLocation::ALL_LOCATIONS))
          continue;
      }

      auto &InfoCache = A.getInfoCache();
      if (!I.mayHaveSideEffects() && InfoCache.isOnlyUsedByAssume(I))
        continue;

      if (auto *LI = dyn_cast<LoadInst>(&I))
        if (LI->hasMetadata(LLVMContext::MD_invariant_load))
          continue;

      if (!ED.EncounteredNonLocalSideEffect &&
          AA::isPotentiallyAffectedByBarrier(A, I, *this))
        ED.EncounteredNonLocalSideEffect = true;
    }

    bool IsEndAndNotReachingAlignedBarriersOnly = false;
    if (!isa<UnreachableInst>(BB.getTerminator()) &&
        !BB.getTerminator()->getNumSuccessors()) {

      Changed |= mergeInPredecessor(A, InterProceduralED, ED);

      auto &FnED = BEDMap[nullptr];
      if (IsKernel && !IsExplicitlyAligned)
        FnED.IsReachingAlignedBarrierOnly = false;
      Changed |= mergeInPredecessor(A, FnED, ED);

      if (!FnED.IsReachingAlignedBarrierOnly) {
        IsEndAndNotReachingAlignedBarriersOnly = true;
        SyncInstWorklist.push_back(BB.getTerminator());
        auto &BBED = BEDMap[&BB];
        Changed |= setAndRecord(BBED.IsReachingAlignedBarrierOnly, false);
      }
    }

    ExecutionDomainTy &StoredED = BEDMap[&BB];
    ED.IsReachingAlignedBarrierOnly = StoredED.IsReachingAlignedBarrierOnly &
                                      !IsEndAndNotReachingAlignedBarriersOnly;

    // Check if we computed anything different as part of the forward
    // traversal. We do not take assumptions and aligned barriers into account
    // as they do not influence the state we iterate. Backward traversal values
    // are handled later on.
    if (ED.IsExecutedByInitialThreadOnly !=
            StoredED.IsExecutedByInitialThreadOnly ||
        ED.IsReachedFromAlignedBarrierOnly !=
            StoredED.IsReachedFromAlignedBarrierOnly ||
        ED.EncounteredNonLocalSideEffect !=
            StoredED.EncounteredNonLocalSideEffect)
      Changed = true;

    // Update the state with the new value.
    StoredED = std::move(ED);
  }

  // Propagate (non-aligned) sync instruction effects backwards until the
  // entry is hit or an aligned barrier.
  SmallSetVector<BasicBlock *, 16> Visited;
  while (!SyncInstWorklist.empty()) {
    Instruction *SyncInst = SyncInstWorklist.pop_back_val();
    Instruction *CurInst = SyncInst;
    bool HitAlignedBarrierOrKnownEnd = false;
    while ((CurInst = CurInst->getPrevNode())) {
      auto *CB = dyn_cast<CallBase>(CurInst);
      if (!CB)
        continue;
      auto &CallOutED = CEDMap[{CB, POST}];
      Changed |= setAndRecord(CallOutED.IsReachingAlignedBarrierOnly, false);
      auto &CallInED = CEDMap[{CB, PRE}];
      HitAlignedBarrierOrKnownEnd =
          AlignedBarriers.count(CB) || !CallInED.IsReachingAlignedBarrierOnly;
      if (HitAlignedBarrierOrKnownEnd)
        break;
      Changed |= setAndRecord(CallInED.IsReachingAlignedBarrierOnly, false);
    }
    if (HitAlignedBarrierOrKnownEnd)
      continue;
    BasicBlock *SyncBB = SyncInst->getParent();
    for (auto *PredBB : predecessors(SyncBB)) {
      if (LivenessAA && LivenessAA->isEdgeDead(PredBB, SyncBB))
        continue;
      if (!Visited.insert(PredBB))
        continue;
      auto &PredED = BEDMap[PredBB];
      if (setAndRecord(PredED.IsReachingAlignedBarrierOnly, false)) {
        Changed = true;
        SyncInstWorklist.push_back(PredBB->getTerminator());
      }
    }
    if (SyncBB != &EntryBB)
      continue;
    Changed |=
        setAndRecord(InterProceduralED.IsReachingAlignedBarrierOnly, false);
  }

  return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
}

/// Try to replace memory allocation calls called by a single thread with a
/// static buffer of shared memory.
struct AAHeapToShared : public StateWrapper<BooleanState, AbstractAttribute> {
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  AAHeapToShared(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Create an abstract attribute view for the position \p IRP.
  static AAHeapToShared &createForPosition(const IRPosition &IRP,
                                           Attributor &A);

  /// Returns true if HeapToShared conversion is assumed to be possible.
  virtual bool isAssumedHeapToShared(CallBase &CB) const = 0;

  /// Returns true if HeapToShared conversion is assumed and the CB is a
  /// callsite to a free operation to be removed.
  virtual bool isAssumedHeapToSharedRemovedFree(CallBase &CB) const = 0;

  /// See AbstractAttribute::getName().
  const std::string getName() const override { return "AAHeapToShared"; }

  /// See AbstractAttribute::getIdAddr().
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAHeapToShared.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  static const char ID;
};

struct AAHeapToSharedFunction : public AAHeapToShared {
  AAHeapToSharedFunction(const IRPosition &IRP, Attributor &A)
      : AAHeapToShared(IRP, A) {}

  const std::string getAsStr(Attributor *) const override {
    return "[AAHeapToShared] " + std::to_string(MallocCalls.size()) +
           " malloc calls eligible.";
  }

  /// See AbstractAttribute::trackStatistics().
  void trackStatistics() const override {}

  /// This functions finds free calls that will be removed by the
  /// HeapToShared transformation.
  void findPotentialRemovedFreeCalls(Attributor &A) {
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    auto &FreeRFI = OMPInfoCache.RFIs[OMPRTL___kmpc_free_shared];

    PotentialRemovedFreeCalls.clear();
    // Update free call users of found malloc calls.
    for (CallBase *CB : MallocCalls) {
      SmallVector<CallBase *, 4> FreeCalls;
      for (auto *U : CB->users()) {
        CallBase *C = dyn_cast<CallBase>(U);
        if (C && C->getCalledFunction() == FreeRFI.Declaration)
          FreeCalls.push_back(C);
      }

      if (FreeCalls.size() != 1)
        continue;

      PotentialRemovedFreeCalls.insert(FreeCalls.front());
    }
  }

  void initialize(Attributor &A) override {
    if (DisableOpenMPOptDeglobalization) {
      indicatePessimisticFixpoint();
      return;
    }

    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    auto &RFI = OMPInfoCache.RFIs[OMPRTL___kmpc_alloc_shared];
    if (!RFI.Declaration)
      return;

    Attributor::SimplifictionCallbackTy SCB =
        [](const IRPosition &, const AbstractAttribute *,
           bool &) -> std::optional<Value *> { return nullptr; };

    Function *F = getAnchorScope();
    for (User *U : RFI.Declaration->users())
      if (CallBase *CB = dyn_cast<CallBase>(U)) {
        if (CB->getFunction() != F)
          continue;
        MallocCalls.insert(CB);
        A.registerSimplificationCallback(IRPosition::callsite_returned(*CB),
                                         SCB);
      }

    findPotentialRemovedFreeCalls(A);
  }

  bool isAssumedHeapToShared(CallBase &CB) const override {
    return isValidState() && MallocCalls.count(&CB);
  }

  bool isAssumedHeapToSharedRemovedFree(CallBase &CB) const override {
    return isValidState() && PotentialRemovedFreeCalls.count(&CB);
  }

  ChangeStatus manifest(Attributor &A) override {
    if (MallocCalls.empty())
      return ChangeStatus::UNCHANGED;

    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    auto &FreeCall = OMPInfoCache.RFIs[OMPRTL___kmpc_free_shared];

    Function *F = getAnchorScope();
    auto *HS = A.lookupAAFor<AAHeapToStack>(IRPosition::function(*F), this,
                                            DepClassTy::OPTIONAL);

    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    for (CallBase *CB : MallocCalls) {
      // Skip replacing this if HeapToStack has already claimed it.
      if (HS && HS->isAssumedHeapToStack(*CB))
        continue;

      // Find the unique free call to remove it.
      SmallVector<CallBase *, 4> FreeCalls;
      for (auto *U : CB->users()) {
        CallBase *C = dyn_cast<CallBase>(U);
        if (C && C->getCalledFunction() == FreeCall.Declaration)
          FreeCalls.push_back(C);
      }
      if (FreeCalls.size() != 1)
        continue;

      auto *AllocSize = cast<ConstantInt>(CB->getArgOperand(0));

      if (AllocSize->getZExtValue() + SharedMemoryUsed > SharedMemoryLimit) {
        LLVM_DEBUG(dbgs() << TAG << "Cannot replace call " << *CB
                          << " with shared memory."
                          << " Shared memory usage is limited to "
                          << SharedMemoryLimit << " bytes\n");
        continue;
      }

      LLVM_DEBUG(dbgs() << TAG << "Replace globalization call " << *CB
                        << " with " << AllocSize->getZExtValue()
                        << " bytes of shared memory\n");

      // Create a new shared memory buffer of the same size as the allocation
      // and replace all the uses of the original allocation with it.
      Module *M = CB->getModule();
      Type *Int8Ty = Type::getInt8Ty(M->getContext());
      Type *Int8ArrTy = ArrayType::get(Int8Ty, AllocSize->getZExtValue());
      auto *SharedMem = new GlobalVariable(
          *M, Int8ArrTy, /* IsConstant */ false, GlobalValue::InternalLinkage,
          PoisonValue::get(Int8ArrTy), CB->getName() + "_shared", nullptr,
          GlobalValue::NotThreadLocal,
          static_cast<unsigned>(AddressSpace::Shared));
      auto *NewBuffer =
          ConstantExpr::getPointerCast(SharedMem, Int8Ty->getPointerTo());

      auto Remark = [&](OptimizationRemark OR) {
        return OR << "Replaced globalized variable with "
                  << ore::NV("SharedMemory", AllocSize->getZExtValue())
                  << (AllocSize->isOne() ? " byte " : " bytes ")
                  << "of shared memory.";
      };
      A.emitRemark<OptimizationRemark>(CB, "OMP111", Remark);

      MaybeAlign Alignment = CB->getRetAlign();
      assert(Alignment &&
             "HeapToShared on allocation without alignment attribute");
      SharedMem->setAlignment(*Alignment);

      A.changeAfterManifest(IRPosition::callsite_returned(*CB), *NewBuffer);
      A.deleteAfterManifest(*CB);
      A.deleteAfterManifest(*FreeCalls.front());

      SharedMemoryUsed += AllocSize->getZExtValue();
      NumBytesMovedToSharedMemory = SharedMemoryUsed;
      Changed = ChangeStatus::CHANGED;
    }

    return Changed;
  }

  ChangeStatus updateImpl(Attributor &A) override {
    if (MallocCalls.empty())
      return indicatePessimisticFixpoint();
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    auto &RFI = OMPInfoCache.RFIs[OMPRTL___kmpc_alloc_shared];
    if (!RFI.Declaration)
      return ChangeStatus::UNCHANGED;

    Function *F = getAnchorScope();

    auto NumMallocCalls = MallocCalls.size();

    // Only consider malloc calls executed by a single thread with a constant.
    for (User *U : RFI.Declaration->users()) {
      if (CallBase *CB = dyn_cast<CallBase>(U)) {
        if (CB->getCaller() != F)
          continue;
        if (!MallocCalls.count(CB))
          continue;
        if (!isa<ConstantInt>(CB->getArgOperand(0))) {
          MallocCalls.remove(CB);
          continue;
        }
        const auto *ED = A.getAAFor<AAExecutionDomain>(
            *this, IRPosition::function(*F), DepClassTy::REQUIRED);
        if (!ED || !ED->isExecutedByInitialThreadOnly(*CB))
          MallocCalls.remove(CB);
      }
    }

    findPotentialRemovedFreeCalls(A);

    if (NumMallocCalls != MallocCalls.size())
      return ChangeStatus::CHANGED;

    return ChangeStatus::UNCHANGED;
  }

  /// Collection of all malloc calls in a function.
  SmallSetVector<CallBase *, 4> MallocCalls;
  /// Collection of potentially removed free calls in a function.
  SmallPtrSet<CallBase *, 4> PotentialRemovedFreeCalls;
  /// The total amount of shared memory that has been used for HeapToShared.
  unsigned SharedMemoryUsed = 0;
};

struct AAKernelInfo : public StateWrapper<KernelInfoState, AbstractAttribute> {
  using Base = StateWrapper<KernelInfoState, AbstractAttribute>;
  AAKernelInfo(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// The callee value is tracked beyond a simple stripPointerCasts, so we allow
  /// unknown callees.
  static bool requiresCalleeForCallBase() { return false; }

  /// Statistics are tracked as part of manifest for now.
  void trackStatistics() const override {}

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *) const override {
    if (!isValidState())
      return "<invalid>";
    return std::string(SPMDCompatibilityTracker.isAssumed() ? "SPMD"
                                                            : "generic") +
           std::string(SPMDCompatibilityTracker.isAtFixpoint() ? " [FIX]"
                                                               : "") +
           std::string(" #PRs: ") +
           (ReachedKnownParallelRegions.isValidState()
                ? std::to_string(ReachedKnownParallelRegions.size())
                : "<invalid>") +
           ", #Unknown PRs: " +
           (ReachedUnknownParallelRegions.isValidState()
                ? std::to_string(ReachedUnknownParallelRegions.size())
                : "<invalid>") +
           ", #Reaching Kernels: " +
           (ReachingKernelEntries.isValidState()
                ? std::to_string(ReachingKernelEntries.size())
                : "<invalid>") +
           ", #ParLevels: " +
           (ParallelLevels.isValidState()
                ? std::to_string(ParallelLevels.size())
                : "<invalid>") +
           ", NestedPar: " + (NestedParallelism ? "yes" : "no");
  }

  /// Create an abstract attribute biew for the position \p IRP.
  static AAKernelInfo &createForPosition(const IRPosition &IRP, Attributor &A);

  /// See AbstractAttribute::getName()
  const std::string getName() const override { return "AAKernelInfo"; }

  /// See AbstractAttribute::getIdAddr()
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AAKernelInfo
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  static const char ID;
};

/// The function kernel info abstract attribute, basically, what can we say
/// about a function with regards to the KernelInfoState.
struct AAKernelInfoFunction : AAKernelInfo {
  AAKernelInfoFunction(const IRPosition &IRP, Attributor &A)
      : AAKernelInfo(IRP, A) {}

  SmallPtrSet<Instruction *, 4> GuardedInstructions;

  SmallPtrSetImpl<Instruction *> &getGuardedInstructions() {
    return GuardedInstructions;
  }

  void setConfigurationOfKernelEnvironment(ConstantStruct *ConfigC) {
    Constant *NewKernelEnvC = ConstantFoldInsertValueInstruction(
        KernelEnvC, ConfigC, {KernelInfo::ConfigurationIdx});
    assert(NewKernelEnvC && "Failed to create new kernel environment");
    KernelEnvC = cast<ConstantStruct>(NewKernelEnvC);
  }

#define KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(MEMBER)                        \
  void set##MEMBER##OfKernelEnvironment(ConstantInt *NewVal) {                 \
    ConstantStruct *ConfigC =                                                  \
        KernelInfo::getConfigurationFromKernelEnvironment(KernelEnvC);         \
    Constant *NewConfigC = ConstantFoldInsertValueInstruction(                 \
        ConfigC, NewVal, {KernelInfo::MEMBER##Idx});                           \
    assert(NewConfigC && "Failed to create new configuration environment");    \
    setConfigurationOfKernelEnvironment(cast<ConstantStruct>(NewConfigC));     \
  }

  KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(UseGenericStateMachine)
  KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(MayUseNestedParallelism)
  KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(ExecMode)
  KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(MinThreads)
  KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(MaxThreads)
  KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(MinTeams)
  KERNEL_ENVIRONMENT_CONFIGURATION_SETTER(MaxTeams)

#undef KERNEL_ENVIRONMENT_CONFIGURATION_SETTER

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    // This is a high-level transform that might change the constant arguments
    // of the init and dinit calls. We need to tell the Attributor about this
    // to avoid other parts using the current constant value for simpliication.
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());

    Function *Fn = getAnchorScope();

    OMPInformationCache::RuntimeFunctionInfo &InitRFI =
        OMPInfoCache.RFIs[OMPRTL___kmpc_target_init];
    OMPInformationCache::RuntimeFunctionInfo &DeinitRFI =
        OMPInfoCache.RFIs[OMPRTL___kmpc_target_deinit];

    // For kernels we perform more initialization work, first we find the init
    // and deinit calls.
    auto StoreCallBase = [](Use &U,
                            OMPInformationCache::RuntimeFunctionInfo &RFI,
                            CallBase *&Storage) {
      CallBase *CB = OpenMPOpt::getCallIfRegularCall(U, &RFI);
      assert(CB &&
             "Unexpected use of __kmpc_target_init or __kmpc_target_deinit!");
      assert(!Storage &&
             "Multiple uses of __kmpc_target_init or __kmpc_target_deinit!");
      Storage = CB;
      return false;
    };
    InitRFI.foreachUse(
        [&](Use &U, Function &) {
          StoreCallBase(U, InitRFI, KernelInitCB);
          return false;
        },
        Fn);
    DeinitRFI.foreachUse(
        [&](Use &U, Function &) {
          StoreCallBase(U, DeinitRFI, KernelDeinitCB);
          return false;
        },
        Fn);

    // Ignore kernels without initializers such as global constructors.
    if (!KernelInitCB || !KernelDeinitCB)
      return;

    // Add itself to the reaching kernel and set IsKernelEntry.
    ReachingKernelEntries.insert(Fn);
    IsKernelEntry = true;

    KernelEnvC =
        KernelInfo::getKernelEnvironementFromKernelInitCB(KernelInitCB);
    GlobalVariable *KernelEnvGV =
        KernelInfo::getKernelEnvironementGVFromKernelInitCB(KernelInitCB);

    Attributor::GlobalVariableSimplifictionCallbackTy
        KernelConfigurationSimplifyCB =
            [&](const GlobalVariable &GV, const AbstractAttribute *AA,
                bool &UsedAssumedInformation) -> std::optional<Constant *> {
      if (!isAtFixpoint()) {
        if (!AA)
          return nullptr;
        UsedAssumedInformation = true;
        A.recordDependence(*this, *AA, DepClassTy::OPTIONAL);
      }
      return KernelEnvC;
    };

    A.registerGlobalVariableSimplificationCallback(
        *KernelEnvGV, KernelConfigurationSimplifyCB);

    // Check if we know we are in SPMD-mode already.
    ConstantInt *ExecModeC =
        KernelInfo::getExecModeFromKernelEnvironment(KernelEnvC);
    ConstantInt *AssumedExecModeC = ConstantInt::get(
        ExecModeC->getIntegerType(),
        ExecModeC->getSExtValue() | OMP_TGT_EXEC_MODE_GENERIC_SPMD);
    if (ExecModeC->getSExtValue() & OMP_TGT_EXEC_MODE_SPMD)
      SPMDCompatibilityTracker.indicateOptimisticFixpoint();
    else if (DisableOpenMPOptSPMDization)
      // This is a generic region but SPMDization is disabled so stop
      // tracking.
      SPMDCompatibilityTracker.indicatePessimisticFixpoint();
    else
      setExecModeOfKernelEnvironment(AssumedExecModeC);

    const Triple T(Fn->getParent()->getTargetTriple());
    auto *Int32Ty = Type::getInt32Ty(Fn->getContext());
    auto [MinThreads, MaxThreads] =
        OpenMPIRBuilder::readThreadBoundsForKernel(T, *Fn);
    if (MinThreads)
      setMinThreadsOfKernelEnvironment(ConstantInt::get(Int32Ty, MinThreads));
    if (MaxThreads)
      setMaxThreadsOfKernelEnvironment(ConstantInt::get(Int32Ty, MaxThreads));
    auto [MinTeams, MaxTeams] =
        OpenMPIRBuilder::readTeamBoundsForKernel(T, *Fn);
    if (MinTeams)
      setMinTeamsOfKernelEnvironment(ConstantInt::get(Int32Ty, MinTeams));
    if (MaxTeams)
      setMaxTeamsOfKernelEnvironment(ConstantInt::get(Int32Ty, MaxTeams));

    ConstantInt *MayUseNestedParallelismC =
        KernelInfo::getMayUseNestedParallelismFromKernelEnvironment(KernelEnvC);
    ConstantInt *AssumedMayUseNestedParallelismC = ConstantInt::get(
        MayUseNestedParallelismC->getIntegerType(), NestedParallelism);
    setMayUseNestedParallelismOfKernelEnvironment(
        AssumedMayUseNestedParallelismC);

    if (!DisableOpenMPOptStateMachineRewrite) {
      ConstantInt *UseGenericStateMachineC =
          KernelInfo::getUseGenericStateMachineFromKernelEnvironment(
              KernelEnvC);
      ConstantInt *AssumedUseGenericStateMachineC =
          ConstantInt::get(UseGenericStateMachineC->getIntegerType(), false);
      setUseGenericStateMachineOfKernelEnvironment(
          AssumedUseGenericStateMachineC);
    }

    // Register virtual uses of functions we might need to preserve.
    auto RegisterVirtualUse = [&](RuntimeFunction RFKind,
                                  Attributor::VirtualUseCallbackTy &CB) {
      if (!OMPInfoCache.RFIs[RFKind].Declaration)
        return;
      A.registerVirtualUseCallback(*OMPInfoCache.RFIs[RFKind].Declaration, CB);
    };

    // Add a dependence to ensure updates if the state changes.
    auto AddDependence = [](Attributor &A, const AAKernelInfo *KI,
                            const AbstractAttribute *QueryingAA) {
      if (QueryingAA) {
        A.recordDependence(*KI, *QueryingAA, DepClassTy::OPTIONAL);
      }
      return true;
    };

    Attributor::VirtualUseCallbackTy CustomStateMachineUseCB =
        [&](Attributor &A, const AbstractAttribute *QueryingAA) {
          // Whenever we create a custom state machine we will insert calls to
          // __kmpc_get_hardware_num_threads_in_block,
          // __kmpc_get_warp_size,
          // __kmpc_barrier_simple_generic,
          // __kmpc_kernel_parallel, and
          // __kmpc_kernel_end_parallel.
          // Not needed if we are on track for SPMDzation.
          if (SPMDCompatibilityTracker.isValidState())
            return AddDependence(A, this, QueryingAA);
          // Not needed if we can't rewrite due to an invalid state.
          if (!ReachedKnownParallelRegions.isValidState())
            return AddDependence(A, this, QueryingAA);
          return false;
        };

    // Not needed if we are pre-runtime merge.
    if (!KernelInitCB->getCalledFunction()->isDeclaration()) {
      RegisterVirtualUse(OMPRTL___kmpc_get_hardware_num_threads_in_block,
                         CustomStateMachineUseCB);
      RegisterVirtualUse(OMPRTL___kmpc_get_warp_size, CustomStateMachineUseCB);
      RegisterVirtualUse(OMPRTL___kmpc_barrier_simple_generic,
                         CustomStateMachineUseCB);
      RegisterVirtualUse(OMPRTL___kmpc_kernel_parallel,
                         CustomStateMachineUseCB);
      RegisterVirtualUse(OMPRTL___kmpc_kernel_end_parallel,
                         CustomStateMachineUseCB);
    }

    // If we do not perform SPMDzation we do not need the virtual uses below.
    if (SPMDCompatibilityTracker.isAtFixpoint())
      return;

    Attributor::VirtualUseCallbackTy HWThreadIdUseCB =
        [&](Attributor &A, const AbstractAttribute *QueryingAA) {
          // Whenever we perform SPMDzation we will insert
          // __kmpc_get_hardware_thread_id_in_block calls.
          if (!SPMDCompatibilityTracker.isValidState())
            return AddDependence(A, this, QueryingAA);
          return false;
        };
    RegisterVirtualUse(OMPRTL___kmpc_get_hardware_thread_id_in_block,
                       HWThreadIdUseCB);

    Attributor::VirtualUseCallbackTy SPMDBarrierUseCB =
        [&](Attributor &A, const AbstractAttribute *QueryingAA) {
          // Whenever we perform SPMDzation with guarding we will insert
          // __kmpc_simple_barrier_spmd calls. If SPMDzation failed, there is
          // nothing to guard, or there are no parallel regions, we don't need
          // the calls.
          if (!SPMDCompatibilityTracker.isValidState())
            return AddDependence(A, this, QueryingAA);
          if (SPMDCompatibilityTracker.empty())
            return AddDependence(A, this, QueryingAA);
          if (!mayContainParallelRegion())
            return AddDependence(A, this, QueryingAA);
          return false;
        };
    RegisterVirtualUse(OMPRTL___kmpc_barrier_simple_spmd, SPMDBarrierUseCB);
  }

  /// Sanitize the string \p S such that it is a suitable global symbol name.
  static std::string sanitizeForGlobalName(std::string S) {
    std::replace_if(
        S.begin(), S.end(),
        [](const char C) {
          return !((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
                   (C >= '0' && C <= '9') || C == '_');
        },
        '.');
    return S;
  }

  /// Modify the IR based on the KernelInfoState as the fixpoint iteration is
  /// finished now.
  ChangeStatus manifest(Attributor &A) override {
    // If we are not looking at a kernel with __kmpc_target_init and
    // __kmpc_target_deinit call we cannot actually manifest the information.
    if (!KernelInitCB || !KernelDeinitCB)
      return ChangeStatus::UNCHANGED;

    ChangeStatus Changed = ChangeStatus::UNCHANGED;

    bool HasBuiltStateMachine = true;
    if (!changeToSPMDMode(A, Changed)) {
      if (!KernelInitCB->getCalledFunction()->isDeclaration())
        HasBuiltStateMachine = buildCustomStateMachine(A, Changed);
      else
        HasBuiltStateMachine = false;
    }

    // We need to reset KernelEnvC if specific rewriting is not done.
    ConstantStruct *ExistingKernelEnvC =
        KernelInfo::getKernelEnvironementFromKernelInitCB(KernelInitCB);
    ConstantInt *OldUseGenericStateMachineVal =
        KernelInfo::getUseGenericStateMachineFromKernelEnvironment(
            ExistingKernelEnvC);
    if (!HasBuiltStateMachine)
      setUseGenericStateMachineOfKernelEnvironment(
          OldUseGenericStateMachineVal);

    // At last, update the KernelEnvc
    GlobalVariable *KernelEnvGV =
        KernelInfo::getKernelEnvironementGVFromKernelInitCB(KernelInitCB);
    if (KernelEnvGV->getInitializer() != KernelEnvC) {
      KernelEnvGV->setInitializer(KernelEnvC);
      Changed = ChangeStatus::CHANGED;
    }

    return Changed;
  }

  void insertInstructionGuardsHelper(Attributor &A) {
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());

    auto CreateGuardedRegion = [&](Instruction *RegionStartI,
                                   Instruction *RegionEndI) {
      LoopInfo *LI = nullptr;
      DominatorTree *DT = nullptr;
      MemorySSAUpdater *MSU = nullptr;
      using InsertPointTy = OpenMPIRBuilder::InsertPointTy;

      BasicBlock *ParentBB = RegionStartI->getParent();
      Function *Fn = ParentBB->getParent();
      Module &M = *Fn->getParent();

      // Create all the blocks and logic.
      // ParentBB:
      //    goto RegionCheckTidBB
      // RegionCheckTidBB:
      //    Tid = __kmpc_hardware_thread_id()
      //    if (Tid != 0)
      //        goto RegionBarrierBB
      // RegionStartBB:
      //    <execute instructions guarded>
      //    goto RegionEndBB
      // RegionEndBB:
      //    <store escaping values to shared mem>
      //    goto RegionBarrierBB
      //  RegionBarrierBB:
      //    __kmpc_simple_barrier_spmd()
      //    // second barrier is omitted if lacking escaping values.
      //    <load escaping values from shared mem>
      //    __kmpc_simple_barrier_spmd()
      //    goto RegionExitBB
      // RegionExitBB:
      //    <execute rest of instructions>

      BasicBlock *RegionEndBB = SplitBlock(ParentBB, RegionEndI->getNextNode(),
                                           DT, LI, MSU, "region.guarded.end");
      BasicBlock *RegionBarrierBB =
          SplitBlock(RegionEndBB, &*RegionEndBB->getFirstInsertionPt(), DT, LI,
                     MSU, "region.barrier");
      BasicBlock *RegionExitBB =
          SplitBlock(RegionBarrierBB, &*RegionBarrierBB->getFirstInsertionPt(),
                     DT, LI, MSU, "region.exit");
      BasicBlock *RegionStartBB =
          SplitBlock(ParentBB, RegionStartI, DT, LI, MSU, "region.guarded");

      assert(ParentBB->getUniqueSuccessor() == RegionStartBB &&
             "Expected a different CFG");

      BasicBlock *RegionCheckTidBB = SplitBlock(
          ParentBB, ParentBB->getTerminator(), DT, LI, MSU, "region.check.tid");

      // Register basic blocks with the Attributor.
      A.registerManifestAddedBasicBlock(*RegionEndBB);
      A.registerManifestAddedBasicBlock(*RegionBarrierBB);
      A.registerManifestAddedBasicBlock(*RegionExitBB);
      A.registerManifestAddedBasicBlock(*RegionStartBB);
      A.registerManifestAddedBasicBlock(*RegionCheckTidBB);

      bool HasBroadcastValues = false;
      // Find escaping outputs from the guarded region to outside users and
      // broadcast their values to them.
      for (Instruction &I : *RegionStartBB) {
        SmallVector<Use *, 4> OutsideUses;
        for (Use &U : I.uses()) {
          Instruction &UsrI = *cast<Instruction>(U.getUser());
          if (UsrI.getParent() != RegionStartBB)
            OutsideUses.push_back(&U);
        }

        if (OutsideUses.empty())
          continue;

        HasBroadcastValues = true;

        // Emit a global variable in shared memory to store the broadcasted
        // value.
        auto *SharedMem = new GlobalVariable(
            M, I.getType(), /* IsConstant */ false,
            GlobalValue::InternalLinkage, UndefValue::get(I.getType()),
            sanitizeForGlobalName(
                (I.getName() + ".guarded.output.alloc").str()),
            nullptr, GlobalValue::NotThreadLocal,
            static_cast<unsigned>(AddressSpace::Shared));

        // Emit a store instruction to update the value.
        new StoreInst(&I, SharedMem,
                      RegionEndBB->getTerminator()->getIterator());

        LoadInst *LoadI = new LoadInst(
            I.getType(), SharedMem, I.getName() + ".guarded.output.load",
            RegionBarrierBB->getTerminator()->getIterator());

        // Emit a load instruction and replace uses of the output value.
        for (Use *U : OutsideUses)
          A.changeUseAfterManifest(*U, *LoadI);
      }

      auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());

      // Go to tid check BB in ParentBB.
      const DebugLoc DL = ParentBB->getTerminator()->getDebugLoc();
      ParentBB->getTerminator()->eraseFromParent();
      OpenMPIRBuilder::LocationDescription Loc(
          InsertPointTy(ParentBB, ParentBB->end()), DL);
      OMPInfoCache.OMPBuilder.updateToLocation(Loc);
      uint32_t SrcLocStrSize;
      auto *SrcLocStr =
          OMPInfoCache.OMPBuilder.getOrCreateSrcLocStr(Loc, SrcLocStrSize);
      Value *Ident =
          OMPInfoCache.OMPBuilder.getOrCreateIdent(SrcLocStr, SrcLocStrSize);
      BranchInst::Create(RegionCheckTidBB, ParentBB)->setDebugLoc(DL);

      // Add check for Tid in RegionCheckTidBB
      RegionCheckTidBB->getTerminator()->eraseFromParent();
      OpenMPIRBuilder::LocationDescription LocRegionCheckTid(
          InsertPointTy(RegionCheckTidBB, RegionCheckTidBB->end()), DL);
      OMPInfoCache.OMPBuilder.updateToLocation(LocRegionCheckTid);
      FunctionCallee HardwareTidFn =
          OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
              M, OMPRTL___kmpc_get_hardware_thread_id_in_block);
      CallInst *Tid =
          OMPInfoCache.OMPBuilder.Builder.CreateCall(HardwareTidFn, {});
      Tid->setDebugLoc(DL);
      OMPInfoCache.setCallingConvention(HardwareTidFn, Tid);
      Value *TidCheck = OMPInfoCache.OMPBuilder.Builder.CreateIsNull(Tid);
      OMPInfoCache.OMPBuilder.Builder
          .CreateCondBr(TidCheck, RegionStartBB, RegionBarrierBB)
          ->setDebugLoc(DL);

      // First barrier for synchronization, ensures main thread has updated
      // values.
      FunctionCallee BarrierFn =
          OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
              M, OMPRTL___kmpc_barrier_simple_spmd);
      OMPInfoCache.OMPBuilder.updateToLocation(InsertPointTy(
          RegionBarrierBB, RegionBarrierBB->getFirstInsertionPt()));
      CallInst *Barrier =
          OMPInfoCache.OMPBuilder.Builder.CreateCall(BarrierFn, {Ident, Tid});
      Barrier->setDebugLoc(DL);
      OMPInfoCache.setCallingConvention(BarrierFn, Barrier);

      // Second barrier ensures workers have read broadcast values.
      if (HasBroadcastValues) {
        CallInst *Barrier =
            CallInst::Create(BarrierFn, {Ident, Tid}, "",
                             RegionBarrierBB->getTerminator()->getIterator());
        Barrier->setDebugLoc(DL);
        OMPInfoCache.setCallingConvention(BarrierFn, Barrier);
      }
    };

    auto &AllocSharedRFI = OMPInfoCache.RFIs[OMPRTL___kmpc_alloc_shared];
    SmallPtrSet<BasicBlock *, 8> Visited;
    for (Instruction *GuardedI : SPMDCompatibilityTracker) {
      BasicBlock *BB = GuardedI->getParent();
      if (!Visited.insert(BB).second)
        continue;

      SmallVector<std::pair<Instruction *, Instruction *>> Reorders;
      Instruction *LastEffect = nullptr;
      BasicBlock::reverse_iterator IP = BB->rbegin(), IPEnd = BB->rend();
      while (++IP != IPEnd) {
        if (!IP->mayHaveSideEffects() && !IP->mayReadFromMemory())
          continue;
        Instruction *I = &*IP;
        if (OpenMPOpt::getCallIfRegularCall(*I, &AllocSharedRFI))
          continue;
        if (!I->user_empty() || !SPMDCompatibilityTracker.contains(I)) {
          LastEffect = nullptr;
          continue;
        }
        if (LastEffect)
          Reorders.push_back({I, LastEffect});
        LastEffect = &*IP;
      }
      for (auto &Reorder : Reorders)
        Reorder.first->moveBefore(Reorder.second);
    }

    SmallVector<std::pair<Instruction *, Instruction *>, 4> GuardedRegions;

    for (Instruction *GuardedI : SPMDCompatibilityTracker) {
      BasicBlock *BB = GuardedI->getParent();
      auto *CalleeAA = A.lookupAAFor<AAKernelInfo>(
          IRPosition::function(*GuardedI->getFunction()), nullptr,
          DepClassTy::NONE);
      assert(CalleeAA != nullptr && "Expected Callee AAKernelInfo");
      auto &CalleeAAFunction = *cast<AAKernelInfoFunction>(CalleeAA);
      // Continue if instruction is already guarded.
      if (CalleeAAFunction.getGuardedInstructions().contains(GuardedI))
        continue;

      Instruction *GuardedRegionStart = nullptr, *GuardedRegionEnd = nullptr;
      for (Instruction &I : *BB) {
        // If instruction I needs to be guarded update the guarded region
        // bounds.
        if (SPMDCompatibilityTracker.contains(&I)) {
          CalleeAAFunction.getGuardedInstructions().insert(&I);
          if (GuardedRegionStart)
            GuardedRegionEnd = &I;
          else
            GuardedRegionStart = GuardedRegionEnd = &I;

          continue;
        }

        // Instruction I does not need guarding, store
        // any region found and reset bounds.
        if (GuardedRegionStart) {
          GuardedRegions.push_back(
              std::make_pair(GuardedRegionStart, GuardedRegionEnd));
          GuardedRegionStart = nullptr;
          GuardedRegionEnd = nullptr;
        }
      }
    }

    for (auto &GR : GuardedRegions)
      CreateGuardedRegion(GR.first, GR.second);
  }

  void forceSingleThreadPerWorkgroupHelper(Attributor &A) {
    // Only allow 1 thread per workgroup to continue executing the user code.
    //
    //     InitCB = __kmpc_target_init(...)
    //     ThreadIdInBlock = __kmpc_get_hardware_thread_id_in_block();
    //     if (ThreadIdInBlock != 0) return;
    // UserCode:
    //     // user code
    //
    auto &Ctx = getAnchorValue().getContext();
    Function *Kernel = getAssociatedFunction();
    assert(Kernel && "Expected an associated function!");

    // Create block for user code to branch to from initial block.
    BasicBlock *InitBB = KernelInitCB->getParent();
    BasicBlock *UserCodeBB = InitBB->splitBasicBlock(
        KernelInitCB->getNextNode(), "main.thread.user_code");
    BasicBlock *ReturnBB =
        BasicBlock::Create(Ctx, "exit.threads", Kernel, UserCodeBB);

    // Register blocks with attributor:
    A.registerManifestAddedBasicBlock(*InitBB);
    A.registerManifestAddedBasicBlock(*UserCodeBB);
    A.registerManifestAddedBasicBlock(*ReturnBB);

    // Debug location:
    const DebugLoc &DLoc = KernelInitCB->getDebugLoc();
    ReturnInst::Create(Ctx, ReturnBB)->setDebugLoc(DLoc);
    InitBB->getTerminator()->eraseFromParent();

    // Prepare call to OMPRTL___kmpc_get_hardware_thread_id_in_block.
    Module &M = *Kernel->getParent();
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    FunctionCallee ThreadIdInBlockFn =
        OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
            M, OMPRTL___kmpc_get_hardware_thread_id_in_block);

    // Get thread ID in block.
    CallInst *ThreadIdInBlock =
        CallInst::Create(ThreadIdInBlockFn, "thread_id.in.block", InitBB);
    OMPInfoCache.setCallingConvention(ThreadIdInBlockFn, ThreadIdInBlock);
    ThreadIdInBlock->setDebugLoc(DLoc);

    // Eliminate all threads in the block with ID not equal to 0:
    Instruction *IsMainThread =
        ICmpInst::Create(ICmpInst::ICmp, CmpInst::ICMP_NE, ThreadIdInBlock,
                         ConstantInt::get(ThreadIdInBlock->getType(), 0),
                         "thread.is_main", InitBB);
    IsMainThread->setDebugLoc(DLoc);
    BranchInst::Create(ReturnBB, UserCodeBB, IsMainThread, InitBB);
  }

  bool changeToSPMDMode(Attributor &A, ChangeStatus &Changed) {
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());

    // We cannot change to SPMD mode if the runtime functions aren't availible.
    if (!OMPInfoCache.runtimeFnsAvailable(
            {OMPRTL___kmpc_get_hardware_thread_id_in_block,
             OMPRTL___kmpc_barrier_simple_spmd}))
      return false;

    if (!SPMDCompatibilityTracker.isAssumed()) {
      for (Instruction *NonCompatibleI : SPMDCompatibilityTracker) {
        if (!NonCompatibleI)
          continue;

        // Skip diagnostics on calls to known OpenMP runtime functions for now.
        if (auto *CB = dyn_cast<CallBase>(NonCompatibleI))
          if (OMPInfoCache.RTLFunctions.contains(CB->getCalledFunction()))
            continue;

        auto Remark = [&](OptimizationRemarkAnalysis ORA) {
          ORA << "Value has potential side effects preventing SPMD-mode "
                 "execution";
          if (isa<CallBase>(NonCompatibleI)) {
            ORA << ". Add `[[omp::assume(\"ompx_spmd_amenable\")]]` to "
                   "the called function to override";
          }
          return ORA << ".";
        };
        A.emitRemark<OptimizationRemarkAnalysis>(NonCompatibleI, "OMP121",
                                                 Remark);

        LLVM_DEBUG(dbgs() << TAG << "SPMD-incompatible side-effect: "
                          << *NonCompatibleI << "\n");
      }

      return false;
    }

    // Get the actual kernel, could be the caller of the anchor scope if we have
    // a debug wrapper.
    Function *Kernel = getAnchorScope();
    if (Kernel->hasLocalLinkage()) {
      assert(Kernel->hasOneUse() && "Unexpected use of debug kernel wrapper.");
      auto *CB = cast<CallBase>(Kernel->user_back());
      Kernel = CB->getCaller();
    }
    assert(omp::isOpenMPKernel(*Kernel) && "Expected kernel function!");

    // Check if the kernel is already in SPMD mode, if so, return success.
    ConstantStruct *ExistingKernelEnvC =
        KernelInfo::getKernelEnvironementFromKernelInitCB(KernelInitCB);
    auto *ExecModeC =
        KernelInfo::getExecModeFromKernelEnvironment(ExistingKernelEnvC);
    const int8_t ExecModeVal = ExecModeC->getSExtValue();
    if (ExecModeVal != OMP_TGT_EXEC_MODE_GENERIC)
      return true;

    // We will now unconditionally modify the IR, indicate a change.
    Changed = ChangeStatus::CHANGED;

    // Do not use instruction guards when no parallel is present inside
    // the target region.
    if (mayContainParallelRegion())
      insertInstructionGuardsHelper(A);
    else
      forceSingleThreadPerWorkgroupHelper(A);

    // Adjust the global exec mode flag that tells the runtime what mode this
    // kernel is executed in.
    assert(ExecModeVal == OMP_TGT_EXEC_MODE_GENERIC &&
           "Initially non-SPMD kernel has SPMD exec mode!");
    setExecModeOfKernelEnvironment(
        ConstantInt::get(ExecModeC->getIntegerType(),
                         ExecModeVal | OMP_TGT_EXEC_MODE_GENERIC_SPMD));

    ++NumOpenMPTargetRegionKernelsSPMD;

    auto Remark = [&](OptimizationRemark OR) {
      return OR << "Transformed generic-mode kernel to SPMD-mode.";
    };
    A.emitRemark<OptimizationRemark>(KernelInitCB, "OMP120", Remark);
    return true;
  };

  bool buildCustomStateMachine(Attributor &A, ChangeStatus &Changed) {
    // If we have disabled state machine rewrites, don't make a custom one
    if (DisableOpenMPOptStateMachineRewrite)
      return false;

    // Don't rewrite the state machine if we are not in a valid state.
    if (!ReachedKnownParallelRegions.isValidState())
      return false;

    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    if (!OMPInfoCache.runtimeFnsAvailable(
            {OMPRTL___kmpc_get_hardware_num_threads_in_block,
             OMPRTL___kmpc_get_warp_size, OMPRTL___kmpc_barrier_simple_generic,
             OMPRTL___kmpc_kernel_parallel, OMPRTL___kmpc_kernel_end_parallel}))
      return false;

    ConstantStruct *ExistingKernelEnvC =
        KernelInfo::getKernelEnvironementFromKernelInitCB(KernelInitCB);

    // Check if the current configuration is non-SPMD and generic state machine.
    // If we already have SPMD mode or a custom state machine we do not need to
    // go any further. If it is anything but a constant something is weird and
    // we give up.
    ConstantInt *UseStateMachineC =
        KernelInfo::getUseGenericStateMachineFromKernelEnvironment(
            ExistingKernelEnvC);
    ConstantInt *ModeC =
        KernelInfo::getExecModeFromKernelEnvironment(ExistingKernelEnvC);

    // If we are stuck with generic mode, try to create a custom device (=GPU)
    // state machine which is specialized for the parallel regions that are
    // reachable by the kernel.
    if (UseStateMachineC->isZero() ||
        (ModeC->getSExtValue() & OMP_TGT_EXEC_MODE_SPMD))
      return false;

    Changed = ChangeStatus::CHANGED;

    // If not SPMD mode, indicate we use a custom state machine now.
    setUseGenericStateMachineOfKernelEnvironment(
        ConstantInt::get(UseStateMachineC->getIntegerType(), false));

    // If we don't actually need a state machine we are done here. This can
    // happen if there simply are no parallel regions. In the resulting kernel
    // all worker threads will simply exit right away, leaving the main thread
    // to do the work alone.
    if (!mayContainParallelRegion()) {
      ++NumOpenMPTargetRegionKernelsWithoutStateMachine;

      auto Remark = [&](OptimizationRemark OR) {
        return OR << "Removing unused state machine from generic-mode kernel.";
      };
      A.emitRemark<OptimizationRemark>(KernelInitCB, "OMP130", Remark);

      return true;
    }

    // Keep track in the statistics of our new shiny custom state machine.
    if (ReachedUnknownParallelRegions.empty()) {
      ++NumOpenMPTargetRegionKernelsCustomStateMachineWithoutFallback;

      auto Remark = [&](OptimizationRemark OR) {
        return OR << "Rewriting generic-mode kernel with a customized state "
                     "machine.";
      };
      A.emitRemark<OptimizationRemark>(KernelInitCB, "OMP131", Remark);
    } else {
      ++NumOpenMPTargetRegionKernelsCustomStateMachineWithFallback;

      auto Remark = [&](OptimizationRemarkAnalysis OR) {
        return OR << "Generic-mode kernel is executed with a customized state "
                     "machine that requires a fallback.";
      };
      A.emitRemark<OptimizationRemarkAnalysis>(KernelInitCB, "OMP132", Remark);

      // Tell the user why we ended up with a fallback.
      for (CallBase *UnknownParallelRegionCB : ReachedUnknownParallelRegions) {
        if (!UnknownParallelRegionCB)
          continue;
        auto Remark = [&](OptimizationRemarkAnalysis ORA) {
          return ORA << "Call may contain unknown parallel regions. Use "
                     << "`[[omp::assume(\"omp_no_parallelism\")]]` to "
                        "override.";
        };
        A.emitRemark<OptimizationRemarkAnalysis>(UnknownParallelRegionCB,
                                                 "OMP133", Remark);
      }
    }

    // Create all the blocks:
    //
    //                       InitCB = __kmpc_target_init(...)
    //                       BlockHwSize =
    //                         __kmpc_get_hardware_num_threads_in_block();
    //                       WarpSize = __kmpc_get_warp_size();
    //                       BlockSize = BlockHwSize - WarpSize;
    // IsWorkerCheckBB:      bool IsWorker = InitCB != -1;
    //                       if (IsWorker) {
    //                         if (InitCB >= BlockSize) return;
    // SMBeginBB:               __kmpc_barrier_simple_generic(...);
    //                         void *WorkFn;
    //                         bool Active = __kmpc_kernel_parallel(&WorkFn);
    //                         if (!WorkFn) return;
    // SMIsActiveCheckBB:       if (Active) {
    // SMIfCascadeCurrentBB:      if      (WorkFn == <ParFn0>)
    //                              ParFn0(...);
    // SMIfCascadeCurrentBB:      else if (WorkFn == <ParFn1>)
    //                              ParFn1(...);
    //                            ...
    // SMIfCascadeCurrentBB:      else
    //                              ((WorkFnTy*)WorkFn)(...);
    // SMEndParallelBB:           __kmpc_kernel_end_parallel(...);
    //                          }
    // SMDoneBB:                __kmpc_barrier_simple_generic(...);
    //                          goto SMBeginBB;
    //                       }
    // UserCodeEntryBB:      // user code
    //                       __kmpc_target_deinit(...)
    //
    auto &Ctx = getAnchorValue().getContext();
    Function *Kernel = getAssociatedFunction();
    assert(Kernel && "Expected an associated function!");

    BasicBlock *InitBB = KernelInitCB->getParent();
    BasicBlock *UserCodeEntryBB = InitBB->splitBasicBlock(
        KernelInitCB->getNextNode(), "thread.user_code.check");
    BasicBlock *IsWorkerCheckBB =
        BasicBlock::Create(Ctx, "is_worker_check", Kernel, UserCodeEntryBB);
    BasicBlock *StateMachineBeginBB = BasicBlock::Create(
        Ctx, "worker_state_machine.begin", Kernel, UserCodeEntryBB);
    BasicBlock *StateMachineFinishedBB = BasicBlock::Create(
        Ctx, "worker_state_machine.finished", Kernel, UserCodeEntryBB);
    BasicBlock *StateMachineIsActiveCheckBB = BasicBlock::Create(
        Ctx, "worker_state_machine.is_active.check", Kernel, UserCodeEntryBB);
    BasicBlock *StateMachineIfCascadeCurrentBB =
        BasicBlock::Create(Ctx, "worker_state_machine.parallel_region.check",
                           Kernel, UserCodeEntryBB);
    BasicBlock *StateMachineEndParallelBB =
        BasicBlock::Create(Ctx, "worker_state_machine.parallel_region.end",
                           Kernel, UserCodeEntryBB);
    BasicBlock *StateMachineDoneBarrierBB = BasicBlock::Create(
        Ctx, "worker_state_machine.done.barrier", Kernel, UserCodeEntryBB);
    A.registerManifestAddedBasicBlock(*InitBB);
    A.registerManifestAddedBasicBlock(*UserCodeEntryBB);
    A.registerManifestAddedBasicBlock(*IsWorkerCheckBB);
    A.registerManifestAddedBasicBlock(*StateMachineBeginBB);
    A.registerManifestAddedBasicBlock(*StateMachineFinishedBB);
    A.registerManifestAddedBasicBlock(*StateMachineIsActiveCheckBB);
    A.registerManifestAddedBasicBlock(*StateMachineIfCascadeCurrentBB);
    A.registerManifestAddedBasicBlock(*StateMachineEndParallelBB);
    A.registerManifestAddedBasicBlock(*StateMachineDoneBarrierBB);

    const DebugLoc &DLoc = KernelInitCB->getDebugLoc();
    ReturnInst::Create(Ctx, StateMachineFinishedBB)->setDebugLoc(DLoc);
    InitBB->getTerminator()->eraseFromParent();

    Instruction *IsWorker =
        ICmpInst::Create(ICmpInst::ICmp, llvm::CmpInst::ICMP_NE, KernelInitCB,
                         ConstantInt::get(KernelInitCB->getType(), -1),
                         "thread.is_worker", InitBB);
    IsWorker->setDebugLoc(DLoc);
    BranchInst::Create(IsWorkerCheckBB, UserCodeEntryBB, IsWorker, InitBB);

    Module &M = *Kernel->getParent();
    FunctionCallee BlockHwSizeFn =
        OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
            M, OMPRTL___kmpc_get_hardware_num_threads_in_block);
    FunctionCallee WarpSizeFn =
        OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
            M, OMPRTL___kmpc_get_warp_size);
    CallInst *BlockHwSize =
        CallInst::Create(BlockHwSizeFn, "block.hw_size", IsWorkerCheckBB);
    OMPInfoCache.setCallingConvention(BlockHwSizeFn, BlockHwSize);
    BlockHwSize->setDebugLoc(DLoc);
    CallInst *WarpSize =
        CallInst::Create(WarpSizeFn, "warp.size", IsWorkerCheckBB);
    OMPInfoCache.setCallingConvention(WarpSizeFn, WarpSize);
    WarpSize->setDebugLoc(DLoc);
    Instruction *BlockSize = BinaryOperator::CreateSub(
        BlockHwSize, WarpSize, "block.size", IsWorkerCheckBB);
    BlockSize->setDebugLoc(DLoc);
    Instruction *IsMainOrWorker = ICmpInst::Create(
        ICmpInst::ICmp, llvm::CmpInst::ICMP_SLT, KernelInitCB, BlockSize,
        "thread.is_main_or_worker", IsWorkerCheckBB);
    IsMainOrWorker->setDebugLoc(DLoc);
    BranchInst::Create(StateMachineBeginBB, StateMachineFinishedBB,
                       IsMainOrWorker, IsWorkerCheckBB);

    // Create local storage for the work function pointer.
    const DataLayout &DL = M.getDataLayout();
    Type *VoidPtrTy = PointerType::getUnqual(Ctx);
    Instruction *WorkFnAI =
        new AllocaInst(VoidPtrTy, DL.getAllocaAddrSpace(), nullptr,
                       "worker.work_fn.addr", Kernel->getEntryBlock().begin());
    WorkFnAI->setDebugLoc(DLoc);

    OMPInfoCache.OMPBuilder.updateToLocation(
        OpenMPIRBuilder::LocationDescription(
            IRBuilder<>::InsertPoint(StateMachineBeginBB,
                                     StateMachineBeginBB->end()),
            DLoc));

    Value *Ident = KernelInfo::getIdentFromKernelEnvironment(KernelEnvC);
    Value *GTid = KernelInitCB;

    FunctionCallee BarrierFn =
        OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
            M, OMPRTL___kmpc_barrier_simple_generic);
    CallInst *Barrier =
        CallInst::Create(BarrierFn, {Ident, GTid}, "", StateMachineBeginBB);
    OMPInfoCache.setCallingConvention(BarrierFn, Barrier);
    Barrier->setDebugLoc(DLoc);

    if (WorkFnAI->getType()->getPointerAddressSpace() !=
        (unsigned int)AddressSpace::Generic) {
      WorkFnAI = new AddrSpaceCastInst(
          WorkFnAI, PointerType::get(Ctx, (unsigned int)AddressSpace::Generic),
          WorkFnAI->getName() + ".generic", StateMachineBeginBB);
      WorkFnAI->setDebugLoc(DLoc);
    }

    FunctionCallee KernelParallelFn =
        OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
            M, OMPRTL___kmpc_kernel_parallel);
    CallInst *IsActiveWorker = CallInst::Create(
        KernelParallelFn, {WorkFnAI}, "worker.is_active", StateMachineBeginBB);
    OMPInfoCache.setCallingConvention(KernelParallelFn, IsActiveWorker);
    IsActiveWorker->setDebugLoc(DLoc);
    Instruction *WorkFn = new LoadInst(VoidPtrTy, WorkFnAI, "worker.work_fn",
                                       StateMachineBeginBB);
    WorkFn->setDebugLoc(DLoc);

    FunctionType *ParallelRegionFnTy = FunctionType::get(
        Type::getVoidTy(Ctx), {Type::getInt16Ty(Ctx), Type::getInt32Ty(Ctx)},
        false);

    Instruction *IsDone =
        ICmpInst::Create(ICmpInst::ICmp, llvm::CmpInst::ICMP_EQ, WorkFn,
                         Constant::getNullValue(VoidPtrTy), "worker.is_done",
                         StateMachineBeginBB);
    IsDone->setDebugLoc(DLoc);
    BranchInst::Create(StateMachineFinishedBB, StateMachineIsActiveCheckBB,
                       IsDone, StateMachineBeginBB)
        ->setDebugLoc(DLoc);

    BranchInst::Create(StateMachineIfCascadeCurrentBB,
                       StateMachineDoneBarrierBB, IsActiveWorker,
                       StateMachineIsActiveCheckBB)
        ->setDebugLoc(DLoc);

    Value *ZeroArg =
        Constant::getNullValue(ParallelRegionFnTy->getParamType(0));

    const unsigned int WrapperFunctionArgNo = 6;

    // Now that we have most of the CFG skeleton it is time for the if-cascade
    // that checks the function pointer we got from the runtime against the
    // parallel regions we expect, if there are any.
    for (int I = 0, E = ReachedKnownParallelRegions.size(); I < E; ++I) {
      auto *CB = ReachedKnownParallelRegions[I];
      auto *ParallelRegion = dyn_cast<Function>(
          CB->getArgOperand(WrapperFunctionArgNo)->stripPointerCasts());
      BasicBlock *PRExecuteBB = BasicBlock::Create(
          Ctx, "worker_state_machine.parallel_region.execute", Kernel,
          StateMachineEndParallelBB);
      CallInst::Create(ParallelRegion, {ZeroArg, GTid}, "", PRExecuteBB)
          ->setDebugLoc(DLoc);
      BranchInst::Create(StateMachineEndParallelBB, PRExecuteBB)
          ->setDebugLoc(DLoc);

      BasicBlock *PRNextBB =
          BasicBlock::Create(Ctx, "worker_state_machine.parallel_region.check",
                             Kernel, StateMachineEndParallelBB);
      A.registerManifestAddedBasicBlock(*PRExecuteBB);
      A.registerManifestAddedBasicBlock(*PRNextBB);

      // Check if we need to compare the pointer at all or if we can just
      // call the parallel region function.
      Value *IsPR;
      if (I + 1 < E || !ReachedUnknownParallelRegions.empty()) {
        Instruction *CmpI = ICmpInst::Create(
            ICmpInst::ICmp, llvm::CmpInst::ICMP_EQ, WorkFn, ParallelRegion,
            "worker.check_parallel_region", StateMachineIfCascadeCurrentBB);
        CmpI->setDebugLoc(DLoc);
        IsPR = CmpI;
      } else {
        IsPR = ConstantInt::getTrue(Ctx);
      }

      BranchInst::Create(PRExecuteBB, PRNextBB, IsPR,
                         StateMachineIfCascadeCurrentBB)
          ->setDebugLoc(DLoc);
      StateMachineIfCascadeCurrentBB = PRNextBB;
    }

    // At the end of the if-cascade we place the indirect function pointer call
    // in case we might need it, that is if there can be parallel regions we
    // have not handled in the if-cascade above.
    if (!ReachedUnknownParallelRegions.empty()) {
      StateMachineIfCascadeCurrentBB->setName(
          "worker_state_machine.parallel_region.fallback.execute");
      CallInst::Create(ParallelRegionFnTy, WorkFn, {ZeroArg, GTid}, "",
                       StateMachineIfCascadeCurrentBB)
          ->setDebugLoc(DLoc);
    }
    BranchInst::Create(StateMachineEndParallelBB,
                       StateMachineIfCascadeCurrentBB)
        ->setDebugLoc(DLoc);

    FunctionCallee EndParallelFn =
        OMPInfoCache.OMPBuilder.getOrCreateRuntimeFunction(
            M, OMPRTL___kmpc_kernel_end_parallel);
    CallInst *EndParallel =
        CallInst::Create(EndParallelFn, {}, "", StateMachineEndParallelBB);
    OMPInfoCache.setCallingConvention(EndParallelFn, EndParallel);
    EndParallel->setDebugLoc(DLoc);
    BranchInst::Create(StateMachineDoneBarrierBB, StateMachineEndParallelBB)
        ->setDebugLoc(DLoc);

    CallInst::Create(BarrierFn, {Ident, GTid}, "", StateMachineDoneBarrierBB)
        ->setDebugLoc(DLoc);
    BranchInst::Create(StateMachineBeginBB, StateMachineDoneBarrierBB)
        ->setDebugLoc(DLoc);

    return true;
  }

  /// Fixpoint iteration update function. Will be called every time a dependence
  /// changed its state (and in the beginning).
  ChangeStatus updateImpl(Attributor &A) override {
    KernelInfoState StateBefore = getState();

    // When we leave this function this RAII will make sure the member
    // KernelEnvC is updated properly depending on the state. That member is
    // used for simplification of values and needs to be up to date at all
    // times.
    struct UpdateKernelEnvCRAII {
      AAKernelInfoFunction &AA;

      UpdateKernelEnvCRAII(AAKernelInfoFunction &AA) : AA(AA) {}

      ~UpdateKernelEnvCRAII() {
        if (!AA.KernelEnvC)
          return;

        ConstantStruct *ExistingKernelEnvC =
            KernelInfo::getKernelEnvironementFromKernelInitCB(AA.KernelInitCB);

        if (!AA.isValidState()) {
          AA.KernelEnvC = ExistingKernelEnvC;
          return;
        }

        if (!AA.ReachedKnownParallelRegions.isValidState())
          AA.setUseGenericStateMachineOfKernelEnvironment(
              KernelInfo::getUseGenericStateMachineFromKernelEnvironment(
                  ExistingKernelEnvC));

        if (!AA.SPMDCompatibilityTracker.isValidState())
          AA.setExecModeOfKernelEnvironment(
              KernelInfo::getExecModeFromKernelEnvironment(ExistingKernelEnvC));

        ConstantInt *MayUseNestedParallelismC =
            KernelInfo::getMayUseNestedParallelismFromKernelEnvironment(
                AA.KernelEnvC);
        ConstantInt *NewMayUseNestedParallelismC = ConstantInt::get(
            MayUseNestedParallelismC->getIntegerType(), AA.NestedParallelism);
        AA.setMayUseNestedParallelismOfKernelEnvironment(
            NewMayUseNestedParallelismC);
      }
    } RAII(*this);

    // Callback to check a read/write instruction.
    auto CheckRWInst = [&](Instruction &I) {
      // We handle calls later.
      if (isa<CallBase>(I))
        return true;
      // We only care about write effects.
      if (!I.mayWriteToMemory())
        return true;
      if (auto *SI = dyn_cast<StoreInst>(&I)) {
        const auto *UnderlyingObjsAA = A.getAAFor<AAUnderlyingObjects>(
            *this, IRPosition::value(*SI->getPointerOperand()),
            DepClassTy::OPTIONAL);
        auto *HS = A.getAAFor<AAHeapToStack>(
            *this, IRPosition::function(*I.getFunction()),
            DepClassTy::OPTIONAL);
        if (UnderlyingObjsAA &&
            UnderlyingObjsAA->forallUnderlyingObjects([&](Value &Obj) {
              if (AA::isAssumedThreadLocalObject(A, Obj, *this))
                return true;
              // Check for AAHeapToStack moved objects which must not be
              // guarded.
              auto *CB = dyn_cast<CallBase>(&Obj);
              return CB && HS && HS->isAssumedHeapToStack(*CB);
            }))
          return true;
      }

      // Insert instruction that needs guarding.
      SPMDCompatibilityTracker.insert(&I);
      return true;
    };

    bool UsedAssumedInformationInCheckRWInst = false;
    if (!SPMDCompatibilityTracker.isAtFixpoint())
      if (!A.checkForAllReadWriteInstructions(
              CheckRWInst, *this, UsedAssumedInformationInCheckRWInst))
        SPMDCompatibilityTracker.indicatePessimisticFixpoint();

    bool UsedAssumedInformationFromReachingKernels = false;
    if (!IsKernelEntry) {
      updateParallelLevels(A);

      bool AllReachingKernelsKnown = true;
      updateReachingKernelEntries(A, AllReachingKernelsKnown);
      UsedAssumedInformationFromReachingKernels = !AllReachingKernelsKnown;

      if (!SPMDCompatibilityTracker.empty()) {
        if (!ParallelLevels.isValidState())
          SPMDCompatibilityTracker.indicatePessimisticFixpoint();
        else if (!ReachingKernelEntries.isValidState())
          SPMDCompatibilityTracker.indicatePessimisticFixpoint();
        else {
          // Check if all reaching kernels agree on the mode as we can otherwise
          // not guard instructions. We might not be sure about the mode so we
          // we cannot fix the internal spmd-zation state either.
          int SPMD = 0, Generic = 0;
          for (auto *Kernel : ReachingKernelEntries) {
            auto *CBAA = A.getAAFor<AAKernelInfo>(
                *this, IRPosition::function(*Kernel), DepClassTy::OPTIONAL);
            if (CBAA && CBAA->SPMDCompatibilityTracker.isValidState() &&
                CBAA->SPMDCompatibilityTracker.isAssumed())
              ++SPMD;
            else
              ++Generic;
            if (!CBAA || !CBAA->SPMDCompatibilityTracker.isAtFixpoint())
              UsedAssumedInformationFromReachingKernels = true;
          }
          if (SPMD != 0 && Generic != 0)
            SPMDCompatibilityTracker.indicatePessimisticFixpoint();
        }
      }
    }

    // Callback to check a call instruction.
    bool AllParallelRegionStatesWereFixed = true;
    bool AllSPMDStatesWereFixed = true;
    auto CheckCallInst = [&](Instruction &I) {
      auto &CB = cast<CallBase>(I);
      auto *CBAA = A.getAAFor<AAKernelInfo>(
          *this, IRPosition::callsite_function(CB), DepClassTy::OPTIONAL);
      if (!CBAA)
        return false;
      getState() ^= CBAA->getState();
      AllSPMDStatesWereFixed &= CBAA->SPMDCompatibilityTracker.isAtFixpoint();
      AllParallelRegionStatesWereFixed &=
          CBAA->ReachedKnownParallelRegions.isAtFixpoint();
      AllParallelRegionStatesWereFixed &=
          CBAA->ReachedUnknownParallelRegions.isAtFixpoint();
      return true;
    };

    bool UsedAssumedInformationInCheckCallInst = false;
    if (!A.checkForAllCallLikeInstructions(
            CheckCallInst, *this, UsedAssumedInformationInCheckCallInst)) {
      LLVM_DEBUG(dbgs() << TAG
                        << "Failed to visit all call-like instructions!\n";);
      return indicatePessimisticFixpoint();
    }

    // If we haven't used any assumed information for the reached parallel
    // region states we can fix it.
    if (!UsedAssumedInformationInCheckCallInst &&
        AllParallelRegionStatesWereFixed) {
      ReachedKnownParallelRegions.indicateOptimisticFixpoint();
      ReachedUnknownParallelRegions.indicateOptimisticFixpoint();
    }

    // If we haven't used any assumed information for the SPMD state we can fix
    // it.
    if (!UsedAssumedInformationInCheckRWInst &&
        !UsedAssumedInformationInCheckCallInst &&
        !UsedAssumedInformationFromReachingKernels && AllSPMDStatesWereFixed)
      SPMDCompatibilityTracker.indicateOptimisticFixpoint();

    return StateBefore == getState() ? ChangeStatus::UNCHANGED
                                     : ChangeStatus::CHANGED;
  }

private:
  /// Update info regarding reaching kernels.
  void updateReachingKernelEntries(Attributor &A,
                                   bool &AllReachingKernelsKnown) {
    auto PredCallSite = [&](AbstractCallSite ACS) {
      Function *Caller = ACS.getInstruction()->getFunction();

      assert(Caller && "Caller is nullptr");

      auto *CAA = A.getOrCreateAAFor<AAKernelInfo>(
          IRPosition::function(*Caller), this, DepClassTy::REQUIRED);
      if (CAA && CAA->ReachingKernelEntries.isValidState()) {
        ReachingKernelEntries ^= CAA->ReachingKernelEntries;
        return true;
      }

      // We lost track of the caller of the associated function, any kernel
      // could reach now.
      ReachingKernelEntries.indicatePessimisticFixpoint();

      return true;
    };

    if (!A.checkForAllCallSites(PredCallSite, *this,
                                true /* RequireAllCallSites */,
                                AllReachingKernelsKnown))
      ReachingKernelEntries.indicatePessimisticFixpoint();
  }

  /// Update info regarding parallel levels.
  void updateParallelLevels(Attributor &A) {
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    OMPInformationCache::RuntimeFunctionInfo &Parallel51RFI =
        OMPInfoCache.RFIs[OMPRTL___kmpc_parallel_51];

    auto PredCallSite = [&](AbstractCallSite ACS) {
      Function *Caller = ACS.getInstruction()->getFunction();

      assert(Caller && "Caller is nullptr");

      auto *CAA =
          A.getOrCreateAAFor<AAKernelInfo>(IRPosition::function(*Caller));
      if (CAA && CAA->ParallelLevels.isValidState()) {
        // Any function that is called by `__kmpc_parallel_51` will not be
        // folded as the parallel level in the function is updated. In order to
        // get it right, all the analysis would depend on the implentation. That
        // said, if in the future any change to the implementation, the analysis
        // could be wrong. As a consequence, we are just conservative here.
        if (Caller == Parallel51RFI.Declaration) {
          ParallelLevels.indicatePessimisticFixpoint();
          return true;
        }

        ParallelLevels ^= CAA->ParallelLevels;

        return true;
      }

      // We lost track of the caller of the associated function, any kernel
      // could reach now.
      ParallelLevels.indicatePessimisticFixpoint();

      return true;
    };

    bool AllCallSitesKnown = true;
    if (!A.checkForAllCallSites(PredCallSite, *this,
                                true /* RequireAllCallSites */,
                                AllCallSitesKnown))
      ParallelLevels.indicatePessimisticFixpoint();
  }
};

/// The call site kernel info abstract attribute, basically, what can we say
/// about a call site with regards to the KernelInfoState. For now this simply
/// forwards the information from the callee.
struct AAKernelInfoCallSite : AAKernelInfo {
  AAKernelInfoCallSite(const IRPosition &IRP, Attributor &A)
      : AAKernelInfo(IRP, A) {}

  /// See AbstractAttribute::initialize(...).
  void initialize(Attributor &A) override {
    AAKernelInfo::initialize(A);

    CallBase &CB = cast<CallBase>(getAssociatedValue());
    auto *AssumptionAA = A.getAAFor<AAAssumptionInfo>(
        *this, IRPosition::callsite_function(CB), DepClassTy::OPTIONAL);

    // Check for SPMD-mode assumptions.
    if (AssumptionAA && AssumptionAA->hasAssumption("ompx_spmd_amenable")) {
      indicateOptimisticFixpoint();
      return;
    }

    // First weed out calls we do not care about, that is readonly/readnone
    // calls, intrinsics, and "no_openmp" calls. Neither of these can reach a
    // parallel region or anything else we are looking for.
    if (!CB.mayWriteToMemory() || isa<IntrinsicInst>(CB)) {
      indicateOptimisticFixpoint();
      return;
    }

    // Next we check if we know the callee. If it is a known OpenMP function
    // we will handle them explicitly in the switch below. If it is not, we
    // will use an AAKernelInfo object on the callee to gather information and
    // merge that into the current state. The latter happens in the updateImpl.
    auto CheckCallee = [&](Function *Callee, unsigned NumCallees) {
      auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
      const auto &It = OMPInfoCache.RuntimeFunctionIDMap.find(Callee);
      if (It == OMPInfoCache.RuntimeFunctionIDMap.end()) {
        // Unknown caller or declarations are not analyzable, we give up.
        if (!Callee || !A.isFunctionIPOAmendable(*Callee)) {

          // Unknown callees might contain parallel regions, except if they have
          // an appropriate assumption attached.
          if (!AssumptionAA ||
              !(AssumptionAA->hasAssumption("omp_no_openmp") ||
                AssumptionAA->hasAssumption("omp_no_parallelism")))
            ReachedUnknownParallelRegions.insert(&CB);

          // If SPMDCompatibilityTracker is not fixed, we need to give up on the
          // idea we can run something unknown in SPMD-mode.
          if (!SPMDCompatibilityTracker.isAtFixpoint()) {
            SPMDCompatibilityTracker.indicatePessimisticFixpoint();
            SPMDCompatibilityTracker.insert(&CB);
          }

          // We have updated the state for this unknown call properly, there
          // won't be any change so we indicate a fixpoint.
          indicateOptimisticFixpoint();
        }
        // If the callee is known and can be used in IPO, we will update the
        // state based on the callee state in updateImpl.
        return;
      }
      if (NumCallees > 1) {
        indicatePessimisticFixpoint();
        return;
      }

      RuntimeFunction RF = It->getSecond();
      switch (RF) {
      // All the functions we know are compatible with SPMD mode.
      case OMPRTL___kmpc_is_spmd_exec_mode:
      case OMPRTL___kmpc_distribute_static_fini:
      case OMPRTL___kmpc_for_static_fini:
      case OMPRTL___kmpc_global_thread_num:
      case OMPRTL___kmpc_get_hardware_num_threads_in_block:
      case OMPRTL___kmpc_get_hardware_num_blocks:
      case OMPRTL___kmpc_single:
      case OMPRTL___kmpc_end_single:
      case OMPRTL___kmpc_master:
      case OMPRTL___kmpc_end_master:
      case OMPRTL___kmpc_barrier:
      case OMPRTL___kmpc_nvptx_parallel_reduce_nowait_v2:
      case OMPRTL___kmpc_nvptx_teams_reduce_nowait_v2:
      case OMPRTL___kmpc_error:
      case OMPRTL___kmpc_flush:
      case OMPRTL___kmpc_get_hardware_thread_id_in_block:
      case OMPRTL___kmpc_get_warp_size:
      case OMPRTL_omp_get_thread_num:
      case OMPRTL_omp_get_num_threads:
      case OMPRTL_omp_get_max_threads:
      case OMPRTL_omp_in_parallel:
      case OMPRTL_omp_get_dynamic:
      case OMPRTL_omp_get_cancellation:
      case OMPRTL_omp_get_nested:
      case OMPRTL_omp_get_schedule:
      case OMPRTL_omp_get_thread_limit:
      case OMPRTL_omp_get_supported_active_levels:
      case OMPRTL_omp_get_max_active_levels:
      case OMPRTL_omp_get_level:
      case OMPRTL_omp_get_ancestor_thread_num:
      case OMPRTL_omp_get_team_size:
      case OMPRTL_omp_get_active_level:
      case OMPRTL_omp_in_final:
      case OMPRTL_omp_get_proc_bind:
      case OMPRTL_omp_get_num_places:
      case OMPRTL_omp_get_num_procs:
      case OMPRTL_omp_get_place_proc_ids:
      case OMPRTL_omp_get_place_num:
      case OMPRTL_omp_get_partition_num_places:
      case OMPRTL_omp_get_partition_place_nums:
      case OMPRTL_omp_get_wtime:
        break;
      case OMPRTL___kmpc_distribute_static_init_4:
      case OMPRTL___kmpc_distribute_static_init_4u:
      case OMPRTL___kmpc_distribute_static_init_8:
      case OMPRTL___kmpc_distribute_static_init_8u:
      case OMPRTL___kmpc_for_static_init_4:
      case OMPRTL___kmpc_for_static_init_4u:
      case OMPRTL___kmpc_for_static_init_8:
      case OMPRTL___kmpc_for_static_init_8u: {
        // Check the schedule and allow static schedule in SPMD mode.
        unsigned ScheduleArgOpNo = 2;
        auto *ScheduleTypeCI =
            dyn_cast<ConstantInt>(CB.getArgOperand(ScheduleArgOpNo));
        unsigned ScheduleTypeVal =
            ScheduleTypeCI ? ScheduleTypeCI->getZExtValue() : 0;
        switch (OMPScheduleType(ScheduleTypeVal)) {
        case OMPScheduleType::UnorderedStatic:
        case OMPScheduleType::UnorderedStaticChunked:
        case OMPScheduleType::OrderedDistribute:
        case OMPScheduleType::OrderedDistributeChunked:
          break;
        default:
          SPMDCompatibilityTracker.indicatePessimisticFixpoint();
          SPMDCompatibilityTracker.insert(&CB);
          break;
        };
      } break;
      case OMPRTL___kmpc_target_init:
        KernelInitCB = &CB;
        break;
      case OMPRTL___kmpc_target_deinit:
        KernelDeinitCB = &CB;
        break;
      case OMPRTL___kmpc_parallel_51:
        if (!handleParallel51(A, CB))
          indicatePessimisticFixpoint();
        return;
      case OMPRTL___kmpc_omp_task:
        // We do not look into tasks right now, just give up.
        SPMDCompatibilityTracker.indicatePessimisticFixpoint();
        SPMDCompatibilityTracker.insert(&CB);
        ReachedUnknownParallelRegions.insert(&CB);
        break;
      case OMPRTL___kmpc_alloc_shared:
      case OMPRTL___kmpc_free_shared:
        // Return without setting a fixpoint, to be resolved in updateImpl.
        return;
      default:
        // Unknown OpenMP runtime calls cannot be executed in SPMD-mode,
        // generally. However, they do not hide parallel regions.
        SPMDCompatibilityTracker.indicatePessimisticFixpoint();
        SPMDCompatibilityTracker.insert(&CB);
        break;
      }
      // All other OpenMP runtime calls will not reach parallel regions so they
      // can be safely ignored for now. Since it is a known OpenMP runtime call
      // we have now modeled all effects and there is no need for any update.
      indicateOptimisticFixpoint();
    };

    const auto *AACE =
        A.getAAFor<AACallEdges>(*this, getIRPosition(), DepClassTy::OPTIONAL);
    if (!AACE || !AACE->getState().isValidState() || AACE->hasUnknownCallee()) {
      CheckCallee(getAssociatedFunction(), 1);
      return;
    }
    const auto &OptimisticEdges = AACE->getOptimisticEdges();
    for (auto *Callee : OptimisticEdges) {
      CheckCallee(Callee, OptimisticEdges.size());
      if (isAtFixpoint())
        break;
    }
  }

  ChangeStatus updateImpl(Attributor &A) override {
    // TODO: Once we have call site specific value information we can provide
    //       call site specific liveness information and then it makes
    //       sense to specialize attributes for call sites arguments instead of
    //       redirecting requests to the callee argument.
    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    KernelInfoState StateBefore = getState();

    auto CheckCallee = [&](Function *F, int NumCallees) {
      const auto &It = OMPInfoCache.RuntimeFunctionIDMap.find(F);

      // If F is not a runtime function, propagate the AAKernelInfo of the
      // callee.
      if (It == OMPInfoCache.RuntimeFunctionIDMap.end()) {
        const IRPosition &FnPos = IRPosition::function(*F);
        auto *FnAA =
            A.getAAFor<AAKernelInfo>(*this, FnPos, DepClassTy::REQUIRED);
        if (!FnAA)
          return indicatePessimisticFixpoint();
        if (getState() == FnAA->getState())
          return ChangeStatus::UNCHANGED;
        getState() = FnAA->getState();
        return ChangeStatus::CHANGED;
      }
      if (NumCallees > 1)
        return indicatePessimisticFixpoint();

      CallBase &CB = cast<CallBase>(getAssociatedValue());
      if (It->getSecond() == OMPRTL___kmpc_parallel_51) {
        if (!handleParallel51(A, CB))
          return indicatePessimisticFixpoint();
        return StateBefore == getState() ? ChangeStatus::UNCHANGED
                                         : ChangeStatus::CHANGED;
      }

      // F is a runtime function that allocates or frees memory, check
      // AAHeapToStack and AAHeapToShared.
      assert(
          (It->getSecond() == OMPRTL___kmpc_alloc_shared ||
           It->getSecond() == OMPRTL___kmpc_free_shared) &&
          "Expected a __kmpc_alloc_shared or __kmpc_free_shared runtime call");

      auto *HeapToStackAA = A.getAAFor<AAHeapToStack>(
          *this, IRPosition::function(*CB.getCaller()), DepClassTy::OPTIONAL);
      auto *HeapToSharedAA = A.getAAFor<AAHeapToShared>(
          *this, IRPosition::function(*CB.getCaller()), DepClassTy::OPTIONAL);

      RuntimeFunction RF = It->getSecond();

      switch (RF) {
      // If neither HeapToStack nor HeapToShared assume the call is removed,
      // assume SPMD incompatibility.
      case OMPRTL___kmpc_alloc_shared:
        if ((!HeapToStackAA || !HeapToStackAA->isAssumedHeapToStack(CB)) &&
            (!HeapToSharedAA || !HeapToSharedAA->isAssumedHeapToShared(CB)))
          SPMDCompatibilityTracker.insert(&CB);
        break;
      case OMPRTL___kmpc_free_shared:
        if ((!HeapToStackAA ||
             !HeapToStackAA->isAssumedHeapToStackRemovedFree(CB)) &&
            (!HeapToSharedAA ||
             !HeapToSharedAA->isAssumedHeapToSharedRemovedFree(CB)))
          SPMDCompatibilityTracker.insert(&CB);
        break;
      default:
        SPMDCompatibilityTracker.indicatePessimisticFixpoint();
        SPMDCompatibilityTracker.insert(&CB);
      }
      return ChangeStatus::CHANGED;
    };

    const auto *AACE =
        A.getAAFor<AACallEdges>(*this, getIRPosition(), DepClassTy::OPTIONAL);
    if (!AACE || !AACE->getState().isValidState() || AACE->hasUnknownCallee()) {
      if (Function *F = getAssociatedFunction())
        CheckCallee(F, /*NumCallees=*/1);
    } else {
      const auto &OptimisticEdges = AACE->getOptimisticEdges();
      for (auto *Callee : OptimisticEdges) {
        CheckCallee(Callee, OptimisticEdges.size());
        if (isAtFixpoint())
          break;
      }
    }

    return StateBefore == getState() ? ChangeStatus::UNCHANGED
                                     : ChangeStatus::CHANGED;
  }

  /// Deal with a __kmpc_parallel_51 call (\p CB). Returns true if the call was
  /// handled, if a problem occurred, false is returned.
  bool handleParallel51(Attributor &A, CallBase &CB) {
    const unsigned int NonWrapperFunctionArgNo = 5;
    const unsigned int WrapperFunctionArgNo = 6;
    auto ParallelRegionOpArgNo = SPMDCompatibilityTracker.isAssumed()
                                     ? NonWrapperFunctionArgNo
                                     : WrapperFunctionArgNo;

    auto *ParallelRegion = dyn_cast<Function>(
        CB.getArgOperand(ParallelRegionOpArgNo)->stripPointerCasts());
    if (!ParallelRegion)
      return false;

    ReachedKnownParallelRegions.insert(&CB);
    /// Check nested parallelism
    auto *FnAA = A.getAAFor<AAKernelInfo>(
        *this, IRPosition::function(*ParallelRegion), DepClassTy::OPTIONAL);
    NestedParallelism |= !FnAA || !FnAA->getState().isValidState() ||
                         !FnAA->ReachedKnownParallelRegions.empty() ||
                         !FnAA->ReachedKnownParallelRegions.isValidState() ||
                         !FnAA->ReachedUnknownParallelRegions.isValidState() ||
                         !FnAA->ReachedUnknownParallelRegions.empty();
    return true;
  }
};

struct AAFoldRuntimeCall
    : public StateWrapper<BooleanState, AbstractAttribute> {
  using Base = StateWrapper<BooleanState, AbstractAttribute>;

  AAFoldRuntimeCall(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Statistics are tracked as part of manifest for now.
  void trackStatistics() const override {}

  /// Create an abstract attribute biew for the position \p IRP.
  static AAFoldRuntimeCall &createForPosition(const IRPosition &IRP,
                                              Attributor &A);

  /// See AbstractAttribute::getName()
  const std::string getName() const override { return "AAFoldRuntimeCall"; }

  /// See AbstractAttribute::getIdAddr()
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAFoldRuntimeCall
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  static const char ID;
};

struct AAFoldRuntimeCallCallSiteReturned : AAFoldRuntimeCall {
  AAFoldRuntimeCallCallSiteReturned(const IRPosition &IRP, Attributor &A)
      : AAFoldRuntimeCall(IRP, A) {}

  /// See AbstractAttribute::getAsStr()
  const std::string getAsStr(Attributor *) const override {
    if (!isValidState())
      return "<invalid>";

    std::string Str("simplified value: ");

    if (!SimplifiedValue)
      return Str + std::string("none");

    if (!*SimplifiedValue)
      return Str + std::string("nullptr");

    if (ConstantInt *CI = dyn_cast<ConstantInt>(*SimplifiedValue))
      return Str + std::to_string(CI->getSExtValue());

    return Str + std::string("unknown");
  }

  void initialize(Attributor &A) override {
    if (DisableOpenMPOptFolding)
      indicatePessimisticFixpoint();

    Function *Callee = getAssociatedFunction();

    auto &OMPInfoCache = static_cast<OMPInformationCache &>(A.getInfoCache());
    const auto &It = OMPInfoCache.RuntimeFunctionIDMap.find(Callee);
    assert(It != OMPInfoCache.RuntimeFunctionIDMap.end() &&
           "Expected a known OpenMP runtime function");

    RFKind = It->getSecond();

    CallBase &CB = cast<CallBase>(getAssociatedValue());
    A.registerSimplificationCallback(
        IRPosition::callsite_returned(CB),
        [&](const IRPosition &IRP, const AbstractAttribute *AA,
            bool &UsedAssumedInformation) -> std::optional<Value *> {
          assert((isValidState() ||
                  (SimplifiedValue && *SimplifiedValue == nullptr)) &&
                 "Unexpected invalid state!");

          if (!isAtFixpoint()) {
            UsedAssumedInformation = true;
            if (AA)
              A.recordDependence(*this, *AA, DepClassTy::OPTIONAL);
          }
          return SimplifiedValue;
        });
  }

  ChangeStatus updateImpl(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;
    switch (RFKind) {
    case OMPRTL___kmpc_is_spmd_exec_mode:
      Changed |= foldIsSPMDExecMode(A);
      break;
    case OMPRTL___kmpc_parallel_level:
      Changed |= foldParallelLevel(A);
      break;
    case OMPRTL___kmpc_get_hardware_num_threads_in_block:
      Changed = Changed | foldKernelFnAttribute(A, "omp_target_thread_limit");
      break;
    case OMPRTL___kmpc_get_hardware_num_blocks:
      Changed = Changed | foldKernelFnAttribute(A, "omp_target_num_teams");
      break;
    default:
      llvm_unreachable("Unhandled OpenMP runtime function!");
    }

    return Changed;
  }

  ChangeStatus manifest(Attributor &A) override {
    ChangeStatus Changed = ChangeStatus::UNCHANGED;

    if (SimplifiedValue && *SimplifiedValue) {
      Instruction &I = *getCtxI();
      A.changeAfterManifest(IRPosition::inst(I), **SimplifiedValue);
      A.deleteAfterManifest(I);

      CallBase *CB = dyn_cast<CallBase>(&I);
      auto Remark = [&](OptimizationRemark OR) {
        if (auto *C = dyn_cast<ConstantInt>(*SimplifiedValue))
          return OR << "Replacing OpenMP runtime call "
                    << CB->getCalledFunction()->getName() << " with "
                    << ore::NV("FoldedValue", C->getZExtValue()) << ".";
        return OR << "Replacing OpenMP runtime call "
                  << CB->getCalledFunction()->getName() << ".";
      };

      if (CB && EnableVerboseRemarks)
        A.emitRemark<OptimizationRemark>(CB, "OMP180", Remark);

      LLVM_DEBUG(dbgs() << TAG << "Replacing runtime call: " << I << " with "
                        << **SimplifiedValue << "\n");

      Changed = ChangeStatus::CHANGED;
    }

    return Changed;
  }

  ChangeStatus indicatePessimisticFixpoint() override {
    SimplifiedValue = nullptr;
    return AAFoldRuntimeCall::indicatePessimisticFixpoint();
  }

private:
  /// Fold __kmpc_is_spmd_exec_mode into a constant if possible.
  ChangeStatus foldIsSPMDExecMode(Attributor &A) {
    std::optional<Value *> SimplifiedValueBefore = SimplifiedValue;

    unsigned AssumedSPMDCount = 0, KnownSPMDCount = 0;
    unsigned AssumedNonSPMDCount = 0, KnownNonSPMDCount = 0;
    auto *CallerKernelInfoAA = A.getAAFor<AAKernelInfo>(
        *this, IRPosition::function(*getAnchorScope()), DepClassTy::REQUIRED);

    if (!CallerKernelInfoAA ||
        !CallerKernelInfoAA->ReachingKernelEntries.isValidState())
      return indicatePessimisticFixpoint();

    for (Kernel K : CallerKernelInfoAA->ReachingKernelEntries) {
      auto *AA = A.getAAFor<AAKernelInfo>(*this, IRPosition::function(*K),
                                          DepClassTy::REQUIRED);

      if (!AA || !AA->isValidState()) {
        SimplifiedValue = nullptr;
        return indicatePessimisticFixpoint();
      }

      if (AA->SPMDCompatibilityTracker.isAssumed()) {
        if (AA->SPMDCompatibilityTracker.isAtFixpoint())
          ++KnownSPMDCount;
        else
          ++AssumedSPMDCount;
      } else {
        if (AA->SPMDCompatibilityTracker.isAtFixpoint())
          ++KnownNonSPMDCount;
        else
          ++AssumedNonSPMDCount;
      }
    }

    if ((AssumedSPMDCount + KnownSPMDCount) &&
        (AssumedNonSPMDCount + KnownNonSPMDCount))
      return indicatePessimisticFixpoint();

    auto &Ctx = getAnchorValue().getContext();
    if (KnownSPMDCount || AssumedSPMDCount) {
      assert(KnownNonSPMDCount == 0 && AssumedNonSPMDCount == 0 &&
             "Expected only SPMD kernels!");
      // All reaching kernels are in SPMD mode. Update all function calls to
      // __kmpc_is_spmd_exec_mode to 1.
      SimplifiedValue = ConstantInt::get(Type::getInt8Ty(Ctx), true);
    } else if (KnownNonSPMDCount || AssumedNonSPMDCount) {
      assert(KnownSPMDCount == 0 && AssumedSPMDCount == 0 &&
             "Expected only non-SPMD kernels!");
      // All reaching kernels are in non-SPMD mode. Update all function
      // calls to __kmpc_is_spmd_exec_mode to 0.
      SimplifiedValue = ConstantInt::get(Type::getInt8Ty(Ctx), false);
    } else {
      // We have empty reaching kernels, therefore we cannot tell if the
      // associated call site can be folded. At this moment, SimplifiedValue
      // must be none.
      assert(!SimplifiedValue && "SimplifiedValue should be none");
    }

    return SimplifiedValue == SimplifiedValueBefore ? ChangeStatus::UNCHANGED
                                                    : ChangeStatus::CHANGED;
  }

  /// Fold __kmpc_parallel_level into a constant if possible.
  ChangeStatus foldParallelLevel(Attributor &A) {
    std::optional<Value *> SimplifiedValueBefore = SimplifiedValue;

    auto *CallerKernelInfoAA = A.getAAFor<AAKernelInfo>(
        *this, IRPosition::function(*getAnchorScope()), DepClassTy::REQUIRED);

    if (!CallerKernelInfoAA ||
        !CallerKernelInfoAA->ParallelLevels.isValidState())
      return indicatePessimisticFixpoint();

    if (!CallerKernelInfoAA->ReachingKernelEntries.isValidState())
      return indicatePessimisticFixpoint();

    if (CallerKernelInfoAA->ReachingKernelEntries.empty()) {
      assert(!SimplifiedValue &&
             "SimplifiedValue should keep none at this point");
      return ChangeStatus::UNCHANGED;
    }

    unsigned AssumedSPMDCount = 0, KnownSPMDCount = 0;
    unsigned AssumedNonSPMDCount = 0, KnownNonSPMDCount = 0;
    for (Kernel K : CallerKernelInfoAA->ReachingKernelEntries) {
      auto *AA = A.getAAFor<AAKernelInfo>(*this, IRPosition::function(*K),
                                          DepClassTy::REQUIRED);
      if (!AA || !AA->SPMDCompatibilityTracker.isValidState())
        return indicatePessimisticFixpoint();

      if (AA->SPMDCompatibilityTracker.isAssumed()) {
        if (AA->SPMDCompatibilityTracker.isAtFixpoint())
          ++KnownSPMDCount;
        else
          ++AssumedSPMDCount;
      } else {
        if (AA->SPMDCompatibilityTracker.isAtFixpoint())
          ++KnownNonSPMDCount;
        else
          ++AssumedNonSPMDCount;
      }
    }

    if ((AssumedSPMDCount + KnownSPMDCount) &&
        (AssumedNonSPMDCount + KnownNonSPMDCount))
      return indicatePessimisticFixpoint();

    auto &Ctx = getAnchorValue().getContext();
    // If the caller can only be reached by SPMD kernel entries, the parallel
    // level is 1. Similarly, if the caller can only be reached by non-SPMD
    // kernel entries, it is 0.
    if (AssumedSPMDCount || KnownSPMDCount) {
      assert(KnownNonSPMDCount == 0 && AssumedNonSPMDCount == 0 &&
             "Expected only SPMD kernels!");
      SimplifiedValue = ConstantInt::get(Type::getInt8Ty(Ctx), 1);
    } else {
      assert(KnownSPMDCount == 0 && AssumedSPMDCount == 0 &&
             "Expected only non-SPMD kernels!");
      SimplifiedValue = ConstantInt::get(Type::getInt8Ty(Ctx), 0);
    }
    return SimplifiedValue == SimplifiedValueBefore ? ChangeStatus::UNCHANGED
                                                    : ChangeStatus::CHANGED;
  }

  ChangeStatus foldKernelFnAttribute(Attributor &A, llvm::StringRef Attr) {
    // Specialize only if all the calls agree with the attribute constant value
    int32_t CurrentAttrValue = -1;
    std::optional<Value *> SimplifiedValueBefore = SimplifiedValue;

    auto *CallerKernelInfoAA = A.getAAFor<AAKernelInfo>(
        *this, IRPosition::function(*getAnchorScope()), DepClassTy::REQUIRED);

    if (!CallerKernelInfoAA ||
        !CallerKernelInfoAA->ReachingKernelEntries.isValidState())
      return indicatePessimisticFixpoint();

    // Iterate over the kernels that reach this function
    for (Kernel K : CallerKernelInfoAA->ReachingKernelEntries) {
      int32_t NextAttrVal = K->getFnAttributeAsParsedInteger(Attr, -1);

      if (NextAttrVal == -1 ||
          (CurrentAttrValue != -1 && CurrentAttrValue != NextAttrVal))
        return indicatePessimisticFixpoint();
      CurrentAttrValue = NextAttrVal;
    }

    if (CurrentAttrValue != -1) {
      auto &Ctx = getAnchorValue().getContext();
      SimplifiedValue =
          ConstantInt::get(Type::getInt32Ty(Ctx), CurrentAttrValue);
    }
    return SimplifiedValue == SimplifiedValueBefore ? ChangeStatus::UNCHANGED
                                                    : ChangeStatus::CHANGED;
  }

  /// An optional value the associated value is assumed to fold to. That is, we
  /// assume the associated value (which is a call) can be replaced by this
  /// simplified value.
  std::optional<Value *> SimplifiedValue;

  /// The runtime function kind of the callee of the associated call site.
  RuntimeFunction RFKind;
};

} // namespace

/// Register folding callsite
void OpenMPOpt::registerFoldRuntimeCall(RuntimeFunction RF) {
  auto &RFI = OMPInfoCache.RFIs[RF];
  RFI.foreachUse(SCC, [&](Use &U, Function &F) {
    CallInst *CI = OpenMPOpt::getCallIfRegularCall(U, &RFI);
    if (!CI)
      return false;
    A.getOrCreateAAFor<AAFoldRuntimeCall>(
        IRPosition::callsite_returned(*CI), /* QueryingAA */ nullptr,
        DepClassTy::NONE, /* ForceUpdate */ false,
        /* UpdateAfterInit */ false);
    return false;
  });
}

void OpenMPOpt::registerAAs(bool IsModulePass) {
  if (SCC.empty())
    return;

  if (IsModulePass) {
    // Ensure we create the AAKernelInfo AAs first and without triggering an
    // update. This will make sure we register all value simplification
    // callbacks before any other AA has the chance to create an AAValueSimplify
    // or similar.
    auto CreateKernelInfoCB = [&](Use &, Function &Kernel) {
      A.getOrCreateAAFor<AAKernelInfo>(
          IRPosition::function(Kernel), /* QueryingAA */ nullptr,
          DepClassTy::NONE, /* ForceUpdate */ false,
          /* UpdateAfterInit */ false);
      return false;
    };
    OMPInformationCache::RuntimeFunctionInfo &InitRFI =
        OMPInfoCache.RFIs[OMPRTL___kmpc_target_init];
    InitRFI.foreachUse(SCC, CreateKernelInfoCB);

    registerFoldRuntimeCall(OMPRTL___kmpc_is_spmd_exec_mode);
    registerFoldRuntimeCall(OMPRTL___kmpc_parallel_level);
    registerFoldRuntimeCall(OMPRTL___kmpc_get_hardware_num_threads_in_block);
    registerFoldRuntimeCall(OMPRTL___kmpc_get_hardware_num_blocks);
  }

  // Create CallSite AA for all Getters.
  if (DeduceICVValues) {
    for (int Idx = 0; Idx < OMPInfoCache.ICVs.size() - 1; ++Idx) {
      auto ICVInfo = OMPInfoCache.ICVs[static_cast<InternalControlVar>(Idx)];

      auto &GetterRFI = OMPInfoCache.RFIs[ICVInfo.Getter];

      auto CreateAA = [&](Use &U, Function &Caller) {
        CallInst *CI = OpenMPOpt::getCallIfRegularCall(U, &GetterRFI);
        if (!CI)
          return false;

        auto &CB = cast<CallBase>(*CI);

        IRPosition CBPos = IRPosition::callsite_function(CB);
        A.getOrCreateAAFor<AAICVTracker>(CBPos);
        return false;
      };

      GetterRFI.foreachUse(SCC, CreateAA);
    }
  }

  // Create an ExecutionDomain AA for every function and a HeapToStack AA for
  // every function if there is a device kernel.
  if (!isOpenMPDevice(M))
    return;

  for (auto *F : SCC) {
    if (F->isDeclaration())
      continue;

    // We look at internal functions only on-demand but if any use is not a
    // direct call or outside the current set of analyzed functions, we have
    // to do it eagerly.
    if (F->hasLocalLinkage()) {
      if (llvm::all_of(F->uses(), [this](const Use &U) {
            const auto *CB = dyn_cast<CallBase>(U.getUser());
            return CB && CB->isCallee(&U) &&
                   A.isRunOn(const_cast<Function *>(CB->getCaller()));
          }))
        continue;
    }
    registerAAsForFunction(A, *F);
  }
}

void OpenMPOpt::registerAAsForFunction(Attributor &A, const Function &F) {
  if (!DisableOpenMPOptDeglobalization)
    A.getOrCreateAAFor<AAHeapToShared>(IRPosition::function(F));
  A.getOrCreateAAFor<AAExecutionDomain>(IRPosition::function(F));
  if (!DisableOpenMPOptDeglobalization)
    A.getOrCreateAAFor<AAHeapToStack>(IRPosition::function(F));
  if (F.hasFnAttribute(Attribute::Convergent))
    A.getOrCreateAAFor<AANonConvergent>(IRPosition::function(F));

  for (auto &I : instructions(F)) {
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      bool UsedAssumedInformation = false;
      A.getAssumedSimplified(IRPosition::value(*LI), /* AA */ nullptr,
                             UsedAssumedInformation, AA::Interprocedural);
      continue;
    }
    if (auto *CI = dyn_cast<CallBase>(&I)) {
      if (CI->isIndirectCall())
        A.getOrCreateAAFor<AAIndirectCallInfo>(
            IRPosition::callsite_function(*CI));
    }
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      A.getOrCreateAAFor<AAIsDead>(IRPosition::value(*SI));
      continue;
    }
    if (auto *FI = dyn_cast<FenceInst>(&I)) {
      A.getOrCreateAAFor<AAIsDead>(IRPosition::value(*FI));
      continue;
    }
    if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
      if (II->getIntrinsicID() == Intrinsic::assume) {
        A.getOrCreateAAFor<AAPotentialValues>(
            IRPosition::value(*II->getArgOperand(0)));
        continue;
      }
    }
  }
}

const char AAICVTracker::ID = 0;
const char AAKernelInfo::ID = 0;
const char AAExecutionDomain::ID = 0;
const char AAHeapToShared::ID = 0;
const char AAFoldRuntimeCall::ID = 0;

AAICVTracker &AAICVTracker::createForPosition(const IRPosition &IRP,
                                              Attributor &A) {
  AAICVTracker *AA = nullptr;
  switch (IRP.getPositionKind()) {
  case IRPosition::IRP_INVALID:
  case IRPosition::IRP_FLOAT:
  case IRPosition::IRP_ARGUMENT:
  case IRPosition::IRP_CALL_SITE_ARGUMENT:
    llvm_unreachable("ICVTracker can only be created for function position!");
  case IRPosition::IRP_RETURNED:
    AA = new (A.Allocator) AAICVTrackerFunctionReturned(IRP, A);
    break;
  case IRPosition::IRP_CALL_SITE_RETURNED:
    AA = new (A.Allocator) AAICVTrackerCallSiteReturned(IRP, A);
    break;
  case IRPosition::IRP_CALL_SITE:
    AA = new (A.Allocator) AAICVTrackerCallSite(IRP, A);
    break;
  case IRPosition::IRP_FUNCTION:
    AA = new (A.Allocator) AAICVTrackerFunction(IRP, A);
    break;
  }

  return *AA;
}

AAExecutionDomain &AAExecutionDomain::createForPosition(const IRPosition &IRP,
                                                        Attributor &A) {
  AAExecutionDomainFunction *AA = nullptr;
  switch (IRP.getPositionKind()) {
  case IRPosition::IRP_INVALID:
  case IRPosition::IRP_FLOAT:
  case IRPosition::IRP_ARGUMENT:
  case IRPosition::IRP_CALL_SITE_ARGUMENT:
  case IRPosition::IRP_RETURNED:
  case IRPosition::IRP_CALL_SITE_RETURNED:
  case IRPosition::IRP_CALL_SITE:
    llvm_unreachable(
        "AAExecutionDomain can only be created for function position!");
  case IRPosition::IRP_FUNCTION:
    AA = new (A.Allocator) AAExecutionDomainFunction(IRP, A);
    break;
  }

  return *AA;
}

AAHeapToShared &AAHeapToShared::createForPosition(const IRPosition &IRP,
                                                  Attributor &A) {
  AAHeapToSharedFunction *AA = nullptr;
  switch (IRP.getPositionKind()) {
  case IRPosition::IRP_INVALID:
  case IRPosition::IRP_FLOAT:
  case IRPosition::IRP_ARGUMENT:
  case IRPosition::IRP_CALL_SITE_ARGUMENT:
  case IRPosition::IRP_RETURNED:
  case IRPosition::IRP_CALL_SITE_RETURNED:
  case IRPosition::IRP_CALL_SITE:
    llvm_unreachable(
        "AAHeapToShared can only be created for function position!");
  case IRPosition::IRP_FUNCTION:
    AA = new (A.Allocator) AAHeapToSharedFunction(IRP, A);
    break;
  }

  return *AA;
}

AAKernelInfo &AAKernelInfo::createForPosition(const IRPosition &IRP,
                                              Attributor &A) {
  AAKernelInfo *AA = nullptr;
  switch (IRP.getPositionKind()) {
  case IRPosition::IRP_INVALID:
  case IRPosition::IRP_FLOAT:
  case IRPosition::IRP_ARGUMENT:
  case IRPosition::IRP_RETURNED:
  case IRPosition::IRP_CALL_SITE_RETURNED:
  case IRPosition::IRP_CALL_SITE_ARGUMENT:
    llvm_unreachable("KernelInfo can only be created for function position!");
  case IRPosition::IRP_CALL_SITE:
    AA = new (A.Allocator) AAKernelInfoCallSite(IRP, A);
    break;
  case IRPosition::IRP_FUNCTION:
    AA = new (A.Allocator) AAKernelInfoFunction(IRP, A);
    break;
  }

  return *AA;
}

AAFoldRuntimeCall &AAFoldRuntimeCall::createForPosition(const IRPosition &IRP,
                                                        Attributor &A) {
  AAFoldRuntimeCall *AA = nullptr;
  switch (IRP.getPositionKind()) {
  case IRPosition::IRP_INVALID:
  case IRPosition::IRP_FLOAT:
  case IRPosition::IRP_ARGUMENT:
  case IRPosition::IRP_RETURNED:
  case IRPosition::IRP_FUNCTION:
  case IRPosition::IRP_CALL_SITE:
  case IRPosition::IRP_CALL_SITE_ARGUMENT:
    llvm_unreachable("KernelInfo can only be created for call site position!");
  case IRPosition::IRP_CALL_SITE_RETURNED:
    AA = new (A.Allocator) AAFoldRuntimeCallCallSiteReturned(IRP, A);
    break;
  }

  return *AA;
}

PreservedAnalyses OpenMPOptPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (!containsOpenMP(M))
    return PreservedAnalyses::all();
  if (DisableOpenMPOptimizations)
    return PreservedAnalyses::all();

  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  KernelSet Kernels = getDeviceKernels(M);

  if (PrintModuleBeforeOptimizations)
    LLVM_DEBUG(dbgs() << TAG << "Module before OpenMPOpt Module Pass:\n" << M);

  auto IsCalled = [&](Function &F) {
    if (Kernels.contains(&F))
      return true;
    for (const User *U : F.users())
      if (!isa<BlockAddress>(U))
        return true;
    return false;
  };

  auto EmitRemark = [&](Function &F) {
    auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
    ORE.emit([&]() {
      OptimizationRemarkAnalysis ORA(DEBUG_TYPE, "OMP140", &F);
      return ORA << "Could not internalize function. "
                 << "Some optimizations may not be possible. [OMP140]";
    });
  };

  bool Changed = false;

  // Create internal copies of each function if this is a kernel Module. This
  // allows iterprocedural passes to see every call edge.
  DenseMap<Function *, Function *> InternalizedMap;
  if (isOpenMPDevice(M)) {
    SmallPtrSet<Function *, 16> InternalizeFns;
    for (Function &F : M)
      if (!F.isDeclaration() && !Kernels.contains(&F) && IsCalled(F) &&
          !DisableInternalization) {
        if (Attributor::isInternalizable(F)) {
          InternalizeFns.insert(&F);
        } else if (!F.hasLocalLinkage() && !F.hasFnAttribute(Attribute::Cold)) {
          EmitRemark(F);
        }
      }

    Changed |=
        Attributor::internalizeFunctions(InternalizeFns, InternalizedMap);
  }

  // Look at every function in the Module unless it was internalized.
  SetVector<Function *> Functions;
  SmallVector<Function *, 16> SCC;
  for (Function &F : M)
    if (!F.isDeclaration() && !InternalizedMap.lookup(&F)) {
      SCC.push_back(&F);
      Functions.insert(&F);
    }

  if (SCC.empty())
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();

  AnalysisGetter AG(FAM);

  auto OREGetter = [&FAM](Function *F) -> OptimizationRemarkEmitter & {
    return FAM.getResult<OptimizationRemarkEmitterAnalysis>(*F);
  };

  BumpPtrAllocator Allocator;
  CallGraphUpdater CGUpdater;

  bool PostLink = LTOPhase == ThinOrFullLTOPhase::FullLTOPostLink ||
                  LTOPhase == ThinOrFullLTOPhase::ThinLTOPreLink;
  OMPInformationCache InfoCache(M, AG, Allocator, /*CGSCC*/ nullptr, PostLink);

  unsigned MaxFixpointIterations =
      (isOpenMPDevice(M)) ? SetFixpointIterations : 32;

  AttributorConfig AC(CGUpdater);
  AC.DefaultInitializeLiveInternals = false;
  AC.IsModulePass = true;
  AC.RewriteSignatures = false;
  AC.MaxFixpointIterations = MaxFixpointIterations;
  AC.OREGetter = OREGetter;
  AC.PassName = DEBUG_TYPE;
  AC.InitializationCallback = OpenMPOpt::registerAAsForFunction;
  AC.IPOAmendableCB = [](const Function &F) {
    return F.hasFnAttribute("kernel");
  };

  Attributor A(Functions, InfoCache, AC);

  OpenMPOpt OMPOpt(SCC, CGUpdater, OREGetter, InfoCache, A);
  Changed |= OMPOpt.run(true);

  // Optionally inline device functions for potentially better performance.
  if (AlwaysInlineDeviceFunctions && isOpenMPDevice(M))
    for (Function &F : M)
      if (!F.isDeclaration() && !Kernels.contains(&F) &&
          !F.hasFnAttribute(Attribute::NoInline))
        F.addFnAttr(Attribute::AlwaysInline);

  if (PrintModuleAfterOptimizations)
    LLVM_DEBUG(dbgs() << TAG << "Module after OpenMPOpt Module Pass:\n" << M);

  if (Changed)
    return PreservedAnalyses::none();

  return PreservedAnalyses::all();
}

PreservedAnalyses OpenMPOptCGSCCPass::run(LazyCallGraph::SCC &C,
                                          CGSCCAnalysisManager &AM,
                                          LazyCallGraph &CG,
                                          CGSCCUpdateResult &UR) {
  if (!containsOpenMP(*C.begin()->getFunction().getParent()))
    return PreservedAnalyses::all();
  if (DisableOpenMPOptimizations)
    return PreservedAnalyses::all();

  SmallVector<Function *, 16> SCC;
  // If there are kernels in the module, we have to run on all SCC's.
  for (LazyCallGraph::Node &N : C) {
    Function *Fn = &N.getFunction();
    SCC.push_back(Fn);
  }

  if (SCC.empty())
    return PreservedAnalyses::all();

  Module &M = *C.begin()->getFunction().getParent();

  if (PrintModuleBeforeOptimizations)
    LLVM_DEBUG(dbgs() << TAG << "Module before OpenMPOpt CGSCC Pass:\n" << M);

  KernelSet Kernels = getDeviceKernels(M);

  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

  AnalysisGetter AG(FAM);

  auto OREGetter = [&FAM](Function *F) -> OptimizationRemarkEmitter & {
    return FAM.getResult<OptimizationRemarkEmitterAnalysis>(*F);
  };

  BumpPtrAllocator Allocator;
  CallGraphUpdater CGUpdater;
  CGUpdater.initialize(CG, C, AM, UR);

  bool PostLink = LTOPhase == ThinOrFullLTOPhase::FullLTOPostLink ||
                  LTOPhase == ThinOrFullLTOPhase::ThinLTOPreLink;
  SetVector<Function *> Functions(SCC.begin(), SCC.end());
  OMPInformationCache InfoCache(*(Functions.back()->getParent()), AG, Allocator,
                                /*CGSCC*/ &Functions, PostLink);

  unsigned MaxFixpointIterations =
      (isOpenMPDevice(M)) ? SetFixpointIterations : 32;

  AttributorConfig AC(CGUpdater);
  AC.DefaultInitializeLiveInternals = false;
  AC.IsModulePass = false;
  AC.RewriteSignatures = false;
  AC.MaxFixpointIterations = MaxFixpointIterations;
  AC.OREGetter = OREGetter;
  AC.PassName = DEBUG_TYPE;
  AC.InitializationCallback = OpenMPOpt::registerAAsForFunction;

  Attributor A(Functions, InfoCache, AC);

  OpenMPOpt OMPOpt(SCC, CGUpdater, OREGetter, InfoCache, A);
  bool Changed = OMPOpt.run(false);

  if (PrintModuleAfterOptimizations)
    LLVM_DEBUG(dbgs() << TAG << "Module after OpenMPOpt CGSCC Pass:\n" << M);

  if (Changed)
    return PreservedAnalyses::none();

  return PreservedAnalyses::all();
}

bool llvm::omp::isOpenMPKernel(Function &Fn) {
  return Fn.hasFnAttribute("kernel");
}

KernelSet llvm::omp::getDeviceKernels(Module &M) {
  // TODO: Create a more cross-platform way of determining device kernels.
  NamedMDNode *MD = M.getNamedMetadata("nvvm.annotations");
  KernelSet Kernels;

  if (!MD)
    return Kernels;

  for (auto *Op : MD->operands()) {
    if (Op->getNumOperands() < 2)
      continue;
    MDString *KindID = dyn_cast<MDString>(Op->getOperand(1));
    if (!KindID || KindID->getString() != "kernel")
      continue;

    Function *KernelFn =
        mdconst::dyn_extract_or_null<Function>(Op->getOperand(0));
    if (!KernelFn)
      continue;

    // We are only interested in OpenMP target regions. Others, such as kernels
    // generated by CUDA but linked together, are not interesting to this pass.
    if (isOpenMPKernel(*KernelFn)) {
      ++NumOpenMPTargetRegionKernels;
      Kernels.insert(KernelFn);
    } else
      ++NumNonOpenMPTargetRegionKernels;
  }

  return Kernels;
}

bool llvm::omp::containsOpenMP(Module &M) {
  Metadata *MD = M.getModuleFlag("openmp");
  if (!MD)
    return false;

  return true;
}

bool llvm::omp::isOpenMPDevice(Module &M) {
  Metadata *MD = M.getModuleFlag("openmp-device");
  if (!MD)
    return false;

  return true;
}
