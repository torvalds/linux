/*===------- llvm-c/LLJITUtils.h - Advanced LLJIT features --------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface for extra utilities to be used with   *|
|* the LLJIT class from the llvm-c/LLJIT.h header. It requires to following   *|
|* link libraries in addition to libLLVMOrcJIT.a:                             *|
|*  - libLLVMOrcDebugging.a                                                   *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
|* Note: This interface is experimental. It is *NOT* stable, and may be       *|
|*       changed without warning. Only C API usage documentation is           *|
|*       provided. See the C++ documentation for all higher level ORC API     *|
|*       details.                                                             *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_LLJITUTILS_H
#define LLVM_C_LLJITUTILS_H

#include "llvm-c/LLJIT.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCExecutionEngineLLJITUtils LLJIT Utilities
 * @ingroup LLVMCExecutionEngineLLJIT
 *
 * @{
 */

/**
 * Install the plugin that submits debug objects to the executor. Executors must
 * expose the llvm_orc_registerJITLoaderGDBWrapper symbol.
 */
LLVMErrorRef LLVMOrcLLJITEnableDebugSupport(LLVMOrcLLJITRef J);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif /* LLVM_C_LLJITUTILS_H */
