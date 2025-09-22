//===--- Attributes.h - Attributes header -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ATTRIBUTES_H
#define LLVM_CLANG_BASIC_ATTRIBUTES_H

#include "clang/Basic/AttributeCommonInfo.h"

namespace clang {

class IdentifierInfo;
class LangOptions;
class TargetInfo;

/// Return the version number associated with the attribute if we
/// recognize and implement the attribute specified by the given information.
int hasAttribute(AttributeCommonInfo::Syntax Syntax,
                 const IdentifierInfo *Scope, const IdentifierInfo *Attr,
                 const TargetInfo &Target, const LangOptions &LangOpts);

} // end namespace clang

#endif // LLVM_CLANG_BASIC_ATTRIBUTES_H
