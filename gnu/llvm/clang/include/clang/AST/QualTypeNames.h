//===--- QualTypeNames.h - Generate Complete QualType Names ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// ===----------------------------------------------------------------------===//
//
// \file
// Functionality to generate the fully-qualified names of QualTypes,
// including recursively expanding any subtypes and template
// parameters.
//
// More precisely: Generates a name that can be used to name the same
// type if used at the end of the current translation unit--with
// certain limitations. See below.
//
// This code desugars names only very minimally, so in this code:
//
// namespace A {
//   struct X {};
// }
// using A::X;
// namespace B {
//   using std::tuple;
//   typedef tuple<X> TX;
//   TX t;
// }
//
// B::t's type is reported as "B::TX", rather than std::tuple<A::X>.
//
// Also, this code replaces types found via using declarations with
// their more qualified name, so for the code:
//
// using std::tuple;
// tuple<int> TInt;
//
// TInt's type will be named, "std::tuple<int>".
//
// Limitations:
//
// Some types have ambiguous names at the end of a translation unit,
// are not namable at all there, or are special cases in other ways.
//
// 1) Types with only local scope will have their local names:
//
// void foo() {
//   struct LocalType {} LocalVar;
// }
//
// LocalVar's type will be named, "struct LocalType", without any
// qualification.
//
// 2) Types that have been shadowed are reported normally, but a
// client using that name at the end of the translation unit will be
// referring to a different type.
//
// ===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_QUALTYPENAMES_H
#define LLVM_CLANG_AST_QUALTYPENAMES_H

#include "clang/AST/ASTContext.h"

namespace clang {
namespace TypeName {
/// Get the fully qualified name for a type. This includes full
/// qualification of all template parameters etc.
///
/// \param[in] QT - the type for which the fully qualified name will be
/// returned.
/// \param[in] Ctx - the ASTContext to be used.
/// \param[in] WithGlobalNsPrefix - If true, then the global namespace
/// specifier "::" will be prepended to the fully qualified name.
std::string getFullyQualifiedName(QualType QT, const ASTContext &Ctx,
                                  const PrintingPolicy &Policy,
                                  bool WithGlobalNsPrefix = false);

/// Generates a QualType that can be used to name the same type
/// if used at the end of the current translation unit. This ignores
/// issues such as type shadowing.
///
/// \param[in] QT - the type for which the fully qualified type will be
/// returned.
/// \param[in] Ctx - the ASTContext to be used.
/// \param[in] WithGlobalNsPrefix - Indicate whether the global namespace
/// specifier "::" should be prepended or not.
QualType getFullyQualifiedType(QualType QT, const ASTContext &Ctx,
                               bool WithGlobalNsPrefix = false);
} // end namespace TypeName
} // end namespace clang
#endif // LLVM_CLANG_AST_QUALTYPENAMES_H
