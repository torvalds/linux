//===--- AllDiagnostics.h - Aggregate Diagnostic headers --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Includes all the separate Diagnostic headers & some related helpers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ALLDIAGNOSTICS_H
#define LLVM_CLANG_BASIC_ALLDIAGNOSTICS_H

#include "clang/Basic/DiagnosticAST.h"
#include "clang/Basic/DiagnosticAnalysis.h"
#include "clang/Basic/DiagnosticComment.h"
#include "clang/Basic/DiagnosticCrossTU.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/DiagnosticInstallAPI.h"
#include "clang/Basic/DiagnosticLex.h"
#include "clang/Basic/DiagnosticParse.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/DiagnosticSerialization.h"
#include "clang/Basic/DiagnosticRefactoring.h"

namespace clang {
template <size_t SizeOfStr, typename FieldType>
class StringSizerHelper {
  static_assert(SizeOfStr <= FieldType(~0U), "Field too small!");
public:
  enum { Size = SizeOfStr };
};
} // end namespace clang

#define STR_SIZE(str, fieldTy) clang::StringSizerHelper<sizeof(str)-1, \
                                                        fieldTy>::Size

#endif
