//===-- AttrSubjectMatchRules.h - Attribute subject match rules -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ATTRSUBJECTMATCHRULES_H
#define LLVM_CLANG_BASIC_ATTRSUBJECTMATCHRULES_H

#include "llvm/ADT/DenseMap.h"

namespace clang {

class SourceRange;

namespace attr {

/// A list of all the recognized kinds of attributes.
enum SubjectMatchRule {
#define ATTR_MATCH_RULE(X, Spelling, IsAbstract) X,
#include "clang/Basic/AttrSubMatchRulesList.inc"
  SubjectMatchRule_Last = -1
#define ATTR_MATCH_RULE(X, Spelling, IsAbstract) +1
#include "clang/Basic/AttrSubMatchRulesList.inc"
};

const char *getSubjectMatchRuleSpelling(SubjectMatchRule Rule);

using ParsedSubjectMatchRuleSet = llvm::DenseMap<int, SourceRange>;

} // end namespace attr
} // end namespace clang

#endif
