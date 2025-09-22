//===--- TestAST.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Testing/TestAST.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnostic.h"
#include "clang/Testing/CommandLineArgs.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VirtualFileSystem.h"

#include "gtest/gtest.h"
#include <string>

namespace clang {
namespace {

// Captures diagnostics into a vector, optionally reporting errors to gtest.
class StoreDiagnostics : public DiagnosticConsumer {
  std::vector<StoredDiagnostic> &Out;
  bool ReportErrors;
  LangOptions LangOpts;

public:
  StoreDiagnostics(std::vector<StoredDiagnostic> &Out, bool ReportErrors)
      : Out(Out), ReportErrors(ReportErrors) {}

  void BeginSourceFile(const LangOptions &LangOpts,
                       const Preprocessor *) override {
    this->LangOpts = LangOpts;
  }

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    Out.emplace_back(DiagLevel, Info);
    if (ReportErrors && DiagLevel >= DiagnosticsEngine::Error) {
      std::string Text;
      llvm::raw_string_ostream OS(Text);
      TextDiagnostic Renderer(OS, LangOpts,
                              &Info.getDiags()->getDiagnosticOptions());
      Renderer.emitStoredDiagnostic(Out.back());
      ADD_FAILURE() << Text;
    }
  }
};

// Fills in the bits of a CompilerInstance that weren't initialized yet.
// Provides "empty" ASTContext etc if we fail before parsing gets started.
void createMissingComponents(CompilerInstance &Clang) {
  if (!Clang.hasDiagnostics())
    Clang.createDiagnostics();
  if (!Clang.hasFileManager())
    Clang.createFileManager();
  if (!Clang.hasSourceManager())
    Clang.createSourceManager(Clang.getFileManager());
  if (!Clang.hasTarget())
    Clang.createTarget();
  if (!Clang.hasPreprocessor())
    Clang.createPreprocessor(TU_Complete);
  if (!Clang.hasASTConsumer())
    Clang.setASTConsumer(std::make_unique<ASTConsumer>());
  if (!Clang.hasASTContext())
    Clang.createASTContext();
  if (!Clang.hasSema())
    Clang.createSema(TU_Complete, /*CodeCompleteConsumer=*/nullptr);
}

} // namespace

TestAST::TestAST(const TestInputs &In) {
  Clang = std::make_unique<CompilerInstance>(
      std::make_shared<PCHContainerOperations>());
  // If we don't manage to finish parsing, create CompilerInstance components
  // anyway so that the test will see an empty AST instead of crashing.
  auto RecoverFromEarlyExit =
      llvm::make_scope_exit([&] { createMissingComponents(*Clang); });

  // Extra error conditions are reported through diagnostics, set that up first.
  bool ErrorOK = In.ErrorOK || llvm::StringRef(In.Code).contains("error-ok");
  Clang->createDiagnostics(new StoreDiagnostics(Diagnostics, !ErrorOK));

  // Parse cc1 argv, (typically [-std=c++20 input.cc]) into CompilerInvocation.
  std::vector<const char *> Argv;
  std::vector<std::string> LangArgs = getCC1ArgsForTesting(In.Language);
  for (const auto &S : LangArgs)
    Argv.push_back(S.c_str());
  for (const auto &S : In.ExtraArgs)
    Argv.push_back(S.c_str());
  std::string Filename = In.FileName;
  if (Filename.empty())
    Filename = getFilenameForTesting(In.Language).str();
  Argv.push_back(Filename.c_str());
  Clang->setInvocation(std::make_unique<CompilerInvocation>());
  if (!CompilerInvocation::CreateFromArgs(Clang->getInvocation(), Argv,
                                          Clang->getDiagnostics(), "clang")) {
    ADD_FAILURE() << "Failed to create invocation";
    return;
  }
  assert(!Clang->getInvocation().getFrontendOpts().DisableFree);

  // Set up a VFS with only the virtual file visible.
  auto VFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  if (auto Err = VFS->setCurrentWorkingDirectory(In.WorkingDir))
    ADD_FAILURE() << "Failed to setWD: " << Err.message();
  VFS->addFile(Filename, /*ModificationTime=*/0,
               llvm::MemoryBuffer::getMemBufferCopy(In.Code, Filename));
  for (const auto &Extra : In.ExtraFiles)
    VFS->addFile(
        Extra.getKey(), /*ModificationTime=*/0,
        llvm::MemoryBuffer::getMemBufferCopy(Extra.getValue(), Extra.getKey()));
  Clang->createFileManager(VFS);

  // Running the FrontendAction creates the other components: SourceManager,
  // Preprocessor, ASTContext, Sema. Preprocessor needs TargetInfo to be set.
  EXPECT_TRUE(Clang->createTarget());
  Action =
      In.MakeAction ? In.MakeAction() : std::make_unique<SyntaxOnlyAction>();
  const FrontendInputFile &Main = Clang->getFrontendOpts().Inputs.front();
  if (!Action->BeginSourceFile(*Clang, Main)) {
    ADD_FAILURE() << "Failed to BeginSourceFile()";
    Action.reset(); // Don't call EndSourceFile if BeginSourceFile failed.
    return;
  }
  if (auto Err = Action->Execute())
    ADD_FAILURE() << "Failed to Execute(): " << llvm::toString(std::move(Err));

  // Action->EndSourceFile() would destroy the ASTContext, we want to keep it.
  // But notify the preprocessor we're done now.
  Clang->getPreprocessor().EndSourceFile();
  // We're done gathering diagnostics, detach the consumer so we can destroy it.
  Clang->getDiagnosticClient().EndSourceFile();
  Clang->getDiagnostics().setClient(new DiagnosticConsumer(),
                                    /*ShouldOwnClient=*/true);
}

void TestAST::clear() {
  if (Action) {
    // We notified the preprocessor of EOF already, so detach it first.
    // Sema needs the PP alive until after EndSourceFile() though.
    auto PP = Clang->getPreprocessorPtr(); // Keep PP alive for now.
    Clang->setPreprocessor(nullptr);       // Detach so we don't send EOF twice.
    Action->EndSourceFile();               // Destroy ASTContext and Sema.
    // Now Sema is gone, PP can safely be destroyed.
  }
  Action.reset();
  Clang.reset();
  Diagnostics.clear();
}

TestAST &TestAST::operator=(TestAST &&M) {
  clear();
  Action = std::move(M.Action);
  Clang = std::move(M.Clang);
  Diagnostics = std::move(M.Diagnostics);
  return *this;
}

TestAST::TestAST(TestAST &&M) { *this = std::move(M); }

TestAST::~TestAST() { clear(); }

} // end namespace clang
