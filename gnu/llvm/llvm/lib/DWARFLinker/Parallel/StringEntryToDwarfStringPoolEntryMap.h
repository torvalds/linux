//===- StringEntryToDwarfStringPoolEntryMap.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_STRINGENTRYTODWARFSTRINGPOOLENTRYMAP_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_STRINGENTRYTODWARFSTRINGPOOLENTRYMAP_H

#include "DWARFLinkerGlobalData.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DWARFLinker/StringPool.h"

namespace llvm {
namespace dwarf_linker {
namespace parallel {

/// This class creates a DwarfStringPoolEntry for the corresponding StringEntry.
class StringEntryToDwarfStringPoolEntryMap {
public:
  StringEntryToDwarfStringPoolEntryMap(LinkingGlobalData &GlobalData)
      : GlobalData(GlobalData) {}
  ~StringEntryToDwarfStringPoolEntryMap() {}

  /// Create DwarfStringPoolEntry for specified StringEntry if necessary.
  /// Initialize DwarfStringPoolEntry with initial values.
  DwarfStringPoolEntryWithExtString *add(const StringEntry *String) {
    DwarfStringPoolEntriesTy::iterator it = DwarfStringPoolEntries.find(String);

    if (it == DwarfStringPoolEntries.end()) {
      DwarfStringPoolEntryWithExtString *DataPtr =
          GlobalData.getAllocator()
              .Allocate<DwarfStringPoolEntryWithExtString>();
      DataPtr->String = String->getKey();
      DataPtr->Index = DwarfStringPoolEntry::NotIndexed;
      DataPtr->Offset = 0;
      DataPtr->Symbol = nullptr;
      it = DwarfStringPoolEntries.insert(std::make_pair(String, DataPtr)).first;
    }

    assert(it->second != nullptr);
    return it->second;
  }

  /// Returns already existed DwarfStringPoolEntry for the specified
  /// StringEntry.
  DwarfStringPoolEntryWithExtString *
  getExistingEntry(const StringEntry *String) const {
    DwarfStringPoolEntriesTy::const_iterator it =
        DwarfStringPoolEntries.find(String);

    assert(it != DwarfStringPoolEntries.end());
    assert(it->second != nullptr);
    return it->second;
  }

  /// Erase contents of StringsForEmission.
  void clear() { DwarfStringPoolEntries.clear(); }

protected:
  using DwarfStringPoolEntriesTy =
      DenseMap<const StringEntry *, DwarfStringPoolEntryWithExtString *>;
  DwarfStringPoolEntriesTy DwarfStringPoolEntries;

  LinkingGlobalData &GlobalData;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_STRINGENTRYTODWARFSTRINGPOOLENTRYMAP_H
