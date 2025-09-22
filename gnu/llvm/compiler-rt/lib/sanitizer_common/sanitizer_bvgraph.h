//===-- sanitizer_bvgraph.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer runtime.
// BVGraph -- a directed graph.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_BVGRAPH_H
#define SANITIZER_BVGRAPH_H

#include "sanitizer_common.h"
#include "sanitizer_bitvector.h"

namespace __sanitizer {

// Directed graph of fixed size implemented as an array of bit vectors.
// Not thread-safe, all accesses should be protected by an external lock.
template<class BV>
class BVGraph {
 public:
  enum SizeEnum : uptr { kSize = BV::kSize };
  uptr size() const { return kSize; }
  // No CTOR.
  void clear() {
    for (uptr i = 0; i < size(); i++)
      v[i].clear();
  }

  bool empty() const {
    for (uptr i = 0; i < size(); i++)
      if (!v[i].empty())
        return false;
    return true;
  }

  // Returns true if a new edge was added.
  bool addEdge(uptr from, uptr to) {
    check(from, to);
    return v[from].setBit(to);
  }

  // Returns true if at least one new edge was added.
  uptr addEdges(const BV &from, uptr to, uptr added_edges[],
                uptr max_added_edges) {
    uptr res = 0;
    t1.copyFrom(from);
    while (!t1.empty()) {
      uptr node = t1.getAndClearFirstOne();
      if (v[node].setBit(to))
        if (res < max_added_edges)
          added_edges[res++] = node;
    }
    return res;
  }

  // *EXPERIMENTAL*
  // Returns true if an edge from=>to exist.
  // This function does not use any global state except for 'this' itself,
  // and thus can be called from different threads w/o locking.
  // This would be racy.
  // FIXME: investigate how much we can prove about this race being "benign".
  bool hasEdge(uptr from, uptr to) { return v[from].getBit(to); }

  // Returns true if the edge from=>to was removed.
  bool removeEdge(uptr from, uptr to) {
    return v[from].clearBit(to);
  }

  // Returns true if at least one edge *=>to was removed.
  bool removeEdgesTo(const BV &to) {
    bool res = 0;
    for (uptr from = 0; from < size(); from++) {
      if (v[from].setDifference(to))
        res = true;
    }
    return res;
  }

  // Returns true if at least one edge from=>* was removed.
  bool removeEdgesFrom(const BV &from) {
    bool res = false;
    t1.copyFrom(from);
    while (!t1.empty()) {
      uptr idx = t1.getAndClearFirstOne();
      if (!v[idx].empty()) {
        v[idx].clear();
        res = true;
      }
    }
    return res;
  }

  void removeEdgesFrom(uptr from) {
    return v[from].clear();
  }

  bool hasEdge(uptr from, uptr to) const {
    check(from, to);
    return v[from].getBit(to);
  }

  // Returns true if there is a path from the node 'from'
  // to any of the nodes in 'targets'.
  bool isReachable(uptr from, const BV &targets) {
    BV &to_visit = t1,
       &visited = t2;
    to_visit.copyFrom(v[from]);
    visited.clear();
    visited.setBit(from);
    while (!to_visit.empty()) {
      uptr idx = to_visit.getAndClearFirstOne();
      if (visited.setBit(idx))
        to_visit.setUnion(v[idx]);
    }
    return targets.intersectsWith(visited);
  }

  // Finds a path from 'from' to one of the nodes in 'target',
  // stores up to 'path_size' items of the path into 'path',
  // returns the path length, or 0 if there is no path of size 'path_size'.
  uptr findPath(uptr from, const BV &targets, uptr *path, uptr path_size) {
    if (path_size == 0)
      return 0;
    path[0] = from;
    if (targets.getBit(from))
      return 1;
    // The function is recursive, so we don't want to create BV on stack.
    // Instead of a getAndClearFirstOne loop we use the slower iterator.
    for (typename BV::Iterator it(v[from]); it.hasNext(); ) {
      uptr idx = it.next();
      if (uptr res = findPath(idx, targets, path + 1, path_size - 1))
        return res + 1;
    }
    return 0;
  }

  // Same as findPath, but finds a shortest path.
  uptr findShortestPath(uptr from, const BV &targets, uptr *path,
                        uptr path_size) {
    for (uptr p = 1; p <= path_size; p++)
      if (findPath(from, targets, path, p) == p)
        return p;
    return 0;
  }

 private:
  void check(uptr idx1, uptr idx2) const {
    CHECK_LT(idx1, size());
    CHECK_LT(idx2, size());
  }
  BV v[kSize];
  // Keep temporary vectors here since we can not create large objects on stack.
  BV t1, t2;
};

} // namespace __sanitizer

#endif // SANITIZER_BVGRAPH_H
