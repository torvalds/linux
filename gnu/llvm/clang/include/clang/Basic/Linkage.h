//===- Linkage.h - Linkage enumeration and utilities ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the Linkage enumeration and various utility functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_LINKAGE_H
#define LLVM_CLANG_BASIC_LINKAGE_H

#include "llvm/Support/ErrorHandling.h"
#include <utility>

namespace clang {

/// Describes the different kinds of linkage
/// (C++ [basic.link], C99 6.2.2) that an entity may have.
enum class Linkage : unsigned char {
  // Linkage hasn't been computed.
  Invalid = 0,

  /// No linkage, which means that the entity is unique and
  /// can only be referred to from within its scope.
  None,

  /// Internal linkage, which indicates that the entity can
  /// be referred to from within the translation unit (but not other
  /// translation units).
  Internal,

  /// External linkage within a unique namespace.
  ///
  /// From the language perspective, these entities have external
  /// linkage. However, since they reside in an anonymous namespace,
  /// their names are unique to this translation unit, which is
  /// equivalent to having internal linkage from the code-generation
  /// point of view.
  UniqueExternal,

  /// No linkage according to the standard, but is visible from other
  /// translation units because of types defined in a inline function.
  VisibleNone,

  /// Module linkage, which indicates that the entity can be referred
  /// to from other translation units within the same module, and indirectly
  /// from arbitrary other translation units through inline functions and
  /// templates in the module interface.
  Module,

  /// External linkage, which indicates that the entity can
  /// be referred to from other translation units.
  External
};

/// Describes the different kinds of language linkage
/// (C++ [dcl.link]) that an entity may have.
enum LanguageLinkage {
  CLanguageLinkage,
  CXXLanguageLinkage,
  NoLanguageLinkage
};

/// A more specific kind of linkage than enum Linkage.
///
/// This is relevant to CodeGen and AST file reading.
enum GVALinkage {
  GVA_Internal,
  GVA_AvailableExternally,
  GVA_DiscardableODR,
  GVA_StrongExternal,
  GVA_StrongODR
};

inline bool isDiscardableGVALinkage(GVALinkage L) {
  return L <= GVA_DiscardableODR;
}

/// Do we know that this will be the only definition of this symbol (excluding
/// inlining-only definitions)?
inline bool isUniqueGVALinkage(GVALinkage L) {
  return L == GVA_Internal || L == GVA_StrongExternal;
}

inline bool isExternallyVisible(Linkage L) {
  switch (L) {
  case Linkage::Invalid:
    llvm_unreachable("Linkage hasn't been computed!");
  case Linkage::None:
  case Linkage::Internal:
  case Linkage::UniqueExternal:
    return false;
  case Linkage::VisibleNone:
  case Linkage::Module:
  case Linkage::External:
    return true;
  }
  llvm_unreachable("Unhandled Linkage enum");
}

inline Linkage getFormalLinkage(Linkage L) {
  switch (L) {
  case Linkage::UniqueExternal:
    return Linkage::External;
  case Linkage::VisibleNone:
    return Linkage::None;
  default:
    return L;
  }
}

inline bool isExternalFormalLinkage(Linkage L) {
  return getFormalLinkage(L) == Linkage::External;
}

/// Compute the minimum linkage given two linkages.
///
/// The linkage can be interpreted as a pair formed by the formal linkage and
/// a boolean for external visibility. This is just what getFormalLinkage and
/// isExternallyVisible return. We want the minimum of both components. The
/// Linkage enum is defined in an order that makes this simple, we just need
/// special cases for when VisibleNoLinkage would lose the visible bit and
/// become NoLinkage.
inline Linkage minLinkage(Linkage L1, Linkage L2) {
  if (L2 == Linkage::VisibleNone)
    std::swap(L1, L2);
  if (L1 == Linkage::VisibleNone) {
    if (L2 == Linkage::Internal)
      return Linkage::None;
    if (L2 == Linkage::UniqueExternal)
      return Linkage::None;
  }
  return L1 < L2 ? L1 : L2;
}

} // namespace clang

#endif // LLVM_CLANG_BASIC_LINKAGE_H
