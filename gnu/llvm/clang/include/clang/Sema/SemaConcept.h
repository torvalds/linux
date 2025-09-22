//===-- SemaConcept.h - Semantic Analysis for Constraints and Concepts ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
//  This file provides semantic analysis for C++ constraints and concepts.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMACONCEPT_H
#define LLVM_CLANG_SEMA_SEMACONCEPT_H
#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
#include <string>
#include <utility>

namespace clang {
class Sema;

enum { ConstraintAlignment = 8 };

struct alignas(ConstraintAlignment) AtomicConstraint {
  const Expr *ConstraintExpr;
  std::optional<ArrayRef<TemplateArgumentLoc>> ParameterMapping;

  AtomicConstraint(Sema &S, const Expr *ConstraintExpr) :
      ConstraintExpr(ConstraintExpr) { };

  bool hasMatchingParameterMapping(ASTContext &C,
                                   const AtomicConstraint &Other) const {
    if (!ParameterMapping != !Other.ParameterMapping)
      return false;
    if (!ParameterMapping)
      return true;
    if (ParameterMapping->size() != Other.ParameterMapping->size())
      return false;

    for (unsigned I = 0, S = ParameterMapping->size(); I < S; ++I) {
      llvm::FoldingSetNodeID IDA, IDB;
      C.getCanonicalTemplateArgument((*ParameterMapping)[I].getArgument())
          .Profile(IDA, C);
      C.getCanonicalTemplateArgument((*Other.ParameterMapping)[I].getArgument())
          .Profile(IDB, C);
      if (IDA != IDB)
        return false;
    }
    return true;
  }

  bool subsumes(ASTContext &C, const AtomicConstraint &Other) const {
    // C++ [temp.constr.order] p2
    //   - an atomic constraint A subsumes another atomic constraint B
    //     if and only if the A and B are identical [...]
    //
    // C++ [temp.constr.atomic] p2
    //   Two atomic constraints are identical if they are formed from the
    //   same expression and the targets of the parameter mappings are
    //   equivalent according to the rules for expressions [...]

    // We do not actually substitute the parameter mappings into the
    // constraint expressions, therefore the constraint expressions are
    // the originals, and comparing them will suffice.
    if (ConstraintExpr != Other.ConstraintExpr)
      return false;

    // Check that the parameter lists are identical
    return hasMatchingParameterMapping(C, Other);
  }
};

struct alignas(ConstraintAlignment) FoldExpandedConstraint;

using NormalFormConstraint =
    llvm::PointerUnion<AtomicConstraint *, FoldExpandedConstraint *>;
struct NormalizedConstraint;
using NormalForm =
    llvm::SmallVector<llvm::SmallVector<NormalFormConstraint, 2>, 4>;

// A constraint is in conjunctive normal form when it is a conjunction of
// clauses where each clause is a disjunction of atomic constraints. For atomic
// constraints A, B, and C, the constraint A  ∧ (B  ∨ C) is in conjunctive
// normal form.
NormalForm makeCNF(const NormalizedConstraint &Normalized);

// A constraint is in disjunctive normal form when it is a disjunction of
// clauses where each clause is a conjunction of atomic constraints. For atomic
// constraints A, B, and C, the disjunctive normal form of the constraint A
//  ∧ (B  ∨ C) is (A  ∧ B)  ∨ (A  ∧ C).
NormalForm makeDNF(const NormalizedConstraint &Normalized);

struct alignas(ConstraintAlignment) NormalizedConstraintPair;

/// \brief A normalized constraint, as defined in C++ [temp.constr.normal], is
/// either an atomic constraint, a conjunction of normalized constraints or a
/// disjunction of normalized constraints.
struct NormalizedConstraint {
  friend class Sema;

  enum CompoundConstraintKind { CCK_Conjunction, CCK_Disjunction };

  using CompoundConstraint = llvm::PointerIntPair<NormalizedConstraintPair *, 1,
                                                  CompoundConstraintKind>;

  llvm::PointerUnion<AtomicConstraint *, FoldExpandedConstraint *,
                     CompoundConstraint>
      Constraint;

  NormalizedConstraint(AtomicConstraint *C): Constraint{C} { };
  NormalizedConstraint(FoldExpandedConstraint *C) : Constraint{C} {};

  NormalizedConstraint(ASTContext &C, NormalizedConstraint LHS,
                       NormalizedConstraint RHS, CompoundConstraintKind Kind);

  NormalizedConstraint(ASTContext &C, const NormalizedConstraint &Other);
  NormalizedConstraint(NormalizedConstraint &&Other):
      Constraint(Other.Constraint) {
    Other.Constraint = nullptr;
  }
  NormalizedConstraint &operator=(const NormalizedConstraint &Other) = delete;
  NormalizedConstraint &operator=(NormalizedConstraint &&Other) {
    if (&Other != this) {
      NormalizedConstraint Temp(std::move(Other));
      std::swap(Constraint, Temp.Constraint);
    }
    return *this;
  }

  bool isAtomic() const { return Constraint.is<AtomicConstraint *>(); }
  bool isFoldExpanded() const {
    return Constraint.is<FoldExpandedConstraint *>();
  }
  bool isCompound() const { return Constraint.is<CompoundConstraint>(); }

  CompoundConstraintKind getCompoundKind() const {
    assert(isCompound() && "getCompoundKind on a non-compound constraint..");
    return Constraint.get<CompoundConstraint>().getInt();
  }

  NormalizedConstraint &getLHS() const;
  NormalizedConstraint &getRHS() const;

  AtomicConstraint *getAtomicConstraint() const {
    assert(isAtomic() &&
           "getAtomicConstraint called on non-atomic constraint.");
    return Constraint.get<AtomicConstraint *>();
  }

  FoldExpandedConstraint *getFoldExpandedConstraint() const {
    assert(isFoldExpanded() &&
           "getFoldExpandedConstraint called on non-fold-expanded constraint.");
    return Constraint.get<FoldExpandedConstraint *>();
  }

private:
  static std::optional<NormalizedConstraint>
  fromConstraintExprs(Sema &S, NamedDecl *D, ArrayRef<const Expr *> E);
  static std::optional<NormalizedConstraint>
  fromConstraintExpr(Sema &S, NamedDecl *D, const Expr *E);
};

struct alignas(ConstraintAlignment) NormalizedConstraintPair {
  NormalizedConstraint LHS, RHS;
};

struct alignas(ConstraintAlignment) FoldExpandedConstraint {
  enum class FoldOperatorKind { And, Or } Kind;
  NormalizedConstraint Constraint;
  const Expr *Pattern;

  FoldExpandedConstraint(FoldOperatorKind K, NormalizedConstraint C,
                         const Expr *Pattern)
      : Kind(K), Constraint(std::move(C)), Pattern(Pattern) {};

  template <typename AtomicSubsumptionEvaluator>
  bool subsumes(const FoldExpandedConstraint &Other,
                const AtomicSubsumptionEvaluator &E) const;

  static bool AreCompatibleForSubsumption(const FoldExpandedConstraint &A,
                                          const FoldExpandedConstraint &B);
};

const NormalizedConstraint *getNormalizedAssociatedConstraints(
    Sema &S, NamedDecl *ConstrainedDecl,
    ArrayRef<const Expr *> AssociatedConstraints);

template <typename AtomicSubsumptionEvaluator>
bool subsumes(const NormalForm &PDNF, const NormalForm &QCNF,
              const AtomicSubsumptionEvaluator &E) {
  // C++ [temp.constr.order] p2
  //   Then, P subsumes Q if and only if, for every disjunctive clause Pi in the
  //   disjunctive normal form of P, Pi subsumes every conjunctive clause Qj in
  //   the conjuctive normal form of Q, where [...]
  for (const auto &Pi : PDNF) {
    for (const auto &Qj : QCNF) {
      // C++ [temp.constr.order] p2
      //   - [...] a disjunctive clause Pi subsumes a conjunctive clause Qj if
      //     and only if there exists an atomic constraint Pia in Pi for which
      //     there exists an atomic constraint, Qjb, in Qj such that Pia
      //     subsumes Qjb.
      bool Found = false;
      for (NormalFormConstraint Pia : Pi) {
        for (NormalFormConstraint Qjb : Qj) {
          if (Pia.is<FoldExpandedConstraint *>() &&
              Qjb.is<FoldExpandedConstraint *>()) {
            if (Pia.get<FoldExpandedConstraint *>()->subsumes(
                    *Qjb.get<FoldExpandedConstraint *>(), E)) {
              Found = true;
              break;
            }
          } else if (Pia.is<AtomicConstraint *>() &&
                     Qjb.is<AtomicConstraint *>()) {
            if (E(*Pia.get<AtomicConstraint *>(),
                  *Qjb.get<AtomicConstraint *>())) {
              Found = true;
              break;
            }
          }
        }
        if (Found)
          break;
      }
      if (!Found)
        return false;
    }
  }
  return true;
}

template <typename AtomicSubsumptionEvaluator>
bool subsumes(Sema &S, NamedDecl *DP, ArrayRef<const Expr *> P, NamedDecl *DQ,
              ArrayRef<const Expr *> Q, bool &Subsumes,
              const AtomicSubsumptionEvaluator &E) {
  // C++ [temp.constr.order] p2
  //   In order to determine if a constraint P subsumes a constraint Q, P is
  //   transformed into disjunctive normal form, and Q is transformed into
  //   conjunctive normal form. [...]
  const NormalizedConstraint *PNormalized =
      getNormalizedAssociatedConstraints(S, DP, P);
  if (!PNormalized)
    return true;
  NormalForm PDNF = makeDNF(*PNormalized);

  const NormalizedConstraint *QNormalized =
      getNormalizedAssociatedConstraints(S, DQ, Q);
  if (!QNormalized)
    return true;
  NormalForm QCNF = makeCNF(*QNormalized);

  Subsumes = subsumes(PDNF, QCNF, E);
  return false;
}

template <typename AtomicSubsumptionEvaluator>
bool FoldExpandedConstraint::subsumes(
    const FoldExpandedConstraint &Other,
    const AtomicSubsumptionEvaluator &E) const {

  // [C++26] [temp.constr.order]
  // a fold expanded constraint A subsumes another fold expanded constraint B if
  // they are compatible for subsumption, have the same fold-operator, and the
  // constraint of A subsumes that of B

  if (Kind != Other.Kind || !AreCompatibleForSubsumption(*this, Other))
    return false;

  NormalForm PDNF = makeDNF(this->Constraint);
  NormalForm QCNF = makeCNF(Other.Constraint);
  return clang::subsumes(PDNF, QCNF, E);
}

} // clang

#endif // LLVM_CLANG_SEMA_SEMACONCEPT_H
