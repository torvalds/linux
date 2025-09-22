/*===-- llvm-c/BitReader.h - BitReader Library C Interface ------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMBitReader.a, which          *|
|* implements input of the LLVM bitcode format.                               *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_BITREADER_H
#define LLVM_C_BITREADER_H

#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCBitReader Bit Reader
 * @ingroup LLVMC
 *
 * @{
 */

/* Builds a module from the bitcode in the specified memory buffer, returning a
   reference to the module via the OutModule parameter. Returns 0 on success.
   Optionally returns a human-readable error message via OutMessage.

   This is deprecated. Use LLVMParseBitcode2. */
LLVMBool LLVMParseBitcode(LLVMMemoryBufferRef MemBuf, LLVMModuleRef *OutModule,
                          char **OutMessage);

/* Builds a module from the bitcode in the specified memory buffer, returning a
   reference to the module via the OutModule parameter. Returns 0 on success. */
LLVMBool LLVMParseBitcode2(LLVMMemoryBufferRef MemBuf,
                           LLVMModuleRef *OutModule);

/* This is deprecated. Use LLVMParseBitcodeInContext2. */
LLVMBool LLVMParseBitcodeInContext(LLVMContextRef ContextRef,
                                   LLVMMemoryBufferRef MemBuf,
                                   LLVMModuleRef *OutModule, char **OutMessage);

LLVMBool LLVMParseBitcodeInContext2(LLVMContextRef ContextRef,
                                    LLVMMemoryBufferRef MemBuf,
                                    LLVMModuleRef *OutModule);

/** Reads a module from the specified path, returning via the OutMP parameter
    a module provider which performs lazy deserialization. Returns 0 on success.
    Optionally returns a human-readable error message via OutMessage.
    This is deprecated. Use LLVMGetBitcodeModuleInContext2. */
LLVMBool LLVMGetBitcodeModuleInContext(LLVMContextRef ContextRef,
                                       LLVMMemoryBufferRef MemBuf,
                                       LLVMModuleRef *OutM, char **OutMessage);

/** Reads a module from the given memory buffer, returning via the OutMP
 * parameter a module provider which performs lazy deserialization.
 *
 * Returns 0 on success.
 *
 * Takes ownership of \p MemBuf if (and only if) the module was read
 * successfully. */
LLVMBool LLVMGetBitcodeModuleInContext2(LLVMContextRef ContextRef,
                                        LLVMMemoryBufferRef MemBuf,
                                        LLVMModuleRef *OutM);

/* This is deprecated. Use LLVMGetBitcodeModule2. */
LLVMBool LLVMGetBitcodeModule(LLVMMemoryBufferRef MemBuf, LLVMModuleRef *OutM,
                              char **OutMessage);

LLVMBool LLVMGetBitcodeModule2(LLVMMemoryBufferRef MemBuf, LLVMModuleRef *OutM);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
