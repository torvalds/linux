//===- DiagnosticCategories.h - Diagnostic Categories Enumerators-*- C++ -*===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DIAGNOSTICCATEGORIES_H
#define LLVM_CLANG_BASIC_DIAGNOSTICCATEGORIES_H

namespace clang {
  namespace diag {
    enum {
#define GET_CATEGORY_TABLE
#define CATEGORY(X, ENUM) ENUM,
#include "clang/Basic/DiagnosticGroups.inc"
#undef CATEGORY
#undef GET_CATEGORY_TABLE
      DiagCat_NUM_CATEGORIES
    };

    enum class Group {
#define DIAG_ENTRY(GroupName, FlagNameOffset, Members, SubGroups, Docs)        \
  GroupName,
#include "clang/Basic/DiagnosticGroups.inc"
#undef CATEGORY
#undef DIAG_ENTRY
    };
  }  // end namespace diag
}  // end namespace clang

#endif
