//===- Markup.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the log symbolizer markup data model and parser.
///
/// See https://llvm.org/docs/SymbolizerMarkupFormat.html
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_SYMBOLIZE_MARKUP_H
#define LLVM_DEBUGINFO_SYMBOLIZE_MARKUP_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Regex.h"

namespace llvm {
namespace symbolize {

/// A node of symbolizer markup.
///
/// If only the Text field is set, this represents a region of text outside a
/// markup element. ANSI SGR control codes are also reported this way; if
/// detected, then the control code will be the entirety of the Text field, and
/// any surrounding text will be reported as preceding and following nodes.
struct MarkupNode {
  /// The full text of this node in the input.
  StringRef Text;

  /// If this represents an element, the tag. Otherwise, empty.
  StringRef Tag;

  /// If this represents an element with fields, a list of the field contents.
  /// Otherwise, empty.
  SmallVector<StringRef> Fields;

  bool operator==(const MarkupNode &Other) const {
    return Text == Other.Text && Tag == Other.Tag && Fields == Other.Fields;
  }
  bool operator!=(const MarkupNode &Other) const { return !(*this == Other); }
};

/// Parses a log containing symbolizer markup into a sequence of nodes.
class MarkupParser {
public:
  MarkupParser(StringSet<> MultilineTags = {});

  /// Parses an individual \p Line of input.
  ///
  /// Nodes from the previous parseLine() call that haven't yet been extracted
  /// by nextNode() are discarded. The nodes returned by nextNode() may
  /// reference the input string, so it must be retained by the caller until the
  /// last use.
  ///
  /// Note that some elements may span multiple lines. If a line ends with the
  /// start of one of these elements, then no nodes will be produced until the
  /// either the end or something that cannot be part of an element is
  /// encountered. This may only occur after multiple calls to parseLine(),
  /// corresponding to the lines of the multi-line element.
  void parseLine(StringRef Line);

  /// Inform the parser of that the input stream has ended.
  ///
  /// This allows the parser to finish any deferred processing (e.g., an
  /// in-progress multi-line element) and may cause nextNode() to return
  /// additional nodes.
  void flush();

  /// Returns the next node in the input sequence.
  ///
  /// Calling nextNode() may invalidate the contents of the node returned by the
  /// previous call.
  ///
  /// \returns the next markup node or std::nullopt if none remain.
  std::optional<MarkupNode> nextNode();

  bool isSGR(const MarkupNode &Node) const {
    return SGRSyntax.match(Node.Text);
  }

private:
  std::optional<MarkupNode> parseElement(StringRef Line);
  void parseTextOutsideMarkup(StringRef Text);
  std::optional<StringRef> parseMultiLineBegin(StringRef Line);
  std::optional<StringRef> parseMultiLineEnd(StringRef Line);

  // Tags of elements that can span multiple lines.
  const StringSet<> MultilineTags;

  // Contents of a multi-line element that has finished being parsed. Retained
  // to keep returned StringRefs for the contents valid.
  std::string FinishedMultiline;

  // Contents of a multi-line element that is still in the process of receiving
  // lines.
  std::string InProgressMultiline;

  // The line currently being parsed.
  StringRef Line;

  // Buffer for nodes parsed from the current line.
  SmallVector<MarkupNode> Buffer;

  // Next buffer index to return.
  size_t NextIdx;

  // Regular expression matching supported ANSI SGR escape sequences.
  const Regex SGRSyntax;
};

} // end namespace symbolize
} // end namespace llvm

#endif // LLVM_DEBUGINFO_SYMBOLIZE_MARKUP_H
