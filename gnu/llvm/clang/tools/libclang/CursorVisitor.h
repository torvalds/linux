//===- CursorVisitor.h - CursorVisitor interface ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CURSORVISITOR_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CURSORVISITOR_H

#include "CXCursor.h"
#include "CXTranslationUnit.h"
#include "Index_Internal.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/TypeLocVisitor.h"
#include <optional>

namespace clang {
class PreprocessingRecord;
class ASTUnit;

namespace concepts {
class Requirement;
}

namespace cxcursor {

class VisitorJob {
public:
  enum Kind {
    DeclVisitKind,
    StmtVisitKind,
    MemberExprPartsKind,
    TypeLocVisitKind,
    OverloadExprPartsKind,
    DeclRefExprPartsKind,
    LabelRefVisitKind,
    ExplicitTemplateArgsVisitKind,
    NestedNameSpecifierLocVisitKind,
    DeclarationNameInfoVisitKind,
    MemberRefVisitKind,
    SizeOfPackExprPartsKind,
    LambdaExprPartsKind,
    ConceptSpecializationExprVisitKind,
    RequiresExprVisitKind,
    PostChildrenVisitKind
  };

protected:
  const void *data[3];
  CXCursor parent;
  Kind K;
  VisitorJob(CXCursor C, Kind k, const void *d1, const void *d2 = nullptr,
             const void *d3 = nullptr)
      : parent(C), K(k) {
    data[0] = d1;
    data[1] = d2;
    data[2] = d3;
  }

public:
  Kind getKind() const { return K; }
  const CXCursor &getParent() const { return parent; }
};

typedef SmallVector<VisitorJob, 10> VisitorWorkList;

// Cursor visitor.
class CursorVisitor : public DeclVisitor<CursorVisitor, bool>,
                      public TypeLocVisitor<CursorVisitor, bool> {
public:
  /// Callback called after child nodes of a cursor have been visited.
  /// Return true to break visitation or false to continue.
  typedef bool (*PostChildrenVisitorTy)(CXCursor cursor,
                                        CXClientData client_data);

private:
  /// The translation unit we are traversing.
  CXTranslationUnit TU;
  ASTUnit *AU;

  /// The parent cursor whose children we are traversing.
  CXCursor Parent;

  /// The declaration that serves at the parent of any statement or
  /// expression nodes.
  const Decl *StmtParent;

  /// The visitor function.
  CXCursorVisitor Visitor;

  PostChildrenVisitorTy PostChildrenVisitor;

  /// The opaque client data, to be passed along to the visitor.
  CXClientData ClientData;

  /// Whether we should visit the preprocessing record entries last,
  /// after visiting other declarations.
  bool VisitPreprocessorLast;

  /// Whether we should visit declarations or preprocessing record
  /// entries that are #included inside the \arg RegionOfInterest.
  bool VisitIncludedEntities;

  /// When valid, a source range to which the cursor should restrict
  /// its search.
  SourceRange RegionOfInterest;

  /// Whether we should only visit declarations and not preprocessing
  /// record entries.
  bool VisitDeclsOnly;

  // FIXME: Eventually remove.  This part of a hack to support proper
  // iteration over all Decls contained lexically within an ObjC container.
  DeclContext::decl_iterator *DI_current;
  DeclContext::decl_iterator DE_current;
  SmallVectorImpl<Decl *>::iterator *FileDI_current;
  SmallVectorImpl<Decl *>::iterator FileDE_current;

  // Cache of pre-allocated worklists for data-recursion walk of Stmts.
  SmallVector<VisitorWorkList *, 5> WorkListFreeList;
  SmallVector<VisitorWorkList *, 5> WorkListCache;

  using DeclVisitor<CursorVisitor, bool>::Visit;
  using TypeLocVisitor<CursorVisitor, bool>::Visit;

  /// Determine whether this particular source range comes before, comes
  /// after, or overlaps the region of interest.
  ///
  /// \param R a half-open source range retrieved from the abstract syntax tree.
  RangeComparisonResult CompareRegionOfInterest(SourceRange R);

  bool visitDeclsFromFileRegion(FileID File, unsigned Offset, unsigned Length);

  class SetParentRAII {
    CXCursor &Parent;
    const Decl *&StmtParent;
    CXCursor OldParent;

  public:
    SetParentRAII(CXCursor &Parent, const Decl *&StmtParent, CXCursor NewParent)
        : Parent(Parent), StmtParent(StmtParent), OldParent(Parent) {
      Parent = NewParent;
      if (clang_isDeclaration(Parent.kind))
        StmtParent = getCursorDecl(Parent);
    }

    ~SetParentRAII() {
      Parent = OldParent;
      if (clang_isDeclaration(Parent.kind))
        StmtParent = getCursorDecl(Parent);
    }
  };

public:
  CursorVisitor(CXTranslationUnit TU, CXCursorVisitor Visitor,
                CXClientData ClientData, bool VisitPreprocessorLast,
                bool VisitIncludedPreprocessingEntries = false,
                SourceRange RegionOfInterest = SourceRange(),
                bool VisitDeclsOnly = false,
                PostChildrenVisitorTy PostChildrenVisitor = nullptr)
      : TU(TU), AU(cxtu::getASTUnit(TU)), Visitor(Visitor),
        PostChildrenVisitor(PostChildrenVisitor), ClientData(ClientData),
        VisitPreprocessorLast(VisitPreprocessorLast),
        VisitIncludedEntities(VisitIncludedPreprocessingEntries),
        RegionOfInterest(RegionOfInterest), VisitDeclsOnly(VisitDeclsOnly),
        DI_current(nullptr), FileDI_current(nullptr) {
    Parent.kind = CXCursor_NoDeclFound;
    Parent.data[0] = nullptr;
    Parent.data[1] = nullptr;
    Parent.data[2] = nullptr;
    StmtParent = nullptr;
  }

  ~CursorVisitor() {
    // Free the pre-allocated worklists for data-recursion.
    for (SmallVectorImpl<VisitorWorkList *>::iterator I = WorkListCache.begin(),
                                                      E = WorkListCache.end();
         I != E; ++I) {
      delete *I;
    }
  }

  ASTUnit *getASTUnit() const { return AU; }
  CXTranslationUnit getTU() const { return TU; }

  bool Visit(CXCursor Cursor, bool CheckedRegionOfInterest = false);

  /// Visit declarations and preprocessed entities for the file region
  /// designated by \see RegionOfInterest.
  bool visitFileRegion();

  bool visitPreprocessedEntitiesInRegion();

  bool shouldVisitIncludedEntities() const { return VisitIncludedEntities; }

  template <typename InputIterator>
  bool visitPreprocessedEntities(InputIterator First, InputIterator Last,
                                 PreprocessingRecord &PPRec,
                                 FileID FID = FileID());

  bool VisitChildren(CXCursor Parent);

  // Declaration visitors
  bool VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D);
  bool VisitTypeAliasDecl(TypeAliasDecl *D);
  bool VisitAttributes(Decl *D);
  bool VisitBlockDecl(BlockDecl *B);
  bool VisitCXXRecordDecl(CXXRecordDecl *D);
  std::optional<bool> shouldVisitCursor(CXCursor C);
  bool VisitDeclContext(DeclContext *DC);
  bool VisitTranslationUnitDecl(TranslationUnitDecl *D);
  bool VisitTypedefDecl(TypedefDecl *D);
  bool VisitTagDecl(TagDecl *D);
  bool VisitClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl *D);
  bool VisitClassTemplatePartialSpecializationDecl(
      ClassTemplatePartialSpecializationDecl *D);
  bool VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D);
  bool VisitEnumConstantDecl(EnumConstantDecl *D);
  bool VisitDeclaratorDecl(DeclaratorDecl *DD);
  bool VisitFunctionDecl(FunctionDecl *ND);
  bool VisitFieldDecl(FieldDecl *D);
  bool VisitVarDecl(VarDecl *);
  bool VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D);
  bool VisitFunctionTemplateDecl(FunctionTemplateDecl *D);
  bool VisitClassTemplateDecl(ClassTemplateDecl *D);
  bool VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D);
  bool VisitObjCTypeParamDecl(ObjCTypeParamDecl *D);
  bool VisitObjCMethodDecl(ObjCMethodDecl *ND);
  bool VisitObjCContainerDecl(ObjCContainerDecl *D);
  bool VisitObjCCategoryDecl(ObjCCategoryDecl *ND);
  bool VisitObjCProtocolDecl(ObjCProtocolDecl *PID);
  bool VisitObjCPropertyDecl(ObjCPropertyDecl *PD);
  bool VisitObjCTypeParamList(ObjCTypeParamList *typeParamList);
  bool VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
  bool VisitObjCImplDecl(ObjCImplDecl *D);
  bool VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
  bool VisitObjCImplementationDecl(ObjCImplementationDecl *D);
  // FIXME: ObjCCompatibleAliasDecl requires aliased-class locations.
  bool VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *PD);
  bool VisitLinkageSpecDecl(LinkageSpecDecl *D);
  bool VisitNamespaceDecl(NamespaceDecl *D);
  bool VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
  bool VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
  bool VisitUsingDecl(UsingDecl *D);
  bool VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
  bool VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
  bool VisitStaticAssertDecl(StaticAssertDecl *D);
  bool VisitFriendDecl(FriendDecl *D);
  bool VisitDecompositionDecl(DecompositionDecl *D);
  bool VisitConceptDecl(ConceptDecl *D);
  bool VisitTypeConstraint(const TypeConstraint &TC);
  bool VisitConceptRequirement(const concepts::Requirement &R);

  // Name visitor
  bool VisitDeclarationNameInfo(DeclarationNameInfo Name);
  bool VisitNestedNameSpecifier(NestedNameSpecifier *NNS, SourceRange Range);
  bool VisitNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS);

  // Template visitors
  bool VisitTemplateParameters(const TemplateParameterList *Params);
  bool VisitTemplateName(TemplateName Name, SourceLocation Loc);
  bool VisitTemplateArgumentLoc(const TemplateArgumentLoc &TAL);

  // Type visitors
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) bool Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc);
#include "clang/AST/TypeLocNodes.def"

  bool VisitTagTypeLoc(TagTypeLoc TL);
  bool VisitArrayTypeLoc(ArrayTypeLoc TL);
  bool VisitFunctionTypeLoc(FunctionTypeLoc TL, bool SkipResultType = false);

  // Data-recursive visitor functions.
  bool IsInRegionOfInterest(CXCursor C);
  bool RunVisitorWorkList(VisitorWorkList &WL);
  void EnqueueWorkList(VisitorWorkList &WL, const Stmt *S);
  void EnqueueWorkList(VisitorWorkList &WL, const Attr *A);
  LLVM_ATTRIBUTE_NOINLINE bool Visit(const Stmt *S);
  LLVM_ATTRIBUTE_NOINLINE bool Visit(const Attr *A);

private:
  std::optional<bool> handleDeclForVisitation(const Decl *D);
};

} // namespace cxcursor
} // namespace clang

#endif
