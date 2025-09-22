//===- TokenLexer.cpp - Lex from a token stream ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TokenLexer interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/TokenLexer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroArgs.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "clang/Lex/VariadicMacroSupport.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include <cassert>
#include <cstring>
#include <optional>

using namespace clang;

/// Create a TokenLexer for the specified macro with the specified actual
/// arguments.  Note that this ctor takes ownership of the ActualArgs pointer.
void TokenLexer::Init(Token &Tok, SourceLocation ELEnd, MacroInfo *MI,
                      MacroArgs *Actuals) {
  // If the client is reusing a TokenLexer, make sure to free any memory
  // associated with it.
  destroy();

  Macro = MI;
  ActualArgs = Actuals;
  CurTokenIdx = 0;

  ExpandLocStart = Tok.getLocation();
  ExpandLocEnd = ELEnd;
  AtStartOfLine = Tok.isAtStartOfLine();
  HasLeadingSpace = Tok.hasLeadingSpace();
  NextTokGetsSpace = false;
  Tokens = &*Macro->tokens_begin();
  OwnsTokens = false;
  DisableMacroExpansion = false;
  IsReinject = false;
  NumTokens = Macro->tokens_end()-Macro->tokens_begin();
  MacroExpansionStart = SourceLocation();

  SourceManager &SM = PP.getSourceManager();
  MacroStartSLocOffset = SM.getNextLocalOffset();

  if (NumTokens > 0) {
    assert(Tokens[0].getLocation().isValid());
    assert((Tokens[0].getLocation().isFileID() || Tokens[0].is(tok::comment)) &&
           "Macro defined in macro?");
    assert(ExpandLocStart.isValid());

    // Reserve a source location entry chunk for the length of the macro
    // definition. Tokens that get lexed directly from the definition will
    // have their locations pointing inside this chunk. This is to avoid
    // creating separate source location entries for each token.
    MacroDefStart = SM.getExpansionLoc(Tokens[0].getLocation());
    MacroDefLength = Macro->getDefinitionLength(SM);
    MacroExpansionStart = SM.createExpansionLoc(MacroDefStart,
                                                ExpandLocStart,
                                                ExpandLocEnd,
                                                MacroDefLength);
  }

  // If this is a function-like macro, expand the arguments and change
  // Tokens to point to the expanded tokens.
  if (Macro->isFunctionLike() && Macro->getNumParams())
    ExpandFunctionArguments();

  // Mark the macro as currently disabled, so that it is not recursively
  // expanded.  The macro must be disabled only after argument pre-expansion of
  // function-like macro arguments occurs.
  Macro->DisableMacro();
}

/// Create a TokenLexer for the specified token stream.  This does not
/// take ownership of the specified token vector.
void TokenLexer::Init(const Token *TokArray, unsigned NumToks,
                      bool disableMacroExpansion, bool ownsTokens,
                      bool isReinject) {
  assert(!isReinject || disableMacroExpansion);
  // If the client is reusing a TokenLexer, make sure to free any memory
  // associated with it.
  destroy();

  Macro = nullptr;
  ActualArgs = nullptr;
  Tokens = TokArray;
  OwnsTokens = ownsTokens;
  DisableMacroExpansion = disableMacroExpansion;
  IsReinject = isReinject;
  NumTokens = NumToks;
  CurTokenIdx = 0;
  ExpandLocStart = ExpandLocEnd = SourceLocation();
  AtStartOfLine = false;
  HasLeadingSpace = false;
  NextTokGetsSpace = false;
  MacroExpansionStart = SourceLocation();

  // Set HasLeadingSpace/AtStartOfLine so that the first token will be
  // returned unmodified.
  if (NumToks != 0) {
    AtStartOfLine   = TokArray[0].isAtStartOfLine();
    HasLeadingSpace = TokArray[0].hasLeadingSpace();
  }
}

void TokenLexer::destroy() {
  // If this was a function-like macro that actually uses its arguments, delete
  // the expanded tokens.
  if (OwnsTokens) {
    delete [] Tokens;
    Tokens = nullptr;
    OwnsTokens = false;
  }

  // TokenLexer owns its formal arguments.
  if (ActualArgs) ActualArgs->destroy(PP);
}

bool TokenLexer::MaybeRemoveCommaBeforeVaArgs(
    SmallVectorImpl<Token> &ResultToks, bool HasPasteOperator, MacroInfo *Macro,
    unsigned MacroArgNo, Preprocessor &PP) {
  // Is the macro argument __VA_ARGS__?
  if (!Macro->isVariadic() || MacroArgNo != Macro->getNumParams()-1)
    return false;

  // In Microsoft-compatibility mode, a comma is removed in the expansion
  // of " ... , __VA_ARGS__ " if __VA_ARGS__ is empty.  This extension is
  // not supported by gcc.
  if (!HasPasteOperator && !PP.getLangOpts().MSVCCompat)
    return false;

  // GCC removes the comma in the expansion of " ... , ## __VA_ARGS__ " if
  // __VA_ARGS__ is empty, but not in strict C99 mode where there are no
  // named arguments, where it remains.  In all other modes, including C99
  // with GNU extensions, it is removed regardless of named arguments.
  // Microsoft also appears to support this extension, unofficially.
  if (PP.getLangOpts().C99 && !PP.getLangOpts().GNUMode
        && Macro->getNumParams() < 2)
    return false;

  // Is a comma available to be removed?
  if (ResultToks.empty() || !ResultToks.back().is(tok::comma))
    return false;

  // Issue an extension diagnostic for the paste operator.
  if (HasPasteOperator)
    PP.Diag(ResultToks.back().getLocation(), diag::ext_paste_comma);

  // Remove the comma.
  ResultToks.pop_back();

  if (!ResultToks.empty()) {
    // If the comma was right after another paste (e.g. "X##,##__VA_ARGS__"),
    // then removal of the comma should produce a placemarker token (in C99
    // terms) which we model by popping off the previous ##, giving us a plain
    // "X" when __VA_ARGS__ is empty.
    if (ResultToks.back().is(tok::hashhash))
      ResultToks.pop_back();

    // Remember that this comma was elided.
    ResultToks.back().setFlag(Token::CommaAfterElided);
  }

  // Never add a space, even if the comma, ##, or arg had a space.
  NextTokGetsSpace = false;
  return true;
}

void TokenLexer::stringifyVAOPTContents(
    SmallVectorImpl<Token> &ResultToks, const VAOptExpansionContext &VCtx,
    const SourceLocation VAOPTClosingParenLoc) {
  const int NumToksPriorToVAOpt = VCtx.getNumberOfTokensPriorToVAOpt();
  const unsigned int NumVAOptTokens = ResultToks.size() - NumToksPriorToVAOpt;
  Token *const VAOPTTokens =
      NumVAOptTokens ? &ResultToks[NumToksPriorToVAOpt] : nullptr;

  SmallVector<Token, 64> ConcatenatedVAOPTResultToks;
  // FIXME: Should we keep track within VCtx that we did or didnot
  // encounter pasting - and only then perform this loop.

  // Perform token pasting (concatenation) prior to stringization.
  for (unsigned int CurTokenIdx = 0; CurTokenIdx != NumVAOptTokens;
       ++CurTokenIdx) {
    if (VAOPTTokens[CurTokenIdx].is(tok::hashhash)) {
      assert(CurTokenIdx != 0 &&
             "Can not have __VAOPT__ contents begin with a ##");
      Token &LHS = VAOPTTokens[CurTokenIdx - 1];
      pasteTokens(LHS, llvm::ArrayRef(VAOPTTokens, NumVAOptTokens),
                  CurTokenIdx);
      // Replace the token prior to the first ## in this iteration.
      ConcatenatedVAOPTResultToks.back() = LHS;
      if (CurTokenIdx == NumVAOptTokens)
        break;
    }
    ConcatenatedVAOPTResultToks.push_back(VAOPTTokens[CurTokenIdx]);
  }

  ConcatenatedVAOPTResultToks.push_back(VCtx.getEOFTok());
  // Get the SourceLocation that represents the start location within
  // the macro definition that marks where this string is substituted
  // into: i.e. the __VA_OPT__ and the ')' within the spelling of the
  // macro definition, and use it to indicate that the stringified token
  // was generated from that location.
  const SourceLocation ExpansionLocStartWithinMacro =
      getExpansionLocForMacroDefLoc(VCtx.getVAOptLoc());
  const SourceLocation ExpansionLocEndWithinMacro =
      getExpansionLocForMacroDefLoc(VAOPTClosingParenLoc);

  Token StringifiedVAOPT = MacroArgs::StringifyArgument(
      &ConcatenatedVAOPTResultToks[0], PP, VCtx.hasCharifyBefore() /*Charify*/,
      ExpansionLocStartWithinMacro, ExpansionLocEndWithinMacro);

  if (VCtx.getLeadingSpaceForStringifiedToken())
    StringifiedVAOPT.setFlag(Token::LeadingSpace);

  StringifiedVAOPT.setFlag(Token::StringifiedInMacro);
  // Resize (shrink) the token stream to just capture this stringified token.
  ResultToks.resize(NumToksPriorToVAOpt + 1);
  ResultToks.back() = StringifiedVAOPT;
}

/// Expand the arguments of a function-like macro so that we can quickly
/// return preexpanded tokens from Tokens.
void TokenLexer::ExpandFunctionArguments() {
  SmallVector<Token, 128> ResultToks;

  // Loop through 'Tokens', expanding them into ResultToks.  Keep
  // track of whether we change anything.  If not, no need to keep them.  If so,
  // we install the newly expanded sequence as the new 'Tokens' list.
  bool MadeChange = false;

  std::optional<bool> CalledWithVariadicArguments;

  VAOptExpansionContext VCtx(PP);

  for (unsigned I = 0, E = NumTokens; I != E; ++I) {
    const Token &CurTok = Tokens[I];
    // We don't want a space for the next token after a paste
    // operator.  In valid code, the token will get smooshed onto the
    // preceding one anyway. In assembler-with-cpp mode, invalid
    // pastes are allowed through: in this case, we do not want the
    // extra whitespace to be added.  For example, we want ". ## foo"
    // -> ".foo" not ". foo".
    if (I != 0 && !Tokens[I-1].is(tok::hashhash) && CurTok.hasLeadingSpace())
      NextTokGetsSpace = true;

    if (VCtx.isVAOptToken(CurTok)) {
      MadeChange = true;
      assert(Tokens[I + 1].is(tok::l_paren) &&
             "__VA_OPT__ must be followed by '('");

      ++I;             // Skip the l_paren
      VCtx.sawVAOptFollowedByOpeningParens(CurTok.getLocation(),
                                           ResultToks.size());

      continue;
    }

    // We have entered into the __VA_OPT__ context, so handle tokens
    // appropriately.
    if (VCtx.isInVAOpt()) {
      // If we are about to process a token that is either an argument to
      // __VA_OPT__ or its closing rparen, then:
      //  1) If the token is the closing rparen that exits us out of __VA_OPT__,
      //  perform any necessary stringification or placemarker processing,
      //  and/or skip to the next token.
      //  2) else if macro was invoked without variadic arguments skip this
      //  token.
      //  3) else (macro was invoked with variadic arguments) process the token
      //  normally.

      if (Tokens[I].is(tok::l_paren))
        VCtx.sawOpeningParen(Tokens[I].getLocation());
      // Continue skipping tokens within __VA_OPT__ if the macro was not
      // called with variadic arguments, else let the rest of the loop handle
      // this token. Note sawClosingParen() returns true only if the r_paren matches
      // the closing r_paren of the __VA_OPT__.
      if (!Tokens[I].is(tok::r_paren) || !VCtx.sawClosingParen()) {
        // Lazily expand __VA_ARGS__ when we see the first __VA_OPT__.
        if (!CalledWithVariadicArguments) {
          CalledWithVariadicArguments =
              ActualArgs->invokedWithVariadicArgument(Macro, PP);
        }
        if (!*CalledWithVariadicArguments) {
          // Skip this token.
          continue;
        }
        // ... else the macro was called with variadic arguments, and we do not
        // have a closing rparen - so process this token normally.
      } else {
        // Current token is the closing r_paren which marks the end of the
        // __VA_OPT__ invocation, so handle any place-marker pasting (if
        // empty) by removing hashhash either before (if exists) or after. And
        // also stringify the entire contents if VAOPT was preceded by a hash,
        // but do so only after any token concatenation that needs to occur
        // within the contents of VAOPT.

        if (VCtx.hasStringifyOrCharifyBefore()) {
          // Replace all the tokens just added from within VAOPT into a single
          // stringified token. This requires token-pasting to eagerly occur
          // within these tokens. If either the contents of VAOPT were empty
          // or the macro wasn't called with any variadic arguments, the result
          // is a token that represents an empty string.
          stringifyVAOPTContents(ResultToks, VCtx,
                                 /*ClosingParenLoc*/ Tokens[I].getLocation());

        } else if (/*No tokens within VAOPT*/
                   ResultToks.size() == VCtx.getNumberOfTokensPriorToVAOpt()) {
          // Treat VAOPT as a placemarker token.  Eat either the '##' before the
          // RHS/VAOPT (if one exists, suggesting that the LHS (if any) to that
          // hashhash was not a placemarker) or the '##'
          // after VAOPT, but not both.

          if (ResultToks.size() && ResultToks.back().is(tok::hashhash)) {
            ResultToks.pop_back();
          } else if ((I + 1 != E) && Tokens[I + 1].is(tok::hashhash)) {
            ++I; // Skip the following hashhash.
          }
        } else {
          // If there's a ## before the __VA_OPT__, we might have discovered
          // that the __VA_OPT__ begins with a placeholder. We delay action on
          // that to now to avoid messing up our stashed count of tokens before
          // __VA_OPT__.
          if (VCtx.beginsWithPlaceholder()) {
            assert(VCtx.getNumberOfTokensPriorToVAOpt() > 0 &&
                   ResultToks.size() >= VCtx.getNumberOfTokensPriorToVAOpt() &&
                   ResultToks[VCtx.getNumberOfTokensPriorToVAOpt() - 1].is(
                       tok::hashhash) &&
                   "no token paste before __VA_OPT__");
            ResultToks.erase(ResultToks.begin() +
                             VCtx.getNumberOfTokensPriorToVAOpt() - 1);
          }
          // If the expansion of __VA_OPT__ ends with a placeholder, eat any
          // following '##' token.
          if (VCtx.endsWithPlaceholder() && I + 1 != E &&
              Tokens[I + 1].is(tok::hashhash)) {
            ++I;
          }
        }
        VCtx.reset();
        // We processed __VA_OPT__'s closing paren (and the exit out of
        // __VA_OPT__), so skip to the next token.
        continue;
      }
    }

    // If we found the stringify operator, get the argument stringified.  The
    // preprocessor already verified that the following token is a macro
    // parameter or __VA_OPT__ when the #define was lexed.

    if (CurTok.isOneOf(tok::hash, tok::hashat)) {
      int ArgNo = Macro->getParameterNum(Tokens[I+1].getIdentifierInfo());
      assert((ArgNo != -1 || VCtx.isVAOptToken(Tokens[I + 1])) &&
             "Token following # is not an argument or __VA_OPT__!");

      if (ArgNo == -1) {
        // Handle the __VA_OPT__ case.
        VCtx.sawHashOrHashAtBefore(NextTokGetsSpace,
                                   CurTok.is(tok::hashat));
        continue;
      }
      // Else handle the simple argument case.
      SourceLocation ExpansionLocStart =
          getExpansionLocForMacroDefLoc(CurTok.getLocation());
      SourceLocation ExpansionLocEnd =
          getExpansionLocForMacroDefLoc(Tokens[I+1].getLocation());

      bool Charify = CurTok.is(tok::hashat);
      const Token *UnexpArg = ActualArgs->getUnexpArgument(ArgNo);
      Token Res = MacroArgs::StringifyArgument(
          UnexpArg, PP, Charify, ExpansionLocStart, ExpansionLocEnd);
      Res.setFlag(Token::StringifiedInMacro);

      // The stringified/charified string leading space flag gets set to match
      // the #/#@ operator.
      if (NextTokGetsSpace)
        Res.setFlag(Token::LeadingSpace);

      ResultToks.push_back(Res);
      MadeChange = true;
      ++I;  // Skip arg name.
      NextTokGetsSpace = false;
      continue;
    }

    // Find out if there is a paste (##) operator before or after the token.
    bool NonEmptyPasteBefore =
      !ResultToks.empty() && ResultToks.back().is(tok::hashhash);
    bool PasteBefore = I != 0 && Tokens[I-1].is(tok::hashhash);
    bool PasteAfter = I+1 != E && Tokens[I+1].is(tok::hashhash);
    bool RParenAfter = I+1 != E && Tokens[I+1].is(tok::r_paren);

    assert((!NonEmptyPasteBefore || PasteBefore || VCtx.isInVAOpt()) &&
           "unexpected ## in ResultToks");

    // Otherwise, if this is not an argument token, just add the token to the
    // output buffer.
    IdentifierInfo *II = CurTok.getIdentifierInfo();
    int ArgNo = II ? Macro->getParameterNum(II) : -1;
    if (ArgNo == -1) {
      // This isn't an argument, just add it.
      ResultToks.push_back(CurTok);

      if (NextTokGetsSpace) {
        ResultToks.back().setFlag(Token::LeadingSpace);
        NextTokGetsSpace = false;
      } else if (PasteBefore && !NonEmptyPasteBefore)
        ResultToks.back().clearFlag(Token::LeadingSpace);

      continue;
    }

    // An argument is expanded somehow, the result is different than the
    // input.
    MadeChange = true;

    // Otherwise, this is a use of the argument.

    // In Microsoft mode, remove the comma before __VA_ARGS__ to ensure there
    // are no trailing commas if __VA_ARGS__ is empty.
    if (!PasteBefore && ActualArgs->isVarargsElidedUse() &&
        MaybeRemoveCommaBeforeVaArgs(ResultToks,
                                     /*HasPasteOperator=*/false,
                                     Macro, ArgNo, PP))
      continue;

    // If it is not the LHS/RHS of a ## operator, we must pre-expand the
    // argument and substitute the expanded tokens into the result.  This is
    // C99 6.10.3.1p1.
    if (!PasteBefore && !PasteAfter) {
      const Token *ResultArgToks;

      // Only preexpand the argument if it could possibly need it.  This
      // avoids some work in common cases.
      const Token *ArgTok = ActualArgs->getUnexpArgument(ArgNo);
      if (ActualArgs->ArgNeedsPreexpansion(ArgTok, PP))
        ResultArgToks = &ActualArgs->getPreExpArgument(ArgNo, PP)[0];
      else
        ResultArgToks = ArgTok;  // Use non-preexpanded tokens.

      // If the arg token expanded into anything, append it.
      if (ResultArgToks->isNot(tok::eof)) {
        size_t FirstResult = ResultToks.size();
        unsigned NumToks = MacroArgs::getArgLength(ResultArgToks);
        ResultToks.append(ResultArgToks, ResultArgToks+NumToks);

        // In Microsoft-compatibility mode, we follow MSVC's preprocessing
        // behavior by not considering single commas from nested macro
        // expansions as argument separators. Set a flag on the token so we can
        // test for this later when the macro expansion is processed.
        if (PP.getLangOpts().MSVCCompat && NumToks == 1 &&
            ResultToks.back().is(tok::comma))
          ResultToks.back().setFlag(Token::IgnoredComma);

        // If the '##' came from expanding an argument, turn it into 'unknown'
        // to avoid pasting.
        for (Token &Tok : llvm::drop_begin(ResultToks, FirstResult))
          if (Tok.is(tok::hashhash))
            Tok.setKind(tok::unknown);

        if(ExpandLocStart.isValid()) {
          updateLocForMacroArgTokens(CurTok.getLocation(),
                                     ResultToks.begin()+FirstResult,
                                     ResultToks.end());
        }

        // If any tokens were substituted from the argument, the whitespace
        // before the first token should match the whitespace of the arg
        // identifier.
        ResultToks[FirstResult].setFlagValue(Token::LeadingSpace,
                                             NextTokGetsSpace);
        ResultToks[FirstResult].setFlagValue(Token::StartOfLine, false);
        NextTokGetsSpace = false;
      } else {
        // We're creating a placeholder token. Usually this doesn't matter,
        // but it can affect paste behavior when at the start or end of a
        // __VA_OPT__.
        if (NonEmptyPasteBefore) {
          // We're imagining a placeholder token is inserted here. If this is
          // the first token in a __VA_OPT__ after a ##, delete the ##.
          assert(VCtx.isInVAOpt() && "should only happen inside a __VA_OPT__");
          VCtx.hasPlaceholderAfterHashhashAtStart();
        } else if (RParenAfter)
          VCtx.hasPlaceholderBeforeRParen();
      }
      continue;
    }

    // Okay, we have a token that is either the LHS or RHS of a paste (##)
    // argument.  It gets substituted as its non-pre-expanded tokens.
    const Token *ArgToks = ActualArgs->getUnexpArgument(ArgNo);
    unsigned NumToks = MacroArgs::getArgLength(ArgToks);
    if (NumToks) {  // Not an empty argument?
      bool VaArgsPseudoPaste = false;
      // If this is the GNU ", ## __VA_ARGS__" extension, and we just learned
      // that __VA_ARGS__ expands to multiple tokens, avoid a pasting error when
      // the expander tries to paste ',' with the first token of the __VA_ARGS__
      // expansion.
      if (NonEmptyPasteBefore && ResultToks.size() >= 2 &&
          ResultToks[ResultToks.size()-2].is(tok::comma) &&
          (unsigned)ArgNo == Macro->getNumParams()-1 &&
          Macro->isVariadic()) {
        VaArgsPseudoPaste = true;
        // Remove the paste operator, report use of the extension.
        PP.Diag(ResultToks.pop_back_val().getLocation(), diag::ext_paste_comma);
      }

      ResultToks.append(ArgToks, ArgToks+NumToks);

      // If the '##' came from expanding an argument, turn it into 'unknown'
      // to avoid pasting.
      for (Token &Tok : llvm::make_range(ResultToks.end() - NumToks,
                                         ResultToks.end())) {
        if (Tok.is(tok::hashhash))
          Tok.setKind(tok::unknown);
      }

      if (ExpandLocStart.isValid()) {
        updateLocForMacroArgTokens(CurTok.getLocation(),
                                   ResultToks.end()-NumToks, ResultToks.end());
      }

      // Transfer the leading whitespace information from the token
      // (the macro argument) onto the first token of the
      // expansion. Note that we don't do this for the GNU
      // pseudo-paste extension ", ## __VA_ARGS__".
      if (!VaArgsPseudoPaste) {
        ResultToks[ResultToks.size() - NumToks].setFlagValue(Token::StartOfLine,
                                                             false);
        ResultToks[ResultToks.size() - NumToks].setFlagValue(
            Token::LeadingSpace, NextTokGetsSpace);
      }

      NextTokGetsSpace = false;
      continue;
    }

    // If an empty argument is on the LHS or RHS of a paste, the standard (C99
    // 6.10.3.3p2,3) calls for a bunch of placemarker stuff to occur.  We
    // implement this by eating ## operators when a LHS or RHS expands to
    // empty.
    if (PasteAfter) {
      // Discard the argument token and skip (don't copy to the expansion
      // buffer) the paste operator after it.
      ++I;
      continue;
    }

    if (RParenAfter && !NonEmptyPasteBefore)
      VCtx.hasPlaceholderBeforeRParen();

    // If this is on the RHS of a paste operator, we've already copied the
    // paste operator to the ResultToks list, unless the LHS was empty too.
    // Remove it.
    assert(PasteBefore);
    if (NonEmptyPasteBefore) {
      assert(ResultToks.back().is(tok::hashhash));
      // Do not remove the paste operator if it is the one before __VA_OPT__
      // (and we are still processing tokens within VA_OPT).  We handle the case
      // of removing the paste operator if __VA_OPT__ reduces to the notional
      // placemarker above when we encounter the closing paren of VA_OPT.
      if (!VCtx.isInVAOpt() ||
          ResultToks.size() > VCtx.getNumberOfTokensPriorToVAOpt())
        ResultToks.pop_back();
      else
        VCtx.hasPlaceholderAfterHashhashAtStart();
    }

    // If this is the __VA_ARGS__ token, and if the argument wasn't provided,
    // and if the macro had at least one real argument, and if the token before
    // the ## was a comma, remove the comma.  This is a GCC extension which is
    // disabled when using -std=c99.
    if (ActualArgs->isVarargsElidedUse())
      MaybeRemoveCommaBeforeVaArgs(ResultToks,
                                   /*HasPasteOperator=*/true,
                                   Macro, ArgNo, PP);
  }

  // If anything changed, install this as the new Tokens list.
  if (MadeChange) {
    assert(!OwnsTokens && "This would leak if we already own the token list");
    // This is deleted in the dtor.
    NumTokens = ResultToks.size();
    // The tokens will be added to Preprocessor's cache and will be removed
    // when this TokenLexer finishes lexing them.
    Tokens = PP.cacheMacroExpandedTokens(this, ResultToks);

    // The preprocessor cache of macro expanded tokens owns these tokens,not us.
    OwnsTokens = false;
  }
}

/// Checks if two tokens form wide string literal.
static bool isWideStringLiteralFromMacro(const Token &FirstTok,
                                         const Token &SecondTok) {
  return FirstTok.is(tok::identifier) &&
         FirstTok.getIdentifierInfo()->isStr("L") && SecondTok.isLiteral() &&
         SecondTok.stringifiedInMacro();
}

/// Lex - Lex and return a token from this macro stream.
bool TokenLexer::Lex(Token &Tok) {
  // Lexing off the end of the macro, pop this macro off the expansion stack.
  if (isAtEnd()) {
    // If this is a macro (not a token stream), mark the macro enabled now
    // that it is no longer being expanded.
    if (Macro) Macro->EnableMacro();

    Tok.startToken();
    Tok.setFlagValue(Token::StartOfLine , AtStartOfLine);
    Tok.setFlagValue(Token::LeadingSpace, HasLeadingSpace || NextTokGetsSpace);
    if (CurTokenIdx == 0)
      Tok.setFlag(Token::LeadingEmptyMacro);
    return PP.HandleEndOfTokenLexer(Tok);
  }

  SourceManager &SM = PP.getSourceManager();

  // If this is the first token of the expanded result, we inherit spacing
  // properties later.
  bool isFirstToken = CurTokenIdx == 0;

  // Get the next token to return.
  Tok = Tokens[CurTokenIdx++];
  if (IsReinject)
    Tok.setFlag(Token::IsReinjected);

  bool TokenIsFromPaste = false;

  // If this token is followed by a token paste (##) operator, paste the tokens!
  // Note that ## is a normal token when not expanding a macro.
  if (!isAtEnd() && Macro &&
      (Tokens[CurTokenIdx].is(tok::hashhash) ||
       // Special processing of L#x macros in -fms-compatibility mode.
       // Microsoft compiler is able to form a wide string literal from
       // 'L#macro_arg' construct in a function-like macro.
       (PP.getLangOpts().MSVCCompat &&
        isWideStringLiteralFromMacro(Tok, Tokens[CurTokenIdx])))) {
    // When handling the microsoft /##/ extension, the final token is
    // returned by pasteTokens, not the pasted token.
    if (pasteTokens(Tok))
      return true;

    TokenIsFromPaste = true;
  }

  // The token's current location indicate where the token was lexed from.  We
  // need this information to compute the spelling of the token, but any
  // diagnostics for the expanded token should appear as if they came from
  // ExpansionLoc.  Pull this information together into a new SourceLocation
  // that captures all of this.
  if (ExpandLocStart.isValid() &&   // Don't do this for token streams.
      // Check that the token's location was not already set properly.
      SM.isBeforeInSLocAddrSpace(Tok.getLocation(), MacroStartSLocOffset)) {
    SourceLocation instLoc;
    if (Tok.is(tok::comment)) {
      instLoc = SM.createExpansionLoc(Tok.getLocation(),
                                      ExpandLocStart,
                                      ExpandLocEnd,
                                      Tok.getLength());
    } else {
      instLoc = getExpansionLocForMacroDefLoc(Tok.getLocation());
    }

    Tok.setLocation(instLoc);
  }

  // If this is the first token, set the lexical properties of the token to
  // match the lexical properties of the macro identifier.
  if (isFirstToken) {
    Tok.setFlagValue(Token::StartOfLine , AtStartOfLine);
    Tok.setFlagValue(Token::LeadingSpace, HasLeadingSpace);
  } else {
    // If this is not the first token, we may still need to pass through
    // leading whitespace if we've expanded a macro.
    if (AtStartOfLine) Tok.setFlag(Token::StartOfLine);
    if (HasLeadingSpace) Tok.setFlag(Token::LeadingSpace);
  }
  AtStartOfLine = false;
  HasLeadingSpace = false;

  // Handle recursive expansion!
  if (!Tok.isAnnotation() && Tok.getIdentifierInfo() != nullptr) {
    // Change the kind of this identifier to the appropriate token kind, e.g.
    // turning "for" into a keyword.
    IdentifierInfo *II = Tok.getIdentifierInfo();
    Tok.setKind(II->getTokenID());

    // If this identifier was poisoned and from a paste, emit an error.  This
    // won't be handled by Preprocessor::HandleIdentifier because this is coming
    // from a macro expansion.
    if (II->isPoisoned() && TokenIsFromPaste) {
      PP.HandlePoisonedIdentifier(Tok);
    }

    if (!DisableMacroExpansion && II->isHandleIdentifierCase())
      return PP.HandleIdentifier(Tok);
  }

  // Otherwise, return a normal token.
  return true;
}

bool TokenLexer::pasteTokens(Token &Tok) {
  return pasteTokens(Tok, llvm::ArrayRef(Tokens, NumTokens), CurTokenIdx);
}

/// LHSTok is the LHS of a ## operator, and CurTokenIdx is the ##
/// operator.  Read the ## and RHS, and paste the LHS/RHS together.  If there
/// are more ## after it, chomp them iteratively.  Return the result as LHSTok.
/// If this returns true, the caller should immediately return the token.
bool TokenLexer::pasteTokens(Token &LHSTok, ArrayRef<Token> TokenStream,
                             unsigned int &CurIdx) {
  assert(CurIdx > 0 && "## can not be the first token within tokens");
  assert((TokenStream[CurIdx].is(tok::hashhash) ||
         (PP.getLangOpts().MSVCCompat &&
          isWideStringLiteralFromMacro(LHSTok, TokenStream[CurIdx]))) &&
             "Token at this Index must be ## or part of the MSVC 'L "
             "#macro-arg' pasting pair");

  // MSVC: If previous token was pasted, this must be a recovery from an invalid
  // paste operation. Ignore spaces before this token to mimic MSVC output.
  // Required for generating valid UUID strings in some MS headers.
  if (PP.getLangOpts().MicrosoftExt && (CurIdx >= 2) &&
      TokenStream[CurIdx - 2].is(tok::hashhash))
    LHSTok.clearFlag(Token::LeadingSpace);

  SmallString<128> Buffer;
  const char *ResultTokStrPtr = nullptr;
  SourceLocation StartLoc = LHSTok.getLocation();
  SourceLocation PasteOpLoc;

  auto IsAtEnd = [&TokenStream, &CurIdx] {
    return TokenStream.size() == CurIdx;
  };

  do {
    // Consume the ## operator if any.
    PasteOpLoc = TokenStream[CurIdx].getLocation();
    if (TokenStream[CurIdx].is(tok::hashhash))
      ++CurIdx;
    assert(!IsAtEnd() && "No token on the RHS of a paste operator!");

    // Get the RHS token.
    const Token &RHS = TokenStream[CurIdx];

    // Allocate space for the result token.  This is guaranteed to be enough for
    // the two tokens.
    Buffer.resize(LHSTok.getLength() + RHS.getLength());

    // Get the spelling of the LHS token in Buffer.
    const char *BufPtr = &Buffer[0];
    bool Invalid = false;
    unsigned LHSLen = PP.getSpelling(LHSTok, BufPtr, &Invalid);
    if (BufPtr != &Buffer[0])   // Really, we want the chars in Buffer!
      memcpy(&Buffer[0], BufPtr, LHSLen);
    if (Invalid)
      return true;

    BufPtr = Buffer.data() + LHSLen;
    unsigned RHSLen = PP.getSpelling(RHS, BufPtr, &Invalid);
    if (Invalid)
      return true;
    if (RHSLen && BufPtr != &Buffer[LHSLen])
      // Really, we want the chars in Buffer!
      memcpy(&Buffer[LHSLen], BufPtr, RHSLen);

    // Trim excess space.
    Buffer.resize(LHSLen+RHSLen);

    // Plop the pasted result (including the trailing newline and null) into a
    // scratch buffer where we can lex it.
    Token ResultTokTmp;
    ResultTokTmp.startToken();

    // Claim that the tmp token is a string_literal so that we can get the
    // character pointer back from CreateString in getLiteralData().
    ResultTokTmp.setKind(tok::string_literal);
    PP.CreateString(Buffer, ResultTokTmp);
    SourceLocation ResultTokLoc = ResultTokTmp.getLocation();
    ResultTokStrPtr = ResultTokTmp.getLiteralData();

    // Lex the resultant pasted token into Result.
    Token Result;

    if (LHSTok.isAnyIdentifier() && RHS.isAnyIdentifier()) {
      // Common paste case: identifier+identifier = identifier.  Avoid creating
      // a lexer and other overhead.
      PP.IncrementPasteCounter(true);
      Result.startToken();
      Result.setKind(tok::raw_identifier);
      Result.setRawIdentifierData(ResultTokStrPtr);
      Result.setLocation(ResultTokLoc);
      Result.setLength(LHSLen+RHSLen);
    } else {
      PP.IncrementPasteCounter(false);

      assert(ResultTokLoc.isFileID() &&
             "Should be a raw location into scratch buffer");
      SourceManager &SourceMgr = PP.getSourceManager();
      FileID LocFileID = SourceMgr.getFileID(ResultTokLoc);

      bool Invalid = false;
      const char *ScratchBufStart
        = SourceMgr.getBufferData(LocFileID, &Invalid).data();
      if (Invalid)
        return false;

      // Make a lexer to lex this string from.  Lex just this one token.
      // Make a lexer object so that we lex and expand the paste result.
      Lexer TL(SourceMgr.getLocForStartOfFile(LocFileID),
               PP.getLangOpts(), ScratchBufStart,
               ResultTokStrPtr, ResultTokStrPtr+LHSLen+RHSLen);

      // Lex a token in raw mode.  This way it won't look up identifiers
      // automatically, lexing off the end will return an eof token, and
      // warnings are disabled.  This returns true if the result token is the
      // entire buffer.
      bool isInvalid = !TL.LexFromRawLexer(Result);

      // If we got an EOF token, we didn't form even ONE token.  For example, we
      // did "/ ## /" to get "//".
      isInvalid |= Result.is(tok::eof);

      // If pasting the two tokens didn't form a full new token, this is an
      // error.  This occurs with "x ## +"  and other stuff.  Return with LHSTok
      // unmodified and with RHS as the next token to lex.
      if (isInvalid) {
        // Explicitly convert the token location to have proper expansion
        // information so that the user knows where it came from.
        SourceManager &SM = PP.getSourceManager();
        SourceLocation Loc =
          SM.createExpansionLoc(PasteOpLoc, ExpandLocStart, ExpandLocEnd, 2);

        // Test for the Microsoft extension of /##/ turning into // here on the
        // error path.
        if (PP.getLangOpts().MicrosoftExt && LHSTok.is(tok::slash) &&
            RHS.is(tok::slash)) {
          HandleMicrosoftCommentPaste(LHSTok, Loc);
          return true;
        }

        // Do not emit the error when preprocessing assembler code.
        if (!PP.getLangOpts().AsmPreprocessor) {
          // If we're in microsoft extensions mode, downgrade this from a hard
          // error to an extension that defaults to an error.  This allows
          // disabling it.
          PP.Diag(Loc, PP.getLangOpts().MicrosoftExt ? diag::ext_pp_bad_paste_ms
                                                     : diag::err_pp_bad_paste)
              << Buffer;
        }

        // An error has occurred so exit loop.
        break;
      }

      // Turn ## into 'unknown' to avoid # ## # from looking like a paste
      // operator.
      if (Result.is(tok::hashhash))
        Result.setKind(tok::unknown);
    }

    // Transfer properties of the LHS over the Result.
    Result.setFlagValue(Token::StartOfLine , LHSTok.isAtStartOfLine());
    Result.setFlagValue(Token::LeadingSpace, LHSTok.hasLeadingSpace());

    // Finally, replace LHS with the result, consume the RHS, and iterate.
    ++CurIdx;
    LHSTok = Result;
  } while (!IsAtEnd() && TokenStream[CurIdx].is(tok::hashhash));

  SourceLocation EndLoc = TokenStream[CurIdx - 1].getLocation();

  // The token's current location indicate where the token was lexed from.  We
  // need this information to compute the spelling of the token, but any
  // diagnostics for the expanded token should appear as if the token was
  // expanded from the full ## expression. Pull this information together into
  // a new SourceLocation that captures all of this.
  SourceManager &SM = PP.getSourceManager();
  if (StartLoc.isFileID())
    StartLoc = getExpansionLocForMacroDefLoc(StartLoc);
  if (EndLoc.isFileID())
    EndLoc = getExpansionLocForMacroDefLoc(EndLoc);
  FileID MacroFID = SM.getFileID(MacroExpansionStart);
  while (SM.getFileID(StartLoc) != MacroFID)
    StartLoc = SM.getImmediateExpansionRange(StartLoc).getBegin();
  while (SM.getFileID(EndLoc) != MacroFID)
    EndLoc = SM.getImmediateExpansionRange(EndLoc).getEnd();

  LHSTok.setLocation(SM.createExpansionLoc(LHSTok.getLocation(), StartLoc, EndLoc,
                                        LHSTok.getLength()));

  // Now that we got the result token, it will be subject to expansion.  Since
  // token pasting re-lexes the result token in raw mode, identifier information
  // isn't looked up.  As such, if the result is an identifier, look up id info.
  if (LHSTok.is(tok::raw_identifier)) {
    // Look up the identifier info for the token.  We disabled identifier lookup
    // by saying we're skipping contents, so we need to do this manually.
    PP.LookUpIdentifierInfo(LHSTok);
  }
  return false;
}

/// isNextTokenLParen - If the next token lexed will pop this macro off the
/// expansion stack, return 2.  If the next unexpanded token is a '(', return
/// 1, otherwise return 0.
unsigned TokenLexer::isNextTokenLParen() const {
  // Out of tokens?
  if (isAtEnd())
    return 2;
  return Tokens[CurTokenIdx].is(tok::l_paren);
}

/// isParsingPreprocessorDirective - Return true if we are in the middle of a
/// preprocessor directive.
bool TokenLexer::isParsingPreprocessorDirective() const {
  return Tokens[NumTokens-1].is(tok::eod) && !isAtEnd();
}

/// HandleMicrosoftCommentPaste - In microsoft compatibility mode, /##/ pastes
/// together to form a comment that comments out everything in the current
/// macro, other active macros, and anything left on the current physical
/// source line of the expanded buffer.  Handle this by returning the
/// first token on the next line.
void TokenLexer::HandleMicrosoftCommentPaste(Token &Tok, SourceLocation OpLoc) {
  PP.Diag(OpLoc, diag::ext_comment_paste_microsoft);

  // We 'comment out' the rest of this macro by just ignoring the rest of the
  // tokens that have not been lexed yet, if any.

  // Since this must be a macro, mark the macro enabled now that it is no longer
  // being expanded.
  assert(Macro && "Token streams can't paste comments");
  Macro->EnableMacro();

  PP.HandleMicrosoftCommentPaste(Tok);
}

/// If \arg loc is a file ID and points inside the current macro
/// definition, returns the appropriate source location pointing at the
/// macro expansion source location entry, otherwise it returns an invalid
/// SourceLocation.
SourceLocation
TokenLexer::getExpansionLocForMacroDefLoc(SourceLocation loc) const {
  assert(ExpandLocStart.isValid() && MacroExpansionStart.isValid() &&
         "Not appropriate for token streams");
  assert(loc.isValid() && loc.isFileID());

  SourceManager &SM = PP.getSourceManager();
  assert(SM.isInSLocAddrSpace(loc, MacroDefStart, MacroDefLength) &&
         "Expected loc to come from the macro definition");

  SourceLocation::UIntTy relativeOffset = 0;
  SM.isInSLocAddrSpace(loc, MacroDefStart, MacroDefLength, &relativeOffset);
  return MacroExpansionStart.getLocWithOffset(relativeOffset);
}

/// Finds the tokens that are consecutive (from the same FileID)
/// creates a single SLocEntry, and assigns SourceLocations to each token that
/// point to that SLocEntry. e.g for
///   assert(foo == bar);
/// There will be a single SLocEntry for the "foo == bar" chunk and locations
/// for the 'foo', '==', 'bar' tokens will point inside that chunk.
///
/// \arg begin_tokens will be updated to a position past all the found
/// consecutive tokens.
static void updateConsecutiveMacroArgTokens(SourceManager &SM,
                                            SourceLocation ExpandLoc,
                                            Token *&begin_tokens,
                                            Token * end_tokens) {
  assert(begin_tokens + 1 < end_tokens);
  SourceLocation BeginLoc = begin_tokens->getLocation();
  llvm::MutableArrayRef<Token> All(begin_tokens, end_tokens);
  llvm::MutableArrayRef<Token> Partition;

  auto NearLast = [&, Last = BeginLoc](SourceLocation Loc) mutable {
    // The maximum distance between two consecutive tokens in a partition.
    // This is an important trick to avoid using too much SourceLocation address
    // space!
    static constexpr SourceLocation::IntTy MaxDistance = 50;
    auto Distance = Loc.getRawEncoding() - Last.getRawEncoding();
    Last = Loc;
    return Distance <= MaxDistance;
  };

  // Partition the tokens by their FileID.
  // This is a hot function, and calling getFileID can be expensive, the
  // implementation is optimized by reducing the number of getFileID.
  if (BeginLoc.isFileID()) {
    // Consecutive tokens not written in macros must be from the same file.
    // (Neither #include nor eof can occur inside a macro argument.)
    Partition = All.take_while([&](const Token &T) {
      return T.getLocation().isFileID() && NearLast(T.getLocation());
    });
  } else {
    // Call getFileID once to calculate the bounds, and use the cheaper
    // sourcelocation-against-bounds comparison.
    FileID BeginFID = SM.getFileID(BeginLoc);
    SourceLocation Limit =
        SM.getComposedLoc(BeginFID, SM.getFileIDSize(BeginFID));
    Partition = All.take_while([&](const Token &T) {
      // NOTE: the Limit is included! The lexer recovery only ever inserts a
      // single token past the end of the FileID, specifically the ) when a
      // macro-arg containing a comma should be guarded by parentheses.
      //
      // It is safe to include the Limit here because SourceManager allocates
      // FileSize + 1 for each SLocEntry.
      //
      // See https://github.com/llvm/llvm-project/issues/60722.
      return T.getLocation() >= BeginLoc && T.getLocation() <= Limit
         &&  NearLast(T.getLocation());
    });
  }
  assert(!Partition.empty());

  // For the consecutive tokens, find the length of the SLocEntry to contain
  // all of them.
  SourceLocation::UIntTy FullLength =
      Partition.back().getEndLoc().getRawEncoding() -
      Partition.front().getLocation().getRawEncoding();
  // Create a macro expansion SLocEntry that will "contain" all of the tokens.
  SourceLocation Expansion =
      SM.createMacroArgExpansionLoc(BeginLoc, ExpandLoc, FullLength);

#ifdef EXPENSIVE_CHECKS
  assert(llvm::all_of(Partition.drop_front(),
                      [&SM, ID = SM.getFileID(Partition.front().getLocation())](
                          const Token &T) {
                        return ID == SM.getFileID(T.getLocation());
                      }) &&
         "Must have the same FIleID!");
#endif
  // Change the location of the tokens from the spelling location to the new
  // expanded location.
  for (Token& T : Partition) {
    SourceLocation::IntTy RelativeOffset =
        T.getLocation().getRawEncoding() - BeginLoc.getRawEncoding();
    T.setLocation(Expansion.getLocWithOffset(RelativeOffset));
  }
  begin_tokens = &Partition.back() + 1;
}

/// Creates SLocEntries and updates the locations of macro argument
/// tokens to their new expanded locations.
///
/// \param ArgIdSpellLoc the location of the macro argument id inside the macro
/// definition.
void TokenLexer::updateLocForMacroArgTokens(SourceLocation ArgIdSpellLoc,
                                            Token *begin_tokens,
                                            Token *end_tokens) {
  SourceManager &SM = PP.getSourceManager();

  SourceLocation ExpandLoc =
      getExpansionLocForMacroDefLoc(ArgIdSpellLoc);

  while (begin_tokens < end_tokens) {
    // If there's only one token just create a SLocEntry for it.
    if (end_tokens - begin_tokens == 1) {
      Token &Tok = *begin_tokens;
      Tok.setLocation(SM.createMacroArgExpansionLoc(Tok.getLocation(),
                                                    ExpandLoc,
                                                    Tok.getLength()));
      return;
    }

    updateConsecutiveMacroArgTokens(SM, ExpandLoc, begin_tokens, end_tokens);
  }
}

void TokenLexer::PropagateLineStartLeadingSpaceInfo(Token &Result) {
  AtStartOfLine = Result.isAtStartOfLine();
  HasLeadingSpace = Result.hasLeadingSpace();
}
