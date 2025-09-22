//===- COFF.h - COFF object file implementation -----------------*- C++ -*-===//
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

#ifndef LLVM_OBJECT_COFF_H
#define LLVM_OBJECT_COFF_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/CVDebugRecord.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <system_error>

namespace llvm {

template <typename T> class ArrayRef;

namespace object {

class BaseRelocRef;
class DelayImportDirectoryEntryRef;
class ExportDirectoryEntryRef;
class ImportDirectoryEntryRef;
class ImportedSymbolRef;
class ResourceSectionRef;

using import_directory_iterator = content_iterator<ImportDirectoryEntryRef>;
using delay_import_directory_iterator =
    content_iterator<DelayImportDirectoryEntryRef>;
using export_directory_iterator = content_iterator<ExportDirectoryEntryRef>;
using imported_symbol_iterator = content_iterator<ImportedSymbolRef>;
using base_reloc_iterator = content_iterator<BaseRelocRef>;

/// The DOS compatible header at the front of all PE/COFF executables.
struct dos_header {
  char                 Magic[2];
  support::ulittle16_t UsedBytesInTheLastPage;
  support::ulittle16_t FileSizeInPages;
  support::ulittle16_t NumberOfRelocationItems;
  support::ulittle16_t HeaderSizeInParagraphs;
  support::ulittle16_t MinimumExtraParagraphs;
  support::ulittle16_t MaximumExtraParagraphs;
  support::ulittle16_t InitialRelativeSS;
  support::ulittle16_t InitialSP;
  support::ulittle16_t Checksum;
  support::ulittle16_t InitialIP;
  support::ulittle16_t InitialRelativeCS;
  support::ulittle16_t AddressOfRelocationTable;
  support::ulittle16_t OverlayNumber;
  support::ulittle16_t Reserved[4];
  support::ulittle16_t OEMid;
  support::ulittle16_t OEMinfo;
  support::ulittle16_t Reserved2[10];
  support::ulittle32_t AddressOfNewExeHeader;
};

struct coff_file_header {
  support::ulittle16_t Machine;
  support::ulittle16_t NumberOfSections;
  support::ulittle32_t TimeDateStamp;
  support::ulittle32_t PointerToSymbolTable;
  support::ulittle32_t NumberOfSymbols;
  support::ulittle16_t SizeOfOptionalHeader;
  support::ulittle16_t Characteristics;

  bool isImportLibrary() const { return NumberOfSections == 0xffff; }
};

struct coff_bigobj_file_header {
  support::ulittle16_t Sig1;
  support::ulittle16_t Sig2;
  support::ulittle16_t Version;
  support::ulittle16_t Machine;
  support::ulittle32_t TimeDateStamp;
  uint8_t              UUID[16];
  support::ulittle32_t unused1;
  support::ulittle32_t unused2;
  support::ulittle32_t unused3;
  support::ulittle32_t unused4;
  support::ulittle32_t NumberOfSections;
  support::ulittle32_t PointerToSymbolTable;
  support::ulittle32_t NumberOfSymbols;
};

/// The 32-bit PE header that follows the COFF header.
struct pe32_header {
  support::ulittle16_t Magic;
  uint8_t MajorLinkerVersion;
  uint8_t MinorLinkerVersion;
  support::ulittle32_t SizeOfCode;
  support::ulittle32_t SizeOfInitializedData;
  support::ulittle32_t SizeOfUninitializedData;
  support::ulittle32_t AddressOfEntryPoint;
  support::ulittle32_t BaseOfCode;
  support::ulittle32_t BaseOfData;
  support::ulittle32_t ImageBase;
  support::ulittle32_t SectionAlignment;
  support::ulittle32_t FileAlignment;
  support::ulittle16_t MajorOperatingSystemVersion;
  support::ulittle16_t MinorOperatingSystemVersion;
  support::ulittle16_t MajorImageVersion;
  support::ulittle16_t MinorImageVersion;
  support::ulittle16_t MajorSubsystemVersion;
  support::ulittle16_t MinorSubsystemVersion;
  support::ulittle32_t Win32VersionValue;
  support::ulittle32_t SizeOfImage;
  support::ulittle32_t SizeOfHeaders;
  support::ulittle32_t CheckSum;
  support::ulittle16_t Subsystem;
  // FIXME: This should be DllCharacteristics.
  support::ulittle16_t DLLCharacteristics;
  support::ulittle32_t SizeOfStackReserve;
  support::ulittle32_t SizeOfStackCommit;
  support::ulittle32_t SizeOfHeapReserve;
  support::ulittle32_t SizeOfHeapCommit;
  support::ulittle32_t LoaderFlags;
  // FIXME: This should be NumberOfRvaAndSizes.
  support::ulittle32_t NumberOfRvaAndSize;
};

/// The 64-bit PE header that follows the COFF header.
struct pe32plus_header {
  support::ulittle16_t Magic;
  uint8_t MajorLinkerVersion;
  uint8_t MinorLinkerVersion;
  support::ulittle32_t SizeOfCode;
  support::ulittle32_t SizeOfInitializedData;
  support::ulittle32_t SizeOfUninitializedData;
  support::ulittle32_t AddressOfEntryPoint;
  support::ulittle32_t BaseOfCode;
  support::ulittle64_t ImageBase;
  support::ulittle32_t SectionAlignment;
  support::ulittle32_t FileAlignment;
  support::ulittle16_t MajorOperatingSystemVersion;
  support::ulittle16_t MinorOperatingSystemVersion;
  support::ulittle16_t MajorImageVersion;
  support::ulittle16_t MinorImageVersion;
  support::ulittle16_t MajorSubsystemVersion;
  support::ulittle16_t MinorSubsystemVersion;
  support::ulittle32_t Win32VersionValue;
  support::ulittle32_t SizeOfImage;
  support::ulittle32_t SizeOfHeaders;
  support::ulittle32_t CheckSum;
  support::ulittle16_t Subsystem;
  support::ulittle16_t DLLCharacteristics;
  support::ulittle64_t SizeOfStackReserve;
  support::ulittle64_t SizeOfStackCommit;
  support::ulittle64_t SizeOfHeapReserve;
  support::ulittle64_t SizeOfHeapCommit;
  support::ulittle32_t LoaderFlags;
  support::ulittle32_t NumberOfRvaAndSize;
};

struct data_directory {
  support::ulittle32_t RelativeVirtualAddress;
  support::ulittle32_t Size;
};

struct debug_directory {
  support::ulittle32_t Characteristics;
  support::ulittle32_t TimeDateStamp;
  support::ulittle16_t MajorVersion;
  support::ulittle16_t MinorVersion;
  support::ulittle32_t Type;
  support::ulittle32_t SizeOfData;
  support::ulittle32_t AddressOfRawData;
  support::ulittle32_t PointerToRawData;
};

template <typename IntTy>
struct import_lookup_table_entry {
  IntTy Data;

  bool isOrdinal() const { return Data < 0; }

  uint16_t getOrdinal() const {
    assert(isOrdinal() && "ILT entry is not an ordinal!");
    return Data & 0xFFFF;
  }

  uint32_t getHintNameRVA() const {
    assert(!isOrdinal() && "ILT entry is not a Hint/Name RVA!");
    return Data & 0xFFFFFFFF;
  }
};

using import_lookup_table_entry32 =
    import_lookup_table_entry<support::little32_t>;
using import_lookup_table_entry64 =
    import_lookup_table_entry<support::little64_t>;

struct delay_import_directory_table_entry {
  // dumpbin reports this field as "Characteristics" instead of "Attributes".
  support::ulittle32_t Attributes;
  support::ulittle32_t Name;
  support::ulittle32_t ModuleHandle;
  support::ulittle32_t DelayImportAddressTable;
  support::ulittle32_t DelayImportNameTable;
  support::ulittle32_t BoundDelayImportTable;
  support::ulittle32_t UnloadDelayImportTable;
  support::ulittle32_t TimeStamp;
};

struct export_directory_table_entry {
  support::ulittle32_t ExportFlags;
  support::ulittle32_t TimeDateStamp;
  support::ulittle16_t MajorVersion;
  support::ulittle16_t MinorVersion;
  support::ulittle32_t NameRVA;
  support::ulittle32_t OrdinalBase;
  support::ulittle32_t AddressTableEntries;
  support::ulittle32_t NumberOfNamePointers;
  support::ulittle32_t ExportAddressTableRVA;
  support::ulittle32_t NamePointerRVA;
  support::ulittle32_t OrdinalTableRVA;
};

union export_address_table_entry {
  support::ulittle32_t ExportRVA;
  support::ulittle32_t ForwarderRVA;
};

using export_name_pointer_table_entry = support::ulittle32_t;
using export_ordinal_table_entry = support::ulittle16_t;

struct StringTableOffset {
  support::ulittle32_t Zeroes;
  support::ulittle32_t Offset;
};

template <typename SectionNumberType>
struct coff_symbol {
  union {
    char ShortName[COFF::NameSize];
    StringTableOffset Offset;
  } Name;

  support::ulittle32_t Value;
  SectionNumberType SectionNumber;

  support::ulittle16_t Type;

  uint8_t StorageClass;
  uint8_t NumberOfAuxSymbols;
};

using coff_symbol16 = coff_symbol<support::ulittle16_t>;
using coff_symbol32 = coff_symbol<support::ulittle32_t>;

// Contains only common parts of coff_symbol16 and coff_symbol32.
struct coff_symbol_generic {
  union {
    char ShortName[COFF::NameSize];
    StringTableOffset Offset;
  } Name;
  support::ulittle32_t Value;
};

struct coff_aux_section_definition;
struct coff_aux_weak_external;

class COFFSymbolRef {
public:
  COFFSymbolRef() = default;
  COFFSymbolRef(const coff_symbol16 *CS) : CS16(CS) {}
  COFFSymbolRef(const coff_symbol32 *CS) : CS32(CS) {}

  const void *getRawPtr() const {
    return CS16 ? static_cast<const void *>(CS16) : CS32;
  }

  const coff_symbol_generic *getGeneric() const {
    if (CS16)
      return reinterpret_cast<const coff_symbol_generic *>(CS16);
    return reinterpret_cast<const coff_symbol_generic *>(CS32);
  }

  friend bool operator<(COFFSymbolRef A, COFFSymbolRef B) {
    return A.getRawPtr() < B.getRawPtr();
  }

  bool isBigObj() const {
    if (CS16)
      return false;
    if (CS32)
      return true;
    llvm_unreachable("COFFSymbolRef points to nothing!");
  }

  const char *getShortName() const {
    return CS16 ? CS16->Name.ShortName : CS32->Name.ShortName;
  }

  const StringTableOffset &getStringTableOffset() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->Name.Offset : CS32->Name.Offset;
  }

  uint32_t getValue() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->Value : CS32->Value;
  }

  int32_t getSectionNumber() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    if (CS16) {
      // Reserved sections are returned as negative numbers.
      if (CS16->SectionNumber <= COFF::MaxNumberOfSections16)
        return CS16->SectionNumber;
      return static_cast<int16_t>(CS16->SectionNumber);
    }
    return static_cast<int32_t>(CS32->SectionNumber);
  }

  uint16_t getType() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->Type : CS32->Type;
  }

  uint8_t getStorageClass() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->StorageClass : CS32->StorageClass;
  }

  uint8_t getNumberOfAuxSymbols() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->NumberOfAuxSymbols : CS32->NumberOfAuxSymbols;
  }

  uint8_t getBaseType() const { return getType() & 0x0F; }

  uint8_t getComplexType() const {
    return (getType() & 0xF0) >> COFF::SCT_COMPLEX_TYPE_SHIFT;
  }

  template <typename T> const T *getAux() const {
    return CS16 ? reinterpret_cast<const T *>(CS16 + 1)
                : reinterpret_cast<const T *>(CS32 + 1);
  }

  const coff_aux_section_definition *getSectionDefinition() const {
    if (!getNumberOfAuxSymbols() ||
        getStorageClass() != COFF::IMAGE_SYM_CLASS_STATIC)
      return nullptr;
    return getAux<coff_aux_section_definition>();
  }

  const coff_aux_weak_external *getWeakExternal() const {
    if (!getNumberOfAuxSymbols() ||
        getStorageClass() != COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL)
      return nullptr;
    return getAux<coff_aux_weak_external>();
  }

  bool isAbsolute() const {
    return getSectionNumber() == -1;
  }

  bool isExternal() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_EXTERNAL;
  }

  bool isCommon() const {
    return (isExternal() || isSection()) &&
           getSectionNumber() == COFF::IMAGE_SYM_UNDEFINED && getValue() != 0;
  }

  bool isUndefined() const {
    return isExternal() && getSectionNumber() == COFF::IMAGE_SYM_UNDEFINED &&
           getValue() == 0;
  }

  bool isWeakExternal() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL;
  }

  bool isFunctionDefinition() const {
    return isExternal() && getBaseType() == COFF::IMAGE_SYM_TYPE_NULL &&
           getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION &&
           !COFF::isReservedSectionNumber(getSectionNumber());
  }

  bool isFunctionLineInfo() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_FUNCTION;
  }

  bool isAnyUndefined() const {
    return isUndefined() || isWeakExternal();
  }

  bool isFileRecord() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_FILE;
  }

  bool isSection() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_SECTION;
  }

  bool isSectionDefinition() const {
    // C++/CLI creates external ABS symbols for non-const appdomain globals.
    // These are also followed by an auxiliary section definition.
    bool isAppdomainGlobal =
        getStorageClass() == COFF::IMAGE_SYM_CLASS_EXTERNAL &&
        getSectionNumber() == COFF::IMAGE_SYM_ABSOLUTE;
    bool isOrdinarySection = getStorageClass() == COFF::IMAGE_SYM_CLASS_STATIC;
    if (!getNumberOfAuxSymbols())
      return false;
    return isAppdomainGlobal || isOrdinarySection;
  }

  bool isCLRToken() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_CLR_TOKEN;
  }

private:
  bool isSet() const { return CS16 || CS32; }

  const coff_symbol16 *CS16 = nullptr;
  const coff_symbol32 *CS32 = nullptr;
};

struct coff_section {
  char Name[COFF::NameSize];
  support::ulittle32_t VirtualSize;
  support::ulittle32_t VirtualAddress;
  support::ulittle32_t SizeOfRawData;
  support::ulittle32_t PointerToRawData;
  support::ulittle32_t PointerToRelocations;
  support::ulittle32_t PointerToLinenumbers;
  support::ulittle16_t NumberOfRelocations;
  support::ulittle16_t NumberOfLinenumbers;
  support::ulittle32_t Characteristics;

  // Returns true if the actual number of relocations is stored in
  // VirtualAddress field of the first relocation table entry.
  bool hasExtendedRelocations() const {
    return (Characteristics & COFF::IMAGE_SCN_LNK_NRELOC_OVFL) &&
           NumberOfRelocations == UINT16_MAX;
  }

  uint32_t getAlignment() const {
    // The IMAGE_SCN_TYPE_NO_PAD bit is a legacy way of getting to
    // IMAGE_SCN_ALIGN_1BYTES.
    if (Characteristics & COFF::IMAGE_SCN_TYPE_NO_PAD)
      return 1;

    // Bit [20:24] contains section alignment. 0 means use a default alignment
    // of 16.
    uint32_t Shift = (Characteristics >> 20) & 0xF;
    if (Shift > 0)
      return 1U << (Shift - 1);
    return 16;
  }
};

struct coff_relocation {
  support::ulittle32_t VirtualAddress;
  support::ulittle32_t SymbolTableIndex;
  support::ulittle16_t Type;
};

struct coff_aux_function_definition {
  support::ulittle32_t TagIndex;
  support::ulittle32_t TotalSize;
  support::ulittle32_t PointerToLinenumber;
  support::ulittle32_t PointerToNextFunction;
  char Unused1[2];
};

static_assert(sizeof(coff_aux_function_definition) == 18,
              "auxiliary entry must be 18 bytes");

struct coff_aux_bf_and_ef_symbol {
  char Unused1[4];
  support::ulittle16_t Linenumber;
  char Unused2[6];
  support::ulittle32_t PointerToNextFunction;
  char Unused3[2];
};

static_assert(sizeof(coff_aux_bf_and_ef_symbol) == 18,
              "auxiliary entry must be 18 bytes");

struct coff_aux_weak_external {
  support::ulittle32_t TagIndex;
  support::ulittle32_t Characteristics;
  char Unused1[10];
};

static_assert(sizeof(coff_aux_weak_external) == 18,
              "auxiliary entry must be 18 bytes");

struct coff_aux_section_definition {
  support::ulittle32_t Length;
  support::ulittle16_t NumberOfRelocations;
  support::ulittle16_t NumberOfLinenumbers;
  support::ulittle32_t CheckSum;
  support::ulittle16_t NumberLowPart;
  uint8_t              Selection;
  uint8_t              Unused;
  support::ulittle16_t NumberHighPart;
  int32_t getNumber(bool IsBigObj) const {
    uint32_t Number = static_cast<uint32_t>(NumberLowPart);
    if (IsBigObj)
      Number |= static_cast<uint32_t>(NumberHighPart) << 16;
    return static_cast<int32_t>(Number);
  }
};

static_assert(sizeof(coff_aux_section_definition) == 18,
              "auxiliary entry must be 18 bytes");

struct coff_aux_clr_token {
  uint8_t              AuxType;
  uint8_t              Reserved;
  support::ulittle32_t SymbolTableIndex;
  char                 MBZ[12];
};

static_assert(sizeof(coff_aux_clr_token) == 18,
              "auxiliary entry must be 18 bytes");

struct coff_import_header {
  support::ulittle16_t Sig1;
  support::ulittle16_t Sig2;
  support::ulittle16_t Version;
  support::ulittle16_t Machine;
  support::ulittle32_t TimeDateStamp;
  support::ulittle32_t SizeOfData;
  support::ulittle16_t OrdinalHint;
  support::ulittle16_t TypeInfo;

  int getType() const { return TypeInfo & 0x3; }
  int getNameType() const { return (TypeInfo >> 2) & 0x7; }
};

struct coff_import_directory_table_entry {
  support::ulittle32_t ImportLookupTableRVA;
  support::ulittle32_t TimeDateStamp;
  support::ulittle32_t ForwarderChain;
  support::ulittle32_t NameRVA;
  support::ulittle32_t ImportAddressTableRVA;

  bool isNull() const {
    return ImportLookupTableRVA == 0 && TimeDateStamp == 0 &&
           ForwarderChain == 0 && NameRVA == 0 && ImportAddressTableRVA == 0;
  }
};

template <typename IntTy>
struct coff_tls_directory {
  IntTy StartAddressOfRawData;
  IntTy EndAddressOfRawData;
  IntTy AddressOfIndex;
  IntTy AddressOfCallBacks;
  support::ulittle32_t SizeOfZeroFill;
  support::ulittle32_t Characteristics;

  uint32_t getAlignment() const {
    // Bit [20:24] contains section alignment.
    uint32_t Shift = (Characteristics & COFF::IMAGE_SCN_ALIGN_MASK) >> 20;
    if (Shift > 0)
      return 1U << (Shift - 1);
    return 0;
  }

  void setAlignment(uint32_t Align) {
    uint32_t AlignBits = 0;
    if (Align) {
      assert(llvm::isPowerOf2_32(Align) && "alignment is not a power of 2");
      assert(llvm::Log2_32(Align) <= 13 && "alignment requested is too large");
      AlignBits = (llvm::Log2_32(Align) + 1) << 20;
    }
    Characteristics =
        (Characteristics & ~COFF::IMAGE_SCN_ALIGN_MASK) | AlignBits;
  }
};

using coff_tls_directory32 = coff_tls_directory<support::little32_t>;
using coff_tls_directory64 = coff_tls_directory<support::little64_t>;

enum class frame_type : uint16_t { Fpo = 0, Trap = 1, Tss = 2, NonFpo = 3 };

struct coff_load_config_code_integrity {
  support::ulittle16_t Flags;
  support::ulittle16_t Catalog;
  support::ulittle32_t CatalogOffset;
  support::ulittle32_t Reserved;
};

/// 32-bit load config (IMAGE_LOAD_CONFIG_DIRECTORY32)
struct coff_load_configuration32 {
  support::ulittle32_t Size;
  support::ulittle32_t TimeDateStamp;
  support::ulittle16_t MajorVersion;
  support::ulittle16_t MinorVersion;
  support::ulittle32_t GlobalFlagsClear;
  support::ulittle32_t GlobalFlagsSet;
  support::ulittle32_t CriticalSectionDefaultTimeout;
  support::ulittle32_t DeCommitFreeBlockThreshold;
  support::ulittle32_t DeCommitTotalFreeThreshold;
  support::ulittle32_t LockPrefixTable;
  support::ulittle32_t MaximumAllocationSize;
  support::ulittle32_t VirtualMemoryThreshold;
  support::ulittle32_t ProcessAffinityMask;
  support::ulittle32_t ProcessHeapFlags;
  support::ulittle16_t CSDVersion;
  support::ulittle16_t DependentLoadFlags;
  support::ulittle32_t EditList;
  support::ulittle32_t SecurityCookie;
  support::ulittle32_t SEHandlerTable;
  support::ulittle32_t SEHandlerCount;

  // Added in MSVC 2015 for /guard:cf.
  support::ulittle32_t GuardCFCheckFunction;
  support::ulittle32_t GuardCFCheckDispatch;
  support::ulittle32_t GuardCFFunctionTable;
  support::ulittle32_t GuardCFFunctionCount;
  support::ulittle32_t GuardFlags; // coff_guard_flags

  // Added in MSVC 2017
  coff_load_config_code_integrity CodeIntegrity;
  support::ulittle32_t GuardAddressTakenIatEntryTable;
  support::ulittle32_t GuardAddressTakenIatEntryCount;
  support::ulittle32_t GuardLongJumpTargetTable;
  support::ulittle32_t GuardLongJumpTargetCount;
  support::ulittle32_t DynamicValueRelocTable;
  support::ulittle32_t CHPEMetadataPointer;
  support::ulittle32_t GuardRFFailureRoutine;
  support::ulittle32_t GuardRFFailureRoutineFunctionPointer;
  support::ulittle32_t DynamicValueRelocTableOffset;
  support::ulittle16_t DynamicValueRelocTableSection;
  support::ulittle16_t Reserved2;
  support::ulittle32_t GuardRFVerifyStackPointerFunctionPointer;
  support::ulittle32_t HotPatchTableOffset;

  // Added in MSVC 2019
  support::ulittle32_t Reserved3;
  support::ulittle32_t EnclaveConfigurationPointer;
  support::ulittle32_t VolatileMetadataPointer;
  support::ulittle32_t GuardEHContinuationTable;
  support::ulittle32_t GuardEHContinuationCount;
  support::ulittle32_t GuardXFGCheckFunctionPointer;
  support::ulittle32_t GuardXFGDispatchFunctionPointer;
  support::ulittle32_t GuardXFGTableDispatchFunctionPointer;
  support::ulittle32_t CastGuardOsDeterminedFailureMode;
};

/// 64-bit load config (IMAGE_LOAD_CONFIG_DIRECTORY64)
struct coff_load_configuration64 {
  support::ulittle32_t Size;
  support::ulittle32_t TimeDateStamp;
  support::ulittle16_t MajorVersion;
  support::ulittle16_t MinorVersion;
  support::ulittle32_t GlobalFlagsClear;
  support::ulittle32_t GlobalFlagsSet;
  support::ulittle32_t CriticalSectionDefaultTimeout;
  support::ulittle64_t DeCommitFreeBlockThreshold;
  support::ulittle64_t DeCommitTotalFreeThreshold;
  support::ulittle64_t LockPrefixTable;
  support::ulittle64_t MaximumAllocationSize;
  support::ulittle64_t VirtualMemoryThreshold;
  support::ulittle64_t ProcessAffinityMask;
  support::ulittle32_t ProcessHeapFlags;
  support::ulittle16_t CSDVersion;
  support::ulittle16_t DependentLoadFlags;
  support::ulittle64_t EditList;
  support::ulittle64_t SecurityCookie;
  support::ulittle64_t SEHandlerTable;
  support::ulittle64_t SEHandlerCount;

  // Added in MSVC 2015 for /guard:cf.
  support::ulittle64_t GuardCFCheckFunction;
  support::ulittle64_t GuardCFCheckDispatch;
  support::ulittle64_t GuardCFFunctionTable;
  support::ulittle64_t GuardCFFunctionCount;
  support::ulittle32_t GuardFlags;

  // Added in MSVC 2017
  coff_load_config_code_integrity CodeIntegrity;
  support::ulittle64_t GuardAddressTakenIatEntryTable;
  support::ulittle64_t GuardAddressTakenIatEntryCount;
  support::ulittle64_t GuardLongJumpTargetTable;
  support::ulittle64_t GuardLongJumpTargetCount;
  support::ulittle64_t DynamicValueRelocTable;
  support::ulittle64_t CHPEMetadataPointer;
  support::ulittle64_t GuardRFFailureRoutine;
  support::ulittle64_t GuardRFFailureRoutineFunctionPointer;
  support::ulittle32_t DynamicValueRelocTableOffset;
  support::ulittle16_t DynamicValueRelocTableSection;
  support::ulittle16_t Reserved2;
  support::ulittle64_t GuardRFVerifyStackPointerFunctionPointer;
  support::ulittle32_t HotPatchTableOffset;

  // Added in MSVC 2019
  support::ulittle32_t Reserved3;
  support::ulittle64_t EnclaveConfigurationPointer;
  support::ulittle64_t VolatileMetadataPointer;
  support::ulittle64_t GuardEHContinuationTable;
  support::ulittle64_t GuardEHContinuationCount;
  support::ulittle64_t GuardXFGCheckFunctionPointer;
  support::ulittle64_t GuardXFGDispatchFunctionPointer;
  support::ulittle64_t GuardXFGTableDispatchFunctionPointer;
  support::ulittle64_t CastGuardOsDeterminedFailureMode;
};

struct chpe_metadata {
  support::ulittle32_t Version;
  support::ulittle32_t CodeMap;
  support::ulittle32_t CodeMapCount;
  support::ulittle32_t CodeRangesToEntryPoints;
  support::ulittle32_t RedirectionMetadata;
  support::ulittle32_t __os_arm64x_dispatch_call_no_redirect;
  support::ulittle32_t __os_arm64x_dispatch_ret;
  support::ulittle32_t __os_arm64x_dispatch_call;
  support::ulittle32_t __os_arm64x_dispatch_icall;
  support::ulittle32_t __os_arm64x_dispatch_icall_cfg;
  support::ulittle32_t AlternateEntryPoint;
  support::ulittle32_t AuxiliaryIAT;
  support::ulittle32_t CodeRangesToEntryPointsCount;
  support::ulittle32_t RedirectionMetadataCount;
  support::ulittle32_t GetX64InformationFunctionPointer;
  support::ulittle32_t SetX64InformationFunctionPointer;
  support::ulittle32_t ExtraRFETable;
  support::ulittle32_t ExtraRFETableSize;
  support::ulittle32_t __os_arm64x_dispatch_fptr;
  support::ulittle32_t AuxiliaryIATCopy;
};

enum chpe_range_type { Arm64 = 0, Arm64EC = 1, Amd64 = 2 };

struct chpe_range_entry {
  support::ulittle32_t StartOffset;
  support::ulittle32_t Length;

  // The two low bits of StartOffset contain a range type.
  static constexpr uint32_t TypeMask = 3;

  uint32_t getStart() const { return StartOffset & ~TypeMask; }
  uint16_t getType() const { return StartOffset & TypeMask; }
};

struct chpe_code_range_entry {
  support::ulittle32_t StartRva;
  support::ulittle32_t EndRva;
  support::ulittle32_t EntryPoint;
};

struct chpe_redirection_entry {
  support::ulittle32_t Source;
  support::ulittle32_t Destination;
};

struct coff_runtime_function_x64 {
  support::ulittle32_t BeginAddress;
  support::ulittle32_t EndAddress;
  support::ulittle32_t UnwindInformation;
};

struct coff_base_reloc_block_header {
  support::ulittle32_t PageRVA;
  support::ulittle32_t BlockSize;
};

struct coff_base_reloc_block_entry {
  support::ulittle16_t Data;

  int getType() const { return Data >> 12; }
  int getOffset() const { return Data & ((1 << 12) - 1); }
};

struct coff_resource_dir_entry {
  union {
    support::ulittle32_t NameOffset;
    support::ulittle32_t ID;
    uint32_t getNameOffset() const {
      return maskTrailingOnes<uint32_t>(31) & NameOffset;
    }
    // Even though the PE/COFF spec doesn't mention this, the high bit of a name
    // offset is set.
    void setNameOffset(uint32_t Offset) { NameOffset = Offset | (1 << 31); }
  } Identifier;
  union {
    support::ulittle32_t DataEntryOffset;
    support::ulittle32_t SubdirOffset;

    bool isSubDir() const { return SubdirOffset >> 31; }
    uint32_t value() const {
      return maskTrailingOnes<uint32_t>(31) & SubdirOffset;
    }

  } Offset;
};

struct coff_resource_data_entry {
  support::ulittle32_t DataRVA;
  support::ulittle32_t DataSize;
  support::ulittle32_t Codepage;
  support::ulittle32_t Reserved;
};

struct coff_resource_dir_table {
  support::ulittle32_t Characteristics;
  support::ulittle32_t TimeDateStamp;
  support::ulittle16_t MajorVersion;
  support::ulittle16_t MinorVersion;
  support::ulittle16_t NumberOfNameEntries;
  support::ulittle16_t NumberOfIDEntries;
};

struct debug_h_header {
  support::ulittle32_t Magic;
  support::ulittle16_t Version;
  support::ulittle16_t HashAlgorithm;
};

class COFFObjectFile : public ObjectFile {
private:
  COFFObjectFile(MemoryBufferRef Object);

  friend class ImportDirectoryEntryRef;
  friend class ExportDirectoryEntryRef;
  const coff_file_header *COFFHeader;
  const coff_bigobj_file_header *COFFBigObjHeader;
  const pe32_header *PE32Header;
  const pe32plus_header *PE32PlusHeader;
  const data_directory *DataDirectory;
  const coff_section *SectionTable;
  const coff_symbol16 *SymbolTable16;
  const coff_symbol32 *SymbolTable32;
  const char *StringTable;
  uint32_t StringTableSize;
  const coff_import_directory_table_entry *ImportDirectory;
  const delay_import_directory_table_entry *DelayImportDirectory;
  uint32_t NumberOfDelayImportDirectory;
  const export_directory_table_entry *ExportDirectory;
  const coff_base_reloc_block_header *BaseRelocHeader;
  const coff_base_reloc_block_header *BaseRelocEnd;
  const debug_directory *DebugDirectoryBegin;
  const debug_directory *DebugDirectoryEnd;
  const coff_tls_directory32 *TLSDirectory32;
  const coff_tls_directory64 *TLSDirectory64;
  // Either coff_load_configuration32 or coff_load_configuration64.
  const void *LoadConfig = nullptr;
  const chpe_metadata *CHPEMetadata = nullptr;

  Expected<StringRef> getString(uint32_t offset) const;

  template <typename coff_symbol_type>
  const coff_symbol_type *toSymb(DataRefImpl Symb) const;
  const coff_section *toSec(DataRefImpl Sec) const;
  const coff_relocation *toRel(DataRefImpl Rel) const;

  // Finish initializing the object and return success or an error.
  Error initialize();

  Error initSymbolTablePtr();
  Error initImportTablePtr();
  Error initDelayImportTablePtr();
  Error initExportTablePtr();
  Error initBaseRelocPtr();
  Error initDebugDirectoryPtr();
  Error initTLSDirectoryPtr();
  Error initLoadConfigPtr();

public:
  static Expected<std::unique_ptr<COFFObjectFile>>
  create(MemoryBufferRef Object);

  uintptr_t getSymbolTable() const {
    if (SymbolTable16)
      return reinterpret_cast<uintptr_t>(SymbolTable16);
    if (SymbolTable32)
      return reinterpret_cast<uintptr_t>(SymbolTable32);
    return uintptr_t(0);
  }

  uint16_t getMachine() const {
    if (COFFHeader) {
      if (CHPEMetadata) {
        switch (COFFHeader->Machine) {
        case COFF::IMAGE_FILE_MACHINE_AMD64:
          return COFF::IMAGE_FILE_MACHINE_ARM64EC;
        case COFF::IMAGE_FILE_MACHINE_ARM64:
          return COFF::IMAGE_FILE_MACHINE_ARM64X;
        }
      }
      return COFFHeader->Machine;
    }
    if (COFFBigObjHeader)
      return COFFBigObjHeader->Machine;
    llvm_unreachable("no COFF header!");
  }

  uint16_t getSizeOfOptionalHeader() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0
                                           : COFFHeader->SizeOfOptionalHeader;
    // bigobj doesn't have this field.
    if (COFFBigObjHeader)
      return 0;
    llvm_unreachable("no COFF header!");
  }

  uint16_t getCharacteristics() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0 : COFFHeader->Characteristics;
    // bigobj doesn't have characteristics to speak of,
    // editbin will silently lie to you if you attempt to set any.
    if (COFFBigObjHeader)
      return 0;
    llvm_unreachable("no COFF header!");
  }

  uint32_t getTimeDateStamp() const {
    if (COFFHeader)
      return COFFHeader->TimeDateStamp;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->TimeDateStamp;
    llvm_unreachable("no COFF header!");
  }

  uint32_t getNumberOfSections() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0 : COFFHeader->NumberOfSections;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->NumberOfSections;
    llvm_unreachable("no COFF header!");
  }

  uint32_t getPointerToSymbolTable() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0
                                           : COFFHeader->PointerToSymbolTable;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->PointerToSymbolTable;
    llvm_unreachable("no COFF header!");
  }

  uint32_t getRawNumberOfSymbols() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0 : COFFHeader->NumberOfSymbols;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->NumberOfSymbols;
    llvm_unreachable("no COFF header!");
  }

  uint32_t getNumberOfSymbols() const {
    if (!SymbolTable16 && !SymbolTable32)
      return 0;
    return getRawNumberOfSymbols();
  }

  uint32_t getStringTableSize() const { return StringTableSize; }

  const export_directory_table_entry *getExportTable() const {
    return ExportDirectory;
  }

  const coff_load_configuration32 *getLoadConfig32() const {
    assert(!is64());
    return reinterpret_cast<const coff_load_configuration32 *>(LoadConfig);
  }

  const coff_load_configuration64 *getLoadConfig64() const {
    assert(is64());
    return reinterpret_cast<const coff_load_configuration64 *>(LoadConfig);
  }

  const chpe_metadata *getCHPEMetadata() const { return CHPEMetadata; }

  StringRef getRelocationTypeName(uint16_t Type) const;

protected:
  void moveSymbolNext(DataRefImpl &Symb) const override;
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
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
  bool isSectionVirtual(DataRefImpl Sec) const override;
  bool isDebugSection(DataRefImpl Sec) const override;
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  void moveRelocationNext(DataRefImpl &Rel) const override;
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

public:
  basic_symbol_iterator symbol_begin() const override;
  basic_symbol_iterator symbol_end() const override;
  section_iterator section_begin() const override;
  section_iterator section_end() const override;

  bool is64Bit() const override { return false; }

  const coff_section *getCOFFSection(const SectionRef &Section) const;
  COFFSymbolRef getCOFFSymbol(const DataRefImpl &Ref) const;
  COFFSymbolRef getCOFFSymbol(const SymbolRef &Symbol) const;
  const coff_relocation *getCOFFRelocation(const RelocationRef &Reloc) const;
  unsigned getSectionID(SectionRef Sec) const;
  unsigned getSymbolSectionID(SymbolRef Sym) const;

  uint8_t getBytesInAddress() const override;
  StringRef getFileFormatName() const override;
  Triple::ArchType getArch() const override;
  Expected<uint64_t> getStartAddress() const override;
  Expected<SubtargetFeatures> getFeatures() const override {
    return SubtargetFeatures();
  }

  import_directory_iterator import_directory_begin() const;
  import_directory_iterator import_directory_end() const;
  delay_import_directory_iterator delay_import_directory_begin() const;
  delay_import_directory_iterator delay_import_directory_end() const;
  export_directory_iterator export_directory_begin() const;
  export_directory_iterator export_directory_end() const;
  base_reloc_iterator base_reloc_begin() const;
  base_reloc_iterator base_reloc_end() const;
  const debug_directory *debug_directory_begin() const {
    return DebugDirectoryBegin;
  }
  const debug_directory *debug_directory_end() const {
    return DebugDirectoryEnd;
  }

  iterator_range<import_directory_iterator> import_directories() const;
  iterator_range<delay_import_directory_iterator>
      delay_import_directories() const;
  iterator_range<export_directory_iterator> export_directories() const;
  iterator_range<base_reloc_iterator> base_relocs() const;
  iterator_range<const debug_directory *> debug_directories() const {
    return make_range(debug_directory_begin(), debug_directory_end());
  }

  const coff_tls_directory32 *getTLSDirectory32() const {
    return TLSDirectory32;
  }
  const coff_tls_directory64 *getTLSDirectory64() const {
    return TLSDirectory64;
  }

  const dos_header *getDOSHeader() const {
    if (!PE32Header && !PE32PlusHeader)
      return nullptr;
    return reinterpret_cast<const dos_header *>(base());
  }

  const coff_file_header *getCOFFHeader() const { return COFFHeader; }
  const coff_bigobj_file_header *getCOFFBigObjHeader() const {
    return COFFBigObjHeader;
  }
  const pe32_header *getPE32Header() const { return PE32Header; }
  const pe32plus_header *getPE32PlusHeader() const { return PE32PlusHeader; }

  const data_directory *getDataDirectory(uint32_t index) const;
  Expected<const coff_section *> getSection(int32_t index) const;

  Expected<COFFSymbolRef> getSymbol(uint32_t index) const {
    if (index >= getNumberOfSymbols())
      return errorCodeToError(object_error::parse_failed);
    if (SymbolTable16)
      return COFFSymbolRef(SymbolTable16 + index);
    if (SymbolTable32)
      return COFFSymbolRef(SymbolTable32 + index);
    return errorCodeToError(object_error::parse_failed);
  }

  template <typename T>
  Error getAuxSymbol(uint32_t index, const T *&Res) const {
    Expected<COFFSymbolRef> S = getSymbol(index);
    if (Error E = S.takeError())
      return E;
    Res = reinterpret_cast<const T *>(S->getRawPtr());
    return Error::success();
  }

  Expected<StringRef> getSymbolName(COFFSymbolRef Symbol) const;
  Expected<StringRef> getSymbolName(const coff_symbol_generic *Symbol) const;

  ArrayRef<uint8_t> getSymbolAuxData(COFFSymbolRef Symbol) const;

  uint32_t getSymbolIndex(COFFSymbolRef Symbol) const;

  size_t getSymbolTableEntrySize() const {
    if (COFFHeader)
      return sizeof(coff_symbol16);
    if (COFFBigObjHeader)
      return sizeof(coff_symbol32);
    llvm_unreachable("null symbol table pointer!");
  }

  ArrayRef<coff_relocation> getRelocations(const coff_section *Sec) const;

  Expected<StringRef> getSectionName(const coff_section *Sec) const;
  uint64_t getSectionSize(const coff_section *Sec) const;
  Error getSectionContents(const coff_section *Sec,
                           ArrayRef<uint8_t> &Res) const;

  uint64_t getImageBase() const;
  Error getVaPtr(uint64_t VA, uintptr_t &Res) const;
  Error getRvaPtr(uint32_t Rva, uintptr_t &Res,
                  const char *ErrorContext = nullptr) const;

  /// Given an RVA base and size, returns a valid array of bytes or an error
  /// code if the RVA and size is not contained completely within a valid
  /// section.
  Error getRvaAndSizeAsBytes(uint32_t RVA, uint32_t Size,
                             ArrayRef<uint8_t> &Contents,
                             const char *ErrorContext = nullptr) const;

  Error getHintName(uint32_t Rva, uint16_t &Hint,
                              StringRef &Name) const;

  /// Get PDB information out of a codeview debug directory entry.
  Error getDebugPDBInfo(const debug_directory *DebugDir,
                        const codeview::DebugInfo *&Info,
                        StringRef &PDBFileName) const;

  /// Get PDB information from an executable. If the information is not present,
  /// Info will be set to nullptr and PDBFileName will be empty. An error is
  /// returned only on corrupt object files. Convenience accessor that can be
  /// used if the debug directory is not already handy.
  Error getDebugPDBInfo(const codeview::DebugInfo *&Info,
                        StringRef &PDBFileName) const;

  bool isRelocatableObject() const override;
  bool is64() const { return PE32PlusHeader; }

  StringRef mapDebugSectionName(StringRef Name) const override;

  static bool classof(const Binary *v) { return v->isCOFF(); }
};

// The iterator for the import directory table.
class ImportDirectoryEntryRef {
public:
  ImportDirectoryEntryRef() = default;
  ImportDirectoryEntryRef(const coff_import_directory_table_entry *Table,
                          uint32_t I, const COFFObjectFile *Owner)
      : ImportTable(Table), Index(I), OwningObject(Owner) {}

  bool operator==(const ImportDirectoryEntryRef &Other) const;
  void moveNext();

  imported_symbol_iterator imported_symbol_begin() const;
  imported_symbol_iterator imported_symbol_end() const;
  iterator_range<imported_symbol_iterator> imported_symbols() const;

  imported_symbol_iterator lookup_table_begin() const;
  imported_symbol_iterator lookup_table_end() const;
  iterator_range<imported_symbol_iterator> lookup_table_symbols() const;

  Error getName(StringRef &Result) const;
  Error getImportLookupTableRVA(uint32_t &Result) const;
  Error getImportAddressTableRVA(uint32_t &Result) const;

  Error
  getImportTableEntry(const coff_import_directory_table_entry *&Result) const;

private:
  const coff_import_directory_table_entry *ImportTable;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

class DelayImportDirectoryEntryRef {
public:
  DelayImportDirectoryEntryRef() = default;
  DelayImportDirectoryEntryRef(const delay_import_directory_table_entry *T,
                               uint32_t I, const COFFObjectFile *Owner)
      : Table(T), Index(I), OwningObject(Owner) {}

  bool operator==(const DelayImportDirectoryEntryRef &Other) const;
  void moveNext();

  imported_symbol_iterator imported_symbol_begin() const;
  imported_symbol_iterator imported_symbol_end() const;
  iterator_range<imported_symbol_iterator> imported_symbols() const;

  Error getName(StringRef &Result) const;
  Error getDelayImportTable(
      const delay_import_directory_table_entry *&Result) const;
  Error getImportAddress(int AddrIndex, uint64_t &Result) const;

private:
  const delay_import_directory_table_entry *Table;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

// The iterator for the export directory table entry.
class ExportDirectoryEntryRef {
public:
  ExportDirectoryEntryRef() = default;
  ExportDirectoryEntryRef(const export_directory_table_entry *Table, uint32_t I,
                          const COFFObjectFile *Owner)
      : ExportTable(Table), Index(I), OwningObject(Owner) {}

  bool operator==(const ExportDirectoryEntryRef &Other) const;
  void moveNext();

  Error getDllName(StringRef &Result) const;
  Error getOrdinalBase(uint32_t &Result) const;
  Error getOrdinal(uint32_t &Result) const;
  Error getExportRVA(uint32_t &Result) const;
  Error getSymbolName(StringRef &Result) const;

  Error isForwarder(bool &Result) const;
  Error getForwardTo(StringRef &Result) const;

private:
  const export_directory_table_entry *ExportTable;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

class ImportedSymbolRef {
public:
  ImportedSymbolRef() = default;
  ImportedSymbolRef(const import_lookup_table_entry32 *Entry, uint32_t I,
                    const COFFObjectFile *Owner)
      : Entry32(Entry), Entry64(nullptr), Index(I), OwningObject(Owner) {}
  ImportedSymbolRef(const import_lookup_table_entry64 *Entry, uint32_t I,
                    const COFFObjectFile *Owner)
      : Entry32(nullptr), Entry64(Entry), Index(I), OwningObject(Owner) {}

  bool operator==(const ImportedSymbolRef &Other) const;
  void moveNext();

  Error getSymbolName(StringRef &Result) const;
  Error isOrdinal(bool &Result) const;
  Error getOrdinal(uint16_t &Result) const;
  Error getHintNameRVA(uint32_t &Result) const;

private:
  const import_lookup_table_entry32 *Entry32;
  const import_lookup_table_entry64 *Entry64;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

class BaseRelocRef {
public:
  BaseRelocRef() = default;
  BaseRelocRef(const coff_base_reloc_block_header *Header,
               const COFFObjectFile *Owner)
      : Header(Header), Index(0) {}

  bool operator==(const BaseRelocRef &Other) const;
  void moveNext();

  Error getType(uint8_t &Type) const;
  Error getRVA(uint32_t &Result) const;

private:
  const coff_base_reloc_block_header *Header;
  uint32_t Index;
};

class ResourceSectionRef {
public:
  ResourceSectionRef() = default;
  explicit ResourceSectionRef(StringRef Ref)
      : BBS(Ref, llvm::endianness::little) {}

  Error load(const COFFObjectFile *O);
  Error load(const COFFObjectFile *O, const SectionRef &S);

  Expected<ArrayRef<UTF16>>
  getEntryNameString(const coff_resource_dir_entry &Entry);
  Expected<const coff_resource_dir_table &>
  getEntrySubDir(const coff_resource_dir_entry &Entry);
  Expected<const coff_resource_data_entry &>
  getEntryData(const coff_resource_dir_entry &Entry);
  Expected<const coff_resource_dir_table &> getBaseTable();
  Expected<const coff_resource_dir_entry &>
  getTableEntry(const coff_resource_dir_table &Table, uint32_t Index);

  Expected<StringRef> getContents(const coff_resource_data_entry &Entry);

private:
  BinaryByteStream BBS;

  SectionRef Section;
  const COFFObjectFile *Obj = nullptr;

  std::vector<const coff_relocation *> Relocs;

  Expected<const coff_resource_dir_table &> getTableAtOffset(uint32_t Offset);
  Expected<const coff_resource_dir_entry &>
  getTableEntryAtOffset(uint32_t Offset);
  Expected<const coff_resource_data_entry &>
  getDataEntryAtOffset(uint32_t Offset);
  Expected<ArrayRef<UTF16>> getDirStringAtOffset(uint32_t Offset);
};

// Corresponds to `_FPO_DATA` structure in the PE/COFF spec.
struct FpoData {
  support::ulittle32_t Offset; // ulOffStart: Offset 1st byte of function code
  support::ulittle32_t Size;   // cbProcSize: # bytes in function
  support::ulittle32_t NumLocals; // cdwLocals: # bytes in locals/4
  support::ulittle16_t NumParams; // cdwParams: # bytes in params/4
  support::ulittle16_t Attributes;

  // cbProlog: # bytes in prolog
  int getPrologSize() const { return Attributes & 0xF; }

  // cbRegs: # regs saved
  int getNumSavedRegs() const { return (Attributes >> 8) & 0x7; }

  // fHasSEH: true if seh is func
  bool hasSEH() const { return (Attributes >> 9) & 1; }

  // fUseBP: true if EBP has been allocated
  bool useBP() const { return (Attributes >> 10) & 1; }

  // cbFrame: frame pointer
  frame_type getFP() const { return static_cast<frame_type>(Attributes >> 14); }
};

class SectionStrippedError
    : public ErrorInfo<SectionStrippedError, BinaryError> {
public:
  SectionStrippedError() { setErrorCode(object_error::section_stripped); }
};

} // end namespace object

} // end namespace llvm

#endif // LLVM_OBJECT_COFF_H
