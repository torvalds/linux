/*===------- llvm-c/Error.h - llvm::Error class C Interface -------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines the C interface to LLVM's Error class.                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_ERROR_H
#define LLVM_C_ERROR_H

#include "llvm-c/ExternC.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCError Error Handling
 * @ingroup LLVMC
 *
 * @{
 */

#define LLVMErrorSuccess 0

/**
 * Opaque reference to an error instance. Null serves as the 'success' value.
 */
typedef struct LLVMOpaqueError *LLVMErrorRef;

/**
 * Error type identifier.
 */
typedef const void *LLVMErrorTypeId;

/**
 * Returns the type id for the given error instance, which must be a failure
 * value (i.e. non-null).
 */
LLVMErrorTypeId LLVMGetErrorTypeId(LLVMErrorRef Err);

/**
 * Dispose of the given error without handling it. This operation consumes the
 * error, and the given LLVMErrorRef value is not usable once this call returns.
 * Note: This method *only* needs to be called if the error is not being passed
 * to some other consuming operation, e.g. LLVMGetErrorMessage.
 */
void LLVMConsumeError(LLVMErrorRef Err);

/**
 * Returns the given string's error message. This operation consumes the error,
 * and the given LLVMErrorRef value is not usable once this call returns.
 * The caller is responsible for disposing of the string by calling
 * LLVMDisposeErrorMessage.
 */
char *LLVMGetErrorMessage(LLVMErrorRef Err);

/**
 * Dispose of the given error message.
 */
void LLVMDisposeErrorMessage(char *ErrMsg);

/**
 * Returns the type id for llvm StringError.
 */
LLVMErrorTypeId LLVMGetStringErrorTypeId(void);

/**
 * Create a StringError.
 */
LLVMErrorRef LLVMCreateStringError(const char *ErrMsg);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
