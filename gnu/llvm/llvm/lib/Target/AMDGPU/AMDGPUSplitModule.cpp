//===- AMDGPUSplitModule.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements a module splitting algorithm designed to support the
/// FullLTO --lto-partitions option for parallel codegen. This is completely
/// different from the common SplitModule pass, as this system is designed with
/// AMDGPU in mind.
///
/// The basic idea of this module splitting implementation is the same as
/// SplitModule: load-balance the module's functions across a set of N
/// partitions to allow parallel codegen. However, it does it very
/// differently than the target-agnostic variant:
///   - The module has "split roots", which are kernels in the vast
//      majority of cases.
///   - Each root has a set of dependencies, and when a root and its
///     dependencies is considered "big", we try to put it in a partition where
///     most dependencies are already imported, to avoid duplicating large
///     amounts of code.
///   - There's special care for indirect calls in order to ensure
///     AMDGPUResourceUsageAnalysis can work correctly.
///
/// This file also includes a more elaborate logging system to enable
/// users to easily generate logs that (if desired) do not include any value
/// names, in order to not leak information about the source file.
/// Such logs are very helpful to understand and fix potential issues with
/// module splitting.

#include "AMDGPUSplitModule.h"
#include "AMDGPUTargetMachine.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "amdgpu-split-module"

namespace {

static cl::opt<float> LargeFnFactor(
    "amdgpu-module-splitting-large-function-threshold", cl::init(2.0f),
    cl::Hidden,
    cl::desc(
        "consider a function as large and needing special treatment when the "
        "cost of importing it into a partition"
        "exceeds the average cost of a partition by this factor; e;g. 2.0 "
        "means if the function and its dependencies is 2 times bigger than "
        "an average partition; 0 disables large functions handling entirely"));

static cl::opt<float> LargeFnOverlapForMerge(
    "amdgpu-module-splitting-large-function-merge-overlap", cl::init(0.8f),
    cl::Hidden,
    cl::desc(
        "defines how much overlap between two large function's dependencies "
        "is needed to put them in the same partition"));

static cl::opt<bool> NoExternalizeGlobals(
    "amdgpu-module-splitting-no-externalize-globals", cl::Hidden,
    cl::desc("disables externalization of global variable with local linkage; "
             "may cause globals to be duplicated which increases binary size"));

static cl::opt<std::string>
    LogDirOpt("amdgpu-module-splitting-log-dir", cl::Hidden,
              cl::desc("output directory for AMDGPU module splitting logs"));

static cl::opt<bool>
    LogPrivate("amdgpu-module-splitting-log-private", cl::Hidden,
               cl::desc("hash value names before printing them in the AMDGPU "
                        "module splitting logs"));

using CostType = InstructionCost::CostType;
using PartitionID = unsigned;
using GetTTIFn = function_ref<const TargetTransformInfo &(Function &)>;

static bool isEntryPoint(const Function *F) {
  return AMDGPU::isEntryFunctionCC(F->getCallingConv());
}

static std::string getName(const Value &V) {
  static bool HideNames;

  static llvm::once_flag HideNameInitFlag;
  llvm::call_once(HideNameInitFlag, [&]() {
    if (LogPrivate.getNumOccurrences())
      HideNames = LogPrivate;
    else {
      const auto EV = sys::Process::GetEnv("AMD_SPLIT_MODULE_LOG_PRIVATE");
      HideNames = (EV.value_or("0") != "0");
    }
  });

  if (!HideNames)
    return V.getName().str();
  return toHex(SHA256::hash(arrayRefFromStringRef(V.getName())),
               /*LowerCase=*/true);
}

/// Main logging helper.
///
/// Logging can be configured by the following environment variable.
///   AMD_SPLIT_MODULE_LOG_DIR=<filepath>
///     If set, uses <filepath> as the directory to write logfiles to
///     each time module splitting is used.
///   AMD_SPLIT_MODULE_LOG_PRIVATE
///     If set to anything other than zero, all names are hidden.
///
/// Both environment variables have corresponding CL options which
/// takes priority over them.
///
/// Any output printed to the log files is also printed to dbgs() when -debug is
/// used and LLVM_DEBUG is defined.
///
/// This approach has a small disadvantage over LLVM_DEBUG though: logging logic
/// cannot be removed from the code (by building without debug). This probably
/// has a small performance cost because if some computation/formatting is
/// needed for logging purpose, it may be done everytime only to be ignored
/// by the logger.
///
/// As this pass only runs once and is not doing anything computationally
/// expensive, this is likely a reasonable trade-off.
///
/// If some computation should really be avoided when unused, users of the class
/// can check whether any logging will occur by using the bool operator.
///
/// \code
///   if (SML) {
///     // Executes only if logging to a file or if -debug is available and
///     used.
///   }
/// \endcode
class SplitModuleLogger {
public:
  SplitModuleLogger(const Module &M) {
    std::string LogDir = LogDirOpt;
    if (LogDir.empty())
      LogDir = sys::Process::GetEnv("AMD_SPLIT_MODULE_LOG_DIR").value_or("");

    // No log dir specified means we don't need to log to a file.
    // We may still log to dbgs(), though.
    if (LogDir.empty())
      return;

    // If a log directory is specified, create a new file with a unique name in
    // that directory.
    int Fd;
    SmallString<0> PathTemplate;
    SmallString<0> RealPath;
    sys::path::append(PathTemplate, LogDir, "Module-%%-%%-%%-%%-%%-%%-%%.txt");
    if (auto Err =
            sys::fs::createUniqueFile(PathTemplate.str(), Fd, RealPath)) {
      report_fatal_error("Failed to create log file at '" + Twine(LogDir) +
                             "': " + Err.message(),
                         /*CrashDiag=*/false);
    }

    FileOS = std::make_unique<raw_fd_ostream>(Fd, /*shouldClose=*/true);
  }

  bool hasLogFile() const { return FileOS != nullptr; }

  raw_ostream &logfile() {
    assert(FileOS && "no logfile!");
    return *FileOS;
  }

  /// \returns true if this SML will log anything either to a file or dbgs().
  /// Can be used to avoid expensive computations that are ignored when logging
  /// is disabled.
  operator bool() const {
    return hasLogFile() || (DebugFlag && isCurrentDebugType(DEBUG_TYPE));
  }

private:
  std::unique_ptr<raw_fd_ostream> FileOS;
};

template <typename Ty>
static SplitModuleLogger &operator<<(SplitModuleLogger &SML, const Ty &Val) {
  static_assert(
      !std::is_same_v<Ty, Value>,
      "do not print values to logs directly, use handleName instead!");
  LLVM_DEBUG(dbgs() << Val);
  if (SML.hasLogFile())
    SML.logfile() << Val;
  return SML;
}

/// Calculate the cost of each function in \p M
/// \param SML Log Helper
/// \param GetTTI Abstract getter for TargetTransformInfo.
/// \param M Module to analyze.
/// \param CostMap[out] Resulting Function -> Cost map.
/// \return The module's total cost.
static CostType
calculateFunctionCosts(SplitModuleLogger &SML, GetTTIFn GetTTI, Module &M,
                       DenseMap<const Function *, CostType> &CostMap) {
  CostType ModuleCost = 0;
  CostType KernelCost = 0;

  for (auto &Fn : M) {
    if (Fn.isDeclaration())
      continue;

    CostType FnCost = 0;
    const auto &TTI = GetTTI(Fn);
    for (const auto &BB : Fn) {
      for (const auto &I : BB) {
        auto Cost =
            TTI.getInstructionCost(&I, TargetTransformInfo::TCK_CodeSize);
        assert(Cost != InstructionCost::getMax());
        // Assume expensive if we can't tell the cost of an instruction.
        CostType CostVal =
            Cost.getValue().value_or(TargetTransformInfo::TCC_Expensive);
        assert((FnCost + CostVal) >= FnCost && "Overflow!");
        FnCost += CostVal;
      }
    }

    assert(FnCost != 0);

    CostMap[&Fn] = FnCost;
    assert((ModuleCost + FnCost) >= ModuleCost && "Overflow!");
    ModuleCost += FnCost;

    if (isEntryPoint(&Fn))
      KernelCost += FnCost;
  }

  CostType FnCost = (ModuleCost - KernelCost);
  CostType ModuleCostOr1 = ModuleCost ? ModuleCost : 1;
  SML << "=> Total Module Cost: " << ModuleCost << '\n'
      << "  => KernelCost: " << KernelCost << " ("
      << format("%0.2f", (float(KernelCost) / ModuleCostOr1) * 100) << "%)\n"
      << "  => FnsCost: " << FnCost << " ("
      << format("%0.2f", (float(FnCost) / ModuleCostOr1) * 100) << "%)\n";

  return ModuleCost;
}

static bool canBeIndirectlyCalled(const Function &F) {
  if (F.isDeclaration() || isEntryPoint(&F))
    return false;
  return !F.hasLocalLinkage() ||
         F.hasAddressTaken(/*PutOffender=*/nullptr,
                           /*IgnoreCallbackUses=*/false,
                           /*IgnoreAssumeLikeCalls=*/true,
                           /*IgnoreLLVMUsed=*/true,
                           /*IgnoreARCAttachedCall=*/false,
                           /*IgnoreCastedDirectCall=*/true);
}

/// When a function or any of its callees performs an indirect call, this
/// takes over \ref addAllDependencies and adds all potentially callable
/// functions to \p Fns so they can be counted as dependencies of the function.
///
/// This is needed due to how AMDGPUResourceUsageAnalysis operates: in the
/// presence of an indirect call, the function's resource usage is the same as
/// the most expensive function in the module.
/// \param M    The module.
/// \param Fns[out] Resulting list of functions.
static void addAllIndirectCallDependencies(const Module &M,
                                           DenseSet<const Function *> &Fns) {
  for (const auto &Fn : M) {
    if (canBeIndirectlyCalled(Fn))
      Fns.insert(&Fn);
  }
}

/// Adds the functions that \p Fn may call to \p Fns, then recurses into each
/// callee until all reachable functions have been gathered.
///
/// \param SML Log Helper
/// \param CG Call graph for \p Fn's module.
/// \param Fn Current function to look at.
/// \param Fns[out] Resulting list of functions.
/// \param OnlyDirect Whether to only consider direct callees.
/// \param HadIndirectCall[out] Set to true if an indirect call was seen at some
/// point, either in \p Fn or in one of the function it calls. When that
/// happens, we fall back to adding all callable functions inside \p Fn's module
/// to \p Fns.
static void addAllDependencies(SplitModuleLogger &SML, const CallGraph &CG,
                               const Function &Fn,
                               DenseSet<const Function *> &Fns, bool OnlyDirect,
                               bool &HadIndirectCall) {
  assert(!Fn.isDeclaration());

  const Module &M = *Fn.getParent();
  SmallVector<const Function *> WorkList({&Fn});
  while (!WorkList.empty()) {
    const auto &CurFn = *WorkList.pop_back_val();
    assert(!CurFn.isDeclaration());

    // Scan for an indirect call. If such a call is found, we have to
    // conservatively assume this can call all non-entrypoint functions in the
    // module.

    for (auto &CGEntry : *CG[&CurFn]) {
      auto *CGNode = CGEntry.second;
      auto *Callee = CGNode->getFunction();
      if (!Callee) {
        if (OnlyDirect)
          continue;

        // Functions have an edge towards CallsExternalNode if they're external
        // declarations, or if they do an indirect call. As we only process
        // definitions here, we know this means the function has an indirect
        // call. We then have to conservatively assume this can call all
        // non-entrypoint functions in the module.
        if (CGNode != CG.getCallsExternalNode())
          continue; // this is another function-less node we don't care about.

        SML << "Indirect call detected in " << getName(CurFn)
            << " - treating all non-entrypoint functions as "
               "potential dependencies\n";

        // TODO: Print an ORE as well ?
        addAllIndirectCallDependencies(M, Fns);
        HadIndirectCall = true;
        continue;
      }

      if (Callee->isDeclaration())
        continue;

      auto [It, Inserted] = Fns.insert(Callee);
      if (Inserted)
        WorkList.push_back(Callee);
    }
  }
}

/// Contains information about a function and its dependencies.
/// This is a splitting root. The splitting algorithm works by
/// assigning these to partitions.
struct FunctionWithDependencies {
  FunctionWithDependencies(SplitModuleLogger &SML, CallGraph &CG,
                           const DenseMap<const Function *, CostType> &FnCosts,
                           const Function *Fn)
      : Fn(Fn) {
    // When Fn is not a kernel, we don't need to collect indirect callees.
    // Resource usage analysis is only performed on kernels, and we collect
    // indirect callees for resource usage analysis.
    addAllDependencies(SML, CG, *Fn, Dependencies,
                       /*OnlyDirect*/ !isEntryPoint(Fn), HasIndirectCall);
    TotalCost = FnCosts.at(Fn);
    for (const auto *Dep : Dependencies) {
      TotalCost += FnCosts.at(Dep);

      // We cannot duplicate functions with external linkage, or functions that
      // may be overriden at runtime.
      HasNonDuplicatableDependecy |=
          (Dep->hasExternalLinkage() || !Dep->isDefinitionExact());
    }
  }

  const Function *Fn = nullptr;
  DenseSet<const Function *> Dependencies;
  /// Whether \p Fn or any of its \ref Dependencies contains an indirect call.
  bool HasIndirectCall = false;
  /// Whether any of \p Fn's dependencies cannot be duplicated.
  bool HasNonDuplicatableDependecy = false;

  CostType TotalCost = 0;

  /// \returns true if this function and its dependencies can be considered
  /// large according to \p Threshold.
  bool isLarge(CostType Threshold) const {
    return TotalCost > Threshold && !Dependencies.empty();
  }
};

/// Calculates how much overlap there is between \p A and \p B.
/// \return A number between 0.0 and 1.0, where 1.0 means A == B and 0.0 means A
/// and B have no shared elements. Kernels do not count in overlap calculation.
static float calculateOverlap(const DenseSet<const Function *> &A,
                              const DenseSet<const Function *> &B) {
  DenseSet<const Function *> Total;
  for (const auto *F : A) {
    if (!isEntryPoint(F))
      Total.insert(F);
  }

  if (Total.empty())
    return 0.0f;

  unsigned NumCommon = 0;
  for (const auto *F : B) {
    if (isEntryPoint(F))
      continue;

    auto [It, Inserted] = Total.insert(F);
    if (!Inserted)
      ++NumCommon;
  }

  return static_cast<float>(NumCommon) / Total.size();
}

/// Performs all of the partitioning work on \p M.
/// \param SML Log Helper
/// \param M Module to partition.
/// \param NumParts Number of partitions to create.
/// \param ModuleCost Total cost of all functions in \p M.
/// \param FnCosts Map of Function -> Cost
/// \param WorkList Functions and their dependencies to process in order.
/// \returns The created partitions (a vector of size \p NumParts )
static std::vector<DenseSet<const Function *>>
doPartitioning(SplitModuleLogger &SML, Module &M, unsigned NumParts,
               CostType ModuleCost,
               const DenseMap<const Function *, CostType> &FnCosts,
               const SmallVector<FunctionWithDependencies> &WorkList) {

  SML << "\n--Partitioning Starts--\n";

  // Calculate a "large function threshold". When more than one function's total
  // import cost exceeds this value, we will try to assign it to an existing
  // partition to reduce the amount of duplication needed.
  //
  // e.g. let two functions X and Y have a import cost of ~10% of the module, we
  // assign X to a partition as usual, but when we get to Y, we check if it's
  // worth also putting it in Y's partition.
  const CostType LargeFnThreshold =
      LargeFnFactor ? CostType(((ModuleCost / NumParts) * LargeFnFactor))
                    : std::numeric_limits<CostType>::max();

  std::vector<DenseSet<const Function *>> Partitions;
  Partitions.resize(NumParts);

  // Assign functions to partitions, and try to keep the partitions more or
  // less balanced. We do that through a priority queue sorted in reverse, so we
  // can always look at the partition with the least content.
  //
  // There are some cases where we will be deliberately unbalanced though.
  //  - Large functions: we try to merge with existing partitions to reduce code
  //  duplication.
  //  - Functions with indirect or external calls always go in the first
  //  partition (P0).
  auto ComparePartitions = [](const std::pair<PartitionID, CostType> &a,
                              const std::pair<PartitionID, CostType> &b) {
    // When two partitions have the same cost, assign to the one with the
    // biggest ID first. This allows us to put things in P0 last, because P0 may
    // have other stuff added later.
    if (a.second == b.second)
      return a.first < b.first;
    return a.second > b.second;
  };

  // We can't use priority_queue here because we need to be able to access any
  // element. This makes this a bit inefficient as we need to sort it again
  // everytime we change it, but it's a very small array anyway (likely under 64
  // partitions) so it's a cheap operation.
  std::vector<std::pair<PartitionID, CostType>> BalancingQueue;
  for (unsigned I = 0; I < NumParts; ++I)
    BalancingQueue.emplace_back(I, 0);

  // Helper function to handle assigning a function to a partition. This takes
  // care of updating the balancing queue.
  const auto AssignToPartition = [&](PartitionID PID,
                                     const FunctionWithDependencies &FWD) {
    auto &FnsInPart = Partitions[PID];
    FnsInPart.insert(FWD.Fn);
    FnsInPart.insert(FWD.Dependencies.begin(), FWD.Dependencies.end());

    SML << "assign " << getName(*FWD.Fn) << " to P" << PID << "\n  ->  ";
    if (!FWD.Dependencies.empty()) {
      SML << FWD.Dependencies.size() << " dependencies added\n";
    };

    // Update the balancing queue. we scan backwards because in the common case
    // the partition is at the end.
    for (auto &[QueuePID, Cost] : reverse(BalancingQueue)) {
      if (QueuePID == PID) {
        CostType NewCost = 0;
        for (auto *Fn : Partitions[PID])
          NewCost += FnCosts.at(Fn);

        SML << "[Updating P" << PID << " Cost]:" << Cost << " -> " << NewCost;
        if (Cost) {
          SML << " (" << unsigned(((float(NewCost) / Cost) - 1) * 100)
              << "% increase)";
        }
        SML << '\n';

        Cost = NewCost;
      }
    }

    sort(BalancingQueue, ComparePartitions);
  };

  for (auto &CurFn : WorkList) {
    // When a function has indirect calls, it must stay in the first partition
    // alongside every reachable non-entry function. This is a nightmare case
    // for splitting as it severely limits what we can do.
    if (CurFn.HasIndirectCall) {
      SML << "Function with indirect call(s): " << getName(*CurFn.Fn)
          << " defaulting to P0\n";
      AssignToPartition(0, CurFn);
      continue;
    }

    // When a function has non duplicatable dependencies, we have to keep it in
    // the first partition as well. This is a conservative approach, a
    // finer-grained approach could keep track of which dependencies are
    // non-duplicatable exactly and just make sure they're grouped together.
    if (CurFn.HasNonDuplicatableDependecy) {
      SML << "Function with externally visible dependency "
          << getName(*CurFn.Fn) << " defaulting to P0\n";
      AssignToPartition(0, CurFn);
      continue;
    }

    // Be smart with large functions to avoid duplicating their dependencies.
    if (CurFn.isLarge(LargeFnThreshold)) {
      assert(LargeFnOverlapForMerge >= 0.0f && LargeFnOverlapForMerge <= 1.0f);
      SML << "Large Function: " << getName(*CurFn.Fn)
          << " - looking for partition with at least "
          << format("%0.2f", LargeFnOverlapForMerge * 100) << "% overlap\n";

      bool Assigned = false;
      for (const auto &[PID, Fns] : enumerate(Partitions)) {
        float Overlap = calculateOverlap(CurFn.Dependencies, Fns);
        SML << "  => " << format("%0.2f", Overlap * 100) << "% overlap with P"
            << PID << '\n';
        if (Overlap > LargeFnOverlapForMerge) {
          SML << "  selecting P" << PID << '\n';
          AssignToPartition(PID, CurFn);
          Assigned = true;
        }
      }

      if (Assigned)
        continue;
    }

    // Normal "load-balancing", assign to partition with least pressure.
    auto [PID, CurCost] = BalancingQueue.back();
    AssignToPartition(PID, CurFn);
  }

  if (SML) {
    for (const auto &[Idx, Part] : enumerate(Partitions)) {
      CostType Cost = 0;
      for (auto *Fn : Part)
        Cost += FnCosts.at(Fn);
      SML << "P" << Idx << " has a total cost of " << Cost << " ("
          << format("%0.2f", (float(Cost) / ModuleCost) * 100)
          << "% of source module)\n";
    }

    SML << "--Partitioning Done--\n\n";
  }

  // Check no functions were missed.
#ifndef NDEBUG
  DenseSet<const Function *> AllFunctions;
  for (const auto &Part : Partitions)
    AllFunctions.insert(Part.begin(), Part.end());

  for (auto &Fn : M) {
    if (!Fn.isDeclaration() && !AllFunctions.contains(&Fn)) {
      assert(AllFunctions.contains(&Fn) && "Missed a function?!");
    }
  }
#endif

  return Partitions;
}

static void externalize(GlobalValue &GV) {
  if (GV.hasLocalLinkage()) {
    GV.setLinkage(GlobalValue::ExternalLinkage);
    GV.setVisibility(GlobalValue::HiddenVisibility);
  }

  // Unnamed entities must be named consistently between modules. setName will
  // give a distinct name to each such entity.
  if (!GV.hasName())
    GV.setName("__llvmsplit_unnamed");
}

static bool hasDirectCaller(const Function &Fn) {
  for (auto &U : Fn.uses()) {
    if (auto *CB = dyn_cast<CallBase>(U.getUser()); CB && CB->isCallee(&U))
      return true;
  }
  return false;
}

static void splitAMDGPUModule(
    GetTTIFn GetTTI, Module &M, unsigned N,
    function_ref<void(std::unique_ptr<Module> MPart)> ModuleCallback) {

  SplitModuleLogger SML(M);

  CallGraph CG(M);

  // Externalize functions whose address are taken.
  //
  // This is needed because partitioning is purely based on calls, but sometimes
  // a kernel/function may just look at the address of another local function
  // and not do anything (no calls). After partitioning, that local function may
  // end up in a different module (so it's just a declaration in the module
  // where its address is taken), which emits a "undefined hidden symbol" linker
  // error.
  //
  // Additionally, it guides partitioning to not duplicate this function if it's
  // called directly at some point.
  for (auto &Fn : M) {
    if (Fn.hasAddressTaken()) {
      if (Fn.hasLocalLinkage()) {
        SML << "[externalize] " << Fn.getName()
            << " because its address is taken\n";
      }
      externalize(Fn);
    }
  }

  // Externalize local GVs, which avoids duplicating their initializers, which
  // in turns helps keep code size in check.
  if (!NoExternalizeGlobals) {
    for (auto &GV : M.globals()) {
      if (GV.hasLocalLinkage())
        SML << "[externalize] GV " << GV.getName() << '\n';
      externalize(GV);
    }
  }

  // Start by calculating the cost of every function in the module, as well as
  // the module's overall cost.
  DenseMap<const Function *, CostType> FnCosts;
  const CostType ModuleCost = calculateFunctionCosts(SML, GetTTI, M, FnCosts);

  // First, gather ever kernel into the worklist.
  SmallVector<FunctionWithDependencies> WorkList;
  for (auto &Fn : M) {
    if (isEntryPoint(&Fn) && !Fn.isDeclaration())
      WorkList.emplace_back(SML, CG, FnCosts, &Fn);
  }

  // Then, find missing functions that need to be considered as additional
  // roots. These can't be called in theory, but in practice we still have to
  // handle them to avoid linker errors.
  {
    DenseSet<const Function *> SeenFunctions;
    for (const auto &FWD : WorkList) {
      SeenFunctions.insert(FWD.Fn);
      SeenFunctions.insert(FWD.Dependencies.begin(), FWD.Dependencies.end());
    }

    for (auto &Fn : M) {
      // If this function is not part of any kernel's dependencies and isn't
      // directly called, consider it as a root.
      if (!Fn.isDeclaration() && !isEntryPoint(&Fn) &&
          !SeenFunctions.count(&Fn) && !hasDirectCaller(Fn)) {
        WorkList.emplace_back(SML, CG, FnCosts, &Fn);
      }
    }
  }

  // Sort the worklist so the most expensive roots are seen first.
  sort(WorkList, [&](auto &A, auto &B) {
    // Sort by total cost, and if the total cost is identical, sort
    // alphabetically.
    if (A.TotalCost == B.TotalCost)
      return A.Fn->getName() < B.Fn->getName();
    return A.TotalCost > B.TotalCost;
  });

  if (SML) {
    SML << "Worklist\n";
    for (const auto &FWD : WorkList) {
      SML << "[root] " << getName(*FWD.Fn) << " (totalCost:" << FWD.TotalCost
          << " indirect:" << FWD.HasIndirectCall
          << " hasNonDuplicatableDep:" << FWD.HasNonDuplicatableDependecy
          << ")\n";
      // Sort function names before printing to ensure determinism.
      SmallVector<std::string> SortedDepNames;
      SortedDepNames.reserve(FWD.Dependencies.size());
      for (const auto *Dep : FWD.Dependencies)
        SortedDepNames.push_back(getName(*Dep));
      sort(SortedDepNames);

      for (const auto &Name : SortedDepNames)
        SML << "  [dependency] " << Name << '\n';
    }
  }

  // This performs all of the partitioning work.
  auto Partitions = doPartitioning(SML, M, N, ModuleCost, FnCosts, WorkList);
  assert(Partitions.size() == N);

  // If we didn't externalize GVs, then local GVs need to be conservatively
  // imported into every module (including their initializers), and then cleaned
  // up afterwards.
  const auto NeedsConservativeImport = [&](const GlobalValue *GV) {
    // We conservatively import private/internal GVs into every module and clean
    // them up afterwards.
    const auto *Var = dyn_cast<GlobalVariable>(GV);
    return Var && Var->hasLocalLinkage();
  };

  SML << "Creating " << N << " modules...\n";
  unsigned TotalFnImpls = 0;
  for (unsigned I = 0; I < N; ++I) {
    const auto &FnsInPart = Partitions[I];

    ValueToValueMapTy VMap;
    std::unique_ptr<Module> MPart(
        CloneModule(M, VMap, [&](const GlobalValue *GV) {
          // Functions go in their assigned partition.
          if (const auto *Fn = dyn_cast<Function>(GV))
            return FnsInPart.contains(Fn);

          if (NeedsConservativeImport(GV))
            return true;

          // Everything else goes in the first partition.
          return I == 0;
        }));

    // Clean-up conservatively imported GVs without any users.
    for (auto &GV : make_early_inc_range(MPart->globals())) {
      if (NeedsConservativeImport(&GV) && GV.use_empty())
        GV.eraseFromParent();
    }

    unsigned NumAllFns = 0, NumKernels = 0;
    for (auto &Cur : *MPart) {
      if (!Cur.isDeclaration()) {
        ++NumAllFns;
        if (isEntryPoint(&Cur))
          ++NumKernels;
      }
    }
    TotalFnImpls += NumAllFns;
    SML << "  - Module " << I << " with " << NumAllFns << " functions ("
        << NumKernels << " kernels)\n";
    ModuleCallback(std::move(MPart));
  }

  SML << TotalFnImpls << " function definitions across all modules ("
      << format("%0.2f", (float(TotalFnImpls) / FnCosts.size()) * 100)
      << "% of original module)\n";
}
} // namespace

PreservedAnalyses AMDGPUSplitModulePass::run(Module &M,
                                             ModuleAnalysisManager &MAM) {
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  const auto TTIGetter = [&FAM](Function &F) -> const TargetTransformInfo & {
    return FAM.getResult<TargetIRAnalysis>(F);
  };
  splitAMDGPUModule(TTIGetter, M, N, ModuleCallback);
  // We don't change the original module.
  return PreservedAnalyses::all();
}
