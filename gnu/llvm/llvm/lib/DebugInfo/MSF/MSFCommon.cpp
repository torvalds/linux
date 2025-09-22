//===- MSFCommon.cpp - Common types and functions for MSF files -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/MSF/MSFError.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <cstring>

using namespace llvm;
using namespace llvm::msf;

Error llvm::msf::validateSuperBlock(const SuperBlock &SB) {
  // Check the magic bytes.
  if (std::memcmp(SB.MagicBytes, Magic, sizeof(Magic)) != 0)
    return make_error<MSFError>(msf_error_code::invalid_format,
                                "MSF magic header doesn't match");

  if (!isValidBlockSize(SB.BlockSize))
    return make_error<MSFError>(msf_error_code::invalid_format,
                                "Unsupported block size.");

  // We don't support directories whose sizes aren't a multiple of four bytes.
  if (SB.NumDirectoryBytes % sizeof(support::ulittle32_t) != 0)
    return make_error<MSFError>(msf_error_code::invalid_format,
                                "Directory size is not multiple of 4.");

  // The number of blocks which comprise the directory is a simple function of
  // the number of bytes it contains.
  uint64_t NumDirectoryBlocks =
      bytesToBlocks(SB.NumDirectoryBytes, SB.BlockSize);

  // The directory, as we understand it, is a block which consists of a list of
  // block numbers.  It is unclear what would happen if the number of blocks
  // couldn't fit on a single block.
  if (NumDirectoryBlocks > SB.BlockSize / sizeof(support::ulittle32_t))
    return make_error<MSFError>(msf_error_code::invalid_format,
                                "Too many directory blocks.");

  if (SB.BlockMapAddr == 0)
    return make_error<MSFError>(msf_error_code::invalid_format,
                                "Block 0 is reserved");

  if (SB.BlockMapAddr >= SB.NumBlocks)
    return make_error<MSFError>(msf_error_code::invalid_format,
                                "Block map address is invalid.");

  if (SB.FreeBlockMapBlock != 1 && SB.FreeBlockMapBlock != 2)
    return make_error<MSFError>(
        msf_error_code::invalid_format,
        "The free block map isn't at block 1 or block 2.");

  return Error::success();
}

MSFStreamLayout llvm::msf::getFpmStreamLayout(const MSFLayout &Msf,
                                              bool IncludeUnusedFpmData,
                                              bool AltFpm) {
  MSFStreamLayout FL;
  uint32_t NumFpmIntervals =
      getNumFpmIntervals(Msf, IncludeUnusedFpmData, AltFpm);

  uint32_t FpmBlock = AltFpm ? Msf.alternateFpmBlock() : Msf.mainFpmBlock();

  for (uint32_t I = 0; I < NumFpmIntervals; ++I) {
    FL.Blocks.push_back(support::ulittle32_t(FpmBlock));
    FpmBlock += msf::getFpmIntervalLength(Msf);
  }

  if (IncludeUnusedFpmData)
    FL.Length = NumFpmIntervals * Msf.SB->BlockSize;
  else
    FL.Length = divideCeil(Msf.SB->NumBlocks, 8);

  return FL;
}
