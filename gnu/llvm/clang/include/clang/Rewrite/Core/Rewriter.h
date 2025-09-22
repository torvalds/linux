//===- Rewriter.h - Code rewriting interface --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Rewriter class, which is used for code
//  transformations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_CORE_REWRITER_H
#define LLVM_CLANG_REWRITE_CORE_REWRITER_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Rewrite/Core/RewriteBuffer.h"
#include "llvm/ADT/StringRef.h"
#include <map>
#include <string>

namespace clang {

class LangOptions;
class SourceManager;

/// Rewriter - This is the main interface to the rewrite buffers.  Its primary
/// job is to dispatch high-level requests to the low-level RewriteBuffers that
/// are involved.
class Rewriter {
  SourceManager *SourceMgr = nullptr;
  const LangOptions *LangOpts = nullptr;
  std::map<FileID, RewriteBuffer> RewriteBuffers;

public:
  struct RewriteOptions {
    /// Given a source range, true to include previous inserts at the
    /// beginning of the range as part of the range itself (true by default).
    bool IncludeInsertsAtBeginOfRange = true;

    /// Given a source range, true to include previous inserts at the
    /// end of the range as part of the range itself (true by default).
    bool IncludeInsertsAtEndOfRange = true;

    /// If true and removing some text leaves a blank line
    /// also remove the empty line (false by default).
    ///
    /// FIXME: This sometimes corrupts the file's rewrite buffer due to
    /// incorrect indexing in the implementation (see the FIXME in
    /// clang::RewriteBuffer::RemoveText).  Moreover, it's inefficient because
    /// it must scan the buffer from the beginning to find the start of the
    /// line.  When feasible, it's better for the caller to check for a blank
    /// line and then, if found, expand the removal range to include it.
    /// Checking for a blank line is easy if, for example, the caller can
    /// guarantee this is the first edit of a line.  In that case, it can just
    /// scan before and after the removal range until the next newline or
    /// begin/end of the input.
    bool RemoveLineIfEmpty = false;

    RewriteOptions() {}
  };

  using buffer_iterator = std::map<FileID, RewriteBuffer>::iterator;
  using const_buffer_iterator = std::map<FileID, RewriteBuffer>::const_iterator;

  explicit Rewriter() = default;
  explicit Rewriter(SourceManager &SM, const LangOptions &LO)
      : SourceMgr(&SM), LangOpts(&LO) {}

  void setSourceMgr(SourceManager &SM, const LangOptions &LO) {
    SourceMgr = &SM;
    LangOpts = &LO;
  }

  SourceManager &getSourceMgr() const { return *SourceMgr; }
  const LangOptions &getLangOpts() const { return *LangOpts; }

  /// isRewritable - Return true if this location is a raw file location, which
  /// is rewritable.  Locations from macros, etc are not rewritable.
  static bool isRewritable(SourceLocation Loc) {
    return Loc.isFileID();
  }

  /// getRangeSize - Return the size in bytes of the specified range if they
  /// are in the same file.  If not, this returns -1.
  int getRangeSize(SourceRange Range,
                   RewriteOptions opts = RewriteOptions()) const;
  int getRangeSize(const CharSourceRange &Range,
                   RewriteOptions opts = RewriteOptions()) const;

  /// getRewrittenText - Return the rewritten form of the text in the specified
  /// range.  If the start or end of the range was unrewritable or if they are
  /// in different buffers, this returns an empty string.
  ///
  /// Note that this method is not particularly efficient.
  std::string getRewrittenText(CharSourceRange Range) const;

  /// getRewrittenText - Return the rewritten form of the text in the specified
  /// range.  If the start or end of the range was unrewritable or if they are
  /// in different buffers, this returns an empty string.
  ///
  /// Note that this method is not particularly efficient.
  std::string getRewrittenText(SourceRange Range) const {
    return getRewrittenText(CharSourceRange::getTokenRange(Range));
  }

  /// InsertText - Insert the specified string at the specified location in the
  /// original buffer.  This method returns true (and does nothing) if the input
  /// location was not rewritable, false otherwise.
  ///
  /// \param indentNewLines if true new lines in the string are indented
  /// using the indentation of the source line in position \p Loc.
  bool InsertText(SourceLocation Loc, StringRef Str,
                  bool InsertAfter = true, bool indentNewLines = false);

  /// InsertTextAfter - Insert the specified string at the specified location in
  ///  the original buffer.  This method returns true (and does nothing) if
  ///  the input location was not rewritable, false otherwise.  Text is
  ///  inserted after any other text that has been previously inserted
  ///  at the some point (the default behavior for InsertText).
  bool InsertTextAfter(SourceLocation Loc, StringRef Str) {
    return InsertText(Loc, Str);
  }

  /// Insert the specified string after the token in the
  /// specified location.
  bool InsertTextAfterToken(SourceLocation Loc, StringRef Str);

  /// InsertText - Insert the specified string at the specified location in the
  /// original buffer.  This method returns true (and does nothing) if the input
  /// location was not rewritable, false otherwise.  Text is
  /// inserted before any other text that has been previously inserted
  /// at the some point.
  bool InsertTextBefore(SourceLocation Loc, StringRef Str) {
    return InsertText(Loc, Str, false);
  }

  /// RemoveText - Remove the specified text region.
  bool RemoveText(SourceLocation Start, unsigned Length,
                  RewriteOptions opts = RewriteOptions());

  /// Remove the specified text region.
  bool RemoveText(CharSourceRange range,
                  RewriteOptions opts = RewriteOptions()) {
    return RemoveText(range.getBegin(), getRangeSize(range, opts), opts);
  }

  /// Remove the specified text region.
  bool RemoveText(SourceRange range, RewriteOptions opts = RewriteOptions()) {
    return RemoveText(range.getBegin(), getRangeSize(range, opts), opts);
  }

  /// ReplaceText - This method replaces a range of characters in the input
  /// buffer with a new string.  This is effectively a combined "remove/insert"
  /// operation.
  bool ReplaceText(SourceLocation Start, unsigned OrigLength,
                   StringRef NewStr);

  /// ReplaceText - This method replaces a range of characters in the input
  /// buffer with a new string.  This is effectively a combined "remove/insert"
  /// operation.
  bool ReplaceText(CharSourceRange range, StringRef NewStr) {
    return ReplaceText(range.getBegin(), getRangeSize(range), NewStr);
  }

  /// ReplaceText - This method replaces a range of characters in the input
  /// buffer with a new string.  This is effectively a combined "remove/insert"
  /// operation.
  bool ReplaceText(SourceRange range, StringRef NewStr) {
    return ReplaceText(range.getBegin(), getRangeSize(range), NewStr);
  }

  /// ReplaceText - This method replaces a range of characters in the input
  /// buffer with a new string.  This is effectively a combined "remove/insert"
  /// operation.
  bool ReplaceText(SourceRange range, SourceRange replacementRange);

  /// Increase indentation for the lines between the given source range.
  /// To determine what the indentation should be, 'parentIndent' is used
  /// that should be at a source location with an indentation one degree
  /// lower than the given range.
  bool IncreaseIndentation(CharSourceRange range, SourceLocation parentIndent);
  bool IncreaseIndentation(SourceRange range, SourceLocation parentIndent) {
    return IncreaseIndentation(CharSourceRange::getTokenRange(range),
                               parentIndent);
  }

  /// getEditBuffer - This is like getRewriteBufferFor, but always returns a
  /// buffer, and allows you to write on it directly.  This is useful if you
  /// want efficient low-level access to apis for scribbling on one specific
  /// FileID's buffer.
  RewriteBuffer &getEditBuffer(FileID FID);

  /// getRewriteBufferFor - Return the rewrite buffer for the specified FileID.
  /// If no modification has been made to it, return null.
  const RewriteBuffer *getRewriteBufferFor(FileID FID) const {
    std::map<FileID, RewriteBuffer>::const_iterator I =
      RewriteBuffers.find(FID);
    return I == RewriteBuffers.end() ? nullptr : &I->second;
  }

  // Iterators over rewrite buffers.
  buffer_iterator buffer_begin() { return RewriteBuffers.begin(); }
  buffer_iterator buffer_end() { return RewriteBuffers.end(); }
  const_buffer_iterator buffer_begin() const { return RewriteBuffers.begin(); }
  const_buffer_iterator buffer_end() const { return RewriteBuffers.end(); }

  /// overwriteChangedFiles - Save all changed files to disk.
  ///
  /// Returns true if any files were not saved successfully.
  /// Outputs diagnostics via the source manager's diagnostic engine
  /// in case of an error.
  bool overwriteChangedFiles();

private:
  unsigned getLocationOffsetAndFileID(SourceLocation Loc, FileID &FID) const;
};

} // namespace clang

#endif // LLVM_CLANG_REWRITE_CORE_REWRITER_H
