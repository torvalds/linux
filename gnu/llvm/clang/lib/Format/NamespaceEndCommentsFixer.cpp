//===--- NamespaceEndCommentsFixer.cpp --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements NamespaceEndCommentsFixer, a TokenAnalyzer that
/// fixes namespace end comments.
///
//===----------------------------------------------------------------------===//

#include "NamespaceEndCommentsFixer.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Regex.h"

#define DEBUG_TYPE "namespace-end-comments-fixer"

namespace clang {
namespace format {

namespace {
// Iterates all tokens starting from StartTok to EndTok and apply Fn to all
// tokens between them including StartTok and EndTok. Returns the token after
// EndTok.
const FormatToken *
processTokens(const FormatToken *Tok, tok::TokenKind StartTok,
              tok::TokenKind EndTok,
              llvm::function_ref<void(const FormatToken *)> Fn) {
  if (!Tok || Tok->isNot(StartTok))
    return Tok;
  int NestLevel = 0;
  do {
    if (Tok->is(StartTok))
      ++NestLevel;
    else if (Tok->is(EndTok))
      --NestLevel;
    if (Fn)
      Fn(Tok);
    Tok = Tok->getNextNonComment();
  } while (Tok && NestLevel > 0);
  return Tok;
}

const FormatToken *skipAttribute(const FormatToken *Tok) {
  if (!Tok)
    return nullptr;
  if (Tok->isAttribute()) {
    Tok = Tok->getNextNonComment();
    Tok = processTokens(Tok, tok::l_paren, tok::r_paren, nullptr);
  } else if (Tok->is(tok::l_square)) {
    Tok = processTokens(Tok, tok::l_square, tok::r_square, nullptr);
  }
  return Tok;
}

// Computes the name of a namespace given the namespace token.
// Returns "" for anonymous namespace.
std::string computeName(const FormatToken *NamespaceTok) {
  assert(NamespaceTok &&
         NamespaceTok->isOneOf(tok::kw_namespace, TT_NamespaceMacro) &&
         "expecting a namespace token");
  std::string name;
  const FormatToken *Tok = NamespaceTok->getNextNonComment();
  if (NamespaceTok->is(TT_NamespaceMacro)) {
    // Collects all the non-comment tokens between opening parenthesis
    // and closing parenthesis or comma.
    assert(Tok && Tok->is(tok::l_paren) && "expected an opening parenthesis");
    Tok = Tok->getNextNonComment();
    while (Tok && !Tok->isOneOf(tok::r_paren, tok::comma)) {
      name += Tok->TokenText;
      Tok = Tok->getNextNonComment();
    }
    return name;
  }
  Tok = skipAttribute(Tok);

  std::string FirstNSName;
  // For `namespace [[foo]] A::B::inline C {` or
  // `namespace MACRO1 MACRO2 A::B::inline C {`, returns "A::B::inline C".
  // Peek for the first '::' (or '{' or '(')) and then return all tokens from
  // one token before that up until the '{'. A '(' might be a macro with
  // arguments.
  const FormatToken *FirstNSTok = nullptr;
  while (Tok && !Tok->isOneOf(tok::l_brace, tok::coloncolon, tok::l_paren)) {
    if (FirstNSTok)
      FirstNSName += FirstNSTok->TokenText;
    FirstNSTok = Tok;
    Tok = Tok->getNextNonComment();
  }

  if (FirstNSTok)
    Tok = FirstNSTok;
  Tok = skipAttribute(Tok);

  FirstNSTok = nullptr;
  // Add everything from '(' to ')'.
  auto AddToken = [&name](const FormatToken *Tok) { name += Tok->TokenText; };
  bool IsPrevColoncolon = false;
  bool HasColoncolon = false;
  bool IsPrevInline = false;
  bool NameFinished = false;
  // If we found '::' in name, then it's the name. Otherwise, we can't tell
  // which one is name. For example, `namespace A B {`.
  while (Tok && Tok->isNot(tok::l_brace)) {
    if (FirstNSTok) {
      if (!IsPrevInline && HasColoncolon && !IsPrevColoncolon) {
        if (FirstNSTok->is(tok::l_paren)) {
          FirstNSTok = Tok =
              processTokens(FirstNSTok, tok::l_paren, tok::r_paren, AddToken);
          continue;
        }
        if (FirstNSTok->isNot(tok::coloncolon)) {
          NameFinished = true;
          break;
        }
      }
      name += FirstNSTok->TokenText;
      IsPrevColoncolon = FirstNSTok->is(tok::coloncolon);
      HasColoncolon = HasColoncolon || IsPrevColoncolon;
      if (FirstNSTok->is(tok::kw_inline)) {
        name += " ";
        IsPrevInline = true;
      }
    }
    FirstNSTok = Tok;
    Tok = Tok->getNextNonComment();
    const FormatToken *TokAfterAttr = skipAttribute(Tok);
    if (TokAfterAttr != Tok)
      FirstNSTok = Tok = TokAfterAttr;
  }
  if (!NameFinished && FirstNSTok && FirstNSTok->isNot(tok::l_brace))
    name += FirstNSTok->TokenText;
  if (FirstNSName.empty() || HasColoncolon)
    return name;
  return name.empty() ? FirstNSName : FirstNSName + " " + name;
}

std::string computeEndCommentText(StringRef NamespaceName, bool AddNewline,
                                  const FormatToken *NamespaceTok,
                                  unsigned SpacesToAdd) {
  std::string text = "//";
  text.append(SpacesToAdd, ' ');
  text += NamespaceTok->TokenText;
  if (NamespaceTok->is(TT_NamespaceMacro))
    text += "(";
  else if (!NamespaceName.empty())
    text += ' ';
  text += NamespaceName;
  if (NamespaceTok->is(TT_NamespaceMacro))
    text += ")";
  if (AddNewline)
    text += '\n';
  return text;
}

bool hasEndComment(const FormatToken *RBraceTok) {
  return RBraceTok->Next && RBraceTok->Next->is(tok::comment);
}

bool validEndComment(const FormatToken *RBraceTok, StringRef NamespaceName,
                     const FormatToken *NamespaceTok) {
  assert(hasEndComment(RBraceTok));
  const FormatToken *Comment = RBraceTok->Next;

  // Matches a valid namespace end comment.
  // Valid namespace end comments don't need to be edited.
  static const llvm::Regex NamespaceCommentPattern =
      llvm::Regex("^/[/*] *(end (of )?)? *(anonymous|unnamed)? *"
                  "namespace( +([a-zA-Z0-9:_ ]+))?\\.? *(\\*/)?$",
                  llvm::Regex::IgnoreCase);
  static const llvm::Regex NamespaceMacroCommentPattern =
      llvm::Regex("^/[/*] *(end (of )?)? *(anonymous|unnamed)? *"
                  "([a-zA-Z0-9_]+)\\(([a-zA-Z0-9:_]*|\".+\")\\)\\.? *(\\*/)?$",
                  llvm::Regex::IgnoreCase);

  SmallVector<StringRef, 8> Groups;
  if (NamespaceTok->is(TT_NamespaceMacro) &&
      NamespaceMacroCommentPattern.match(Comment->TokenText, &Groups)) {
    StringRef NamespaceTokenText = Groups.size() > 4 ? Groups[4] : "";
    // The name of the macro must be used.
    if (NamespaceTokenText != NamespaceTok->TokenText)
      return false;
  } else if (NamespaceTok->isNot(tok::kw_namespace) ||
             !NamespaceCommentPattern.match(Comment->TokenText, &Groups)) {
    // Comment does not match regex.
    return false;
  }
  StringRef NamespaceNameInComment = Groups.size() > 5 ? Groups[5].rtrim() : "";
  // Anonymous namespace comments must not mention a namespace name.
  if (NamespaceName.empty() && !NamespaceNameInComment.empty())
    return false;
  StringRef AnonymousInComment = Groups.size() > 3 ? Groups[3] : "";
  // Named namespace comments must not mention anonymous namespace.
  if (!NamespaceName.empty() && !AnonymousInComment.empty())
    return false;
  if (NamespaceNameInComment == NamespaceName)
    return true;

  // Has namespace comment flowed onto the next line.
  // } // namespace
  //   // verylongnamespacenamethatdidnotfitonthepreviouscommentline
  if (!(Comment->Next && Comment->Next->is(TT_LineComment)))
    return false;

  static const llvm::Regex CommentPattern = llvm::Regex(
      "^/[/*] *( +([a-zA-Z0-9:_]+))?\\.? *(\\*/)?$", llvm::Regex::IgnoreCase);

  // Pull out just the comment text.
  if (!CommentPattern.match(Comment->Next->TokenText, &Groups))
    return false;
  NamespaceNameInComment = Groups.size() > 2 ? Groups[2] : "";

  return NamespaceNameInComment == NamespaceName;
}

void addEndComment(const FormatToken *RBraceTok, StringRef EndCommentText,
                   const SourceManager &SourceMgr,
                   tooling::Replacements *Fixes) {
  auto EndLoc = RBraceTok->Tok.getEndLoc();
  auto Range = CharSourceRange::getCharRange(EndLoc, EndLoc);
  auto Err = Fixes->add(tooling::Replacement(SourceMgr, Range, EndCommentText));
  if (Err) {
    llvm::errs() << "Error while adding namespace end comment: "
                 << llvm::toString(std::move(Err)) << "\n";
  }
}

void updateEndComment(const FormatToken *RBraceTok, StringRef EndCommentText,
                      const SourceManager &SourceMgr,
                      tooling::Replacements *Fixes) {
  assert(hasEndComment(RBraceTok));
  const FormatToken *Comment = RBraceTok->Next;
  auto Range = CharSourceRange::getCharRange(Comment->getStartOfNonWhitespace(),
                                             Comment->Tok.getEndLoc());
  auto Err = Fixes->add(tooling::Replacement(SourceMgr, Range, EndCommentText));
  if (Err) {
    llvm::errs() << "Error while updating namespace end comment: "
                 << llvm::toString(std::move(Err)) << "\n";
  }
}
} // namespace

const FormatToken *
getNamespaceToken(const AnnotatedLine *Line,
                  const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
  if (!Line->Affected || Line->InPPDirective || !Line->startsWith(tok::r_brace))
    return nullptr;
  size_t StartLineIndex = Line->MatchingOpeningBlockLineIndex;
  if (StartLineIndex == UnwrappedLine::kInvalidIndex)
    return nullptr;
  assert(StartLineIndex < AnnotatedLines.size());
  const FormatToken *NamespaceTok = AnnotatedLines[StartLineIndex]->First;
  if (NamespaceTok->is(tok::l_brace)) {
    // "namespace" keyword can be on the line preceding '{', e.g. in styles
    // where BraceWrapping.AfterNamespace is true.
    if (StartLineIndex > 0) {
      NamespaceTok = AnnotatedLines[StartLineIndex - 1]->First;
      if (AnnotatedLines[StartLineIndex - 1]->endsWith(tok::semi))
        return nullptr;
    }
  }

  return NamespaceTok->getNamespaceToken();
}

StringRef
getNamespaceTokenText(const AnnotatedLine *Line,
                      const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
  const FormatToken *NamespaceTok = getNamespaceToken(Line, AnnotatedLines);
  return NamespaceTok ? NamespaceTok->TokenText : StringRef();
}

NamespaceEndCommentsFixer::NamespaceEndCommentsFixer(const Environment &Env,
                                                     const FormatStyle &Style)
    : TokenAnalyzer(Env, Style) {}

std::pair<tooling::Replacements, unsigned> NamespaceEndCommentsFixer::analyze(
    TokenAnnotator &Annotator, SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
    FormatTokenLexer &Tokens) {
  const SourceManager &SourceMgr = Env.getSourceManager();
  AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
  tooling::Replacements Fixes;

  // Spin through the lines and ensure we have balanced braces.
  int Braces = 0;
  for (AnnotatedLine *Line : AnnotatedLines) {
    FormatToken *Tok = Line->First;
    while (Tok) {
      Braces += Tok->is(tok::l_brace) ? 1 : Tok->is(tok::r_brace) ? -1 : 0;
      Tok = Tok->Next;
    }
  }
  // Don't attempt to comment unbalanced braces or this can
  // lead to comments being placed on the closing brace which isn't
  // the matching brace of the namespace. (occurs during incomplete editing).
  if (Braces != 0)
    return {Fixes, 0};

  std::string AllNamespaceNames;
  size_t StartLineIndex = SIZE_MAX;
  StringRef NamespaceTokenText;
  unsigned int CompactedNamespacesCount = 0;
  for (size_t I = 0, E = AnnotatedLines.size(); I != E; ++I) {
    const AnnotatedLine *EndLine = AnnotatedLines[I];
    const FormatToken *NamespaceTok =
        getNamespaceToken(EndLine, AnnotatedLines);
    if (!NamespaceTok)
      continue;
    FormatToken *RBraceTok = EndLine->First;
    if (RBraceTok->Finalized)
      continue;
    RBraceTok->Finalized = true;
    const FormatToken *EndCommentPrevTok = RBraceTok;
    // Namespaces often end with '};'. In that case, attach namespace end
    // comments to the semicolon tokens.
    if (RBraceTok->Next && RBraceTok->Next->is(tok::semi))
      EndCommentPrevTok = RBraceTok->Next;
    if (StartLineIndex == SIZE_MAX)
      StartLineIndex = EndLine->MatchingOpeningBlockLineIndex;
    std::string NamespaceName = computeName(NamespaceTok);
    if (Style.CompactNamespaces) {
      if (CompactedNamespacesCount == 0)
        NamespaceTokenText = NamespaceTok->TokenText;
      if ((I + 1 < E) &&
          NamespaceTokenText ==
              getNamespaceTokenText(AnnotatedLines[I + 1], AnnotatedLines) &&
          StartLineIndex - CompactedNamespacesCount - 1 ==
              AnnotatedLines[I + 1]->MatchingOpeningBlockLineIndex &&
          !AnnotatedLines[I + 1]->First->Finalized) {
        if (hasEndComment(EndCommentPrevTok)) {
          // remove end comment, it will be merged in next one
          updateEndComment(EndCommentPrevTok, std::string(), SourceMgr, &Fixes);
        }
        ++CompactedNamespacesCount;
        if (!NamespaceName.empty())
          AllNamespaceNames = "::" + NamespaceName + AllNamespaceNames;
        continue;
      }
      NamespaceName += AllNamespaceNames;
      CompactedNamespacesCount = 0;
      AllNamespaceNames = std::string();
    }
    // The next token in the token stream after the place where the end comment
    // token must be. This is either the next token on the current line or the
    // first token on the next line.
    const FormatToken *EndCommentNextTok = EndCommentPrevTok->Next;
    if (EndCommentNextTok && EndCommentNextTok->is(tok::comment))
      EndCommentNextTok = EndCommentNextTok->Next;
    if (!EndCommentNextTok && I + 1 < E)
      EndCommentNextTok = AnnotatedLines[I + 1]->First;
    bool AddNewline = EndCommentNextTok &&
                      EndCommentNextTok->NewlinesBefore == 0 &&
                      EndCommentNextTok->isNot(tok::eof);
    const std::string EndCommentText =
        computeEndCommentText(NamespaceName, AddNewline, NamespaceTok,
                              Style.SpacesInLineCommentPrefix.Minimum);
    if (!hasEndComment(EndCommentPrevTok)) {
      unsigned LineCount = 0;
      for (auto J = StartLineIndex + 1; J < I; ++J)
        LineCount += AnnotatedLines[J]->size();
      if (LineCount > Style.ShortNamespaceLines) {
        addEndComment(EndCommentPrevTok,
                      std::string(Style.SpacesBeforeTrailingComments, ' ') +
                          EndCommentText,
                      SourceMgr, &Fixes);
      }
    } else if (!validEndComment(EndCommentPrevTok, NamespaceName,
                                NamespaceTok)) {
      updateEndComment(EndCommentPrevTok, EndCommentText, SourceMgr, &Fixes);
    }
    StartLineIndex = SIZE_MAX;
  }
  return {Fixes, 0};
}

} // namespace format
} // namespace clang
