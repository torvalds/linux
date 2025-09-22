//===- DWARFDebugLoc.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/Support/Errc.h"
#include <cstdint>

namespace llvm {
class DWARFUnit;
class MCRegisterInfo;
class raw_ostream;
class DWARFObject;
struct DIDumpOptions;
struct DWARFLocationExpression;
namespace object {
struct SectionedAddress;
}

/// A single location within a location list. Entries are stored in the DWARF5
/// form even if they originally come from a DWARF<=4 location list.
struct DWARFLocationEntry {
  /// The entry kind (DW_LLE_***).
  uint8_t Kind;

  /// The first value of the location entry (if applicable).
  uint64_t Value0;

  /// The second value of the location entry (if applicable).
  uint64_t Value1;

  /// The index of the section this entry is relative to (if applicable).
  uint64_t SectionIndex;

  /// The location expression itself (if applicable).
  SmallVector<uint8_t, 4> Loc;
};

/// An abstract base class for various kinds of location tables (.debug_loc,
/// .debug_loclists, and their dwo variants).
class DWARFLocationTable {
public:
  DWARFLocationTable(DWARFDataExtractor Data) : Data(std::move(Data)) {}
  virtual ~DWARFLocationTable() = default;

  /// Call the user-provided callback for each entry (including the end-of-list
  /// entry) in the location list starting at \p Offset. The callback can return
  /// false to terminate the iteration early. Returns an error if it was unable
  /// to parse the entire location list correctly. Upon successful termination
  /// \p Offset will be updated point past the end of the list.
  virtual Error visitLocationList(
      uint64_t *Offset,
      function_ref<bool(const DWARFLocationEntry &)> Callback) const = 0;

  /// Dump the location list at the given \p Offset. The function returns true
  /// iff it has successfully reched the end of the list. This means that one
  /// can attempt to parse another list after the current one (\p Offset will be
  /// updated to point past the end of the current list).
  bool dumpLocationList(uint64_t *Offset, raw_ostream &OS,
                        std::optional<object::SectionedAddress> BaseAddr,
                        const DWARFObject &Obj, DWARFUnit *U,
                        DIDumpOptions DumpOpts, unsigned Indent) const;

  Error visitAbsoluteLocationList(
      uint64_t Offset, std::optional<object::SectionedAddress> BaseAddr,
      std::function<std::optional<object::SectionedAddress>(uint32_t)>
          LookupAddr,
      function_ref<bool(Expected<DWARFLocationExpression>)> Callback) const;

  const DWARFDataExtractor &getData() { return Data; }

protected:
  DWARFDataExtractor Data;

  virtual void dumpRawEntry(const DWARFLocationEntry &Entry, raw_ostream &OS,
                            unsigned Indent, DIDumpOptions DumpOpts,
                            const DWARFObject &Obj) const = 0;
};

class DWARFDebugLoc final : public DWARFLocationTable {
public:
  /// A list of locations that contain one variable.
  struct LocationList {
    /// The beginning offset where this location list is stored in the debug_loc
    /// section.
    uint64_t Offset;
    /// All the locations in which the variable is stored.
    SmallVector<DWARFLocationEntry, 2> Entries;
  };

private:
  using LocationLists = SmallVector<LocationList, 4>;

  /// A list of all the variables in the debug_loc section, each one describing
  /// the locations in which the variable is stored.
  LocationLists Locations;

public:
  DWARFDebugLoc(DWARFDataExtractor Data)
      : DWARFLocationTable(std::move(Data)) {}

  /// Print the location lists found within the debug_loc section.
  void dump(raw_ostream &OS, const DWARFObject &Obj, DIDumpOptions DumpOpts,
            std::optional<uint64_t> Offset) const;

  Error visitLocationList(
      uint64_t *Offset,
      function_ref<bool(const DWARFLocationEntry &)> Callback) const override;

protected:
  void dumpRawEntry(const DWARFLocationEntry &Entry, raw_ostream &OS,
                    unsigned Indent, DIDumpOptions DumpOpts,
                    const DWARFObject &Obj) const override;
};

class DWARFDebugLoclists final : public DWARFLocationTable {
public:
  DWARFDebugLoclists(DWARFDataExtractor Data, uint16_t Version)
      : DWARFLocationTable(std::move(Data)), Version(Version) {}

  Error visitLocationList(
      uint64_t *Offset,
      function_ref<bool(const DWARFLocationEntry &)> Callback) const override;

  /// Dump all location lists within the given range.
  void dumpRange(uint64_t StartOffset, uint64_t Size, raw_ostream &OS,
                 const DWARFObject &Obj, DIDumpOptions DumpOpts);

protected:
  void dumpRawEntry(const DWARFLocationEntry &Entry, raw_ostream &OS,
                    unsigned Indent, DIDumpOptions DumpOpts,
                    const DWARFObject &Obj) const override;

private:
  uint16_t Version;
};

class ResolverError : public ErrorInfo<ResolverError> {
public:
  static char ID;

  ResolverError(uint32_t Index, dwarf::LoclistEntries Kind) : Index(Index), Kind(Kind) {}

  void log(raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override {
    return llvm::errc::invalid_argument;
  }

private:
  uint32_t Index;
  dwarf::LoclistEntries Kind;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H
