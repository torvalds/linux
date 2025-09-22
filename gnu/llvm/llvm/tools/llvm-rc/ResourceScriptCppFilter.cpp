//===-- ResourceScriptCppFilter.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file implements an interface defined in ResourceScriptCppFilter.h.
//
//===---------------------------------------------------------------------===//

#include "ResourceScriptCppFilter.h"
#include "llvm/ADT/StringExtras.h"

#include <vector>

using namespace llvm;

namespace {

class Filter {
public:
  explicit Filter(StringRef Input) : Data(Input), DataLength(Input.size()) {}

  std::string run();

private:
  // Parse the line, returning whether the line should be included in
  // the output.
  bool parseLine(StringRef Line);

  bool streamEof() const;

  StringRef Data;
  size_t DataLength;

  size_t Pos = 0;
  bool Outputting = true;
};

std::string Filter::run() {
  std::vector<StringRef> Output;

  while (!streamEof() && Pos != StringRef::npos) {
    size_t LineStart = Pos;
    Pos = Data.find_first_of("\r\n", Pos);
    Pos = Data.find_first_not_of("\r\n", Pos);
    StringRef Line = Data.take_front(Pos).drop_front(LineStart);

    if (parseLine(Line))
      Output.push_back(Line);
  }

  return llvm::join(Output, "");
}

bool Filter::parseLine(StringRef Line) {
  Line = Line.ltrim();

  if (!Line.consume_front("#")) {
    // A normal content line, filtered according to the current mode.
    return Outputting;
  }

  // Found a preprocessing directive line. From here on, we always return
  // false since the preprocessing directives should be filtered out.

  Line.consume_front("line");
  if (!Line.starts_with(" "))
    return false; // Not a line directive (pragma etc).

  // #line 123 "path/file.h"
  // # 123 "path/file.h" 1

  Line =
      Line.ltrim(); // There could be multiple spaces after the #line directive

  size_t N;
  if (Line.consumeInteger(10, N)) // Returns true to signify an error
    return false;

  Line = Line.ltrim();

  if (!Line.consume_front("\""))
    return false; // Malformed line, no quote found.

  // Split the string at the last quote (in case the path name had
  // escaped quotes as well).
  Line = Line.rsplit('"').first;

  StringRef Ext = Line.rsplit('.').second;

  if (Ext.equals_insensitive("h") || Ext.equals_insensitive("c")) {
    Outputting = false;
  } else {
    Outputting = true;
  }

  return false;
}

bool Filter::streamEof() const { return Pos == DataLength; }

} // anonymous namespace

namespace llvm {

std::string filterCppOutput(StringRef Input) { return Filter(Input).run(); }

} // namespace llvm
