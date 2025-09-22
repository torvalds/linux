//===-- clang-import-test.cpp - ASTImporter/ExternalASTSource testbed -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExternalASTMerger.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Driver/Types.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Signals.h"
#include "llvm/TargetParser/Host.h"

#include <memory>
#include <string>

using namespace clang;

static llvm::cl::opt<std::string> Expression(
    "expression", llvm::cl::Required,
    llvm::cl::desc("Path to a file containing the expression to parse"));

static llvm::cl::list<std::string>
    Imports("import",
            llvm::cl::desc("Path to a file containing declarations to import"));

static llvm::cl::opt<bool>
    Direct("direct", llvm::cl::Optional,
           llvm::cl::desc("Use the parsed declarations without indirection"));

static llvm::cl::opt<bool> UseOrigins(
    "use-origins", llvm::cl::Optional,
    llvm::cl::desc(
        "Use DeclContext origin information for more accurate lookups"));

static llvm::cl::list<std::string>
    ClangArgs("Xcc",
              llvm::cl::desc("Argument to pass to the CompilerInvocation"),
              llvm::cl::CommaSeparated);

static llvm::cl::opt<std::string>
    Input("x", llvm::cl::Optional,
          llvm::cl::desc("The language to parse (default: c++)"),
          llvm::cl::init("c++"));

static llvm::cl::opt<bool> ObjCARC("objc-arc", llvm::cl::init(false),
                                   llvm::cl::desc("Emable ObjC ARC"));

static llvm::cl::opt<bool> DumpAST("dump-ast", llvm::cl::init(false),
                                   llvm::cl::desc("Dump combined AST"));

static llvm::cl::opt<bool> DumpIR("dump-ir", llvm::cl::init(false),
                                  llvm::cl::desc("Dump IR from final parse"));

namespace init_convenience {
class TestDiagnosticConsumer : public DiagnosticConsumer {
private:
  std::unique_ptr<TextDiagnosticBuffer> Passthrough;
  const LangOptions *LangOpts = nullptr;

public:
  TestDiagnosticConsumer()
      : Passthrough(std::make_unique<TextDiagnosticBuffer>()) {}

  void BeginSourceFile(const LangOptions &LangOpts,
                       const Preprocessor *PP = nullptr) override {
    this->LangOpts = &LangOpts;
    return Passthrough->BeginSourceFile(LangOpts, PP);
  }

  void EndSourceFile() override {
    this->LangOpts = nullptr;
    Passthrough->EndSourceFile();
  }

  bool IncludeInDiagnosticCounts() const override {
    return Passthrough->IncludeInDiagnosticCounts();
  }

private:
  static void PrintSourceForLocation(const SourceLocation &Loc,
                                     SourceManager &SM) {
    const char *LocData = SM.getCharacterData(Loc, /*Invalid=*/nullptr);
    unsigned LocColumn =
        SM.getSpellingColumnNumber(Loc, /*Invalid=*/nullptr) - 1;
    FileID FID = SM.getFileID(Loc);
    llvm::MemoryBufferRef Buffer = SM.getBufferOrFake(FID, Loc);

    assert(LocData >= Buffer.getBufferStart() &&
           LocData < Buffer.getBufferEnd());

    const char *LineBegin = LocData - LocColumn;

    assert(LineBegin >= Buffer.getBufferStart());

    const char *LineEnd = nullptr;

    for (LineEnd = LineBegin; *LineEnd != '\n' && *LineEnd != '\r' &&
                              LineEnd < Buffer.getBufferEnd();
         ++LineEnd)
      ;

    llvm::StringRef LineString(LineBegin, LineEnd - LineBegin);

    llvm::errs() << LineString << '\n';
    llvm::errs().indent(LocColumn);
    llvm::errs() << '^';
    llvm::errs() << '\n';
  }

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    if (Info.hasSourceManager() && LangOpts) {
      SourceManager &SM = Info.getSourceManager();

      if (Info.getLocation().isValid()) {
        Info.getLocation().print(llvm::errs(), SM);
        llvm::errs() << ": ";
      }

      SmallString<16> DiagText;
      Info.FormatDiagnostic(DiagText);
      llvm::errs() << DiagText << '\n';

      if (Info.getLocation().isValid()) {
        PrintSourceForLocation(Info.getLocation(), SM);
      }

      for (const CharSourceRange &Range : Info.getRanges()) {
        bool Invalid = true;
        StringRef Ref = Lexer::getSourceText(Range, SM, *LangOpts, &Invalid);
        if (!Invalid) {
          llvm::errs() << Ref << '\n';
        }
      }
    }
    DiagnosticConsumer::HandleDiagnostic(DiagLevel, Info);
  }
};

std::unique_ptr<CompilerInstance> BuildCompilerInstance() {
  auto Ins = std::make_unique<CompilerInstance>();
  auto DC = std::make_unique<TestDiagnosticConsumer>();
  const bool ShouldOwnClient = true;
  Ins->createDiagnostics(DC.release(), ShouldOwnClient);

  auto Inv = std::make_unique<CompilerInvocation>();

  std::vector<const char *> ClangArgv(ClangArgs.size());
  std::transform(ClangArgs.begin(), ClangArgs.end(), ClangArgv.begin(),
                 [](const std::string &s) -> const char * { return s.data(); });
  CompilerInvocation::CreateFromArgs(*Inv, ClangArgv, Ins->getDiagnostics());

  {
    using namespace driver::types;
    ID Id = lookupTypeForTypeSpecifier(Input.c_str());
    assert(Id != TY_INVALID);
    if (isCXX(Id)) {
      Inv->getLangOpts().CPlusPlus = true;
      Inv->getLangOpts().CPlusPlus11 = true;
      Inv->getHeaderSearchOpts().UseLibcxx = true;
    }
    if (isObjC(Id)) {
      Inv->getLangOpts().ObjC = 1;
    }
  }
  Inv->getLangOpts().ObjCAutoRefCount = ObjCARC;

  Inv->getLangOpts().Bool = true;
  Inv->getLangOpts().WChar = true;
  Inv->getLangOpts().Blocks = true;
  Inv->getLangOpts().DebuggerSupport = true;
  Inv->getLangOpts().SpellChecking = false;
  Inv->getLangOpts().ThreadsafeStatics = false;
  Inv->getLangOpts().AccessControl = false;
  Inv->getLangOpts().DollarIdents = true;
  Inv->getLangOpts().Exceptions = true;
  Inv->getLangOpts().CXXExceptions = true;
  // Needed for testing dynamic_cast.
  Inv->getLangOpts().RTTI = true;
  Inv->getCodeGenOpts().setDebugInfo(llvm::codegenoptions::FullDebugInfo);
  Inv->getTargetOpts().Triple = llvm::sys::getDefaultTargetTriple();

  Ins->setInvocation(std::move(Inv));

  TargetInfo *TI = TargetInfo::CreateTargetInfo(
      Ins->getDiagnostics(), Ins->getInvocation().TargetOpts);
  Ins->setTarget(TI);
  Ins->getTarget().adjust(Ins->getDiagnostics(), Ins->getLangOpts());
  Ins->createFileManager();
  Ins->createSourceManager(Ins->getFileManager());
  Ins->createPreprocessor(TU_Complete);

  return Ins;
}

std::unique_ptr<ASTContext>
BuildASTContext(CompilerInstance &CI, SelectorTable &ST, Builtin::Context &BC) {
  auto &PP = CI.getPreprocessor();
  auto AST = std::make_unique<ASTContext>(
      CI.getLangOpts(), CI.getSourceManager(),
      PP.getIdentifierTable(), ST, BC, PP.TUKind);
  AST->InitBuiltinTypes(CI.getTarget());
  return AST;
}

std::unique_ptr<CodeGenerator> BuildCodeGen(CompilerInstance &CI,
                                            llvm::LLVMContext &LLVMCtx) {
  StringRef ModuleName("$__module");
  return std::unique_ptr<CodeGenerator>(CreateLLVMCodeGen(
      CI.getDiagnostics(), ModuleName, &CI.getVirtualFileSystem(),
      CI.getHeaderSearchOpts(), CI.getPreprocessorOpts(), CI.getCodeGenOpts(),
      LLVMCtx));
}
} // namespace init_convenience

namespace {

/// A container for a CompilerInstance (possibly with an ExternalASTMerger
/// attached to its ASTContext).
///
/// Provides an accessor for the DeclContext origins associated with the
/// ExternalASTMerger (or an empty list of origins if no ExternalASTMerger is
/// attached).
///
/// This is the main unit of parsed source code maintained by clang-import-test.
struct CIAndOrigins {
  using OriginMap = clang::ExternalASTMerger::OriginMap;
  std::unique_ptr<CompilerInstance> CI;

  ASTContext &getASTContext() { return CI->getASTContext(); }
  FileManager &getFileManager() { return CI->getFileManager(); }
  const OriginMap &getOriginMap() {
    static const OriginMap EmptyOriginMap{};
    if (ExternalASTSource *Source = CI->getASTContext().getExternalSource())
      return static_cast<ExternalASTMerger *>(Source)->GetOrigins();
    return EmptyOriginMap;
  }
  DiagnosticConsumer &getDiagnosticClient() {
    return CI->getDiagnosticClient();
  }
  CompilerInstance &getCompilerInstance() { return *CI; }
};

void AddExternalSource(CIAndOrigins &CI,
                       llvm::MutableArrayRef<CIAndOrigins> Imports) {
  ExternalASTMerger::ImporterTarget Target(
      {CI.getASTContext(), CI.getFileManager()});
  llvm::SmallVector<ExternalASTMerger::ImporterSource, 3> Sources;
  for (CIAndOrigins &Import : Imports)
    Sources.emplace_back(Import.getASTContext(), Import.getFileManager(),
                         Import.getOriginMap());
  auto ES = std::make_unique<ExternalASTMerger>(Target, Sources);
  CI.getASTContext().setExternalSource(ES.release());
  CI.getASTContext().getTranslationUnitDecl()->setHasExternalVisibleStorage();
}

CIAndOrigins BuildIndirect(CIAndOrigins &CI) {
  CIAndOrigins IndirectCI{init_convenience::BuildCompilerInstance()};
  auto ST = std::make_unique<SelectorTable>();
  auto BC = std::make_unique<Builtin::Context>();
  std::unique_ptr<ASTContext> AST = init_convenience::BuildASTContext(
      IndirectCI.getCompilerInstance(), *ST, *BC);
  IndirectCI.getCompilerInstance().setASTContext(AST.release());
  AddExternalSource(IndirectCI, CI);
  return IndirectCI;
}

llvm::Error ParseSource(const std::string &Path, CompilerInstance &CI,
                        ASTConsumer &Consumer) {
  SourceManager &SM = CI.getSourceManager();
  auto FE = CI.getFileManager().getFileRef(Path);
  if (!FE) {
    llvm::consumeError(FE.takeError());
    return llvm::make_error<llvm::StringError>(
        llvm::Twine("No such file or directory: ", Path), std::error_code());
  }
  SM.setMainFileID(SM.createFileID(*FE, SourceLocation(), SrcMgr::C_User));
  ParseAST(CI.getPreprocessor(), &Consumer, CI.getASTContext());
  return llvm::Error::success();
}

llvm::Expected<CIAndOrigins> Parse(const std::string &Path,
                                   llvm::MutableArrayRef<CIAndOrigins> Imports,
                                   bool ShouldDumpAST, bool ShouldDumpIR) {
  CIAndOrigins CI{init_convenience::BuildCompilerInstance()};
  auto ST = std::make_unique<SelectorTable>();
  auto BC = std::make_unique<Builtin::Context>();
  std::unique_ptr<ASTContext> AST =
      init_convenience::BuildASTContext(CI.getCompilerInstance(), *ST, *BC);
  CI.getCompilerInstance().setASTContext(AST.release());
  if (Imports.size())
    AddExternalSource(CI, Imports);

  std::vector<std::unique_ptr<ASTConsumer>> ASTConsumers;

  auto LLVMCtx = std::make_unique<llvm::LLVMContext>();
  ASTConsumers.push_back(
      init_convenience::BuildCodeGen(CI.getCompilerInstance(), *LLVMCtx));
  auto &CG = *static_cast<CodeGenerator *>(ASTConsumers.back().get());

  if (ShouldDumpAST)
    ASTConsumers.push_back(CreateASTDumper(nullptr /*Dump to stdout.*/, "",
                                           true, false, false, false,
                                           clang::ADOF_Default));

  CI.getDiagnosticClient().BeginSourceFile(
      CI.getCompilerInstance().getLangOpts(),
      &CI.getCompilerInstance().getPreprocessor());
  MultiplexConsumer Consumers(std::move(ASTConsumers));
  Consumers.Initialize(CI.getASTContext());

  if (llvm::Error PE = ParseSource(Path, CI.getCompilerInstance(), Consumers))
    return std::move(PE);
  CI.getDiagnosticClient().EndSourceFile();
  if (ShouldDumpIR)
    CG.GetModule()->print(llvm::outs(), nullptr);
  if (CI.getDiagnosticClient().getNumErrors())
    return llvm::make_error<llvm::StringError>(
        "Errors occurred while parsing the expression.", std::error_code());
  return std::move(CI);
}

void Forget(CIAndOrigins &CI, llvm::MutableArrayRef<CIAndOrigins> Imports) {
  llvm::SmallVector<ExternalASTMerger::ImporterSource, 3> Sources;
  for (CIAndOrigins &Import : Imports)
    Sources.push_back({Import.getASTContext(), Import.getFileManager(),
                       Import.getOriginMap()});
  ExternalASTSource *Source = CI.CI->getASTContext().getExternalSource();
  auto *Merger = static_cast<ExternalASTMerger *>(Source);
  Merger->RemoveSources(Sources);
}

} // end namespace

int main(int argc, const char **argv) {
  const bool DisableCrashReporting = true;
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0], DisableCrashReporting);
  llvm::cl::ParseCommandLineOptions(argc, argv);
  std::vector<CIAndOrigins> ImportCIs;
  for (auto I : Imports) {
    llvm::Expected<CIAndOrigins> ImportCI = Parse(I, {}, false, false);
    if (auto E = ImportCI.takeError()) {
      llvm::errs() << "error: " << llvm::toString(std::move(E)) << "\n";
      exit(-1);
    }
    ImportCIs.push_back(std::move(*ImportCI));
  }
  std::vector<CIAndOrigins> IndirectCIs;
  if (!Direct || UseOrigins) {
    for (auto &ImportCI : ImportCIs) {
      CIAndOrigins IndirectCI = BuildIndirect(ImportCI);
      IndirectCIs.push_back(std::move(IndirectCI));
    }
  }
  if (UseOrigins)
    for (auto &ImportCI : ImportCIs)
      IndirectCIs.push_back(std::move(ImportCI));
  llvm::Expected<CIAndOrigins> ExpressionCI =
      Parse(Expression, (Direct && !UseOrigins) ? ImportCIs : IndirectCIs,
            DumpAST, DumpIR);
  if (auto E = ExpressionCI.takeError()) {
    llvm::errs() << "error: " << llvm::toString(std::move(E)) << "\n";
    exit(-1);
  }
  Forget(*ExpressionCI, (Direct && !UseOrigins) ? ImportCIs : IndirectCIs);
  return 0;
}
