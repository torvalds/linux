//===- TpiStreamBuilder.h - PDB Tpi Stream Creation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAMBUILDER_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"

#include <vector>

namespace llvm {
class BinaryByteStream;
template <typename T> struct BinaryItemTraits;

template <> struct BinaryItemTraits<llvm::codeview::CVType> {
  static size_t length(const codeview::CVType &Item) { return Item.length(); }
  static ArrayRef<uint8_t> bytes(const codeview::CVType &Item) {
    return Item.data();
  }
};

namespace msf {
class MSFBuilder;
struct MSFLayout;
}
namespace pdb {
struct TpiStreamHeader;

class TpiStreamBuilder {
public:
  explicit TpiStreamBuilder(msf::MSFBuilder &Msf, uint32_t StreamIdx);
  ~TpiStreamBuilder();

  TpiStreamBuilder(const TpiStreamBuilder &) = delete;
  TpiStreamBuilder &operator=(const TpiStreamBuilder &) = delete;

  void setVersionHeader(PdbRaw_TpiVer Version);
  void addTypeRecord(ArrayRef<uint8_t> Type, std::optional<uint32_t> Hash);
  void addTypeRecords(ArrayRef<uint8_t> Types, ArrayRef<uint16_t> Sizes,
                      ArrayRef<uint32_t> Hashes);

  Error finalizeMsfLayout();

  uint32_t getRecordCount() const { return TypeRecordCount; }

  Error commit(const msf::MSFLayout &Layout, WritableBinaryStreamRef Buffer);

  uint32_t calculateSerializedLength();

private:
  void updateTypeIndexOffsets(ArrayRef<uint16_t> Sizes);

  uint32_t calculateHashBufferSize() const;
  uint32_t calculateIndexOffsetSize() const;
  Error finalize();

  msf::MSFBuilder &Msf;
  BumpPtrAllocator &Allocator;

  uint32_t TypeRecordCount = 0;
  size_t TypeRecordBytes = 0;

  PdbRaw_TpiVer VerHeader = PdbRaw_TpiVer::PdbTpiV80;
  std::vector<ArrayRef<uint8_t>> TypeRecBuffers;
  std::vector<uint32_t> TypeHashes;
  std::vector<codeview::TypeIndexOffset> TypeIndexOffsets;
  uint32_t HashStreamIndex = kInvalidStreamIndex;
  std::unique_ptr<BinaryByteStream> HashValueStream;

  const TpiStreamHeader *Header;
  uint32_t Idx;
};
} // namespace pdb
}

#endif
