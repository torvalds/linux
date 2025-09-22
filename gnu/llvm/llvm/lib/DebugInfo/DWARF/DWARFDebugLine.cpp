//===- DWARFDebugLine.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <utility>

using namespace llvm;
using namespace dwarf;

using FileLineInfoKind = DILineInfoSpecifier::FileLineInfoKind;

namespace {

struct ContentDescriptor {
  dwarf::LineNumberEntryFormat Type;
  dwarf::Form Form;
};

using ContentDescriptors = SmallVector<ContentDescriptor, 4>;

} // end anonymous namespace

static bool versionIsSupported(uint16_t Version) {
  return Version >= 2 && Version <= 5;
}

void DWARFDebugLine::ContentTypeTracker::trackContentType(
    dwarf::LineNumberEntryFormat ContentType) {
  switch (ContentType) {
  case dwarf::DW_LNCT_timestamp:
    HasModTime = true;
    break;
  case dwarf::DW_LNCT_size:
    HasLength = true;
    break;
  case dwarf::DW_LNCT_MD5:
    HasMD5 = true;
    break;
  case dwarf::DW_LNCT_LLVM_source:
    HasSource = true;
    break;
  default:
    // We only care about values we consider optional, and new values may be
    // added in the vendor extension range, so we do not match exhaustively.
    break;
  }
}

DWARFDebugLine::Prologue::Prologue() { clear(); }

bool DWARFDebugLine::Prologue::hasFileAtIndex(uint64_t FileIndex) const {
  uint16_t DwarfVersion = getVersion();
  assert(DwarfVersion != 0 &&
         "line table prologue has no dwarf version information");
  if (DwarfVersion >= 5)
    return FileIndex < FileNames.size();
  return FileIndex != 0 && FileIndex <= FileNames.size();
}

std::optional<uint64_t>
DWARFDebugLine::Prologue::getLastValidFileIndex() const {
  if (FileNames.empty())
    return std::nullopt;
  uint16_t DwarfVersion = getVersion();
  assert(DwarfVersion != 0 &&
         "line table prologue has no dwarf version information");
  // In DWARF v5 the file names are 0-indexed.
  if (DwarfVersion >= 5)
    return FileNames.size() - 1;
  return FileNames.size();
}

const llvm::DWARFDebugLine::FileNameEntry &
DWARFDebugLine::Prologue::getFileNameEntry(uint64_t Index) const {
  uint16_t DwarfVersion = getVersion();
  assert(DwarfVersion != 0 &&
         "line table prologue has no dwarf version information");
  // In DWARF v5 the file names are 0-indexed.
  if (DwarfVersion >= 5)
    return FileNames[Index];
  return FileNames[Index - 1];
}

void DWARFDebugLine::Prologue::clear() {
  TotalLength = PrologueLength = 0;
  SegSelectorSize = 0;
  MinInstLength = MaxOpsPerInst = DefaultIsStmt = LineBase = LineRange = 0;
  OpcodeBase = 0;
  FormParams = dwarf::FormParams({0, 0, DWARF32});
  ContentTypes = ContentTypeTracker();
  StandardOpcodeLengths.clear();
  IncludeDirectories.clear();
  FileNames.clear();
}

void DWARFDebugLine::Prologue::dump(raw_ostream &OS,
                                    DIDumpOptions DumpOptions) const {
  if (!totalLengthIsValid())
    return;
  int OffsetDumpWidth = 2 * dwarf::getDwarfOffsetByteSize(FormParams.Format);
  OS << "Line table prologue:\n"
     << format("    total_length: 0x%0*" PRIx64 "\n", OffsetDumpWidth,
               TotalLength)
     << "          format: " << dwarf::FormatString(FormParams.Format) << "\n"
     << format("         version: %u\n", getVersion());
  if (!versionIsSupported(getVersion()))
    return;
  if (getVersion() >= 5)
    OS << format("    address_size: %u\n", getAddressSize())
       << format(" seg_select_size: %u\n", SegSelectorSize);
  OS << format(" prologue_length: 0x%0*" PRIx64 "\n", OffsetDumpWidth,
               PrologueLength)
     << format(" min_inst_length: %u\n", MinInstLength)
     << format(getVersion() >= 4 ? "max_ops_per_inst: %u\n" : "", MaxOpsPerInst)
     << format(" default_is_stmt: %u\n", DefaultIsStmt)
     << format("       line_base: %i\n", LineBase)
     << format("      line_range: %u\n", LineRange)
     << format("     opcode_base: %u\n", OpcodeBase);

  for (uint32_t I = 0; I != StandardOpcodeLengths.size(); ++I)
    OS << formatv("standard_opcode_lengths[{0}] = {1}\n",
                  static_cast<dwarf::LineNumberOps>(I + 1),
                  StandardOpcodeLengths[I]);

  if (!IncludeDirectories.empty()) {
    // DWARF v5 starts directory indexes at 0.
    uint32_t DirBase = getVersion() >= 5 ? 0 : 1;
    for (uint32_t I = 0; I != IncludeDirectories.size(); ++I) {
      OS << format("include_directories[%3u] = ", I + DirBase);
      IncludeDirectories[I].dump(OS, DumpOptions);
      OS << '\n';
    }
  }

  if (!FileNames.empty()) {
    // DWARF v5 starts file indexes at 0.
    uint32_t FileBase = getVersion() >= 5 ? 0 : 1;
    for (uint32_t I = 0; I != FileNames.size(); ++I) {
      const FileNameEntry &FileEntry = FileNames[I];
      OS <<   format("file_names[%3u]:\n", I + FileBase);
      OS <<          "           name: ";
      FileEntry.Name.dump(OS, DumpOptions);
      OS << '\n'
         <<   format("      dir_index: %" PRIu64 "\n", FileEntry.DirIdx);
      if (ContentTypes.HasMD5)
        OS <<        "   md5_checksum: " << FileEntry.Checksum.digest() << '\n';
      if (ContentTypes.HasModTime)
        OS << format("       mod_time: 0x%8.8" PRIx64 "\n", FileEntry.ModTime);
      if (ContentTypes.HasLength)
        OS << format("         length: 0x%8.8" PRIx64 "\n", FileEntry.Length);
      if (ContentTypes.HasSource) {
        auto Source = FileEntry.Source.getAsCString();
        if (!Source)
          consumeError(Source.takeError());
        else if ((*Source)[0]) {
          OS << "         source: ";
          FileEntry.Source.dump(OS, DumpOptions);
          OS << '\n';
        }
      }
    }
  }
}

// Parse v2-v4 directory and file tables.
static Error
parseV2DirFileTables(const DWARFDataExtractor &DebugLineData,
                     uint64_t *OffsetPtr,
                     DWARFDebugLine::ContentTypeTracker &ContentTypes,
                     std::vector<DWARFFormValue> &IncludeDirectories,
                     std::vector<DWARFDebugLine::FileNameEntry> &FileNames) {
  while (true) {
    Error Err = Error::success();
    StringRef S = DebugLineData.getCStrRef(OffsetPtr, &Err);
    if (Err) {
      consumeError(std::move(Err));
      return createStringError(errc::invalid_argument,
                               "include directories table was not null "
                               "terminated before the end of the prologue");
    }
    if (S.empty())
      break;
    DWARFFormValue Dir =
        DWARFFormValue::createFromPValue(dwarf::DW_FORM_string, S.data());
    IncludeDirectories.push_back(Dir);
  }

  ContentTypes.HasModTime = true;
  ContentTypes.HasLength = true;

  while (true) {
    Error Err = Error::success();
    StringRef Name = DebugLineData.getCStrRef(OffsetPtr, &Err);
    if (!Err && Name.empty())
      break;

    DWARFDebugLine::FileNameEntry FileEntry;
    FileEntry.Name =
        DWARFFormValue::createFromPValue(dwarf::DW_FORM_string, Name.data());
    FileEntry.DirIdx = DebugLineData.getULEB128(OffsetPtr, &Err);
    FileEntry.ModTime = DebugLineData.getULEB128(OffsetPtr, &Err);
    FileEntry.Length = DebugLineData.getULEB128(OffsetPtr, &Err);

    if (Err) {
      consumeError(std::move(Err));
      return createStringError(
          errc::invalid_argument,
          "file names table was not null terminated before "
          "the end of the prologue");
    }
    FileNames.push_back(FileEntry);
  }

  return Error::success();
}

// Parse v5 directory/file entry content descriptions.
// Returns the descriptors, or an error if we did not find a path or ran off
// the end of the prologue.
static llvm::Expected<ContentDescriptors>
parseV5EntryFormat(const DWARFDataExtractor &DebugLineData, uint64_t *OffsetPtr,
                   DWARFDebugLine::ContentTypeTracker *ContentTypes) {
  Error Err = Error::success();
  ContentDescriptors Descriptors;
  int FormatCount = DebugLineData.getU8(OffsetPtr, &Err);
  bool HasPath = false;
  for (int I = 0; I != FormatCount && !Err; ++I) {
    ContentDescriptor Descriptor;
    Descriptor.Type =
        dwarf::LineNumberEntryFormat(DebugLineData.getULEB128(OffsetPtr, &Err));
    Descriptor.Form = dwarf::Form(DebugLineData.getULEB128(OffsetPtr, &Err));
    if (Descriptor.Type == dwarf::DW_LNCT_path)
      HasPath = true;
    if (ContentTypes)
      ContentTypes->trackContentType(Descriptor.Type);
    Descriptors.push_back(Descriptor);
  }

  if (Err)
    return createStringError(errc::invalid_argument,
                             "failed to parse entry content descriptors: %s",
                             toString(std::move(Err)).c_str());

  if (!HasPath)
    return createStringError(errc::invalid_argument,
                             "failed to parse entry content descriptions"
                             " because no path was found");
  return Descriptors;
}

static Error
parseV5DirFileTables(const DWARFDataExtractor &DebugLineData,
                     uint64_t *OffsetPtr, const dwarf::FormParams &FormParams,
                     const DWARFContext &Ctx, const DWARFUnit *U,
                     DWARFDebugLine::ContentTypeTracker &ContentTypes,
                     std::vector<DWARFFormValue> &IncludeDirectories,
                     std::vector<DWARFDebugLine::FileNameEntry> &FileNames) {
  // Get the directory entry description.
  llvm::Expected<ContentDescriptors> DirDescriptors =
      parseV5EntryFormat(DebugLineData, OffsetPtr, nullptr);
  if (!DirDescriptors)
    return DirDescriptors.takeError();

  // Get the directory entries, according to the format described above.
  uint64_t DirEntryCount = DebugLineData.getULEB128(OffsetPtr);
  for (uint64_t I = 0; I != DirEntryCount; ++I) {
    for (auto Descriptor : *DirDescriptors) {
      DWARFFormValue Value(Descriptor.Form);
      switch (Descriptor.Type) {
      case DW_LNCT_path:
        if (!Value.extractValue(DebugLineData, OffsetPtr, FormParams, &Ctx, U))
          return createStringError(errc::invalid_argument,
                                   "failed to parse directory entry because "
                                   "extracting the form value failed");
        IncludeDirectories.push_back(Value);
        break;
      default:
        if (!Value.skipValue(DebugLineData, OffsetPtr, FormParams))
          return createStringError(errc::invalid_argument,
                                   "failed to parse directory entry because "
                                   "skipping the form value failed");
      }
    }
  }

  // Get the file entry description.
  llvm::Expected<ContentDescriptors> FileDescriptors =
      parseV5EntryFormat(DebugLineData, OffsetPtr, &ContentTypes);
  if (!FileDescriptors)
    return FileDescriptors.takeError();

  // Get the file entries, according to the format described above.
  uint64_t FileEntryCount = DebugLineData.getULEB128(OffsetPtr);
  for (uint64_t I = 0; I != FileEntryCount; ++I) {
    DWARFDebugLine::FileNameEntry FileEntry;
    for (auto Descriptor : *FileDescriptors) {
      DWARFFormValue Value(Descriptor.Form);
      if (!Value.extractValue(DebugLineData, OffsetPtr, FormParams, &Ctx, U))
        return createStringError(errc::invalid_argument,
                                 "failed to parse file entry because "
                                 "extracting the form value failed");
      switch (Descriptor.Type) {
      case DW_LNCT_path:
        FileEntry.Name = Value;
        break;
      case DW_LNCT_LLVM_source:
        FileEntry.Source = Value;
        break;
      case DW_LNCT_directory_index:
        FileEntry.DirIdx = *Value.getAsUnsignedConstant();
        break;
      case DW_LNCT_timestamp:
        FileEntry.ModTime = *Value.getAsUnsignedConstant();
        break;
      case DW_LNCT_size:
        FileEntry.Length = *Value.getAsUnsignedConstant();
        break;
      case DW_LNCT_MD5:
        if (!Value.getAsBlock() || Value.getAsBlock()->size() != 16)
          return createStringError(
              errc::invalid_argument,
              "failed to parse file entry because the MD5 hash is invalid");
        std::uninitialized_copy_n(Value.getAsBlock()->begin(), 16,
                                  FileEntry.Checksum.begin());
        break;
      default:
        break;
      }
    }
    FileNames.push_back(FileEntry);
  }
  return Error::success();
}

uint64_t DWARFDebugLine::Prologue::getLength() const {
  uint64_t Length = PrologueLength + sizeofTotalLength() +
                    sizeof(getVersion()) + sizeofPrologueLength();
  if (getVersion() >= 5)
    Length += 2; // Address + Segment selector sizes.
  return Length;
}

Error DWARFDebugLine::Prologue::parse(
    DWARFDataExtractor DebugLineData, uint64_t *OffsetPtr,
    function_ref<void(Error)> RecoverableErrorHandler, const DWARFContext &Ctx,
    const DWARFUnit *U) {
  const uint64_t PrologueOffset = *OffsetPtr;

  clear();
  DataExtractor::Cursor Cursor(*OffsetPtr);
  std::tie(TotalLength, FormParams.Format) =
      DebugLineData.getInitialLength(Cursor);

  DebugLineData =
      DWARFDataExtractor(DebugLineData, Cursor.tell() + TotalLength);
  FormParams.Version = DebugLineData.getU16(Cursor);
  if (Cursor && !versionIsSupported(getVersion())) {
    // Treat this error as unrecoverable - we cannot be sure what any of
    // the data represents including the length field, so cannot skip it or make
    // any reasonable assumptions.
    *OffsetPtr = Cursor.tell();
    return createStringError(
        errc::not_supported,
        "parsing line table prologue at offset 0x%8.8" PRIx64
        ": unsupported version %" PRIu16,
        PrologueOffset, getVersion());
  }

  if (getVersion() >= 5) {
    FormParams.AddrSize = DebugLineData.getU8(Cursor);
    const uint8_t DataAddrSize = DebugLineData.getAddressSize();
    const uint8_t PrologueAddrSize = getAddressSize();
    if (Cursor) {
      if (DataAddrSize == 0) {
        if (PrologueAddrSize != 4 && PrologueAddrSize != 8) {
          RecoverableErrorHandler(createStringError(
              errc::not_supported,
              "parsing line table prologue at offset 0x%8.8" PRIx64
              ": invalid address size %" PRIu8,
              PrologueOffset, PrologueAddrSize));
        }
      } else if (DataAddrSize != PrologueAddrSize) {
        RecoverableErrorHandler(createStringError(
            errc::not_supported,
            "parsing line table prologue at offset 0x%8.8" PRIx64 ": address "
            "size %" PRIu8 " doesn't match architecture address size %" PRIu8,
            PrologueOffset, PrologueAddrSize, DataAddrSize));
      }
    }
    SegSelectorSize = DebugLineData.getU8(Cursor);
  }

  PrologueLength =
      DebugLineData.getRelocatedValue(Cursor, sizeofPrologueLength());
  const uint64_t EndPrologueOffset = PrologueLength + Cursor.tell();
  DebugLineData = DWARFDataExtractor(DebugLineData, EndPrologueOffset);
  MinInstLength = DebugLineData.getU8(Cursor);
  if (getVersion() >= 4)
    MaxOpsPerInst = DebugLineData.getU8(Cursor);
  DefaultIsStmt = DebugLineData.getU8(Cursor);
  LineBase = DebugLineData.getU8(Cursor);
  LineRange = DebugLineData.getU8(Cursor);
  OpcodeBase = DebugLineData.getU8(Cursor);

  if (Cursor && OpcodeBase == 0) {
    // If the opcode base is 0, we cannot read the standard opcode lengths (of
    // which there are supposed to be one fewer than the opcode base). Assume
    // there are no standard opcodes and continue parsing.
    RecoverableErrorHandler(createStringError(
        errc::invalid_argument,
        "parsing line table prologue at offset 0x%8.8" PRIx64
        " found opcode base of 0. Assuming no standard opcodes",
        PrologueOffset));
  } else if (Cursor) {
    StandardOpcodeLengths.reserve(OpcodeBase - 1);
    for (uint32_t I = 1; I < OpcodeBase; ++I) {
      uint8_t OpLen = DebugLineData.getU8(Cursor);
      StandardOpcodeLengths.push_back(OpLen);
    }
  }

  *OffsetPtr = Cursor.tell();
  // A corrupt file name or directory table does not prevent interpretation of
  // the main line program, so check the cursor state now so that its errors can
  // be handled separately.
  if (!Cursor)
    return createStringError(
        errc::invalid_argument,
        "parsing line table prologue at offset 0x%8.8" PRIx64 ": %s",
        PrologueOffset, toString(Cursor.takeError()).c_str());

  Error E =
      getVersion() >= 5
          ? parseV5DirFileTables(DebugLineData, OffsetPtr, FormParams, Ctx, U,
                                 ContentTypes, IncludeDirectories, FileNames)
          : parseV2DirFileTables(DebugLineData, OffsetPtr, ContentTypes,
                                 IncludeDirectories, FileNames);
  if (E) {
    RecoverableErrorHandler(joinErrors(
        createStringError(
            errc::invalid_argument,
            "parsing line table prologue at 0x%8.8" PRIx64
            " found an invalid directory or file table description at"
            " 0x%8.8" PRIx64,
            PrologueOffset, *OffsetPtr),
        std::move(E)));
    return Error::success();
  }

  assert(*OffsetPtr <= EndPrologueOffset);
  if (*OffsetPtr != EndPrologueOffset) {
    RecoverableErrorHandler(createStringError(
        errc::invalid_argument,
        "unknown data in line table prologue at offset 0x%8.8" PRIx64
        ": parsing ended (at offset 0x%8.8" PRIx64
        ") before reaching the prologue end at offset 0x%8.8" PRIx64,
        PrologueOffset, *OffsetPtr, EndPrologueOffset));
  }
  return Error::success();
}

DWARFDebugLine::Row::Row(bool DefaultIsStmt) { reset(DefaultIsStmt); }

void DWARFDebugLine::Row::postAppend() {
  Discriminator = 0;
  BasicBlock = false;
  PrologueEnd = false;
  EpilogueBegin = false;
}

void DWARFDebugLine::Row::reset(bool DefaultIsStmt) {
  Address.Address = 0;
  Address.SectionIndex = object::SectionedAddress::UndefSection;
  Line = 1;
  Column = 0;
  File = 1;
  Isa = 0;
  Discriminator = 0;
  IsStmt = DefaultIsStmt;
  OpIndex = 0;
  BasicBlock = false;
  EndSequence = false;
  PrologueEnd = false;
  EpilogueBegin = false;
}

void DWARFDebugLine::Row::dumpTableHeader(raw_ostream &OS, unsigned Indent) {
  OS.indent(Indent)
      << "Address            Line   Column File   ISA Discriminator OpIndex "
         "Flags\n";
  OS.indent(Indent)
      << "------------------ ------ ------ ------ --- ------------- ------- "
         "-------------\n";
}

void DWARFDebugLine::Row::dump(raw_ostream &OS) const {
  OS << format("0x%16.16" PRIx64 " %6u %6u", Address.Address, Line, Column)
     << format(" %6u %3u %13u %7u ", File, Isa, Discriminator, OpIndex)
     << (IsStmt ? " is_stmt" : "") << (BasicBlock ? " basic_block" : "")
     << (PrologueEnd ? " prologue_end" : "")
     << (EpilogueBegin ? " epilogue_begin" : "")
     << (EndSequence ? " end_sequence" : "") << '\n';
}

DWARFDebugLine::Sequence::Sequence() { reset(); }

void DWARFDebugLine::Sequence::reset() {
  LowPC = 0;
  HighPC = 0;
  SectionIndex = object::SectionedAddress::UndefSection;
  FirstRowIndex = 0;
  LastRowIndex = 0;
  Empty = true;
}

DWARFDebugLine::LineTable::LineTable() { clear(); }

void DWARFDebugLine::LineTable::dump(raw_ostream &OS,
                                     DIDumpOptions DumpOptions) const {
  Prologue.dump(OS, DumpOptions);

  if (!Rows.empty()) {
    OS << '\n';
    Row::dumpTableHeader(OS, 0);
    for (const Row &R : Rows) {
      R.dump(OS);
    }
  }

  // Terminate the table with a final blank line to clearly delineate it from
  // later dumps.
  OS << '\n';
}

void DWARFDebugLine::LineTable::clear() {
  Prologue.clear();
  Rows.clear();
  Sequences.clear();
}

DWARFDebugLine::ParsingState::ParsingState(
    struct LineTable *LT, uint64_t TableOffset,
    function_ref<void(Error)> ErrorHandler)
    : LineTable(LT), LineTableOffset(TableOffset), ErrorHandler(ErrorHandler) {
  resetRowAndSequence();
}

void DWARFDebugLine::ParsingState::resetRowAndSequence() {
  Row.reset(LineTable->Prologue.DefaultIsStmt);
  Sequence.reset();
}

void DWARFDebugLine::ParsingState::appendRowToMatrix() {
  unsigned RowNumber = LineTable->Rows.size();
  if (Sequence.Empty) {
    // Record the beginning of instruction sequence.
    Sequence.Empty = false;
    Sequence.LowPC = Row.Address.Address;
    Sequence.FirstRowIndex = RowNumber;
  }
  LineTable->appendRow(Row);
  if (Row.EndSequence) {
    // Record the end of instruction sequence.
    Sequence.HighPC = Row.Address.Address;
    Sequence.LastRowIndex = RowNumber + 1;
    Sequence.SectionIndex = Row.Address.SectionIndex;
    if (Sequence.isValid())
      LineTable->appendSequence(Sequence);
    Sequence.reset();
  }
  Row.postAppend();
}

const DWARFDebugLine::LineTable *
DWARFDebugLine::getLineTable(uint64_t Offset) const {
  LineTableConstIter Pos = LineTableMap.find(Offset);
  if (Pos != LineTableMap.end())
    return &Pos->second;
  return nullptr;
}

Expected<const DWARFDebugLine::LineTable *> DWARFDebugLine::getOrParseLineTable(
    DWARFDataExtractor &DebugLineData, uint64_t Offset, const DWARFContext &Ctx,
    const DWARFUnit *U, function_ref<void(Error)> RecoverableErrorHandler) {
  if (!DebugLineData.isValidOffset(Offset))
    return createStringError(errc::invalid_argument, "offset 0x%8.8" PRIx64
                       " is not a valid debug line section offset",
                       Offset);

  std::pair<LineTableIter, bool> Pos =
      LineTableMap.insert(LineTableMapTy::value_type(Offset, LineTable()));
  LineTable *LT = &Pos.first->second;
  if (Pos.second) {
    if (Error Err =
            LT->parse(DebugLineData, &Offset, Ctx, U, RecoverableErrorHandler))
      return std::move(Err);
    return LT;
  }
  return LT;
}

void DWARFDebugLine::clearLineTable(uint64_t Offset) {
  LineTableMap.erase(Offset);
}

static StringRef getOpcodeName(uint8_t Opcode, uint8_t OpcodeBase) {
  assert(Opcode != 0);
  if (Opcode < OpcodeBase)
    return LNStandardString(Opcode);
  return "special";
}

DWARFDebugLine::ParsingState::AddrOpIndexDelta
DWARFDebugLine::ParsingState::advanceAddrOpIndex(uint64_t OperationAdvance,
                                                 uint8_t Opcode,
                                                 uint64_t OpcodeOffset) {
  StringRef OpcodeName = getOpcodeName(Opcode, LineTable->Prologue.OpcodeBase);
  // For versions less than 4, the MaxOpsPerInst member is set to 0, as the
  // maximum_operations_per_instruction field wasn't introduced until DWARFv4.
  // Don't warn about bad values in this situation.
  if (ReportAdvanceAddrProblem && LineTable->Prologue.getVersion() >= 4 &&
      LineTable->Prologue.MaxOpsPerInst == 0)
    ErrorHandler(createStringError(
        errc::invalid_argument,
        "line table program at offset 0x%8.8" PRIx64
        " contains a %s opcode at offset 0x%8.8" PRIx64
        ", but the prologue maximum_operations_per_instruction value is 0"
        ", which is invalid. Assuming a value of 1 instead",
        LineTableOffset, OpcodeName.data(), OpcodeOffset));
  // Although we are able to correctly parse line number programs with
  // MaxOpsPerInst > 1, the rest of DWARFDebugLine and its
  // users have not been updated to handle line information for all operations
  // in a multi-operation instruction, so warn about potentially incorrect
  // results.
  if (ReportAdvanceAddrProblem && LineTable->Prologue.MaxOpsPerInst > 1)
    ErrorHandler(createStringError(
        errc::not_supported,
        "line table program at offset 0x%8.8" PRIx64
        " contains a %s opcode at offset 0x%8.8" PRIx64
        ", but the prologue maximum_operations_per_instruction value is %" PRId8
        ", which is experimentally supported, so line number information "
        "may be incorrect",
        LineTableOffset, OpcodeName.data(), OpcodeOffset,
        LineTable->Prologue.MaxOpsPerInst));
  if (ReportAdvanceAddrProblem && LineTable->Prologue.MinInstLength == 0)
    ErrorHandler(
        createStringError(errc::invalid_argument,
                          "line table program at offset 0x%8.8" PRIx64
                          " contains a %s opcode at offset 0x%8.8" PRIx64
                          ", but the prologue minimum_instruction_length value "
                          "is 0, which prevents any address advancing",
                          LineTableOffset, OpcodeName.data(), OpcodeOffset));
  ReportAdvanceAddrProblem = false;

  // Advances the address and op_index according to DWARFv5, section 6.2.5.1:
  //
  // new address = address +
  //   minimum_instruction_length *
  //   ((op_index + operation advance) / maximum_operations_per_instruction)
  //
  // new op_index =
  //   (op_index + operation advance) % maximum_operations_per_instruction

  // For versions less than 4, the MaxOpsPerInst member is set to 0, as the
  // maximum_operations_per_instruction field wasn't introduced until DWARFv4.
  uint8_t MaxOpsPerInst =
      std::max(LineTable->Prologue.MaxOpsPerInst, uint8_t{1});

  uint64_t AddrOffset = ((Row.OpIndex + OperationAdvance) / MaxOpsPerInst) *
                        LineTable->Prologue.MinInstLength;
  Row.Address.Address += AddrOffset;

  uint8_t PrevOpIndex = Row.OpIndex;
  Row.OpIndex = (Row.OpIndex + OperationAdvance) % MaxOpsPerInst;
  int16_t OpIndexDelta = static_cast<int16_t>(Row.OpIndex) - PrevOpIndex;

  return {AddrOffset, OpIndexDelta};
}

DWARFDebugLine::ParsingState::OpcodeAdvanceResults
DWARFDebugLine::ParsingState::advanceForOpcode(uint8_t Opcode,
                                               uint64_t OpcodeOffset) {
  assert(Opcode == DW_LNS_const_add_pc ||
         Opcode >= LineTable->Prologue.OpcodeBase);
  if (ReportBadLineRange && LineTable->Prologue.LineRange == 0) {
    StringRef OpcodeName =
        getOpcodeName(Opcode, LineTable->Prologue.OpcodeBase);
    ErrorHandler(
        createStringError(errc::not_supported,
                          "line table program at offset 0x%8.8" PRIx64
                          " contains a %s opcode at offset 0x%8.8" PRIx64
                          ", but the prologue line_range value is 0. The "
                          "address and line will not be adjusted",
                          LineTableOffset, OpcodeName.data(), OpcodeOffset));
    ReportBadLineRange = false;
  }

  uint8_t OpcodeValue = Opcode;
  if (Opcode == DW_LNS_const_add_pc)
    OpcodeValue = 255;
  uint8_t AdjustedOpcode = OpcodeValue - LineTable->Prologue.OpcodeBase;
  uint64_t OperationAdvance =
      LineTable->Prologue.LineRange != 0
          ? AdjustedOpcode / LineTable->Prologue.LineRange
          : 0;
  AddrOpIndexDelta Advance =
      advanceAddrOpIndex(OperationAdvance, Opcode, OpcodeOffset);
  return {Advance.AddrOffset, Advance.OpIndexDelta, AdjustedOpcode};
}

DWARFDebugLine::ParsingState::SpecialOpcodeDelta
DWARFDebugLine::ParsingState::handleSpecialOpcode(uint8_t Opcode,
                                                  uint64_t OpcodeOffset) {
  // A special opcode value is chosen based on the amount that needs
  // to be added to the line and address registers. The maximum line
  // increment for a special opcode is the value of the line_base
  // field in the header, plus the value of the line_range field,
  // minus 1 (line base + line range - 1). If the desired line
  // increment is greater than the maximum line increment, a standard
  // opcode must be used instead of a special opcode. The "address
  // advance" is calculated by dividing the desired address increment
  // by the minimum_instruction_length field from the header. The
  // special opcode is then calculated using the following formula:
  //
  //  opcode = (desired line increment - line_base) +
  //           (line_range * address advance) + opcode_base
  //
  // If the resulting opcode is greater than 255, a standard opcode
  // must be used instead.
  //
  // To decode a special opcode, subtract the opcode_base from the
  // opcode itself to give the adjusted opcode. The amount to
  // increment the address register is the result of the adjusted
  // opcode divided by the line_range multiplied by the
  // minimum_instruction_length field from the header. That is:
  //
  //  address increment = (adjusted opcode / line_range) *
  //                      minimum_instruction_length
  //
  // The amount to increment the line register is the line_base plus
  // the result of the adjusted opcode modulo the line_range. That is:
  //
  // line increment = line_base + (adjusted opcode % line_range)

  DWARFDebugLine::ParsingState::OpcodeAdvanceResults AddrAdvanceResult =
      advanceForOpcode(Opcode, OpcodeOffset);
  int32_t LineOffset = 0;
  if (LineTable->Prologue.LineRange != 0)
    LineOffset =
        LineTable->Prologue.LineBase +
        (AddrAdvanceResult.AdjustedOpcode % LineTable->Prologue.LineRange);
  Row.Line += LineOffset;
  return {AddrAdvanceResult.AddrDelta, LineOffset,
          AddrAdvanceResult.OpIndexDelta};
}

/// Parse a ULEB128 using the specified \p Cursor. \returns the parsed value on
/// success, or std::nullopt if \p Cursor is in a failing state.
template <typename T>
static std::optional<T> parseULEB128(DWARFDataExtractor &Data,
                                     DataExtractor::Cursor &Cursor) {
  T Value = Data.getULEB128(Cursor);
  if (Cursor)
    return Value;
  return std::nullopt;
}

Error DWARFDebugLine::LineTable::parse(
    DWARFDataExtractor &DebugLineData, uint64_t *OffsetPtr,
    const DWARFContext &Ctx, const DWARFUnit *U,
    function_ref<void(Error)> RecoverableErrorHandler, raw_ostream *OS,
    bool Verbose) {
  assert((OS || !Verbose) && "cannot have verbose output without stream");
  const uint64_t DebugLineOffset = *OffsetPtr;

  clear();

  Error PrologueErr =
      Prologue.parse(DebugLineData, OffsetPtr, RecoverableErrorHandler, Ctx, U);

  if (OS) {
    DIDumpOptions DumpOptions;
    DumpOptions.Verbose = Verbose;
    Prologue.dump(*OS, DumpOptions);
  }

  if (PrologueErr) {
    // Ensure there is a blank line after the prologue to clearly delineate it
    // from later dumps.
    if (OS)
      *OS << "\n";
    return PrologueErr;
  }

  uint64_t ProgramLength = Prologue.TotalLength + Prologue.sizeofTotalLength();
  if (!DebugLineData.isValidOffsetForDataOfSize(DebugLineOffset,
                                                ProgramLength)) {
    assert(DebugLineData.size() > DebugLineOffset &&
           "prologue parsing should handle invalid offset");
    uint64_t BytesRemaining = DebugLineData.size() - DebugLineOffset;
    RecoverableErrorHandler(
        createStringError(errc::invalid_argument,
                          "line table program with offset 0x%8.8" PRIx64
                          " has length 0x%8.8" PRIx64 " but only 0x%8.8" PRIx64
                          " bytes are available",
                          DebugLineOffset, ProgramLength, BytesRemaining));
    // Continue by capping the length at the number of remaining bytes.
    ProgramLength = BytesRemaining;
  }

  // Create a DataExtractor which can only see the data up to the end of the
  // table, to prevent reading past the end.
  const uint64_t EndOffset = DebugLineOffset + ProgramLength;
  DWARFDataExtractor TableData(DebugLineData, EndOffset);

  // See if we should tell the data extractor the address size.
  if (TableData.getAddressSize() == 0)
    TableData.setAddressSize(Prologue.getAddressSize());
  else
    assert(Prologue.getAddressSize() == 0 ||
           Prologue.getAddressSize() == TableData.getAddressSize());

  ParsingState State(this, DebugLineOffset, RecoverableErrorHandler);

  *OffsetPtr = DebugLineOffset + Prologue.getLength();
  if (OS && *OffsetPtr < EndOffset) {
    *OS << '\n';
    Row::dumpTableHeader(*OS, /*Indent=*/Verbose ? 12 : 0);
  }
  bool TombstonedAddress = false;
  auto EmitRow = [&] {
    if (!TombstonedAddress) {
      if (Verbose) {
        *OS << "\n";
        OS->indent(12);
      }
      if (OS)
        State.Row.dump(*OS);
      State.appendRowToMatrix();
    }
  };
  while (*OffsetPtr < EndOffset) {
    DataExtractor::Cursor Cursor(*OffsetPtr);

    if (Verbose)
      *OS << format("0x%08.08" PRIx64 ": ", *OffsetPtr);

    uint64_t OpcodeOffset = *OffsetPtr;
    uint8_t Opcode = TableData.getU8(Cursor);
    size_t RowCount = Rows.size();

    if (Cursor && Verbose)
      *OS << format("%02.02" PRIx8 " ", Opcode);

    if (Opcode == 0) {
      // Extended Opcodes always start with a zero opcode followed by
      // a uleb128 length so you can skip ones you don't know about
      uint64_t Len = TableData.getULEB128(Cursor);
      uint64_t ExtOffset = Cursor.tell();

      // Tolerate zero-length; assume length is correct and soldier on.
      if (Len == 0) {
        if (Cursor && Verbose)
          *OS << "Badly formed extended line op (length 0)\n";
        if (!Cursor) {
          if (Verbose)
            *OS << "\n";
          RecoverableErrorHandler(Cursor.takeError());
        }
        *OffsetPtr = Cursor.tell();
        continue;
      }

      uint8_t SubOpcode = TableData.getU8(Cursor);
      // OperandOffset will be the same as ExtOffset, if it was not possible to
      // read the SubOpcode.
      uint64_t OperandOffset = Cursor.tell();
      if (Verbose)
        *OS << LNExtendedString(SubOpcode);
      switch (SubOpcode) {
      case DW_LNE_end_sequence:
        // Set the end_sequence register of the state machine to true and
        // append a row to the matrix using the current values of the
        // state-machine registers. Then reset the registers to the initial
        // values specified above. Every statement program sequence must end
        // with a DW_LNE_end_sequence instruction which creates a row whose
        // address is that of the byte after the last target machine instruction
        // of the sequence.
        State.Row.EndSequence = true;
        // No need to test the Cursor is valid here, since it must be to get
        // into this code path - if it were invalid, the default case would be
        // followed.
        EmitRow();
        State.resetRowAndSequence();
        break;

      case DW_LNE_set_address:
        // Takes a single relocatable address as an operand. The size of the
        // operand is the size appropriate to hold an address on the target
        // machine. Set the address register to the value given by the
        // relocatable address and set the op_index register to 0. All of the
        // other statement program opcodes that affect the address register
        // add a delta to it. This instruction stores a relocatable value into
        // it instead.
        //
        // Make sure the extractor knows the address size.  If not, infer it
        // from the size of the operand.
        {
          uint8_t ExtractorAddressSize = TableData.getAddressSize();
          uint64_t OpcodeAddressSize = Len - 1;
          if (ExtractorAddressSize != OpcodeAddressSize &&
              ExtractorAddressSize != 0)
            RecoverableErrorHandler(createStringError(
                errc::invalid_argument,
                "mismatching address size at offset 0x%8.8" PRIx64
                " expected 0x%2.2" PRIx8 " found 0x%2.2" PRIx64,
                ExtOffset, ExtractorAddressSize, Len - 1));

          // Assume that the line table is correct and temporarily override the
          // address size. If the size is unsupported, give up trying to read
          // the address and continue to the next opcode.
          if (OpcodeAddressSize != 1 && OpcodeAddressSize != 2 &&
              OpcodeAddressSize != 4 && OpcodeAddressSize != 8) {
            RecoverableErrorHandler(createStringError(
                errc::invalid_argument,
                "address size 0x%2.2" PRIx64
                " of DW_LNE_set_address opcode at offset 0x%8.8" PRIx64
                " is unsupported",
                OpcodeAddressSize, ExtOffset));
            TableData.skip(Cursor, OpcodeAddressSize);
          } else {
            TableData.setAddressSize(OpcodeAddressSize);
            State.Row.Address.Address = TableData.getRelocatedAddress(
                Cursor, &State.Row.Address.SectionIndex);
            State.Row.OpIndex = 0;

            uint64_t Tombstone =
                dwarf::computeTombstoneAddress(OpcodeAddressSize);
            TombstonedAddress = State.Row.Address.Address == Tombstone;

            // Restore the address size if the extractor already had it.
            if (ExtractorAddressSize != 0)
              TableData.setAddressSize(ExtractorAddressSize);
          }

          if (Cursor && Verbose) {
            *OS << " (";
            DWARFFormValue::dumpAddress(*OS, OpcodeAddressSize, State.Row.Address.Address);
            *OS << ')';
          }
        }
        break;

      case DW_LNE_define_file:
        // Takes 4 arguments. The first is a null terminated string containing
        // a source file name. The second is an unsigned LEB128 number
        // representing the directory index of the directory in which the file
        // was found. The third is an unsigned LEB128 number representing the
        // time of last modification of the file. The fourth is an unsigned
        // LEB128 number representing the length in bytes of the file. The time
        // and length fields may contain LEB128(0) if the information is not
        // available.
        //
        // The directory index represents an entry in the include_directories
        // section of the statement program prologue. The index is LEB128(0)
        // if the file was found in the current directory of the compilation,
        // LEB128(1) if it was found in the first directory in the
        // include_directories section, and so on. The directory index is
        // ignored for file names that represent full path names.
        //
        // The files are numbered, starting at 1, in the order in which they
        // appear; the names in the prologue come before names defined by
        // the DW_LNE_define_file instruction. These numbers are used in the
        // the file register of the state machine.
        {
          FileNameEntry FileEntry;
          const char *Name = TableData.getCStr(Cursor);
          FileEntry.Name =
              DWARFFormValue::createFromPValue(dwarf::DW_FORM_string, Name);
          FileEntry.DirIdx = TableData.getULEB128(Cursor);
          FileEntry.ModTime = TableData.getULEB128(Cursor);
          FileEntry.Length = TableData.getULEB128(Cursor);
          Prologue.FileNames.push_back(FileEntry);
          if (Cursor && Verbose)
            *OS << " (" << Name << ", dir=" << FileEntry.DirIdx << ", mod_time="
                << format("(0x%16.16" PRIx64 ")", FileEntry.ModTime)
                << ", length=" << FileEntry.Length << ")";
        }
        break;

      case DW_LNE_set_discriminator:
        State.Row.Discriminator = TableData.getULEB128(Cursor);
        if (Cursor && Verbose)
          *OS << " (" << State.Row.Discriminator << ")";
        break;

      default:
        if (Cursor && Verbose)
          *OS << format("Unrecognized extended op 0x%02.02" PRIx8, SubOpcode)
              << format(" length %" PRIx64, Len);
        // Len doesn't include the zero opcode byte or the length itself, but
        // it does include the sub_opcode, so we have to adjust for that.
        TableData.skip(Cursor, Len - 1);
        break;
      }
      // Make sure the length as recorded in the table and the standard length
      // for the opcode match. If they don't, continue from the end as claimed
      // by the table. Similarly, continue from the claimed end in the event of
      // a parsing error.
      uint64_t End = ExtOffset + Len;
      if (Cursor && Cursor.tell() != End)
        RecoverableErrorHandler(createStringError(
            errc::illegal_byte_sequence,
            "unexpected line op length at offset 0x%8.8" PRIx64
            " expected 0x%2.2" PRIx64 " found 0x%2.2" PRIx64,
            ExtOffset, Len, Cursor.tell() - ExtOffset));
      if (!Cursor && Verbose) {
        DWARFDataExtractor::Cursor ByteCursor(OperandOffset);
        uint8_t Byte = TableData.getU8(ByteCursor);
        if (ByteCursor) {
          *OS << " (<parsing error>";
          do {
            *OS << format(" %2.2" PRIx8, Byte);
            Byte = TableData.getU8(ByteCursor);
          } while (ByteCursor);
          *OS << ")";
        }

        // The only parse failure in this case should be if the end was reached.
        // In that case, throw away the error, as the main Cursor's error will
        // be sufficient.
        consumeError(ByteCursor.takeError());
      }
      *OffsetPtr = End;
    } else if (Opcode < Prologue.OpcodeBase) {
      if (Verbose)
        *OS << LNStandardString(Opcode);
      switch (Opcode) {
      // Standard Opcodes
      case DW_LNS_copy:
        // Takes no arguments. Append a row to the matrix using the
        // current values of the state-machine registers.
        EmitRow();
        break;

      case DW_LNS_advance_pc:
        // Takes a single unsigned LEB128 operand as the operation advance
        // and modifies the address and op_index registers of the state machine
        // according to that.
        if (std::optional<uint64_t> Operand =
                parseULEB128<uint64_t>(TableData, Cursor)) {
          ParsingState::AddrOpIndexDelta Advance =
              State.advanceAddrOpIndex(*Operand, Opcode, OpcodeOffset);
          if (Verbose)
            *OS << " (addr += " << Advance.AddrOffset
                << ", op-index += " << Advance.OpIndexDelta << ")";
        }
        break;

      case DW_LNS_advance_line:
        // Takes a single signed LEB128 operand and adds that value to
        // the line register of the state machine.
        {
          int64_t LineDelta = TableData.getSLEB128(Cursor);
          if (Cursor) {
            State.Row.Line += LineDelta;
            if (Verbose)
              *OS << " (" << State.Row.Line << ")";
          }
        }
        break;

      case DW_LNS_set_file:
        // Takes a single unsigned LEB128 operand and stores it in the file
        // register of the state machine.
        if (std::optional<uint16_t> File =
                parseULEB128<uint16_t>(TableData, Cursor)) {
          State.Row.File = *File;
          if (Verbose)
            *OS << " (" << State.Row.File << ")";
        }
        break;

      case DW_LNS_set_column:
        // Takes a single unsigned LEB128 operand and stores it in the
        // column register of the state machine.
        if (std::optional<uint16_t> Column =
                parseULEB128<uint16_t>(TableData, Cursor)) {
          State.Row.Column = *Column;
          if (Verbose)
            *OS << " (" << State.Row.Column << ")";
        }
        break;

      case DW_LNS_negate_stmt:
        // Takes no arguments. Set the is_stmt register of the state
        // machine to the logical negation of its current value.
        State.Row.IsStmt = !State.Row.IsStmt;
        break;

      case DW_LNS_set_basic_block:
        // Takes no arguments. Set the basic_block register of the
        // state machine to true
        State.Row.BasicBlock = true;
        break;

      case DW_LNS_const_add_pc:
        // Takes no arguments. Advance the address and op_index registers of
        // the state machine by the increments corresponding to special
        // opcode 255. The motivation for DW_LNS_const_add_pc is this:
        // when the statement program needs to advance the address by a
        // small amount, it can use a single special opcode, which occupies
        // a single byte. When it needs to advance the address by up to
        // twice the range of the last special opcode, it can use
        // DW_LNS_const_add_pc followed by a special opcode, for a total
        // of two bytes. Only if it needs to advance the address by more
        // than twice that range will it need to use both DW_LNS_advance_pc
        // and a special opcode, requiring three or more bytes.
        {
          ParsingState::OpcodeAdvanceResults Advance =
              State.advanceForOpcode(Opcode, OpcodeOffset);
          if (Verbose)
            *OS << format(" (addr += 0x%16.16" PRIx64 ", op-index += %" PRIu8
                          ")",
                          Advance.AddrDelta, Advance.OpIndexDelta);
        }
        break;

      case DW_LNS_fixed_advance_pc:
        // Takes a single uhalf operand. Add to the address register of
        // the state machine the value of the (unencoded) operand and set
        // the op_index register to 0. This is the only extended opcode that
        // takes an argument that is not a variable length number.
        // The motivation for DW_LNS_fixed_advance_pc is this: existing
        // assemblers cannot emit DW_LNS_advance_pc or special opcodes because
        // they cannot encode LEB128 numbers or judge when the computation
        // of a special opcode overflows and requires the use of
        // DW_LNS_advance_pc. Such assemblers, however, can use
        // DW_LNS_fixed_advance_pc instead, sacrificing compression.
        {
          uint16_t PCOffset =
              TableData.getRelocatedValue(Cursor, 2);
          if (Cursor) {
            State.Row.Address.Address += PCOffset;
            State.Row.OpIndex = 0;
            if (Verbose)
              *OS << format(" (addr += 0x%4.4" PRIx16 ", op-index = 0)",
                            PCOffset);
          }
        }
        break;

      case DW_LNS_set_prologue_end:
        // Takes no arguments. Set the prologue_end register of the
        // state machine to true
        State.Row.PrologueEnd = true;
        break;

      case DW_LNS_set_epilogue_begin:
        // Takes no arguments. Set the basic_block register of the
        // state machine to true
        State.Row.EpilogueBegin = true;
        break;

      case DW_LNS_set_isa:
        // Takes a single unsigned LEB128 operand and stores it in the
        // ISA register of the state machine.
        if (std::optional<uint8_t> Isa =
                parseULEB128<uint8_t>(TableData, Cursor)) {
          State.Row.Isa = *Isa;
          if (Verbose)
            *OS << " (" << (uint64_t)State.Row.Isa << ")";
        }
        break;

      default:
        // Handle any unknown standard opcodes here. We know the lengths
        // of such opcodes because they are specified in the prologue
        // as a multiple of LEB128 operands for each opcode.
        {
          assert(Opcode - 1U < Prologue.StandardOpcodeLengths.size());
          if (Verbose)
            *OS << "Unrecognized standard opcode";
          uint8_t OpcodeLength = Prologue.StandardOpcodeLengths[Opcode - 1];
          std::vector<uint64_t> Operands;
          for (uint8_t I = 0; I < OpcodeLength; ++I) {
            if (std::optional<uint64_t> Value =
                    parseULEB128<uint64_t>(TableData, Cursor))
              Operands.push_back(*Value);
            else
              break;
          }
          if (Verbose && !Operands.empty()) {
            *OS << " (operands: ";
            bool First = true;
            for (uint64_t Value : Operands) {
              if (!First)
                *OS << ", ";
              First = false;
              *OS << format("0x%16.16" PRIx64, Value);
            }
            if (Verbose)
              *OS << ')';
          }
        }
        break;
      }

      *OffsetPtr = Cursor.tell();
    } else {
      // Special Opcodes.
      ParsingState::SpecialOpcodeDelta Delta =
          State.handleSpecialOpcode(Opcode, OpcodeOffset);

      if (Verbose)
        *OS << "address += " << Delta.Address << ",  line += " << Delta.Line
            << ",  op-index += " << Delta.OpIndex;
      EmitRow();
      *OffsetPtr = Cursor.tell();
    }

    // When a row is added to the matrix, it is also dumped, which includes a
    // new line already, so don't add an extra one.
    if (Verbose && Rows.size() == RowCount)
      *OS << "\n";

    // Most parse failures other than when parsing extended opcodes are due to
    // failures to read ULEBs. Bail out of parsing, since we don't know where to
    // continue reading from as there is no stated length for such byte
    // sequences. Print the final trailing new line if needed before doing so.
    if (!Cursor && Opcode != 0) {
      if (Verbose)
        *OS << "\n";
      return Cursor.takeError();
    }

    if (!Cursor)
      RecoverableErrorHandler(Cursor.takeError());
  }

  if (!State.Sequence.Empty)
    RecoverableErrorHandler(createStringError(
        errc::illegal_byte_sequence,
        "last sequence in debug line table at offset 0x%8.8" PRIx64
        " is not terminated",
        DebugLineOffset));

  // Sort all sequences so that address lookup will work faster.
  if (!Sequences.empty()) {
    llvm::sort(Sequences, Sequence::orderByHighPC);
    // Note: actually, instruction address ranges of sequences should not
    // overlap (in shared objects and executables). If they do, the address
    // lookup would still work, though, but result would be ambiguous.
    // We don't report warning in this case. For example,
    // sometimes .so compiled from multiple object files contains a few
    // rudimentary sequences for address ranges [0x0, 0xsomething).
  }

  // Terminate the table with a final blank line to clearly delineate it from
  // later dumps.
  if (OS)
    *OS << "\n";

  return Error::success();
}

uint32_t DWARFDebugLine::LineTable::findRowInSeq(
    const DWARFDebugLine::Sequence &Seq,
    object::SectionedAddress Address) const {
  if (!Seq.containsPC(Address))
    return UnknownRowIndex;
  assert(Seq.SectionIndex == Address.SectionIndex);
  // In some cases, e.g. first instruction in a function, the compiler generates
  // two entries, both with the same address. We want the last one.
  //
  // In general we want a non-empty range: the last row whose address is less
  // than or equal to Address. This can be computed as upper_bound - 1.
  //
  // TODO: This function, and its users, needs to be update to return multiple
  // rows for bundles with multiple op-indexes.
  DWARFDebugLine::Row Row;
  Row.Address = Address;
  RowIter FirstRow = Rows.begin() + Seq.FirstRowIndex;
  RowIter LastRow = Rows.begin() + Seq.LastRowIndex;
  assert(FirstRow->Address.Address <= Row.Address.Address &&
         Row.Address.Address < LastRow[-1].Address.Address);
  RowIter RowPos = std::upper_bound(FirstRow + 1, LastRow - 1, Row,
                                    DWARFDebugLine::Row::orderByAddress) -
                   1;
  assert(Seq.SectionIndex == RowPos->Address.SectionIndex);
  return RowPos - Rows.begin();
}

uint32_t DWARFDebugLine::LineTable::lookupAddress(
    object::SectionedAddress Address) const {

  // Search for relocatable addresses
  uint32_t Result = lookupAddressImpl(Address);

  if (Result != UnknownRowIndex ||
      Address.SectionIndex == object::SectionedAddress::UndefSection)
    return Result;

  // Search for absolute addresses
  Address.SectionIndex = object::SectionedAddress::UndefSection;
  return lookupAddressImpl(Address);
}

uint32_t DWARFDebugLine::LineTable::lookupAddressImpl(
    object::SectionedAddress Address) const {
  // First, find an instruction sequence containing the given address.
  DWARFDebugLine::Sequence Sequence;
  Sequence.SectionIndex = Address.SectionIndex;
  Sequence.HighPC = Address.Address;
  SequenceIter It = llvm::upper_bound(Sequences, Sequence,
                                      DWARFDebugLine::Sequence::orderByHighPC);
  if (It == Sequences.end() || It->SectionIndex != Address.SectionIndex)
    return UnknownRowIndex;
  return findRowInSeq(*It, Address);
}

bool DWARFDebugLine::LineTable::lookupAddressRange(
    object::SectionedAddress Address, uint64_t Size,
    std::vector<uint32_t> &Result) const {

  // Search for relocatable addresses
  if (lookupAddressRangeImpl(Address, Size, Result))
    return true;

  if (Address.SectionIndex == object::SectionedAddress::UndefSection)
    return false;

  // Search for absolute addresses
  Address.SectionIndex = object::SectionedAddress::UndefSection;
  return lookupAddressRangeImpl(Address, Size, Result);
}

bool DWARFDebugLine::LineTable::lookupAddressRangeImpl(
    object::SectionedAddress Address, uint64_t Size,
    std::vector<uint32_t> &Result) const {
  if (Sequences.empty())
    return false;
  uint64_t EndAddr = Address.Address + Size;
  // First, find an instruction sequence containing the given address.
  DWARFDebugLine::Sequence Sequence;
  Sequence.SectionIndex = Address.SectionIndex;
  Sequence.HighPC = Address.Address;
  SequenceIter LastSeq = Sequences.end();
  SequenceIter SeqPos = llvm::upper_bound(
      Sequences, Sequence, DWARFDebugLine::Sequence::orderByHighPC);
  if (SeqPos == LastSeq || !SeqPos->containsPC(Address))
    return false;

  SequenceIter StartPos = SeqPos;

  // Add the rows from the first sequence to the vector, starting with the
  // index we just calculated

  while (SeqPos != LastSeq && SeqPos->LowPC < EndAddr) {
    const DWARFDebugLine::Sequence &CurSeq = *SeqPos;
    // For the first sequence, we need to find which row in the sequence is the
    // first in our range.
    uint32_t FirstRowIndex = CurSeq.FirstRowIndex;
    if (SeqPos == StartPos)
      FirstRowIndex = findRowInSeq(CurSeq, Address);

    // Figure out the last row in the range.
    uint32_t LastRowIndex =
        findRowInSeq(CurSeq, {EndAddr - 1, Address.SectionIndex});
    if (LastRowIndex == UnknownRowIndex)
      LastRowIndex = CurSeq.LastRowIndex - 1;

    assert(FirstRowIndex != UnknownRowIndex);
    assert(LastRowIndex != UnknownRowIndex);

    for (uint32_t I = FirstRowIndex; I <= LastRowIndex; ++I) {
      Result.push_back(I);
    }

    ++SeqPos;
  }

  return true;
}

std::optional<StringRef>
DWARFDebugLine::LineTable::getSourceByIndex(uint64_t FileIndex,
                                            FileLineInfoKind Kind) const {
  if (Kind == FileLineInfoKind::None || !Prologue.hasFileAtIndex(FileIndex))
    return std::nullopt;
  const FileNameEntry &Entry = Prologue.getFileNameEntry(FileIndex);
  if (auto E = dwarf::toString(Entry.Source))
    return StringRef(*E);
  return std::nullopt;
}

static bool isPathAbsoluteOnWindowsOrPosix(const Twine &Path) {
  // Debug info can contain paths from any OS, not necessarily
  // an OS we're currently running on. Moreover different compilation units can
  // be compiled on different operating systems and linked together later.
  return sys::path::is_absolute(Path, sys::path::Style::posix) ||
         sys::path::is_absolute(Path, sys::path::Style::windows);
}

bool DWARFDebugLine::Prologue::getFileNameByIndex(
    uint64_t FileIndex, StringRef CompDir, FileLineInfoKind Kind,
    std::string &Result, sys::path::Style Style) const {
  if (Kind == FileLineInfoKind::None || !hasFileAtIndex(FileIndex))
    return false;
  const FileNameEntry &Entry = getFileNameEntry(FileIndex);
  auto E = dwarf::toString(Entry.Name);
  if (!E)
    return false;
  StringRef FileName = *E;
  if (Kind == FileLineInfoKind::RawValue ||
      isPathAbsoluteOnWindowsOrPosix(FileName)) {
    Result = std::string(FileName);
    return true;
  }
  if (Kind == FileLineInfoKind::BaseNameOnly) {
    Result = std::string(llvm::sys::path::filename(FileName));
    return true;
  }

  SmallString<16> FilePath;
  StringRef IncludeDir;
  // Be defensive about the contents of Entry.
  if (getVersion() >= 5) {
    // DirIdx 0 is the compilation directory, so don't include it for
    // relative names.
    if ((Entry.DirIdx != 0 || Kind != FileLineInfoKind::RelativeFilePath) &&
        Entry.DirIdx < IncludeDirectories.size())
      IncludeDir = dwarf::toStringRef(IncludeDirectories[Entry.DirIdx]);
  } else {
    if (0 < Entry.DirIdx && Entry.DirIdx <= IncludeDirectories.size())
      IncludeDir = dwarf::toStringRef(IncludeDirectories[Entry.DirIdx - 1]);
  }

  // For absolute paths only, include the compilation directory of compile unit,
  // unless v5 DirIdx == 0 (IncludeDir indicates the compilation directory). We
  // know that FileName is not absolute, the only way to have an absolute path
  // at this point would be if IncludeDir is absolute.
  if (Kind == FileLineInfoKind::AbsoluteFilePath &&
      (getVersion() < 5 || Entry.DirIdx != 0) && !CompDir.empty() &&
      !isPathAbsoluteOnWindowsOrPosix(IncludeDir))
    sys::path::append(FilePath, Style, CompDir);

  assert((Kind == FileLineInfoKind::AbsoluteFilePath ||
          Kind == FileLineInfoKind::RelativeFilePath) &&
         "invalid FileLineInfo Kind");

  // sys::path::append skips empty strings.
  sys::path::append(FilePath, Style, IncludeDir, FileName);
  Result = std::string(FilePath);
  return true;
}

bool DWARFDebugLine::LineTable::getFileLineInfoForAddress(
    object::SectionedAddress Address, const char *CompDir,
    FileLineInfoKind Kind, DILineInfo &Result) const {
  // Get the index of row we're looking for in the line table.
  uint32_t RowIndex = lookupAddress(Address);
  if (RowIndex == -1U)
    return false;
  // Take file number and line/column from the row.
  const auto &Row = Rows[RowIndex];
  if (!getFileNameByIndex(Row.File, CompDir, Kind, Result.FileName))
    return false;
  Result.Line = Row.Line;
  Result.Column = Row.Column;
  Result.Discriminator = Row.Discriminator;
  Result.Source = getSourceByIndex(Row.File, Kind);
  return true;
}

bool DWARFDebugLine::LineTable::getDirectoryForEntry(
    const FileNameEntry &Entry, std::string &Directory) const {
  if (Prologue.getVersion() >= 5) {
    if (Entry.DirIdx < Prologue.IncludeDirectories.size()) {
      Directory =
          dwarf::toString(Prologue.IncludeDirectories[Entry.DirIdx], "");
      return true;
    }
    return false;
  }
  if (0 < Entry.DirIdx && Entry.DirIdx <= Prologue.IncludeDirectories.size()) {
    Directory =
        dwarf::toString(Prologue.IncludeDirectories[Entry.DirIdx - 1], "");
    return true;
  }
  return false;
}

// We want to supply the Unit associated with a .debug_line[.dwo] table when
// we dump it, if possible, but still dump the table even if there isn't a Unit.
// Therefore, collect up handles on all the Units that point into the
// line-table section.
static DWARFDebugLine::SectionParser::LineToUnitMap
buildLineToUnitMap(DWARFUnitVector::iterator_range Units) {
  DWARFDebugLine::SectionParser::LineToUnitMap LineToUnit;
  for (const auto &U : Units)
    if (auto CUDIE = U->getUnitDIE())
      if (auto StmtOffset = toSectionOffset(CUDIE.find(DW_AT_stmt_list)))
        LineToUnit.insert(std::make_pair(*StmtOffset, &*U));
  return LineToUnit;
}

DWARFDebugLine::SectionParser::SectionParser(
    DWARFDataExtractor &Data, const DWARFContext &C,
    DWARFUnitVector::iterator_range Units)
    : DebugLineData(Data), Context(C) {
  LineToUnit = buildLineToUnitMap(Units);
  if (!DebugLineData.isValidOffset(Offset))
    Done = true;
}

bool DWARFDebugLine::Prologue::totalLengthIsValid() const {
  return TotalLength != 0u;
}

DWARFDebugLine::LineTable DWARFDebugLine::SectionParser::parseNext(
    function_ref<void(Error)> RecoverableErrorHandler,
    function_ref<void(Error)> UnrecoverableErrorHandler, raw_ostream *OS,
    bool Verbose) {
  assert(DebugLineData.isValidOffset(Offset) &&
         "parsing should have terminated");
  DWARFUnit *U = prepareToParse(Offset);
  uint64_t OldOffset = Offset;
  LineTable LT;
  if (Error Err = LT.parse(DebugLineData, &Offset, Context, U,
                           RecoverableErrorHandler, OS, Verbose))
    UnrecoverableErrorHandler(std::move(Err));
  moveToNextTable(OldOffset, LT.Prologue);
  return LT;
}

void DWARFDebugLine::SectionParser::skip(
    function_ref<void(Error)> RecoverableErrorHandler,
    function_ref<void(Error)> UnrecoverableErrorHandler) {
  assert(DebugLineData.isValidOffset(Offset) &&
         "parsing should have terminated");
  DWARFUnit *U = prepareToParse(Offset);
  uint64_t OldOffset = Offset;
  LineTable LT;
  if (Error Err = LT.Prologue.parse(DebugLineData, &Offset,
                                    RecoverableErrorHandler, Context, U))
    UnrecoverableErrorHandler(std::move(Err));
  moveToNextTable(OldOffset, LT.Prologue);
}

DWARFUnit *DWARFDebugLine::SectionParser::prepareToParse(uint64_t Offset) {
  DWARFUnit *U = nullptr;
  auto It = LineToUnit.find(Offset);
  if (It != LineToUnit.end())
    U = It->second;
  DebugLineData.setAddressSize(U ? U->getAddressByteSize() : 0);
  return U;
}

bool DWARFDebugLine::SectionParser::hasValidVersion(uint64_t Offset) {
  DataExtractor::Cursor Cursor(Offset);
  auto [TotalLength, _] = DebugLineData.getInitialLength(Cursor);
  DWARFDataExtractor HeaderData(DebugLineData, Cursor.tell() + TotalLength);
  uint16_t Version = HeaderData.getU16(Cursor);
  if (!Cursor) {
    // Ignore any error here.
    // If this is not the end of the section parseNext() will still be
    // attempted, where this error will occur again (and can be handled).
    consumeError(Cursor.takeError());
    return false;
  }
  return versionIsSupported(Version);
}

void DWARFDebugLine::SectionParser::moveToNextTable(uint64_t OldOffset,
                                                    const Prologue &P) {
  // If the length field is not valid, we don't know where the next table is, so
  // cannot continue to parse. Mark the parser as done, and leave the Offset
  // value as it currently is. This will be the end of the bad length field.
  if (!P.totalLengthIsValid()) {
    Done = true;
    return;
  }

  Offset = OldOffset + P.TotalLength + P.sizeofTotalLength();
  if (!DebugLineData.isValidOffset(Offset)) {
    Done = true;
    return;
  }

  // Heuristic: If the version is valid, then this is probably a line table.
  // Otherwise, the offset might need alignment (to a 4 or 8 byte boundary).
  if (hasValidVersion(Offset))
    return;

  // ARM C/C++ Compiler aligns each line table to word boundaries and pads out
  // the .debug_line section to a word multiple. Note that in the specification
  // this does not seem forbidden since each unit has a DW_AT_stmt_list.
  for (unsigned Align : {4, 8}) {
    uint64_t AlignedOffset = alignTo(Offset, Align);
    if (!DebugLineData.isValidOffset(AlignedOffset)) {
      // This is almost certainly not another line table but some alignment
      // padding. This assumes the alignments tested are ordered, and are
      // smaller than the header size (which is true for 4 and 8).
      Done = true;
      return;
    }
    if (hasValidVersion(AlignedOffset)) {
      Offset = AlignedOffset;
      break;
    }
  }
}
