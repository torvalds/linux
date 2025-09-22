//===- lib/DebugInfo/Symbolize/Markup.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the log symbolizer markup data model and parser.
///
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/Markup.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"

namespace llvm {
namespace symbolize {

// Matches the following:
//   "\033[0m"
//   "\033[1m"
//   "\033[30m" -- "\033[37m"
static const char SGRSyntaxStr[] = "\033\\[([0-1]|3[0-7])m";

MarkupParser::MarkupParser(StringSet<> MultilineTags)
    : MultilineTags(std::move(MultilineTags)), SGRSyntax(SGRSyntaxStr) {}

static StringRef takeTo(StringRef Str, StringRef::iterator Pos) {
  return Str.take_front(Pos - Str.begin());
}
static void advanceTo(StringRef &Str, StringRef::iterator Pos) {
  Str = Str.drop_front(Pos - Str.begin());
}

void MarkupParser::parseLine(StringRef Line) {
  Buffer.clear();
  NextIdx = 0;
  FinishedMultiline.clear();
  this->Line = Line;
}

std::optional<MarkupNode> MarkupParser::nextNode() {
  // Pull something out of the buffer if possible.
  if (!Buffer.empty()) {
    if (NextIdx < Buffer.size())
      return std::move(Buffer[NextIdx++]);
    NextIdx = 0;
    Buffer.clear();
  }

  // The buffer is empty, so parse the next bit of the line.

  if (Line.empty())
    return std::nullopt;

  if (!InProgressMultiline.empty()) {
    if (std::optional<StringRef> MultilineEnd = parseMultiLineEnd(Line)) {
      llvm::append_range(InProgressMultiline, *MultilineEnd);
      assert(FinishedMultiline.empty() &&
             "At most one multi-line element can be finished at a time.");
      FinishedMultiline.swap(InProgressMultiline);
      // Parse the multi-line element as if it were contiguous.
      advanceTo(Line, MultilineEnd->end());
      return *parseElement(FinishedMultiline);
    }

    // The whole line is part of the multi-line element.
    llvm::append_range(InProgressMultiline, Line);
    Line = Line.drop_front(Line.size());
    return std::nullopt;
  }

  // Find the first valid markup element, if any.
  if (std::optional<MarkupNode> Element = parseElement(Line)) {
    parseTextOutsideMarkup(takeTo(Line, Element->Text.begin()));
    Buffer.push_back(std::move(*Element));
    advanceTo(Line, Element->Text.end());
    return nextNode();
  }

  // Since there were no valid elements remaining, see if the line opens a
  // multi-line element.
  if (std::optional<StringRef> MultilineBegin = parseMultiLineBegin(Line)) {
    // Emit any text before the element.
    parseTextOutsideMarkup(takeTo(Line, MultilineBegin->begin()));

    // Begin recording the multi-line element.
    llvm::append_range(InProgressMultiline, *MultilineBegin);
    Line = Line.drop_front(Line.size());
    return nextNode();
  }

  // The line doesn't contain any more markup elements, so emit it as text.
  parseTextOutsideMarkup(Line);
  Line = Line.drop_front(Line.size());
  return nextNode();
}

void MarkupParser::flush() {
  Buffer.clear();
  NextIdx = 0;
  Line = {};
  if (InProgressMultiline.empty())
    return;
  FinishedMultiline.swap(InProgressMultiline);
  parseTextOutsideMarkup(FinishedMultiline);
}

// Finds and returns the next valid markup element in the given line. Returns
// std::nullopt if the line contains no valid elements.
std::optional<MarkupNode> MarkupParser::parseElement(StringRef Line) {
  while (true) {
    // Find next element using begin and end markers.
    size_t BeginPos = Line.find("{{{");
    if (BeginPos == StringRef::npos)
      return std::nullopt;
    size_t EndPos = Line.find("}}}", BeginPos + 3);
    if (EndPos == StringRef::npos)
      return std::nullopt;
    EndPos += 3;
    MarkupNode Element;
    Element.Text = Line.slice(BeginPos, EndPos);
    Line = Line.substr(EndPos);

    // Parse tag.
    StringRef Content = Element.Text.drop_front(3).drop_back(3);
    StringRef FieldsContent;
    std::tie(Element.Tag, FieldsContent) = Content.split(':');
    if (Element.Tag.empty())
      continue;

    // Parse fields.
    if (!FieldsContent.empty())
      FieldsContent.split(Element.Fields, ":");
    else if (Content.back() == ':')
      Element.Fields.push_back(FieldsContent);

    return Element;
  }
}

static MarkupNode textNode(StringRef Text) {
  MarkupNode Node;
  Node.Text = Text;
  return Node;
}

// Parses a region of text known to be outside any markup elements. Such text
// may still contain SGR control codes, so the region is further subdivided into
// control codes and true text regions.
void MarkupParser::parseTextOutsideMarkup(StringRef Text) {
  if (Text.empty())
    return;
  SmallVector<StringRef> Matches;
  while (SGRSyntax.match(Text, &Matches)) {
    // Emit any text before the SGR element.
    if (Matches.begin()->begin() != Text.begin())
      Buffer.push_back(textNode(takeTo(Text, Matches.begin()->begin())));

    Buffer.push_back(textNode(*Matches.begin()));
    advanceTo(Text, Matches.begin()->end());
  }
  if (!Text.empty())
    Buffer.push_back(textNode(Text));
}

// Given that a line doesn't contain any valid markup, see if it ends with the
// start of a multi-line element. If so, returns the beginning.
std::optional<StringRef> MarkupParser::parseMultiLineBegin(StringRef Line) {
  // A multi-line begin marker must be the last one on the line.
  size_t BeginPos = Line.rfind("{{{");
  if (BeginPos == StringRef::npos)
    return std::nullopt;
  size_t BeginTagPos = BeginPos + 3;

  // If there are any end markers afterwards, the begin marker cannot belong to
  // a multi-line element.
  size_t EndPos = Line.find("}}}", BeginTagPos);
  if (EndPos != StringRef::npos)
    return std::nullopt;

  // Check whether the tag is registered multi-line.
  size_t EndTagPos = Line.find(':', BeginTagPos);
  if (EndTagPos == StringRef::npos)
    return std::nullopt;
  StringRef Tag = Line.slice(BeginTagPos, EndTagPos);
  if (!MultilineTags.contains(Tag))
    return std::nullopt;
  return Line.substr(BeginPos);
}

// See if the line begins with the ending of an in-progress multi-line element.
// If so, return the ending.
std::optional<StringRef> MarkupParser::parseMultiLineEnd(StringRef Line) {
  size_t EndPos = Line.find("}}}");
  if (EndPos == StringRef::npos)
    return std::nullopt;
  return Line.take_front(EndPos + 3);
}

} // end namespace symbolize
} // end namespace llvm
