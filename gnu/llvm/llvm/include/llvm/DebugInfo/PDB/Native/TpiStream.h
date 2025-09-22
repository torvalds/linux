//===- TpiStream.cpp - PDB Type Info (TPI) Stream 2 Access ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAM_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"

#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStream;
namespace codeview {
class TypeIndex;
struct TypeIndexOffset;
class LazyRandomTypeCollection;
}
namespace msf {
class MappedBlockStream;
}
namespace pdb {
struct TpiStreamHeader;
class PDBFile;

class TpiStream {
  friend class TpiStreamBuilder;

public:
  TpiStream(PDBFile &File, std::unique_ptr<msf::MappedBlockStream> Stream);
  ~TpiStream();
  Error reload();

  PdbRaw_TpiVer getTpiVersion() const;

  uint32_t TypeIndexBegin() const;
  uint32_t TypeIndexEnd() const;
  uint32_t getNumTypeRecords() const;
  uint16_t getTypeHashStreamIndex() const;
  uint16_t getTypeHashStreamAuxIndex() const;

  uint32_t getHashKeySize() const;
  uint32_t getNumHashBuckets() const;
  FixedStreamArray<support::ulittle32_t> getHashValues() const;
  FixedStreamArray<codeview::TypeIndexOffset> getTypeIndexOffsets() const;
  HashTable<support::ulittle32_t> &getHashAdjusters();

  codeview::CVTypeRange types(bool *HadError) const;
  const codeview::CVTypeArray &typeArray() const { return TypeRecords; }

  codeview::LazyRandomTypeCollection &typeCollection() { return *Types; }

  Expected<codeview::TypeIndex>
  findFullDeclForForwardRef(codeview::TypeIndex ForwardRefTI) const;

  std::vector<codeview::TypeIndex> findRecordsByName(StringRef Name) const;

  codeview::CVType getType(codeview::TypeIndex Index);

  BinarySubstreamRef getTypeRecordsSubstream() const;

  Error commit();

  void buildHashMap();

  bool supportsTypeLookup() const;

private:
  PDBFile &Pdb;
  std::unique_ptr<msf::MappedBlockStream> Stream;

  std::unique_ptr<codeview::LazyRandomTypeCollection> Types;

  BinarySubstreamRef TypeRecordsSubstream;

  codeview::CVTypeArray TypeRecords;

  std::unique_ptr<BinaryStream> HashStream;
  FixedStreamArray<support::ulittle32_t> HashValues;
  FixedStreamArray<codeview::TypeIndexOffset> TypeIndexOffsets;
  HashTable<support::ulittle32_t> HashAdjusters;

  std::vector<std::vector<codeview::TypeIndex>> HashMap;

  const TpiStreamHeader *Header;
};
}
}

#endif
