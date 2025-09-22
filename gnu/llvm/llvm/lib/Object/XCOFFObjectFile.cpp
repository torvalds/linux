//===--- XCOFFObjectFile.cpp - XCOFF object file implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the XCOFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/XCOFFObjectFile.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <cstddef>
#include <cstring>

namespace llvm {

using namespace XCOFF;

namespace object {

static const uint8_t FunctionSym = 0x20;
static const uint16_t NoRelMask = 0x0001;
static const size_t SymbolAuxTypeOffset = 17;

// Checks that [Ptr, Ptr + Size) bytes fall inside the memory buffer
// 'M'. Returns a pointer to the underlying object on success.
template <typename T>
static Expected<const T *> getObject(MemoryBufferRef M, const void *Ptr,
                                     const uint64_t Size = sizeof(T)) {
  uintptr_t Addr = reinterpret_cast<uintptr_t>(Ptr);
  if (Error E = Binary::checkOffset(M, Addr, Size))
    return std::move(E);
  return reinterpret_cast<const T *>(Addr);
}

static uintptr_t getWithOffset(uintptr_t Base, ptrdiff_t Offset) {
  return reinterpret_cast<uintptr_t>(reinterpret_cast<const char *>(Base) +
                                     Offset);
}

template <typename T> static const T *viewAs(uintptr_t in) {
  return reinterpret_cast<const T *>(in);
}

static StringRef generateXCOFFFixedNameStringRef(const char *Name) {
  auto NulCharPtr =
      static_cast<const char *>(memchr(Name, '\0', XCOFF::NameSize));
  return NulCharPtr ? StringRef(Name, NulCharPtr - Name)
                    : StringRef(Name, XCOFF::NameSize);
}

template <typename T> StringRef XCOFFSectionHeader<T>::getName() const {
  const T &DerivedXCOFFSectionHeader = static_cast<const T &>(*this);
  return generateXCOFFFixedNameStringRef(DerivedXCOFFSectionHeader.Name);
}

template <typename T> uint16_t XCOFFSectionHeader<T>::getSectionType() const {
  const T &DerivedXCOFFSectionHeader = static_cast<const T &>(*this);
  return DerivedXCOFFSectionHeader.Flags & SectionFlagsTypeMask;
}

template <typename T>
uint32_t XCOFFSectionHeader<T>::getSectionSubtype() const {
  const T &DerivedXCOFFSectionHeader = static_cast<const T &>(*this);
  return DerivedXCOFFSectionHeader.Flags & ~SectionFlagsTypeMask;
}

template <typename T>
bool XCOFFSectionHeader<T>::isReservedSectionType() const {
  return getSectionType() & SectionFlagsReservedMask;
}

template <typename AddressType>
bool XCOFFRelocation<AddressType>::isRelocationSigned() const {
  return Info & XR_SIGN_INDICATOR_MASK;
}

template <typename AddressType>
bool XCOFFRelocation<AddressType>::isFixupIndicated() const {
  return Info & XR_FIXUP_INDICATOR_MASK;
}

template <typename AddressType>
uint8_t XCOFFRelocation<AddressType>::getRelocatedLength() const {
  // The relocation encodes the bit length being relocated minus 1. Add back
  // the 1 to get the actual length being relocated.
  return (Info & XR_BIASED_LENGTH_MASK) + 1;
}

template struct ExceptionSectionEntry<support::ubig32_t>;
template struct ExceptionSectionEntry<support::ubig64_t>;

template <typename T>
Expected<StringRef> getLoaderSecSymNameInStrTbl(const T *LoaderSecHeader,
                                                uint64_t Offset) {
  if (LoaderSecHeader->LengthOfStrTbl > Offset)
    return (reinterpret_cast<const char *>(LoaderSecHeader) +
            LoaderSecHeader->OffsetToStrTbl + Offset);

  return createError("entry with offset 0x" + Twine::utohexstr(Offset) +
                     " in the loader section's string table with size 0x" +
                     Twine::utohexstr(LoaderSecHeader->LengthOfStrTbl) +
                     " is invalid");
}

Expected<StringRef> LoaderSectionSymbolEntry32::getSymbolName(
    const LoaderSectionHeader32 *LoaderSecHeader32) const {
  const NameOffsetInStrTbl *NameInStrTbl =
      reinterpret_cast<const NameOffsetInStrTbl *>(SymbolName);
  if (NameInStrTbl->IsNameInStrTbl != XCOFFSymbolRef::NAME_IN_STR_TBL_MAGIC)
    return generateXCOFFFixedNameStringRef(SymbolName);

  return getLoaderSecSymNameInStrTbl(LoaderSecHeader32, NameInStrTbl->Offset);
}

Expected<StringRef> LoaderSectionSymbolEntry64::getSymbolName(
    const LoaderSectionHeader64 *LoaderSecHeader64) const {
  return getLoaderSecSymNameInStrTbl(LoaderSecHeader64, Offset);
}

uintptr_t
XCOFFObjectFile::getAdvancedSymbolEntryAddress(uintptr_t CurrentAddress,
                                               uint32_t Distance) {
  return getWithOffset(CurrentAddress, Distance * XCOFF::SymbolTableEntrySize);
}

const XCOFF::SymbolAuxType *
XCOFFObjectFile::getSymbolAuxType(uintptr_t AuxEntryAddress) const {
  assert(is64Bit() && "64-bit interface called on a 32-bit object file.");
  return viewAs<XCOFF::SymbolAuxType>(
      getWithOffset(AuxEntryAddress, SymbolAuxTypeOffset));
}

void XCOFFObjectFile::checkSectionAddress(uintptr_t Addr,
                                          uintptr_t TableAddress) const {
  if (Addr < TableAddress)
    report_fatal_error("Section header outside of section header table.");

  uintptr_t Offset = Addr - TableAddress;
  if (Offset >= getSectionHeaderSize() * getNumberOfSections())
    report_fatal_error("Section header outside of section header table.");

  if (Offset % getSectionHeaderSize() != 0)
    report_fatal_error(
        "Section header pointer does not point to a valid section header.");
}

const XCOFFSectionHeader32 *
XCOFFObjectFile::toSection32(DataRefImpl Ref) const {
  assert(!is64Bit() && "32-bit interface called on 64-bit object file.");
#ifndef NDEBUG
  checkSectionAddress(Ref.p, getSectionHeaderTableAddress());
#endif
  return viewAs<XCOFFSectionHeader32>(Ref.p);
}

const XCOFFSectionHeader64 *
XCOFFObjectFile::toSection64(DataRefImpl Ref) const {
  assert(is64Bit() && "64-bit interface called on a 32-bit object file.");
#ifndef NDEBUG
  checkSectionAddress(Ref.p, getSectionHeaderTableAddress());
#endif
  return viewAs<XCOFFSectionHeader64>(Ref.p);
}

XCOFFSymbolRef XCOFFObjectFile::toSymbolRef(DataRefImpl Ref) const {
  assert(Ref.p != 0 && "Symbol table pointer can not be nullptr!");
#ifndef NDEBUG
  checkSymbolEntryPointer(Ref.p);
#endif
  return XCOFFSymbolRef(Ref, this);
}

const XCOFFFileHeader32 *XCOFFObjectFile::fileHeader32() const {
  assert(!is64Bit() && "32-bit interface called on 64-bit object file.");
  return static_cast<const XCOFFFileHeader32 *>(FileHeader);
}

const XCOFFFileHeader64 *XCOFFObjectFile::fileHeader64() const {
  assert(is64Bit() && "64-bit interface called on a 32-bit object file.");
  return static_cast<const XCOFFFileHeader64 *>(FileHeader);
}

const XCOFFAuxiliaryHeader32 *XCOFFObjectFile::auxiliaryHeader32() const {
  assert(!is64Bit() && "32-bit interface called on 64-bit object file.");
  return static_cast<const XCOFFAuxiliaryHeader32 *>(AuxiliaryHeader);
}

const XCOFFAuxiliaryHeader64 *XCOFFObjectFile::auxiliaryHeader64() const {
  assert(is64Bit() && "64-bit interface called on a 32-bit object file.");
  return static_cast<const XCOFFAuxiliaryHeader64 *>(AuxiliaryHeader);
}

template <typename T> const T *XCOFFObjectFile::sectionHeaderTable() const {
  return static_cast<const T *>(SectionHeaderTable);
}

const XCOFFSectionHeader32 *
XCOFFObjectFile::sectionHeaderTable32() const {
  assert(!is64Bit() && "32-bit interface called on 64-bit object file.");
  return static_cast<const XCOFFSectionHeader32 *>(SectionHeaderTable);
}

const XCOFFSectionHeader64 *
XCOFFObjectFile::sectionHeaderTable64() const {
  assert(is64Bit() && "64-bit interface called on a 32-bit object file.");
  return static_cast<const XCOFFSectionHeader64 *>(SectionHeaderTable);
}

void XCOFFObjectFile::moveSymbolNext(DataRefImpl &Symb) const {
  uintptr_t NextSymbolAddr = getAdvancedSymbolEntryAddress(
      Symb.p, toSymbolRef(Symb).getNumberOfAuxEntries() + 1);
#ifndef NDEBUG
  // This function is used by basic_symbol_iterator, which allows to
  // point to the end-of-symbol-table address.
  if (NextSymbolAddr != getEndOfSymbolTableAddress())
    checkSymbolEntryPointer(NextSymbolAddr);
#endif
  Symb.p = NextSymbolAddr;
}

Expected<StringRef>
XCOFFObjectFile::getStringTableEntry(uint32_t Offset) const {
  // The byte offset is relative to the start of the string table.
  // A byte offset value of 0 is a null or zero-length symbol
  // name. A byte offset in the range 1 to 3 (inclusive) points into the length
  // field; as a soft-error recovery mechanism, we treat such cases as having an
  // offset of 0.
  if (Offset < 4)
    return StringRef(nullptr, 0);

  if (StringTable.Data != nullptr && StringTable.Size > Offset)
    return (StringTable.Data + Offset);

  return createError("entry with offset 0x" + Twine::utohexstr(Offset) +
                     " in a string table with size 0x" +
                     Twine::utohexstr(StringTable.Size) + " is invalid");
}

StringRef XCOFFObjectFile::getStringTable() const {
  // If the size is less than or equal to 4, then the string table contains no
  // string data.
  return StringRef(StringTable.Data,
                   StringTable.Size <= 4 ? 0 : StringTable.Size);
}

Expected<StringRef>
XCOFFObjectFile::getCFileName(const XCOFFFileAuxEnt *CFileEntPtr) const {
  if (CFileEntPtr->NameInStrTbl.Magic != XCOFFSymbolRef::NAME_IN_STR_TBL_MAGIC)
    return generateXCOFFFixedNameStringRef(CFileEntPtr->Name);
  return getStringTableEntry(CFileEntPtr->NameInStrTbl.Offset);
}

Expected<StringRef> XCOFFObjectFile::getSymbolName(DataRefImpl Symb) const {
  return toSymbolRef(Symb).getName();
}

Expected<uint64_t> XCOFFObjectFile::getSymbolAddress(DataRefImpl Symb) const {
  return toSymbolRef(Symb).getValue();
}

uint64_t XCOFFObjectFile::getSymbolValueImpl(DataRefImpl Symb) const {
  return toSymbolRef(Symb).getValue();
}

uint32_t XCOFFObjectFile::getSymbolAlignment(DataRefImpl Symb) const {
  uint64_t Result = 0;
  XCOFFSymbolRef XCOFFSym = toSymbolRef(Symb);
  if (XCOFFSym.isCsectSymbol()) {
    Expected<XCOFFCsectAuxRef> CsectAuxRefOrError =
        XCOFFSym.getXCOFFCsectAuxRef();
    if (!CsectAuxRefOrError)
      // TODO: report the error up the stack.
      consumeError(CsectAuxRefOrError.takeError());
    else
      Result = 1ULL << CsectAuxRefOrError.get().getAlignmentLog2();
  }
  return Result;
}

uint64_t XCOFFObjectFile::getCommonSymbolSizeImpl(DataRefImpl Symb) const {
  uint64_t Result = 0;
  XCOFFSymbolRef XCOFFSym = toSymbolRef(Symb);
  if (XCOFFSym.isCsectSymbol()) {
    Expected<XCOFFCsectAuxRef> CsectAuxRefOrError =
        XCOFFSym.getXCOFFCsectAuxRef();
    if (!CsectAuxRefOrError)
      // TODO: report the error up the stack.
      consumeError(CsectAuxRefOrError.takeError());
    else {
      XCOFFCsectAuxRef CsectAuxRef = CsectAuxRefOrError.get();
      assert(CsectAuxRef.getSymbolType() == XCOFF::XTY_CM);
      Result = CsectAuxRef.getSectionOrLength();
    }
  }
  return Result;
}

Expected<SymbolRef::Type>
XCOFFObjectFile::getSymbolType(DataRefImpl Symb) const {
  XCOFFSymbolRef XCOFFSym = toSymbolRef(Symb);

  Expected<bool> IsFunction = XCOFFSym.isFunction();
  if (!IsFunction)
    return IsFunction.takeError();

  if (*IsFunction)
    return SymbolRef::ST_Function;

  if (XCOFF::C_FILE == XCOFFSym.getStorageClass())
    return SymbolRef::ST_File;

  int16_t SecNum = XCOFFSym.getSectionNumber();
  if (SecNum <= 0)
    return SymbolRef::ST_Other;

  Expected<DataRefImpl> SecDRIOrErr =
      getSectionByNum(XCOFFSym.getSectionNumber());

  if (!SecDRIOrErr)
    return SecDRIOrErr.takeError();

  DataRefImpl SecDRI = SecDRIOrErr.get();

  Expected<StringRef> SymNameOrError = XCOFFSym.getName();
  if (SymNameOrError) {
    // The "TOC" symbol is treated as SymbolRef::ST_Other.
    if (SymNameOrError.get() == "TOC")
      return SymbolRef::ST_Other;

    // The symbol for a section name is treated as SymbolRef::ST_Other.
    StringRef SecName;
    if (is64Bit())
      SecName = XCOFFObjectFile::toSection64(SecDRIOrErr.get())->getName();
    else
      SecName = XCOFFObjectFile::toSection32(SecDRIOrErr.get())->getName();

    if (SecName == SymNameOrError.get())
      return SymbolRef::ST_Other;
  } else
    return SymNameOrError.takeError();

  if (isSectionData(SecDRI) || isSectionBSS(SecDRI))
    return SymbolRef::ST_Data;

  if (isDebugSection(SecDRI))
    return SymbolRef::ST_Debug;

  return SymbolRef::ST_Other;
}

Expected<section_iterator>
XCOFFObjectFile::getSymbolSection(DataRefImpl Symb) const {
  const int16_t SectNum = toSymbolRef(Symb).getSectionNumber();

  if (isReservedSectionNumber(SectNum))
    return section_end();

  Expected<DataRefImpl> ExpSec = getSectionByNum(SectNum);
  if (!ExpSec)
    return ExpSec.takeError();

  return section_iterator(SectionRef(ExpSec.get(), this));
}

void XCOFFObjectFile::moveSectionNext(DataRefImpl &Sec) const {
  const char *Ptr = reinterpret_cast<const char *>(Sec.p);
  Sec.p = reinterpret_cast<uintptr_t>(Ptr + getSectionHeaderSize());
}

Expected<StringRef> XCOFFObjectFile::getSectionName(DataRefImpl Sec) const {
  return generateXCOFFFixedNameStringRef(getSectionNameInternal(Sec));
}

uint64_t XCOFFObjectFile::getSectionAddress(DataRefImpl Sec) const {
  // Avoid ternary due to failure to convert the ubig32_t value to a unit64_t
  // with MSVC.
  if (is64Bit())
    return toSection64(Sec)->VirtualAddress;

  return toSection32(Sec)->VirtualAddress;
}

uint64_t XCOFFObjectFile::getSectionIndex(DataRefImpl Sec) const {
  // Section numbers in XCOFF are numbered beginning at 1. A section number of
  // zero is used to indicate that a symbol is being imported or is undefined.
  if (is64Bit())
    return toSection64(Sec) - sectionHeaderTable64() + 1;
  else
    return toSection32(Sec) - sectionHeaderTable32() + 1;
}

uint64_t XCOFFObjectFile::getSectionSize(DataRefImpl Sec) const {
  // Avoid ternary due to failure to convert the ubig32_t value to a unit64_t
  // with MSVC.
  if (is64Bit())
    return toSection64(Sec)->SectionSize;

  return toSection32(Sec)->SectionSize;
}

Expected<ArrayRef<uint8_t>>
XCOFFObjectFile::getSectionContents(DataRefImpl Sec) const {
  if (isSectionVirtual(Sec))
    return ArrayRef<uint8_t>();

  uint64_t OffsetToRaw;
  if (is64Bit())
    OffsetToRaw = toSection64(Sec)->FileOffsetToRawData;
  else
    OffsetToRaw = toSection32(Sec)->FileOffsetToRawData;

  const uint8_t * ContentStart = base() + OffsetToRaw;
  uint64_t SectionSize = getSectionSize(Sec);
  if (Error E = Binary::checkOffset(
          Data, reinterpret_cast<uintptr_t>(ContentStart), SectionSize))
    return createError(
        toString(std::move(E)) + ": section data with offset 0x" +
        Twine::utohexstr(OffsetToRaw) + " and size 0x" +
        Twine::utohexstr(SectionSize) + " goes past the end of the file");

  return ArrayRef(ContentStart, SectionSize);
}

uint64_t XCOFFObjectFile::getSectionAlignment(DataRefImpl Sec) const {
  uint64_t Result = 0;
  llvm_unreachable("Not yet implemented!");
  return Result;
}

uint64_t XCOFFObjectFile::getSectionFileOffsetToRawData(DataRefImpl Sec) const {
  if (is64Bit())
    return toSection64(Sec)->FileOffsetToRawData;

  return toSection32(Sec)->FileOffsetToRawData;
}

Expected<uintptr_t> XCOFFObjectFile::getSectionFileOffsetToRawData(
    XCOFF::SectionTypeFlags SectType) const {
  DataRefImpl DRI = getSectionByType(SectType);

  if (DRI.p == 0) // No section is not an error.
    return 0;

  uint64_t SectionOffset = getSectionFileOffsetToRawData(DRI);
  uint64_t SizeOfSection = getSectionSize(DRI);

  uintptr_t SectionStart = reinterpret_cast<uintptr_t>(base() + SectionOffset);
  if (Error E = Binary::checkOffset(Data, SectionStart, SizeOfSection)) {
    SmallString<32> UnknownType;
    Twine(("<Unknown:") + Twine::utohexstr(SectType) + ">")
        .toVector(UnknownType);
    const char *SectionName = UnknownType.c_str();

    switch (SectType) {
#define ECASE(Value, String)                                                   \
  case XCOFF::Value:                                                           \
    SectionName = String;                                                      \
    break

      ECASE(STYP_PAD, "pad");
      ECASE(STYP_DWARF, "dwarf");
      ECASE(STYP_TEXT, "text");
      ECASE(STYP_DATA, "data");
      ECASE(STYP_BSS, "bss");
      ECASE(STYP_EXCEPT, "expect");
      ECASE(STYP_INFO, "info");
      ECASE(STYP_TDATA, "tdata");
      ECASE(STYP_TBSS, "tbss");
      ECASE(STYP_LOADER, "loader");
      ECASE(STYP_DEBUG, "debug");
      ECASE(STYP_TYPCHK, "typchk");
      ECASE(STYP_OVRFLO, "ovrflo");
#undef ECASE
    }
    return createError(toString(std::move(E)) + ": " + SectionName +
                       " section with offset 0x" +
                       Twine::utohexstr(SectionOffset) + " and size 0x" +
                       Twine::utohexstr(SizeOfSection) +
                       " goes past the end of the file");
  }
  return SectionStart;
}

bool XCOFFObjectFile::isSectionCompressed(DataRefImpl Sec) const {
  return false;
}

bool XCOFFObjectFile::isSectionText(DataRefImpl Sec) const {
  return getSectionFlags(Sec) & XCOFF::STYP_TEXT;
}

bool XCOFFObjectFile::isSectionData(DataRefImpl Sec) const {
  uint32_t Flags = getSectionFlags(Sec);
  return Flags & (XCOFF::STYP_DATA | XCOFF::STYP_TDATA);
}

bool XCOFFObjectFile::isSectionBSS(DataRefImpl Sec) const {
  uint32_t Flags = getSectionFlags(Sec);
  return Flags & (XCOFF::STYP_BSS | XCOFF::STYP_TBSS);
}

bool XCOFFObjectFile::isDebugSection(DataRefImpl Sec) const {
  uint32_t Flags = getSectionFlags(Sec);
  return Flags & (XCOFF::STYP_DEBUG | XCOFF::STYP_DWARF);
}

bool XCOFFObjectFile::isSectionVirtual(DataRefImpl Sec) const {
  return is64Bit() ? toSection64(Sec)->FileOffsetToRawData == 0
                   : toSection32(Sec)->FileOffsetToRawData == 0;
}

relocation_iterator XCOFFObjectFile::section_rel_begin(DataRefImpl Sec) const {
  DataRefImpl Ret;
  if (is64Bit()) {
    const XCOFFSectionHeader64 *SectionEntPtr = toSection64(Sec);
    auto RelocationsOrErr =
        relocations<XCOFFSectionHeader64, XCOFFRelocation64>(*SectionEntPtr);
    if (Error E = RelocationsOrErr.takeError()) {
      // TODO: report the error up the stack.
      consumeError(std::move(E));
      return relocation_iterator(RelocationRef());
    }
    Ret.p = reinterpret_cast<uintptr_t>(&*RelocationsOrErr.get().begin());
  } else {
    const XCOFFSectionHeader32 *SectionEntPtr = toSection32(Sec);
    auto RelocationsOrErr =
        relocations<XCOFFSectionHeader32, XCOFFRelocation32>(*SectionEntPtr);
    if (Error E = RelocationsOrErr.takeError()) {
      // TODO: report the error up the stack.
      consumeError(std::move(E));
      return relocation_iterator(RelocationRef());
    }
    Ret.p = reinterpret_cast<uintptr_t>(&*RelocationsOrErr.get().begin());
  }
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator XCOFFObjectFile::section_rel_end(DataRefImpl Sec) const {
  DataRefImpl Ret;
  if (is64Bit()) {
    const XCOFFSectionHeader64 *SectionEntPtr = toSection64(Sec);
    auto RelocationsOrErr =
        relocations<XCOFFSectionHeader64, XCOFFRelocation64>(*SectionEntPtr);
    if (Error E = RelocationsOrErr.takeError()) {
      // TODO: report the error up the stack.
      consumeError(std::move(E));
      return relocation_iterator(RelocationRef());
    }
    Ret.p = reinterpret_cast<uintptr_t>(&*RelocationsOrErr.get().end());
  } else {
    const XCOFFSectionHeader32 *SectionEntPtr = toSection32(Sec);
    auto RelocationsOrErr =
        relocations<XCOFFSectionHeader32, XCOFFRelocation32>(*SectionEntPtr);
    if (Error E = RelocationsOrErr.takeError()) {
      // TODO: report the error up the stack.
      consumeError(std::move(E));
      return relocation_iterator(RelocationRef());
    }
    Ret.p = reinterpret_cast<uintptr_t>(&*RelocationsOrErr.get().end());
  }
  return relocation_iterator(RelocationRef(Ret, this));
}

void XCOFFObjectFile::moveRelocationNext(DataRefImpl &Rel) const {
  if (is64Bit())
    Rel.p = reinterpret_cast<uintptr_t>(viewAs<XCOFFRelocation64>(Rel.p) + 1);
  else
    Rel.p = reinterpret_cast<uintptr_t>(viewAs<XCOFFRelocation32>(Rel.p) + 1);
}

uint64_t XCOFFObjectFile::getRelocationOffset(DataRefImpl Rel) const {
  if (is64Bit()) {
    const XCOFFRelocation64 *Reloc = viewAs<XCOFFRelocation64>(Rel.p);
    const XCOFFSectionHeader64 *Sec64 = sectionHeaderTable64();
    const uint64_t RelocAddress = Reloc->VirtualAddress;
    const uint16_t NumberOfSections = getNumberOfSections();
    for (uint16_t I = 0; I < NumberOfSections; ++I) {
      // Find which section this relocation belongs to, and get the
      // relocation offset relative to the start of the section.
      if (Sec64->VirtualAddress <= RelocAddress &&
          RelocAddress < Sec64->VirtualAddress + Sec64->SectionSize) {
        return RelocAddress - Sec64->VirtualAddress;
      }
      ++Sec64;
    }
  } else {
    const XCOFFRelocation32 *Reloc = viewAs<XCOFFRelocation32>(Rel.p);
    const XCOFFSectionHeader32 *Sec32 = sectionHeaderTable32();
    const uint32_t RelocAddress = Reloc->VirtualAddress;
    const uint16_t NumberOfSections = getNumberOfSections();
    for (uint16_t I = 0; I < NumberOfSections; ++I) {
      // Find which section this relocation belongs to, and get the
      // relocation offset relative to the start of the section.
      if (Sec32->VirtualAddress <= RelocAddress &&
          RelocAddress < Sec32->VirtualAddress + Sec32->SectionSize) {
        return RelocAddress - Sec32->VirtualAddress;
      }
      ++Sec32;
    }
  }
  return InvalidRelocOffset;
}

symbol_iterator XCOFFObjectFile::getRelocationSymbol(DataRefImpl Rel) const {
  uint32_t Index;
  if (is64Bit()) {
    const XCOFFRelocation64 *Reloc = viewAs<XCOFFRelocation64>(Rel.p);
    Index = Reloc->SymbolIndex;

    if (Index >= getNumberOfSymbolTableEntries64())
      return symbol_end();
  } else {
    const XCOFFRelocation32 *Reloc = viewAs<XCOFFRelocation32>(Rel.p);
    Index = Reloc->SymbolIndex;

    if (Index >= getLogicalNumberOfSymbolTableEntries32())
      return symbol_end();
  }
  DataRefImpl SymDRI;
  SymDRI.p = getSymbolEntryAddressByIndex(Index);
  return symbol_iterator(SymbolRef(SymDRI, this));
}

uint64_t XCOFFObjectFile::getRelocationType(DataRefImpl Rel) const {
  if (is64Bit())
    return viewAs<XCOFFRelocation64>(Rel.p)->Type;
  return viewAs<XCOFFRelocation32>(Rel.p)->Type;
}

void XCOFFObjectFile::getRelocationTypeName(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  StringRef Res;
  if (is64Bit()) {
    const XCOFFRelocation64 *Reloc = viewAs<XCOFFRelocation64>(Rel.p);
    Res = XCOFF::getRelocationTypeString(Reloc->Type);
  } else {
    const XCOFFRelocation32 *Reloc = viewAs<XCOFFRelocation32>(Rel.p);
    Res = XCOFF::getRelocationTypeString(Reloc->Type);
  }
  Result.append(Res.begin(), Res.end());
}

Expected<uint32_t> XCOFFObjectFile::getSymbolFlags(DataRefImpl Symb) const {
  XCOFFSymbolRef XCOFFSym = toSymbolRef(Symb);
  uint32_t Result = SymbolRef::SF_None;

  if (XCOFFSym.getSectionNumber() == XCOFF::N_ABS)
    Result |= SymbolRef::SF_Absolute;

  XCOFF::StorageClass SC = XCOFFSym.getStorageClass();
  if (XCOFF::C_EXT == SC || XCOFF::C_WEAKEXT == SC)
    Result |= SymbolRef::SF_Global;

  if (XCOFF::C_WEAKEXT == SC)
    Result |= SymbolRef::SF_Weak;

  if (XCOFFSym.isCsectSymbol()) {
    Expected<XCOFFCsectAuxRef> CsectAuxEntOrErr =
        XCOFFSym.getXCOFFCsectAuxRef();
    if (CsectAuxEntOrErr) {
      if (CsectAuxEntOrErr.get().getSymbolType() == XCOFF::XTY_CM)
        Result |= SymbolRef::SF_Common;
    } else
      return CsectAuxEntOrErr.takeError();
  }

  if (XCOFFSym.getSectionNumber() == XCOFF::N_UNDEF)
    Result |= SymbolRef::SF_Undefined;

  // There is no visibility in old 32 bit XCOFF object file interpret.
  if (is64Bit() || (auxiliaryHeader32() && (auxiliaryHeader32()->getVersion() ==
                                            NEW_XCOFF_INTERPRET))) {
    uint16_t SymType = XCOFFSym.getSymbolType();
    if ((SymType & VISIBILITY_MASK) == SYM_V_HIDDEN)
      Result |= SymbolRef::SF_Hidden;

    if ((SymType & VISIBILITY_MASK) == SYM_V_EXPORTED)
      Result |= SymbolRef::SF_Exported;
  }
  return Result;
}

basic_symbol_iterator XCOFFObjectFile::symbol_begin() const {
  DataRefImpl SymDRI;
  SymDRI.p = reinterpret_cast<uintptr_t>(SymbolTblPtr);
  return basic_symbol_iterator(SymbolRef(SymDRI, this));
}

basic_symbol_iterator XCOFFObjectFile::symbol_end() const {
  DataRefImpl SymDRI;
  const uint32_t NumberOfSymbolTableEntries = getNumberOfSymbolTableEntries();
  SymDRI.p = getSymbolEntryAddressByIndex(NumberOfSymbolTableEntries);
  return basic_symbol_iterator(SymbolRef(SymDRI, this));
}

XCOFFObjectFile::xcoff_symbol_iterator_range XCOFFObjectFile::symbols() const {
  return xcoff_symbol_iterator_range(symbol_begin(), symbol_end());
}

section_iterator XCOFFObjectFile::section_begin() const {
  DataRefImpl DRI;
  DRI.p = getSectionHeaderTableAddress();
  return section_iterator(SectionRef(DRI, this));
}

section_iterator XCOFFObjectFile::section_end() const {
  DataRefImpl DRI;
  DRI.p = getWithOffset(getSectionHeaderTableAddress(),
                        getNumberOfSections() * getSectionHeaderSize());
  return section_iterator(SectionRef(DRI, this));
}

uint8_t XCOFFObjectFile::getBytesInAddress() const { return is64Bit() ? 8 : 4; }

StringRef XCOFFObjectFile::getFileFormatName() const {
  return is64Bit() ? "aix5coff64-rs6000" : "aixcoff-rs6000";
}

Triple::ArchType XCOFFObjectFile::getArch() const {
  return is64Bit() ? Triple::ppc64 : Triple::ppc;
}

Expected<SubtargetFeatures> XCOFFObjectFile::getFeatures() const {
  return SubtargetFeatures();
}

bool XCOFFObjectFile::isRelocatableObject() const {
  if (is64Bit())
    return !(fileHeader64()->Flags & NoRelMask);
  return !(fileHeader32()->Flags & NoRelMask);
}

Expected<uint64_t> XCOFFObjectFile::getStartAddress() const {
  if (AuxiliaryHeader == nullptr)
    return 0;

  return is64Bit() ? auxiliaryHeader64()->getEntryPointAddr()
                   : auxiliaryHeader32()->getEntryPointAddr();
}

StringRef XCOFFObjectFile::mapDebugSectionName(StringRef Name) const {
  return StringSwitch<StringRef>(Name)
      .Case("dwinfo", "debug_info")
      .Case("dwline", "debug_line")
      .Case("dwpbnms", "debug_pubnames")
      .Case("dwpbtyp", "debug_pubtypes")
      .Case("dwarnge", "debug_aranges")
      .Case("dwabrev", "debug_abbrev")
      .Case("dwstr", "debug_str")
      .Case("dwrnges", "debug_ranges")
      .Case("dwloc", "debug_loc")
      .Case("dwframe", "debug_frame")
      .Case("dwmac", "debug_macinfo")
      .Default(Name);
}

size_t XCOFFObjectFile::getFileHeaderSize() const {
  return is64Bit() ? sizeof(XCOFFFileHeader64) : sizeof(XCOFFFileHeader32);
}

size_t XCOFFObjectFile::getSectionHeaderSize() const {
  return is64Bit() ? sizeof(XCOFFSectionHeader64) :
                     sizeof(XCOFFSectionHeader32);
}

bool XCOFFObjectFile::is64Bit() const {
  return Binary::ID_XCOFF64 == getType();
}

Expected<StringRef> XCOFFObjectFile::getRawData(const char *Start,
                                                uint64_t Size,
                                                StringRef Name) const {
  uintptr_t StartPtr = reinterpret_cast<uintptr_t>(Start);
  // TODO: this path is untested.
  if (Error E = Binary::checkOffset(Data, StartPtr, Size))
    return createError(toString(std::move(E)) + ": " + Name.data() +
                       " data with offset 0x" + Twine::utohexstr(StartPtr) +
                       " and size 0x" + Twine::utohexstr(Size) +
                       " goes past the end of the file");
  return StringRef(Start, Size);
}

uint16_t XCOFFObjectFile::getMagic() const {
  return is64Bit() ? fileHeader64()->Magic : fileHeader32()->Magic;
}

Expected<DataRefImpl> XCOFFObjectFile::getSectionByNum(int16_t Num) const {
  if (Num <= 0 || Num > getNumberOfSections())
    return createStringError(object_error::invalid_section_index,
                             "the section index (" + Twine(Num) +
                                 ") is invalid");

  DataRefImpl DRI;
  DRI.p = getWithOffset(getSectionHeaderTableAddress(),
                        getSectionHeaderSize() * (Num - 1));
  return DRI;
}

DataRefImpl
XCOFFObjectFile::getSectionByType(XCOFF::SectionTypeFlags SectType) const {
  DataRefImpl DRI;
  auto GetSectionAddr = [&](const auto &Sections) -> uintptr_t {
    for (const auto &Sec : Sections)
      if (Sec.getSectionType() == SectType)
        return reinterpret_cast<uintptr_t>(&Sec);
    return uintptr_t(0);
  };
  if (is64Bit())
    DRI.p = GetSectionAddr(sections64());
  else
    DRI.p = GetSectionAddr(sections32());
  return DRI;
}

Expected<StringRef>
XCOFFObjectFile::getSymbolSectionName(XCOFFSymbolRef SymEntPtr) const {
  const int16_t SectionNum = SymEntPtr.getSectionNumber();

  switch (SectionNum) {
  case XCOFF::N_DEBUG:
    return "N_DEBUG";
  case XCOFF::N_ABS:
    return "N_ABS";
  case XCOFF::N_UNDEF:
    return "N_UNDEF";
  default:
    Expected<DataRefImpl> SecRef = getSectionByNum(SectionNum);
    if (SecRef)
      return generateXCOFFFixedNameStringRef(
          getSectionNameInternal(SecRef.get()));
    return SecRef.takeError();
  }
}

unsigned XCOFFObjectFile::getSymbolSectionID(SymbolRef Sym) const {
  XCOFFSymbolRef XCOFFSymRef(Sym.getRawDataRefImpl(), this);
  return XCOFFSymRef.getSectionNumber();
}

bool XCOFFObjectFile::isReservedSectionNumber(int16_t SectionNumber) {
  return (SectionNumber <= 0 && SectionNumber >= -2);
}

uint16_t XCOFFObjectFile::getNumberOfSections() const {
  return is64Bit() ? fileHeader64()->NumberOfSections
                   : fileHeader32()->NumberOfSections;
}

int32_t XCOFFObjectFile::getTimeStamp() const {
  return is64Bit() ? fileHeader64()->TimeStamp : fileHeader32()->TimeStamp;
}

uint16_t XCOFFObjectFile::getOptionalHeaderSize() const {
  return is64Bit() ? fileHeader64()->AuxHeaderSize
                   : fileHeader32()->AuxHeaderSize;
}

uint32_t XCOFFObjectFile::getSymbolTableOffset32() const {
  return fileHeader32()->SymbolTableOffset;
}

int32_t XCOFFObjectFile::getRawNumberOfSymbolTableEntries32() const {
  // As far as symbol table size is concerned, if this field is negative it is
  // to be treated as a 0. However since this field is also used for printing we
  // don't want to truncate any negative values.
  return fileHeader32()->NumberOfSymTableEntries;
}

uint32_t XCOFFObjectFile::getLogicalNumberOfSymbolTableEntries32() const {
  return (fileHeader32()->NumberOfSymTableEntries >= 0
              ? fileHeader32()->NumberOfSymTableEntries
              : 0);
}

uint64_t XCOFFObjectFile::getSymbolTableOffset64() const {
  return fileHeader64()->SymbolTableOffset;
}

uint32_t XCOFFObjectFile::getNumberOfSymbolTableEntries64() const {
  return fileHeader64()->NumberOfSymTableEntries;
}

uint32_t XCOFFObjectFile::getNumberOfSymbolTableEntries() const {
  return is64Bit() ? getNumberOfSymbolTableEntries64()
                   : getLogicalNumberOfSymbolTableEntries32();
}

uintptr_t XCOFFObjectFile::getEndOfSymbolTableAddress() const {
  const uint32_t NumberOfSymTableEntries = getNumberOfSymbolTableEntries();
  return getWithOffset(reinterpret_cast<uintptr_t>(SymbolTblPtr),
                       XCOFF::SymbolTableEntrySize * NumberOfSymTableEntries);
}

void XCOFFObjectFile::checkSymbolEntryPointer(uintptr_t SymbolEntPtr) const {
  if (SymbolEntPtr < reinterpret_cast<uintptr_t>(SymbolTblPtr))
    report_fatal_error("Symbol table entry is outside of symbol table.");

  if (SymbolEntPtr >= getEndOfSymbolTableAddress())
    report_fatal_error("Symbol table entry is outside of symbol table.");

  ptrdiff_t Offset = reinterpret_cast<const char *>(SymbolEntPtr) -
                     reinterpret_cast<const char *>(SymbolTblPtr);

  if (Offset % XCOFF::SymbolTableEntrySize != 0)
    report_fatal_error(
        "Symbol table entry position is not valid inside of symbol table.");
}

uint32_t XCOFFObjectFile::getSymbolIndex(uintptr_t SymbolEntPtr) const {
  return (reinterpret_cast<const char *>(SymbolEntPtr) -
          reinterpret_cast<const char *>(SymbolTblPtr)) /
         XCOFF::SymbolTableEntrySize;
}

uint64_t XCOFFObjectFile::getSymbolSize(DataRefImpl Symb) const {
  uint64_t Result = 0;
  XCOFFSymbolRef XCOFFSym = toSymbolRef(Symb);
  if (XCOFFSym.isCsectSymbol()) {
    Expected<XCOFFCsectAuxRef> CsectAuxRefOrError =
        XCOFFSym.getXCOFFCsectAuxRef();
    if (!CsectAuxRefOrError)
      // TODO: report the error up the stack.
      consumeError(CsectAuxRefOrError.takeError());
    else {
      XCOFFCsectAuxRef CsectAuxRef = CsectAuxRefOrError.get();
      uint8_t SymType = CsectAuxRef.getSymbolType();
      if (SymType == XCOFF::XTY_SD || SymType == XCOFF::XTY_CM)
        Result = CsectAuxRef.getSectionOrLength();
    }
  }
  return Result;
}

uintptr_t XCOFFObjectFile::getSymbolEntryAddressByIndex(uint32_t Index) const {
  return getAdvancedSymbolEntryAddress(
      reinterpret_cast<uintptr_t>(getPointerToSymbolTable()), Index);
}

Expected<StringRef>
XCOFFObjectFile::getSymbolNameByIndex(uint32_t Index) const {
  const uint32_t NumberOfSymTableEntries = getNumberOfSymbolTableEntries();

  if (Index >= NumberOfSymTableEntries)
    return createError("symbol index " + Twine(Index) +
                       " exceeds symbol count " +
                       Twine(NumberOfSymTableEntries));

  DataRefImpl SymDRI;
  SymDRI.p = getSymbolEntryAddressByIndex(Index);
  return getSymbolName(SymDRI);
}

uint16_t XCOFFObjectFile::getFlags() const {
  return is64Bit() ? fileHeader64()->Flags : fileHeader32()->Flags;
}

const char *XCOFFObjectFile::getSectionNameInternal(DataRefImpl Sec) const {
  return is64Bit() ? toSection64(Sec)->Name : toSection32(Sec)->Name;
}

uintptr_t XCOFFObjectFile::getSectionHeaderTableAddress() const {
  return reinterpret_cast<uintptr_t>(SectionHeaderTable);
}

int32_t XCOFFObjectFile::getSectionFlags(DataRefImpl Sec) const {
  return is64Bit() ? toSection64(Sec)->Flags : toSection32(Sec)->Flags;
}

XCOFFObjectFile::XCOFFObjectFile(unsigned int Type, MemoryBufferRef Object)
    : ObjectFile(Type, Object) {
  assert(Type == Binary::ID_XCOFF32 || Type == Binary::ID_XCOFF64);
}

ArrayRef<XCOFFSectionHeader64> XCOFFObjectFile::sections64() const {
  assert(is64Bit() && "64-bit interface called for non 64-bit file.");
  const XCOFFSectionHeader64 *TablePtr = sectionHeaderTable64();
  return ArrayRef<XCOFFSectionHeader64>(TablePtr,
                                        TablePtr + getNumberOfSections());
}

ArrayRef<XCOFFSectionHeader32> XCOFFObjectFile::sections32() const {
  assert(!is64Bit() && "32-bit interface called for non 32-bit file.");
  const XCOFFSectionHeader32 *TablePtr = sectionHeaderTable32();
  return ArrayRef<XCOFFSectionHeader32>(TablePtr,
                                        TablePtr + getNumberOfSections());
}

// In an XCOFF32 file, when the field value is 65535, then an STYP_OVRFLO
// section header contains the actual count of relocation entries in the s_paddr
// field. STYP_OVRFLO headers contain the section index of their corresponding
// sections as their raw "NumberOfRelocations" field value.
template <typename T>
Expected<uint32_t> XCOFFObjectFile::getNumberOfRelocationEntries(
    const XCOFFSectionHeader<T> &Sec) const {
  const T &Section = static_cast<const T &>(Sec);
  if (is64Bit())
    return Section.NumberOfRelocations;

  uint16_t SectionIndex = &Section - sectionHeaderTable<T>() + 1;
  if (Section.NumberOfRelocations < XCOFF::RelocOverflow)
    return Section.NumberOfRelocations;
  for (const auto &Sec : sections32()) {
    if (Sec.Flags == XCOFF::STYP_OVRFLO &&
        Sec.NumberOfRelocations == SectionIndex)
      return Sec.PhysicalAddress;
  }
  return errorCodeToError(object_error::parse_failed);
}

template <typename Shdr, typename Reloc>
Expected<ArrayRef<Reloc>> XCOFFObjectFile::relocations(const Shdr &Sec) const {
  uintptr_t RelocAddr = getWithOffset(reinterpret_cast<uintptr_t>(FileHeader),
                                      Sec.FileOffsetToRelocationInfo);
  auto NumRelocEntriesOrErr = getNumberOfRelocationEntries(Sec);
  if (Error E = NumRelocEntriesOrErr.takeError())
    return std::move(E);

  uint32_t NumRelocEntries = NumRelocEntriesOrErr.get();
  static_assert((sizeof(Reloc) == XCOFF::RelocationSerializationSize64 ||
                 sizeof(Reloc) == XCOFF::RelocationSerializationSize32),
                "Relocation structure is incorrect");
  auto RelocationOrErr =
      getObject<Reloc>(Data, reinterpret_cast<void *>(RelocAddr),
                       NumRelocEntries * sizeof(Reloc));
  if (!RelocationOrErr)
    return createError(
        toString(RelocationOrErr.takeError()) + ": relocations with offset 0x" +
        Twine::utohexstr(Sec.FileOffsetToRelocationInfo) + " and size 0x" +
        Twine::utohexstr(NumRelocEntries * sizeof(Reloc)) +
        " go past the end of the file");

  const Reloc *StartReloc = RelocationOrErr.get();

  return ArrayRef<Reloc>(StartReloc, StartReloc + NumRelocEntries);
}

template <typename ExceptEnt>
Expected<ArrayRef<ExceptEnt>> XCOFFObjectFile::getExceptionEntries() const {
  assert((is64Bit() && sizeof(ExceptEnt) == sizeof(ExceptionSectionEntry64)) ||
         (!is64Bit() && sizeof(ExceptEnt) == sizeof(ExceptionSectionEntry32)));

  Expected<uintptr_t> ExceptionSectOrErr =
      getSectionFileOffsetToRawData(XCOFF::STYP_EXCEPT);
  if (!ExceptionSectOrErr)
    return ExceptionSectOrErr.takeError();

  DataRefImpl DRI = getSectionByType(XCOFF::STYP_EXCEPT);
  if (DRI.p == 0)
    return ArrayRef<ExceptEnt>();

  ExceptEnt *ExceptEntStart =
      reinterpret_cast<ExceptEnt *>(*ExceptionSectOrErr);
  return ArrayRef<ExceptEnt>(
      ExceptEntStart, ExceptEntStart + getSectionSize(DRI) / sizeof(ExceptEnt));
}

template Expected<ArrayRef<ExceptionSectionEntry32>>
XCOFFObjectFile::getExceptionEntries() const;
template Expected<ArrayRef<ExceptionSectionEntry64>>
XCOFFObjectFile::getExceptionEntries() const;

Expected<XCOFFStringTable>
XCOFFObjectFile::parseStringTable(const XCOFFObjectFile *Obj, uint64_t Offset) {
  // If there is a string table, then the buffer must contain at least 4 bytes
  // for the string table's size. Not having a string table is not an error.
  if (Error E = Binary::checkOffset(
          Obj->Data, reinterpret_cast<uintptr_t>(Obj->base() + Offset), 4)) {
    consumeError(std::move(E));
    return XCOFFStringTable{0, nullptr};
  }

  // Read the size out of the buffer.
  uint32_t Size = support::endian::read32be(Obj->base() + Offset);

  // If the size is less then 4, then the string table is just a size and no
  // string data.
  if (Size <= 4)
    return XCOFFStringTable{4, nullptr};

  auto StringTableOrErr =
      getObject<char>(Obj->Data, Obj->base() + Offset, Size);
  if (!StringTableOrErr)
    return createError(toString(StringTableOrErr.takeError()) +
                       ": string table with offset 0x" +
                       Twine::utohexstr(Offset) + " and size 0x" +
                       Twine::utohexstr(Size) +
                       " goes past the end of the file");

  const char *StringTablePtr = StringTableOrErr.get();
  if (StringTablePtr[Size - 1] != '\0')
    return errorCodeToError(object_error::string_table_non_null_end);

  return XCOFFStringTable{Size, StringTablePtr};
}

// This function returns the import file table. Each entry in the import file
// table consists of: "path_name\0base_name\0archive_member_name\0".
Expected<StringRef> XCOFFObjectFile::getImportFileTable() const {
  Expected<uintptr_t> LoaderSectionAddrOrError =
      getSectionFileOffsetToRawData(XCOFF::STYP_LOADER);
  if (!LoaderSectionAddrOrError)
    return LoaderSectionAddrOrError.takeError();

  uintptr_t LoaderSectionAddr = LoaderSectionAddrOrError.get();
  if (!LoaderSectionAddr)
    return StringRef();

  uint64_t OffsetToImportFileTable = 0;
  uint64_t LengthOfImportFileTable = 0;
  if (is64Bit()) {
    const LoaderSectionHeader64 *LoaderSec64 =
        viewAs<LoaderSectionHeader64>(LoaderSectionAddr);
    OffsetToImportFileTable = LoaderSec64->OffsetToImpid;
    LengthOfImportFileTable = LoaderSec64->LengthOfImpidStrTbl;
  } else {
    const LoaderSectionHeader32 *LoaderSec32 =
        viewAs<LoaderSectionHeader32>(LoaderSectionAddr);
    OffsetToImportFileTable = LoaderSec32->OffsetToImpid;
    LengthOfImportFileTable = LoaderSec32->LengthOfImpidStrTbl;
  }

  auto ImportTableOrErr = getObject<char>(
      Data,
      reinterpret_cast<void *>(LoaderSectionAddr + OffsetToImportFileTable),
      LengthOfImportFileTable);
  if (!ImportTableOrErr)
    return createError(
        toString(ImportTableOrErr.takeError()) +
        ": import file table with offset 0x" +
        Twine::utohexstr(LoaderSectionAddr + OffsetToImportFileTable) +
        " and size 0x" + Twine::utohexstr(LengthOfImportFileTable) +
        " goes past the end of the file");

  const char *ImportTablePtr = ImportTableOrErr.get();
  if (ImportTablePtr[LengthOfImportFileTable - 1] != '\0')
    return createError(
        ": import file name table with offset 0x" +
        Twine::utohexstr(LoaderSectionAddr + OffsetToImportFileTable) +
        " and size 0x" + Twine::utohexstr(LengthOfImportFileTable) +
        " must end with a null terminator");

  return StringRef(ImportTablePtr, LengthOfImportFileTable);
}

Expected<std::unique_ptr<XCOFFObjectFile>>
XCOFFObjectFile::create(unsigned Type, MemoryBufferRef MBR) {
  // Can't use std::make_unique because of the private constructor.
  std::unique_ptr<XCOFFObjectFile> Obj;
  Obj.reset(new XCOFFObjectFile(Type, MBR));

  uint64_t CurOffset = 0;
  const auto *Base = Obj->base();
  MemoryBufferRef Data = Obj->Data;

  // Parse file header.
  auto FileHeaderOrErr =
      getObject<void>(Data, Base + CurOffset, Obj->getFileHeaderSize());
  if (Error E = FileHeaderOrErr.takeError())
    return std::move(E);
  Obj->FileHeader = FileHeaderOrErr.get();

  CurOffset += Obj->getFileHeaderSize();

  if (Obj->getOptionalHeaderSize()) {
    auto AuxiliaryHeaderOrErr =
        getObject<void>(Data, Base + CurOffset, Obj->getOptionalHeaderSize());
    if (Error E = AuxiliaryHeaderOrErr.takeError())
      return std::move(E);
    Obj->AuxiliaryHeader = AuxiliaryHeaderOrErr.get();
  }

  CurOffset += Obj->getOptionalHeaderSize();

  // Parse the section header table if it is present.
  if (Obj->getNumberOfSections()) {
    uint64_t SectionHeadersSize =
        Obj->getNumberOfSections() * Obj->getSectionHeaderSize();
    auto SecHeadersOrErr =
        getObject<void>(Data, Base + CurOffset, SectionHeadersSize);
    if (!SecHeadersOrErr)
      return createError(toString(SecHeadersOrErr.takeError()) +
                         ": section headers with offset 0x" +
                         Twine::utohexstr(CurOffset) + " and size 0x" +
                         Twine::utohexstr(SectionHeadersSize) +
                         " go past the end of the file");

    Obj->SectionHeaderTable = SecHeadersOrErr.get();
  }

  const uint32_t NumberOfSymbolTableEntries =
      Obj->getNumberOfSymbolTableEntries();

  // If there is no symbol table we are done parsing the memory buffer.
  if (NumberOfSymbolTableEntries == 0)
    return std::move(Obj);

  // Parse symbol table.
  CurOffset = Obj->is64Bit() ? Obj->getSymbolTableOffset64()
                             : Obj->getSymbolTableOffset32();
  const uint64_t SymbolTableSize =
      static_cast<uint64_t>(XCOFF::SymbolTableEntrySize) *
      NumberOfSymbolTableEntries;
  auto SymTableOrErr =
      getObject<void *>(Data, Base + CurOffset, SymbolTableSize);
  if (!SymTableOrErr)
    return createError(
        toString(SymTableOrErr.takeError()) + ": symbol table with offset 0x" +
        Twine::utohexstr(CurOffset) + " and size 0x" +
        Twine::utohexstr(SymbolTableSize) + " goes past the end of the file");

  Obj->SymbolTblPtr = SymTableOrErr.get();
  CurOffset += SymbolTableSize;

  // Parse String table.
  Expected<XCOFFStringTable> StringTableOrErr =
      parseStringTable(Obj.get(), CurOffset);
  if (Error E = StringTableOrErr.takeError())
    return std::move(E);
  Obj->StringTable = StringTableOrErr.get();

  return std::move(Obj);
}

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createXCOFFObjectFile(MemoryBufferRef MemBufRef,
                                  unsigned FileType) {
  return XCOFFObjectFile::create(FileType, MemBufRef);
}

std::optional<StringRef> XCOFFObjectFile::tryGetCPUName() const {
  return StringRef("future");
}

Expected<bool> XCOFFSymbolRef::isFunction() const {
  if (!isCsectSymbol())
    return false;

  if (getSymbolType() & FunctionSym)
    return true;

  Expected<XCOFFCsectAuxRef> ExpCsectAuxEnt = getXCOFFCsectAuxRef();
  if (!ExpCsectAuxEnt)
    return ExpCsectAuxEnt.takeError();

  const XCOFFCsectAuxRef CsectAuxRef = ExpCsectAuxEnt.get();

  if (CsectAuxRef.getStorageMappingClass() != XCOFF::XMC_PR &&
      CsectAuxRef.getStorageMappingClass() != XCOFF::XMC_GL)
    return false;

  // A function definition should not be a common type symbol or an external
  // symbol.
  if (CsectAuxRef.getSymbolType() == XCOFF::XTY_CM ||
      CsectAuxRef.getSymbolType() == XCOFF::XTY_ER)
    return false;

  // If the next symbol is an XTY_LD type symbol with the same address, this
  // XTY_SD symbol is not a function. Otherwise this is a function symbol for
  // -ffunction-sections.
  if (CsectAuxRef.getSymbolType() == XCOFF::XTY_SD) {
    // If this is a csect with size 0, it won't be a function definition.
    // This is used to work around the fact that LLVM always generates below
    // symbol for -ffunction-sections:
    // m   0x00000000     .text     1  unamex                    **No Symbol**
    // a4  0x00000000       0    0     SD       PR    0    0
    // FIXME: remove or replace this meaningless symbol.
    if (getSize() == 0)
      return false;

    xcoff_symbol_iterator NextIt(this);
    // If this is the last main symbol table entry, there won't be an XTY_LD
    // type symbol below.
    if (++NextIt == getObject()->symbol_end())
      return true;

    if (cantFail(getAddress()) != cantFail(NextIt->getAddress()))
      return true;

    // Check next symbol is XTY_LD. If so, this symbol is not a function.
    Expected<XCOFFCsectAuxRef> NextCsectAuxEnt = NextIt->getXCOFFCsectAuxRef();
    if (!NextCsectAuxEnt)
      return NextCsectAuxEnt.takeError();

    if (NextCsectAuxEnt.get().getSymbolType() == XCOFF::XTY_LD)
      return false;

    return true;
  }

  if (CsectAuxRef.getSymbolType() == XCOFF::XTY_LD)
    return true;

  return createError(
      "symbol csect aux entry with index " +
      Twine(getObject()->getSymbolIndex(CsectAuxRef.getEntryAddress())) +
      " has invalid symbol type " +
      Twine::utohexstr(CsectAuxRef.getSymbolType()));
}

bool XCOFFSymbolRef::isCsectSymbol() const {
  XCOFF::StorageClass SC = getStorageClass();
  return (SC == XCOFF::C_EXT || SC == XCOFF::C_WEAKEXT ||
          SC == XCOFF::C_HIDEXT);
}

Expected<XCOFFCsectAuxRef> XCOFFSymbolRef::getXCOFFCsectAuxRef() const {
  assert(isCsectSymbol() &&
         "Calling csect symbol interface with a non-csect symbol.");

  uint8_t NumberOfAuxEntries = getNumberOfAuxEntries();

  Expected<StringRef> NameOrErr = getName();
  if (auto Err = NameOrErr.takeError())
    return std::move(Err);

  uint32_t SymbolIdx = getObject()->getSymbolIndex(getEntryAddress());
  if (!NumberOfAuxEntries) {
    return createError("csect symbol \"" + *NameOrErr + "\" with index " +
                       Twine(SymbolIdx) + " contains no auxiliary entry");
  }

  if (!getObject()->is64Bit()) {
    // In XCOFF32, the csect auxilliary entry is always the last auxiliary
    // entry for the symbol.
    uintptr_t AuxAddr = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
        getEntryAddress(), NumberOfAuxEntries);
    return XCOFFCsectAuxRef(viewAs<XCOFFCsectAuxEnt32>(AuxAddr));
  }

  // XCOFF64 uses SymbolAuxType to identify the auxiliary entry type.
  // We need to iterate through all the auxiliary entries to find it.
  for (uint8_t Index = NumberOfAuxEntries; Index > 0; --Index) {
    uintptr_t AuxAddr = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
        getEntryAddress(), Index);
    if (*getObject()->getSymbolAuxType(AuxAddr) ==
        XCOFF::SymbolAuxType::AUX_CSECT) {
#ifndef NDEBUG
      getObject()->checkSymbolEntryPointer(AuxAddr);
#endif
      return XCOFFCsectAuxRef(viewAs<XCOFFCsectAuxEnt64>(AuxAddr));
    }
  }

  return createError(
      "a csect auxiliary entry has not been found for symbol \"" + *NameOrErr +
      "\" with index " + Twine(SymbolIdx));
}

Expected<StringRef> XCOFFSymbolRef::getName() const {
  // A storage class value with the high-order bit on indicates that the name is
  // a symbolic debugger stabstring.
  if (getStorageClass() & 0x80)
    return StringRef("Unimplemented Debug Name");

  if (!getObject()->is64Bit()) {
    if (getSymbol32()->NameInStrTbl.Magic !=
        XCOFFSymbolRef::NAME_IN_STR_TBL_MAGIC)
      return generateXCOFFFixedNameStringRef(getSymbol32()->SymbolName);

    return getObject()->getStringTableEntry(getSymbol32()->NameInStrTbl.Offset);
  }

  return getObject()->getStringTableEntry(getSymbol64()->Offset);
}

// Explicitly instantiate template classes.
template struct XCOFFSectionHeader<XCOFFSectionHeader32>;
template struct XCOFFSectionHeader<XCOFFSectionHeader64>;

template struct XCOFFRelocation<llvm::support::ubig32_t>;
template struct XCOFFRelocation<llvm::support::ubig64_t>;

template llvm::Expected<llvm::ArrayRef<llvm::object::XCOFFRelocation64>>
llvm::object::XCOFFObjectFile::relocations<llvm::object::XCOFFSectionHeader64,
                                           llvm::object::XCOFFRelocation64>(
    llvm::object::XCOFFSectionHeader64 const &) const;
template llvm::Expected<llvm::ArrayRef<llvm::object::XCOFFRelocation32>>
llvm::object::XCOFFObjectFile::relocations<llvm::object::XCOFFSectionHeader32,
                                           llvm::object::XCOFFRelocation32>(
    llvm::object::XCOFFSectionHeader32 const &) const;

bool doesXCOFFTracebackTableBegin(ArrayRef<uint8_t> Bytes) {
  if (Bytes.size() < 4)
    return false;

  return support::endian::read32be(Bytes.data()) == 0;
}

#define GETVALUEWITHMASK(X) (Data & (TracebackTable::X))
#define GETVALUEWITHMASKSHIFT(X, S)                                            \
  ((Data & (TracebackTable::X)) >> (TracebackTable::S))

Expected<TBVectorExt> TBVectorExt::create(StringRef TBvectorStrRef) {
  Error Err = Error::success();
  TBVectorExt TBTVecExt(TBvectorStrRef, Err);
  if (Err)
    return std::move(Err);
  return TBTVecExt;
}

TBVectorExt::TBVectorExt(StringRef TBvectorStrRef, Error &Err) {
  const uint8_t *Ptr = reinterpret_cast<const uint8_t *>(TBvectorStrRef.data());
  Data = support::endian::read16be(Ptr);
  uint32_t VecParmsTypeValue = support::endian::read32be(Ptr + 2);
  unsigned ParmsNum =
      GETVALUEWITHMASKSHIFT(NumberOfVectorParmsMask, NumberOfVectorParmsShift);

  ErrorAsOutParameter EAO(&Err);
  Expected<SmallString<32>> VecParmsTypeOrError =
      parseVectorParmsType(VecParmsTypeValue, ParmsNum);
  if (!VecParmsTypeOrError)
    Err = VecParmsTypeOrError.takeError();
  else
    VecParmsInfo = VecParmsTypeOrError.get();
}

uint8_t TBVectorExt::getNumberOfVRSaved() const {
  return GETVALUEWITHMASKSHIFT(NumberOfVRSavedMask, NumberOfVRSavedShift);
}

bool TBVectorExt::isVRSavedOnStack() const {
  return GETVALUEWITHMASK(IsVRSavedOnStackMask);
}

bool TBVectorExt::hasVarArgs() const {
  return GETVALUEWITHMASK(HasVarArgsMask);
}

uint8_t TBVectorExt::getNumberOfVectorParms() const {
  return GETVALUEWITHMASKSHIFT(NumberOfVectorParmsMask,
                               NumberOfVectorParmsShift);
}

bool TBVectorExt::hasVMXInstruction() const {
  return GETVALUEWITHMASK(HasVMXInstructionMask);
}
#undef GETVALUEWITHMASK
#undef GETVALUEWITHMASKSHIFT

Expected<XCOFFTracebackTable>
XCOFFTracebackTable::create(const uint8_t *Ptr, uint64_t &Size, bool Is64Bit) {
  Error Err = Error::success();
  XCOFFTracebackTable TBT(Ptr, Size, Err, Is64Bit);
  if (Err)
    return std::move(Err);
  return TBT;
}

XCOFFTracebackTable::XCOFFTracebackTable(const uint8_t *Ptr, uint64_t &Size,
                                         Error &Err, bool Is64Bit)
    : TBPtr(Ptr), Is64BitObj(Is64Bit) {
  ErrorAsOutParameter EAO(&Err);
  DataExtractor DE(ArrayRef<uint8_t>(Ptr, Size), /*IsLittleEndian=*/false,
                   /*AddressSize=*/0);
  DataExtractor::Cursor Cur(/*Offset=*/0);

  // Skip 8 bytes of mandatory fields.
  DE.getU64(Cur);

  unsigned FixedParmsNum = getNumberOfFixedParms();
  unsigned FloatingParmsNum = getNumberOfFPParms();
  uint32_t ParamsTypeValue = 0;

  // Begin to parse optional fields.
  if (Cur && (FixedParmsNum + FloatingParmsNum) > 0)
    ParamsTypeValue = DE.getU32(Cur);

  if (Cur && hasTraceBackTableOffset())
    TraceBackTableOffset = DE.getU32(Cur);

  if (Cur && isInterruptHandler())
    HandlerMask = DE.getU32(Cur);

  if (Cur && hasControlledStorage()) {
    NumOfCtlAnchors = DE.getU32(Cur);
    if (Cur && NumOfCtlAnchors) {
      SmallVector<uint32_t, 8> Disp;
      Disp.reserve(*NumOfCtlAnchors);
      for (uint32_t I = 0; I < NumOfCtlAnchors && Cur; ++I)
        Disp.push_back(DE.getU32(Cur));
      if (Cur)
        ControlledStorageInfoDisp = std::move(Disp);
    }
  }

  if (Cur && isFuncNamePresent()) {
    uint16_t FunctionNameLen = DE.getU16(Cur);
    if (Cur)
      FunctionName = DE.getBytes(Cur, FunctionNameLen);
  }

  if (Cur && isAllocaUsed())
    AllocaRegister = DE.getU8(Cur);

  unsigned VectorParmsNum = 0;
  if (Cur && hasVectorInfo()) {
    StringRef VectorExtRef = DE.getBytes(Cur, 6);
    if (Cur) {
      Expected<TBVectorExt> TBVecExtOrErr = TBVectorExt::create(VectorExtRef);
      if (!TBVecExtOrErr) {
        Err = TBVecExtOrErr.takeError();
        return;
      }
      VecExt = TBVecExtOrErr.get();
      VectorParmsNum = VecExt->getNumberOfVectorParms();
      // Skip two bytes of padding after vector info.
      DE.skip(Cur, 2);
    }
  }

  // As long as there is no fixed-point or floating-point parameter, this
  // field remains not present even when hasVectorInfo gives true and
  // indicates the presence of vector parameters.
  if (Cur && (FixedParmsNum + FloatingParmsNum) > 0) {
    Expected<SmallString<32>> ParmsTypeOrError =
        hasVectorInfo()
            ? parseParmsTypeWithVecInfo(ParamsTypeValue, FixedParmsNum,
                                        FloatingParmsNum, VectorParmsNum)
            : parseParmsType(ParamsTypeValue, FixedParmsNum, FloatingParmsNum);

    if (!ParmsTypeOrError) {
      Err = ParmsTypeOrError.takeError();
      return;
    }
    ParmsType = ParmsTypeOrError.get();
  }

  if (Cur && hasExtensionTable()) {
    ExtensionTable = DE.getU8(Cur);

    if (*ExtensionTable & ExtendedTBTableFlag::TB_EH_INFO) {
      // eh_info displacement must be 4-byte aligned.
      Cur.seek(alignTo(Cur.tell(), 4));
      EhInfoDisp = Is64BitObj ? DE.getU64(Cur) : DE.getU32(Cur);
    }
  }
  if (!Cur)
    Err = Cur.takeError();

  Size = Cur.tell();
}

#define GETBITWITHMASK(P, X)                                                   \
  (support::endian::read32be(TBPtr + (P)) & (TracebackTable::X))
#define GETBITWITHMASKSHIFT(P, X, S)                                           \
  ((support::endian::read32be(TBPtr + (P)) & (TracebackTable::X)) >>           \
   (TracebackTable::S))

uint8_t XCOFFTracebackTable::getVersion() const {
  return GETBITWITHMASKSHIFT(0, VersionMask, VersionShift);
}

uint8_t XCOFFTracebackTable::getLanguageID() const {
  return GETBITWITHMASKSHIFT(0, LanguageIdMask, LanguageIdShift);
}

bool XCOFFTracebackTable::isGlobalLinkage() const {
  return GETBITWITHMASK(0, IsGlobaLinkageMask);
}

bool XCOFFTracebackTable::isOutOfLineEpilogOrPrologue() const {
  return GETBITWITHMASK(0, IsOutOfLineEpilogOrPrologueMask);
}

bool XCOFFTracebackTable::hasTraceBackTableOffset() const {
  return GETBITWITHMASK(0, HasTraceBackTableOffsetMask);
}

bool XCOFFTracebackTable::isInternalProcedure() const {
  return GETBITWITHMASK(0, IsInternalProcedureMask);
}

bool XCOFFTracebackTable::hasControlledStorage() const {
  return GETBITWITHMASK(0, HasControlledStorageMask);
}

bool XCOFFTracebackTable::isTOCless() const {
  return GETBITWITHMASK(0, IsTOClessMask);
}

bool XCOFFTracebackTable::isFloatingPointPresent() const {
  return GETBITWITHMASK(0, IsFloatingPointPresentMask);
}

bool XCOFFTracebackTable::isFloatingPointOperationLogOrAbortEnabled() const {
  return GETBITWITHMASK(0, IsFloatingPointOperationLogOrAbortEnabledMask);
}

bool XCOFFTracebackTable::isInterruptHandler() const {
  return GETBITWITHMASK(0, IsInterruptHandlerMask);
}

bool XCOFFTracebackTable::isFuncNamePresent() const {
  return GETBITWITHMASK(0, IsFunctionNamePresentMask);
}

bool XCOFFTracebackTable::isAllocaUsed() const {
  return GETBITWITHMASK(0, IsAllocaUsedMask);
}

uint8_t XCOFFTracebackTable::getOnConditionDirective() const {
  return GETBITWITHMASKSHIFT(0, OnConditionDirectiveMask,
                             OnConditionDirectiveShift);
}

bool XCOFFTracebackTable::isCRSaved() const {
  return GETBITWITHMASK(0, IsCRSavedMask);
}

bool XCOFFTracebackTable::isLRSaved() const {
  return GETBITWITHMASK(0, IsLRSavedMask);
}

bool XCOFFTracebackTable::isBackChainStored() const {
  return GETBITWITHMASK(4, IsBackChainStoredMask);
}

bool XCOFFTracebackTable::isFixup() const {
  return GETBITWITHMASK(4, IsFixupMask);
}

uint8_t XCOFFTracebackTable::getNumOfFPRsSaved() const {
  return GETBITWITHMASKSHIFT(4, FPRSavedMask, FPRSavedShift);
}

bool XCOFFTracebackTable::hasExtensionTable() const {
  return GETBITWITHMASK(4, HasExtensionTableMask);
}

bool XCOFFTracebackTable::hasVectorInfo() const {
  return GETBITWITHMASK(4, HasVectorInfoMask);
}

uint8_t XCOFFTracebackTable::getNumOfGPRsSaved() const {
  return GETBITWITHMASKSHIFT(4, GPRSavedMask, GPRSavedShift);
}

uint8_t XCOFFTracebackTable::getNumberOfFixedParms() const {
  return GETBITWITHMASKSHIFT(4, NumberOfFixedParmsMask,
                             NumberOfFixedParmsShift);
}

uint8_t XCOFFTracebackTable::getNumberOfFPParms() const {
  return GETBITWITHMASKSHIFT(4, NumberOfFloatingPointParmsMask,
                             NumberOfFloatingPointParmsShift);
}

bool XCOFFTracebackTable::hasParmsOnStack() const {
  return GETBITWITHMASK(4, HasParmsOnStackMask);
}

#undef GETBITWITHMASK
#undef GETBITWITHMASKSHIFT
} // namespace object
} // namespace llvm
