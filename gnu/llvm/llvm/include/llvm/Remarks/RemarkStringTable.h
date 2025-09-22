//===-- RemarkStringTable.h - Serializing string table ----------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class is used to deduplicate and serialize a string table used for
// generating remarks.
//
// For parsing a string table, use ParsedStringTable in RemarkParser.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKSTRINGTABLE_H
#define LLVM_REMARKS_REMARKSTRINGTABLE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include <vector>

namespace llvm {

class raw_ostream;
class StringRef;

namespace remarks {

struct ParsedStringTable;
struct Remark;

/// The string table used for serializing remarks.
/// This table can be for example serialized in a section to be consumed after
/// the compilation.
struct StringTable {
  /// The string table containing all the unique strings used in the output.
  /// It maps a string to an unique ID.
  StringMap<unsigned, BumpPtrAllocator> StrTab;
  /// Total size of the string table when serialized.
  size_t SerializedSize = 0;

  StringTable() = default;

  /// Disable copy.
  StringTable(const StringTable &) = delete;
  StringTable &operator=(const StringTable &) = delete;
  /// Should be movable.
  StringTable(StringTable &&) = default;
  StringTable &operator=(StringTable &&) = default;

  /// Construct a string table from a ParsedStringTable.
  StringTable(const ParsedStringTable &Other);

  /// Add a string to the table. It returns an unique ID of the string.
  std::pair<unsigned, StringRef> add(StringRef Str);
  /// Modify \p R to use strings from this string table. If the string table
  /// does not contain the strings, it adds them.
  void internalize(Remark &R);
  /// Serialize the string table to a stream. It is serialized as a little
  /// endian uint64 (the size of the table in bytes) followed by a sequence of
  /// NULL-terminated strings, where the N-th string is the string with the ID N
  /// in the StrTab map.
  void serialize(raw_ostream &OS) const;
  /// Serialize the string table to a vector. This allows users to do the actual
  /// writing to file/memory/other.
  /// The string with the ID == N should be the N-th element in the vector.
  std::vector<StringRef> serialize() const;
};

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKSTRINGTABLE_H
