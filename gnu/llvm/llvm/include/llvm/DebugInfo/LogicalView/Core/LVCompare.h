//===-- LVCompare.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVCompare class, which is used to describe a logical
// view comparison.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVCOMPARE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVCOMPARE_H

#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"

namespace llvm {
namespace logicalview {

class LVReader;

// Record the elements missing or added and their compare pass.
using LVPassEntry = std::tuple<LVReader *, LVElement *, LVComparePass>;
using LVPassTable = std::vector<LVPassEntry>;

class LVCompare final {
  raw_ostream &OS;
  LVScopes ScopeStack;

  // As the comparison is performed twice (by exchanging the reference
  // and target readers) the element missing/added status does specify
  // the comparison pass.
  // By recording each missing/added elements along with its pass, it
  // allows checking which elements were missing/added during each pass.
  LVPassTable PassTable;

  // Reader used on the LHS of the comparison.
  // In the 'Missing' pass, it points to the reference reader.
  // In the 'Added' pass it points to the target reader.
  LVReader *Reader = nullptr;

  bool FirstMissing = true;
  bool PrintLines = false;
  bool PrintScopes = false;
  bool PrintSymbols = false;
  bool PrintTypes = false;

  static void setInstance(LVCompare *Compare);

  void printCurrentStack();
  void printSummary() const;

public:
  LVCompare() = delete;
  LVCompare(raw_ostream &OS);
  LVCompare(const LVCompare &) = delete;
  LVCompare &operator=(const LVCompare &) = delete;
  ~LVCompare() = default;

  static LVCompare &getInstance();

  // Scopes stack used during the missing/added reporting.
  void push(LVScope *Scope) { ScopeStack.push_back(Scope); }
  void pop() { ScopeStack.pop_back(); }

  // Perform comparison between the 'Reference' and 'Target' scopes tree.
  Error execute(LVReader *ReferenceReader, LVReader *TargetReader);

  void addPassEntry(LVReader *Reader, LVElement *Element, LVComparePass Pass) {
    PassTable.emplace_back(Reader, Element, Pass);
  }
  const LVPassTable &getPassTable() const & { return PassTable; }

  void printItem(LVElement *Element, LVComparePass Pass);
  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

inline LVCompare &getComparator() { return LVCompare::getInstance(); }

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVCOMPARE_H
