/*===-- clang-c/Platform.h - C Index platform decls   -------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides platform specific macros (dllimport, deprecated, ...) *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_PLATFORM_H
#define LLVM_CLANG_C_PLATFORM_H

#include "clang-c/ExternC.h"

LLVM_CLANG_C_EXTERN_C_BEGIN

/* Windows DLL import/export. */
#ifndef CINDEX_NO_EXPORTS
  #define CINDEX_EXPORTS
#endif
#ifdef _WIN32
  #ifdef CINDEX_EXPORTS
    #ifdef _CINDEX_LIB_
      #define CINDEX_LINKAGE __declspec(dllexport)
    #else
      #define CINDEX_LINKAGE __declspec(dllimport)
    #endif
  #endif
#elif defined(CINDEX_EXPORTS) && defined(__GNUC__)
  #define CINDEX_LINKAGE __attribute__((visibility("default")))
#endif

#ifndef CINDEX_LINKAGE
  #define CINDEX_LINKAGE
#endif

#ifdef __GNUC__
  #define CINDEX_DEPRECATED __attribute__((deprecated))
#else
  #ifdef _MSC_VER
    #define CINDEX_DEPRECATED __declspec(deprecated)
  #else
    #define CINDEX_DEPRECATED
  #endif
#endif

LLVM_CLANG_C_EXTERN_C_END

#endif
