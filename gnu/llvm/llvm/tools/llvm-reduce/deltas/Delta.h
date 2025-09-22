//===- Delta.h - Delta Debugging Algorithm Implementation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for the Delta Debugging Algorithm:
// it splits a given set of Targets (i.e. Functions, Instructions, BBs, etc.)
// into chunks and tries to reduce the number chunks that are interesting.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_DELTA_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_DELTA_H

#include "ReducerWorkItem.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <utility>

namespace llvm {

class TestRunner;

struct Chunk {
  int Begin;
  int End;

  /// Helper function to verify if a given Target-index is inside the Chunk
  bool contains(int Index) const { return Index >= Begin && Index <= End; }

  void print() const {
    errs() << '[' << Begin;
    if (End - Begin != 0)
      errs() << ',' << End;
    errs() << ']';
  }

  /// Operator when populating CurrentChunks in Generic Delta Pass
  friend bool operator!=(const Chunk &C1, const Chunk &C2) {
    return C1.Begin != C2.Begin || C1.End != C2.End;
  }

  friend bool operator==(const Chunk &C1, const Chunk &C2) {
    return C1.Begin == C2.Begin && C1.End == C2.End;
  }

  /// Operator used for sets
  friend bool operator<(const Chunk &C1, const Chunk &C2) {
    return std::tie(C1.Begin, C1.End) < std::tie(C2.Begin, C2.End);
  }
};

template<>
struct DenseMapInfo<Chunk> {
  static inline Chunk getEmptyKey() {
    return {DenseMapInfo<int>::getEmptyKey(),
            DenseMapInfo<int>::getEmptyKey()};
  }

  static inline Chunk getTombstoneKey() {
    return {DenseMapInfo<int>::getTombstoneKey(),
            DenseMapInfo<int>::getTombstoneKey()};
  }

  static unsigned getHashValue(const Chunk Val) {
    std::pair<int, int> PairVal = std::make_pair(Val.Begin, Val.End);
    return DenseMapInfo<std::pair<int, int>>::getHashValue(PairVal);
  }

  static bool isEqual(const Chunk LHS, const Chunk RHS) {
    return LHS == RHS;
  }
};


/// Provides opaque interface for querying into ChunksToKeep without having to
/// actually understand what is going on.
class Oracle {
  /// Out of all the features that we promised to be,
  /// how many have we already processed?
  int Index = 0;

  /// The actual workhorse, contains the knowledge whether or not
  /// some particular feature should be preserved this time.
  ArrayRef<Chunk> ChunksToKeep;

public:
  explicit Oracle(ArrayRef<Chunk> ChunksToKeep) : ChunksToKeep(ChunksToKeep) {}

  /// Should be called for each feature on which we are operating.
  /// Name is self-explanatory - if returns true, then it should be preserved.
  bool shouldKeep() {
    if (ChunksToKeep.empty()) {
      ++Index;
      return false; // All further features are to be discarded.
    }

    // Does the current (front) chunk contain such a feature?
    bool ShouldKeep = ChunksToKeep.front().contains(Index);

    // Is this the last feature in the chunk?
    if (ChunksToKeep.front().End == Index)
      ChunksToKeep = ChunksToKeep.drop_front(); // Onto next chunk.

    ++Index;

    return ShouldKeep;
  }

  int count() { return Index; }
};

using ReductionFunc = function_ref<void(Oracle &, ReducerWorkItem &)>;

/// This function implements the Delta Debugging algorithm, it receives a
/// number of Targets (e.g. Functions, Instructions, Basic Blocks, etc.) and
/// splits them in half; these chunks of targets are then tested while ignoring
/// one chunk, if a chunk is proven to be uninteresting (i.e. fails the test)
/// it is removed from consideration. The algorithm will attempt to split the
/// Chunks in half and start the process again until it can't split chunks
/// anymore.
///
/// This function is intended to be called by each specialized delta pass (e.g.
/// RemoveFunctions) and receives three key parameters:
/// * Test: The main TestRunner instance which is used to run the provided
/// interesting-ness test, as well as to store and access the reduced Program.
/// * ExtractChunksFromModule: A function used to tailor the main program so it
/// only contains Targets that are inside Chunks of the given iteration.
/// Note: This function is implemented by each specialized Delta pass
///
/// Other implementations of the Delta Debugging algorithm can also be found in
/// the CReduce, Delta, and Lithium projects.
void runDeltaPass(TestRunner &Test, ReductionFunc ExtractChunksFromModule,
                  StringRef Message);
} // namespace llvm

#endif
