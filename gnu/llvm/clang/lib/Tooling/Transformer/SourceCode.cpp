//===--- SourceCode.cpp - Source code manipulation routines -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file provides functions that simplify extraction of source code.
//
//===----------------------------------------------------------------------===//
#include "clang/Tooling/Transformer/SourceCode.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Comment.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <set>

using namespace clang;

using llvm::errc;
using llvm::StringError;

StringRef clang::tooling::getText(CharSourceRange Range,
                                  const ASTContext &Context) {
  return Lexer::getSourceText(Range, Context.getSourceManager(),
                              Context.getLangOpts());
}

CharSourceRange clang::tooling::maybeExtendRange(CharSourceRange Range,
                                                 tok::TokenKind Next,
                                                 ASTContext &Context) {
  CharSourceRange R = Lexer::getAsCharRange(Range, Context.getSourceManager(),
                                            Context.getLangOpts());
  if (R.isInvalid())
    return Range;
  Token Tok;
  bool Err =
      Lexer::getRawToken(R.getEnd(), Tok, Context.getSourceManager(),
                         Context.getLangOpts(), /*IgnoreWhiteSpace=*/true);
  if (Err || !Tok.is(Next))
    return Range;
  return CharSourceRange::getTokenRange(Range.getBegin(), Tok.getLocation());
}

llvm::Error clang::tooling::validateRange(const CharSourceRange &Range,
                                          const SourceManager &SM,
                                          bool AllowSystemHeaders) {
  if (Range.isInvalid())
    return llvm::make_error<StringError>(errc::invalid_argument,
                                         "Invalid range");

  if (Range.getBegin().isMacroID() || Range.getEnd().isMacroID())
    return llvm::make_error<StringError>(
        errc::invalid_argument, "Range starts or ends in a macro expansion");

  if (!AllowSystemHeaders) {
    if (SM.isInSystemHeader(Range.getBegin()) ||
        SM.isInSystemHeader(Range.getEnd()))
      return llvm::make_error<StringError>(errc::invalid_argument,
                                           "Range is in system header");
  }

  std::pair<FileID, unsigned> BeginInfo = SM.getDecomposedLoc(Range.getBegin());
  std::pair<FileID, unsigned> EndInfo = SM.getDecomposedLoc(Range.getEnd());
  if (BeginInfo.first != EndInfo.first)
    return llvm::make_error<StringError>(
        errc::invalid_argument, "Range begins and ends in different files");

  if (BeginInfo.second > EndInfo.second)
    return llvm::make_error<StringError>(errc::invalid_argument,
                                         "Range's begin is past its end");

  return llvm::Error::success();
}

llvm::Error clang::tooling::validateEditRange(const CharSourceRange &Range,
                                              const SourceManager &SM) {
  return validateRange(Range, SM, /*AllowSystemHeaders=*/false);
}

static bool spelledInMacroDefinition(SourceLocation Loc,
                                     const SourceManager &SM) {
  while (Loc.isMacroID()) {
    const auto &Expansion = SM.getSLocEntry(SM.getFileID(Loc)).getExpansion();
    if (Expansion.isMacroArgExpansion()) {
      // Check the spelling location of the macro arg, in case the arg itself is
      // in a macro expansion.
      Loc = Expansion.getSpellingLoc();
    } else {
      return true;
    }
  }
  return false;
}

// Returns the expansion char-range of `Loc` if `Loc` is a split token. For
// example, `>>` in nested templates needs the first `>` to be split, otherwise
// the `SourceLocation` of the token would lex as `>>` instead of `>`.
static std::optional<CharSourceRange>
getExpansionForSplitToken(SourceLocation Loc, const SourceManager &SM,
                          const LangOptions &LangOpts) {
  if (Loc.isMacroID()) {
    bool Invalid = false;
    auto &SLoc = SM.getSLocEntry(SM.getFileID(Loc), &Invalid);
    if (Invalid)
      return std::nullopt;
    if (auto &Expansion = SLoc.getExpansion();
        !Expansion.isExpansionTokenRange()) {
      // A char-range expansion is only used where a token-range would be
      // incorrect, and so identifies this as a split token (and importantly,
      // not as a macro).
      return Expansion.getExpansionLocRange();
    }
  }
  return std::nullopt;
}

// If `Range` covers a split token, returns the expansion range, otherwise
// returns `Range`.
static CharSourceRange getRangeForSplitTokens(CharSourceRange Range,
                                              const SourceManager &SM,
                                              const LangOptions &LangOpts) {
  if (Range.isTokenRange()) {
    auto BeginToken = getExpansionForSplitToken(Range.getBegin(), SM, LangOpts);
    auto EndToken = getExpansionForSplitToken(Range.getEnd(), SM, LangOpts);
    if (EndToken) {
      SourceLocation BeginLoc =
          BeginToken ? BeginToken->getBegin() : Range.getBegin();
      // We can't use the expansion location with a token-range, because that
      // will incorrectly lex the end token, so use a char-range that ends at
      // the split.
      return CharSourceRange::getCharRange(BeginLoc, EndToken->getEnd());
    } else if (BeginToken) {
      // Since the end token is not split, the whole range covers the split, so
      // the only adjustment we make is to use the expansion location of the
      // begin token.
      return CharSourceRange::getTokenRange(BeginToken->getBegin(),
                                            Range.getEnd());
    }
  }
  return Range;
}

static CharSourceRange getRange(const CharSourceRange &EditRange,
                                const SourceManager &SM,
                                const LangOptions &LangOpts,
                                bool IncludeMacroExpansion) {
  CharSourceRange Range;
  if (IncludeMacroExpansion) {
    Range = Lexer::makeFileCharRange(EditRange, SM, LangOpts);
  } else {
    auto AdjustedRange = getRangeForSplitTokens(EditRange, SM, LangOpts);
    if (spelledInMacroDefinition(AdjustedRange.getBegin(), SM) ||
        spelledInMacroDefinition(AdjustedRange.getEnd(), SM))
      return {};

    auto B = SM.getSpellingLoc(AdjustedRange.getBegin());
    auto E = SM.getSpellingLoc(AdjustedRange.getEnd());
    if (AdjustedRange.isTokenRange())
      E = Lexer::getLocForEndOfToken(E, 0, SM, LangOpts);
    Range = CharSourceRange::getCharRange(B, E);
  }
  return Range;
}

std::optional<CharSourceRange> clang::tooling::getFileRangeForEdit(
    const CharSourceRange &EditRange, const SourceManager &SM,
    const LangOptions &LangOpts, bool IncludeMacroExpansion) {
  CharSourceRange Range =
      getRange(EditRange, SM, LangOpts, IncludeMacroExpansion);
  bool IsInvalid = llvm::errorToBool(validateEditRange(Range, SM));
  if (IsInvalid)
    return std::nullopt;
  return Range;
}

std::optional<CharSourceRange> clang::tooling::getFileRange(
    const CharSourceRange &EditRange, const SourceManager &SM,
    const LangOptions &LangOpts, bool IncludeMacroExpansion) {
  CharSourceRange Range =
      getRange(EditRange, SM, LangOpts, IncludeMacroExpansion);
  bool IsInvalid =
      llvm::errorToBool(validateRange(Range, SM, /*AllowSystemHeaders=*/true));
  if (IsInvalid)
    return std::nullopt;
  return Range;
}

static bool startsWithNewline(const SourceManager &SM, const Token &Tok) {
  return isVerticalWhitespace(SM.getCharacterData(Tok.getLocation())[0]);
}

static bool contains(const std::set<tok::TokenKind> &Terminators,
                     const Token &Tok) {
  return Terminators.count(Tok.getKind()) > 0;
}

// Returns the exclusive, *file* end location of the entity whose last token is
// at location 'EntityLast'. That is, it returns the location one past the last
// relevant character.
//
// Associated tokens include comments, horizontal whitespace and 'Terminators'
// -- optional tokens, which, if any are found, will be included; if
// 'Terminators' is empty, we will not include any extra tokens beyond comments
// and horizontal whitespace.
static SourceLocation
getEntityEndLoc(const SourceManager &SM, SourceLocation EntityLast,
                const std::set<tok::TokenKind> &Terminators,
                const LangOptions &LangOpts) {
  assert(EntityLast.isValid() && "Invalid end location found.");

  // We remember the last location of a non-horizontal-whitespace token we have
  // lexed; this is the location up to which we will want to delete.
  // FIXME: Support using the spelling loc here for cases where we want to
  // analyze the macro text.

  CharSourceRange ExpansionRange = SM.getExpansionRange(EntityLast);
  // FIXME: Should check isTokenRange(), for the (rare) case that
  // `ExpansionRange` is a character range.
  std::unique_ptr<Lexer> Lexer = [&]() {
    bool Invalid = false;
    auto FileOffset = SM.getDecomposedLoc(ExpansionRange.getEnd());
    llvm::StringRef File = SM.getBufferData(FileOffset.first, &Invalid);
    assert(!Invalid && "Cannot get file/offset");
    return std::make_unique<clang::Lexer>(
        SM.getLocForStartOfFile(FileOffset.first), LangOpts, File.begin(),
        File.data() + FileOffset.second, File.end());
  }();

  // Tell Lexer to return whitespace as pseudo-tokens (kind is tok::unknown).
  Lexer->SetKeepWhitespaceMode(true);

  // Generally, the code we want to include looks like this ([] are optional),
  // If Terminators is empty:
  //   [ <comment> ] [ <newline> ]
  // Otherwise:
  //   ... <terminator> [ <comment> ] [ <newline> ]

  Token Tok;
  bool Terminated = false;

  // First, lex to the current token (which is the last token of the range that
  // is definitely associated with the decl). Then, we process the first token
  // separately from the rest based on conditions that hold specifically for
  // that first token.
  //
  // We do not search for a terminator if none is required or we've already
  // encountered it. Otherwise, if the original `EntityLast` location was in a
  // macro expansion, we don't have visibility into the text, so we assume we've
  // already terminated. However, we note this assumption with
  // `TerminatedByMacro`, because we'll want to handle it somewhat differently
  // for the terminators semicolon and comma. These terminators can be safely
  // associated with the entity when they appear after the macro -- extra
  // semicolons have no effect on the program and a well-formed program won't
  // have multiple commas in a row, so we're guaranteed that there is only one.
  //
  // FIXME: This handling of macros is more conservative than necessary. When
  // the end of the expansion coincides with the end of the node, we can still
  // safely analyze the code. But, it is more complicated, because we need to
  // start by lexing the spelling loc for the first token and then switch to the
  // expansion loc.
  bool TerminatedByMacro = false;
  Lexer->LexFromRawLexer(Tok);
  if (Terminators.empty() || contains(Terminators, Tok))
    Terminated = true;
  else if (EntityLast.isMacroID()) {
    Terminated = true;
    TerminatedByMacro = true;
  }

  // We save the most recent candidate for the exclusive end location.
  SourceLocation End = Tok.getEndLoc();

  while (!Terminated) {
    // Lex the next token we want to possibly expand the range with.
    Lexer->LexFromRawLexer(Tok);

    switch (Tok.getKind()) {
    case tok::eof:
    // Unexpected separators.
    case tok::l_brace:
    case tok::r_brace:
    case tok::comma:
      return End;
    // Whitespace pseudo-tokens.
    case tok::unknown:
      if (startsWithNewline(SM, Tok))
        // Include at least until the end of the line.
        End = Tok.getEndLoc();
      break;
    default:
      if (contains(Terminators, Tok))
        Terminated = true;
      End = Tok.getEndLoc();
      break;
    }
  }

  do {
    // Lex the next token we want to possibly expand the range with.
    Lexer->LexFromRawLexer(Tok);

    switch (Tok.getKind()) {
    case tok::unknown:
      if (startsWithNewline(SM, Tok))
        // We're done, but include this newline.
        return Tok.getEndLoc();
      break;
    case tok::comment:
      // Include any comments we find on the way.
      End = Tok.getEndLoc();
      break;
    case tok::semi:
    case tok::comma:
      if (TerminatedByMacro && contains(Terminators, Tok)) {
        End = Tok.getEndLoc();
        // We've found a real terminator.
        TerminatedByMacro = false;
        break;
      }
      // Found an unrelated token; stop and don't include it.
      return End;
    default:
      // Found an unrelated token; stop and don't include it.
      return End;
    }
  } while (true);
}

// Returns the expected terminator tokens for the given declaration.
//
// If we do not know the correct terminator token, returns an empty set.
//
// There are cases where we have more than one possible terminator (for example,
// we find either a comma or a semicolon after a VarDecl).
static std::set<tok::TokenKind> getTerminators(const Decl &D) {
  if (llvm::isa<RecordDecl>(D) || llvm::isa<UsingDecl>(D))
    return {tok::semi};

  if (llvm::isa<FunctionDecl>(D) || llvm::isa<LinkageSpecDecl>(D))
    return {tok::r_brace, tok::semi};

  if (llvm::isa<VarDecl>(D) || llvm::isa<FieldDecl>(D))
    return {tok::comma, tok::semi};

  return {};
}

// Starting from `Loc`, skips whitespace up to, and including, a single
// newline. Returns the (exclusive) end of any skipped whitespace (that is, the
// location immediately after the whitespace).
static SourceLocation skipWhitespaceAndNewline(const SourceManager &SM,
                                               SourceLocation Loc,
                                               const LangOptions &LangOpts) {
  const char *LocChars = SM.getCharacterData(Loc);
  int i = 0;
  while (isHorizontalWhitespace(LocChars[i]))
    ++i;
  if (isVerticalWhitespace(LocChars[i]))
    ++i;
  return Loc.getLocWithOffset(i);
}

// Is `Loc` separated from any following decl by something meaningful (e.g. an
// empty line, a comment), ignoring horizontal whitespace?  Since this is a
// heuristic, we return false when in doubt.  `Loc` cannot be the first location
// in the file.
static bool atOrBeforeSeparation(const SourceManager &SM, SourceLocation Loc,
                                 const LangOptions &LangOpts) {
  // If the preceding character is a newline, we'll check for an empty line as a
  // separator. However, we can't identify an empty line using tokens, so we
  // analyse the characters. If we try to use tokens, we'll just end up with a
  // whitespace token, whose characters we'd have to analyse anyhow.
  bool Invalid = false;
  const char *LocChars =
      SM.getCharacterData(Loc.getLocWithOffset(-1), &Invalid);
  assert(!Invalid &&
         "Loc must be a valid character and not the first of the source file.");
  if (isVerticalWhitespace(LocChars[0])) {
    for (int i = 1; isWhitespace(LocChars[i]); ++i)
      if (isVerticalWhitespace(LocChars[i]))
        return true;
  }
  // We didn't find an empty line, so lex the next token, skipping past any
  // whitespace we just scanned.
  Token Tok;
  bool Failed = Lexer::getRawToken(Loc, Tok, SM, LangOpts,
                                   /*IgnoreWhiteSpace=*/true);
  if (Failed)
    // Any text that confuses the lexer seems fair to consider a separation.
    return true;

  switch (Tok.getKind()) {
  case tok::comment:
  case tok::l_brace:
  case tok::r_brace:
  case tok::eof:
    return true;
  default:
    return false;
  }
}

CharSourceRange tooling::getAssociatedRange(const Decl &Decl,
                                            ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  const LangOptions &LangOpts = Context.getLangOpts();
  CharSourceRange Range = CharSourceRange::getTokenRange(Decl.getSourceRange());

  // First, expand to the start of the template<> declaration if necessary.
  if (const auto *Record = llvm::dyn_cast<CXXRecordDecl>(&Decl)) {
    if (const auto *T = Record->getDescribedClassTemplate())
      if (SM.isBeforeInTranslationUnit(T->getBeginLoc(), Range.getBegin()))
        Range.setBegin(T->getBeginLoc());
  } else if (const auto *F = llvm::dyn_cast<FunctionDecl>(&Decl)) {
    if (const auto *T = F->getDescribedFunctionTemplate())
      if (SM.isBeforeInTranslationUnit(T->getBeginLoc(), Range.getBegin()))
        Range.setBegin(T->getBeginLoc());
  }

  // Next, expand the end location past trailing comments to include a potential
  // newline at the end of the decl's line.
  Range.setEnd(
      getEntityEndLoc(SM, Decl.getEndLoc(), getTerminators(Decl), LangOpts));
  Range.setTokenRange(false);

  // Expand to include preceeding associated comments. We ignore any comments
  // that are not preceeding the decl, since we've already skipped trailing
  // comments with getEntityEndLoc.
  if (const RawComment *Comment =
          Decl.getASTContext().getRawCommentForDeclNoCache(&Decl))
    // Only include a preceding comment if:
    // * it is *not* separate from the declaration (not including any newline
    //   that immediately follows the comment),
    // * the decl *is* separate from any following entity (so, there are no
    //   other entities the comment could refer to), and
    // * it is not a IfThisThenThat lint check.
    if (SM.isBeforeInTranslationUnit(Comment->getBeginLoc(),
                                     Range.getBegin()) &&
        !atOrBeforeSeparation(
            SM, skipWhitespaceAndNewline(SM, Comment->getEndLoc(), LangOpts),
            LangOpts) &&
        atOrBeforeSeparation(SM, Range.getEnd(), LangOpts)) {
      const StringRef CommentText = Comment->getRawText(SM);
      if (!CommentText.contains("LINT.IfChange") &&
          !CommentText.contains("LINT.ThenChange"))
        Range.setBegin(Comment->getBeginLoc());
    }
  // Add leading attributes.
  for (auto *Attr : Decl.attrs()) {
    if (Attr->getLocation().isInvalid() ||
        !SM.isBeforeInTranslationUnit(Attr->getLocation(), Range.getBegin()))
      continue;
    Range.setBegin(Attr->getLocation());

    // Extend to the left '[[' or '__attribute((' if we saw the attribute,
    // unless it is not a valid location.
    bool Invalid;
    StringRef Source =
        SM.getBufferData(SM.getFileID(Range.getBegin()), &Invalid);
    if (Invalid)
      continue;
    llvm::StringRef BeforeAttr =
        Source.substr(0, SM.getFileOffset(Range.getBegin()));
    llvm::StringRef BeforeAttrStripped = BeforeAttr.rtrim();

    for (llvm::StringRef Prefix : {"[[", "__attribute__(("}) {
      // Handle whitespace between attribute prefix and attribute value.
      if (BeforeAttrStripped.ends_with(Prefix)) {
        // Move start to start position of prefix, which is
        // length(BeforeAttr) - length(BeforeAttrStripped) + length(Prefix)
        // positions to the left.
        Range.setBegin(Range.getBegin().getLocWithOffset(static_cast<int>(
            -BeforeAttr.size() + BeforeAttrStripped.size() - Prefix.size())));
        break;
        // If we didn't see '[[' or '__attribute' it's probably coming from a
        // macro expansion which is already handled by makeFileCharRange(),
        // below.
      }
    }
  }

  // Range.getEnd() is already fully un-expanded by getEntityEndLoc. But,
  // Range.getBegin() may be inside an expansion.
  return Lexer::makeFileCharRange(Range, SM, LangOpts);
}
