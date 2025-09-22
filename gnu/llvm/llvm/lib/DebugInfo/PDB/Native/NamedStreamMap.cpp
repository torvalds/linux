//===- NamedStreamMap.cpp - PDB Named Stream Map --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace llvm::pdb;

NamedStreamMapTraits::NamedStreamMapTraits(NamedStreamMap &NS) : NS(&NS) {}

uint16_t NamedStreamMapTraits::hashLookupKey(StringRef S) const {
  // In the reference implementation, this uses
  // HASH Hasher<ULONG*, USHORT*>::hashPbCb(PB pb, size_t cb, ULONG ulMod).
  // Here, the type HASH is a typedef of unsigned short.
  // ** It is not a bug that we truncate the result of hashStringV1, in fact
  //    it is a bug if we do not! **
  // See NMTNI::hash() in the reference implementation.
  return static_cast<uint16_t>(hashStringV1(S));
}

StringRef NamedStreamMapTraits::storageKeyToLookupKey(uint32_t Offset) const {
  return NS->getString(Offset);
}

uint32_t NamedStreamMapTraits::lookupKeyToStorageKey(StringRef S) {
  return NS->appendStringData(S);
}

NamedStreamMap::NamedStreamMap() : HashTraits(*this), OffsetIndexMap(1) {}

Error NamedStreamMap::load(BinaryStreamReader &Stream) {
  uint32_t StringBufferSize;
  if (auto EC = Stream.readInteger(StringBufferSize))
    return joinErrors(std::move(EC),
                      make_error<RawError>(raw_error_code::corrupt_file,
                                           "Expected string buffer size"));

  StringRef Buffer;
  if (auto EC = Stream.readFixedString(Buffer, StringBufferSize))
    return EC;
  NamesBuffer.assign(Buffer.begin(), Buffer.end());

  return OffsetIndexMap.load(Stream);
}

Error NamedStreamMap::commit(BinaryStreamWriter &Writer) const {
  // The first field is the number of bytes of string data.
  if (auto EC = Writer.writeInteger<uint32_t>(NamesBuffer.size()))
    return EC;

  // Then the actual string data.
  StringRef Data(NamesBuffer.data(), NamesBuffer.size());
  if (auto EC = Writer.writeFixedString(Data))
    return EC;

  // And finally the Offset Index map.
  if (auto EC = OffsetIndexMap.commit(Writer))
    return EC;

  return Error::success();
}

uint32_t NamedStreamMap::calculateSerializedLength() const {
  return sizeof(uint32_t)                              // String data size
         + NamesBuffer.size()                          // String data
         + OffsetIndexMap.calculateSerializedLength(); // Offset Index Map
}

uint32_t NamedStreamMap::size() const { return OffsetIndexMap.size(); }

StringRef NamedStreamMap::getString(uint32_t Offset) const {
  assert(NamesBuffer.size() > Offset);
  return StringRef(NamesBuffer.data() + Offset);
}

uint32_t NamedStreamMap::hashString(uint32_t Offset) const {
  return hashStringV1(getString(Offset));
}

bool NamedStreamMap::get(StringRef Stream, uint32_t &StreamNo) const {
  auto Iter = OffsetIndexMap.find_as(Stream, HashTraits);
  if (Iter == OffsetIndexMap.end())
    return false;
  StreamNo = (*Iter).second;
  return true;
}

StringMap<uint32_t> NamedStreamMap::entries() const {
  StringMap<uint32_t> Result;
  for (const auto &Entry : OffsetIndexMap) {
    StringRef Stream(NamesBuffer.data() + Entry.first);
    Result.try_emplace(Stream, Entry.second);
  }
  return Result;
}

uint32_t NamedStreamMap::appendStringData(StringRef S) {
  uint32_t Offset = NamesBuffer.size();
  llvm::append_range(NamesBuffer, S);
  NamesBuffer.push_back('\0');
  return Offset;
}

void NamedStreamMap::set(StringRef Stream, uint32_t StreamNo) {
  OffsetIndexMap.set_as(Stream, support::ulittle32_t(StreamNo), HashTraits);
}
