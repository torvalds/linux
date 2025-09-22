//===-- UnresolvedSet.h - Unresolved sets of declarations  ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the WeakInfo class, which is used to store
//  information about the target of a #pragma weak directive.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_WEAK_H
#define LLVM_CLANG_SEMA_WEAK_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMapInfo.h"

namespace clang {

class IdentifierInfo;

/// Captures information about a \#pragma weak directive.
class WeakInfo {
  const IdentifierInfo *alias = nullptr; // alias (optional)
  SourceLocation loc;                    // for diagnostics
public:
  WeakInfo() = default;
  WeakInfo(const IdentifierInfo *Alias, SourceLocation Loc)
      : alias(Alias), loc(Loc) {}
  inline const IdentifierInfo *getAlias() const { return alias; }
  inline SourceLocation getLocation() const { return loc; }
  bool operator==(WeakInfo RHS) const = delete;
  bool operator!=(WeakInfo RHS) const = delete;

  struct DenseMapInfoByAliasOnly
      : private llvm::DenseMapInfo<const IdentifierInfo *> {
    static inline WeakInfo getEmptyKey() {
      return WeakInfo(DenseMapInfo::getEmptyKey(), SourceLocation());
    }
    static inline WeakInfo getTombstoneKey() {
      return WeakInfo(DenseMapInfo::getTombstoneKey(), SourceLocation());
    }
    static unsigned getHashValue(const WeakInfo &W) {
      return DenseMapInfo::getHashValue(W.getAlias());
    }
    static bool isEqual(const WeakInfo &LHS, const WeakInfo &RHS) {
      return DenseMapInfo::isEqual(LHS.getAlias(), RHS.getAlias());
    }
  };
};

} // end namespace clang

#endif // LLVM_CLANG_SEMA_WEAK_H
