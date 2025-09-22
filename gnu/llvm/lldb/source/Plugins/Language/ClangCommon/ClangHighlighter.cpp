//===-- ClangHighlighter.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangHighlighter.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/StreamString.h"

#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MemoryBuffer.h"
#include <optional>

using namespace lldb_private;

bool ClangHighlighter::isKeyword(llvm::StringRef token) const {
  return keywords.contains(token);
}

ClangHighlighter::ClangHighlighter() {
#define KEYWORD(X, N) keywords.insert(#X);
#include "clang/Basic/TokenKinds.def"
}

/// Determines which style should be applied to the given token.
/// \param highlighter
///     The current highlighter that should use the style.
/// \param token
///     The current token.
/// \param tok_str
///     The string in the source code the token represents.
/// \param options
///     The style we use for coloring the source code.
/// \param in_pp_directive
///     If we are currently in a preprocessor directive. NOTE: This is
///     passed by reference and will be updated if the current token starts
///     or ends a preprocessor directive.
/// \return
///     The ColorStyle that should be applied to the token.
static HighlightStyle::ColorStyle
determineClangStyle(const ClangHighlighter &highlighter,
                    const clang::Token &token, llvm::StringRef tok_str,
                    const HighlightStyle &options, bool &in_pp_directive) {
  using namespace clang;

  if (token.is(tok::comment)) {
    // If we were in a preprocessor directive before, we now left it.
    in_pp_directive = false;
    return options.comment;
  } else if (in_pp_directive || token.getKind() == tok::hash) {
    // Let's assume that the rest of the line is a PP directive.
    in_pp_directive = true;
    // Preprocessor directives are hard to match, so we have to hack this in.
    return options.pp_directive;
  } else if (tok::isStringLiteral(token.getKind()))
    return options.string_literal;
  else if (tok::isLiteral(token.getKind()))
    return options.scalar_literal;
  else if (highlighter.isKeyword(tok_str))
    return options.keyword;
  else
    switch (token.getKind()) {
    case tok::raw_identifier:
    case tok::identifier:
      return options.identifier;
    case tok::l_brace:
    case tok::r_brace:
      return options.braces;
    case tok::l_square:
    case tok::r_square:
      return options.square_brackets;
    case tok::l_paren:
    case tok::r_paren:
      return options.parentheses;
    case tok::comma:
      return options.comma;
    case tok::coloncolon:
    case tok::colon:
      return options.colon;

    case tok::amp:
    case tok::ampamp:
    case tok::ampequal:
    case tok::star:
    case tok::starequal:
    case tok::plus:
    case tok::plusplus:
    case tok::plusequal:
    case tok::minus:
    case tok::arrow:
    case tok::minusminus:
    case tok::minusequal:
    case tok::tilde:
    case tok::exclaim:
    case tok::exclaimequal:
    case tok::slash:
    case tok::slashequal:
    case tok::percent:
    case tok::percentequal:
    case tok::less:
    case tok::lessless:
    case tok::lessequal:
    case tok::lesslessequal:
    case tok::spaceship:
    case tok::greater:
    case tok::greatergreater:
    case tok::greaterequal:
    case tok::greatergreaterequal:
    case tok::caret:
    case tok::caretequal:
    case tok::pipe:
    case tok::pipepipe:
    case tok::pipeequal:
    case tok::question:
    case tok::equal:
    case tok::equalequal:
      return options.operators;
    default:
      break;
    }
  return HighlightStyle::ColorStyle();
}

void ClangHighlighter::Highlight(const HighlightStyle &options,
                                 llvm::StringRef line,
                                 std::optional<size_t> cursor_pos,
                                 llvm::StringRef previous_lines,
                                 Stream &result) const {
  using namespace clang;

  FileSystemOptions file_opts;
  FileManager file_mgr(file_opts,
                       FileSystem::Instance().GetVirtualFileSystem());

  // The line might end in a backslash which would cause Clang to drop the
  // backslash and the terminating new line. This makes sense when parsing C++,
  // but when highlighting we care about preserving the backslash/newline. To
  // not lose this information we remove the new line here so that Clang knows
  // this is just a single line we are highlighting. We add back the newline
  // after tokenizing.
  llvm::StringRef line_ending = "";
  // There are a few legal line endings Clang recognizes and we need to
  // temporarily remove from the string.
  if (line.consume_back("\r\n"))
    line_ending = "\r\n";
  else if (line.consume_back("\n"))
    line_ending = "\n";
  else if (line.consume_back("\r"))
    line_ending = "\r";

  unsigned line_number = previous_lines.count('\n') + 1U;

  // Let's build the actual source code Clang needs and setup some utility
  // objects.
  std::string full_source = previous_lines.str() + line.str();
  llvm::IntrusiveRefCntPtr<DiagnosticIDs> diag_ids(new DiagnosticIDs());
  llvm::IntrusiveRefCntPtr<DiagnosticOptions> diags_opts(
      new DiagnosticOptions());
  DiagnosticsEngine diags(diag_ids, diags_opts);
  clang::SourceManager SM(diags, file_mgr);
  auto buf = llvm::MemoryBuffer::getMemBuffer(full_source);

  FileID FID = SM.createFileID(buf->getMemBufferRef());

  // Let's just enable the latest ObjC and C++ which should get most tokens
  // right.
  LangOptions Opts;
  Opts.ObjC = true;
  // FIXME: This should probably set CPlusPlus, CPlusPlus11, ... too
  Opts.CPlusPlus17 = true;
  Opts.LineComment = true;

  Lexer lex(FID, buf->getMemBufferRef(), SM, Opts);
  // The lexer should keep whitespace around.
  lex.SetKeepWhitespaceMode(true);

  // Keeps track if we have entered a PP directive.
  bool in_pp_directive = false;

  // True once we actually lexed the user provided line.
  bool found_user_line = false;

  // True if we already highlighted the token under the cursor, false otherwise.
  bool highlighted_cursor = false;
  Token token;
  bool exit = false;
  while (!exit) {
    // Returns true if this is the last token we get from the lexer.
    exit = lex.LexFromRawLexer(token);

    bool invalid = false;
    unsigned current_line_number =
        SM.getSpellingLineNumber(token.getLocation(), &invalid);
    if (current_line_number != line_number)
      continue;
    found_user_line = true;

    // We don't need to print any tokens without a spelling line number.
    if (invalid)
      continue;

    // Same as above but with the column number.
    invalid = false;
    unsigned start = SM.getSpellingColumnNumber(token.getLocation(), &invalid);
    if (invalid)
      continue;
    // Column numbers start at 1, but indexes in our string start at 0.
    --start;

    // Annotations don't have a length, so let's skip them.
    if (token.isAnnotation())
      continue;

    // Extract the token string from our source code.
    llvm::StringRef tok_str = line.substr(start, token.getLength());

    // If the token is just an empty string, we can skip all the work below.
    if (tok_str.empty())
      continue;

    // If the cursor is inside this token, we have to apply the 'selected'
    // highlight style before applying the actual token color.
    llvm::StringRef to_print = tok_str;
    StreamString storage;
    auto end = start + token.getLength();
    if (cursor_pos && end > *cursor_pos && !highlighted_cursor) {
      highlighted_cursor = true;
      options.selected.Apply(storage, tok_str);
      to_print = storage.GetString();
    }

    // See how we are supposed to highlight this token.
    HighlightStyle::ColorStyle color =
        determineClangStyle(*this, token, tok_str, options, in_pp_directive);

    color.Apply(result, to_print);
  }

  // Add the line ending we trimmed before tokenizing.
  result << line_ending;

  // If we went over the whole file but couldn't find our own file, then
  // somehow our setup was wrong. When we're in release mode we just give the
  // user the normal line and pretend we don't know how to highlight it. In
  // debug mode we bail out with an assert as this should never happen.
  if (!found_user_line) {
    result << line;
    assert(false && "We couldn't find the user line in the input file?");
  }
}
