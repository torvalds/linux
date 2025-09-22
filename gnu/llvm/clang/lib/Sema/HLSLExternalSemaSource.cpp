//===--- HLSLExternalSemaSource.cpp - HLSL Sema Source --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/HLSLExternalSemaSource.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/HLSLRuntime.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "llvm/Frontend/HLSL/HLSLResource.h"

#include <functional>

using namespace clang;
using namespace llvm::hlsl;

namespace {

struct TemplateParameterListBuilder;

struct BuiltinTypeDeclBuilder {
  CXXRecordDecl *Record = nullptr;
  ClassTemplateDecl *Template = nullptr;
  ClassTemplateDecl *PrevTemplate = nullptr;
  NamespaceDecl *HLSLNamespace = nullptr;
  llvm::StringMap<FieldDecl *> Fields;

  BuiltinTypeDeclBuilder(CXXRecordDecl *R) : Record(R) {
    Record->startDefinition();
    Template = Record->getDescribedClassTemplate();
  }

  BuiltinTypeDeclBuilder(Sema &S, NamespaceDecl *Namespace, StringRef Name)
      : HLSLNamespace(Namespace) {
    ASTContext &AST = S.getASTContext();
    IdentifierInfo &II = AST.Idents.get(Name, tok::TokenKind::identifier);

    LookupResult Result(S, &II, SourceLocation(), Sema::LookupTagName);
    CXXRecordDecl *PrevDecl = nullptr;
    if (S.LookupQualifiedName(Result, HLSLNamespace)) {
      NamedDecl *Found = Result.getFoundDecl();
      if (auto *TD = dyn_cast<ClassTemplateDecl>(Found)) {
        PrevDecl = TD->getTemplatedDecl();
        PrevTemplate = TD;
      } else
        PrevDecl = dyn_cast<CXXRecordDecl>(Found);
      assert(PrevDecl && "Unexpected lookup result type.");
    }

    if (PrevDecl && PrevDecl->isCompleteDefinition()) {
      Record = PrevDecl;
      return;
    }

    Record = CXXRecordDecl::Create(AST, TagDecl::TagKind::Class, HLSLNamespace,
                                   SourceLocation(), SourceLocation(), &II,
                                   PrevDecl, true);
    Record->setImplicit(true);
    Record->setLexicalDeclContext(HLSLNamespace);
    Record->setHasExternalLexicalStorage();

    // Don't let anyone derive from built-in types.
    Record->addAttr(FinalAttr::CreateImplicit(AST, SourceRange(),
                                              FinalAttr::Keyword_final));
  }

  ~BuiltinTypeDeclBuilder() {
    if (HLSLNamespace && !Template && Record->getDeclContext() == HLSLNamespace)
      HLSLNamespace->addDecl(Record);
  }

  BuiltinTypeDeclBuilder &
  addMemberVariable(StringRef Name, QualType Type,
                    AccessSpecifier Access = AccessSpecifier::AS_private) {
    if (Record->isCompleteDefinition())
      return *this;
    assert(Record->isBeingDefined() &&
           "Definition must be started before adding members!");
    ASTContext &AST = Record->getASTContext();

    IdentifierInfo &II = AST.Idents.get(Name, tok::TokenKind::identifier);
    TypeSourceInfo *MemTySource =
        AST.getTrivialTypeSourceInfo(Type, SourceLocation());
    auto *Field = FieldDecl::Create(
        AST, Record, SourceLocation(), SourceLocation(), &II, Type, MemTySource,
        nullptr, false, InClassInitStyle::ICIS_NoInit);
    Field->setAccess(Access);
    Field->setImplicit(true);
    Record->addDecl(Field);
    Fields[Name] = Field;
    return *this;
  }

  BuiltinTypeDeclBuilder &
  addHandleMember(AccessSpecifier Access = AccessSpecifier::AS_private) {
    if (Record->isCompleteDefinition())
      return *this;
    QualType Ty = Record->getASTContext().VoidPtrTy;
    if (Template) {
      if (const auto *TTD = dyn_cast<TemplateTypeParmDecl>(
              Template->getTemplateParameters()->getParam(0)))
        Ty = Record->getASTContext().getPointerType(
            QualType(TTD->getTypeForDecl(), 0));
    }
    return addMemberVariable("h", Ty, Access);
  }

  BuiltinTypeDeclBuilder &annotateHLSLResource(ResourceClass RC,
                                               ResourceKind RK, bool IsROV) {
    if (Record->isCompleteDefinition())
      return *this;
    Record->addAttr(
        HLSLResourceClassAttr::CreateImplicit(Record->getASTContext(), RC));
    Record->addAttr(
        HLSLResourceAttr::CreateImplicit(Record->getASTContext(), RK, IsROV));
    return *this;
  }

  static DeclRefExpr *lookupBuiltinFunction(ASTContext &AST, Sema &S,
                                            StringRef Name) {
    IdentifierInfo &II = AST.Idents.get(Name, tok::TokenKind::identifier);
    DeclarationNameInfo NameInfo =
        DeclarationNameInfo(DeclarationName(&II), SourceLocation());
    LookupResult R(S, NameInfo, Sema::LookupOrdinaryName);
    // AllowBuiltinCreation is false but LookupDirect will create
    // the builtin when searching the global scope anyways...
    S.LookupName(R, S.getCurScope());
    // FIXME: If the builtin function was user-declared in global scope,
    // this assert *will* fail. Should this call LookupBuiltin instead?
    assert(R.isSingleResult() &&
           "Since this is a builtin it should always resolve!");
    auto *VD = cast<ValueDecl>(R.getFoundDecl());
    QualType Ty = VD->getType();
    return DeclRefExpr::Create(AST, NestedNameSpecifierLoc(), SourceLocation(),
                               VD, false, NameInfo, Ty, VK_PRValue);
  }

  static Expr *emitResourceClassExpr(ASTContext &AST, ResourceClass RC) {
    return IntegerLiteral::Create(
        AST,
        llvm::APInt(AST.getIntWidth(AST.UnsignedCharTy),
                    static_cast<uint8_t>(RC)),
        AST.UnsignedCharTy, SourceLocation());
  }

  BuiltinTypeDeclBuilder &addDefaultHandleConstructor(Sema &S,
                                                      ResourceClass RC) {
    if (Record->isCompleteDefinition())
      return *this;
    ASTContext &AST = Record->getASTContext();

    QualType ConstructorType =
        AST.getFunctionType(AST.VoidTy, {}, FunctionProtoType::ExtProtoInfo());

    CanQualType CanTy = Record->getTypeForDecl()->getCanonicalTypeUnqualified();
    DeclarationName Name = AST.DeclarationNames.getCXXConstructorName(CanTy);
    CXXConstructorDecl *Constructor = CXXConstructorDecl::Create(
        AST, Record, SourceLocation(),
        DeclarationNameInfo(Name, SourceLocation()), ConstructorType,
        AST.getTrivialTypeSourceInfo(ConstructorType, SourceLocation()),
        ExplicitSpecifier(), false, true, false,
        ConstexprSpecKind::Unspecified);

    DeclRefExpr *Fn =
        lookupBuiltinFunction(AST, S, "__builtin_hlsl_create_handle");
    Expr *RCExpr = emitResourceClassExpr(AST, RC);
    Expr *Call = CallExpr::Create(AST, Fn, {RCExpr}, AST.VoidPtrTy, VK_PRValue,
                                  SourceLocation(), FPOptionsOverride());

    CXXThisExpr *This = CXXThisExpr::Create(
        AST, SourceLocation(), Constructor->getFunctionObjectParameterType(),
        true);
    Expr *Handle = MemberExpr::CreateImplicit(AST, This, false, Fields["h"],
                                              Fields["h"]->getType(), VK_LValue,
                                              OK_Ordinary);

    // If the handle isn't a void pointer, cast the builtin result to the
    // correct type.
    if (Handle->getType().getCanonicalType() != AST.VoidPtrTy) {
      Call = CXXStaticCastExpr::Create(
          AST, Handle->getType(), VK_PRValue, CK_Dependent, Call, nullptr,
          AST.getTrivialTypeSourceInfo(Handle->getType(), SourceLocation()),
          FPOptionsOverride(), SourceLocation(), SourceLocation(),
          SourceRange());
    }

    BinaryOperator *Assign = BinaryOperator::Create(
        AST, Handle, Call, BO_Assign, Handle->getType(), VK_LValue, OK_Ordinary,
        SourceLocation(), FPOptionsOverride());

    Constructor->setBody(
        CompoundStmt::Create(AST, {Assign}, FPOptionsOverride(),
                             SourceLocation(), SourceLocation()));
    Constructor->setAccess(AccessSpecifier::AS_public);
    Record->addDecl(Constructor);
    return *this;
  }

  BuiltinTypeDeclBuilder &addArraySubscriptOperators() {
    if (Record->isCompleteDefinition())
      return *this;
    addArraySubscriptOperator(true);
    addArraySubscriptOperator(false);
    return *this;
  }

  BuiltinTypeDeclBuilder &addArraySubscriptOperator(bool IsConst) {
    if (Record->isCompleteDefinition())
      return *this;
    assert(Fields.count("h") > 0 &&
           "Subscript operator must be added after the handle.");

    FieldDecl *Handle = Fields["h"];
    ASTContext &AST = Record->getASTContext();

    assert(Handle->getType().getCanonicalType() != AST.VoidPtrTy &&
           "Not yet supported for void pointer handles.");

    QualType ElemTy =
        QualType(Handle->getType()->getPointeeOrArrayElementType(), 0);
    QualType ReturnTy = ElemTy;

    FunctionProtoType::ExtProtoInfo ExtInfo;

    // Subscript operators return references to elements, const makes the
    // reference and method const so that the underlying data is not mutable.
    ReturnTy = AST.getLValueReferenceType(ReturnTy);
    if (IsConst) {
      ExtInfo.TypeQuals.addConst();
      ReturnTy.addConst();
    }

    QualType MethodTy =
        AST.getFunctionType(ReturnTy, {AST.UnsignedIntTy}, ExtInfo);
    auto *TSInfo = AST.getTrivialTypeSourceInfo(MethodTy, SourceLocation());
    auto *MethodDecl = CXXMethodDecl::Create(
        AST, Record, SourceLocation(),
        DeclarationNameInfo(
            AST.DeclarationNames.getCXXOperatorName(OO_Subscript),
            SourceLocation()),
        MethodTy, TSInfo, SC_None, false, false, ConstexprSpecKind::Unspecified,
        SourceLocation());

    IdentifierInfo &II = AST.Idents.get("Idx", tok::TokenKind::identifier);
    auto *IdxParam = ParmVarDecl::Create(
        AST, MethodDecl->getDeclContext(), SourceLocation(), SourceLocation(),
        &II, AST.UnsignedIntTy,
        AST.getTrivialTypeSourceInfo(AST.UnsignedIntTy, SourceLocation()),
        SC_None, nullptr);
    MethodDecl->setParams({IdxParam});

    // Also add the parameter to the function prototype.
    auto FnProtoLoc = TSInfo->getTypeLoc().getAs<FunctionProtoTypeLoc>();
    FnProtoLoc.setParam(0, IdxParam);

    auto *This =
        CXXThisExpr::Create(AST, SourceLocation(),
                            MethodDecl->getFunctionObjectParameterType(), true);
    auto *HandleAccess = MemberExpr::CreateImplicit(
        AST, This, false, Handle, Handle->getType(), VK_LValue, OK_Ordinary);

    auto *IndexExpr = DeclRefExpr::Create(
        AST, NestedNameSpecifierLoc(), SourceLocation(), IdxParam, false,
        DeclarationNameInfo(IdxParam->getDeclName(), SourceLocation()),
        AST.UnsignedIntTy, VK_PRValue);

    auto *Array =
        new (AST) ArraySubscriptExpr(HandleAccess, IndexExpr, ElemTy, VK_LValue,
                                     OK_Ordinary, SourceLocation());

    auto *Return = ReturnStmt::Create(AST, SourceLocation(), Array, nullptr);

    MethodDecl->setBody(CompoundStmt::Create(AST, {Return}, FPOptionsOverride(),
                                             SourceLocation(),
                                             SourceLocation()));
    MethodDecl->setLexicalDeclContext(Record);
    MethodDecl->setAccess(AccessSpecifier::AS_public);
    MethodDecl->addAttr(AlwaysInlineAttr::CreateImplicit(
        AST, SourceRange(), AlwaysInlineAttr::CXX11_clang_always_inline));
    Record->addDecl(MethodDecl);

    return *this;
  }

  BuiltinTypeDeclBuilder &startDefinition() {
    if (Record->isCompleteDefinition())
      return *this;
    Record->startDefinition();
    return *this;
  }

  BuiltinTypeDeclBuilder &completeDefinition() {
    if (Record->isCompleteDefinition())
      return *this;
    assert(Record->isBeingDefined() &&
           "Definition must be started before completing it.");

    Record->completeDefinition();
    return *this;
  }

  TemplateParameterListBuilder addTemplateArgumentList(Sema &S);
  BuiltinTypeDeclBuilder &addSimpleTemplateParams(Sema &S,
                                                  ArrayRef<StringRef> Names);
};

struct TemplateParameterListBuilder {
  BuiltinTypeDeclBuilder &Builder;
  Sema &S;
  llvm::SmallVector<NamedDecl *> Params;

  TemplateParameterListBuilder(Sema &S, BuiltinTypeDeclBuilder &RB)
      : Builder(RB), S(S) {}

  ~TemplateParameterListBuilder() { finalizeTemplateArgs(); }

  TemplateParameterListBuilder &
  addTypeParameter(StringRef Name, QualType DefaultValue = QualType()) {
    if (Builder.Record->isCompleteDefinition())
      return *this;
    unsigned Position = static_cast<unsigned>(Params.size());
    auto *Decl = TemplateTypeParmDecl::Create(
        S.Context, Builder.Record->getDeclContext(), SourceLocation(),
        SourceLocation(), /* TemplateDepth */ 0, Position,
        &S.Context.Idents.get(Name, tok::TokenKind::identifier),
        /* Typename */ false,
        /* ParameterPack */ false);
    if (!DefaultValue.isNull())
      Decl->setDefaultArgument(
          S.Context, S.getTrivialTemplateArgumentLoc(DefaultValue, QualType(),
                                                     SourceLocation()));

    Params.emplace_back(Decl);
    return *this;
  }

  BuiltinTypeDeclBuilder &finalizeTemplateArgs() {
    if (Params.empty())
      return Builder;
    auto *ParamList = TemplateParameterList::Create(S.Context, SourceLocation(),
                                                    SourceLocation(), Params,
                                                    SourceLocation(), nullptr);
    Builder.Template = ClassTemplateDecl::Create(
        S.Context, Builder.Record->getDeclContext(), SourceLocation(),
        DeclarationName(Builder.Record->getIdentifier()), ParamList,
        Builder.Record);
    Builder.Record->setDescribedClassTemplate(Builder.Template);
    Builder.Template->setImplicit(true);
    Builder.Template->setLexicalDeclContext(Builder.Record->getDeclContext());
    // NOTE: setPreviousDecl before addDecl so new decl replace old decl when
    // make visible.
    Builder.Template->setPreviousDecl(Builder.PrevTemplate);
    Builder.Record->getDeclContext()->addDecl(Builder.Template);
    Params.clear();

    QualType T = Builder.Template->getInjectedClassNameSpecialization();
    T = S.Context.getInjectedClassNameType(Builder.Record, T);

    return Builder;
  }
};
} // namespace

TemplateParameterListBuilder
BuiltinTypeDeclBuilder::addTemplateArgumentList(Sema &S) {
  return TemplateParameterListBuilder(S, *this);
}

BuiltinTypeDeclBuilder &
BuiltinTypeDeclBuilder::addSimpleTemplateParams(Sema &S,
                                                ArrayRef<StringRef> Names) {
  TemplateParameterListBuilder Builder = this->addTemplateArgumentList(S);
  for (StringRef Name : Names)
    Builder.addTypeParameter(Name);
  return Builder.finalizeTemplateArgs();
}

HLSLExternalSemaSource::~HLSLExternalSemaSource() {}

void HLSLExternalSemaSource::InitializeSema(Sema &S) {
  SemaPtr = &S;
  ASTContext &AST = SemaPtr->getASTContext();
  // If the translation unit has external storage force external decls to load.
  if (AST.getTranslationUnitDecl()->hasExternalLexicalStorage())
    (void)AST.getTranslationUnitDecl()->decls_begin();

  IdentifierInfo &HLSL = AST.Idents.get("hlsl", tok::TokenKind::identifier);
  LookupResult Result(S, &HLSL, SourceLocation(), Sema::LookupNamespaceName);
  NamespaceDecl *PrevDecl = nullptr;
  if (S.LookupQualifiedName(Result, AST.getTranslationUnitDecl()))
    PrevDecl = Result.getAsSingle<NamespaceDecl>();
  HLSLNamespace = NamespaceDecl::Create(
      AST, AST.getTranslationUnitDecl(), /*Inline=*/false, SourceLocation(),
      SourceLocation(), &HLSL, PrevDecl, /*Nested=*/false);
  HLSLNamespace->setImplicit(true);
  HLSLNamespace->setHasExternalLexicalStorage();
  AST.getTranslationUnitDecl()->addDecl(HLSLNamespace);

  // Force external decls in the HLSL namespace to load from the PCH.
  (void)HLSLNamespace->getCanonicalDecl()->decls_begin();
  defineTrivialHLSLTypes();
  defineHLSLTypesWithForwardDeclarations();

  // This adds a `using namespace hlsl` directive. In DXC, we don't put HLSL's
  // built in types inside a namespace, but we are planning to change that in
  // the near future. In order to be source compatible older versions of HLSL
  // will need to implicitly use the hlsl namespace. For now in clang everything
  // will get added to the namespace, and we can remove the using directive for
  // future language versions to match HLSL's evolution.
  auto *UsingDecl = UsingDirectiveDecl::Create(
      AST, AST.getTranslationUnitDecl(), SourceLocation(), SourceLocation(),
      NestedNameSpecifierLoc(), SourceLocation(), HLSLNamespace,
      AST.getTranslationUnitDecl());

  AST.getTranslationUnitDecl()->addDecl(UsingDecl);
}

void HLSLExternalSemaSource::defineHLSLVectorAlias() {
  ASTContext &AST = SemaPtr->getASTContext();

  llvm::SmallVector<NamedDecl *> TemplateParams;

  auto *TypeParam = TemplateTypeParmDecl::Create(
      AST, HLSLNamespace, SourceLocation(), SourceLocation(), 0, 0,
      &AST.Idents.get("element", tok::TokenKind::identifier), false, false);
  TypeParam->setDefaultArgument(
      AST, SemaPtr->getTrivialTemplateArgumentLoc(
               TemplateArgument(AST.FloatTy), QualType(), SourceLocation()));

  TemplateParams.emplace_back(TypeParam);

  auto *SizeParam = NonTypeTemplateParmDecl::Create(
      AST, HLSLNamespace, SourceLocation(), SourceLocation(), 0, 1,
      &AST.Idents.get("element_count", tok::TokenKind::identifier), AST.IntTy,
      false, AST.getTrivialTypeSourceInfo(AST.IntTy));
  llvm::APInt Val(AST.getIntWidth(AST.IntTy), 4);
  TemplateArgument Default(AST, llvm::APSInt(std::move(Val)), AST.IntTy,
                           /*IsDefaulted=*/true);
  SizeParam->setDefaultArgument(
      AST, SemaPtr->getTrivialTemplateArgumentLoc(Default, AST.IntTy,
                                                  SourceLocation(), SizeParam));
  TemplateParams.emplace_back(SizeParam);

  auto *ParamList =
      TemplateParameterList::Create(AST, SourceLocation(), SourceLocation(),
                                    TemplateParams, SourceLocation(), nullptr);

  IdentifierInfo &II = AST.Idents.get("vector", tok::TokenKind::identifier);

  QualType AliasType = AST.getDependentSizedExtVectorType(
      AST.getTemplateTypeParmType(0, 0, false, TypeParam),
      DeclRefExpr::Create(
          AST, NestedNameSpecifierLoc(), SourceLocation(), SizeParam, false,
          DeclarationNameInfo(SizeParam->getDeclName(), SourceLocation()),
          AST.IntTy, VK_LValue),
      SourceLocation());

  auto *Record = TypeAliasDecl::Create(AST, HLSLNamespace, SourceLocation(),
                                       SourceLocation(), &II,
                                       AST.getTrivialTypeSourceInfo(AliasType));
  Record->setImplicit(true);

  auto *Template =
      TypeAliasTemplateDecl::Create(AST, HLSLNamespace, SourceLocation(),
                                    Record->getIdentifier(), ParamList, Record);

  Record->setDescribedAliasTemplate(Template);
  Template->setImplicit(true);
  Template->setLexicalDeclContext(Record->getDeclContext());
  HLSLNamespace->addDecl(Template);
}

void HLSLExternalSemaSource::defineTrivialHLSLTypes() {
  defineHLSLVectorAlias();
}

/// Set up common members and attributes for buffer types
static BuiltinTypeDeclBuilder setupBufferType(CXXRecordDecl *Decl, Sema &S,
                                              ResourceClass RC, ResourceKind RK,
                                              bool IsROV) {
  return BuiltinTypeDeclBuilder(Decl)
      .addHandleMember()
      .addDefaultHandleConstructor(S, RC)
      .annotateHLSLResource(RC, RK, IsROV);
}

void HLSLExternalSemaSource::defineHLSLTypesWithForwardDeclarations() {
  CXXRecordDecl *Decl;
  Decl = BuiltinTypeDeclBuilder(*SemaPtr, HLSLNamespace, "RWBuffer")
             .addSimpleTemplateParams(*SemaPtr, {"element_type"})
             .Record;
  onCompletion(Decl, [this](CXXRecordDecl *Decl) {
    setupBufferType(Decl, *SemaPtr, ResourceClass::UAV,
                    ResourceKind::TypedBuffer, /*IsROV=*/false)
        .addArraySubscriptOperators()
        .completeDefinition();
  });

  Decl =
      BuiltinTypeDeclBuilder(*SemaPtr, HLSLNamespace, "RasterizerOrderedBuffer")
          .addSimpleTemplateParams(*SemaPtr, {"element_type"})
          .Record;
  onCompletion(Decl, [this](CXXRecordDecl *Decl) {
    setupBufferType(Decl, *SemaPtr, ResourceClass::UAV,
                    ResourceKind::TypedBuffer, /*IsROV=*/true)
        .addArraySubscriptOperators()
        .completeDefinition();
  });
}

void HLSLExternalSemaSource::onCompletion(CXXRecordDecl *Record,
                                          CompletionFunction Fn) {
  Completions.insert(std::make_pair(Record->getCanonicalDecl(), Fn));
}

void HLSLExternalSemaSource::CompleteType(TagDecl *Tag) {
  if (!isa<CXXRecordDecl>(Tag))
    return;
  auto Record = cast<CXXRecordDecl>(Tag);

  // If this is a specialization, we need to get the underlying templated
  // declaration and complete that.
  if (auto TDecl = dyn_cast<ClassTemplateSpecializationDecl>(Record))
    Record = TDecl->getSpecializedTemplate()->getTemplatedDecl();
  Record = Record->getCanonicalDecl();
  auto It = Completions.find(Record);
  if (It == Completions.end())
    return;
  It->second(Record);
}
