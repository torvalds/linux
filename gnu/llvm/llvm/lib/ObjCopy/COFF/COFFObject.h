//===- COFFObject.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_COFF_COFFOBJECT_H
#define LLVM_LIB_OBJCOPY_COFF_COFFOBJECT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {
namespace objcopy {
namespace coff {

struct Relocation {
  Relocation() = default;
  Relocation(const object::coff_relocation &R) : Reloc(R) {}

  object::coff_relocation Reloc;
  size_t Target = 0;
  StringRef TargetName; // Used for diagnostics only
};

struct Section {
  object::coff_section Header;
  std::vector<Relocation> Relocs;
  StringRef Name;
  ssize_t UniqueId;
  size_t Index;

  ArrayRef<uint8_t> getContents() const {
    if (!OwnedContents.empty())
      return OwnedContents;
    return ContentsRef;
  }

  void setContentsRef(ArrayRef<uint8_t> Data) {
    OwnedContents.clear();
    ContentsRef = Data;
  }

  void setOwnedContents(std::vector<uint8_t> &&Data) {
    ContentsRef = ArrayRef<uint8_t>();
    OwnedContents = std::move(Data);
    Header.SizeOfRawData = OwnedContents.size();
  }

  void clearContents() {
    ContentsRef = ArrayRef<uint8_t>();
    OwnedContents.clear();
  }

private:
  ArrayRef<uint8_t> ContentsRef;
  std::vector<uint8_t> OwnedContents;
};

struct AuxSymbol {
  AuxSymbol(ArrayRef<uint8_t> In) {
    assert(In.size() == sizeof(Opaque));
    std::copy(In.begin(), In.end(), Opaque);
  }

  ArrayRef<uint8_t> getRef() const {
    return ArrayRef<uint8_t>(Opaque, sizeof(Opaque));
  }

  uint8_t Opaque[sizeof(object::coff_symbol16)];
};

struct Symbol {
  object::coff_symbol32 Sym;
  StringRef Name;
  std::vector<AuxSymbol> AuxData;
  StringRef AuxFile;
  ssize_t TargetSectionId;
  ssize_t AssociativeComdatTargetSectionId = 0;
  std::optional<size_t> WeakTargetSymbolId;
  size_t UniqueId;
  size_t RawIndex;
  bool Referenced;
};

struct Object {
  bool IsPE = false;

  object::dos_header DosHeader;
  ArrayRef<uint8_t> DosStub;

  object::coff_file_header CoffFileHeader;

  bool Is64 = false;
  object::pe32plus_header PeHeader;
  uint32_t BaseOfData = 0; // pe32plus_header lacks this field.

  std::vector<object::data_directory> DataDirectories;

  ArrayRef<Symbol> getSymbols() const { return Symbols; }
  // This allows mutating individual Symbols, but not mutating the list
  // of symbols itself.
  iterator_range<std::vector<Symbol>::iterator> getMutableSymbols() {
    return make_range(Symbols.begin(), Symbols.end());
  }

  const Symbol *findSymbol(size_t UniqueId) const;

  void addSymbols(ArrayRef<Symbol> NewSymbols);
  Error removeSymbols(function_ref<Expected<bool>(const Symbol &)> ToRemove);

  // Set the Referenced field on all Symbols, based on relocations in
  // all sections.
  Error markSymbols();

  ArrayRef<Section> getSections() const { return Sections; }
  // This allows mutating individual Sections, but not mutating the list
  // of sections itself.
  iterator_range<std::vector<Section>::iterator> getMutableSections() {
    return make_range(Sections.begin(), Sections.end());
  }

  const Section *findSection(ssize_t UniqueId) const;

  void addSections(ArrayRef<Section> NewSections);
  void removeSections(function_ref<bool(const Section &)> ToRemove);
  void truncateSections(function_ref<bool(const Section &)> ToTruncate);

private:
  std::vector<Symbol> Symbols;
  DenseMap<size_t, Symbol *> SymbolMap;

  size_t NextSymbolUniqueId = 0;

  std::vector<Section> Sections;
  DenseMap<ssize_t, Section *> SectionMap;

  ssize_t NextSectionUniqueId = 1; // Allow a UniqueId 0 to mean undefined.

  // Update SymbolMap.
  void updateSymbols();

  // Update SectionMap and Index in each Section.
  void updateSections();
};

// Copy between coff_symbol16 and coff_symbol32.
// The source and destination files can use either coff_symbol16 or
// coff_symbol32, while we always store them as coff_symbol32 in the
// intermediate data structure.
template <class Symbol1Ty, class Symbol2Ty>
void copySymbol(Symbol1Ty &Dest, const Symbol2Ty &Src) {
  static_assert(sizeof(Dest.Name.ShortName) == sizeof(Src.Name.ShortName),
                "Mismatched name sizes");
  memcpy(Dest.Name.ShortName, Src.Name.ShortName, sizeof(Dest.Name.ShortName));
  Dest.Value = Src.Value;
  Dest.SectionNumber = Src.SectionNumber;
  Dest.Type = Src.Type;
  Dest.StorageClass = Src.StorageClass;
  Dest.NumberOfAuxSymbols = Src.NumberOfAuxSymbols;
}

// Copy between pe32_header and pe32plus_header.
// We store the intermediate state in a pe32plus_header.
template <class PeHeader1Ty, class PeHeader2Ty>
void copyPeHeader(PeHeader1Ty &Dest, const PeHeader2Ty &Src) {
  Dest.Magic = Src.Magic;
  Dest.MajorLinkerVersion = Src.MajorLinkerVersion;
  Dest.MinorLinkerVersion = Src.MinorLinkerVersion;
  Dest.SizeOfCode = Src.SizeOfCode;
  Dest.SizeOfInitializedData = Src.SizeOfInitializedData;
  Dest.SizeOfUninitializedData = Src.SizeOfUninitializedData;
  Dest.AddressOfEntryPoint = Src.AddressOfEntryPoint;
  Dest.BaseOfCode = Src.BaseOfCode;
  Dest.ImageBase = Src.ImageBase;
  Dest.SectionAlignment = Src.SectionAlignment;
  Dest.FileAlignment = Src.FileAlignment;
  Dest.MajorOperatingSystemVersion = Src.MajorOperatingSystemVersion;
  Dest.MinorOperatingSystemVersion = Src.MinorOperatingSystemVersion;
  Dest.MajorImageVersion = Src.MajorImageVersion;
  Dest.MinorImageVersion = Src.MinorImageVersion;
  Dest.MajorSubsystemVersion = Src.MajorSubsystemVersion;
  Dest.MinorSubsystemVersion = Src.MinorSubsystemVersion;
  Dest.Win32VersionValue = Src.Win32VersionValue;
  Dest.SizeOfImage = Src.SizeOfImage;
  Dest.SizeOfHeaders = Src.SizeOfHeaders;
  Dest.CheckSum = Src.CheckSum;
  Dest.Subsystem = Src.Subsystem;
  Dest.DLLCharacteristics = Src.DLLCharacteristics;
  Dest.SizeOfStackReserve = Src.SizeOfStackReserve;
  Dest.SizeOfStackCommit = Src.SizeOfStackCommit;
  Dest.SizeOfHeapReserve = Src.SizeOfHeapReserve;
  Dest.SizeOfHeapCommit = Src.SizeOfHeapCommit;
  Dest.LoaderFlags = Src.LoaderFlags;
  Dest.NumberOfRvaAndSize = Src.NumberOfRvaAndSize;
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_COFF_COFFOBJECT_H
