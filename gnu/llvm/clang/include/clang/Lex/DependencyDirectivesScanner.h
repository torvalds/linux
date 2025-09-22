//===- clang/Lex/DependencyDirectivesScanner.h ---------------------*- C++ -*-//
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

#ifndef LLVM_CLANG_LEX_DEPENDENCYDIRECTIVESSCANNER_H
#define LLVM_CLANG_LEX_DEPENDENCYDIRECTIVESSCANNER_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"

namespace clang {

namespace tok {
enum TokenKind : unsigned short;
}

class DiagnosticsEngine;

namespace dependency_directives_scan {

/// Token lexed as part of dependency directive scanning.
struct Token {
  /// Offset into the original source input.
  unsigned Offset;
  unsigned Length;
  tok::TokenKind Kind;
  unsigned short Flags;

  Token(unsigned Offset, unsigned Length, tok::TokenKind Kind,
        unsigned short Flags)
      : Offset(Offset), Length(Length), Kind(Kind), Flags(Flags) {}

  unsigned getEnd() const { return Offset + Length; }

  bool is(tok::TokenKind K) const { return Kind == K; }
  bool isNot(tok::TokenKind K) const { return Kind != K; }
  bool isOneOf(tok::TokenKind K1, tok::TokenKind K2) const {
    return is(K1) || is(K2);
  }
  template <typename... Ts> bool isOneOf(tok::TokenKind K1, Ts... Ks) const {
    return is(K1) || isOneOf(Ks...);
  }
};

/// Represents the kind of preprocessor directive or a module declaration that
/// is tracked by the scanner in its token output.
enum DirectiveKind : uint8_t {
  pp_none,
  pp_include,
  pp___include_macros,
  pp_define,
  pp_undef,
  pp_import,
  pp_pragma_import,
  pp_pragma_once,
  pp_pragma_push_macro,
  pp_pragma_pop_macro,
  pp_pragma_include_alias,
  pp_pragma_system_header,
  pp_include_next,
  pp_if,
  pp_ifdef,
  pp_ifndef,
  pp_elif,
  pp_elifdef,
  pp_elifndef,
  pp_else,
  pp_endif,
  decl_at_import,
  cxx_module_decl,
  cxx_import_decl,
  cxx_export_module_decl,
  cxx_export_import_decl,
  /// Indicates that there are tokens present between the last scanned directive
  /// and eof. The \p Directive::Tokens array will be empty for this kind.
  tokens_present_before_eof,
  pp_eof,
};

/// Represents a directive that's lexed as part of the dependency directives
/// scanning. It's used to track various preprocessor directives that could
/// potentially have an effect on the dependencies.
struct Directive {
  ArrayRef<Token> Tokens;

  /// The kind of token.
  DirectiveKind Kind = pp_none;

  Directive() = default;
  Directive(DirectiveKind K, ArrayRef<Token> Tokens)
      : Tokens(Tokens), Kind(K) {}
};

} // end namespace dependency_directives_scan

/// Scan the input for the preprocessor directives that might have
/// an effect on the dependencies for a compilation unit.
///
/// This function ignores all non-preprocessor code and anything that
/// can't affect what gets included.
///
/// \returns false on success, true on error. If the diagnostic engine is not
/// null, an appropriate error is reported using the given input location
/// with the offset that corresponds to the \p Input buffer offset.
bool scanSourceForDependencyDirectives(
    StringRef Input, SmallVectorImpl<dependency_directives_scan::Token> &Tokens,
    SmallVectorImpl<dependency_directives_scan::Directive> &Directives,
    DiagnosticsEngine *Diags = nullptr,
    SourceLocation InputSourceLoc = SourceLocation());

/// Print the previously scanned dependency directives as minimized source text.
///
/// \param Source The original source text that the dependency directives were
/// scanned from.
/// \param Directives The previously scanned dependency
/// directives.
/// \param OS the stream to print the dependency directives on.
///
/// This is used primarily for testing purposes, during dependency scanning the
/// \p Lexer uses the tokens directly, not their printed version.
void printDependencyDirectivesAsSource(
    StringRef Source,
    ArrayRef<dependency_directives_scan::Directive> Directives,
    llvm::raw_ostream &OS);

} // end namespace clang

#endif // LLVM_CLANG_LEX_DEPENDENCYDIRECTIVESSCANNER_H
