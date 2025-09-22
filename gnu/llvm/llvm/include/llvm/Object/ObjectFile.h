//===- ObjectFile.h - File format independent object file -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a file format independent ObjectFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_OBJECTFILE_H
#define LLVM_OBJECT_OBJECTFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/BinaryFormat/Swift.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace llvm {

class SubtargetFeatures;

namespace object {

class COFFObjectFile;
class MachOObjectFile;
class ObjectFile;
class SectionRef;
class SymbolRef;
class symbol_iterator;
class WasmObjectFile;

using section_iterator = content_iterator<SectionRef>;

typedef std::function<bool(const SectionRef &)> SectionFilterPredicate;
/// This is a value type class that represents a single relocation in the list
/// of relocations in the object file.
class RelocationRef {
  DataRefImpl RelocationPimpl;
  const ObjectFile *OwningObject = nullptr;

public:
  RelocationRef() = default;
  RelocationRef(DataRefImpl RelocationP, const ObjectFile *Owner);

  bool operator==(const RelocationRef &Other) const;

  void moveNext();

  uint64_t getOffset() const;
  symbol_iterator getSymbol() const;
  uint64_t getType() const;

  /// Get a string that represents the type of this relocation.
  ///
  /// This is for display purposes only.
  void getTypeName(SmallVectorImpl<char> &Result) const;

  DataRefImpl getRawDataRefImpl() const;
  const ObjectFile *getObject() const;
};

using relocation_iterator = content_iterator<RelocationRef>;

/// This is a value type class that represents a single section in the list of
/// sections in the object file.
class SectionRef {
  friend class SymbolRef;

  DataRefImpl SectionPimpl;
  const ObjectFile *OwningObject = nullptr;

public:
  SectionRef() = default;
  SectionRef(DataRefImpl SectionP, const ObjectFile *Owner);

  bool operator==(const SectionRef &Other) const;
  bool operator!=(const SectionRef &Other) const;
  bool operator<(const SectionRef &Other) const;

  void moveNext();

  Expected<StringRef> getName() const;
  uint64_t getAddress() const;
  uint64_t getIndex() const;
  uint64_t getSize() const;
  Expected<StringRef> getContents() const;

  /// Get the alignment of this section.
  Align getAlignment() const;

  bool isCompressed() const;
  /// Whether this section contains instructions.
  bool isText() const;
  /// Whether this section contains data, not instructions.
  bool isData() const;
  /// Whether this section contains BSS uninitialized data.
  bool isBSS() const;
  bool isVirtual() const;
  bool isBitcode() const;
  bool isStripped() const;

  /// Whether this section will be placed in the text segment, according to the
  /// Berkeley size format. This is true if the section is allocatable, and
  /// contains either code or readonly data.
  bool isBerkeleyText() const;
  /// Whether this section will be placed in the data segment, according to the
  /// Berkeley size format. This is true if the section is allocatable and
  /// contains data (e.g. PROGBITS), but is not text.
  bool isBerkeleyData() const;

  /// Whether this section is a debug section.
  bool isDebugSection() const;

  bool containsSymbol(SymbolRef S) const;

  relocation_iterator relocation_begin() const;
  relocation_iterator relocation_end() const;
  iterator_range<relocation_iterator> relocations() const {
    return make_range(relocation_begin(), relocation_end());
  }

  /// Returns the related section if this section contains relocations. The
  /// returned section may or may not have applied its relocations.
  Expected<section_iterator> getRelocatedSection() const;

  DataRefImpl getRawDataRefImpl() const;
  const ObjectFile *getObject() const;
};

struct SectionedAddress {
  const static uint64_t UndefSection = UINT64_MAX;

  uint64_t Address = 0;
  uint64_t SectionIndex = UndefSection;
};

inline bool operator<(const SectionedAddress &LHS,
                      const SectionedAddress &RHS) {
  return std::tie(LHS.SectionIndex, LHS.Address) <
         std::tie(RHS.SectionIndex, RHS.Address);
}

inline bool operator==(const SectionedAddress &LHS,
                       const SectionedAddress &RHS) {
  return std::tie(LHS.SectionIndex, LHS.Address) ==
         std::tie(RHS.SectionIndex, RHS.Address);
}

raw_ostream &operator<<(raw_ostream &OS, const SectionedAddress &Addr);

/// This is a value type class that represents a single symbol in the list of
/// symbols in the object file.
class SymbolRef : public BasicSymbolRef {
  friend class SectionRef;

public:
  enum Type {
    ST_Unknown, // Type not specified
    ST_Other,
    ST_Data,
    ST_Debug,
    ST_File,
    ST_Function,
  };

  SymbolRef() = default;
  SymbolRef(DataRefImpl SymbolP, const ObjectFile *Owner);
  SymbolRef(const BasicSymbolRef &B) : BasicSymbolRef(B) {
    assert(isa<ObjectFile>(BasicSymbolRef::getObject()));
  }

  Expected<StringRef> getName() const;
  /// Returns the symbol virtual address (i.e. address at which it will be
  /// mapped).
  Expected<uint64_t> getAddress() const;

  /// Return the value of the symbol depending on the object this can be an
  /// offset or a virtual address.
  Expected<uint64_t> getValue() const;

  /// Get the alignment of this symbol as the actual value (not log 2).
  uint32_t getAlignment() const;
  uint64_t getCommonSize() const;
  Expected<SymbolRef::Type> getType() const;

  /// Get section this symbol is defined in reference to. Result is
  /// end_sections() if it is undefined or is an absolute symbol.
  Expected<section_iterator> getSection() const;

  const ObjectFile *getObject() const;
};

class symbol_iterator : public basic_symbol_iterator {
public:
  symbol_iterator(SymbolRef Sym) : basic_symbol_iterator(Sym) {}
  symbol_iterator(const basic_symbol_iterator &B)
      : basic_symbol_iterator(SymbolRef(B->getRawDataRefImpl(),
                                        cast<ObjectFile>(B->getObject()))) {}

  const SymbolRef *operator->() const {
    const BasicSymbolRef &P = basic_symbol_iterator::operator *();
    return static_cast<const SymbolRef*>(&P);
  }

  const SymbolRef &operator*() const {
    const BasicSymbolRef &P = basic_symbol_iterator::operator *();
    return static_cast<const SymbolRef&>(P);
  }
};

/// This class is the base class for all object file types. Concrete instances
/// of this object are created by createObjectFile, which figures out which type
/// to create.
class ObjectFile : public SymbolicFile {
  virtual void anchor();

protected:
  ObjectFile(unsigned int Type, MemoryBufferRef Source);

  const uint8_t *base() const {
    return reinterpret_cast<const uint8_t *>(Data.getBufferStart());
  }

  // These functions are for SymbolRef to call internally. The main goal of
  // this is to allow SymbolRef::SymbolPimpl to point directly to the symbol
  // entry in the memory mapped object file. SymbolPimpl cannot contain any
  // virtual functions because then it could not point into the memory mapped
  // file.
  //
  // Implementations assume that the DataRefImpl is valid and has not been
  // modified externally. It's UB otherwise.
  friend class SymbolRef;

  virtual Expected<StringRef> getSymbolName(DataRefImpl Symb) const = 0;
  Error printSymbolName(raw_ostream &OS,
                                  DataRefImpl Symb) const override;
  virtual Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const = 0;
  virtual uint64_t getSymbolValueImpl(DataRefImpl Symb) const = 0;
  virtual uint32_t getSymbolAlignment(DataRefImpl Symb) const;
  virtual uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const = 0;
  virtual Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const = 0;
  virtual Expected<section_iterator>
  getSymbolSection(DataRefImpl Symb) const = 0;

  // Same as above for SectionRef.
  friend class SectionRef;

  virtual void moveSectionNext(DataRefImpl &Sec) const = 0;
  virtual Expected<StringRef> getSectionName(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionAddress(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionIndex(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionSize(DataRefImpl Sec) const = 0;
  virtual Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionAlignment(DataRefImpl Sec) const = 0;
  virtual bool isSectionCompressed(DataRefImpl Sec) const = 0;
  virtual bool isSectionText(DataRefImpl Sec) const = 0;
  virtual bool isSectionData(DataRefImpl Sec) const = 0;
  virtual bool isSectionBSS(DataRefImpl Sec) const = 0;
  // A section is 'virtual' if its contents aren't present in the object image.
  virtual bool isSectionVirtual(DataRefImpl Sec) const = 0;
  virtual bool isSectionBitcode(DataRefImpl Sec) const;
  virtual bool isSectionStripped(DataRefImpl Sec) const;
  virtual bool isBerkeleyText(DataRefImpl Sec) const;
  virtual bool isBerkeleyData(DataRefImpl Sec) const;
  virtual bool isDebugSection(DataRefImpl Sec) const;
  virtual relocation_iterator section_rel_begin(DataRefImpl Sec) const = 0;
  virtual relocation_iterator section_rel_end(DataRefImpl Sec) const = 0;
  virtual Expected<section_iterator> getRelocatedSection(DataRefImpl Sec) const;

  // Same as above for RelocationRef.
  friend class RelocationRef;
  virtual void moveRelocationNext(DataRefImpl &Rel) const = 0;
  virtual uint64_t getRelocationOffset(DataRefImpl Rel) const = 0;
  virtual symbol_iterator getRelocationSymbol(DataRefImpl Rel) const = 0;
  virtual uint64_t getRelocationType(DataRefImpl Rel) const = 0;
  virtual void getRelocationTypeName(DataRefImpl Rel,
                                     SmallVectorImpl<char> &Result) const = 0;

  virtual llvm::binaryformat::Swift5ReflectionSectionKind
  mapReflectionSectionNameToEnumValue(StringRef SectionName) const {
    return llvm::binaryformat::Swift5ReflectionSectionKind::unknown;
  };

  Expected<uint64_t> getSymbolValue(DataRefImpl Symb) const;

public:
  ObjectFile() = delete;
  ObjectFile(const ObjectFile &other) = delete;
  ObjectFile &operator=(const ObjectFile &other) = delete;

  uint64_t getCommonSymbolSize(DataRefImpl Symb) const {
    Expected<uint32_t> SymbolFlagsOrErr = getSymbolFlags(Symb);
    if (!SymbolFlagsOrErr)
      // TODO: Actually report errors helpfully.
      report_fatal_error(SymbolFlagsOrErr.takeError());
    assert(*SymbolFlagsOrErr & SymbolRef::SF_Common);
    return getCommonSymbolSizeImpl(Symb);
  }

  virtual std::vector<SectionRef> dynamic_relocation_sections() const {
    return std::vector<SectionRef>();
  }

  using symbol_iterator_range = iterator_range<symbol_iterator>;
  symbol_iterator_range symbols() const {
    return symbol_iterator_range(symbol_begin(), symbol_end());
  }

  virtual section_iterator section_begin() const = 0;
  virtual section_iterator section_end() const = 0;

  using section_iterator_range = iterator_range<section_iterator>;
  section_iterator_range sections() const {
    return section_iterator_range(section_begin(), section_end());
  }

  virtual bool hasDebugInfo() const;

  /// The number of bytes used to represent an address in this object
  ///        file format.
  virtual uint8_t getBytesInAddress() const = 0;

  virtual StringRef getFileFormatName() const = 0;
  virtual Triple::ArchType getArch() const = 0;
  virtual Triple::OSType getOS() const { return Triple::UnknownOS; }
  virtual Expected<SubtargetFeatures> getFeatures() const = 0;
  virtual std::optional<StringRef> tryGetCPUName() const {
    return std::nullopt;
  };
  virtual void setARMSubArch(Triple &TheTriple) const { }
  virtual Expected<uint64_t> getStartAddress() const {
    return errorCodeToError(object_error::parse_failed);
  };

  /// Create a triple from the data in this object file.
  Triple makeTriple() const;

  /// Maps a debug section name to a standard DWARF section name.
  virtual StringRef mapDebugSectionName(StringRef Name) const { return Name; }

  /// True if this is a relocatable object (.o/.obj).
  virtual bool isRelocatableObject() const = 0;

  /// True if the reflection section can be stripped by the linker.
  bool isReflectionSectionStrippable(
      llvm::binaryformat::Swift5ReflectionSectionKind ReflectionSectionKind)
      const;

  /// @returns Pointer to ObjectFile subclass to handle this type of object.
  /// @param ObjectPath The path to the object file. ObjectPath.isObject must
  ///        return true.
  /// Create ObjectFile from path.
  static Expected<OwningBinary<ObjectFile>>
  createObjectFile(StringRef ObjectPath);

  static Expected<std::unique_ptr<ObjectFile>>
  createObjectFile(MemoryBufferRef Object, llvm::file_magic Type,
                   bool InitContent = true);
  static Expected<std::unique_ptr<ObjectFile>>
  createObjectFile(MemoryBufferRef Object) {
    return createObjectFile(Object, llvm::file_magic::unknown);
  }

  static bool classof(const Binary *v) {
    return v->isObject();
  }

  static Expected<std::unique_ptr<COFFObjectFile>>
  createCOFFObjectFile(MemoryBufferRef Object);

  static Expected<std::unique_ptr<ObjectFile>>
  createXCOFFObjectFile(MemoryBufferRef Object, unsigned FileType);

  static Expected<std::unique_ptr<ObjectFile>>
  createELFObjectFile(MemoryBufferRef Object, bool InitContent = true);

  static Expected<std::unique_ptr<MachOObjectFile>>
  createMachOObjectFile(MemoryBufferRef Object, uint32_t UniversalCputype = 0,
                        uint32_t UniversalIndex = 0,
                        size_t MachOFilesetEntryOffset = 0);

  static Expected<std::unique_ptr<ObjectFile>>
  createGOFFObjectFile(MemoryBufferRef Object);

  static Expected<std::unique_ptr<WasmObjectFile>>
  createWasmObjectFile(MemoryBufferRef Object);
};

/// A filtered iterator for SectionRefs that skips sections based on some given
/// predicate.
class SectionFilterIterator {
public:
  SectionFilterIterator(SectionFilterPredicate Pred,
                        const section_iterator &Begin,
                        const section_iterator &End)
      : Predicate(std::move(Pred)), Iterator(Begin), End(End) {
    scanPredicate();
  }
  const SectionRef &operator*() const { return *Iterator; }
  SectionFilterIterator &operator++() {
    ++Iterator;
    scanPredicate();
    return *this;
  }
  bool operator!=(const SectionFilterIterator &Other) const {
    return Iterator != Other.Iterator;
  }

private:
  void scanPredicate() {
    while (Iterator != End && !Predicate(*Iterator)) {
      ++Iterator;
    }
  }
  SectionFilterPredicate Predicate;
  section_iterator Iterator;
  section_iterator End;
};

/// Creates an iterator range of SectionFilterIterators for a given Object and
/// predicate.
class SectionFilter {
public:
  SectionFilter(SectionFilterPredicate Pred, const ObjectFile &Obj)
      : Predicate(std::move(Pred)), Object(Obj) {}
  SectionFilterIterator begin() {
    return SectionFilterIterator(Predicate, Object.section_begin(),
                                 Object.section_end());
  }
  SectionFilterIterator end() {
    return SectionFilterIterator(Predicate, Object.section_end(),
                                 Object.section_end());
  }

private:
  SectionFilterPredicate Predicate;
  const ObjectFile &Object;
};

// Inline function definitions.
inline SymbolRef::SymbolRef(DataRefImpl SymbolP, const ObjectFile *Owner)
    : BasicSymbolRef(SymbolP, Owner) {}

inline Expected<StringRef> SymbolRef::getName() const {
  return getObject()->getSymbolName(getRawDataRefImpl());
}

inline Expected<uint64_t> SymbolRef::getAddress() const {
  return getObject()->getSymbolAddress(getRawDataRefImpl());
}

inline Expected<uint64_t> SymbolRef::getValue() const {
  return getObject()->getSymbolValue(getRawDataRefImpl());
}

inline uint32_t SymbolRef::getAlignment() const {
  return getObject()->getSymbolAlignment(getRawDataRefImpl());
}

inline uint64_t SymbolRef::getCommonSize() const {
  return getObject()->getCommonSymbolSize(getRawDataRefImpl());
}

inline Expected<section_iterator> SymbolRef::getSection() const {
  return getObject()->getSymbolSection(getRawDataRefImpl());
}

inline Expected<SymbolRef::Type> SymbolRef::getType() const {
  return getObject()->getSymbolType(getRawDataRefImpl());
}

inline const ObjectFile *SymbolRef::getObject() const {
  const SymbolicFile *O = BasicSymbolRef::getObject();
  return cast<ObjectFile>(O);
}

/// SectionRef
inline SectionRef::SectionRef(DataRefImpl SectionP,
                              const ObjectFile *Owner)
  : SectionPimpl(SectionP)
  , OwningObject(Owner) {}

inline bool SectionRef::operator==(const SectionRef &Other) const {
  return OwningObject == Other.OwningObject &&
         SectionPimpl == Other.SectionPimpl;
}

inline bool SectionRef::operator!=(const SectionRef &Other) const {
  return !(*this == Other);
}

inline bool SectionRef::operator<(const SectionRef &Other) const {
  assert(OwningObject == Other.OwningObject);
  return SectionPimpl < Other.SectionPimpl;
}

inline void SectionRef::moveNext() {
  return OwningObject->moveSectionNext(SectionPimpl);
}

inline Expected<StringRef> SectionRef::getName() const {
  return OwningObject->getSectionName(SectionPimpl);
}

inline uint64_t SectionRef::getAddress() const {
  return OwningObject->getSectionAddress(SectionPimpl);
}

inline uint64_t SectionRef::getIndex() const {
  return OwningObject->getSectionIndex(SectionPimpl);
}

inline uint64_t SectionRef::getSize() const {
  return OwningObject->getSectionSize(SectionPimpl);
}

inline Expected<StringRef> SectionRef::getContents() const {
  Expected<ArrayRef<uint8_t>> Res =
      OwningObject->getSectionContents(SectionPimpl);
  if (!Res)
    return Res.takeError();
  return StringRef(reinterpret_cast<const char *>(Res->data()), Res->size());
}

inline Align SectionRef::getAlignment() const {
  return MaybeAlign(OwningObject->getSectionAlignment(SectionPimpl))
      .valueOrOne();
}

inline bool SectionRef::isCompressed() const {
  return OwningObject->isSectionCompressed(SectionPimpl);
}

inline bool SectionRef::isText() const {
  return OwningObject->isSectionText(SectionPimpl);
}

inline bool SectionRef::isData() const {
  return OwningObject->isSectionData(SectionPimpl);
}

inline bool SectionRef::isBSS() const {
  return OwningObject->isSectionBSS(SectionPimpl);
}

inline bool SectionRef::isVirtual() const {
  return OwningObject->isSectionVirtual(SectionPimpl);
}

inline bool SectionRef::isBitcode() const {
  return OwningObject->isSectionBitcode(SectionPimpl);
}

inline bool SectionRef::isStripped() const {
  return OwningObject->isSectionStripped(SectionPimpl);
}

inline bool SectionRef::isBerkeleyText() const {
  return OwningObject->isBerkeleyText(SectionPimpl);
}

inline bool SectionRef::isBerkeleyData() const {
  return OwningObject->isBerkeleyData(SectionPimpl);
}

inline bool SectionRef::isDebugSection() const {
  return OwningObject->isDebugSection(SectionPimpl);
}

inline relocation_iterator SectionRef::relocation_begin() const {
  return OwningObject->section_rel_begin(SectionPimpl);
}

inline relocation_iterator SectionRef::relocation_end() const {
  return OwningObject->section_rel_end(SectionPimpl);
}

inline Expected<section_iterator> SectionRef::getRelocatedSection() const {
  return OwningObject->getRelocatedSection(SectionPimpl);
}

inline DataRefImpl SectionRef::getRawDataRefImpl() const {
  return SectionPimpl;
}

inline const ObjectFile *SectionRef::getObject() const {
  return OwningObject;
}

/// RelocationRef
inline RelocationRef::RelocationRef(DataRefImpl RelocationP,
                              const ObjectFile *Owner)
  : RelocationPimpl(RelocationP)
  , OwningObject(Owner) {}

inline bool RelocationRef::operator==(const RelocationRef &Other) const {
  return RelocationPimpl == Other.RelocationPimpl;
}

inline void RelocationRef::moveNext() {
  return OwningObject->moveRelocationNext(RelocationPimpl);
}

inline uint64_t RelocationRef::getOffset() const {
  return OwningObject->getRelocationOffset(RelocationPimpl);
}

inline symbol_iterator RelocationRef::getSymbol() const {
  return OwningObject->getRelocationSymbol(RelocationPimpl);
}

inline uint64_t RelocationRef::getType() const {
  return OwningObject->getRelocationType(RelocationPimpl);
}

inline void RelocationRef::getTypeName(SmallVectorImpl<char> &Result) const {
  return OwningObject->getRelocationTypeName(RelocationPimpl, Result);
}

inline DataRefImpl RelocationRef::getRawDataRefImpl() const {
  return RelocationPimpl;
}

inline const ObjectFile *RelocationRef::getObject() const {
  return OwningObject;
}

} // end namespace object

template <> struct DenseMapInfo<object::SectionRef> {
  static bool isEqual(const object::SectionRef &A,
                      const object::SectionRef &B) {
    return A == B;
  }
  static object::SectionRef getEmptyKey() {
    return object::SectionRef({}, nullptr);
  }
  static object::SectionRef getTombstoneKey() {
    object::DataRefImpl TS;
    TS.p = (uintptr_t)-1;
    return object::SectionRef(TS, nullptr);
  }
  static unsigned getHashValue(const object::SectionRef &Sec) {
    object::DataRefImpl Raw = Sec.getRawDataRefImpl();
    return hash_combine(Raw.p, Raw.d.a, Raw.d.b);
  }
};

} // end namespace llvm

#endif // LLVM_OBJECT_OBJECTFILE_H
