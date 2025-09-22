//===--- PrintPreprocessedOutput.cpp - Implement the -E mode --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This code simply runs the preprocessor on the input file and prints out the
// result.  This is the traditional behavior of the -E option.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/TokenConcatenation.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
using namespace clang;

/// PrintMacroDefinition - Print a macro definition in a form that will be
/// properly accepted back as a definition.
static void PrintMacroDefinition(const IdentifierInfo &II, const MacroInfo &MI,
                                 Preprocessor &PP, raw_ostream *OS) {
  *OS << "#define " << II.getName();

  if (MI.isFunctionLike()) {
    *OS << '(';
    if (!MI.param_empty()) {
      MacroInfo::param_iterator AI = MI.param_begin(), E = MI.param_end();
      for (; AI+1 != E; ++AI) {
        *OS << (*AI)->getName();
        *OS << ',';
      }

      // Last argument.
      if ((*AI)->getName() == "__VA_ARGS__")
        *OS << "...";
      else
        *OS << (*AI)->getName();
    }

    if (MI.isGNUVarargs())
      *OS << "...";  // #define foo(x...)

    *OS << ')';
  }

  // GCC always emits a space, even if the macro body is empty.  However, do not
  // want to emit two spaces if the first token has a leading space.
  if (MI.tokens_empty() || !MI.tokens_begin()->hasLeadingSpace())
    *OS << ' ';

  SmallString<128> SpellingBuffer;
  for (const auto &T : MI.tokens()) {
    if (T.hasLeadingSpace())
      *OS << ' ';

    *OS << PP.getSpelling(T, SpellingBuffer);
  }
}

//===----------------------------------------------------------------------===//
// Preprocessed token printer
//===----------------------------------------------------------------------===//

namespace {
class PrintPPOutputPPCallbacks : public PPCallbacks {
  Preprocessor &PP;
  SourceManager &SM;
  TokenConcatenation ConcatInfo;
public:
  raw_ostream *OS;
private:
  unsigned CurLine;

  bool EmittedTokensOnThisLine;
  bool EmittedDirectiveOnThisLine;
  SrcMgr::CharacteristicKind FileType;
  SmallString<512> CurFilename;
  bool Initialized;
  bool DisableLineMarkers;
  bool DumpDefines;
  bool DumpIncludeDirectives;
  bool DumpEmbedDirectives;
  bool UseLineDirectives;
  bool IsFirstFileEntered;
  bool MinimizeWhitespace;
  bool DirectivesOnly;
  bool KeepSystemIncludes;
  raw_ostream *OrigOS;
  std::unique_ptr<llvm::raw_null_ostream> NullOS;
  unsigned NumToksToSkip;

  Token PrevTok;
  Token PrevPrevTok;

public:
  PrintPPOutputPPCallbacks(Preprocessor &pp, raw_ostream *os, bool lineMarkers,
                           bool defines, bool DumpIncludeDirectives,
                           bool DumpEmbedDirectives, bool UseLineDirectives,
                           bool MinimizeWhitespace, bool DirectivesOnly,
                           bool KeepSystemIncludes)
      : PP(pp), SM(PP.getSourceManager()), ConcatInfo(PP), OS(os),
        DisableLineMarkers(lineMarkers), DumpDefines(defines),
        DumpIncludeDirectives(DumpIncludeDirectives),
        DumpEmbedDirectives(DumpEmbedDirectives),
        UseLineDirectives(UseLineDirectives),
        MinimizeWhitespace(MinimizeWhitespace), DirectivesOnly(DirectivesOnly),
        KeepSystemIncludes(KeepSystemIncludes), OrigOS(os), NumToksToSkip(0) {
    CurLine = 0;
    CurFilename += "<uninit>";
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
    FileType = SrcMgr::C_User;
    Initialized = false;
    IsFirstFileEntered = false;
    if (KeepSystemIncludes)
      NullOS = std::make_unique<llvm::raw_null_ostream>();

    PrevTok.startToken();
    PrevPrevTok.startToken();
  }

  /// Returns true if #embed directives should be expanded into a comma-
  /// delimited list of integer constants or not.
  bool expandEmbedContents() const { return !DumpEmbedDirectives; }

  bool isMinimizeWhitespace() const { return MinimizeWhitespace; }

  void setEmittedTokensOnThisLine() { EmittedTokensOnThisLine = true; }
  bool hasEmittedTokensOnThisLine() const { return EmittedTokensOnThisLine; }

  void setEmittedDirectiveOnThisLine() { EmittedDirectiveOnThisLine = true; }
  bool hasEmittedDirectiveOnThisLine() const {
    return EmittedDirectiveOnThisLine;
  }

  /// Ensure that the output stream position is at the beginning of a new line
  /// and inserts one if it does not. It is intended to ensure that directives
  /// inserted by the directives not from the input source (such as #line) are
  /// in the first column. To insert newlines that represent the input, use
  /// MoveToLine(/*...*/, /*RequireStartOfLine=*/true).
  void startNewLineIfNeeded();

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;
  void EmbedDirective(SourceLocation HashLoc, StringRef FileName, bool IsAngled,
                      OptionalFileEntryRef File,
                      const LexEmbedParametersResult &Params) override;
  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, StringRef SearchPath,
                          StringRef RelativePath, const Module *SuggestedModule,
                          bool ModuleImported,
                          SrcMgr::CharacteristicKind FileType) override;
  void Ident(SourceLocation Loc, StringRef str) override;
  void PragmaMessage(SourceLocation Loc, StringRef Namespace,
                     PragmaMessageKind Kind, StringRef Str) override;
  void PragmaDebug(SourceLocation Loc, StringRef DebugType) override;
  void PragmaDiagnosticPush(SourceLocation Loc, StringRef Namespace) override;
  void PragmaDiagnosticPop(SourceLocation Loc, StringRef Namespace) override;
  void PragmaDiagnostic(SourceLocation Loc, StringRef Namespace,
                        diag::Severity Map, StringRef Str) override;
  void PragmaWarning(SourceLocation Loc, PragmaWarningSpecifier WarningSpec,
                     ArrayRef<int> Ids) override;
  void PragmaWarningPush(SourceLocation Loc, int Level) override;
  void PragmaWarningPop(SourceLocation Loc) override;
  void PragmaExecCharsetPush(SourceLocation Loc, StringRef Str) override;
  void PragmaExecCharsetPop(SourceLocation Loc) override;
  void PragmaAssumeNonNullBegin(SourceLocation Loc) override;
  void PragmaAssumeNonNullEnd(SourceLocation Loc) override;

  /// Insert whitespace before emitting the next token.
  ///
  /// @param Tok             Next token to be emitted.
  /// @param RequireSpace    Ensure at least one whitespace is emitted. Useful
  ///                        if non-tokens have been emitted to the stream.
  /// @param RequireSameLine Never emit newlines. Useful when semantics depend
  ///                        on being on the same line, such as directives.
  void HandleWhitespaceBeforeTok(const Token &Tok, bool RequireSpace,
                                 bool RequireSameLine);

  /// Move to the line of the provided source location. This will
  /// return true if a newline was inserted or if
  /// the requested location is the first token on the first line.
  /// In these cases the next output will be the first column on the line and
  /// make it possible to insert indention. The newline was inserted
  /// implicitly when at the beginning of the file.
  ///
  /// @param Tok                 Token where to move to.
  /// @param RequireStartOfLine  Whether the next line depends on being in the
  ///                            first column, such as a directive.
  ///
  /// @return Whether column adjustments are necessary.
  bool MoveToLine(const Token &Tok, bool RequireStartOfLine) {
    PresumedLoc PLoc = SM.getPresumedLoc(Tok.getLocation());
    unsigned TargetLine = PLoc.isValid() ? PLoc.getLine() : CurLine;
    bool IsFirstInFile =
        Tok.isAtStartOfLine() && PLoc.isValid() && PLoc.getLine() == 1;
    return MoveToLine(TargetLine, RequireStartOfLine) || IsFirstInFile;
  }

  /// Move to the line of the provided source location. Returns true if a new
  /// line was inserted.
  bool MoveToLine(SourceLocation Loc, bool RequireStartOfLine) {
    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    unsigned TargetLine = PLoc.isValid() ? PLoc.getLine() : CurLine;
    return MoveToLine(TargetLine, RequireStartOfLine);
  }
  bool MoveToLine(unsigned LineNo, bool RequireStartOfLine);

  bool AvoidConcat(const Token &PrevPrevTok, const Token &PrevTok,
                   const Token &Tok) {
    return ConcatInfo.AvoidConcat(PrevPrevTok, PrevTok, Tok);
  }
  void WriteLineInfo(unsigned LineNo, const char *Extra=nullptr,
                     unsigned ExtraLen=0);
  bool LineMarkersAreDisabled() const { return DisableLineMarkers; }
  void HandleNewlinesInToken(const char *TokStr, unsigned Len);

  /// MacroDefined - This hook is called whenever a macro definition is seen.
  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override;

  /// MacroUndefined - This hook is called whenever a macro #undef is seen.
  void MacroUndefined(const Token &MacroNameTok,
                      const MacroDefinition &MD,
                      const MacroDirective *Undef) override;

  void BeginModule(const Module *M);
  void EndModule(const Module *M);

  unsigned GetNumToksToSkip() const { return NumToksToSkip; }
  void ResetSkipToks() { NumToksToSkip = 0; }
};
}  // end anonymous namespace

void PrintPPOutputPPCallbacks::WriteLineInfo(unsigned LineNo,
                                             const char *Extra,
                                             unsigned ExtraLen) {
  startNewLineIfNeeded();

  // Emit #line directives or GNU line markers depending on what mode we're in.
  if (UseLineDirectives) {
    *OS << "#line" << ' ' << LineNo << ' ' << '"';
    OS->write_escaped(CurFilename);
    *OS << '"';
  } else {
    *OS << '#' << ' ' << LineNo << ' ' << '"';
    OS->write_escaped(CurFilename);
    *OS << '"';

    if (ExtraLen)
      OS->write(Extra, ExtraLen);

    if (FileType == SrcMgr::C_System)
      OS->write(" 3", 2);
    else if (FileType == SrcMgr::C_ExternCSystem)
      OS->write(" 3 4", 4);
  }
  *OS << '\n';
}

/// MoveToLine - Move the output to the source line specified by the location
/// object.  We can do this by emitting some number of \n's, or be emitting a
/// #line directive.  This returns false if already at the specified line, true
/// if some newlines were emitted.
bool PrintPPOutputPPCallbacks::MoveToLine(unsigned LineNo,
                                          bool RequireStartOfLine) {
  // If it is required to start a new line or finish the current, insert
  // vertical whitespace now and take it into account when moving to the
  // expected line.
  bool StartedNewLine = false;
  if ((RequireStartOfLine && EmittedTokensOnThisLine) ||
      EmittedDirectiveOnThisLine) {
    *OS << '\n';
    StartedNewLine = true;
    CurLine += 1;
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
  }

  // If this line is "close enough" to the original line, just print newlines,
  // otherwise print a #line directive.
  if (CurLine == LineNo) {
    // Nothing to do if we are already on the correct line.
  } else if (MinimizeWhitespace && DisableLineMarkers) {
    // With -E -P -fminimize-whitespace, don't emit anything if not necessary.
  } else if (!StartedNewLine && LineNo - CurLine == 1) {
    // Printing a single line has priority over printing a #line directive, even
    // when minimizing whitespace which otherwise would print #line directives
    // for every single line.
    *OS << '\n';
    StartedNewLine = true;
  } else if (!DisableLineMarkers) {
    if (LineNo - CurLine <= 8) {
      const char *NewLines = "\n\n\n\n\n\n\n\n";
      OS->write(NewLines, LineNo - CurLine);
    } else {
      // Emit a #line or line marker.
      WriteLineInfo(LineNo, nullptr, 0);
    }
    StartedNewLine = true;
  } else if (EmittedTokensOnThisLine) {
    // If we are not on the correct line and don't need to be line-correct,
    // at least ensure we start on a new line.
    *OS << '\n';
    StartedNewLine = true;
  }

  if (StartedNewLine) {
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
  }

  CurLine = LineNo;
  return StartedNewLine;
}

void PrintPPOutputPPCallbacks::startNewLineIfNeeded() {
  if (EmittedTokensOnThisLine || EmittedDirectiveOnThisLine) {
    *OS << '\n';
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
  }
}

/// FileChanged - Whenever the preprocessor enters or exits a #include file
/// it invokes this handler.  Update our conception of the current source
/// position.
void PrintPPOutputPPCallbacks::FileChanged(SourceLocation Loc,
                                           FileChangeReason Reason,
                                       SrcMgr::CharacteristicKind NewFileType,
                                       FileID PrevFID) {
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  SourceManager &SourceMgr = SM;

  PresumedLoc UserLoc = SourceMgr.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  unsigned NewLine = UserLoc.getLine();

  if (Reason == PPCallbacks::EnterFile) {
    SourceLocation IncludeLoc = UserLoc.getIncludeLoc();
    if (IncludeLoc.isValid())
      MoveToLine(IncludeLoc, /*RequireStartOfLine=*/false);
  } else if (Reason == PPCallbacks::SystemHeaderPragma) {
    // GCC emits the # directive for this directive on the line AFTER the
    // directive and emits a bunch of spaces that aren't needed. This is because
    // otherwise we will emit a line marker for THIS line, which requires an
    // extra blank line after the directive to avoid making all following lines
    // off by one. We can do better by simply incrementing NewLine here.
    NewLine += 1;
  }

  CurLine = NewLine;

  // In KeepSystemIncludes mode, redirect OS as needed.
  if (KeepSystemIncludes && (isSystem(FileType) != isSystem(NewFileType)))
    OS = isSystem(FileType) ? OrigOS : NullOS.get();

  CurFilename.clear();
  CurFilename += UserLoc.getFilename();
  FileType = NewFileType;

  if (DisableLineMarkers) {
    if (!MinimizeWhitespace)
      startNewLineIfNeeded();
    return;
  }

  if (!Initialized) {
    WriteLineInfo(CurLine);
    Initialized = true;
  }

  // Do not emit an enter marker for the main file (which we expect is the first
  // entered file). This matches gcc, and improves compatibility with some tools
  // which track the # line markers as a way to determine when the preprocessed
  // output is in the context of the main file.
  if (Reason == PPCallbacks::EnterFile && !IsFirstFileEntered) {
    IsFirstFileEntered = true;
    return;
  }

  switch (Reason) {
  case PPCallbacks::EnterFile:
    WriteLineInfo(CurLine, " 1", 2);
    break;
  case PPCallbacks::ExitFile:
    WriteLineInfo(CurLine, " 2", 2);
    break;
  case PPCallbacks::SystemHeaderPragma:
  case PPCallbacks::RenameFile:
    WriteLineInfo(CurLine);
    break;
  }
}

void PrintPPOutputPPCallbacks::EmbedDirective(
    SourceLocation HashLoc, StringRef FileName, bool IsAngled,
    OptionalFileEntryRef File, const LexEmbedParametersResult &Params) {
  if (!DumpEmbedDirectives)
    return;

  // The EmbedDirective() callback is called before we produce the annotation
  // token stream for the directive. We skip printing the annotation tokens
  // within PrintPreprocessedTokens(), but we also need to skip the prefix,
  // suffix, and if_empty tokens as those are inserted directly into the token
  // stream and would otherwise be printed immediately after printing the
  // #embed directive.
  //
  // FIXME: counting tokens to skip is a kludge but we have no way to know
  // which tokens were inserted as part of the embed and which ones were
  // explicitly written by the user.
  MoveToLine(HashLoc, /*RequireStartOfLine=*/true);
  *OS << "#embed " << (IsAngled ? '<' : '"') << FileName
      << (IsAngled ? '>' : '"');

  auto PrintToks = [&](llvm::ArrayRef<Token> Toks) {
    SmallString<128> SpellingBuffer;
    for (const Token &T : Toks) {
      if (T.hasLeadingSpace())
        *OS << " ";
      *OS << PP.getSpelling(T, SpellingBuffer);
    }
  };
  bool SkipAnnotToks = true;
  if (Params.MaybeIfEmptyParam) {
    *OS << " if_empty(";
    PrintToks(Params.MaybeIfEmptyParam->Tokens);
    *OS << ")";
    // If the file is empty, we can skip those tokens. If the file is not
    // empty, we skip the annotation tokens.
    if (File && !File->getSize()) {
      NumToksToSkip += Params.MaybeIfEmptyParam->Tokens.size();
      SkipAnnotToks = false;
    }
  }

  if (Params.MaybeLimitParam) {
    *OS << " limit(" << Params.MaybeLimitParam->Limit << ")";
  }
  if (Params.MaybeOffsetParam) {
    *OS << " clang::offset(" << Params.MaybeOffsetParam->Offset << ")";
  }
  if (Params.MaybePrefixParam) {
    *OS << " prefix(";
    PrintToks(Params.MaybePrefixParam->Tokens);
    *OS << ")";
    NumToksToSkip += Params.MaybePrefixParam->Tokens.size();
  }
  if (Params.MaybeSuffixParam) {
    *OS << " suffix(";
    PrintToks(Params.MaybeSuffixParam->Tokens);
    *OS << ")";
    NumToksToSkip += Params.MaybeSuffixParam->Tokens.size();
  }

  // We may need to skip the annotation token.
  if (SkipAnnotToks)
    NumToksToSkip++;

  *OS << " /* clang -E -dE */";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::InclusionDirective(
    SourceLocation HashLoc, const Token &IncludeTok, StringRef FileName,
    bool IsAngled, CharSourceRange FilenameRange, OptionalFileEntryRef File,
    StringRef SearchPath, StringRef RelativePath, const Module *SuggestedModule,
    bool ModuleImported, SrcMgr::CharacteristicKind FileType) {
  // In -dI mode, dump #include directives prior to dumping their content or
  // interpretation. Similar for -fkeep-system-includes.
  if (DumpIncludeDirectives || (KeepSystemIncludes && isSystem(FileType))) {
    MoveToLine(HashLoc, /*RequireStartOfLine=*/true);
    const std::string TokenText = PP.getSpelling(IncludeTok);
    assert(!TokenText.empty());
    *OS << "#" << TokenText << " "
        << (IsAngled ? '<' : '"') << FileName << (IsAngled ? '>' : '"')
        << " /* clang -E "
        << (DumpIncludeDirectives ? "-dI" : "-fkeep-system-includes")
        << " */";
    setEmittedDirectiveOnThisLine();
  }

  // When preprocessing, turn implicit imports into module import pragmas.
  if (ModuleImported) {
    switch (IncludeTok.getIdentifierInfo()->getPPKeywordID()) {
    case tok::pp_include:
    case tok::pp_import:
    case tok::pp_include_next:
      MoveToLine(HashLoc, /*RequireStartOfLine=*/true);
      *OS << "#pragma clang module import "
          << SuggestedModule->getFullModuleName(true)
          << " /* clang -E: implicit import for "
          << "#" << PP.getSpelling(IncludeTok) << " "
          << (IsAngled ? '<' : '"') << FileName << (IsAngled ? '>' : '"')
          << " */";
      setEmittedDirectiveOnThisLine();
      break;

    case tok::pp___include_macros:
      // #__include_macros has no effect on a user of a preprocessed source
      // file; the only effect is on preprocessing.
      //
      // FIXME: That's not *quite* true: it causes the module in question to
      // be loaded, which can affect downstream diagnostics.
      break;

    default:
      llvm_unreachable("unknown include directive kind");
      break;
    }
  }
}

/// Handle entering the scope of a module during a module compilation.
void PrintPPOutputPPCallbacks::BeginModule(const Module *M) {
  startNewLineIfNeeded();
  *OS << "#pragma clang module begin " << M->getFullModuleName(true);
  setEmittedDirectiveOnThisLine();
}

/// Handle leaving the scope of a module during a module compilation.
void PrintPPOutputPPCallbacks::EndModule(const Module *M) {
  startNewLineIfNeeded();
  *OS << "#pragma clang module end /*" << M->getFullModuleName(true) << "*/";
  setEmittedDirectiveOnThisLine();
}

/// Ident - Handle #ident directives when read by the preprocessor.
///
void PrintPPOutputPPCallbacks::Ident(SourceLocation Loc, StringRef S) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  OS->write("#ident ", strlen("#ident "));
  OS->write(S.begin(), S.size());
  setEmittedTokensOnThisLine();
}

/// MacroDefined - This hook is called whenever a macro definition is seen.
void PrintPPOutputPPCallbacks::MacroDefined(const Token &MacroNameTok,
                                            const MacroDirective *MD) {
  const MacroInfo *MI = MD->getMacroInfo();
  // Print out macro definitions in -dD mode and when we have -fdirectives-only
  // for C++20 header units.
  if ((!DumpDefines && !DirectivesOnly) ||
      // Ignore __FILE__ etc.
      MI->isBuiltinMacro())
    return;

  SourceLocation DefLoc = MI->getDefinitionLoc();
  if (DirectivesOnly && !MI->isUsed()) {
    SourceManager &SM = PP.getSourceManager();
    if (SM.isWrittenInBuiltinFile(DefLoc) ||
        SM.isWrittenInCommandLineFile(DefLoc))
      return;
  }
  MoveToLine(DefLoc, /*RequireStartOfLine=*/true);
  PrintMacroDefinition(*MacroNameTok.getIdentifierInfo(), *MI, PP, OS);
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::MacroUndefined(const Token &MacroNameTok,
                                              const MacroDefinition &MD,
                                              const MacroDirective *Undef) {
  // Print out macro definitions in -dD mode and when we have -fdirectives-only
  // for C++20 header units.
  if (!DumpDefines && !DirectivesOnly)
    return;

  MoveToLine(MacroNameTok.getLocation(), /*RequireStartOfLine=*/true);
  *OS << "#undef " << MacroNameTok.getIdentifierInfo()->getName();
  setEmittedDirectiveOnThisLine();
}

static void outputPrintable(raw_ostream *OS, StringRef Str) {
  for (unsigned char Char : Str) {
    if (isPrintable(Char) && Char != '\\' && Char != '"')
      *OS << (char)Char;
    else // Output anything hard as an octal escape.
      *OS << '\\'
          << (char)('0' + ((Char >> 6) & 7))
          << (char)('0' + ((Char >> 3) & 7))
          << (char)('0' + ((Char >> 0) & 7));
  }
}

void PrintPPOutputPPCallbacks::PragmaMessage(SourceLocation Loc,
                                             StringRef Namespace,
                                             PragmaMessageKind Kind,
                                             StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma ";
  if (!Namespace.empty())
    *OS << Namespace << ' ';
  switch (Kind) {
    case PMK_Message:
      *OS << "message(\"";
      break;
    case PMK_Warning:
      *OS << "warning \"";
      break;
    case PMK_Error:
      *OS << "error \"";
      break;
  }

  outputPrintable(OS, Str);
  *OS << '"';
  if (Kind == PMK_Message)
    *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaDebug(SourceLocation Loc,
                                           StringRef DebugType) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  *OS << "#pragma clang __debug ";
  *OS << DebugType;

  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaDiagnosticPush(SourceLocation Loc, StringRef Namespace) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic push";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaDiagnosticPop(SourceLocation Loc, StringRef Namespace) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic pop";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaDiagnostic(SourceLocation Loc,
                                                StringRef Namespace,
                                                diag::Severity Map,
                                                StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic ";
  switch (Map) {
  case diag::Severity::Remark:
    *OS << "remark";
    break;
  case diag::Severity::Warning:
    *OS << "warning";
    break;
  case diag::Severity::Error:
    *OS << "error";
    break;
  case diag::Severity::Ignored:
    *OS << "ignored";
    break;
  case diag::Severity::Fatal:
    *OS << "fatal";
    break;
  }
  *OS << " \"" << Str << '"';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaWarning(SourceLocation Loc,
                                             PragmaWarningSpecifier WarningSpec,
                                             ArrayRef<int> Ids) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  *OS << "#pragma warning(";
  switch(WarningSpec) {
    case PWS_Default:  *OS << "default"; break;
    case PWS_Disable:  *OS << "disable"; break;
    case PWS_Error:    *OS << "error"; break;
    case PWS_Once:     *OS << "once"; break;
    case PWS_Suppress: *OS << "suppress"; break;
    case PWS_Level1:   *OS << '1'; break;
    case PWS_Level2:   *OS << '2'; break;
    case PWS_Level3:   *OS << '3'; break;
    case PWS_Level4:   *OS << '4'; break;
  }
  *OS << ':';

  for (ArrayRef<int>::iterator I = Ids.begin(), E = Ids.end(); I != E; ++I)
    *OS << ' ' << *I;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaWarningPush(SourceLocation Loc,
                                                 int Level) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma warning(push";
  if (Level >= 0)
    *OS << ", " << Level;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaWarningPop(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma warning(pop)";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaExecCharsetPush(SourceLocation Loc,
                                                     StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma character_execution_set(push";
  if (!Str.empty())
    *OS << ", " << Str;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::PragmaExecCharsetPop(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma character_execution_set(pop)";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaAssumeNonNullBegin(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma clang assume_nonnull begin";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::
PragmaAssumeNonNullEnd(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma clang assume_nonnull end";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPPCallbacks::HandleWhitespaceBeforeTok(const Token &Tok,
                                                         bool RequireSpace,
                                                         bool RequireSameLine) {
  // These tokens are not expanded to anything and don't need whitespace before
  // them.
  if (Tok.is(tok::eof) ||
      (Tok.isAnnotation() && !Tok.is(tok::annot_header_unit) &&
       !Tok.is(tok::annot_module_begin) && !Tok.is(tok::annot_module_end) &&
       !Tok.is(tok::annot_repl_input_end) && !Tok.is(tok::annot_embed)))
    return;

  // EmittedDirectiveOnThisLine takes priority over RequireSameLine.
  if ((!RequireSameLine || EmittedDirectiveOnThisLine) &&
      MoveToLine(Tok, /*RequireStartOfLine=*/EmittedDirectiveOnThisLine)) {
    if (MinimizeWhitespace) {
      // Avoid interpreting hash as a directive under -fpreprocessed.
      if (Tok.is(tok::hash))
        *OS << ' ';
    } else {
      // Print out space characters so that the first token on a line is
      // indented for easy reading.
      unsigned ColNo = SM.getExpansionColumnNumber(Tok.getLocation());

      // The first token on a line can have a column number of 1, yet still
      // expect leading white space, if a macro expansion in column 1 starts
      // with an empty macro argument, or an empty nested macro expansion. In
      // this case, move the token to column 2.
      if (ColNo == 1 && Tok.hasLeadingSpace())
        ColNo = 2;

      // This hack prevents stuff like:
      // #define HASH #
      // HASH define foo bar
      // From having the # character end up at column 1, which makes it so it
      // is not handled as a #define next time through the preprocessor if in
      // -fpreprocessed mode.
      if (ColNo <= 1 && Tok.is(tok::hash))
        *OS << ' ';

      // Otherwise, indent the appropriate number of spaces.
      for (; ColNo > 1; --ColNo)
        *OS << ' ';
    }
  } else {
    // Insert whitespace between the previous and next token if either
    // - The caller requires it
    // - The input had whitespace between them and we are not in
    //   whitespace-minimization mode
    // - The whitespace is necessary to keep the tokens apart and there is not
    //   already a newline between them
    if (RequireSpace || (!MinimizeWhitespace && Tok.hasLeadingSpace()) ||
        ((EmittedTokensOnThisLine || EmittedDirectiveOnThisLine) &&
         AvoidConcat(PrevPrevTok, PrevTok, Tok)))
      *OS << ' ';
  }

  PrevPrevTok = PrevTok;
  PrevTok = Tok;
}

void PrintPPOutputPPCallbacks::HandleNewlinesInToken(const char *TokStr,
                                                     unsigned Len) {
  unsigned NumNewlines = 0;
  for (; Len; --Len, ++TokStr) {
    if (*TokStr != '\n' &&
        *TokStr != '\r')
      continue;

    ++NumNewlines;

    // If we have \n\r or \r\n, skip both and count as one line.
    if (Len != 1 &&
        (TokStr[1] == '\n' || TokStr[1] == '\r') &&
        TokStr[0] != TokStr[1]) {
      ++TokStr;
      --Len;
    }
  }

  if (NumNewlines == 0) return;

  CurLine += NumNewlines;
}


namespace {
struct UnknownPragmaHandler : public PragmaHandler {
  const char *Prefix;
  PrintPPOutputPPCallbacks *Callbacks;

  // Set to true if tokens should be expanded
  bool ShouldExpandTokens;

  UnknownPragmaHandler(const char *prefix, PrintPPOutputPPCallbacks *callbacks,
                       bool RequireTokenExpansion)
      : Prefix(prefix), Callbacks(callbacks),
        ShouldExpandTokens(RequireTokenExpansion) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &PragmaTok) override {
    // Figure out what line we went to and insert the appropriate number of
    // newline characters.
    Callbacks->MoveToLine(PragmaTok.getLocation(), /*RequireStartOfLine=*/true);
    Callbacks->OS->write(Prefix, strlen(Prefix));
    Callbacks->setEmittedTokensOnThisLine();

    if (ShouldExpandTokens) {
      // The first token does not have expanded macros. Expand them, if
      // required.
      auto Toks = std::make_unique<Token[]>(1);
      Toks[0] = PragmaTok;
      PP.EnterTokenStream(std::move(Toks), /*NumToks=*/1,
                          /*DisableMacroExpansion=*/false,
                          /*IsReinject=*/false);
      PP.Lex(PragmaTok);
    }

    // Read and print all of the pragma tokens.
    bool IsFirst = true;
    while (PragmaTok.isNot(tok::eod)) {
      Callbacks->HandleWhitespaceBeforeTok(PragmaTok, /*RequireSpace=*/IsFirst,
                                           /*RequireSameLine=*/true);
      IsFirst = false;
      std::string TokSpell = PP.getSpelling(PragmaTok);
      Callbacks->OS->write(&TokSpell[0], TokSpell.size());
      Callbacks->setEmittedTokensOnThisLine();

      if (ShouldExpandTokens)
        PP.Lex(PragmaTok);
      else
        PP.LexUnexpandedToken(PragmaTok);
    }
    Callbacks->setEmittedDirectiveOnThisLine();
  }
};
} // end anonymous namespace


static void PrintPreprocessedTokens(Preprocessor &PP, Token &Tok,
                                    PrintPPOutputPPCallbacks *Callbacks) {
  bool DropComments = PP.getLangOpts().TraditionalCPP &&
                      !PP.getCommentRetentionState();

  bool IsStartOfLine = false;
  char Buffer[256];
  while (true) {
    // Two lines joined with line continuation ('\' as last character on the
    // line) must be emitted as one line even though Tok.getLine() returns two
    // different values. In this situation Tok.isAtStartOfLine() is false even
    // though it may be the first token on the lexical line. When
    // dropping/skipping a token that is at the start of a line, propagate the
    // start-of-line-ness to the next token to not append it to the previous
    // line.
    IsStartOfLine = IsStartOfLine || Tok.isAtStartOfLine();

    Callbacks->HandleWhitespaceBeforeTok(Tok, /*RequireSpace=*/false,
                                         /*RequireSameLine=*/!IsStartOfLine);

    if (DropComments && Tok.is(tok::comment)) {
      // Skip comments. Normally the preprocessor does not generate
      // tok::comment nodes at all when not keeping comments, but under
      // -traditional-cpp the lexer keeps /all/ whitespace, including comments.
      PP.Lex(Tok);
      continue;
    } else if (Tok.is(tok::annot_repl_input_end)) {
      PP.Lex(Tok);
      continue;
    } else if (Tok.is(tok::eod)) {
      // Don't print end of directive tokens, since they are typically newlines
      // that mess up our line tracking. These come from unknown pre-processor
      // directives or hash-prefixed comments in standalone assembly files.
      PP.Lex(Tok);
      // FIXME: The token on the next line after #include should have
      // Tok.isAtStartOfLine() set.
      IsStartOfLine = true;
      continue;
    } else if (Tok.is(tok::annot_module_include)) {
      // PrintPPOutputPPCallbacks::InclusionDirective handles producing
      // appropriate output here. Ignore this token entirely.
      PP.Lex(Tok);
      IsStartOfLine = true;
      continue;
    } else if (Tok.is(tok::annot_module_begin)) {
      // FIXME: We retrieve this token after the FileChanged callback, and
      // retrieve the module_end token before the FileChanged callback, so
      // we render this within the file and render the module end outside the
      // file, but this is backwards from the token locations: the module_begin
      // token is at the include location (outside the file) and the module_end
      // token is at the EOF location (within the file).
      Callbacks->BeginModule(
          reinterpret_cast<Module *>(Tok.getAnnotationValue()));
      PP.Lex(Tok);
      IsStartOfLine = true;
      continue;
    } else if (Tok.is(tok::annot_module_end)) {
      Callbacks->EndModule(
          reinterpret_cast<Module *>(Tok.getAnnotationValue()));
      PP.Lex(Tok);
      IsStartOfLine = true;
      continue;
    } else if (Tok.is(tok::annot_header_unit)) {
      // This is a header-name that has been (effectively) converted into a
      // module-name.
      // FIXME: The module name could contain non-identifier module name
      // components. We don't have a good way to round-trip those.
      Module *M = reinterpret_cast<Module *>(Tok.getAnnotationValue());
      std::string Name = M->getFullModuleName();
      Callbacks->OS->write(Name.data(), Name.size());
      Callbacks->HandleNewlinesInToken(Name.data(), Name.size());
    } else if (Tok.is(tok::annot_embed)) {
      // Manually explode the binary data out to a stream of comma-delimited
      // integer values. If the user passed -dE, that is handled by the
      // EmbedDirective() callback. We should only get here if the user did not
      // pass -dE.
      assert(Callbacks->expandEmbedContents() &&
             "did not expect an embed annotation");
      auto *Data =
          reinterpret_cast<EmbedAnnotationData *>(Tok.getAnnotationValue());

      // Loop over the contents and print them as a comma-delimited list of
      // values.
      bool PrintComma = false;
      for (auto Iter = Data->BinaryData.begin(), End = Data->BinaryData.end();
           Iter != End; ++Iter) {
        if (PrintComma)
          *Callbacks->OS << ", ";
        *Callbacks->OS << static_cast<unsigned>(*Iter);
        PrintComma = true;
      }
      IsStartOfLine = true;
    } else if (Tok.isAnnotation()) {
      // Ignore annotation tokens created by pragmas - the pragmas themselves
      // will be reproduced in the preprocessed output.
      PP.Lex(Tok);
      continue;
    } else if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
      *Callbacks->OS << II->getName();
    } else if (Tok.isLiteral() && !Tok.needsCleaning() &&
               Tok.getLiteralData()) {
      Callbacks->OS->write(Tok.getLiteralData(), Tok.getLength());
    } else if (Tok.getLength() < std::size(Buffer)) {
      const char *TokPtr = Buffer;
      unsigned Len = PP.getSpelling(Tok, TokPtr);
      Callbacks->OS->write(TokPtr, Len);

      // Tokens that can contain embedded newlines need to adjust our current
      // line number.
      // FIXME: The token may end with a newline in which case
      // setEmittedDirectiveOnThisLine/setEmittedTokensOnThisLine afterwards is
      // wrong.
      if (Tok.getKind() == tok::comment || Tok.getKind() == tok::unknown)
        Callbacks->HandleNewlinesInToken(TokPtr, Len);
      if (Tok.is(tok::comment) && Len >= 2 && TokPtr[0] == '/' &&
          TokPtr[1] == '/') {
        // It's a line comment;
        // Ensure that we don't concatenate anything behind it.
        Callbacks->setEmittedDirectiveOnThisLine();
      }
    } else {
      std::string S = PP.getSpelling(Tok);
      Callbacks->OS->write(S.data(), S.size());

      // Tokens that can contain embedded newlines need to adjust our current
      // line number.
      if (Tok.getKind() == tok::comment || Tok.getKind() == tok::unknown)
        Callbacks->HandleNewlinesInToken(S.data(), S.size());
      if (Tok.is(tok::comment) && S.size() >= 2 && S[0] == '/' && S[1] == '/') {
        // It's a line comment;
        // Ensure that we don't concatenate anything behind it.
        Callbacks->setEmittedDirectiveOnThisLine();
      }
    }
    Callbacks->setEmittedTokensOnThisLine();
    IsStartOfLine = false;

    if (Tok.is(tok::eof)) break;

    PP.Lex(Tok);
    // If lexing that token causes us to need to skip future tokens, do so now.
    for (unsigned I = 0, Skip = Callbacks->GetNumToksToSkip(); I < Skip; ++I)
      PP.Lex(Tok);
    Callbacks->ResetSkipToks();
  }
}

typedef std::pair<const IdentifierInfo *, MacroInfo *> id_macro_pair;
static int MacroIDCompare(const id_macro_pair *LHS, const id_macro_pair *RHS) {
  return LHS->first->getName().compare(RHS->first->getName());
}

static void DoPrintMacros(Preprocessor &PP, raw_ostream *OS) {
  // Ignore unknown pragmas.
  PP.IgnorePragmas();

  // -dM mode just scans and ignores all tokens in the files, then dumps out
  // the macro table at the end.
  PP.EnterMainSourceFile();

  Token Tok;
  do PP.Lex(Tok);
  while (Tok.isNot(tok::eof));

  SmallVector<id_macro_pair, 128> MacrosByID;
  for (Preprocessor::macro_iterator I = PP.macro_begin(), E = PP.macro_end();
       I != E; ++I) {
    auto *MD = I->second.getLatest();
    if (MD && MD->isDefined())
      MacrosByID.push_back(id_macro_pair(I->first, MD->getMacroInfo()));
  }
  llvm::array_pod_sort(MacrosByID.begin(), MacrosByID.end(), MacroIDCompare);

  for (unsigned i = 0, e = MacrosByID.size(); i != e; ++i) {
    MacroInfo &MI = *MacrosByID[i].second;
    // Ignore computed macros like __LINE__ and friends.
    if (MI.isBuiltinMacro()) continue;

    PrintMacroDefinition(*MacrosByID[i].first, MI, PP, OS);
    *OS << '\n';
  }
}

/// DoPrintPreprocessedInput - This implements -E mode.
///
void clang::DoPrintPreprocessedInput(Preprocessor &PP, raw_ostream *OS,
                                     const PreprocessorOutputOptions &Opts) {
  // Show macros with no output is handled specially.
  if (!Opts.ShowCPP) {
    assert(Opts.ShowMacros && "Not yet implemented!");
    DoPrintMacros(PP, OS);
    return;
  }

  // Inform the preprocessor whether we want it to retain comments or not, due
  // to -C or -CC.
  PP.SetCommentRetentionState(Opts.ShowComments, Opts.ShowMacroComments);

  PrintPPOutputPPCallbacks *Callbacks = new PrintPPOutputPPCallbacks(
      PP, OS, !Opts.ShowLineMarkers, Opts.ShowMacros,
      Opts.ShowIncludeDirectives, Opts.ShowEmbedDirectives,
      Opts.UseLineDirectives, Opts.MinimizeWhitespace, Opts.DirectivesOnly,
      Opts.KeepSystemIncludes);

  // Expand macros in pragmas with -fms-extensions.  The assumption is that
  // the majority of pragmas in such a file will be Microsoft pragmas.
  // Remember the handlers we will add so that we can remove them later.
  std::unique_ptr<UnknownPragmaHandler> MicrosoftExtHandler(
      new UnknownPragmaHandler(
          "#pragma", Callbacks,
          /*RequireTokenExpansion=*/PP.getLangOpts().MicrosoftExt));

  std::unique_ptr<UnknownPragmaHandler> GCCHandler(new UnknownPragmaHandler(
      "#pragma GCC", Callbacks,
      /*RequireTokenExpansion=*/PP.getLangOpts().MicrosoftExt));

  std::unique_ptr<UnknownPragmaHandler> ClangHandler(new UnknownPragmaHandler(
      "#pragma clang", Callbacks,
      /*RequireTokenExpansion=*/PP.getLangOpts().MicrosoftExt));

  PP.AddPragmaHandler(MicrosoftExtHandler.get());
  PP.AddPragmaHandler("GCC", GCCHandler.get());
  PP.AddPragmaHandler("clang", ClangHandler.get());

  // The tokens after pragma omp need to be expanded.
  //
  //  OpenMP [2.1, Directive format]
  //  Preprocessing tokens following the #pragma omp are subject to macro
  //  replacement.
  std::unique_ptr<UnknownPragmaHandler> OpenMPHandler(
      new UnknownPragmaHandler("#pragma omp", Callbacks,
                               /*RequireTokenExpansion=*/true));
  PP.AddPragmaHandler("omp", OpenMPHandler.get());

  PP.addPPCallbacks(std::unique_ptr<PPCallbacks>(Callbacks));

  // After we have configured the preprocessor, enter the main file.
  PP.EnterMainSourceFile();
  if (Opts.DirectivesOnly)
    PP.SetMacroExpansionOnlyInDirectives();

  // Consume all of the tokens that come from the predefines buffer.  Those
  // should not be emitted into the output and are guaranteed to be at the
  // start.
  const SourceManager &SourceMgr = PP.getSourceManager();
  Token Tok;
  do {
    PP.Lex(Tok);
    if (Tok.is(tok::eof) || !Tok.getLocation().isFileID())
      break;

    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Tok.getLocation());
    if (PLoc.isInvalid())
      break;

    if (strcmp(PLoc.getFilename(), "<built-in>"))
      break;
  } while (true);

  // Read all the preprocessed tokens, printing them out to the stream.
  PrintPreprocessedTokens(PP, Tok, Callbacks);
  *OS << '\n';

  // Remove the handlers we just added to leave the preprocessor in a sane state
  // so that it can be reused (for example by a clang::Parser instance).
  PP.RemovePragmaHandler(MicrosoftExtHandler.get());
  PP.RemovePragmaHandler("GCC", GCCHandler.get());
  PP.RemovePragmaHandler("clang", ClangHandler.get());
  PP.RemovePragmaHandler("omp", OpenMPHandler.get());
}
