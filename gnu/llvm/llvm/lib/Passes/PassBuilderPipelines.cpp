//===- Construction of pass pipelines -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the implementation of the PassBuilder based on our
/// static pass registry as well as related functionality. It also provides
/// helpers to aid in analyzing, debugging, and testing passes and pass
/// pipelines.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/PGOOptions.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/Coroutines/CoroCleanup.h"
#include "llvm/Transforms/Coroutines/CoroConditionalWrapper.h"
#include "llvm/Transforms/Coroutines/CoroEarly.h"
#include "llvm/Transforms/Coroutines/CoroElide.h"
#include "llvm/Transforms/Coroutines/CoroSplit.h"
#include "llvm/Transforms/HipStdPar/HipStdPar.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/Annotation2Metadata.h"
#include "llvm/Transforms/IPO/ArgumentPromotion.h"
#include "llvm/Transforms/IPO/Attributor.h"
#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/CrossDSOCFI.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/ElimAvailExtern.h"
#include "llvm/Transforms/IPO/EmbedBitcodePass.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/GlobalSplit.h"
#include "llvm/Transforms/IPO/HotColdSplitting.h"
#include "llvm/Transforms/IPO/IROutliner.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/Transforms/IPO/MemProfContextDisambiguation.h"
#include "llvm/Transforms/IPO/MergeFunctions.h"
#include "llvm/Transforms/IPO/ModuleInliner.h"
#include "llvm/Transforms/IPO/OpenMPOpt.h"
#include "llvm/Transforms/IPO/PartialInlining.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/IPO/SampleProfile.h"
#include "llvm/Transforms/IPO/SampleProfileProbe.h"
#include "llvm/Transforms/IPO/SyntheticCountsPropagation.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation/CGProfile.h"
#include "llvm/Transforms/Instrumentation/ControlHeightReduction.h"
#include "llvm/Transforms/Instrumentation/InstrOrderFile.h"
#include "llvm/Transforms/Instrumentation/InstrProfiling.h"
#include "llvm/Transforms/Instrumentation/MemProfiler.h"
#include "llvm/Transforms/Instrumentation/PGOCtxProfLowering.h"
#include "llvm/Transforms/Instrumentation/PGOForceFunctionAttrs.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/AlignmentFromAssumptions.h"
#include "llvm/Transforms/Scalar/AnnotationRemarks.h"
#include "llvm/Transforms/Scalar/BDCE.h"
#include "llvm/Transforms/Scalar/CallSiteSplitting.h"
#include "llvm/Transforms/Scalar/ConstraintElimination.h"
#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/Transforms/Scalar/DFAJumpThreading.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/InferAlignment.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/Transforms/Scalar/JumpTableToSwitch.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopDistribute.h"
#include "llvm/Transforms/Scalar/LoopFlatten.h"
#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/Transforms/Scalar/LoopInstSimplify.h"
#include "llvm/Transforms/Scalar/LoopInterchange.h"
#include "llvm/Transforms/Scalar/LoopLoadElimination.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"
#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/Transforms/Scalar/LoopUnrollAndJamPass.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/LoopVersioningLICM.h"
#include "llvm/Transforms/Scalar/LowerConstantIntrinsics.h"
#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/Transforms/Scalar/LowerMatrixIntrinsics.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"
#include "llvm/Transforms/Scalar/NewGVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/SpeculativeExecution.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Scalar/WarnMissedTransforms.h"
#include "llvm/Transforms/Utils/AddDiscriminators.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/CanonicalizeAliases.h"
#include "llvm/Transforms/Utils/CountVisits.h"
#include "llvm/Transforms/Utils/EntryExitInstrumenter.h"
#include "llvm/Transforms/Utils/InjectTLIMappings.h"
#include "llvm/Transforms/Utils/LibCallsShrinkWrap.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/MoveAutoInit.h"
#include "llvm/Transforms/Utils/NameAnonGlobals.h"
#include "llvm/Transforms/Utils/RelLookupTableConverter.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"
#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "llvm/Transforms/Vectorize/SLPVectorizer.h"
#include "llvm/Transforms/Vectorize/VectorCombine.h"

using namespace llvm;

static cl::opt<InliningAdvisorMode> UseInlineAdvisor(
    "enable-ml-inliner", cl::init(InliningAdvisorMode::Default), cl::Hidden,
    cl::desc("Enable ML policy for inliner. Currently trained for -Oz only"),
    cl::values(clEnumValN(InliningAdvisorMode::Default, "default",
                          "Heuristics-based inliner version"),
               clEnumValN(InliningAdvisorMode::Development, "development",
                          "Use development mode (runtime-loadable model)"),
               clEnumValN(InliningAdvisorMode::Release, "release",
                          "Use release mode (AOT-compiled model)")));

static cl::opt<bool> EnableSyntheticCounts(
    "enable-npm-synthetic-counts", cl::Hidden,
    cl::desc("Run synthetic function entry count generation "
             "pass"));

/// Flag to enable inline deferral during PGO.
static cl::opt<bool>
    EnablePGOInlineDeferral("enable-npm-pgo-inline-deferral", cl::init(true),
                            cl::Hidden,
                            cl::desc("Enable inline deferral during PGO"));

static cl::opt<bool> EnableModuleInliner("enable-module-inliner",
                                         cl::init(false), cl::Hidden,
                                         cl::desc("Enable module inliner"));

static cl::opt<bool> PerformMandatoryInliningsFirst(
    "mandatory-inlining-first", cl::init(false), cl::Hidden,
    cl::desc("Perform mandatory inlinings module-wide, before performing "
             "inlining"));

static cl::opt<bool> EnableEagerlyInvalidateAnalyses(
    "eagerly-invalidate-analyses", cl::init(true), cl::Hidden,
    cl::desc("Eagerly invalidate more analyses in default pipelines"));

static cl::opt<bool> EnableMergeFunctions(
    "enable-merge-functions", cl::init(false), cl::Hidden,
    cl::desc("Enable function merging as part of the optimization pipeline"));

static cl::opt<bool> EnablePostPGOLoopRotation(
    "enable-post-pgo-loop-rotation", cl::init(true), cl::Hidden,
    cl::desc("Run the loop rotation transformation after PGO instrumentation"));

static cl::opt<bool> EnableGlobalAnalyses(
    "enable-global-analyses", cl::init(true), cl::Hidden,
    cl::desc("Enable inter-procedural analyses"));

static cl::opt<bool>
    RunPartialInlining("enable-partial-inlining", cl::init(false), cl::Hidden,
                       cl::desc("Run Partial inlinining pass"));

static cl::opt<bool> ExtraVectorizerPasses(
    "extra-vectorizer-passes", cl::init(false), cl::Hidden,
    cl::desc("Run cleanup optimization passes after vectorization"));

static cl::opt<bool> RunNewGVN("enable-newgvn", cl::init(false), cl::Hidden,
                               cl::desc("Run the NewGVN pass"));

static cl::opt<bool> EnableLoopInterchange(
    "enable-loopinterchange", cl::init(false), cl::Hidden,
    cl::desc("Enable the experimental LoopInterchange Pass"));

static cl::opt<bool> EnableUnrollAndJam("enable-unroll-and-jam",
                                        cl::init(false), cl::Hidden,
                                        cl::desc("Enable Unroll And Jam Pass"));

static cl::opt<bool> EnableLoopFlatten("enable-loop-flatten", cl::init(false),
                                       cl::Hidden,
                                       cl::desc("Enable the LoopFlatten Pass"));

// Experimentally allow loop header duplication. This should allow for better
// optimization at Oz, since loop-idiom recognition can then recognize things
// like memcpy. If this ends up being useful for many targets, we should drop
// this flag and make a code generation option that can be controlled
// independent of the opt level and exposed through the frontend.
static cl::opt<bool> EnableLoopHeaderDuplication(
    "enable-loop-header-duplication", cl::init(false), cl::Hidden,
    cl::desc("Enable loop header duplication at any optimization level"));

static cl::opt<bool>
    EnableDFAJumpThreading("enable-dfa-jump-thread",
                           cl::desc("Enable DFA jump threading"),
                           cl::init(false), cl::Hidden);

// TODO: turn on and remove flag
static cl::opt<bool> EnablePGOForceFunctionAttrs(
    "enable-pgo-force-function-attrs",
    cl::desc("Enable pass to set function attributes based on PGO profiles"),
    cl::init(false));

static cl::opt<bool>
    EnableHotColdSplit("hot-cold-split",
                       cl::desc("Enable hot-cold splitting pass"));

static cl::opt<bool> EnableIROutliner("ir-outliner", cl::init(false),
                                      cl::Hidden,
                                      cl::desc("Enable ir outliner pass"));

static cl::opt<bool>
    DisablePreInliner("disable-preinline", cl::init(false), cl::Hidden,
                      cl::desc("Disable pre-instrumentation inliner"));

static cl::opt<int> PreInlineThreshold(
    "preinline-threshold", cl::Hidden, cl::init(75),
    cl::desc("Control the amount of inlining in pre-instrumentation inliner "
             "(default = 75)"));

static cl::opt<bool>
    EnableGVNHoist("enable-gvn-hoist",
                   cl::desc("Enable the GVN hoisting pass (default = off)"));

static cl::opt<bool>
    EnableGVNSink("enable-gvn-sink",
                  cl::desc("Enable the GVN sinking pass (default = off)"));

static cl::opt<bool> EnableJumpTableToSwitch(
    "enable-jump-table-to-switch",
    cl::desc("Enable JumpTableToSwitch pass (default = off)"));

// This option is used in simplifying testing SampleFDO optimizations for
// profile loading.
static cl::opt<bool>
    EnableCHR("enable-chr", cl::init(true), cl::Hidden,
              cl::desc("Enable control height reduction optimization (CHR)"));

static cl::opt<bool> FlattenedProfileUsed(
    "flattened-profile-used", cl::init(false), cl::Hidden,
    cl::desc("Indicate the sample profile being used is flattened, i.e., "
             "no inline hierachy exists in the profile"));

static cl::opt<bool> EnableOrderFileInstrumentation(
    "enable-order-file-instrumentation", cl::init(false), cl::Hidden,
    cl::desc("Enable order file instrumentation (default = off)"));

static cl::opt<bool>
    EnableMatrix("enable-matrix", cl::init(false), cl::Hidden,
                 cl::desc("Enable lowering of the matrix intrinsics"));

static cl::opt<bool> EnableConstraintElimination(
    "enable-constraint-elimination", cl::init(true), cl::Hidden,
    cl::desc(
        "Enable pass to eliminate conditions based on linear constraints"));

static cl::opt<AttributorRunOption> AttributorRun(
    "attributor-enable", cl::Hidden, cl::init(AttributorRunOption::NONE),
    cl::desc("Enable the attributor inter-procedural deduction pass"),
    cl::values(clEnumValN(AttributorRunOption::ALL, "all",
                          "enable all attributor runs"),
               clEnumValN(AttributorRunOption::MODULE, "module",
                          "enable module-wide attributor runs"),
               clEnumValN(AttributorRunOption::CGSCC, "cgscc",
                          "enable call graph SCC attributor runs"),
               clEnumValN(AttributorRunOption::NONE, "none",
                          "disable attributor runs")));

static cl::opt<bool> EnableSampledInstr(
    "enable-sampled-instrumentation", cl::init(false), cl::Hidden,
    cl::desc("Enable profile instrumentation sampling (default = off)"));
static cl::opt<bool> UseLoopVersioningLICM(
    "enable-loop-versioning-licm", cl::init(false), cl::Hidden,
    cl::desc("Enable the experimental Loop Versioning LICM pass"));

namespace llvm {
extern cl::opt<bool> EnableMemProfContextDisambiguation;

extern cl::opt<bool> EnableInferAlignmentPass;
} // namespace llvm

PipelineTuningOptions::PipelineTuningOptions() {
  LoopInterleaving = true;
  LoopVectorization = true;
  SLPVectorization = false;
  LoopUnrolling = true;
  ForgetAllSCEVInLoopUnroll = ForgetSCEVInLoopUnroll;
  LicmMssaOptCap = SetLicmMssaOptCap;
  LicmMssaNoAccForPromotionCap = SetLicmMssaNoAccForPromotionCap;
  CallGraphProfile = true;
  UnifiedLTO = false;
  MergeFunctions = EnableMergeFunctions;
  InlinerThreshold = -1;
  EagerlyInvalidateAnalyses = EnableEagerlyInvalidateAnalyses;
}

namespace llvm {
extern cl::opt<unsigned> MaxDevirtIterations;
} // namespace llvm

void PassBuilder::invokePeepholeEPCallbacks(FunctionPassManager &FPM,
                                            OptimizationLevel Level) {
  for (auto &C : PeepholeEPCallbacks)
    C(FPM, Level);
}
void PassBuilder::invokeLateLoopOptimizationsEPCallbacks(
    LoopPassManager &LPM, OptimizationLevel Level) {
  for (auto &C : LateLoopOptimizationsEPCallbacks)
    C(LPM, Level);
}
void PassBuilder::invokeLoopOptimizerEndEPCallbacks(LoopPassManager &LPM,
                                                    OptimizationLevel Level) {
  for (auto &C : LoopOptimizerEndEPCallbacks)
    C(LPM, Level);
}
void PassBuilder::invokeScalarOptimizerLateEPCallbacks(
    FunctionPassManager &FPM, OptimizationLevel Level) {
  for (auto &C : ScalarOptimizerLateEPCallbacks)
    C(FPM, Level);
}
void PassBuilder::invokeCGSCCOptimizerLateEPCallbacks(CGSCCPassManager &CGPM,
                                                      OptimizationLevel Level) {
  for (auto &C : CGSCCOptimizerLateEPCallbacks)
    C(CGPM, Level);
}
void PassBuilder::invokeVectorizerStartEPCallbacks(FunctionPassManager &FPM,
                                                   OptimizationLevel Level) {
  for (auto &C : VectorizerStartEPCallbacks)
    C(FPM, Level);
}
void PassBuilder::invokeOptimizerEarlyEPCallbacks(ModulePassManager &MPM,
                                                  OptimizationLevel Level) {
  for (auto &C : OptimizerEarlyEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokeOptimizerLastEPCallbacks(ModulePassManager &MPM,
                                                 OptimizationLevel Level) {
  for (auto &C : OptimizerLastEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokeFullLinkTimeOptimizationEarlyEPCallbacks(
    ModulePassManager &MPM, OptimizationLevel Level) {
  for (auto &C : FullLinkTimeOptimizationEarlyEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokeFullLinkTimeOptimizationLastEPCallbacks(
    ModulePassManager &MPM, OptimizationLevel Level) {
  for (auto &C : FullLinkTimeOptimizationLastEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokePipelineStartEPCallbacks(ModulePassManager &MPM,
                                                 OptimizationLevel Level) {
  for (auto &C : PipelineStartEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokePipelineEarlySimplificationEPCallbacks(
    ModulePassManager &MPM, OptimizationLevel Level) {
  for (auto &C : PipelineEarlySimplificationEPCallbacks)
    C(MPM, Level);
}

// Helper to add AnnotationRemarksPass.
static void addAnnotationRemarksPass(ModulePassManager &MPM) {
  MPM.addPass(createModuleToFunctionPassAdaptor(AnnotationRemarksPass()));
}

// Helper to check if the current compilation phase is preparing for LTO
static bool isLTOPreLink(ThinOrFullLTOPhase Phase) {
  return Phase == ThinOrFullLTOPhase::ThinLTOPreLink ||
         Phase == ThinOrFullLTOPhase::FullLTOPreLink;
}

// TODO: Investigate the cost/benefit of tail call elimination on debugging.
FunctionPassManager
PassBuilder::buildO1FunctionSimplificationPipeline(OptimizationLevel Level,
                                                   ThinOrFullLTOPhase Phase) {

  FunctionPassManager FPM;

  if (AreStatisticsEnabled())
    FPM.addPass(CountVisitsPass());

  // Form SSA out of local memory accesses after breaking apart aggregates into
  // scalars.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Catch trivial redundancies
  FPM.addPass(EarlyCSEPass(true /* Enable mem-ssa. */));

  // Hoisting of scalars and load expressions.
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());

  FPM.addPass(LibCallsShrinkWrapPass());

  invokePeepholeEPCallbacks(FPM, Level);

  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));

  // Form canonically associated expression trees, and simplify the trees using
  // basic mathematical properties. For example, this will form (nearly)
  // minimal multiplication trees.
  FPM.addPass(ReassociatePass());

  // Add the primary loop simplification pipeline.
  // FIXME: Currently this is split into two loop pass pipelines because we run
  // some function passes in between them. These can and should be removed
  // and/or replaced by scheduling the loop pass equivalents in the correct
  // positions. But those equivalent passes aren't powerful enough yet.
  // Specifically, `SimplifyCFGPass` and `InstCombinePass` are currently still
  // used. We have `LoopSimplifyCFGPass` which isn't yet powerful enough yet to
  // fully replace `SimplifyCFGPass`, and the closest to the other we have is
  // `LoopInstSimplify`.
  LoopPassManager LPM1, LPM2;

  // Simplify the loop body. We do this initially to clean up after other loop
  // passes run, either when iterating on a loop or on inner loops with
  // implications on the outer loop.
  LPM1.addPass(LoopInstSimplifyPass());
  LPM1.addPass(LoopSimplifyCFGPass());

  // Try to remove as much code from the loop header as possible,
  // to reduce amount of IR that will have to be duplicated. However,
  // do not perform speculative hoisting the first time as LICM
  // will destroy metadata that may not need to be destroyed if run
  // after loop rotation.
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/false));

  LPM1.addPass(LoopRotatePass(/* Disable header duplication */ true,
                              isLTOPreLink(Phase)));
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/true));
  LPM1.addPass(SimpleLoopUnswitchPass());
  if (EnableLoopFlatten)
    LPM1.addPass(LoopFlattenPass());

  LPM2.addPass(LoopIdiomRecognizePass());
  LPM2.addPass(IndVarSimplifyPass());

  invokeLateLoopOptimizationsEPCallbacks(LPM2, Level);

  LPM2.addPass(LoopDeletionPass());

  if (EnableLoopInterchange)
    LPM2.addPass(LoopInterchangePass());

  // Do not enable unrolling in PreLinkThinLTO phase during sample PGO
  // because it changes IR to makes profile annotation in back compile
  // inaccurate. The normal unroller doesn't pay attention to forced full unroll
  // attributes so we need to make sure and allow the full unroll pass to pay
  // attention to it.
  if (Phase != ThinOrFullLTOPhase::ThinLTOPreLink || !PGOOpt ||
      PGOOpt->Action != PGOOptions::SampleUse)
    LPM2.addPass(LoopFullUnrollPass(Level.getSpeedupLevel(),
                                    /* OnlyWhenForced= */ !PTO.LoopUnrolling,
                                    PTO.ForgetAllSCEVInLoopUnroll));

  invokeLoopOptimizerEndEPCallbacks(LPM2, Level);

  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM1),
                                              /*UseMemorySSA=*/true,
                                              /*UseBlockFrequencyInfo=*/true));
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());
  // The loop passes in LPM2 (LoopFullUnrollPass) do not preserve MemorySSA.
  // *All* loop passes must preserve it, in order to be able to use it.
  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM2),
                                              /*UseMemorySSA=*/false,
                                              /*UseBlockFrequencyInfo=*/false));

  // Delete small array after loop unroll.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Specially optimize memory movement as it doesn't look like dataflow in SSA.
  FPM.addPass(MemCpyOptPass());

  // Sparse conditional constant propagation.
  // FIXME: It isn't clear why we do this *after* loop passes rather than
  // before...
  FPM.addPass(SCCPPass());

  // Delete dead bit computations (instcombine runs after to fold away the dead
  // computations, and then ADCE will run later to exploit any new DCE
  // opportunities that creates).
  FPM.addPass(BDCEPass());

  // Run instcombine after redundancy and dead bit elimination to exploit
  // opportunities opened up by them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  FPM.addPass(CoroElidePass());

  invokeScalarOptimizerLateEPCallbacks(FPM, Level);

  // Finally, do an expensive DCE pass to catch all the dead code exposed by
  // the simplifications and basic cleanup after all the simplifications.
  // TODO: Investigate if this is too expensive.
  FPM.addPass(ADCEPass());
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  return FPM;
}

FunctionPassManager
PassBuilder::buildFunctionSimplificationPipeline(OptimizationLevel Level,
                                                 ThinOrFullLTOPhase Phase) {
  assert(Level != OptimizationLevel::O0 && "Must request optimizations!");

  // The O1 pipeline has a separate pipeline creation function to simplify
  // construction readability.
  if (Level.getSpeedupLevel() == 1)
    return buildO1FunctionSimplificationPipeline(Level, Phase);

  FunctionPassManager FPM;

  if (AreStatisticsEnabled())
    FPM.addPass(CountVisitsPass());

  // Form SSA out of local memory accesses after breaking apart aggregates into
  // scalars.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Catch trivial redundancies
  FPM.addPass(EarlyCSEPass(true /* Enable mem-ssa. */));
  if (EnableKnowledgeRetention)
    FPM.addPass(AssumeSimplifyPass());

  // Hoisting of scalars and load expressions.
  if (EnableGVNHoist)
    FPM.addPass(GVNHoistPass());

  // Global value numbering based sinking.
  if (EnableGVNSink) {
    FPM.addPass(GVNSinkPass());
    FPM.addPass(
        SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  }

  // Speculative execution if the target has divergent branches; otherwise nop.
  FPM.addPass(SpeculativeExecutionPass(/* OnlyIfDivergentTarget =*/true));

  // Optimize based on known information about branches, and cleanup afterward.
  FPM.addPass(JumpThreadingPass());
  FPM.addPass(CorrelatedValuePropagationPass());

  // Jump table to switch conversion.
  if (EnableJumpTableToSwitch)
    FPM.addPass(JumpTableToSwitchPass());

  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());
  FPM.addPass(AggressiveInstCombinePass());

  if (!Level.isOptimizingForSize())
    FPM.addPass(LibCallsShrinkWrapPass());

  invokePeepholeEPCallbacks(FPM, Level);

  // For PGO use pipeline, try to optimize memory intrinsics such as memcpy
  // using the size value profile. Don't perform this when optimizing for size.
  if (PGOOpt && PGOOpt->Action == PGOOptions::IRUse &&
      !Level.isOptimizingForSize())
    FPM.addPass(PGOMemOPSizeOpt());

  FPM.addPass(TailCallElimPass());
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));

  // Form canonically associated expression trees, and simplify the trees using
  // basic mathematical properties. For example, this will form (nearly)
  // minimal multiplication trees.
  FPM.addPass(ReassociatePass());

  if (EnableConstraintElimination)
    FPM.addPass(ConstraintEliminationPass());

  // Add the primary loop simplification pipeline.
  // FIXME: Currently this is split into two loop pass pipelines because we run
  // some function passes in between them. These can and should be removed
  // and/or replaced by scheduling the loop pass equivalents in the correct
  // positions. But those equivalent passes aren't powerful enough yet.
  // Specifically, `SimplifyCFGPass` and `InstCombinePass` are currently still
  // used. We have `LoopSimplifyCFGPass` which isn't yet powerful enough yet to
  // fully replace `SimplifyCFGPass`, and the closest to the other we have is
  // `LoopInstSimplify`.
  LoopPassManager LPM1, LPM2;

  // Simplify the loop body. We do this initially to clean up after other loop
  // passes run, either when iterating on a loop or on inner loops with
  // implications on the outer loop.
  LPM1.addPass(LoopInstSimplifyPass());
  LPM1.addPass(LoopSimplifyCFGPass());

  // Try to remove as much code from the loop header as possible,
  // to reduce amount of IR that will have to be duplicated. However,
  // do not perform speculative hoisting the first time as LICM
  // will destroy metadata that may not need to be destroyed if run
  // after loop rotation.
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/false));

  // Disable header duplication in loop rotation at -Oz.
  LPM1.addPass(LoopRotatePass(EnableLoopHeaderDuplication ||
                                  Level != OptimizationLevel::Oz,
                              isLTOPreLink(Phase)));
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/true));
  LPM1.addPass(
      SimpleLoopUnswitchPass(/* NonTrivial */ Level == OptimizationLevel::O3));
  if (EnableLoopFlatten)
    LPM1.addPass(LoopFlattenPass());

  LPM2.addPass(LoopIdiomRecognizePass());
  LPM2.addPass(IndVarSimplifyPass());

  {
    ExtraSimpleLoopUnswitchPassManager ExtraPasses;
    ExtraPasses.addPass(SimpleLoopUnswitchPass(/* NonTrivial */ Level ==
                                               OptimizationLevel::O3));
    LPM2.addPass(std::move(ExtraPasses));
  }

  invokeLateLoopOptimizationsEPCallbacks(LPM2, Level);

  LPM2.addPass(LoopDeletionPass());

  if (EnableLoopInterchange)
    LPM2.addPass(LoopInterchangePass());

  // Do not enable unrolling in PreLinkThinLTO phase during sample PGO
  // because it changes IR to makes profile annotation in back compile
  // inaccurate. The normal unroller doesn't pay attention to forced full unroll
  // attributes so we need to make sure and allow the full unroll pass to pay
  // attention to it.
  if (Phase != ThinOrFullLTOPhase::ThinLTOPreLink || !PGOOpt ||
      PGOOpt->Action != PGOOptions::SampleUse)
    LPM2.addPass(LoopFullUnrollPass(Level.getSpeedupLevel(),
                                    /* OnlyWhenForced= */ !PTO.LoopUnrolling,
                                    PTO.ForgetAllSCEVInLoopUnroll));

  invokeLoopOptimizerEndEPCallbacks(LPM2, Level);

  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM1),
                                              /*UseMemorySSA=*/true,
                                              /*UseBlockFrequencyInfo=*/true));
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());
  // The loop passes in LPM2 (LoopIdiomRecognizePass, IndVarSimplifyPass,
  // LoopDeletionPass and LoopFullUnrollPass) do not preserve MemorySSA.
  // *All* loop passes must preserve it, in order to be able to use it.
  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM2),
                                              /*UseMemorySSA=*/false,
                                              /*UseBlockFrequencyInfo=*/false));

  // Delete small array after loop unroll.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Try vectorization/scalarization transforms that are both improvements
  // themselves and can allow further folds with GVN and InstCombine.
  FPM.addPass(VectorCombinePass(/*TryEarlyFoldsOnly=*/true));

  // Eliminate redundancies.
  FPM.addPass(MergedLoadStoreMotionPass());
  if (RunNewGVN)
    FPM.addPass(NewGVNPass());
  else
    FPM.addPass(GVNPass());

  // Sparse conditional constant propagation.
  // FIXME: It isn't clear why we do this *after* loop passes rather than
  // before...
  FPM.addPass(SCCPPass());

  // Delete dead bit computations (instcombine runs after to fold away the dead
  // computations, and then ADCE will run later to exploit any new DCE
  // opportunities that creates).
  FPM.addPass(BDCEPass());

  // Run instcombine after redundancy and dead bit elimination to exploit
  // opportunities opened up by them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  // Re-consider control flow based optimizations after redundancy elimination,
  // redo DCE, etc.
  if (EnableDFAJumpThreading)
    FPM.addPass(DFAJumpThreadingPass());

  FPM.addPass(JumpThreadingPass());
  FPM.addPass(CorrelatedValuePropagationPass());

  // Finally, do an expensive DCE pass to catch all the dead code exposed by
  // the simplifications and basic cleanup after all the simplifications.
  // TODO: Investigate if this is too expensive.
  FPM.addPass(ADCEPass());

  // Specially optimize memory movement as it doesn't look like dataflow in SSA.
  FPM.addPass(MemCpyOptPass());

  FPM.addPass(DSEPass());
  FPM.addPass(MoveAutoInitPass());

  FPM.addPass(createFunctionToLoopPassAdaptor(
      LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
               /*AllowSpeculation=*/true),
      /*UseMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));

  FPM.addPass(CoroElidePass());

  invokeScalarOptimizerLateEPCallbacks(FPM, Level);

  FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions()
                                  .convertSwitchRangeToICmp(true)
                                  .hoistCommonInsts(true)
                                  .sinkCommonInsts(true)));
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  return FPM;
}

void PassBuilder::addRequiredLTOPreLinkPasses(ModulePassManager &MPM) {
  MPM.addPass(CanonicalizeAliasesPass());
  MPM.addPass(NameAnonGlobalPass());
}

void PassBuilder::addPreInlinerPasses(ModulePassManager &MPM,
                                      OptimizationLevel Level,
                                      ThinOrFullLTOPhase LTOPhase) {
  assert(Level != OptimizationLevel::O0 && "Not expecting O0 here!");
  if (DisablePreInliner)
    return;
  InlineParams IP;

  IP.DefaultThreshold = PreInlineThreshold;

  // FIXME: The hint threshold has the same value used by the regular inliner
  // when not optimzing for size. This should probably be lowered after
  // performance testing.
  // FIXME: this comment is cargo culted from the old pass manager, revisit).
  IP.HintThreshold = Level.isOptimizingForSize() ? PreInlineThreshold : 325;
  ModuleInlinerWrapperPass MIWP(
      IP, /* MandatoryFirst */ true,
      InlineContext{LTOPhase, InlinePass::EarlyInliner});
  CGSCCPassManager &CGPipeline = MIWP.getPM();

  FunctionPassManager FPM;
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));
  FPM.addPass(EarlyCSEPass()); // Catch trivial redundancies.
  FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(
      true)));                    // Merge & remove basic blocks.
  FPM.addPass(InstCombinePass()); // Combine silly sequences.
  invokePeepholeEPCallbacks(FPM, Level);

  CGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
      std::move(FPM), PTO.EagerlyInvalidateAnalyses));

  MPM.addPass(std::move(MIWP));

  // Delete anything that is now dead to make sure that we don't instrument
  // dead code. Instrumentation can end up keeping dead code around and
  // dramatically increase code size.
  MPM.addPass(GlobalDCEPass());
}

void PassBuilder::addPostPGOLoopRotation(ModulePassManager &MPM,
                                         OptimizationLevel Level) {
  if (EnablePostPGOLoopRotation) {
    // Disable header duplication in loop rotation at -Oz.
    MPM.addPass(createModuleToFunctionPassAdaptor(
        createFunctionToLoopPassAdaptor(
            LoopRotatePass(EnableLoopHeaderDuplication ||
                           Level != OptimizationLevel::Oz),
            /*UseMemorySSA=*/false,
            /*UseBlockFrequencyInfo=*/false),
        PTO.EagerlyInvalidateAnalyses));
  }
}

void PassBuilder::addPGOInstrPasses(ModulePassManager &MPM,
                                    OptimizationLevel Level, bool RunProfileGen,
                                    bool IsCS, bool AtomicCounterUpdate,
                                    std::string ProfileFile,
                                    std::string ProfileRemappingFile,
                                    IntrusiveRefCntPtr<vfs::FileSystem> FS) {
  assert(Level != OptimizationLevel::O0 && "Not expecting O0 here!");

  if (!RunProfileGen) {
    assert(!ProfileFile.empty() && "Profile use expecting a profile file!");
    MPM.addPass(
        PGOInstrumentationUse(ProfileFile, ProfileRemappingFile, IsCS, FS));
    // Cache ProfileSummaryAnalysis once to avoid the potential need to insert
    // RequireAnalysisPass for PSI before subsequent non-module passes.
    MPM.addPass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());
    return;
  }

  // Perform PGO instrumentation.
  MPM.addPass(PGOInstrumentationGen(IsCS));

  addPostPGOLoopRotation(MPM, Level);
  // Add the profile lowering pass.
  InstrProfOptions Options;
  if (!ProfileFile.empty())
    Options.InstrProfileOutput = ProfileFile;
  // Do counter promotion at Level greater than O0.
  Options.DoCounterPromotion = true;
  Options.UseBFIInPromotion = IsCS;
  if (EnableSampledInstr) {
    Options.Sampling = true;
    // With sampling, there is little beneifit to enable counter promotion.
    // But note that sampling does work with counter promotion.
    Options.DoCounterPromotion = false;
  }
  Options.Atomic = AtomicCounterUpdate;
  MPM.addPass(InstrProfilingLoweringPass(Options, IsCS));
}

void PassBuilder::addPGOInstrPassesForO0(
    ModulePassManager &MPM, bool RunProfileGen, bool IsCS,
    bool AtomicCounterUpdate, std::string ProfileFile,
    std::string ProfileRemappingFile, IntrusiveRefCntPtr<vfs::FileSystem> FS) {
  if (!RunProfileGen) {
    assert(!ProfileFile.empty() && "Profile use expecting a profile file!");
    MPM.addPass(
        PGOInstrumentationUse(ProfileFile, ProfileRemappingFile, IsCS, FS));
    // Cache ProfileSummaryAnalysis once to avoid the potential need to insert
    // RequireAnalysisPass for PSI before subsequent non-module passes.
    MPM.addPass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());
    return;
  }

  // Perform PGO instrumentation.
  MPM.addPass(PGOInstrumentationGen(IsCS));
  // Add the profile lowering pass.
  InstrProfOptions Options;
  if (!ProfileFile.empty())
    Options.InstrProfileOutput = ProfileFile;
  // Do not do counter promotion at O0.
  Options.DoCounterPromotion = false;
  Options.UseBFIInPromotion = IsCS;
  Options.Atomic = AtomicCounterUpdate;
  MPM.addPass(InstrProfilingLoweringPass(Options, IsCS));
}

static InlineParams getInlineParamsFromOptLevel(OptimizationLevel Level) {
  return getInlineParams(Level.getSpeedupLevel(), Level.getSizeLevel());
}

ModuleInlinerWrapperPass
PassBuilder::buildInlinerPipeline(OptimizationLevel Level,
                                  ThinOrFullLTOPhase Phase) {
  InlineParams IP;
  if (PTO.InlinerThreshold == -1)
    IP = getInlineParamsFromOptLevel(Level);
  else
    IP = getInlineParams(PTO.InlinerThreshold);
  // For PreLinkThinLTO + SamplePGO, set hot-caller threshold to 0 to
  // disable hot callsite inline (as much as possible [1]) because it makes
  // profile annotation in the backend inaccurate.
  //
  // [1] Note the cost of a function could be below zero due to erased
  // prologue / epilogue.
  if (Phase == ThinOrFullLTOPhase::ThinLTOPreLink && PGOOpt &&
      PGOOpt->Action == PGOOptions::SampleUse)
    IP.HotCallSiteThreshold = 0;

  if (PGOOpt)
    IP.EnableDeferral = EnablePGOInlineDeferral;

  ModuleInlinerWrapperPass MIWP(IP, PerformMandatoryInliningsFirst,
                                InlineContext{Phase, InlinePass::CGSCCInliner},
                                UseInlineAdvisor, MaxDevirtIterations);

  // Require the GlobalsAA analysis for the module so we can query it within
  // the CGSCC pipeline.
  if (EnableGlobalAnalyses) {
    MIWP.addModulePass(RequireAnalysisPass<GlobalsAA, Module>());
    // Invalidate AAManager so it can be recreated and pick up the newly
    // available GlobalsAA.
    MIWP.addModulePass(
        createModuleToFunctionPassAdaptor(InvalidateAnalysisPass<AAManager>()));
  }

  // Require the ProfileSummaryAnalysis for the module so we can query it within
  // the inliner pass.
  MIWP.addModulePass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());

  // Now begin the main postorder CGSCC pipeline.
  // FIXME: The current CGSCC pipeline has its origins in the legacy pass
  // manager and trying to emulate its precise behavior. Much of this doesn't
  // make a lot of sense and we should revisit the core CGSCC structure.
  CGSCCPassManager &MainCGPipeline = MIWP.getPM();

  // Note: historically, the PruneEH pass was run first to deduce nounwind and
  // generally clean up exception handling overhead. It isn't clear this is
  // valuable as the inliner doesn't currently care whether it is inlining an
  // invoke or a call.

  if (AttributorRun & AttributorRunOption::CGSCC)
    MainCGPipeline.addPass(AttributorCGSCCPass());

  // Deduce function attributes. We do another run of this after the function
  // simplification pipeline, so this only needs to run when it could affect the
  // function simplification pipeline, which is only the case with recursive
  // functions.
  MainCGPipeline.addPass(PostOrderFunctionAttrsPass(/*SkipNonRecursive*/ true));

  // When at O3 add argument promotion to the pass pipeline.
  // FIXME: It isn't at all clear why this should be limited to O3.
  if (Level == OptimizationLevel::O3)
    MainCGPipeline.addPass(ArgumentPromotionPass());

  // Try to perform OpenMP specific optimizations. This is a (quick!) no-op if
  // there are no OpenMP runtime calls present in the module.
  if (Level == OptimizationLevel::O2 || Level == OptimizationLevel::O3)
    MainCGPipeline.addPass(OpenMPOptCGSCCPass());

  invokeCGSCCOptimizerLateEPCallbacks(MainCGPipeline, Level);

  // Add the core function simplification pipeline nested inside the
  // CGSCC walk.
  MainCGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
      buildFunctionSimplificationPipeline(Level, Phase),
      PTO.EagerlyInvalidateAnalyses, /*NoRerun=*/true));

  // Finally, deduce any function attributes based on the fully simplified
  // function.
  MainCGPipeline.addPass(PostOrderFunctionAttrsPass());

  // Mark that the function is fully simplified and that it shouldn't be
  // simplified again if we somehow revisit it due to CGSCC mutations unless
  // it's been modified since.
  MainCGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
      RequireAnalysisPass<ShouldNotRunFunctionPassesAnalysis, Function>()));

  MainCGPipeline.addPass(CoroSplitPass(Level != OptimizationLevel::O0));

  // Make sure we don't affect potential future NoRerun CGSCC adaptors.
  MIWP.addLateModulePass(createModuleToFunctionPassAdaptor(
      InvalidateAnalysisPass<ShouldNotRunFunctionPassesAnalysis>()));

  return MIWP;
}

ModulePassManager
PassBuilder::buildModuleInlinerPipeline(OptimizationLevel Level,
                                        ThinOrFullLTOPhase Phase) {
  ModulePassManager MPM;

  InlineParams IP = getInlineParamsFromOptLevel(Level);
  // For PreLinkThinLTO + SamplePGO, set hot-caller threshold to 0 to
  // disable hot callsite inline (as much as possible [1]) because it makes
  // profile annotation in the backend inaccurate.
  //
  // [1] Note the cost of a function could be below zero due to erased
  // prologue / epilogue.
  if (Phase == ThinOrFullLTOPhase::ThinLTOPreLink && PGOOpt &&
      PGOOpt->Action == PGOOptions::SampleUse)
    IP.HotCallSiteThreshold = 0;

  if (PGOOpt)
    IP.EnableDeferral = EnablePGOInlineDeferral;

  // The inline deferral logic is used to avoid losing some
  // inlining chance in future. It is helpful in SCC inliner, in which
  // inlining is processed in bottom-up order.
  // While in module inliner, the inlining order is a priority-based order
  // by default. The inline deferral is unnecessary there. So we disable the
  // inline deferral logic in module inliner.
  IP.EnableDeferral = false;

  MPM.addPass(ModuleInlinerPass(IP, UseInlineAdvisor, Phase));

  MPM.addPass(createModuleToFunctionPassAdaptor(
      buildFunctionSimplificationPipeline(Level, Phase),
      PTO.EagerlyInvalidateAnalyses));

  MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(
      CoroSplitPass(Level != OptimizationLevel::O0)));

  return MPM;
}

ModulePassManager
PassBuilder::buildModuleSimplificationPipeline(OptimizationLevel Level,
                                               ThinOrFullLTOPhase Phase) {
  assert(Level != OptimizationLevel::O0 &&
         "Should not be used for O0 pipeline");

  assert(Phase != ThinOrFullLTOPhase::FullLTOPostLink &&
         "FullLTOPostLink shouldn't call buildModuleSimplificationPipeline!");

  ModulePassManager MPM;

  // Place pseudo probe instrumentation as the first pass of the pipeline to
  // minimize the impact of optimization changes.
  if (PGOOpt && PGOOpt->PseudoProbeForProfiling &&
      Phase != ThinOrFullLTOPhase::ThinLTOPostLink)
    MPM.addPass(SampleProfileProbePass(TM));

  bool HasSampleProfile = PGOOpt && (PGOOpt->Action == PGOOptions::SampleUse);

  // In ThinLTO mode, when flattened profile is used, all the available
  // profile information will be annotated in PreLink phase so there is
  // no need to load the profile again in PostLink.
  bool LoadSampleProfile =
      HasSampleProfile &&
      !(FlattenedProfileUsed && Phase == ThinOrFullLTOPhase::ThinLTOPostLink);

  // During the ThinLTO backend phase we perform early indirect call promotion
  // here, before globalopt. Otherwise imported available_externally functions
  // look unreferenced and are removed. If we are going to load the sample
  // profile then defer until later.
  // TODO: See if we can move later and consolidate with the location where
  // we perform ICP when we are loading a sample profile.
  // TODO: We pass HasSampleProfile (whether there was a sample profile file
  // passed to the compile) to the SamplePGO flag of ICP. This is used to
  // determine whether the new direct calls are annotated with prof metadata.
  // Ideally this should be determined from whether the IR is annotated with
  // sample profile, and not whether the a sample profile was provided on the
  // command line. E.g. for flattened profiles where we will not be reloading
  // the sample profile in the ThinLTO backend, we ideally shouldn't have to
  // provide the sample profile file.
  if (Phase == ThinOrFullLTOPhase::ThinLTOPostLink && !LoadSampleProfile)
    MPM.addPass(PGOIndirectCallPromotion(true /* InLTO */, HasSampleProfile));

  // Create an early function pass manager to cleanup the output of the
  // frontend. Not necessary with LTO post link pipelines since the pre link
  // pipeline already cleaned up the frontend output.
  if (Phase != ThinOrFullLTOPhase::ThinLTOPostLink) {
    // Do basic inference of function attributes from known properties of system
    // libraries and other oracles.
    MPM.addPass(InferFunctionAttrsPass());
    MPM.addPass(CoroEarlyPass());

    FunctionPassManager EarlyFPM;
    EarlyFPM.addPass(EntryExitInstrumenterPass(/*PostInlining=*/false));
    // Lower llvm.expect to metadata before attempting transforms.
    // Compare/branch metadata may alter the behavior of passes like
    // SimplifyCFG.
    EarlyFPM.addPass(LowerExpectIntrinsicPass());
    EarlyFPM.addPass(SimplifyCFGPass());
    EarlyFPM.addPass(SROAPass(SROAOptions::ModifyCFG));
    EarlyFPM.addPass(EarlyCSEPass());
    if (Level == OptimizationLevel::O3)
      EarlyFPM.addPass(CallSiteSplittingPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(
        std::move(EarlyFPM), PTO.EagerlyInvalidateAnalyses));
  }

  if (LoadSampleProfile) {
    // Annotate sample profile right after early FPM to ensure freshness of
    // the debug info.
    MPM.addPass(SampleProfileLoaderPass(PGOOpt->ProfileFile,
                                        PGOOpt->ProfileRemappingFile, Phase));
    // Cache ProfileSummaryAnalysis once to avoid the potential need to insert
    // RequireAnalysisPass for PSI before subsequent non-module passes.
    MPM.addPass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());
    // Do not invoke ICP in the LTOPrelink phase as it makes it hard
    // for the profile annotation to be accurate in the LTO backend.
    if (!isLTOPreLink(Phase))
      // We perform early indirect call promotion here, before globalopt.
      // This is important for the ThinLTO backend phase because otherwise
      // imported available_externally functions look unreferenced and are
      // removed.
      MPM.addPass(
          PGOIndirectCallPromotion(true /* IsInLTO */, true /* SamplePGO */));
  }

  // Try to perform OpenMP specific optimizations on the module. This is a
  // (quick!) no-op if there are no OpenMP runtime calls present in the module.
  MPM.addPass(OpenMPOptPass());

  if (AttributorRun & AttributorRunOption::MODULE)
    MPM.addPass(AttributorPass());

  // Lower type metadata and the type.test intrinsic in the ThinLTO
  // post link pipeline after ICP. This is to enable usage of the type
  // tests in ICP sequences.
  if (Phase == ThinOrFullLTOPhase::ThinLTOPostLink)
    MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));

  invokePipelineEarlySimplificationEPCallbacks(MPM, Level);

  // Interprocedural constant propagation now that basic cleanup has occurred
  // and prior to optimizing globals.
  // FIXME: This position in the pipeline hasn't been carefully considered in
  // years, it should be re-analyzed.
  MPM.addPass(IPSCCPPass(
              IPSCCPOptions(/*AllowFuncSpec=*/
                            Level != OptimizationLevel::Os &&
                            Level != OptimizationLevel::Oz &&
                            !isLTOPreLink(Phase))));

  // Attach metadata to indirect call sites indicating the set of functions
  // they may target at run-time. This should follow IPSCCP.
  MPM.addPass(CalledValuePropagationPass());

  // Optimize globals to try and fold them into constants.
  MPM.addPass(GlobalOptPass());

  // Create a small function pass pipeline to cleanup after all the global
  // optimizations.
  FunctionPassManager GlobalCleanupPM;
  // FIXME: Should this instead by a run of SROA?
  GlobalCleanupPM.addPass(PromotePass());
  GlobalCleanupPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(GlobalCleanupPM, Level);
  GlobalCleanupPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(GlobalCleanupPM),
                                                PTO.EagerlyInvalidateAnalyses));

  // We already asserted this happens in non-FullLTOPostLink earlier.
  const bool IsPreLink = Phase != ThinOrFullLTOPhase::ThinLTOPostLink;
  const bool IsPGOPreLink = PGOOpt && IsPreLink;
  const bool IsPGOInstrGen =
      IsPGOPreLink && PGOOpt->Action == PGOOptions::IRInstr;
  const bool IsPGOInstrUse =
      IsPGOPreLink && PGOOpt->Action == PGOOptions::IRUse;
  const bool IsMemprofUse = IsPGOPreLink && !PGOOpt->MemoryProfile.empty();
  // We don't want to mix pgo ctx gen and pgo gen; we also don't currently
  // enable ctx profiling from the frontend.
  assert(
      !(IsPGOInstrGen && PGOCtxProfLoweringPass::isContextualIRPGOEnabled()) &&
      "Enabling both instrumented FDO and contextual instrumentation is not "
      "supported.");
  // Enable contextual profiling instrumentation.
  const bool IsCtxProfGen = !IsPGOInstrGen && IsPreLink &&
                            PGOCtxProfLoweringPass::isContextualIRPGOEnabled();

  if (IsPGOInstrGen || IsPGOInstrUse || IsMemprofUse || IsCtxProfGen)
    addPreInlinerPasses(MPM, Level, Phase);

  // Add all the requested passes for instrumentation PGO, if requested.
  if (IsPGOInstrGen || IsPGOInstrUse) {
    addPGOInstrPasses(MPM, Level,
                      /*RunProfileGen=*/IsPGOInstrGen,
                      /*IsCS=*/false, PGOOpt->AtomicCounterUpdate,
                      PGOOpt->ProfileFile, PGOOpt->ProfileRemappingFile,
                      PGOOpt->FS);
  } else if (IsCtxProfGen) {
    MPM.addPass(PGOInstrumentationGen(false));
    addPostPGOLoopRotation(MPM, Level);
    MPM.addPass(PGOCtxProfLoweringPass());
  }

  if (IsPGOInstrGen || IsPGOInstrUse || IsCtxProfGen)
    MPM.addPass(PGOIndirectCallPromotion(false, false));

  if (IsPGOPreLink && PGOOpt->CSAction == PGOOptions::CSIRInstr)
    MPM.addPass(PGOInstrumentationGenCreateVar(PGOOpt->CSProfileGenFile,
                                               EnableSampledInstr));

  if (IsMemprofUse)
    MPM.addPass(MemProfUsePass(PGOOpt->MemoryProfile, PGOOpt->FS));

  // Synthesize function entry counts for non-PGO compilation.
  if (EnableSyntheticCounts && !PGOOpt)
    MPM.addPass(SyntheticCountsPropagation());

  if (EnablePGOForceFunctionAttrs && PGOOpt)
    MPM.addPass(PGOForceFunctionAttrsPass(PGOOpt->ColdOptType));

  MPM.addPass(AlwaysInlinerPass(/*InsertLifetimeIntrinsics=*/true));

  if (EnableModuleInliner)
    MPM.addPass(buildModuleInlinerPipeline(Level, Phase));
  else
    MPM.addPass(buildInlinerPipeline(Level, Phase));

  // Remove any dead arguments exposed by cleanups, constant folding globals,
  // and argument promotion.
  MPM.addPass(DeadArgumentEliminationPass());

  MPM.addPass(CoroCleanupPass());

  // Optimize globals now that functions are fully simplified.
  MPM.addPass(GlobalOptPass());
  MPM.addPass(GlobalDCEPass());

  return MPM;
}

/// TODO: Should LTO cause any differences to this set of passes?
void PassBuilder::addVectorPasses(OptimizationLevel Level,
                                  FunctionPassManager &FPM, bool IsFullLTO) {
  FPM.addPass(LoopVectorizePass(
      LoopVectorizeOptions(!PTO.LoopInterleaving, !PTO.LoopVectorization)));

  if (EnableInferAlignmentPass)
    FPM.addPass(InferAlignmentPass());
  if (IsFullLTO) {
    // The vectorizer may have significantly shortened a loop body; unroll
    // again. Unroll small loops to hide loop backedge latency and saturate any
    // parallel execution resources of an out-of-order processor. We also then
    // need to clean up redundancies and loop invariant code.
    // FIXME: It would be really good to use a loop-integrated instruction
    // combiner for cleanup here so that the unrolling and LICM can be pipelined
    // across the loop nests.
    // We do UnrollAndJam in a separate LPM to ensure it happens before unroll
    if (EnableUnrollAndJam && PTO.LoopUnrolling)
      FPM.addPass(createFunctionToLoopPassAdaptor(
          LoopUnrollAndJamPass(Level.getSpeedupLevel())));
    FPM.addPass(LoopUnrollPass(LoopUnrollOptions(
        Level.getSpeedupLevel(), /*OnlyWhenForced=*/!PTO.LoopUnrolling,
        PTO.ForgetAllSCEVInLoopUnroll)));
    FPM.addPass(WarnMissedTransformationsPass());
    // Now that we are done with loop unrolling, be it either by LoopVectorizer,
    // or LoopUnroll passes, some variable-offset GEP's into alloca's could have
    // become constant-offset, thus enabling SROA and alloca promotion. Do so.
    // NOTE: we are very late in the pipeline, and we don't have any LICM
    // or SimplifyCFG passes scheduled after us, that would cleanup
    // the CFG mess this may created if allowed to modify CFG, so forbid that.
    FPM.addPass(SROAPass(SROAOptions::PreserveCFG));
  }

  if (!IsFullLTO) {
    // Eliminate loads by forwarding stores from the previous iteration to loads
    // of the current iteration.
    FPM.addPass(LoopLoadEliminationPass());
  }
  // Cleanup after the loop optimization passes.
  FPM.addPass(InstCombinePass());

  if (Level.getSpeedupLevel() > 1 && ExtraVectorizerPasses) {
    ExtraVectorPassManager ExtraPasses;
    // At higher optimization levels, try to clean up any runtime overlap and
    // alignment checks inserted by the vectorizer. We want to track correlated
    // runtime checks for two inner loops in the same outer loop, fold any
    // common computations, hoist loop-invariant aspects out of any outer loop,
    // and unswitch the runtime checks if possible. Once hoisted, we may have
    // dead (or speculatable) control flows or more combining opportunities.
    ExtraPasses.addPass(EarlyCSEPass());
    ExtraPasses.addPass(CorrelatedValuePropagationPass());
    ExtraPasses.addPass(InstCombinePass());
    LoopPassManager LPM;
    LPM.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                         /*AllowSpeculation=*/true));
    LPM.addPass(SimpleLoopUnswitchPass(/* NonTrivial */ Level ==
                                       OptimizationLevel::O3));
    ExtraPasses.addPass(
        createFunctionToLoopPassAdaptor(std::move(LPM), /*UseMemorySSA=*/true,
                                        /*UseBlockFrequencyInfo=*/true));
    ExtraPasses.addPass(
        SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
    ExtraPasses.addPass(InstCombinePass());
    FPM.addPass(std::move(ExtraPasses));
  }

  // Now that we've formed fast to execute loop structures, we do further
  // optimizations. These are run afterward as they might block doing complex
  // analyses and transforms such as what are needed for loop vectorization.

  // Cleanup after loop vectorization, etc. Simplification passes like CVP and
  // GVN, loop transforms, and others have already run, so it's now better to
  // convert to more optimized IR using more aggressive simplify CFG options.
  // The extra sinking transform can create larger basic blocks, so do this
  // before SLP vectorization.
  FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions()
                                  .forwardSwitchCondToPhi(true)
                                  .convertSwitchRangeToICmp(true)
                                  .convertSwitchToLookupTable(true)
                                  .needCanonicalLoops(false)
                                  .hoistCommonInsts(true)
                                  .sinkCommonInsts(true)));

  if (IsFullLTO) {
    FPM.addPass(SCCPPass());
    FPM.addPass(InstCombinePass());
    FPM.addPass(BDCEPass());
  }

  // Optimize parallel scalar instruction chains into SIMD instructions.
  if (PTO.SLPVectorization) {
    FPM.addPass(SLPVectorizerPass());
    if (Level.getSpeedupLevel() > 1 && ExtraVectorizerPasses) {
      FPM.addPass(EarlyCSEPass());
    }
  }
  // Enhance/cleanup vector code.
  FPM.addPass(VectorCombinePass());

  if (!IsFullLTO) {
    FPM.addPass(InstCombinePass());
    // Unroll small loops to hide loop backedge latency and saturate any
    // parallel execution resources of an out-of-order processor. We also then
    // need to clean up redundancies and loop invariant code.
    // FIXME: It would be really good to use a loop-integrated instruction
    // combiner for cleanup here so that the unrolling and LICM can be pipelined
    // across the loop nests.
    // We do UnrollAndJam in a separate LPM to ensure it happens before unroll
    if (EnableUnrollAndJam && PTO.LoopUnrolling) {
      FPM.addPass(createFunctionToLoopPassAdaptor(
          LoopUnrollAndJamPass(Level.getSpeedupLevel())));
    }
    FPM.addPass(LoopUnrollPass(LoopUnrollOptions(
        Level.getSpeedupLevel(), /*OnlyWhenForced=*/!PTO.LoopUnrolling,
        PTO.ForgetAllSCEVInLoopUnroll)));
    FPM.addPass(WarnMissedTransformationsPass());
    // Now that we are done with loop unrolling, be it either by LoopVectorizer,
    // or LoopUnroll passes, some variable-offset GEP's into alloca's could have
    // become constant-offset, thus enabling SROA and alloca promotion. Do so.
    // NOTE: we are very late in the pipeline, and we don't have any LICM
    // or SimplifyCFG passes scheduled after us, that would cleanup
    // the CFG mess this may created if allowed to modify CFG, so forbid that.
    FPM.addPass(SROAPass(SROAOptions::PreserveCFG));
  }

  if (EnableInferAlignmentPass)
    FPM.addPass(InferAlignmentPass());
  FPM.addPass(InstCombinePass());

  // This is needed for two reasons:
  //   1. It works around problems that instcombine introduces, such as sinking
  //      expensive FP divides into loops containing multiplications using the
  //      divide result.
  //   2. It helps to clean up some loop-invariant code created by the loop
  //      unroll pass when IsFullLTO=false.
  FPM.addPass(createFunctionToLoopPassAdaptor(
      LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
               /*AllowSpeculation=*/true),
      /*UseMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));

  // Now that we've vectorized and unrolled loops, we may have more refined
  // alignment information, try to re-derive it here.
  FPM.addPass(AlignmentFromAssumptionsPass());
}

ModulePassManager
PassBuilder::buildModuleOptimizationPipeline(OptimizationLevel Level,
                                             ThinOrFullLTOPhase LTOPhase) {
  const bool LTOPreLink = isLTOPreLink(LTOPhase);
  ModulePassManager MPM;

  // Run partial inlining pass to partially inline functions that have
  // large bodies.
  if (RunPartialInlining)
    MPM.addPass(PartialInlinerPass());

  // Remove avail extern fns and globals definitions since we aren't compiling
  // an object file for later LTO. For LTO we want to preserve these so they
  // are eligible for inlining at link-time. Note if they are unreferenced they
  // will be removed by GlobalDCE later, so this only impacts referenced
  // available externally globals. Eventually they will be suppressed during
  // codegen, but eliminating here enables more opportunity for GlobalDCE as it
  // may make globals referenced by available external functions dead and saves
  // running remaining passes on the eliminated functions. These should be
  // preserved during prelinking for link-time inlining decisions.
  if (!LTOPreLink)
    MPM.addPass(EliminateAvailableExternallyPass());

  if (EnableOrderFileInstrumentation)
    MPM.addPass(InstrOrderFilePass());

  // Do RPO function attribute inference across the module to forward-propagate
  // attributes where applicable.
  // FIXME: Is this really an optimization rather than a canonicalization?
  MPM.addPass(ReversePostOrderFunctionAttrsPass());

  // Do a post inline PGO instrumentation and use pass. This is a context
  // sensitive PGO pass. We don't want to do this in LTOPreLink phrase as
  // cross-module inline has not been done yet. The context sensitive
  // instrumentation is after all the inlines are done.
  if (!LTOPreLink && PGOOpt) {
    if (PGOOpt->CSAction == PGOOptions::CSIRInstr)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/true,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->CSProfileGenFile, PGOOpt->ProfileRemappingFile,
                        PGOOpt->FS);
    else if (PGOOpt->CSAction == PGOOptions::CSIRUse)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/false,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->ProfileFile, PGOOpt->ProfileRemappingFile,
                        PGOOpt->FS);
  }

  // Re-compute GlobalsAA here prior to function passes. This is particularly
  // useful as the above will have inlined, DCE'ed, and function-attr
  // propagated everything. We should at this point have a reasonably minimal
  // and richly annotated call graph. By computing aliasing and mod/ref
  // information for all local globals here, the late loop passes and notably
  // the vectorizer will be able to use them to help recognize vectorizable
  // memory operations.
  if (EnableGlobalAnalyses)
    MPM.addPass(RecomputeGlobalsAAPass());

  invokeOptimizerEarlyEPCallbacks(MPM, Level);

  FunctionPassManager OptimizePM;
  // Scheduling LoopVersioningLICM when inlining is over, because after that
  // we may see more accurate aliasing. Reason to run this late is that too
  // early versioning may prevent further inlining due to increase of code
  // size. Other optimizations which runs later might get benefit of no-alias
  // assumption in clone loop.
  if (UseLoopVersioningLICM) {
    OptimizePM.addPass(
        createFunctionToLoopPassAdaptor(LoopVersioningLICMPass()));
    // LoopVersioningLICM pass might increase new LICM opportunities.
    OptimizePM.addPass(createFunctionToLoopPassAdaptor(
        LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                 /*AllowSpeculation=*/true),
        /*USeMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));
  }

  OptimizePM.addPass(Float2IntPass());
  OptimizePM.addPass(LowerConstantIntrinsicsPass());

  if (EnableMatrix) {
    OptimizePM.addPass(LowerMatrixIntrinsicsPass());
    OptimizePM.addPass(EarlyCSEPass());
  }

  // CHR pass should only be applied with the profile information.
  // The check is to check the profile summary information in CHR.
  if (EnableCHR && Level == OptimizationLevel::O3)
    OptimizePM.addPass(ControlHeightReductionPass());

  // FIXME: We need to run some loop optimizations to re-rotate loops after
  // simplifycfg and others undo their rotation.

  // Optimize the loop execution. These passes operate on entire loop nests
  // rather than on each loop in an inside-out manner, and so they are actually
  // function passes.

  invokeVectorizerStartEPCallbacks(OptimizePM, Level);

  LoopPassManager LPM;
  // First rotate loops that may have been un-rotated by prior passes.
  // Disable header duplication at -Oz.
  LPM.addPass(LoopRotatePass(EnableLoopHeaderDuplication ||
                                 Level != OptimizationLevel::Oz,
                             LTOPreLink));
  // Some loops may have become dead by now. Try to delete them.
  // FIXME: see discussion in https://reviews.llvm.org/D112851,
  //        this may need to be revisited once we run GVN before loop deletion
  //        in the simplification pipeline.
  LPM.addPass(LoopDeletionPass());
  OptimizePM.addPass(createFunctionToLoopPassAdaptor(
      std::move(LPM), /*UseMemorySSA=*/false, /*UseBlockFrequencyInfo=*/false));

  // Distribute loops to allow partial vectorization.  I.e. isolate dependences
  // into separate loop that would otherwise inhibit vectorization.  This is
  // currently only performed for loops marked with the metadata
  // llvm.loop.distribute=true or when -enable-loop-distribute is specified.
  OptimizePM.addPass(LoopDistributePass());

  // Populates the VFABI attribute with the scalar-to-vector mappings
  // from the TargetLibraryInfo.
  OptimizePM.addPass(InjectTLIMappings());

  addVectorPasses(Level, OptimizePM, /* IsFullLTO */ false);

  // LoopSink pass sinks instructions hoisted by LICM, which serves as a
  // canonicalization pass that enables other optimizations. As a result,
  // LoopSink pass needs to be a very late IR pass to avoid undoing LICM
  // result too early.
  OptimizePM.addPass(LoopSinkPass());

  // And finally clean up LCSSA form before generating code.
  OptimizePM.addPass(InstSimplifyPass());

  // This hoists/decomposes div/rem ops. It should run after other sink/hoist
  // passes to avoid re-sinking, but before SimplifyCFG because it can allow
  // flattening of blocks.
  OptimizePM.addPass(DivRemPairsPass());

  // Try to annotate calls that were created during optimization.
  OptimizePM.addPass(TailCallElimPass());

  // LoopSink (and other loop passes since the last simplifyCFG) might have
  // resulted in single-entry-single-exit or empty blocks. Clean up the CFG.
  OptimizePM.addPass(SimplifyCFGPass(SimplifyCFGOptions()
                                         .convertSwitchRangeToICmp(true)
                                         .speculateUnpredictables(true)));

  // Add the core optimizing pipeline.
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(OptimizePM),
                                                PTO.EagerlyInvalidateAnalyses));

  invokeOptimizerLastEPCallbacks(MPM, Level);

  // Split out cold code. Splitting is done late to avoid hiding context from
  // other optimizations and inadvertently regressing performance. The tradeoff
  // is that this has a higher code size cost than splitting early.
  if (EnableHotColdSplit && !LTOPreLink)
    MPM.addPass(HotColdSplittingPass());

  // Search the code for similar regions of code. If enough similar regions can
  // be found where extracting the regions into their own function will decrease
  // the size of the program, we extract the regions, a deduplicate the
  // structurally similar regions.
  if (EnableIROutliner)
    MPM.addPass(IROutlinerPass());

  // Now we need to do some global optimization transforms.
  // FIXME: It would seem like these should come first in the optimization
  // pipeline and maybe be the bottom of the canonicalization pipeline? Weird
  // ordering here.
  MPM.addPass(GlobalDCEPass());
  MPM.addPass(ConstantMergePass());

  // Merge functions if requested. It has a better chance to merge functions
  // after ConstantMerge folded jump tables.
  if (PTO.MergeFunctions)
    MPM.addPass(MergeFunctionsPass());

  if (PTO.CallGraphProfile && !LTOPreLink)
    MPM.addPass(CGProfilePass(LTOPhase == ThinOrFullLTOPhase::FullLTOPostLink ||
                              LTOPhase == ThinOrFullLTOPhase::ThinLTOPostLink));

  // TODO: Relative look table converter pass caused an issue when full lto is
  // enabled. See https://reviews.llvm.org/D94355 for more details.
  // Until the issue fixed, disable this pass during pre-linking phase.
  if (!LTOPreLink)
    MPM.addPass(RelLookupTableConverterPass());

  return MPM;
}

ModulePassManager
PassBuilder::buildPerModuleDefaultPipeline(OptimizationLevel Level,
                                           bool LTOPreLink) {
  if (Level == OptimizationLevel::O0)
    return buildO0DefaultPipeline(Level, LTOPreLink);

  ModulePassManager MPM;

  // Convert @llvm.global.annotations to !annotation metadata.
  MPM.addPass(Annotation2MetadataPass());

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());

  if (PGOOpt && PGOOpt->DebugInfoForProfiling)
    MPM.addPass(createModuleToFunctionPassAdaptor(AddDiscriminatorsPass()));

  // Apply module pipeline start EP callback.
  invokePipelineStartEPCallbacks(MPM, Level);

  const ThinOrFullLTOPhase LTOPhase = LTOPreLink
                                          ? ThinOrFullLTOPhase::FullLTOPreLink
                                          : ThinOrFullLTOPhase::None;
  // Add the core simplification pipeline.
  MPM.addPass(buildModuleSimplificationPipeline(Level, LTOPhase));

  // Now add the optimization pipeline.
  MPM.addPass(buildModuleOptimizationPipeline(Level, LTOPhase));

  if (PGOOpt && PGOOpt->PseudoProbeForProfiling &&
      PGOOpt->Action == PGOOptions::SampleUse)
    MPM.addPass(PseudoProbeUpdatePass());

  // Emit annotation remarks.
  addAnnotationRemarksPass(MPM);

  if (LTOPreLink)
    addRequiredLTOPreLinkPasses(MPM);
  return MPM;
}

ModulePassManager
PassBuilder::buildFatLTODefaultPipeline(OptimizationLevel Level, bool ThinLTO,
                                        bool EmitSummary) {
  ModulePassManager MPM;
  if (ThinLTO)
    MPM.addPass(buildThinLTOPreLinkDefaultPipeline(Level));
  else
    MPM.addPass(buildLTOPreLinkDefaultPipeline(Level));
  MPM.addPass(EmbedBitcodePass(ThinLTO, EmitSummary));

  // Use the ThinLTO post-link pipeline with sample profiling
  if (ThinLTO && PGOOpt && PGOOpt->Action == PGOOptions::SampleUse)
    MPM.addPass(buildThinLTODefaultPipeline(Level, /*ImportSummary=*/nullptr));
  else {
    // otherwise, just use module optimization
    MPM.addPass(
        buildModuleOptimizationPipeline(Level, ThinOrFullLTOPhase::None));
    // Emit annotation remarks.
    addAnnotationRemarksPass(MPM);
  }
  return MPM;
}

ModulePassManager
PassBuilder::buildThinLTOPreLinkDefaultPipeline(OptimizationLevel Level) {
  if (Level == OptimizationLevel::O0)
    return buildO0DefaultPipeline(Level, /*LTOPreLink*/true);

  ModulePassManager MPM;

  // Convert @llvm.global.annotations to !annotation metadata.
  MPM.addPass(Annotation2MetadataPass());

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());

  if (PGOOpt && PGOOpt->DebugInfoForProfiling)
    MPM.addPass(createModuleToFunctionPassAdaptor(AddDiscriminatorsPass()));

  // Apply module pipeline start EP callback.
  invokePipelineStartEPCallbacks(MPM, Level);

  // If we are planning to perform ThinLTO later, we don't bloat the code with
  // unrolling/vectorization/... now. Just simplify the module as much as we
  // can.
  MPM.addPass(buildModuleSimplificationPipeline(
      Level, ThinOrFullLTOPhase::ThinLTOPreLink));

  // Run partial inlining pass to partially inline functions that have
  // large bodies.
  // FIXME: It isn't clear whether this is really the right place to run this
  // in ThinLTO. Because there is another canonicalization and simplification
  // phase that will run after the thin link, running this here ends up with
  // less information than will be available later and it may grow functions in
  // ways that aren't beneficial.
  if (RunPartialInlining)
    MPM.addPass(PartialInlinerPass());

  if (PGOOpt && PGOOpt->PseudoProbeForProfiling &&
      PGOOpt->Action == PGOOptions::SampleUse)
    MPM.addPass(PseudoProbeUpdatePass());

  // Handle Optimizer{Early,Last}EPCallbacks added by clang on PreLink. Actual
  // optimization is going to be done in PostLink stage, but clang can't add
  // callbacks there in case of in-process ThinLTO called by linker.
  invokeOptimizerEarlyEPCallbacks(MPM, Level);
  invokeOptimizerLastEPCallbacks(MPM, Level);

  // Emit annotation remarks.
  addAnnotationRemarksPass(MPM);

  addRequiredLTOPreLinkPasses(MPM);

  return MPM;
}

ModulePassManager PassBuilder::buildThinLTODefaultPipeline(
    OptimizationLevel Level, const ModuleSummaryIndex *ImportSummary) {
  ModulePassManager MPM;

  if (ImportSummary) {
    // For ThinLTO we must apply the context disambiguation decisions early, to
    // ensure we can correctly match the callsites to summary data.
    if (EnableMemProfContextDisambiguation)
      MPM.addPass(MemProfContextDisambiguation(ImportSummary));

    // These passes import type identifier resolutions for whole-program
    // devirtualization and CFI. They must run early because other passes may
    // disturb the specific instruction patterns that these passes look for,
    // creating dependencies on resolutions that may not appear in the summary.
    //
    // For example, GVN may transform the pattern assume(type.test) appearing in
    // two basic blocks into assume(phi(type.test, type.test)), which would
    // transform a dependency on a WPD resolution into a dependency on a type
    // identifier resolution for CFI.
    //
    // Also, WPD has access to more precise information than ICP and can
    // devirtualize more effectively, so it should operate on the IR first.
    //
    // The WPD and LowerTypeTest passes need to run at -O0 to lower type
    // metadata and intrinsics.
    MPM.addPass(WholeProgramDevirtPass(nullptr, ImportSummary));
    MPM.addPass(LowerTypeTestsPass(nullptr, ImportSummary));
  }

  if (Level == OptimizationLevel::O0) {
    // Run a second time to clean up any type tests left behind by WPD for use
    // in ICP.
    MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));
    // Drop available_externally and unreferenced globals. This is necessary
    // with ThinLTO in order to avoid leaving undefined references to dead
    // globals in the object file.
    MPM.addPass(EliminateAvailableExternallyPass());
    MPM.addPass(GlobalDCEPass());
    return MPM;
  }

  // Add the core simplification pipeline.
  MPM.addPass(buildModuleSimplificationPipeline(
      Level, ThinOrFullLTOPhase::ThinLTOPostLink));

  // Now add the optimization pipeline.
  MPM.addPass(buildModuleOptimizationPipeline(
      Level, ThinOrFullLTOPhase::ThinLTOPostLink));

  // Emit annotation remarks.
  addAnnotationRemarksPass(MPM);

  return MPM;
}

ModulePassManager
PassBuilder::buildLTOPreLinkDefaultPipeline(OptimizationLevel Level) {
  // FIXME: We should use a customized pre-link pipeline!
  return buildPerModuleDefaultPipeline(Level,
                                       /* LTOPreLink */ true);
}

ModulePassManager
PassBuilder::buildLTODefaultPipeline(OptimizationLevel Level,
                                     ModuleSummaryIndex *ExportSummary) {
  ModulePassManager MPM;

  invokeFullLinkTimeOptimizationEarlyEPCallbacks(MPM, Level);

  // Create a function that performs CFI checks for cross-DSO calls with targets
  // in the current module.
  MPM.addPass(CrossDSOCFIPass());

  if (Level == OptimizationLevel::O0) {
    // The WPD and LowerTypeTest passes need to run at -O0 to lower type
    // metadata and intrinsics.
    MPM.addPass(WholeProgramDevirtPass(ExportSummary, nullptr));
    MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));
    // Run a second time to clean up any type tests left behind by WPD for use
    // in ICP.
    MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));

    invokeFullLinkTimeOptimizationLastEPCallbacks(MPM, Level);

    // Emit annotation remarks.
    addAnnotationRemarksPass(MPM);

    return MPM;
  }

  if (PGOOpt && PGOOpt->Action == PGOOptions::SampleUse) {
    // Load sample profile before running the LTO optimization pipeline.
    MPM.addPass(SampleProfileLoaderPass(PGOOpt->ProfileFile,
                                        PGOOpt->ProfileRemappingFile,
                                        ThinOrFullLTOPhase::FullLTOPostLink));
    // Cache ProfileSummaryAnalysis once to avoid the potential need to insert
    // RequireAnalysisPass for PSI before subsequent non-module passes.
    MPM.addPass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());
  }

  // Try to run OpenMP optimizations, quick no-op if no OpenMP metadata present.
  MPM.addPass(OpenMPOptPass(ThinOrFullLTOPhase::FullLTOPostLink));

  // Remove unused virtual tables to improve the quality of code generated by
  // whole-program devirtualization and bitset lowering.
  MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));

  // Do basic inference of function attributes from known properties of system
  // libraries and other oracles.
  MPM.addPass(InferFunctionAttrsPass());

  if (Level.getSpeedupLevel() > 1) {
    MPM.addPass(createModuleToFunctionPassAdaptor(
        CallSiteSplittingPass(), PTO.EagerlyInvalidateAnalyses));

    // Indirect call promotion. This should promote all the targets that are
    // left by the earlier promotion pass that promotes intra-module targets.
    // This two-step promotion is to save the compile time. For LTO, it should
    // produce the same result as if we only do promotion here.
    MPM.addPass(PGOIndirectCallPromotion(
        true /* InLTO */, PGOOpt && PGOOpt->Action == PGOOptions::SampleUse));

    // Propagate constants at call sites into the functions they call.  This
    // opens opportunities for globalopt (and inlining) by substituting function
    // pointers passed as arguments to direct uses of functions.
    MPM.addPass(IPSCCPPass(IPSCCPOptions(/*AllowFuncSpec=*/
                                         Level != OptimizationLevel::Os &&
                                         Level != OptimizationLevel::Oz)));

    // Attach metadata to indirect call sites indicating the set of functions
    // they may target at run-time. This should follow IPSCCP.
    MPM.addPass(CalledValuePropagationPass());
  }

  // Now deduce any function attributes based in the current code.
  MPM.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));

  // Do RPO function attribute inference across the module to forward-propagate
  // attributes where applicable.
  // FIXME: Is this really an optimization rather than a canonicalization?
  MPM.addPass(ReversePostOrderFunctionAttrsPass());

  // Use in-range annotations on GEP indices to split globals where beneficial.
  MPM.addPass(GlobalSplitPass());

  // Run whole program optimization of virtual call when the list of callees
  // is fixed.
  MPM.addPass(WholeProgramDevirtPass(ExportSummary, nullptr));

  // Stop here at -O1.
  if (Level == OptimizationLevel::O1) {
    // The LowerTypeTestsPass needs to run to lower type metadata and the
    // type.test intrinsics. The pass does nothing if CFI is disabled.
    MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));
    // Run a second time to clean up any type tests left behind by WPD for use
    // in ICP (which is performed earlier than this in the regular LTO
    // pipeline).
    MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));

    invokeFullLinkTimeOptimizationLastEPCallbacks(MPM, Level);

    // Emit annotation remarks.
    addAnnotationRemarksPass(MPM);

    return MPM;
  }

  // Optimize globals to try and fold them into constants.
  MPM.addPass(GlobalOptPass());

  // Promote any localized globals to SSA registers.
  MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));

  // Linking modules together can lead to duplicate global constant, only
  // keep one copy of each constant.
  MPM.addPass(ConstantMergePass());

  // Remove unused arguments from functions.
  MPM.addPass(DeadArgumentEliminationPass());

  // Reduce the code after globalopt and ipsccp.  Both can open up significant
  // simplification opportunities, and both can propagate functions through
  // function pointers.  When this happens, we often have to resolve varargs
  // calls, etc, so let instcombine do this.
  FunctionPassManager PeepholeFPM;
  PeepholeFPM.addPass(InstCombinePass());
  if (Level.getSpeedupLevel() > 1)
    PeepholeFPM.addPass(AggressiveInstCombinePass());
  invokePeepholeEPCallbacks(PeepholeFPM, Level);

  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(PeepholeFPM),
                                                PTO.EagerlyInvalidateAnalyses));

  // Note: historically, the PruneEH pass was run first to deduce nounwind and
  // generally clean up exception handling overhead. It isn't clear this is
  // valuable as the inliner doesn't currently care whether it is inlining an
  // invoke or a call.
  // Run the inliner now.
  if (EnableModuleInliner) {
    MPM.addPass(ModuleInlinerPass(getInlineParamsFromOptLevel(Level),
                                  UseInlineAdvisor,
                                  ThinOrFullLTOPhase::FullLTOPostLink));
  } else {
    MPM.addPass(ModuleInlinerWrapperPass(
        getInlineParamsFromOptLevel(Level),
        /* MandatoryFirst */ true,
        InlineContext{ThinOrFullLTOPhase::FullLTOPostLink,
                      InlinePass::CGSCCInliner}));
  }

  // Perform context disambiguation after inlining, since that would reduce the
  // amount of additional cloning required to distinguish the allocation
  // contexts.
  if (EnableMemProfContextDisambiguation)
    MPM.addPass(MemProfContextDisambiguation());

  // Optimize globals again after we ran the inliner.
  MPM.addPass(GlobalOptPass());

  // Run the OpenMPOpt pass again after global optimizations.
  MPM.addPass(OpenMPOptPass(ThinOrFullLTOPhase::FullLTOPostLink));

  // Garbage collect dead functions.
  MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));

  // If we didn't decide to inline a function, check to see if we can
  // transform it to pass arguments by value instead of by reference.
  MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(ArgumentPromotionPass()));

  FunctionPassManager FPM;
  // The IPO Passes may leave cruft around. Clean up after them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  if (EnableConstraintElimination)
    FPM.addPass(ConstraintEliminationPass());

  FPM.addPass(JumpThreadingPass());

  // Do a post inline PGO instrumentation and use pass. This is a context
  // sensitive PGO pass.
  if (PGOOpt) {
    if (PGOOpt->CSAction == PGOOptions::CSIRInstr)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/true,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->CSProfileGenFile, PGOOpt->ProfileRemappingFile,
                        PGOOpt->FS);
    else if (PGOOpt->CSAction == PGOOptions::CSIRUse)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/false,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->ProfileFile, PGOOpt->ProfileRemappingFile,
                        PGOOpt->FS);
  }

  // Break up allocas
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // LTO provides additional opportunities for tailcall elimination due to
  // link-time inlining, and visibility of nocapture attribute.
  FPM.addPass(TailCallElimPass());

  // Run a few AA driver optimizations here and now to cleanup the code.
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM),
                                                PTO.EagerlyInvalidateAnalyses));

  MPM.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));

  // Require the GlobalsAA analysis for the module so we can query it within
  // MainFPM.
  if (EnableGlobalAnalyses) {
    MPM.addPass(RequireAnalysisPass<GlobalsAA, Module>());
    // Invalidate AAManager so it can be recreated and pick up the newly
    // available GlobalsAA.
    MPM.addPass(
        createModuleToFunctionPassAdaptor(InvalidateAnalysisPass<AAManager>()));
  }

  FunctionPassManager MainFPM;
  MainFPM.addPass(createFunctionToLoopPassAdaptor(
      LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
               /*AllowSpeculation=*/true),
      /*USeMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));

  if (RunNewGVN)
    MainFPM.addPass(NewGVNPass());
  else
    MainFPM.addPass(GVNPass());

  // Remove dead memcpy()'s.
  MainFPM.addPass(MemCpyOptPass());

  // Nuke dead stores.
  MainFPM.addPass(DSEPass());
  MainFPM.addPass(MoveAutoInitPass());
  MainFPM.addPass(MergedLoadStoreMotionPass());

  LoopPassManager LPM;
  if (EnableLoopFlatten && Level.getSpeedupLevel() > 1)
    LPM.addPass(LoopFlattenPass());
  LPM.addPass(IndVarSimplifyPass());
  LPM.addPass(LoopDeletionPass());
  // FIXME: Add loop interchange.

  // Unroll small loops and perform peeling.
  LPM.addPass(LoopFullUnrollPass(Level.getSpeedupLevel(),
                                 /* OnlyWhenForced= */ !PTO.LoopUnrolling,
                                 PTO.ForgetAllSCEVInLoopUnroll));
  // The loop passes in LPM (LoopFullUnrollPass) do not preserve MemorySSA.
  // *All* loop passes must preserve it, in order to be able to use it.
  MainFPM.addPass(createFunctionToLoopPassAdaptor(
      std::move(LPM), /*UseMemorySSA=*/false, /*UseBlockFrequencyInfo=*/true));

  MainFPM.addPass(LoopDistributePass());

  addVectorPasses(Level, MainFPM, /* IsFullLTO */ true);

  // Run the OpenMPOpt CGSCC pass again late.
  MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(
      OpenMPOptCGSCCPass(ThinOrFullLTOPhase::FullLTOPostLink)));

  invokePeepholeEPCallbacks(MainFPM, Level);
  MainFPM.addPass(JumpThreadingPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(MainFPM),
                                                PTO.EagerlyInvalidateAnalyses));

  // Lower type metadata and the type.test intrinsic. This pass supports
  // clang's control flow integrity mechanisms (-fsanitize=cfi*) and needs
  // to be run at link time if CFI is enabled. This pass does nothing if
  // CFI is disabled.
  MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));
  // Run a second time to clean up any type tests left behind by WPD for use
  // in ICP (which is performed earlier than this in the regular LTO pipeline).
  MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));

  // Enable splitting late in the FullLTO post-link pipeline.
  if (EnableHotColdSplit)
    MPM.addPass(HotColdSplittingPass());

  // Add late LTO optimization passes.
  FunctionPassManager LateFPM;

  // LoopSink pass sinks instructions hoisted by LICM, which serves as a
  // canonicalization pass that enables other optimizations. As a result,
  // LoopSink pass needs to be a very late IR pass to avoid undoing LICM
  // result too early.
  LateFPM.addPass(LoopSinkPass());

  // This hoists/decomposes div/rem ops. It should run after other sink/hoist
  // passes to avoid re-sinking, but before SimplifyCFG because it can allow
  // flattening of blocks.
  LateFPM.addPass(DivRemPairsPass());

  // Delete basic blocks, which optimization passes may have killed.
  LateFPM.addPass(SimplifyCFGPass(SimplifyCFGOptions()
                                      .convertSwitchRangeToICmp(true)
                                      .hoistCommonInsts(true)
                                      .speculateUnpredictables(true)));
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(LateFPM)));

  // Drop bodies of available eternally objects to improve GlobalDCE.
  MPM.addPass(EliminateAvailableExternallyPass());

  // Now that we have optimized the program, discard unreachable functions.
  MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));

  if (PTO.MergeFunctions)
    MPM.addPass(MergeFunctionsPass());

  if (PTO.CallGraphProfile)
    MPM.addPass(CGProfilePass(/*InLTOPostLink=*/true));

  invokeFullLinkTimeOptimizationLastEPCallbacks(MPM, Level);

  // Emit annotation remarks.
  addAnnotationRemarksPass(MPM);

  return MPM;
}

ModulePassManager PassBuilder::buildO0DefaultPipeline(OptimizationLevel Level,
                                                      bool LTOPreLink) {
  assert(Level == OptimizationLevel::O0 &&
         "buildO0DefaultPipeline should only be used with O0");

  ModulePassManager MPM;

  // Perform pseudo probe instrumentation in O0 mode. This is for the
  // consistency between different build modes. For example, a LTO build can be
  // mixed with an O0 prelink and an O2 postlink. Loading a sample profile in
  // the postlink will require pseudo probe instrumentation in the prelink.
  if (PGOOpt && PGOOpt->PseudoProbeForProfiling)
    MPM.addPass(SampleProfileProbePass(TM));

  if (PGOOpt && (PGOOpt->Action == PGOOptions::IRInstr ||
                 PGOOpt->Action == PGOOptions::IRUse))
    addPGOInstrPassesForO0(
        MPM,
        /*RunProfileGen=*/(PGOOpt->Action == PGOOptions::IRInstr),
        /*IsCS=*/false, PGOOpt->AtomicCounterUpdate, PGOOpt->ProfileFile,
        PGOOpt->ProfileRemappingFile, PGOOpt->FS);

  // Instrument function entry and exit before all inlining.
  MPM.addPass(createModuleToFunctionPassAdaptor(
      EntryExitInstrumenterPass(/*PostInlining=*/false)));

  invokePipelineStartEPCallbacks(MPM, Level);

  if (PGOOpt && PGOOpt->DebugInfoForProfiling)
    MPM.addPass(createModuleToFunctionPassAdaptor(AddDiscriminatorsPass()));

  invokePipelineEarlySimplificationEPCallbacks(MPM, Level);

  // Build a minimal pipeline based on the semantics required by LLVM,
  // which is just that always inlining occurs. Further, disable generating
  // lifetime intrinsics to avoid enabling further optimizations during
  // code generation.
  MPM.addPass(AlwaysInlinerPass(
      /*InsertLifetimeIntrinsics=*/false));

  if (PTO.MergeFunctions)
    MPM.addPass(MergeFunctionsPass());

  if (EnableMatrix)
    MPM.addPass(
        createModuleToFunctionPassAdaptor(LowerMatrixIntrinsicsPass(true)));

  if (!CGSCCOptimizerLateEPCallbacks.empty()) {
    CGSCCPassManager CGPM;
    invokeCGSCCOptimizerLateEPCallbacks(CGPM, Level);
    if (!CGPM.isEmpty())
      MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));
  }
  if (!LateLoopOptimizationsEPCallbacks.empty()) {
    LoopPassManager LPM;
    invokeLateLoopOptimizationsEPCallbacks(LPM, Level);
    if (!LPM.isEmpty()) {
      MPM.addPass(createModuleToFunctionPassAdaptor(
          createFunctionToLoopPassAdaptor(std::move(LPM))));
    }
  }
  if (!LoopOptimizerEndEPCallbacks.empty()) {
    LoopPassManager LPM;
    invokeLoopOptimizerEndEPCallbacks(LPM, Level);
    if (!LPM.isEmpty()) {
      MPM.addPass(createModuleToFunctionPassAdaptor(
          createFunctionToLoopPassAdaptor(std::move(LPM))));
    }
  }
  if (!ScalarOptimizerLateEPCallbacks.empty()) {
    FunctionPassManager FPM;
    invokeScalarOptimizerLateEPCallbacks(FPM, Level);
    if (!FPM.isEmpty())
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }

  invokeOptimizerEarlyEPCallbacks(MPM, Level);

  if (!VectorizerStartEPCallbacks.empty()) {
    FunctionPassManager FPM;
    invokeVectorizerStartEPCallbacks(FPM, Level);
    if (!FPM.isEmpty())
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }

  ModulePassManager CoroPM;
  CoroPM.addPass(CoroEarlyPass());
  CGSCCPassManager CGPM;
  CGPM.addPass(CoroSplitPass());
  CoroPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));
  CoroPM.addPass(CoroCleanupPass());
  CoroPM.addPass(GlobalDCEPass());
  MPM.addPass(CoroConditionalWrapper(std::move(CoroPM)));

  invokeOptimizerLastEPCallbacks(MPM, Level);

  if (LTOPreLink)
    addRequiredLTOPreLinkPasses(MPM);

  MPM.addPass(createModuleToFunctionPassAdaptor(AnnotationRemarksPass()));

  return MPM;
}

AAManager PassBuilder::buildDefaultAAPipeline() {
  AAManager AA;

  // The order in which these are registered determines their priority when
  // being queried.

  // First we register the basic alias analysis that provides the majority of
  // per-function local AA logic. This is a stateless, on-demand local set of
  // AA techniques.
  AA.registerFunctionAnalysis<BasicAA>();

  // Next we query fast, specialized alias analyses that wrap IR-embedded
  // information about aliasing.
  AA.registerFunctionAnalysis<ScopedNoAliasAA>();
  AA.registerFunctionAnalysis<TypeBasedAA>();

  // Add support for querying global aliasing information when available.
  // Because the `AAManager` is a function analysis and `GlobalsAA` is a module
  // analysis, all that the `AAManager` can do is query for any *cached*
  // results from `GlobalsAA` through a readonly proxy.
  if (EnableGlobalAnalyses)
    AA.registerModuleAnalysis<GlobalsAA>();

  // Add target-specific alias analyses.
  if (TM)
    TM->registerDefaultAliasAnalyses(AA);

  return AA;
}
