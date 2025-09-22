//===- XCOFFObjectFile.h - XCOFF object file implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the XCOFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_XCOFFOBJECTFILE_H
#define LLVM_OBJECT_XCOFFOBJECTFILE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include <limits>

namespace llvm {
namespace object {

class xcoff_symbol_iterator;

struct XCOFFFileHeader32 {
  support::ubig16_t Magic;
  support::ubig16_t NumberOfSections;

  // Unix time value, value of 0 indicates no timestamp.
  // Negative values are reserved.
  support::big32_t TimeStamp;

  support::ubig32_t SymbolTableOffset; // File offset to symbol table.
  support::big32_t NumberOfSymTableEntries;
  support::ubig16_t AuxHeaderSize;
  support::ubig16_t Flags;
};

struct XCOFFFileHeader64 {
  support::ubig16_t Magic;
  support::ubig16_t NumberOfSections;

  // Unix time value, value of 0 indicates no timestamp.
  // Negative values are reserved.
  support::big32_t TimeStamp;

  support::ubig64_t SymbolTableOffset; // File offset to symbol table.
  support::ubig16_t AuxHeaderSize;
  support::ubig16_t Flags;
  support::ubig32_t NumberOfSymTableEntries;
};

template <typename T> struct XCOFFAuxiliaryHeader {
  static constexpr uint8_t AuxiHeaderFlagMask = 0xF0;
  static constexpr uint8_t AuxiHeaderTDataAlignmentMask = 0x0F;

public:
  uint8_t getFlag() const {
    return static_cast<const T *>(this)->FlagAndTDataAlignment &
           AuxiHeaderFlagMask;
  }

  uint8_t getTDataAlignment() const {
    return static_cast<const T *>(this)->FlagAndTDataAlignment &
           AuxiHeaderTDataAlignmentMask;
  }

  uint16_t getVersion() const { return static_cast<const T *>(this)->Version; }
  uint64_t getEntryPointAddr() const {
    return static_cast<const T *>(this)->EntryPointAddr;
  }
};

struct XCOFFAuxiliaryHeader32 : XCOFFAuxiliaryHeader<XCOFFAuxiliaryHeader32> {
  support::ubig16_t
      AuxMagic; ///< If the value of the o_vstamp field is greater than 1, the
                ///< o_mflags field is reserved for future use and it should
                ///< contain 0. Otherwise, this field is not used.
  support::ubig16_t
      Version; ///< The valid values are 1 and 2. When the o_vstamp field is 2
               ///< in an XCOFF32 file, the new interpretation of the n_type
               ///< field in the symbol table entry is used.
  support::ubig32_t TextSize;
  support::ubig32_t InitDataSize;
  support::ubig32_t BssDataSize;
  support::ubig32_t EntryPointAddr;
  support::ubig32_t TextStartAddr;
  support::ubig32_t DataStartAddr;
  support::ubig32_t TOCAnchorAddr;
  support::ubig16_t SecNumOfEntryPoint;
  support::ubig16_t SecNumOfText;
  support::ubig16_t SecNumOfData;
  support::ubig16_t SecNumOfTOC;
  support::ubig16_t SecNumOfLoader;
  support::ubig16_t SecNumOfBSS;
  support::ubig16_t MaxAlignOfText;
  support::ubig16_t MaxAlignOfData;
  support::ubig16_t ModuleType;
  uint8_t CpuFlag;
  uint8_t CpuType;
  support::ubig32_t MaxStackSize; ///< If the value is 0, the system default
                                  ///< maximum stack size is used.
  support::ubig32_t MaxDataSize;  ///< If the value is 0, the system default
                                  ///< maximum data size is used.
  support::ubig32_t
      ReservedForDebugger; ///< This field should contain 0. When a loaded
                           ///< program is being debugged, the memory image of
                           ///< this field may be modified by a debugger to
                           ///< insert a trap instruction.
  uint8_t TextPageSize;  ///< Specifies the size of pages for the exec text. The
                         ///< default value is 0 (system-selected page size).
  uint8_t DataPageSize;  ///< Specifies the size of pages for the exec data. The
                         ///< default value is 0 (system-selected page size).
  uint8_t StackPageSize; ///< Specifies the size of pages for the stack. The
                         ///< default value is 0 (system-selected page size).
  uint8_t FlagAndTDataAlignment;
  support::ubig16_t SecNumOfTData;
  support::ubig16_t SecNumOfTBSS;
};

struct XCOFFAuxiliaryHeader64 : XCOFFAuxiliaryHeader<XCOFFAuxiliaryHeader64> {
  support::ubig16_t AuxMagic;
  support::ubig16_t Version;
  support::ubig32_t ReservedForDebugger;
  support::ubig64_t TextStartAddr;
  support::ubig64_t DataStartAddr;
  support::ubig64_t TOCAnchorAddr;
  support::ubig16_t SecNumOfEntryPoint;
  support::ubig16_t SecNumOfText;
  support::ubig16_t SecNumOfData;
  support::ubig16_t SecNumOfTOC;
  support::ubig16_t SecNumOfLoader;
  support::ubig16_t SecNumOfBSS;
  support::ubig16_t MaxAlignOfText;
  support::ubig16_t MaxAlignOfData;
  support::ubig16_t ModuleType;
  uint8_t CpuFlag;
  uint8_t CpuType;
  uint8_t TextPageSize;
  uint8_t DataPageSize;
  uint8_t StackPageSize;
  uint8_t FlagAndTDataAlignment;
  support::ubig64_t TextSize;
  support::ubig64_t InitDataSize;
  support::ubig64_t BssDataSize;
  support::ubig64_t EntryPointAddr;
  support::ubig64_t MaxStackSize;
  support::ubig64_t MaxDataSize;
  support::ubig16_t SecNumOfTData;
  support::ubig16_t SecNumOfTBSS;
  support::ubig16_t XCOFF64Flag;
};

template <typename T> struct XCOFFSectionHeader {
  // The section flags definitions are the same in both 32- and 64-bit objects.
  //  Least significant 3 bits are reserved.
  static constexpr unsigned SectionFlagsReservedMask = 0x7;

  // The low order 16 bits of section flags denotes the section type.
  // The high order 16 bits of section flags denotes the section subtype.
  // For now, this is only used for DWARF sections.
  static constexpr unsigned SectionFlagsTypeMask = 0xffffu;

public:
  StringRef getName() const;
  uint16_t getSectionType() const;
  uint32_t getSectionSubtype() const;
  bool isReservedSectionType() const;
};

// Explicit extern template declarations.
struct XCOFFSectionHeader32;
struct XCOFFSectionHeader64;
extern template struct XCOFFSectionHeader<XCOFFSectionHeader32>;
extern template struct XCOFFSectionHeader<XCOFFSectionHeader64>;

struct XCOFFSectionHeader32 : XCOFFSectionHeader<XCOFFSectionHeader32> {
  char Name[XCOFF::NameSize];
  support::ubig32_t PhysicalAddress;
  support::ubig32_t VirtualAddress;
  support::ubig32_t SectionSize;
  support::ubig32_t FileOffsetToRawData;
  support::ubig32_t FileOffsetToRelocationInfo;
  support::ubig32_t FileOffsetToLineNumberInfo;
  support::ubig16_t NumberOfRelocations;
  support::ubig16_t NumberOfLineNumbers;
  support::big32_t Flags;
};

struct XCOFFSectionHeader64 : XCOFFSectionHeader<XCOFFSectionHeader64> {
  char Name[XCOFF::NameSize];
  support::ubig64_t PhysicalAddress;
  support::ubig64_t VirtualAddress;
  support::ubig64_t SectionSize;
  support::big64_t FileOffsetToRawData;
  support::big64_t FileOffsetToRelocationInfo;
  support::big64_t FileOffsetToLineNumberInfo;
  support::ubig32_t NumberOfRelocations;
  support::ubig32_t NumberOfLineNumbers;
  support::big32_t Flags;
  char Padding[4];
};

struct LoaderSectionHeader32;
struct LoaderSectionHeader64;
struct LoaderSectionSymbolEntry32 {
  struct NameOffsetInStrTbl {
    support::big32_t IsNameInStrTbl; // Zero indicates name in string table.
    support::ubig32_t Offset;
  };

  char SymbolName[XCOFF::NameSize];
  support::ubig32_t Value; // The virtual address of the symbol.
  support::big16_t SectionNumber;
  uint8_t SymbolType;
  XCOFF::StorageClass StorageClass;
  support::ubig32_t ImportFileID;
  support::ubig32_t ParameterTypeCheck;

  Expected<StringRef>
  getSymbolName(const LoaderSectionHeader32 *LoaderSecHeader) const;
};

struct LoaderSectionSymbolEntry64 {
  support::ubig64_t Value; // The virtual address of the symbol.
  support::ubig32_t Offset;
  support::big16_t SectionNumber;
  uint8_t SymbolType;
  XCOFF::StorageClass StorageClass;
  support::ubig32_t ImportFileID;
  support::ubig32_t ParameterTypeCheck;

  Expected<StringRef>
  getSymbolName(const LoaderSectionHeader64 *LoaderSecHeader) const;
};

struct LoaderSectionRelocationEntry32 {
  support::ubig32_t VirtualAddr;
  support::big32_t SymbolIndex;
  support::ubig16_t Type;
  support::big16_t SectionNum;
};

struct LoaderSectionRelocationEntry64 {
  support::ubig64_t VirtualAddr;
  support::ubig16_t Type;
  support::big16_t SectionNum;
  support::big32_t SymbolIndex;
};

struct LoaderSectionHeader32 {
  support::ubig32_t Version;
  support::ubig32_t NumberOfSymTabEnt;
  support::ubig32_t NumberOfRelTabEnt;
  support::ubig32_t LengthOfImpidStrTbl;
  support::ubig32_t NumberOfImpid;
  support::big32_t OffsetToImpid;
  support::ubig32_t LengthOfStrTbl;
  support::big32_t OffsetToStrTbl;

  uint64_t getOffsetToSymTbl() const {
    return NumberOfSymTabEnt == 0 ? 0 : sizeof(LoaderSectionHeader32);
  }

  uint64_t getOffsetToRelEnt() const {
    // Relocation table is after Symbol table.
    return NumberOfRelTabEnt == 0
               ? 0
               : sizeof(LoaderSectionHeader32) +
                     sizeof(LoaderSectionSymbolEntry32) * NumberOfSymTabEnt;
  }
};

struct LoaderSectionHeader64 {
  support::ubig32_t Version;
  support::ubig32_t NumberOfSymTabEnt;
  support::ubig32_t NumberOfRelTabEnt;
  support::ubig32_t LengthOfImpidStrTbl;
  support::ubig32_t NumberOfImpid;
  support::ubig32_t LengthOfStrTbl;
  support::big64_t OffsetToImpid;
  support::big64_t OffsetToStrTbl;
  support::big64_t OffsetToSymTbl;
  support::big64_t OffsetToRelEnt;

  uint64_t getOffsetToSymTbl() const { return OffsetToSymTbl; }
  uint64_t getOffsetToRelEnt() const { return OffsetToRelEnt; }
};

template <typename AddressType> struct ExceptionSectionEntry {
  union {
    support::ubig32_t SymbolIdx;
    AddressType TrapInstAddr;
  };
  uint8_t LangId;
  uint8_t Reason;

  uint32_t getSymbolIndex() const {
    assert(Reason == 0 && "Get symbol table index of the function only when "
                          "the e_reason field is 0.");
    return SymbolIdx;
  }

  uint64_t getTrapInstAddr() const {
    assert(Reason != 0 && "Zero is not a valid trap exception reason code.");
    return TrapInstAddr;
  }
  uint8_t getLangID() const { return LangId; }
  uint8_t getReason() const { return Reason; }
};

typedef ExceptionSectionEntry<support::ubig32_t> ExceptionSectionEntry32;
typedef ExceptionSectionEntry<support::ubig64_t> ExceptionSectionEntry64;

// Explicit extern template declarations.
extern template struct ExceptionSectionEntry<support::ubig32_t>;
extern template struct ExceptionSectionEntry<support::ubig64_t>;

struct XCOFFStringTable {
  uint32_t Size;
  const char *Data;
};

struct XCOFFCsectAuxEnt32 {
  support::ubig32_t SectionOrLength;
  support::ubig32_t ParameterHashIndex;
  support::ubig16_t TypeChkSectNum;
  uint8_t SymbolAlignmentAndType;
  XCOFF::StorageMappingClass StorageMappingClass;
  support::ubig32_t StabInfoIndex;
  support::ubig16_t StabSectNum;
};

struct XCOFFCsectAuxEnt64 {
  support::ubig32_t SectionOrLengthLowByte;
  support::ubig32_t ParameterHashIndex;
  support::ubig16_t TypeChkSectNum;
  uint8_t SymbolAlignmentAndType;
  XCOFF::StorageMappingClass StorageMappingClass;
  support::ubig32_t SectionOrLengthHighByte;
  uint8_t Pad;
  XCOFF::SymbolAuxType AuxType;
};

class XCOFFCsectAuxRef {
public:
  static constexpr uint8_t SymbolTypeMask = 0x07;
  static constexpr uint8_t SymbolAlignmentMask = 0xF8;
  static constexpr size_t SymbolAlignmentBitOffset = 3;

  XCOFFCsectAuxRef(const XCOFFCsectAuxEnt32 *Entry32) : Entry32(Entry32) {}
  XCOFFCsectAuxRef(const XCOFFCsectAuxEnt64 *Entry64) : Entry64(Entry64) {}

  // For getSectionOrLength(),
  // If the symbol type is XTY_SD or XTY_CM, the csect length.
  // If the symbol type is XTY_LD, the symbol table
  // index of the containing csect.
  // If the symbol type is XTY_ER, 0.
  uint64_t getSectionOrLength() const {
    return Entry32 ? getSectionOrLength32() : getSectionOrLength64();
  }

  uint32_t getSectionOrLength32() const {
    assert(Entry32 && "32-bit interface called on 64-bit object file.");
    return Entry32->SectionOrLength;
  }

  uint64_t getSectionOrLength64() const {
    assert(Entry64 && "64-bit interface called on 32-bit object file.");
    return (static_cast<uint64_t>(Entry64->SectionOrLengthHighByte) << 32) |
           Entry64->SectionOrLengthLowByte;
  }

#define GETVALUE(X) Entry32 ? Entry32->X : Entry64->X

  uint32_t getParameterHashIndex() const {
    return GETVALUE(ParameterHashIndex);
  }

  uint16_t getTypeChkSectNum() const { return GETVALUE(TypeChkSectNum); }

  XCOFF::StorageMappingClass getStorageMappingClass() const {
    return GETVALUE(StorageMappingClass);
  }

  uintptr_t getEntryAddress() const {
    return Entry32 ? reinterpret_cast<uintptr_t>(Entry32)
                   : reinterpret_cast<uintptr_t>(Entry64);
  }

  uint16_t getAlignmentLog2() const {
    return (getSymbolAlignmentAndType() & SymbolAlignmentMask) >>
           SymbolAlignmentBitOffset;
  }

  uint8_t getSymbolType() const {
    return getSymbolAlignmentAndType() & SymbolTypeMask;
  }

  bool isLabel() const { return getSymbolType() == XCOFF::XTY_LD; }

  uint32_t getStabInfoIndex32() const {
    assert(Entry32 && "32-bit interface called on 64-bit object file.");
    return Entry32->StabInfoIndex;
  }

  uint16_t getStabSectNum32() const {
    assert(Entry32 && "32-bit interface called on 64-bit object file.");
    return Entry32->StabSectNum;
  }

  XCOFF::SymbolAuxType getAuxType64() const {
    assert(Entry64 && "64-bit interface called on 32-bit object file.");
    return Entry64->AuxType;
  }

  uint8_t getSymbolAlignmentAndType() const {
    return GETVALUE(SymbolAlignmentAndType);
  }

#undef GETVALUE

private:
  const XCOFFCsectAuxEnt32 *Entry32 = nullptr;
  const XCOFFCsectAuxEnt64 *Entry64 = nullptr;
};

struct XCOFFFileAuxEnt {
  typedef struct {
    support::big32_t Magic; // Zero indicates name in string table.
    support::ubig32_t Offset;
    char NamePad[XCOFF::FileNamePadSize];
  } NameInStrTblType;
  union {
    char Name[XCOFF::NameSize + XCOFF::FileNamePadSize];
    NameInStrTblType NameInStrTbl;
  };
  XCOFF::CFileStringType Type;
  uint8_t ReservedZeros[2];
  XCOFF::SymbolAuxType AuxType; // 64-bit XCOFF file only.
};

struct XCOFFSectAuxEntForStat {
  support::ubig32_t SectionLength;
  support::ubig16_t NumberOfRelocEnt;
  support::ubig16_t NumberOfLineNum;
  uint8_t Pad[10];
}; // 32-bit XCOFF file only.

struct XCOFFFunctionAuxEnt32 {
  support::ubig32_t OffsetToExceptionTbl;
  support::ubig32_t SizeOfFunction;
  support::ubig32_t PtrToLineNum;
  support::big32_t SymIdxOfNextBeyond;
  uint8_t Pad[2];
};

struct XCOFFFunctionAuxEnt64 {
  support::ubig64_t PtrToLineNum;
  support::ubig32_t SizeOfFunction;
  support::big32_t SymIdxOfNextBeyond;
  uint8_t Pad;
  XCOFF::SymbolAuxType AuxType; // Contains _AUX_FCN; Type of auxiliary entry
};

struct XCOFFExceptionAuxEnt {
  support::ubig64_t OffsetToExceptionTbl;
  support::ubig32_t SizeOfFunction;
  support::big32_t SymIdxOfNextBeyond;
  uint8_t Pad;
  XCOFF::SymbolAuxType AuxType; // Contains _AUX_EXCEPT; Type of auxiliary entry
};

struct XCOFFBlockAuxEnt32 {
  uint8_t ReservedZeros1[2];
  support::ubig16_t LineNumHi;
  support::ubig16_t LineNumLo;
  uint8_t ReservedZeros2[12];
};

struct XCOFFBlockAuxEnt64 {
  support::ubig32_t LineNum;
  uint8_t Pad[13];
  XCOFF::SymbolAuxType AuxType; // Contains _AUX_SYM; Type of auxiliary entry
};

struct XCOFFSectAuxEntForDWARF32 {
  support::ubig32_t LengthOfSectionPortion;
  uint8_t Pad1[4];
  support::ubig32_t NumberOfRelocEnt;
  uint8_t Pad2[6];
};

struct XCOFFSectAuxEntForDWARF64 {
  support::ubig64_t LengthOfSectionPortion;
  support::ubig64_t NumberOfRelocEnt;
  uint8_t Pad;
  XCOFF::SymbolAuxType AuxType; // Contains _AUX_SECT; Type of Auxillary entry
};

template <typename AddressType> struct XCOFFRelocation {
public:
  AddressType VirtualAddress;
  support::ubig32_t SymbolIndex;

  // Packed field, see XR_* masks for details of packing.
  uint8_t Info;

  XCOFF::RelocationType Type;

public:
  bool isRelocationSigned() const;
  bool isFixupIndicated() const;

  // Returns the number of bits being relocated.
  uint8_t getRelocatedLength() const;
};

extern template struct XCOFFRelocation<llvm::support::ubig32_t>;
extern template struct XCOFFRelocation<llvm::support::ubig64_t>;

struct XCOFFRelocation32 : XCOFFRelocation<llvm::support::ubig32_t> {};
struct XCOFFRelocation64 : XCOFFRelocation<llvm::support::ubig64_t> {};

class XCOFFSymbolRef;

class XCOFFObjectFile : public ObjectFile {
private:
  const void *FileHeader = nullptr;
  const void *AuxiliaryHeader = nullptr;
  const void *SectionHeaderTable = nullptr;

  const void *SymbolTblPtr = nullptr;
  XCOFFStringTable StringTable = {0, nullptr};

  const XCOFFSectionHeader32 *sectionHeaderTable32() const;
  const XCOFFSectionHeader64 *sectionHeaderTable64() const;
  template <typename T> const T *sectionHeaderTable() const;

  size_t getFileHeaderSize() const;
  size_t getSectionHeaderSize() const;

  const XCOFFSectionHeader32 *toSection32(DataRefImpl Ref) const;
  const XCOFFSectionHeader64 *toSection64(DataRefImpl Ref) const;
  uintptr_t getSectionHeaderTableAddress() const;
  uintptr_t getEndOfSymbolTableAddress() const;

  DataRefImpl getSectionByType(XCOFF::SectionTypeFlags SectType) const;
  uint64_t getSectionFileOffsetToRawData(DataRefImpl Sec) const;

  // This returns a pointer to the start of the storage for the name field of
  // the 32-bit or 64-bit SectionHeader struct. This string is *not* necessarily
  // null-terminated.
  const char *getSectionNameInternal(DataRefImpl Sec) const;

  static bool isReservedSectionNumber(int16_t SectionNumber);

  // Constructor and "create" factory function. The constructor is only a thin
  // wrapper around the base constructor. The "create" function fills out the
  // XCOFF-specific information and performs the error checking along the way.
  XCOFFObjectFile(unsigned Type, MemoryBufferRef Object);
  static Expected<std::unique_ptr<XCOFFObjectFile>> create(unsigned Type,
                                                           MemoryBufferRef MBR);

  // Helper for parsing the StringTable. Returns an 'Error' if parsing failed
  // and an XCOFFStringTable if parsing succeeded.
  static Expected<XCOFFStringTable> parseStringTable(const XCOFFObjectFile *Obj,
                                                     uint64_t Offset);

  // Make a friend so it can call the private 'create' function.
  friend Expected<std::unique_ptr<ObjectFile>>
  ObjectFile::createXCOFFObjectFile(MemoryBufferRef Object, unsigned FileType);

  void checkSectionAddress(uintptr_t Addr, uintptr_t TableAddr) const;

public:
  static constexpr uint64_t InvalidRelocOffset =
      std::numeric_limits<uint64_t>::max();

  // Interface inherited from base classes.
  void moveSymbolNext(DataRefImpl &Symb) const override;
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  basic_symbol_iterator symbol_begin() const override;
  basic_symbol_iterator symbol_end() const override;

  using xcoff_symbol_iterator_range = iterator_range<xcoff_symbol_iterator>;
  xcoff_symbol_iterator_range symbols() const;

  bool is64Bit() const override;
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;

  void moveSectionNext(DataRefImpl &Sec) const override;
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  bool isSectionCompressed(DataRefImpl Sec) const override;
  bool isSectionText(DataRefImpl Sec) const override;
  bool isSectionData(DataRefImpl Sec) const override;
  bool isSectionBSS(DataRefImpl Sec) const override;
  bool isDebugSection(DataRefImpl Sec) const override;

  bool isSectionVirtual(DataRefImpl Sec) const override;
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  void moveRelocationNext(DataRefImpl &Rel) const override;

  /// \returns the relocation offset with the base address of the containing
  /// section as zero, or InvalidRelocOffset on errors (such as a relocation
  /// that does not refer to an address in any section).
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

  section_iterator section_begin() const override;
  section_iterator section_end() const override;
  uint8_t getBytesInAddress() const override;
  StringRef getFileFormatName() const override;
  Triple::ArchType getArch() const override;
  Expected<SubtargetFeatures> getFeatures() const override;
  Expected<uint64_t> getStartAddress() const override;
  StringRef mapDebugSectionName(StringRef Name) const override;
  bool isRelocatableObject() const override;

  // Below here is the non-inherited interface.

  Expected<StringRef> getRawData(const char *Start, uint64_t Size,
                                 StringRef Name) const;

  const XCOFFAuxiliaryHeader32 *auxiliaryHeader32() const;
  const XCOFFAuxiliaryHeader64 *auxiliaryHeader64() const;

  const void *getPointerToSymbolTable() const { return SymbolTblPtr; }

  Expected<StringRef> getSymbolSectionName(XCOFFSymbolRef Ref) const;
  unsigned getSymbolSectionID(SymbolRef Sym) const;
  XCOFFSymbolRef toSymbolRef(DataRefImpl Ref) const;

  // File header related interfaces.
  const XCOFFFileHeader32 *fileHeader32() const;
  const XCOFFFileHeader64 *fileHeader64() const;
  uint16_t getMagic() const;
  uint16_t getNumberOfSections() const;
  int32_t getTimeStamp() const;

  // Symbol table offset and entry count are handled differently between
  // XCOFF32 and XCOFF64.
  uint32_t getSymbolTableOffset32() const;
  uint64_t getSymbolTableOffset64() const;

  // Note that this value is signed and might return a negative value. Negative
  // values are reserved for future use.
  int32_t getRawNumberOfSymbolTableEntries32() const;

  // The sanitized value appropriate to use as an index into the symbol table.
  uint32_t getLogicalNumberOfSymbolTableEntries32() const;

  uint32_t getNumberOfSymbolTableEntries64() const;

  // Return getLogicalNumberOfSymbolTableEntries32 or
  // getNumberOfSymbolTableEntries64 depending on the object mode.
  uint32_t getNumberOfSymbolTableEntries() const;

  uint32_t getSymbolIndex(uintptr_t SymEntPtr) const;
  uint64_t getSymbolSize(DataRefImpl Symb) const;
  uintptr_t getSymbolByIndex(uint32_t Idx) const {
    return reinterpret_cast<uintptr_t>(SymbolTblPtr) +
           XCOFF::SymbolTableEntrySize * Idx;
  }
  uintptr_t getSymbolEntryAddressByIndex(uint32_t SymbolTableIndex) const;
  Expected<StringRef> getSymbolNameByIndex(uint32_t SymbolTableIndex) const;

  Expected<StringRef> getCFileName(const XCOFFFileAuxEnt *CFileEntPtr) const;
  uint16_t getOptionalHeaderSize() const;
  uint16_t getFlags() const;

  // Section header table related interfaces.
  ArrayRef<XCOFFSectionHeader32> sections32() const;
  ArrayRef<XCOFFSectionHeader64> sections64() const;

  int32_t getSectionFlags(DataRefImpl Sec) const;
  Expected<DataRefImpl> getSectionByNum(int16_t Num) const;

  Expected<uintptr_t>
  getSectionFileOffsetToRawData(XCOFF::SectionTypeFlags SectType) const;

  void checkSymbolEntryPointer(uintptr_t SymbolEntPtr) const;

  // Relocation-related interfaces.
  template <typename T>
  Expected<uint32_t>
  getNumberOfRelocationEntries(const XCOFFSectionHeader<T> &Sec) const;

  template <typename Shdr, typename Reloc>
  Expected<ArrayRef<Reloc>> relocations(const Shdr &Sec) const;

  // Loader section related interfaces.
  Expected<StringRef> getImportFileTable() const;

  // Exception-related interface.
  template <typename ExceptEnt>
  Expected<ArrayRef<ExceptEnt>> getExceptionEntries() const;

  // This function returns string table entry.
  Expected<StringRef> getStringTableEntry(uint32_t Offset) const;

  // This function returns the string table.
  StringRef getStringTable() const;

  const XCOFF::SymbolAuxType *getSymbolAuxType(uintptr_t AuxEntryAddress) const;

  static uintptr_t getAdvancedSymbolEntryAddress(uintptr_t CurrentAddress,
                                                 uint32_t Distance);

  static bool classof(const Binary *B) { return B->isXCOFF(); }

  std::optional<StringRef> tryGetCPUName() const override;
}; // XCOFFObjectFile

typedef struct {
  uint8_t LanguageId;
  uint8_t CpuTypeId;
} CFileLanguageIdAndTypeIdType;

struct XCOFFSymbolEntry32 {
  typedef struct {
    support::big32_t Magic; // Zero indicates name in string table.
    support::ubig32_t Offset;
  } NameInStrTblType;

  union {
    char SymbolName[XCOFF::NameSize];
    NameInStrTblType NameInStrTbl;
  };

  support::ubig32_t Value; // Symbol value; storage class-dependent.
  support::big16_t SectionNumber;

  union {
    support::ubig16_t SymbolType;
    CFileLanguageIdAndTypeIdType CFileLanguageIdAndTypeId;
  };

  XCOFF::StorageClass StorageClass;
  uint8_t NumberOfAuxEntries;
};

struct XCOFFSymbolEntry64 {
  support::ubig64_t Value; // Symbol value; storage class-dependent.
  support::ubig32_t Offset;
  support::big16_t SectionNumber;

  union {
    support::ubig16_t SymbolType;
    CFileLanguageIdAndTypeIdType CFileLanguageIdAndTypeId;
  };

  XCOFF::StorageClass StorageClass;
  uint8_t NumberOfAuxEntries;
};

class XCOFFSymbolRef : public SymbolRef {
public:
  enum { NAME_IN_STR_TBL_MAGIC = 0x0 };

  XCOFFSymbolRef(DataRefImpl SymEntDataRef,
                 const XCOFFObjectFile *OwningObjectPtr)
      : SymbolRef(SymEntDataRef, OwningObjectPtr) {
    assert(OwningObjectPtr && "OwningObjectPtr cannot be nullptr!");
    assert(SymEntDataRef.p != 0 &&
           "Symbol table entry pointer cannot be nullptr!");
  }

  const XCOFFSymbolEntry32 *getSymbol32() const {
    return reinterpret_cast<const XCOFFSymbolEntry32 *>(getRawDataRefImpl().p);
  }

  const XCOFFSymbolEntry64 *getSymbol64() const {
    return reinterpret_cast<const XCOFFSymbolEntry64 *>(getRawDataRefImpl().p);
  }

  uint64_t getValue() const {
    return getObject()->is64Bit() ? getValue64() : getValue32();
  }

  uint32_t getValue32() const {
    return reinterpret_cast<const XCOFFSymbolEntry32 *>(getRawDataRefImpl().p)
        ->Value;
  }

  uint64_t getValue64() const {
    return reinterpret_cast<const XCOFFSymbolEntry64 *>(getRawDataRefImpl().p)
        ->Value;
  }

  uint64_t getSize() const {
    return getObject()->getSymbolSize(getRawDataRefImpl());
  }

#define GETVALUE(X)                                                            \
  getObject()->is64Bit()                                                       \
      ? reinterpret_cast<const XCOFFSymbolEntry64 *>(getRawDataRefImpl().p)->X \
      : reinterpret_cast<const XCOFFSymbolEntry32 *>(getRawDataRefImpl().p)->X

  int16_t getSectionNumber() const { return GETVALUE(SectionNumber); }

  uint16_t getSymbolType() const { return GETVALUE(SymbolType); }

  uint8_t getLanguageIdForCFile() const {
    assert(getStorageClass() == XCOFF::C_FILE &&
           "This interface is for C_FILE only.");
    return GETVALUE(CFileLanguageIdAndTypeId.LanguageId);
  }

  uint8_t getCPUTypeIddForCFile() const {
    assert(getStorageClass() == XCOFF::C_FILE &&
           "This interface is for C_FILE only.");
    return GETVALUE(CFileLanguageIdAndTypeId.CpuTypeId);
  }

  XCOFF::StorageClass getStorageClass() const { return GETVALUE(StorageClass); }

  uint8_t getNumberOfAuxEntries() const { return GETVALUE(NumberOfAuxEntries); }

#undef GETVALUE

  uintptr_t getEntryAddress() const {
    return getRawDataRefImpl().p;
  }

  Expected<StringRef> getName() const;
  Expected<bool> isFunction() const;
  bool isCsectSymbol() const;
  Expected<XCOFFCsectAuxRef> getXCOFFCsectAuxRef() const;

private:
  const XCOFFObjectFile *getObject() const {
    return cast<XCOFFObjectFile>(BasicSymbolRef::getObject());
  }
};

class xcoff_symbol_iterator : public symbol_iterator {
public:
  xcoff_symbol_iterator(const basic_symbol_iterator &B)
      : symbol_iterator(B) {}

  xcoff_symbol_iterator(const XCOFFSymbolRef *Symbol)
      : symbol_iterator(*Symbol) {}

  const XCOFFSymbolRef *operator->() const {
    return static_cast<const XCOFFSymbolRef *>(symbol_iterator::operator->());
  }

  const XCOFFSymbolRef &operator*() const {
    return static_cast<const XCOFFSymbolRef &>(symbol_iterator::operator*());
  }
};

class TBVectorExt {
  uint16_t Data;
  SmallString<32> VecParmsInfo;

  TBVectorExt(StringRef TBvectorStrRef, Error &Err);

public:
  static Expected<TBVectorExt> create(StringRef TBvectorStrRef);
  uint8_t getNumberOfVRSaved() const;
  bool isVRSavedOnStack() const;
  bool hasVarArgs() const;
  uint8_t getNumberOfVectorParms() const;
  bool hasVMXInstruction() const;
  SmallString<32> getVectorParmsInfo() const { return VecParmsInfo; };
};

/// This class provides methods to extract traceback table data from a buffer.
/// The various accessors may reference the buffer provided via the constructor.

class XCOFFTracebackTable {
  const uint8_t *const TBPtr;
  bool Is64BitObj;
  std::optional<SmallString<32>> ParmsType;
  std::optional<uint32_t> TraceBackTableOffset;
  std::optional<uint32_t> HandlerMask;
  std::optional<uint32_t> NumOfCtlAnchors;
  std::optional<SmallVector<uint32_t, 8>> ControlledStorageInfoDisp;
  std::optional<StringRef> FunctionName;
  std::optional<uint8_t> AllocaRegister;
  std::optional<TBVectorExt> VecExt;
  std::optional<uint8_t> ExtensionTable;
  std::optional<uint64_t> EhInfoDisp;

  XCOFFTracebackTable(const uint8_t *Ptr, uint64_t &Size, Error &Err,
                      bool Is64Bit = false);

public:
  /// Parse an XCOFF Traceback Table from \a Ptr with \a Size bytes.
  /// Returns an XCOFFTracebackTable upon successful parsing, otherwise an
  /// Error is returned.
  ///
  /// \param[in] Ptr
  ///   A pointer that points just past the initial 4 bytes of zeros at the
  ///   beginning of an XCOFF Traceback Table.
  ///
  /// \param[in, out] Size
  ///    A pointer that points to the length of the XCOFF Traceback Table.
  ///    If the XCOFF Traceback Table is not parsed successfully or there are
  ///    extra bytes that are not recognized, \a Size will be updated to be the
  ///    size up to the end of the last successfully parsed field of the table.
  static Expected<XCOFFTracebackTable>
  create(const uint8_t *Ptr, uint64_t &Size, bool Is64Bits = false);
  uint8_t getVersion() const;
  uint8_t getLanguageID() const;

  bool isGlobalLinkage() const;
  bool isOutOfLineEpilogOrPrologue() const;
  bool hasTraceBackTableOffset() const;
  bool isInternalProcedure() const;
  bool hasControlledStorage() const;
  bool isTOCless() const;
  bool isFloatingPointPresent() const;
  bool isFloatingPointOperationLogOrAbortEnabled() const;

  bool isInterruptHandler() const;
  bool isFuncNamePresent() const;
  bool isAllocaUsed() const;
  uint8_t getOnConditionDirective() const;
  bool isCRSaved() const;
  bool isLRSaved() const;

  bool isBackChainStored() const;
  bool isFixup() const;
  uint8_t getNumOfFPRsSaved() const;

  bool hasVectorInfo() const;
  bool hasExtensionTable() const;
  uint8_t getNumOfGPRsSaved() const;

  uint8_t getNumberOfFixedParms() const;

  uint8_t getNumberOfFPParms() const;
  bool hasParmsOnStack() const;

  const std::optional<SmallString<32>> &getParmsType() const {
    return ParmsType;
  }
  const std::optional<uint32_t> &getTraceBackTableOffset() const {
    return TraceBackTableOffset;
  }
  const std::optional<uint32_t> &getHandlerMask() const { return HandlerMask; }
  const std::optional<uint32_t> &getNumOfCtlAnchors() {
    return NumOfCtlAnchors;
  }
  const std::optional<SmallVector<uint32_t, 8>> &
  getControlledStorageInfoDisp() {
    return ControlledStorageInfoDisp;
  }
  const std::optional<StringRef> &getFunctionName() const {
    return FunctionName;
  }
  const std::optional<uint8_t> &getAllocaRegister() const {
    return AllocaRegister;
  }
  const std::optional<TBVectorExt> &getVectorExt() const { return VecExt; }
  const std::optional<uint8_t> &getExtensionTable() const {
    return ExtensionTable;
  }
  const std::optional<uint64_t> &getEhInfoDisp() const { return EhInfoDisp; }
};

bool doesXCOFFTracebackTableBegin(ArrayRef<uint8_t> Bytes);
} // namespace object
} // namespace llvm

#endif // LLVM_OBJECT_XCOFFOBJECTFILE_H
