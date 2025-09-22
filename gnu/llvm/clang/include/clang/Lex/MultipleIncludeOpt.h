//===--- MultipleIncludeOpt.h - Header Multiple-Include Optzn ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the MultipleIncludeOpt interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_MULTIPLEINCLUDEOPT_H
#define LLVM_CLANG_LEX_MULTIPLEINCLUDEOPT_H

#include "clang/Basic/SourceLocation.h"

namespace clang {
class IdentifierInfo;

/// Implements the simple state machine that the Lexer class uses to
/// detect files subject to the 'multiple-include' optimization.
///
/// The public methods in this class are triggered by various
/// events that occur when a file is lexed, and after the entire file is lexed,
/// information about which macro (if any) controls the header is returned.
class MultipleIncludeOpt {
  /// ReadAnyTokens - This is set to false when a file is first opened and true
  /// any time a token is returned to the client or a (non-multiple-include)
  /// directive is parsed.  When the final \#endif is parsed this is reset back
  /// to false, that way any tokens before the first \#ifdef or after the last
  /// \#endif can be easily detected.
  bool ReadAnyTokens;

  /// ImmediatelyAfterTopLevelIfndef - This is true when the only tokens
  /// processed in the file so far is an #ifndef and an identifier.  Used in
  /// the detection of header guards in a file.
  bool ImmediatelyAfterTopLevelIfndef;

  /// ReadAnyTokens - This is set to false when a file is first opened and true
  /// any time a token is returned to the client or a (non-multiple-include)
  /// directive is parsed.  When the final #endif is parsed this is reset back
  /// to false, that way any tokens before the first #ifdef or after the last
  /// #endif can be easily detected.
  bool DidMacroExpansion;

  /// TheMacro - The controlling macro for a file, if valid.
  ///
  const IdentifierInfo *TheMacro;

  /// DefinedMacro - The macro defined right after TheMacro, if any.
  const IdentifierInfo *DefinedMacro;

  SourceLocation MacroLoc;
  SourceLocation DefinedLoc;
public:
  MultipleIncludeOpt() {
    ReadAnyTokens = false;
    ImmediatelyAfterTopLevelIfndef = false;
    DidMacroExpansion = false;
    TheMacro = nullptr;
    DefinedMacro = nullptr;
  }

  SourceLocation GetMacroLocation() const {
    return MacroLoc;
  }

  SourceLocation GetDefinedLocation() const {
    return DefinedLoc;
  }

  void resetImmediatelyAfterTopLevelIfndef() {
    ImmediatelyAfterTopLevelIfndef = false;
  }

  void SetDefinedMacro(IdentifierInfo *M, SourceLocation Loc) {
    DefinedMacro = M;
    DefinedLoc = Loc;
  }

  /// Invalidate - Permanently mark this file as not being suitable for the
  /// include-file optimization.
  void Invalidate() {
    // If we have read tokens but have no controlling macro, the state-machine
    // below can never "accept".
    ReadAnyTokens = true;
    ImmediatelyAfterTopLevelIfndef = false;
    DefinedMacro = nullptr;
    TheMacro = nullptr;
  }

  /// getHasReadAnyTokensVal - This is used for the \#ifndef handshake at the
  /// top of the file when reading preprocessor directives.  Otherwise, reading
  /// the "ifndef x" would count as reading tokens.
  bool getHasReadAnyTokensVal() const { return ReadAnyTokens; }

  /// getImmediatelyAfterTopLevelIfndef - returns true if the last directive
  /// was an #ifndef at the beginning of the file.
  bool getImmediatelyAfterTopLevelIfndef() const {
    return ImmediatelyAfterTopLevelIfndef;
  }

  // If a token is read, remember that we have seen a side-effect in this file.
  void ReadToken() {
    ReadAnyTokens = true;
    ImmediatelyAfterTopLevelIfndef = false;
  }

  /// SetReadToken - Set whether the value of 'ReadAnyTokens'.  Called to
  /// override when encountering tokens outside of the include guard that have
  /// no effect if the file in question is is included multiple times (e.g. the
  /// null directive).
  void SetReadToken(bool Value) { ReadAnyTokens = Value; }

  /// ExpandedMacro - When a macro is expanded with this lexer as the current
  /// buffer, this method is called to disable the MIOpt if needed.
  void ExpandedMacro() { DidMacroExpansion = true; }

  /// Called when entering a top-level \#ifndef directive (or the
  /// "\#if !defined" equivalent) without any preceding tokens.
  ///
  /// Note, we don't care about the input value of 'ReadAnyTokens'.  The caller
  /// ensures that this is only called if there are no tokens read before the
  /// \#ifndef.  The caller is required to do this, because reading the \#if
  /// line obviously reads in tokens.
  void EnterTopLevelIfndef(const IdentifierInfo *M, SourceLocation Loc) {
    // If the macro is already set, this is after the top-level #endif.
    if (TheMacro)
      return Invalidate();

    // If we have already expanded a macro by the end of the #ifndef line, then
    // there is a macro expansion *in* the #ifndef line.  This means that the
    // condition could evaluate differently when subsequently #included.  Reject
    // this.
    if (DidMacroExpansion)
      return Invalidate();

    // Remember that we're in the #if and that we have the macro.
    ReadAnyTokens = true;
    ImmediatelyAfterTopLevelIfndef = true;
    TheMacro = M;
    MacroLoc = Loc;
  }

  /// Invoked when a top level conditional (except \#ifndef) is found.
  void EnterTopLevelConditional() {
    // If a conditional directive (except #ifndef) is found at the top level,
    // there is a chunk of the file not guarded by the controlling macro.
    Invalidate();
  }

  /// Called when the lexer exits the top-level conditional.
  void ExitTopLevelConditional() {
    // If we have a macro, that means the top of the file was ok.  Set our state
    // back to "not having read any tokens" so we can detect anything after the
    // #endif.
    if (!TheMacro) return Invalidate();

    // At this point, we haven't "read any tokens" but we do have a controlling
    // macro.
    ReadAnyTokens = false;
    ImmediatelyAfterTopLevelIfndef = false;
  }

  /// Once the entire file has been lexed, if there is a controlling
  /// macro, return it.
  const IdentifierInfo *GetControllingMacroAtEndOfFile() const {
    // If we haven't read any tokens after the #endif, return the controlling
    // macro if it's valid (if it isn't, it will be null).
    if (!ReadAnyTokens)
      return TheMacro;
    return nullptr;
  }

  /// If the ControllingMacro is followed by a macro definition, return
  /// the macro that was defined.
  const IdentifierInfo *GetDefinedMacro() const {
    return DefinedMacro;
  }
};

}  // end namespace clang

#endif
