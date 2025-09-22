//===- MSFCommon.h - Common types and functions for MSF files ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MSFCOMMON_H
#define LLVM_DEBUGINFO_MSF_MSFCOMMON_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace msf {

static const char Magic[] = {'M',  'i',  'c',    'r', 'o', 's',  'o',  'f',
                             't',  ' ',  'C',    '/', 'C', '+',  '+',  ' ',
                             'M',  'S',  'F',    ' ', '7', '.',  '0',  '0',
                             '\r', '\n', '\x1a', 'D', 'S', '\0', '\0', '\0'};

// The superblock is overlaid at the beginning of the file (offset 0).
// It starts with a magic header and is followed by information which
// describes the layout of the file system.
struct SuperBlock {
  char MagicBytes[sizeof(Magic)];
  // The file system is split into a variable number of fixed size elements.
  // These elements are referred to as blocks.  The size of a block may vary
  // from system to system.
  support::ulittle32_t BlockSize;
  // The index of the free block map.
  support::ulittle32_t FreeBlockMapBlock;
  // This contains the number of blocks resident in the file system.  In
  // practice, NumBlocks * BlockSize is equivalent to the size of the MSF
  // file.
  support::ulittle32_t NumBlocks;
  // This contains the number of bytes which make up the directory.
  support::ulittle32_t NumDirectoryBytes;
  // This field's purpose is not yet known.
  support::ulittle32_t Unknown1;
  // This contains the block # of the block map.
  support::ulittle32_t BlockMapAddr;
};

struct MSFLayout {
  MSFLayout() = default;

  uint32_t mainFpmBlock() const {
    assert(SB->FreeBlockMapBlock == 1 || SB->FreeBlockMapBlock == 2);
    return SB->FreeBlockMapBlock;
  }

  uint32_t alternateFpmBlock() const {
    // If mainFpmBlock is 1, this is 2.  If mainFpmBlock is 2, this is 1.
    return 3U - mainFpmBlock();
  }

  const SuperBlock *SB = nullptr;
  BitVector FreePageMap;
  ArrayRef<support::ulittle32_t> DirectoryBlocks;
  ArrayRef<support::ulittle32_t> StreamSizes;
  std::vector<ArrayRef<support::ulittle32_t>> StreamMap;
};

/// Describes the layout of a stream in an MSF layout.  A "stream" here
/// is defined as any logical unit of data which may be arranged inside the MSF
/// file as a sequence of (possibly discontiguous) blocks.  When we want to read
/// from a particular MSF Stream, we fill out a stream layout structure and the
/// reader uses it to determine which blocks in the underlying MSF file contain
/// the data, so that it can be pieced together in the right order.
class MSFStreamLayout {
public:
  uint32_t Length;
  std::vector<support::ulittle32_t> Blocks;
};

/// Determine the layout of the FPM stream, given the MSF layout.  An FPM
/// stream spans 1 or more blocks, each at equally spaced intervals throughout
/// the file.
MSFStreamLayout getFpmStreamLayout(const MSFLayout &Msf,
                                   bool IncludeUnusedFpmData = false,
                                   bool AltFpm = false);

inline bool isValidBlockSize(uint32_t Size) {
  switch (Size) {
  case 512:
  case 1024:
  case 2048:
  case 4096:
  case 8192:
  case 16384:
  case 32768:
    return true;
  }
  return false;
}

/// Given the specified block size, returns the maximum possible file size.
/// Block Size  |  Max File Size
/// <= 4096     |      4GB
///    8192     |      8GB
///   16384     |      16GB
///   32768     |      32GB
/// \p Size - the block size of the MSF
inline uint64_t getMaxFileSizeFromBlockSize(uint32_t Size) {
  switch (Size) {
  case 8192:
    return (uint64_t)UINT32_MAX * 2ULL;
  case 16384:
    return (uint64_t)UINT32_MAX * 3ULL;
  case 32768:
    return (uint64_t)UINT32_MAX * 4ULL;
  default:
    return (uint64_t)UINT32_MAX;
  }
}

// Super Block, Fpm0, Fpm1, and Block Map
inline uint32_t getMinimumBlockCount() { return 4; }

// Super Block, Fpm0, and Fpm1 are reserved.  The Block Map, although required
// need not be at block 3.
inline uint32_t getFirstUnreservedBlock() { return 3; }

inline uint64_t bytesToBlocks(uint64_t NumBytes, uint64_t BlockSize) {
  return divideCeil(NumBytes, BlockSize);
}

inline uint64_t blockToOffset(uint64_t BlockNumber, uint64_t BlockSize) {
  return BlockNumber * BlockSize;
}

inline uint32_t getFpmIntervalLength(const MSFLayout &L) {
  return L.SB->BlockSize;
}

/// Given an MSF with the specified block size and number of blocks, determine
/// how many pieces the specified Fpm is split into.
/// \p BlockSize - the block size of the MSF
/// \p NumBlocks - the total number of blocks in the MSF
/// \p IncludeUnusedFpmData - When true, this will count every block that is
///    both in the file and matches the form of an FPM block, even if some of
///    those FPM blocks are unused (a single FPM block can describe the
///    allocation status of up to 32,767 blocks, although one appears only
///    every 4,096 blocks).  So there are 8x as many blocks that match the
///    form as there are blocks that are necessary to describe the allocation
///    status of the file.  When this parameter is false, these extraneous
///    trailing blocks are not counted.
inline uint32_t getNumFpmIntervals(uint32_t BlockSize, uint32_t NumBlocks,
                                   bool IncludeUnusedFpmData, int FpmNumber) {
  assert(FpmNumber == 1 || FpmNumber == 2);
  if (IncludeUnusedFpmData) {
    // This calculation determines how many times a number of the form
    // BlockSize * k + N appears in the range [0, NumBlocks).  We only need to
    // do this when unused data is included, since the number of blocks dwarfs
    // the number of fpm blocks.
    return divideCeil(NumBlocks - FpmNumber, BlockSize);
  }

  // We want the minimum number of intervals required, where each interval can
  // represent BlockSize * 8 blocks.
  return divideCeil(NumBlocks, 8 * BlockSize);
}

inline uint32_t getNumFpmIntervals(const MSFLayout &L,
                                   bool IncludeUnusedFpmData = false,
                                   bool AltFpm = false) {
  return getNumFpmIntervals(L.SB->BlockSize, L.SB->NumBlocks,
                            IncludeUnusedFpmData,
                            AltFpm ? L.alternateFpmBlock() : L.mainFpmBlock());
}

Error validateSuperBlock(const SuperBlock &SB);

} // end namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MSFCOMMON_H
