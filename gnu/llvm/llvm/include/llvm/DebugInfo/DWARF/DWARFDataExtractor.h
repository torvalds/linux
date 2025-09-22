//===- DWARFDataExtractor.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDATAEXTRACTOR_H
#define LLVM_DEBUGINFO_DWARF_DWARFDATAEXTRACTOR_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/Support/DataExtractor.h"

namespace llvm {
class DWARFObject;

/// A DataExtractor (typically for an in-memory copy of an object-file section)
/// plus a relocation map for that section, if there is one.
class DWARFDataExtractor : public DataExtractor {
  const DWARFObject *Obj = nullptr;
  const DWARFSection *Section = nullptr;

public:
  /// Constructor for the normal case of extracting data from a DWARF section.
  /// The DWARFSection's lifetime must be at least as long as the extractor's.
  DWARFDataExtractor(const DWARFObject &Obj, const DWARFSection &Section,
                     bool IsLittleEndian, uint8_t AddressSize)
      : DataExtractor(Section.Data, IsLittleEndian, AddressSize), Obj(&Obj),
        Section(&Section) {}

  /// Constructor for cases when there are no relocations.
  DWARFDataExtractor(StringRef Data, bool IsLittleEndian, uint8_t AddressSize)
    : DataExtractor(Data, IsLittleEndian, AddressSize) {}
  DWARFDataExtractor(ArrayRef<uint8_t> Data, bool IsLittleEndian,
                     uint8_t AddressSize)
      : DataExtractor(
            StringRef(reinterpret_cast<const char *>(Data.data()), Data.size()),
            IsLittleEndian, AddressSize) {}

  /// Truncating constructor
  DWARFDataExtractor(const DWARFDataExtractor &Other, size_t Length)
      : DataExtractor(Other.getData().substr(0, Length), Other.isLittleEndian(),
                      Other.getAddressSize()),
        Obj(Other.Obj), Section(Other.Section) {}

  /// Extracts the DWARF "initial length" field, which can either be a 32-bit
  /// value smaller than 0xfffffff0, or the value 0xffffffff followed by a
  /// 64-bit length. Returns the actual length, and the DWARF format which is
  /// encoded in the field. In case of errors, it returns {0, DWARF32} and
  /// leaves the offset unchanged.
  std::pair<uint64_t, dwarf::DwarfFormat>
  getInitialLength(uint64_t *Off, Error *Err = nullptr) const;

  std::pair<uint64_t, dwarf::DwarfFormat> getInitialLength(Cursor &C) const {
    return getInitialLength(&getOffset(C), &getError(C));
  }

  /// Extracts a value and applies a relocation to the result if
  /// one exists for the given offset.
  uint64_t getRelocatedValue(uint32_t Size, uint64_t *Off,
                             uint64_t *SectionIndex = nullptr,
                             Error *Err = nullptr) const;
  uint64_t getRelocatedValue(Cursor &C, uint32_t Size,
                             uint64_t *SectionIndex = nullptr) const {
    return getRelocatedValue(Size, &getOffset(C), SectionIndex, &getError(C));
  }

  /// Extracts an address-sized value and applies a relocation to the result if
  /// one exists for the given offset.
  uint64_t getRelocatedAddress(uint64_t *Off, uint64_t *SecIx = nullptr) const {
    return getRelocatedValue(getAddressSize(), Off, SecIx);
  }
  uint64_t getRelocatedAddress(Cursor &C, uint64_t *SecIx = nullptr) const {
    return getRelocatedValue(getAddressSize(), &getOffset(C), SecIx,
                             &getError(C));
  }

  /// Extracts a DWARF-encoded pointer in \p Offset using \p Encoding.
  /// There is a DWARF encoding that uses a PC-relative adjustment.
  /// For these values, \p AbsPosOffset is used to fix them, which should
  /// reflect the absolute address of this pointer.
  std::optional<uint64_t> getEncodedPointer(uint64_t *Offset, uint8_t Encoding,
                                            uint64_t AbsPosOffset = 0) const;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDATAEXTRACTOR_H
