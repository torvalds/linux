//===-- xray_function_call_trie.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This file defines the interface for a function call trie.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_FUNCTION_CALL_TRIE_H
#define XRAY_FUNCTION_CALL_TRIE_H

#include "xray_buffer_queue.h"
#include "xray_defs.h"
#include "xray_profiling_flags.h"
#include "xray_segmented_array.h"
#include <limits>
#include <memory> // For placement new.
#include <utility>

namespace __xray {

/// A FunctionCallTrie represents the stack traces of XRay instrumented
/// functions that we've encountered, where a node corresponds to a function and
/// the path from the root to the node its stack trace. Each node in the trie
/// will contain some useful values, including:
///
///   * The cumulative amount of time spent in this particular node/stack.
///   * The number of times this stack has appeared.
///   * A histogram of latencies for that particular node.
///
/// Each node in the trie will also contain a list of callees, represented using
/// a Array<NodeIdPair> -- each NodeIdPair instance will contain the function
/// ID of the callee, and a pointer to the node.
///
/// If we visualise this data structure, we'll find the following potential
/// representation:
///
///   [function id node] -> [callees] [cumulative time]
///                         [call counter] [latency histogram]
///
/// As an example, when we have a function in this pseudocode:
///
///   func f(N) {
///     g()
///     h()
///     for i := 1..N { j() }
///   }
///
/// We may end up with a trie of the following form:
///
///   f -> [ g, h, j ] [...] [1] [...]
///   g -> [ ... ] [...] [1] [...]
///   h -> [ ... ] [...] [1] [...]
///   j -> [ ... ] [...] [N] [...]
///
/// If for instance the function g() called j() like so:
///
///   func g() {
///     for i := 1..10 { j() }
///   }
///
/// We'll find the following updated trie:
///
///   f -> [ g, h, j ] [...] [1] [...]
///   g -> [ j' ] [...] [1] [...]
///   h -> [ ... ] [...] [1] [...]
///   j -> [ ... ] [...] [N] [...]
///   j' -> [ ... ] [...] [10] [...]
///
/// Note that we'll have a new node representing the path `f -> g -> j'` with
/// isolated data. This isolation gives us a means of representing the stack
/// traces as a path, as opposed to a key in a table. The alternative
/// implementation here would be to use a separate table for the path, and use
/// hashes of the path as an identifier to accumulate the information. We've
/// moved away from this approach as it takes a lot of time to compute the hash
/// every time we need to update a function's call information as we're handling
/// the entry and exit events.
///
/// This approach allows us to maintain a shadow stack, which represents the
/// currently executing path, and on function exits quickly compute the amount
/// of time elapsed from the entry, then update the counters for the node
/// already represented in the trie. This necessitates an efficient
/// representation of the various data structures (the list of callees must be
/// cache-aware and efficient to look up, and the histogram must be compact and
/// quick to update) to enable us to keep the overheads of this implementation
/// to the minimum.
class FunctionCallTrie {
public:
  struct Node;

  // We use a NodeIdPair type instead of a std::pair<...> to not rely on the
  // standard library types in this header.
  struct NodeIdPair {
    Node *NodePtr;
    int32_t FId;
  };

  using NodeIdPairArray = Array<NodeIdPair>;
  using NodeIdPairAllocatorType = NodeIdPairArray::AllocatorType;

  // A Node in the FunctionCallTrie gives us a list of callees, the cumulative
  // number of times this node actually appeared, the cumulative amount of time
  // for this particular node including its children call times, and just the
  // local time spent on this node. Each Node will have the ID of the XRay
  // instrumented function that it is associated to.
  struct Node {
    Node *Parent;
    NodeIdPairArray Callees;
    uint64_t CallCount;
    uint64_t CumulativeLocalTime; // Typically in TSC deltas, not wall-time.
    int32_t FId;

    // TODO: Include the compact histogram.
  };

private:
  struct ShadowStackEntry {
    uint64_t EntryTSC;
    Node *NodePtr;
    uint16_t EntryCPU;
  };

  using NodeArray = Array<Node>;
  using RootArray = Array<Node *>;
  using ShadowStackArray = Array<ShadowStackEntry>;

public:
  // We collate the allocators we need into a single struct, as a convenience to
  // allow us to initialize these as a group.
  struct Allocators {
    using NodeAllocatorType = NodeArray::AllocatorType;
    using RootAllocatorType = RootArray::AllocatorType;
    using ShadowStackAllocatorType = ShadowStackArray::AllocatorType;

    // Use hosted aligned storage members to allow for trivial move and init.
    // This also allows us to sidestep the potential-failing allocation issue.
    alignas(NodeAllocatorType) std::byte
        NodeAllocatorStorage[sizeof(NodeAllocatorType)];
    alignas(RootAllocatorType) std::byte
        RootAllocatorStorage[sizeof(RootAllocatorType)];
    alignas(ShadowStackAllocatorType) std::byte
        ShadowStackAllocatorStorage[sizeof(ShadowStackAllocatorType)];
    alignas(NodeIdPairAllocatorType) std::byte
        NodeIdPairAllocatorStorage[sizeof(NodeIdPairAllocatorType)];

    NodeAllocatorType *NodeAllocator = nullptr;
    RootAllocatorType *RootAllocator = nullptr;
    ShadowStackAllocatorType *ShadowStackAllocator = nullptr;
    NodeIdPairAllocatorType *NodeIdPairAllocator = nullptr;

    Allocators() = default;
    Allocators(const Allocators &) = delete;
    Allocators &operator=(const Allocators &) = delete;

    struct Buffers {
      BufferQueue::Buffer NodeBuffer;
      BufferQueue::Buffer RootsBuffer;
      BufferQueue::Buffer ShadowStackBuffer;
      BufferQueue::Buffer NodeIdPairBuffer;
    };

    explicit Allocators(Buffers &B) XRAY_NEVER_INSTRUMENT {
      new (&NodeAllocatorStorage)
          NodeAllocatorType(B.NodeBuffer.Data, B.NodeBuffer.Size);
      NodeAllocator =
          reinterpret_cast<NodeAllocatorType *>(&NodeAllocatorStorage);

      new (&RootAllocatorStorage)
          RootAllocatorType(B.RootsBuffer.Data, B.RootsBuffer.Size);
      RootAllocator =
          reinterpret_cast<RootAllocatorType *>(&RootAllocatorStorage);

      new (&ShadowStackAllocatorStorage) ShadowStackAllocatorType(
          B.ShadowStackBuffer.Data, B.ShadowStackBuffer.Size);
      ShadowStackAllocator = reinterpret_cast<ShadowStackAllocatorType *>(
          &ShadowStackAllocatorStorage);

      new (&NodeIdPairAllocatorStorage) NodeIdPairAllocatorType(
          B.NodeIdPairBuffer.Data, B.NodeIdPairBuffer.Size);
      NodeIdPairAllocator = reinterpret_cast<NodeIdPairAllocatorType *>(
          &NodeIdPairAllocatorStorage);
    }

    explicit Allocators(uptr Max) XRAY_NEVER_INSTRUMENT {
      new (&NodeAllocatorStorage) NodeAllocatorType(Max);
      NodeAllocator =
          reinterpret_cast<NodeAllocatorType *>(&NodeAllocatorStorage);

      new (&RootAllocatorStorage) RootAllocatorType(Max);
      RootAllocator =
          reinterpret_cast<RootAllocatorType *>(&RootAllocatorStorage);

      new (&ShadowStackAllocatorStorage) ShadowStackAllocatorType(Max);
      ShadowStackAllocator = reinterpret_cast<ShadowStackAllocatorType *>(
          &ShadowStackAllocatorStorage);

      new (&NodeIdPairAllocatorStorage) NodeIdPairAllocatorType(Max);
      NodeIdPairAllocator = reinterpret_cast<NodeIdPairAllocatorType *>(
          &NodeIdPairAllocatorStorage);
    }

    Allocators(Allocators &&O) XRAY_NEVER_INSTRUMENT {
      // Here we rely on the safety of memcpy'ing contents of the storage
      // members, and then pointing the source pointers to nullptr.
      internal_memcpy(&NodeAllocatorStorage, &O.NodeAllocatorStorage,
                      sizeof(NodeAllocatorType));
      internal_memcpy(&RootAllocatorStorage, &O.RootAllocatorStorage,
                      sizeof(RootAllocatorType));
      internal_memcpy(&ShadowStackAllocatorStorage,
                      &O.ShadowStackAllocatorStorage,
                      sizeof(ShadowStackAllocatorType));
      internal_memcpy(&NodeIdPairAllocatorStorage,
                      &O.NodeIdPairAllocatorStorage,
                      sizeof(NodeIdPairAllocatorType));

      NodeAllocator =
          reinterpret_cast<NodeAllocatorType *>(&NodeAllocatorStorage);
      RootAllocator =
          reinterpret_cast<RootAllocatorType *>(&RootAllocatorStorage);
      ShadowStackAllocator = reinterpret_cast<ShadowStackAllocatorType *>(
          &ShadowStackAllocatorStorage);
      NodeIdPairAllocator = reinterpret_cast<NodeIdPairAllocatorType *>(
          &NodeIdPairAllocatorStorage);

      O.NodeAllocator = nullptr;
      O.RootAllocator = nullptr;
      O.ShadowStackAllocator = nullptr;
      O.NodeIdPairAllocator = nullptr;
    }

    Allocators &operator=(Allocators &&O) XRAY_NEVER_INSTRUMENT {
      // When moving into an existing instance, we ensure that we clean up the
      // current allocators.
      if (NodeAllocator)
        NodeAllocator->~NodeAllocatorType();
      if (O.NodeAllocator) {
        new (&NodeAllocatorStorage)
            NodeAllocatorType(std::move(*O.NodeAllocator));
        NodeAllocator =
            reinterpret_cast<NodeAllocatorType *>(&NodeAllocatorStorage);
        O.NodeAllocator = nullptr;
      } else {
        NodeAllocator = nullptr;
      }

      if (RootAllocator)
        RootAllocator->~RootAllocatorType();
      if (O.RootAllocator) {
        new (&RootAllocatorStorage)
            RootAllocatorType(std::move(*O.RootAllocator));
        RootAllocator =
            reinterpret_cast<RootAllocatorType *>(&RootAllocatorStorage);
        O.RootAllocator = nullptr;
      } else {
        RootAllocator = nullptr;
      }

      if (ShadowStackAllocator)
        ShadowStackAllocator->~ShadowStackAllocatorType();
      if (O.ShadowStackAllocator) {
        new (&ShadowStackAllocatorStorage)
            ShadowStackAllocatorType(std::move(*O.ShadowStackAllocator));
        ShadowStackAllocator = reinterpret_cast<ShadowStackAllocatorType *>(
            &ShadowStackAllocatorStorage);
        O.ShadowStackAllocator = nullptr;
      } else {
        ShadowStackAllocator = nullptr;
      }

      if (NodeIdPairAllocator)
        NodeIdPairAllocator->~NodeIdPairAllocatorType();
      if (O.NodeIdPairAllocator) {
        new (&NodeIdPairAllocatorStorage)
            NodeIdPairAllocatorType(std::move(*O.NodeIdPairAllocator));
        NodeIdPairAllocator = reinterpret_cast<NodeIdPairAllocatorType *>(
            &NodeIdPairAllocatorStorage);
        O.NodeIdPairAllocator = nullptr;
      } else {
        NodeIdPairAllocator = nullptr;
      }

      return *this;
    }

    ~Allocators() XRAY_NEVER_INSTRUMENT {
      if (NodeAllocator != nullptr)
        NodeAllocator->~NodeAllocatorType();
      if (RootAllocator != nullptr)
        RootAllocator->~RootAllocatorType();
      if (ShadowStackAllocator != nullptr)
        ShadowStackAllocator->~ShadowStackAllocatorType();
      if (NodeIdPairAllocator != nullptr)
        NodeIdPairAllocator->~NodeIdPairAllocatorType();
    }
  };

  static Allocators InitAllocators() XRAY_NEVER_INSTRUMENT {
    return InitAllocatorsCustom(profilingFlags()->per_thread_allocator_max);
  }

  static Allocators InitAllocatorsCustom(uptr Max) XRAY_NEVER_INSTRUMENT {
    Allocators A(Max);
    return A;
  }

  static Allocators
  InitAllocatorsFromBuffers(Allocators::Buffers &Bufs) XRAY_NEVER_INSTRUMENT {
    Allocators A(Bufs);
    return A;
  }

private:
  NodeArray Nodes;
  RootArray Roots;
  ShadowStackArray ShadowStack;
  NodeIdPairAllocatorType *NodeIdPairAllocator;
  uint32_t OverflowedFunctions;

public:
  explicit FunctionCallTrie(const Allocators &A) XRAY_NEVER_INSTRUMENT
      : Nodes(*A.NodeAllocator),
        Roots(*A.RootAllocator),
        ShadowStack(*A.ShadowStackAllocator),
        NodeIdPairAllocator(A.NodeIdPairAllocator),
        OverflowedFunctions(0) {}

  FunctionCallTrie() = delete;
  FunctionCallTrie(const FunctionCallTrie &) = delete;
  FunctionCallTrie &operator=(const FunctionCallTrie &) = delete;

  FunctionCallTrie(FunctionCallTrie &&O) XRAY_NEVER_INSTRUMENT
      : Nodes(std::move(O.Nodes)),
        Roots(std::move(O.Roots)),
        ShadowStack(std::move(O.ShadowStack)),
        NodeIdPairAllocator(O.NodeIdPairAllocator),
        OverflowedFunctions(O.OverflowedFunctions) {}

  FunctionCallTrie &operator=(FunctionCallTrie &&O) XRAY_NEVER_INSTRUMENT {
    Nodes = std::move(O.Nodes);
    Roots = std::move(O.Roots);
    ShadowStack = std::move(O.ShadowStack);
    NodeIdPairAllocator = O.NodeIdPairAllocator;
    OverflowedFunctions = O.OverflowedFunctions;
    return *this;
  }

  ~FunctionCallTrie() XRAY_NEVER_INSTRUMENT {}

  void enterFunction(const int32_t FId, uint64_t TSC,
                     uint16_t CPU) XRAY_NEVER_INSTRUMENT {
    DCHECK_NE(FId, 0);

    // If we're already overflowed the function call stack, do not bother
    // attempting to record any more function entries.
    if (UNLIKELY(OverflowedFunctions)) {
      ++OverflowedFunctions;
      return;
    }

    // If this is the first function we've encountered, we want to set up the
    // node(s) and treat it as a root.
    if (UNLIKELY(ShadowStack.empty())) {
      auto *NewRoot = Nodes.AppendEmplace(
          nullptr, NodeIdPairArray(*NodeIdPairAllocator), 0u, 0u, FId);
      if (UNLIKELY(NewRoot == nullptr))
        return;
      if (Roots.AppendEmplace(NewRoot) == nullptr) {
        Nodes.trim(1);
        return;
      }
      if (ShadowStack.AppendEmplace(TSC, NewRoot, CPU) == nullptr) {
        Nodes.trim(1);
        Roots.trim(1);
        ++OverflowedFunctions;
        return;
      }
      return;
    }

    // From this point on, we require that the stack is not empty.
    DCHECK(!ShadowStack.empty());
    auto TopNode = ShadowStack.back().NodePtr;
    DCHECK_NE(TopNode, nullptr);

    // If we've seen this callee before, then we access that node and place that
    // on the top of the stack.
    auto* Callee = TopNode->Callees.find_element(
        [FId](const NodeIdPair &NR) { return NR.FId == FId; });
    if (Callee != nullptr) {
      CHECK_NE(Callee->NodePtr, nullptr);
      if (ShadowStack.AppendEmplace(TSC, Callee->NodePtr, CPU) == nullptr)
        ++OverflowedFunctions;
      return;
    }

    // This means we've never seen this stack before, create a new node here.
    auto* NewNode = Nodes.AppendEmplace(
        TopNode, NodeIdPairArray(*NodeIdPairAllocator), 0u, 0u, FId);
    if (UNLIKELY(NewNode == nullptr))
      return;
    DCHECK_NE(NewNode, nullptr);
    TopNode->Callees.AppendEmplace(NewNode, FId);
    if (ShadowStack.AppendEmplace(TSC, NewNode, CPU) == nullptr)
      ++OverflowedFunctions;
    return;
  }

  void exitFunction(int32_t FId, uint64_t TSC,
                    uint16_t CPU) XRAY_NEVER_INSTRUMENT {
    // If we're exiting functions that have "overflowed" or don't fit into the
    // stack due to allocator constraints, we then decrement that count first.
    if (OverflowedFunctions) {
      --OverflowedFunctions;
      return;
    }

    // When we exit a function, we look up the ShadowStack to see whether we've
    // entered this function before. We do as little processing here as we can,
    // since most of the hard work would have already been done at function
    // entry.
    uint64_t CumulativeTreeTime = 0;

    while (!ShadowStack.empty()) {
      const auto &Top = ShadowStack.back();
      auto TopNode = Top.NodePtr;
      DCHECK_NE(TopNode, nullptr);

      // We may encounter overflow on the TSC we're provided, which may end up
      // being less than the TSC when we first entered the function.
      //
      // To get the accurate measurement of cycles, we need to check whether
      // we've overflowed (TSC < Top.EntryTSC) and then account the difference
      // between the entry TSC and the max for the TSC counter (max of uint64_t)
      // then add the value of TSC. We can prove that the maximum delta we will
      // get is at most the 64-bit unsigned value, since the difference between
      // a TSC of 0 and a Top.EntryTSC of 1 is (numeric_limits<uint64_t>::max()
      // - 1) + 1.
      //
      // NOTE: This assumes that TSCs are synchronised across CPUs.
      // TODO: Count the number of times we've seen CPU migrations.
      uint64_t LocalTime =
          Top.EntryTSC > TSC
              ? (std::numeric_limits<uint64_t>::max() - Top.EntryTSC) + TSC
              : TSC - Top.EntryTSC;
      TopNode->CallCount++;
      TopNode->CumulativeLocalTime += LocalTime - CumulativeTreeTime;
      CumulativeTreeTime += LocalTime;
      ShadowStack.trim(1);

      // TODO: Update the histogram for the node.
      if (TopNode->FId == FId)
        break;
    }
  }

  const RootArray &getRoots() const XRAY_NEVER_INSTRUMENT { return Roots; }

  // The deepCopyInto operation will update the provided FunctionCallTrie by
  // re-creating the contents of this particular FunctionCallTrie in the other
  // FunctionCallTrie. It will do this using a Depth First Traversal from the
  // roots, and while doing so recreating the traversal in the provided
  // FunctionCallTrie.
  //
  // This operation will *not* destroy the state in `O`, and thus may cause some
  // duplicate entries in `O` if it is not empty.
  //
  // This function is *not* thread-safe, and may require external
  // synchronisation of both "this" and |O|.
  //
  // This function must *not* be called with a non-empty FunctionCallTrie |O|.
  void deepCopyInto(FunctionCallTrie &O) const XRAY_NEVER_INSTRUMENT {
    DCHECK(O.getRoots().empty());

    // We then push the root into a stack, to use as the parent marker for new
    // nodes we push in as we're traversing depth-first down the call tree.
    struct NodeAndParent {
      FunctionCallTrie::Node *Node;
      FunctionCallTrie::Node *NewNode;
    };
    using Stack = Array<NodeAndParent>;

    typename Stack::AllocatorType StackAllocator(
        profilingFlags()->stack_allocator_max);
    Stack DFSStack(StackAllocator);

    for (const auto Root : getRoots()) {
      // Add a node in O for this root.
      auto NewRoot = O.Nodes.AppendEmplace(
          nullptr, NodeIdPairArray(*O.NodeIdPairAllocator), Root->CallCount,
          Root->CumulativeLocalTime, Root->FId);

      // Because we cannot allocate more memory we should bail out right away.
      if (UNLIKELY(NewRoot == nullptr))
        return;

      if (UNLIKELY(O.Roots.Append(NewRoot) == nullptr))
        return;

      // TODO: Figure out what to do if we fail to allocate any more stack
      // space. Maybe warn or report once?
      if (DFSStack.AppendEmplace(Root, NewRoot) == nullptr)
        return;
      while (!DFSStack.empty()) {
        NodeAndParent NP = DFSStack.back();
        DCHECK_NE(NP.Node, nullptr);
        DCHECK_NE(NP.NewNode, nullptr);
        DFSStack.trim(1);
        for (const auto Callee : NP.Node->Callees) {
          auto NewNode = O.Nodes.AppendEmplace(
              NP.NewNode, NodeIdPairArray(*O.NodeIdPairAllocator),
              Callee.NodePtr->CallCount, Callee.NodePtr->CumulativeLocalTime,
              Callee.FId);
          if (UNLIKELY(NewNode == nullptr))
            return;
          if (UNLIKELY(NP.NewNode->Callees.AppendEmplace(NewNode, Callee.FId) ==
                       nullptr))
            return;
          if (UNLIKELY(DFSStack.AppendEmplace(Callee.NodePtr, NewNode) ==
                       nullptr))
            return;
        }
      }
    }
  }

  // The mergeInto operation will update the provided FunctionCallTrie by
  // traversing the current trie's roots and updating (i.e. merging) the data in
  // the nodes with the data in the target's nodes. If the node doesn't exist in
  // the provided trie, we add a new one in the right position, and inherit the
  // data from the original (current) trie, along with all its callees.
  //
  // This function is *not* thread-safe, and may require external
  // synchronisation of both "this" and |O|.
  void mergeInto(FunctionCallTrie &O) const XRAY_NEVER_INSTRUMENT {
    struct NodeAndTarget {
      FunctionCallTrie::Node *OrigNode;
      FunctionCallTrie::Node *TargetNode;
    };
    using Stack = Array<NodeAndTarget>;
    typename Stack::AllocatorType StackAllocator(
        profilingFlags()->stack_allocator_max);
    Stack DFSStack(StackAllocator);

    for (const auto Root : getRoots()) {
      Node *TargetRoot = nullptr;
      auto R = O.Roots.find_element(
          [&](const Node *Node) { return Node->FId == Root->FId; });
      if (R == nullptr) {
        TargetRoot = O.Nodes.AppendEmplace(
            nullptr, NodeIdPairArray(*O.NodeIdPairAllocator), 0u, 0u,
            Root->FId);
        if (UNLIKELY(TargetRoot == nullptr))
          return;

        O.Roots.Append(TargetRoot);
      } else {
        TargetRoot = *R;
      }

      DFSStack.AppendEmplace(Root, TargetRoot);
      while (!DFSStack.empty()) {
        NodeAndTarget NT = DFSStack.back();
        DCHECK_NE(NT.OrigNode, nullptr);
        DCHECK_NE(NT.TargetNode, nullptr);
        DFSStack.trim(1);
        // TODO: Update the histogram as well when we have it ready.
        NT.TargetNode->CallCount += NT.OrigNode->CallCount;
        NT.TargetNode->CumulativeLocalTime += NT.OrigNode->CumulativeLocalTime;
        for (const auto Callee : NT.OrigNode->Callees) {
          auto TargetCallee = NT.TargetNode->Callees.find_element(
              [&](const FunctionCallTrie::NodeIdPair &C) {
                return C.FId == Callee.FId;
              });
          if (TargetCallee == nullptr) {
            auto NewTargetNode = O.Nodes.AppendEmplace(
                NT.TargetNode, NodeIdPairArray(*O.NodeIdPairAllocator), 0u, 0u,
                Callee.FId);

            if (UNLIKELY(NewTargetNode == nullptr))
              return;

            TargetCallee =
                NT.TargetNode->Callees.AppendEmplace(NewTargetNode, Callee.FId);
          }
          DFSStack.AppendEmplace(Callee.NodePtr, TargetCallee->NodePtr);
        }
      }
    }
  }
};

} // namespace __xray

#endif // XRAY_FUNCTION_CALL_TRIE_H
