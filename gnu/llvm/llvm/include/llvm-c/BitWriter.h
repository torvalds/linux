/*===-- llvm-c/BitWriter.h - BitWriter Library C Interface ------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMBitWriter.a, which          *|
|* implements output of the LLVM bitcode format.                              *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_BITWRITER_H
#define LLVM_C_BITWRITER_H

#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCBitWriter Bit Writer
 * @ingroup LLVMC
 *
 * @{
 */

/*===-- Operations on modules ---------------------------------------------===*/

/** Writes a module to the specified path. Returns 0 on success. */
int LLVMWriteBitcodeToFile(LLVMModuleRef M, const char *Path);

/** Writes a module to an open file descriptor. Returns 0 on success. */
int LLVMWriteBitcodeToFD(LLVMModuleRef M, int FD, int ShouldClose,
                         int Unbuffered);

/** Deprecated for LLVMWriteBitcodeToFD. Writes a module to an open file
    descriptor. Returns 0 on success. Closes the Handle. */
int LLVMWriteBitcodeToFileHandle(LLVMModuleRef M, int Handle);

/** Writes a module to a new memory buffer and returns it. */
LLVMMemoryBufferRef LLVMWriteBitcodeToMemoryBuffer(LLVMModuleRef M);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
