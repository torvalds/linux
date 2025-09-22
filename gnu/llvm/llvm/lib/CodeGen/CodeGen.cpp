//===-- CodeGen.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the common initialization routines for the
// CodeGen library.
//
//===----------------------------------------------------------------------===//

#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"

using namespace llvm;

/// initializeCodeGen - Initialize all passes linked into the CodeGen library.
void llvm::initializeCodeGen(PassRegistry &Registry) {
  initializeAssignmentTrackingAnalysisPass(Registry);
  initializeAtomicExpandLegacyPass(Registry);
  initializeBasicBlockPathCloningPass(Registry);
  initializeBasicBlockSectionsPass(Registry);
  initializeBranchFolderPassPass(Registry);
  initializeBranchRelaxationPass(Registry);
  initializeBreakFalseDepsPass(Registry);
  initializeCallBrPreparePass(Registry);
  initializeCFGuardLongjmpPass(Registry);
  initializeCFIFixupPass(Registry);
  initializeCFIInstrInserterPass(Registry);
  initializeCheckDebugMachineModulePass(Registry);
  initializeCodeGenPrepareLegacyPassPass(Registry);
  initializeDeadMachineInstructionElimPass(Registry);
  initializeDebugifyMachineModulePass(Registry);
  initializeDetectDeadLanesPass(Registry);
  initializeDwarfEHPrepareLegacyPassPass(Registry);
  initializeEarlyIfConverterPass(Registry);
  initializeEarlyIfPredicatorPass(Registry);
  initializeEarlyMachineLICMPass(Registry);
  initializeEarlyTailDuplicatePass(Registry);
  initializeExpandLargeDivRemLegacyPassPass(Registry);
  initializeExpandLargeFpConvertLegacyPassPass(Registry);
  initializeExpandMemCmpLegacyPassPass(Registry);
  initializeExpandPostRAPass(Registry);
  initializeFEntryInserterPass(Registry);
  initializeFinalizeISelPass(Registry);
  initializeFinalizeMachineBundlesPass(Registry);
  initializeFixupStatepointCallerSavedPass(Registry);
  initializeFuncletLayoutPass(Registry);
  initializeGCMachineCodeAnalysisPass(Registry);
  initializeGCModuleInfoPass(Registry);
  initializeHardwareLoopsLegacyPass(Registry);
  initializeIfConverterPass(Registry);
  initializeImplicitNullChecksPass(Registry);
  initializeIndirectBrExpandLegacyPassPass(Registry);
  initializeInitUndefPass(Registry);
  initializeInterleavedLoadCombinePass(Registry);
  initializeInterleavedAccessPass(Registry);
  initializeJMCInstrumenterPass(Registry);
  initializeLiveDebugValuesPass(Registry);
  initializeLiveDebugVariablesPass(Registry);
  initializeLiveIntervalsWrapperPassPass(Registry);
  initializeLiveRangeShrinkPass(Registry);
  initializeLiveStacksPass(Registry);
  initializeLiveVariablesWrapperPassPass(Registry);
  initializeLocalStackSlotPassPass(Registry);
  initializeLowerGlobalDtorsLegacyPassPass(Registry);
  initializeLowerIntrinsicsPass(Registry);
  initializeMIRAddFSDiscriminatorsPass(Registry);
  initializeMIRCanonicalizerPass(Registry);
  initializeMIRNamerPass(Registry);
  initializeMIRProfileLoaderPassPass(Registry);
  initializeMachineBlockFrequencyInfoWrapperPassPass(Registry);
  initializeMachineBlockPlacementPass(Registry);
  initializeMachineBlockPlacementStatsPass(Registry);
  initializeMachineCFGPrinterPass(Registry);
  initializeMachineCSEPass(Registry);
  initializeMachineCombinerPass(Registry);
  initializeMachineCopyPropagationPass(Registry);
  initializeMachineCycleInfoPrinterPassPass(Registry);
  initializeMachineCycleInfoWrapperPassPass(Registry);
  initializeMachineDominatorTreeWrapperPassPass(Registry);
  initializeMachineFunctionPrinterPassPass(Registry);
  initializeMachineLateInstrsCleanupPass(Registry);
  initializeMachineLICMPass(Registry);
  initializeMachineLoopInfoWrapperPassPass(Registry);
  initializeMachineModuleInfoWrapperPassPass(Registry);
  initializeMachineOptimizationRemarkEmitterPassPass(Registry);
  initializeMachineOutlinerPass(Registry);
  initializeMachinePipelinerPass(Registry);
  initializeMachineSanitizerBinaryMetadataPass(Registry);
  initializeModuloScheduleTestPass(Registry);
  initializeMachinePostDominatorTreeWrapperPassPass(Registry);
  initializeMachineRegionInfoPassPass(Registry);
  initializeMachineSchedulerPass(Registry);
  initializeMachineSinkingPass(Registry);
  initializeMachineUniformityAnalysisPassPass(Registry);
  initializeMachineUniformityInfoPrinterPassPass(Registry);
  initializeMachineVerifierLegacyPassPass(Registry);
  initializeObjCARCContractLegacyPassPass(Registry);
  initializeOptimizePHIsPass(Registry);
  initializePEIPass(Registry);
  initializePHIEliminationPass(Registry);
  initializePatchableFunctionPass(Registry);
  initializePeepholeOptimizerPass(Registry);
  initializePostMachineSchedulerPass(Registry);
  initializePostRAHazardRecognizerPass(Registry);
  initializePostRAMachineSinkingPass(Registry);
  initializePostRASchedulerPass(Registry);
  initializePreISelIntrinsicLoweringLegacyPassPass(Registry);
  initializeProcessImplicitDefsPass(Registry);
  initializeRABasicPass(Registry);
  initializeRAGreedyPass(Registry);
  initializeRegAllocFastPass(Registry);
  initializeRegUsageInfoCollectorPass(Registry);
  initializeRegUsageInfoPropagationPass(Registry);
  initializeRegisterCoalescerPass(Registry);
  initializeRemoveRedundantDebugValuesPass(Registry);
  initializeRenameIndependentSubregsPass(Registry);
  initializeSafeStackLegacyPassPass(Registry);
  initializeSelectOptimizePass(Registry);
  initializeShadowStackGCLoweringPass(Registry);
  initializeShrinkWrapPass(Registry);
  initializeSjLjEHPreparePass(Registry);
  initializeSlotIndexesWrapperPassPass(Registry);
  initializeStackColoringPass(Registry);
  initializeStackFrameLayoutAnalysisPassPass(Registry);
  initializeStackMapLivenessPass(Registry);
  initializeStackProtectorPass(Registry);
  initializeStackSlotColoringPass(Registry);
  initializeStripDebugMachineModulePass(Registry);
  initializeTailDuplicatePass(Registry);
  initializeTargetPassConfigPass(Registry);
  initializeTwoAddressInstructionLegacyPassPass(Registry);
  initializeTypePromotionLegacyPass(Registry);
  initializeUnpackMachineBundlesPass(Registry);
  initializeUnreachableBlockElimLegacyPassPass(Registry);
  initializeUnreachableMachineBlockElimPass(Registry);
  initializeVirtRegMapPass(Registry);
  initializeVirtRegRewriterPass(Registry);
  initializeWasmEHPreparePass(Registry);
  initializeWinEHPreparePass(Registry);
  initializeXRayInstrumentationPass(Registry);
}
