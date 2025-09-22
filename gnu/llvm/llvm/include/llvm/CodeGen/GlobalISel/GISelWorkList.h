//===- GISelWorkList.h - Worklist for GISel passes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GISELWORKLIST_H
#define LLVM_CODEGEN_GLOBALISEL_GISELWORKLIST_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

class MachineInstr;

// Worklist which mostly works similar to InstCombineWorkList, but on
// MachineInstrs. The main difference with something like a SetVector is that
// erasing an element doesn't move all elements over one place - instead just
// nulls out the element of the vector.
//
// FIXME: Does it make sense to factor out common code with the
// instcombinerWorkList?
template<unsigned N>
class GISelWorkList {
  SmallVector<MachineInstr *, N> Worklist;
  DenseMap<MachineInstr *, unsigned> WorklistMap;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  bool Finalized = true;
#endif

public:
  GISelWorkList() : WorklistMap(N) {}

  bool empty() const { return WorklistMap.empty(); }

  unsigned size() const { return WorklistMap.size(); }

  // Since we don't know ahead of time how many instructions we're going to add
  // to the worklist, and migrating densemap's elements is quite expensive
  // everytime we resize, only insert to the smallvector (typically during the
  // initial phase of populating lists). Before the worklist can be used,
  // finalize should be called. Also assert with NDEBUG if list is ever used
  // without finalizing. Note that unlike insert, we won't check for duplicates
  // - so the ideal place to use this is during the initial prepopulating phase
  // of most passes.
  void deferred_insert(MachineInstr *I) {
    Worklist.push_back(I);
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Finalized = false;
#endif
  }

  // This should only be called when using deferred_insert.
  // This asserts that the WorklistMap is empty, and then
  // inserts all the elements in the Worklist into the map.
  // It also asserts if there are any duplicate elements found.
  void finalize() {
    assert(WorklistMap.empty() && "Expecting empty worklistmap");
    if (Worklist.size() > N)
      WorklistMap.reserve(Worklist.size());
    for (unsigned i = 0; i < Worklist.size(); ++i)
      if (!WorklistMap.try_emplace(Worklist[i], i).second)
        llvm_unreachable("Duplicate elements in the list");
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    Finalized = true;
#endif
  }

  /// Add the specified instruction to the worklist if it isn't already in it.
  void insert(MachineInstr *I) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(Finalized && "GISelWorkList used without finalizing");
#endif
    if (WorklistMap.try_emplace(I, Worklist.size()).second)
      Worklist.push_back(I);
  }

  /// Remove I from the worklist if it exists.
  void remove(const MachineInstr *I) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert((Finalized || WorklistMap.empty()) && "Neither finalized nor empty");
#endif
    auto It = WorklistMap.find(I);
    if (It == WorklistMap.end())
      return; // Not in worklist.

    // Don't bother moving everything down, just null out the slot.
    Worklist[It->second] = nullptr;

    WorklistMap.erase(It);
  }

  void clear() {
    Worklist.clear();
    WorklistMap.clear();
  }

  MachineInstr *pop_back_val() {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(Finalized && "GISelWorkList used without finalizing");
#endif
    MachineInstr *I;
    do {
      I = Worklist.pop_back_val();
    } while(!I);
    assert(I && "Pop back on empty worklist");
    WorklistMap.erase(I);
    return I;
  }
};

} // end namespace llvm.

#endif
