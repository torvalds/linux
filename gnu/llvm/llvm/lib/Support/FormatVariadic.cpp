//===- FormatVariadic.cpp - Format string parsing and analysis ----*-C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "llvm/Support/FormatVariadic.h"
#include <cassert>
#include <optional>

using namespace llvm;

static std::optional<AlignStyle> translateLocChar(char C) {
  switch (C) {
  case '-':
    return AlignStyle::Left;
  case '=':
    return AlignStyle::Center;
  case '+':
    return AlignStyle::Right;
  default:
    return std::nullopt;
  }
  LLVM_BUILTIN_UNREACHABLE;
}

bool formatv_object_base::consumeFieldLayout(StringRef &Spec, AlignStyle &Where,
                                             size_t &Align, char &Pad) {
  Where = AlignStyle::Right;
  Align = 0;
  Pad = ' ';
  if (Spec.empty())
    return true;

  if (Spec.size() > 1) {
    // A maximum of 2 characters at the beginning can be used for something
    // other
    // than the width.
    // If Spec[1] is a loc char, then Spec[0] is a pad char and Spec[2:...]
    // contains the width.
    // Otherwise, if Spec[0] is a loc char, then Spec[1:...] contains the width.
    // Otherwise, Spec[0:...] contains the width.
    if (auto Loc = translateLocChar(Spec[1])) {
      Pad = Spec[0];
      Where = *Loc;
      Spec = Spec.drop_front(2);
    } else if (auto Loc = translateLocChar(Spec[0])) {
      Where = *Loc;
      Spec = Spec.drop_front(1);
    }
  }

  bool Failed = Spec.consumeInteger(0, Align);
  return !Failed;
}

std::optional<ReplacementItem>
formatv_object_base::parseReplacementItem(StringRef Spec) {
  StringRef RepString = Spec.trim("{}");

  // If the replacement sequence does not start with a non-negative integer,
  // this is an error.
  char Pad = ' ';
  std::size_t Align = 0;
  AlignStyle Where = AlignStyle::Right;
  StringRef Options;
  size_t Index = 0;
  RepString = RepString.trim();
  if (RepString.consumeInteger(0, Index)) {
    assert(false && "Invalid replacement sequence index!");
    return ReplacementItem{};
  }
  RepString = RepString.trim();
  if (RepString.consume_front(",")) {
    if (!consumeFieldLayout(RepString, Where, Align, Pad))
      assert(false && "Invalid replacement field layout specification!");
  }
  RepString = RepString.trim();
  if (RepString.consume_front(":")) {
    Options = RepString.trim();
    RepString = StringRef();
  }
  RepString = RepString.trim();
  if (!RepString.empty()) {
    assert(false && "Unexpected characters found in replacement string!");
  }

  return ReplacementItem{Spec, Index, Align, Where, Pad, Options};
}

std::pair<ReplacementItem, StringRef>
formatv_object_base::splitLiteralAndReplacement(StringRef Fmt) {
  while (!Fmt.empty()) {
    // Everything up until the first brace is a literal.
    if (Fmt.front() != '{') {
      std::size_t BO = Fmt.find_first_of('{');
      return std::make_pair(ReplacementItem{Fmt.substr(0, BO)}, Fmt.substr(BO));
    }

    StringRef Braces = Fmt.take_while([](char C) { return C == '{'; });
    // If there is more than one brace, then some of them are escaped.  Treat
    // these as replacements.
    if (Braces.size() > 1) {
      size_t NumEscapedBraces = Braces.size() / 2;
      StringRef Middle = Fmt.take_front(NumEscapedBraces);
      StringRef Right = Fmt.drop_front(NumEscapedBraces * 2);
      return std::make_pair(ReplacementItem{Middle}, Right);
    }
    // An unterminated open brace is undefined.  We treat the rest of the string
    // as a literal replacement, but we assert to indicate that this is
    // undefined and that we consider it an error.
    std::size_t BC = Fmt.find_first_of('}');
    if (BC == StringRef::npos) {
      assert(
          false &&
          "Unterminated brace sequence.  Escape with {{ for a literal brace.");
      return std::make_pair(ReplacementItem{Fmt}, StringRef());
    }

    // Even if there is a closing brace, if there is another open brace before
    // this closing brace, treat this portion as literal, and try again with the
    // next one.
    std::size_t BO2 = Fmt.find_first_of('{', 1);
    if (BO2 < BC)
      return std::make_pair(ReplacementItem{Fmt.substr(0, BO2)},
                            Fmt.substr(BO2));

    StringRef Spec = Fmt.slice(1, BC);
    StringRef Right = Fmt.substr(BC + 1);

    auto RI = parseReplacementItem(Spec);
    if (RI)
      return std::make_pair(*RI, Right);

    // If there was an error parsing the replacement item, treat it as an
    // invalid replacement spec, and just continue.
    Fmt = Fmt.drop_front(BC + 1);
  }
  return std::make_pair(ReplacementItem{Fmt}, StringRef());
}

SmallVector<ReplacementItem, 2>
formatv_object_base::parseFormatString(StringRef Fmt) {
  SmallVector<ReplacementItem, 2> Replacements;
  ReplacementItem I;
  while (!Fmt.empty()) {
    std::tie(I, Fmt) = splitLiteralAndReplacement(Fmt);
    if (I.Type != ReplacementType::Empty)
      Replacements.push_back(I);
  }
  return Replacements;
}

void support::detail::format_adapter::anchor() {}
