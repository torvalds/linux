//===--- tools/extra/clang-rename/ClangRename.cpp - Clang rename tool -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a clang-rename tool that automatically finds and
/// renames symbols in C++ code.
///
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Refactoring/Rename/RenamingAction.h"
#include "clang/Tooling/Refactoring/Rename/USRFindingAction.h"
#include "clang/Tooling/ReplacementsYaml.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <system_error>

using namespace llvm;
using namespace clang;

/// An oldname -> newname rename.
struct RenameAllInfo {
  unsigned Offset = 0;
  std::string QualifiedName;
  std::string NewName;
};

LLVM_YAML_IS_SEQUENCE_VECTOR(RenameAllInfo)

namespace llvm {
namespace yaml {

/// Specialized MappingTraits to describe how a RenameAllInfo is
/// (de)serialized.
template <> struct MappingTraits<RenameAllInfo> {
  static void mapping(IO &IO, RenameAllInfo &Info) {
    IO.mapOptional("Offset", Info.Offset);
    IO.mapOptional("QualifiedName", Info.QualifiedName);
    IO.mapRequired("NewName", Info.NewName);
  }
};

} // end namespace yaml
} // end namespace llvm

static cl::OptionCategory ClangRenameOptions("clang-rename common options");

static cl::list<unsigned> SymbolOffsets(
    "offset",
    cl::desc("Locates the symbol by offset as opposed to <line>:<column>."),
    cl::cat(ClangRenameOptions));
static cl::opt<bool> Inplace("i", cl::desc("Overwrite edited <file>s."),
                             cl::cat(ClangRenameOptions));
static cl::list<std::string>
    QualifiedNames("qualified-name",
                   cl::desc("The fully qualified name of the symbol."),
                   cl::cat(ClangRenameOptions));

static cl::list<std::string>
    NewNames("new-name", cl::desc("The new name to change the symbol to."),
             cl::cat(ClangRenameOptions));
static cl::opt<bool> PrintName(
    "pn",
    cl::desc("Print the found symbol's name prior to renaming to stderr."),
    cl::cat(ClangRenameOptions));
static cl::opt<bool> PrintLocations(
    "pl", cl::desc("Print the locations affected by renaming to stderr."),
    cl::cat(ClangRenameOptions));
static cl::opt<std::string>
    ExportFixes("export-fixes",
                cl::desc("YAML file to store suggested fixes in."),
                cl::value_desc("filename"), cl::cat(ClangRenameOptions));
static cl::opt<std::string>
    Input("input", cl::desc("YAML file to load oldname-newname pairs from."),
          cl::Optional, cl::cat(ClangRenameOptions));
static cl::opt<bool> Force("force",
                           cl::desc("Ignore nonexistent qualified names."),
                           cl::cat(ClangRenameOptions));

int main(int argc, const char **argv) {
  auto ExpectedParser =
      tooling::CommonOptionsParser::create(argc, argv, ClangRenameOptions);
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  tooling::CommonOptionsParser &OP = ExpectedParser.get();

  if (!Input.empty()) {
    // Populate QualifiedNames and NewNames from a YAML file.
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
        llvm::MemoryBuffer::getFile(Input);
    if (!Buffer) {
      errs() << "clang-rename: failed to read " << Input << ": "
             << Buffer.getError().message() << "\n";
      return 1;
    }

    std::vector<RenameAllInfo> Infos;
    llvm::yaml::Input YAML(Buffer.get()->getBuffer());
    YAML >> Infos;
    for (const auto &Info : Infos) {
      if (!Info.QualifiedName.empty())
        QualifiedNames.push_back(Info.QualifiedName);
      else
        SymbolOffsets.push_back(Info.Offset);
      NewNames.push_back(Info.NewName);
    }
  }

  // Check the arguments for correctness.
  if (NewNames.empty()) {
    errs() << "clang-rename: -new-name must be specified.\n\n";
    return 1;
  }

  if (SymbolOffsets.empty() == QualifiedNames.empty()) {
    errs() << "clang-rename: -offset and -qualified-name can't be present at "
              "the same time.\n";
    return 1;
  }

  // Check if NewNames is a valid identifier in C++17.
  LangOptions Options;
  Options.CPlusPlus = true;
  Options.CPlusPlus17 = true;
  IdentifierTable Table(Options);
  for (const auto &NewName : NewNames) {
    auto NewNameTokKind = Table.get(NewName).getTokenID();
    if (!tok::isAnyIdentifier(NewNameTokKind)) {
      errs() << "ERROR: new name is not a valid identifier in C++17.\n\n";
      return 1;
    }
  }

  if (SymbolOffsets.size() + QualifiedNames.size() != NewNames.size()) {
    errs() << "clang-rename: number of symbol offsets(" << SymbolOffsets.size()
           << ") + number of qualified names (" << QualifiedNames.size()
           << ") must be equal to number of new names(" << NewNames.size()
           << ").\n\n";
    cl::PrintHelpMessage();
    return 1;
  }

  auto Files = OP.getSourcePathList();
  tooling::RefactoringTool Tool(OP.getCompilations(), Files);
  tooling::USRFindingAction FindingAction(SymbolOffsets, QualifiedNames, Force);
  Tool.run(tooling::newFrontendActionFactory(&FindingAction).get());
  const std::vector<std::vector<std::string>> &USRList =
      FindingAction.getUSRList();
  const std::vector<std::string> &PrevNames = FindingAction.getUSRSpellings();
  if (PrintName) {
    for (const auto &PrevName : PrevNames) {
      outs() << "clang-rename found name: " << PrevName << '\n';
    }
  }

  if (FindingAction.errorOccurred()) {
    // Diagnostics are already issued at this point.
    return 1;
  }

  // Perform the renaming.
  tooling::RenamingAction RenameAction(NewNames, PrevNames, USRList,
                                       Tool.getReplacements(), PrintLocations);
  std::unique_ptr<tooling::FrontendActionFactory> Factory =
      tooling::newFrontendActionFactory(&RenameAction);
  int ExitCode;

  if (Inplace) {
    ExitCode = Tool.runAndSave(Factory.get());
  } else {
    ExitCode = Tool.run(Factory.get());

    if (!ExportFixes.empty()) {
      std::error_code EC;
      llvm::raw_fd_ostream OS(ExportFixes, EC, llvm::sys::fs::OF_None);
      if (EC) {
        llvm::errs() << "Error opening output file: " << EC.message() << '\n';
        return 1;
      }

      // Export replacements.
      tooling::TranslationUnitReplacements TUR;
      const auto &FileToReplacements = Tool.getReplacements();
      for (const auto &Entry : FileToReplacements)
        TUR.Replacements.insert(TUR.Replacements.end(), Entry.second.begin(),
                                Entry.second.end());

      yaml::Output YAML(OS);
      YAML << TUR;
      OS.close();
      return 0;
    }

    // Write every file to stdout. Right now we just barf the files without any
    // indication of which files start where, other than that we print the files
    // in the same order we see them.
    LangOptions DefaultLangOptions;
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
    TextDiagnosticPrinter DiagnosticPrinter(errs(), &*DiagOpts);
    DiagnosticsEngine Diagnostics(
        IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs()), &*DiagOpts,
        &DiagnosticPrinter, false);
    auto &FileMgr = Tool.getFiles();
    SourceManager Sources(Diagnostics, FileMgr);
    Rewriter Rewrite(Sources, DefaultLangOptions);

    Tool.applyAllReplacements(Rewrite);
    for (const auto &File : Files) {
      auto Entry = FileMgr.getOptionalFileRef(File);
      if (!Entry) {
        errs() << "clang-rename: " << File << " does not exist.\n";
        return 1;
      }
      const auto ID = Sources.getOrCreateFileID(*Entry, SrcMgr::C_User);
      Rewrite.getEditBuffer(ID).write(outs());
    }
  }

  return ExitCode;
}
