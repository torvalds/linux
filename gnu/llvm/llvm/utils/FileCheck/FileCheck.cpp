//===- FileCheck.cpp - Check that File's Contents match what is expected --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FileCheck does a line-by line check of a file that validates whether it
// contains the expected content.  This is useful for regression tests etc.
//
// This program exits with an exit status of 2 on error, exit status of 0 if
// the file matched the expected contents, and exit status of 1 if it did not
// contain the expected contents.
//
//===----------------------------------------------------------------------===//

#include "llvm/FileCheck/FileCheck.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>
#include <map>
using namespace llvm;

static cl::extrahelp FileCheckOptsEnv(
    "\nOptions are parsed from the environment variable FILECHECK_OPTS and\n"
    "from the command line.\n");

static cl::opt<std::string>
    CheckFilename(cl::Positional, cl::desc("<check-file>"), cl::Optional);

static cl::opt<std::string>
    InputFilename("input-file", cl::desc("File to check (defaults to stdin)"),
                  cl::init("-"), cl::value_desc("filename"));

static cl::list<std::string> CheckPrefixes(
    "check-prefix",
    cl::desc("Prefix to use from check file (defaults to 'CHECK')"));
static cl::alias CheckPrefixesAlias(
    "check-prefixes", cl::aliasopt(CheckPrefixes), cl::CommaSeparated,
    cl::NotHidden,
    cl::desc(
        "Alias for -check-prefix permitting multiple comma separated values"));

static cl::list<std::string> CommentPrefixes(
    "comment-prefixes", cl::CommaSeparated, cl::Hidden,
    cl::desc("Comma-separated list of comment prefixes to use from check file\n"
             "(defaults to 'COM,RUN'). Please avoid using this feature in\n"
             "LLVM's LIT-based test suites, which should be easier to\n"
             "maintain if they all follow a consistent comment style. This\n"
             "feature is meant for non-LIT test suites using FileCheck."));

static cl::opt<bool> NoCanonicalizeWhiteSpace(
    "strict-whitespace",
    cl::desc("Do not treat all horizontal whitespace as equivalent"));

static cl::opt<bool> IgnoreCase(
    "ignore-case",
    cl::desc("Use case-insensitive matching"));

static cl::list<std::string> ImplicitCheckNot(
    "implicit-check-not",
    cl::desc("Add an implicit negative check with this pattern to every\n"
             "positive check. This can be used to ensure that no instances of\n"
             "this pattern occur which are not matched by a positive pattern"),
    cl::value_desc("pattern"));

static cl::list<std::string>
    GlobalDefines("D", cl::AlwaysPrefix,
                  cl::desc("Define a variable to be used in capture patterns."),
                  cl::value_desc("VAR=VALUE"));

static cl::opt<bool> AllowEmptyInput(
    "allow-empty", cl::init(false),
    cl::desc("Allow the input file to be empty. This is useful when making\n"
             "checks that some error message does not occur, for example."));

static cl::opt<bool> AllowUnusedPrefixes(
    "allow-unused-prefixes",
    cl::desc("Allow prefixes to be specified but not appear in the test."));

static cl::opt<bool> MatchFullLines(
    "match-full-lines", cl::init(false),
    cl::desc("Require all positive matches to cover an entire input line.\n"
             "Allows leading and trailing whitespace if --strict-whitespace\n"
             "is not also passed."));

static cl::opt<bool> EnableVarScope(
    "enable-var-scope", cl::init(false),
    cl::desc("Enables scope for regex variables. Variables with names that\n"
             "do not start with '$' will be reset at the beginning of\n"
             "each CHECK-LABEL block."));

static cl::opt<bool> AllowDeprecatedDagOverlap(
    "allow-deprecated-dag-overlap", cl::init(false),
    cl::desc("Enable overlapping among matches in a group of consecutive\n"
             "CHECK-DAG directives.  This option is deprecated and is only\n"
             "provided for convenience as old tests are migrated to the new\n"
             "non-overlapping CHECK-DAG implementation.\n"));

static cl::opt<bool> Verbose(
    "v",
    cl::desc("Print directive pattern matches, or add them to the input dump\n"
             "if enabled.\n"));

static cl::opt<bool> VerboseVerbose(
    "vv",
    cl::desc("Print information helpful in diagnosing internal FileCheck\n"
             "issues, or add it to the input dump if enabled.  Implies\n"
             "-v.\n"));

// The order of DumpInputValue members affects their precedence, as documented
// for -dump-input below.
enum DumpInputValue {
  DumpInputNever,
  DumpInputFail,
  DumpInputAlways,
  DumpInputHelp
};

static cl::list<DumpInputValue> DumpInputs(
    "dump-input",
    cl::desc("Dump input to stderr, adding annotations representing\n"
             "currently enabled diagnostics.  When there are multiple\n"
             "occurrences of this option, the <value> that appears earliest\n"
             "in the list below has precedence.  The default is 'fail'.\n"),
    cl::value_desc("mode"),
    cl::values(clEnumValN(DumpInputHelp, "help", "Explain input dump and quit"),
               clEnumValN(DumpInputAlways, "always", "Always dump input"),
               clEnumValN(DumpInputFail, "fail", "Dump input on failure"),
               clEnumValN(DumpInputNever, "never", "Never dump input")));

// The order of DumpInputFilterValue members affects their precedence, as
// documented for -dump-input-filter below.
enum DumpInputFilterValue {
  DumpInputFilterError,
  DumpInputFilterAnnotation,
  DumpInputFilterAnnotationFull,
  DumpInputFilterAll
};

static cl::list<DumpInputFilterValue> DumpInputFilters(
    "dump-input-filter",
    cl::desc("In the dump requested by -dump-input, print only input lines of\n"
             "kind <value> plus any context specified by -dump-input-context.\n"
             "When there are multiple occurrences of this option, the <value>\n"
             "that appears earliest in the list below has precedence.  The\n"
             "default is 'error' when -dump-input=fail, and it's 'all' when\n"
             "-dump-input=always.\n"),
    cl::values(clEnumValN(DumpInputFilterAll, "all", "All input lines"),
               clEnumValN(DumpInputFilterAnnotationFull, "annotation-full",
                          "Input lines with annotations"),
               clEnumValN(DumpInputFilterAnnotation, "annotation",
                          "Input lines with starting points of annotations"),
               clEnumValN(DumpInputFilterError, "error",
                          "Input lines with starting points of error "
                          "annotations")));

static cl::list<unsigned> DumpInputContexts(
    "dump-input-context", cl::value_desc("N"),
    cl::desc("In the dump requested by -dump-input, print <N> input lines\n"
             "before and <N> input lines after any lines specified by\n"
             "-dump-input-filter.  When there are multiple occurrences of\n"
             "this option, the largest specified <N> has precedence.  The\n"
             "default is 5.\n"));

typedef cl::list<std::string>::const_iterator prefix_iterator;







static void DumpCommandLine(int argc, char **argv) {
  errs() << "FileCheck command line: ";
  for (int I = 0; I < argc; I++)
    errs() << " " << argv[I];
  errs() << "\n";
}

struct MarkerStyle {
  /// The starting char (before tildes) for marking the line.
  char Lead;
  /// What color to use for this annotation.
  raw_ostream::Colors Color;
  /// A note to follow the marker, or empty string if none.
  std::string Note;
  /// Does this marker indicate inclusion by -dump-input-filter=error?
  bool FiltersAsError;
  MarkerStyle() {}
  MarkerStyle(char Lead, raw_ostream::Colors Color,
              const std::string &Note = "", bool FiltersAsError = false)
      : Lead(Lead), Color(Color), Note(Note), FiltersAsError(FiltersAsError) {
    assert((!FiltersAsError || !Note.empty()) &&
           "expected error diagnostic to have note");
  }
};

static MarkerStyle GetMarker(FileCheckDiag::MatchType MatchTy) {
  switch (MatchTy) {
  case FileCheckDiag::MatchFoundAndExpected:
    return MarkerStyle('^', raw_ostream::GREEN);
  case FileCheckDiag::MatchFoundButExcluded:
    return MarkerStyle('!', raw_ostream::RED, "error: no match expected",
                       /*FiltersAsError=*/true);
  case FileCheckDiag::MatchFoundButWrongLine:
    return MarkerStyle('!', raw_ostream::RED, "error: match on wrong line",
                       /*FiltersAsError=*/true);
  case FileCheckDiag::MatchFoundButDiscarded:
    return MarkerStyle('!', raw_ostream::CYAN,
                       "discard: overlaps earlier match");
  case FileCheckDiag::MatchFoundErrorNote:
    // Note should always be overridden within the FileCheckDiag.
    return MarkerStyle('!', raw_ostream::RED,
                       "error: unknown error after match",
                       /*FiltersAsError=*/true);
  case FileCheckDiag::MatchNoneAndExcluded:
    return MarkerStyle('X', raw_ostream::GREEN);
  case FileCheckDiag::MatchNoneButExpected:
    return MarkerStyle('X', raw_ostream::RED, "error: no match found",
                       /*FiltersAsError=*/true);
  case FileCheckDiag::MatchNoneForInvalidPattern:
    return MarkerStyle('X', raw_ostream::RED,
                       "error: match failed for invalid pattern",
                       /*FiltersAsError=*/true);
  case FileCheckDiag::MatchFuzzy:
    return MarkerStyle('?', raw_ostream::MAGENTA, "possible intended match",
                       /*FiltersAsError=*/true);
  }
  llvm_unreachable_internal("unexpected match type");
}

static void DumpInputAnnotationHelp(raw_ostream &OS) {
  OS << "The following description was requested by -dump-input=help to\n"
     << "explain the input dump printed by FileCheck.\n"
     << "\n"
     << "Related command-line options:\n"
     << "\n"
     << "  - -dump-input=<value> enables or disables the input dump\n"
     << "  - -dump-input-filter=<value> filters the input lines\n"
     << "  - -dump-input-context=<N> adjusts the context of filtered lines\n"
     << "  - -v and -vv add more annotations\n"
     << "  - -color forces colors to be enabled both in the dump and below\n"
     << "  - -help documents the above options in more detail\n"
     << "\n"
     << "These options can also be set via FILECHECK_OPTS.  For example, for\n"
     << "maximum debugging output on failures:\n"
     << "\n"
     << "  $ FILECHECK_OPTS='-dump-input-filter=all -vv -color' ninja check\n"
     << "\n"
     << "Input dump annotation format:\n"
     << "\n";

  // Labels for input lines.
  OS << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "L:";
  OS << "     labels line number L of the input file\n"
     << "           An extra space is added after each input line to represent"
     << " the\n"
     << "           newline character\n";

  // Labels for annotation lines.
  OS << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "T:L";
  OS << "    labels the only match result for either (1) a pattern of type T"
     << " from\n"
     << "           line L of the check file if L is an integer or (2) the"
     << " I-th implicit\n"
     << "           pattern if L is \"imp\" followed by an integer "
     << "I (index origin one)\n";
  OS << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "T:L'N";
  OS << "  labels the Nth match result for such a pattern\n";

  // Markers on annotation lines.
  OS << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "^~~";
  OS << "    marks good match (reported if -v)\n"
     << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "!~~";
  OS << "    marks bad match, such as:\n"
     << "           - CHECK-NEXT on same line as previous match (error)\n"
     << "           - CHECK-NOT found (error)\n"
     << "           - CHECK-DAG overlapping match (discarded, reported if "
     << "-vv)\n"
     << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "X~~";
  OS << "    marks search range when no match is found, such as:\n"
     << "           - CHECK-NEXT not found (error)\n"
     << "           - CHECK-NOT not found (success, reported if -vv)\n"
     << "           - CHECK-DAG not found after discarded matches (error)\n"
     << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "?";
  OS << "      marks fuzzy match when no match is found\n";

  // Elided lines.
  OS << "  - ";
  WithColor(OS, raw_ostream::SAVEDCOLOR, true) << "...";
  OS << "    indicates elided input lines and annotations, as specified by\n"
     << "           -dump-input-filter and -dump-input-context\n";

  // Colors.
  OS << "  - colors ";
  WithColor(OS, raw_ostream::GREEN, true) << "success";
  OS << ", ";
  WithColor(OS, raw_ostream::RED, true) << "error";
  OS << ", ";
  WithColor(OS, raw_ostream::MAGENTA, true) << "fuzzy match";
  OS << ", ";
  WithColor(OS, raw_ostream::CYAN, true, false) << "discarded match";
  OS << ", ";
  WithColor(OS, raw_ostream::CYAN, true, true) << "unmatched input";
  OS << "\n";
}

/// An annotation for a single input line.
struct InputAnnotation {
  /// The index of the match result across all checks
  unsigned DiagIndex;
  /// The label for this annotation.
  std::string Label;
  /// Is this the initial fragment of a diagnostic that has been broken across
  /// multiple lines?
  bool IsFirstLine;
  /// What input line (one-origin indexing) this annotation marks.  This might
  /// be different from the starting line of the original diagnostic if
  /// !IsFirstLine.
  unsigned InputLine;
  /// The column range (one-origin indexing, open end) in which to mark the
  /// input line.  If InputEndCol is UINT_MAX, treat it as the last column
  /// before the newline.
  unsigned InputStartCol, InputEndCol;
  /// The marker to use.
  MarkerStyle Marker;
  /// Whether this annotation represents a good match for an expected pattern.
  bool FoundAndExpectedMatch;
};

/// Get an abbreviation for the check type.
static std::string GetCheckTypeAbbreviation(Check::FileCheckType Ty) {
  switch (Ty) {
  case Check::CheckPlain:
    if (Ty.getCount() > 1)
      return "count";
    return "check";
  case Check::CheckNext:
    return "next";
  case Check::CheckSame:
    return "same";
  case Check::CheckNot:
    return "not";
  case Check::CheckDAG:
    return "dag";
  case Check::CheckLabel:
    return "label";
  case Check::CheckEmpty:
    return "empty";
  case Check::CheckComment:
    return "com";
  case Check::CheckEOF:
    return "eof";
  case Check::CheckBadNot:
    return "bad-not";
  case Check::CheckBadCount:
    return "bad-count";
  case Check::CheckMisspelled:
    return "misspelled";
  case Check::CheckNone:
    llvm_unreachable("invalid FileCheckType");
  }
  llvm_unreachable("unknown FileCheckType");
}

static void
BuildInputAnnotations(const SourceMgr &SM, unsigned CheckFileBufferID,
                      const std::pair<unsigned, unsigned> &ImpPatBufferIDRange,
                      const std::vector<FileCheckDiag> &Diags,
                      std::vector<InputAnnotation> &Annotations,
                      unsigned &LabelWidth) {
  struct CompareSMLoc {
    bool operator()(const SMLoc &LHS, const SMLoc &RHS) const {
      return LHS.getPointer() < RHS.getPointer();
    }
  };
  // How many diagnostics does each pattern have?
  std::map<SMLoc, unsigned, CompareSMLoc> DiagCountPerPattern;
  for (const FileCheckDiag &Diag : Diags)
    ++DiagCountPerPattern[Diag.CheckLoc];
  // How many diagnostics have we seen so far per pattern?
  std::map<SMLoc, unsigned, CompareSMLoc> DiagIndexPerPattern;
  // How many total diagnostics have we seen so far?
  unsigned DiagIndex = 0;
  // What's the widest label?
  LabelWidth = 0;
  for (auto DiagItr = Diags.begin(), DiagEnd = Diags.end(); DiagItr != DiagEnd;
       ++DiagItr) {
    InputAnnotation A;
    A.DiagIndex = DiagIndex++;

    // Build label, which uniquely identifies this check result.
    unsigned CheckBufferID = SM.FindBufferContainingLoc(DiagItr->CheckLoc);
    auto CheckLineAndCol =
        SM.getLineAndColumn(DiagItr->CheckLoc, CheckBufferID);
    llvm::raw_string_ostream Label(A.Label);
    Label << GetCheckTypeAbbreviation(DiagItr->CheckTy) << ":";
    if (CheckBufferID == CheckFileBufferID)
      Label << CheckLineAndCol.first;
    else if (ImpPatBufferIDRange.first <= CheckBufferID &&
             CheckBufferID < ImpPatBufferIDRange.second)
      Label << "imp" << (CheckBufferID - ImpPatBufferIDRange.first + 1);
    else
      llvm_unreachable("expected diagnostic's check location to be either in "
                       "the check file or for an implicit pattern");
    if (DiagCountPerPattern[DiagItr->CheckLoc] > 1)
      Label << "'" << DiagIndexPerPattern[DiagItr->CheckLoc]++;
    LabelWidth = std::max((std::string::size_type)LabelWidth, A.Label.size());

    A.Marker = GetMarker(DiagItr->MatchTy);
    if (!DiagItr->Note.empty()) {
      A.Marker.Note = DiagItr->Note;
      // It's less confusing if notes that don't actually have ranges don't have
      // markers.  For example, a marker for 'with "VAR" equal to "5"' would
      // seem to indicate where "VAR" matches, but the location we actually have
      // for the marker simply points to the start of the match/search range for
      // the full pattern of which the substitution is potentially just one
      // component.
      if (DiagItr->InputStartLine == DiagItr->InputEndLine &&
          DiagItr->InputStartCol == DiagItr->InputEndCol)
        A.Marker.Lead = ' ';
    }
    if (DiagItr->MatchTy == FileCheckDiag::MatchFoundErrorNote) {
      assert(!DiagItr->Note.empty() &&
             "expected custom note for MatchFoundErrorNote");
      A.Marker.Note = "error: " + A.Marker.Note;
    }
    A.FoundAndExpectedMatch =
        DiagItr->MatchTy == FileCheckDiag::MatchFoundAndExpected;

    // Compute the mark location, and break annotation into multiple
    // annotations if it spans multiple lines.
    A.IsFirstLine = true;
    A.InputLine = DiagItr->InputStartLine;
    A.InputStartCol = DiagItr->InputStartCol;
    if (DiagItr->InputStartLine == DiagItr->InputEndLine) {
      // Sometimes ranges are empty in order to indicate a specific point, but
      // that would mean nothing would be marked, so adjust the range to
      // include the following character.
      A.InputEndCol =
          std::max(DiagItr->InputStartCol + 1, DiagItr->InputEndCol);
      Annotations.push_back(A);
    } else {
      assert(DiagItr->InputStartLine < DiagItr->InputEndLine &&
             "expected input range not to be inverted");
      A.InputEndCol = UINT_MAX;
      Annotations.push_back(A);
      for (unsigned L = DiagItr->InputStartLine + 1, E = DiagItr->InputEndLine;
           L <= E; ++L) {
        // If a range ends before the first column on a line, then it has no
        // characters on that line, so there's nothing to render.
        if (DiagItr->InputEndCol == 1 && L == E)
          break;
        InputAnnotation B;
        B.DiagIndex = A.DiagIndex;
        B.Label = A.Label;
        B.IsFirstLine = false;
        B.InputLine = L;
        B.Marker = A.Marker;
        B.Marker.Lead = '~';
        B.Marker.Note = "";
        B.InputStartCol = 1;
        if (L != E)
          B.InputEndCol = UINT_MAX;
        else
          B.InputEndCol = DiagItr->InputEndCol;
        B.FoundAndExpectedMatch = A.FoundAndExpectedMatch;
        Annotations.push_back(B);
      }
    }
  }
}

static unsigned FindInputLineInFilter(
    DumpInputFilterValue DumpInputFilter, unsigned CurInputLine,
    const std::vector<InputAnnotation>::iterator &AnnotationBeg,
    const std::vector<InputAnnotation>::iterator &AnnotationEnd) {
  if (DumpInputFilter == DumpInputFilterAll)
    return CurInputLine;
  for (auto AnnotationItr = AnnotationBeg; AnnotationItr != AnnotationEnd;
       ++AnnotationItr) {
    switch (DumpInputFilter) {
    case DumpInputFilterAll:
      llvm_unreachable("unexpected DumpInputFilterAll");
      break;
    case DumpInputFilterAnnotationFull:
      return AnnotationItr->InputLine;
    case DumpInputFilterAnnotation:
      if (AnnotationItr->IsFirstLine)
        return AnnotationItr->InputLine;
      break;
    case DumpInputFilterError:
      if (AnnotationItr->IsFirstLine && AnnotationItr->Marker.FiltersAsError)
        return AnnotationItr->InputLine;
      break;
    }
  }
  return UINT_MAX;
}

/// To OS, print a vertical ellipsis (right-justified at LabelWidth) if it would
/// occupy less lines than ElidedLines, but print ElidedLines otherwise.  Either
/// way, clear ElidedLines.  Thus, if ElidedLines is empty, do nothing.
static void DumpEllipsisOrElidedLines(raw_ostream &OS, std::string &ElidedLines,
                                      unsigned LabelWidth) {
  if (ElidedLines.empty())
    return;
  unsigned EllipsisLines = 3;
  if (EllipsisLines < StringRef(ElidedLines).count('\n')) {
    for (unsigned i = 0; i < EllipsisLines; ++i) {
      WithColor(OS, raw_ostream::BLACK, /*Bold=*/true)
          << right_justify(".", LabelWidth);
      OS << '\n';
    }
  } else
    OS << ElidedLines;
  ElidedLines.clear();
}

static void DumpAnnotatedInput(raw_ostream &OS, const FileCheckRequest &Req,
                               DumpInputFilterValue DumpInputFilter,
                               unsigned DumpInputContext,
                               StringRef InputFileText,
                               std::vector<InputAnnotation> &Annotations,
                               unsigned LabelWidth) {
  OS << "Input was:\n<<<<<<\n";

  // Sort annotations.
  llvm::sort(Annotations,
             [](const InputAnnotation &A, const InputAnnotation &B) {
               // 1. Sort annotations in the order of the input lines.
               //
               // This makes it easier to find relevant annotations while
               // iterating input lines in the implementation below.  FileCheck
               // does not always produce diagnostics in the order of input
               // lines due to, for example, CHECK-DAG and CHECK-NOT.
               if (A.InputLine != B.InputLine)
                 return A.InputLine < B.InputLine;
               // 2. Sort annotations in the temporal order FileCheck produced
               // their associated diagnostics.
               //
               // This sort offers several benefits:
               //
               // A. On a single input line, the order of annotations reflects
               //    the FileCheck logic for processing directives/patterns.
               //    This can be helpful in understanding cases in which the
               //    order of the associated directives/patterns in the check
               //    file or on the command line either (i) does not match the
               //    temporal order in which FileCheck looks for matches for the
               //    directives/patterns (due to, for example, CHECK-LABEL,
               //    CHECK-NOT, or `--implicit-check-not`) or (ii) does match
               //    that order but does not match the order of those
               //    diagnostics along an input line (due to, for example,
               //    CHECK-DAG).
               //
               //    On the other hand, because our presentation format presents
               //    input lines in order, there's no clear way to offer the
               //    same benefit across input lines.  For consistency, it might
               //    then seem worthwhile to have annotations on a single line
               //    also sorted in input order (that is, by input column).
               //    However, in practice, this appears to be more confusing
               //    than helpful.  Perhaps it's intuitive to expect annotations
               //    to be listed in the temporal order in which they were
               //    produced except in cases the presentation format obviously
               //    and inherently cannot support it (that is, across input
               //    lines).
               //
               // B. When diagnostics' annotations are split among multiple
               //    input lines, the user must track them from one input line
               //    to the next.  One property of the sort chosen here is that
               //    it facilitates the user in this regard by ensuring the
               //    following: when comparing any two input lines, a
               //    diagnostic's annotations are sorted in the same position
               //    relative to all other diagnostics' annotations.
               return A.DiagIndex < B.DiagIndex;
             });

  // Compute the width of the label column.
  const unsigned char *InputFilePtr = InputFileText.bytes_begin(),
                      *InputFileEnd = InputFileText.bytes_end();
  unsigned LineCount = InputFileText.count('\n');
  if (InputFileEnd[-1] != '\n')
    ++LineCount;
  unsigned LineNoWidth = std::log10(LineCount) + 1;
  // +3 below adds spaces (1) to the left of the (right-aligned) line numbers
  // on input lines and (2) to the right of the (left-aligned) labels on
  // annotation lines so that input lines and annotation lines are more
  // visually distinct.  For example, the spaces on the annotation lines ensure
  // that input line numbers and check directive line numbers never align
  // horizontally.  Those line numbers might not even be for the same file.
  // One space would be enough to achieve that, but more makes it even easier
  // to see.
  LabelWidth = std::max(LabelWidth, LineNoWidth) + 3;

  // Print annotated input lines.
  unsigned PrevLineInFilter = 0; // 0 means none so far
  unsigned NextLineInFilter = 0; // 0 means uncomputed, UINT_MAX means none
  std::string ElidedLines;
  raw_string_ostream ElidedLinesOS(ElidedLines);
  ColorMode TheColorMode =
      WithColor(OS).colorsEnabled() ? ColorMode::Enable : ColorMode::Disable;
  if (TheColorMode == ColorMode::Enable)
    ElidedLinesOS.enable_colors(true);
  auto AnnotationItr = Annotations.begin(), AnnotationEnd = Annotations.end();
  for (unsigned Line = 1;
       InputFilePtr != InputFileEnd || AnnotationItr != AnnotationEnd;
       ++Line) {
    const unsigned char *InputFileLine = InputFilePtr;

    // Compute the previous and next line included by the filter.
    if (NextLineInFilter < Line)
      NextLineInFilter = FindInputLineInFilter(DumpInputFilter, Line,
                                               AnnotationItr, AnnotationEnd);
    assert(NextLineInFilter && "expected NextLineInFilter to be computed");
    if (NextLineInFilter == Line)
      PrevLineInFilter = Line;

    // Elide this input line and its annotations if it's not within the
    // context specified by -dump-input-context of an input line included by
    // -dump-input-filter.  However, in case the resulting ellipsis would occupy
    // more lines than the input lines and annotations it elides, buffer the
    // elided lines and annotations so we can print them instead.
    raw_ostream *LineOS;
    if ((!PrevLineInFilter || PrevLineInFilter + DumpInputContext < Line) &&
        (NextLineInFilter == UINT_MAX ||
         Line + DumpInputContext < NextLineInFilter))
      LineOS = &ElidedLinesOS;
    else {
      LineOS = &OS;
      DumpEllipsisOrElidedLines(OS, ElidedLines, LabelWidth);
    }

    // Print right-aligned line number.
    WithColor(*LineOS, raw_ostream::BLACK, /*Bold=*/true, /*BF=*/false,
              TheColorMode)
        << format_decimal(Line, LabelWidth) << ": ";

    // For the case where -v and colors are enabled, find the annotations for
    // good matches for expected patterns in order to highlight everything
    // else in the line.  There are no such annotations if -v is disabled.
    std::vector<InputAnnotation> FoundAndExpectedMatches;
    if (Req.Verbose && TheColorMode == ColorMode::Enable) {
      for (auto I = AnnotationItr; I != AnnotationEnd && I->InputLine == Line;
           ++I) {
        if (I->FoundAndExpectedMatch)
          FoundAndExpectedMatches.push_back(*I);
      }
    }

    // Print numbered line with highlighting where there are no matches for
    // expected patterns.
    bool Newline = false;
    {
      WithColor COS(*LineOS, raw_ostream::SAVEDCOLOR, /*Bold=*/false,
                    /*BG=*/false, TheColorMode);
      bool InMatch = false;
      if (Req.Verbose)
        COS.changeColor(raw_ostream::CYAN, true, true);
      for (unsigned Col = 1; InputFilePtr != InputFileEnd && !Newline; ++Col) {
        bool WasInMatch = InMatch;
        InMatch = false;
        for (const InputAnnotation &M : FoundAndExpectedMatches) {
          if (M.InputStartCol <= Col && Col < M.InputEndCol) {
            InMatch = true;
            break;
          }
        }
        if (!WasInMatch && InMatch)
          COS.resetColor();
        else if (WasInMatch && !InMatch)
          COS.changeColor(raw_ostream::CYAN, true, true);
        if (*InputFilePtr == '\n') {
          Newline = true;
          COS << ' ';
        } else
          COS << *InputFilePtr;
        ++InputFilePtr;
      }
    }
    *LineOS << '\n';
    unsigned InputLineWidth = InputFilePtr - InputFileLine;

    // Print any annotations.
    while (AnnotationItr != AnnotationEnd &&
           AnnotationItr->InputLine == Line) {
      WithColor COS(*LineOS, AnnotationItr->Marker.Color, /*Bold=*/true,
                    /*BG=*/false, TheColorMode);
      // The two spaces below are where the ": " appears on input lines.
      COS << left_justify(AnnotationItr->Label, LabelWidth) << "  ";
      unsigned Col;
      for (Col = 1; Col < AnnotationItr->InputStartCol; ++Col)
        COS << ' ';
      COS << AnnotationItr->Marker.Lead;
      // If InputEndCol=UINT_MAX, stop at InputLineWidth.
      for (++Col; Col < AnnotationItr->InputEndCol && Col <= InputLineWidth;
           ++Col)
        COS << '~';
      const std::string &Note = AnnotationItr->Marker.Note;
      if (!Note.empty()) {
        // Put the note at the end of the input line.  If we were to instead
        // put the note right after the marker, subsequent annotations for the
        // same input line might appear to mark this note instead of the input
        // line.
        for (; Col <= InputLineWidth; ++Col)
          COS << ' ';
        COS << ' ' << Note;
      }
      COS << '\n';
      ++AnnotationItr;
    }
  }
  DumpEllipsisOrElidedLines(OS, ElidedLines, LabelWidth);

  OS << ">>>>>>\n";
}

int main(int argc, char **argv) {
  // Enable use of ANSI color codes because FileCheck is using them to
  // highlight text.
  llvm::sys::Process::UseANSIEscapeCodes(true);

  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv, /*Overview*/ "", /*Errs*/ nullptr,
                              "FILECHECK_OPTS");

  // Select -dump-input* values.  The -help documentation specifies the default
  // value and which value to choose if an option is specified multiple times.
  // In the latter case, the general rule of thumb is to choose the value that
  // provides the most information.
  DumpInputValue DumpInput =
      DumpInputs.empty() ? DumpInputFail : *llvm::max_element(DumpInputs);
  DumpInputFilterValue DumpInputFilter;
  if (DumpInputFilters.empty())
    DumpInputFilter = DumpInput == DumpInputAlways ? DumpInputFilterAll
                                                   : DumpInputFilterError;
  else
    DumpInputFilter = *llvm::max_element(DumpInputFilters);
  unsigned DumpInputContext =
      DumpInputContexts.empty() ? 5 : *llvm::max_element(DumpInputContexts);

  if (DumpInput == DumpInputHelp) {
    DumpInputAnnotationHelp(outs());
    return 0;
  }
  if (CheckFilename.empty()) {
    errs() << "<check-file> not specified\n";
    return 2;
  }

  FileCheckRequest Req;
  append_range(Req.CheckPrefixes, CheckPrefixes);

  append_range(Req.CommentPrefixes, CommentPrefixes);

  append_range(Req.ImplicitCheckNot, ImplicitCheckNot);

  bool GlobalDefineError = false;
  for (StringRef G : GlobalDefines) {
    size_t EqIdx = G.find('=');
    if (EqIdx == std::string::npos) {
      errs() << "Missing equal sign in command-line definition '-D" << G
             << "'\n";
      GlobalDefineError = true;
      continue;
    }
    if (EqIdx == 0) {
      errs() << "Missing variable name in command-line definition '-D" << G
             << "'\n";
      GlobalDefineError = true;
      continue;
    }
    Req.GlobalDefines.push_back(G);
  }
  if (GlobalDefineError)
    return 2;

  Req.AllowEmptyInput = AllowEmptyInput;
  Req.AllowUnusedPrefixes = AllowUnusedPrefixes;
  Req.EnableVarScope = EnableVarScope;
  Req.AllowDeprecatedDagOverlap = AllowDeprecatedDagOverlap;
  Req.Verbose = Verbose;
  Req.VerboseVerbose = VerboseVerbose;
  Req.NoCanonicalizeWhiteSpace = NoCanonicalizeWhiteSpace;
  Req.MatchFullLines = MatchFullLines;
  Req.IgnoreCase = IgnoreCase;

  if (VerboseVerbose)
    Req.Verbose = true;

  FileCheck FC(Req);
  if (!FC.ValidateCheckPrefixes())
    return 2;

  SourceMgr SM;

  // Read the expected strings from the check file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> CheckFileOrErr =
      MemoryBuffer::getFileOrSTDIN(CheckFilename, /*IsText=*/true);
  if (std::error_code EC = CheckFileOrErr.getError()) {
    errs() << "Could not open check file '" << CheckFilename
           << "': " << EC.message() << '\n';
    return 2;
  }
  MemoryBuffer &CheckFile = *CheckFileOrErr.get();

  SmallString<4096> CheckFileBuffer;
  StringRef CheckFileText = FC.CanonicalizeFile(CheckFile, CheckFileBuffer);

  unsigned CheckFileBufferID =
      SM.AddNewSourceBuffer(MemoryBuffer::getMemBuffer(
                                CheckFileText, CheckFile.getBufferIdentifier()),
                            SMLoc());

  std::pair<unsigned, unsigned> ImpPatBufferIDRange;
  if (FC.readCheckFile(SM, CheckFileText, &ImpPatBufferIDRange))
    return 2;

  // Open the file to check and add it to SourceMgr.
  ErrorOr<std::unique_ptr<MemoryBuffer>> InputFileOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (InputFilename == "-")
    InputFilename = "<stdin>"; // Overwrite for improved diagnostic messages
  if (std::error_code EC = InputFileOrErr.getError()) {
    errs() << "Could not open input file '" << InputFilename
           << "': " << EC.message() << '\n';
    return 2;
  }
  MemoryBuffer &InputFile = *InputFileOrErr.get();

  if (InputFile.getBufferSize() == 0 && !AllowEmptyInput) {
    errs() << "FileCheck error: '" << InputFilename << "' is empty.\n";
    DumpCommandLine(argc, argv);
    return 2;
  }

  SmallString<4096> InputFileBuffer;
  StringRef InputFileText = FC.CanonicalizeFile(InputFile, InputFileBuffer);

  SM.AddNewSourceBuffer(MemoryBuffer::getMemBuffer(
                            InputFileText, InputFile.getBufferIdentifier()),
                        SMLoc());

  std::vector<FileCheckDiag> Diags;
  int ExitCode = FC.checkInput(SM, InputFileText,
                               DumpInput == DumpInputNever ? nullptr : &Diags)
                     ? EXIT_SUCCESS
                     : 1;
  if (DumpInput == DumpInputAlways ||
      (ExitCode == 1 && DumpInput == DumpInputFail)) {
    errs() << "\n"
           << "Input file: " << InputFilename << "\n"
           << "Check file: " << CheckFilename << "\n"
           << "\n"
           << "-dump-input=help explains the following input dump.\n"
           << "\n";
    std::vector<InputAnnotation> Annotations;
    unsigned LabelWidth;
    BuildInputAnnotations(SM, CheckFileBufferID, ImpPatBufferIDRange, Diags,
                          Annotations, LabelWidth);
    DumpAnnotatedInput(errs(), Req, DumpInputFilter, DumpInputContext,
                       InputFileText, Annotations, LabelWidth);
  }

  return ExitCode;
}
