//===- ASTStructuralEquivalence.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implement StructuralEquivalenceContext class and helper functions
//  for layout matching.
//
// The structural equivalence check could have been implemented as a parallel
// BFS on a pair of graphs.  That must have been the original approach at the
// beginning.
// Let's consider this simple BFS algorithm from the `s` source:
// ```
// void bfs(Graph G, int s)
// {
//   Queue<Integer> queue = new Queue<Integer>();
//   marked[s] = true; // Mark the source
//   queue.enqueue(s); // and put it on the queue.
//   while (!q.isEmpty()) {
//     int v = queue.dequeue(); // Remove next vertex from the queue.
//     for (int w : G.adj(v))
//       if (!marked[w]) // For every unmarked adjacent vertex,
//       {
//         marked[w] = true;
//         queue.enqueue(w);
//       }
//   }
// }
// ```
// Indeed, it has it's queue, which holds pairs of nodes, one from each graph,
// this is the `DeclsToCheck` member. `VisitedDecls` plays the role of the
// marking (`marked`) functionality above, we use it to check whether we've
// already seen a pair of nodes.
//
// We put in the elements into the queue only in the toplevel decl check
// function:
// ```
// static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
//                                      Decl *D1, Decl *D2);
// ```
// The `while` loop where we iterate over the children is implemented in
// `Finish()`.  And `Finish` is called only from the two **member** functions
// which check the equivalency of two Decls or two Types. ASTImporter (and
// other clients) call only these functions.
//
// The `static` implementation functions are called from `Finish`, these push
// the children nodes to the queue via `static bool
// IsStructurallyEquivalent(StructuralEquivalenceContext &Context, Decl *D1,
// Decl *D2)`.  So far so good, this is almost like the BFS.  However, if we
// let a static implementation function to call `Finish` via another **member**
// function that means we end up with two nested while loops each of them
// working on the same queue. This is wrong and nobody can reason about it's
// doing. Thus, static implementation functions must not call the **member**
// functions.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <optional>
#include <utility>

using namespace clang;

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     QualType T1, QualType T2);
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     Decl *D1, Decl *D2);
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const Stmt *S1, const Stmt *S2);
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateArgument &Arg1,
                                     const TemplateArgument &Arg2);
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateArgumentLoc &Arg1,
                                     const TemplateArgumentLoc &Arg2);
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     NestedNameSpecifier *NNS1,
                                     NestedNameSpecifier *NNS2);
static bool IsStructurallyEquivalent(const IdentifierInfo *Name1,
                                     const IdentifierInfo *Name2);

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const DeclarationName Name1,
                                     const DeclarationName Name2) {
  if (Name1.getNameKind() != Name2.getNameKind())
    return false;

  switch (Name1.getNameKind()) {

  case DeclarationName::Identifier:
    return IsStructurallyEquivalent(Name1.getAsIdentifierInfo(),
                                    Name2.getAsIdentifierInfo());

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    return IsStructurallyEquivalent(Context, Name1.getCXXNameType(),
                                    Name2.getCXXNameType());

  case DeclarationName::CXXDeductionGuideName: {
    if (!IsStructurallyEquivalent(
            Context, Name1.getCXXDeductionGuideTemplate()->getDeclName(),
            Name2.getCXXDeductionGuideTemplate()->getDeclName()))
      return false;
    return IsStructurallyEquivalent(Context,
                                    Name1.getCXXDeductionGuideTemplate(),
                                    Name2.getCXXDeductionGuideTemplate());
  }

  case DeclarationName::CXXOperatorName:
    return Name1.getCXXOverloadedOperator() == Name2.getCXXOverloadedOperator();

  case DeclarationName::CXXLiteralOperatorName:
    return IsStructurallyEquivalent(Name1.getCXXLiteralIdentifier(),
                                    Name2.getCXXLiteralIdentifier());

  case DeclarationName::CXXUsingDirective:
    return true; // FIXME When do we consider two using directives equal?

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    return true; // FIXME
  }

  llvm_unreachable("Unhandled kind of DeclarationName");
  return true;
}

namespace {
/// Encapsulates Stmt comparison logic.
class StmtComparer {
  StructuralEquivalenceContext &Context;

  // IsStmtEquivalent overloads. Each overload compares a specific statement
  // and only has to compare the data that is specific to the specific statement
  // class. Should only be called from TraverseStmt.

  bool IsStmtEquivalent(const AddrLabelExpr *E1, const AddrLabelExpr *E2) {
    return IsStructurallyEquivalent(Context, E1->getLabel(), E2->getLabel());
  }

  bool IsStmtEquivalent(const AtomicExpr *E1, const AtomicExpr *E2) {
    return E1->getOp() == E2->getOp();
  }

  bool IsStmtEquivalent(const BinaryOperator *E1, const BinaryOperator *E2) {
    return E1->getOpcode() == E2->getOpcode();
  }

  bool IsStmtEquivalent(const CallExpr *E1, const CallExpr *E2) {
    // FIXME: IsStructurallyEquivalent requires non-const Decls.
    Decl *Callee1 = const_cast<Decl *>(E1->getCalleeDecl());
    Decl *Callee2 = const_cast<Decl *>(E2->getCalleeDecl());

    // Compare whether both calls know their callee.
    if (static_cast<bool>(Callee1) != static_cast<bool>(Callee2))
      return false;

    // Both calls have no callee, so nothing to do.
    if (!static_cast<bool>(Callee1))
      return true;

    assert(Callee2);
    return IsStructurallyEquivalent(Context, Callee1, Callee2);
  }

  bool IsStmtEquivalent(const CharacterLiteral *E1,
                        const CharacterLiteral *E2) {
    return E1->getValue() == E2->getValue() && E1->getKind() == E2->getKind();
  }

  bool IsStmtEquivalent(const ChooseExpr *E1, const ChooseExpr *E2) {
    return true; // Semantics only depend on children.
  }

  bool IsStmtEquivalent(const CompoundStmt *E1, const CompoundStmt *E2) {
    // Number of children is actually checked by the generic children comparison
    // code, but a CompoundStmt is one of the few statements where the number of
    // children frequently differs and the number of statements is also always
    // precomputed. Directly comparing the number of children here is thus
    // just an optimization.
    return E1->size() == E2->size();
  }

  bool IsStmtEquivalent(const DeclRefExpr *DRE1, const DeclRefExpr *DRE2) {
    const ValueDecl *Decl1 = DRE1->getDecl();
    const ValueDecl *Decl2 = DRE2->getDecl();
    if (!Decl1 || !Decl2)
      return false;
    return IsStructurallyEquivalent(Context, const_cast<ValueDecl *>(Decl1),
                                    const_cast<ValueDecl *>(Decl2));
  }

  bool IsStmtEquivalent(const DependentScopeDeclRefExpr *DE1,
                        const DependentScopeDeclRefExpr *DE2) {
    if (!IsStructurallyEquivalent(Context, DE1->getDeclName(),
                                  DE2->getDeclName()))
      return false;
    return IsStructurallyEquivalent(Context, DE1->getQualifier(),
                                    DE2->getQualifier());
  }

  bool IsStmtEquivalent(const Expr *E1, const Expr *E2) {
    return IsStructurallyEquivalent(Context, E1->getType(), E2->getType());
  }

  bool IsStmtEquivalent(const ExpressionTraitExpr *E1,
                        const ExpressionTraitExpr *E2) {
    return E1->getTrait() == E2->getTrait() && E1->getValue() == E2->getValue();
  }

  bool IsStmtEquivalent(const FloatingLiteral *E1, const FloatingLiteral *E2) {
    return E1->isExact() == E2->isExact() && E1->getValue() == E2->getValue();
  }

  bool IsStmtEquivalent(const GenericSelectionExpr *E1,
                        const GenericSelectionExpr *E2) {
    for (auto Pair : zip_longest(E1->getAssocTypeSourceInfos(),
                                 E2->getAssocTypeSourceInfos())) {
      std::optional<TypeSourceInfo *> Child1 = std::get<0>(Pair);
      std::optional<TypeSourceInfo *> Child2 = std::get<1>(Pair);
      // Skip this case if there are a different number of associated types.
      if (!Child1 || !Child2)
        return false;

      if (!IsStructurallyEquivalent(Context, (*Child1)->getType(),
                                    (*Child2)->getType()))
        return false;
    }

    return true;
  }

  bool IsStmtEquivalent(const ImplicitCastExpr *CastE1,
                        const ImplicitCastExpr *CastE2) {
    return IsStructurallyEquivalent(Context, CastE1->getType(),
                                    CastE2->getType());
  }

  bool IsStmtEquivalent(const IntegerLiteral *E1, const IntegerLiteral *E2) {
    return E1->getValue() == E2->getValue();
  }

  bool IsStmtEquivalent(const MemberExpr *E1, const MemberExpr *E2) {
    return IsStructurallyEquivalent(Context, E1->getFoundDecl(),
                                    E2->getFoundDecl());
  }

  bool IsStmtEquivalent(const ObjCStringLiteral *E1,
                        const ObjCStringLiteral *E2) {
    // Just wraps a StringLiteral child.
    return true;
  }

  bool IsStmtEquivalent(const Stmt *S1, const Stmt *S2) { return true; }

  bool IsStmtEquivalent(const GotoStmt *S1, const GotoStmt *S2) {
    LabelDecl *L1 = S1->getLabel();
    LabelDecl *L2 = S2->getLabel();
    if (!L1 || !L2)
      return L1 == L2;

    IdentifierInfo *Name1 = L1->getIdentifier();
    IdentifierInfo *Name2 = L2->getIdentifier();
    return ::IsStructurallyEquivalent(Name1, Name2);
  }

  bool IsStmtEquivalent(const SourceLocExpr *E1, const SourceLocExpr *E2) {
    return E1->getIdentKind() == E2->getIdentKind();
  }

  bool IsStmtEquivalent(const StmtExpr *E1, const StmtExpr *E2) {
    return E1->getTemplateDepth() == E2->getTemplateDepth();
  }

  bool IsStmtEquivalent(const StringLiteral *E1, const StringLiteral *E2) {
    return E1->getBytes() == E2->getBytes();
  }

  bool IsStmtEquivalent(const SubstNonTypeTemplateParmExpr *E1,
                        const SubstNonTypeTemplateParmExpr *E2) {
    if (!IsStructurallyEquivalent(Context, E1->getAssociatedDecl(),
                                  E2->getAssociatedDecl()))
      return false;
    if (E1->getIndex() != E2->getIndex())
      return false;
    if (E1->getPackIndex() != E2->getPackIndex())
      return false;
    return true;
  }

  bool IsStmtEquivalent(const SubstNonTypeTemplateParmPackExpr *E1,
                        const SubstNonTypeTemplateParmPackExpr *E2) {
    return IsStructurallyEquivalent(Context, E1->getArgumentPack(),
                                    E2->getArgumentPack());
  }

  bool IsStmtEquivalent(const TypeTraitExpr *E1, const TypeTraitExpr *E2) {
    if (E1->getTrait() != E2->getTrait())
      return false;

    for (auto Pair : zip_longest(E1->getArgs(), E2->getArgs())) {
      std::optional<TypeSourceInfo *> Child1 = std::get<0>(Pair);
      std::optional<TypeSourceInfo *> Child2 = std::get<1>(Pair);
      // Different number of args.
      if (!Child1 || !Child2)
        return false;

      if (!IsStructurallyEquivalent(Context, (*Child1)->getType(),
                                    (*Child2)->getType()))
        return false;
    }
    return true;
  }

  bool IsStmtEquivalent(const CXXDependentScopeMemberExpr *E1,
                        const CXXDependentScopeMemberExpr *E2) {
    if (!IsStructurallyEquivalent(Context, E1->getMember(), E2->getMember())) {
      return false;
    }
    return IsStructurallyEquivalent(Context, E1->getBaseType(),
                                    E2->getBaseType());
  }

  bool IsStmtEquivalent(const UnaryExprOrTypeTraitExpr *E1,
                        const UnaryExprOrTypeTraitExpr *E2) {
    if (E1->getKind() != E2->getKind())
      return false;
    return IsStructurallyEquivalent(Context, E1->getTypeOfArgument(),
                                    E2->getTypeOfArgument());
  }

  bool IsStmtEquivalent(const UnaryOperator *E1, const UnaryOperator *E2) {
    return E1->getOpcode() == E2->getOpcode();
  }

  bool IsStmtEquivalent(const VAArgExpr *E1, const VAArgExpr *E2) {
    // Semantics only depend on children.
    return true;
  }

  bool IsStmtEquivalent(const OverloadExpr *E1, const OverloadExpr *E2) {
    if (!IsStructurallyEquivalent(Context, E1->getName(), E2->getName()))
      return false;

    if (static_cast<bool>(E1->getQualifier()) !=
        static_cast<bool>(E2->getQualifier()))
      return false;
    if (E1->getQualifier() &&
        !IsStructurallyEquivalent(Context, E1->getQualifier(),
                                  E2->getQualifier()))
      return false;

    if (E1->getNumTemplateArgs() != E2->getNumTemplateArgs())
      return false;
    const TemplateArgumentLoc *Args1 = E1->getTemplateArgs();
    const TemplateArgumentLoc *Args2 = E2->getTemplateArgs();
    for (unsigned int ArgI = 0, ArgN = E1->getNumTemplateArgs(); ArgI < ArgN;
         ++ArgI)
      if (!IsStructurallyEquivalent(Context, Args1[ArgI], Args2[ArgI]))
        return false;

    return true;
  }

  bool IsStmtEquivalent(const CXXBoolLiteralExpr *E1, const CXXBoolLiteralExpr *E2) {
    return E1->getValue() == E2->getValue();
  }

  /// End point of the traversal chain.
  bool TraverseStmt(const Stmt *S1, const Stmt *S2) { return true; }

  // Create traversal methods that traverse the class hierarchy and return
  // the accumulated result of the comparison. Each TraverseStmt overload
  // calls the TraverseStmt overload of the parent class. For example,
  // the TraverseStmt overload for 'BinaryOperator' calls the TraverseStmt
  // overload of 'Expr' which then calls the overload for 'Stmt'.
#define STMT(CLASS, PARENT)                                                    \
  bool TraverseStmt(const CLASS *S1, const CLASS *S2) {                        \
    if (!TraverseStmt(static_cast<const PARENT *>(S1),                         \
                      static_cast<const PARENT *>(S2)))                        \
      return false;                                                            \
    return IsStmtEquivalent(S1, S2);                                           \
  }
#include "clang/AST/StmtNodes.inc"

public:
  StmtComparer(StructuralEquivalenceContext &C) : Context(C) {}

  /// Determine whether two statements are equivalent. The statements have to
  /// be of the same kind. The children of the statements and their properties
  /// are not compared by this function.
  bool IsEquivalent(const Stmt *S1, const Stmt *S2) {
    if (S1->getStmtClass() != S2->getStmtClass())
      return false;

    // Each TraverseStmt walks the class hierarchy from the leaf class to
    // the root class 'Stmt' (e.g. 'BinaryOperator' -> 'Expr' -> 'Stmt'). Cast
    // the Stmt we have here to its specific subclass so that we call the
    // overload that walks the whole class hierarchy from leaf to root (e.g.,
    // cast to 'BinaryOperator' so that 'Expr' and 'Stmt' is traversed).
    switch (S1->getStmtClass()) {
    case Stmt::NoStmtClass:
      llvm_unreachable("Can't traverse NoStmtClass");
#define STMT(CLASS, PARENT)                                                    \
  case Stmt::StmtClass::CLASS##Class:                                          \
    return TraverseStmt(static_cast<const CLASS *>(S1),                        \
                        static_cast<const CLASS *>(S2));
#define ABSTRACT_STMT(S)
#include "clang/AST/StmtNodes.inc"
    }
    llvm_unreachable("Invalid statement kind");
  }
};
} // namespace

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const UnaryOperator *E1,
                                     const CXXOperatorCallExpr *E2) {
  return UnaryOperator::getOverloadedOperator(E1->getOpcode()) ==
             E2->getOperator() &&
         IsStructurallyEquivalent(Context, E1->getSubExpr(), E2->getArg(0));
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const CXXOperatorCallExpr *E1,
                                     const UnaryOperator *E2) {
  return E1->getOperator() ==
             UnaryOperator::getOverloadedOperator(E2->getOpcode()) &&
         IsStructurallyEquivalent(Context, E1->getArg(0), E2->getSubExpr());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const BinaryOperator *E1,
                                     const CXXOperatorCallExpr *E2) {
  return BinaryOperator::getOverloadedOperator(E1->getOpcode()) ==
             E2->getOperator() &&
         IsStructurallyEquivalent(Context, E1->getLHS(), E2->getArg(0)) &&
         IsStructurallyEquivalent(Context, E1->getRHS(), E2->getArg(1));
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const CXXOperatorCallExpr *E1,
                                     const BinaryOperator *E2) {
  return E1->getOperator() ==
             BinaryOperator::getOverloadedOperator(E2->getOpcode()) &&
         IsStructurallyEquivalent(Context, E1->getArg(0), E2->getLHS()) &&
         IsStructurallyEquivalent(Context, E1->getArg(1), E2->getRHS());
}

/// Determine structural equivalence of two statements.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const Stmt *S1, const Stmt *S2) {
  if (!S1 || !S2)
    return S1 == S2;

  // Check for statements with similar syntax but different AST.
  // A UnaryOperator node is more lightweight than a CXXOperatorCallExpr node.
  // The more heavyweight node is only created if the definition-time name
  // lookup had any results. The lookup results are stored CXXOperatorCallExpr
  // only. The lookup results can be different in a "From" and "To" AST even if
  // the compared structure is otherwise equivalent. For this reason we must
  // treat a similar unary/binary operator node and CXXOperatorCall node as
  // equivalent.
  if (const auto *E2CXXOperatorCall = dyn_cast<CXXOperatorCallExpr>(S2)) {
    if (const auto *E1Unary = dyn_cast<UnaryOperator>(S1))
      return IsStructurallyEquivalent(Context, E1Unary, E2CXXOperatorCall);
    if (const auto *E1Binary = dyn_cast<BinaryOperator>(S1))
      return IsStructurallyEquivalent(Context, E1Binary, E2CXXOperatorCall);
  }
  if (const auto *E1CXXOperatorCall = dyn_cast<CXXOperatorCallExpr>(S1)) {
    if (const auto *E2Unary = dyn_cast<UnaryOperator>(S2))
      return IsStructurallyEquivalent(Context, E1CXXOperatorCall, E2Unary);
    if (const auto *E2Binary = dyn_cast<BinaryOperator>(S2))
      return IsStructurallyEquivalent(Context, E1CXXOperatorCall, E2Binary);
  }

  // Compare the statements itself.
  StmtComparer Comparer(Context);
  if (!Comparer.IsEquivalent(S1, S2))
    return false;

  // Iterate over the children of both statements and also compare them.
  for (auto Pair : zip_longest(S1->children(), S2->children())) {
    std::optional<const Stmt *> Child1 = std::get<0>(Pair);
    std::optional<const Stmt *> Child2 = std::get<1>(Pair);
    // One of the statements has a different amount of children than the other,
    // so the statements can't be equivalent.
    if (!Child1 || !Child2)
      return false;
    if (!IsStructurallyEquivalent(Context, *Child1, *Child2))
      return false;
  }
  return true;
}

/// Determine whether two identifiers are equivalent.
static bool IsStructurallyEquivalent(const IdentifierInfo *Name1,
                                     const IdentifierInfo *Name2) {
  if (!Name1 || !Name2)
    return Name1 == Name2;

  return Name1->getName() == Name2->getName();
}

/// Determine whether two nested-name-specifiers are equivalent.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     NestedNameSpecifier *NNS1,
                                     NestedNameSpecifier *NNS2) {
  if (NNS1->getKind() != NNS2->getKind())
    return false;

  NestedNameSpecifier *Prefix1 = NNS1->getPrefix(),
                      *Prefix2 = NNS2->getPrefix();
  if ((bool)Prefix1 != (bool)Prefix2)
    return false;

  if (Prefix1)
    if (!IsStructurallyEquivalent(Context, Prefix1, Prefix2))
      return false;

  switch (NNS1->getKind()) {
  case NestedNameSpecifier::Identifier:
    return IsStructurallyEquivalent(NNS1->getAsIdentifier(),
                                    NNS2->getAsIdentifier());
  case NestedNameSpecifier::Namespace:
    return IsStructurallyEquivalent(Context, NNS1->getAsNamespace(),
                                    NNS2->getAsNamespace());
  case NestedNameSpecifier::NamespaceAlias:
    return IsStructurallyEquivalent(Context, NNS1->getAsNamespaceAlias(),
                                    NNS2->getAsNamespaceAlias());
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    return IsStructurallyEquivalent(Context, QualType(NNS1->getAsType(), 0),
                                    QualType(NNS2->getAsType(), 0));
  case NestedNameSpecifier::Global:
    return true;
  case NestedNameSpecifier::Super:
    return IsStructurallyEquivalent(Context, NNS1->getAsRecordDecl(),
                                    NNS2->getAsRecordDecl());
  }
  return false;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateName &N1,
                                     const TemplateName &N2) {
  TemplateDecl *TemplateDeclN1 = N1.getAsTemplateDecl();
  TemplateDecl *TemplateDeclN2 = N2.getAsTemplateDecl();
  if (TemplateDeclN1 && TemplateDeclN2) {
    if (!IsStructurallyEquivalent(Context, TemplateDeclN1, TemplateDeclN2))
      return false;
    // If the kind is different we compare only the template decl.
    if (N1.getKind() != N2.getKind())
      return true;
  } else if (TemplateDeclN1 || TemplateDeclN2)
    return false;
  else if (N1.getKind() != N2.getKind())
    return false;

  // Check for special case incompatibilities.
  switch (N1.getKind()) {

  case TemplateName::OverloadedTemplate: {
    OverloadedTemplateStorage *OS1 = N1.getAsOverloadedTemplate(),
                              *OS2 = N2.getAsOverloadedTemplate();
    OverloadedTemplateStorage::iterator I1 = OS1->begin(), I2 = OS2->begin(),
                                        E1 = OS1->end(), E2 = OS2->end();
    for (; I1 != E1 && I2 != E2; ++I1, ++I2)
      if (!IsStructurallyEquivalent(Context, *I1, *I2))
        return false;
    return I1 == E1 && I2 == E2;
  }

  case TemplateName::AssumedTemplate: {
    AssumedTemplateStorage *TN1 = N1.getAsAssumedTemplateName(),
                           *TN2 = N1.getAsAssumedTemplateName();
    return TN1->getDeclName() == TN2->getDeclName();
  }

  case TemplateName::DependentTemplate: {
    DependentTemplateName *DN1 = N1.getAsDependentTemplateName(),
                          *DN2 = N2.getAsDependentTemplateName();
    if (!IsStructurallyEquivalent(Context, DN1->getQualifier(),
                                  DN2->getQualifier()))
      return false;
    if (DN1->isIdentifier() && DN2->isIdentifier())
      return IsStructurallyEquivalent(DN1->getIdentifier(),
                                      DN2->getIdentifier());
    else if (DN1->isOverloadedOperator() && DN2->isOverloadedOperator())
      return DN1->getOperator() == DN2->getOperator();
    return false;
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    SubstTemplateTemplateParmPackStorage
        *P1 = N1.getAsSubstTemplateTemplateParmPack(),
        *P2 = N2.getAsSubstTemplateTemplateParmPack();
    return IsStructurallyEquivalent(Context, P1->getArgumentPack(),
                                    P2->getArgumentPack()) &&
           IsStructurallyEquivalent(Context, P1->getAssociatedDecl(),
                                    P2->getAssociatedDecl()) &&
           P1->getIndex() == P2->getIndex();
  }

   case TemplateName::Template:
   case TemplateName::QualifiedTemplate:
   case TemplateName::SubstTemplateTemplateParm:
   case TemplateName::UsingTemplate:
     // It is sufficient to check value of getAsTemplateDecl.
     break;

  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ArrayRef<TemplateArgument> Args1,
                                     ArrayRef<TemplateArgument> Args2);

/// Determine whether two template arguments are equivalent.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateArgument &Arg1,
                                     const TemplateArgument &Arg2) {
  if (Arg1.getKind() != Arg2.getKind())
    return false;

  switch (Arg1.getKind()) {
  case TemplateArgument::Null:
    return true;

  case TemplateArgument::Type:
    return IsStructurallyEquivalent(Context, Arg1.getAsType(), Arg2.getAsType());

  case TemplateArgument::Integral:
    if (!IsStructurallyEquivalent(Context, Arg1.getIntegralType(),
                                          Arg2.getIntegralType()))
      return false;

    return llvm::APSInt::isSameValue(Arg1.getAsIntegral(),
                                     Arg2.getAsIntegral());

  case TemplateArgument::Declaration:
    return IsStructurallyEquivalent(Context, Arg1.getAsDecl(), Arg2.getAsDecl());

  case TemplateArgument::NullPtr:
    return true; // FIXME: Is this correct?

  case TemplateArgument::Template:
    return IsStructurallyEquivalent(Context, Arg1.getAsTemplate(),
                                    Arg2.getAsTemplate());

  case TemplateArgument::TemplateExpansion:
    return IsStructurallyEquivalent(Context,
                                    Arg1.getAsTemplateOrTemplatePattern(),
                                    Arg2.getAsTemplateOrTemplatePattern());

  case TemplateArgument::Expression:
    return IsStructurallyEquivalent(Context, Arg1.getAsExpr(),
                                    Arg2.getAsExpr());

  case TemplateArgument::StructuralValue:
    return Arg1.structurallyEquals(Arg2);

  case TemplateArgument::Pack:
    return IsStructurallyEquivalent(Context, Arg1.pack_elements(),
                                    Arg2.pack_elements());
  }

  llvm_unreachable("Invalid template argument kind");
}

/// Determine structural equivalence of two template argument lists.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ArrayRef<TemplateArgument> Args1,
                                     ArrayRef<TemplateArgument> Args2) {
  if (Args1.size() != Args2.size())
    return false;
  for (unsigned I = 0, N = Args1.size(); I != N; ++I) {
    if (!IsStructurallyEquivalent(Context, Args1[I], Args2[I]))
      return false;
  }
  return true;
}

/// Determine whether two template argument locations are equivalent.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateArgumentLoc &Arg1,
                                     const TemplateArgumentLoc &Arg2) {
  return IsStructurallyEquivalent(Context, Arg1.getArgument(),
                                  Arg2.getArgument());
}

/// Determine structural equivalence for the common part of array
/// types.
static bool IsArrayStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                          const ArrayType *Array1,
                                          const ArrayType *Array2) {
  if (!IsStructurallyEquivalent(Context, Array1->getElementType(),
                                Array2->getElementType()))
    return false;
  if (Array1->getSizeModifier() != Array2->getSizeModifier())
    return false;
  if (Array1->getIndexTypeQualifiers() != Array2->getIndexTypeQualifiers())
    return false;

  return true;
}

/// Determine structural equivalence based on the ExtInfo of functions. This
/// is inspired by ASTContext::mergeFunctionTypes(), we compare calling
/// conventions bits but must not compare some other bits.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FunctionType::ExtInfo EI1,
                                     FunctionType::ExtInfo EI2) {
  // Compatible functions must have compatible calling conventions.
  if (EI1.getCC() != EI2.getCC())
    return false;

  // Regparm is part of the calling convention.
  if (EI1.getHasRegParm() != EI2.getHasRegParm())
    return false;
  if (EI1.getRegParm() != EI2.getRegParm())
    return false;

  if (EI1.getProducesResult() != EI2.getProducesResult())
    return false;
  if (EI1.getNoCallerSavedRegs() != EI2.getNoCallerSavedRegs())
    return false;
  if (EI1.getNoCfCheck() != EI2.getNoCfCheck())
    return false;

  return true;
}

/// Check the equivalence of exception specifications.
static bool IsEquivalentExceptionSpec(StructuralEquivalenceContext &Context,
                                      const FunctionProtoType *Proto1,
                                      const FunctionProtoType *Proto2) {

  auto Spec1 = Proto1->getExceptionSpecType();
  auto Spec2 = Proto2->getExceptionSpecType();

  if (isUnresolvedExceptionSpec(Spec1) || isUnresolvedExceptionSpec(Spec2))
    return true;

  if (Spec1 != Spec2)
    return false;
  if (Spec1 == EST_Dynamic) {
    if (Proto1->getNumExceptions() != Proto2->getNumExceptions())
      return false;
    for (unsigned I = 0, N = Proto1->getNumExceptions(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Proto1->getExceptionType(I),
                                    Proto2->getExceptionType(I)))
        return false;
    }
  } else if (isComputedNoexcept(Spec1)) {
    if (!IsStructurallyEquivalent(Context, Proto1->getNoexceptExpr(),
                                  Proto2->getNoexceptExpr()))
      return false;
  }

  return true;
}

/// Determine structural equivalence of two types.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     QualType T1, QualType T2) {
  if (T1.isNull() || T2.isNull())
    return T1.isNull() && T2.isNull();

  QualType OrigT1 = T1;
  QualType OrigT2 = T2;

  if (!Context.StrictTypeSpelling) {
    // We aren't being strict about token-to-token equivalence of types,
    // so map down to the canonical type.
    T1 = Context.FromCtx.getCanonicalType(T1);
    T2 = Context.ToCtx.getCanonicalType(T2);
  }

  if (T1.getQualifiers() != T2.getQualifiers())
    return false;

  Type::TypeClass TC = T1->getTypeClass();

  if (T1->getTypeClass() != T2->getTypeClass()) {
    // Compare function types with prototypes vs. without prototypes as if
    // both did not have prototypes.
    if (T1->getTypeClass() == Type::FunctionProto &&
        T2->getTypeClass() == Type::FunctionNoProto)
      TC = Type::FunctionNoProto;
    else if (T1->getTypeClass() == Type::FunctionNoProto &&
             T2->getTypeClass() == Type::FunctionProto)
      TC = Type::FunctionNoProto;
    else
      return false;
  }

  switch (TC) {
  case Type::Builtin:
    // FIXME: Deal with Char_S/Char_U.
    if (cast<BuiltinType>(T1)->getKind() != cast<BuiltinType>(T2)->getKind())
      return false;
    break;

  case Type::Complex:
    if (!IsStructurallyEquivalent(Context,
                                  cast<ComplexType>(T1)->getElementType(),
                                  cast<ComplexType>(T2)->getElementType()))
      return false;
    break;

  case Type::Adjusted:
  case Type::Decayed:
  case Type::ArrayParameter:
    if (!IsStructurallyEquivalent(Context,
                                  cast<AdjustedType>(T1)->getOriginalType(),
                                  cast<AdjustedType>(T2)->getOriginalType()))
      return false;
    break;

  case Type::Pointer:
    if (!IsStructurallyEquivalent(Context,
                                  cast<PointerType>(T1)->getPointeeType(),
                                  cast<PointerType>(T2)->getPointeeType()))
      return false;
    break;

  case Type::BlockPointer:
    if (!IsStructurallyEquivalent(Context,
                                  cast<BlockPointerType>(T1)->getPointeeType(),
                                  cast<BlockPointerType>(T2)->getPointeeType()))
      return false;
    break;

  case Type::LValueReference:
  case Type::RValueReference: {
    const auto *Ref1 = cast<ReferenceType>(T1);
    const auto *Ref2 = cast<ReferenceType>(T2);
    if (Ref1->isSpelledAsLValue() != Ref2->isSpelledAsLValue())
      return false;
    if (Ref1->isInnerRef() != Ref2->isInnerRef())
      return false;
    if (!IsStructurallyEquivalent(Context, Ref1->getPointeeTypeAsWritten(),
                                  Ref2->getPointeeTypeAsWritten()))
      return false;
    break;
  }

  case Type::MemberPointer: {
    const auto *MemPtr1 = cast<MemberPointerType>(T1);
    const auto *MemPtr2 = cast<MemberPointerType>(T2);
    if (!IsStructurallyEquivalent(Context, MemPtr1->getPointeeType(),
                                  MemPtr2->getPointeeType()))
      return false;
    if (!IsStructurallyEquivalent(Context, QualType(MemPtr1->getClass(), 0),
                                  QualType(MemPtr2->getClass(), 0)))
      return false;
    break;
  }

  case Type::ConstantArray: {
    const auto *Array1 = cast<ConstantArrayType>(T1);
    const auto *Array2 = cast<ConstantArrayType>(T2);
    if (!llvm::APInt::isSameValue(Array1->getSize(), Array2->getSize()))
      return false;

    if (!IsArrayStructurallyEquivalent(Context, Array1, Array2))
      return false;
    break;
  }

  case Type::IncompleteArray:
    if (!IsArrayStructurallyEquivalent(Context, cast<ArrayType>(T1),
                                       cast<ArrayType>(T2)))
      return false;
    break;

  case Type::VariableArray: {
    const auto *Array1 = cast<VariableArrayType>(T1);
    const auto *Array2 = cast<VariableArrayType>(T2);
    if (!IsStructurallyEquivalent(Context, Array1->getSizeExpr(),
                                  Array2->getSizeExpr()))
      return false;

    if (!IsArrayStructurallyEquivalent(Context, Array1, Array2))
      return false;

    break;
  }

  case Type::DependentSizedArray: {
    const auto *Array1 = cast<DependentSizedArrayType>(T1);
    const auto *Array2 = cast<DependentSizedArrayType>(T2);
    if (!IsStructurallyEquivalent(Context, Array1->getSizeExpr(),
                                  Array2->getSizeExpr()))
      return false;

    if (!IsArrayStructurallyEquivalent(Context, Array1, Array2))
      return false;

    break;
  }

  case Type::DependentAddressSpace: {
    const auto *DepAddressSpace1 = cast<DependentAddressSpaceType>(T1);
    const auto *DepAddressSpace2 = cast<DependentAddressSpaceType>(T2);
    if (!IsStructurallyEquivalent(Context, DepAddressSpace1->getAddrSpaceExpr(),
                                  DepAddressSpace2->getAddrSpaceExpr()))
      return false;
    if (!IsStructurallyEquivalent(Context, DepAddressSpace1->getPointeeType(),
                                  DepAddressSpace2->getPointeeType()))
      return false;

    break;
  }

  case Type::DependentSizedExtVector: {
    const auto *Vec1 = cast<DependentSizedExtVectorType>(T1);
    const auto *Vec2 = cast<DependentSizedExtVectorType>(T2);
    if (!IsStructurallyEquivalent(Context, Vec1->getSizeExpr(),
                                  Vec2->getSizeExpr()))
      return false;
    if (!IsStructurallyEquivalent(Context, Vec1->getElementType(),
                                  Vec2->getElementType()))
      return false;
    break;
  }

  case Type::DependentVector: {
    const auto *Vec1 = cast<DependentVectorType>(T1);
    const auto *Vec2 = cast<DependentVectorType>(T2);
    if (Vec1->getVectorKind() != Vec2->getVectorKind())
      return false;
    if (!IsStructurallyEquivalent(Context, Vec1->getSizeExpr(),
                                  Vec2->getSizeExpr()))
      return false;
    if (!IsStructurallyEquivalent(Context, Vec1->getElementType(),
                                  Vec2->getElementType()))
      return false;
    break;
  }

  case Type::Vector:
  case Type::ExtVector: {
    const auto *Vec1 = cast<VectorType>(T1);
    const auto *Vec2 = cast<VectorType>(T2);
    if (!IsStructurallyEquivalent(Context, Vec1->getElementType(),
                                  Vec2->getElementType()))
      return false;
    if (Vec1->getNumElements() != Vec2->getNumElements())
      return false;
    if (Vec1->getVectorKind() != Vec2->getVectorKind())
      return false;
    break;
  }

  case Type::DependentSizedMatrix: {
    const DependentSizedMatrixType *Mat1 = cast<DependentSizedMatrixType>(T1);
    const DependentSizedMatrixType *Mat2 = cast<DependentSizedMatrixType>(T2);
    // The element types, row and column expressions must be structurally
    // equivalent.
    if (!IsStructurallyEquivalent(Context, Mat1->getRowExpr(),
                                  Mat2->getRowExpr()) ||
        !IsStructurallyEquivalent(Context, Mat1->getColumnExpr(),
                                  Mat2->getColumnExpr()) ||
        !IsStructurallyEquivalent(Context, Mat1->getElementType(),
                                  Mat2->getElementType()))
      return false;
    break;
  }

  case Type::ConstantMatrix: {
    const ConstantMatrixType *Mat1 = cast<ConstantMatrixType>(T1);
    const ConstantMatrixType *Mat2 = cast<ConstantMatrixType>(T2);
    // The element types must be structurally equivalent and the number of rows
    // and columns must match.
    if (!IsStructurallyEquivalent(Context, Mat1->getElementType(),
                                  Mat2->getElementType()) ||
        Mat1->getNumRows() != Mat2->getNumRows() ||
        Mat1->getNumColumns() != Mat2->getNumColumns())
      return false;
    break;
  }

  case Type::FunctionProto: {
    const auto *Proto1 = cast<FunctionProtoType>(T1);
    const auto *Proto2 = cast<FunctionProtoType>(T2);

    if (Proto1->getNumParams() != Proto2->getNumParams())
      return false;
    for (unsigned I = 0, N = Proto1->getNumParams(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Proto1->getParamType(I),
                                    Proto2->getParamType(I)))
        return false;
    }
    if (Proto1->isVariadic() != Proto2->isVariadic())
      return false;

    if (Proto1->getMethodQuals() != Proto2->getMethodQuals())
      return false;

    // Check exceptions, this information is lost in canonical type.
    const auto *OrigProto1 =
        cast<FunctionProtoType>(OrigT1.getDesugaredType(Context.FromCtx));
    const auto *OrigProto2 =
        cast<FunctionProtoType>(OrigT2.getDesugaredType(Context.ToCtx));
    if (!IsEquivalentExceptionSpec(Context, OrigProto1, OrigProto2))
      return false;

    // Fall through to check the bits common with FunctionNoProtoType.
    [[fallthrough]];
  }

  case Type::FunctionNoProto: {
    const auto *Function1 = cast<FunctionType>(T1);
    const auto *Function2 = cast<FunctionType>(T2);
    if (!IsStructurallyEquivalent(Context, Function1->getReturnType(),
                                  Function2->getReturnType()))
      return false;
    if (!IsStructurallyEquivalent(Context, Function1->getExtInfo(),
                                  Function2->getExtInfo()))
      return false;
    break;
  }

  case Type::UnresolvedUsing:
    if (!IsStructurallyEquivalent(Context,
                                  cast<UnresolvedUsingType>(T1)->getDecl(),
                                  cast<UnresolvedUsingType>(T2)->getDecl()))
      return false;
    break;

  case Type::Attributed:
    if (!IsStructurallyEquivalent(Context,
                                  cast<AttributedType>(T1)->getModifiedType(),
                                  cast<AttributedType>(T2)->getModifiedType()))
      return false;
    if (!IsStructurallyEquivalent(
            Context, cast<AttributedType>(T1)->getEquivalentType(),
            cast<AttributedType>(T2)->getEquivalentType()))
      return false;
    break;

  case Type::CountAttributed:
    if (!IsStructurallyEquivalent(Context,
                                  cast<CountAttributedType>(T1)->desugar(),
                                  cast<CountAttributedType>(T2)->desugar()))
      return false;
    break;

  case Type::BTFTagAttributed:
    if (!IsStructurallyEquivalent(
            Context, cast<BTFTagAttributedType>(T1)->getWrappedType(),
            cast<BTFTagAttributedType>(T2)->getWrappedType()))
      return false;
    break;

  case Type::Paren:
    if (!IsStructurallyEquivalent(Context, cast<ParenType>(T1)->getInnerType(),
                                  cast<ParenType>(T2)->getInnerType()))
      return false;
    break;

  case Type::MacroQualified:
    if (!IsStructurallyEquivalent(
            Context, cast<MacroQualifiedType>(T1)->getUnderlyingType(),
            cast<MacroQualifiedType>(T2)->getUnderlyingType()))
      return false;
    break;

  case Type::Using:
    if (!IsStructurallyEquivalent(Context, cast<UsingType>(T1)->getFoundDecl(),
                                  cast<UsingType>(T2)->getFoundDecl()))
      return false;
    if (!IsStructurallyEquivalent(Context,
                                  cast<UsingType>(T1)->getUnderlyingType(),
                                  cast<UsingType>(T2)->getUnderlyingType()))
      return false;
    break;

  case Type::Typedef:
    if (!IsStructurallyEquivalent(Context, cast<TypedefType>(T1)->getDecl(),
                                  cast<TypedefType>(T2)->getDecl()) ||
        !IsStructurallyEquivalent(Context, cast<TypedefType>(T1)->desugar(),
                                  cast<TypedefType>(T2)->desugar()))
      return false;
    break;

  case Type::TypeOfExpr:
    if (!IsStructurallyEquivalent(
            Context, cast<TypeOfExprType>(T1)->getUnderlyingExpr(),
            cast<TypeOfExprType>(T2)->getUnderlyingExpr()))
      return false;
    break;

  case Type::TypeOf:
    if (!IsStructurallyEquivalent(Context,
                                  cast<TypeOfType>(T1)->getUnmodifiedType(),
                                  cast<TypeOfType>(T2)->getUnmodifiedType()))
      return false;
    break;

  case Type::UnaryTransform:
    if (!IsStructurallyEquivalent(
            Context, cast<UnaryTransformType>(T1)->getUnderlyingType(),
            cast<UnaryTransformType>(T2)->getUnderlyingType()))
      return false;
    break;

  case Type::Decltype:
    if (!IsStructurallyEquivalent(Context,
                                  cast<DecltypeType>(T1)->getUnderlyingExpr(),
                                  cast<DecltypeType>(T2)->getUnderlyingExpr()))
      return false;
    break;

  case Type::Auto: {
    auto *Auto1 = cast<AutoType>(T1);
    auto *Auto2 = cast<AutoType>(T2);
    if (!IsStructurallyEquivalent(Context, Auto1->getDeducedType(),
                                  Auto2->getDeducedType()))
      return false;
    if (Auto1->isConstrained() != Auto2->isConstrained())
      return false;
    if (Auto1->isConstrained()) {
      if (Auto1->getTypeConstraintConcept() !=
          Auto2->getTypeConstraintConcept())
        return false;
      if (!IsStructurallyEquivalent(Context,
                                    Auto1->getTypeConstraintArguments(),
                                    Auto2->getTypeConstraintArguments()))
        return false;
    }
    break;
  }

  case Type::DeducedTemplateSpecialization: {
    const auto *DT1 = cast<DeducedTemplateSpecializationType>(T1);
    const auto *DT2 = cast<DeducedTemplateSpecializationType>(T2);
    if (!IsStructurallyEquivalent(Context, DT1->getTemplateName(),
                                  DT2->getTemplateName()))
      return false;
    if (!IsStructurallyEquivalent(Context, DT1->getDeducedType(),
                                  DT2->getDeducedType()))
      return false;
    break;
  }

  case Type::Record:
  case Type::Enum:
    if (!IsStructurallyEquivalent(Context, cast<TagType>(T1)->getDecl(),
                                  cast<TagType>(T2)->getDecl()))
      return false;
    break;

  case Type::TemplateTypeParm: {
    const auto *Parm1 = cast<TemplateTypeParmType>(T1);
    const auto *Parm2 = cast<TemplateTypeParmType>(T2);
    if (!Context.IgnoreTemplateParmDepth &&
        Parm1->getDepth() != Parm2->getDepth())
      return false;
    if (Parm1->getIndex() != Parm2->getIndex())
      return false;
    if (Parm1->isParameterPack() != Parm2->isParameterPack())
      return false;

    // Names of template type parameters are never significant.
    break;
  }

  case Type::SubstTemplateTypeParm: {
    const auto *Subst1 = cast<SubstTemplateTypeParmType>(T1);
    const auto *Subst2 = cast<SubstTemplateTypeParmType>(T2);
    if (!IsStructurallyEquivalent(Context, Subst1->getReplacementType(),
                                  Subst2->getReplacementType()))
      return false;
    if (!IsStructurallyEquivalent(Context, Subst1->getAssociatedDecl(),
                                  Subst2->getAssociatedDecl()))
      return false;
    if (Subst1->getIndex() != Subst2->getIndex())
      return false;
    if (Subst1->getPackIndex() != Subst2->getPackIndex())
      return false;
    break;
  }

  case Type::SubstTemplateTypeParmPack: {
    const auto *Subst1 = cast<SubstTemplateTypeParmPackType>(T1);
    const auto *Subst2 = cast<SubstTemplateTypeParmPackType>(T2);
    if (!IsStructurallyEquivalent(Context, Subst1->getAssociatedDecl(),
                                  Subst2->getAssociatedDecl()))
      return false;
    if (Subst1->getIndex() != Subst2->getIndex())
      return false;
    if (!IsStructurallyEquivalent(Context, Subst1->getArgumentPack(),
                                  Subst2->getArgumentPack()))
      return false;
    break;
  }

  case Type::TemplateSpecialization: {
    const auto *Spec1 = cast<TemplateSpecializationType>(T1);
    const auto *Spec2 = cast<TemplateSpecializationType>(T2);
    if (!IsStructurallyEquivalent(Context, Spec1->getTemplateName(),
                                  Spec2->getTemplateName()))
      return false;
    if (!IsStructurallyEquivalent(Context, Spec1->template_arguments(),
                                  Spec2->template_arguments()))
      return false;
    break;
  }

  case Type::Elaborated: {
    const auto *Elab1 = cast<ElaboratedType>(T1);
    const auto *Elab2 = cast<ElaboratedType>(T2);
    // CHECKME: what if a keyword is ElaboratedTypeKeyword::None or
    // ElaboratedTypeKeyword::Typename
    // ?
    if (Elab1->getKeyword() != Elab2->getKeyword())
      return false;
    if (!IsStructurallyEquivalent(Context, Elab1->getQualifier(),
                                  Elab2->getQualifier()))
      return false;
    if (!IsStructurallyEquivalent(Context, Elab1->getNamedType(),
                                  Elab2->getNamedType()))
      return false;
    break;
  }

  case Type::InjectedClassName: {
    const auto *Inj1 = cast<InjectedClassNameType>(T1);
    const auto *Inj2 = cast<InjectedClassNameType>(T2);
    if (!IsStructurallyEquivalent(Context,
                                  Inj1->getInjectedSpecializationType(),
                                  Inj2->getInjectedSpecializationType()))
      return false;
    break;
  }

  case Type::DependentName: {
    const auto *Typename1 = cast<DependentNameType>(T1);
    const auto *Typename2 = cast<DependentNameType>(T2);
    if (!IsStructurallyEquivalent(Context, Typename1->getQualifier(),
                                  Typename2->getQualifier()))
      return false;
    if (!IsStructurallyEquivalent(Typename1->getIdentifier(),
                                  Typename2->getIdentifier()))
      return false;

    break;
  }

  case Type::DependentTemplateSpecialization: {
    const auto *Spec1 = cast<DependentTemplateSpecializationType>(T1);
    const auto *Spec2 = cast<DependentTemplateSpecializationType>(T2);
    if (!IsStructurallyEquivalent(Context, Spec1->getQualifier(),
                                  Spec2->getQualifier()))
      return false;
    if (!IsStructurallyEquivalent(Spec1->getIdentifier(),
                                  Spec2->getIdentifier()))
      return false;
    if (!IsStructurallyEquivalent(Context, Spec1->template_arguments(),
                                  Spec2->template_arguments()))
      return false;
    break;
  }

  case Type::PackExpansion:
    if (!IsStructurallyEquivalent(Context,
                                  cast<PackExpansionType>(T1)->getPattern(),
                                  cast<PackExpansionType>(T2)->getPattern()))
      return false;
    break;

  case Type::PackIndexing:
    if (!IsStructurallyEquivalent(Context,
                                  cast<PackIndexingType>(T1)->getPattern(),
                                  cast<PackIndexingType>(T2)->getPattern()))
      if (!IsStructurallyEquivalent(Context,
                                    cast<PackIndexingType>(T1)->getIndexExpr(),
                                    cast<PackIndexingType>(T2)->getIndexExpr()))
        return false;
    break;

  case Type::ObjCInterface: {
    const auto *Iface1 = cast<ObjCInterfaceType>(T1);
    const auto *Iface2 = cast<ObjCInterfaceType>(T2);
    if (!IsStructurallyEquivalent(Context, Iface1->getDecl(),
                                  Iface2->getDecl()))
      return false;
    break;
  }

  case Type::ObjCTypeParam: {
    const auto *Obj1 = cast<ObjCTypeParamType>(T1);
    const auto *Obj2 = cast<ObjCTypeParamType>(T2);
    if (!IsStructurallyEquivalent(Context, Obj1->getDecl(), Obj2->getDecl()))
      return false;

    if (Obj1->getNumProtocols() != Obj2->getNumProtocols())
      return false;
    for (unsigned I = 0, N = Obj1->getNumProtocols(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Obj1->getProtocol(I),
                                    Obj2->getProtocol(I)))
        return false;
    }
    break;
  }

  case Type::ObjCObject: {
    const auto *Obj1 = cast<ObjCObjectType>(T1);
    const auto *Obj2 = cast<ObjCObjectType>(T2);
    if (!IsStructurallyEquivalent(Context, Obj1->getBaseType(),
                                  Obj2->getBaseType()))
      return false;
    if (Obj1->getNumProtocols() != Obj2->getNumProtocols())
      return false;
    for (unsigned I = 0, N = Obj1->getNumProtocols(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Obj1->getProtocol(I),
                                    Obj2->getProtocol(I)))
        return false;
    }
    break;
  }

  case Type::ObjCObjectPointer: {
    const auto *Ptr1 = cast<ObjCObjectPointerType>(T1);
    const auto *Ptr2 = cast<ObjCObjectPointerType>(T2);
    if (!IsStructurallyEquivalent(Context, Ptr1->getPointeeType(),
                                  Ptr2->getPointeeType()))
      return false;
    break;
  }

  case Type::Atomic:
    if (!IsStructurallyEquivalent(Context, cast<AtomicType>(T1)->getValueType(),
                                  cast<AtomicType>(T2)->getValueType()))
      return false;
    break;

  case Type::Pipe:
    if (!IsStructurallyEquivalent(Context, cast<PipeType>(T1)->getElementType(),
                                  cast<PipeType>(T2)->getElementType()))
      return false;
    break;
  case Type::BitInt: {
    const auto *Int1 = cast<BitIntType>(T1);
    const auto *Int2 = cast<BitIntType>(T2);

    if (Int1->isUnsigned() != Int2->isUnsigned() ||
        Int1->getNumBits() != Int2->getNumBits())
      return false;
    break;
  }
  case Type::DependentBitInt: {
    const auto *Int1 = cast<DependentBitIntType>(T1);
    const auto *Int2 = cast<DependentBitIntType>(T2);

    if (Int1->isUnsigned() != Int2->isUnsigned() ||
        !IsStructurallyEquivalent(Context, Int1->getNumBitsExpr(),
                                  Int2->getNumBitsExpr()))
      return false;
    break;
  }
  } // end switch

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     VarDecl *D1, VarDecl *D2) {
  IdentifierInfo *Name1 = D1->getIdentifier();
  IdentifierInfo *Name2 = D2->getIdentifier();
  if (!::IsStructurallyEquivalent(Name1, Name2))
    return false;

  if (!IsStructurallyEquivalent(Context, D1->getType(), D2->getType()))
    return false;

  // Compare storage class and initializer only if none or both are a
  // definition. Like a forward-declaration matches a class definition, variable
  // declarations that are not definitions should match with the definitions.
  if (D1->isThisDeclarationADefinition() != D2->isThisDeclarationADefinition())
    return true;

  if (D1->getStorageClass() != D2->getStorageClass())
    return false;

  return IsStructurallyEquivalent(Context, D1->getInit(), D2->getInit());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FieldDecl *Field1, FieldDecl *Field2,
                                     QualType Owner2Type) {
  const auto *Owner2 = cast<Decl>(Field2->getDeclContext());

  // For anonymous structs/unions, match up the anonymous struct/union type
  // declarations directly, so that we don't go off searching for anonymous
  // types
  if (Field1->isAnonymousStructOrUnion() &&
      Field2->isAnonymousStructOrUnion()) {
    RecordDecl *D1 = Field1->getType()->castAs<RecordType>()->getDecl();
    RecordDecl *D2 = Field2->getType()->castAs<RecordType>()->getDecl();
    return IsStructurallyEquivalent(Context, D1, D2);
  }

  // Check for equivalent field names.
  IdentifierInfo *Name1 = Field1->getIdentifier();
  IdentifierInfo *Name2 = Field2->getIdentifier();
  if (!::IsStructurallyEquivalent(Name1, Name2)) {
    if (Context.Complain) {
      Context.Diag2(
          Owner2->getLocation(),
          Context.getApplicableDiagnostic(diag::err_odr_tag_type_inconsistent))
          << Owner2Type;
      Context.Diag2(Field2->getLocation(), diag::note_odr_field_name)
          << Field2->getDeclName();
      Context.Diag1(Field1->getLocation(), diag::note_odr_field_name)
          << Field1->getDeclName();
    }
    return false;
  }

  if (!IsStructurallyEquivalent(Context, Field1->getType(),
                                Field2->getType())) {
    if (Context.Complain) {
      Context.Diag2(
          Owner2->getLocation(),
          Context.getApplicableDiagnostic(diag::err_odr_tag_type_inconsistent))
          << Owner2Type;
      Context.Diag2(Field2->getLocation(), diag::note_odr_field)
          << Field2->getDeclName() << Field2->getType();
      Context.Diag1(Field1->getLocation(), diag::note_odr_field)
          << Field1->getDeclName() << Field1->getType();
    }
    return false;
  }

  if (Field1->isBitField())
    return IsStructurallyEquivalent(Context, Field1->getBitWidth(),
                                    Field2->getBitWidth());

  return true;
}

/// Determine structural equivalence of two fields.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FieldDecl *Field1, FieldDecl *Field2) {
  const auto *Owner2 = cast<RecordDecl>(Field2->getDeclContext());
  return IsStructurallyEquivalent(Context, Field1, Field2,
                                  Context.ToCtx.getTypeDeclType(Owner2));
}

/// Determine structural equivalence of two methods.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     CXXMethodDecl *Method1,
                                     CXXMethodDecl *Method2) {
  bool PropertiesEqual =
      Method1->getDeclKind() == Method2->getDeclKind() &&
      Method1->getRefQualifier() == Method2->getRefQualifier() &&
      Method1->getAccess() == Method2->getAccess() &&
      Method1->getOverloadedOperator() == Method2->getOverloadedOperator() &&
      Method1->isStatic() == Method2->isStatic() &&
      Method1->isImplicitObjectMemberFunction() ==
          Method2->isImplicitObjectMemberFunction() &&
      Method1->isConst() == Method2->isConst() &&
      Method1->isVolatile() == Method2->isVolatile() &&
      Method1->isVirtual() == Method2->isVirtual() &&
      Method1->isPureVirtual() == Method2->isPureVirtual() &&
      Method1->isDefaulted() == Method2->isDefaulted() &&
      Method1->isDeleted() == Method2->isDeleted();
  if (!PropertiesEqual)
    return false;
  // FIXME: Check for 'final'.

  if (auto *Constructor1 = dyn_cast<CXXConstructorDecl>(Method1)) {
    auto *Constructor2 = cast<CXXConstructorDecl>(Method2);
    if (!Constructor1->getExplicitSpecifier().isEquivalent(
            Constructor2->getExplicitSpecifier()))
      return false;
  }

  if (auto *Conversion1 = dyn_cast<CXXConversionDecl>(Method1)) {
    auto *Conversion2 = cast<CXXConversionDecl>(Method2);
    if (!Conversion1->getExplicitSpecifier().isEquivalent(
            Conversion2->getExplicitSpecifier()))
      return false;
    if (!IsStructurallyEquivalent(Context, Conversion1->getConversionType(),
                                  Conversion2->getConversionType()))
      return false;
  }

  const IdentifierInfo *Name1 = Method1->getIdentifier();
  const IdentifierInfo *Name2 = Method2->getIdentifier();
  if (!::IsStructurallyEquivalent(Name1, Name2)) {
    return false;
    // TODO: Names do not match, add warning like at check for FieldDecl.
  }

  // Check the prototypes.
  if (!::IsStructurallyEquivalent(Context,
                                  Method1->getType(), Method2->getType()))
    return false;

  return true;
}

/// Determine structural equivalence of two lambda classes.
static bool
IsStructurallyEquivalentLambdas(StructuralEquivalenceContext &Context,
                                CXXRecordDecl *D1, CXXRecordDecl *D2) {
  assert(D1->isLambda() && D2->isLambda() &&
         "Must be called on lambda classes");
  if (!IsStructurallyEquivalent(Context, D1->getLambdaCallOperator(),
                                D2->getLambdaCallOperator()))
    return false;

  return true;
}

/// Determine if context of a class is equivalent.
static bool
IsRecordContextStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                      RecordDecl *D1, RecordDecl *D2) {
  // The context should be completely equal, including anonymous and inline
  // namespaces.
  // We compare objects as part of full translation units, not subtrees of
  // translation units.
  DeclContext *DC1 = D1->getDeclContext()->getNonTransparentContext();
  DeclContext *DC2 = D2->getDeclContext()->getNonTransparentContext();
  while (true) {
    // Special case: We allow a struct defined in a function to be equivalent
    // with a similar struct defined outside of a function.
    if ((DC1->isFunctionOrMethod() && DC2->isTranslationUnit()) ||
        (DC2->isFunctionOrMethod() && DC1->isTranslationUnit()))
      return true;

    if (DC1->getDeclKind() != DC2->getDeclKind())
      return false;
    if (DC1->isTranslationUnit())
      break;
    if (DC1->isInlineNamespace() != DC2->isInlineNamespace())
      return false;
    if (const auto *ND1 = dyn_cast<NamedDecl>(DC1)) {
      const auto *ND2 = cast<NamedDecl>(DC2);
      if (!DC1->isInlineNamespace() &&
          !IsStructurallyEquivalent(ND1->getIdentifier(), ND2->getIdentifier()))
        return false;
    }

    if (auto *D1Spec = dyn_cast<ClassTemplateSpecializationDecl>(DC1)) {
      auto *D2Spec = dyn_cast<ClassTemplateSpecializationDecl>(DC2);
      if (!IsStructurallyEquivalent(Context, D1Spec, D2Spec))
        return false;
    }

    DC1 = DC1->getParent()->getNonTransparentContext();
    DC2 = DC2->getParent()->getNonTransparentContext();
  }

  return true;
}

static bool NameIsStructurallyEquivalent(const TagDecl &D1, const TagDecl &D2) {
  auto GetName = [](const TagDecl &D) -> const IdentifierInfo * {
    if (const IdentifierInfo *Name = D.getIdentifier())
      return Name;
    if (const TypedefNameDecl *TypedefName = D.getTypedefNameForAnonDecl())
      return TypedefName->getIdentifier();
    return nullptr;
  };
  return IsStructurallyEquivalent(GetName(D1), GetName(D2));
}

/// Determine structural equivalence of two records.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     RecordDecl *D1, RecordDecl *D2) {
  if (!NameIsStructurallyEquivalent(*D1, *D2)) {
    return false;
  }

  if (D1->isUnion() != D2->isUnion()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(), Context.getApplicableDiagnostic(
                                           diag::err_odr_tag_type_inconsistent))
          << Context.ToCtx.getTypeDeclType(D2);
      Context.Diag1(D1->getLocation(), diag::note_odr_tag_kind_here)
          << D1->getDeclName() << (unsigned)D1->getTagKind();
    }
    return false;
  }

  if (!D1->getDeclName() && !D2->getDeclName()) {
    // If both anonymous structs/unions are in a record context, make sure
    // they occur in the same location in the context records.
    if (std::optional<unsigned> Index1 =
            StructuralEquivalenceContext::findUntaggedStructOrUnionIndex(D1)) {
      if (std::optional<unsigned> Index2 =
              StructuralEquivalenceContext::findUntaggedStructOrUnionIndex(
                  D2)) {
        if (*Index1 != *Index2)
          return false;
      }
    }
  }

  // If the records occur in different context (namespace), these should be
  // different. This is specially important if the definition of one or both
  // records is missing.
  if (!IsRecordContextStructurallyEquivalent(Context, D1, D2))
    return false;

  // If both declarations are class template specializations, we know
  // the ODR applies, so check the template and template arguments.
  const auto *Spec1 = dyn_cast<ClassTemplateSpecializationDecl>(D1);
  const auto *Spec2 = dyn_cast<ClassTemplateSpecializationDecl>(D2);
  if (Spec1 && Spec2) {
    // Check that the specialized templates are the same.
    if (!IsStructurallyEquivalent(Context, Spec1->getSpecializedTemplate(),
                                  Spec2->getSpecializedTemplate()))
      return false;

    // Check that the template arguments are the same.
    if (Spec1->getTemplateArgs().size() != Spec2->getTemplateArgs().size())
      return false;

    for (unsigned I = 0, N = Spec1->getTemplateArgs().size(); I != N; ++I)
      if (!IsStructurallyEquivalent(Context, Spec1->getTemplateArgs().get(I),
                                    Spec2->getTemplateArgs().get(I)))
        return false;
  }
  // If one is a class template specialization and the other is not, these
  // structures are different.
  else if (Spec1 || Spec2)
    return false;

  // Compare the definitions of these two records. If either or both are
  // incomplete (i.e. it is a forward decl), we assume that they are
  // equivalent.
  D1 = D1->getDefinition();
  D2 = D2->getDefinition();
  if (!D1 || !D2)
    return true;

  // If any of the records has external storage and we do a minimal check (or
  // AST import) we assume they are equivalent. (If we didn't have this
  // assumption then `RecordDecl::LoadFieldsFromExternalStorage` could trigger
  // another AST import which in turn would call the structural equivalency
  // check again and finally we'd have an improper result.)
  if (Context.EqKind == StructuralEquivalenceKind::Minimal)
    if (D1->hasExternalLexicalStorage() || D2->hasExternalLexicalStorage())
      return true;

  // If one definition is currently being defined, we do not compare for
  // equality and we assume that the decls are equal.
  if (D1->isBeingDefined() || D2->isBeingDefined())
    return true;

  if (auto *D1CXX = dyn_cast<CXXRecordDecl>(D1)) {
    if (auto *D2CXX = dyn_cast<CXXRecordDecl>(D2)) {
      if (D1CXX->hasExternalLexicalStorage() &&
          !D1CXX->isCompleteDefinition()) {
        D1CXX->getASTContext().getExternalSource()->CompleteType(D1CXX);
      }

      if (D1CXX->isLambda() != D2CXX->isLambda())
        return false;
      if (D1CXX->isLambda()) {
        if (!IsStructurallyEquivalentLambdas(Context, D1CXX, D2CXX))
          return false;
      }

      if (D1CXX->getNumBases() != D2CXX->getNumBases()) {
        if (Context.Complain) {
          Context.Diag2(D2->getLocation(),
                        Context.getApplicableDiagnostic(
                            diag::err_odr_tag_type_inconsistent))
              << Context.ToCtx.getTypeDeclType(D2);
          Context.Diag2(D2->getLocation(), diag::note_odr_number_of_bases)
              << D2CXX->getNumBases();
          Context.Diag1(D1->getLocation(), diag::note_odr_number_of_bases)
              << D1CXX->getNumBases();
        }
        return false;
      }

      // Check the base classes.
      for (CXXRecordDecl::base_class_iterator Base1 = D1CXX->bases_begin(),
                                              BaseEnd1 = D1CXX->bases_end(),
                                              Base2 = D2CXX->bases_begin();
           Base1 != BaseEnd1; ++Base1, ++Base2) {
        if (!IsStructurallyEquivalent(Context, Base1->getType(),
                                      Base2->getType())) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(),
                          Context.getApplicableDiagnostic(
                              diag::err_odr_tag_type_inconsistent))
                << Context.ToCtx.getTypeDeclType(D2);
            Context.Diag2(Base2->getBeginLoc(), diag::note_odr_base)
                << Base2->getType() << Base2->getSourceRange();
            Context.Diag1(Base1->getBeginLoc(), diag::note_odr_base)
                << Base1->getType() << Base1->getSourceRange();
          }
          return false;
        }

        // Check virtual vs. non-virtual inheritance mismatch.
        if (Base1->isVirtual() != Base2->isVirtual()) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(),
                          Context.getApplicableDiagnostic(
                              diag::err_odr_tag_type_inconsistent))
                << Context.ToCtx.getTypeDeclType(D2);
            Context.Diag2(Base2->getBeginLoc(), diag::note_odr_virtual_base)
                << Base2->isVirtual() << Base2->getSourceRange();
            Context.Diag1(Base1->getBeginLoc(), diag::note_odr_base)
                << Base1->isVirtual() << Base1->getSourceRange();
          }
          return false;
        }
      }

      // Check the friends for consistency.
      CXXRecordDecl::friend_iterator Friend2 = D2CXX->friend_begin(),
                                     Friend2End = D2CXX->friend_end();
      for (CXXRecordDecl::friend_iterator Friend1 = D1CXX->friend_begin(),
                                          Friend1End = D1CXX->friend_end();
           Friend1 != Friend1End; ++Friend1, ++Friend2) {
        if (Friend2 == Friend2End) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(),
                          Context.getApplicableDiagnostic(
                              diag::err_odr_tag_type_inconsistent))
                << Context.ToCtx.getTypeDeclType(D2CXX);
            Context.Diag1((*Friend1)->getFriendLoc(), diag::note_odr_friend);
            Context.Diag2(D2->getLocation(), diag::note_odr_missing_friend);
          }
          return false;
        }

        if (!IsStructurallyEquivalent(Context, *Friend1, *Friend2)) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(),
                          Context.getApplicableDiagnostic(
                              diag::err_odr_tag_type_inconsistent))
                << Context.ToCtx.getTypeDeclType(D2CXX);
            Context.Diag1((*Friend1)->getFriendLoc(), diag::note_odr_friend);
            Context.Diag2((*Friend2)->getFriendLoc(), diag::note_odr_friend);
          }
          return false;
        }
      }

      if (Friend2 != Friend2End) {
        if (Context.Complain) {
          Context.Diag2(D2->getLocation(),
                        Context.getApplicableDiagnostic(
                            diag::err_odr_tag_type_inconsistent))
              << Context.ToCtx.getTypeDeclType(D2);
          Context.Diag2((*Friend2)->getFriendLoc(), diag::note_odr_friend);
          Context.Diag1(D1->getLocation(), diag::note_odr_missing_friend);
        }
        return false;
      }
    } else if (D1CXX->getNumBases() > 0) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(),
                      Context.getApplicableDiagnostic(
                          diag::err_odr_tag_type_inconsistent))
            << Context.ToCtx.getTypeDeclType(D2);
        const CXXBaseSpecifier *Base1 = D1CXX->bases_begin();
        Context.Diag1(Base1->getBeginLoc(), diag::note_odr_base)
            << Base1->getType() << Base1->getSourceRange();
        Context.Diag2(D2->getLocation(), diag::note_odr_missing_base);
      }
      return false;
    }
  }

  // Check the fields for consistency.
  QualType D2Type = Context.ToCtx.getTypeDeclType(D2);
  RecordDecl::field_iterator Field2 = D2->field_begin(),
                             Field2End = D2->field_end();
  for (RecordDecl::field_iterator Field1 = D1->field_begin(),
                                  Field1End = D1->field_end();
       Field1 != Field1End; ++Field1, ++Field2) {
    if (Field2 == Field2End) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(),
                      Context.getApplicableDiagnostic(
                          diag::err_odr_tag_type_inconsistent))
            << Context.ToCtx.getTypeDeclType(D2);
        Context.Diag1(Field1->getLocation(), diag::note_odr_field)
            << Field1->getDeclName() << Field1->getType();
        Context.Diag2(D2->getLocation(), diag::note_odr_missing_field);
      }
      return false;
    }

    if (!IsStructurallyEquivalent(Context, *Field1, *Field2, D2Type))
      return false;
  }

  if (Field2 != Field2End) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(), Context.getApplicableDiagnostic(
                                           diag::err_odr_tag_type_inconsistent))
          << Context.ToCtx.getTypeDeclType(D2);
      Context.Diag2(Field2->getLocation(), diag::note_odr_field)
          << Field2->getDeclName() << Field2->getType();
      Context.Diag1(D1->getLocation(), diag::note_odr_missing_field);
    }
    return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     EnumConstantDecl *D1,
                                     EnumConstantDecl *D2) {
  const llvm::APSInt &FromVal = D1->getInitVal();
  const llvm::APSInt &ToVal = D2->getInitVal();
  if (FromVal.isSigned() != ToVal.isSigned())
    return false;
  if (FromVal.getBitWidth() != ToVal.getBitWidth())
    return false;
  if (FromVal != ToVal)
    return false;

  if (!IsStructurallyEquivalent(D1->getIdentifier(), D2->getIdentifier()))
    return false;

  // Init expressions are the most expensive check, so do them last.
  return IsStructurallyEquivalent(Context, D1->getInitExpr(),
                                  D2->getInitExpr());
}

/// Determine structural equivalence of two enums.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     EnumDecl *D1, EnumDecl *D2) {
  if (!NameIsStructurallyEquivalent(*D1, *D2)) {
    return false;
  }

  // Compare the definitions of these two enums. If either or both are
  // incomplete (i.e. forward declared), we assume that they are equivalent.
  D1 = D1->getDefinition();
  D2 = D2->getDefinition();
  if (!D1 || !D2)
    return true;

  EnumDecl::enumerator_iterator EC2 = D2->enumerator_begin(),
                                EC2End = D2->enumerator_end();
  for (EnumDecl::enumerator_iterator EC1 = D1->enumerator_begin(),
                                     EC1End = D1->enumerator_end();
       EC1 != EC1End; ++EC1, ++EC2) {
    if (EC2 == EC2End) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(),
                      Context.getApplicableDiagnostic(
                          diag::err_odr_tag_type_inconsistent))
            << Context.ToCtx.getTypeDeclType(D2);
        Context.Diag1(EC1->getLocation(), diag::note_odr_enumerator)
            << EC1->getDeclName() << toString(EC1->getInitVal(), 10);
        Context.Diag2(D2->getLocation(), diag::note_odr_missing_enumerator);
      }
      return false;
    }

    llvm::APSInt Val1 = EC1->getInitVal();
    llvm::APSInt Val2 = EC2->getInitVal();
    if (!llvm::APSInt::isSameValue(Val1, Val2) ||
        !IsStructurallyEquivalent(EC1->getIdentifier(), EC2->getIdentifier())) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(),
                      Context.getApplicableDiagnostic(
                          diag::err_odr_tag_type_inconsistent))
            << Context.ToCtx.getTypeDeclType(D2);
        Context.Diag2(EC2->getLocation(), diag::note_odr_enumerator)
            << EC2->getDeclName() << toString(EC2->getInitVal(), 10);
        Context.Diag1(EC1->getLocation(), diag::note_odr_enumerator)
            << EC1->getDeclName() << toString(EC1->getInitVal(), 10);
      }
      return false;
    }
  }

  if (EC2 != EC2End) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(), Context.getApplicableDiagnostic(
                                           diag::err_odr_tag_type_inconsistent))
          << Context.ToCtx.getTypeDeclType(D2);
      Context.Diag2(EC2->getLocation(), diag::note_odr_enumerator)
          << EC2->getDeclName() << toString(EC2->getInitVal(), 10);
      Context.Diag1(D1->getLocation(), diag::note_odr_missing_enumerator);
    }
    return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TemplateParameterList *Params1,
                                     TemplateParameterList *Params2) {
  if (Params1->size() != Params2->size()) {
    if (Context.Complain) {
      Context.Diag2(Params2->getTemplateLoc(),
                    Context.getApplicableDiagnostic(
                        diag::err_odr_different_num_template_parameters))
          << Params1->size() << Params2->size();
      Context.Diag1(Params1->getTemplateLoc(),
                    diag::note_odr_template_parameter_list);
    }
    return false;
  }

  for (unsigned I = 0, N = Params1->size(); I != N; ++I) {
    if (Params1->getParam(I)->getKind() != Params2->getParam(I)->getKind()) {
      if (Context.Complain) {
        Context.Diag2(Params2->getParam(I)->getLocation(),
                      Context.getApplicableDiagnostic(
                          diag::err_odr_different_template_parameter_kind));
        Context.Diag1(Params1->getParam(I)->getLocation(),
                      diag::note_odr_template_parameter_here);
      }
      return false;
    }

    if (!IsStructurallyEquivalent(Context, Params1->getParam(I),
                                  Params2->getParam(I)))
      return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TemplateTypeParmDecl *D1,
                                     TemplateTypeParmDecl *D2) {
  if (D1->isParameterPack() != D2->isParameterPack()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    Context.getApplicableDiagnostic(
                        diag::err_odr_parameter_pack_non_pack))
          << D2->isParameterPack();
      Context.Diag1(D1->getLocation(), diag::note_odr_parameter_pack_non_pack)
          << D1->isParameterPack();
    }
    return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     NonTypeTemplateParmDecl *D1,
                                     NonTypeTemplateParmDecl *D2) {
  if (D1->isParameterPack() != D2->isParameterPack()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    Context.getApplicableDiagnostic(
                        diag::err_odr_parameter_pack_non_pack))
          << D2->isParameterPack();
      Context.Diag1(D1->getLocation(), diag::note_odr_parameter_pack_non_pack)
          << D1->isParameterPack();
    }
    return false;
  }
  if (!Context.IgnoreTemplateParmDepth && D1->getDepth() != D2->getDepth())
    return false;
  if (D1->getIndex() != D2->getIndex())
    return false;
  // Check types.
  if (!IsStructurallyEquivalent(Context, D1->getType(), D2->getType())) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    Context.getApplicableDiagnostic(
                        diag::err_odr_non_type_parameter_type_inconsistent))
          << D2->getType() << D1->getType();
      Context.Diag1(D1->getLocation(), diag::note_odr_value_here)
          << D1->getType();
    }
    return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TemplateTemplateParmDecl *D1,
                                     TemplateTemplateParmDecl *D2) {
  if (D1->isParameterPack() != D2->isParameterPack()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    Context.getApplicableDiagnostic(
                        diag::err_odr_parameter_pack_non_pack))
          << D2->isParameterPack();
      Context.Diag1(D1->getLocation(), diag::note_odr_parameter_pack_non_pack)
          << D1->isParameterPack();
    }
    return false;
  }

  // Check template parameter lists.
  return IsStructurallyEquivalent(Context, D1->getTemplateParameters(),
                                  D2->getTemplateParameters());
}

static bool IsTemplateDeclCommonStructurallyEquivalent(
    StructuralEquivalenceContext &Ctx, TemplateDecl *D1, TemplateDecl *D2) {
  if (!IsStructurallyEquivalent(D1->getIdentifier(), D2->getIdentifier()))
    return false;
  if (!D1->getIdentifier()) // Special name
    if (D1->getNameAsString() != D2->getNameAsString())
      return false;
  return IsStructurallyEquivalent(Ctx, D1->getTemplateParameters(),
                                  D2->getTemplateParameters());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ClassTemplateDecl *D1,
                                     ClassTemplateDecl *D2) {
  // Check template parameters.
  if (!IsTemplateDeclCommonStructurallyEquivalent(Context, D1, D2))
    return false;

  // Check the templated declaration.
  return IsStructurallyEquivalent(Context, D1->getTemplatedDecl(),
                                  D2->getTemplatedDecl());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FunctionTemplateDecl *D1,
                                     FunctionTemplateDecl *D2) {
  // Check template parameters.
  if (!IsTemplateDeclCommonStructurallyEquivalent(Context, D1, D2))
    return false;

  // Check the templated declaration.
  return IsStructurallyEquivalent(Context, D1->getTemplatedDecl()->getType(),
                                  D2->getTemplatedDecl()->getType());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TypeAliasTemplateDecl *D1,
                                     TypeAliasTemplateDecl *D2) {
  // Check template parameters.
  if (!IsTemplateDeclCommonStructurallyEquivalent(Context, D1, D2))
    return false;

  // Check the templated declaration.
  return IsStructurallyEquivalent(Context, D1->getTemplatedDecl(),
                                  D2->getTemplatedDecl());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ConceptDecl *D1,
                                     ConceptDecl *D2) {
  // Check template parameters.
  if (!IsTemplateDeclCommonStructurallyEquivalent(Context, D1, D2))
    return false;

  // Check the constraint expression.
  return IsStructurallyEquivalent(Context, D1->getConstraintExpr(),
                                  D2->getConstraintExpr());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FriendDecl *D1, FriendDecl *D2) {
  if ((D1->getFriendType() && D2->getFriendDecl()) ||
      (D1->getFriendDecl() && D2->getFriendType())) {
      return false;
  }
  if (D1->getFriendType() && D2->getFriendType())
    return IsStructurallyEquivalent(Context,
                                    D1->getFriendType()->getType(),
                                    D2->getFriendType()->getType());
  if (D1->getFriendDecl() && D2->getFriendDecl())
    return IsStructurallyEquivalent(Context, D1->getFriendDecl(),
                                    D2->getFriendDecl());
  return false;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TypedefNameDecl *D1, TypedefNameDecl *D2) {
  if (!IsStructurallyEquivalent(D1->getIdentifier(), D2->getIdentifier()))
    return false;

  return IsStructurallyEquivalent(Context, D1->getUnderlyingType(),
                                  D2->getUnderlyingType());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FunctionDecl *D1, FunctionDecl *D2) {
  if (!IsStructurallyEquivalent(D1->getIdentifier(), D2->getIdentifier()))
    return false;

  if (D1->isOverloadedOperator()) {
    if (!D2->isOverloadedOperator())
      return false;
    if (D1->getOverloadedOperator() != D2->getOverloadedOperator())
      return false;
  }

  // FIXME: Consider checking for function attributes as well.
  if (!IsStructurallyEquivalent(Context, D1->getType(), D2->getType()))
    return false;

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ObjCIvarDecl *D1, ObjCIvarDecl *D2,
                                     QualType Owner2Type) {
  if (D1->getAccessControl() != D2->getAccessControl())
    return false;

  return IsStructurallyEquivalent(Context, cast<FieldDecl>(D1),
                                  cast<FieldDecl>(D2), Owner2Type);
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ObjCIvarDecl *D1, ObjCIvarDecl *D2) {
  QualType Owner2Type =
      Context.ToCtx.getObjCInterfaceType(D2->getContainingInterface());
  return IsStructurallyEquivalent(Context, D1, D2, Owner2Type);
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ObjCMethodDecl *Method1,
                                     ObjCMethodDecl *Method2) {
  bool PropertiesEqual =
      Method1->isInstanceMethod() == Method2->isInstanceMethod() &&
      Method1->isVariadic() == Method2->isVariadic() &&
      Method1->isDirectMethod() == Method2->isDirectMethod();
  if (!PropertiesEqual)
    return false;

  // Compare selector slot names.
  Selector Selector1 = Method1->getSelector(),
           Selector2 = Method2->getSelector();
  unsigned NumArgs = Selector1.getNumArgs();
  if (NumArgs != Selector2.getNumArgs())
    return false;
  // Compare all selector slots. For selectors with arguments it means all arg
  // slots. And if there are no arguments, compare the first-and-only slot.
  unsigned SlotsToCheck = NumArgs > 0 ? NumArgs : 1;
  for (unsigned I = 0; I < SlotsToCheck; ++I) {
    if (!IsStructurallyEquivalent(Selector1.getIdentifierInfoForSlot(I),
                                  Selector2.getIdentifierInfoForSlot(I)))
      return false;
  }

  // Compare types.
  if (!IsStructurallyEquivalent(Context, Method1->getReturnType(),
                                Method2->getReturnType()))
    return false;
  assert(
      Method1->param_size() == Method2->param_size() &&
      "Same number of arguments should be already enforced in Selector checks");
  for (ObjCMethodDecl::param_type_iterator
           ParamT1 = Method1->param_type_begin(),
           ParamT1End = Method1->param_type_end(),
           ParamT2 = Method2->param_type_begin(),
           ParamT2End = Method2->param_type_end();
       (ParamT1 != ParamT1End) && (ParamT2 != ParamT2End);
       ++ParamT1, ++ParamT2) {
    if (!IsStructurallyEquivalent(Context, *ParamT1, *ParamT2))
      return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ObjCCategoryDecl *D1,
                                     ObjCCategoryDecl *D2) {
  if (!IsStructurallyEquivalent(D1->getIdentifier(), D2->getIdentifier()))
    return false;

  const ObjCInterfaceDecl *Intf1 = D1->getClassInterface(),
                          *Intf2 = D2->getClassInterface();
  if ((!Intf1 || !Intf2) && (Intf1 != Intf2))
    return false;

  if (Intf1 &&
      !IsStructurallyEquivalent(Intf1->getIdentifier(), Intf2->getIdentifier()))
    return false;

  // Compare protocols.
  ObjCCategoryDecl::protocol_iterator Protocol2 = D2->protocol_begin(),
                                      Protocol2End = D2->protocol_end();
  for (ObjCCategoryDecl::protocol_iterator Protocol1 = D1->protocol_begin(),
                                           Protocol1End = D1->protocol_end();
       Protocol1 != Protocol1End; ++Protocol1, ++Protocol2) {
    if (Protocol2 == Protocol2End)
      return false;
    if (!IsStructurallyEquivalent((*Protocol1)->getIdentifier(),
                                  (*Protocol2)->getIdentifier()))
      return false;
  }
  if (Protocol2 != Protocol2End)
    return false;

  // Compare ivars.
  QualType D2Type =
      Intf2 ? Context.ToCtx.getObjCInterfaceType(Intf2) : QualType();
  ObjCCategoryDecl::ivar_iterator Ivar2 = D2->ivar_begin(),
                                  Ivar2End = D2->ivar_end();
  for (ObjCCategoryDecl::ivar_iterator Ivar1 = D1->ivar_begin(),
                                       Ivar1End = D1->ivar_end();
       Ivar1 != Ivar1End; ++Ivar1, ++Ivar2) {
    if (Ivar2 == Ivar2End)
      return false;
    if (!IsStructurallyEquivalent(Context, *Ivar1, *Ivar2, D2Type))
      return false;
  }
  if (Ivar2 != Ivar2End)
    return false;

  // Compare methods.
  ObjCCategoryDecl::method_iterator Method2 = D2->meth_begin(),
                                    Method2End = D2->meth_end();
  for (ObjCCategoryDecl::method_iterator Method1 = D1->meth_begin(),
                                         Method1End = D1->meth_end();
       Method1 != Method1End; ++Method1, ++Method2) {
    if (Method2 == Method2End)
      return false;
    if (!IsStructurallyEquivalent(Context, *Method1, *Method2))
      return false;
  }
  if (Method2 != Method2End)
    return false;

  return true;
}

/// Determine structural equivalence of two declarations.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     Decl *D1, Decl *D2) {
  // FIXME: Check for known structural equivalences via a callback of some sort.

  D1 = D1->getCanonicalDecl();
  D2 = D2->getCanonicalDecl();
  std::pair<Decl *, Decl *> P{D1, D2};

  // Check whether we already know that these two declarations are not
  // structurally equivalent.
  if (Context.NonEquivalentDecls.count(P))
    return false;

  // Check if a check for these declarations is already pending.
  // If yes D1 and D2 will be checked later (from DeclsToCheck),
  // or these are already checked (and equivalent).
  bool Inserted = Context.VisitedDecls.insert(P).second;
  if (!Inserted)
    return true;

  Context.DeclsToCheck.push(P);

  return true;
}

DiagnosticBuilder StructuralEquivalenceContext::Diag1(SourceLocation Loc,
                                                      unsigned DiagID) {
  assert(Complain && "Not allowed to complain");
  if (LastDiagFromC2)
    FromCtx.getDiagnostics().notePriorDiagnosticFrom(ToCtx.getDiagnostics());
  LastDiagFromC2 = false;
  return FromCtx.getDiagnostics().Report(Loc, DiagID);
}

DiagnosticBuilder StructuralEquivalenceContext::Diag2(SourceLocation Loc,
                                                      unsigned DiagID) {
  assert(Complain && "Not allowed to complain");
  if (!LastDiagFromC2)
    ToCtx.getDiagnostics().notePriorDiagnosticFrom(FromCtx.getDiagnostics());
  LastDiagFromC2 = true;
  return ToCtx.getDiagnostics().Report(Loc, DiagID);
}

std::optional<unsigned>
StructuralEquivalenceContext::findUntaggedStructOrUnionIndex(RecordDecl *Anon) {
  ASTContext &Context = Anon->getASTContext();
  QualType AnonTy = Context.getRecordType(Anon);

  const auto *Owner = dyn_cast<RecordDecl>(Anon->getDeclContext());
  if (!Owner)
    return std::nullopt;

  unsigned Index = 0;
  for (const auto *D : Owner->noload_decls()) {
    const auto *F = dyn_cast<FieldDecl>(D);
    if (!F)
      continue;

    if (F->isAnonymousStructOrUnion()) {
      if (Context.hasSameType(F->getType(), AnonTy))
        break;
      ++Index;
      continue;
    }

    // If the field looks like this:
    // struct { ... } A;
    QualType FieldType = F->getType();
    // In case of nested structs.
    while (const auto *ElabType = dyn_cast<ElaboratedType>(FieldType))
      FieldType = ElabType->getNamedType();

    if (const auto *RecType = dyn_cast<RecordType>(FieldType)) {
      const RecordDecl *RecDecl = RecType->getDecl();
      if (RecDecl->getDeclContext() == Owner && !RecDecl->getIdentifier()) {
        if (Context.hasSameType(FieldType, AnonTy))
          break;
        ++Index;
        continue;
      }
    }
  }

  return Index;
}

unsigned StructuralEquivalenceContext::getApplicableDiagnostic(
    unsigned ErrorDiagnostic) {
  if (ErrorOnTagTypeMismatch)
    return ErrorDiagnostic;

  switch (ErrorDiagnostic) {
  case diag::err_odr_variable_type_inconsistent:
    return diag::warn_odr_variable_type_inconsistent;
  case diag::err_odr_variable_multiple_def:
    return diag::warn_odr_variable_multiple_def;
  case diag::err_odr_function_type_inconsistent:
    return diag::warn_odr_function_type_inconsistent;
  case diag::err_odr_tag_type_inconsistent:
    return diag::warn_odr_tag_type_inconsistent;
  case diag::err_odr_field_type_inconsistent:
    return diag::warn_odr_field_type_inconsistent;
  case diag::err_odr_ivar_type_inconsistent:
    return diag::warn_odr_ivar_type_inconsistent;
  case diag::err_odr_objc_superclass_inconsistent:
    return diag::warn_odr_objc_superclass_inconsistent;
  case diag::err_odr_objc_method_result_type_inconsistent:
    return diag::warn_odr_objc_method_result_type_inconsistent;
  case diag::err_odr_objc_method_num_params_inconsistent:
    return diag::warn_odr_objc_method_num_params_inconsistent;
  case diag::err_odr_objc_method_param_type_inconsistent:
    return diag::warn_odr_objc_method_param_type_inconsistent;
  case diag::err_odr_objc_method_variadic_inconsistent:
    return diag::warn_odr_objc_method_variadic_inconsistent;
  case diag::err_odr_objc_property_type_inconsistent:
    return diag::warn_odr_objc_property_type_inconsistent;
  case diag::err_odr_objc_property_impl_kind_inconsistent:
    return diag::warn_odr_objc_property_impl_kind_inconsistent;
  case diag::err_odr_objc_synthesize_ivar_inconsistent:
    return diag::warn_odr_objc_synthesize_ivar_inconsistent;
  case diag::err_odr_different_num_template_parameters:
    return diag::warn_odr_different_num_template_parameters;
  case diag::err_odr_different_template_parameter_kind:
    return diag::warn_odr_different_template_parameter_kind;
  case diag::err_odr_parameter_pack_non_pack:
    return diag::warn_odr_parameter_pack_non_pack;
  case diag::err_odr_non_type_parameter_type_inconsistent:
    return diag::warn_odr_non_type_parameter_type_inconsistent;
  }
  llvm_unreachable("Diagnostic kind not handled in preceding switch");
}

bool StructuralEquivalenceContext::IsEquivalent(Decl *D1, Decl *D2) {

  // Ensure that the implementation functions (all static functions in this TU)
  // never call the public ASTStructuralEquivalence::IsEquivalent() functions,
  // because that will wreak havoc the internal state (DeclsToCheck and
  // VisitedDecls members) and can cause faulty behaviour.
  // In other words: Do not start a graph search from a new node with the
  // internal data of another search in progress.
  // FIXME: Better encapsulation and separation of internal and public
  // functionality.
  assert(DeclsToCheck.empty());
  assert(VisitedDecls.empty());

  if (!::IsStructurallyEquivalent(*this, D1, D2))
    return false;

  return !Finish();
}

bool StructuralEquivalenceContext::IsEquivalent(QualType T1, QualType T2) {
  assert(DeclsToCheck.empty());
  assert(VisitedDecls.empty());
  if (!::IsStructurallyEquivalent(*this, T1, T2))
    return false;

  return !Finish();
}

bool StructuralEquivalenceContext::IsEquivalent(Stmt *S1, Stmt *S2) {
  assert(DeclsToCheck.empty());
  assert(VisitedDecls.empty());
  if (!::IsStructurallyEquivalent(*this, S1, S2))
    return false;

  return !Finish();
}

bool StructuralEquivalenceContext::CheckCommonEquivalence(Decl *D1, Decl *D2) {
  // Check for equivalent described template.
  TemplateDecl *Template1 = D1->getDescribedTemplate();
  TemplateDecl *Template2 = D2->getDescribedTemplate();
  if ((Template1 != nullptr) != (Template2 != nullptr))
    return false;
  if (Template1 && !IsStructurallyEquivalent(*this, Template1, Template2))
    return false;

  // FIXME: Move check for identifier names into this function.

  return true;
}

bool StructuralEquivalenceContext::CheckKindSpecificEquivalence(
    Decl *D1, Decl *D2) {

  // Kind mismatch.
  if (D1->getKind() != D2->getKind())
    return false;

  // Cast the Decls to their actual subclass so that the right overload of
  // IsStructurallyEquivalent is called.
  switch (D1->getKind()) {
#define ABSTRACT_DECL(DECL)
#define DECL(DERIVED, BASE)                                                    \
  case Decl::Kind::DERIVED:                                                    \
    return ::IsStructurallyEquivalent(*this, static_cast<DERIVED##Decl *>(D1), \
                                      static_cast<DERIVED##Decl *>(D2));
#include "clang/AST/DeclNodes.inc"
  }
  return true;
}

bool StructuralEquivalenceContext::Finish() {
  while (!DeclsToCheck.empty()) {
    // Check the next declaration.
    std::pair<Decl *, Decl *> P = DeclsToCheck.front();
    DeclsToCheck.pop();

    Decl *D1 = P.first;
    Decl *D2 = P.second;

    bool Equivalent =
        CheckCommonEquivalence(D1, D2) && CheckKindSpecificEquivalence(D1, D2);

    if (!Equivalent) {
      // Note that these two declarations are not equivalent (and we already
      // know about it).
      NonEquivalentDecls.insert(P);

      return true;
    }
  }

  return false;
}
