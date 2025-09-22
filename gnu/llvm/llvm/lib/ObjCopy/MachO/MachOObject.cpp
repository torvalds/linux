//===- MachOObject.cpp - Mach-O object file model ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MachOObject.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/SystemZ/zOSSupport.h"
#include <unordered_set>

using namespace llvm;
using namespace llvm::objcopy::macho;

Section::Section(StringRef SegName, StringRef SectName)
    : Segname(SegName), Sectname(SectName),
      CanonicalName((Twine(SegName) + Twine(',') + SectName).str()) {}

Section::Section(StringRef SegName, StringRef SectName, StringRef Content)
    : Segname(SegName), Sectname(SectName),
      CanonicalName((Twine(SegName) + Twine(',') + SectName).str()),
      Content(Content) {}

const SymbolEntry *SymbolTable::getSymbolByIndex(uint32_t Index) const {
  assert(Index < Symbols.size() && "invalid symbol index");
  return Symbols[Index].get();
}

SymbolEntry *SymbolTable::getSymbolByIndex(uint32_t Index) {
  return const_cast<SymbolEntry *>(
      static_cast<const SymbolTable *>(this)->getSymbolByIndex(Index));
}

void SymbolTable::removeSymbols(
    function_ref<bool(const std::unique_ptr<SymbolEntry> &)> ToRemove) {
  llvm::erase_if(Symbols, ToRemove);
}

void Object::updateLoadCommandIndexes() {
  static constexpr char TextSegmentName[] = "__TEXT";
  // Update indices of special load commands
  for (size_t Index = 0, Size = LoadCommands.size(); Index < Size; ++Index) {
    LoadCommand &LC = LoadCommands[Index];
    switch (LC.MachOLoadCommand.load_command_data.cmd) {
    case MachO::LC_CODE_SIGNATURE:
      CodeSignatureCommandIndex = Index;
      break;
    case MachO::LC_SEGMENT:
      if (StringRef(LC.MachOLoadCommand.segment_command_data.segname) ==
          TextSegmentName)
        TextSegmentCommandIndex = Index;
      break;
    case MachO::LC_SEGMENT_64:
      if (StringRef(LC.MachOLoadCommand.segment_command_64_data.segname) ==
          TextSegmentName)
        TextSegmentCommandIndex = Index;
      break;
    case MachO::LC_SYMTAB:
      SymTabCommandIndex = Index;
      break;
    case MachO::LC_DYSYMTAB:
      DySymTabCommandIndex = Index;
      break;
    case MachO::LC_DYLD_INFO:
    case MachO::LC_DYLD_INFO_ONLY:
      DyLdInfoCommandIndex = Index;
      break;
    case MachO::LC_DATA_IN_CODE:
      DataInCodeCommandIndex = Index;
      break;
    case MachO::LC_LINKER_OPTIMIZATION_HINT:
      LinkerOptimizationHintCommandIndex = Index;
      break;
    case MachO::LC_FUNCTION_STARTS:
      FunctionStartsCommandIndex = Index;
      break;
    case MachO::LC_DYLIB_CODE_SIGN_DRS:
      DylibCodeSignDRsIndex = Index;
      break;
    case MachO::LC_DYLD_CHAINED_FIXUPS:
      ChainedFixupsCommandIndex = Index;
      break;
    case MachO::LC_DYLD_EXPORTS_TRIE:
      ExportsTrieCommandIndex = Index;
      break;
    }
  }
}

Error Object::removeLoadCommands(
    function_ref<bool(const LoadCommand &)> ToRemove) {
  auto It = std::stable_partition(
      LoadCommands.begin(), LoadCommands.end(),
      [&](const LoadCommand &LC) { return !ToRemove(LC); });
  LoadCommands.erase(It, LoadCommands.end());

  updateLoadCommandIndexes();
  return Error::success();
}

Error Object::removeSections(
    function_ref<bool(const std::unique_ptr<Section> &)> ToRemove) {
  DenseMap<uint32_t, const Section *> OldIndexToSection;
  uint32_t NextSectionIndex = 1;
  for (LoadCommand &LC : LoadCommands) {
    auto It = std::stable_partition(
        std::begin(LC.Sections), std::end(LC.Sections),
        [&](const std::unique_ptr<Section> &Sec) { return !ToRemove(Sec); });
    for (auto I = LC.Sections.begin(), End = It; I != End; ++I) {
      OldIndexToSection[(*I)->Index] = I->get();
      (*I)->Index = NextSectionIndex++;
    }
    LC.Sections.erase(It, LC.Sections.end());
  }

  auto IsDead = [&](const std::unique_ptr<SymbolEntry> &S) -> bool {
    std::optional<uint32_t> Section = S->section();
    return (Section && !OldIndexToSection.count(*Section));
  };

  SmallPtrSet<const SymbolEntry *, 2> DeadSymbols;
  for (const std::unique_ptr<SymbolEntry> &Sym : SymTable.Symbols)
    if (IsDead(Sym))
      DeadSymbols.insert(Sym.get());

  for (const LoadCommand &LC : LoadCommands)
    for (const std::unique_ptr<Section> &Sec : LC.Sections)
      for (const RelocationInfo &R : Sec->Relocations)
        if (R.Symbol && *R.Symbol && DeadSymbols.count(*R.Symbol))
          return createStringError(std::errc::invalid_argument,
                                   "symbol '%s' defined in section with index "
                                   "'%u' cannot be removed because it is "
                                   "referenced by a relocation in section '%s'",
                                   (*R.Symbol)->Name.c_str(),
                                   *((*R.Symbol)->section()),
                                   Sec->CanonicalName.c_str());
  SymTable.removeSymbols(IsDead);
  for (std::unique_ptr<SymbolEntry> &S : SymTable.Symbols)
    if (S->section())
      S->n_sect = OldIndexToSection[S->n_sect]->Index;
  return Error::success();
}

uint64_t Object::nextAvailableSegmentAddress() const {
  uint64_t HeaderSize =
      is64Bit() ? sizeof(MachO::mach_header_64) : sizeof(MachO::mach_header);
  uint64_t Addr = HeaderSize + Header.SizeOfCmds;
  for (const LoadCommand &LC : LoadCommands) {
    const MachO::macho_load_command &MLC = LC.MachOLoadCommand;
    switch (MLC.load_command_data.cmd) {
    case MachO::LC_SEGMENT:
      Addr = std::max(Addr,
                      static_cast<uint64_t>(MLC.segment_command_data.vmaddr) +
                          MLC.segment_command_data.vmsize);
      break;
    case MachO::LC_SEGMENT_64:
      Addr = std::max(Addr, MLC.segment_command_64_data.vmaddr +
                                MLC.segment_command_64_data.vmsize);
      break;
    default:
      continue;
    }
  }
  return Addr;
}

template <typename SegmentType>
static void
constructSegment(SegmentType &Seg, llvm::MachO::LoadCommandType CmdType,
                 StringRef SegName, uint64_t SegVMAddr, uint64_t SegVMSize) {
  assert(SegName.size() <= sizeof(Seg.segname) && "too long segment name");
  memset(&Seg, 0, sizeof(SegmentType));
  Seg.cmd = CmdType;
  strncpy(Seg.segname, SegName.data(), SegName.size());
  Seg.maxprot |=
      (MachO::VM_PROT_READ | MachO::VM_PROT_WRITE | MachO::VM_PROT_EXECUTE);
  Seg.initprot |=
      (MachO::VM_PROT_READ | MachO::VM_PROT_WRITE | MachO::VM_PROT_EXECUTE);
  Seg.vmaddr = SegVMAddr;
  Seg.vmsize = SegVMSize;
}

LoadCommand &Object::addSegment(StringRef SegName, uint64_t SegVMSize) {
  LoadCommand LC;
  const uint64_t SegVMAddr = nextAvailableSegmentAddress();
  if (is64Bit())
    constructSegment(LC.MachOLoadCommand.segment_command_64_data,
                     MachO::LC_SEGMENT_64, SegName, SegVMAddr, SegVMSize);
  else
    constructSegment(LC.MachOLoadCommand.segment_command_data,
                     MachO::LC_SEGMENT, SegName, SegVMAddr, SegVMSize);

  LoadCommands.push_back(std::move(LC));
  return LoadCommands.back();
}

/// Extracts a segment name from a string which is possibly non-null-terminated.
static StringRef extractSegmentName(const char *SegName) {
  return StringRef(SegName,
                   strnlen(SegName, sizeof(MachO::segment_command::segname)));
}

std::optional<StringRef> LoadCommand::getSegmentName() const {
  const MachO::macho_load_command &MLC = MachOLoadCommand;
  switch (MLC.load_command_data.cmd) {
  case MachO::LC_SEGMENT:
    return extractSegmentName(MLC.segment_command_data.segname);
  case MachO::LC_SEGMENT_64:
    return extractSegmentName(MLC.segment_command_64_data.segname);
  default:
    return std::nullopt;
  }
}

std::optional<uint64_t> LoadCommand::getSegmentVMAddr() const {
  const MachO::macho_load_command &MLC = MachOLoadCommand;
  switch (MLC.load_command_data.cmd) {
  case MachO::LC_SEGMENT:
    return MLC.segment_command_data.vmaddr;
  case MachO::LC_SEGMENT_64:
    return MLC.segment_command_64_data.vmaddr;
  default:
    return std::nullopt;
  }
}
