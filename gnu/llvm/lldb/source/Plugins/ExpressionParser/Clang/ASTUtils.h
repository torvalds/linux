//===-- ASTUtils.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_ASTUTILS_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_ASTUTILS_H

#include "clang/Basic/ASTSourceDescriptor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"
#include <optional>

namespace clang {

class Module;

} // namespace clang

namespace lldb_private {

/// Wraps an ExternalASTSource into an ExternalSemaSource. Doesn't take
/// ownership of the provided source.
class ExternalASTSourceWrapper : public clang::ExternalSemaSource {
  ExternalASTSource *m_Source;

public:
  ExternalASTSourceWrapper(ExternalASTSource *Source) : m_Source(Source) {
    assert(m_Source && "Can't wrap nullptr ExternalASTSource");
  }

  ~ExternalASTSourceWrapper() override;

  clang::Decl *GetExternalDecl(clang::GlobalDeclID ID) override {
    return m_Source->GetExternalDecl(ID);
  }

  clang::Selector GetExternalSelector(uint32_t ID) override {
    return m_Source->GetExternalSelector(ID);
  }

  uint32_t GetNumExternalSelectors() override {
    return m_Source->GetNumExternalSelectors();
  }

  clang::Stmt *GetExternalDeclStmt(uint64_t Offset) override {
    return m_Source->GetExternalDeclStmt(Offset);
  }

  clang::CXXCtorInitializer **
  GetExternalCXXCtorInitializers(uint64_t Offset) override {
    return m_Source->GetExternalCXXCtorInitializers(Offset);
  }

  clang::CXXBaseSpecifier *
  GetExternalCXXBaseSpecifiers(uint64_t Offset) override {
    return m_Source->GetExternalCXXBaseSpecifiers(Offset);
  }

  void updateOutOfDateIdentifier(const clang::IdentifierInfo &II) override {
    m_Source->updateOutOfDateIdentifier(II);
  }

  bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                      clang::DeclarationName Name) override {
    return m_Source->FindExternalVisibleDeclsByName(DC, Name);
  }

  void completeVisibleDeclsMap(const clang::DeclContext *DC) override {
    m_Source->completeVisibleDeclsMap(DC);
  }

  clang::Module *getModule(unsigned ID) override {
    return m_Source->getModule(ID);
  }

  std::optional<clang::ASTSourceDescriptor>
  getSourceDescriptor(unsigned ID) override {
    return m_Source->getSourceDescriptor(ID);
  }

  ExtKind hasExternalDefinitions(const clang::Decl *D) override {
    return m_Source->hasExternalDefinitions(D);
  }

  void FindExternalLexicalDecls(
      const clang::DeclContext *DC,
      llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
      llvm::SmallVectorImpl<clang::Decl *> &Result) override {
    m_Source->FindExternalLexicalDecls(DC, IsKindWeWant, Result);
  }

  void
  FindFileRegionDecls(clang::FileID File, unsigned Offset, unsigned Length,
                      llvm::SmallVectorImpl<clang::Decl *> &Decls) override {
    m_Source->FindFileRegionDecls(File, Offset, Length, Decls);
  }

  void CompleteRedeclChain(const clang::Decl *D) override {
    m_Source->CompleteRedeclChain(D);
  }

  void CompleteType(clang::TagDecl *Tag) override {
    m_Source->CompleteType(Tag);
  }

  void CompleteType(clang::ObjCInterfaceDecl *Class) override {
    m_Source->CompleteType(Class);
  }

  void ReadComments() override { m_Source->ReadComments(); }

  void StartedDeserializing() override { m_Source->StartedDeserializing(); }

  void FinishedDeserializing() override { m_Source->FinishedDeserializing(); }

  void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
    m_Source->StartTranslationUnit(Consumer);
  }

  void PrintStats() override;

  bool layoutRecordType(
      const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &BaseOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &VirtualBaseOffsets) override {
    return m_Source->layoutRecordType(Record, Size, Alignment, FieldOffsets,
                                      BaseOffsets, VirtualBaseOffsets);
  }
};

/// Wraps an ASTConsumer into an SemaConsumer. Doesn't take ownership of the
/// provided consumer. If the provided ASTConsumer is also a SemaConsumer,
/// the wrapper will also forward SemaConsumer functions.
class ASTConsumerForwarder : public clang::SemaConsumer {
  clang::ASTConsumer *m_c;
  clang::SemaConsumer *m_sc;

public:
  ASTConsumerForwarder(clang::ASTConsumer *c) : m_c(c) {
    m_sc = llvm::dyn_cast<clang::SemaConsumer>(m_c);
  }

  ~ASTConsumerForwarder() override;

  void Initialize(clang::ASTContext &Context) override {
    m_c->Initialize(Context);
  }

  bool HandleTopLevelDecl(clang::DeclGroupRef D) override {
    return m_c->HandleTopLevelDecl(D);
  }

  void HandleInlineFunctionDefinition(clang::FunctionDecl *D) override {
    m_c->HandleInlineFunctionDefinition(D);
  }

  void HandleInterestingDecl(clang::DeclGroupRef D) override {
    m_c->HandleInterestingDecl(D);
  }

  void HandleTranslationUnit(clang::ASTContext &Ctx) override {
    m_c->HandleTranslationUnit(Ctx);
  }

  void HandleTagDeclDefinition(clang::TagDecl *D) override {
    m_c->HandleTagDeclDefinition(D);
  }

  void HandleTagDeclRequiredDefinition(const clang::TagDecl *D) override {
    m_c->HandleTagDeclRequiredDefinition(D);
  }

  void HandleCXXImplicitFunctionInstantiation(clang::FunctionDecl *D) override {
    m_c->HandleCXXImplicitFunctionInstantiation(D);
  }

  void HandleTopLevelDeclInObjCContainer(clang::DeclGroupRef D) override {
    m_c->HandleTopLevelDeclInObjCContainer(D);
  }

  void HandleImplicitImportDecl(clang::ImportDecl *D) override {
    m_c->HandleImplicitImportDecl(D);
  }

  void CompleteTentativeDefinition(clang::VarDecl *D) override {
    m_c->CompleteTentativeDefinition(D);
  }

  void AssignInheritanceModel(clang::CXXRecordDecl *RD) override {
    m_c->AssignInheritanceModel(RD);
  }

  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *D) override {
    m_c->HandleCXXStaticMemberVarInstantiation(D);
  }

  void HandleVTable(clang::CXXRecordDecl *RD) override {
    m_c->HandleVTable(RD);
  }

  clang::ASTMutationListener *GetASTMutationListener() override {
    return m_c->GetASTMutationListener();
  }

  clang::ASTDeserializationListener *GetASTDeserializationListener() override {
    return m_c->GetASTDeserializationListener();
  }

  void PrintStats() override;

  void InitializeSema(clang::Sema &S) override {
    if (m_sc)
      m_sc->InitializeSema(S);
  }

  /// Inform the semantic consumer that Sema is no longer available.
  void ForgetSema() override {
    if (m_sc)
      m_sc->ForgetSema();
  }

  bool shouldSkipFunctionBody(clang::Decl *D) override {
    return m_c->shouldSkipFunctionBody(D);
  }
};

/// A ExternalSemaSource multiplexer that prioritizes its sources.
///
/// This ExternalSemaSource will forward all requests to its attached sources.
/// However, unlike a normal multiplexer it will not forward a request to all
/// sources, but instead give priority to certain sources. If a source with a
/// higher priority can fulfill a request, all sources with a lower priority
/// will not receive the request.
///
/// This class is mostly use to multiplex between sources of different
/// 'quality', e.g. a C++ modules and debug information. The C++ module will
/// provide more accurate replies to the requests, but might not be able to
/// answer all requests. The debug information will be used as a fallback then
/// to provide information that is not in the C++ module.
class SemaSourceWithPriorities : public clang::ExternalSemaSource {

private:
  /// The sources ordered in decreasing priority.
  llvm::SmallVector<clang::ExternalSemaSource *, 2> Sources;

public:
  /// Construct a SemaSourceWithPriorities with a 'high quality' source that
  /// has the higher priority and a 'low quality' source that will be used
  /// as a fallback.
  SemaSourceWithPriorities(clang::ExternalSemaSource &high_quality_source,
                           clang::ExternalSemaSource &low_quality_source) {
    Sources.push_back(&high_quality_source);
    Sources.push_back(&low_quality_source);
  }

  ~SemaSourceWithPriorities() override;

  void addSource(clang::ExternalSemaSource &source) {
    Sources.push_back(&source);
  }

  //===--------------------------------------------------------------------===//
  // ExternalASTSource.
  //===--------------------------------------------------------------------===//

  clang::Decl *GetExternalDecl(clang::GlobalDeclID ID) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (clang::Decl *Result = Sources[i]->GetExternalDecl(ID))
        return Result;
    return nullptr;
  }

  void CompleteRedeclChain(const clang::Decl *D) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->CompleteRedeclChain(D);
  }

  clang::Selector GetExternalSelector(uint32_t ID) override {
    clang::Selector Sel;
    for (size_t i = 0; i < Sources.size(); ++i) {
      Sel = Sources[i]->GetExternalSelector(ID);
      if (!Sel.isNull())
        return Sel;
    }
    return Sel;
  }

  uint32_t GetNumExternalSelectors() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (uint32_t total = Sources[i]->GetNumExternalSelectors())
        return total;
    return 0;
  }

  clang::Stmt *GetExternalDeclStmt(uint64_t Offset) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (clang::Stmt *Result = Sources[i]->GetExternalDeclStmt(Offset))
        return Result;
    return nullptr;
  }

  clang::CXXBaseSpecifier *
  GetExternalCXXBaseSpecifiers(uint64_t Offset) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (clang::CXXBaseSpecifier *R =
              Sources[i]->GetExternalCXXBaseSpecifiers(Offset))
        return R;
    return nullptr;
  }

  clang::CXXCtorInitializer **
  GetExternalCXXCtorInitializers(uint64_t Offset) override {
    for (auto *S : Sources)
      if (auto *R = S->GetExternalCXXCtorInitializers(Offset))
        return R;
    return nullptr;
  }

  ExtKind hasExternalDefinitions(const clang::Decl *D) override {
    for (const auto &S : Sources)
      if (auto EK = S->hasExternalDefinitions(D))
        if (EK != EK_ReplyHazy)
          return EK;
    return EK_ReplyHazy;
  }

  bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                      clang::DeclarationName Name) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (Sources[i]->FindExternalVisibleDeclsByName(DC, Name))
        return true;
    return false;
  }

  void completeVisibleDeclsMap(const clang::DeclContext *DC) override {
    // FIXME: Only one source should be able to complete the decls map.
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->completeVisibleDeclsMap(DC);
  }

  void FindExternalLexicalDecls(
      const clang::DeclContext *DC,
      llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
      llvm::SmallVectorImpl<clang::Decl *> &Result) override {
    for (size_t i = 0; i < Sources.size(); ++i) {
      Sources[i]->FindExternalLexicalDecls(DC, IsKindWeWant, Result);
      if (!Result.empty())
        return;
    }
  }

  void
  FindFileRegionDecls(clang::FileID File, unsigned Offset, unsigned Length,
                      llvm::SmallVectorImpl<clang::Decl *> &Decls) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->FindFileRegionDecls(File, Offset, Length, Decls);
  }

  void CompleteType(clang::TagDecl *Tag) override {
    for (clang::ExternalSemaSource *S : Sources) {
      S->CompleteType(Tag);
      // Stop after the first source completed the type.
      if (Tag->isCompleteDefinition())
        break;
    }
  }

  void CompleteType(clang::ObjCInterfaceDecl *Class) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->CompleteType(Class);
  }

  void ReadComments() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadComments();
  }

  void StartedDeserializing() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->StartedDeserializing();
  }

  void FinishedDeserializing() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->FinishedDeserializing();
  }

  void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->StartTranslationUnit(Consumer);
  }

  void PrintStats() override;

  clang::Module *getModule(unsigned ID) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (auto M = Sources[i]->getModule(ID))
        return M;
    return nullptr;
  }

  bool layoutRecordType(
      const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &BaseOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &VirtualBaseOffsets) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (Sources[i]->layoutRecordType(Record, Size, Alignment, FieldOffsets,
                                       BaseOffsets, VirtualBaseOffsets))
        return true;
    return false;
  }

  void getMemoryBufferSizes(MemoryBufferSizes &sizes) const override {
    for (auto &Source : Sources)
      Source->getMemoryBufferSizes(sizes);
  }

  //===--------------------------------------------------------------------===//
  // ExternalSemaSource.
  //===--------------------------------------------------------------------===//

  void InitializeSema(clang::Sema &S) override {
    for (auto &Source : Sources)
      Source->InitializeSema(S);
  }

  void ForgetSema() override {
    for (auto &Source : Sources)
      Source->ForgetSema();
  }

  void ReadMethodPool(clang::Selector Sel) override {
    for (auto &Source : Sources)
      Source->ReadMethodPool(Sel);
  }

  void updateOutOfDateSelector(clang::Selector Sel) override {
    for (auto &Source : Sources)
      Source->updateOutOfDateSelector(Sel);
  }

  void ReadKnownNamespaces(
      llvm::SmallVectorImpl<clang::NamespaceDecl *> &Namespaces) override {
    for (auto &Source : Sources)
      Source->ReadKnownNamespaces(Namespaces);
  }

  void ReadUndefinedButUsed(
      llvm::MapVector<clang::NamedDecl *, clang::SourceLocation> &Undefined)
      override {
    for (auto &Source : Sources)
      Source->ReadUndefinedButUsed(Undefined);
  }

  void ReadMismatchingDeleteExpressions(
      llvm::MapVector<clang::FieldDecl *,
                      llvm::SmallVector<std::pair<clang::SourceLocation, bool>,
                                        4>> &Exprs) override {
    for (auto &Source : Sources)
      Source->ReadMismatchingDeleteExpressions(Exprs);
  }

  bool LookupUnqualified(clang::LookupResult &R, clang::Scope *S) override {
    for (auto &Source : Sources) {
      Source->LookupUnqualified(R, S);
      if (!R.empty())
        break;
    }

    return !R.empty();
  }

  void ReadTentativeDefinitions(
      llvm::SmallVectorImpl<clang::VarDecl *> &Defs) override {
    for (auto &Source : Sources)
      Source->ReadTentativeDefinitions(Defs);
  }

  void ReadUnusedFileScopedDecls(
      llvm::SmallVectorImpl<const clang::DeclaratorDecl *> &Decls) override {
    for (auto &Source : Sources)
      Source->ReadUnusedFileScopedDecls(Decls);
  }

  void ReadDelegatingConstructors(
      llvm::SmallVectorImpl<clang::CXXConstructorDecl *> &Decls) override {
    for (auto &Source : Sources)
      Source->ReadDelegatingConstructors(Decls);
  }

  void ReadExtVectorDecls(
      llvm::SmallVectorImpl<clang::TypedefNameDecl *> &Decls) override {
    for (auto &Source : Sources)
      Source->ReadExtVectorDecls(Decls);
  }

  void ReadUnusedLocalTypedefNameCandidates(
      llvm::SmallSetVector<const clang::TypedefNameDecl *, 4> &Decls) override {
    for (auto &Source : Sources)
      Source->ReadUnusedLocalTypedefNameCandidates(Decls);
  }

  void ReadReferencedSelectors(
      llvm::SmallVectorImpl<std::pair<clang::Selector, clang::SourceLocation>>
          &Sels) override {
    for (auto &Source : Sources)
      Source->ReadReferencedSelectors(Sels);
  }

  void ReadWeakUndeclaredIdentifiers(
      llvm::SmallVectorImpl<std::pair<clang::IdentifierInfo *, clang::WeakInfo>>
          &WI) override {
    for (auto &Source : Sources)
      Source->ReadWeakUndeclaredIdentifiers(WI);
  }

  void ReadUsedVTables(
      llvm::SmallVectorImpl<clang::ExternalVTableUse> &VTables) override {
    for (auto &Source : Sources)
      Source->ReadUsedVTables(VTables);
  }

  void ReadPendingInstantiations(
      llvm::SmallVectorImpl<
          std::pair<clang::ValueDecl *, clang::SourceLocation>> &Pending)
      override {
    for (auto &Source : Sources)
      Source->ReadPendingInstantiations(Pending);
  }

  void ReadLateParsedTemplates(
      llvm::MapVector<const clang::FunctionDecl *,
                      std::unique_ptr<clang::LateParsedTemplate>> &LPTMap)
      override {
    for (auto &Source : Sources)
      Source->ReadLateParsedTemplates(LPTMap);
  }

  clang::TypoCorrection
  CorrectTypo(const clang::DeclarationNameInfo &Typo, int LookupKind,
              clang::Scope *S, clang::CXXScopeSpec *SS,
              clang::CorrectionCandidateCallback &CCC,
              clang::DeclContext *MemberContext, bool EnteringContext,
              const clang::ObjCObjectPointerType *OPT) override {
    for (auto &Source : Sources) {
      if (clang::TypoCorrection C =
              Source->CorrectTypo(Typo, LookupKind, S, SS, CCC,
                                      MemberContext, EnteringContext, OPT))
        return C;
    }
    return clang::TypoCorrection();
  }

  bool MaybeDiagnoseMissingCompleteType(clang::SourceLocation Loc,
                                        clang::QualType T) override {
    for (auto &Source : Sources) {
      if (Source->MaybeDiagnoseMissingCompleteType(Loc, T))
        return true;
    }
    return false;
  }
};

} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_ASTUTILS_H
