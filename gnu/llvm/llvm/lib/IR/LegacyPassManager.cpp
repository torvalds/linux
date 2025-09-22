//===- LegacyPassManager.cpp - LLVM Pass Infrastructure Implementation ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the legacy LLVM Pass Manager infrastructure.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManagers.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

extern cl::opt<bool> UseNewDbgInfoFormat;
// See PassManagers.h for Pass Manager infrastructure overview.

//===----------------------------------------------------------------------===//
// Pass debugging information.  Often it is useful to find out what pass is
// running when a crash occurs in a utility.  When this library is compiled with
// debugging on, a command line option (--debug-pass) is enabled that causes the
// pass name to be printed before it executes.
//

namespace {
// Different debug levels that can be enabled...
enum PassDebugLevel {
  Disabled, Arguments, Structure, Executions, Details
};
} // namespace

static cl::opt<enum PassDebugLevel> PassDebugging(
    "debug-pass", cl::Hidden,
    cl::desc("Print legacy PassManager debugging information"),
    cl::values(clEnumVal(Disabled, "disable debug output"),
               clEnumVal(Arguments, "print pass arguments to pass to 'opt'"),
               clEnumVal(Structure, "print pass structure before run()"),
               clEnumVal(Executions, "print pass name before it is executed"),
               clEnumVal(Details, "print pass details when it is executed")));

/// isPassDebuggingExecutionsOrMore - Return true if -debug-pass=Executions
/// or higher is specified.
bool PMDataManager::isPassDebuggingExecutionsOrMore() const {
  return PassDebugging >= Executions;
}

unsigned PMDataManager::initSizeRemarkInfo(
    Module &M, StringMap<std::pair<unsigned, unsigned>> &FunctionToInstrCount) {
  // Only calculate getInstructionCount if the size-info remark is requested.
  unsigned InstrCount = 0;

  // Collect instruction counts for every function. We'll use this to emit
  // per-function size remarks later.
  for (Function &F : M) {
    unsigned FCount = F.getInstructionCount();

    // Insert a record into FunctionToInstrCount keeping track of the current
    // size of the function as the first member of a pair. Set the second
    // member to 0; if the function is deleted by the pass, then when we get
    // here, we'll be able to let the user know that F no longer contributes to
    // the module.
    FunctionToInstrCount[F.getName().str()] =
        std::pair<unsigned, unsigned>(FCount, 0);
    InstrCount += FCount;
  }
  return InstrCount;
}

void PMDataManager::emitInstrCountChangedRemark(
    Pass *P, Module &M, int64_t Delta, unsigned CountBefore,
    StringMap<std::pair<unsigned, unsigned>> &FunctionToInstrCount,
    Function *F) {
  // If it's a pass manager, don't emit a remark. (This hinges on the assumption
  // that the only passes that return non-null with getAsPMDataManager are pass
  // managers.) The reason we have to do this is to avoid emitting remarks for
  // CGSCC passes.
  if (P->getAsPMDataManager())
    return;

  // Set to true if this isn't a module pass or CGSCC pass.
  bool CouldOnlyImpactOneFunction = (F != nullptr);

  // Helper lambda that updates the changes to the size of some function.
  auto UpdateFunctionChanges =
      [&FunctionToInstrCount](Function &MaybeChangedFn) {
        // Update the total module count.
        unsigned FnSize = MaybeChangedFn.getInstructionCount();
        auto It = FunctionToInstrCount.find(MaybeChangedFn.getName());

        // If we created a new function, then we need to add it to the map and
        // say that it changed from 0 instructions to FnSize.
        if (It == FunctionToInstrCount.end()) {
          FunctionToInstrCount[MaybeChangedFn.getName()] =
              std::pair<unsigned, unsigned>(0, FnSize);
          return;
        }
        // Insert the new function size into the second member of the pair. This
        // tells us whether or not this function changed in size.
        It->second.second = FnSize;
      };

  // We need to initially update all of the function sizes.
  // If no function was passed in, then we're either a module pass or an
  // CGSCC pass.
  if (!CouldOnlyImpactOneFunction)
    std::for_each(M.begin(), M.end(), UpdateFunctionChanges);
  else
    UpdateFunctionChanges(*F);

  // Do we have a function we can use to emit a remark?
  if (!CouldOnlyImpactOneFunction) {
    // We need a function containing at least one basic block in order to output
    // remarks. Since it's possible that the first function in the module
    // doesn't actually contain a basic block, we have to go and find one that's
    // suitable for emitting remarks.
    auto It = llvm::find_if(M, [](const Function &Fn) { return !Fn.empty(); });

    // Didn't find a function. Quit.
    if (It == M.end())
      return;

    // We found a function containing at least one basic block.
    F = &*It;
  }
  int64_t CountAfter = static_cast<int64_t>(CountBefore) + Delta;
  BasicBlock &BB = *F->begin();
  OptimizationRemarkAnalysis R("size-info", "IRSizeChange",
                               DiagnosticLocation(), &BB);
  // FIXME: Move ore namespace to DiagnosticInfo so that we can use it. This
  // would let us use NV instead of DiagnosticInfoOptimizationBase::Argument.
  R << DiagnosticInfoOptimizationBase::Argument("Pass", P->getPassName())
    << ": IR instruction count changed from "
    << DiagnosticInfoOptimizationBase::Argument("IRInstrsBefore", CountBefore)
    << " to "
    << DiagnosticInfoOptimizationBase::Argument("IRInstrsAfter", CountAfter)
    << "; Delta: "
    << DiagnosticInfoOptimizationBase::Argument("DeltaInstrCount", Delta);
  F->getContext().diagnose(R); // Not using ORE for layering reasons.

  // Emit per-function size change remarks separately.
  std::string PassName = P->getPassName().str();

  // Helper lambda that emits a remark when the size of a function has changed.
  auto EmitFunctionSizeChangedRemark = [&FunctionToInstrCount, &F, &BB,
                                        &PassName](StringRef Fname) {
    unsigned FnCountBefore, FnCountAfter;
    std::pair<unsigned, unsigned> &Change = FunctionToInstrCount[Fname];
    std::tie(FnCountBefore, FnCountAfter) = Change;
    int64_t FnDelta = static_cast<int64_t>(FnCountAfter) -
                      static_cast<int64_t>(FnCountBefore);

    if (FnDelta == 0)
      return;

    // FIXME: We shouldn't use BB for the location here. Unfortunately, because
    // the function that we're looking at could have been deleted, we can't use
    // it for the source location. We *want* remarks when a function is deleted
    // though, so we're kind of stuck here as is. (This remark, along with the
    // whole-module size change remarks really ought not to have source
    // locations at all.)
    OptimizationRemarkAnalysis FR("size-info", "FunctionIRSizeChange",
                                  DiagnosticLocation(), &BB);
    FR << DiagnosticInfoOptimizationBase::Argument("Pass", PassName)
       << ": Function: "
       << DiagnosticInfoOptimizationBase::Argument("Function", Fname)
       << ": IR instruction count changed from "
       << DiagnosticInfoOptimizationBase::Argument("IRInstrsBefore",
                                                   FnCountBefore)
       << " to "
       << DiagnosticInfoOptimizationBase::Argument("IRInstrsAfter",
                                                   FnCountAfter)
       << "; Delta: "
       << DiagnosticInfoOptimizationBase::Argument("DeltaInstrCount", FnDelta);
    F->getContext().diagnose(FR);

    // Update the function size.
    Change.first = FnCountAfter;
  };

  // Are we looking at more than one function? If so, emit remarks for all of
  // the functions in the module. Otherwise, only emit one remark.
  if (!CouldOnlyImpactOneFunction)
    std::for_each(FunctionToInstrCount.keys().begin(),
                  FunctionToInstrCount.keys().end(),
                  EmitFunctionSizeChangedRemark);
  else
    EmitFunctionSizeChangedRemark(F->getName().str());
}

void PassManagerPrettyStackEntry::print(raw_ostream &OS) const {
  if (!V && !M)
    OS << "Releasing pass '";
  else
    OS << "Running pass '";

  OS << P->getPassName() << "'";

  if (M) {
    OS << " on module '" << M->getModuleIdentifier() << "'.\n";
    return;
  }
  if (!V) {
    OS << '\n';
    return;
  }

  OS << " on ";
  if (isa<Function>(V))
    OS << "function";
  else if (isa<BasicBlock>(V))
    OS << "basic block";
  else
    OS << "value";

  OS << " '";
  V->printAsOperand(OS, /*PrintType=*/false, M);
  OS << "'\n";
}

namespace llvm {
namespace legacy {
bool debugPassSpecified() { return PassDebugging != Disabled; }

//===----------------------------------------------------------------------===//
// FunctionPassManagerImpl
//
/// FunctionPassManagerImpl manages FPPassManagers
class FunctionPassManagerImpl : public Pass,
                                public PMDataManager,
                                public PMTopLevelManager {
  virtual void anchor();
private:
  bool wasRun;
public:
  static char ID;
  explicit FunctionPassManagerImpl()
      : Pass(PT_PassManager, ID), PMTopLevelManager(new FPPassManager()),
        wasRun(false) {}

  /// \copydoc FunctionPassManager::add()
  void add(Pass *P) {
    schedulePass(P);
  }

  /// createPrinterPass - Get a function printer pass.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override {
    return createPrintFunctionPass(O, Banner);
  }

  // Prepare for running an on the fly pass, freeing memory if needed
  // from a previous run.
  void releaseMemoryOnTheFly();

  /// run - Execute all of the passes scheduled for execution.  Keep track of
  /// whether any of the passes modifies the module, and if so, return true.
  bool run(Function &F);

  /// doInitialization - Run all of the initializers for the function passes.
  ///
  bool doInitialization(Module &M) override;

  /// doFinalization - Run all of the finalizers for the function passes.
  ///
  bool doFinalization(Module &M) override;


  PMDataManager *getAsPMDataManager() override { return this; }
  Pass *getAsPass() override { return this; }
  PassManagerType getTopLevelPassManagerType() override {
    return PMT_FunctionPassManager;
  }

  /// Pass Manager itself does not invalidate any analysis info.
  void getAnalysisUsage(AnalysisUsage &Info) const override {
    Info.setPreservesAll();
  }

  FPPassManager *getContainedManager(unsigned N) {
    assert(N < PassManagers.size() && "Pass number out of range!");
    FPPassManager *FP = static_cast<FPPassManager *>(PassManagers[N]);
    return FP;
  }

  void dumpPassStructure(unsigned Offset) override {
    for (unsigned I = 0; I < getNumContainedManagers(); ++I)
      getContainedManager(I)->dumpPassStructure(Offset);
  }
};

void FunctionPassManagerImpl::anchor() {}

char FunctionPassManagerImpl::ID = 0;

//===----------------------------------------------------------------------===//
// FunctionPassManagerImpl implementation
//
bool FunctionPassManagerImpl::doInitialization(Module &M) {
  bool Changed = false;

  dumpArguments();
  dumpPasses();

  for (ImmutablePass *ImPass : getImmutablePasses())
    Changed |= ImPass->doInitialization(M);

  for (unsigned Index = 0; Index < getNumContainedManagers(); ++Index)
    Changed |= getContainedManager(Index)->doInitialization(M);

  return Changed;
}

bool FunctionPassManagerImpl::doFinalization(Module &M) {
  bool Changed = false;

  for (int Index = getNumContainedManagers() - 1; Index >= 0; --Index)
    Changed |= getContainedManager(Index)->doFinalization(M);

  for (ImmutablePass *ImPass : getImmutablePasses())
    Changed |= ImPass->doFinalization(M);

  return Changed;
}

void FunctionPassManagerImpl::releaseMemoryOnTheFly() {
  if (!wasRun)
    return;
  for (unsigned Index = 0; Index < getNumContainedManagers(); ++Index) {
    FPPassManager *FPPM = getContainedManager(Index);
    for (unsigned Index = 0; Index < FPPM->getNumContainedPasses(); ++Index) {
      FPPM->getContainedPass(Index)->releaseMemory();
    }
  }
  wasRun = false;
}

// Execute all the passes managed by this top level manager.
// Return true if any function is modified by a pass.
bool FunctionPassManagerImpl::run(Function &F) {
  bool Changed = false;

  initializeAllAnalysisInfo();
  for (unsigned Index = 0; Index < getNumContainedManagers(); ++Index) {
    Changed |= getContainedManager(Index)->runOnFunction(F);
    F.getContext().yield();
  }

  for (unsigned Index = 0; Index < getNumContainedManagers(); ++Index)
    getContainedManager(Index)->cleanup();

  wasRun = true;
  return Changed;
}
} // namespace legacy
} // namespace llvm

namespace {
//===----------------------------------------------------------------------===//
// MPPassManager
//
/// MPPassManager manages ModulePasses and function pass managers.
/// It batches all Module passes and function pass managers together and
/// sequences them to process one module.
class MPPassManager : public Pass, public PMDataManager {
public:
  static char ID;
  explicit MPPassManager() : Pass(PT_PassManager, ID) {}

  // Delete on the fly managers.
  ~MPPassManager() override {
    for (auto &OnTheFlyManager : OnTheFlyManagers) {
      legacy::FunctionPassManagerImpl *FPP = OnTheFlyManager.second;
      delete FPP;
    }
  }

  /// createPrinterPass - Get a module printer pass.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override {
    return createPrintModulePass(O, Banner);
  }

  /// run - Execute all of the passes scheduled for execution.  Keep track of
  /// whether any of the passes modifies the module, and if so, return true.
  bool runOnModule(Module &M);

  using llvm::Pass::doInitialization;
  using llvm::Pass::doFinalization;

  /// Pass Manager itself does not invalidate any analysis info.
  void getAnalysisUsage(AnalysisUsage &Info) const override {
    Info.setPreservesAll();
  }

  /// Add RequiredPass into list of lower level passes required by pass P.
  /// RequiredPass is run on the fly by Pass Manager when P requests it
  /// through getAnalysis interface.
  void addLowerLevelRequiredPass(Pass *P, Pass *RequiredPass) override;

  /// Return function pass corresponding to PassInfo PI, that is
  /// required by module pass MP. Instantiate analysis pass, by using
  /// its runOnFunction() for function F.
  std::tuple<Pass *, bool> getOnTheFlyPass(Pass *MP, AnalysisID PI,
                                           Function &F) override;

  StringRef getPassName() const override { return "Module Pass Manager"; }

  PMDataManager *getAsPMDataManager() override { return this; }
  Pass *getAsPass() override { return this; }

  // Print passes managed by this manager
  void dumpPassStructure(unsigned Offset) override {
    dbgs().indent(Offset*2) << "ModulePass Manager\n";
    for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
      ModulePass *MP = getContainedPass(Index);
      MP->dumpPassStructure(Offset + 1);
      MapVector<Pass *, legacy::FunctionPassManagerImpl *>::const_iterator I =
          OnTheFlyManagers.find(MP);
      if (I != OnTheFlyManagers.end())
        I->second->dumpPassStructure(Offset + 2);
      dumpLastUses(MP, Offset+1);
    }
  }

  ModulePass *getContainedPass(unsigned N) {
    assert(N < PassVector.size() && "Pass number out of range!");
    return static_cast<ModulePass *>(PassVector[N]);
  }

  PassManagerType getPassManagerType() const override {
    return PMT_ModulePassManager;
  }

 private:
  /// Collection of on the fly FPPassManagers. These managers manage
  /// function passes that are required by module passes.
   MapVector<Pass *, legacy::FunctionPassManagerImpl *> OnTheFlyManagers;
};

char MPPassManager::ID = 0;
} // End anonymous namespace

namespace llvm {
namespace legacy {
//===----------------------------------------------------------------------===//
// PassManagerImpl
//

/// PassManagerImpl manages MPPassManagers
class PassManagerImpl : public Pass,
                        public PMDataManager,
                        public PMTopLevelManager {
  virtual void anchor();

public:
  static char ID;
  explicit PassManagerImpl()
      : Pass(PT_PassManager, ID), PMTopLevelManager(new MPPassManager()) {}

  /// \copydoc PassManager::add()
  void add(Pass *P) {
    schedulePass(P);
  }

  /// createPrinterPass - Get a module printer pass.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override {
    return createPrintModulePass(O, Banner);
  }

  /// run - Execute all of the passes scheduled for execution.  Keep track of
  /// whether any of the passes modifies the module, and if so, return true.
  bool run(Module &M);

  using llvm::Pass::doInitialization;
  using llvm::Pass::doFinalization;

  /// Pass Manager itself does not invalidate any analysis info.
  void getAnalysisUsage(AnalysisUsage &Info) const override {
    Info.setPreservesAll();
  }

  PMDataManager *getAsPMDataManager() override { return this; }
  Pass *getAsPass() override { return this; }
  PassManagerType getTopLevelPassManagerType() override {
    return PMT_ModulePassManager;
  }

  MPPassManager *getContainedManager(unsigned N) {
    assert(N < PassManagers.size() && "Pass number out of range!");
    MPPassManager *MP = static_cast<MPPassManager *>(PassManagers[N]);
    return MP;
  }
};

void PassManagerImpl::anchor() {}

char PassManagerImpl::ID = 0;

//===----------------------------------------------------------------------===//
// PassManagerImpl implementation

//
/// run - Execute all of the passes scheduled for execution.  Keep track of
/// whether any of the passes modifies the module, and if so, return true.
bool PassManagerImpl::run(Module &M) {
  bool Changed = false;

  dumpArguments();
  dumpPasses();

  // RemoveDIs: if a command line flag is given, convert to the
  // DbgVariableRecord representation of debug-info for the duration of these
  // passes.
  ScopedDbgInfoFormatSetter FormatSetter(M, UseNewDbgInfoFormat);

  for (ImmutablePass *ImPass : getImmutablePasses())
    Changed |= ImPass->doInitialization(M);

  initializeAllAnalysisInfo();
  for (unsigned Index = 0; Index < getNumContainedManagers(); ++Index) {
    Changed |= getContainedManager(Index)->runOnModule(M);
    M.getContext().yield();
  }

  for (ImmutablePass *ImPass : getImmutablePasses())
    Changed |= ImPass->doFinalization(M);

  return Changed;
}
} // namespace legacy
} // namespace llvm

//===----------------------------------------------------------------------===//
// PMTopLevelManager implementation

/// Initialize top level manager. Create first pass manager.
PMTopLevelManager::PMTopLevelManager(PMDataManager *PMDM) {
  PMDM->setTopLevelManager(this);
  addPassManager(PMDM);
  activeStack.push(PMDM);
}

/// Set pass P as the last user of the given analysis passes.
void
PMTopLevelManager::setLastUser(ArrayRef<Pass*> AnalysisPasses, Pass *P) {
  unsigned PDepth = 0;
  if (P->getResolver())
    PDepth = P->getResolver()->getPMDataManager().getDepth();

  for (Pass *AP : AnalysisPasses) {
    // Record P as the new last user of AP.
    auto &LastUserOfAP = LastUser[AP];
    if (LastUserOfAP)
      InversedLastUser[LastUserOfAP].erase(AP);
    LastUserOfAP = P;
    InversedLastUser[P].insert(AP);

    if (P == AP)
      continue;

    // Update the last users of passes that are required transitive by AP.
    AnalysisUsage *AnUsage = findAnalysisUsage(AP);
    const AnalysisUsage::VectorType &IDs = AnUsage->getRequiredTransitiveSet();
    SmallVector<Pass *, 12> LastUses;
    SmallVector<Pass *, 12> LastPMUses;
    for (AnalysisID ID : IDs) {
      Pass *AnalysisPass = findAnalysisPass(ID);
      assert(AnalysisPass && "Expected analysis pass to exist.");
      AnalysisResolver *AR = AnalysisPass->getResolver();
      assert(AR && "Expected analysis resolver to exist.");
      unsigned APDepth = AR->getPMDataManager().getDepth();

      if (PDepth == APDepth)
        LastUses.push_back(AnalysisPass);
      else if (PDepth > APDepth)
        LastPMUses.push_back(AnalysisPass);
    }

    setLastUser(LastUses, P);

    // If this pass has a corresponding pass manager, push higher level
    // analysis to this pass manager.
    if (P->getResolver())
      setLastUser(LastPMUses, P->getResolver()->getPMDataManager().getAsPass());

    // If AP is the last user of other passes then make P last user of
    // such passes.
    auto &LastUsedByAP = InversedLastUser[AP];
    for (Pass *L : LastUsedByAP)
      LastUser[L] = P;
    InversedLastUser[P].insert(LastUsedByAP.begin(), LastUsedByAP.end());
    LastUsedByAP.clear();
  }
}

/// Collect passes whose last user is P
void PMTopLevelManager::collectLastUses(SmallVectorImpl<Pass *> &LastUses,
                                        Pass *P) {
  auto DMI = InversedLastUser.find(P);
  if (DMI == InversedLastUser.end())
    return;

  auto &LU = DMI->second;
  LastUses.append(LU.begin(), LU.end());
}

AnalysisUsage *PMTopLevelManager::findAnalysisUsage(Pass *P) {
  AnalysisUsage *AnUsage = nullptr;
  auto DMI = AnUsageMap.find(P);
  if (DMI != AnUsageMap.end())
    AnUsage = DMI->second;
  else {
    // Look up the analysis usage from the pass instance (different instances
    // of the same pass can produce different results), but unique the
    // resulting object to reduce memory usage.  This helps to greatly reduce
    // memory usage when we have many instances of only a few pass types
    // (e.g. instcombine, simplifycfg, etc...) which tend to share a fixed set
    // of dependencies.
    AnalysisUsage AU;
    P->getAnalysisUsage(AU);

    AUFoldingSetNode* Node = nullptr;
    FoldingSetNodeID ID;
    AUFoldingSetNode::Profile(ID, AU);
    void *IP = nullptr;
    if (auto *N = UniqueAnalysisUsages.FindNodeOrInsertPos(ID, IP))
      Node = N;
    else {
      Node = new (AUFoldingSetNodeAllocator.Allocate()) AUFoldingSetNode(AU);
      UniqueAnalysisUsages.InsertNode(Node, IP);
    }
    assert(Node && "cached analysis usage must be non null");

    AnUsageMap[P] = &Node->AU;
    AnUsage = &Node->AU;
  }
  return AnUsage;
}

/// Schedule pass P for execution. Make sure that passes required by
/// P are run before P is run. Update analysis info maintained by
/// the manager. Remove dead passes. This is a recursive function.
void PMTopLevelManager::schedulePass(Pass *P) {

  // TODO : Allocate function manager for this pass, other wise required set
  // may be inserted into previous function manager

  // Give pass a chance to prepare the stage.
  P->preparePassManager(activeStack);

  // If P is an analysis pass and it is available then do not
  // generate the analysis again. Stale analysis info should not be
  // available at this point.
  const PassInfo *PI = findAnalysisPassInfo(P->getPassID());
  if (PI && PI->isAnalysis() && findAnalysisPass(P->getPassID())) {
    // Remove any cached AnalysisUsage information.
    AnUsageMap.erase(P);
    delete P;
    return;
  }

  AnalysisUsage *AnUsage = findAnalysisUsage(P);

  bool checkAnalysis = true;
  while (checkAnalysis) {
    checkAnalysis = false;

    const AnalysisUsage::VectorType &RequiredSet = AnUsage->getRequiredSet();
    for (const AnalysisID ID : RequiredSet) {

      Pass *AnalysisPass = findAnalysisPass(ID);
      if (!AnalysisPass) {
        const PassInfo *PI = findAnalysisPassInfo(ID);

        if (!PI) {
          // Pass P is not in the global PassRegistry
          dbgs() << "Pass '"  << P->getPassName() << "' is not initialized." << "\n";
          dbgs() << "Verify if there is a pass dependency cycle." << "\n";
          dbgs() << "Required Passes:" << "\n";
          for (const AnalysisID ID2 : RequiredSet) {
            if (ID == ID2)
              break;
            Pass *AnalysisPass2 = findAnalysisPass(ID2);
            if (AnalysisPass2) {
              dbgs() << "\t" << AnalysisPass2->getPassName() << "\n";
            } else {
              dbgs() << "\t"   << "Error: Required pass not found! Possible causes:"  << "\n";
              dbgs() << "\t\t" << "- Pass misconfiguration (e.g.: missing macros)"    << "\n";
              dbgs() << "\t\t" << "- Corruption of the global PassRegistry"           << "\n";
            }
          }
        }

        assert(PI && "Expected required passes to be initialized");
        AnalysisPass = PI->createPass();
        if (P->getPotentialPassManagerType () ==
            AnalysisPass->getPotentialPassManagerType())
          // Schedule analysis pass that is managed by the same pass manager.
          schedulePass(AnalysisPass);
        else if (P->getPotentialPassManagerType () >
                 AnalysisPass->getPotentialPassManagerType()) {
          // Schedule analysis pass that is managed by a new manager.
          schedulePass(AnalysisPass);
          // Recheck analysis passes to ensure that required analyses that
          // are already checked are still available.
          checkAnalysis = true;
        } else
          // Do not schedule this analysis. Lower level analysis
          // passes are run on the fly.
          delete AnalysisPass;
      }
    }
  }

  // Now all required passes are available.
  if (ImmutablePass *IP = P->getAsImmutablePass()) {
    // P is a immutable pass and it will be managed by this
    // top level manager. Set up analysis resolver to connect them.
    PMDataManager *DM = getAsPMDataManager();
    AnalysisResolver *AR = new AnalysisResolver(*DM);
    P->setResolver(AR);
    DM->initializeAnalysisImpl(P);
    addImmutablePass(IP);
    DM->recordAvailableAnalysis(IP);
    return;
  }

  if (PI && !PI->isAnalysis() && shouldPrintBeforePass(PI->getPassArgument())) {
    Pass *PP =
        P->createPrinterPass(dbgs(), ("*** IR Dump Before " + P->getPassName() +
                                      " (" + PI->getPassArgument() + ") ***")
                                         .str());
    PP->assignPassManager(activeStack, getTopLevelPassManagerType());
  }

  // Add the requested pass to the best available pass manager.
  P->assignPassManager(activeStack, getTopLevelPassManagerType());

  if (PI && !PI->isAnalysis() && shouldPrintAfterPass(PI->getPassArgument())) {
    Pass *PP =
        P->createPrinterPass(dbgs(), ("*** IR Dump After " + P->getPassName() +
                                      " (" + PI->getPassArgument() + ") ***")
                                         .str());
    PP->assignPassManager(activeStack, getTopLevelPassManagerType());
  }
}

/// Find the pass that implements Analysis AID. Search immutable
/// passes and all pass managers. If desired pass is not found
/// then return NULL.
Pass *PMTopLevelManager::findAnalysisPass(AnalysisID AID) {
  // For immutable passes we have a direct mapping from ID to pass, so check
  // that first.
  if (Pass *P = ImmutablePassMap.lookup(AID))
    return P;

  // Check pass managers
  for (PMDataManager *PassManager : PassManagers)
    if (Pass *P = PassManager->findAnalysisPass(AID, false))
      return P;

  // Check other pass managers
  for (PMDataManager *IndirectPassManager : IndirectPassManagers)
    if (Pass *P = IndirectPassManager->findAnalysisPass(AID, false))
      return P;

  return nullptr;
}

const PassInfo *PMTopLevelManager::findAnalysisPassInfo(AnalysisID AID) const {
  const PassInfo *&PI = AnalysisPassInfos[AID];
  if (!PI)
    PI = PassRegistry::getPassRegistry()->getPassInfo(AID);
  else
    assert(PI == PassRegistry::getPassRegistry()->getPassInfo(AID) &&
           "The pass info pointer changed for an analysis ID!");

  return PI;
}

void PMTopLevelManager::addImmutablePass(ImmutablePass *P) {
  P->initializePass();
  ImmutablePasses.push_back(P);

  // Add this pass to the map from its analysis ID. We clobber any prior runs
  // of the pass in the map so that the last one added is the one found when
  // doing lookups.
  AnalysisID AID = P->getPassID();
  ImmutablePassMap[AID] = P;

  // Also add any interfaces implemented by the immutable pass to the map for
  // fast lookup.
  const PassInfo *PassInf = findAnalysisPassInfo(AID);
  assert(PassInf && "Expected all immutable passes to be initialized");
  for (const PassInfo *ImmPI : PassInf->getInterfacesImplemented())
    ImmutablePassMap[ImmPI->getTypeInfo()] = P;
}

// Print passes managed by this top level manager.
void PMTopLevelManager::dumpPasses() const {

  if (PassDebugging < Structure)
    return;

  // Print out the immutable passes
  for (ImmutablePass *Pass : ImmutablePasses)
    Pass->dumpPassStructure(0);

  // Every class that derives from PMDataManager also derives from Pass
  // (sometimes indirectly), but there's no inheritance relationship
  // between PMDataManager and Pass, so we have to getAsPass to get
  // from a PMDataManager* to a Pass*.
  for (PMDataManager *Manager : PassManagers)
    Manager->getAsPass()->dumpPassStructure(1);
}

void PMTopLevelManager::dumpArguments() const {

  if (PassDebugging < Arguments)
    return;

  dbgs() << "Pass Arguments: ";
  for (ImmutablePass *P : ImmutablePasses)
    if (const PassInfo *PI = findAnalysisPassInfo(P->getPassID())) {
      assert(PI && "Expected all immutable passes to be initialized");
      if (!PI->isAnalysisGroup())
        dbgs() << " -" << PI->getPassArgument();
    }
  for (PMDataManager *PM : PassManagers)
    PM->dumpPassArguments();
  dbgs() << "\n";
}

void PMTopLevelManager::initializeAllAnalysisInfo() {
  for (PMDataManager *PM : PassManagers)
    PM->initializeAnalysisInfo();

  // Initailize other pass managers
  for (PMDataManager *IPM : IndirectPassManagers)
    IPM->initializeAnalysisInfo();
}

/// Destructor
PMTopLevelManager::~PMTopLevelManager() {
  for (PMDataManager *PM : PassManagers)
    delete PM;

  for (ImmutablePass *P : ImmutablePasses)
    delete P;
}

//===----------------------------------------------------------------------===//
// PMDataManager implementation

/// Augement AvailableAnalysis by adding analysis made available by pass P.
void PMDataManager::recordAvailableAnalysis(Pass *P) {
  AnalysisID PI = P->getPassID();

  AvailableAnalysis[PI] = P;

  assert(!AvailableAnalysis.empty());

  // This pass is the current implementation of all of the interfaces it
  // implements as well.
  const PassInfo *PInf = TPM->findAnalysisPassInfo(PI);
  if (!PInf) return;
  for (const PassInfo *PI : PInf->getInterfacesImplemented())
    AvailableAnalysis[PI->getTypeInfo()] = P;
}

// Return true if P preserves high level analysis used by other
// passes managed by this manager
bool PMDataManager::preserveHigherLevelAnalysis(Pass *P) {
  AnalysisUsage *AnUsage = TPM->findAnalysisUsage(P);
  if (AnUsage->getPreservesAll())
    return true;

  const AnalysisUsage::VectorType &PreservedSet = AnUsage->getPreservedSet();
  for (Pass *P1 : HigherLevelAnalysis) {
    if (P1->getAsImmutablePass() == nullptr &&
        !is_contained(PreservedSet, P1->getPassID()))
      return false;
  }

  return true;
}

/// verifyPreservedAnalysis -- Verify analysis preserved by pass P.
void PMDataManager::verifyPreservedAnalysis(Pass *P) {
  // Don't do this unless assertions are enabled.
#ifdef NDEBUG
  return;
#endif
  AnalysisUsage *AnUsage = TPM->findAnalysisUsage(P);
  const AnalysisUsage::VectorType &PreservedSet = AnUsage->getPreservedSet();

  // Verify preserved analysis
  for (AnalysisID AID : PreservedSet) {
    if (Pass *AP = findAnalysisPass(AID, true)) {
      TimeRegion PassTimer(getPassTimer(AP));
      AP->verifyAnalysis();
    }
  }
}

/// Remove Analysis not preserved by Pass P
void PMDataManager::removeNotPreservedAnalysis(Pass *P) {
  AnalysisUsage *AnUsage = TPM->findAnalysisUsage(P);
  if (AnUsage->getPreservesAll())
    return;

  const AnalysisUsage::VectorType &PreservedSet = AnUsage->getPreservedSet();
  for (DenseMap<AnalysisID, Pass*>::iterator I = AvailableAnalysis.begin(),
         E = AvailableAnalysis.end(); I != E; ) {
    DenseMap<AnalysisID, Pass*>::iterator Info = I++;
    if (Info->second->getAsImmutablePass() == nullptr &&
        !is_contained(PreservedSet, Info->first)) {
      // Remove this analysis
      if (PassDebugging >= Details) {
        Pass *S = Info->second;
        dbgs() << " -- '" <<  P->getPassName() << "' is not preserving '";
        dbgs() << S->getPassName() << "'\n";
      }
      AvailableAnalysis.erase(Info);
    }
  }

  // Check inherited analysis also. If P is not preserving analysis
  // provided by parent manager then remove it here.
  for (DenseMap<AnalysisID, Pass *> *IA : InheritedAnalysis) {
    if (!IA)
      continue;

    for (DenseMap<AnalysisID, Pass *>::iterator I = IA->begin(),
                                                E = IA->end();
         I != E;) {
      DenseMap<AnalysisID, Pass *>::iterator Info = I++;
      if (Info->second->getAsImmutablePass() == nullptr &&
          !is_contained(PreservedSet, Info->first)) {
        // Remove this analysis
        if (PassDebugging >= Details) {
          Pass *S = Info->second;
          dbgs() << " -- '" <<  P->getPassName() << "' is not preserving '";
          dbgs() << S->getPassName() << "'\n";
        }
        IA->erase(Info);
      }
    }
  }
}

/// Remove analysis passes that are not used any longer
void PMDataManager::removeDeadPasses(Pass *P, StringRef Msg,
                                     enum PassDebuggingString DBG_STR) {

  SmallVector<Pass *, 12> DeadPasses;

  // If this is a on the fly manager then it does not have TPM.
  if (!TPM)
    return;

  TPM->collectLastUses(DeadPasses, P);

  if (PassDebugging >= Details && !DeadPasses.empty()) {
    dbgs() << " -*- '" <<  P->getPassName();
    dbgs() << "' is the last user of following pass instances.";
    dbgs() << " Free these instances\n";
  }

  for (Pass *P : DeadPasses)
    freePass(P, Msg, DBG_STR);
}

void PMDataManager::freePass(Pass *P, StringRef Msg,
                             enum PassDebuggingString DBG_STR) {
  dumpPassInfo(P, FREEING_MSG, DBG_STR, Msg);

  {
    // If the pass crashes releasing memory, remember this.
    PassManagerPrettyStackEntry X(P);
    TimeRegion PassTimer(getPassTimer(P));

    P->releaseMemory();
  }

  AnalysisID PI = P->getPassID();
  if (const PassInfo *PInf = TPM->findAnalysisPassInfo(PI)) {
    // Remove the pass itself (if it is not already removed).
    AvailableAnalysis.erase(PI);

    // Remove all interfaces this pass implements, for which it is also
    // listed as the available implementation.
    for (const PassInfo *PI : PInf->getInterfacesImplemented()) {
      DenseMap<AnalysisID, Pass *>::iterator Pos =
          AvailableAnalysis.find(PI->getTypeInfo());
      if (Pos != AvailableAnalysis.end() && Pos->second == P)
        AvailableAnalysis.erase(Pos);
    }
  }
}

/// Add pass P into the PassVector. Update
/// AvailableAnalysis appropriately if ProcessAnalysis is true.
void PMDataManager::add(Pass *P, bool ProcessAnalysis) {
  // This manager is going to manage pass P. Set up analysis resolver
  // to connect them.
  AnalysisResolver *AR = new AnalysisResolver(*this);
  P->setResolver(AR);

  // If a FunctionPass F is the last user of ModulePass info M
  // then the F's manager, not F, records itself as a last user of M.
  SmallVector<Pass *, 12> TransferLastUses;

  if (!ProcessAnalysis) {
    // Add pass
    PassVector.push_back(P);
    return;
  }

  // At the moment, this pass is the last user of all required passes.
  SmallVector<Pass *, 12> LastUses;
  SmallVector<Pass *, 8> UsedPasses;
  SmallVector<AnalysisID, 8> ReqAnalysisNotAvailable;

  unsigned PDepth = this->getDepth();

  collectRequiredAndUsedAnalyses(UsedPasses, ReqAnalysisNotAvailable, P);
  for (Pass *PUsed : UsedPasses) {
    unsigned RDepth = 0;

    assert(PUsed->getResolver() && "Analysis Resolver is not set");
    PMDataManager &DM = PUsed->getResolver()->getPMDataManager();
    RDepth = DM.getDepth();

    if (PDepth == RDepth)
      LastUses.push_back(PUsed);
    else if (PDepth > RDepth) {
      // Let the parent claim responsibility of last use
      TransferLastUses.push_back(PUsed);
      // Keep track of higher level analysis used by this manager.
      HigherLevelAnalysis.push_back(PUsed);
    } else
      llvm_unreachable("Unable to accommodate Used Pass");
  }

  // Set P as P's last user until someone starts using P.
  // However, if P is a Pass Manager then it does not need
  // to record its last user.
  if (!P->getAsPMDataManager())
    LastUses.push_back(P);
  TPM->setLastUser(LastUses, P);

  if (!TransferLastUses.empty()) {
    Pass *My_PM = getAsPass();
    TPM->setLastUser(TransferLastUses, My_PM);
    TransferLastUses.clear();
  }

  // Now, take care of required analyses that are not available.
  for (AnalysisID ID : ReqAnalysisNotAvailable) {
    const PassInfo *PI = TPM->findAnalysisPassInfo(ID);
    Pass *AnalysisPass = PI->createPass();
    this->addLowerLevelRequiredPass(P, AnalysisPass);
  }

  // Take a note of analysis required and made available by this pass.
  // Remove the analysis not preserved by this pass
  removeNotPreservedAnalysis(P);
  recordAvailableAnalysis(P);

  // Add pass
  PassVector.push_back(P);
}


/// Populate UP with analysis pass that are used or required by
/// pass P and are available. Populate RP_NotAvail with analysis
/// pass that are required by pass P but are not available.
void PMDataManager::collectRequiredAndUsedAnalyses(
    SmallVectorImpl<Pass *> &UP, SmallVectorImpl<AnalysisID> &RP_NotAvail,
    Pass *P) {
  AnalysisUsage *AnUsage = TPM->findAnalysisUsage(P);

  for (const auto &UsedID : AnUsage->getUsedSet())
    if (Pass *AnalysisPass = findAnalysisPass(UsedID, true))
      UP.push_back(AnalysisPass);

  for (const auto &RequiredID : AnUsage->getRequiredSet())
    if (Pass *AnalysisPass = findAnalysisPass(RequiredID, true))
      UP.push_back(AnalysisPass);
    else
      RP_NotAvail.push_back(RequiredID);
}

// All Required analyses should be available to the pass as it runs!  Here
// we fill in the AnalysisImpls member of the pass so that it can
// successfully use the getAnalysis() method to retrieve the
// implementations it needs.
//
void PMDataManager::initializeAnalysisImpl(Pass *P) {
  AnalysisUsage *AnUsage = TPM->findAnalysisUsage(P);

  for (const AnalysisID ID : AnUsage->getRequiredSet()) {
    Pass *Impl = findAnalysisPass(ID, true);
    if (!Impl)
      // This may be analysis pass that is initialized on the fly.
      // If that is not the case then it will raise an assert when it is used.
      continue;
    AnalysisResolver *AR = P->getResolver();
    assert(AR && "Analysis Resolver is not set");
    AR->addAnalysisImplsPair(ID, Impl);
  }
}

/// Find the pass that implements Analysis AID. If desired pass is not found
/// then return NULL.
Pass *PMDataManager::findAnalysisPass(AnalysisID AID, bool SearchParent) {

  // Check if AvailableAnalysis map has one entry.
  DenseMap<AnalysisID, Pass*>::const_iterator I =  AvailableAnalysis.find(AID);

  if (I != AvailableAnalysis.end())
    return I->second;

  // Search Parents through TopLevelManager
  if (SearchParent)
    return TPM->findAnalysisPass(AID);

  return nullptr;
}

// Print list of passes that are last used by P.
void PMDataManager::dumpLastUses(Pass *P, unsigned Offset) const{
  if (PassDebugging < Details)
    return;

  SmallVector<Pass *, 12> LUses;

  // If this is a on the fly manager then it does not have TPM.
  if (!TPM)
    return;

  TPM->collectLastUses(LUses, P);

  for (Pass *P : LUses) {
    dbgs() << "--" << std::string(Offset*2, ' ');
    P->dumpPassStructure(0);
  }
}

void PMDataManager::dumpPassArguments() const {
  for (Pass *P : PassVector) {
    if (PMDataManager *PMD = P->getAsPMDataManager())
      PMD->dumpPassArguments();
    else
      if (const PassInfo *PI =
            TPM->findAnalysisPassInfo(P->getPassID()))
        if (!PI->isAnalysisGroup())
          dbgs() << " -" << PI->getPassArgument();
  }
}

void PMDataManager::dumpPassInfo(Pass *P, enum PassDebuggingString S1,
                                 enum PassDebuggingString S2,
                                 StringRef Msg) {
  if (PassDebugging < Executions)
    return;
  dbgs() << "[" << std::chrono::system_clock::now() << "] " << (void *)this
         << std::string(getDepth() * 2 + 1, ' ');
  switch (S1) {
  case EXECUTION_MSG:
    dbgs() << "Executing Pass '" << P->getPassName();
    break;
  case MODIFICATION_MSG:
    dbgs() << "Made Modification '" << P->getPassName();
    break;
  case FREEING_MSG:
    dbgs() << " Freeing Pass '" << P->getPassName();
    break;
  default:
    break;
  }
  switch (S2) {
  case ON_FUNCTION_MSG:
    dbgs() << "' on Function '" << Msg << "'...\n";
    break;
  case ON_MODULE_MSG:
    dbgs() << "' on Module '"  << Msg << "'...\n";
    break;
  case ON_REGION_MSG:
    dbgs() << "' on Region '"  << Msg << "'...\n";
    break;
  case ON_LOOP_MSG:
    dbgs() << "' on Loop '" << Msg << "'...\n";
    break;
  case ON_CG_MSG:
    dbgs() << "' on Call Graph Nodes '" << Msg << "'...\n";
    break;
  default:
    break;
  }
}

void PMDataManager::dumpRequiredSet(const Pass *P) const {
  if (PassDebugging < Details)
    return;

  AnalysisUsage analysisUsage;
  P->getAnalysisUsage(analysisUsage);
  dumpAnalysisUsage("Required", P, analysisUsage.getRequiredSet());
}

void PMDataManager::dumpPreservedSet(const Pass *P) const {
  if (PassDebugging < Details)
    return;

  AnalysisUsage analysisUsage;
  P->getAnalysisUsage(analysisUsage);
  dumpAnalysisUsage("Preserved", P, analysisUsage.getPreservedSet());
}

void PMDataManager::dumpUsedSet(const Pass *P) const {
  if (PassDebugging < Details)
    return;

  AnalysisUsage analysisUsage;
  P->getAnalysisUsage(analysisUsage);
  dumpAnalysisUsage("Used", P, analysisUsage.getUsedSet());
}

void PMDataManager::dumpAnalysisUsage(StringRef Msg, const Pass *P,
                                   const AnalysisUsage::VectorType &Set) const {
  assert(PassDebugging >= Details);
  if (Set.empty())
    return;
  dbgs() << (const void*)P << std::string(getDepth()*2+3, ' ') << Msg << " Analyses:";
  for (unsigned i = 0; i != Set.size(); ++i) {
    if (i) dbgs() << ',';
    const PassInfo *PInf = TPM->findAnalysisPassInfo(Set[i]);
    if (!PInf) {
      // Some preserved passes, such as AliasAnalysis, may not be initialized by
      // all drivers.
      dbgs() << " Uninitialized Pass";
      continue;
    }
    dbgs() << ' ' << PInf->getPassName();
  }
  dbgs() << '\n';
}

/// Add RequiredPass into list of lower level passes required by pass P.
/// RequiredPass is run on the fly by Pass Manager when P requests it
/// through getAnalysis interface.
/// This should be handled by specific pass manager.
void PMDataManager::addLowerLevelRequiredPass(Pass *P, Pass *RequiredPass) {
  if (TPM) {
    TPM->dumpArguments();
    TPM->dumpPasses();
  }

  // Module Level pass may required Function Level analysis info
  // (e.g. dominator info). Pass manager uses on the fly function pass manager
  // to provide this on demand. In that case, in Pass manager terminology,
  // module level pass is requiring lower level analysis info managed by
  // lower level pass manager.

  // When Pass manager is not able to order required analysis info, Pass manager
  // checks whether any lower level manager will be able to provide this
  // analysis info on demand or not.
#ifndef NDEBUG
  dbgs() << "Unable to schedule '" << RequiredPass->getPassName();
  dbgs() << "' required by '" << P->getPassName() << "'\n";
#endif
  llvm_unreachable("Unable to schedule pass");
}

std::tuple<Pass *, bool> PMDataManager::getOnTheFlyPass(Pass *P, AnalysisID PI,
                                                        Function &F) {
  llvm_unreachable("Unable to find on the fly pass");
}

// Destructor
PMDataManager::~PMDataManager() {
  for (Pass *P : PassVector)
    delete P;
}

//===----------------------------------------------------------------------===//
// NOTE: Is this the right place to define this method ?
// getAnalysisIfAvailable - Return analysis result or null if it doesn't exist.
Pass *AnalysisResolver::getAnalysisIfAvailable(AnalysisID ID) const {
  return PM.findAnalysisPass(ID, true);
}

std::tuple<Pass *, bool>
AnalysisResolver::findImplPass(Pass *P, AnalysisID AnalysisPI, Function &F) {
  return PM.getOnTheFlyPass(P, AnalysisPI, F);
}

namespace llvm {
namespace legacy {

//===----------------------------------------------------------------------===//
// FunctionPassManager implementation

/// Create new Function pass manager
FunctionPassManager::FunctionPassManager(Module *m) : M(m) {
  FPM = new legacy::FunctionPassManagerImpl();
  // FPM is the top level manager.
  FPM->setTopLevelManager(FPM);

  AnalysisResolver *AR = new AnalysisResolver(*FPM);
  FPM->setResolver(AR);
}

FunctionPassManager::~FunctionPassManager() {
  delete FPM;
}

void FunctionPassManager::add(Pass *P) {
  FPM->add(P);
}

/// run - Execute all of the passes scheduled for execution.  Keep
/// track of whether any of the passes modifies the function, and if
/// so, return true.
///
bool FunctionPassManager::run(Function &F) {
  handleAllErrors(F.materialize(), [&](ErrorInfoBase &EIB) {
    report_fatal_error(Twine("Error reading bitcode file: ") + EIB.message());
  });
  return FPM->run(F);
}


/// doInitialization - Run all of the initializers for the function passes.
///
bool FunctionPassManager::doInitialization() {
  return FPM->doInitialization(*M);
}

/// doFinalization - Run all of the finalizers for the function passes.
///
bool FunctionPassManager::doFinalization() {
  return FPM->doFinalization(*M);
}
} // namespace legacy
} // namespace llvm

/// cleanup - After running all passes, clean up pass manager cache.
void FPPassManager::cleanup() {
 for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    FunctionPass *FP = getContainedPass(Index);
    AnalysisResolver *AR = FP->getResolver();
    assert(AR && "Analysis Resolver is not set");
    AR->clearAnalysisImpls();
 }
}


//===----------------------------------------------------------------------===//
// FPPassManager implementation

char FPPassManager::ID = 0;
/// Print passes managed by this manager
void FPPassManager::dumpPassStructure(unsigned Offset) {
  dbgs().indent(Offset*2) << "FunctionPass Manager\n";
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    FunctionPass *FP = getContainedPass(Index);
    FP->dumpPassStructure(Offset + 1);
    dumpLastUses(FP, Offset+1);
  }
}

/// Execute all of the passes scheduled for execution by invoking
/// runOnFunction method.  Keep track of whether any of the passes modifies
/// the function, and if so, return true.
bool FPPassManager::runOnFunction(Function &F) {
  if (F.isDeclaration())
    return false;

  bool Changed = false;
  Module &M = *F.getParent();
  // Collect inherited analysis from Module level pass manager.
  populateInheritedAnalysis(TPM->activeStack);

  unsigned InstrCount, FunctionSize = 0;
  StringMap<std::pair<unsigned, unsigned>> FunctionToInstrCount;
  bool EmitICRemark = M.shouldEmitInstrCountChangedRemark();
  // Collect the initial size of the module.
  if (EmitICRemark) {
    InstrCount = initSizeRemarkInfo(M, FunctionToInstrCount);
    FunctionSize = F.getInstructionCount();
  }

  // Store name outside of loop to avoid redundant calls.
  const StringRef Name = F.getName();
  llvm::TimeTraceScope FunctionScope("OptFunction", Name);

  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    FunctionPass *FP = getContainedPass(Index);
    bool LocalChanged = false;

    // Call getPassName only when required. The call itself is fairly cheap, but
    // still virtual and repeated calling adds unnecessary overhead.
    llvm::TimeTraceScope PassScope(
        "RunPass", [FP]() { return std::string(FP->getPassName()); });

    dumpPassInfo(FP, EXECUTION_MSG, ON_FUNCTION_MSG, Name);
    dumpRequiredSet(FP);

    initializeAnalysisImpl(FP);

    {
      PassManagerPrettyStackEntry X(FP, F);
      TimeRegion PassTimer(getPassTimer(FP));
#ifdef EXPENSIVE_CHECKS
      uint64_t RefHash = FP->structuralHash(F);
#endif
      LocalChanged |= FP->runOnFunction(F);

#if defined(EXPENSIVE_CHECKS) && !defined(NDEBUG)
      if (!LocalChanged && (RefHash != FP->structuralHash(F))) {
        llvm::errs() << "Pass modifies its input and doesn't report it: "
                     << FP->getPassName() << "\n";
        llvm_unreachable("Pass modifies its input and doesn't report it");
      }
#endif

      if (EmitICRemark) {
        unsigned NewSize = F.getInstructionCount();

        // Update the size of the function, emit a remark, and update the size
        // of the module.
        if (NewSize != FunctionSize) {
          int64_t Delta = static_cast<int64_t>(NewSize) -
                          static_cast<int64_t>(FunctionSize);
          emitInstrCountChangedRemark(FP, M, Delta, InstrCount,
                                      FunctionToInstrCount, &F);
          InstrCount = static_cast<int64_t>(InstrCount) + Delta;
          FunctionSize = NewSize;
        }
      }
    }

    Changed |= LocalChanged;
    if (LocalChanged)
      dumpPassInfo(FP, MODIFICATION_MSG, ON_FUNCTION_MSG, Name);
    dumpPreservedSet(FP);
    dumpUsedSet(FP);

    verifyPreservedAnalysis(FP);
    if (LocalChanged)
      removeNotPreservedAnalysis(FP);
    recordAvailableAnalysis(FP);
    removeDeadPasses(FP, Name, ON_FUNCTION_MSG);
  }

  return Changed;
}

bool FPPassManager::runOnModule(Module &M) {
  bool Changed = false;

  for (Function &F : M)
    Changed |= runOnFunction(F);

  return Changed;
}

bool FPPassManager::doInitialization(Module &M) {
  bool Changed = false;

  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index)
    Changed |= getContainedPass(Index)->doInitialization(M);

  return Changed;
}

bool FPPassManager::doFinalization(Module &M) {
  bool Changed = false;

  for (int Index = getNumContainedPasses() - 1; Index >= 0; --Index)
    Changed |= getContainedPass(Index)->doFinalization(M);

  return Changed;
}

//===----------------------------------------------------------------------===//
// MPPassManager implementation

/// Execute all of the passes scheduled for execution by invoking
/// runOnModule method.  Keep track of whether any of the passes modifies
/// the module, and if so, return true.
bool
MPPassManager::runOnModule(Module &M) {
  llvm::TimeTraceScope TimeScope("OptModule", M.getName());

  bool Changed = false;

  // Initialize on-the-fly passes
  for (auto &OnTheFlyManager : OnTheFlyManagers) {
    legacy::FunctionPassManagerImpl *FPP = OnTheFlyManager.second;
    Changed |= FPP->doInitialization(M);
  }

  // Initialize module passes
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index)
    Changed |= getContainedPass(Index)->doInitialization(M);

  unsigned InstrCount;
  StringMap<std::pair<unsigned, unsigned>> FunctionToInstrCount;
  bool EmitICRemark = M.shouldEmitInstrCountChangedRemark();
  // Collect the initial size of the module.
  if (EmitICRemark)
    InstrCount = initSizeRemarkInfo(M, FunctionToInstrCount);

  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    ModulePass *MP = getContainedPass(Index);
    bool LocalChanged = false;

    dumpPassInfo(MP, EXECUTION_MSG, ON_MODULE_MSG, M.getModuleIdentifier());
    dumpRequiredSet(MP);

    initializeAnalysisImpl(MP);

    {
      PassManagerPrettyStackEntry X(MP, M);
      TimeRegion PassTimer(getPassTimer(MP));

#ifdef EXPENSIVE_CHECKS
      uint64_t RefHash = MP->structuralHash(M);
#endif

      LocalChanged |= MP->runOnModule(M);

#ifdef EXPENSIVE_CHECKS
      assert((LocalChanged || (RefHash == MP->structuralHash(M))) &&
             "Pass modifies its input and doesn't report it.");
#endif

      if (EmitICRemark) {
        // Update the size of the module.
        unsigned ModuleCount = M.getInstructionCount();
        if (ModuleCount != InstrCount) {
          int64_t Delta = static_cast<int64_t>(ModuleCount) -
                          static_cast<int64_t>(InstrCount);
          emitInstrCountChangedRemark(MP, M, Delta, InstrCount,
                                      FunctionToInstrCount);
          InstrCount = ModuleCount;
        }
      }
    }

    Changed |= LocalChanged;
    if (LocalChanged)
      dumpPassInfo(MP, MODIFICATION_MSG, ON_MODULE_MSG,
                   M.getModuleIdentifier());
    dumpPreservedSet(MP);
    dumpUsedSet(MP);

    verifyPreservedAnalysis(MP);
    if (LocalChanged)
      removeNotPreservedAnalysis(MP);
    recordAvailableAnalysis(MP);
    removeDeadPasses(MP, M.getModuleIdentifier(), ON_MODULE_MSG);
  }

  // Finalize module passes
  for (int Index = getNumContainedPasses() - 1; Index >= 0; --Index)
    Changed |= getContainedPass(Index)->doFinalization(M);

  // Finalize on-the-fly passes
  for (auto &OnTheFlyManager : OnTheFlyManagers) {
    legacy::FunctionPassManagerImpl *FPP = OnTheFlyManager.second;
    // We don't know when is the last time an on-the-fly pass is run,
    // so we need to releaseMemory / finalize here
    FPP->releaseMemoryOnTheFly();
    Changed |= FPP->doFinalization(M);
  }

  return Changed;
}

/// Add RequiredPass into list of lower level passes required by pass P.
/// RequiredPass is run on the fly by Pass Manager when P requests it
/// through getAnalysis interface.
void MPPassManager::addLowerLevelRequiredPass(Pass *P, Pass *RequiredPass) {
  assert(RequiredPass && "No required pass?");
  assert(P->getPotentialPassManagerType() == PMT_ModulePassManager &&
         "Unable to handle Pass that requires lower level Analysis pass");
  assert((P->getPotentialPassManagerType() <
          RequiredPass->getPotentialPassManagerType()) &&
         "Unable to handle Pass that requires lower level Analysis pass");

  legacy::FunctionPassManagerImpl *FPP = OnTheFlyManagers[P];
  if (!FPP) {
    FPP = new legacy::FunctionPassManagerImpl();
    // FPP is the top level manager.
    FPP->setTopLevelManager(FPP);

    OnTheFlyManagers[P] = FPP;
  }
  const PassInfo *RequiredPassPI =
      TPM->findAnalysisPassInfo(RequiredPass->getPassID());

  Pass *FoundPass = nullptr;
  if (RequiredPassPI && RequiredPassPI->isAnalysis()) {
    FoundPass =
      ((PMTopLevelManager*)FPP)->findAnalysisPass(RequiredPass->getPassID());
  }
  if (!FoundPass) {
    FoundPass = RequiredPass;
    // This should be guaranteed to add RequiredPass to the passmanager given
    // that we checked for an available analysis above.
    FPP->add(RequiredPass);
  }
  // Register P as the last user of FoundPass or RequiredPass.
  SmallVector<Pass *, 1> LU;
  LU.push_back(FoundPass);
  FPP->setLastUser(LU,  P);
}

/// Return function pass corresponding to PassInfo PI, that is
/// required by module pass MP. Instantiate analysis pass, by using
/// its runOnFunction() for function F.
std::tuple<Pass *, bool> MPPassManager::getOnTheFlyPass(Pass *MP, AnalysisID PI,
                                                        Function &F) {
  legacy::FunctionPassManagerImpl *FPP = OnTheFlyManagers[MP];
  assert(FPP && "Unable to find on the fly pass");

  FPP->releaseMemoryOnTheFly();
  bool Changed = FPP->run(F);
  return std::make_tuple(((PMTopLevelManager *)FPP)->findAnalysisPass(PI),
                         Changed);
}

namespace llvm {
namespace legacy {

//===----------------------------------------------------------------------===//
// PassManager implementation

/// Create new pass manager
PassManager::PassManager() {
  PM = new PassManagerImpl();
  // PM is the top level manager
  PM->setTopLevelManager(PM);
}

PassManager::~PassManager() {
  delete PM;
}

void PassManager::add(Pass *P) {
  PM->add(P);
}

/// run - Execute all of the passes scheduled for execution.  Keep track of
/// whether any of the passes modifies the module, and if so, return true.
bool PassManager::run(Module &M) {
  return PM->run(M);
}
} // namespace legacy
} // namespace llvm

//===----------------------------------------------------------------------===//
// PMStack implementation
//

// Pop Pass Manager from the stack and clear its analysis info.
void PMStack::pop() {

  PMDataManager *Top = this->top();
  Top->initializeAnalysisInfo();

  S.pop_back();
}

// Push PM on the stack and set its top level manager.
void PMStack::push(PMDataManager *PM) {
  assert(PM && "Unable to push. Pass Manager expected");
  assert(PM->getDepth()==0 && "Pass Manager depth set too early");

  if (!this->empty()) {
    assert(PM->getPassManagerType() > this->top()->getPassManagerType()
           && "pushing bad pass manager to PMStack");
    PMTopLevelManager *TPM = this->top()->getTopLevelManager();

    assert(TPM && "Unable to find top level manager");
    TPM->addIndirectPassManager(PM);
    PM->setTopLevelManager(TPM);
    PM->setDepth(this->top()->getDepth()+1);
  } else {
    assert((PM->getPassManagerType() == PMT_ModulePassManager
           || PM->getPassManagerType() == PMT_FunctionPassManager)
           && "pushing bad pass manager to PMStack");
    PM->setDepth(1);
  }

  S.push_back(PM);
}

// Dump content of the pass manager stack.
LLVM_DUMP_METHOD void PMStack::dump() const {
  for (PMDataManager *Manager : S)
    dbgs() << Manager->getAsPass()->getPassName() << ' ';

  if (!S.empty())
    dbgs() << '\n';
}

/// Find appropriate Module Pass Manager in the PM Stack and
/// add self into that manager.
void ModulePass::assignPassManager(PMStack &PMS,
                                   PassManagerType PreferredType) {
  // Find Module Pass Manager
  PassManagerType T;
  while ((T = PMS.top()->getPassManagerType()) > PMT_ModulePassManager &&
         T != PreferredType)
    PMS.pop();
  PMS.top()->add(this);
}

/// Find appropriate Function Pass Manager or Call Graph Pass Manager
/// in the PM Stack and add self into that manager.
void FunctionPass::assignPassManager(PMStack &PMS,
                                     PassManagerType /*PreferredType*/) {
  // Find Function Pass Manager
  PMDataManager *PM;
  while (PM = PMS.top(), PM->getPassManagerType() > PMT_FunctionPassManager)
    PMS.pop();

  // Create new Function Pass Manager if needed.
  if (PM->getPassManagerType() != PMT_FunctionPassManager) {
    // [1] Create new Function Pass Manager
    auto *FPP = new FPPassManager;
    FPP->populateInheritedAnalysis(PMS);

    // [2] Set up new manager's top level manager
    PM->getTopLevelManager()->addIndirectPassManager(FPP);

    // [3] Assign manager to manage this new manager. This may create
    // and push new managers into PMS
    FPP->assignPassManager(PMS, PM->getPassManagerType());

    // [4] Push new manager into PMS
    PMS.push(FPP);
    PM = FPP;
  }

  // Assign FPP as the manager of this pass.
  PM->add(this);
}

legacy::PassManagerBase::~PassManagerBase() = default;
