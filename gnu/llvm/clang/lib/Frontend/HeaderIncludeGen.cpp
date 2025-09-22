//===-- HeaderIncludeGen.cpp - Generate Header Includes -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/Utils.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;

namespace {
class HeaderIncludesCallback : public PPCallbacks {
  SourceManager &SM;
  raw_ostream *OutputFile;
  const DependencyOutputOptions &DepOpts;
  unsigned CurrentIncludeDepth;
  bool HasProcessedPredefines;
  bool OwnsOutputFile;
  bool ShowAllHeaders;
  bool ShowDepth;
  bool MSStyle;

public:
  HeaderIncludesCallback(const Preprocessor *PP, bool ShowAllHeaders_,
                         raw_ostream *OutputFile_,
                         const DependencyOutputOptions &DepOpts,
                         bool OwnsOutputFile_, bool ShowDepth_, bool MSStyle_)
      : SM(PP->getSourceManager()), OutputFile(OutputFile_), DepOpts(DepOpts),
        CurrentIncludeDepth(0), HasProcessedPredefines(false),
        OwnsOutputFile(OwnsOutputFile_), ShowAllHeaders(ShowAllHeaders_),
        ShowDepth(ShowDepth_), MSStyle(MSStyle_) {}

  ~HeaderIncludesCallback() override {
    if (OwnsOutputFile)
      delete OutputFile;
  }

  HeaderIncludesCallback(const HeaderIncludesCallback &) = delete;
  HeaderIncludesCallback &operator=(const HeaderIncludesCallback &) = delete;

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;

  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override;

private:
  bool ShouldShowHeader(SrcMgr::CharacteristicKind HeaderType) {
    if (!DepOpts.IncludeSystemHeaders && isSystem(HeaderType))
      return false;

    // Show the current header if we are (a) past the predefines, or (b) showing
    // all headers and in the predefines at a depth past the initial file and
    // command line buffers.
    return (HasProcessedPredefines ||
            (ShowAllHeaders && CurrentIncludeDepth > 2));
  }
};

/// A callback for emitting header usage information to a file in JSON. Each
/// line in the file is a JSON object that includes the source file name and
/// the list of headers directly or indirectly included from it. For example:
///
/// {"source":"/tmp/foo.c",
///  "includes":["/usr/include/stdio.h", "/usr/include/stdlib.h"]}
///
/// To reduce the amount of data written to the file, we only record system
/// headers that are directly included from a file that isn't in the system
/// directory.
class HeaderIncludesJSONCallback : public PPCallbacks {
  SourceManager &SM;
  raw_ostream *OutputFile;
  bool OwnsOutputFile;
  SmallVector<std::string, 16> IncludedHeaders;

public:
  HeaderIncludesJSONCallback(const Preprocessor *PP, raw_ostream *OutputFile_,
                             bool OwnsOutputFile_)
      : SM(PP->getSourceManager()), OutputFile(OutputFile_),
        OwnsOutputFile(OwnsOutputFile_) {}

  ~HeaderIncludesJSONCallback() override {
    if (OwnsOutputFile)
      delete OutputFile;
  }

  HeaderIncludesJSONCallback(const HeaderIncludesJSONCallback &) = delete;
  HeaderIncludesJSONCallback &
  operator=(const HeaderIncludesJSONCallback &) = delete;

  void EndOfMainFile() override;

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;

  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override;
};
}

static void PrintHeaderInfo(raw_ostream *OutputFile, StringRef Filename,
                            bool ShowDepth, unsigned CurrentIncludeDepth,
                            bool MSStyle) {
  // Write to a temporary string to avoid unnecessary flushing on errs().
  SmallString<512> Pathname(Filename);
  if (!MSStyle)
    Lexer::Stringify(Pathname);

  SmallString<256> Msg;
  if (MSStyle)
    Msg += "Note: including file:";

  if (ShowDepth) {
    // The main source file is at depth 1, so skip one dot.
    for (unsigned i = 1; i != CurrentIncludeDepth; ++i)
      Msg += MSStyle ? ' ' : '.';

    if (!MSStyle)
      Msg += ' ';
  }
  Msg += Pathname;
  Msg += '\n';

  *OutputFile << Msg;
  OutputFile->flush();
}

void clang::AttachHeaderIncludeGen(Preprocessor &PP,
                                   const DependencyOutputOptions &DepOpts,
                                   bool ShowAllHeaders, StringRef OutputPath,
                                   bool ShowDepth, bool MSStyle) {
  raw_ostream *OutputFile = &llvm::errs();
  bool OwnsOutputFile = false;

  // Choose output stream, when printing in cl.exe /showIncludes style.
  if (MSStyle) {
    switch (DepOpts.ShowIncludesDest) {
    default:
      llvm_unreachable("Invalid destination for /showIncludes output!");
    case ShowIncludesDestination::Stderr:
      OutputFile = &llvm::errs();
      break;
    case ShowIncludesDestination::Stdout:
      OutputFile = &llvm::outs();
      break;
    }
  }

  // Open the output file, if used.
  if (!OutputPath.empty()) {
    std::error_code EC;
    llvm::raw_fd_ostream *OS = new llvm::raw_fd_ostream(
        OutputPath.str(), EC,
        llvm::sys::fs::OF_Append | llvm::sys::fs::OF_TextWithCRLF);
    if (EC) {
      PP.getDiagnostics().Report(clang::diag::warn_fe_cc_print_header_failure)
          << EC.message();
      delete OS;
    } else {
      OS->SetUnbuffered();
      OutputFile = OS;
      OwnsOutputFile = true;
    }
  }

  switch (DepOpts.HeaderIncludeFormat) {
  case HIFMT_None:
    llvm_unreachable("unexpected header format kind");
  case HIFMT_Textual: {
    assert(DepOpts.HeaderIncludeFiltering == HIFIL_None &&
           "header filtering is currently always disabled when output format is"
           "textual");
    // Print header info for extra headers, pretending they were discovered by
    // the regular preprocessor. The primary use case is to support proper
    // generation of Make / Ninja file dependencies for implicit includes, such
    // as sanitizer ignorelists. It's only important for cl.exe compatibility,
    // the GNU way to generate rules is -M / -MM / -MD / -MMD.
    for (const auto &Header : DepOpts.ExtraDeps)
      PrintHeaderInfo(OutputFile, Header.first, ShowDepth, 2, MSStyle);
    PP.addPPCallbacks(std::make_unique<HeaderIncludesCallback>(
        &PP, ShowAllHeaders, OutputFile, DepOpts, OwnsOutputFile, ShowDepth,
        MSStyle));
    break;
  }
  case HIFMT_JSON: {
    assert(DepOpts.HeaderIncludeFiltering == HIFIL_Only_Direct_System &&
           "only-direct-system is the only option for filtering");
    PP.addPPCallbacks(std::make_unique<HeaderIncludesJSONCallback>(
        &PP, OutputFile, OwnsOutputFile));
    break;
  }
  }
}

void HeaderIncludesCallback::FileChanged(SourceLocation Loc,
                                         FileChangeReason Reason,
                                         SrcMgr::CharacteristicKind NewFileType,
                                         FileID PrevFID) {
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  PresumedLoc UserLoc = SM.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  // Adjust the current include depth.
  if (Reason == PPCallbacks::EnterFile) {
    ++CurrentIncludeDepth;
  } else if (Reason == PPCallbacks::ExitFile) {
    if (CurrentIncludeDepth)
      --CurrentIncludeDepth;

    // We track when we are done with the predefines by watching for the first
    // place where we drop back to a nesting depth of 1.
    if (CurrentIncludeDepth == 1 && !HasProcessedPredefines)
      HasProcessedPredefines = true;

    return;
  } else {
    return;
  }

  if (!ShouldShowHeader(NewFileType))
    return;

  unsigned IncludeDepth = CurrentIncludeDepth;
  if (!HasProcessedPredefines)
    --IncludeDepth; // Ignore indent from <built-in>.

  // FIXME: Identify headers in a more robust way than comparing their name to
  // "<command line>" and "<built-in>" in a bunch of places.
  if (Reason == PPCallbacks::EnterFile &&
      UserLoc.getFilename() != StringRef("<command line>")) {
    PrintHeaderInfo(OutputFile, UserLoc.getFilename(), ShowDepth, IncludeDepth,
                    MSStyle);
  }
}

void HeaderIncludesCallback::FileSkipped(const FileEntryRef &SkippedFile, const
                                         Token &FilenameTok,
                                         SrcMgr::CharacteristicKind FileType) {
  if (!DepOpts.ShowSkippedHeaderIncludes)
    return;

  if (!ShouldShowHeader(FileType))
    return;

  PrintHeaderInfo(OutputFile, SkippedFile.getName(), ShowDepth,
                  CurrentIncludeDepth + 1, MSStyle);
}

void HeaderIncludesJSONCallback::EndOfMainFile() {
  OptionalFileEntryRef FE = SM.getFileEntryRefForID(SM.getMainFileID());
  SmallString<256> MainFile(FE->getName());
  SM.getFileManager().makeAbsolutePath(MainFile);

  std::string Str;
  llvm::raw_string_ostream OS(Str);
  llvm::json::OStream JOS(OS);
  JOS.object([&] {
    JOS.attribute("source", MainFile.c_str());
    JOS.attributeArray("includes", [&] {
      llvm::StringSet<> SeenHeaders;
      for (const std::string &H : IncludedHeaders)
        if (SeenHeaders.insert(H).second)
          JOS.value(H);
    });
  });
  OS << "\n";

  if (OutputFile->get_kind() == raw_ostream::OStreamKind::OK_FDStream) {
    llvm::raw_fd_ostream *FDS = static_cast<llvm::raw_fd_ostream *>(OutputFile);
    if (auto L = FDS->lock())
      *OutputFile << Str;
  } else
    *OutputFile << Str;
}

/// Determine whether the header file should be recorded. The header file should
/// be recorded only if the header file is a system header and the current file
/// isn't a system header.
static bool shouldRecordNewFile(SrcMgr::CharacteristicKind NewFileType,
                                SourceLocation PrevLoc, SourceManager &SM) {
  return SrcMgr::isSystem(NewFileType) && !SM.isInSystemHeader(PrevLoc);
}

void HeaderIncludesJSONCallback::FileChanged(
    SourceLocation Loc, FileChangeReason Reason,
    SrcMgr::CharacteristicKind NewFileType, FileID PrevFID) {
  if (PrevFID.isInvalid() ||
      !shouldRecordNewFile(NewFileType, SM.getLocForStartOfFile(PrevFID), SM))
    return;

  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  PresumedLoc UserLoc = SM.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  if (Reason == PPCallbacks::EnterFile &&
      UserLoc.getFilename() != StringRef("<command line>"))
    IncludedHeaders.push_back(UserLoc.getFilename());
}

void HeaderIncludesJSONCallback::FileSkipped(
    const FileEntryRef &SkippedFile, const Token &FilenameTok,
    SrcMgr::CharacteristicKind FileType) {
  if (!shouldRecordNewFile(FileType, FilenameTok.getLocation(), SM))
    return;

  IncludedHeaders.push_back(SkippedFile.getName().str());
}
