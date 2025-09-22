//===--- ClangSACheckers.h - Registration functions for Checkers *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the registation functions for the checkers defined in
// libclangStaticAnalyzerCheckers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CHECKERS_BUILTINCHECKERREGISTRATION_H
#define LLVM_CLANG_STATICANALYZER_CHECKERS_BUILTINCHECKERREGISTRATION_H

#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"

namespace clang {
namespace ento {

class CheckerManager;

#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI, IS_HIDDEN)                 \
  void register##CLASS(CheckerManager &mgr);                                   \
  bool shouldRegister##CLASS(const CheckerManager &mgr);
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS

} // end ento namespace

} // end clang namespace

#endif
