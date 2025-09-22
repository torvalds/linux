//===- BaseSubobject.h - BaseSubobject class --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a definition of the BaseSubobject class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_BASESUBOBJECT_H
#define LLVM_CLANG_AST_BASESUBOBJECT_H

#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclCXX.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/type_traits.h"
#include <cstdint>
#include <utility>

namespace clang {

class CXXRecordDecl;

// BaseSubobject - Uniquely identifies a direct or indirect base class.
// Stores both the base class decl and the offset from the most derived class to
// the base class. Used for vtable and VTT generation.
class BaseSubobject {
  /// Base - The base class declaration.
  const CXXRecordDecl *Base;

  /// BaseOffset - The offset from the most derived class to the base class.
  CharUnits BaseOffset;

public:
  BaseSubobject() = default;
  BaseSubobject(const CXXRecordDecl *Base, CharUnits BaseOffset)
      : Base(Base), BaseOffset(BaseOffset) {}

  /// getBase - Returns the base class declaration.
  const CXXRecordDecl *getBase() const { return Base; }

  /// getBaseOffset - Returns the base class offset.
  CharUnits getBaseOffset() const { return BaseOffset; }

  friend bool operator==(const BaseSubobject &LHS, const BaseSubobject &RHS) {
    return LHS.Base == RHS.Base && LHS.BaseOffset == RHS.BaseOffset;
 }
};

} // namespace clang

namespace llvm {

template<> struct DenseMapInfo<clang::BaseSubobject> {
  static clang::BaseSubobject getEmptyKey() {
    return clang::BaseSubobject(
      DenseMapInfo<const clang::CXXRecordDecl *>::getEmptyKey(),
      clang::CharUnits::fromQuantity(DenseMapInfo<int64_t>::getEmptyKey()));
  }

  static clang::BaseSubobject getTombstoneKey() {
    return clang::BaseSubobject(
      DenseMapInfo<const clang::CXXRecordDecl *>::getTombstoneKey(),
      clang::CharUnits::fromQuantity(DenseMapInfo<int64_t>::getTombstoneKey()));
  }

  static unsigned getHashValue(const clang::BaseSubobject &Base) {
    using PairTy = std::pair<const clang::CXXRecordDecl *, clang::CharUnits>;

    return DenseMapInfo<PairTy>::getHashValue(PairTy(Base.getBase(),
                                                     Base.getBaseOffset()));
  }

  static bool isEqual(const clang::BaseSubobject &LHS,
                      const clang::BaseSubobject &RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif // LLVM_CLANG_AST_BASESUBOBJECT_H
