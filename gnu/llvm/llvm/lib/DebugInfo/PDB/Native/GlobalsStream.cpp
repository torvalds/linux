//===- GlobalsStream.cpp - PDB Index of Symbols by Name ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The on-disk structores used in this file are based on the reference
// implementation which is available at
// https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/dbi/gsi.h
//
// When you are reading the reference source code, you'd find the
// information below useful.
//
//  - ppdb1->m_fMinimalDbgInfo seems to be always true.
//  - SMALLBUCKETS macro is defined.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"

#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Error.h"
#include <algorithm>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::pdb;

GlobalsStream::GlobalsStream(std::unique_ptr<MappedBlockStream> Stream)
    : Stream(std::move(Stream)) {}

GlobalsStream::~GlobalsStream() = default;

Error GlobalsStream::reload() {
  BinaryStreamReader Reader(*Stream);
  if (auto E = GlobalsTable.read(Reader))
    return E;
  return Error::success();
}

std::vector<std::pair<uint32_t, codeview::CVSymbol>>
GlobalsStream::findRecordsByName(StringRef Name,
                                 const SymbolStream &Symbols) const {
  std::vector<std::pair<uint32_t, codeview::CVSymbol>> Result;

  // Hash the name to figure out which bucket this goes into.
  size_t ExpandedBucketIndex = hashStringV1(Name) % IPHR_HASH;
  int32_t CompressedBucketIndex = GlobalsTable.BucketMap[ExpandedBucketIndex];
  if (CompressedBucketIndex == -1)
    return Result;

  uint32_t LastBucketIndex = GlobalsTable.HashBuckets.size() - 1;
  uint32_t StartRecordIndex =
      GlobalsTable.HashBuckets[CompressedBucketIndex] / 12;
  uint32_t EndRecordIndex = 0;
  if (LLVM_LIKELY(uint32_t(CompressedBucketIndex) < LastBucketIndex)) {
    EndRecordIndex = GlobalsTable.HashBuckets[CompressedBucketIndex + 1];
  } else {
    // If this is the last bucket, it consists of all hash records until the end
    // of the HashRecords array.
    EndRecordIndex = GlobalsTable.HashRecords.size() * 12;
  }

  EndRecordIndex /= 12;

  assert(EndRecordIndex <= GlobalsTable.HashRecords.size());
  while (StartRecordIndex < EndRecordIndex) {
    PSHashRecord PSH = GlobalsTable.HashRecords[StartRecordIndex];
    uint32_t Off = PSH.Off - 1;
    codeview::CVSymbol Record = Symbols.readRecord(Off);
    if (codeview::getSymbolName(Record) == Name)
      Result.push_back(std::make_pair(Off, std::move(Record)));
    ++StartRecordIndex;
  }
  return Result;
}

static Error checkHashHdrVersion(const GSIHashHeader *HashHdr) {
  if (HashHdr->VerHdr != GSIHashHeader::HdrVersion)
    return make_error<RawError>(
        raw_error_code::feature_unsupported,
        "Encountered unsupported globals stream version.");

  return Error::success();
}

static Error readGSIHashHeader(const GSIHashHeader *&HashHdr,
                               BinaryStreamReader &Reader) {
  if (Reader.readObject(HashHdr))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Stream does not contain a GSIHashHeader.");

  if (HashHdr->VerSignature != GSIHashHeader::HdrSignature)
    return make_error<RawError>(
        raw_error_code::feature_unsupported,
        "GSIHashHeader signature (0xffffffff) not found.");

  return Error::success();
}

static Error readGSIHashRecords(FixedStreamArray<PSHashRecord> &HashRecords,
                                const GSIHashHeader *HashHdr,
                                BinaryStreamReader &Reader) {
  if (auto EC = checkHashHdrVersion(HashHdr))
    return EC;

  // HashHdr->HrSize specifies the number of bytes of PSHashRecords we have.
  // Verify that we can read them all.
  if (HashHdr->HrSize % sizeof(PSHashRecord))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Invalid HR array size.");
  uint32_t NumHashRecords = HashHdr->HrSize / sizeof(PSHashRecord);
  if (auto EC = Reader.readArray(HashRecords, NumHashRecords))
    return joinErrors(std::move(EC),
                      make_error<RawError>(raw_error_code::corrupt_file,
                                           "Error reading hash records."));

  return Error::success();
}

static Error
readGSIHashBuckets(FixedStreamArray<support::ulittle32_t> &HashBuckets,
                   FixedStreamArray<support::ulittle32_t> &HashBitmap,
                   const GSIHashHeader *HashHdr,
                   MutableArrayRef<int32_t> BucketMap,
                   BinaryStreamReader &Reader) {
  if (auto EC = checkHashHdrVersion(HashHdr))
    return EC;

  // Before the actual hash buckets, there is a bitmap of length determined by
  // IPHR_HASH.
  size_t BitmapSizeInBits = alignTo(IPHR_HASH + 1, 32);
  uint32_t NumBitmapEntries = BitmapSizeInBits / 32;
  if (auto EC = Reader.readArray(HashBitmap, NumBitmapEntries))
    return joinErrors(std::move(EC),
                      make_error<RawError>(raw_error_code::corrupt_file,
                                           "Could not read a bitmap."));
  uint32_t CompressedBucketIdx = 0;
  for (uint32_t I = 0; I <= IPHR_HASH; ++I) {
    uint8_t WordIdx = I / 32;
    uint8_t BitIdx = I % 32;
    bool IsSet = HashBitmap[WordIdx] & (1U << BitIdx);
    if (IsSet) {
      BucketMap[I] = CompressedBucketIdx++;
    } else {
      BucketMap[I] = -1;
    }
  }

  uint32_t NumBuckets = 0;
  for (uint32_t B : HashBitmap)
    NumBuckets += llvm::popcount(B);

  // Hash buckets follow.
  if (auto EC = Reader.readArray(HashBuckets, NumBuckets))
    return joinErrors(std::move(EC),
                      make_error<RawError>(raw_error_code::corrupt_file,
                                           "Hash buckets corrupted."));

  return Error::success();
}

Error GSIHashTable::read(BinaryStreamReader &Reader) {
  if (auto EC = readGSIHashHeader(HashHdr, Reader))
    return EC;
  if (auto EC = readGSIHashRecords(HashRecords, HashHdr, Reader))
    return EC;
  if (HashHdr->HrSize > 0)
    if (auto EC = readGSIHashBuckets(HashBuckets, HashBitmap, HashHdr,
                                     BucketMap, Reader))
      return EC;
  return Error::success();
}
