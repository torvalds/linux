//===----- llvm/Analysis/CaptureTracking.h - Pointer capture ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help determine which pointers are captured.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CAPTURETRACKING_H
#define LLVM_ANALYSIS_CAPTURETRACKING_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm {

  class Value;
  class Use;
  class DataLayout;
  class Instruction;
  class DominatorTree;
  class LoopInfo;
  class Function;

  /// getDefaultMaxUsesToExploreForCaptureTracking - Return default value of
  /// the maximal number of uses to explore before giving up. It is used by
  /// PointerMayBeCaptured family analysis.
  unsigned getDefaultMaxUsesToExploreForCaptureTracking();

  /// PointerMayBeCaptured - Return true if this pointer value may be captured
  /// by the enclosing function (which is required to exist).  This routine can
  /// be expensive, so consider caching the results.  The boolean ReturnCaptures
  /// specifies whether returning the value (or part of it) from the function
  /// counts as capturing it or not.  The boolean StoreCaptures specified
  /// whether storing the value (or part of it) into memory anywhere
  /// automatically counts as capturing it or not.
  /// MaxUsesToExplore specifies how many uses the analysis should explore for
  /// one value before giving up due too "too many uses". If MaxUsesToExplore
  /// is zero, a default value is assumed.
  bool PointerMayBeCaptured(const Value *V, bool ReturnCaptures,
                            bool StoreCaptures, unsigned MaxUsesToExplore = 0);

  /// PointerMayBeCapturedBefore - Return true if this pointer value may be
  /// captured by the enclosing function (which is required to exist). If a
  /// DominatorTree is provided, only captures which happen before the given
  /// instruction are considered. This routine can be expensive, so consider
  /// caching the results.  The boolean ReturnCaptures specifies whether
  /// returning the value (or part of it) from the function counts as capturing
  /// it or not.  The boolean StoreCaptures specified whether storing the value
  /// (or part of it) into memory anywhere automatically counts as capturing it
  /// or not. Captures by the provided instruction are considered if the
  /// final parameter is true.
  /// MaxUsesToExplore specifies how many uses the analysis should explore for
  /// one value before giving up due too "too many uses". If MaxUsesToExplore
  /// is zero, a default value is assumed.
  bool PointerMayBeCapturedBefore(const Value *V, bool ReturnCaptures,
                                  bool StoreCaptures, const Instruction *I,
                                  const DominatorTree *DT,
                                  bool IncludeI = false,
                                  unsigned MaxUsesToExplore = 0,
                                  const LoopInfo *LI = nullptr);

  // Returns the 'earliest' instruction that captures \p V in \F. An instruction
  // A is considered earlier than instruction B, if A dominates B. If 2 escapes
  // do not dominate each other, the terminator of the common dominator is
  // chosen. If not all uses can be analyzed, the earliest escape is set to
  // the first instruction in the function entry block. If \p V does not escape,
  // nullptr is returned. Note that the caller of the function has to ensure
  // that the instruction the result value is compared against is not in a
  // cycle.
  Instruction *FindEarliestCapture(const Value *V, Function &F,
                                   bool ReturnCaptures, bool StoreCaptures,
                                   const DominatorTree &DT,
                                   unsigned MaxUsesToExplore = 0);

  /// This callback is used in conjunction with PointerMayBeCaptured. In
  /// addition to the interface here, you'll need to provide your own getters
  /// to see whether anything was captured.
  struct CaptureTracker {
    virtual ~CaptureTracker();

    /// tooManyUses - The depth of traversal has breached a limit. There may be
    /// capturing instructions that will not be passed into captured().
    virtual void tooManyUses() = 0;

    /// shouldExplore - This is the use of a value derived from the pointer.
    /// To prune the search (ie., assume that none of its users could possibly
    /// capture) return false. To search it, return true.
    ///
    /// U->getUser() is always an Instruction.
    virtual bool shouldExplore(const Use *U);

    /// captured - Information about the pointer was captured by the user of
    /// use U. Return true to stop the traversal or false to continue looking
    /// for more capturing instructions.
    virtual bool captured(const Use *U) = 0;

    /// isDereferenceableOrNull - Overload to allow clients with additional
    /// knowledge about pointer dereferenceability to provide it and thereby
    /// avoid conservative responses when a pointer is compared to null.
    virtual bool isDereferenceableOrNull(Value *O, const DataLayout &DL);
  };

  /// Types of use capture kinds, see \p DetermineUseCaptureKind.
  enum class UseCaptureKind {
    NO_CAPTURE,
    MAY_CAPTURE,
    PASSTHROUGH,
  };

  /// Determine what kind of capture behaviour \p U may exhibit.
  ///
  /// A use can be no-capture, a use can potentially capture, or a use can be
  /// passthrough such that the uses of the user or \p U should be inspected.
  /// The \p IsDereferenceableOrNull callback is used to rule out capturing for
  /// certain comparisons.
  UseCaptureKind
  DetermineUseCaptureKind(const Use &U,
                          llvm::function_ref<bool(Value *, const DataLayout &)>
                              IsDereferenceableOrNull);

  /// PointerMayBeCaptured - Visit the value and the values derived from it and
  /// find values which appear to be capturing the pointer value. This feeds
  /// results into and is controlled by the CaptureTracker object.
  /// MaxUsesToExplore specifies how many uses the analysis should explore for
  /// one value before giving up due too "too many uses". If MaxUsesToExplore
  /// is zero, a default value is assumed.
  void PointerMayBeCaptured(const Value *V, CaptureTracker *Tracker,
                            unsigned MaxUsesToExplore = 0);

  /// Returns true if the pointer is to a function-local object that never
  /// escapes from the function.
  bool isNonEscapingLocalObject(
      const Value *V,
      SmallDenseMap<const Value *, bool, 8> *IsCapturedCache = nullptr);
} // end namespace llvm

#endif
