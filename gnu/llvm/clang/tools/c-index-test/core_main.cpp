//===-- core_main.cpp - Core Index Tool testbed ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Mangle.h"
#include "clang/Basic/LangOptions.h"
#include "clang/CodeGen/ObjectFilePCHContainerOperations.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/Utils.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::index;
using namespace llvm;

extern "C" int indextest_core_main(int argc, const char **argv);
extern "C" int indextest_perform_shell_execution(const char *command_line);

namespace {

enum class ActionType {
  None,
  PrintSourceSymbols,
};

namespace options {

static cl::OptionCategory IndexTestCoreCategory("index-test-core options");

static cl::opt<ActionType>
Action(cl::desc("Action:"), cl::init(ActionType::None),
       cl::values(
          clEnumValN(ActionType::PrintSourceSymbols,
                     "print-source-symbols", "Print symbols from source")),
       cl::cat(IndexTestCoreCategory));

static cl::extrahelp MoreHelp(
  "\nAdd \"-- <compiler arguments>\" at the end to setup the compiler "
  "invocation\n"
);

static cl::opt<bool>
DumpModuleImports("dump-imported-module-files",
               cl::desc("Print symbols and input files from imported modules"));

static cl::opt<bool>
IncludeLocals("include-locals", cl::desc("Print local symbols"));

static cl::opt<bool> IgnoreMacros("ignore-macros",
                                  cl::desc("Skip indexing macros"));

static cl::opt<std::string>
ModuleFilePath("module-file",
               cl::desc("Path to module file to print symbols from"));
static cl::opt<std::string>
  ModuleFormat("fmodule-format", cl::init("raw"),
        cl::desc("Container format for clang modules and PCH, 'raw' or 'obj'"));

}
} // anonymous namespace

static void printSymbolInfo(SymbolInfo SymInfo, raw_ostream &OS);
static void printSymbolNameAndUSR(const Decl *D, ASTContext &Ctx,
                                  raw_ostream &OS);
static void printSymbolNameAndUSR(const clang::Module *Mod, raw_ostream &OS);

namespace {

class PrintIndexDataConsumer : public IndexDataConsumer {
  raw_ostream &OS;
  std::unique_ptr<ASTNameGenerator> ASTNameGen;
  std::shared_ptr<Preprocessor> PP;

public:
  PrintIndexDataConsumer(raw_ostream &OS) : OS(OS) {
  }

  void initialize(ASTContext &Ctx) override {
    ASTNameGen.reset(new ASTNameGenerator(Ctx));
  }

  void setPreprocessor(std::shared_ptr<Preprocessor> PP) override {
    this->PP = std::move(PP);
  }

  bool handleDeclOccurrence(const Decl *D, SymbolRoleSet Roles,
                            ArrayRef<SymbolRelation> Relations,
                            SourceLocation Loc, ASTNodeInfo ASTNode) override {
    ASTContext &Ctx = D->getASTContext();
    SourceManager &SM = Ctx.getSourceManager();

    Loc = SM.getFileLoc(Loc);
    FileID FID = SM.getFileID(Loc);
    unsigned Line = SM.getLineNumber(FID, SM.getFileOffset(Loc));
    unsigned Col = SM.getColumnNumber(FID, SM.getFileOffset(Loc));
    OS << Line << ':' << Col << " | ";

    printSymbolInfo(getSymbolInfo(D), OS);
    OS << " | ";

    printSymbolNameAndUSR(D, Ctx, OS);
    OS << " | ";

    if (ASTNameGen->writeName(D, OS))
      OS << "<no-cgname>";
    OS << " | ";

    printSymbolRoles(Roles, OS);
    OS << " | ";

    OS << "rel: " << Relations.size() << '\n';

    for (auto &SymRel : Relations) {
      OS << '\t';
      printSymbolRoles(SymRel.Roles, OS);
      OS << " | ";
      printSymbolNameAndUSR(SymRel.RelatedSymbol, Ctx, OS);
      OS << '\n';
    }

    return true;
  }

  bool handleModuleOccurrence(const ImportDecl *ImportD,
                              const clang::Module *Mod, SymbolRoleSet Roles,
                              SourceLocation Loc) override {
    ASTContext &Ctx = ImportD->getASTContext();
    SourceManager &SM = Ctx.getSourceManager();

    Loc = SM.getFileLoc(Loc);
    FileID FID = SM.getFileID(Loc);
    unsigned Line = SM.getLineNumber(FID, SM.getFileOffset(Loc));
    unsigned Col = SM.getColumnNumber(FID, SM.getFileOffset(Loc));
    OS << Line << ':' << Col << " | ";

    printSymbolInfo(getSymbolInfo(ImportD), OS);
    OS << " | ";

    printSymbolNameAndUSR(Mod, OS);
    OS << " | ";

    printSymbolRoles(Roles, OS);
    OS << " |\n";

    return true;
  }

  bool handleMacroOccurrence(const IdentifierInfo *Name, const MacroInfo *MI,
                             SymbolRoleSet Roles, SourceLocation Loc) override {
    assert(PP);
    SourceManager &SM = PP->getSourceManager();

    Loc = SM.getFileLoc(Loc);
    FileID FID = SM.getFileID(Loc);
    unsigned Line = SM.getLineNumber(FID, SM.getFileOffset(Loc));
    unsigned Col = SM.getColumnNumber(FID, SM.getFileOffset(Loc));
    OS << Line << ':' << Col << " | ";

    printSymbolInfo(getSymbolInfoForMacro(*MI), OS);
    OS << " | ";

    OS << Name->getName();
    OS << " | ";

    SmallString<256> USRBuf;
    if (generateUSRForMacro(Name->getName(), MI->getDefinitionLoc(), SM,
                            USRBuf)) {
      OS << "<no-usr>";
    } else {
      OS << USRBuf;
    }
    OS << " | ";

    printSymbolRoles(Roles, OS);
    OS << " |\n";
    return true;
  }
};

} // anonymous namespace

//===----------------------------------------------------------------------===//
// Print Source Symbols
//===----------------------------------------------------------------------===//

static void dumpModuleFileInputs(serialization::ModuleFile &Mod,
                                 ASTReader &Reader,
                                 raw_ostream &OS) {
  OS << "---- Module Inputs ----\n";
  Reader.visitInputFiles(Mod, /*IncludeSystem=*/true, /*Complain=*/false,
                        [&](const serialization::InputFile &IF, bool isSystem) {
    OS << (isSystem ? "system" : "user") << " | ";
    OS << IF.getFile()->getName() << '\n';
  });
}

static bool printSourceSymbols(const char *Executable,
                               ArrayRef<const char *> Args,
                               bool dumpModuleImports, bool indexLocals,
                               bool ignoreMacros) {
  SmallVector<const char *, 4> ArgsWithProgName;
  ArgsWithProgName.push_back(Executable);
  ArgsWithProgName.append(Args.begin(), Args.end());
  IntrusiveRefCntPtr<DiagnosticsEngine>
    Diags(CompilerInstance::createDiagnostics(new DiagnosticOptions));
  CreateInvocationOptions CIOpts;
  CIOpts.Diags = Diags;
  CIOpts.ProbePrecompiled = true; // FIXME: historical default. Needed?
  auto CInvok = createInvocation(ArgsWithProgName, std::move(CIOpts));
  if (!CInvok)
    return true;

  raw_ostream &OS = outs();
  auto DataConsumer = std::make_shared<PrintIndexDataConsumer>(OS);
  IndexingOptions IndexOpts;
  IndexOpts.IndexFunctionLocals = indexLocals;
  IndexOpts.IndexMacros = !ignoreMacros;
  IndexOpts.IndexMacrosInPreprocessor = !ignoreMacros;
  std::unique_ptr<FrontendAction> IndexAction =
      createIndexingAction(DataConsumer, IndexOpts);

  auto PCHContainerOps = std::make_shared<PCHContainerOperations>();
  std::unique_ptr<ASTUnit> Unit(ASTUnit::LoadFromCompilerInvocationAction(
      std::move(CInvok), PCHContainerOps, Diags, IndexAction.get()));

  if (!Unit)
    return true;

  if (dumpModuleImports) {
    if (auto Reader = Unit->getASTReader()) {
      Reader->getModuleManager().visit([&](serialization::ModuleFile &Mod) -> bool {
        OS << "==== Module " << Mod.ModuleName << " ====\n";
        indexModuleFile(Mod, *Reader, *DataConsumer, IndexOpts);
        dumpModuleFileInputs(Mod, *Reader, OS);
        return true; // skip module dependencies.
      });
    }
  }

  return false;
}

static bool printSourceSymbolsFromModule(StringRef modulePath,
                                         StringRef format) {
  FileSystemOptions FileSystemOpts;
  auto pchContOps = std::make_shared<PCHContainerOperations>();
  // Register the support for object-file-wrapped Clang modules.
  pchContOps->registerReader(std::make_unique<ObjectFilePCHContainerReader>());
  auto pchRdr = pchContOps->getReaderOrNull(format);
  if (!pchRdr) {
    errs() << "unknown module format: " << format << '\n';
    return true;
  }

  auto HSOpts = std::make_shared<HeaderSearchOptions>();

  IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
      CompilerInstance::createDiagnostics(new DiagnosticOptions());
  std::unique_ptr<ASTUnit> AU = ASTUnit::LoadFromASTFile(
      std::string(modulePath), *pchRdr, ASTUnit::LoadASTOnly, Diags,
      FileSystemOpts, HSOpts, /*LangOpts=*/nullptr,
      /*OnlyLocalDecls=*/true, CaptureDiagsKind::None,
      /*AllowASTWithCompilerErrors=*/true,
      /*UserFilesAreVolatile=*/false);
  if (!AU) {
    errs() << "failed to create TU for: " << modulePath << '\n';
    return true;
  }

  PrintIndexDataConsumer DataConsumer(outs());
  IndexingOptions IndexOpts;
  indexASTUnit(*AU, DataConsumer, IndexOpts);

  return false;
}

//===----------------------------------------------------------------------===//
// Helper Utils
//===----------------------------------------------------------------------===//

static void printSymbolInfo(SymbolInfo SymInfo, raw_ostream &OS) {
  OS << getSymbolKindString(SymInfo.Kind);
  if (SymInfo.SubKind != SymbolSubKind::None)
    OS << '/' << getSymbolSubKindString(SymInfo.SubKind);
  if (SymInfo.Properties) {
    OS << '(';
    printSymbolProperties(SymInfo.Properties, OS);
    OS << ')';
  }
  OS << '/' << getSymbolLanguageString(SymInfo.Lang);
}

static void printSymbolNameAndUSR(const Decl *D, ASTContext &Ctx,
                                  raw_ostream &OS) {
  if (printSymbolName(D, Ctx.getLangOpts(), OS)) {
    OS << "<no-name>";
  }
  OS << " | ";

  SmallString<256> USRBuf;
  if (generateUSRForDecl(D, USRBuf)) {
    OS << "<no-usr>";
  } else {
    OS << USRBuf;
  }
}

static void printSymbolNameAndUSR(const clang::Module *Mod, raw_ostream &OS) {
  assert(Mod);
  OS << Mod->getFullModuleName() << " | ";
  generateFullUSRForModule(Mod, OS);
}

//===----------------------------------------------------------------------===//
// Command line processing.
//===----------------------------------------------------------------------===//

int indextest_core_main(int argc, const char **argv) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  void *MainAddr = (void*) (intptr_t) indextest_core_main;
  std::string Executable = llvm::sys::fs::getMainExecutable(argv[0], MainAddr);

  assert(argv[1] == StringRef("core"));
  ++argv;
  --argc;

  std::vector<const char *> CompArgs;
  const char **DoubleDash = std::find(argv, argv + argc, StringRef("--"));
  if (DoubleDash != argv + argc) {
    CompArgs = std::vector<const char *>(DoubleDash + 1, argv + argc);
    argc = DoubleDash - argv;
  }

  cl::HideUnrelatedOptions(options::IndexTestCoreCategory);
  cl::ParseCommandLineOptions(argc, argv, "index-test-core");

  if (options::Action == ActionType::None) {
    errs() << "error: action required; pass '-help' for options\n";
    return 1;
  }

  if (options::Action == ActionType::PrintSourceSymbols) {
    if (!options::ModuleFilePath.empty()) {
      return printSourceSymbolsFromModule(options::ModuleFilePath,
                                          options::ModuleFormat);
    }
    if (CompArgs.empty()) {
      errs() << "error: missing compiler args; pass '-- <compiler arguments>'\n";
      return 1;
    }
    return printSourceSymbols(Executable.c_str(), CompArgs,
                              options::DumpModuleImports,
                              options::IncludeLocals, options::IgnoreMacros);
  }

  return 0;
}

//===----------------------------------------------------------------------===//
// Utility functions
//===----------------------------------------------------------------------===//

int indextest_perform_shell_execution(const char *command_line) {
  BumpPtrAllocator Alloc;
  llvm::StringSaver Saver(Alloc);
  SmallVector<const char *, 4> Args;
  llvm::cl::TokenizeGNUCommandLine(command_line, Saver, Args);
  auto Program = llvm::sys::findProgramByName(Args[0]);
  if (std::error_code ec = Program.getError()) {
    llvm::errs() << "command not found: " << Args[0] << "\n";
    return ec.value();
  }
  SmallVector<StringRef, 8> execArgs(Args.begin(), Args.end());
  return llvm::sys::ExecuteAndWait(*Program, execArgs);
}
