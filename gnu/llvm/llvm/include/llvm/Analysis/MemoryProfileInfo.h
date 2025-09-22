//===- llvm/Analysis/MemoryProfileInfo.h - memory profile info ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utilities to analyze memory profile information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYPROFILEINFO_H
#define LLVM_ANALYSIS_MEMORYPROFILEINFO_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include <map>

namespace llvm {
namespace memprof {

/// Return the allocation type for a given set of memory profile values.
AllocationType getAllocType(uint64_t TotalLifetimeAccessDensity,
                            uint64_t AllocCount, uint64_t TotalLifetime);

/// Build callstack metadata from the provided list of call stack ids. Returns
/// the resulting metadata node.
MDNode *buildCallstackMetadata(ArrayRef<uint64_t> CallStack, LLVMContext &Ctx);

/// Returns the stack node from an MIB metadata node.
MDNode *getMIBStackNode(const MDNode *MIB);

/// Returns the allocation type from an MIB metadata node.
AllocationType getMIBAllocType(const MDNode *MIB);

/// Returns the total size from an MIB metadata node, or 0 if it was not
/// recorded.
uint64_t getMIBTotalSize(const MDNode *MIB);

/// Returns the string to use in attributes with the given type.
std::string getAllocTypeAttributeString(AllocationType Type);

/// True if the AllocTypes bitmask contains just a single type.
bool hasSingleAllocType(uint8_t AllocTypes);

/// Class to build a trie of call stack contexts for a particular profiled
/// allocation call, along with their associated allocation types.
/// The allocation will be at the root of the trie, which is then used to
/// compute the minimum lists of context ids needed to associate a call context
/// with a single allocation type.
class CallStackTrie {
private:
  struct CallStackTrieNode {
    // Allocation types for call context sharing the context prefix at this
    // node.
    uint8_t AllocTypes;
    uint64_t TotalSize;
    // Map of caller stack id to the corresponding child Trie node.
    std::map<uint64_t, CallStackTrieNode *> Callers;
    CallStackTrieNode(AllocationType Type, uint64_t TotalSize)
        : AllocTypes(static_cast<uint8_t>(Type)), TotalSize(TotalSize) {}
  };

  // The node for the allocation at the root.
  CallStackTrieNode *Alloc = nullptr;
  // The allocation's leaf stack id.
  uint64_t AllocStackId = 0;

  void deleteTrieNode(CallStackTrieNode *Node) {
    if (!Node)
      return;
    for (auto C : Node->Callers)
      deleteTrieNode(C.second);
    delete Node;
  }

  // Recursive helper to trim contexts and create metadata nodes.
  bool buildMIBNodes(CallStackTrieNode *Node, LLVMContext &Ctx,
                     std::vector<uint64_t> &MIBCallStack,
                     std::vector<Metadata *> &MIBNodes,
                     bool CalleeHasAmbiguousCallerContext);

public:
  CallStackTrie() = default;
  ~CallStackTrie() { deleteTrieNode(Alloc); }

  bool empty() const { return Alloc == nullptr; }

  /// Add a call stack context with the given allocation type to the Trie.
  /// The context is represented by the list of stack ids (computed during
  /// matching via a debug location hash), expected to be in order from the
  /// allocation call down to the bottom of the call stack (i.e. callee to
  /// caller order).
  void addCallStack(AllocationType AllocType, ArrayRef<uint64_t> StackIds,
                    uint64_t TotalSize = 0);

  /// Add the call stack context along with its allocation type from the MIB
  /// metadata to the Trie.
  void addCallStack(MDNode *MIB);

  /// Build and attach the minimal necessary MIB metadata. If the alloc has a
  /// single allocation type, add a function attribute instead. The reason for
  /// adding an attribute in this case is that it matches how the behavior for
  /// allocation calls will be communicated to lib call simplification after
  /// cloning or another optimization to distinguish the allocation types,
  /// which is lower overhead and more direct than maintaining this metadata.
  /// Returns true if memprof metadata attached, false if not (attribute added).
  bool buildAndAttachMIBMetadata(CallBase *CI);
};

/// Helper class to iterate through stack ids in both metadata (memprof MIB and
/// callsite) and the corresponding ThinLTO summary data structures
/// (CallsiteInfo and MIBInfo). This simplifies implementation of client code
/// which doesn't need to worry about whether we are operating with IR (Regular
/// LTO), or summary (ThinLTO).
template <class NodeT, class IteratorT> class CallStack {
public:
  CallStack(const NodeT *N = nullptr) : N(N) {}

  // Implement minimum required methods for range-based for loop.
  // The default implementation assumes we are operating on ThinLTO data
  // structures, which have a vector of StackIdIndices. There are specialized
  // versions provided to iterate through metadata.
  struct CallStackIterator {
    const NodeT *N = nullptr;
    IteratorT Iter;
    CallStackIterator(const NodeT *N, bool End);
    uint64_t operator*();
    bool operator==(const CallStackIterator &rhs) { return Iter == rhs.Iter; }
    bool operator!=(const CallStackIterator &rhs) { return !(*this == rhs); }
    void operator++() { ++Iter; }
  };

  bool empty() const { return N == nullptr; }

  CallStackIterator begin() const;
  CallStackIterator end() const { return CallStackIterator(N, /*End*/ true); }
  CallStackIterator beginAfterSharedPrefix(CallStack &Other);
  uint64_t back() const;

private:
  const NodeT *N = nullptr;
};

template <class NodeT, class IteratorT>
CallStack<NodeT, IteratorT>::CallStackIterator::CallStackIterator(
    const NodeT *N, bool End)
    : N(N) {
  if (!N) {
    Iter = nullptr;
    return;
  }
  Iter = End ? N->StackIdIndices.end() : N->StackIdIndices.begin();
}

template <class NodeT, class IteratorT>
uint64_t CallStack<NodeT, IteratorT>::CallStackIterator::operator*() {
  assert(Iter != N->StackIdIndices.end());
  return *Iter;
}

template <class NodeT, class IteratorT>
uint64_t CallStack<NodeT, IteratorT>::back() const {
  assert(N);
  return N->StackIdIndices.back();
}

template <class NodeT, class IteratorT>
typename CallStack<NodeT, IteratorT>::CallStackIterator
CallStack<NodeT, IteratorT>::begin() const {
  return CallStackIterator(N, /*End*/ false);
}

template <class NodeT, class IteratorT>
typename CallStack<NodeT, IteratorT>::CallStackIterator
CallStack<NodeT, IteratorT>::beginAfterSharedPrefix(CallStack &Other) {
  CallStackIterator Cur = begin();
  for (CallStackIterator OtherCur = Other.begin();
       Cur != end() && OtherCur != Other.end(); ++Cur, ++OtherCur)
    assert(*Cur == *OtherCur);
  return Cur;
}

/// Specializations for iterating through IR metadata stack contexts.
template <>
CallStack<MDNode, MDNode::op_iterator>::CallStackIterator::CallStackIterator(
    const MDNode *N, bool End);
template <>
uint64_t CallStack<MDNode, MDNode::op_iterator>::CallStackIterator::operator*();
template <> uint64_t CallStack<MDNode, MDNode::op_iterator>::back() const;

} // end namespace memprof
} // end namespace llvm

#endif
