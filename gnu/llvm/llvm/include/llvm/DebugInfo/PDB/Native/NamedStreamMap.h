//===- NamedStreamMap.h - PDB Named Stream Map ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NAMEDSTREAMMAP_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NAMEDSTREAMMAP_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

class BinaryStreamReader;
class BinaryStreamWriter;

namespace pdb {

class NamedStreamMap;

struct NamedStreamMapTraits {
  NamedStreamMap *NS;

  explicit NamedStreamMapTraits(NamedStreamMap &NS);
  uint16_t hashLookupKey(StringRef S) const;
  StringRef storageKeyToLookupKey(uint32_t Offset) const;
  uint32_t lookupKeyToStorageKey(StringRef S);
};

class NamedStreamMap {
  friend class NamedStreamMapBuilder;

public:
  NamedStreamMap();

  Error load(BinaryStreamReader &Stream);
  Error commit(BinaryStreamWriter &Writer) const;
  uint32_t calculateSerializedLength() const;

  uint32_t size() const;
  bool get(StringRef Stream, uint32_t &StreamNo) const;
  void set(StringRef Stream, uint32_t StreamNo);

  uint32_t appendStringData(StringRef S);
  StringRef getString(uint32_t Offset) const;
  uint32_t hashString(uint32_t Offset) const;

  StringMap<uint32_t> entries() const;

private:
  NamedStreamMapTraits HashTraits;
  /// Closed hash table from Offset -> StreamNumber, where Offset is the offset
  /// of the stream name in NamesBuffer.
  HashTable<support::ulittle32_t> OffsetIndexMap;

  /// Buffer of string data.
  std::vector<char> NamesBuffer;
};

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NAMEDSTREAMMAP_H
