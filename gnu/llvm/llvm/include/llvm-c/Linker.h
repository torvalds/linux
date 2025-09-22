/*===-- llvm-c/Linker.h - Module Linker C Interface -------------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines the C interface to the module/file/archive linker.       *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_LINKER_H
#define LLVM_C_LINKER_H

#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCCoreLinker Linker
 * @ingroup LLVMCCore
 *
 * @{
 */

/* This enum is provided for backwards-compatibility only. It has no effect. */
typedef enum {
  LLVMLinkerDestroySource = 0, /* This is the default behavior. */
  LLVMLinkerPreserveSource_Removed = 1 /* This option has been deprecated and
                                          should not be used. */
} LLVMLinkerMode;

/* Links the source module into the destination module. The source module is
 * destroyed.
 * The return value is true if an error occurred, false otherwise.
 * Use the diagnostic handler to get any diagnostic message.
*/
LLVMBool LLVMLinkModules2(LLVMModuleRef Dest, LLVMModuleRef Src);

LLVM_C_EXTERN_C_END

/**
 * @}
 */

#endif
