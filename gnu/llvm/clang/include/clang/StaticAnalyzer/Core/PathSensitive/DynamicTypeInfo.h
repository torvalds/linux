//===- DynamicTypeInfo.h - Runtime type information -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEINFO_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEINFO_H

#include "clang/AST/Type.h"

namespace clang {
namespace ento {

/// Stores the currently inferred strictest bound on the runtime type
/// of a region in a given state along the analysis path.
class DynamicTypeInfo {
public:
  DynamicTypeInfo() {}

  DynamicTypeInfo(QualType Ty, bool CanBeSub = true)
      : DynTy(Ty), CanBeASubClass(CanBeSub) {}

  /// Returns false if the type information is precise (the type 'DynTy' is
  /// the only type in the lattice), true otherwise.
  bool canBeASubClass() const { return CanBeASubClass; }

  /// Returns true if the dynamic type info is available.
  bool isValid() const { return !DynTy.isNull(); }

  /// Returns the currently inferred upper bound on the runtime type.
  QualType getType() const { return DynTy; }

  operator bool() const { return isValid(); }

  bool operator==(const DynamicTypeInfo &RHS) const {
    return DynTy == RHS.DynTy && CanBeASubClass == RHS.CanBeASubClass;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.Add(DynTy);
    ID.AddBoolean(CanBeASubClass);
  }

private:
  QualType DynTy;
  bool CanBeASubClass;
};

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEINFO_H
