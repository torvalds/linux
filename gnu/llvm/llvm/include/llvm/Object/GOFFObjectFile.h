//===- GOFFObjectFile.h - GOFF object file implementation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the GOFFObjectFile class.
// Record classes and derivatives are also declared and implemented.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_GOFFOBJECTFILE_H
#define LLVM_OBJECT_GOFFOBJECTFILE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/ConvertEBCDIC.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

namespace object {

class GOFFObjectFile : public ObjectFile {
  friend class GOFFSymbolRef;

  IndexedMap<const uint8_t *> EsdPtrs; // Indexed by EsdId.
  SmallVector<const uint8_t *, 256> TextPtrs;

  mutable DenseMap<uint32_t, std::pair<size_t, std::unique_ptr<char[]>>>
      EsdNamesCache;

  typedef DataRefImpl SectionEntryImpl;
  // (EDID, 0)               code, r/o data section
  // (EDID,PRID)             r/w data section
  SmallVector<SectionEntryImpl, 256> SectionList;
  mutable DenseMap<uint32_t, SmallVector<uint8_t>> SectionDataCache;

public:
  Expected<StringRef> getSymbolName(SymbolRef Symbol) const;

  GOFFObjectFile(MemoryBufferRef Object, Error &Err);
  static inline bool classof(const Binary *V) { return V->isGOFF(); }
  section_iterator section_begin() const override;
  section_iterator section_end() const override;

  uint8_t getBytesInAddress() const override { return 8; }

  StringRef getFileFormatName() const override { return "GOFF-SystemZ"; }

  Triple::ArchType getArch() const override { return Triple::systemz; }

  Expected<SubtargetFeatures> getFeatures() const override { return SubtargetFeatures(); }

  bool isRelocatableObject() const override { return true; }

  void moveSymbolNext(DataRefImpl &Symb) const override;
  basic_symbol_iterator symbol_begin() const override;
  basic_symbol_iterator symbol_end() const override;

  bool is64Bit() const override {
    return true;
  }

  bool isSectionNoLoad(DataRefImpl Sec) const;
  bool isSectionReadOnlyData(DataRefImpl Sec) const;
  bool isSectionZeroInit(DataRefImpl Sec) const;

private:
  // SymbolRef.
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  uint64_t getSymbolSize(DataRefImpl Symb) const;

  const uint8_t *getSymbolEsdRecord(DataRefImpl Symb) const;
  bool isSymbolUnresolved(DataRefImpl Symb) const;
  bool isSymbolIndirect(DataRefImpl Symb) const;

  // SectionRef.
  void moveSectionNext(DataRefImpl &Sec) const override;
  virtual Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  virtual Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  uint64_t getSectionIndex(DataRefImpl Sec) const override { return Sec.d.a; }
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  bool isSectionCompressed(DataRefImpl Sec) const override { return false; }
  bool isSectionText(DataRefImpl Sec) const override;
  bool isSectionData(DataRefImpl Sec) const override;
  bool isSectionBSS(DataRefImpl Sec) const override { return false; }
  bool isSectionVirtual(DataRefImpl Sec) const override { return false; }
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override {
    return relocation_iterator(RelocationRef(Sec, this));
  }
  relocation_iterator section_rel_end(DataRefImpl Sec) const override {
    return relocation_iterator(RelocationRef(Sec, this));
  }

  const uint8_t *getSectionEdEsdRecord(DataRefImpl &Sec) const;
  const uint8_t *getSectionPrEsdRecord(DataRefImpl &Sec) const;
  const uint8_t *getSectionEdEsdRecord(uint32_t SectionIndex) const;
  const uint8_t *getSectionPrEsdRecord(uint32_t SectionIndex) const;
  uint32_t getSectionDefEsdId(DataRefImpl &Sec) const;

  // RelocationRef.
  void moveRelocationNext(DataRefImpl &Rel) const override {}
  uint64_t getRelocationOffset(DataRefImpl Rel) const override { return 0; }
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override {
    DataRefImpl Temp;
    return basic_symbol_iterator(SymbolRef(Temp, this));
  }
  uint64_t getRelocationType(DataRefImpl Rel) const override { return 0; }
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override {}
};

class GOFFSymbolRef : public SymbolRef {
public:
  GOFFSymbolRef(const SymbolRef &B) : SymbolRef(B) {
    assert(isa<GOFFObjectFile>(SymbolRef::getObject()));
  }

  const GOFFObjectFile *getObject() const {
    return cast<GOFFObjectFile>(BasicSymbolRef::getObject());
  }

  Expected<uint32_t> getSymbolGOFFFlags() const {
    return getObject()->getSymbolFlags(getRawDataRefImpl());
  }

  Expected<SymbolRef::Type> getSymbolGOFFType() const {
    return getObject()->getSymbolType(getRawDataRefImpl());
  }

  uint64_t getSize() const {
    return getObject()->getSymbolSize(getRawDataRefImpl());
  }
};

} // namespace object

} // namespace llvm

#endif
