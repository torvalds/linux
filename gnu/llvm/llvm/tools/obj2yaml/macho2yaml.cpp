//===------ macho2yaml.cpp - obj2yaml conversion tool -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/SystemZ/zOSSupport.h"

#include <string.h> // for memcpy

using namespace llvm;

class MachODumper {

  template <typename StructType>
  Expected<const char *> processLoadCommandData(
      MachOYAML::LoadCommand &LC,
      const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
      MachOYAML::Object &Y);

  const object::MachOObjectFile &Obj;
  std::unique_ptr<DWARFContext> DWARFCtx;
  unsigned RawSegment;
  void dumpHeader(std::unique_ptr<MachOYAML::Object> &Y);
  Error dumpLoadCommands(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpLinkEdit(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpRebaseOpcodes(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpFunctionStarts(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpBindOpcodes(std::vector<MachOYAML::BindOpcode> &BindOpcodes,
                       ArrayRef<uint8_t> OpcodeBuffer, bool Lazy = false);
  void dumpExportTrie(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpSymbols(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpIndirectSymbols(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpChainedFixups(std::unique_ptr<MachOYAML::Object> &Y);
  void dumpDataInCode(std::unique_ptr<MachOYAML::Object> &Y);

  template <typename SectionType>
  Expected<MachOYAML::Section> constructSectionCommon(SectionType Sec,
                                                      size_t SecIndex);
  template <typename SectionType>
  Expected<MachOYAML::Section> constructSection(SectionType Sec,
                                                size_t SecIndex);
  template <typename SectionType, typename SegmentType>
  Expected<const char *>
  extractSections(const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
                  std::vector<MachOYAML::Section> &Sections,
                  MachOYAML::Object &Y);

public:
  MachODumper(const object::MachOObjectFile &O,
              std::unique_ptr<DWARFContext> DCtx, unsigned RawSegments)
      : Obj(O), DWARFCtx(std::move(DCtx)), RawSegment(RawSegments) {}
  Expected<std::unique_ptr<MachOYAML::Object>> dump();
};

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct)                         \
  case MachO::LCName:                                                          \
    memcpy((void *)&(LC.Data.LCStruct##_data), LoadCmd.Ptr,                    \
           sizeof(MachO::LCStruct));                                           \
    if (Obj.isLittleEndian() != sys::IsLittleEndianHost)                       \
      MachO::swapStruct(LC.Data.LCStruct##_data);                              \
    if (Expected<const char *> ExpectedEndPtr =                                \
            processLoadCommandData<MachO::LCStruct>(LC, LoadCmd, *Y.get()))    \
      EndPtr = *ExpectedEndPtr;                                                \
    else                                                                       \
      return ExpectedEndPtr.takeError();                                       \
    break;

template <typename SectionType>
Expected<MachOYAML::Section>
MachODumper::constructSectionCommon(SectionType Sec, size_t SecIndex) {
  MachOYAML::Section TempSec;
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
  TempSec.reserved3 = 0;
  if (!MachO::isVirtualSection(Sec.flags & MachO::SECTION_TYPE))
    TempSec.content =
        yaml::BinaryRef(Obj.getSectionContents(Sec.offset, Sec.size));

  if (Expected<object::SectionRef> SecRef = Obj.getSection(SecIndex)) {
    TempSec.relocations.reserve(TempSec.nreloc);
    for (const object::RelocationRef &Reloc : SecRef->relocations()) {
      const object::DataRefImpl Rel = Reloc.getRawDataRefImpl();
      const MachO::any_relocation_info RE = Obj.getRelocation(Rel);
      MachOYAML::Relocation R;
      R.address = Obj.getAnyRelocationAddress(RE);
      R.is_pcrel = Obj.getAnyRelocationPCRel(RE);
      R.length = Obj.getAnyRelocationLength(RE);
      R.type = Obj.getAnyRelocationType(RE);
      R.is_scattered = Obj.isRelocationScattered(RE);
      R.symbolnum = (R.is_scattered ? 0 : Obj.getPlainRelocationSymbolNum(RE));
      R.is_extern =
          (R.is_scattered ? false : Obj.getPlainRelocationExternal(RE));
      R.value = (R.is_scattered ? Obj.getScatteredRelocationValue(RE) : 0);
      TempSec.relocations.push_back(R);
    }
  } else {
    return SecRef.takeError();
  }
  return TempSec;
}

template <>
Expected<MachOYAML::Section> MachODumper::constructSection(MachO::section Sec,
                                                           size_t SecIndex) {
  Expected<MachOYAML::Section> TempSec = constructSectionCommon(Sec, SecIndex);
  if (TempSec)
    TempSec->reserved3 = 0;
  return TempSec;
}

template <>
Expected<MachOYAML::Section>
MachODumper::constructSection(MachO::section_64 Sec, size_t SecIndex) {
  Expected<MachOYAML::Section> TempSec = constructSectionCommon(Sec, SecIndex);
  if (TempSec)
    TempSec->reserved3 = Sec.reserved3;
  return TempSec;
}

static Error dumpDebugSection(StringRef SecName, DWARFContext &DCtx,
                              DWARFYAML::Data &DWARF) {
  if (SecName == "__debug_abbrev")
    return dumpDebugAbbrev(DCtx, DWARF);
  if (SecName == "__debug_aranges")
    return dumpDebugARanges(DCtx, DWARF);
  if (SecName == "__debug_info") {
    dumpDebugInfo(DCtx, DWARF);
    return Error::success();
  }
  if (SecName == "__debug_line") {
    dumpDebugLines(DCtx, DWARF);
    return Error::success();
  }
  if (SecName.starts_with("__debug_pub")) {
    // FIXME: We should extract pub-section dumpers from this function.
    dumpDebugPubSections(DCtx, DWARF);
    return Error::success();
  }
  if (SecName == "__debug_ranges")
    return dumpDebugRanges(DCtx, DWARF);
  if (SecName == "__debug_str")
    return dumpDebugStrings(DCtx, DWARF);
  return createStringError(errc::not_supported,
                           "dumping " + SecName + " section is not supported");
}

template <typename SectionType, typename SegmentType>
Expected<const char *> MachODumper::extractSections(
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    std::vector<MachOYAML::Section> &Sections, MachOYAML::Object &Y) {
  auto End = LoadCmd.Ptr + LoadCmd.C.cmdsize;
  const SectionType *Curr =
      reinterpret_cast<const SectionType *>(LoadCmd.Ptr + sizeof(SegmentType));
  for (; reinterpret_cast<const void *>(Curr) < End; Curr++) {
    SectionType Sec;
    memcpy((void *)&Sec, Curr, sizeof(SectionType));
    if (Obj.isLittleEndian() != sys::IsLittleEndianHost)
      MachO::swapStruct(Sec);
    // For MachO section indices start from 1.
    if (Expected<MachOYAML::Section> S =
            constructSection(Sec, Sections.size() + 1)) {
      StringRef SecName(S->sectname);

      // Copy data sections if requested.
      if ((RawSegment & ::RawSegments::data) &&
          StringRef(S->segname).starts_with("__DATA"))
        S->content =
            yaml::BinaryRef(Obj.getSectionContents(Sec.offset, Sec.size));

      if (SecName.starts_with("__debug_")) {
        // If the DWARF section cannot be successfully parsed, emit raw content
        // instead of an entry in the DWARF section of the YAML.
        if (Error Err = dumpDebugSection(SecName, *DWARFCtx, Y.DWARF))
          consumeError(std::move(Err));
        else
          S->content.reset();
      }
      Sections.push_back(std::move(*S));
    } else
      return S.takeError();
  }
  return reinterpret_cast<const char *>(Curr);
}

template <typename StructType>
Expected<const char *> MachODumper::processLoadCommandData(
    MachOYAML::LoadCommand &LC,
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    MachOYAML::Object &Y) {
  return LoadCmd.Ptr + sizeof(StructType);
}

template <>
Expected<const char *>
MachODumper::processLoadCommandData<MachO::segment_command>(
    MachOYAML::LoadCommand &LC,
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    MachOYAML::Object &Y) {
  return extractSections<MachO::section, MachO::segment_command>(
      LoadCmd, LC.Sections, Y);
}

template <>
Expected<const char *>
MachODumper::processLoadCommandData<MachO::segment_command_64>(
    MachOYAML::LoadCommand &LC,
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    MachOYAML::Object &Y) {
  return extractSections<MachO::section_64, MachO::segment_command_64>(
      LoadCmd, LC.Sections, Y);
}

template <typename StructType>
const char *
readString(MachOYAML::LoadCommand &LC,
           const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd) {
  auto Start = LoadCmd.Ptr + sizeof(StructType);
  auto MaxSize = LoadCmd.C.cmdsize - sizeof(StructType);
  auto Size = strnlen(Start, MaxSize);
  LC.Content = StringRef(Start, Size).str();
  return Start + Size;
}

template <>
Expected<const char *>
MachODumper::processLoadCommandData<MachO::dylib_command>(
    MachOYAML::LoadCommand &LC,
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    MachOYAML::Object &Y) {
  return readString<MachO::dylib_command>(LC, LoadCmd);
}

template <>
Expected<const char *>
MachODumper::processLoadCommandData<MachO::dylinker_command>(
    MachOYAML::LoadCommand &LC,
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    MachOYAML::Object &Y) {
  return readString<MachO::dylinker_command>(LC, LoadCmd);
}

template <>
Expected<const char *>
MachODumper::processLoadCommandData<MachO::rpath_command>(
    MachOYAML::LoadCommand &LC,
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    MachOYAML::Object &Y) {
  return readString<MachO::rpath_command>(LC, LoadCmd);
}

template <>
Expected<const char *>
MachODumper::processLoadCommandData<MachO::build_version_command>(
    MachOYAML::LoadCommand &LC,
    const llvm::object::MachOObjectFile::LoadCommandInfo &LoadCmd,
    MachOYAML::Object &Y) {
  auto Start = LoadCmd.Ptr + sizeof(MachO::build_version_command);
  auto NTools = LC.Data.build_version_command_data.ntools;
  for (unsigned i = 0; i < NTools; ++i) {
    auto Curr = Start + i * sizeof(MachO::build_tool_version);
    MachO::build_tool_version BV;
    memcpy((void *)&BV, Curr, sizeof(MachO::build_tool_version));
    if (Obj.isLittleEndian() != sys::IsLittleEndianHost)
      MachO::swapStruct(BV);
    LC.Tools.push_back(BV);
  }
  return Start + NTools * sizeof(MachO::build_tool_version);
}

Expected<std::unique_ptr<MachOYAML::Object>> MachODumper::dump() {
  auto Y = std::make_unique<MachOYAML::Object>();
  Y->IsLittleEndian = Obj.isLittleEndian();
  dumpHeader(Y);
  if (Error Err = dumpLoadCommands(Y))
    return std::move(Err);
  if (RawSegment & ::RawSegments::linkedit)
    Y->RawLinkEditSegment =
        yaml::BinaryRef(Obj.getSegmentContents("__LINKEDIT"));
  else
    dumpLinkEdit(Y);

  return std::move(Y);
}

void MachODumper::dumpHeader(std::unique_ptr<MachOYAML::Object> &Y) {
  Y->Header.magic = Obj.getHeader().magic;
  Y->Header.cputype = Obj.getHeader().cputype;
  Y->Header.cpusubtype = Obj.getHeader().cpusubtype;
  Y->Header.filetype = Obj.getHeader().filetype;
  Y->Header.ncmds = Obj.getHeader().ncmds;
  Y->Header.sizeofcmds = Obj.getHeader().sizeofcmds;
  Y->Header.flags = Obj.getHeader().flags;
  Y->Header.reserved = 0;
}

Error MachODumper::dumpLoadCommands(std::unique_ptr<MachOYAML::Object> &Y) {
  for (auto LoadCmd : Obj.load_commands()) {
    MachOYAML::LoadCommand LC;
    const char *EndPtr = LoadCmd.Ptr;
    switch (LoadCmd.C.cmd) {
    default:
      memcpy((void *)&(LC.Data.load_command_data), LoadCmd.Ptr,
             sizeof(MachO::load_command));
      if (Obj.isLittleEndian() != sys::IsLittleEndianHost)
        MachO::swapStruct(LC.Data.load_command_data);
      if (Expected<const char *> ExpectedEndPtr =
              processLoadCommandData<MachO::load_command>(LC, LoadCmd, *Y))
        EndPtr = *ExpectedEndPtr;
      else
        return ExpectedEndPtr.takeError();
      break;
#include "llvm/BinaryFormat/MachO.def"
    }
    auto RemainingBytes = LoadCmd.C.cmdsize - (EndPtr - LoadCmd.Ptr);
    if (!std::all_of(EndPtr, &EndPtr[RemainingBytes],
                     [](const char C) { return C == 0; })) {
      LC.PayloadBytes.insert(LC.PayloadBytes.end(), EndPtr,
                             &EndPtr[RemainingBytes]);
      RemainingBytes = 0;
    }
    LC.ZeroPadBytes = RemainingBytes;
    Y->LoadCommands.push_back(std::move(LC));
  }
  return Error::success();
}

void MachODumper::dumpLinkEdit(std::unique_ptr<MachOYAML::Object> &Y) {
  dumpRebaseOpcodes(Y);
  dumpBindOpcodes(Y->LinkEdit.BindOpcodes, Obj.getDyldInfoBindOpcodes());
  dumpBindOpcodes(Y->LinkEdit.WeakBindOpcodes,
                  Obj.getDyldInfoWeakBindOpcodes());
  dumpBindOpcodes(Y->LinkEdit.LazyBindOpcodes, Obj.getDyldInfoLazyBindOpcodes(),
                  true);
  dumpExportTrie(Y);
  dumpSymbols(Y);
  dumpIndirectSymbols(Y);
  dumpFunctionStarts(Y);
  dumpChainedFixups(Y);
  dumpDataInCode(Y);
}

void MachODumper::dumpFunctionStarts(std::unique_ptr<MachOYAML::Object> &Y) {
  MachOYAML::LinkEditData &LEData = Y->LinkEdit;

  auto FunctionStarts = Obj.getFunctionStarts();
  for (auto Addr : FunctionStarts)
    LEData.FunctionStarts.push_back(Addr);
}

void MachODumper::dumpRebaseOpcodes(std::unique_ptr<MachOYAML::Object> &Y) {
  MachOYAML::LinkEditData &LEData = Y->LinkEdit;

  auto RebaseOpcodes = Obj.getDyldInfoRebaseOpcodes();
  for (auto OpCode = RebaseOpcodes.begin(); OpCode != RebaseOpcodes.end();
       ++OpCode) {
    MachOYAML::RebaseOpcode RebaseOp;
    RebaseOp.Opcode =
        static_cast<MachO::RebaseOpcode>(*OpCode & MachO::REBASE_OPCODE_MASK);
    RebaseOp.Imm = *OpCode & MachO::REBASE_IMMEDIATE_MASK;

    unsigned Count;
    uint64_t ULEB = 0;

    switch (RebaseOp.Opcode) {
    case MachO::REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:

      ULEB = decodeULEB128(OpCode + 1, &Count);
      RebaseOp.ExtraData.push_back(ULEB);
      OpCode += Count;
      [[fallthrough]];
    // Intentionally no break here -- This opcode has two ULEB values
    case MachO::REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
    case MachO::REBASE_OPCODE_ADD_ADDR_ULEB:
    case MachO::REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
    case MachO::REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:

      ULEB = decodeULEB128(OpCode + 1, &Count);
      RebaseOp.ExtraData.push_back(ULEB);
      OpCode += Count;
      break;
    default:
      break;
    }

    LEData.RebaseOpcodes.push_back(RebaseOp);

    if (RebaseOp.Opcode == MachO::REBASE_OPCODE_DONE)
      break;
  }
}

StringRef ReadStringRef(const uint8_t *Start) {
  const uint8_t *Itr = Start;
  for (; *Itr; ++Itr)
    ;
  return StringRef(reinterpret_cast<const char *>(Start), Itr - Start);
}

void MachODumper::dumpBindOpcodes(
    std::vector<MachOYAML::BindOpcode> &BindOpcodes,
    ArrayRef<uint8_t> OpcodeBuffer, bool Lazy) {
  for (auto OpCode = OpcodeBuffer.begin(); OpCode != OpcodeBuffer.end();
       ++OpCode) {
    MachOYAML::BindOpcode BindOp;
    BindOp.Opcode =
        static_cast<MachO::BindOpcode>(*OpCode & MachO::BIND_OPCODE_MASK);
    BindOp.Imm = *OpCode & MachO::BIND_IMMEDIATE_MASK;

    unsigned Count;
    uint64_t ULEB = 0;
    int64_t SLEB = 0;

    switch (BindOp.Opcode) {
    case MachO::BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
      ULEB = decodeULEB128(OpCode + 1, &Count);
      BindOp.ULEBExtraData.push_back(ULEB);
      OpCode += Count;
      [[fallthrough]];
    // Intentionally no break here -- this opcode has two ULEB values

    case MachO::BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
    case MachO::BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
    case MachO::BIND_OPCODE_ADD_ADDR_ULEB:
    case MachO::BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
      ULEB = decodeULEB128(OpCode + 1, &Count);
      BindOp.ULEBExtraData.push_back(ULEB);
      OpCode += Count;
      break;

    case MachO::BIND_OPCODE_SET_ADDEND_SLEB:
      SLEB = decodeSLEB128(OpCode + 1, &Count);
      BindOp.SLEBExtraData.push_back(SLEB);
      OpCode += Count;
      break;

    case MachO::BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
      BindOp.Symbol = ReadStringRef(OpCode + 1);
      OpCode += BindOp.Symbol.size() + 1;
      break;
    default:
      break;
    }

    BindOpcodes.push_back(BindOp);

    // Lazy bindings have DONE opcodes between operations, so we need to keep
    // processing after a DONE.
    if (!Lazy && BindOp.Opcode == MachO::BIND_OPCODE_DONE)
      break;
  }
}

/*!
 * /brief processes a node from the export trie, and its children.
 *
 * To my knowledge there is no documentation of the encoded format of this data
 * other than in the heads of the Apple linker engineers. To that end hopefully
 * this comment and the implementation below can serve to light the way for
 * anyone crazy enough to come down this path in the future.
 *
 * This function reads and preserves the trie structure of the export trie. To
 * my knowledge there is no code anywhere else that reads the data and preserves
 * the Trie. LD64 (sources available at opensource.apple.com) has a similar
 * implementation that parses the export trie into a vector. That code as well
 * as LLVM's libObject MachO implementation were the basis for this.
 *
 * The export trie is an encoded trie. The node serialization is a bit awkward.
 * The below pseudo-code is the best description I've come up with for it.
 *
 * struct SerializedNode {
 *   ULEB128 TerminalSize;
 *   struct TerminalData { <-- This is only present if TerminalSize > 0
 *     ULEB128 Flags;
 *     ULEB128 Address; <-- Present if (! Flags & REEXPORT )
 *     ULEB128 Other; <-- Present if ( Flags & REEXPORT ||
 *                                     Flags & STUB_AND_RESOLVER )
 *     char[] ImportName; <-- Present if ( Flags & REEXPORT )
 *   }
 *   uint8_t ChildrenCount;
 *   Pair<char[], ULEB128> ChildNameOffsetPair[ChildrenCount];
 *   SerializedNode Children[ChildrenCount]
 * }
 *
 * Terminal nodes are nodes that represent actual exports. They can appear
 * anywhere in the tree other than at the root; they do not need to be leaf
 * nodes. When reading the data out of the trie this routine reads it in-order,
 * but it puts the child names and offsets directly into the child nodes. This
 * results in looping over the children twice during serialization and
 * de-serialization, but it makes the YAML representation more human readable.
 *
 * Below is an example of the graph from a "Hello World" executable:
 *
 * -------
 * | ''  |
 * -------
 *    |
 * -------
 * | '_' |
 * -------
 *    |
 *    |----------------------------------------|
 *    |                                        |
 *  ------------------------      ---------------------
 *  | '_mh_execute_header' |      | 'main'            |
 *  | Flags: 0x00000000    |      | Flags: 0x00000000 |
 *  | Addr:  0x00000000    |      | Addr:  0x00001160 |
 *  ------------------------      ---------------------
 *
 * This graph represents the trie for the exports "__mh_execute_header" and
 * "_main". In the graph only the "_main" and "__mh_execute_header" nodes are
 * terminal.
*/

const uint8_t *processExportNode(const uint8_t *Start, const uint8_t *CurrPtr,
                                 const uint8_t *const End,
                                 MachOYAML::ExportEntry &Entry) {
  if (CurrPtr >= End)
    return CurrPtr;
  unsigned Count = 0;
  Entry.TerminalSize = decodeULEB128(CurrPtr, &Count);
  CurrPtr += Count;
  if (Entry.TerminalSize != 0) {
    Entry.Flags = decodeULEB128(CurrPtr, &Count);
    CurrPtr += Count;
    if (Entry.Flags & MachO::EXPORT_SYMBOL_FLAGS_REEXPORT) {
      Entry.Address = 0;
      Entry.Other = decodeULEB128(CurrPtr, &Count);
      CurrPtr += Count;
      Entry.ImportName = std::string(reinterpret_cast<const char *>(CurrPtr));
    } else {
      Entry.Address = decodeULEB128(CurrPtr, &Count);
      CurrPtr += Count;
      if (Entry.Flags & MachO::EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
        Entry.Other = decodeULEB128(CurrPtr, &Count);
        CurrPtr += Count;
      } else
        Entry.Other = 0;
    }
  }
  uint8_t childrenCount = *CurrPtr++;
  if (childrenCount == 0)
    return CurrPtr;

  Entry.Children.insert(Entry.Children.begin(), (size_t)childrenCount,
                        MachOYAML::ExportEntry());
  for (auto &Child : Entry.Children) {
    Child.Name = std::string(reinterpret_cast<const char *>(CurrPtr));
    CurrPtr += Child.Name.length() + 1;
    Child.NodeOffset = decodeULEB128(CurrPtr, &Count);
    CurrPtr += Count;
  }
  for (auto &Child : Entry.Children) {
    CurrPtr = processExportNode(Start, Start + Child.NodeOffset, End, Child);
  }
  return CurrPtr;
}

void MachODumper::dumpExportTrie(std::unique_ptr<MachOYAML::Object> &Y) {
  MachOYAML::LinkEditData &LEData = Y->LinkEdit;
  // The exports trie can be in LC_DYLD_INFO or LC_DYLD_EXPORTS_TRIE
  auto ExportsTrie = Obj.getDyldInfoExportsTrie();
  if (ExportsTrie.empty())
    ExportsTrie = Obj.getDyldExportsTrie();
  processExportNode(ExportsTrie.begin(), ExportsTrie.begin(), ExportsTrie.end(),
                    LEData.ExportTrie);
}

template <typename nlist_t>
MachOYAML::NListEntry constructNameList(const nlist_t &nlist) {
  MachOYAML::NListEntry NL;
  NL.n_strx = nlist.n_strx;
  NL.n_type = nlist.n_type;
  NL.n_sect = nlist.n_sect;
  NL.n_desc = nlist.n_desc;
  NL.n_value = nlist.n_value;
  return NL;
}

void MachODumper::dumpSymbols(std::unique_ptr<MachOYAML::Object> &Y) {
  MachOYAML::LinkEditData &LEData = Y->LinkEdit;

  for (auto Symbol : Obj.symbols()) {
    MachOYAML::NListEntry NLE =
        Obj.is64Bit()
            ? constructNameList<MachO::nlist_64>(
                  Obj.getSymbol64TableEntry(Symbol.getRawDataRefImpl()))
            : constructNameList<MachO::nlist>(
                  Obj.getSymbolTableEntry(Symbol.getRawDataRefImpl()));
    LEData.NameList.push_back(NLE);
  }

  StringRef RemainingTable = Obj.getStringTableData();
  while (RemainingTable.size() > 0) {
    auto SymbolPair = RemainingTable.split('\0');
    RemainingTable = SymbolPair.second;
    LEData.StringTable.push_back(SymbolPair.first);
  }
}

void MachODumper::dumpIndirectSymbols(std::unique_ptr<MachOYAML::Object> &Y) {
  MachOYAML::LinkEditData &LEData = Y->LinkEdit;

  MachO::dysymtab_command DLC = Obj.getDysymtabLoadCommand();
  for (unsigned i = 0; i < DLC.nindirectsyms; ++i)
    LEData.IndirectSymbols.push_back(Obj.getIndirectSymbolTableEntry(DLC, i));
}

void MachODumper::dumpChainedFixups(std::unique_ptr<MachOYAML::Object> &Y) {
  MachOYAML::LinkEditData &LEData = Y->LinkEdit;

  for (const auto &LC : Y->LoadCommands) {
    if (LC.Data.load_command_data.cmd == llvm::MachO::LC_DYLD_CHAINED_FIXUPS) {
      const MachO::linkedit_data_command &DC =
          LC.Data.linkedit_data_command_data;
      if (DC.dataoff) {
        assert(DC.dataoff < Obj.getData().size());
        assert(DC.dataoff + DC.datasize <= Obj.getData().size());
        const char *Bytes = Obj.getData().data() + DC.dataoff;
        for (size_t Idx = 0; Idx < DC.datasize; Idx++) {
          LEData.ChainedFixups.push_back(Bytes[Idx]);
        }
      }
      break;
    }
  }
}

void MachODumper::dumpDataInCode(std::unique_ptr<MachOYAML::Object> &Y) {
  MachOYAML::LinkEditData &LEData = Y->LinkEdit;

  MachO::linkedit_data_command DIC = Obj.getDataInCodeLoadCommand();
  uint32_t NumEntries = DIC.datasize / sizeof(MachO::data_in_code_entry);
  for (uint32_t Idx = 0; Idx < NumEntries; ++Idx) {
    MachO::data_in_code_entry DICE =
        Obj.getDataInCodeTableEntry(DIC.dataoff, Idx);
    MachOYAML::DataInCodeEntry Entry{DICE.offset, DICE.length, DICE.kind};
    LEData.DataInCode.emplace_back(Entry);
  }
}

Error macho2yaml(raw_ostream &Out, const object::MachOObjectFile &Obj,
                 unsigned RawSegments) {
  std::unique_ptr<DWARFContext> DCtx = DWARFContext::create(Obj);
  MachODumper Dumper(Obj, std::move(DCtx), RawSegments);
  Expected<std::unique_ptr<MachOYAML::Object>> YAML = Dumper.dump();
  if (!YAML)
    return YAML.takeError();

  yaml::YamlObjectFile YAMLFile;
  YAMLFile.MachO = std::move(YAML.get());

  yaml::Output Yout(Out);
  Yout << YAMLFile;
  return Error::success();
}

Error macho2yaml(raw_ostream &Out, const object::MachOUniversalBinary &Obj,
                 unsigned RawSegments) {
  yaml::YamlObjectFile YAMLFile;
  YAMLFile.FatMachO.reset(new MachOYAML::UniversalBinary());
  MachOYAML::UniversalBinary &YAML = *YAMLFile.FatMachO;
  YAML.Header.magic = Obj.getMagic();
  YAML.Header.nfat_arch = Obj.getNumberOfObjects();

  for (auto Slice : Obj.objects()) {
    MachOYAML::FatArch arch;
    arch.cputype = Slice.getCPUType();
    arch.cpusubtype = Slice.getCPUSubType();
    arch.offset = Slice.getOffset();
    arch.size = Slice.getSize();
    arch.align = Slice.getAlign();
    arch.reserved = Slice.getReserved();
    YAML.FatArchs.push_back(arch);

    auto SliceObj = Slice.getAsObjectFile();
    if (!SliceObj)
      return SliceObj.takeError();

    std::unique_ptr<DWARFContext> DCtx = DWARFContext::create(*SliceObj.get());
    MachODumper Dumper(*SliceObj.get(), std::move(DCtx), RawSegments);
    Expected<std::unique_ptr<MachOYAML::Object>> YAMLObj = Dumper.dump();
    if (!YAMLObj)
      return YAMLObj.takeError();
    YAML.Slices.push_back(*YAMLObj.get());
  }

  yaml::Output Yout(Out);
  Yout << YAML;
  return Error::success();
}

Error macho2yaml(raw_ostream &Out, const object::Binary &Binary,
                 unsigned RawSegments) {
  if (const auto *MachOObj = dyn_cast<object::MachOUniversalBinary>(&Binary))
    return macho2yaml(Out, *MachOObj, RawSegments);

  if (const auto *MachOObj = dyn_cast<object::MachOObjectFile>(&Binary))
    return macho2yaml(Out, *MachOObj, RawSegments);

  llvm_unreachable("unexpected Mach-O file format");
}
