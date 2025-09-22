//===--- SemaFixItUtils.h - Sema FixIts -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines helper classes for generation of Sema FixItHints.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_SEMA_SEMAFIXITUTILS_H
#define LLVM_CLANG_SEMA_SEMAFIXITUTILS_H

#include "clang/AST/Expr.h"

namespace clang {

enum OverloadFixItKind {
  OFIK_Undefined = 0,
  OFIK_Dereference,
  OFIK_TakeAddress,
  OFIK_RemoveDereference,
  OFIK_RemoveTakeAddress
};

class Sema;

/// The class facilities generation and storage of conversion FixIts. Hints for
/// new conversions are added using TryToFixConversion method. The default type
/// conversion checker can be reset.
struct ConversionFixItGenerator {
  /// Performs a simple check to see if From type can be converted to To type.
  static bool compareTypesSimple(CanQualType From,
                                 CanQualType To,
                                 Sema &S,
                                 SourceLocation Loc,
                                 ExprValueKind FromVK);

  /// The list of Hints generated so far.
  std::vector<FixItHint> Hints;

  /// The number of Conversions fixed. This can be different from the size
  /// of the Hints vector since we allow multiple FixIts per conversion.
  unsigned NumConversionsFixed;

  /// The type of fix applied. If multiple conversions are fixed, corresponds
  /// to the kid of the very first conversion.
  OverloadFixItKind Kind;

  typedef bool (*TypeComparisonFuncTy) (const CanQualType FromTy,
                                        const CanQualType ToTy,
                                        Sema &S,
                                        SourceLocation Loc,
                                        ExprValueKind FromVK);
  /// The type comparison function used to decide if expression FromExpr of
  /// type FromTy can be converted to ToTy. For example, one could check if
  /// an implicit conversion exists. Returns true if comparison exists.
  TypeComparisonFuncTy CompareTypes;

  ConversionFixItGenerator(TypeComparisonFuncTy Foo): NumConversionsFixed(0),
                                                      Kind(OFIK_Undefined),
                                                      CompareTypes(Foo) {}

  ConversionFixItGenerator(): NumConversionsFixed(0),
                              Kind(OFIK_Undefined),
                              CompareTypes(compareTypesSimple) {}

  /// Resets the default conversion checker method.
  void setConversionChecker(TypeComparisonFuncTy Foo) {
    CompareTypes = Foo;
  }

  /// If possible, generates and stores a fix for the given conversion.
  bool tryToFixConversion(const Expr *FromExpr,
                          const QualType FromQTy, const QualType ToQTy,
                          Sema &S);

  void clear() {
    Hints.clear();
    NumConversionsFixed = 0;
  }

  bool isNull() {
    return (NumConversionsFixed == 0);
  }
};

} // endof namespace clang
#endif
