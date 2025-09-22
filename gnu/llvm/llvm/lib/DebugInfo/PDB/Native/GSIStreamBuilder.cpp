//===- DbiStreamBuilder.cpp - PDB Dbi Stream Creation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The data structures defined in this file are based on the reference
// implementation which is available at
// https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/dbi/gsi.cpp
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolSerializer.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryItemStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <vector>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::pdb;
using namespace llvm::codeview;

// Helper class for building the public and global PDB hash table buckets.
struct llvm::pdb::GSIHashStreamBuilder {
  // Sum of the size of all public or global records.
  uint32_t RecordByteSize = 0;

  std::vector<PSHashRecord> HashRecords;

  // The hash bitmap has `ceil((IPHR_HASH + 1) / 32)` words in it. The
  // reference implementation builds a hash table with IPHR_HASH buckets in it.
  // The last bucket is used to link together free hash table cells in a linked
  // list, but it is always empty in the compressed, on-disk format. However,
  // the bitmap must have a bit for it.
  std::array<support::ulittle32_t, (IPHR_HASH + 32) / 32> HashBitmap;

  std::vector<support::ulittle32_t> HashBuckets;

  uint32_t calculateSerializedLength() const;
  Error commit(BinaryStreamWriter &Writer);

  void finalizePublicBuckets();
  void finalizeGlobalBuckets(uint32_t RecordZeroOffset);

  // Assign public and global symbol records into hash table buckets.
  // Modifies the list of records to store the bucket index, but does not
  // change the order.
  void finalizeBuckets(uint32_t RecordZeroOffset,
                       MutableArrayRef<BulkPublic> Globals);
};

// DenseMapInfo implementation for deduplicating symbol records.
struct llvm::pdb::SymbolDenseMapInfo {
  static inline CVSymbol getEmptyKey() {
    static CVSymbol Empty;
    return Empty;
  }
  static inline CVSymbol getTombstoneKey() {
    static CVSymbol Tombstone(
        DenseMapInfo<ArrayRef<uint8_t>>::getTombstoneKey());
    return Tombstone;
  }
  static unsigned getHashValue(const CVSymbol &Val) {
    return xxh3_64bits(Val.RecordData);
  }
  static bool isEqual(const CVSymbol &LHS, const CVSymbol &RHS) {
    return LHS.RecordData == RHS.RecordData;
  }
};

namespace {
LLVM_PACKED_START
struct PublicSym32Layout {
  RecordPrefix Prefix;
  PublicSym32Header Pub;
  // char Name[];
};
LLVM_PACKED_END
} // namespace

// Calculate how much memory this public needs when serialized.
static uint32_t sizeOfPublic(const BulkPublic &Pub) {
  uint32_t NameLen = Pub.NameLen;
  NameLen = std::min(NameLen,
                     uint32_t(MaxRecordLength - sizeof(PublicSym32Layout) - 1));
  return alignTo(sizeof(PublicSym32Layout) + NameLen + 1, 4);
}

static CVSymbol serializePublic(uint8_t *Mem, const BulkPublic &Pub) {
  // Assume the caller has allocated sizeOfPublic bytes.
  uint32_t NameLen = std::min(
      Pub.NameLen, uint32_t(MaxRecordLength - sizeof(PublicSym32Layout) - 1));
  size_t Size = alignTo(sizeof(PublicSym32Layout) + NameLen + 1, 4);
  assert(Size == sizeOfPublic(Pub));
  auto *FixedMem = reinterpret_cast<PublicSym32Layout *>(Mem);
  FixedMem->Prefix.RecordKind = static_cast<uint16_t>(codeview::S_PUB32);
  FixedMem->Prefix.RecordLen = static_cast<uint16_t>(Size - 2);
  FixedMem->Pub.Flags = Pub.Flags;
  FixedMem->Pub.Offset = Pub.Offset;
  FixedMem->Pub.Segment = Pub.Segment;
  char *NameMem = reinterpret_cast<char *>(FixedMem + 1);
  memcpy(NameMem, Pub.Name, NameLen);
  // Zero the null terminator and remaining bytes.
  memset(&NameMem[NameLen], 0, Size - sizeof(PublicSym32Layout) - NameLen);
  return CVSymbol(ArrayRef(reinterpret_cast<uint8_t *>(Mem), Size));
}

uint32_t GSIHashStreamBuilder::calculateSerializedLength() const {
  uint32_t Size = sizeof(GSIHashHeader);
  Size += HashRecords.size() * sizeof(PSHashRecord);
  Size += HashBitmap.size() * sizeof(uint32_t);
  Size += HashBuckets.size() * sizeof(uint32_t);
  return Size;
}

Error GSIHashStreamBuilder::commit(BinaryStreamWriter &Writer) {
  GSIHashHeader Header;
  Header.VerSignature = GSIHashHeader::HdrSignature;
  Header.VerHdr = GSIHashHeader::HdrVersion;
  Header.HrSize = HashRecords.size() * sizeof(PSHashRecord);
  Header.NumBuckets = HashBitmap.size() * 4 + HashBuckets.size() * 4;

  if (auto EC = Writer.writeObject(Header))
    return EC;

  if (auto EC = Writer.writeArray(ArrayRef(HashRecords)))
    return EC;
  if (auto EC = Writer.writeArray(ArrayRef(HashBitmap)))
    return EC;
  if (auto EC = Writer.writeArray(ArrayRef(HashBuckets)))
    return EC;
  return Error::success();
}

static bool isAsciiString(StringRef S) {
  return llvm::all_of(S, [](char C) { return unsigned(C) < 0x80; });
}

// See `caseInsensitiveComparePchPchCchCch` in gsi.cpp
static int gsiRecordCmp(StringRef S1, StringRef S2) {
  size_t LS = S1.size();
  size_t RS = S2.size();
  // Shorter strings always compare less than longer strings.
  if (LS != RS)
    return (LS > RS) - (LS < RS);

  // If either string contains non ascii characters, memcmp them.
  if (LLVM_UNLIKELY(!isAsciiString(S1) || !isAsciiString(S2)))
    return memcmp(S1.data(), S2.data(), LS);

  // Both strings are ascii, perform a case-insensitive comparison.
  return S1.compare_insensitive(S2.data());
}

void GSIStreamBuilder::finalizePublicBuckets() {
  PSH->finalizeBuckets(0, Publics);
}

void GSIStreamBuilder::finalizeGlobalBuckets(uint32_t RecordZeroOffset) {
  // Build up a list of globals to be bucketed. Use the BulkPublic data
  // structure for this purpose, even though these are global records, not
  // public records. Most of the same fields are required:
  // - Name
  // - NameLen
  // - SymOffset
  // - BucketIdx
  // The dead fields are Offset, Segment, and Flags.
  std::vector<BulkPublic> Records;
  Records.resize(Globals.size());
  uint32_t SymOffset = RecordZeroOffset;
  for (size_t I = 0, E = Globals.size(); I < E; ++I) {
    StringRef Name = getSymbolName(Globals[I]);
    Records[I].Name = Name.data();
    Records[I].NameLen = Name.size();
    Records[I].SymOffset = SymOffset;
    SymOffset += Globals[I].length();
  }

  GSH->finalizeBuckets(RecordZeroOffset, Records);
}

void GSIHashStreamBuilder::finalizeBuckets(
    uint32_t RecordZeroOffset, MutableArrayRef<BulkPublic> Records) {
  // Hash every name in parallel.
  parallelFor(0, Records.size(), [&](size_t I) {
    Records[I].setBucketIdx(hashStringV1(Records[I].Name) % IPHR_HASH);
  });

  // Count up the size of each bucket. Then, use an exclusive prefix sum to
  // calculate the bucket start offsets. This is C++17 std::exclusive_scan, but
  // we can't use it yet.
  uint32_t BucketStarts[IPHR_HASH] = {0};
  for (const BulkPublic &P : Records)
    ++BucketStarts[P.BucketIdx];
  uint32_t Sum = 0;
  for (uint32_t &B : BucketStarts) {
    uint32_t Size = B;
    B = Sum;
    Sum += Size;
  }

  // Place globals into the hash table in bucket order. When placing a global,
  // update the bucket start. Every hash table slot should be filled. Always use
  // a refcount of one for now.
  HashRecords.resize(Records.size());
  uint32_t BucketCursors[IPHR_HASH];
  memcpy(BucketCursors, BucketStarts, sizeof(BucketCursors));
  for (int I = 0, E = Records.size(); I < E; ++I) {
    uint32_t HashIdx = BucketCursors[Records[I].BucketIdx]++;
    HashRecords[HashIdx].Off = I;
    HashRecords[HashIdx].CRef = 1;
  }

  // Within the buckets, sort each bucket by memcmp of the symbol's name.  It's
  // important that we use the same sorting algorithm as is used by the
  // reference implementation to ensure that the search for a record within a
  // bucket can properly early-out when it detects the record won't be found.
  // The algorithm used here corresponds to the function
  // caseInsensitiveComparePchPchCchCch in the reference implementation.
  parallelFor(0, IPHR_HASH, [&](size_t I) {
    auto B = HashRecords.begin() + BucketStarts[I];
    auto E = HashRecords.begin() + BucketCursors[I];
    if (B == E)
      return;
    auto BucketCmp = [Records](const PSHashRecord &LHash,
                               const PSHashRecord &RHash) {
      const BulkPublic &L = Records[uint32_t(LHash.Off)];
      const BulkPublic &R = Records[uint32_t(RHash.Off)];
      assert(L.BucketIdx == R.BucketIdx);
      int Cmp = gsiRecordCmp(L.getName(), R.getName());
      if (Cmp != 0)
        return Cmp < 0;
      // This comparison is necessary to make the sorting stable in the presence
      // of two static globals with the same name. The easiest way to observe
      // this is with S_LDATA32 records.
      return L.SymOffset < R.SymOffset;
    };
    llvm::sort(B, E, BucketCmp);

    // After we are done sorting, replace the global indices with the stream
    // offsets of each global. Add one when writing symbol offsets to disk.
    // See GSI1::fixSymRecs.
    for (PSHashRecord &HRec : make_range(B, E))
      HRec.Off = Records[uint32_t(HRec.Off)].SymOffset + 1;
  });

  // For each non-empty bucket, push the bucket start offset into HashBuckets
  // and set a bit in the hash bitmap.
  for (uint32_t I = 0; I < HashBitmap.size(); ++I) {
    uint32_t Word = 0;
    for (uint32_t J = 0; J < 32; ++J) {
      // Skip empty buckets.
      uint32_t BucketIdx = I * 32 + J;
      if (BucketIdx >= IPHR_HASH ||
          BucketStarts[BucketIdx] == BucketCursors[BucketIdx])
        continue;
      Word |= (1U << J);

      // Calculate what the offset of the first hash record in the chain would
      // be if it were inflated to contain 32-bit pointers. On a 32-bit system,
      // each record would be 12 bytes. See HROffsetCalc in gsi.h.
      const int SizeOfHROffsetCalc = 12;
      ulittle32_t ChainStartOff =
          ulittle32_t(BucketStarts[BucketIdx] * SizeOfHROffsetCalc);
      HashBuckets.push_back(ChainStartOff);
    }
    HashBitmap[I] = Word;
  }
}

GSIStreamBuilder::GSIStreamBuilder(msf::MSFBuilder &Msf)
    : Msf(Msf), PSH(std::make_unique<GSIHashStreamBuilder>()),
      GSH(std::make_unique<GSIHashStreamBuilder>()) {}

GSIStreamBuilder::~GSIStreamBuilder() = default;

uint32_t GSIStreamBuilder::calculatePublicsHashStreamSize() const {
  uint32_t Size = 0;
  Size += sizeof(PublicsStreamHeader);
  Size += PSH->calculateSerializedLength();
  Size += Publics.size() * sizeof(uint32_t); // AddrMap
  // FIXME: Add thunk map and section offsets for incremental linking.

  return Size;
}

uint32_t GSIStreamBuilder::calculateGlobalsHashStreamSize() const {
  return GSH->calculateSerializedLength();
}

Error GSIStreamBuilder::finalizeMsfLayout() {
  // First we write public symbol records, then we write global symbol records.
  finalizePublicBuckets();
  finalizeGlobalBuckets(PSH->RecordByteSize);

  Expected<uint32_t> Idx = Msf.addStream(calculateGlobalsHashStreamSize());
  if (!Idx)
    return Idx.takeError();
  GlobalsStreamIndex = *Idx;

  Idx = Msf.addStream(calculatePublicsHashStreamSize());
  if (!Idx)
    return Idx.takeError();
  PublicsStreamIndex = *Idx;

  uint32_t RecordBytes = PSH->RecordByteSize + GSH->RecordByteSize;

  Idx = Msf.addStream(RecordBytes);
  if (!Idx)
    return Idx.takeError();
  RecordStreamIndex = *Idx;
  return Error::success();
}

void GSIStreamBuilder::addPublicSymbols(std::vector<BulkPublic> &&PublicsIn) {
  assert(Publics.empty() && PSH->RecordByteSize == 0 &&
         "publics can only be added once");
  Publics = std::move(PublicsIn);

  // Sort the symbols by name. PDBs contain lots of symbols, so use parallelism.
  parallelSort(Publics, [](const BulkPublic &L, const BulkPublic &R) {
    return L.getName() < R.getName();
  });

  // Assign offsets and calculate the length of the public symbol records.
  uint32_t SymOffset = 0;
  for (BulkPublic &Pub : Publics) {
    Pub.SymOffset = SymOffset;
    SymOffset += sizeOfPublic(Pub);
  }

  // Remember the length of the public stream records.
  PSH->RecordByteSize = SymOffset;
}

void GSIStreamBuilder::addGlobalSymbol(const ProcRefSym &Sym) {
  serializeAndAddGlobal(Sym);
}

void GSIStreamBuilder::addGlobalSymbol(const DataSym &Sym) {
  serializeAndAddGlobal(Sym);
}

void GSIStreamBuilder::addGlobalSymbol(const ConstantSym &Sym) {
  serializeAndAddGlobal(Sym);
}

template <typename T>
void GSIStreamBuilder::serializeAndAddGlobal(const T &Symbol) {
  T Copy(Symbol);
  addGlobalSymbol(SymbolSerializer::writeOneSymbol(Copy, Msf.getAllocator(),
                                                   CodeViewContainer::Pdb));
}

void GSIStreamBuilder::addGlobalSymbol(const codeview::CVSymbol &Symbol) {
  // Ignore duplicate typedefs and constants.
  if (Symbol.kind() == S_UDT || Symbol.kind() == S_CONSTANT) {
    auto Iter = GlobalsSeen.insert(Symbol);
    if (!Iter.second)
      return;
  }
  GSH->RecordByteSize += Symbol.length();
  Globals.push_back(Symbol);
}

// Serialize each public and write it.
static Error writePublics(BinaryStreamWriter &Writer,
                          ArrayRef<BulkPublic> Publics) {
  std::vector<uint8_t> Storage;
  for (const BulkPublic &Pub : Publics) {
    Storage.resize(sizeOfPublic(Pub));
    serializePublic(Storage.data(), Pub);
    if (Error E = Writer.writeBytes(Storage))
      return E;
  }
  return Error::success();
}

static Error writeRecords(BinaryStreamWriter &Writer,
                          ArrayRef<CVSymbol> Records) {
  BinaryItemStream<CVSymbol> ItemStream(llvm::endianness::little);
  ItemStream.setItems(Records);
  BinaryStreamRef RecordsRef(ItemStream);
  return Writer.writeStreamRef(RecordsRef);
}

Error GSIStreamBuilder::commitSymbolRecordStream(
    WritableBinaryStreamRef Stream) {
  BinaryStreamWriter Writer(Stream);

  // Write public symbol records first, followed by global symbol records.  This
  // must match the order that we assume in finalizeMsfLayout when computing
  // PSHZero and GSHZero.
  if (auto EC = writePublics(Writer, Publics))
    return EC;
  if (auto EC = writeRecords(Writer, Globals))
    return EC;

  return Error::success();
}

static std::vector<support::ulittle32_t>
computeAddrMap(ArrayRef<BulkPublic> Publics) {
  // Build a parallel vector of indices into the Publics vector, and sort it by
  // address.
  std::vector<ulittle32_t> PubAddrMap;
  PubAddrMap.reserve(Publics.size());
  for (int I = 0, E = Publics.size(); I < E; ++I)
    PubAddrMap.push_back(ulittle32_t(I));

  auto AddrCmp = [Publics](const ulittle32_t &LIdx, const ulittle32_t &RIdx) {
    const BulkPublic &L = Publics[LIdx];
    const BulkPublic &R = Publics[RIdx];
    if (L.Segment != R.Segment)
      return L.Segment < R.Segment;
    if (L.Offset != R.Offset)
      return L.Offset < R.Offset;
    // parallelSort is unstable, so we have to do name comparison to ensure
    // that two names for the same location come out in a deterministic order.
    return L.getName() < R.getName();
  };
  parallelSort(PubAddrMap, AddrCmp);

  // Rewrite the public symbol indices into symbol offsets.
  for (ulittle32_t &Entry : PubAddrMap)
    Entry = Publics[Entry].SymOffset;
  return PubAddrMap;
}

Error GSIStreamBuilder::commitPublicsHashStream(
    WritableBinaryStreamRef Stream) {
  BinaryStreamWriter Writer(Stream);
  PublicsStreamHeader Header;

  // FIXME: Fill these in. They are for incremental linking.
  Header.SymHash = PSH->calculateSerializedLength();
  Header.AddrMap = Publics.size() * 4;
  Header.NumThunks = 0;
  Header.SizeOfThunk = 0;
  Header.ISectThunkTable = 0;
  memset(Header.Padding, 0, sizeof(Header.Padding));
  Header.OffThunkTable = 0;
  Header.NumSections = 0;
  if (auto EC = Writer.writeObject(Header))
    return EC;

  if (auto EC = PSH->commit(Writer))
    return EC;

  std::vector<support::ulittle32_t> PubAddrMap = computeAddrMap(Publics);
  assert(PubAddrMap.size() == Publics.size());
  if (auto EC = Writer.writeArray(ArrayRef(PubAddrMap)))
    return EC;

  return Error::success();
}

Error GSIStreamBuilder::commitGlobalsHashStream(
    WritableBinaryStreamRef Stream) {
  BinaryStreamWriter Writer(Stream);
  return GSH->commit(Writer);
}

Error GSIStreamBuilder::commit(const msf::MSFLayout &Layout,
                               WritableBinaryStreamRef Buffer) {
  llvm::TimeTraceScope timeScope("Commit GSI stream");
  auto GS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, getGlobalsStreamIndex(), Msf.getAllocator());
  auto PS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, getPublicsStreamIndex(), Msf.getAllocator());
  auto PRS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, getRecordStreamIndex(), Msf.getAllocator());

  if (auto EC = commitSymbolRecordStream(*PRS))
    return EC;
  if (auto EC = commitGlobalsHashStream(*GS))
    return EC;
  if (auto EC = commitPublicsHashStream(*PS))
    return EC;
  return Error::success();
}
