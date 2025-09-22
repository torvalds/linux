/*===-- llvm-c/Transform/PassBuilder.h - PassBuilder for LLVM C ---*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header contains the LLVM-C interface into the new pass manager        *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TRANSFORMS_PASSBUILDER_H
#define LLVM_C_TRANSFORMS_PASSBUILDER_H

#include "llvm-c/Error.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Types.h"

/**
 * @defgroup LLVMCCoreNewPM New Pass Manager
 * @ingroup LLVMCCore
 *
 * @{
 */

LLVM_C_EXTERN_C_BEGIN

/**
 * A set of options passed which are attached to the Pass Manager upon run.
 *
 * This corresponds to an llvm::LLVMPassBuilderOptions instance
 *
 * The details for how the different properties of this structure are used can
 * be found in the source for LLVMRunPasses
 */
typedef struct LLVMOpaquePassBuilderOptions *LLVMPassBuilderOptionsRef;

/**
 * Construct and run a set of passes over a module
 *
 * This function takes a string with the passes that should be used. The format
 * of this string is the same as opt's -passes argument for the new pass
 * manager. Individual passes may be specified, separated by commas. Full
 * pipelines may also be invoked using `default<O3>` and friends. See opt for
 * full reference of the Passes format.
 */
LLVMErrorRef LLVMRunPasses(LLVMModuleRef M, const char *Passes,
                           LLVMTargetMachineRef TM,
                           LLVMPassBuilderOptionsRef Options);

/**
 * Create a new set of options for a PassBuilder
 *
 * Ownership of the returned instance is given to the client, and they are
 * responsible for it. The client should call LLVMDisposePassBuilderOptions
 * to free the pass builder options.
 */
LLVMPassBuilderOptionsRef LLVMCreatePassBuilderOptions(void);

/**
 * Toggle adding the VerifierPass for the PassBuilder, ensuring all functions
 * inside the module is valid.
 */
void LLVMPassBuilderOptionsSetVerifyEach(LLVMPassBuilderOptionsRef Options,
                                         LLVMBool VerifyEach);

/**
 * Toggle debug logging when running the PassBuilder
 */
void LLVMPassBuilderOptionsSetDebugLogging(LLVMPassBuilderOptionsRef Options,
                                           LLVMBool DebugLogging);

void LLVMPassBuilderOptionsSetLoopInterleaving(
    LLVMPassBuilderOptionsRef Options, LLVMBool LoopInterleaving);

void LLVMPassBuilderOptionsSetLoopVectorization(
    LLVMPassBuilderOptionsRef Options, LLVMBool LoopVectorization);

void LLVMPassBuilderOptionsSetSLPVectorization(
    LLVMPassBuilderOptionsRef Options, LLVMBool SLPVectorization);

void LLVMPassBuilderOptionsSetLoopUnrolling(LLVMPassBuilderOptionsRef Options,
                                            LLVMBool LoopUnrolling);

void LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll(
    LLVMPassBuilderOptionsRef Options, LLVMBool ForgetAllSCEVInLoopUnroll);

void LLVMPassBuilderOptionsSetLicmMssaOptCap(LLVMPassBuilderOptionsRef Options,
                                             unsigned LicmMssaOptCap);

void LLVMPassBuilderOptionsSetLicmMssaNoAccForPromotionCap(
    LLVMPassBuilderOptionsRef Options, unsigned LicmMssaNoAccForPromotionCap);

void LLVMPassBuilderOptionsSetCallGraphProfile(
    LLVMPassBuilderOptionsRef Options, LLVMBool CallGraphProfile);

void LLVMPassBuilderOptionsSetMergeFunctions(LLVMPassBuilderOptionsRef Options,
                                             LLVMBool MergeFunctions);

void LLVMPassBuilderOptionsSetInlinerThreshold(
    LLVMPassBuilderOptionsRef Options, int Threshold);

/**
 * Dispose of a heap-allocated PassBuilderOptions instance
 */
void LLVMDisposePassBuilderOptions(LLVMPassBuilderOptionsRef Options);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif // LLVM_C_TRANSFORMS_PASSBUILDER_H
