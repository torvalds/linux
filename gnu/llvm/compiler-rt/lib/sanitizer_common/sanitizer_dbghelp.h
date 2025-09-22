//===-- sanitizer_dbghelp.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Wrappers for lazy loaded dbghelp.dll. Provides function pointers and a
// callback to initialize them.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_SYMBOLIZER_WIN_H
#define SANITIZER_SYMBOLIZER_WIN_H

#if !SANITIZER_WINDOWS
#error "sanitizer_dbghelp.h is a Windows-only header"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

namespace __sanitizer {

extern decltype(::StackWalk64) *StackWalk64;
extern decltype(::SymCleanup) *SymCleanup;
extern decltype(::SymFromAddr) *SymFromAddr;
extern decltype(::SymFunctionTableAccess64) *SymFunctionTableAccess64;
extern decltype(::SymGetLineFromAddr64) *SymGetLineFromAddr64;
extern decltype(::SymGetModuleBase64) *SymGetModuleBase64;
extern decltype(::SymGetSearchPathW) *SymGetSearchPathW;
extern decltype(::SymInitialize) *SymInitialize;
extern decltype(::SymSetOptions) *SymSetOptions;
extern decltype(::SymSetSearchPathW) *SymSetSearchPathW;
extern decltype(::UnDecorateSymbolName) *UnDecorateSymbolName;

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_WIN_H
