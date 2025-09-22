//===- DependencyDirectivesScanner.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This is the interface for scanning header and source files to get the
/// minimum necessary preprocessor directives for evaluating includes. It
/// reduces the source down to #define, #include, #import, @import, and any
/// conditional preprocessor logic that contains one of those.
///
//===----------------------------------------------------------------------===//

#include "clang/Lex/DependencyDirectivesScanner.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Pragma.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"
#include <optional>

using namespace clang;
using namespace clang::dependency_directives_scan;
using namespace llvm;

namespace {

struct DirectiveWithTokens {
  DirectiveKind Kind;
  unsigned NumTokens;

  DirectiveWithTokens(DirectiveKind Kind, unsigned NumTokens)
      : Kind(Kind), NumTokens(NumTokens) {}
};

/// Does an efficient "scan" of the sources to detect the presence of
/// preprocessor (or module import) directives and collects the raw lexed tokens
/// for those directives so that the \p Lexer can "replay" them when the file is
/// included.
///
/// Note that the behavior of the raw lexer is affected by the language mode,
/// while at this point we want to do a scan and collect tokens once,
/// irrespective of the language mode that the file will get included in. To
/// compensate for that the \p Lexer, while "replaying", will adjust a token
/// where appropriate, when it could affect the preprocessor's state.
/// For example in a directive like
///
/// \code
///   #if __has_cpp_attribute(clang::fallthrough)
/// \endcode
///
/// The preprocessor needs to see '::' as 'tok::coloncolon' instead of 2
/// 'tok::colon'. The \p Lexer will adjust if it sees consecutive 'tok::colon'
/// while in C++ mode.
struct Scanner {
  Scanner(StringRef Input,
          SmallVectorImpl<dependency_directives_scan::Token> &Tokens,
          DiagnosticsEngine *Diags, SourceLocation InputSourceLoc)
      : Input(Input), Tokens(Tokens), Diags(Diags),
        InputSourceLoc(InputSourceLoc), LangOpts(getLangOptsForDepScanning()),
        TheLexer(InputSourceLoc, LangOpts, Input.begin(), Input.begin(),
                 Input.end()) {}

  static LangOptions getLangOptsForDepScanning() {
    LangOptions LangOpts;
    // Set the lexer to use 'tok::at' for '@', instead of 'tok::unknown'.
    LangOpts.ObjC = true;
    LangOpts.LineComment = true;
    LangOpts.RawStringLiterals = true;
    // FIXME: we do not enable C11 or C++11, so we are missing u/u8/U"".
    return LangOpts;
  }

  /// Lex the provided source and emit the directive tokens.
  ///
  /// \returns True on error.
  bool scan(SmallVectorImpl<Directive> &Directives);

private:
  /// Lexes next token and advances \p First and the \p Lexer.
  [[nodiscard]] dependency_directives_scan::Token &
  lexToken(const char *&First, const char *const End);

  [[nodiscard]] dependency_directives_scan::Token &
  lexIncludeFilename(const char *&First, const char *const End);

  void skipLine(const char *&First, const char *const End);
  void skipDirective(StringRef Name, const char *&First, const char *const End);

  /// Returns the spelling of a string literal or identifier after performing
  /// any processing needed to handle \c clang::Token::NeedsCleaning.
  StringRef cleanStringIfNeeded(const dependency_directives_scan::Token &Tok);

  /// Lexes next token and if it is identifier returns its string, otherwise
  /// it skips the current line and returns \p std::nullopt.
  ///
  /// In any case (whatever the token kind) \p First and the \p Lexer will
  /// advance beyond the token.
  [[nodiscard]] std::optional<StringRef>
  tryLexIdentifierOrSkipLine(const char *&First, const char *const End);

  /// Used when it is certain that next token is an identifier.
  [[nodiscard]] StringRef lexIdentifier(const char *&First,
                                        const char *const End);

  /// Lexes next token and returns true iff it is an identifier that matches \p
  /// Id, otherwise it skips the current line and returns false.
  ///
  /// In any case (whatever the token kind) \p First and the \p Lexer will
  /// advance beyond the token.
  [[nodiscard]] bool isNextIdentifierOrSkipLine(StringRef Id,
                                                const char *&First,
                                                const char *const End);

  /// Lexes next token and returns true iff it matches the kind \p K.
  /// Otherwise it skips the current line and returns false.
  ///
  /// In any case (whatever the token kind) \p First and the \p Lexer will
  /// advance beyond the token.
  [[nodiscard]] bool isNextTokenOrSkipLine(tok::TokenKind K, const char *&First,
                                           const char *const End);

  /// Lexes next token and if it is string literal, returns its string.
  /// Otherwise, it skips the current line and returns \p std::nullopt.
  ///
  /// In any case (whatever the token kind) \p First and the \p Lexer will
  /// advance beyond the token.
  [[nodiscard]] std::optional<StringRef>
  tryLexStringLiteralOrSkipLine(const char *&First, const char *const End);

  [[nodiscard]] bool scanImpl(const char *First, const char *const End);
  [[nodiscard]] bool lexPPLine(const char *&First, const char *const End);
  [[nodiscard]] bool lexAt(const char *&First, const char *const End);
  [[nodiscard]] bool lexModule(const char *&First, const char *const End);
  [[nodiscard]] bool lexDefine(const char *HashLoc, const char *&First,
                               const char *const End);
  [[nodiscard]] bool lexPragma(const char *&First, const char *const End);
  [[nodiscard]] bool lex_Pragma(const char *&First, const char *const End);
  [[nodiscard]] bool lexEndif(const char *&First, const char *const End);
  [[nodiscard]] bool lexDefault(DirectiveKind Kind, const char *&First,
                                const char *const End);
  [[nodiscard]] bool lexModuleDirectiveBody(DirectiveKind Kind,
                                            const char *&First,
                                            const char *const End);
  void lexPPDirectiveBody(const char *&First, const char *const End);

  DirectiveWithTokens &pushDirective(DirectiveKind Kind) {
    Tokens.append(CurDirToks);
    DirsWithToks.emplace_back(Kind, CurDirToks.size());
    CurDirToks.clear();
    return DirsWithToks.back();
  }
  void popDirective() {
    Tokens.pop_back_n(DirsWithToks.pop_back_val().NumTokens);
  }
  DirectiveKind topDirective() const {
    return DirsWithToks.empty() ? pp_none : DirsWithToks.back().Kind;
  }

  unsigned getOffsetAt(const char *CurPtr) const {
    return CurPtr - Input.data();
  }

  /// Reports a diagnostic if the diagnostic engine is provided. Always returns
  /// true at the end.
  bool reportError(const char *CurPtr, unsigned Err);

  StringMap<char> SplitIds;
  StringRef Input;
  SmallVectorImpl<dependency_directives_scan::Token> &Tokens;
  DiagnosticsEngine *Diags;
  SourceLocation InputSourceLoc;

  const char *LastTokenPtr = nullptr;
  /// Keeps track of the tokens for the currently lexed directive. Once a
  /// directive is fully lexed and "committed" then the tokens get appended to
  /// \p Tokens and \p CurDirToks is cleared for the next directive.
  SmallVector<dependency_directives_scan::Token, 32> CurDirToks;
  /// The directives that were lexed along with the number of tokens that each
  /// directive contains. The tokens of all the directives are kept in \p Tokens
  /// vector, in the same order as the directives order in \p DirsWithToks.
  SmallVector<DirectiveWithTokens, 64> DirsWithToks;
  LangOptions LangOpts;
  Lexer TheLexer;
};

} // end anonymous namespace

bool Scanner::reportError(const char *CurPtr, unsigned Err) {
  if (!Diags)
    return true;
  assert(CurPtr >= Input.data() && "invalid buffer ptr");
  Diags->Report(InputSourceLoc.getLocWithOffset(getOffsetAt(CurPtr)), Err);
  return true;
}

static void skipOverSpaces(const char *&First, const char *const End) {
  while (First != End && isHorizontalWhitespace(*First))
    ++First;
}

[[nodiscard]] static bool isRawStringLiteral(const char *First,
                                             const char *Current) {
  assert(First <= Current);

  // Check if we can even back up.
  if (*Current != '"' || First == Current)
    return false;

  // Check for an "R".
  --Current;
  if (*Current != 'R')
    return false;
  if (First == Current || !isAsciiIdentifierContinue(*--Current))
    return true;

  // Check for a prefix of "u", "U", or "L".
  if (*Current == 'u' || *Current == 'U' || *Current == 'L')
    return First == Current || !isAsciiIdentifierContinue(*--Current);

  // Check for a prefix of "u8".
  if (*Current != '8' || First == Current || *Current-- != 'u')
    return false;
  return First == Current || !isAsciiIdentifierContinue(*--Current);
}

static void skipRawString(const char *&First, const char *const End) {
  assert(First[0] == '"');
  assert(First[-1] == 'R');

  const char *Last = ++First;
  while (Last != End && *Last != '(')
    ++Last;
  if (Last == End) {
    First = Last; // Hit the end... just give up.
    return;
  }

  StringRef Terminator(First, Last - First);
  for (;;) {
    // Move First to just past the next ")".
    First = Last;
    while (First != End && *First != ')')
      ++First;
    if (First == End)
      return;
    ++First;

    // Look ahead for the terminator sequence.
    Last = First;
    while (Last != End && size_t(Last - First) < Terminator.size() &&
           Terminator[Last - First] == *Last)
      ++Last;

    // Check if we hit it (or the end of the file).
    if (Last == End) {
      First = Last;
      return;
    }
    if (size_t(Last - First) < Terminator.size())
      continue;
    if (*Last != '"')
      continue;
    First = Last + 1;
    return;
  }
}

// Returns the length of EOL, either 0 (no end-of-line), 1 (\n) or 2 (\r\n)
static unsigned isEOL(const char *First, const char *const End) {
  if (First == End)
    return 0;
  if (End - First > 1 && isVerticalWhitespace(First[0]) &&
      isVerticalWhitespace(First[1]) && First[0] != First[1])
    return 2;
  return !!isVerticalWhitespace(First[0]);
}

static void skipString(const char *&First, const char *const End) {
  assert(*First == '\'' || *First == '"' || *First == '<');
  const char Terminator = *First == '<' ? '>' : *First;
  for (++First; First != End && *First != Terminator; ++First) {
    // String and character literals don't extend past the end of the line.
    if (isVerticalWhitespace(*First))
      return;
    if (*First != '\\')
      continue;
    // Skip past backslash to the next character. This ensures that the
    // character right after it is skipped as well, which matters if it's
    // the terminator.
    if (++First == End)
      return;
    if (!isWhitespace(*First))
      continue;
    // Whitespace after the backslash might indicate a line continuation.
    const char *FirstAfterBackslashPastSpace = First;
    skipOverSpaces(FirstAfterBackslashPastSpace, End);
    if (unsigned NLSize = isEOL(FirstAfterBackslashPastSpace, End)) {
      // Advance the character pointer to the next line for the next
      // iteration.
      First = FirstAfterBackslashPastSpace + NLSize - 1;
    }
  }
  if (First != End)
    ++First; // Finish off the string.
}

// Returns the length of the skipped newline
static unsigned skipNewline(const char *&First, const char *End) {
  if (First == End)
    return 0;
  assert(isVerticalWhitespace(*First));
  unsigned Len = isEOL(First, End);
  assert(Len && "expected newline");
  First += Len;
  return Len;
}

static bool wasLineContinuation(const char *First, unsigned EOLLen) {
  return *(First - (int)EOLLen - 1) == '\\';
}

static void skipToNewlineRaw(const char *&First, const char *const End) {
  for (;;) {
    if (First == End)
      return;

    unsigned Len = isEOL(First, End);
    if (Len)
      return;

    do {
      if (++First == End)
        return;
      Len = isEOL(First, End);
    } while (!Len);

    if (First[-1] != '\\')
      return;

    First += Len;
    // Keep skipping lines...
  }
}

static void skipLineComment(const char *&First, const char *const End) {
  assert(First[0] == '/' && First[1] == '/');
  First += 2;
  skipToNewlineRaw(First, End);
}

static void skipBlockComment(const char *&First, const char *const End) {
  assert(First[0] == '/' && First[1] == '*');
  if (End - First < 4) {
    First = End;
    return;
  }
  for (First += 3; First != End; ++First)
    if (First[-1] == '*' && First[0] == '/') {
      ++First;
      return;
    }
}

/// \returns True if the current single quotation mark character is a C++14
/// digit separator.
static bool isQuoteCppDigitSeparator(const char *const Start,
                                     const char *const Cur,
                                     const char *const End) {
  assert(*Cur == '\'' && "expected quotation character");
  // skipLine called in places where we don't expect a valid number
  // body before `start` on the same line, so always return false at the start.
  if (Start == Cur)
    return false;
  // The previous character must be a valid PP number character.
  // Make sure that the L, u, U, u8 prefixes don't get marked as a
  // separator though.
  char Prev = *(Cur - 1);
  if (Prev == 'L' || Prev == 'U' || Prev == 'u')
    return false;
  if (Prev == '8' && (Cur - 1 != Start) && *(Cur - 2) == 'u')
    return false;
  if (!isPreprocessingNumberBody(Prev))
    return false;
  // The next character should be a valid identifier body character.
  return (Cur + 1) < End && isAsciiIdentifierContinue(*(Cur + 1));
}

void Scanner::skipLine(const char *&First, const char *const End) {
  for (;;) {
    assert(First <= End);
    if (First == End)
      return;

    if (isVerticalWhitespace(*First)) {
      skipNewline(First, End);
      return;
    }
    const char *Start = First;
    while (First != End && !isVerticalWhitespace(*First)) {
      // Iterate over strings correctly to avoid comments and newlines.
      if (*First == '"' ||
          (*First == '\'' && !isQuoteCppDigitSeparator(Start, First, End))) {
        LastTokenPtr = First;
        if (isRawStringLiteral(Start, First))
          skipRawString(First, End);
        else
          skipString(First, End);
        continue;
      }

      // Iterate over comments correctly.
      if (*First != '/' || End - First < 2) {
        LastTokenPtr = First;
        ++First;
        continue;
      }

      if (First[1] == '/') {
        // "//...".
        skipLineComment(First, End);
        continue;
      }

      if (First[1] != '*') {
        LastTokenPtr = First;
        ++First;
        continue;
      }

      // "/*...*/".
      skipBlockComment(First, End);
    }
    if (First == End)
      return;

    // Skip over the newline.
    unsigned Len = skipNewline(First, End);
    if (!wasLineContinuation(First, Len)) // Continue past line-continuations.
      break;
  }
}

void Scanner::skipDirective(StringRef Name, const char *&First,
                            const char *const End) {
  if (llvm::StringSwitch<bool>(Name)
          .Case("warning", true)
          .Case("error", true)
          .Default(false))
    // Do not process quotes or comments.
    skipToNewlineRaw(First, End);
  else
    skipLine(First, End);
}

static void skipWhitespace(const char *&First, const char *const End) {
  for (;;) {
    assert(First <= End);
    skipOverSpaces(First, End);

    if (End - First < 2)
      return;

    if (First[0] == '\\' && isVerticalWhitespace(First[1])) {
      skipNewline(++First, End);
      continue;
    }

    // Check for a non-comment character.
    if (First[0] != '/')
      return;

    // "// ...".
    if (First[1] == '/') {
      skipLineComment(First, End);
      return;
    }

    // Cannot be a comment.
    if (First[1] != '*')
      return;

    // "/*...*/".
    skipBlockComment(First, End);
  }
}

bool Scanner::lexModuleDirectiveBody(DirectiveKind Kind, const char *&First,
                                     const char *const End) {
  const char *DirectiveLoc = Input.data() + CurDirToks.front().Offset;
  for (;;) {
    const dependency_directives_scan::Token &Tok = lexToken(First, End);
    if (Tok.is(tok::eof))
      return reportError(
          DirectiveLoc,
          diag::err_dep_source_scanner_missing_semi_after_at_import);
    if (Tok.is(tok::semi))
      break;
  }
  pushDirective(Kind);
  skipWhitespace(First, End);
  if (First == End)
    return false;
  if (!isVerticalWhitespace(*First))
    return reportError(
        DirectiveLoc, diag::err_dep_source_scanner_unexpected_tokens_at_import);
  skipNewline(First, End);
  return false;
}

dependency_directives_scan::Token &Scanner::lexToken(const char *&First,
                                                     const char *const End) {
  clang::Token Tok;
  TheLexer.LexFromRawLexer(Tok);
  First = Input.data() + TheLexer.getCurrentBufferOffset();
  assert(First <= End);

  unsigned Offset = TheLexer.getCurrentBufferOffset() - Tok.getLength();
  CurDirToks.emplace_back(Offset, Tok.getLength(), Tok.getKind(),
                          Tok.getFlags());
  return CurDirToks.back();
}

dependency_directives_scan::Token &
Scanner::lexIncludeFilename(const char *&First, const char *const End) {
  clang::Token Tok;
  TheLexer.LexIncludeFilename(Tok);
  First = Input.data() + TheLexer.getCurrentBufferOffset();
  assert(First <= End);

  unsigned Offset = TheLexer.getCurrentBufferOffset() - Tok.getLength();
  CurDirToks.emplace_back(Offset, Tok.getLength(), Tok.getKind(),
                          Tok.getFlags());
  return CurDirToks.back();
}

void Scanner::lexPPDirectiveBody(const char *&First, const char *const End) {
  while (true) {
    const dependency_directives_scan::Token &Tok = lexToken(First, End);
    if (Tok.is(tok::eod) || Tok.is(tok::eof))
      break;
  }
}

StringRef
Scanner::cleanStringIfNeeded(const dependency_directives_scan::Token &Tok) {
  bool NeedsCleaning = Tok.Flags & clang::Token::NeedsCleaning;
  if (LLVM_LIKELY(!NeedsCleaning))
    return Input.slice(Tok.Offset, Tok.getEnd());

  SmallString<64> Spelling;
  Spelling.resize(Tok.Length);

  // FIXME: C++11 raw string literals need special handling (see getSpellingSlow
  // in the Lexer). Currently we cannot see them due to our LangOpts.

  unsigned SpellingLength = 0;
  const char *BufPtr = Input.begin() + Tok.Offset;
  const char *AfterIdent = Input.begin() + Tok.getEnd();
  while (BufPtr < AfterIdent) {
    auto [Char, Size] = Lexer::getCharAndSizeNoWarn(BufPtr, LangOpts);
    Spelling[SpellingLength++] = Char;
    BufPtr += Size;
  }

  return SplitIds.try_emplace(StringRef(Spelling.begin(), SpellingLength), 0)
      .first->first();
}

std::optional<StringRef>
Scanner::tryLexIdentifierOrSkipLine(const char *&First, const char *const End) {
  const dependency_directives_scan::Token &Tok = lexToken(First, End);
  if (Tok.isNot(tok::raw_identifier)) {
    if (!Tok.is(tok::eod))
      skipLine(First, End);
    return std::nullopt;
  }

  return cleanStringIfNeeded(Tok);
}

StringRef Scanner::lexIdentifier(const char *&First, const char *const End) {
  std::optional<StringRef> Id = tryLexIdentifierOrSkipLine(First, End);
  assert(Id && "expected identifier token");
  return *Id;
}

bool Scanner::isNextIdentifierOrSkipLine(StringRef Id, const char *&First,
                                         const char *const End) {
  if (std::optional<StringRef> FoundId =
          tryLexIdentifierOrSkipLine(First, End)) {
    if (*FoundId == Id)
      return true;
    skipLine(First, End);
  }
  return false;
}

bool Scanner::isNextTokenOrSkipLine(tok::TokenKind K, const char *&First,
                                    const char *const End) {
  const dependency_directives_scan::Token &Tok = lexToken(First, End);
  if (Tok.is(K))
    return true;
  skipLine(First, End);
  return false;
}

std::optional<StringRef>
Scanner::tryLexStringLiteralOrSkipLine(const char *&First,
                                       const char *const End) {
  const dependency_directives_scan::Token &Tok = lexToken(First, End);
  if (!tok::isStringLiteral(Tok.Kind)) {
    if (!Tok.is(tok::eod))
      skipLine(First, End);
    return std::nullopt;
  }

  return cleanStringIfNeeded(Tok);
}

bool Scanner::lexAt(const char *&First, const char *const End) {
  // Handle "@import".

  // Lex '@'.
  const dependency_directives_scan::Token &AtTok = lexToken(First, End);
  assert(AtTok.is(tok::at));
  (void)AtTok;

  if (!isNextIdentifierOrSkipLine("import", First, End))
    return false;
  return lexModuleDirectiveBody(decl_at_import, First, End);
}

bool Scanner::lexModule(const char *&First, const char *const End) {
  StringRef Id = lexIdentifier(First, End);
  bool Export = false;
  if (Id == "export") {
    Export = true;
    std::optional<StringRef> NextId = tryLexIdentifierOrSkipLine(First, End);
    if (!NextId)
      return false;
    Id = *NextId;
  }

  if (Id != "module" && Id != "import") {
    skipLine(First, End);
    return false;
  }

  skipWhitespace(First, End);

  // Ignore this as a module directive if the next character can't be part of
  // an import.

  switch (*First) {
  case ':': {
    // `module :` is never the start of a valid module declaration.
    if (Id == "module") {
      skipLine(First, End);
      return false;
    }
    // `import:(type)name` is a valid ObjC method decl, so check one more token.
    (void)lexToken(First, End);
    if (!tryLexIdentifierOrSkipLine(First, End))
      return false;
    break;
  }
  case '<':
  case '"':
    break;
  default:
    if (!isAsciiIdentifierContinue(*First)) {
      skipLine(First, End);
      return false;
    }
  }

  TheLexer.seek(getOffsetAt(First), /*IsAtStartOfLine*/ false);

  DirectiveKind Kind;
  if (Id == "module")
    Kind = Export ? cxx_export_module_decl : cxx_module_decl;
  else
    Kind = Export ? cxx_export_import_decl : cxx_import_decl;

  return lexModuleDirectiveBody(Kind, First, End);
}

bool Scanner::lex_Pragma(const char *&First, const char *const End) {
  if (!isNextTokenOrSkipLine(tok::l_paren, First, End))
    return false;

  std::optional<StringRef> Str = tryLexStringLiteralOrSkipLine(First, End);

  if (!Str || !isNextTokenOrSkipLine(tok::r_paren, First, End))
    return false;

  SmallString<64> Buffer(*Str);
  prepare_PragmaString(Buffer);

  // Use a new scanner instance since the tokens will be inside the allocated
  // string. We should already have captured all the relevant tokens in the
  // current scanner.
  SmallVector<dependency_directives_scan::Token> DiscardTokens;
  const char *Begin = Buffer.c_str();
  Scanner PragmaScanner{StringRef(Begin, Buffer.size()), DiscardTokens, Diags,
                        InputSourceLoc};

  PragmaScanner.TheLexer.setParsingPreprocessorDirective(true);
  if (PragmaScanner.lexPragma(Begin, Buffer.end()))
    return true;

  DirectiveKind K = PragmaScanner.topDirective();
  if (K == pp_none) {
    skipLine(First, End);
    return false;
  }

  assert(Begin == Buffer.end());
  pushDirective(K);
  return false;
}

bool Scanner::lexPragma(const char *&First, const char *const End) {
  std::optional<StringRef> FoundId = tryLexIdentifierOrSkipLine(First, End);
  if (!FoundId)
    return false;

  StringRef Id = *FoundId;
  auto Kind = llvm::StringSwitch<DirectiveKind>(Id)
                  .Case("once", pp_pragma_once)
                  .Case("push_macro", pp_pragma_push_macro)
                  .Case("pop_macro", pp_pragma_pop_macro)
                  .Case("include_alias", pp_pragma_include_alias)
                  .Default(pp_none);
  if (Kind != pp_none) {
    lexPPDirectiveBody(First, End);
    pushDirective(Kind);
    return false;
  }

  if (Id != "clang") {
    skipLine(First, End);
    return false;
  }

  FoundId = tryLexIdentifierOrSkipLine(First, End);
  if (!FoundId)
    return false;
  Id = *FoundId;

  // #pragma clang system_header
  if (Id == "system_header") {
    lexPPDirectiveBody(First, End);
    pushDirective(pp_pragma_system_header);
    return false;
  }

  if (Id != "module") {
    skipLine(First, End);
    return false;
  }

  // #pragma clang module.
  if (!isNextIdentifierOrSkipLine("import", First, End))
    return false;

  // #pragma clang module import.
  lexPPDirectiveBody(First, End);
  pushDirective(pp_pragma_import);
  return false;
}

bool Scanner::lexEndif(const char *&First, const char *const End) {
  // Strip out "#else" if it's empty.
  if (topDirective() == pp_else)
    popDirective();

  // If "#ifdef" is empty, strip it and skip the "#endif".
  //
  // FIXME: Once/if Clang starts disallowing __has_include in macro expansions,
  // we can skip empty `#if` and `#elif` blocks as well after scanning for a
  // literal __has_include in the condition.  Even without that rule we could
  // drop the tokens if we scan for identifiers in the condition and find none.
  if (topDirective() == pp_ifdef || topDirective() == pp_ifndef) {
    popDirective();
    skipLine(First, End);
    return false;
  }

  return lexDefault(pp_endif, First, End);
}

bool Scanner::lexDefault(DirectiveKind Kind, const char *&First,
                         const char *const End) {
  lexPPDirectiveBody(First, End);
  pushDirective(Kind);
  return false;
}

static bool isStartOfRelevantLine(char First) {
  switch (First) {
  case '#':
  case '@':
  case 'i':
  case 'e':
  case 'm':
  case '_':
    return true;
  }
  return false;
}

bool Scanner::lexPPLine(const char *&First, const char *const End) {
  assert(First != End);

  skipWhitespace(First, End);
  assert(First <= End);
  if (First == End)
    return false;

  if (!isStartOfRelevantLine(*First)) {
    skipLine(First, End);
    assert(First <= End);
    return false;
  }

  LastTokenPtr = First;

  TheLexer.seek(getOffsetAt(First), /*IsAtStartOfLine*/ true);

  auto ScEx1 = make_scope_exit([&]() {
    /// Clear Scanner's CurDirToks before returning, in case we didn't push a
    /// new directive.
    CurDirToks.clear();
  });

  // Handle "@import".
  if (*First == '@')
    return lexAt(First, End);

  if (*First == 'i' || *First == 'e' || *First == 'm')
    return lexModule(First, End);

  if (*First == '_') {
    if (isNextIdentifierOrSkipLine("_Pragma", First, End))
      return lex_Pragma(First, End);
    return false;
  }

  // Handle preprocessing directives.

  TheLexer.setParsingPreprocessorDirective(true);
  auto ScEx2 = make_scope_exit(
      [&]() { TheLexer.setParsingPreprocessorDirective(false); });

  // Lex '#'.
  const dependency_directives_scan::Token &HashTok = lexToken(First, End);
  if (HashTok.is(tok::hashhash)) {
    // A \p tok::hashhash at this location is passed by the preprocessor to the
    // parser to interpret, like any other token. So for dependency scanning
    // skip it like a normal token not affecting the preprocessor.
    skipLine(First, End);
    assert(First <= End);
    return false;
  }
  assert(HashTok.is(tok::hash));
  (void)HashTok;

  std::optional<StringRef> FoundId = tryLexIdentifierOrSkipLine(First, End);
  if (!FoundId)
    return false;

  StringRef Id = *FoundId;

  if (Id == "pragma")
    return lexPragma(First, End);

  auto Kind = llvm::StringSwitch<DirectiveKind>(Id)
                  .Case("include", pp_include)
                  .Case("__include_macros", pp___include_macros)
                  .Case("define", pp_define)
                  .Case("undef", pp_undef)
                  .Case("import", pp_import)
                  .Case("include_next", pp_include_next)
                  .Case("if", pp_if)
                  .Case("ifdef", pp_ifdef)
                  .Case("ifndef", pp_ifndef)
                  .Case("elif", pp_elif)
                  .Case("elifdef", pp_elifdef)
                  .Case("elifndef", pp_elifndef)
                  .Case("else", pp_else)
                  .Case("endif", pp_endif)
                  .Default(pp_none);
  if (Kind == pp_none) {
    skipDirective(Id, First, End);
    return false;
  }

  if (Kind == pp_endif)
    return lexEndif(First, End);

  switch (Kind) {
  case pp_include:
  case pp___include_macros:
  case pp_include_next:
  case pp_import:
    // Ignore missing filenames in include or import directives.
    if (lexIncludeFilename(First, End).is(tok::eod)) {
      skipDirective(Id, First, End);
      return true;
    }
    break;
  default:
    break;
  }

  // Everything else.
  return lexDefault(Kind, First, End);
}

static void skipUTF8ByteOrderMark(const char *&First, const char *const End) {
  if ((End - First) >= 3 && First[0] == '\xef' && First[1] == '\xbb' &&
      First[2] == '\xbf')
    First += 3;
}

bool Scanner::scanImpl(const char *First, const char *const End) {
  skipUTF8ByteOrderMark(First, End);
  while (First != End)
    if (lexPPLine(First, End))
      return true;
  return false;
}

bool Scanner::scan(SmallVectorImpl<Directive> &Directives) {
  bool Error = scanImpl(Input.begin(), Input.end());

  if (!Error) {
    // Add an EOF on success.
    if (LastTokenPtr &&
        (Tokens.empty() || LastTokenPtr > Input.begin() + Tokens.back().Offset))
      pushDirective(tokens_present_before_eof);
    pushDirective(pp_eof);
  }

  ArrayRef<dependency_directives_scan::Token> RemainingTokens = Tokens;
  for (const DirectiveWithTokens &DirWithToks : DirsWithToks) {
    assert(RemainingTokens.size() >= DirWithToks.NumTokens);
    Directives.emplace_back(DirWithToks.Kind,
                            RemainingTokens.take_front(DirWithToks.NumTokens));
    RemainingTokens = RemainingTokens.drop_front(DirWithToks.NumTokens);
  }
  assert(RemainingTokens.empty());

  return Error;
}

bool clang::scanSourceForDependencyDirectives(
    StringRef Input, SmallVectorImpl<dependency_directives_scan::Token> &Tokens,
    SmallVectorImpl<Directive> &Directives, DiagnosticsEngine *Diags,
    SourceLocation InputSourceLoc) {
  return Scanner(Input, Tokens, Diags, InputSourceLoc).scan(Directives);
}

void clang::printDependencyDirectivesAsSource(
    StringRef Source,
    ArrayRef<dependency_directives_scan::Directive> Directives,
    llvm::raw_ostream &OS) {
  // Add a space separator where it is convenient for testing purposes.
  auto needsSpaceSeparator =
      [](tok::TokenKind Prev,
         const dependency_directives_scan::Token &Tok) -> bool {
    if (Prev == Tok.Kind)
      return !Tok.isOneOf(tok::l_paren, tok::r_paren, tok::l_square,
                          tok::r_square);
    if (Prev == tok::raw_identifier &&
        Tok.isOneOf(tok::hash, tok::numeric_constant, tok::string_literal,
                    tok::char_constant, tok::header_name))
      return true;
    if (Prev == tok::r_paren &&
        Tok.isOneOf(tok::raw_identifier, tok::hash, tok::string_literal,
                    tok::char_constant, tok::unknown))
      return true;
    if (Prev == tok::comma &&
        Tok.isOneOf(tok::l_paren, tok::string_literal, tok::less))
      return true;
    return false;
  };

  for (const dependency_directives_scan::Directive &Directive : Directives) {
    if (Directive.Kind == tokens_present_before_eof)
      OS << "<TokBeforeEOF>";
    std::optional<tok::TokenKind> PrevTokenKind;
    for (const dependency_directives_scan::Token &Tok : Directive.Tokens) {
      if (PrevTokenKind && needsSpaceSeparator(*PrevTokenKind, Tok))
        OS << ' ';
      PrevTokenKind = Tok.Kind;
      OS << Source.slice(Tok.Offset, Tok.getEnd());
    }
  }
}
