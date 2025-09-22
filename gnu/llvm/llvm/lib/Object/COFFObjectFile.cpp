//===- COFFObjectFile.cpp - COFF object file implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the COFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/WindowsMachineFlag.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <system_error>

using namespace llvm;
using namespace object;

using support::ulittle16_t;
using support::ulittle32_t;
using support::ulittle64_t;
using support::little16_t;

// Returns false if size is greater than the buffer size. And sets ec.
static bool checkSize(MemoryBufferRef M, std::error_code &EC, uint64_t Size) {
  if (M.getBufferSize() < Size) {
    EC = object_error::unexpected_eof;
    return false;
  }
  return true;
}

// Sets Obj unless any bytes in [addr, addr + size) fall outsize of m.
// Returns unexpected_eof if error.
template <typename T>
static Error getObject(const T *&Obj, MemoryBufferRef M, const void *Ptr,
                       const uint64_t Size = sizeof(T)) {
  uintptr_t Addr = reinterpret_cast<uintptr_t>(Ptr);
  if (Error E = Binary::checkOffset(M, Addr, Size))
    return E;
  Obj = reinterpret_cast<const T *>(Addr);
  return Error::success();
}

// Decode a string table entry in base 64 (//AAAAAA). Expects \arg Str without
// prefixed slashes.
static bool decodeBase64StringEntry(StringRef Str, uint32_t &Result) {
  assert(Str.size() <= 6 && "String too long, possible overflow.");
  if (Str.size() > 6)
    return true;

  uint64_t Value = 0;
  while (!Str.empty()) {
    unsigned CharVal;
    if (Str[0] >= 'A' && Str[0] <= 'Z') // 0..25
      CharVal = Str[0] - 'A';
    else if (Str[0] >= 'a' && Str[0] <= 'z') // 26..51
      CharVal = Str[0] - 'a' + 26;
    else if (Str[0] >= '0' && Str[0] <= '9') // 52..61
      CharVal = Str[0] - '0' + 52;
    else if (Str[0] == '+') // 62
      CharVal = 62;
    else if (Str[0] == '/') // 63
      CharVal = 63;
    else
      return true;

    Value = (Value * 64) + CharVal;
    Str = Str.substr(1);
  }

  if (Value > std::numeric_limits<uint32_t>::max())
    return true;

  Result = static_cast<uint32_t>(Value);
  return false;
}

template <typename coff_symbol_type>
const coff_symbol_type *COFFObjectFile::toSymb(DataRefImpl Ref) const {
  const coff_symbol_type *Addr =
      reinterpret_cast<const coff_symbol_type *>(Ref.p);

  assert(!checkOffset(Data, reinterpret_cast<uintptr_t>(Addr), sizeof(*Addr)));
#ifndef NDEBUG
  // Verify that the symbol points to a valid entry in the symbol table.
  uintptr_t Offset =
      reinterpret_cast<uintptr_t>(Addr) - reinterpret_cast<uintptr_t>(base());

  assert((Offset - getPointerToSymbolTable()) % sizeof(coff_symbol_type) == 0 &&
         "Symbol did not point to the beginning of a symbol");
#endif

  return Addr;
}

const coff_section *COFFObjectFile::toSec(DataRefImpl Ref) const {
  const coff_section *Addr = reinterpret_cast<const coff_section*>(Ref.p);

#ifndef NDEBUG
  // Verify that the section points to a valid entry in the section table.
  if (Addr < SectionTable || Addr >= (SectionTable + getNumberOfSections()))
    report_fatal_error("Section was outside of section table.");

  uintptr_t Offset = reinterpret_cast<uintptr_t>(Addr) -
                     reinterpret_cast<uintptr_t>(SectionTable);
  assert(Offset % sizeof(coff_section) == 0 &&
         "Section did not point to the beginning of a section");
#endif

  return Addr;
}

void COFFObjectFile::moveSymbolNext(DataRefImpl &Ref) const {
  auto End = reinterpret_cast<uintptr_t>(StringTable);
  if (SymbolTable16) {
    const coff_symbol16 *Symb = toSymb<coff_symbol16>(Ref);
    Symb += 1 + Symb->NumberOfAuxSymbols;
    Ref.p = std::min(reinterpret_cast<uintptr_t>(Symb), End);
  } else if (SymbolTable32) {
    const coff_symbol32 *Symb = toSymb<coff_symbol32>(Ref);
    Symb += 1 + Symb->NumberOfAuxSymbols;
    Ref.p = std::min(reinterpret_cast<uintptr_t>(Symb), End);
  } else {
    llvm_unreachable("no symbol table pointer!");
  }
}

Expected<StringRef> COFFObjectFile::getSymbolName(DataRefImpl Ref) const {
  return getSymbolName(getCOFFSymbol(Ref));
}

uint64_t COFFObjectFile::getSymbolValueImpl(DataRefImpl Ref) const {
  return getCOFFSymbol(Ref).getValue();
}

uint32_t COFFObjectFile::getSymbolAlignment(DataRefImpl Ref) const {
  // MSVC/link.exe seems to align symbols to the next-power-of-2
  // up to 32 bytes.
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  return std::min(uint64_t(32), PowerOf2Ceil(Symb.getValue()));
}

Expected<uint64_t> COFFObjectFile::getSymbolAddress(DataRefImpl Ref) const {
  uint64_t Result = cantFail(getSymbolValue(Ref));
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  int32_t SectionNumber = Symb.getSectionNumber();

  if (Symb.isAnyUndefined() || Symb.isCommon() ||
      COFF::isReservedSectionNumber(SectionNumber))
    return Result;

  Expected<const coff_section *> Section = getSection(SectionNumber);
  if (!Section)
    return Section.takeError();
  Result += (*Section)->VirtualAddress;

  // The section VirtualAddress does not include ImageBase, and we want to
  // return virtual addresses.
  Result += getImageBase();

  return Result;
}

Expected<SymbolRef::Type> COFFObjectFile::getSymbolType(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  int32_t SectionNumber = Symb.getSectionNumber();

  if (Symb.getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION)
    return SymbolRef::ST_Function;
  if (Symb.isAnyUndefined())
    return SymbolRef::ST_Unknown;
  if (Symb.isCommon())
    return SymbolRef::ST_Data;
  if (Symb.isFileRecord())
    return SymbolRef::ST_File;

  // TODO: perhaps we need a new symbol type ST_Section.
  if (SectionNumber == COFF::IMAGE_SYM_DEBUG || Symb.isSectionDefinition())
    return SymbolRef::ST_Debug;

  if (!COFF::isReservedSectionNumber(SectionNumber))
    return SymbolRef::ST_Data;

  return SymbolRef::ST_Other;
}

Expected<uint32_t> COFFObjectFile::getSymbolFlags(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  uint32_t Result = SymbolRef::SF_None;

  if (Symb.isExternal() || Symb.isWeakExternal())
    Result |= SymbolRef::SF_Global;

  if (const coff_aux_weak_external *AWE = Symb.getWeakExternal()) {
    Result |= SymbolRef::SF_Weak;
    if (AWE->Characteristics != COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS)
      Result |= SymbolRef::SF_Undefined;
  }

  if (Symb.getSectionNumber() == COFF::IMAGE_SYM_ABSOLUTE)
    Result |= SymbolRef::SF_Absolute;

  if (Symb.isFileRecord())
    Result |= SymbolRef::SF_FormatSpecific;

  if (Symb.isSectionDefinition())
    Result |= SymbolRef::SF_FormatSpecific;

  if (Symb.isCommon())
    Result |= SymbolRef::SF_Common;

  if (Symb.isUndefined())
    Result |= SymbolRef::SF_Undefined;

  return Result;
}

uint64_t COFFObjectFile::getCommonSymbolSizeImpl(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  return Symb.getValue();
}

Expected<section_iterator>
COFFObjectFile::getSymbolSection(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  if (COFF::isReservedSectionNumber(Symb.getSectionNumber()))
    return section_end();
  Expected<const coff_section *> Sec = getSection(Symb.getSectionNumber());
  if (!Sec)
    return Sec.takeError();
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(*Sec);
  return section_iterator(SectionRef(Ret, this));
}

unsigned COFFObjectFile::getSymbolSectionID(SymbolRef Sym) const {
  COFFSymbolRef Symb = getCOFFSymbol(Sym.getRawDataRefImpl());
  return Symb.getSectionNumber();
}

void COFFObjectFile::moveSectionNext(DataRefImpl &Ref) const {
  const coff_section *Sec = toSec(Ref);
  Sec += 1;
  Ref.p = reinterpret_cast<uintptr_t>(Sec);
}

Expected<StringRef> COFFObjectFile::getSectionName(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  return getSectionName(Sec);
}

uint64_t COFFObjectFile::getSectionAddress(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  uint64_t Result = Sec->VirtualAddress;

  // The section VirtualAddress does not include ImageBase, and we want to
  // return virtual addresses.
  Result += getImageBase();
  return Result;
}

uint64_t COFFObjectFile::getSectionIndex(DataRefImpl Sec) const {
  return toSec(Sec) - SectionTable;
}

uint64_t COFFObjectFile::getSectionSize(DataRefImpl Ref) const {
  return getSectionSize(toSec(Ref));
}

Expected<ArrayRef<uint8_t>>
COFFObjectFile::getSectionContents(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  ArrayRef<uint8_t> Res;
  if (Error E = getSectionContents(Sec, Res))
    return E;
  return Res;
}

uint64_t COFFObjectFile::getSectionAlignment(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  return Sec->getAlignment();
}

bool COFFObjectFile::isSectionCompressed(DataRefImpl Sec) const {
  return false;
}

bool COFFObjectFile::isSectionText(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  return Sec->Characteristics & COFF::IMAGE_SCN_CNT_CODE;
}

bool COFFObjectFile::isSectionData(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  return Sec->Characteristics & COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
}

bool COFFObjectFile::isSectionBSS(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  const uint32_t BssFlags = COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA |
                            COFF::IMAGE_SCN_MEM_READ |
                            COFF::IMAGE_SCN_MEM_WRITE;
  return (Sec->Characteristics & BssFlags) == BssFlags;
}

// The .debug sections are the only debug sections for COFF
// (\see MCObjectFileInfo.cpp).
bool COFFObjectFile::isDebugSection(DataRefImpl Ref) const {
  Expected<StringRef> SectionNameOrErr = getSectionName(Ref);
  if (!SectionNameOrErr) {
    // TODO: Report the error message properly.
    consumeError(SectionNameOrErr.takeError());
    return false;
  }
  StringRef SectionName = SectionNameOrErr.get();
  return SectionName.starts_with(".debug");
}

unsigned COFFObjectFile::getSectionID(SectionRef Sec) const {
  uintptr_t Offset =
      Sec.getRawDataRefImpl().p - reinterpret_cast<uintptr_t>(SectionTable);
  assert((Offset % sizeof(coff_section)) == 0);
  return (Offset / sizeof(coff_section)) + 1;
}

bool COFFObjectFile::isSectionVirtual(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  // In COFF, a virtual section won't have any in-file
  // content, so the file pointer to the content will be zero.
  return Sec->PointerToRawData == 0;
}

static uint32_t getNumberOfRelocations(const coff_section *Sec,
                                       MemoryBufferRef M, const uint8_t *base) {
  // The field for the number of relocations in COFF section table is only
  // 16-bit wide. If a section has more than 65535 relocations, 0xFFFF is set to
  // NumberOfRelocations field, and the actual relocation count is stored in the
  // VirtualAddress field in the first relocation entry.
  if (Sec->hasExtendedRelocations()) {
    const coff_relocation *FirstReloc;
    if (Error E = getObject(FirstReloc, M,
                            reinterpret_cast<const coff_relocation *>(
                                base + Sec->PointerToRelocations))) {
      consumeError(std::move(E));
      return 0;
    }
    // -1 to exclude this first relocation entry.
    return FirstReloc->VirtualAddress - 1;
  }
  return Sec->NumberOfRelocations;
}

static const coff_relocation *
getFirstReloc(const coff_section *Sec, MemoryBufferRef M, const uint8_t *Base) {
  uint64_t NumRelocs = getNumberOfRelocations(Sec, M, Base);
  if (!NumRelocs)
    return nullptr;
  auto begin = reinterpret_cast<const coff_relocation *>(
      Base + Sec->PointerToRelocations);
  if (Sec->hasExtendedRelocations()) {
    // Skip the first relocation entry repurposed to store the number of
    // relocations.
    begin++;
  }
  if (auto E = Binary::checkOffset(M, reinterpret_cast<uintptr_t>(begin),
                                   sizeof(coff_relocation) * NumRelocs)) {
    consumeError(std::move(E));
    return nullptr;
  }
  return begin;
}

relocation_iterator COFFObjectFile::section_rel_begin(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  const coff_relocation *begin = getFirstReloc(Sec, Data, base());
  if (begin && Sec->VirtualAddress != 0)
    report_fatal_error("Sections with relocations should have an address of 0");
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(begin);
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator COFFObjectFile::section_rel_end(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  const coff_relocation *I = getFirstReloc(Sec, Data, base());
  if (I)
    I += getNumberOfRelocations(Sec, Data, base());
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(I);
  return relocation_iterator(RelocationRef(Ret, this));
}

// Initialize the pointer to the symbol table.
Error COFFObjectFile::initSymbolTablePtr() {
  if (COFFHeader)
    if (Error E = getObject(
            SymbolTable16, Data, base() + getPointerToSymbolTable(),
            (uint64_t)getNumberOfSymbols() * getSymbolTableEntrySize()))
      return E;

  if (COFFBigObjHeader)
    if (Error E = getObject(
            SymbolTable32, Data, base() + getPointerToSymbolTable(),
            (uint64_t)getNumberOfSymbols() * getSymbolTableEntrySize()))
      return E;

  // Find string table. The first four byte of the string table contains the
  // total size of the string table, including the size field itself. If the
  // string table is empty, the value of the first four byte would be 4.
  uint32_t StringTableOffset = getPointerToSymbolTable() +
                               getNumberOfSymbols() * getSymbolTableEntrySize();
  const uint8_t *StringTableAddr = base() + StringTableOffset;
  const ulittle32_t *StringTableSizePtr;
  if (Error E = getObject(StringTableSizePtr, Data, StringTableAddr))
    return E;
  StringTableSize = *StringTableSizePtr;
  if (Error E = getObject(StringTable, Data, StringTableAddr, StringTableSize))
    return E;

  // Treat table sizes < 4 as empty because contrary to the PECOFF spec, some
  // tools like cvtres write a size of 0 for an empty table instead of 4.
  if (StringTableSize < 4)
    StringTableSize = 4;

  // Check that the string table is null terminated if has any in it.
  if (StringTableSize > 4 && StringTable[StringTableSize - 1] != 0)
    return createStringError(object_error::parse_failed,
                             "string table missing null terminator");
  return Error::success();
}

uint64_t COFFObjectFile::getImageBase() const {
  if (PE32Header)
    return PE32Header->ImageBase;
  else if (PE32PlusHeader)
    return PE32PlusHeader->ImageBase;
  // This actually comes up in practice.
  return 0;
}

// Returns the file offset for the given VA.
Error COFFObjectFile::getVaPtr(uint64_t Addr, uintptr_t &Res) const {
  uint64_t ImageBase = getImageBase();
  uint64_t Rva = Addr - ImageBase;
  assert(Rva <= UINT32_MAX);
  return getRvaPtr((uint32_t)Rva, Res);
}

// Returns the file offset for the given RVA.
Error COFFObjectFile::getRvaPtr(uint32_t Addr, uintptr_t &Res,
                                const char *ErrorContext) const {
  for (const SectionRef &S : sections()) {
    const coff_section *Section = getCOFFSection(S);
    uint32_t SectionStart = Section->VirtualAddress;
    uint32_t SectionEnd = Section->VirtualAddress + Section->VirtualSize;
    if (SectionStart <= Addr && Addr < SectionEnd) {
      // A table/directory entry can be pointing to somewhere in a stripped
      // section, in an object that went through `objcopy --only-keep-debug`.
      // In this case we don't want to cause the parsing of the object file to
      // fail, otherwise it will be impossible to use this object as debug info
      // in LLDB. Return SectionStrippedError here so that
      // COFFObjectFile::initialize can ignore the error.
      // Somewhat common binaries may have RVAs pointing outside of the
      // provided raw data. Instead of rejecting the binaries, just
      // treat the section as stripped for these purposes.
      if (Section->SizeOfRawData < Section->VirtualSize &&
          Addr >= SectionStart + Section->SizeOfRawData) {
        return make_error<SectionStrippedError>();
      }
      uint32_t Offset = Addr - SectionStart;
      Res = reinterpret_cast<uintptr_t>(base()) + Section->PointerToRawData +
            Offset;
      return Error::success();
    }
  }
  if (ErrorContext)
    return createStringError(object_error::parse_failed,
                             "RVA 0x%" PRIx32 " for %s not found", Addr,
                             ErrorContext);
  return createStringError(object_error::parse_failed,
                           "RVA 0x%" PRIx32 " not found", Addr);
}

Error COFFObjectFile::getRvaAndSizeAsBytes(uint32_t RVA, uint32_t Size,
                                           ArrayRef<uint8_t> &Contents,
                                           const char *ErrorContext) const {
  for (const SectionRef &S : sections()) {
    const coff_section *Section = getCOFFSection(S);
    uint32_t SectionStart = Section->VirtualAddress;
    // Check if this RVA is within the section bounds. Be careful about integer
    // overflow.
    uint32_t OffsetIntoSection = RVA - SectionStart;
    if (SectionStart <= RVA && OffsetIntoSection < Section->VirtualSize &&
        Size <= Section->VirtualSize - OffsetIntoSection) {
      uintptr_t Begin = reinterpret_cast<uintptr_t>(base()) +
                        Section->PointerToRawData + OffsetIntoSection;
      Contents =
          ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Begin), Size);
      return Error::success();
    }
  }
  if (ErrorContext)
    return createStringError(object_error::parse_failed,
                             "RVA 0x%" PRIx32 " for %s not found", RVA,
                             ErrorContext);
  return createStringError(object_error::parse_failed,
                           "RVA 0x%" PRIx32 " not found", RVA);
}

// Returns hint and name fields, assuming \p Rva is pointing to a Hint/Name
// table entry.
Error COFFObjectFile::getHintName(uint32_t Rva, uint16_t &Hint,
                                  StringRef &Name) const {
  uintptr_t IntPtr = 0;
  if (Error E = getRvaPtr(Rva, IntPtr))
    return E;
  const uint8_t *Ptr = reinterpret_cast<const uint8_t *>(IntPtr);
  Hint = *reinterpret_cast<const ulittle16_t *>(Ptr);
  Name = StringRef(reinterpret_cast<const char *>(Ptr + 2));
  return Error::success();
}

Error COFFObjectFile::getDebugPDBInfo(const debug_directory *DebugDir,
                                      const codeview::DebugInfo *&PDBInfo,
                                      StringRef &PDBFileName) const {
  ArrayRef<uint8_t> InfoBytes;
  if (Error E =
          getRvaAndSizeAsBytes(DebugDir->AddressOfRawData, DebugDir->SizeOfData,
                               InfoBytes, "PDB info"))
    return E;
  if (InfoBytes.size() < sizeof(*PDBInfo) + 1)
    return createStringError(object_error::parse_failed, "PDB info too small");
  PDBInfo = reinterpret_cast<const codeview::DebugInfo *>(InfoBytes.data());
  InfoBytes = InfoBytes.drop_front(sizeof(*PDBInfo));
  PDBFileName = StringRef(reinterpret_cast<const char *>(InfoBytes.data()),
                          InfoBytes.size());
  // Truncate the name at the first null byte. Ignore any padding.
  PDBFileName = PDBFileName.split('\0').first;
  return Error::success();
}

Error COFFObjectFile::getDebugPDBInfo(const codeview::DebugInfo *&PDBInfo,
                                      StringRef &PDBFileName) const {
  for (const debug_directory &D : debug_directories())
    if (D.Type == COFF::IMAGE_DEBUG_TYPE_CODEVIEW)
      return getDebugPDBInfo(&D, PDBInfo, PDBFileName);
  // If we get here, there is no PDB info to return.
  PDBInfo = nullptr;
  PDBFileName = StringRef();
  return Error::success();
}

// Find the import table.
Error COFFObjectFile::initImportTablePtr() {
  // First, we get the RVA of the import table. If the file lacks a pointer to
  // the import table, do nothing.
  const data_directory *DataEntry = getDataDirectory(COFF::IMPORT_TABLE);
  if (!DataEntry)
    return Error::success();

  // Do nothing if the pointer to import table is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return Error::success();

  uint32_t ImportTableRva = DataEntry->RelativeVirtualAddress;

  // Find the section that contains the RVA. This is needed because the RVA is
  // the import table's memory address which is different from its file offset.
  uintptr_t IntPtr = 0;
  if (Error E = getRvaPtr(ImportTableRva, IntPtr, "import table"))
    return E;
  if (Error E = checkOffset(Data, IntPtr, DataEntry->Size))
    return E;
  ImportDirectory = reinterpret_cast<
      const coff_import_directory_table_entry *>(IntPtr);
  return Error::success();
}

// Initializes DelayImportDirectory and NumberOfDelayImportDirectory.
Error COFFObjectFile::initDelayImportTablePtr() {
  const data_directory *DataEntry =
      getDataDirectory(COFF::DELAY_IMPORT_DESCRIPTOR);
  if (!DataEntry)
    return Error::success();
  if (DataEntry->RelativeVirtualAddress == 0)
    return Error::success();

  uint32_t RVA = DataEntry->RelativeVirtualAddress;
  NumberOfDelayImportDirectory = DataEntry->Size /
      sizeof(delay_import_directory_table_entry) - 1;

  uintptr_t IntPtr = 0;
  if (Error E = getRvaPtr(RVA, IntPtr, "delay import table"))
    return E;
  if (Error E = checkOffset(Data, IntPtr, DataEntry->Size))
    return E;

  DelayImportDirectory = reinterpret_cast<
      const delay_import_directory_table_entry *>(IntPtr);
  return Error::success();
}

// Find the export table.
Error COFFObjectFile::initExportTablePtr() {
  // First, we get the RVA of the export table. If the file lacks a pointer to
  // the export table, do nothing.
  const data_directory *DataEntry = getDataDirectory(COFF::EXPORT_TABLE);
  if (!DataEntry)
    return Error::success();

  // Do nothing if the pointer to export table is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return Error::success();

  uint32_t ExportTableRva = DataEntry->RelativeVirtualAddress;
  uintptr_t IntPtr = 0;
  if (Error E = getRvaPtr(ExportTableRva, IntPtr, "export table"))
    return E;
  if (Error E = checkOffset(Data, IntPtr, DataEntry->Size))
    return E;

  ExportDirectory =
      reinterpret_cast<const export_directory_table_entry *>(IntPtr);
  return Error::success();
}

Error COFFObjectFile::initBaseRelocPtr() {
  const data_directory *DataEntry =
      getDataDirectory(COFF::BASE_RELOCATION_TABLE);
  if (!DataEntry)
    return Error::success();
  if (DataEntry->RelativeVirtualAddress == 0)
    return Error::success();

  uintptr_t IntPtr = 0;
  if (Error E = getRvaPtr(DataEntry->RelativeVirtualAddress, IntPtr,
                          "base reloc table"))
    return E;
  if (Error E = checkOffset(Data, IntPtr, DataEntry->Size))
    return E;

  BaseRelocHeader = reinterpret_cast<const coff_base_reloc_block_header *>(
      IntPtr);
  BaseRelocEnd = reinterpret_cast<coff_base_reloc_block_header *>(
      IntPtr + DataEntry->Size);
  // FIXME: Verify the section containing BaseRelocHeader has at least
  // DataEntry->Size bytes after DataEntry->RelativeVirtualAddress.
  return Error::success();
}

Error COFFObjectFile::initDebugDirectoryPtr() {
  // Get the RVA of the debug directory. Do nothing if it does not exist.
  const data_directory *DataEntry = getDataDirectory(COFF::DEBUG_DIRECTORY);
  if (!DataEntry)
    return Error::success();

  // Do nothing if the RVA is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return Error::success();

  // Check that the size is a multiple of the entry size.
  if (DataEntry->Size % sizeof(debug_directory) != 0)
    return createStringError(object_error::parse_failed,
                             "debug directory has uneven size");

  uintptr_t IntPtr = 0;
  if (Error E = getRvaPtr(DataEntry->RelativeVirtualAddress, IntPtr,
                          "debug directory"))
    return E;
  if (Error E = checkOffset(Data, IntPtr, DataEntry->Size))
    return E;

  DebugDirectoryBegin = reinterpret_cast<const debug_directory *>(IntPtr);
  DebugDirectoryEnd = reinterpret_cast<const debug_directory *>(
      IntPtr + DataEntry->Size);
  // FIXME: Verify the section containing DebugDirectoryBegin has at least
  // DataEntry->Size bytes after DataEntry->RelativeVirtualAddress.
  return Error::success();
}

Error COFFObjectFile::initTLSDirectoryPtr() {
  // Get the RVA of the TLS directory. Do nothing if it does not exist.
  const data_directory *DataEntry = getDataDirectory(COFF::TLS_TABLE);
  if (!DataEntry)
    return Error::success();

  // Do nothing if the RVA is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return Error::success();

  uint64_t DirSize =
      is64() ? sizeof(coff_tls_directory64) : sizeof(coff_tls_directory32);

  // Check that the size is correct.
  if (DataEntry->Size != DirSize)
    return createStringError(
        object_error::parse_failed,
        "TLS Directory size (%u) is not the expected size (%" PRIu64 ").",
        static_cast<uint32_t>(DataEntry->Size), DirSize);

  uintptr_t IntPtr = 0;
  if (Error E =
          getRvaPtr(DataEntry->RelativeVirtualAddress, IntPtr, "TLS directory"))
    return E;
  if (Error E = checkOffset(Data, IntPtr, DataEntry->Size))
    return E;

  if (is64())
    TLSDirectory64 = reinterpret_cast<const coff_tls_directory64 *>(IntPtr);
  else
    TLSDirectory32 = reinterpret_cast<const coff_tls_directory32 *>(IntPtr);

  return Error::success();
}

Error COFFObjectFile::initLoadConfigPtr() {
  // Get the RVA of the debug directory. Do nothing if it does not exist.
  const data_directory *DataEntry = getDataDirectory(COFF::LOAD_CONFIG_TABLE);
  if (!DataEntry)
    return Error::success();

  // Do nothing if the RVA is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return Error::success();
  uintptr_t IntPtr = 0;
  if (Error E = getRvaPtr(DataEntry->RelativeVirtualAddress, IntPtr,
                          "load config table"))
    return E;
  if (Error E = checkOffset(Data, IntPtr, DataEntry->Size))
    return E;

  LoadConfig = (const void *)IntPtr;

  if (is64()) {
    auto Config = getLoadConfig64();
    if (Config->Size >=
            offsetof(coff_load_configuration64, CHPEMetadataPointer) +
                sizeof(Config->CHPEMetadataPointer) &&
        Config->CHPEMetadataPointer) {
      uint64_t ChpeOff = Config->CHPEMetadataPointer;
      if (Error E =
              getRvaPtr(ChpeOff - getImageBase(), IntPtr, "CHPE metadata"))
        return E;
      if (Error E = checkOffset(Data, IntPtr, sizeof(CHPEMetadata)))
        return E;

      CHPEMetadata = reinterpret_cast<const chpe_metadata *>(IntPtr);

      // Validate CHPE metadata
      if (CHPEMetadata->CodeMapCount) {
        if (Error E = getRvaPtr(CHPEMetadata->CodeMap, IntPtr, "CHPE code map"))
          return E;
        if (Error E = checkOffset(Data, IntPtr,
                                  CHPEMetadata->CodeMapCount *
                                      sizeof(chpe_range_entry)))
          return E;
      }

      if (CHPEMetadata->CodeRangesToEntryPointsCount) {
        if (Error E = getRvaPtr(CHPEMetadata->CodeRangesToEntryPoints, IntPtr,
                                "CHPE entry point ranges"))
          return E;
        if (Error E = checkOffset(Data, IntPtr,
                                  CHPEMetadata->CodeRangesToEntryPointsCount *
                                      sizeof(chpe_code_range_entry)))
          return E;
      }

      if (CHPEMetadata->RedirectionMetadataCount) {
        if (Error E = getRvaPtr(CHPEMetadata->RedirectionMetadata, IntPtr,
                                "CHPE redirection metadata"))
          return E;
        if (Error E = checkOffset(Data, IntPtr,
                                  CHPEMetadata->RedirectionMetadataCount *
                                      sizeof(chpe_redirection_entry)))
          return E;
      }
    }
  }

  return Error::success();
}

Expected<std::unique_ptr<COFFObjectFile>>
COFFObjectFile::create(MemoryBufferRef Object) {
  std::unique_ptr<COFFObjectFile> Obj(new COFFObjectFile(std::move(Object)));
  if (Error E = Obj->initialize())
    return E;
  return std::move(Obj);
}

COFFObjectFile::COFFObjectFile(MemoryBufferRef Object)
    : ObjectFile(Binary::ID_COFF, Object), COFFHeader(nullptr),
      COFFBigObjHeader(nullptr), PE32Header(nullptr), PE32PlusHeader(nullptr),
      DataDirectory(nullptr), SectionTable(nullptr), SymbolTable16(nullptr),
      SymbolTable32(nullptr), StringTable(nullptr), StringTableSize(0),
      ImportDirectory(nullptr), DelayImportDirectory(nullptr),
      NumberOfDelayImportDirectory(0), ExportDirectory(nullptr),
      BaseRelocHeader(nullptr), BaseRelocEnd(nullptr),
      DebugDirectoryBegin(nullptr), DebugDirectoryEnd(nullptr),
      TLSDirectory32(nullptr), TLSDirectory64(nullptr) {}

static Error ignoreStrippedErrors(Error E) {
  if (E.isA<SectionStrippedError>()) {
    consumeError(std::move(E));
    return Error::success();
  }
  return E;
}

Error COFFObjectFile::initialize() {
  // Check that we at least have enough room for a header.
  std::error_code EC;
  if (!checkSize(Data, EC, sizeof(coff_file_header)))
    return errorCodeToError(EC);

  // The current location in the file where we are looking at.
  uint64_t CurPtr = 0;

  // PE header is optional and is present only in executables. If it exists,
  // it is placed right after COFF header.
  bool HasPEHeader = false;

  // Check if this is a PE/COFF file.
  if (checkSize(Data, EC, sizeof(dos_header) + sizeof(COFF::PEMagic))) {
    // PE/COFF, seek through MS-DOS compatibility stub and 4-byte
    // PE signature to find 'normal' COFF header.
    const auto *DH = reinterpret_cast<const dos_header *>(base());
    if (DH->Magic[0] == 'M' && DH->Magic[1] == 'Z') {
      CurPtr = DH->AddressOfNewExeHeader;
      // Check the PE magic bytes. ("PE\0\0")
      if (memcmp(base() + CurPtr, COFF::PEMagic, sizeof(COFF::PEMagic)) != 0) {
        return createStringError(object_error::parse_failed,
                                 "incorrect PE magic");
      }
      CurPtr += sizeof(COFF::PEMagic); // Skip the PE magic bytes.
      HasPEHeader = true;
    }
  }

  if (Error E = getObject(COFFHeader, Data, base() + CurPtr))
    return E;

  // It might be a bigobj file, let's check.  Note that COFF bigobj and COFF
  // import libraries share a common prefix but bigobj is more restrictive.
  if (!HasPEHeader && COFFHeader->Machine == COFF::IMAGE_FILE_MACHINE_UNKNOWN &&
      COFFHeader->NumberOfSections == uint16_t(0xffff) &&
      checkSize(Data, EC, sizeof(coff_bigobj_file_header))) {
    if (Error E = getObject(COFFBigObjHeader, Data, base() + CurPtr))
      return E;

    // Verify that we are dealing with bigobj.
    if (COFFBigObjHeader->Version >= COFF::BigObjHeader::MinBigObjectVersion &&
        std::memcmp(COFFBigObjHeader->UUID, COFF::BigObjMagic,
                    sizeof(COFF::BigObjMagic)) == 0) {
      COFFHeader = nullptr;
      CurPtr += sizeof(coff_bigobj_file_header);
    } else {
      // It's not a bigobj.
      COFFBigObjHeader = nullptr;
    }
  }
  if (COFFHeader) {
    // The prior checkSize call may have failed.  This isn't a hard error
    // because we were just trying to sniff out bigobj.
    EC = std::error_code();
    CurPtr += sizeof(coff_file_header);

    if (COFFHeader->isImportLibrary())
      return errorCodeToError(EC);
  }

  if (HasPEHeader) {
    const pe32_header *Header;
    if (Error E = getObject(Header, Data, base() + CurPtr))
      return E;

    const uint8_t *DataDirAddr;
    uint64_t DataDirSize;
    if (Header->Magic == COFF::PE32Header::PE32) {
      PE32Header = Header;
      DataDirAddr = base() + CurPtr + sizeof(pe32_header);
      DataDirSize = sizeof(data_directory) * PE32Header->NumberOfRvaAndSize;
    } else if (Header->Magic == COFF::PE32Header::PE32_PLUS) {
      PE32PlusHeader = reinterpret_cast<const pe32plus_header *>(Header);
      DataDirAddr = base() + CurPtr + sizeof(pe32plus_header);
      DataDirSize = sizeof(data_directory) * PE32PlusHeader->NumberOfRvaAndSize;
    } else {
      // It's neither PE32 nor PE32+.
      return createStringError(object_error::parse_failed,
                               "incorrect PE magic");
    }
    if (Error E = getObject(DataDirectory, Data, DataDirAddr, DataDirSize))
      return E;
  }

  if (COFFHeader)
    CurPtr += COFFHeader->SizeOfOptionalHeader;

  assert(COFFHeader || COFFBigObjHeader);

  if (Error E =
          getObject(SectionTable, Data, base() + CurPtr,
                    (uint64_t)getNumberOfSections() * sizeof(coff_section)))
    return E;

  // Initialize the pointer to the symbol table.
  if (getPointerToSymbolTable() != 0) {
    if (Error E = initSymbolTablePtr()) {
      // Recover from errors reading the symbol table.
      consumeError(std::move(E));
      SymbolTable16 = nullptr;
      SymbolTable32 = nullptr;
      StringTable = nullptr;
      StringTableSize = 0;
    }
  } else {
    // We had better not have any symbols if we don't have a symbol table.
    if (getNumberOfSymbols() != 0) {
      return createStringError(object_error::parse_failed,
                               "symbol table missing");
    }
  }

  // Initialize the pointer to the beginning of the import table.
  if (Error E = ignoreStrippedErrors(initImportTablePtr()))
    return E;
  if (Error E = ignoreStrippedErrors(initDelayImportTablePtr()))
    return E;

  // Initialize the pointer to the export table.
  if (Error E = ignoreStrippedErrors(initExportTablePtr()))
    return E;

  // Initialize the pointer to the base relocation table.
  if (Error E = ignoreStrippedErrors(initBaseRelocPtr()))
    return E;

  // Initialize the pointer to the debug directory.
  if (Error E = ignoreStrippedErrors(initDebugDirectoryPtr()))
    return E;

  // Initialize the pointer to the TLS directory.
  if (Error E = ignoreStrippedErrors(initTLSDirectoryPtr()))
    return E;

  if (Error E = ignoreStrippedErrors(initLoadConfigPtr()))
    return E;

  return Error::success();
}

basic_symbol_iterator COFFObjectFile::symbol_begin() const {
  DataRefImpl Ret;
  Ret.p = getSymbolTable();
  return basic_symbol_iterator(SymbolRef(Ret, this));
}

basic_symbol_iterator COFFObjectFile::symbol_end() const {
  // The symbol table ends where the string table begins.
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(StringTable);
  return basic_symbol_iterator(SymbolRef(Ret, this));
}

import_directory_iterator COFFObjectFile::import_directory_begin() const {
  if (!ImportDirectory)
    return import_directory_end();
  if (ImportDirectory->isNull())
    return import_directory_end();
  return import_directory_iterator(
      ImportDirectoryEntryRef(ImportDirectory, 0, this));
}

import_directory_iterator COFFObjectFile::import_directory_end() const {
  return import_directory_iterator(
      ImportDirectoryEntryRef(nullptr, -1, this));
}

delay_import_directory_iterator
COFFObjectFile::delay_import_directory_begin() const {
  return delay_import_directory_iterator(
      DelayImportDirectoryEntryRef(DelayImportDirectory, 0, this));
}

delay_import_directory_iterator
COFFObjectFile::delay_import_directory_end() const {
  return delay_import_directory_iterator(
      DelayImportDirectoryEntryRef(
          DelayImportDirectory, NumberOfDelayImportDirectory, this));
}

export_directory_iterator COFFObjectFile::export_directory_begin() const {
  return export_directory_iterator(
      ExportDirectoryEntryRef(ExportDirectory, 0, this));
}

export_directory_iterator COFFObjectFile::export_directory_end() const {
  if (!ExportDirectory)
    return export_directory_iterator(ExportDirectoryEntryRef(nullptr, 0, this));
  ExportDirectoryEntryRef Ref(ExportDirectory,
                              ExportDirectory->AddressTableEntries, this);
  return export_directory_iterator(Ref);
}

section_iterator COFFObjectFile::section_begin() const {
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(SectionTable);
  return section_iterator(SectionRef(Ret, this));
}

section_iterator COFFObjectFile::section_end() const {
  DataRefImpl Ret;
  int NumSections =
      COFFHeader && COFFHeader->isImportLibrary() ? 0 : getNumberOfSections();
  Ret.p = reinterpret_cast<uintptr_t>(SectionTable + NumSections);
  return section_iterator(SectionRef(Ret, this));
}

base_reloc_iterator COFFObjectFile::base_reloc_begin() const {
  return base_reloc_iterator(BaseRelocRef(BaseRelocHeader, this));
}

base_reloc_iterator COFFObjectFile::base_reloc_end() const {
  return base_reloc_iterator(BaseRelocRef(BaseRelocEnd, this));
}

uint8_t COFFObjectFile::getBytesInAddress() const {
  return getArch() == Triple::x86_64 || getArch() == Triple::aarch64 ? 8 : 4;
}

StringRef COFFObjectFile::getFileFormatName() const {
  switch(getMachine()) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return "COFF-i386";
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return "COFF-x86-64";
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return "COFF-ARM";
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return "COFF-ARM64";
  case COFF::IMAGE_FILE_MACHINE_ARM64EC:
    return "COFF-ARM64EC";
  case COFF::IMAGE_FILE_MACHINE_ARM64X:
    return "COFF-ARM64X";
  default:
    return "COFF-<unknown arch>";
  }
}

Triple::ArchType COFFObjectFile::getArch() const {
  return getMachineArchType(getMachine());
}

Expected<uint64_t> COFFObjectFile::getStartAddress() const {
  if (PE32Header)
    return PE32Header->AddressOfEntryPoint;
  return 0;
}

iterator_range<import_directory_iterator>
COFFObjectFile::import_directories() const {
  return make_range(import_directory_begin(), import_directory_end());
}

iterator_range<delay_import_directory_iterator>
COFFObjectFile::delay_import_directories() const {
  return make_range(delay_import_directory_begin(),
                    delay_import_directory_end());
}

iterator_range<export_directory_iterator>
COFFObjectFile::export_directories() const {
  return make_range(export_directory_begin(), export_directory_end());
}

iterator_range<base_reloc_iterator> COFFObjectFile::base_relocs() const {
  return make_range(base_reloc_begin(), base_reloc_end());
}

const data_directory *COFFObjectFile::getDataDirectory(uint32_t Index) const {
  if (!DataDirectory)
    return nullptr;
  assert(PE32Header || PE32PlusHeader);
  uint32_t NumEnt = PE32Header ? PE32Header->NumberOfRvaAndSize
                               : PE32PlusHeader->NumberOfRvaAndSize;
  if (Index >= NumEnt)
    return nullptr;
  return &DataDirectory[Index];
}

Expected<const coff_section *> COFFObjectFile::getSection(int32_t Index) const {
  // Perhaps getting the section of a reserved section index should be an error,
  // but callers rely on this to return null.
  if (COFF::isReservedSectionNumber(Index))
    return (const coff_section *)nullptr;
  if (static_cast<uint32_t>(Index) <= getNumberOfSections()) {
    // We already verified the section table data, so no need to check again.
    return SectionTable + (Index - 1);
  }
  return createStringError(object_error::parse_failed,
                           "section index out of bounds");
}

Expected<StringRef> COFFObjectFile::getString(uint32_t Offset) const {
  if (StringTableSize <= 4)
    // Tried to get a string from an empty string table.
    return createStringError(object_error::parse_failed, "string table empty");
  if (Offset >= StringTableSize)
    return errorCodeToError(object_error::unexpected_eof);
  return StringRef(StringTable + Offset);
}

Expected<StringRef> COFFObjectFile::getSymbolName(COFFSymbolRef Symbol) const {
  return getSymbolName(Symbol.getGeneric());
}

Expected<StringRef>
COFFObjectFile::getSymbolName(const coff_symbol_generic *Symbol) const {
  // Check for string table entry. First 4 bytes are 0.
  if (Symbol->Name.Offset.Zeroes == 0)
    return getString(Symbol->Name.Offset.Offset);

  // Null terminated, let ::strlen figure out the length.
  if (Symbol->Name.ShortName[COFF::NameSize - 1] == 0)
    return StringRef(Symbol->Name.ShortName);

  // Not null terminated, use all 8 bytes.
  return StringRef(Symbol->Name.ShortName, COFF::NameSize);
}

ArrayRef<uint8_t>
COFFObjectFile::getSymbolAuxData(COFFSymbolRef Symbol) const {
  const uint8_t *Aux = nullptr;

  size_t SymbolSize = getSymbolTableEntrySize();
  if (Symbol.getNumberOfAuxSymbols() > 0) {
    // AUX data comes immediately after the symbol in COFF
    Aux = reinterpret_cast<const uint8_t *>(Symbol.getRawPtr()) + SymbolSize;
#ifndef NDEBUG
    // Verify that the Aux symbol points to a valid entry in the symbol table.
    uintptr_t Offset = uintptr_t(Aux) - uintptr_t(base());
    if (Offset < getPointerToSymbolTable() ||
        Offset >=
            getPointerToSymbolTable() + (getNumberOfSymbols() * SymbolSize))
      report_fatal_error("Aux Symbol data was outside of symbol table.");

    assert((Offset - getPointerToSymbolTable()) % SymbolSize == 0 &&
           "Aux Symbol data did not point to the beginning of a symbol");
#endif
  }
  return ArrayRef(Aux, Symbol.getNumberOfAuxSymbols() * SymbolSize);
}

uint32_t COFFObjectFile::getSymbolIndex(COFFSymbolRef Symbol) const {
  uintptr_t Offset =
      reinterpret_cast<uintptr_t>(Symbol.getRawPtr()) - getSymbolTable();
  assert(Offset % getSymbolTableEntrySize() == 0 &&
         "Symbol did not point to the beginning of a symbol");
  size_t Index = Offset / getSymbolTableEntrySize();
  assert(Index < getNumberOfSymbols());
  return Index;
}

Expected<StringRef>
COFFObjectFile::getSectionName(const coff_section *Sec) const {
  StringRef Name = StringRef(Sec->Name, COFF::NameSize).split('\0').first;

  // Check for string table entry. First byte is '/'.
  if (Name.starts_with("/")) {
    uint32_t Offset;
    if (Name.starts_with("//")) {
      if (decodeBase64StringEntry(Name.substr(2), Offset))
        return createStringError(object_error::parse_failed,
                                 "invalid section name");
    } else {
      if (Name.substr(1).getAsInteger(10, Offset))
        return createStringError(object_error::parse_failed,
                                 "invalid section name");
    }
    return getString(Offset);
  }

  return Name;
}

uint64_t COFFObjectFile::getSectionSize(const coff_section *Sec) const {
  // SizeOfRawData and VirtualSize change what they represent depending on
  // whether or not we have an executable image.
  //
  // For object files, SizeOfRawData contains the size of section's data;
  // VirtualSize should be zero but isn't due to buggy COFF writers.
  //
  // For executables, SizeOfRawData *must* be a multiple of FileAlignment; the
  // actual section size is in VirtualSize.  It is possible for VirtualSize to
  // be greater than SizeOfRawData; the contents past that point should be
  // considered to be zero.
  if (getDOSHeader())
    return std::min(Sec->VirtualSize, Sec->SizeOfRawData);
  return Sec->SizeOfRawData;
}

Error COFFObjectFile::getSectionContents(const coff_section *Sec,
                                         ArrayRef<uint8_t> &Res) const {
  // In COFF, a virtual section won't have any in-file
  // content, so the file pointer to the content will be zero.
  if (Sec->PointerToRawData == 0)
    return Error::success();
  // The only thing that we need to verify is that the contents is contained
  // within the file bounds. We don't need to make sure it doesn't cover other
  // data, as there's nothing that says that is not allowed.
  uintptr_t ConStart =
      reinterpret_cast<uintptr_t>(base()) + Sec->PointerToRawData;
  uint32_t SectionSize = getSectionSize(Sec);
  if (Error E = checkOffset(Data, ConStart, SectionSize))
    return E;
  Res = ArrayRef(reinterpret_cast<const uint8_t *>(ConStart), SectionSize);
  return Error::success();
}

const coff_relocation *COFFObjectFile::toRel(DataRefImpl Rel) const {
  return reinterpret_cast<const coff_relocation*>(Rel.p);
}

void COFFObjectFile::moveRelocationNext(DataRefImpl &Rel) const {
  Rel.p = reinterpret_cast<uintptr_t>(
            reinterpret_cast<const coff_relocation*>(Rel.p) + 1);
}

uint64_t COFFObjectFile::getRelocationOffset(DataRefImpl Rel) const {
  const coff_relocation *R = toRel(Rel);
  return R->VirtualAddress;
}

symbol_iterator COFFObjectFile::getRelocationSymbol(DataRefImpl Rel) const {
  const coff_relocation *R = toRel(Rel);
  DataRefImpl Ref;
  if (R->SymbolTableIndex >= getNumberOfSymbols())
    return symbol_end();
  if (SymbolTable16)
    Ref.p = reinterpret_cast<uintptr_t>(SymbolTable16 + R->SymbolTableIndex);
  else if (SymbolTable32)
    Ref.p = reinterpret_cast<uintptr_t>(SymbolTable32 + R->SymbolTableIndex);
  else
    llvm_unreachable("no symbol table pointer!");
  return symbol_iterator(SymbolRef(Ref, this));
}

uint64_t COFFObjectFile::getRelocationType(DataRefImpl Rel) const {
  const coff_relocation* R = toRel(Rel);
  return R->Type;
}

const coff_section *
COFFObjectFile::getCOFFSection(const SectionRef &Section) const {
  return toSec(Section.getRawDataRefImpl());
}

COFFSymbolRef COFFObjectFile::getCOFFSymbol(const DataRefImpl &Ref) const {
  if (SymbolTable16)
    return toSymb<coff_symbol16>(Ref);
  if (SymbolTable32)
    return toSymb<coff_symbol32>(Ref);
  llvm_unreachable("no symbol table pointer!");
}

COFFSymbolRef COFFObjectFile::getCOFFSymbol(const SymbolRef &Symbol) const {
  return getCOFFSymbol(Symbol.getRawDataRefImpl());
}

const coff_relocation *
COFFObjectFile::getCOFFRelocation(const RelocationRef &Reloc) const {
  return toRel(Reloc.getRawDataRefImpl());
}

ArrayRef<coff_relocation>
COFFObjectFile::getRelocations(const coff_section *Sec) const {
  return {getFirstReloc(Sec, Data, base()),
          getNumberOfRelocations(Sec, Data, base())};
}

#define LLVM_COFF_SWITCH_RELOC_TYPE_NAME(reloc_type)                           \
  case COFF::reloc_type:                                                       \
    return #reloc_type;

StringRef COFFObjectFile::getRelocationTypeName(uint16_t Type) const {
  switch (getArch()) {
  case Triple::x86_64:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ADDR64);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ADDR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ADDR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_1);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_2);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_3);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_4);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_5);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SECREL7);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SREL32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_PAIR);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SSPAN32);
    default:
      return "Unknown";
    }
    break;
  case Triple::thumb:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_ADDR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_ADDR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH24);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH11);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BLX24);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BLX11);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_REL32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_MOV32A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_MOV32T);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH20T);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH24T);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BLX23T);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_PAIR);
    default:
      return "Unknown";
    }
    break;
  case Triple::aarch64:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ADDR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ADDR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_BRANCH26);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_PAGEBASE_REL21);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_REL21);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_PAGEOFFSET_12A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_PAGEOFFSET_12L);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL_LOW12A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL_HIGH12A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL_LOW12L);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ADDR64);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_BRANCH19);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_BRANCH14);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_REL32);
    default:
      return "Unknown";
    }
    break;
  case Triple::x86:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_DIR16);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_REL16);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_DIR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_DIR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SEG12);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SECREL7);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_REL32);
    default:
      return "Unknown";
    }
    break;
  default:
    return "Unknown";
  }
}

#undef LLVM_COFF_SWITCH_RELOC_TYPE_NAME

void COFFObjectFile::getRelocationTypeName(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  const coff_relocation *Reloc = toRel(Rel);
  StringRef Res = getRelocationTypeName(Reloc->Type);
  Result.append(Res.begin(), Res.end());
}

bool COFFObjectFile::isRelocatableObject() const {
  return !DataDirectory;
}

StringRef COFFObjectFile::mapDebugSectionName(StringRef Name) const {
  return StringSwitch<StringRef>(Name)
      .Case("eh_fram", "eh_frame")
      .Default(Name);
}

bool ImportDirectoryEntryRef::
operator==(const ImportDirectoryEntryRef &Other) const {
  return ImportTable == Other.ImportTable && Index == Other.Index;
}

void ImportDirectoryEntryRef::moveNext() {
  ++Index;
  if (ImportTable[Index].isNull()) {
    Index = -1;
    ImportTable = nullptr;
  }
}

Error ImportDirectoryEntryRef::getImportTableEntry(
    const coff_import_directory_table_entry *&Result) const {
  return getObject(Result, OwningObject->Data, ImportTable + Index);
}

static imported_symbol_iterator
makeImportedSymbolIterator(const COFFObjectFile *Object,
                           uintptr_t Ptr, int Index) {
  if (Object->getBytesInAddress() == 4) {
    auto *P = reinterpret_cast<const import_lookup_table_entry32 *>(Ptr);
    return imported_symbol_iterator(ImportedSymbolRef(P, Index, Object));
  }
  auto *P = reinterpret_cast<const import_lookup_table_entry64 *>(Ptr);
  return imported_symbol_iterator(ImportedSymbolRef(P, Index, Object));
}

static imported_symbol_iterator
importedSymbolBegin(uint32_t RVA, const COFFObjectFile *Object) {
  uintptr_t IntPtr = 0;
  // FIXME: Handle errors.
  cantFail(Object->getRvaPtr(RVA, IntPtr));
  return makeImportedSymbolIterator(Object, IntPtr, 0);
}

static imported_symbol_iterator
importedSymbolEnd(uint32_t RVA, const COFFObjectFile *Object) {
  uintptr_t IntPtr = 0;
  // FIXME: Handle errors.
  cantFail(Object->getRvaPtr(RVA, IntPtr));
  // Forward the pointer to the last entry which is null.
  int Index = 0;
  if (Object->getBytesInAddress() == 4) {
    auto *Entry = reinterpret_cast<ulittle32_t *>(IntPtr);
    while (*Entry++)
      ++Index;
  } else {
    auto *Entry = reinterpret_cast<ulittle64_t *>(IntPtr);
    while (*Entry++)
      ++Index;
  }
  return makeImportedSymbolIterator(Object, IntPtr, Index);
}

imported_symbol_iterator
ImportDirectoryEntryRef::imported_symbol_begin() const {
  return importedSymbolBegin(ImportTable[Index].ImportAddressTableRVA,
                             OwningObject);
}

imported_symbol_iterator
ImportDirectoryEntryRef::imported_symbol_end() const {
  return importedSymbolEnd(ImportTable[Index].ImportAddressTableRVA,
                           OwningObject);
}

iterator_range<imported_symbol_iterator>
ImportDirectoryEntryRef::imported_symbols() const {
  return make_range(imported_symbol_begin(), imported_symbol_end());
}

imported_symbol_iterator ImportDirectoryEntryRef::lookup_table_begin() const {
  return importedSymbolBegin(ImportTable[Index].ImportLookupTableRVA,
                             OwningObject);
}

imported_symbol_iterator ImportDirectoryEntryRef::lookup_table_end() const {
  return importedSymbolEnd(ImportTable[Index].ImportLookupTableRVA,
                           OwningObject);
}

iterator_range<imported_symbol_iterator>
ImportDirectoryEntryRef::lookup_table_symbols() const {
  return make_range(lookup_table_begin(), lookup_table_end());
}

Error ImportDirectoryEntryRef::getName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (Error E = OwningObject->getRvaPtr(ImportTable[Index].NameRVA, IntPtr,
                                        "import directory name"))
    return E;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return Error::success();
}

Error
ImportDirectoryEntryRef::getImportLookupTableRVA(uint32_t  &Result) const {
  Result = ImportTable[Index].ImportLookupTableRVA;
  return Error::success();
}

Error ImportDirectoryEntryRef::getImportAddressTableRVA(
    uint32_t &Result) const {
  Result = ImportTable[Index].ImportAddressTableRVA;
  return Error::success();
}

bool DelayImportDirectoryEntryRef::
operator==(const DelayImportDirectoryEntryRef &Other) const {
  return Table == Other.Table && Index == Other.Index;
}

void DelayImportDirectoryEntryRef::moveNext() {
  ++Index;
}

imported_symbol_iterator
DelayImportDirectoryEntryRef::imported_symbol_begin() const {
  return importedSymbolBegin(Table[Index].DelayImportNameTable,
                             OwningObject);
}

imported_symbol_iterator
DelayImportDirectoryEntryRef::imported_symbol_end() const {
  return importedSymbolEnd(Table[Index].DelayImportNameTable,
                           OwningObject);
}

iterator_range<imported_symbol_iterator>
DelayImportDirectoryEntryRef::imported_symbols() const {
  return make_range(imported_symbol_begin(), imported_symbol_end());
}

Error DelayImportDirectoryEntryRef::getName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (Error E = OwningObject->getRvaPtr(Table[Index].Name, IntPtr,
                                        "delay import directory name"))
    return E;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return Error::success();
}

Error DelayImportDirectoryEntryRef::getDelayImportTable(
    const delay_import_directory_table_entry *&Result) const {
  Result = &Table[Index];
  return Error::success();
}

Error DelayImportDirectoryEntryRef::getImportAddress(int AddrIndex,
                                                     uint64_t &Result) const {
  uint32_t RVA = Table[Index].DelayImportAddressTable +
      AddrIndex * (OwningObject->is64() ? 8 : 4);
  uintptr_t IntPtr = 0;
  if (Error E = OwningObject->getRvaPtr(RVA, IntPtr, "import address"))
    return E;
  if (OwningObject->is64())
    Result = *reinterpret_cast<const ulittle64_t *>(IntPtr);
  else
    Result = *reinterpret_cast<const ulittle32_t *>(IntPtr);
  return Error::success();
}

bool ExportDirectoryEntryRef::
operator==(const ExportDirectoryEntryRef &Other) const {
  return ExportTable == Other.ExportTable && Index == Other.Index;
}

void ExportDirectoryEntryRef::moveNext() {
  ++Index;
}

// Returns the name of the current export symbol. If the symbol is exported only
// by ordinal, the empty string is set as a result.
Error ExportDirectoryEntryRef::getDllName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (Error E =
          OwningObject->getRvaPtr(ExportTable->NameRVA, IntPtr, "dll name"))
    return E;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return Error::success();
}

// Returns the starting ordinal number.
Error ExportDirectoryEntryRef::getOrdinalBase(uint32_t &Result) const {
  Result = ExportTable->OrdinalBase;
  return Error::success();
}

// Returns the export ordinal of the current export symbol.
Error ExportDirectoryEntryRef::getOrdinal(uint32_t &Result) const {
  Result = ExportTable->OrdinalBase + Index;
  return Error::success();
}

// Returns the address of the current export symbol.
Error ExportDirectoryEntryRef::getExportRVA(uint32_t &Result) const {
  uintptr_t IntPtr = 0;
  if (Error EC = OwningObject->getRvaPtr(ExportTable->ExportAddressTableRVA,
                                         IntPtr, "export address"))
    return EC;
  const export_address_table_entry *entry =
      reinterpret_cast<const export_address_table_entry *>(IntPtr);
  Result = entry[Index].ExportRVA;
  return Error::success();
}

// Returns the name of the current export symbol. If the symbol is exported only
// by ordinal, the empty string is set as a result.
Error
ExportDirectoryEntryRef::getSymbolName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (Error EC = OwningObject->getRvaPtr(ExportTable->OrdinalTableRVA, IntPtr,
                                         "export ordinal table"))
    return EC;
  const ulittle16_t *Start = reinterpret_cast<const ulittle16_t *>(IntPtr);

  uint32_t NumEntries = ExportTable->NumberOfNamePointers;
  int Offset = 0;
  for (const ulittle16_t *I = Start, *E = Start + NumEntries;
       I < E; ++I, ++Offset) {
    if (*I != Index)
      continue;
    if (Error EC = OwningObject->getRvaPtr(ExportTable->NamePointerRVA, IntPtr,
                                           "export table entry"))
      return EC;
    const ulittle32_t *NamePtr = reinterpret_cast<const ulittle32_t *>(IntPtr);
    if (Error EC = OwningObject->getRvaPtr(NamePtr[Offset], IntPtr,
                                           "export symbol name"))
      return EC;
    Result = StringRef(reinterpret_cast<const char *>(IntPtr));
    return Error::success();
  }
  Result = "";
  return Error::success();
}

Error ExportDirectoryEntryRef::isForwarder(bool &Result) const {
  const data_directory *DataEntry =
      OwningObject->getDataDirectory(COFF::EXPORT_TABLE);
  if (!DataEntry)
    return createStringError(object_error::parse_failed,
                             "export table missing");
  uint32_t RVA;
  if (auto EC = getExportRVA(RVA))
    return EC;
  uint32_t Begin = DataEntry->RelativeVirtualAddress;
  uint32_t End = DataEntry->RelativeVirtualAddress + DataEntry->Size;
  Result = (Begin <= RVA && RVA < End);
  return Error::success();
}

Error ExportDirectoryEntryRef::getForwardTo(StringRef &Result) const {
  uint32_t RVA;
  if (auto EC = getExportRVA(RVA))
    return EC;
  uintptr_t IntPtr = 0;
  if (auto EC = OwningObject->getRvaPtr(RVA, IntPtr, "export forward target"))
    return EC;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return Error::success();
}

bool ImportedSymbolRef::
operator==(const ImportedSymbolRef &Other) const {
  return Entry32 == Other.Entry32 && Entry64 == Other.Entry64
      && Index == Other.Index;
}

void ImportedSymbolRef::moveNext() {
  ++Index;
}

Error ImportedSymbolRef::getSymbolName(StringRef &Result) const {
  uint32_t RVA;
  if (Entry32) {
    // If a symbol is imported only by ordinal, it has no name.
    if (Entry32[Index].isOrdinal())
      return Error::success();
    RVA = Entry32[Index].getHintNameRVA();
  } else {
    if (Entry64[Index].isOrdinal())
      return Error::success();
    RVA = Entry64[Index].getHintNameRVA();
  }
  uintptr_t IntPtr = 0;
  if (Error EC = OwningObject->getRvaPtr(RVA, IntPtr, "import symbol name"))
    return EC;
  // +2 because the first two bytes is hint.
  Result = StringRef(reinterpret_cast<const char *>(IntPtr + 2));
  return Error::success();
}

Error ImportedSymbolRef::isOrdinal(bool &Result) const {
  if (Entry32)
    Result = Entry32[Index].isOrdinal();
  else
    Result = Entry64[Index].isOrdinal();
  return Error::success();
}

Error ImportedSymbolRef::getHintNameRVA(uint32_t &Result) const {
  if (Entry32)
    Result = Entry32[Index].getHintNameRVA();
  else
    Result = Entry64[Index].getHintNameRVA();
  return Error::success();
}

Error ImportedSymbolRef::getOrdinal(uint16_t &Result) const {
  uint32_t RVA;
  if (Entry32) {
    if (Entry32[Index].isOrdinal()) {
      Result = Entry32[Index].getOrdinal();
      return Error::success();
    }
    RVA = Entry32[Index].getHintNameRVA();
  } else {
    if (Entry64[Index].isOrdinal()) {
      Result = Entry64[Index].getOrdinal();
      return Error::success();
    }
    RVA = Entry64[Index].getHintNameRVA();
  }
  uintptr_t IntPtr = 0;
  if (Error EC = OwningObject->getRvaPtr(RVA, IntPtr, "import symbol ordinal"))
    return EC;
  Result = *reinterpret_cast<const ulittle16_t *>(IntPtr);
  return Error::success();
}

Expected<std::unique_ptr<COFFObjectFile>>
ObjectFile::createCOFFObjectFile(MemoryBufferRef Object) {
  return COFFObjectFile::create(Object);
}

bool BaseRelocRef::operator==(const BaseRelocRef &Other) const {
  return Header == Other.Header && Index == Other.Index;
}

void BaseRelocRef::moveNext() {
  // Header->BlockSize is the size of the current block, including the
  // size of the header itself.
  uint32_t Size = sizeof(*Header) +
      sizeof(coff_base_reloc_block_entry) * (Index + 1);
  if (Size == Header->BlockSize) {
    // .reloc contains a list of base relocation blocks. Each block
    // consists of the header followed by entries. The header contains
    // how many entories will follow. When we reach the end of the
    // current block, proceed to the next block.
    Header = reinterpret_cast<const coff_base_reloc_block_header *>(
        reinterpret_cast<const uint8_t *>(Header) + Size);
    Index = 0;
  } else {
    ++Index;
  }
}

Error BaseRelocRef::getType(uint8_t &Type) const {
  auto *Entry = reinterpret_cast<const coff_base_reloc_block_entry *>(Header + 1);
  Type = Entry[Index].getType();
  return Error::success();
}

Error BaseRelocRef::getRVA(uint32_t &Result) const {
  auto *Entry = reinterpret_cast<const coff_base_reloc_block_entry *>(Header + 1);
  Result = Header->PageRVA + Entry[Index].getOffset();
  return Error::success();
}

#define RETURN_IF_ERROR(Expr)                                                  \
  do {                                                                         \
    Error E = (Expr);                                                          \
    if (E)                                                                     \
      return std::move(E);                                                     \
  } while (0)

Expected<ArrayRef<UTF16>>
ResourceSectionRef::getDirStringAtOffset(uint32_t Offset) {
  BinaryStreamReader Reader = BinaryStreamReader(BBS);
  Reader.setOffset(Offset);
  uint16_t Length;
  RETURN_IF_ERROR(Reader.readInteger(Length));
  ArrayRef<UTF16> RawDirString;
  RETURN_IF_ERROR(Reader.readArray(RawDirString, Length));
  return RawDirString;
}

Expected<ArrayRef<UTF16>>
ResourceSectionRef::getEntryNameString(const coff_resource_dir_entry &Entry) {
  return getDirStringAtOffset(Entry.Identifier.getNameOffset());
}

Expected<const coff_resource_dir_table &>
ResourceSectionRef::getTableAtOffset(uint32_t Offset) {
  const coff_resource_dir_table *Table = nullptr;

  BinaryStreamReader Reader(BBS);
  Reader.setOffset(Offset);
  RETURN_IF_ERROR(Reader.readObject(Table));
  assert(Table != nullptr);
  return *Table;
}

Expected<const coff_resource_dir_entry &>
ResourceSectionRef::getTableEntryAtOffset(uint32_t Offset) {
  const coff_resource_dir_entry *Entry = nullptr;

  BinaryStreamReader Reader(BBS);
  Reader.setOffset(Offset);
  RETURN_IF_ERROR(Reader.readObject(Entry));
  assert(Entry != nullptr);
  return *Entry;
}

Expected<const coff_resource_data_entry &>
ResourceSectionRef::getDataEntryAtOffset(uint32_t Offset) {
  const coff_resource_data_entry *Entry = nullptr;

  BinaryStreamReader Reader(BBS);
  Reader.setOffset(Offset);
  RETURN_IF_ERROR(Reader.readObject(Entry));
  assert(Entry != nullptr);
  return *Entry;
}

Expected<const coff_resource_dir_table &>
ResourceSectionRef::getEntrySubDir(const coff_resource_dir_entry &Entry) {
  assert(Entry.Offset.isSubDir());
  return getTableAtOffset(Entry.Offset.value());
}

Expected<const coff_resource_data_entry &>
ResourceSectionRef::getEntryData(const coff_resource_dir_entry &Entry) {
  assert(!Entry.Offset.isSubDir());
  return getDataEntryAtOffset(Entry.Offset.value());
}

Expected<const coff_resource_dir_table &> ResourceSectionRef::getBaseTable() {
  return getTableAtOffset(0);
}

Expected<const coff_resource_dir_entry &>
ResourceSectionRef::getTableEntry(const coff_resource_dir_table &Table,
                                  uint32_t Index) {
  if (Index >= (uint32_t)(Table.NumberOfNameEntries + Table.NumberOfIDEntries))
    return createStringError(object_error::parse_failed, "index out of range");
  const uint8_t *TablePtr = reinterpret_cast<const uint8_t *>(&Table);
  ptrdiff_t TableOffset = TablePtr - BBS.data().data();
  return getTableEntryAtOffset(TableOffset + sizeof(Table) +
                               Index * sizeof(coff_resource_dir_entry));
}

Error ResourceSectionRef::load(const COFFObjectFile *O) {
  for (const SectionRef &S : O->sections()) {
    Expected<StringRef> Name = S.getName();
    if (!Name)
      return Name.takeError();

    if (*Name == ".rsrc" || *Name == ".rsrc$01")
      return load(O, S);
  }
  return createStringError(object_error::parse_failed,
                           "no resource section found");
}

Error ResourceSectionRef::load(const COFFObjectFile *O, const SectionRef &S) {
  Obj = O;
  Section = S;
  Expected<StringRef> Contents = Section.getContents();
  if (!Contents)
    return Contents.takeError();
  BBS = BinaryByteStream(*Contents, llvm::endianness::little);
  const coff_section *COFFSect = Obj->getCOFFSection(Section);
  ArrayRef<coff_relocation> OrigRelocs = Obj->getRelocations(COFFSect);
  Relocs.reserve(OrigRelocs.size());
  for (const coff_relocation &R : OrigRelocs)
    Relocs.push_back(&R);
  llvm::sort(Relocs, [](const coff_relocation *A, const coff_relocation *B) {
    return A->VirtualAddress < B->VirtualAddress;
  });
  return Error::success();
}

Expected<StringRef>
ResourceSectionRef::getContents(const coff_resource_data_entry &Entry) {
  if (!Obj)
    return createStringError(object_error::parse_failed, "no object provided");

  // Find a potential relocation at the DataRVA field (first member of
  // the coff_resource_data_entry struct).
  const uint8_t *EntryPtr = reinterpret_cast<const uint8_t *>(&Entry);
  ptrdiff_t EntryOffset = EntryPtr - BBS.data().data();
  coff_relocation RelocTarget{ulittle32_t(EntryOffset), ulittle32_t(0),
                              ulittle16_t(0)};
  auto RelocsForOffset =
      std::equal_range(Relocs.begin(), Relocs.end(), &RelocTarget,
                       [](const coff_relocation *A, const coff_relocation *B) {
                         return A->VirtualAddress < B->VirtualAddress;
                       });

  if (RelocsForOffset.first != RelocsForOffset.second) {
    // We found a relocation with the right offset. Check that it does have
    // the expected type.
    const coff_relocation &R = **RelocsForOffset.first;
    uint16_t RVAReloc;
    switch (Obj->getArch()) {
    case Triple::x86:
      RVAReloc = COFF::IMAGE_REL_I386_DIR32NB;
      break;
    case Triple::x86_64:
      RVAReloc = COFF::IMAGE_REL_AMD64_ADDR32NB;
      break;
    case Triple::thumb:
      RVAReloc = COFF::IMAGE_REL_ARM_ADDR32NB;
      break;
    case Triple::aarch64:
      RVAReloc = COFF::IMAGE_REL_ARM64_ADDR32NB;
      break;
    default:
      return createStringError(object_error::parse_failed,
                               "unsupported architecture");
    }
    if (R.Type != RVAReloc)
      return createStringError(object_error::parse_failed,
                               "unexpected relocation type");
    // Get the relocation's symbol
    Expected<COFFSymbolRef> Sym = Obj->getSymbol(R.SymbolTableIndex);
    if (!Sym)
      return Sym.takeError();
    // And the symbol's section
    Expected<const coff_section *> Section =
        Obj->getSection(Sym->getSectionNumber());
    if (!Section)
      return Section.takeError();
    // Add the initial value of DataRVA to the symbol's offset to find the
    // data it points at.
    uint64_t Offset = Entry.DataRVA + Sym->getValue();
    ArrayRef<uint8_t> Contents;
    if (Error E = Obj->getSectionContents(*Section, Contents))
      return E;
    if (Offset + Entry.DataSize > Contents.size())
      return createStringError(object_error::parse_failed,
                               "data outside of section");
    // Return a reference to the data inside the section.
    return StringRef(reinterpret_cast<const char *>(Contents.data()) + Offset,
                     Entry.DataSize);
  } else {
    // Relocatable objects need a relocation for the DataRVA field.
    if (Obj->isRelocatableObject())
      return createStringError(object_error::parse_failed,
                               "no relocation found for DataRVA");

    // Locate the section that contains the address that DataRVA points at.
    uint64_t VA = Entry.DataRVA + Obj->getImageBase();
    for (const SectionRef &S : Obj->sections()) {
      if (VA >= S.getAddress() &&
          VA + Entry.DataSize <= S.getAddress() + S.getSize()) {
        uint64_t Offset = VA - S.getAddress();
        Expected<StringRef> Contents = S.getContents();
        if (!Contents)
          return Contents.takeError();
        return Contents->slice(Offset, Offset + Entry.DataSize);
      }
    }
    return createStringError(object_error::parse_failed,
                             "address not found in image");
  }
}
