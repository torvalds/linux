//===-- llvm/ADT/IntEqClasses.h - Equiv. Classes of Integers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Equivalence classes for small integers. This is a mapping of the integers
/// 0 .. N-1 into M equivalence classes numbered 0 .. M-1.
///
/// Initially each integer has its own equivalence class. Classes are joined by
/// passing a representative member of each class to join().
///
/// Once the classes are built, compress() will number them 0 .. M-1 and prevent
/// further changes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_INTEQCLASSES_H
#define LLVM_ADT_INTEQCLASSES_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class IntEqClasses {
  /// EC - When uncompressed, map each integer to a smaller member of its
  /// equivalence class. The class leader is the smallest member and maps to
  /// itself.
  ///
  /// When compressed, EC[i] is the equivalence class of i.
  SmallVector<unsigned, 8> EC;

  /// NumClasses - The number of equivalence classes when compressed, or 0 when
  /// uncompressed.
  unsigned NumClasses = 0;

public:
  /// IntEqClasses - Create an equivalence class mapping for 0 .. N-1.
  IntEqClasses(unsigned N = 0) { grow(N); }

  /// grow - Increase capacity to hold 0 .. N-1, putting new integers in unique
  /// equivalence classes.
  /// This requires an uncompressed map.
  void grow(unsigned N);

  /// clear - Clear all classes so that grow() will assign a unique class to
  /// every integer.
  void clear() {
    EC.clear();
    NumClasses = 0;
  }

  /// Join the equivalence classes of a and b. After joining classes,
  /// findLeader(a) == findLeader(b). This requires an uncompressed map.
  /// Returns the new leader.
  unsigned join(unsigned a, unsigned b);

  /// findLeader - Compute the leader of a's equivalence class. This is the
  /// smallest member of the class.
  /// This requires an uncompressed map.
  unsigned findLeader(unsigned a) const;

  /// compress - Compress equivalence classes by numbering them 0 .. M.
  /// This makes the equivalence class map immutable.
  void compress();

  /// getNumClasses - Return the number of equivalence classes after compress()
  /// was called.
  unsigned getNumClasses() const { return NumClasses; }

  /// operator[] - Return a's equivalence class number, 0 .. getNumClasses()-1.
  /// This requires a compressed map.
  unsigned operator[](unsigned a) const {
    assert(NumClasses && "operator[] called before compress()");
    return EC[a];
  }

  /// uncompress - Change back to the uncompressed representation that allows
  /// editing.
  void uncompress();
};

} // End llvm namespace

#endif
