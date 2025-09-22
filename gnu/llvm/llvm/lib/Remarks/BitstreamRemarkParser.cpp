//===- BitstreamRemarkParser.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides utility methods used by clients that want to use the
// parser for remark diagnostics in LLVM.
//
//===----------------------------------------------------------------------===//

#include "llvm/Remarks/BitstreamRemarkParser.h"
#include "BitstreamRemarkParser.h"
#include "llvm/Remarks/Remark.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <optional>

using namespace llvm;
using namespace llvm::remarks;

static Error unknownRecord(const char *BlockName, unsigned RecordID) {
  return createStringError(
      std::make_error_code(std::errc::illegal_byte_sequence),
      "Error while parsing %s: unknown record entry (%lu).", BlockName,
      RecordID);
}

static Error malformedRecord(const char *BlockName, const char *RecordName) {
  return createStringError(
      std::make_error_code(std::errc::illegal_byte_sequence),
      "Error while parsing %s: malformed record entry (%s).", BlockName,
      RecordName);
}

BitstreamMetaParserHelper::BitstreamMetaParserHelper(
    BitstreamCursor &Stream, BitstreamBlockInfo &BlockInfo)
    : Stream(Stream), BlockInfo(BlockInfo) {}

/// Parse a record and fill in the fields in the parser.
static Error parseRecord(BitstreamMetaParserHelper &Parser, unsigned Code) {
  BitstreamCursor &Stream = Parser.Stream;
  // Note: 2 is used here because it's the max number of fields we have per
  // record.
  SmallVector<uint64_t, 2> Record;
  StringRef Blob;
  Expected<unsigned> RecordID = Stream.readRecord(Code, Record, &Blob);
  if (!RecordID)
    return RecordID.takeError();

  switch (*RecordID) {
  case RECORD_META_CONTAINER_INFO: {
    if (Record.size() != 2)
      return malformedRecord("BLOCK_META", "RECORD_META_CONTAINER_INFO");
    Parser.ContainerVersion = Record[0];
    Parser.ContainerType = Record[1];
    break;
  }
  case RECORD_META_REMARK_VERSION: {
    if (Record.size() != 1)
      return malformedRecord("BLOCK_META", "RECORD_META_REMARK_VERSION");
    Parser.RemarkVersion = Record[0];
    break;
  }
  case RECORD_META_STRTAB: {
    if (Record.size() != 0)
      return malformedRecord("BLOCK_META", "RECORD_META_STRTAB");
    Parser.StrTabBuf = Blob;
    break;
  }
  case RECORD_META_EXTERNAL_FILE: {
    if (Record.size() != 0)
      return malformedRecord("BLOCK_META", "RECORD_META_EXTERNAL_FILE");
    Parser.ExternalFilePath = Blob;
    break;
  }
  default:
    return unknownRecord("BLOCK_META", *RecordID);
  }
  return Error::success();
}

BitstreamRemarkParserHelper::BitstreamRemarkParserHelper(
    BitstreamCursor &Stream)
    : Stream(Stream) {}

/// Parse a record and fill in the fields in the parser.
static Error parseRecord(BitstreamRemarkParserHelper &Parser, unsigned Code) {
  BitstreamCursor &Stream = Parser.Stream;
  // Note: 5 is used here because it's the max number of fields we have per
  // record.
  SmallVector<uint64_t, 5> Record;
  StringRef Blob;
  Expected<unsigned> RecordID = Stream.readRecord(Code, Record, &Blob);
  if (!RecordID)
    return RecordID.takeError();

  switch (*RecordID) {
  case RECORD_REMARK_HEADER: {
    if (Record.size() != 4)
      return malformedRecord("BLOCK_REMARK", "RECORD_REMARK_HEADER");
    Parser.Type = Record[0];
    Parser.RemarkNameIdx = Record[1];
    Parser.PassNameIdx = Record[2];
    Parser.FunctionNameIdx = Record[3];
    break;
  }
  case RECORD_REMARK_DEBUG_LOC: {
    if (Record.size() != 3)
      return malformedRecord("BLOCK_REMARK", "RECORD_REMARK_DEBUG_LOC");
    Parser.SourceFileNameIdx = Record[0];
    Parser.SourceLine = Record[1];
    Parser.SourceColumn = Record[2];
    break;
  }
  case RECORD_REMARK_HOTNESS: {
    if (Record.size() != 1)
      return malformedRecord("BLOCK_REMARK", "RECORD_REMARK_HOTNESS");
    Parser.Hotness = Record[0];
    break;
  }
  case RECORD_REMARK_ARG_WITH_DEBUGLOC: {
    if (Record.size() != 5)
      return malformedRecord("BLOCK_REMARK", "RECORD_REMARK_ARG_WITH_DEBUGLOC");
    // Create a temporary argument. Use that as a valid memory location for this
    // argument entry.
    Parser.TmpArgs.emplace_back();
    Parser.TmpArgs.back().KeyIdx = Record[0];
    Parser.TmpArgs.back().ValueIdx = Record[1];
    Parser.TmpArgs.back().SourceFileNameIdx = Record[2];
    Parser.TmpArgs.back().SourceLine = Record[3];
    Parser.TmpArgs.back().SourceColumn = Record[4];
    Parser.Args =
        ArrayRef<BitstreamRemarkParserHelper::Argument>(Parser.TmpArgs);
    break;
  }
  case RECORD_REMARK_ARG_WITHOUT_DEBUGLOC: {
    if (Record.size() != 2)
      return malformedRecord("BLOCK_REMARK",
                             "RECORD_REMARK_ARG_WITHOUT_DEBUGLOC");
    // Create a temporary argument. Use that as a valid memory location for this
    // argument entry.
    Parser.TmpArgs.emplace_back();
    Parser.TmpArgs.back().KeyIdx = Record[0];
    Parser.TmpArgs.back().ValueIdx = Record[1];
    Parser.Args =
        ArrayRef<BitstreamRemarkParserHelper::Argument>(Parser.TmpArgs);
    break;
  }
  default:
    return unknownRecord("BLOCK_REMARK", *RecordID);
  }
  return Error::success();
}

template <typename T>
static Error parseBlock(T &ParserHelper, unsigned BlockID,
                        const char *BlockName) {
  BitstreamCursor &Stream = ParserHelper.Stream;
  Expected<BitstreamEntry> Next = Stream.advance();
  if (!Next)
    return Next.takeError();
  if (Next->Kind != BitstreamEntry::SubBlock || Next->ID != BlockID)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing %s: expecting [ENTER_SUBBLOCK, %s, ...].",
        BlockName, BlockName);
  if (Stream.EnterSubBlock(BlockID))
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while entering %s.", BlockName);

  // Stop when there is nothing to read anymore or when we encounter an
  // END_BLOCK.
  while (!Stream.AtEndOfStream()) {
    Next = Stream.advance();
    if (!Next)
      return Next.takeError();
    switch (Next->Kind) {
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Error:
    case BitstreamEntry::SubBlock:
      return createStringError(
          std::make_error_code(std::errc::illegal_byte_sequence),
          "Error while parsing %s: expecting records.", BlockName);
    case BitstreamEntry::Record:
      if (Error E = parseRecord(ParserHelper, Next->ID))
        return E;
      continue;
    }
  }
  // If we're here, it means we didn't get an END_BLOCK yet, but we're at the
  // end of the stream. In this case, error.
  return createStringError(
      std::make_error_code(std::errc::illegal_byte_sequence),
      "Error while parsing %s: unterminated block.", BlockName);
}

Error BitstreamMetaParserHelper::parse() {
  return parseBlock(*this, META_BLOCK_ID, "META_BLOCK");
}

Error BitstreamRemarkParserHelper::parse() {
  return parseBlock(*this, REMARK_BLOCK_ID, "REMARK_BLOCK");
}

BitstreamParserHelper::BitstreamParserHelper(StringRef Buffer)
    : Stream(Buffer) {}

Expected<std::array<char, 4>> BitstreamParserHelper::parseMagic() {
  std::array<char, 4> Result;
  for (unsigned i = 0; i < 4; ++i)
    if (Expected<unsigned> R = Stream.Read(8))
      Result[i] = *R;
    else
      return R.takeError();
  return Result;
}

Error BitstreamParserHelper::parseBlockInfoBlock() {
  Expected<BitstreamEntry> Next = Stream.advance();
  if (!Next)
    return Next.takeError();
  if (Next->Kind != BitstreamEntry::SubBlock ||
      Next->ID != llvm::bitc::BLOCKINFO_BLOCK_ID)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCKINFO_BLOCK: expecting [ENTER_SUBBLOCK, "
        "BLOCKINFO_BLOCK, ...].");

  Expected<std::optional<BitstreamBlockInfo>> MaybeBlockInfo =
      Stream.ReadBlockInfoBlock();
  if (!MaybeBlockInfo)
    return MaybeBlockInfo.takeError();

  if (!*MaybeBlockInfo)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCKINFO_BLOCK.");

  BlockInfo = **MaybeBlockInfo;

  Stream.setBlockInfo(&BlockInfo);
  return Error::success();
}

static Expected<bool> isBlock(BitstreamCursor &Stream, unsigned BlockID) {
  bool Result = false;
  uint64_t PreviousBitNo = Stream.GetCurrentBitNo();
  Expected<BitstreamEntry> Next = Stream.advance();
  if (!Next)
    return Next.takeError();
  switch (Next->Kind) {
  case BitstreamEntry::SubBlock:
    // Check for the block id.
    Result = Next->ID == BlockID;
    break;
  case BitstreamEntry::Error:
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Unexpected error while parsing bitstream.");
  default:
    Result = false;
    break;
  }
  if (Error E = Stream.JumpToBit(PreviousBitNo))
    return std::move(E);
  return Result;
}

Expected<bool> BitstreamParserHelper::isMetaBlock() {
  return isBlock(Stream, META_BLOCK_ID);
}

Expected<bool> BitstreamParserHelper::isRemarkBlock() {
  return isBlock(Stream, META_BLOCK_ID);
}

static Error validateMagicNumber(StringRef MagicNumber) {
  if (MagicNumber != remarks::ContainerMagic)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Unknown magic number: expecting %s, got %.4s.",
                             remarks::ContainerMagic.data(), MagicNumber.data());
  return Error::success();
}

static Error advanceToMetaBlock(BitstreamParserHelper &Helper) {
  Expected<std::array<char, 4>> MagicNumber = Helper.parseMagic();
  if (!MagicNumber)
    return MagicNumber.takeError();
  if (Error E = validateMagicNumber(
          StringRef(MagicNumber->data(), MagicNumber->size())))
    return E;
  if (Error E = Helper.parseBlockInfoBlock())
    return E;
  Expected<bool> isMetaBlock = Helper.isMetaBlock();
  if (!isMetaBlock)
    return isMetaBlock.takeError();
  if (!*isMetaBlock)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Expecting META_BLOCK after the BLOCKINFO_BLOCK.");
  return Error::success();
}

Expected<std::unique_ptr<BitstreamRemarkParser>>
remarks::createBitstreamParserFromMeta(
    StringRef Buf, std::optional<ParsedStringTable> StrTab,
    std::optional<StringRef> ExternalFilePrependPath) {
  BitstreamParserHelper Helper(Buf);
  Expected<std::array<char, 4>> MagicNumber = Helper.parseMagic();
  if (!MagicNumber)
    return MagicNumber.takeError();

  if (Error E = validateMagicNumber(
          StringRef(MagicNumber->data(), MagicNumber->size())))
    return std::move(E);

  auto Parser =
      StrTab ? std::make_unique<BitstreamRemarkParser>(Buf, std::move(*StrTab))
             : std::make_unique<BitstreamRemarkParser>(Buf);

  if (ExternalFilePrependPath)
    Parser->ExternalFilePrependPath = std::string(*ExternalFilePrependPath);

  return std::move(Parser);
}

Expected<std::unique_ptr<Remark>> BitstreamRemarkParser::next() {
  if (ParserHelper.atEndOfStream())
    return make_error<EndOfFileError>();

  if (!ReadyToParseRemarks) {
    if (Error E = parseMeta())
      return std::move(E);
    ReadyToParseRemarks = true;
  }

  return parseRemark();
}

Error BitstreamRemarkParser::parseMeta() {
  // Advance and to the meta block.
  if (Error E = advanceToMetaBlock(ParserHelper))
    return E;

  BitstreamMetaParserHelper MetaHelper(ParserHelper.Stream,
                                       ParserHelper.BlockInfo);
  if (Error E = MetaHelper.parse())
    return E;

  if (Error E = processCommonMeta(MetaHelper))
    return E;

  switch (ContainerType) {
  case BitstreamRemarkContainerType::Standalone:
    return processStandaloneMeta(MetaHelper);
  case BitstreamRemarkContainerType::SeparateRemarksFile:
    return processSeparateRemarksFileMeta(MetaHelper);
  case BitstreamRemarkContainerType::SeparateRemarksMeta:
    return processSeparateRemarksMetaMeta(MetaHelper);
  }
  llvm_unreachable("Unknown BitstreamRemarkContainerType enum");
}

Error BitstreamRemarkParser::processCommonMeta(
    BitstreamMetaParserHelper &Helper) {
  if (std::optional<uint64_t> Version = Helper.ContainerVersion)
    ContainerVersion = *Version;
  else
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_META: missing container version.");

  if (std::optional<uint8_t> Type = Helper.ContainerType) {
    // Always >= BitstreamRemarkContainerType::First since it's unsigned.
    if (*Type > static_cast<uint8_t>(BitstreamRemarkContainerType::Last))
      return createStringError(
          std::make_error_code(std::errc::illegal_byte_sequence),
          "Error while parsing BLOCK_META: invalid container type.");

    ContainerType = static_cast<BitstreamRemarkContainerType>(*Type);
  } else
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_META: missing container type.");

  return Error::success();
}

static Error processStrTab(BitstreamRemarkParser &P,
                           std::optional<StringRef> StrTabBuf) {
  if (!StrTabBuf)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_META: missing string table.");
  // Parse and assign the string table.
  P.StrTab.emplace(*StrTabBuf);
  return Error::success();
}

static Error processRemarkVersion(BitstreamRemarkParser &P,
                                  std::optional<uint64_t> RemarkVersion) {
  if (!RemarkVersion)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_META: missing remark version.");
  P.RemarkVersion = *RemarkVersion;
  return Error::success();
}

Error BitstreamRemarkParser::processExternalFilePath(
    std::optional<StringRef> ExternalFilePath) {
  if (!ExternalFilePath)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_META: missing external file path.");

  SmallString<80> FullPath(ExternalFilePrependPath);
  sys::path::append(FullPath, *ExternalFilePath);

  // External file: open the external file, parse it, check if its metadata
  // matches the one from the separate metadata, then replace the current parser
  // with the one parsing the remarks.
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(FullPath);
  if (std::error_code EC = BufferOrErr.getError())
    return createFileError(FullPath, EC);

  TmpRemarkBuffer = std::move(*BufferOrErr);

  // Don't try to parse the file if it's empty.
  if (TmpRemarkBuffer->getBufferSize() == 0)
    return make_error<EndOfFileError>();

  // Create a separate parser used for parsing the separate file.
  ParserHelper = BitstreamParserHelper(TmpRemarkBuffer->getBuffer());
  // Advance and check until we can parse the meta block.
  if (Error E = advanceToMetaBlock(ParserHelper))
    return E;
  // Parse the meta from the separate file.
  // Note: here we overwrite the BlockInfo with the one from the file. This will
  // be used to parse the rest of the file.
  BitstreamMetaParserHelper SeparateMetaHelper(ParserHelper.Stream,
                                               ParserHelper.BlockInfo);
  if (Error E = SeparateMetaHelper.parse())
    return E;

  uint64_t PreviousContainerVersion = ContainerVersion;
  if (Error E = processCommonMeta(SeparateMetaHelper))
    return E;

  if (ContainerType != BitstreamRemarkContainerType::SeparateRemarksFile)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing external file's BLOCK_META: wrong container "
        "type.");

  if (PreviousContainerVersion != ContainerVersion)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing external file's BLOCK_META: mismatching versions: "
        "original meta: %lu, external file meta: %lu.",
        PreviousContainerVersion, ContainerVersion);

  // Process the meta from the separate file.
  return processSeparateRemarksFileMeta(SeparateMetaHelper);
}

Error BitstreamRemarkParser::processStandaloneMeta(
    BitstreamMetaParserHelper &Helper) {
  if (Error E = processStrTab(*this, Helper.StrTabBuf))
    return E;
  return processRemarkVersion(*this, Helper.RemarkVersion);
}

Error BitstreamRemarkParser::processSeparateRemarksFileMeta(
    BitstreamMetaParserHelper &Helper) {
  return processRemarkVersion(*this, Helper.RemarkVersion);
}

Error BitstreamRemarkParser::processSeparateRemarksMetaMeta(
    BitstreamMetaParserHelper &Helper) {
  if (Error E = processStrTab(*this, Helper.StrTabBuf))
    return E;
  return processExternalFilePath(Helper.ExternalFilePath);
}

Expected<std::unique_ptr<Remark>> BitstreamRemarkParser::parseRemark() {
  BitstreamRemarkParserHelper RemarkHelper(ParserHelper.Stream);
  if (Error E = RemarkHelper.parse())
    return std::move(E);

  return processRemark(RemarkHelper);
}

Expected<std::unique_ptr<Remark>>
BitstreamRemarkParser::processRemark(BitstreamRemarkParserHelper &Helper) {
  std::unique_ptr<Remark> Result = std::make_unique<Remark>();
  Remark &R = *Result;

  if (StrTab == std::nullopt)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Error while parsing BLOCK_REMARK: missing string table.");

  if (!Helper.Type)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_REMARK: missing remark type.");

  // Always >= Type::First since it's unsigned.
  if (*Helper.Type > static_cast<uint8_t>(Type::Last))
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_REMARK: unknown remark type.");

  R.RemarkType = static_cast<Type>(*Helper.Type);

  if (!Helper.RemarkNameIdx)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_REMARK: missing remark name.");

  if (Expected<StringRef> RemarkName = (*StrTab)[*Helper.RemarkNameIdx])
    R.RemarkName = *RemarkName;
  else
    return RemarkName.takeError();

  if (!Helper.PassNameIdx)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_REMARK: missing remark pass.");

  if (Expected<StringRef> PassName = (*StrTab)[*Helper.PassNameIdx])
    R.PassName = *PassName;
  else
    return PassName.takeError();

  if (!Helper.FunctionNameIdx)
    return createStringError(
        std::make_error_code(std::errc::illegal_byte_sequence),
        "Error while parsing BLOCK_REMARK: missing remark function name.");
  if (Expected<StringRef> FunctionName = (*StrTab)[*Helper.FunctionNameIdx])
    R.FunctionName = *FunctionName;
  else
    return FunctionName.takeError();

  if (Helper.SourceFileNameIdx && Helper.SourceLine && Helper.SourceColumn) {
    Expected<StringRef> SourceFileName = (*StrTab)[*Helper.SourceFileNameIdx];
    if (!SourceFileName)
      return SourceFileName.takeError();
    R.Loc.emplace();
    R.Loc->SourceFilePath = *SourceFileName;
    R.Loc->SourceLine = *Helper.SourceLine;
    R.Loc->SourceColumn = *Helper.SourceColumn;
  }

  if (Helper.Hotness)
    R.Hotness = *Helper.Hotness;

  if (!Helper.Args)
    return std::move(Result);

  for (const BitstreamRemarkParserHelper::Argument &Arg : *Helper.Args) {
    if (!Arg.KeyIdx)
      return createStringError(
          std::make_error_code(std::errc::illegal_byte_sequence),
          "Error while parsing BLOCK_REMARK: missing key in remark argument.");
    if (!Arg.ValueIdx)
      return createStringError(
          std::make_error_code(std::errc::illegal_byte_sequence),
          "Error while parsing BLOCK_REMARK: missing value in remark "
          "argument.");

    // We have at least a key and a value, create an entry.
    R.Args.emplace_back();

    if (Expected<StringRef> Key = (*StrTab)[*Arg.KeyIdx])
      R.Args.back().Key = *Key;
    else
      return Key.takeError();

    if (Expected<StringRef> Value = (*StrTab)[*Arg.ValueIdx])
      R.Args.back().Val = *Value;
    else
      return Value.takeError();

    if (Arg.SourceFileNameIdx && Arg.SourceLine && Arg.SourceColumn) {
      if (Expected<StringRef> SourceFileName =
              (*StrTab)[*Arg.SourceFileNameIdx]) {
        R.Args.back().Loc.emplace();
        R.Args.back().Loc->SourceFilePath = *SourceFileName;
        R.Args.back().Loc->SourceLine = *Arg.SourceLine;
        R.Args.back().Loc->SourceColumn = *Arg.SourceColumn;
      } else
        return SourceFileName.takeError();
    }
  }

  return std::move(Result);
}
