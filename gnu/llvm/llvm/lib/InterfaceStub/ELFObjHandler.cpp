//===- ELFObjHandler.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/

#include "llvm/InterfaceStub/ELFObjHandler.h"
#include "llvm/InterfaceStub/IFSStub.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include <optional>

using llvm::object::ELFObjectFile;

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;

namespace llvm {
namespace ifs {

// Simple struct to hold relevant .dynamic entries.
struct DynamicEntries {
  uint64_t StrTabAddr = 0;
  uint64_t StrSize = 0;
  std::optional<uint64_t> SONameOffset;
  std::vector<uint64_t> NeededLibNames;
  // Symbol table:
  uint64_t DynSymAddr = 0;
  // Hash tables:
  std::optional<uint64_t> ElfHash;
  std::optional<uint64_t> GnuHash;
};

/// This initializes an ELF file header with information specific to a binary
/// dynamic shared object.
/// Offsets, indexes, links, etc. for section and program headers are just
/// zero-initialized as they will be updated elsewhere.
///
/// @param ElfHeader Target ELFT::Ehdr to populate.
/// @param Machine Target architecture (e_machine from ELF specifications).
template <class ELFT>
static void initELFHeader(typename ELFT::Ehdr &ElfHeader, uint16_t Machine) {
  memset(&ElfHeader, 0, sizeof(ElfHeader));
  // ELF identification.
  ElfHeader.e_ident[EI_MAG0] = ElfMagic[EI_MAG0];
  ElfHeader.e_ident[EI_MAG1] = ElfMagic[EI_MAG1];
  ElfHeader.e_ident[EI_MAG2] = ElfMagic[EI_MAG2];
  ElfHeader.e_ident[EI_MAG3] = ElfMagic[EI_MAG3];
  ElfHeader.e_ident[EI_CLASS] = ELFT::Is64Bits ? ELFCLASS64 : ELFCLASS32;
  bool IsLittleEndian = ELFT::Endianness == llvm::endianness::little;
  ElfHeader.e_ident[EI_DATA] = IsLittleEndian ? ELFDATA2LSB : ELFDATA2MSB;
  ElfHeader.e_ident[EI_VERSION] = EV_CURRENT;
  ElfHeader.e_ident[EI_OSABI] = ELFOSABI_NONE;

  // Remainder of ELF header.
  ElfHeader.e_type = ET_DYN;
  ElfHeader.e_machine = Machine;
  ElfHeader.e_version = EV_CURRENT;
  ElfHeader.e_ehsize = sizeof(typename ELFT::Ehdr);
  ElfHeader.e_phentsize = sizeof(typename ELFT::Phdr);
  ElfHeader.e_shentsize = sizeof(typename ELFT::Shdr);
}

namespace {
template <class ELFT> struct OutputSection {
  using Elf_Shdr = typename ELFT::Shdr;
  std::string Name;
  Elf_Shdr Shdr;
  uint64_t Addr;
  uint64_t Offset;
  uint64_t Size;
  uint64_t Align;
  uint32_t Index;
  bool NoBits = true;
};

template <class T, class ELFT>
struct ContentSection : public OutputSection<ELFT> {
  T Content;
  ContentSection() { this->NoBits = false; }
};

// This class just wraps StringTableBuilder for the purpose of adding a
// default constructor.
class ELFStringTableBuilder : public StringTableBuilder {
public:
  ELFStringTableBuilder() : StringTableBuilder(StringTableBuilder::ELF) {}
};

template <class ELFT> class ELFSymbolTableBuilder {
public:
  using Elf_Sym = typename ELFT::Sym;

  ELFSymbolTableBuilder() { Symbols.push_back({}); }

  void add(size_t StNameOffset, uint64_t StSize, uint8_t StBind, uint8_t StType,
           uint8_t StOther, uint16_t StShndx) {
    Elf_Sym S{};
    S.st_name = StNameOffset;
    S.st_size = StSize;
    S.st_info = (StBind << 4) | (StType & 0xf);
    S.st_other = StOther;
    S.st_shndx = StShndx;
    Symbols.push_back(S);
  }

  size_t getSize() const { return Symbols.size() * sizeof(Elf_Sym); }

  void write(uint8_t *Buf) const {
    memcpy(Buf, Symbols.data(), sizeof(Elf_Sym) * Symbols.size());
  }

private:
  llvm::SmallVector<Elf_Sym, 8> Symbols;
};

template <class ELFT> class ELFDynamicTableBuilder {
public:
  using Elf_Dyn = typename ELFT::Dyn;

  size_t addAddr(uint64_t Tag, uint64_t Addr) {
    Elf_Dyn Entry;
    Entry.d_tag = Tag;
    Entry.d_un.d_ptr = Addr;
    Entries.push_back(Entry);
    return Entries.size() - 1;
  }

  void modifyAddr(size_t Index, uint64_t Addr) {
    Entries[Index].d_un.d_ptr = Addr;
  }

  size_t addValue(uint64_t Tag, uint64_t Value) {
    Elf_Dyn Entry;
    Entry.d_tag = Tag;
    Entry.d_un.d_val = Value;
    Entries.push_back(Entry);
    return Entries.size() - 1;
  }

  void modifyValue(size_t Index, uint64_t Value) {
    Entries[Index].d_un.d_val = Value;
  }

  size_t getSize() const {
    // Add DT_NULL entry at the end.
    return (Entries.size() + 1) * sizeof(Elf_Dyn);
  }

  void write(uint8_t *Buf) const {
    memcpy(Buf, Entries.data(), sizeof(Elf_Dyn) * Entries.size());
    // Add DT_NULL entry at the end.
    memset(Buf + sizeof(Elf_Dyn) * Entries.size(), 0, sizeof(Elf_Dyn));
  }

private:
  llvm::SmallVector<Elf_Dyn, 8> Entries;
};

template <class ELFT> class ELFStubBuilder {
public:
  using Elf_Ehdr = typename ELFT::Ehdr;
  using Elf_Shdr = typename ELFT::Shdr;
  using Elf_Phdr = typename ELFT::Phdr;
  using Elf_Sym = typename ELFT::Sym;
  using Elf_Addr = typename ELFT::Addr;
  using Elf_Dyn = typename ELFT::Dyn;

  ELFStubBuilder(const ELFStubBuilder &) = delete;
  ELFStubBuilder(ELFStubBuilder &&) = default;

  explicit ELFStubBuilder(const IFSStub &Stub) {
    DynSym.Name = ".dynsym";
    DynSym.Align = sizeof(Elf_Addr);
    DynStr.Name = ".dynstr";
    DynStr.Align = 1;
    DynTab.Name = ".dynamic";
    DynTab.Align = sizeof(Elf_Addr);
    ShStrTab.Name = ".shstrtab";
    ShStrTab.Align = 1;

    // Populate string tables.
    for (const IFSSymbol &Sym : Stub.Symbols)
      DynStr.Content.add(Sym.Name);
    for (const std::string &Lib : Stub.NeededLibs)
      DynStr.Content.add(Lib);
    if (Stub.SoName)
      DynStr.Content.add(*Stub.SoName);

    std::vector<OutputSection<ELFT> *> Sections = {&DynSym, &DynStr, &DynTab,
                                                   &ShStrTab};
    const OutputSection<ELFT> *LastSection = Sections.back();
    // Now set the Index and put sections names into ".shstrtab".
    uint64_t Index = 1;
    for (OutputSection<ELFT> *Sec : Sections) {
      Sec->Index = Index++;
      ShStrTab.Content.add(Sec->Name);
    }
    ShStrTab.Content.finalize();
    ShStrTab.Size = ShStrTab.Content.getSize();
    DynStr.Content.finalize();
    DynStr.Size = DynStr.Content.getSize();

    // Populate dynamic symbol table.
    for (const IFSSymbol &Sym : Stub.Symbols) {
      uint8_t Bind = Sym.Weak ? STB_WEAK : STB_GLOBAL;
      // For non-undefined symbols, value of the shndx is not relevant at link
      // time as long as it is not SHN_UNDEF. Set shndx to 1, which
      // points to ".dynsym".
      uint16_t Shndx = Sym.Undefined ? SHN_UNDEF : 1;
      uint64_t Size = Sym.Size.value_or(0);
      DynSym.Content.add(DynStr.Content.getOffset(Sym.Name), Size, Bind,
                         convertIFSSymbolTypeToELF(Sym.Type), 0, Shndx);
    }
    DynSym.Size = DynSym.Content.getSize();

    // Poplulate dynamic table.
    size_t DynSymIndex = DynTab.Content.addAddr(DT_SYMTAB, 0);
    size_t DynStrIndex = DynTab.Content.addAddr(DT_STRTAB, 0);
    DynTab.Content.addValue(DT_STRSZ, DynSym.Size);
    for (const std::string &Lib : Stub.NeededLibs)
      DynTab.Content.addValue(DT_NEEDED, DynStr.Content.getOffset(Lib));
    if (Stub.SoName)
      DynTab.Content.addValue(DT_SONAME,
                              DynStr.Content.getOffset(*Stub.SoName));
    DynTab.Size = DynTab.Content.getSize();
    // Calculate sections' addresses and offsets.
    uint64_t CurrentOffset = sizeof(Elf_Ehdr);
    for (OutputSection<ELFT> *Sec : Sections) {
      Sec->Offset = alignTo(CurrentOffset, Sec->Align);
      Sec->Addr = Sec->Offset;
      CurrentOffset = Sec->Offset + Sec->Size;
    }
    // Fill Addr back to dynamic table.
    DynTab.Content.modifyAddr(DynSymIndex, DynSym.Addr);
    DynTab.Content.modifyAddr(DynStrIndex, DynStr.Addr);
    // Write section headers of string tables.
    fillSymTabShdr(DynSym, SHT_DYNSYM);
    fillStrTabShdr(DynStr, SHF_ALLOC);
    fillDynTabShdr(DynTab);
    fillStrTabShdr(ShStrTab);

    // Finish initializing the ELF header.
    initELFHeader<ELFT>(ElfHeader, static_cast<uint16_t>(*Stub.Target.Arch));
    ElfHeader.e_shstrndx = ShStrTab.Index;
    ElfHeader.e_shnum = LastSection->Index + 1;
    ElfHeader.e_shoff =
        alignTo(LastSection->Offset + LastSection->Size, sizeof(Elf_Addr));
  }

  size_t getSize() const {
    return ElfHeader.e_shoff + ElfHeader.e_shnum * sizeof(Elf_Shdr);
  }

  void write(uint8_t *Data) const {
    write(Data, ElfHeader);
    DynSym.Content.write(Data + DynSym.Shdr.sh_offset);
    DynStr.Content.write(Data + DynStr.Shdr.sh_offset);
    DynTab.Content.write(Data + DynTab.Shdr.sh_offset);
    ShStrTab.Content.write(Data + ShStrTab.Shdr.sh_offset);
    writeShdr(Data, DynSym);
    writeShdr(Data, DynStr);
    writeShdr(Data, DynTab);
    writeShdr(Data, ShStrTab);
  }

private:
  Elf_Ehdr ElfHeader;
  ContentSection<ELFStringTableBuilder, ELFT> DynStr;
  ContentSection<ELFStringTableBuilder, ELFT> ShStrTab;
  ContentSection<ELFSymbolTableBuilder<ELFT>, ELFT> DynSym;
  ContentSection<ELFDynamicTableBuilder<ELFT>, ELFT> DynTab;

  template <class T> static void write(uint8_t *Data, const T &Value) {
    *reinterpret_cast<T *>(Data) = Value;
  }

  void fillStrTabShdr(ContentSection<ELFStringTableBuilder, ELFT> &StrTab,
                      uint32_t ShFlags = 0) const {
    StrTab.Shdr.sh_type = SHT_STRTAB;
    StrTab.Shdr.sh_flags = ShFlags;
    StrTab.Shdr.sh_addr = StrTab.Addr;
    StrTab.Shdr.sh_offset = StrTab.Offset;
    StrTab.Shdr.sh_info = 0;
    StrTab.Shdr.sh_size = StrTab.Size;
    StrTab.Shdr.sh_name = ShStrTab.Content.getOffset(StrTab.Name);
    StrTab.Shdr.sh_addralign = StrTab.Align;
    StrTab.Shdr.sh_entsize = 0;
    StrTab.Shdr.sh_link = 0;
  }
  void fillSymTabShdr(ContentSection<ELFSymbolTableBuilder<ELFT>, ELFT> &SymTab,
                      uint32_t ShType) const {
    SymTab.Shdr.sh_type = ShType;
    SymTab.Shdr.sh_flags = SHF_ALLOC;
    SymTab.Shdr.sh_addr = SymTab.Addr;
    SymTab.Shdr.sh_offset = SymTab.Offset;
    // Only non-local symbols are included in the tbe file, so .dynsym only
    // contains 1 local symbol (the undefined symbol at index 0). The sh_info
    // should always be 1.
    SymTab.Shdr.sh_info = 1;
    SymTab.Shdr.sh_size = SymTab.Size;
    SymTab.Shdr.sh_name = this->ShStrTab.Content.getOffset(SymTab.Name);
    SymTab.Shdr.sh_addralign = SymTab.Align;
    SymTab.Shdr.sh_entsize = sizeof(Elf_Sym);
    SymTab.Shdr.sh_link = this->DynStr.Index;
  }
  void fillDynTabShdr(
      ContentSection<ELFDynamicTableBuilder<ELFT>, ELFT> &DynTab) const {
    DynTab.Shdr.sh_type = SHT_DYNAMIC;
    DynTab.Shdr.sh_flags = SHF_ALLOC;
    DynTab.Shdr.sh_addr = DynTab.Addr;
    DynTab.Shdr.sh_offset = DynTab.Offset;
    DynTab.Shdr.sh_info = 0;
    DynTab.Shdr.sh_size = DynTab.Size;
    DynTab.Shdr.sh_name = this->ShStrTab.Content.getOffset(DynTab.Name);
    DynTab.Shdr.sh_addralign = DynTab.Align;
    DynTab.Shdr.sh_entsize = sizeof(Elf_Dyn);
    DynTab.Shdr.sh_link = this->DynStr.Index;
  }
  uint64_t shdrOffset(const OutputSection<ELFT> &Sec) const {
    return ElfHeader.e_shoff + Sec.Index * sizeof(Elf_Shdr);
  }

  void writeShdr(uint8_t *Data, const OutputSection<ELFT> &Sec) const {
    write(Data + shdrOffset(Sec), Sec.Shdr);
  }
};

/// This function takes an error, and appends a string of text to the end of
/// that error. Since "appending" to an Error isn't supported behavior of an
/// Error, this function technically creates a new error with the combined
/// message and consumes the old error.
///
/// @param Err Source error.
/// @param After Text to append at the end of Err's error message.
Error appendToError(Error Err, StringRef After) {
  std::string Message;
  raw_string_ostream Stream(Message);
  Stream << Err;
  Stream << " " << After;
  consumeError(std::move(Err));
  return createError(Stream.str());
}

template <class ELFT> class DynSym {
  using Elf_Shdr_Range = typename ELFT::ShdrRange;
  using Elf_Shdr = typename ELFT::Shdr;

public:
  static Expected<DynSym> create(const ELFFile<ELFT> &ElfFile,
                                 const DynamicEntries &DynEnt) {
    Expected<Elf_Shdr_Range> Shdrs = ElfFile.sections();
    if (!Shdrs)
      return Shdrs.takeError();
    return DynSym(ElfFile, DynEnt, *Shdrs);
  }

  Expected<const uint8_t *> getDynSym() {
    if (DynSymHdr)
      return ElfFile.base() + DynSymHdr->sh_offset;
    return getDynamicData(DynEnt.DynSymAddr, "dynamic symbol table");
  }

  Expected<StringRef> getDynStr() {
    if (DynSymHdr)
      return ElfFile.getStringTableForSymtab(*DynSymHdr, Shdrs);
    Expected<const uint8_t *> DataOrErr = getDynamicData(
        DynEnt.StrTabAddr, "dynamic string table", DynEnt.StrSize);
    if (!DataOrErr)
      return DataOrErr.takeError();
    return StringRef(reinterpret_cast<const char *>(*DataOrErr),
                     DynEnt.StrSize);
  }

private:
  DynSym(const ELFFile<ELFT> &ElfFile, const DynamicEntries &DynEnt,
         Elf_Shdr_Range Shdrs)
      : ElfFile(ElfFile), DynEnt(DynEnt), Shdrs(Shdrs),
        DynSymHdr(findDynSymHdr()) {}

  const Elf_Shdr *findDynSymHdr() {
    for (const Elf_Shdr &Sec : Shdrs)
      if (Sec.sh_type == SHT_DYNSYM) {
        // If multiple .dynsym are present, use the first one.
        // This behavior aligns with llvm::object::ELFFile::getDynSymtabSize()
        return &Sec;
      }
    return nullptr;
  }

  Expected<const uint8_t *> getDynamicData(uint64_t EntAddr, StringRef Name,
                                           uint64_t Size = 0) {
    Expected<const uint8_t *> SecPtr = ElfFile.toMappedAddr(EntAddr);
    if (!SecPtr)
      return appendToError(
          SecPtr.takeError(),
          ("when locating " + Name + " section contents").str());
    Expected<const uint8_t *> SecEndPtr = ElfFile.toMappedAddr(EntAddr + Size);
    if (!SecEndPtr)
      return appendToError(
          SecEndPtr.takeError(),
          ("when locating " + Name + " section contents").str());
    return *SecPtr;
  }

  const ELFFile<ELFT> &ElfFile;
  const DynamicEntries &DynEnt;
  Elf_Shdr_Range Shdrs;
  const Elf_Shdr *DynSymHdr;
};
} // end anonymous namespace

/// This function behaves similarly to StringRef::substr(), but attempts to
/// terminate the returned StringRef at the first null terminator. If no null
/// terminator is found, an error is returned.
///
/// @param Str Source string to create a substring from.
/// @param Offset The start index of the desired substring.
static Expected<StringRef> terminatedSubstr(StringRef Str, size_t Offset) {
  size_t StrEnd = Str.find('\0', Offset);
  if (StrEnd == StringLiteral::npos) {
    return createError(
        "String overran bounds of string table (no null terminator)");
  }

  size_t StrLen = StrEnd - Offset;
  return Str.substr(Offset, StrLen);
}

/// This function populates a DynamicEntries struct using an ELFT::DynRange.
/// After populating the struct, the members are validated with
/// some basic correctness checks.
///
/// @param Dyn Target DynamicEntries struct to populate.
/// @param DynTable Source dynamic table.
template <class ELFT>
static Error populateDynamic(DynamicEntries &Dyn,
                             typename ELFT::DynRange DynTable) {
  if (DynTable.empty())
    return createError("No .dynamic section found");

  // Search .dynamic for relevant entries.
  bool FoundDynStr = false;
  bool FoundDynStrSz = false;
  bool FoundDynSym = false;
  for (auto &Entry : DynTable) {
    switch (Entry.d_tag) {
    case DT_SONAME:
      Dyn.SONameOffset = Entry.d_un.d_val;
      break;
    case DT_STRTAB:
      Dyn.StrTabAddr = Entry.d_un.d_ptr;
      FoundDynStr = true;
      break;
    case DT_STRSZ:
      Dyn.StrSize = Entry.d_un.d_val;
      FoundDynStrSz = true;
      break;
    case DT_NEEDED:
      Dyn.NeededLibNames.push_back(Entry.d_un.d_val);
      break;
    case DT_SYMTAB:
      Dyn.DynSymAddr = Entry.d_un.d_ptr;
      FoundDynSym = true;
      break;
    case DT_HASH:
      Dyn.ElfHash = Entry.d_un.d_ptr;
      break;
    case DT_GNU_HASH:
      Dyn.GnuHash = Entry.d_un.d_ptr;
    }
  }

  if (!FoundDynStr) {
    return createError(
        "Couldn't locate dynamic string table (no DT_STRTAB entry)");
  }
  if (!FoundDynStrSz) {
    return createError(
        "Couldn't determine dynamic string table size (no DT_STRSZ entry)");
  }
  if (!FoundDynSym) {
    return createError(
        "Couldn't locate dynamic symbol table (no DT_SYMTAB entry)");
  }
  if (Dyn.SONameOffset && *Dyn.SONameOffset >= Dyn.StrSize) {
    return createStringError(object_error::parse_failed,
                             "DT_SONAME string offset (0x%016" PRIx64
                             ") outside of dynamic string table",
                             *Dyn.SONameOffset);
  }
  for (uint64_t Offset : Dyn.NeededLibNames) {
    if (Offset >= Dyn.StrSize) {
      return createStringError(object_error::parse_failed,
                               "DT_NEEDED string offset (0x%016" PRIx64
                               ") outside of dynamic string table",
                               Offset);
    }
  }

  return Error::success();
}

/// This function creates an IFSSymbol and populates all members using
/// information from a binary ELFT::Sym.
///
/// @param SymName The desired name of the IFSSymbol.
/// @param RawSym ELFT::Sym to extract symbol information from.
template <class ELFT>
static IFSSymbol createELFSym(StringRef SymName,
                              const typename ELFT::Sym &RawSym) {
  IFSSymbol TargetSym{std::string(SymName)};
  uint8_t Binding = RawSym.getBinding();
  if (Binding == STB_WEAK)
    TargetSym.Weak = true;
  else
    TargetSym.Weak = false;

  TargetSym.Undefined = RawSym.isUndefined();
  TargetSym.Type = convertELFSymbolTypeToIFS(RawSym.st_info);

  if (TargetSym.Type == IFSSymbolType::Func) {
    TargetSym.Size = 0;
  } else {
    TargetSym.Size = RawSym.st_size;
  }
  return TargetSym;
}

/// This function populates an IFSStub with symbols using information read
/// from an ELF binary.
///
/// @param TargetStub IFSStub to add symbols to.
/// @param DynSym Range of dynamic symbols to add to TargetStub.
/// @param DynStr StringRef to the dynamic string table.
template <class ELFT>
static Error populateSymbols(IFSStub &TargetStub,
                             const typename ELFT::SymRange DynSym,
                             StringRef DynStr) {
  // Skips the first symbol since it's the NULL symbol.
  for (auto RawSym : DynSym.drop_front(1)) {
    // If a symbol does not have global or weak binding, ignore it.
    uint8_t Binding = RawSym.getBinding();
    if (!(Binding == STB_GLOBAL || Binding == STB_WEAK))
      continue;
    // If a symbol doesn't have default or protected visibility, ignore it.
    uint8_t Visibility = RawSym.getVisibility();
    if (!(Visibility == STV_DEFAULT || Visibility == STV_PROTECTED))
      continue;
    // Create an IFSSymbol and populate it with information from the symbol
    // table entry.
    Expected<StringRef> SymName = terminatedSubstr(DynStr, RawSym.st_name);
    if (!SymName)
      return SymName.takeError();
    IFSSymbol Sym = createELFSym<ELFT>(*SymName, RawSym);
    TargetStub.Symbols.push_back(std::move(Sym));
    // TODO: Populate symbol warning.
  }
  return Error::success();
}

/// Returns a new IFSStub with all members populated from an ELFObjectFile.
/// @param ElfObj Source ELFObjectFile.
template <class ELFT>
static Expected<std::unique_ptr<IFSStub>>
buildStub(const ELFObjectFile<ELFT> &ElfObj) {
  using Elf_Dyn_Range = typename ELFT::DynRange;
  using Elf_Sym_Range = typename ELFT::SymRange;
  using Elf_Sym = typename ELFT::Sym;
  std::unique_ptr<IFSStub> DestStub = std::make_unique<IFSStub>();
  const ELFFile<ELFT> &ElfFile = ElfObj.getELFFile();
  // Fetch .dynamic table.
  Expected<Elf_Dyn_Range> DynTable = ElfFile.dynamicEntries();
  if (!DynTable) {
    return DynTable.takeError();
  }

  // Collect relevant .dynamic entries.
  DynamicEntries DynEnt;
  if (Error Err = populateDynamic<ELFT>(DynEnt, *DynTable))
    return std::move(Err);
  Expected<DynSym<ELFT>> EDynSym = DynSym<ELFT>::create(ElfFile, DynEnt);
  if (!EDynSym)
    return EDynSym.takeError();

  Expected<StringRef> EDynStr = EDynSym->getDynStr();
  if (!EDynStr)
    return EDynStr.takeError();

  StringRef DynStr = *EDynStr;

  // Populate Arch from ELF header.
  DestStub->Target.Arch = static_cast<IFSArch>(ElfFile.getHeader().e_machine);
  DestStub->Target.BitWidth =
      convertELFBitWidthToIFS(ElfFile.getHeader().e_ident[EI_CLASS]);
  DestStub->Target.Endianness =
      convertELFEndiannessToIFS(ElfFile.getHeader().e_ident[EI_DATA]);
  DestStub->Target.ObjectFormat = "ELF";

  // Populate SoName from .dynamic entries and dynamic string table.
  if (DynEnt.SONameOffset) {
    Expected<StringRef> NameOrErr =
        terminatedSubstr(DynStr, *DynEnt.SONameOffset);
    if (!NameOrErr) {
      return appendToError(NameOrErr.takeError(), "when reading DT_SONAME");
    }
    DestStub->SoName = std::string(*NameOrErr);
  }

  // Populate NeededLibs from .dynamic entries and dynamic string table.
  for (uint64_t NeededStrOffset : DynEnt.NeededLibNames) {
    Expected<StringRef> LibNameOrErr =
        terminatedSubstr(DynStr, NeededStrOffset);
    if (!LibNameOrErr) {
      return appendToError(LibNameOrErr.takeError(), "when reading DT_NEEDED");
    }
    DestStub->NeededLibs.push_back(std::string(*LibNameOrErr));
  }

  // Populate Symbols from .dynsym table and dynamic string table.
  Expected<uint64_t> SymCount = ElfFile.getDynSymtabSize();
  if (!SymCount)
    return SymCount.takeError();
  if (*SymCount > 0) {
    // Get pointer to in-memory location of .dynsym section.
    Expected<const uint8_t *> DynSymPtr = EDynSym->getDynSym();
    if (!DynSymPtr)
      return appendToError(DynSymPtr.takeError(),
                           "when locating .dynsym section contents");
    Elf_Sym_Range DynSyms = ArrayRef<Elf_Sym>(
        reinterpret_cast<const Elf_Sym *>(*DynSymPtr), *SymCount);
    Error SymReadError = populateSymbols<ELFT>(*DestStub, DynSyms, DynStr);
    if (SymReadError)
      return appendToError(std::move(SymReadError),
                           "when reading dynamic symbols");
  }

  return std::move(DestStub);
}

/// This function opens a file for writing and then writes a binary ELF stub to
/// the file.
///
/// @param FilePath File path for writing the ELF binary.
/// @param Stub Source InterFace Stub to generate a binary ELF stub from.
template <class ELFT>
static Error writeELFBinaryToFile(StringRef FilePath, const IFSStub &Stub,
                                  bool WriteIfChanged) {
  ELFStubBuilder<ELFT> Builder{Stub};
  // Write Stub to memory first.
  std::vector<uint8_t> Buf(Builder.getSize());
  Builder.write(Buf.data());

  if (WriteIfChanged) {
    if (ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrError =
            MemoryBuffer::getFile(FilePath)) {
      // Compare Stub output with existing Stub file.
      // If Stub file unchanged, abort updating.
      if ((*BufOrError)->getBufferSize() == Builder.getSize() &&
          !memcmp((*BufOrError)->getBufferStart(), Buf.data(),
                  Builder.getSize()))
        return Error::success();
    }
  }

  Expected<std::unique_ptr<FileOutputBuffer>> BufOrError =
      FileOutputBuffer::create(FilePath, Builder.getSize());
  if (!BufOrError)
    return createStringError(errc::invalid_argument,
                             toString(BufOrError.takeError()) +
                                 " when trying to open `" + FilePath +
                                 "` for writing");

  // Write binary to file.
  std::unique_ptr<FileOutputBuffer> FileBuf = std::move(*BufOrError);
  memcpy(FileBuf->getBufferStart(), Buf.data(), Buf.size());

  return FileBuf->commit();
}

Expected<std::unique_ptr<IFSStub>> readELFFile(MemoryBufferRef Buf) {
  Expected<std::unique_ptr<Binary>> BinOrErr = createBinary(Buf);
  if (!BinOrErr) {
    return BinOrErr.takeError();
  }

  Binary *Bin = BinOrErr->get();
  if (auto Obj = dyn_cast<ELFObjectFile<ELF32LE>>(Bin)) {
    return buildStub(*Obj);
  } else if (auto Obj = dyn_cast<ELFObjectFile<ELF64LE>>(Bin)) {
    return buildStub(*Obj);
  } else if (auto Obj = dyn_cast<ELFObjectFile<ELF32BE>>(Bin)) {
    return buildStub(*Obj);
  } else if (auto Obj = dyn_cast<ELFObjectFile<ELF64BE>>(Bin)) {
    return buildStub(*Obj);
  }
  return createStringError(errc::not_supported, "unsupported binary format");
}

// This function wraps the ELFT writeELFBinaryToFile() so writeBinaryStub()
// can be called without having to use ELFType templates directly.
Error writeBinaryStub(StringRef FilePath, const IFSStub &Stub,
                      bool WriteIfChanged) {
  assert(Stub.Target.Arch);
  assert(Stub.Target.BitWidth);
  assert(Stub.Target.Endianness);
  if (Stub.Target.BitWidth == IFSBitWidthType::IFS32) {
    if (Stub.Target.Endianness == IFSEndiannessType::Little) {
      return writeELFBinaryToFile<ELF32LE>(FilePath, Stub, WriteIfChanged);
    } else {
      return writeELFBinaryToFile<ELF32BE>(FilePath, Stub, WriteIfChanged);
    }
  } else {
    if (Stub.Target.Endianness == IFSEndiannessType::Little) {
      return writeELFBinaryToFile<ELF64LE>(FilePath, Stub, WriteIfChanged);
    } else {
      return writeELFBinaryToFile<ELF64BE>(FilePath, Stub, WriteIfChanged);
    }
  }
  llvm_unreachable("invalid binary output target");
}

} // end namespace ifs
} // end namespace llvm
