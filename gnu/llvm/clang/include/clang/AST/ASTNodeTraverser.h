//===--- ASTNodeTraverser.h - Traversal of AST nodes ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AST traversal facilities.  Other users
// of this class may make use of the same traversal logic by inheriting it,
// similar to RecursiveASTVisitor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTNODETRAVERSER_H
#define LLVM_CLANG_AST_ASTNODETRAVERSER_H

#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/AttrVisitor.h"
#include "clang/AST/CommentVisitor.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/LocInfoType.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateArgumentVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/AST/TypeVisitor.h"
#include "llvm/Support/SaveAndRestore.h"

namespace clang {

class APValue;

/**

ASTNodeTraverser traverses the Clang AST for dumping purposes.

The `Derived::doGetNodeDelegate()` method is required to be an accessible member
which returns a reference of type `NodeDelegateType &` which implements the
following interface:

struct {
  template <typename Fn> void AddChild(Fn DoAddChild);
  template <typename Fn> void AddChild(StringRef Label, Fn DoAddChild);

  void Visit(const comments::Comment *C, const comments::FullComment *FC);
  void Visit(const Attr *A);
  void Visit(const TemplateArgument &TA, SourceRange R = {},
             const Decl *From = nullptr, StringRef Label = {});
  void Visit(const Stmt *Node);
  void Visit(const Type *T);
  void Visit(QualType T);
  void Visit(TypeLoc);
  void Visit(const Decl *D);
  void Visit(const CXXCtorInitializer *Init);
  void Visit(const OpenACCClause *C);
  void Visit(const OMPClause *C);
  void Visit(const BlockDecl::Capture &C);
  void Visit(const GenericSelectionExpr::ConstAssociation &A);
  void Visit(const concepts::Requirement *R);
  void Visit(const APValue &Value, QualType Ty);
};
*/
template <typename Derived, typename NodeDelegateType>
class ASTNodeTraverser
    : public ConstDeclVisitor<Derived>,
      public ConstStmtVisitor<Derived>,
      public comments::ConstCommentVisitor<Derived, void,
                                           const comments::FullComment *>,
      public TypeVisitor<Derived>,
      public TypeLocVisitor<Derived>,
      public ConstAttrVisitor<Derived>,
      public ConstTemplateArgumentVisitor<Derived> {

  /// Indicates whether we should trigger deserialization of nodes that had
  /// not already been loaded.
  bool Deserialize = false;

  /// Tracks whether we should dump TypeLocs etc.
  ///
  /// Detailed location information such as TypeLoc nodes is not usually
  /// included in the dump (too verbose).
  /// But when explicitly asked to dump a Loc node, we do so recursively,
  /// including e.g. FunctionTypeLoc => ParmVarDecl => TypeLoc.
  bool VisitLocs = false;

  TraversalKind Traversal = TraversalKind::TK_AsIs;

  NodeDelegateType &getNodeDelegate() {
    return getDerived().doGetNodeDelegate();
  }
  Derived &getDerived() { return *static_cast<Derived *>(this); }

public:
  void setDeserialize(bool D) { Deserialize = D; }
  bool getDeserialize() const { return Deserialize; }

  void SetTraversalKind(TraversalKind TK) { Traversal = TK; }
  TraversalKind GetTraversalKind() const { return Traversal; }

  void Visit(const Decl *D, bool VisitLocs = false) {
    if (Traversal == TK_IgnoreUnlessSpelledInSource && D->isImplicit())
      return;

    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(D);
      if (!D)
        return;

      {
        llvm::SaveAndRestore RestoreVisitLocs(this->VisitLocs, VisitLocs);
        ConstDeclVisitor<Derived>::Visit(D);
      }

      for (const auto &A : D->attrs())
        Visit(A);

      if (const comments::FullComment *Comment =
              D->getASTContext().getLocalCommentForDeclUncached(D))
        Visit(Comment, Comment);

      // Decls within functions are visited by the body.
      if (!isa<FunctionDecl, ObjCMethodDecl, BlockDecl>(*D)) {
        if (Traversal != TK_AsIs) {
          if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
            auto SK = CTSD->getSpecializationKind();
            if (SK == TSK_ExplicitInstantiationDeclaration ||
                SK == TSK_ExplicitInstantiationDefinition)
              return;
          }
        }
        if (const auto *DC = dyn_cast<DeclContext>(D))
          dumpDeclContext(DC);
      }
    });
  }

  void Visit(const Stmt *Node, StringRef Label = {}) {
    getNodeDelegate().AddChild(Label, [=] {
      const Stmt *S = Node;

      if (auto *E = dyn_cast_or_null<Expr>(S)) {
        switch (Traversal) {
        case TK_AsIs:
          break;
        case TK_IgnoreUnlessSpelledInSource:
          S = E->IgnoreUnlessSpelledInSource();
          break;
        }
      }

      getNodeDelegate().Visit(S);

      if (!S) {
        return;
      }

      ConstStmtVisitor<Derived>::Visit(S);

      // Some statements have custom mechanisms for dumping their children.
      if (isa<DeclStmt>(S) || isa<GenericSelectionExpr>(S) ||
          isa<RequiresExpr>(S))
        return;

      if (Traversal == TK_IgnoreUnlessSpelledInSource &&
          isa<LambdaExpr, CXXForRangeStmt, CallExpr,
              CXXRewrittenBinaryOperator>(S))
        return;

      for (const Stmt *SubStmt : S->children())
        Visit(SubStmt);
    });
  }

  void Visit(QualType T) {
    SplitQualType SQT = T.split();
    if (!SQT.Quals.hasQualifiers())
      return Visit(SQT.Ty);

    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(T);
      Visit(T.split().Ty);
    });
  }

  void Visit(const Type *T) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(T);
      if (!T)
        return;
      TypeVisitor<Derived>::Visit(T);

      QualType SingleStepDesugar =
          T->getLocallyUnqualifiedSingleStepDesugaredType();
      if (SingleStepDesugar != QualType(T, 0))
        Visit(SingleStepDesugar);
    });
  }

  void Visit(TypeLoc T) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(T);
      if (T.isNull())
        return;
      TypeLocVisitor<Derived>::Visit(T);
      if (auto Inner = T.getNextTypeLoc())
        Visit(Inner);
    });
  }

  void Visit(const Attr *A) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(A);
      ConstAttrVisitor<Derived>::Visit(A);
    });
  }

  void Visit(const CXXCtorInitializer *Init) {
    if (Traversal == TK_IgnoreUnlessSpelledInSource && !Init->isWritten())
      return;
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(Init);
      Visit(Init->getInit());
    });
  }

  void Visit(const TemplateArgument &A, SourceRange R = {},
             const Decl *From = nullptr, const char *Label = nullptr) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(A, R, From, Label);
      ConstTemplateArgumentVisitor<Derived>::Visit(A);
    });
  }

  void Visit(const BlockDecl::Capture &C) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(C);
      if (C.hasCopyExpr())
        Visit(C.getCopyExpr());
    });
  }

  void Visit(const OpenACCClause *C) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(C);
      for (const auto *S : C->children())
        Visit(S);
    });
  }

  void Visit(const OMPClause *C) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(C);
      for (const auto *S : C->children())
        Visit(S);
    });
  }

  void Visit(const GenericSelectionExpr::ConstAssociation &A) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(A);
      if (const TypeSourceInfo *TSI = A.getTypeSourceInfo())
        Visit(TSI->getType());
      Visit(A.getAssociationExpr());
    });
  }

  void Visit(const concepts::Requirement *R) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(R);
      if (!R)
        return;
      if (auto *TR = dyn_cast<concepts::TypeRequirement>(R)) {
        if (!TR->isSubstitutionFailure())
          Visit(TR->getType()->getType().getTypePtr());
      } else if (auto *ER = dyn_cast<concepts::ExprRequirement>(R)) {
        if (!ER->isExprSubstitutionFailure())
          Visit(ER->getExpr());
        if (!ER->getReturnTypeRequirement().isEmpty())
          Visit(ER->getReturnTypeRequirement()
                    .getTypeConstraint()
                    ->getImmediatelyDeclaredConstraint());
      } else if (auto *NR = dyn_cast<concepts::NestedRequirement>(R)) {
        if (!NR->hasInvalidConstraint())
          Visit(NR->getConstraintExpr());
      }
    });
  }

  void Visit(const ConceptReference *R) {
    getNodeDelegate().AddChild([=] { getNodeDelegate().Visit(R); });
  }

  void Visit(const APValue &Value, QualType Ty) {
    getNodeDelegate().AddChild([=] { getNodeDelegate().Visit(Value, Ty); });
  }

  void Visit(const comments::Comment *C, const comments::FullComment *FC) {
    getNodeDelegate().AddChild([=] {
      getNodeDelegate().Visit(C, FC);
      if (!C) {
        return;
      }
      comments::ConstCommentVisitor<Derived, void,
                                    const comments::FullComment *>::visit(C,
                                                                          FC);
      for (comments::Comment::child_iterator I = C->child_begin(),
                                             E = C->child_end();
           I != E; ++I)
        Visit(*I, FC);
    });
  }

  void Visit(const DynTypedNode &N) {
    // FIXME: Improve this with a switch or a visitor pattern.
    if (const auto *D = N.get<Decl>())
      Visit(D);
    else if (const auto *S = N.get<Stmt>())
      Visit(S);
    else if (const auto *QT = N.get<QualType>())
      Visit(*QT);
    else if (const auto *T = N.get<Type>())
      Visit(T);
    else if (const auto *TL = N.get<TypeLoc>())
      Visit(*TL);
    else if (const auto *C = N.get<CXXCtorInitializer>())
      Visit(C);
    else if (const auto *C = N.get<OMPClause>())
      Visit(C);
    else if (const auto *T = N.get<TemplateArgument>())
      Visit(*T);
    else if (const auto *CR = N.get<ConceptReference>())
      Visit(CR);
  }

  void dumpDeclContext(const DeclContext *DC) {
    if (!DC)
      return;

    for (const auto *D : (Deserialize ? DC->decls() : DC->noload_decls()))
      Visit(D);
  }

  void dumpTemplateParameters(const TemplateParameterList *TPL) {
    if (!TPL)
      return;

    for (const auto &TP : *TPL)
      Visit(TP);

    if (const Expr *RC = TPL->getRequiresClause())
      Visit(RC);
  }

  void
  dumpASTTemplateArgumentListInfo(const ASTTemplateArgumentListInfo *TALI) {
    if (!TALI)
      return;

    for (const auto &TA : TALI->arguments())
      dumpTemplateArgumentLoc(TA);
  }

  void dumpTemplateArgumentLoc(const TemplateArgumentLoc &A,
                               const Decl *From = nullptr,
                               const char *Label = nullptr) {
    Visit(A.getArgument(), A.getSourceRange(), From, Label);
  }

  void dumpTemplateArgumentList(const TemplateArgumentList &TAL) {
    for (unsigned i = 0, e = TAL.size(); i < e; ++i)
      Visit(TAL[i]);
  }

  void dumpObjCTypeParamList(const ObjCTypeParamList *typeParams) {
    if (!typeParams)
      return;

    for (const auto &typeParam : *typeParams) {
      Visit(typeParam);
    }
  }

  void VisitComplexType(const ComplexType *T) { Visit(T->getElementType()); }
  void VisitLocInfoType(const LocInfoType *T) {
    Visit(T->getTypeSourceInfo()->getTypeLoc());
  }
  void VisitPointerType(const PointerType *T) { Visit(T->getPointeeType()); }
  void VisitBlockPointerType(const BlockPointerType *T) {
    Visit(T->getPointeeType());
  }
  void VisitReferenceType(const ReferenceType *T) {
    Visit(T->getPointeeType());
  }
  void VisitMemberPointerType(const MemberPointerType *T) {
    Visit(T->getClass());
    Visit(T->getPointeeType());
  }
  void VisitArrayType(const ArrayType *T) { Visit(T->getElementType()); }
  void VisitVariableArrayType(const VariableArrayType *T) {
    VisitArrayType(T);
    Visit(T->getSizeExpr());
  }
  void VisitDependentSizedArrayType(const DependentSizedArrayType *T) {
    Visit(T->getElementType());
    Visit(T->getSizeExpr());
  }
  void VisitDependentSizedExtVectorType(const DependentSizedExtVectorType *T) {
    Visit(T->getElementType());
    Visit(T->getSizeExpr());
  }
  void VisitVectorType(const VectorType *T) { Visit(T->getElementType()); }
  void VisitFunctionType(const FunctionType *T) { Visit(T->getReturnType()); }
  void VisitFunctionProtoType(const FunctionProtoType *T) {
    VisitFunctionType(T);
    for (const QualType &PT : T->getParamTypes())
      Visit(PT);
  }
  void VisitTypeOfExprType(const TypeOfExprType *T) {
    Visit(T->getUnderlyingExpr());
  }
  void VisitDecltypeType(const DecltypeType *T) {
    Visit(T->getUnderlyingExpr());
  }

  void VisitPackIndexingType(const PackIndexingType *T) {
    Visit(T->getPattern());
    Visit(T->getIndexExpr());
  }

  void VisitUnaryTransformType(const UnaryTransformType *T) {
    Visit(T->getBaseType());
  }
  void VisitAttributedType(const AttributedType *T) {
    // FIXME: AttrKind
    if (T->getModifiedType() != T->getEquivalentType())
      Visit(T->getModifiedType());
  }
  void VisitBTFTagAttributedType(const BTFTagAttributedType *T) {
    Visit(T->getWrappedType());
  }
  void VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *) {}
  void
  VisitSubstTemplateTypeParmPackType(const SubstTemplateTypeParmPackType *T) {
    Visit(T->getArgumentPack());
  }
  void VisitTemplateSpecializationType(const TemplateSpecializationType *T) {
    for (const auto &Arg : T->template_arguments())
      Visit(Arg);
  }
  void VisitObjCObjectPointerType(const ObjCObjectPointerType *T) {
    Visit(T->getPointeeType());
  }
  void VisitAtomicType(const AtomicType *T) { Visit(T->getValueType()); }
  void VisitPipeType(const PipeType *T) { Visit(T->getElementType()); }
  void VisitAdjustedType(const AdjustedType *T) { Visit(T->getOriginalType()); }
  void VisitPackExpansionType(const PackExpansionType *T) {
    if (!T->isSugared())
      Visit(T->getPattern());
  }
  void VisitAutoType(const AutoType *T) {
    for (const auto &Arg : T->getTypeConstraintArguments())
      Visit(Arg);
  }
  // FIXME: ElaboratedType, DependentNameType,
  // DependentTemplateSpecializationType, ObjCObjectType

  // For TypeLocs, we automatically visit the inner type loc (pointee type etc).
  // We must explicitly visit other lexically-nested nodes.
  void VisitFunctionProtoTypeLoc(FunctionProtoTypeLoc TL) {
    TypeLocVisitor<Derived>::VisitFunctionTypeLoc(TL);
    for (const auto *Param : TL.getParams())
      Visit(Param, /*VisitTypeLocs=*/true);
  }
  void VisitAutoTypeLoc(AutoTypeLoc TL) {
    if (const auto *CR = TL.getConceptReference()) {
      if (auto *Args = CR->getTemplateArgsAsWritten())
        for (const auto &Arg : Args->arguments())
          dumpTemplateArgumentLoc(Arg);
    }
  }
  void VisitMemberPointerTypeLoc(MemberPointerTypeLoc TL) {
    Visit(TL.getClassTInfo()->getTypeLoc());
  }
  void VisitVariableArrayTypeLoc(VariableArrayTypeLoc TL) {
    Visit(TL.getSizeExpr());
  }
  void VisitDependentSizedArrayTypeLoc(DependentSizedArrayTypeLoc TL) {
    Visit(TL.getSizeExpr());
  }
  void VisitDependentSizedExtVectorTypeLoc(DependentSizedExtVectorTypeLoc TL) {
    Visit(cast<DependentSizedExtVectorType>(TL.getType())->getSizeExpr());
  }
  void VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
    Visit(TL.getUnderlyingExpr());
  }
  void VisitDecltypeType(DecltypeType TL) {
    Visit(TL.getUnderlyingExpr());
  }
  void VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
    for (unsigned I=0, N=TL.getNumArgs(); I < N; ++I)
      dumpTemplateArgumentLoc(TL.getArgLoc(I));
  }
  void VisitDependentTemplateSpecializationTypeLoc(
      DependentTemplateSpecializationTypeLoc TL) {
    for (unsigned I=0, N=TL.getNumArgs(); I < N; ++I)
      dumpTemplateArgumentLoc(TL.getArgLoc(I));
  }

  void VisitTypedefDecl(const TypedefDecl *D) { Visit(D->getUnderlyingType()); }

  void VisitEnumConstantDecl(const EnumConstantDecl *D) {
    if (const Expr *Init = D->getInitExpr())
      Visit(Init);
  }

  void VisitFunctionDecl(const FunctionDecl *D) {
    if (FunctionTemplateSpecializationInfo *FTSI =
            D->getTemplateSpecializationInfo())
      dumpTemplateArgumentList(*FTSI->TemplateArguments);
    else if (DependentFunctionTemplateSpecializationInfo *DFTSI =
                 D->getDependentSpecializationInfo())
      dumpASTTemplateArgumentListInfo(DFTSI->TemplateArgumentsAsWritten);

    if (D->param_begin())
      for (const auto *Parameter : D->parameters())
        Visit(Parameter);

    if (const Expr *TRC = D->getTrailingRequiresClause())
      Visit(TRC);

    if (Traversal == TK_IgnoreUnlessSpelledInSource && D->isDefaulted())
      return;

    if (const auto *C = dyn_cast<CXXConstructorDecl>(D))
      for (const auto *I : C->inits())
        Visit(I);

    if (D->doesThisDeclarationHaveABody())
      Visit(D->getBody());
  }

  void VisitFieldDecl(const FieldDecl *D) {
    if (D->isBitField())
      Visit(D->getBitWidth());
    if (Expr *Init = D->getInClassInitializer())
      Visit(Init);
  }

  void VisitVarDecl(const VarDecl *D) {
    if (Traversal == TK_IgnoreUnlessSpelledInSource && D->isCXXForRangeDecl())
      return;

    if (const auto *TSI = D->getTypeSourceInfo(); VisitLocs && TSI)
      Visit(TSI->getTypeLoc());
    if (D->hasInit())
      Visit(D->getInit());
  }

  void VisitDecompositionDecl(const DecompositionDecl *D) {
    VisitVarDecl(D);
    for (const auto *B : D->bindings())
      Visit(B);
  }

  void VisitBindingDecl(const BindingDecl *D) {
    if (Traversal == TK_IgnoreUnlessSpelledInSource)
      return;

    if (const auto *V = D->getHoldingVar())
      Visit(V);

    if (const auto *E = D->getBinding())
      Visit(E);
  }

  void VisitFileScopeAsmDecl(const FileScopeAsmDecl *D) {
    Visit(D->getAsmString());
  }

  void VisitTopLevelStmtDecl(const TopLevelStmtDecl *D) { Visit(D->getStmt()); }

  void VisitCapturedDecl(const CapturedDecl *D) { Visit(D->getBody()); }

  void VisitOMPThreadPrivateDecl(const OMPThreadPrivateDecl *D) {
    for (const auto *E : D->varlists())
      Visit(E);
  }

  void VisitOMPDeclareReductionDecl(const OMPDeclareReductionDecl *D) {
    Visit(D->getCombiner());
    if (const auto *Initializer = D->getInitializer())
      Visit(Initializer);
  }

  void VisitOMPDeclareMapperDecl(const OMPDeclareMapperDecl *D) {
    for (const auto *C : D->clauselists())
      Visit(C);
  }

  void VisitOMPCapturedExprDecl(const OMPCapturedExprDecl *D) {
    Visit(D->getInit());
  }

  void VisitOMPAllocateDecl(const OMPAllocateDecl *D) {
    for (const auto *E : D->varlists())
      Visit(E);
    for (const auto *C : D->clauselists())
      Visit(C);
  }

  template <typename SpecializationDecl>
  void dumpTemplateDeclSpecialization(const SpecializationDecl *D) {
    for (const auto *RedeclWithBadType : D->redecls()) {
      // FIXME: The redecls() range sometimes has elements of a less-specific
      // type. (In particular, ClassTemplateSpecializationDecl::redecls() gives
      // us TagDecls, and should give CXXRecordDecls).
      auto *Redecl = dyn_cast<SpecializationDecl>(RedeclWithBadType);
      if (!Redecl) {
        // Found the injected-class-name for a class template. This will be
        // dumped as part of its surrounding class so we don't need to dump it
        // here.
        assert(isa<CXXRecordDecl>(RedeclWithBadType) &&
               "expected an injected-class-name");
        continue;
      }
      Visit(Redecl);
    }
  }

  template <typename TemplateDecl>
  void dumpTemplateDecl(const TemplateDecl *D) {
    dumpTemplateParameters(D->getTemplateParameters());

    Visit(D->getTemplatedDecl());

    if (Traversal == TK_AsIs) {
      for (const auto *Child : D->specializations())
        dumpTemplateDeclSpecialization(Child);
    }
  }

  void VisitTypeAliasDecl(const TypeAliasDecl *D) {
    Visit(D->getUnderlyingType());
  }

  void VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D) {
    dumpTemplateParameters(D->getTemplateParameters());
    Visit(D->getTemplatedDecl());
  }

  void VisitStaticAssertDecl(const StaticAssertDecl *D) {
    Visit(D->getAssertExpr());
    Visit(D->getMessage());
  }

  void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
    dumpTemplateDecl(D);
  }

  void VisitClassTemplateDecl(const ClassTemplateDecl *D) {
    dumpTemplateDecl(D);
  }

  void VisitClassTemplateSpecializationDecl(
      const ClassTemplateSpecializationDecl *D) {
    dumpTemplateArgumentList(D->getTemplateArgs());
  }

  void VisitClassTemplatePartialSpecializationDecl(
      const ClassTemplatePartialSpecializationDecl *D) {
    VisitClassTemplateSpecializationDecl(D);
    dumpTemplateParameters(D->getTemplateParameters());
  }

  void VisitVarTemplateDecl(const VarTemplateDecl *D) { dumpTemplateDecl(D); }

  void VisitBuiltinTemplateDecl(const BuiltinTemplateDecl *D) {
    dumpTemplateParameters(D->getTemplateParameters());
  }

  void
  VisitVarTemplateSpecializationDecl(const VarTemplateSpecializationDecl *D) {
    dumpTemplateArgumentList(D->getTemplateArgs());
    VisitVarDecl(D);
  }

  void VisitVarTemplatePartialSpecializationDecl(
      const VarTemplatePartialSpecializationDecl *D) {
    dumpTemplateParameters(D->getTemplateParameters());
    VisitVarTemplateSpecializationDecl(D);
  }

  void VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D) {
    if (const auto *TC = D->getTypeConstraint())
      Visit(TC->getImmediatelyDeclaredConstraint());
    if (D->hasDefaultArgument())
      Visit(D->getDefaultArgument().getArgument(), SourceRange(),
            D->getDefaultArgStorage().getInheritedFrom(),
            D->defaultArgumentWasInherited() ? "inherited from" : "previous");
  }

  void VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D) {
    if (const auto *E = D->getPlaceholderTypeConstraint())
      Visit(E);
    if (D->hasDefaultArgument())
      dumpTemplateArgumentLoc(
          D->getDefaultArgument(), D->getDefaultArgStorage().getInheritedFrom(),
          D->defaultArgumentWasInherited() ? "inherited from" : "previous");
  }

  void VisitTemplateTemplateParmDecl(const TemplateTemplateParmDecl *D) {
    dumpTemplateParameters(D->getTemplateParameters());
    if (D->hasDefaultArgument())
      dumpTemplateArgumentLoc(
          D->getDefaultArgument(), D->getDefaultArgStorage().getInheritedFrom(),
          D->defaultArgumentWasInherited() ? "inherited from" : "previous");
  }

  void VisitConceptDecl(const ConceptDecl *D) {
    dumpTemplateParameters(D->getTemplateParameters());
    Visit(D->getConstraintExpr());
  }

  void VisitImplicitConceptSpecializationDecl(
      const ImplicitConceptSpecializationDecl *CSD) {
    for (const TemplateArgument &Arg : CSD->getTemplateArguments())
      Visit(Arg);
  }

  void VisitConceptSpecializationExpr(const ConceptSpecializationExpr *CSE) {
    Visit(CSE->getSpecializationDecl());
    if (CSE->hasExplicitTemplateArgs())
      for (const auto &ArgLoc : CSE->getTemplateArgsAsWritten()->arguments())
        dumpTemplateArgumentLoc(ArgLoc);
  }

  void VisitUsingShadowDecl(const UsingShadowDecl *D) {
    if (auto *TD = dyn_cast<TypeDecl>(D->getUnderlyingDecl()))
      Visit(TD->getTypeForDecl());
  }

  void VisitFriendDecl(const FriendDecl *D) {
    if (D->getFriendType()) {
      // Traverse any CXXRecordDecl owned by this type, since
      // it will not be in the parent context:
      if (auto *ET = D->getFriendType()->getType()->getAs<ElaboratedType>())
        if (auto *TD = ET->getOwnedTagDecl())
          Visit(TD);
    } else {
      Visit(D->getFriendDecl());
    }
  }

  void VisitObjCMethodDecl(const ObjCMethodDecl *D) {
    if (D->isThisDeclarationADefinition())
      dumpDeclContext(D);
    else
      for (const ParmVarDecl *Parameter : D->parameters())
        Visit(Parameter);

    if (D->hasBody())
      Visit(D->getBody());
  }

  void VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
    dumpObjCTypeParamList(D->getTypeParamList());
  }

  void VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
    dumpObjCTypeParamList(D->getTypeParamListAsWritten());
  }

  void VisitObjCImplementationDecl(const ObjCImplementationDecl *D) {
    for (const auto &I : D->inits())
      Visit(I);
  }

  void VisitBlockDecl(const BlockDecl *D) {
    for (const auto &I : D->parameters())
      Visit(I);

    for (const auto &I : D->captures())
      Visit(I);
    Visit(D->getBody());
  }

  void VisitDeclStmt(const DeclStmt *Node) {
    for (const auto &D : Node->decls())
      Visit(D);
  }

  void VisitAttributedStmt(const AttributedStmt *Node) {
    for (const auto *A : Node->getAttrs())
      Visit(A);
  }

  void VisitCXXCatchStmt(const CXXCatchStmt *Node) {
    Visit(Node->getExceptionDecl());
  }

  void VisitCapturedStmt(const CapturedStmt *Node) {
    Visit(Node->getCapturedDecl());
  }

  void VisitOMPExecutableDirective(const OMPExecutableDirective *Node) {
    for (const auto *C : Node->clauses())
      Visit(C);
  }

  void VisitOpenACCConstructStmt(const OpenACCConstructStmt *Node) {
    for (const auto *C : Node->clauses())
      Visit(C);
  }

  void VisitInitListExpr(const InitListExpr *ILE) {
    if (auto *Filler = ILE->getArrayFiller()) {
      Visit(Filler, "array_filler");
    }
  }

  void VisitCXXParenListInitExpr(const CXXParenListInitExpr *PLIE) {
    if (auto *Filler = PLIE->getArrayFiller()) {
      Visit(Filler, "array_filler");
    }
  }

  void VisitBlockExpr(const BlockExpr *Node) { Visit(Node->getBlockDecl()); }

  void VisitOpaqueValueExpr(const OpaqueValueExpr *Node) {
    if (Expr *Source = Node->getSourceExpr())
      Visit(Source);
  }

  void VisitGenericSelectionExpr(const GenericSelectionExpr *E) {
    if (E->isExprPredicate()) {
      Visit(E->getControllingExpr());
      Visit(E->getControllingExpr()->getType()); // FIXME: remove
    } else
      Visit(E->getControllingType()->getType());

    for (const auto Assoc : E->associations()) {
      Visit(Assoc);
    }
  }

  void VisitUnresolvedLookupExpr(const UnresolvedLookupExpr *E) {
    if (E->hasExplicitTemplateArgs())
      for (auto Arg : E->template_arguments())
        Visit(Arg.getArgument());
  }

  void VisitRequiresExpr(const RequiresExpr *E) {
    for (auto *D : E->getLocalParameters())
      Visit(D);
    for (auto *R : E->getRequirements())
      Visit(R);
  }

  void VisitTypeTraitExpr(const TypeTraitExpr *E) {
    // Argument types are not children of the TypeTraitExpr.
    for (auto *A : E->getArgs())
      Visit(A->getType());
  }

  void VisitLambdaExpr(const LambdaExpr *Node) {
    if (Traversal == TK_IgnoreUnlessSpelledInSource) {
      for (unsigned I = 0, N = Node->capture_size(); I != N; ++I) {
        const auto *C = Node->capture_begin() + I;
        if (!C->isExplicit())
          continue;
        if (Node->isInitCapture(C))
          Visit(C->getCapturedVar());
        else
          Visit(Node->capture_init_begin()[I]);
      }
      dumpTemplateParameters(Node->getTemplateParameterList());
      for (const auto *P : Node->getCallOperator()->parameters())
        Visit(P);
      Visit(Node->getBody());
    } else {
      return Visit(Node->getLambdaClass());
    }
  }

  void VisitSizeOfPackExpr(const SizeOfPackExpr *Node) {
    if (Node->isPartiallySubstituted())
      for (const auto &A : Node->getPartialArguments())
        Visit(A);
  }

  void VisitSubstNonTypeTemplateParmExpr(const SubstNonTypeTemplateParmExpr *E) {
    Visit(E->getParameter());
  }
  void VisitSubstNonTypeTemplateParmPackExpr(
      const SubstNonTypeTemplateParmPackExpr *E) {
    Visit(E->getParameterPack());
    Visit(E->getArgumentPack());
  }

  void VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node) {
    if (const VarDecl *CatchParam = Node->getCatchParamDecl())
      Visit(CatchParam);
  }

  void VisitCXXForRangeStmt(const CXXForRangeStmt *Node) {
    if (Traversal == TK_IgnoreUnlessSpelledInSource) {
      Visit(Node->getInit());
      Visit(Node->getLoopVariable());
      Visit(Node->getRangeInit());
      Visit(Node->getBody());
    }
  }

  void VisitCallExpr(const CallExpr *Node) {
    for (const auto *Child :
         make_filter_range(Node->children(), [this](const Stmt *Child) {
           if (Traversal != TK_IgnoreUnlessSpelledInSource)
             return false;
           return !isa<CXXDefaultArgExpr>(Child);
         })) {
      Visit(Child);
    }
  }

  void VisitCXXRewrittenBinaryOperator(const CXXRewrittenBinaryOperator *Node) {
    if (Traversal == TK_IgnoreUnlessSpelledInSource) {
      Visit(Node->getLHS());
      Visit(Node->getRHS());
    } else {
      ConstStmtVisitor<Derived>::VisitCXXRewrittenBinaryOperator(Node);
    }
  }

  void VisitExpressionTemplateArgument(const TemplateArgument &TA) {
    Visit(TA.getAsExpr());
  }

  void VisitTypeTemplateArgument(const TemplateArgument &TA) {
    Visit(TA.getAsType());
  }

  void VisitPackTemplateArgument(const TemplateArgument &TA) {
    for (const auto &TArg : TA.pack_elements())
      Visit(TArg);
  }

  void VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *Node) {
    Visit(Node->getExpr());
  }

  void VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *Node) {
    Visit(Node->getExpr());
  }

  // Implements Visit methods for Attrs.
#include "clang/AST/AttrNodeTraverse.inc"
};

} // namespace clang

#endif // LLVM_CLANG_AST_ASTNODETRAVERSER_H
