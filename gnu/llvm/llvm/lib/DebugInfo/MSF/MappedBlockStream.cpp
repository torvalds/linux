//===- MappedBlockStream.cpp - Reads stream data from an MSF file ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::msf;

namespace {

template <typename Base> class MappedBlockStreamImpl : public Base {
public:
  template <typename... Args>
  MappedBlockStreamImpl(Args &&... Params)
      : Base(std::forward<Args>(Params)...) {}
};

} // end anonymous namespace

using Interval = std::pair<uint64_t, uint64_t>;

static Interval intersect(const Interval &I1, const Interval &I2) {
  return std::make_pair(std::max(I1.first, I2.first),
                        std::min(I1.second, I2.second));
}

MappedBlockStream::MappedBlockStream(uint32_t BlockSize,
                                     const MSFStreamLayout &Layout,
                                     BinaryStreamRef MsfData,
                                     BumpPtrAllocator &Allocator)
    : BlockSize(BlockSize), StreamLayout(Layout), MsfData(MsfData),
      Allocator(Allocator) {}

std::unique_ptr<MappedBlockStream> MappedBlockStream::createStream(
    uint32_t BlockSize, const MSFStreamLayout &Layout, BinaryStreamRef MsfData,
    BumpPtrAllocator &Allocator) {
  return std::make_unique<MappedBlockStreamImpl<MappedBlockStream>>(
      BlockSize, Layout, MsfData, Allocator);
}

std::unique_ptr<MappedBlockStream> MappedBlockStream::createIndexedStream(
    const MSFLayout &Layout, BinaryStreamRef MsfData, uint32_t StreamIndex,
    BumpPtrAllocator &Allocator) {
  assert(StreamIndex < Layout.StreamMap.size() && "Invalid stream index");
  MSFStreamLayout SL;
  SL.Blocks = Layout.StreamMap[StreamIndex];
  SL.Length = Layout.StreamSizes[StreamIndex];
  return std::make_unique<MappedBlockStreamImpl<MappedBlockStream>>(
      Layout.SB->BlockSize, SL, MsfData, Allocator);
}

std::unique_ptr<MappedBlockStream>
MappedBlockStream::createDirectoryStream(const MSFLayout &Layout,
                                         BinaryStreamRef MsfData,
                                         BumpPtrAllocator &Allocator) {
  MSFStreamLayout SL;
  SL.Blocks = Layout.DirectoryBlocks;
  SL.Length = Layout.SB->NumDirectoryBytes;
  return createStream(Layout.SB->BlockSize, SL, MsfData, Allocator);
}

std::unique_ptr<MappedBlockStream>
MappedBlockStream::createFpmStream(const MSFLayout &Layout,
                                   BinaryStreamRef MsfData,
                                   BumpPtrAllocator &Allocator) {
  MSFStreamLayout SL(getFpmStreamLayout(Layout));
  return createStream(Layout.SB->BlockSize, SL, MsfData, Allocator);
}

Error MappedBlockStream::readBytes(uint64_t Offset, uint64_t Size,
                                   ArrayRef<uint8_t> &Buffer) {
  // Make sure we aren't trying to read beyond the end of the stream.
  if (auto EC = checkOffsetForRead(Offset, Size))
    return EC;

  if (tryReadContiguously(Offset, Size, Buffer))
    return Error::success();

  auto CacheIter = CacheMap.find(Offset);
  if (CacheIter != CacheMap.end()) {
    // Try to find an alloc that was large enough for this request.
    for (auto &Entry : CacheIter->second) {
      if (Entry.size() >= Size) {
        Buffer = Entry.slice(0, Size);
        return Error::success();
      }
    }
  }

  // We couldn't find a buffer that started at the correct offset (the most
  // common scenario).  Try to see if there is a buffer that starts at some
  // other offset but overlaps the desired range.
  for (auto &CacheItem : CacheMap) {
    Interval RequestExtent = std::make_pair(Offset, Offset + Size);

    // We already checked this one on the fast path above.
    if (CacheItem.first == Offset)
      continue;
    // If the initial extent of the cached item is beyond the ending extent
    // of the request, there is no overlap.
    if (CacheItem.first >= Offset + Size)
      continue;

    // We really only have to check the last item in the list, since we append
    // in order of increasing length.
    if (CacheItem.second.empty())
      continue;

    auto CachedAlloc = CacheItem.second.back();
    // If the initial extent of the request is beyond the ending extent of
    // the cached item, there is no overlap.
    Interval CachedExtent =
        std::make_pair(CacheItem.first, CacheItem.first + CachedAlloc.size());
    if (RequestExtent.first >= CachedExtent.first + CachedExtent.second)
      continue;

    Interval Intersection = intersect(CachedExtent, RequestExtent);
    // Only use this if the entire request extent is contained in the cached
    // extent.
    if (Intersection != RequestExtent)
      continue;

    uint64_t CacheRangeOffset =
        AbsoluteDifference(CachedExtent.first, Intersection.first);
    Buffer = CachedAlloc.slice(CacheRangeOffset, Size);
    return Error::success();
  }

  // Otherwise allocate a large enough buffer in the pool, memcpy the data
  // into it, and return an ArrayRef to that.  Do not touch existing pool
  // allocations, as existing clients may be holding a pointer which must
  // not be invalidated.
  uint8_t *WriteBuffer = static_cast<uint8_t *>(Allocator.Allocate(Size, 8));
  if (auto EC = readBytes(Offset, MutableArrayRef<uint8_t>(WriteBuffer, Size)))
    return EC;

  if (CacheIter != CacheMap.end()) {
    CacheIter->second.emplace_back(WriteBuffer, Size);
  } else {
    std::vector<CacheEntry> List;
    List.emplace_back(WriteBuffer, Size);
    CacheMap.insert(std::make_pair(Offset, List));
  }
  Buffer = ArrayRef<uint8_t>(WriteBuffer, Size);
  return Error::success();
}

Error MappedBlockStream::readLongestContiguousChunk(uint64_t Offset,
                                                    ArrayRef<uint8_t> &Buffer) {
  // Make sure we aren't trying to read beyond the end of the stream.
  if (auto EC = checkOffsetForRead(Offset, 1))
    return EC;

  uint64_t First = Offset / BlockSize;
  uint64_t Last = First;

  while (Last < getNumBlocks() - 1) {
    if (StreamLayout.Blocks[Last] != StreamLayout.Blocks[Last + 1] - 1)
      break;
    ++Last;
  }

  uint64_t OffsetInFirstBlock = Offset % BlockSize;
  uint64_t BytesFromFirstBlock = BlockSize - OffsetInFirstBlock;
  uint64_t BlockSpan = Last - First + 1;
  uint64_t ByteSpan = BytesFromFirstBlock + (BlockSpan - 1) * BlockSize;

  ArrayRef<uint8_t> BlockData;
  uint64_t MsfOffset = blockToOffset(StreamLayout.Blocks[First], BlockSize);
  if (auto EC = MsfData.readBytes(MsfOffset, BlockSize, BlockData))
    return EC;

  BlockData = BlockData.drop_front(OffsetInFirstBlock);
  Buffer = ArrayRef<uint8_t>(BlockData.data(), ByteSpan);
  return Error::success();
}

uint64_t MappedBlockStream::getLength() { return StreamLayout.Length; }

bool MappedBlockStream::tryReadContiguously(uint64_t Offset, uint64_t Size,
                                            ArrayRef<uint8_t> &Buffer) {
  if (Size == 0) {
    Buffer = ArrayRef<uint8_t>();
    return true;
  }
  // Attempt to fulfill the request with a reference directly into the stream.
  // This can work even if the request crosses a block boundary, provided that
  // all subsequent blocks are contiguous.  For example, a 10k read with a 4k
  // block size can be filled with a reference if, from the starting offset,
  // 3 blocks in a row are contiguous.
  uint64_t BlockNum = Offset / BlockSize;
  uint64_t OffsetInBlock = Offset % BlockSize;
  uint64_t BytesFromFirstBlock = std::min(Size, BlockSize - OffsetInBlock);
  uint64_t NumAdditionalBlocks =
      alignTo(Size - BytesFromFirstBlock, BlockSize) / BlockSize;

  uint64_t RequiredContiguousBlocks = NumAdditionalBlocks + 1;
  uint64_t E = StreamLayout.Blocks[BlockNum];
  for (uint64_t I = 0; I < RequiredContiguousBlocks; ++I, ++E) {
    if (StreamLayout.Blocks[I + BlockNum] != E)
      return false;
  }

  // Read out the entire block where the requested offset starts.  Then drop
  // bytes from the beginning so that the actual starting byte lines up with
  // the requested starting byte.  Then, since we know this is a contiguous
  // cross-block span, explicitly resize the ArrayRef to cover the entire
  // request length.
  ArrayRef<uint8_t> BlockData;
  uint64_t FirstBlockAddr = StreamLayout.Blocks[BlockNum];
  uint64_t MsfOffset = blockToOffset(FirstBlockAddr, BlockSize);
  if (auto EC = MsfData.readBytes(MsfOffset, BlockSize, BlockData)) {
    consumeError(std::move(EC));
    return false;
  }
  BlockData = BlockData.drop_front(OffsetInBlock);
  Buffer = ArrayRef<uint8_t>(BlockData.data(), Size);
  return true;
}

Error MappedBlockStream::readBytes(uint64_t Offset,
                                   MutableArrayRef<uint8_t> Buffer) {
  uint64_t BlockNum = Offset / BlockSize;
  uint64_t OffsetInBlock = Offset % BlockSize;

  // Make sure we aren't trying to read beyond the end of the stream.
  if (auto EC = checkOffsetForRead(Offset, Buffer.size()))
    return EC;

  uint64_t BytesLeft = Buffer.size();
  uint64_t BytesWritten = 0;
  uint8_t *WriteBuffer = Buffer.data();
  while (BytesLeft > 0) {
    uint64_t StreamBlockAddr = StreamLayout.Blocks[BlockNum];

    ArrayRef<uint8_t> BlockData;
    uint64_t Offset = blockToOffset(StreamBlockAddr, BlockSize);
    if (auto EC = MsfData.readBytes(Offset, BlockSize, BlockData))
      return EC;

    const uint8_t *ChunkStart = BlockData.data() + OffsetInBlock;
    uint64_t BytesInChunk = std::min(BytesLeft, BlockSize - OffsetInBlock);
    ::memcpy(WriteBuffer + BytesWritten, ChunkStart, BytesInChunk);

    BytesWritten += BytesInChunk;
    BytesLeft -= BytesInChunk;
    ++BlockNum;
    OffsetInBlock = 0;
  }

  return Error::success();
}

void MappedBlockStream::invalidateCache() { CacheMap.shrink_and_clear(); }

void MappedBlockStream::fixCacheAfterWrite(uint64_t Offset,
                                           ArrayRef<uint8_t> Data) const {
  // If this write overlapped a read which previously came from the pool,
  // someone may still be holding a pointer to that alloc which is now invalid.
  // Compute the overlapping range and update the cache entry, so any
  // outstanding buffers are automatically updated.
  for (const auto &MapEntry : CacheMap) {
    // If the end of the written extent precedes the beginning of the cached
    // extent, ignore this map entry.
    if (Offset + Data.size() < MapEntry.first)
      continue;
    for (const auto &Alloc : MapEntry.second) {
      // If the end of the cached extent precedes the beginning of the written
      // extent, ignore this alloc.
      if (MapEntry.first + Alloc.size() < Offset)
        continue;

      // If we get here, they are guaranteed to overlap.
      Interval WriteInterval = std::make_pair(Offset, Offset + Data.size());
      Interval CachedInterval =
          std::make_pair(MapEntry.first, MapEntry.first + Alloc.size());
      // If they overlap, we need to write the new data into the overlapping
      // range.
      auto Intersection = intersect(WriteInterval, CachedInterval);
      assert(Intersection.first <= Intersection.second);

      uint64_t Length = Intersection.second - Intersection.first;
      uint64_t SrcOffset =
          AbsoluteDifference(WriteInterval.first, Intersection.first);
      uint64_t DestOffset =
          AbsoluteDifference(CachedInterval.first, Intersection.first);
      ::memcpy(Alloc.data() + DestOffset, Data.data() + SrcOffset, Length);
    }
  }
}

WritableMappedBlockStream::WritableMappedBlockStream(
    uint32_t BlockSize, const MSFStreamLayout &Layout,
    WritableBinaryStreamRef MsfData, BumpPtrAllocator &Allocator)
    : ReadInterface(BlockSize, Layout, MsfData, Allocator),
      WriteInterface(MsfData) {}

std::unique_ptr<WritableMappedBlockStream>
WritableMappedBlockStream::createStream(uint32_t BlockSize,
                                        const MSFStreamLayout &Layout,
                                        WritableBinaryStreamRef MsfData,
                                        BumpPtrAllocator &Allocator) {
  return std::make_unique<MappedBlockStreamImpl<WritableMappedBlockStream>>(
      BlockSize, Layout, MsfData, Allocator);
}

std::unique_ptr<WritableMappedBlockStream>
WritableMappedBlockStream::createIndexedStream(const MSFLayout &Layout,
                                               WritableBinaryStreamRef MsfData,
                                               uint32_t StreamIndex,
                                               BumpPtrAllocator &Allocator) {
  assert(StreamIndex < Layout.StreamMap.size() && "Invalid stream index");
  MSFStreamLayout SL;
  SL.Blocks = Layout.StreamMap[StreamIndex];
  SL.Length = Layout.StreamSizes[StreamIndex];
  return createStream(Layout.SB->BlockSize, SL, MsfData, Allocator);
}

std::unique_ptr<WritableMappedBlockStream>
WritableMappedBlockStream::createDirectoryStream(
    const MSFLayout &Layout, WritableBinaryStreamRef MsfData,
    BumpPtrAllocator &Allocator) {
  MSFStreamLayout SL;
  SL.Blocks = Layout.DirectoryBlocks;
  SL.Length = Layout.SB->NumDirectoryBytes;
  return createStream(Layout.SB->BlockSize, SL, MsfData, Allocator);
}

std::unique_ptr<WritableMappedBlockStream>
WritableMappedBlockStream::createFpmStream(const MSFLayout &Layout,
                                           WritableBinaryStreamRef MsfData,
                                           BumpPtrAllocator &Allocator,
                                           bool AltFpm) {
  // We only want to give the user a stream containing the bytes of the FPM that
  // are actually valid, but we want to initialize all of the bytes, even those
  // that come from reserved FPM blocks where the entire block is unused.  To do
  // this, we first create the full layout, which gives us a stream with all
  // bytes and all blocks, and initialize everything to 0xFF (all blocks in the
  // file are unused).  Then we create the minimal layout (which contains only a
  // subset of the bytes previously initialized), and return that to the user.
  MSFStreamLayout MinLayout(getFpmStreamLayout(Layout, false, AltFpm));

  MSFStreamLayout FullLayout(getFpmStreamLayout(Layout, true, AltFpm));
  auto Result =
      createStream(Layout.SB->BlockSize, FullLayout, MsfData, Allocator);
  if (!Result)
    return Result;
  std::vector<uint8_t> InitData(Layout.SB->BlockSize, 0xFF);
  BinaryStreamWriter Initializer(*Result);
  while (Initializer.bytesRemaining() > 0)
    cantFail(Initializer.writeBytes(InitData));
  return createStream(Layout.SB->BlockSize, MinLayout, MsfData, Allocator);
}

Error WritableMappedBlockStream::readBytes(uint64_t Offset, uint64_t Size,
                                           ArrayRef<uint8_t> &Buffer) {
  return ReadInterface.readBytes(Offset, Size, Buffer);
}

Error WritableMappedBlockStream::readLongestContiguousChunk(
    uint64_t Offset, ArrayRef<uint8_t> &Buffer) {
  return ReadInterface.readLongestContiguousChunk(Offset, Buffer);
}

uint64_t WritableMappedBlockStream::getLength() {
  return ReadInterface.getLength();
}

Error WritableMappedBlockStream::writeBytes(uint64_t Offset,
                                            ArrayRef<uint8_t> Buffer) {
  // Make sure we aren't trying to write beyond the end of the stream.
  if (auto EC = checkOffsetForWrite(Offset, Buffer.size()))
    return EC;

  uint64_t BlockNum = Offset / getBlockSize();
  uint64_t OffsetInBlock = Offset % getBlockSize();

  uint64_t BytesLeft = Buffer.size();
  uint64_t BytesWritten = 0;
  while (BytesLeft > 0) {
    uint64_t StreamBlockAddr = getStreamLayout().Blocks[BlockNum];
    uint64_t BytesToWriteInChunk =
        std::min(BytesLeft, getBlockSize() - OffsetInBlock);

    const uint8_t *Chunk = Buffer.data() + BytesWritten;
    ArrayRef<uint8_t> ChunkData(Chunk, BytesToWriteInChunk);
    uint64_t MsfOffset = blockToOffset(StreamBlockAddr, getBlockSize());
    MsfOffset += OffsetInBlock;
    if (auto EC = WriteInterface.writeBytes(MsfOffset, ChunkData))
      return EC;

    BytesLeft -= BytesToWriteInChunk;
    BytesWritten += BytesToWriteInChunk;
    ++BlockNum;
    OffsetInBlock = 0;
  }

  ReadInterface.fixCacheAfterWrite(Offset, Buffer);

  return Error::success();
}

Error WritableMappedBlockStream::commit() { return WriteInterface.commit(); }
