//===- DebugStringTableSubsection.cpp - CodeView String Table -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace llvm::codeview;

DebugStringTableSubsectionRef::DebugStringTableSubsectionRef()
    : DebugSubsectionRef(DebugSubsectionKind::StringTable) {}

Error DebugStringTableSubsectionRef::initialize(BinaryStreamRef Contents) {
  Stream = Contents;
  return Error::success();
}

Error DebugStringTableSubsectionRef::initialize(BinaryStreamReader &Reader) {
  return Reader.readStreamRef(Stream);
}

Expected<StringRef>
DebugStringTableSubsectionRef::getString(uint32_t Offset) const {
  BinaryStreamReader Reader(Stream);
  Reader.setOffset(Offset);
  StringRef Result;
  if (auto EC = Reader.readCString(Result))
    return std::move(EC);
  return Result;
}

DebugStringTableSubsection::DebugStringTableSubsection()
    : DebugSubsection(DebugSubsectionKind::StringTable) {}

uint32_t DebugStringTableSubsection::insert(StringRef S) {
  auto P = StringToId.insert({S, StringSize});

  // If a given string didn't exist in the string table, we want to increment
  // the string table size and insert it into the reverse lookup.
  if (P.second) {
    IdToString.insert({P.first->getValue(), P.first->getKey()});
    StringSize += S.size() + 1; // +1 for '\0'
  }

  return P.first->second;
}

uint32_t DebugStringTableSubsection::calculateSerializedSize() const {
  return StringSize;
}

Error DebugStringTableSubsection::commit(BinaryStreamWriter &Writer) const {
  uint32_t Begin = Writer.getOffset();
  uint32_t End = Begin + StringSize;

  // Write a null string at the beginning.
  if (auto EC = Writer.writeCString(StringRef()))
    return EC;

  for (auto &Pair : StringToId) {
    StringRef S = Pair.getKey();
    uint32_t Offset = Begin + Pair.getValue();
    Writer.setOffset(Offset);
    if (auto EC = Writer.writeCString(S))
      return EC;
    assert(Writer.getOffset() <= End);
  }

  Writer.setOffset(End);
  assert((End - Begin) == StringSize);
  return Error::success();
}

uint32_t DebugStringTableSubsection::size() const { return StringToId.size(); }

std::vector<uint32_t> DebugStringTableSubsection::sortedIds() const {
  std::vector<uint32_t> Result;
  Result.reserve(IdToString.size());
  for (const auto &Entry : IdToString)
    Result.push_back(Entry.first);
  llvm::sort(Result);
  return Result;
}

uint32_t DebugStringTableSubsection::getIdForString(StringRef S) const {
  auto Iter = StringToId.find(S);
  assert(Iter != StringToId.end());
  return Iter->second;
}

StringRef DebugStringTableSubsection::getStringForId(uint32_t Id) const {
  auto Iter = IdToString.find(Id);
  assert(Iter != IdToString.end());
  return Iter->second;
}
