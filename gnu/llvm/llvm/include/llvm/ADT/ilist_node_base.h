//===- llvm/ADT/ilist_node_base.h - Intrusive List Node Base -----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_NODE_BASE_H
#define LLVM_ADT_ILIST_NODE_BASE_H

#include "llvm/ADT/PointerIntPair.h"

namespace llvm {

namespace ilist_detail {

template <class NodeBase, bool EnableSentinelTracking> class node_base_prevnext;

template <class NodeBase> class node_base_prevnext<NodeBase, false> {
  NodeBase *Prev = nullptr;
  NodeBase *Next = nullptr;

public:
  void setPrev(NodeBase *Prev) { this->Prev = Prev; }
  void setNext(NodeBase *Next) { this->Next = Next; }
  NodeBase *getPrev() const { return Prev; }
  NodeBase *getNext() const { return Next; }

  bool isKnownSentinel() const { return false; }
  void initializeSentinel() {}
};

template <class NodeBase> class node_base_prevnext<NodeBase, true> {
  PointerIntPair<NodeBase *, 1> PrevAndSentinel;
  NodeBase *Next = nullptr;

public:
  void setPrev(NodeBase *Prev) { PrevAndSentinel.setPointer(Prev); }
  void setNext(NodeBase *Next) { this->Next = Next; }
  NodeBase *getPrev() const { return PrevAndSentinel.getPointer(); }
  NodeBase *getNext() const { return Next; }

  bool isSentinel() const { return PrevAndSentinel.getInt(); }
  bool isKnownSentinel() const { return isSentinel(); }
  void initializeSentinel() { PrevAndSentinel.setInt(true); }
};

template <class ParentTy> class node_base_parent {
  ParentTy *Parent = nullptr;

public:
  void setNodeBaseParent(ParentTy *Parent) { this->Parent = Parent; }
  inline const ParentTy *getNodeBaseParent() const { return Parent; }
  inline ParentTy *getNodeBaseParent() { return Parent; }
};
template <> class node_base_parent<void> {};

} // end namespace ilist_detail

/// Base class for ilist nodes.
///
/// Optionally tracks whether this node is the sentinel.
template <bool EnableSentinelTracking, class ParentTy>
class ilist_node_base : public ilist_detail::node_base_prevnext<
                            ilist_node_base<EnableSentinelTracking, ParentTy>,
                            EnableSentinelTracking>,
                        public ilist_detail::node_base_parent<ParentTy> {};

} // end namespace llvm

#endif // LLVM_ADT_ILIST_NODE_BASE_H
