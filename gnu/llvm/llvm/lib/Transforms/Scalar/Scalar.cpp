//===-- Scalar.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMScalarOpts.a, which
// implements several scalar transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "llvm/InitializePasses.h"

using namespace llvm;

/// initializeScalarOptsPasses - Initialize all passes linked into the
/// ScalarOpts library.
void llvm::initializeScalarOpts(PassRegistry &Registry) {
  initializeConstantHoistingLegacyPassPass(Registry);
  initializeDCELegacyPassPass(Registry);
  initializeGVNLegacyPassPass(Registry);
  initializeEarlyCSELegacyPassPass(Registry);
  initializeEarlyCSEMemSSALegacyPassPass(Registry);
  initializeFlattenCFGLegacyPassPass(Registry);
  initializeInferAddressSpacesPass(Registry);
  initializeInstSimplifyLegacyPassPass(Registry);
  initializeLegacyLICMPassPass(Registry);
  initializeLoopDataPrefetchLegacyPassPass(Registry);
  initializeLoopStrengthReducePass(Registry);
  initializeLoopUnrollPass(Registry);
  initializeLowerAtomicLegacyPassPass(Registry);
  initializeLowerConstantIntrinsicsPass(Registry);
  initializeMergeICmpsLegacyPassPass(Registry);
  initializeNaryReassociateLegacyPassPass(Registry);
  initializePartiallyInlineLibCallsLegacyPassPass(Registry);
  initializeReassociateLegacyPassPass(Registry);
  initializeScalarizeMaskedMemIntrinLegacyPassPass(Registry);
  initializeSROALegacyPassPass(Registry);
  initializeCFGSimplifyPassPass(Registry);
  initializeStructurizeCFGLegacyPassPass(Registry);
  initializeSinkingLegacyPassPass(Registry);
  initializeTailCallElimPass(Registry);
  initializeTLSVariableHoistLegacyPassPass(Registry);
  initializeSeparateConstOffsetFromGEPLegacyPassPass(Registry);
  initializeSpeculativeExecutionLegacyPassPass(Registry);
  initializeStraightLineStrengthReduceLegacyPassPass(Registry);
  initializePlaceBackedgeSafepointsLegacyPassPass(Registry);
  initializePostInlineEntryExitInstrumenterPass(Registry);
}
