//===- GSIStreamBuilder.h - PDB Publics/Globals Stream Creation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_GSISTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_GSISTREAMBUILDER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class ConstantSym;
class DataSym;
class ProcRefSym;
} // namespace codeview
template <typename T> struct BinaryItemTraits;

template <> struct BinaryItemTraits<codeview::CVSymbol> {
  static size_t length(const codeview::CVSymbol &Item) {
    return Item.RecordData.size();
  }
  static ArrayRef<uint8_t> bytes(const codeview::CVSymbol &Item) {
    return Item.RecordData;
  }
};

namespace msf {
class MSFBuilder;
struct MSFLayout;
} // namespace msf
namespace pdb {
struct GSIHashStreamBuilder;
struct BulkPublic;
struct SymbolDenseMapInfo;

class GSIStreamBuilder {

public:
  explicit GSIStreamBuilder(msf::MSFBuilder &Msf);
  ~GSIStreamBuilder();

  GSIStreamBuilder(const GSIStreamBuilder &) = delete;
  GSIStreamBuilder &operator=(const GSIStreamBuilder &) = delete;

  Error finalizeMsfLayout();

  Error commit(const msf::MSFLayout &Layout, WritableBinaryStreamRef Buffer);

  uint32_t getPublicsStreamIndex() const { return PublicsStreamIndex; }
  uint32_t getGlobalsStreamIndex() const { return GlobalsStreamIndex; }
  uint32_t getRecordStreamIndex() const { return RecordStreamIndex; }

  // Add public symbols in bulk.
  void addPublicSymbols(std::vector<BulkPublic> &&PublicsIn);

  void addGlobalSymbol(const codeview::ProcRefSym &Sym);
  void addGlobalSymbol(const codeview::DataSym &Sym);
  void addGlobalSymbol(const codeview::ConstantSym &Sym);

  // Add a pre-serialized global symbol record. The caller must ensure that the
  // symbol data remains alive until the global stream is committed to disk.
  void addGlobalSymbol(const codeview::CVSymbol &Sym);

private:
  void finalizePublicBuckets();
  void finalizeGlobalBuckets(uint32_t RecordZeroOffset);

  template <typename T> void serializeAndAddGlobal(const T &Symbol);

  uint32_t calculatePublicsHashStreamSize() const;
  uint32_t calculateGlobalsHashStreamSize() const;
  Error commitSymbolRecordStream(WritableBinaryStreamRef Stream);
  Error commitPublicsHashStream(WritableBinaryStreamRef Stream);
  Error commitGlobalsHashStream(WritableBinaryStreamRef Stream);

  uint32_t PublicsStreamIndex = kInvalidStreamIndex;
  uint32_t GlobalsStreamIndex = kInvalidStreamIndex;
  uint32_t RecordStreamIndex = kInvalidStreamIndex;
  msf::MSFBuilder &Msf;
  std::unique_ptr<GSIHashStreamBuilder> PSH;
  std::unique_ptr<GSIHashStreamBuilder> GSH;

  // List of all of the public records. These are stored unserialized so that we
  // can defer copying the names until we are ready to commit the PDB.
  std::vector<BulkPublic> Publics;

  // List of all of the global records.
  std::vector<codeview::CVSymbol> Globals;

  // Hash table for deduplicating global typedef and constant records. Only used
  // for globals.
  llvm::DenseSet<codeview::CVSymbol, SymbolDenseMapInfo> GlobalsSeen;
};

/// This struct is equivalent to codeview::PublicSym32, but it has been
/// optimized for size to speed up bulk serialization and sorting operations
/// during PDB writing.
struct BulkPublic {
  BulkPublic() : Flags(0), BucketIdx(0) {}

  const char *Name = nullptr;
  uint32_t NameLen = 0;

  // Offset of the symbol record in the publics stream.
  uint32_t SymOffset = 0;

  // Section offset of the symbol in the image.
  uint32_t Offset = 0;

  // Section index of the section containing the symbol.
  uint16_t Segment = 0;

  // PublicSymFlags.
  uint16_t Flags : 4;

  // GSI hash table bucket index. The maximum value is IPHR_HASH.
  uint16_t BucketIdx : 12;
  static_assert(IPHR_HASH <= 1 << 12, "bitfield too small");

  void setFlags(codeview::PublicSymFlags F) {
    Flags = uint32_t(F);
    assert(Flags == uint32_t(F) && "truncated");
  }

  void setBucketIdx(uint16_t B) {
    assert(B < IPHR_HASH);
    BucketIdx = B;
  }

  StringRef getName() const { return StringRef(Name, NameLen); }
};

static_assert(sizeof(BulkPublic) <= 24, "unexpected size increase");
static_assert(std::is_trivially_copyable<BulkPublic>::value,
              "should be trivial");

} // namespace pdb
} // namespace llvm

#endif
