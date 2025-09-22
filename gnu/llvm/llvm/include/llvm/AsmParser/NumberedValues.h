//===-- NumberedValues.h - --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_NUMBEREDVALUES_H
#define LLVM_ASMPARSER_NUMBEREDVALUES_H

#include "llvm/ADT/DenseMap.h"

namespace llvm {

/// Mapping from value ID to value, which also remembers what the next unused
/// ID is.
template <class T> class NumberedValues {
  DenseMap<unsigned, T> Vals;
  unsigned NextUnusedID = 0;

public:
  unsigned getNext() const { return NextUnusedID; }
  T get(unsigned ID) const { return Vals.lookup(ID); }
  void add(unsigned ID, T V) {
    assert(ID >= NextUnusedID && "Invalid value ID");
    Vals.insert({ID, V});
    NextUnusedID = ID + 1;
  }
};

} // end namespace llvm

#endif
