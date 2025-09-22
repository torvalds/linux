//===- MachOLayoutBuilder.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MachOLayoutBuilder.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SystemZ/zOSSupport.h"

using namespace llvm;
using namespace llvm::objcopy::macho;

StringTableBuilder::Kind
MachOLayoutBuilder::getStringTableBuilderKind(const Object &O, bool Is64Bit) {
  if (O.Header.FileType == MachO::HeaderFileType::MH_OBJECT)
    return Is64Bit ? StringTableBuilder::MachO64 : StringTableBuilder::MachO;
  return Is64Bit ? StringTableBuilder::MachO64Linked
                 : StringTableBuilder::MachOLinked;
}

uint32_t MachOLayoutBuilder::computeSizeOfCmds() const {
  uint32_t Size = 0;
  for (const LoadCommand &LC : O.LoadCommands) {
    const MachO::macho_load_command &MLC = LC.MachOLoadCommand;
    auto cmd = MLC.load_command_data.cmd;
    switch (cmd) {
    case MachO::LC_SEGMENT:
      Size += sizeof(MachO::segment_command) +
              sizeof(MachO::section) * LC.Sections.size();
      continue;
    case MachO::LC_SEGMENT_64:
      Size += sizeof(MachO::segment_command_64) +
              sizeof(MachO::section_64) * LC.Sections.size();
      continue;
    }

    switch (cmd) {
#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct)                         \
  case MachO::LCName:                                                          \
    Size += sizeof(MachO::LCStruct) + LC.Payload.size();                       \
    break;
#include "llvm/BinaryFormat/MachO.def"
#undef HANDLE_LOAD_COMMAND
    }
  }

  return Size;
}

void MachOLayoutBuilder::constructStringTable() {
  for (std::unique_ptr<SymbolEntry> &Sym : O.SymTable.Symbols)
    StrTableBuilder.add(Sym->Name);
  StrTableBuilder.finalize();
}

void MachOLayoutBuilder::updateSymbolIndexes() {
  uint32_t Index = 0;
  for (auto &Symbol : O.SymTable.Symbols)
    Symbol->Index = Index++;
}

// Updates the index and the number of local/external/undefined symbols.
void MachOLayoutBuilder::updateDySymTab(MachO::macho_load_command &MLC) {
  assert(MLC.load_command_data.cmd == MachO::LC_DYSYMTAB);
  // Make sure that nlist entries in the symbol table are sorted by the those
  // types. The order is: local < defined external < undefined external.
  assert(llvm::is_sorted(O.SymTable.Symbols,
                         [](const std::unique_ptr<SymbolEntry> &A,
                            const std::unique_ptr<SymbolEntry> &B) {
                           bool AL = A->isLocalSymbol(),
                                BL = B->isLocalSymbol();
                           if (AL != BL)
                             return AL;
                           return !AL && !A->isUndefinedSymbol() &&
                                  B->isUndefinedSymbol();
                         }) &&
         "Symbols are not sorted by their types.");

  uint32_t NumLocalSymbols = 0;
  auto Iter = O.SymTable.Symbols.begin();
  auto End = O.SymTable.Symbols.end();
  for (; Iter != End; ++Iter) {
    if ((*Iter)->isExternalSymbol())
      break;

    ++NumLocalSymbols;
  }

  uint32_t NumExtDefSymbols = 0;
  for (; Iter != End; ++Iter) {
    if ((*Iter)->isUndefinedSymbol())
      break;

    ++NumExtDefSymbols;
  }

  MLC.dysymtab_command_data.ilocalsym = 0;
  MLC.dysymtab_command_data.nlocalsym = NumLocalSymbols;
  MLC.dysymtab_command_data.iextdefsym = NumLocalSymbols;
  MLC.dysymtab_command_data.nextdefsym = NumExtDefSymbols;
  MLC.dysymtab_command_data.iundefsym = NumLocalSymbols + NumExtDefSymbols;
  MLC.dysymtab_command_data.nundefsym =
      O.SymTable.Symbols.size() - (NumLocalSymbols + NumExtDefSymbols);
}

// Recomputes and updates offset and size fields in load commands and sections
// since they could be modified.
uint64_t MachOLayoutBuilder::layoutSegments() {
  auto HeaderSize =
      Is64Bit ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
  const bool IsObjectFile =
      O.Header.FileType == MachO::HeaderFileType::MH_OBJECT;
  uint64_t Offset = IsObjectFile ? (HeaderSize + O.Header.SizeOfCmds) : 0;
  for (LoadCommand &LC : O.LoadCommands) {
    auto &MLC = LC.MachOLoadCommand;
    StringRef Segname;
    uint64_t SegmentVmAddr;
    uint64_t SegmentVmSize;
    switch (MLC.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
      SegmentVmAddr = MLC.segment_command_data.vmaddr;
      SegmentVmSize = MLC.segment_command_data.vmsize;
      Segname = StringRef(MLC.segment_command_data.segname,
                          strnlen(MLC.segment_command_data.segname,
                                  sizeof(MLC.segment_command_data.segname)));
      break;
    case MachO::LC_SEGMENT_64:
      SegmentVmAddr = MLC.segment_command_64_data.vmaddr;
      SegmentVmSize = MLC.segment_command_64_data.vmsize;
      Segname = StringRef(MLC.segment_command_64_data.segname,
                          strnlen(MLC.segment_command_64_data.segname,
                                  sizeof(MLC.segment_command_64_data.segname)));
      break;
    default:
      continue;
    }

    if (Segname == "__LINKEDIT") {
      // We update the __LINKEDIT segment later (in layoutTail).
      assert(LC.Sections.empty() && "__LINKEDIT segment has sections");
      LinkEditLoadCommand = &MLC;
      continue;
    }

    // Update file offsets and sizes of sections.
    uint64_t SegOffset = Offset;
    uint64_t SegFileSize = 0;
    uint64_t VMSize = 0;
    for (std::unique_ptr<Section> &Sec : LC.Sections) {
      assert(SegmentVmAddr <= Sec->Addr &&
             "Section's address cannot be smaller than Segment's one");
      uint32_t SectOffset = Sec->Addr - SegmentVmAddr;
      if (IsObjectFile) {
        if (!Sec->hasValidOffset()) {
          Sec->Offset = 0;
        } else {
          uint64_t PaddingSize =
              offsetToAlignment(SegFileSize, Align(1ull << Sec->Align));
          Sec->Offset = SegOffset + SegFileSize + PaddingSize;
          Sec->Size = Sec->Content.size();
          SegFileSize += PaddingSize + Sec->Size;
        }
      } else {
        if (!Sec->hasValidOffset()) {
          Sec->Offset = 0;
        } else {
          Sec->Offset = SegOffset + SectOffset;
          Sec->Size = Sec->Content.size();
          SegFileSize = std::max(SegFileSize, SectOffset + Sec->Size);
        }
      }
      VMSize = std::max(VMSize, SectOffset + Sec->Size);
    }

    if (IsObjectFile) {
      Offset += SegFileSize;
    } else {
      Offset = alignTo(Offset + SegFileSize, PageSize);
      SegFileSize = alignTo(SegFileSize, PageSize);
      // Use the original vmsize if the segment is __PAGEZERO.
      VMSize =
          Segname == "__PAGEZERO" ? SegmentVmSize : alignTo(VMSize, PageSize);
    }

    switch (MLC.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
      MLC.segment_command_data.cmdsize =
          sizeof(MachO::segment_command) +
          sizeof(MachO::section) * LC.Sections.size();
      MLC.segment_command_data.nsects = LC.Sections.size();
      MLC.segment_command_data.fileoff = SegOffset;
      MLC.segment_command_data.vmsize = VMSize;
      MLC.segment_command_data.filesize = SegFileSize;
      break;
    case MachO::LC_SEGMENT_64:
      MLC.segment_command_64_data.cmdsize =
          sizeof(MachO::segment_command_64) +
          sizeof(MachO::section_64) * LC.Sections.size();
      MLC.segment_command_64_data.nsects = LC.Sections.size();
      MLC.segment_command_64_data.fileoff = SegOffset;
      MLC.segment_command_64_data.vmsize = VMSize;
      MLC.segment_command_64_data.filesize = SegFileSize;
      break;
    }
  }

  return Offset;
}

uint64_t MachOLayoutBuilder::layoutRelocations(uint64_t Offset) {
  for (LoadCommand &LC : O.LoadCommands)
    for (std::unique_ptr<Section> &Sec : LC.Sections) {
      Sec->RelOff = Sec->Relocations.empty() ? 0 : Offset;
      Sec->NReloc = Sec->Relocations.size();
      Offset += sizeof(MachO::any_relocation_info) * Sec->NReloc;
    }

  return Offset;
}

Error MachOLayoutBuilder::layoutTail(uint64_t Offset) {
  // If we are building the layout of an executable or dynamic library
  // which does not have any segments other than __LINKEDIT,
  // the Offset can be equal to zero by this time. It happens because of the
  // convention that in such cases the file offsets specified by LC_SEGMENT
  // start with zero (unlike the case of a relocatable object file).
  const uint64_t HeaderSize =
      Is64Bit ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
  assert((!(O.Header.FileType == MachO::HeaderFileType::MH_OBJECT) ||
          Offset >= HeaderSize + O.Header.SizeOfCmds) &&
         "Incorrect tail offset");
  Offset = std::max(Offset, HeaderSize + O.Header.SizeOfCmds);

  // The exports trie can be in either LC_DYLD_INFO or in
  // LC_DYLD_EXPORTS_TRIE, but not both.
  size_t DyldInfoExportsTrieSize = 0;
  size_t DyldExportsTrieSize = 0;
  for (const auto &LC : O.LoadCommands) {
    switch (LC.MachOLoadCommand.load_command_data.cmd) {
    case MachO::LC_DYLD_INFO:
    case MachO::LC_DYLD_INFO_ONLY:
      DyldInfoExportsTrieSize = O.Exports.Trie.size();
      break;
    case MachO::LC_DYLD_EXPORTS_TRIE:
      DyldExportsTrieSize = O.Exports.Trie.size();
      break;
    default:
      break;
    }
  }
  assert((DyldInfoExportsTrieSize == 0 || DyldExportsTrieSize == 0) &&
         "Export trie in both LCs");

  uint64_t NListSize = Is64Bit ? sizeof(MachO::nlist_64) : sizeof(MachO::nlist);
  uint64_t StartOfLinkEdit = Offset;

  // The order of LINKEDIT elements is as follows:
  // rebase info, binding info, weak binding info, lazy binding info, export
  // trie, chained fixups, dyld exports trie, function starts, data-in-code,
  // symbol table, indirect symbol table, symbol table strings,
  // dylib codesign drs, and code signature.
  auto updateOffset = [&Offset](size_t Size) {
    uint64_t PreviousOffset = Offset;
    Offset += Size;
    return PreviousOffset;
  };

  uint64_t StartOfRebaseInfo = updateOffset(O.Rebases.Opcodes.size());
  uint64_t StartOfBindingInfo = updateOffset(O.Binds.Opcodes.size());
  uint64_t StartOfWeakBindingInfo = updateOffset(O.WeakBinds.Opcodes.size());
  uint64_t StartOfLazyBindingInfo = updateOffset(O.LazyBinds.Opcodes.size());
  uint64_t StartOfExportTrie = updateOffset(DyldInfoExportsTrieSize);
  uint64_t StartOfChainedFixups = updateOffset(O.ChainedFixups.Data.size());
  uint64_t StartOfDyldExportsTrie = updateOffset(DyldExportsTrieSize);
  uint64_t StartOfFunctionStarts = updateOffset(O.FunctionStarts.Data.size());
  uint64_t StartOfDataInCode = updateOffset(O.DataInCode.Data.size());
  uint64_t StartOfLinkerOptimizationHint =
      updateOffset(O.LinkerOptimizationHint.Data.size());
  uint64_t StartOfSymbols = updateOffset(NListSize * O.SymTable.Symbols.size());
  uint64_t StartOfIndirectSymbols =
      updateOffset(sizeof(uint32_t) * O.IndirectSymTable.Symbols.size());
  uint64_t StartOfSymbolStrings = updateOffset(StrTableBuilder.getSize());
  uint64_t StartOfDylibCodeSignDRs = updateOffset(O.DylibCodeSignDRs.Data.size());

  uint64_t StartOfCodeSignature = Offset;
  uint32_t CodeSignatureSize = 0;
  if (O.CodeSignatureCommandIndex) {
    StartOfCodeSignature = alignTo(StartOfCodeSignature, 16);

    // Note: These calculations are to be kept in sync with the same
    // calculations performed in LLD's CodeSignatureSection.
    const uint32_t AllHeadersSize =
        alignTo(CodeSignature.FixedHeadersSize + OutputFileName.size() + 1,
                CodeSignature.Align);
    const uint32_t BlockCount =
        (StartOfCodeSignature + CodeSignature.BlockSize - 1) /
        CodeSignature.BlockSize;
    const uint32_t Size =
        alignTo(AllHeadersSize + BlockCount * CodeSignature.HashSize,
                CodeSignature.Align);

    CodeSignature.StartOffset = StartOfCodeSignature;
    CodeSignature.AllHeadersSize = AllHeadersSize;
    CodeSignature.BlockCount = BlockCount;
    CodeSignature.OutputFileName = OutputFileName;
    CodeSignature.Size = Size;
    CodeSignatureSize = Size;
  }
  uint64_t LinkEditSize =
      StartOfCodeSignature + CodeSignatureSize - StartOfLinkEdit;

  // Now we have determined the layout of the contents of the __LINKEDIT
  // segment. Update its load command.
  if (LinkEditLoadCommand) {
    MachO::macho_load_command *MLC = LinkEditLoadCommand;
    switch (LinkEditLoadCommand->load_command_data.cmd) {
    case MachO::LC_SEGMENT:
      MLC->segment_command_data.cmdsize = sizeof(MachO::segment_command);
      MLC->segment_command_data.fileoff = StartOfLinkEdit;
      MLC->segment_command_data.vmsize = alignTo(LinkEditSize, PageSize);
      MLC->segment_command_data.filesize = LinkEditSize;
      break;
    case MachO::LC_SEGMENT_64:
      MLC->segment_command_64_data.cmdsize = sizeof(MachO::segment_command_64);
      MLC->segment_command_64_data.fileoff = StartOfLinkEdit;
      MLC->segment_command_64_data.vmsize = alignTo(LinkEditSize, PageSize);
      MLC->segment_command_64_data.filesize = LinkEditSize;
      break;
    }
  }

  for (LoadCommand &LC : O.LoadCommands) {
    auto &MLC = LC.MachOLoadCommand;
    auto cmd = MLC.load_command_data.cmd;
    switch (cmd) {
    case MachO::LC_CODE_SIGNATURE:
      MLC.linkedit_data_command_data.dataoff = StartOfCodeSignature;
      MLC.linkedit_data_command_data.datasize = CodeSignatureSize;
      break;
    case MachO::LC_DYLIB_CODE_SIGN_DRS:
      MLC.linkedit_data_command_data.dataoff = StartOfDylibCodeSignDRs;
      MLC.linkedit_data_command_data.datasize = O.DylibCodeSignDRs.Data.size();
      break;
    case MachO::LC_SYMTAB:
      MLC.symtab_command_data.symoff = StartOfSymbols;
      MLC.symtab_command_data.nsyms = O.SymTable.Symbols.size();
      MLC.symtab_command_data.stroff = StartOfSymbolStrings;
      MLC.symtab_command_data.strsize = StrTableBuilder.getSize();
      break;
    case MachO::LC_DYSYMTAB: {
      if (MLC.dysymtab_command_data.ntoc != 0 ||
          MLC.dysymtab_command_data.nmodtab != 0 ||
          MLC.dysymtab_command_data.nextrefsyms != 0 ||
          MLC.dysymtab_command_data.nlocrel != 0 ||
          MLC.dysymtab_command_data.nextrel != 0)
        return createStringError(llvm::errc::not_supported,
                                 "shared library is not yet supported");

      if (!O.IndirectSymTable.Symbols.empty()) {
        MLC.dysymtab_command_data.indirectsymoff = StartOfIndirectSymbols;
        MLC.dysymtab_command_data.nindirectsyms =
            O.IndirectSymTable.Symbols.size();
      }

      updateDySymTab(MLC);
      break;
    }
    case MachO::LC_DATA_IN_CODE:
      MLC.linkedit_data_command_data.dataoff = StartOfDataInCode;
      MLC.linkedit_data_command_data.datasize = O.DataInCode.Data.size();
      break;
    case MachO::LC_LINKER_OPTIMIZATION_HINT:
      MLC.linkedit_data_command_data.dataoff = StartOfLinkerOptimizationHint;
      MLC.linkedit_data_command_data.datasize =
          O.LinkerOptimizationHint.Data.size();
      break;
    case MachO::LC_FUNCTION_STARTS:
      MLC.linkedit_data_command_data.dataoff = StartOfFunctionStarts;
      MLC.linkedit_data_command_data.datasize = O.FunctionStarts.Data.size();
      break;
    case MachO::LC_DYLD_CHAINED_FIXUPS:
      MLC.linkedit_data_command_data.dataoff = StartOfChainedFixups;
      MLC.linkedit_data_command_data.datasize = O.ChainedFixups.Data.size();
      break;
    case MachO::LC_DYLD_EXPORTS_TRIE:
      MLC.linkedit_data_command_data.dataoff = StartOfDyldExportsTrie;
      MLC.linkedit_data_command_data.datasize = DyldExportsTrieSize;
      break;
    case MachO::LC_DYLD_INFO:
    case MachO::LC_DYLD_INFO_ONLY:
      MLC.dyld_info_command_data.rebase_off =
          O.Rebases.Opcodes.empty() ? 0 : StartOfRebaseInfo;
      MLC.dyld_info_command_data.rebase_size = O.Rebases.Opcodes.size();
      MLC.dyld_info_command_data.bind_off =
          O.Binds.Opcodes.empty() ? 0 : StartOfBindingInfo;
      MLC.dyld_info_command_data.bind_size = O.Binds.Opcodes.size();
      MLC.dyld_info_command_data.weak_bind_off =
          O.WeakBinds.Opcodes.empty() ? 0 : StartOfWeakBindingInfo;
      MLC.dyld_info_command_data.weak_bind_size = O.WeakBinds.Opcodes.size();
      MLC.dyld_info_command_data.lazy_bind_off =
          O.LazyBinds.Opcodes.empty() ? 0 : StartOfLazyBindingInfo;
      MLC.dyld_info_command_data.lazy_bind_size = O.LazyBinds.Opcodes.size();
      MLC.dyld_info_command_data.export_off =
          O.Exports.Trie.empty() ? 0 : StartOfExportTrie;
      MLC.dyld_info_command_data.export_size = DyldInfoExportsTrieSize;
      break;
    // Note that LC_ENCRYPTION_INFO.cryptoff despite its name and the comment in
    // <mach-o/loader.h> is not an offset in the binary file, instead, it is a
    // relative virtual address. At the moment modification of the __TEXT
    // segment of executables isn't supported anyway (e.g. data in code entries
    // are not recalculated). Moreover, in general
    // LC_ENCRYPT_INFO/LC_ENCRYPTION_INFO_64 are nontrivial to update because
    // without making additional assumptions (e.g. that the entire __TEXT
    // segment should be encrypted) we do not know how to recalculate the
    // boundaries of the encrypted part. For now just copy over these load
    // commands until we encounter a real world usecase where
    // LC_ENCRYPT_INFO/LC_ENCRYPTION_INFO_64 need to be adjusted.
    case MachO::LC_ENCRYPTION_INFO:
    case MachO::LC_ENCRYPTION_INFO_64:
    case MachO::LC_LOAD_DYLINKER:
    case MachO::LC_MAIN:
    case MachO::LC_RPATH:
    case MachO::LC_SEGMENT:
    case MachO::LC_SEGMENT_64:
    case MachO::LC_VERSION_MIN_MACOSX:
    case MachO::LC_VERSION_MIN_IPHONEOS:
    case MachO::LC_VERSION_MIN_TVOS:
    case MachO::LC_VERSION_MIN_WATCHOS:
    case MachO::LC_BUILD_VERSION:
    case MachO::LC_ID_DYLIB:
    case MachO::LC_LOAD_DYLIB:
    case MachO::LC_LOAD_WEAK_DYLIB:
    case MachO::LC_UUID:
    case MachO::LC_SOURCE_VERSION:
    case MachO::LC_THREAD:
    case MachO::LC_UNIXTHREAD:
    case MachO::LC_SUB_FRAMEWORK:
    case MachO::LC_SUB_UMBRELLA:
    case MachO::LC_SUB_CLIENT:
    case MachO::LC_SUB_LIBRARY:
    case MachO::LC_LINKER_OPTION:
      // Nothing to update.
      break;
    default:
      // Abort if it's unsupported in order to prevent corrupting the object.
      return createStringError(llvm::errc::not_supported,
                               "unsupported load command (cmd=0x%x)", cmd);
    }
  }

  return Error::success();
}

Error MachOLayoutBuilder::layout() {
  O.Header.NCmds = O.LoadCommands.size();
  O.Header.SizeOfCmds = computeSizeOfCmds();
  constructStringTable();
  updateSymbolIndexes();
  uint64_t Offset = layoutSegments();
  Offset = layoutRelocations(Offset);
  return layoutTail(Offset);
}
