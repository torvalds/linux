//===- TpiStream.cpp - PDB Type Info (TPI) Stream 2 Access ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/TpiStream.h"

#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <vector>

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::support;
using namespace llvm::msf;
using namespace llvm::pdb;

TpiStream::TpiStream(PDBFile &File, std::unique_ptr<MappedBlockStream> Stream)
    : Pdb(File), Stream(std::move(Stream)) {}

TpiStream::~TpiStream() = default;

Error TpiStream::reload() {
  BinaryStreamReader Reader(*Stream);

  if (Reader.bytesRemaining() < sizeof(TpiStreamHeader))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "TPI Stream does not contain a header.");

  if (Reader.readObject(Header))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "TPI Stream does not contain a header.");

  if (Header->Version != PdbTpiV80)
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Unsupported TPI Version.");

  if (Header->HeaderSize != sizeof(TpiStreamHeader))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Corrupt TPI Header size.");

  if (Header->HashKeySize != sizeof(ulittle32_t))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "TPI Stream expected 4 byte hash key size.");

  if (Header->NumHashBuckets < MinTpiHashBuckets ||
      Header->NumHashBuckets > MaxTpiHashBuckets)
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "TPI Stream Invalid number of hash buckets.");

  // The actual type records themselves come from this stream
  if (auto EC =
          Reader.readSubstream(TypeRecordsSubstream, Header->TypeRecordBytes))
    return EC;

  BinaryStreamReader RecordReader(TypeRecordsSubstream.StreamData);
  if (auto EC =
          RecordReader.readArray(TypeRecords, TypeRecordsSubstream.size()))
    return EC;

  // Hash indices, hash values, etc come from the hash stream.
  if (Header->HashStreamIndex != kInvalidStreamIndex) {
    auto HS = Pdb.safelyCreateIndexedStream(Header->HashStreamIndex);
    if (!HS) {
      consumeError(HS.takeError());
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Invalid TPI hash stream index.");
    }
    BinaryStreamReader HSR(**HS);

    // There should be a hash value for every type record, or no hashes at all.
    uint32_t NumHashValues =
        Header->HashValueBuffer.Length / sizeof(ulittle32_t);
    if (NumHashValues != getNumTypeRecords() && NumHashValues != 0)
      return make_error<RawError>(
          raw_error_code::corrupt_file,
          "TPI hash count does not match with the number of type records.");
    HSR.setOffset(Header->HashValueBuffer.Off);
    if (auto EC = HSR.readArray(HashValues, NumHashValues))
      return EC;

    HSR.setOffset(Header->IndexOffsetBuffer.Off);
    uint32_t NumTypeIndexOffsets =
        Header->IndexOffsetBuffer.Length / sizeof(TypeIndexOffset);
    if (auto EC = HSR.readArray(TypeIndexOffsets, NumTypeIndexOffsets))
      return EC;

    if (Header->HashAdjBuffer.Length > 0) {
      HSR.setOffset(Header->HashAdjBuffer.Off);
      if (auto EC = HashAdjusters.load(HSR))
        return EC;
    }

    HashStream = std::move(*HS);
  }

  Types = std::make_unique<LazyRandomTypeCollection>(
      TypeRecords, getNumTypeRecords(), getTypeIndexOffsets());
  return Error::success();
}

PdbRaw_TpiVer TpiStream::getTpiVersion() const {
  uint32_t Value = Header->Version;
  return static_cast<PdbRaw_TpiVer>(Value);
}

uint32_t TpiStream::TypeIndexBegin() const { return Header->TypeIndexBegin; }

uint32_t TpiStream::TypeIndexEnd() const { return Header->TypeIndexEnd; }

uint32_t TpiStream::getNumTypeRecords() const {
  return TypeIndexEnd() - TypeIndexBegin();
}

uint16_t TpiStream::getTypeHashStreamIndex() const {
  return Header->HashStreamIndex;
}

uint16_t TpiStream::getTypeHashStreamAuxIndex() const {
  return Header->HashAuxStreamIndex;
}

uint32_t TpiStream::getNumHashBuckets() const { return Header->NumHashBuckets; }
uint32_t TpiStream::getHashKeySize() const { return Header->HashKeySize; }

void TpiStream::buildHashMap() {
  if (!HashMap.empty())
    return;
  if (HashValues.empty())
    return;

  HashMap.resize(Header->NumHashBuckets);

  TypeIndex TIB{Header->TypeIndexBegin};
  TypeIndex TIE{Header->TypeIndexEnd};
  while (TIB < TIE) {
    uint32_t HV = HashValues[TIB.toArrayIndex()];
    HashMap[HV].push_back(TIB++);
  }
}

std::vector<TypeIndex> TpiStream::findRecordsByName(StringRef Name) const {
  if (!supportsTypeLookup())
    const_cast<TpiStream*>(this)->buildHashMap();

  uint32_t Bucket = hashStringV1(Name) % Header->NumHashBuckets;
  if (Bucket > HashMap.size())
    return {};

  std::vector<TypeIndex> Result;
  for (TypeIndex TI : HashMap[Bucket]) {
    std::string ThisName = computeTypeName(*Types, TI);
    if (ThisName == Name)
      Result.push_back(TI);
  }
  return Result;
}

bool TpiStream::supportsTypeLookup() const { return !HashMap.empty(); }

Expected<TypeIndex>
TpiStream::findFullDeclForForwardRef(TypeIndex ForwardRefTI) const {
  if (!supportsTypeLookup())
    const_cast<TpiStream*>(this)->buildHashMap();

  CVType F = Types->getType(ForwardRefTI);
  if (!isUdtForwardRef(F))
    return ForwardRefTI;

  Expected<TagRecordHash> ForwardTRH = hashTagRecord(F);
  if (!ForwardTRH)
    return ForwardTRH.takeError();

  uint32_t BucketIdx = ForwardTRH->FullRecordHash % Header->NumHashBuckets;

  for (TypeIndex TI : HashMap[BucketIdx]) {
    CVType CVT = Types->getType(TI);
    if (CVT.kind() != F.kind())
      continue;

    Expected<TagRecordHash> FullTRH = hashTagRecord(CVT);
    if (!FullTRH)
      return FullTRH.takeError();
    if (ForwardTRH->FullRecordHash != FullTRH->FullRecordHash)
      continue;
    TagRecord &ForwardTR = ForwardTRH->getRecord();
    TagRecord &FullTR = FullTRH->getRecord();

    if (!ForwardTR.hasUniqueName()) {
      if (ForwardTR.getName() == FullTR.getName())
        return TI;
      continue;
    }

    if (!FullTR.hasUniqueName())
      continue;
    if (ForwardTR.getUniqueName() == FullTR.getUniqueName())
      return TI;
  }
  return ForwardRefTI;
}

codeview::CVType TpiStream::getType(codeview::TypeIndex Index) {
  assert(!Index.isSimple());
  return Types->getType(Index);
}

BinarySubstreamRef TpiStream::getTypeRecordsSubstream() const {
  return TypeRecordsSubstream;
}

FixedStreamArray<support::ulittle32_t> TpiStream::getHashValues() const {
  return HashValues;
}

FixedStreamArray<TypeIndexOffset> TpiStream::getTypeIndexOffsets() const {
  return TypeIndexOffsets;
}

HashTable<support::ulittle32_t> &TpiStream::getHashAdjusters() {
  return HashAdjusters;
}

CVTypeRange TpiStream::types(bool *HadError) const {
  return make_range(TypeRecords.begin(HadError), TypeRecords.end());
}

Error TpiStream::commit() { return Error::success(); }
