//===- yaml2macho - Convert YAML to a Mach object file --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The Mach component of yaml2obj.
///
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/ObjectYAML/DWARFEmitter.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/SystemZ/zOSSupport.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/Format.h"

using namespace llvm;

namespace {

class MachOWriter {
public:
  MachOWriter(MachOYAML::Object &Obj) : Obj(Obj), fileStart(0) {
    is64Bit = Obj.Header.magic == MachO::MH_MAGIC_64 ||
              Obj.Header.magic == MachO::MH_CIGAM_64;
    memset(reinterpret_cast<void *>(&Header), 0, sizeof(MachO::mach_header_64));
  }

  Error writeMachO(raw_ostream &OS);

private:
  void writeHeader(raw_ostream &OS);
  void writeLoadCommands(raw_ostream &OS);
  Error writeSectionData(raw_ostream &OS);
  void writeRelocations(raw_ostream &OS);
  void writeLinkEditData(raw_ostream &OS);

  void writeBindOpcodes(raw_ostream &OS,
                        std::vector<MachOYAML::BindOpcode> &BindOpcodes);
  // LinkEdit writers
  void writeRebaseOpcodes(raw_ostream &OS);
  void writeBasicBindOpcodes(raw_ostream &OS);
  void writeWeakBindOpcodes(raw_ostream &OS);
  void writeLazyBindOpcodes(raw_ostream &OS);
  void writeNameList(raw_ostream &OS);
  void writeStringTable(raw_ostream &OS);
  void writeExportTrie(raw_ostream &OS);
  void writeDynamicSymbolTable(raw_ostream &OS);
  void writeFunctionStarts(raw_ostream &OS);
  void writeChainedFixups(raw_ostream &OS);
  void writeDyldExportsTrie(raw_ostream &OS);
  void writeDataInCode(raw_ostream &OS);

  void dumpExportEntry(raw_ostream &OS, MachOYAML::ExportEntry &Entry);
  void ZeroToOffset(raw_ostream &OS, size_t offset);

  MachOYAML::Object &Obj;
  bool is64Bit;
  uint64_t fileStart;
  MachO::mach_header_64 Header;

  // Old PPC Object Files didn't have __LINKEDIT segments, the data was just
  // stuck at the end of the file.
  bool FoundLinkEditSeg = false;
};

Error MachOWriter::writeMachO(raw_ostream &OS) {
  fileStart = OS.tell();
  writeHeader(OS);
  writeLoadCommands(OS);
  if (Error Err = writeSectionData(OS))
    return Err;
  writeRelocations(OS);
  if (!FoundLinkEditSeg)
    writeLinkEditData(OS);
  return Error::success();
}

void MachOWriter::writeHeader(raw_ostream &OS) {
  Header.magic = Obj.Header.magic;
  Header.cputype = Obj.Header.cputype;
  Header.cpusubtype = Obj.Header.cpusubtype;
  Header.filetype = Obj.Header.filetype;
  Header.ncmds = Obj.Header.ncmds;
  Header.sizeofcmds = Obj.Header.sizeofcmds;
  Header.flags = Obj.Header.flags;
  Header.reserved = Obj.Header.reserved;

  if (Obj.IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(Header);

  auto header_size =
      is64Bit ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
  OS.write((const char *)&Header, header_size);
}

template <typename SectionType>
SectionType constructSection(const MachOYAML::Section &Sec) {
  SectionType TempSec;
  memcpy(reinterpret_cast<void *>(&TempSec.sectname[0]), &Sec.sectname[0], 16);
  memcpy(reinterpret_cast<void *>(&TempSec.segname[0]), &Sec.segname[0], 16);
  TempSec.addr = Sec.addr;
  TempSec.size = Sec.size;
  TempSec.offset = Sec.offset;
  TempSec.align = Sec.align;
  TempSec.reloff = Sec.reloff;
  TempSec.nreloc = Sec.nreloc;
  TempSec.flags = Sec.flags;
  TempSec.reserved1 = Sec.reserved1;
  TempSec.reserved2 = Sec.reserved2;
  return TempSec;
}

template <typename StructType>
size_t writeLoadCommandData(MachOYAML::LoadCommand &LC, raw_ostream &OS,
                            bool IsLittleEndian) {
  return 0;
}

template <>
size_t writeLoadCommandData<MachO::segment_command>(MachOYAML::LoadCommand &LC,
                                                    raw_ostream &OS,
                                                    bool IsLittleEndian) {
  size_t BytesWritten = 0;
  for (const auto &Sec : LC.Sections) {
    auto TempSec = constructSection<MachO::section>(Sec);
    if (IsLittleEndian != sys::IsLittleEndianHost)
      MachO::swapStruct(TempSec);
    OS.write(reinterpret_cast<const char *>(&(TempSec)),
             sizeof(MachO::section));
    BytesWritten += sizeof(MachO::section);
  }
  return BytesWritten;
}

template <>
size_t writeLoadCommandData<MachO::segment_command_64>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  size_t BytesWritten = 0;
  for (const auto &Sec : LC.Sections) {
    auto TempSec = constructSection<MachO::section_64>(Sec);
    TempSec.reserved3 = Sec.reserved3;
    if (IsLittleEndian != sys::IsLittleEndianHost)
      MachO::swapStruct(TempSec);
    OS.write(reinterpret_cast<const char *>(&(TempSec)),
             sizeof(MachO::section_64));
    BytesWritten += sizeof(MachO::section_64);
  }
  return BytesWritten;
}

size_t writePayloadString(MachOYAML::LoadCommand &LC, raw_ostream &OS) {
  size_t BytesWritten = 0;
  if (!LC.Content.empty()) {
    OS.write(LC.Content.c_str(), LC.Content.length());
    BytesWritten = LC.Content.length();
  }
  return BytesWritten;
}

template <>
size_t writeLoadCommandData<MachO::dylib_command>(MachOYAML::LoadCommand &LC,
                                                  raw_ostream &OS,
                                                  bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::dylinker_command>(MachOYAML::LoadCommand &LC,
                                                     raw_ostream &OS,
                                                     bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::rpath_command>(MachOYAML::LoadCommand &LC,
                                                  raw_ostream &OS,
                                                  bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::sub_framework_command>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::sub_umbrella_command>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::sub_client_command>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::sub_library_command>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  return writePayloadString(LC, OS);
}

template <>
size_t writeLoadCommandData<MachO::build_version_command>(
    MachOYAML::LoadCommand &LC, raw_ostream &OS, bool IsLittleEndian) {
  size_t BytesWritten = 0;
  for (const auto &T : LC.Tools) {
    struct MachO::build_tool_version tool = T;
    if (IsLittleEndian != sys::IsLittleEndianHost)
      MachO::swapStruct(tool);
    OS.write(reinterpret_cast<const char *>(&tool),
             sizeof(MachO::build_tool_version));
    BytesWritten += sizeof(MachO::build_tool_version);
  }
  return BytesWritten;
}

void ZeroFillBytes(raw_ostream &OS, size_t Size) {
  std::vector<uint8_t> FillData(Size, 0);
  OS.write(reinterpret_cast<char *>(FillData.data()), Size);
}

void Fill(raw_ostream &OS, size_t Size, uint32_t Data) {
  std::vector<uint32_t> FillData((Size / 4) + 1, Data);
  OS.write(reinterpret_cast<char *>(FillData.data()), Size);
}

void MachOWriter::ZeroToOffset(raw_ostream &OS, size_t Offset) {
  auto currOffset = OS.tell() - fileStart;
  if (currOffset < Offset)
    ZeroFillBytes(OS, Offset - currOffset);
}

void MachOWriter::writeLoadCommands(raw_ostream &OS) {
  for (auto &LC : Obj.LoadCommands) {
    size_t BytesWritten = 0;
    llvm::MachO::macho_load_command Data = LC.Data;

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct)                         \
  case MachO::LCName:                                                          \
    if (Obj.IsLittleEndian != sys::IsLittleEndianHost)                         \
      MachO::swapStruct(Data.LCStruct##_data);                                 \
    OS.write(reinterpret_cast<const char *>(&(Data.LCStruct##_data)),          \
             sizeof(MachO::LCStruct));                                         \
    BytesWritten = sizeof(MachO::LCStruct);                                    \
    BytesWritten +=                                                            \
        writeLoadCommandData<MachO::LCStruct>(LC, OS, Obj.IsLittleEndian);     \
    break;

    switch (LC.Data.load_command_data.cmd) {
    default:
      if (Obj.IsLittleEndian != sys::IsLittleEndianHost)
        MachO::swapStruct(Data.load_command_data);
      OS.write(reinterpret_cast<const char *>(&(Data.load_command_data)),
               sizeof(MachO::load_command));
      BytesWritten = sizeof(MachO::load_command);
      BytesWritten +=
          writeLoadCommandData<MachO::load_command>(LC, OS, Obj.IsLittleEndian);
      break;
#include "llvm/BinaryFormat/MachO.def"
    }

    if (LC.PayloadBytes.size() > 0) {
      OS.write(reinterpret_cast<const char *>(LC.PayloadBytes.data()),
               LC.PayloadBytes.size());
      BytesWritten += LC.PayloadBytes.size();
    }

    if (LC.ZeroPadBytes > 0) {
      ZeroFillBytes(OS, LC.ZeroPadBytes);
      BytesWritten += LC.ZeroPadBytes;
    }

    // Fill remaining bytes with 0. This will only get hit in partially
    // specified test cases.
    auto BytesRemaining = LC.Data.load_command_data.cmdsize - BytesWritten;
    if (BytesRemaining > 0) {
      ZeroFillBytes(OS, BytesRemaining);
    }
  }
}

Error MachOWriter::writeSectionData(raw_ostream &OS) {
  uint64_t LinkEditOff = 0;
  for (auto &LC : Obj.LoadCommands) {
    switch (LC.Data.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
    case MachO::LC_SEGMENT_64:
      uint64_t segOff = is64Bit ? LC.Data.segment_command_64_data.fileoff
                                : LC.Data.segment_command_data.fileoff;
      if (0 ==
          strncmp(&LC.Data.segment_command_data.segname[0], "__LINKEDIT", 16)) {
        FoundLinkEditSeg = true;
        LinkEditOff = segOff;
        if (Obj.RawLinkEditSegment)
          continue;
        writeLinkEditData(OS);
      }
      for (auto &Sec : LC.Sections) {
        ZeroToOffset(OS, Sec.offset);
        // Zero Fill any data between the end of the last thing we wrote and the
        // start of this section.
        if (OS.tell() - fileStart > Sec.offset && Sec.offset != (uint32_t)0)
          return createStringError(
              errc::invalid_argument,
              llvm::formatv(
                  "wrote too much data somewhere, section offsets in "
                  "section {0} for segment {1} don't line up: "
                  "[cursor={2:x}], [fileStart={3:x}], [sectionOffset={4:x}]",
                  Sec.sectname, Sec.segname, OS.tell(), fileStart,
                  Sec.offset.value));

        StringRef SectName(Sec.sectname,
                           strnlen(Sec.sectname, sizeof(Sec.sectname)));
        // If the section's content is specified in the 'DWARF' entry, we will
        // emit it regardless of the section's segname.
        if (Obj.DWARF.getNonEmptySectionNames().count(SectName.substr(2))) {
          if (Sec.content)
            return createStringError(errc::invalid_argument,
                                     "cannot specify section '" + SectName +
                                         "' contents in the 'DWARF' entry and "
                                         "the 'content' at the same time");
          auto EmitFunc = DWARFYAML::getDWARFEmitterByName(SectName.substr(2));
          if (Error Err = EmitFunc(OS, Obj.DWARF))
            return Err;
          continue;
        }

        // Skip if it's a virtual section.
        if (MachO::isVirtualSection(Sec.flags & MachO::SECTION_TYPE))
          continue;

        if (Sec.content) {
          yaml::BinaryRef Content = *Sec.content;
          Content.writeAsBinary(OS);
          ZeroFillBytes(OS, Sec.size - Content.binary_size());
        } else {
          // Fill section data with 0xDEADBEEF.
          Fill(OS, Sec.size, 0xDEADBEEFu);
        }
      }
      uint64_t segSize = is64Bit ? LC.Data.segment_command_64_data.filesize
                                 : LC.Data.segment_command_data.filesize;
      ZeroToOffset(OS, segOff + segSize);
      break;
    }
  }

  if (Obj.RawLinkEditSegment) {
    ZeroToOffset(OS, LinkEditOff);
    if (OS.tell() - fileStart > LinkEditOff || !LinkEditOff)
      return createStringError(errc::invalid_argument,
                               "section offsets don't line up");
    Obj.RawLinkEditSegment->writeAsBinary(OS);
  }
  return Error::success();
}

// The implementation of makeRelocationInfo and makeScatteredRelocationInfo is
// consistent with how libObject parses MachO binary files. For the reference
// see getStruct, getRelocation, getPlainRelocationPCRel,
// getPlainRelocationLength and related methods in MachOObjectFile.cpp
static MachO::any_relocation_info
makeRelocationInfo(const MachOYAML::Relocation &R, bool IsLE) {
  assert(!R.is_scattered && "non-scattered relocation expected");
  MachO::any_relocation_info MRE;
  MRE.r_word0 = R.address;
  if (IsLE)
    MRE.r_word1 = ((unsigned)R.symbolnum << 0) | ((unsigned)R.is_pcrel << 24) |
                  ((unsigned)R.length << 25) | ((unsigned)R.is_extern << 27) |
                  ((unsigned)R.type << 28);
  else
    MRE.r_word1 = ((unsigned)R.symbolnum << 8) | ((unsigned)R.is_pcrel << 7) |
                  ((unsigned)R.length << 5) | ((unsigned)R.is_extern << 4) |
                  ((unsigned)R.type << 0);
  return MRE;
}

static MachO::any_relocation_info
makeScatteredRelocationInfo(const MachOYAML::Relocation &R) {
  assert(R.is_scattered && "scattered relocation expected");
  MachO::any_relocation_info MRE;
  MRE.r_word0 = (((unsigned)R.address << 0) | ((unsigned)R.type << 24) |
                 ((unsigned)R.length << 28) | ((unsigned)R.is_pcrel << 30) |
                 MachO::R_SCATTERED);
  MRE.r_word1 = R.value;
  return MRE;
}

void MachOWriter::writeRelocations(raw_ostream &OS) {
  for (const MachOYAML::LoadCommand &LC : Obj.LoadCommands) {
    switch (LC.Data.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
    case MachO::LC_SEGMENT_64:
      for (const MachOYAML::Section &Sec : LC.Sections) {
        if (Sec.relocations.empty())
          continue;
        ZeroToOffset(OS, Sec.reloff);
        for (const MachOYAML::Relocation &R : Sec.relocations) {
          MachO::any_relocation_info MRE =
              R.is_scattered ? makeScatteredRelocationInfo(R)
                             : makeRelocationInfo(R, Obj.IsLittleEndian);
          if (Obj.IsLittleEndian != sys::IsLittleEndianHost)
            MachO::swapStruct(MRE);
          OS.write(reinterpret_cast<const char *>(&MRE),
                   sizeof(MachO::any_relocation_info));
        }
      }
    }
  }
}

void MachOWriter::writeBindOpcodes(
    raw_ostream &OS, std::vector<MachOYAML::BindOpcode> &BindOpcodes) {

  for (const auto &Opcode : BindOpcodes) {
    uint8_t OpByte = Opcode.Opcode | Opcode.Imm;
    OS.write(reinterpret_cast<char *>(&OpByte), 1);
    for (auto Data : Opcode.ULEBExtraData) {
      encodeULEB128(Data, OS);
    }
    for (auto Data : Opcode.SLEBExtraData) {
      encodeSLEB128(Data, OS);
    }
    if (!Opcode.Symbol.empty()) {
      OS.write(Opcode.Symbol.data(), Opcode.Symbol.size());
      OS.write('\0');
    }
  }
}

void MachOWriter::dumpExportEntry(raw_ostream &OS,
                                  MachOYAML::ExportEntry &Entry) {
  encodeULEB128(Entry.TerminalSize, OS);
  if (Entry.TerminalSize > 0) {
    encodeULEB128(Entry.Flags, OS);
    if (Entry.Flags & MachO::EXPORT_SYMBOL_FLAGS_REEXPORT) {
      encodeULEB128(Entry.Other, OS);
      OS << Entry.ImportName;
      OS.write('\0');
    } else {
      encodeULEB128(Entry.Address, OS);
      if (Entry.Flags & MachO::EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER)
        encodeULEB128(Entry.Other, OS);
    }
  }
  OS.write(static_cast<uint8_t>(Entry.Children.size()));
  for (const auto &EE : Entry.Children) {
    OS << EE.Name;
    OS.write('\0');
    encodeULEB128(EE.NodeOffset, OS);
  }
  for (auto EE : Entry.Children)
    dumpExportEntry(OS, EE);
}

void MachOWriter::writeExportTrie(raw_ostream &OS) {
  dumpExportEntry(OS, Obj.LinkEdit.ExportTrie);
}

template <typename NListType>
void writeNListEntry(MachOYAML::NListEntry &NLE, raw_ostream &OS,
                     bool IsLittleEndian) {
  NListType ListEntry;
  ListEntry.n_strx = NLE.n_strx;
  ListEntry.n_type = NLE.n_type;
  ListEntry.n_sect = NLE.n_sect;
  ListEntry.n_desc = NLE.n_desc;
  ListEntry.n_value = NLE.n_value;

  if (IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(ListEntry);
  OS.write(reinterpret_cast<const char *>(&ListEntry), sizeof(NListType));
}

void MachOWriter::writeLinkEditData(raw_ostream &OS) {
  typedef void (MachOWriter::*writeHandler)(raw_ostream &);
  typedef std::pair<uint64_t, writeHandler> writeOperation;
  std::vector<writeOperation> WriteQueue;

  MachO::dyld_info_command *DyldInfoOnlyCmd = nullptr;
  MachO::symtab_command *SymtabCmd = nullptr;
  MachO::dysymtab_command *DSymtabCmd = nullptr;
  MachO::linkedit_data_command *FunctionStartsCmd = nullptr;
  MachO::linkedit_data_command *ChainedFixupsCmd = nullptr;
  MachO::linkedit_data_command *DyldExportsTrieCmd = nullptr;
  MachO::linkedit_data_command *DataInCodeCmd = nullptr;
  for (auto &LC : Obj.LoadCommands) {
    switch (LC.Data.load_command_data.cmd) {
    case MachO::LC_SYMTAB:
      SymtabCmd = &LC.Data.symtab_command_data;
      WriteQueue.push_back(
          std::make_pair(SymtabCmd->symoff, &MachOWriter::writeNameList));
      WriteQueue.push_back(
          std::make_pair(SymtabCmd->stroff, &MachOWriter::writeStringTable));
      break;
    case MachO::LC_DYLD_INFO_ONLY:
      DyldInfoOnlyCmd = &LC.Data.dyld_info_command_data;
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->rebase_off,
                                          &MachOWriter::writeRebaseOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->bind_off,
                                          &MachOWriter::writeBasicBindOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->weak_bind_off,
                                          &MachOWriter::writeWeakBindOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->lazy_bind_off,
                                          &MachOWriter::writeLazyBindOpcodes));
      WriteQueue.push_back(std::make_pair(DyldInfoOnlyCmd->export_off,
                                          &MachOWriter::writeExportTrie));
      break;
    case MachO::LC_DYSYMTAB:
      DSymtabCmd = &LC.Data.dysymtab_command_data;
      WriteQueue.push_back(std::make_pair(
          DSymtabCmd->indirectsymoff, &MachOWriter::writeDynamicSymbolTable));
      break;
    case MachO::LC_FUNCTION_STARTS:
      FunctionStartsCmd = &LC.Data.linkedit_data_command_data;
      WriteQueue.push_back(std::make_pair(FunctionStartsCmd->dataoff,
                                          &MachOWriter::writeFunctionStarts));
      break;
    case MachO::LC_DYLD_CHAINED_FIXUPS:
      ChainedFixupsCmd = &LC.Data.linkedit_data_command_data;
      WriteQueue.push_back(std::make_pair(ChainedFixupsCmd->dataoff,
                                          &MachOWriter::writeChainedFixups));
      break;
    case MachO::LC_DYLD_EXPORTS_TRIE:
      DyldExportsTrieCmd = &LC.Data.linkedit_data_command_data;
      WriteQueue.push_back(std::make_pair(DyldExportsTrieCmd->dataoff,
                                          &MachOWriter::writeDyldExportsTrie));
      break;
    case MachO::LC_DATA_IN_CODE:
      DataInCodeCmd = &LC.Data.linkedit_data_command_data;
      WriteQueue.push_back(std::make_pair(DataInCodeCmd->dataoff,
                                          &MachOWriter::writeDataInCode));
      break;
    }
  }

  llvm::sort(WriteQueue, llvm::less_first());

  for (auto writeOp : WriteQueue) {
    ZeroToOffset(OS, writeOp.first);
    (this->*writeOp.second)(OS);
  }
}

void MachOWriter::writeRebaseOpcodes(raw_ostream &OS) {
  MachOYAML::LinkEditData &LinkEdit = Obj.LinkEdit;

  for (const auto &Opcode : LinkEdit.RebaseOpcodes) {
    uint8_t OpByte = Opcode.Opcode | Opcode.Imm;
    OS.write(reinterpret_cast<char *>(&OpByte), 1);
    for (auto Data : Opcode.ExtraData)
      encodeULEB128(Data, OS);
  }
}

void MachOWriter::writeBasicBindOpcodes(raw_ostream &OS) {
  writeBindOpcodes(OS, Obj.LinkEdit.BindOpcodes);
}

void MachOWriter::writeWeakBindOpcodes(raw_ostream &OS) {
  writeBindOpcodes(OS, Obj.LinkEdit.WeakBindOpcodes);
}

void MachOWriter::writeLazyBindOpcodes(raw_ostream &OS) {
  writeBindOpcodes(OS, Obj.LinkEdit.LazyBindOpcodes);
}

void MachOWriter::writeNameList(raw_ostream &OS) {
  for (auto NLE : Obj.LinkEdit.NameList) {
    if (is64Bit)
      writeNListEntry<MachO::nlist_64>(NLE, OS, Obj.IsLittleEndian);
    else
      writeNListEntry<MachO::nlist>(NLE, OS, Obj.IsLittleEndian);
  }
}

void MachOWriter::writeStringTable(raw_ostream &OS) {
  for (auto Str : Obj.LinkEdit.StringTable) {
    OS.write(Str.data(), Str.size());
    OS.write('\0');
  }
}

void MachOWriter::writeDynamicSymbolTable(raw_ostream &OS) {
  for (auto Data : Obj.LinkEdit.IndirectSymbols)
    OS.write(reinterpret_cast<const char *>(&Data),
             sizeof(yaml::Hex32::BaseType));
}

void MachOWriter::writeFunctionStarts(raw_ostream &OS) {
  uint64_t Addr = 0;
  for (uint64_t NextAddr : Obj.LinkEdit.FunctionStarts) {
    uint64_t Delta = NextAddr - Addr;
    encodeULEB128(Delta, OS);
    Addr = NextAddr;
  }

  OS.write('\0');
}

void MachOWriter::writeDataInCode(raw_ostream &OS) {
  for (const auto &Entry : Obj.LinkEdit.DataInCode) {
    MachO::data_in_code_entry DICE{Entry.Offset, Entry.Length, Entry.Kind};
    if (Obj.IsLittleEndian != sys::IsLittleEndianHost)
      MachO::swapStruct(DICE);
    OS.write(reinterpret_cast<const char *>(&DICE),
             sizeof(MachO::data_in_code_entry));
  }
}

void MachOWriter::writeChainedFixups(raw_ostream &OS) {
  if (Obj.LinkEdit.ChainedFixups.size() > 0)
    OS.write(reinterpret_cast<const char *>(Obj.LinkEdit.ChainedFixups.data()),
             Obj.LinkEdit.ChainedFixups.size());
}

void MachOWriter::writeDyldExportsTrie(raw_ostream &OS) {
  dumpExportEntry(OS, Obj.LinkEdit.ExportTrie);
}

class UniversalWriter {
public:
  UniversalWriter(yaml::YamlObjectFile &ObjectFile)
      : ObjectFile(ObjectFile), fileStart(0) {}

  Error writeMachO(raw_ostream &OS);

private:
  void writeFatHeader(raw_ostream &OS);
  void writeFatArchs(raw_ostream &OS);

  void ZeroToOffset(raw_ostream &OS, size_t offset);

  yaml::YamlObjectFile &ObjectFile;
  uint64_t fileStart;
};

Error UniversalWriter::writeMachO(raw_ostream &OS) {
  fileStart = OS.tell();
  if (ObjectFile.MachO) {
    MachOWriter Writer(*ObjectFile.MachO);
    return Writer.writeMachO(OS);
  }

  writeFatHeader(OS);
  writeFatArchs(OS);

  auto &FatFile = *ObjectFile.FatMachO;
  if (FatFile.FatArchs.size() < FatFile.Slices.size())
    return createStringError(
        errc::invalid_argument,
        "cannot write 'Slices' if not described in 'FatArches'");

  for (size_t i = 0; i < FatFile.Slices.size(); i++) {
    ZeroToOffset(OS, FatFile.FatArchs[i].offset);
    MachOWriter Writer(FatFile.Slices[i]);
    if (Error Err = Writer.writeMachO(OS))
      return Err;

    auto SliceEnd = FatFile.FatArchs[i].offset + FatFile.FatArchs[i].size;
    ZeroToOffset(OS, SliceEnd);
  }

  return Error::success();
}

void UniversalWriter::writeFatHeader(raw_ostream &OS) {
  auto &FatFile = *ObjectFile.FatMachO;
  MachO::fat_header header;
  header.magic = FatFile.Header.magic;
  header.nfat_arch = FatFile.Header.nfat_arch;
  if (sys::IsLittleEndianHost)
    swapStruct(header);
  OS.write(reinterpret_cast<const char *>(&header), sizeof(MachO::fat_header));
}

template <typename FatArchType>
FatArchType constructFatArch(MachOYAML::FatArch &Arch) {
  FatArchType FatArch;
  FatArch.cputype = Arch.cputype;
  FatArch.cpusubtype = Arch.cpusubtype;
  FatArch.offset = Arch.offset;
  FatArch.size = Arch.size;
  FatArch.align = Arch.align;
  return FatArch;
}

template <typename StructType>
void writeFatArch(MachOYAML::FatArch &LC, raw_ostream &OS) {}

template <>
void writeFatArch<MachO::fat_arch>(MachOYAML::FatArch &Arch, raw_ostream &OS) {
  auto FatArch = constructFatArch<MachO::fat_arch>(Arch);
  if (sys::IsLittleEndianHost)
    swapStruct(FatArch);
  OS.write(reinterpret_cast<const char *>(&FatArch), sizeof(MachO::fat_arch));
}

template <>
void writeFatArch<MachO::fat_arch_64>(MachOYAML::FatArch &Arch,
                                      raw_ostream &OS) {
  auto FatArch = constructFatArch<MachO::fat_arch_64>(Arch);
  FatArch.reserved = Arch.reserved;
  if (sys::IsLittleEndianHost)
    swapStruct(FatArch);
  OS.write(reinterpret_cast<const char *>(&FatArch),
           sizeof(MachO::fat_arch_64));
}

void UniversalWriter::writeFatArchs(raw_ostream &OS) {
  auto &FatFile = *ObjectFile.FatMachO;
  bool is64Bit = FatFile.Header.magic == MachO::FAT_MAGIC_64;
  for (auto Arch : FatFile.FatArchs) {
    if (is64Bit)
      writeFatArch<MachO::fat_arch_64>(Arch, OS);
    else
      writeFatArch<MachO::fat_arch>(Arch, OS);
  }
}

void UniversalWriter::ZeroToOffset(raw_ostream &OS, size_t Offset) {
  auto currOffset = OS.tell() - fileStart;
  if (currOffset < Offset)
    ZeroFillBytes(OS, Offset - currOffset);
}

} // end anonymous namespace

namespace llvm {
namespace yaml {

bool yaml2macho(YamlObjectFile &Doc, raw_ostream &Out, ErrorHandler EH) {
  UniversalWriter Writer(Doc);
  if (Error Err = Writer.writeMachO(Out)) {
    handleAllErrors(std::move(Err),
                    [&](const ErrorInfoBase &Err) { EH(Err.message()); });
    return false;
  }
  return true;
}

} // namespace yaml
} // namespace llvm
