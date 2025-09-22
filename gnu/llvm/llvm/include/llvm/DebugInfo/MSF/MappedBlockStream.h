//==- MappedBlockStream.h - Discontiguous stream data in an MSF --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MAPPEDBLOCKSTREAM_H
#define LLVM_DEBUGINFO_MSF_MAPPEDBLOCKSTREAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
namespace msf {

/// MappedBlockStream represents data stored in an MSF file into chunks of a
/// particular size (called the Block Size), and whose chunks may not be
/// necessarily contiguous.  The arrangement of these chunks MSF the file
/// is described by some other metadata contained within the MSF file.  In
/// the case of a standard MSF Stream, the layout of the stream's blocks
/// is described by the MSF "directory", but in the case of the directory
/// itself, the layout is described by an array at a fixed location within
/// the MSF.  MappedBlockStream provides methods for reading from and writing
/// to one of these streams transparently, as if it were a contiguous sequence
/// of bytes.
class MappedBlockStream : public BinaryStream {
  friend class WritableMappedBlockStream;

public:
  static std::unique_ptr<MappedBlockStream>
  createStream(uint32_t BlockSize, const MSFStreamLayout &Layout,
               BinaryStreamRef MsfData, BumpPtrAllocator &Allocator);

  static std::unique_ptr<MappedBlockStream>
  createIndexedStream(const MSFLayout &Layout, BinaryStreamRef MsfData,
                      uint32_t StreamIndex, BumpPtrAllocator &Allocator);

  static std::unique_ptr<MappedBlockStream>
  createFpmStream(const MSFLayout &Layout, BinaryStreamRef MsfData,
                  BumpPtrAllocator &Allocator);

  static std::unique_ptr<MappedBlockStream>
  createDirectoryStream(const MSFLayout &Layout, BinaryStreamRef MsfData,
                        BumpPtrAllocator &Allocator);

  llvm::endianness getEndian() const override {
    return llvm::endianness::little;
  }

  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override;
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override;

  uint64_t getLength() override;

  BumpPtrAllocator &getAllocator() { return Allocator; }

  void invalidateCache();

  uint32_t getBlockSize() const { return BlockSize; }
  uint32_t getNumBlocks() const { return StreamLayout.Blocks.size(); }
  uint32_t getStreamLength() const { return StreamLayout.Length; }

protected:
  MappedBlockStream(uint32_t BlockSize, const MSFStreamLayout &StreamLayout,
                    BinaryStreamRef MsfData, BumpPtrAllocator &Allocator);

private:
  const MSFStreamLayout &getStreamLayout() const { return StreamLayout; }
  void fixCacheAfterWrite(uint64_t Offset, ArrayRef<uint8_t> Data) const;

  Error readBytes(uint64_t Offset, MutableArrayRef<uint8_t> Buffer);
  bool tryReadContiguously(uint64_t Offset, uint64_t Size,
                           ArrayRef<uint8_t> &Buffer);

  const uint32_t BlockSize;
  const MSFStreamLayout StreamLayout;
  BinaryStreamRef MsfData;

  using CacheEntry = MutableArrayRef<uint8_t>;

  // We just store the allocator by reference.  We use this to allocate
  // contiguous memory for things like arrays or strings that cross a block
  // boundary, and this memory is expected to outlive the stream.  For example,
  // someone could create a stream, read some stuff, then close the stream, and
  // we would like outstanding references to fields to remain valid since the
  // entire file is mapped anyway.  Because of that, the user must supply the
  // allocator to allocate broken records from.
  BumpPtrAllocator &Allocator;
  DenseMap<uint32_t, std::vector<CacheEntry>> CacheMap;
};

class WritableMappedBlockStream : public WritableBinaryStream {
public:
  static std::unique_ptr<WritableMappedBlockStream>
  createStream(uint32_t BlockSize, const MSFStreamLayout &Layout,
               WritableBinaryStreamRef MsfData, BumpPtrAllocator &Allocator);

  static std::unique_ptr<WritableMappedBlockStream>
  createIndexedStream(const MSFLayout &Layout, WritableBinaryStreamRef MsfData,
                      uint32_t StreamIndex, BumpPtrAllocator &Allocator);

  static std::unique_ptr<WritableMappedBlockStream>
  createDirectoryStream(const MSFLayout &Layout,
                        WritableBinaryStreamRef MsfData,
                        BumpPtrAllocator &Allocator);

  static std::unique_ptr<WritableMappedBlockStream>
  createFpmStream(const MSFLayout &Layout, WritableBinaryStreamRef MsfData,
                  BumpPtrAllocator &Allocator, bool AltFpm = false);

  llvm::endianness getEndian() const override {
    return llvm::endianness::little;
  }

  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override;
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override;
  uint64_t getLength() override;

  Error writeBytes(uint64_t Offset, ArrayRef<uint8_t> Buffer) override;

  Error commit() override;

  const MSFStreamLayout &getStreamLayout() const {
    return ReadInterface.getStreamLayout();
  }

  uint32_t getBlockSize() const { return ReadInterface.getBlockSize(); }
  uint32_t getNumBlocks() const { return ReadInterface.getNumBlocks(); }
  uint32_t getStreamLength() const { return ReadInterface.getStreamLength(); }

protected:
  WritableMappedBlockStream(uint32_t BlockSize,
                            const MSFStreamLayout &StreamLayout,
                            WritableBinaryStreamRef MsfData,
                            BumpPtrAllocator &Allocator);

private:
  MappedBlockStream ReadInterface;
  WritableBinaryStreamRef WriteInterface;
};

} // namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MAPPEDBLOCKSTREAM_H
