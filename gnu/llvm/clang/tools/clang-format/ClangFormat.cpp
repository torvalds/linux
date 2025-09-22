//===-- clang-format/ClangFormat.cpp - Clang format tool ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a clang-format tool that automatically formats
/// (fragments of) C++ code.
///
//===----------------------------------------------------------------------===//

#include "../../lib/Format/MatchFilePath.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Format/Format.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Process.h"
#include <fstream>

using namespace llvm;
using clang::tooling::Replacements;

static cl::opt<bool> Help("h", cl::desc("Alias for -help"), cl::Hidden);

// Mark all our options with this category, everything else (except for -version
// and -help) will be hidden.
static cl::OptionCategory ClangFormatCategory("Clang-format options");

static cl::list<unsigned>
    Offsets("offset",
            cl::desc("Format a range starting at this byte offset.\n"
                     "Multiple ranges can be formatted by specifying\n"
                     "several -offset and -length pairs.\n"
                     "Can only be used with one input file."),
            cl::cat(ClangFormatCategory));
static cl::list<unsigned>
    Lengths("length",
            cl::desc("Format a range of this length (in bytes).\n"
                     "Multiple ranges can be formatted by specifying\n"
                     "several -offset and -length pairs.\n"
                     "When only a single -offset is specified without\n"
                     "-length, clang-format will format up to the end\n"
                     "of the file.\n"
                     "Can only be used with one input file."),
            cl::cat(ClangFormatCategory));
static cl::list<std::string>
    LineRanges("lines",
               cl::desc("<start line>:<end line> - format a range of\n"
                        "lines (both 1-based).\n"
                        "Multiple ranges can be formatted by specifying\n"
                        "several -lines arguments.\n"
                        "Can't be used with -offset and -length.\n"
                        "Can only be used with one input file."),
               cl::cat(ClangFormatCategory));
static cl::opt<std::string>
    Style("style", cl::desc(clang::format::StyleOptionHelpDescription),
          cl::init(clang::format::DefaultFormatStyle),
          cl::cat(ClangFormatCategory));
static cl::opt<std::string>
    FallbackStyle("fallback-style",
                  cl::desc("The name of the predefined style used as a\n"
                           "fallback in case clang-format is invoked with\n"
                           "-style=file, but can not find the .clang-format\n"
                           "file to use. Defaults to 'LLVM'.\n"
                           "Use -fallback-style=none to skip formatting."),
                  cl::init(clang::format::DefaultFallbackStyle),
                  cl::cat(ClangFormatCategory));

static cl::opt<std::string> AssumeFileName(
    "assume-filename",
    cl::desc("Set filename used to determine the language and to find\n"
             ".clang-format file.\n"
             "Only used when reading from stdin.\n"
             "If this is not passed, the .clang-format file is searched\n"
             "relative to the current working directory when reading stdin.\n"
             "Unrecognized filenames are treated as C++.\n"
             "supported:\n"
             "  CSharp: .cs\n"
             "  Java: .java\n"
             "  JavaScript: .mjs .js .ts\n"
             "  Json: .json\n"
             "  Objective-C: .m .mm\n"
             "  Proto: .proto .protodevel\n"
             "  TableGen: .td\n"
             "  TextProto: .txtpb .textpb .pb.txt .textproto .asciipb\n"
             "  Verilog: .sv .svh .v .vh"),
    cl::init("<stdin>"), cl::cat(ClangFormatCategory));

static cl::opt<bool> Inplace("i",
                             cl::desc("Inplace edit <file>s, if specified."),
                             cl::cat(ClangFormatCategory));

static cl::opt<bool> OutputXML("output-replacements-xml",
                               cl::desc("Output replacements as XML."),
                               cl::cat(ClangFormatCategory));
static cl::opt<bool>
    DumpConfig("dump-config",
               cl::desc("Dump configuration options to stdout and exit.\n"
                        "Can be used with -style option."),
               cl::cat(ClangFormatCategory));
static cl::opt<unsigned>
    Cursor("cursor",
           cl::desc("The position of the cursor when invoking\n"
                    "clang-format from an editor integration"),
           cl::init(0), cl::cat(ClangFormatCategory));

static cl::opt<bool>
    SortIncludes("sort-includes",
                 cl::desc("If set, overrides the include sorting behavior\n"
                          "determined by the SortIncludes style flag"),
                 cl::cat(ClangFormatCategory));

static cl::opt<std::string> QualifierAlignment(
    "qualifier-alignment",
    cl::desc("If set, overrides the qualifier alignment style\n"
             "determined by the QualifierAlignment style flag"),
    cl::init(""), cl::cat(ClangFormatCategory));

static cl::opt<std::string> Files(
    "files",
    cl::desc("A file containing a list of files to process, one per line."),
    cl::value_desc("filename"), cl::init(""), cl::cat(ClangFormatCategory));

static cl::opt<bool>
    Verbose("verbose", cl::desc("If set, shows the list of processed files"),
            cl::cat(ClangFormatCategory));

// Use --dry-run to match other LLVM tools when you mean do it but don't
// actually do it
static cl::opt<bool>
    DryRun("dry-run",
           cl::desc("If set, do not actually make the formatting changes"),
           cl::cat(ClangFormatCategory));

// Use -n as a common command as an alias for --dry-run. (git and make use -n)
static cl::alias DryRunShort("n", cl::desc("Alias for --dry-run"),
                             cl::cat(ClangFormatCategory), cl::aliasopt(DryRun),
                             cl::NotHidden);

// Emulate being able to turn on/off the warning.
static cl::opt<bool>
    WarnFormat("Wclang-format-violations",
               cl::desc("Warnings about individual formatting changes needed. "
                        "Used only with --dry-run or -n"),
               cl::init(true), cl::cat(ClangFormatCategory), cl::Hidden);

static cl::opt<bool>
    NoWarnFormat("Wno-clang-format-violations",
                 cl::desc("Do not warn about individual formatting changes "
                          "needed. Used only with --dry-run or -n"),
                 cl::init(false), cl::cat(ClangFormatCategory), cl::Hidden);

static cl::opt<unsigned> ErrorLimit(
    "ferror-limit",
    cl::desc("Set the maximum number of clang-format errors to emit\n"
             "before stopping (0 = no limit).\n"
             "Used only with --dry-run or -n"),
    cl::init(0), cl::cat(ClangFormatCategory));

static cl::opt<bool>
    WarningsAsErrors("Werror",
                     cl::desc("If set, changes formatting warnings to errors"),
                     cl::cat(ClangFormatCategory));

namespace {
enum class WNoError { Unknown };
}

static cl::bits<WNoError> WNoErrorList(
    "Wno-error",
    cl::desc("If set don't error out on the specified warning type."),
    cl::values(
        clEnumValN(WNoError::Unknown, "unknown",
                   "If set, unknown format options are only warned about.\n"
                   "This can be used to enable formatting, even if the\n"
                   "configuration contains unknown (newer) options.\n"
                   "Use with caution, as this might lead to dramatically\n"
                   "differing format depending on an option being\n"
                   "supported or not.")),
    cl::cat(ClangFormatCategory));

static cl::opt<bool>
    ShowColors("fcolor-diagnostics",
               cl::desc("If set, and on a color-capable terminal controls "
                        "whether or not to print diagnostics in color"),
               cl::init(true), cl::cat(ClangFormatCategory), cl::Hidden);

static cl::opt<bool>
    NoShowColors("fno-color-diagnostics",
                 cl::desc("If set, and on a color-capable terminal controls "
                          "whether or not to print diagnostics in color"),
                 cl::init(false), cl::cat(ClangFormatCategory), cl::Hidden);

static cl::list<std::string> FileNames(cl::Positional,
                                       cl::desc("[@<file>] [<file> ...]"),
                                       cl::cat(ClangFormatCategory));

static cl::opt<bool> FailOnIncompleteFormat(
    "fail-on-incomplete-format",
    cl::desc("If set, fail with exit code 1 on incomplete format."),
    cl::init(false), cl::cat(ClangFormatCategory));

static cl::opt<bool> ListIgnored("list-ignored",
                                 cl::desc("List ignored files."),
                                 cl::cat(ClangFormatCategory), cl::Hidden);

namespace clang {
namespace format {

static FileID createInMemoryFile(StringRef FileName, MemoryBufferRef Source,
                                 SourceManager &Sources, FileManager &Files,
                                 llvm::vfs::InMemoryFileSystem *MemFS) {
  MemFS->addFileNoOwn(FileName, 0, Source);
  auto File = Files.getOptionalFileRef(FileName);
  assert(File && "File not added to MemFS?");
  return Sources.createFileID(*File, SourceLocation(), SrcMgr::C_User);
}

// Parses <start line>:<end line> input to a pair of line numbers.
// Returns true on error.
static bool parseLineRange(StringRef Input, unsigned &FromLine,
                           unsigned &ToLine) {
  std::pair<StringRef, StringRef> LineRange = Input.split(':');
  return LineRange.first.getAsInteger(0, FromLine) ||
         LineRange.second.getAsInteger(0, ToLine);
}

static bool fillRanges(MemoryBuffer *Code,
                       std::vector<tooling::Range> &Ranges) {
  IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  FileManager Files(FileSystemOptions(), InMemoryFileSystem);
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
      new DiagnosticOptions);
  SourceManager Sources(Diagnostics, Files);
  FileID ID = createInMemoryFile("<irrelevant>", *Code, Sources, Files,
                                 InMemoryFileSystem.get());
  if (!LineRanges.empty()) {
    if (!Offsets.empty() || !Lengths.empty()) {
      errs() << "error: cannot use -lines with -offset/-length\n";
      return true;
    }

    for (unsigned i = 0, e = LineRanges.size(); i < e; ++i) {
      unsigned FromLine, ToLine;
      if (parseLineRange(LineRanges[i], FromLine, ToLine)) {
        errs() << "error: invalid <start line>:<end line> pair\n";
        return true;
      }
      if (FromLine < 1) {
        errs() << "error: start line should be at least 1\n";
        return true;
      }
      if (FromLine > ToLine) {
        errs() << "error: start line should not exceed end line\n";
        return true;
      }
      SourceLocation Start = Sources.translateLineCol(ID, FromLine, 1);
      SourceLocation End = Sources.translateLineCol(ID, ToLine, UINT_MAX);
      if (Start.isInvalid() || End.isInvalid())
        return true;
      unsigned Offset = Sources.getFileOffset(Start);
      unsigned Length = Sources.getFileOffset(End) - Offset;
      Ranges.push_back(tooling::Range(Offset, Length));
    }
    return false;
  }

  if (Offsets.empty())
    Offsets.push_back(0);
  if (Offsets.size() != Lengths.size() &&
      !(Offsets.size() == 1 && Lengths.empty())) {
    errs() << "error: number of -offset and -length arguments must match.\n";
    return true;
  }
  for (unsigned i = 0, e = Offsets.size(); i != e; ++i) {
    if (Offsets[i] >= Code->getBufferSize()) {
      errs() << "error: offset " << Offsets[i] << " is outside the file\n";
      return true;
    }
    SourceLocation Start =
        Sources.getLocForStartOfFile(ID).getLocWithOffset(Offsets[i]);
    SourceLocation End;
    if (i < Lengths.size()) {
      if (Offsets[i] + Lengths[i] > Code->getBufferSize()) {
        errs() << "error: invalid length " << Lengths[i]
               << ", offset + length (" << Offsets[i] + Lengths[i]
               << ") is outside the file.\n";
        return true;
      }
      End = Start.getLocWithOffset(Lengths[i]);
    } else {
      End = Sources.getLocForEndOfFile(ID);
    }
    unsigned Offset = Sources.getFileOffset(Start);
    unsigned Length = Sources.getFileOffset(End) - Offset;
    Ranges.push_back(tooling::Range(Offset, Length));
  }
  return false;
}

static void outputReplacementXML(StringRef Text) {
  // FIXME: When we sort includes, we need to make sure the stream is correct
  // utf-8.
  size_t From = 0;
  size_t Index;
  while ((Index = Text.find_first_of("\n\r<&", From)) != StringRef::npos) {
    outs() << Text.substr(From, Index - From);
    switch (Text[Index]) {
    case '\n':
      outs() << "&#10;";
      break;
    case '\r':
      outs() << "&#13;";
      break;
    case '<':
      outs() << "&lt;";
      break;
    case '&':
      outs() << "&amp;";
      break;
    default:
      llvm_unreachable("Unexpected character encountered!");
    }
    From = Index + 1;
  }
  outs() << Text.substr(From);
}

static void outputReplacementsXML(const Replacements &Replaces) {
  for (const auto &R : Replaces) {
    outs() << "<replacement "
           << "offset='" << R.getOffset() << "' "
           << "length='" << R.getLength() << "'>";
    outputReplacementXML(R.getReplacementText());
    outs() << "</replacement>\n";
  }
}

static bool
emitReplacementWarnings(const Replacements &Replaces, StringRef AssumedFileName,
                        const std::unique_ptr<llvm::MemoryBuffer> &Code) {
  if (Replaces.empty())
    return false;

  unsigned Errors = 0;
  if (WarnFormat && !NoWarnFormat) {
    SourceMgr Mgr;
    const char *StartBuf = Code->getBufferStart();

    Mgr.AddNewSourceBuffer(
        MemoryBuffer::getMemBuffer(StartBuf, AssumedFileName), SMLoc());
    for (const auto &R : Replaces) {
      SMDiagnostic Diag = Mgr.GetMessage(
          SMLoc::getFromPointer(StartBuf + R.getOffset()),
          WarningsAsErrors ? SourceMgr::DiagKind::DK_Error
                           : SourceMgr::DiagKind::DK_Warning,
          "code should be clang-formatted [-Wclang-format-violations]");

      Diag.print(nullptr, llvm::errs(), (ShowColors && !NoShowColors));
      if (ErrorLimit && ++Errors >= ErrorLimit)
        break;
    }
  }
  return WarningsAsErrors;
}

static void outputXML(const Replacements &Replaces,
                      const Replacements &FormatChanges,
                      const FormattingAttemptStatus &Status,
                      const cl::opt<unsigned> &Cursor,
                      unsigned CursorPosition) {
  outs() << "<?xml version='1.0'?>\n<replacements "
            "xml:space='preserve' incomplete_format='"
         << (Status.FormatComplete ? "false" : "true") << "'";
  if (!Status.FormatComplete)
    outs() << " line='" << Status.Line << "'";
  outs() << ">\n";
  if (Cursor.getNumOccurrences() != 0) {
    outs() << "<cursor>" << FormatChanges.getShiftedCodePosition(CursorPosition)
           << "</cursor>\n";
  }

  outputReplacementsXML(Replaces);
  outs() << "</replacements>\n";
}

class ClangFormatDiagConsumer : public DiagnosticConsumer {
  virtual void anchor() {}

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {

    SmallVector<char, 16> vec;
    Info.FormatDiagnostic(vec);
    errs() << "clang-format error:" << vec << "\n";
  }
};

// Returns true on error.
static bool format(StringRef FileName, bool ErrorOnIncompleteFormat = false) {
  const bool IsSTDIN = FileName == "-";
  if (!OutputXML && Inplace && IsSTDIN) {
    errs() << "error: cannot use -i when reading from stdin.\n";
    return false;
  }
  // On Windows, overwriting a file with an open file mapping doesn't work,
  // so read the whole file into memory when formatting in-place.
  ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
      !OutputXML && Inplace
          ? MemoryBuffer::getFileAsStream(FileName)
          : MemoryBuffer::getFileOrSTDIN(FileName, /*IsText=*/true);
  if (std::error_code EC = CodeOrErr.getError()) {
    errs() << EC.message() << "\n";
    return true;
  }
  std::unique_ptr<llvm::MemoryBuffer> Code = std::move(CodeOrErr.get());
  if (Code->getBufferSize() == 0)
    return false; // Empty files are formatted correctly.

  StringRef BufStr = Code->getBuffer();

  const char *InvalidBOM = SrcMgr::ContentCache::getInvalidBOM(BufStr);

  if (InvalidBOM) {
    errs() << "error: encoding with unsupported byte order mark \""
           << InvalidBOM << "\" detected";
    if (!IsSTDIN)
      errs() << " in file '" << FileName << "'";
    errs() << ".\n";
    return true;
  }

  std::vector<tooling::Range> Ranges;
  if (fillRanges(Code.get(), Ranges))
    return true;
  StringRef AssumedFileName = IsSTDIN ? AssumeFileName : FileName;
  if (AssumedFileName.empty()) {
    llvm::errs() << "error: empty filenames are not allowed\n";
    return true;
  }

  Expected<FormatStyle> FormatStyle =
      getStyle(Style, AssumedFileName, FallbackStyle, Code->getBuffer(),
               nullptr, WNoErrorList.isSet(WNoError::Unknown));
  if (!FormatStyle) {
    llvm::errs() << toString(FormatStyle.takeError()) << "\n";
    return true;
  }

  StringRef QualifierAlignmentOrder = QualifierAlignment;

  FormatStyle->QualifierAlignment =
      StringSwitch<FormatStyle::QualifierAlignmentStyle>(
          QualifierAlignmentOrder.lower())
          .Case("right", FormatStyle::QAS_Right)
          .Case("left", FormatStyle::QAS_Left)
          .Default(FormatStyle->QualifierAlignment);

  if (FormatStyle->QualifierAlignment == FormatStyle::QAS_Left) {
    FormatStyle->QualifierOrder = {"const", "volatile", "type"};
  } else if (FormatStyle->QualifierAlignment == FormatStyle::QAS_Right) {
    FormatStyle->QualifierOrder = {"type", "const", "volatile"};
  } else if (QualifierAlignmentOrder.contains("type")) {
    FormatStyle->QualifierAlignment = FormatStyle::QAS_Custom;
    SmallVector<StringRef> Qualifiers;
    QualifierAlignmentOrder.split(Qualifiers, " ", /*MaxSplit=*/-1,
                                  /*KeepEmpty=*/false);
    FormatStyle->QualifierOrder = {Qualifiers.begin(), Qualifiers.end()};
  }

  if (SortIncludes.getNumOccurrences() != 0) {
    if (SortIncludes)
      FormatStyle->SortIncludes = FormatStyle::SI_CaseSensitive;
    else
      FormatStyle->SortIncludes = FormatStyle::SI_Never;
  }
  unsigned CursorPosition = Cursor;
  Replacements Replaces = sortIncludes(*FormatStyle, Code->getBuffer(), Ranges,
                                       AssumedFileName, &CursorPosition);

  // To format JSON insert a variable to trick the code into thinking its
  // JavaScript.
  if (FormatStyle->isJson() && !FormatStyle->DisableFormat) {
    auto Err = Replaces.add(tooling::Replacement(
        tooling::Replacement(AssumedFileName, 0, 0, "x = ")));
    if (Err)
      llvm::errs() << "Bad Json variable insertion\n";
  }

  auto ChangedCode = tooling::applyAllReplacements(Code->getBuffer(), Replaces);
  if (!ChangedCode) {
    llvm::errs() << toString(ChangedCode.takeError()) << "\n";
    return true;
  }
  // Get new affected ranges after sorting `#includes`.
  Ranges = tooling::calculateRangesAfterReplacements(Replaces, Ranges);
  FormattingAttemptStatus Status;
  Replacements FormatChanges =
      reformat(*FormatStyle, *ChangedCode, Ranges, AssumedFileName, &Status);
  Replaces = Replaces.merge(FormatChanges);
  if (OutputXML || DryRun) {
    if (DryRun)
      return emitReplacementWarnings(Replaces, AssumedFileName, Code);
    outputXML(Replaces, FormatChanges, Status, Cursor, CursorPosition);
  } else {
    IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
        new llvm::vfs::InMemoryFileSystem);
    FileManager Files(FileSystemOptions(), InMemoryFileSystem);

    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts(new DiagnosticOptions());
    ClangFormatDiagConsumer IgnoreDiagnostics;
    DiagnosticsEngine Diagnostics(
        IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs), &*DiagOpts,
        &IgnoreDiagnostics, false);
    SourceManager Sources(Diagnostics, Files);
    FileID ID = createInMemoryFile(AssumedFileName, *Code, Sources, Files,
                                   InMemoryFileSystem.get());
    Rewriter Rewrite(Sources, LangOptions());
    tooling::applyAllReplacements(Replaces, Rewrite);
    if (Inplace) {
      if (Rewrite.overwriteChangedFiles())
        return true;
    } else {
      if (Cursor.getNumOccurrences() != 0) {
        outs() << "{ \"Cursor\": "
               << FormatChanges.getShiftedCodePosition(CursorPosition)
               << ", \"IncompleteFormat\": "
               << (Status.FormatComplete ? "false" : "true");
        if (!Status.FormatComplete)
          outs() << ", \"Line\": " << Status.Line;
        outs() << " }\n";
      }
      Rewrite.getEditBuffer(ID).write(outs());
    }
  }
  return ErrorOnIncompleteFormat && !Status.FormatComplete;
}

} // namespace format
} // namespace clang

static void PrintVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("clang-format") << '\n';
}

// Dump the configuration.
static int dumpConfig() {
  std::unique_ptr<llvm::MemoryBuffer> Code;
  // We can't read the code to detect the language if there's no file name.
  if (!FileNames.empty()) {
    // Read in the code in case the filename alone isn't enough to detect the
    // language.
    ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
        MemoryBuffer::getFileOrSTDIN(FileNames[0], /*IsText=*/true);
    if (std::error_code EC = CodeOrErr.getError()) {
      llvm::errs() << EC.message() << "\n";
      return 1;
    }
    Code = std::move(CodeOrErr.get());
  }
  Expected<clang::format::FormatStyle> FormatStyle = clang::format::getStyle(
      Style,
      FileNames.empty() || FileNames[0] == "-" ? AssumeFileName : FileNames[0],
      FallbackStyle, Code ? Code->getBuffer() : "");
  if (!FormatStyle) {
    llvm::errs() << toString(FormatStyle.takeError()) << "\n";
    return 1;
  }
  std::string Config = clang::format::configurationAsText(*FormatStyle);
  outs() << Config << "\n";
  return 0;
}

using String = SmallString<128>;
static String IgnoreDir;             // Directory of .clang-format-ignore file.
static String PrevDir;               // Directory of previous `FilePath`.
static SmallVector<String> Patterns; // Patterns in .clang-format-ignore file.

// Check whether `FilePath` is ignored according to the nearest
// .clang-format-ignore file based on the rules below:
// - A blank line is skipped.
// - Leading and trailing spaces of a line are trimmed.
// - A line starting with a hash (`#`) is a comment.
// - A non-comment line is a single pattern.
// - The slash (`/`) is used as the directory separator.
// - A pattern is relative to the directory of the .clang-format-ignore file (or
//   the root directory if the pattern starts with a slash).
// - A pattern is negated if it starts with a bang (`!`).
static bool isIgnored(StringRef FilePath) {
  using namespace llvm::sys::fs;
  if (!is_regular_file(FilePath))
    return false;

  String Path;
  String AbsPath{FilePath};

  using namespace llvm::sys::path;
  make_absolute(AbsPath);
  remove_dots(AbsPath, /*remove_dot_dot=*/true);

  if (StringRef Dir{parent_path(AbsPath)}; PrevDir != Dir) {
    PrevDir = Dir;

    for (;;) {
      Path = Dir;
      append(Path, ".clang-format-ignore");
      if (is_regular_file(Path))
        break;
      Dir = parent_path(Dir);
      if (Dir.empty())
        return false;
    }

    IgnoreDir = convert_to_slash(Dir);

    std::ifstream IgnoreFile{Path.c_str()};
    if (!IgnoreFile.good())
      return false;

    Patterns.clear();

    for (std::string Line; std::getline(IgnoreFile, Line);) {
      if (const auto Pattern{StringRef{Line}.trim()};
          // Skip empty and comment lines.
          !Pattern.empty() && Pattern[0] != '#') {
        Patterns.push_back(Pattern);
      }
    }
  }

  if (IgnoreDir.empty())
    return false;

  const auto Pathname{convert_to_slash(AbsPath)};
  for (const auto &Pat : Patterns) {
    const bool IsNegated = Pat[0] == '!';
    StringRef Pattern{Pat};
    if (IsNegated)
      Pattern = Pattern.drop_front();

    if (Pattern.empty())
      continue;

    Pattern = Pattern.ltrim();

    // `Pattern` is relative to `IgnoreDir` unless it starts with a slash.
    // This doesn't support patterns containing drive names (e.g. `C:`).
    if (Pattern[0] != '/') {
      Path = IgnoreDir;
      append(Path, Style::posix, Pattern);
      remove_dots(Path, /*remove_dot_dot=*/true, Style::posix);
      Pattern = Path;
    }

    if (clang::format::matchFilePath(Pattern, Pathname) == !IsNegated)
      return true;
  }

  return false;
}

int main(int argc, const char **argv) {
  InitLLVM X(argc, argv);

  cl::HideUnrelatedOptions(ClangFormatCategory);

  cl::SetVersionPrinter(PrintVersion);
  cl::ParseCommandLineOptions(
      argc, argv,
      "A tool to format C/C++/Java/JavaScript/JSON/Objective-C/Protobuf/C# "
      "code.\n\n"
      "If no arguments are specified, it formats the code from standard input\n"
      "and writes the result to the standard output.\n"
      "If <file>s are given, it reformats the files. If -i is specified\n"
      "together with <file>s, the files are edited in-place. Otherwise, the\n"
      "result is written to the standard output.\n");

  if (Help) {
    cl::PrintHelpMessage();
    return 0;
  }

  if (DumpConfig)
    return dumpConfig();

  if (!Files.empty()) {
    std::ifstream ExternalFileOfFiles{std::string(Files)};
    std::string Line;
    unsigned LineNo = 1;
    while (std::getline(ExternalFileOfFiles, Line)) {
      FileNames.push_back(Line);
      LineNo++;
    }
    errs() << "Clang-formating " << LineNo << " files\n";
  }

  if (FileNames.empty())
    return clang::format::format("-", FailOnIncompleteFormat);

  if (FileNames.size() > 1 &&
      (!Offsets.empty() || !Lengths.empty() || !LineRanges.empty())) {
    errs() << "error: -offset, -length and -lines can only be used for "
              "single file.\n";
    return 1;
  }

  unsigned FileNo = 1;
  bool Error = false;
  for (const auto &FileName : FileNames) {
    const bool Ignored = isIgnored(FileName);
    if (ListIgnored) {
      if (Ignored)
        outs() << FileName << '\n';
      continue;
    }
    if (Ignored)
      continue;
    if (Verbose) {
      errs() << "Formatting [" << FileNo++ << "/" << FileNames.size() << "] "
             << FileName << "\n";
    }
    Error |= clang::format::format(FileName, FailOnIncompleteFormat);
  }
  return Error ? 1 : 0;
}
