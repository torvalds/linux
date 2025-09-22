//==- HTMLRewrite.h - Translate source code into prettified HTML ---*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a set of functions used for translating source code
//  into beautified HTML.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_CORE_HTMLREWRITE_H
#define LLVM_CLANG_REWRITE_CORE_HTMLREWRITE_H

#include "clang/Basic/SourceLocation.h"
#include <string>

namespace clang {

class Rewriter;
class RewriteBuffer;
class Preprocessor;

namespace html {
  struct RelexRewriteCache;
  using RelexRewriteCacheRef = std::shared_ptr<RelexRewriteCache>;

  /// If you need to rewrite the same file multiple times, you can instantiate
  /// a RelexRewriteCache and refer functions such as SyntaxHighlight()
  /// and HighlightMacros() to it so that to avoid re-lexing the file each time.
  /// The cache may outlive the rewriter as long as cached FileIDs and source
  /// locations continue to make sense for the translation unit as a whole.
  RelexRewriteCacheRef instantiateRelexRewriteCache();

  /// HighlightRange - Highlight a range in the source code with the specified
  /// start/end tags.  B/E must be in the same file.  This ensures that
  /// start/end tags are placed at the start/end of each line if the range is
  /// multiline.
  void HighlightRange(Rewriter &R, SourceLocation B, SourceLocation E,
                      const char *StartTag, const char *EndTag,
                      bool IsTokenRange = true);

  /// HighlightRange - Highlight a range in the source code with the specified
  /// start/end tags.  The Start/end of the range must be in the same file.
  /// This ensures that start/end tags are placed at the start/end of each line
  /// if the range is multiline.
  inline void HighlightRange(Rewriter &R, SourceRange Range,
                             const char *StartTag, const char *EndTag) {
    HighlightRange(R, Range.getBegin(), Range.getEnd(), StartTag, EndTag);
  }

  /// HighlightRange - This is the same as the above method, but takes
  /// decomposed file locations.
  void HighlightRange(RewriteBuffer &RB, unsigned B, unsigned E,
                      const char *BufferStart,
                      const char *StartTag, const char *EndTag);

  /// EscapeText - HTMLize a specified file so that special characters are
  /// are translated so that they are not interpreted as HTML tags.
  void EscapeText(Rewriter& R, FileID FID,
                  bool EscapeSpaces = false, bool ReplaceTabs = false);

  /// EscapeText - HTMLized the provided string so that special characters
  ///  in 's' are not interpreted as HTML tags.  Unlike the version of
  ///  EscapeText that rewrites a file, this version by default replaces tabs
  ///  with spaces.
  std::string EscapeText(StringRef s,
                         bool EscapeSpaces = false, bool ReplaceTabs = false);

  void AddLineNumbers(Rewriter& R, FileID FID);

  void AddHeaderFooterInternalBuiltinCSS(Rewriter &R, FileID FID,
                                         StringRef title);

  /// SyntaxHighlight - Relex the specified FileID and annotate the HTML with
  /// information about keywords, comments, etc.
  void SyntaxHighlight(Rewriter &R, FileID FID, const Preprocessor &PP,
                       RelexRewriteCacheRef Cache = nullptr);

  /// HighlightMacros - This uses the macro table state from the end of the
  /// file, to reexpand macros and insert (into the HTML) information about the
  /// macro expansions.  This won't be perfectly perfect, but it will be
  /// reasonably close.
  void HighlightMacros(Rewriter &R, FileID FID, const Preprocessor &PP,
                       RelexRewriteCacheRef Cache = nullptr);

} // end html namespace
} // end clang namespace

#endif
