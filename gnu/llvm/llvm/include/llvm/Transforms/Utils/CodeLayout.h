//===- CodeLayout.h - Code layout/placement algorithms  ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Declares methods and data structures for code layout algorithms.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CODELAYOUT_H
#define LLVM_TRANSFORMS_UTILS_CODELAYOUT_H

#include "llvm/ADT/ArrayRef.h"

#include <utility>
#include <vector>

namespace llvm::codelayout {

using EdgeT = std::pair<uint64_t, uint64_t>;

struct EdgeCount {
  uint64_t src;
  uint64_t dst;
  uint64_t count;
};

/// Find a layout of nodes (basic blocks) of a given CFG optimizing jump
/// locality and thus processor I-cache utilization. This is achieved via
/// increasing the number of fall-through jumps and co-locating frequently
/// executed nodes together.
/// The nodes are assumed to be indexed by integers from [0, |V|) so that the
/// current order is the identity permutation.
/// \p NodeSizes: The sizes of the nodes (in bytes).
/// \p NodeCounts: The execution counts of the nodes in the profile.
/// \p EdgeCounts: The execution counts of every edge (jump) in the profile. The
///    map also defines the edges in CFG and should include 0-count edges.
/// \returns The best block order found.
std::vector<uint64_t> computeExtTspLayout(ArrayRef<uint64_t> NodeSizes,
                                          ArrayRef<uint64_t> NodeCounts,
                                          ArrayRef<EdgeCount> EdgeCounts);

/// Estimate the "quality" of a given node order in CFG. The higher the score,
/// the better the order is. The score is designed to reflect the locality of
/// the given order, which is anti-correlated with the number of I-cache misses
/// in a typical execution of the function.
double calcExtTspScore(ArrayRef<uint64_t> Order, ArrayRef<uint64_t> NodeSizes,
                       ArrayRef<uint64_t> NodeCounts,
                       ArrayRef<EdgeCount> EdgeCounts);

/// Estimate the "quality" of the current node order in CFG.
double calcExtTspScore(ArrayRef<uint64_t> NodeSizes,
                       ArrayRef<uint64_t> NodeCounts,
                       ArrayRef<EdgeCount> EdgeCounts);

/// Algorithm-specific params for Cache-Directed Sort. The values are tuned for
/// the best performance of large-scale front-end bound binaries.
struct CDSortConfig {
  /// The size of the cache.
  unsigned CacheEntries = 16;
  /// The size of a line in the cache.
  unsigned CacheSize = 2048;
  /// The maximum size of a chain to create.
  unsigned MaxChainSize = 128;
  /// The power exponent for the distance-based locality.
  double DistancePower = 0.25;
  /// The scale factor for the frequency-based locality.
  double FrequencyScale = 0.25;
};

/// Apply a Cache-Directed Sort for functions represented by a call graph.
/// The placement is done by optimizing the call locality by co-locating
/// frequently executed functions.
/// \p FuncSizes: The sizes of the nodes (in bytes).
/// \p FuncCounts: The execution counts of the nodes in the profile.
/// \p CallCounts: The execution counts of every edge (jump) in the profile. The
///    map also defines the edges in CFG and should include 0-count edges.
/// \p CallOffsets: The offsets of the calls from their source nodes.
/// \returns The best function order found.
std::vector<uint64_t> computeCacheDirectedLayout(
    ArrayRef<uint64_t> FuncSizes, ArrayRef<uint64_t> FuncCounts,
    ArrayRef<EdgeCount> CallCounts, ArrayRef<uint64_t> CallOffsets);

/// Apply a Cache-Directed Sort with a custom config.
std::vector<uint64_t> computeCacheDirectedLayout(
    const CDSortConfig &Config, ArrayRef<uint64_t> FuncSizes,
    ArrayRef<uint64_t> FuncCounts, ArrayRef<EdgeCount> CallCounts,
    ArrayRef<uint64_t> CallOffsets);

} // namespace llvm::codelayout

#endif // LLVM_TRANSFORMS_UTILS_CODELAYOUT_H
