//===- BitCodeEnums.h - Core enums for the bitstream format -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines "core" bitstream enum values.
// It has been separated from the other header that defines bitstream enum
// values, BitCodes.h, to allow tools to track changes to the various
// bitstream and bitcode enums without needing to fully or partially build
// LLVM itself.
//
// The enum values defined in this file should be considered permanent.  If
// new features are added, they should have values added at the end of the
// respective lists.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITSTREAM_BITCODEENUMS_H
#define LLVM_BITSTREAM_BITCODEENUMS_H

namespace llvm {
/// Offsets of the 32-bit fields of bitstream wrapper header.
enum BitstreamWrapperHeader : unsigned {
  BWH_MagicField = 0 * 4,
  BWH_VersionField = 1 * 4,
  BWH_OffsetField = 2 * 4,
  BWH_SizeField = 3 * 4,
  BWH_CPUTypeField = 4 * 4,
  BWH_HeaderSize = 5 * 4
};

namespace bitc {
enum StandardWidths {
  BlockIDWidth = 8,   // We use VBR-8 for block IDs.
  CodeLenWidth = 4,   // Codelen are VBR-4.
  BlockSizeWidth = 32 // BlockSize up to 2^32 32-bit words = 16GB per block.
};

// The standard abbrev namespace always has a way to exit a block, enter a
// nested block, define abbrevs, and define an unabbreviated record.
enum FixedAbbrevIDs {
  END_BLOCK = 0, // Must be zero to guarantee termination for broken bitcode.
  ENTER_SUBBLOCK = 1,

  /// DEFINE_ABBREV - Defines an abbrev for the current block.  It consists
  /// of a vbr5 for # operand infos.  Each operand info is emitted with a
  /// single bit to indicate if it is a literal encoding.  If so, the value is
  /// emitted with a vbr8.  If not, the encoding is emitted as 3 bits followed
  /// by the info value as a vbr5 if needed.
  DEFINE_ABBREV = 2,

  // UNABBREV_RECORDs are emitted with a vbr6 for the record code, followed by
  // a vbr6 for the # operands, followed by vbr6's for each operand.
  UNABBREV_RECORD = 3,

  // This is not a code, this is a marker for the first abbrev assignment.
  FIRST_APPLICATION_ABBREV = 4
};

/// StandardBlockIDs - All bitcode files can optionally include a BLOCKINFO
/// block, which contains metadata about other blocks in the file.
enum StandardBlockIDs {
  /// BLOCKINFO_BLOCK is used to define metadata about blocks, for example,
  /// standard abbrevs that should be available to all blocks of a specified
  /// ID.
  BLOCKINFO_BLOCK_ID = 0,

  // Block IDs 1-7 are reserved for future expansion.
  FIRST_APPLICATION_BLOCKID = 8
};

/// BlockInfoCodes - The blockinfo block contains metadata about user-defined
/// blocks.
enum BlockInfoCodes {
  // DEFINE_ABBREV has magic semantics here, applying to the current SETBID'd
  // block, instead of the BlockInfo block.

  BLOCKINFO_CODE_SETBID = 1,       // SETBID: [blockid#]
  BLOCKINFO_CODE_BLOCKNAME = 2,    // BLOCKNAME: [name]
  BLOCKINFO_CODE_SETRECORDNAME = 3 // BLOCKINFO_CODE_SETRECORDNAME:
                                   //                             [id, name]
};

} // namespace bitc
} // namespace llvm

#endif
