//===- Pragma.cpp - Pragma registration and handling ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the PragmaHandler/PragmaTable interfaces and implements
// pragma related methods of the Preprocessor class.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Pragma.h"
#include "clang/Basic/CLWarnings.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorLexer.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Token.h"
#include "clang/Lex/TokenLexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Timer.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace clang;

// Out-of-line destructor to provide a home for the class.
PragmaHandler::~PragmaHandler() = default;

//===----------------------------------------------------------------------===//
// EmptyPragmaHandler Implementation.
//===----------------------------------------------------------------------===//

EmptyPragmaHandler::EmptyPragmaHandler(StringRef Name) : PragmaHandler(Name) {}

void EmptyPragmaHandler::HandlePragma(Preprocessor &PP,
                                      PragmaIntroducer Introducer,
                                      Token &FirstToken) {}

//===----------------------------------------------------------------------===//
// PragmaNamespace Implementation.
//===----------------------------------------------------------------------===//

/// FindHandler - Check to see if there is already a handler for the
/// specified name.  If not, return the handler for the null identifier if it
/// exists, otherwise return null.  If IgnoreNull is true (the default) then
/// the null handler isn't returned on failure to match.
PragmaHandler *PragmaNamespace::FindHandler(StringRef Name,
                                            bool IgnoreNull) const {
  auto I = Handlers.find(Name);
  if (I != Handlers.end())
    return I->getValue().get();
  if (IgnoreNull)
    return nullptr;
  I = Handlers.find(StringRef());
  if (I != Handlers.end())
    return I->getValue().get();
  return nullptr;
}

void PragmaNamespace::AddPragma(PragmaHandler *Handler) {
  assert(!Handlers.count(Handler->getName()) &&
         "A handler with this name is already registered in this namespace");
  Handlers[Handler->getName()].reset(Handler);
}

void PragmaNamespace::RemovePragmaHandler(PragmaHandler *Handler) {
  auto I = Handlers.find(Handler->getName());
  assert(I != Handlers.end() &&
         "Handler not registered in this namespace");
  // Release ownership back to the caller.
  I->getValue().release();
  Handlers.erase(I);
}

void PragmaNamespace::HandlePragma(Preprocessor &PP,
                                   PragmaIntroducer Introducer, Token &Tok) {
  // Read the 'namespace' that the directive is in, e.g. STDC.  Do not macro
  // expand it, the user can have a STDC #define, that should not affect this.
  PP.LexUnexpandedToken(Tok);

  // Get the handler for this token.  If there is no handler, ignore the pragma.
  PragmaHandler *Handler
    = FindHandler(Tok.getIdentifierInfo() ? Tok.getIdentifierInfo()->getName()
                                          : StringRef(),
                  /*IgnoreNull=*/false);
  if (!Handler) {
    PP.Diag(Tok, diag::warn_pragma_ignored);
    return;
  }

  // Otherwise, pass it down.
  Handler->HandlePragma(PP, Introducer, Tok);
}

//===----------------------------------------------------------------------===//
// Preprocessor Pragma Directive Handling.
//===----------------------------------------------------------------------===//

namespace {
// TokenCollector provides the option to collect tokens that were "read"
// and return them to the stream to be read later.
// Currently used when reading _Pragma/__pragma directives.
struct TokenCollector {
  Preprocessor &Self;
  bool Collect;
  SmallVector<Token, 3> Tokens;
  Token &Tok;

  void lex() {
    if (Collect)
      Tokens.push_back(Tok);
    Self.Lex(Tok);
  }

  void revert() {
    assert(Collect && "did not collect tokens");
    assert(!Tokens.empty() && "collected unexpected number of tokens");

    // Push the ( "string" ) tokens into the token stream.
    auto Toks = std::make_unique<Token[]>(Tokens.size());
    std::copy(Tokens.begin() + 1, Tokens.end(), Toks.get());
    Toks[Tokens.size() - 1] = Tok;
    Self.EnterTokenStream(std::move(Toks), Tokens.size(),
                          /*DisableMacroExpansion*/ true,
                          /*IsReinject*/ true);

    // ... and return the pragma token unchanged.
    Tok = *Tokens.begin();
  }
};
} // namespace

/// HandlePragmaDirective - The "\#pragma" directive has been parsed.  Lex the
/// rest of the pragma, passing it to the registered pragma handlers.
void Preprocessor::HandlePragmaDirective(PragmaIntroducer Introducer) {
  if (Callbacks)
    Callbacks->PragmaDirective(Introducer.Loc, Introducer.Kind);

  if (!PragmasEnabled)
    return;

  ++NumPragma;

  // Invoke the first level of pragma handlers which reads the namespace id.
  Token Tok;
  PragmaHandlers->HandlePragma(*this, Introducer, Tok);

  // If the pragma handler didn't read the rest of the line, consume it now.
  if ((CurTokenLexer && CurTokenLexer->isParsingPreprocessorDirective())
   || (CurPPLexer && CurPPLexer->ParsingPreprocessorDirective))
    DiscardUntilEndOfDirective();
}

/// Handle_Pragma - Read a _Pragma directive, slice it up, process it, then
/// return the first token after the directive.  The _Pragma token has just
/// been read into 'Tok'.
void Preprocessor::Handle_Pragma(Token &Tok) {
  // C11 6.10.3.4/3:
  //   all pragma unary operator expressions within [a completely
  //   macro-replaced preprocessing token sequence] are [...] processed [after
  //   rescanning is complete]
  //
  // This means that we execute _Pragma operators in two cases:
  //
  //  1) on token sequences that would otherwise be produced as the output of
  //     phase 4 of preprocessing, and
  //  2) on token sequences formed as the macro-replaced token sequence of a
  //     macro argument
  //
  // Case #2 appears to be a wording bug: only _Pragmas that would survive to
  // the end of phase 4 should actually be executed. Discussion on the WG14
  // mailing list suggests that a _Pragma operator is notionally checked early,
  // but only pragmas that survive to the end of phase 4 should be executed.
  //
  // In Case #2, we check the syntax now, but then put the tokens back into the
  // token stream for later consumption.

  TokenCollector Toks = {*this, InMacroArgPreExpansion, {}, Tok};

  // Remember the pragma token location.
  SourceLocation PragmaLoc = Tok.getLocation();

  // Read the '('.
  Toks.lex();
  if (Tok.isNot(tok::l_paren)) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }

  // Read the '"..."'.
  Toks.lex();
  if (!tok::isStringLiteral(Tok.getKind())) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    // Skip bad tokens, and the ')', if present.
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::eof))
      Lex(Tok);
    while (Tok.isNot(tok::r_paren) &&
           !Tok.isAtStartOfLine() &&
           Tok.isNot(tok::eof))
      Lex(Tok);
    if (Tok.is(tok::r_paren))
      Lex(Tok);
    return;
  }

  if (Tok.hasUDSuffix()) {
    Diag(Tok, diag::err_invalid_string_udl);
    // Skip this token, and the ')', if present.
    Lex(Tok);
    if (Tok.is(tok::r_paren))
      Lex(Tok);
    return;
  }

  // Remember the string.
  Token StrTok = Tok;

  // Read the ')'.
  Toks.lex();
  if (Tok.isNot(tok::r_paren)) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }

  // If we're expanding a macro argument, put the tokens back.
  if (InMacroArgPreExpansion) {
    Toks.revert();
    return;
  }

  SourceLocation RParenLoc = Tok.getLocation();
  bool Invalid = false;
  SmallString<64> StrVal;
  StrVal.resize(StrTok.getLength());
  StringRef StrValRef = getSpelling(StrTok, StrVal, &Invalid);
  if (Invalid) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }

  assert(StrValRef.size() <= StrVal.size());

  // If the token was spelled somewhere else, copy it.
  if (StrValRef.begin() != StrVal.begin())
    StrVal.assign(StrValRef);
  // Truncate if necessary.
  else if (StrValRef.size() != StrVal.size())
    StrVal.resize(StrValRef.size());

  // The _Pragma is lexically sound.  Destringize according to C11 6.10.9.1.
  prepare_PragmaString(StrVal);

  // Plop the string (including the newline and trailing null) into a buffer
  // where we can lex it.
  Token TmpTok;
  TmpTok.startToken();
  CreateString(StrVal, TmpTok);
  SourceLocation TokLoc = TmpTok.getLocation();

  // Make and enter a lexer object so that we lex and expand the tokens just
  // like any others.
  Lexer *TL = Lexer::Create_PragmaLexer(TokLoc, PragmaLoc, RParenLoc,
                                        StrVal.size(), *this);

  EnterSourceFileWithLexer(TL, nullptr);

  // With everything set up, lex this as a #pragma directive.
  HandlePragmaDirective({PIK__Pragma, PragmaLoc});

  // Finally, return whatever came after the pragma directive.
  return Lex(Tok);
}

void clang::prepare_PragmaString(SmallVectorImpl<char> &StrVal) {
  if (StrVal[0] == 'L' || StrVal[0] == 'U' ||
      (StrVal[0] == 'u' && StrVal[1] != '8'))
    StrVal.erase(StrVal.begin());
  else if (StrVal[0] == 'u')
    StrVal.erase(StrVal.begin(), StrVal.begin() + 2);

  if (StrVal[0] == 'R') {
    // FIXME: C++11 does not specify how to handle raw-string-literals here.
    // We strip off the 'R', the quotes, the d-char-sequences, and the parens.
    assert(StrVal[1] == '"' && StrVal[StrVal.size() - 1] == '"' &&
           "Invalid raw string token!");

    // Measure the length of the d-char-sequence.
    unsigned NumDChars = 0;
    while (StrVal[2 + NumDChars] != '(') {
      assert(NumDChars < (StrVal.size() - 5) / 2 &&
             "Invalid raw string token!");
      ++NumDChars;
    }
    assert(StrVal[StrVal.size() - 2 - NumDChars] == ')');

    // Remove 'R " d-char-sequence' and 'd-char-sequence "'. We'll replace the
    // parens below.
    StrVal.erase(StrVal.begin(), StrVal.begin() + 2 + NumDChars);
    StrVal.erase(StrVal.end() - 1 - NumDChars, StrVal.end());
  } else {
    assert(StrVal[0] == '"' && StrVal[StrVal.size()-1] == '"' &&
           "Invalid string token!");

    // Remove escaped quotes and escapes.
    unsigned ResultPos = 1;
    for (size_t i = 1, e = StrVal.size() - 1; i != e; ++i) {
      // Skip escapes.  \\ -> '\' and \" -> '"'.
      if (StrVal[i] == '\\' && i + 1 < e &&
          (StrVal[i + 1] == '\\' || StrVal[i + 1] == '"'))
        ++i;
      StrVal[ResultPos++] = StrVal[i];
    }
    StrVal.erase(StrVal.begin() + ResultPos, StrVal.end() - 1);
  }

  // Remove the front quote, replacing it with a space, so that the pragma
  // contents appear to have a space before them.
  StrVal[0] = ' ';

  // Replace the terminating quote with a \n.
  StrVal[StrVal.size() - 1] = '\n';
}

/// HandleMicrosoft__pragma - Like Handle_Pragma except the pragma text
/// is not enclosed within a string literal.
void Preprocessor::HandleMicrosoft__pragma(Token &Tok) {
  // During macro pre-expansion, check the syntax now but put the tokens back
  // into the token stream for later consumption. Same as Handle_Pragma.
  TokenCollector Toks = {*this, InMacroArgPreExpansion, {}, Tok};

  // Remember the pragma token location.
  SourceLocation PragmaLoc = Tok.getLocation();

  // Read the '('.
  Toks.lex();
  if (Tok.isNot(tok::l_paren)) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }

  // Get the tokens enclosed within the __pragma(), as well as the final ')'.
  SmallVector<Token, 32> PragmaToks;
  int NumParens = 0;
  Toks.lex();
  while (Tok.isNot(tok::eof)) {
    PragmaToks.push_back(Tok);
    if (Tok.is(tok::l_paren))
      NumParens++;
    else if (Tok.is(tok::r_paren) && NumParens-- == 0)
      break;
    Toks.lex();
  }

  if (Tok.is(tok::eof)) {
    Diag(PragmaLoc, diag::err_unterminated___pragma);
    return;
  }

  // If we're expanding a macro argument, put the tokens back.
  if (InMacroArgPreExpansion) {
    Toks.revert();
    return;
  }

  PragmaToks.front().setFlag(Token::LeadingSpace);

  // Replace the ')' with an EOD to mark the end of the pragma.
  PragmaToks.back().setKind(tok::eod);

  Token *TokArray = new Token[PragmaToks.size()];
  std::copy(PragmaToks.begin(), PragmaToks.end(), TokArray);

  // Push the tokens onto the stack.
  EnterTokenStream(TokArray, PragmaToks.size(), true, true,
                   /*IsReinject*/ false);

  // With everything set up, lex this as a #pragma directive.
  HandlePragmaDirective({PIK___pragma, PragmaLoc});

  // Finally, return whatever came after the pragma directive.
  return Lex(Tok);
}

/// HandlePragmaOnce - Handle \#pragma once.  OnceTok is the 'once'.
void Preprocessor::HandlePragmaOnce(Token &OnceTok) {
  // Don't honor the 'once' when handling the primary source file, unless
  // this is a prefix to a TU, which indicates we're generating a PCH file, or
  // when the main file is a header (e.g. when -xc-header is provided on the
  // commandline).
  if (isInPrimaryFile() && TUKind != TU_Prefix && !getLangOpts().IsHeaderFile) {
    Diag(OnceTok, diag::pp_pragma_once_in_main_file);
    return;
  }

  // Get the current file lexer we're looking at.  Ignore _Pragma 'files' etc.
  // Mark the file as a once-only file now.
  HeaderInfo.MarkFileIncludeOnce(*getCurrentFileLexer()->getFileEntry());
}

void Preprocessor::HandlePragmaMark(Token &MarkTok) {
  assert(CurPPLexer && "No current lexer?");

  SmallString<64> Buffer;
  CurLexer->ReadToEndOfLine(&Buffer);
  if (Callbacks)
    Callbacks->PragmaMark(MarkTok.getLocation(), Buffer);
}

/// HandlePragmaPoison - Handle \#pragma GCC poison.  PoisonTok is the 'poison'.
void Preprocessor::HandlePragmaPoison() {
  Token Tok;

  while (true) {
    // Read the next token to poison.  While doing this, pretend that we are
    // skipping while reading the identifier to poison.
    // This avoids errors on code like:
    //   #pragma GCC poison X
    //   #pragma GCC poison X
    if (CurPPLexer) CurPPLexer->LexingRawMode = true;
    LexUnexpandedToken(Tok);
    if (CurPPLexer) CurPPLexer->LexingRawMode = false;

    // If we reached the end of line, we're done.
    if (Tok.is(tok::eod)) return;

    // Can only poison identifiers.
    if (Tok.isNot(tok::raw_identifier)) {
      Diag(Tok, diag::err_pp_invalid_poison);
      return;
    }

    // Look up the identifier info for the token.  We disabled identifier lookup
    // by saying we're skipping contents, so we need to do this manually.
    IdentifierInfo *II = LookUpIdentifierInfo(Tok);

    // Already poisoned.
    if (II->isPoisoned()) continue;

    // If this is a macro identifier, emit a warning.
    if (isMacroDefined(II))
      Diag(Tok, diag::pp_poisoning_existing_macro);

    // Finally, poison it!
    II->setIsPoisoned();
    if (II->isFromAST())
      II->setChangedSinceDeserialization();
  }
}

/// HandlePragmaSystemHeader - Implement \#pragma GCC system_header.  We know
/// that the whole directive has been parsed.
void Preprocessor::HandlePragmaSystemHeader(Token &SysHeaderTok) {
  if (isInPrimaryFile()) {
    Diag(SysHeaderTok, diag::pp_pragma_sysheader_in_main_file);
    return;
  }

  // Get the current file lexer we're looking at.  Ignore _Pragma 'files' etc.
  PreprocessorLexer *TheLexer = getCurrentFileLexer();

  // Mark the file as a system header.
  HeaderInfo.MarkFileSystemHeader(*TheLexer->getFileEntry());

  PresumedLoc PLoc = SourceMgr.getPresumedLoc(SysHeaderTok.getLocation());
  if (PLoc.isInvalid())
    return;

  unsigned FilenameID = SourceMgr.getLineTableFilenameID(PLoc.getFilename());

  // Notify the client, if desired, that we are in a new source file.
  if (Callbacks)
    Callbacks->FileChanged(SysHeaderTok.getLocation(),
                           PPCallbacks::SystemHeaderPragma, SrcMgr::C_System);

  // Emit a line marker.  This will change any source locations from this point
  // forward to realize they are in a system header.
  // Create a line note with this information.
  SourceMgr.AddLineNote(SysHeaderTok.getLocation(), PLoc.getLine() + 1,
                        FilenameID, /*IsEntry=*/false, /*IsExit=*/false,
                        SrcMgr::C_System);
}

/// HandlePragmaDependency - Handle \#pragma GCC dependency "foo" blah.
void Preprocessor::HandlePragmaDependency(Token &DependencyTok) {
  Token FilenameTok;
  if (LexHeaderName(FilenameTok, /*AllowConcatenation*/false))
    return;

  // If the next token wasn't a header-name, diagnose the error.
  if (FilenameTok.isNot(tok::header_name)) {
    Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
    return;
  }

  // Reserve a buffer to get the spelling.
  SmallString<128> FilenameBuffer;
  bool Invalid = false;
  StringRef Filename = getSpelling(FilenameTok, FilenameBuffer, &Invalid);
  if (Invalid)
    return;

  bool isAngled =
    GetIncludeFilenameSpelling(FilenameTok.getLocation(), Filename);
  // If GetIncludeFilenameSpelling set the start ptr to null, there was an
  // error.
  if (Filename.empty())
    return;

  // Search include directories for this file.
  OptionalFileEntryRef File =
      LookupFile(FilenameTok.getLocation(), Filename, isAngled, nullptr,
                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  if (!File) {
    if (!SuppressIncludeNotFoundError)
      Diag(FilenameTok, diag::err_pp_file_not_found) << Filename;
    return;
  }

  OptionalFileEntryRef CurFile = getCurrentFileLexer()->getFileEntry();

  // If this file is older than the file it depends on, emit a diagnostic.
  if (CurFile && CurFile->getModificationTime() < File->getModificationTime()) {
    // Lex tokens at the end of the message and include them in the message.
    std::string Message;
    Lex(DependencyTok);
    while (DependencyTok.isNot(tok::eod)) {
      Message += getSpelling(DependencyTok) + " ";
      Lex(DependencyTok);
    }

    // Remove the trailing ' ' if present.
    if (!Message.empty())
      Message.erase(Message.end()-1);
    Diag(FilenameTok, diag::pp_out_of_date_dependency) << Message;
  }
}

/// ParsePragmaPushOrPopMacro - Handle parsing of pragma push_macro/pop_macro.
/// Return the IdentifierInfo* associated with the macro to push or pop.
IdentifierInfo *Preprocessor::ParsePragmaPushOrPopMacro(Token &Tok) {
  // Remember the pragma token location.
  Token PragmaTok = Tok;

  // Read the '('.
  Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    Diag(PragmaTok.getLocation(), diag::err_pragma_push_pop_macro_malformed)
      << getSpelling(PragmaTok);
    return nullptr;
  }

  // Read the macro name string.
  Lex(Tok);
  if (Tok.isNot(tok::string_literal)) {
    Diag(PragmaTok.getLocation(), diag::err_pragma_push_pop_macro_malformed)
      << getSpelling(PragmaTok);
    return nullptr;
  }

  if (Tok.hasUDSuffix()) {
    Diag(Tok, diag::err_invalid_string_udl);
    return nullptr;
  }

  // Remember the macro string.
  std::string StrVal = getSpelling(Tok);

  // Read the ')'.
  Lex(Tok);
  if (Tok.isNot(tok::r_paren)) {
    Diag(PragmaTok.getLocation(), diag::err_pragma_push_pop_macro_malformed)
      << getSpelling(PragmaTok);
    return nullptr;
  }

  assert(StrVal[0] == '"' && StrVal[StrVal.size()-1] == '"' &&
         "Invalid string token!");

  // Create a Token from the string.
  Token MacroTok;
  MacroTok.startToken();
  MacroTok.setKind(tok::raw_identifier);
  CreateString(StringRef(&StrVal[1], StrVal.size() - 2), MacroTok);

  // Get the IdentifierInfo of MacroToPushTok.
  return LookUpIdentifierInfo(MacroTok);
}

/// Handle \#pragma push_macro.
///
/// The syntax is:
/// \code
///   #pragma push_macro("macro")
/// \endcode
void Preprocessor::HandlePragmaPushMacro(Token &PushMacroTok) {
  // Parse the pragma directive and get the macro IdentifierInfo*.
  IdentifierInfo *IdentInfo = ParsePragmaPushOrPopMacro(PushMacroTok);
  if (!IdentInfo) return;

  // Get the MacroInfo associated with IdentInfo.
  MacroInfo *MI = getMacroInfo(IdentInfo);

  if (MI) {
    // Allow the original MacroInfo to be redefined later.
    MI->setIsAllowRedefinitionsWithoutWarning(true);
  }

  // Push the cloned MacroInfo so we can retrieve it later.
  PragmaPushMacroInfo[IdentInfo].push_back(MI);
}

/// Handle \#pragma pop_macro.
///
/// The syntax is:
/// \code
///   #pragma pop_macro("macro")
/// \endcode
void Preprocessor::HandlePragmaPopMacro(Token &PopMacroTok) {
  SourceLocation MessageLoc = PopMacroTok.getLocation();

  // Parse the pragma directive and get the macro IdentifierInfo*.
  IdentifierInfo *IdentInfo = ParsePragmaPushOrPopMacro(PopMacroTok);
  if (!IdentInfo) return;

  // Find the vector<MacroInfo*> associated with the macro.
  llvm::DenseMap<IdentifierInfo *, std::vector<MacroInfo *>>::iterator iter =
    PragmaPushMacroInfo.find(IdentInfo);
  if (iter != PragmaPushMacroInfo.end()) {
    // Forget the MacroInfo currently associated with IdentInfo.
    if (MacroInfo *MI = getMacroInfo(IdentInfo)) {
      if (MI->isWarnIfUnused())
        WarnUnusedMacroLocs.erase(MI->getDefinitionLoc());
      appendMacroDirective(IdentInfo, AllocateUndefMacroDirective(MessageLoc));
    }

    // Get the MacroInfo we want to reinstall.
    MacroInfo *MacroToReInstall = iter->second.back();

    if (MacroToReInstall)
      // Reinstall the previously pushed macro.
      appendDefMacroDirective(IdentInfo, MacroToReInstall, MessageLoc);

    // Pop PragmaPushMacroInfo stack.
    iter->second.pop_back();
    if (iter->second.empty())
      PragmaPushMacroInfo.erase(iter);
  } else {
    Diag(MessageLoc, diag::warn_pragma_pop_macro_no_push)
      << IdentInfo->getName();
  }
}

void Preprocessor::HandlePragmaIncludeAlias(Token &Tok) {
  // We will either get a quoted filename or a bracketed filename, and we
  // have to track which we got.  The first filename is the source name,
  // and the second name is the mapped filename.  If the first is quoted,
  // the second must be as well (cannot mix and match quotes and brackets).

  // Get the open paren
  Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::warn_pragma_include_alias_expected) << "(";
    return;
  }

  // We expect either a quoted string literal, or a bracketed name
  Token SourceFilenameTok;
  if (LexHeaderName(SourceFilenameTok))
    return;

  StringRef SourceFileName;
  SmallString<128> FileNameBuffer;
  if (SourceFilenameTok.is(tok::header_name)) {
    SourceFileName = getSpelling(SourceFilenameTok, FileNameBuffer);
  } else {
    Diag(Tok, diag::warn_pragma_include_alias_expected_filename);
    return;
  }
  FileNameBuffer.clear();

  // Now we expect a comma, followed by another include name
  Lex(Tok);
  if (Tok.isNot(tok::comma)) {
    Diag(Tok, diag::warn_pragma_include_alias_expected) << ",";
    return;
  }

  Token ReplaceFilenameTok;
  if (LexHeaderName(ReplaceFilenameTok))
    return;

  StringRef ReplaceFileName;
  if (ReplaceFilenameTok.is(tok::header_name)) {
    ReplaceFileName = getSpelling(ReplaceFilenameTok, FileNameBuffer);
  } else {
    Diag(Tok, diag::warn_pragma_include_alias_expected_filename);
    return;
  }

  // Finally, we expect the closing paren
  Lex(Tok);
  if (Tok.isNot(tok::r_paren)) {
    Diag(Tok, diag::warn_pragma_include_alias_expected) << ")";
    return;
  }

  // Now that we have the source and target filenames, we need to make sure
  // they're both of the same type (angled vs non-angled)
  StringRef OriginalSource = SourceFileName;

  bool SourceIsAngled =
    GetIncludeFilenameSpelling(SourceFilenameTok.getLocation(),
                                SourceFileName);
  bool ReplaceIsAngled =
    GetIncludeFilenameSpelling(ReplaceFilenameTok.getLocation(),
                                ReplaceFileName);
  if (!SourceFileName.empty() && !ReplaceFileName.empty() &&
      (SourceIsAngled != ReplaceIsAngled)) {
    unsigned int DiagID;
    if (SourceIsAngled)
      DiagID = diag::warn_pragma_include_alias_mismatch_angle;
    else
      DiagID = diag::warn_pragma_include_alias_mismatch_quote;

    Diag(SourceFilenameTok.getLocation(), DiagID)
      << SourceFileName
      << ReplaceFileName;

    return;
  }

  // Now we can let the include handler know about this mapping
  getHeaderSearchInfo().AddIncludeAlias(OriginalSource, ReplaceFileName);
}

// Lex a component of a module name: either an identifier or a string literal;
// for components that can be expressed both ways, the two forms are equivalent.
static bool LexModuleNameComponent(
    Preprocessor &PP, Token &Tok,
    std::pair<IdentifierInfo *, SourceLocation> &ModuleNameComponent,
    bool First) {
  PP.LexUnexpandedToken(Tok);
  if (Tok.is(tok::string_literal) && !Tok.hasUDSuffix()) {
    StringLiteralParser Literal(Tok, PP);
    if (Literal.hadError)
      return true;
    ModuleNameComponent = std::make_pair(
        PP.getIdentifierInfo(Literal.GetString()), Tok.getLocation());
  } else if (!Tok.isAnnotation() && Tok.getIdentifierInfo()) {
    ModuleNameComponent =
        std::make_pair(Tok.getIdentifierInfo(), Tok.getLocation());
  } else {
    PP.Diag(Tok.getLocation(), diag::err_pp_expected_module_name) << First;
    return true;
  }
  return false;
}

static bool LexModuleName(
    Preprocessor &PP, Token &Tok,
    llvm::SmallVectorImpl<std::pair<IdentifierInfo *, SourceLocation>>
        &ModuleName) {
  while (true) {
    std::pair<IdentifierInfo*, SourceLocation> NameComponent;
    if (LexModuleNameComponent(PP, Tok, NameComponent, ModuleName.empty()))
      return true;
    ModuleName.push_back(NameComponent);

    PP.LexUnexpandedToken(Tok);
    if (Tok.isNot(tok::period))
      return false;
  }
}

void Preprocessor::HandlePragmaModuleBuild(Token &Tok) {
  SourceLocation Loc = Tok.getLocation();

  std::pair<IdentifierInfo *, SourceLocation> ModuleNameLoc;
  if (LexModuleNameComponent(*this, Tok, ModuleNameLoc, true))
    return;
  IdentifierInfo *ModuleName = ModuleNameLoc.first;

  LexUnexpandedToken(Tok);
  if (Tok.isNot(tok::eod)) {
    Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma";
    DiscardUntilEndOfDirective();
  }

  CurLexer->LexingRawMode = true;

  auto TryConsumeIdentifier = [&](StringRef Ident) -> bool {
    if (Tok.getKind() != tok::raw_identifier ||
        Tok.getRawIdentifier() != Ident)
      return false;
    CurLexer->Lex(Tok);
    return true;
  };

  // Scan forward looking for the end of the module.
  const char *Start = CurLexer->getBufferLocation();
  const char *End = nullptr;
  unsigned NestingLevel = 1;
  while (true) {
    End = CurLexer->getBufferLocation();
    CurLexer->Lex(Tok);

    if (Tok.is(tok::eof)) {
      Diag(Loc, diag::err_pp_module_build_missing_end);
      break;
    }

    if (Tok.isNot(tok::hash) || !Tok.isAtStartOfLine()) {
      // Token was part of module; keep going.
      continue;
    }

    // We hit something directive-shaped; check to see if this is the end
    // of the module build.
    CurLexer->ParsingPreprocessorDirective = true;
    CurLexer->Lex(Tok);
    if (TryConsumeIdentifier("pragma") && TryConsumeIdentifier("clang") &&
        TryConsumeIdentifier("module")) {
      if (TryConsumeIdentifier("build"))
        // #pragma clang module build -> entering a nested module build.
        ++NestingLevel;
      else if (TryConsumeIdentifier("endbuild")) {
        // #pragma clang module endbuild -> leaving a module build.
        if (--NestingLevel == 0)
          break;
      }
      // We should either be looking at the EOD or more of the current directive
      // preceding the EOD. Either way we can ignore this token and keep going.
      assert(Tok.getKind() != tok::eof && "missing EOD before EOF");
    }
  }

  CurLexer->LexingRawMode = false;

  // Load the extracted text as a preprocessed module.
  assert(CurLexer->getBuffer().begin() <= Start &&
         Start <= CurLexer->getBuffer().end() &&
         CurLexer->getBuffer().begin() <= End &&
         End <= CurLexer->getBuffer().end() &&
         "module source range not contained within same file buffer");
  TheModuleLoader.createModuleFromSource(Loc, ModuleName->getName(),
                                         StringRef(Start, End - Start));
}

void Preprocessor::HandlePragmaHdrstop(Token &Tok) {
  Lex(Tok);
  if (Tok.is(tok::l_paren)) {
    Diag(Tok.getLocation(), diag::warn_pp_hdrstop_filename_ignored);

    std::string FileName;
    if (!LexStringLiteral(Tok, FileName, "pragma hdrstop", false))
      return;

    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      return;
    }
    Lex(Tok);
  }
  if (Tok.isNot(tok::eod))
    Diag(Tok.getLocation(), diag::ext_pp_extra_tokens_at_eol)
        << "pragma hdrstop";

  if (creatingPCHWithPragmaHdrStop() &&
      SourceMgr.isInMainFile(Tok.getLocation())) {
    assert(CurLexer && "no lexer for #pragma hdrstop processing");
    Token &Result = Tok;
    Result.startToken();
    CurLexer->FormTokenWithChars(Result, CurLexer->BufferEnd, tok::eof);
    CurLexer->cutOffLexing();
  }
  if (usingPCHWithPragmaHdrStop())
    SkippingUntilPragmaHdrStop = false;
}

/// AddPragmaHandler - Add the specified pragma handler to the preprocessor.
/// If 'Namespace' is non-null, then it is a token required to exist on the
/// pragma line before the pragma string starts, e.g. "STDC" or "GCC".
void Preprocessor::AddPragmaHandler(StringRef Namespace,
                                    PragmaHandler *Handler) {
  PragmaNamespace *InsertNS = PragmaHandlers.get();

  // If this is specified to be in a namespace, step down into it.
  if (!Namespace.empty()) {
    // If there is already a pragma handler with the name of this namespace,
    // we either have an error (directive with the same name as a namespace) or
    // we already have the namespace to insert into.
    if (PragmaHandler *Existing = PragmaHandlers->FindHandler(Namespace)) {
      InsertNS = Existing->getIfNamespace();
      assert(InsertNS != nullptr && "Cannot have a pragma namespace and pragma"
             " handler with the same name!");
    } else {
      // Otherwise, this namespace doesn't exist yet, create and insert the
      // handler for it.
      InsertNS = new PragmaNamespace(Namespace);
      PragmaHandlers->AddPragma(InsertNS);
    }
  }

  // Check to make sure we don't already have a pragma for this identifier.
  assert(!InsertNS->FindHandler(Handler->getName()) &&
         "Pragma handler already exists for this identifier!");
  InsertNS->AddPragma(Handler);
}

/// RemovePragmaHandler - Remove the specific pragma handler from the
/// preprocessor. If \arg Namespace is non-null, then it should be the
/// namespace that \arg Handler was added to. It is an error to remove
/// a handler that has not been registered.
void Preprocessor::RemovePragmaHandler(StringRef Namespace,
                                       PragmaHandler *Handler) {
  PragmaNamespace *NS = PragmaHandlers.get();

  // If this is specified to be in a namespace, step down into it.
  if (!Namespace.empty()) {
    PragmaHandler *Existing = PragmaHandlers->FindHandler(Namespace);
    assert(Existing && "Namespace containing handler does not exist!");

    NS = Existing->getIfNamespace();
    assert(NS && "Invalid namespace, registered as a regular pragma handler!");
  }

  NS->RemovePragmaHandler(Handler);

  // If this is a non-default namespace and it is now empty, remove it.
  if (NS != PragmaHandlers.get() && NS->IsEmpty()) {
    PragmaHandlers->RemovePragmaHandler(NS);
    delete NS;
  }
}

bool Preprocessor::LexOnOffSwitch(tok::OnOffSwitch &Result) {
  Token Tok;
  LexUnexpandedToken(Tok);

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::ext_on_off_switch_syntax);
    return true;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();
  if (II->isStr("ON"))
    Result = tok::OOS_ON;
  else if (II->isStr("OFF"))
    Result = tok::OOS_OFF;
  else if (II->isStr("DEFAULT"))
    Result = tok::OOS_DEFAULT;
  else {
    Diag(Tok, diag::ext_on_off_switch_syntax);
    return true;
  }

  // Verify that this is followed by EOD.
  LexUnexpandedToken(Tok);
  if (Tok.isNot(tok::eod))
    Diag(Tok, diag::ext_pragma_syntax_eod);
  return false;
}

namespace {

/// PragmaOnceHandler - "\#pragma once" marks the file as atomically included.
struct PragmaOnceHandler : public PragmaHandler {
  PragmaOnceHandler() : PragmaHandler("once") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &OnceTok) override {
    PP.CheckEndOfDirective("pragma once");
    PP.HandlePragmaOnce(OnceTok);
  }
};

/// PragmaMarkHandler - "\#pragma mark ..." is ignored by the compiler, and the
/// rest of the line is not lexed.
struct PragmaMarkHandler : public PragmaHandler {
  PragmaMarkHandler() : PragmaHandler("mark") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &MarkTok) override {
    PP.HandlePragmaMark(MarkTok);
  }
};

/// PragmaPoisonHandler - "\#pragma poison x" marks x as not usable.
struct PragmaPoisonHandler : public PragmaHandler {
  PragmaPoisonHandler() : PragmaHandler("poison") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &PoisonTok) override {
    PP.HandlePragmaPoison();
  }
};

/// PragmaSystemHeaderHandler - "\#pragma system_header" marks the current file
/// as a system header, which silences warnings in it.
struct PragmaSystemHeaderHandler : public PragmaHandler {
  PragmaSystemHeaderHandler() : PragmaHandler("system_header") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &SHToken) override {
    PP.HandlePragmaSystemHeader(SHToken);
    PP.CheckEndOfDirective("pragma");
  }
};

struct PragmaDependencyHandler : public PragmaHandler {
  PragmaDependencyHandler() : PragmaHandler("dependency") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &DepToken) override {
    PP.HandlePragmaDependency(DepToken);
  }
};

struct PragmaDebugHandler : public PragmaHandler {
  PragmaDebugHandler() : PragmaHandler("__debug") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &DebugToken) override {
    Token Tok;
    PP.LexUnexpandedToken(Tok);
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::warn_pragma_debug_missing_command);
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (II->isStr("assert")) {
      if (!PP.getPreprocessorOpts().DisablePragmaDebugCrash)
        llvm_unreachable("This is an assertion!");
    } else if (II->isStr("crash")) {
      llvm::Timer T("crash", "pragma crash");
      llvm::TimeRegion R(&T);
      if (!PP.getPreprocessorOpts().DisablePragmaDebugCrash)
        LLVM_BUILTIN_TRAP;
    } else if (II->isStr("parser_crash")) {
      if (!PP.getPreprocessorOpts().DisablePragmaDebugCrash) {
        Token Crasher;
        Crasher.startToken();
        Crasher.setKind(tok::annot_pragma_parser_crash);
        Crasher.setAnnotationRange(SourceRange(Tok.getLocation()));
        PP.EnterToken(Crasher, /*IsReinject*/ false);
      }
    } else if (II->isStr("dump")) {
      Token DumpAnnot;
      DumpAnnot.startToken();
      DumpAnnot.setKind(tok::annot_pragma_dump);
      DumpAnnot.setAnnotationRange(SourceRange(Tok.getLocation()));
      PP.EnterToken(DumpAnnot, /*IsReinject*/false);
    } else if (II->isStr("diag_mapping")) {
      Token DiagName;
      PP.LexUnexpandedToken(DiagName);
      if (DiagName.is(tok::eod))
        PP.getDiagnostics().dump();
      else if (DiagName.is(tok::string_literal) && !DiagName.hasUDSuffix()) {
        StringLiteralParser Literal(DiagName, PP,
                                    StringLiteralEvalMethod::Unevaluated);
        if (Literal.hadError)
          return;
        PP.getDiagnostics().dump(Literal.GetString());
      } else {
        PP.Diag(DiagName, diag::warn_pragma_debug_missing_argument)
            << II->getName();
      }
    } else if (II->isStr("llvm_fatal_error")) {
      if (!PP.getPreprocessorOpts().DisablePragmaDebugCrash)
        llvm::report_fatal_error("#pragma clang __debug llvm_fatal_error");
    } else if (II->isStr("llvm_unreachable")) {
      if (!PP.getPreprocessorOpts().DisablePragmaDebugCrash)
        llvm_unreachable("#pragma clang __debug llvm_unreachable");
    } else if (II->isStr("macro")) {
      Token MacroName;
      PP.LexUnexpandedToken(MacroName);
      auto *MacroII = MacroName.getIdentifierInfo();
      if (MacroII)
        PP.dumpMacroInfo(MacroII);
      else
        PP.Diag(MacroName, diag::warn_pragma_debug_missing_argument)
            << II->getName();
    } else if (II->isStr("module_map")) {
      llvm::SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 8>
          ModuleName;
      if (LexModuleName(PP, Tok, ModuleName))
        return;
      ModuleMap &MM = PP.getHeaderSearchInfo().getModuleMap();
      Module *M = nullptr;
      for (auto IIAndLoc : ModuleName) {
        M = MM.lookupModuleQualified(IIAndLoc.first->getName(), M);
        if (!M) {
          PP.Diag(IIAndLoc.second, diag::warn_pragma_debug_unknown_module)
              << IIAndLoc.first;
          return;
        }
      }
      M->dump();
    } else if (II->isStr("overflow_stack")) {
      if (!PP.getPreprocessorOpts().DisablePragmaDebugCrash)
        DebugOverflowStack();
    } else if (II->isStr("captured")) {
      HandleCaptured(PP);
    } else if (II->isStr("modules")) {
      struct ModuleVisitor {
        Preprocessor &PP;
        void visit(Module *M, bool VisibleOnly) {
          SourceLocation ImportLoc = PP.getModuleImportLoc(M);
          if (!VisibleOnly || ImportLoc.isValid()) {
            llvm::errs() << M->getFullModuleName() << " ";
            if (ImportLoc.isValid()) {
              llvm::errs() << M << " visible ";
              ImportLoc.print(llvm::errs(), PP.getSourceManager());
            }
            llvm::errs() << "\n";
          }
          for (Module *Sub : M->submodules()) {
            if (!VisibleOnly || ImportLoc.isInvalid() || Sub->IsExplicit)
              visit(Sub, VisibleOnly);
          }
        }
        void visitAll(bool VisibleOnly) {
          for (auto &NameAndMod :
               PP.getHeaderSearchInfo().getModuleMap().modules())
            visit(NameAndMod.second, VisibleOnly);
        }
      } Visitor{PP};

      Token Kind;
      PP.LexUnexpandedToken(Kind);
      auto *DumpII = Kind.getIdentifierInfo();
      if (!DumpII) {
        PP.Diag(Kind, diag::warn_pragma_debug_missing_argument)
            << II->getName();
      } else if (DumpII->isStr("all")) {
        Visitor.visitAll(false);
      } else if (DumpII->isStr("visible")) {
        Visitor.visitAll(true);
      } else if (DumpII->isStr("building")) {
        for (auto &Building : PP.getBuildingSubmodules()) {
          llvm::errs() << "in " << Building.M->getFullModuleName();
          if (Building.ImportLoc.isValid()) {
            llvm::errs() << " imported ";
            if (Building.IsPragma)
              llvm::errs() << "via pragma ";
            llvm::errs() << "at ";
            Building.ImportLoc.print(llvm::errs(), PP.getSourceManager());
            llvm::errs() << "\n";
          }
        }
      } else {
        PP.Diag(Tok, diag::warn_pragma_debug_unexpected_command)
          << DumpII->getName();
      }
    } else if (II->isStr("sloc_usage")) {
      // An optional integer literal argument specifies the number of files to
      // specifically report information about.
      std::optional<unsigned> MaxNotes;
      Token ArgToken;
      PP.Lex(ArgToken);
      uint64_t Value;
      if (ArgToken.is(tok::numeric_constant) &&
          PP.parseSimpleIntegerLiteral(ArgToken, Value)) {
        MaxNotes = Value;
      } else if (ArgToken.isNot(tok::eod)) {
        PP.Diag(ArgToken, diag::warn_pragma_debug_unexpected_argument);
      }

      PP.Diag(Tok, diag::remark_sloc_usage);
      PP.getSourceManager().noteSLocAddressSpaceUsage(PP.getDiagnostics(),
                                                      MaxNotes);
    } else {
      PP.Diag(Tok, diag::warn_pragma_debug_unexpected_command)
        << II->getName();
    }

    PPCallbacks *Callbacks = PP.getPPCallbacks();
    if (Callbacks)
      Callbacks->PragmaDebug(Tok.getLocation(), II->getName());
  }

  void HandleCaptured(Preprocessor &PP) {
    Token Tok;
    PP.LexUnexpandedToken(Tok);

    if (Tok.isNot(tok::eod)) {
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol)
        << "pragma clang __debug captured";
      return;
    }

    SourceLocation NameLoc = Tok.getLocation();
    MutableArrayRef<Token> Toks(
        PP.getPreprocessorAllocator().Allocate<Token>(1), 1);
    Toks[0].startToken();
    Toks[0].setKind(tok::annot_pragma_captured);
    Toks[0].setLocation(NameLoc);

    PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                        /*IsReinject=*/false);
  }

// Disable MSVC warning about runtime stack overflow.
#ifdef _MSC_VER
    #pragma warning(disable : 4717)
#endif
  static void DebugOverflowStack(void (*P)() = nullptr) {
    void (*volatile Self)(void(*P)()) = DebugOverflowStack;
    Self(reinterpret_cast<void(*)()>(Self));
  }
#ifdef _MSC_VER
    #pragma warning(default : 4717)
#endif
};

struct PragmaUnsafeBufferUsageHandler : public PragmaHandler {
  PragmaUnsafeBufferUsageHandler() : PragmaHandler("unsafe_buffer_usage") {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override {
    Token Tok;

    PP.LexUnexpandedToken(Tok);
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::err_pp_pragma_unsafe_buffer_usage_syntax);
      return;
    }

    IdentifierInfo *II = Tok.getIdentifierInfo();
    SourceLocation Loc = Tok.getLocation();

    if (II->isStr("begin")) {
      if (PP.enterOrExitSafeBufferOptOutRegion(true, Loc))
        PP.Diag(Loc, diag::err_pp_double_begin_pragma_unsafe_buffer_usage);
    } else if (II->isStr("end")) {
      if (PP.enterOrExitSafeBufferOptOutRegion(false, Loc))
        PP.Diag(Loc, diag::err_pp_unmatched_end_begin_pragma_unsafe_buffer_usage);
    } else
      PP.Diag(Tok, diag::err_pp_pragma_unsafe_buffer_usage_syntax);
  }
};

/// PragmaDiagnosticHandler - e.g. '\#pragma GCC diagnostic ignored "-Wformat"'
struct PragmaDiagnosticHandler : public PragmaHandler {
private:
  const char *Namespace;

public:
  explicit PragmaDiagnosticHandler(const char *NS)
      : PragmaHandler("diagnostic"), Namespace(NS) {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &DiagToken) override {
    SourceLocation DiagLoc = DiagToken.getLocation();
    Token Tok;
    PP.LexUnexpandedToken(Tok);
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::warn_pragma_diagnostic_invalid);
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();
    PPCallbacks *Callbacks = PP.getPPCallbacks();

    // Get the next token, which is either an EOD or a string literal. We lex
    // it now so that we can early return if the previous token was push or pop.
    PP.LexUnexpandedToken(Tok);

    if (II->isStr("pop")) {
      if (!PP.getDiagnostics().popMappings(DiagLoc))
        PP.Diag(Tok, diag::warn_pragma_diagnostic_cannot_pop);
      else if (Callbacks)
        Callbacks->PragmaDiagnosticPop(DiagLoc, Namespace);

      if (Tok.isNot(tok::eod))
        PP.Diag(Tok.getLocation(), diag::warn_pragma_diagnostic_invalid_token);
      return;
    } else if (II->isStr("push")) {
      PP.getDiagnostics().pushMappings(DiagLoc);
      if (Callbacks)
        Callbacks->PragmaDiagnosticPush(DiagLoc, Namespace);

      if (Tok.isNot(tok::eod))
        PP.Diag(Tok.getLocation(), diag::warn_pragma_diagnostic_invalid_token);
      return;
    }

    diag::Severity SV = llvm::StringSwitch<diag::Severity>(II->getName())
                            .Case("ignored", diag::Severity::Ignored)
                            .Case("warning", diag::Severity::Warning)
                            .Case("error", diag::Severity::Error)
                            .Case("fatal", diag::Severity::Fatal)
                            .Default(diag::Severity());

    if (SV == diag::Severity()) {
      PP.Diag(Tok, diag::warn_pragma_diagnostic_invalid);
      return;
    }

    // At this point, we expect a string literal.
    SourceLocation StringLoc = Tok.getLocation();
    std::string WarningName;
    if (!PP.FinishLexStringLiteral(Tok, WarningName, "pragma diagnostic",
                                   /*AllowMacroExpansion=*/false))
      return;

    if (Tok.isNot(tok::eod)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_diagnostic_invalid_token);
      return;
    }

    if (WarningName.size() < 3 || WarningName[0] != '-' ||
        (WarningName[1] != 'W' && WarningName[1] != 'R')) {
      PP.Diag(StringLoc, diag::warn_pragma_diagnostic_invalid_option);
      return;
    }

    diag::Flavor Flavor = WarningName[1] == 'W' ? diag::Flavor::WarningOrError
                                                : diag::Flavor::Remark;
    StringRef Group = StringRef(WarningName).substr(2);
    bool unknownDiag = false;
    if (Group == "everything") {
      // Special handling for pragma clang diagnostic ... "-Weverything".
      // There is no formal group named "everything", so there has to be a
      // special case for it.
      PP.getDiagnostics().setSeverityForAll(Flavor, SV, DiagLoc);
    } else
      unknownDiag = PP.getDiagnostics().setSeverityForGroup(Flavor, Group, SV,
                                                            DiagLoc);
    if (unknownDiag)
      PP.Diag(StringLoc, diag::warn_pragma_diagnostic_unknown_warning)
        << WarningName;
    else if (Callbacks)
      Callbacks->PragmaDiagnostic(DiagLoc, Namespace, SV, WarningName);
  }
};

/// "\#pragma hdrstop [<header-name-string>]"
struct PragmaHdrstopHandler : public PragmaHandler {
  PragmaHdrstopHandler() : PragmaHandler("hdrstop") {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &DepToken) override {
    PP.HandlePragmaHdrstop(DepToken);
  }
};

/// "\#pragma warning(...)".  MSVC's diagnostics do not map cleanly to clang's
/// diagnostics, so we don't really implement this pragma.  We parse it and
/// ignore it to avoid -Wunknown-pragma warnings.
struct PragmaWarningHandler : public PragmaHandler {
  PragmaWarningHandler() : PragmaHandler("warning") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    // Parse things like:
    // warning(push, 1)
    // warning(pop)
    // warning(disable : 1 2 3 ; error : 4 5 6 ; suppress : 7 8 9)
    SourceLocation DiagLoc = Tok.getLocation();
    PPCallbacks *Callbacks = PP.getPPCallbacks();

    PP.Lex(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok, diag::warn_pragma_warning_expected) << "(";
      return;
    }

    PP.Lex(Tok);
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (II && II->isStr("push")) {
      // #pragma warning( push[ ,n ] )
      int Level = -1;
      PP.Lex(Tok);
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok);
        uint64_t Value;
        if (Tok.is(tok::numeric_constant) &&
            PP.parseSimpleIntegerLiteral(Tok, Value))
          Level = int(Value);
        if (Level < 0 || Level > 4) {
          PP.Diag(Tok, diag::warn_pragma_warning_push_level);
          return;
        }
      }
      PP.getDiagnostics().pushMappings(DiagLoc);
      if (Callbacks)
        Callbacks->PragmaWarningPush(DiagLoc, Level);
    } else if (II && II->isStr("pop")) {
      // #pragma warning( pop )
      PP.Lex(Tok);
      if (!PP.getDiagnostics().popMappings(DiagLoc))
        PP.Diag(Tok, diag::warn_pragma_diagnostic_cannot_pop);
      else if (Callbacks)
        Callbacks->PragmaWarningPop(DiagLoc);
    } else {
      // #pragma warning( warning-specifier : warning-number-list
      //                  [; warning-specifier : warning-number-list...] )
      while (true) {
        II = Tok.getIdentifierInfo();
        if (!II && !Tok.is(tok::numeric_constant)) {
          PP.Diag(Tok, diag::warn_pragma_warning_spec_invalid);
          return;
        }

        // Figure out which warning specifier this is.
        bool SpecifierValid;
        PPCallbacks::PragmaWarningSpecifier Specifier;
        if (II) {
          int SpecifierInt = llvm::StringSwitch<int>(II->getName())
                                 .Case("default", PPCallbacks::PWS_Default)
                                 .Case("disable", PPCallbacks::PWS_Disable)
                                 .Case("error", PPCallbacks::PWS_Error)
                                 .Case("once", PPCallbacks::PWS_Once)
                                 .Case("suppress", PPCallbacks::PWS_Suppress)
                                 .Default(-1);
          SpecifierValid = SpecifierInt != -1;
          if (SpecifierValid)
            Specifier =
                static_cast<PPCallbacks::PragmaWarningSpecifier>(SpecifierInt);

          // If we read a correct specifier, snatch next token (that should be
          // ":", checked later).
          if (SpecifierValid)
            PP.Lex(Tok);
        } else {
          // Token is a numeric constant. It should be either 1, 2, 3 or 4.
          uint64_t Value;
          if (PP.parseSimpleIntegerLiteral(Tok, Value)) {
            if ((SpecifierValid = (Value >= 1) && (Value <= 4)))
              Specifier = static_cast<PPCallbacks::PragmaWarningSpecifier>(
                  PPCallbacks::PWS_Level1 + Value - 1);
          } else
            SpecifierValid = false;
          // Next token already snatched by parseSimpleIntegerLiteral.
        }

        if (!SpecifierValid) {
          PP.Diag(Tok, diag::warn_pragma_warning_spec_invalid);
          return;
        }
        if (Tok.isNot(tok::colon)) {
          PP.Diag(Tok, diag::warn_pragma_warning_expected) << ":";
          return;
        }

        // Collect the warning ids.
        SmallVector<int, 4> Ids;
        PP.Lex(Tok);
        while (Tok.is(tok::numeric_constant)) {
          uint64_t Value;
          if (!PP.parseSimpleIntegerLiteral(Tok, Value) || Value == 0 ||
              Value > INT_MAX) {
            PP.Diag(Tok, diag::warn_pragma_warning_expected_number);
            return;
          }
          Ids.push_back(int(Value));
        }

        // Only act on disable for now.
        diag::Severity SV = diag::Severity();
        if (Specifier == PPCallbacks::PWS_Disable)
          SV = diag::Severity::Ignored;
        if (SV != diag::Severity())
          for (int Id : Ids) {
            if (auto Group = diagGroupFromCLWarningID(Id)) {
              bool unknownDiag = PP.getDiagnostics().setSeverityForGroup(
                  diag::Flavor::WarningOrError, *Group, SV, DiagLoc);
              assert(!unknownDiag &&
                     "wd table should only contain known diags");
              (void)unknownDiag;
            }
          }

        if (Callbacks)
          Callbacks->PragmaWarning(DiagLoc, Specifier, Ids);

        // Parse the next specifier if there is a semicolon.
        if (Tok.isNot(tok::semi))
          break;
        PP.Lex(Tok);
      }
    }

    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok, diag::warn_pragma_warning_expected) << ")";
      return;
    }

    PP.Lex(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma warning";
  }
};

/// "\#pragma execution_character_set(...)". MSVC supports this pragma only
/// for "UTF-8". We parse it and ignore it if UTF-8 is provided and warn
/// otherwise to avoid -Wunknown-pragma warnings.
struct PragmaExecCharsetHandler : public PragmaHandler {
  PragmaExecCharsetHandler() : PragmaHandler("execution_character_set") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    // Parse things like:
    // execution_character_set(push, "UTF-8")
    // execution_character_set(pop)
    SourceLocation DiagLoc = Tok.getLocation();
    PPCallbacks *Callbacks = PP.getPPCallbacks();

    PP.Lex(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok, diag::warn_pragma_exec_charset_expected) << "(";
      return;
    }

    PP.Lex(Tok);
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (II && II->isStr("push")) {
      // #pragma execution_character_set( push[ , string ] )
      PP.Lex(Tok);
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok);

        std::string ExecCharset;
        if (!PP.FinishLexStringLiteral(Tok, ExecCharset,
                                       "pragma execution_character_set",
                                       /*AllowMacroExpansion=*/false))
          return;

        // MSVC supports either of these, but nothing else.
        if (ExecCharset != "UTF-8" && ExecCharset != "utf-8") {
          PP.Diag(Tok, diag::warn_pragma_exec_charset_push_invalid) << ExecCharset;
          return;
        }
      }
      if (Callbacks)
        Callbacks->PragmaExecCharsetPush(DiagLoc, "UTF-8");
    } else if (II && II->isStr("pop")) {
      // #pragma execution_character_set( pop )
      PP.Lex(Tok);
      if (Callbacks)
        Callbacks->PragmaExecCharsetPop(DiagLoc);
    } else {
      PP.Diag(Tok, diag::warn_pragma_exec_charset_spec_invalid);
      return;
    }

    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok, diag::warn_pragma_exec_charset_expected) << ")";
      return;
    }

    PP.Lex(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma execution_character_set";
  }
};

/// PragmaIncludeAliasHandler - "\#pragma include_alias("...")".
struct PragmaIncludeAliasHandler : public PragmaHandler {
  PragmaIncludeAliasHandler() : PragmaHandler("include_alias") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &IncludeAliasTok) override {
    PP.HandlePragmaIncludeAlias(IncludeAliasTok);
  }
};

/// PragmaMessageHandler - Handle the microsoft and gcc \#pragma message
/// extension.  The syntax is:
/// \code
///   #pragma message(string)
/// \endcode
/// OR, in GCC mode:
/// \code
///   #pragma message string
/// \endcode
/// string is a string, which is fully macro expanded, and permits string
/// concatenation, embedded escape characters, etc... See MSDN for more details.
/// Also handles \#pragma GCC warning and \#pragma GCC error which take the same
/// form as \#pragma message.
struct PragmaMessageHandler : public PragmaHandler {
private:
  const PPCallbacks::PragmaMessageKind Kind;
  const StringRef Namespace;

  static const char* PragmaKind(PPCallbacks::PragmaMessageKind Kind,
                                bool PragmaNameOnly = false) {
    switch (Kind) {
      case PPCallbacks::PMK_Message:
        return PragmaNameOnly ? "message" : "pragma message";
      case PPCallbacks::PMK_Warning:
        return PragmaNameOnly ? "warning" : "pragma warning";
      case PPCallbacks::PMK_Error:
        return PragmaNameOnly ? "error" : "pragma error";
    }
    llvm_unreachable("Unknown PragmaMessageKind!");
  }

public:
  PragmaMessageHandler(PPCallbacks::PragmaMessageKind Kind,
                       StringRef Namespace = StringRef())
      : PragmaHandler(PragmaKind(Kind, true)), Kind(Kind),
        Namespace(Namespace) {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    SourceLocation MessageLoc = Tok.getLocation();
    PP.Lex(Tok);
    bool ExpectClosingParen = false;
    switch (Tok.getKind()) {
    case tok::l_paren:
      // We have a MSVC style pragma message.
      ExpectClosingParen = true;
      // Read the string.
      PP.Lex(Tok);
      break;
    case tok::string_literal:
      // We have a GCC style pragma message, and we just read the string.
      break;
    default:
      PP.Diag(MessageLoc, diag::err_pragma_message_malformed) << Kind;
      return;
    }

    std::string MessageString;
    if (!PP.FinishLexStringLiteral(Tok, MessageString, PragmaKind(Kind),
                                   /*AllowMacroExpansion=*/true))
      return;

    if (ExpectClosingParen) {
      if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_message_malformed) << Kind;
        return;
      }
      PP.Lex(Tok);  // eat the r_paren.
    }

    if (Tok.isNot(tok::eod)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_message_malformed) << Kind;
      return;
    }

    // Output the message.
    PP.Diag(MessageLoc, (Kind == PPCallbacks::PMK_Error)
                          ? diag::err_pragma_message
                          : diag::warn_pragma_message) << MessageString;

    // If the pragma is lexically sound, notify any interested PPCallbacks.
    if (PPCallbacks *Callbacks = PP.getPPCallbacks())
      Callbacks->PragmaMessage(MessageLoc, Namespace, Kind, MessageString);
  }
};

/// Handle the clang \#pragma module import extension. The syntax is:
/// \code
///   #pragma clang module import some.module.name
/// \endcode
struct PragmaModuleImportHandler : public PragmaHandler {
  PragmaModuleImportHandler() : PragmaHandler("import") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    SourceLocation ImportLoc = Tok.getLocation();

    // Read the module name.
    llvm::SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 8>
        ModuleName;
    if (LexModuleName(PP, Tok, ModuleName))
      return;

    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma";

    // If we have a non-empty module path, load the named module.
    Module *Imported =
        PP.getModuleLoader().loadModule(ImportLoc, ModuleName, Module::Hidden,
                                      /*IsInclusionDirective=*/false);
    if (!Imported)
      return;

    PP.makeModuleVisible(Imported, ImportLoc);
    PP.EnterAnnotationToken(SourceRange(ImportLoc, ModuleName.back().second),
                            tok::annot_module_include, Imported);
    if (auto *CB = PP.getPPCallbacks())
      CB->moduleImport(ImportLoc, ModuleName, Imported);
  }
};

/// Handle the clang \#pragma module begin extension. The syntax is:
/// \code
///   #pragma clang module begin some.module.name
///   ...
///   #pragma clang module end
/// \endcode
struct PragmaModuleBeginHandler : public PragmaHandler {
  PragmaModuleBeginHandler() : PragmaHandler("begin") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    SourceLocation BeginLoc = Tok.getLocation();

    // Read the module name.
    llvm::SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 8>
        ModuleName;
    if (LexModuleName(PP, Tok, ModuleName))
      return;

    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma";

    // We can only enter submodules of the current module.
    StringRef Current = PP.getLangOpts().CurrentModule;
    if (ModuleName.front().first->getName() != Current) {
      PP.Diag(ModuleName.front().second, diag::err_pp_module_begin_wrong_module)
        << ModuleName.front().first << (ModuleName.size() > 1)
        << Current.empty() << Current;
      return;
    }

    // Find the module we're entering. We require that a module map for it
    // be loaded or implicitly loadable.
    auto &HSI = PP.getHeaderSearchInfo();
    Module *M = HSI.lookupModule(Current, ModuleName.front().second);
    if (!M) {
      PP.Diag(ModuleName.front().second,
              diag::err_pp_module_begin_no_module_map) << Current;
      return;
    }
    for (unsigned I = 1; I != ModuleName.size(); ++I) {
      auto *NewM = M->findOrInferSubmodule(ModuleName[I].first->getName());
      if (!NewM) {
        PP.Diag(ModuleName[I].second, diag::err_pp_module_begin_no_submodule)
          << M->getFullModuleName() << ModuleName[I].first;
        return;
      }
      M = NewM;
    }

    // If the module isn't available, it doesn't make sense to enter it.
    if (Preprocessor::checkModuleIsAvailable(
            PP.getLangOpts(), PP.getTargetInfo(), *M, PP.getDiagnostics())) {
      PP.Diag(BeginLoc, diag::note_pp_module_begin_here)
        << M->getTopLevelModuleName();
      return;
    }

    // Enter the scope of the submodule.
    PP.EnterSubmodule(M, BeginLoc, /*ForPragma*/true);
    PP.EnterAnnotationToken(SourceRange(BeginLoc, ModuleName.back().second),
                            tok::annot_module_begin, M);
  }
};

/// Handle the clang \#pragma module end extension.
struct PragmaModuleEndHandler : public PragmaHandler {
  PragmaModuleEndHandler() : PragmaHandler("end") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    SourceLocation Loc = Tok.getLocation();

    PP.LexUnexpandedToken(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma";

    Module *M = PP.LeaveSubmodule(/*ForPragma*/true);
    if (M)
      PP.EnterAnnotationToken(SourceRange(Loc), tok::annot_module_end, M);
    else
      PP.Diag(Loc, diag::err_pp_module_end_without_module_begin);
  }
};

/// Handle the clang \#pragma module build extension.
struct PragmaModuleBuildHandler : public PragmaHandler {
  PragmaModuleBuildHandler() : PragmaHandler("build") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    PP.HandlePragmaModuleBuild(Tok);
  }
};

/// Handle the clang \#pragma module load extension.
struct PragmaModuleLoadHandler : public PragmaHandler {
  PragmaModuleLoadHandler() : PragmaHandler("load") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    SourceLocation Loc = Tok.getLocation();

    // Read the module name.
    llvm::SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 8>
        ModuleName;
    if (LexModuleName(PP, Tok, ModuleName))
      return;

    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma";

    // Load the module, don't make it visible.
    PP.getModuleLoader().loadModule(Loc, ModuleName, Module::Hidden,
                                    /*IsInclusionDirective=*/false);
  }
};

/// PragmaPushMacroHandler - "\#pragma push_macro" saves the value of the
/// macro on the top of the stack.
struct PragmaPushMacroHandler : public PragmaHandler {
  PragmaPushMacroHandler() : PragmaHandler("push_macro") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &PushMacroTok) override {
    PP.HandlePragmaPushMacro(PushMacroTok);
  }
};

/// PragmaPopMacroHandler - "\#pragma pop_macro" sets the value of the
/// macro to the value on the top of the stack.
struct PragmaPopMacroHandler : public PragmaHandler {
  PragmaPopMacroHandler() : PragmaHandler("pop_macro") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &PopMacroTok) override {
    PP.HandlePragmaPopMacro(PopMacroTok);
  }
};

/// PragmaARCCFCodeAuditedHandler -
///   \#pragma clang arc_cf_code_audited begin/end
struct PragmaARCCFCodeAuditedHandler : public PragmaHandler {
  PragmaARCCFCodeAuditedHandler() : PragmaHandler("arc_cf_code_audited") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &NameTok) override {
    SourceLocation Loc = NameTok.getLocation();
    bool IsBegin;

    Token Tok;

    // Lex the 'begin' or 'end'.
    PP.LexUnexpandedToken(Tok);
    const IdentifierInfo *BeginEnd = Tok.getIdentifierInfo();
    if (BeginEnd && BeginEnd->isStr("begin")) {
      IsBegin = true;
    } else if (BeginEnd && BeginEnd->isStr("end")) {
      IsBegin = false;
    } else {
      PP.Diag(Tok.getLocation(), diag::err_pp_arc_cf_code_audited_syntax);
      return;
    }

    // Verify that this is followed by EOD.
    PP.LexUnexpandedToken(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma";

    // The start location of the active audit.
    SourceLocation BeginLoc = PP.getPragmaARCCFCodeAuditedInfo().second;

    // The start location we want after processing this.
    SourceLocation NewLoc;

    if (IsBegin) {
      // Complain about attempts to re-enter an audit.
      if (BeginLoc.isValid()) {
        PP.Diag(Loc, diag::err_pp_double_begin_of_arc_cf_code_audited);
        PP.Diag(BeginLoc, diag::note_pragma_entered_here);
      }
      NewLoc = Loc;
    } else {
      // Complain about attempts to leave an audit that doesn't exist.
      if (!BeginLoc.isValid()) {
        PP.Diag(Loc, diag::err_pp_unmatched_end_of_arc_cf_code_audited);
        return;
      }
      NewLoc = SourceLocation();
    }

    PP.setPragmaARCCFCodeAuditedInfo(NameTok.getIdentifierInfo(), NewLoc);
  }
};

/// PragmaAssumeNonNullHandler -
///   \#pragma clang assume_nonnull begin/end
struct PragmaAssumeNonNullHandler : public PragmaHandler {
  PragmaAssumeNonNullHandler() : PragmaHandler("assume_nonnull") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &NameTok) override {
    SourceLocation Loc = NameTok.getLocation();
    bool IsBegin;

    Token Tok;

    // Lex the 'begin' or 'end'.
    PP.LexUnexpandedToken(Tok);
    const IdentifierInfo *BeginEnd = Tok.getIdentifierInfo();
    if (BeginEnd && BeginEnd->isStr("begin")) {
      IsBegin = true;
    } else if (BeginEnd && BeginEnd->isStr("end")) {
      IsBegin = false;
    } else {
      PP.Diag(Tok.getLocation(), diag::err_pp_assume_nonnull_syntax);
      return;
    }

    // Verify that this is followed by EOD.
    PP.LexUnexpandedToken(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma";

    // The start location of the active audit.
    SourceLocation BeginLoc = PP.getPragmaAssumeNonNullLoc();

    // The start location we want after processing this.
    SourceLocation NewLoc;
    PPCallbacks *Callbacks = PP.getPPCallbacks();

    if (IsBegin) {
      // Complain about attempts to re-enter an audit.
      if (BeginLoc.isValid()) {
        PP.Diag(Loc, diag::err_pp_double_begin_of_assume_nonnull);
        PP.Diag(BeginLoc, diag::note_pragma_entered_here);
      }
      NewLoc = Loc;
      if (Callbacks)
        Callbacks->PragmaAssumeNonNullBegin(NewLoc);
    } else {
      // Complain about attempts to leave an audit that doesn't exist.
      if (!BeginLoc.isValid()) {
        PP.Diag(Loc, diag::err_pp_unmatched_end_of_assume_nonnull);
        return;
      }
      NewLoc = SourceLocation();
      if (Callbacks)
        Callbacks->PragmaAssumeNonNullEnd(NewLoc);
    }

    PP.setPragmaAssumeNonNullLoc(NewLoc);
  }
};

/// Handle "\#pragma region [...]"
///
/// The syntax is
/// \code
///   #pragma region [optional name]
///   #pragma endregion [optional comment]
/// \endcode
///
/// \note This is
/// <a href="http://msdn.microsoft.com/en-us/library/b6xkz944(v=vs.80).aspx">editor-only</a>
/// pragma, just skipped by compiler.
struct PragmaRegionHandler : public PragmaHandler {
  PragmaRegionHandler(const char *pragma) : PragmaHandler(pragma) {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &NameTok) override {
    // #pragma region: endregion matches can be verified
    // __pragma(region): no sense, but ignored by msvc
    // _Pragma is not valid for MSVC, but there isn't any point
    // to handle a _Pragma differently.
  }
};

/// "\#pragma managed"
/// "\#pragma managed(...)"
/// "\#pragma unmanaged"
/// MSVC ignores this pragma when not compiling using /clr, which clang doesn't
/// support. We parse it and ignore it to avoid -Wunknown-pragma warnings.
struct PragmaManagedHandler : public EmptyPragmaHandler {
  PragmaManagedHandler(const char *pragma) : EmptyPragmaHandler(pragma) {}
};

/// This handles parsing pragmas that take a macro name and optional message
static IdentifierInfo *HandleMacroAnnotationPragma(Preprocessor &PP, Token &Tok,
                                                   const char *Pragma,
                                                   std::string &MessageString) {
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok, diag::err_expected) << "(";
    return nullptr;
  }

  PP.LexUnexpandedToken(Tok);
  if (!Tok.is(tok::identifier)) {
    PP.Diag(Tok, diag::err_expected) << tok::identifier;
    return nullptr;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();

  if (!II->hasMacroDefinition()) {
    PP.Diag(Tok, diag::err_pp_visibility_non_macro) << II;
    return nullptr;
  }

  PP.Lex(Tok);
  if (Tok.is(tok::comma)) {
    PP.Lex(Tok);
    if (!PP.FinishLexStringLiteral(Tok, MessageString, Pragma,
                                   /*AllowMacroExpansion=*/true))
      return nullptr;
  }

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok, diag::err_expected) << ")";
    return nullptr;
  }
  return II;
}

/// "\#pragma clang deprecated(...)"
///
/// The syntax is
/// \code
///   #pragma clang deprecate(MACRO_NAME [, Message])
/// \endcode
struct PragmaDeprecatedHandler : public PragmaHandler {
  PragmaDeprecatedHandler() : PragmaHandler("deprecated") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    std::string MessageString;

    if (IdentifierInfo *II = HandleMacroAnnotationPragma(
            PP, Tok, "#pragma clang deprecated", MessageString)) {
      II->setIsDeprecatedMacro(true);
      PP.addMacroDeprecationMsg(II, std::move(MessageString),
                                Tok.getLocation());
    }
  }
};

/// "\#pragma clang restrict_expansion(...)"
///
/// The syntax is
/// \code
///   #pragma clang restrict_expansion(MACRO_NAME [, Message])
/// \endcode
struct PragmaRestrictExpansionHandler : public PragmaHandler {
  PragmaRestrictExpansionHandler() : PragmaHandler("restrict_expansion") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    std::string MessageString;

    if (IdentifierInfo *II = HandleMacroAnnotationPragma(
            PP, Tok, "#pragma clang restrict_expansion", MessageString)) {
      II->setIsRestrictExpansion(true);
      PP.addRestrictExpansionMsg(II, std::move(MessageString),
                                 Tok.getLocation());
    }
  }
};

/// "\#pragma clang final(...)"
///
/// The syntax is
/// \code
///   #pragma clang final(MACRO_NAME)
/// \endcode
struct PragmaFinalHandler : public PragmaHandler {
  PragmaFinalHandler() : PragmaHandler("final") {}

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override {
    PP.Lex(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok, diag::err_expected) << "(";
      return;
    }

    PP.LexUnexpandedToken(Tok);
    if (!Tok.is(tok::identifier)) {
      PP.Diag(Tok, diag::err_expected) << tok::identifier;
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (!II->hasMacroDefinition()) {
      PP.Diag(Tok, diag::err_pp_visibility_non_macro) << II;
      return;
    }

    PP.Lex(Tok);
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok, diag::err_expected) << ")";
      return;
    }
    II->setIsFinal(true);
    PP.addFinalLoc(II, Tok.getLocation());
  }
};

} // namespace

/// RegisterBuiltinPragmas - Install the standard preprocessor pragmas:
/// \#pragma GCC poison/system_header/dependency and \#pragma once.
void Preprocessor::RegisterBuiltinPragmas() {
  AddPragmaHandler(new PragmaOnceHandler());
  AddPragmaHandler(new PragmaMarkHandler());
  AddPragmaHandler(new PragmaPushMacroHandler());
  AddPragmaHandler(new PragmaPopMacroHandler());
  AddPragmaHandler(new PragmaMessageHandler(PPCallbacks::PMK_Message));

  // #pragma GCC ...
  AddPragmaHandler("GCC", new PragmaPoisonHandler());
  AddPragmaHandler("GCC", new PragmaSystemHeaderHandler());
  AddPragmaHandler("GCC", new PragmaDependencyHandler());
  AddPragmaHandler("GCC", new PragmaDiagnosticHandler("GCC"));
  AddPragmaHandler("GCC", new PragmaMessageHandler(PPCallbacks::PMK_Warning,
                                                   "GCC"));
  AddPragmaHandler("GCC", new PragmaMessageHandler(PPCallbacks::PMK_Error,
                                                   "GCC"));
  // #pragma clang ...
  AddPragmaHandler("clang", new PragmaPoisonHandler());
  AddPragmaHandler("clang", new PragmaSystemHeaderHandler());
  AddPragmaHandler("clang", new PragmaDebugHandler());
  AddPragmaHandler("clang", new PragmaDependencyHandler());
  AddPragmaHandler("clang", new PragmaDiagnosticHandler("clang"));
  AddPragmaHandler("clang", new PragmaARCCFCodeAuditedHandler());
  AddPragmaHandler("clang", new PragmaAssumeNonNullHandler());
  AddPragmaHandler("clang", new PragmaDeprecatedHandler());
  AddPragmaHandler("clang", new PragmaRestrictExpansionHandler());
  AddPragmaHandler("clang", new PragmaFinalHandler());

  // #pragma clang module ...
  auto *ModuleHandler = new PragmaNamespace("module");
  AddPragmaHandler("clang", ModuleHandler);
  ModuleHandler->AddPragma(new PragmaModuleImportHandler());
  ModuleHandler->AddPragma(new PragmaModuleBeginHandler());
  ModuleHandler->AddPragma(new PragmaModuleEndHandler());
  ModuleHandler->AddPragma(new PragmaModuleBuildHandler());
  ModuleHandler->AddPragma(new PragmaModuleLoadHandler());

  // Safe Buffers pragmas
  AddPragmaHandler("clang", new PragmaUnsafeBufferUsageHandler);

  // Add region pragmas.
  AddPragmaHandler(new PragmaRegionHandler("region"));
  AddPragmaHandler(new PragmaRegionHandler("endregion"));

  // MS extensions.
  if (LangOpts.MicrosoftExt) {
    AddPragmaHandler(new PragmaWarningHandler());
    AddPragmaHandler(new PragmaExecCharsetHandler());
    AddPragmaHandler(new PragmaIncludeAliasHandler());
    AddPragmaHandler(new PragmaHdrstopHandler());
    AddPragmaHandler(new PragmaSystemHeaderHandler());
    AddPragmaHandler(new PragmaManagedHandler("managed"));
    AddPragmaHandler(new PragmaManagedHandler("unmanaged"));
  }

  // Pragmas added by plugins
  for (const PragmaHandlerRegistry::entry &handler :
       PragmaHandlerRegistry::entries()) {
    AddPragmaHandler(handler.instantiate().release());
  }
}

/// Ignore all pragmas, useful for modes such as -Eonly which would otherwise
/// warn about those pragmas being unknown.
void Preprocessor::IgnorePragmas() {
  AddPragmaHandler(new EmptyPragmaHandler());
  // Also ignore all pragmas in all namespaces created
  // in Preprocessor::RegisterBuiltinPragmas().
  AddPragmaHandler("GCC", new EmptyPragmaHandler());
  AddPragmaHandler("clang", new EmptyPragmaHandler());
}
