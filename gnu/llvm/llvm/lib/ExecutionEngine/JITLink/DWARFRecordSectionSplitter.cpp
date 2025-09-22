//===-------- JITLink_DWARFRecordSectionSplitter.cpp - JITLink-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/Support/BinaryStreamReader.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

DWARFRecordSectionSplitter::DWARFRecordSectionSplitter(StringRef SectionName)
    : SectionName(SectionName) {}

Error DWARFRecordSectionSplitter::operator()(LinkGraph &G) {
  auto *Section = G.findSectionByName(SectionName);

  if (!Section) {
    LLVM_DEBUG({
      dbgs() << "DWARFRecordSectionSplitter: No " << SectionName
             << " section. Nothing to do\n";
    });
    return Error::success();
  }

  LLVM_DEBUG({
    dbgs() << "DWARFRecordSectionSplitter: Processing " << SectionName
           << "...\n";
  });

  DenseMap<Block *, LinkGraph::SplitBlockCache> Caches;

  {
    // Pre-build the split caches.
    for (auto *B : Section->blocks())
      Caches[B] = LinkGraph::SplitBlockCache::value_type();
    for (auto *Sym : Section->symbols())
      Caches[&Sym->getBlock()]->push_back(Sym);
    for (auto *B : Section->blocks())
      llvm::sort(*Caches[B], [](const Symbol *LHS, const Symbol *RHS) {
        return LHS->getOffset() > RHS->getOffset();
      });
  }

  // Iterate over blocks (we do this by iterating over Caches entries rather
  // than Section->blocks() as we will be inserting new blocks along the way,
  // which would invalidate iterators in the latter sequence.
  for (auto &KV : Caches) {
    auto &B = *KV.first;
    auto &BCache = KV.second;
    if (auto Err = processBlock(G, B, BCache))
      return Err;
  }

  return Error::success();
}

Error DWARFRecordSectionSplitter::processBlock(
    LinkGraph &G, Block &B, LinkGraph::SplitBlockCache &Cache) {
  LLVM_DEBUG(dbgs() << "  Processing block at " << B.getAddress() << "\n");

  // Section should not contain zero-fill blocks.
  if (B.isZeroFill())
    return make_error<JITLinkError>("Unexpected zero-fill block in " +
                                    SectionName + " section");

  if (B.getSize() == 0) {
    LLVM_DEBUG(dbgs() << "    Block is empty. Skipping.\n");
    return Error::success();
  }

  BinaryStreamReader BlockReader(
      StringRef(B.getContent().data(), B.getContent().size()),
      G.getEndianness());

  while (true) {
    uint64_t RecordStartOffset = BlockReader.getOffset();

    LLVM_DEBUG({
      dbgs() << "    Processing CFI record at "
             << formatv("{0:x16}", B.getAddress()) << "\n";
    });

    uint32_t Length;
    if (auto Err = BlockReader.readInteger(Length))
      return Err;
    if (Length != 0xffffffff) {
      if (auto Err = BlockReader.skip(Length))
        return Err;
    } else {
      uint64_t ExtendedLength;
      if (auto Err = BlockReader.readInteger(ExtendedLength))
        return Err;
      if (auto Err = BlockReader.skip(ExtendedLength))
        return Err;
    }

    // If this was the last block then there's nothing to split
    if (BlockReader.empty()) {
      LLVM_DEBUG(dbgs() << "      Extracted " << B << "\n");
      return Error::success();
    }

    uint64_t BlockSize = BlockReader.getOffset() - RecordStartOffset;
    auto &NewBlock = G.splitBlock(B, BlockSize, &Cache);
    (void)NewBlock;
    LLVM_DEBUG(dbgs() << "      Extracted " << NewBlock << "\n");
  }
}

} // namespace jitlink
} // namespace llvm
