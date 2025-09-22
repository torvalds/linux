//===- VerifyDiagnosticConsumer.cpp - Verifying Diagnostic Client ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a concrete diagnostic client, which buffers the diagnostic messages.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/VerifyDiagnosticConsumer.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace clang;

using Directive = VerifyDiagnosticConsumer::Directive;
using DirectiveList = VerifyDiagnosticConsumer::DirectiveList;
using ExpectedData = VerifyDiagnosticConsumer::ExpectedData;

#ifndef NDEBUG

namespace {

class VerifyFileTracker : public PPCallbacks {
  VerifyDiagnosticConsumer &Verify;
  SourceManager &SM;

public:
  VerifyFileTracker(VerifyDiagnosticConsumer &Verify, SourceManager &SM)
      : Verify(Verify), SM(SM) {}

  /// Hook into the preprocessor and update the list of parsed
  /// files when the preprocessor indicates a new file is entered.
  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override {
    Verify.UpdateParsedFileStatus(SM, SM.getFileID(Loc),
                                  VerifyDiagnosticConsumer::IsParsed);
  }
};

} // namespace

#endif

//===----------------------------------------------------------------------===//
// Checking diagnostics implementation.
//===----------------------------------------------------------------------===//

using DiagList = TextDiagnosticBuffer::DiagList;
using const_diag_iterator = TextDiagnosticBuffer::const_iterator;

namespace {

/// StandardDirective - Directive with string matching.
class StandardDirective : public Directive {
public:
  StandardDirective(SourceLocation DirectiveLoc, SourceLocation DiagnosticLoc,
                    bool MatchAnyFileAndLine, bool MatchAnyLine, StringRef Text,
                    unsigned Min, unsigned Max)
      : Directive(DirectiveLoc, DiagnosticLoc, MatchAnyFileAndLine,
                  MatchAnyLine, Text, Min, Max) {}

  bool isValid(std::string &Error) override {
    // all strings are considered valid; even empty ones
    return true;
  }

  bool match(StringRef S) override { return S.contains(Text); }
};

/// RegexDirective - Directive with regular-expression matching.
class RegexDirective : public Directive {
public:
  RegexDirective(SourceLocation DirectiveLoc, SourceLocation DiagnosticLoc,
                 bool MatchAnyFileAndLine, bool MatchAnyLine, StringRef Text,
                 unsigned Min, unsigned Max, StringRef RegexStr)
      : Directive(DirectiveLoc, DiagnosticLoc, MatchAnyFileAndLine,
                  MatchAnyLine, Text, Min, Max),
        Regex(RegexStr) {}

  bool isValid(std::string &Error) override {
    return Regex.isValid(Error);
  }

  bool match(StringRef S) override {
    return Regex.match(S);
  }

private:
  llvm::Regex Regex;
};

class ParseHelper
{
public:
  ParseHelper(StringRef S)
      : Begin(S.begin()), End(S.end()), C(Begin), P(Begin) {}

  // Return true if string literal is next.
  bool Next(StringRef S) {
    P = C;
    PEnd = C + S.size();
    if (PEnd > End)
      return false;
    return memcmp(P, S.data(), S.size()) == 0;
  }

  // Return true if number is next.
  // Output N only if number is next.
  bool Next(unsigned &N) {
    unsigned TMP = 0;
    P = C;
    PEnd = P;
    for (; PEnd < End && *PEnd >= '0' && *PEnd <= '9'; ++PEnd) {
      TMP *= 10;
      TMP += *PEnd - '0';
    }
    if (PEnd == C)
      return false;
    N = TMP;
    return true;
  }

  // Return true if a marker is next.
  // A marker is the longest match for /#[A-Za-z0-9_-]+/.
  bool NextMarker() {
    P = C;
    if (P == End || *P != '#')
      return false;
    PEnd = P;
    ++PEnd;
    while ((isAlphanumeric(*PEnd) || *PEnd == '-' || *PEnd == '_') &&
           PEnd < End)
      ++PEnd;
    return PEnd > P + 1;
  }

  // Return true if string literal S is matched in content.
  // When true, P marks begin-position of the match, and calling Advance sets C
  // to end-position of the match.
  // If S is the empty string, then search for any letter instead (makes sense
  // with FinishDirectiveToken=true).
  // If EnsureStartOfWord, then skip matches that don't start a new word.
  // If FinishDirectiveToken, then assume the match is the start of a comment
  // directive for -verify, and extend the match to include the entire first
  // token of that directive.
  bool Search(StringRef S, bool EnsureStartOfWord = false,
              bool FinishDirectiveToken = false) {
    do {
      if (!S.empty()) {
        P = std::search(C, End, S.begin(), S.end());
        PEnd = P + S.size();
      }
      else {
        P = C;
        while (P != End && !isLetter(*P))
          ++P;
        PEnd = P + 1;
      }
      if (P == End)
        break;
      // If not start of word but required, skip and search again.
      if (EnsureStartOfWord
               // Check if string literal starts a new word.
          && !(P == Begin || isWhitespace(P[-1])
               // Or it could be preceded by the start of a comment.
               || (P > (Begin + 1) && (P[-1] == '/' || P[-1] == '*')
                                   &&  P[-2] == '/')))
        continue;
      if (FinishDirectiveToken) {
        while (PEnd != End && (isAlphanumeric(*PEnd)
                               || *PEnd == '-' || *PEnd == '_'))
          ++PEnd;
        // Put back trailing digits and hyphens to be parsed later as a count
        // or count range.  Because -verify prefixes must start with letters,
        // we know the actual directive we found starts with a letter, so
        // we won't put back the entire directive word and thus record an empty
        // string.
        assert(isLetter(*P) && "-verify prefix must start with a letter");
        while (isDigit(PEnd[-1]) || PEnd[-1] == '-')
          --PEnd;
      }
      return true;
    } while (Advance());
    return false;
  }

  // Return true if a CloseBrace that closes the OpenBrace at the current nest
  // level is found. When true, P marks begin-position of CloseBrace.
  bool SearchClosingBrace(StringRef OpenBrace, StringRef CloseBrace) {
    unsigned Depth = 1;
    P = C;
    while (P < End) {
      StringRef S(P, End - P);
      if (S.starts_with(OpenBrace)) {
        ++Depth;
        P += OpenBrace.size();
      } else if (S.starts_with(CloseBrace)) {
        --Depth;
        if (Depth == 0) {
          PEnd = P + CloseBrace.size();
          return true;
        }
        P += CloseBrace.size();
      } else {
        ++P;
      }
    }
    return false;
  }

  // Advance 1-past previous next/search.
  // Behavior is undefined if previous next/search failed.
  bool Advance() {
    C = PEnd;
    return C < End;
  }

  // Return the text matched by the previous next/search.
  // Behavior is undefined if previous next/search failed.
  StringRef Match() { return StringRef(P, PEnd - P); }

  // Skip zero or more whitespace.
  void SkipWhitespace() {
    for (; C < End && isWhitespace(*C); ++C)
      ;
  }

  // Return true if EOF reached.
  bool Done() {
    return !(C < End);
  }

  // Beginning of expected content.
  const char * const Begin;

  // End of expected content (1-past).
  const char * const End;

  // Position of next char in content.
  const char *C;

  // Previous next/search subject start.
  const char *P;

private:
  // Previous next/search subject end (1-past).
  const char *PEnd = nullptr;
};

// The information necessary to create a directive.
struct UnattachedDirective {
  DirectiveList *DL = nullptr;
  bool RegexKind = false;
  SourceLocation DirectivePos, ContentBegin;
  std::string Text;
  unsigned Min = 1, Max = 1;
};

// Attach the specified directive to the line of code indicated by
// \p ExpectedLoc.
void attachDirective(DiagnosticsEngine &Diags, const UnattachedDirective &UD,
                     SourceLocation ExpectedLoc,
                     bool MatchAnyFileAndLine = false,
                     bool MatchAnyLine = false) {
  // Construct new directive.
  std::unique_ptr<Directive> D = Directive::create(
      UD.RegexKind, UD.DirectivePos, ExpectedLoc, MatchAnyFileAndLine,
      MatchAnyLine, UD.Text, UD.Min, UD.Max);

  std::string Error;
  if (!D->isValid(Error)) {
    Diags.Report(UD.ContentBegin, diag::err_verify_invalid_content)
      << (UD.RegexKind ? "regex" : "string") << Error;
  }

  UD.DL->push_back(std::move(D));
}

} // anonymous

// Tracker for markers in the input files. A marker is a comment of the form
//
//   n = 123; // #123
//
// ... that can be referred to by a later expected-* directive:
//
//   // expected-error@#123 {{undeclared identifier 'n'}}
//
// Marker declarations must be at the start of a comment or preceded by
// whitespace to distinguish them from uses of markers in directives.
class VerifyDiagnosticConsumer::MarkerTracker {
  DiagnosticsEngine &Diags;

  struct Marker {
    SourceLocation DefLoc;
    SourceLocation RedefLoc;
    SourceLocation UseLoc;
  };
  llvm::StringMap<Marker> Markers;

  // Directives that couldn't be created yet because they name an unknown
  // marker.
  llvm::StringMap<llvm::SmallVector<UnattachedDirective, 2>> DeferredDirectives;

public:
  MarkerTracker(DiagnosticsEngine &Diags) : Diags(Diags) {}

  // Register a marker.
  void addMarker(StringRef MarkerName, SourceLocation Pos) {
    auto InsertResult = Markers.insert(
        {MarkerName, Marker{Pos, SourceLocation(), SourceLocation()}});

    Marker &M = InsertResult.first->second;
    if (!InsertResult.second) {
      // Marker was redefined.
      M.RedefLoc = Pos;
    } else {
      // First definition: build any deferred directives.
      auto Deferred = DeferredDirectives.find(MarkerName);
      if (Deferred != DeferredDirectives.end()) {
        for (auto &UD : Deferred->second) {
          if (M.UseLoc.isInvalid())
            M.UseLoc = UD.DirectivePos;
          attachDirective(Diags, UD, Pos);
        }
        DeferredDirectives.erase(Deferred);
      }
    }
  }

  // Register a directive at the specified marker.
  void addDirective(StringRef MarkerName, const UnattachedDirective &UD) {
    auto MarkerIt = Markers.find(MarkerName);
    if (MarkerIt != Markers.end()) {
      Marker &M = MarkerIt->second;
      if (M.UseLoc.isInvalid())
        M.UseLoc = UD.DirectivePos;
      return attachDirective(Diags, UD, M.DefLoc);
    }
    DeferredDirectives[MarkerName].push_back(UD);
  }

  // Ensure we have no remaining deferred directives, and no
  // multiply-defined-and-used markers.
  void finalize() {
    for (auto &MarkerInfo : Markers) {
      StringRef Name = MarkerInfo.first();
      Marker &M = MarkerInfo.second;
      if (M.RedefLoc.isValid() && M.UseLoc.isValid()) {
        Diags.Report(M.UseLoc, diag::err_verify_ambiguous_marker) << Name;
        Diags.Report(M.DefLoc, diag::note_verify_ambiguous_marker) << Name;
        Diags.Report(M.RedefLoc, diag::note_verify_ambiguous_marker) << Name;
      }
    }

    for (auto &DeferredPair : DeferredDirectives) {
      Diags.Report(DeferredPair.second.front().DirectivePos,
                   diag::err_verify_no_such_marker)
          << DeferredPair.first();
    }
  }
};

static std::string DetailedErrorString(const DiagnosticsEngine &Diags) {
  if (Diags.getDiagnosticOptions().VerifyPrefixes.empty())
    return "expected";
  return *Diags.getDiagnosticOptions().VerifyPrefixes.begin();
}

/// ParseDirective - Go through the comment and see if it indicates expected
/// diagnostics. If so, then put them in the appropriate directive list.
///
/// Returns true if any valid directives were found.
static bool ParseDirective(StringRef S, ExpectedData *ED, SourceManager &SM,
                           Preprocessor *PP, SourceLocation Pos,
                           VerifyDiagnosticConsumer::DirectiveStatus &Status,
                           VerifyDiagnosticConsumer::MarkerTracker &Markers) {
  DiagnosticsEngine &Diags = PP ? PP->getDiagnostics() : SM.getDiagnostics();

  // First, scan the comment looking for markers.
  for (ParseHelper PH(S); !PH.Done();) {
    if (!PH.Search("#", true))
      break;
    PH.C = PH.P;
    if (!PH.NextMarker()) {
      PH.Next("#");
      PH.Advance();
      continue;
    }
    PH.Advance();
    Markers.addMarker(PH.Match(), Pos);
  }

  // A single comment may contain multiple directives.
  bool FoundDirective = false;
  for (ParseHelper PH(S); !PH.Done();) {
    // Search for the initial directive token.
    // If one prefix, save time by searching only for its directives.
    // Otherwise, search for any potential directive token and check it later.
    const auto &Prefixes = Diags.getDiagnosticOptions().VerifyPrefixes;
    if (!(Prefixes.size() == 1 ? PH.Search(*Prefixes.begin(), true, true)
                               : PH.Search("", true, true)))
      break;

    StringRef DToken = PH.Match();
    PH.Advance();

    // Default directive kind.
    UnattachedDirective D;
    const char *KindStr = "string";

    // Parse the initial directive token in reverse so we can easily determine
    // its exact actual prefix.  If we were to parse it from the front instead,
    // it would be harder to determine where the prefix ends because there
    // might be multiple matching -verify prefixes because some might prefix
    // others.

    // Regex in initial directive token: -re
    if (DToken.consume_back("-re")) {
      D.RegexKind = true;
      KindStr = "regex";
    }

    // Type in initial directive token: -{error|warning|note|no-diagnostics}
    bool NoDiag = false;
    StringRef DType;
    if (DToken.ends_with(DType = "-error"))
      D.DL = ED ? &ED->Errors : nullptr;
    else if (DToken.ends_with(DType = "-warning"))
      D.DL = ED ? &ED->Warnings : nullptr;
    else if (DToken.ends_with(DType = "-remark"))
      D.DL = ED ? &ED->Remarks : nullptr;
    else if (DToken.ends_with(DType = "-note"))
      D.DL = ED ? &ED->Notes : nullptr;
    else if (DToken.ends_with(DType = "-no-diagnostics")) {
      NoDiag = true;
      if (D.RegexKind)
        continue;
    } else
      continue;
    DToken = DToken.substr(0, DToken.size()-DType.size());

    // What's left in DToken is the actual prefix.  That might not be a -verify
    // prefix even if there is only one -verify prefix (for example, the full
    // DToken is foo-bar-warning, but foo is the only -verify prefix).
    if (!std::binary_search(Prefixes.begin(), Prefixes.end(), DToken))
      continue;

    if (NoDiag) {
      if (Status == VerifyDiagnosticConsumer::HasOtherExpectedDirectives)
        Diags.Report(Pos, diag::err_verify_invalid_no_diags)
            << DetailedErrorString(Diags) << /*IsExpectedNoDiagnostics=*/true;
      else
        Status = VerifyDiagnosticConsumer::HasExpectedNoDiagnostics;
      continue;
    }
    if (Status == VerifyDiagnosticConsumer::HasExpectedNoDiagnostics) {
      Diags.Report(Pos, diag::err_verify_invalid_no_diags)
          << DetailedErrorString(Diags) << /*IsExpectedNoDiagnostics=*/false;
      continue;
    }
    Status = VerifyDiagnosticConsumer::HasOtherExpectedDirectives;

    // If a directive has been found but we're not interested
    // in storing the directive information, return now.
    if (!D.DL)
      return true;

    // Next optional token: @
    SourceLocation ExpectedLoc;
    StringRef Marker;
    bool MatchAnyFileAndLine = false;
    bool MatchAnyLine = false;
    if (!PH.Next("@")) {
      ExpectedLoc = Pos;
    } else {
      PH.Advance();
      unsigned Line = 0;
      bool FoundPlus = PH.Next("+");
      if (FoundPlus || PH.Next("-")) {
        // Relative to current line.
        PH.Advance();
        bool Invalid = false;
        unsigned ExpectedLine = SM.getSpellingLineNumber(Pos, &Invalid);
        if (!Invalid && PH.Next(Line) && (FoundPlus || Line < ExpectedLine)) {
          if (FoundPlus) ExpectedLine += Line;
          else ExpectedLine -= Line;
          ExpectedLoc = SM.translateLineCol(SM.getFileID(Pos), ExpectedLine, 1);
        }
      } else if (PH.Next(Line)) {
        // Absolute line number.
        if (Line > 0)
          ExpectedLoc = SM.translateLineCol(SM.getFileID(Pos), Line, 1);
      } else if (PH.NextMarker()) {
        Marker = PH.Match();
      } else if (PP && PH.Search(":")) {
        // Specific source file.
        StringRef Filename(PH.C, PH.P-PH.C);
        PH.Advance();

        if (Filename == "*") {
          MatchAnyFileAndLine = true;
          if (!PH.Next("*")) {
            Diags.Report(Pos.getLocWithOffset(PH.C - PH.Begin),
                         diag::err_verify_missing_line)
                << "'*'";
            continue;
          }
          MatchAnyLine = true;
          ExpectedLoc = SourceLocation();
        } else {
          // Lookup file via Preprocessor, like a #include.
          OptionalFileEntryRef File =
              PP->LookupFile(Pos, Filename, false, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr, nullptr);
          if (!File) {
            Diags.Report(Pos.getLocWithOffset(PH.C - PH.Begin),
                         diag::err_verify_missing_file)
                << Filename << KindStr;
            continue;
          }

          FileID FID = SM.translateFile(*File);
          if (FID.isInvalid())
            FID = SM.createFileID(*File, Pos, SrcMgr::C_User);

          if (PH.Next(Line) && Line > 0)
            ExpectedLoc = SM.translateLineCol(FID, Line, 1);
          else if (PH.Next("*")) {
            MatchAnyLine = true;
            ExpectedLoc = SM.translateLineCol(FID, 1, 1);
          }
        }
      } else if (PH.Next("*")) {
        MatchAnyLine = true;
        ExpectedLoc = SourceLocation();
      }

      if (ExpectedLoc.isInvalid() && !MatchAnyLine && Marker.empty()) {
        Diags.Report(Pos.getLocWithOffset(PH.C-PH.Begin),
                     diag::err_verify_missing_line) << KindStr;
        continue;
      }
      PH.Advance();
    }

    // Skip optional whitespace.
    PH.SkipWhitespace();

    // Next optional token: positive integer or a '+'.
    if (PH.Next(D.Min)) {
      PH.Advance();
      // A positive integer can be followed by a '+' meaning min
      // or more, or by a '-' meaning a range from min to max.
      if (PH.Next("+")) {
        D.Max = Directive::MaxCount;
        PH.Advance();
      } else if (PH.Next("-")) {
        PH.Advance();
        if (!PH.Next(D.Max) || D.Max < D.Min) {
          Diags.Report(Pos.getLocWithOffset(PH.C-PH.Begin),
                       diag::err_verify_invalid_range) << KindStr;
          continue;
        }
        PH.Advance();
      } else {
        D.Max = D.Min;
      }
    } else if (PH.Next("+")) {
      // '+' on its own means "1 or more".
      D.Max = Directive::MaxCount;
      PH.Advance();
    }

    // Skip optional whitespace.
    PH.SkipWhitespace();

    // Next token: {{
    if (!PH.Next("{{")) {
      Diags.Report(Pos.getLocWithOffset(PH.C-PH.Begin),
                   diag::err_verify_missing_start) << KindStr;
      continue;
    }
    llvm::SmallString<8> CloseBrace("}}");
    const char *const DelimBegin = PH.C;
    PH.Advance();
    // Count the number of opening braces for `string` kinds
    for (; !D.RegexKind && PH.Next("{"); PH.Advance())
      CloseBrace += '}';
    const char* const ContentBegin = PH.C; // mark content begin
    // Search for closing brace
    StringRef OpenBrace(DelimBegin, ContentBegin - DelimBegin);
    if (!PH.SearchClosingBrace(OpenBrace, CloseBrace)) {
      Diags.Report(Pos.getLocWithOffset(PH.C - PH.Begin),
                   diag::err_verify_missing_end)
          << KindStr << CloseBrace;
      continue;
    }
    const char* const ContentEnd = PH.P; // mark content end
    PH.Advance();

    D.DirectivePos = Pos;
    D.ContentBegin = Pos.getLocWithOffset(ContentBegin - PH.Begin);

    // Build directive text; convert \n to newlines.
    StringRef NewlineStr = "\\n";
    StringRef Content(ContentBegin, ContentEnd-ContentBegin);
    size_t CPos = 0;
    size_t FPos;
    while ((FPos = Content.find(NewlineStr, CPos)) != StringRef::npos) {
      D.Text += Content.substr(CPos, FPos-CPos);
      D.Text += '\n';
      CPos = FPos + NewlineStr.size();
    }
    if (D.Text.empty())
      D.Text.assign(ContentBegin, ContentEnd);

    // Check that regex directives contain at least one regex.
    if (D.RegexKind && D.Text.find("{{") == StringRef::npos) {
      Diags.Report(D.ContentBegin, diag::err_verify_missing_regex) << D.Text;
      return false;
    }

    if (Marker.empty())
      attachDirective(Diags, D, ExpectedLoc, MatchAnyFileAndLine, MatchAnyLine);
    else
      Markers.addDirective(Marker, D);
    FoundDirective = true;
  }

  return FoundDirective;
}

VerifyDiagnosticConsumer::VerifyDiagnosticConsumer(DiagnosticsEngine &Diags_)
    : Diags(Diags_), PrimaryClient(Diags.getClient()),
      PrimaryClientOwner(Diags.takeClient()),
      Buffer(new TextDiagnosticBuffer()), Markers(new MarkerTracker(Diags)),
      Status(HasNoDirectives) {
  if (Diags.hasSourceManager())
    setSourceManager(Diags.getSourceManager());
}

VerifyDiagnosticConsumer::~VerifyDiagnosticConsumer() {
  assert(!ActiveSourceFiles && "Incomplete parsing of source files!");
  assert(!CurrentPreprocessor && "CurrentPreprocessor should be invalid!");
  SrcManager = nullptr;
  CheckDiagnostics();
  assert(!Diags.ownsClient() &&
         "The VerifyDiagnosticConsumer takes over ownership of the client!");
}

// DiagnosticConsumer interface.

void VerifyDiagnosticConsumer::BeginSourceFile(const LangOptions &LangOpts,
                                               const Preprocessor *PP) {
  // Attach comment handler on first invocation.
  if (++ActiveSourceFiles == 1) {
    if (PP) {
      CurrentPreprocessor = PP;
      this->LangOpts = &LangOpts;
      setSourceManager(PP->getSourceManager());
      const_cast<Preprocessor *>(PP)->addCommentHandler(this);
#ifndef NDEBUG
      // Debug build tracks parsed files.
      const_cast<Preprocessor *>(PP)->addPPCallbacks(
                      std::make_unique<VerifyFileTracker>(*this, *SrcManager));
#endif
    }
  }

  assert((!PP || CurrentPreprocessor == PP) && "Preprocessor changed!");
  PrimaryClient->BeginSourceFile(LangOpts, PP);
}

void VerifyDiagnosticConsumer::EndSourceFile() {
  assert(ActiveSourceFiles && "No active source files!");
  PrimaryClient->EndSourceFile();

  // Detach comment handler once last active source file completed.
  if (--ActiveSourceFiles == 0) {
    if (CurrentPreprocessor)
      const_cast<Preprocessor *>(CurrentPreprocessor)->
          removeCommentHandler(this);

    // Diagnose any used-but-not-defined markers.
    Markers->finalize();

    // Check diagnostics once last file completed.
    CheckDiagnostics();
    CurrentPreprocessor = nullptr;
    LangOpts = nullptr;
  }
}

void VerifyDiagnosticConsumer::HandleDiagnostic(
      DiagnosticsEngine::Level DiagLevel, const Diagnostic &Info) {
  if (Info.hasSourceManager()) {
    // If this diagnostic is for a different source manager, ignore it.
    if (SrcManager && &Info.getSourceManager() != SrcManager)
      return;

    setSourceManager(Info.getSourceManager());
  }

#ifndef NDEBUG
  // Debug build tracks unparsed files for possible
  // unparsed expected-* directives.
  if (SrcManager) {
    SourceLocation Loc = Info.getLocation();
    if (Loc.isValid()) {
      ParsedStatus PS = IsUnparsed;

      Loc = SrcManager->getExpansionLoc(Loc);
      FileID FID = SrcManager->getFileID(Loc);

      auto FE = SrcManager->getFileEntryRefForID(FID);
      if (FE && CurrentPreprocessor && SrcManager->isLoadedFileID(FID)) {
        // If the file is a modules header file it shall not be parsed
        // for expected-* directives.
        HeaderSearch &HS = CurrentPreprocessor->getHeaderSearchInfo();
        if (HS.findModuleForHeader(*FE))
          PS = IsUnparsedNoDirectives;
      }

      UpdateParsedFileStatus(*SrcManager, FID, PS);
    }
  }
#endif

  // Send the diagnostic to the buffer, we will check it once we reach the end
  // of the source file (or are destructed).
  Buffer->HandleDiagnostic(DiagLevel, Info);
}

/// HandleComment - Hook into the preprocessor and extract comments containing
///  expected errors and warnings.
bool VerifyDiagnosticConsumer::HandleComment(Preprocessor &PP,
                                             SourceRange Comment) {
  SourceManager &SM = PP.getSourceManager();

  // If this comment is for a different source manager, ignore it.
  if (SrcManager && &SM != SrcManager)
    return false;

  SourceLocation CommentBegin = Comment.getBegin();

  const char *CommentRaw = SM.getCharacterData(CommentBegin);
  StringRef C(CommentRaw, SM.getCharacterData(Comment.getEnd()) - CommentRaw);

  if (C.empty())
    return false;

  // Fold any "\<EOL>" sequences
  size_t loc = C.find('\\');
  if (loc == StringRef::npos) {
    ParseDirective(C, &ED, SM, &PP, CommentBegin, Status, *Markers);
    return false;
  }

  std::string C2;
  C2.reserve(C.size());

  for (size_t last = 0;; loc = C.find('\\', last)) {
    if (loc == StringRef::npos || loc == C.size()) {
      C2 += C.substr(last);
      break;
    }
    C2 += C.substr(last, loc-last);
    last = loc + 1;

    if (C[last] == '\n' || C[last] == '\r') {
      ++last;

      // Escape \r\n  or \n\r, but not \n\n.
      if (last < C.size())
        if (C[last] == '\n' || C[last] == '\r')
          if (C[last] != C[last-1])
            ++last;
    } else {
      // This was just a normal backslash.
      C2 += '\\';
    }
  }

  if (!C2.empty())
    ParseDirective(C2, &ED, SM, &PP, CommentBegin, Status, *Markers);
  return false;
}

#ifndef NDEBUG
/// Lex the specified source file to determine whether it contains
/// any expected-* directives.  As a Lexer is used rather than a full-blown
/// Preprocessor, directives inside skipped #if blocks will still be found.
///
/// \return true if any directives were found.
static bool findDirectives(SourceManager &SM, FileID FID,
                           const LangOptions &LangOpts) {
  // Create a raw lexer to pull all the comments out of FID.
  if (FID.isInvalid())
    return false;

  // Create a lexer to lex all the tokens of the main file in raw mode.
  llvm::MemoryBufferRef FromFile = SM.getBufferOrFake(FID);
  Lexer RawLex(FID, FromFile, SM, LangOpts);

  // Return comments as tokens, this is how we find expected diagnostics.
  RawLex.SetCommentRetentionState(true);

  Token Tok;
  Tok.setKind(tok::comment);
  VerifyDiagnosticConsumer::DirectiveStatus Status =
    VerifyDiagnosticConsumer::HasNoDirectives;
  while (Tok.isNot(tok::eof)) {
    RawLex.LexFromRawLexer(Tok);
    if (!Tok.is(tok::comment)) continue;

    std::string Comment = RawLex.getSpelling(Tok, SM, LangOpts);
    if (Comment.empty()) continue;

    // We don't care about tracking markers for this phase.
    VerifyDiagnosticConsumer::MarkerTracker Markers(SM.getDiagnostics());

    // Find first directive.
    if (ParseDirective(Comment, nullptr, SM, nullptr, Tok.getLocation(),
                       Status, Markers))
      return true;
  }
  return false;
}
#endif // !NDEBUG

/// Takes a list of diagnostics that have been generated but not matched
/// by an expected-* directive and produces a diagnostic to the user from this.
static unsigned PrintUnexpected(DiagnosticsEngine &Diags, SourceManager *SourceMgr,
                                const_diag_iterator diag_begin,
                                const_diag_iterator diag_end,
                                const char *Kind) {
  if (diag_begin == diag_end) return 0;

  SmallString<256> Fmt;
  llvm::raw_svector_ostream OS(Fmt);
  for (const_diag_iterator I = diag_begin, E = diag_end; I != E; ++I) {
    if (I->first.isInvalid() || !SourceMgr)
      OS << "\n  (frontend)";
    else {
      OS << "\n ";
      if (OptionalFileEntryRef File =
              SourceMgr->getFileEntryRefForID(SourceMgr->getFileID(I->first)))
        OS << " File " << File->getName();
      OS << " Line " << SourceMgr->getPresumedLineNumber(I->first);
    }
    OS << ": " << I->second;
  }

  std::string Prefix = *Diags.getDiagnosticOptions().VerifyPrefixes.begin();
  std::string KindStr = Prefix + "-" + Kind;
  Diags.Report(diag::err_verify_inconsistent_diags).setForceEmit()
      << KindStr << /*Unexpected=*/true << OS.str();
  return std::distance(diag_begin, diag_end);
}

/// Takes a list of diagnostics that were expected to have been generated
/// but were not and produces a diagnostic to the user from this.
static unsigned PrintExpected(DiagnosticsEngine &Diags,
                              SourceManager &SourceMgr,
                              std::vector<Directive *> &DL, const char *Kind) {
  if (DL.empty())
    return 0;

  SmallString<256> Fmt;
  llvm::raw_svector_ostream OS(Fmt);
  for (const auto *D : DL) {
    if (D->DiagnosticLoc.isInvalid() || D->MatchAnyFileAndLine)
      OS << "\n  File *";
    else
      OS << "\n  File " << SourceMgr.getFilename(D->DiagnosticLoc);
    if (D->MatchAnyLine)
      OS << " Line *";
    else
      OS << " Line " << SourceMgr.getPresumedLineNumber(D->DiagnosticLoc);
    if (D->DirectiveLoc != D->DiagnosticLoc)
      OS << " (directive at "
         << SourceMgr.getFilename(D->DirectiveLoc) << ':'
         << SourceMgr.getPresumedLineNumber(D->DirectiveLoc) << ')';
    OS << ": " << D->Text;
  }

  std::string Prefix = *Diags.getDiagnosticOptions().VerifyPrefixes.begin();
  std::string KindStr = Prefix + "-" + Kind;
  Diags.Report(diag::err_verify_inconsistent_diags).setForceEmit()
      << KindStr << /*Unexpected=*/false << OS.str();
  return DL.size();
}

/// Determine whether two source locations come from the same file.
static bool IsFromSameFile(SourceManager &SM, SourceLocation DirectiveLoc,
                           SourceLocation DiagnosticLoc) {
  while (DiagnosticLoc.isMacroID())
    DiagnosticLoc = SM.getImmediateMacroCallerLoc(DiagnosticLoc);

  if (SM.isWrittenInSameFile(DirectiveLoc, DiagnosticLoc))
    return true;

  const FileEntry *DiagFile = SM.getFileEntryForID(SM.getFileID(DiagnosticLoc));
  if (!DiagFile && SM.isWrittenInMainFile(DirectiveLoc))
    return true;

  return (DiagFile == SM.getFileEntryForID(SM.getFileID(DirectiveLoc)));
}

/// CheckLists - Compare expected to seen diagnostic lists and return the
/// the difference between them.
static unsigned CheckLists(DiagnosticsEngine &Diags, SourceManager &SourceMgr,
                           const char *Label,
                           DirectiveList &Left,
                           const_diag_iterator d2_begin,
                           const_diag_iterator d2_end,
                           bool IgnoreUnexpected) {
  std::vector<Directive *> LeftOnly;
  DiagList Right(d2_begin, d2_end);

  for (auto &Owner : Left) {
    Directive &D = *Owner;
    unsigned LineNo1 = SourceMgr.getPresumedLineNumber(D.DiagnosticLoc);

    for (unsigned i = 0; i < D.Max; ++i) {
      DiagList::iterator II, IE;
      for (II = Right.begin(), IE = Right.end(); II != IE; ++II) {
        if (!D.MatchAnyLine) {
          unsigned LineNo2 = SourceMgr.getPresumedLineNumber(II->first);
          if (LineNo1 != LineNo2)
            continue;
        }

        if (!D.DiagnosticLoc.isInvalid() && !D.MatchAnyFileAndLine &&
            !IsFromSameFile(SourceMgr, D.DiagnosticLoc, II->first))
          continue;

        const std::string &RightText = II->second;
        if (D.match(RightText))
          break;
      }
      if (II == IE) {
        // Not found.
        if (i >= D.Min) break;
        LeftOnly.push_back(&D);
      } else {
        // Found. The same cannot be found twice.
        Right.erase(II);
      }
    }
  }
  // Now all that's left in Right are those that were not matched.
  unsigned num = PrintExpected(Diags, SourceMgr, LeftOnly, Label);
  if (!IgnoreUnexpected)
    num += PrintUnexpected(Diags, &SourceMgr, Right.begin(), Right.end(), Label);
  return num;
}

/// CheckResults - This compares the expected results to those that
/// were actually reported. It emits any discrepencies. Return "true" if there
/// were problems. Return "false" otherwise.
static unsigned CheckResults(DiagnosticsEngine &Diags, SourceManager &SourceMgr,
                             const TextDiagnosticBuffer &Buffer,
                             ExpectedData &ED) {
  // We want to capture the delta between what was expected and what was
  // seen.
  //
  //   Expected \ Seen - set expected but not seen
  //   Seen \ Expected - set seen but not expected
  unsigned NumProblems = 0;

  const DiagnosticLevelMask DiagMask =
    Diags.getDiagnosticOptions().getVerifyIgnoreUnexpected();

  // See if there are error mismatches.
  NumProblems += CheckLists(Diags, SourceMgr, "error", ED.Errors,
                            Buffer.err_begin(), Buffer.err_end(),
                            bool(DiagnosticLevelMask::Error & DiagMask));

  // See if there are warning mismatches.
  NumProblems += CheckLists(Diags, SourceMgr, "warning", ED.Warnings,
                            Buffer.warn_begin(), Buffer.warn_end(),
                            bool(DiagnosticLevelMask::Warning & DiagMask));

  // See if there are remark mismatches.
  NumProblems += CheckLists(Diags, SourceMgr, "remark", ED.Remarks,
                            Buffer.remark_begin(), Buffer.remark_end(),
                            bool(DiagnosticLevelMask::Remark & DiagMask));

  // See if there are note mismatches.
  NumProblems += CheckLists(Diags, SourceMgr, "note", ED.Notes,
                            Buffer.note_begin(), Buffer.note_end(),
                            bool(DiagnosticLevelMask::Note & DiagMask));

  return NumProblems;
}

void VerifyDiagnosticConsumer::UpdateParsedFileStatus(SourceManager &SM,
                                                      FileID FID,
                                                      ParsedStatus PS) {
  // Check SourceManager hasn't changed.
  setSourceManager(SM);

#ifndef NDEBUG
  if (FID.isInvalid())
    return;

  OptionalFileEntryRef FE = SM.getFileEntryRefForID(FID);

  if (PS == IsParsed) {
    // Move the FileID from the unparsed set to the parsed set.
    UnparsedFiles.erase(FID);
    ParsedFiles.insert(std::make_pair(FID, FE ? &FE->getFileEntry() : nullptr));
  } else if (!ParsedFiles.count(FID) && !UnparsedFiles.count(FID)) {
    // Add the FileID to the unparsed set if we haven't seen it before.

    // Check for directives.
    bool FoundDirectives;
    if (PS == IsUnparsedNoDirectives)
      FoundDirectives = false;
    else
      FoundDirectives = !LangOpts || findDirectives(SM, FID, *LangOpts);

    // Add the FileID to the unparsed set.
    UnparsedFiles.insert(std::make_pair(FID,
                                      UnparsedFileStatus(FE, FoundDirectives)));
  }
#endif
}

void VerifyDiagnosticConsumer::CheckDiagnostics() {
  // Ensure any diagnostics go to the primary client.
  DiagnosticConsumer *CurClient = Diags.getClient();
  std::unique_ptr<DiagnosticConsumer> Owner = Diags.takeClient();
  Diags.setClient(PrimaryClient, false);

#ifndef NDEBUG
  // In a debug build, scan through any files that may have been missed
  // during parsing and issue a fatal error if directives are contained
  // within these files.  If a fatal error occurs, this suggests that
  // this file is being parsed separately from the main file, in which
  // case consider moving the directives to the correct place, if this
  // is applicable.
  if (!UnparsedFiles.empty()) {
    // Generate a cache of parsed FileEntry pointers for alias lookups.
    llvm::SmallPtrSet<const FileEntry *, 8> ParsedFileCache;
    for (const auto &I : ParsedFiles)
      if (const FileEntry *FE = I.second)
        ParsedFileCache.insert(FE);

    // Iterate through list of unparsed files.
    for (const auto &I : UnparsedFiles) {
      const UnparsedFileStatus &Status = I.second;
      OptionalFileEntryRef FE = Status.getFile();

      // Skip files that have been parsed via an alias.
      if (FE && ParsedFileCache.count(*FE))
        continue;

      // Report a fatal error if this file contained directives.
      if (Status.foundDirectives()) {
        llvm::report_fatal_error("-verify directives found after rather"
                                 " than during normal parsing of " +
                                 (FE ? FE->getName() : "(unknown)"));
      }
    }

    // UnparsedFiles has been processed now, so clear it.
    UnparsedFiles.clear();
  }
#endif // !NDEBUG

  if (SrcManager) {
    // Produce an error if no expected-* directives could be found in the
    // source file(s) processed.
    if (Status == HasNoDirectives) {
      Diags.Report(diag::err_verify_no_directives).setForceEmit()
          << DetailedErrorString(Diags);
      ++NumErrors;
      Status = HasNoDirectivesReported;
    }

    // Check that the expected diagnostics occurred.
    NumErrors += CheckResults(Diags, *SrcManager, *Buffer, ED);
  } else {
    const DiagnosticLevelMask DiagMask =
        ~Diags.getDiagnosticOptions().getVerifyIgnoreUnexpected();
    if (bool(DiagnosticLevelMask::Error & DiagMask))
      NumErrors += PrintUnexpected(Diags, nullptr, Buffer->err_begin(),
                                   Buffer->err_end(), "error");
    if (bool(DiagnosticLevelMask::Warning & DiagMask))
      NumErrors += PrintUnexpected(Diags, nullptr, Buffer->warn_begin(),
                                   Buffer->warn_end(), "warn");
    if (bool(DiagnosticLevelMask::Remark & DiagMask))
      NumErrors += PrintUnexpected(Diags, nullptr, Buffer->remark_begin(),
                                   Buffer->remark_end(), "remark");
    if (bool(DiagnosticLevelMask::Note & DiagMask))
      NumErrors += PrintUnexpected(Diags, nullptr, Buffer->note_begin(),
                                   Buffer->note_end(), "note");
  }

  Diags.setClient(CurClient, Owner.release() != nullptr);

  // Reset the buffer, we have processed all the diagnostics in it.
  Buffer.reset(new TextDiagnosticBuffer());
  ED.Reset();
}

std::unique_ptr<Directive> Directive::create(bool RegexKind,
                                             SourceLocation DirectiveLoc,
                                             SourceLocation DiagnosticLoc,
                                             bool MatchAnyFileAndLine,
                                             bool MatchAnyLine, StringRef Text,
                                             unsigned Min, unsigned Max) {
  if (!RegexKind)
    return std::make_unique<StandardDirective>(DirectiveLoc, DiagnosticLoc,
                                               MatchAnyFileAndLine,
                                               MatchAnyLine, Text, Min, Max);

  // Parse the directive into a regular expression.
  std::string RegexStr;
  StringRef S = Text;
  while (!S.empty()) {
    if (S.consume_front("{{")) {
      size_t RegexMatchLength = S.find("}}");
      assert(RegexMatchLength != StringRef::npos);
      // Append the regex, enclosed in parentheses.
      RegexStr += "(";
      RegexStr.append(S.data(), RegexMatchLength);
      RegexStr += ")";
      S = S.drop_front(RegexMatchLength + 2);
    } else {
      size_t VerbatimMatchLength = S.find("{{");
      if (VerbatimMatchLength == StringRef::npos)
        VerbatimMatchLength = S.size();
      // Escape and append the fixed string.
      RegexStr += llvm::Regex::escape(S.substr(0, VerbatimMatchLength));
      S = S.drop_front(VerbatimMatchLength);
    }
  }

  return std::make_unique<RegexDirective>(DirectiveLoc, DiagnosticLoc,
                                          MatchAnyFileAndLine, MatchAnyLine,
                                          Text, Min, Max, RegexStr);
}
