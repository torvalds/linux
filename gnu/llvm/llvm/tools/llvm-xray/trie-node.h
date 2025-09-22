//===- trie-node.h - XRay Call Stack Data Structure -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a data structure and routines for working with call stacks
// of instrumented functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_XRAY_STACK_TRIE_H
#define LLVM_TOOLS_LLVM_XRAY_STACK_TRIE_H

#include <forward_list>
#include <numeric>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

/// A type to represent a trie of invocations. It is useful to construct a
/// graph of these nodes from reading an XRay trace, such that each function
/// call can be placed in a larger context.
///
/// The template parameter allows users of the template to attach their own
/// data elements to each node in the invocation graph.
template <typename AssociatedData> struct TrieNode {
  /// The function ID.
  int32_t FuncId;

  /// The caller of this function.
  TrieNode<AssociatedData> *Parent;

  /// The callees from this function.
  llvm::SmallVector<TrieNode<AssociatedData> *, 4> Callees;

  /// Additional parameterized data on each node.
  AssociatedData ExtraData;
};

/// Merges together two TrieNodes with like function ids, aggregating their
/// callee lists and durations. The caller must provide storage where new merged
/// nodes can be allocated in the form of a linked list.
template <typename T, typename Callable>
TrieNode<T> *
mergeTrieNodes(const TrieNode<T> &Left, const TrieNode<T> &Right,
               /*Non-deduced pointer type for nullptr compatibility*/
               std::remove_reference_t<TrieNode<T> *> NewParent,
               std::forward_list<TrieNode<T>> &NodeStore,
               Callable &&MergeCallable) {
  llvm::function_ref<T(const T &, const T &)> MergeFn(
      std::forward<Callable>(MergeCallable));
  assert(Left.FuncId == Right.FuncId);
  NodeStore.push_front(TrieNode<T>{
      Left.FuncId, NewParent, {}, MergeFn(Left.ExtraData, Right.ExtraData)});
  auto I = NodeStore.begin();
  auto *Node = &*I;

  // Build a map of callees from the left side.
  llvm::DenseMap<int32_t, TrieNode<T> *> LeftCalleesByFuncId;
  for (auto *Callee : Left.Callees) {
    LeftCalleesByFuncId[Callee->FuncId] = Callee;
  }

  // Iterate through the right side, either merging with the map values or
  // directly adding to the Callees vector. The iteration also removes any
  // merged values from the left side map.
  // TODO: Unroll into iterative and explicit stack for efficiency.
  for (auto *Callee : Right.Callees) {
    auto iter = LeftCalleesByFuncId.find(Callee->FuncId);
    if (iter != LeftCalleesByFuncId.end()) {
      Node->Callees.push_back(
          mergeTrieNodes(*(iter->second), *Callee, Node, NodeStore, MergeFn));
      LeftCalleesByFuncId.erase(iter);
    } else {
      Node->Callees.push_back(Callee);
    }
  }

  // Add any callees that weren't found in the right side.
  for (auto MapPairIter : LeftCalleesByFuncId) {
    Node->Callees.push_back(MapPairIter.second);
  }

  return Node;
}

#endif // LLVM_TOOLS_LLVM_XRAY_STACK_TRIE_H
