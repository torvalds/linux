//===--- ASTTypeTraits.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Provides a dynamic type identifier and a dynamically typed node container
//  that can be used to store an AST base node at runtime in the same storage in
//  a type safe way.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/TypeLoc.h"

using namespace clang;

const ASTNodeKind::KindInfo ASTNodeKind::AllKindInfo[] = {
    {NKI_None, "<None>"},
    {NKI_None, "TemplateArgument"},
    {NKI_None, "TemplateArgumentLoc"},
    {NKI_None, "LambdaCapture"},
    {NKI_None, "TemplateName"},
    {NKI_None, "NestedNameSpecifierLoc"},
    {NKI_None, "QualType"},
#define TYPELOC(CLASS, PARENT) {NKI_##PARENT, #CLASS "TypeLoc"},
#include "clang/AST/TypeLocNodes.def"
    {NKI_None, "TypeLoc"},
    {NKI_None, "CXXBaseSpecifier"},
    {NKI_None, "CXXCtorInitializer"},
    {NKI_None, "NestedNameSpecifier"},
    {NKI_None, "Decl"},
#define DECL(DERIVED, BASE) { NKI_##BASE, #DERIVED "Decl" },
#include "clang/AST/DeclNodes.inc"
    {NKI_None, "Stmt"},
#define STMT(DERIVED, BASE) { NKI_##BASE, #DERIVED },
#include "clang/AST/StmtNodes.inc"
    {NKI_None, "Type"},
#define TYPE(DERIVED, BASE) { NKI_##BASE, #DERIVED "Type" },
#include "clang/AST/TypeNodes.inc"
    {NKI_None, "OMPClause"},
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) {NKI_OMPClause, #Class},
#include "llvm/Frontend/OpenMP/OMP.inc"
    {NKI_None, "Attr"},
#define ATTR(A) {NKI_Attr, #A "Attr"},
#include "clang/Basic/AttrList.inc"
    {NKI_None, "ObjCProtocolLoc"},
    {NKI_None, "ConceptReference"},
};

bool ASTNodeKind::isBaseOf(ASTNodeKind Other) const {
  return isBaseOf(KindId, Other.KindId);
}

bool ASTNodeKind::isBaseOf(ASTNodeKind Other, unsigned *Distance) const {
  return isBaseOf(KindId, Other.KindId, Distance);
}

bool ASTNodeKind::isBaseOf(NodeKindId Base, NodeKindId Derived) {
  if (Base == NKI_None || Derived == NKI_None)
    return false;
  while (Derived != Base && Derived != NKI_None) {
    Derived = AllKindInfo[Derived].ParentId;
  }
  return Derived == Base;
}

bool ASTNodeKind::isBaseOf(NodeKindId Base, NodeKindId Derived,
                           unsigned *Distance) {
  if (Base == NKI_None || Derived == NKI_None) return false;
  unsigned Dist = 0;
  while (Derived != Base && Derived != NKI_None) {
    Derived = AllKindInfo[Derived].ParentId;
    ++Dist;
  }
  if (Distance)
    *Distance = Dist;
  return Derived == Base;
}

ASTNodeKind ASTNodeKind::getCladeKind() const {
  NodeKindId LastId = KindId;
  while (LastId) {
    NodeKindId ParentId = AllKindInfo[LastId].ParentId;
    if (ParentId == NKI_None)
      return LastId;
    LastId = ParentId;
  }
  return NKI_None;
}

StringRef ASTNodeKind::asStringRef() const { return AllKindInfo[KindId].Name; }

ASTNodeKind ASTNodeKind::getMostDerivedType(ASTNodeKind Kind1,
                                            ASTNodeKind Kind2) {
  if (Kind1.isBaseOf(Kind2)) return Kind2;
  if (Kind2.isBaseOf(Kind1)) return Kind1;
  return ASTNodeKind();
}

ASTNodeKind ASTNodeKind::getMostDerivedCommonAncestor(ASTNodeKind Kind1,
                                                      ASTNodeKind Kind2) {
  NodeKindId Parent = Kind1.KindId;
  while (!isBaseOf(Parent, Kind2.KindId) && Parent != NKI_None) {
    Parent = AllKindInfo[Parent].ParentId;
  }
  return ASTNodeKind(Parent);
}

ASTNodeKind ASTNodeKind::getFromNode(const Decl &D) {
  switch (D.getKind()) {
#define DECL(DERIVED, BASE)                                                    \
    case Decl::DERIVED: return ASTNodeKind(NKI_##DERIVED##Decl);
#define ABSTRACT_DECL(D)
#include "clang/AST/DeclNodes.inc"
  };
  llvm_unreachable("invalid decl kind");
}

ASTNodeKind ASTNodeKind::getFromNode(const Stmt &S) {
  switch (S.getStmtClass()) {
    case Stmt::NoStmtClass: return NKI_None;
#define STMT(CLASS, PARENT)                                                    \
    case Stmt::CLASS##Class: return ASTNodeKind(NKI_##CLASS);
#define ABSTRACT_STMT(S)
#include "clang/AST/StmtNodes.inc"
  }
  llvm_unreachable("invalid stmt kind");
}

ASTNodeKind ASTNodeKind::getFromNode(const Type &T) {
  switch (T.getTypeClass()) {
#define TYPE(Class, Base)                                                      \
    case Type::Class: return ASTNodeKind(NKI_##Class##Type);
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.inc"
  }
  llvm_unreachable("invalid type kind");
 }

 ASTNodeKind ASTNodeKind::getFromNode(const TypeLoc &T) {
   switch (T.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case TypeLoc::CLASS:                                                         \
    return ASTNodeKind(NKI_##CLASS##TypeLoc);
#include "clang/AST/TypeLocNodes.def"
   }
   llvm_unreachable("invalid typeloc kind");
 }

ASTNodeKind ASTNodeKind::getFromNode(const OMPClause &C) {
  switch (C.getClauseKind()) {
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class)                                         \
  case llvm::omp::Clause::Enum:                                                \
    return ASTNodeKind(NKI_##Class);
#define CLAUSE_NO_CLASS(Enum, Str)                                             \
  case llvm::omp::Clause::Enum:                                                \
    llvm_unreachable("unexpected OpenMP clause kind");
#include "llvm/Frontend/OpenMP/OMP.inc"
  }
  llvm_unreachable("invalid omp clause kind");
}

ASTNodeKind ASTNodeKind::getFromNode(const Attr &A) {
  switch (A.getKind()) {
#define ATTR(A)                                                                \
  case attr::A:                                                                \
    return ASTNodeKind(NKI_##A##Attr);
#include "clang/Basic/AttrList.inc"
  }
  llvm_unreachable("invalid attr kind");
}

void DynTypedNode::print(llvm::raw_ostream &OS,
                         const PrintingPolicy &PP) const {
  if (const TemplateArgument *TA = get<TemplateArgument>())
    TA->print(PP, OS, /*IncludeType*/ true);
  else if (const TemplateArgumentLoc *TAL = get<TemplateArgumentLoc>())
    TAL->getArgument().print(PP, OS, /*IncludeType*/ true);
  else if (const TemplateName *TN = get<TemplateName>())
    TN->print(OS, PP);
  else if (const NestedNameSpecifier *NNS = get<NestedNameSpecifier>())
    NNS->print(OS, PP);
  else if (const NestedNameSpecifierLoc *NNSL = get<NestedNameSpecifierLoc>()) {
    if (const NestedNameSpecifier *NNS = NNSL->getNestedNameSpecifier())
      NNS->print(OS, PP);
    else
      OS << "(empty NestedNameSpecifierLoc)";
  } else if (const QualType *QT = get<QualType>())
    QT->print(OS, PP);
  else if (const TypeLoc *TL = get<TypeLoc>())
    TL->getType().print(OS, PP);
  else if (const Decl *D = get<Decl>())
    D->print(OS, PP);
  else if (const Stmt *S = get<Stmt>())
    S->printPretty(OS, nullptr, PP);
  else if (const Type *T = get<Type>())
    QualType(T, 0).print(OS, PP);
  else if (const Attr *A = get<Attr>())
    A->printPretty(OS, PP);
  else if (const ObjCProtocolLoc *P = get<ObjCProtocolLoc>())
    P->getProtocol()->print(OS, PP);
  else if (const ConceptReference *C = get<ConceptReference>())
    C->print(OS, PP);
  else
    OS << "Unable to print values of type " << NodeKind.asStringRef() << "\n";
}

void DynTypedNode::dump(llvm::raw_ostream &OS,
                        const ASTContext &Context) const {
  if (const Decl *D = get<Decl>())
    D->dump(OS);
  else if (const Stmt *S = get<Stmt>())
    S->dump(OS, Context);
  else if (const Type *T = get<Type>())
    T->dump(OS, Context);
  else if (const ConceptReference *C = get<ConceptReference>())
    C->dump(OS);
  else if (const TypeLoc *TL = get<TypeLoc>())
    TL->dump(OS, Context);
  else
    OS << "Unable to dump values of type " << NodeKind.asStringRef() << "\n";
}

SourceRange DynTypedNode::getSourceRange() const {
  if (const CXXCtorInitializer *CCI = get<CXXCtorInitializer>())
    return CCI->getSourceRange();
  if (const NestedNameSpecifierLoc *NNSL = get<NestedNameSpecifierLoc>())
    return NNSL->getSourceRange();
  if (const TypeLoc *TL = get<TypeLoc>())
    return TL->getSourceRange();
  if (const Decl *D = get<Decl>())
    return D->getSourceRange();
  if (const Stmt *S = get<Stmt>())
    return S->getSourceRange();
  if (const TemplateArgumentLoc *TAL = get<TemplateArgumentLoc>())
    return TAL->getSourceRange();
  if (const auto *C = get<OMPClause>())
    return SourceRange(C->getBeginLoc(), C->getEndLoc());
  if (const auto *CBS = get<CXXBaseSpecifier>())
    return CBS->getSourceRange();
  if (const auto *A = get<Attr>())
    return A->getRange();
  if (const ObjCProtocolLoc *P = get<ObjCProtocolLoc>())
    return P->getSourceRange();
  if (const ConceptReference *C = get<ConceptReference>())
    return C->getSourceRange();
  return SourceRange();
}
