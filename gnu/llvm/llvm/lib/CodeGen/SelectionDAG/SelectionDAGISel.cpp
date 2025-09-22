//===- SelectionDAGISel.cpp - Implement the SelectionDAGISel class --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the SelectionDAGISel class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/SelectionDAGISel.h"
#include "ScheduleDAGSDNodes.h"
#include "SelectionDAGBuilder.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/CodeGen/AssignmentTrackingAnalysis.h"
#include "llvm/CodeGen/CodeGenCommonISel.h"
#include "llvm/CodeGen/FastISel.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/CodeGen/SwiftErrorValueTracking.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "isel"
#define ISEL_DUMP_DEBUG_TYPE DEBUG_TYPE "-dump"

STATISTIC(NumFastIselFailures, "Number of instructions fast isel failed on");
STATISTIC(NumFastIselSuccess, "Number of instructions fast isel selected");
STATISTIC(NumFastIselBlocks, "Number of blocks selected entirely by fast isel");
STATISTIC(NumDAGBlocks, "Number of blocks selected using DAG");
STATISTIC(NumDAGIselRetries,"Number of times dag isel has to try another path");
STATISTIC(NumEntryBlocks, "Number of entry blocks encountered");
STATISTIC(NumFastIselFailLowerArguments,
          "Number of entry blocks where fast isel failed to lower arguments");

static cl::opt<int> EnableFastISelAbort(
    "fast-isel-abort", cl::Hidden,
    cl::desc("Enable abort calls when \"fast\" instruction selection "
             "fails to lower an instruction: 0 disable the abort, 1 will "
             "abort but for args, calls and terminators, 2 will also "
             "abort for argument lowering, and 3 will never fallback "
             "to SelectionDAG."));

static cl::opt<bool> EnableFastISelFallbackReport(
    "fast-isel-report-on-fallback", cl::Hidden,
    cl::desc("Emit a diagnostic when \"fast\" instruction selection "
             "falls back to SelectionDAG."));

static cl::opt<bool>
UseMBPI("use-mbpi",
        cl::desc("use Machine Branch Probability Info"),
        cl::init(true), cl::Hidden);

#ifndef NDEBUG
static cl::opt<std::string>
FilterDAGBasicBlockName("filter-view-dags", cl::Hidden,
                        cl::desc("Only display the basic block whose name "
                                 "matches this for all view-*-dags options"));
static cl::opt<bool>
ViewDAGCombine1("view-dag-combine1-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before the first "
                   "dag combine pass"));
static cl::opt<bool>
ViewLegalizeTypesDAGs("view-legalize-types-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before legalize types"));
static cl::opt<bool>
    ViewDAGCombineLT("view-dag-combine-lt-dags", cl::Hidden,
                     cl::desc("Pop up a window to show dags before the post "
                              "legalize types dag combine pass"));
static cl::opt<bool>
    ViewLegalizeDAGs("view-legalize-dags", cl::Hidden,
                     cl::desc("Pop up a window to show dags before legalize"));
static cl::opt<bool>
ViewDAGCombine2("view-dag-combine2-dags", cl::Hidden,
          cl::desc("Pop up a window to show dags before the second "
                   "dag combine pass"));
static cl::opt<bool>
ViewISelDAGs("view-isel-dags", cl::Hidden,
          cl::desc("Pop up a window to show isel dags as they are selected"));
static cl::opt<bool>
ViewSchedDAGs("view-sched-dags", cl::Hidden,
          cl::desc("Pop up a window to show sched dags as they are processed"));
static cl::opt<bool>
ViewSUnitDAGs("view-sunit-dags", cl::Hidden,
      cl::desc("Pop up a window to show SUnit dags after they are processed"));
#else
static const bool ViewDAGCombine1 = false, ViewLegalizeTypesDAGs = false,
                  ViewDAGCombineLT = false, ViewLegalizeDAGs = false,
                  ViewDAGCombine2 = false, ViewISelDAGs = false,
                  ViewSchedDAGs = false, ViewSUnitDAGs = false;
#endif

#ifndef NDEBUG
#define ISEL_DUMP(X)                                                           \
  do {                                                                         \
    if (llvm::DebugFlag &&                                                     \
        (isCurrentDebugType(DEBUG_TYPE) ||                                     \
         (isCurrentDebugType(ISEL_DUMP_DEBUG_TYPE) && MatchFilterFuncName))) { \
      X;                                                                       \
    }                                                                          \
  } while (false)
#else
#define ISEL_DUMP(X) do { } while (false)
#endif

//===---------------------------------------------------------------------===//
///
/// RegisterScheduler class - Track the registration of instruction schedulers.
///
//===---------------------------------------------------------------------===//
MachinePassRegistry<RegisterScheduler::FunctionPassCtor>
    RegisterScheduler::Registry;

//===---------------------------------------------------------------------===//
///
/// ISHeuristic command line option for instruction schedulers.
///
//===---------------------------------------------------------------------===//
static cl::opt<RegisterScheduler::FunctionPassCtor, false,
               RegisterPassParser<RegisterScheduler>>
ISHeuristic("pre-RA-sched",
            cl::init(&createDefaultScheduler), cl::Hidden,
            cl::desc("Instruction schedulers available (before register"
                     " allocation):"));

static RegisterScheduler
defaultListDAGScheduler("default", "Best scheduler for the target",
                        createDefaultScheduler);

static bool dontUseFastISelFor(const Function &Fn) {
  // Don't enable FastISel for functions with swiftasync Arguments.
  // Debug info on those is reliant on good Argument lowering, and FastISel is
  // not capable of lowering the entire function. Mixing the two selectors tend
  // to result in poor lowering of Arguments.
  return any_of(Fn.args(), [](const Argument &Arg) {
    return Arg.hasAttribute(Attribute::AttrKind::SwiftAsync);
  });
}

namespace llvm {

  //===--------------------------------------------------------------------===//
  /// This class is used by SelectionDAGISel to temporarily override
  /// the optimization level on a per-function basis.
  class OptLevelChanger {
    SelectionDAGISel &IS;
    CodeGenOptLevel SavedOptLevel;
    bool SavedFastISel;

  public:
    OptLevelChanger(SelectionDAGISel &ISel, CodeGenOptLevel NewOptLevel)
        : IS(ISel) {
      SavedOptLevel = IS.OptLevel;
      SavedFastISel = IS.TM.Options.EnableFastISel;
      if (NewOptLevel != SavedOptLevel) {
        IS.OptLevel = NewOptLevel;
        IS.TM.setOptLevel(NewOptLevel);
        LLVM_DEBUG(dbgs() << "\nChanging optimization level for Function "
                          << IS.MF->getFunction().getName() << "\n");
        LLVM_DEBUG(dbgs() << "\tBefore: -O" << static_cast<int>(SavedOptLevel)
                          << " ; After: -O" << static_cast<int>(NewOptLevel)
                          << "\n");
        if (NewOptLevel == CodeGenOptLevel::None)
          IS.TM.setFastISel(IS.TM.getO0WantsFastISel());
      }
      if (dontUseFastISelFor(IS.MF->getFunction()))
        IS.TM.setFastISel(false);
      LLVM_DEBUG(
          dbgs() << "\tFastISel is "
                 << (IS.TM.Options.EnableFastISel ? "enabled" : "disabled")
                 << "\n");
    }

    ~OptLevelChanger() {
      if (IS.OptLevel == SavedOptLevel)
        return;
      LLVM_DEBUG(dbgs() << "\nRestoring optimization level for Function "
                        << IS.MF->getFunction().getName() << "\n");
      LLVM_DEBUG(dbgs() << "\tBefore: -O" << static_cast<int>(IS.OptLevel)
                        << " ; After: -O" << static_cast<int>(SavedOptLevel) << "\n");
      IS.OptLevel = SavedOptLevel;
      IS.TM.setOptLevel(SavedOptLevel);
      IS.TM.setFastISel(SavedFastISel);
    }
  };

  //===--------------------------------------------------------------------===//
  /// createDefaultScheduler - This creates an instruction scheduler appropriate
  /// for the target.
  ScheduleDAGSDNodes *createDefaultScheduler(SelectionDAGISel *IS,
                                             CodeGenOptLevel OptLevel) {
    const TargetLowering *TLI = IS->TLI;
    const TargetSubtargetInfo &ST = IS->MF->getSubtarget();

    // Try first to see if the Target has its own way of selecting a scheduler
    if (auto *SchedulerCtor = ST.getDAGScheduler(OptLevel)) {
      return SchedulerCtor(IS, OptLevel);
    }

    if (OptLevel == CodeGenOptLevel::None ||
        (ST.enableMachineScheduler() && ST.enableMachineSchedDefaultSched()) ||
        TLI->getSchedulingPreference() == Sched::Source)
      return createSourceListDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::RegPressure)
      return createBURRListDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::Hybrid)
      return createHybridListDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::VLIW)
      return createVLIWDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::Fast)
      return createFastDAGScheduler(IS, OptLevel);
    if (TLI->getSchedulingPreference() == Sched::Linearize)
      return createDAGLinearizer(IS, OptLevel);
    assert(TLI->getSchedulingPreference() == Sched::ILP &&
           "Unknown sched type!");
    return createILPListDAGScheduler(IS, OptLevel);
  }

} // end namespace llvm

MachineBasicBlock *
TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                            MachineBasicBlock *MBB) const {
#ifndef NDEBUG
  dbgs() << "If a target marks an instruction with "
          "'usesCustomInserter', it must implement "
          "TargetLowering::EmitInstrWithCustomInserter!\n";
#endif
  llvm_unreachable(nullptr);
}

void TargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                   SDNode *Node) const {
  assert(!MI.hasPostISelHook() &&
         "If a target marks an instruction with 'hasPostISelHook', "
         "it must implement TargetLowering::AdjustInstrPostInstrSelection!");
}

//===----------------------------------------------------------------------===//
// SelectionDAGISel code
//===----------------------------------------------------------------------===//

SelectionDAGISelLegacy::SelectionDAGISelLegacy(
    char &ID, std::unique_ptr<SelectionDAGISel> S)
    : MachineFunctionPass(ID), Selector(std::move(S)) {
  initializeGCModuleInfoPass(*PassRegistry::getPassRegistry());
  initializeBranchProbabilityInfoWrapperPassPass(
      *PassRegistry::getPassRegistry());
  initializeAAResultsWrapperPassPass(*PassRegistry::getPassRegistry());
  initializeTargetLibraryInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool SelectionDAGISelLegacy::runOnMachineFunction(MachineFunction &MF) {
  // If we already selected that function, we do not need to run SDISel.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Selected))
    return false;

  // Do some sanity-checking on the command-line options.
  if (EnableFastISelAbort && !Selector->TM.Options.EnableFastISel)
    report_fatal_error("-fast-isel-abort > 0 requires -fast-isel");

  // Decide what flavour of variable location debug-info will be used, before
  // we change the optimisation level.
  MF.setUseDebugInstrRef(MF.shouldUseDebugInstrRef());

  // Reset the target options before resetting the optimization
  // level below.
  // FIXME: This is a horrible hack and should be processed via
  // codegen looking at the optimization level explicitly when
  // it wants to look at it.
  Selector->TM.resetTargetOptions(MF.getFunction());
  // Reset OptLevel to None for optnone functions.
  CodeGenOptLevel NewOptLevel = skipFunction(MF.getFunction())
                                    ? CodeGenOptLevel::None
                                    : Selector->OptLevel;

  Selector->MF = &MF;
  OptLevelChanger OLC(*Selector, NewOptLevel);
  Selector->initializeAnalysisResults(*this);
  return Selector->runOnMachineFunction(MF);
}

SelectionDAGISel::SelectionDAGISel(TargetMachine &tm, CodeGenOptLevel OL)
    : TM(tm), FuncInfo(new FunctionLoweringInfo()),
      SwiftError(new SwiftErrorValueTracking()),
      CurDAG(new SelectionDAG(tm, OL)),
      SDB(std::make_unique<SelectionDAGBuilder>(*CurDAG, *FuncInfo, *SwiftError,
                                                OL)),
      OptLevel(OL) {
  initializeGCModuleInfoPass(*PassRegistry::getPassRegistry());
  initializeBranchProbabilityInfoWrapperPassPass(
      *PassRegistry::getPassRegistry());
  initializeAAResultsWrapperPassPass(*PassRegistry::getPassRegistry());
  initializeTargetLibraryInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

SelectionDAGISel::~SelectionDAGISel() {
  delete CurDAG;
  delete SwiftError;
}

void SelectionDAGISelLegacy::getAnalysisUsage(AnalysisUsage &AU) const {
  CodeGenOptLevel OptLevel = Selector->OptLevel;
  if (OptLevel != CodeGenOptLevel::None)
      AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<GCModuleInfo>();
  AU.addRequired<StackProtector>();
  AU.addPreserved<GCModuleInfo>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
#ifndef NDEBUG
  AU.addRequired<TargetTransformInfoWrapperPass>();
#endif
  AU.addRequired<AssumptionCacheTracker>();
  if (UseMBPI && OptLevel != CodeGenOptLevel::None)
      AU.addRequired<BranchProbabilityInfoWrapperPass>();
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  // AssignmentTrackingAnalysis only runs if assignment tracking is enabled for
  // the module.
  AU.addRequired<AssignmentTrackingAnalysis>();
  AU.addPreserved<AssignmentTrackingAnalysis>();
  if (OptLevel != CodeGenOptLevel::None)
      LazyBlockFrequencyInfoPass::getLazyBFIAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

static void computeUsesMSVCFloatingPoint(const Triple &TT, const Function &F,
                                         MachineModuleInfo &MMI) {
  // Only needed for MSVC
  if (!TT.isWindowsMSVCEnvironment())
    return;

  // If it's already set, nothing to do.
  if (MMI.usesMSVCFloatingPoint())
    return;

  for (const Instruction &I : instructions(F)) {
    if (I.getType()->isFPOrFPVectorTy()) {
      MMI.setUsesMSVCFloatingPoint(true);
      return;
    }
    for (const auto &Op : I.operands()) {
      if (Op->getType()->isFPOrFPVectorTy()) {
        MMI.setUsesMSVCFloatingPoint(true);
        return;
      }
    }
  }
}

PreservedAnalyses
SelectionDAGISelPass::run(MachineFunction &MF,
                          MachineFunctionAnalysisManager &MFAM) {
  // If we already selected that function, we do not need to run SDISel.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Selected))
    return PreservedAnalyses::all();

  // Do some sanity-checking on the command-line options.
  if (EnableFastISelAbort && !Selector->TM.Options.EnableFastISel)
    report_fatal_error("-fast-isel-abort > 0 requires -fast-isel");

  // Decide what flavour of variable location debug-info will be used, before
  // we change the optimisation level.
  MF.setUseDebugInstrRef(MF.shouldUseDebugInstrRef());

  // Reset the target options before resetting the optimization
  // level below.
  // FIXME: This is a horrible hack and should be processed via
  // codegen looking at the optimization level explicitly when
  // it wants to look at it.
  Selector->TM.resetTargetOptions(MF.getFunction());
  // Reset OptLevel to None for optnone functions.
  // TODO: Add a function analysis to handle this.
  Selector->MF = &MF;
  // Reset OptLevel to None for optnone functions.
  CodeGenOptLevel NewOptLevel = MF.getFunction().hasOptNone()
                                    ? CodeGenOptLevel::None
                                    : Selector->OptLevel;

  OptLevelChanger OLC(*Selector, NewOptLevel);
  Selector->initializeAnalysisResults(MFAM);
  Selector->runOnMachineFunction(MF);

  return getMachineFunctionPassPreservedAnalyses();
}

void SelectionDAGISel::initializeAnalysisResults(
    MachineFunctionAnalysisManager &MFAM) {
  auto &FAM = MFAM.getResult<FunctionAnalysisManagerMachineFunctionProxy>(*MF)
                  .getManager();
  auto &MAMP = MFAM.getResult<ModuleAnalysisManagerMachineFunctionProxy>(*MF);
  Function &Fn = MF->getFunction();
#ifndef NDEBUG
  FuncName = Fn.getName();
  MatchFilterFuncName = isFunctionInPrintList(FuncName);
#else
  (void)MatchFilterFuncName;
#endif

  TII = MF->getSubtarget().getInstrInfo();
  TLI = MF->getSubtarget().getTargetLowering();
  RegInfo = &MF->getRegInfo();
  LibInfo = &FAM.getResult<TargetLibraryAnalysis>(Fn);
  GFI = Fn.hasGC() ? &FAM.getResult<GCFunctionAnalysis>(Fn) : nullptr;
  ORE = std::make_unique<OptimizationRemarkEmitter>(&Fn);
  AC = &FAM.getResult<AssumptionAnalysis>(Fn);
  auto *PSI = MAMP.getCachedResult<ProfileSummaryAnalysis>(*Fn.getParent());
  BlockFrequencyInfo *BFI = nullptr;
  FAM.getResult<BlockFrequencyAnalysis>(Fn);
  if (PSI && PSI->hasProfileSummary() && OptLevel != CodeGenOptLevel::None)
    BFI = &FAM.getResult<BlockFrequencyAnalysis>(Fn);

  FunctionVarLocs const *FnVarLocs = nullptr;
  if (isAssignmentTrackingEnabled(*Fn.getParent()))
    FnVarLocs = &FAM.getResult<DebugAssignmentTrackingAnalysis>(Fn);

  auto *UA = FAM.getCachedResult<UniformityInfoAnalysis>(Fn);
  CurDAG->init(*MF, *ORE, MFAM, LibInfo, UA, PSI, BFI, FnVarLocs);

  // Now get the optional analyzes if we want to.
  // This is based on the possibly changed OptLevel (after optnone is taken
  // into account).  That's unfortunate but OK because it just means we won't
  // ask for passes that have been required anyway.

  if (UseMBPI && OptLevel != CodeGenOptLevel::None)
    FuncInfo->BPI = &FAM.getResult<BranchProbabilityAnalysis>(Fn);
  else
    FuncInfo->BPI = nullptr;

  if (OptLevel != CodeGenOptLevel::None)
    AA = &FAM.getResult<AAManager>(Fn);
  else
    AA = nullptr;

  SP = &FAM.getResult<SSPLayoutAnalysis>(Fn);

#if !defined(NDEBUG) && LLVM_ENABLE_ABI_BREAKING_CHECKS
  TTI = &FAM.getResult<TargetIRAnalysis>(Fn);
#endif
}

void SelectionDAGISel::initializeAnalysisResults(MachineFunctionPass &MFP) {
  Function &Fn = MF->getFunction();
#ifndef NDEBUG
  FuncName = Fn.getName();
  MatchFilterFuncName = isFunctionInPrintList(FuncName);
#else
  (void)MatchFilterFuncName;
#endif

  TII = MF->getSubtarget().getInstrInfo();
  TLI = MF->getSubtarget().getTargetLowering();
  RegInfo = &MF->getRegInfo();
  LibInfo = &MFP.getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(Fn);
  GFI = Fn.hasGC() ? &MFP.getAnalysis<GCModuleInfo>().getFunctionInfo(Fn)
                   : nullptr;
  ORE = std::make_unique<OptimizationRemarkEmitter>(&Fn);
  AC = &MFP.getAnalysis<AssumptionCacheTracker>().getAssumptionCache(Fn);
  auto *PSI = &MFP.getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  BlockFrequencyInfo *BFI = nullptr;
  if (PSI && PSI->hasProfileSummary() && OptLevel != CodeGenOptLevel::None)
    BFI = &MFP.getAnalysis<LazyBlockFrequencyInfoPass>().getBFI();

  FunctionVarLocs const *FnVarLocs = nullptr;
  if (isAssignmentTrackingEnabled(*Fn.getParent()))
    FnVarLocs = MFP.getAnalysis<AssignmentTrackingAnalysis>().getResults();

  UniformityInfo *UA = nullptr;
  if (auto *UAPass = MFP.getAnalysisIfAvailable<UniformityInfoWrapperPass>())
    UA = &UAPass->getUniformityInfo();
  CurDAG->init(*MF, *ORE, &MFP, LibInfo, UA, PSI, BFI, FnVarLocs);

  // Now get the optional analyzes if we want to.
  // This is based on the possibly changed OptLevel (after optnone is taken
  // into account).  That's unfortunate but OK because it just means we won't
  // ask for passes that have been required anyway.

  if (UseMBPI && OptLevel != CodeGenOptLevel::None)
    FuncInfo->BPI =
        &MFP.getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
  else
    FuncInfo->BPI = nullptr;

  if (OptLevel != CodeGenOptLevel::None)
    AA = &MFP.getAnalysis<AAResultsWrapperPass>().getAAResults();
  else
    AA = nullptr;

  SP = &MFP.getAnalysis<StackProtector>().getLayoutInfo();

#if !defined(NDEBUG) && LLVM_ENABLE_ABI_BREAKING_CHECKS
  TTI = &MFP.getAnalysis<TargetTransformInfoWrapperPass>().getTTI(Fn);
#endif
}

bool SelectionDAGISel::runOnMachineFunction(MachineFunction &mf) {
  SwiftError->setFunction(mf);
  const Function &Fn = mf.getFunction();

  bool InstrRef = mf.shouldUseDebugInstrRef();

  FuncInfo->set(MF->getFunction(), *MF, CurDAG);

  ISEL_DUMP(dbgs() << "\n\n\n=== " << FuncName << '\n');

  SDB->init(GFI, AA, AC, LibInfo);

  MF->setHasInlineAsm(false);

  FuncInfo->SplitCSR = false;

  // We split CSR if the target supports it for the given function
  // and the function has only return exits.
  if (OptLevel != CodeGenOptLevel::None && TLI->supportSplitCSR(MF)) {
    FuncInfo->SplitCSR = true;

    // Collect all the return blocks.
    for (const BasicBlock &BB : Fn) {
      if (!succ_empty(&BB))
        continue;

      const Instruction *Term = BB.getTerminator();
      if (isa<UnreachableInst>(Term) || isa<ReturnInst>(Term))
        continue;

      // Bail out if the exit block is not Return nor Unreachable.
      FuncInfo->SplitCSR = false;
      break;
    }
  }

  MachineBasicBlock *EntryMBB = &MF->front();
  if (FuncInfo->SplitCSR)
    // This performs initialization so lowering for SplitCSR will be correct.
    TLI->initializeSplitCSR(EntryMBB);

  SelectAllBasicBlocks(Fn);
  if (FastISelFailed && EnableFastISelFallbackReport) {
    DiagnosticInfoISelFallback DiagFallback(Fn);
    Fn.getContext().diagnose(DiagFallback);
  }

  // Replace forward-declared registers with the registers containing
  // the desired value.
  // Note: it is important that this happens **before** the call to
  // EmitLiveInCopies, since implementations can skip copies of unused
  // registers. If we don't apply the reg fixups before, some registers may
  // appear as unused and will be skipped, resulting in bad MI.
  MachineRegisterInfo &MRI = MF->getRegInfo();
  for (DenseMap<Register, Register>::iterator I = FuncInfo->RegFixups.begin(),
                                              E = FuncInfo->RegFixups.end();
       I != E; ++I) {
    Register From = I->first;
    Register To = I->second;
    // If To is also scheduled to be replaced, find what its ultimate
    // replacement is.
    while (true) {
      DenseMap<Register, Register>::iterator J = FuncInfo->RegFixups.find(To);
      if (J == E)
        break;
      To = J->second;
    }
    // Make sure the new register has a sufficiently constrained register class.
    if (From.isVirtual() && To.isVirtual())
      MRI.constrainRegClass(To, MRI.getRegClass(From));
    // Replace it.

    // Replacing one register with another won't touch the kill flags.
    // We need to conservatively clear the kill flags as a kill on the old
    // register might dominate existing uses of the new register.
    if (!MRI.use_empty(To))
      MRI.clearKillFlags(From);
    MRI.replaceRegWith(From, To);
  }

  // If the first basic block in the function has live ins that need to be
  // copied into vregs, emit the copies into the top of the block before
  // emitting the code for the block.
  const TargetRegisterInfo &TRI = *MF->getSubtarget().getRegisterInfo();
  RegInfo->EmitLiveInCopies(EntryMBB, TRI, *TII);

  // Insert copies in the entry block and the return blocks.
  if (FuncInfo->SplitCSR) {
    SmallVector<MachineBasicBlock*, 4> Returns;
    // Collect all the return blocks.
    for (MachineBasicBlock &MBB : mf) {
      if (!MBB.succ_empty())
        continue;

      MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
      if (Term != MBB.end() && Term->isReturn()) {
        Returns.push_back(&MBB);
        continue;
      }
    }
    TLI->insertCopiesSplitCSR(EntryMBB, Returns);
  }

  DenseMap<unsigned, unsigned> LiveInMap;
  if (!FuncInfo->ArgDbgValues.empty())
    for (std::pair<unsigned, unsigned> LI : RegInfo->liveins())
      if (LI.second)
        LiveInMap.insert(LI);

  // Insert DBG_VALUE instructions for function arguments to the entry block.
  for (unsigned i = 0, e = FuncInfo->ArgDbgValues.size(); i != e; ++i) {
    MachineInstr *MI = FuncInfo->ArgDbgValues[e - i - 1];
    assert(MI->getOpcode() != TargetOpcode::DBG_VALUE_LIST &&
           "Function parameters should not be described by DBG_VALUE_LIST.");
    bool hasFI = MI->getDebugOperand(0).isFI();
    Register Reg =
        hasFI ? TRI.getFrameRegister(*MF) : MI->getDebugOperand(0).getReg();
    if (Reg.isPhysical())
      EntryMBB->insert(EntryMBB->begin(), MI);
    else {
      MachineInstr *Def = RegInfo->getVRegDef(Reg);
      if (Def) {
        MachineBasicBlock::iterator InsertPos = Def;
        // FIXME: VR def may not be in entry block.
        Def->getParent()->insert(std::next(InsertPos), MI);
      } else
        LLVM_DEBUG(dbgs() << "Dropping debug info for dead vreg"
                          << Register::virtReg2Index(Reg) << "\n");
    }

    // Don't try and extend through copies in instruction referencing mode.
    if (InstrRef)
      continue;

    // If Reg is live-in then update debug info to track its copy in a vreg.
    DenseMap<unsigned, unsigned>::iterator LDI = LiveInMap.find(Reg);
    if (LDI != LiveInMap.end()) {
      assert(!hasFI && "There's no handling of frame pointer updating here yet "
                       "- add if needed");
      MachineInstr *Def = RegInfo->getVRegDef(LDI->second);
      MachineBasicBlock::iterator InsertPos = Def;
      const MDNode *Variable = MI->getDebugVariable();
      const MDNode *Expr = MI->getDebugExpression();
      DebugLoc DL = MI->getDebugLoc();
      bool IsIndirect = MI->isIndirectDebugValue();
      if (IsIndirect)
        assert(MI->getDebugOffset().getImm() == 0 &&
               "DBG_VALUE with nonzero offset");
      assert(cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(DL) &&
             "Expected inlined-at fields to agree");
      assert(MI->getOpcode() != TargetOpcode::DBG_VALUE_LIST &&
             "Didn't expect to see a DBG_VALUE_LIST here");
      // Def is never a terminator here, so it is ok to increment InsertPos.
      BuildMI(*EntryMBB, ++InsertPos, DL, TII->get(TargetOpcode::DBG_VALUE),
              IsIndirect, LDI->second, Variable, Expr);

      // If this vreg is directly copied into an exported register then
      // that COPY instructions also need DBG_VALUE, if it is the only
      // user of LDI->second.
      MachineInstr *CopyUseMI = nullptr;
      for (MachineInstr &UseMI : RegInfo->use_instructions(LDI->second)) {
        if (UseMI.isDebugValue())
          continue;
        if (UseMI.isCopy() && !CopyUseMI && UseMI.getParent() == EntryMBB) {
          CopyUseMI = &UseMI;
          continue;
        }
        // Otherwise this is another use or second copy use.
        CopyUseMI = nullptr;
        break;
      }
      if (CopyUseMI &&
          TRI.getRegSizeInBits(LDI->second, MRI) ==
              TRI.getRegSizeInBits(CopyUseMI->getOperand(0).getReg(), MRI)) {
        // Use MI's debug location, which describes where Variable was
        // declared, rather than whatever is attached to CopyUseMI.
        MachineInstr *NewMI =
            BuildMI(*MF, DL, TII->get(TargetOpcode::DBG_VALUE), IsIndirect,
                    CopyUseMI->getOperand(0).getReg(), Variable, Expr);
        MachineBasicBlock::iterator Pos = CopyUseMI;
        EntryMBB->insertAfter(Pos, NewMI);
      }
    }
  }

  // For debug-info, in instruction referencing mode, we need to perform some
  // post-isel maintenence.
  if (MF->useDebugInstrRef())
    MF->finalizeDebugInstrRefs();

  // Determine if there are any calls in this machine function.
  MachineFrameInfo &MFI = MF->getFrameInfo();
  for (const auto &MBB : *MF) {
    if (MFI.hasCalls() && MF->hasInlineAsm())
      break;

    for (const auto &MI : MBB) {
      const MCInstrDesc &MCID = TII->get(MI.getOpcode());
      if ((MCID.isCall() && !MCID.isReturn()) ||
          MI.isStackAligningInlineAsm()) {
        MFI.setHasCalls(true);
      }
      if (MI.isInlineAsm()) {
        MF->setHasInlineAsm(true);
      }
    }
  }

  // Determine if floating point is used for msvc
  computeUsesMSVCFloatingPoint(TM.getTargetTriple(), Fn, MF->getMMI());

  // Release function-specific state. SDB and CurDAG are already cleared
  // at this point.
  FuncInfo->clear();

  ISEL_DUMP(dbgs() << "*** MachineFunction at end of ISel ***\n");
  ISEL_DUMP(MF->print(dbgs()));

  return true;
}

static void reportFastISelFailure(MachineFunction &MF,
                                  OptimizationRemarkEmitter &ORE,
                                  OptimizationRemarkMissed &R,
                                  bool ShouldAbort) {
  // Print the function name explicitly if we don't have a debug location (which
  // makes the diagnostic less useful) or if we're going to emit a raw error.
  if (!R.getLocation().isValid() || ShouldAbort)
    R << (" (in function: " + MF.getName() + ")").str();

  if (ShouldAbort)
    report_fatal_error(Twine(R.getMsg()));

  ORE.emit(R);
  LLVM_DEBUG(dbgs() << R.getMsg() << "\n");
}

void SelectionDAGISel::SelectBasicBlock(BasicBlock::const_iterator Begin,
                                        BasicBlock::const_iterator End,
                                        bool &HadTailCall) {
  // Allow creating illegal types during DAG building for the basic block.
  CurDAG->NewNodesMustHaveLegalTypes = false;

  // Lower the instructions. If a call is emitted as a tail call, cease emitting
  // nodes for this block. If an instruction is elided, don't emit it, but do
  // handle any debug-info attached to it.
  for (BasicBlock::const_iterator I = Begin; I != End && !SDB->HasTailCall; ++I) {
    if (!ElidedArgCopyInstrs.count(&*I))
      SDB->visit(*I);
    else
      SDB->visitDbgInfo(*I);
  }

  // Make sure the root of the DAG is up-to-date.
  CurDAG->setRoot(SDB->getControlRoot());
  HadTailCall = SDB->HasTailCall;
  SDB->resolveOrClearDbgInfo();
  SDB->clear();

  // Final step, emit the lowered DAG as machine code.
  CodeGenAndEmitDAG();
}

void SelectionDAGISel::ComputeLiveOutVRegInfo() {
  SmallPtrSet<SDNode *, 16> Added;
  SmallVector<SDNode*, 128> Worklist;

  Worklist.push_back(CurDAG->getRoot().getNode());
  Added.insert(CurDAG->getRoot().getNode());

  KnownBits Known;

  do {
    SDNode *N = Worklist.pop_back_val();

    // Otherwise, add all chain operands to the worklist.
    for (const SDValue &Op : N->op_values())
      if (Op.getValueType() == MVT::Other && Added.insert(Op.getNode()).second)
        Worklist.push_back(Op.getNode());

    // If this is a CopyToReg with a vreg dest, process it.
    if (N->getOpcode() != ISD::CopyToReg)
      continue;

    unsigned DestReg = cast<RegisterSDNode>(N->getOperand(1))->getReg();
    if (!Register::isVirtualRegister(DestReg))
      continue;

    // Ignore non-integer values.
    SDValue Src = N->getOperand(2);
    EVT SrcVT = Src.getValueType();
    if (!SrcVT.isInteger())
      continue;

    unsigned NumSignBits = CurDAG->ComputeNumSignBits(Src);
    Known = CurDAG->computeKnownBits(Src);
    FuncInfo->AddLiveOutRegInfo(DestReg, NumSignBits, Known);
  } while (!Worklist.empty());
}

void SelectionDAGISel::CodeGenAndEmitDAG() {
  StringRef GroupName = "sdag";
  StringRef GroupDescription = "Instruction Selection and Scheduling";
  std::string BlockName;
  bool MatchFilterBB = false;
  (void)MatchFilterBB;

  // Pre-type legalization allow creation of any node types.
  CurDAG->NewNodesMustHaveLegalTypes = false;

#ifndef NDEBUG
  MatchFilterBB = (FilterDAGBasicBlockName.empty() ||
                   FilterDAGBasicBlockName ==
                       FuncInfo->MBB->getBasicBlock()->getName());
#endif
#ifdef NDEBUG
  if (ViewDAGCombine1 || ViewLegalizeTypesDAGs || ViewDAGCombineLT ||
      ViewLegalizeDAGs || ViewDAGCombine2 || ViewISelDAGs || ViewSchedDAGs ||
      ViewSUnitDAGs)
#endif
  {
    BlockName =
        (MF->getName() + ":" + FuncInfo->MBB->getBasicBlock()->getName()).str();
  }
  ISEL_DUMP(dbgs() << "\nInitial selection DAG: "
                   << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                   << "'\n";
            CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  if (TTI->hasBranchDivergence())
    CurDAG->VerifyDAGDivergence();
#endif

  if (ViewDAGCombine1 && MatchFilterBB)
    CurDAG->viewGraph("dag-combine1 input for " + BlockName);

  // Run the DAG combiner in pre-legalize mode.
  {
    NamedRegionTimer T("combine1", "DAG Combining 1", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    CurDAG->Combine(BeforeLegalizeTypes, AA, OptLevel);
  }

  ISEL_DUMP(dbgs() << "\nOptimized lowered selection DAG: "
                   << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                   << "'\n";
            CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  if (TTI->hasBranchDivergence())
    CurDAG->VerifyDAGDivergence();
#endif

  // Second step, hack on the DAG until it only uses operations and types that
  // the target supports.
  if (ViewLegalizeTypesDAGs && MatchFilterBB)
    CurDAG->viewGraph("legalize-types input for " + BlockName);

  bool Changed;
  {
    NamedRegionTimer T("legalize_types", "Type Legalization", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    Changed = CurDAG->LegalizeTypes();
  }

  ISEL_DUMP(dbgs() << "\nType-legalized selection DAG: "
                   << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                   << "'\n";
            CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  if (TTI->hasBranchDivergence())
    CurDAG->VerifyDAGDivergence();
#endif

  // Only allow creation of legal node types.
  CurDAG->NewNodesMustHaveLegalTypes = true;

  if (Changed) {
    if (ViewDAGCombineLT && MatchFilterBB)
      CurDAG->viewGraph("dag-combine-lt input for " + BlockName);

    // Run the DAG combiner in post-type-legalize mode.
    {
      NamedRegionTimer T("combine_lt", "DAG Combining after legalize types",
                         GroupName, GroupDescription, TimePassesIsEnabled);
      CurDAG->Combine(AfterLegalizeTypes, AA, OptLevel);
    }

    ISEL_DUMP(dbgs() << "\nOptimized type-legalized selection DAG: "
                     << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                     << "'\n";
              CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    if (TTI->hasBranchDivergence())
      CurDAG->VerifyDAGDivergence();
#endif
  }

  {
    NamedRegionTimer T("legalize_vec", "Vector Legalization", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    Changed = CurDAG->LegalizeVectors();
  }

  if (Changed) {
    ISEL_DUMP(dbgs() << "\nVector-legalized selection DAG: "
                     << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                     << "'\n";
              CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    if (TTI->hasBranchDivergence())
      CurDAG->VerifyDAGDivergence();
#endif

    {
      NamedRegionTimer T("legalize_types2", "Type Legalization 2", GroupName,
                         GroupDescription, TimePassesIsEnabled);
      CurDAG->LegalizeTypes();
    }

    ISEL_DUMP(dbgs() << "\nVector/type-legalized selection DAG: "
                     << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                     << "'\n";
              CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    if (TTI->hasBranchDivergence())
      CurDAG->VerifyDAGDivergence();
#endif

    if (ViewDAGCombineLT && MatchFilterBB)
      CurDAG->viewGraph("dag-combine-lv input for " + BlockName);

    // Run the DAG combiner in post-type-legalize mode.
    {
      NamedRegionTimer T("combine_lv", "DAG Combining after legalize vectors",
                         GroupName, GroupDescription, TimePassesIsEnabled);
      CurDAG->Combine(AfterLegalizeVectorOps, AA, OptLevel);
    }

    ISEL_DUMP(dbgs() << "\nOptimized vector-legalized selection DAG: "
                     << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                     << "'\n";
              CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    if (TTI->hasBranchDivergence())
      CurDAG->VerifyDAGDivergence();
#endif
  }

  if (ViewLegalizeDAGs && MatchFilterBB)
    CurDAG->viewGraph("legalize input for " + BlockName);

  {
    NamedRegionTimer T("legalize", "DAG Legalization", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    CurDAG->Legalize();
  }

  ISEL_DUMP(dbgs() << "\nLegalized selection DAG: "
                   << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                   << "'\n";
            CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  if (TTI->hasBranchDivergence())
    CurDAG->VerifyDAGDivergence();
#endif

  if (ViewDAGCombine2 && MatchFilterBB)
    CurDAG->viewGraph("dag-combine2 input for " + BlockName);

  // Run the DAG combiner in post-legalize mode.
  {
    NamedRegionTimer T("combine2", "DAG Combining 2", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    CurDAG->Combine(AfterLegalizeDAG, AA, OptLevel);
  }

  ISEL_DUMP(dbgs() << "\nOptimized legalized selection DAG: "
                   << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                   << "'\n";
            CurDAG->dump());

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  if (TTI->hasBranchDivergence())
    CurDAG->VerifyDAGDivergence();
#endif

  if (OptLevel != CodeGenOptLevel::None)
    ComputeLiveOutVRegInfo();

  if (ViewISelDAGs && MatchFilterBB)
    CurDAG->viewGraph("isel input for " + BlockName);

  // Third, instruction select all of the operations to machine code, adding the
  // code to the MachineBasicBlock.
  {
    NamedRegionTimer T("isel", "Instruction Selection", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    DoInstructionSelection();
  }

  ISEL_DUMP(dbgs() << "\nSelected selection DAG: "
                   << printMBBReference(*FuncInfo->MBB) << " '" << BlockName
                   << "'\n";
            CurDAG->dump());

  if (ViewSchedDAGs && MatchFilterBB)
    CurDAG->viewGraph("scheduler input for " + BlockName);

  // Schedule machine code.
  ScheduleDAGSDNodes *Scheduler = CreateScheduler();
  {
    NamedRegionTimer T("sched", "Instruction Scheduling", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    Scheduler->Run(CurDAG, FuncInfo->MBB);
  }

  if (ViewSUnitDAGs && MatchFilterBB)
    Scheduler->viewGraph();

  // Emit machine code to BB.  This can change 'BB' to the last block being
  // inserted into.
  MachineBasicBlock *FirstMBB = FuncInfo->MBB, *LastMBB;
  {
    NamedRegionTimer T("emit", "Instruction Creation", GroupName,
                       GroupDescription, TimePassesIsEnabled);

    // FuncInfo->InsertPt is passed by reference and set to the end of the
    // scheduled instructions.
    LastMBB = FuncInfo->MBB = Scheduler->EmitSchedule(FuncInfo->InsertPt);
  }

  // If the block was split, make sure we update any references that are used to
  // update PHI nodes later on.
  if (FirstMBB != LastMBB)
    SDB->UpdateSplitBlock(FirstMBB, LastMBB);

  // Free the scheduler state.
  {
    NamedRegionTimer T("cleanup", "Instruction Scheduling Cleanup", GroupName,
                       GroupDescription, TimePassesIsEnabled);
    delete Scheduler;
  }

  // Free the SelectionDAG state, now that we're finished with it.
  CurDAG->clear();
}

namespace {

/// ISelUpdater - helper class to handle updates of the instruction selection
/// graph.
class ISelUpdater : public SelectionDAG::DAGUpdateListener {
  SelectionDAG::allnodes_iterator &ISelPosition;

public:
  ISelUpdater(SelectionDAG &DAG, SelectionDAG::allnodes_iterator &isp)
    : SelectionDAG::DAGUpdateListener(DAG), ISelPosition(isp) {}

  /// NodeDeleted - Handle nodes deleted from the graph. If the node being
  /// deleted is the current ISelPosition node, update ISelPosition.
  ///
  void NodeDeleted(SDNode *N, SDNode *E) override {
    if (ISelPosition == SelectionDAG::allnodes_iterator(N))
      ++ISelPosition;
  }

  /// NodeInserted - Handle new nodes inserted into the graph: propagate
  /// metadata from root nodes that also applies to new nodes, in case the root
  /// is later deleted.
  void NodeInserted(SDNode *N) override {
    SDNode *CurNode = &*ISelPosition;
    if (MDNode *MD = DAG.getPCSections(CurNode))
      DAG.addPCSections(N, MD);
    if (MDNode *MMRA = DAG.getMMRAMetadata(CurNode))
      DAG.addMMRAMetadata(N, MMRA);
  }
};

} // end anonymous namespace

// This function is used to enforce the topological node id property
// leveraged during instruction selection. Before the selection process all
// nodes are given a non-negative id such that all nodes have a greater id than
// their operands. As this holds transitively we can prune checks that a node N
// is a predecessor of M another by not recursively checking through M's
// operands if N's ID is larger than M's ID. This significantly improves
// performance of various legality checks (e.g. IsLegalToFold / UpdateChains).

// However, when we fuse multiple nodes into a single node during the
// selection we may induce a predecessor relationship between inputs and
// outputs of distinct nodes being merged, violating the topological property.
// Should a fused node have a successor which has yet to be selected,
// our legality checks would be incorrect. To avoid this we mark all unselected
// successor nodes, i.e. id != -1, as invalid for pruning by bit-negating (x =>
// (-(x+1))) the ids and modify our pruning check to ignore negative Ids of M.
// We use bit-negation to more clearly enforce that node id -1 can only be
// achieved by selected nodes. As the conversion is reversable to the original
// Id, topological pruning can still be leveraged when looking for unselected
// nodes. This method is called internally in all ISel replacement related
// functions.
void SelectionDAGISel::EnforceNodeIdInvariant(SDNode *Node) {
  SmallVector<SDNode *, 4> Nodes;
  Nodes.push_back(Node);

  while (!Nodes.empty()) {
    SDNode *N = Nodes.pop_back_val();
    for (auto *U : N->uses()) {
      auto UId = U->getNodeId();
      if (UId > 0) {
        InvalidateNodeId(U);
        Nodes.push_back(U);
      }
    }
  }
}

// InvalidateNodeId - As explained in EnforceNodeIdInvariant, mark a
// NodeId with the equivalent node id which is invalid for topological
// pruning.
void SelectionDAGISel::InvalidateNodeId(SDNode *N) {
  int InvalidId = -(N->getNodeId() + 1);
  N->setNodeId(InvalidId);
}

// getUninvalidatedNodeId - get original uninvalidated node id.
int SelectionDAGISel::getUninvalidatedNodeId(SDNode *N) {
  int Id = N->getNodeId();
  if (Id < -1)
    return -(Id + 1);
  return Id;
}

void SelectionDAGISel::DoInstructionSelection() {
  LLVM_DEBUG(dbgs() << "===== Instruction selection begins: "
                    << printMBBReference(*FuncInfo->MBB) << " '"
                    << FuncInfo->MBB->getName() << "'\n");

  PreprocessISelDAG();

  // Select target instructions for the DAG.
  {
    // Number all nodes with a topological order and set DAGSize.
    DAGSize = CurDAG->AssignTopologicalOrder();

    // Create a dummy node (which is not added to allnodes), that adds
    // a reference to the root node, preventing it from being deleted,
    // and tracking any changes of the root.
    HandleSDNode Dummy(CurDAG->getRoot());
    SelectionDAG::allnodes_iterator ISelPosition (CurDAG->getRoot().getNode());
    ++ISelPosition;

    // Make sure that ISelPosition gets properly updated when nodes are deleted
    // in calls made from this function. New nodes inherit relevant metadata.
    ISelUpdater ISU(*CurDAG, ISelPosition);

    // The AllNodes list is now topological-sorted. Visit the
    // nodes by starting at the end of the list (the root of the
    // graph) and preceding back toward the beginning (the entry
    // node).
    while (ISelPosition != CurDAG->allnodes_begin()) {
      SDNode *Node = &*--ISelPosition;
      // Skip dead nodes. DAGCombiner is expected to eliminate all dead nodes,
      // but there are currently some corner cases that it misses. Also, this
      // makes it theoretically possible to disable the DAGCombiner.
      if (Node->use_empty())
        continue;

#ifndef NDEBUG
      SmallVector<SDNode *, 4> Nodes;
      Nodes.push_back(Node);

      while (!Nodes.empty()) {
        auto N = Nodes.pop_back_val();
        if (N->getOpcode() == ISD::TokenFactor || N->getNodeId() < 0)
          continue;
        for (const SDValue &Op : N->op_values()) {
          if (Op->getOpcode() == ISD::TokenFactor)
            Nodes.push_back(Op.getNode());
          else {
            // We rely on topological ordering of node ids for checking for
            // cycles when fusing nodes during selection. All unselected nodes
            // successors of an already selected node should have a negative id.
            // This assertion will catch such cases. If this assertion triggers
            // it is likely you using DAG-level Value/Node replacement functions
            // (versus equivalent ISEL replacement) in backend-specific
            // selections. See comment in EnforceNodeIdInvariant for more
            // details.
            assert(Op->getNodeId() != -1 &&
                   "Node has already selected predecessor node");
          }
        }
      }
#endif

      // When we are using non-default rounding modes or FP exception behavior
      // FP operations are represented by StrictFP pseudo-operations.  For
      // targets that do not (yet) understand strict FP operations directly,
      // we convert them to normal FP opcodes instead at this point.  This
      // will allow them to be handled by existing target-specific instruction
      // selectors.
      if (!TLI->isStrictFPEnabled() && Node->isStrictFPOpcode()) {
        // For some opcodes, we need to call TLI->getOperationAction using
        // the first operand type instead of the result type.  Note that this
        // must match what SelectionDAGLegalize::LegalizeOp is doing.
        EVT ActionVT;
        switch (Node->getOpcode()) {
        case ISD::STRICT_SINT_TO_FP:
        case ISD::STRICT_UINT_TO_FP:
        case ISD::STRICT_LRINT:
        case ISD::STRICT_LLRINT:
        case ISD::STRICT_LROUND:
        case ISD::STRICT_LLROUND:
        case ISD::STRICT_FSETCC:
        case ISD::STRICT_FSETCCS:
          ActionVT = Node->getOperand(1).getValueType();
          break;
        default:
          ActionVT = Node->getValueType(0);
          break;
        }
        if (TLI->getOperationAction(Node->getOpcode(), ActionVT)
            == TargetLowering::Expand)
          Node = CurDAG->mutateStrictFPToFP(Node);
      }

      LLVM_DEBUG(dbgs() << "\nISEL: Starting selection on root node: ";
                 Node->dump(CurDAG));

      Select(Node);
    }

    CurDAG->setRoot(Dummy.getValue());
  }

  LLVM_DEBUG(dbgs() << "\n===== Instruction selection ends:\n");

  PostprocessISelDAG();
}

static bool hasExceptionPointerOrCodeUser(const CatchPadInst *CPI) {
  for (const User *U : CPI->users()) {
    if (const IntrinsicInst *EHPtrCall = dyn_cast<IntrinsicInst>(U)) {
      Intrinsic::ID IID = EHPtrCall->getIntrinsicID();
      if (IID == Intrinsic::eh_exceptionpointer ||
          IID == Intrinsic::eh_exceptioncode)
        return true;
    }
  }
  return false;
}

// wasm.landingpad.index intrinsic is for associating a landing pad index number
// with a catchpad instruction. Retrieve the landing pad index in the intrinsic
// and store the mapping in the function.
static void mapWasmLandingPadIndex(MachineBasicBlock *MBB,
                                   const CatchPadInst *CPI) {
  MachineFunction *MF = MBB->getParent();
  // In case of single catch (...), we don't emit LSDA, so we don't need
  // this information.
  bool IsSingleCatchAllClause =
      CPI->arg_size() == 1 &&
      cast<Constant>(CPI->getArgOperand(0))->isNullValue();
  // cathchpads for longjmp use an empty type list, e.g. catchpad within %0 []
  // and they don't need LSDA info
  bool IsCatchLongjmp = CPI->arg_size() == 0;
  if (!IsSingleCatchAllClause && !IsCatchLongjmp) {
    // Create a mapping from landing pad label to landing pad index.
    bool IntrFound = false;
    for (const User *U : CPI->users()) {
      if (const auto *Call = dyn_cast<IntrinsicInst>(U)) {
        Intrinsic::ID IID = Call->getIntrinsicID();
        if (IID == Intrinsic::wasm_landingpad_index) {
          Value *IndexArg = Call->getArgOperand(1);
          int Index = cast<ConstantInt>(IndexArg)->getZExtValue();
          MF->setWasmLandingPadIndex(MBB, Index);
          IntrFound = true;
          break;
        }
      }
    }
    assert(IntrFound && "wasm.landingpad.index intrinsic not found!");
    (void)IntrFound;
  }
}

/// PrepareEHLandingPad - Emit an EH_LABEL, set up live-in registers, and
/// do other setup for EH landing-pad blocks.
bool SelectionDAGISel::PrepareEHLandingPad() {
  MachineBasicBlock *MBB = FuncInfo->MBB;
  const Constant *PersonalityFn = FuncInfo->Fn->getPersonalityFn();
  const BasicBlock *LLVMBB = MBB->getBasicBlock();
  const TargetRegisterClass *PtrRC =
      TLI->getRegClassFor(TLI->getPointerTy(CurDAG->getDataLayout()));

  auto Pers = classifyEHPersonality(PersonalityFn);

  // Catchpads have one live-in register, which typically holds the exception
  // pointer or code.
  if (isFuncletEHPersonality(Pers)) {
    if (const auto *CPI = dyn_cast<CatchPadInst>(LLVMBB->getFirstNonPHI())) {
      if (hasExceptionPointerOrCodeUser(CPI)) {
        // Get or create the virtual register to hold the pointer or code.  Mark
        // the live in physreg and copy into the vreg.
        MCPhysReg EHPhysReg = TLI->getExceptionPointerRegister(PersonalityFn);
        assert(EHPhysReg && "target lacks exception pointer register");
        MBB->addLiveIn(EHPhysReg);
        unsigned VReg = FuncInfo->getCatchPadExceptionPointerVReg(CPI, PtrRC);
        BuildMI(*MBB, FuncInfo->InsertPt, SDB->getCurDebugLoc(),
                TII->get(TargetOpcode::COPY), VReg)
            .addReg(EHPhysReg, RegState::Kill);
      }
    }
    return true;
  }

  // Add a label to mark the beginning of the landing pad.  Deletion of the
  // landing pad can thus be detected via the MachineModuleInfo.
  MCSymbol *Label = MF->addLandingPad(MBB);

  const MCInstrDesc &II = TII->get(TargetOpcode::EH_LABEL);
  BuildMI(*MBB, FuncInfo->InsertPt, SDB->getCurDebugLoc(), II)
    .addSym(Label);

  // If the unwinder does not preserve all registers, ensure that the
  // function marks the clobbered registers as used.
  const TargetRegisterInfo &TRI = *MF->getSubtarget().getRegisterInfo();
  if (auto *RegMask = TRI.getCustomEHPadPreservedMask(*MF))
    MF->getRegInfo().addPhysRegsUsedFromRegMask(RegMask);

  if (Pers == EHPersonality::Wasm_CXX) {
    if (const auto *CPI = dyn_cast<CatchPadInst>(LLVMBB->getFirstNonPHI()))
      mapWasmLandingPadIndex(MBB, CPI);
  } else {
    // Assign the call site to the landing pad's begin label.
    MF->setCallSiteLandingPad(Label, SDB->LPadToCallSiteMap[MBB]);
    // Mark exception register as live in.
    if (unsigned Reg = TLI->getExceptionPointerRegister(PersonalityFn))
      FuncInfo->ExceptionPointerVirtReg = MBB->addLiveIn(Reg, PtrRC);
    // Mark exception selector register as live in.
    if (unsigned Reg = TLI->getExceptionSelectorRegister(PersonalityFn))
      FuncInfo->ExceptionSelectorVirtReg = MBB->addLiveIn(Reg, PtrRC);
  }

  return true;
}

// Mark and Report IPToState for each Block under IsEHa
void SelectionDAGISel::reportIPToStateForBlocks(MachineFunction *MF) {
  MachineModuleInfo &MMI = MF->getMMI();
  llvm::WinEHFuncInfo *EHInfo = MF->getWinEHFuncInfo();
  if (!EHInfo)
    return;
  for (MachineBasicBlock &MBB : *MF) {
    const BasicBlock *BB = MBB.getBasicBlock();
    int State = EHInfo->BlockToStateMap[BB];
    if (BB->getFirstMayFaultInst()) {
      // Report IP range only for blocks with Faulty inst
      auto MBBb = MBB.getFirstNonPHI();

      if (MBBb == MBB.end())
        continue;

      MachineInstr *MIb = &*MBBb;
      if (MIb->isTerminator())
        continue;

      // Insert EH Labels
      MCSymbol *BeginLabel = MMI.getContext().createTempSymbol();
      MCSymbol *EndLabel = MMI.getContext().createTempSymbol();
      EHInfo->addIPToStateRange(State, BeginLabel, EndLabel);
      BuildMI(MBB, MBBb, SDB->getCurDebugLoc(),
              TII->get(TargetOpcode::EH_LABEL))
          .addSym(BeginLabel);
      auto MBBe = MBB.instr_end();
      MachineInstr *MIe = &*(--MBBe);
      // insert before (possible multiple) terminators
      while (MIe->isTerminator())
        MIe = &*(--MBBe);
      ++MBBe;
      BuildMI(MBB, MBBe, SDB->getCurDebugLoc(),
              TII->get(TargetOpcode::EH_LABEL))
          .addSym(EndLabel);
    }
  }
}

/// isFoldedOrDeadInstruction - Return true if the specified instruction is
/// side-effect free and is either dead or folded into a generated instruction.
/// Return false if it needs to be emitted.
static bool isFoldedOrDeadInstruction(const Instruction *I,
                                      const FunctionLoweringInfo &FuncInfo) {
  return !I->mayWriteToMemory() && // Side-effecting instructions aren't folded.
         !I->isTerminator() &&     // Terminators aren't folded.
         !isa<DbgInfoIntrinsic>(I) && // Debug instructions aren't folded.
         !I->isEHPad() &&             // EH pad instructions aren't folded.
         !FuncInfo.isExportedInst(I); // Exported instrs must be computed.
}

static bool processIfEntryValueDbgDeclare(FunctionLoweringInfo &FuncInfo,
                                          const Value *Arg, DIExpression *Expr,
                                          DILocalVariable *Var,
                                          DebugLoc DbgLoc) {
  if (!Expr->isEntryValue() || !isa<Argument>(Arg))
    return false;

  auto ArgIt = FuncInfo.ValueMap.find(Arg);
  if (ArgIt == FuncInfo.ValueMap.end())
    return false;
  Register ArgVReg = ArgIt->getSecond();

  // Find the corresponding livein physical register to this argument.
  for (auto [PhysReg, VirtReg] : FuncInfo.RegInfo->liveins())
    if (VirtReg == ArgVReg) {
      // Append an op deref to account for the fact that this is a dbg_declare.
      Expr = DIExpression::append(Expr, dwarf::DW_OP_deref);
      FuncInfo.MF->setVariableDbgInfo(Var, Expr, PhysReg, DbgLoc);
      LLVM_DEBUG(dbgs() << "processDbgDeclare: setVariableDbgInfo Var=" << *Var
                        << ", Expr=" << *Expr << ",  MCRegister=" << PhysReg
                        << ", DbgLoc=" << DbgLoc << "\n");
      return true;
    }
  return false;
}

static bool processDbgDeclare(FunctionLoweringInfo &FuncInfo,
                              const Value *Address, DIExpression *Expr,
                              DILocalVariable *Var, DebugLoc DbgLoc) {
  if (!Address) {
    LLVM_DEBUG(dbgs() << "processDbgDeclares skipping " << *Var
                      << " (bad address)\n");
    return false;
  }

  if (processIfEntryValueDbgDeclare(FuncInfo, Address, Expr, Var, DbgLoc))
    return true;

  MachineFunction *MF = FuncInfo.MF;
  const DataLayout &DL = MF->getDataLayout();

  assert(Var && "Missing variable");
  assert(DbgLoc && "Missing location");

  // Look through casts and constant offset GEPs. These mostly come from
  // inalloca.
  APInt Offset(DL.getTypeSizeInBits(Address->getType()), 0);
  Address = Address->stripAndAccumulateInBoundsConstantOffsets(DL, Offset);

  // Check if the variable is a static alloca or a byval or inalloca
  // argument passed in memory. If it is not, then we will ignore this
  // intrinsic and handle this during isel like dbg.value.
  int FI = std::numeric_limits<int>::max();
  if (const auto *AI = dyn_cast<AllocaInst>(Address)) {
    auto SI = FuncInfo.StaticAllocaMap.find(AI);
    if (SI != FuncInfo.StaticAllocaMap.end())
      FI = SI->second;
  } else if (const auto *Arg = dyn_cast<Argument>(Address))
    FI = FuncInfo.getArgumentFrameIndex(Arg);

  if (FI == std::numeric_limits<int>::max())
    return false;

  if (Offset.getBoolValue())
    Expr = DIExpression::prepend(Expr, DIExpression::ApplyOffset,
                                 Offset.getZExtValue());

  LLVM_DEBUG(dbgs() << "processDbgDeclare: setVariableDbgInfo Var=" << *Var
                    << ", Expr=" << *Expr << ",  FI=" << FI
                    << ", DbgLoc=" << DbgLoc << "\n");
  MF->setVariableDbgInfo(Var, Expr, FI, DbgLoc);
  return true;
}

/// Collect llvm.dbg.declare information. This is done after argument lowering
/// in case the declarations refer to arguments.
static void processDbgDeclares(FunctionLoweringInfo &FuncInfo) {
  for (const auto &I : instructions(*FuncInfo.Fn)) {
    const auto *DI = dyn_cast<DbgDeclareInst>(&I);
    if (DI && processDbgDeclare(FuncInfo, DI->getAddress(), DI->getExpression(),
                                DI->getVariable(), DI->getDebugLoc()))
      FuncInfo.PreprocessedDbgDeclares.insert(DI);
    for (const DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
      if (DVR.Type == DbgVariableRecord::LocationType::Declare &&
          processDbgDeclare(FuncInfo, DVR.getVariableLocationOp(0),
                            DVR.getExpression(), DVR.getVariable(),
                            DVR.getDebugLoc()))
        FuncInfo.PreprocessedDVRDeclares.insert(&DVR);
    }
  }
}

/// Collect single location variable information generated with assignment
/// tracking. This is done after argument lowering in case the declarations
/// refer to arguments.
static void processSingleLocVars(FunctionLoweringInfo &FuncInfo,
                                 FunctionVarLocs const *FnVarLocs) {
  for (auto It = FnVarLocs->single_locs_begin(),
            End = FnVarLocs->single_locs_end();
       It != End; ++It) {
    assert(!It->Values.hasArgList() && "Single loc variadic ops not supported");
    processDbgDeclare(FuncInfo, It->Values.getVariableLocationOp(0), It->Expr,
                      FnVarLocs->getDILocalVariable(It->VariableID), It->DL);
  }
}

void SelectionDAGISel::SelectAllBasicBlocks(const Function &Fn) {
  FastISelFailed = false;
  // Initialize the Fast-ISel state, if needed.
  FastISel *FastIS = nullptr;
  if (TM.Options.EnableFastISel) {
    LLVM_DEBUG(dbgs() << "Enabling fast-isel\n");
    FastIS = TLI->createFastISel(*FuncInfo, LibInfo);
  }

  ReversePostOrderTraversal<const Function*> RPOT(&Fn);

  // Lower arguments up front. An RPO iteration always visits the entry block
  // first.
  assert(*RPOT.begin() == &Fn.getEntryBlock());
  ++NumEntryBlocks;

  // Set up FuncInfo for ISel. Entry blocks never have PHIs.
  FuncInfo->MBB = FuncInfo->MBBMap[&Fn.getEntryBlock()];
  FuncInfo->InsertPt = FuncInfo->MBB->begin();

  CurDAG->setFunctionLoweringInfo(FuncInfo.get());

  if (!FastIS) {
    LowerArguments(Fn);
  } else {
    // See if fast isel can lower the arguments.
    FastIS->startNewBlock();
    if (!FastIS->lowerArguments()) {
      FastISelFailed = true;
      // Fast isel failed to lower these arguments
      ++NumFastIselFailLowerArguments;

      OptimizationRemarkMissed R("sdagisel", "FastISelFailure",
                                 Fn.getSubprogram(),
                                 &Fn.getEntryBlock());
      R << "FastISel didn't lower all arguments: "
        << ore::NV("Prototype", Fn.getFunctionType());
      reportFastISelFailure(*MF, *ORE, R, EnableFastISelAbort > 1);

      // Use SelectionDAG argument lowering
      LowerArguments(Fn);
      CurDAG->setRoot(SDB->getControlRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    // If we inserted any instructions at the beginning, make a note of
    // where they are, so we can be sure to emit subsequent instructions
    // after them.
    if (FuncInfo->InsertPt != FuncInfo->MBB->begin())
      FastIS->setLastLocalValue(&*std::prev(FuncInfo->InsertPt));
    else
      FastIS->setLastLocalValue(nullptr);
  }

  bool Inserted = SwiftError->createEntriesInEntryBlock(SDB->getCurDebugLoc());

  if (FastIS && Inserted)
    FastIS->setLastLocalValue(&*std::prev(FuncInfo->InsertPt));

  if (isAssignmentTrackingEnabled(*Fn.getParent())) {
    assert(CurDAG->getFunctionVarLocs() &&
           "expected AssignmentTrackingAnalysis pass results");
    processSingleLocVars(*FuncInfo, CurDAG->getFunctionVarLocs());
  } else {
    processDbgDeclares(*FuncInfo);
  }

  // Iterate over all basic blocks in the function.
  for (const BasicBlock *LLVMBB : RPOT) {
    if (OptLevel != CodeGenOptLevel::None) {
      bool AllPredsVisited = true;
      for (const BasicBlock *Pred : predecessors(LLVMBB)) {
        if (!FuncInfo->VisitedBBs.count(Pred)) {
          AllPredsVisited = false;
          break;
        }
      }

      if (AllPredsVisited) {
        for (const PHINode &PN : LLVMBB->phis())
          FuncInfo->ComputePHILiveOutRegInfo(&PN);
      } else {
        for (const PHINode &PN : LLVMBB->phis())
          FuncInfo->InvalidatePHILiveOutRegInfo(&PN);
      }

      FuncInfo->VisitedBBs.insert(LLVMBB);
    }

    BasicBlock::const_iterator const Begin =
        LLVMBB->getFirstNonPHI()->getIterator();
    BasicBlock::const_iterator const End = LLVMBB->end();
    BasicBlock::const_iterator BI = End;

    FuncInfo->MBB = FuncInfo->MBBMap[LLVMBB];
    if (!FuncInfo->MBB)
      continue; // Some blocks like catchpads have no code or MBB.

    // Insert new instructions after any phi or argument setup code.
    FuncInfo->InsertPt = FuncInfo->MBB->end();

    // Setup an EH landing-pad block.
    FuncInfo->ExceptionPointerVirtReg = 0;
    FuncInfo->ExceptionSelectorVirtReg = 0;
    if (LLVMBB->isEHPad())
      if (!PrepareEHLandingPad())
        continue;

    // Before doing SelectionDAG ISel, see if FastISel has been requested.
    if (FastIS) {
      if (LLVMBB != &Fn.getEntryBlock())
        FastIS->startNewBlock();

      unsigned NumFastIselRemaining = std::distance(Begin, End);

      // Pre-assign swifterror vregs.
      SwiftError->preassignVRegs(FuncInfo->MBB, Begin, End);

      // Do FastISel on as many instructions as possible.
      for (; BI != Begin; --BI) {
        const Instruction *Inst = &*std::prev(BI);

        // If we no longer require this instruction, skip it.
        if (isFoldedOrDeadInstruction(Inst, *FuncInfo) ||
            ElidedArgCopyInstrs.count(Inst)) {
          --NumFastIselRemaining;
          FastIS->handleDbgInfo(Inst);
          continue;
        }

        // Bottom-up: reset the insert pos at the top, after any local-value
        // instructions.
        FastIS->recomputeInsertPt();

        // Try to select the instruction with FastISel.
        if (FastIS->selectInstruction(Inst)) {
          --NumFastIselRemaining;
          ++NumFastIselSuccess;

          FastIS->handleDbgInfo(Inst);
          // If fast isel succeeded, skip over all the folded instructions, and
          // then see if there is a load right before the selected instructions.
          // Try to fold the load if so.
          const Instruction *BeforeInst = Inst;
          while (BeforeInst != &*Begin) {
            BeforeInst = &*std::prev(BasicBlock::const_iterator(BeforeInst));
            if (!isFoldedOrDeadInstruction(BeforeInst, *FuncInfo))
              break;
          }
          if (BeforeInst != Inst && isa<LoadInst>(BeforeInst) &&
              BeforeInst->hasOneUse() &&
              FastIS->tryToFoldLoad(cast<LoadInst>(BeforeInst), Inst)) {
            // If we succeeded, don't re-select the load.
            LLVM_DEBUG(dbgs()
                       << "FastISel folded load: " << *BeforeInst << "\n");
            FastIS->handleDbgInfo(BeforeInst);
            BI = std::next(BasicBlock::const_iterator(BeforeInst));
            --NumFastIselRemaining;
            ++NumFastIselSuccess;
          }
          continue;
        }

        FastISelFailed = true;

        // Then handle certain instructions as single-LLVM-Instruction blocks.
        // We cannot separate out GCrelocates to their own blocks since we need
        // to keep track of gc-relocates for a particular gc-statepoint. This is
        // done by SelectionDAGBuilder::LowerAsSTATEPOINT, called before
        // visitGCRelocate.
        if (isa<CallInst>(Inst) && !isa<GCStatepointInst>(Inst) &&
            !isa<GCRelocateInst>(Inst) && !isa<GCResultInst>(Inst)) {
          OptimizationRemarkMissed R("sdagisel", "FastISelFailure",
                                     Inst->getDebugLoc(), LLVMBB);

          R << "FastISel missed call";

          if (R.isEnabled() || EnableFastISelAbort) {
            std::string InstStrStorage;
            raw_string_ostream InstStr(InstStrStorage);
            InstStr << *Inst;

            R << ": " << InstStrStorage;
          }

          reportFastISelFailure(*MF, *ORE, R, EnableFastISelAbort > 2);

          if (!Inst->getType()->isVoidTy() && !Inst->getType()->isTokenTy() &&
              !Inst->use_empty()) {
            Register &R = FuncInfo->ValueMap[Inst];
            if (!R)
              R = FuncInfo->CreateRegs(Inst);
          }

          bool HadTailCall = false;
          MachineBasicBlock::iterator SavedInsertPt = FuncInfo->InsertPt;
          SelectBasicBlock(Inst->getIterator(), BI, HadTailCall);

          // If the call was emitted as a tail call, we're done with the block.
          // We also need to delete any previously emitted instructions.
          if (HadTailCall) {
            FastIS->removeDeadCode(SavedInsertPt, FuncInfo->MBB->end());
            --BI;
            break;
          }

          // Recompute NumFastIselRemaining as Selection DAG instruction
          // selection may have handled the call, input args, etc.
          unsigned RemainingNow = std::distance(Begin, BI);
          NumFastIselFailures += NumFastIselRemaining - RemainingNow;
          NumFastIselRemaining = RemainingNow;
          continue;
        }

        OptimizationRemarkMissed R("sdagisel", "FastISelFailure",
                                   Inst->getDebugLoc(), LLVMBB);

        bool ShouldAbort = EnableFastISelAbort;
        if (Inst->isTerminator()) {
          // Use a different message for terminator misses.
          R << "FastISel missed terminator";
          // Don't abort for terminator unless the level is really high
          ShouldAbort = (EnableFastISelAbort > 2);
        } else {
          R << "FastISel missed";
        }

        if (R.isEnabled() || EnableFastISelAbort) {
          std::string InstStrStorage;
          raw_string_ostream InstStr(InstStrStorage);
          InstStr << *Inst;
          R << ": " << InstStrStorage;
        }

        reportFastISelFailure(*MF, *ORE, R, ShouldAbort);

        NumFastIselFailures += NumFastIselRemaining;
        break;
      }

      FastIS->recomputeInsertPt();
    }

    if (SP->shouldEmitSDCheck(*LLVMBB)) {
      bool FunctionBasedInstrumentation =
          TLI->getSSPStackGuardCheck(*Fn.getParent());
      SDB->SPDescriptor.initialize(LLVMBB, FuncInfo->MBBMap[LLVMBB],
                                   FunctionBasedInstrumentation);
    }

    if (Begin != BI)
      ++NumDAGBlocks;
    else
      ++NumFastIselBlocks;

    if (Begin != BI) {
      // Run SelectionDAG instruction selection on the remainder of the block
      // not handled by FastISel. If FastISel is not run, this is the entire
      // block.
      bool HadTailCall;
      SelectBasicBlock(Begin, BI, HadTailCall);

      // But if FastISel was run, we already selected some of the block.
      // If we emitted a tail-call, we need to delete any previously emitted
      // instruction that follows it.
      if (FastIS && HadTailCall && FuncInfo->InsertPt != FuncInfo->MBB->end())
        FastIS->removeDeadCode(FuncInfo->InsertPt, FuncInfo->MBB->end());
    }

    if (FastIS)
      FastIS->finishBasicBlock();
    FinishBasicBlock();
    FuncInfo->PHINodesToUpdate.clear();
    ElidedArgCopyInstrs.clear();
  }

  // AsynchEH: Report Block State under -AsynchEH
  if (Fn.getParent()->getModuleFlag("eh-asynch"))
    reportIPToStateForBlocks(MF);

  SP->copyToMachineFrameInfo(MF->getFrameInfo());

  SwiftError->propagateVRegs();

  delete FastIS;
  SDB->clearDanglingDebugInfo();
  SDB->SPDescriptor.resetPerFunctionState();
}

void
SelectionDAGISel::FinishBasicBlock() {
  LLVM_DEBUG(dbgs() << "Total amount of phi nodes to update: "
                    << FuncInfo->PHINodesToUpdate.size() << "\n";
             for (unsigned i = 0, e = FuncInfo->PHINodesToUpdate.size(); i != e;
                  ++i) dbgs()
             << "Node " << i << " : (" << FuncInfo->PHINodesToUpdate[i].first
             << ", " << FuncInfo->PHINodesToUpdate[i].second << ")\n");

  // Next, now that we know what the last MBB the LLVM BB expanded is, update
  // PHI nodes in successors.
  for (unsigned i = 0, e = FuncInfo->PHINodesToUpdate.size(); i != e; ++i) {
    MachineInstrBuilder PHI(*MF, FuncInfo->PHINodesToUpdate[i].first);
    assert(PHI->isPHI() &&
           "This is not a machine PHI node that we are updating!");
    if (!FuncInfo->MBB->isSuccessor(PHI->getParent()))
      continue;
    PHI.addReg(FuncInfo->PHINodesToUpdate[i].second).addMBB(FuncInfo->MBB);
  }

  // Handle stack protector.
  if (SDB->SPDescriptor.shouldEmitFunctionBasedCheckStackProtector()) {
    // The target provides a guard check function. There is no need to
    // generate error handling code or to split current basic block.
    MachineBasicBlock *ParentMBB = SDB->SPDescriptor.getParentMBB();

    // Add load and check to the basicblock.
    FuncInfo->MBB = ParentMBB;
    FuncInfo->InsertPt =
        findSplitPointForStackProtector(ParentMBB, *TII);
    SDB->visitSPDescriptorParent(SDB->SPDescriptor, ParentMBB);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // Clear the Per-BB State.
    SDB->SPDescriptor.resetPerBBState();
  } else if (SDB->SPDescriptor.shouldEmitStackProtector()) {
    MachineBasicBlock *ParentMBB = SDB->SPDescriptor.getParentMBB();
    MachineBasicBlock *SuccessMBB = SDB->SPDescriptor.getSuccessMBB();

    // Find the split point to split the parent mbb. At the same time copy all
    // physical registers used in the tail of parent mbb into virtual registers
    // before the split point and back into physical registers after the split
    // point. This prevents us needing to deal with Live-ins and many other
    // register allocation issues caused by us splitting the parent mbb. The
    // register allocator will clean up said virtual copies later on.
    MachineBasicBlock::iterator SplitPoint =
        findSplitPointForStackProtector(ParentMBB, *TII);

    // Splice the terminator of ParentMBB into SuccessMBB.
    SuccessMBB->splice(SuccessMBB->end(), ParentMBB,
                       SplitPoint,
                       ParentMBB->end());

    // Add compare/jump on neq/jump to the parent BB.
    FuncInfo->MBB = ParentMBB;
    FuncInfo->InsertPt = ParentMBB->end();
    SDB->visitSPDescriptorParent(SDB->SPDescriptor, ParentMBB);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // CodeGen Failure MBB if we have not codegened it yet.
    MachineBasicBlock *FailureMBB = SDB->SPDescriptor.getFailureMBB();
    if (FailureMBB->empty()) {
      FuncInfo->MBB = FailureMBB;
      FuncInfo->InsertPt = FailureMBB->end();
      SDB->visitSPDescriptorFailure(SDB->SPDescriptor);
      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    // Clear the Per-BB State.
    SDB->SPDescriptor.resetPerBBState();
  }

  // Lower each BitTestBlock.
  for (auto &BTB : SDB->SL->BitTestCases) {
    // Lower header first, if it wasn't already lowered
    if (!BTB.Emitted) {
      // Set the current basic block to the mbb we wish to insert the code into
      FuncInfo->MBB = BTB.Parent;
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // Emit the code
      SDB->visitBitTestHeader(BTB, FuncInfo->MBB);
      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    BranchProbability UnhandledProb = BTB.Prob;
    for (unsigned j = 0, ej = BTB.Cases.size(); j != ej; ++j) {
      UnhandledProb -= BTB.Cases[j].ExtraProb;
      // Set the current basic block to the mbb we wish to insert the code into
      FuncInfo->MBB = BTB.Cases[j].ThisBB;
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // Emit the code

      // If all cases cover a contiguous range, it is not necessary to jump to
      // the default block after the last bit test fails. This is because the
      // range check during bit test header creation has guaranteed that every
      // case here doesn't go outside the range. In this case, there is no need
      // to perform the last bit test, as it will always be true. Instead, make
      // the second-to-last bit-test fall through to the target of the last bit
      // test, and delete the last bit test.

      MachineBasicBlock *NextMBB;
      if ((BTB.ContiguousRange || BTB.FallthroughUnreachable) && j + 2 == ej) {
        // Second-to-last bit-test with contiguous range or omitted range
        // check: fall through to the target of the final bit test.
        NextMBB = BTB.Cases[j + 1].TargetBB;
      } else if (j + 1 == ej) {
        // For the last bit test, fall through to Default.
        NextMBB = BTB.Default;
      } else {
        // Otherwise, fall through to the next bit test.
        NextMBB = BTB.Cases[j + 1].ThisBB;
      }

      SDB->visitBitTestCase(BTB, NextMBB, UnhandledProb, BTB.Reg, BTB.Cases[j],
                            FuncInfo->MBB);

      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();

      if ((BTB.ContiguousRange || BTB.FallthroughUnreachable) && j + 2 == ej) {
        // Since we're not going to use the final bit test, remove it.
        BTB.Cases.pop_back();
        break;
      }
    }

    // Update PHI Nodes
    for (const std::pair<MachineInstr *, unsigned> &P :
         FuncInfo->PHINodesToUpdate) {
      MachineInstrBuilder PHI(*MF, P.first);
      MachineBasicBlock *PHIBB = PHI->getParent();
      assert(PHI->isPHI() &&
             "This is not a machine PHI node that we are updating!");
      // This is "default" BB. We have two jumps to it. From "header" BB and
      // from last "case" BB, unless the latter was skipped.
      if (PHIBB == BTB.Default) {
        PHI.addReg(P.second).addMBB(BTB.Parent);
        if (!BTB.ContiguousRange) {
          PHI.addReg(P.second).addMBB(BTB.Cases.back().ThisBB);
         }
      }
      // One of "cases" BB.
      for (const SwitchCG::BitTestCase &BT : BTB.Cases) {
        MachineBasicBlock* cBB = BT.ThisBB;
        if (cBB->isSuccessor(PHIBB))
          PHI.addReg(P.second).addMBB(cBB);
      }
    }
  }
  SDB->SL->BitTestCases.clear();

  // If the JumpTable record is filled in, then we need to emit a jump table.
  // Updating the PHI nodes is tricky in this case, since we need to determine
  // whether the PHI is a successor of the range check MBB or the jump table MBB
  for (unsigned i = 0, e = SDB->SL->JTCases.size(); i != e; ++i) {
    // Lower header first, if it wasn't already lowered
    if (!SDB->SL->JTCases[i].first.Emitted) {
      // Set the current basic block to the mbb we wish to insert the code into
      FuncInfo->MBB = SDB->SL->JTCases[i].first.HeaderBB;
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // Emit the code
      SDB->visitJumpTableHeader(SDB->SL->JTCases[i].second,
                                SDB->SL->JTCases[i].first, FuncInfo->MBB);
      CurDAG->setRoot(SDB->getRoot());
      SDB->clear();
      CodeGenAndEmitDAG();
    }

    // Set the current basic block to the mbb we wish to insert the code into
    FuncInfo->MBB = SDB->SL->JTCases[i].second.MBB;
    FuncInfo->InsertPt = FuncInfo->MBB->end();
    // Emit the code
    SDB->visitJumpTable(SDB->SL->JTCases[i].second);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // Update PHI Nodes
    for (unsigned pi = 0, pe = FuncInfo->PHINodesToUpdate.size();
         pi != pe; ++pi) {
      MachineInstrBuilder PHI(*MF, FuncInfo->PHINodesToUpdate[pi].first);
      MachineBasicBlock *PHIBB = PHI->getParent();
      assert(PHI->isPHI() &&
             "This is not a machine PHI node that we are updating!");
      // "default" BB. We can go there only from header BB.
      if (PHIBB == SDB->SL->JTCases[i].second.Default)
        PHI.addReg(FuncInfo->PHINodesToUpdate[pi].second)
           .addMBB(SDB->SL->JTCases[i].first.HeaderBB);
      // JT BB. Just iterate over successors here
      if (FuncInfo->MBB->isSuccessor(PHIBB))
        PHI.addReg(FuncInfo->PHINodesToUpdate[pi].second).addMBB(FuncInfo->MBB);
    }
  }
  SDB->SL->JTCases.clear();

  // If we generated any switch lowering information, build and codegen any
  // additional DAGs necessary.
  for (unsigned i = 0, e = SDB->SL->SwitchCases.size(); i != e; ++i) {
    // Set the current basic block to the mbb we wish to insert the code into
    FuncInfo->MBB = SDB->SL->SwitchCases[i].ThisBB;
    FuncInfo->InsertPt = FuncInfo->MBB->end();

    // Determine the unique successors.
    SmallVector<MachineBasicBlock *, 2> Succs;
    Succs.push_back(SDB->SL->SwitchCases[i].TrueBB);
    if (SDB->SL->SwitchCases[i].TrueBB != SDB->SL->SwitchCases[i].FalseBB)
      Succs.push_back(SDB->SL->SwitchCases[i].FalseBB);

    // Emit the code. Note that this could result in FuncInfo->MBB being split.
    SDB->visitSwitchCase(SDB->SL->SwitchCases[i], FuncInfo->MBB);
    CurDAG->setRoot(SDB->getRoot());
    SDB->clear();
    CodeGenAndEmitDAG();

    // Remember the last block, now that any splitting is done, for use in
    // populating PHI nodes in successors.
    MachineBasicBlock *ThisBB = FuncInfo->MBB;

    // Handle any PHI nodes in successors of this chunk, as if we were coming
    // from the original BB before switch expansion.  Note that PHI nodes can
    // occur multiple times in PHINodesToUpdate.  We have to be very careful to
    // handle them the right number of times.
    for (MachineBasicBlock *Succ : Succs) {
      FuncInfo->MBB = Succ;
      FuncInfo->InsertPt = FuncInfo->MBB->end();
      // FuncInfo->MBB may have been removed from the CFG if a branch was
      // constant folded.
      if (ThisBB->isSuccessor(FuncInfo->MBB)) {
        for (MachineBasicBlock::iterator
             MBBI = FuncInfo->MBB->begin(), MBBE = FuncInfo->MBB->end();
             MBBI != MBBE && MBBI->isPHI(); ++MBBI) {
          MachineInstrBuilder PHI(*MF, MBBI);
          // This value for this PHI node is recorded in PHINodesToUpdate.
          for (unsigned pn = 0; ; ++pn) {
            assert(pn != FuncInfo->PHINodesToUpdate.size() &&
                   "Didn't find PHI entry!");
            if (FuncInfo->PHINodesToUpdate[pn].first == PHI) {
              PHI.addReg(FuncInfo->PHINodesToUpdate[pn].second).addMBB(ThisBB);
              break;
            }
          }
        }
      }
    }
  }
  SDB->SL->SwitchCases.clear();
}

/// Create the scheduler. If a specific scheduler was specified
/// via the SchedulerRegistry, use it, otherwise select the
/// one preferred by the target.
///
ScheduleDAGSDNodes *SelectionDAGISel::CreateScheduler() {
  return ISHeuristic(this, OptLevel);
}

//===----------------------------------------------------------------------===//
// Helper functions used by the generated instruction selector.
//===----------------------------------------------------------------------===//
// Calls to these methods are generated by tblgen.

/// CheckAndMask - The isel is trying to match something like (and X, 255).  If
/// the dag combiner simplified the 255, we still want to match.  RHS is the
/// actual value in the DAG on the RHS of an AND, and DesiredMaskS is the value
/// specified in the .td file (e.g. 255).
bool SelectionDAGISel::CheckAndMask(SDValue LHS, ConstantSDNode *RHS,
                                    int64_t DesiredMaskS) const {
  const APInt &ActualMask = RHS->getAPIntValue();
  const APInt &DesiredMask = APInt(LHS.getValueSizeInBits(), DesiredMaskS);

  // If the actual mask exactly matches, success!
  if (ActualMask == DesiredMask)
    return true;

  // If the actual AND mask is allowing unallowed bits, this doesn't match.
  if (!ActualMask.isSubsetOf(DesiredMask))
    return false;

  // Otherwise, the DAG Combiner may have proven that the value coming in is
  // either already zero or is not demanded.  Check for known zero input bits.
  APInt NeededMask = DesiredMask & ~ActualMask;
  if (CurDAG->MaskedValueIsZero(LHS, NeededMask))
    return true;

  // TODO: check to see if missing bits are just not demanded.

  // Otherwise, this pattern doesn't match.
  return false;
}

/// CheckOrMask - The isel is trying to match something like (or X, 255).  If
/// the dag combiner simplified the 255, we still want to match.  RHS is the
/// actual value in the DAG on the RHS of an OR, and DesiredMaskS is the value
/// specified in the .td file (e.g. 255).
bool SelectionDAGISel::CheckOrMask(SDValue LHS, ConstantSDNode *RHS,
                                   int64_t DesiredMaskS) const {
  const APInt &ActualMask = RHS->getAPIntValue();
  const APInt &DesiredMask = APInt(LHS.getValueSizeInBits(), DesiredMaskS);

  // If the actual mask exactly matches, success!
  if (ActualMask == DesiredMask)
    return true;

  // If the actual AND mask is allowing unallowed bits, this doesn't match.
  if (!ActualMask.isSubsetOf(DesiredMask))
    return false;

  // Otherwise, the DAG Combiner may have proven that the value coming in is
  // either already zero or is not demanded.  Check for known zero input bits.
  APInt NeededMask = DesiredMask & ~ActualMask;
  KnownBits Known = CurDAG->computeKnownBits(LHS);

  // If all the missing bits in the or are already known to be set, match!
  if (NeededMask.isSubsetOf(Known.One))
    return true;

  // TODO: check to see if missing bits are just not demanded.

  // Otherwise, this pattern doesn't match.
  return false;
}

/// SelectInlineAsmMemoryOperands - Calls to this are automatically generated
/// by tblgen.  Others should not call it.
void SelectionDAGISel::SelectInlineAsmMemoryOperands(std::vector<SDValue> &Ops,
                                                     const SDLoc &DL) {
  // Change the vector of SDValue into a list of SDNodeHandle for x86 might call
  // replaceAllUses when matching address.

  std::list<HandleSDNode> Handles;

  Handles.emplace_back(Ops[InlineAsm::Op_InputChain]); // 0
  Handles.emplace_back(Ops[InlineAsm::Op_AsmString]);  // 1
  Handles.emplace_back(Ops[InlineAsm::Op_MDNode]);     // 2, !srcloc
  Handles.emplace_back(
      Ops[InlineAsm::Op_ExtraInfo]); // 3 (SideEffect, AlignStack)

  unsigned i = InlineAsm::Op_FirstOperand, e = Ops.size();
  if (Ops[e - 1].getValueType() == MVT::Glue)
    --e;  // Don't process a glue operand if it is here.

  while (i != e) {
    InlineAsm::Flag Flags(Ops[i]->getAsZExtVal());
    if (!Flags.isMemKind() && !Flags.isFuncKind()) {
      // Just skip over this operand, copying the operands verbatim.
      Handles.insert(Handles.end(), Ops.begin() + i,
                     Ops.begin() + i + Flags.getNumOperandRegisters() + 1);
      i += Flags.getNumOperandRegisters() + 1;
    } else {
      assert(Flags.getNumOperandRegisters() == 1 &&
             "Memory operand with multiple values?");

      unsigned TiedToOperand;
      if (Flags.isUseOperandTiedToDef(TiedToOperand)) {
        // We need the constraint ID from the operand this is tied to.
        unsigned CurOp = InlineAsm::Op_FirstOperand;
        Flags = InlineAsm::Flag(Ops[CurOp]->getAsZExtVal());
        for (; TiedToOperand; --TiedToOperand) {
          CurOp += Flags.getNumOperandRegisters() + 1;
          Flags = InlineAsm::Flag(Ops[CurOp]->getAsZExtVal());
        }
      }

      // Otherwise, this is a memory operand.  Ask the target to select it.
      std::vector<SDValue> SelOps;
      const InlineAsm::ConstraintCode ConstraintID =
          Flags.getMemoryConstraintID();
      if (SelectInlineAsmMemoryOperand(Ops[i + 1], ConstraintID, SelOps))
        report_fatal_error("Could not match memory address.  Inline asm"
                           " failure!");

      // Add this to the output node.
      Flags = InlineAsm::Flag(Flags.isMemKind() ? InlineAsm::Kind::Mem
                                                : InlineAsm::Kind::Func,
                              SelOps.size());
      Flags.setMemConstraint(ConstraintID);
      Handles.emplace_back(CurDAG->getTargetConstant(Flags, DL, MVT::i32));
      Handles.insert(Handles.end(), SelOps.begin(), SelOps.end());
      i += 2;
    }
  }

  // Add the glue input back if present.
  if (e != Ops.size())
    Handles.emplace_back(Ops.back());

  Ops.clear();
  for (auto &handle : Handles)
    Ops.push_back(handle.getValue());
}

/// findGlueUse - Return use of MVT::Glue value produced by the specified
/// SDNode.
///
static SDNode *findGlueUse(SDNode *N) {
  unsigned FlagResNo = N->getNumValues()-1;
  for (SDNode::use_iterator I = N->use_begin(), E = N->use_end(); I != E; ++I) {
    SDUse &Use = I.getUse();
    if (Use.getResNo() == FlagResNo)
      return Use.getUser();
  }
  return nullptr;
}

/// findNonImmUse - Return true if "Def" is a predecessor of "Root" via a path
/// beyond "ImmedUse".  We may ignore chains as they are checked separately.
static bool findNonImmUse(SDNode *Root, SDNode *Def, SDNode *ImmedUse,
                          bool IgnoreChains) {
  SmallPtrSet<const SDNode *, 16> Visited;
  SmallVector<const SDNode *, 16> WorkList;
  // Only check if we have non-immediate uses of Def.
  if (ImmedUse->isOnlyUserOf(Def))
    return false;

  // We don't care about paths to Def that go through ImmedUse so mark it
  // visited and mark non-def operands as used.
  Visited.insert(ImmedUse);
  for (const SDValue &Op : ImmedUse->op_values()) {
    SDNode *N = Op.getNode();
    // Ignore chain deps (they are validated by
    // HandleMergeInputChains) and immediate uses
    if ((Op.getValueType() == MVT::Other && IgnoreChains) || N == Def)
      continue;
    if (!Visited.insert(N).second)
      continue;
    WorkList.push_back(N);
  }

  // Initialize worklist to operands of Root.
  if (Root != ImmedUse) {
    for (const SDValue &Op : Root->op_values()) {
      SDNode *N = Op.getNode();
      // Ignore chains (they are validated by HandleMergeInputChains)
      if ((Op.getValueType() == MVT::Other && IgnoreChains) || N == Def)
        continue;
      if (!Visited.insert(N).second)
        continue;
      WorkList.push_back(N);
    }
  }

  return SDNode::hasPredecessorHelper(Def, Visited, WorkList, 0, true);
}

/// IsProfitableToFold - Returns true if it's profitable to fold the specific
/// operand node N of U during instruction selection that starts at Root.
bool SelectionDAGISel::IsProfitableToFold(SDValue N, SDNode *U,
                                          SDNode *Root) const {
  if (OptLevel == CodeGenOptLevel::None)
    return false;
  return N.hasOneUse();
}

/// IsLegalToFold - Returns true if the specific operand node N of
/// U can be folded during instruction selection that starts at Root.
bool SelectionDAGISel::IsLegalToFold(SDValue N, SDNode *U, SDNode *Root,
                                     CodeGenOptLevel OptLevel,
                                     bool IgnoreChains) {
  if (OptLevel == CodeGenOptLevel::None)
    return false;

  // If Root use can somehow reach N through a path that doesn't contain
  // U then folding N would create a cycle. e.g. In the following
  // diagram, Root can reach N through X. If N is folded into Root, then
  // X is both a predecessor and a successor of U.
  //
  //          [N*]           //
  //         ^   ^           //
  //        /     \          //
  //      [U*]    [X]?       //
  //        ^     ^          //
  //         \   /           //
  //          \ /            //
  //         [Root*]         //
  //
  // * indicates nodes to be folded together.
  //
  // If Root produces glue, then it gets (even more) interesting. Since it
  // will be "glued" together with its glue use in the scheduler, we need to
  // check if it might reach N.
  //
  //          [N*]           //
  //         ^   ^           //
  //        /     \          //
  //      [U*]    [X]?       //
  //        ^       ^        //
  //         \       \       //
  //          \      |       //
  //         [Root*] |       //
  //          ^      |       //
  //          f      |       //
  //          |      /       //
  //         [Y]    /        //
  //           ^   /         //
  //           f  /          //
  //           | /           //
  //          [GU]           //
  //
  // If GU (glue use) indirectly reaches N (the load), and Root folds N
  // (call it Fold), then X is a predecessor of GU and a successor of
  // Fold. But since Fold and GU are glued together, this will create
  // a cycle in the scheduling graph.

  // If the node has glue, walk down the graph to the "lowest" node in the
  // glueged set.
  EVT VT = Root->getValueType(Root->getNumValues()-1);
  while (VT == MVT::Glue) {
    SDNode *GU = findGlueUse(Root);
    if (!GU)
      break;
    Root = GU;
    VT = Root->getValueType(Root->getNumValues()-1);

    // If our query node has a glue result with a use, we've walked up it.  If
    // the user (which has already been selected) has a chain or indirectly uses
    // the chain, HandleMergeInputChains will not consider it.  Because of
    // this, we cannot ignore chains in this predicate.
    IgnoreChains = false;
  }

  return !findNonImmUse(Root, N.getNode(), U, IgnoreChains);
}

void SelectionDAGISel::Select_INLINEASM(SDNode *N) {
  SDLoc DL(N);

  std::vector<SDValue> Ops(N->op_begin(), N->op_end());
  SelectInlineAsmMemoryOperands(Ops, DL);

  const EVT VTs[] = {MVT::Other, MVT::Glue};
  SDValue New = CurDAG->getNode(N->getOpcode(), DL, VTs, Ops);
  New->setNodeId(-1);
  ReplaceUses(N, New.getNode());
  CurDAG->RemoveDeadNode(N);
}

void SelectionDAGISel::Select_READ_REGISTER(SDNode *Op) {
  SDLoc dl(Op);
  MDNodeSDNode *MD = cast<MDNodeSDNode>(Op->getOperand(1));
  const MDString *RegStr = cast<MDString>(MD->getMD()->getOperand(0));

  EVT VT = Op->getValueType(0);
  LLT Ty = VT.isSimple() ? getLLTForMVT(VT.getSimpleVT()) : LLT();
  Register Reg =
      TLI->getRegisterByName(RegStr->getString().data(), Ty,
                             CurDAG->getMachineFunction());
  SDValue New = CurDAG->getCopyFromReg(
                        Op->getOperand(0), dl, Reg, Op->getValueType(0));
  New->setNodeId(-1);
  ReplaceUses(Op, New.getNode());
  CurDAG->RemoveDeadNode(Op);
}

void SelectionDAGISel::Select_WRITE_REGISTER(SDNode *Op) {
  SDLoc dl(Op);
  MDNodeSDNode *MD = cast<MDNodeSDNode>(Op->getOperand(1));
  const MDString *RegStr = cast<MDString>(MD->getMD()->getOperand(0));

  EVT VT = Op->getOperand(2).getValueType();
  LLT Ty = VT.isSimple() ? getLLTForMVT(VT.getSimpleVT()) : LLT();

  Register Reg = TLI->getRegisterByName(RegStr->getString().data(), Ty,
                                        CurDAG->getMachineFunction());
  SDValue New = CurDAG->getCopyToReg(
                        Op->getOperand(0), dl, Reg, Op->getOperand(2));
  New->setNodeId(-1);
  ReplaceUses(Op, New.getNode());
  CurDAG->RemoveDeadNode(Op);
}

void SelectionDAGISel::Select_UNDEF(SDNode *N) {
  CurDAG->SelectNodeTo(N, TargetOpcode::IMPLICIT_DEF, N->getValueType(0));
}

void SelectionDAGISel::Select_FREEZE(SDNode *N) {
  // TODO: We don't have FREEZE pseudo-instruction in MachineInstr-level now.
  // If FREEZE instruction is added later, the code below must be changed as
  // well.
  CurDAG->SelectNodeTo(N, TargetOpcode::COPY, N->getValueType(0),
                       N->getOperand(0));
}

void SelectionDAGISel::Select_ARITH_FENCE(SDNode *N) {
  CurDAG->SelectNodeTo(N, TargetOpcode::ARITH_FENCE, N->getValueType(0),
                       N->getOperand(0));
}

void SelectionDAGISel::Select_MEMBARRIER(SDNode *N) {
  CurDAG->SelectNodeTo(N, TargetOpcode::MEMBARRIER, N->getValueType(0),
                       N->getOperand(0));
}

void SelectionDAGISel::Select_CONVERGENCECTRL_ANCHOR(SDNode *N) {
  CurDAG->SelectNodeTo(N, TargetOpcode::CONVERGENCECTRL_ANCHOR,
                       N->getValueType(0));
}

void SelectionDAGISel::Select_CONVERGENCECTRL_ENTRY(SDNode *N) {
  CurDAG->SelectNodeTo(N, TargetOpcode::CONVERGENCECTRL_ENTRY,
                       N->getValueType(0));
}

void SelectionDAGISel::Select_CONVERGENCECTRL_LOOP(SDNode *N) {
  CurDAG->SelectNodeTo(N, TargetOpcode::CONVERGENCECTRL_LOOP,
                       N->getValueType(0), N->getOperand(0));
}

void SelectionDAGISel::pushStackMapLiveVariable(SmallVectorImpl<SDValue> &Ops,
                                                SDValue OpVal, SDLoc DL) {
  SDNode *OpNode = OpVal.getNode();

  // FrameIndex nodes should have been directly emitted to TargetFrameIndex
  // nodes at DAG-construction time.
  assert(OpNode->getOpcode() != ISD::FrameIndex);

  if (OpNode->getOpcode() == ISD::Constant) {
    Ops.push_back(
        CurDAG->getTargetConstant(StackMaps::ConstantOp, DL, MVT::i64));
    Ops.push_back(CurDAG->getTargetConstant(OpNode->getAsZExtVal(), DL,
                                            OpVal.getValueType()));
  } else {
    Ops.push_back(OpVal);
  }
}

void SelectionDAGISel::Select_STACKMAP(SDNode *N) {
  SmallVector<SDValue, 32> Ops;
  auto *It = N->op_begin();
  SDLoc DL(N);

  // Stash the chain and glue operands so we can move them to the end.
  SDValue Chain = *It++;
  SDValue InGlue = *It++;

  // <id> operand.
  SDValue ID = *It++;
  assert(ID.getValueType() == MVT::i64);
  Ops.push_back(ID);

  // <numShadowBytes> operand.
  SDValue Shad = *It++;
  assert(Shad.getValueType() == MVT::i32);
  Ops.push_back(Shad);

  // Live variable operands.
  for (; It != N->op_end(); It++)
    pushStackMapLiveVariable(Ops, *It, DL);

  Ops.push_back(Chain);
  Ops.push_back(InGlue);

  SDVTList NodeTys = CurDAG->getVTList(MVT::Other, MVT::Glue);
  CurDAG->SelectNodeTo(N, TargetOpcode::STACKMAP, NodeTys, Ops);
}

void SelectionDAGISel::Select_PATCHPOINT(SDNode *N) {
  SmallVector<SDValue, 32> Ops;
  auto *It = N->op_begin();
  SDLoc DL(N);

  // Cache arguments that will be moved to the end in the target node.
  SDValue Chain = *It++;
  std::optional<SDValue> Glue;
  if (It->getValueType() == MVT::Glue)
    Glue = *It++;
  SDValue RegMask = *It++;

  // <id> operand.
  SDValue ID = *It++;
  assert(ID.getValueType() == MVT::i64);
  Ops.push_back(ID);

  // <numShadowBytes> operand.
  SDValue Shad = *It++;
  assert(Shad.getValueType() == MVT::i32);
  Ops.push_back(Shad);

  // Add the callee.
  Ops.push_back(*It++);

  // Add <numArgs>.
  SDValue NumArgs = *It++;
  assert(NumArgs.getValueType() == MVT::i32);
  Ops.push_back(NumArgs);

  // Calling convention.
  Ops.push_back(*It++);

  // Push the args for the call.
  for (uint64_t I = NumArgs->getAsZExtVal(); I != 0; I--)
    Ops.push_back(*It++);

  // Now push the live variables.
  for (; It != N->op_end(); It++)
    pushStackMapLiveVariable(Ops, *It, DL);

  // Finally, the regmask, chain and (if present) glue are moved to the end.
  Ops.push_back(RegMask);
  Ops.push_back(Chain);
  if (Glue.has_value())
    Ops.push_back(*Glue);

  SDVTList NodeTys = N->getVTList();
  CurDAG->SelectNodeTo(N, TargetOpcode::PATCHPOINT, NodeTys, Ops);
}

/// GetVBR - decode a vbr encoding whose top bit is set.
LLVM_ATTRIBUTE_ALWAYS_INLINE static uint64_t
GetVBR(uint64_t Val, const unsigned char *MatcherTable, unsigned &Idx) {
  assert(Val >= 128 && "Not a VBR");
  Val &= 127;  // Remove first vbr bit.

  unsigned Shift = 7;
  uint64_t NextBits;
  do {
    NextBits = MatcherTable[Idx++];
    Val |= (NextBits&127) << Shift;
    Shift += 7;
  } while (NextBits & 128);

  return Val;
}

void SelectionDAGISel::Select_JUMP_TABLE_DEBUG_INFO(SDNode *N) {
  SDLoc dl(N);
  CurDAG->SelectNodeTo(N, TargetOpcode::JUMP_TABLE_DEBUG_INFO, MVT::Glue,
                       CurDAG->getTargetConstant(N->getConstantOperandVal(1),
                                                 dl, MVT::i64, true));
}

/// When a match is complete, this method updates uses of interior chain results
/// to use the new results.
void SelectionDAGISel::UpdateChains(
    SDNode *NodeToMatch, SDValue InputChain,
    SmallVectorImpl<SDNode *> &ChainNodesMatched, bool isMorphNodeTo) {
  SmallVector<SDNode*, 4> NowDeadNodes;

  // Now that all the normal results are replaced, we replace the chain and
  // glue results if present.
  if (!ChainNodesMatched.empty()) {
    assert(InputChain.getNode() &&
           "Matched input chains but didn't produce a chain");
    // Loop over all of the nodes we matched that produced a chain result.
    // Replace all the chain results with the final chain we ended up with.
    for (unsigned i = 0, e = ChainNodesMatched.size(); i != e; ++i) {
      SDNode *ChainNode = ChainNodesMatched[i];
      // If ChainNode is null, it's because we replaced it on a previous
      // iteration and we cleared it out of the map. Just skip it.
      if (!ChainNode)
        continue;

      assert(ChainNode->getOpcode() != ISD::DELETED_NODE &&
             "Deleted node left in chain");

      // Don't replace the results of the root node if we're doing a
      // MorphNodeTo.
      if (ChainNode == NodeToMatch && isMorphNodeTo)
        continue;

      SDValue ChainVal = SDValue(ChainNode, ChainNode->getNumValues()-1);
      if (ChainVal.getValueType() == MVT::Glue)
        ChainVal = ChainVal.getValue(ChainVal->getNumValues()-2);
      assert(ChainVal.getValueType() == MVT::Other && "Not a chain?");
      SelectionDAG::DAGNodeDeletedListener NDL(
          *CurDAG, [&](SDNode *N, SDNode *E) {
            std::replace(ChainNodesMatched.begin(), ChainNodesMatched.end(), N,
                         static_cast<SDNode *>(nullptr));
          });
      if (ChainNode->getOpcode() != ISD::TokenFactor)
        ReplaceUses(ChainVal, InputChain);

      // If the node became dead and we haven't already seen it, delete it.
      if (ChainNode != NodeToMatch && ChainNode->use_empty() &&
          !llvm::is_contained(NowDeadNodes, ChainNode))
        NowDeadNodes.push_back(ChainNode);
    }
  }

  if (!NowDeadNodes.empty())
    CurDAG->RemoveDeadNodes(NowDeadNodes);

  LLVM_DEBUG(dbgs() << "ISEL: Match complete!\n");
}

/// HandleMergeInputChains - This implements the OPC_EmitMergeInputChains
/// operation for when the pattern matched at least one node with a chains.  The
/// input vector contains a list of all of the chained nodes that we match.  We
/// must determine if this is a valid thing to cover (i.e. matching it won't
/// induce cycles in the DAG) and if so, creating a TokenFactor node. that will
/// be used as the input node chain for the generated nodes.
static SDValue
HandleMergeInputChains(SmallVectorImpl<SDNode*> &ChainNodesMatched,
                       SelectionDAG *CurDAG) {

  SmallPtrSet<const SDNode *, 16> Visited;
  SmallVector<const SDNode *, 8> Worklist;
  SmallVector<SDValue, 3> InputChains;
  unsigned int Max = 8192;

  // Quick exit on trivial merge.
  if (ChainNodesMatched.size() == 1)
    return ChainNodesMatched[0]->getOperand(0);

  // Add chains that aren't already added (internal). Peek through
  // token factors.
  std::function<void(const SDValue)> AddChains = [&](const SDValue V) {
    if (V.getValueType() != MVT::Other)
      return;
    if (V->getOpcode() == ISD::EntryToken)
      return;
    if (!Visited.insert(V.getNode()).second)
      return;
    if (V->getOpcode() == ISD::TokenFactor) {
      for (const SDValue &Op : V->op_values())
        AddChains(Op);
    } else
      InputChains.push_back(V);
  };

  for (auto *N : ChainNodesMatched) {
    Worklist.push_back(N);
    Visited.insert(N);
  }

  while (!Worklist.empty())
    AddChains(Worklist.pop_back_val()->getOperand(0));

  // Skip the search if there are no chain dependencies.
  if (InputChains.size() == 0)
    return CurDAG->getEntryNode();

  // If one of these chains is a successor of input, we must have a
  // node that is both the predecessor and successor of the
  // to-be-merged nodes. Fail.
  Visited.clear();
  for (SDValue V : InputChains)
    Worklist.push_back(V.getNode());

  for (auto *N : ChainNodesMatched)
    if (SDNode::hasPredecessorHelper(N, Visited, Worklist, Max, true))
      return SDValue();

  // Return merged chain.
  if (InputChains.size() == 1)
    return InputChains[0];
  return CurDAG->getNode(ISD::TokenFactor, SDLoc(ChainNodesMatched[0]),
                         MVT::Other, InputChains);
}

/// MorphNode - Handle morphing a node in place for the selector.
SDNode *SelectionDAGISel::
MorphNode(SDNode *Node, unsigned TargetOpc, SDVTList VTList,
          ArrayRef<SDValue> Ops, unsigned EmitNodeInfo) {
  // It is possible we're using MorphNodeTo to replace a node with no
  // normal results with one that has a normal result (or we could be
  // adding a chain) and the input could have glue and chains as well.
  // In this case we need to shift the operands down.
  // FIXME: This is a horrible hack and broken in obscure cases, no worse
  // than the old isel though.
  int OldGlueResultNo = -1, OldChainResultNo = -1;

  unsigned NTMNumResults = Node->getNumValues();
  if (Node->getValueType(NTMNumResults-1) == MVT::Glue) {
    OldGlueResultNo = NTMNumResults-1;
    if (NTMNumResults != 1 &&
        Node->getValueType(NTMNumResults-2) == MVT::Other)
      OldChainResultNo = NTMNumResults-2;
  } else if (Node->getValueType(NTMNumResults-1) == MVT::Other)
    OldChainResultNo = NTMNumResults-1;

  // Call the underlying SelectionDAG routine to do the transmogrification. Note
  // that this deletes operands of the old node that become dead.
  SDNode *Res = CurDAG->MorphNodeTo(Node, ~TargetOpc, VTList, Ops);

  // MorphNodeTo can operate in two ways: if an existing node with the
  // specified operands exists, it can just return it.  Otherwise, it
  // updates the node in place to have the requested operands.
  if (Res == Node) {
    // If we updated the node in place, reset the node ID.  To the isel,
    // this should be just like a newly allocated machine node.
    Res->setNodeId(-1);
  }

  unsigned ResNumResults = Res->getNumValues();
  // Move the glue if needed.
  if ((EmitNodeInfo & OPFL_GlueOutput) && OldGlueResultNo != -1 &&
      static_cast<unsigned>(OldGlueResultNo) != ResNumResults - 1)
    ReplaceUses(SDValue(Node, OldGlueResultNo),
                SDValue(Res, ResNumResults - 1));

  if ((EmitNodeInfo & OPFL_GlueOutput) != 0)
    --ResNumResults;

  // Move the chain reference if needed.
  if ((EmitNodeInfo & OPFL_Chain) && OldChainResultNo != -1 &&
      static_cast<unsigned>(OldChainResultNo) != ResNumResults - 1)
    ReplaceUses(SDValue(Node, OldChainResultNo),
                SDValue(Res, ResNumResults - 1));

  // Otherwise, no replacement happened because the node already exists. Replace
  // Uses of the old node with the new one.
  if (Res != Node) {
    ReplaceNode(Node, Res);
  } else {
    EnforceNodeIdInvariant(Res);
  }

  return Res;
}

/// CheckSame - Implements OP_CheckSame.
LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckSame(const unsigned char *MatcherTable, unsigned &MatcherIndex, SDValue N,
          const SmallVectorImpl<std::pair<SDValue, SDNode *>> &RecordedNodes) {
  // Accept if it is exactly the same as a previously recorded node.
  unsigned RecNo = MatcherTable[MatcherIndex++];
  assert(RecNo < RecordedNodes.size() && "Invalid CheckSame");
  return N == RecordedNodes[RecNo].first;
}

/// CheckChildSame - Implements OP_CheckChildXSame.
LLVM_ATTRIBUTE_ALWAYS_INLINE static bool CheckChildSame(
    const unsigned char *MatcherTable, unsigned &MatcherIndex, SDValue N,
    const SmallVectorImpl<std::pair<SDValue, SDNode *>> &RecordedNodes,
    unsigned ChildNo) {
  if (ChildNo >= N.getNumOperands())
    return false;  // Match fails if out of range child #.
  return ::CheckSame(MatcherTable, MatcherIndex, N.getOperand(ChildNo),
                     RecordedNodes);
}

/// CheckPatternPredicate - Implements OP_CheckPatternPredicate.
LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckPatternPredicate(unsigned Opcode, const unsigned char *MatcherTable,
                      unsigned &MatcherIndex, const SelectionDAGISel &SDISel) {
  bool TwoBytePredNo =
      Opcode == SelectionDAGISel::OPC_CheckPatternPredicateTwoByte;
  unsigned PredNo =
      TwoBytePredNo || Opcode == SelectionDAGISel::OPC_CheckPatternPredicate
          ? MatcherTable[MatcherIndex++]
          : Opcode - SelectionDAGISel::OPC_CheckPatternPredicate0;
  if (TwoBytePredNo)
    PredNo |= MatcherTable[MatcherIndex++] << 8;
  return SDISel.CheckPatternPredicate(PredNo);
}

/// CheckNodePredicate - Implements OP_CheckNodePredicate.
LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckNodePredicate(unsigned Opcode, const unsigned char *MatcherTable,
                   unsigned &MatcherIndex, const SelectionDAGISel &SDISel,
                   SDNode *N) {
  unsigned PredNo = Opcode == SelectionDAGISel::OPC_CheckPredicate
                        ? MatcherTable[MatcherIndex++]
                        : Opcode - SelectionDAGISel::OPC_CheckPredicate0;
  return SDISel.CheckNodePredicate(N, PredNo);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckOpcode(const unsigned char *MatcherTable, unsigned &MatcherIndex,
            SDNode *N) {
  uint16_t Opc = MatcherTable[MatcherIndex++];
  Opc |= static_cast<uint16_t>(MatcherTable[MatcherIndex++]) << 8;
  return N->getOpcode() == Opc;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool CheckType(MVT::SimpleValueType VT,
                                                   SDValue N,
                                                   const TargetLowering *TLI,
                                                   const DataLayout &DL) {
  if (N.getValueType() == VT)
    return true;

  // Handle the case when VT is iPTR.
  return VT == MVT::iPTR && N.getValueType() == TLI->getPointerTy(DL);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckChildType(MVT::SimpleValueType VT, SDValue N, const TargetLowering *TLI,
               const DataLayout &DL, unsigned ChildNo) {
  if (ChildNo >= N.getNumOperands())
    return false; // Match fails if out of range child #.
  return ::CheckType(VT, N.getOperand(ChildNo), TLI, DL);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckCondCode(const unsigned char *MatcherTable, unsigned &MatcherIndex,
              SDValue N) {
  return cast<CondCodeSDNode>(N)->get() ==
         static_cast<ISD::CondCode>(MatcherTable[MatcherIndex++]);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckChild2CondCode(const unsigned char *MatcherTable, unsigned &MatcherIndex,
                    SDValue N) {
  if (2 >= N.getNumOperands())
    return false;
  return ::CheckCondCode(MatcherTable, MatcherIndex, N.getOperand(2));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckValueType(const unsigned char *MatcherTable, unsigned &MatcherIndex,
               SDValue N, const TargetLowering *TLI, const DataLayout &DL) {
  MVT::SimpleValueType VT =
      static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
  if (cast<VTSDNode>(N)->getVT() == VT)
    return true;

  // Handle the case when VT is iPTR.
  return VT == MVT::iPTR && cast<VTSDNode>(N)->getVT() == TLI->getPointerTy(DL);
}

// Bit 0 stores the sign of the immediate. The upper bits contain the magnitude
// shifted left by 1.
static uint64_t decodeSignRotatedValue(uint64_t V) {
  if ((V & 1) == 0)
    return V >> 1;
  if (V != 1)
    return -(V >> 1);
  // There is no such thing as -0 with integers.  "-0" really means MININT.
  return 1ULL << 63;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckInteger(const unsigned char *MatcherTable, unsigned &MatcherIndex,
             SDValue N) {
  int64_t Val = MatcherTable[MatcherIndex++];
  if (Val & 128)
    Val = GetVBR(Val, MatcherTable, MatcherIndex);

  Val = decodeSignRotatedValue(Val);

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N);
  return C && C->getAPIntValue().trySExtValue() == Val;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckChildInteger(const unsigned char *MatcherTable, unsigned &MatcherIndex,
                  SDValue N, unsigned ChildNo) {
  if (ChildNo >= N.getNumOperands())
    return false;  // Match fails if out of range child #.
  return ::CheckInteger(MatcherTable, MatcherIndex, N.getOperand(ChildNo));
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckAndImm(const unsigned char *MatcherTable, unsigned &MatcherIndex,
            SDValue N, const SelectionDAGISel &SDISel) {
  int64_t Val = MatcherTable[MatcherIndex++];
  if (Val & 128)
    Val = GetVBR(Val, MatcherTable, MatcherIndex);

  if (N->getOpcode() != ISD::AND) return false;

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  return C && SDISel.CheckAndMask(N.getOperand(0), C, Val);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE static bool
CheckOrImm(const unsigned char *MatcherTable, unsigned &MatcherIndex, SDValue N,
           const SelectionDAGISel &SDISel) {
  int64_t Val = MatcherTable[MatcherIndex++];
  if (Val & 128)
    Val = GetVBR(Val, MatcherTable, MatcherIndex);

  if (N->getOpcode() != ISD::OR) return false;

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  return C && SDISel.CheckOrMask(N.getOperand(0), C, Val);
}

/// IsPredicateKnownToFail - If we know how and can do so without pushing a
/// scope, evaluate the current node.  If the current predicate is known to
/// fail, set Result=true and return anything.  If the current predicate is
/// known to pass, set Result=false and return the MatcherIndex to continue
/// with.  If the current predicate is unknown, set Result=false and return the
/// MatcherIndex to continue with.
static unsigned IsPredicateKnownToFail(const unsigned char *Table,
                                       unsigned Index, SDValue N,
                                       bool &Result,
                                       const SelectionDAGISel &SDISel,
                  SmallVectorImpl<std::pair<SDValue, SDNode*>> &RecordedNodes) {
  unsigned Opcode = Table[Index++];
  switch (Opcode) {
  default:
    Result = false;
    return Index-1;  // Could not evaluate this predicate.
  case SelectionDAGISel::OPC_CheckSame:
    Result = !::CheckSame(Table, Index, N, RecordedNodes);
    return Index;
  case SelectionDAGISel::OPC_CheckChild0Same:
  case SelectionDAGISel::OPC_CheckChild1Same:
  case SelectionDAGISel::OPC_CheckChild2Same:
  case SelectionDAGISel::OPC_CheckChild3Same:
    Result = !::CheckChildSame(Table, Index, N, RecordedNodes,
                        Table[Index-1] - SelectionDAGISel::OPC_CheckChild0Same);
    return Index;
  case SelectionDAGISel::OPC_CheckPatternPredicate:
  case SelectionDAGISel::OPC_CheckPatternPredicate0:
  case SelectionDAGISel::OPC_CheckPatternPredicate1:
  case SelectionDAGISel::OPC_CheckPatternPredicate2:
  case SelectionDAGISel::OPC_CheckPatternPredicate3:
  case SelectionDAGISel::OPC_CheckPatternPredicate4:
  case SelectionDAGISel::OPC_CheckPatternPredicate5:
  case SelectionDAGISel::OPC_CheckPatternPredicate6:
  case SelectionDAGISel::OPC_CheckPatternPredicate7:
  case SelectionDAGISel::OPC_CheckPatternPredicateTwoByte:
    Result = !::CheckPatternPredicate(Opcode, Table, Index, SDISel);
    return Index;
  case SelectionDAGISel::OPC_CheckPredicate:
  case SelectionDAGISel::OPC_CheckPredicate0:
  case SelectionDAGISel::OPC_CheckPredicate1:
  case SelectionDAGISel::OPC_CheckPredicate2:
  case SelectionDAGISel::OPC_CheckPredicate3:
  case SelectionDAGISel::OPC_CheckPredicate4:
  case SelectionDAGISel::OPC_CheckPredicate5:
  case SelectionDAGISel::OPC_CheckPredicate6:
  case SelectionDAGISel::OPC_CheckPredicate7:
    Result = !::CheckNodePredicate(Opcode, Table, Index, SDISel, N.getNode());
    return Index;
  case SelectionDAGISel::OPC_CheckOpcode:
    Result = !::CheckOpcode(Table, Index, N.getNode());
    return Index;
  case SelectionDAGISel::OPC_CheckType:
  case SelectionDAGISel::OPC_CheckTypeI32:
  case SelectionDAGISel::OPC_CheckTypeI64: {
    MVT::SimpleValueType VT;
    switch (Opcode) {
    case SelectionDAGISel::OPC_CheckTypeI32:
      VT = MVT::i32;
      break;
    case SelectionDAGISel::OPC_CheckTypeI64:
      VT = MVT::i64;
      break;
    default:
      VT = static_cast<MVT::SimpleValueType>(Table[Index++]);
      break;
    }
    Result = !::CheckType(VT, N, SDISel.TLI, SDISel.CurDAG->getDataLayout());
    return Index;
  }
  case SelectionDAGISel::OPC_CheckTypeRes: {
    unsigned Res = Table[Index++];
    Result = !::CheckType(static_cast<MVT::SimpleValueType>(Table[Index++]),
                          N.getValue(Res), SDISel.TLI,
                          SDISel.CurDAG->getDataLayout());
    return Index;
  }
  case SelectionDAGISel::OPC_CheckChild0Type:
  case SelectionDAGISel::OPC_CheckChild1Type:
  case SelectionDAGISel::OPC_CheckChild2Type:
  case SelectionDAGISel::OPC_CheckChild3Type:
  case SelectionDAGISel::OPC_CheckChild4Type:
  case SelectionDAGISel::OPC_CheckChild5Type:
  case SelectionDAGISel::OPC_CheckChild6Type:
  case SelectionDAGISel::OPC_CheckChild7Type:
  case SelectionDAGISel::OPC_CheckChild0TypeI32:
  case SelectionDAGISel::OPC_CheckChild1TypeI32:
  case SelectionDAGISel::OPC_CheckChild2TypeI32:
  case SelectionDAGISel::OPC_CheckChild3TypeI32:
  case SelectionDAGISel::OPC_CheckChild4TypeI32:
  case SelectionDAGISel::OPC_CheckChild5TypeI32:
  case SelectionDAGISel::OPC_CheckChild6TypeI32:
  case SelectionDAGISel::OPC_CheckChild7TypeI32:
  case SelectionDAGISel::OPC_CheckChild0TypeI64:
  case SelectionDAGISel::OPC_CheckChild1TypeI64:
  case SelectionDAGISel::OPC_CheckChild2TypeI64:
  case SelectionDAGISel::OPC_CheckChild3TypeI64:
  case SelectionDAGISel::OPC_CheckChild4TypeI64:
  case SelectionDAGISel::OPC_CheckChild5TypeI64:
  case SelectionDAGISel::OPC_CheckChild6TypeI64:
  case SelectionDAGISel::OPC_CheckChild7TypeI64: {
    MVT::SimpleValueType VT;
    unsigned ChildNo;
    if (Opcode >= SelectionDAGISel::OPC_CheckChild0TypeI32 &&
        Opcode <= SelectionDAGISel::OPC_CheckChild7TypeI32) {
      VT = MVT::i32;
      ChildNo = Opcode - SelectionDAGISel::OPC_CheckChild0TypeI32;
    } else if (Opcode >= SelectionDAGISel::OPC_CheckChild0TypeI64 &&
               Opcode <= SelectionDAGISel::OPC_CheckChild7TypeI64) {
      VT = MVT::i64;
      ChildNo = Opcode - SelectionDAGISel::OPC_CheckChild0TypeI64;
    } else {
      VT = static_cast<MVT::SimpleValueType>(Table[Index++]);
      ChildNo = Opcode - SelectionDAGISel::OPC_CheckChild0Type;
    }
    Result = !::CheckChildType(VT, N, SDISel.TLI,
                               SDISel.CurDAG->getDataLayout(), ChildNo);
    return Index;
  }
  case SelectionDAGISel::OPC_CheckCondCode:
    Result = !::CheckCondCode(Table, Index, N);
    return Index;
  case SelectionDAGISel::OPC_CheckChild2CondCode:
    Result = !::CheckChild2CondCode(Table, Index, N);
    return Index;
  case SelectionDAGISel::OPC_CheckValueType:
    Result = !::CheckValueType(Table, Index, N, SDISel.TLI,
                               SDISel.CurDAG->getDataLayout());
    return Index;
  case SelectionDAGISel::OPC_CheckInteger:
    Result = !::CheckInteger(Table, Index, N);
    return Index;
  case SelectionDAGISel::OPC_CheckChild0Integer:
  case SelectionDAGISel::OPC_CheckChild1Integer:
  case SelectionDAGISel::OPC_CheckChild2Integer:
  case SelectionDAGISel::OPC_CheckChild3Integer:
  case SelectionDAGISel::OPC_CheckChild4Integer:
    Result = !::CheckChildInteger(Table, Index, N,
                     Table[Index-1] - SelectionDAGISel::OPC_CheckChild0Integer);
    return Index;
  case SelectionDAGISel::OPC_CheckAndImm:
    Result = !::CheckAndImm(Table, Index, N, SDISel);
    return Index;
  case SelectionDAGISel::OPC_CheckOrImm:
    Result = !::CheckOrImm(Table, Index, N, SDISel);
    return Index;
  }
}

namespace {

struct MatchScope {
  /// FailIndex - If this match fails, this is the index to continue with.
  unsigned FailIndex;

  /// NodeStack - The node stack when the scope was formed.
  SmallVector<SDValue, 4> NodeStack;

  /// NumRecordedNodes - The number of recorded nodes when the scope was formed.
  unsigned NumRecordedNodes;

  /// NumMatchedMemRefs - The number of matched memref entries.
  unsigned NumMatchedMemRefs;

  /// InputChain/InputGlue - The current chain/glue
  SDValue InputChain, InputGlue;

  /// HasChainNodesMatched - True if the ChainNodesMatched list is non-empty.
  bool HasChainNodesMatched;
};

/// \A DAG update listener to keep the matching state
/// (i.e. RecordedNodes and MatchScope) uptodate if the target is allowed to
/// change the DAG while matching.  X86 addressing mode matcher is an example
/// for this.
class MatchStateUpdater : public SelectionDAG::DAGUpdateListener
{
  SDNode **NodeToMatch;
  SmallVectorImpl<std::pair<SDValue, SDNode *>> &RecordedNodes;
  SmallVectorImpl<MatchScope> &MatchScopes;

public:
  MatchStateUpdater(SelectionDAG &DAG, SDNode **NodeToMatch,
                    SmallVectorImpl<std::pair<SDValue, SDNode *>> &RN,
                    SmallVectorImpl<MatchScope> &MS)
      : SelectionDAG::DAGUpdateListener(DAG), NodeToMatch(NodeToMatch),
        RecordedNodes(RN), MatchScopes(MS) {}

  void NodeDeleted(SDNode *N, SDNode *E) override {
    // Some early-returns here to avoid the search if we deleted the node or
    // if the update comes from MorphNodeTo (MorphNodeTo is the last thing we
    // do, so it's unnecessary to update matching state at that point).
    // Neither of these can occur currently because we only install this
    // update listener during matching a complex patterns.
    if (!E || E->isMachineOpcode())
      return;
    // Check if NodeToMatch was updated.
    if (N == *NodeToMatch)
      *NodeToMatch = E;
    // Performing linear search here does not matter because we almost never
    // run this code.  You'd have to have a CSE during complex pattern
    // matching.
    for (auto &I : RecordedNodes)
      if (I.first.getNode() == N)
        I.first.setNode(E);

    for (auto &I : MatchScopes)
      for (auto &J : I.NodeStack)
        if (J.getNode() == N)
          J.setNode(E);
  }
};

} // end anonymous namespace

void SelectionDAGISel::SelectCodeCommon(SDNode *NodeToMatch,
                                        const unsigned char *MatcherTable,
                                        unsigned TableSize) {
  // FIXME: Should these even be selected?  Handle these cases in the caller?
  switch (NodeToMatch->getOpcode()) {
  default:
    break;
  case ISD::EntryToken:       // These nodes remain the same.
  case ISD::BasicBlock:
  case ISD::Register:
  case ISD::RegisterMask:
  case ISD::HANDLENODE:
  case ISD::MDNODE_SDNODE:
  case ISD::TargetConstant:
  case ISD::TargetConstantFP:
  case ISD::TargetConstantPool:
  case ISD::TargetFrameIndex:
  case ISD::TargetExternalSymbol:
  case ISD::MCSymbol:
  case ISD::TargetBlockAddress:
  case ISD::TargetJumpTable:
  case ISD::TargetGlobalTLSAddress:
  case ISD::TargetGlobalAddress:
  case ISD::TokenFactor:
  case ISD::CopyFromReg:
  case ISD::CopyToReg:
  case ISD::EH_LABEL:
  case ISD::ANNOTATION_LABEL:
  case ISD::LIFETIME_START:
  case ISD::LIFETIME_END:
  case ISD::PSEUDO_PROBE:
    NodeToMatch->setNodeId(-1); // Mark selected.
    return;
  case ISD::AssertSext:
  case ISD::AssertZext:
  case ISD::AssertAlign:
    ReplaceUses(SDValue(NodeToMatch, 0), NodeToMatch->getOperand(0));
    CurDAG->RemoveDeadNode(NodeToMatch);
    return;
  case ISD::INLINEASM:
  case ISD::INLINEASM_BR:
    Select_INLINEASM(NodeToMatch);
    return;
  case ISD::READ_REGISTER:
    Select_READ_REGISTER(NodeToMatch);
    return;
  case ISD::WRITE_REGISTER:
    Select_WRITE_REGISTER(NodeToMatch);
    return;
  case ISD::UNDEF:
    Select_UNDEF(NodeToMatch);
    return;
  case ISD::FREEZE:
    Select_FREEZE(NodeToMatch);
    return;
  case ISD::ARITH_FENCE:
    Select_ARITH_FENCE(NodeToMatch);
    return;
  case ISD::MEMBARRIER:
    Select_MEMBARRIER(NodeToMatch);
    return;
  case ISD::STACKMAP:
    Select_STACKMAP(NodeToMatch);
    return;
  case ISD::PATCHPOINT:
    Select_PATCHPOINT(NodeToMatch);
    return;
  case ISD::JUMP_TABLE_DEBUG_INFO:
    Select_JUMP_TABLE_DEBUG_INFO(NodeToMatch);
    return;
  case ISD::CONVERGENCECTRL_ANCHOR:
    Select_CONVERGENCECTRL_ANCHOR(NodeToMatch);
    return;
  case ISD::CONVERGENCECTRL_ENTRY:
    Select_CONVERGENCECTRL_ENTRY(NodeToMatch);
    return;
  case ISD::CONVERGENCECTRL_LOOP:
    Select_CONVERGENCECTRL_LOOP(NodeToMatch);
    return;
  }

  assert(!NodeToMatch->isMachineOpcode() && "Node already selected!");

  // Set up the node stack with NodeToMatch as the only node on the stack.
  SmallVector<SDValue, 8> NodeStack;
  SDValue N = SDValue(NodeToMatch, 0);
  NodeStack.push_back(N);

  // MatchScopes - Scopes used when matching, if a match failure happens, this
  // indicates where to continue checking.
  SmallVector<MatchScope, 8> MatchScopes;

  // RecordedNodes - This is the set of nodes that have been recorded by the
  // state machine.  The second value is the parent of the node, or null if the
  // root is recorded.
  SmallVector<std::pair<SDValue, SDNode*>, 8> RecordedNodes;

  // MatchedMemRefs - This is the set of MemRef's we've seen in the input
  // pattern.
  SmallVector<MachineMemOperand*, 2> MatchedMemRefs;

  // These are the current input chain and glue for use when generating nodes.
  // Various Emit operations change these.  For example, emitting a copytoreg
  // uses and updates these.
  SDValue InputChain, InputGlue;

  // ChainNodesMatched - If a pattern matches nodes that have input/output
  // chains, the OPC_EmitMergeInputChains operation is emitted which indicates
  // which ones they are.  The result is captured into this list so that we can
  // update the chain results when the pattern is complete.
  SmallVector<SDNode*, 3> ChainNodesMatched;

  LLVM_DEBUG(dbgs() << "ISEL: Starting pattern match\n");

  // Determine where to start the interpreter.  Normally we start at opcode #0,
  // but if the state machine starts with an OPC_SwitchOpcode, then we
  // accelerate the first lookup (which is guaranteed to be hot) with the
  // OpcodeOffset table.
  unsigned MatcherIndex = 0;

  if (!OpcodeOffset.empty()) {
    // Already computed the OpcodeOffset table, just index into it.
    if (N.getOpcode() < OpcodeOffset.size())
      MatcherIndex = OpcodeOffset[N.getOpcode()];
    LLVM_DEBUG(dbgs() << "  Initial Opcode index to " << MatcherIndex << "\n");

  } else if (MatcherTable[0] == OPC_SwitchOpcode) {
    // Otherwise, the table isn't computed, but the state machine does start
    // with an OPC_SwitchOpcode instruction.  Populate the table now, since this
    // is the first time we're selecting an instruction.
    unsigned Idx = 1;
    while (true) {
      // Get the size of this case.
      unsigned CaseSize = MatcherTable[Idx++];
      if (CaseSize & 128)
        CaseSize = GetVBR(CaseSize, MatcherTable, Idx);
      if (CaseSize == 0) break;

      // Get the opcode, add the index to the table.
      uint16_t Opc = MatcherTable[Idx++];
      Opc |= static_cast<uint16_t>(MatcherTable[Idx++]) << 8;
      if (Opc >= OpcodeOffset.size())
        OpcodeOffset.resize((Opc+1)*2);
      OpcodeOffset[Opc] = Idx;
      Idx += CaseSize;
    }

    // Okay, do the lookup for the first opcode.
    if (N.getOpcode() < OpcodeOffset.size())
      MatcherIndex = OpcodeOffset[N.getOpcode()];
  }

  while (true) {
    assert(MatcherIndex < TableSize && "Invalid index");
#ifndef NDEBUG
    unsigned CurrentOpcodeIndex = MatcherIndex;
#endif
    BuiltinOpcodes Opcode =
        static_cast<BuiltinOpcodes>(MatcherTable[MatcherIndex++]);
    switch (Opcode) {
    case OPC_Scope: {
      // Okay, the semantics of this operation are that we should push a scope
      // then evaluate the first child.  However, pushing a scope only to have
      // the first check fail (which then pops it) is inefficient.  If we can
      // determine immediately that the first check (or first several) will
      // immediately fail, don't even bother pushing a scope for them.
      unsigned FailIndex;

      while (true) {
        unsigned NumToSkip = MatcherTable[MatcherIndex++];
        if (NumToSkip & 128)
          NumToSkip = GetVBR(NumToSkip, MatcherTable, MatcherIndex);
        // Found the end of the scope with no match.
        if (NumToSkip == 0) {
          FailIndex = 0;
          break;
        }

        FailIndex = MatcherIndex+NumToSkip;

        unsigned MatcherIndexOfPredicate = MatcherIndex;
        (void)MatcherIndexOfPredicate; // silence warning.

        // If we can't evaluate this predicate without pushing a scope (e.g. if
        // it is a 'MoveParent') or if the predicate succeeds on this node, we
        // push the scope and evaluate the full predicate chain.
        bool Result;
        MatcherIndex = IsPredicateKnownToFail(MatcherTable, MatcherIndex, N,
                                              Result, *this, RecordedNodes);
        if (!Result)
          break;

        LLVM_DEBUG(
            dbgs() << "  Skipped scope entry (due to false predicate) at "
                   << "index " << MatcherIndexOfPredicate << ", continuing at "
                   << FailIndex << "\n");
        ++NumDAGIselRetries;

        // Otherwise, we know that this case of the Scope is guaranteed to fail,
        // move to the next case.
        MatcherIndex = FailIndex;
      }

      // If the whole scope failed to match, bail.
      if (FailIndex == 0) break;

      // Push a MatchScope which indicates where to go if the first child fails
      // to match.
      MatchScope NewEntry;
      NewEntry.FailIndex = FailIndex;
      NewEntry.NodeStack.append(NodeStack.begin(), NodeStack.end());
      NewEntry.NumRecordedNodes = RecordedNodes.size();
      NewEntry.NumMatchedMemRefs = MatchedMemRefs.size();
      NewEntry.InputChain = InputChain;
      NewEntry.InputGlue = InputGlue;
      NewEntry.HasChainNodesMatched = !ChainNodesMatched.empty();
      MatchScopes.push_back(NewEntry);
      continue;
    }
    case OPC_RecordNode: {
      // Remember this node, it may end up being an operand in the pattern.
      SDNode *Parent = nullptr;
      if (NodeStack.size() > 1)
        Parent = NodeStack[NodeStack.size()-2].getNode();
      RecordedNodes.push_back(std::make_pair(N, Parent));
      continue;
    }

    case OPC_RecordChild0: case OPC_RecordChild1:
    case OPC_RecordChild2: case OPC_RecordChild3:
    case OPC_RecordChild4: case OPC_RecordChild5:
    case OPC_RecordChild6: case OPC_RecordChild7: {
      unsigned ChildNo = Opcode-OPC_RecordChild0;
      if (ChildNo >= N.getNumOperands())
        break;  // Match fails if out of range child #.

      RecordedNodes.push_back(std::make_pair(N->getOperand(ChildNo),
                                             N.getNode()));
      continue;
    }
    case OPC_RecordMemRef:
      if (auto *MN = dyn_cast<MemSDNode>(N))
        MatchedMemRefs.push_back(MN->getMemOperand());
      else {
        LLVM_DEBUG(dbgs() << "Expected MemSDNode "; N->dump(CurDAG);
                   dbgs() << '\n');
      }

      continue;

    case OPC_CaptureGlueInput:
      // If the current node has an input glue, capture it in InputGlue.
      if (N->getNumOperands() != 0 &&
          N->getOperand(N->getNumOperands()-1).getValueType() == MVT::Glue)
        InputGlue = N->getOperand(N->getNumOperands()-1);
      continue;

    case OPC_MoveChild: {
      unsigned ChildNo = MatcherTable[MatcherIndex++];
      if (ChildNo >= N.getNumOperands())
        break;  // Match fails if out of range child #.
      N = N.getOperand(ChildNo);
      NodeStack.push_back(N);
      continue;
    }

    case OPC_MoveChild0: case OPC_MoveChild1:
    case OPC_MoveChild2: case OPC_MoveChild3:
    case OPC_MoveChild4: case OPC_MoveChild5:
    case OPC_MoveChild6: case OPC_MoveChild7: {
      unsigned ChildNo = Opcode-OPC_MoveChild0;
      if (ChildNo >= N.getNumOperands())
        break;  // Match fails if out of range child #.
      N = N.getOperand(ChildNo);
      NodeStack.push_back(N);
      continue;
    }

    case OPC_MoveSibling:
    case OPC_MoveSibling0:
    case OPC_MoveSibling1:
    case OPC_MoveSibling2:
    case OPC_MoveSibling3:
    case OPC_MoveSibling4:
    case OPC_MoveSibling5:
    case OPC_MoveSibling6:
    case OPC_MoveSibling7: {
      // Pop the current node off the NodeStack.
      NodeStack.pop_back();
      assert(!NodeStack.empty() && "Node stack imbalance!");
      N = NodeStack.back();

      unsigned SiblingNo = Opcode == OPC_MoveSibling
                               ? MatcherTable[MatcherIndex++]
                               : Opcode - OPC_MoveSibling0;
      if (SiblingNo >= N.getNumOperands())
        break; // Match fails if out of range sibling #.
      N = N.getOperand(SiblingNo);
      NodeStack.push_back(N);
      continue;
    }
    case OPC_MoveParent:
      // Pop the current node off the NodeStack.
      NodeStack.pop_back();
      assert(!NodeStack.empty() && "Node stack imbalance!");
      N = NodeStack.back();
      continue;

    case OPC_CheckSame:
      if (!::CheckSame(MatcherTable, MatcherIndex, N, RecordedNodes)) break;
      continue;

    case OPC_CheckChild0Same: case OPC_CheckChild1Same:
    case OPC_CheckChild2Same: case OPC_CheckChild3Same:
      if (!::CheckChildSame(MatcherTable, MatcherIndex, N, RecordedNodes,
                            Opcode-OPC_CheckChild0Same))
        break;
      continue;

    case OPC_CheckPatternPredicate:
    case OPC_CheckPatternPredicate0:
    case OPC_CheckPatternPredicate1:
    case OPC_CheckPatternPredicate2:
    case OPC_CheckPatternPredicate3:
    case OPC_CheckPatternPredicate4:
    case OPC_CheckPatternPredicate5:
    case OPC_CheckPatternPredicate6:
    case OPC_CheckPatternPredicate7:
    case OPC_CheckPatternPredicateTwoByte:
      if (!::CheckPatternPredicate(Opcode, MatcherTable, MatcherIndex, *this))
        break;
      continue;
    case SelectionDAGISel::OPC_CheckPredicate0:
    case SelectionDAGISel::OPC_CheckPredicate1:
    case SelectionDAGISel::OPC_CheckPredicate2:
    case SelectionDAGISel::OPC_CheckPredicate3:
    case SelectionDAGISel::OPC_CheckPredicate4:
    case SelectionDAGISel::OPC_CheckPredicate5:
    case SelectionDAGISel::OPC_CheckPredicate6:
    case SelectionDAGISel::OPC_CheckPredicate7:
    case OPC_CheckPredicate:
      if (!::CheckNodePredicate(Opcode, MatcherTable, MatcherIndex, *this,
                                N.getNode()))
        break;
      continue;
    case OPC_CheckPredicateWithOperands: {
      unsigned OpNum = MatcherTable[MatcherIndex++];
      SmallVector<SDValue, 8> Operands;

      for (unsigned i = 0; i < OpNum; ++i)
        Operands.push_back(RecordedNodes[MatcherTable[MatcherIndex++]].first);

      unsigned PredNo = MatcherTable[MatcherIndex++];
      if (!CheckNodePredicateWithOperands(N.getNode(), PredNo, Operands))
        break;
      continue;
    }
    case OPC_CheckComplexPat:
    case OPC_CheckComplexPat0:
    case OPC_CheckComplexPat1:
    case OPC_CheckComplexPat2:
    case OPC_CheckComplexPat3:
    case OPC_CheckComplexPat4:
    case OPC_CheckComplexPat5:
    case OPC_CheckComplexPat6:
    case OPC_CheckComplexPat7: {
      unsigned CPNum = Opcode == OPC_CheckComplexPat
                           ? MatcherTable[MatcherIndex++]
                           : Opcode - OPC_CheckComplexPat0;
      unsigned RecNo = MatcherTable[MatcherIndex++];
      assert(RecNo < RecordedNodes.size() && "Invalid CheckComplexPat");

      // If target can modify DAG during matching, keep the matching state
      // consistent.
      std::unique_ptr<MatchStateUpdater> MSU;
      if (ComplexPatternFuncMutatesDAG())
        MSU.reset(new MatchStateUpdater(*CurDAG, &NodeToMatch, RecordedNodes,
                                        MatchScopes));

      if (!CheckComplexPattern(NodeToMatch, RecordedNodes[RecNo].second,
                               RecordedNodes[RecNo].first, CPNum,
                               RecordedNodes))
        break;
      continue;
    }
    case OPC_CheckOpcode:
      if (!::CheckOpcode(MatcherTable, MatcherIndex, N.getNode())) break;
      continue;

    case OPC_CheckType:
    case OPC_CheckTypeI32:
    case OPC_CheckTypeI64:
      MVT::SimpleValueType VT;
      switch (Opcode) {
      case OPC_CheckTypeI32:
        VT = MVT::i32;
        break;
      case OPC_CheckTypeI64:
        VT = MVT::i64;
        break;
      default:
        VT = static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
        break;
      }
      if (!::CheckType(VT, N, TLI, CurDAG->getDataLayout()))
        break;
      continue;

    case OPC_CheckTypeRes: {
      unsigned Res = MatcherTable[MatcherIndex++];
      if (!::CheckType(
              static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]),
              N.getValue(Res), TLI, CurDAG->getDataLayout()))
        break;
      continue;
    }

    case OPC_SwitchOpcode: {
      unsigned CurNodeOpcode = N.getOpcode();
      unsigned SwitchStart = MatcherIndex-1; (void)SwitchStart;
      unsigned CaseSize;
      while (true) {
        // Get the size of this case.
        CaseSize = MatcherTable[MatcherIndex++];
        if (CaseSize & 128)
          CaseSize = GetVBR(CaseSize, MatcherTable, MatcherIndex);
        if (CaseSize == 0) break;

        uint16_t Opc = MatcherTable[MatcherIndex++];
        Opc |= static_cast<uint16_t>(MatcherTable[MatcherIndex++]) << 8;

        // If the opcode matches, then we will execute this case.
        if (CurNodeOpcode == Opc)
          break;

        // Otherwise, skip over this case.
        MatcherIndex += CaseSize;
      }

      // If no cases matched, bail out.
      if (CaseSize == 0) break;

      // Otherwise, execute the case we found.
      LLVM_DEBUG(dbgs() << "  OpcodeSwitch from " << SwitchStart << " to "
                        << MatcherIndex << "\n");
      continue;
    }

    case OPC_SwitchType: {
      MVT CurNodeVT = N.getSimpleValueType();
      unsigned SwitchStart = MatcherIndex-1; (void)SwitchStart;
      unsigned CaseSize;
      while (true) {
        // Get the size of this case.
        CaseSize = MatcherTable[MatcherIndex++];
        if (CaseSize & 128)
          CaseSize = GetVBR(CaseSize, MatcherTable, MatcherIndex);
        if (CaseSize == 0) break;

        MVT CaseVT =
            static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
        if (CaseVT == MVT::iPTR)
          CaseVT = TLI->getPointerTy(CurDAG->getDataLayout());

        // If the VT matches, then we will execute this case.
        if (CurNodeVT == CaseVT)
          break;

        // Otherwise, skip over this case.
        MatcherIndex += CaseSize;
      }

      // If no cases matched, bail out.
      if (CaseSize == 0) break;

      // Otherwise, execute the case we found.
      LLVM_DEBUG(dbgs() << "  TypeSwitch[" << CurNodeVT
                        << "] from " << SwitchStart << " to " << MatcherIndex
                        << '\n');
      continue;
    }
    case OPC_CheckChild0Type:
    case OPC_CheckChild1Type:
    case OPC_CheckChild2Type:
    case OPC_CheckChild3Type:
    case OPC_CheckChild4Type:
    case OPC_CheckChild5Type:
    case OPC_CheckChild6Type:
    case OPC_CheckChild7Type:
    case OPC_CheckChild0TypeI32:
    case OPC_CheckChild1TypeI32:
    case OPC_CheckChild2TypeI32:
    case OPC_CheckChild3TypeI32:
    case OPC_CheckChild4TypeI32:
    case OPC_CheckChild5TypeI32:
    case OPC_CheckChild6TypeI32:
    case OPC_CheckChild7TypeI32:
    case OPC_CheckChild0TypeI64:
    case OPC_CheckChild1TypeI64:
    case OPC_CheckChild2TypeI64:
    case OPC_CheckChild3TypeI64:
    case OPC_CheckChild4TypeI64:
    case OPC_CheckChild5TypeI64:
    case OPC_CheckChild6TypeI64:
    case OPC_CheckChild7TypeI64: {
      MVT::SimpleValueType VT;
      unsigned ChildNo;
      if (Opcode >= SelectionDAGISel::OPC_CheckChild0TypeI32 &&
          Opcode <= SelectionDAGISel::OPC_CheckChild7TypeI32) {
        VT = MVT::i32;
        ChildNo = Opcode - SelectionDAGISel::OPC_CheckChild0TypeI32;
      } else if (Opcode >= SelectionDAGISel::OPC_CheckChild0TypeI64 &&
                 Opcode <= SelectionDAGISel::OPC_CheckChild7TypeI64) {
        VT = MVT::i64;
        ChildNo = Opcode - SelectionDAGISel::OPC_CheckChild0TypeI64;
      } else {
        VT = static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
        ChildNo = Opcode - SelectionDAGISel::OPC_CheckChild0Type;
      }
      if (!::CheckChildType(VT, N, TLI, CurDAG->getDataLayout(), ChildNo))
        break;
      continue;
    }
    case OPC_CheckCondCode:
      if (!::CheckCondCode(MatcherTable, MatcherIndex, N)) break;
      continue;
    case OPC_CheckChild2CondCode:
      if (!::CheckChild2CondCode(MatcherTable, MatcherIndex, N)) break;
      continue;
    case OPC_CheckValueType:
      if (!::CheckValueType(MatcherTable, MatcherIndex, N, TLI,
                            CurDAG->getDataLayout()))
        break;
      continue;
    case OPC_CheckInteger:
      if (!::CheckInteger(MatcherTable, MatcherIndex, N)) break;
      continue;
    case OPC_CheckChild0Integer: case OPC_CheckChild1Integer:
    case OPC_CheckChild2Integer: case OPC_CheckChild3Integer:
    case OPC_CheckChild4Integer:
      if (!::CheckChildInteger(MatcherTable, MatcherIndex, N,
                               Opcode-OPC_CheckChild0Integer)) break;
      continue;
    case OPC_CheckAndImm:
      if (!::CheckAndImm(MatcherTable, MatcherIndex, N, *this)) break;
      continue;
    case OPC_CheckOrImm:
      if (!::CheckOrImm(MatcherTable, MatcherIndex, N, *this)) break;
      continue;
    case OPC_CheckImmAllOnesV:
      if (!ISD::isConstantSplatVectorAllOnes(N.getNode()))
        break;
      continue;
    case OPC_CheckImmAllZerosV:
      if (!ISD::isConstantSplatVectorAllZeros(N.getNode()))
        break;
      continue;

    case OPC_CheckFoldableChainNode: {
      assert(NodeStack.size() != 1 && "No parent node");
      // Verify that all intermediate nodes between the root and this one have
      // a single use (ignoring chains, which are handled in UpdateChains).
      bool HasMultipleUses = false;
      for (unsigned i = 1, e = NodeStack.size()-1; i != e; ++i) {
        unsigned NNonChainUses = 0;
        SDNode *NS = NodeStack[i].getNode();
        for (auto UI = NS->use_begin(), UE = NS->use_end(); UI != UE; ++UI)
          if (UI.getUse().getValueType() != MVT::Other)
            if (++NNonChainUses > 1) {
              HasMultipleUses = true;
              break;
            }
        if (HasMultipleUses) break;
      }
      if (HasMultipleUses) break;

      // Check to see that the target thinks this is profitable to fold and that
      // we can fold it without inducing cycles in the graph.
      if (!IsProfitableToFold(N, NodeStack[NodeStack.size()-2].getNode(),
                              NodeToMatch) ||
          !IsLegalToFold(N, NodeStack[NodeStack.size()-2].getNode(),
                         NodeToMatch, OptLevel,
                         true/*We validate our own chains*/))
        break;

      continue;
    }
    case OPC_EmitInteger:
    case OPC_EmitInteger8:
    case OPC_EmitInteger16:
    case OPC_EmitInteger32:
    case OPC_EmitInteger64:
    case OPC_EmitStringInteger:
    case OPC_EmitStringInteger32: {
      MVT::SimpleValueType VT;
      switch (Opcode) {
      case OPC_EmitInteger8:
        VT = MVT::i8;
        break;
      case OPC_EmitInteger16:
        VT = MVT::i16;
        break;
      case OPC_EmitInteger32:
      case OPC_EmitStringInteger32:
        VT = MVT::i32;
        break;
      case OPC_EmitInteger64:
        VT = MVT::i64;
        break;
      default:
        VT = static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
        break;
      }
      int64_t Val = MatcherTable[MatcherIndex++];
      if (Val & 128)
        Val = GetVBR(Val, MatcherTable, MatcherIndex);
      if (Opcode >= OPC_EmitInteger && Opcode <= OPC_EmitInteger64)
        Val = decodeSignRotatedValue(Val);
      RecordedNodes.push_back(std::pair<SDValue, SDNode *>(
          CurDAG->getTargetConstant(Val, SDLoc(NodeToMatch), VT), nullptr));
      continue;
    }
    case OPC_EmitRegister:
    case OPC_EmitRegisterI32:
    case OPC_EmitRegisterI64: {
      MVT::SimpleValueType VT;
      switch (Opcode) {
      case OPC_EmitRegisterI32:
        VT = MVT::i32;
        break;
      case OPC_EmitRegisterI64:
        VT = MVT::i64;
        break;
      default:
        VT = static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
        break;
      }
      unsigned RegNo = MatcherTable[MatcherIndex++];
      RecordedNodes.push_back(std::pair<SDValue, SDNode *>(
          CurDAG->getRegister(RegNo, VT), nullptr));
      continue;
    }
    case OPC_EmitRegister2: {
      // For targets w/ more than 256 register names, the register enum
      // values are stored in two bytes in the matcher table (just like
      // opcodes).
      MVT::SimpleValueType VT =
          static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
      unsigned RegNo = MatcherTable[MatcherIndex++];
      RegNo |= MatcherTable[MatcherIndex++] << 8;
      RecordedNodes.push_back(std::pair<SDValue, SDNode*>(
                              CurDAG->getRegister(RegNo, VT), nullptr));
      continue;
    }

    case OPC_EmitConvertToTarget:
    case OPC_EmitConvertToTarget0:
    case OPC_EmitConvertToTarget1:
    case OPC_EmitConvertToTarget2:
    case OPC_EmitConvertToTarget3:
    case OPC_EmitConvertToTarget4:
    case OPC_EmitConvertToTarget5:
    case OPC_EmitConvertToTarget6:
    case OPC_EmitConvertToTarget7: {
      // Convert from IMM/FPIMM to target version.
      unsigned RecNo = Opcode == OPC_EmitConvertToTarget
                           ? MatcherTable[MatcherIndex++]
                           : Opcode - OPC_EmitConvertToTarget0;
      assert(RecNo < RecordedNodes.size() && "Invalid EmitConvertToTarget");
      SDValue Imm = RecordedNodes[RecNo].first;

      if (Imm->getOpcode() == ISD::Constant) {
        const ConstantInt *Val=cast<ConstantSDNode>(Imm)->getConstantIntValue();
        Imm = CurDAG->getTargetConstant(*Val, SDLoc(NodeToMatch),
                                        Imm.getValueType());
      } else if (Imm->getOpcode() == ISD::ConstantFP) {
        const ConstantFP *Val=cast<ConstantFPSDNode>(Imm)->getConstantFPValue();
        Imm = CurDAG->getTargetConstantFP(*Val, SDLoc(NodeToMatch),
                                          Imm.getValueType());
      }

      RecordedNodes.push_back(std::make_pair(Imm, RecordedNodes[RecNo].second));
      continue;
    }

    case OPC_EmitMergeInputChains1_0:    // OPC_EmitMergeInputChains, 1, 0
    case OPC_EmitMergeInputChains1_1:    // OPC_EmitMergeInputChains, 1, 1
    case OPC_EmitMergeInputChains1_2: {  // OPC_EmitMergeInputChains, 1, 2
      // These are space-optimized forms of OPC_EmitMergeInputChains.
      assert(!InputChain.getNode() &&
             "EmitMergeInputChains should be the first chain producing node");
      assert(ChainNodesMatched.empty() &&
             "Should only have one EmitMergeInputChains per match");

      // Read all of the chained nodes.
      unsigned RecNo = Opcode - OPC_EmitMergeInputChains1_0;
      assert(RecNo < RecordedNodes.size() && "Invalid EmitMergeInputChains");
      ChainNodesMatched.push_back(RecordedNodes[RecNo].first.getNode());

      // If the chained node is not the root, we can't fold it if it has
      // multiple uses.
      // FIXME: What if other value results of the node have uses not matched
      // by this pattern?
      if (ChainNodesMatched.back() != NodeToMatch &&
          !RecordedNodes[RecNo].first.hasOneUse()) {
        ChainNodesMatched.clear();
        break;
      }

      // Merge the input chains if they are not intra-pattern references.
      InputChain = HandleMergeInputChains(ChainNodesMatched, CurDAG);

      if (!InputChain.getNode())
        break;  // Failed to merge.
      continue;
    }

    case OPC_EmitMergeInputChains: {
      assert(!InputChain.getNode() &&
             "EmitMergeInputChains should be the first chain producing node");
      // This node gets a list of nodes we matched in the input that have
      // chains.  We want to token factor all of the input chains to these nodes
      // together.  However, if any of the input chains is actually one of the
      // nodes matched in this pattern, then we have an intra-match reference.
      // Ignore these because the newly token factored chain should not refer to
      // the old nodes.
      unsigned NumChains = MatcherTable[MatcherIndex++];
      assert(NumChains != 0 && "Can't TF zero chains");

      assert(ChainNodesMatched.empty() &&
             "Should only have one EmitMergeInputChains per match");

      // Read all of the chained nodes.
      for (unsigned i = 0; i != NumChains; ++i) {
        unsigned RecNo = MatcherTable[MatcherIndex++];
        assert(RecNo < RecordedNodes.size() && "Invalid EmitMergeInputChains");
        ChainNodesMatched.push_back(RecordedNodes[RecNo].first.getNode());

        // If the chained node is not the root, we can't fold it if it has
        // multiple uses.
        // FIXME: What if other value results of the node have uses not matched
        // by this pattern?
        if (ChainNodesMatched.back() != NodeToMatch &&
            !RecordedNodes[RecNo].first.hasOneUse()) {
          ChainNodesMatched.clear();
          break;
        }
      }

      // If the inner loop broke out, the match fails.
      if (ChainNodesMatched.empty())
        break;

      // Merge the input chains if they are not intra-pattern references.
      InputChain = HandleMergeInputChains(ChainNodesMatched, CurDAG);

      if (!InputChain.getNode())
        break;  // Failed to merge.

      continue;
    }

    case OPC_EmitCopyToReg:
    case OPC_EmitCopyToReg0:
    case OPC_EmitCopyToReg1:
    case OPC_EmitCopyToReg2:
    case OPC_EmitCopyToReg3:
    case OPC_EmitCopyToReg4:
    case OPC_EmitCopyToReg5:
    case OPC_EmitCopyToReg6:
    case OPC_EmitCopyToReg7:
    case OPC_EmitCopyToRegTwoByte: {
      unsigned RecNo =
          Opcode >= OPC_EmitCopyToReg0 && Opcode <= OPC_EmitCopyToReg7
              ? Opcode - OPC_EmitCopyToReg0
              : MatcherTable[MatcherIndex++];
      assert(RecNo < RecordedNodes.size() && "Invalid EmitCopyToReg");
      unsigned DestPhysReg = MatcherTable[MatcherIndex++];
      if (Opcode == OPC_EmitCopyToRegTwoByte)
        DestPhysReg |= MatcherTable[MatcherIndex++] << 8;

      if (!InputChain.getNode())
        InputChain = CurDAG->getEntryNode();

      InputChain = CurDAG->getCopyToReg(InputChain, SDLoc(NodeToMatch),
                                        DestPhysReg, RecordedNodes[RecNo].first,
                                        InputGlue);

      InputGlue = InputChain.getValue(1);
      continue;
    }

    case OPC_EmitNodeXForm: {
      unsigned XFormNo = MatcherTable[MatcherIndex++];
      unsigned RecNo = MatcherTable[MatcherIndex++];
      assert(RecNo < RecordedNodes.size() && "Invalid EmitNodeXForm");
      SDValue Res = RunSDNodeXForm(RecordedNodes[RecNo].first, XFormNo);
      RecordedNodes.push_back(std::pair<SDValue,SDNode*>(Res, nullptr));
      continue;
    }
    case OPC_Coverage: {
      // This is emitted right before MorphNode/EmitNode.
      // So it should be safe to assume that this node has been selected
      unsigned index = MatcherTable[MatcherIndex++];
      index |= (MatcherTable[MatcherIndex++] << 8);
      dbgs() << "COVERED: " << getPatternForIndex(index) << "\n";
      dbgs() << "INCLUDED: " << getIncludePathForIndex(index) << "\n";
      continue;
    }

    case OPC_EmitNode:
    case OPC_EmitNode0:
    case OPC_EmitNode1:
    case OPC_EmitNode2:
    case OPC_EmitNode0None:
    case OPC_EmitNode1None:
    case OPC_EmitNode2None:
    case OPC_EmitNode0Chain:
    case OPC_EmitNode1Chain:
    case OPC_EmitNode2Chain:
    case OPC_MorphNodeTo:
    case OPC_MorphNodeTo0:
    case OPC_MorphNodeTo1:
    case OPC_MorphNodeTo2:
    case OPC_MorphNodeTo0None:
    case OPC_MorphNodeTo1None:
    case OPC_MorphNodeTo2None:
    case OPC_MorphNodeTo0Chain:
    case OPC_MorphNodeTo1Chain:
    case OPC_MorphNodeTo2Chain:
    case OPC_MorphNodeTo0GlueInput:
    case OPC_MorphNodeTo1GlueInput:
    case OPC_MorphNodeTo2GlueInput:
    case OPC_MorphNodeTo0GlueOutput:
    case OPC_MorphNodeTo1GlueOutput:
    case OPC_MorphNodeTo2GlueOutput: {
      uint16_t TargetOpc = MatcherTable[MatcherIndex++];
      TargetOpc |= static_cast<uint16_t>(MatcherTable[MatcherIndex++]) << 8;
      unsigned EmitNodeInfo;
      if (Opcode >= OPC_EmitNode0None && Opcode <= OPC_EmitNode2Chain) {
        if (Opcode >= OPC_EmitNode0Chain && Opcode <= OPC_EmitNode2Chain)
          EmitNodeInfo = OPFL_Chain;
        else
          EmitNodeInfo = OPFL_None;
      } else if (Opcode >= OPC_MorphNodeTo0None &&
                 Opcode <= OPC_MorphNodeTo2GlueOutput) {
        if (Opcode >= OPC_MorphNodeTo0Chain && Opcode <= OPC_MorphNodeTo2Chain)
          EmitNodeInfo = OPFL_Chain;
        else if (Opcode >= OPC_MorphNodeTo0GlueInput &&
                 Opcode <= OPC_MorphNodeTo2GlueInput)
          EmitNodeInfo = OPFL_GlueInput;
        else if (Opcode >= OPC_MorphNodeTo0GlueOutput &&
                 Opcode <= OPC_MorphNodeTo2GlueOutput)
          EmitNodeInfo = OPFL_GlueOutput;
        else
          EmitNodeInfo = OPFL_None;
      } else
        EmitNodeInfo = MatcherTable[MatcherIndex++];
      // Get the result VT list.
      unsigned NumVTs;
      // If this is one of the compressed forms, get the number of VTs based
      // on the Opcode. Otherwise read the next byte from the table.
      if (Opcode >= OPC_MorphNodeTo0 && Opcode <= OPC_MorphNodeTo2)
        NumVTs = Opcode - OPC_MorphNodeTo0;
      else if (Opcode >= OPC_MorphNodeTo0None && Opcode <= OPC_MorphNodeTo2None)
        NumVTs = Opcode - OPC_MorphNodeTo0None;
      else if (Opcode >= OPC_MorphNodeTo0Chain &&
               Opcode <= OPC_MorphNodeTo2Chain)
        NumVTs = Opcode - OPC_MorphNodeTo0Chain;
      else if (Opcode >= OPC_MorphNodeTo0GlueInput &&
               Opcode <= OPC_MorphNodeTo2GlueInput)
        NumVTs = Opcode - OPC_MorphNodeTo0GlueInput;
      else if (Opcode >= OPC_MorphNodeTo0GlueOutput &&
               Opcode <= OPC_MorphNodeTo2GlueOutput)
        NumVTs = Opcode - OPC_MorphNodeTo0GlueOutput;
      else if (Opcode >= OPC_EmitNode0 && Opcode <= OPC_EmitNode2)
        NumVTs = Opcode - OPC_EmitNode0;
      else if (Opcode >= OPC_EmitNode0None && Opcode <= OPC_EmitNode2None)
        NumVTs = Opcode - OPC_EmitNode0None;
      else if (Opcode >= OPC_EmitNode0Chain && Opcode <= OPC_EmitNode2Chain)
        NumVTs = Opcode - OPC_EmitNode0Chain;
      else
        NumVTs = MatcherTable[MatcherIndex++];
      SmallVector<EVT, 4> VTs;
      for (unsigned i = 0; i != NumVTs; ++i) {
        MVT::SimpleValueType VT =
            static_cast<MVT::SimpleValueType>(MatcherTable[MatcherIndex++]);
        if (VT == MVT::iPTR)
          VT = TLI->getPointerTy(CurDAG->getDataLayout()).SimpleTy;
        VTs.push_back(VT);
      }

      if (EmitNodeInfo & OPFL_Chain)
        VTs.push_back(MVT::Other);
      if (EmitNodeInfo & OPFL_GlueOutput)
        VTs.push_back(MVT::Glue);

      // This is hot code, so optimize the two most common cases of 1 and 2
      // results.
      SDVTList VTList;
      if (VTs.size() == 1)
        VTList = CurDAG->getVTList(VTs[0]);
      else if (VTs.size() == 2)
        VTList = CurDAG->getVTList(VTs[0], VTs[1]);
      else
        VTList = CurDAG->getVTList(VTs);

      // Get the operand list.
      unsigned NumOps = MatcherTable[MatcherIndex++];
      SmallVector<SDValue, 8> Ops;
      for (unsigned i = 0; i != NumOps; ++i) {
        unsigned RecNo = MatcherTable[MatcherIndex++];
        if (RecNo & 128)
          RecNo = GetVBR(RecNo, MatcherTable, MatcherIndex);

        assert(RecNo < RecordedNodes.size() && "Invalid EmitNode");
        Ops.push_back(RecordedNodes[RecNo].first);
      }

      // If there are variadic operands to add, handle them now.
      if (EmitNodeInfo & OPFL_VariadicInfo) {
        // Determine the start index to copy from.
        unsigned FirstOpToCopy = getNumFixedFromVariadicInfo(EmitNodeInfo);
        FirstOpToCopy += (EmitNodeInfo & OPFL_Chain) ? 1 : 0;
        assert(NodeToMatch->getNumOperands() >= FirstOpToCopy &&
               "Invalid variadic node");
        // Copy all of the variadic operands, not including a potential glue
        // input.
        for (unsigned i = FirstOpToCopy, e = NodeToMatch->getNumOperands();
             i != e; ++i) {
          SDValue V = NodeToMatch->getOperand(i);
          if (V.getValueType() == MVT::Glue) break;
          Ops.push_back(V);
        }
      }

      // If this has chain/glue inputs, add them.
      if (EmitNodeInfo & OPFL_Chain)
        Ops.push_back(InputChain);
      if ((EmitNodeInfo & OPFL_GlueInput) && InputGlue.getNode() != nullptr)
        Ops.push_back(InputGlue);

      // Check whether any matched node could raise an FP exception.  Since all
      // such nodes must have a chain, it suffices to check ChainNodesMatched.
      // We need to perform this check before potentially modifying one of the
      // nodes via MorphNode.
      bool MayRaiseFPException =
          llvm::any_of(ChainNodesMatched, [this](SDNode *N) {
            return mayRaiseFPException(N) && !N->getFlags().hasNoFPExcept();
          });

      // Create the node.
      MachineSDNode *Res = nullptr;
      bool IsMorphNodeTo =
          Opcode == OPC_MorphNodeTo ||
          (Opcode >= OPC_MorphNodeTo0 && Opcode <= OPC_MorphNodeTo2GlueOutput);
      if (!IsMorphNodeTo) {
        // If this is a normal EmitNode command, just create the new node and
        // add the results to the RecordedNodes list.
        Res = CurDAG->getMachineNode(TargetOpc, SDLoc(NodeToMatch),
                                     VTList, Ops);

        // Add all the non-glue/non-chain results to the RecordedNodes list.
        for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
          if (VTs[i] == MVT::Other || VTs[i] == MVT::Glue) break;
          RecordedNodes.push_back(std::pair<SDValue,SDNode*>(SDValue(Res, i),
                                                             nullptr));
        }
      } else {
        assert(NodeToMatch->getOpcode() != ISD::DELETED_NODE &&
               "NodeToMatch was removed partway through selection");
        SelectionDAG::DAGNodeDeletedListener NDL(*CurDAG, [&](SDNode *N,
                                                              SDNode *E) {
          CurDAG->salvageDebugInfo(*N);
          auto &Chain = ChainNodesMatched;
          assert((!E || !is_contained(Chain, N)) &&
                 "Chain node replaced during MorphNode");
          llvm::erase(Chain, N);
        });
        Res = cast<MachineSDNode>(MorphNode(NodeToMatch, TargetOpc, VTList,
                                            Ops, EmitNodeInfo));
      }

      // Set the NoFPExcept flag when no original matched node could
      // raise an FP exception, but the new node potentially might.
      if (!MayRaiseFPException && mayRaiseFPException(Res)) {
        SDNodeFlags Flags = Res->getFlags();
        Flags.setNoFPExcept(true);
        Res->setFlags(Flags);
      }

      // If the node had chain/glue results, update our notion of the current
      // chain and glue.
      if (EmitNodeInfo & OPFL_GlueOutput) {
        InputGlue = SDValue(Res, VTs.size()-1);
        if (EmitNodeInfo & OPFL_Chain)
          InputChain = SDValue(Res, VTs.size()-2);
      } else if (EmitNodeInfo & OPFL_Chain)
        InputChain = SDValue(Res, VTs.size()-1);

      // If the OPFL_MemRefs glue is set on this node, slap all of the
      // accumulated memrefs onto it.
      //
      // FIXME: This is vastly incorrect for patterns with multiple outputs
      // instructions that access memory and for ComplexPatterns that match
      // loads.
      if (EmitNodeInfo & OPFL_MemRefs) {
        // Only attach load or store memory operands if the generated
        // instruction may load or store.
        const MCInstrDesc &MCID = TII->get(TargetOpc);
        bool mayLoad = MCID.mayLoad();
        bool mayStore = MCID.mayStore();

        // We expect to have relatively few of these so just filter them into a
        // temporary buffer so that we can easily add them to the instruction.
        SmallVector<MachineMemOperand *, 4> FilteredMemRefs;
        for (MachineMemOperand *MMO : MatchedMemRefs) {
          if (MMO->isLoad()) {
            if (mayLoad)
              FilteredMemRefs.push_back(MMO);
          } else if (MMO->isStore()) {
            if (mayStore)
              FilteredMemRefs.push_back(MMO);
          } else {
            FilteredMemRefs.push_back(MMO);
          }
        }

        CurDAG->setNodeMemRefs(Res, FilteredMemRefs);
      }

      LLVM_DEBUG(if (!MatchedMemRefs.empty() && Res->memoperands_empty()) dbgs()
                     << "  Dropping mem operands\n";
                 dbgs() << "  " << (IsMorphNodeTo ? "Morphed" : "Created")
                        << " node: ";
                 Res->dump(CurDAG););

      // If this was a MorphNodeTo then we're completely done!
      if (IsMorphNodeTo) {
        // Update chain uses.
        UpdateChains(Res, InputChain, ChainNodesMatched, true);
        return;
      }
      continue;
    }

    case OPC_CompleteMatch: {
      // The match has been completed, and any new nodes (if any) have been
      // created.  Patch up references to the matched dag to use the newly
      // created nodes.
      unsigned NumResults = MatcherTable[MatcherIndex++];

      for (unsigned i = 0; i != NumResults; ++i) {
        unsigned ResSlot = MatcherTable[MatcherIndex++];
        if (ResSlot & 128)
          ResSlot = GetVBR(ResSlot, MatcherTable, MatcherIndex);

        assert(ResSlot < RecordedNodes.size() && "Invalid CompleteMatch");
        SDValue Res = RecordedNodes[ResSlot].first;

        assert(i < NodeToMatch->getNumValues() &&
               NodeToMatch->getValueType(i) != MVT::Other &&
               NodeToMatch->getValueType(i) != MVT::Glue &&
               "Invalid number of results to complete!");
        assert((NodeToMatch->getValueType(i) == Res.getValueType() ||
                NodeToMatch->getValueType(i) == MVT::iPTR ||
                Res.getValueType() == MVT::iPTR ||
                NodeToMatch->getValueType(i).getSizeInBits() ==
                    Res.getValueSizeInBits()) &&
               "invalid replacement");
        ReplaceUses(SDValue(NodeToMatch, i), Res);
      }

      // Update chain uses.
      UpdateChains(NodeToMatch, InputChain, ChainNodesMatched, false);

      // If the root node defines glue, we need to update it to the glue result.
      // TODO: This never happens in our tests and I think it can be removed /
      // replaced with an assert, but if we do it this the way the change is
      // NFC.
      if (NodeToMatch->getValueType(NodeToMatch->getNumValues() - 1) ==
              MVT::Glue &&
          InputGlue.getNode())
        ReplaceUses(SDValue(NodeToMatch, NodeToMatch->getNumValues() - 1),
                    InputGlue);

      assert(NodeToMatch->use_empty() &&
             "Didn't replace all uses of the node?");
      CurDAG->RemoveDeadNode(NodeToMatch);

      return;
    }
    }

    // If the code reached this point, then the match failed.  See if there is
    // another child to try in the current 'Scope', otherwise pop it until we
    // find a case to check.
    LLVM_DEBUG(dbgs() << "  Match failed at index " << CurrentOpcodeIndex
                      << "\n");
    ++NumDAGIselRetries;
    while (true) {
      if (MatchScopes.empty()) {
        CannotYetSelect(NodeToMatch);
        return;
      }

      // Restore the interpreter state back to the point where the scope was
      // formed.
      MatchScope &LastScope = MatchScopes.back();
      RecordedNodes.resize(LastScope.NumRecordedNodes);
      NodeStack.clear();
      NodeStack.append(LastScope.NodeStack.begin(), LastScope.NodeStack.end());
      N = NodeStack.back();

      if (LastScope.NumMatchedMemRefs != MatchedMemRefs.size())
        MatchedMemRefs.resize(LastScope.NumMatchedMemRefs);
      MatcherIndex = LastScope.FailIndex;

      LLVM_DEBUG(dbgs() << "  Continuing at " << MatcherIndex << "\n");

      InputChain = LastScope.InputChain;
      InputGlue = LastScope.InputGlue;
      if (!LastScope.HasChainNodesMatched)
        ChainNodesMatched.clear();

      // Check to see what the offset is at the new MatcherIndex.  If it is zero
      // we have reached the end of this scope, otherwise we have another child
      // in the current scope to try.
      unsigned NumToSkip = MatcherTable[MatcherIndex++];
      if (NumToSkip & 128)
        NumToSkip = GetVBR(NumToSkip, MatcherTable, MatcherIndex);

      // If we have another child in this scope to match, update FailIndex and
      // try it.
      if (NumToSkip != 0) {
        LastScope.FailIndex = MatcherIndex+NumToSkip;
        break;
      }

      // End of this scope, pop it and try the next child in the containing
      // scope.
      MatchScopes.pop_back();
    }
  }
}

/// Return whether the node may raise an FP exception.
bool SelectionDAGISel::mayRaiseFPException(SDNode *N) const {
  // For machine opcodes, consult the MCID flag.
  if (N->isMachineOpcode()) {
    const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());
    return MCID.mayRaiseFPException();
  }

  // For ISD opcodes, only StrictFP opcodes may raise an FP
  // exception.
  if (N->isTargetOpcode())
    return N->isTargetStrictFPOpcode();
  return N->isStrictFPOpcode();
}

bool SelectionDAGISel::isOrEquivalentToAdd(const SDNode *N) const {
  assert(N->getOpcode() == ISD::OR && "Unexpected opcode");
  auto *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!C)
    return false;

  // Detect when "or" is used to add an offset to a stack object.
  if (auto *FN = dyn_cast<FrameIndexSDNode>(N->getOperand(0))) {
    MachineFrameInfo &MFI = MF->getFrameInfo();
    Align A = MFI.getObjectAlign(FN->getIndex());
    int32_t Off = C->getSExtValue();
    // If the alleged offset fits in the zero bits guaranteed by
    // the alignment, then this or is really an add.
    return (Off >= 0) && (((A.value() - 1) & Off) == unsigned(Off));
  }
  return false;
}

void SelectionDAGISel::CannotYetSelect(SDNode *N) {
  std::string msg;
  raw_string_ostream Msg(msg);
  Msg << "Cannot select: ";

  if (N->getOpcode() != ISD::INTRINSIC_W_CHAIN &&
      N->getOpcode() != ISD::INTRINSIC_WO_CHAIN &&
      N->getOpcode() != ISD::INTRINSIC_VOID) {
    N->printrFull(Msg, CurDAG);
    Msg << "\nIn function: " << MF->getName();
  } else {
    bool HasInputChain = N->getOperand(0).getValueType() == MVT::Other;
    unsigned iid = N->getConstantOperandVal(HasInputChain);
    if (iid < Intrinsic::num_intrinsics)
      Msg << "intrinsic %" << Intrinsic::getBaseName((Intrinsic::ID)iid);
    else if (const TargetIntrinsicInfo *TII = TM.getIntrinsicInfo())
      Msg << "target intrinsic %" << TII->getName(iid);
    else
      Msg << "unknown intrinsic #" << iid;
  }
  report_fatal_error(Twine(msg));
}
