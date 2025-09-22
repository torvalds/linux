//===--- PPDirectives.cpp - Directive Handling for Preprocessor -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implements # directive processing for the Preprocessor.
///
//===----------------------------------------------------------------------===//

#include "clang/Basic/CharInfo.h"
#include "clang/Basic/DirectoryEntry.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/CodeCompletionHandler.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Token.h"
#include "clang/Lex/VariadicMacroSupport.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SaveAndRestore.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <utility>

using namespace clang;

//===----------------------------------------------------------------------===//
// Utility Methods for Preprocessor Directive Handling.
//===----------------------------------------------------------------------===//

MacroInfo *Preprocessor::AllocateMacroInfo(SourceLocation L) {
  static_assert(std::is_trivially_destructible_v<MacroInfo>, "");
  return new (BP) MacroInfo(L);
}

DefMacroDirective *Preprocessor::AllocateDefMacroDirective(MacroInfo *MI,
                                                           SourceLocation Loc) {
  return new (BP) DefMacroDirective(MI, Loc);
}

UndefMacroDirective *
Preprocessor::AllocateUndefMacroDirective(SourceLocation UndefLoc) {
  return new (BP) UndefMacroDirective(UndefLoc);
}

VisibilityMacroDirective *
Preprocessor::AllocateVisibilityMacroDirective(SourceLocation Loc,
                                               bool isPublic) {
  return new (BP) VisibilityMacroDirective(Loc, isPublic);
}

/// Read and discard all tokens remaining on the current line until
/// the tok::eod token is found.
SourceRange Preprocessor::DiscardUntilEndOfDirective(Token &Tmp) {
  SourceRange Res;

  LexUnexpandedToken(Tmp);
  Res.setBegin(Tmp.getLocation());
  while (Tmp.isNot(tok::eod)) {
    assert(Tmp.isNot(tok::eof) && "EOF seen while discarding directive tokens");
    LexUnexpandedToken(Tmp);
  }
  Res.setEnd(Tmp.getLocation());
  return Res;
}

/// Enumerates possible cases of #define/#undef a reserved identifier.
enum MacroDiag {
  MD_NoWarn,        //> Not a reserved identifier
  MD_KeywordDef,    //> Macro hides keyword, enabled by default
  MD_ReservedMacro  //> #define of #undef reserved id, disabled by default
};

/// Enumerates possible %select values for the pp_err_elif_after_else and
/// pp_err_elif_without_if diagnostics.
enum PPElifDiag {
  PED_Elif,
  PED_Elifdef,
  PED_Elifndef
};

static bool isFeatureTestMacro(StringRef MacroName) {
  // list from:
  // * https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_macros.html
  // * https://docs.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt?view=msvc-160
  // * man 7 feature_test_macros
  // The list must be sorted for correct binary search.
  static constexpr StringRef ReservedMacro[] = {
      "_ATFILE_SOURCE",
      "_BSD_SOURCE",
      "_CRT_NONSTDC_NO_WARNINGS",
      "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES",
      "_CRT_SECURE_NO_WARNINGS",
      "_FILE_OFFSET_BITS",
      "_FORTIFY_SOURCE",
      "_GLIBCXX_ASSERTIONS",
      "_GLIBCXX_CONCEPT_CHECKS",
      "_GLIBCXX_DEBUG",
      "_GLIBCXX_DEBUG_PEDANTIC",
      "_GLIBCXX_PARALLEL",
      "_GLIBCXX_PARALLEL_ASSERTIONS",
      "_GLIBCXX_SANITIZE_VECTOR",
      "_GLIBCXX_USE_CXX11_ABI",
      "_GLIBCXX_USE_DEPRECATED",
      "_GNU_SOURCE",
      "_ISOC11_SOURCE",
      "_ISOC95_SOURCE",
      "_ISOC99_SOURCE",
      "_LARGEFILE64_SOURCE",
      "_POSIX_C_SOURCE",
      "_REENTRANT",
      "_SVID_SOURCE",
      "_THREAD_SAFE",
      "_XOPEN_SOURCE",
      "_XOPEN_SOURCE_EXTENDED",
      "__STDCPP_WANT_MATH_SPEC_FUNCS__",
      "__STDC_FORMAT_MACROS",
  };
  return std::binary_search(std::begin(ReservedMacro), std::end(ReservedMacro),
                            MacroName);
}

static bool isLanguageDefinedBuiltin(const SourceManager &SourceMgr,
                                     const MacroInfo *MI,
                                     const StringRef MacroName) {
  // If this is a macro with special handling (like __LINE__) then it's language
  // defined.
  if (MI->isBuiltinMacro())
    return true;
  // Builtin macros are defined in the builtin file
  if (!SourceMgr.isWrittenInBuiltinFile(MI->getDefinitionLoc()))
    return false;
  // C defines macros starting with __STDC, and C++ defines macros starting with
  // __STDCPP
  if (MacroName.starts_with("__STDC"))
    return true;
  // C++ defines the __cplusplus macro
  if (MacroName == "__cplusplus")
    return true;
  // C++ defines various feature-test macros starting with __cpp
  if (MacroName.starts_with("__cpp"))
    return true;
  // Anything else isn't language-defined
  return false;
}

static MacroDiag shouldWarnOnMacroDef(Preprocessor &PP, IdentifierInfo *II) {
  const LangOptions &Lang = PP.getLangOpts();
  StringRef Text = II->getName();
  if (isReservedInAllContexts(II->isReserved(Lang)))
    return isFeatureTestMacro(Text) ? MD_NoWarn : MD_ReservedMacro;
  if (II->isKeyword(Lang))
    return MD_KeywordDef;
  if (Lang.CPlusPlus11 && (Text == "override" || Text == "final"))
    return MD_KeywordDef;
  return MD_NoWarn;
}

static MacroDiag shouldWarnOnMacroUndef(Preprocessor &PP, IdentifierInfo *II) {
  const LangOptions &Lang = PP.getLangOpts();
  // Do not warn on keyword undef.  It is generally harmless and widely used.
  if (isReservedInAllContexts(II->isReserved(Lang)))
    return MD_ReservedMacro;
  return MD_NoWarn;
}

// Return true if we want to issue a diagnostic by default if we
// encounter this name in a #include with the wrong case. For now,
// this includes the standard C and C++ headers, Posix headers,
// and Boost headers. Improper case for these #includes is a
// potential portability issue.
static bool warnByDefaultOnWrongCase(StringRef Include) {
  // If the first component of the path is "boost", treat this like a standard header
  // for the purposes of diagnostics.
  if (::llvm::sys::path::begin(Include)->equals_insensitive("boost"))
    return true;

  // "condition_variable" is the longest standard header name at 18 characters.
  // If the include file name is longer than that, it can't be a standard header.
  static const size_t MaxStdHeaderNameLen = 18u;
  if (Include.size() > MaxStdHeaderNameLen)
    return false;

  // Lowercase and normalize the search string.
  SmallString<32> LowerInclude{Include};
  for (char &Ch : LowerInclude) {
    // In the ASCII range?
    if (static_cast<unsigned char>(Ch) > 0x7f)
      return false; // Can't be a standard header
    // ASCII lowercase:
    if (Ch >= 'A' && Ch <= 'Z')
      Ch += 'a' - 'A';
    // Normalize path separators for comparison purposes.
    else if (::llvm::sys::path::is_separator(Ch))
      Ch = '/';
  }

  // The standard C/C++ and Posix headers
  return llvm::StringSwitch<bool>(LowerInclude)
    // C library headers
    .Cases("assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h", true)
    .Cases("float.h", "inttypes.h", "iso646.h", "limits.h", "locale.h", true)
    .Cases("math.h", "setjmp.h", "signal.h", "stdalign.h", "stdarg.h", true)
    .Cases("stdatomic.h", "stdbool.h", "stdckdint.h", "stddef.h", true)
    .Cases("stdint.h", "stdio.h", "stdlib.h", "stdnoreturn.h", true)
    .Cases("string.h", "tgmath.h", "threads.h", "time.h", "uchar.h", true)
    .Cases("wchar.h", "wctype.h", true)

    // C++ headers for C library facilities
    .Cases("cassert", "ccomplex", "cctype", "cerrno", "cfenv", true)
    .Cases("cfloat", "cinttypes", "ciso646", "climits", "clocale", true)
    .Cases("cmath", "csetjmp", "csignal", "cstdalign", "cstdarg", true)
    .Cases("cstdbool", "cstddef", "cstdint", "cstdio", "cstdlib", true)
    .Cases("cstring", "ctgmath", "ctime", "cuchar", "cwchar", true)
    .Case("cwctype", true)

    // C++ library headers
    .Cases("algorithm", "fstream", "list", "regex", "thread", true)
    .Cases("array", "functional", "locale", "scoped_allocator", "tuple", true)
    .Cases("atomic", "future", "map", "set", "type_traits", true)
    .Cases("bitset", "initializer_list", "memory", "shared_mutex", "typeindex", true)
    .Cases("chrono", "iomanip", "mutex", "sstream", "typeinfo", true)
    .Cases("codecvt", "ios", "new", "stack", "unordered_map", true)
    .Cases("complex", "iosfwd", "numeric", "stdexcept", "unordered_set", true)
    .Cases("condition_variable", "iostream", "ostream", "streambuf", "utility", true)
    .Cases("deque", "istream", "queue", "string", "valarray", true)
    .Cases("exception", "iterator", "random", "strstream", "vector", true)
    .Cases("forward_list", "limits", "ratio", "system_error", true)

    // POSIX headers (which aren't also C headers)
    .Cases("aio.h", "arpa/inet.h", "cpio.h", "dirent.h", "dlfcn.h", true)
    .Cases("fcntl.h", "fmtmsg.h", "fnmatch.h", "ftw.h", "glob.h", true)
    .Cases("grp.h", "iconv.h", "langinfo.h", "libgen.h", "monetary.h", true)
    .Cases("mqueue.h", "ndbm.h", "net/if.h", "netdb.h", "netinet/in.h", true)
    .Cases("netinet/tcp.h", "nl_types.h", "poll.h", "pthread.h", "pwd.h", true)
    .Cases("regex.h", "sched.h", "search.h", "semaphore.h", "spawn.h", true)
    .Cases("strings.h", "stropts.h", "sys/ipc.h", "sys/mman.h", "sys/msg.h", true)
    .Cases("sys/resource.h", "sys/select.h",  "sys/sem.h", "sys/shm.h", "sys/socket.h", true)
    .Cases("sys/stat.h", "sys/statvfs.h", "sys/time.h", "sys/times.h", "sys/types.h", true)
    .Cases("sys/uio.h", "sys/un.h", "sys/utsname.h", "sys/wait.h", "syslog.h", true)
    .Cases("tar.h", "termios.h", "trace.h", "ulimit.h", true)
    .Cases("unistd.h", "utime.h", "utmpx.h", "wordexp.h", true)
    .Default(false);
}

/// Find a similar string in `Candidates`.
///
/// \param LHS a string for a similar string in `Candidates`
///
/// \param Candidates the candidates to find a similar string.
///
/// \returns a similar string if exists. If no similar string exists,
/// returns std::nullopt.
static std::optional<StringRef>
findSimilarStr(StringRef LHS, const std::vector<StringRef> &Candidates) {
  // We need to check if `Candidates` has the exact case-insensitive string
  // because the Levenshtein distance match does not care about it.
  for (StringRef C : Candidates) {
    if (LHS.equals_insensitive(C)) {
      return C;
    }
  }

  // Keep going with the Levenshtein distance match.
  // If the LHS size is less than 3, use the LHS size minus 1 and if not,
  // use the LHS size divided by 3.
  size_t Length = LHS.size();
  size_t MaxDist = Length < 3 ? Length - 1 : Length / 3;

  std::optional<std::pair<StringRef, size_t>> SimilarStr;
  for (StringRef C : Candidates) {
    size_t CurDist = LHS.edit_distance(C, true);
    if (CurDist <= MaxDist) {
      if (!SimilarStr) {
        // The first similar string found.
        SimilarStr = {C, CurDist};
      } else if (CurDist < SimilarStr->second) {
        // More similar string found.
        SimilarStr = {C, CurDist};
      }
    }
  }

  if (SimilarStr) {
    return SimilarStr->first;
  } else {
    return std::nullopt;
  }
}

bool Preprocessor::CheckMacroName(Token &MacroNameTok, MacroUse isDefineUndef,
                                  bool *ShadowFlag) {
  // Missing macro name?
  if (MacroNameTok.is(tok::eod))
    return Diag(MacroNameTok, diag::err_pp_missing_macro_name);

  IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
  if (!II)
    return Diag(MacroNameTok, diag::err_pp_macro_not_identifier);

  if (II->isCPlusPlusOperatorKeyword()) {
    // C++ 2.5p2: Alternative tokens behave the same as its primary token
    // except for their spellings.
    Diag(MacroNameTok, getLangOpts().MicrosoftExt
                           ? diag::ext_pp_operator_used_as_macro_name
                           : diag::err_pp_operator_used_as_macro_name)
        << II << MacroNameTok.getKind();
    // Allow #defining |and| and friends for Microsoft compatibility or
    // recovery when legacy C headers are included in C++.
  }

  if ((isDefineUndef != MU_Other) && II->getPPKeywordID() == tok::pp_defined) {
    // Error if defining "defined": C99 6.10.8/4, C++ [cpp.predefined]p4.
    return Diag(MacroNameTok, diag::err_defined_macro_name);
  }

  // If defining/undefining reserved identifier or a keyword, we need to issue
  // a warning.
  SourceLocation MacroNameLoc = MacroNameTok.getLocation();
  if (ShadowFlag)
    *ShadowFlag = false;
  if (!SourceMgr.isInSystemHeader(MacroNameLoc) &&
      (SourceMgr.getBufferName(MacroNameLoc) != "<built-in>")) {
    MacroDiag D = MD_NoWarn;
    if (isDefineUndef == MU_Define) {
      D = shouldWarnOnMacroDef(*this, II);
    }
    else if (isDefineUndef == MU_Undef)
      D = shouldWarnOnMacroUndef(*this, II);
    if (D == MD_KeywordDef) {
      // We do not want to warn on some patterns widely used in configuration
      // scripts.  This requires analyzing next tokens, so do not issue warnings
      // now, only inform caller.
      if (ShadowFlag)
        *ShadowFlag = true;
    }
    if (D == MD_ReservedMacro)
      Diag(MacroNameTok, diag::warn_pp_macro_is_reserved_id);
  }

  // Okay, we got a good identifier.
  return false;
}

/// Lex and validate a macro name, which occurs after a
/// \#define or \#undef.
///
/// This sets the token kind to eod and discards the rest of the macro line if
/// the macro name is invalid.
///
/// \param MacroNameTok Token that is expected to be a macro name.
/// \param isDefineUndef Context in which macro is used.
/// \param ShadowFlag Points to a flag that is set if macro shadows a keyword.
void Preprocessor::ReadMacroName(Token &MacroNameTok, MacroUse isDefineUndef,
                                 bool *ShadowFlag) {
  // Read the token, don't allow macro expansion on it.
  LexUnexpandedToken(MacroNameTok);

  if (MacroNameTok.is(tok::code_completion)) {
    if (CodeComplete)
      CodeComplete->CodeCompleteMacroName(isDefineUndef == MU_Define);
    setCodeCompletionReached();
    LexUnexpandedToken(MacroNameTok);
  }

  if (!CheckMacroName(MacroNameTok, isDefineUndef, ShadowFlag))
    return;

  // Invalid macro name, read and discard the rest of the line and set the
  // token kind to tok::eod if necessary.
  if (MacroNameTok.isNot(tok::eod)) {
    MacroNameTok.setKind(tok::eod);
    DiscardUntilEndOfDirective();
  }
}

/// Ensure that the next token is a tok::eod token.
///
/// If not, emit a diagnostic and consume up until the eod.  If EnableMacros is
/// true, then we consider macros that expand to zero tokens as being ok.
///
/// Returns the location of the end of the directive.
SourceLocation Preprocessor::CheckEndOfDirective(const char *DirType,
                                                 bool EnableMacros) {
  Token Tmp;
  // Lex unexpanded tokens for most directives: macros might expand to zero
  // tokens, causing us to miss diagnosing invalid lines.  Some directives (like
  // #line) allow empty macros.
  if (EnableMacros)
    Lex(Tmp);
  else
    LexUnexpandedToken(Tmp);

  // There should be no tokens after the directive, but we allow them as an
  // extension.
  while (Tmp.is(tok::comment))  // Skip comments in -C mode.
    LexUnexpandedToken(Tmp);

  if (Tmp.is(tok::eod))
    return Tmp.getLocation();

  // Add a fixit in GNU/C99/C++ mode.  Don't offer a fixit for strict-C89,
  // or if this is a macro-style preprocessing directive, because it is more
  // trouble than it is worth to insert /**/ and check that there is no /**/
  // in the range also.
  FixItHint Hint;
  if ((LangOpts.GNUMode || LangOpts.C99 || LangOpts.CPlusPlus) &&
      !CurTokenLexer)
    Hint = FixItHint::CreateInsertion(Tmp.getLocation(),"//");
  Diag(Tmp, diag::ext_pp_extra_tokens_at_eol) << DirType << Hint;
  return DiscardUntilEndOfDirective().getEnd();
}

void Preprocessor::SuggestTypoedDirective(const Token &Tok,
                                          StringRef Directive) const {
  // If this is a `.S` file, treat unknown # directives as non-preprocessor
  // directives.
  if (getLangOpts().AsmPreprocessor) return;

  std::vector<StringRef> Candidates = {
      "if", "ifdef", "ifndef", "elif", "else", "endif"
  };
  if (LangOpts.C23 || LangOpts.CPlusPlus23)
    Candidates.insert(Candidates.end(), {"elifdef", "elifndef"});

  if (std::optional<StringRef> Sugg = findSimilarStr(Directive, Candidates)) {
    // Directive cannot be coming from macro.
    assert(Tok.getLocation().isFileID());
    CharSourceRange DirectiveRange = CharSourceRange::getCharRange(
        Tok.getLocation(),
        Tok.getLocation().getLocWithOffset(Directive.size()));
    StringRef SuggValue = *Sugg;

    auto Hint = FixItHint::CreateReplacement(DirectiveRange, SuggValue);
    Diag(Tok, diag::warn_pp_invalid_directive) << 1 << SuggValue << Hint;
  }
}

/// SkipExcludedConditionalBlock - We just read a \#if or related directive and
/// decided that the subsequent tokens are in the \#if'd out portion of the
/// file.  Lex the rest of the file, until we see an \#endif.  If
/// FoundNonSkipPortion is true, then we have already emitted code for part of
/// this \#if directive, so \#else/\#elif blocks should never be entered.
/// If ElseOk is true, then \#else directives are ok, if not, then we have
/// already seen one so a \#else directive is a duplicate.  When this returns,
/// the caller can lex the first valid token.
void Preprocessor::SkipExcludedConditionalBlock(SourceLocation HashTokenLoc,
                                                SourceLocation IfTokenLoc,
                                                bool FoundNonSkipPortion,
                                                bool FoundElse,
                                                SourceLocation ElseLoc) {
  // In SkippingRangeStateTy we are depending on SkipExcludedConditionalBlock()
  // not getting called recursively by storing the RecordedSkippedRanges
  // DenseMap lookup pointer (field SkipRangePtr). SkippingRangeStateTy expects
  // that RecordedSkippedRanges won't get modified and SkipRangePtr won't be
  // invalidated. If this changes and there is a need to call
  // SkipExcludedConditionalBlock() recursively, SkippingRangeStateTy should
  // change to do a second lookup in endLexPass function instead of reusing the
  // lookup pointer.
  assert(!SkippingExcludedConditionalBlock &&
         "calling SkipExcludedConditionalBlock recursively");
  llvm::SaveAndRestore SARSkipping(SkippingExcludedConditionalBlock, true);

  ++NumSkipped;
  assert(!CurTokenLexer && "Conditional PP block cannot appear in a macro!");
  assert(CurPPLexer && "Conditional PP block must be in a file!");
  assert(CurLexer && "Conditional PP block but no current lexer set!");

  if (PreambleConditionalStack.reachedEOFWhileSkipping())
    PreambleConditionalStack.clearSkipInfo();
  else
    CurPPLexer->pushConditionalLevel(IfTokenLoc, /*isSkipping*/ false,
                                     FoundNonSkipPortion, FoundElse);

  // Enter raw mode to disable identifier lookup (and thus macro expansion),
  // disabling warnings, etc.
  CurPPLexer->LexingRawMode = true;
  Token Tok;
  SourceLocation endLoc;

  /// Keeps track and caches skipped ranges and also retrieves a prior skipped
  /// range if the same block is re-visited.
  struct SkippingRangeStateTy {
    Preprocessor &PP;

    const char *BeginPtr = nullptr;
    unsigned *SkipRangePtr = nullptr;

    SkippingRangeStateTy(Preprocessor &PP) : PP(PP) {}

    void beginLexPass() {
      if (BeginPtr)
        return; // continue skipping a block.

      // Initiate a skipping block and adjust the lexer if we already skipped it
      // before.
      BeginPtr = PP.CurLexer->getBufferLocation();
      SkipRangePtr = &PP.RecordedSkippedRanges[BeginPtr];
      if (*SkipRangePtr) {
        PP.CurLexer->seek(PP.CurLexer->getCurrentBufferOffset() + *SkipRangePtr,
                          /*IsAtStartOfLine*/ true);
      }
    }

    void endLexPass(const char *Hashptr) {
      if (!BeginPtr) {
        // Not doing normal lexing.
        assert(PP.CurLexer->isDependencyDirectivesLexer());
        return;
      }

      // Finished skipping a block, record the range if it's first time visited.
      if (!*SkipRangePtr) {
        *SkipRangePtr = Hashptr - BeginPtr;
      }
      assert(*SkipRangePtr == unsigned(Hashptr - BeginPtr));
      BeginPtr = nullptr;
      SkipRangePtr = nullptr;
    }
  } SkippingRangeState(*this);

  while (true) {
    if (CurLexer->isDependencyDirectivesLexer()) {
      CurLexer->LexDependencyDirectiveTokenWhileSkipping(Tok);
    } else {
      SkippingRangeState.beginLexPass();
      while (true) {
        CurLexer->Lex(Tok);

        if (Tok.is(tok::code_completion)) {
          setCodeCompletionReached();
          if (CodeComplete)
            CodeComplete->CodeCompleteInConditionalExclusion();
          continue;
        }

        // If this is the end of the buffer, we have an error.
        if (Tok.is(tok::eof)) {
          // We don't emit errors for unterminated conditionals here,
          // Lexer::LexEndOfFile can do that properly.
          // Just return and let the caller lex after this #include.
          if (PreambleConditionalStack.isRecording())
            PreambleConditionalStack.SkipInfo.emplace(HashTokenLoc, IfTokenLoc,
                                                      FoundNonSkipPortion,
                                                      FoundElse, ElseLoc);
          break;
        }

        // If this token is not a preprocessor directive, just skip it.
        if (Tok.isNot(tok::hash) || !Tok.isAtStartOfLine())
          continue;

        break;
      }
    }
    if (Tok.is(tok::eof))
      break;

    // We just parsed a # character at the start of a line, so we're in
    // directive mode.  Tell the lexer this so any newlines we see will be
    // converted into an EOD token (this terminates the macro).
    CurPPLexer->ParsingPreprocessorDirective = true;
    if (CurLexer) CurLexer->SetKeepWhitespaceMode(false);

    assert(Tok.is(tok::hash));
    const char *Hashptr = CurLexer->getBufferLocation() - Tok.getLength();
    assert(CurLexer->getSourceLocation(Hashptr) == Tok.getLocation());

    // Read the next token, the directive flavor.
    LexUnexpandedToken(Tok);

    // If this isn't an identifier directive (e.g. is "# 1\n" or "#\n", or
    // something bogus), skip it.
    if (Tok.isNot(tok::raw_identifier)) {
      CurPPLexer->ParsingPreprocessorDirective = false;
      // Restore comment saving mode.
      if (CurLexer) CurLexer->resetExtendedTokenMode();
      continue;
    }

    // If the first letter isn't i or e, it isn't intesting to us.  We know that
    // this is safe in the face of spelling differences, because there is no way
    // to spell an i/e in a strange way that is another letter.  Skipping this
    // allows us to avoid looking up the identifier info for #define/#undef and
    // other common directives.
    StringRef RI = Tok.getRawIdentifier();

    char FirstChar = RI[0];
    if (FirstChar >= 'a' && FirstChar <= 'z' &&
        FirstChar != 'i' && FirstChar != 'e') {
      CurPPLexer->ParsingPreprocessorDirective = false;
      // Restore comment saving mode.
      if (CurLexer) CurLexer->resetExtendedTokenMode();
      continue;
    }

    // Get the identifier name without trigraphs or embedded newlines.  Note
    // that we can't use Tok.getIdentifierInfo() because its lookup is disabled
    // when skipping.
    char DirectiveBuf[20];
    StringRef Directive;
    if (!Tok.needsCleaning() && RI.size() < 20) {
      Directive = RI;
    } else {
      std::string DirectiveStr = getSpelling(Tok);
      size_t IdLen = DirectiveStr.size();
      if (IdLen >= 20) {
        CurPPLexer->ParsingPreprocessorDirective = false;
        // Restore comment saving mode.
        if (CurLexer) CurLexer->resetExtendedTokenMode();
        continue;
      }
      memcpy(DirectiveBuf, &DirectiveStr[0], IdLen);
      Directive = StringRef(DirectiveBuf, IdLen);
    }

    if (Directive.starts_with("if")) {
      StringRef Sub = Directive.substr(2);
      if (Sub.empty() ||   // "if"
          Sub == "def" ||   // "ifdef"
          Sub == "ndef") {  // "ifndef"
        // We know the entire #if/#ifdef/#ifndef block will be skipped, don't
        // bother parsing the condition.
        DiscardUntilEndOfDirective();
        CurPPLexer->pushConditionalLevel(Tok.getLocation(), /*wasskipping*/true,
                                       /*foundnonskip*/false,
                                       /*foundelse*/false);
      } else {
        SuggestTypoedDirective(Tok, Directive);
      }
    } else if (Directive[0] == 'e') {
      StringRef Sub = Directive.substr(1);
      if (Sub == "ndif") {  // "endif"
        PPConditionalInfo CondInfo;
        CondInfo.WasSkipping = true; // Silence bogus warning.
        bool InCond = CurPPLexer->popConditionalLevel(CondInfo);
        (void)InCond;  // Silence warning in no-asserts mode.
        assert(!InCond && "Can't be skipping if not in a conditional!");

        // If we popped the outermost skipping block, we're done skipping!
        if (!CondInfo.WasSkipping) {
          SkippingRangeState.endLexPass(Hashptr);
          // Restore the value of LexingRawMode so that trailing comments
          // are handled correctly, if we've reached the outermost block.
          CurPPLexer->LexingRawMode = false;
          endLoc = CheckEndOfDirective("endif");
          CurPPLexer->LexingRawMode = true;
          if (Callbacks)
            Callbacks->Endif(Tok.getLocation(), CondInfo.IfLoc);
          break;
        } else {
          DiscardUntilEndOfDirective();
        }
      } else if (Sub == "lse") { // "else".
        // #else directive in a skipping conditional.  If not in some other
        // skipping conditional, and if #else hasn't already been seen, enter it
        // as a non-skipping conditional.
        PPConditionalInfo &CondInfo = CurPPLexer->peekConditionalLevel();

        if (!CondInfo.WasSkipping)
          SkippingRangeState.endLexPass(Hashptr);

        // If this is a #else with a #else before it, report the error.
        if (CondInfo.FoundElse)
          Diag(Tok, diag::pp_err_else_after_else);

        // Note that we've seen a #else in this conditional.
        CondInfo.FoundElse = true;

        // If the conditional is at the top level, and the #if block wasn't
        // entered, enter the #else block now.
        if (!CondInfo.WasSkipping && !CondInfo.FoundNonSkip) {
          CondInfo.FoundNonSkip = true;
          // Restore the value of LexingRawMode so that trailing comments
          // are handled correctly.
          CurPPLexer->LexingRawMode = false;
          endLoc = CheckEndOfDirective("else");
          CurPPLexer->LexingRawMode = true;
          if (Callbacks)
            Callbacks->Else(Tok.getLocation(), CondInfo.IfLoc);
          break;
        } else {
          DiscardUntilEndOfDirective();  // C99 6.10p4.
        }
      } else if (Sub == "lif") {  // "elif".
        PPConditionalInfo &CondInfo = CurPPLexer->peekConditionalLevel();

        if (!CondInfo.WasSkipping)
          SkippingRangeState.endLexPass(Hashptr);

        // If this is a #elif with a #else before it, report the error.
        if (CondInfo.FoundElse)
          Diag(Tok, diag::pp_err_elif_after_else) << PED_Elif;

        // If this is in a skipping block or if we're already handled this #if
        // block, don't bother parsing the condition.
        if (CondInfo.WasSkipping || CondInfo.FoundNonSkip) {
          // FIXME: We should probably do at least some minimal parsing of the
          // condition to verify that it is well-formed. The current state
          // allows #elif* directives with completely malformed (or missing)
          // conditions.
          DiscardUntilEndOfDirective();
        } else {
          // Restore the value of LexingRawMode so that identifiers are
          // looked up, etc, inside the #elif expression.
          assert(CurPPLexer->LexingRawMode && "We have to be skipping here!");
          CurPPLexer->LexingRawMode = false;
          IdentifierInfo *IfNDefMacro = nullptr;
          DirectiveEvalResult DER = EvaluateDirectiveExpression(IfNDefMacro);
          // Stop if Lexer became invalid after hitting code completion token.
          if (!CurPPLexer)
            return;
          const bool CondValue = DER.Conditional;
          CurPPLexer->LexingRawMode = true;
          if (Callbacks) {
            Callbacks->Elif(
                Tok.getLocation(), DER.ExprRange,
                (CondValue ? PPCallbacks::CVK_True : PPCallbacks::CVK_False),
                CondInfo.IfLoc);
          }
          // If this condition is true, enter it!
          if (CondValue) {
            CondInfo.FoundNonSkip = true;
            break;
          }
        }
      } else if (Sub == "lifdef" ||  // "elifdef"
                 Sub == "lifndef") { // "elifndef"
        bool IsElifDef = Sub == "lifdef";
        PPConditionalInfo &CondInfo = CurPPLexer->peekConditionalLevel();
        Token DirectiveToken = Tok;

        if (!CondInfo.WasSkipping)
          SkippingRangeState.endLexPass(Hashptr);

        // Warn if using `#elifdef` & `#elifndef` in not C23 & C++23 mode even
        // if this branch is in a skipping block.
        unsigned DiagID;
        if (LangOpts.CPlusPlus)
          DiagID = LangOpts.CPlusPlus23 ? diag::warn_cxx23_compat_pp_directive
                                        : diag::ext_cxx23_pp_directive;
        else
          DiagID = LangOpts.C23 ? diag::warn_c23_compat_pp_directive
                                : diag::ext_c23_pp_directive;
        Diag(Tok, DiagID) << (IsElifDef ? PED_Elifdef : PED_Elifndef);

        // If this is a #elif with a #else before it, report the error.
        if (CondInfo.FoundElse)
          Diag(Tok, diag::pp_err_elif_after_else)
              << (IsElifDef ? PED_Elifdef : PED_Elifndef);

        // If this is in a skipping block or if we're already handled this #if
        // block, don't bother parsing the condition.
        if (CondInfo.WasSkipping || CondInfo.FoundNonSkip) {
          // FIXME: We should probably do at least some minimal parsing of the
          // condition to verify that it is well-formed. The current state
          // allows #elif* directives with completely malformed (or missing)
          // conditions.
          DiscardUntilEndOfDirective();
        } else {
          // Restore the value of LexingRawMode so that identifiers are
          // looked up, etc, inside the #elif[n]def expression.
          assert(CurPPLexer->LexingRawMode && "We have to be skipping here!");
          CurPPLexer->LexingRawMode = false;
          Token MacroNameTok;
          ReadMacroName(MacroNameTok);
          CurPPLexer->LexingRawMode = true;

          // If the macro name token is tok::eod, there was an error that was
          // already reported.
          if (MacroNameTok.is(tok::eod)) {
            // Skip code until we get to #endif.  This helps with recovery by
            // not emitting an error when the #endif is reached.
            continue;
          }

          emitMacroExpansionWarnings(MacroNameTok);

          CheckEndOfDirective(IsElifDef ? "elifdef" : "elifndef");

          IdentifierInfo *MII = MacroNameTok.getIdentifierInfo();
          auto MD = getMacroDefinition(MII);
          MacroInfo *MI = MD.getMacroInfo();

          if (Callbacks) {
            if (IsElifDef) {
              Callbacks->Elifdef(DirectiveToken.getLocation(), MacroNameTok,
                                 MD);
            } else {
              Callbacks->Elifndef(DirectiveToken.getLocation(), MacroNameTok,
                                  MD);
            }
          }
          // If this condition is true, enter it!
          if (static_cast<bool>(MI) == IsElifDef) {
            CondInfo.FoundNonSkip = true;
            break;
          }
        }
      } else {
        SuggestTypoedDirective(Tok, Directive);
      }
    } else {
      SuggestTypoedDirective(Tok, Directive);
    }

    CurPPLexer->ParsingPreprocessorDirective = false;
    // Restore comment saving mode.
    if (CurLexer) CurLexer->resetExtendedTokenMode();
  }

  // Finally, if we are out of the conditional (saw an #endif or ran off the end
  // of the file, just stop skipping and return to lexing whatever came after
  // the #if block.
  CurPPLexer->LexingRawMode = false;

  // The last skipped range isn't actually skipped yet if it's truncated
  // by the end of the preamble; we'll resume parsing after the preamble.
  if (Callbacks && (Tok.isNot(tok::eof) || !isRecordingPreamble()))
    Callbacks->SourceRangeSkipped(
        SourceRange(HashTokenLoc, endLoc.isValid()
                                      ? endLoc
                                      : CurPPLexer->getSourceLocation()),
        Tok.getLocation());
}

Module *Preprocessor::getModuleForLocation(SourceLocation Loc,
                                           bool AllowTextual) {
  if (!SourceMgr.isInMainFile(Loc)) {
    // Try to determine the module of the include directive.
    // FIXME: Look into directly passing the FileEntry from LookupFile instead.
    FileID IDOfIncl = SourceMgr.getFileID(SourceMgr.getExpansionLoc(Loc));
    if (auto EntryOfIncl = SourceMgr.getFileEntryRefForID(IDOfIncl)) {
      // The include comes from an included file.
      return HeaderInfo.getModuleMap()
          .findModuleForHeader(*EntryOfIncl, AllowTextual)
          .getModule();
    }
  }

  // This is either in the main file or not in a file at all. It belongs
  // to the current module, if there is one.
  return getLangOpts().CurrentModule.empty()
             ? nullptr
             : HeaderInfo.lookupModule(getLangOpts().CurrentModule, Loc);
}

OptionalFileEntryRef
Preprocessor::getHeaderToIncludeForDiagnostics(SourceLocation IncLoc,
                                               SourceLocation Loc) {
  Module *IncM = getModuleForLocation(
      IncLoc, LangOpts.ModulesValidateTextualHeaderIncludes);

  // Walk up through the include stack, looking through textual headers of M
  // until we hit a non-textual header that we can #include. (We assume textual
  // headers of a module with non-textual headers aren't meant to be used to
  // import entities from the module.)
  auto &SM = getSourceManager();
  while (!Loc.isInvalid() && !SM.isInMainFile(Loc)) {
    auto ID = SM.getFileID(SM.getExpansionLoc(Loc));
    auto FE = SM.getFileEntryRefForID(ID);
    if (!FE)
      break;

    // We want to find all possible modules that might contain this header, so
    // search all enclosing directories for module maps and load them.
    HeaderInfo.hasModuleMap(FE->getName(), /*Root*/ nullptr,
                            SourceMgr.isInSystemHeader(Loc));

    bool InPrivateHeader = false;
    for (auto Header : HeaderInfo.findAllModulesForHeader(*FE)) {
      if (!Header.isAccessibleFrom(IncM)) {
        // It's in a private header; we can't #include it.
        // FIXME: If there's a public header in some module that re-exports it,
        // then we could suggest including that, but it's not clear that's the
        // expected way to make this entity visible.
        InPrivateHeader = true;
        continue;
      }

      // Don't suggest explicitly excluded headers.
      if (Header.getRole() == ModuleMap::ExcludedHeader)
        continue;

      // We'll suggest including textual headers below if they're
      // include-guarded.
      if (Header.getRole() & ModuleMap::TextualHeader)
        continue;

      // If we have a module import syntax, we shouldn't include a header to
      // make a particular module visible. Let the caller know they should
      // suggest an import instead.
      if (getLangOpts().ObjC || getLangOpts().CPlusPlusModules)
        return std::nullopt;

      // If this is an accessible, non-textual header of M's top-level module
      // that transitively includes the given location and makes the
      // corresponding module visible, this is the thing to #include.
      return *FE;
    }

    // FIXME: If we're bailing out due to a private header, we shouldn't suggest
    // an import either.
    if (InPrivateHeader)
      return std::nullopt;

    // If the header is includable and has an include guard, assume the
    // intended way to expose its contents is by #include, not by importing a
    // module that transitively includes it.
    if (getHeaderSearchInfo().isFileMultipleIncludeGuarded(*FE))
      return *FE;

    Loc = SM.getIncludeLoc(ID);
  }

  return std::nullopt;
}

OptionalFileEntryRef Preprocessor::LookupFile(
    SourceLocation FilenameLoc, StringRef Filename, bool isAngled,
    ConstSearchDirIterator FromDir, const FileEntry *FromFile,
    ConstSearchDirIterator *CurDirArg, SmallVectorImpl<char> *SearchPath,
    SmallVectorImpl<char> *RelativePath,
    ModuleMap::KnownHeader *SuggestedModule, bool *IsMapped,
    bool *IsFrameworkFound, bool SkipCache, bool OpenFile, bool CacheFailures) {
  ConstSearchDirIterator CurDirLocal = nullptr;
  ConstSearchDirIterator &CurDir = CurDirArg ? *CurDirArg : CurDirLocal;

  Module *RequestingModule = getModuleForLocation(
      FilenameLoc, LangOpts.ModulesValidateTextualHeaderIncludes);

  // If the header lookup mechanism may be relative to the current inclusion
  // stack, record the parent #includes.
  SmallVector<std::pair<OptionalFileEntryRef, DirectoryEntryRef>, 16> Includers;
  bool BuildSystemModule = false;
  if (!FromDir && !FromFile) {
    FileID FID = getCurrentFileLexer()->getFileID();
    OptionalFileEntryRef FileEnt = SourceMgr.getFileEntryRefForID(FID);

    // If there is no file entry associated with this file, it must be the
    // predefines buffer or the module includes buffer. Any other file is not
    // lexed with a normal lexer, so it won't be scanned for preprocessor
    // directives.
    //
    // If we have the predefines buffer, resolve #include references (which come
    // from the -include command line argument) from the current working
    // directory instead of relative to the main file.
    //
    // If we have the module includes buffer, resolve #include references (which
    // come from header declarations in the module map) relative to the module
    // map file.
    if (!FileEnt) {
      if (FID == SourceMgr.getMainFileID() && MainFileDir) {
        auto IncludeDir =
            HeaderInfo.getModuleMap().shouldImportRelativeToBuiltinIncludeDir(
                Filename, getCurrentModule())
                ? HeaderInfo.getModuleMap().getBuiltinDir()
                : MainFileDir;
        Includers.push_back(std::make_pair(std::nullopt, *IncludeDir));
        BuildSystemModule = getCurrentModule()->IsSystem;
      } else if ((FileEnt = SourceMgr.getFileEntryRefForID(
                      SourceMgr.getMainFileID()))) {
        auto CWD = FileMgr.getOptionalDirectoryRef(".");
        Includers.push_back(std::make_pair(*FileEnt, *CWD));
      }
    } else {
      Includers.push_back(std::make_pair(*FileEnt, FileEnt->getDir()));
    }

    // MSVC searches the current include stack from top to bottom for
    // headers included by quoted include directives.
    // See: http://msdn.microsoft.com/en-us/library/36k2cdd4.aspx
    if (LangOpts.MSVCCompat && !isAngled) {
      for (IncludeStackInfo &ISEntry : llvm::reverse(IncludeMacroStack)) {
        if (IsFileLexer(ISEntry))
          if ((FileEnt = ISEntry.ThePPLexer->getFileEntry()))
            Includers.push_back(std::make_pair(*FileEnt, FileEnt->getDir()));
      }
    }
  }

  CurDir = CurDirLookup;

  if (FromFile) {
    // We're supposed to start looking from after a particular file. Search
    // the include path until we find that file or run out of files.
    ConstSearchDirIterator TmpCurDir = CurDir;
    ConstSearchDirIterator TmpFromDir = nullptr;
    while (OptionalFileEntryRef FE = HeaderInfo.LookupFile(
               Filename, FilenameLoc, isAngled, TmpFromDir, &TmpCurDir,
               Includers, SearchPath, RelativePath, RequestingModule,
               SuggestedModule, /*IsMapped=*/nullptr,
               /*IsFrameworkFound=*/nullptr, SkipCache)) {
      // Keep looking as if this file did a #include_next.
      TmpFromDir = TmpCurDir;
      ++TmpFromDir;
      if (&FE->getFileEntry() == FromFile) {
        // Found it.
        FromDir = TmpFromDir;
        CurDir = TmpCurDir;
        break;
      }
    }
  }

  // Do a standard file entry lookup.
  OptionalFileEntryRef FE = HeaderInfo.LookupFile(
      Filename, FilenameLoc, isAngled, FromDir, &CurDir, Includers, SearchPath,
      RelativePath, RequestingModule, SuggestedModule, IsMapped,
      IsFrameworkFound, SkipCache, BuildSystemModule, OpenFile, CacheFailures);
  if (FE)
    return FE;

  OptionalFileEntryRef CurFileEnt;
  // Otherwise, see if this is a subframework header.  If so, this is relative
  // to one of the headers on the #include stack.  Walk the list of the current
  // headers on the #include stack and pass them to HeaderInfo.
  if (IsFileLexer()) {
    if ((CurFileEnt = CurPPLexer->getFileEntry())) {
      if (OptionalFileEntryRef FE = HeaderInfo.LookupSubframeworkHeader(
              Filename, *CurFileEnt, SearchPath, RelativePath, RequestingModule,
              SuggestedModule)) {
        return FE;
      }
    }
  }

  for (IncludeStackInfo &ISEntry : llvm::reverse(IncludeMacroStack)) {
    if (IsFileLexer(ISEntry)) {
      if ((CurFileEnt = ISEntry.ThePPLexer->getFileEntry())) {
        if (OptionalFileEntryRef FE = HeaderInfo.LookupSubframeworkHeader(
                Filename, *CurFileEnt, SearchPath, RelativePath,
                RequestingModule, SuggestedModule)) {
          return FE;
        }
      }
    }
  }

  // Otherwise, we really couldn't find the file.
  return std::nullopt;
}

OptionalFileEntryRef
Preprocessor::LookupEmbedFile(StringRef Filename, bool isAngled, bool OpenFile,
                              const FileEntry *LookupFromFile) {
  FileManager &FM = this->getFileManager();
  if (llvm::sys::path::is_absolute(Filename)) {
    // lookup path or immediately fail
    llvm::Expected<FileEntryRef> ShouldBeEntry =
        FM.getFileRef(Filename, OpenFile);
    return llvm::expectedToOptional(std::move(ShouldBeEntry));
  }

  auto SeparateComponents = [](SmallVectorImpl<char> &LookupPath,
                               StringRef StartingFrom, StringRef FileName,
                               bool RemoveInitialFileComponentFromLookupPath) {
    llvm::sys::path::native(StartingFrom, LookupPath);
    if (RemoveInitialFileComponentFromLookupPath)
      llvm::sys::path::remove_filename(LookupPath);
    if (!LookupPath.empty() &&
        !llvm::sys::path::is_separator(LookupPath.back())) {
      LookupPath.push_back(llvm::sys::path::get_separator().front());
    }
    LookupPath.append(FileName.begin(), FileName.end());
  };

  // Otherwise, it's search time!
  SmallString<512> LookupPath;
  // Non-angled lookup
  if (!isAngled) {
    if (LookupFromFile) {
      // Use file-based lookup.
      StringRef FullFileDir = LookupFromFile->tryGetRealPathName();
      if (!FullFileDir.empty()) {
        SeparateComponents(LookupPath, FullFileDir, Filename, true);
        llvm::Expected<FileEntryRef> ShouldBeEntry =
            FM.getFileRef(LookupPath, OpenFile);
        if (ShouldBeEntry)
          return llvm::expectedToOptional(std::move(ShouldBeEntry));
        llvm::consumeError(ShouldBeEntry.takeError());
      }
    }

    // Otherwise, do working directory lookup.
    LookupPath.clear();
    auto MaybeWorkingDirEntry = FM.getDirectoryRef(".");
    if (MaybeWorkingDirEntry) {
      DirectoryEntryRef WorkingDirEntry = *MaybeWorkingDirEntry;
      StringRef WorkingDir = WorkingDirEntry.getName();
      if (!WorkingDir.empty()) {
        SeparateComponents(LookupPath, WorkingDir, Filename, false);
        llvm::Expected<FileEntryRef> ShouldBeEntry =
            FM.getFileRef(LookupPath, OpenFile);
        if (ShouldBeEntry)
          return llvm::expectedToOptional(std::move(ShouldBeEntry));
        llvm::consumeError(ShouldBeEntry.takeError());
      }
    }
  }

  for (const auto &Entry : PPOpts->EmbedEntries) {
    LookupPath.clear();
    SeparateComponents(LookupPath, Entry, Filename, false);
    llvm::Expected<FileEntryRef> ShouldBeEntry =
        FM.getFileRef(LookupPath, OpenFile);
    if (ShouldBeEntry)
      return llvm::expectedToOptional(std::move(ShouldBeEntry));
    llvm::consumeError(ShouldBeEntry.takeError());
  }
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Preprocessor Directive Handling.
//===----------------------------------------------------------------------===//

class Preprocessor::ResetMacroExpansionHelper {
public:
  ResetMacroExpansionHelper(Preprocessor *pp)
    : PP(pp), save(pp->DisableMacroExpansion) {
    if (pp->MacroExpansionInDirectivesOverride)
      pp->DisableMacroExpansion = false;
  }

  ~ResetMacroExpansionHelper() {
    PP->DisableMacroExpansion = save;
  }

private:
  Preprocessor *PP;
  bool save;
};

/// Process a directive while looking for the through header or a #pragma
/// hdrstop. The following directives are handled:
/// #include (to check if it is the through header)
/// #define (to warn about macros that don't match the PCH)
/// #pragma (to check for pragma hdrstop).
/// All other directives are completely discarded.
void Preprocessor::HandleSkippedDirectiveWhileUsingPCH(Token &Result,
                                                       SourceLocation HashLoc) {
  if (const IdentifierInfo *II = Result.getIdentifierInfo()) {
    if (II->getPPKeywordID() == tok::pp_define) {
      return HandleDefineDirective(Result,
                                   /*ImmediatelyAfterHeaderGuard=*/false);
    }
    if (SkippingUntilPCHThroughHeader &&
        II->getPPKeywordID() == tok::pp_include) {
      return HandleIncludeDirective(HashLoc, Result);
    }
    if (SkippingUntilPragmaHdrStop && II->getPPKeywordID() == tok::pp_pragma) {
      Lex(Result);
      auto *II = Result.getIdentifierInfo();
      if (II && II->getName() == "hdrstop")
        return HandlePragmaHdrstop(Result);
    }
  }
  DiscardUntilEndOfDirective();
}

/// HandleDirective - This callback is invoked when the lexer sees a # token
/// at the start of a line.  This consumes the directive, modifies the
/// lexer/preprocessor state, and advances the lexer(s) so that the next token
/// read is the correct one.
void Preprocessor::HandleDirective(Token &Result) {
  // FIXME: Traditional: # with whitespace before it not recognized by K&R?

  // We just parsed a # character at the start of a line, so we're in directive
  // mode.  Tell the lexer this so any newlines we see will be converted into an
  // EOD token (which terminates the directive).
  CurPPLexer->ParsingPreprocessorDirective = true;
  if (CurLexer) CurLexer->SetKeepWhitespaceMode(false);

  bool ImmediatelyAfterTopLevelIfndef =
      CurPPLexer->MIOpt.getImmediatelyAfterTopLevelIfndef();
  CurPPLexer->MIOpt.resetImmediatelyAfterTopLevelIfndef();

  ++NumDirectives;

  // We are about to read a token.  For the multiple-include optimization FA to
  // work, we have to remember if we had read any tokens *before* this
  // pp-directive.
  bool ReadAnyTokensBeforeDirective =CurPPLexer->MIOpt.getHasReadAnyTokensVal();

  // Save the '#' token in case we need to return it later.
  Token SavedHash = Result;

  // Read the next token, the directive flavor.  This isn't expanded due to
  // C99 6.10.3p8.
  LexUnexpandedToken(Result);

  // C99 6.10.3p11: Is this preprocessor directive in macro invocation?  e.g.:
  //   #define A(x) #x
  //   A(abc
  //     #warning blah
  //   def)
  // If so, the user is relying on undefined behavior, emit a diagnostic. Do
  // not support this for #include-like directives, since that can result in
  // terrible diagnostics, and does not work in GCC.
  if (InMacroArgs) {
    if (IdentifierInfo *II = Result.getIdentifierInfo()) {
      switch (II->getPPKeywordID()) {
      case tok::pp_include:
      case tok::pp_import:
      case tok::pp_include_next:
      case tok::pp___include_macros:
      case tok::pp_pragma:
      case tok::pp_embed:
        Diag(Result, diag::err_embedded_directive) << II->getName();
        Diag(*ArgMacro, diag::note_macro_expansion_here)
            << ArgMacro->getIdentifierInfo();
        DiscardUntilEndOfDirective();
        return;
      default:
        break;
      }
    }
    Diag(Result, diag::ext_embedded_directive);
  }

  // Temporarily enable macro expansion if set so
  // and reset to previous state when returning from this function.
  ResetMacroExpansionHelper helper(this);

  if (SkippingUntilPCHThroughHeader || SkippingUntilPragmaHdrStop)
    return HandleSkippedDirectiveWhileUsingPCH(Result, SavedHash.getLocation());

  switch (Result.getKind()) {
  case tok::eod:
    // Ignore the null directive with regards to the multiple-include
    // optimization, i.e. allow the null directive to appear outside of the
    // include guard and still enable the multiple-include optimization.
    CurPPLexer->MIOpt.SetReadToken(ReadAnyTokensBeforeDirective);
    return;   // null directive.
  case tok::code_completion:
    setCodeCompletionReached();
    if (CodeComplete)
      CodeComplete->CodeCompleteDirective(
                                    CurPPLexer->getConditionalStackDepth() > 0);
    return;
  case tok::numeric_constant:  // # 7  GNU line marker directive.
    // In a .S file "# 4" may be a comment so don't treat it as a preprocessor
    // directive. However do permit it in the predefines file, as we use line
    // markers to mark the builtin macros as being in a system header.
    if (getLangOpts().AsmPreprocessor &&
        SourceMgr.getFileID(SavedHash.getLocation()) != getPredefinesFileID())
      break;
    return HandleDigitDirective(Result);
  default:
    IdentifierInfo *II = Result.getIdentifierInfo();
    if (!II) break; // Not an identifier.

    // Ask what the preprocessor keyword ID is.
    switch (II->getPPKeywordID()) {
    default: break;
    // C99 6.10.1 - Conditional Inclusion.
    case tok::pp_if:
      return HandleIfDirective(Result, SavedHash, ReadAnyTokensBeforeDirective);
    case tok::pp_ifdef:
      return HandleIfdefDirective(Result, SavedHash, false,
                                  true /*not valid for miopt*/);
    case tok::pp_ifndef:
      return HandleIfdefDirective(Result, SavedHash, true,
                                  ReadAnyTokensBeforeDirective);
    case tok::pp_elif:
    case tok::pp_elifdef:
    case tok::pp_elifndef:
      return HandleElifFamilyDirective(Result, SavedHash, II->getPPKeywordID());

    case tok::pp_else:
      return HandleElseDirective(Result, SavedHash);
    case tok::pp_endif:
      return HandleEndifDirective(Result);

    // C99 6.10.2 - Source File Inclusion.
    case tok::pp_include:
      // Handle #include.
      return HandleIncludeDirective(SavedHash.getLocation(), Result);
    case tok::pp___include_macros:
      // Handle -imacros.
      return HandleIncludeMacrosDirective(SavedHash.getLocation(), Result);

    // C99 6.10.3 - Macro Replacement.
    case tok::pp_define:
      return HandleDefineDirective(Result, ImmediatelyAfterTopLevelIfndef);
    case tok::pp_undef:
      return HandleUndefDirective();

    // C99 6.10.4 - Line Control.
    case tok::pp_line:
      return HandleLineDirective();

    // C99 6.10.5 - Error Directive.
    case tok::pp_error:
      return HandleUserDiagnosticDirective(Result, false);

    // C99 6.10.6 - Pragma Directive.
    case tok::pp_pragma:
      return HandlePragmaDirective({PIK_HashPragma, SavedHash.getLocation()});

    // GNU Extensions.
    case tok::pp_import:
      return HandleImportDirective(SavedHash.getLocation(), Result);
    case tok::pp_include_next:
      return HandleIncludeNextDirective(SavedHash.getLocation(), Result);

    case tok::pp_warning:
      if (LangOpts.CPlusPlus)
        Diag(Result, LangOpts.CPlusPlus23
                         ? diag::warn_cxx23_compat_warning_directive
                         : diag::ext_pp_warning_directive)
            << /*C++23*/ 1;
      else
        Diag(Result, LangOpts.C23 ? diag::warn_c23_compat_warning_directive
                                  : diag::ext_pp_warning_directive)
            << /*C23*/ 0;

      return HandleUserDiagnosticDirective(Result, true);
    case tok::pp_ident:
      return HandleIdentSCCSDirective(Result);
    case tok::pp_sccs:
      return HandleIdentSCCSDirective(Result);
    case tok::pp_embed:
      return HandleEmbedDirective(SavedHash.getLocation(), Result,
                                  getCurrentFileLexer()
                                      ? *getCurrentFileLexer()->getFileEntry()
                                      : static_cast<FileEntry *>(nullptr));
    case tok::pp_assert:
      //isExtension = true;  // FIXME: implement #assert
      break;
    case tok::pp_unassert:
      //isExtension = true;  // FIXME: implement #unassert
      break;

    case tok::pp___public_macro:
      if (getLangOpts().Modules || getLangOpts().ModulesLocalVisibility)
        return HandleMacroPublicDirective(Result);
      break;

    case tok::pp___private_macro:
      if (getLangOpts().Modules || getLangOpts().ModulesLocalVisibility)
        return HandleMacroPrivateDirective();
      break;
    }
    break;
  }

  // If this is a .S file, treat unknown # directives as non-preprocessor
  // directives.  This is important because # may be a comment or introduce
  // various pseudo-ops.  Just return the # token and push back the following
  // token to be lexed next time.
  if (getLangOpts().AsmPreprocessor) {
    auto Toks = std::make_unique<Token[]>(2);
    // Return the # and the token after it.
    Toks[0] = SavedHash;
    Toks[1] = Result;

    // If the second token is a hashhash token, then we need to translate it to
    // unknown so the token lexer doesn't try to perform token pasting.
    if (Result.is(tok::hashhash))
      Toks[1].setKind(tok::unknown);

    // Enter this token stream so that we re-lex the tokens.  Make sure to
    // enable macro expansion, in case the token after the # is an identifier
    // that is expanded.
    EnterTokenStream(std::move(Toks), 2, false, /*IsReinject*/false);
    return;
  }

  // If we reached here, the preprocessing token is not valid!
  // Start suggesting if a similar directive found.
  Diag(Result, diag::err_pp_invalid_directive) << 0;

  // Read the rest of the PP line.
  DiscardUntilEndOfDirective();

  // Okay, we're done parsing the directive.
}

/// GetLineValue - Convert a numeric token into an unsigned value, emitting
/// Diagnostic DiagID if it is invalid, and returning the value in Val.
static bool GetLineValue(Token &DigitTok, unsigned &Val,
                         unsigned DiagID, Preprocessor &PP,
                         bool IsGNULineDirective=false) {
  if (DigitTok.isNot(tok::numeric_constant)) {
    PP.Diag(DigitTok, DiagID);

    if (DigitTok.isNot(tok::eod))
      PP.DiscardUntilEndOfDirective();
    return true;
  }

  SmallString<64> IntegerBuffer;
  IntegerBuffer.resize(DigitTok.getLength());
  const char *DigitTokBegin = &IntegerBuffer[0];
  bool Invalid = false;
  unsigned ActualLength = PP.getSpelling(DigitTok, DigitTokBegin, &Invalid);
  if (Invalid)
    return true;

  // Verify that we have a simple digit-sequence, and compute the value.  This
  // is always a simple digit string computed in decimal, so we do this manually
  // here.
  Val = 0;
  for (unsigned i = 0; i != ActualLength; ++i) {
    // C++1y [lex.fcon]p1:
    //   Optional separating single quotes in a digit-sequence are ignored
    if (DigitTokBegin[i] == '\'')
      continue;

    if (!isDigit(DigitTokBegin[i])) {
      PP.Diag(PP.AdvanceToTokenCharacter(DigitTok.getLocation(), i),
              diag::err_pp_line_digit_sequence) << IsGNULineDirective;
      PP.DiscardUntilEndOfDirective();
      return true;
    }

    unsigned NextVal = Val*10+(DigitTokBegin[i]-'0');
    if (NextVal < Val) { // overflow.
      PP.Diag(DigitTok, DiagID);
      PP.DiscardUntilEndOfDirective();
      return true;
    }
    Val = NextVal;
  }

  if (DigitTokBegin[0] == '0' && Val)
    PP.Diag(DigitTok.getLocation(), diag::warn_pp_line_decimal)
      << IsGNULineDirective;

  return false;
}

/// Handle a \#line directive: C99 6.10.4.
///
/// The two acceptable forms are:
/// \verbatim
///   # line digit-sequence
///   # line digit-sequence "s-char-sequence"
/// \endverbatim
void Preprocessor::HandleLineDirective() {
  // Read the line # and string argument.  Per C99 6.10.4p5, these tokens are
  // expanded.
  Token DigitTok;
  Lex(DigitTok);

  // Validate the number and convert it to an unsigned.
  unsigned LineNo;
  if (GetLineValue(DigitTok, LineNo, diag::err_pp_line_requires_integer,*this))
    return;

  if (LineNo == 0)
    Diag(DigitTok, diag::ext_pp_line_zero);

  // Enforce C99 6.10.4p3: "The digit sequence shall not specify ... a
  // number greater than 2147483647".  C90 requires that the line # be <= 32767.
  unsigned LineLimit = 32768U;
  if (LangOpts.C99 || LangOpts.CPlusPlus11)
    LineLimit = 2147483648U;
  if (LineNo >= LineLimit)
    Diag(DigitTok, diag::ext_pp_line_too_big) << LineLimit;
  else if (LangOpts.CPlusPlus11 && LineNo >= 32768U)
    Diag(DigitTok, diag::warn_cxx98_compat_pp_line_too_big);

  int FilenameID = -1;
  Token StrTok;
  Lex(StrTok);

  // If the StrTok is "eod", then it wasn't present.  Otherwise, it must be a
  // string followed by eod.
  if (StrTok.is(tok::eod))
    ; // ok
  else if (StrTok.isNot(tok::string_literal)) {
    Diag(StrTok, diag::err_pp_line_invalid_filename);
    DiscardUntilEndOfDirective();
    return;
  } else if (StrTok.hasUDSuffix()) {
    Diag(StrTok, diag::err_invalid_string_udl);
    DiscardUntilEndOfDirective();
    return;
  } else {
    // Parse and validate the string, converting it into a unique ID.
    StringLiteralParser Literal(StrTok, *this);
    assert(Literal.isOrdinary() && "Didn't allow wide strings in");
    if (Literal.hadError) {
      DiscardUntilEndOfDirective();
      return;
    }
    if (Literal.Pascal) {
      Diag(StrTok, diag::err_pp_linemarker_invalid_filename);
      DiscardUntilEndOfDirective();
      return;
    }
    FilenameID = SourceMgr.getLineTableFilenameID(Literal.GetString());

    // Verify that there is nothing after the string, other than EOD.  Because
    // of C99 6.10.4p5, macros that expand to empty tokens are ok.
    CheckEndOfDirective("line", true);
  }

  // Take the file kind of the file containing the #line directive. #line
  // directives are often used for generated sources from the same codebase, so
  // the new file should generally be classified the same way as the current
  // file. This is visible in GCC's pre-processed output, which rewrites #line
  // to GNU line markers.
  SrcMgr::CharacteristicKind FileKind =
      SourceMgr.getFileCharacteristic(DigitTok.getLocation());

  SourceMgr.AddLineNote(DigitTok.getLocation(), LineNo, FilenameID, false,
                        false, FileKind);

  if (Callbacks)
    Callbacks->FileChanged(CurPPLexer->getSourceLocation(),
                           PPCallbacks::RenameFile, FileKind);
}

/// ReadLineMarkerFlags - Parse and validate any flags at the end of a GNU line
/// marker directive.
static bool ReadLineMarkerFlags(bool &IsFileEntry, bool &IsFileExit,
                                SrcMgr::CharacteristicKind &FileKind,
                                Preprocessor &PP) {
  unsigned FlagVal;
  Token FlagTok;
  PP.Lex(FlagTok);
  if (FlagTok.is(tok::eod)) return false;
  if (GetLineValue(FlagTok, FlagVal, diag::err_pp_linemarker_invalid_flag, PP))
    return true;

  if (FlagVal == 1) {
    IsFileEntry = true;

    PP.Lex(FlagTok);
    if (FlagTok.is(tok::eod)) return false;
    if (GetLineValue(FlagTok, FlagVal, diag::err_pp_linemarker_invalid_flag,PP))
      return true;
  } else if (FlagVal == 2) {
    IsFileExit = true;

    SourceManager &SM = PP.getSourceManager();
    // If we are leaving the current presumed file, check to make sure the
    // presumed include stack isn't empty!
    FileID CurFileID =
      SM.getDecomposedExpansionLoc(FlagTok.getLocation()).first;
    PresumedLoc PLoc = SM.getPresumedLoc(FlagTok.getLocation());
    if (PLoc.isInvalid())
      return true;

    // If there is no include loc (main file) or if the include loc is in a
    // different physical file, then we aren't in a "1" line marker flag region.
    SourceLocation IncLoc = PLoc.getIncludeLoc();
    if (IncLoc.isInvalid() ||
        SM.getDecomposedExpansionLoc(IncLoc).first != CurFileID) {
      PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_pop);
      PP.DiscardUntilEndOfDirective();
      return true;
    }

    PP.Lex(FlagTok);
    if (FlagTok.is(tok::eod)) return false;
    if (GetLineValue(FlagTok, FlagVal, diag::err_pp_linemarker_invalid_flag,PP))
      return true;
  }

  // We must have 3 if there are still flags.
  if (FlagVal != 3) {
    PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_flag);
    PP.DiscardUntilEndOfDirective();
    return true;
  }

  FileKind = SrcMgr::C_System;

  PP.Lex(FlagTok);
  if (FlagTok.is(tok::eod)) return false;
  if (GetLineValue(FlagTok, FlagVal, diag::err_pp_linemarker_invalid_flag, PP))
    return true;

  // We must have 4 if there is yet another flag.
  if (FlagVal != 4) {
    PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_flag);
    PP.DiscardUntilEndOfDirective();
    return true;
  }

  FileKind = SrcMgr::C_ExternCSystem;

  PP.Lex(FlagTok);
  if (FlagTok.is(tok::eod)) return false;

  // There are no more valid flags here.
  PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_flag);
  PP.DiscardUntilEndOfDirective();
  return true;
}

/// HandleDigitDirective - Handle a GNU line marker directive, whose syntax is
/// one of the following forms:
///
///     # 42
///     # 42 "file" ('1' | '2')?
///     # 42 "file" ('1' | '2')? '3' '4'?
///
void Preprocessor::HandleDigitDirective(Token &DigitTok) {
  // Validate the number and convert it to an unsigned.  GNU does not have a
  // line # limit other than it fit in 32-bits.
  unsigned LineNo;
  if (GetLineValue(DigitTok, LineNo, diag::err_pp_linemarker_requires_integer,
                   *this, true))
    return;

  Token StrTok;
  Lex(StrTok);

  bool IsFileEntry = false, IsFileExit = false;
  int FilenameID = -1;
  SrcMgr::CharacteristicKind FileKind = SrcMgr::C_User;

  // If the StrTok is "eod", then it wasn't present.  Otherwise, it must be a
  // string followed by eod.
  if (StrTok.is(tok::eod)) {
    Diag(StrTok, diag::ext_pp_gnu_line_directive);
    // Treat this like "#line NN", which doesn't change file characteristics.
    FileKind = SourceMgr.getFileCharacteristic(DigitTok.getLocation());
  } else if (StrTok.isNot(tok::string_literal)) {
    Diag(StrTok, diag::err_pp_linemarker_invalid_filename);
    DiscardUntilEndOfDirective();
    return;
  } else if (StrTok.hasUDSuffix()) {
    Diag(StrTok, diag::err_invalid_string_udl);
    DiscardUntilEndOfDirective();
    return;
  } else {
    // Parse and validate the string, converting it into a unique ID.
    StringLiteralParser Literal(StrTok, *this);
    assert(Literal.isOrdinary() && "Didn't allow wide strings in");
    if (Literal.hadError) {
      DiscardUntilEndOfDirective();
      return;
    }
    if (Literal.Pascal) {
      Diag(StrTok, diag::err_pp_linemarker_invalid_filename);
      DiscardUntilEndOfDirective();
      return;
    }

    // If a filename was present, read any flags that are present.
    if (ReadLineMarkerFlags(IsFileEntry, IsFileExit, FileKind, *this))
      return;
    if (!SourceMgr.isWrittenInBuiltinFile(DigitTok.getLocation()) &&
        !SourceMgr.isWrittenInCommandLineFile(DigitTok.getLocation()))
      Diag(StrTok, diag::ext_pp_gnu_line_directive);

    // Exiting to an empty string means pop to the including file, so leave
    // FilenameID as -1 in that case.
    if (!(IsFileExit && Literal.GetString().empty()))
      FilenameID = SourceMgr.getLineTableFilenameID(Literal.GetString());
  }

  // Create a line note with this information.
  SourceMgr.AddLineNote(DigitTok.getLocation(), LineNo, FilenameID, IsFileEntry,
                        IsFileExit, FileKind);

  // If the preprocessor has callbacks installed, notify them of the #line
  // change.  This is used so that the line marker comes out in -E mode for
  // example.
  if (Callbacks) {
    PPCallbacks::FileChangeReason Reason = PPCallbacks::RenameFile;
    if (IsFileEntry)
      Reason = PPCallbacks::EnterFile;
    else if (IsFileExit)
      Reason = PPCallbacks::ExitFile;

    Callbacks->FileChanged(CurPPLexer->getSourceLocation(), Reason, FileKind);
  }
}

/// HandleUserDiagnosticDirective - Handle a #warning or #error directive.
///
void Preprocessor::HandleUserDiagnosticDirective(Token &Tok,
                                                 bool isWarning) {
  // Read the rest of the line raw.  We do this because we don't want macros
  // to be expanded and we don't require that the tokens be valid preprocessing
  // tokens.  For example, this is allowed: "#warning `   'foo".  GCC does
  // collapse multiple consecutive white space between tokens, but this isn't
  // specified by the standard.
  SmallString<128> Message;
  CurLexer->ReadToEndOfLine(&Message);

  // Find the first non-whitespace character, so that we can make the
  // diagnostic more succinct.
  StringRef Msg = Message.str().ltrim(' ');

  if (isWarning)
    Diag(Tok, diag::pp_hash_warning) << Msg;
  else
    Diag(Tok, diag::err_pp_hash_error) << Msg;
}

/// HandleIdentSCCSDirective - Handle a #ident/#sccs directive.
///
void Preprocessor::HandleIdentSCCSDirective(Token &Tok) {
  // Yes, this directive is an extension.
  Diag(Tok, diag::ext_pp_ident_directive);

  // Read the string argument.
  Token StrTok;
  Lex(StrTok);

  // If the token kind isn't a string, it's a malformed directive.
  if (StrTok.isNot(tok::string_literal) &&
      StrTok.isNot(tok::wide_string_literal)) {
    Diag(StrTok, diag::err_pp_malformed_ident);
    if (StrTok.isNot(tok::eod))
      DiscardUntilEndOfDirective();
    return;
  }

  if (StrTok.hasUDSuffix()) {
    Diag(StrTok, diag::err_invalid_string_udl);
    DiscardUntilEndOfDirective();
    return;
  }

  // Verify that there is nothing after the string, other than EOD.
  CheckEndOfDirective("ident");

  if (Callbacks) {
    bool Invalid = false;
    std::string Str = getSpelling(StrTok, &Invalid);
    if (!Invalid)
      Callbacks->Ident(Tok.getLocation(), Str);
  }
}

/// Handle a #public directive.
void Preprocessor::HandleMacroPublicDirective(Token &Tok) {
  Token MacroNameTok;
  ReadMacroName(MacroNameTok, MU_Undef);

  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.is(tok::eod))
    return;

  // Check to see if this is the last token on the #__public_macro line.
  CheckEndOfDirective("__public_macro");

  IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
  // Okay, we finally have a valid identifier to undef.
  MacroDirective *MD = getLocalMacroDirective(II);

  // If the macro is not defined, this is an error.
  if (!MD) {
    Diag(MacroNameTok, diag::err_pp_visibility_non_macro) << II;
    return;
  }

  // Note that this macro has now been exported.
  appendMacroDirective(II, AllocateVisibilityMacroDirective(
                                MacroNameTok.getLocation(), /*isPublic=*/true));
}

/// Handle a #private directive.
void Preprocessor::HandleMacroPrivateDirective() {
  Token MacroNameTok;
  ReadMacroName(MacroNameTok, MU_Undef);

  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.is(tok::eod))
    return;

  // Check to see if this is the last token on the #__private_macro line.
  CheckEndOfDirective("__private_macro");

  IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
  // Okay, we finally have a valid identifier to undef.
  MacroDirective *MD = getLocalMacroDirective(II);

  // If the macro is not defined, this is an error.
  if (!MD) {
    Diag(MacroNameTok, diag::err_pp_visibility_non_macro) << II;
    return;
  }

  // Note that this macro has now been marked private.
  appendMacroDirective(II, AllocateVisibilityMacroDirective(
                               MacroNameTok.getLocation(), /*isPublic=*/false));
}

//===----------------------------------------------------------------------===//
// Preprocessor Include Directive Handling.
//===----------------------------------------------------------------------===//

/// GetIncludeFilenameSpelling - Turn the specified lexer token into a fully
/// checked and spelled filename, e.g. as an operand of \#include. This returns
/// true if the input filename was in <>'s or false if it were in ""'s.  The
/// caller is expected to provide a buffer that is large enough to hold the
/// spelling of the filename, but is also expected to handle the case when
/// this method decides to use a different buffer.
bool Preprocessor::GetIncludeFilenameSpelling(SourceLocation Loc,
                                              StringRef &Buffer) {
  // Get the text form of the filename.
  assert(!Buffer.empty() && "Can't have tokens with empty spellings!");

  // FIXME: Consider warning on some of the cases described in C11 6.4.7/3 and
  // C++20 [lex.header]/2:
  //
  // If `"`, `'`, `\`, `/*`, or `//` appears in a header-name, then
  //   in C: behavior is undefined
  //   in C++: program is conditionally-supported with implementation-defined
  //           semantics

  // Make sure the filename is <x> or "x".
  bool isAngled;
  if (Buffer[0] == '<') {
    if (Buffer.back() != '>') {
      Diag(Loc, diag::err_pp_expects_filename);
      Buffer = StringRef();
      return true;
    }
    isAngled = true;
  } else if (Buffer[0] == '"') {
    if (Buffer.back() != '"') {
      Diag(Loc, diag::err_pp_expects_filename);
      Buffer = StringRef();
      return true;
    }
    isAngled = false;
  } else {
    Diag(Loc, diag::err_pp_expects_filename);
    Buffer = StringRef();
    return true;
  }

  // Diagnose #include "" as invalid.
  if (Buffer.size() <= 2) {
    Diag(Loc, diag::err_pp_empty_filename);
    Buffer = StringRef();
    return true;
  }

  // Skip the brackets.
  Buffer = Buffer.substr(1, Buffer.size()-2);
  return isAngled;
}

/// Push a token onto the token stream containing an annotation.
void Preprocessor::EnterAnnotationToken(SourceRange Range,
                                        tok::TokenKind Kind,
                                        void *AnnotationVal) {
  // FIXME: Produce this as the current token directly, rather than
  // allocating a new token for it.
  auto Tok = std::make_unique<Token[]>(1);
  Tok[0].startToken();
  Tok[0].setKind(Kind);
  Tok[0].setLocation(Range.getBegin());
  Tok[0].setAnnotationEndLoc(Range.getEnd());
  Tok[0].setAnnotationValue(AnnotationVal);
  EnterTokenStream(std::move(Tok), 1, true, /*IsReinject*/ false);
}

/// Produce a diagnostic informing the user that a #include or similar
/// was implicitly treated as a module import.
static void diagnoseAutoModuleImport(
    Preprocessor &PP, SourceLocation HashLoc, Token &IncludeTok,
    ArrayRef<std::pair<IdentifierInfo *, SourceLocation>> Path,
    SourceLocation PathEnd) {
  SmallString<128> PathString;
  for (size_t I = 0, N = Path.size(); I != N; ++I) {
    if (I)
      PathString += '.';
    PathString += Path[I].first->getName();
  }

  int IncludeKind = 0;
  switch (IncludeTok.getIdentifierInfo()->getPPKeywordID()) {
  case tok::pp_include:
    IncludeKind = 0;
    break;

  case tok::pp_import:
    IncludeKind = 1;
    break;

  case tok::pp_include_next:
    IncludeKind = 2;
    break;

  case tok::pp___include_macros:
    IncludeKind = 3;
    break;

  default:
    llvm_unreachable("unknown include directive kind");
  }

  PP.Diag(HashLoc, diag::remark_pp_include_directive_modular_translation)
      << IncludeKind << PathString;
}

// Given a vector of path components and a string containing the real
// path to the file, build a properly-cased replacement in the vector,
// and return true if the replacement should be suggested.
static bool trySimplifyPath(SmallVectorImpl<StringRef> &Components,
                            StringRef RealPathName,
                            llvm::sys::path::Style Separator) {
  auto RealPathComponentIter = llvm::sys::path::rbegin(RealPathName);
  auto RealPathComponentEnd = llvm::sys::path::rend(RealPathName);
  int Cnt = 0;
  bool SuggestReplacement = false;

  auto IsSep = [Separator](StringRef Component) {
    return Component.size() == 1 &&
           llvm::sys::path::is_separator(Component[0], Separator);
  };

  // Below is a best-effort to handle ".." in paths. It is admittedly
  // not 100% correct in the presence of symlinks.
  for (auto &Component : llvm::reverse(Components)) {
    if ("." == Component) {
    } else if (".." == Component) {
      ++Cnt;
    } else if (Cnt) {
      --Cnt;
    } else if (RealPathComponentIter != RealPathComponentEnd) {
      if (!IsSep(Component) && !IsSep(*RealPathComponentIter) &&
          Component != *RealPathComponentIter) {
        // If these non-separator path components differ by more than just case,
        // then we may be looking at symlinked paths. Bail on this diagnostic to
        // avoid noisy false positives.
        SuggestReplacement =
            RealPathComponentIter->equals_insensitive(Component);
        if (!SuggestReplacement)
          break;
        Component = *RealPathComponentIter;
      }
      ++RealPathComponentIter;
    }
  }
  return SuggestReplacement;
}

bool Preprocessor::checkModuleIsAvailable(const LangOptions &LangOpts,
                                          const TargetInfo &TargetInfo,
                                          const Module &M,
                                          DiagnosticsEngine &Diags) {
  Module::Requirement Requirement;
  Module::UnresolvedHeaderDirective MissingHeader;
  Module *ShadowingModule = nullptr;
  if (M.isAvailable(LangOpts, TargetInfo, Requirement, MissingHeader,
                    ShadowingModule))
    return false;

  if (MissingHeader.FileNameLoc.isValid()) {
    Diags.Report(MissingHeader.FileNameLoc, diag::err_module_header_missing)
        << MissingHeader.IsUmbrella << MissingHeader.FileName;
  } else if (ShadowingModule) {
    Diags.Report(M.DefinitionLoc, diag::err_module_shadowed) << M.Name;
    Diags.Report(ShadowingModule->DefinitionLoc,
                 diag::note_previous_definition);
  } else {
    // FIXME: Track the location at which the requirement was specified, and
    // use it here.
    Diags.Report(M.DefinitionLoc, diag::err_module_unavailable)
        << M.getFullModuleName() << Requirement.RequiredState
        << Requirement.FeatureName;
  }
  return true;
}

std::pair<ConstSearchDirIterator, const FileEntry *>
Preprocessor::getIncludeNextStart(const Token &IncludeNextTok) const {
  // #include_next is like #include, except that we start searching after
  // the current found directory.  If we can't do this, issue a
  // diagnostic.
  ConstSearchDirIterator Lookup = CurDirLookup;
  const FileEntry *LookupFromFile = nullptr;

  if (isInPrimaryFile() && LangOpts.IsHeaderFile) {
    // If the main file is a header, then it's either for PCH/AST generation,
    // or libclang opened it. Either way, handle it as a normal include below
    // and do not complain about include_next.
  } else if (isInPrimaryFile()) {
    Lookup = nullptr;
    Diag(IncludeNextTok, diag::pp_include_next_in_primary);
  } else if (CurLexerSubmodule) {
    // Start looking up in the directory *after* the one in which the current
    // file would be found, if any.
    assert(CurPPLexer && "#include_next directive in macro?");
    if (auto FE = CurPPLexer->getFileEntry())
      LookupFromFile = *FE;
    Lookup = nullptr;
  } else if (!Lookup) {
    // The current file was not found by walking the include path. Either it
    // is the primary file (handled above), or it was found by absolute path,
    // or it was found relative to such a file.
    // FIXME: Track enough information so we know which case we're in.
    Diag(IncludeNextTok, diag::pp_include_next_absolute_path);
  } else {
    // Start looking up in the next directory.
    ++Lookup;
  }

  return {Lookup, LookupFromFile};
}

/// HandleIncludeDirective - The "\#include" tokens have just been read, read
/// the file to be included from the lexer, then include it!  This is a common
/// routine with functionality shared between \#include, \#include_next and
/// \#import.  LookupFrom is set when this is a \#include_next directive, it
/// specifies the file to start searching from.
void Preprocessor::HandleIncludeDirective(SourceLocation HashLoc,
                                          Token &IncludeTok,
                                          ConstSearchDirIterator LookupFrom,
                                          const FileEntry *LookupFromFile) {
  Token FilenameTok;
  if (LexHeaderName(FilenameTok))
    return;

  if (FilenameTok.isNot(tok::header_name)) {
    Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
    if (FilenameTok.isNot(tok::eod))
      DiscardUntilEndOfDirective();
    return;
  }

  // Verify that there is nothing after the filename, other than EOD.  Note
  // that we allow macros that expand to nothing after the filename, because
  // this falls into the category of "#include pp-tokens new-line" specified
  // in C99 6.10.2p4.
  SourceLocation EndLoc =
      CheckEndOfDirective(IncludeTok.getIdentifierInfo()->getNameStart(), true);

  auto Action = HandleHeaderIncludeOrImport(HashLoc, IncludeTok, FilenameTok,
                                            EndLoc, LookupFrom, LookupFromFile);
  switch (Action.Kind) {
  case ImportAction::None:
  case ImportAction::SkippedModuleImport:
    break;
  case ImportAction::ModuleBegin:
    EnterAnnotationToken(SourceRange(HashLoc, EndLoc),
                         tok::annot_module_begin, Action.ModuleForHeader);
    break;
  case ImportAction::HeaderUnitImport:
    EnterAnnotationToken(SourceRange(HashLoc, EndLoc), tok::annot_header_unit,
                         Action.ModuleForHeader);
    break;
  case ImportAction::ModuleImport:
    EnterAnnotationToken(SourceRange(HashLoc, EndLoc),
                         tok::annot_module_include, Action.ModuleForHeader);
    break;
  case ImportAction::Failure:
    assert(TheModuleLoader.HadFatalFailure &&
           "This should be an early exit only to a fatal error");
    TheModuleLoader.HadFatalFailure = true;
    IncludeTok.setKind(tok::eof);
    CurLexer->cutOffLexing();
    return;
  }
}

OptionalFileEntryRef Preprocessor::LookupHeaderIncludeOrImport(
    ConstSearchDirIterator *CurDir, StringRef &Filename,
    SourceLocation FilenameLoc, CharSourceRange FilenameRange,
    const Token &FilenameTok, bool &IsFrameworkFound, bool IsImportDecl,
    bool &IsMapped, ConstSearchDirIterator LookupFrom,
    const FileEntry *LookupFromFile, StringRef &LookupFilename,
    SmallVectorImpl<char> &RelativePath, SmallVectorImpl<char> &SearchPath,
    ModuleMap::KnownHeader &SuggestedModule, bool isAngled) {
  auto DiagnoseHeaderInclusion = [&](FileEntryRef FE) {
    if (LangOpts.AsmPreprocessor)
      return;

    Module *RequestingModule = getModuleForLocation(
        FilenameLoc, LangOpts.ModulesValidateTextualHeaderIncludes);
    bool RequestingModuleIsModuleInterface =
        !SourceMgr.isInMainFile(FilenameLoc);

    HeaderInfo.getModuleMap().diagnoseHeaderInclusion(
        RequestingModule, RequestingModuleIsModuleInterface, FilenameLoc,
        Filename, FE);
  };

  OptionalFileEntryRef File = LookupFile(
      FilenameLoc, LookupFilename, isAngled, LookupFrom, LookupFromFile, CurDir,
      Callbacks ? &SearchPath : nullptr, Callbacks ? &RelativePath : nullptr,
      &SuggestedModule, &IsMapped, &IsFrameworkFound);
  if (File) {
    DiagnoseHeaderInclusion(*File);
    return File;
  }

  // Give the clients a chance to silently skip this include.
  if (Callbacks && Callbacks->FileNotFound(Filename))
    return std::nullopt;

  if (SuppressIncludeNotFoundError)
    return std::nullopt;

  // If the file could not be located and it was included via angle
  // brackets, we can attempt a lookup as though it were a quoted path to
  // provide the user with a possible fixit.
  if (isAngled) {
    OptionalFileEntryRef File = LookupFile(
        FilenameLoc, LookupFilename, false, LookupFrom, LookupFromFile, CurDir,
        Callbacks ? &SearchPath : nullptr, Callbacks ? &RelativePath : nullptr,
        &SuggestedModule, &IsMapped,
        /*IsFrameworkFound=*/nullptr);
    if (File) {
      DiagnoseHeaderInclusion(*File);
      Diag(FilenameTok, diag::err_pp_file_not_found_angled_include_not_fatal)
          << Filename << IsImportDecl
          << FixItHint::CreateReplacement(FilenameRange,
                                          "\"" + Filename.str() + "\"");
      return File;
    }
  }

  // Check for likely typos due to leading or trailing non-isAlphanumeric
  // characters
  StringRef OriginalFilename = Filename;
  if (LangOpts.SpellChecking) {
    // A heuristic to correct a typo file name by removing leading and
    // trailing non-isAlphanumeric characters.
    auto CorrectTypoFilename = [](llvm::StringRef Filename) {
      Filename = Filename.drop_until(isAlphanumeric);
      while (!Filename.empty() && !isAlphanumeric(Filename.back())) {
        Filename = Filename.drop_back();
      }
      return Filename;
    };
    StringRef TypoCorrectionName = CorrectTypoFilename(Filename);
    StringRef TypoCorrectionLookupName = CorrectTypoFilename(LookupFilename);

    OptionalFileEntryRef File = LookupFile(
        FilenameLoc, TypoCorrectionLookupName, isAngled, LookupFrom,
        LookupFromFile, CurDir, Callbacks ? &SearchPath : nullptr,
        Callbacks ? &RelativePath : nullptr, &SuggestedModule, &IsMapped,
        /*IsFrameworkFound=*/nullptr);
    if (File) {
      DiagnoseHeaderInclusion(*File);
      auto Hint =
          isAngled ? FixItHint::CreateReplacement(
                         FilenameRange, "<" + TypoCorrectionName.str() + ">")
                   : FixItHint::CreateReplacement(
                         FilenameRange, "\"" + TypoCorrectionName.str() + "\"");
      Diag(FilenameTok, diag::err_pp_file_not_found_typo_not_fatal)
          << OriginalFilename << TypoCorrectionName << Hint;
      // We found the file, so set the Filename to the name after typo
      // correction.
      Filename = TypoCorrectionName;
      LookupFilename = TypoCorrectionLookupName;
      return File;
    }
  }

  // If the file is still not found, just go with the vanilla diagnostic
  assert(!File && "expected missing file");
  Diag(FilenameTok, diag::err_pp_file_not_found)
      << OriginalFilename << FilenameRange;
  if (IsFrameworkFound) {
    size_t SlashPos = OriginalFilename.find('/');
    assert(SlashPos != StringRef::npos &&
           "Include with framework name should have '/' in the filename");
    StringRef FrameworkName = OriginalFilename.substr(0, SlashPos);
    FrameworkCacheEntry &CacheEntry =
        HeaderInfo.LookupFrameworkCache(FrameworkName);
    assert(CacheEntry.Directory && "Found framework should be in cache");
    Diag(FilenameTok, diag::note_pp_framework_without_header)
        << OriginalFilename.substr(SlashPos + 1) << FrameworkName
        << CacheEntry.Directory->getName();
  }

  return std::nullopt;
}

/// Handle either a #include-like directive or an import declaration that names
/// a header file.
///
/// \param HashLoc The location of the '#' token for an include, or
///        SourceLocation() for an import declaration.
/// \param IncludeTok The include / include_next / import token.
/// \param FilenameTok The header-name token.
/// \param EndLoc The location at which any imported macros become visible.
/// \param LookupFrom For #include_next, the starting directory for the
///        directory lookup.
/// \param LookupFromFile For #include_next, the starting file for the directory
///        lookup.
Preprocessor::ImportAction Preprocessor::HandleHeaderIncludeOrImport(
    SourceLocation HashLoc, Token &IncludeTok, Token &FilenameTok,
    SourceLocation EndLoc, ConstSearchDirIterator LookupFrom,
    const FileEntry *LookupFromFile) {
  SmallString<128> FilenameBuffer;
  StringRef Filename = getSpelling(FilenameTok, FilenameBuffer);
  SourceLocation CharEnd = FilenameTok.getEndLoc();

  CharSourceRange FilenameRange
    = CharSourceRange::getCharRange(FilenameTok.getLocation(), CharEnd);
  StringRef OriginalFilename = Filename;
  bool isAngled =
    GetIncludeFilenameSpelling(FilenameTok.getLocation(), Filename);

  // If GetIncludeFilenameSpelling set the start ptr to null, there was an
  // error.
  if (Filename.empty())
    return {ImportAction::None};

  bool IsImportDecl = HashLoc.isInvalid();
  SourceLocation StartLoc = IsImportDecl ? IncludeTok.getLocation() : HashLoc;

  // Complain about attempts to #include files in an audit pragma.
  if (PragmaARCCFCodeAuditedInfo.second.isValid()) {
    Diag(StartLoc, diag::err_pp_include_in_arc_cf_code_audited) << IsImportDecl;
    Diag(PragmaARCCFCodeAuditedInfo.second, diag::note_pragma_entered_here);

    // Immediately leave the pragma.
    PragmaARCCFCodeAuditedInfo = {nullptr, SourceLocation()};
  }

  // Complain about attempts to #include files in an assume-nonnull pragma.
  if (PragmaAssumeNonNullLoc.isValid()) {
    Diag(StartLoc, diag::err_pp_include_in_assume_nonnull) << IsImportDecl;
    Diag(PragmaAssumeNonNullLoc, diag::note_pragma_entered_here);

    // Immediately leave the pragma.
    PragmaAssumeNonNullLoc = SourceLocation();
  }

  if (HeaderInfo.HasIncludeAliasMap()) {
    // Map the filename with the brackets still attached.  If the name doesn't
    // map to anything, fall back on the filename we've already gotten the
    // spelling for.
    StringRef NewName = HeaderInfo.MapHeaderToIncludeAlias(OriginalFilename);
    if (!NewName.empty())
      Filename = NewName;
  }

  // Search include directories.
  bool IsMapped = false;
  bool IsFrameworkFound = false;
  ConstSearchDirIterator CurDir = nullptr;
  SmallString<1024> SearchPath;
  SmallString<1024> RelativePath;
  // We get the raw path only if we have 'Callbacks' to which we later pass
  // the path.
  ModuleMap::KnownHeader SuggestedModule;
  SourceLocation FilenameLoc = FilenameTok.getLocation();
  StringRef LookupFilename = Filename;

  // Normalize slashes when compiling with -fms-extensions on non-Windows. This
  // is unnecessary on Windows since the filesystem there handles backslashes.
  SmallString<128> NormalizedPath;
  llvm::sys::path::Style BackslashStyle = llvm::sys::path::Style::native;
  if (is_style_posix(BackslashStyle) && LangOpts.MicrosoftExt) {
    NormalizedPath = Filename.str();
    llvm::sys::path::native(NormalizedPath);
    LookupFilename = NormalizedPath;
    BackslashStyle = llvm::sys::path::Style::windows;
  }

  OptionalFileEntryRef File = LookupHeaderIncludeOrImport(
      &CurDir, Filename, FilenameLoc, FilenameRange, FilenameTok,
      IsFrameworkFound, IsImportDecl, IsMapped, LookupFrom, LookupFromFile,
      LookupFilename, RelativePath, SearchPath, SuggestedModule, isAngled);

  if (usingPCHWithThroughHeader() && SkippingUntilPCHThroughHeader) {
    if (File && isPCHThroughHeader(&File->getFileEntry()))
      SkippingUntilPCHThroughHeader = false;
    return {ImportAction::None};
  }

  // Should we enter the source file? Set to Skip if either the source file is
  // known to have no effect beyond its effect on module visibility -- that is,
  // if it's got an include guard that is already defined, set to Import if it
  // is a modular header we've already built and should import.

  // For C++20 Modules
  // [cpp.include]/7 If the header identified by the header-name denotes an
  // importable header, it is implementation-defined whether the #include
  // preprocessing directive is instead replaced by an import directive.
  // For this implementation, the translation is permitted when we are parsing
  // the Global Module Fragment, and not otherwise (the cases where it would be
  // valid to replace an include with an import are highly constrained once in
  // named module purview; this choice avoids considerable complexity in
  // determining valid cases).

  enum { Enter, Import, Skip, IncludeLimitReached } Action = Enter;

  if (PPOpts->SingleFileParseMode)
    Action = IncludeLimitReached;

  // If we've reached the max allowed include depth, it is usually due to an
  // include cycle. Don't enter already processed files again as it can lead to
  // reaching the max allowed include depth again.
  if (Action == Enter && HasReachedMaxIncludeDepth && File &&
      alreadyIncluded(*File))
    Action = IncludeLimitReached;

  // FIXME: We do not have a good way to disambiguate C++ clang modules from
  // C++ standard modules (other than use/non-use of Header Units).

  Module *ModuleToImport = SuggestedModule.getModule();

  bool MaybeTranslateInclude = Action == Enter && File && ModuleToImport &&
                               !ModuleToImport->isForBuilding(getLangOpts());

  // Maybe a usable Header Unit
  bool UsableHeaderUnit = false;
  if (getLangOpts().CPlusPlusModules && ModuleToImport &&
      ModuleToImport->isHeaderUnit()) {
    if (TrackGMFState.inGMF() || IsImportDecl)
      UsableHeaderUnit = true;
    else if (!IsImportDecl) {
      // This is a Header Unit that we do not include-translate
      ModuleToImport = nullptr;
    }
  }
  // Maybe a usable clang header module.
  bool UsableClangHeaderModule =
      (getLangOpts().CPlusPlusModules || getLangOpts().Modules) &&
      ModuleToImport && !ModuleToImport->isHeaderUnit();

  // Determine whether we should try to import the module for this #include, if
  // there is one. Don't do so if precompiled module support is disabled or we
  // are processing this module textually (because we're building the module).
  if (MaybeTranslateInclude && (UsableHeaderUnit || UsableClangHeaderModule)) {
    // If this include corresponds to a module but that module is
    // unavailable, diagnose the situation and bail out.
    // FIXME: Remove this; loadModule does the same check (but produces
    // slightly worse diagnostics).
    if (checkModuleIsAvailable(getLangOpts(), getTargetInfo(), *ModuleToImport,
                               getDiagnostics())) {
      Diag(FilenameTok.getLocation(),
           diag::note_implicit_top_level_module_import_here)
          << ModuleToImport->getTopLevelModuleName();
      return {ImportAction::None};
    }

    // Compute the module access path corresponding to this module.
    // FIXME: Should we have a second loadModule() overload to avoid this
    // extra lookup step?
    SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Path;
    for (Module *Mod = ModuleToImport; Mod; Mod = Mod->Parent)
      Path.push_back(std::make_pair(getIdentifierInfo(Mod->Name),
                                    FilenameTok.getLocation()));
    std::reverse(Path.begin(), Path.end());

    // Warn that we're replacing the include/import with a module import.
    if (!IsImportDecl)
      diagnoseAutoModuleImport(*this, StartLoc, IncludeTok, Path, CharEnd);

    // Load the module to import its macros. We'll make the declarations
    // visible when the parser gets here.
    // FIXME: Pass ModuleToImport in here rather than converting it to a path
    // and making the module loader convert it back again.
    ModuleLoadResult Imported = TheModuleLoader.loadModule(
        IncludeTok.getLocation(), Path, Module::Hidden,
        /*IsInclusionDirective=*/true);
    assert((Imported == nullptr || Imported == ModuleToImport) &&
           "the imported module is different than the suggested one");

    if (Imported) {
      Action = Import;
    } else if (Imported.isMissingExpected()) {
      markClangModuleAsAffecting(
          static_cast<Module *>(Imported)->getTopLevelModule());
      // We failed to find a submodule that we assumed would exist (because it
      // was in the directory of an umbrella header, for instance), but no
      // actual module containing it exists (because the umbrella header is
      // incomplete).  Treat this as a textual inclusion.
      ModuleToImport = nullptr;
    } else if (Imported.isConfigMismatch()) {
      // On a configuration mismatch, enter the header textually. We still know
      // that it's part of the corresponding module.
    } else {
      // We hit an error processing the import. Bail out.
      if (hadModuleLoaderFatalFailure()) {
        // With a fatal failure in the module loader, we abort parsing.
        Token &Result = IncludeTok;
        assert(CurLexer && "#include but no current lexer set!");
        Result.startToken();
        CurLexer->FormTokenWithChars(Result, CurLexer->BufferEnd, tok::eof);
        CurLexer->cutOffLexing();
      }
      return {ImportAction::None};
    }
  }

  // The #included file will be considered to be a system header if either it is
  // in a system include directory, or if the #includer is a system include
  // header.
  SrcMgr::CharacteristicKind FileCharacter =
      SourceMgr.getFileCharacteristic(FilenameTok.getLocation());
  if (File)
    FileCharacter = std::max(HeaderInfo.getFileDirFlavor(*File), FileCharacter);

  // If this is a '#import' or an import-declaration, don't re-enter the file.
  //
  // FIXME: If we have a suggested module for a '#include', and we've already
  // visited this file, don't bother entering it again. We know it has no
  // further effect.
  bool EnterOnce =
      IsImportDecl ||
      IncludeTok.getIdentifierInfo()->getPPKeywordID() == tok::pp_import;

  bool IsFirstIncludeOfFile = false;

  // Ask HeaderInfo if we should enter this #include file.  If not, #including
  // this file will have no effect.
  if (Action == Enter && File &&
      !HeaderInfo.ShouldEnterIncludeFile(*this, *File, EnterOnce,
                                         getLangOpts().Modules, ModuleToImport,
                                         IsFirstIncludeOfFile)) {
    // C++ standard modules:
    // If we are not in the GMF, then we textually include only
    // clang modules:
    // Even if we've already preprocessed this header once and know that we
    // don't need to see its contents again, we still need to import it if it's
    // modular because we might not have imported it from this submodule before.
    //
    // FIXME: We don't do this when compiling a PCH because the AST
    // serialization layer can't cope with it. This means we get local
    // submodule visibility semantics wrong in that case.
    if (UsableHeaderUnit && !getLangOpts().CompilingPCH)
      Action = TrackGMFState.inGMF() ? Import : Skip;
    else
      Action = (ModuleToImport && !getLangOpts().CompilingPCH) ? Import : Skip;
  }

  // Check for circular inclusion of the main file.
  // We can't generate a consistent preamble with regard to the conditional
  // stack if the main file is included again as due to the preamble bounds
  // some directives (e.g. #endif of a header guard) will never be seen.
  // Since this will lead to confusing errors, avoid the inclusion.
  if (Action == Enter && File && PreambleConditionalStack.isRecording() &&
      SourceMgr.isMainFile(File->getFileEntry())) {
    Diag(FilenameTok.getLocation(),
         diag::err_pp_including_mainfile_in_preamble);
    return {ImportAction::None};
  }

  if (Callbacks && !IsImportDecl) {
    // Notify the callback object that we've seen an inclusion directive.
    // FIXME: Use a different callback for a pp-import?
    Callbacks->InclusionDirective(HashLoc, IncludeTok, LookupFilename, isAngled,
                                  FilenameRange, File, SearchPath, RelativePath,
                                  SuggestedModule.getModule(), Action == Import,
                                  FileCharacter);
    if (Action == Skip && File)
      Callbacks->FileSkipped(*File, FilenameTok, FileCharacter);
  }

  if (!File)
    return {ImportAction::None};

  // If this is a C++20 pp-import declaration, diagnose if we didn't find any
  // module corresponding to the named header.
  if (IsImportDecl && !ModuleToImport) {
    Diag(FilenameTok, diag::err_header_import_not_header_unit)
      << OriginalFilename << File->getName();
    return {ImportAction::None};
  }

  // Issue a diagnostic if the name of the file on disk has a different case
  // than the one we're about to open.
  const bool CheckIncludePathPortability =
      !IsMapped && !File->getFileEntry().tryGetRealPathName().empty();

  if (CheckIncludePathPortability) {
    StringRef Name = LookupFilename;
    StringRef NameWithoriginalSlashes = Filename;
#if defined(_WIN32)
    // Skip UNC prefix if present. (tryGetRealPathName() always
    // returns a path with the prefix skipped.)
    bool NameWasUNC = Name.consume_front("\\\\?\\");
    NameWithoriginalSlashes.consume_front("\\\\?\\");
#endif
    StringRef RealPathName = File->getFileEntry().tryGetRealPathName();
    SmallVector<StringRef, 16> Components(llvm::sys::path::begin(Name),
                                          llvm::sys::path::end(Name));
#if defined(_WIN32)
    // -Wnonportable-include-path is designed to diagnose includes using
    // case even on systems with a case-insensitive file system.
    // On Windows, RealPathName always starts with an upper-case drive
    // letter for absolute paths, but Name might start with either
    // case depending on if `cd c:\foo` or `cd C:\foo` was used in the shell.
    // ("foo" will always have on-disk case, no matter which case was
    // used in the cd command). To not emit this warning solely for
    // the drive letter, whose case is dependent on if `cd` is used
    // with upper- or lower-case drive letters, always consider the
    // given drive letter case as correct for the purpose of this warning.
    SmallString<128> FixedDriveRealPath;
    if (llvm::sys::path::is_absolute(Name) &&
        llvm::sys::path::is_absolute(RealPathName) &&
        toLowercase(Name[0]) == toLowercase(RealPathName[0]) &&
        isLowercase(Name[0]) != isLowercase(RealPathName[0])) {
      assert(Components.size() >= 3 && "should have drive, backslash, name");
      assert(Components[0].size() == 2 && "should start with drive");
      assert(Components[0][1] == ':' && "should have colon");
      FixedDriveRealPath = (Name.substr(0, 1) + RealPathName.substr(1)).str();
      RealPathName = FixedDriveRealPath;
    }
#endif

    if (trySimplifyPath(Components, RealPathName, BackslashStyle)) {
      SmallString<128> Path;
      Path.reserve(Name.size()+2);
      Path.push_back(isAngled ? '<' : '"');

      const auto IsSep = [BackslashStyle](char c) {
        return llvm::sys::path::is_separator(c, BackslashStyle);
      };

      for (auto Component : Components) {
        // On POSIX, Components will contain a single '/' as first element
        // exactly if Name is an absolute path.
        // On Windows, it will contain "C:" followed by '\' for absolute paths.
        // The drive letter is optional for absolute paths on Windows, but
        // clang currently cannot process absolute paths in #include lines that
        // don't have a drive.
        // If the first entry in Components is a directory separator,
        // then the code at the bottom of this loop that keeps the original
        // directory separator style copies it. If the second entry is
        // a directory separator (the C:\ case), then that separator already
        // got copied when the C: was processed and we want to skip that entry.
        if (!(Component.size() == 1 && IsSep(Component[0])))
          Path.append(Component);
        else if (Path.size() != 1)
          continue;

        // Append the separator(s) the user used, or the close quote
        if (Path.size() > NameWithoriginalSlashes.size()) {
          Path.push_back(isAngled ? '>' : '"');
          continue;
        }
        assert(IsSep(NameWithoriginalSlashes[Path.size()-1]));
        do
          Path.push_back(NameWithoriginalSlashes[Path.size()-1]);
        while (Path.size() <= NameWithoriginalSlashes.size() &&
               IsSep(NameWithoriginalSlashes[Path.size()-1]));
      }

#if defined(_WIN32)
      // Restore UNC prefix if it was there.
      if (NameWasUNC)
        Path = (Path.substr(0, 1) + "\\\\?\\" + Path.substr(1)).str();
#endif

      // For user files and known standard headers, issue a diagnostic.
      // For other system headers, don't. They can be controlled separately.
      auto DiagId =
          (FileCharacter == SrcMgr::C_User || warnByDefaultOnWrongCase(Name))
              ? diag::pp_nonportable_path
              : diag::pp_nonportable_system_path;
      Diag(FilenameTok, DiagId) << Path <<
        FixItHint::CreateReplacement(FilenameRange, Path);
    }
  }

  switch (Action) {
  case Skip:
    // If we don't need to enter the file, stop now.
    if (ModuleToImport)
      return {ImportAction::SkippedModuleImport, ModuleToImport};
    return {ImportAction::None};

  case IncludeLimitReached:
    // If we reached our include limit and don't want to enter any more files,
    // don't go any further.
    return {ImportAction::None};

  case Import: {
    // If this is a module import, make it visible if needed.
    assert(ModuleToImport && "no module to import");

    makeModuleVisible(ModuleToImport, EndLoc);

    if (IncludeTok.getIdentifierInfo()->getPPKeywordID() ==
        tok::pp___include_macros)
      return {ImportAction::None};

    return {ImportAction::ModuleImport, ModuleToImport};
  }

  case Enter:
    break;
  }

  // Check that we don't have infinite #include recursion.
  if (IncludeMacroStack.size() == MaxAllowedIncludeStackDepth-1) {
    Diag(FilenameTok, diag::err_pp_include_too_deep);
    HasReachedMaxIncludeDepth = true;
    return {ImportAction::None};
  }

  if (isAngled && isInNamedModule())
    Diag(FilenameTok, diag::warn_pp_include_angled_in_module_purview)
        << getNamedModuleName();

  // Look up the file, create a File ID for it.
  SourceLocation IncludePos = FilenameTok.getLocation();
  // If the filename string was the result of macro expansions, set the include
  // position on the file where it will be included and after the expansions.
  if (IncludePos.isMacroID())
    IncludePos = SourceMgr.getExpansionRange(IncludePos).getEnd();
  FileID FID = SourceMgr.createFileID(*File, IncludePos, FileCharacter);
  if (!FID.isValid()) {
    TheModuleLoader.HadFatalFailure = true;
    return ImportAction::Failure;
  }

  // If all is good, enter the new file!
  if (EnterSourceFile(FID, CurDir, FilenameTok.getLocation(),
                      IsFirstIncludeOfFile))
    return {ImportAction::None};

  // Determine if we're switching to building a new submodule, and which one.
  // This does not apply for C++20 modules header units.
  if (ModuleToImport && !ModuleToImport->isHeaderUnit()) {
    if (ModuleToImport->getTopLevelModule()->ShadowingModule) {
      // We are building a submodule that belongs to a shadowed module. This
      // means we find header files in the shadowed module.
      Diag(ModuleToImport->DefinitionLoc,
           diag::err_module_build_shadowed_submodule)
          << ModuleToImport->getFullModuleName();
      Diag(ModuleToImport->getTopLevelModule()->ShadowingModule->DefinitionLoc,
           diag::note_previous_definition);
      return {ImportAction::None};
    }
    // When building a pch, -fmodule-name tells the compiler to textually
    // include headers in the specified module. We are not building the
    // specified module.
    //
    // FIXME: This is the wrong way to handle this. We should produce a PCH
    // that behaves the same as the header would behave in a compilation using
    // that PCH, which means we should enter the submodule. We need to teach
    // the AST serialization layer to deal with the resulting AST.
    if (getLangOpts().CompilingPCH &&
        ModuleToImport->isForBuilding(getLangOpts()))
      return {ImportAction::None};

    assert(!CurLexerSubmodule && "should not have marked this as a module yet");
    CurLexerSubmodule = ModuleToImport;

    // Let the macro handling code know that any future macros are within
    // the new submodule.
    EnterSubmodule(ModuleToImport, EndLoc, /*ForPragma*/ false);

    // Let the parser know that any future declarations are within the new
    // submodule.
    // FIXME: There's no point doing this if we're handling a #__include_macros
    // directive.
    return {ImportAction::ModuleBegin, ModuleToImport};
  }

  assert(!IsImportDecl && "failed to diagnose missing module for import decl");
  return {ImportAction::None};
}

/// HandleIncludeNextDirective - Implements \#include_next.
///
void Preprocessor::HandleIncludeNextDirective(SourceLocation HashLoc,
                                              Token &IncludeNextTok) {
  Diag(IncludeNextTok, diag::ext_pp_include_next_directive);

  ConstSearchDirIterator Lookup = nullptr;
  const FileEntry *LookupFromFile;
  std::tie(Lookup, LookupFromFile) = getIncludeNextStart(IncludeNextTok);

  return HandleIncludeDirective(HashLoc, IncludeNextTok, Lookup,
                                LookupFromFile);
}

/// HandleMicrosoftImportDirective - Implements \#import for Microsoft Mode
void Preprocessor::HandleMicrosoftImportDirective(Token &Tok) {
  // The Microsoft #import directive takes a type library and generates header
  // files from it, and includes those.  This is beyond the scope of what clang
  // does, so we ignore it and error out.  However, #import can optionally have
  // trailing attributes that span multiple lines.  We're going to eat those
  // so we can continue processing from there.
  Diag(Tok, diag::err_pp_import_directive_ms );

  // Read tokens until we get to the end of the directive.  Note that the
  // directive can be split over multiple lines using the backslash character.
  DiscardUntilEndOfDirective();
}

/// HandleImportDirective - Implements \#import.
///
void Preprocessor::HandleImportDirective(SourceLocation HashLoc,
                                         Token &ImportTok) {
  if (!LangOpts.ObjC) {  // #import is standard for ObjC.
    if (LangOpts.MSVCCompat)
      return HandleMicrosoftImportDirective(ImportTok);
    Diag(ImportTok, diag::ext_pp_import_directive);
  }
  return HandleIncludeDirective(HashLoc, ImportTok);
}

/// HandleIncludeMacrosDirective - The -imacros command line option turns into a
/// pseudo directive in the predefines buffer.  This handles it by sucking all
/// tokens through the preprocessor and discarding them (only keeping the side
/// effects on the preprocessor).
void Preprocessor::HandleIncludeMacrosDirective(SourceLocation HashLoc,
                                                Token &IncludeMacrosTok) {
  // This directive should only occur in the predefines buffer.  If not, emit an
  // error and reject it.
  SourceLocation Loc = IncludeMacrosTok.getLocation();
  if (SourceMgr.getBufferName(Loc) != "<built-in>") {
    Diag(IncludeMacrosTok.getLocation(),
         diag::pp_include_macros_out_of_predefines);
    DiscardUntilEndOfDirective();
    return;
  }

  // Treat this as a normal #include for checking purposes.  If this is
  // successful, it will push a new lexer onto the include stack.
  HandleIncludeDirective(HashLoc, IncludeMacrosTok);

  Token TmpTok;
  do {
    Lex(TmpTok);
    assert(TmpTok.isNot(tok::eof) && "Didn't find end of -imacros!");
  } while (TmpTok.isNot(tok::hashhash));
}

//===----------------------------------------------------------------------===//
// Preprocessor Macro Directive Handling.
//===----------------------------------------------------------------------===//

/// ReadMacroParameterList - The ( starting a parameter list of a macro
/// definition has just been read.  Lex the rest of the parameters and the
/// closing ), updating MI with what we learn.  Return true if an error occurs
/// parsing the param list.
bool Preprocessor::ReadMacroParameterList(MacroInfo *MI, Token &Tok) {
  SmallVector<IdentifierInfo*, 32> Parameters;

  while (true) {
    LexUnexpandedNonComment(Tok);
    switch (Tok.getKind()) {
    case tok::r_paren:
      // Found the end of the parameter list.
      if (Parameters.empty())  // #define FOO()
        return false;
      // Otherwise we have #define FOO(A,)
      Diag(Tok, diag::err_pp_expected_ident_in_arg_list);
      return true;
    case tok::ellipsis:  // #define X(... -> C99 varargs
      if (!LangOpts.C99)
        Diag(Tok, LangOpts.CPlusPlus11 ?
             diag::warn_cxx98_compat_variadic_macro :
             diag::ext_variadic_macro);

      // OpenCL v1.2 s6.9.e: variadic macros are not supported.
      if (LangOpts.OpenCL && !LangOpts.OpenCLCPlusPlus) {
        Diag(Tok, diag::ext_pp_opencl_variadic_macros);
      }

      // Lex the token after the identifier.
      LexUnexpandedNonComment(Tok);
      if (Tok.isNot(tok::r_paren)) {
        Diag(Tok, diag::err_pp_missing_rparen_in_macro_def);
        return true;
      }
      // Add the __VA_ARGS__ identifier as a parameter.
      Parameters.push_back(Ident__VA_ARGS__);
      MI->setIsC99Varargs();
      MI->setParameterList(Parameters, BP);
      return false;
    case tok::eod:  // #define X(
      Diag(Tok, diag::err_pp_missing_rparen_in_macro_def);
      return true;
    default:
      // Handle keywords and identifiers here to accept things like
      // #define Foo(for) for.
      IdentifierInfo *II = Tok.getIdentifierInfo();
      if (!II) {
        // #define X(1
        Diag(Tok, diag::err_pp_invalid_tok_in_arg_list);
        return true;
      }

      // If this is already used as a parameter, it is used multiple times (e.g.
      // #define X(A,A.
      if (llvm::is_contained(Parameters, II)) { // C99 6.10.3p6
        Diag(Tok, diag::err_pp_duplicate_name_in_arg_list) << II;
        return true;
      }

      // Add the parameter to the macro info.
      Parameters.push_back(II);

      // Lex the token after the identifier.
      LexUnexpandedNonComment(Tok);

      switch (Tok.getKind()) {
      default:          // #define X(A B
        Diag(Tok, diag::err_pp_expected_comma_in_arg_list);
        return true;
      case tok::r_paren: // #define X(A)
        MI->setParameterList(Parameters, BP);
        return false;
      case tok::comma:  // #define X(A,
        break;
      case tok::ellipsis:  // #define X(A... -> GCC extension
        // Diagnose extension.
        Diag(Tok, diag::ext_named_variadic_macro);

        // Lex the token after the identifier.
        LexUnexpandedNonComment(Tok);
        if (Tok.isNot(tok::r_paren)) {
          Diag(Tok, diag::err_pp_missing_rparen_in_macro_def);
          return true;
        }

        MI->setIsGNUVarargs();
        MI->setParameterList(Parameters, BP);
        return false;
      }
    }
  }
}

static bool isConfigurationPattern(Token &MacroName, MacroInfo *MI,
                                   const LangOptions &LOptions) {
  if (MI->getNumTokens() == 1) {
    const Token &Value = MI->getReplacementToken(0);

    // Macro that is identity, like '#define inline inline' is a valid pattern.
    if (MacroName.getKind() == Value.getKind())
      return true;

    // Macro that maps a keyword to the same keyword decorated with leading/
    // trailing underscores is a valid pattern:
    //    #define inline __inline
    //    #define inline __inline__
    //    #define inline _inline (in MS compatibility mode)
    StringRef MacroText = MacroName.getIdentifierInfo()->getName();
    if (IdentifierInfo *II = Value.getIdentifierInfo()) {
      if (!II->isKeyword(LOptions))
        return false;
      StringRef ValueText = II->getName();
      StringRef TrimmedValue = ValueText;
      if (!ValueText.starts_with("__")) {
        if (ValueText.starts_with("_"))
          TrimmedValue = TrimmedValue.drop_front(1);
        else
          return false;
      } else {
        TrimmedValue = TrimmedValue.drop_front(2);
        if (TrimmedValue.ends_with("__"))
          TrimmedValue = TrimmedValue.drop_back(2);
      }
      return TrimmedValue == MacroText;
    } else {
      return false;
    }
  }

  // #define inline
  return MacroName.isOneOf(tok::kw_extern, tok::kw_inline, tok::kw_static,
                           tok::kw_const) &&
         MI->getNumTokens() == 0;
}

// ReadOptionalMacroParameterListAndBody - This consumes all (i.e. the
// entire line) of the macro's tokens and adds them to MacroInfo, and while
// doing so performs certain validity checks including (but not limited to):
//   - # (stringization) is followed by a macro parameter
//
//  Returns a nullptr if an invalid sequence of tokens is encountered or returns
//  a pointer to a MacroInfo object.

MacroInfo *Preprocessor::ReadOptionalMacroParameterListAndBody(
    const Token &MacroNameTok, const bool ImmediatelyAfterHeaderGuard) {

  Token LastTok = MacroNameTok;
  // Create the new macro.
  MacroInfo *const MI = AllocateMacroInfo(MacroNameTok.getLocation());

  Token Tok;
  LexUnexpandedToken(Tok);

  // Ensure we consume the rest of the macro body if errors occur.
  auto _ = llvm::make_scope_exit([&]() {
    // The flag indicates if we are still waiting for 'eod'.
    if (CurLexer->ParsingPreprocessorDirective)
      DiscardUntilEndOfDirective();
  });

  // Used to un-poison and then re-poison identifiers of the __VA_ARGS__ ilk
  // within their appropriate context.
  VariadicMacroScopeGuard VariadicMacroScopeGuard(*this);

  // If this is a function-like macro definition, parse the argument list,
  // marking each of the identifiers as being used as macro arguments.  Also,
  // check other constraints on the first token of the macro body.
  if (Tok.is(tok::eod)) {
    if (ImmediatelyAfterHeaderGuard) {
      // Save this macro information since it may part of a header guard.
      CurPPLexer->MIOpt.SetDefinedMacro(MacroNameTok.getIdentifierInfo(),
                                        MacroNameTok.getLocation());
    }
    // If there is no body to this macro, we have no special handling here.
  } else if (Tok.hasLeadingSpace()) {
    // This is a normal token with leading space.  Clear the leading space
    // marker on the first token to get proper expansion.
    Tok.clearFlag(Token::LeadingSpace);
  } else if (Tok.is(tok::l_paren)) {
    // This is a function-like macro definition.  Read the argument list.
    MI->setIsFunctionLike();
    if (ReadMacroParameterList(MI, LastTok))
      return nullptr;

    // If this is a definition of an ISO C/C++ variadic function-like macro (not
    // using the GNU named varargs extension) inform our variadic scope guard
    // which un-poisons and re-poisons certain identifiers (e.g. __VA_ARGS__)
    // allowed only within the definition of a variadic macro.

    if (MI->isC99Varargs()) {
      VariadicMacroScopeGuard.enterScope();
    }

    // Read the first token after the arg list for down below.
    LexUnexpandedToken(Tok);
  } else if (LangOpts.C99 || LangOpts.CPlusPlus11) {
    // C99 requires whitespace between the macro definition and the body.  Emit
    // a diagnostic for something like "#define X+".
    Diag(Tok, diag::ext_c99_whitespace_required_after_macro_name);
  } else {
    // C90 6.8 TC1 says: "In the definition of an object-like macro, if the
    // first character of a replacement list is not a character required by
    // subclause 5.2.1, then there shall be white-space separation between the
    // identifier and the replacement list.".  5.2.1 lists this set:
    //   "A-Za-z0-9!"#%&'()*+,_./:;<=>?[\]^_{|}~" as well as whitespace, which
    // is irrelevant here.
    bool isInvalid = false;
    if (Tok.is(tok::at)) // @ is not in the list above.
      isInvalid = true;
    else if (Tok.is(tok::unknown)) {
      // If we have an unknown token, it is something strange like "`".  Since
      // all of valid characters would have lexed into a single character
      // token of some sort, we know this is not a valid case.
      isInvalid = true;
    }
    if (isInvalid)
      Diag(Tok, diag::ext_missing_whitespace_after_macro_name);
    else
      Diag(Tok, diag::warn_missing_whitespace_after_macro_name);
  }

  if (!Tok.is(tok::eod))
    LastTok = Tok;

  SmallVector<Token, 16> Tokens;

  // Read the rest of the macro body.
  if (MI->isObjectLike()) {
    // Object-like macros are very simple, just read their body.
    while (Tok.isNot(tok::eod)) {
      LastTok = Tok;
      Tokens.push_back(Tok);
      // Get the next token of the macro.
      LexUnexpandedToken(Tok);
    }
  } else {
    // Otherwise, read the body of a function-like macro.  While we are at it,
    // check C99 6.10.3.2p1: ensure that # operators are followed by macro
    // parameters in function-like macro expansions.

    VAOptDefinitionContext VAOCtx(*this);

    while (Tok.isNot(tok::eod)) {
      LastTok = Tok;

      if (!Tok.isOneOf(tok::hash, tok::hashat, tok::hashhash)) {
        Tokens.push_back(Tok);

        if (VAOCtx.isVAOptToken(Tok)) {
          // If we're already within a VAOPT, emit an error.
          if (VAOCtx.isInVAOpt()) {
            Diag(Tok, diag::err_pp_vaopt_nested_use);
            return nullptr;
          }
          // Ensure VAOPT is followed by a '(' .
          LexUnexpandedToken(Tok);
          if (Tok.isNot(tok::l_paren)) {
            Diag(Tok, diag::err_pp_missing_lparen_in_vaopt_use);
            return nullptr;
          }
          Tokens.push_back(Tok);
          VAOCtx.sawVAOptFollowedByOpeningParens(Tok.getLocation());
          LexUnexpandedToken(Tok);
          if (Tok.is(tok::hashhash)) {
            Diag(Tok, diag::err_vaopt_paste_at_start);
            return nullptr;
          }
          continue;
        } else if (VAOCtx.isInVAOpt()) {
          if (Tok.is(tok::r_paren)) {
            if (VAOCtx.sawClosingParen()) {
              assert(Tokens.size() >= 3 &&
                     "Must have seen at least __VA_OPT__( "
                     "and a subsequent tok::r_paren");
              if (Tokens[Tokens.size() - 2].is(tok::hashhash)) {
                Diag(Tok, diag::err_vaopt_paste_at_end);
                return nullptr;
              }
            }
          } else if (Tok.is(tok::l_paren)) {
            VAOCtx.sawOpeningParen(Tok.getLocation());
          }
        }
        // Get the next token of the macro.
        LexUnexpandedToken(Tok);
        continue;
      }

      // If we're in -traditional mode, then we should ignore stringification
      // and token pasting. Mark the tokens as unknown so as not to confuse
      // things.
      if (getLangOpts().TraditionalCPP) {
        Tok.setKind(tok::unknown);
        Tokens.push_back(Tok);

        // Get the next token of the macro.
        LexUnexpandedToken(Tok);
        continue;
      }

      if (Tok.is(tok::hashhash)) {
        // If we see token pasting, check if it looks like the gcc comma
        // pasting extension.  We'll use this information to suppress
        // diagnostics later on.

        // Get the next token of the macro.
        LexUnexpandedToken(Tok);

        if (Tok.is(tok::eod)) {
          Tokens.push_back(LastTok);
          break;
        }

        if (!Tokens.empty() && Tok.getIdentifierInfo() == Ident__VA_ARGS__ &&
            Tokens[Tokens.size() - 1].is(tok::comma))
          MI->setHasCommaPasting();

        // Things look ok, add the '##' token to the macro.
        Tokens.push_back(LastTok);
        continue;
      }

      // Our Token is a stringization operator.
      // Get the next token of the macro.
      LexUnexpandedToken(Tok);

      // Check for a valid macro arg identifier or __VA_OPT__.
      if (!VAOCtx.isVAOptToken(Tok) &&
          (Tok.getIdentifierInfo() == nullptr ||
           MI->getParameterNum(Tok.getIdentifierInfo()) == -1)) {

        // If this is assembler-with-cpp mode, we accept random gibberish after
        // the '#' because '#' is often a comment character.  However, change
        // the kind of the token to tok::unknown so that the preprocessor isn't
        // confused.
        if (getLangOpts().AsmPreprocessor && Tok.isNot(tok::eod)) {
          LastTok.setKind(tok::unknown);
          Tokens.push_back(LastTok);
          continue;
        } else {
          Diag(Tok, diag::err_pp_stringize_not_parameter)
            << LastTok.is(tok::hashat);
          return nullptr;
        }
      }

      // Things look ok, add the '#' and param name tokens to the macro.
      Tokens.push_back(LastTok);

      // If the token following '#' is VAOPT, let the next iteration handle it
      // and check it for correctness, otherwise add the token and prime the
      // loop with the next one.
      if (!VAOCtx.isVAOptToken(Tok)) {
        Tokens.push_back(Tok);
        LastTok = Tok;

        // Get the next token of the macro.
        LexUnexpandedToken(Tok);
      }
    }
    if (VAOCtx.isInVAOpt()) {
      assert(Tok.is(tok::eod) && "Must be at End Of preprocessing Directive");
      Diag(Tok, diag::err_pp_expected_after)
        << LastTok.getKind() << tok::r_paren;
      Diag(VAOCtx.getUnmatchedOpeningParenLoc(), diag::note_matching) << tok::l_paren;
      return nullptr;
    }
  }
  MI->setDefinitionEndLoc(LastTok.getLocation());

  MI->setTokens(Tokens, BP);
  return MI;
}

static bool isObjCProtectedMacro(const IdentifierInfo *II) {
  return II->isStr("__strong") || II->isStr("__weak") ||
         II->isStr("__unsafe_unretained") || II->isStr("__autoreleasing");
}

/// HandleDefineDirective - Implements \#define.  This consumes the entire macro
/// line then lets the caller lex the next real token.
void Preprocessor::HandleDefineDirective(
    Token &DefineTok, const bool ImmediatelyAfterHeaderGuard) {
  ++NumDefined;

  Token MacroNameTok;
  bool MacroShadowsKeyword;
  ReadMacroName(MacroNameTok, MU_Define, &MacroShadowsKeyword);

  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.is(tok::eod))
    return;

  IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
  // Issue a final pragma warning if we're defining a macro that was has been
  // undefined and is being redefined.
  if (!II->hasMacroDefinition() && II->hadMacroDefinition() && II->isFinal())
    emitFinalMacroWarning(MacroNameTok, /*IsUndef=*/false);

  // If we are supposed to keep comments in #defines, reenable comment saving
  // mode.
  if (CurLexer) CurLexer->SetCommentRetentionState(KeepMacroComments);

  MacroInfo *const MI = ReadOptionalMacroParameterListAndBody(
      MacroNameTok, ImmediatelyAfterHeaderGuard);

  if (!MI) return;

  if (MacroShadowsKeyword &&
      !isConfigurationPattern(MacroNameTok, MI, getLangOpts())) {
    Diag(MacroNameTok, diag::warn_pp_macro_hides_keyword);
  }
  // Check that there is no paste (##) operator at the beginning or end of the
  // replacement list.
  unsigned NumTokens = MI->getNumTokens();
  if (NumTokens != 0) {
    if (MI->getReplacementToken(0).is(tok::hashhash)) {
      Diag(MI->getReplacementToken(0), diag::err_paste_at_start);
      return;
    }
    if (MI->getReplacementToken(NumTokens-1).is(tok::hashhash)) {
      Diag(MI->getReplacementToken(NumTokens-1), diag::err_paste_at_end);
      return;
    }
  }

  // When skipping just warn about macros that do not match.
  if (SkippingUntilPCHThroughHeader) {
    const MacroInfo *OtherMI = getMacroInfo(MacroNameTok.getIdentifierInfo());
    if (!OtherMI || !MI->isIdenticalTo(*OtherMI, *this,
                             /*Syntactic=*/LangOpts.MicrosoftExt))
      Diag(MI->getDefinitionLoc(), diag::warn_pp_macro_def_mismatch_with_pch)
          << MacroNameTok.getIdentifierInfo();
    // Issue the diagnostic but allow the change if msvc extensions are enabled
    if (!LangOpts.MicrosoftExt)
      return;
  }

  // Finally, if this identifier already had a macro defined for it, verify that
  // the macro bodies are identical, and issue diagnostics if they are not.
  if (const MacroInfo *OtherMI=getMacroInfo(MacroNameTok.getIdentifierInfo())) {
    // Final macros are hard-mode: they always warn. Even if the bodies are
    // identical. Even if they are in system headers. Even if they are things we
    // would silently allow in the past.
    if (MacroNameTok.getIdentifierInfo()->isFinal())
      emitFinalMacroWarning(MacroNameTok, /*IsUndef=*/false);

    // In Objective-C, ignore attempts to directly redefine the builtin
    // definitions of the ownership qualifiers.  It's still possible to
    // #undef them.
    if (getLangOpts().ObjC &&
        SourceMgr.getFileID(OtherMI->getDefinitionLoc()) ==
            getPredefinesFileID() &&
        isObjCProtectedMacro(MacroNameTok.getIdentifierInfo())) {
      // Warn if it changes the tokens.
      if ((!getDiagnostics().getSuppressSystemWarnings() ||
           !SourceMgr.isInSystemHeader(DefineTok.getLocation())) &&
          !MI->isIdenticalTo(*OtherMI, *this,
                             /*Syntactic=*/LangOpts.MicrosoftExt)) {
        Diag(MI->getDefinitionLoc(), diag::warn_pp_objc_macro_redef_ignored);
      }
      assert(!OtherMI->isWarnIfUnused());
      return;
    }

    // It is very common for system headers to have tons of macro redefinitions
    // and for warnings to be disabled in system headers.  If this is the case,
    // then don't bother calling MacroInfo::isIdenticalTo.
    if (!getDiagnostics().getSuppressSystemWarnings() ||
        !SourceMgr.isInSystemHeader(DefineTok.getLocation())) {

      if (!OtherMI->isUsed() && OtherMI->isWarnIfUnused())
        Diag(OtherMI->getDefinitionLoc(), diag::pp_macro_not_used);

      // Warn if defining "__LINE__" and other builtins, per C99 6.10.8/4 and
      // C++ [cpp.predefined]p4, but allow it as an extension.
      if (isLanguageDefinedBuiltin(SourceMgr, OtherMI, II->getName()))
        Diag(MacroNameTok, diag::ext_pp_redef_builtin_macro);
      // Macros must be identical.  This means all tokens and whitespace
      // separation must be the same.  C99 6.10.3p2.
      else if (!OtherMI->isAllowRedefinitionsWithoutWarning() &&
               !MI->isIdenticalTo(*OtherMI, *this, /*Syntactic=*/LangOpts.MicrosoftExt)) {
        Diag(MI->getDefinitionLoc(), diag::ext_pp_macro_redef)
          << MacroNameTok.getIdentifierInfo();
        Diag(OtherMI->getDefinitionLoc(), diag::note_previous_definition);
      }
    }
    if (OtherMI->isWarnIfUnused())
      WarnUnusedMacroLocs.erase(OtherMI->getDefinitionLoc());
  }

  DefMacroDirective *MD =
      appendDefMacroDirective(MacroNameTok.getIdentifierInfo(), MI);

  assert(!MI->isUsed());
  // If we need warning for not using the macro, add its location in the
  // warn-because-unused-macro set. If it gets used it will be removed from set.
  if (getSourceManager().isInMainFile(MI->getDefinitionLoc()) &&
      !Diags->isIgnored(diag::pp_macro_not_used, MI->getDefinitionLoc()) &&
      !MacroExpansionInDirectivesOverride &&
      getSourceManager().getFileID(MI->getDefinitionLoc()) !=
          getPredefinesFileID()) {
    MI->setIsWarnIfUnused(true);
    WarnUnusedMacroLocs.insert(MI->getDefinitionLoc());
  }

  // If the callbacks want to know, tell them about the macro definition.
  if (Callbacks)
    Callbacks->MacroDefined(MacroNameTok, MD);

  // If we're in MS compatibility mode and the macro being defined is the
  // assert macro, implicitly add a macro definition for static_assert to work
  // around their broken assert.h header file in C. Only do so if there isn't
  // already a static_assert macro defined.
  if (!getLangOpts().CPlusPlus && getLangOpts().MSVCCompat &&
      MacroNameTok.getIdentifierInfo()->isStr("assert") &&
      !isMacroDefined("static_assert")) {
    MacroInfo *MI = AllocateMacroInfo(SourceLocation());

    Token Tok;
    Tok.startToken();
    Tok.setKind(tok::kw__Static_assert);
    Tok.setIdentifierInfo(getIdentifierInfo("_Static_assert"));
    MI->setTokens({Tok}, BP);
    (void)appendDefMacroDirective(getIdentifierInfo("static_assert"), MI);
  }
}

/// HandleUndefDirective - Implements \#undef.
///
void Preprocessor::HandleUndefDirective() {
  ++NumUndefined;

  Token MacroNameTok;
  ReadMacroName(MacroNameTok, MU_Undef);

  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.is(tok::eod))
    return;

  // Check to see if this is the last token on the #undef line.
  CheckEndOfDirective("undef");

  // Okay, we have a valid identifier to undef.
  auto *II = MacroNameTok.getIdentifierInfo();
  auto MD = getMacroDefinition(II);
  UndefMacroDirective *Undef = nullptr;

  if (II->isFinal())
    emitFinalMacroWarning(MacroNameTok, /*IsUndef=*/true);

  // If the macro is not defined, this is a noop undef.
  if (const MacroInfo *MI = MD.getMacroInfo()) {
    if (!MI->isUsed() && MI->isWarnIfUnused())
      Diag(MI->getDefinitionLoc(), diag::pp_macro_not_used);

    // Warn if undefining "__LINE__" and other builtins, per C99 6.10.8/4 and
    // C++ [cpp.predefined]p4, but allow it as an extension.
    if (isLanguageDefinedBuiltin(SourceMgr, MI, II->getName()))
      Diag(MacroNameTok, diag::ext_pp_undef_builtin_macro);

    if (MI->isWarnIfUnused())
      WarnUnusedMacroLocs.erase(MI->getDefinitionLoc());

    Undef = AllocateUndefMacroDirective(MacroNameTok.getLocation());
  }

  // If the callbacks want to know, tell them about the macro #undef.
  // Note: no matter if the macro was defined or not.
  if (Callbacks)
    Callbacks->MacroUndefined(MacroNameTok, MD, Undef);

  if (Undef)
    appendMacroDirective(II, Undef);
}

//===----------------------------------------------------------------------===//
// Preprocessor Conditional Directive Handling.
//===----------------------------------------------------------------------===//

/// HandleIfdefDirective - Implements the \#ifdef/\#ifndef directive.  isIfndef
/// is true when this is a \#ifndef directive.  ReadAnyTokensBeforeDirective is
/// true if any tokens have been returned or pp-directives activated before this
/// \#ifndef has been lexed.
///
void Preprocessor::HandleIfdefDirective(Token &Result,
                                        const Token &HashToken,
                                        bool isIfndef,
                                        bool ReadAnyTokensBeforeDirective) {
  ++NumIf;
  Token DirectiveTok = Result;

  Token MacroNameTok;
  ReadMacroName(MacroNameTok);

  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.is(tok::eod)) {
    // Skip code until we get to #endif.  This helps with recovery by not
    // emitting an error when the #endif is reached.
    SkipExcludedConditionalBlock(HashToken.getLocation(),
                                 DirectiveTok.getLocation(),
                                 /*Foundnonskip*/ false, /*FoundElse*/ false);
    return;
  }

  emitMacroExpansionWarnings(MacroNameTok, /*IsIfnDef=*/true);

  // Check to see if this is the last token on the #if[n]def line.
  CheckEndOfDirective(isIfndef ? "ifndef" : "ifdef");

  IdentifierInfo *MII = MacroNameTok.getIdentifierInfo();
  auto MD = getMacroDefinition(MII);
  MacroInfo *MI = MD.getMacroInfo();

  if (CurPPLexer->getConditionalStackDepth() == 0) {
    // If the start of a top-level #ifdef and if the macro is not defined,
    // inform MIOpt that this might be the start of a proper include guard.
    // Otherwise it is some other form of unknown conditional which we can't
    // handle.
    if (!ReadAnyTokensBeforeDirective && !MI) {
      assert(isIfndef && "#ifdef shouldn't reach here");
      CurPPLexer->MIOpt.EnterTopLevelIfndef(MII, MacroNameTok.getLocation());
    } else
      CurPPLexer->MIOpt.EnterTopLevelConditional();
  }

  // If there is a macro, process it.
  if (MI)  // Mark it used.
    markMacroAsUsed(MI);

  if (Callbacks) {
    if (isIfndef)
      Callbacks->Ifndef(DirectiveTok.getLocation(), MacroNameTok, MD);
    else
      Callbacks->Ifdef(DirectiveTok.getLocation(), MacroNameTok, MD);
  }

  bool RetainExcludedCB = PPOpts->RetainExcludedConditionalBlocks &&
    getSourceManager().isInMainFile(DirectiveTok.getLocation());

  // Should we include the stuff contained by this directive?
  if (PPOpts->SingleFileParseMode && !MI) {
    // In 'single-file-parse mode' undefined identifiers trigger parsing of all
    // the directive blocks.
    CurPPLexer->pushConditionalLevel(DirectiveTok.getLocation(),
                                     /*wasskip*/false, /*foundnonskip*/false,
                                     /*foundelse*/false);
  } else if (!MI == isIfndef || RetainExcludedCB) {
    // Yes, remember that we are inside a conditional, then lex the next token.
    CurPPLexer->pushConditionalLevel(DirectiveTok.getLocation(),
                                     /*wasskip*/false, /*foundnonskip*/true,
                                     /*foundelse*/false);
  } else {
    // No, skip the contents of this block.
    SkipExcludedConditionalBlock(HashToken.getLocation(),
                                 DirectiveTok.getLocation(),
                                 /*Foundnonskip*/ false,
                                 /*FoundElse*/ false);
  }
}

/// HandleIfDirective - Implements the \#if directive.
///
void Preprocessor::HandleIfDirective(Token &IfToken,
                                     const Token &HashToken,
                                     bool ReadAnyTokensBeforeDirective) {
  ++NumIf;

  // Parse and evaluate the conditional expression.
  IdentifierInfo *IfNDefMacro = nullptr;
  const DirectiveEvalResult DER = EvaluateDirectiveExpression(IfNDefMacro);
  const bool ConditionalTrue = DER.Conditional;
  // Lexer might become invalid if we hit code completion point while evaluating
  // expression.
  if (!CurPPLexer)
    return;

  // If this condition is equivalent to #ifndef X, and if this is the first
  // directive seen, handle it for the multiple-include optimization.
  if (CurPPLexer->getConditionalStackDepth() == 0) {
    if (!ReadAnyTokensBeforeDirective && IfNDefMacro && ConditionalTrue)
      // FIXME: Pass in the location of the macro name, not the 'if' token.
      CurPPLexer->MIOpt.EnterTopLevelIfndef(IfNDefMacro, IfToken.getLocation());
    else
      CurPPLexer->MIOpt.EnterTopLevelConditional();
  }

  if (Callbacks)
    Callbacks->If(
        IfToken.getLocation(), DER.ExprRange,
        (ConditionalTrue ? PPCallbacks::CVK_True : PPCallbacks::CVK_False));

  bool RetainExcludedCB = PPOpts->RetainExcludedConditionalBlocks &&
    getSourceManager().isInMainFile(IfToken.getLocation());

  // Should we include the stuff contained by this directive?
  if (PPOpts->SingleFileParseMode && DER.IncludedUndefinedIds) {
    // In 'single-file-parse mode' undefined identifiers trigger parsing of all
    // the directive blocks.
    CurPPLexer->pushConditionalLevel(IfToken.getLocation(), /*wasskip*/false,
                                     /*foundnonskip*/false, /*foundelse*/false);
  } else if (ConditionalTrue || RetainExcludedCB) {
    // Yes, remember that we are inside a conditional, then lex the next token.
    CurPPLexer->pushConditionalLevel(IfToken.getLocation(), /*wasskip*/false,
                                   /*foundnonskip*/true, /*foundelse*/false);
  } else {
    // No, skip the contents of this block.
    SkipExcludedConditionalBlock(HashToken.getLocation(), IfToken.getLocation(),
                                 /*Foundnonskip*/ false,
                                 /*FoundElse*/ false);
  }
}

/// HandleEndifDirective - Implements the \#endif directive.
///
void Preprocessor::HandleEndifDirective(Token &EndifToken) {
  ++NumEndif;

  // Check that this is the whole directive.
  CheckEndOfDirective("endif");

  PPConditionalInfo CondInfo;
  if (CurPPLexer->popConditionalLevel(CondInfo)) {
    // No conditionals on the stack: this is an #endif without an #if.
    Diag(EndifToken, diag::err_pp_endif_without_if);
    return;
  }

  // If this the end of a top-level #endif, inform MIOpt.
  if (CurPPLexer->getConditionalStackDepth() == 0)
    CurPPLexer->MIOpt.ExitTopLevelConditional();

  assert(!CondInfo.WasSkipping && !CurPPLexer->LexingRawMode &&
         "This code should only be reachable in the non-skipping case!");

  if (Callbacks)
    Callbacks->Endif(EndifToken.getLocation(), CondInfo.IfLoc);
}

/// HandleElseDirective - Implements the \#else directive.
///
void Preprocessor::HandleElseDirective(Token &Result, const Token &HashToken) {
  ++NumElse;

  // #else directive in a non-skipping conditional... start skipping.
  CheckEndOfDirective("else");

  PPConditionalInfo CI;
  if (CurPPLexer->popConditionalLevel(CI)) {
    Diag(Result, diag::pp_err_else_without_if);
    return;
  }

  // If this is a top-level #else, inform the MIOpt.
  if (CurPPLexer->getConditionalStackDepth() == 0)
    CurPPLexer->MIOpt.EnterTopLevelConditional();

  // If this is a #else with a #else before it, report the error.
  if (CI.FoundElse) Diag(Result, diag::pp_err_else_after_else);

  if (Callbacks)
    Callbacks->Else(Result.getLocation(), CI.IfLoc);

  bool RetainExcludedCB = PPOpts->RetainExcludedConditionalBlocks &&
    getSourceManager().isInMainFile(Result.getLocation());

  if ((PPOpts->SingleFileParseMode && !CI.FoundNonSkip) || RetainExcludedCB) {
    // In 'single-file-parse mode' undefined identifiers trigger parsing of all
    // the directive blocks.
    CurPPLexer->pushConditionalLevel(CI.IfLoc, /*wasskip*/false,
                                     /*foundnonskip*/false, /*foundelse*/true);
    return;
  }

  // Finally, skip the rest of the contents of this block.
  SkipExcludedConditionalBlock(HashToken.getLocation(), CI.IfLoc,
                               /*Foundnonskip*/ true,
                               /*FoundElse*/ true, Result.getLocation());
}

/// Implements the \#elif, \#elifdef, and \#elifndef directives.
void Preprocessor::HandleElifFamilyDirective(Token &ElifToken,
                                             const Token &HashToken,
                                             tok::PPKeywordKind Kind) {
  PPElifDiag DirKind = Kind == tok::pp_elif      ? PED_Elif
                       : Kind == tok::pp_elifdef ? PED_Elifdef
                                                 : PED_Elifndef;
  ++NumElse;

  // Warn if using `#elifdef` & `#elifndef` in not C23 & C++23 mode.
  switch (DirKind) {
  case PED_Elifdef:
  case PED_Elifndef:
    unsigned DiagID;
    if (LangOpts.CPlusPlus)
      DiagID = LangOpts.CPlusPlus23 ? diag::warn_cxx23_compat_pp_directive
                                    : diag::ext_cxx23_pp_directive;
    else
      DiagID = LangOpts.C23 ? diag::warn_c23_compat_pp_directive
                            : diag::ext_c23_pp_directive;
    Diag(ElifToken, DiagID) << DirKind;
    break;
  default:
    break;
  }

  // #elif directive in a non-skipping conditional... start skipping.
  // We don't care what the condition is, because we will always skip it (since
  // the block immediately before it was included).
  SourceRange ConditionRange = DiscardUntilEndOfDirective();

  PPConditionalInfo CI;
  if (CurPPLexer->popConditionalLevel(CI)) {
    Diag(ElifToken, diag::pp_err_elif_without_if) << DirKind;
    return;
  }

  // If this is a top-level #elif, inform the MIOpt.
  if (CurPPLexer->getConditionalStackDepth() == 0)
    CurPPLexer->MIOpt.EnterTopLevelConditional();

  // If this is a #elif with a #else before it, report the error.
  if (CI.FoundElse)
    Diag(ElifToken, diag::pp_err_elif_after_else) << DirKind;

  if (Callbacks) {
    switch (Kind) {
    case tok::pp_elif:
      Callbacks->Elif(ElifToken.getLocation(), ConditionRange,
                      PPCallbacks::CVK_NotEvaluated, CI.IfLoc);
      break;
    case tok::pp_elifdef:
      Callbacks->Elifdef(ElifToken.getLocation(), ConditionRange, CI.IfLoc);
      break;
    case tok::pp_elifndef:
      Callbacks->Elifndef(ElifToken.getLocation(), ConditionRange, CI.IfLoc);
      break;
    default:
      assert(false && "unexpected directive kind");
      break;
    }
  }

  bool RetainExcludedCB = PPOpts->RetainExcludedConditionalBlocks &&
    getSourceManager().isInMainFile(ElifToken.getLocation());

  if ((PPOpts->SingleFileParseMode && !CI.FoundNonSkip) || RetainExcludedCB) {
    // In 'single-file-parse mode' undefined identifiers trigger parsing of all
    // the directive blocks.
    CurPPLexer->pushConditionalLevel(ElifToken.getLocation(), /*wasskip*/false,
                                     /*foundnonskip*/false, /*foundelse*/false);
    return;
  }

  // Finally, skip the rest of the contents of this block.
  SkipExcludedConditionalBlock(
      HashToken.getLocation(), CI.IfLoc, /*Foundnonskip*/ true,
      /*FoundElse*/ CI.FoundElse, ElifToken.getLocation());
}

std::optional<LexEmbedParametersResult>
Preprocessor::LexEmbedParameters(Token &CurTok, bool ForHasEmbed) {
  LexEmbedParametersResult Result{};
  SmallVector<Token, 2> ParameterTokens;
  tok::TokenKind EndTokenKind = ForHasEmbed ? tok::r_paren : tok::eod;

  auto DiagMismatchedBracesAndSkipToEOD =
      [&](tok::TokenKind Expected,
          std::pair<tok::TokenKind, SourceLocation> Matches) {
        Diag(CurTok, diag::err_expected) << Expected;
        Diag(Matches.second, diag::note_matching) << Matches.first;
        if (CurTok.isNot(tok::eod))
          DiscardUntilEndOfDirective(CurTok);
      };

  auto ExpectOrDiagAndSkipToEOD = [&](tok::TokenKind Kind) {
    if (CurTok.isNot(Kind)) {
      Diag(CurTok, diag::err_expected) << Kind;
      if (CurTok.isNot(tok::eod))
        DiscardUntilEndOfDirective(CurTok);
      return false;
    }
    return true;
  };

  // C23 6.10:
  // pp-parameter-name:
  //   pp-standard-parameter
  //   pp-prefixed-parameter
  //
  // pp-standard-parameter:
  //   identifier
  //
  // pp-prefixed-parameter:
  //   identifier :: identifier
  auto LexPPParameterName = [&]() -> std::optional<std::string> {
    // We expect the current token to be an identifier; if it's not, things
    // have gone wrong.
    if (!ExpectOrDiagAndSkipToEOD(tok::identifier))
      return std::nullopt;

    const IdentifierInfo *Prefix = CurTok.getIdentifierInfo();

    // Lex another token; it is either a :: or we're done with the parameter
    // name.
    LexNonComment(CurTok);
    if (CurTok.is(tok::coloncolon)) {
      // We found a ::, so lex another identifier token.
      LexNonComment(CurTok);
      if (!ExpectOrDiagAndSkipToEOD(tok::identifier))
        return std::nullopt;

      const IdentifierInfo *Suffix = CurTok.getIdentifierInfo();

      // Lex another token so we're past the name.
      LexNonComment(CurTok);
      return (llvm::Twine(Prefix->getName()) + "::" + Suffix->getName()).str();
    }
    return Prefix->getName().str();
  };

  // C23 6.10p5: In all aspects, a preprocessor standard parameter specified by
  // this document as an identifier pp_param and an identifier of the form
  // __pp_param__ shall behave the same when used as a preprocessor parameter,
  // except for the spelling.
  auto NormalizeParameterName = [](StringRef Name) {
    if (Name.size() > 4 && Name.starts_with("__") && Name.ends_with("__"))
      return Name.substr(2, Name.size() - 4);
    return Name;
  };

  auto LexParenthesizedIntegerExpr = [&]() -> std::optional<size_t> {
    // we have a limit parameter and its internals are processed using
    // evaluation rules from #if.
    if (!ExpectOrDiagAndSkipToEOD(tok::l_paren))
      return std::nullopt;

    // We do not consume the ( because EvaluateDirectiveExpression will lex
    // the next token for us.
    IdentifierInfo *ParameterIfNDef = nullptr;
    bool EvaluatedDefined;
    DirectiveEvalResult LimitEvalResult = EvaluateDirectiveExpression(
        ParameterIfNDef, CurTok, EvaluatedDefined, /*CheckForEOD=*/false);

    if (!LimitEvalResult.Value) {
      // If there was an error evaluating the directive expression, we expect
      // to be at the end of directive token.
      assert(CurTok.is(tok::eod) && "expect to be at the end of directive");
      return std::nullopt;
    }

    if (!ExpectOrDiagAndSkipToEOD(tok::r_paren))
      return std::nullopt;

    // Eat the ).
    LexNonComment(CurTok);

    // C23 6.10.3.2p2: The token defined shall not appear within the constant
    // expression.
    if (EvaluatedDefined) {
      Diag(CurTok, diag::err_defined_in_pp_embed);
      return std::nullopt;
    }

    if (LimitEvalResult.Value) {
      const llvm::APSInt &Result = *LimitEvalResult.Value;
      if (Result.isNegative()) {
        Diag(CurTok, diag::err_requires_positive_value)
            << toString(Result, 10) << /*positive*/ 0;
        return std::nullopt;
      }
      return Result.getLimitedValue();
    }
    return std::nullopt;
  };

  auto GetMatchingCloseBracket = [](tok::TokenKind Kind) {
    switch (Kind) {
    case tok::l_paren:
      return tok::r_paren;
    case tok::l_brace:
      return tok::r_brace;
    case tok::l_square:
      return tok::r_square;
    default:
      llvm_unreachable("should not get here");
    }
  };

  auto LexParenthesizedBalancedTokenSoup =
      [&](llvm::SmallVectorImpl<Token> &Tokens) {
        std::vector<std::pair<tok::TokenKind, SourceLocation>> BracketStack;

        // We expect the current token to be a left paren.
        if (!ExpectOrDiagAndSkipToEOD(tok::l_paren))
          return false;
        LexNonComment(CurTok); // Eat the (

        bool WaitingForInnerCloseParen = false;
        while (CurTok.isNot(tok::eod) &&
               (WaitingForInnerCloseParen || CurTok.isNot(tok::r_paren))) {
          switch (CurTok.getKind()) {
          default: // Shutting up diagnostics about not fully-covered switch.
            break;
          case tok::l_paren:
            WaitingForInnerCloseParen = true;
            [[fallthrough]];
          case tok::l_brace:
          case tok::l_square:
            BracketStack.push_back({CurTok.getKind(), CurTok.getLocation()});
            break;
          case tok::r_paren:
            WaitingForInnerCloseParen = false;
            [[fallthrough]];
          case tok::r_brace:
          case tok::r_square: {
            tok::TokenKind Matching =
                GetMatchingCloseBracket(BracketStack.back().first);
            if (BracketStack.empty() || CurTok.getKind() != Matching) {
              DiagMismatchedBracesAndSkipToEOD(Matching, BracketStack.back());
              return false;
            }
            BracketStack.pop_back();
          } break;
          }
          Tokens.push_back(CurTok);
          LexNonComment(CurTok);
        }

        // When we're done, we want to eat the closing paren.
        if (!ExpectOrDiagAndSkipToEOD(tok::r_paren))
          return false;

        LexNonComment(CurTok); // Eat the )
        return true;
      };

  LexNonComment(CurTok); // Prime the pump.
  while (!CurTok.isOneOf(EndTokenKind, tok::eod)) {
    SourceLocation ParamStartLoc = CurTok.getLocation();
    std::optional<std::string> ParamName = LexPPParameterName();
    if (!ParamName)
      return std::nullopt;
    StringRef Parameter = NormalizeParameterName(*ParamName);

    // Lex the parameters (dependent on the parameter type we want!).
    //
    // C23 6.10.3.Xp1: The X standard embed parameter may appear zero times or
    // one time in the embed parameter sequence.
    if (Parameter == "limit") {
      if (Result.MaybeLimitParam)
        Diag(CurTok, diag::err_pp_embed_dup_params) << Parameter;

      std::optional<size_t> Limit = LexParenthesizedIntegerExpr();
      if (!Limit)
        return std::nullopt;
      Result.MaybeLimitParam =
          PPEmbedParameterLimit{*Limit, {ParamStartLoc, CurTok.getLocation()}};
    } else if (Parameter == "clang::offset") {
      if (Result.MaybeOffsetParam)
        Diag(CurTok, diag::err_pp_embed_dup_params) << Parameter;

      std::optional<size_t> Offset = LexParenthesizedIntegerExpr();
      if (!Offset)
        return std::nullopt;
      Result.MaybeOffsetParam = PPEmbedParameterOffset{
          *Offset, {ParamStartLoc, CurTok.getLocation()}};
    } else if (Parameter == "prefix") {
      if (Result.MaybePrefixParam)
        Diag(CurTok, diag::err_pp_embed_dup_params) << Parameter;

      SmallVector<Token, 4> Soup;
      if (!LexParenthesizedBalancedTokenSoup(Soup))
        return std::nullopt;
      Result.MaybePrefixParam = PPEmbedParameterPrefix{
          std::move(Soup), {ParamStartLoc, CurTok.getLocation()}};
    } else if (Parameter == "suffix") {
      if (Result.MaybeSuffixParam)
        Diag(CurTok, diag::err_pp_embed_dup_params) << Parameter;

      SmallVector<Token, 4> Soup;
      if (!LexParenthesizedBalancedTokenSoup(Soup))
        return std::nullopt;
      Result.MaybeSuffixParam = PPEmbedParameterSuffix{
          std::move(Soup), {ParamStartLoc, CurTok.getLocation()}};
    } else if (Parameter == "if_empty") {
      if (Result.MaybeIfEmptyParam)
        Diag(CurTok, diag::err_pp_embed_dup_params) << Parameter;

      SmallVector<Token, 4> Soup;
      if (!LexParenthesizedBalancedTokenSoup(Soup))
        return std::nullopt;
      Result.MaybeIfEmptyParam = PPEmbedParameterIfEmpty{
          std::move(Soup), {ParamStartLoc, CurTok.getLocation()}};
    } else {
      ++Result.UnrecognizedParams;

      // If there's a left paren, we need to parse a balanced token sequence
      // and just eat those tokens.
      if (CurTok.is(tok::l_paren)) {
        SmallVector<Token, 4> Soup;
        if (!LexParenthesizedBalancedTokenSoup(Soup))
          return std::nullopt;
      }
      if (!ForHasEmbed) {
        Diag(CurTok, diag::err_pp_unknown_parameter) << 1 << Parameter;
        return std::nullopt;
      }
    }
  }
  return Result;
}

void Preprocessor::HandleEmbedDirectiveImpl(
    SourceLocation HashLoc, const LexEmbedParametersResult &Params,
    StringRef BinaryContents) {
  if (BinaryContents.empty()) {
    // If we have no binary contents, the only thing we need to emit are the
    // if_empty tokens, if any.
    // FIXME: this loses AST fidelity; nothing in the compiler will see that
    // these tokens came from #embed. We have to hack around this when printing
    // preprocessed output. The same is true for prefix and suffix tokens.
    if (Params.MaybeIfEmptyParam) {
      ArrayRef<Token> Toks = Params.MaybeIfEmptyParam->Tokens;
      size_t TokCount = Toks.size();
      auto NewToks = std::make_unique<Token[]>(TokCount);
      llvm::copy(Toks, NewToks.get());
      EnterTokenStream(std::move(NewToks), TokCount, true, true);
    }
    return;
  }

  size_t NumPrefixToks = Params.PrefixTokenCount(),
         NumSuffixToks = Params.SuffixTokenCount();
  size_t TotalNumToks = 1 + NumPrefixToks + NumSuffixToks;
  size_t CurIdx = 0;
  auto Toks = std::make_unique<Token[]>(TotalNumToks);

  // Add the prefix tokens, if any.
  if (Params.MaybePrefixParam) {
    llvm::copy(Params.MaybePrefixParam->Tokens, &Toks[CurIdx]);
    CurIdx += NumPrefixToks;
  }

  EmbedAnnotationData *Data = new (BP) EmbedAnnotationData;
  Data->BinaryData = BinaryContents;

  Toks[CurIdx].startToken();
  Toks[CurIdx].setKind(tok::annot_embed);
  Toks[CurIdx].setAnnotationRange(HashLoc);
  Toks[CurIdx++].setAnnotationValue(Data);

  // Now add the suffix tokens, if any.
  if (Params.MaybeSuffixParam) {
    llvm::copy(Params.MaybeSuffixParam->Tokens, &Toks[CurIdx]);
    CurIdx += NumSuffixToks;
  }

  assert(CurIdx == TotalNumToks && "Calculated the incorrect number of tokens");
  EnterTokenStream(std::move(Toks), TotalNumToks, true, true);
}

void Preprocessor::HandleEmbedDirective(SourceLocation HashLoc, Token &EmbedTok,
                                        const FileEntry *LookupFromFile) {
  // Give the usual extension/compatibility warnings.
  if (LangOpts.C23)
    Diag(EmbedTok, diag::warn_compat_pp_embed_directive);
  else
    Diag(EmbedTok, diag::ext_pp_embed_directive)
        << (LangOpts.CPlusPlus ? /*Clang*/ 1 : /*C23*/ 0);

  // Parse the filename header
  Token FilenameTok;
  if (LexHeaderName(FilenameTok))
    return;

  if (FilenameTok.isNot(tok::header_name)) {
    Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
    if (FilenameTok.isNot(tok::eod))
      DiscardUntilEndOfDirective();
    return;
  }

  // Parse the optional sequence of
  // directive-parameters:
  //     identifier parameter-name-list[opt] directive-argument-list[opt]
  // directive-argument-list:
  //    '(' balanced-token-sequence ')'
  // parameter-name-list:
  //    '::' identifier parameter-name-list[opt]
  Token CurTok;
  std::optional<LexEmbedParametersResult> Params =
      LexEmbedParameters(CurTok, /*ForHasEmbed=*/false);

  assert((Params || CurTok.is(tok::eod)) &&
         "expected success or to be at the end of the directive");
  if (!Params)
    return;

  // Now, splat the data out!
  SmallString<128> FilenameBuffer;
  StringRef Filename = getSpelling(FilenameTok, FilenameBuffer);
  StringRef OriginalFilename = Filename;
  bool isAngled =
      GetIncludeFilenameSpelling(FilenameTok.getLocation(), Filename);
  // If GetIncludeFilenameSpelling set the start ptr to null, there was an
  // error.
  assert(!Filename.empty());
  OptionalFileEntryRef MaybeFileRef =
      this->LookupEmbedFile(Filename, isAngled, true, LookupFromFile);
  if (!MaybeFileRef) {
    // could not find file
    if (Callbacks && Callbacks->EmbedFileNotFound(OriginalFilename)) {
      return;
    }
    Diag(FilenameTok, diag::err_pp_file_not_found) << Filename;
    return;
  }
  std::optional<llvm::MemoryBufferRef> MaybeFile =
      getSourceManager().getMemoryBufferForFileOrNone(*MaybeFileRef);
  if (!MaybeFile) {
    // could not find file
    Diag(FilenameTok, diag::err_cannot_open_file)
        << Filename << "a buffer to the contents could not be created";
    return;
  }
  StringRef BinaryContents = MaybeFile->getBuffer();

  // The order is important between 'offset' and 'limit'; we want to offset
  // first and then limit second; otherwise we may reduce the notional resource
  // size to something too small to offset into.
  if (Params->MaybeOffsetParam) {
    // FIXME: just like with the limit() and if_empty() parameters, this loses
    // source fidelity in the AST; it has no idea that there was an offset
    // involved.
    // offsets all the way to the end of the file make for an empty file.
    BinaryContents = BinaryContents.substr(Params->MaybeOffsetParam->Offset);
  }

  if (Params->MaybeLimitParam) {
    // FIXME: just like with the clang::offset() and if_empty() parameters,
    // this loses source fidelity in the AST; it has no idea there was a limit
    // involved.
    BinaryContents = BinaryContents.substr(0, Params->MaybeLimitParam->Limit);
  }

  if (Callbacks)
    Callbacks->EmbedDirective(HashLoc, Filename, isAngled, MaybeFileRef,
                              *Params);
  HandleEmbedDirectiveImpl(HashLoc, *Params, BinaryContents);
}
