//===- IndexingAction.cpp - Frontend index action -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Index/IndexingAction.h"
#include "IndexingContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/STLExtras.h"
#include <memory>

using namespace clang;
using namespace clang::index;

namespace {

class IndexPPCallbacks final : public PPCallbacks {
  std::shared_ptr<IndexingContext> IndexCtx;

public:
  IndexPPCallbacks(std::shared_ptr<IndexingContext> IndexCtx)
      : IndexCtx(std::move(IndexCtx)) {}

  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *Args) override {
    IndexCtx->handleMacroReference(*MacroNameTok.getIdentifierInfo(),
                                   Range.getBegin(), *MD.getMacroInfo());
  }

  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override {
    IndexCtx->handleMacroDefined(*MacroNameTok.getIdentifierInfo(),
                                 MacroNameTok.getLocation(),
                                 *MD->getMacroInfo());
  }

  void MacroUndefined(const Token &MacroNameTok, const MacroDefinition &MD,
                      const MacroDirective *Undef) override {
    if (!MD.getMacroInfo())  // Ignore noop #undef.
      return;
    IndexCtx->handleMacroUndefined(*MacroNameTok.getIdentifierInfo(),
                                   MacroNameTok.getLocation(),
                                   *MD.getMacroInfo());
  }

  void Defined(const Token &MacroNameTok, const MacroDefinition &MD,
               SourceRange Range) override {
    if (!MD.getMacroInfo()) // Ignore nonexistent macro.
      return;
    // Note: this is defined(M), not #define M
    IndexCtx->handleMacroReference(*MacroNameTok.getIdentifierInfo(),
                                   MacroNameTok.getLocation(),
                                   *MD.getMacroInfo());
  }
  void Ifdef(SourceLocation Loc, const Token &MacroNameTok,
             const MacroDefinition &MD) override {
    if (!MD.getMacroInfo()) // Ignore non-existent macro.
      return;
    IndexCtx->handleMacroReference(*MacroNameTok.getIdentifierInfo(),
                                   MacroNameTok.getLocation(),
                                   *MD.getMacroInfo());
  }
  void Ifndef(SourceLocation Loc, const Token &MacroNameTok,
              const MacroDefinition &MD) override {
    if (!MD.getMacroInfo()) // Ignore nonexistent macro.
      return;
    IndexCtx->handleMacroReference(*MacroNameTok.getIdentifierInfo(),
                                   MacroNameTok.getLocation(),
                                   *MD.getMacroInfo());
  }

  using PPCallbacks::Elifdef;
  using PPCallbacks::Elifndef;
  void Elifdef(SourceLocation Loc, const Token &MacroNameTok,
               const MacroDefinition &MD) override {
    if (!MD.getMacroInfo()) // Ignore non-existent macro.
      return;
    IndexCtx->handleMacroReference(*MacroNameTok.getIdentifierInfo(),
                                   MacroNameTok.getLocation(),
                                   *MD.getMacroInfo());
  }
  void Elifndef(SourceLocation Loc, const Token &MacroNameTok,
                const MacroDefinition &MD) override {
    if (!MD.getMacroInfo()) // Ignore non-existent macro.
      return;
    IndexCtx->handleMacroReference(*MacroNameTok.getIdentifierInfo(),
                                   MacroNameTok.getLocation(),
                                   *MD.getMacroInfo());
  }
};

class IndexASTConsumer final : public ASTConsumer {
  std::shared_ptr<IndexDataConsumer> DataConsumer;
  std::shared_ptr<IndexingContext> IndexCtx;
  std::shared_ptr<Preprocessor> PP;
  std::function<bool(const Decl *)> ShouldSkipFunctionBody;

public:
  IndexASTConsumer(std::shared_ptr<IndexDataConsumer> DataConsumer,
                   const IndexingOptions &Opts,
                   std::shared_ptr<Preprocessor> PP,
                   std::function<bool(const Decl *)> ShouldSkipFunctionBody)
      : DataConsumer(std::move(DataConsumer)),
        IndexCtx(new IndexingContext(Opts, *this->DataConsumer)),
        PP(std::move(PP)),
        ShouldSkipFunctionBody(std::move(ShouldSkipFunctionBody)) {
    assert(this->DataConsumer != nullptr);
    assert(this->PP != nullptr);
  }

protected:
  void Initialize(ASTContext &Context) override {
    IndexCtx->setASTContext(Context);
    IndexCtx->getDataConsumer().initialize(Context);
    IndexCtx->getDataConsumer().setPreprocessor(PP);
    PP->addPPCallbacks(std::make_unique<IndexPPCallbacks>(IndexCtx));
  }

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    return IndexCtx->indexDeclGroupRef(DG);
  }

  void HandleInterestingDecl(DeclGroupRef DG) override {
    // Ignore deserialized decls.
  }

  void HandleTopLevelDeclInObjCContainer(DeclGroupRef DG) override {
    IndexCtx->indexDeclGroupRef(DG);
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
    DataConsumer->finish();
  }

  bool shouldSkipFunctionBody(Decl *D) override {
    return ShouldSkipFunctionBody(D);
  }
};

class IndexAction final : public ASTFrontendAction {
  std::shared_ptr<IndexDataConsumer> DataConsumer;
  IndexingOptions Opts;

public:
  IndexAction(std::shared_ptr<IndexDataConsumer> DataConsumer,
              const IndexingOptions &Opts)
      : DataConsumer(std::move(DataConsumer)), Opts(Opts) {
    assert(this->DataConsumer != nullptr);
  }

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    return std::make_unique<IndexASTConsumer>(
        DataConsumer, Opts, CI.getPreprocessorPtr(),
        /*ShouldSkipFunctionBody=*/[](const Decl *) { return false; });
  }
};

} // anonymous namespace

std::unique_ptr<ASTConsumer> index::createIndexingASTConsumer(
    std::shared_ptr<IndexDataConsumer> DataConsumer,
    const IndexingOptions &Opts, std::shared_ptr<Preprocessor> PP,
    std::function<bool(const Decl *)> ShouldSkipFunctionBody) {
  return std::make_unique<IndexASTConsumer>(DataConsumer, Opts, PP,
                                            ShouldSkipFunctionBody);
}

std::unique_ptr<ASTConsumer> clang::index::createIndexingASTConsumer(
    std::shared_ptr<IndexDataConsumer> DataConsumer,
    const IndexingOptions &Opts, std::shared_ptr<Preprocessor> PP) {
  std::function<bool(const Decl *)> ShouldSkipFunctionBody = [](const Decl *) {
    return false;
  };
  if (Opts.ShouldTraverseDecl)
    ShouldSkipFunctionBody =
        [ShouldTraverseDecl(Opts.ShouldTraverseDecl)](const Decl *D) {
          return !ShouldTraverseDecl(D);
        };
  return createIndexingASTConsumer(std::move(DataConsumer), Opts, std::move(PP),
                                   std::move(ShouldSkipFunctionBody));
}

std::unique_ptr<FrontendAction>
index::createIndexingAction(std::shared_ptr<IndexDataConsumer> DataConsumer,
                            const IndexingOptions &Opts) {
  assert(DataConsumer != nullptr);
  return std::make_unique<IndexAction>(std::move(DataConsumer), Opts);
}

static bool topLevelDeclVisitor(void *context, const Decl *D) {
  IndexingContext &IndexCtx = *static_cast<IndexingContext *>(context);
  return IndexCtx.indexTopLevelDecl(D);
}

static void indexTranslationUnit(ASTUnit &Unit, IndexingContext &IndexCtx) {
  Unit.visitLocalTopLevelDecls(&IndexCtx, topLevelDeclVisitor);
}

static void indexPreprocessorMacro(const IdentifierInfo *II,
                                   const MacroInfo *MI,
                                   MacroDirective::Kind DirectiveKind,
                                   SourceLocation Loc,
                                   IndexDataConsumer &DataConsumer) {
  // When using modules, it may happen that we find #undef of a macro that
  // was defined in another module. In such case, MI may be nullptr, since
  // we only look for macro definitions in the current TU. In that case,
  // there is nothing to index.
  if (!MI)
    return;

  // Skip implicit visibility change.
  if (DirectiveKind == MacroDirective::MD_Visibility)
    return;

  auto Role = DirectiveKind == MacroDirective::MD_Define
                  ? SymbolRole::Definition
                  : SymbolRole::Undefinition;
  DataConsumer.handleMacroOccurrence(II, MI, static_cast<unsigned>(Role), Loc);
}

static void indexPreprocessorMacros(Preprocessor &PP,
                                    IndexDataConsumer &DataConsumer) {
  for (const auto &M : PP.macros()) {
    for (auto *MD = M.second.getLatest(); MD; MD = MD->getPrevious()) {
      indexPreprocessorMacro(M.first, MD->getMacroInfo(), MD->getKind(),
                             MD->getLocation(), DataConsumer);
    }
  }
}

static void indexPreprocessorModuleMacros(Preprocessor &PP,
                                          serialization::ModuleFile &Mod,
                                          IndexDataConsumer &DataConsumer) {
  for (const auto &M : PP.macros()) {
    if (M.second.getLatest() == nullptr) {
      for (auto *MM : PP.getLeafModuleMacros(M.first)) {
        auto *OwningMod = MM->getOwningModule();
        if (OwningMod && OwningMod->getASTFile() == Mod.File) {
          if (auto *MI = MM->getMacroInfo()) {
            indexPreprocessorMacro(M.first, MI, MacroDirective::MD_Define,
                                   MI->getDefinitionLoc(), DataConsumer);
          }
        }
      }
    }
  }
}

void index::indexASTUnit(ASTUnit &Unit, IndexDataConsumer &DataConsumer,
                         IndexingOptions Opts) {
  IndexingContext IndexCtx(Opts, DataConsumer);
  IndexCtx.setASTContext(Unit.getASTContext());
  DataConsumer.initialize(Unit.getASTContext());
  DataConsumer.setPreprocessor(Unit.getPreprocessorPtr());

  if (Opts.IndexMacrosInPreprocessor)
    indexPreprocessorMacros(Unit.getPreprocessor(), DataConsumer);
  indexTranslationUnit(Unit, IndexCtx);
  DataConsumer.finish();
}

void index::indexTopLevelDecls(ASTContext &Ctx, Preprocessor &PP,
                               ArrayRef<const Decl *> Decls,
                               IndexDataConsumer &DataConsumer,
                               IndexingOptions Opts) {
  IndexingContext IndexCtx(Opts, DataConsumer);
  IndexCtx.setASTContext(Ctx);

  DataConsumer.initialize(Ctx);

  if (Opts.IndexMacrosInPreprocessor)
    indexPreprocessorMacros(PP, DataConsumer);

  for (const Decl *D : Decls)
    IndexCtx.indexTopLevelDecl(D);
  DataConsumer.finish();
}

std::unique_ptr<PPCallbacks>
index::indexMacrosCallback(IndexDataConsumer &Consumer, IndexingOptions Opts) {
  return std::make_unique<IndexPPCallbacks>(
      std::make_shared<IndexingContext>(Opts, Consumer));
}

void index::indexModuleFile(serialization::ModuleFile &Mod, ASTReader &Reader,
                            IndexDataConsumer &DataConsumer,
                            IndexingOptions Opts) {
  ASTContext &Ctx = Reader.getContext();
  IndexingContext IndexCtx(Opts, DataConsumer);
  IndexCtx.setASTContext(Ctx);
  DataConsumer.initialize(Ctx);

  if (Opts.IndexMacrosInPreprocessor) {
    indexPreprocessorModuleMacros(Reader.getPreprocessor(), Mod, DataConsumer);
  }

  for (const Decl *D : Reader.getModuleFileLevelDecls(Mod)) {
    IndexCtx.indexTopLevelDecl(D);
  }
  DataConsumer.finish();
}
