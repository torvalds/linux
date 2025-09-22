//===- ASTImporter.cpp - Importing ASTs from other Contexts ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporter class which imports AST nodes from one
//  context into another context.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTImporter.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/ASTImporterSharedState.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeVisitor.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace clang {

  using llvm::make_error;
  using llvm::Error;
  using llvm::Expected;
  using ExpectedTypePtr = llvm::Expected<const Type *>;
  using ExpectedType = llvm::Expected<QualType>;
  using ExpectedStmt = llvm::Expected<Stmt *>;
  using ExpectedExpr = llvm::Expected<Expr *>;
  using ExpectedDecl = llvm::Expected<Decl *>;
  using ExpectedSLoc = llvm::Expected<SourceLocation>;
  using ExpectedName = llvm::Expected<DeclarationName>;

  std::string ASTImportError::toString() const {
    // FIXME: Improve error texts.
    switch (Error) {
    case NameConflict:
      return "NameConflict";
    case UnsupportedConstruct:
      return "UnsupportedConstruct";
    case Unknown:
      return "Unknown error";
    }
    llvm_unreachable("Invalid error code.");
    return "Invalid error code.";
  }

  void ASTImportError::log(raw_ostream &OS) const { OS << toString(); }

  std::error_code ASTImportError::convertToErrorCode() const {
    llvm_unreachable("Function not implemented.");
  }

  char ASTImportError::ID;

  template <class T>
  SmallVector<Decl *, 2>
  getCanonicalForwardRedeclChain(Redeclarable<T>* D) {
    SmallVector<Decl *, 2> Redecls;
    for (auto *R : D->getFirstDecl()->redecls()) {
      if (R != D->getFirstDecl())
        Redecls.push_back(R);
    }
    Redecls.push_back(D->getFirstDecl());
    std::reverse(Redecls.begin(), Redecls.end());
    return Redecls;
  }

  SmallVector<Decl*, 2> getCanonicalForwardRedeclChain(Decl* D) {
    if (auto *FD = dyn_cast<FunctionDecl>(D))
      return getCanonicalForwardRedeclChain<FunctionDecl>(FD);
    if (auto *VD = dyn_cast<VarDecl>(D))
      return getCanonicalForwardRedeclChain<VarDecl>(VD);
    if (auto *TD = dyn_cast<TagDecl>(D))
      return getCanonicalForwardRedeclChain<TagDecl>(TD);
    llvm_unreachable("Bad declaration kind");
  }

  void updateFlags(const Decl *From, Decl *To) {
    // Check if some flags or attrs are new in 'From' and copy into 'To'.
    // FIXME: Other flags or attrs?
    if (From->isUsed(false) && !To->isUsed(false))
      To->setIsUsed();
  }

  /// How to handle import errors that occur when import of a child declaration
  /// of a DeclContext fails.
  class ChildErrorHandlingStrategy {
    /// This context is imported (in the 'from' domain).
    /// It is nullptr if a non-DeclContext is imported.
    const DeclContext *const FromDC;
    /// Ignore import errors of the children.
    /// If true, the context can be imported successfully if a child
    /// of it failed to import. Otherwise the import errors of the child nodes
    /// are accumulated (joined) into the import error object of the parent.
    /// (Import of a parent can fail in other ways.)
    bool const IgnoreChildErrors;

  public:
    ChildErrorHandlingStrategy(const DeclContext *FromDC)
        : FromDC(FromDC), IgnoreChildErrors(!isa<TagDecl>(FromDC)) {}
    ChildErrorHandlingStrategy(const Decl *FromD)
        : FromDC(dyn_cast<DeclContext>(FromD)),
          IgnoreChildErrors(!isa<TagDecl>(FromD)) {}

    /// Process the import result of a child (of the current declaration).
    /// \param ResultErr The import error that can be used as result of
    /// importing the parent. This may be changed by the function.
    /// \param ChildErr Result of importing a child. Can be success or error.
    void handleChildImportResult(Error &ResultErr, Error &&ChildErr) {
      if (ChildErr && !IgnoreChildErrors)
        ResultErr = joinErrors(std::move(ResultErr), std::move(ChildErr));
      else
        consumeError(std::move(ChildErr));
    }

    /// Determine if import failure of a child does not cause import failure of
    /// its parent.
    bool ignoreChildErrorOnParent(Decl *FromChildD) const {
      if (!IgnoreChildErrors || !FromDC)
        return false;
      return FromDC->containsDecl(FromChildD);
    }
  };

  class ASTNodeImporter : public TypeVisitor<ASTNodeImporter, ExpectedType>,
                          public DeclVisitor<ASTNodeImporter, ExpectedDecl>,
                          public StmtVisitor<ASTNodeImporter, ExpectedStmt> {
    ASTImporter &Importer;

    // Use this instead of Importer.importInto .
    template <typename ImportT>
    [[nodiscard]] Error importInto(ImportT &To, const ImportT &From) {
      return Importer.importInto(To, From);
    }

    // Use this to import pointers of specific type.
    template <typename ImportT>
    [[nodiscard]] Error importInto(ImportT *&To, ImportT *From) {
      auto ToOrErr = Importer.Import(From);
      if (ToOrErr)
        To = cast_or_null<ImportT>(*ToOrErr);
      return ToOrErr.takeError();
    }

    // Call the import function of ASTImporter for a baseclass of type `T` and
    // cast the return value to `T`.
    template <typename T>
    auto import(T *From)
        -> std::conditional_t<std::is_base_of_v<Type, T>, Expected<const T *>,
                              Expected<T *>> {
      auto ToOrErr = Importer.Import(From);
      if (!ToOrErr)
        return ToOrErr.takeError();
      return cast_or_null<T>(*ToOrErr);
    }

    template <typename T>
    auto import(const T *From) {
      return import(const_cast<T *>(From));
    }

    // Call the import function of ASTImporter for type `T`.
    template <typename T>
    Expected<T> import(const T &From) {
      return Importer.Import(From);
    }

    // Import an std::optional<T> by importing the contained T, if any.
    template <typename T>
    Expected<std::optional<T>> import(std::optional<T> From) {
      if (!From)
        return std::nullopt;
      return import(*From);
    }

    ExplicitSpecifier importExplicitSpecifier(Error &Err,
                                              ExplicitSpecifier ESpec);

    // Wrapper for an overload set.
    template <typename ToDeclT> struct CallOverloadedCreateFun {
      template <typename... Args> decltype(auto) operator()(Args &&... args) {
        return ToDeclT::Create(std::forward<Args>(args)...);
      }
    };

    // Always use these functions to create a Decl during import. There are
    // certain tasks which must be done after the Decl was created, e.g. we
    // must immediately register that as an imported Decl.  The parameter `ToD`
    // will be set to the newly created Decl or if had been imported before
    // then to the already imported Decl.  Returns a bool value set to true if
    // the `FromD` had been imported before.
    template <typename ToDeclT, typename FromDeclT, typename... Args>
    [[nodiscard]] bool GetImportedOrCreateDecl(ToDeclT *&ToD, FromDeclT *FromD,
                                               Args &&...args) {
      // There may be several overloads of ToDeclT::Create. We must make sure
      // to call the one which would be chosen by the arguments, thus we use a
      // wrapper for the overload set.
      CallOverloadedCreateFun<ToDeclT> OC;
      return GetImportedOrCreateSpecialDecl(ToD, OC, FromD,
                                            std::forward<Args>(args)...);
    }
    // Use this overload if a special Type is needed to be created.  E.g if we
    // want to create a `TypeAliasDecl` and assign that to a `TypedefNameDecl`
    // then:
    // TypedefNameDecl *ToTypedef;
    // GetImportedOrCreateDecl<TypeAliasDecl>(ToTypedef, FromD, ...);
    template <typename NewDeclT, typename ToDeclT, typename FromDeclT,
              typename... Args>
    [[nodiscard]] bool GetImportedOrCreateDecl(ToDeclT *&ToD, FromDeclT *FromD,
                                               Args &&...args) {
      CallOverloadedCreateFun<NewDeclT> OC;
      return GetImportedOrCreateSpecialDecl(ToD, OC, FromD,
                                            std::forward<Args>(args)...);
    }
    // Use this version if a special create function must be
    // used, e.g. CXXRecordDecl::CreateLambda .
    template <typename ToDeclT, typename CreateFunT, typename FromDeclT,
              typename... Args>
    [[nodiscard]] bool
    GetImportedOrCreateSpecialDecl(ToDeclT *&ToD, CreateFunT CreateFun,
                                   FromDeclT *FromD, Args &&...args) {
      if (Importer.getImportDeclErrorIfAny(FromD)) {
        ToD = nullptr;
        return true; // Already imported but with error.
      }
      ToD = cast_or_null<ToDeclT>(Importer.GetAlreadyImportedOrNull(FromD));
      if (ToD)
        return true; // Already imported.
      ToD = CreateFun(std::forward<Args>(args)...);
      // Keep track of imported Decls.
      Importer.RegisterImportedDecl(FromD, ToD);
      Importer.SharedState->markAsNewDecl(ToD);
      InitializeImportedDecl(FromD, ToD);
      return false; // A new Decl is created.
    }

    void InitializeImportedDecl(Decl *FromD, Decl *ToD) {
      ToD->IdentifierNamespace = FromD->IdentifierNamespace;
      if (FromD->isUsed())
        ToD->setIsUsed();
      if (FromD->isImplicit())
        ToD->setImplicit();
    }

    // Check if we have found an existing definition.  Returns with that
    // definition if yes, otherwise returns null.
    Decl *FindAndMapDefinition(FunctionDecl *D, FunctionDecl *FoundFunction) {
      const FunctionDecl *Definition = nullptr;
      if (D->doesThisDeclarationHaveABody() &&
          FoundFunction->hasBody(Definition))
        return Importer.MapImported(D, const_cast<FunctionDecl *>(Definition));
      return nullptr;
    }

    void addDeclToContexts(Decl *FromD, Decl *ToD) {
      if (Importer.isMinimalImport()) {
        // In minimal import case the decl must be added even if it is not
        // contained in original context, for LLDB compatibility.
        // FIXME: Check if a better solution is possible.
        if (!FromD->getDescribedTemplate() &&
            FromD->getFriendObjectKind() == Decl::FOK_None)
          ToD->getLexicalDeclContext()->addDeclInternal(ToD);
        return;
      }

      DeclContext *FromDC = FromD->getDeclContext();
      DeclContext *FromLexicalDC = FromD->getLexicalDeclContext();
      DeclContext *ToDC = ToD->getDeclContext();
      DeclContext *ToLexicalDC = ToD->getLexicalDeclContext();

      bool Visible = false;
      if (FromDC->containsDeclAndLoad(FromD)) {
        ToDC->addDeclInternal(ToD);
        Visible = true;
      }
      if (ToDC != ToLexicalDC && FromLexicalDC->containsDeclAndLoad(FromD)) {
        ToLexicalDC->addDeclInternal(ToD);
        Visible = true;
      }

      // If the Decl was added to any context, it was made already visible.
      // Otherwise it is still possible that it should be visible.
      if (!Visible) {
        if (auto *FromNamed = dyn_cast<NamedDecl>(FromD)) {
          auto *ToNamed = cast<NamedDecl>(ToD);
          DeclContextLookupResult FromLookup =
              FromDC->lookup(FromNamed->getDeclName());
          if (llvm::is_contained(FromLookup, FromNamed))
            ToDC->makeDeclVisibleInContext(ToNamed);
        }
      }
    }

    void updateLookupTableForTemplateParameters(TemplateParameterList &Params,
                                                DeclContext *OldDC) {
      ASTImporterLookupTable *LT = Importer.SharedState->getLookupTable();
      if (!LT)
        return;

      for (NamedDecl *TP : Params)
        LT->update(TP, OldDC);
    }

    void updateLookupTableForTemplateParameters(TemplateParameterList &Params) {
      updateLookupTableForTemplateParameters(
          Params, Importer.getToContext().getTranslationUnitDecl());
    }

  public:
    explicit ASTNodeImporter(ASTImporter &Importer) : Importer(Importer) {}

    using TypeVisitor<ASTNodeImporter, ExpectedType>::Visit;
    using DeclVisitor<ASTNodeImporter, ExpectedDecl>::Visit;
    using StmtVisitor<ASTNodeImporter, ExpectedStmt>::Visit;

    // Importing types
    ExpectedType VisitType(const Type *T);
#define TYPE(Class, Base)                                                    \
    ExpectedType Visit##Class##Type(const Class##Type *T);
#include "clang/AST/TypeNodes.inc"

    // Importing declarations
    Error ImportDeclParts(NamedDecl *D, DeclarationName &Name, NamedDecl *&ToD,
                          SourceLocation &Loc);
    Error ImportDeclParts(
        NamedDecl *D, DeclContext *&DC, DeclContext *&LexicalDC,
        DeclarationName &Name, NamedDecl *&ToD, SourceLocation &Loc);
    Error ImportDefinitionIfNeeded(Decl *FromD, Decl *ToD = nullptr);
    Error ImportDeclarationNameLoc(
        const DeclarationNameInfo &From, DeclarationNameInfo &To);
    Error ImportDeclContext(DeclContext *FromDC, bool ForceImport = false);
    Error ImportDeclContext(
        Decl *From, DeclContext *&ToDC, DeclContext *&ToLexicalDC);
    Error ImportImplicitMethods(const CXXRecordDecl *From, CXXRecordDecl *To);

    Error ImportFieldDeclDefinition(const FieldDecl *From, const FieldDecl *To);
    Expected<CXXCastPath> ImportCastPath(CastExpr *E);
    Expected<APValue> ImportAPValue(const APValue &FromValue);

    using Designator = DesignatedInitExpr::Designator;

    /// What we should import from the definition.
    enum ImportDefinitionKind {
      /// Import the default subset of the definition, which might be
      /// nothing (if minimal import is set) or might be everything (if minimal
      /// import is not set).
      IDK_Default,
      /// Import everything.
      IDK_Everything,
      /// Import only the bare bones needed to establish a valid
      /// DeclContext.
      IDK_Basic
    };

    bool shouldForceImportDeclContext(ImportDefinitionKind IDK) {
      return IDK == IDK_Everything ||
             (IDK == IDK_Default && !Importer.isMinimalImport());
    }

    Error ImportInitializer(VarDecl *From, VarDecl *To);
    Error ImportDefinition(
        RecordDecl *From, RecordDecl *To,
        ImportDefinitionKind Kind = IDK_Default);
    Error ImportDefinition(
        EnumDecl *From, EnumDecl *To,
        ImportDefinitionKind Kind = IDK_Default);
    Error ImportDefinition(
        ObjCInterfaceDecl *From, ObjCInterfaceDecl *To,
        ImportDefinitionKind Kind = IDK_Default);
    Error ImportDefinition(
        ObjCProtocolDecl *From, ObjCProtocolDecl *To,
        ImportDefinitionKind Kind = IDK_Default);
    Error ImportTemplateArguments(ArrayRef<TemplateArgument> FromArgs,
                                  SmallVectorImpl<TemplateArgument> &ToArgs);
    Expected<TemplateArgument>
    ImportTemplateArgument(const TemplateArgument &From);

    template <typename InContainerTy>
    Error ImportTemplateArgumentListInfo(
        const InContainerTy &Container, TemplateArgumentListInfo &ToTAInfo);

    template<typename InContainerTy>
    Error ImportTemplateArgumentListInfo(
      SourceLocation FromLAngleLoc, SourceLocation FromRAngleLoc,
      const InContainerTy &Container, TemplateArgumentListInfo &Result);

    using TemplateArgsTy = SmallVector<TemplateArgument, 8>;
    using FunctionTemplateAndArgsTy =
        std::tuple<FunctionTemplateDecl *, TemplateArgsTy>;
    Expected<FunctionTemplateAndArgsTy>
    ImportFunctionTemplateWithTemplateArgsFromSpecialization(
        FunctionDecl *FromFD);

    template <typename DeclTy>
    Error ImportTemplateParameterLists(const DeclTy *FromD, DeclTy *ToD);

    Error ImportTemplateInformation(FunctionDecl *FromFD, FunctionDecl *ToFD);

    Error ImportFunctionDeclBody(FunctionDecl *FromFD, FunctionDecl *ToFD);

    Error ImportDefaultArgOfParmVarDecl(const ParmVarDecl *FromParam,
                                        ParmVarDecl *ToParam);

    Expected<InheritedConstructor>
    ImportInheritedConstructor(const InheritedConstructor &From);

    template <typename T>
    bool hasSameVisibilityContextAndLinkage(T *Found, T *From);

    bool IsStructuralMatch(Decl *From, Decl *To, bool Complain = true,
                           bool IgnoreTemplateParmDepth = false);
    ExpectedDecl VisitDecl(Decl *D);
    ExpectedDecl VisitImportDecl(ImportDecl *D);
    ExpectedDecl VisitEmptyDecl(EmptyDecl *D);
    ExpectedDecl VisitAccessSpecDecl(AccessSpecDecl *D);
    ExpectedDecl VisitStaticAssertDecl(StaticAssertDecl *D);
    ExpectedDecl VisitTranslationUnitDecl(TranslationUnitDecl *D);
    ExpectedDecl VisitBindingDecl(BindingDecl *D);
    ExpectedDecl VisitNamespaceDecl(NamespaceDecl *D);
    ExpectedDecl VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
    ExpectedDecl VisitTypedefNameDecl(TypedefNameDecl *D, bool IsAlias);
    ExpectedDecl VisitTypedefDecl(TypedefDecl *D);
    ExpectedDecl VisitTypeAliasDecl(TypeAliasDecl *D);
    ExpectedDecl VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D);
    ExpectedDecl VisitLabelDecl(LabelDecl *D);
    ExpectedDecl VisitEnumDecl(EnumDecl *D);
    ExpectedDecl VisitRecordDecl(RecordDecl *D);
    ExpectedDecl VisitEnumConstantDecl(EnumConstantDecl *D);
    ExpectedDecl VisitFunctionDecl(FunctionDecl *D);
    ExpectedDecl VisitCXXMethodDecl(CXXMethodDecl *D);
    ExpectedDecl VisitCXXConstructorDecl(CXXConstructorDecl *D);
    ExpectedDecl VisitCXXDestructorDecl(CXXDestructorDecl *D);
    ExpectedDecl VisitCXXConversionDecl(CXXConversionDecl *D);
    ExpectedDecl VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D);
    ExpectedDecl VisitFieldDecl(FieldDecl *D);
    ExpectedDecl VisitIndirectFieldDecl(IndirectFieldDecl *D);
    ExpectedDecl VisitFriendDecl(FriendDecl *D);
    ExpectedDecl VisitObjCIvarDecl(ObjCIvarDecl *D);
    ExpectedDecl VisitVarDecl(VarDecl *D);
    ExpectedDecl VisitImplicitParamDecl(ImplicitParamDecl *D);
    ExpectedDecl VisitParmVarDecl(ParmVarDecl *D);
    ExpectedDecl VisitObjCMethodDecl(ObjCMethodDecl *D);
    ExpectedDecl VisitObjCTypeParamDecl(ObjCTypeParamDecl *D);
    ExpectedDecl VisitObjCCategoryDecl(ObjCCategoryDecl *D);
    ExpectedDecl VisitObjCProtocolDecl(ObjCProtocolDecl *D);
    ExpectedDecl VisitLinkageSpecDecl(LinkageSpecDecl *D);
    ExpectedDecl VisitUsingDecl(UsingDecl *D);
    ExpectedDecl VisitUsingShadowDecl(UsingShadowDecl *D);
    ExpectedDecl VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
    ExpectedDecl VisitUsingPackDecl(UsingPackDecl *D);
    ExpectedDecl ImportUsingShadowDecls(BaseUsingDecl *D, BaseUsingDecl *ToSI);
    ExpectedDecl VisitUsingEnumDecl(UsingEnumDecl *D);
    ExpectedDecl VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
    ExpectedDecl VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
    ExpectedDecl VisitBuiltinTemplateDecl(BuiltinTemplateDecl *D);
    ExpectedDecl
    VisitLifetimeExtendedTemporaryDecl(LifetimeExtendedTemporaryDecl *D);

    Expected<ObjCTypeParamList *>
    ImportObjCTypeParamList(ObjCTypeParamList *list);

    ExpectedDecl VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
    ExpectedDecl VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
    ExpectedDecl VisitObjCImplementationDecl(ObjCImplementationDecl *D);
    ExpectedDecl VisitObjCPropertyDecl(ObjCPropertyDecl *D);
    ExpectedDecl VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
    ExpectedDecl VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D);
    ExpectedDecl VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D);
    ExpectedDecl VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D);
    ExpectedDecl VisitClassTemplateDecl(ClassTemplateDecl *D);
    ExpectedDecl VisitClassTemplateSpecializationDecl(
                                            ClassTemplateSpecializationDecl *D);
    ExpectedDecl VisitVarTemplateDecl(VarTemplateDecl *D);
    ExpectedDecl VisitVarTemplateSpecializationDecl(VarTemplateSpecializationDecl *D);
    ExpectedDecl VisitFunctionTemplateDecl(FunctionTemplateDecl *D);

    // Importing statements
    ExpectedStmt VisitStmt(Stmt *S);
    ExpectedStmt VisitGCCAsmStmt(GCCAsmStmt *S);
    ExpectedStmt VisitDeclStmt(DeclStmt *S);
    ExpectedStmt VisitNullStmt(NullStmt *S);
    ExpectedStmt VisitCompoundStmt(CompoundStmt *S);
    ExpectedStmt VisitCaseStmt(CaseStmt *S);
    ExpectedStmt VisitDefaultStmt(DefaultStmt *S);
    ExpectedStmt VisitLabelStmt(LabelStmt *S);
    ExpectedStmt VisitAttributedStmt(AttributedStmt *S);
    ExpectedStmt VisitIfStmt(IfStmt *S);
    ExpectedStmt VisitSwitchStmt(SwitchStmt *S);
    ExpectedStmt VisitWhileStmt(WhileStmt *S);
    ExpectedStmt VisitDoStmt(DoStmt *S);
    ExpectedStmt VisitForStmt(ForStmt *S);
    ExpectedStmt VisitGotoStmt(GotoStmt *S);
    ExpectedStmt VisitIndirectGotoStmt(IndirectGotoStmt *S);
    ExpectedStmt VisitContinueStmt(ContinueStmt *S);
    ExpectedStmt VisitBreakStmt(BreakStmt *S);
    ExpectedStmt VisitReturnStmt(ReturnStmt *S);
    // FIXME: MSAsmStmt
    // FIXME: SEHExceptStmt
    // FIXME: SEHFinallyStmt
    // FIXME: SEHTryStmt
    // FIXME: SEHLeaveStmt
    // FIXME: CapturedStmt
    ExpectedStmt VisitCXXCatchStmt(CXXCatchStmt *S);
    ExpectedStmt VisitCXXTryStmt(CXXTryStmt *S);
    ExpectedStmt VisitCXXForRangeStmt(CXXForRangeStmt *S);
    // FIXME: MSDependentExistsStmt
    ExpectedStmt VisitObjCForCollectionStmt(ObjCForCollectionStmt *S);
    ExpectedStmt VisitObjCAtCatchStmt(ObjCAtCatchStmt *S);
    ExpectedStmt VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *S);
    ExpectedStmt VisitObjCAtTryStmt(ObjCAtTryStmt *S);
    ExpectedStmt VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S);
    ExpectedStmt VisitObjCAtThrowStmt(ObjCAtThrowStmt *S);
    ExpectedStmt VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S);

    // Importing expressions
    ExpectedStmt VisitExpr(Expr *E);
    ExpectedStmt VisitSourceLocExpr(SourceLocExpr *E);
    ExpectedStmt VisitVAArgExpr(VAArgExpr *E);
    ExpectedStmt VisitChooseExpr(ChooseExpr *E);
    ExpectedStmt VisitConvertVectorExpr(ConvertVectorExpr *E);
    ExpectedStmt VisitShuffleVectorExpr(ShuffleVectorExpr *E);
    ExpectedStmt VisitGNUNullExpr(GNUNullExpr *E);
    ExpectedStmt VisitGenericSelectionExpr(GenericSelectionExpr *E);
    ExpectedStmt VisitPredefinedExpr(PredefinedExpr *E);
    ExpectedStmt VisitDeclRefExpr(DeclRefExpr *E);
    ExpectedStmt VisitImplicitValueInitExpr(ImplicitValueInitExpr *E);
    ExpectedStmt VisitDesignatedInitExpr(DesignatedInitExpr *E);
    ExpectedStmt VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E);
    ExpectedStmt VisitIntegerLiteral(IntegerLiteral *E);
    ExpectedStmt VisitFloatingLiteral(FloatingLiteral *E);
    ExpectedStmt VisitImaginaryLiteral(ImaginaryLiteral *E);
    ExpectedStmt VisitFixedPointLiteral(FixedPointLiteral *E);
    ExpectedStmt VisitCharacterLiteral(CharacterLiteral *E);
    ExpectedStmt VisitStringLiteral(StringLiteral *E);
    ExpectedStmt VisitCompoundLiteralExpr(CompoundLiteralExpr *E);
    ExpectedStmt VisitAtomicExpr(AtomicExpr *E);
    ExpectedStmt VisitAddrLabelExpr(AddrLabelExpr *E);
    ExpectedStmt VisitConstantExpr(ConstantExpr *E);
    ExpectedStmt VisitParenExpr(ParenExpr *E);
    ExpectedStmt VisitParenListExpr(ParenListExpr *E);
    ExpectedStmt VisitStmtExpr(StmtExpr *E);
    ExpectedStmt VisitUnaryOperator(UnaryOperator *E);
    ExpectedStmt VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E);
    ExpectedStmt VisitBinaryOperator(BinaryOperator *E);
    ExpectedStmt VisitConditionalOperator(ConditionalOperator *E);
    ExpectedStmt VisitBinaryConditionalOperator(BinaryConditionalOperator *E);
    ExpectedStmt VisitCXXRewrittenBinaryOperator(CXXRewrittenBinaryOperator *E);
    ExpectedStmt VisitOpaqueValueExpr(OpaqueValueExpr *E);
    ExpectedStmt VisitArrayTypeTraitExpr(ArrayTypeTraitExpr *E);
    ExpectedStmt VisitExpressionTraitExpr(ExpressionTraitExpr *E);
    ExpectedStmt VisitArraySubscriptExpr(ArraySubscriptExpr *E);
    ExpectedStmt VisitCompoundAssignOperator(CompoundAssignOperator *E);
    ExpectedStmt VisitImplicitCastExpr(ImplicitCastExpr *E);
    ExpectedStmt VisitExplicitCastExpr(ExplicitCastExpr *E);
    ExpectedStmt VisitOffsetOfExpr(OffsetOfExpr *OE);
    ExpectedStmt VisitCXXThrowExpr(CXXThrowExpr *E);
    ExpectedStmt VisitCXXNoexceptExpr(CXXNoexceptExpr *E);
    ExpectedStmt VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E);
    ExpectedStmt VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E);
    ExpectedStmt VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E);
    ExpectedStmt VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *E);
    ExpectedStmt VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E);
    ExpectedStmt VisitPackExpansionExpr(PackExpansionExpr *E);
    ExpectedStmt VisitSizeOfPackExpr(SizeOfPackExpr *E);
    ExpectedStmt VisitCXXNewExpr(CXXNewExpr *E);
    ExpectedStmt VisitCXXDeleteExpr(CXXDeleteExpr *E);
    ExpectedStmt VisitCXXConstructExpr(CXXConstructExpr *E);
    ExpectedStmt VisitCXXMemberCallExpr(CXXMemberCallExpr *E);
    ExpectedStmt VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr *E);
    ExpectedStmt VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E);
    ExpectedStmt VisitCXXUnresolvedConstructExpr(CXXUnresolvedConstructExpr *E);
    ExpectedStmt VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E);
    ExpectedStmt VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E);
    ExpectedStmt VisitExprWithCleanups(ExprWithCleanups *E);
    ExpectedStmt VisitCXXThisExpr(CXXThisExpr *E);
    ExpectedStmt VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E);
    ExpectedStmt VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E);
    ExpectedStmt VisitMemberExpr(MemberExpr *E);
    ExpectedStmt VisitCallExpr(CallExpr *E);
    ExpectedStmt VisitLambdaExpr(LambdaExpr *LE);
    ExpectedStmt VisitInitListExpr(InitListExpr *E);
    ExpectedStmt VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E);
    ExpectedStmt VisitCXXInheritedCtorInitExpr(CXXInheritedCtorInitExpr *E);
    ExpectedStmt VisitArrayInitLoopExpr(ArrayInitLoopExpr *E);
    ExpectedStmt VisitArrayInitIndexExpr(ArrayInitIndexExpr *E);
    ExpectedStmt VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E);
    ExpectedStmt VisitCXXNamedCastExpr(CXXNamedCastExpr *E);
    ExpectedStmt VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *E);
    ExpectedStmt VisitTypeTraitExpr(TypeTraitExpr *E);
    ExpectedStmt VisitCXXTypeidExpr(CXXTypeidExpr *E);
    ExpectedStmt VisitCXXFoldExpr(CXXFoldExpr *E);

    // Helper for chaining together multiple imports. If an error is detected,
    // subsequent imports will return default constructed nodes, so that failure
    // can be detected with a single conditional branch after a sequence of
    // imports.
    template <typename T> T importChecked(Error &Err, const T &From) {
      // Don't attempt to import nodes if we hit an error earlier.
      if (Err)
        return T{};
      Expected<T> MaybeVal = import(From);
      if (!MaybeVal) {
        Err = MaybeVal.takeError();
        return T{};
      }
      return *MaybeVal;
    }

    template<typename IIter, typename OIter>
    Error ImportArrayChecked(IIter Ibegin, IIter Iend, OIter Obegin) {
      using ItemT = std::remove_reference_t<decltype(*Obegin)>;
      for (; Ibegin != Iend; ++Ibegin, ++Obegin) {
        Expected<ItemT> ToOrErr = import(*Ibegin);
        if (!ToOrErr)
          return ToOrErr.takeError();
        *Obegin = *ToOrErr;
      }
      return Error::success();
    }

    // Import every item from a container structure into an output container.
    // If error occurs, stops at first error and returns the error.
    // The output container should have space for all needed elements (it is not
    // expanded, new items are put into from the beginning).
    template<typename InContainerTy, typename OutContainerTy>
    Error ImportContainerChecked(
        const InContainerTy &InContainer, OutContainerTy &OutContainer) {
      return ImportArrayChecked(
          InContainer.begin(), InContainer.end(), OutContainer.begin());
    }

    template<typename InContainerTy, typename OIter>
    Error ImportArrayChecked(const InContainerTy &InContainer, OIter Obegin) {
      return ImportArrayChecked(InContainer.begin(), InContainer.end(), Obegin);
    }

    Error ImportOverriddenMethods(CXXMethodDecl *ToMethod,
                                  CXXMethodDecl *FromMethod);

    Expected<FunctionDecl *> FindFunctionTemplateSpecialization(
        FunctionDecl *FromFD);

    // Returns true if the given function has a placeholder return type and
    // that type is declared inside the body of the function.
    // E.g. auto f() { struct X{}; return X(); }
    bool hasReturnTypeDeclaredInside(FunctionDecl *D);
  };

template <typename InContainerTy>
Error ASTNodeImporter::ImportTemplateArgumentListInfo(
    SourceLocation FromLAngleLoc, SourceLocation FromRAngleLoc,
    const InContainerTy &Container, TemplateArgumentListInfo &Result) {
  auto ToLAngleLocOrErr = import(FromLAngleLoc);
  if (!ToLAngleLocOrErr)
    return ToLAngleLocOrErr.takeError();
  auto ToRAngleLocOrErr = import(FromRAngleLoc);
  if (!ToRAngleLocOrErr)
    return ToRAngleLocOrErr.takeError();

  TemplateArgumentListInfo ToTAInfo(*ToLAngleLocOrErr, *ToRAngleLocOrErr);
  if (auto Err = ImportTemplateArgumentListInfo(Container, ToTAInfo))
    return Err;
  Result = ToTAInfo;
  return Error::success();
}

template <>
Error ASTNodeImporter::ImportTemplateArgumentListInfo<TemplateArgumentListInfo>(
    const TemplateArgumentListInfo &From, TemplateArgumentListInfo &Result) {
  return ImportTemplateArgumentListInfo(
      From.getLAngleLoc(), From.getRAngleLoc(), From.arguments(), Result);
}

template <>
Error ASTNodeImporter::ImportTemplateArgumentListInfo<
    ASTTemplateArgumentListInfo>(
        const ASTTemplateArgumentListInfo &From,
        TemplateArgumentListInfo &Result) {
  return ImportTemplateArgumentListInfo(
      From.LAngleLoc, From.RAngleLoc, From.arguments(), Result);
}

Expected<ASTNodeImporter::FunctionTemplateAndArgsTy>
ASTNodeImporter::ImportFunctionTemplateWithTemplateArgsFromSpecialization(
    FunctionDecl *FromFD) {
  assert(FromFD->getTemplatedKind() ==
      FunctionDecl::TK_FunctionTemplateSpecialization);

  FunctionTemplateAndArgsTy Result;

  auto *FTSInfo = FromFD->getTemplateSpecializationInfo();
  if (Error Err = importInto(std::get<0>(Result), FTSInfo->getTemplate()))
    return std::move(Err);

  // Import template arguments.
  if (Error Err = ImportTemplateArguments(FTSInfo->TemplateArguments->asArray(),
                                          std::get<1>(Result)))
    return std::move(Err);

  return Result;
}

template <>
Expected<TemplateParameterList *>
ASTNodeImporter::import(TemplateParameterList *From) {
  SmallVector<NamedDecl *, 4> To(From->size());
  if (Error Err = ImportContainerChecked(*From, To))
    return std::move(Err);

  ExpectedExpr ToRequiresClause = import(From->getRequiresClause());
  if (!ToRequiresClause)
    return ToRequiresClause.takeError();

  auto ToTemplateLocOrErr = import(From->getTemplateLoc());
  if (!ToTemplateLocOrErr)
    return ToTemplateLocOrErr.takeError();
  auto ToLAngleLocOrErr = import(From->getLAngleLoc());
  if (!ToLAngleLocOrErr)
    return ToLAngleLocOrErr.takeError();
  auto ToRAngleLocOrErr = import(From->getRAngleLoc());
  if (!ToRAngleLocOrErr)
    return ToRAngleLocOrErr.takeError();

  return TemplateParameterList::Create(
      Importer.getToContext(),
      *ToTemplateLocOrErr,
      *ToLAngleLocOrErr,
      To,
      *ToRAngleLocOrErr,
      *ToRequiresClause);
}

template <>
Expected<TemplateArgument>
ASTNodeImporter::import(const TemplateArgument &From) {
  switch (From.getKind()) {
  case TemplateArgument::Null:
    return TemplateArgument();

  case TemplateArgument::Type: {
    ExpectedType ToTypeOrErr = import(From.getAsType());
    if (!ToTypeOrErr)
      return ToTypeOrErr.takeError();
    return TemplateArgument(*ToTypeOrErr, /*isNullPtr*/ false,
                            From.getIsDefaulted());
  }

  case TemplateArgument::Integral: {
    ExpectedType ToTypeOrErr = import(From.getIntegralType());
    if (!ToTypeOrErr)
      return ToTypeOrErr.takeError();
    return TemplateArgument(From, *ToTypeOrErr);
  }

  case TemplateArgument::Declaration: {
    Expected<ValueDecl *> ToOrErr = import(From.getAsDecl());
    if (!ToOrErr)
      return ToOrErr.takeError();
    ExpectedType ToTypeOrErr = import(From.getParamTypeForDecl());
    if (!ToTypeOrErr)
      return ToTypeOrErr.takeError();
    return TemplateArgument(dyn_cast<ValueDecl>((*ToOrErr)->getCanonicalDecl()),
                            *ToTypeOrErr, From.getIsDefaulted());
  }

  case TemplateArgument::NullPtr: {
    ExpectedType ToTypeOrErr = import(From.getNullPtrType());
    if (!ToTypeOrErr)
      return ToTypeOrErr.takeError();
    return TemplateArgument(*ToTypeOrErr, /*isNullPtr*/ true,
                            From.getIsDefaulted());
  }

  case TemplateArgument::StructuralValue: {
    ExpectedType ToTypeOrErr = import(From.getStructuralValueType());
    if (!ToTypeOrErr)
      return ToTypeOrErr.takeError();
    Expected<APValue> ToValueOrErr = import(From.getAsStructuralValue());
    if (!ToValueOrErr)
      return ToValueOrErr.takeError();
    return TemplateArgument(Importer.getToContext(), *ToTypeOrErr,
                            *ToValueOrErr);
  }

  case TemplateArgument::Template: {
    Expected<TemplateName> ToTemplateOrErr = import(From.getAsTemplate());
    if (!ToTemplateOrErr)
      return ToTemplateOrErr.takeError();

    return TemplateArgument(*ToTemplateOrErr, From.getIsDefaulted());
  }

  case TemplateArgument::TemplateExpansion: {
    Expected<TemplateName> ToTemplateOrErr =
        import(From.getAsTemplateOrTemplatePattern());
    if (!ToTemplateOrErr)
      return ToTemplateOrErr.takeError();

    return TemplateArgument(*ToTemplateOrErr, From.getNumTemplateExpansions(),
                            From.getIsDefaulted());
  }

  case TemplateArgument::Expression:
    if (ExpectedExpr ToExpr = import(From.getAsExpr()))
      return TemplateArgument(*ToExpr, From.getIsDefaulted());
    else
      return ToExpr.takeError();

  case TemplateArgument::Pack: {
    SmallVector<TemplateArgument, 2> ToPack;
    ToPack.reserve(From.pack_size());
    if (Error Err = ImportTemplateArguments(From.pack_elements(), ToPack))
      return std::move(Err);

    return TemplateArgument(
        llvm::ArrayRef(ToPack).copy(Importer.getToContext()));
  }
  }

  llvm_unreachable("Invalid template argument kind");
}

template <>
Expected<TemplateArgumentLoc>
ASTNodeImporter::import(const TemplateArgumentLoc &TALoc) {
  Expected<TemplateArgument> ArgOrErr = import(TALoc.getArgument());
  if (!ArgOrErr)
    return ArgOrErr.takeError();
  TemplateArgument Arg = *ArgOrErr;

  TemplateArgumentLocInfo FromInfo = TALoc.getLocInfo();

  TemplateArgumentLocInfo ToInfo;
  if (Arg.getKind() == TemplateArgument::Expression) {
    ExpectedExpr E = import(FromInfo.getAsExpr());
    if (!E)
      return E.takeError();
    ToInfo = TemplateArgumentLocInfo(*E);
  } else if (Arg.getKind() == TemplateArgument::Type) {
    if (auto TSIOrErr = import(FromInfo.getAsTypeSourceInfo()))
      ToInfo = TemplateArgumentLocInfo(*TSIOrErr);
    else
      return TSIOrErr.takeError();
  } else {
    auto ToTemplateQualifierLocOrErr =
        import(FromInfo.getTemplateQualifierLoc());
    if (!ToTemplateQualifierLocOrErr)
      return ToTemplateQualifierLocOrErr.takeError();
    auto ToTemplateNameLocOrErr = import(FromInfo.getTemplateNameLoc());
    if (!ToTemplateNameLocOrErr)
      return ToTemplateNameLocOrErr.takeError();
    auto ToTemplateEllipsisLocOrErr =
        import(FromInfo.getTemplateEllipsisLoc());
    if (!ToTemplateEllipsisLocOrErr)
      return ToTemplateEllipsisLocOrErr.takeError();
    ToInfo = TemplateArgumentLocInfo(
        Importer.getToContext(), *ToTemplateQualifierLocOrErr,
        *ToTemplateNameLocOrErr, *ToTemplateEllipsisLocOrErr);
  }

  return TemplateArgumentLoc(Arg, ToInfo);
}

template <>
Expected<DeclGroupRef> ASTNodeImporter::import(const DeclGroupRef &DG) {
  if (DG.isNull())
    return DeclGroupRef::Create(Importer.getToContext(), nullptr, 0);
  size_t NumDecls = DG.end() - DG.begin();
  SmallVector<Decl *, 1> ToDecls;
  ToDecls.reserve(NumDecls);
  for (Decl *FromD : DG) {
    if (auto ToDOrErr = import(FromD))
      ToDecls.push_back(*ToDOrErr);
    else
      return ToDOrErr.takeError();
  }
  return DeclGroupRef::Create(Importer.getToContext(),
                              ToDecls.begin(),
                              NumDecls);
}

template <>
Expected<ASTNodeImporter::Designator>
ASTNodeImporter::import(const Designator &D) {
  if (D.isFieldDesignator()) {
    IdentifierInfo *ToFieldName = Importer.Import(D.getFieldName());

    ExpectedSLoc ToDotLocOrErr = import(D.getDotLoc());
    if (!ToDotLocOrErr)
      return ToDotLocOrErr.takeError();

    ExpectedSLoc ToFieldLocOrErr = import(D.getFieldLoc());
    if (!ToFieldLocOrErr)
      return ToFieldLocOrErr.takeError();

    return DesignatedInitExpr::Designator::CreateFieldDesignator(
        ToFieldName, *ToDotLocOrErr, *ToFieldLocOrErr);
  }

  ExpectedSLoc ToLBracketLocOrErr = import(D.getLBracketLoc());
  if (!ToLBracketLocOrErr)
    return ToLBracketLocOrErr.takeError();

  ExpectedSLoc ToRBracketLocOrErr = import(D.getRBracketLoc());
  if (!ToRBracketLocOrErr)
    return ToRBracketLocOrErr.takeError();

  if (D.isArrayDesignator())
    return Designator::CreateArrayDesignator(D.getArrayIndex(),
                                             *ToLBracketLocOrErr,
                                             *ToRBracketLocOrErr);

  ExpectedSLoc ToEllipsisLocOrErr = import(D.getEllipsisLoc());
  if (!ToEllipsisLocOrErr)
    return ToEllipsisLocOrErr.takeError();

  assert(D.isArrayRangeDesignator());
  return Designator::CreateArrayRangeDesignator(
      D.getArrayIndex(), *ToLBracketLocOrErr, *ToEllipsisLocOrErr,
      *ToRBracketLocOrErr);
}

template <>
Expected<ConceptReference *> ASTNodeImporter::import(ConceptReference *From) {
  Error Err = Error::success();
  auto ToNNS = importChecked(Err, From->getNestedNameSpecifierLoc());
  auto ToTemplateKWLoc = importChecked(Err, From->getTemplateKWLoc());
  auto ToConceptNameLoc =
      importChecked(Err, From->getConceptNameInfo().getLoc());
  auto ToConceptName = importChecked(Err, From->getConceptNameInfo().getName());
  auto ToFoundDecl = importChecked(Err, From->getFoundDecl());
  auto ToNamedConcept = importChecked(Err, From->getNamedConcept());
  if (Err)
    return std::move(Err);
  TemplateArgumentListInfo ToTAInfo;
  const auto *ASTTemplateArgs = From->getTemplateArgsAsWritten();
  if (ASTTemplateArgs)
    if (Error Err = ImportTemplateArgumentListInfo(*ASTTemplateArgs, ToTAInfo))
      return std::move(Err);
  auto *ConceptRef = ConceptReference::Create(
      Importer.getToContext(), ToNNS, ToTemplateKWLoc,
      DeclarationNameInfo(ToConceptName, ToConceptNameLoc), ToFoundDecl,
      ToNamedConcept,
      ASTTemplateArgs ? ASTTemplateArgumentListInfo::Create(
                            Importer.getToContext(), ToTAInfo)
                      : nullptr);
  return ConceptRef;
}

template <>
Expected<LambdaCapture> ASTNodeImporter::import(const LambdaCapture &From) {
  ValueDecl *Var = nullptr;
  if (From.capturesVariable()) {
    if (auto VarOrErr = import(From.getCapturedVar()))
      Var = *VarOrErr;
    else
      return VarOrErr.takeError();
  }

  auto LocationOrErr = import(From.getLocation());
  if (!LocationOrErr)
    return LocationOrErr.takeError();

  SourceLocation EllipsisLoc;
  if (From.isPackExpansion())
    if (Error Err = importInto(EllipsisLoc, From.getEllipsisLoc()))
      return std::move(Err);

  return LambdaCapture(
      *LocationOrErr, From.isImplicit(), From.getCaptureKind(), Var,
      EllipsisLoc);
}

template <typename T>
bool ASTNodeImporter::hasSameVisibilityContextAndLinkage(T *Found, T *From) {
  if (Found->getLinkageInternal() != From->getLinkageInternal())
    return false;

  if (From->hasExternalFormalLinkage())
    return Found->hasExternalFormalLinkage();
  if (Importer.GetFromTU(Found) != From->getTranslationUnitDecl())
    return false;
  if (From->isInAnonymousNamespace())
    return Found->isInAnonymousNamespace();
  else
    return !Found->isInAnonymousNamespace() &&
           !Found->hasExternalFormalLinkage();
}

template <>
bool ASTNodeImporter::hasSameVisibilityContextAndLinkage(TypedefNameDecl *Found,
                                               TypedefNameDecl *From) {
  if (Found->getLinkageInternal() != From->getLinkageInternal())
    return false;

  if (From->isInAnonymousNamespace() && Found->isInAnonymousNamespace())
    return Importer.GetFromTU(Found) == From->getTranslationUnitDecl();
  return From->isInAnonymousNamespace() == Found->isInAnonymousNamespace();
}

} // namespace clang

//----------------------------------------------------------------------------
// Import Types
//----------------------------------------------------------------------------

using namespace clang;

ExpectedType ASTNodeImporter::VisitType(const Type *T) {
  Importer.FromDiag(SourceLocation(), diag::err_unsupported_ast_node)
    << T->getTypeClassName();
  return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
}

ExpectedType ASTNodeImporter::VisitAtomicType(const AtomicType *T){
  ExpectedType UnderlyingTypeOrErr = import(T->getValueType());
  if (!UnderlyingTypeOrErr)
    return UnderlyingTypeOrErr.takeError();

  return Importer.getToContext().getAtomicType(*UnderlyingTypeOrErr);
}

ExpectedType ASTNodeImporter::VisitBuiltinType(const BuiltinType *T) {
  switch (T->getKind()) {
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  case BuiltinType::Id: \
    return Importer.getToContext().SingletonId;
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id: \
    return Importer.getToContext().Id##Ty;
#include "clang/Basic/OpenCLExtensionTypes.def"
#define SVE_TYPE(Name, Id, SingletonId) \
  case BuiltinType::Id: \
    return Importer.getToContext().SingletonId;
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
  case BuiltinType::Id: \
    return Importer.getToContext().Id##Ty;
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId)                                        \
  case BuiltinType::Id:                                                        \
    return Importer.getToContext().SingletonId;
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId)                                       \
  case BuiltinType::Id:                                                        \
    return Importer.getToContext().SingletonId;
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId)                                     \
  case BuiltinType::Id:                                                        \
    return Importer.getToContext().SingletonId;
#include "clang/Basic/AMDGPUTypes.def"
#define SHARED_SINGLETON_TYPE(Expansion)
#define BUILTIN_TYPE(Id, SingletonId) \
  case BuiltinType::Id: return Importer.getToContext().SingletonId;
#include "clang/AST/BuiltinTypes.def"

  // FIXME: for Char16, Char32, and NullPtr, make sure that the "to"
  // context supports C++.

  // FIXME: for ObjCId, ObjCClass, and ObjCSel, make sure that the "to"
  // context supports ObjC.

  case BuiltinType::Char_U:
    // The context we're importing from has an unsigned 'char'. If we're
    // importing into a context with a signed 'char', translate to
    // 'unsigned char' instead.
    if (Importer.getToContext().getLangOpts().CharIsSigned)
      return Importer.getToContext().UnsignedCharTy;

    return Importer.getToContext().CharTy;

  case BuiltinType::Char_S:
    // The context we're importing from has an unsigned 'char'. If we're
    // importing into a context with a signed 'char', translate to
    // 'unsigned char' instead.
    if (!Importer.getToContext().getLangOpts().CharIsSigned)
      return Importer.getToContext().SignedCharTy;

    return Importer.getToContext().CharTy;

  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    // FIXME: If not in C++, shall we translate to the C equivalent of
    // wchar_t?
    return Importer.getToContext().WCharTy;
  }

  llvm_unreachable("Invalid BuiltinType Kind!");
}

ExpectedType ASTNodeImporter::VisitDecayedType(const DecayedType *T) {
  ExpectedType ToOriginalTypeOrErr = import(T->getOriginalType());
  if (!ToOriginalTypeOrErr)
    return ToOriginalTypeOrErr.takeError();

  return Importer.getToContext().getDecayedType(*ToOriginalTypeOrErr);
}

ExpectedType ASTNodeImporter::VisitComplexType(const ComplexType *T) {
  ExpectedType ToElementTypeOrErr = import(T->getElementType());
  if (!ToElementTypeOrErr)
    return ToElementTypeOrErr.takeError();

  return Importer.getToContext().getComplexType(*ToElementTypeOrErr);
}

ExpectedType ASTNodeImporter::VisitPointerType(const PointerType *T) {
  ExpectedType ToPointeeTypeOrErr = import(T->getPointeeType());
  if (!ToPointeeTypeOrErr)
    return ToPointeeTypeOrErr.takeError();

  return Importer.getToContext().getPointerType(*ToPointeeTypeOrErr);
}

ExpectedType ASTNodeImporter::VisitBlockPointerType(const BlockPointerType *T) {
  // FIXME: Check for blocks support in "to" context.
  ExpectedType ToPointeeTypeOrErr = import(T->getPointeeType());
  if (!ToPointeeTypeOrErr)
    return ToPointeeTypeOrErr.takeError();

  return Importer.getToContext().getBlockPointerType(*ToPointeeTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitLValueReferenceType(const LValueReferenceType *T) {
  // FIXME: Check for C++ support in "to" context.
  ExpectedType ToPointeeTypeOrErr = import(T->getPointeeTypeAsWritten());
  if (!ToPointeeTypeOrErr)
    return ToPointeeTypeOrErr.takeError();

  return Importer.getToContext().getLValueReferenceType(*ToPointeeTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitRValueReferenceType(const RValueReferenceType *T) {
  // FIXME: Check for C++0x support in "to" context.
  ExpectedType ToPointeeTypeOrErr = import(T->getPointeeTypeAsWritten());
  if (!ToPointeeTypeOrErr)
    return ToPointeeTypeOrErr.takeError();

  return Importer.getToContext().getRValueReferenceType(*ToPointeeTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitMemberPointerType(const MemberPointerType *T) {
  // FIXME: Check for C++ support in "to" context.
  ExpectedType ToPointeeTypeOrErr = import(T->getPointeeType());
  if (!ToPointeeTypeOrErr)
    return ToPointeeTypeOrErr.takeError();

  ExpectedTypePtr ClassTypeOrErr = import(T->getClass());
  if (!ClassTypeOrErr)
    return ClassTypeOrErr.takeError();

  return Importer.getToContext().getMemberPointerType(*ToPointeeTypeOrErr,
                                                      *ClassTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitConstantArrayType(const ConstantArrayType *T) {
  Error Err = Error::success();
  auto ToElementType = importChecked(Err, T->getElementType());
  auto ToSizeExpr = importChecked(Err, T->getSizeExpr());
  if (Err)
    return std::move(Err);

  return Importer.getToContext().getConstantArrayType(
      ToElementType, T->getSize(), ToSizeExpr, T->getSizeModifier(),
      T->getIndexTypeCVRQualifiers());
}

ExpectedType
ASTNodeImporter::VisitArrayParameterType(const ArrayParameterType *T) {
  ExpectedType ToArrayTypeOrErr = VisitConstantArrayType(T);
  if (!ToArrayTypeOrErr)
    return ToArrayTypeOrErr.takeError();

  return Importer.getToContext().getArrayParameterType(*ToArrayTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitIncompleteArrayType(const IncompleteArrayType *T) {
  ExpectedType ToElementTypeOrErr = import(T->getElementType());
  if (!ToElementTypeOrErr)
    return ToElementTypeOrErr.takeError();

  return Importer.getToContext().getIncompleteArrayType(*ToElementTypeOrErr,
                                                        T->getSizeModifier(),
                                                T->getIndexTypeCVRQualifiers());
}

ExpectedType
ASTNodeImporter::VisitVariableArrayType(const VariableArrayType *T) {
  Error Err = Error::success();
  QualType ToElementType = importChecked(Err, T->getElementType());
  Expr *ToSizeExpr = importChecked(Err, T->getSizeExpr());
  SourceRange ToBracketsRange = importChecked(Err, T->getBracketsRange());
  if (Err)
    return std::move(Err);
  return Importer.getToContext().getVariableArrayType(
      ToElementType, ToSizeExpr, T->getSizeModifier(),
      T->getIndexTypeCVRQualifiers(), ToBracketsRange);
}

ExpectedType ASTNodeImporter::VisitDependentSizedArrayType(
    const DependentSizedArrayType *T) {
  Error Err = Error::success();
  QualType ToElementType = importChecked(Err, T->getElementType());
  Expr *ToSizeExpr = importChecked(Err, T->getSizeExpr());
  SourceRange ToBracketsRange = importChecked(Err, T->getBracketsRange());
  if (Err)
    return std::move(Err);
  // SizeExpr may be null if size is not specified directly.
  // For example, 'int a[]'.

  return Importer.getToContext().getDependentSizedArrayType(
      ToElementType, ToSizeExpr, T->getSizeModifier(),
      T->getIndexTypeCVRQualifiers(), ToBracketsRange);
}

ExpectedType ASTNodeImporter::VisitDependentSizedExtVectorType(
    const DependentSizedExtVectorType *T) {
  Error Err = Error::success();
  QualType ToElementType = importChecked(Err, T->getElementType());
  Expr *ToSizeExpr = importChecked(Err, T->getSizeExpr());
  SourceLocation ToAttrLoc = importChecked(Err, T->getAttributeLoc());
  if (Err)
    return std::move(Err);
  return Importer.getToContext().getDependentSizedExtVectorType(
      ToElementType, ToSizeExpr, ToAttrLoc);
}

ExpectedType ASTNodeImporter::VisitVectorType(const VectorType *T) {
  ExpectedType ToElementTypeOrErr = import(T->getElementType());
  if (!ToElementTypeOrErr)
    return ToElementTypeOrErr.takeError();

  return Importer.getToContext().getVectorType(*ToElementTypeOrErr,
                                               T->getNumElements(),
                                               T->getVectorKind());
}

ExpectedType ASTNodeImporter::VisitExtVectorType(const ExtVectorType *T) {
  ExpectedType ToElementTypeOrErr = import(T->getElementType());
  if (!ToElementTypeOrErr)
    return ToElementTypeOrErr.takeError();

  return Importer.getToContext().getExtVectorType(*ToElementTypeOrErr,
                                                  T->getNumElements());
}

ExpectedType
ASTNodeImporter::VisitFunctionNoProtoType(const FunctionNoProtoType *T) {
  // FIXME: What happens if we're importing a function without a prototype
  // into C++? Should we make it variadic?
  ExpectedType ToReturnTypeOrErr = import(T->getReturnType());
  if (!ToReturnTypeOrErr)
    return ToReturnTypeOrErr.takeError();

  return Importer.getToContext().getFunctionNoProtoType(*ToReturnTypeOrErr,
                                                        T->getExtInfo());
}

ExpectedType
ASTNodeImporter::VisitFunctionProtoType(const FunctionProtoType *T) {
  ExpectedType ToReturnTypeOrErr = import(T->getReturnType());
  if (!ToReturnTypeOrErr)
    return ToReturnTypeOrErr.takeError();

  // Import argument types
  SmallVector<QualType, 4> ArgTypes;
  for (const auto &A : T->param_types()) {
    ExpectedType TyOrErr = import(A);
    if (!TyOrErr)
      return TyOrErr.takeError();
    ArgTypes.push_back(*TyOrErr);
  }

  // Import exception types
  SmallVector<QualType, 4> ExceptionTypes;
  for (const auto &E : T->exceptions()) {
    ExpectedType TyOrErr = import(E);
    if (!TyOrErr)
      return TyOrErr.takeError();
    ExceptionTypes.push_back(*TyOrErr);
  }

  FunctionProtoType::ExtProtoInfo FromEPI = T->getExtProtoInfo();
  Error Err = Error::success();
  FunctionProtoType::ExtProtoInfo ToEPI;
  ToEPI.ExtInfo = FromEPI.ExtInfo;
  ToEPI.Variadic = FromEPI.Variadic;
  ToEPI.HasTrailingReturn = FromEPI.HasTrailingReturn;
  ToEPI.TypeQuals = FromEPI.TypeQuals;
  ToEPI.RefQualifier = FromEPI.RefQualifier;
  ToEPI.ExceptionSpec.Type = FromEPI.ExceptionSpec.Type;
  ToEPI.ExceptionSpec.NoexceptExpr =
      importChecked(Err, FromEPI.ExceptionSpec.NoexceptExpr);
  ToEPI.ExceptionSpec.SourceDecl =
      importChecked(Err, FromEPI.ExceptionSpec.SourceDecl);
  ToEPI.ExceptionSpec.SourceTemplate =
      importChecked(Err, FromEPI.ExceptionSpec.SourceTemplate);
  ToEPI.ExceptionSpec.Exceptions = ExceptionTypes;

  if (Err)
    return std::move(Err);

  return Importer.getToContext().getFunctionType(
      *ToReturnTypeOrErr, ArgTypes, ToEPI);
}

ExpectedType ASTNodeImporter::VisitUnresolvedUsingType(
    const UnresolvedUsingType *T) {
  Error Err = Error::success();
  auto ToD = importChecked(Err, T->getDecl());
  auto ToPrevD = importChecked(Err, T->getDecl()->getPreviousDecl());
  if (Err)
    return std::move(Err);

  return Importer.getToContext().getTypeDeclType(
      ToD, cast_or_null<TypeDecl>(ToPrevD));
}

ExpectedType ASTNodeImporter::VisitParenType(const ParenType *T) {
  ExpectedType ToInnerTypeOrErr = import(T->getInnerType());
  if (!ToInnerTypeOrErr)
    return ToInnerTypeOrErr.takeError();

  return Importer.getToContext().getParenType(*ToInnerTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitPackIndexingType(clang::PackIndexingType const *T) {

  ExpectedType Pattern = import(T->getPattern());
  if (!Pattern)
    return Pattern.takeError();
  ExpectedExpr Index = import(T->getIndexExpr());
  if (!Index)
    return Index.takeError();
  return Importer.getToContext().getPackIndexingType(*Pattern, *Index);
}

ExpectedType ASTNodeImporter::VisitTypedefType(const TypedefType *T) {
  Expected<TypedefNameDecl *> ToDeclOrErr = import(T->getDecl());
  if (!ToDeclOrErr)
    return ToDeclOrErr.takeError();

  TypedefNameDecl *ToDecl = *ToDeclOrErr;
  if (ToDecl->getTypeForDecl())
    return QualType(ToDecl->getTypeForDecl(), 0);

  ExpectedType ToUnderlyingTypeOrErr = import(T->desugar());
  if (!ToUnderlyingTypeOrErr)
    return ToUnderlyingTypeOrErr.takeError();

  return Importer.getToContext().getTypedefType(ToDecl, *ToUnderlyingTypeOrErr);
}

ExpectedType ASTNodeImporter::VisitTypeOfExprType(const TypeOfExprType *T) {
  ExpectedExpr ToExprOrErr = import(T->getUnderlyingExpr());
  if (!ToExprOrErr)
    return ToExprOrErr.takeError();
  return Importer.getToContext().getTypeOfExprType(*ToExprOrErr, T->getKind());
}

ExpectedType ASTNodeImporter::VisitTypeOfType(const TypeOfType *T) {
  ExpectedType ToUnderlyingTypeOrErr = import(T->getUnmodifiedType());
  if (!ToUnderlyingTypeOrErr)
    return ToUnderlyingTypeOrErr.takeError();
  return Importer.getToContext().getTypeOfType(*ToUnderlyingTypeOrErr,
                                               T->getKind());
}

ExpectedType ASTNodeImporter::VisitUsingType(const UsingType *T) {
  Expected<UsingShadowDecl *> FoundOrErr = import(T->getFoundDecl());
  if (!FoundOrErr)
    return FoundOrErr.takeError();
  Expected<QualType> UnderlyingOrErr = import(T->getUnderlyingType());
  if (!UnderlyingOrErr)
    return UnderlyingOrErr.takeError();

  return Importer.getToContext().getUsingType(*FoundOrErr, *UnderlyingOrErr);
}

ExpectedType ASTNodeImporter::VisitDecltypeType(const DecltypeType *T) {
  // FIXME: Make sure that the "to" context supports C++0x!
  ExpectedExpr ToExprOrErr = import(T->getUnderlyingExpr());
  if (!ToExprOrErr)
    return ToExprOrErr.takeError();

  ExpectedType ToUnderlyingTypeOrErr = import(T->getUnderlyingType());
  if (!ToUnderlyingTypeOrErr)
    return ToUnderlyingTypeOrErr.takeError();

  return Importer.getToContext().getDecltypeType(
      *ToExprOrErr, *ToUnderlyingTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitUnaryTransformType(const UnaryTransformType *T) {
  ExpectedType ToBaseTypeOrErr = import(T->getBaseType());
  if (!ToBaseTypeOrErr)
    return ToBaseTypeOrErr.takeError();

  ExpectedType ToUnderlyingTypeOrErr = import(T->getUnderlyingType());
  if (!ToUnderlyingTypeOrErr)
    return ToUnderlyingTypeOrErr.takeError();

  return Importer.getToContext().getUnaryTransformType(
      *ToBaseTypeOrErr, *ToUnderlyingTypeOrErr, T->getUTTKind());
}

ExpectedType ASTNodeImporter::VisitAutoType(const AutoType *T) {
  // FIXME: Make sure that the "to" context supports C++11!
  ExpectedType ToDeducedTypeOrErr = import(T->getDeducedType());
  if (!ToDeducedTypeOrErr)
    return ToDeducedTypeOrErr.takeError();

  ExpectedDecl ToTypeConstraintConcept = import(T->getTypeConstraintConcept());
  if (!ToTypeConstraintConcept)
    return ToTypeConstraintConcept.takeError();

  SmallVector<TemplateArgument, 2> ToTemplateArgs;
  if (Error Err = ImportTemplateArguments(T->getTypeConstraintArguments(),
                                          ToTemplateArgs))
    return std::move(Err);

  return Importer.getToContext().getAutoType(
      *ToDeducedTypeOrErr, T->getKeyword(), /*IsDependent*/false,
      /*IsPack=*/false, cast_or_null<ConceptDecl>(*ToTypeConstraintConcept),
      ToTemplateArgs);
}

ExpectedType ASTNodeImporter::VisitDeducedTemplateSpecializationType(
    const DeducedTemplateSpecializationType *T) {
  // FIXME: Make sure that the "to" context supports C++17!
  Expected<TemplateName> ToTemplateNameOrErr = import(T->getTemplateName());
  if (!ToTemplateNameOrErr)
    return ToTemplateNameOrErr.takeError();
  ExpectedType ToDeducedTypeOrErr = import(T->getDeducedType());
  if (!ToDeducedTypeOrErr)
    return ToDeducedTypeOrErr.takeError();

  return Importer.getToContext().getDeducedTemplateSpecializationType(
      *ToTemplateNameOrErr, *ToDeducedTypeOrErr, T->isDependentType());
}

ExpectedType ASTNodeImporter::VisitInjectedClassNameType(
    const InjectedClassNameType *T) {
  Expected<CXXRecordDecl *> ToDeclOrErr = import(T->getDecl());
  if (!ToDeclOrErr)
    return ToDeclOrErr.takeError();

  // The InjectedClassNameType is created in VisitRecordDecl when the
  // T->getDecl() is imported. Here we can return the existing type.
  const Type *Ty = (*ToDeclOrErr)->getTypeForDecl();
  assert(isa_and_nonnull<InjectedClassNameType>(Ty));
  return QualType(Ty, 0);
}

ExpectedType ASTNodeImporter::VisitRecordType(const RecordType *T) {
  Expected<RecordDecl *> ToDeclOrErr = import(T->getDecl());
  if (!ToDeclOrErr)
    return ToDeclOrErr.takeError();

  return Importer.getToContext().getTagDeclType(*ToDeclOrErr);
}

ExpectedType ASTNodeImporter::VisitEnumType(const EnumType *T) {
  Expected<EnumDecl *> ToDeclOrErr = import(T->getDecl());
  if (!ToDeclOrErr)
    return ToDeclOrErr.takeError();

  return Importer.getToContext().getTagDeclType(*ToDeclOrErr);
}

ExpectedType ASTNodeImporter::VisitAttributedType(const AttributedType *T) {
  ExpectedType ToModifiedTypeOrErr = import(T->getModifiedType());
  if (!ToModifiedTypeOrErr)
    return ToModifiedTypeOrErr.takeError();
  ExpectedType ToEquivalentTypeOrErr = import(T->getEquivalentType());
  if (!ToEquivalentTypeOrErr)
    return ToEquivalentTypeOrErr.takeError();

  return Importer.getToContext().getAttributedType(T->getAttrKind(),
      *ToModifiedTypeOrErr, *ToEquivalentTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitCountAttributedType(const CountAttributedType *T) {
  ExpectedType ToWrappedTypeOrErr = import(T->desugar());
  if (!ToWrappedTypeOrErr)
    return ToWrappedTypeOrErr.takeError();

  Error Err = Error::success();
  Expr *CountExpr = importChecked(Err, T->getCountExpr());

  SmallVector<TypeCoupledDeclRefInfo, 1> CoupledDecls;
  for (const TypeCoupledDeclRefInfo &TI : T->dependent_decls()) {
    Expected<ValueDecl *> ToDeclOrErr = import(TI.getDecl());
    if (!ToDeclOrErr)
      return ToDeclOrErr.takeError();
    CoupledDecls.emplace_back(*ToDeclOrErr, TI.isDeref());
  }

  return Importer.getToContext().getCountAttributedType(
      *ToWrappedTypeOrErr, CountExpr, T->isCountInBytes(), T->isOrNull(),
      ArrayRef(CoupledDecls.data(), CoupledDecls.size()));
}

ExpectedType ASTNodeImporter::VisitTemplateTypeParmType(
    const TemplateTypeParmType *T) {
  Expected<TemplateTypeParmDecl *> ToDeclOrErr = import(T->getDecl());
  if (!ToDeclOrErr)
    return ToDeclOrErr.takeError();

  return Importer.getToContext().getTemplateTypeParmType(
      T->getDepth(), T->getIndex(), T->isParameterPack(), *ToDeclOrErr);
}

ExpectedType ASTNodeImporter::VisitSubstTemplateTypeParmType(
    const SubstTemplateTypeParmType *T) {
  Expected<Decl *> ReplacedOrErr = import(T->getAssociatedDecl());
  if (!ReplacedOrErr)
    return ReplacedOrErr.takeError();

  ExpectedType ToReplacementTypeOrErr = import(T->getReplacementType());
  if (!ToReplacementTypeOrErr)
    return ToReplacementTypeOrErr.takeError();

  return Importer.getToContext().getSubstTemplateTypeParmType(
      *ToReplacementTypeOrErr, *ReplacedOrErr, T->getIndex(),
      T->getPackIndex());
}

ExpectedType ASTNodeImporter::VisitSubstTemplateTypeParmPackType(
    const SubstTemplateTypeParmPackType *T) {
  Expected<Decl *> ReplacedOrErr = import(T->getAssociatedDecl());
  if (!ReplacedOrErr)
    return ReplacedOrErr.takeError();

  Expected<TemplateArgument> ToArgumentPack = import(T->getArgumentPack());
  if (!ToArgumentPack)
    return ToArgumentPack.takeError();

  return Importer.getToContext().getSubstTemplateTypeParmPackType(
      *ReplacedOrErr, T->getIndex(), T->getFinal(), *ToArgumentPack);
}

ExpectedType ASTNodeImporter::VisitTemplateSpecializationType(
                                       const TemplateSpecializationType *T) {
  auto ToTemplateOrErr = import(T->getTemplateName());
  if (!ToTemplateOrErr)
    return ToTemplateOrErr.takeError();

  SmallVector<TemplateArgument, 2> ToTemplateArgs;
  if (Error Err =
          ImportTemplateArguments(T->template_arguments(), ToTemplateArgs))
    return std::move(Err);

  QualType ToCanonType;
  if (!T->isCanonicalUnqualified()) {
    QualType FromCanonType
      = Importer.getFromContext().getCanonicalType(QualType(T, 0));
    if (ExpectedType TyOrErr = import(FromCanonType))
      ToCanonType = *TyOrErr;
    else
      return TyOrErr.takeError();
  }
  return Importer.getToContext().getTemplateSpecializationType(*ToTemplateOrErr,
                                                               ToTemplateArgs,
                                                               ToCanonType);
}

ExpectedType ASTNodeImporter::VisitElaboratedType(const ElaboratedType *T) {
  // Note: the qualifier in an ElaboratedType is optional.
  auto ToQualifierOrErr = import(T->getQualifier());
  if (!ToQualifierOrErr)
    return ToQualifierOrErr.takeError();

  ExpectedType ToNamedTypeOrErr = import(T->getNamedType());
  if (!ToNamedTypeOrErr)
    return ToNamedTypeOrErr.takeError();

  Expected<TagDecl *> ToOwnedTagDeclOrErr = import(T->getOwnedTagDecl());
  if (!ToOwnedTagDeclOrErr)
    return ToOwnedTagDeclOrErr.takeError();

  return Importer.getToContext().getElaboratedType(T->getKeyword(),
                                                   *ToQualifierOrErr,
                                                   *ToNamedTypeOrErr,
                                                   *ToOwnedTagDeclOrErr);
}

ExpectedType
ASTNodeImporter::VisitPackExpansionType(const PackExpansionType *T) {
  ExpectedType ToPatternOrErr = import(T->getPattern());
  if (!ToPatternOrErr)
    return ToPatternOrErr.takeError();

  return Importer.getToContext().getPackExpansionType(*ToPatternOrErr,
                                                      T->getNumExpansions(),
                                                      /*ExpactPack=*/false);
}

ExpectedType ASTNodeImporter::VisitDependentTemplateSpecializationType(
    const DependentTemplateSpecializationType *T) {
  auto ToQualifierOrErr = import(T->getQualifier());
  if (!ToQualifierOrErr)
    return ToQualifierOrErr.takeError();

  IdentifierInfo *ToName = Importer.Import(T->getIdentifier());

  SmallVector<TemplateArgument, 2> ToPack;
  ToPack.reserve(T->template_arguments().size());
  if (Error Err = ImportTemplateArguments(T->template_arguments(), ToPack))
    return std::move(Err);

  return Importer.getToContext().getDependentTemplateSpecializationType(
      T->getKeyword(), *ToQualifierOrErr, ToName, ToPack);
}

ExpectedType
ASTNodeImporter::VisitDependentNameType(const DependentNameType *T) {
  auto ToQualifierOrErr = import(T->getQualifier());
  if (!ToQualifierOrErr)
    return ToQualifierOrErr.takeError();

  IdentifierInfo *Name = Importer.Import(T->getIdentifier());

  QualType Canon;
  if (T != T->getCanonicalTypeInternal().getTypePtr()) {
    if (ExpectedType TyOrErr = import(T->getCanonicalTypeInternal()))
      Canon = (*TyOrErr).getCanonicalType();
    else
      return TyOrErr.takeError();
  }

  return Importer.getToContext().getDependentNameType(T->getKeyword(),
                                                      *ToQualifierOrErr,
                                                      Name, Canon);
}

ExpectedType
ASTNodeImporter::VisitObjCInterfaceType(const ObjCInterfaceType *T) {
  Expected<ObjCInterfaceDecl *> ToDeclOrErr = import(T->getDecl());
  if (!ToDeclOrErr)
    return ToDeclOrErr.takeError();

  return Importer.getToContext().getObjCInterfaceType(*ToDeclOrErr);
}

ExpectedType ASTNodeImporter::VisitObjCObjectType(const ObjCObjectType *T) {
  ExpectedType ToBaseTypeOrErr = import(T->getBaseType());
  if (!ToBaseTypeOrErr)
    return ToBaseTypeOrErr.takeError();

  SmallVector<QualType, 4> TypeArgs;
  for (auto TypeArg : T->getTypeArgsAsWritten()) {
    if (ExpectedType TyOrErr = import(TypeArg))
      TypeArgs.push_back(*TyOrErr);
    else
      return TyOrErr.takeError();
  }

  SmallVector<ObjCProtocolDecl *, 4> Protocols;
  for (auto *P : T->quals()) {
    if (Expected<ObjCProtocolDecl *> ProtocolOrErr = import(P))
      Protocols.push_back(*ProtocolOrErr);
    else
      return ProtocolOrErr.takeError();

  }

  return Importer.getToContext().getObjCObjectType(*ToBaseTypeOrErr, TypeArgs,
                                                   Protocols,
                                                   T->isKindOfTypeAsWritten());
}

ExpectedType
ASTNodeImporter::VisitObjCObjectPointerType(const ObjCObjectPointerType *T) {
  ExpectedType ToPointeeTypeOrErr = import(T->getPointeeType());
  if (!ToPointeeTypeOrErr)
    return ToPointeeTypeOrErr.takeError();

  return Importer.getToContext().getObjCObjectPointerType(*ToPointeeTypeOrErr);
}

ExpectedType
ASTNodeImporter::VisitMacroQualifiedType(const MacroQualifiedType *T) {
  ExpectedType ToUnderlyingTypeOrErr = import(T->getUnderlyingType());
  if (!ToUnderlyingTypeOrErr)
    return ToUnderlyingTypeOrErr.takeError();

  IdentifierInfo *ToIdentifier = Importer.Import(T->getMacroIdentifier());
  return Importer.getToContext().getMacroQualifiedType(*ToUnderlyingTypeOrErr,
                                                       ToIdentifier);
}

ExpectedType clang::ASTNodeImporter::VisitAdjustedType(const AdjustedType *T) {
  Error Err = Error::success();
  QualType ToOriginalType = importChecked(Err, T->getOriginalType());
  QualType ToAdjustedType = importChecked(Err, T->getAdjustedType());
  if (Err)
    return std::move(Err);

  return Importer.getToContext().getAdjustedType(ToOriginalType,
                                                 ToAdjustedType);
}

ExpectedType clang::ASTNodeImporter::VisitBitIntType(const BitIntType *T) {
  return Importer.getToContext().getBitIntType(T->isUnsigned(),
                                               T->getNumBits());
}

ExpectedType clang::ASTNodeImporter::VisitBTFTagAttributedType(
    const clang::BTFTagAttributedType *T) {
  Error Err = Error::success();
  const BTFTypeTagAttr *ToBTFAttr = importChecked(Err, T->getAttr());
  QualType ToWrappedType = importChecked(Err, T->getWrappedType());
  if (Err)
    return std::move(Err);

  return Importer.getToContext().getBTFTagAttributedType(ToBTFAttr,
                                                         ToWrappedType);
}

ExpectedType clang::ASTNodeImporter::VisitConstantMatrixType(
    const clang::ConstantMatrixType *T) {
  ExpectedType ToElementTypeOrErr = import(T->getElementType());
  if (!ToElementTypeOrErr)
    return ToElementTypeOrErr.takeError();

  return Importer.getToContext().getConstantMatrixType(
      *ToElementTypeOrErr, T->getNumRows(), T->getNumColumns());
}

ExpectedType clang::ASTNodeImporter::VisitDependentAddressSpaceType(
    const clang::DependentAddressSpaceType *T) {
  Error Err = Error::success();
  QualType ToPointeeType = importChecked(Err, T->getPointeeType());
  Expr *ToAddrSpaceExpr = importChecked(Err, T->getAddrSpaceExpr());
  SourceLocation ToAttrLoc = importChecked(Err, T->getAttributeLoc());
  if (Err)
    return std::move(Err);

  return Importer.getToContext().getDependentAddressSpaceType(
      ToPointeeType, ToAddrSpaceExpr, ToAttrLoc);
}

ExpectedType clang::ASTNodeImporter::VisitDependentBitIntType(
    const clang::DependentBitIntType *T) {
  ExpectedExpr ToNumBitsExprOrErr = import(T->getNumBitsExpr());
  if (!ToNumBitsExprOrErr)
    return ToNumBitsExprOrErr.takeError();
  return Importer.getToContext().getDependentBitIntType(T->isUnsigned(),
                                                        *ToNumBitsExprOrErr);
}

ExpectedType clang::ASTNodeImporter::VisitDependentSizedMatrixType(
    const clang::DependentSizedMatrixType *T) {
  Error Err = Error::success();
  QualType ToElementType = importChecked(Err, T->getElementType());
  Expr *ToRowExpr = importChecked(Err, T->getRowExpr());
  Expr *ToColumnExpr = importChecked(Err, T->getColumnExpr());
  SourceLocation ToAttrLoc = importChecked(Err, T->getAttributeLoc());
  if (Err)
    return std::move(Err);

  return Importer.getToContext().getDependentSizedMatrixType(
      ToElementType, ToRowExpr, ToColumnExpr, ToAttrLoc);
}

ExpectedType clang::ASTNodeImporter::VisitDependentVectorType(
    const clang::DependentVectorType *T) {
  Error Err = Error::success();
  QualType ToElementType = importChecked(Err, T->getElementType());
  Expr *ToSizeExpr = importChecked(Err, T->getSizeExpr());
  SourceLocation ToAttrLoc = importChecked(Err, T->getAttributeLoc());
  if (Err)
    return std::move(Err);

  return Importer.getToContext().getDependentVectorType(
      ToElementType, ToSizeExpr, ToAttrLoc, T->getVectorKind());
}

ExpectedType clang::ASTNodeImporter::VisitObjCTypeParamType(
    const clang::ObjCTypeParamType *T) {
  Expected<ObjCTypeParamDecl *> ToDeclOrErr = import(T->getDecl());
  if (!ToDeclOrErr)
    return ToDeclOrErr.takeError();

  SmallVector<ObjCProtocolDecl *, 4> ToProtocols;
  for (ObjCProtocolDecl *FromProtocol : T->getProtocols()) {
    Expected<ObjCProtocolDecl *> ToProtocolOrErr = import(FromProtocol);
    if (!ToProtocolOrErr)
      return ToProtocolOrErr.takeError();
    ToProtocols.push_back(*ToProtocolOrErr);
  }

  return Importer.getToContext().getObjCTypeParamType(*ToDeclOrErr,
                                                      ToProtocols);
}

ExpectedType clang::ASTNodeImporter::VisitPipeType(const clang::PipeType *T) {
  ExpectedType ToElementTypeOrErr = import(T->getElementType());
  if (!ToElementTypeOrErr)
    return ToElementTypeOrErr.takeError();

  ASTContext &ToCtx = Importer.getToContext();
  if (T->isReadOnly())
    return ToCtx.getReadPipeType(*ToElementTypeOrErr);
  else
    return ToCtx.getWritePipeType(*ToElementTypeOrErr);
}

//----------------------------------------------------------------------------
// Import Declarations
//----------------------------------------------------------------------------
Error ASTNodeImporter::ImportDeclParts(
    NamedDecl *D, DeclContext *&DC, DeclContext *&LexicalDC,
    DeclarationName &Name, NamedDecl *&ToD, SourceLocation &Loc) {
  // Check if RecordDecl is in FunctionDecl parameters to avoid infinite loop.
  // example: int struct_in_proto(struct data_t{int a;int b;} *d);
  // FIXME: We could support these constructs by importing a different type of
  // this parameter and by importing the original type of the parameter only
  // after the FunctionDecl is created. See
  // VisitFunctionDecl::UsedDifferentProtoType.
  DeclContext *OrigDC = D->getDeclContext();
  FunctionDecl *FunDecl;
  if (isa<RecordDecl>(D) && (FunDecl = dyn_cast<FunctionDecl>(OrigDC)) &&
      FunDecl->hasBody()) {
    auto getLeafPointeeType = [](const Type *T) {
      while (T->isPointerType() || T->isArrayType()) {
        T = T->getPointeeOrArrayElementType();
      }
      return T;
    };
    for (const ParmVarDecl *P : FunDecl->parameters()) {
      const Type *LeafT =
          getLeafPointeeType(P->getType().getCanonicalType().getTypePtr());
      auto *RT = dyn_cast<RecordType>(LeafT);
      if (RT && RT->getDecl() == D) {
        Importer.FromDiag(D->getLocation(), diag::err_unsupported_ast_node)
            << D->getDeclKindName();
        return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
      }
    }
  }

  // Import the context of this declaration.
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return Err;

  // Import the name of this declaration.
  if (Error Err = importInto(Name, D->getDeclName()))
    return Err;

  // Import the location of this declaration.
  if (Error Err = importInto(Loc, D->getLocation()))
    return Err;

  ToD = cast_or_null<NamedDecl>(Importer.GetAlreadyImportedOrNull(D));
  if (ToD)
    if (Error Err = ASTNodeImporter(*this).ImportDefinitionIfNeeded(D, ToD))
      return Err;

  return Error::success();
}

Error ASTNodeImporter::ImportDeclParts(NamedDecl *D, DeclarationName &Name,
                                       NamedDecl *&ToD, SourceLocation &Loc) {

  // Import the name of this declaration.
  if (Error Err = importInto(Name, D->getDeclName()))
    return Err;

  // Import the location of this declaration.
  if (Error Err = importInto(Loc, D->getLocation()))
    return Err;

  ToD = cast_or_null<NamedDecl>(Importer.GetAlreadyImportedOrNull(D));
  if (ToD)
    if (Error Err = ASTNodeImporter(*this).ImportDefinitionIfNeeded(D, ToD))
      return Err;

  return Error::success();
}

Error ASTNodeImporter::ImportDefinitionIfNeeded(Decl *FromD, Decl *ToD) {
  if (!FromD)
    return Error::success();

  if (!ToD)
    if (Error Err = importInto(ToD, FromD))
      return Err;

  if (RecordDecl *FromRecord = dyn_cast<RecordDecl>(FromD)) {
    if (RecordDecl *ToRecord = cast<RecordDecl>(ToD)) {
      if (FromRecord->getDefinition() && FromRecord->isCompleteDefinition() &&
          !ToRecord->getDefinition()) {
        if (Error Err = ImportDefinition(FromRecord, ToRecord))
          return Err;
      }
    }
    return Error::success();
  }

  if (EnumDecl *FromEnum = dyn_cast<EnumDecl>(FromD)) {
    if (EnumDecl *ToEnum = cast<EnumDecl>(ToD)) {
      if (FromEnum->getDefinition() && !ToEnum->getDefinition()) {
        if (Error Err = ImportDefinition(FromEnum, ToEnum))
          return Err;
      }
    }
    return Error::success();
  }

  return Error::success();
}

Error
ASTNodeImporter::ImportDeclarationNameLoc(
    const DeclarationNameInfo &From, DeclarationNameInfo& To) {
  // NOTE: To.Name and To.Loc are already imported.
  // We only have to import To.LocInfo.
  switch (To.getName().getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    return Error::success();

  case DeclarationName::CXXOperatorName: {
    if (auto ToRangeOrErr = import(From.getCXXOperatorNameRange()))
      To.setCXXOperatorNameRange(*ToRangeOrErr);
    else
      return ToRangeOrErr.takeError();
    return Error::success();
  }
  case DeclarationName::CXXLiteralOperatorName: {
    if (ExpectedSLoc LocOrErr = import(From.getCXXLiteralOperatorNameLoc()))
      To.setCXXLiteralOperatorNameLoc(*LocOrErr);
    else
      return LocOrErr.takeError();
    return Error::success();
  }
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName: {
    if (auto ToTInfoOrErr = import(From.getNamedTypeInfo()))
      To.setNamedTypeInfo(*ToTInfoOrErr);
    else
      return ToTInfoOrErr.takeError();
    return Error::success();
  }
  }
  llvm_unreachable("Unknown name kind.");
}

Error
ASTNodeImporter::ImportDeclContext(DeclContext *FromDC, bool ForceImport) {
  if (Importer.isMinimalImport() && !ForceImport) {
    auto ToDCOrErr = Importer.ImportContext(FromDC);
    return ToDCOrErr.takeError();
  }

  // We use strict error handling in case of records and enums, but not
  // with e.g. namespaces.
  //
  // FIXME Clients of the ASTImporter should be able to choose an
  // appropriate error handling strategy for their needs.  For instance,
  // they may not want to mark an entire namespace as erroneous merely
  // because there is an ODR error with two typedefs.  As another example,
  // the client may allow EnumConstantDecls with same names but with
  // different values in two distinct translation units.
  ChildErrorHandlingStrategy HandleChildErrors(FromDC);

  auto MightNeedReordering = [](const Decl *D) {
    return isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D) || isa<FriendDecl>(D);
  };

  // Import everything that might need reordering first.
  Error ChildErrors = Error::success();
  for (auto *From : FromDC->decls()) {
    if (!MightNeedReordering(From))
      continue;

    ExpectedDecl ImportedOrErr = import(From);

    // If we are in the process of ImportDefinition(...) for a RecordDecl we
    // want to make sure that we are also completing each FieldDecl. There
    // are currently cases where this does not happen and this is correctness
    // fix since operations such as code generation will expect this to be so.
    if (!ImportedOrErr) {
      HandleChildErrors.handleChildImportResult(ChildErrors,
                                                ImportedOrErr.takeError());
      continue;
    }
    FieldDecl *FieldFrom = dyn_cast_or_null<FieldDecl>(From);
    Decl *ImportedDecl = *ImportedOrErr;
    FieldDecl *FieldTo = dyn_cast_or_null<FieldDecl>(ImportedDecl);
    if (FieldFrom && FieldTo) {
      Error Err = ImportFieldDeclDefinition(FieldFrom, FieldTo);
      HandleChildErrors.handleChildImportResult(ChildErrors, std::move(Err));
    }
  }

  // We reorder declarations in RecordDecls because they may have another order
  // in the "to" context than they have in the "from" context. This may happen
  // e.g when we import a class like this:
  //    struct declToImport {
  //        int a = c + b;
  //        int b = 1;
  //        int c = 2;
  //    };
  // During the import of `a` we import first the dependencies in sequence,
  // thus the order would be `c`, `b`, `a`. We will get the normal order by
  // first removing the already imported members and then adding them in the
  // order as they appear in the "from" context.
  //
  // Keeping field order is vital because it determines structure layout.
  //
  // Here and below, we cannot call field_begin() method and its callers on
  // ToDC if it has an external storage. Calling field_begin() will
  // automatically load all the fields by calling
  // LoadFieldsFromExternalStorage(). LoadFieldsFromExternalStorage() would
  // call ASTImporter::Import(). This is because the ExternalASTSource
  // interface in LLDB is implemented by the means of the ASTImporter. However,
  // calling an import at this point would result in an uncontrolled import, we
  // must avoid that.

  auto ToDCOrErr = Importer.ImportContext(FromDC);
  if (!ToDCOrErr) {
    consumeError(std::move(ChildErrors));
    return ToDCOrErr.takeError();
  }

  if (const auto *FromRD = dyn_cast<RecordDecl>(FromDC)) {
    DeclContext *ToDC = *ToDCOrErr;
    // Remove all declarations, which may be in wrong order in the
    // lexical DeclContext and then add them in the proper order.
    for (auto *D : FromRD->decls()) {
      if (!MightNeedReordering(D))
        continue;

      assert(D && "DC contains a null decl");
      if (Decl *ToD = Importer.GetAlreadyImportedOrNull(D)) {
        // Remove only the decls which we successfully imported.
        assert(ToDC == ToD->getLexicalDeclContext() && ToDC->containsDecl(ToD));
        // Remove the decl from its wrong place in the linked list.
        ToDC->removeDecl(ToD);
        // Add the decl to the end of the linked list.
        // This time it will be at the proper place because the enclosing for
        // loop iterates in the original (good) order of the decls.
        ToDC->addDeclInternal(ToD);
      }
    }
  }

  // Import everything else.
  for (auto *From : FromDC->decls()) {
    if (MightNeedReordering(From))
      continue;

    ExpectedDecl ImportedOrErr = import(From);
    if (!ImportedOrErr)
      HandleChildErrors.handleChildImportResult(ChildErrors,
                                                ImportedOrErr.takeError());
  }

  return ChildErrors;
}

Error ASTNodeImporter::ImportFieldDeclDefinition(const FieldDecl *From,
                                                 const FieldDecl *To) {
  RecordDecl *FromRecordDecl = nullptr;
  RecordDecl *ToRecordDecl = nullptr;
  // If we have a field that is an ArrayType we need to check if the array
  // element is a RecordDecl and if so we need to import the definition.
  QualType FromType = From->getType();
  QualType ToType = To->getType();
  if (FromType->isArrayType()) {
    // getBaseElementTypeUnsafe(...) handles multi-dimensonal arrays for us.
    FromRecordDecl = FromType->getBaseElementTypeUnsafe()->getAsRecordDecl();
    ToRecordDecl = ToType->getBaseElementTypeUnsafe()->getAsRecordDecl();
  }

  if (!FromRecordDecl || !ToRecordDecl) {
    const RecordType *RecordFrom = FromType->getAs<RecordType>();
    const RecordType *RecordTo = ToType->getAs<RecordType>();

    if (RecordFrom && RecordTo) {
      FromRecordDecl = RecordFrom->getDecl();
      ToRecordDecl = RecordTo->getDecl();
    }
  }

  if (FromRecordDecl && ToRecordDecl) {
    if (FromRecordDecl->isCompleteDefinition() &&
        !ToRecordDecl->isCompleteDefinition())
      return ImportDefinition(FromRecordDecl, ToRecordDecl);
  }

  return Error::success();
}

Error ASTNodeImporter::ImportDeclContext(
    Decl *FromD, DeclContext *&ToDC, DeclContext *&ToLexicalDC) {
  auto ToDCOrErr = Importer.ImportContext(FromD->getDeclContext());
  if (!ToDCOrErr)
    return ToDCOrErr.takeError();
  ToDC = *ToDCOrErr;

  if (FromD->getDeclContext() != FromD->getLexicalDeclContext()) {
    auto ToLexicalDCOrErr = Importer.ImportContext(
        FromD->getLexicalDeclContext());
    if (!ToLexicalDCOrErr)
      return ToLexicalDCOrErr.takeError();
    ToLexicalDC = *ToLexicalDCOrErr;
  } else
    ToLexicalDC = ToDC;

  return Error::success();
}

Error ASTNodeImporter::ImportImplicitMethods(
    const CXXRecordDecl *From, CXXRecordDecl *To) {
  assert(From->isCompleteDefinition() && To->getDefinition() == To &&
      "Import implicit methods to or from non-definition");

  for (CXXMethodDecl *FromM : From->methods())
    if (FromM->isImplicit()) {
      Expected<CXXMethodDecl *> ToMOrErr = import(FromM);
      if (!ToMOrErr)
        return ToMOrErr.takeError();
    }

  return Error::success();
}

static Error setTypedefNameForAnonDecl(TagDecl *From, TagDecl *To,
                                       ASTImporter &Importer) {
  if (TypedefNameDecl *FromTypedef = From->getTypedefNameForAnonDecl()) {
    if (ExpectedDecl ToTypedefOrErr = Importer.Import(FromTypedef))
      To->setTypedefNameForAnonDecl(cast<TypedefNameDecl>(*ToTypedefOrErr));
    else
      return ToTypedefOrErr.takeError();
  }
  return Error::success();
}

Error ASTNodeImporter::ImportDefinition(
    RecordDecl *From, RecordDecl *To, ImportDefinitionKind Kind) {
  auto DefinitionCompleter = [To]() {
    // There are cases in LLDB when we first import a class without its
    // members. The class will have DefinitionData, but no members. Then,
    // importDefinition is called from LLDB, which tries to get the members, so
    // when we get here, the class already has the DefinitionData set, so we
    // must unset the CompleteDefinition here to be able to complete again the
    // definition.
    To->setCompleteDefinition(false);
    To->completeDefinition();
  };

  if (To->getDefinition() || To->isBeingDefined()) {
    if (Kind == IDK_Everything ||
        // In case of lambdas, the class already has a definition ptr set, but
        // the contained decls are not imported yet. Also, isBeingDefined was
        // set in CXXRecordDecl::CreateLambda.  We must import the contained
        // decls here and finish the definition.
        (To->isLambda() && shouldForceImportDeclContext(Kind))) {
      if (To->isLambda()) {
        auto *FromCXXRD = cast<CXXRecordDecl>(From);
        SmallVector<LambdaCapture, 8> ToCaptures;
        ToCaptures.reserve(FromCXXRD->capture_size());
        for (const auto &FromCapture : FromCXXRD->captures()) {
          if (auto ToCaptureOrErr = import(FromCapture))
            ToCaptures.push_back(*ToCaptureOrErr);
          else
            return ToCaptureOrErr.takeError();
        }
        cast<CXXRecordDecl>(To)->setCaptures(Importer.getToContext(),
                                             ToCaptures);
      }

      Error Result = ImportDeclContext(From, /*ForceImport=*/true);
      // Finish the definition of the lambda, set isBeingDefined to false.
      if (To->isLambda())
        DefinitionCompleter();
      return Result;
    }

    return Error::success();
  }

  To->startDefinition();
  // Set the definition to complete even if it is really not complete during
  // import. Some AST constructs (expressions) require the record layout
  // to be calculated (see 'clang::computeDependence') at the time they are
  // constructed. Import of such AST node is possible during import of the
  // same record, there is no way to have a completely defined record (all
  // fields imported) at that time without multiple AST import passes.
  if (!Importer.isMinimalImport())
    To->setCompleteDefinition(true);
  // Complete the definition even if error is returned.
  // The RecordDecl may be already part of the AST so it is better to
  // have it in complete state even if something is wrong with it.
  auto DefinitionCompleterScopeExit =
      llvm::make_scope_exit(DefinitionCompleter);

  if (Error Err = setTypedefNameForAnonDecl(From, To, Importer))
    return Err;

  // Add base classes.
  auto *ToCXX = dyn_cast<CXXRecordDecl>(To);
  auto *FromCXX = dyn_cast<CXXRecordDecl>(From);
  if (ToCXX && FromCXX && ToCXX->dataPtr() && FromCXX->dataPtr()) {

    struct CXXRecordDecl::DefinitionData &ToData = ToCXX->data();
    struct CXXRecordDecl::DefinitionData &FromData = FromCXX->data();

    #define FIELD(Name, Width, Merge) \
    ToData.Name = FromData.Name;
    #include "clang/AST/CXXRecordDeclDefinitionBits.def"

    // Copy over the data stored in RecordDeclBits
    ToCXX->setArgPassingRestrictions(FromCXX->getArgPassingRestrictions());

    SmallVector<CXXBaseSpecifier *, 4> Bases;
    for (const auto &Base1 : FromCXX->bases()) {
      ExpectedType TyOrErr = import(Base1.getType());
      if (!TyOrErr)
        return TyOrErr.takeError();

      SourceLocation EllipsisLoc;
      if (Base1.isPackExpansion()) {
        if (ExpectedSLoc LocOrErr = import(Base1.getEllipsisLoc()))
          EllipsisLoc = *LocOrErr;
        else
          return LocOrErr.takeError();
      }

      // Ensure that we have a definition for the base.
      if (Error Err =
          ImportDefinitionIfNeeded(Base1.getType()->getAsCXXRecordDecl()))
        return Err;

      auto RangeOrErr = import(Base1.getSourceRange());
      if (!RangeOrErr)
        return RangeOrErr.takeError();

      auto TSIOrErr = import(Base1.getTypeSourceInfo());
      if (!TSIOrErr)
        return TSIOrErr.takeError();

      Bases.push_back(
          new (Importer.getToContext()) CXXBaseSpecifier(
              *RangeOrErr,
              Base1.isVirtual(),
              Base1.isBaseOfClass(),
              Base1.getAccessSpecifierAsWritten(),
              *TSIOrErr,
              EllipsisLoc));
    }
    if (!Bases.empty())
      ToCXX->setBases(Bases.data(), Bases.size());
  }

  if (shouldForceImportDeclContext(Kind)) {
    if (Error Err = ImportDeclContext(From, /*ForceImport=*/true))
      return Err;
  }

  return Error::success();
}

Error ASTNodeImporter::ImportInitializer(VarDecl *From, VarDecl *To) {
  if (To->getAnyInitializer())
    return Error::success();

  Expr *FromInit = From->getInit();
  if (!FromInit)
    return Error::success();

  ExpectedExpr ToInitOrErr = import(FromInit);
  if (!ToInitOrErr)
    return ToInitOrErr.takeError();

  To->setInit(*ToInitOrErr);
  if (EvaluatedStmt *FromEval = From->getEvaluatedStmt()) {
    EvaluatedStmt *ToEval = To->ensureEvaluatedStmt();
    ToEval->HasConstantInitialization = FromEval->HasConstantInitialization;
    ToEval->HasConstantDestruction = FromEval->HasConstantDestruction;
    // FIXME: Also import the initializer value.
  }

  // FIXME: Other bits to merge?
  return Error::success();
}

Error ASTNodeImporter::ImportDefinition(
    EnumDecl *From, EnumDecl *To, ImportDefinitionKind Kind) {
  if (To->getDefinition() || To->isBeingDefined()) {
    if (Kind == IDK_Everything)
      return ImportDeclContext(From, /*ForceImport=*/true);
    return Error::success();
  }

  To->startDefinition();

  if (Error Err = setTypedefNameForAnonDecl(From, To, Importer))
    return Err;

  ExpectedType ToTypeOrErr =
      import(Importer.getFromContext().getTypeDeclType(From));
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedType ToPromotionTypeOrErr = import(From->getPromotionType());
  if (!ToPromotionTypeOrErr)
    return ToPromotionTypeOrErr.takeError();

  if (shouldForceImportDeclContext(Kind))
    if (Error Err = ImportDeclContext(From, /*ForceImport=*/true))
      return Err;

  // FIXME: we might need to merge the number of positive or negative bits
  // if the enumerator lists don't match.
  To->completeDefinition(*ToTypeOrErr, *ToPromotionTypeOrErr,
                         From->getNumPositiveBits(),
                         From->getNumNegativeBits());
  return Error::success();
}

Error ASTNodeImporter::ImportTemplateArguments(
    ArrayRef<TemplateArgument> FromArgs,
    SmallVectorImpl<TemplateArgument> &ToArgs) {
  for (const auto &Arg : FromArgs) {
    if (auto ToOrErr = import(Arg))
      ToArgs.push_back(*ToOrErr);
    else
      return ToOrErr.takeError();
  }

  return Error::success();
}

// FIXME: Do not forget to remove this and use only 'import'.
Expected<TemplateArgument>
ASTNodeImporter::ImportTemplateArgument(const TemplateArgument &From) {
  return import(From);
}

template <typename InContainerTy>
Error ASTNodeImporter::ImportTemplateArgumentListInfo(
    const InContainerTy &Container, TemplateArgumentListInfo &ToTAInfo) {
  for (const auto &FromLoc : Container) {
    if (auto ToLocOrErr = import(FromLoc))
      ToTAInfo.addArgument(*ToLocOrErr);
    else
      return ToLocOrErr.takeError();
  }
  return Error::success();
}

static StructuralEquivalenceKind
getStructuralEquivalenceKind(const ASTImporter &Importer) {
  return Importer.isMinimalImport() ? StructuralEquivalenceKind::Minimal
                                    : StructuralEquivalenceKind::Default;
}

bool ASTNodeImporter::IsStructuralMatch(Decl *From, Decl *To, bool Complain,
                                        bool IgnoreTemplateParmDepth) {
  // Eliminate a potential failure point where we attempt to re-import
  // something we're trying to import while completing ToRecord.
  Decl *ToOrigin = Importer.GetOriginalDecl(To);
  if (ToOrigin) {
    To = ToOrigin;
  }

  StructuralEquivalenceContext Ctx(
      Importer.getFromContext(), Importer.getToContext(),
      Importer.getNonEquivalentDecls(), getStructuralEquivalenceKind(Importer),
      /*StrictTypeSpelling=*/false, Complain, /*ErrorOnTagTypeMismatch=*/false,
      IgnoreTemplateParmDepth);
  return Ctx.IsEquivalent(From, To);
}

ExpectedDecl ASTNodeImporter::VisitDecl(Decl *D) {
  Importer.FromDiag(D->getLocation(), diag::err_unsupported_ast_node)
    << D->getDeclKindName();
  return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
}

ExpectedDecl ASTNodeImporter::VisitImportDecl(ImportDecl *D) {
  Importer.FromDiag(D->getLocation(), diag::err_unsupported_ast_node)
      << D->getDeclKindName();
  return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
}

ExpectedDecl ASTNodeImporter::VisitEmptyDecl(EmptyDecl *D) {
  // Import the context of this declaration.
  DeclContext *DC, *LexicalDC;
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return std::move(Err);

  // Import the location of this declaration.
  ExpectedSLoc LocOrErr = import(D->getLocation());
  if (!LocOrErr)
    return LocOrErr.takeError();

  EmptyDecl *ToD;
  if (GetImportedOrCreateDecl(ToD, D, Importer.getToContext(), DC, *LocOrErr))
    return ToD;

  ToD->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToD);
  return ToD;
}

ExpectedDecl ASTNodeImporter::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  TranslationUnitDecl *ToD =
    Importer.getToContext().getTranslationUnitDecl();

  Importer.MapImported(D, ToD);

  return ToD;
}

ExpectedDecl ASTNodeImporter::VisitBindingDecl(BindingDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToND;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToND, Loc))
    return std::move(Err);
  if (ToND)
    return ToND;

  BindingDecl *ToD;
  if (GetImportedOrCreateDecl(ToD, D, Importer.getToContext(), DC, Loc,
                              Name.getAsIdentifierInfo()))
    return ToD;

  Error Err = Error::success();
  QualType ToType = importChecked(Err, D->getType());
  Expr *ToBinding = importChecked(Err, D->getBinding());
  ValueDecl *ToDecomposedDecl = importChecked(Err, D->getDecomposedDecl());
  if (Err)
    return std::move(Err);

  ToD->setBinding(ToType, ToBinding);
  ToD->setDecomposedDecl(ToDecomposedDecl);
  addDeclToContexts(D, ToD);

  return ToD;
}

ExpectedDecl ASTNodeImporter::VisitAccessSpecDecl(AccessSpecDecl *D) {
  ExpectedSLoc LocOrErr = import(D->getLocation());
  if (!LocOrErr)
    return LocOrErr.takeError();
  auto ColonLocOrErr = import(D->getColonLoc());
  if (!ColonLocOrErr)
    return ColonLocOrErr.takeError();

  // Import the context of this declaration.
  auto DCOrErr = Importer.ImportContext(D->getDeclContext());
  if (!DCOrErr)
    return DCOrErr.takeError();
  DeclContext *DC = *DCOrErr;

  AccessSpecDecl *ToD;
  if (GetImportedOrCreateDecl(ToD, D, Importer.getToContext(), D->getAccess(),
                              DC, *LocOrErr, *ColonLocOrErr))
    return ToD;

  // Lexical DeclContext and Semantic DeclContext
  // is always the same for the accessSpec.
  ToD->setLexicalDeclContext(DC);
  DC->addDeclInternal(ToD);

  return ToD;
}

ExpectedDecl ASTNodeImporter::VisitStaticAssertDecl(StaticAssertDecl *D) {
  auto DCOrErr = Importer.ImportContext(D->getDeclContext());
  if (!DCOrErr)
    return DCOrErr.takeError();
  DeclContext *DC = *DCOrErr;
  DeclContext *LexicalDC = DC;

  Error Err = Error::success();
  auto ToLocation = importChecked(Err, D->getLocation());
  auto ToRParenLoc = importChecked(Err, D->getRParenLoc());
  auto ToAssertExpr = importChecked(Err, D->getAssertExpr());
  auto ToMessage = importChecked(Err, D->getMessage());
  if (Err)
    return std::move(Err);

  StaticAssertDecl *ToD;
  if (GetImportedOrCreateDecl(
      ToD, D, Importer.getToContext(), DC, ToLocation, ToAssertExpr, ToMessage,
      ToRParenLoc, D->isFailed()))
    return ToD;

  ToD->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToD);
  return ToD;
}

ExpectedDecl ASTNodeImporter::VisitNamespaceDecl(NamespaceDecl *D) {
  // Import the major distinguishing characteristics of this namespace.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  NamespaceDecl *MergeWithNamespace = nullptr;
  if (!Name) {
    // This is an anonymous namespace. Adopt an existing anonymous
    // namespace if we can.
    // FIXME: Not testable.
    if (auto *TU = dyn_cast<TranslationUnitDecl>(DC))
      MergeWithNamespace = TU->getAnonymousNamespace();
    else
      MergeWithNamespace = cast<NamespaceDecl>(DC)->getAnonymousNamespace();
  } else {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(Decl::IDNS_Namespace))
        continue;

      if (auto *FoundNS = dyn_cast<NamespaceDecl>(FoundDecl)) {
        MergeWithNamespace = FoundNS;
        ConflictingDecls.clear();
        break;
      }

      ConflictingDecls.push_back(FoundDecl);
    }

    if (!ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          Name, DC, Decl::IDNS_Namespace, ConflictingDecls.data(),
          ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  ExpectedSLoc BeginLocOrErr = import(D->getBeginLoc());
  if (!BeginLocOrErr)
    return BeginLocOrErr.takeError();
  ExpectedSLoc RBraceLocOrErr = import(D->getRBraceLoc());
  if (!RBraceLocOrErr)
    return RBraceLocOrErr.takeError();

  // Create the "to" namespace, if needed.
  NamespaceDecl *ToNamespace = MergeWithNamespace;
  if (!ToNamespace) {
    if (GetImportedOrCreateDecl(ToNamespace, D, Importer.getToContext(), DC,
                                D->isInline(), *BeginLocOrErr, Loc,
                                Name.getAsIdentifierInfo(),
                                /*PrevDecl=*/nullptr, D->isNested()))
      return ToNamespace;
    ToNamespace->setRBraceLoc(*RBraceLocOrErr);
    ToNamespace->setLexicalDeclContext(LexicalDC);
    LexicalDC->addDeclInternal(ToNamespace);

    // If this is an anonymous namespace, register it as the anonymous
    // namespace within its context.
    if (!Name) {
      if (auto *TU = dyn_cast<TranslationUnitDecl>(DC))
        TU->setAnonymousNamespace(ToNamespace);
      else
        cast<NamespaceDecl>(DC)->setAnonymousNamespace(ToNamespace);
    }
  }
  Importer.MapImported(D, ToNamespace);

  if (Error Err = ImportDeclContext(D))
    return std::move(Err);

  return ToNamespace;
}

ExpectedDecl ASTNodeImporter::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  // Import the major distinguishing characteristics of this namespace.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *LookupD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, LookupD, Loc))
    return std::move(Err);
  if (LookupD)
    return LookupD;

  // NOTE: No conflict resolution is done for namespace aliases now.

  Error Err = Error::success();
  auto ToNamespaceLoc = importChecked(Err, D->getNamespaceLoc());
  auto ToAliasLoc = importChecked(Err, D->getAliasLoc());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  auto ToTargetNameLoc = importChecked(Err, D->getTargetNameLoc());
  auto ToNamespace = importChecked(Err, D->getNamespace());
  if (Err)
    return std::move(Err);

  IdentifierInfo *ToIdentifier = Importer.Import(D->getIdentifier());

  NamespaceAliasDecl *ToD;
  if (GetImportedOrCreateDecl(
      ToD, D, Importer.getToContext(), DC, ToNamespaceLoc, ToAliasLoc,
      ToIdentifier, ToQualifierLoc, ToTargetNameLoc, ToNamespace))
    return ToD;

  ToD->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToD);

  return ToD;
}

ExpectedDecl
ASTNodeImporter::VisitTypedefNameDecl(TypedefNameDecl *D, bool IsAlias) {
  // Import the major distinguishing characteristics of this typedef.
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  // Do not import the DeclContext, we will import it once the TypedefNameDecl
  // is created.
  if (Error Err = ImportDeclParts(D, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  DeclContext *DC = cast_or_null<DeclContext>(
      Importer.GetAlreadyImportedOrNull(cast<Decl>(D->getDeclContext())));
  DeclContext *LexicalDC =
      cast_or_null<DeclContext>(Importer.GetAlreadyImportedOrNull(
          cast<Decl>(D->getLexicalDeclContext())));

  // If this typedef is not in block scope, determine whether we've
  // seen a typedef with the same name (that we can merge with) or any
  // other entity by that name (which name lookup could conflict with).
  // Note: Repeated typedefs are not valid in C99:
  // 'typedef int T; typedef int T;' is invalid
  // We do not care about this now.
  if (DC && !DC->isFunctionOrMethod()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    unsigned IDNS = Decl::IDNS_Ordinary;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;
      if (auto *FoundTypedef = dyn_cast<TypedefNameDecl>(FoundDecl)) {
        if (!hasSameVisibilityContextAndLinkage(FoundTypedef, D))
          continue;

        QualType FromUT = D->getUnderlyingType();
        QualType FoundUT = FoundTypedef->getUnderlyingType();
        if (Importer.IsStructurallyEquivalent(FromUT, FoundUT)) {
          // If the underlying declarations are unnamed records these can be
          // imported as different types. We should create a distinct typedef
          // node in this case.
          // If we found an existing underlying type with a record in a
          // different context (than the imported), this is already reason for
          // having distinct typedef nodes for these.
          // Again this can create situation like
          // 'typedef int T; typedef int T;' but this is hard to avoid without
          // a rename strategy at import.
          if (!FromUT.isNull() && !FoundUT.isNull()) {
            RecordDecl *FromR = FromUT->getAsRecordDecl();
            RecordDecl *FoundR = FoundUT->getAsRecordDecl();
            if (FromR && FoundR &&
                !hasSameVisibilityContextAndLinkage(FoundR, FromR))
              continue;
          }
          // If the "From" context has a complete underlying type but we
          // already have a complete underlying type then return with that.
          if (!FromUT->isIncompleteType() && !FoundUT->isIncompleteType())
            return Importer.MapImported(D, FoundTypedef);
          // FIXME Handle redecl chain. When you do that make consistent changes
          // in ASTImporterLookupTable too.
        } else {
          ConflictingDecls.push_back(FoundDecl);
        }
      }
    }

    if (!ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          Name, DC, IDNS, ConflictingDecls.data(), ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  Error Err = Error::success();
  auto ToUnderlyingType = importChecked(Err, D->getUnderlyingType());
  auto ToTypeSourceInfo = importChecked(Err, D->getTypeSourceInfo());
  auto ToBeginLoc = importChecked(Err, D->getBeginLoc());
  if (Err)
    return std::move(Err);

  // Create the new typedef node.
  // FIXME: ToUnderlyingType is not used.
  (void)ToUnderlyingType;
  TypedefNameDecl *ToTypedef;
  if (IsAlias) {
    if (GetImportedOrCreateDecl<TypeAliasDecl>(
        ToTypedef, D, Importer.getToContext(), DC, ToBeginLoc, Loc,
        Name.getAsIdentifierInfo(), ToTypeSourceInfo))
      return ToTypedef;
  } else if (GetImportedOrCreateDecl<TypedefDecl>(
      ToTypedef, D, Importer.getToContext(), DC, ToBeginLoc, Loc,
      Name.getAsIdentifierInfo(), ToTypeSourceInfo))
    return ToTypedef;

  // Import the DeclContext and set it to the Typedef.
  if ((Err = ImportDeclContext(D, DC, LexicalDC)))
    return std::move(Err);
  ToTypedef->setDeclContext(DC);
  ToTypedef->setLexicalDeclContext(LexicalDC);
  // Add to the lookupTable because we could not do that in MapImported.
  Importer.AddToLookupTable(ToTypedef);

  ToTypedef->setAccess(D->getAccess());

  // Templated declarations should not appear in DeclContext.
  TypeAliasDecl *FromAlias = IsAlias ? cast<TypeAliasDecl>(D) : nullptr;
  if (!FromAlias || !FromAlias->getDescribedAliasTemplate())
    LexicalDC->addDeclInternal(ToTypedef);

  return ToTypedef;
}

ExpectedDecl ASTNodeImporter::VisitTypedefDecl(TypedefDecl *D) {
  return VisitTypedefNameDecl(D, /*IsAlias=*/false);
}

ExpectedDecl ASTNodeImporter::VisitTypeAliasDecl(TypeAliasDecl *D) {
  return VisitTypedefNameDecl(D, /*IsAlias=*/true);
}

ExpectedDecl
ASTNodeImporter::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
  // Import the major distinguishing characteristics of this typedef.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *FoundD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, FoundD, Loc))
    return std::move(Err);
  if (FoundD)
    return FoundD;

  // If this typedef is not in block scope, determine whether we've
  // seen a typedef with the same name (that we can merge with) or any
  // other entity by that name (which name lookup could conflict with).
  if (!DC->isFunctionOrMethod()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    unsigned IDNS = Decl::IDNS_Ordinary;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;
      if (auto *FoundAlias = dyn_cast<TypeAliasTemplateDecl>(FoundDecl)) {
        if (IsStructuralMatch(D, FoundAlias))
          return Importer.MapImported(D, FoundAlias);
        ConflictingDecls.push_back(FoundDecl);
      }
    }

    if (!ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          Name, DC, IDNS, ConflictingDecls.data(), ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  Error Err = Error::success();
  auto ToTemplateParameters = importChecked(Err, D->getTemplateParameters());
  auto ToTemplatedDecl = importChecked(Err, D->getTemplatedDecl());
  if (Err)
    return std::move(Err);

  TypeAliasTemplateDecl *ToAlias;
  if (GetImportedOrCreateDecl(ToAlias, D, Importer.getToContext(), DC, Loc,
                              Name, ToTemplateParameters, ToTemplatedDecl))
    return ToAlias;

  ToTemplatedDecl->setDescribedAliasTemplate(ToAlias);

  ToAlias->setAccess(D->getAccess());
  ToAlias->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToAlias);
  if (DC != Importer.getToContext().getTranslationUnitDecl())
    updateLookupTableForTemplateParameters(*ToTemplateParameters);
  return ToAlias;
}

ExpectedDecl ASTNodeImporter::VisitLabelDecl(LabelDecl *D) {
  // Import the major distinguishing characteristics of this label.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  assert(LexicalDC->isFunctionOrMethod());

  LabelDecl *ToLabel;
  if (D->isGnuLocal()) {
    ExpectedSLoc BeginLocOrErr = import(D->getBeginLoc());
    if (!BeginLocOrErr)
      return BeginLocOrErr.takeError();
    if (GetImportedOrCreateDecl(ToLabel, D, Importer.getToContext(), DC, Loc,
                                Name.getAsIdentifierInfo(), *BeginLocOrErr))
      return ToLabel;

  } else {
    if (GetImportedOrCreateDecl(ToLabel, D, Importer.getToContext(), DC, Loc,
                                Name.getAsIdentifierInfo()))
      return ToLabel;

  }

  Expected<LabelStmt *> ToStmtOrErr = import(D->getStmt());
  if (!ToStmtOrErr)
    return ToStmtOrErr.takeError();

  ToLabel->setStmt(*ToStmtOrErr);
  ToLabel->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToLabel);
  return ToLabel;
}

ExpectedDecl ASTNodeImporter::VisitEnumDecl(EnumDecl *D) {
  // Import the major distinguishing characteristics of this enum.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Figure out what enum name we're looking for.
  unsigned IDNS = Decl::IDNS_Tag;
  DeclarationName SearchName = Name;
  if (!SearchName && D->getTypedefNameForAnonDecl()) {
    if (Error Err = importInto(
        SearchName, D->getTypedefNameForAnonDecl()->getDeclName()))
      return std::move(Err);
    IDNS = Decl::IDNS_Ordinary;
  } else if (Importer.getToContext().getLangOpts().CPlusPlus)
    IDNS |= Decl::IDNS_Ordinary;

  // We may already have an enum of the same name; try to find and match it.
  EnumDecl *PrevDecl = nullptr;
  if (!DC->isFunctionOrMethod()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    auto FoundDecls =
        Importer.findDeclsInToCtx(DC, SearchName);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;

      if (auto *Typedef = dyn_cast<TypedefNameDecl>(FoundDecl)) {
        if (const auto *Tag = Typedef->getUnderlyingType()->getAs<TagType>())
          FoundDecl = Tag->getDecl();
      }

      if (auto *FoundEnum = dyn_cast<EnumDecl>(FoundDecl)) {
        if (!hasSameVisibilityContextAndLinkage(FoundEnum, D))
          continue;
        if (IsStructuralMatch(D, FoundEnum, !SearchName.isEmpty())) {
          EnumDecl *FoundDef = FoundEnum->getDefinition();
          if (D->isThisDeclarationADefinition() && FoundDef)
            return Importer.MapImported(D, FoundDef);
          PrevDecl = FoundEnum->getMostRecentDecl();
          break;
        }
        ConflictingDecls.push_back(FoundDecl);
      }
    }

    // In case of unnamed enums, we try to find an existing similar one, if none
    // was found, perform the import always.
    // Structural in-equivalence is not detected in this way here, but it may
    // be found when the parent decl is imported (if the enum is part of a
    // class). To make this totally exact a more difficult solution is needed.
    if (SearchName && !ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          SearchName, DC, IDNS, ConflictingDecls.data(),
          ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  Error Err = Error::success();
  auto ToBeginLoc = importChecked(Err, D->getBeginLoc());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  auto ToIntegerType = importChecked(Err, D->getIntegerType());
  auto ToBraceRange = importChecked(Err, D->getBraceRange());
  if (Err)
    return std::move(Err);

  // Create the enum declaration.
  EnumDecl *D2;
  if (GetImportedOrCreateDecl(
          D2, D, Importer.getToContext(), DC, ToBeginLoc,
          Loc, Name.getAsIdentifierInfo(), PrevDecl, D->isScoped(),
          D->isScopedUsingClassTag(), D->isFixed()))
    return D2;

  D2->setQualifierInfo(ToQualifierLoc);
  D2->setIntegerType(ToIntegerType);
  D2->setBraceRange(ToBraceRange);
  D2->setAccess(D->getAccess());
  D2->setLexicalDeclContext(LexicalDC);
  addDeclToContexts(D, D2);

  if (MemberSpecializationInfo *MemberInfo = D->getMemberSpecializationInfo()) {
    TemplateSpecializationKind SK = MemberInfo->getTemplateSpecializationKind();
    EnumDecl *FromInst = D->getInstantiatedFromMemberEnum();
    if (Expected<EnumDecl *> ToInstOrErr = import(FromInst))
      D2->setInstantiationOfMemberEnum(*ToInstOrErr, SK);
    else
      return ToInstOrErr.takeError();
    if (ExpectedSLoc POIOrErr = import(MemberInfo->getPointOfInstantiation()))
      D2->getMemberSpecializationInfo()->setPointOfInstantiation(*POIOrErr);
    else
      return POIOrErr.takeError();
  }

  // Import the definition
  if (D->isCompleteDefinition())
    if (Error Err = ImportDefinition(D, D2))
      return std::move(Err);

  return D2;
}

ExpectedDecl ASTNodeImporter::VisitRecordDecl(RecordDecl *D) {
  bool IsFriendTemplate = false;
  if (auto *DCXX = dyn_cast<CXXRecordDecl>(D)) {
    IsFriendTemplate =
        DCXX->getDescribedClassTemplate() &&
        DCXX->getDescribedClassTemplate()->getFriendObjectKind() !=
            Decl::FOK_None;
  }

  // Import the major distinguishing characteristics of this record.
  DeclContext *DC = nullptr, *LexicalDC = nullptr;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Figure out what structure name we're looking for.
  unsigned IDNS = Decl::IDNS_Tag;
  DeclarationName SearchName = Name;
  if (!SearchName && D->getTypedefNameForAnonDecl()) {
    if (Error Err = importInto(
        SearchName, D->getTypedefNameForAnonDecl()->getDeclName()))
      return std::move(Err);
    IDNS = Decl::IDNS_Ordinary;
  } else if (Importer.getToContext().getLangOpts().CPlusPlus)
    IDNS |= Decl::IDNS_Ordinary | Decl::IDNS_TagFriend;

  bool IsDependentContext = DC != LexicalDC ? LexicalDC->isDependentContext()
                                            : DC->isDependentContext();
  bool DependentFriend = IsFriendTemplate && IsDependentContext;

  // We may already have a record of the same name; try to find and match it.
  RecordDecl *PrevDecl = nullptr;
  if (!DependentFriend && !DC->isFunctionOrMethod() && !D->isLambda()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    auto FoundDecls =
        Importer.findDeclsInToCtx(DC, SearchName);
    if (!FoundDecls.empty()) {
      // We're going to have to compare D against potentially conflicting Decls,
      // so complete it.
      if (D->hasExternalLexicalStorage() && !D->isCompleteDefinition())
        D->getASTContext().getExternalSource()->CompleteType(D);
    }

    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;

      Decl *Found = FoundDecl;
      if (auto *Typedef = dyn_cast<TypedefNameDecl>(Found)) {
        if (const auto *Tag = Typedef->getUnderlyingType()->getAs<TagType>())
          Found = Tag->getDecl();
      }

      if (auto *FoundRecord = dyn_cast<RecordDecl>(Found)) {
        // Do not emit false positive diagnostic in case of unnamed
        // struct/union and in case of anonymous structs.  Would be false
        // because there may be several anonymous/unnamed structs in a class.
        // E.g. these are both valid:
        //  struct A { // unnamed structs
        //    struct { struct A *next; } entry0;
        //    struct { struct A *next; } entry1;
        //  };
        //  struct X { struct { int a; }; struct { int b; }; }; // anon structs
        if (!SearchName)
          if (!IsStructuralMatch(D, FoundRecord, false))
            continue;

        if (!hasSameVisibilityContextAndLinkage(FoundRecord, D))
          continue;

        if (IsStructuralMatch(D, FoundRecord)) {
          RecordDecl *FoundDef = FoundRecord->getDefinition();
          if (D->isThisDeclarationADefinition() && FoundDef) {
            // FIXME: Structural equivalence check should check for same
            // user-defined methods.
            Importer.MapImported(D, FoundDef);
            if (const auto *DCXX = dyn_cast<CXXRecordDecl>(D)) {
              auto *FoundCXX = dyn_cast<CXXRecordDecl>(FoundDef);
              assert(FoundCXX && "Record type mismatch");

              if (!Importer.isMinimalImport())
                // FoundDef may not have every implicit method that D has
                // because implicit methods are created only if they are used.
                if (Error Err = ImportImplicitMethods(DCXX, FoundCXX))
                  return std::move(Err);
            }
          }
          PrevDecl = FoundRecord->getMostRecentDecl();
          break;
        }
        ConflictingDecls.push_back(FoundDecl);
      } // kind is RecordDecl
    } // for

    if (!ConflictingDecls.empty() && SearchName) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          SearchName, DC, IDNS, ConflictingDecls.data(),
          ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  ExpectedSLoc BeginLocOrErr = import(D->getBeginLoc());
  if (!BeginLocOrErr)
    return BeginLocOrErr.takeError();

  // Create the record declaration.
  RecordDecl *D2 = nullptr;
  CXXRecordDecl *D2CXX = nullptr;
  if (auto *DCXX = dyn_cast<CXXRecordDecl>(D)) {
    if (DCXX->isLambda()) {
      auto TInfoOrErr = import(DCXX->getLambdaTypeInfo());
      if (!TInfoOrErr)
        return TInfoOrErr.takeError();
      if (GetImportedOrCreateSpecialDecl(
              D2CXX, CXXRecordDecl::CreateLambda, D, Importer.getToContext(),
              DC, *TInfoOrErr, Loc, DCXX->getLambdaDependencyKind(),
              DCXX->isGenericLambda(), DCXX->getLambdaCaptureDefault()))
        return D2CXX;
      CXXRecordDecl::LambdaNumbering Numbering = DCXX->getLambdaNumbering();
      ExpectedDecl CDeclOrErr = import(Numbering.ContextDecl);
      if (!CDeclOrErr)
        return CDeclOrErr.takeError();
      Numbering.ContextDecl = *CDeclOrErr;
      D2CXX->setLambdaNumbering(Numbering);
   } else if (DCXX->isInjectedClassName()) {
      // We have to be careful to do a similar dance to the one in
      // Sema::ActOnStartCXXMemberDeclarations
      const bool DelayTypeCreation = true;
      if (GetImportedOrCreateDecl(
              D2CXX, D, Importer.getToContext(), D->getTagKind(), DC,
              *BeginLocOrErr, Loc, Name.getAsIdentifierInfo(),
              cast_or_null<CXXRecordDecl>(PrevDecl), DelayTypeCreation))
        return D2CXX;
      Importer.getToContext().getTypeDeclType(
          D2CXX, dyn_cast<CXXRecordDecl>(DC));
    } else {
      if (GetImportedOrCreateDecl(D2CXX, D, Importer.getToContext(),
                                  D->getTagKind(), DC, *BeginLocOrErr, Loc,
                                  Name.getAsIdentifierInfo(),
                                  cast_or_null<CXXRecordDecl>(PrevDecl)))
        return D2CXX;
    }

    D2 = D2CXX;
    D2->setAccess(D->getAccess());
    D2->setLexicalDeclContext(LexicalDC);
    addDeclToContexts(D, D2);

    if (ClassTemplateDecl *FromDescribed =
        DCXX->getDescribedClassTemplate()) {
      ClassTemplateDecl *ToDescribed;
      if (Error Err = importInto(ToDescribed, FromDescribed))
        return std::move(Err);
      D2CXX->setDescribedClassTemplate(ToDescribed);
      if (!DCXX->isInjectedClassName() && !IsFriendTemplate) {
        // In a record describing a template the type should be an
        // InjectedClassNameType (see Sema::CheckClassTemplate). Update the
        // previously set type to the correct value here (ToDescribed is not
        // available at record create).
        CXXRecordDecl *Injected = nullptr;
        for (NamedDecl *Found : D2CXX->noload_lookup(Name)) {
          auto *Record = dyn_cast<CXXRecordDecl>(Found);
          if (Record && Record->isInjectedClassName()) {
            Injected = Record;
            break;
          }
        }
        // Create an injected type for the whole redecl chain.
        // The chain may contain an already existing injected type at the start,
        // if yes this should be reused. We must ensure that only one type
        // object exists for the injected type (including the injected record
        // declaration), ASTContext does not check it.
        SmallVector<Decl *, 2> Redecls =
            getCanonicalForwardRedeclChain(D2CXX);
        const Type *FrontTy =
            cast<CXXRecordDecl>(Redecls.front())->getTypeForDecl();
        QualType InjSpec;
        if (auto *InjTy = FrontTy->getAs<InjectedClassNameType>())
          InjSpec = InjTy->getInjectedSpecializationType();
        else
          InjSpec = ToDescribed->getInjectedClassNameSpecialization();
        for (auto *R : Redecls) {
          auto *RI = cast<CXXRecordDecl>(R);
          if (R != Redecls.front() ||
              !isa<InjectedClassNameType>(RI->getTypeForDecl()))
            RI->setTypeForDecl(nullptr);
          // This function tries to get the injected type from getTypeForDecl,
          // then from the previous declaration if possible. If not, it creates
          // a new type.
          Importer.getToContext().getInjectedClassNameType(RI, InjSpec);
        }
        // Set the new type for the injected decl too.
        if (Injected) {
          Injected->setTypeForDecl(nullptr);
          // This function will copy the injected type from D2CXX into Injected.
          // The injected decl does not have a previous decl to copy from.
          Importer.getToContext().getTypeDeclType(Injected, D2CXX);
        }
      }
    } else if (MemberSpecializationInfo *MemberInfo =
                   DCXX->getMemberSpecializationInfo()) {
        TemplateSpecializationKind SK =
            MemberInfo->getTemplateSpecializationKind();
        CXXRecordDecl *FromInst = DCXX->getInstantiatedFromMemberClass();

        if (Expected<CXXRecordDecl *> ToInstOrErr = import(FromInst))
          D2CXX->setInstantiationOfMemberClass(*ToInstOrErr, SK);
        else
          return ToInstOrErr.takeError();

        if (ExpectedSLoc POIOrErr =
            import(MemberInfo->getPointOfInstantiation()))
          D2CXX->getMemberSpecializationInfo()->setPointOfInstantiation(
            *POIOrErr);
        else
          return POIOrErr.takeError();
    }

  } else {
    if (GetImportedOrCreateDecl(D2, D, Importer.getToContext(),
                                D->getTagKind(), DC, *BeginLocOrErr, Loc,
                                Name.getAsIdentifierInfo(), PrevDecl))
      return D2;
    D2->setLexicalDeclContext(LexicalDC);
    addDeclToContexts(D, D2);
  }

  if (auto BraceRangeOrErr = import(D->getBraceRange()))
    D2->setBraceRange(*BraceRangeOrErr);
  else
    return BraceRangeOrErr.takeError();
  if (auto QualifierLocOrErr = import(D->getQualifierLoc()))
    D2->setQualifierInfo(*QualifierLocOrErr);
  else
    return QualifierLocOrErr.takeError();

  if (D->isAnonymousStructOrUnion())
    D2->setAnonymousStructOrUnion(true);

  if (D->isCompleteDefinition())
    if (Error Err = ImportDefinition(D, D2, IDK_Default))
      return std::move(Err);

  return D2;
}

ExpectedDecl ASTNodeImporter::VisitEnumConstantDecl(EnumConstantDecl *D) {
  // Import the major distinguishing characteristics of this enumerator.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Determine whether there are any other declarations with the same name and
  // in the same context.
  if (!LexicalDC->isFunctionOrMethod()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    unsigned IDNS = Decl::IDNS_Ordinary;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;

      if (auto *FoundEnumConstant = dyn_cast<EnumConstantDecl>(FoundDecl)) {
        if (IsStructuralMatch(D, FoundEnumConstant))
          return Importer.MapImported(D, FoundEnumConstant);
        ConflictingDecls.push_back(FoundDecl);
      }
    }

    if (!ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          Name, DC, IDNS, ConflictingDecls.data(), ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  ExpectedType TypeOrErr = import(D->getType());
  if (!TypeOrErr)
    return TypeOrErr.takeError();

  ExpectedExpr InitOrErr = import(D->getInitExpr());
  if (!InitOrErr)
    return InitOrErr.takeError();

  EnumConstantDecl *ToEnumerator;
  if (GetImportedOrCreateDecl(
          ToEnumerator, D, Importer.getToContext(), cast<EnumDecl>(DC), Loc,
          Name.getAsIdentifierInfo(), *TypeOrErr, *InitOrErr, D->getInitVal()))
    return ToEnumerator;

  ToEnumerator->setAccess(D->getAccess());
  ToEnumerator->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToEnumerator);
  return ToEnumerator;
}

template <typename DeclTy>
Error ASTNodeImporter::ImportTemplateParameterLists(const DeclTy *FromD,
                                                    DeclTy *ToD) {
  unsigned int Num = FromD->getNumTemplateParameterLists();
  if (Num == 0)
    return Error::success();
  SmallVector<TemplateParameterList *, 2> ToTPLists(Num);
  for (unsigned int I = 0; I < Num; ++I)
    if (Expected<TemplateParameterList *> ToTPListOrErr =
            import(FromD->getTemplateParameterList(I)))
      ToTPLists[I] = *ToTPListOrErr;
    else
      return ToTPListOrErr.takeError();
  ToD->setTemplateParameterListsInfo(Importer.ToContext, ToTPLists);
  return Error::success();
}

Error ASTNodeImporter::ImportTemplateInformation(
    FunctionDecl *FromFD, FunctionDecl *ToFD) {
  switch (FromFD->getTemplatedKind()) {
  case FunctionDecl::TK_NonTemplate:
  case FunctionDecl::TK_FunctionTemplate:
    return Error::success();

  case FunctionDecl::TK_DependentNonTemplate:
    if (Expected<FunctionDecl *> InstFDOrErr =
            import(FromFD->getInstantiatedFromDecl()))
      ToFD->setInstantiatedFromDecl(*InstFDOrErr);
    return Error::success();
  case FunctionDecl::TK_MemberSpecialization: {
    TemplateSpecializationKind TSK = FromFD->getTemplateSpecializationKind();

    if (Expected<FunctionDecl *> InstFDOrErr =
        import(FromFD->getInstantiatedFromMemberFunction()))
      ToFD->setInstantiationOfMemberFunction(*InstFDOrErr, TSK);
    else
      return InstFDOrErr.takeError();

    if (ExpectedSLoc POIOrErr = import(
        FromFD->getMemberSpecializationInfo()->getPointOfInstantiation()))
      ToFD->getMemberSpecializationInfo()->setPointOfInstantiation(*POIOrErr);
    else
      return POIOrErr.takeError();

    return Error::success();
  }

  case FunctionDecl::TK_FunctionTemplateSpecialization: {
    auto FunctionAndArgsOrErr =
        ImportFunctionTemplateWithTemplateArgsFromSpecialization(FromFD);
    if (!FunctionAndArgsOrErr)
      return FunctionAndArgsOrErr.takeError();

    TemplateArgumentList *ToTAList = TemplateArgumentList::CreateCopy(
          Importer.getToContext(), std::get<1>(*FunctionAndArgsOrErr));

    auto *FTSInfo = FromFD->getTemplateSpecializationInfo();
    TemplateArgumentListInfo ToTAInfo;
    const auto *FromTAArgsAsWritten = FTSInfo->TemplateArgumentsAsWritten;
    if (FromTAArgsAsWritten)
      if (Error Err = ImportTemplateArgumentListInfo(
          *FromTAArgsAsWritten, ToTAInfo))
        return Err;

    ExpectedSLoc POIOrErr = import(FTSInfo->getPointOfInstantiation());
    if (!POIOrErr)
      return POIOrErr.takeError();

    if (Error Err = ImportTemplateParameterLists(FromFD, ToFD))
      return Err;

    TemplateSpecializationKind TSK = FTSInfo->getTemplateSpecializationKind();
    ToFD->setFunctionTemplateSpecialization(
        std::get<0>(*FunctionAndArgsOrErr), ToTAList, /* InsertPos= */ nullptr,
        TSK, FromTAArgsAsWritten ? &ToTAInfo : nullptr, *POIOrErr);
    return Error::success();
  }

  case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
    auto *FromInfo = FromFD->getDependentSpecializationInfo();
    UnresolvedSet<8> Candidates;
    for (FunctionTemplateDecl *FTD : FromInfo->getCandidates()) {
      if (Expected<FunctionTemplateDecl *> ToFTDOrErr = import(FTD))
        Candidates.addDecl(*ToFTDOrErr);
      else
        return ToFTDOrErr.takeError();
    }

    // Import TemplateArgumentListInfo.
    TemplateArgumentListInfo ToTAInfo;
    const auto *FromTAArgsAsWritten = FromInfo->TemplateArgumentsAsWritten;
    if (FromTAArgsAsWritten)
      if (Error Err =
              ImportTemplateArgumentListInfo(*FromTAArgsAsWritten, ToTAInfo))
        return Err;

    ToFD->setDependentTemplateSpecialization(
        Importer.getToContext(), Candidates,
        FromTAArgsAsWritten ? &ToTAInfo : nullptr);
    return Error::success();
  }
  }
  llvm_unreachable("All cases should be covered!");
}

Expected<FunctionDecl *>
ASTNodeImporter::FindFunctionTemplateSpecialization(FunctionDecl *FromFD) {
  auto FunctionAndArgsOrErr =
      ImportFunctionTemplateWithTemplateArgsFromSpecialization(FromFD);
  if (!FunctionAndArgsOrErr)
    return FunctionAndArgsOrErr.takeError();

  FunctionTemplateDecl *Template;
  TemplateArgsTy ToTemplArgs;
  std::tie(Template, ToTemplArgs) = *FunctionAndArgsOrErr;
  void *InsertPos = nullptr;
  auto *FoundSpec = Template->findSpecialization(ToTemplArgs, InsertPos);
  return FoundSpec;
}

Error ASTNodeImporter::ImportFunctionDeclBody(FunctionDecl *FromFD,
                                              FunctionDecl *ToFD) {
  if (Stmt *FromBody = FromFD->getBody()) {
    if (ExpectedStmt ToBodyOrErr = import(FromBody))
      ToFD->setBody(*ToBodyOrErr);
    else
      return ToBodyOrErr.takeError();
  }
  return Error::success();
}

// Returns true if the given D has a DeclContext up to the TranslationUnitDecl
// which is equal to the given DC, or D is equal to DC.
static bool isAncestorDeclContextOf(const DeclContext *DC, const Decl *D) {
  const DeclContext *DCi = dyn_cast<DeclContext>(D);
  if (!DCi)
    DCi = D->getDeclContext();
  assert(DCi && "Declaration should have a context");
  while (DCi != D->getTranslationUnitDecl()) {
    if (DCi == DC)
      return true;
    DCi = DCi->getParent();
  }
  return false;
}

// Check if there is a declaration that has 'DC' as parent context and is
// referenced from statement 'S' or one of its children. The search is done in
// BFS order through children of 'S'.
static bool isAncestorDeclContextOf(const DeclContext *DC, const Stmt *S) {
  SmallVector<const Stmt *> ToProcess;
  ToProcess.push_back(S);
  while (!ToProcess.empty()) {
    const Stmt *CurrentS = ToProcess.pop_back_val();
    ToProcess.append(CurrentS->child_begin(), CurrentS->child_end());
    if (const auto *DeclRef = dyn_cast<DeclRefExpr>(CurrentS)) {
      if (const Decl *D = DeclRef->getDecl())
        if (isAncestorDeclContextOf(DC, D))
          return true;
    } else if (const auto *E =
                   dyn_cast_or_null<SubstNonTypeTemplateParmExpr>(CurrentS)) {
      if (const Decl *D = E->getAssociatedDecl())
        if (isAncestorDeclContextOf(DC, D))
          return true;
    }
  }
  return false;
}

namespace {
/// Check if a type has any reference to a declaration that is inside the body
/// of a function.
/// The \c CheckType(QualType) function should be used to determine
/// this property.
///
/// The type visitor visits one type object only (not recursive).
/// To find all referenced declarations we must discover all type objects until
/// the canonical type is reached (walk over typedef and similar objects). This
/// is done by loop over all "sugar" type objects. For every such type we must
/// check all declarations that are referenced from it. For this check the
/// visitor is used. In the visit functions all referenced declarations except
/// the one that follows in the sugar chain (if any) must be checked. For this
/// check the same visitor is re-used (it has no state-dependent data).
///
/// The visit functions have 3 possible return values:
///  - True, found a declaration inside \c ParentDC.
///  - False, found declarations only outside \c ParentDC and it is not possible
///    to find more declarations (the "sugar" chain does not continue).
///  - Empty optional value, found no declarations or only outside \c ParentDC,
///    but it is possible to find more declarations in the type "sugar" chain.
/// The loop over the "sugar" types can be implemented by using type visit
/// functions only (call \c CheckType with the desugared type). With the current
/// solution no visit function is needed if the type has only a desugared type
/// as data.
class IsTypeDeclaredInsideVisitor
    : public TypeVisitor<IsTypeDeclaredInsideVisitor, std::optional<bool>> {
public:
  IsTypeDeclaredInsideVisitor(const FunctionDecl *ParentDC)
      : ParentDC(ParentDC) {}

  bool CheckType(QualType T) {
    // Check the chain of "sugar" types.
    // The "sugar" types are typedef or similar types that have the same
    // canonical type.
    if (std::optional<bool> Res = Visit(T.getTypePtr()))
      return *Res;
    QualType DsT =
        T.getSingleStepDesugaredType(ParentDC->getParentASTContext());
    while (DsT != T) {
      if (std::optional<bool> Res = Visit(DsT.getTypePtr()))
        return *Res;
      T = DsT;
      DsT = T.getSingleStepDesugaredType(ParentDC->getParentASTContext());
    }
    return false;
  }

  std::optional<bool> VisitTagType(const TagType *T) {
    if (auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(T->getDecl()))
      for (const auto &Arg : Spec->getTemplateArgs().asArray())
        if (checkTemplateArgument(Arg))
          return true;
    return isAncestorDeclContextOf(ParentDC, T->getDecl());
  }

  std::optional<bool> VisitPointerType(const PointerType *T) {
    return CheckType(T->getPointeeType());
  }

  std::optional<bool> VisitReferenceType(const ReferenceType *T) {
    return CheckType(T->getPointeeTypeAsWritten());
  }

  std::optional<bool> VisitTypedefType(const TypedefType *T) {
    const TypedefNameDecl *TD = T->getDecl();
    assert(TD);
    return isAncestorDeclContextOf(ParentDC, TD);
  }

  std::optional<bool> VisitUsingType(const UsingType *T) {
    if (T->getFoundDecl() &&
        isAncestorDeclContextOf(ParentDC, T->getFoundDecl()))
      return true;

    return {};
  }

  std::optional<bool>
  VisitTemplateSpecializationType(const TemplateSpecializationType *T) {
    for (const auto &Arg : T->template_arguments())
      if (checkTemplateArgument(Arg))
        return true;
    // This type is a "sugar" to a record type, it can have a desugared type.
    return {};
  }

  std::optional<bool>
  VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *T) {
    // The "associated declaration" can be the same as ParentDC.
    if (isAncestorDeclContextOf(ParentDC, T->getAssociatedDecl()))
      return true;
    return {};
  }

  std::optional<bool> VisitConstantArrayType(const ConstantArrayType *T) {
    if (T->getSizeExpr() && isAncestorDeclContextOf(ParentDC, T->getSizeExpr()))
      return true;

    return CheckType(T->getElementType());
  }

  std::optional<bool> VisitVariableArrayType(const VariableArrayType *T) {
    llvm_unreachable(
        "Variable array should not occur in deduced return type of a function");
  }

  std::optional<bool> VisitIncompleteArrayType(const IncompleteArrayType *T) {
    llvm_unreachable("Incomplete array should not occur in deduced return type "
                     "of a function");
  }

  std::optional<bool> VisitDependentArrayType(const IncompleteArrayType *T) {
    llvm_unreachable("Dependent array should not occur in deduced return type "
                     "of a function");
  }

private:
  const DeclContext *const ParentDC;

  bool checkTemplateArgument(const TemplateArgument &Arg) {
    switch (Arg.getKind()) {
    case TemplateArgument::Null:
      return false;
    case TemplateArgument::Integral:
      return CheckType(Arg.getIntegralType());
    case TemplateArgument::Type:
      return CheckType(Arg.getAsType());
    case TemplateArgument::Expression:
      return isAncestorDeclContextOf(ParentDC, Arg.getAsExpr());
    case TemplateArgument::Declaration:
      // FIXME: The declaration in this case is not allowed to be in a function?
      return isAncestorDeclContextOf(ParentDC, Arg.getAsDecl());
    case TemplateArgument::NullPtr:
      // FIXME: The type is not allowed to be in the function?
      return CheckType(Arg.getNullPtrType());
    case TemplateArgument::StructuralValue:
      return CheckType(Arg.getStructuralValueType());
    case TemplateArgument::Pack:
      for (const auto &PackArg : Arg.getPackAsArray())
        if (checkTemplateArgument(PackArg))
          return true;
      return false;
    case TemplateArgument::Template:
      // Templates can not be defined locally in functions.
      // A template passed as argument can be not in ParentDC.
      return false;
    case TemplateArgument::TemplateExpansion:
      // Templates can not be defined locally in functions.
      // A template passed as argument can be not in ParentDC.
      return false;
    }
    llvm_unreachable("Unknown TemplateArgument::ArgKind enum");
  };
};
} // namespace

/// This function checks if the given function has a return type that contains
/// a reference (in any way) to a declaration inside the same function.
bool ASTNodeImporter::hasReturnTypeDeclaredInside(FunctionDecl *D) {
  QualType FromTy = D->getType();
  const auto *FromFPT = FromTy->getAs<FunctionProtoType>();
  assert(FromFPT && "Must be called on FunctionProtoType");

  auto IsCXX11LambdaWithouTrailingReturn = [&]() {
    if (Importer.FromContext.getLangOpts().CPlusPlus14) // C++14 or later
      return false;

    if (FromFPT->hasTrailingReturn())
      return false;

    if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
      return cast<CXXRecordDecl>(MD->getDeclContext())->isLambda();

    return false;
  };

  QualType RetT = FromFPT->getReturnType();
  if (isa<AutoType>(RetT.getTypePtr()) || IsCXX11LambdaWithouTrailingReturn()) {
    FunctionDecl *Def = D->getDefinition();
    IsTypeDeclaredInsideVisitor Visitor(Def ? Def : D);
    return Visitor.CheckType(RetT);
  }

  return false;
}

ExplicitSpecifier
ASTNodeImporter::importExplicitSpecifier(Error &Err, ExplicitSpecifier ESpec) {
  Expr *ExplicitExpr = ESpec.getExpr();
  if (ExplicitExpr)
    ExplicitExpr = importChecked(Err, ESpec.getExpr());
  return ExplicitSpecifier(ExplicitExpr, ESpec.getKind());
}

ExpectedDecl ASTNodeImporter::VisitFunctionDecl(FunctionDecl *D) {

  SmallVector<Decl *, 2> Redecls = getCanonicalForwardRedeclChain(D);
  auto RedeclIt = Redecls.begin();
  // Import the first part of the decl chain. I.e. import all previous
  // declarations starting from the canonical decl.
  for (; RedeclIt != Redecls.end() && *RedeclIt != D; ++RedeclIt) {
    ExpectedDecl ToRedeclOrErr = import(*RedeclIt);
    if (!ToRedeclOrErr)
      return ToRedeclOrErr.takeError();
  }
  assert(*RedeclIt == D);

  // Import the major distinguishing characteristics of this function.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  FunctionDecl *FoundByLookup = nullptr;
  FunctionTemplateDecl *FromFT = D->getDescribedFunctionTemplate();

  // If this is a function template specialization, then try to find the same
  // existing specialization in the "to" context. The lookup below will not
  // find any specialization, but would find the primary template; thus, we
  // have to skip normal lookup in case of specializations.
  // FIXME handle member function templates (TK_MemberSpecialization) similarly?
  if (D->getTemplatedKind() ==
      FunctionDecl::TK_FunctionTemplateSpecialization) {
    auto FoundFunctionOrErr = FindFunctionTemplateSpecialization(D);
    if (!FoundFunctionOrErr)
      return FoundFunctionOrErr.takeError();
    if (FunctionDecl *FoundFunction = *FoundFunctionOrErr) {
      if (Decl *Def = FindAndMapDefinition(D, FoundFunction))
        return Def;
      FoundByLookup = FoundFunction;
    }
  }
  // Try to find a function in our own ("to") context with the same name, same
  // type, and in the same context as the function we're importing.
  else if (!LexicalDC->isFunctionOrMethod()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    unsigned IDNS = Decl::IDNS_Ordinary | Decl::IDNS_OrdinaryFriend;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;

      if (auto *FoundFunction = dyn_cast<FunctionDecl>(FoundDecl)) {
        if (!hasSameVisibilityContextAndLinkage(FoundFunction, D))
          continue;

        if (IsStructuralMatch(D, FoundFunction)) {
          if (Decl *Def = FindAndMapDefinition(D, FoundFunction))
            return Def;
          FoundByLookup = FoundFunction;
          break;
        }
        // FIXME: Check for overloading more carefully, e.g., by boosting
        // Sema::IsOverload out to the AST library.

        // Function overloading is okay in C++.
        if (Importer.getToContext().getLangOpts().CPlusPlus)
          continue;

        // Complain about inconsistent function types.
        Importer.ToDiag(Loc, diag::warn_odr_function_type_inconsistent)
            << Name << D->getType() << FoundFunction->getType();
        Importer.ToDiag(FoundFunction->getLocation(), diag::note_odr_value_here)
            << FoundFunction->getType();
        ConflictingDecls.push_back(FoundDecl);
      }
    }

    if (!ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          Name, DC, IDNS, ConflictingDecls.data(), ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  // We do not allow more than one in-class declaration of a function. This is
  // because AST clients like VTableBuilder asserts on this. VTableBuilder
  // assumes there is only one in-class declaration. Building a redecl
  // chain would result in more than one in-class declaration for
  // overrides (even if they are part of the same redecl chain inside the
  // derived class.)
  if (FoundByLookup) {
    if (isa<CXXMethodDecl>(FoundByLookup)) {
      if (D->getLexicalDeclContext() == D->getDeclContext()) {
        if (!D->doesThisDeclarationHaveABody()) {
          if (FunctionTemplateDecl *DescribedD =
                  D->getDescribedFunctionTemplate()) {
            // Handle a "templated" function together with its described
            // template. This avoids need for a similar check at import of the
            // described template.
            assert(FoundByLookup->getDescribedFunctionTemplate() &&
                   "Templated function mapped to non-templated?");
            Importer.MapImported(DescribedD,
                                 FoundByLookup->getDescribedFunctionTemplate());
          }
          return Importer.MapImported(D, FoundByLookup);
        } else {
          // Let's continue and build up the redecl chain in this case.
          // FIXME Merge the functions into one decl.
        }
      }
    }
  }

  DeclarationNameInfo NameInfo(Name, Loc);
  // Import additional name location/type info.
  if (Error Err = ImportDeclarationNameLoc(D->getNameInfo(), NameInfo))
    return std::move(Err);

  QualType FromTy = D->getType();
  TypeSourceInfo *FromTSI = D->getTypeSourceInfo();
  // Set to true if we do not import the type of the function as is. There are
  // cases when the original type would result in an infinite recursion during
  // the import. To avoid an infinite recursion when importing, we create the
  // FunctionDecl with a simplified function type and update it only after the
  // relevant AST nodes are already imported.
  // The type is related to TypeSourceInfo (it references the type), so we must
  // do the same with TypeSourceInfo.
  bool UsedDifferentProtoType = false;
  if (const auto *FromFPT = FromTy->getAs<FunctionProtoType>()) {
    QualType FromReturnTy = FromFPT->getReturnType();
    // Functions with auto return type may define a struct inside their body
    // and the return type could refer to that struct.
    // E.g.: auto foo() { struct X{}; return X(); }
    // To avoid an infinite recursion when importing, create the FunctionDecl
    // with a simplified return type.
    if (hasReturnTypeDeclaredInside(D)) {
      FromReturnTy = Importer.getFromContext().VoidTy;
      UsedDifferentProtoType = true;
    }
    FunctionProtoType::ExtProtoInfo FromEPI = FromFPT->getExtProtoInfo();
    // FunctionProtoType::ExtProtoInfo's ExceptionSpecDecl can point to the
    // FunctionDecl that we are importing the FunctionProtoType for.
    // To avoid an infinite recursion when importing, create the FunctionDecl
    // with a simplified function type.
    if (FromEPI.ExceptionSpec.SourceDecl ||
        FromEPI.ExceptionSpec.SourceTemplate ||
        FromEPI.ExceptionSpec.NoexceptExpr) {
      FunctionProtoType::ExtProtoInfo DefaultEPI;
      FromEPI = DefaultEPI;
      UsedDifferentProtoType = true;
    }
    FromTy = Importer.getFromContext().getFunctionType(
        FromReturnTy, FromFPT->getParamTypes(), FromEPI);
    FromTSI = Importer.getFromContext().getTrivialTypeSourceInfo(
        FromTy, D->getBeginLoc());
  }

  Error Err = Error::success();
  auto T = importChecked(Err, FromTy);
  auto TInfo = importChecked(Err, FromTSI);
  auto ToInnerLocStart = importChecked(Err, D->getInnerLocStart());
  auto ToEndLoc = importChecked(Err, D->getEndLoc());
  auto ToDefaultLoc = importChecked(Err, D->getDefaultLoc());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  auto TrailingRequiresClause =
      importChecked(Err, D->getTrailingRequiresClause());
  if (Err)
    return std::move(Err);

  // Import the function parameters.
  SmallVector<ParmVarDecl *, 8> Parameters;
  for (auto *P : D->parameters()) {
    if (Expected<ParmVarDecl *> ToPOrErr = import(P))
      Parameters.push_back(*ToPOrErr);
    else
      return ToPOrErr.takeError();
  }

  // Create the imported function.
  FunctionDecl *ToFunction = nullptr;
  if (auto *FromConstructor = dyn_cast<CXXConstructorDecl>(D)) {
    ExplicitSpecifier ESpec =
        importExplicitSpecifier(Err, FromConstructor->getExplicitSpecifier());
    if (Err)
      return std::move(Err);
    auto ToInheritedConstructor = InheritedConstructor();
    if (FromConstructor->isInheritingConstructor()) {
      Expected<InheritedConstructor> ImportedInheritedCtor =
          import(FromConstructor->getInheritedConstructor());
      if (!ImportedInheritedCtor)
        return ImportedInheritedCtor.takeError();
      ToInheritedConstructor = *ImportedInheritedCtor;
    }
    if (GetImportedOrCreateDecl<CXXConstructorDecl>(
            ToFunction, D, Importer.getToContext(), cast<CXXRecordDecl>(DC),
            ToInnerLocStart, NameInfo, T, TInfo, ESpec, D->UsesFPIntrin(),
            D->isInlineSpecified(), D->isImplicit(), D->getConstexprKind(),
            ToInheritedConstructor, TrailingRequiresClause))
      return ToFunction;
  } else if (CXXDestructorDecl *FromDtor = dyn_cast<CXXDestructorDecl>(D)) {

    Error Err = Error::success();
    auto ToOperatorDelete = importChecked(
        Err, const_cast<FunctionDecl *>(FromDtor->getOperatorDelete()));
    auto ToThisArg = importChecked(Err, FromDtor->getOperatorDeleteThisArg());
    if (Err)
      return std::move(Err);

    if (GetImportedOrCreateDecl<CXXDestructorDecl>(
            ToFunction, D, Importer.getToContext(), cast<CXXRecordDecl>(DC),
            ToInnerLocStart, NameInfo, T, TInfo, D->UsesFPIntrin(),
            D->isInlineSpecified(), D->isImplicit(), D->getConstexprKind(),
            TrailingRequiresClause))
      return ToFunction;

    CXXDestructorDecl *ToDtor = cast<CXXDestructorDecl>(ToFunction);

    ToDtor->setOperatorDelete(ToOperatorDelete, ToThisArg);
  } else if (CXXConversionDecl *FromConversion =
                 dyn_cast<CXXConversionDecl>(D)) {
    ExplicitSpecifier ESpec =
        importExplicitSpecifier(Err, FromConversion->getExplicitSpecifier());
    if (Err)
      return std::move(Err);
    if (GetImportedOrCreateDecl<CXXConversionDecl>(
            ToFunction, D, Importer.getToContext(), cast<CXXRecordDecl>(DC),
            ToInnerLocStart, NameInfo, T, TInfo, D->UsesFPIntrin(),
            D->isInlineSpecified(), ESpec, D->getConstexprKind(),
            SourceLocation(), TrailingRequiresClause))
      return ToFunction;
  } else if (auto *Method = dyn_cast<CXXMethodDecl>(D)) {
    if (GetImportedOrCreateDecl<CXXMethodDecl>(
            ToFunction, D, Importer.getToContext(), cast<CXXRecordDecl>(DC),
            ToInnerLocStart, NameInfo, T, TInfo, Method->getStorageClass(),
            Method->UsesFPIntrin(), Method->isInlineSpecified(),
            D->getConstexprKind(), SourceLocation(), TrailingRequiresClause))
      return ToFunction;
  } else if (auto *Guide = dyn_cast<CXXDeductionGuideDecl>(D)) {
    ExplicitSpecifier ESpec =
        importExplicitSpecifier(Err, Guide->getExplicitSpecifier());
    CXXConstructorDecl *Ctor =
        importChecked(Err, Guide->getCorrespondingConstructor());
    if (Err)
      return std::move(Err);
    if (GetImportedOrCreateDecl<CXXDeductionGuideDecl>(
            ToFunction, D, Importer.getToContext(), DC, ToInnerLocStart, ESpec,
            NameInfo, T, TInfo, ToEndLoc, Ctor))
      return ToFunction;
    cast<CXXDeductionGuideDecl>(ToFunction)
        ->setDeductionCandidateKind(Guide->getDeductionCandidateKind());
  } else {
    if (GetImportedOrCreateDecl(
            ToFunction, D, Importer.getToContext(), DC, ToInnerLocStart,
            NameInfo, T, TInfo, D->getStorageClass(), D->UsesFPIntrin(),
            D->isInlineSpecified(), D->hasWrittenPrototype(),
            D->getConstexprKind(), TrailingRequiresClause))
      return ToFunction;
  }

  // Connect the redecl chain.
  if (FoundByLookup) {
    auto *Recent = const_cast<FunctionDecl *>(
          FoundByLookup->getMostRecentDecl());
    ToFunction->setPreviousDecl(Recent);
    // FIXME Probably we should merge exception specifications.  E.g. In the
    // "To" context the existing function may have exception specification with
    // noexcept-unevaluated, while the newly imported function may have an
    // evaluated noexcept.  A call to adjustExceptionSpec() on the imported
    // decl and its redeclarations may be required.
  }

  StringLiteral *Msg = D->getDeletedMessage();
  if (Msg) {
    auto Imported = import(Msg);
    if (!Imported)
      return Imported.takeError();
    Msg = *Imported;
  }

  ToFunction->setQualifierInfo(ToQualifierLoc);
  ToFunction->setAccess(D->getAccess());
  ToFunction->setLexicalDeclContext(LexicalDC);
  ToFunction->setVirtualAsWritten(D->isVirtualAsWritten());
  ToFunction->setTrivial(D->isTrivial());
  ToFunction->setIsPureVirtual(D->isPureVirtual());
  ToFunction->setDefaulted(D->isDefaulted());
  ToFunction->setExplicitlyDefaulted(D->isExplicitlyDefaulted());
  ToFunction->setDeletedAsWritten(D->isDeletedAsWritten());
  ToFunction->setFriendConstraintRefersToEnclosingTemplate(
      D->FriendConstraintRefersToEnclosingTemplate());
  ToFunction->setRangeEnd(ToEndLoc);
  ToFunction->setDefaultLoc(ToDefaultLoc);

  if (Msg)
    ToFunction->setDefaultedOrDeletedInfo(
        FunctionDecl::DefaultedOrDeletedFunctionInfo::Create(
            Importer.getToContext(), {}, Msg));

  // Set the parameters.
  for (auto *Param : Parameters) {
    Param->setOwningFunction(ToFunction);
    ToFunction->addDeclInternal(Param);
    if (ASTImporterLookupTable *LT = Importer.SharedState->getLookupTable())
      LT->update(Param, Importer.getToContext().getTranslationUnitDecl());
  }
  ToFunction->setParams(Parameters);

  // We need to complete creation of FunctionProtoTypeLoc manually with setting
  // params it refers to.
  if (TInfo) {
    if (auto ProtoLoc =
        TInfo->getTypeLoc().IgnoreParens().getAs<FunctionProtoTypeLoc>()) {
      for (unsigned I = 0, N = Parameters.size(); I != N; ++I)
        ProtoLoc.setParam(I, Parameters[I]);
    }
  }

  // Import the describing template function, if any.
  if (FromFT) {
    auto ToFTOrErr = import(FromFT);
    if (!ToFTOrErr)
      return ToFTOrErr.takeError();
  }

  // Import Ctor initializers.
  if (auto *FromConstructor = dyn_cast<CXXConstructorDecl>(D)) {
    if (unsigned NumInitializers = FromConstructor->getNumCtorInitializers()) {
      SmallVector<CXXCtorInitializer *, 4> CtorInitializers(NumInitializers);
      // Import first, then allocate memory and copy if there was no error.
      if (Error Err = ImportContainerChecked(
          FromConstructor->inits(), CtorInitializers))
        return std::move(Err);
      auto **Memory =
          new (Importer.getToContext()) CXXCtorInitializer *[NumInitializers];
      std::copy(CtorInitializers.begin(), CtorInitializers.end(), Memory);
      auto *ToCtor = cast<CXXConstructorDecl>(ToFunction);
      ToCtor->setCtorInitializers(Memory);
      ToCtor->setNumCtorInitializers(NumInitializers);
    }
  }

  // If it is a template, import all related things.
  if (Error Err = ImportTemplateInformation(D, ToFunction))
    return std::move(Err);

  if (auto *FromCXXMethod = dyn_cast<CXXMethodDecl>(D))
    if (Error Err = ImportOverriddenMethods(cast<CXXMethodDecl>(ToFunction),
                                            FromCXXMethod))
      return std::move(Err);

  if (D->doesThisDeclarationHaveABody()) {
    Error Err = ImportFunctionDeclBody(D, ToFunction);

    if (Err)
      return std::move(Err);
  }

  // Import and set the original type in case we used another type.
  if (UsedDifferentProtoType) {
    if (ExpectedType TyOrErr = import(D->getType()))
      ToFunction->setType(*TyOrErr);
    else
      return TyOrErr.takeError();
    if (Expected<TypeSourceInfo *> TSIOrErr = import(D->getTypeSourceInfo()))
      ToFunction->setTypeSourceInfo(*TSIOrErr);
    else
      return TSIOrErr.takeError();
  }

  // FIXME: Other bits to merge?

  addDeclToContexts(D, ToFunction);

  // Import the rest of the chain. I.e. import all subsequent declarations.
  for (++RedeclIt; RedeclIt != Redecls.end(); ++RedeclIt) {
    ExpectedDecl ToRedeclOrErr = import(*RedeclIt);
    if (!ToRedeclOrErr)
      return ToRedeclOrErr.takeError();
  }

  return ToFunction;
}

ExpectedDecl ASTNodeImporter::VisitCXXMethodDecl(CXXMethodDecl *D) {
  return VisitFunctionDecl(D);
}

ExpectedDecl ASTNodeImporter::VisitCXXConstructorDecl(CXXConstructorDecl *D) {
  return VisitCXXMethodDecl(D);
}

ExpectedDecl ASTNodeImporter::VisitCXXDestructorDecl(CXXDestructorDecl *D) {
  return VisitCXXMethodDecl(D);
}

ExpectedDecl ASTNodeImporter::VisitCXXConversionDecl(CXXConversionDecl *D) {
  return VisitCXXMethodDecl(D);
}

ExpectedDecl
ASTNodeImporter::VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D) {
  return VisitFunctionDecl(D);
}

ExpectedDecl ASTNodeImporter::VisitFieldDecl(FieldDecl *D) {
  // Import the major distinguishing characteristics of a variable.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Determine whether we've already imported this field.
  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  for (auto *FoundDecl : FoundDecls) {
    if (FieldDecl *FoundField = dyn_cast<FieldDecl>(FoundDecl)) {
      // For anonymous fields, match up by index.
      if (!Name &&
          ASTImporter::getFieldIndex(D) !=
          ASTImporter::getFieldIndex(FoundField))
        continue;

      if (Importer.IsStructurallyEquivalent(D->getType(),
                                            FoundField->getType())) {
        Importer.MapImported(D, FoundField);
        // In case of a FieldDecl of a ClassTemplateSpecializationDecl, the
        // initializer of a FieldDecl might not had been instantiated in the
        // "To" context.  However, the "From" context might instantiated that,
        // thus we have to merge that.
        // Note: `hasInClassInitializer()` is not the same as non-null
        // `getInClassInitializer()` value.
        if (Expr *FromInitializer = D->getInClassInitializer()) {
          if (ExpectedExpr ToInitializerOrErr = import(FromInitializer)) {
            // Import of the FromInitializer may result in the setting of
            // InClassInitializer. If not, set it here.
            assert(FoundField->hasInClassInitializer() &&
                   "Field should have an in-class initializer if it has an "
                   "expression for it.");
            if (!FoundField->getInClassInitializer())
              FoundField->setInClassInitializer(*ToInitializerOrErr);
          } else {
              return ToInitializerOrErr.takeError();
          }
        }
        return FoundField;
      }

      // FIXME: Why is this case not handled with calling HandleNameConflict?
      Importer.ToDiag(Loc, diag::warn_odr_field_type_inconsistent)
        << Name << D->getType() << FoundField->getType();
      Importer.ToDiag(FoundField->getLocation(), diag::note_odr_value_here)
        << FoundField->getType();

      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }
  }

  Error Err = Error::success();
  auto ToType = importChecked(Err, D->getType());
  auto ToTInfo = importChecked(Err, D->getTypeSourceInfo());
  auto ToBitWidth = importChecked(Err, D->getBitWidth());
  auto ToInnerLocStart = importChecked(Err, D->getInnerLocStart());
  if (Err)
    return std::move(Err);
  const Type *ToCapturedVLAType = nullptr;
  if (Error Err = Importer.importInto(
          ToCapturedVLAType, cast_or_null<Type>(D->getCapturedVLAType())))
    return std::move(Err);

  FieldDecl *ToField;
  if (GetImportedOrCreateDecl(ToField, D, Importer.getToContext(), DC,
                              ToInnerLocStart, Loc, Name.getAsIdentifierInfo(),
                              ToType, ToTInfo, ToBitWidth, D->isMutable(),
                              D->getInClassInitStyle()))
    return ToField;

  // We need [[no_unqiue_address]] attributes to be added to FieldDecl, before
  // we add fields in CXXRecordDecl::addedMember, otherwise record will be
  // marked as having non-zero size.
  Err = Importer.ImportAttrs(ToField, D);
  if (Err)
    return std::move(Err);
  ToField->setAccess(D->getAccess());
  ToField->setLexicalDeclContext(LexicalDC);
  ToField->setImplicit(D->isImplicit());
  if (ToCapturedVLAType)
    ToField->setCapturedVLAType(cast<VariableArrayType>(ToCapturedVLAType));
  LexicalDC->addDeclInternal(ToField);
  // Import initializer only after the field was created, it may have recursive
  // reference to the field.
  auto ToInitializer = importChecked(Err, D->getInClassInitializer());
  if (Err)
    return std::move(Err);
  if (ToInitializer) {
    auto *AlreadyImported = ToField->getInClassInitializer();
    if (AlreadyImported)
      assert(ToInitializer == AlreadyImported &&
             "Duplicate import of in-class initializer.");
    else
      ToField->setInClassInitializer(ToInitializer);
  }

  return ToField;
}

ExpectedDecl ASTNodeImporter::VisitIndirectFieldDecl(IndirectFieldDecl *D) {
  // Import the major distinguishing characteristics of a variable.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Determine whether we've already imported this field.
  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  for (unsigned I = 0, N = FoundDecls.size(); I != N; ++I) {
    if (auto *FoundField = dyn_cast<IndirectFieldDecl>(FoundDecls[I])) {
      // For anonymous indirect fields, match up by index.
      if (!Name &&
          ASTImporter::getFieldIndex(D) !=
          ASTImporter::getFieldIndex(FoundField))
        continue;

      if (Importer.IsStructurallyEquivalent(D->getType(),
                                            FoundField->getType(),
                                            !Name.isEmpty())) {
        Importer.MapImported(D, FoundField);
        return FoundField;
      }

      // If there are more anonymous fields to check, continue.
      if (!Name && I < N-1)
        continue;

      // FIXME: Why is this case not handled with calling HandleNameConflict?
      Importer.ToDiag(Loc, diag::warn_odr_field_type_inconsistent)
        << Name << D->getType() << FoundField->getType();
      Importer.ToDiag(FoundField->getLocation(), diag::note_odr_value_here)
        << FoundField->getType();

      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }
  }

  // Import the type.
  auto TypeOrErr = import(D->getType());
  if (!TypeOrErr)
    return TypeOrErr.takeError();

  auto **NamedChain =
    new (Importer.getToContext()) NamedDecl*[D->getChainingSize()];

  unsigned i = 0;
  for (auto *PI : D->chain())
    if (Expected<NamedDecl *> ToD = import(PI))
      NamedChain[i++] = *ToD;
    else
      return ToD.takeError();

  llvm::MutableArrayRef<NamedDecl *> CH = {NamedChain, D->getChainingSize()};
  IndirectFieldDecl *ToIndirectField;
  if (GetImportedOrCreateDecl(ToIndirectField, D, Importer.getToContext(), DC,
                              Loc, Name.getAsIdentifierInfo(), *TypeOrErr, CH))
    // FIXME here we leak `NamedChain` which is allocated before
    return ToIndirectField;

  ToIndirectField->setAccess(D->getAccess());
  ToIndirectField->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToIndirectField);
  return ToIndirectField;
}

/// Used as return type of getFriendCountAndPosition.
struct FriendCountAndPosition {
  /// Number of similar looking friends.
  unsigned int TotalCount;
  /// Index of the specific FriendDecl.
  unsigned int IndexOfDecl;
};

static bool IsEquivalentFriend(ASTImporter &Importer, FriendDecl *FD1,
                               FriendDecl *FD2) {
  if ((!FD1->getFriendType()) != (!FD2->getFriendType()))
    return false;

  if (const TypeSourceInfo *TSI = FD1->getFriendType())
    return Importer.IsStructurallyEquivalent(
        TSI->getType(), FD2->getFriendType()->getType(), /*Complain=*/false);

  ASTImporter::NonEquivalentDeclSet NonEquivalentDecls;
  StructuralEquivalenceContext Ctx(
      FD1->getASTContext(), FD2->getASTContext(), NonEquivalentDecls,
      StructuralEquivalenceKind::Default,
      /* StrictTypeSpelling = */ false, /* Complain = */ false);
  return Ctx.IsEquivalent(FD1, FD2);
}

static FriendCountAndPosition getFriendCountAndPosition(ASTImporter &Importer,
                                                        FriendDecl *FD) {
  unsigned int FriendCount = 0;
  std::optional<unsigned int> FriendPosition;
  const auto *RD = cast<CXXRecordDecl>(FD->getLexicalDeclContext());

  for (FriendDecl *FoundFriend : RD->friends()) {
    if (FoundFriend == FD) {
      FriendPosition = FriendCount;
      ++FriendCount;
    } else if (IsEquivalentFriend(Importer, FD, FoundFriend)) {
      ++FriendCount;
    }
  }

  assert(FriendPosition && "Friend decl not found in own parent.");

  return {FriendCount, *FriendPosition};
}

ExpectedDecl ASTNodeImporter::VisitFriendDecl(FriendDecl *D) {
  // Import the major distinguishing characteristics of a declaration.
  DeclContext *DC, *LexicalDC;
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return std::move(Err);

  // Determine whether we've already imported this decl.
  // FriendDecl is not a NamedDecl so we cannot use lookup.
  // We try to maintain order and count of redundant friend declarations.
  const auto *RD = cast<CXXRecordDecl>(DC);
  SmallVector<FriendDecl *, 2> ImportedEquivalentFriends;
  for (FriendDecl *ImportedFriend : RD->friends())
    if (IsEquivalentFriend(Importer, D, ImportedFriend))
      ImportedEquivalentFriends.push_back(ImportedFriend);

  FriendCountAndPosition CountAndPosition =
      getFriendCountAndPosition(Importer, D);

  assert(ImportedEquivalentFriends.size() <= CountAndPosition.TotalCount &&
         "Class with non-matching friends is imported, ODR check wrong?");
  if (ImportedEquivalentFriends.size() == CountAndPosition.TotalCount)
    return Importer.MapImported(
        D, ImportedEquivalentFriends[CountAndPosition.IndexOfDecl]);

  // Not found. Create it.
  // The declarations will be put into order later by ImportDeclContext.
  FriendDecl::FriendUnion ToFU;
  if (NamedDecl *FriendD = D->getFriendDecl()) {
    NamedDecl *ToFriendD;
    if (Error Err = importInto(ToFriendD, FriendD))
      return std::move(Err);

    if (FriendD->getFriendObjectKind() != Decl::FOK_None &&
        !(FriendD->isInIdentifierNamespace(Decl::IDNS_NonMemberOperator)))
      ToFriendD->setObjectOfFriendDecl(false);

    ToFU = ToFriendD;
  } else { // The friend is a type, not a decl.
    if (auto TSIOrErr = import(D->getFriendType()))
      ToFU = *TSIOrErr;
    else
      return TSIOrErr.takeError();
  }

  SmallVector<TemplateParameterList *, 1> ToTPLists(D->NumTPLists);
  auto **FromTPLists = D->getTrailingObjects<TemplateParameterList *>();
  for (unsigned I = 0; I < D->NumTPLists; I++) {
    if (auto ListOrErr = import(FromTPLists[I]))
      ToTPLists[I] = *ListOrErr;
    else
      return ListOrErr.takeError();
  }

  auto LocationOrErr = import(D->getLocation());
  if (!LocationOrErr)
    return LocationOrErr.takeError();
  auto FriendLocOrErr = import(D->getFriendLoc());
  if (!FriendLocOrErr)
    return FriendLocOrErr.takeError();

  FriendDecl *FrD;
  if (GetImportedOrCreateDecl(FrD, D, Importer.getToContext(), DC,
                              *LocationOrErr, ToFU,
                              *FriendLocOrErr, ToTPLists))
    return FrD;

  FrD->setAccess(D->getAccess());
  FrD->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(FrD);
  return FrD;
}

ExpectedDecl ASTNodeImporter::VisitObjCIvarDecl(ObjCIvarDecl *D) {
  // Import the major distinguishing characteristics of an ivar.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Determine whether we've already imported this ivar
  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  for (auto *FoundDecl : FoundDecls) {
    if (ObjCIvarDecl *FoundIvar = dyn_cast<ObjCIvarDecl>(FoundDecl)) {
      if (Importer.IsStructurallyEquivalent(D->getType(),
                                            FoundIvar->getType())) {
        Importer.MapImported(D, FoundIvar);
        return FoundIvar;
      }

      Importer.ToDiag(Loc, diag::warn_odr_ivar_type_inconsistent)
        << Name << D->getType() << FoundIvar->getType();
      Importer.ToDiag(FoundIvar->getLocation(), diag::note_odr_value_here)
        << FoundIvar->getType();

      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }
  }

  Error Err = Error::success();
  auto ToType = importChecked(Err, D->getType());
  auto ToTypeSourceInfo = importChecked(Err, D->getTypeSourceInfo());
  auto ToBitWidth = importChecked(Err, D->getBitWidth());
  auto ToInnerLocStart = importChecked(Err, D->getInnerLocStart());
  if (Err)
    return std::move(Err);

  ObjCIvarDecl *ToIvar;
  if (GetImportedOrCreateDecl(
          ToIvar, D, Importer.getToContext(), cast<ObjCContainerDecl>(DC),
          ToInnerLocStart, Loc, Name.getAsIdentifierInfo(),
          ToType, ToTypeSourceInfo,
          D->getAccessControl(),ToBitWidth, D->getSynthesize()))
    return ToIvar;

  ToIvar->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToIvar);
  return ToIvar;
}

ExpectedDecl ASTNodeImporter::VisitVarDecl(VarDecl *D) {

  SmallVector<Decl*, 2> Redecls = getCanonicalForwardRedeclChain(D);
  auto RedeclIt = Redecls.begin();
  // Import the first part of the decl chain. I.e. import all previous
  // declarations starting from the canonical decl.
  for (; RedeclIt != Redecls.end() && *RedeclIt != D; ++RedeclIt) {
    ExpectedDecl RedeclOrErr = import(*RedeclIt);
    if (!RedeclOrErr)
      return RedeclOrErr.takeError();
  }
  assert(*RedeclIt == D);

  // Import the major distinguishing characteristics of a variable.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Try to find a variable in our own ("to") context with the same name and
  // in the same context as the variable we're importing.
  VarDecl *FoundByLookup = nullptr;
  if (D->isFileVarDecl()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    unsigned IDNS = Decl::IDNS_Ordinary;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;

      if (auto *FoundVar = dyn_cast<VarDecl>(FoundDecl)) {
        if (!hasSameVisibilityContextAndLinkage(FoundVar, D))
          continue;
        if (Importer.IsStructurallyEquivalent(D->getType(),
                                              FoundVar->getType())) {

          // The VarDecl in the "From" context has a definition, but in the
          // "To" context we already have a definition.
          VarDecl *FoundDef = FoundVar->getDefinition();
          if (D->isThisDeclarationADefinition() && FoundDef)
            // FIXME Check for ODR error if the two definitions have
            // different initializers?
            return Importer.MapImported(D, FoundDef);

          // The VarDecl in the "From" context has an initializer, but in the
          // "To" context we already have an initializer.
          const VarDecl *FoundDInit = nullptr;
          if (D->getInit() && FoundVar->getAnyInitializer(FoundDInit))
            // FIXME Diagnose ODR error if the two initializers are different?
            return Importer.MapImported(D, const_cast<VarDecl*>(FoundDInit));

          FoundByLookup = FoundVar;
          break;
        }

        const ArrayType *FoundArray
          = Importer.getToContext().getAsArrayType(FoundVar->getType());
        const ArrayType *TArray
          = Importer.getToContext().getAsArrayType(D->getType());
        if (FoundArray && TArray) {
          if (isa<IncompleteArrayType>(FoundArray) &&
              isa<ConstantArrayType>(TArray)) {
            // Import the type.
            if (auto TyOrErr = import(D->getType()))
              FoundVar->setType(*TyOrErr);
            else
              return TyOrErr.takeError();

            FoundByLookup = FoundVar;
            break;
          } else if (isa<IncompleteArrayType>(TArray) &&
                     isa<ConstantArrayType>(FoundArray)) {
            FoundByLookup = FoundVar;
            break;
          }
        }

        Importer.ToDiag(Loc, diag::warn_odr_variable_type_inconsistent)
          << Name << D->getType() << FoundVar->getType();
        Importer.ToDiag(FoundVar->getLocation(), diag::note_odr_value_here)
          << FoundVar->getType();
        ConflictingDecls.push_back(FoundDecl);
      }
    }

    if (!ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          Name, DC, IDNS, ConflictingDecls.data(), ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  Error Err = Error::success();
  auto ToType = importChecked(Err, D->getType());
  auto ToTypeSourceInfo = importChecked(Err, D->getTypeSourceInfo());
  auto ToInnerLocStart = importChecked(Err, D->getInnerLocStart());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  if (Err)
    return std::move(Err);

  VarDecl *ToVar;
  if (auto *FromDecomp = dyn_cast<DecompositionDecl>(D)) {
    SmallVector<BindingDecl *> Bindings(FromDecomp->bindings().size());
    if (Error Err =
            ImportArrayChecked(FromDecomp->bindings(), Bindings.begin()))
      return std::move(Err);
    DecompositionDecl *ToDecomp;
    if (GetImportedOrCreateDecl(
            ToDecomp, FromDecomp, Importer.getToContext(), DC, ToInnerLocStart,
            Loc, ToType, ToTypeSourceInfo, D->getStorageClass(), Bindings))
      return ToDecomp;
    ToVar = ToDecomp;
  } else {
    // Create the imported variable.
    if (GetImportedOrCreateDecl(ToVar, D, Importer.getToContext(), DC,
                                ToInnerLocStart, Loc,
                                Name.getAsIdentifierInfo(), ToType,
                                ToTypeSourceInfo, D->getStorageClass()))
      return ToVar;
  }

  ToVar->setTSCSpec(D->getTSCSpec());
  ToVar->setQualifierInfo(ToQualifierLoc);
  ToVar->setAccess(D->getAccess());
  ToVar->setLexicalDeclContext(LexicalDC);
  if (D->isInlineSpecified())
    ToVar->setInlineSpecified();
  if (D->isInline())
    ToVar->setImplicitlyInline();

  if (FoundByLookup) {
    auto *Recent = const_cast<VarDecl *>(FoundByLookup->getMostRecentDecl());
    ToVar->setPreviousDecl(Recent);
  }

  // Import the described template, if any.
  if (D->getDescribedVarTemplate()) {
    auto ToVTOrErr = import(D->getDescribedVarTemplate());
    if (!ToVTOrErr)
      return ToVTOrErr.takeError();
  } else if (MemberSpecializationInfo *MSI = D->getMemberSpecializationInfo()) {
    TemplateSpecializationKind SK = MSI->getTemplateSpecializationKind();
    VarDecl *FromInst = D->getInstantiatedFromStaticDataMember();
    if (Expected<VarDecl *> ToInstOrErr = import(FromInst))
      ToVar->setInstantiationOfStaticDataMember(*ToInstOrErr, SK);
    else
      return ToInstOrErr.takeError();
    if (ExpectedSLoc POIOrErr = import(MSI->getPointOfInstantiation()))
      ToVar->getMemberSpecializationInfo()->setPointOfInstantiation(*POIOrErr);
    else
      return POIOrErr.takeError();
  }

  if (Error Err = ImportInitializer(D, ToVar))
    return std::move(Err);

  if (D->isConstexpr())
    ToVar->setConstexpr(true);

  addDeclToContexts(D, ToVar);

  // Import the rest of the chain. I.e. import all subsequent declarations.
  for (++RedeclIt; RedeclIt != Redecls.end(); ++RedeclIt) {
    ExpectedDecl RedeclOrErr = import(*RedeclIt);
    if (!RedeclOrErr)
      return RedeclOrErr.takeError();
  }

  return ToVar;
}

ExpectedDecl ASTNodeImporter::VisitImplicitParamDecl(ImplicitParamDecl *D) {
  // Parameters are created in the translation unit's context, then moved
  // into the function declaration's context afterward.
  DeclContext *DC = Importer.getToContext().getTranslationUnitDecl();

  Error Err = Error::success();
  auto ToDeclName = importChecked(Err, D->getDeclName());
  auto ToLocation = importChecked(Err, D->getLocation());
  auto ToType = importChecked(Err, D->getType());
  if (Err)
    return std::move(Err);

  // Create the imported parameter.
  ImplicitParamDecl *ToParm = nullptr;
  if (GetImportedOrCreateDecl(ToParm, D, Importer.getToContext(), DC,
                              ToLocation, ToDeclName.getAsIdentifierInfo(),
                              ToType, D->getParameterKind()))
    return ToParm;
  return ToParm;
}

Error ASTNodeImporter::ImportDefaultArgOfParmVarDecl(
    const ParmVarDecl *FromParam, ParmVarDecl *ToParam) {
  ToParam->setHasInheritedDefaultArg(FromParam->hasInheritedDefaultArg());
  ToParam->setExplicitObjectParameterLoc(
      FromParam->getExplicitObjectParamThisLoc());
  ToParam->setKNRPromoted(FromParam->isKNRPromoted());

  if (FromParam->hasUninstantiatedDefaultArg()) {
    if (auto ToDefArgOrErr = import(FromParam->getUninstantiatedDefaultArg()))
      ToParam->setUninstantiatedDefaultArg(*ToDefArgOrErr);
    else
      return ToDefArgOrErr.takeError();
  } else if (FromParam->hasUnparsedDefaultArg()) {
    ToParam->setUnparsedDefaultArg();
  } else if (FromParam->hasDefaultArg()) {
    if (auto ToDefArgOrErr = import(FromParam->getDefaultArg()))
      ToParam->setDefaultArg(*ToDefArgOrErr);
    else
      return ToDefArgOrErr.takeError();
  }

  return Error::success();
}

Expected<InheritedConstructor>
ASTNodeImporter::ImportInheritedConstructor(const InheritedConstructor &From) {
  Error Err = Error::success();
  CXXConstructorDecl *ToBaseCtor = importChecked(Err, From.getConstructor());
  ConstructorUsingShadowDecl *ToShadow =
      importChecked(Err, From.getShadowDecl());
  if (Err)
    return std::move(Err);
  return InheritedConstructor(ToShadow, ToBaseCtor);
}

ExpectedDecl ASTNodeImporter::VisitParmVarDecl(ParmVarDecl *D) {
  // Parameters are created in the translation unit's context, then moved
  // into the function declaration's context afterward.
  DeclContext *DC = Importer.getToContext().getTranslationUnitDecl();

  Error Err = Error::success();
  auto ToDeclName = importChecked(Err, D->getDeclName());
  auto ToLocation = importChecked(Err, D->getLocation());
  auto ToInnerLocStart = importChecked(Err, D->getInnerLocStart());
  auto ToType = importChecked(Err, D->getType());
  auto ToTypeSourceInfo = importChecked(Err, D->getTypeSourceInfo());
  if (Err)
    return std::move(Err);

  ParmVarDecl *ToParm;
  if (GetImportedOrCreateDecl(ToParm, D, Importer.getToContext(), DC,
                              ToInnerLocStart, ToLocation,
                              ToDeclName.getAsIdentifierInfo(), ToType,
                              ToTypeSourceInfo, D->getStorageClass(),
                              /*DefaultArg*/ nullptr))
    return ToParm;

  // Set the default argument. It should be no problem if it was already done.
  // Do not import the default expression before GetImportedOrCreateDecl call
  // to avoid possible infinite import loop because circular dependency.
  if (Error Err = ImportDefaultArgOfParmVarDecl(D, ToParm))
    return std::move(Err);

  if (D->isObjCMethodParameter()) {
    ToParm->setObjCMethodScopeInfo(D->getFunctionScopeIndex());
    ToParm->setObjCDeclQualifier(D->getObjCDeclQualifier());
  } else {
    ToParm->setScopeInfo(D->getFunctionScopeDepth(),
                         D->getFunctionScopeIndex());
  }

  return ToParm;
}

ExpectedDecl ASTNodeImporter::VisitObjCMethodDecl(ObjCMethodDecl *D) {
  // Import the major distinguishing characteristics of a method.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  for (auto *FoundDecl : FoundDecls) {
    if (auto *FoundMethod = dyn_cast<ObjCMethodDecl>(FoundDecl)) {
      if (FoundMethod->isInstanceMethod() != D->isInstanceMethod())
        continue;

      // Check return types.
      if (!Importer.IsStructurallyEquivalent(D->getReturnType(),
                                             FoundMethod->getReturnType())) {
        Importer.ToDiag(Loc, diag::warn_odr_objc_method_result_type_inconsistent)
            << D->isInstanceMethod() << Name << D->getReturnType()
            << FoundMethod->getReturnType();
        Importer.ToDiag(FoundMethod->getLocation(),
                        diag::note_odr_objc_method_here)
          << D->isInstanceMethod() << Name;

        return make_error<ASTImportError>(ASTImportError::NameConflict);
      }

      // Check the number of parameters.
      if (D->param_size() != FoundMethod->param_size()) {
        Importer.ToDiag(Loc, diag::warn_odr_objc_method_num_params_inconsistent)
          << D->isInstanceMethod() << Name
          << D->param_size() << FoundMethod->param_size();
        Importer.ToDiag(FoundMethod->getLocation(),
                        diag::note_odr_objc_method_here)
          << D->isInstanceMethod() << Name;

        return make_error<ASTImportError>(ASTImportError::NameConflict);
      }

      // Check parameter types.
      for (ObjCMethodDecl::param_iterator P = D->param_begin(),
             PEnd = D->param_end(), FoundP = FoundMethod->param_begin();
           P != PEnd; ++P, ++FoundP) {
        if (!Importer.IsStructurallyEquivalent((*P)->getType(),
                                               (*FoundP)->getType())) {
          Importer.FromDiag((*P)->getLocation(),
                            diag::warn_odr_objc_method_param_type_inconsistent)
            << D->isInstanceMethod() << Name
            << (*P)->getType() << (*FoundP)->getType();
          Importer.ToDiag((*FoundP)->getLocation(), diag::note_odr_value_here)
            << (*FoundP)->getType();

          return make_error<ASTImportError>(ASTImportError::NameConflict);
        }
      }

      // Check variadic/non-variadic.
      // Check the number of parameters.
      if (D->isVariadic() != FoundMethod->isVariadic()) {
        Importer.ToDiag(Loc, diag::warn_odr_objc_method_variadic_inconsistent)
          << D->isInstanceMethod() << Name;
        Importer.ToDiag(FoundMethod->getLocation(),
                        diag::note_odr_objc_method_here)
          << D->isInstanceMethod() << Name;

        return make_error<ASTImportError>(ASTImportError::NameConflict);
      }

      // FIXME: Any other bits we need to merge?
      return Importer.MapImported(D, FoundMethod);
    }
  }

  Error Err = Error::success();
  auto ToEndLoc = importChecked(Err, D->getEndLoc());
  auto ToReturnType = importChecked(Err, D->getReturnType());
  auto ToReturnTypeSourceInfo =
      importChecked(Err, D->getReturnTypeSourceInfo());
  if (Err)
    return std::move(Err);

  ObjCMethodDecl *ToMethod;
  if (GetImportedOrCreateDecl(
          ToMethod, D, Importer.getToContext(), Loc, ToEndLoc,
          Name.getObjCSelector(), ToReturnType, ToReturnTypeSourceInfo, DC,
          D->isInstanceMethod(), D->isVariadic(), D->isPropertyAccessor(),
          D->isSynthesizedAccessorStub(), D->isImplicit(), D->isDefined(),
          D->getImplementationControl(), D->hasRelatedResultType()))
    return ToMethod;

  // FIXME: When we decide to merge method definitions, we'll need to
  // deal with implicit parameters.

  // Import the parameters
  SmallVector<ParmVarDecl *, 5> ToParams;
  for (auto *FromP : D->parameters()) {
    if (Expected<ParmVarDecl *> ToPOrErr = import(FromP))
      ToParams.push_back(*ToPOrErr);
    else
      return ToPOrErr.takeError();
  }

  // Set the parameters.
  for (auto *ToParam : ToParams) {
    ToParam->setOwningFunction(ToMethod);
    ToMethod->addDeclInternal(ToParam);
  }

  SmallVector<SourceLocation, 12> FromSelLocs;
  D->getSelectorLocs(FromSelLocs);
  SmallVector<SourceLocation, 12> ToSelLocs(FromSelLocs.size());
  if (Error Err = ImportContainerChecked(FromSelLocs, ToSelLocs))
    return std::move(Err);

  ToMethod->setMethodParams(Importer.getToContext(), ToParams, ToSelLocs);

  ToMethod->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToMethod);

  // Implicit params are declared when Sema encounters the definition but this
  // never happens when the method is imported. Manually declare the implicit
  // params now that the MethodDecl knows its class interface.
  if (D->getSelfDecl())
    ToMethod->createImplicitParams(Importer.getToContext(),
                                   ToMethod->getClassInterface());

  return ToMethod;
}

ExpectedDecl ASTNodeImporter::VisitObjCTypeParamDecl(ObjCTypeParamDecl *D) {
  // Import the major distinguishing characteristics of a category.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  Error Err = Error::success();
  auto ToVarianceLoc = importChecked(Err, D->getVarianceLoc());
  auto ToLocation = importChecked(Err, D->getLocation());
  auto ToColonLoc = importChecked(Err, D->getColonLoc());
  auto ToTypeSourceInfo = importChecked(Err, D->getTypeSourceInfo());
  if (Err)
    return std::move(Err);

  ObjCTypeParamDecl *Result;
  if (GetImportedOrCreateDecl(
          Result, D, Importer.getToContext(), DC, D->getVariance(),
          ToVarianceLoc, D->getIndex(),
          ToLocation, Name.getAsIdentifierInfo(),
          ToColonLoc, ToTypeSourceInfo))
    return Result;

  // Only import 'ObjCTypeParamType' after the decl is created.
  auto ToTypeForDecl = importChecked(Err, D->getTypeForDecl());
  if (Err)
    return std::move(Err);
  Result->setTypeForDecl(ToTypeForDecl);
  Result->setLexicalDeclContext(LexicalDC);
  return Result;
}

ExpectedDecl ASTNodeImporter::VisitObjCCategoryDecl(ObjCCategoryDecl *D) {
  // Import the major distinguishing characteristics of a category.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  ObjCInterfaceDecl *ToInterface;
  if (Error Err = importInto(ToInterface, D->getClassInterface()))
    return std::move(Err);

  // Determine if we've already encountered this category.
  ObjCCategoryDecl *MergeWithCategory
    = ToInterface->FindCategoryDeclaration(Name.getAsIdentifierInfo());
  ObjCCategoryDecl *ToCategory = MergeWithCategory;
  if (!ToCategory) {

    Error Err = Error::success();
    auto ToAtStartLoc = importChecked(Err, D->getAtStartLoc());
    auto ToCategoryNameLoc = importChecked(Err, D->getCategoryNameLoc());
    auto ToIvarLBraceLoc = importChecked(Err, D->getIvarLBraceLoc());
    auto ToIvarRBraceLoc = importChecked(Err, D->getIvarRBraceLoc());
    if (Err)
      return std::move(Err);

    if (GetImportedOrCreateDecl(ToCategory, D, Importer.getToContext(), DC,
                                ToAtStartLoc, Loc,
                                ToCategoryNameLoc,
                                Name.getAsIdentifierInfo(), ToInterface,
                                /*TypeParamList=*/nullptr,
                                ToIvarLBraceLoc,
                                ToIvarRBraceLoc))
      return ToCategory;

    ToCategory->setLexicalDeclContext(LexicalDC);
    LexicalDC->addDeclInternal(ToCategory);
    // Import the type parameter list after MapImported, to avoid
    // loops when bringing in their DeclContext.
    if (auto PListOrErr = ImportObjCTypeParamList(D->getTypeParamList()))
      ToCategory->setTypeParamList(*PListOrErr);
    else
      return PListOrErr.takeError();

    // Import protocols
    SmallVector<ObjCProtocolDecl *, 4> Protocols;
    SmallVector<SourceLocation, 4> ProtocolLocs;
    ObjCCategoryDecl::protocol_loc_iterator FromProtoLoc
      = D->protocol_loc_begin();
    for (ObjCCategoryDecl::protocol_iterator FromProto = D->protocol_begin(),
                                          FromProtoEnd = D->protocol_end();
         FromProto != FromProtoEnd;
         ++FromProto, ++FromProtoLoc) {
      if (Expected<ObjCProtocolDecl *> ToProtoOrErr = import(*FromProto))
        Protocols.push_back(*ToProtoOrErr);
      else
        return ToProtoOrErr.takeError();

      if (ExpectedSLoc ToProtoLocOrErr = import(*FromProtoLoc))
        ProtocolLocs.push_back(*ToProtoLocOrErr);
      else
        return ToProtoLocOrErr.takeError();
    }

    // FIXME: If we're merging, make sure that the protocol list is the same.
    ToCategory->setProtocolList(Protocols.data(), Protocols.size(),
                                ProtocolLocs.data(), Importer.getToContext());

  } else {
    Importer.MapImported(D, ToCategory);
  }

  // Import all of the members of this category.
  if (Error Err = ImportDeclContext(D))
    return std::move(Err);

  // If we have an implementation, import it as well.
  if (D->getImplementation()) {
    if (Expected<ObjCCategoryImplDecl *> ToImplOrErr =
        import(D->getImplementation()))
      ToCategory->setImplementation(*ToImplOrErr);
    else
      return ToImplOrErr.takeError();
  }

  return ToCategory;
}

Error ASTNodeImporter::ImportDefinition(
    ObjCProtocolDecl *From, ObjCProtocolDecl *To, ImportDefinitionKind Kind) {
  if (To->getDefinition()) {
    if (shouldForceImportDeclContext(Kind))
      if (Error Err = ImportDeclContext(From))
        return Err;
    return Error::success();
  }

  // Start the protocol definition
  To->startDefinition();

  // Import protocols
  SmallVector<ObjCProtocolDecl *, 4> Protocols;
  SmallVector<SourceLocation, 4> ProtocolLocs;
  ObjCProtocolDecl::protocol_loc_iterator FromProtoLoc =
      From->protocol_loc_begin();
  for (ObjCProtocolDecl::protocol_iterator FromProto = From->protocol_begin(),
                                        FromProtoEnd = From->protocol_end();
       FromProto != FromProtoEnd;
       ++FromProto, ++FromProtoLoc) {
    if (Expected<ObjCProtocolDecl *> ToProtoOrErr = import(*FromProto))
      Protocols.push_back(*ToProtoOrErr);
    else
      return ToProtoOrErr.takeError();

    if (ExpectedSLoc ToProtoLocOrErr = import(*FromProtoLoc))
      ProtocolLocs.push_back(*ToProtoLocOrErr);
    else
      return ToProtoLocOrErr.takeError();

  }

  // FIXME: If we're merging, make sure that the protocol list is the same.
  To->setProtocolList(Protocols.data(), Protocols.size(),
                      ProtocolLocs.data(), Importer.getToContext());

  if (shouldForceImportDeclContext(Kind)) {
    // Import all of the members of this protocol.
    if (Error Err = ImportDeclContext(From, /*ForceImport=*/true))
      return Err;
  }
  return Error::success();
}

ExpectedDecl ASTNodeImporter::VisitObjCProtocolDecl(ObjCProtocolDecl *D) {
  // If this protocol has a definition in the translation unit we're coming
  // from, but this particular declaration is not that definition, import the
  // definition and map to that.
  ObjCProtocolDecl *Definition = D->getDefinition();
  if (Definition && Definition != D) {
    if (ExpectedDecl ImportedDefOrErr = import(Definition))
      return Importer.MapImported(D, *ImportedDefOrErr);
    else
      return ImportedDefOrErr.takeError();
  }

  // Import the major distinguishing characteristics of a protocol.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  ObjCProtocolDecl *MergeWithProtocol = nullptr;
  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  for (auto *FoundDecl : FoundDecls) {
    if (!FoundDecl->isInIdentifierNamespace(Decl::IDNS_ObjCProtocol))
      continue;

    if ((MergeWithProtocol = dyn_cast<ObjCProtocolDecl>(FoundDecl)))
      break;
  }

  ObjCProtocolDecl *ToProto = MergeWithProtocol;
  if (!ToProto) {
    auto ToAtBeginLocOrErr = import(D->getAtStartLoc());
    if (!ToAtBeginLocOrErr)
      return ToAtBeginLocOrErr.takeError();

    if (GetImportedOrCreateDecl(ToProto, D, Importer.getToContext(), DC,
                                Name.getAsIdentifierInfo(), Loc,
                                *ToAtBeginLocOrErr,
                                /*PrevDecl=*/nullptr))
      return ToProto;
    ToProto->setLexicalDeclContext(LexicalDC);
    LexicalDC->addDeclInternal(ToProto);
  }

  Importer.MapImported(D, ToProto);

  if (D->isThisDeclarationADefinition())
    if (Error Err = ImportDefinition(D, ToProto))
      return std::move(Err);

  return ToProto;
}

ExpectedDecl ASTNodeImporter::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  DeclContext *DC, *LexicalDC;
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return std::move(Err);

  ExpectedSLoc ExternLocOrErr = import(D->getExternLoc());
  if (!ExternLocOrErr)
    return ExternLocOrErr.takeError();

  ExpectedSLoc LangLocOrErr = import(D->getLocation());
  if (!LangLocOrErr)
    return LangLocOrErr.takeError();

  bool HasBraces = D->hasBraces();

  LinkageSpecDecl *ToLinkageSpec;
  if (GetImportedOrCreateDecl(ToLinkageSpec, D, Importer.getToContext(), DC,
                              *ExternLocOrErr, *LangLocOrErr,
                              D->getLanguage(), HasBraces))
    return ToLinkageSpec;

  if (HasBraces) {
    ExpectedSLoc RBraceLocOrErr = import(D->getRBraceLoc());
    if (!RBraceLocOrErr)
      return RBraceLocOrErr.takeError();
    ToLinkageSpec->setRBraceLoc(*RBraceLocOrErr);
  }

  ToLinkageSpec->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToLinkageSpec);

  return ToLinkageSpec;
}

ExpectedDecl ASTNodeImporter::ImportUsingShadowDecls(BaseUsingDecl *D,
                                                     BaseUsingDecl *ToSI) {
  for (UsingShadowDecl *FromShadow : D->shadows()) {
    if (Expected<UsingShadowDecl *> ToShadowOrErr = import(FromShadow))
      ToSI->addShadowDecl(*ToShadowOrErr);
    else
      // FIXME: We return error here but the definition is already created
      // and available with lookups. How to fix this?..
      return ToShadowOrErr.takeError();
  }
  return ToSI;
}

ExpectedDecl ASTNodeImporter::VisitUsingDecl(UsingDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  Error Err = Error::success();
  auto ToLoc = importChecked(Err, D->getNameInfo().getLoc());
  auto ToUsingLoc = importChecked(Err, D->getUsingLoc());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  if (Err)
    return std::move(Err);

  DeclarationNameInfo NameInfo(Name, ToLoc);
  if (Error Err = ImportDeclarationNameLoc(D->getNameInfo(), NameInfo))
    return std::move(Err);

  UsingDecl *ToUsing;
  if (GetImportedOrCreateDecl(ToUsing, D, Importer.getToContext(), DC,
                              ToUsingLoc, ToQualifierLoc, NameInfo,
                              D->hasTypename()))
    return ToUsing;

  ToUsing->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToUsing);

  if (NamedDecl *FromPattern =
      Importer.getFromContext().getInstantiatedFromUsingDecl(D)) {
    if (Expected<NamedDecl *> ToPatternOrErr = import(FromPattern))
      Importer.getToContext().setInstantiatedFromUsingDecl(
          ToUsing, *ToPatternOrErr);
    else
      return ToPatternOrErr.takeError();
  }

  return ImportUsingShadowDecls(D, ToUsing);
}

ExpectedDecl ASTNodeImporter::VisitUsingEnumDecl(UsingEnumDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  Error Err = Error::success();
  auto ToUsingLoc = importChecked(Err, D->getUsingLoc());
  auto ToEnumLoc = importChecked(Err, D->getEnumLoc());
  auto ToNameLoc = importChecked(Err, D->getLocation());
  auto *ToEnumType = importChecked(Err, D->getEnumType());
  if (Err)
    return std::move(Err);

  UsingEnumDecl *ToUsingEnum;
  if (GetImportedOrCreateDecl(ToUsingEnum, D, Importer.getToContext(), DC,
                              ToUsingLoc, ToEnumLoc, ToNameLoc, ToEnumType))
    return ToUsingEnum;

  ToUsingEnum->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToUsingEnum);

  if (UsingEnumDecl *FromPattern =
          Importer.getFromContext().getInstantiatedFromUsingEnumDecl(D)) {
    if (Expected<UsingEnumDecl *> ToPatternOrErr = import(FromPattern))
      Importer.getToContext().setInstantiatedFromUsingEnumDecl(ToUsingEnum,
                                                               *ToPatternOrErr);
    else
      return ToPatternOrErr.takeError();
  }

  return ImportUsingShadowDecls(D, ToUsingEnum);
}

ExpectedDecl ASTNodeImporter::VisitUsingShadowDecl(UsingShadowDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  Expected<BaseUsingDecl *> ToIntroducerOrErr = import(D->getIntroducer());
  if (!ToIntroducerOrErr)
    return ToIntroducerOrErr.takeError();

  Expected<NamedDecl *> ToTargetOrErr = import(D->getTargetDecl());
  if (!ToTargetOrErr)
    return ToTargetOrErr.takeError();

  UsingShadowDecl *ToShadow;
  if (auto *FromConstructorUsingShadow =
          dyn_cast<ConstructorUsingShadowDecl>(D)) {
    Error Err = Error::success();
    ConstructorUsingShadowDecl *Nominated = importChecked(
        Err, FromConstructorUsingShadow->getNominatedBaseClassShadowDecl());
    if (Err)
      return std::move(Err);
    // The 'Target' parameter of ConstructorUsingShadowDecl constructor
    // is really the "NominatedBaseClassShadowDecl" value if it exists
    // (see code of ConstructorUsingShadowDecl::ConstructorUsingShadowDecl).
    // We should pass the NominatedBaseClassShadowDecl to it (if non-null) to
    // get the correct values.
    if (GetImportedOrCreateDecl<ConstructorUsingShadowDecl>(
            ToShadow, D, Importer.getToContext(), DC, Loc,
            cast<UsingDecl>(*ToIntroducerOrErr),
            Nominated ? Nominated : *ToTargetOrErr,
            FromConstructorUsingShadow->constructsVirtualBase()))
      return ToShadow;
  } else {
    if (GetImportedOrCreateDecl(ToShadow, D, Importer.getToContext(), DC, Loc,
                                Name, *ToIntroducerOrErr, *ToTargetOrErr))
      return ToShadow;
  }

  ToShadow->setLexicalDeclContext(LexicalDC);
  ToShadow->setAccess(D->getAccess());

  if (UsingShadowDecl *FromPattern =
      Importer.getFromContext().getInstantiatedFromUsingShadowDecl(D)) {
    if (Expected<UsingShadowDecl *> ToPatternOrErr = import(FromPattern))
      Importer.getToContext().setInstantiatedFromUsingShadowDecl(
          ToShadow, *ToPatternOrErr);
    else
      // FIXME: We return error here but the definition is already created
      // and available with lookups. How to fix this?..
      return ToPatternOrErr.takeError();
  }

  LexicalDC->addDeclInternal(ToShadow);

  return ToShadow;
}

ExpectedDecl ASTNodeImporter::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  auto ToComAncestorOrErr = Importer.ImportContext(D->getCommonAncestor());
  if (!ToComAncestorOrErr)
    return ToComAncestorOrErr.takeError();

  Error Err = Error::success();
  auto ToNominatedNamespace = importChecked(Err, D->getNominatedNamespace());
  auto ToUsingLoc = importChecked(Err, D->getUsingLoc());
  auto ToNamespaceKeyLocation =
      importChecked(Err, D->getNamespaceKeyLocation());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  auto ToIdentLocation = importChecked(Err, D->getIdentLocation());
  if (Err)
    return std::move(Err);

  UsingDirectiveDecl *ToUsingDir;
  if (GetImportedOrCreateDecl(ToUsingDir, D, Importer.getToContext(), DC,
                              ToUsingLoc,
                              ToNamespaceKeyLocation,
                              ToQualifierLoc,
                              ToIdentLocation,
                              ToNominatedNamespace, *ToComAncestorOrErr))
    return ToUsingDir;

  ToUsingDir->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToUsingDir);

  return ToUsingDir;
}

ExpectedDecl ASTNodeImporter::VisitUsingPackDecl(UsingPackDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  auto ToInstantiatedFromUsingOrErr =
      Importer.Import(D->getInstantiatedFromUsingDecl());
  if (!ToInstantiatedFromUsingOrErr)
    return ToInstantiatedFromUsingOrErr.takeError();
  SmallVector<NamedDecl *, 4> Expansions(D->expansions().size());
  if (Error Err = ImportArrayChecked(D->expansions(), Expansions.begin()))
    return std::move(Err);

  UsingPackDecl *ToUsingPack;
  if (GetImportedOrCreateDecl(ToUsingPack, D, Importer.getToContext(), DC,
                              cast<NamedDecl>(*ToInstantiatedFromUsingOrErr),
                              Expansions))
    return ToUsingPack;

  addDeclToContexts(D, ToUsingPack);

  return ToUsingPack;
}

ExpectedDecl ASTNodeImporter::VisitUnresolvedUsingValueDecl(
    UnresolvedUsingValueDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  Error Err = Error::success();
  auto ToLoc = importChecked(Err, D->getNameInfo().getLoc());
  auto ToUsingLoc = importChecked(Err, D->getUsingLoc());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  auto ToEllipsisLoc = importChecked(Err, D->getEllipsisLoc());
  if (Err)
    return std::move(Err);

  DeclarationNameInfo NameInfo(Name, ToLoc);
  if (Error Err = ImportDeclarationNameLoc(D->getNameInfo(), NameInfo))
    return std::move(Err);

  UnresolvedUsingValueDecl *ToUsingValue;
  if (GetImportedOrCreateDecl(ToUsingValue, D, Importer.getToContext(), DC,
                              ToUsingLoc, ToQualifierLoc, NameInfo,
                              ToEllipsisLoc))
    return ToUsingValue;

  ToUsingValue->setAccess(D->getAccess());
  ToUsingValue->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToUsingValue);

  return ToUsingValue;
}

ExpectedDecl ASTNodeImporter::VisitUnresolvedUsingTypenameDecl(
    UnresolvedUsingTypenameDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD = nullptr;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  Error Err = Error::success();
  auto ToUsingLoc = importChecked(Err, D->getUsingLoc());
  auto ToTypenameLoc = importChecked(Err, D->getTypenameLoc());
  auto ToQualifierLoc = importChecked(Err, D->getQualifierLoc());
  auto ToEllipsisLoc = importChecked(Err, D->getEllipsisLoc());
  if (Err)
    return std::move(Err);

  UnresolvedUsingTypenameDecl *ToUsing;
  if (GetImportedOrCreateDecl(ToUsing, D, Importer.getToContext(), DC,
                              ToUsingLoc, ToTypenameLoc,
                              ToQualifierLoc, Loc, Name, ToEllipsisLoc))
    return ToUsing;

  ToUsing->setAccess(D->getAccess());
  ToUsing->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToUsing);

  return ToUsing;
}

ExpectedDecl ASTNodeImporter::VisitBuiltinTemplateDecl(BuiltinTemplateDecl *D) {
  Decl* ToD = nullptr;
  switch (D->getBuiltinTemplateKind()) {
  case BuiltinTemplateKind::BTK__make_integer_seq:
    ToD = Importer.getToContext().getMakeIntegerSeqDecl();
    break;
  case BuiltinTemplateKind::BTK__type_pack_element:
    ToD = Importer.getToContext().getTypePackElementDecl();
    break;
  }
  assert(ToD && "BuiltinTemplateDecl of unsupported kind!");
  Importer.MapImported(D, ToD);
  return ToD;
}

Error ASTNodeImporter::ImportDefinition(
    ObjCInterfaceDecl *From, ObjCInterfaceDecl *To, ImportDefinitionKind Kind) {
  if (To->getDefinition()) {
    // Check consistency of superclass.
    ObjCInterfaceDecl *FromSuper = From->getSuperClass();
    if (FromSuper) {
      if (auto FromSuperOrErr = import(FromSuper))
        FromSuper = *FromSuperOrErr;
      else
        return FromSuperOrErr.takeError();
    }

    ObjCInterfaceDecl *ToSuper = To->getSuperClass();
    if ((bool)FromSuper != (bool)ToSuper ||
        (FromSuper && !declaresSameEntity(FromSuper, ToSuper))) {
      Importer.ToDiag(To->getLocation(),
                      diag::warn_odr_objc_superclass_inconsistent)
        << To->getDeclName();
      if (ToSuper)
        Importer.ToDiag(To->getSuperClassLoc(), diag::note_odr_objc_superclass)
          << To->getSuperClass()->getDeclName();
      else
        Importer.ToDiag(To->getLocation(),
                        diag::note_odr_objc_missing_superclass);
      if (From->getSuperClass())
        Importer.FromDiag(From->getSuperClassLoc(),
                          diag::note_odr_objc_superclass)
        << From->getSuperClass()->getDeclName();
      else
        Importer.FromDiag(From->getLocation(),
                          diag::note_odr_objc_missing_superclass);
    }

    if (shouldForceImportDeclContext(Kind))
      if (Error Err = ImportDeclContext(From))
        return Err;
    return Error::success();
  }

  // Start the definition.
  To->startDefinition();

  // If this class has a superclass, import it.
  if (From->getSuperClass()) {
    if (auto SuperTInfoOrErr = import(From->getSuperClassTInfo()))
      To->setSuperClass(*SuperTInfoOrErr);
    else
      return SuperTInfoOrErr.takeError();
  }

  // Import protocols
  SmallVector<ObjCProtocolDecl *, 4> Protocols;
  SmallVector<SourceLocation, 4> ProtocolLocs;
  ObjCInterfaceDecl::protocol_loc_iterator FromProtoLoc =
      From->protocol_loc_begin();

  for (ObjCInterfaceDecl::protocol_iterator FromProto = From->protocol_begin(),
                                         FromProtoEnd = From->protocol_end();
       FromProto != FromProtoEnd;
       ++FromProto, ++FromProtoLoc) {
    if (Expected<ObjCProtocolDecl *> ToProtoOrErr = import(*FromProto))
      Protocols.push_back(*ToProtoOrErr);
    else
      return ToProtoOrErr.takeError();

    if (ExpectedSLoc ToProtoLocOrErr = import(*FromProtoLoc))
      ProtocolLocs.push_back(*ToProtoLocOrErr);
    else
      return ToProtoLocOrErr.takeError();

  }

  // FIXME: If we're merging, make sure that the protocol list is the same.
  To->setProtocolList(Protocols.data(), Protocols.size(),
                      ProtocolLocs.data(), Importer.getToContext());

  // Import categories. When the categories themselves are imported, they'll
  // hook themselves into this interface.
  for (auto *Cat : From->known_categories()) {
    auto ToCatOrErr = import(Cat);
    if (!ToCatOrErr)
      return ToCatOrErr.takeError();
  }

  // If we have an @implementation, import it as well.
  if (From->getImplementation()) {
    if (Expected<ObjCImplementationDecl *> ToImplOrErr =
        import(From->getImplementation()))
      To->setImplementation(*ToImplOrErr);
    else
      return ToImplOrErr.takeError();
  }

  // Import all of the members of this class.
  if (Error Err = ImportDeclContext(From, /*ForceImport=*/true))
    return Err;

  return Error::success();
}

Expected<ObjCTypeParamList *>
ASTNodeImporter::ImportObjCTypeParamList(ObjCTypeParamList *list) {
  if (!list)
    return nullptr;

  SmallVector<ObjCTypeParamDecl *, 4> toTypeParams;
  for (auto *fromTypeParam : *list) {
    if (auto toTypeParamOrErr = import(fromTypeParam))
      toTypeParams.push_back(*toTypeParamOrErr);
    else
      return toTypeParamOrErr.takeError();
  }

  auto LAngleLocOrErr = import(list->getLAngleLoc());
  if (!LAngleLocOrErr)
    return LAngleLocOrErr.takeError();

  auto RAngleLocOrErr = import(list->getRAngleLoc());
  if (!RAngleLocOrErr)
    return RAngleLocOrErr.takeError();

  return ObjCTypeParamList::create(Importer.getToContext(),
                                   *LAngleLocOrErr,
                                   toTypeParams,
                                   *RAngleLocOrErr);
}

ExpectedDecl ASTNodeImporter::VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
  // If this class has a definition in the translation unit we're coming from,
  // but this particular declaration is not that definition, import the
  // definition and map to that.
  ObjCInterfaceDecl *Definition = D->getDefinition();
  if (Definition && Definition != D) {
    if (ExpectedDecl ImportedDefOrErr = import(Definition))
      return Importer.MapImported(D, *ImportedDefOrErr);
    else
      return ImportedDefOrErr.takeError();
  }

  // Import the major distinguishing characteristics of an @interface.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Look for an existing interface with the same name.
  ObjCInterfaceDecl *MergeWithIface = nullptr;
  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  for (auto *FoundDecl : FoundDecls) {
    if (!FoundDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))
      continue;

    if ((MergeWithIface = dyn_cast<ObjCInterfaceDecl>(FoundDecl)))
      break;
  }

  // Create an interface declaration, if one does not already exist.
  ObjCInterfaceDecl *ToIface = MergeWithIface;
  if (!ToIface) {
    ExpectedSLoc AtBeginLocOrErr = import(D->getAtStartLoc());
    if (!AtBeginLocOrErr)
      return AtBeginLocOrErr.takeError();

    if (GetImportedOrCreateDecl(
            ToIface, D, Importer.getToContext(), DC,
            *AtBeginLocOrErr, Name.getAsIdentifierInfo(),
            /*TypeParamList=*/nullptr,
            /*PrevDecl=*/nullptr, Loc, D->isImplicitInterfaceDecl()))
      return ToIface;
    ToIface->setLexicalDeclContext(LexicalDC);
    LexicalDC->addDeclInternal(ToIface);
  }
  Importer.MapImported(D, ToIface);
  // Import the type parameter list after MapImported, to avoid
  // loops when bringing in their DeclContext.
  if (auto ToPListOrErr =
      ImportObjCTypeParamList(D->getTypeParamListAsWritten()))
    ToIface->setTypeParamList(*ToPListOrErr);
  else
    return ToPListOrErr.takeError();

  if (D->isThisDeclarationADefinition())
    if (Error Err = ImportDefinition(D, ToIface))
      return std::move(Err);

  return ToIface;
}

ExpectedDecl
ASTNodeImporter::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D) {
  ObjCCategoryDecl *Category;
  if (Error Err = importInto(Category, D->getCategoryDecl()))
    return std::move(Err);

  ObjCCategoryImplDecl *ToImpl = Category->getImplementation();
  if (!ToImpl) {
    DeclContext *DC, *LexicalDC;
    if (Error Err = ImportDeclContext(D, DC, LexicalDC))
      return std::move(Err);

    Error Err = Error::success();
    auto ToLocation = importChecked(Err, D->getLocation());
    auto ToAtStartLoc = importChecked(Err, D->getAtStartLoc());
    auto ToCategoryNameLoc = importChecked(Err, D->getCategoryNameLoc());
    if (Err)
      return std::move(Err);

    if (GetImportedOrCreateDecl(
            ToImpl, D, Importer.getToContext(), DC,
            Importer.Import(D->getIdentifier()), Category->getClassInterface(),
            ToLocation, ToAtStartLoc, ToCategoryNameLoc))
      return ToImpl;

    ToImpl->setLexicalDeclContext(LexicalDC);
    LexicalDC->addDeclInternal(ToImpl);
    Category->setImplementation(ToImpl);
  }

  Importer.MapImported(D, ToImpl);
  if (Error Err = ImportDeclContext(D))
    return std::move(Err);

  return ToImpl;
}

ExpectedDecl
ASTNodeImporter::VisitObjCImplementationDecl(ObjCImplementationDecl *D) {
  // Find the corresponding interface.
  ObjCInterfaceDecl *Iface;
  if (Error Err = importInto(Iface, D->getClassInterface()))
    return std::move(Err);

  // Import the superclass, if any.
  ObjCInterfaceDecl *Super;
  if (Error Err = importInto(Super, D->getSuperClass()))
    return std::move(Err);

  ObjCImplementationDecl *Impl = Iface->getImplementation();
  if (!Impl) {
    // We haven't imported an implementation yet. Create a new @implementation
    // now.
    DeclContext *DC, *LexicalDC;
    if (Error Err = ImportDeclContext(D, DC, LexicalDC))
      return std::move(Err);

    Error Err = Error::success();
    auto ToLocation = importChecked(Err, D->getLocation());
    auto ToAtStartLoc = importChecked(Err, D->getAtStartLoc());
    auto ToSuperClassLoc = importChecked(Err, D->getSuperClassLoc());
    auto ToIvarLBraceLoc = importChecked(Err, D->getIvarLBraceLoc());
    auto ToIvarRBraceLoc = importChecked(Err, D->getIvarRBraceLoc());
    if (Err)
      return std::move(Err);

    if (GetImportedOrCreateDecl(Impl, D, Importer.getToContext(),
                                DC, Iface, Super,
                                ToLocation,
                                ToAtStartLoc,
                                ToSuperClassLoc,
                                ToIvarLBraceLoc,
                                ToIvarRBraceLoc))
      return Impl;

    Impl->setLexicalDeclContext(LexicalDC);

    // Associate the implementation with the class it implements.
    Iface->setImplementation(Impl);
    Importer.MapImported(D, Iface->getImplementation());
  } else {
    Importer.MapImported(D, Iface->getImplementation());

    // Verify that the existing @implementation has the same superclass.
    if ((Super && !Impl->getSuperClass()) ||
        (!Super && Impl->getSuperClass()) ||
        (Super && Impl->getSuperClass() &&
         !declaresSameEntity(Super->getCanonicalDecl(),
                             Impl->getSuperClass()))) {
      Importer.ToDiag(Impl->getLocation(),
                      diag::warn_odr_objc_superclass_inconsistent)
        << Iface->getDeclName();
      // FIXME: It would be nice to have the location of the superclass
      // below.
      if (Impl->getSuperClass())
        Importer.ToDiag(Impl->getLocation(),
                        diag::note_odr_objc_superclass)
        << Impl->getSuperClass()->getDeclName();
      else
        Importer.ToDiag(Impl->getLocation(),
                        diag::note_odr_objc_missing_superclass);
      if (D->getSuperClass())
        Importer.FromDiag(D->getLocation(),
                          diag::note_odr_objc_superclass)
        << D->getSuperClass()->getDeclName();
      else
        Importer.FromDiag(D->getLocation(),
                          diag::note_odr_objc_missing_superclass);

      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }
  }

  // Import all of the members of this @implementation.
  if (Error Err = ImportDeclContext(D))
    return std::move(Err);

  return Impl;
}

ExpectedDecl ASTNodeImporter::VisitObjCPropertyDecl(ObjCPropertyDecl *D) {
  // Import the major distinguishing characteristics of an @property.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Check whether we have already imported this property.
  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  for (auto *FoundDecl : FoundDecls) {
    if (auto *FoundProp = dyn_cast<ObjCPropertyDecl>(FoundDecl)) {
      // Instance and class properties can share the same name but are different
      // declarations.
      if (FoundProp->isInstanceProperty() != D->isInstanceProperty())
        continue;

      // Check property types.
      if (!Importer.IsStructurallyEquivalent(D->getType(),
                                             FoundProp->getType())) {
        Importer.ToDiag(Loc, diag::warn_odr_objc_property_type_inconsistent)
          << Name << D->getType() << FoundProp->getType();
        Importer.ToDiag(FoundProp->getLocation(), diag::note_odr_value_here)
          << FoundProp->getType();

        return make_error<ASTImportError>(ASTImportError::NameConflict);
      }

      // FIXME: Check property attributes, getters, setters, etc.?

      // Consider these properties to be equivalent.
      Importer.MapImported(D, FoundProp);
      return FoundProp;
    }
  }

  Error Err = Error::success();
  auto ToType = importChecked(Err, D->getType());
  auto ToTypeSourceInfo = importChecked(Err, D->getTypeSourceInfo());
  auto ToAtLoc = importChecked(Err, D->getAtLoc());
  auto ToLParenLoc = importChecked(Err, D->getLParenLoc());
  if (Err)
    return std::move(Err);

  // Create the new property.
  ObjCPropertyDecl *ToProperty;
  if (GetImportedOrCreateDecl(
          ToProperty, D, Importer.getToContext(), DC, Loc,
          Name.getAsIdentifierInfo(), ToAtLoc,
          ToLParenLoc, ToType,
          ToTypeSourceInfo, D->getPropertyImplementation()))
    return ToProperty;

  auto ToGetterName = importChecked(Err, D->getGetterName());
  auto ToSetterName = importChecked(Err, D->getSetterName());
  auto ToGetterNameLoc = importChecked(Err, D->getGetterNameLoc());
  auto ToSetterNameLoc = importChecked(Err, D->getSetterNameLoc());
  auto ToGetterMethodDecl = importChecked(Err, D->getGetterMethodDecl());
  auto ToSetterMethodDecl = importChecked(Err, D->getSetterMethodDecl());
  auto ToPropertyIvarDecl = importChecked(Err, D->getPropertyIvarDecl());
  if (Err)
    return std::move(Err);

  ToProperty->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToProperty);

  ToProperty->setPropertyAttributes(D->getPropertyAttributes());
  ToProperty->setPropertyAttributesAsWritten(
                                      D->getPropertyAttributesAsWritten());
  ToProperty->setGetterName(ToGetterName, ToGetterNameLoc);
  ToProperty->setSetterName(ToSetterName, ToSetterNameLoc);
  ToProperty->setGetterMethodDecl(ToGetterMethodDecl);
  ToProperty->setSetterMethodDecl(ToSetterMethodDecl);
  ToProperty->setPropertyIvarDecl(ToPropertyIvarDecl);
  return ToProperty;
}

ExpectedDecl
ASTNodeImporter::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
  ObjCPropertyDecl *Property;
  if (Error Err = importInto(Property, D->getPropertyDecl()))
    return std::move(Err);

  DeclContext *DC, *LexicalDC;
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return std::move(Err);

  auto *InImpl = cast<ObjCImplDecl>(LexicalDC);

  // Import the ivar (for an @synthesize).
  ObjCIvarDecl *Ivar = nullptr;
  if (Error Err = importInto(Ivar, D->getPropertyIvarDecl()))
    return std::move(Err);

  ObjCPropertyImplDecl *ToImpl
    = InImpl->FindPropertyImplDecl(Property->getIdentifier(),
                                   Property->getQueryKind());
  if (!ToImpl) {

    Error Err = Error::success();
    auto ToBeginLoc = importChecked(Err, D->getBeginLoc());
    auto ToLocation = importChecked(Err, D->getLocation());
    auto ToPropertyIvarDeclLoc =
        importChecked(Err, D->getPropertyIvarDeclLoc());
    if (Err)
      return std::move(Err);

    if (GetImportedOrCreateDecl(ToImpl, D, Importer.getToContext(), DC,
                                ToBeginLoc,
                                ToLocation, Property,
                                D->getPropertyImplementation(), Ivar,
                                ToPropertyIvarDeclLoc))
      return ToImpl;

    ToImpl->setLexicalDeclContext(LexicalDC);
    LexicalDC->addDeclInternal(ToImpl);
  } else {
    // Check that we have the same kind of property implementation (@synthesize
    // vs. @dynamic).
    if (D->getPropertyImplementation() != ToImpl->getPropertyImplementation()) {
      Importer.ToDiag(ToImpl->getLocation(),
                      diag::warn_odr_objc_property_impl_kind_inconsistent)
        << Property->getDeclName()
        << (ToImpl->getPropertyImplementation()
                                              == ObjCPropertyImplDecl::Dynamic);
      Importer.FromDiag(D->getLocation(),
                        diag::note_odr_objc_property_impl_kind)
        << D->getPropertyDecl()->getDeclName()
        << (D->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic);

      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }

    // For @synthesize, check that we have the same
    if (D->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize &&
        Ivar != ToImpl->getPropertyIvarDecl()) {
      Importer.ToDiag(ToImpl->getPropertyIvarDeclLoc(),
                      diag::warn_odr_objc_synthesize_ivar_inconsistent)
        << Property->getDeclName()
        << ToImpl->getPropertyIvarDecl()->getDeclName()
        << Ivar->getDeclName();
      Importer.FromDiag(D->getPropertyIvarDeclLoc(),
                        diag::note_odr_objc_synthesize_ivar_here)
        << D->getPropertyIvarDecl()->getDeclName();

      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }

    // Merge the existing implementation with the new implementation.
    Importer.MapImported(D, ToImpl);
  }

  return ToImpl;
}

ExpectedDecl
ASTNodeImporter::VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D) {
  // For template arguments, we adopt the translation unit as our declaration
  // context. This context will be fixed when the actual template declaration
  // is created.

  ExpectedSLoc BeginLocOrErr = import(D->getBeginLoc());
  if (!BeginLocOrErr)
    return BeginLocOrErr.takeError();

  ExpectedSLoc LocationOrErr = import(D->getLocation());
  if (!LocationOrErr)
    return LocationOrErr.takeError();

  TemplateTypeParmDecl *ToD = nullptr;
  if (GetImportedOrCreateDecl(
      ToD, D, Importer.getToContext(),
      Importer.getToContext().getTranslationUnitDecl(),
      *BeginLocOrErr, *LocationOrErr,
      D->getDepth(), D->getIndex(), Importer.Import(D->getIdentifier()),
      D->wasDeclaredWithTypename(), D->isParameterPack(),
      D->hasTypeConstraint()))
    return ToD;

  // Import the type-constraint
  if (const TypeConstraint *TC = D->getTypeConstraint()) {

    Error Err = Error::success();
    auto ToConceptRef = importChecked(Err, TC->getConceptReference());
    auto ToIDC = importChecked(Err, TC->getImmediatelyDeclaredConstraint());
    if (Err)
      return std::move(Err);

    ToD->setTypeConstraint(ToConceptRef, ToIDC);
  }

  if (D->hasDefaultArgument()) {
    Expected<TemplateArgumentLoc> ToDefaultArgOrErr =
        import(D->getDefaultArgument());
    if (!ToDefaultArgOrErr)
      return ToDefaultArgOrErr.takeError();
    ToD->setDefaultArgument(ToD->getASTContext(), *ToDefaultArgOrErr);
  }

  return ToD;
}

ExpectedDecl
ASTNodeImporter::VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D) {

  Error Err = Error::success();
  auto ToDeclName = importChecked(Err, D->getDeclName());
  auto ToLocation = importChecked(Err, D->getLocation());
  auto ToType = importChecked(Err, D->getType());
  auto ToTypeSourceInfo = importChecked(Err, D->getTypeSourceInfo());
  auto ToInnerLocStart = importChecked(Err, D->getInnerLocStart());
  if (Err)
    return std::move(Err);

  NonTypeTemplateParmDecl *ToD = nullptr;
  if (GetImportedOrCreateDecl(ToD, D, Importer.getToContext(),
                              Importer.getToContext().getTranslationUnitDecl(),
                              ToInnerLocStart, ToLocation, D->getDepth(),
                              D->getPosition(),
                              ToDeclName.getAsIdentifierInfo(), ToType,
                              D->isParameterPack(), ToTypeSourceInfo))
    return ToD;

  if (D->hasDefaultArgument()) {
    Expected<TemplateArgumentLoc> ToDefaultArgOrErr =
        import(D->getDefaultArgument());
    if (!ToDefaultArgOrErr)
      return ToDefaultArgOrErr.takeError();
    ToD->setDefaultArgument(Importer.getToContext(), *ToDefaultArgOrErr);
  }

  return ToD;
}

ExpectedDecl
ASTNodeImporter::VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D) {
  // Import the name of this declaration.
  auto NameOrErr = import(D->getDeclName());
  if (!NameOrErr)
    return NameOrErr.takeError();

  // Import the location of this declaration.
  ExpectedSLoc LocationOrErr = import(D->getLocation());
  if (!LocationOrErr)
    return LocationOrErr.takeError();

  // Import template parameters.
  auto TemplateParamsOrErr = import(D->getTemplateParameters());
  if (!TemplateParamsOrErr)
    return TemplateParamsOrErr.takeError();

  TemplateTemplateParmDecl *ToD = nullptr;
  if (GetImportedOrCreateDecl(
          ToD, D, Importer.getToContext(),
          Importer.getToContext().getTranslationUnitDecl(), *LocationOrErr,
          D->getDepth(), D->getPosition(), D->isParameterPack(),
          (*NameOrErr).getAsIdentifierInfo(), D->wasDeclaredWithTypename(),
          *TemplateParamsOrErr))
    return ToD;

  if (D->hasDefaultArgument()) {
    Expected<TemplateArgumentLoc> ToDefaultArgOrErr =
        import(D->getDefaultArgument());
    if (!ToDefaultArgOrErr)
      return ToDefaultArgOrErr.takeError();
    ToD->setDefaultArgument(Importer.getToContext(), *ToDefaultArgOrErr);
  }

  return ToD;
}

// Returns the definition for a (forward) declaration of a TemplateDecl, if
// it has any definition in the redecl chain.
template <typename T> static auto getTemplateDefinition(T *D) -> T * {
  assert(D->getTemplatedDecl() && "Should be called on templates only");
  auto *ToTemplatedDef = D->getTemplatedDecl()->getDefinition();
  if (!ToTemplatedDef)
    return nullptr;
  auto *TemplateWithDef = ToTemplatedDef->getDescribedTemplate();
  return cast_or_null<T>(TemplateWithDef);
}

ExpectedDecl ASTNodeImporter::VisitClassTemplateDecl(ClassTemplateDecl *D) {

  // Import the major distinguishing characteristics of this class template.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // Should check if a declaration is friend in a dependent context.
  // Such templates are not linked together in a declaration chain.
  // The ASTImporter strategy is to map existing forward declarations to
  // imported ones only if strictly necessary, otherwise import these as new
  // forward declarations. In case of the "dependent friend" declarations, new
  // declarations are created, but not linked in a declaration chain.
  auto IsDependentFriend = [](ClassTemplateDecl *TD) {
    return TD->getFriendObjectKind() != Decl::FOK_None &&
           TD->getLexicalDeclContext()->isDependentContext();
  };
  bool DependentFriend = IsDependentFriend(D);

  ClassTemplateDecl *FoundByLookup = nullptr;

  // We may already have a template of the same name; try to find and match it.
  if (!DC->isFunctionOrMethod()) {
    SmallVector<NamedDecl *, 4> ConflictingDecls;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary |
                                              Decl::IDNS_TagFriend))
        continue;

      Decl *Found = FoundDecl;
      auto *FoundTemplate = dyn_cast<ClassTemplateDecl>(Found);
      if (FoundTemplate) {
        if (!hasSameVisibilityContextAndLinkage(FoundTemplate, D))
          continue;

        // FIXME: sufficient conditon for 'IgnoreTemplateParmDepth'?
        bool IgnoreTemplateParmDepth =
            (FoundTemplate->getFriendObjectKind() != Decl::FOK_None) !=
            (D->getFriendObjectKind() != Decl::FOK_None);
        if (IsStructuralMatch(D, FoundTemplate, /*Complain=*/true,
                              IgnoreTemplateParmDepth)) {
          if (DependentFriend || IsDependentFriend(FoundTemplate))
            continue;

          ClassTemplateDecl *TemplateWithDef =
              getTemplateDefinition(FoundTemplate);
          if (D->isThisDeclarationADefinition() && TemplateWithDef)
            return Importer.MapImported(D, TemplateWithDef);
          if (!FoundByLookup)
            FoundByLookup = FoundTemplate;
          // Search in all matches because there may be multiple decl chains,
          // see ASTTests test ImportExistingFriendClassTemplateDef.
          continue;
        }
        ConflictingDecls.push_back(FoundDecl);
      }
    }

    if (!ConflictingDecls.empty()) {
      ExpectedName NameOrErr = Importer.HandleNameConflict(
          Name, DC, Decl::IDNS_Ordinary, ConflictingDecls.data(),
          ConflictingDecls.size());
      if (NameOrErr)
        Name = NameOrErr.get();
      else
        return NameOrErr.takeError();
    }
  }

  CXXRecordDecl *FromTemplated = D->getTemplatedDecl();

  auto TemplateParamsOrErr = import(D->getTemplateParameters());
  if (!TemplateParamsOrErr)
    return TemplateParamsOrErr.takeError();

  // Create the declaration that is being templated.
  CXXRecordDecl *ToTemplated;
  if (Error Err = importInto(ToTemplated, FromTemplated))
    return std::move(Err);

  // Create the class template declaration itself.
  ClassTemplateDecl *D2;
  if (GetImportedOrCreateDecl(D2, D, Importer.getToContext(), DC, Loc, Name,
                              *TemplateParamsOrErr, ToTemplated))
    return D2;

  ToTemplated->setDescribedClassTemplate(D2);

  D2->setAccess(D->getAccess());
  D2->setLexicalDeclContext(LexicalDC);

  addDeclToContexts(D, D2);
  updateLookupTableForTemplateParameters(**TemplateParamsOrErr);

  if (FoundByLookup) {
    auto *Recent =
        const_cast<ClassTemplateDecl *>(FoundByLookup->getMostRecentDecl());

    // It is possible that during the import of the class template definition
    // we start the import of a fwd friend decl of the very same class template
    // and we add the fwd friend decl to the lookup table. But the ToTemplated
    // had been created earlier and by that time the lookup could not find
    // anything existing, so it has no previous decl. Later, (still during the
    // import of the fwd friend decl) we start to import the definition again
    // and this time the lookup finds the previous fwd friend class template.
    // In this case we must set up the previous decl for the templated decl.
    if (!ToTemplated->getPreviousDecl()) {
      assert(FoundByLookup->getTemplatedDecl() &&
             "Found decl must have its templated decl set");
      CXXRecordDecl *PrevTemplated =
          FoundByLookup->getTemplatedDecl()->getMostRecentDecl();
      if (ToTemplated != PrevTemplated)
        ToTemplated->setPreviousDecl(PrevTemplated);
    }

    D2->setPreviousDecl(Recent);
  }

  return D2;
}

ExpectedDecl ASTNodeImporter::VisitClassTemplateSpecializationDecl(
                                          ClassTemplateSpecializationDecl *D) {
  ClassTemplateDecl *ClassTemplate;
  if (Error Err = importInto(ClassTemplate, D->getSpecializedTemplate()))
    return std::move(Err);

  // Import the context of this declaration.
  DeclContext *DC, *LexicalDC;
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return std::move(Err);

  // Import template arguments.
  SmallVector<TemplateArgument, 2> TemplateArgs;
  if (Error Err =
          ImportTemplateArguments(D->getTemplateArgs().asArray(), TemplateArgs))
    return std::move(Err);
  // Try to find an existing specialization with these template arguments and
  // template parameter list.
  void *InsertPos = nullptr;
  ClassTemplateSpecializationDecl *PrevDecl = nullptr;
  ClassTemplatePartialSpecializationDecl *PartialSpec =
            dyn_cast<ClassTemplatePartialSpecializationDecl>(D);

  // Import template parameters.
  TemplateParameterList *ToTPList = nullptr;

  if (PartialSpec) {
    auto ToTPListOrErr = import(PartialSpec->getTemplateParameters());
    if (!ToTPListOrErr)
      return ToTPListOrErr.takeError();
    ToTPList = *ToTPListOrErr;
    PrevDecl = ClassTemplate->findPartialSpecialization(TemplateArgs,
                                                        *ToTPListOrErr,
                                                        InsertPos);
  } else
    PrevDecl = ClassTemplate->findSpecialization(TemplateArgs, InsertPos);

  if (PrevDecl) {
    if (IsStructuralMatch(D, PrevDecl)) {
      CXXRecordDecl *PrevDefinition = PrevDecl->getDefinition();
      if (D->isThisDeclarationADefinition() && PrevDefinition) {
        Importer.MapImported(D, PrevDefinition);
        // Import those default field initializers which have been
        // instantiated in the "From" context, but not in the "To" context.
        for (auto *FromField : D->fields()) {
          auto ToOrErr = import(FromField);
          if (!ToOrErr)
            return ToOrErr.takeError();
        }

        // Import those methods which have been instantiated in the
        // "From" context, but not in the "To" context.
        for (CXXMethodDecl *FromM : D->methods()) {
          auto ToOrErr = import(FromM);
          if (!ToOrErr)
            return ToOrErr.takeError();
        }

        // TODO Import instantiated default arguments.
        // TODO Import instantiated exception specifications.
        //
        // Generally, ASTCommon.h/DeclUpdateKind enum gives a very good hint
        // what else could be fused during an AST merge.
        return PrevDefinition;
      }
    } else { // ODR violation.
      // FIXME HandleNameConflict
      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }
  }

  // Import the location of this declaration.
  ExpectedSLoc BeginLocOrErr = import(D->getBeginLoc());
  if (!BeginLocOrErr)
    return BeginLocOrErr.takeError();
  ExpectedSLoc IdLocOrErr = import(D->getLocation());
  if (!IdLocOrErr)
    return IdLocOrErr.takeError();

  // Import TemplateArgumentListInfo.
  TemplateArgumentListInfo ToTAInfo;
  if (const auto *ASTTemplateArgs = D->getTemplateArgsAsWritten()) {
    if (Error Err = ImportTemplateArgumentListInfo(*ASTTemplateArgs, ToTAInfo))
      return std::move(Err);
  }

  // Create the specialization.
  ClassTemplateSpecializationDecl *D2 = nullptr;
  if (PartialSpec) {
    QualType CanonInjType;
    if (Error Err = importInto(
        CanonInjType, PartialSpec->getInjectedSpecializationType()))
      return std::move(Err);
    CanonInjType = CanonInjType.getCanonicalType();

    if (GetImportedOrCreateDecl<ClassTemplatePartialSpecializationDecl>(
            D2, D, Importer.getToContext(), D->getTagKind(), DC, *BeginLocOrErr,
            *IdLocOrErr, ToTPList, ClassTemplate,
            llvm::ArrayRef(TemplateArgs.data(), TemplateArgs.size()),
            CanonInjType,
            cast_or_null<ClassTemplatePartialSpecializationDecl>(PrevDecl)))
      return D2;

    // Update InsertPos, because preceding import calls may have invalidated
    // it by adding new specializations.
    auto *PartSpec2 = cast<ClassTemplatePartialSpecializationDecl>(D2);
    if (!ClassTemplate->findPartialSpecialization(TemplateArgs, ToTPList,
                                                  InsertPos))
      // Add this partial specialization to the class template.
      ClassTemplate->AddPartialSpecialization(PartSpec2, InsertPos);
    if (Expected<ClassTemplatePartialSpecializationDecl *> ToInstOrErr =
            import(PartialSpec->getInstantiatedFromMember()))
      PartSpec2->setInstantiatedFromMember(*ToInstOrErr);
    else
      return ToInstOrErr.takeError();

    updateLookupTableForTemplateParameters(*ToTPList);
  } else { // Not a partial specialization.
    if (GetImportedOrCreateDecl(
            D2, D, Importer.getToContext(), D->getTagKind(), DC,
            *BeginLocOrErr, *IdLocOrErr, ClassTemplate, TemplateArgs,
            PrevDecl))
      return D2;

    // Update InsertPos, because preceding import calls may have invalidated
    // it by adding new specializations.
    if (!ClassTemplate->findSpecialization(TemplateArgs, InsertPos))
      // Add this specialization to the class template.
      ClassTemplate->AddSpecialization(D2, InsertPos);
  }

  D2->setSpecializationKind(D->getSpecializationKind());

  // Set the context of this specialization/instantiation.
  D2->setLexicalDeclContext(LexicalDC);

  // Add to the DC only if it was an explicit specialization/instantiation.
  if (D2->isExplicitInstantiationOrSpecialization()) {
    LexicalDC->addDeclInternal(D2);
  }

  if (auto BraceRangeOrErr = import(D->getBraceRange()))
    D2->setBraceRange(*BraceRangeOrErr);
  else
    return BraceRangeOrErr.takeError();

  if (Error Err = ImportTemplateParameterLists(D, D2))
    return std::move(Err);

  // Import the qualifier, if any.
  if (auto LocOrErr = import(D->getQualifierLoc()))
    D2->setQualifierInfo(*LocOrErr);
  else
    return LocOrErr.takeError();

  if (D->getTemplateArgsAsWritten())
    D2->setTemplateArgsAsWritten(ToTAInfo);

  if (auto LocOrErr = import(D->getTemplateKeywordLoc()))
    D2->setTemplateKeywordLoc(*LocOrErr);
  else
    return LocOrErr.takeError();

  if (auto LocOrErr = import(D->getExternKeywordLoc()))
    D2->setExternKeywordLoc(*LocOrErr);
  else
    return LocOrErr.takeError();

  if (D->getPointOfInstantiation().isValid()) {
    if (auto POIOrErr = import(D->getPointOfInstantiation()))
      D2->setPointOfInstantiation(*POIOrErr);
    else
      return POIOrErr.takeError();
  }

  D2->setTemplateSpecializationKind(D->getTemplateSpecializationKind());

  if (auto P = D->getInstantiatedFrom()) {
    if (auto *CTD = P.dyn_cast<ClassTemplateDecl *>()) {
      if (auto CTDorErr = import(CTD))
        D2->setInstantiationOf(*CTDorErr);
    } else {
      auto *CTPSD = cast<ClassTemplatePartialSpecializationDecl *>(P);
      auto CTPSDOrErr = import(CTPSD);
      if (!CTPSDOrErr)
        return CTPSDOrErr.takeError();
      const TemplateArgumentList &DArgs = D->getTemplateInstantiationArgs();
      SmallVector<TemplateArgument, 2> D2ArgsVec(DArgs.size());
      for (unsigned I = 0; I < DArgs.size(); ++I) {
        const TemplateArgument &DArg = DArgs[I];
        if (auto ArgOrErr = import(DArg))
          D2ArgsVec[I] = *ArgOrErr;
        else
          return ArgOrErr.takeError();
      }
      D2->setInstantiationOf(
          *CTPSDOrErr,
          TemplateArgumentList::CreateCopy(Importer.getToContext(), D2ArgsVec));
    }
  }

  if (D->isCompleteDefinition())
    if (Error Err = ImportDefinition(D, D2))
      return std::move(Err);

  return D2;
}

ExpectedDecl ASTNodeImporter::VisitVarTemplateDecl(VarTemplateDecl *D) {
  // Import the major distinguishing characteristics of this variable template.
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;
  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);
  if (ToD)
    return ToD;

  // We may already have a template of the same name; try to find and match it.
  assert(!DC->isFunctionOrMethod() &&
         "Variable templates cannot be declared at function scope");

  SmallVector<NamedDecl *, 4> ConflictingDecls;
  auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
  VarTemplateDecl *FoundByLookup = nullptr;
  for (auto *FoundDecl : FoundDecls) {
    if (!FoundDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))
      continue;

    if (VarTemplateDecl *FoundTemplate = dyn_cast<VarTemplateDecl>(FoundDecl)) {
      // Use the templated decl, some linkage flags are set only there.
      if (!hasSameVisibilityContextAndLinkage(FoundTemplate->getTemplatedDecl(),
                                              D->getTemplatedDecl()))
        continue;
      if (IsStructuralMatch(D, FoundTemplate)) {
        // FIXME Check for ODR error if the two definitions have
        // different initializers?
        VarTemplateDecl *FoundDef = getTemplateDefinition(FoundTemplate);
        if (D->getDeclContext()->isRecord()) {
          assert(FoundTemplate->getDeclContext()->isRecord() &&
                 "Member variable template imported as non-member, "
                 "inconsistent imported AST?");
          if (FoundDef)
            return Importer.MapImported(D, FoundDef);
          if (!D->isThisDeclarationADefinition())
            return Importer.MapImported(D, FoundTemplate);
        } else {
          if (FoundDef && D->isThisDeclarationADefinition())
            return Importer.MapImported(D, FoundDef);
        }
        FoundByLookup = FoundTemplate;
        break;
      }
      ConflictingDecls.push_back(FoundDecl);
    }
  }

  if (!ConflictingDecls.empty()) {
    ExpectedName NameOrErr = Importer.HandleNameConflict(
        Name, DC, Decl::IDNS_Ordinary, ConflictingDecls.data(),
        ConflictingDecls.size());
    if (NameOrErr)
      Name = NameOrErr.get();
    else
      return NameOrErr.takeError();
  }

  VarDecl *DTemplated = D->getTemplatedDecl();

  // Import the type.
  // FIXME: Value not used?
  ExpectedType TypeOrErr = import(DTemplated->getType());
  if (!TypeOrErr)
    return TypeOrErr.takeError();

  // Create the declaration that is being templated.
  VarDecl *ToTemplated;
  if (Error Err = importInto(ToTemplated, DTemplated))
    return std::move(Err);

  // Create the variable template declaration itself.
  auto TemplateParamsOrErr = import(D->getTemplateParameters());
  if (!TemplateParamsOrErr)
    return TemplateParamsOrErr.takeError();

  VarTemplateDecl *ToVarTD;
  if (GetImportedOrCreateDecl(ToVarTD, D, Importer.getToContext(), DC, Loc,
                              Name, *TemplateParamsOrErr, ToTemplated))
    return ToVarTD;

  ToTemplated->setDescribedVarTemplate(ToVarTD);

  ToVarTD->setAccess(D->getAccess());
  ToVarTD->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(ToVarTD);
  if (DC != Importer.getToContext().getTranslationUnitDecl())
    updateLookupTableForTemplateParameters(**TemplateParamsOrErr);

  if (FoundByLookup) {
    auto *Recent =
        const_cast<VarTemplateDecl *>(FoundByLookup->getMostRecentDecl());
    if (!ToTemplated->getPreviousDecl()) {
      auto *PrevTemplated =
          FoundByLookup->getTemplatedDecl()->getMostRecentDecl();
      if (ToTemplated != PrevTemplated)
        ToTemplated->setPreviousDecl(PrevTemplated);
    }
    ToVarTD->setPreviousDecl(Recent);
  }

  return ToVarTD;
}

ExpectedDecl ASTNodeImporter::VisitVarTemplateSpecializationDecl(
    VarTemplateSpecializationDecl *D) {
  // A VarTemplateSpecializationDecl inherits from VarDecl, the import is done
  // in an analog way (but specialized for this case).

  SmallVector<Decl *, 2> Redecls = getCanonicalForwardRedeclChain(D);
  auto RedeclIt = Redecls.begin();
  // Import the first part of the decl chain. I.e. import all previous
  // declarations starting from the canonical decl.
  for (; RedeclIt != Redecls.end() && *RedeclIt != D; ++RedeclIt) {
    ExpectedDecl RedeclOrErr = import(*RedeclIt);
    if (!RedeclOrErr)
      return RedeclOrErr.takeError();
  }
  assert(*RedeclIt == D);

  VarTemplateDecl *VarTemplate = nullptr;
  if (Error Err = importInto(VarTemplate, D->getSpecializedTemplate()))
    return std::move(Err);

  // Import the context of this declaration.
  DeclContext *DC, *LexicalDC;
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return std::move(Err);

  // Import the location of this declaration.
  ExpectedSLoc BeginLocOrErr = import(D->getBeginLoc());
  if (!BeginLocOrErr)
    return BeginLocOrErr.takeError();

  auto IdLocOrErr = import(D->getLocation());
  if (!IdLocOrErr)
    return IdLocOrErr.takeError();

  // Import template arguments.
  SmallVector<TemplateArgument, 2> TemplateArgs;
  if (Error Err =
          ImportTemplateArguments(D->getTemplateArgs().asArray(), TemplateArgs))
    return std::move(Err);

  // Try to find an existing specialization with these template arguments.
  void *InsertPos = nullptr;
  VarTemplateSpecializationDecl *FoundSpecialization =
      VarTemplate->findSpecialization(TemplateArgs, InsertPos);
  if (FoundSpecialization) {
    if (IsStructuralMatch(D, FoundSpecialization)) {
      VarDecl *FoundDef = FoundSpecialization->getDefinition();
      if (D->getDeclContext()->isRecord()) {
        // In a record, it is allowed only to have one optional declaration and
        // one definition of the (static or constexpr) variable template.
        assert(
            FoundSpecialization->getDeclContext()->isRecord() &&
            "Member variable template specialization imported as non-member, "
            "inconsistent imported AST?");
        if (FoundDef)
          return Importer.MapImported(D, FoundDef);
        if (!D->isThisDeclarationADefinition())
          return Importer.MapImported(D, FoundSpecialization);
      } else {
        // If definition is imported and there is already one, map to it.
        // Otherwise create a new variable and link it to the existing.
        if (FoundDef && D->isThisDeclarationADefinition())
          return Importer.MapImported(D, FoundDef);
      }
    } else {
      return make_error<ASTImportError>(ASTImportError::NameConflict);
    }
  }

  VarTemplateSpecializationDecl *D2 = nullptr;

  TemplateArgumentListInfo ToTAInfo;
  if (const auto *Args = D->getTemplateArgsAsWritten()) {
    if (Error Err = ImportTemplateArgumentListInfo(*Args, ToTAInfo))
      return std::move(Err);
  }

  using PartVarSpecDecl = VarTemplatePartialSpecializationDecl;
  // Create a new specialization.
  if (auto *FromPartial = dyn_cast<PartVarSpecDecl>(D)) {
    auto ToTPListOrErr = import(FromPartial->getTemplateParameters());
    if (!ToTPListOrErr)
      return ToTPListOrErr.takeError();

    PartVarSpecDecl *ToPartial;
    if (GetImportedOrCreateDecl(ToPartial, D, Importer.getToContext(), DC,
                                *BeginLocOrErr, *IdLocOrErr, *ToTPListOrErr,
                                VarTemplate, QualType(), nullptr,
                                D->getStorageClass(), TemplateArgs))
      return ToPartial;

    if (Expected<PartVarSpecDecl *> ToInstOrErr =
            import(FromPartial->getInstantiatedFromMember()))
      ToPartial->setInstantiatedFromMember(*ToInstOrErr);
    else
      return ToInstOrErr.takeError();

    if (FromPartial->isMemberSpecialization())
      ToPartial->setMemberSpecialization();

    D2 = ToPartial;

    // FIXME: Use this update if VarTemplatePartialSpecializationDecl is fixed
    // to adopt template parameters.
    // updateLookupTableForTemplateParameters(**ToTPListOrErr);
  } else { // Full specialization
    if (GetImportedOrCreateDecl(D2, D, Importer.getToContext(), DC,
                                *BeginLocOrErr, *IdLocOrErr, VarTemplate,
                                QualType(), nullptr, D->getStorageClass(),
                                TemplateArgs))
      return D2;
  }

  // Update InsertPos, because preceding import calls may have invalidated
  // it by adding new specializations.
  if (!VarTemplate->findSpecialization(TemplateArgs, InsertPos))
    VarTemplate->AddSpecialization(D2, InsertPos);

  QualType T;
  if (Error Err = importInto(T, D->getType()))
    return std::move(Err);
  D2->setType(T);

  auto TInfoOrErr = import(D->getTypeSourceInfo());
  if (!TInfoOrErr)
    return TInfoOrErr.takeError();
  D2->setTypeSourceInfo(*TInfoOrErr);

  if (D->getPointOfInstantiation().isValid()) {
    if (ExpectedSLoc POIOrErr = import(D->getPointOfInstantiation()))
      D2->setPointOfInstantiation(*POIOrErr);
    else
      return POIOrErr.takeError();
  }

  D2->setSpecializationKind(D->getSpecializationKind());

  if (D->getTemplateArgsAsWritten())
    D2->setTemplateArgsAsWritten(ToTAInfo);

  if (auto LocOrErr = import(D->getQualifierLoc()))
    D2->setQualifierInfo(*LocOrErr);
  else
    return LocOrErr.takeError();

  if (D->isConstexpr())
    D2->setConstexpr(true);

  D2->setAccess(D->getAccess());

  if (Error Err = ImportInitializer(D, D2))
    return std::move(Err);

  if (FoundSpecialization)
    D2->setPreviousDecl(FoundSpecialization->getMostRecentDecl());

  addDeclToContexts(D, D2);

  // Import the rest of the chain. I.e. import all subsequent declarations.
  for (++RedeclIt; RedeclIt != Redecls.end(); ++RedeclIt) {
    ExpectedDecl RedeclOrErr = import(*RedeclIt);
    if (!RedeclOrErr)
      return RedeclOrErr.takeError();
  }

  return D2;
}

ExpectedDecl
ASTNodeImporter::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  DeclContext *DC, *LexicalDC;
  DeclarationName Name;
  SourceLocation Loc;
  NamedDecl *ToD;

  if (Error Err = ImportDeclParts(D, DC, LexicalDC, Name, ToD, Loc))
    return std::move(Err);

  if (ToD)
    return ToD;

  const FunctionTemplateDecl *FoundByLookup = nullptr;

  // Try to find a function in our own ("to") context with the same name, same
  // type, and in the same context as the function we're importing.
  // FIXME Split this into a separate function.
  if (!LexicalDC->isFunctionOrMethod()) {
    unsigned IDNS = Decl::IDNS_Ordinary | Decl::IDNS_OrdinaryFriend;
    auto FoundDecls = Importer.findDeclsInToCtx(DC, Name);
    for (auto *FoundDecl : FoundDecls) {
      if (!FoundDecl->isInIdentifierNamespace(IDNS))
        continue;

      if (auto *FoundTemplate = dyn_cast<FunctionTemplateDecl>(FoundDecl)) {
        if (!hasSameVisibilityContextAndLinkage(FoundTemplate, D))
          continue;
        if (IsStructuralMatch(D, FoundTemplate)) {
          FunctionTemplateDecl *TemplateWithDef =
              getTemplateDefinition(FoundTemplate);
          if (D->isThisDeclarationADefinition() && TemplateWithDef)
            return Importer.MapImported(D, TemplateWithDef);

          FoundByLookup = FoundTemplate;
          break;
          // TODO: handle conflicting names
        }
      }
    }
  }

  auto ParamsOrErr = import(D->getTemplateParameters());
  if (!ParamsOrErr)
    return ParamsOrErr.takeError();
  TemplateParameterList *Params = *ParamsOrErr;

  FunctionDecl *TemplatedFD;
  if (Error Err = importInto(TemplatedFD, D->getTemplatedDecl()))
    return std::move(Err);

  // At creation of the template the template parameters are "adopted"
  // (DeclContext is changed). After this possible change the lookup table
  // must be updated.
  // At deduction guides the DeclContext of the template parameters may be
  // different from what we would expect, it may be the class template, or a
  // probably different CXXDeductionGuideDecl. This may come from the fact that
  // the template parameter objects may be shared between deduction guides or
  // the class template, and at creation of multiple FunctionTemplateDecl
  // objects (for deduction guides) the same parameters are re-used. The
  // "adoption" happens multiple times with different parent, even recursively
  // for TemplateTemplateParmDecl. The same happens at import when the
  // FunctionTemplateDecl objects are created, but in different order.
  // In this way the DeclContext of these template parameters is not necessarily
  // the same as in the "from" context.
  SmallVector<DeclContext *, 2> OldParamDC;
  OldParamDC.reserve(Params->size());
  llvm::transform(*Params, std::back_inserter(OldParamDC),
                  [](NamedDecl *ND) { return ND->getDeclContext(); });

  FunctionTemplateDecl *ToFunc;
  if (GetImportedOrCreateDecl(ToFunc, D, Importer.getToContext(), DC, Loc, Name,
                              Params, TemplatedFD))
    return ToFunc;

  TemplatedFD->setDescribedFunctionTemplate(ToFunc);

  ToFunc->setAccess(D->getAccess());
  ToFunc->setLexicalDeclContext(LexicalDC);
  addDeclToContexts(D, ToFunc);

  ASTImporterLookupTable *LT = Importer.SharedState->getLookupTable();
  if (LT && !OldParamDC.empty()) {
    for (unsigned int I = 0; I < OldParamDC.size(); ++I)
      LT->updateForced(Params->getParam(I), OldParamDC[I]);
  }

  if (FoundByLookup) {
    auto *Recent =
        const_cast<FunctionTemplateDecl *>(FoundByLookup->getMostRecentDecl());
    if (!TemplatedFD->getPreviousDecl()) {
      assert(FoundByLookup->getTemplatedDecl() &&
             "Found decl must have its templated decl set");
      auto *PrevTemplated =
          FoundByLookup->getTemplatedDecl()->getMostRecentDecl();
      if (TemplatedFD != PrevTemplated)
        TemplatedFD->setPreviousDecl(PrevTemplated);
    }
    ToFunc->setPreviousDecl(Recent);
  }

  return ToFunc;
}

//----------------------------------------------------------------------------
// Import Statements
//----------------------------------------------------------------------------

ExpectedStmt ASTNodeImporter::VisitStmt(Stmt *S) {
  Importer.FromDiag(S->getBeginLoc(), diag::err_unsupported_ast_node)
      << S->getStmtClassName();
  return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
}


ExpectedStmt ASTNodeImporter::VisitGCCAsmStmt(GCCAsmStmt *S) {
  if (Importer.returnWithErrorInTest())
    return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
  SmallVector<IdentifierInfo *, 4> Names;
  for (unsigned I = 0, E = S->getNumOutputs(); I != E; I++) {
    IdentifierInfo *ToII = Importer.Import(S->getOutputIdentifier(I));
    // ToII is nullptr when no symbolic name is given for output operand
    // see ParseStmtAsm::ParseAsmOperandsOpt
    Names.push_back(ToII);
  }

  for (unsigned I = 0, E = S->getNumInputs(); I != E; I++) {
    IdentifierInfo *ToII = Importer.Import(S->getInputIdentifier(I));
    // ToII is nullptr when no symbolic name is given for input operand
    // see ParseStmtAsm::ParseAsmOperandsOpt
    Names.push_back(ToII);
  }

  SmallVector<StringLiteral *, 4> Clobbers;
  for (unsigned I = 0, E = S->getNumClobbers(); I != E; I++) {
    if (auto ClobberOrErr = import(S->getClobberStringLiteral(I)))
      Clobbers.push_back(*ClobberOrErr);
    else
      return ClobberOrErr.takeError();

  }

  SmallVector<StringLiteral *, 4> Constraints;
  for (unsigned I = 0, E = S->getNumOutputs(); I != E; I++) {
    if (auto OutputOrErr = import(S->getOutputConstraintLiteral(I)))
      Constraints.push_back(*OutputOrErr);
    else
      return OutputOrErr.takeError();
  }

  for (unsigned I = 0, E = S->getNumInputs(); I != E; I++) {
    if (auto InputOrErr = import(S->getInputConstraintLiteral(I)))
      Constraints.push_back(*InputOrErr);
    else
      return InputOrErr.takeError();
  }

  SmallVector<Expr *, 4> Exprs(S->getNumOutputs() + S->getNumInputs() +
                               S->getNumLabels());
  if (Error Err = ImportContainerChecked(S->outputs(), Exprs))
    return std::move(Err);

  if (Error Err =
          ImportArrayChecked(S->inputs(), Exprs.begin() + S->getNumOutputs()))
    return std::move(Err);

  if (Error Err = ImportArrayChecked(
          S->labels(), Exprs.begin() + S->getNumOutputs() + S->getNumInputs()))
    return std::move(Err);

  ExpectedSLoc AsmLocOrErr = import(S->getAsmLoc());
  if (!AsmLocOrErr)
    return AsmLocOrErr.takeError();
  auto AsmStrOrErr = import(S->getAsmString());
  if (!AsmStrOrErr)
    return AsmStrOrErr.takeError();
  ExpectedSLoc RParenLocOrErr = import(S->getRParenLoc());
  if (!RParenLocOrErr)
    return RParenLocOrErr.takeError();

  return new (Importer.getToContext()) GCCAsmStmt(
      Importer.getToContext(),
      *AsmLocOrErr,
      S->isSimple(),
      S->isVolatile(),
      S->getNumOutputs(),
      S->getNumInputs(),
      Names.data(),
      Constraints.data(),
      Exprs.data(),
      *AsmStrOrErr,
      S->getNumClobbers(),
      Clobbers.data(),
      S->getNumLabels(),
      *RParenLocOrErr);
}

ExpectedStmt ASTNodeImporter::VisitDeclStmt(DeclStmt *S) {

  Error Err = Error::success();
  auto ToDG = importChecked(Err, S->getDeclGroup());
  auto ToBeginLoc = importChecked(Err, S->getBeginLoc());
  auto ToEndLoc = importChecked(Err, S->getEndLoc());
  if (Err)
    return std::move(Err);
  return new (Importer.getToContext()) DeclStmt(ToDG, ToBeginLoc, ToEndLoc);
}

ExpectedStmt ASTNodeImporter::VisitNullStmt(NullStmt *S) {
  ExpectedSLoc ToSemiLocOrErr = import(S->getSemiLoc());
  if (!ToSemiLocOrErr)
    return ToSemiLocOrErr.takeError();
  return new (Importer.getToContext()) NullStmt(
      *ToSemiLocOrErr, S->hasLeadingEmptyMacro());
}

ExpectedStmt ASTNodeImporter::VisitCompoundStmt(CompoundStmt *S) {
  SmallVector<Stmt *, 8> ToStmts(S->size());

  if (Error Err = ImportContainerChecked(S->body(), ToStmts))
    return std::move(Err);

  ExpectedSLoc ToLBracLocOrErr = import(S->getLBracLoc());
  if (!ToLBracLocOrErr)
    return ToLBracLocOrErr.takeError();

  ExpectedSLoc ToRBracLocOrErr = import(S->getRBracLoc());
  if (!ToRBracLocOrErr)
    return ToRBracLocOrErr.takeError();

  FPOptionsOverride FPO =
      S->hasStoredFPFeatures() ? S->getStoredFPFeatures() : FPOptionsOverride();
  return CompoundStmt::Create(Importer.getToContext(), ToStmts, FPO,
                              *ToLBracLocOrErr, *ToRBracLocOrErr);
}

ExpectedStmt ASTNodeImporter::VisitCaseStmt(CaseStmt *S) {

  Error Err = Error::success();
  auto ToLHS = importChecked(Err, S->getLHS());
  auto ToRHS = importChecked(Err, S->getRHS());
  auto ToSubStmt = importChecked(Err, S->getSubStmt());
  auto ToCaseLoc = importChecked(Err, S->getCaseLoc());
  auto ToEllipsisLoc = importChecked(Err, S->getEllipsisLoc());
  auto ToColonLoc = importChecked(Err, S->getColonLoc());
  if (Err)
    return std::move(Err);

  auto *ToStmt = CaseStmt::Create(Importer.getToContext(), ToLHS, ToRHS,
                                  ToCaseLoc, ToEllipsisLoc, ToColonLoc);
  ToStmt->setSubStmt(ToSubStmt);

  return ToStmt;
}

ExpectedStmt ASTNodeImporter::VisitDefaultStmt(DefaultStmt *S) {

  Error Err = Error::success();
  auto ToDefaultLoc = importChecked(Err, S->getDefaultLoc());
  auto ToColonLoc = importChecked(Err, S->getColonLoc());
  auto ToSubStmt = importChecked(Err, S->getSubStmt());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) DefaultStmt(
    ToDefaultLoc, ToColonLoc, ToSubStmt);
}

ExpectedStmt ASTNodeImporter::VisitLabelStmt(LabelStmt *S) {

  Error Err = Error::success();
  auto ToIdentLoc = importChecked(Err, S->getIdentLoc());
  auto ToLabelDecl = importChecked(Err, S->getDecl());
  auto ToSubStmt = importChecked(Err, S->getSubStmt());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) LabelStmt(
      ToIdentLoc, ToLabelDecl, ToSubStmt);
}

ExpectedStmt ASTNodeImporter::VisitAttributedStmt(AttributedStmt *S) {
  ExpectedSLoc ToAttrLocOrErr = import(S->getAttrLoc());
  if (!ToAttrLocOrErr)
    return ToAttrLocOrErr.takeError();
  ArrayRef<const Attr*> FromAttrs(S->getAttrs());
  SmallVector<const Attr *, 1> ToAttrs(FromAttrs.size());
  if (Error Err = ImportContainerChecked(FromAttrs, ToAttrs))
    return std::move(Err);
  ExpectedStmt ToSubStmtOrErr = import(S->getSubStmt());
  if (!ToSubStmtOrErr)
    return ToSubStmtOrErr.takeError();

  return AttributedStmt::Create(
      Importer.getToContext(), *ToAttrLocOrErr, ToAttrs, *ToSubStmtOrErr);
}

ExpectedStmt ASTNodeImporter::VisitIfStmt(IfStmt *S) {

  Error Err = Error::success();
  auto ToIfLoc = importChecked(Err, S->getIfLoc());
  auto ToInit = importChecked(Err, S->getInit());
  auto ToConditionVariable = importChecked(Err, S->getConditionVariable());
  auto ToCond = importChecked(Err, S->getCond());
  auto ToLParenLoc = importChecked(Err, S->getLParenLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  auto ToThen = importChecked(Err, S->getThen());
  auto ToElseLoc = importChecked(Err, S->getElseLoc());
  auto ToElse = importChecked(Err, S->getElse());
  if (Err)
    return std::move(Err);

  return IfStmt::Create(Importer.getToContext(), ToIfLoc, S->getStatementKind(),
                        ToInit, ToConditionVariable, ToCond, ToLParenLoc,
                        ToRParenLoc, ToThen, ToElseLoc, ToElse);
}

ExpectedStmt ASTNodeImporter::VisitSwitchStmt(SwitchStmt *S) {

  Error Err = Error::success();
  auto ToInit = importChecked(Err, S->getInit());
  auto ToConditionVariable = importChecked(Err, S->getConditionVariable());
  auto ToCond = importChecked(Err, S->getCond());
  auto ToLParenLoc = importChecked(Err, S->getLParenLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  auto ToBody = importChecked(Err, S->getBody());
  auto ToSwitchLoc = importChecked(Err, S->getSwitchLoc());
  if (Err)
    return std::move(Err);

  auto *ToStmt =
      SwitchStmt::Create(Importer.getToContext(), ToInit, ToConditionVariable,
                         ToCond, ToLParenLoc, ToRParenLoc);
  ToStmt->setBody(ToBody);
  ToStmt->setSwitchLoc(ToSwitchLoc);

  // Now we have to re-chain the cases.
  SwitchCase *LastChainedSwitchCase = nullptr;
  for (SwitchCase *SC = S->getSwitchCaseList(); SC != nullptr;
       SC = SC->getNextSwitchCase()) {
    Expected<SwitchCase *> ToSCOrErr = import(SC);
    if (!ToSCOrErr)
      return ToSCOrErr.takeError();
    if (LastChainedSwitchCase)
      LastChainedSwitchCase->setNextSwitchCase(*ToSCOrErr);
    else
      ToStmt->setSwitchCaseList(*ToSCOrErr);
    LastChainedSwitchCase = *ToSCOrErr;
  }

  return ToStmt;
}

ExpectedStmt ASTNodeImporter::VisitWhileStmt(WhileStmt *S) {

  Error Err = Error::success();
  auto ToConditionVariable = importChecked(Err, S->getConditionVariable());
  auto ToCond = importChecked(Err, S->getCond());
  auto ToBody = importChecked(Err, S->getBody());
  auto ToWhileLoc = importChecked(Err, S->getWhileLoc());
  auto ToLParenLoc = importChecked(Err, S->getLParenLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  if (Err)
    return std::move(Err);

  return WhileStmt::Create(Importer.getToContext(), ToConditionVariable, ToCond,
                           ToBody, ToWhileLoc, ToLParenLoc, ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitDoStmt(DoStmt *S) {

  Error Err = Error::success();
  auto ToBody = importChecked(Err, S->getBody());
  auto ToCond = importChecked(Err, S->getCond());
  auto ToDoLoc = importChecked(Err, S->getDoLoc());
  auto ToWhileLoc = importChecked(Err, S->getWhileLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) DoStmt(
      ToBody, ToCond, ToDoLoc, ToWhileLoc, ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitForStmt(ForStmt *S) {

  Error Err = Error::success();
  auto ToInit = importChecked(Err, S->getInit());
  auto ToCond = importChecked(Err, S->getCond());
  auto ToConditionVariable = importChecked(Err, S->getConditionVariable());
  auto ToInc = importChecked(Err, S->getInc());
  auto ToBody = importChecked(Err, S->getBody());
  auto ToForLoc = importChecked(Err, S->getForLoc());
  auto ToLParenLoc = importChecked(Err, S->getLParenLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ForStmt(
      Importer.getToContext(),
      ToInit, ToCond, ToConditionVariable, ToInc, ToBody, ToForLoc, ToLParenLoc,
      ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitGotoStmt(GotoStmt *S) {

  Error Err = Error::success();
  auto ToLabel = importChecked(Err, S->getLabel());
  auto ToGotoLoc = importChecked(Err, S->getGotoLoc());
  auto ToLabelLoc = importChecked(Err, S->getLabelLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) GotoStmt(
      ToLabel, ToGotoLoc, ToLabelLoc);
}

ExpectedStmt ASTNodeImporter::VisitIndirectGotoStmt(IndirectGotoStmt *S) {

  Error Err = Error::success();
  auto ToGotoLoc = importChecked(Err, S->getGotoLoc());
  auto ToStarLoc = importChecked(Err, S->getStarLoc());
  auto ToTarget = importChecked(Err, S->getTarget());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) IndirectGotoStmt(
      ToGotoLoc, ToStarLoc, ToTarget);
}

ExpectedStmt ASTNodeImporter::VisitContinueStmt(ContinueStmt *S) {
  ExpectedSLoc ToContinueLocOrErr = import(S->getContinueLoc());
  if (!ToContinueLocOrErr)
    return ToContinueLocOrErr.takeError();
  return new (Importer.getToContext()) ContinueStmt(*ToContinueLocOrErr);
}

ExpectedStmt ASTNodeImporter::VisitBreakStmt(BreakStmt *S) {
  auto ToBreakLocOrErr = import(S->getBreakLoc());
  if (!ToBreakLocOrErr)
    return ToBreakLocOrErr.takeError();
  return new (Importer.getToContext()) BreakStmt(*ToBreakLocOrErr);
}

ExpectedStmt ASTNodeImporter::VisitReturnStmt(ReturnStmt *S) {

  Error Err = Error::success();
  auto ToReturnLoc = importChecked(Err, S->getReturnLoc());
  auto ToRetValue = importChecked(Err, S->getRetValue());
  auto ToNRVOCandidate = importChecked(Err, S->getNRVOCandidate());
  if (Err)
    return std::move(Err);

  return ReturnStmt::Create(Importer.getToContext(), ToReturnLoc, ToRetValue,
                            ToNRVOCandidate);
}

ExpectedStmt ASTNodeImporter::VisitCXXCatchStmt(CXXCatchStmt *S) {

  Error Err = Error::success();
  auto ToCatchLoc = importChecked(Err, S->getCatchLoc());
  auto ToExceptionDecl = importChecked(Err, S->getExceptionDecl());
  auto ToHandlerBlock = importChecked(Err, S->getHandlerBlock());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) CXXCatchStmt (
      ToCatchLoc, ToExceptionDecl, ToHandlerBlock);
}

ExpectedStmt ASTNodeImporter::VisitCXXTryStmt(CXXTryStmt *S) {
  ExpectedSLoc ToTryLocOrErr = import(S->getTryLoc());
  if (!ToTryLocOrErr)
    return ToTryLocOrErr.takeError();

  ExpectedStmt ToTryBlockOrErr = import(S->getTryBlock());
  if (!ToTryBlockOrErr)
    return ToTryBlockOrErr.takeError();

  SmallVector<Stmt *, 1> ToHandlers(S->getNumHandlers());
  for (unsigned HI = 0, HE = S->getNumHandlers(); HI != HE; ++HI) {
    CXXCatchStmt *FromHandler = S->getHandler(HI);
    if (auto ToHandlerOrErr = import(FromHandler))
      ToHandlers[HI] = *ToHandlerOrErr;
    else
      return ToHandlerOrErr.takeError();
  }

  return CXXTryStmt::Create(Importer.getToContext(), *ToTryLocOrErr,
                            cast<CompoundStmt>(*ToTryBlockOrErr), ToHandlers);
}

ExpectedStmt ASTNodeImporter::VisitCXXForRangeStmt(CXXForRangeStmt *S) {

  Error Err = Error::success();
  auto ToInit = importChecked(Err, S->getInit());
  auto ToRangeStmt = importChecked(Err, S->getRangeStmt());
  auto ToBeginStmt = importChecked(Err, S->getBeginStmt());
  auto ToEndStmt = importChecked(Err, S->getEndStmt());
  auto ToCond = importChecked(Err, S->getCond());
  auto ToInc = importChecked(Err, S->getInc());
  auto ToLoopVarStmt = importChecked(Err, S->getLoopVarStmt());
  auto ToBody = importChecked(Err, S->getBody());
  auto ToForLoc = importChecked(Err, S->getForLoc());
  auto ToCoawaitLoc = importChecked(Err, S->getCoawaitLoc());
  auto ToColonLoc = importChecked(Err, S->getColonLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) CXXForRangeStmt(
      ToInit, ToRangeStmt, ToBeginStmt, ToEndStmt, ToCond, ToInc, ToLoopVarStmt,
      ToBody, ToForLoc, ToCoawaitLoc, ToColonLoc, ToRParenLoc);
}

ExpectedStmt
ASTNodeImporter::VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
  Error Err = Error::success();
  auto ToElement = importChecked(Err, S->getElement());
  auto ToCollection = importChecked(Err, S->getCollection());
  auto ToBody = importChecked(Err, S->getBody());
  auto ToForLoc = importChecked(Err, S->getForLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ObjCForCollectionStmt(ToElement,
                                                             ToCollection,
                                                             ToBody,
                                                             ToForLoc,
                                                             ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitObjCAtCatchStmt(ObjCAtCatchStmt *S) {

  Error Err = Error::success();
  auto ToAtCatchLoc = importChecked(Err, S->getAtCatchLoc());
  auto ToRParenLoc = importChecked(Err, S->getRParenLoc());
  auto ToCatchParamDecl = importChecked(Err, S->getCatchParamDecl());
  auto ToCatchBody = importChecked(Err, S->getCatchBody());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ObjCAtCatchStmt (
      ToAtCatchLoc, ToRParenLoc, ToCatchParamDecl, ToCatchBody);
}

ExpectedStmt ASTNodeImporter::VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
  ExpectedSLoc ToAtFinallyLocOrErr = import(S->getAtFinallyLoc());
  if (!ToAtFinallyLocOrErr)
    return ToAtFinallyLocOrErr.takeError();
  ExpectedStmt ToAtFinallyStmtOrErr = import(S->getFinallyBody());
  if (!ToAtFinallyStmtOrErr)
    return ToAtFinallyStmtOrErr.takeError();
  return new (Importer.getToContext()) ObjCAtFinallyStmt(*ToAtFinallyLocOrErr,
                                                         *ToAtFinallyStmtOrErr);
}

ExpectedStmt ASTNodeImporter::VisitObjCAtTryStmt(ObjCAtTryStmt *S) {

  Error Err = Error::success();
  auto ToAtTryLoc = importChecked(Err, S->getAtTryLoc());
  auto ToTryBody = importChecked(Err, S->getTryBody());
  auto ToFinallyStmt = importChecked(Err, S->getFinallyStmt());
  if (Err)
    return std::move(Err);

  SmallVector<Stmt *, 1> ToCatchStmts(S->getNumCatchStmts());
  for (unsigned CI = 0, CE = S->getNumCatchStmts(); CI != CE; ++CI) {
    ObjCAtCatchStmt *FromCatchStmt = S->getCatchStmt(CI);
    if (ExpectedStmt ToCatchStmtOrErr = import(FromCatchStmt))
      ToCatchStmts[CI] = *ToCatchStmtOrErr;
    else
      return ToCatchStmtOrErr.takeError();
  }

  return ObjCAtTryStmt::Create(Importer.getToContext(),
                               ToAtTryLoc, ToTryBody,
                               ToCatchStmts.begin(), ToCatchStmts.size(),
                               ToFinallyStmt);
}

ExpectedStmt
ASTNodeImporter::VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S) {

  Error Err = Error::success();
  auto ToAtSynchronizedLoc = importChecked(Err, S->getAtSynchronizedLoc());
  auto ToSynchExpr = importChecked(Err, S->getSynchExpr());
  auto ToSynchBody = importChecked(Err, S->getSynchBody());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ObjCAtSynchronizedStmt(
    ToAtSynchronizedLoc, ToSynchExpr, ToSynchBody);
}

ExpectedStmt ASTNodeImporter::VisitObjCAtThrowStmt(ObjCAtThrowStmt *S) {
  ExpectedSLoc ToThrowLocOrErr = import(S->getThrowLoc());
  if (!ToThrowLocOrErr)
    return ToThrowLocOrErr.takeError();
  ExpectedExpr ToThrowExprOrErr = import(S->getThrowExpr());
  if (!ToThrowExprOrErr)
    return ToThrowExprOrErr.takeError();
  return new (Importer.getToContext()) ObjCAtThrowStmt(
      *ToThrowLocOrErr, *ToThrowExprOrErr);
}

ExpectedStmt ASTNodeImporter::VisitObjCAutoreleasePoolStmt(
    ObjCAutoreleasePoolStmt *S) {
  ExpectedSLoc ToAtLocOrErr = import(S->getAtLoc());
  if (!ToAtLocOrErr)
    return ToAtLocOrErr.takeError();
  ExpectedStmt ToSubStmtOrErr = import(S->getSubStmt());
  if (!ToSubStmtOrErr)
    return ToSubStmtOrErr.takeError();
  return new (Importer.getToContext()) ObjCAutoreleasePoolStmt(*ToAtLocOrErr,
                                                               *ToSubStmtOrErr);
}

//----------------------------------------------------------------------------
// Import Expressions
//----------------------------------------------------------------------------
ExpectedStmt ASTNodeImporter::VisitExpr(Expr *E) {
  Importer.FromDiag(E->getBeginLoc(), diag::err_unsupported_ast_node)
      << E->getStmtClassName();
  return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
}

ExpectedStmt ASTNodeImporter::VisitSourceLocExpr(SourceLocExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto BLoc = importChecked(Err, E->getBeginLoc());
  auto RParenLoc = importChecked(Err, E->getEndLoc());
  if (Err)
    return std::move(Err);
  auto ParentContextOrErr = Importer.ImportContext(E->getParentContext());
  if (!ParentContextOrErr)
    return ParentContextOrErr.takeError();

  return new (Importer.getToContext())
      SourceLocExpr(Importer.getToContext(), E->getIdentKind(), ToType, BLoc,
                    RParenLoc, *ParentContextOrErr);
}

ExpectedStmt ASTNodeImporter::VisitVAArgExpr(VAArgExpr *E) {

  Error Err = Error::success();
  auto ToBuiltinLoc = importChecked(Err, E->getBuiltinLoc());
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  auto ToWrittenTypeInfo = importChecked(Err, E->getWrittenTypeInfo());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) VAArgExpr(
      ToBuiltinLoc, ToSubExpr, ToWrittenTypeInfo, ToRParenLoc, ToType,
      E->isMicrosoftABI());
}

ExpectedStmt ASTNodeImporter::VisitChooseExpr(ChooseExpr *E) {

  Error Err = Error::success();
  auto ToCond = importChecked(Err, E->getCond());
  auto ToLHS = importChecked(Err, E->getLHS());
  auto ToRHS = importChecked(Err, E->getRHS());
  auto ToBuiltinLoc = importChecked(Err, E->getBuiltinLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  ExprValueKind VK = E->getValueKind();
  ExprObjectKind OK = E->getObjectKind();

  // The value of CondIsTrue only matters if the value is not
  // condition-dependent.
  bool CondIsTrue = !E->isConditionDependent() && E->isConditionTrue();

  return new (Importer.getToContext())
      ChooseExpr(ToBuiltinLoc, ToCond, ToLHS, ToRHS, ToType, VK, OK,
                 ToRParenLoc, CondIsTrue);
}

ExpectedStmt ASTNodeImporter::VisitConvertVectorExpr(ConvertVectorExpr *E) {
  Error Err = Error::success();
  auto *ToSrcExpr = importChecked(Err, E->getSrcExpr());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  auto ToBuiltinLoc = importChecked(Err, E->getBuiltinLoc());
  auto ToType = importChecked(Err, E->getType());
  auto *ToTSI = importChecked(Err, E->getTypeSourceInfo());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext())
      ConvertVectorExpr(ToSrcExpr, ToTSI, ToType, E->getValueKind(),
                        E->getObjectKind(), ToBuiltinLoc, ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  Error Err = Error::success();
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  auto ToBeginLoc = importChecked(Err, E->getBeginLoc());
  auto ToType = importChecked(Err, E->getType());
  const unsigned NumSubExprs = E->getNumSubExprs();

  llvm::SmallVector<Expr *, 8> ToSubExprs;
  llvm::ArrayRef<Expr *> FromSubExprs(E->getSubExprs(), NumSubExprs);
  ToSubExprs.resize(NumSubExprs);

  if ((Err = ImportContainerChecked(FromSubExprs, ToSubExprs)))
    return std::move(Err);

  return new (Importer.getToContext()) ShuffleVectorExpr(
      Importer.getToContext(), ToSubExprs, ToType, ToBeginLoc, ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitGNUNullExpr(GNUNullExpr *E) {
  ExpectedType TypeOrErr = import(E->getType());
  if (!TypeOrErr)
    return TypeOrErr.takeError();

  ExpectedSLoc BeginLocOrErr = import(E->getBeginLoc());
  if (!BeginLocOrErr)
    return BeginLocOrErr.takeError();

  return new (Importer.getToContext()) GNUNullExpr(*TypeOrErr, *BeginLocOrErr);
}

ExpectedStmt
ASTNodeImporter::VisitGenericSelectionExpr(GenericSelectionExpr *E) {
  Error Err = Error::success();
  auto ToGenericLoc = importChecked(Err, E->getGenericLoc());
  Expr *ToControllingExpr = nullptr;
  TypeSourceInfo *ToControllingType = nullptr;
  if (E->isExprPredicate())
    ToControllingExpr = importChecked(Err, E->getControllingExpr());
  else
    ToControllingType = importChecked(Err, E->getControllingType());
  assert((ToControllingExpr || ToControllingType) &&
         "Either the controlling expr or type must be nonnull");
  auto ToDefaultLoc = importChecked(Err, E->getDefaultLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  ArrayRef<const TypeSourceInfo *> FromAssocTypes(E->getAssocTypeSourceInfos());
  SmallVector<TypeSourceInfo *, 1> ToAssocTypes(FromAssocTypes.size());
  if (Error Err = ImportContainerChecked(FromAssocTypes, ToAssocTypes))
    return std::move(Err);

  ArrayRef<const Expr *> FromAssocExprs(E->getAssocExprs());
  SmallVector<Expr *, 1> ToAssocExprs(FromAssocExprs.size());
  if (Error Err = ImportContainerChecked(FromAssocExprs, ToAssocExprs))
    return std::move(Err);

  const ASTContext &ToCtx = Importer.getToContext();
  if (E->isResultDependent()) {
    if (ToControllingExpr) {
      return GenericSelectionExpr::Create(
          ToCtx, ToGenericLoc, ToControllingExpr, llvm::ArrayRef(ToAssocTypes),
          llvm::ArrayRef(ToAssocExprs), ToDefaultLoc, ToRParenLoc,
          E->containsUnexpandedParameterPack());
    }
    return GenericSelectionExpr::Create(
        ToCtx, ToGenericLoc, ToControllingType, llvm::ArrayRef(ToAssocTypes),
        llvm::ArrayRef(ToAssocExprs), ToDefaultLoc, ToRParenLoc,
        E->containsUnexpandedParameterPack());
  }

  if (ToControllingExpr) {
    return GenericSelectionExpr::Create(
        ToCtx, ToGenericLoc, ToControllingExpr, llvm::ArrayRef(ToAssocTypes),
        llvm::ArrayRef(ToAssocExprs), ToDefaultLoc, ToRParenLoc,
        E->containsUnexpandedParameterPack(), E->getResultIndex());
  }
  return GenericSelectionExpr::Create(
      ToCtx, ToGenericLoc, ToControllingType, llvm::ArrayRef(ToAssocTypes),
      llvm::ArrayRef(ToAssocExprs), ToDefaultLoc, ToRParenLoc,
      E->containsUnexpandedParameterPack(), E->getResultIndex());
}

ExpectedStmt ASTNodeImporter::VisitPredefinedExpr(PredefinedExpr *E) {

  Error Err = Error::success();
  auto ToBeginLoc = importChecked(Err, E->getBeginLoc());
  auto ToType = importChecked(Err, E->getType());
  auto ToFunctionName = importChecked(Err, E->getFunctionName());
  if (Err)
    return std::move(Err);

  return PredefinedExpr::Create(Importer.getToContext(), ToBeginLoc, ToType,
                                E->getIdentKind(), E->isTransparent(),
                                ToFunctionName);
}

ExpectedStmt ASTNodeImporter::VisitDeclRefExpr(DeclRefExpr *E) {

  Error Err = Error::success();
  auto ToQualifierLoc = importChecked(Err, E->getQualifierLoc());
  auto ToTemplateKeywordLoc = importChecked(Err, E->getTemplateKeywordLoc());
  auto ToDecl = importChecked(Err, E->getDecl());
  auto ToLocation = importChecked(Err, E->getLocation());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  NamedDecl *ToFoundD = nullptr;
  if (E->getDecl() != E->getFoundDecl()) {
    auto FoundDOrErr = import(E->getFoundDecl());
    if (!FoundDOrErr)
      return FoundDOrErr.takeError();
    ToFoundD = *FoundDOrErr;
  }

  TemplateArgumentListInfo ToTAInfo;
  TemplateArgumentListInfo *ToResInfo = nullptr;
  if (E->hasExplicitTemplateArgs()) {
    if (Error Err =
            ImportTemplateArgumentListInfo(E->getLAngleLoc(), E->getRAngleLoc(),
                                           E->template_arguments(), ToTAInfo))
      return std::move(Err);
    ToResInfo = &ToTAInfo;
  }

  auto *ToE = DeclRefExpr::Create(
      Importer.getToContext(), ToQualifierLoc, ToTemplateKeywordLoc, ToDecl,
      E->refersToEnclosingVariableOrCapture(), ToLocation, ToType,
      E->getValueKind(), ToFoundD, ToResInfo, E->isNonOdrUse());
  if (E->hadMultipleCandidates())
    ToE->setHadMultipleCandidates(true);
  ToE->setIsImmediateEscalating(E->isImmediateEscalating());
  return ToE;
}

ExpectedStmt ASTNodeImporter::VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
  ExpectedType TypeOrErr = import(E->getType());
  if (!TypeOrErr)
    return TypeOrErr.takeError();

  return new (Importer.getToContext()) ImplicitValueInitExpr(*TypeOrErr);
}

ExpectedStmt ASTNodeImporter::VisitDesignatedInitExpr(DesignatedInitExpr *E) {
  ExpectedExpr ToInitOrErr = import(E->getInit());
  if (!ToInitOrErr)
    return ToInitOrErr.takeError();

  ExpectedSLoc ToEqualOrColonLocOrErr = import(E->getEqualOrColonLoc());
  if (!ToEqualOrColonLocOrErr)
    return ToEqualOrColonLocOrErr.takeError();

  SmallVector<Expr *, 4> ToIndexExprs(E->getNumSubExprs() - 1);
  // List elements from the second, the first is Init itself
  for (unsigned I = 1, N = E->getNumSubExprs(); I < N; I++) {
    if (ExpectedExpr ToArgOrErr = import(E->getSubExpr(I)))
      ToIndexExprs[I - 1] = *ToArgOrErr;
    else
      return ToArgOrErr.takeError();
  }

  SmallVector<Designator, 4> ToDesignators(E->size());
  if (Error Err = ImportContainerChecked(E->designators(), ToDesignators))
    return std::move(Err);

  return DesignatedInitExpr::Create(
        Importer.getToContext(), ToDesignators,
        ToIndexExprs, *ToEqualOrColonLocOrErr,
        E->usesGNUSyntax(), *ToInitOrErr);
}

ExpectedStmt
ASTNodeImporter::VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedSLoc ToLocationOrErr = import(E->getLocation());
  if (!ToLocationOrErr)
    return ToLocationOrErr.takeError();

  return new (Importer.getToContext()) CXXNullPtrLiteralExpr(
      *ToTypeOrErr, *ToLocationOrErr);
}

ExpectedStmt ASTNodeImporter::VisitIntegerLiteral(IntegerLiteral *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedSLoc ToLocationOrErr = import(E->getLocation());
  if (!ToLocationOrErr)
    return ToLocationOrErr.takeError();

  return IntegerLiteral::Create(
      Importer.getToContext(), E->getValue(), *ToTypeOrErr, *ToLocationOrErr);
}


ExpectedStmt ASTNodeImporter::VisitFloatingLiteral(FloatingLiteral *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedSLoc ToLocationOrErr = import(E->getLocation());
  if (!ToLocationOrErr)
    return ToLocationOrErr.takeError();

  return FloatingLiteral::Create(
      Importer.getToContext(), E->getValue(), E->isExact(),
      *ToTypeOrErr, *ToLocationOrErr);
}

ExpectedStmt ASTNodeImporter::VisitImaginaryLiteral(ImaginaryLiteral *E) {
  auto ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedExpr ToSubExprOrErr = import(E->getSubExpr());
  if (!ToSubExprOrErr)
    return ToSubExprOrErr.takeError();

  return new (Importer.getToContext()) ImaginaryLiteral(
      *ToSubExprOrErr, *ToTypeOrErr);
}

ExpectedStmt ASTNodeImporter::VisitFixedPointLiteral(FixedPointLiteral *E) {
  auto ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedSLoc ToLocationOrErr = import(E->getLocation());
  if (!ToLocationOrErr)
    return ToLocationOrErr.takeError();

  return new (Importer.getToContext()) FixedPointLiteral(
      Importer.getToContext(), E->getValue(), *ToTypeOrErr, *ToLocationOrErr,
      Importer.getToContext().getFixedPointScale(*ToTypeOrErr));
}

ExpectedStmt ASTNodeImporter::VisitCharacterLiteral(CharacterLiteral *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedSLoc ToLocationOrErr = import(E->getLocation());
  if (!ToLocationOrErr)
    return ToLocationOrErr.takeError();

  return new (Importer.getToContext()) CharacterLiteral(
      E->getValue(), E->getKind(), *ToTypeOrErr, *ToLocationOrErr);
}

ExpectedStmt ASTNodeImporter::VisitStringLiteral(StringLiteral *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  SmallVector<SourceLocation, 4> ToLocations(E->getNumConcatenated());
  if (Error Err = ImportArrayChecked(
      E->tokloc_begin(), E->tokloc_end(), ToLocations.begin()))
    return std::move(Err);

  return StringLiteral::Create(
      Importer.getToContext(), E->getBytes(), E->getKind(), E->isPascal(),
      *ToTypeOrErr, ToLocations.data(), ToLocations.size());
}

ExpectedStmt ASTNodeImporter::VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {

  Error Err = Error::success();
  auto ToLParenLoc = importChecked(Err, E->getLParenLoc());
  auto ToTypeSourceInfo = importChecked(Err, E->getTypeSourceInfo());
  auto ToType = importChecked(Err, E->getType());
  auto ToInitializer = importChecked(Err, E->getInitializer());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) CompoundLiteralExpr(
        ToLParenLoc, ToTypeSourceInfo, ToType, E->getValueKind(),
        ToInitializer, E->isFileScope());
}

ExpectedStmt ASTNodeImporter::VisitAtomicExpr(AtomicExpr *E) {

  Error Err = Error::success();
  auto ToBuiltinLoc = importChecked(Err, E->getBuiltinLoc());
  auto ToType = importChecked(Err, E->getType());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  SmallVector<Expr *, 6> ToExprs(E->getNumSubExprs());
  if (Error Err = ImportArrayChecked(
      E->getSubExprs(), E->getSubExprs() + E->getNumSubExprs(),
      ToExprs.begin()))
    return std::move(Err);

  return new (Importer.getToContext()) AtomicExpr(

      ToBuiltinLoc, ToExprs, ToType, E->getOp(), ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitAddrLabelExpr(AddrLabelExpr *E) {
  Error Err = Error::success();
  auto ToAmpAmpLoc = importChecked(Err, E->getAmpAmpLoc());
  auto ToLabelLoc = importChecked(Err, E->getLabelLoc());
  auto ToLabel = importChecked(Err, E->getLabel());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) AddrLabelExpr(
      ToAmpAmpLoc, ToLabelLoc, ToLabel, ToType);
}
ExpectedStmt ASTNodeImporter::VisitConstantExpr(ConstantExpr *E) {
  Error Err = Error::success();
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  auto ToResult = importChecked(Err, E->getAPValueResult());
  if (Err)
    return std::move(Err);

  return ConstantExpr::Create(Importer.getToContext(), ToSubExpr, ToResult);
}
ExpectedStmt ASTNodeImporter::VisitParenExpr(ParenExpr *E) {
  Error Err = Error::success();
  auto ToLParen = importChecked(Err, E->getLParen());
  auto ToRParen = importChecked(Err, E->getRParen());
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext())
      ParenExpr(ToLParen, ToRParen, ToSubExpr);
}

ExpectedStmt ASTNodeImporter::VisitParenListExpr(ParenListExpr *E) {
  SmallVector<Expr *, 4> ToExprs(E->getNumExprs());
  if (Error Err = ImportContainerChecked(E->exprs(), ToExprs))
    return std::move(Err);

  ExpectedSLoc ToLParenLocOrErr = import(E->getLParenLoc());
  if (!ToLParenLocOrErr)
    return ToLParenLocOrErr.takeError();

  ExpectedSLoc ToRParenLocOrErr = import(E->getRParenLoc());
  if (!ToRParenLocOrErr)
    return ToRParenLocOrErr.takeError();

  return ParenListExpr::Create(Importer.getToContext(), *ToLParenLocOrErr,
                               ToExprs, *ToRParenLocOrErr);
}

ExpectedStmt ASTNodeImporter::VisitStmtExpr(StmtExpr *E) {
  Error Err = Error::success();
  auto ToSubStmt = importChecked(Err, E->getSubStmt());
  auto ToType = importChecked(Err, E->getType());
  auto ToLParenLoc = importChecked(Err, E->getLParenLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext())
      StmtExpr(ToSubStmt, ToType, ToLParenLoc, ToRParenLoc,
               E->getTemplateDepth());
}

ExpectedStmt ASTNodeImporter::VisitUnaryOperator(UnaryOperator *E) {
  Error Err = Error::success();
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  auto ToType = importChecked(Err, E->getType());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  if (Err)
    return std::move(Err);

  auto *UO = UnaryOperator::CreateEmpty(Importer.getToContext(),
                                        E->hasStoredFPFeatures());
  UO->setType(ToType);
  UO->setSubExpr(ToSubExpr);
  UO->setOpcode(E->getOpcode());
  UO->setOperatorLoc(ToOperatorLoc);
  UO->setCanOverflow(E->canOverflow());
  if (E->hasStoredFPFeatures())
    UO->setStoredFPFeatures(E->getStoredFPFeatures());

  return UO;
}

ExpectedStmt

ASTNodeImporter::VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  if (E->isArgumentType()) {
    Expected<TypeSourceInfo *> ToArgumentTypeInfoOrErr =
        import(E->getArgumentTypeInfo());
    if (!ToArgumentTypeInfoOrErr)
      return ToArgumentTypeInfoOrErr.takeError();

    return new (Importer.getToContext()) UnaryExprOrTypeTraitExpr(
        E->getKind(), *ToArgumentTypeInfoOrErr, ToType, ToOperatorLoc,
        ToRParenLoc);
  }

  ExpectedExpr ToArgumentExprOrErr = import(E->getArgumentExpr());
  if (!ToArgumentExprOrErr)
    return ToArgumentExprOrErr.takeError();

  return new (Importer.getToContext()) UnaryExprOrTypeTraitExpr(
      E->getKind(), *ToArgumentExprOrErr, ToType, ToOperatorLoc, ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitBinaryOperator(BinaryOperator *E) {
  Error Err = Error::success();
  auto ToLHS = importChecked(Err, E->getLHS());
  auto ToRHS = importChecked(Err, E->getRHS());
  auto ToType = importChecked(Err, E->getType());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  if (Err)
    return std::move(Err);

  return BinaryOperator::Create(
      Importer.getToContext(), ToLHS, ToRHS, E->getOpcode(), ToType,
      E->getValueKind(), E->getObjectKind(), ToOperatorLoc,
      E->getFPFeatures());
}

ExpectedStmt ASTNodeImporter::VisitConditionalOperator(ConditionalOperator *E) {
  Error Err = Error::success();
  auto ToCond = importChecked(Err, E->getCond());
  auto ToQuestionLoc = importChecked(Err, E->getQuestionLoc());
  auto ToLHS = importChecked(Err, E->getLHS());
  auto ToColonLoc = importChecked(Err, E->getColonLoc());
  auto ToRHS = importChecked(Err, E->getRHS());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ConditionalOperator(
      ToCond, ToQuestionLoc, ToLHS, ToColonLoc, ToRHS, ToType,
      E->getValueKind(), E->getObjectKind());
}

ExpectedStmt
ASTNodeImporter::VisitBinaryConditionalOperator(BinaryConditionalOperator *E) {
  Error Err = Error::success();
  auto ToCommon = importChecked(Err, E->getCommon());
  auto ToOpaqueValue = importChecked(Err, E->getOpaqueValue());
  auto ToCond = importChecked(Err, E->getCond());
  auto ToTrueExpr = importChecked(Err, E->getTrueExpr());
  auto ToFalseExpr = importChecked(Err, E->getFalseExpr());
  auto ToQuestionLoc = importChecked(Err, E->getQuestionLoc());
  auto ToColonLoc = importChecked(Err, E->getColonLoc());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) BinaryConditionalOperator(
      ToCommon, ToOpaqueValue, ToCond, ToTrueExpr, ToFalseExpr,
      ToQuestionLoc, ToColonLoc, ToType, E->getValueKind(),
      E->getObjectKind());
}

ExpectedStmt ASTNodeImporter::VisitCXXRewrittenBinaryOperator(
    CXXRewrittenBinaryOperator *E) {
  Error Err = Error::success();
  auto ToSemanticForm = importChecked(Err, E->getSemanticForm());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext())
      CXXRewrittenBinaryOperator(ToSemanticForm, E->isReversed());
}

ExpectedStmt ASTNodeImporter::VisitArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {
  Error Err = Error::success();
  auto ToBeginLoc = importChecked(Err, E->getBeginLoc());
  auto ToQueriedTypeSourceInfo =
      importChecked(Err, E->getQueriedTypeSourceInfo());
  auto ToDimensionExpression = importChecked(Err, E->getDimensionExpression());
  auto ToEndLoc = importChecked(Err, E->getEndLoc());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ArrayTypeTraitExpr(
      ToBeginLoc, E->getTrait(), ToQueriedTypeSourceInfo, E->getValue(),
      ToDimensionExpression, ToEndLoc, ToType);
}

ExpectedStmt ASTNodeImporter::VisitExpressionTraitExpr(ExpressionTraitExpr *E) {
  Error Err = Error::success();
  auto ToBeginLoc = importChecked(Err, E->getBeginLoc());
  auto ToQueriedExpression = importChecked(Err, E->getQueriedExpression());
  auto ToEndLoc = importChecked(Err, E->getEndLoc());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ExpressionTraitExpr(
      ToBeginLoc, E->getTrait(), ToQueriedExpression, E->getValue(),
      ToEndLoc, ToType);
}

ExpectedStmt ASTNodeImporter::VisitOpaqueValueExpr(OpaqueValueExpr *E) {
  Error Err = Error::success();
  auto ToLocation = importChecked(Err, E->getLocation());
  auto ToType = importChecked(Err, E->getType());
  auto ToSourceExpr = importChecked(Err, E->getSourceExpr());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) OpaqueValueExpr(
      ToLocation, ToType, E->getValueKind(), E->getObjectKind(), ToSourceExpr);
}

ExpectedStmt ASTNodeImporter::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  Error Err = Error::success();
  auto ToLHS = importChecked(Err, E->getLHS());
  auto ToRHS = importChecked(Err, E->getRHS());
  auto ToType = importChecked(Err, E->getType());
  auto ToRBracketLoc = importChecked(Err, E->getRBracketLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ArraySubscriptExpr(
      ToLHS, ToRHS, ToType, E->getValueKind(), E->getObjectKind(),
      ToRBracketLoc);
}

ExpectedStmt
ASTNodeImporter::VisitCompoundAssignOperator(CompoundAssignOperator *E) {
  Error Err = Error::success();
  auto ToLHS = importChecked(Err, E->getLHS());
  auto ToRHS = importChecked(Err, E->getRHS());
  auto ToType = importChecked(Err, E->getType());
  auto ToComputationLHSType = importChecked(Err, E->getComputationLHSType());
  auto ToComputationResultType =
      importChecked(Err, E->getComputationResultType());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  if (Err)
    return std::move(Err);

  return CompoundAssignOperator::Create(
      Importer.getToContext(), ToLHS, ToRHS, E->getOpcode(), ToType,
      E->getValueKind(), E->getObjectKind(), ToOperatorLoc,
      E->getFPFeatures(),
      ToComputationLHSType, ToComputationResultType);
}

Expected<CXXCastPath>
ASTNodeImporter::ImportCastPath(CastExpr *CE) {
  CXXCastPath Path;
  for (auto I = CE->path_begin(), E = CE->path_end(); I != E; ++I) {
    if (auto SpecOrErr = import(*I))
      Path.push_back(*SpecOrErr);
    else
      return SpecOrErr.takeError();
  }
  return Path;
}

ExpectedStmt ASTNodeImporter::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedExpr ToSubExprOrErr = import(E->getSubExpr());
  if (!ToSubExprOrErr)
    return ToSubExprOrErr.takeError();

  Expected<CXXCastPath> ToBasePathOrErr = ImportCastPath(E);
  if (!ToBasePathOrErr)
    return ToBasePathOrErr.takeError();

  return ImplicitCastExpr::Create(
      Importer.getToContext(), *ToTypeOrErr, E->getCastKind(), *ToSubExprOrErr,
      &(*ToBasePathOrErr), E->getValueKind(), E->getFPFeatures());
}

ExpectedStmt ASTNodeImporter::VisitExplicitCastExpr(ExplicitCastExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  auto ToTypeInfoAsWritten = importChecked(Err, E->getTypeInfoAsWritten());
  if (Err)
    return std::move(Err);

  Expected<CXXCastPath> ToBasePathOrErr = ImportCastPath(E);
  if (!ToBasePathOrErr)
    return ToBasePathOrErr.takeError();
  CXXCastPath *ToBasePath = &(*ToBasePathOrErr);

  switch (E->getStmtClass()) {
  case Stmt::CStyleCastExprClass: {
    auto *CCE = cast<CStyleCastExpr>(E);
    ExpectedSLoc ToLParenLocOrErr = import(CCE->getLParenLoc());
    if (!ToLParenLocOrErr)
      return ToLParenLocOrErr.takeError();
    ExpectedSLoc ToRParenLocOrErr = import(CCE->getRParenLoc());
    if (!ToRParenLocOrErr)
      return ToRParenLocOrErr.takeError();
    return CStyleCastExpr::Create(
        Importer.getToContext(), ToType, E->getValueKind(), E->getCastKind(),
        ToSubExpr, ToBasePath, CCE->getFPFeatures(), ToTypeInfoAsWritten,
        *ToLParenLocOrErr, *ToRParenLocOrErr);
  }

  case Stmt::CXXFunctionalCastExprClass: {
    auto *FCE = cast<CXXFunctionalCastExpr>(E);
    ExpectedSLoc ToLParenLocOrErr = import(FCE->getLParenLoc());
    if (!ToLParenLocOrErr)
      return ToLParenLocOrErr.takeError();
    ExpectedSLoc ToRParenLocOrErr = import(FCE->getRParenLoc());
    if (!ToRParenLocOrErr)
      return ToRParenLocOrErr.takeError();
    return CXXFunctionalCastExpr::Create(
        Importer.getToContext(), ToType, E->getValueKind(), ToTypeInfoAsWritten,
        E->getCastKind(), ToSubExpr, ToBasePath, FCE->getFPFeatures(),
        *ToLParenLocOrErr, *ToRParenLocOrErr);
  }

  case Stmt::ObjCBridgedCastExprClass: {
    auto *OCE = cast<ObjCBridgedCastExpr>(E);
    ExpectedSLoc ToLParenLocOrErr = import(OCE->getLParenLoc());
    if (!ToLParenLocOrErr)
      return ToLParenLocOrErr.takeError();
    ExpectedSLoc ToBridgeKeywordLocOrErr = import(OCE->getBridgeKeywordLoc());
    if (!ToBridgeKeywordLocOrErr)
      return ToBridgeKeywordLocOrErr.takeError();
    return new (Importer.getToContext()) ObjCBridgedCastExpr(
        *ToLParenLocOrErr, OCE->getBridgeKind(), E->getCastKind(),
        *ToBridgeKeywordLocOrErr, ToTypeInfoAsWritten, ToSubExpr);
  }
  case Stmt::BuiltinBitCastExprClass: {
    auto *BBC = cast<BuiltinBitCastExpr>(E);
    ExpectedSLoc ToKWLocOrErr = import(BBC->getBeginLoc());
    if (!ToKWLocOrErr)
      return ToKWLocOrErr.takeError();
    ExpectedSLoc ToRParenLocOrErr = import(BBC->getEndLoc());
    if (!ToRParenLocOrErr)
      return ToRParenLocOrErr.takeError();
    return new (Importer.getToContext()) BuiltinBitCastExpr(
        ToType, E->getValueKind(), E->getCastKind(), ToSubExpr,
        ToTypeInfoAsWritten, *ToKWLocOrErr, *ToRParenLocOrErr);
  }
  default:
    llvm_unreachable("Cast expression of unsupported type!");
    return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
  }
}

ExpectedStmt ASTNodeImporter::VisitOffsetOfExpr(OffsetOfExpr *E) {
  SmallVector<OffsetOfNode, 4> ToNodes;
  for (int I = 0, N = E->getNumComponents(); I < N; ++I) {
    const OffsetOfNode &FromNode = E->getComponent(I);

    SourceLocation ToBeginLoc, ToEndLoc;

    if (FromNode.getKind() != OffsetOfNode::Base) {
      Error Err = Error::success();
      ToBeginLoc = importChecked(Err, FromNode.getBeginLoc());
      ToEndLoc = importChecked(Err, FromNode.getEndLoc());
      if (Err)
        return std::move(Err);
    }

    switch (FromNode.getKind()) {
    case OffsetOfNode::Array:
      ToNodes.push_back(
          OffsetOfNode(ToBeginLoc, FromNode.getArrayExprIndex(), ToEndLoc));
      break;
    case OffsetOfNode::Base: {
      auto ToBSOrErr = import(FromNode.getBase());
      if (!ToBSOrErr)
        return ToBSOrErr.takeError();
      ToNodes.push_back(OffsetOfNode(*ToBSOrErr));
      break;
    }
    case OffsetOfNode::Field: {
      auto ToFieldOrErr = import(FromNode.getField());
      if (!ToFieldOrErr)
        return ToFieldOrErr.takeError();
      ToNodes.push_back(OffsetOfNode(ToBeginLoc, *ToFieldOrErr, ToEndLoc));
      break;
    }
    case OffsetOfNode::Identifier: {
      IdentifierInfo *ToII = Importer.Import(FromNode.getFieldName());
      ToNodes.push_back(OffsetOfNode(ToBeginLoc, ToII, ToEndLoc));
      break;
    }
    }
  }

  SmallVector<Expr *, 4> ToExprs(E->getNumExpressions());
  for (int I = 0, N = E->getNumExpressions(); I < N; ++I) {
    ExpectedExpr ToIndexExprOrErr = import(E->getIndexExpr(I));
    if (!ToIndexExprOrErr)
      return ToIndexExprOrErr.takeError();
    ToExprs[I] = *ToIndexExprOrErr;
  }

  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToTypeSourceInfo = importChecked(Err, E->getTypeSourceInfo());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  return OffsetOfExpr::Create(
      Importer.getToContext(), ToType, ToOperatorLoc, ToTypeSourceInfo, ToNodes,
      ToExprs, ToRParenLoc);
}

ExpectedStmt ASTNodeImporter::VisitCXXNoexceptExpr(CXXNoexceptExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToOperand = importChecked(Err, E->getOperand());
  auto ToBeginLoc = importChecked(Err, E->getBeginLoc());
  auto ToEndLoc = importChecked(Err, E->getEndLoc());
  if (Err)
    return std::move(Err);

  CanThrowResult ToCanThrow;
  if (E->isValueDependent())
    ToCanThrow = CT_Dependent;
  else
    ToCanThrow = E->getValue() ? CT_Can : CT_Cannot;

  return new (Importer.getToContext()) CXXNoexceptExpr(
      ToType, ToOperand, ToCanThrow, ToBeginLoc, ToEndLoc);
}

ExpectedStmt ASTNodeImporter::VisitCXXThrowExpr(CXXThrowExpr *E) {
  Error Err = Error::success();
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  auto ToType = importChecked(Err, E->getType());
  auto ToThrowLoc = importChecked(Err, E->getThrowLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) CXXThrowExpr(
      ToSubExpr, ToType, ToThrowLoc, E->isThrownVariableInScope());
}

ExpectedStmt ASTNodeImporter::VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
  ExpectedSLoc ToUsedLocOrErr = import(E->getUsedLocation());
  if (!ToUsedLocOrErr)
    return ToUsedLocOrErr.takeError();

  auto ToParamOrErr = import(E->getParam());
  if (!ToParamOrErr)
    return ToParamOrErr.takeError();

  auto UsedContextOrErr = Importer.ImportContext(E->getUsedContext());
  if (!UsedContextOrErr)
    return UsedContextOrErr.takeError();

  // Import the default arg if it was not imported yet.
  // This is needed because it can happen that during the import of the
  // default expression (from VisitParmVarDecl) the same ParmVarDecl is
  // encountered here. The default argument for a ParmVarDecl is set in the
  // ParmVarDecl only after it is imported (set in VisitParmVarDecl if not here,
  // see VisitParmVarDecl).
  ParmVarDecl *ToParam = *ToParamOrErr;
  if (!ToParam->getDefaultArg()) {
    std::optional<ParmVarDecl *> FromParam =
        Importer.getImportedFromDecl(ToParam);
    assert(FromParam && "ParmVarDecl was not imported?");

    if (Error Err = ImportDefaultArgOfParmVarDecl(*FromParam, ToParam))
      return std::move(Err);
  }
  Expr *RewrittenInit = nullptr;
  if (E->hasRewrittenInit()) {
    ExpectedExpr ExprOrErr = import(E->getRewrittenExpr());
    if (!ExprOrErr)
      return ExprOrErr.takeError();
    RewrittenInit = ExprOrErr.get();
  }
  return CXXDefaultArgExpr::Create(Importer.getToContext(), *ToUsedLocOrErr,
                                   *ToParamOrErr, RewrittenInit,
                                   *UsedContextOrErr);
}

ExpectedStmt
ASTNodeImporter::VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToTypeSourceInfo = importChecked(Err, E->getTypeSourceInfo());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) CXXScalarValueInitExpr(
      ToType, ToTypeSourceInfo, ToRParenLoc);
}

ExpectedStmt
ASTNodeImporter::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
  ExpectedExpr ToSubExprOrErr = import(E->getSubExpr());
  if (!ToSubExprOrErr)
    return ToSubExprOrErr.takeError();

  auto ToDtorOrErr = import(E->getTemporary()->getDestructor());
  if (!ToDtorOrErr)
    return ToDtorOrErr.takeError();

  ASTContext &ToCtx = Importer.getToContext();
  CXXTemporary *Temp = CXXTemporary::Create(ToCtx, *ToDtorOrErr);
  return CXXBindTemporaryExpr::Create(ToCtx, Temp, *ToSubExprOrErr);
}

ExpectedStmt

ASTNodeImporter::VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *E) {
  Error Err = Error::success();
  auto ToConstructor = importChecked(Err, E->getConstructor());
  auto ToType = importChecked(Err, E->getType());
  auto ToTypeSourceInfo = importChecked(Err, E->getTypeSourceInfo());
  auto ToParenOrBraceRange = importChecked(Err, E->getParenOrBraceRange());
  if (Err)
    return std::move(Err);

  SmallVector<Expr *, 8> ToArgs(E->getNumArgs());
  if (Error Err = ImportContainerChecked(E->arguments(), ToArgs))
    return std::move(Err);

  return CXXTemporaryObjectExpr::Create(
      Importer.getToContext(), ToConstructor, ToType, ToTypeSourceInfo, ToArgs,
      ToParenOrBraceRange, E->hadMultipleCandidates(),
      E->isListInitialization(), E->isStdInitListInitialization(),
      E->requiresZeroInitialization());
}

ExpectedDecl ASTNodeImporter::VisitLifetimeExtendedTemporaryDecl(
    LifetimeExtendedTemporaryDecl *D) {
  DeclContext *DC, *LexicalDC;
  if (Error Err = ImportDeclContext(D, DC, LexicalDC))
    return std::move(Err);

  Error Err = Error::success();
  auto Temporary = importChecked(Err, D->getTemporaryExpr());
  auto ExtendingDecl = importChecked(Err, D->getExtendingDecl());
  if (Err)
    return std::move(Err);
  // FIXME: Should ManglingNumber get numbers associated with 'to' context?

  LifetimeExtendedTemporaryDecl *To;
  if (GetImportedOrCreateDecl(To, D, Temporary, ExtendingDecl,
                              D->getManglingNumber()))
    return To;

  To->setLexicalDeclContext(LexicalDC);
  LexicalDC->addDeclInternal(To);
  return To;
}

ExpectedStmt
ASTNodeImporter::VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  Expr *ToTemporaryExpr = importChecked(
      Err, E->getLifetimeExtendedTemporaryDecl() ? nullptr : E->getSubExpr());
  auto ToMaterializedDecl =
      importChecked(Err, E->getLifetimeExtendedTemporaryDecl());
  if (Err)
    return std::move(Err);

  if (!ToTemporaryExpr)
    ToTemporaryExpr = cast<Expr>(ToMaterializedDecl->getTemporaryExpr());

  auto *ToMTE = new (Importer.getToContext()) MaterializeTemporaryExpr(
      ToType, ToTemporaryExpr, E->isBoundToLvalueReference(),
      ToMaterializedDecl);

  return ToMTE;
}

ExpectedStmt ASTNodeImporter::VisitPackExpansionExpr(PackExpansionExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToPattern = importChecked(Err, E->getPattern());
  auto ToEllipsisLoc = importChecked(Err, E->getEllipsisLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) PackExpansionExpr(
      ToType, ToPattern, ToEllipsisLoc, E->getNumExpansions());
}

ExpectedStmt ASTNodeImporter::VisitSizeOfPackExpr(SizeOfPackExpr *E) {
  Error Err = Error::success();
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToPack = importChecked(Err, E->getPack());
  auto ToPackLoc = importChecked(Err, E->getPackLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  std::optional<unsigned> Length;
  if (!E->isValueDependent())
    Length = E->getPackLength();

  SmallVector<TemplateArgument, 8> ToPartialArguments;
  if (E->isPartiallySubstituted()) {
    if (Error Err = ImportTemplateArguments(E->getPartialArguments(),
                                            ToPartialArguments))
      return std::move(Err);
  }

  return SizeOfPackExpr::Create(
      Importer.getToContext(), ToOperatorLoc, ToPack, ToPackLoc, ToRParenLoc,
      Length, ToPartialArguments);
}


ExpectedStmt ASTNodeImporter::VisitCXXNewExpr(CXXNewExpr *E) {
  Error Err = Error::success();
  auto ToOperatorNew = importChecked(Err, E->getOperatorNew());
  auto ToOperatorDelete = importChecked(Err, E->getOperatorDelete());
  auto ToTypeIdParens = importChecked(Err, E->getTypeIdParens());
  auto ToArraySize = importChecked(Err, E->getArraySize());
  auto ToInitializer = importChecked(Err, E->getInitializer());
  auto ToType = importChecked(Err, E->getType());
  auto ToAllocatedTypeSourceInfo =
      importChecked(Err, E->getAllocatedTypeSourceInfo());
  auto ToSourceRange = importChecked(Err, E->getSourceRange());
  auto ToDirectInitRange = importChecked(Err, E->getDirectInitRange());
  if (Err)
    return std::move(Err);

  SmallVector<Expr *, 4> ToPlacementArgs(E->getNumPlacementArgs());
  if (Error Err =
      ImportContainerChecked(E->placement_arguments(), ToPlacementArgs))
    return std::move(Err);

  return CXXNewExpr::Create(
      Importer.getToContext(), E->isGlobalNew(), ToOperatorNew,
      ToOperatorDelete, E->passAlignment(), E->doesUsualArrayDeleteWantSize(),
      ToPlacementArgs, ToTypeIdParens, ToArraySize, E->getInitializationStyle(),
      ToInitializer, ToType, ToAllocatedTypeSourceInfo, ToSourceRange,
      ToDirectInitRange);
}

ExpectedStmt ASTNodeImporter::VisitCXXDeleteExpr(CXXDeleteExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToOperatorDelete = importChecked(Err, E->getOperatorDelete());
  auto ToArgument = importChecked(Err, E->getArgument());
  auto ToBeginLoc = importChecked(Err, E->getBeginLoc());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) CXXDeleteExpr(
      ToType, E->isGlobalDelete(), E->isArrayForm(), E->isArrayFormAsWritten(),
      E->doesUsualArrayDeleteWantSize(), ToOperatorDelete, ToArgument,
      ToBeginLoc);
}

ExpectedStmt ASTNodeImporter::VisitCXXConstructExpr(CXXConstructExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToLocation = importChecked(Err, E->getLocation());
  auto ToConstructor = importChecked(Err, E->getConstructor());
  auto ToParenOrBraceRange = importChecked(Err, E->getParenOrBraceRange());
  if (Err)
    return std::move(Err);

  SmallVector<Expr *, 6> ToArgs(E->getNumArgs());
  if (Error Err = ImportContainerChecked(E->arguments(), ToArgs))
    return std::move(Err);

  CXXConstructExpr *ToE = CXXConstructExpr::Create(
      Importer.getToContext(), ToType, ToLocation, ToConstructor,
      E->isElidable(), ToArgs, E->hadMultipleCandidates(),
      E->isListInitialization(), E->isStdInitListInitialization(),
      E->requiresZeroInitialization(), E->getConstructionKind(),
      ToParenOrBraceRange);
  ToE->setIsImmediateEscalating(E->isImmediateEscalating());
  return ToE;
}

ExpectedStmt ASTNodeImporter::VisitExprWithCleanups(ExprWithCleanups *E) {
  ExpectedExpr ToSubExprOrErr = import(E->getSubExpr());
  if (!ToSubExprOrErr)
    return ToSubExprOrErr.takeError();

  SmallVector<ExprWithCleanups::CleanupObject, 8> ToObjects(E->getNumObjects());
  if (Error Err = ImportContainerChecked(E->getObjects(), ToObjects))
    return std::move(Err);

  return ExprWithCleanups::Create(
      Importer.getToContext(), *ToSubExprOrErr, E->cleanupsHaveSideEffects(),
      ToObjects);
}

ExpectedStmt ASTNodeImporter::VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
  Error Err = Error::success();
  auto ToCallee = importChecked(Err, E->getCallee());
  auto ToType = importChecked(Err, E->getType());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  SmallVector<Expr *, 4> ToArgs(E->getNumArgs());
  if (Error Err = ImportContainerChecked(E->arguments(), ToArgs))
    return std::move(Err);

  return CXXMemberCallExpr::Create(Importer.getToContext(), ToCallee, ToArgs,
                                   ToType, E->getValueKind(), ToRParenLoc,
                                   E->getFPFeatures());
}

ExpectedStmt ASTNodeImporter::VisitCXXThisExpr(CXXThisExpr *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedSLoc ToLocationOrErr = import(E->getLocation());
  if (!ToLocationOrErr)
    return ToLocationOrErr.takeError();

  return CXXThisExpr::Create(Importer.getToContext(), *ToLocationOrErr,
                             *ToTypeOrErr, E->isImplicit());
}

ExpectedStmt ASTNodeImporter::VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedSLoc ToLocationOrErr = import(E->getLocation());
  if (!ToLocationOrErr)
    return ToLocationOrErr.takeError();

  return CXXBoolLiteralExpr::Create(Importer.getToContext(), E->getValue(),
                                    *ToTypeOrErr, *ToLocationOrErr);
}

ExpectedStmt ASTNodeImporter::VisitMemberExpr(MemberExpr *E) {
  Error Err = Error::success();
  auto ToBase = importChecked(Err, E->getBase());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToQualifierLoc = importChecked(Err, E->getQualifierLoc());
  auto ToTemplateKeywordLoc = importChecked(Err, E->getTemplateKeywordLoc());
  auto ToMemberDecl = importChecked(Err, E->getMemberDecl());
  auto ToType = importChecked(Err, E->getType());
  auto ToDecl = importChecked(Err, E->getFoundDecl().getDecl());
  auto ToName = importChecked(Err, E->getMemberNameInfo().getName());
  auto ToLoc = importChecked(Err, E->getMemberNameInfo().getLoc());
  if (Err)
    return std::move(Err);

  DeclAccessPair ToFoundDecl =
      DeclAccessPair::make(ToDecl, E->getFoundDecl().getAccess());

  DeclarationNameInfo ToMemberNameInfo(ToName, ToLoc);

  TemplateArgumentListInfo ToTAInfo, *ResInfo = nullptr;
  if (E->hasExplicitTemplateArgs()) {
    if (Error Err =
            ImportTemplateArgumentListInfo(E->getLAngleLoc(), E->getRAngleLoc(),
                                           E->template_arguments(), ToTAInfo))
      return std::move(Err);
    ResInfo = &ToTAInfo;
  }

  return MemberExpr::Create(Importer.getToContext(), ToBase, E->isArrow(),
                            ToOperatorLoc, ToQualifierLoc, ToTemplateKeywordLoc,
                            ToMemberDecl, ToFoundDecl, ToMemberNameInfo,
                            ResInfo, ToType, E->getValueKind(),
                            E->getObjectKind(), E->isNonOdrUse());
}

ExpectedStmt
ASTNodeImporter::VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E) {
  Error Err = Error::success();
  auto ToBase = importChecked(Err, E->getBase());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToQualifierLoc = importChecked(Err, E->getQualifierLoc());
  auto ToScopeTypeInfo = importChecked(Err, E->getScopeTypeInfo());
  auto ToColonColonLoc = importChecked(Err, E->getColonColonLoc());
  auto ToTildeLoc = importChecked(Err, E->getTildeLoc());
  if (Err)
    return std::move(Err);

  PseudoDestructorTypeStorage Storage;
  if (const IdentifierInfo *FromII = E->getDestroyedTypeIdentifier()) {
    const IdentifierInfo *ToII = Importer.Import(FromII);
    ExpectedSLoc ToDestroyedTypeLocOrErr = import(E->getDestroyedTypeLoc());
    if (!ToDestroyedTypeLocOrErr)
      return ToDestroyedTypeLocOrErr.takeError();
    Storage = PseudoDestructorTypeStorage(ToII, *ToDestroyedTypeLocOrErr);
  } else {
    if (auto ToTIOrErr = import(E->getDestroyedTypeInfo()))
      Storage = PseudoDestructorTypeStorage(*ToTIOrErr);
    else
      return ToTIOrErr.takeError();
  }

  return new (Importer.getToContext()) CXXPseudoDestructorExpr(
      Importer.getToContext(), ToBase, E->isArrow(), ToOperatorLoc,
      ToQualifierLoc, ToScopeTypeInfo, ToColonColonLoc, ToTildeLoc, Storage);
}

ExpectedStmt ASTNodeImporter::VisitCXXDependentScopeMemberExpr(
    CXXDependentScopeMemberExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToQualifierLoc = importChecked(Err, E->getQualifierLoc());
  auto ToTemplateKeywordLoc = importChecked(Err, E->getTemplateKeywordLoc());
  auto ToFirstQualifierFoundInScope =
      importChecked(Err, E->getFirstQualifierFoundInScope());
  if (Err)
    return std::move(Err);

  Expr *ToBase = nullptr;
  if (!E->isImplicitAccess()) {
    if (ExpectedExpr ToBaseOrErr = import(E->getBase()))
      ToBase = *ToBaseOrErr;
    else
      return ToBaseOrErr.takeError();
  }

  TemplateArgumentListInfo ToTAInfo, *ResInfo = nullptr;

  if (E->hasExplicitTemplateArgs()) {
    if (Error Err =
            ImportTemplateArgumentListInfo(E->getLAngleLoc(), E->getRAngleLoc(),
                                           E->template_arguments(), ToTAInfo))
      return std::move(Err);
    ResInfo = &ToTAInfo;
  }
  auto ToMember = importChecked(Err, E->getMember());
  auto ToMemberLoc = importChecked(Err, E->getMemberLoc());
  if (Err)
    return std::move(Err);
  DeclarationNameInfo ToMemberNameInfo(ToMember, ToMemberLoc);

  // Import additional name location/type info.
  if (Error Err =
          ImportDeclarationNameLoc(E->getMemberNameInfo(), ToMemberNameInfo))
    return std::move(Err);

  return CXXDependentScopeMemberExpr::Create(
      Importer.getToContext(), ToBase, ToType, E->isArrow(), ToOperatorLoc,
      ToQualifierLoc, ToTemplateKeywordLoc, ToFirstQualifierFoundInScope,
      ToMemberNameInfo, ResInfo);
}

ExpectedStmt
ASTNodeImporter::VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
  Error Err = Error::success();
  auto ToQualifierLoc = importChecked(Err, E->getQualifierLoc());
  auto ToTemplateKeywordLoc = importChecked(Err, E->getTemplateKeywordLoc());
  auto ToDeclName = importChecked(Err, E->getDeclName());
  auto ToNameLoc = importChecked(Err, E->getNameInfo().getLoc());
  auto ToLAngleLoc = importChecked(Err, E->getLAngleLoc());
  auto ToRAngleLoc = importChecked(Err, E->getRAngleLoc());
  if (Err)
    return std::move(Err);

  DeclarationNameInfo ToNameInfo(ToDeclName, ToNameLoc);
  if (Error Err = ImportDeclarationNameLoc(E->getNameInfo(), ToNameInfo))
    return std::move(Err);

  TemplateArgumentListInfo ToTAInfo(ToLAngleLoc, ToRAngleLoc);
  TemplateArgumentListInfo *ResInfo = nullptr;
  if (E->hasExplicitTemplateArgs()) {
    if (Error Err =
        ImportTemplateArgumentListInfo(E->template_arguments(), ToTAInfo))
      return std::move(Err);
    ResInfo = &ToTAInfo;
  }

  return DependentScopeDeclRefExpr::Create(
      Importer.getToContext(), ToQualifierLoc, ToTemplateKeywordLoc,
      ToNameInfo, ResInfo);
}

ExpectedStmt ASTNodeImporter::VisitCXXUnresolvedConstructExpr(
    CXXUnresolvedConstructExpr *E) {
  Error Err = Error::success();
  auto ToLParenLoc = importChecked(Err, E->getLParenLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  auto ToType = importChecked(Err, E->getType());
  auto ToTypeSourceInfo = importChecked(Err, E->getTypeSourceInfo());
  if (Err)
    return std::move(Err);

  SmallVector<Expr *, 8> ToArgs(E->getNumArgs());
  if (Error Err =
      ImportArrayChecked(E->arg_begin(), E->arg_end(), ToArgs.begin()))
    return std::move(Err);

  return CXXUnresolvedConstructExpr::Create(
      Importer.getToContext(), ToType, ToTypeSourceInfo, ToLParenLoc,
      llvm::ArrayRef(ToArgs), ToRParenLoc, E->isListInitialization());
}

ExpectedStmt
ASTNodeImporter::VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E) {
  Expected<CXXRecordDecl *> ToNamingClassOrErr = import(E->getNamingClass());
  if (!ToNamingClassOrErr)
    return ToNamingClassOrErr.takeError();

  auto ToQualifierLocOrErr = import(E->getQualifierLoc());
  if (!ToQualifierLocOrErr)
    return ToQualifierLocOrErr.takeError();

  Error Err = Error::success();
  auto ToName = importChecked(Err, E->getName());
  auto ToNameLoc = importChecked(Err, E->getNameLoc());
  if (Err)
    return std::move(Err);
  DeclarationNameInfo ToNameInfo(ToName, ToNameLoc);

  // Import additional name location/type info.
  if (Error Err = ImportDeclarationNameLoc(E->getNameInfo(), ToNameInfo))
    return std::move(Err);

  UnresolvedSet<8> ToDecls;
  for (auto *D : E->decls())
    if (auto ToDOrErr = import(D))
      ToDecls.addDecl(cast<NamedDecl>(*ToDOrErr));
    else
      return ToDOrErr.takeError();

  if (E->hasExplicitTemplateArgs()) {
    TemplateArgumentListInfo ToTAInfo;
    if (Error Err = ImportTemplateArgumentListInfo(
        E->getLAngleLoc(), E->getRAngleLoc(), E->template_arguments(),
        ToTAInfo))
      return std::move(Err);

    ExpectedSLoc ToTemplateKeywordLocOrErr = import(E->getTemplateKeywordLoc());
    if (!ToTemplateKeywordLocOrErr)
      return ToTemplateKeywordLocOrErr.takeError();

    const bool KnownDependent =
        (E->getDependence() & ExprDependence::TypeValue) ==
        ExprDependence::TypeValue;
    return UnresolvedLookupExpr::Create(
        Importer.getToContext(), *ToNamingClassOrErr, *ToQualifierLocOrErr,
        *ToTemplateKeywordLocOrErr, ToNameInfo, E->requiresADL(), &ToTAInfo,
        ToDecls.begin(), ToDecls.end(), KnownDependent,
        /*KnownInstantiationDependent=*/E->isInstantiationDependent());
  }

  return UnresolvedLookupExpr::Create(
      Importer.getToContext(), *ToNamingClassOrErr, *ToQualifierLocOrErr,
      ToNameInfo, E->requiresADL(), ToDecls.begin(), ToDecls.end(),
      /*KnownDependent=*/E->isTypeDependent(),
      /*KnownInstantiationDependent=*/E->isInstantiationDependent());
}

ExpectedStmt
ASTNodeImporter::VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToQualifierLoc = importChecked(Err, E->getQualifierLoc());
  auto ToTemplateKeywordLoc = importChecked(Err, E->getTemplateKeywordLoc());
  auto ToName = importChecked(Err, E->getName());
  auto ToNameLoc = importChecked(Err, E->getNameLoc());
  if (Err)
    return std::move(Err);

  DeclarationNameInfo ToNameInfo(ToName, ToNameLoc);
  // Import additional name location/type info.
  if (Error Err = ImportDeclarationNameLoc(E->getNameInfo(), ToNameInfo))
    return std::move(Err);

  UnresolvedSet<8> ToDecls;
  for (Decl *D : E->decls())
    if (auto ToDOrErr = import(D))
      ToDecls.addDecl(cast<NamedDecl>(*ToDOrErr));
    else
      return ToDOrErr.takeError();

  TemplateArgumentListInfo ToTAInfo;
  TemplateArgumentListInfo *ResInfo = nullptr;
  if (E->hasExplicitTemplateArgs()) {
    TemplateArgumentListInfo FromTAInfo;
    E->copyTemplateArgumentsInto(FromTAInfo);
    if (Error Err = ImportTemplateArgumentListInfo(FromTAInfo, ToTAInfo))
      return std::move(Err);
    ResInfo = &ToTAInfo;
  }

  Expr *ToBase = nullptr;
  if (!E->isImplicitAccess()) {
    if (ExpectedExpr ToBaseOrErr = import(E->getBase()))
      ToBase = *ToBaseOrErr;
    else
      return ToBaseOrErr.takeError();
  }

  return UnresolvedMemberExpr::Create(
      Importer.getToContext(), E->hasUnresolvedUsing(), ToBase, ToType,
      E->isArrow(), ToOperatorLoc, ToQualifierLoc, ToTemplateKeywordLoc,
      ToNameInfo, ResInfo, ToDecls.begin(), ToDecls.end());
}

ExpectedStmt ASTNodeImporter::VisitCallExpr(CallExpr *E) {
  Error Err = Error::success();
  auto ToCallee = importChecked(Err, E->getCallee());
  auto ToType = importChecked(Err, E->getType());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  if (Err)
    return std::move(Err);

  unsigned NumArgs = E->getNumArgs();
  llvm::SmallVector<Expr *, 2> ToArgs(NumArgs);
  if (Error Err = ImportContainerChecked(E->arguments(), ToArgs))
     return std::move(Err);

  if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
    return CXXOperatorCallExpr::Create(
        Importer.getToContext(), OCE->getOperator(), ToCallee, ToArgs, ToType,
        OCE->getValueKind(), ToRParenLoc, OCE->getFPFeatures(),
        OCE->getADLCallKind());
  }

  return CallExpr::Create(Importer.getToContext(), ToCallee, ToArgs, ToType,
                          E->getValueKind(), ToRParenLoc, E->getFPFeatures(),
                          /*MinNumArgs=*/0, E->getADLCallKind());
}

ExpectedStmt ASTNodeImporter::VisitLambdaExpr(LambdaExpr *E) {
  CXXRecordDecl *FromClass = E->getLambdaClass();
  auto ToClassOrErr = import(FromClass);
  if (!ToClassOrErr)
    return ToClassOrErr.takeError();
  CXXRecordDecl *ToClass = *ToClassOrErr;

  auto ToCallOpOrErr = import(E->getCallOperator());
  if (!ToCallOpOrErr)
    return ToCallOpOrErr.takeError();

  SmallVector<Expr *, 8> ToCaptureInits(E->capture_size());
  if (Error Err = ImportContainerChecked(E->capture_inits(), ToCaptureInits))
    return std::move(Err);

  Error Err = Error::success();
  auto ToIntroducerRange = importChecked(Err, E->getIntroducerRange());
  auto ToCaptureDefaultLoc = importChecked(Err, E->getCaptureDefaultLoc());
  auto ToEndLoc = importChecked(Err, E->getEndLoc());
  if (Err)
    return std::move(Err);

  return LambdaExpr::Create(Importer.getToContext(), ToClass, ToIntroducerRange,
                            E->getCaptureDefault(), ToCaptureDefaultLoc,
                            E->hasExplicitParameters(),
                            E->hasExplicitResultType(), ToCaptureInits,
                            ToEndLoc, E->containsUnexpandedParameterPack());
}


ExpectedStmt ASTNodeImporter::VisitInitListExpr(InitListExpr *E) {
  Error Err = Error::success();
  auto ToLBraceLoc = importChecked(Err, E->getLBraceLoc());
  auto ToRBraceLoc = importChecked(Err, E->getRBraceLoc());
  auto ToType = importChecked(Err, E->getType());
  if (Err)
    return std::move(Err);

  SmallVector<Expr *, 4> ToExprs(E->getNumInits());
  if (Error Err = ImportContainerChecked(E->inits(), ToExprs))
    return std::move(Err);

  ASTContext &ToCtx = Importer.getToContext();
  InitListExpr *To = new (ToCtx) InitListExpr(
      ToCtx, ToLBraceLoc, ToExprs, ToRBraceLoc);
  To->setType(ToType);

  if (E->hasArrayFiller()) {
    if (ExpectedExpr ToFillerOrErr = import(E->getArrayFiller()))
      To->setArrayFiller(*ToFillerOrErr);
    else
      return ToFillerOrErr.takeError();
  }

  if (FieldDecl *FromFD = E->getInitializedFieldInUnion()) {
    if (auto ToFDOrErr = import(FromFD))
      To->setInitializedFieldInUnion(*ToFDOrErr);
    else
      return ToFDOrErr.takeError();
  }

  if (InitListExpr *SyntForm = E->getSyntacticForm()) {
    if (auto ToSyntFormOrErr = import(SyntForm))
      To->setSyntacticForm(*ToSyntFormOrErr);
    else
      return ToSyntFormOrErr.takeError();
  }

  // Copy InitListExprBitfields, which are not handled in the ctor of
  // InitListExpr.
  To->sawArrayRangeDesignator(E->hadArrayRangeDesignator());

  return To;
}

ExpectedStmt ASTNodeImporter::VisitCXXStdInitializerListExpr(
    CXXStdInitializerListExpr *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  ExpectedExpr ToSubExprOrErr = import(E->getSubExpr());
  if (!ToSubExprOrErr)
    return ToSubExprOrErr.takeError();

  return new (Importer.getToContext()) CXXStdInitializerListExpr(
      *ToTypeOrErr, *ToSubExprOrErr);
}

ExpectedStmt ASTNodeImporter::VisitCXXInheritedCtorInitExpr(
    CXXInheritedCtorInitExpr *E) {
  Error Err = Error::success();
  auto ToLocation = importChecked(Err, E->getLocation());
  auto ToType = importChecked(Err, E->getType());
  auto ToConstructor = importChecked(Err, E->getConstructor());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) CXXInheritedCtorInitExpr(
      ToLocation, ToType, ToConstructor, E->constructsVBase(),
      E->inheritedFromVBase());
}

ExpectedStmt ASTNodeImporter::VisitArrayInitLoopExpr(ArrayInitLoopExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToCommonExpr = importChecked(Err, E->getCommonExpr());
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) ArrayInitLoopExpr(
      ToType, ToCommonExpr, ToSubExpr);
}

ExpectedStmt ASTNodeImporter::VisitArrayInitIndexExpr(ArrayInitIndexExpr *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();
  return new (Importer.getToContext()) ArrayInitIndexExpr(*ToTypeOrErr);
}

ExpectedStmt ASTNodeImporter::VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E) {
  ExpectedSLoc ToBeginLocOrErr = import(E->getBeginLoc());
  if (!ToBeginLocOrErr)
    return ToBeginLocOrErr.takeError();

  auto ToFieldOrErr = import(E->getField());
  if (!ToFieldOrErr)
    return ToFieldOrErr.takeError();

  auto UsedContextOrErr = Importer.ImportContext(E->getUsedContext());
  if (!UsedContextOrErr)
    return UsedContextOrErr.takeError();

  FieldDecl *ToField = *ToFieldOrErr;
  assert(ToField->hasInClassInitializer() &&
         "Field should have in-class initializer if there is a default init "
         "expression that uses it.");
  if (!ToField->getInClassInitializer()) {
    // The in-class initializer may be not yet set in "To" AST even if the
    // field is already there. This must be set here to make construction of
    // CXXDefaultInitExpr work.
    auto ToInClassInitializerOrErr =
        import(E->getField()->getInClassInitializer());
    if (!ToInClassInitializerOrErr)
      return ToInClassInitializerOrErr.takeError();
    ToField->setInClassInitializer(*ToInClassInitializerOrErr);
  }

  Expr *RewrittenInit = nullptr;
  if (E->hasRewrittenInit()) {
    ExpectedExpr ExprOrErr = import(E->getRewrittenExpr());
    if (!ExprOrErr)
      return ExprOrErr.takeError();
    RewrittenInit = ExprOrErr.get();
  }

  return CXXDefaultInitExpr::Create(Importer.getToContext(), *ToBeginLocOrErr,
                                    ToField, *UsedContextOrErr, RewrittenInit);
}

ExpectedStmt ASTNodeImporter::VisitCXXNamedCastExpr(CXXNamedCastExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToSubExpr = importChecked(Err, E->getSubExpr());
  auto ToTypeInfoAsWritten = importChecked(Err, E->getTypeInfoAsWritten());
  auto ToOperatorLoc = importChecked(Err, E->getOperatorLoc());
  auto ToRParenLoc = importChecked(Err, E->getRParenLoc());
  auto ToAngleBrackets = importChecked(Err, E->getAngleBrackets());
  if (Err)
    return std::move(Err);

  ExprValueKind VK = E->getValueKind();
  CastKind CK = E->getCastKind();
  auto ToBasePathOrErr = ImportCastPath(E);
  if (!ToBasePathOrErr)
    return ToBasePathOrErr.takeError();

  if (auto CCE = dyn_cast<CXXStaticCastExpr>(E)) {
    return CXXStaticCastExpr::Create(
        Importer.getToContext(), ToType, VK, CK, ToSubExpr, &(*ToBasePathOrErr),
        ToTypeInfoAsWritten, CCE->getFPFeatures(), ToOperatorLoc, ToRParenLoc,
        ToAngleBrackets);
  } else if (isa<CXXDynamicCastExpr>(E)) {
    return CXXDynamicCastExpr::Create(
        Importer.getToContext(), ToType, VK, CK, ToSubExpr, &(*ToBasePathOrErr),
        ToTypeInfoAsWritten, ToOperatorLoc, ToRParenLoc, ToAngleBrackets);
  } else if (isa<CXXReinterpretCastExpr>(E)) {
    return CXXReinterpretCastExpr::Create(
        Importer.getToContext(), ToType, VK, CK, ToSubExpr, &(*ToBasePathOrErr),
        ToTypeInfoAsWritten, ToOperatorLoc, ToRParenLoc, ToAngleBrackets);
  } else if (isa<CXXConstCastExpr>(E)) {
    return CXXConstCastExpr::Create(
        Importer.getToContext(), ToType, VK, ToSubExpr, ToTypeInfoAsWritten,
        ToOperatorLoc, ToRParenLoc, ToAngleBrackets);
  } else {
    llvm_unreachable("Unknown cast type");
    return make_error<ASTImportError>();
  }
}

ExpectedStmt ASTNodeImporter::VisitSubstNonTypeTemplateParmExpr(
    SubstNonTypeTemplateParmExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToExprLoc = importChecked(Err, E->getExprLoc());
  auto ToAssociatedDecl = importChecked(Err, E->getAssociatedDecl());
  auto ToReplacement = importChecked(Err, E->getReplacement());
  if (Err)
    return std::move(Err);

  return new (Importer.getToContext()) SubstNonTypeTemplateParmExpr(
      ToType, E->getValueKind(), ToExprLoc, ToReplacement, ToAssociatedDecl,
      E->getIndex(), E->getPackIndex(), E->isReferenceParameter());
}

ExpectedStmt ASTNodeImporter::VisitTypeTraitExpr(TypeTraitExpr *E) {
  Error Err = Error::success();
  auto ToType = importChecked(Err, E->getType());
  auto ToBeginLoc = importChecked(Err, E->getBeginLoc());
  auto ToEndLoc = importChecked(Err, E->getEndLoc());
  if (Err)
    return std::move(Err);

  SmallVector<TypeSourceInfo *, 4> ToArgs(E->getNumArgs());
  if (Error Err = ImportContainerChecked(E->getArgs(), ToArgs))
    return std::move(Err);

  // According to Sema::BuildTypeTrait(), if E is value-dependent,
  // Value is always false.
  bool ToValue = (E->isValueDependent() ? false : E->getValue());

  return TypeTraitExpr::Create(
      Importer.getToContext(), ToType, ToBeginLoc, E->getTrait(), ToArgs,
      ToEndLoc, ToValue);
}

ExpectedStmt ASTNodeImporter::VisitCXXTypeidExpr(CXXTypeidExpr *E) {
  ExpectedType ToTypeOrErr = import(E->getType());
  if (!ToTypeOrErr)
    return ToTypeOrErr.takeError();

  auto ToSourceRangeOrErr = import(E->getSourceRange());
  if (!ToSourceRangeOrErr)
    return ToSourceRangeOrErr.takeError();

  if (E->isTypeOperand()) {
    if (auto ToTSIOrErr = import(E->getTypeOperandSourceInfo()))
      return new (Importer.getToContext()) CXXTypeidExpr(
          *ToTypeOrErr, *ToTSIOrErr, *ToSourceRangeOrErr);
    else
      return ToTSIOrErr.takeError();
  }

  ExpectedExpr ToExprOperandOrErr = import(E->getExprOperand());
  if (!ToExprOperandOrErr)
    return ToExprOperandOrErr.takeError();

  return new (Importer.getToContext()) CXXTypeidExpr(
      *ToTypeOrErr, *ToExprOperandOrErr, *ToSourceRangeOrErr);
}

ExpectedStmt ASTNodeImporter::VisitCXXFoldExpr(CXXFoldExpr *E) {
  Error Err = Error::success();

  QualType ToType = importChecked(Err, E->getType());
  UnresolvedLookupExpr *ToCallee = importChecked(Err, E->getCallee());
  SourceLocation ToLParenLoc = importChecked(Err, E->getLParenLoc());
  Expr *ToLHS = importChecked(Err, E->getLHS());
  SourceLocation ToEllipsisLoc = importChecked(Err, E->getEllipsisLoc());
  Expr *ToRHS = importChecked(Err, E->getRHS());
  SourceLocation ToRParenLoc = importChecked(Err, E->getRParenLoc());

  if (Err)
    return std::move(Err);

  return new (Importer.getToContext())
      CXXFoldExpr(ToType, ToCallee, ToLParenLoc, ToLHS, E->getOperator(),
                  ToEllipsisLoc, ToRHS, ToRParenLoc, E->getNumExpansions());
}

Error ASTNodeImporter::ImportOverriddenMethods(CXXMethodDecl *ToMethod,
                                               CXXMethodDecl *FromMethod) {
  Error ImportErrors = Error::success();
  for (auto *FromOverriddenMethod : FromMethod->overridden_methods()) {
    if (auto ImportedOrErr = import(FromOverriddenMethod))
      ToMethod->getCanonicalDecl()->addOverriddenMethod(cast<CXXMethodDecl>(
          (*ImportedOrErr)->getCanonicalDecl()));
    else
      ImportErrors =
          joinErrors(std::move(ImportErrors), ImportedOrErr.takeError());
  }
  return ImportErrors;
}

ASTImporter::ASTImporter(ASTContext &ToContext, FileManager &ToFileManager,
                         ASTContext &FromContext, FileManager &FromFileManager,
                         bool MinimalImport,
                         std::shared_ptr<ASTImporterSharedState> SharedState)
    : SharedState(SharedState), ToContext(ToContext), FromContext(FromContext),
      ToFileManager(ToFileManager), FromFileManager(FromFileManager),
      Minimal(MinimalImport), ODRHandling(ODRHandlingType::Conservative) {

  // Create a default state without the lookup table: LLDB case.
  if (!SharedState) {
    this->SharedState = std::make_shared<ASTImporterSharedState>();
  }

  ImportedDecls[FromContext.getTranslationUnitDecl()] =
      ToContext.getTranslationUnitDecl();
}

ASTImporter::~ASTImporter() = default;

std::optional<unsigned> ASTImporter::getFieldIndex(Decl *F) {
  assert(F && (isa<FieldDecl>(*F) || isa<IndirectFieldDecl>(*F)) &&
      "Try to get field index for non-field.");

  auto *Owner = dyn_cast<RecordDecl>(F->getDeclContext());
  if (!Owner)
    return std::nullopt;

  unsigned Index = 0;
  for (const auto *D : Owner->decls()) {
    if (D == F)
      return Index;

    if (isa<FieldDecl>(*D) || isa<IndirectFieldDecl>(*D))
      ++Index;
  }

  llvm_unreachable("Field was not found in its parent context.");

  return std::nullopt;
}

ASTImporter::FoundDeclsTy
ASTImporter::findDeclsInToCtx(DeclContext *DC, DeclarationName Name) {
  // We search in the redecl context because of transparent contexts.
  // E.g. a simple C language enum is a transparent context:
  //   enum E { A, B };
  // Now if we had a global variable in the TU
  //   int A;
  // then the enum constant 'A' and the variable 'A' violates ODR.
  // We can diagnose this only if we search in the redecl context.
  DeclContext *ReDC = DC->getRedeclContext();
  if (SharedState->getLookupTable()) {
    ASTImporterLookupTable::LookupResult LookupResult =
        SharedState->getLookupTable()->lookup(ReDC, Name);
    return FoundDeclsTy(LookupResult.begin(), LookupResult.end());
  } else {
    DeclContext::lookup_result NoloadLookupResult = ReDC->noload_lookup(Name);
    FoundDeclsTy Result(NoloadLookupResult.begin(), NoloadLookupResult.end());
    // We must search by the slow case of localUncachedLookup because that is
    // working even if there is no LookupPtr for the DC. We could use
    // DC::buildLookup() to create the LookupPtr, but that would load external
    // decls again, we must avoid that case.
    // Also, even if we had the LookupPtr, we must find Decls which are not
    // in the LookupPtr, so we need the slow case.
    // These cases are handled in ASTImporterLookupTable, but we cannot use
    // that with LLDB since that traverses through the AST which initiates the
    // load of external decls again via DC::decls().  And again, we must avoid
    // loading external decls during the import.
    if (Result.empty())
      ReDC->localUncachedLookup(Name, Result);
    return Result;
  }
}

void ASTImporter::AddToLookupTable(Decl *ToD) {
  SharedState->addDeclToLookup(ToD);
}

Expected<Decl *> ASTImporter::ImportImpl(Decl *FromD) {
  // Import the decl using ASTNodeImporter.
  ASTNodeImporter Importer(*this);
  return Importer.Visit(FromD);
}

void ASTImporter::RegisterImportedDecl(Decl *FromD, Decl *ToD) {
  MapImported(FromD, ToD);
}

llvm::Expected<ExprWithCleanups::CleanupObject>
ASTImporter::Import(ExprWithCleanups::CleanupObject From) {
  if (auto *CLE = From.dyn_cast<CompoundLiteralExpr *>()) {
    if (Expected<Expr *> R = Import(CLE))
      return ExprWithCleanups::CleanupObject(cast<CompoundLiteralExpr>(*R));
  }

  // FIXME: Handle BlockDecl when we implement importing BlockExpr in
  //        ASTNodeImporter.
  return make_error<ASTImportError>(ASTImportError::UnsupportedConstruct);
}

ExpectedTypePtr ASTImporter::Import(const Type *FromT) {
  if (!FromT)
    return FromT;

  // Check whether we've already imported this type.
  llvm::DenseMap<const Type *, const Type *>::iterator Pos =
      ImportedTypes.find(FromT);
  if (Pos != ImportedTypes.end())
    return Pos->second;

  // Import the type.
  ASTNodeImporter Importer(*this);
  ExpectedType ToTOrErr = Importer.Visit(FromT);
  if (!ToTOrErr)
    return ToTOrErr.takeError();

  // Record the imported type.
  ImportedTypes[FromT] = ToTOrErr->getTypePtr();

  return ToTOrErr->getTypePtr();
}

Expected<QualType> ASTImporter::Import(QualType FromT) {
  if (FromT.isNull())
    return QualType{};

  ExpectedTypePtr ToTyOrErr = Import(FromT.getTypePtr());
  if (!ToTyOrErr)
    return ToTyOrErr.takeError();

  return ToContext.getQualifiedType(*ToTyOrErr, FromT.getLocalQualifiers());
}

Expected<TypeSourceInfo *> ASTImporter::Import(TypeSourceInfo *FromTSI) {
  if (!FromTSI)
    return FromTSI;

  // FIXME: For now we just create a "trivial" type source info based
  // on the type and a single location. Implement a real version of this.
  ExpectedType TOrErr = Import(FromTSI->getType());
  if (!TOrErr)
    return TOrErr.takeError();
  ExpectedSLoc BeginLocOrErr = Import(FromTSI->getTypeLoc().getBeginLoc());
  if (!BeginLocOrErr)
    return BeginLocOrErr.takeError();

  return ToContext.getTrivialTypeSourceInfo(*TOrErr, *BeginLocOrErr);
}

namespace {
// To use this object, it should be created before the new attribute is created,
// and destructed after it is created. The construction already performs the
// import of the data.
template <typename T> struct AttrArgImporter {
  AttrArgImporter(const AttrArgImporter<T> &) = delete;
  AttrArgImporter(AttrArgImporter<T> &&) = default;
  AttrArgImporter<T> &operator=(const AttrArgImporter<T> &) = delete;
  AttrArgImporter<T> &operator=(AttrArgImporter<T> &&) = default;

  AttrArgImporter(ASTNodeImporter &I, Error &Err, const T &From)
      : To(I.importChecked(Err, From)) {}

  const T &value() { return To; }

private:
  T To;
};

// To use this object, it should be created before the new attribute is created,
// and destructed after it is created. The construction already performs the
// import of the data. The array data is accessible in a pointer form, this form
// is used by the attribute classes. This object should be created once for the
// array data to be imported (the array size is not imported, just copied).
template <typename T> struct AttrArgArrayImporter {
  AttrArgArrayImporter(const AttrArgArrayImporter<T> &) = delete;
  AttrArgArrayImporter(AttrArgArrayImporter<T> &&) = default;
  AttrArgArrayImporter<T> &operator=(const AttrArgArrayImporter<T> &) = delete;
  AttrArgArrayImporter<T> &operator=(AttrArgArrayImporter<T> &&) = default;

  AttrArgArrayImporter(ASTNodeImporter &I, Error &Err,
                       const llvm::iterator_range<T *> &From,
                       unsigned ArraySize) {
    if (Err)
      return;
    To.reserve(ArraySize);
    Err = I.ImportContainerChecked(From, To);
  }

  T *value() { return To.data(); }

private:
  llvm::SmallVector<T, 2> To;
};

class AttrImporter {
  Error Err{Error::success()};
  Attr *ToAttr = nullptr;
  ASTImporter &Importer;
  ASTNodeImporter NImporter;

public:
  AttrImporter(ASTImporter &I) : Importer(I), NImporter(I) {}

  // Useful for accessing the imported attribute.
  template <typename T> T *castAttrAs() { return cast<T>(ToAttr); }
  template <typename T> const T *castAttrAs() const { return cast<T>(ToAttr); }

  // Create an "importer" for an attribute parameter.
  // Result of the 'value()' of that object is to be passed to the function
  // 'importAttr', in the order that is expected by the attribute class.
  template <class T> AttrArgImporter<T> importArg(const T &From) {
    return AttrArgImporter<T>(NImporter, Err, From);
  }

  // Create an "importer" for an attribute parameter that has array type.
  // Result of the 'value()' of that object is to be passed to the function
  // 'importAttr', then the size of the array as next argument.
  template <typename T>
  AttrArgArrayImporter<T> importArrayArg(const llvm::iterator_range<T *> &From,
                                         unsigned ArraySize) {
    return AttrArgArrayImporter<T>(NImporter, Err, From, ArraySize);
  }

  // Create an attribute object with the specified arguments.
  // The 'FromAttr' is the original (not imported) attribute, the 'ImportedArg'
  // should be values that are passed to the 'Create' function of the attribute.
  // (The 'Create' with 'ASTContext' first and 'AttributeCommonInfo' last is
  // used here.) As much data is copied or imported from the old attribute
  // as possible. The passed arguments should be already imported.
  // If an import error happens, the internal error is set to it, and any
  // further import attempt is ignored.
  template <typename T, typename... Arg>
  void importAttr(const T *FromAttr, Arg &&...ImportedArg) {
    static_assert(std::is_base_of<Attr, T>::value,
                  "T should be subclass of Attr.");
    assert(!ToAttr && "Use one AttrImporter to import one Attribute object.");

    const IdentifierInfo *ToAttrName = Importer.Import(FromAttr->getAttrName());
    const IdentifierInfo *ToScopeName =
        Importer.Import(FromAttr->getScopeName());
    SourceRange ToAttrRange =
        NImporter.importChecked(Err, FromAttr->getRange());
    SourceLocation ToScopeLoc =
        NImporter.importChecked(Err, FromAttr->getScopeLoc());

    if (Err)
      return;

    AttributeCommonInfo ToI(ToAttrName, ToScopeName, ToAttrRange, ToScopeLoc,
                            FromAttr->getParsedKind(), FromAttr->getForm());
    // The "SemanticSpelling" is not needed to be passed to the constructor.
    // That value is recalculated from the SpellingListIndex if needed.
    ToAttr = T::Create(Importer.getToContext(),
                       std::forward<Arg>(ImportedArg)..., ToI);

    ToAttr->setImplicit(FromAttr->isImplicit());
    ToAttr->setPackExpansion(FromAttr->isPackExpansion());
    if (auto *ToInheritableAttr = dyn_cast<InheritableAttr>(ToAttr))
      ToInheritableAttr->setInherited(FromAttr->isInherited());
  }

  // Create a clone of the 'FromAttr' and import its source range only.
  // This causes objects with invalid references to be created if the 'FromAttr'
  // contains other data that should be imported.
  void cloneAttr(const Attr *FromAttr) {
    assert(!ToAttr && "Use one AttrImporter to import one Attribute object.");

    SourceRange ToRange = NImporter.importChecked(Err, FromAttr->getRange());
    if (Err)
      return;

    ToAttr = FromAttr->clone(Importer.getToContext());
    ToAttr->setRange(ToRange);
    ToAttr->setAttrName(Importer.Import(FromAttr->getAttrName()));
  }

  // Get the result of the previous import attempt (can be used only once).
  llvm::Expected<Attr *> getResult() && {
    if (Err)
      return std::move(Err);
    assert(ToAttr && "Attribute should be created.");
    return ToAttr;
  }
};
} // namespace

Expected<Attr *> ASTImporter::Import(const Attr *FromAttr) {
  AttrImporter AI(*this);

  // FIXME: Is there some kind of AttrVisitor to use here?
  switch (FromAttr->getKind()) {
  case attr::Aligned: {
    auto *From = cast<AlignedAttr>(FromAttr);
    if (From->isAlignmentExpr())
      AI.importAttr(From, true, AI.importArg(From->getAlignmentExpr()).value());
    else
      AI.importAttr(From, false,
                    AI.importArg(From->getAlignmentType()).value());
    break;
  }

  case attr::AlignValue: {
    auto *From = cast<AlignValueAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getAlignment()).value());
    break;
  }

  case attr::Format: {
    const auto *From = cast<FormatAttr>(FromAttr);
    AI.importAttr(From, Import(From->getType()), From->getFormatIdx(),
                  From->getFirstArg());
    break;
  }

  case attr::EnableIf: {
    const auto *From = cast<EnableIfAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getCond()).value(),
                  From->getMessage());
    break;
  }

  case attr::AssertCapability: {
    const auto *From = cast<AssertCapabilityAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::AcquireCapability: {
    const auto *From = cast<AcquireCapabilityAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::TryAcquireCapability: {
    const auto *From = cast<TryAcquireCapabilityAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getSuccessValue()).value(),
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::ReleaseCapability: {
    const auto *From = cast<ReleaseCapabilityAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::RequiresCapability: {
    const auto *From = cast<RequiresCapabilityAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::GuardedBy: {
    const auto *From = cast<GuardedByAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getArg()).value());
    break;
  }
  case attr::PtGuardedBy: {
    const auto *From = cast<PtGuardedByAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getArg()).value());
    break;
  }
  case attr::AcquiredAfter: {
    const auto *From = cast<AcquiredAfterAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::AcquiredBefore: {
    const auto *From = cast<AcquiredBeforeAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::AssertExclusiveLock: {
    const auto *From = cast<AssertExclusiveLockAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::AssertSharedLock: {
    const auto *From = cast<AssertSharedLockAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::ExclusiveTrylockFunction: {
    const auto *From = cast<ExclusiveTrylockFunctionAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getSuccessValue()).value(),
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::SharedTrylockFunction: {
    const auto *From = cast<SharedTrylockFunctionAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getSuccessValue()).value(),
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  case attr::LockReturned: {
    const auto *From = cast<LockReturnedAttr>(FromAttr);
    AI.importAttr(From, AI.importArg(From->getArg()).value());
    break;
  }
  case attr::LocksExcluded: {
    const auto *From = cast<LocksExcludedAttr>(FromAttr);
    AI.importAttr(From,
                  AI.importArrayArg(From->args(), From->args_size()).value(),
                  From->args_size());
    break;
  }
  default: {
    // The default branch works for attributes that have no arguments to import.
    // FIXME: Handle every attribute type that has arguments of type to import
    // (most often Expr* or Decl* or type) in the switch above.
    AI.cloneAttr(FromAttr);
    break;
  }
  }

  return std::move(AI).getResult();
}

Decl *ASTImporter::GetAlreadyImportedOrNull(const Decl *FromD) const {
  return ImportedDecls.lookup(FromD);
}

TranslationUnitDecl *ASTImporter::GetFromTU(Decl *ToD) {
  auto FromDPos = ImportedFromDecls.find(ToD);
  if (FromDPos == ImportedFromDecls.end())
    return nullptr;
  return FromDPos->second->getTranslationUnitDecl();
}

Error ASTImporter::ImportAttrs(Decl *ToD, Decl *FromD) {
  if (!FromD->hasAttrs() || ToD->hasAttrs())
    return Error::success();
  for (const Attr *FromAttr : FromD->getAttrs()) {
    auto ToAttrOrErr = Import(FromAttr);
    if (ToAttrOrErr)
      ToD->addAttr(*ToAttrOrErr);
    else
      return ToAttrOrErr.takeError();
  }
  return Error::success();
}

Expected<Decl *> ASTImporter::Import(Decl *FromD) {
  if (!FromD)
    return nullptr;

  // Push FromD to the stack, and remove that when we return.
  ImportPath.push(FromD);
  auto ImportPathBuilder =
      llvm::make_scope_exit([this]() { ImportPath.pop(); });

  // Check whether there was a previous failed import.
  // If yes return the existing error.
  if (auto Error = getImportDeclErrorIfAny(FromD))
    return make_error<ASTImportError>(*Error);

  // Check whether we've already imported this declaration.
  Decl *ToD = GetAlreadyImportedOrNull(FromD);
  if (ToD) {
    // Already imported (possibly from another TU) and with an error.
    if (auto Error = SharedState->getImportDeclErrorIfAny(ToD)) {
      setImportDeclError(FromD, *Error);
      return make_error<ASTImportError>(*Error);
    }

    // If FromD has some updated flags after last import, apply it.
    updateFlags(FromD, ToD);
    // If we encounter a cycle during an import then we save the relevant part
    // of the import path associated to the Decl.
    if (ImportPath.hasCycleAtBack())
      SavedImportPaths[FromD].push_back(ImportPath.copyCycleAtBack());
    return ToD;
  }

  // Import the declaration.
  ExpectedDecl ToDOrErr = ImportImpl(FromD);
  if (!ToDOrErr) {
    // Failed to import.

    auto Pos = ImportedDecls.find(FromD);
    if (Pos != ImportedDecls.end()) {
      // Import failed after the object was created.
      // Remove all references to it.
      auto *ToD = Pos->second;
      ImportedDecls.erase(Pos);

      // ImportedDecls and ImportedFromDecls are not symmetric.  It may happen
      // (e.g. with namespaces) that several decls from the 'from' context are
      // mapped to the same decl in the 'to' context.  If we removed entries
      // from the LookupTable here then we may end up removing them multiple
      // times.

      // The Lookuptable contains decls only which are in the 'to' context.
      // Remove from the Lookuptable only if it is *imported* into the 'to'
      // context (and do not remove it if it was added during the initial
      // traverse of the 'to' context).
      auto PosF = ImportedFromDecls.find(ToD);
      if (PosF != ImportedFromDecls.end()) {
        // In the case of TypedefNameDecl we create the Decl first and only
        // then we import and set its DeclContext. So, the DC might not be set
        // when we reach here.
        if (ToD->getDeclContext())
          SharedState->removeDeclFromLookup(ToD);
        ImportedFromDecls.erase(PosF);
      }

      // FIXME: AST may contain remaining references to the failed object.
      // However, the ImportDeclErrors in the shared state contains all the
      // failed objects together with their error.
    }

    // Error encountered for the first time.
    // After takeError the error is not usable any more in ToDOrErr.
    // Get a copy of the error object (any more simple solution for this?).
    ASTImportError ErrOut;
    handleAllErrors(ToDOrErr.takeError(),
                    [&ErrOut](const ASTImportError &E) { ErrOut = E; });
    setImportDeclError(FromD, ErrOut);
    // Set the error for the mapped to Decl, which is in the "to" context.
    if (Pos != ImportedDecls.end())
      SharedState->setImportDeclError(Pos->second, ErrOut);

    // Set the error for all nodes which have been created before we
    // recognized the error.
    for (const auto &Path : SavedImportPaths[FromD]) {
      // The import path contains import-dependency nodes first.
      // Save the node that was imported as dependency of the current node.
      Decl *PrevFromDi = FromD;
      for (Decl *FromDi : Path) {
        // Begin and end of the path equals 'FromD', skip it.
        if (FromDi == FromD)
          continue;
        // We should not set import error on a node and all following nodes in
        // the path if child import errors are ignored.
        if (ChildErrorHandlingStrategy(FromDi).ignoreChildErrorOnParent(
                PrevFromDi))
          break;
        PrevFromDi = FromDi;
        setImportDeclError(FromDi, ErrOut);
        //FIXME Should we remove these Decls from ImportedDecls?
        // Set the error for the mapped to Decl, which is in the "to" context.
        auto Ii = ImportedDecls.find(FromDi);
        if (Ii != ImportedDecls.end())
          SharedState->setImportDeclError(Ii->second, ErrOut);
          // FIXME Should we remove these Decls from the LookupTable,
          // and from ImportedFromDecls?
      }
    }
    SavedImportPaths.erase(FromD);

    // Do not return ToDOrErr, error was taken out of it.
    return make_error<ASTImportError>(ErrOut);
  }

  ToD = *ToDOrErr;

  // FIXME: Handle the "already imported with error" case. We can get here
  // nullptr only if GetImportedOrCreateDecl returned nullptr (after a
  // previously failed create was requested).
  // Later GetImportedOrCreateDecl can be updated to return the error.
  if (!ToD) {
    auto Err = getImportDeclErrorIfAny(FromD);
    assert(Err);
    return make_error<ASTImportError>(*Err);
  }

  // We could import from the current TU without error.  But previously we
  // already had imported a Decl as `ToD` from another TU (with another
  // ASTImporter object) and with an error.
  if (auto Error = SharedState->getImportDeclErrorIfAny(ToD)) {
    setImportDeclError(FromD, *Error);
    return make_error<ASTImportError>(*Error);
  }
  // Make sure that ImportImpl registered the imported decl.
  assert(ImportedDecls.count(FromD) != 0 && "Missing call to MapImported?");
  if (auto Error = ImportAttrs(ToD, FromD))
    return std::move(Error);

  // Notify subclasses.
  Imported(FromD, ToD);

  updateFlags(FromD, ToD);
  SavedImportPaths.erase(FromD);
  return ToDOrErr;
}

llvm::Expected<InheritedConstructor>
ASTImporter::Import(const InheritedConstructor &From) {
  return ASTNodeImporter(*this).ImportInheritedConstructor(From);
}

Expected<DeclContext *> ASTImporter::ImportContext(DeclContext *FromDC) {
  if (!FromDC)
    return FromDC;

  ExpectedDecl ToDCOrErr = Import(cast<Decl>(FromDC));
  if (!ToDCOrErr)
    return ToDCOrErr.takeError();
  auto *ToDC = cast<DeclContext>(*ToDCOrErr);

  // When we're using a record/enum/Objective-C class/protocol as a context, we
  // need it to have a definition.
  if (auto *ToRecord = dyn_cast<RecordDecl>(ToDC)) {
    auto *FromRecord = cast<RecordDecl>(FromDC);
    if (ToRecord->isCompleteDefinition())
      return ToDC;

    // If FromRecord is not defined we need to force it to be.
    // Simply calling CompleteDecl(...) for a RecordDecl will break some cases
    // it will start the definition but we never finish it.
    // If there are base classes they won't be imported and we will
    // be missing anything that we inherit from those bases.
    if (FromRecord->getASTContext().getExternalSource() &&
        !FromRecord->isCompleteDefinition())
      FromRecord->getASTContext().getExternalSource()->CompleteType(FromRecord);

    if (FromRecord->isCompleteDefinition())
      if (Error Err = ASTNodeImporter(*this).ImportDefinition(
          FromRecord, ToRecord, ASTNodeImporter::IDK_Basic))
        return std::move(Err);
  } else if (auto *ToEnum = dyn_cast<EnumDecl>(ToDC)) {
    auto *FromEnum = cast<EnumDecl>(FromDC);
    if (ToEnum->isCompleteDefinition()) {
      // Do nothing.
    } else if (FromEnum->isCompleteDefinition()) {
      if (Error Err = ASTNodeImporter(*this).ImportDefinition(
          FromEnum, ToEnum, ASTNodeImporter::IDK_Basic))
        return std::move(Err);
    } else {
      CompleteDecl(ToEnum);
    }
  } else if (auto *ToClass = dyn_cast<ObjCInterfaceDecl>(ToDC)) {
    auto *FromClass = cast<ObjCInterfaceDecl>(FromDC);
    if (ToClass->getDefinition()) {
      // Do nothing.
    } else if (ObjCInterfaceDecl *FromDef = FromClass->getDefinition()) {
      if (Error Err = ASTNodeImporter(*this).ImportDefinition(
          FromDef, ToClass, ASTNodeImporter::IDK_Basic))
        return std::move(Err);
    } else {
      CompleteDecl(ToClass);
    }
  } else if (auto *ToProto = dyn_cast<ObjCProtocolDecl>(ToDC)) {
    auto *FromProto = cast<ObjCProtocolDecl>(FromDC);
    if (ToProto->getDefinition()) {
      // Do nothing.
    } else if (ObjCProtocolDecl *FromDef = FromProto->getDefinition()) {
      if (Error Err = ASTNodeImporter(*this).ImportDefinition(
          FromDef, ToProto, ASTNodeImporter::IDK_Basic))
        return std::move(Err);
    } else {
      CompleteDecl(ToProto);
    }
  }

  return ToDC;
}

Expected<Expr *> ASTImporter::Import(Expr *FromE) {
  if (ExpectedStmt ToSOrErr = Import(cast_or_null<Stmt>(FromE)))
    return cast_or_null<Expr>(*ToSOrErr);
  else
    return ToSOrErr.takeError();
}

Expected<Stmt *> ASTImporter::Import(Stmt *FromS) {
  if (!FromS)
    return nullptr;

  // Check whether we've already imported this statement.
  llvm::DenseMap<Stmt *, Stmt *>::iterator Pos = ImportedStmts.find(FromS);
  if (Pos != ImportedStmts.end())
    return Pos->second;

  // Import the statement.
  ASTNodeImporter Importer(*this);
  ExpectedStmt ToSOrErr = Importer.Visit(FromS);
  if (!ToSOrErr)
    return ToSOrErr;

  if (auto *ToE = dyn_cast<Expr>(*ToSOrErr)) {
    auto *FromE = cast<Expr>(FromS);
    // Copy ExprBitfields, which may not be handled in Expr subclasses
    // constructors.
    ToE->setValueKind(FromE->getValueKind());
    ToE->setObjectKind(FromE->getObjectKind());
    ToE->setDependence(FromE->getDependence());
  }

  // Record the imported statement object.
  ImportedStmts[FromS] = *ToSOrErr;
  return ToSOrErr;
}

Expected<NestedNameSpecifier *>
ASTImporter::Import(NestedNameSpecifier *FromNNS) {
  if (!FromNNS)
    return nullptr;

  NestedNameSpecifier *Prefix = nullptr;
  if (Error Err = importInto(Prefix, FromNNS->getPrefix()))
    return std::move(Err);

  switch (FromNNS->getKind()) {
  case NestedNameSpecifier::Identifier:
    assert(FromNNS->getAsIdentifier() && "NNS should contain identifier.");
    return NestedNameSpecifier::Create(ToContext, Prefix,
                                       Import(FromNNS->getAsIdentifier()));

  case NestedNameSpecifier::Namespace:
    if (ExpectedDecl NSOrErr = Import(FromNNS->getAsNamespace())) {
      return NestedNameSpecifier::Create(ToContext, Prefix,
                                         cast<NamespaceDecl>(*NSOrErr));
    } else
      return NSOrErr.takeError();

  case NestedNameSpecifier::NamespaceAlias:
    if (ExpectedDecl NSADOrErr = Import(FromNNS->getAsNamespaceAlias()))
      return NestedNameSpecifier::Create(ToContext, Prefix,
                                         cast<NamespaceAliasDecl>(*NSADOrErr));
    else
      return NSADOrErr.takeError();

  case NestedNameSpecifier::Global:
    return NestedNameSpecifier::GlobalSpecifier(ToContext);

  case NestedNameSpecifier::Super:
    if (ExpectedDecl RDOrErr = Import(FromNNS->getAsRecordDecl()))
      return NestedNameSpecifier::SuperSpecifier(ToContext,
                                                 cast<CXXRecordDecl>(*RDOrErr));
    else
      return RDOrErr.takeError();

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    if (ExpectedTypePtr TyOrErr = Import(FromNNS->getAsType())) {
      bool TSTemplate =
          FromNNS->getKind() == NestedNameSpecifier::TypeSpecWithTemplate;
      return NestedNameSpecifier::Create(ToContext, Prefix, TSTemplate,
                                         *TyOrErr);
    } else {
      return TyOrErr.takeError();
    }
  }

  llvm_unreachable("Invalid nested name specifier kind");
}

Expected<NestedNameSpecifierLoc>
ASTImporter::Import(NestedNameSpecifierLoc FromNNS) {
  // Copied from NestedNameSpecifier mostly.
  SmallVector<NestedNameSpecifierLoc , 8> NestedNames;
  NestedNameSpecifierLoc NNS = FromNNS;

  // Push each of the nested-name-specifiers's onto a stack for
  // serialization in reverse order.
  while (NNS) {
    NestedNames.push_back(NNS);
    NNS = NNS.getPrefix();
  }

  NestedNameSpecifierLocBuilder Builder;

  while (!NestedNames.empty()) {
    NNS = NestedNames.pop_back_val();
    NestedNameSpecifier *Spec = nullptr;
    if (Error Err = importInto(Spec, NNS.getNestedNameSpecifier()))
      return std::move(Err);

    NestedNameSpecifier::SpecifierKind Kind = Spec->getKind();

    SourceLocation ToLocalBeginLoc, ToLocalEndLoc;
    if (Kind != NestedNameSpecifier::Super) {
      if (Error Err = importInto(ToLocalBeginLoc, NNS.getLocalBeginLoc()))
        return std::move(Err);

      if (Kind != NestedNameSpecifier::Global)
        if (Error Err = importInto(ToLocalEndLoc, NNS.getLocalEndLoc()))
          return std::move(Err);
    }

    switch (Kind) {
    case NestedNameSpecifier::Identifier:
      Builder.Extend(getToContext(), Spec->getAsIdentifier(), ToLocalBeginLoc,
                     ToLocalEndLoc);
      break;

    case NestedNameSpecifier::Namespace:
      Builder.Extend(getToContext(), Spec->getAsNamespace(), ToLocalBeginLoc,
                     ToLocalEndLoc);
      break;

    case NestedNameSpecifier::NamespaceAlias:
      Builder.Extend(getToContext(), Spec->getAsNamespaceAlias(),
                     ToLocalBeginLoc, ToLocalEndLoc);
      break;

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate: {
      SourceLocation ToTLoc;
      if (Error Err = importInto(ToTLoc, NNS.getTypeLoc().getBeginLoc()))
        return std::move(Err);
      TypeSourceInfo *TSI = getToContext().getTrivialTypeSourceInfo(
            QualType(Spec->getAsType(), 0), ToTLoc);
      if (Kind == NestedNameSpecifier::TypeSpecWithTemplate)
        // ToLocalBeginLoc is here the location of the 'template' keyword.
        Builder.Extend(getToContext(), ToLocalBeginLoc, TSI->getTypeLoc(),
                       ToLocalEndLoc);
      else
        // No location for 'template' keyword here.
        Builder.Extend(getToContext(), SourceLocation{}, TSI->getTypeLoc(),
                       ToLocalEndLoc);
      break;
    }

    case NestedNameSpecifier::Global:
      Builder.MakeGlobal(getToContext(), ToLocalBeginLoc);
      break;

    case NestedNameSpecifier::Super: {
      auto ToSourceRangeOrErr = Import(NNS.getSourceRange());
      if (!ToSourceRangeOrErr)
        return ToSourceRangeOrErr.takeError();

      Builder.MakeSuper(getToContext(), Spec->getAsRecordDecl(),
                        ToSourceRangeOrErr->getBegin(),
                        ToSourceRangeOrErr->getEnd());
    }
  }
  }

  return Builder.getWithLocInContext(getToContext());
}

Expected<TemplateName> ASTImporter::Import(TemplateName From) {
  switch (From.getKind()) {
  case TemplateName::Template:
    if (ExpectedDecl ToTemplateOrErr = Import(From.getAsTemplateDecl()))
      return TemplateName(cast<TemplateDecl>((*ToTemplateOrErr)->getCanonicalDecl()));
    else
      return ToTemplateOrErr.takeError();

  case TemplateName::OverloadedTemplate: {
    OverloadedTemplateStorage *FromStorage = From.getAsOverloadedTemplate();
    UnresolvedSet<2> ToTemplates;
    for (auto *I : *FromStorage) {
      if (auto ToOrErr = Import(I))
        ToTemplates.addDecl(cast<NamedDecl>(*ToOrErr));
      else
        return ToOrErr.takeError();
    }
    return ToContext.getOverloadedTemplateName(ToTemplates.begin(),
                                               ToTemplates.end());
  }

  case TemplateName::AssumedTemplate: {
    AssumedTemplateStorage *FromStorage = From.getAsAssumedTemplateName();
    auto DeclNameOrErr = Import(FromStorage->getDeclName());
    if (!DeclNameOrErr)
      return DeclNameOrErr.takeError();
    return ToContext.getAssumedTemplateName(*DeclNameOrErr);
  }

  case TemplateName::QualifiedTemplate: {
    QualifiedTemplateName *QTN = From.getAsQualifiedTemplateName();
    auto QualifierOrErr = Import(QTN->getQualifier());
    if (!QualifierOrErr)
      return QualifierOrErr.takeError();
    auto TNOrErr = Import(QTN->getUnderlyingTemplate());
    if (!TNOrErr)
      return TNOrErr.takeError();
    return ToContext.getQualifiedTemplateName(
        *QualifierOrErr, QTN->hasTemplateKeyword(), *TNOrErr);
  }

  case TemplateName::DependentTemplate: {
    DependentTemplateName *DTN = From.getAsDependentTemplateName();
    auto QualifierOrErr = Import(DTN->getQualifier());
    if (!QualifierOrErr)
      return QualifierOrErr.takeError();

    if (DTN->isIdentifier()) {
      return ToContext.getDependentTemplateName(*QualifierOrErr,
                                                Import(DTN->getIdentifier()));
    }

    return ToContext.getDependentTemplateName(*QualifierOrErr,
                                              DTN->getOperator());
  }

  case TemplateName::SubstTemplateTemplateParm: {
    SubstTemplateTemplateParmStorage *Subst =
        From.getAsSubstTemplateTemplateParm();
    auto ReplacementOrErr = Import(Subst->getReplacement());
    if (!ReplacementOrErr)
      return ReplacementOrErr.takeError();

    auto AssociatedDeclOrErr = Import(Subst->getAssociatedDecl());
    if (!AssociatedDeclOrErr)
      return AssociatedDeclOrErr.takeError();

    return ToContext.getSubstTemplateTemplateParm(
        *ReplacementOrErr, *AssociatedDeclOrErr, Subst->getIndex(),
        Subst->getPackIndex());
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    SubstTemplateTemplateParmPackStorage *SubstPack =
        From.getAsSubstTemplateTemplateParmPack();
    ASTNodeImporter Importer(*this);
    auto ArgPackOrErr =
        Importer.ImportTemplateArgument(SubstPack->getArgumentPack());
    if (!ArgPackOrErr)
      return ArgPackOrErr.takeError();

    auto AssociatedDeclOrErr = Import(SubstPack->getAssociatedDecl());
    if (!AssociatedDeclOrErr)
      return AssociatedDeclOrErr.takeError();

    return ToContext.getSubstTemplateTemplateParmPack(
        *ArgPackOrErr, *AssociatedDeclOrErr, SubstPack->getIndex(),
        SubstPack->getFinal());
  }
  case TemplateName::UsingTemplate: {
    auto UsingOrError = Import(From.getAsUsingShadowDecl());
    if (!UsingOrError)
      return UsingOrError.takeError();
    return TemplateName(cast<UsingShadowDecl>(*UsingOrError));
  }
  }

  llvm_unreachable("Invalid template name kind");
}

Expected<SourceLocation> ASTImporter::Import(SourceLocation FromLoc) {
  if (FromLoc.isInvalid())
    return SourceLocation{};

  SourceManager &FromSM = FromContext.getSourceManager();
  bool IsBuiltin = FromSM.isWrittenInBuiltinFile(FromLoc);

  std::pair<FileID, unsigned> Decomposed = FromSM.getDecomposedLoc(FromLoc);
  Expected<FileID> ToFileIDOrErr = Import(Decomposed.first, IsBuiltin);
  if (!ToFileIDOrErr)
    return ToFileIDOrErr.takeError();
  SourceManager &ToSM = ToContext.getSourceManager();
  return ToSM.getComposedLoc(*ToFileIDOrErr, Decomposed.second);
}

Expected<SourceRange> ASTImporter::Import(SourceRange FromRange) {
  SourceLocation ToBegin, ToEnd;
  if (Error Err = importInto(ToBegin, FromRange.getBegin()))
    return std::move(Err);
  if (Error Err = importInto(ToEnd, FromRange.getEnd()))
    return std::move(Err);

  return SourceRange(ToBegin, ToEnd);
}

Expected<FileID> ASTImporter::Import(FileID FromID, bool IsBuiltin) {
  llvm::DenseMap<FileID, FileID>::iterator Pos = ImportedFileIDs.find(FromID);
  if (Pos != ImportedFileIDs.end())
    return Pos->second;

  SourceManager &FromSM = FromContext.getSourceManager();
  SourceManager &ToSM = ToContext.getSourceManager();
  const SrcMgr::SLocEntry &FromSLoc = FromSM.getSLocEntry(FromID);

  // Map the FromID to the "to" source manager.
  FileID ToID;
  if (FromSLoc.isExpansion()) {
    const SrcMgr::ExpansionInfo &FromEx = FromSLoc.getExpansion();
    ExpectedSLoc ToSpLoc = Import(FromEx.getSpellingLoc());
    if (!ToSpLoc)
      return ToSpLoc.takeError();
    ExpectedSLoc ToExLocS = Import(FromEx.getExpansionLocStart());
    if (!ToExLocS)
      return ToExLocS.takeError();
    unsigned ExLength = FromSM.getFileIDSize(FromID);
    SourceLocation MLoc;
    if (FromEx.isMacroArgExpansion()) {
      MLoc = ToSM.createMacroArgExpansionLoc(*ToSpLoc, *ToExLocS, ExLength);
    } else {
      if (ExpectedSLoc ToExLocE = Import(FromEx.getExpansionLocEnd()))
        MLoc = ToSM.createExpansionLoc(*ToSpLoc, *ToExLocS, *ToExLocE, ExLength,
                                       FromEx.isExpansionTokenRange());
      else
        return ToExLocE.takeError();
    }
    ToID = ToSM.getFileID(MLoc);
  } else {
    const SrcMgr::ContentCache *Cache = &FromSLoc.getFile().getContentCache();

    if (!IsBuiltin && !Cache->BufferOverridden) {
      // Include location of this file.
      ExpectedSLoc ToIncludeLoc = Import(FromSLoc.getFile().getIncludeLoc());
      if (!ToIncludeLoc)
        return ToIncludeLoc.takeError();

      // Every FileID that is not the main FileID needs to have a valid include
      // location so that the include chain points to the main FileID. When
      // importing the main FileID (which has no include location), we need to
      // create a fake include location in the main file to keep this property
      // intact.
      SourceLocation ToIncludeLocOrFakeLoc = *ToIncludeLoc;
      if (FromID == FromSM.getMainFileID())
        ToIncludeLocOrFakeLoc = ToSM.getLocForStartOfFile(ToSM.getMainFileID());

      if (Cache->OrigEntry && Cache->OrigEntry->getDir()) {
        // FIXME: We probably want to use getVirtualFile(), so we don't hit the
        // disk again
        // FIXME: We definitely want to re-use the existing MemoryBuffer, rather
        // than mmap the files several times.
        auto Entry =
            ToFileManager.getOptionalFileRef(Cache->OrigEntry->getName());
        // FIXME: The filename may be a virtual name that does probably not
        // point to a valid file and we get no Entry here. In this case try with
        // the memory buffer below.
        if (Entry)
          ToID = ToSM.createFileID(*Entry, ToIncludeLocOrFakeLoc,
                                   FromSLoc.getFile().getFileCharacteristic());
      }
    }

    if (ToID.isInvalid() || IsBuiltin) {
      // FIXME: We want to re-use the existing MemoryBuffer!
      std::optional<llvm::MemoryBufferRef> FromBuf =
          Cache->getBufferOrNone(FromContext.getDiagnostics(),
                                 FromSM.getFileManager(), SourceLocation{});
      if (!FromBuf)
        return llvm::make_error<ASTImportError>(ASTImportError::Unknown);

      std::unique_ptr<llvm::MemoryBuffer> ToBuf =
          llvm::MemoryBuffer::getMemBufferCopy(FromBuf->getBuffer(),
                                               FromBuf->getBufferIdentifier());
      ToID = ToSM.createFileID(std::move(ToBuf),
                               FromSLoc.getFile().getFileCharacteristic());
    }
  }

  assert(ToID.isValid() && "Unexpected invalid fileID was created.");

  ImportedFileIDs[FromID] = ToID;
  return ToID;
}

Expected<CXXCtorInitializer *> ASTImporter::Import(CXXCtorInitializer *From) {
  ExpectedExpr ToExprOrErr = Import(From->getInit());
  if (!ToExprOrErr)
    return ToExprOrErr.takeError();

  auto LParenLocOrErr = Import(From->getLParenLoc());
  if (!LParenLocOrErr)
    return LParenLocOrErr.takeError();

  auto RParenLocOrErr = Import(From->getRParenLoc());
  if (!RParenLocOrErr)
    return RParenLocOrErr.takeError();

  if (From->isBaseInitializer()) {
    auto ToTInfoOrErr = Import(From->getTypeSourceInfo());
    if (!ToTInfoOrErr)
      return ToTInfoOrErr.takeError();

    SourceLocation EllipsisLoc;
    if (From->isPackExpansion())
      if (Error Err = importInto(EllipsisLoc, From->getEllipsisLoc()))
        return std::move(Err);

    return new (ToContext) CXXCtorInitializer(
        ToContext, *ToTInfoOrErr, From->isBaseVirtual(), *LParenLocOrErr,
        *ToExprOrErr, *RParenLocOrErr, EllipsisLoc);
  } else if (From->isMemberInitializer()) {
    ExpectedDecl ToFieldOrErr = Import(From->getMember());
    if (!ToFieldOrErr)
      return ToFieldOrErr.takeError();

    auto MemberLocOrErr = Import(From->getMemberLocation());
    if (!MemberLocOrErr)
      return MemberLocOrErr.takeError();

    return new (ToContext) CXXCtorInitializer(
        ToContext, cast_or_null<FieldDecl>(*ToFieldOrErr), *MemberLocOrErr,
        *LParenLocOrErr, *ToExprOrErr, *RParenLocOrErr);
  } else if (From->isIndirectMemberInitializer()) {
    ExpectedDecl ToIFieldOrErr = Import(From->getIndirectMember());
    if (!ToIFieldOrErr)
      return ToIFieldOrErr.takeError();

    auto MemberLocOrErr = Import(From->getMemberLocation());
    if (!MemberLocOrErr)
      return MemberLocOrErr.takeError();

    return new (ToContext) CXXCtorInitializer(
        ToContext, cast_or_null<IndirectFieldDecl>(*ToIFieldOrErr),
        *MemberLocOrErr, *LParenLocOrErr, *ToExprOrErr, *RParenLocOrErr);
  } else if (From->isDelegatingInitializer()) {
    auto ToTInfoOrErr = Import(From->getTypeSourceInfo());
    if (!ToTInfoOrErr)
      return ToTInfoOrErr.takeError();

    return new (ToContext)
        CXXCtorInitializer(ToContext, *ToTInfoOrErr, *LParenLocOrErr,
                           *ToExprOrErr, *RParenLocOrErr);
  } else {
    // FIXME: assert?
    return make_error<ASTImportError>();
  }
}

Expected<CXXBaseSpecifier *>
ASTImporter::Import(const CXXBaseSpecifier *BaseSpec) {
  auto Pos = ImportedCXXBaseSpecifiers.find(BaseSpec);
  if (Pos != ImportedCXXBaseSpecifiers.end())
    return Pos->second;

  Expected<SourceRange> ToSourceRange = Import(BaseSpec->getSourceRange());
  if (!ToSourceRange)
    return ToSourceRange.takeError();
  Expected<TypeSourceInfo *> ToTSI = Import(BaseSpec->getTypeSourceInfo());
  if (!ToTSI)
    return ToTSI.takeError();
  ExpectedSLoc ToEllipsisLoc = Import(BaseSpec->getEllipsisLoc());
  if (!ToEllipsisLoc)
    return ToEllipsisLoc.takeError();
  CXXBaseSpecifier *Imported = new (ToContext) CXXBaseSpecifier(
      *ToSourceRange, BaseSpec->isVirtual(), BaseSpec->isBaseOfClass(),
      BaseSpec->getAccessSpecifierAsWritten(), *ToTSI, *ToEllipsisLoc);
  ImportedCXXBaseSpecifiers[BaseSpec] = Imported;
  return Imported;
}

llvm::Expected<APValue> ASTImporter::Import(const APValue &FromValue) {
  ASTNodeImporter Importer(*this);
  return Importer.ImportAPValue(FromValue);
}

Error ASTImporter::ImportDefinition(Decl *From) {
  ExpectedDecl ToOrErr = Import(From);
  if (!ToOrErr)
    return ToOrErr.takeError();
  Decl *To = *ToOrErr;

  auto *FromDC = cast<DeclContext>(From);
  ASTNodeImporter Importer(*this);

  if (auto *ToRecord = dyn_cast<RecordDecl>(To)) {
    if (!ToRecord->getDefinition()) {
      return Importer.ImportDefinition(
          cast<RecordDecl>(FromDC), ToRecord,
          ASTNodeImporter::IDK_Everything);
    }
  }

  if (auto *ToEnum = dyn_cast<EnumDecl>(To)) {
    if (!ToEnum->getDefinition()) {
      return Importer.ImportDefinition(
          cast<EnumDecl>(FromDC), ToEnum, ASTNodeImporter::IDK_Everything);
    }
  }

  if (auto *ToIFace = dyn_cast<ObjCInterfaceDecl>(To)) {
    if (!ToIFace->getDefinition()) {
      return Importer.ImportDefinition(
          cast<ObjCInterfaceDecl>(FromDC), ToIFace,
          ASTNodeImporter::IDK_Everything);
    }
  }

  if (auto *ToProto = dyn_cast<ObjCProtocolDecl>(To)) {
    if (!ToProto->getDefinition()) {
      return Importer.ImportDefinition(
          cast<ObjCProtocolDecl>(FromDC), ToProto,
          ASTNodeImporter::IDK_Everything);
    }
  }

  return Importer.ImportDeclContext(FromDC, true);
}

Expected<DeclarationName> ASTImporter::Import(DeclarationName FromName) {
  if (!FromName)
    return DeclarationName{};

  switch (FromName.getNameKind()) {
  case DeclarationName::Identifier:
    return DeclarationName(Import(FromName.getAsIdentifierInfo()));

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    if (auto ToSelOrErr = Import(FromName.getObjCSelector()))
      return DeclarationName(*ToSelOrErr);
    else
      return ToSelOrErr.takeError();

  case DeclarationName::CXXConstructorName: {
    if (auto ToTyOrErr = Import(FromName.getCXXNameType()))
      return ToContext.DeclarationNames.getCXXConstructorName(
          ToContext.getCanonicalType(*ToTyOrErr));
    else
      return ToTyOrErr.takeError();
  }

  case DeclarationName::CXXDestructorName: {
    if (auto ToTyOrErr = Import(FromName.getCXXNameType()))
      return ToContext.DeclarationNames.getCXXDestructorName(
          ToContext.getCanonicalType(*ToTyOrErr));
    else
      return ToTyOrErr.takeError();
  }

  case DeclarationName::CXXDeductionGuideName: {
    if (auto ToTemplateOrErr = Import(FromName.getCXXDeductionGuideTemplate()))
      return ToContext.DeclarationNames.getCXXDeductionGuideName(
          cast<TemplateDecl>(*ToTemplateOrErr));
    else
      return ToTemplateOrErr.takeError();
  }

  case DeclarationName::CXXConversionFunctionName: {
    if (auto ToTyOrErr = Import(FromName.getCXXNameType()))
      return ToContext.DeclarationNames.getCXXConversionFunctionName(
          ToContext.getCanonicalType(*ToTyOrErr));
    else
      return ToTyOrErr.takeError();
  }

  case DeclarationName::CXXOperatorName:
    return ToContext.DeclarationNames.getCXXOperatorName(
                                          FromName.getCXXOverloadedOperator());

  case DeclarationName::CXXLiteralOperatorName:
    return ToContext.DeclarationNames.getCXXLiteralOperatorName(
        Import(FromName.getCXXLiteralIdentifier()));

  case DeclarationName::CXXUsingDirective:
    // FIXME: STATICS!
    return DeclarationName::getUsingDirectiveName();
  }

  llvm_unreachable("Invalid DeclarationName Kind!");
}

IdentifierInfo *ASTImporter::Import(const IdentifierInfo *FromId) {
  if (!FromId)
    return nullptr;

  IdentifierInfo *ToId = &ToContext.Idents.get(FromId->getName());

  if (!ToId->getBuiltinID() && FromId->getBuiltinID())
    ToId->setBuiltinID(FromId->getBuiltinID());

  return ToId;
}

Expected<Selector> ASTImporter::Import(Selector FromSel) {
  if (FromSel.isNull())
    return Selector{};

  SmallVector<const IdentifierInfo *, 4> Idents;
  Idents.push_back(Import(FromSel.getIdentifierInfoForSlot(0)));
  for (unsigned I = 1, N = FromSel.getNumArgs(); I < N; ++I)
    Idents.push_back(Import(FromSel.getIdentifierInfoForSlot(I)));
  return ToContext.Selectors.getSelector(FromSel.getNumArgs(), Idents.data());
}

llvm::Expected<APValue>
ASTNodeImporter::ImportAPValue(const APValue &FromValue) {
  APValue Result;
  llvm::Error Err = llvm::Error::success();
  auto ImportLoop = [&](const APValue *From, APValue *To, unsigned Size) {
    for (unsigned Idx = 0; Idx < Size; Idx++) {
      APValue Tmp = importChecked(Err, From[Idx]);
      To[Idx] = Tmp;
    }
  };
  switch (FromValue.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
  case APValue::Int:
  case APValue::Float:
  case APValue::FixedPoint:
  case APValue::ComplexInt:
  case APValue::ComplexFloat:
    Result = FromValue;
    break;
  case APValue::Vector: {
    Result.MakeVector();
    MutableArrayRef<APValue> Elts =
        Result.setVectorUninit(FromValue.getVectorLength());
    ImportLoop(((const APValue::Vec *)(const char *)&FromValue.Data)->Elts,
               Elts.data(), FromValue.getVectorLength());
    break;
  }
  case APValue::Array:
    Result.MakeArray(FromValue.getArrayInitializedElts(),
                     FromValue.getArraySize());
    ImportLoop(((const APValue::Arr *)(const char *)&FromValue.Data)->Elts,
               ((const APValue::Arr *)(const char *)&Result.Data)->Elts,
               FromValue.getArrayInitializedElts());
    break;
  case APValue::Struct:
    Result.MakeStruct(FromValue.getStructNumBases(),
                      FromValue.getStructNumFields());
    ImportLoop(
        ((const APValue::StructData *)(const char *)&FromValue.Data)->Elts,
        ((const APValue::StructData *)(const char *)&Result.Data)->Elts,
        FromValue.getStructNumBases() + FromValue.getStructNumFields());
    break;
  case APValue::Union: {
    Result.MakeUnion();
    const Decl *ImpFDecl = importChecked(Err, FromValue.getUnionField());
    APValue ImpValue = importChecked(Err, FromValue.getUnionValue());
    if (Err)
      return std::move(Err);
    Result.setUnion(cast<FieldDecl>(ImpFDecl), ImpValue);
    break;
  }
  case APValue::AddrLabelDiff: {
    Result.MakeAddrLabelDiff();
    const Expr *ImpLHS = importChecked(Err, FromValue.getAddrLabelDiffLHS());
    const Expr *ImpRHS = importChecked(Err, FromValue.getAddrLabelDiffRHS());
    if (Err)
      return std::move(Err);
    Result.setAddrLabelDiff(cast<AddrLabelExpr>(ImpLHS),
                            cast<AddrLabelExpr>(ImpRHS));
    break;
  }
  case APValue::MemberPointer: {
    const Decl *ImpMemPtrDecl =
        importChecked(Err, FromValue.getMemberPointerDecl());
    if (Err)
      return std::move(Err);
    MutableArrayRef<const CXXRecordDecl *> ToPath =
        Result.setMemberPointerUninit(
            cast<const ValueDecl>(ImpMemPtrDecl),
            FromValue.isMemberPointerToDerivedMember(),
            FromValue.getMemberPointerPath().size());
    llvm::ArrayRef<const CXXRecordDecl *> FromPath =
        Result.getMemberPointerPath();
    for (unsigned Idx = 0; Idx < FromValue.getMemberPointerPath().size();
         Idx++) {
      const Decl *ImpDecl = importChecked(Err, FromPath[Idx]);
      if (Err)
        return std::move(Err);
      ToPath[Idx] = cast<const CXXRecordDecl>(ImpDecl->getCanonicalDecl());
    }
    break;
  }
  case APValue::LValue:
    APValue::LValueBase Base;
    QualType FromElemTy;
    if (FromValue.getLValueBase()) {
      assert(!FromValue.getLValueBase().is<DynamicAllocLValue>() &&
             "in C++20 dynamic allocation are transient so they shouldn't "
             "appear in the AST");
      if (!FromValue.getLValueBase().is<TypeInfoLValue>()) {
        if (const auto *E =
                FromValue.getLValueBase().dyn_cast<const Expr *>()) {
          FromElemTy = E->getType();
          const Expr *ImpExpr = importChecked(Err, E);
          if (Err)
            return std::move(Err);
          Base = APValue::LValueBase(ImpExpr,
                                     FromValue.getLValueBase().getCallIndex(),
                                     FromValue.getLValueBase().getVersion());
        } else {
          FromElemTy =
              FromValue.getLValueBase().get<const ValueDecl *>()->getType();
          const Decl *ImpDecl = importChecked(
              Err, FromValue.getLValueBase().get<const ValueDecl *>());
          if (Err)
            return std::move(Err);
          Base = APValue::LValueBase(cast<ValueDecl>(ImpDecl),
                                     FromValue.getLValueBase().getCallIndex(),
                                     FromValue.getLValueBase().getVersion());
        }
      } else {
        FromElemTy = FromValue.getLValueBase().getTypeInfoType();
        const Type *ImpTypeInfo = importChecked(
            Err, FromValue.getLValueBase().get<TypeInfoLValue>().getType());
        QualType ImpType =
            importChecked(Err, FromValue.getLValueBase().getTypeInfoType());
        if (Err)
          return std::move(Err);
        Base = APValue::LValueBase::getTypeInfo(TypeInfoLValue(ImpTypeInfo),
                                                ImpType);
      }
    }
    CharUnits Offset = FromValue.getLValueOffset();
    unsigned PathLength = FromValue.getLValuePath().size();
    Result.MakeLValue();
    if (FromValue.hasLValuePath()) {
      MutableArrayRef<APValue::LValuePathEntry> ToPath = Result.setLValueUninit(
          Base, Offset, PathLength, FromValue.isLValueOnePastTheEnd(),
          FromValue.isNullPointer());
      llvm::ArrayRef<APValue::LValuePathEntry> FromPath =
          FromValue.getLValuePath();
      for (unsigned LoopIdx = 0; LoopIdx < PathLength; LoopIdx++) {
        if (FromElemTy->isRecordType()) {
          const Decl *FromDecl =
              FromPath[LoopIdx].getAsBaseOrMember().getPointer();
          const Decl *ImpDecl = importChecked(Err, FromDecl);
          if (Err)
            return std::move(Err);
          if (auto *RD = dyn_cast<CXXRecordDecl>(FromDecl))
            FromElemTy = Importer.FromContext.getRecordType(RD);
          else
            FromElemTy = cast<ValueDecl>(FromDecl)->getType();
          ToPath[LoopIdx] = APValue::LValuePathEntry(APValue::BaseOrMemberType(
              ImpDecl, FromPath[LoopIdx].getAsBaseOrMember().getInt()));
        } else {
          FromElemTy =
              Importer.FromContext.getAsArrayType(FromElemTy)->getElementType();
          ToPath[LoopIdx] = APValue::LValuePathEntry::ArrayIndex(
              FromPath[LoopIdx].getAsArrayIndex());
        }
      }
    } else
      Result.setLValue(Base, Offset, APValue::NoLValuePath{},
                       FromValue.isNullPointer());
  }
  if (Err)
    return std::move(Err);
  return Result;
}

Expected<DeclarationName> ASTImporter::HandleNameConflict(DeclarationName Name,
                                                          DeclContext *DC,
                                                          unsigned IDNS,
                                                          NamedDecl **Decls,
                                                          unsigned NumDecls) {
  if (ODRHandling == ODRHandlingType::Conservative)
    // Report error at any name conflict.
    return make_error<ASTImportError>(ASTImportError::NameConflict);
  else
    // Allow to create the new Decl with the same name.
    return Name;
}

DiagnosticBuilder ASTImporter::ToDiag(SourceLocation Loc, unsigned DiagID) {
  if (LastDiagFromFrom)
    ToContext.getDiagnostics().notePriorDiagnosticFrom(
      FromContext.getDiagnostics());
  LastDiagFromFrom = false;
  return ToContext.getDiagnostics().Report(Loc, DiagID);
}

DiagnosticBuilder ASTImporter::FromDiag(SourceLocation Loc, unsigned DiagID) {
  if (!LastDiagFromFrom)
    FromContext.getDiagnostics().notePriorDiagnosticFrom(
      ToContext.getDiagnostics());
  LastDiagFromFrom = true;
  return FromContext.getDiagnostics().Report(Loc, DiagID);
}

void ASTImporter::CompleteDecl (Decl *D) {
  if (auto *ID = dyn_cast<ObjCInterfaceDecl>(D)) {
    if (!ID->getDefinition())
      ID->startDefinition();
  }
  else if (auto *PD = dyn_cast<ObjCProtocolDecl>(D)) {
    if (!PD->getDefinition())
      PD->startDefinition();
  }
  else if (auto *TD = dyn_cast<TagDecl>(D)) {
    if (!TD->getDefinition() && !TD->isBeingDefined()) {
      TD->startDefinition();
      TD->setCompleteDefinition(true);
    }
  }
  else {
    assert(0 && "CompleteDecl called on a Decl that can't be completed");
  }
}

Decl *ASTImporter::MapImported(Decl *From, Decl *To) {
  llvm::DenseMap<Decl *, Decl *>::iterator Pos = ImportedDecls.find(From);
  assert((Pos == ImportedDecls.end() || Pos->second == To) &&
      "Try to import an already imported Decl");
  if (Pos != ImportedDecls.end())
    return Pos->second;
  ImportedDecls[From] = To;
  // This mapping should be maintained only in this function. Therefore do not
  // check for additional consistency.
  ImportedFromDecls[To] = From;
  // In the case of TypedefNameDecl we create the Decl first and only then we
  // import and set its DeclContext. So, the DC is still not set when we reach
  // here from GetImportedOrCreateDecl.
  if (To->getDeclContext())
    AddToLookupTable(To);
  return To;
}

std::optional<ASTImportError>
ASTImporter::getImportDeclErrorIfAny(Decl *FromD) const {
  auto Pos = ImportDeclErrors.find(FromD);
  if (Pos != ImportDeclErrors.end())
    return Pos->second;
  else
    return std::nullopt;
}

void ASTImporter::setImportDeclError(Decl *From, ASTImportError Error) {
  auto InsertRes = ImportDeclErrors.insert({From, Error});
  (void)InsertRes;
  // Either we set the error for the first time, or we already had set one and
  // now we want to set the same error.
  assert(InsertRes.second || InsertRes.first->second.Error == Error.Error);
}

bool ASTImporter::IsStructurallyEquivalent(QualType From, QualType To,
                                           bool Complain) {
  llvm::DenseMap<const Type *, const Type *>::iterator Pos =
      ImportedTypes.find(From.getTypePtr());
  if (Pos != ImportedTypes.end()) {
    if (ExpectedType ToFromOrErr = Import(From)) {
      if (ToContext.hasSameType(*ToFromOrErr, To))
        return true;
    } else {
      llvm::consumeError(ToFromOrErr.takeError());
    }
  }

  StructuralEquivalenceContext Ctx(FromContext, ToContext, NonEquivalentDecls,
                                   getStructuralEquivalenceKind(*this), false,
                                   Complain);
  return Ctx.IsEquivalent(From, To);
}
