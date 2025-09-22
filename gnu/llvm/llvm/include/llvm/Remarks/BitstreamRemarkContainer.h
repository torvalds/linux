//===-- BitstreamRemarkContainer.h - Container for remarks --------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides declarations for things used in the various types of
// remark containers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_BITSTREAMREMARKCONTAINER_H
#define LLVM_REMARKS_BITSTREAMREMARKCONTAINER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitCodes.h"
#include <cstdint>

namespace llvm {
namespace remarks {

/// The current version of the remark container.
/// Note: this is different from the version of the remark entry.
constexpr uint64_t CurrentContainerVersion = 0;
/// The magic number used for identifying remark blocks.
constexpr StringLiteral ContainerMagic("RMRK");

/// Type of the remark container.
/// The remark container has two modes:
/// * separate: the metadata is separate from the remarks and points to the
///             auxiliary file that contains the remarks.
/// * standalone: the metadata and the remarks are emitted together.
enum class BitstreamRemarkContainerType {
  /// The metadata emitted separately.
  /// This will contain the following:
  /// * Container version and type
  /// * String table
  /// * External file
  SeparateRemarksMeta,
  /// The remarks emitted separately.
  /// This will contain the following:
  /// * Container version and type
  /// * Remark version
  SeparateRemarksFile,
  /// Everything is emitted together.
  /// This will contain the following:
  /// * Container version and type
  /// * Remark version
  /// * String table
  Standalone,
  First = SeparateRemarksMeta,
  Last = Standalone,
};

/// The possible blocks that will be encountered in a bitstream remark
/// container.
enum BlockIDs {
  /// The metadata block is mandatory. It should always come after the
  /// BLOCKINFO_BLOCK, and contains metadata that should be used when parsing
  /// REMARK_BLOCKs.
  /// There should always be only one META_BLOCK.
  META_BLOCK_ID = bitc::FIRST_APPLICATION_BLOCKID,
  /// One remark entry is represented using a REMARK_BLOCK. There can be
  /// multiple REMARK_BLOCKs in the same file.
  REMARK_BLOCK_ID
};

constexpr StringRef MetaBlockName = StringRef("Meta", 4);
constexpr StringRef RemarkBlockName = StringRef("Remark", 6);

/// The possible records that can be encountered in the previously described
/// blocks.
enum RecordIDs {
  // Meta block records.
  RECORD_META_CONTAINER_INFO = 1,
  RECORD_META_REMARK_VERSION,
  RECORD_META_STRTAB,
  RECORD_META_EXTERNAL_FILE,
  // Remark block records.
  RECORD_REMARK_HEADER,
  RECORD_REMARK_DEBUG_LOC,
  RECORD_REMARK_HOTNESS,
  RECORD_REMARK_ARG_WITH_DEBUGLOC,
  RECORD_REMARK_ARG_WITHOUT_DEBUGLOC,
  // Helpers.
  RECORD_FIRST = RECORD_META_CONTAINER_INFO,
  RECORD_LAST = RECORD_REMARK_ARG_WITHOUT_DEBUGLOC
};

constexpr StringRef MetaContainerInfoName = StringRef("Container info", 14);
constexpr StringRef MetaRemarkVersionName = StringRef("Remark version", 14);
constexpr StringRef MetaStrTabName = StringRef("String table", 12);
constexpr StringRef MetaExternalFileName = StringRef("External File", 13);
constexpr StringRef RemarkHeaderName = StringRef("Remark header", 13);
constexpr StringRef RemarkDebugLocName = StringRef("Remark debug location", 21);
constexpr StringRef RemarkHotnessName = StringRef("Remark hotness", 14);
constexpr StringRef RemarkArgWithDebugLocName =
    StringRef("Argument with debug location", 28);
constexpr StringRef RemarkArgWithoutDebugLocName = StringRef("Argument", 8);

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_BITSTREAMREMARKCONTAINER_H
