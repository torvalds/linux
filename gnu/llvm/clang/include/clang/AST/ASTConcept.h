//===--- ASTConcept.h - Concepts Related AST Data Structures ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file provides AST data structures related to concepts.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTCONCEPT_H
#define LLVM_CLANG_AST_ASTCONCEPT_H

#include "clang/AST/DeclarationName.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <utility>

namespace clang {

class ConceptDecl;
class Expr;
class NamedDecl;
struct PrintingPolicy;

/// The result of a constraint satisfaction check, containing the necessary
/// information to diagnose an unsatisfied constraint.
class ConstraintSatisfaction : public llvm::FoldingSetNode {
  // The template-like entity that 'owns' the constraint checked here (can be a
  // constrained entity or a concept).
  const NamedDecl *ConstraintOwner = nullptr;
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;

public:

  ConstraintSatisfaction() = default;

  ConstraintSatisfaction(const NamedDecl *ConstraintOwner,
                         ArrayRef<TemplateArgument> TemplateArgs) :
      ConstraintOwner(ConstraintOwner), TemplateArgs(TemplateArgs.begin(),
                                                     TemplateArgs.end()) { }

  using SubstitutionDiagnostic = std::pair<SourceLocation, StringRef>;
  using Detail = llvm::PointerUnion<Expr *, SubstitutionDiagnostic *>;

  bool IsSatisfied = false;
  bool ContainsErrors = false;

  /// \brief The substituted constraint expr, if the template arguments could be
  /// substituted into them, or a diagnostic if substitution resulted in an
  /// invalid expression.
  llvm::SmallVector<Detail, 4> Details;

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &C) {
    Profile(ID, C, ConstraintOwner, TemplateArgs);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &C,
                      const NamedDecl *ConstraintOwner,
                      ArrayRef<TemplateArgument> TemplateArgs);

  bool HasSubstitutionFailure() {
    for (const auto &Detail : Details)
      if (Detail.dyn_cast<SubstitutionDiagnostic *>())
        return true;
    return false;
  }
};

/// Pairs of unsatisfied atomic constraint expressions along with the
/// substituted constraint expr, if the template arguments could be
/// substituted into them, or a diagnostic if substitution resulted in
/// an invalid expression.
using UnsatisfiedConstraintRecord =
    llvm::PointerUnion<Expr *, std::pair<SourceLocation, StringRef> *>;

/// \brief The result of a constraint satisfaction check, containing the
/// necessary information to diagnose an unsatisfied constraint.
///
/// This is safe to store in an AST node, as opposed to ConstraintSatisfaction.
struct ASTConstraintSatisfaction final :
    llvm::TrailingObjects<ASTConstraintSatisfaction,
                          UnsatisfiedConstraintRecord> {
  std::size_t NumRecords;
  bool IsSatisfied : 1;
  bool ContainsErrors : 1;

  const UnsatisfiedConstraintRecord *begin() const {
    return getTrailingObjects<UnsatisfiedConstraintRecord>();
  }

  const UnsatisfiedConstraintRecord *end() const {
    return getTrailingObjects<UnsatisfiedConstraintRecord>() + NumRecords;
  }

  ASTConstraintSatisfaction(const ASTContext &C,
                            const ConstraintSatisfaction &Satisfaction);
  ASTConstraintSatisfaction(const ASTContext &C,
                            const ASTConstraintSatisfaction &Satisfaction);

  static ASTConstraintSatisfaction *
  Create(const ASTContext &C, const ConstraintSatisfaction &Satisfaction);
  static ASTConstraintSatisfaction *
  Rebuild(const ASTContext &C, const ASTConstraintSatisfaction &Satisfaction);
};

/// A reference to a concept and its template args, as it appears in the code.
///
/// Examples:
///   template <int X> requires is_even<X> int half = X/2;
///                             ~~~~~~~~~~ (in ConceptSpecializationExpr)
///
///   std::input_iterator auto I = Container.begin();
///   ~~~~~~~~~~~~~~~~~~~ (in AutoTypeLoc)
///
///   template <std::derives_from<Expr> T> void dump();
///             ~~~~~~~~~~~~~~~~~~~~~~~ (in TemplateTypeParmDecl)
class ConceptReference {
  // \brief The optional nested name specifier used when naming the concept.
  NestedNameSpecifierLoc NestedNameSpec;

  /// \brief The location of the template keyword, if specified when naming the
  /// concept.
  SourceLocation TemplateKWLoc;

  /// \brief The concept name used.
  DeclarationNameInfo ConceptName;

  /// \brief The declaration found by name lookup when the expression was
  /// created.
  /// Can differ from NamedConcept when, for example, the concept was found
  /// through a UsingShadowDecl.
  NamedDecl *FoundDecl;

  /// \brief The concept named.
  ConceptDecl *NamedConcept;

  /// \brief The template argument list source info used to specialize the
  /// concept.
  const ASTTemplateArgumentListInfo *ArgsAsWritten;

  ConceptReference(NestedNameSpecifierLoc NNS, SourceLocation TemplateKWLoc,
                   DeclarationNameInfo ConceptNameInfo, NamedDecl *FoundDecl,
                   ConceptDecl *NamedConcept,
                   const ASTTemplateArgumentListInfo *ArgsAsWritten)
      : NestedNameSpec(NNS), TemplateKWLoc(TemplateKWLoc),
        ConceptName(ConceptNameInfo), FoundDecl(FoundDecl),
        NamedConcept(NamedConcept), ArgsAsWritten(ArgsAsWritten) {}

public:
  static ConceptReference *
  Create(const ASTContext &C, NestedNameSpecifierLoc NNS,
         SourceLocation TemplateKWLoc, DeclarationNameInfo ConceptNameInfo,
         NamedDecl *FoundDecl, ConceptDecl *NamedConcept,
         const ASTTemplateArgumentListInfo *ArgsAsWritten);

  const NestedNameSpecifierLoc &getNestedNameSpecifierLoc() const {
    return NestedNameSpec;
  }

  const DeclarationNameInfo &getConceptNameInfo() const { return ConceptName; }

  SourceLocation getConceptNameLoc() const {
    return getConceptNameInfo().getLoc();
  }

  SourceLocation getTemplateKWLoc() const { return TemplateKWLoc; }

  SourceLocation getLocation() const { return getConceptNameLoc(); }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    // Note that if the qualifier is null the template KW must also be null.
    if (auto QualifierLoc = getNestedNameSpecifierLoc())
      return QualifierLoc.getBeginLoc();
    return getConceptNameInfo().getBeginLoc();
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    return getTemplateArgsAsWritten() &&
                   getTemplateArgsAsWritten()->getRAngleLoc().isValid()
               ? getTemplateArgsAsWritten()->getRAngleLoc()
               : getConceptNameInfo().getEndLoc();
  }

  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(getBeginLoc(), getEndLoc());
  }

  NamedDecl *getFoundDecl() const {
    return FoundDecl;
  }

  ConceptDecl *getNamedConcept() const {
    return NamedConcept;
  }

  const ASTTemplateArgumentListInfo *getTemplateArgsAsWritten() const {
    return ArgsAsWritten;
  }

  /// \brief Whether or not template arguments were explicitly specified in the
  /// concept reference (they might not be in type constraints, for example)
  bool hasExplicitTemplateArgs() const {
    return ArgsAsWritten != nullptr;
  }

  void print(llvm::raw_ostream &OS, const PrintingPolicy &Policy) const;
  void dump() const;
  void dump(llvm::raw_ostream &) const;
};

/// Models the abbreviated syntax to constrain a template type parameter:
///   template <convertible_to<string> T> void print(T object);
///             ~~~~~~~~~~~~~~~~~~~~~~
/// Semantically, this adds an "immediately-declared constraint" with extra arg:
///    requires convertible_to<T, string>
///
/// In the C++ grammar, a type-constraint is also used for auto types:
///    convertible_to<string> auto X = ...;
/// We do *not* model these as TypeConstraints, but AutoType(Loc) directly.
class TypeConstraint {
  /// \brief The immediately-declared constraint expression introduced by this
  /// type-constraint.
  Expr *ImmediatelyDeclaredConstraint = nullptr;
  ConceptReference *ConceptRef;

public:
  TypeConstraint(ConceptReference *ConceptRef,
                 Expr *ImmediatelyDeclaredConstraint)
      : ImmediatelyDeclaredConstraint(ImmediatelyDeclaredConstraint),
        ConceptRef(ConceptRef) {}

  /// \brief Get the immediately-declared constraint expression introduced by
  /// this type-constraint, that is - the constraint expression that is added to
  /// the associated constraints of the enclosing declaration in practice.
  Expr *getImmediatelyDeclaredConstraint() const {
    return ImmediatelyDeclaredConstraint;
  }

  ConceptReference *getConceptReference() const { return ConceptRef; }

  // FIXME: Instead of using these concept related functions the callers should
  // directly work with the corresponding ConceptReference.
  ConceptDecl *getNamedConcept() const { return ConceptRef->getNamedConcept(); }

  SourceLocation getConceptNameLoc() const {
    return ConceptRef->getConceptNameLoc();
  }

  bool hasExplicitTemplateArgs() const {
    return ConceptRef->hasExplicitTemplateArgs();
  }

  const ASTTemplateArgumentListInfo *getTemplateArgsAsWritten() const {
    return ConceptRef->getTemplateArgsAsWritten();
  }

  SourceLocation getTemplateKWLoc() const {
    return ConceptRef->getTemplateKWLoc();
  }

  NamedDecl *getFoundDecl() const { return ConceptRef->getFoundDecl(); }

  const NestedNameSpecifierLoc &getNestedNameSpecifierLoc() const {
    return ConceptRef->getNestedNameSpecifierLoc();
  }

  const DeclarationNameInfo &getConceptNameInfo() const {
    return ConceptRef->getConceptNameInfo();
  }

  void print(llvm::raw_ostream &OS, const PrintingPolicy &Policy) const {
    ConceptRef->print(OS, Policy);
  }
};

} // clang

#endif // LLVM_CLANG_AST_ASTCONCEPT_H
