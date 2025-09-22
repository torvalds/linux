//===--- ExprOpenMP.h - Classes for representing expressions ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPROPENMP_H
#define LLVM_CLANG_AST_EXPROPENMP_H

#include "clang/AST/ComputeDependence.h"
#include "clang/AST/Expr.h"

namespace clang {
/// An explicit cast in C or a C-style cast in C++, which uses the syntax
/// ([s1][s2]...[sn])expr. For example: @c ([3][3])f.
class OMPArrayShapingExpr final
    : public Expr,
      private llvm::TrailingObjects<OMPArrayShapingExpr, Expr *, SourceRange> {
  friend TrailingObjects;
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  /// Base node.
  SourceLocation LPLoc; /// The location of the left paren
  SourceLocation RPLoc; /// The location of the right paren
  unsigned NumDims = 0; /// Number of dimensions in the shaping expression.

  /// Construct full expression.
  OMPArrayShapingExpr(QualType ExprTy, Expr *Op, SourceLocation L,
                      SourceLocation R, ArrayRef<Expr *> Dims);

  /// Construct an empty expression.
  explicit OMPArrayShapingExpr(EmptyShell Shell, unsigned NumDims)
      : Expr(OMPArrayShapingExprClass, Shell), NumDims(NumDims) {}

  /// Sets the dimensions for the array shaping.
  void setDimensions(ArrayRef<Expr *> Dims);

  /// Sets the base expression for array shaping operation.
  void setBase(Expr *Op) { getTrailingObjects<Expr *>()[NumDims] = Op; }

  /// Sets source ranges for the brackets in the array shaping operation.
  void setBracketsRanges(ArrayRef<SourceRange> BR);

  unsigned numTrailingObjects(OverloadToken<Expr *>) const {
    // Add an extra one for the base expression.
    return NumDims + 1;
  }

  unsigned numTrailingObjects(OverloadToken<SourceRange>) const {
    return NumDims;
  }

public:
  static OMPArrayShapingExpr *Create(const ASTContext &Context, QualType T,
                                     Expr *Op, SourceLocation L,
                                     SourceLocation R, ArrayRef<Expr *> Dims,
                                     ArrayRef<SourceRange> BracketRanges);

  static OMPArrayShapingExpr *CreateEmpty(const ASTContext &Context,
                                          unsigned NumDims);

  SourceLocation getLParenLoc() const { return LPLoc; }
  void setLParenLoc(SourceLocation L) { LPLoc = L; }

  SourceLocation getRParenLoc() const { return RPLoc; }
  void setRParenLoc(SourceLocation L) { RPLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LPLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getBase()->getEndLoc();
  }

  /// Fetches the dimensions for array shaping expression.
  ArrayRef<Expr *> getDimensions() const {
    return llvm::ArrayRef(getTrailingObjects<Expr *>(), NumDims);
  }

  /// Fetches source ranges for the brackets os the array shaping expression.
  ArrayRef<SourceRange> getBracketsRanges() const {
    return llvm::ArrayRef(getTrailingObjects<SourceRange>(), NumDims);
  }

  /// Fetches base expression of array shaping expression.
  Expr *getBase() { return getTrailingObjects<Expr *>()[NumDims]; }
  const Expr *getBase() const { return getTrailingObjects<Expr *>()[NumDims]; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPArrayShapingExprClass;
  }

  // Iterators
  child_range children() {
    Stmt **Begin = reinterpret_cast<Stmt **>(getTrailingObjects<Expr *>());
    return child_range(Begin, Begin + NumDims + 1);
  }
  const_child_range children() const {
    Stmt *const *Begin =
        reinterpret_cast<Stmt *const *>(getTrailingObjects<Expr *>());
    return const_child_range(Begin, Begin + NumDims + 1);
  }
};

/// Helper expressions and declaration for OMPIteratorExpr class for each
/// iteration space.
struct OMPIteratorHelperData {
  /// Internal normalized counter.
  VarDecl *CounterVD = nullptr;
  /// Normalized upper bound. Normalized loop iterates from 0 to Upper with
  /// step 1.
  Expr *Upper = nullptr;
  /// Update expression for the originally specified iteration variable,
  /// calculated as VD = Begin + CounterVD * Step;
  Expr *Update = nullptr;
  /// Updater for the internal counter: ++CounterVD;
  Expr *CounterUpdate = nullptr;
};

/// OpenMP 5.0 [2.1.6 Iterators]
/// Iterators are identifiers that expand to multiple values in the clause on
/// which they appear.
/// The syntax of the iterator modifier is as follows:
/// \code
/// iterator(iterators-definition)
/// \endcode
/// where iterators-definition is one of the following:
/// \code
/// iterator-specifier [, iterators-definition ]
/// \endcode
/// where iterator-specifier is one of the following:
/// \code
/// [ iterator-type ] identifier = range-specification
/// \endcode
/// where identifier is a base language identifier.
/// iterator-type is a type name.
/// range-specification is of the form begin:end[:step], where begin and end are
/// expressions for which their types can be converted to iterator-type and step
/// is an integral expression.
/// In an iterator-specifier, if the iterator-type is not specified then the
/// type of that iterator is of int type.
/// The iterator-type must be an integral or pointer type.
/// The iterator-type must not be const qualified.
class OMPIteratorExpr final
    : public Expr,
      private llvm::TrailingObjects<OMPIteratorExpr, Decl *, Expr *,
                                    SourceLocation, OMPIteratorHelperData> {
public:
  /// Iterator range representation begin:end[:step].
  struct IteratorRange {
    Expr *Begin = nullptr;
    Expr *End = nullptr;
    Expr *Step = nullptr;
  };
  /// Iterator definition representation.
  struct IteratorDefinition {
    Decl *IteratorDecl = nullptr;
    IteratorRange Range;
    SourceLocation AssignmentLoc;
    SourceLocation ColonLoc, SecondColonLoc;
  };

private:
  friend TrailingObjects;
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

  /// Offset in the list of expressions for subelements of the ranges.
  enum class RangeExprOffset {
    Begin = 0,
    End = 1,
    Step = 2,
    Total = 3,
  };
  /// Offset in the list of locations for subelements of colon symbols
  /// locations.
  enum class RangeLocOffset {
    AssignLoc = 0,
    FirstColonLoc = 1,
    SecondColonLoc = 2,
    Total = 3,
  };
  /// Location of 'iterator' keyword.
  SourceLocation IteratorKwLoc;
  /// Location of '('.
  SourceLocation LPLoc;
  /// Location of ')'.
  SourceLocation RPLoc;
  /// Number of iterator definitions.
  unsigned NumIterators = 0;

  OMPIteratorExpr(QualType ExprTy, SourceLocation IteratorKwLoc,
                  SourceLocation L, SourceLocation R,
                  ArrayRef<IteratorDefinition> Data,
                  ArrayRef<OMPIteratorHelperData> Helpers);

  /// Construct an empty expression.
  explicit OMPIteratorExpr(EmptyShell Shell, unsigned NumIterators)
      : Expr(OMPIteratorExprClass, Shell), NumIterators(NumIterators) {}

  /// Sets basic declaration for the specified iterator definition.
  void setIteratorDeclaration(unsigned I, Decl *D);

  /// Sets the location of the assignment symbol for the specified iterator
  /// definition.
  void setAssignmentLoc(unsigned I, SourceLocation Loc);

  /// Sets begin, end and optional step expressions for specified iterator
  /// definition.
  void setIteratorRange(unsigned I, Expr *Begin, SourceLocation ColonLoc,
                        Expr *End, SourceLocation SecondColonLoc, Expr *Step);

  /// Sets helpers for the specified iteration space.
  void setHelper(unsigned I, const OMPIteratorHelperData &D);

  unsigned numTrailingObjects(OverloadToken<Decl *>) const {
    return NumIterators;
  }

  unsigned numTrailingObjects(OverloadToken<Expr *>) const {
    return NumIterators * static_cast<int>(RangeExprOffset::Total);
  }

  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return NumIterators * static_cast<int>(RangeLocOffset::Total);
  }

public:
  static OMPIteratorExpr *Create(const ASTContext &Context, QualType T,
                                 SourceLocation IteratorKwLoc, SourceLocation L,
                                 SourceLocation R,
                                 ArrayRef<IteratorDefinition> Data,
                                 ArrayRef<OMPIteratorHelperData> Helpers);

  static OMPIteratorExpr *CreateEmpty(const ASTContext &Context,
                                      unsigned NumIterators);

  SourceLocation getLParenLoc() const { return LPLoc; }
  void setLParenLoc(SourceLocation L) { LPLoc = L; }

  SourceLocation getRParenLoc() const { return RPLoc; }
  void setRParenLoc(SourceLocation L) { RPLoc = L; }

  SourceLocation getIteratorKwLoc() const { return IteratorKwLoc; }
  void setIteratorKwLoc(SourceLocation L) { IteratorKwLoc = L; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return IteratorKwLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RPLoc; }

  /// Gets the iterator declaration for the given iterator.
  Decl *getIteratorDecl(unsigned I);
  const Decl *getIteratorDecl(unsigned I) const {
    return const_cast<OMPIteratorExpr *>(this)->getIteratorDecl(I);
  }

  /// Gets the iterator range for the given iterator.
  IteratorRange getIteratorRange(unsigned I);
  const IteratorRange getIteratorRange(unsigned I) const {
    return const_cast<OMPIteratorExpr *>(this)->getIteratorRange(I);
  }

  /// Gets the location of '=' for the given iterator definition.
  SourceLocation getAssignLoc(unsigned I) const;
  /// Gets the location of the first ':' in the range for the given iterator
  /// definition.
  SourceLocation getColonLoc(unsigned I) const;
  /// Gets the location of the second ':' (if any) in the range for the given
  /// iteratori definition.
  SourceLocation getSecondColonLoc(unsigned I) const;

  /// Returns number of iterator definitions.
  unsigned numOfIterators() const { return NumIterators; }

  /// Fetches helper data for the specified iteration space.
  OMPIteratorHelperData &getHelper(unsigned I);
  const OMPIteratorHelperData &getHelper(unsigned I) const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPIteratorExprClass;
  }

  // Iterators
  child_range children() {
    Stmt **Begin = reinterpret_cast<Stmt **>(getTrailingObjects<Expr *>());
    return child_range(
        Begin, Begin + NumIterators * static_cast<int>(RangeExprOffset::Total));
  }
  const_child_range children() const {
    Stmt *const *Begin =
        reinterpret_cast<Stmt *const *>(getTrailingObjects<Expr *>());
    return const_child_range(
        Begin, Begin + NumIterators * static_cast<int>(RangeExprOffset::Total));
  }
};

} // end namespace clang

#endif
