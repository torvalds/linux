//===--- MisExpect.h - Check the use of llvm.expect with PGO data ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit diagnostic messages for potentially incorrect
// usage of the llvm.expect intrinsic. This utility extracts the threshold
// values from metadata associated with the instrumented Branch or Switch
// instruction. The threshold values are then used to determine if a diagnostic
// should be emitted.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MISEXPECT_H
#define LLVM_TRANSFORMS_UTILS_MISEXPECT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"

namespace llvm {
namespace misexpect {

/// checkBackendInstrumentation - compares PGO counters to the thresholds used
/// for llvm.expect and warns if the PGO counters are outside of the expected
/// range. It extracts the expected weights from the MD_prof weights attached
/// to the instruction, which are assumed to come from lowered llvm.expect
/// intrinsics. The RealWeights parameter and the extracted expected weights are
/// then passed to verifyMisexpect() for verification
///
/// \param I The Instruction being checked
/// \param RealWeights A vector of profile weights for each target block
void checkBackendInstrumentation(Instruction &I,
                                 const llvm::ArrayRef<uint32_t> RealWeights);

/// checkFrontendInstrumentation - compares PGO counters to the thresholds used
/// for llvm.expect and warns if the PGO counters are outside of the expected
/// range. It extracts the expected weights from the MD_prof weights attached
/// to the instruction, which are assumed to come from profiling data
/// attached by the frontend prior to llvm.expect intrinsic lowering. The
/// ExpectedWeights parameter and the extracted real weights are then passed to
/// verifyMisexpect() for verification
///
/// \param I The Instruction being checked
/// \param ExpectedWeights A vector of the expected weights for each target
/// block, this determines the threshold values used when emitting diagnostics
void checkFrontendInstrumentation(Instruction &I,
                                  const ArrayRef<uint32_t> ExpectedWeights);

/// veryifyMisExpect - compares RealWeights to the thresholds used
/// for llvm.expect and warns if the PGO counters are outside of the expected
/// range.
///
/// \param I The Instruction being checked
/// \param RealWeights A vector of profile weights from the profile data
/// \param ExpectedWeights A vector of the weights attatch by llvm.expect
void verifyMisExpect(Instruction &I, ArrayRef<uint32_t> RealWeights,
                     const ArrayRef<uint32_t> ExpectedWeights);

/// checkExpectAnnotations - compares PGO counters to the thresholds used
/// for llvm.expect and warns if the PGO counters are outside of the expected
/// range. It extracts the expected weights from the MD_prof weights attached
/// to the instruction, which are assumed to come from lowered llvm.expect
/// intrinsics. The RealWeights parameter and the extracted expected weights are
/// then passed to verifyMisexpect() for verification. It is a thin wrapper
/// around the checkFrontendInstrumentation and checkBackendInstrumentation APIs
///
/// \param I The Instruction being checked
/// \param ExistingWeights A vector of profile weights for each target block
/// \param IsFrontend A boolean describing if this is Frontend instrumentation
void checkExpectAnnotations(Instruction &I,
                            const ArrayRef<uint32_t> ExistingWeights,
                            bool IsFrontend);

} // namespace misexpect
} // namespace llvm

#endif
