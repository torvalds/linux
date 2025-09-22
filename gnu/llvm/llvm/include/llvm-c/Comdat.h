/*===-- llvm-c/Comdat.h - Module Comdat C Interface -------------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines the C interface to COMDAT.                               *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_COMDAT_H
#define LLVM_C_COMDAT_H

#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCCoreComdat Comdats
 * @ingroup LLVMCCore
 *
 * @{
 */

typedef enum {
  LLVMAnyComdatSelectionKind,        ///< The linker may choose any COMDAT.
  LLVMExactMatchComdatSelectionKind, ///< The data referenced by the COMDAT must
                                     ///< be the same.
  LLVMLargestComdatSelectionKind,    ///< The linker will choose the largest
                                     ///< COMDAT.
  LLVMNoDeduplicateComdatSelectionKind, ///< No deduplication is performed.
  LLVMSameSizeComdatSelectionKind ///< The data referenced by the COMDAT must be
                                  ///< the same size.
} LLVMComdatSelectionKind;

/**
 * Return the Comdat in the module with the specified name. It is created
 * if it didn't already exist.
 *
 * @see llvm::Module::getOrInsertComdat()
 */
LLVMComdatRef LLVMGetOrInsertComdat(LLVMModuleRef M, const char *Name);

/**
 * Get the Comdat assigned to the given global object.
 *
 * @see llvm::GlobalObject::getComdat()
 */
LLVMComdatRef LLVMGetComdat(LLVMValueRef V);

/**
 * Assign the Comdat to the given global object.
 *
 * @see llvm::GlobalObject::setComdat()
 */
void LLVMSetComdat(LLVMValueRef V, LLVMComdatRef C);

/*
 * Get the conflict resolution selection kind for the Comdat.
 *
 * @see llvm::Comdat::getSelectionKind()
 */
LLVMComdatSelectionKind LLVMGetComdatSelectionKind(LLVMComdatRef C);

/*
 * Set the conflict resolution selection kind for the Comdat.
 *
 * @see llvm::Comdat::setSelectionKind()
 */
void LLVMSetComdatSelectionKind(LLVMComdatRef C, LLVMComdatSelectionKind Kind);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
