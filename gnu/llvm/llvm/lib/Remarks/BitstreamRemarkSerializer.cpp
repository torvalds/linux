//===- BitstreamRemarkSerializer.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the implementation of the LLVM bitstream remark serializer
// using LLVM's bitstream writer.
//
//===----------------------------------------------------------------------===//

#include "llvm/Remarks/BitstreamRemarkSerializer.h"
#include "llvm/Remarks/Remark.h"
#include <optional>

using namespace llvm;
using namespace llvm::remarks;

BitstreamRemarkSerializerHelper::BitstreamRemarkSerializerHelper(
    BitstreamRemarkContainerType ContainerType)
    : Bitstream(Encoded), ContainerType(ContainerType) {}

static void push(SmallVectorImpl<uint64_t> &R, StringRef Str) {
  append_range(R, Str);
}

static void setRecordName(unsigned RecordID, BitstreamWriter &Bitstream,
                          SmallVectorImpl<uint64_t> &R, StringRef Str) {
  R.clear();
  R.push_back(RecordID);
  push(R, Str);
  Bitstream.EmitRecord(bitc::BLOCKINFO_CODE_SETRECORDNAME, R);
}

static void initBlock(unsigned BlockID, BitstreamWriter &Bitstream,
                      SmallVectorImpl<uint64_t> &R, StringRef Str) {
  R.clear();
  R.push_back(BlockID);
  Bitstream.EmitRecord(bitc::BLOCKINFO_CODE_SETBID, R);

  R.clear();
  push(R, Str);
  Bitstream.EmitRecord(bitc::BLOCKINFO_CODE_BLOCKNAME, R);
}

void BitstreamRemarkSerializerHelper::setupMetaBlockInfo() {
  // Setup the metadata block.
  initBlock(META_BLOCK_ID, Bitstream, R, MetaBlockName);

  // The container information.
  setRecordName(RECORD_META_CONTAINER_INFO, Bitstream, R,
                MetaContainerInfoName);

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(RECORD_META_CONTAINER_INFO));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Version.
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2));  // Type.
  RecordMetaContainerInfoAbbrevID =
      Bitstream.EmitBlockInfoAbbrev(META_BLOCK_ID, Abbrev);
}

void BitstreamRemarkSerializerHelper::setupMetaRemarkVersion() {
  setRecordName(RECORD_META_REMARK_VERSION, Bitstream, R,
                MetaRemarkVersionName);

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(RECORD_META_REMARK_VERSION));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Version.
  RecordMetaRemarkVersionAbbrevID =
      Bitstream.EmitBlockInfoAbbrev(META_BLOCK_ID, Abbrev);
}

void BitstreamRemarkSerializerHelper::emitMetaRemarkVersion(
    uint64_t RemarkVersion) {
  // The remark version is emitted only if we emit remarks.
  R.clear();
  R.push_back(RECORD_META_REMARK_VERSION);
  R.push_back(RemarkVersion);
  Bitstream.EmitRecordWithAbbrev(RecordMetaRemarkVersionAbbrevID, R);
}

void BitstreamRemarkSerializerHelper::setupMetaStrTab() {
  setRecordName(RECORD_META_STRTAB, Bitstream, R, MetaStrTabName);

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(RECORD_META_STRTAB));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Raw table.
  RecordMetaStrTabAbbrevID =
      Bitstream.EmitBlockInfoAbbrev(META_BLOCK_ID, Abbrev);
}

void BitstreamRemarkSerializerHelper::emitMetaStrTab(
    const StringTable &StrTab) {
  // The string table is not emitted if we emit remarks separately.
  R.clear();
  R.push_back(RECORD_META_STRTAB);

  // Serialize to a blob.
  std::string Buf;
  raw_string_ostream OS(Buf);
  StrTab.serialize(OS);
  StringRef Blob = OS.str();
  Bitstream.EmitRecordWithBlob(RecordMetaStrTabAbbrevID, R, Blob);
}

void BitstreamRemarkSerializerHelper::setupMetaExternalFile() {
  setRecordName(RECORD_META_EXTERNAL_FILE, Bitstream, R, MetaExternalFileName);

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(RECORD_META_EXTERNAL_FILE));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Filename.
  RecordMetaExternalFileAbbrevID =
      Bitstream.EmitBlockInfoAbbrev(META_BLOCK_ID, Abbrev);
}

void BitstreamRemarkSerializerHelper::emitMetaExternalFile(StringRef Filename) {
  // The external file is emitted only if we emit the separate metadata.
  R.clear();
  R.push_back(RECORD_META_EXTERNAL_FILE);
  Bitstream.EmitRecordWithBlob(RecordMetaExternalFileAbbrevID, R, Filename);
}

void BitstreamRemarkSerializerHelper::setupRemarkBlockInfo() {
  // Setup the remark block.
  initBlock(REMARK_BLOCK_ID, Bitstream, R, RemarkBlockName);

  // The header of a remark.
  {
    setRecordName(RECORD_REMARK_HEADER, Bitstream, R, RemarkHeaderName);

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(RECORD_REMARK_HEADER));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // Type
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Remark Name
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Pass name
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));   // Function name
    RecordRemarkHeaderAbbrevID =
        Bitstream.EmitBlockInfoAbbrev(REMARK_BLOCK_ID, Abbrev);
  }

  // The location of a remark.
  {
    setRecordName(RECORD_REMARK_DEBUG_LOC, Bitstream, R, RemarkDebugLocName);

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(RECORD_REMARK_DEBUG_LOC));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7));    // File
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Line
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Column
    RecordRemarkDebugLocAbbrevID =
        Bitstream.EmitBlockInfoAbbrev(REMARK_BLOCK_ID, Abbrev);
  }

  // The hotness of a remark.
  {
    setRecordName(RECORD_REMARK_HOTNESS, Bitstream, R, RemarkHotnessName);

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(RECORD_REMARK_HOTNESS));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Hotness
    RecordRemarkHotnessAbbrevID =
        Bitstream.EmitBlockInfoAbbrev(REMARK_BLOCK_ID, Abbrev);
  }

  // An argument entry with a debug location attached.
  {
    setRecordName(RECORD_REMARK_ARG_WITH_DEBUGLOC, Bitstream, R,
                  RemarkArgWithDebugLocName);

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(RECORD_REMARK_ARG_WITH_DEBUGLOC));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7));    // Key
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7));    // Value
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7));    // File
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Line
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Column
    RecordRemarkArgWithDebugLocAbbrevID =
        Bitstream.EmitBlockInfoAbbrev(REMARK_BLOCK_ID, Abbrev);
  }

  // An argument entry with no debug location attached.
  {
    setRecordName(RECORD_REMARK_ARG_WITHOUT_DEBUGLOC, Bitstream, R,
                  RemarkArgWithoutDebugLocName);

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(RECORD_REMARK_ARG_WITHOUT_DEBUGLOC));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7)); // Key
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 7)); // Value
    RecordRemarkArgWithoutDebugLocAbbrevID =
        Bitstream.EmitBlockInfoAbbrev(REMARK_BLOCK_ID, Abbrev);
  }
}

void BitstreamRemarkSerializerHelper::setupBlockInfo() {
  // Emit magic number.
  for (const char C : ContainerMagic)
    Bitstream.Emit(static_cast<unsigned>(C), 8);

  Bitstream.EnterBlockInfoBlock();

  // Setup the main metadata. Depending on the container type, we'll setup the
  // required records next.
  setupMetaBlockInfo();

  switch (ContainerType) {
  case BitstreamRemarkContainerType::SeparateRemarksMeta:
    // Needs a string table that the separate remark file is using.
    setupMetaStrTab();
    // Needs to know where the external remarks file is.
    setupMetaExternalFile();
    break;
  case BitstreamRemarkContainerType::SeparateRemarksFile:
    // Contains remarks: emit the version.
    setupMetaRemarkVersion();
    // Contains remarks: emit the remark abbrevs.
    setupRemarkBlockInfo();
    break;
  case BitstreamRemarkContainerType::Standalone:
    // Contains remarks: emit the version.
    setupMetaRemarkVersion();
    // Needs a string table.
    setupMetaStrTab();
    // Contains remarks: emit the remark abbrevs.
    setupRemarkBlockInfo();
    break;
  }

  Bitstream.ExitBlock();
}

void BitstreamRemarkSerializerHelper::emitMetaBlock(
    uint64_t ContainerVersion, std::optional<uint64_t> RemarkVersion,
    std::optional<const StringTable *> StrTab,
    std::optional<StringRef> Filename) {
  // Emit the meta block
  Bitstream.EnterSubblock(META_BLOCK_ID, 3);

  // The container version and type.
  R.clear();
  R.push_back(RECORD_META_CONTAINER_INFO);
  R.push_back(ContainerVersion);
  R.push_back(static_cast<uint64_t>(ContainerType));
  Bitstream.EmitRecordWithAbbrev(RecordMetaContainerInfoAbbrevID, R);

  switch (ContainerType) {
  case BitstreamRemarkContainerType::SeparateRemarksMeta:
    assert(StrTab != std::nullopt && *StrTab != nullptr);
    emitMetaStrTab(**StrTab);
    assert(Filename != std::nullopt);
    emitMetaExternalFile(*Filename);
    break;
  case BitstreamRemarkContainerType::SeparateRemarksFile:
    assert(RemarkVersion != std::nullopt);
    emitMetaRemarkVersion(*RemarkVersion);
    break;
  case BitstreamRemarkContainerType::Standalone:
    assert(RemarkVersion != std::nullopt);
    emitMetaRemarkVersion(*RemarkVersion);
    assert(StrTab != std::nullopt && *StrTab != nullptr);
    emitMetaStrTab(**StrTab);
    break;
  }

  Bitstream.ExitBlock();
}

void BitstreamRemarkSerializerHelper::emitRemarkBlock(const Remark &Remark,
                                                      StringTable &StrTab) {
  Bitstream.EnterSubblock(REMARK_BLOCK_ID, 4);

  R.clear();
  R.push_back(RECORD_REMARK_HEADER);
  R.push_back(static_cast<uint64_t>(Remark.RemarkType));
  R.push_back(StrTab.add(Remark.RemarkName).first);
  R.push_back(StrTab.add(Remark.PassName).first);
  R.push_back(StrTab.add(Remark.FunctionName).first);
  Bitstream.EmitRecordWithAbbrev(RecordRemarkHeaderAbbrevID, R);

  if (const std::optional<RemarkLocation> &Loc = Remark.Loc) {
    R.clear();
    R.push_back(RECORD_REMARK_DEBUG_LOC);
    R.push_back(StrTab.add(Loc->SourceFilePath).first);
    R.push_back(Loc->SourceLine);
    R.push_back(Loc->SourceColumn);
    Bitstream.EmitRecordWithAbbrev(RecordRemarkDebugLocAbbrevID, R);
  }

  if (std::optional<uint64_t> Hotness = Remark.Hotness) {
    R.clear();
    R.push_back(RECORD_REMARK_HOTNESS);
    R.push_back(*Hotness);
    Bitstream.EmitRecordWithAbbrev(RecordRemarkHotnessAbbrevID, R);
  }

  for (const Argument &Arg : Remark.Args) {
    R.clear();
    unsigned Key = StrTab.add(Arg.Key).first;
    unsigned Val = StrTab.add(Arg.Val).first;
    bool HasDebugLoc = Arg.Loc != std::nullopt;
    R.push_back(HasDebugLoc ? RECORD_REMARK_ARG_WITH_DEBUGLOC
                            : RECORD_REMARK_ARG_WITHOUT_DEBUGLOC);
    R.push_back(Key);
    R.push_back(Val);
    if (HasDebugLoc) {
      R.push_back(StrTab.add(Arg.Loc->SourceFilePath).first);
      R.push_back(Arg.Loc->SourceLine);
      R.push_back(Arg.Loc->SourceColumn);
    }
    Bitstream.EmitRecordWithAbbrev(HasDebugLoc
                                       ? RecordRemarkArgWithDebugLocAbbrevID
                                       : RecordRemarkArgWithoutDebugLocAbbrevID,
                                   R);
  }
  Bitstream.ExitBlock();
}

void BitstreamRemarkSerializerHelper::flushToStream(raw_ostream &OS) {
  OS.write(Encoded.data(), Encoded.size());
  Encoded.clear();
}

StringRef BitstreamRemarkSerializerHelper::getBuffer() {
  return StringRef(Encoded.data(), Encoded.size());
}

BitstreamRemarkSerializer::BitstreamRemarkSerializer(raw_ostream &OS,
                                                     SerializerMode Mode)
    : RemarkSerializer(Format::Bitstream, OS, Mode),
      Helper(BitstreamRemarkContainerType::SeparateRemarksFile) {
  assert(Mode == SerializerMode::Separate &&
         "For SerializerMode::Standalone, a pre-filled string table needs to "
         "be provided.");
  // We always use a string table with bitstream.
  StrTab.emplace();
}

BitstreamRemarkSerializer::BitstreamRemarkSerializer(raw_ostream &OS,
                                                     SerializerMode Mode,
                                                     StringTable StrTabIn)
    : RemarkSerializer(Format::Bitstream, OS, Mode),
      Helper(Mode == SerializerMode::Separate
                 ? BitstreamRemarkContainerType::SeparateRemarksFile
                 : BitstreamRemarkContainerType::Standalone) {
  StrTab = std::move(StrTabIn);
}

void BitstreamRemarkSerializer::emit(const Remark &Remark) {
  if (!DidSetUp) {
    // Emit the metadata that is embedded in the remark file.
    // If we're in standalone mode, serialize the string table as well.
    bool IsStandalone =
        Helper.ContainerType == BitstreamRemarkContainerType::Standalone;
    BitstreamMetaSerializer MetaSerializer(
        OS, Helper,
        IsStandalone ? &*StrTab
                     : std::optional<const StringTable *>(std::nullopt));
    MetaSerializer.emit();
    DidSetUp = true;
  }

  assert(DidSetUp &&
         "The Block info block and the meta block were not emitted yet.");
  Helper.emitRemarkBlock(Remark, *StrTab);

  Helper.flushToStream(OS);
}

std::unique_ptr<MetaSerializer> BitstreamRemarkSerializer::metaSerializer(
    raw_ostream &OS, std::optional<StringRef> ExternalFilename) {
  assert(Helper.ContainerType !=
         BitstreamRemarkContainerType::SeparateRemarksMeta);
  bool IsStandalone =
      Helper.ContainerType == BitstreamRemarkContainerType::Standalone;
  return std::make_unique<BitstreamMetaSerializer>(
      OS,
      IsStandalone ? BitstreamRemarkContainerType::Standalone
                   : BitstreamRemarkContainerType::SeparateRemarksMeta,
      &*StrTab, ExternalFilename);
}

void BitstreamMetaSerializer::emit() {
  Helper->setupBlockInfo();
  Helper->emitMetaBlock(CurrentContainerVersion, CurrentRemarkVersion, StrTab,
                        ExternalFilename);
  Helper->flushToStream(OS);
}
