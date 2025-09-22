//===-- LVSupport.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the supporting functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVSupport.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include <iomanip>

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Support"

namespace {
// Unique string pool instance used by all logical readers.
LVStringPool StringPool;
} // namespace
LVStringPool &llvm::logicalview::getStringPool() { return StringPool; }

// Perform the following transformations to the given 'Path':
// - all characters to lowercase.
// - '\\' into '/' (Platform independent).
// - '//' into '/'
std::string llvm::logicalview::transformPath(StringRef Path) {
  std::string Name(Path);
  std::transform(Name.begin(), Name.end(), Name.begin(), tolower);
  std::replace(Name.begin(), Name.end(), '\\', '/');

  // Remove all duplicate slashes.
  size_t Pos = 0;
  while ((Pos = Name.find("//", Pos)) != std::string::npos)
    Name.erase(Pos, 1);

  return Name;
}

// Convert the given 'Path' to lowercase and change any matching character
// from 'CharSet' into '_'.
// The characters in 'CharSet' are:
//   '/', '\', '<', '>', '.', ':', '%', '*', '?', '|', '"', ' '.
std::string llvm::logicalview::flattenedFilePath(StringRef Path) {
  std::string Name(Path);
  std::transform(Name.begin(), Name.end(), Name.begin(), tolower);

  const char *CharSet = "/\\<>.:%*?|\" ";
  char *Input = Name.data();
  while (Input && *Input) {
    Input = strpbrk(Input, CharSet);
    if (Input)
      *Input++ = '_';
  };
  return Name;
}

using LexicalEntry = std::pair<size_t, size_t>;
using LexicalIndexes = SmallVector<LexicalEntry, 10>;

static LexicalIndexes getAllLexicalIndexes(StringRef Name) {
  if (Name.empty())
    return {};

  size_t AngleCount = 0;
  size_t ColonSeen = 0;
  size_t Current = 0;

  LexicalIndexes Indexes;

#ifndef NDEBUG
  auto PrintLexicalEntry = [&]() {
    LexicalEntry Entry = Indexes.back();
    llvm::dbgs() << formatv(
        "'{0}:{1}', '{2}'\n", Entry.first, Entry.second,
        Name.substr(Entry.first, Entry.second - Entry.first + 1));
  };
#endif

  size_t Length = Name.size();
  for (size_t Index = 0; Index < Length; ++Index) {
    LLVM_DEBUG({
      llvm::dbgs() << formatv("Index: '{0}', Char: '{1}'\n", Index,
                              Name[Index]);
    });
    switch (Name[Index]) {
    case '<':
      ++AngleCount;
      break;
    case '>':
      --AngleCount;
      break;
    case ':':
      ++ColonSeen;
      break;
    }
    if (ColonSeen == 2) {
      if (!AngleCount) {
        Indexes.push_back(LexicalEntry(Current, Index - 2));
        Current = Index + 1;
        LLVM_DEBUG({ PrintLexicalEntry(); });
      }
      ColonSeen = 0;
      continue;
    }
  }

  // Store last component.
  Indexes.push_back(LexicalEntry(Current, Length - 1));
  LLVM_DEBUG({ PrintLexicalEntry(); });
  return Indexes;
}

LVLexicalComponent llvm::logicalview::getInnerComponent(StringRef Name) {
  if (Name.empty())
    return {};

  LexicalIndexes Indexes = getAllLexicalIndexes(Name);
  if (Indexes.size() == 1)
    return std::make_tuple(StringRef(), Name);

  LexicalEntry BeginEntry = Indexes.front();
  LexicalEntry EndEntry = Indexes[Indexes.size() - 2];
  StringRef Outer =
      Name.substr(BeginEntry.first, EndEntry.second - BeginEntry.first + 1);

  LexicalEntry LastEntry = Indexes.back();
  StringRef Inner =
      Name.substr(LastEntry.first, LastEntry.second - LastEntry.first + 1);

  return std::make_tuple(Outer, Inner);
}

LVStringRefs llvm::logicalview::getAllLexicalComponents(StringRef Name) {
  if (Name.empty())
    return {};

  LexicalIndexes Indexes = getAllLexicalIndexes(Name);
  LVStringRefs Components;
  for (const LexicalEntry &Entry : Indexes)
    Components.push_back(
        Name.substr(Entry.first, Entry.second - Entry.first + 1));

  return Components;
}

std::string llvm::logicalview::getScopedName(const LVStringRefs &Components,
                                             StringRef BaseName) {
  if (Components.empty())
    return {};
  std::string Name(BaseName);
  raw_string_ostream Stream(Name);
  if (BaseName.size())
    Stream << "::";
  Stream << Components[0];
  for (LVStringRefs::size_type Index = 1; Index < Components.size(); ++Index)
    Stream << "::" << Components[Index];
  return Name;
}
