//===- GOFFObjectFile.cpp - GOFF object file implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the GOFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/GOFFObjectFile.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/Object/GOFF.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/raw_ostream.h"

#ifndef DEBUG_TYPE
#define DEBUG_TYPE "goff"
#endif

using namespace llvm::object;
using namespace llvm;

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createGOFFObjectFile(MemoryBufferRef Object) {
  Error Err = Error::success();
  std::unique_ptr<GOFFObjectFile> Ret(new GOFFObjectFile(Object, Err));
  if (Err)
    return std::move(Err);
  return std::move(Ret);
}

GOFFObjectFile::GOFFObjectFile(MemoryBufferRef Object, Error &Err)
    : ObjectFile(Binary::ID_GOFF, Object) {
  ErrorAsOutParameter ErrAsOutParam(&Err);
  // Object file isn't the right size, bail out early.
  if ((Object.getBufferSize() % GOFF::RecordLength) != 0) {
    Err = createStringError(
        object_error::unexpected_eof,
        "object file is not the right size. Must be a multiple "
        "of 80 bytes, but is " +
            std::to_string(Object.getBufferSize()) + " bytes");
    return;
  }
  // Object file doesn't start/end with HDR/END records.
  // Bail out early.
  if (Object.getBufferSize() != 0) {
    if ((base()[1] & 0xF0) >> 4 != GOFF::RT_HDR) {
      Err = createStringError(object_error::parse_failed,
                              "object file must start with HDR record");
      return;
    }
    if ((base()[Object.getBufferSize() - GOFF::RecordLength + 1] & 0xF0) >> 4 !=
        GOFF::RT_END) {
      Err = createStringError(object_error::parse_failed,
                              "object file must end with END record");
      return;
    }
  }

  SectionEntryImpl DummySection;
  SectionList.emplace_back(DummySection); // Dummy entry at index 0.

  uint8_t PrevRecordType = 0;
  uint8_t PrevContinuationBits = 0;
  const uint8_t *End = reinterpret_cast<const uint8_t *>(Data.getBufferEnd());
  for (const uint8_t *I = base(); I < End; I += GOFF::RecordLength) {
    uint8_t RecordType = (I[1] & 0xF0) >> 4;
    bool IsContinuation = I[1] & 0x02;
    bool PrevWasContinued = PrevContinuationBits & 0x01;
    size_t RecordNum = (I - base()) / GOFF::RecordLength;

    // If the previous record was continued, the current record should be a
    // continuation.
    if (PrevWasContinued && !IsContinuation) {
      if (PrevRecordType == RecordType) {
        Err = createStringError(object_error::parse_failed,
                                "record " + std::to_string(RecordNum) +
                                    " is not a continuation record but the "
                                    "preceding record is continued");
        return;
      }
    }
    // Don't parse continuations records, only parse initial record.
    if (IsContinuation) {
      if (RecordType != PrevRecordType) {
        Err = createStringError(object_error::parse_failed,
                                "record " + std::to_string(RecordNum) +
                                    " is a continuation record that does not "
                                    "match the type of the previous record");
        return;
      }
      if (!PrevWasContinued) {
        Err = createStringError(object_error::parse_failed,
                                "record " + std::to_string(RecordNum) +
                                    " is a continuation record that is not "
                                    "preceded by a continued record");
        return;
      }
      PrevRecordType = RecordType;
      PrevContinuationBits = I[1] & 0x03;
      continue;
    }
    LLVM_DEBUG(for (size_t J = 0; J < GOFF::RecordLength; ++J) {
      const uint8_t *P = I + J;
      if (J % 8 == 0)
        dbgs() << "  ";
      dbgs() << format("%02hhX", *P);
    });

    switch (RecordType) {
    case GOFF::RT_ESD: {
      // Save ESD record.
      uint32_t EsdId;
      ESDRecord::getEsdId(I, EsdId);
      EsdPtrs.grow(EsdId);
      EsdPtrs[EsdId] = I;

      // Determine and save the "sections" in GOFF.
      // A section is saved as a tuple of the form
      // case (1): (ED,child PR)
      //    - where the PR must have non-zero length.
      // case (2a) (ED,0)
      //   - where the ED is of non-zero length.
      // case (2b) (ED,0)
      //   - where the ED is zero length but
      //     contains a label (LD).
      GOFF::ESDSymbolType SymbolType;
      ESDRecord::getSymbolType(I, SymbolType);
      SectionEntryImpl Section;
      uint32_t Length;
      ESDRecord::getLength(I, Length);
      if (SymbolType == GOFF::ESD_ST_ElementDefinition) {
        // case (2a)
        if (Length != 0) {
          Section.d.a = EsdId;
          SectionList.emplace_back(Section);
        }
      } else if (SymbolType == GOFF::ESD_ST_PartReference) {
        // case (1)
        if (Length != 0) {
          uint32_t SymEdId;
          ESDRecord::getParentEsdId(I, SymEdId);
          Section.d.a = SymEdId;
          Section.d.b = EsdId;
          SectionList.emplace_back(Section);
        }
      } else if (SymbolType == GOFF::ESD_ST_LabelDefinition) {
        // case (2b)
        uint32_t SymEdId;
        ESDRecord::getParentEsdId(I, SymEdId);
        const uint8_t *SymEdRecord = EsdPtrs[SymEdId];
        uint32_t EdLength;
        ESDRecord::getLength(SymEdRecord, EdLength);
        if (!EdLength) { // [ EDID, PRID ]
          // LD child of a zero length parent ED.
          // Add the section ED which was previously ignored.
          Section.d.a = SymEdId;
          SectionList.emplace_back(Section);
        }
      }
      LLVM_DEBUG(dbgs() << "  --  ESD " << EsdId << "\n");
      break;
    }
    case GOFF::RT_TXT:
      // Save TXT records.
      TextPtrs.emplace_back(I);
      LLVM_DEBUG(dbgs() << "  --  TXT\n");
      break;
    case GOFF::RT_END:
      LLVM_DEBUG(dbgs() << "  --  END (GOFF record type) unhandled\n");
      break;
    case GOFF::RT_HDR:
      LLVM_DEBUG(dbgs() << "  --  HDR (GOFF record type) unhandled\n");
      break;
    default:
      llvm_unreachable("Unknown record type");
    }
    PrevRecordType = RecordType;
    PrevContinuationBits = I[1] & 0x03;
  }
}

const uint8_t *GOFFObjectFile::getSymbolEsdRecord(DataRefImpl Symb) const {
  const uint8_t *EsdRecord = EsdPtrs[Symb.d.a];
  return EsdRecord;
}

Expected<StringRef> GOFFObjectFile::getSymbolName(DataRefImpl Symb) const {
  if (EsdNamesCache.count(Symb.d.a)) {
    auto &StrPtr = EsdNamesCache[Symb.d.a];
    return StringRef(StrPtr.second.get(), StrPtr.first);
  }

  SmallString<256> SymbolName;
  if (auto Err = ESDRecord::getData(getSymbolEsdRecord(Symb), SymbolName))
    return std::move(Err);

  SmallString<256> SymbolNameConverted;
  ConverterEBCDIC::convertToUTF8(SymbolName, SymbolNameConverted);

  size_t Size = SymbolNameConverted.size();
  auto StrPtr = std::make_pair(Size, std::make_unique<char[]>(Size));
  char *Buf = StrPtr.second.get();
  memcpy(Buf, SymbolNameConverted.data(), Size);
  EsdNamesCache[Symb.d.a] = std::move(StrPtr);
  return StringRef(Buf, Size);
}

Expected<StringRef> GOFFObjectFile::getSymbolName(SymbolRef Symbol) const {
  return getSymbolName(Symbol.getRawDataRefImpl());
}

Expected<uint64_t> GOFFObjectFile::getSymbolAddress(DataRefImpl Symb) const {
  uint32_t Offset;
  const uint8_t *EsdRecord = getSymbolEsdRecord(Symb);
  ESDRecord::getOffset(EsdRecord, Offset);
  return static_cast<uint64_t>(Offset);
}

uint64_t GOFFObjectFile::getSymbolValueImpl(DataRefImpl Symb) const {
  uint32_t Offset;
  const uint8_t *EsdRecord = getSymbolEsdRecord(Symb);
  ESDRecord::getOffset(EsdRecord, Offset);
  return static_cast<uint64_t>(Offset);
}

uint64_t GOFFObjectFile::getCommonSymbolSizeImpl(DataRefImpl Symb) const {
  return 0;
}

bool GOFFObjectFile::isSymbolUnresolved(DataRefImpl Symb) const {
  const uint8_t *Record = getSymbolEsdRecord(Symb);
  GOFF::ESDSymbolType SymbolType;
  ESDRecord::getSymbolType(Record, SymbolType);

  if (SymbolType == GOFF::ESD_ST_ExternalReference)
    return true;
  if (SymbolType == GOFF::ESD_ST_PartReference) {
    uint32_t Length;
    ESDRecord::getLength(Record, Length);
    if (Length == 0)
      return true;
  }
  return false;
}

bool GOFFObjectFile::isSymbolIndirect(DataRefImpl Symb) const {
  const uint8_t *Record = getSymbolEsdRecord(Symb);
  bool Indirect;
  ESDRecord::getIndirectReference(Record, Indirect);
  return Indirect;
}

Expected<uint32_t> GOFFObjectFile::getSymbolFlags(DataRefImpl Symb) const {
  uint32_t Flags = 0;
  if (isSymbolUnresolved(Symb))
    Flags |= SymbolRef::SF_Undefined;

  const uint8_t *Record = getSymbolEsdRecord(Symb);

  GOFF::ESDBindingStrength BindingStrength;
  ESDRecord::getBindingStrength(Record, BindingStrength);
  if (BindingStrength == GOFF::ESD_BST_Weak)
    Flags |= SymbolRef::SF_Weak;

  GOFF::ESDBindingScope BindingScope;
  ESDRecord::getBindingScope(Record, BindingScope);

  if (BindingScope != GOFF::ESD_BSC_Section) {
    Expected<StringRef> Name = getSymbolName(Symb);
    if (Name && *Name != " ") { // Blank name is local.
      Flags |= SymbolRef::SF_Global;
      if (BindingScope == GOFF::ESD_BSC_ImportExport)
        Flags |= SymbolRef::SF_Exported;
      else if (!(Flags & SymbolRef::SF_Undefined))
        Flags |= SymbolRef::SF_Hidden;
    }
  }

  return Flags;
}

Expected<SymbolRef::Type>
GOFFObjectFile::getSymbolType(DataRefImpl Symb) const {
  const uint8_t *Record = getSymbolEsdRecord(Symb);
  GOFF::ESDSymbolType SymbolType;
  ESDRecord::getSymbolType(Record, SymbolType);
  GOFF::ESDExecutable Executable;
  ESDRecord::getExecutable(Record, Executable);

  if (SymbolType != GOFF::ESD_ST_SectionDefinition &&
      SymbolType != GOFF::ESD_ST_ElementDefinition &&
      SymbolType != GOFF::ESD_ST_LabelDefinition &&
      SymbolType != GOFF::ESD_ST_PartReference &&
      SymbolType != GOFF::ESD_ST_ExternalReference) {
    uint32_t EsdId;
    ESDRecord::getEsdId(Record, EsdId);
    return createStringError(llvm::errc::invalid_argument,
                             "ESD record %" PRIu32
                             " has invalid symbol type 0x%02" PRIX8,
                             EsdId, SymbolType);
  }
  switch (SymbolType) {
  case GOFF::ESD_ST_SectionDefinition:
  case GOFF::ESD_ST_ElementDefinition:
    return SymbolRef::ST_Other;
  case GOFF::ESD_ST_LabelDefinition:
  case GOFF::ESD_ST_PartReference:
  case GOFF::ESD_ST_ExternalReference:
    if (Executable != GOFF::ESD_EXE_CODE && Executable != GOFF::ESD_EXE_DATA &&
        Executable != GOFF::ESD_EXE_Unspecified) {
      uint32_t EsdId;
      ESDRecord::getEsdId(Record, EsdId);
      return createStringError(llvm::errc::invalid_argument,
                               "ESD record %" PRIu32
                               " has unknown Executable type 0x%02X",
                               EsdId, Executable);
    }
    switch (Executable) {
    case GOFF::ESD_EXE_CODE:
      return SymbolRef::ST_Function;
    case GOFF::ESD_EXE_DATA:
      return SymbolRef::ST_Data;
    case GOFF::ESD_EXE_Unspecified:
      return SymbolRef::ST_Unknown;
    }
    llvm_unreachable("Unhandled ESDExecutable");
  }
  llvm_unreachable("Unhandled ESDSymbolType");
}

Expected<section_iterator>
GOFFObjectFile::getSymbolSection(DataRefImpl Symb) const {
  DataRefImpl Sec;

  if (isSymbolUnresolved(Symb))
    return section_iterator(SectionRef(Sec, this));

  const uint8_t *SymEsdRecord = EsdPtrs[Symb.d.a];
  uint32_t SymEdId;
  ESDRecord::getParentEsdId(SymEsdRecord, SymEdId);
  const uint8_t *SymEdRecord = EsdPtrs[SymEdId];

  for (size_t I = 0, E = SectionList.size(); I < E; ++I) {
    bool Found;
    const uint8_t *SectionPrRecord = getSectionPrEsdRecord(I);
    if (SectionPrRecord) {
      Found = SymEsdRecord == SectionPrRecord;
    } else {
      const uint8_t *SectionEdRecord = getSectionEdEsdRecord(I);
      Found = SymEdRecord == SectionEdRecord;
    }

    if (Found) {
      Sec.d.a = I;
      return section_iterator(SectionRef(Sec, this));
    }
  }
  return createStringError(llvm::errc::invalid_argument,
                           "symbol with ESD id " + std::to_string(Symb.d.a) +
                               " refers to invalid section with ESD id " +
                               std::to_string(SymEdId));
}

uint64_t GOFFObjectFile::getSymbolSize(DataRefImpl Symb) const {
  const uint8_t *Record = getSymbolEsdRecord(Symb);
  uint32_t Length;
  ESDRecord::getLength(Record, Length);
  return Length;
}

const uint8_t *GOFFObjectFile::getSectionEdEsdRecord(DataRefImpl &Sec) const {
  SectionEntryImpl EsdIds = SectionList[Sec.d.a];
  const uint8_t *EsdRecord = EsdPtrs[EsdIds.d.a];
  return EsdRecord;
}

const uint8_t *GOFFObjectFile::getSectionPrEsdRecord(DataRefImpl &Sec) const {
  SectionEntryImpl EsdIds = SectionList[Sec.d.a];
  const uint8_t *EsdRecord = nullptr;
  if (EsdIds.d.b)
    EsdRecord = EsdPtrs[EsdIds.d.b];
  return EsdRecord;
}

const uint8_t *
GOFFObjectFile::getSectionEdEsdRecord(uint32_t SectionIndex) const {
  DataRefImpl Sec;
  Sec.d.a = SectionIndex;
  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  return EsdRecord;
}

const uint8_t *
GOFFObjectFile::getSectionPrEsdRecord(uint32_t SectionIndex) const {
  DataRefImpl Sec;
  Sec.d.a = SectionIndex;
  const uint8_t *EsdRecord = getSectionPrEsdRecord(Sec);
  return EsdRecord;
}

uint32_t GOFFObjectFile::getSectionDefEsdId(DataRefImpl &Sec) const {
  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  uint32_t Length;
  ESDRecord::getLength(EsdRecord, Length);
  if (Length == 0) {
    const uint8_t *PrEsdRecord = getSectionPrEsdRecord(Sec);
    if (PrEsdRecord)
      EsdRecord = PrEsdRecord;
  }

  uint32_t DefEsdId;
  ESDRecord::getEsdId(EsdRecord, DefEsdId);
  LLVM_DEBUG(dbgs() << "Got def EsdId: " << DefEsdId << '\n');
  return DefEsdId;
}

void GOFFObjectFile::moveSectionNext(DataRefImpl &Sec) const {
  Sec.d.a++;
  if ((Sec.d.a) >= SectionList.size())
    Sec.d.a = 0;
}

Expected<StringRef> GOFFObjectFile::getSectionName(DataRefImpl Sec) const {
  DataRefImpl EdSym;
  SectionEntryImpl EsdIds = SectionList[Sec.d.a];
  EdSym.d.a = EsdIds.d.a;
  Expected<StringRef> Name = getSymbolName(EdSym);
  if (Name) {
    StringRef Res = *Name;
    LLVM_DEBUG(dbgs() << "Got section: " << Res << '\n');
    LLVM_DEBUG(dbgs() << "Final section name: " << Res << '\n');
    Name = Res;
  }
  return Name;
}

uint64_t GOFFObjectFile::getSectionAddress(DataRefImpl Sec) const {
  uint32_t Offset;
  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  ESDRecord::getOffset(EsdRecord, Offset);
  return Offset;
}

uint64_t GOFFObjectFile::getSectionSize(DataRefImpl Sec) const {
  uint32_t Length;
  uint32_t DefEsdId = getSectionDefEsdId(Sec);
  const uint8_t *EsdRecord = EsdPtrs[DefEsdId];
  ESDRecord::getLength(EsdRecord, Length);
  LLVM_DEBUG(dbgs() << "Got section size: " << Length << '\n');
  return static_cast<uint64_t>(Length);
}

// Unravel TXT records and expand fill characters to produce
// a contiguous sequence of bytes.
Expected<ArrayRef<uint8_t>>
GOFFObjectFile::getSectionContents(DataRefImpl Sec) const {
  if (SectionDataCache.count(Sec.d.a)) {
    auto &Buf = SectionDataCache[Sec.d.a];
    return ArrayRef<uint8_t>(Buf);
  }
  uint64_t SectionSize = getSectionSize(Sec);
  uint32_t DefEsdId = getSectionDefEsdId(Sec);

  const uint8_t *EdEsdRecord = getSectionEdEsdRecord(Sec);
  bool FillBytePresent;
  ESDRecord::getFillBytePresent(EdEsdRecord, FillBytePresent);
  uint8_t FillByte = '\0';
  if (FillBytePresent)
    ESDRecord::getFillByteValue(EdEsdRecord, FillByte);

  // Initialize section with fill byte.
  SmallVector<uint8_t> Data(SectionSize, FillByte);

  // Replace section with content from text records.
  for (const uint8_t *TxtRecordInt : TextPtrs) {
    const uint8_t *TxtRecordPtr = TxtRecordInt;
    uint32_t TxtEsdId;
    TXTRecord::getElementEsdId(TxtRecordPtr, TxtEsdId);
    LLVM_DEBUG(dbgs() << "Got txt EsdId: " << TxtEsdId << '\n');

    if (TxtEsdId != DefEsdId)
      continue;

    uint32_t TxtDataOffset;
    TXTRecord::getOffset(TxtRecordPtr, TxtDataOffset);

    uint16_t TxtDataSize;
    TXTRecord::getDataLength(TxtRecordPtr, TxtDataSize);

    LLVM_DEBUG(dbgs() << "Record offset " << TxtDataOffset << ", data size "
                      << TxtDataSize << "\n");

    SmallString<256> CompleteData;
    CompleteData.reserve(TxtDataSize);
    if (Error Err = TXTRecord::getData(TxtRecordPtr, CompleteData))
      return std::move(Err);
    assert(CompleteData.size() == TxtDataSize && "Wrong length of data");
    std::copy(CompleteData.data(), CompleteData.data() + TxtDataSize,
              Data.begin() + TxtDataOffset);
  }
  SectionDataCache[Sec.d.a] = Data;
  return ArrayRef<uint8_t>(SectionDataCache[Sec.d.a]);
}

uint64_t GOFFObjectFile::getSectionAlignment(DataRefImpl Sec) const {
  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDAlignment Pow2Alignment;
  ESDRecord::getAlignment(EsdRecord, Pow2Alignment);
  return 1ULL << static_cast<uint64_t>(Pow2Alignment);
}

bool GOFFObjectFile::isSectionText(DataRefImpl Sec) const {
  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDExecutable Executable;
  ESDRecord::getExecutable(EsdRecord, Executable);
  return Executable == GOFF::ESD_EXE_CODE;
}

bool GOFFObjectFile::isSectionData(DataRefImpl Sec) const {
  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDExecutable Executable;
  ESDRecord::getExecutable(EsdRecord, Executable);
  return Executable == GOFF::ESD_EXE_DATA;
}

bool GOFFObjectFile::isSectionNoLoad(DataRefImpl Sec) const {
  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDLoadingBehavior LoadingBehavior;
  ESDRecord::getLoadingBehavior(EsdRecord, LoadingBehavior);
  return LoadingBehavior == GOFF::ESD_LB_NoLoad;
}

bool GOFFObjectFile::isSectionReadOnlyData(DataRefImpl Sec) const {
  if (!isSectionData(Sec))
    return false;

  const uint8_t *EsdRecord = getSectionEdEsdRecord(Sec);
  GOFF::ESDLoadingBehavior LoadingBehavior;
  ESDRecord::getLoadingBehavior(EsdRecord, LoadingBehavior);
  return LoadingBehavior == GOFF::ESD_LB_Initial;
}

bool GOFFObjectFile::isSectionZeroInit(DataRefImpl Sec) const {
  // GOFF uses fill characters and fill characters are applied
  // on getSectionContents() - so we say false to zero init.
  return false;
}

section_iterator GOFFObjectFile::section_begin() const {
  DataRefImpl Sec;
  moveSectionNext(Sec);
  return section_iterator(SectionRef(Sec, this));
}

section_iterator GOFFObjectFile::section_end() const {
  DataRefImpl Sec;
  return section_iterator(SectionRef(Sec, this));
}

void GOFFObjectFile::moveSymbolNext(DataRefImpl &Symb) const {
  for (uint32_t I = Symb.d.a + 1, E = EsdPtrs.size(); I < E; ++I) {
    if (EsdPtrs[I]) {
      const uint8_t *EsdRecord = EsdPtrs[I];
      GOFF::ESDSymbolType SymbolType;
      ESDRecord::getSymbolType(EsdRecord, SymbolType);
      // Skip EDs - i.e. section symbols.
      bool IgnoreSpecialGOFFSymbols = true;
      bool SkipSymbol = ((SymbolType == GOFF::ESD_ST_ElementDefinition) ||
                         (SymbolType == GOFF::ESD_ST_SectionDefinition)) &&
                        IgnoreSpecialGOFFSymbols;
      if (!SkipSymbol) {
        Symb.d.a = I;
        return;
      }
    }
  }
  Symb.d.a = 0;
}

basic_symbol_iterator GOFFObjectFile::symbol_begin() const {
  DataRefImpl Symb;
  moveSymbolNext(Symb);
  return basic_symbol_iterator(SymbolRef(Symb, this));
}

basic_symbol_iterator GOFFObjectFile::symbol_end() const {
  DataRefImpl Symb;
  return basic_symbol_iterator(SymbolRef(Symb, this));
}

Error Record::getContinuousData(const uint8_t *Record, uint16_t DataLength,
                                int DataIndex, SmallString<256> &CompleteData) {
  // First record.
  const uint8_t *Slice = Record + DataIndex;
  size_t SliceLength =
      std::min(DataLength, (uint16_t)(GOFF::RecordLength - DataIndex));
  CompleteData.append(Slice, Slice + SliceLength);
  DataLength -= SliceLength;
  Slice += SliceLength;

  // Continuation records.
  for (; DataLength > 0;
       DataLength -= SliceLength, Slice += GOFF::PayloadLength) {
    // Slice points to the start of the new record.
    // Check that this block is a Continuation.
    assert(Record::isContinuation(Slice) && "Continuation bit must be set");
    // Check that the last Continuation is terminated correctly.
    if (DataLength <= 77 && Record::isContinued(Slice))
      return createStringError(object_error::parse_failed,
                               "continued bit should not be set");

    SliceLength = std::min(DataLength, (uint16_t)GOFF::PayloadLength);
    Slice += GOFF::RecordPrefixLength;
    CompleteData.append(Slice, Slice + SliceLength);
  }
  return Error::success();
}

Error HDRRecord::getData(const uint8_t *Record,
                         SmallString<256> &CompleteData) {
  uint16_t Length = getPropertyModuleLength(Record);
  return getContinuousData(Record, Length, 60, CompleteData);
}

Error ESDRecord::getData(const uint8_t *Record,
                         SmallString<256> &CompleteData) {
  uint16_t DataSize = getNameLength(Record);
  return getContinuousData(Record, DataSize, 72, CompleteData);
}

Error TXTRecord::getData(const uint8_t *Record,
                         SmallString<256> &CompleteData) {
  uint16_t Length;
  getDataLength(Record, Length);
  return getContinuousData(Record, Length, 24, CompleteData);
}

Error ENDRecord::getData(const uint8_t *Record,
                         SmallString<256> &CompleteData) {
  uint16_t Length = getNameLength(Record);
  return getContinuousData(Record, Length, 26, CompleteData);
}
