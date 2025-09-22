//===- SerializedDiagnosticReader.cpp - Reads diagnostics -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/SerializedDiagnosticReader.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Frontend/SerializedDiagnostics.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/ManagedStatic.h"
#include <cstdint>
#include <optional>
#include <system_error>

using namespace clang;
using namespace serialized_diags;

std::error_code SerializedDiagnosticReader::readDiagnostics(StringRef File) {
  // Open the diagnostics file.
  FileSystemOptions FO;
  FileManager FileMgr(FO);

  auto Buffer = FileMgr.getBufferForFile(File);
  if (!Buffer)
    return SDError::CouldNotLoad;

  llvm::BitstreamCursor Stream(**Buffer);
  std::optional<llvm::BitstreamBlockInfo> BlockInfo;

  if (Stream.AtEndOfStream())
    return SDError::InvalidSignature;

  // Sniff for the signature.
  for (unsigned char C : {'D', 'I', 'A', 'G'}) {
    if (Expected<llvm::SimpleBitstreamCursor::word_t> Res = Stream.Read(8)) {
      if (Res.get() == C)
        continue;
    } else {
      // FIXME this drops the error on the floor.
      consumeError(Res.takeError());
    }
    return SDError::InvalidSignature;
  }

  // Read the top level blocks.
  while (!Stream.AtEndOfStream()) {
    if (Expected<unsigned> Res = Stream.ReadCode()) {
      if (Res.get() != llvm::bitc::ENTER_SUBBLOCK)
        return SDError::InvalidDiagnostics;
    } else {
      // FIXME this drops the error on the floor.
      consumeError(Res.takeError());
      return SDError::InvalidDiagnostics;
    }

    std::error_code EC;
    Expected<unsigned> MaybeSubBlockID = Stream.ReadSubBlockID();
    if (!MaybeSubBlockID) {
      // FIXME this drops the error on the floor.
      consumeError(MaybeSubBlockID.takeError());
      return SDError::InvalidDiagnostics;
    }

    switch (MaybeSubBlockID.get()) {
    case llvm::bitc::BLOCKINFO_BLOCK_ID: {
      Expected<std::optional<llvm::BitstreamBlockInfo>> MaybeBlockInfo =
          Stream.ReadBlockInfoBlock();
      if (!MaybeBlockInfo) {
        // FIXME this drops the error on the floor.
        consumeError(MaybeBlockInfo.takeError());
        return SDError::InvalidDiagnostics;
      }
      BlockInfo = std::move(MaybeBlockInfo.get());
    }
      if (!BlockInfo)
        return SDError::MalformedBlockInfoBlock;
      Stream.setBlockInfo(&*BlockInfo);
      continue;
    case BLOCK_META:
      if ((EC = readMetaBlock(Stream)))
        return EC;
      continue;
    case BLOCK_DIAG:
      if ((EC = readDiagnosticBlock(Stream)))
        return EC;
      continue;
    default:
      if (llvm::Error Err = Stream.SkipBlock()) {
        // FIXME this drops the error on the floor.
        consumeError(std::move(Err));
        return SDError::MalformedTopLevelBlock;
      }
      continue;
    }
  }
  return {};
}

enum class SerializedDiagnosticReader::Cursor {
  Record = 1,
  BlockEnd,
  BlockBegin
};

llvm::ErrorOr<SerializedDiagnosticReader::Cursor>
SerializedDiagnosticReader::skipUntilRecordOrBlock(
    llvm::BitstreamCursor &Stream, unsigned &BlockOrRecordID) {
  BlockOrRecordID = 0;

  while (!Stream.AtEndOfStream()) {
    unsigned Code;
    if (Expected<unsigned> Res = Stream.ReadCode())
      Code = Res.get();
    else
      return llvm::errorToErrorCode(Res.takeError());

    if (Code >= static_cast<unsigned>(llvm::bitc::FIRST_APPLICATION_ABBREV)) {
      // We found a record.
      BlockOrRecordID = Code;
      return Cursor::Record;
    }
    switch (static_cast<llvm::bitc::FixedAbbrevIDs>(Code)) {
    case llvm::bitc::ENTER_SUBBLOCK:
      if (Expected<unsigned> Res = Stream.ReadSubBlockID())
        BlockOrRecordID = Res.get();
      else
        return llvm::errorToErrorCode(Res.takeError());
      return Cursor::BlockBegin;

    case llvm::bitc::END_BLOCK:
      if (Stream.ReadBlockEnd())
        return SDError::InvalidDiagnostics;
      return Cursor::BlockEnd;

    case llvm::bitc::DEFINE_ABBREV:
      if (llvm::Error Err = Stream.ReadAbbrevRecord())
        return llvm::errorToErrorCode(std::move(Err));
      continue;

    case llvm::bitc::UNABBREV_RECORD:
      return SDError::UnsupportedConstruct;

    case llvm::bitc::FIRST_APPLICATION_ABBREV:
      llvm_unreachable("Unexpected abbrev id.");
    }
  }

  return SDError::InvalidDiagnostics;
}

std::error_code
SerializedDiagnosticReader::readMetaBlock(llvm::BitstreamCursor &Stream) {
  if (llvm::Error Err =
          Stream.EnterSubBlock(clang::serialized_diags::BLOCK_META)) {
    // FIXME this drops the error on the floor.
    consumeError(std::move(Err));
    return SDError::MalformedMetadataBlock;
  }

  bool VersionChecked = false;

  while (true) {
    unsigned BlockOrCode = 0;
    llvm::ErrorOr<Cursor> Res = skipUntilRecordOrBlock(Stream, BlockOrCode);
    if (!Res)
      Res.getError();

    switch (Res.get()) {
    case Cursor::Record:
      break;
    case Cursor::BlockBegin:
      if (llvm::Error Err = Stream.SkipBlock()) {
        // FIXME this drops the error on the floor.
        consumeError(std::move(Err));
        return SDError::MalformedMetadataBlock;
      }
      [[fallthrough]];
    case Cursor::BlockEnd:
      if (!VersionChecked)
        return SDError::MissingVersion;
      return {};
    }

    SmallVector<uint64_t, 1> Record;
    Expected<unsigned> MaybeRecordID = Stream.readRecord(BlockOrCode, Record);
    if (!MaybeRecordID)
      return errorToErrorCode(MaybeRecordID.takeError());
    unsigned RecordID = MaybeRecordID.get();

    if (RecordID == RECORD_VERSION) {
      if (Record.size() < 1)
        return SDError::MissingVersion;
      if (Record[0] > VersionNumber)
        return SDError::VersionMismatch;
      VersionChecked = true;
    }
  }
}

std::error_code
SerializedDiagnosticReader::readDiagnosticBlock(llvm::BitstreamCursor &Stream) {
  if (llvm::Error Err =
          Stream.EnterSubBlock(clang::serialized_diags::BLOCK_DIAG)) {
    // FIXME this drops the error on the floor.
    consumeError(std::move(Err));
    return SDError::MalformedDiagnosticBlock;
  }

  std::error_code EC;
  if ((EC = visitStartOfDiagnostic()))
    return EC;

  SmallVector<uint64_t, 16> Record;
  while (true) {
    unsigned BlockOrCode = 0;
    llvm::ErrorOr<Cursor> Res = skipUntilRecordOrBlock(Stream, BlockOrCode);
    if (!Res)
      Res.getError();

    switch (Res.get()) {
    case Cursor::BlockBegin:
      // The only blocks we care about are subdiagnostics.
      if (BlockOrCode == serialized_diags::BLOCK_DIAG) {
        if ((EC = readDiagnosticBlock(Stream)))
          return EC;
      } else if (llvm::Error Err = Stream.SkipBlock()) {
        // FIXME this drops the error on the floor.
        consumeError(std::move(Err));
        return SDError::MalformedSubBlock;
      }
      continue;
    case Cursor::BlockEnd:
      if ((EC = visitEndOfDiagnostic()))
        return EC;
      return {};
    case Cursor::Record:
      break;
    }

    // Read the record.
    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecID =
        Stream.readRecord(BlockOrCode, Record, &Blob);
    if (!MaybeRecID)
      return errorToErrorCode(MaybeRecID.takeError());
    unsigned RecID = MaybeRecID.get();

    if (RecID < serialized_diags::RECORD_FIRST ||
        RecID > serialized_diags::RECORD_LAST)
      continue;

    switch ((RecordIDs)RecID) {
    case RECORD_CATEGORY:
      // A category has ID and name size.
      if (Record.size() != 2)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitCategoryRecord(Record[0], Blob)))
        return EC;
      continue;
    case RECORD_DIAG:
      // A diagnostic has severity, location (4), category, flag, and message
      // size.
      if (Record.size() != 8)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitDiagnosticRecord(
               Record[0], Location(Record[1], Record[2], Record[3], Record[4]),
               Record[5], Record[6], Blob)))
        return EC;
      continue;
    case RECORD_DIAG_FLAG:
      // A diagnostic flag has ID and name size.
      if (Record.size() != 2)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitDiagFlagRecord(Record[0], Blob)))
        return EC;
      continue;
    case RECORD_FILENAME:
      // A filename has ID, size, timestamp, and name size. The size and
      // timestamp are legacy fields that are always zero these days.
      if (Record.size() != 4)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitFilenameRecord(Record[0], Record[1], Record[2], Blob)))
        return EC;
      continue;
    case RECORD_FIXIT:
      // A fixit has two locations (4 each) and message size.
      if (Record.size() != 9)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitFixitRecord(
               Location(Record[0], Record[1], Record[2], Record[3]),
               Location(Record[4], Record[5], Record[6], Record[7]), Blob)))
        return EC;
      continue;
    case RECORD_SOURCE_RANGE:
      // A source range is two locations (4 each).
      if (Record.size() != 8)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitSourceRangeRecord(
               Location(Record[0], Record[1], Record[2], Record[3]),
               Location(Record[4], Record[5], Record[6], Record[7]))))
        return EC;
      continue;
    case RECORD_VERSION:
      // A version is just a number.
      if (Record.size() != 1)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitVersionRecord(Record[0])))
        return EC;
      continue;
    }
  }
}

namespace {

class SDErrorCategoryType final : public std::error_category {
  const char *name() const noexcept override {
    return "clang.serialized_diags";
  }

  std::string message(int IE) const override {
    auto E = static_cast<SDError>(IE);
    switch (E) {
    case SDError::CouldNotLoad:
      return "Failed to open diagnostics file";
    case SDError::InvalidSignature:
      return "Invalid diagnostics signature";
    case SDError::InvalidDiagnostics:
      return "Parse error reading diagnostics";
    case SDError::MalformedTopLevelBlock:
      return "Malformed block at top-level of diagnostics";
    case SDError::MalformedSubBlock:
      return "Malformed sub-block in a diagnostic";
    case SDError::MalformedBlockInfoBlock:
      return "Malformed BlockInfo block";
    case SDError::MalformedMetadataBlock:
      return "Malformed Metadata block";
    case SDError::MalformedDiagnosticBlock:
      return "Malformed Diagnostic block";
    case SDError::MalformedDiagnosticRecord:
      return "Malformed Diagnostic record";
    case SDError::MissingVersion:
      return "No version provided in diagnostics";
    case SDError::VersionMismatch:
      return "Unsupported diagnostics version";
    case SDError::UnsupportedConstruct:
      return "Bitcode constructs that are not supported in diagnostics appear";
    case SDError::HandlerFailed:
      return "Generic error occurred while handling a record";
    }
    llvm_unreachable("Unknown error type!");
  }
};

} // namespace

static llvm::ManagedStatic<SDErrorCategoryType> ErrorCategory;
const std::error_category &clang::serialized_diags::SDErrorCategory() {
  return *ErrorCategory;
}
