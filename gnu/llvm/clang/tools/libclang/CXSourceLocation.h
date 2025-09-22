//===- CXSourceLocation.h - CXSourceLocations Utilities ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines routines for manipulating CXSourceLocations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CXSOURCELOCATION_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CXSOURCELOCATION_H

#include "clang-c/Index.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"

namespace clang {

class SourceManager;

namespace cxloc {

/// Translate a Clang source location into a CIndex source location.
static inline CXSourceLocation 
translateSourceLocation(const SourceManager &SM, const LangOptions &LangOpts,
                        SourceLocation Loc) {
  if (Loc.isInvalid())
    return clang_getNullLocation();

  CXSourceLocation Result = { { &SM, &LangOpts, },
                              Loc.getRawEncoding() };
  return Result;
}
  
/// Translate a Clang source location into a CIndex source location.
static inline CXSourceLocation translateSourceLocation(ASTContext &Context,
                                                       SourceLocation Loc) {
  return translateSourceLocation(Context.getSourceManager(),
                                 Context.getLangOpts(),
                                 Loc);
}

/// Translate a Clang source range into a CIndex source range.
///
/// Clang internally represents ranges where the end location points to the
/// start of the token at the end. However, for external clients it is more
/// useful to have a CXSourceRange be a proper half-open interval. This routine
/// does the appropriate translation.
CXSourceRange translateSourceRange(const SourceManager &SM, 
                                   const LangOptions &LangOpts,
                                   const CharSourceRange &R);
  
/// Translate a Clang source range into a CIndex source range.
static inline CXSourceRange translateSourceRange(ASTContext &Context,
                                                 SourceRange R) {
  return translateSourceRange(Context.getSourceManager(),
                              Context.getLangOpts(),
                              CharSourceRange::getTokenRange(R));
}

static inline SourceLocation translateSourceLocation(CXSourceLocation L) {
  return SourceLocation::getFromRawEncoding(L.int_data);
}

static inline SourceRange translateCXSourceRange(CXSourceRange R) {
  return SourceRange(SourceLocation::getFromRawEncoding(R.begin_int_data),
                     SourceLocation::getFromRawEncoding(R.end_int_data));
}

/// Translates CXSourceRange to CharSourceRange.
/// The semantics of \p R are:
/// R.begin_int_data is first character of the range.
/// R.end_int_data is one character past the end of the range.
CharSourceRange translateCXRangeToCharRange(CXSourceRange R);
}} // end namespace: clang::cxloc

#endif
