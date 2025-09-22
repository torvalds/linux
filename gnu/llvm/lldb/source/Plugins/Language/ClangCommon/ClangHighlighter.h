//===-- ClangHighlighter.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_CLANGCOMMON_CLANGHIGHLIGHTER_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_CLANGCOMMON_CLANGHIGHLIGHTER_H

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringSet.h"

#include "lldb/Core/Highlighter.h"
#include <optional>

namespace lldb_private {

class ClangHighlighter : public Highlighter {
  llvm::StringSet<> keywords;

public:
  ClangHighlighter();
  llvm::StringRef GetName() const override { return "clang"; }

  void Highlight(const HighlightStyle &options, llvm::StringRef line,
                 std::optional<size_t> cursor_pos,
                 llvm::StringRef previous_lines, Stream &s) const override;

  /// Returns true if the given string represents a keywords in any Clang
  /// supported language.
  bool isKeyword(llvm::StringRef token) const;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_CLANGCOMMON_CLANGHIGHLIGHTER_H
