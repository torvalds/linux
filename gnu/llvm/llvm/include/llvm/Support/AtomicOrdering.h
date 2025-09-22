//===-- llvm/Support/AtomicOrdering.h ---Atomic Ordering---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Atomic ordering constants.
///
/// These values are used by LLVM to represent atomic ordering for C++11's
/// memory model and more, as detailed in docs/Atomics.rst.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ATOMICORDERING_H
#define LLVM_SUPPORT_ATOMICORDERING_H

#include <cstddef>

namespace llvm {

/// Atomic ordering for C11 / C++11's memory models.
///
/// These values cannot change because they are shared with standard library
/// implementations as well as with other compilers.
enum class AtomicOrderingCABI {
  relaxed = 0,
  consume = 1,
  acquire = 2,
  release = 3,
  acq_rel = 4,
  seq_cst = 5,
};

bool operator<(AtomicOrderingCABI, AtomicOrderingCABI) = delete;
bool operator>(AtomicOrderingCABI, AtomicOrderingCABI) = delete;
bool operator<=(AtomicOrderingCABI, AtomicOrderingCABI) = delete;
bool operator>=(AtomicOrderingCABI, AtomicOrderingCABI) = delete;

// Validate an integral value which isn't known to fit within the enum's range
// is a valid AtomicOrderingCABI.
template <typename Int> inline bool isValidAtomicOrderingCABI(Int I) {
  return (Int)AtomicOrderingCABI::relaxed <= I &&
         I <= (Int)AtomicOrderingCABI::seq_cst;
}

/// Atomic ordering for LLVM's memory model.
///
/// C++ defines ordering as a lattice. LLVM supplements this with NotAtomic and
/// Unordered, which are both below the C++ orders.
///
/// not_atomic-->unordered-->relaxed-->release--------------->acq_rel-->seq_cst
///                                   \-->consume-->acquire--/
enum class AtomicOrdering : unsigned {
  NotAtomic = 0,
  Unordered = 1,
  Monotonic = 2, // Equivalent to C++'s relaxed.
  // Consume = 3,  // Not specified yet.
  Acquire = 4,
  Release = 5,
  AcquireRelease = 6,
  SequentiallyConsistent = 7,
  LAST = SequentiallyConsistent
};

bool operator<(AtomicOrdering, AtomicOrdering) = delete;
bool operator>(AtomicOrdering, AtomicOrdering) = delete;
bool operator<=(AtomicOrdering, AtomicOrdering) = delete;
bool operator>=(AtomicOrdering, AtomicOrdering) = delete;

// Validate an integral value which isn't known to fit within the enum's range
// is a valid AtomicOrdering.
template <typename Int> inline bool isValidAtomicOrdering(Int I) {
  return static_cast<Int>(AtomicOrdering::NotAtomic) <= I &&
         I <= static_cast<Int>(AtomicOrdering::SequentiallyConsistent) &&
         I != 3;
}

/// String used by LLVM IR to represent atomic ordering.
inline const char *toIRString(AtomicOrdering ao) {
  static const char *names[8] = {"not_atomic", "unordered", "monotonic",
                                 "consume",    "acquire",   "release",
                                 "acq_rel",    "seq_cst"};
  return names[static_cast<size_t>(ao)];
}

/// Returns true if ao is stronger than other as defined by the AtomicOrdering
/// lattice, which is based on C++'s definition.
inline bool isStrongerThan(AtomicOrdering AO, AtomicOrdering Other) {
  static const bool lookup[8][8] = {
      //               NA     UN     RX     CO     AC     RE     AR     SC
      /* NotAtomic */ {false, false, false, false, false, false, false, false},
      /* Unordered */ { true, false, false, false, false, false, false, false},
      /* relaxed   */ { true,  true, false, false, false, false, false, false},
      /* consume   */ { true,  true,  true, false, false, false, false, false},
      /* acquire   */ { true,  true,  true,  true, false, false, false, false},
      /* release   */ { true,  true,  true, false, false, false, false, false},
      /* acq_rel   */ { true,  true,  true,  true,  true,  true, false, false},
      /* seq_cst   */ { true,  true,  true,  true,  true,  true,  true, false},
  };
  return lookup[static_cast<size_t>(AO)][static_cast<size_t>(Other)];
}

inline bool isAtLeastOrStrongerThan(AtomicOrdering AO, AtomicOrdering Other) {
  static const bool lookup[8][8] = {
      //               NA     UN     RX     CO     AC     RE     AR     SC
      /* NotAtomic */ { true, false, false, false, false, false, false, false},
      /* Unordered */ { true,  true, false, false, false, false, false, false},
      /* relaxed   */ { true,  true,  true, false, false, false, false, false},
      /* consume   */ { true,  true,  true,  true, false, false, false, false},
      /* acquire   */ { true,  true,  true,  true,  true, false, false, false},
      /* release   */ { true,  true,  true, false, false,  true, false, false},
      /* acq_rel   */ { true,  true,  true,  true,  true,  true,  true, false},
      /* seq_cst   */ { true,  true,  true,  true,  true,  true,  true,  true},
  };
  return lookup[static_cast<size_t>(AO)][static_cast<size_t>(Other)];
}

inline bool isStrongerThanUnordered(AtomicOrdering AO) {
  return isStrongerThan(AO, AtomicOrdering::Unordered);
}

inline bool isStrongerThanMonotonic(AtomicOrdering AO) {
  return isStrongerThan(AO, AtomicOrdering::Monotonic);
}

inline bool isAcquireOrStronger(AtomicOrdering AO) {
  return isAtLeastOrStrongerThan(AO, AtomicOrdering::Acquire);
}

inline bool isReleaseOrStronger(AtomicOrdering AO) {
  return isAtLeastOrStrongerThan(AO, AtomicOrdering::Release);
}

/// Return a single atomic ordering that is at least as strong as both the \p AO
/// and \p Other orderings for an atomic operation.
inline AtomicOrdering getMergedAtomicOrdering(AtomicOrdering AO,
                                              AtomicOrdering Other) {
  if ((AO == AtomicOrdering::Acquire && Other == AtomicOrdering::Release) ||
      (AO == AtomicOrdering::Release && Other == AtomicOrdering::Acquire))
    return AtomicOrdering::AcquireRelease;
  return isStrongerThan(AO, Other) ? AO : Other;
}

inline AtomicOrderingCABI toCABI(AtomicOrdering AO) {
  static const AtomicOrderingCABI lookup[8] = {
      /* NotAtomic */ AtomicOrderingCABI::relaxed,
      /* Unordered */ AtomicOrderingCABI::relaxed,
      /* relaxed   */ AtomicOrderingCABI::relaxed,
      /* consume   */ AtomicOrderingCABI::consume,
      /* acquire   */ AtomicOrderingCABI::acquire,
      /* release   */ AtomicOrderingCABI::release,
      /* acq_rel   */ AtomicOrderingCABI::acq_rel,
      /* seq_cst   */ AtomicOrderingCABI::seq_cst,
  };
  return lookup[static_cast<size_t>(AO)];
}

} // end namespace llvm

#endif // LLVM_SUPPORT_ATOMICORDERING_H
