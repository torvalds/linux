//===- MachOWriter.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MachOWriter.h"
#include "MachOLayoutBuilder.h"
#include "MachOObject.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SHA256.h"
#include <memory>

#if defined(__APPLE__)
#include <sys/mman.h>
#endif

using namespace llvm;
using namespace llvm::objcopy::macho;
using namespace llvm::support::endian;

size_t MachOWriter::headerSize() const {
  return Is64Bit ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
}

size_t MachOWriter::loadCommandsSize() const { return O.Header.SizeOfCmds; }

size_t MachOWriter::symTableSize() const {
  return O.SymTable.Symbols.size() *
         (Is64Bit ? sizeof(MachO::nlist_64) : sizeof(MachO::nlist));
}

size_t MachOWriter::totalSize() const {
  // Going from tail to head and looking for an appropriate "anchor" to
  // calculate the total size assuming that all the offsets are either valid
  // ("true") or 0 (0 indicates that the corresponding part is missing).

  SmallVector<size_t, 7> Ends;
  if (O.SymTabCommandIndex) {
    const MachO::symtab_command &SymTabCommand =
        O.LoadCommands[*O.SymTabCommandIndex]
            .MachOLoadCommand.symtab_command_data;
    if (SymTabCommand.symoff)
      Ends.push_back(SymTabCommand.symoff + symTableSize());
    if (SymTabCommand.stroff)
      Ends.push_back(SymTabCommand.stroff + SymTabCommand.strsize);
  }
  if (O.DyLdInfoCommandIndex) {
    const MachO::dyld_info_command &DyLdInfoCommand =
        O.LoadCommands[*O.DyLdInfoCommandIndex]
            .MachOLoadCommand.dyld_info_command_data;
    if (DyLdInfoCommand.rebase_off) {
      assert((DyLdInfoCommand.rebase_size == O.Rebases.Opcodes.size()) &&
             "Incorrect rebase opcodes size");
      Ends.push_back(DyLdInfoCommand.rebase_off + DyLdInfoCommand.rebase_size);
    }
    if (DyLdInfoCommand.bind_off) {
      assert((DyLdInfoCommand.bind_size == O.Binds.Opcodes.size()) &&
             "Incorrect bind opcodes size");
      Ends.push_back(DyLdInfoCommand.bind_off + DyLdInfoCommand.bind_size);
    }
    if (DyLdInfoCommand.weak_bind_off) {
      assert((DyLdInfoCommand.weak_bind_size == O.WeakBinds.Opcodes.size()) &&
             "Incorrect weak bind opcodes size");
      Ends.push_back(DyLdInfoCommand.weak_bind_off +
                     DyLdInfoCommand.weak_bind_size);
    }
    if (DyLdInfoCommand.lazy_bind_off) {
      assert((DyLdInfoCommand.lazy_bind_size == O.LazyBinds.Opcodes.size()) &&
             "Incorrect lazy bind opcodes size");
      Ends.push_back(DyLdInfoCommand.lazy_bind_off +
                     DyLdInfoCommand.lazy_bind_size);
    }
    if (DyLdInfoCommand.export_off) {
      assert((DyLdInfoCommand.export_size == O.Exports.Trie.size()) &&
             "Incorrect trie size");
      Ends.push_back(DyLdInfoCommand.export_off + DyLdInfoCommand.export_size);
    }
  }

  if (O.DySymTabCommandIndex) {
    const MachO::dysymtab_command &DySymTabCommand =
        O.LoadCommands[*O.DySymTabCommandIndex]
            .MachOLoadCommand.dysymtab_command_data;

    if (DySymTabCommand.indirectsymoff)
      Ends.push_back(DySymTabCommand.indirectsymoff +
                     sizeof(uint32_t) * O.IndirectSymTable.Symbols.size());
  }

  for (std::optional<size_t> LinkEditDataCommandIndex :
       {O.CodeSignatureCommandIndex, O.DylibCodeSignDRsIndex,
        O.DataInCodeCommandIndex, O.LinkerOptimizationHintCommandIndex,
        O.FunctionStartsCommandIndex, O.ChainedFixupsCommandIndex,
        O.ExportsTrieCommandIndex})
    if (LinkEditDataCommandIndex) {
      const MachO::linkedit_data_command &LinkEditDataCommand =
          O.LoadCommands[*LinkEditDataCommandIndex]
              .MachOLoadCommand.linkedit_data_command_data;
      if (LinkEditDataCommand.dataoff)
        Ends.push_back(LinkEditDataCommand.dataoff +
                       LinkEditDataCommand.datasize);
    }

  // Otherwise, use the last section / reloction.
  for (const LoadCommand &LC : O.LoadCommands)
    for (const std::unique_ptr<Section> &S : LC.Sections) {
      if (!S->hasValidOffset()) {
        assert((S->Offset == 0) && "Skipped section's offset must be zero");
        assert((S->isVirtualSection() || S->Size == 0) &&
               "Non-zero-fill sections with zero offset must have zero size");
        continue;
      }
      assert((S->Offset != 0) &&
             "Non-zero-fill section's offset cannot be zero");
      Ends.push_back(S->Offset + S->Size);
      if (S->RelOff)
        Ends.push_back(S->RelOff +
                       S->NReloc * sizeof(MachO::any_relocation_info));
    }

  if (!Ends.empty())
    return *llvm::max_element(Ends);

  // Otherwise, we have only Mach header and load commands.
  return headerSize() + loadCommandsSize();
}

void MachOWriter::writeHeader() {
  MachO::mach_header_64 Header;

  Header.magic = O.Header.Magic;
  Header.cputype = O.Header.CPUType;
  Header.cpusubtype = O.Header.CPUSubType;
  Header.filetype = O.Header.FileType;
  Header.ncmds = O.Header.NCmds;
  Header.sizeofcmds = O.Header.SizeOfCmds;
  Header.flags = O.Header.Flags;
  Header.reserved = O.Header.Reserved;

  if (IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(Header);

  auto HeaderSize =
      Is64Bit ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
  memcpy(Buf->getBufferStart(), &Header, HeaderSize);
}

void MachOWriter::writeLoadCommands() {
  uint8_t *Begin =
      reinterpret_cast<uint8_t *>(Buf->getBufferStart()) + headerSize();
  for (const LoadCommand &LC : O.LoadCommands) {
    // Construct a load command.
    MachO::macho_load_command MLC = LC.MachOLoadCommand;
    switch (MLC.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
      if (IsLittleEndian != sys::IsLittleEndianHost)
        MachO::swapStruct(MLC.segment_command_data);
      memcpy(Begin, &MLC.segment_command_data, sizeof(MachO::segment_command));
      Begin += sizeof(MachO::segment_command);

      for (const std::unique_ptr<Section> &Sec : LC.Sections)
        writeSectionInLoadCommand<MachO::section>(*Sec, Begin);
      continue;
    case MachO::LC_SEGMENT_64:
      if (IsLittleEndian != sys::IsLittleEndianHost)
        MachO::swapStruct(MLC.segment_command_64_data);
      memcpy(Begin, &MLC.segment_command_64_data,
             sizeof(MachO::segment_command_64));
      Begin += sizeof(MachO::segment_command_64);

      for (const std::unique_ptr<Section> &Sec : LC.Sections)
        writeSectionInLoadCommand<MachO::section_64>(*Sec, Begin);
      continue;
    }

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct)                         \
  case MachO::LCName:                                                          \
    assert(sizeof(MachO::LCStruct) + LC.Payload.size() ==                      \
           MLC.load_command_data.cmdsize);                                     \
    if (IsLittleEndian != sys::IsLittleEndianHost)                             \
      MachO::swapStruct(MLC.LCStruct##_data);                                  \
    memcpy(Begin, &MLC.LCStruct##_data, sizeof(MachO::LCStruct));              \
    Begin += sizeof(MachO::LCStruct);                                          \
    if (!LC.Payload.empty())                                                   \
      memcpy(Begin, LC.Payload.data(), LC.Payload.size());                     \
    Begin += LC.Payload.size();                                                \
    break;

    // Copy the load command as it is.
    switch (MLC.load_command_data.cmd) {
    default:
      assert(sizeof(MachO::load_command) + LC.Payload.size() ==
             MLC.load_command_data.cmdsize);
      if (IsLittleEndian != sys::IsLittleEndianHost)
        MachO::swapStruct(MLC.load_command_data);
      memcpy(Begin, &MLC.load_command_data, sizeof(MachO::load_command));
      Begin += sizeof(MachO::load_command);
      if (!LC.Payload.empty())
        memcpy(Begin, LC.Payload.data(), LC.Payload.size());
      Begin += LC.Payload.size();
      break;
#include "llvm/BinaryFormat/MachO.def"
    }
  }
}

template <typename StructType>
void MachOWriter::writeSectionInLoadCommand(const Section &Sec, uint8_t *&Out) {
  StructType Temp;
  assert(Sec.Segname.size() <= sizeof(Temp.segname) && "too long segment name");
  assert(Sec.Sectname.size() <= sizeof(Temp.sectname) &&
         "too long section name");
  memset(&Temp, 0, sizeof(StructType));
  memcpy(Temp.segname, Sec.Segname.data(), Sec.Segname.size());
  memcpy(Temp.sectname, Sec.Sectname.data(), Sec.Sectname.size());
  Temp.addr = Sec.Addr;
  Temp.size = Sec.Size;
  Temp.offset = Sec.Offset;
  Temp.align = Sec.Align;
  Temp.reloff = Sec.RelOff;
  Temp.nreloc = Sec.NReloc;
  Temp.flags = Sec.Flags;
  Temp.reserved1 = Sec.Reserved1;
  Temp.reserved2 = Sec.Reserved2;

  if (IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(Temp);
  memcpy(Out, &Temp, sizeof(StructType));
  Out += sizeof(StructType);
}

void MachOWriter::writeSections() {
  for (const LoadCommand &LC : O.LoadCommands)
    for (const std::unique_ptr<Section> &Sec : LC.Sections) {
      if (!Sec->hasValidOffset()) {
        assert((Sec->Offset == 0) && "Skipped section's offset must be zero");
        assert((Sec->isVirtualSection() || Sec->Size == 0) &&
               "Non-zero-fill sections with zero offset must have zero size");
        continue;
      }

      assert(Sec->Offset && "Section offset can not be zero");
      assert((Sec->Size == Sec->Content.size()) && "Incorrect section size");
      memcpy(Buf->getBufferStart() + Sec->Offset, Sec->Content.data(),
             Sec->Content.size());
      for (size_t Index = 0; Index < Sec->Relocations.size(); ++Index) {
        RelocationInfo RelocInfo = Sec->Relocations[Index];
        if (!RelocInfo.Scattered && !RelocInfo.IsAddend) {
          const uint32_t SymbolNum = RelocInfo.Extern
                                         ? (*RelocInfo.Symbol)->Index
                                         : (*RelocInfo.Sec)->Index;
          RelocInfo.setPlainRelocationSymbolNum(SymbolNum, IsLittleEndian);
        }
        if (IsLittleEndian != sys::IsLittleEndianHost)
          MachO::swapStruct(
              reinterpret_cast<MachO::any_relocation_info &>(RelocInfo.Info));
        memcpy(Buf->getBufferStart() + Sec->RelOff +
                   Index * sizeof(MachO::any_relocation_info),
               &RelocInfo.Info, sizeof(RelocInfo.Info));
      }
    }
}

template <typename NListType>
void writeNListEntry(const SymbolEntry &SE, bool IsLittleEndian, char *&Out,
                     uint32_t Nstrx) {
  NListType ListEntry;
  ListEntry.n_strx = Nstrx;
  ListEntry.n_type = SE.n_type;
  ListEntry.n_sect = SE.n_sect;
  ListEntry.n_desc = SE.n_desc;
  ListEntry.n_value = SE.n_value;

  if (IsLittleEndian != sys::IsLittleEndianHost)
    MachO::swapStruct(ListEntry);
  memcpy(Out, reinterpret_cast<const char *>(&ListEntry), sizeof(NListType));
  Out += sizeof(NListType);
}

void MachOWriter::writeStringTable() {
  if (!O.SymTabCommandIndex)
    return;
  const MachO::symtab_command &SymTabCommand =
      O.LoadCommands[*O.SymTabCommandIndex]
          .MachOLoadCommand.symtab_command_data;

  uint8_t *StrTable = (uint8_t *)Buf->getBufferStart() + SymTabCommand.stroff;
  LayoutBuilder.getStringTableBuilder().write(StrTable);
}

void MachOWriter::writeSymbolTable() {
  if (!O.SymTabCommandIndex)
    return;
  const MachO::symtab_command &SymTabCommand =
      O.LoadCommands[*O.SymTabCommandIndex]
          .MachOLoadCommand.symtab_command_data;

  char *SymTable = (char *)Buf->getBufferStart() + SymTabCommand.symoff;
  for (auto &Symbol : O.SymTable.Symbols) {
    SymbolEntry *Sym = Symbol.get();
    uint32_t Nstrx = LayoutBuilder.getStringTableBuilder().getOffset(Sym->Name);

    if (Is64Bit)
      writeNListEntry<MachO::nlist_64>(*Sym, IsLittleEndian, SymTable, Nstrx);
    else
      writeNListEntry<MachO::nlist>(*Sym, IsLittleEndian, SymTable, Nstrx);
  }
}

void MachOWriter::writeRebaseInfo() {
  if (!O.DyLdInfoCommandIndex)
    return;
  const MachO::dyld_info_command &DyLdInfoCommand =
      O.LoadCommands[*O.DyLdInfoCommandIndex]
          .MachOLoadCommand.dyld_info_command_data;
  char *Out = (char *)Buf->getBufferStart() + DyLdInfoCommand.rebase_off;
  assert((DyLdInfoCommand.rebase_size == O.Rebases.Opcodes.size()) &&
         "Incorrect rebase opcodes size");
  memcpy(Out, O.Rebases.Opcodes.data(), O.Rebases.Opcodes.size());
}

void MachOWriter::writeBindInfo() {
  if (!O.DyLdInfoCommandIndex)
    return;
  const MachO::dyld_info_command &DyLdInfoCommand =
      O.LoadCommands[*O.DyLdInfoCommandIndex]
          .MachOLoadCommand.dyld_info_command_data;
  char *Out = (char *)Buf->getBufferStart() + DyLdInfoCommand.bind_off;
  assert((DyLdInfoCommand.bind_size == O.Binds.Opcodes.size()) &&
         "Incorrect bind opcodes size");
  memcpy(Out, O.Binds.Opcodes.data(), O.Binds.Opcodes.size());
}

void MachOWriter::writeWeakBindInfo() {
  if (!O.DyLdInfoCommandIndex)
    return;
  const MachO::dyld_info_command &DyLdInfoCommand =
      O.LoadCommands[*O.DyLdInfoCommandIndex]
          .MachOLoadCommand.dyld_info_command_data;
  char *Out = (char *)Buf->getBufferStart() + DyLdInfoCommand.weak_bind_off;
  assert((DyLdInfoCommand.weak_bind_size == O.WeakBinds.Opcodes.size()) &&
         "Incorrect weak bind opcodes size");
  memcpy(Out, O.WeakBinds.Opcodes.data(), O.WeakBinds.Opcodes.size());
}

void MachOWriter::writeLazyBindInfo() {
  if (!O.DyLdInfoCommandIndex)
    return;
  const MachO::dyld_info_command &DyLdInfoCommand =
      O.LoadCommands[*O.DyLdInfoCommandIndex]
          .MachOLoadCommand.dyld_info_command_data;
  char *Out = (char *)Buf->getBufferStart() + DyLdInfoCommand.lazy_bind_off;
  assert((DyLdInfoCommand.lazy_bind_size == O.LazyBinds.Opcodes.size()) &&
         "Incorrect lazy bind opcodes size");
  memcpy(Out, O.LazyBinds.Opcodes.data(), O.LazyBinds.Opcodes.size());
}

void MachOWriter::writeExportInfo() {
  if (!O.DyLdInfoCommandIndex)
    return;
  const MachO::dyld_info_command &DyLdInfoCommand =
      O.LoadCommands[*O.DyLdInfoCommandIndex]
          .MachOLoadCommand.dyld_info_command_data;
  char *Out = (char *)Buf->getBufferStart() + DyLdInfoCommand.export_off;
  assert((DyLdInfoCommand.export_size == O.Exports.Trie.size()) &&
         "Incorrect export trie size");
  memcpy(Out, O.Exports.Trie.data(), O.Exports.Trie.size());
}

void MachOWriter::writeIndirectSymbolTable() {
  if (!O.DySymTabCommandIndex)
    return;

  const MachO::dysymtab_command &DySymTabCommand =
      O.LoadCommands[*O.DySymTabCommandIndex]
          .MachOLoadCommand.dysymtab_command_data;

  uint32_t *Out =
      (uint32_t *)(Buf->getBufferStart() + DySymTabCommand.indirectsymoff);
  for (const IndirectSymbolEntry &Sym : O.IndirectSymTable.Symbols) {
    uint32_t Entry = (Sym.Symbol) ? (*Sym.Symbol)->Index : Sym.OriginalIndex;
    if (IsLittleEndian != sys::IsLittleEndianHost)
      sys::swapByteOrder(Entry);
    *Out++ = Entry;
  }
}

void MachOWriter::writeLinkData(std::optional<size_t> LCIndex,
                                const LinkData &LD) {
  if (!LCIndex)
    return;
  const MachO::linkedit_data_command &LinkEditDataCommand =
      O.LoadCommands[*LCIndex].MachOLoadCommand.linkedit_data_command_data;
  char *Out = (char *)Buf->getBufferStart() + LinkEditDataCommand.dataoff;
  assert((LinkEditDataCommand.datasize == LD.Data.size()) &&
         "Incorrect data size");
  memcpy(Out, LD.Data.data(), LD.Data.size());
}

static uint64_t
getSegmentFileOffset(const LoadCommand &TextSegmentLoadCommand) {
  const MachO::macho_load_command &MLC =
      TextSegmentLoadCommand.MachOLoadCommand;
  switch (MLC.load_command_data.cmd) {
  case MachO::LC_SEGMENT:
    return MLC.segment_command_data.fileoff;
  case MachO::LC_SEGMENT_64:
    return MLC.segment_command_64_data.fileoff;
  default:
    return 0;
  }
}

static uint64_t getSegmentFileSize(const LoadCommand &TextSegmentLoadCommand) {
  const MachO::macho_load_command &MLC =
      TextSegmentLoadCommand.MachOLoadCommand;
  switch (MLC.load_command_data.cmd) {
  case MachO::LC_SEGMENT:
    return MLC.segment_command_data.filesize;
  case MachO::LC_SEGMENT_64:
    return MLC.segment_command_64_data.filesize;
  default:
    return 0;
  }
}

void MachOWriter::writeCodeSignatureData() {
  // NOTE: This CodeSignature section behaviour must be kept in sync with that
  // performed in LLD's CodeSignatureSection::write /
  // CodeSignatureSection::writeHashes. Furthermore, this call must occur only
  // after the rest of the binary has already been written to the buffer. This
  // is because the buffer is read from to perform the necessary hashing.

  // The CodeSignature section is the last section in the MachO binary and
  // contains a hash of all content in the binary before it. Since llvm-objcopy
  // has likely modified the target binary, the hash must be regenerated
  // entirely. To generate this hash, we must read from the start of the binary
  // (HashReadStart) to just before the start of the CodeSignature section
  // (HashReadEnd).

  const CodeSignatureInfo &CodeSignature = LayoutBuilder.getCodeSignature();

  uint8_t *BufferStart = reinterpret_cast<uint8_t *>(Buf->getBufferStart());
  uint8_t *HashReadStart = BufferStart;
  uint8_t *HashReadEnd = BufferStart + CodeSignature.StartOffset;

  // The CodeSignature section begins with a header, after which the hashes
  // of each page of the binary are written.
  uint8_t *HashWriteStart = HashReadEnd + CodeSignature.AllHeadersSize;

  uint32_t TextSegmentFileOff = 0;
  uint32_t TextSegmentFileSize = 0;
  if (O.TextSegmentCommandIndex) {
    const LoadCommand &TextSegmentLoadCommand =
        O.LoadCommands[*O.TextSegmentCommandIndex];
    assert(TextSegmentLoadCommand.MachOLoadCommand.load_command_data.cmd ==
               MachO::LC_SEGMENT ||
           TextSegmentLoadCommand.MachOLoadCommand.load_command_data.cmd ==
               MachO::LC_SEGMENT_64);
    assert(StringRef(TextSegmentLoadCommand.MachOLoadCommand
                         .segment_command_data.segname) == "__TEXT");
    TextSegmentFileOff = getSegmentFileOffset(TextSegmentLoadCommand);
    TextSegmentFileSize = getSegmentFileSize(TextSegmentLoadCommand);
  }

  const uint32_t FileNamePad = CodeSignature.AllHeadersSize -
                               CodeSignature.FixedHeadersSize -
                               CodeSignature.OutputFileName.size();

  // Write code section header.
  auto *SuperBlob = reinterpret_cast<MachO::CS_SuperBlob *>(HashReadEnd);
  write32be(&SuperBlob->magic, MachO::CSMAGIC_EMBEDDED_SIGNATURE);
  write32be(&SuperBlob->length, CodeSignature.Size);
  write32be(&SuperBlob->count, 1);
  auto *BlobIndex = reinterpret_cast<MachO::CS_BlobIndex *>(&SuperBlob[1]);
  write32be(&BlobIndex->type, MachO::CSSLOT_CODEDIRECTORY);
  write32be(&BlobIndex->offset, CodeSignature.BlobHeadersSize);
  auto *CodeDirectory = reinterpret_cast<MachO::CS_CodeDirectory *>(
      HashReadEnd + CodeSignature.BlobHeadersSize);
  write32be(&CodeDirectory->magic, MachO::CSMAGIC_CODEDIRECTORY);
  write32be(&CodeDirectory->length,
            CodeSignature.Size - CodeSignature.BlobHeadersSize);
  write32be(&CodeDirectory->version, MachO::CS_SUPPORTSEXECSEG);
  write32be(&CodeDirectory->flags, MachO::CS_ADHOC | MachO::CS_LINKER_SIGNED);
  write32be(&CodeDirectory->hashOffset,
            sizeof(MachO::CS_CodeDirectory) +
                CodeSignature.OutputFileName.size() + FileNamePad);
  write32be(&CodeDirectory->identOffset, sizeof(MachO::CS_CodeDirectory));
  CodeDirectory->nSpecialSlots = 0;
  write32be(&CodeDirectory->nCodeSlots, CodeSignature.BlockCount);
  write32be(&CodeDirectory->codeLimit, CodeSignature.StartOffset);
  CodeDirectory->hashSize = static_cast<uint8_t>(CodeSignature.HashSize);
  CodeDirectory->hashType = MachO::kSecCodeSignatureHashSHA256;
  CodeDirectory->platform = 0;
  CodeDirectory->pageSize = CodeSignature.BlockSizeShift;
  CodeDirectory->spare2 = 0;
  CodeDirectory->scatterOffset = 0;
  CodeDirectory->teamOffset = 0;
  CodeDirectory->spare3 = 0;
  CodeDirectory->codeLimit64 = 0;
  write64be(&CodeDirectory->execSegBase, TextSegmentFileOff);
  write64be(&CodeDirectory->execSegLimit, TextSegmentFileSize);
  write64be(&CodeDirectory->execSegFlags, O.Header.FileType == MachO::MH_EXECUTE
                                              ? MachO::CS_EXECSEG_MAIN_BINARY
                                              : 0);

  auto *Id = reinterpret_cast<char *>(&CodeDirectory[1]);
  memcpy(Id, CodeSignature.OutputFileName.begin(),
         CodeSignature.OutputFileName.size());
  memset(Id + CodeSignature.OutputFileName.size(), 0, FileNamePad);

  // Write the hashes.
  uint8_t *CurrHashReadPosition = HashReadStart;
  uint8_t *CurrHashWritePosition = HashWriteStart;
  while (CurrHashReadPosition < HashReadEnd) {
    StringRef Block(reinterpret_cast<char *>(CurrHashReadPosition),
                    std::min(static_cast<size_t>(HashReadEnd
                             - CurrHashReadPosition),
                             static_cast<size_t>(CodeSignature.BlockSize)));
    SHA256 Hasher;
    Hasher.update(Block);
    std::array<uint8_t, 32> Hash = Hasher.final();
    assert(Hash.size() == CodeSignature.HashSize);
    memcpy(CurrHashWritePosition, Hash.data(), CodeSignature.HashSize);
    CurrHashReadPosition += CodeSignature.BlockSize;
    CurrHashWritePosition += CodeSignature.HashSize;
  }
#if defined(__APPLE__)
  // This is macOS-specific work-around and makes no sense for any
  // other host OS. See https://openradar.appspot.com/FB8914231
  //
  // The macOS kernel maintains a signature-verification cache to
  // quickly validate applications at time of execve(2).  The trouble
  // is that for the kernel creates the cache entry at the time of the
  // mmap(2) call, before we have a chance to write either the code to
  // sign or the signature header+hashes.  The fix is to invalidate
  // all cached data associated with the output file, thus discarding
  // the bogus prematurely-cached signature.
  msync(BufferStart, CodeSignature.StartOffset + CodeSignature.Size,
        MS_INVALIDATE);
#endif
}

void MachOWriter::writeDataInCodeData() {
  return writeLinkData(O.DataInCodeCommandIndex, O.DataInCode);
}

void MachOWriter::writeLinkerOptimizationHint() {
  return writeLinkData(O.LinkerOptimizationHintCommandIndex,
                       O.LinkerOptimizationHint);
}

void MachOWriter::writeFunctionStartsData() {
  return writeLinkData(O.FunctionStartsCommandIndex, O.FunctionStarts);
}

void MachOWriter::writeDylibCodeSignDRsData() {
  return writeLinkData(O.DylibCodeSignDRsIndex, O.DylibCodeSignDRs);
}

void MachOWriter::writeChainedFixupsData() {
  return writeLinkData(O.ChainedFixupsCommandIndex, O.ChainedFixups);
}

void MachOWriter::writeExportsTrieData() {
  if (!O.ExportsTrieCommandIndex)
    return;
  const MachO::linkedit_data_command &ExportsTrieCmd =
      O.LoadCommands[*O.ExportsTrieCommandIndex]
          .MachOLoadCommand.linkedit_data_command_data;
  char *Out = (char *)Buf->getBufferStart() + ExportsTrieCmd.dataoff;
  assert((ExportsTrieCmd.datasize == O.Exports.Trie.size()) &&
         "Incorrect export trie size");
  memcpy(Out, O.Exports.Trie.data(), O.Exports.Trie.size());
}

void MachOWriter::writeTail() {
  typedef void (MachOWriter::*WriteHandlerType)();
  typedef std::pair<uint64_t, WriteHandlerType> WriteOperation;
  SmallVector<WriteOperation, 7> Queue;

  if (O.SymTabCommandIndex) {
    const MachO::symtab_command &SymTabCommand =
        O.LoadCommands[*O.SymTabCommandIndex]
            .MachOLoadCommand.symtab_command_data;
    if (SymTabCommand.symoff)
      Queue.push_back({SymTabCommand.symoff, &MachOWriter::writeSymbolTable});
    if (SymTabCommand.stroff)
      Queue.push_back({SymTabCommand.stroff, &MachOWriter::writeStringTable});
  }

  if (O.DyLdInfoCommandIndex) {
    const MachO::dyld_info_command &DyLdInfoCommand =
        O.LoadCommands[*O.DyLdInfoCommandIndex]
            .MachOLoadCommand.dyld_info_command_data;
    if (DyLdInfoCommand.rebase_off)
      Queue.push_back(
          {DyLdInfoCommand.rebase_off, &MachOWriter::writeRebaseInfo});
    if (DyLdInfoCommand.bind_off)
      Queue.push_back({DyLdInfoCommand.bind_off, &MachOWriter::writeBindInfo});
    if (DyLdInfoCommand.weak_bind_off)
      Queue.push_back(
          {DyLdInfoCommand.weak_bind_off, &MachOWriter::writeWeakBindInfo});
    if (DyLdInfoCommand.lazy_bind_off)
      Queue.push_back(
          {DyLdInfoCommand.lazy_bind_off, &MachOWriter::writeLazyBindInfo});
    if (DyLdInfoCommand.export_off)
      Queue.push_back(
          {DyLdInfoCommand.export_off, &MachOWriter::writeExportInfo});
  }

  if (O.DySymTabCommandIndex) {
    const MachO::dysymtab_command &DySymTabCommand =
        O.LoadCommands[*O.DySymTabCommandIndex]
            .MachOLoadCommand.dysymtab_command_data;

    if (DySymTabCommand.indirectsymoff)
      Queue.emplace_back(DySymTabCommand.indirectsymoff,
                         &MachOWriter::writeIndirectSymbolTable);
  }

  std::initializer_list<std::pair<std::optional<size_t>, WriteHandlerType>>
      LinkEditDataCommandWriters = {
          {O.CodeSignatureCommandIndex, &MachOWriter::writeCodeSignatureData},
          {O.DylibCodeSignDRsIndex, &MachOWriter::writeDylibCodeSignDRsData},
          {O.DataInCodeCommandIndex, &MachOWriter::writeDataInCodeData},
          {O.LinkerOptimizationHintCommandIndex,
           &MachOWriter::writeLinkerOptimizationHint},
          {O.FunctionStartsCommandIndex, &MachOWriter::writeFunctionStartsData},
          {O.ChainedFixupsCommandIndex, &MachOWriter::writeChainedFixupsData},
          {O.ExportsTrieCommandIndex, &MachOWriter::writeExportsTrieData}};
  for (const auto &W : LinkEditDataCommandWriters) {
    std::optional<size_t> LinkEditDataCommandIndex;
    WriteHandlerType WriteHandler;
    std::tie(LinkEditDataCommandIndex, WriteHandler) = W;
    if (LinkEditDataCommandIndex) {
      const MachO::linkedit_data_command &LinkEditDataCommand =
          O.LoadCommands[*LinkEditDataCommandIndex]
              .MachOLoadCommand.linkedit_data_command_data;
      if (LinkEditDataCommand.dataoff)
        Queue.emplace_back(LinkEditDataCommand.dataoff, WriteHandler);
    }
  }

  llvm::sort(Queue, llvm::less_first());

  for (auto WriteOp : Queue)
    (this->*WriteOp.second)();
}

Error MachOWriter::finalize() { return LayoutBuilder.layout(); }

Error MachOWriter::write() {
  size_t TotalSize = totalSize();
  Buf = WritableMemoryBuffer::getNewMemBuffer(TotalSize);
  if (!Buf)
    return createStringError(errc::not_enough_memory,
                             "failed to allocate memory buffer of " +
                                 Twine::utohexstr(TotalSize) + " bytes");
  writeHeader();
  writeLoadCommands();
  writeSections();
  writeTail();

  // TODO: Implement direct writing to the output stream (without intermediate
  // memory buffer Buf).
  Out.write(Buf->getBufferStart(), Buf->getBufferSize());
  return Error::success();
}
