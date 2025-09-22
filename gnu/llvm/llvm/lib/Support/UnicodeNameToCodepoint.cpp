//===- llvm/Support/UnicodeNameToCodepoint.cpp - Unicode character properties
//-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements functions to map the name or alias of a unicode
// character to its codepoint.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Unicode.h"

namespace llvm {
namespace sys {
namespace unicode {

extern const char *UnicodeNameToCodepointDict;
extern const uint8_t *UnicodeNameToCodepointIndex;
extern const std::size_t UnicodeNameToCodepointIndexSize;
extern const std::size_t UnicodeNameToCodepointLargestNameSize;

using BufferType = SmallString<64>;

struct Node {
  bool IsRoot = false;
  char32_t Value = 0xFFFFFFFF;
  uint32_t ChildrenOffset = 0;
  bool HasSibling = false;
  uint32_t Size = 0;
  StringRef Name;
  const Node *Parent = nullptr;

  constexpr bool isValid() const {
    return !Name.empty() || Value == 0xFFFFFFFF;
  }
  constexpr bool hasChildren() const { return ChildrenOffset != 0 || IsRoot; }

  std::string fullName() const {
    std::string S;
    // Reserve enough space for most unicode code points.
    // The chosen value represent the 99th percentile of name size as of
    // Unicode 15.0.
    S.reserve(46);
    const Node *N = this;
    while (N) {
      std::reverse_copy(N->Name.begin(), N->Name.end(), std::back_inserter(S));
      N = N->Parent;
    }
    std::reverse(S.begin(), S.end());
    return S;
  }
};

static Node createRoot() {
  Node N;
  N.IsRoot = true;
  N.ChildrenOffset = 1;
  N.Size = 1;
  return N;
}

static Node readNode(uint32_t Offset, const Node *Parent = nullptr) {
  if (Offset == 0)
    return createRoot();

  uint32_t Origin = Offset;
  Node N;
  N.Parent = Parent;
  uint8_t NameInfo = UnicodeNameToCodepointIndex[Offset++];
  if (Offset + 6 >= UnicodeNameToCodepointIndexSize)
    return N;

  bool LongName = NameInfo & 0x40;
  bool HasValue = NameInfo & 0x80;
  std::size_t Size = NameInfo & ~0xC0;
  if (LongName) {
    uint32_t NameOffset = (UnicodeNameToCodepointIndex[Offset++] << 8);
    NameOffset |= UnicodeNameToCodepointIndex[Offset++];
    N.Name = StringRef(UnicodeNameToCodepointDict + NameOffset, Size);
  } else {
    N.Name = StringRef(UnicodeNameToCodepointDict + Size, 1);
  }
  if (HasValue) {
    uint8_t H = UnicodeNameToCodepointIndex[Offset++];
    uint8_t M = UnicodeNameToCodepointIndex[Offset++];
    uint8_t L = UnicodeNameToCodepointIndex[Offset++];
    N.Value = ((H << 16) | (M << 8) | L) >> 3;

    bool HasChildren = L & 0x02;
    N.HasSibling = L & 0x01;

    if (HasChildren) {
      N.ChildrenOffset = UnicodeNameToCodepointIndex[Offset++] << 16;
      N.ChildrenOffset |= UnicodeNameToCodepointIndex[Offset++] << 8;
      N.ChildrenOffset |= UnicodeNameToCodepointIndex[Offset++];
    }
  } else {
    uint8_t H = UnicodeNameToCodepointIndex[Offset++];
    N.HasSibling = H & 0x80;
    bool HasChildren = H & 0x40;
    H &= uint8_t(~0xC0);
    if (HasChildren) {
      N.ChildrenOffset = (H << 16);
      N.ChildrenOffset |=
          (uint32_t(UnicodeNameToCodepointIndex[Offset++]) << 8);
      N.ChildrenOffset |= UnicodeNameToCodepointIndex[Offset++];
    }
  }
  N.Size = Offset - Origin;
  return N;
}

static bool startsWith(StringRef Name, StringRef Needle, bool Strict,
                       std::size_t &Consummed, char &PreviousCharInName,
                       bool IsPrefix = false) {

  Consummed = 0;
  if (Strict) {
    if (!Name.starts_with(Needle))
      return false;
    Consummed = Needle.size();
    return true;
  }
  if (Needle.empty())
    return true;

  auto NamePos = Name.begin();
  auto NeedlePos = Needle.begin();

  char PreviousCharInNameOrigin = PreviousCharInName;
  char PreviousCharInNeedle = *Needle.begin();
  auto IgnoreSpaces = [](auto It, auto End, char &PreviousChar,
                         bool IsPrefix = false) {
    while (It != End) {
      const auto Next = std::next(It);
      // Ignore spaces, underscore, medial hyphens
      // The generator ensures a needle never ends (or starts) by a medial
      // hyphen https://unicode.org/reports/tr44/#UAX44-LM2.
      bool Ignore =
          *It == ' ' || *It == '_' ||
          (*It == '-' && isAlnum(PreviousChar) &&
           ((Next != End && isAlnum(*Next)) || (Next == End && IsPrefix)));
      PreviousChar = *It;
      if (!Ignore)
        break;
      ++It;
    }
    return It;
  };

  while (true) {
    NamePos = IgnoreSpaces(NamePos, Name.end(), PreviousCharInName);
    NeedlePos =
        IgnoreSpaces(NeedlePos, Needle.end(), PreviousCharInNeedle, IsPrefix);
    if (NeedlePos == Needle.end())
      break;
    if (NamePos == Name.end())
      break;
    if (toUpper(*NeedlePos) != toUpper(*NamePos))
      break;
    NeedlePos++;
    NamePos++;
  }
  Consummed = std::distance(Name.begin(), NamePos);
  if (NeedlePos != Needle.end()) {
    PreviousCharInName = PreviousCharInNameOrigin;
  }
  return NeedlePos == Needle.end();
}

static std::tuple<Node, bool, uint32_t>
compareNode(uint32_t Offset, StringRef Name, bool Strict,
            char PreviousCharInName, BufferType &Buffer,
            const Node *Parent = nullptr) {
  Node N = readNode(Offset, Parent);
  std::size_t Consummed = 0;
  bool DoesStartWith = N.IsRoot || startsWith(Name, N.Name, Strict, Consummed,
                                              PreviousCharInName);
  if (!DoesStartWith)
    return std::make_tuple(N, false, 0);

  if (Name.size() - Consummed == 0 && N.Value != 0xFFFFFFFF)
    return std::make_tuple(N, true, N.Value);

  if (N.hasChildren()) {
    uint32_t ChildOffset = N.ChildrenOffset;
    for (;;) {
      Node C;
      bool Matches;
      uint32_t Value;
      std::tie(C, Matches, Value) =
          compareNode(ChildOffset, Name.substr(Consummed), Strict,
                      PreviousCharInName, Buffer, &N);
      if (Matches) {
        std::reverse_copy(C.Name.begin(), C.Name.end(),
                          std::back_inserter(Buffer));
        return std::make_tuple(N, true, Value);
      }
      ChildOffset += C.Size;
      if (!C.HasSibling)
        break;
    }
  }
  return std::make_tuple(N, false, 0);
}

static std::tuple<Node, bool, uint32_t>
compareNode(uint32_t Offset, StringRef Name, bool Strict, BufferType &Buffer) {
  return compareNode(Offset, Name, Strict, 0, Buffer);
}

// clang-format off
constexpr const char *const HangulSyllables[][3] = {
    { "G",  "A",   ""   },
    { "GG", "AE",  "G"  },
    { "N",  "YA",  "GG" },
    { "D",  "YAE", "GS" },
    { "DD", "EO",  "N", },
    { "R",  "E",   "NJ" },
    { "M",  "YEO", "NH" },
    { "B",  "YE",  "D"  },
    { "BB", "O",   "L"  },
    { "S",  "WA",  "LG" },
    { "SS", "WAE", "LM" },
    { "",   "OE",  "LB" },
    { "J",  "YO",  "LS" },
    { "JJ", "U",   "LT" },
    { "C",  "WEO", "LP" },
    { "K",  "WE",  "LH" },
    { "T",  "WI",  "M"  },
    { "P",  "YU",  "B"  },
    { "H",  "EU",  "BS" },
    { 0,    "YI",  "S"  },
    { 0,    "I",   "SS" },
    { 0,    0,     "NG" },
    { 0,    0,     "J"  },
    { 0,    0,     "C"  },
    { 0,    0,     "K"  },
    { 0,    0,     "T"  },
    { 0,    0,     "P"  },
    { 0,    0,     "H"  }
    };
// clang-format on

// Unicode 15.0
// 3.12 Conjoining Jamo Behavior Common constants
constexpr const char32_t SBase = 0xAC00;
constexpr const uint32_t LCount = 19;
constexpr const uint32_t VCount = 21;
constexpr const uint32_t TCount = 28;

static std::size_t findSyllable(StringRef Name, bool Strict,
                                char &PreviousInName, int &Pos, int Column) {
  assert(Column == 0 || Column == 1 || Column == 2);
  static std::size_t CountPerColumn[] = {LCount, VCount, TCount};
  int Len = -1;
  int Prev = PreviousInName;
  for (std::size_t I = 0; I < CountPerColumn[Column]; I++) {
    StringRef Syllable(HangulSyllables[I][Column]);
    if (int(Syllable.size()) <= Len)
      continue;
    std::size_t Consummed = 0;
    char PreviousInNameCopy = PreviousInName;
    bool DoesStartWith =
        startsWith(Name, Syllable, Strict, Consummed, PreviousInNameCopy);
    if (!DoesStartWith)
      continue;
    Len = Consummed;
    Pos = I;
    Prev = PreviousInNameCopy;
  }
  if (Len == -1)
    return 0;
  PreviousInName = Prev;
  return size_t(Len);
}

static std::optional<char32_t>
nameToHangulCodePoint(StringRef Name, bool Strict, BufferType &Buffer) {
  Buffer.clear();
  // Hangul Syllable Decomposition
  std::size_t Consummed = 0;
  char NameStart = 0;
  bool DoesStartWith =
      startsWith(Name, "HANGUL SYLLABLE ", Strict, Consummed, NameStart);
  if (!DoesStartWith)
    return std::nullopt;
  Name = Name.substr(Consummed);
  int L = -1, V = -1, T = -1;
  Name = Name.substr(findSyllable(Name, Strict, NameStart, L, 0));
  Name = Name.substr(findSyllable(Name, Strict, NameStart, V, 1));
  Name = Name.substr(findSyllable(Name, Strict, NameStart, T, 2));
  if (L != -1 && V != -1 && T != -1 && Name.empty()) {
    if (!Strict) {
      Buffer.append("HANGUL SYLLABLE ");
      if (L != -1)
        Buffer.append(HangulSyllables[L][0]);
      if (V != -1)
        Buffer.append(HangulSyllables[V][1]);
      if (T != -1)
        Buffer.append(HangulSyllables[T][2]);
    }
    return SBase + (std::uint32_t(L) * VCount + std::uint32_t(V)) * TCount +
           std::uint32_t(T);
  }
  // Otherwise, it's an illegal syllable name.
  return std::nullopt;
}

struct GeneratedNamesData {
  StringRef Prefix;
  uint32_t Start;
  uint32_t End;
};

// Unicode 15.1 Table 4-8. Name Derivation Rule Prefix Strings
static const GeneratedNamesData GeneratedNamesDataTable[] = {
    {"CJK UNIFIED IDEOGRAPH-", 0x3400, 0x4DBF},
    {"CJK UNIFIED IDEOGRAPH-", 0x4E00, 0x9FFF},
    {"CJK UNIFIED IDEOGRAPH-", 0x20000, 0x2A6DF},
    {"CJK UNIFIED IDEOGRAPH-", 0x2A700, 0x2B739},
    {"CJK UNIFIED IDEOGRAPH-", 0x2B740, 0x2B81D},
    {"CJK UNIFIED IDEOGRAPH-", 0x2B820, 0x2CEA1},
    {"CJK UNIFIED IDEOGRAPH-", 0x2CEB0, 0x2EBE0},
    {"CJK UNIFIED IDEOGRAPH-", 0x2EBF0, 0x2EE5D},
    {"CJK UNIFIED IDEOGRAPH-", 0x30000, 0x3134A},
    {"CJK UNIFIED IDEOGRAPH-", 0x31350, 0x323AF},
    {"TANGUT IDEOGRAPH-", 0x17000, 0x187F7},
    {"TANGUT IDEOGRAPH-", 0x18D00, 0x18D08},
    {"KHITAN SMALL SCRIPT CHARACTER-", 0x18B00, 0x18CD5},
    {"NUSHU CHARACTER-", 0x1B170, 0x1B2FB},
    {"CJK COMPATIBILITY IDEOGRAPH-", 0xF900, 0xFA6D},
    {"CJK COMPATIBILITY IDEOGRAPH-", 0xFA70, 0xFAD9},
    {"CJK COMPATIBILITY IDEOGRAPH-", 0x2F800, 0x2FA1D},
};

static std::optional<char32_t>
nameToGeneratedCodePoint(StringRef Name, bool Strict, BufferType &Buffer) {
  for (auto &&Item : GeneratedNamesDataTable) {
    Buffer.clear();
    std::size_t Consummed = 0;
    char NameStart = 0;
    bool DoesStartWith = startsWith(Name, Item.Prefix, Strict, Consummed,
                                    NameStart, /*IsPrefix=*/true);
    if (!DoesStartWith)
      continue;
    auto Number = Name.substr(Consummed);
    unsigned long long V = 0;
    // Be consistent about mandating upper casing.
    if (Strict &&
        llvm::any_of(Number, [](char C) { return C >= 'a' && C <= 'f'; }))
      return {};
    if (getAsUnsignedInteger(Number, 16, V) || V < Item.Start || V > Item.End)
      continue;
    if (!Strict) {
      Buffer.append(Item.Prefix);
      Buffer.append(utohexstr(V, true));
    }
    return V;
  }
  return std::nullopt;
}

static std::optional<char32_t> nameToCodepoint(StringRef Name, bool Strict,
                                               BufferType &Buffer) {
  if (Name.empty())
    return std::nullopt;

  std::optional<char32_t> Res = nameToHangulCodePoint(Name, Strict, Buffer);
  if (!Res)
    Res = nameToGeneratedCodePoint(Name, Strict, Buffer);
  if (Res)
    return *Res;

  Buffer.clear();
  Node Node;
  bool Matches;
  uint32_t Value;
  std::tie(Node, Matches, Value) = compareNode(0, Name, Strict, Buffer);
  if (Matches) {
    std::reverse(Buffer.begin(), Buffer.end());
    // UAX44-LM2. Ignore case, whitespace, underscore ('_'), and all medial
    // hyphens except the hyphen in U+1180 HANGUL JUNGSEONG O-E.
    if (!Strict && Value == 0x116c && Name.contains_insensitive("O-E")) {
      Buffer = "HANGUL JUNGSEONG O-E";
      Value = 0x1180;
    }
    return Value;
  }
  return std::nullopt;
}

std::optional<char32_t> nameToCodepointStrict(StringRef Name) {

  BufferType Buffer;
  auto Opt = nameToCodepoint(Name, true, Buffer);
  return Opt;
}

std::optional<LooseMatchingResult>
nameToCodepointLooseMatching(StringRef Name) {
  BufferType Buffer;
  auto Opt = nameToCodepoint(Name, false, Buffer);
  if (!Opt)
    return std::nullopt;
  return LooseMatchingResult{*Opt, Buffer};
}

// Find the unicode character whose editing distance to Pattern
// is shortest, using the Wagner–Fischer algorithm.
llvm::SmallVector<MatchForCodepointName>
nearestMatchesForCodepointName(StringRef Pattern, std::size_t MaxMatchesCount) {
  // We maintain a fixed size vector of matches,
  // sorted by distance
  // The worst match (with the biggest distance) are discarded when new elements
  // are added.
  std::size_t LargestEditDistance = 0;
  llvm::SmallVector<MatchForCodepointName> Matches;
  Matches.reserve(MaxMatchesCount + 1);

  auto Insert = [&](const Node &Node, uint32_t Distance,
                    char32_t Value) -> bool {
    if (Distance > LargestEditDistance) {
      if (Matches.size() == MaxMatchesCount)
        return false;
      LargestEditDistance = Distance;
    }
    // To avoid allocations, the creation of the name is delayed
    // as much as possible.
    std::string Name;
    auto GetName = [&] {
      if (Name.empty())
        Name = Node.fullName();
      return Name;
    };

    auto It = llvm::lower_bound(
        Matches, Distance,
        [&](const MatchForCodepointName &a, std::size_t Distance) {
          if (Distance == a.Distance)
            return a.Name < GetName();
          return a.Distance < Distance;
        });
    if (It == Matches.end() && Matches.size() == MaxMatchesCount)
      return false;

    MatchForCodepointName M{GetName(), Distance, Value};
    Matches.insert(It, std::move(M));
    if (Matches.size() > MaxMatchesCount)
      Matches.pop_back();
    return true;
  };

  // We ignore case, space, hyphens, etc,
  // in both the search pattern and the prospective names.
  auto Normalize = [](StringRef Name) {
    std::string Out;
    Out.reserve(Name.size());
    for (char C : Name) {
      if (isAlnum(C))
        Out.push_back(toUpper(C));
    }
    return Out;
  };
  std::string NormalizedName = Normalize(Pattern);

  // Allocate a matrix big enough for longest names.
  const std::size_t Columns =
      std::min(NormalizedName.size(), UnicodeNameToCodepointLargestNameSize) +
      1;

  LLVM_ATTRIBUTE_UNUSED static std::size_t Rows =
      UnicodeNameToCodepointLargestNameSize + 1;

  std::vector<char> Distances(
      Columns * (UnicodeNameToCodepointLargestNameSize + 1), 0);

  auto Get = [&Distances, Columns](size_t Column, std::size_t Row) -> char & {
    assert(Column < Columns);
    assert(Row < Rows);
    return Distances[Row * Columns + Column];
  };

  for (std::size_t I = 0; I < Columns; I++)
    Get(I, 0) = I;

  // Visit the childrens,
  // Filling (and overriding) the matrix for the name fragment of each node
  // iteratively. CompleteName is used to collect the actual name of potential
  // match, respecting case and spacing.
  auto VisitNode = [&](const Node &N, std::size_t Row,
                       auto &VisitNode) -> void {
    std::size_t J = 0;
    for (; J < N.Name.size(); J++) {
      if (!isAlnum(N.Name[J]))
        continue;

      Get(0, Row) = Row;

      for (std::size_t I = 1; I < Columns; I++) {
        const int Delete = Get(I - 1, Row) + 1;
        const int Insert = Get(I, Row - 1) + 1;

        const int Replace =
            Get(I - 1, Row - 1) + (NormalizedName[I - 1] != N.Name[J] ? 1 : 0);

        Get(I, Row) = std::min(Insert, std::min(Delete, Replace));
      }

      Row++;
    }

    unsigned Cost = Get(Columns - 1, Row - 1);
    if (N.Value != 0xFFFFFFFF) {
      Insert(N, Cost, N.Value);
    }

    if (N.hasChildren()) {
      auto ChildOffset = N.ChildrenOffset;
      for (;;) {
        Node C = readNode(ChildOffset, &N);
        ChildOffset += C.Size;
        if (!C.isValid())
          break;
        VisitNode(C, Row, VisitNode);
        if (!C.HasSibling)
          break;
      }
    }
  };

  Node Root = createRoot();
  VisitNode(Root, 1, VisitNode);
  return Matches;
}

} // namespace unicode

} // namespace sys
} // namespace llvm
