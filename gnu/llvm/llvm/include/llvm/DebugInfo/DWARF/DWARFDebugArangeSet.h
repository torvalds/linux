//===- DWARFDebugArangeSet.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGESET_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGESET_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {

class raw_ostream;
class DWARFDataExtractor;

class DWARFDebugArangeSet {
public:
  struct Header {
    /// The total length of the entries for that set, not including the length
    /// field itself.
    uint64_t Length;
    /// The DWARF format of the set.
    dwarf::DwarfFormat Format;
    /// The offset from the beginning of the .debug_info section of the
    /// compilation unit entry referenced by the table.
    uint64_t CuOffset;
    /// The DWARF version number.
    uint16_t Version;
    /// The size in bytes of an address on the target architecture. For segmented
    /// addressing, this is the size of the offset portion of the address.
    uint8_t AddrSize;
    /// The size in bytes of a segment descriptor on the target architecture.
    /// If the target system uses a flat address space, this value is 0.
    uint8_t SegSize;
  };

  struct Descriptor {
    uint64_t Address;
    uint64_t Length;

    uint64_t getEndAddress() const { return Address + Length; }
    void dump(raw_ostream &OS, uint32_t AddressSize) const;
  };

private:
  using DescriptorColl = std::vector<Descriptor>;
  using desc_iterator_range = iterator_range<DescriptorColl::const_iterator>;

  uint64_t Offset;
  Header HeaderData;
  DescriptorColl ArangeDescriptors;

public:
  DWARFDebugArangeSet() { clear(); }

  void clear();
  Error extract(DWARFDataExtractor data, uint64_t *offset_ptr,
                function_ref<void(Error)> WarningHandler);
  void dump(raw_ostream &OS) const;

  uint64_t getCompileUnitDIEOffset() const { return HeaderData.CuOffset; }

  const Header &getHeader() const { return HeaderData; }

  desc_iterator_range descriptors() const {
    return desc_iterator_range(ArangeDescriptors.begin(),
                               ArangeDescriptors.end());
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGESET_H
