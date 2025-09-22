//===- ExprConcepts.h - C++2a Concepts expressions --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines Expressions and AST nodes for C++2a concepts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPRCONCEPTS_H
#define LLVM_CLANG_AST_EXPRCONCEPTS_H

#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TrailingObjects.h"
#include <string>
#include <utility>

namespace clang {
class ASTStmtReader;
class ASTStmtWriter;

/// \brief Represents the specialization of a concept - evaluates to a prvalue
/// of type bool.
///
/// According to C++2a [expr.prim.id]p3 an id-expression that denotes the
/// specialization of a concept results in a prvalue of type bool.
class ConceptSpecializationExpr final : public Expr {
  friend class ASTReader;
  friend class ASTStmtReader;

private:
  ConceptReference *ConceptRef;

  /// \brief The Implicit Concept Specialization Decl, which holds the template
  /// arguments for this specialization.
  ImplicitConceptSpecializationDecl *SpecDecl;

  /// \brief Information about the satisfaction of the named concept with the
  /// given arguments. If this expression is value dependent, this is to be
  /// ignored.
  ASTConstraintSatisfaction *Satisfaction;

  ConceptSpecializationExpr(const ASTContext &C, ConceptReference *ConceptRef,
                            ImplicitConceptSpecializationDecl *SpecDecl,
                            const ConstraintSatisfaction *Satisfaction);

  ConceptSpecializationExpr(const ASTContext &C, ConceptReference *ConceptRef,
                            ImplicitConceptSpecializationDecl *SpecDecl,
                            const ConstraintSatisfaction *Satisfaction,
                            bool Dependent,
                            bool ContainsUnexpandedParameterPack);
  ConceptSpecializationExpr(EmptyShell Empty);

public:
  static ConceptSpecializationExpr *
  Create(const ASTContext &C, ConceptReference *ConceptRef,
         ImplicitConceptSpecializationDecl *SpecDecl,
         const ConstraintSatisfaction *Satisfaction);

  static ConceptSpecializationExpr *
  Create(const ASTContext &C, ConceptReference *ConceptRef,
         ImplicitConceptSpecializationDecl *SpecDecl,
         const ConstraintSatisfaction *Satisfaction, bool Dependent,
         bool ContainsUnexpandedParameterPack);

  ArrayRef<TemplateArgument> getTemplateArguments() const {
    return SpecDecl->getTemplateArguments();
  }

  ConceptReference *getConceptReference() const { return ConceptRef; }

  ConceptDecl *getNamedConcept() const { return ConceptRef->getNamedConcept(); }

  // FIXME: Several of the following functions can be removed. Instead the
  // caller can directly work with the ConceptReference.
  bool hasExplicitTemplateArgs() const {
    return ConceptRef->hasExplicitTemplateArgs();
  }

  SourceLocation getConceptNameLoc() const {
    return ConceptRef->getConceptNameLoc();
  }
  const ASTTemplateArgumentListInfo *getTemplateArgsAsWritten() const {
    return ConceptRef->getTemplateArgsAsWritten();
  }

  const NestedNameSpecifierLoc &getNestedNameSpecifierLoc() const {
    return ConceptRef->getNestedNameSpecifierLoc();
  }

  SourceLocation getTemplateKWLoc() const {
    return ConceptRef->getTemplateKWLoc();
  }

  NamedDecl *getFoundDecl() const { return ConceptRef->getFoundDecl(); }

  const DeclarationNameInfo &getConceptNameInfo() const {
    return ConceptRef->getConceptNameInfo();
  }

  const ImplicitConceptSpecializationDecl *getSpecializationDecl() const {
    assert(SpecDecl && "Template Argument Decl not initialized");
    return SpecDecl;
  }

  /// \brief Whether or not the concept with the given arguments was satisfied
  /// when the expression was created.
  /// The expression must not be dependent.
  bool isSatisfied() const {
    assert(!isValueDependent() &&
           "isSatisfied called on a dependent ConceptSpecializationExpr");
    return Satisfaction->IsSatisfied;
  }

  /// \brief Get elaborated satisfaction info about the template arguments'
  /// satisfaction of the named concept.
  /// The expression must not be dependent.
  const ASTConstraintSatisfaction &getSatisfaction() const {
    assert(!isValueDependent() &&
           "getSatisfaction called on dependent ConceptSpecializationExpr");
    return *Satisfaction;
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConceptSpecializationExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return ConceptRef->getBeginLoc();
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    return ConceptRef->getEndLoc();
  }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return ConceptRef->getLocation();
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

namespace concepts {

/// \brief A static requirement that can be used in a requires-expression to
/// check properties of types and expression.
class Requirement {
public:
  // Note - simple and compound requirements are both represented by the same
  // class (ExprRequirement).
  enum RequirementKind { RK_Type, RK_Simple, RK_Compound, RK_Nested };
private:
  const RequirementKind Kind;
  // FIXME: use RequirementDependence to model dependence?
  LLVM_PREFERRED_TYPE(bool)
  bool Dependent : 1;
  LLVM_PREFERRED_TYPE(bool)
  bool ContainsUnexpandedParameterPack : 1;
  LLVM_PREFERRED_TYPE(bool)
  bool Satisfied : 1;
public:
  struct SubstitutionDiagnostic {
    StringRef SubstitutedEntity;
    // FIXME: Store diagnostics semantically and not as prerendered strings.
    //  Fixing this probably requires serialization of PartialDiagnostic
    //  objects.
    SourceLocation DiagLoc;
    StringRef DiagMessage;
  };

  Requirement(RequirementKind Kind, bool IsDependent,
              bool ContainsUnexpandedParameterPack, bool IsSatisfied = true) :
      Kind(Kind), Dependent(IsDependent),
      ContainsUnexpandedParameterPack(ContainsUnexpandedParameterPack),
      Satisfied(IsSatisfied) {}

  RequirementKind getKind() const { return Kind; }

  bool isSatisfied() const {
    assert(!Dependent &&
           "isSatisfied can only be called on non-dependent requirements.");
    return Satisfied;
  }

  void setSatisfied(bool IsSatisfied) {
    assert(!Dependent &&
           "setSatisfied can only be called on non-dependent requirements.");
    Satisfied = IsSatisfied;
  }

  void setDependent(bool IsDependent) { Dependent = IsDependent; }
  bool isDependent() const { return Dependent; }

  void setContainsUnexpandedParameterPack(bool Contains) {
    ContainsUnexpandedParameterPack = Contains;
  }
  bool containsUnexpandedParameterPack() const {
    return ContainsUnexpandedParameterPack;
  }
};

/// \brief A requires-expression requirement which queries the existence of a
/// type name or type template specialization ('type' requirements).
class TypeRequirement : public Requirement {
public:
  enum SatisfactionStatus {
      SS_Dependent,
      SS_SubstitutionFailure,
      SS_Satisfied
  };
private:
  llvm::PointerUnion<SubstitutionDiagnostic *, TypeSourceInfo *> Value;
  SatisfactionStatus Status;
public:
  friend ASTStmtReader;
  friend ASTStmtWriter;

  /// \brief Construct a type requirement from a type. If the given type is not
  /// dependent, this indicates that the type exists and the requirement will be
  /// satisfied. Otherwise, the SubstitutionDiagnostic constructor is to be
  /// used.
  TypeRequirement(TypeSourceInfo *T);

  /// \brief Construct a type requirement when the nested name specifier is
  /// invalid due to a bad substitution. The requirement is unsatisfied.
  TypeRequirement(SubstitutionDiagnostic *Diagnostic) :
      Requirement(RK_Type, false, false, false), Value(Diagnostic),
      Status(SS_SubstitutionFailure) {}

  SatisfactionStatus getSatisfactionStatus() const { return Status; }
  void setSatisfactionStatus(SatisfactionStatus Status) {
    this->Status = Status;
  }

  bool isSubstitutionFailure() const {
    return Status == SS_SubstitutionFailure;
  }

  SubstitutionDiagnostic *getSubstitutionDiagnostic() const {
    assert(Status == SS_SubstitutionFailure &&
           "Attempted to get substitution diagnostic when there has been no "
           "substitution failure.");
    return Value.get<SubstitutionDiagnostic *>();
  }

  TypeSourceInfo *getType() const {
    assert(!isSubstitutionFailure() &&
           "Attempted to get type when there has been a substitution failure.");
    return Value.get<TypeSourceInfo *>();
  }

  static bool classof(const Requirement *R) {
    return R->getKind() == RK_Type;
  }
};

/// \brief A requires-expression requirement which queries the validity and
/// properties of an expression ('simple' and 'compound' requirements).
class ExprRequirement : public Requirement {
public:
  enum SatisfactionStatus {
      SS_Dependent,
      SS_ExprSubstitutionFailure,
      SS_NoexceptNotMet,
      SS_TypeRequirementSubstitutionFailure,
      SS_ConstraintsNotSatisfied,
      SS_Satisfied
  };
  class ReturnTypeRequirement {
      llvm::PointerIntPair<
          llvm::PointerUnion<TemplateParameterList *, SubstitutionDiagnostic *>,
          1, bool>
          TypeConstraintInfo;
  public:
      friend ASTStmtReader;
      friend ASTStmtWriter;

      /// \brief No return type requirement was specified.
      ReturnTypeRequirement() : TypeConstraintInfo(nullptr, false) {}

      /// \brief A return type requirement was specified but it was a
      /// substitution failure.
      ReturnTypeRequirement(SubstitutionDiagnostic *SubstDiag) :
          TypeConstraintInfo(SubstDiag, false) {}

      /// \brief A 'type constraint' style return type requirement.
      /// \param TPL an invented template parameter list containing a single
      /// type parameter with a type-constraint.
      // TODO: Can we maybe not save the whole template parameter list and just
      //  the type constraint? Saving the whole TPL makes it easier to handle in
      //  serialization but is less elegant.
      ReturnTypeRequirement(TemplateParameterList *TPL);

      bool isDependent() const {
        return TypeConstraintInfo.getInt();
      }

      bool containsUnexpandedParameterPack() const {
        if (!isTypeConstraint())
          return false;
        return getTypeConstraintTemplateParameterList()
                ->containsUnexpandedParameterPack();
      }

      bool isEmpty() const {
        return TypeConstraintInfo.getPointer().isNull();
      }

      bool isSubstitutionFailure() const {
        return !isEmpty() &&
            TypeConstraintInfo.getPointer().is<SubstitutionDiagnostic *>();
      }

      bool isTypeConstraint() const {
        return !isEmpty() &&
            TypeConstraintInfo.getPointer().is<TemplateParameterList *>();
      }

      SubstitutionDiagnostic *getSubstitutionDiagnostic() const {
        assert(isSubstitutionFailure());
        return TypeConstraintInfo.getPointer().get<SubstitutionDiagnostic *>();
      }

      const TypeConstraint *getTypeConstraint() const;

      TemplateParameterList *getTypeConstraintTemplateParameterList() const {
        assert(isTypeConstraint());
        return TypeConstraintInfo.getPointer().get<TemplateParameterList *>();
      }
  };
private:
  llvm::PointerUnion<Expr *, SubstitutionDiagnostic *> Value;
  SourceLocation NoexceptLoc; // May be empty if noexcept wasn't specified.
  ReturnTypeRequirement TypeReq;
  ConceptSpecializationExpr *SubstitutedConstraintExpr;
  SatisfactionStatus Status;
public:
  friend ASTStmtReader;
  friend ASTStmtWriter;

  /// \brief Construct a compound requirement.
  /// \param E the expression which is checked by this requirement.
  /// \param IsSimple whether this was a simple requirement in source.
  /// \param NoexceptLoc the location of the noexcept keyword, if it was
  /// specified, otherwise an empty location.
  /// \param Req the requirement for the type of the checked expression.
  /// \param Status the satisfaction status of this requirement.
  ExprRequirement(
      Expr *E, bool IsSimple, SourceLocation NoexceptLoc,
      ReturnTypeRequirement Req, SatisfactionStatus Status,
      ConceptSpecializationExpr *SubstitutedConstraintExpr = nullptr);

  /// \brief Construct a compound requirement whose expression was a
  /// substitution failure. The requirement is not satisfied.
  /// \param E the diagnostic emitted while instantiating the original
  /// expression.
  /// \param IsSimple whether this was a simple requirement in source.
  /// \param NoexceptLoc the location of the noexcept keyword, if it was
  /// specified, otherwise an empty location.
  /// \param Req the requirement for the type of the checked expression (omit
  /// if no requirement was specified).
  ExprRequirement(SubstitutionDiagnostic *E, bool IsSimple,
                  SourceLocation NoexceptLoc, ReturnTypeRequirement Req = {});

  bool isSimple() const { return getKind() == RK_Simple; }
  bool isCompound() const { return getKind() == RK_Compound; }

  bool hasNoexceptRequirement() const { return NoexceptLoc.isValid(); }
  SourceLocation getNoexceptLoc() const { return NoexceptLoc; }

  SatisfactionStatus getSatisfactionStatus() const { return Status; }

  bool isExprSubstitutionFailure() const {
    return Status == SS_ExprSubstitutionFailure;
  }

  const ReturnTypeRequirement &getReturnTypeRequirement() const {
    return TypeReq;
  }

  ConceptSpecializationExpr *
  getReturnTypeRequirementSubstitutedConstraintExpr() const {
    assert(Status >= SS_TypeRequirementSubstitutionFailure);
    return SubstitutedConstraintExpr;
  }

  SubstitutionDiagnostic *getExprSubstitutionDiagnostic() const {
    assert(isExprSubstitutionFailure() &&
           "Attempted to get expression substitution diagnostic when there has "
           "been no expression substitution failure");
    return Value.get<SubstitutionDiagnostic *>();
  }

  Expr *getExpr() const {
    assert(!isExprSubstitutionFailure() &&
           "ExprRequirement has no expression because there has been a "
           "substitution failure.");
    return Value.get<Expr *>();
  }

  static bool classof(const Requirement *R) {
    return R->getKind() == RK_Compound || R->getKind() == RK_Simple;
  }
};

/// \brief A requires-expression requirement which is satisfied when a general
/// constraint expression is satisfied ('nested' requirements).
class NestedRequirement : public Requirement {
  Expr *Constraint = nullptr;
  const ASTConstraintSatisfaction *Satisfaction = nullptr;
  bool HasInvalidConstraint = false;
  StringRef InvalidConstraintEntity;

public:
  friend ASTStmtReader;
  friend ASTStmtWriter;

  NestedRequirement(Expr *Constraint)
      : Requirement(RK_Nested, /*IsDependent=*/true,
                    Constraint->containsUnexpandedParameterPack()),
        Constraint(Constraint) {
    assert(Constraint->isInstantiationDependent() &&
           "Nested requirement with non-dependent constraint must be "
           "constructed with a ConstraintSatisfaction object");
  }

  NestedRequirement(ASTContext &C, Expr *Constraint,
                    const ConstraintSatisfaction &Satisfaction)
      : Requirement(RK_Nested, Constraint->isInstantiationDependent(),
                    Constraint->containsUnexpandedParameterPack(),
                    Satisfaction.IsSatisfied),
        Constraint(Constraint),
        Satisfaction(ASTConstraintSatisfaction::Create(C, Satisfaction)) {}

  NestedRequirement(StringRef InvalidConstraintEntity,
                    const ASTConstraintSatisfaction *Satisfaction)
      : Requirement(RK_Nested,
                    /*IsDependent=*/false,
                    /*ContainsUnexpandedParameterPack*/ false,
                    Satisfaction->IsSatisfied),
        Satisfaction(Satisfaction), HasInvalidConstraint(true),
        InvalidConstraintEntity(InvalidConstraintEntity) {}

  NestedRequirement(ASTContext &C, StringRef InvalidConstraintEntity,
                    const ConstraintSatisfaction &Satisfaction)
      : NestedRequirement(InvalidConstraintEntity,
                          ASTConstraintSatisfaction::Create(C, Satisfaction)) {}

  bool hasInvalidConstraint() const { return HasInvalidConstraint; }

  StringRef getInvalidConstraintEntity() {
    assert(hasInvalidConstraint());
    return InvalidConstraintEntity;
  }

  Expr *getConstraintExpr() const {
    assert(!hasInvalidConstraint() &&
           "getConstraintExpr() may not be called "
           "on nested requirements with invalid constraint.");
    return Constraint;
  }

  const ASTConstraintSatisfaction &getConstraintSatisfaction() const {
    return *Satisfaction;
  }

  static bool classof(const Requirement *R) {
    return R->getKind() == RK_Nested;
  }
};

using EntityPrinter = llvm::function_ref<void(llvm::raw_ostream &)>;

/// \brief create a Requirement::SubstitutionDiagnostic with only a
/// SubstitutedEntity and DiagLoc using Sema's allocator.
Requirement::SubstitutionDiagnostic *
createSubstDiagAt(Sema &S, SourceLocation Location, EntityPrinter Printer);

} // namespace concepts

/// C++2a [expr.prim.req]:
///     A requires-expression provides a concise way to express requirements on
///     template arguments. A requirement is one that can be checked by name
///     lookup (6.4) or by checking properties of types and expressions.
///     [...]
///     A requires-expression is a prvalue of type bool [...]
class RequiresExpr final : public Expr,
    llvm::TrailingObjects<RequiresExpr, ParmVarDecl *,
                          concepts::Requirement *> {
  friend TrailingObjects;
  friend class ASTStmtReader;

  unsigned NumLocalParameters;
  unsigned NumRequirements;
  RequiresExprBodyDecl *Body;
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
  SourceLocation RBraceLoc;

  unsigned numTrailingObjects(OverloadToken<ParmVarDecl *>) const {
    return NumLocalParameters;
  }

  unsigned numTrailingObjects(OverloadToken<concepts::Requirement *>) const {
    return NumRequirements;
  }

  RequiresExpr(ASTContext &C, SourceLocation RequiresKWLoc,
               RequiresExprBodyDecl *Body, SourceLocation LParenLoc,
               ArrayRef<ParmVarDecl *> LocalParameters,
               SourceLocation RParenLoc,
               ArrayRef<concepts::Requirement *> Requirements,
               SourceLocation RBraceLoc);
  RequiresExpr(ASTContext &C, EmptyShell Empty, unsigned NumLocalParameters,
               unsigned NumRequirements);

public:
  static RequiresExpr *Create(ASTContext &C, SourceLocation RequiresKWLoc,
                              RequiresExprBodyDecl *Body,
                              SourceLocation LParenLoc,
                              ArrayRef<ParmVarDecl *> LocalParameters,
                              SourceLocation RParenLoc,
                              ArrayRef<concepts::Requirement *> Requirements,
                              SourceLocation RBraceLoc);
  static RequiresExpr *
  Create(ASTContext &C, EmptyShell Empty, unsigned NumLocalParameters,
         unsigned NumRequirements);

  ArrayRef<ParmVarDecl *> getLocalParameters() const {
    return {getTrailingObjects<ParmVarDecl *>(), NumLocalParameters};
  }

  RequiresExprBodyDecl *getBody() const { return Body; }

  ArrayRef<concepts::Requirement *> getRequirements() const {
    return {getTrailingObjects<concepts::Requirement *>(), NumRequirements};
  }

  /// \brief Whether or not the requires clause is satisfied.
  /// The expression must not be dependent.
  bool isSatisfied() const {
    assert(!isValueDependent()
           && "isSatisfied called on a dependent RequiresExpr");
    return RequiresExprBits.IsSatisfied;
  }

  void setSatisfied(bool IsSatisfied) {
    assert(!isValueDependent() &&
           "setSatisfied called on a dependent RequiresExpr");
    RequiresExprBits.IsSatisfied = IsSatisfied;
  }

  SourceLocation getRequiresKWLoc() const {
    return RequiresExprBits.RequiresKWLoc;
  }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == RequiresExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return RequiresExprBits.RequiresKWLoc;
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return RBraceLoc;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

} // namespace clang

#endif // LLVM_CLANG_AST_EXPRCONCEPTS_H
