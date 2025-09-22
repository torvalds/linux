//==--- InterCheckerAPI.h ---------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file allows introduction of checker dependencies. It contains APIs for
// inter-checker communications.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_INTERCHECKERAPI_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_INTERCHECKERAPI_H

// FIXME: This file goes against how a checker should be implemented either in
// a single file, or be exposed in a header file. Let's try to get rid of it!

namespace clang {
namespace ento {

class CheckerManager;

/// Register the part of MallocChecker connected to InnerPointerChecker.
void registerInnerPointerCheckerAux(CheckerManager &Mgr);

} // namespace ento
} // namespace clang

#endif /* INTERCHECKERAPI_H_ */
