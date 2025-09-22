/*===-- llvm-c/TargetMachine.h - Target Machine Library C Interface - C++ -*-=*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to the Target and TargetMachine       *|
|* classes, which can be used to generate assembly or object files.           *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TARGETMACHINE_H
#define LLVM_C_TARGETMACHINE_H

#include "llvm-c/ExternC.h"
#include "llvm-c/Target.h"
#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @addtogroup LLVMCTarget
 *
 * @{
 */

typedef struct LLVMOpaqueTargetMachineOptions *LLVMTargetMachineOptionsRef;
typedef struct LLVMOpaqueTargetMachine *LLVMTargetMachineRef;
typedef struct LLVMTarget *LLVMTargetRef;

typedef enum {
    LLVMCodeGenLevelNone,
    LLVMCodeGenLevelLess,
    LLVMCodeGenLevelDefault,
    LLVMCodeGenLevelAggressive
} LLVMCodeGenOptLevel;

typedef enum {
    LLVMRelocDefault,
    LLVMRelocStatic,
    LLVMRelocPIC,
    LLVMRelocDynamicNoPic,
    LLVMRelocROPI,
    LLVMRelocRWPI,
    LLVMRelocROPI_RWPI
} LLVMRelocMode;

typedef enum {
    LLVMCodeModelDefault,
    LLVMCodeModelJITDefault,
    LLVMCodeModelTiny,
    LLVMCodeModelSmall,
    LLVMCodeModelKernel,
    LLVMCodeModelMedium,
    LLVMCodeModelLarge
} LLVMCodeModel;

typedef enum {
    LLVMAssemblyFile,
    LLVMObjectFile
} LLVMCodeGenFileType;

typedef enum {
  LLVMGlobalISelAbortEnable,
  LLVMGlobalISelAbortDisable,
  LLVMGlobalISelAbortDisableWithDiag,
} LLVMGlobalISelAbortMode;

/** Returns the first llvm::Target in the registered targets list. */
LLVMTargetRef LLVMGetFirstTarget(void);
/** Returns the next llvm::Target given a previous one (or null if there's none) */
LLVMTargetRef LLVMGetNextTarget(LLVMTargetRef T);

/*===-- Target ------------------------------------------------------------===*/
/** Finds the target corresponding to the given name and stores it in \p T.
  Returns 0 on success. */
LLVMTargetRef LLVMGetTargetFromName(const char *Name);

/** Finds the target corresponding to the given triple and stores it in \p T.
  Returns 0 on success. Optionally returns any error in ErrorMessage.
  Use LLVMDisposeMessage to dispose the message. */
LLVMBool LLVMGetTargetFromTriple(const char* Triple, LLVMTargetRef *T,
                                 char **ErrorMessage);

/** Returns the name of a target. See llvm::Target::getName */
const char *LLVMGetTargetName(LLVMTargetRef T);

/** Returns the description  of a target. See llvm::Target::getDescription */
const char *LLVMGetTargetDescription(LLVMTargetRef T);

/** Returns if the target has a JIT */
LLVMBool LLVMTargetHasJIT(LLVMTargetRef T);

/** Returns if the target has a TargetMachine associated */
LLVMBool LLVMTargetHasTargetMachine(LLVMTargetRef T);

/** Returns if the target as an ASM backend (required for emitting output) */
LLVMBool LLVMTargetHasAsmBackend(LLVMTargetRef T);

/*===-- Target Machine ----------------------------------------------------===*/
/**
 * Create a new set of options for an llvm::TargetMachine.
 *
 * The returned option structure must be released with
 * LLVMDisposeTargetMachineOptions() after the call to
 * LLVMCreateTargetMachineWithOptions().
 */
LLVMTargetMachineOptionsRef LLVMCreateTargetMachineOptions(void);

/**
 * Dispose of an LLVMTargetMachineOptionsRef instance.
 */
void LLVMDisposeTargetMachineOptions(LLVMTargetMachineOptionsRef Options);

void LLVMTargetMachineOptionsSetCPU(LLVMTargetMachineOptionsRef Options,
                                    const char *CPU);

/**
 * Set the list of features for the target machine.
 *
 * \param Features a comma-separated list of features.
 */
void LLVMTargetMachineOptionsSetFeatures(LLVMTargetMachineOptionsRef Options,
                                         const char *Features);

void LLVMTargetMachineOptionsSetABI(LLVMTargetMachineOptionsRef Options,
                                    const char *ABI);

void LLVMTargetMachineOptionsSetCodeGenOptLevel(
    LLVMTargetMachineOptionsRef Options, LLVMCodeGenOptLevel Level);

void LLVMTargetMachineOptionsSetRelocMode(LLVMTargetMachineOptionsRef Options,
                                          LLVMRelocMode Reloc);

void LLVMTargetMachineOptionsSetCodeModel(LLVMTargetMachineOptionsRef Options,
                                          LLVMCodeModel CodeModel);

/**
 * Create a new llvm::TargetMachine.
 *
 * \param T the target to create a machine for.
 * \param Triple a triple describing the target machine.
 * \param Options additional configuration (see
 *                LLVMCreateTargetMachineOptions()).
 */
LLVMTargetMachineRef
LLVMCreateTargetMachineWithOptions(LLVMTargetRef T, const char *Triple,
                                   LLVMTargetMachineOptionsRef Options);

/** Creates a new llvm::TargetMachine. See llvm::Target::createTargetMachine */
LLVMTargetMachineRef LLVMCreateTargetMachine(LLVMTargetRef T,
  const char *Triple, const char *CPU, const char *Features,
  LLVMCodeGenOptLevel Level, LLVMRelocMode Reloc, LLVMCodeModel CodeModel);

/** Dispose the LLVMTargetMachineRef instance generated by
  LLVMCreateTargetMachine. */
void LLVMDisposeTargetMachine(LLVMTargetMachineRef T);

/** Returns the Target used in a TargetMachine */
LLVMTargetRef LLVMGetTargetMachineTarget(LLVMTargetMachineRef T);

/** Returns the triple used creating this target machine. See
  llvm::TargetMachine::getTriple. The result needs to be disposed with
  LLVMDisposeMessage. */
char *LLVMGetTargetMachineTriple(LLVMTargetMachineRef T);

/** Returns the cpu used creating this target machine. See
  llvm::TargetMachine::getCPU. The result needs to be disposed with
  LLVMDisposeMessage. */
char *LLVMGetTargetMachineCPU(LLVMTargetMachineRef T);

/** Returns the feature string used creating this target machine. See
  llvm::TargetMachine::getFeatureString. The result needs to be disposed with
  LLVMDisposeMessage. */
char *LLVMGetTargetMachineFeatureString(LLVMTargetMachineRef T);

/** Create a DataLayout based on the targetMachine. */
LLVMTargetDataRef LLVMCreateTargetDataLayout(LLVMTargetMachineRef T);

/** Set the target machine's ASM verbosity. */
void LLVMSetTargetMachineAsmVerbosity(LLVMTargetMachineRef T,
                                      LLVMBool VerboseAsm);

/** Enable fast-path instruction selection. */
void LLVMSetTargetMachineFastISel(LLVMTargetMachineRef T, LLVMBool Enable);

/** Enable global instruction selection. */
void LLVMSetTargetMachineGlobalISel(LLVMTargetMachineRef T, LLVMBool Enable);

/** Set abort behaviour when global instruction selection fails to lower/select
 * an instruction. */
void LLVMSetTargetMachineGlobalISelAbort(LLVMTargetMachineRef T,
                                         LLVMGlobalISelAbortMode Mode);

/** Enable the MachineOutliner pass. */
void LLVMSetTargetMachineMachineOutliner(LLVMTargetMachineRef T,
                                         LLVMBool Enable);

/** Emits an asm or object file for the given module to the filename. This
  wraps several c++ only classes (among them a file stream). Returns any
  error in ErrorMessage. Use LLVMDisposeMessage to dispose the message. */
LLVMBool LLVMTargetMachineEmitToFile(LLVMTargetMachineRef T, LLVMModuleRef M,
                                     const char *Filename,
                                     LLVMCodeGenFileType codegen,
                                     char **ErrorMessage);

/** Compile the LLVM IR stored in \p M and store the result in \p OutMemBuf. */
LLVMBool LLVMTargetMachineEmitToMemoryBuffer(LLVMTargetMachineRef T, LLVMModuleRef M,
  LLVMCodeGenFileType codegen, char** ErrorMessage, LLVMMemoryBufferRef *OutMemBuf);

/*===-- Triple ------------------------------------------------------------===*/
/** Get a triple for the host machine as a string. The result needs to be
  disposed with LLVMDisposeMessage. */
char* LLVMGetDefaultTargetTriple(void);

/** Normalize a target triple. The result needs to be disposed with
  LLVMDisposeMessage. */
char* LLVMNormalizeTargetTriple(const char* triple);

/** Get the host CPU as a string. The result needs to be disposed with
  LLVMDisposeMessage. */
char* LLVMGetHostCPUName(void);

/** Get the host CPU's features as a string. The result needs to be disposed
  with LLVMDisposeMessage. */
char* LLVMGetHostCPUFeatures(void);

/** Adds the target-specific analysis passes to the pass manager. */
void LLVMAddAnalysisPasses(LLVMTargetMachineRef T, LLVMPassManagerRef PM);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
