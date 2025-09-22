//===- Parsing and selection of pass pipelines ----------------------------===//
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

#include "llvm/Passes/PassBuilder.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/AliasAnalysisEvaluator.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Analysis/CFGSCCPrinter.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallPrinter.h"
#include "llvm/Analysis/CostModel.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/DDG.h"
#include "llvm/Analysis/DDGPrinter.h"
#include "llvm/Analysis/Delinearization.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DomPrinter.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/IRSimilarityIdentifier.h"
#include "llvm/Analysis/IVUsers.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineSizeEstimatorAnalysis.h"
#include "llvm/Analysis/InstCount.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/Lint.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopCacheAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopNestAnalysis.h"
#include "llvm/Analysis/MemDerefPrinter.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ModuleDebugInfoPrinter.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/ObjCARCAliasAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PhiValues.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/StackLifetime.h"
#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/Analysis/StructuralHash.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/CodeGen/AssignmentTrackingAnalysis.h"
#include "llvm/CodeGen/AtomicExpand.h"
#include "llvm/CodeGen/BasicBlockSectionsProfileReader.h"
#include "llvm/CodeGen/CallBrPrepare.h"
#include "llvm/CodeGen/CodeGenPrepare.h"
#include "llvm/CodeGen/DeadMachineInstructionElim.h"
#include "llvm/CodeGen/DwarfEHPrepare.h"
#include "llvm/CodeGen/ExpandLargeDivRem.h"
#include "llvm/CodeGen/ExpandLargeFpConvert.h"
#include "llvm/CodeGen/ExpandMemCmp.h"
#include "llvm/CodeGen/FinalizeISel.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/GlobalMerge.h"
#include "llvm/CodeGen/HardwareLoops.h"
#include "llvm/CodeGen/IndirectBrExpand.h"
#include "llvm/CodeGen/InterleavedAccess.h"
#include "llvm/CodeGen/InterleavedLoadCombine.h"
#include "llvm/CodeGen/JMCInstrumenter.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/LocalStackSlotAllocation.h"
#include "llvm/CodeGen/LowerEmuTLS.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineVerifier.h"
#include "llvm/CodeGen/PHIElimination.h"
#include "llvm/CodeGen/PreISelIntrinsicLowering.h"
#include "llvm/CodeGen/RegAllocFast.h"
#include "llvm/CodeGen/SafeStack.h"
#include "llvm/CodeGen/SelectOptimize.h"
#include "llvm/CodeGen/ShadowStackGCLowering.h"
#include "llvm/CodeGen/SjLjEHPrepare.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TwoAddressInstructionPass.h"
#include "llvm/CodeGen/TypePromotion.h"
#include "llvm/CodeGen/WasmEHPrepare.h"
#include "llvm/CodeGen/WinEHPrepare.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/IR/SafepointIRVerifier.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Regex.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/CFGuard.h"
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
#include "llvm/Transforms/IPO/BlockExtractor.h"
#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/CrossDSOCFI.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/ElimAvailExtern.h"
#include "llvm/Transforms/IPO/EmbedBitcodePass.h"
#include "llvm/Transforms/IPO/ExpandVariadics.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/GlobalSplit.h"
#include "llvm/Transforms/IPO/HotColdSplitting.h"
#include "llvm/Transforms/IPO/IROutliner.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/IPO/LoopExtractor.h"
#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/Transforms/IPO/MemProfContextDisambiguation.h"
#include "llvm/Transforms/IPO/MergeFunctions.h"
#include "llvm/Transforms/IPO/ModuleInliner.h"
#include "llvm/Transforms/IPO/OpenMPOpt.h"
#include "llvm/Transforms/IPO/PartialInlining.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/IPO/SampleProfile.h"
#include "llvm/Transforms/IPO/SampleProfileProbe.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/IPO/StripSymbols.h"
#include "llvm/Transforms/IPO/SyntheticCountsPropagation.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/BoundsChecking.h"
#include "llvm/Transforms/Instrumentation/CGProfile.h"
#include "llvm/Transforms/Instrumentation/ControlHeightReduction.h"
#include "llvm/Transforms/Instrumentation/DataFlowSanitizer.h"
#include "llvm/Transforms/Instrumentation/GCOVProfiler.h"
#include "llvm/Transforms/Instrumentation/HWAddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/InstrOrderFile.h"
#include "llvm/Transforms/Instrumentation/InstrProfiling.h"
#include "llvm/Transforms/Instrumentation/KCFI.h"
#include "llvm/Transforms/Instrumentation/LowerAllowCheckPass.h"
#include "llvm/Transforms/Instrumentation/MemProfiler.h"
#include "llvm/Transforms/Instrumentation/MemorySanitizer.h"
#include "llvm/Transforms/Instrumentation/NumericalStabilitySanitizer.h"
#include "llvm/Transforms/Instrumentation/PGOCtxProfLowering.h"
#include "llvm/Transforms/Instrumentation/PGOForceFunctionAttrs.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Instrumentation/PoisonChecking.h"
#include "llvm/Transforms/Instrumentation/SanitizerBinaryMetadata.h"
#include "llvm/Transforms/Instrumentation/SanitizerCoverage.h"
#include "llvm/Transforms/Instrumentation/ThreadSanitizer.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/AlignmentFromAssumptions.h"
#include "llvm/Transforms/Scalar/AnnotationRemarks.h"
#include "llvm/Transforms/Scalar/BDCE.h"
#include "llvm/Transforms/Scalar/CallSiteSplitting.h"
#include "llvm/Transforms/Scalar/ConstantHoisting.h"
#include "llvm/Transforms/Scalar/ConstraintElimination.h"
#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/DFAJumpThreading.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/FlattenCFG.h"
#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/GuardWidening.h"
#include "llvm/Transforms/Scalar/IVUsersPrinter.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/InductiveRangeCheckElimination.h"
#include "llvm/Transforms/Scalar/InferAddressSpaces.h"
#include "llvm/Transforms/Scalar/InferAlignment.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/Transforms/Scalar/JumpTableToSwitch.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopAccessAnalysisPrinter.h"
#include "llvm/Transforms/Scalar/LoopBoundSplit.h"
#include "llvm/Transforms/Scalar/LoopDataPrefetch.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopDistribute.h"
#include "llvm/Transforms/Scalar/LoopFlatten.h"
#include "llvm/Transforms/Scalar/LoopFuse.h"
#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/Transforms/Scalar/LoopInstSimplify.h"
#include "llvm/Transforms/Scalar/LoopInterchange.h"
#include "llvm/Transforms/Scalar/LoopLoadElimination.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopPredication.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"
#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/Transforms/Scalar/LoopStrengthReduce.h"
#include "llvm/Transforms/Scalar/LoopUnrollAndJamPass.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/LoopVersioningLICM.h"
#include "llvm/Transforms/Scalar/LowerAtomicPass.h"
#include "llvm/Transforms/Scalar/LowerConstantIntrinsics.h"
#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/Transforms/Scalar/LowerGuardIntrinsic.h"
#include "llvm/Transforms/Scalar/LowerMatrixIntrinsics.h"
#include "llvm/Transforms/Scalar/LowerWidenableCondition.h"
#include "llvm/Transforms/Scalar/MakeGuardsExplicit.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/MergeICmps.h"
#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"
#include "llvm/Transforms/Scalar/NaryReassociate.h"
#include "llvm/Transforms/Scalar/NewGVN.h"
#include "llvm/Transforms/Scalar/PartiallyInlineLibCalls.h"
#include "llvm/Transforms/Scalar/PlaceSafepoints.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/Reg2Mem.h"
#include "llvm/Transforms/Scalar/RewriteStatepointsForGC.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/ScalarizeMaskedMemIntrin.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/Transforms/Scalar/SeparateConstOffsetFromGEP.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/Scalar/SpeculativeExecution.h"
#include "llvm/Transforms/Scalar/StraightLineStrengthReduce.h"
#include "llvm/Transforms/Scalar/StructurizeCFG.h"
#include "llvm/Transforms/Scalar/TLSVariableHoist.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Scalar/WarnMissedTransforms.h"
#include "llvm/Transforms/Utils/AddDiscriminators.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/BreakCriticalEdges.h"
#include "llvm/Transforms/Utils/CanonicalizeAliases.h"
#include "llvm/Transforms/Utils/CanonicalizeFreezeInLoops.h"
#include "llvm/Transforms/Utils/CountVisits.h"
#include "llvm/Transforms/Utils/DXILUpgrade.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include "llvm/Transforms/Utils/EntryExitInstrumenter.h"
#include "llvm/Transforms/Utils/FixIrreducible.h"
#include "llvm/Transforms/Utils/HelloWorld.h"
#include "llvm/Transforms/Utils/InjectTLIMappings.h"
#include "llvm/Transforms/Utils/InstructionNamer.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LibCallsShrinkWrap.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"
#include "llvm/Transforms/Utils/LowerGlobalDtors.h"
#include "llvm/Transforms/Utils/LowerIFunc.h"
#include "llvm/Transforms/Utils/LowerInvoke.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/MetaRenamer.h"
#include "llvm/Transforms/Utils/MoveAutoInit.h"
#include "llvm/Transforms/Utils/NameAnonGlobals.h"
#include "llvm/Transforms/Utils/PredicateInfo.h"
#include "llvm/Transforms/Utils/RelLookupTableConverter.h"
#include "llvm/Transforms/Utils/StripGCRelocates.h"
#include "llvm/Transforms/Utils/StripNonLineTableDebugInfo.h"
#include "llvm/Transforms/Utils/SymbolRewriter.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "llvm/Transforms/Utils/UnifyLoopExits.h"
#include "llvm/Transforms/Vectorize/LoadStoreVectorizer.h"
#include "llvm/Transforms/Vectorize/LoopIdiomVectorize.h"
#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "llvm/Transforms/Vectorize/SLPVectorizer.h"
#include "llvm/Transforms/Vectorize/VectorCombine.h"
#include <optional>

using namespace llvm;

static const Regex DefaultAliasRegex(
    "^(default|thinlto-pre-link|thinlto|lto-pre-link|lto)<(O[0123sz])>$");

namespace llvm {
cl::opt<bool> PrintPipelinePasses(
    "print-pipeline-passes",
    cl::desc("Print a '-passes' compatible string describing the pipeline "
             "(best-effort only)."));
} // namespace llvm

AnalysisKey NoOpModuleAnalysis::Key;
AnalysisKey NoOpCGSCCAnalysis::Key;
AnalysisKey NoOpFunctionAnalysis::Key;
AnalysisKey NoOpLoopAnalysis::Key;

namespace {

// Passes for testing crashes.
// DO NOT USE THIS EXCEPT FOR TESTING!
class TriggerCrashModulePass : public PassInfoMixin<TriggerCrashModulePass> {
public:
  PreservedAnalyses run(Module &, ModuleAnalysisManager &) {
    abort();
    return PreservedAnalyses::all();
  }
  static StringRef name() { return "TriggerCrashModulePass"; }
};

class TriggerCrashFunctionPass
    : public PassInfoMixin<TriggerCrashFunctionPass> {
public:
  PreservedAnalyses run(Function &, FunctionAnalysisManager &) {
    abort();
    return PreservedAnalyses::all();
  }
  static StringRef name() { return "TriggerCrashFunctionPass"; }
};

// A pass for testing message reporting of -verify-each failures.
// DO NOT USE THIS EXCEPT FOR TESTING!
class TriggerVerifierErrorPass
    : public PassInfoMixin<TriggerVerifierErrorPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    // Intentionally break the Module by creating an alias without setting the
    // aliasee.
    auto *PtrTy = llvm::PointerType::getUnqual(M.getContext());
    GlobalAlias::create(PtrTy, PtrTy->getAddressSpace(),
                        GlobalValue::LinkageTypes::InternalLinkage,
                        "__bad_alias", nullptr, &M);
    return PreservedAnalyses::none();
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    // Intentionally break the Function by inserting a terminator
    // instruction in the middle of a basic block.
    BasicBlock &BB = F.getEntryBlock();
    new UnreachableInst(F.getContext(), BB.getTerminator()->getIterator());
    return PreservedAnalyses::none();
  }

  PreservedAnalyses run(MachineFunction &MF, MachineFunctionAnalysisManager &) {
    // Intentionally create a virtual register and set NoVRegs property.
    auto &MRI = MF.getRegInfo();
    MRI.createGenericVirtualRegister(LLT::scalar(8));
    MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
    return PreservedAnalyses::all();
  }

  static StringRef name() { return "TriggerVerifierErrorPass"; }
};

// A pass requires all MachineFunctionProperties.
// DO NOT USE THIS EXCEPT FOR TESTING!
class RequireAllMachineFunctionPropertiesPass
    : public PassInfoMixin<RequireAllMachineFunctionPropertiesPass> {
public:
  PreservedAnalyses run(MachineFunction &MF, MachineFunctionAnalysisManager &) {
    MFPropsModifier _(*this, MF);
    return PreservedAnalyses::none();
  }

  static MachineFunctionProperties getRequiredProperties() {
    MachineFunctionProperties MFProps;
    MFProps.set(MachineFunctionProperties::Property::FailedISel);
    MFProps.set(MachineFunctionProperties::Property::FailsVerification);
    MFProps.set(MachineFunctionProperties::Property::IsSSA);
    MFProps.set(MachineFunctionProperties::Property::Legalized);
    MFProps.set(MachineFunctionProperties::Property::NoPHIs);
    MFProps.set(MachineFunctionProperties::Property::NoVRegs);
    MFProps.set(MachineFunctionProperties::Property::RegBankSelected);
    MFProps.set(MachineFunctionProperties::Property::Selected);
    MFProps.set(MachineFunctionProperties::Property::TiedOpsRewritten);
    MFProps.set(MachineFunctionProperties::Property::TracksDebugUserValues);
    MFProps.set(MachineFunctionProperties::Property::TracksLiveness);
    return MFProps;
  }
  static StringRef name() { return "RequireAllMachineFunctionPropertiesPass"; }
};

} // namespace

PassBuilder::PassBuilder(TargetMachine *TM, PipelineTuningOptions PTO,
                         std::optional<PGOOptions> PGOOpt,
                         PassInstrumentationCallbacks *PIC)
    : TM(TM), PTO(PTO), PGOOpt(PGOOpt), PIC(PIC) {
  if (TM)
    TM->registerPassBuilderCallbacks(*this);
  if (PIC) {
    PIC->registerClassToPassNameCallback([this, PIC]() {
      // MSVC requires this to be captured if it's used inside decltype.
      // Other compilers consider it an unused lambda capture.
      (void)this;
#define MODULE_PASS(NAME, CREATE_PASS)                                         \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define MODULE_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)      \
  PIC->addClassToPassName(CLASS, NAME);
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)    \
  PIC->addClassToPassName(CLASS, NAME);
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define LOOPNEST_PASS(NAME, CREATE_PASS)                                       \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  PIC->addClassToPassName(CLASS, NAME);
#define LOOP_ANALYSIS(NAME, CREATE_PASS)                                       \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define CGSCC_PASS(NAME, CREATE_PASS)                                          \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define CGSCC_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)       \
  PIC->addClassToPassName(CLASS, NAME);
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#include "PassRegistry.def"

#define MACHINE_FUNCTION_ANALYSIS(NAME, CREATE_PASS)                           \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#define MACHINE_FUNCTION_PASS(NAME, CREATE_PASS)                               \
  PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);
#include "llvm/Passes/MachinePassRegistry.def"
    });
  }
}

void PassBuilder::registerModuleAnalyses(ModuleAnalysisManager &MAM) {
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  MAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : ModuleAnalysisRegistrationCallbacks)
    C(MAM);
}

void PassBuilder::registerCGSCCAnalyses(CGSCCAnalysisManager &CGAM) {
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  CGAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : CGSCCAnalysisRegistrationCallbacks)
    C(CGAM);
}

void PassBuilder::registerFunctionAnalyses(FunctionAnalysisManager &FAM) {
  // We almost always want the default alias analysis pipeline.
  // If a user wants a different one, they can register their own before calling
  // registerFunctionAnalyses().
  FAM.registerPass([&] { return buildDefaultAAPipeline(); });

#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  FAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : FunctionAnalysisRegistrationCallbacks)
    C(FAM);
}

void PassBuilder::registerMachineFunctionAnalyses(
    MachineFunctionAnalysisManager &MFAM) {

#define MACHINE_FUNCTION_ANALYSIS(NAME, CREATE_PASS)                           \
  MFAM.registerPass([&] { return CREATE_PASS; });
#include "llvm/Passes/MachinePassRegistry.def"

  for (auto &C : MachineFunctionAnalysisRegistrationCallbacks)
    C(MFAM);
}

void PassBuilder::registerLoopAnalyses(LoopAnalysisManager &LAM) {
#define LOOP_ANALYSIS(NAME, CREATE_PASS)                                       \
  LAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : LoopAnalysisRegistrationCallbacks)
    C(LAM);
}

static std::optional<std::pair<bool, bool>>
parseFunctionPipelineName(StringRef Name) {
  std::pair<bool, bool> Params;
  if (!Name.consume_front("function"))
    return std::nullopt;
  if (Name.empty())
    return Params;
  if (!Name.consume_front("<") || !Name.consume_back(">"))
    return std::nullopt;
  while (!Name.empty()) {
    auto [Front, Back] = Name.split(';');
    Name = Back;
    if (Front == "eager-inv")
      Params.first = true;
    else if (Front == "no-rerun")
      Params.second = true;
    else
      return std::nullopt;
  }
  return Params;
}

static std::optional<int> parseDevirtPassName(StringRef Name) {
  if (!Name.consume_front("devirt<") || !Name.consume_back(">"))
    return std::nullopt;
  int Count;
  if (Name.getAsInteger(0, Count) || Count < 0)
    return std::nullopt;
  return Count;
}

static std::optional<OptimizationLevel> parseOptLevel(StringRef S) {
  return StringSwitch<std::optional<OptimizationLevel>>(S)
      .Case("O0", OptimizationLevel::O0)
      .Case("O1", OptimizationLevel::O1)
      .Case("O2", OptimizationLevel::O2)
      .Case("O3", OptimizationLevel::O3)
      .Case("Os", OptimizationLevel::Os)
      .Case("Oz", OptimizationLevel::Oz)
      .Default(std::nullopt);
}

Expected<bool> PassBuilder::parseSinglePassOption(StringRef Params,
                                                  StringRef OptionName,
                                                  StringRef PassName) {
  bool Result = false;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName == OptionName) {
      Result = true;
    } else {
      return make_error<StringError>(
          formatv("invalid {1} pass parameter '{0}' ", ParamName, PassName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

namespace {

/// Parser of parameters for HardwareLoops  pass.
Expected<HardwareLoopOptions> parseHardwareLoopOptions(StringRef Params) {
  HardwareLoopOptions HardwareLoopOpts;

  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');
    if (ParamName.consume_front("hardware-loop-decrement=")) {
      int Count;
      if (ParamName.getAsInteger(0, Count))
        return make_error<StringError>(
            formatv("invalid HardwareLoopPass parameter '{0}' ", ParamName).str(),
            inconvertibleErrorCode());
      HardwareLoopOpts.setDecrement(Count);
      continue;
    }
    if (ParamName.consume_front("hardware-loop-counter-bitwidth=")) {
      int Count;
      if (ParamName.getAsInteger(0, Count))
        return make_error<StringError>(
            formatv("invalid HardwareLoopPass parameter '{0}' ", ParamName).str(),
            inconvertibleErrorCode());
      HardwareLoopOpts.setCounterBitwidth(Count);
      continue;
    }
    if (ParamName == "force-hardware-loops") {
      HardwareLoopOpts.setForce(true);
    } else if (ParamName == "force-hardware-loop-phi") {
      HardwareLoopOpts.setForcePhi(true);
    } else if (ParamName == "force-nested-hardware-loop") {
      HardwareLoopOpts.setForceNested(true);
    } else if (ParamName == "force-hardware-loop-guard") {
      HardwareLoopOpts.setForceGuard(true);
    } else {
      return make_error<StringError>(
          formatv("invalid HardwarePass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return HardwareLoopOpts;
}

/// Parser of parameters for LoopUnroll pass.
Expected<LoopUnrollOptions> parseLoopUnrollOptions(StringRef Params) {
  LoopUnrollOptions UnrollOpts;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');
    std::optional<OptimizationLevel> OptLevel = parseOptLevel(ParamName);
    // Don't accept -Os/-Oz.
    if (OptLevel && !OptLevel->isOptimizingForSize()) {
      UnrollOpts.setOptLevel(OptLevel->getSpeedupLevel());
      continue;
    }
    if (ParamName.consume_front("full-unroll-max=")) {
      int Count;
      if (ParamName.getAsInteger(0, Count))
        return make_error<StringError>(
            formatv("invalid LoopUnrollPass parameter '{0}' ", ParamName).str(),
            inconvertibleErrorCode());
      UnrollOpts.setFullUnrollMaxCount(Count);
      continue;
    }

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "partial") {
      UnrollOpts.setPartial(Enable);
    } else if (ParamName == "peeling") {
      UnrollOpts.setPeeling(Enable);
    } else if (ParamName == "profile-peeling") {
      UnrollOpts.setProfileBasedPeeling(Enable);
    } else if (ParamName == "runtime") {
      UnrollOpts.setRuntime(Enable);
    } else if (ParamName == "upperbound") {
      UnrollOpts.setUpperBound(Enable);
    } else {
      return make_error<StringError>(
          formatv("invalid LoopUnrollPass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return UnrollOpts;
}

Expected<bool> parseGlobalDCEPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(
      Params, "vfe-linkage-unit-visibility", "GlobalDCE");
}

Expected<bool> parseCGProfilePassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "in-lto-post-link",
                                            "CGProfile");
}

Expected<bool> parseInlinerPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "only-mandatory",
                                            "InlinerPass");
}

Expected<bool> parseCoroSplitPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "reuse-storage",
                                            "CoroSplitPass");
}

Expected<bool> parsePostOrderFunctionAttrsPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(
      Params, "skip-non-recursive-function-attrs", "PostOrderFunctionAttrs");
}

Expected<CFGuardPass::Mechanism> parseCFGuardPassOptions(StringRef Params) {
  if (Params.empty())
    return CFGuardPass::Mechanism::Check;

  auto [Param, RHS] = Params.split(';');
  if (!RHS.empty())
    return make_error<StringError>(
        formatv("too many CFGuardPass parameters '{0}' ", Params).str(),
        inconvertibleErrorCode());

  if (Param == "check")
    return CFGuardPass::Mechanism::Check;
  if (Param == "dispatch")
    return CFGuardPass::Mechanism::Dispatch;

  return make_error<StringError>(
      formatv("invalid CFGuardPass mechanism: '{0}' ", Param).str(),
      inconvertibleErrorCode());
}

Expected<bool> parseEarlyCSEPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "memssa", "EarlyCSE");
}

Expected<bool> parseEntryExitInstrumenterPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "post-inline",
                                            "EntryExitInstrumenter");
}

Expected<bool> parseLoopExtractorPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "single", "LoopExtractor");
}

Expected<bool> parseLowerMatrixIntrinsicsPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "minimal",
                                            "LowerMatrixIntrinsics");
}

Expected<AddressSanitizerOptions> parseASanPassOptions(StringRef Params) {
  AddressSanitizerOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName == "kernel") {
      Result.CompileKernel = true;
    } else {
      return make_error<StringError>(
          formatv("invalid AddressSanitizer pass parameter '{0}' ", ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<HWAddressSanitizerOptions> parseHWASanPassOptions(StringRef Params) {
  HWAddressSanitizerOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName == "recover") {
      Result.Recover = true;
    } else if (ParamName == "kernel") {
      Result.CompileKernel = true;
    } else {
      return make_error<StringError>(
          formatv("invalid HWAddressSanitizer pass parameter '{0}' ", ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<EmbedBitcodeOptions> parseEmbedBitcodePassOptions(StringRef Params) {
  EmbedBitcodeOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName == "thinlto") {
      Result.IsThinLTO = true;
    } else if (ParamName == "emit-summary") {
      Result.EmitLTOSummary = true;
    } else {
      return make_error<StringError>(
          formatv("invalid EmbedBitcode pass parameter '{0}' ", ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<MemorySanitizerOptions> parseMSanPassOptions(StringRef Params) {
  MemorySanitizerOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName == "recover") {
      Result.Recover = true;
    } else if (ParamName == "kernel") {
      Result.Kernel = true;
    } else if (ParamName.consume_front("track-origins=")) {
      if (ParamName.getAsInteger(0, Result.TrackOrigins))
        return make_error<StringError>(
            formatv("invalid argument to MemorySanitizer pass track-origins "
                    "parameter: '{0}' ",
                    ParamName)
                .str(),
            inconvertibleErrorCode());
    } else if (ParamName == "eager-checks") {
      Result.EagerChecks = true;
    } else {
      return make_error<StringError>(
          formatv("invalid MemorySanitizer pass parameter '{0}' ", ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

/// Parser of parameters for SimplifyCFG pass.
Expected<SimplifyCFGOptions> parseSimplifyCFGOptions(StringRef Params) {
  SimplifyCFGOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "speculate-blocks") {
      Result.speculateBlocks(Enable);
    } else if (ParamName == "simplify-cond-branch") {
      Result.setSimplifyCondBranch(Enable);
    } else if (ParamName == "forward-switch-cond") {
      Result.forwardSwitchCondToPhi(Enable);
    } else if (ParamName == "switch-range-to-icmp") {
      Result.convertSwitchRangeToICmp(Enable);
    } else if (ParamName == "switch-to-lookup") {
      Result.convertSwitchToLookupTable(Enable);
    } else if (ParamName == "keep-loops") {
      Result.needCanonicalLoops(Enable);
    } else if (ParamName == "hoist-common-insts") {
      Result.hoistCommonInsts(Enable);
    } else if (ParamName == "sink-common-insts") {
      Result.sinkCommonInsts(Enable);
    } else if (ParamName == "speculate-unpredictables") {
      Result.speculateUnpredictables(Enable);
    } else if (Enable && ParamName.consume_front("bonus-inst-threshold=")) {
      APInt BonusInstThreshold;
      if (ParamName.getAsInteger(0, BonusInstThreshold))
        return make_error<StringError>(
            formatv("invalid argument to SimplifyCFG pass bonus-threshold "
                    "parameter: '{0}' ",
                    ParamName).str(),
            inconvertibleErrorCode());
      Result.bonusInstThreshold(BonusInstThreshold.getSExtValue());
    } else {
      return make_error<StringError>(
          formatv("invalid SimplifyCFG pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<InstCombineOptions> parseInstCombineOptions(StringRef Params) {
  InstCombineOptions Result;
  // When specifying "instcombine" in -passes enable fix-point verification by
  // default, as this is what most tests should use.
  Result.setVerifyFixpoint(true);
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "use-loop-info") {
      Result.setUseLoopInfo(Enable);
    } else if (ParamName == "verify-fixpoint") {
      Result.setVerifyFixpoint(Enable);
    } else if (Enable && ParamName.consume_front("max-iterations=")) {
      APInt MaxIterations;
      if (ParamName.getAsInteger(0, MaxIterations))
        return make_error<StringError>(
            formatv("invalid argument to InstCombine pass max-iterations "
                    "parameter: '{0}' ",
                    ParamName).str(),
            inconvertibleErrorCode());
      Result.setMaxIterations((unsigned)MaxIterations.getZExtValue());
    } else {
      return make_error<StringError>(
          formatv("invalid InstCombine pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

/// Parser of parameters for LoopVectorize pass.
Expected<LoopVectorizeOptions> parseLoopVectorizeOptions(StringRef Params) {
  LoopVectorizeOptions Opts;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "interleave-forced-only") {
      Opts.setInterleaveOnlyWhenForced(Enable);
    } else if (ParamName == "vectorize-forced-only") {
      Opts.setVectorizeOnlyWhenForced(Enable);
    } else {
      return make_error<StringError>(
          formatv("invalid LoopVectorize parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Opts;
}

Expected<std::pair<bool, bool>> parseLoopUnswitchOptions(StringRef Params) {
  std::pair<bool, bool> Result = {false, true};
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "nontrivial") {
      Result.first = Enable;
    } else if (ParamName == "trivial") {
      Result.second = Enable;
    } else {
      return make_error<StringError>(
          formatv("invalid LoopUnswitch pass parameter '{0}' ", ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<LICMOptions> parseLICMOptions(StringRef Params) {
  LICMOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "allowspeculation") {
      Result.AllowSpeculation = Enable;
    } else {
      return make_error<StringError>(
          formatv("invalid LICM pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<std::pair<bool, bool>> parseLoopRotateOptions(StringRef Params) {
  std::pair<bool, bool> Result = {true, false};
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "header-duplication") {
      Result.first = Enable;
    } else if (ParamName == "prepare-for-lto") {
      Result.second = Enable;
    } else {
      return make_error<StringError>(
          formatv("invalid LoopRotate pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<bool> parseMergedLoadStoreMotionOptions(StringRef Params) {
  bool Result = false;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "split-footer-bb") {
      Result = Enable;
    } else {
      return make_error<StringError>(
          formatv("invalid MergedLoadStoreMotion pass parameter '{0}' ",
                  ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<GVNOptions> parseGVNOptions(StringRef Params) {
  GVNOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "pre") {
      Result.setPRE(Enable);
    } else if (ParamName == "load-pre") {
      Result.setLoadPRE(Enable);
    } else if (ParamName == "split-backedge-load-pre") {
      Result.setLoadPRESplitBackedge(Enable);
    } else if (ParamName == "memdep") {
      Result.setMemDep(Enable);
    } else {
      return make_error<StringError>(
          formatv("invalid GVN pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<IPSCCPOptions> parseIPSCCPOptions(StringRef Params) {
  IPSCCPOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "func-spec")
      Result.setFuncSpec(Enable);
    else
      return make_error<StringError>(
          formatv("invalid IPSCCP pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
  }
  return Result;
}

Expected<SROAOptions> parseSROAOptions(StringRef Params) {
  if (Params.empty() || Params == "modify-cfg")
    return SROAOptions::ModifyCFG;
  if (Params == "preserve-cfg")
    return SROAOptions::PreserveCFG;
  return make_error<StringError>(
      formatv("invalid SROA pass parameter '{0}' (either preserve-cfg or "
              "modify-cfg can be specified)",
              Params)
          .str(),
      inconvertibleErrorCode());
}

Expected<StackLifetime::LivenessType>
parseStackLifetimeOptions(StringRef Params) {
  StackLifetime::LivenessType Result = StackLifetime::LivenessType::May;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName == "may") {
      Result = StackLifetime::LivenessType::May;
    } else if (ParamName == "must") {
      Result = StackLifetime::LivenessType::Must;
    } else {
      return make_error<StringError>(
          formatv("invalid StackLifetime parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<bool> parseDependenceAnalysisPrinterOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "normalized-results",
                                            "DependenceAnalysisPrinter");
}

Expected<bool> parseSeparateConstOffsetFromGEPPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "lower-gep",
                                            "SeparateConstOffsetFromGEP");
}

Expected<OptimizationLevel>
parseFunctionSimplificationPipelineOptions(StringRef Params) {
  std::optional<OptimizationLevel> L = parseOptLevel(Params);
  if (!L || *L == OptimizationLevel::O0) {
    return make_error<StringError>(
        formatv("invalid function-simplification parameter '{0}' ", Params)
            .str(),
        inconvertibleErrorCode());
  };
  return *L;
}

Expected<bool> parseMemorySSAPrinterPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "no-ensure-optimized-uses",
                                            "MemorySSAPrinterPass");
}

Expected<bool> parseSpeculativeExecutionPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "only-if-divergent-target",
                                            "SpeculativeExecutionPass");
}

Expected<std::string> parseMemProfUsePassOptions(StringRef Params) {
  std::string Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName.consume_front("profile-filename=")) {
      Result = ParamName.str();
    } else {
      return make_error<StringError>(
          formatv("invalid MemProfUse pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<bool> parseStructuralHashPrinterPassOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "detailed",
                                            "StructuralHashPrinterPass");
}

Expected<bool> parseWinEHPrepareOptions(StringRef Params) {
  return PassBuilder::parseSinglePassOption(Params, "demote-catchswitch-only",
                                            "WinEHPreparePass");
}

Expected<GlobalMergeOptions> parseGlobalMergeOptions(StringRef Params) {
  GlobalMergeOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "group-by-use")
      Result.GroupByUse = Enable;
    else if (ParamName == "ignore-single-use")
      Result.IgnoreSingleUse = Enable;
    else if (ParamName == "merge-const")
      Result.MergeConst = Enable;
    else if (ParamName == "merge-external")
      Result.MergeExternal = Enable;
    else if (ParamName.consume_front("max-offset=")) {
      if (ParamName.getAsInteger(0, Result.MaxOffset))
        return make_error<StringError>(
            formatv("invalid GlobalMergePass parameter '{0}' ", ParamName)
                .str(),
            inconvertibleErrorCode());
    }
  }
  return Result;
}

Expected<SmallVector<std::string, 0>> parseInternalizeGVs(StringRef Params) {
  SmallVector<std::string, 1> PreservedGVs;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName.consume_front("preserve-gv=")) {
      PreservedGVs.push_back(ParamName.str());
    } else {
      return make_error<StringError>(
          formatv("invalid Internalize pass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }

  return Expected<SmallVector<std::string, 0>>(std::move(PreservedGVs));
}

Expected<RegAllocFastPassOptions>
parseRegAllocFastPassOptions(PassBuilder &PB, StringRef Params) {
  RegAllocFastPassOptions Opts;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');

    if (ParamName.consume_front("filter=")) {
      std::optional<RegAllocFilterFunc> Filter =
          PB.parseRegAllocFilter(ParamName);
      if (!Filter) {
        return make_error<StringError>(
            formatv("invalid regallocfast register filter '{0}' ", ParamName)
                .str(),
            inconvertibleErrorCode());
      }
      Opts.Filter = *Filter;
      Opts.FilterName = ParamName;
      continue;
    }

    if (ParamName == "no-clear-vregs") {
      Opts.ClearVRegs = false;
      continue;
    }

    return make_error<StringError>(
        formatv("invalid regallocfast pass parameter '{0}' ", ParamName).str(),
        inconvertibleErrorCode());
  }
  return Opts;
}

} // namespace

/// Tests whether a pass name starts with a valid prefix for a default pipeline
/// alias.
static bool startsWithDefaultPipelineAliasPrefix(StringRef Name) {
  return Name.starts_with("default") || Name.starts_with("thinlto") ||
         Name.starts_with("lto");
}

/// Tests whether registered callbacks will accept a given pass name.
///
/// When parsing a pipeline text, the type of the outermost pipeline may be
/// omitted, in which case the type is automatically determined from the first
/// pass name in the text. This may be a name that is handled through one of the
/// callbacks. We check this through the oridinary parsing callbacks by setting
/// up a dummy PassManager in order to not force the client to also handle this
/// type of query.
template <typename PassManagerT, typename CallbacksT>
static bool callbacksAcceptPassName(StringRef Name, CallbacksT &Callbacks) {
  if (!Callbacks.empty()) {
    PassManagerT DummyPM;
    for (auto &CB : Callbacks)
      if (CB(Name, DummyPM, {}))
        return true;
  }
  return false;
}

template <typename CallbacksT>
static bool isModulePassName(StringRef Name, CallbacksT &Callbacks) {
  // Manually handle aliases for pre-configured pipeline fragments.
  if (startsWithDefaultPipelineAliasPrefix(Name))
    return DefaultAliasRegex.match(Name);

  StringRef NameNoBracket = Name.take_until([](char C) { return C == '<'; });

  // Explicitly handle pass manager names.
  if (Name == "module")
    return true;
  if (Name == "cgscc")
    return true;
  if (NameNoBracket == "function")
    return true;
  if (Name == "coro-cond")
    return true;

#define MODULE_PASS(NAME, CREATE_PASS)                                         \
  if (Name == NAME)                                                            \
    return true;
#define MODULE_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)      \
  if (PassBuilder::checkParametrizedPassName(Name, NAME))                      \
    return true;
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<ModulePassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isCGSCCPassName(StringRef Name, CallbacksT &Callbacks) {
  // Explicitly handle pass manager names.
  StringRef NameNoBracket = Name.take_until([](char C) { return C == '<'; });
  if (Name == "cgscc")
    return true;
  if (NameNoBracket == "function")
    return true;

  // Explicitly handle custom-parsed pass names.
  if (parseDevirtPassName(Name))
    return true;

#define CGSCC_PASS(NAME, CREATE_PASS)                                          \
  if (Name == NAME)                                                            \
    return true;
#define CGSCC_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)       \
  if (PassBuilder::checkParametrizedPassName(Name, NAME))                      \
    return true;
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<CGSCCPassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isFunctionPassName(StringRef Name, CallbacksT &Callbacks) {
  // Explicitly handle pass manager names.
  StringRef NameNoBracket = Name.take_until([](char C) { return C == '<'; });
  if (NameNoBracket == "function")
    return true;
  if (Name == "loop" || Name == "loop-mssa" || Name == "machine-function")
    return true;

#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME)                                                            \
    return true;
#define FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)    \
  if (PassBuilder::checkParametrizedPassName(Name, NAME))                      \
    return true;
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<FunctionPassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isMachineFunctionPassName(StringRef Name, CallbacksT &Callbacks) {
  // Explicitly handle pass manager names.
  if (Name == "machine-function")
    return true;

#define MACHINE_FUNCTION_PASS(NAME, CREATE_PASS)                               \
  if (Name == NAME)                                                            \
    return true;
#define MACHINE_FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER,    \
                                          PARAMS)                              \
  if (PassBuilder::checkParametrizedPassName(Name, NAME))                      \
    return true;

#define MACHINE_FUNCTION_ANALYSIS(NAME, CREATE_PASS)                           \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;

#include "llvm/Passes/MachinePassRegistry.def"

  return callbacksAcceptPassName<MachineFunctionPassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isLoopNestPassName(StringRef Name, CallbacksT &Callbacks,
                               bool &UseMemorySSA) {
  UseMemorySSA = false;

  if (PassBuilder::checkParametrizedPassName(Name, "lnicm")) {
    UseMemorySSA = true;
    return true;
  }

#define LOOPNEST_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME)                                                            \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<LoopPassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isLoopPassName(StringRef Name, CallbacksT &Callbacks,
                           bool &UseMemorySSA) {
  UseMemorySSA = false;

  if (PassBuilder::checkParametrizedPassName(Name, "licm")) {
    UseMemorySSA = true;
    return true;
  }

#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  if (Name == NAME)                                                            \
    return true;
#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  if (PassBuilder::checkParametrizedPassName(Name, NAME))                      \
    return true;
#define LOOP_ANALYSIS(NAME, CREATE_PASS)                                       \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<LoopPassManager>(Name, Callbacks);
}

std::optional<std::vector<PassBuilder::PipelineElement>>
PassBuilder::parsePipelineText(StringRef Text) {
  std::vector<PipelineElement> ResultPipeline;

  SmallVector<std::vector<PipelineElement> *, 4> PipelineStack = {
      &ResultPipeline};
  for (;;) {
    std::vector<PipelineElement> &Pipeline = *PipelineStack.back();
    size_t Pos = Text.find_first_of(",()");
    Pipeline.push_back({Text.substr(0, Pos), {}});

    // If we have a single terminating name, we're done.
    if (Pos == Text.npos)
      break;

    char Sep = Text[Pos];
    Text = Text.substr(Pos + 1);
    if (Sep == ',')
      // Just a name ending in a comma, continue.
      continue;

    if (Sep == '(') {
      // Push the inner pipeline onto the stack to continue processing.
      PipelineStack.push_back(&Pipeline.back().InnerPipeline);
      continue;
    }

    assert(Sep == ')' && "Bogus separator!");
    // When handling the close parenthesis, we greedily consume them to avoid
    // empty strings in the pipeline.
    do {
      // If we try to pop the outer pipeline we have unbalanced parentheses.
      if (PipelineStack.size() == 1)
        return std::nullopt;

      PipelineStack.pop_back();
    } while (Text.consume_front(")"));

    // Check if we've finished parsing.
    if (Text.empty())
      break;

    // Otherwise, the end of an inner pipeline always has to be followed by
    // a comma, and then we can continue.
    if (!Text.consume_front(","))
      return std::nullopt;
  }

  if (PipelineStack.size() > 1)
    // Unbalanced paretheses.
    return std::nullopt;

  assert(PipelineStack.back() == &ResultPipeline &&
         "Wrong pipeline at the bottom of the stack!");
  return {std::move(ResultPipeline)};
}

Error PassBuilder::parseModulePass(ModulePassManager &MPM,
                                   const PipelineElement &E) {
  auto &Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "module") {
      ModulePassManager NestedMPM;
      if (auto Err = parseModulePassPipeline(NestedMPM, InnerPipeline))
        return Err;
      MPM.addPass(std::move(NestedMPM));
      return Error::success();
    }
    if (Name == "coro-cond") {
      ModulePassManager NestedMPM;
      if (auto Err = parseModulePassPipeline(NestedMPM, InnerPipeline))
        return Err;
      MPM.addPass(CoroConditionalWrapper(std::move(NestedMPM)));
      return Error::success();
    }
    if (Name == "cgscc") {
      CGSCCPassManager CGPM;
      if (auto Err = parseCGSCCPassPipeline(CGPM, InnerPipeline))
        return Err;
      MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));
      return Error::success();
    }
    if (auto Params = parseFunctionPipelineName(Name)) {
      if (Params->second)
        return make_error<StringError>(
            "cannot have a no-rerun module to function adaptor",
            inconvertibleErrorCode());
      FunctionPassManager FPM;
      if (auto Err = parseFunctionPassPipeline(FPM, InnerPipeline))
        return Err;
      MPM.addPass(
          createModuleToFunctionPassAdaptor(std::move(FPM), Params->first));
      return Error::success();
    }

    for (auto &C : ModulePipelineParsingCallbacks)
      if (C(Name, MPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as module pipeline", Name).str(),
        inconvertibleErrorCode());
    ;
  }

  // Manually handle aliases for pre-configured pipeline fragments.
  if (startsWithDefaultPipelineAliasPrefix(Name)) {
    SmallVector<StringRef, 3> Matches;
    if (!DefaultAliasRegex.match(Name, &Matches))
      return make_error<StringError>(
          formatv("unknown default pipeline alias '{0}'", Name).str(),
          inconvertibleErrorCode());

    assert(Matches.size() == 3 && "Must capture two matched strings!");

    OptimizationLevel L = *parseOptLevel(Matches[2]);

    // This is consistent with old pass manager invoked via opt, but
    // inconsistent with clang. Clang doesn't enable loop vectorization
    // but does enable slp vectorization at Oz.
    PTO.LoopVectorization =
        L.getSpeedupLevel() > 1 && L != OptimizationLevel::Oz;
    PTO.SLPVectorization =
        L.getSpeedupLevel() > 1 && L != OptimizationLevel::Oz;

    if (Matches[1] == "default") {
      MPM.addPass(buildPerModuleDefaultPipeline(L));
    } else if (Matches[1] == "thinlto-pre-link") {
      MPM.addPass(buildThinLTOPreLinkDefaultPipeline(L));
    } else if (Matches[1] == "thinlto") {
      MPM.addPass(buildThinLTODefaultPipeline(L, nullptr));
    } else if (Matches[1] == "lto-pre-link") {
      if (PTO.UnifiedLTO)
        // When UnifiedLTO is enabled, use the ThinLTO pre-link pipeline. This
        // avoids compile-time performance regressions and keeps the pre-link
        // LTO pipeline "unified" for both LTO modes.
        MPM.addPass(buildThinLTOPreLinkDefaultPipeline(L));
      else
        MPM.addPass(buildLTOPreLinkDefaultPipeline(L));
    } else {
      assert(Matches[1] == "lto" && "Not one of the matched options!");
      MPM.addPass(buildLTODefaultPipeline(L, nullptr));
    }
    return Error::success();
  }

  // Finally expand the basic registered passes from the .inc file.
#define MODULE_PASS(NAME, CREATE_PASS)                                         \
  if (Name == NAME) {                                                          \
    MPM.addPass(CREATE_PASS);                                                  \
    return Error::success();                                                   \
  }
#define MODULE_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)      \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    MPM.addPass(CREATE_PASS(Params.get()));                                    \
    return Error::success();                                                   \
  }
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  if (Name == "require<" NAME ">") {                                           \
    MPM.addPass(                                                               \
        RequireAnalysisPass<                                                   \
            std::remove_reference_t<decltype(CREATE_PASS)>, Module>());        \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    MPM.addPass(InvalidateAnalysisPass<                                        \
                std::remove_reference_t<decltype(CREATE_PASS)>>());            \
    return Error::success();                                                   \
  }
#define CGSCC_PASS(NAME, CREATE_PASS)                                          \
  if (Name == NAME) {                                                          \
    MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(CREATE_PASS));         \
    return Error::success();                                                   \
  }
#define CGSCC_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)       \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    MPM.addPass(                                                               \
        createModuleToPostOrderCGSCCPassAdaptor(CREATE_PASS(Params.get())));   \
    return Error::success();                                                   \
  }
#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    MPM.addPass(createModuleToFunctionPassAdaptor(CREATE_PASS));               \
    return Error::success();                                                   \
  }
#define FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)    \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    MPM.addPass(createModuleToFunctionPassAdaptor(CREATE_PASS(Params.get()))); \
    return Error::success();                                                   \
  }
#define LOOPNEST_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    MPM.addPass(createModuleToFunctionPassAdaptor(                             \
        createFunctionToLoopPassAdaptor(CREATE_PASS, false, false)));          \
    return Error::success();                                                   \
  }
#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  if (Name == NAME) {                                                          \
    MPM.addPass(createModuleToFunctionPassAdaptor(                             \
        createFunctionToLoopPassAdaptor(CREATE_PASS, false, false)));          \
    return Error::success();                                                   \
  }
#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    MPM.addPass(                                                               \
        createModuleToFunctionPassAdaptor(createFunctionToLoopPassAdaptor(     \
            CREATE_PASS(Params.get()), false, false)));                        \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : ModulePipelineParsingCallbacks)
    if (C(Name, MPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(
      formatv("unknown module pass '{0}'", Name).str(),
      inconvertibleErrorCode());
}

Error PassBuilder::parseCGSCCPass(CGSCCPassManager &CGPM,
                                  const PipelineElement &E) {
  auto &Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "cgscc") {
      CGSCCPassManager NestedCGPM;
      if (auto Err = parseCGSCCPassPipeline(NestedCGPM, InnerPipeline))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      CGPM.addPass(std::move(NestedCGPM));
      return Error::success();
    }
    if (auto Params = parseFunctionPipelineName(Name)) {
      FunctionPassManager FPM;
      if (auto Err = parseFunctionPassPipeline(FPM, InnerPipeline))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      CGPM.addPass(createCGSCCToFunctionPassAdaptor(
          std::move(FPM), Params->first, Params->second));
      return Error::success();
    }
    if (auto MaxRepetitions = parseDevirtPassName(Name)) {
      CGSCCPassManager NestedCGPM;
      if (auto Err = parseCGSCCPassPipeline(NestedCGPM, InnerPipeline))
        return Err;
      CGPM.addPass(
          createDevirtSCCRepeatedPass(std::move(NestedCGPM), *MaxRepetitions));
      return Error::success();
    }

    for (auto &C : CGSCCPipelineParsingCallbacks)
      if (C(Name, CGPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as cgscc pipeline", Name).str(),
        inconvertibleErrorCode());
  }

// Now expand the basic registered passes from the .inc file.
#define CGSCC_PASS(NAME, CREATE_PASS)                                          \
  if (Name == NAME) {                                                          \
    CGPM.addPass(CREATE_PASS);                                                 \
    return Error::success();                                                   \
  }
#define CGSCC_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)       \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    CGPM.addPass(CREATE_PASS(Params.get()));                                   \
    return Error::success();                                                   \
  }
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  if (Name == "require<" NAME ">") {                                           \
    CGPM.addPass(RequireAnalysisPass<                                          \
                 std::remove_reference_t<decltype(CREATE_PASS)>,               \
                 LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,    \
                 CGSCCUpdateResult &>());                                      \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    CGPM.addPass(InvalidateAnalysisPass<                                       \
                 std::remove_reference_t<decltype(CREATE_PASS)>>());           \
    return Error::success();                                                   \
  }
#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    CGPM.addPass(createCGSCCToFunctionPassAdaptor(CREATE_PASS));               \
    return Error::success();                                                   \
  }
#define FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)    \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    CGPM.addPass(createCGSCCToFunctionPassAdaptor(CREATE_PASS(Params.get()))); \
    return Error::success();                                                   \
  }
#define LOOPNEST_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    CGPM.addPass(createCGSCCToFunctionPassAdaptor(                             \
        createFunctionToLoopPassAdaptor(CREATE_PASS, false, false)));          \
    return Error::success();                                                   \
  }
#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  if (Name == NAME) {                                                          \
    CGPM.addPass(createCGSCCToFunctionPassAdaptor(                             \
        createFunctionToLoopPassAdaptor(CREATE_PASS, false, false)));          \
    return Error::success();                                                   \
  }
#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    CGPM.addPass(                                                              \
        createCGSCCToFunctionPassAdaptor(createFunctionToLoopPassAdaptor(      \
            CREATE_PASS(Params.get()), false, false)));                        \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : CGSCCPipelineParsingCallbacks)
    if (C(Name, CGPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(
      formatv("unknown cgscc pass '{0}'", Name).str(),
      inconvertibleErrorCode());
}

Error PassBuilder::parseFunctionPass(FunctionPassManager &FPM,
                                     const PipelineElement &E) {
  auto &Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "function") {
      FunctionPassManager NestedFPM;
      if (auto Err = parseFunctionPassPipeline(NestedFPM, InnerPipeline))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      FPM.addPass(std::move(NestedFPM));
      return Error::success();
    }
    if (Name == "loop" || Name == "loop-mssa") {
      LoopPassManager LPM;
      if (auto Err = parseLoopPassPipeline(LPM, InnerPipeline))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      bool UseMemorySSA = (Name == "loop-mssa");
      bool UseBFI = llvm::any_of(InnerPipeline, [](auto Pipeline) {
        return Pipeline.Name.contains("simple-loop-unswitch");
      });
      bool UseBPI = llvm::any_of(InnerPipeline, [](auto Pipeline) {
        return Pipeline.Name == "loop-predication";
      });
      FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM), UseMemorySSA,
                                                  UseBFI, UseBPI));
      return Error::success();
    }
    if (Name == "machine-function") {
      MachineFunctionPassManager MFPM;
      if (auto Err = parseMachinePassPipeline(MFPM, InnerPipeline))
        return Err;
      FPM.addPass(createFunctionToMachineFunctionPassAdaptor(std::move(MFPM)));
      return Error::success();
    }

    for (auto &C : FunctionPipelineParsingCallbacks)
      if (C(Name, FPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as function pipeline", Name).str(),
        inconvertibleErrorCode());
  }

// Now expand the basic registered passes from the .inc file.
#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    FPM.addPass(CREATE_PASS);                                                  \
    return Error::success();                                                   \
  }
#define FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)    \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    FPM.addPass(CREATE_PASS(Params.get()));                                    \
    return Error::success();                                                   \
  }
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  if (Name == "require<" NAME ">") {                                           \
    FPM.addPass(                                                               \
        RequireAnalysisPass<                                                   \
            std::remove_reference_t<decltype(CREATE_PASS)>, Function>());      \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    FPM.addPass(InvalidateAnalysisPass<                                        \
                std::remove_reference_t<decltype(CREATE_PASS)>>());            \
    return Error::success();                                                   \
  }
// FIXME: UseMemorySSA is set to false. Maybe we could do things like:
//        bool UseMemorySSA = !("canon-freeze" || "loop-predication" ||
//                              "guard-widening");
//        The risk is that it may become obsolete if we're not careful.
#define LOOPNEST_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    FPM.addPass(createFunctionToLoopPassAdaptor(CREATE_PASS, false, false));   \
    return Error::success();                                                   \
  }
#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  if (Name == NAME) {                                                          \
    FPM.addPass(createFunctionToLoopPassAdaptor(CREATE_PASS, false, false));   \
    return Error::success();                                                   \
  }
#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    FPM.addPass(createFunctionToLoopPassAdaptor(CREATE_PASS(Params.get()),     \
                                                false, false));                \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : FunctionPipelineParsingCallbacks)
    if (C(Name, FPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(
      formatv("unknown function pass '{0}'", Name).str(),
      inconvertibleErrorCode());
}

Error PassBuilder::parseLoopPass(LoopPassManager &LPM,
                                 const PipelineElement &E) {
  StringRef Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "loop") {
      LoopPassManager NestedLPM;
      if (auto Err = parseLoopPassPipeline(NestedLPM, InnerPipeline))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      LPM.addPass(std::move(NestedLPM));
      return Error::success();
    }

    for (auto &C : LoopPipelineParsingCallbacks)
      if (C(Name, LPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as loop pipeline", Name).str(),
        inconvertibleErrorCode());
  }

// Now expand the basic registered passes from the .inc file.
#define LOOPNEST_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    LPM.addPass(CREATE_PASS);                                                  \
    return Error::success();                                                   \
  }
#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  if (Name == NAME) {                                                          \
    LPM.addPass(CREATE_PASS);                                                  \
    return Error::success();                                                   \
  }
#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    LPM.addPass(CREATE_PASS(Params.get()));                                    \
    return Error::success();                                                   \
  }
#define LOOP_ANALYSIS(NAME, CREATE_PASS)                                       \
  if (Name == "require<" NAME ">") {                                           \
    LPM.addPass(RequireAnalysisPass<                                           \
                std::remove_reference_t<decltype(CREATE_PASS)>, Loop,          \
                LoopAnalysisManager, LoopStandardAnalysisResults &,            \
                LPMUpdater &>());                                              \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    LPM.addPass(InvalidateAnalysisPass<                                        \
                std::remove_reference_t<decltype(CREATE_PASS)>>());            \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : LoopPipelineParsingCallbacks)
    if (C(Name, LPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(formatv("unknown loop pass '{0}'", Name).str(),
                                 inconvertibleErrorCode());
}

Error PassBuilder::parseMachinePass(MachineFunctionPassManager &MFPM,
                                    const PipelineElement &E) {
  StringRef Name = E.Name;
  if (!E.InnerPipeline.empty())
    return make_error<StringError>("invalid pipeline",
                                   inconvertibleErrorCode());

#define MACHINE_MODULE_PASS(NAME, CREATE_PASS)                                 \
  if (Name == NAME) {                                                          \
    MFPM.addPass(CREATE_PASS);                                                 \
    return Error::success();                                                   \
  }
#define MACHINE_FUNCTION_PASS(NAME, CREATE_PASS)                               \
  if (Name == NAME) {                                                          \
    MFPM.addPass(CREATE_PASS);                                                 \
    return Error::success();                                                   \
  }
#define MACHINE_FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER,    \
                                          PARAMS)                              \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    MFPM.addPass(CREATE_PASS(Params.get()));                                   \
    return Error::success();                                                   \
  }
#define MACHINE_FUNCTION_ANALYSIS(NAME, CREATE_PASS)                           \
  if (Name == "require<" NAME ">") {                                           \
    MFPM.addPass(                                                              \
        RequireAnalysisPass<std::remove_reference_t<decltype(CREATE_PASS)>,    \
                            MachineFunction>());                               \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    MFPM.addPass(InvalidateAnalysisPass<                                       \
                 std::remove_reference_t<decltype(CREATE_PASS)>>());           \
    return Error::success();                                                   \
  }
#include "llvm/Passes/MachinePassRegistry.def"

  for (auto &C : MachineFunctionPipelineParsingCallbacks)
    if (C(Name, MFPM, E.InnerPipeline))
      return Error::success();
  return make_error<StringError>(
      formatv("unknown machine pass '{0}'", Name).str(),
      inconvertibleErrorCode());
}

bool PassBuilder::parseAAPassName(AAManager &AA, StringRef Name) {
#define MODULE_ALIAS_ANALYSIS(NAME, CREATE_PASS)                               \
  if (Name == NAME) {                                                          \
    AA.registerModuleAnalysis<                                                 \
        std::remove_reference_t<decltype(CREATE_PASS)>>();                     \
    return true;                                                               \
  }
#define FUNCTION_ALIAS_ANALYSIS(NAME, CREATE_PASS)                             \
  if (Name == NAME) {                                                          \
    AA.registerFunctionAnalysis<                                               \
        std::remove_reference_t<decltype(CREATE_PASS)>>();                     \
    return true;                                                               \
  }
#include "PassRegistry.def"

  for (auto &C : AAParsingCallbacks)
    if (C(Name, AA))
      return true;
  return false;
}

Error PassBuilder::parseMachinePassPipeline(
    MachineFunctionPassManager &MFPM, ArrayRef<PipelineElement> Pipeline) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseMachinePass(MFPM, Element))
      return Err;
  }
  return Error::success();
}

Error PassBuilder::parseLoopPassPipeline(LoopPassManager &LPM,
                                         ArrayRef<PipelineElement> Pipeline) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseLoopPass(LPM, Element))
      return Err;
  }
  return Error::success();
}

Error PassBuilder::parseFunctionPassPipeline(
    FunctionPassManager &FPM, ArrayRef<PipelineElement> Pipeline) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseFunctionPass(FPM, Element))
      return Err;
  }
  return Error::success();
}

Error PassBuilder::parseCGSCCPassPipeline(CGSCCPassManager &CGPM,
                                          ArrayRef<PipelineElement> Pipeline) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseCGSCCPass(CGPM, Element))
      return Err;
  }
  return Error::success();
}

void PassBuilder::crossRegisterProxies(LoopAnalysisManager &LAM,
                                       FunctionAnalysisManager &FAM,
                                       CGSCCAnalysisManager &CGAM,
                                       ModuleAnalysisManager &MAM,
                                       MachineFunctionAnalysisManager *MFAM) {
  MAM.registerPass([&] { return FunctionAnalysisManagerModuleProxy(FAM); });
  MAM.registerPass([&] { return CGSCCAnalysisManagerModuleProxy(CGAM); });
  CGAM.registerPass([&] { return ModuleAnalysisManagerCGSCCProxy(MAM); });
  FAM.registerPass([&] { return CGSCCAnalysisManagerFunctionProxy(CGAM); });
  FAM.registerPass([&] { return ModuleAnalysisManagerFunctionProxy(MAM); });
  FAM.registerPass([&] { return LoopAnalysisManagerFunctionProxy(LAM); });
  LAM.registerPass([&] { return FunctionAnalysisManagerLoopProxy(FAM); });
  if (MFAM) {
    MAM.registerPass(
        [&] { return MachineFunctionAnalysisManagerModuleProxy(*MFAM); });
    FAM.registerPass(
        [&] { return MachineFunctionAnalysisManagerFunctionProxy(*MFAM); });
    MFAM->registerPass(
        [&] { return ModuleAnalysisManagerMachineFunctionProxy(MAM); });
    MFAM->registerPass(
        [&] { return FunctionAnalysisManagerMachineFunctionProxy(FAM); });
  }
}

Error PassBuilder::parseModulePassPipeline(ModulePassManager &MPM,
                                           ArrayRef<PipelineElement> Pipeline) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseModulePass(MPM, Element))
      return Err;
  }
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c ModulePassManager
// FIXME: Should this routine accept a TargetMachine or require the caller to
// pre-populate the analysis managers with target-specific stuff?
Error PassBuilder::parsePassPipeline(ModulePassManager &MPM,
                                     StringRef PipelineText) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  // If the first name isn't at the module layer, wrap the pipeline up
  // automatically.
  StringRef FirstName = Pipeline->front().Name;

  if (!isModulePassName(FirstName, ModulePipelineParsingCallbacks)) {
    bool UseMemorySSA;
    if (isCGSCCPassName(FirstName, CGSCCPipelineParsingCallbacks)) {
      Pipeline = {{"cgscc", std::move(*Pipeline)}};
    } else if (isFunctionPassName(FirstName,
                                  FunctionPipelineParsingCallbacks)) {
      Pipeline = {{"function", std::move(*Pipeline)}};
    } else if (isLoopNestPassName(FirstName, LoopPipelineParsingCallbacks,
                                  UseMemorySSA)) {
      Pipeline = {{"function", {{UseMemorySSA ? "loop-mssa" : "loop",
                                 std::move(*Pipeline)}}}};
    } else if (isLoopPassName(FirstName, LoopPipelineParsingCallbacks,
                              UseMemorySSA)) {
      Pipeline = {{"function", {{UseMemorySSA ? "loop-mssa" : "loop",
                                 std::move(*Pipeline)}}}};
    } else if (isMachineFunctionPassName(
                   FirstName, MachineFunctionPipelineParsingCallbacks)) {
      Pipeline = {{"function", {{"machine-function", std::move(*Pipeline)}}}};
    } else {
      for (auto &C : TopLevelPipelineParsingCallbacks)
        if (C(MPM, *Pipeline))
          return Error::success();

      // Unknown pass or pipeline name!
      auto &InnerPipeline = Pipeline->front().InnerPipeline;
      return make_error<StringError>(
          formatv("unknown {0} name '{1}'",
                  (InnerPipeline.empty() ? "pass" : "pipeline"), FirstName)
              .str(),
          inconvertibleErrorCode());
    }
  }

  if (auto Err = parseModulePassPipeline(MPM, *Pipeline))
    return Err;
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c CGSCCPassManager
Error PassBuilder::parsePassPipeline(CGSCCPassManager &CGPM,
                                     StringRef PipelineText) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  StringRef FirstName = Pipeline->front().Name;
  if (!isCGSCCPassName(FirstName, CGSCCPipelineParsingCallbacks))
    return make_error<StringError>(
        formatv("unknown cgscc pass '{0}' in pipeline '{1}'", FirstName,
                PipelineText)
            .str(),
        inconvertibleErrorCode());

  if (auto Err = parseCGSCCPassPipeline(CGPM, *Pipeline))
    return Err;
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c
// FunctionPassManager
Error PassBuilder::parsePassPipeline(FunctionPassManager &FPM,
                                     StringRef PipelineText) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  StringRef FirstName = Pipeline->front().Name;
  if (!isFunctionPassName(FirstName, FunctionPipelineParsingCallbacks))
    return make_error<StringError>(
        formatv("unknown function pass '{0}' in pipeline '{1}'", FirstName,
                PipelineText)
            .str(),
        inconvertibleErrorCode());

  if (auto Err = parseFunctionPassPipeline(FPM, *Pipeline))
    return Err;
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c LoopPassManager
Error PassBuilder::parsePassPipeline(LoopPassManager &CGPM,
                                     StringRef PipelineText) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  if (auto Err = parseLoopPassPipeline(CGPM, *Pipeline))
    return Err;

  return Error::success();
}

Error PassBuilder::parsePassPipeline(MachineFunctionPassManager &MFPM,
                                     StringRef PipelineText) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid machine pass pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  if (auto Err = parseMachinePassPipeline(MFPM, *Pipeline))
    return Err;

  return Error::success();
}

Error PassBuilder::parseAAPipeline(AAManager &AA, StringRef PipelineText) {
  // If the pipeline just consists of the word 'default' just replace the AA
  // manager with our default one.
  if (PipelineText == "default") {
    AA = buildDefaultAAPipeline();
    return Error::success();
  }

  while (!PipelineText.empty()) {
    StringRef Name;
    std::tie(Name, PipelineText) = PipelineText.split(',');
    if (!parseAAPassName(AA, Name))
      return make_error<StringError>(
          formatv("unknown alias analysis name '{0}'", Name).str(),
          inconvertibleErrorCode());
  }

  return Error::success();
}

std::optional<RegAllocFilterFunc>
PassBuilder::parseRegAllocFilter(StringRef FilterName) {
  if (FilterName == "all")
    return nullptr;
  for (auto &C : RegClassFilterParsingCallbacks)
    if (auto F = C(FilterName))
      return F;
  return std::nullopt;
}

static void printPassName(StringRef PassName, raw_ostream &OS) {
  OS << "  " << PassName << "\n";
}
static void printPassName(StringRef PassName, StringRef Params,
                          raw_ostream &OS) {
  OS << "  " << PassName << "<" << Params << ">\n";
}

void PassBuilder::printPassNames(raw_ostream &OS) {
  // TODO: print pass descriptions when they are available

  OS << "Module passes:\n";
#define MODULE_PASS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Module passes with params:\n";
#define MODULE_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)      \
  printPassName(NAME, PARAMS, OS);
#include "PassRegistry.def"

  OS << "Module analyses:\n";
#define MODULE_ANALYSIS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Module alias analyses:\n";
#define MODULE_ALIAS_ANALYSIS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "CGSCC passes:\n";
#define CGSCC_PASS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "CGSCC passes with params:\n";
#define CGSCC_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)       \
  printPassName(NAME, PARAMS, OS);
#include "PassRegistry.def"

  OS << "CGSCC analyses:\n";
#define CGSCC_ANALYSIS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Function passes:\n";
#define FUNCTION_PASS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Function passes with params:\n";
#define FUNCTION_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)    \
  printPassName(NAME, PARAMS, OS);
#include "PassRegistry.def"

  OS << "Function analyses:\n";
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Function alias analyses:\n";
#define FUNCTION_ALIAS_ANALYSIS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "LoopNest passes:\n";
#define LOOPNEST_PASS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Loop passes:\n";
#define LOOP_PASS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Loop passes with params:\n";
#define LOOP_PASS_WITH_PARAMS(NAME, CLASS, CREATE_PASS, PARSER, PARAMS)        \
  printPassName(NAME, PARAMS, OS);
#include "PassRegistry.def"

  OS << "Loop analyses:\n";
#define LOOP_ANALYSIS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "PassRegistry.def"

  OS << "Machine module passes (WIP):\n";
#define MACHINE_MODULE_PASS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "llvm/Passes/MachinePassRegistry.def"

  OS << "Machine function passes (WIP):\n";
#define MACHINE_FUNCTION_PASS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "llvm/Passes/MachinePassRegistry.def"

  OS << "Machine function analyses (WIP):\n";
#define MACHINE_FUNCTION_ANALYSIS(NAME, CREATE_PASS) printPassName(NAME, OS);
#include "llvm/Passes/MachinePassRegistry.def"
}

void PassBuilder::registerParseTopLevelPipelineCallback(
    const std::function<bool(ModulePassManager &, ArrayRef<PipelineElement>)>
        &C) {
  TopLevelPipelineParsingCallbacks.push_back(C);
}
