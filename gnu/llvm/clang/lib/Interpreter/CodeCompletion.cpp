//===------ CodeCompletion.cpp - Code Completion for ClangRepl -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the classes which performs code completion at the REPL.
//
//===----------------------------------------------------------------------===//

#include "clang/Interpreter/CodeCompletion.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/DeclLookups.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Interpreter/Interpreter.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/Debug.h"
#define DEBUG_TYPE "REPLCC"

namespace clang {

const std::string CodeCompletionFileName = "input_line_[Completion]";

clang::CodeCompleteOptions getClangCompleteOpts() {
  clang::CodeCompleteOptions Opts;
  Opts.IncludeCodePatterns = true;
  Opts.IncludeMacros = true;
  Opts.IncludeGlobals = true;
  Opts.IncludeBriefComments = true;
  return Opts;
}

class ReplCompletionConsumer : public CodeCompleteConsumer {
public:
  ReplCompletionConsumer(std::vector<std::string> &Results,
                         ReplCodeCompleter &CC)
      : CodeCompleteConsumer(getClangCompleteOpts()),
        CCAllocator(std::make_shared<GlobalCodeCompletionAllocator>()),
        CCTUInfo(CCAllocator), Results(Results), CC(CC) {}

  // The entry of handling code completion. When the function is called, we
  // create a `Context`-based handler (see classes defined below) to handle each
  // completion result.
  void ProcessCodeCompleteResults(class Sema &S, CodeCompletionContext Context,
                                  CodeCompletionResult *InResults,
                                  unsigned NumResults) final;

  CodeCompletionAllocator &getAllocator() override { return *CCAllocator; }

  CodeCompletionTUInfo &getCodeCompletionTUInfo() override { return CCTUInfo; }

private:
  std::shared_ptr<GlobalCodeCompletionAllocator> CCAllocator;
  CodeCompletionTUInfo CCTUInfo;
  std::vector<std::string> &Results;
  ReplCodeCompleter &CC;
};

/// The class CompletionContextHandler contains four interfaces, each of
/// which handles one type of completion result.
/// Its derived classes are used to create concrete handlers based on
/// \c CodeCompletionContext.
class CompletionContextHandler {
protected:
  CodeCompletionContext CCC;
  std::vector<std::string> &Results;

private:
  Sema &S;

public:
  CompletionContextHandler(Sema &S, CodeCompletionContext CCC,
                           std::vector<std::string> &Results)
      : CCC(CCC), Results(Results), S(S) {}

  virtual ~CompletionContextHandler() = default;
  /// Converts a Declaration completion result to a completion string, and then
  /// stores it in Results.
  virtual void handleDeclaration(const CodeCompletionResult &Result) {
    auto PreferredType = CCC.getPreferredType();
    if (PreferredType.isNull()) {
      Results.push_back(Result.Declaration->getName().str());
      return;
    }

    if (auto *VD = dyn_cast<VarDecl>(Result.Declaration)) {
      auto ArgumentType = VD->getType();
      if (PreferredType->isReferenceType()) {
        QualType RT = PreferredType->castAs<ReferenceType>()->getPointeeType();
        Sema::ReferenceConversions RefConv;
        Sema::ReferenceCompareResult RefRelationship =
            S.CompareReferenceRelationship(SourceLocation(), RT, ArgumentType,
                                           &RefConv);
        switch (RefRelationship) {
        case Sema::Ref_Compatible:
        case Sema::Ref_Related:
          Results.push_back(VD->getName().str());
          break;
        case Sema::Ref_Incompatible:
          break;
        }
      } else if (S.Context.hasSameType(ArgumentType, PreferredType)) {
        Results.push_back(VD->getName().str());
      }
    }
  }

  /// Converts a Keyword completion result to a completion string, and then
  /// stores it in Results.
  virtual void handleKeyword(const CodeCompletionResult &Result) {
    auto Prefix = S.getPreprocessor().getCodeCompletionFilter();
    // Add keyword to the completion results only if we are in a type-aware
    // situation.
    if (!CCC.getBaseType().isNull() || !CCC.getPreferredType().isNull())
      return;
    if (StringRef(Result.Keyword).starts_with(Prefix))
      Results.push_back(Result.Keyword);
  }

  /// Converts a Pattern completion result to a completion string, and then
  /// stores it in Results.
  virtual void handlePattern(const CodeCompletionResult &Result) {}

  /// Converts a Macro completion result to a completion string, and then stores
  /// it in Results.
  virtual void handleMacro(const CodeCompletionResult &Result) {}
};

class DotMemberAccessHandler : public CompletionContextHandler {
public:
  DotMemberAccessHandler(Sema &S, CodeCompletionContext CCC,
                         std::vector<std::string> &Results)
      : CompletionContextHandler(S, CCC, Results) {}
  void handleDeclaration(const CodeCompletionResult &Result) override {
    auto *ID = Result.Declaration->getIdentifier();
    if (!ID)
      return;
    if (!isa<CXXMethodDecl>(Result.Declaration))
      return;
    const auto *Fun = cast<CXXMethodDecl>(Result.Declaration);
    if (Fun->getParent()->getCanonicalDecl() ==
        CCC.getBaseType()->getAsCXXRecordDecl()->getCanonicalDecl()) {
      LLVM_DEBUG(llvm::dbgs() << "[In HandleCodeCompleteDOT] Name : "
                              << ID->getName() << "\n");
      Results.push_back(ID->getName().str());
    }
  }

  void handleKeyword(const CodeCompletionResult &Result) override {}
};

void ReplCompletionConsumer::ProcessCodeCompleteResults(
    class Sema &S, CodeCompletionContext Context,
    CodeCompletionResult *InResults, unsigned NumResults) {

  auto Prefix = S.getPreprocessor().getCodeCompletionFilter();
  CC.Prefix = Prefix;

  std::unique_ptr<CompletionContextHandler> CCH;

  // initialize fine-grained code completion handler based on the code
  // completion context.
  switch (Context.getKind()) {
  case CodeCompletionContext::CCC_DotMemberAccess:
    CCH.reset(new DotMemberAccessHandler(S, Context, this->Results));
    break;
  default:
    CCH.reset(new CompletionContextHandler(S, Context, this->Results));
  };

  for (unsigned I = 0; I < NumResults; I++) {
    auto &Result = InResults[I];
    switch (Result.Kind) {
    case CodeCompletionResult::RK_Declaration:
      if (Result.Hidden) {
        break;
      }
      if (!Result.Declaration->getDeclName().isIdentifier() ||
          !Result.Declaration->getName().starts_with(Prefix)) {
        break;
      }
      CCH->handleDeclaration(Result);
      break;
    case CodeCompletionResult::RK_Keyword:
      CCH->handleKeyword(Result);
      break;
    case CodeCompletionResult::RK_Macro:
      CCH->handleMacro(Result);
      break;
    case CodeCompletionResult::RK_Pattern:
      CCH->handlePattern(Result);
      break;
    }
  }

  std::sort(Results.begin(), Results.end());
}

class IncrementalSyntaxOnlyAction : public SyntaxOnlyAction {
  const CompilerInstance *ParentCI;

public:
  IncrementalSyntaxOnlyAction(const CompilerInstance *ParentCI)
      : ParentCI(ParentCI) {}

protected:
  void ExecuteAction() override;
};

class ExternalSource : public clang::ExternalASTSource {
  TranslationUnitDecl *ChildTUDeclCtxt;
  ASTContext &ParentASTCtxt;
  TranslationUnitDecl *ParentTUDeclCtxt;

  std::unique_ptr<ASTImporter> Importer;

public:
  ExternalSource(ASTContext &ChildASTCtxt, FileManager &ChildFM,
                 ASTContext &ParentASTCtxt, FileManager &ParentFM);
  bool FindExternalVisibleDeclsByName(const DeclContext *DC,
                                      DeclarationName Name) override;
  void
  completeVisibleDeclsMap(const clang::DeclContext *childDeclContext) override;
};

// This method is intended to set up `ExternalASTSource` to the running
// compiler instance before the super `ExecuteAction` triggers parsing
void IncrementalSyntaxOnlyAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  ExternalSource *myExternalSource =
      new ExternalSource(CI.getASTContext(), CI.getFileManager(),
                         ParentCI->getASTContext(), ParentCI->getFileManager());
  llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> astContextExternalSource(
      myExternalSource);
  CI.getASTContext().setExternalSource(astContextExternalSource);
  CI.getASTContext().getTranslationUnitDecl()->setHasExternalVisibleStorage(
      true);

  // Load all external decls into current context. Under the hood, it calls
  // ExternalSource::completeVisibleDeclsMap, which make all decls on the redecl
  // chain visible.
  //
  // This is crucial to code completion on dot members, since a bound variable
  // before "." would be otherwise treated out-of-scope.
  //
  // clang-repl> Foo f1;
  // clang-repl> f1.<tab>
  CI.getASTContext().getTranslationUnitDecl()->lookups();
  SyntaxOnlyAction::ExecuteAction();
}

ExternalSource::ExternalSource(ASTContext &ChildASTCtxt, FileManager &ChildFM,
                               ASTContext &ParentASTCtxt, FileManager &ParentFM)
    : ChildTUDeclCtxt(ChildASTCtxt.getTranslationUnitDecl()),
      ParentASTCtxt(ParentASTCtxt),
      ParentTUDeclCtxt(ParentASTCtxt.getTranslationUnitDecl()) {
  ASTImporter *importer =
      new ASTImporter(ChildASTCtxt, ChildFM, ParentASTCtxt, ParentFM,
                      /*MinimalImport : ON*/ true);
  Importer.reset(importer);
}

bool ExternalSource::FindExternalVisibleDeclsByName(const DeclContext *DC,
                                                    DeclarationName Name) {

  IdentifierTable &ParentIdTable = ParentASTCtxt.Idents;

  auto ParentDeclName =
      DeclarationName(&(ParentIdTable.get(Name.getAsString())));

  DeclContext::lookup_result lookup_result =
      ParentTUDeclCtxt->lookup(ParentDeclName);

  if (!lookup_result.empty()) {
    return true;
  }
  return false;
}

void ExternalSource::completeVisibleDeclsMap(
    const DeclContext *ChildDeclContext) {
  assert(ChildDeclContext && ChildDeclContext == ChildTUDeclCtxt &&
         "No child decl context!");

  if (!ChildDeclContext->hasExternalVisibleStorage())
    return;

  for (auto *DeclCtxt = ParentTUDeclCtxt; DeclCtxt != nullptr;
       DeclCtxt = DeclCtxt->getPreviousDecl()) {
    for (auto &IDeclContext : DeclCtxt->decls()) {
      if (!llvm::isa<NamedDecl>(IDeclContext))
        continue;

      NamedDecl *Decl = llvm::cast<NamedDecl>(IDeclContext);

      auto DeclOrErr = Importer->Import(Decl);
      if (!DeclOrErr) {
        // if an error happens, it usually means the decl has already been
        // imported or the decl is a result of a failed import.  But in our
        // case, every import is fresh each time code completion is
        // triggered. So Import usually doesn't fail. If it does, it just means
        // the related decl can't be used in code completion and we can safely
        // drop it.
        llvm::consumeError(DeclOrErr.takeError());
        continue;
      }

      if (!llvm::isa<NamedDecl>(*DeclOrErr))
        continue;

      NamedDecl *importedNamedDecl = llvm::cast<NamedDecl>(*DeclOrErr);

      SetExternalVisibleDeclsForName(ChildDeclContext,
                                     importedNamedDecl->getDeclName(),
                                     importedNamedDecl);

      if (!llvm::isa<CXXRecordDecl>(importedNamedDecl))
        continue;

      auto *Record = llvm::cast<CXXRecordDecl>(importedNamedDecl);

      if (auto Err = Importer->ImportDefinition(Decl)) {
        // the same as above
        consumeError(std::move(Err));
        continue;
      }

      Record->setHasLoadedFieldsFromExternalStorage(true);
      LLVM_DEBUG(llvm::dbgs()
                 << "\nCXXRecrod : " << Record->getName() << " size(methods): "
                 << std::distance(Record->method_begin(), Record->method_end())
                 << " has def?:  " << Record->hasDefinition()
                 << " # (methods): "
                 << std::distance(Record->getDefinition()->method_begin(),
                                  Record->getDefinition()->method_end())
                 << "\n");
      for (auto *Meth : Record->methods())
        SetExternalVisibleDeclsForName(ChildDeclContext, Meth->getDeclName(),
                                       Meth);
    }
    ChildDeclContext->setHasExternalLexicalStorage(false);
  }
}

void ReplCodeCompleter::codeComplete(CompilerInstance *InterpCI,
                                     llvm::StringRef Content, unsigned Line,
                                     unsigned Col,
                                     const CompilerInstance *ParentCI,
                                     std::vector<std::string> &CCResults) {
  auto DiagOpts = DiagnosticOptions();
  auto consumer = ReplCompletionConsumer(CCResults, *this);

  auto diag = InterpCI->getDiagnosticsPtr();
  std::unique_ptr<ASTUnit> AU(ASTUnit::LoadFromCompilerInvocationAction(
      InterpCI->getInvocationPtr(), std::make_shared<PCHContainerOperations>(),
      diag));
  llvm::SmallVector<clang::StoredDiagnostic, 8> sd = {};
  llvm::SmallVector<const llvm::MemoryBuffer *, 1> tb = {};
  InterpCI->getFrontendOpts().Inputs[0] = FrontendInputFile(
      CodeCompletionFileName, Language::CXX, InputKind::Source);
  auto Act = std::make_unique<IncrementalSyntaxOnlyAction>(ParentCI);
  std::unique_ptr<llvm::MemoryBuffer> MB =
      llvm::MemoryBuffer::getMemBufferCopy(Content, CodeCompletionFileName);
  llvm::SmallVector<ASTUnit::RemappedFile, 4> RemappedFiles;

  RemappedFiles.push_back(std::make_pair(CodeCompletionFileName, MB.get()));
  // we don't want the AU destructor to release the memory buffer that MB
  // owns twice, because MB handles its resource on its own.
  AU->setOwnsRemappedFileBuffers(false);
  AU->CodeComplete(CodeCompletionFileName, 1, Col, RemappedFiles, false, false,
                   false, consumer,
                   std::make_shared<clang::PCHContainerOperations>(), *diag,
                   InterpCI->getLangOpts(), InterpCI->getSourceManager(),
                   InterpCI->getFileManager(), sd, tb, std::move(Act));
}

} // namespace clang
