//===- llvm/InitializePasses.h - Initialize All Passes ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for the pass initialization routines
// for the entire LLVM project.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_INITIALIZEPASSES_H
#define LLVM_INITIALIZEPASSES_H

namespace llvm {

class PassRegistry;

/// Initialize all passes linked into the Core library.
void initializeCore(PassRegistry&);

/// Initialize all passes linked into the TransformUtils library.
void initializeTransformUtils(PassRegistry&);

/// Initialize all passes linked into the ScalarOpts library.
void initializeScalarOpts(PassRegistry&);

/// Initialize all passes linked into the Vectorize library.
void initializeVectorization(PassRegistry&);

/// Initialize all passes linked into the InstCombine library.
void initializeInstCombine(PassRegistry&);

/// Initialize all passes linked into the IPO library.
void initializeIPO(PassRegistry&);

/// Initialize all passes linked into the Analysis library.
void initializeAnalysis(PassRegistry&);

/// Initialize all passes linked into the CodeGen library.
void initializeCodeGen(PassRegistry&);

/// Initialize all passes linked into the GlobalISel library.
void initializeGlobalISel(PassRegistry&);

/// Initialize all passes linked into the CodeGen library.
void initializeTarget(PassRegistry&);

void initializeAAResultsWrapperPassPass(PassRegistry&);
void initializeAlwaysInlinerLegacyPassPass(PassRegistry&);
void initializeAssignmentTrackingAnalysisPass(PassRegistry &);
void initializeAssumptionCacheTrackerPass(PassRegistry&);
void initializeAtomicExpandLegacyPass(PassRegistry &);
void initializeBasicBlockPathCloningPass(PassRegistry &);
void initializeBasicBlockSectionsProfileReaderWrapperPassPass(PassRegistry &);
void initializeBasicBlockSectionsPass(PassRegistry &);
void initializeBarrierNoopPass(PassRegistry&);
void initializeBasicAAWrapperPassPass(PassRegistry&);
void initializeBlockFrequencyInfoWrapperPassPass(PassRegistry&);
void initializeBranchFolderPassPass(PassRegistry&);
void initializeBranchProbabilityInfoWrapperPassPass(PassRegistry&);
void initializeBranchRelaxationPass(PassRegistry&);
void initializeBreakCriticalEdgesPass(PassRegistry&);
void initializeBreakFalseDepsPass(PassRegistry&);
void initializeCanonicalizeFreezeInLoopsPass(PassRegistry &);
void initializeCFGSimplifyPassPass(PassRegistry&);
void initializeCFGuardPass(PassRegistry&);
void initializeCFGuardLongjmpPass(PassRegistry&);
void initializeCFIFixupPass(PassRegistry&);
void initializeCFIInstrInserterPass(PassRegistry&);
void initializeCallBrPreparePass(PassRegistry &);
void initializeCallGraphDOTPrinterPass(PassRegistry&);
void initializeCallGraphViewerPass(PassRegistry&);
void initializeCallGraphWrapperPassPass(PassRegistry&);
void initializeCheckDebugMachineModulePass(PassRegistry &);
void initializeCodeGenPrepareLegacyPassPass(PassRegistry &);
void initializeComplexDeinterleavingLegacyPassPass(PassRegistry&);
void initializeConstantHoistingLegacyPassPass(PassRegistry&);
void initializeCycleInfoWrapperPassPass(PassRegistry &);
void initializeDAEPass(PassRegistry&);
void initializeDAHPass(PassRegistry&);
void initializeDCELegacyPassPass(PassRegistry&);
void initializeDeadMachineInstructionElimPass(PassRegistry&);
void initializeDebugifyMachineModulePass(PassRegistry &);
void initializeDependenceAnalysisWrapperPassPass(PassRegistry&);
void initializeDetectDeadLanesPass(PassRegistry&);
void initializeDomOnlyPrinterWrapperPassPass(PassRegistry &);
void initializeDomOnlyViewerWrapperPassPass(PassRegistry &);
void initializeDomPrinterWrapperPassPass(PassRegistry &);
void initializeDomViewerWrapperPassPass(PassRegistry &);
void initializeDominanceFrontierWrapperPassPass(PassRegistry&);
void initializeDominatorTreeWrapperPassPass(PassRegistry&);
void initializeDwarfEHPrepareLegacyPassPass(PassRegistry &);
void initializeEarlyCSELegacyPassPass(PassRegistry&);
void initializeEarlyCSEMemSSALegacyPassPass(PassRegistry&);
void initializeEarlyIfConverterPass(PassRegistry&);
void initializeEarlyIfPredicatorPass(PassRegistry &);
void initializeEarlyMachineLICMPass(PassRegistry&);
void initializeEarlyTailDuplicatePass(PassRegistry&);
void initializeEdgeBundlesPass(PassRegistry&);
void initializeEHContGuardCatchretPass(PassRegistry &);
void initializeExpandLargeFpConvertLegacyPassPass(PassRegistry&);
void initializeExpandLargeDivRemLegacyPassPass(PassRegistry&);
void initializeExpandMemCmpLegacyPassPass(PassRegistry &);
void initializeExpandPostRAPass(PassRegistry&);
void initializeExpandReductionsPass(PassRegistry&);
void initializeExpandVariadicsPass(PassRegistry &);
void initializeExpandVectorPredicationPass(PassRegistry &);
void initializeExternalAAWrapperPassPass(PassRegistry&);
void initializeFEntryInserterPass(PassRegistry&);
void initializeFinalizeISelPass(PassRegistry&);
void initializeFinalizeMachineBundlesPass(PassRegistry&);
void initializeFixIrreduciblePass(PassRegistry &);
void initializeFixupStatepointCallerSavedPass(PassRegistry&);
void initializeFlattenCFGLegacyPassPass(PassRegistry &);
void initializeFuncletLayoutPass(PassRegistry&);
void initializeGCEmptyBasicBlocksPass(PassRegistry &);
void initializeGCMachineCodeAnalysisPass(PassRegistry&);
void initializeGCModuleInfoPass(PassRegistry&);
void initializeGVNLegacyPassPass(PassRegistry&);
void initializeGlobalMergePass(PassRegistry&);
void initializeGlobalsAAWrapperPassPass(PassRegistry &);
void initializeHardwareLoopsLegacyPass(PassRegistry&);
void initializeMIRProfileLoaderPassPass(PassRegistry &);
void initializeIRSimilarityIdentifierWrapperPassPass(PassRegistry&);
void initializeIRTranslatorPass(PassRegistry&);
void initializeIVUsersWrapperPassPass(PassRegistry&);
void initializeIfConverterPass(PassRegistry&);
void initializeImmutableModuleSummaryIndexWrapperPassPass(PassRegistry&);
void initializeImplicitNullChecksPass(PassRegistry&);
void initializeIndirectBrExpandLegacyPassPass(PassRegistry &);
void initializeInferAddressSpacesPass(PassRegistry&);
void initializeInstSimplifyLegacyPassPass(PassRegistry &);
void initializeInstructionCombiningPassPass(PassRegistry&);
void initializeInstructionSelectPass(PassRegistry&);
void initializeInterleavedAccessPass(PassRegistry&);
void initializeInterleavedLoadCombinePass(PassRegistry &);
void initializeJMCInstrumenterPass(PassRegistry&);
void initializeKCFIPass(PassRegistry &);
void initializeLCSSAVerificationPassPass(PassRegistry&);
void initializeLCSSAWrapperPassPass(PassRegistry&);
void initializeLazyBlockFrequencyInfoPassPass(PassRegistry&);
void initializeLazyBranchProbabilityInfoPassPass(PassRegistry&);
void initializeLazyMachineBlockFrequencyInfoPassPass(PassRegistry&);
void initializeLazyValueInfoWrapperPassPass(PassRegistry&);
void initializeLegacyLICMPassPass(PassRegistry&);
void initializeLegalizerPass(PassRegistry&);
void initializeGISelCSEAnalysisWrapperPassPass(PassRegistry &);
void initializeGISelKnownBitsAnalysisPass(PassRegistry &);
void initializeLiveDebugValuesPass(PassRegistry&);
void initializeLiveDebugVariablesPass(PassRegistry&);
void initializeLiveIntervalsWrapperPassPass(PassRegistry &);
void initializeLiveRangeShrinkPass(PassRegistry&);
void initializeLiveRegMatrixPass(PassRegistry&);
void initializeLiveStacksPass(PassRegistry&);
void initializeLiveVariablesWrapperPassPass(PassRegistry &);
void initializeLoadStoreOptPass(PassRegistry &);
void initializeLoadStoreVectorizerLegacyPassPass(PassRegistry&);
void initializeLocalStackSlotPassPass(PassRegistry&);
void initializeLocalizerPass(PassRegistry&);
void initializeLoopDataPrefetchLegacyPassPass(PassRegistry&);
void initializeLoopExtractorLegacyPassPass(PassRegistry &);
void initializeLoopInfoWrapperPassPass(PassRegistry&);
void initializeLoopPassPass(PassRegistry&);
void initializeLoopSimplifyPass(PassRegistry&);
void initializeLoopStrengthReducePass(PassRegistry&);
void initializeLoopUnrollPass(PassRegistry&);
void initializeLowerAtomicLegacyPassPass(PassRegistry&);
void initializeLowerConstantIntrinsicsPass(PassRegistry&);
void initializeLowerEmuTLSPass(PassRegistry&);
void initializeLowerGlobalDtorsLegacyPassPass(PassRegistry &);
void initializeLowerIntrinsicsPass(PassRegistry&);
void initializeLowerInvokeLegacyPassPass(PassRegistry&);
void initializeLowerSwitchLegacyPassPass(PassRegistry &);
void initializeKCFIPass(PassRegistry &);
void initializeMIRAddFSDiscriminatorsPass(PassRegistry &);
void initializeMIRCanonicalizerPass(PassRegistry &);
void initializeMIRNamerPass(PassRegistry &);
void initializeMIRPrintingPassPass(PassRegistry&);
void initializeMachineBlockFrequencyInfoWrapperPassPass(PassRegistry &);
void initializeMachineBlockPlacementPass(PassRegistry&);
void initializeMachineBlockPlacementStatsPass(PassRegistry&);
void initializeMachineBranchProbabilityInfoWrapperPassPass(PassRegistry &);
void initializeMachineCFGPrinterPass(PassRegistry &);
void initializeMachineCSEPass(PassRegistry&);
void initializeMachineCombinerPass(PassRegistry&);
void initializeMachineCopyPropagationPass(PassRegistry&);
void initializeMachineCycleInfoPrinterPassPass(PassRegistry &);
void initializeMachineCycleInfoWrapperPassPass(PassRegistry &);
void initializeMachineDominanceFrontierPass(PassRegistry&);
void initializeMachineDominatorTreeWrapperPassPass(PassRegistry &);
void initializeMachineFunctionPrinterPassPass(PassRegistry&);
void initializeMachineFunctionSplitterPass(PassRegistry &);
void initializeMachineLateInstrsCleanupPass(PassRegistry&);
void initializeMachineLICMPass(PassRegistry&);
void initializeMachineLoopInfoWrapperPassPass(PassRegistry &);
void initializeMachineModuleInfoWrapperPassPass(PassRegistry &);
void initializeMachineOptimizationRemarkEmitterPassPass(PassRegistry&);
void initializeMachineOutlinerPass(PassRegistry&);
void initializeMachinePipelinerPass(PassRegistry&);
void initializeMachinePostDominatorTreeWrapperPassPass(PassRegistry &);
void initializeMachineRegionInfoPassPass(PassRegistry&);
void initializeMachineSanitizerBinaryMetadataPass(PassRegistry &);
void initializeMachineSchedulerPass(PassRegistry&);
void initializeMachineSinkingPass(PassRegistry&);
void initializeMachineTraceMetricsPass(PassRegistry&);
void initializeMachineUniformityInfoPrinterPassPass(PassRegistry &);
void initializeMachineUniformityAnalysisPassPass(PassRegistry &);
void initializeMachineVerifierLegacyPassPass(PassRegistry &);
void initializeMemoryDependenceWrapperPassPass(PassRegistry&);
void initializeMemorySSAWrapperPassPass(PassRegistry&);
void initializeMergeICmpsLegacyPassPass(PassRegistry &);
void initializeModuleSummaryIndexWrapperPassPass(PassRegistry&);
void initializeModuloScheduleTestPass(PassRegistry&);
void initializeNaryReassociateLegacyPassPass(PassRegistry&);
void initializeObjCARCContractLegacyPassPass(PassRegistry &);
void initializeOptimizationRemarkEmitterWrapperPassPass(PassRegistry&);
void initializeOptimizePHIsPass(PassRegistry&);
void initializePEIPass(PassRegistry&);
void initializePHIEliminationPass(PassRegistry&);
void initializePartiallyInlineLibCallsLegacyPassPass(PassRegistry&);
void initializePatchableFunctionPass(PassRegistry&);
void initializePeepholeOptimizerPass(PassRegistry&);
void initializePhiValuesWrapperPassPass(PassRegistry&);
void initializePhysicalRegisterUsageInfoPass(PassRegistry&);
void initializePlaceBackedgeSafepointsLegacyPassPass(PassRegistry &);
void initializePostDomOnlyPrinterWrapperPassPass(PassRegistry &);
void initializePostDomOnlyViewerWrapperPassPass(PassRegistry &);
void initializePostDomPrinterWrapperPassPass(PassRegistry &);
void initializePostDomViewerWrapperPassPass(PassRegistry &);
void initializePostDominatorTreeWrapperPassPass(PassRegistry&);
void initializePostInlineEntryExitInstrumenterPass(PassRegistry&);
void initializePostMachineSchedulerPass(PassRegistry&);
void initializePostRAHazardRecognizerPass(PassRegistry&);
void initializePostRAMachineSinkingPass(PassRegistry&);
void initializePostRASchedulerPass(PassRegistry&);
void initializePreISelIntrinsicLoweringLegacyPassPass(PassRegistry&);
void initializePrintFunctionPassWrapperPass(PassRegistry&);
void initializePrintModulePassWrapperPass(PassRegistry&);
void initializeProcessImplicitDefsPass(PassRegistry&);
void initializeProfileSummaryInfoWrapperPassPass(PassRegistry&);
void initializePromoteLegacyPassPass(PassRegistry&);
void initializeRABasicPass(PassRegistry&);
void initializePseudoProbeInserterPass(PassRegistry &);
void initializeRAGreedyPass(PassRegistry&);
void initializeReachingDefAnalysisPass(PassRegistry&);
void initializeReassociateLegacyPassPass(PassRegistry&);
void initializeRegAllocEvictionAdvisorAnalysisPass(PassRegistry &);
void initializeRegAllocFastPass(PassRegistry&);
void initializeRegAllocPriorityAdvisorAnalysisPass(PassRegistry &);
void initializeRegAllocScoringPass(PassRegistry &);
void initializeRegBankSelectPass(PassRegistry&);
void initializeRegUsageInfoCollectorPass(PassRegistry&);
void initializeRegUsageInfoPropagationPass(PassRegistry&);
void initializeRegionInfoPassPass(PassRegistry&);
void initializeRegionOnlyPrinterPass(PassRegistry&);
void initializeRegionOnlyViewerPass(PassRegistry&);
void initializeRegionPrinterPass(PassRegistry&);
void initializeRegionViewerPass(PassRegistry&);
void initializeRegisterCoalescerPass(PassRegistry&);
void initializeRemoveRedundantDebugValuesPass(PassRegistry&);
void initializeRenameIndependentSubregsPass(PassRegistry&);
void initializeReplaceWithVeclibLegacyPass(PassRegistry &);
void initializeResetMachineFunctionPass(PassRegistry&);
void initializeReturnProtectorPass(PassRegistry&);
void initializeSCEVAAWrapperPassPass(PassRegistry&);
void initializeSROALegacyPassPass(PassRegistry&);
void initializeSafeStackLegacyPassPass(PassRegistry&);
void initializeSafepointIRVerifierPass(PassRegistry&);
void initializeSelectOptimizePass(PassRegistry &);
void initializeScalarEvolutionWrapperPassPass(PassRegistry&);
void initializeScalarizeMaskedMemIntrinLegacyPassPass(PassRegistry &);
void initializeScavengerTestPass(PassRegistry&);
void initializeScopedNoAliasAAWrapperPassPass(PassRegistry&);
void initializeSeparateConstOffsetFromGEPLegacyPassPass(PassRegistry &);
void initializeShadowStackGCLoweringPass(PassRegistry&);
void initializeShrinkWrapPass(PassRegistry&);
void initializeSingleLoopExtractorPass(PassRegistry&);
void initializeSinkingLegacyPassPass(PassRegistry&);
void initializeSjLjEHPreparePass(PassRegistry&);
void initializeSlotIndexesWrapperPassPass(PassRegistry &);
void initializeSpeculativeExecutionLegacyPassPass(PassRegistry&);
void initializeSpillPlacementPass(PassRegistry&);
void initializeStackColoringPass(PassRegistry&);
void initializeStackFrameLayoutAnalysisPassPass(PassRegistry &);
void initializeStackMapLivenessPass(PassRegistry&);
void initializeStackProtectorPass(PassRegistry&);
void initializeStackSafetyGlobalInfoWrapperPassPass(PassRegistry &);
void initializeStackSafetyInfoWrapperPassPass(PassRegistry &);
void initializeStackSlotColoringPass(PassRegistry&);
void initializeStraightLineStrengthReduceLegacyPassPass(PassRegistry &);
void initializeStripDebugMachineModulePass(PassRegistry &);
void initializeStructurizeCFGLegacyPassPass(PassRegistry &);
void initializeTailCallElimPass(PassRegistry&);
void initializeTailDuplicatePass(PassRegistry&);
void initializeTargetLibraryInfoWrapperPassPass(PassRegistry&);
void initializeTargetPassConfigPass(PassRegistry&);
void initializeTargetTransformInfoWrapperPassPass(PassRegistry&);
void initializeTLSVariableHoistLegacyPassPass(PassRegistry &);
void initializeTwoAddressInstructionLegacyPassPass(PassRegistry &);
void initializeTypeBasedAAWrapperPassPass(PassRegistry&);
void initializeTypePromotionLegacyPass(PassRegistry&);
void initializeInitUndefPass(PassRegistry &);
void initializeUniformityInfoWrapperPassPass(PassRegistry &);
void initializeUnifyLoopExitsLegacyPassPass(PassRegistry &);
void initializeUnpackMachineBundlesPass(PassRegistry&);
void initializeUnreachableBlockElimLegacyPassPass(PassRegistry&);
void initializeUnreachableMachineBlockElimPass(PassRegistry&);
void initializeVerifierLegacyPassPass(PassRegistry&);
void initializeVirtRegMapPass(PassRegistry&);
void initializeVirtRegRewriterPass(PassRegistry&);
void initializeWasmEHPreparePass(PassRegistry&);
void initializeWinEHPreparePass(PassRegistry&);
void initializeWriteBitcodePassPass(PassRegistry&);
void initializeXRayInstrumentationPass(PassRegistry&);

} // end namespace llvm

#endif // LLVM_INITIALIZEPASSES_H
