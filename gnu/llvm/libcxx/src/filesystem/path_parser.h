//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef PATH_PARSER_H
#define PATH_PARSER_H

#include <__config>
#include <__utility/unreachable.h>
#include <cstddef>
#include <filesystem>
#include <utility>

#include "format_string.h"

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

inline bool isSeparator(path::value_type C) {
  if (C == '/')
    return true;
#if defined(_LIBCPP_WIN32API)
  if (C == '\\')
    return true;
#endif
  return false;
}

inline bool isDriveLetter(path::value_type C) { return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z'); }

namespace parser {

using string_view_t    = path::__string_view;
using string_view_pair = pair<string_view_t, string_view_t>;
using PosPtr           = path::value_type const*;

struct PathParser {
  enum ParserState : unsigned char {
    // Zero is a special sentinel value used by default constructed iterators.
    PS_BeforeBegin   = path::iterator::_BeforeBegin,
    PS_InRootName    = path::iterator::_InRootName,
    PS_InRootDir     = path::iterator::_InRootDir,
    PS_InFilenames   = path::iterator::_InFilenames,
    PS_InTrailingSep = path::iterator::_InTrailingSep,
    PS_AtEnd         = path::iterator::_AtEnd
  };

  const string_view_t Path;
  string_view_t RawEntry;
  ParserState State_;

private:
  PathParser(string_view_t P, ParserState State) noexcept : Path(P), State_(State) {}

public:
  PathParser(string_view_t P, string_view_t E, unsigned char S)
      : Path(P), RawEntry(E), State_(static_cast<ParserState>(S)) {
    // S cannot be '0' or PS_BeforeBegin.
  }

  static PathParser CreateBegin(string_view_t P) noexcept {
    PathParser PP(P, PS_BeforeBegin);
    PP.increment();
    return PP;
  }

  static PathParser CreateEnd(string_view_t P) noexcept {
    PathParser PP(P, PS_AtEnd);
    return PP;
  }

  PosPtr peek() const noexcept {
    auto TkEnd = getNextTokenStartPos();
    auto End   = getAfterBack();
    return TkEnd == End ? nullptr : TkEnd;
  }

  void increment() noexcept {
    const PosPtr End   = getAfterBack();
    const PosPtr Start = getNextTokenStartPos();
    if (Start == End)
      return makeState(PS_AtEnd);

    switch (State_) {
    case PS_BeforeBegin: {
      PosPtr TkEnd = consumeRootName(Start, End);
      if (TkEnd)
        return makeState(PS_InRootName, Start, TkEnd);
    }
      _LIBCPP_FALLTHROUGH();
    case PS_InRootName: {
      PosPtr TkEnd = consumeAllSeparators(Start, End);
      if (TkEnd)
        return makeState(PS_InRootDir, Start, TkEnd);
      else
        return makeState(PS_InFilenames, Start, consumeName(Start, End));
    }
    case PS_InRootDir:
      return makeState(PS_InFilenames, Start, consumeName(Start, End));

    case PS_InFilenames: {
      PosPtr SepEnd = consumeAllSeparators(Start, End);
      if (SepEnd != End) {
        PosPtr TkEnd = consumeName(SepEnd, End);
        if (TkEnd)
          return makeState(PS_InFilenames, SepEnd, TkEnd);
      }
      return makeState(PS_InTrailingSep, Start, SepEnd);
    }

    case PS_InTrailingSep:
      return makeState(PS_AtEnd);

    case PS_AtEnd:
      __libcpp_unreachable();
    }
  }

  void decrement() noexcept {
    const PosPtr REnd   = getBeforeFront();
    const PosPtr RStart = getCurrentTokenStartPos() - 1;
    if (RStart == REnd) // we're decrementing the begin
      return makeState(PS_BeforeBegin);

    switch (State_) {
    case PS_AtEnd: {
      // Try to consume a trailing separator or root directory first.
      if (PosPtr SepEnd = consumeAllSeparators(RStart, REnd)) {
        if (SepEnd == REnd)
          return makeState(PS_InRootDir, Path.data(), RStart + 1);
        PosPtr TkStart = consumeRootName(SepEnd, REnd);
        if (TkStart == REnd)
          return makeState(PS_InRootDir, RStart, RStart + 1);
        return makeState(PS_InTrailingSep, SepEnd + 1, RStart + 1);
      } else {
        PosPtr TkStart = consumeRootName(RStart, REnd);
        if (TkStart == REnd)
          return makeState(PS_InRootName, TkStart + 1, RStart + 1);
        TkStart = consumeName(RStart, REnd);
        return makeState(PS_InFilenames, TkStart + 1, RStart + 1);
      }
    }
    case PS_InTrailingSep:
      return makeState(PS_InFilenames, consumeName(RStart, REnd) + 1, RStart + 1);
    case PS_InFilenames: {
      PosPtr SepEnd = consumeAllSeparators(RStart, REnd);
      if (SepEnd == REnd)
        return makeState(PS_InRootDir, Path.data(), RStart + 1);
      PosPtr TkStart = consumeRootName(SepEnd ? SepEnd : RStart, REnd);
      if (TkStart == REnd) {
        if (SepEnd)
          return makeState(PS_InRootDir, SepEnd + 1, RStart + 1);
        return makeState(PS_InRootName, TkStart + 1, RStart + 1);
      }
      TkStart = consumeName(SepEnd, REnd);
      return makeState(PS_InFilenames, TkStart + 1, SepEnd + 1);
    }
    case PS_InRootDir:
      return makeState(PS_InRootName, Path.data(), RStart + 1);
    case PS_InRootName:
    case PS_BeforeBegin:
      __libcpp_unreachable();
    }
  }

  /// \brief Return a view with the "preferred representation" of the current
  ///   element. For example trailing separators are represented as a '.'
  string_view_t operator*() const noexcept {
    switch (State_) {
    case PS_BeforeBegin:
    case PS_AtEnd:
      return PATHSTR("");
    case PS_InRootDir:
      if (RawEntry[0] == '\\')
        return PATHSTR("\\");
      else
        return PATHSTR("/");
    case PS_InTrailingSep:
      return PATHSTR("");
    case PS_InRootName:
    case PS_InFilenames:
      return RawEntry;
    }
    __libcpp_unreachable();
  }

  explicit operator bool() const noexcept { return State_ != PS_BeforeBegin && State_ != PS_AtEnd; }

  PathParser& operator++() noexcept {
    increment();
    return *this;
  }

  PathParser& operator--() noexcept {
    decrement();
    return *this;
  }

  bool atEnd() const noexcept { return State_ == PS_AtEnd; }

  bool inRootDir() const noexcept { return State_ == PS_InRootDir; }

  bool inRootName() const noexcept { return State_ == PS_InRootName; }

  bool inRootPath() const noexcept { return inRootName() || inRootDir(); }

private:
  void makeState(ParserState NewState, PosPtr Start, PosPtr End) noexcept {
    State_    = NewState;
    RawEntry = string_view_t(Start, End - Start);
  }
  void makeState(ParserState NewState) noexcept {
    State_    = NewState;
    RawEntry = {};
  }

  PosPtr getAfterBack() const noexcept { return Path.data() + Path.size(); }

  PosPtr getBeforeFront() const noexcept { return Path.data() - 1; }

  /// \brief Return a pointer to the first character after the currently
  ///   lexed element.
  PosPtr getNextTokenStartPos() const noexcept {
    switch (State_) {
    case PS_BeforeBegin:
      return Path.data();
    case PS_InRootName:
    case PS_InRootDir:
    case PS_InFilenames:
      return &RawEntry.back() + 1;
    case PS_InTrailingSep:
    case PS_AtEnd:
      return getAfterBack();
    }
    __libcpp_unreachable();
  }

  /// \brief Return a pointer to the first character in the currently lexed
  ///   element.
  PosPtr getCurrentTokenStartPos() const noexcept {
    switch (State_) {
    case PS_BeforeBegin:
    case PS_InRootName:
      return &Path.front();
    case PS_InRootDir:
    case PS_InFilenames:
    case PS_InTrailingSep:
      return &RawEntry.front();
    case PS_AtEnd:
      return &Path.back() + 1;
    }
    __libcpp_unreachable();
  }

  // Consume all consecutive separators.
  PosPtr consumeAllSeparators(PosPtr P, PosPtr End) const noexcept {
    if (P == nullptr || P == End || !isSeparator(*P))
      return nullptr;
    const int Inc = P < End ? 1 : -1;
    P += Inc;
    while (P != End && isSeparator(*P))
      P += Inc;
    return P;
  }

  // Consume exactly N separators, or return nullptr.
  PosPtr consumeNSeparators(PosPtr P, PosPtr End, int N) const noexcept {
    PosPtr Ret = consumeAllSeparators(P, End);
    if (Ret == nullptr)
      return nullptr;
    if (P < End) {
      if (Ret == P + N)
        return Ret;
    } else {
      if (Ret == P - N)
        return Ret;
    }
    return nullptr;
  }

  PosPtr consumeName(PosPtr P, PosPtr End) const noexcept {
    PosPtr Start = P;
    if (P == nullptr || P == End || isSeparator(*P))
      return nullptr;
    const int Inc = P < End ? 1 : -1;
    P += Inc;
    while (P != End && !isSeparator(*P))
      P += Inc;
    if (P == End && Inc < 0) {
      // Iterating backwards and consumed all the rest of the input.
      // Check if the start of the string would have been considered
      // a root name.
      PosPtr RootEnd = consumeRootName(End + 1, Start);
      if (RootEnd)
        return RootEnd - 1;
    }
    return P;
  }

  PosPtr consumeDriveLetter(PosPtr P, PosPtr End) const noexcept {
    if (P == End)
      return nullptr;
    if (P < End) {
      if (P + 1 == End || !isDriveLetter(P[0]) || P[1] != ':')
        return nullptr;
      return P + 2;
    } else {
      if (P - 1 == End || !isDriveLetter(P[-1]) || P[0] != ':')
        return nullptr;
      return P - 2;
    }
  }

  PosPtr consumeNetworkRoot(PosPtr P, PosPtr End) const noexcept {
    if (P == End)
      return nullptr;
    if (P < End)
      return consumeName(consumeNSeparators(P, End, 2), End);
    else
      return consumeNSeparators(consumeName(P, End), End, 2);
  }

  PosPtr consumeRootName(PosPtr P, PosPtr End) const noexcept {
#if defined(_LIBCPP_WIN32API)
    if (PosPtr Ret = consumeDriveLetter(P, End))
      return Ret;
    if (PosPtr Ret = consumeNetworkRoot(P, End))
      return Ret;
#endif
    return nullptr;
  }
};

inline string_view_pair separate_filename(string_view_t const& s) {
  if (s == PATHSTR(".") || s == PATHSTR("..") || s.empty())
    return string_view_pair{s, PATHSTR("")};
  auto pos = s.find_last_of('.');
  if (pos == string_view_t::npos || pos == 0)
    return string_view_pair{s, string_view_t{}};
  return string_view_pair{s.substr(0, pos), s.substr(pos)};
}

inline string_view_t createView(PosPtr S, PosPtr E) noexcept { return {S, static_cast<size_t>(E - S) + 1}; }

} // namespace parser

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // PATH_PARSER_H
