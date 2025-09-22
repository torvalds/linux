//===-- COFFDumper.cpp - COFF-specific dumper -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the COFF-specific dumper for llvm-readobj.
///
//===----------------------------------------------------------------------===//

#include "ARMWinEHPrinter.h"
#include "ObjDumper.h"
#include "StackMapPrinter.h"
#include "Win64EHDumper.h"
#include "llvm-readobj.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/SymbolDumpDelegate.h"
#include "llvm/DebugInfo/CodeView/SymbolDumper.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeDumpVisitor.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeStreamMerger.h"
#include "llvm/DebugInfo/CodeView/TypeTableCollection.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Win64EH.h"
#include "llvm/Support/raw_ostream.h"
#include <ctime>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::codeview;
using namespace llvm::support;
using namespace llvm::Win64EH;

namespace {

struct LoadConfigTables {
  uint64_t SEHTableVA = 0;
  uint64_t SEHTableCount = 0;
  uint32_t GuardFlags = 0;
  uint64_t GuardFidTableVA = 0;
  uint64_t GuardFidTableCount = 0;
  uint64_t GuardIatTableVA = 0;
  uint64_t GuardIatTableCount = 0;
  uint64_t GuardLJmpTableVA = 0;
  uint64_t GuardLJmpTableCount = 0;
  uint64_t GuardEHContTableVA = 0;
  uint64_t GuardEHContTableCount = 0;
};

class COFFDumper : public ObjDumper {
public:
  friend class COFFObjectDumpDelegate;
  COFFDumper(const llvm::object::COFFObjectFile *Obj, ScopedPrinter &Writer)
      : ObjDumper(Writer, Obj->getFileName()), Obj(Obj), Writer(Writer),
        Types(100) {}

  void printFileHeaders() override;
  void printSectionHeaders() override;
  void printRelocations() override;
  void printUnwindInfo() override;

  void printNeededLibraries() override;

  void printCOFFImports() override;
  void printCOFFExports() override;
  void printCOFFDirectives() override;
  void printCOFFBaseReloc() override;
  void printCOFFDebugDirectory() override;
  void printCOFFTLSDirectory() override;
  void printCOFFResources() override;
  void printCOFFLoadConfig() override;
  void printCodeViewDebugInfo() override;
  void mergeCodeViewTypes(llvm::codeview::MergingTypeTableBuilder &CVIDs,
                          llvm::codeview::MergingTypeTableBuilder &CVTypes,
                          llvm::codeview::GlobalTypeTableBuilder &GlobalCVIDs,
                          llvm::codeview::GlobalTypeTableBuilder &GlobalCVTypes,
                          bool GHash) override;
  void printStackMap() const override;
  void printAddrsig() override;
  void printCGProfile() override;

private:
  StringRef getSymbolName(uint32_t Index);
  void printSymbols(bool ExtraSymInfo) override;
  void printDynamicSymbols() override;
  void printSymbol(const SymbolRef &Sym);
  void printRelocation(const SectionRef &Section, const RelocationRef &Reloc,
                       uint64_t Bias = 0);
  void printDataDirectory(uint32_t Index, const std::string &FieldName);

  void printDOSHeader(const dos_header *DH);
  template <class PEHeader> void printPEHeader(const PEHeader *Hdr);
  void printBaseOfDataField(const pe32_header *Hdr);
  void printBaseOfDataField(const pe32plus_header *Hdr);
  template <typename T>
  void printCOFFLoadConfig(const T *Conf, LoadConfigTables &Tables);
  template <typename IntTy>
  void printCOFFTLSDirectory(const coff_tls_directory<IntTy> *TlsTable);
  typedef void (*PrintExtraCB)(raw_ostream &, const uint8_t *);
  void printRVATable(uint64_t TableVA, uint64_t Count, uint64_t EntrySize,
                     PrintExtraCB PrintExtra = nullptr);

  void printCodeViewSymbolSection(StringRef SectionName, const SectionRef &Section);
  void printCodeViewTypeSection(StringRef SectionName, const SectionRef &Section);
  StringRef getFileNameForFileOffset(uint32_t FileOffset);
  void printFileNameForOffset(StringRef Label, uint32_t FileOffset);
  void printTypeIndex(StringRef FieldName, TypeIndex TI) {
    // Forward to CVTypeDumper for simplicity.
    codeview::printTypeIndex(Writer, FieldName, TI, Types);
  }

  void printCodeViewSymbolsSubsection(StringRef Subsection,
                                      const SectionRef &Section,
                                      StringRef SectionContents);

  void printCodeViewFileChecksums(StringRef Subsection);

  void printCodeViewInlineeLines(StringRef Subsection);

  void printRelocatedField(StringRef Label, const coff_section *Sec,
                           uint32_t RelocOffset, uint32_t Offset,
                           StringRef *RelocSym = nullptr);

  uint32_t countTotalTableEntries(ResourceSectionRef RSF,
                                  const coff_resource_dir_table &Table,
                                  StringRef Level);

  void printResourceDirectoryTable(ResourceSectionRef RSF,
                                   const coff_resource_dir_table &Table,
                                   StringRef Level);

  void printBinaryBlockWithRelocs(StringRef Label, const SectionRef &Sec,
                                  StringRef SectionContents, StringRef Block);

  /// Given a .debug$S section, find the string table and file checksum table.
  void initializeFileAndStringTables(BinaryStreamReader &Reader);

  void cacheRelocations();

  std::error_code resolveSymbol(const coff_section *Section, uint64_t Offset,
                                SymbolRef &Sym);
  std::error_code resolveSymbolName(const coff_section *Section,
                                    uint64_t Offset, StringRef &Name);
  std::error_code resolveSymbolName(const coff_section *Section,
                                    StringRef SectionContents,
                                    const void *RelocPtr, StringRef &Name);
  void printImportedSymbols(iterator_range<imported_symbol_iterator> Range);
  void printDelayImportedSymbols(
      const DelayImportDirectoryEntryRef &I,
      iterator_range<imported_symbol_iterator> Range);

  typedef DenseMap<const coff_section*, std::vector<RelocationRef> > RelocMapTy;

  const llvm::object::COFFObjectFile *Obj;
  bool RelocCached = false;
  RelocMapTy RelocMap;

  DebugChecksumsSubsectionRef CVFileChecksumTable;

  DebugStringTableSubsectionRef CVStringTable;

  /// Track the compilation CPU type. S_COMPILE3 symbol records typically come
  /// first, but if we don't see one, just assume an X64 CPU type. It is common.
  CPUType CompilationCPUType = CPUType::X64;

  ScopedPrinter &Writer;
  LazyRandomTypeCollection Types;
};

class COFFObjectDumpDelegate : public SymbolDumpDelegate {
public:
  COFFObjectDumpDelegate(COFFDumper &CD, const SectionRef &SR,
                         const COFFObjectFile *Obj, StringRef SectionContents)
      : CD(CD), SR(SR), SectionContents(SectionContents) {
    Sec = Obj->getCOFFSection(SR);
  }

  uint32_t getRecordOffset(BinaryStreamReader Reader) override {
    ArrayRef<uint8_t> Data;
    if (auto EC = Reader.readLongestContiguousChunk(Data)) {
      llvm::consumeError(std::move(EC));
      return 0;
    }
    return Data.data() - SectionContents.bytes_begin();
  }

  void printRelocatedField(StringRef Label, uint32_t RelocOffset,
                           uint32_t Offset, StringRef *RelocSym) override {
    CD.printRelocatedField(Label, Sec, RelocOffset, Offset, RelocSym);
  }

  void printBinaryBlockWithRelocs(StringRef Label,
                                  ArrayRef<uint8_t> Block) override {
    StringRef SBlock(reinterpret_cast<const char *>(Block.data()),
                     Block.size());
    if (opts::CodeViewSubsectionBytes)
      CD.printBinaryBlockWithRelocs(Label, SR, SectionContents, SBlock);
  }

  StringRef getFileNameForFileOffset(uint32_t FileOffset) override {
    return CD.getFileNameForFileOffset(FileOffset);
  }

  DebugStringTableSubsectionRef getStringTable() override {
    return CD.CVStringTable;
  }

private:
  COFFDumper &CD;
  const SectionRef &SR;
  const coff_section *Sec;
  StringRef SectionContents;
};

} // end namespace

namespace llvm {

std::unique_ptr<ObjDumper> createCOFFDumper(const object::COFFObjectFile &Obj,
                                            ScopedPrinter &Writer) {
  return std::make_unique<COFFDumper>(&Obj, Writer);
}

} // namespace llvm

// Given a section and an offset into this section the function returns the
// symbol used for the relocation at the offset.
std::error_code COFFDumper::resolveSymbol(const coff_section *Section,
                                          uint64_t Offset, SymbolRef &Sym) {
  cacheRelocations();
  const auto &Relocations = RelocMap[Section];
  auto SymI = Obj->symbol_end();
  for (const auto &Relocation : Relocations) {
    uint64_t RelocationOffset = Relocation.getOffset();

    if (RelocationOffset == Offset) {
      SymI = Relocation.getSymbol();
      break;
    }
  }
  if (SymI == Obj->symbol_end())
    return inconvertibleErrorCode();
  Sym = *SymI;
  return std::error_code();
}

// Given a section and an offset into this section the function returns the name
// of the symbol used for the relocation at the offset.
std::error_code COFFDumper::resolveSymbolName(const coff_section *Section,
                                              uint64_t Offset,
                                              StringRef &Name) {
  SymbolRef Symbol;
  if (std::error_code EC = resolveSymbol(Section, Offset, Symbol))
    return EC;
  Expected<StringRef> NameOrErr = Symbol.getName();
  if (!NameOrErr)
    return errorToErrorCode(NameOrErr.takeError());
  Name = *NameOrErr;
  return std::error_code();
}

// Helper for when you have a pointer to real data and you want to know about
// relocations against it.
std::error_code COFFDumper::resolveSymbolName(const coff_section *Section,
                                              StringRef SectionContents,
                                              const void *RelocPtr,
                                              StringRef &Name) {
  assert(SectionContents.data() < RelocPtr &&
         RelocPtr < SectionContents.data() + SectionContents.size() &&
         "pointer to relocated object is not in section");
  uint64_t Offset = ptrdiff_t(reinterpret_cast<const char *>(RelocPtr) -
                              SectionContents.data());
  return resolveSymbolName(Section, Offset, Name);
}

void COFFDumper::printRelocatedField(StringRef Label, const coff_section *Sec,
                                     uint32_t RelocOffset, uint32_t Offset,
                                     StringRef *RelocSym) {
  StringRef SymStorage;
  StringRef &Symbol = RelocSym ? *RelocSym : SymStorage;
  if (!resolveSymbolName(Sec, RelocOffset, Symbol))
    W.printSymbolOffset(Label, Symbol, Offset);
  else
    W.printHex(Label, RelocOffset);
}

void COFFDumper::printBinaryBlockWithRelocs(StringRef Label,
                                            const SectionRef &Sec,
                                            StringRef SectionContents,
                                            StringRef Block) {
  W.printBinaryBlock(Label, Block);

  assert(SectionContents.begin() < Block.begin() &&
         SectionContents.end() >= Block.end() &&
         "Block is not contained in SectionContents");
  uint64_t OffsetStart = Block.data() - SectionContents.data();
  uint64_t OffsetEnd = OffsetStart + Block.size();

  W.flush();
  cacheRelocations();
  ListScope D(W, "BlockRelocations");
  const coff_section *Section = Obj->getCOFFSection(Sec);
  const auto &Relocations = RelocMap[Section];
  for (const auto &Relocation : Relocations) {
    uint64_t RelocationOffset = Relocation.getOffset();
    if (OffsetStart <= RelocationOffset && RelocationOffset < OffsetEnd)
      printRelocation(Sec, Relocation, OffsetStart);
  }
}

const EnumEntry<COFF::MachineTypes> ImageFileMachineType[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_UNKNOWN  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_AM33     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_AMD64    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARM      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARM64    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARM64EC  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARM64X   ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARMNT    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_EBC      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_I386     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_IA64     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_M32R     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPS16   ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPSFPU  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPSFPU16),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_POWERPC  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_POWERPCFP),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_R4000    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH3      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH3DSP   ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH4      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH5      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_THUMB    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_WCEMIPSV2)
};

const EnumEntry<COFF::Characteristics> ImageFileCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_RELOCS_STRIPPED        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_EXECUTABLE_IMAGE       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LINE_NUMS_STRIPPED     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LOCAL_SYMS_STRIPPED    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_AGGRESSIVE_WS_TRIM     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LARGE_ADDRESS_AWARE    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_BYTES_REVERSED_LO      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_32BIT_MACHINE          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_DEBUG_STRIPPED         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_NET_RUN_FROM_SWAP      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_SYSTEM                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_DLL                    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_UP_SYSTEM_ONLY         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_BYTES_REVERSED_HI      )
};

const EnumEntry<COFF::WindowsSubsystem> PEWindowsSubsystem[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_UNKNOWN                ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_NATIVE                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_GUI            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_CUI            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_POSIX_CUI              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_CE_GUI         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_APPLICATION        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_ROM                ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_XBOX                   ),
};

const EnumEntry<COFF::DLLCharacteristics> PEDLLCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NX_COMPAT            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_SEH               ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_BIND              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_APPCONTAINER         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_GUARD_CF             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE),
};

static const EnumEntry<COFF::ExtendedDLLCharacteristics>
    PEExtendedDLLCharacteristics[] = {
        LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT),
};

static const EnumEntry<COFF::SectionCharacteristics>
ImageSectionCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NOLOAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NO_PAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_CODE              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_INITIALIZED_DATA  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_UNINITIALIZED_DATA),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_OTHER             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_INFO              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_REMOVE            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_COMDAT            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_GPREL                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_PURGEABLE         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_16BIT             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_LOCKED            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_PRELOAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_16BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_32BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_64BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_128BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_256BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_512BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1024BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2048BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4096BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8192BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_NRELOC_OVFL       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_DISCARDABLE       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_CACHED        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_PAGED         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_SHARED            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_EXECUTE           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_READ              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_WRITE             )
};

const EnumEntry<COFF::SymbolBaseType> ImageSymType[] = {
  { "Null"  , COFF::IMAGE_SYM_TYPE_NULL   },
  { "Void"  , COFF::IMAGE_SYM_TYPE_VOID   },
  { "Char"  , COFF::IMAGE_SYM_TYPE_CHAR   },
  { "Short" , COFF::IMAGE_SYM_TYPE_SHORT  },
  { "Int"   , COFF::IMAGE_SYM_TYPE_INT    },
  { "Long"  , COFF::IMAGE_SYM_TYPE_LONG   },
  { "Float" , COFF::IMAGE_SYM_TYPE_FLOAT  },
  { "Double", COFF::IMAGE_SYM_TYPE_DOUBLE },
  { "Struct", COFF::IMAGE_SYM_TYPE_STRUCT },
  { "Union" , COFF::IMAGE_SYM_TYPE_UNION  },
  { "Enum"  , COFF::IMAGE_SYM_TYPE_ENUM   },
  { "MOE"   , COFF::IMAGE_SYM_TYPE_MOE    },
  { "Byte"  , COFF::IMAGE_SYM_TYPE_BYTE   },
  { "Word"  , COFF::IMAGE_SYM_TYPE_WORD   },
  { "UInt"  , COFF::IMAGE_SYM_TYPE_UINT   },
  { "DWord" , COFF::IMAGE_SYM_TYPE_DWORD  }
};

const EnumEntry<COFF::SymbolComplexType> ImageSymDType[] = {
  { "Null"    , COFF::IMAGE_SYM_DTYPE_NULL     },
  { "Pointer" , COFF::IMAGE_SYM_DTYPE_POINTER  },
  { "Function", COFF::IMAGE_SYM_DTYPE_FUNCTION },
  { "Array"   , COFF::IMAGE_SYM_DTYPE_ARRAY    }
};

const EnumEntry<COFF::SymbolStorageClass> ImageSymClass[] = {
  { "EndOfFunction"  , COFF::IMAGE_SYM_CLASS_END_OF_FUNCTION  },
  { "Null"           , COFF::IMAGE_SYM_CLASS_NULL             },
  { "Automatic"      , COFF::IMAGE_SYM_CLASS_AUTOMATIC        },
  { "External"       , COFF::IMAGE_SYM_CLASS_EXTERNAL         },
  { "Static"         , COFF::IMAGE_SYM_CLASS_STATIC           },
  { "Register"       , COFF::IMAGE_SYM_CLASS_REGISTER         },
  { "ExternalDef"    , COFF::IMAGE_SYM_CLASS_EXTERNAL_DEF     },
  { "Label"          , COFF::IMAGE_SYM_CLASS_LABEL            },
  { "UndefinedLabel" , COFF::IMAGE_SYM_CLASS_UNDEFINED_LABEL  },
  { "MemberOfStruct" , COFF::IMAGE_SYM_CLASS_MEMBER_OF_STRUCT },
  { "Argument"       , COFF::IMAGE_SYM_CLASS_ARGUMENT         },
  { "StructTag"      , COFF::IMAGE_SYM_CLASS_STRUCT_TAG       },
  { "MemberOfUnion"  , COFF::IMAGE_SYM_CLASS_MEMBER_OF_UNION  },
  { "UnionTag"       , COFF::IMAGE_SYM_CLASS_UNION_TAG        },
  { "TypeDefinition" , COFF::IMAGE_SYM_CLASS_TYPE_DEFINITION  },
  { "UndefinedStatic", COFF::IMAGE_SYM_CLASS_UNDEFINED_STATIC },
  { "EnumTag"        , COFF::IMAGE_SYM_CLASS_ENUM_TAG         },
  { "MemberOfEnum"   , COFF::IMAGE_SYM_CLASS_MEMBER_OF_ENUM   },
  { "RegisterParam"  , COFF::IMAGE_SYM_CLASS_REGISTER_PARAM   },
  { "BitField"       , COFF::IMAGE_SYM_CLASS_BIT_FIELD        },
  { "Block"          , COFF::IMAGE_SYM_CLASS_BLOCK            },
  { "Function"       , COFF::IMAGE_SYM_CLASS_FUNCTION         },
  { "EndOfStruct"    , COFF::IMAGE_SYM_CLASS_END_OF_STRUCT    },
  { "File"           , COFF::IMAGE_SYM_CLASS_FILE             },
  { "Section"        , COFF::IMAGE_SYM_CLASS_SECTION          },
  { "WeakExternal"   , COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL    },
  { "CLRToken"       , COFF::IMAGE_SYM_CLASS_CLR_TOKEN        }
};

const EnumEntry<COFF::COMDATType> ImageCOMDATSelect[] = {
  { "NoDuplicates", COFF::IMAGE_COMDAT_SELECT_NODUPLICATES },
  { "Any"         , COFF::IMAGE_COMDAT_SELECT_ANY          },
  { "SameSize"    , COFF::IMAGE_COMDAT_SELECT_SAME_SIZE    },
  { "ExactMatch"  , COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH  },
  { "Associative" , COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE  },
  { "Largest"     , COFF::IMAGE_COMDAT_SELECT_LARGEST      },
  { "Newest"      , COFF::IMAGE_COMDAT_SELECT_NEWEST       }
};

const EnumEntry<COFF::DebugType> ImageDebugType[] = {
    {"Unknown", COFF::IMAGE_DEBUG_TYPE_UNKNOWN},
    {"COFF", COFF::IMAGE_DEBUG_TYPE_COFF},
    {"CodeView", COFF::IMAGE_DEBUG_TYPE_CODEVIEW},
    {"FPO", COFF::IMAGE_DEBUG_TYPE_FPO},
    {"Misc", COFF::IMAGE_DEBUG_TYPE_MISC},
    {"Exception", COFF::IMAGE_DEBUG_TYPE_EXCEPTION},
    {"Fixup", COFF::IMAGE_DEBUG_TYPE_FIXUP},
    {"OmapToSrc", COFF::IMAGE_DEBUG_TYPE_OMAP_TO_SRC},
    {"OmapFromSrc", COFF::IMAGE_DEBUG_TYPE_OMAP_FROM_SRC},
    {"Borland", COFF::IMAGE_DEBUG_TYPE_BORLAND},
    {"Reserved10", COFF::IMAGE_DEBUG_TYPE_RESERVED10},
    {"CLSID", COFF::IMAGE_DEBUG_TYPE_CLSID},
    {"VCFeature", COFF::IMAGE_DEBUG_TYPE_VC_FEATURE},
    {"POGO", COFF::IMAGE_DEBUG_TYPE_POGO},
    {"ILTCG", COFF::IMAGE_DEBUG_TYPE_ILTCG},
    {"MPX", COFF::IMAGE_DEBUG_TYPE_MPX},
    {"Repro", COFF::IMAGE_DEBUG_TYPE_REPRO},
    {"ExtendedDLLCharacteristics",
     COFF::IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS},
};

static const EnumEntry<COFF::WeakExternalCharacteristics>
WeakExternalCharacteristics[] = {
  { "NoLibrary"       , COFF::IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY },
  { "Library"         , COFF::IMAGE_WEAK_EXTERN_SEARCH_LIBRARY   },
  { "Alias"           , COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS     },
  { "AntiDependency"  , COFF::IMAGE_WEAK_EXTERN_ANTI_DEPENDENCY  },
};

const EnumEntry<uint32_t> SubSectionTypes[] = {
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, Symbols),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, Lines),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, StringTable),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, FileChecksums),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, FrameData),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, InlineeLines),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeImports),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeExports),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, ILLines),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, FuncMDTokenMap),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, TypeMDTokenMap),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, MergedAssemblyInput),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, CoffSymbolRVA),
};

const EnumEntry<uint32_t> FrameDataFlags[] = {
    LLVM_READOBJ_ENUM_ENT(FrameData, HasSEH),
    LLVM_READOBJ_ENUM_ENT(FrameData, HasEH),
    LLVM_READOBJ_ENUM_ENT(FrameData, IsFunctionStart),
};

const EnumEntry<uint8_t> FileChecksumKindNames[] = {
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, None),
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, MD5),
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, SHA1),
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, SHA256),
};

const EnumEntry<uint32_t> PELoadConfigGuardFlags[] = {
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags, CF_INSTRUMENTED),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags, CFW_INSTRUMENTED),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags, CF_FUNCTION_TABLE_PRESENT),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags, SECURITY_COOKIE_UNUSED),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags, PROTECT_DELAYLOAD_IAT),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                DELAYLOAD_IAT_IN_ITS_OWN_SECTION),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_EXPORT_SUPPRESSION_INFO_PRESENT),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags, CF_ENABLE_EXPORT_SUPPRESSION),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags, CF_LONGJUMP_TABLE_PRESENT),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                EH_CONTINUATION_TABLE_PRESENT),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_5BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_6BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_7BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_8BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_9BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_10BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_11BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_12BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_13BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_14BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_15BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_16BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_17BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_18BYTES),
    LLVM_READOBJ_ENUM_CLASS_ENT(COFF::GuardFlags,
                                CF_FUNCTION_TABLE_SIZE_19BYTES),
};

template <typename T>
static std::error_code getSymbolAuxData(const COFFObjectFile *Obj,
                                        COFFSymbolRef Symbol,
                                        uint8_t AuxSymbolIdx, const T *&Aux) {
  ArrayRef<uint8_t> AuxData = Obj->getSymbolAuxData(Symbol);
  AuxData = AuxData.slice(AuxSymbolIdx * Obj->getSymbolTableEntrySize());
  Aux = reinterpret_cast<const T*>(AuxData.data());
  return std::error_code();
}

void COFFDumper::cacheRelocations() {
  if (RelocCached)
    return;
  RelocCached = true;

  for (const SectionRef &S : Obj->sections()) {
    const coff_section *Section = Obj->getCOFFSection(S);

    append_range(RelocMap[Section], S.relocations());

    // Sort relocations by address.
    llvm::sort(RelocMap[Section], [](RelocationRef L, RelocationRef R) {
      return L.getOffset() < R.getOffset();
    });
  }
}

void COFFDumper::printDataDirectory(uint32_t Index,
                                    const std::string &FieldName) {
  const data_directory *Data = Obj->getDataDirectory(Index);
  if (!Data)
    return;
  W.printHex(FieldName + "RVA", Data->RelativeVirtualAddress);
  W.printHex(FieldName + "Size", Data->Size);
}

void COFFDumper::printFileHeaders() {
  time_t TDS = Obj->getTimeDateStamp();
  char FormattedTime[20] = { };
  strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));

  {
    DictScope D(W, "ImageFileHeader");
    W.printEnum("Machine", Obj->getMachine(), ArrayRef(ImageFileMachineType));
    W.printNumber("SectionCount", Obj->getNumberOfSections());
    W.printHex   ("TimeDateStamp", FormattedTime, Obj->getTimeDateStamp());
    W.printHex   ("PointerToSymbolTable", Obj->getPointerToSymbolTable());
    W.printNumber("SymbolCount", Obj->getNumberOfSymbols());
    W.printNumber("StringTableSize", Obj->getStringTableSize());
    W.printNumber("OptionalHeaderSize", Obj->getSizeOfOptionalHeader());
    W.printFlags("Characteristics", Obj->getCharacteristics(),
                 ArrayRef(ImageFileCharacteristics));
  }

  // Print PE header. This header does not exist if this is an object file and
  // not an executable.
  if (const pe32_header *PEHeader = Obj->getPE32Header())
    printPEHeader<pe32_header>(PEHeader);

  if (const pe32plus_header *PEPlusHeader = Obj->getPE32PlusHeader())
    printPEHeader<pe32plus_header>(PEPlusHeader);

  if (const dos_header *DH = Obj->getDOSHeader())
    printDOSHeader(DH);
}

void COFFDumper::printDOSHeader(const dos_header *DH) {
  DictScope D(W, "DOSHeader");
  W.printString("Magic", StringRef(DH->Magic, sizeof(DH->Magic)));
  W.printNumber("UsedBytesInTheLastPage", DH->UsedBytesInTheLastPage);
  W.printNumber("FileSizeInPages", DH->FileSizeInPages);
  W.printNumber("NumberOfRelocationItems", DH->NumberOfRelocationItems);
  W.printNumber("HeaderSizeInParagraphs", DH->HeaderSizeInParagraphs);
  W.printNumber("MinimumExtraParagraphs", DH->MinimumExtraParagraphs);
  W.printNumber("MaximumExtraParagraphs", DH->MaximumExtraParagraphs);
  W.printNumber("InitialRelativeSS", DH->InitialRelativeSS);
  W.printNumber("InitialSP", DH->InitialSP);
  W.printNumber("Checksum", DH->Checksum);
  W.printNumber("InitialIP", DH->InitialIP);
  W.printNumber("InitialRelativeCS", DH->InitialRelativeCS);
  W.printNumber("AddressOfRelocationTable", DH->AddressOfRelocationTable);
  W.printNumber("OverlayNumber", DH->OverlayNumber);
  W.printNumber("OEMid", DH->OEMid);
  W.printNumber("OEMinfo", DH->OEMinfo);
  W.printNumber("AddressOfNewExeHeader", DH->AddressOfNewExeHeader);
}

template <class PEHeader>
void COFFDumper::printPEHeader(const PEHeader *Hdr) {
  DictScope D(W, "ImageOptionalHeader");
  W.printHex   ("Magic", Hdr->Magic);
  W.printNumber("MajorLinkerVersion", Hdr->MajorLinkerVersion);
  W.printNumber("MinorLinkerVersion", Hdr->MinorLinkerVersion);
  W.printNumber("SizeOfCode", Hdr->SizeOfCode);
  W.printNumber("SizeOfInitializedData", Hdr->SizeOfInitializedData);
  W.printNumber("SizeOfUninitializedData", Hdr->SizeOfUninitializedData);
  W.printHex   ("AddressOfEntryPoint", Hdr->AddressOfEntryPoint);
  W.printHex   ("BaseOfCode", Hdr->BaseOfCode);
  printBaseOfDataField(Hdr);
  W.printHex   ("ImageBase", Hdr->ImageBase);
  W.printNumber("SectionAlignment", Hdr->SectionAlignment);
  W.printNumber("FileAlignment", Hdr->FileAlignment);
  W.printNumber("MajorOperatingSystemVersion",
                Hdr->MajorOperatingSystemVersion);
  W.printNumber("MinorOperatingSystemVersion",
                Hdr->MinorOperatingSystemVersion);
  W.printNumber("MajorImageVersion", Hdr->MajorImageVersion);
  W.printNumber("MinorImageVersion", Hdr->MinorImageVersion);
  W.printNumber("MajorSubsystemVersion", Hdr->MajorSubsystemVersion);
  W.printNumber("MinorSubsystemVersion", Hdr->MinorSubsystemVersion);
  W.printNumber("SizeOfImage", Hdr->SizeOfImage);
  W.printNumber("SizeOfHeaders", Hdr->SizeOfHeaders);
  W.printHex   ("CheckSum", Hdr->CheckSum);
  W.printEnum("Subsystem", Hdr->Subsystem, ArrayRef(PEWindowsSubsystem));
  W.printFlags("Characteristics", Hdr->DLLCharacteristics,
               ArrayRef(PEDLLCharacteristics));
  W.printNumber("SizeOfStackReserve", Hdr->SizeOfStackReserve);
  W.printNumber("SizeOfStackCommit", Hdr->SizeOfStackCommit);
  W.printNumber("SizeOfHeapReserve", Hdr->SizeOfHeapReserve);
  W.printNumber("SizeOfHeapCommit", Hdr->SizeOfHeapCommit);
  W.printNumber("NumberOfRvaAndSize", Hdr->NumberOfRvaAndSize);

  if (Hdr->NumberOfRvaAndSize > 0) {
    DictScope D(W, "DataDirectory");
    static const char * const directory[] = {
      "ExportTable", "ImportTable", "ResourceTable", "ExceptionTable",
      "CertificateTable", "BaseRelocationTable", "Debug", "Architecture",
      "GlobalPtr", "TLSTable", "LoadConfigTable", "BoundImport", "IAT",
      "DelayImportDescriptor", "CLRRuntimeHeader", "Reserved"
    };

    for (uint32_t i = 0; i < Hdr->NumberOfRvaAndSize; ++i)
      if (i < std::size(directory))
        printDataDirectory(i, directory[i]);
      else
        printDataDirectory(i, "Unknown");
  }
}

void COFFDumper::printCOFFDebugDirectory() {
  ListScope LS(W, "DebugDirectory");
  for (const debug_directory &D : Obj->debug_directories()) {
    char FormattedTime[20] = {};
    time_t TDS = D.TimeDateStamp;
    strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));
    DictScope S(W, "DebugEntry");
    W.printHex("Characteristics", D.Characteristics);
    W.printHex("TimeDateStamp", FormattedTime, D.TimeDateStamp);
    W.printHex("MajorVersion", D.MajorVersion);
    W.printHex("MinorVersion", D.MinorVersion);
    W.printEnum("Type", D.Type, ArrayRef(ImageDebugType));
    W.printHex("SizeOfData", D.SizeOfData);
    W.printHex("AddressOfRawData", D.AddressOfRawData);
    W.printHex("PointerToRawData", D.PointerToRawData);
    // Ideally, if D.AddressOfRawData == 0, we should try to load the payload
    // using D.PointerToRawData instead.
    if (D.AddressOfRawData == 0)
      continue;
    if (D.Type == COFF::IMAGE_DEBUG_TYPE_CODEVIEW) {
      const codeview::DebugInfo *DebugInfo;
      StringRef PDBFileName;
      if (Error E = Obj->getDebugPDBInfo(&D, DebugInfo, PDBFileName))
        reportError(std::move(E), Obj->getFileName());

      DictScope PDBScope(W, "PDBInfo");
      W.printHex("PDBSignature", DebugInfo->Signature.CVSignature);
      if (DebugInfo->Signature.CVSignature == OMF::Signature::PDB70) {
        W.printString(
            "PDBGUID",
            formatv("{0}", fmt_guid(DebugInfo->PDB70.Signature)).str());
        W.printNumber("PDBAge", DebugInfo->PDB70.Age);
        W.printString("PDBFileName", PDBFileName);
      }
    } else if (D.SizeOfData != 0) {
      // FIXME: Data visualization for IMAGE_DEBUG_TYPE_VC_FEATURE and
      // IMAGE_DEBUG_TYPE_POGO?
      ArrayRef<uint8_t> RawData;
      if (Error E = Obj->getRvaAndSizeAsBytes(D.AddressOfRawData,
                                                         D.SizeOfData, RawData))
        reportError(std::move(E), Obj->getFileName());
      if (D.Type == COFF::IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS) {
        // FIXME right now the only possible value would fit in 8 bits,
        // but that might change in the future
        uint16_t Characteristics = RawData[0];
        W.printFlags("ExtendedCharacteristics", Characteristics,
                     ArrayRef(PEExtendedDLLCharacteristics));
      }
      W.printBinaryBlock("RawData", RawData);
    }
  }
}

void COFFDumper::printRVATable(uint64_t TableVA, uint64_t Count,
                               uint64_t EntrySize, PrintExtraCB PrintExtra) {
  uintptr_t TableStart, TableEnd;
  if (Error E = Obj->getVaPtr(TableVA, TableStart))
    reportError(std::move(E), Obj->getFileName());
  if (Error E =
          Obj->getVaPtr(TableVA + Count * EntrySize - 1, TableEnd))
    reportError(std::move(E), Obj->getFileName());
  TableEnd++;
  for (uintptr_t I = TableStart; I < TableEnd; I += EntrySize) {
    uint32_t RVA = *reinterpret_cast<const ulittle32_t *>(I);
    raw_ostream &OS = W.startLine();
    OS << W.hex(Obj->getImageBase() + RVA);
    if (PrintExtra)
      PrintExtra(OS, reinterpret_cast<const uint8_t *>(I));
    OS << '\n';
  }
}

void COFFDumper::printCOFFLoadConfig() {
  LoadConfigTables Tables;
  if (Obj->is64())
    printCOFFLoadConfig(Obj->getLoadConfig64(), Tables);
  else
    printCOFFLoadConfig(Obj->getLoadConfig32(), Tables);

  if (auto CHPE = Obj->getCHPEMetadata()) {
    ListScope LS(W, "CHPEMetadata");
    W.printHex("Version", CHPE->Version);

    if (CHPE->CodeMapCount) {
      ListScope CMLS(W, "CodeMap");

      uintptr_t CodeMapInt;
      if (Error E = Obj->getRvaPtr(CHPE->CodeMap, CodeMapInt))
        reportError(std::move(E), Obj->getFileName());
      auto CodeMap = reinterpret_cast<const chpe_range_entry *>(CodeMapInt);
      for (uint32_t i = 0; i < CHPE->CodeMapCount; i++) {
        uint32_t Start = CodeMap[i].getStart();
        W.startLine() << W.hex(Start) << " - "
                      << W.hex(Start + CodeMap[i].Length) << "  ";
        switch (CodeMap[i].getType()) {
        case chpe_range_type::Arm64:
          W.getOStream() << "ARM64\n";
          break;
        case chpe_range_type::Arm64EC:
          W.getOStream() << "ARM64EC\n";
          break;
        case chpe_range_type::Amd64:
          W.getOStream() << "X64\n";
          break;
        default:
          W.getOStream() << W.hex(CodeMap[i].StartOffset & 3) << "\n";
          break;
        }
      }
    } else {
      W.printNumber("CodeMap", CHPE->CodeMap);
    }

    if (CHPE->CodeRangesToEntryPointsCount) {
      ListScope CRLS(W, "CodeRangesToEntryPoints");

      uintptr_t CodeRangesInt;
      if (Error E =
              Obj->getRvaPtr(CHPE->CodeRangesToEntryPoints, CodeRangesInt))
        reportError(std::move(E), Obj->getFileName());
      auto CodeRanges =
          reinterpret_cast<const chpe_code_range_entry *>(CodeRangesInt);
      for (uint32_t i = 0; i < CHPE->CodeRangesToEntryPointsCount; i++) {
        W.startLine() << W.hex(CodeRanges[i].StartRva) << " - "
                      << W.hex(CodeRanges[i].EndRva) << " -> "
                      << W.hex(CodeRanges[i].EntryPoint) << "\n";
      }
    } else {
      W.printNumber("CodeRangesToEntryPoints", CHPE->CodeRangesToEntryPoints);
    }

    if (CHPE->RedirectionMetadataCount) {
      ListScope RMLS(W, "RedirectionMetadata");

      uintptr_t RedirMetadataInt;
      if (Error E = Obj->getRvaPtr(CHPE->RedirectionMetadata, RedirMetadataInt))
        reportError(std::move(E), Obj->getFileName());
      auto RedirMetadata =
          reinterpret_cast<const chpe_redirection_entry *>(RedirMetadataInt);
      for (uint32_t i = 0; i < CHPE->RedirectionMetadataCount; i++) {
        W.startLine() << W.hex(RedirMetadata[i].Source) << " -> "
                      << W.hex(RedirMetadata[i].Destination) << "\n";
      }
    } else {
      W.printNumber("RedirectionMetadata", CHPE->RedirectionMetadata);
    }

    W.printHex("__os_arm64x_dispatch_call_no_redirect",
               CHPE->__os_arm64x_dispatch_call_no_redirect);
    W.printHex("__os_arm64x_dispatch_ret", CHPE->__os_arm64x_dispatch_ret);
    W.printHex("__os_arm64x_dispatch_call", CHPE->__os_arm64x_dispatch_call);
    W.printHex("__os_arm64x_dispatch_icall", CHPE->__os_arm64x_dispatch_icall);
    W.printHex("__os_arm64x_dispatch_icall_cfg",
               CHPE->__os_arm64x_dispatch_icall_cfg);
    W.printHex("AlternateEntryPoint", CHPE->AlternateEntryPoint);
    W.printHex("AuxiliaryIAT", CHPE->AuxiliaryIAT);
    W.printHex("GetX64InformationFunctionPointer",
               CHPE->GetX64InformationFunctionPointer);
    W.printHex("SetX64InformationFunctionPointer",
               CHPE->SetX64InformationFunctionPointer);
    W.printHex("ExtraRFETable", CHPE->ExtraRFETable);
    W.printHex("ExtraRFETableSize", CHPE->ExtraRFETableSize);
    W.printHex("__os_arm64x_dispatch_fptr", CHPE->__os_arm64x_dispatch_fptr);
    W.printHex("AuxiliaryIATCopy", CHPE->AuxiliaryIATCopy);
  }

  if (Tables.SEHTableVA) {
    ListScope LS(W, "SEHTable");
    printRVATable(Tables.SEHTableVA, Tables.SEHTableCount, 4);
  }

  auto PrintGuardFlags = [](raw_ostream &OS, const uint8_t *Entry) {
    uint8_t Flags = *reinterpret_cast<const uint8_t *>(Entry + 4);
    if (Flags)
      OS << " flags " << utohexstr(Flags);
  };

  // The stride gives the number of extra bytes in addition to the 4-byte
  // RVA of each entry in the table. As of writing only a 1-byte extra flag
  // has been defined.
  uint32_t Stride = Tables.GuardFlags >> 28;
  PrintExtraCB PrintExtra = Stride == 1 ? +PrintGuardFlags : nullptr;

  if (Tables.GuardFidTableVA) {
    ListScope LS(W, "GuardFidTable");
    printRVATable(Tables.GuardFidTableVA, Tables.GuardFidTableCount,
                  4 + Stride, PrintExtra);
  }

  if (Tables.GuardIatTableVA) {
    ListScope LS(W, "GuardIatTable");
    printRVATable(Tables.GuardIatTableVA, Tables.GuardIatTableCount,
                  4 + Stride, PrintExtra);
  }

  if (Tables.GuardLJmpTableVA) {
    ListScope LS(W, "GuardLJmpTable");
    printRVATable(Tables.GuardLJmpTableVA, Tables.GuardLJmpTableCount,
                  4 + Stride, PrintExtra);
  }

  if (Tables.GuardEHContTableVA) {
    ListScope LS(W, "GuardEHContTable");
    printRVATable(Tables.GuardEHContTableVA, Tables.GuardEHContTableCount,
                  4 + Stride, PrintExtra);
  }
}

template <typename T>
void COFFDumper::printCOFFLoadConfig(const T *Conf, LoadConfigTables &Tables) {
  if (!Conf)
    return;

  ListScope LS(W, "LoadConfig");
  char FormattedTime[20] = {};
  time_t TDS = Conf->TimeDateStamp;
  strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));
  W.printHex("Size", Conf->Size);

  // Print everything before SecurityCookie. The vast majority of images today
  // have all these fields.
  if (Conf->Size < offsetof(T, SEHandlerTable))
    return;
  W.printHex("TimeDateStamp", FormattedTime, TDS);
  W.printHex("MajorVersion", Conf->MajorVersion);
  W.printHex("MinorVersion", Conf->MinorVersion);
  W.printHex("GlobalFlagsClear", Conf->GlobalFlagsClear);
  W.printHex("GlobalFlagsSet", Conf->GlobalFlagsSet);
  W.printHex("CriticalSectionDefaultTimeout",
             Conf->CriticalSectionDefaultTimeout);
  W.printHex("DeCommitFreeBlockThreshold", Conf->DeCommitFreeBlockThreshold);
  W.printHex("DeCommitTotalFreeThreshold", Conf->DeCommitTotalFreeThreshold);
  W.printHex("LockPrefixTable", Conf->LockPrefixTable);
  W.printHex("MaximumAllocationSize", Conf->MaximumAllocationSize);
  W.printHex("VirtualMemoryThreshold", Conf->VirtualMemoryThreshold);
  W.printHex("ProcessHeapFlags", Conf->ProcessHeapFlags);
  W.printHex("ProcessAffinityMask", Conf->ProcessAffinityMask);
  W.printHex("CSDVersion", Conf->CSDVersion);
  W.printHex("DependentLoadFlags", Conf->DependentLoadFlags);
  W.printHex("EditList", Conf->EditList);
  W.printHex("SecurityCookie", Conf->SecurityCookie);

  // Print the safe SEH table if present.
  if (Conf->Size < offsetof(T, GuardCFCheckFunction))
    return;
  W.printHex("SEHandlerTable", Conf->SEHandlerTable);
  W.printNumber("SEHandlerCount", Conf->SEHandlerCount);

  Tables.SEHTableVA = Conf->SEHandlerTable;
  Tables.SEHTableCount = Conf->SEHandlerCount;

  // Print everything before CodeIntegrity. (2015)
  if (Conf->Size < offsetof(T, CodeIntegrity))
    return;
  W.printHex("GuardCFCheckFunction", Conf->GuardCFCheckFunction);
  W.printHex("GuardCFCheckDispatch", Conf->GuardCFCheckDispatch);
  W.printHex("GuardCFFunctionTable", Conf->GuardCFFunctionTable);
  W.printNumber("GuardCFFunctionCount", Conf->GuardCFFunctionCount);
  W.printFlags("GuardFlags", Conf->GuardFlags, ArrayRef(PELoadConfigGuardFlags),
               (uint32_t)COFF::GuardFlags::CF_FUNCTION_TABLE_SIZE_MASK);

  Tables.GuardFidTableVA = Conf->GuardCFFunctionTable;
  Tables.GuardFidTableCount = Conf->GuardCFFunctionCount;
  Tables.GuardFlags = Conf->GuardFlags;

  // Print everything before Reserved3. (2017)
  if (Conf->Size < offsetof(T, Reserved3))
    return;
  W.printHex("GuardAddressTakenIatEntryTable",
             Conf->GuardAddressTakenIatEntryTable);
  W.printNumber("GuardAddressTakenIatEntryCount",
                Conf->GuardAddressTakenIatEntryCount);
  W.printHex("GuardLongJumpTargetTable", Conf->GuardLongJumpTargetTable);
  W.printNumber("GuardLongJumpTargetCount", Conf->GuardLongJumpTargetCount);
  W.printHex("DynamicValueRelocTable", Conf->DynamicValueRelocTable);
  W.printHex("CHPEMetadataPointer", Conf->CHPEMetadataPointer);
  W.printHex("GuardRFFailureRoutine", Conf->GuardRFFailureRoutine);
  W.printHex("GuardRFFailureRoutineFunctionPointer",
             Conf->GuardRFFailureRoutineFunctionPointer);
  W.printHex("DynamicValueRelocTableOffset",
             Conf->DynamicValueRelocTableOffset);
  W.printNumber("DynamicValueRelocTableSection",
                Conf->DynamicValueRelocTableSection);
  W.printHex("GuardRFVerifyStackPointerFunctionPointer",
             Conf->GuardRFVerifyStackPointerFunctionPointer);
  W.printHex("HotPatchTableOffset", Conf->HotPatchTableOffset);

  Tables.GuardIatTableVA = Conf->GuardAddressTakenIatEntryTable;
  Tables.GuardIatTableCount = Conf->GuardAddressTakenIatEntryCount;

  Tables.GuardLJmpTableVA = Conf->GuardLongJumpTargetTable;
  Tables.GuardLJmpTableCount = Conf->GuardLongJumpTargetCount;

  // Print the rest. (2019)
  if (Conf->Size < sizeof(T))
    return;
  W.printHex("EnclaveConfigurationPointer", Conf->EnclaveConfigurationPointer);
  W.printHex("VolatileMetadataPointer", Conf->VolatileMetadataPointer);
  W.printHex("GuardEHContinuationTable", Conf->GuardEHContinuationTable);
  W.printNumber("GuardEHContinuationCount", Conf->GuardEHContinuationCount);

  Tables.GuardEHContTableVA = Conf->GuardEHContinuationTable;
  Tables.GuardEHContTableCount = Conf->GuardEHContinuationCount;
}

void COFFDumper::printBaseOfDataField(const pe32_header *Hdr) {
  W.printHex("BaseOfData", Hdr->BaseOfData);
}

void COFFDumper::printBaseOfDataField(const pe32plus_header *) {}

void COFFDumper::printCodeViewDebugInfo() {
  // Print types first to build CVUDTNames, then print symbols.
  for (const SectionRef &S : Obj->sections()) {
    StringRef SectionName = unwrapOrError(Obj->getFileName(), S.getName());
    // .debug$T is a standard CodeView type section, while .debug$P is the same
    // format but used for MSVC precompiled header object files.
    if (SectionName == ".debug$T" || SectionName == ".debug$P")
      printCodeViewTypeSection(SectionName, S);
  }
  for (const SectionRef &S : Obj->sections()) {
    StringRef SectionName = unwrapOrError(Obj->getFileName(), S.getName());
    if (SectionName == ".debug$S")
      printCodeViewSymbolSection(SectionName, S);
  }
}

void COFFDumper::initializeFileAndStringTables(BinaryStreamReader &Reader) {
  while (Reader.bytesRemaining() > 0 &&
         (!CVFileChecksumTable.valid() || !CVStringTable.valid())) {
    // The section consists of a number of subsection in the following format:
    // |SubSectionType|SubSectionSize|Contents...|
    uint32_t SubType, SubSectionSize;

    if (Error E = Reader.readInteger(SubType))
      reportError(std::move(E), Obj->getFileName());
    if (Error E = Reader.readInteger(SubSectionSize))
      reportError(std::move(E), Obj->getFileName());

    StringRef Contents;
    if (Error E = Reader.readFixedString(Contents, SubSectionSize))
      reportError(std::move(E), Obj->getFileName());

    BinaryStreamRef ST(Contents, llvm::endianness::little);
    switch (DebugSubsectionKind(SubType)) {
    case DebugSubsectionKind::FileChecksums:
      if (Error E = CVFileChecksumTable.initialize(ST))
        reportError(std::move(E), Obj->getFileName());
      break;
    case DebugSubsectionKind::StringTable:
      if (Error E = CVStringTable.initialize(ST))
        reportError(std::move(E), Obj->getFileName());
      break;
    default:
      break;
    }

    uint32_t PaddedSize = alignTo(SubSectionSize, 4);
    if (Error E = Reader.skip(PaddedSize - SubSectionSize))
      reportError(std::move(E), Obj->getFileName());
  }
}

void COFFDumper::printCodeViewSymbolSection(StringRef SectionName,
                                            const SectionRef &Section) {
  StringRef SectionContents =
      unwrapOrError(Obj->getFileName(), Section.getContents());
  StringRef Data = SectionContents;

  SmallVector<StringRef, 10> FunctionNames;
  StringMap<StringRef> FunctionLineTables;

  ListScope D(W, "CodeViewDebugInfo");
  // Print the section to allow correlation with printSectionHeaders.
  W.printNumber("Section", SectionName, Obj->getSectionID(Section));

  uint32_t Magic;
  if (Error E = consume(Data, Magic))
    reportError(std::move(E), Obj->getFileName());

  W.printHex("Magic", Magic);
  if (Magic != COFF::DEBUG_SECTION_MAGIC)
    reportError(errorCodeToError(object_error::parse_failed),
                Obj->getFileName());

  BinaryStreamReader FSReader(Data, llvm::endianness::little);
  initializeFileAndStringTables(FSReader);

  // TODO: Convert this over to using ModuleSubstreamVisitor.
  while (!Data.empty()) {
    // The section consists of a number of subsection in the following format:
    // |SubSectionType|SubSectionSize|Contents...|
    uint32_t SubType, SubSectionSize;
    if (Error E = consume(Data, SubType))
      reportError(std::move(E), Obj->getFileName());
    if (Error E = consume(Data, SubSectionSize))
      reportError(std::move(E), Obj->getFileName());

    ListScope S(W, "Subsection");
    // Dump the subsection as normal even if the ignore bit is set.
    if (SubType & SubsectionIgnoreFlag) {
      W.printHex("IgnoredSubsectionKind", SubType);
      SubType &= ~SubsectionIgnoreFlag;
    }
    W.printEnum("SubSectionType", SubType, ArrayRef(SubSectionTypes));
    W.printHex("SubSectionSize", SubSectionSize);

    // Get the contents of the subsection.
    if (SubSectionSize > Data.size())
      return reportError(errorCodeToError(object_error::parse_failed),
                         Obj->getFileName());
    StringRef Contents = Data.substr(0, SubSectionSize);

    // Add SubSectionSize to the current offset and align that offset to find
    // the next subsection.
    size_t SectionOffset = Data.data() - SectionContents.data();
    size_t NextOffset = SectionOffset + SubSectionSize;
    NextOffset = alignTo(NextOffset, 4);
    if (NextOffset > SectionContents.size())
      return reportError(errorCodeToError(object_error::parse_failed),
                         Obj->getFileName());
    Data = SectionContents.drop_front(NextOffset);

    // Optionally print the subsection bytes in case our parsing gets confused
    // later.
    if (opts::CodeViewSubsectionBytes)
      printBinaryBlockWithRelocs("SubSectionContents", Section, SectionContents,
                                 Contents);

    switch (DebugSubsectionKind(SubType)) {
    case DebugSubsectionKind::Symbols:
      printCodeViewSymbolsSubsection(Contents, Section, SectionContents);
      break;

    case DebugSubsectionKind::InlineeLines:
      printCodeViewInlineeLines(Contents);
      break;

    case DebugSubsectionKind::FileChecksums:
      printCodeViewFileChecksums(Contents);
      break;

    case DebugSubsectionKind::Lines: {
      // Holds a PC to file:line table.  Some data to parse this subsection is
      // stored in the other subsections, so just check sanity and store the
      // pointers for deferred processing.

      if (SubSectionSize < 12) {
        // There should be at least three words to store two function
        // relocations and size of the code.
        reportError(errorCodeToError(object_error::parse_failed),
                    Obj->getFileName());
        return;
      }

      StringRef LinkageName;
      if (std::error_code EC = resolveSymbolName(Obj->getCOFFSection(Section),
                                                 SectionOffset, LinkageName))
        reportError(errorCodeToError(EC), Obj->getFileName());

      W.printString("LinkageName", LinkageName);
      if (FunctionLineTables.count(LinkageName) != 0) {
        // Saw debug info for this function already?
        reportError(errorCodeToError(object_error::parse_failed),
                    Obj->getFileName());
        return;
      }

      FunctionLineTables[LinkageName] = Contents;
      FunctionNames.push_back(LinkageName);
      break;
    }
    case DebugSubsectionKind::FrameData: {
      // First four bytes is a relocation against the function.
      BinaryStreamReader SR(Contents, llvm::endianness::little);

      DebugFrameDataSubsectionRef FrameData;
      if (Error E = FrameData.initialize(SR))
        reportError(std::move(E), Obj->getFileName());

      StringRef LinkageName;
      if (std::error_code EC =
              resolveSymbolName(Obj->getCOFFSection(Section), SectionContents,
                                FrameData.getRelocPtr(), LinkageName))
        reportError(errorCodeToError(EC), Obj->getFileName());
      W.printString("LinkageName", LinkageName);

      // To find the active frame description, search this array for the
      // smallest PC range that includes the current PC.
      for (const auto &FD : FrameData) {
        StringRef FrameFunc = unwrapOrError(
            Obj->getFileName(), CVStringTable.getString(FD.FrameFunc));

        DictScope S(W, "FrameData");
        W.printHex("RvaStart", FD.RvaStart);
        W.printHex("CodeSize", FD.CodeSize);
        W.printHex("LocalSize", FD.LocalSize);
        W.printHex("ParamsSize", FD.ParamsSize);
        W.printHex("MaxStackSize", FD.MaxStackSize);
        W.printHex("PrologSize", FD.PrologSize);
        W.printHex("SavedRegsSize", FD.SavedRegsSize);
        W.printFlags("Flags", FD.Flags, ArrayRef(FrameDataFlags));

        // The FrameFunc string is a small RPN program. It can be broken up into
        // statements that end in the '=' operator, which assigns the value on
        // the top of the stack to the previously pushed variable. Variables can
        // be temporary values ($T0) or physical registers ($esp). Print each
        // assignment on its own line to make these programs easier to read.
        {
          ListScope FFS(W, "FrameFunc");
          while (!FrameFunc.empty()) {
            size_t EqOrEnd = FrameFunc.find('=');
            if (EqOrEnd == StringRef::npos)
              EqOrEnd = FrameFunc.size();
            else
              ++EqOrEnd;
            StringRef Stmt = FrameFunc.substr(0, EqOrEnd);
            W.printString(Stmt);
            FrameFunc = FrameFunc.drop_front(EqOrEnd).trim();
          }
        }
      }
      break;
    }

    // Do nothing for unrecognized subsections.
    default:
      break;
    }
    W.flush();
  }

  // Dump the line tables now that we've read all the subsections and know all
  // the required information.
  for (unsigned I = 0, E = FunctionNames.size(); I != E; ++I) {
    StringRef Name = FunctionNames[I];
    ListScope S(W, "FunctionLineTable");
    W.printString("LinkageName", Name);

    BinaryStreamReader Reader(FunctionLineTables[Name],
                              llvm::endianness::little);

    DebugLinesSubsectionRef LineInfo;
    if (Error E = LineInfo.initialize(Reader))
      reportError(std::move(E), Obj->getFileName());

    W.printHex("Flags", LineInfo.header()->Flags);
    W.printHex("CodeSize", LineInfo.header()->CodeSize);
    for (const auto &Entry : LineInfo) {

      ListScope S(W, "FilenameSegment");
      printFileNameForOffset("Filename", Entry.NameIndex);
      uint32_t ColumnIndex = 0;
      for (const auto &Line : Entry.LineNumbers) {
        if (Line.Offset >= LineInfo.header()->CodeSize) {
          reportError(errorCodeToError(object_error::parse_failed),
                      Obj->getFileName());
          return;
        }

        std::string PC = std::string(formatv("+{0:X}", uint32_t(Line.Offset)));
        ListScope PCScope(W, PC);
        codeview::LineInfo LI(Line.Flags);

        if (LI.isAlwaysStepInto())
          W.printString("StepInto", StringRef("Always"));
        else if (LI.isNeverStepInto())
          W.printString("StepInto", StringRef("Never"));
        else
          W.printNumber("LineNumberStart", LI.getStartLine());
        W.printNumber("LineNumberEndDelta", LI.getLineDelta());
        W.printBoolean("IsStatement", LI.isStatement());
        if (LineInfo.hasColumnInfo()) {
          W.printNumber("ColStart", Entry.Columns[ColumnIndex].StartColumn);
          W.printNumber("ColEnd", Entry.Columns[ColumnIndex].EndColumn);
          ++ColumnIndex;
        }
      }
    }
  }
}

void COFFDumper::printCodeViewSymbolsSubsection(StringRef Subsection,
                                                const SectionRef &Section,
                                                StringRef SectionContents) {
  ArrayRef<uint8_t> BinaryData(Subsection.bytes_begin(),
                               Subsection.bytes_end());
  auto CODD = std::make_unique<COFFObjectDumpDelegate>(*this, Section, Obj,
                                                        SectionContents);
  CVSymbolDumper CVSD(W, Types, CodeViewContainer::ObjectFile, std::move(CODD),
                      CompilationCPUType, opts::CodeViewSubsectionBytes);
  CVSymbolArray Symbols;
  BinaryStreamReader Reader(BinaryData, llvm::endianness::little);
  if (Error E = Reader.readArray(Symbols, Reader.getLength())) {
    W.flush();
    reportError(std::move(E), Obj->getFileName());
  }

  if (Error E = CVSD.dump(Symbols)) {
    W.flush();
    reportError(std::move(E), Obj->getFileName());
  }
  CompilationCPUType = CVSD.getCompilationCPUType();
  W.flush();
}

void COFFDumper::printCodeViewFileChecksums(StringRef Subsection) {
  BinaryStreamRef Stream(Subsection, llvm::endianness::little);
  DebugChecksumsSubsectionRef Checksums;
  if (Error E = Checksums.initialize(Stream))
    reportError(std::move(E), Obj->getFileName());

  for (auto &FC : Checksums) {
    DictScope S(W, "FileChecksum");

    StringRef Filename = unwrapOrError(
        Obj->getFileName(), CVStringTable.getString(FC.FileNameOffset));
    W.printHex("Filename", Filename, FC.FileNameOffset);
    W.printHex("ChecksumSize", FC.Checksum.size());
    W.printEnum("ChecksumKind", uint8_t(FC.Kind),
                ArrayRef(FileChecksumKindNames));

    W.printBinary("ChecksumBytes", FC.Checksum);
  }
}

void COFFDumper::printCodeViewInlineeLines(StringRef Subsection) {
  BinaryStreamReader SR(Subsection, llvm::endianness::little);
  DebugInlineeLinesSubsectionRef Lines;
  if (Error E = Lines.initialize(SR))
    reportError(std::move(E), Obj->getFileName());

  for (auto &Line : Lines) {
    DictScope S(W, "InlineeSourceLine");
    printTypeIndex("Inlinee", Line.Header->Inlinee);
    printFileNameForOffset("FileID", Line.Header->FileID);
    W.printNumber("SourceLineNum", Line.Header->SourceLineNum);

    if (Lines.hasExtraFiles()) {
      W.printNumber("ExtraFileCount", Line.ExtraFiles.size());
      ListScope ExtraFiles(W, "ExtraFiles");
      for (const auto &FID : Line.ExtraFiles) {
        printFileNameForOffset("FileID", FID);
      }
    }
  }
}

StringRef COFFDumper::getFileNameForFileOffset(uint32_t FileOffset) {
  // The file checksum subsection should precede all references to it.
  if (!CVFileChecksumTable.valid() || !CVStringTable.valid())
    reportError(errorCodeToError(object_error::parse_failed),
                Obj->getFileName());

  auto Iter = CVFileChecksumTable.getArray().at(FileOffset);

  // Check if the file checksum table offset is valid.
  if (Iter == CVFileChecksumTable.end())
    reportError(errorCodeToError(object_error::parse_failed),
                Obj->getFileName());

  return unwrapOrError(Obj->getFileName(),
                       CVStringTable.getString(Iter->FileNameOffset));
}

void COFFDumper::printFileNameForOffset(StringRef Label, uint32_t FileOffset) {
  W.printHex(Label, getFileNameForFileOffset(FileOffset), FileOffset);
}

void COFFDumper::mergeCodeViewTypes(MergingTypeTableBuilder &CVIDs,
                                    MergingTypeTableBuilder &CVTypes,
                                    GlobalTypeTableBuilder &GlobalCVIDs,
                                    GlobalTypeTableBuilder &GlobalCVTypes,
                                    bool GHash) {
  for (const SectionRef &S : Obj->sections()) {
    StringRef SectionName = unwrapOrError(Obj->getFileName(), S.getName());
    if (SectionName == ".debug$T") {
      StringRef Data = unwrapOrError(Obj->getFileName(), S.getContents());
      uint32_t Magic;
      if (Error E = consume(Data, Magic))
        reportError(std::move(E), Obj->getFileName());

      if (Magic != 4)
        reportError(errorCodeToError(object_error::parse_failed),
                    Obj->getFileName());

      CVTypeArray Types;
      BinaryStreamReader Reader(Data, llvm::endianness::little);
      if (auto EC = Reader.readArray(Types, Reader.getLength())) {
        consumeError(std::move(EC));
        W.flush();
        reportError(errorCodeToError(object_error::parse_failed),
                    Obj->getFileName());
      }
      SmallVector<TypeIndex, 128> SourceToDest;
      std::optional<PCHMergerInfo> PCHInfo;
      if (GHash) {
        std::vector<GloballyHashedType> Hashes =
            GloballyHashedType::hashTypes(Types);
        if (Error E =
                mergeTypeAndIdRecords(GlobalCVIDs, GlobalCVTypes, SourceToDest,
                                      Types, Hashes, PCHInfo))
          return reportError(std::move(E), Obj->getFileName());
      } else {
        if (Error E = mergeTypeAndIdRecords(CVIDs, CVTypes, SourceToDest, Types,
                                            PCHInfo))
          return reportError(std::move(E), Obj->getFileName());
      }
    }
  }
}

void COFFDumper::printCodeViewTypeSection(StringRef SectionName,
                                          const SectionRef &Section) {
  ListScope D(W, "CodeViewTypes");
  W.printNumber("Section", SectionName, Obj->getSectionID(Section));

  StringRef Data = unwrapOrError(Obj->getFileName(), Section.getContents());
  if (opts::CodeViewSubsectionBytes)
    W.printBinaryBlock("Data", Data);

  uint32_t Magic;
  if (Error E = consume(Data, Magic))
    reportError(std::move(E), Obj->getFileName());

  W.printHex("Magic", Magic);
  if (Magic != COFF::DEBUG_SECTION_MAGIC)
    reportError(errorCodeToError(object_error::parse_failed),
                Obj->getFileName());

  Types.reset(Data, 100);

  TypeDumpVisitor TDV(Types, &W, opts::CodeViewSubsectionBytes);
  if (Error E = codeview::visitTypeStream(Types, TDV))
    reportError(std::move(E), Obj->getFileName());

  W.flush();
}

void COFFDumper::printSectionHeaders() {
  ListScope SectionsD(W, "Sections");
  int SectionNumber = 0;
  for (const SectionRef &Sec : Obj->sections()) {
    ++SectionNumber;
    const coff_section *Section = Obj->getCOFFSection(Sec);

    StringRef Name = unwrapOrError(Obj->getFileName(), Sec.getName());

    DictScope D(W, "Section");
    W.printNumber("Number", SectionNumber);
    W.printBinary("Name", Name, Section->Name);
    W.printHex   ("VirtualSize", Section->VirtualSize);
    W.printHex   ("VirtualAddress", Section->VirtualAddress);
    W.printNumber("RawDataSize", Section->SizeOfRawData);
    W.printHex   ("PointerToRawData", Section->PointerToRawData);
    W.printHex   ("PointerToRelocations", Section->PointerToRelocations);
    W.printHex   ("PointerToLineNumbers", Section->PointerToLinenumbers);
    W.printNumber("RelocationCount", Section->NumberOfRelocations);
    W.printNumber("LineNumberCount", Section->NumberOfLinenumbers);
    W.printFlags("Characteristics", Section->Characteristics,
                 ArrayRef(ImageSectionCharacteristics),
                 COFF::SectionCharacteristics(0x00F00000));

    if (opts::SectionRelocations) {
      ListScope D(W, "Relocations");
      for (const RelocationRef &Reloc : Sec.relocations())
        printRelocation(Sec, Reloc);
    }

    if (opts::SectionSymbols) {
      ListScope D(W, "Symbols");
      for (const SymbolRef &Symbol : Obj->symbols()) {
        if (!Sec.containsSymbol(Symbol))
          continue;

        printSymbol(Symbol);
      }
    }

    if (opts::SectionData &&
        !(Section->Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA)) {
      StringRef Data = unwrapOrError(Obj->getFileName(), Sec.getContents());
      W.printBinaryBlock("SectionData", Data);
    }
  }
}

void COFFDumper::printRelocations() {
  ListScope D(W, "Relocations");

  int SectionNumber = 0;
  for (const SectionRef &Section : Obj->sections()) {
    ++SectionNumber;
    StringRef Name = unwrapOrError(Obj->getFileName(), Section.getName());

    bool PrintedGroup = false;
    for (const RelocationRef &Reloc : Section.relocations()) {
      if (!PrintedGroup) {
        W.startLine() << "Section (" << SectionNumber << ") " << Name << " {\n";
        W.indent();
        PrintedGroup = true;
      }

      printRelocation(Section, Reloc);
    }

    if (PrintedGroup) {
      W.unindent();
      W.startLine() << "}\n";
    }
  }
}

void COFFDumper::printRelocation(const SectionRef &Section,
                                 const RelocationRef &Reloc, uint64_t Bias) {
  uint64_t Offset = Reloc.getOffset() - Bias;
  uint64_t RelocType = Reloc.getType();
  SmallString<32> RelocName;
  StringRef SymbolName;
  Reloc.getTypeName(RelocName);
  symbol_iterator Symbol = Reloc.getSymbol();
  int64_t SymbolIndex = -1;
  if (Symbol != Obj->symbol_end()) {
    Expected<StringRef> SymbolNameOrErr = Symbol->getName();
    if (!SymbolNameOrErr)
      reportError(SymbolNameOrErr.takeError(), Obj->getFileName());

    SymbolName = *SymbolNameOrErr;
    SymbolIndex = Obj->getSymbolIndex(Obj->getCOFFSymbol(*Symbol));
  }

  if (opts::ExpandRelocs) {
    DictScope Group(W, "Relocation");
    W.printHex("Offset", Offset);
    W.printNumber("Type", RelocName, RelocType);
    W.printString("Symbol", SymbolName.empty() ? "-" : SymbolName);
    W.printNumber("SymbolIndex", SymbolIndex);
  } else {
    raw_ostream& OS = W.startLine();
    OS << W.hex(Offset)
       << " " << RelocName
       << " " << (SymbolName.empty() ? "-" : SymbolName)
       << " (" << SymbolIndex << ")"
       << "\n";
  }
}

void COFFDumper::printSymbols(bool /*ExtraSymInfo*/) {
  ListScope Group(W, "Symbols");

  for (const SymbolRef &Symbol : Obj->symbols())
    printSymbol(Symbol);
}

void COFFDumper::printDynamicSymbols() { ListScope Group(W, "DynamicSymbols"); }

static Expected<StringRef>
getSectionName(const llvm::object::COFFObjectFile *Obj, int32_t SectionNumber,
               const coff_section *Section) {
  if (Section)
    return Obj->getSectionName(Section);
  if (SectionNumber == llvm::COFF::IMAGE_SYM_DEBUG)
    return StringRef("IMAGE_SYM_DEBUG");
  if (SectionNumber == llvm::COFF::IMAGE_SYM_ABSOLUTE)
    return StringRef("IMAGE_SYM_ABSOLUTE");
  if (SectionNumber == llvm::COFF::IMAGE_SYM_UNDEFINED)
    return StringRef("IMAGE_SYM_UNDEFINED");
  return StringRef("");
}

void COFFDumper::printSymbol(const SymbolRef &Sym) {
  DictScope D(W, "Symbol");

  COFFSymbolRef Symbol = Obj->getCOFFSymbol(Sym);
  Expected<const coff_section *> SecOrErr =
      Obj->getSection(Symbol.getSectionNumber());
  if (!SecOrErr) {
    W.startLine() << "Invalid section number: " << Symbol.getSectionNumber()
                  << "\n";
    W.flush();
    consumeError(SecOrErr.takeError());
    return;
  }
  const coff_section *Section = *SecOrErr;

  StringRef SymbolName;
  if (Expected<StringRef> SymNameOrErr = Obj->getSymbolName(Symbol))
    SymbolName = *SymNameOrErr;

  StringRef SectionName;
  if (Expected<StringRef> SecNameOrErr =
          getSectionName(Obj, Symbol.getSectionNumber(), Section))
    SectionName = *SecNameOrErr;

  W.printString("Name", SymbolName);
  W.printNumber("Value", Symbol.getValue());
  W.printNumber("Section", SectionName, Symbol.getSectionNumber());
  W.printEnum("BaseType", Symbol.getBaseType(), ArrayRef(ImageSymType));
  W.printEnum("ComplexType", Symbol.getComplexType(), ArrayRef(ImageSymDType));
  W.printEnum("StorageClass", Symbol.getStorageClass(),
              ArrayRef(ImageSymClass));
  W.printNumber("AuxSymbolCount", Symbol.getNumberOfAuxSymbols());

  for (uint8_t I = 0; I < Symbol.getNumberOfAuxSymbols(); ++I) {
    if (Symbol.isFunctionDefinition()) {
      const coff_aux_function_definition *Aux;
      if (std::error_code EC = getSymbolAuxData(Obj, Symbol, I, Aux))
        reportError(errorCodeToError(EC), Obj->getFileName());

      DictScope AS(W, "AuxFunctionDef");
      W.printNumber("TagIndex", Aux->TagIndex);
      W.printNumber("TotalSize", Aux->TotalSize);
      W.printHex("PointerToLineNumber", Aux->PointerToLinenumber);
      W.printHex("PointerToNextFunction", Aux->PointerToNextFunction);

    } else if (Symbol.isAnyUndefined()) {
      const coff_aux_weak_external *Aux;
      if (std::error_code EC = getSymbolAuxData(Obj, Symbol, I, Aux))
        reportError(errorCodeToError(EC), Obj->getFileName());

      DictScope AS(W, "AuxWeakExternal");
      W.printNumber("Linked", getSymbolName(Aux->TagIndex), Aux->TagIndex);
      W.printEnum("Search", Aux->Characteristics,
                  ArrayRef(WeakExternalCharacteristics));

    } else if (Symbol.isFileRecord()) {
      const char *FileName;
      if (std::error_code EC = getSymbolAuxData(Obj, Symbol, I, FileName))
        reportError(errorCodeToError(EC), Obj->getFileName());
      DictScope AS(W, "AuxFileRecord");

      StringRef Name(FileName, Symbol.getNumberOfAuxSymbols() *
                                   Obj->getSymbolTableEntrySize());
      W.printString("FileName", Name.rtrim(StringRef("\0", 1)));
      break;
    } else if (Symbol.isSectionDefinition()) {
      const coff_aux_section_definition *Aux;
      if (std::error_code EC = getSymbolAuxData(Obj, Symbol, I, Aux))
        reportError(errorCodeToError(EC), Obj->getFileName());

      int32_t AuxNumber = Aux->getNumber(Symbol.isBigObj());

      DictScope AS(W, "AuxSectionDef");
      W.printNumber("Length", Aux->Length);
      W.printNumber("RelocationCount", Aux->NumberOfRelocations);
      W.printNumber("LineNumberCount", Aux->NumberOfLinenumbers);
      W.printHex("Checksum", Aux->CheckSum);
      W.printNumber("Number", AuxNumber);
      W.printEnum("Selection", Aux->Selection, ArrayRef(ImageCOMDATSelect));

      if (Section && Section->Characteristics & COFF::IMAGE_SCN_LNK_COMDAT
          && Aux->Selection == COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
        Expected<const coff_section *> Assoc = Obj->getSection(AuxNumber);
        if (!Assoc)
          reportError(Assoc.takeError(), Obj->getFileName());
        Expected<StringRef> AssocName = getSectionName(Obj, AuxNumber, *Assoc);
        if (!AssocName)
          reportError(AssocName.takeError(), Obj->getFileName());

        W.printNumber("AssocSection", *AssocName, AuxNumber);
      }
    } else if (Symbol.isCLRToken()) {
      const coff_aux_clr_token *Aux;
      if (std::error_code EC = getSymbolAuxData(Obj, Symbol, I, Aux))
        reportError(errorCodeToError(EC), Obj->getFileName());

      DictScope AS(W, "AuxCLRToken");
      W.printNumber("AuxType", Aux->AuxType);
      W.printNumber("Reserved", Aux->Reserved);
      W.printNumber("SymbolTableIndex", getSymbolName(Aux->SymbolTableIndex),
                    Aux->SymbolTableIndex);

    } else {
      W.startLine() << "<unhandled auxiliary record>\n";
    }
  }
}

void COFFDumper::printUnwindInfo() {
  ListScope D(W, "UnwindInformation");
  switch (Obj->getMachine()) {
  case COFF::IMAGE_FILE_MACHINE_AMD64: {
    Win64EH::Dumper Dumper(W);
    Win64EH::Dumper::SymbolResolver
    Resolver = [](const object::coff_section *Section, uint64_t Offset,
                  SymbolRef &Symbol, void *user_data) -> std::error_code {
      COFFDumper *Dumper = reinterpret_cast<COFFDumper *>(user_data);
      return Dumper->resolveSymbol(Section, Offset, Symbol);
    };
    Win64EH::Dumper::Context Ctx(*Obj, Resolver, this);
    Dumper.printData(Ctx);
    break;
  }
  case COFF::IMAGE_FILE_MACHINE_ARM64:
  case COFF::IMAGE_FILE_MACHINE_ARM64EC:
  case COFF::IMAGE_FILE_MACHINE_ARM64X:
  case COFF::IMAGE_FILE_MACHINE_ARMNT: {
    ARM::WinEH::Decoder Decoder(W, Obj->getMachine() !=
                                       COFF::IMAGE_FILE_MACHINE_ARMNT);
    // TODO Propagate the error.
    consumeError(Decoder.dumpProcedureData(*Obj));
    break;
  }
  default:
    W.printEnum("unsupported Image Machine", Obj->getMachine(),
                ArrayRef(ImageFileMachineType));
    break;
  }
}

void COFFDumper::printNeededLibraries() {
  ListScope D(W, "NeededLibraries");

  using LibsTy = std::vector<StringRef>;
  LibsTy Libs;

  for (const ImportDirectoryEntryRef &DirRef : Obj->import_directories()) {
    StringRef Name;
    if (!DirRef.getName(Name))
      Libs.push_back(Name);
  }

  llvm::stable_sort(Libs);

  for (const auto &L : Libs) {
    W.startLine() << L << "\n";
  }
}

void COFFDumper::printImportedSymbols(
    iterator_range<imported_symbol_iterator> Range) {
  for (const ImportedSymbolRef &I : Range) {
    StringRef Sym;
    if (Error E = I.getSymbolName(Sym))
      reportError(std::move(E), Obj->getFileName());
    uint16_t Ordinal;
    if (Error E = I.getOrdinal(Ordinal))
      reportError(std::move(E), Obj->getFileName());
    W.printNumber("Symbol", Sym, Ordinal);
  }
}

void COFFDumper::printDelayImportedSymbols(
    const DelayImportDirectoryEntryRef &I,
    iterator_range<imported_symbol_iterator> Range) {
  int Index = 0;
  for (const ImportedSymbolRef &S : Range) {
    DictScope Import(W, "Import");
    StringRef Sym;
    if (Error E = S.getSymbolName(Sym))
      reportError(std::move(E), Obj->getFileName());

    uint16_t Ordinal;
    if (Error E = S.getOrdinal(Ordinal))
      reportError(std::move(E), Obj->getFileName());
    W.printNumber("Symbol", Sym, Ordinal);

    uint64_t Addr;
    if (Error E = I.getImportAddress(Index++, Addr))
      reportError(std::move(E), Obj->getFileName());
    W.printHex("Address", Addr);
  }
}

void COFFDumper::printCOFFImports() {
  // Regular imports
  for (const ImportDirectoryEntryRef &I : Obj->import_directories()) {
    DictScope Import(W, "Import");
    StringRef Name;
    if (Error E = I.getName(Name))
      reportError(std::move(E), Obj->getFileName());
    W.printString("Name", Name);
    uint32_t ILTAddr;
    if (Error E = I.getImportLookupTableRVA(ILTAddr))
      reportError(std::move(E), Obj->getFileName());
    W.printHex("ImportLookupTableRVA", ILTAddr);
    uint32_t IATAddr;
    if (Error E = I.getImportAddressTableRVA(IATAddr))
      reportError(std::move(E), Obj->getFileName());
    W.printHex("ImportAddressTableRVA", IATAddr);
    // The import lookup table can be missing with certain older linkers, so
    // fall back to the import address table in that case.
    if (ILTAddr)
      printImportedSymbols(I.lookup_table_symbols());
    else
      printImportedSymbols(I.imported_symbols());
  }

  // Delay imports
  for (const DelayImportDirectoryEntryRef &I : Obj->delay_import_directories()) {
    DictScope Import(W, "DelayImport");
    StringRef Name;
    if (Error E = I.getName(Name))
      reportError(std::move(E), Obj->getFileName());
    W.printString("Name", Name);
    const delay_import_directory_table_entry *Table;
    if (Error E = I.getDelayImportTable(Table))
      reportError(std::move(E), Obj->getFileName());
    W.printHex("Attributes", Table->Attributes);
    W.printHex("ModuleHandle", Table->ModuleHandle);
    W.printHex("ImportAddressTable", Table->DelayImportAddressTable);
    W.printHex("ImportNameTable", Table->DelayImportNameTable);
    W.printHex("BoundDelayImportTable", Table->BoundDelayImportTable);
    W.printHex("UnloadDelayImportTable", Table->UnloadDelayImportTable);
    printDelayImportedSymbols(I, I.imported_symbols());
  }
}

void COFFDumper::printCOFFExports() {
  for (const ExportDirectoryEntryRef &Exp : Obj->export_directories()) {
    DictScope Export(W, "Export");

    StringRef Name;
    uint32_t Ordinal;
    bool IsForwarder;

    if (Error E = Exp.getSymbolName(Name))
      reportError(std::move(E), Obj->getFileName());
    if (Error E = Exp.getOrdinal(Ordinal))
      reportError(std::move(E), Obj->getFileName());
    if (Error E = Exp.isForwarder(IsForwarder))
      reportError(std::move(E), Obj->getFileName());

    W.printNumber("Ordinal", Ordinal);
    W.printString("Name", Name);
    StringRef ForwardTo;
    if (IsForwarder) {
      if (Error E = Exp.getForwardTo(ForwardTo))
        reportError(std::move(E), Obj->getFileName());
      W.printString("ForwardedTo", ForwardTo);
    } else {
      uint32_t RVA;
      if (Error E = Exp.getExportRVA(RVA))
        reportError(std::move(E), Obj->getFileName());
      W.printHex("RVA", RVA);
    }
  }
}

void COFFDumper::printCOFFDirectives() {
  for (const SectionRef &Section : Obj->sections()) {
    StringRef Name = unwrapOrError(Obj->getFileName(), Section.getName());
    if (Name != ".drectve")
      continue;

    StringRef Contents =
        unwrapOrError(Obj->getFileName(), Section.getContents());
    W.printString("Directive(s)", Contents);
  }
}

static std::string getBaseRelocTypeName(uint8_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_BASED_ABSOLUTE: return "ABSOLUTE";
  case COFF::IMAGE_REL_BASED_HIGH: return "HIGH";
  case COFF::IMAGE_REL_BASED_LOW: return "LOW";
  case COFF::IMAGE_REL_BASED_HIGHLOW: return "HIGHLOW";
  case COFF::IMAGE_REL_BASED_HIGHADJ: return "HIGHADJ";
  case COFF::IMAGE_REL_BASED_ARM_MOV32T: return "ARM_MOV32(T)";
  case COFF::IMAGE_REL_BASED_DIR64: return "DIR64";
  default: return "unknown (" + llvm::utostr(Type) + ")";
  }
}

void COFFDumper::printCOFFBaseReloc() {
  ListScope D(W, "BaseReloc");
  for (const BaseRelocRef &I : Obj->base_relocs()) {
    uint8_t Type;
    uint32_t RVA;
    if (Error E = I.getRVA(RVA))
      reportError(std::move(E), Obj->getFileName());
    if (Error E = I.getType(Type))
      reportError(std::move(E), Obj->getFileName());
    DictScope Import(W, "Entry");
    W.printString("Type", getBaseRelocTypeName(Type));
    W.printHex("Address", RVA);
  }
}

void COFFDumper::printCOFFResources() {
  ListScope ResourcesD(W, "Resources");
  for (const SectionRef &S : Obj->sections()) {
    StringRef Name = unwrapOrError(Obj->getFileName(), S.getName());
    if (!Name.starts_with(".rsrc"))
      continue;

    StringRef Ref = unwrapOrError(Obj->getFileName(), S.getContents());

    if ((Name == ".rsrc") || (Name == ".rsrc$01")) {
      ResourceSectionRef RSF;
      Error E = RSF.load(Obj, S);
      if (E)
        reportError(std::move(E), Obj->getFileName());
      auto &BaseTable = unwrapOrError(Obj->getFileName(), RSF.getBaseTable());
      W.printNumber("Total Number of Resources",
                    countTotalTableEntries(RSF, BaseTable, "Type"));
      W.printHex("Base Table Address",
                 Obj->getCOFFSection(S)->PointerToRawData);
      W.startLine() << "\n";
      printResourceDirectoryTable(RSF, BaseTable, "Type");
    }
    if (opts::SectionData)
      W.printBinaryBlock(Name.str() + " Data", Ref);
  }
}

uint32_t
COFFDumper::countTotalTableEntries(ResourceSectionRef RSF,
                                   const coff_resource_dir_table &Table,
                                   StringRef Level) {
  uint32_t TotalEntries = 0;
  for (int i = 0; i < Table.NumberOfNameEntries + Table.NumberOfIDEntries;
       i++) {
    auto Entry = unwrapOrError(Obj->getFileName(), RSF.getTableEntry(Table, i));
    if (Entry.Offset.isSubDir()) {
      StringRef NextLevel;
      if (Level == "Name")
        NextLevel = "Language";
      else
        NextLevel = "Name";
      auto &NextTable =
          unwrapOrError(Obj->getFileName(), RSF.getEntrySubDir(Entry));
      TotalEntries += countTotalTableEntries(RSF, NextTable, NextLevel);
    } else {
      TotalEntries += 1;
    }
  }
  return TotalEntries;
}

void COFFDumper::printResourceDirectoryTable(
    ResourceSectionRef RSF, const coff_resource_dir_table &Table,
    StringRef Level) {

  W.printNumber("Number of String Entries", Table.NumberOfNameEntries);
  W.printNumber("Number of ID Entries", Table.NumberOfIDEntries);

  // Iterate through level in resource directory tree.
  for (int i = 0; i < Table.NumberOfNameEntries + Table.NumberOfIDEntries;
       i++) {
    auto Entry = unwrapOrError(Obj->getFileName(), RSF.getTableEntry(Table, i));
    StringRef Name;
    SmallString<20> IDStr;
    raw_svector_ostream OS(IDStr);
    if (i < Table.NumberOfNameEntries) {
      ArrayRef<UTF16> RawEntryNameString =
          unwrapOrError(Obj->getFileName(), RSF.getEntryNameString(Entry));
      std::vector<UTF16> EndianCorrectedNameString;
      if (llvm::sys::IsBigEndianHost) {
        EndianCorrectedNameString.resize(RawEntryNameString.size() + 1);
        std::copy(RawEntryNameString.begin(), RawEntryNameString.end(),
                  EndianCorrectedNameString.begin() + 1);
        EndianCorrectedNameString[0] = UNI_UTF16_BYTE_ORDER_MARK_SWAPPED;
        RawEntryNameString = ArrayRef(EndianCorrectedNameString);
      }
      std::string EntryNameString;
      if (!llvm::convertUTF16ToUTF8String(RawEntryNameString, EntryNameString))
        reportError(errorCodeToError(object_error::parse_failed),
                    Obj->getFileName());
      OS << ": ";
      OS << EntryNameString;
    } else {
      if (Level == "Type") {
        OS << ": ";
        printResourceTypeName(Entry.Identifier.ID, OS);
      } else {
        OS << ": (ID " << Entry.Identifier.ID << ")";
      }
    }
    Name = IDStr;
    ListScope ResourceType(W, Level.str() + Name.str());
    if (Entry.Offset.isSubDir()) {
      W.printHex("Table Offset", Entry.Offset.value());
      StringRef NextLevel;
      if (Level == "Name")
        NextLevel = "Language";
      else
        NextLevel = "Name";
      auto &NextTable =
          unwrapOrError(Obj->getFileName(), RSF.getEntrySubDir(Entry));
      printResourceDirectoryTable(RSF, NextTable, NextLevel);
    } else {
      W.printHex("Entry Offset", Entry.Offset.value());
      char FormattedTime[20] = {};
      time_t TDS = time_t(Table.TimeDateStamp);
      strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));
      W.printHex("Time/Date Stamp", FormattedTime, Table.TimeDateStamp);
      W.printNumber("Major Version", Table.MajorVersion);
      W.printNumber("Minor Version", Table.MinorVersion);
      W.printNumber("Characteristics", Table.Characteristics);
      ListScope DataScope(W, "Data");
      auto &DataEntry =
          unwrapOrError(Obj->getFileName(), RSF.getEntryData(Entry));
      W.printHex("DataRVA", DataEntry.DataRVA);
      W.printNumber("DataSize", DataEntry.DataSize);
      W.printNumber("Codepage", DataEntry.Codepage);
      W.printNumber("Reserved", DataEntry.Reserved);
      StringRef Contents =
          unwrapOrError(Obj->getFileName(), RSF.getContents(DataEntry));
      W.printBinaryBlock("Data", Contents);
    }
  }
}

void COFFDumper::printStackMap() const {
  SectionRef StackMapSection;
  for (auto Sec : Obj->sections()) {
    StringRef Name;
    if (Expected<StringRef> NameOrErr = Sec.getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    if (Name == ".llvm_stackmaps") {
      StackMapSection = Sec;
      break;
    }
  }

  if (StackMapSection == SectionRef())
    return;

  StringRef StackMapContents =
      unwrapOrError(Obj->getFileName(), StackMapSection.getContents());
  ArrayRef<uint8_t> StackMapContentsArray =
      arrayRefFromStringRef(StackMapContents);

  if (Obj->isLittleEndian())
    prettyPrintStackMap(
        W, StackMapParser<llvm::endianness::little>(StackMapContentsArray));
  else
    prettyPrintStackMap(
        W, StackMapParser<llvm::endianness::big>(StackMapContentsArray));
}

void COFFDumper::printAddrsig() {
  SectionRef AddrsigSection;
  for (auto Sec : Obj->sections()) {
    StringRef Name;
    if (Expected<StringRef> NameOrErr = Sec.getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    if (Name == ".llvm_addrsig") {
      AddrsigSection = Sec;
      break;
    }
  }

  if (AddrsigSection == SectionRef())
    return;

  StringRef AddrsigContents =
      unwrapOrError(Obj->getFileName(), AddrsigSection.getContents());
  ArrayRef<uint8_t> AddrsigContentsArray(AddrsigContents.bytes_begin(),
                                         AddrsigContents.size());

  ListScope L(W, "Addrsig");
  const uint8_t *Cur = AddrsigContents.bytes_begin();
  const uint8_t *End = AddrsigContents.bytes_end();
  while (Cur != End) {
    unsigned Size;
    const char *Err = nullptr;
    uint64_t SymIndex = decodeULEB128(Cur, &Size, End, &Err);
    if (Err)
      reportError(createError(Err), Obj->getFileName());

    W.printNumber("Sym", getSymbolName(SymIndex), SymIndex);
    Cur += Size;
  }
}

void COFFDumper::printCGProfile() {
  SectionRef CGProfileSection;
  for (SectionRef Sec : Obj->sections()) {
    StringRef Name = unwrapOrError(Obj->getFileName(), Sec.getName());
    if (Name == ".llvm.call-graph-profile") {
      CGProfileSection = Sec;
      break;
    }
  }

  if (CGProfileSection == SectionRef())
    return;

  StringRef CGProfileContents =
      unwrapOrError(Obj->getFileName(), CGProfileSection.getContents());
  BinaryStreamReader Reader(CGProfileContents, llvm::endianness::little);

  ListScope L(W, "CGProfile");
  while (!Reader.empty()) {
    uint32_t FromIndex, ToIndex;
    uint64_t Count;
    if (Error Err = Reader.readInteger(FromIndex))
      reportError(std::move(Err), Obj->getFileName());
    if (Error Err = Reader.readInteger(ToIndex))
      reportError(std::move(Err), Obj->getFileName());
    if (Error Err = Reader.readInteger(Count))
      reportError(std::move(Err), Obj->getFileName());

    DictScope D(W, "CGProfileEntry");
    W.printNumber("From", getSymbolName(FromIndex), FromIndex);
    W.printNumber("To", getSymbolName(ToIndex), ToIndex);
    W.printNumber("Weight", Count);
  }
}

StringRef COFFDumper::getSymbolName(uint32_t Index) {
  Expected<COFFSymbolRef> Sym = Obj->getSymbol(Index);
  if (!Sym)
    reportError(Sym.takeError(), Obj->getFileName());

  Expected<StringRef> SymName = Obj->getSymbolName(*Sym);
  if (!SymName)
    reportError(SymName.takeError(), Obj->getFileName());

  return *SymName;
}

void llvm::dumpCodeViewMergedTypes(ScopedPrinter &Writer,
                                   ArrayRef<ArrayRef<uint8_t>> IpiRecords,
                                   ArrayRef<ArrayRef<uint8_t>> TpiRecords) {
  TypeTableCollection TpiTypes(TpiRecords);
  {
    ListScope S(Writer, "MergedTypeStream");
    TypeDumpVisitor TDV(TpiTypes, &Writer, opts::CodeViewSubsectionBytes);
    if (Error Err = codeview::visitTypeStream(TpiTypes, TDV))
      reportError(std::move(Err), "<?>");
    Writer.flush();
  }

  // Flatten the id stream and print it next. The ID stream refers to names from
  // the type stream.
  TypeTableCollection IpiTypes(IpiRecords);
  {
    ListScope S(Writer, "MergedIDStream");
    TypeDumpVisitor TDV(TpiTypes, &Writer, opts::CodeViewSubsectionBytes);
    TDV.setIpiTypes(IpiTypes);
    if (Error Err = codeview::visitTypeStream(IpiTypes, TDV))
      reportError(std::move(Err), "<?>");
    Writer.flush();
  }
}

void COFFDumper::printCOFFTLSDirectory() {
  if (Obj->is64())
    printCOFFTLSDirectory(Obj->getTLSDirectory64());
  else
    printCOFFTLSDirectory(Obj->getTLSDirectory32());
}

template <typename IntTy>
void COFFDumper::printCOFFTLSDirectory(
    const coff_tls_directory<IntTy> *TlsTable) {
  DictScope D(W, "TLSDirectory");
  if (!TlsTable)
    return;

  W.printHex("StartAddressOfRawData", TlsTable->StartAddressOfRawData);
  W.printHex("EndAddressOfRawData", TlsTable->EndAddressOfRawData);
  W.printHex("AddressOfIndex", TlsTable->AddressOfIndex);
  W.printHex("AddressOfCallBacks", TlsTable->AddressOfCallBacks);
  W.printHex("SizeOfZeroFill", TlsTable->SizeOfZeroFill);
  W.printFlags("Characteristics", TlsTable->Characteristics,
               ArrayRef(ImageSectionCharacteristics),
               COFF::SectionCharacteristics(COFF::IMAGE_SCN_ALIGN_MASK));
}
