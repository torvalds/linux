//===- ClangExtDefMapGen.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------------------------===//
//
// Clang tool which creates a list of defined functions and the files in which
// they are defined.
//
//===--------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CrossTU/CrossTranslationUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include <optional>
#include <sstream>
#include <string>

using namespace llvm;
using namespace clang;
using namespace clang::cross_tu;
using namespace clang::tooling;

static cl::OptionCategory
    ClangExtDefMapGenCategory("clang-extdefmapgen options");

class MapExtDefNamesConsumer : public ASTConsumer {
public:
  MapExtDefNamesConsumer(ASTContext &Context,
                         StringRef astFilePath = StringRef())
      : Ctx(Context), SM(Context.getSourceManager()) {
    CurrentFileName = astFilePath.str();
  }

  ~MapExtDefNamesConsumer() {
    // Flush results to standard output.
    llvm::outs() << createCrossTUIndexString(Index);
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    handleDecl(Context.getTranslationUnitDecl());
  }

private:
  void handleDecl(const Decl *D);
  void addIfInMain(const DeclaratorDecl *DD, SourceLocation defStart);

  ASTContext &Ctx;
  SourceManager &SM;
  llvm::StringMap<std::string> Index;
  std::string CurrentFileName;
};

void MapExtDefNamesConsumer::handleDecl(const Decl *D) {
  if (!D)
    return;

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isThisDeclarationADefinition())
      if (const Stmt *Body = FD->getBody())
        addIfInMain(FD, Body->getBeginLoc());
  } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (cross_tu::shouldImport(VD, Ctx) && VD->hasInit())
      if (const Expr *Init = VD->getInit())
        addIfInMain(VD, Init->getBeginLoc());
  }

  if (const auto *DC = dyn_cast<DeclContext>(D))
    for (const Decl *D : DC->decls())
      handleDecl(D);
}

void MapExtDefNamesConsumer::addIfInMain(const DeclaratorDecl *DD,
                                         SourceLocation defStart) {
  std::optional<std::string> LookupName =
      CrossTranslationUnitContext::getLookupName(DD);
  if (!LookupName)
    return;
  assert(!LookupName->empty() && "Lookup name should be non-empty.");

  if (CurrentFileName.empty()) {
    CurrentFileName = std::string(
        SM.getFileEntryForID(SM.getMainFileID())->tryGetRealPathName());
    if (CurrentFileName.empty())
      CurrentFileName = "invalid_file";
  }

  switch (DD->getLinkageInternal()) {
  case Linkage::External:
  case Linkage::VisibleNone:
  case Linkage::UniqueExternal:
    if (SM.isInMainFile(defStart))
      Index[*LookupName] = CurrentFileName;
    break;
  case Linkage::Invalid:
    llvm_unreachable("Linkage has not been computed!");
  default:
    break;
  }
}

class MapExtDefNamesAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<MapExtDefNamesConsumer>(CI.getASTContext());
  }
};

static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

static IntrusiveRefCntPtr<DiagnosticsEngine> Diags;

IntrusiveRefCntPtr<DiagnosticsEngine> GetDiagnosticsEngine() {
  if (Diags) {
    // Call reset to make sure we don't mix errors
    Diags->Reset(false);
    return Diags;
  }

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  TextDiagnosticPrinter *DiagClient =
      new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  DiagClient->setPrefix("clang-extdef-mappping");
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());

  IntrusiveRefCntPtr<DiagnosticsEngine> DiagEngine(
      new DiagnosticsEngine(DiagID, &*DiagOpts, DiagClient));
  Diags.swap(DiagEngine);

  // Retain this one time so it's not destroyed by ASTUnit::LoadFromASTFile
  Diags->Retain();
  return Diags;
}

static CompilerInstance *CI = nullptr;

static bool HandleAST(StringRef AstPath) {

  if (!CI)
    CI = new CompilerInstance();

  IntrusiveRefCntPtr<DiagnosticsEngine> DiagEngine = GetDiagnosticsEngine();

  std::unique_ptr<ASTUnit> Unit = ASTUnit::LoadFromASTFile(
      AstPath.str(), CI->getPCHContainerOperations()->getRawReader(),
      ASTUnit::LoadASTOnly, DiagEngine, CI->getFileSystemOpts(),
      CI->getHeaderSearchOptsPtr());

  if (!Unit)
    return false;

  FileManager FM(CI->getFileSystemOpts());
  SmallString<128> AbsPath(AstPath);
  FM.makeAbsolutePath(AbsPath);

  MapExtDefNamesConsumer Consumer =
      MapExtDefNamesConsumer(Unit->getASTContext(), AbsPath);
  Consumer.HandleTranslationUnit(Unit->getASTContext());

  return true;
}

static int HandleFiles(ArrayRef<std::string> SourceFiles,
                       CompilationDatabase &compilations) {
  std::vector<std::string> SourcesToBeParsed;

  // Loop over all input files, if they are pre-compiled AST
  // process them directly in HandleAST, otherwise put them
  // on a list for ClangTool to handle.
  for (StringRef Src : SourceFiles) {
    if (Src.ends_with(".ast")) {
      if (!HandleAST(Src)) {
        return 1;
      }
    } else {
      SourcesToBeParsed.push_back(Src.str());
    }
  }

  if (!SourcesToBeParsed.empty()) {
    ClangTool Tool(compilations, SourcesToBeParsed);
    return Tool.run(newFrontendActionFactory<MapExtDefNamesAction>().get());
  }

  return 0;
}

int main(int argc, const char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0], false);
  PrettyStackTraceProgram X(argc, argv);

  const char *Overview = "\nThis tool collects the USR name and location "
                         "of external definitions in the source files "
                         "(excluding headers).\n"
                         "Input can be either source files that are compiled "
                         "with compile database or .ast files that are "
                         "created from clang's -emit-ast option.\n";
  auto ExpectedParser = CommonOptionsParser::create(
      argc, argv, ClangExtDefMapGenCategory, cl::ZeroOrMore, Overview);
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();

  return HandleFiles(OptionsParser.getSourcePathList(),
                     OptionsParser.getCompilations());
}
