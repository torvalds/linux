//===-- BitstreamRemarkSerializer.h - Bitstream serializer ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an implementation of the serializer using the LLVM
// Bitstream format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_BITSTREAMREMARKSERIALIZER_H
#define LLVM_REMARKS_BITSTREAMREMARKSERIALIZER_H

#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Remarks/BitstreamRemarkContainer.h"
#include "llvm/Remarks/RemarkSerializer.h"
#include <optional>

namespace llvm {
namespace remarks {

struct Remarks;

/// Serialize the remarks to LLVM bitstream.
/// This class provides ways to emit remarks in the LLVM bitstream format and
/// its associated metadata.
///
/// * The separate model:
///   Separate meta:        | Container info
///                         | String table
///                         | External file
///
///   Separate remarks:     | Container info
///                         | Remark version
///                         | Remark0
///                         | Remark1
///                         | Remark2
///                         | ...
///
/// * The standalone model: | Container info
///                         | String table
///                         | Remark version
///                         | Remark0
///                         | Remark1
///                         | Remark2
///                         | ...
///
struct BitstreamRemarkSerializerHelper {
  /// Buffer used for encoding the bitstream before writing it to the final
  /// stream.
  SmallVector<char, 1024> Encoded;
  /// Buffer used to construct records and pass to the bitstream writer.
  SmallVector<uint64_t, 64> R;
  /// The Bitstream writer.
  BitstreamWriter Bitstream;
  /// The type of the container we are serializing.
  BitstreamRemarkContainerType ContainerType;

  /// Abbrev IDs initialized in the block info block.
  /// Note: depending on the container type, some IDs might be uninitialized.
  /// Warning: When adding more abbrev IDs, make sure to update the
  /// BlockCodeSize (in the call to EnterSubblock).
  uint64_t RecordMetaContainerInfoAbbrevID = 0;
  uint64_t RecordMetaRemarkVersionAbbrevID = 0;
  uint64_t RecordMetaStrTabAbbrevID = 0;
  uint64_t RecordMetaExternalFileAbbrevID = 0;
  uint64_t RecordRemarkHeaderAbbrevID = 0;
  uint64_t RecordRemarkDebugLocAbbrevID = 0;
  uint64_t RecordRemarkHotnessAbbrevID = 0;
  uint64_t RecordRemarkArgWithDebugLocAbbrevID = 0;
  uint64_t RecordRemarkArgWithoutDebugLocAbbrevID = 0;

  BitstreamRemarkSerializerHelper(BitstreamRemarkContainerType ContainerType);

  // Disable copy and move: Bitstream points to Encoded, which needs special
  // handling during copy/move, but moving the vectors is probably useless
  // anyway.
  BitstreamRemarkSerializerHelper(const BitstreamRemarkSerializerHelper &) =
      delete;
  BitstreamRemarkSerializerHelper &
  operator=(const BitstreamRemarkSerializerHelper &) = delete;
  BitstreamRemarkSerializerHelper(BitstreamRemarkSerializerHelper &&) = delete;
  BitstreamRemarkSerializerHelper &
  operator=(BitstreamRemarkSerializerHelper &&) = delete;

  /// Set up the necessary block info entries according to the container type.
  void setupBlockInfo();

  /// Set up the block info for the metadata block.
  void setupMetaBlockInfo();
  /// The remark version in the metadata block.
  void setupMetaRemarkVersion();
  void emitMetaRemarkVersion(uint64_t RemarkVersion);
  /// The strtab in the metadata block.
  void setupMetaStrTab();
  void emitMetaStrTab(const StringTable &StrTab);
  /// The external file in the metadata block.
  void setupMetaExternalFile();
  void emitMetaExternalFile(StringRef Filename);

  /// The block info for the remarks block.
  void setupRemarkBlockInfo();

  /// Emit the metadata for the remarks.
  void emitMetaBlock(uint64_t ContainerVersion,
                     std::optional<uint64_t> RemarkVersion,
                     std::optional<const StringTable *> StrTab = std::nullopt,
                     std::optional<StringRef> Filename = std::nullopt);

  /// Emit a remark block. The string table is required.
  void emitRemarkBlock(const Remark &Remark, StringTable &StrTab);
  /// Finalize the writing to \p OS.
  void flushToStream(raw_ostream &OS);
  /// Finalize the writing to a buffer.
  /// The contents of the buffer remain valid for the lifetime of the object.
  /// Any call to any other function in this class will invalidate the buffer.
  StringRef getBuffer();
};

/// Implementation of the remark serializer using LLVM bitstream.
struct BitstreamRemarkSerializer : public RemarkSerializer {
  /// The file should contain:
  /// 1) The block info block that describes how to read the blocks.
  /// 2) The metadata block that contains various information about the remarks
  ///    in the file.
  /// 3) A number of remark blocks.

  /// We need to set up 1) and 2) first, so that we can emit 3) after. This flag
  /// is used to emit the first two blocks only once.
  bool DidSetUp = false;
  /// The helper to emit bitstream.
  BitstreamRemarkSerializerHelper Helper;

  /// Construct a serializer that will create its own string table.
  BitstreamRemarkSerializer(raw_ostream &OS, SerializerMode Mode);
  /// Construct a serializer with a pre-filled string table.
  BitstreamRemarkSerializer(raw_ostream &OS, SerializerMode Mode,
                            StringTable StrTab);

  /// Emit a remark to the stream. This also emits the metadata associated to
  /// the remarks based on the SerializerMode specified at construction.
  /// This writes the serialized output to the provided stream.
  void emit(const Remark &Remark) override;
  /// The metadata serializer associated to this remark serializer. Based on the
  /// container type of the current serializer, the container type of the
  /// metadata serializer will change.
  std::unique_ptr<MetaSerializer> metaSerializer(
      raw_ostream &OS,
      std::optional<StringRef> ExternalFilename = std::nullopt) override;

  static bool classof(const RemarkSerializer *S) {
    return S->SerializerFormat == Format::Bitstream;
  }
};

/// Serializer of metadata for bitstream remarks.
struct BitstreamMetaSerializer : public MetaSerializer {
  /// This class can be used with [1] a pre-constructed
  /// BitstreamRemarkSerializerHelper, or with [2] one that is owned by the meta
  /// serializer. In case of [1], we need to be able to store a reference to the
  /// object, while in case of [2] we need to store the whole object.
  std::optional<BitstreamRemarkSerializerHelper> TmpHelper;
  /// The actual helper, that can point to \p TmpHelper or to an external helper
  /// object.
  BitstreamRemarkSerializerHelper *Helper = nullptr;

  std::optional<const StringTable *> StrTab;
  std::optional<StringRef> ExternalFilename;

  /// Create a new meta serializer based on \p ContainerType.
  BitstreamMetaSerializer(
      raw_ostream &OS, BitstreamRemarkContainerType ContainerType,
      std::optional<const StringTable *> StrTab = std::nullopt,
      std::optional<StringRef> ExternalFilename = std::nullopt)
      : MetaSerializer(OS), TmpHelper(std::nullopt), Helper(nullptr),
        StrTab(StrTab), ExternalFilename(ExternalFilename) {
    TmpHelper.emplace(ContainerType);
    Helper = &*TmpHelper;
  }

  /// Create a new meta serializer based on a previously built \p Helper.
  BitstreamMetaSerializer(
      raw_ostream &OS, BitstreamRemarkSerializerHelper &Helper,
      std::optional<const StringTable *> StrTab = std::nullopt,
      std::optional<StringRef> ExternalFilename = std::nullopt)
      : MetaSerializer(OS), TmpHelper(std::nullopt), Helper(&Helper),
        StrTab(StrTab), ExternalFilename(ExternalFilename) {}

  void emit() override;
};

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_BITSTREAMREMARKSERIALIZER_H
