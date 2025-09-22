//===- llvm/ADT/PostOrderIterator.h - PostOrder iterator --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file builds on the ADT/GraphTraits.h file to build a generic graph
/// post order iterator.  This should work over any graph type that has a
/// GraphTraits specialization.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POSTORDERITERATOR_H
#define LLVM_ADT_POSTORDERITERATOR_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include <iterator>
#include <optional>
#include <set>
#include <utility>

namespace llvm {

// The po_iterator_storage template provides access to the set of already
// visited nodes during the po_iterator's depth-first traversal.
//
// The default implementation simply contains a set of visited nodes, while
// the External=true version uses a reference to an external set.
//
// It is possible to prune the depth-first traversal in several ways:
//
// - When providing an external set that already contains some graph nodes,
//   those nodes won't be visited again. This is useful for restarting a
//   post-order traversal on a graph with nodes that aren't dominated by a
//   single node.
//
// - By providing a custom SetType class, unwanted graph nodes can be excluded
//   by having the insert() function return false. This could for example
//   confine a CFG traversal to blocks in a specific loop.
//
// - Finally, by specializing the po_iterator_storage template itself, graph
//   edges can be pruned by returning false in the insertEdge() function. This
//   could be used to remove loop back-edges from the CFG seen by po_iterator.
//
// A specialized po_iterator_storage class can observe both the pre-order and
// the post-order. The insertEdge() function is called in a pre-order, while
// the finishPostorder() function is called just before the po_iterator moves
// on to the next node.

/// Default po_iterator_storage implementation with an internal set object.
template<class SetType, bool External>
class po_iterator_storage {
  SetType Visited;

public:
  // Return true if edge destination should be visited.
  template <typename NodeRef>
  bool insertEdge(std::optional<NodeRef> From, NodeRef To) {
    return Visited.insert(To).second;
  }

  // Called after all children of BB have been visited.
  template <typename NodeRef> void finishPostorder(NodeRef BB) {}
};

/// Specialization of po_iterator_storage that references an external set.
template<class SetType>
class po_iterator_storage<SetType, true> {
  SetType &Visited;

public:
  po_iterator_storage(SetType &VSet) : Visited(VSet) {}
  po_iterator_storage(const po_iterator_storage &S) : Visited(S.Visited) {}

  // Return true if edge destination should be visited, called with From = 0 for
  // the root node.
  // Graph edges can be pruned by specializing this function.
  template <class NodeRef>
  bool insertEdge(std::optional<NodeRef> From, NodeRef To) {
    return Visited.insert(To).second;
  }

  // Called after all children of BB have been visited.
  template <class NodeRef> void finishPostorder(NodeRef BB) {}
};

template <class GraphT,
          class SetType = SmallPtrSet<typename GraphTraits<GraphT>::NodeRef, 8>,
          bool ExtStorage = false, class GT = GraphTraits<GraphT>>
class po_iterator : public po_iterator_storage<SetType, ExtStorage> {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = typename GT::NodeRef;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = const value_type &;

private:
  using NodeRef = typename GT::NodeRef;
  using ChildItTy = typename GT::ChildIteratorType;

  /// Used to maintain the ordering.
  /// First element is basic block pointer, second is iterator for the next
  /// child to visit, third is the end iterator.
  SmallVector<std::tuple<NodeRef, ChildItTy, ChildItTy>, 8> VisitStack;

  po_iterator(NodeRef BB) {
    this->insertEdge(std::optional<NodeRef>(), BB);
    VisitStack.emplace_back(BB, GT::child_begin(BB), GT::child_end(BB));
    traverseChild();
  }

  po_iterator() = default; // End is when stack is empty.

  po_iterator(NodeRef BB, SetType &S)
      : po_iterator_storage<SetType, ExtStorage>(S) {
    if (this->insertEdge(std::optional<NodeRef>(), BB)) {
      VisitStack.emplace_back(BB, GT::child_begin(BB), GT::child_end(BB));
      traverseChild();
    }
  }

  po_iterator(SetType &S)
      : po_iterator_storage<SetType, ExtStorage>(S) {
  } // End is when stack is empty.

  void traverseChild() {
    while (true) {
      auto &Entry = VisitStack.back();
      if (std::get<1>(Entry) == std::get<2>(Entry))
        break;
      NodeRef BB = *std::get<1>(Entry)++;
      if (this->insertEdge(std::optional<NodeRef>(std::get<0>(Entry)), BB)) {
        // If the block is not visited...
        VisitStack.emplace_back(BB, GT::child_begin(BB), GT::child_end(BB));
      }
    }
  }

public:
  // Provide static "constructors"...
  static po_iterator begin(const GraphT &G) {
    return po_iterator(GT::getEntryNode(G));
  }
  static po_iterator end(const GraphT &G) { return po_iterator(); }

  static po_iterator begin(const GraphT &G, SetType &S) {
    return po_iterator(GT::getEntryNode(G), S);
  }
  static po_iterator end(const GraphT &G, SetType &S) { return po_iterator(S); }

  bool operator==(const po_iterator &x) const {
    return VisitStack == x.VisitStack;
  }
  bool operator!=(const po_iterator &x) const { return !(*this == x); }

  reference operator*() const { return std::get<0>(VisitStack.back()); }

  // This is a nonstandard operator-> that dereferences the pointer an extra
  // time... so that you can actually call methods ON the BasicBlock, because
  // the contained type is a pointer.  This allows BBIt->getTerminator() f.e.
  //
  NodeRef operator->() const { return **this; }

  po_iterator &operator++() { // Preincrement
    this->finishPostorder(std::get<0>(VisitStack.back()));
    VisitStack.pop_back();
    if (!VisitStack.empty())
      traverseChild();
    return *this;
  }

  po_iterator operator++(int) { // Postincrement
    po_iterator tmp = *this;
    ++*this;
    return tmp;
  }
};

// Provide global constructors that automatically figure out correct types...
//
template <class T>
po_iterator<T> po_begin(const T &G) { return po_iterator<T>::begin(G); }
template <class T>
po_iterator<T> po_end  (const T &G) { return po_iterator<T>::end(G); }

template <class T> iterator_range<po_iterator<T>> post_order(const T &G) {
  return make_range(po_begin(G), po_end(G));
}

// Provide global definitions of external postorder iterators...
template <class T, class SetType = std::set<typename GraphTraits<T>::NodeRef>>
struct po_ext_iterator : public po_iterator<T, SetType, true> {
  po_ext_iterator(const po_iterator<T, SetType, true> &V) :
  po_iterator<T, SetType, true>(V) {}
};

template<class T, class SetType>
po_ext_iterator<T, SetType> po_ext_begin(T G, SetType &S) {
  return po_ext_iterator<T, SetType>::begin(G, S);
}

template<class T, class SetType>
po_ext_iterator<T, SetType> po_ext_end(T G, SetType &S) {
  return po_ext_iterator<T, SetType>::end(G, S);
}

template <class T, class SetType>
iterator_range<po_ext_iterator<T, SetType>> post_order_ext(const T &G, SetType &S) {
  return make_range(po_ext_begin(G, S), po_ext_end(G, S));
}

// Provide global definitions of inverse post order iterators...
template <class T, class SetType = std::set<typename GraphTraits<T>::NodeRef>,
          bool External = false>
struct ipo_iterator : public po_iterator<Inverse<T>, SetType, External> {
  ipo_iterator(const po_iterator<Inverse<T>, SetType, External> &V) :
     po_iterator<Inverse<T>, SetType, External> (V) {}
};

template <class T>
ipo_iterator<T> ipo_begin(const T &G) {
  return ipo_iterator<T>::begin(G);
}

template <class T>
ipo_iterator<T> ipo_end(const T &G){
  return ipo_iterator<T>::end(G);
}

template <class T>
iterator_range<ipo_iterator<T>> inverse_post_order(const T &G) {
  return make_range(ipo_begin(G), ipo_end(G));
}

// Provide global definitions of external inverse postorder iterators...
template <class T, class SetType = std::set<typename GraphTraits<T>::NodeRef>>
struct ipo_ext_iterator : public ipo_iterator<T, SetType, true> {
  ipo_ext_iterator(const ipo_iterator<T, SetType, true> &V) :
    ipo_iterator<T, SetType, true>(V) {}
  ipo_ext_iterator(const po_iterator<Inverse<T>, SetType, true> &V) :
    ipo_iterator<T, SetType, true>(V) {}
};

template <class T, class SetType>
ipo_ext_iterator<T, SetType> ipo_ext_begin(const T &G, SetType &S) {
  return ipo_ext_iterator<T, SetType>::begin(G, S);
}

template <class T, class SetType>
ipo_ext_iterator<T, SetType> ipo_ext_end(const T &G, SetType &S) {
  return ipo_ext_iterator<T, SetType>::end(G, S);
}

template <class T, class SetType>
iterator_range<ipo_ext_iterator<T, SetType>>
inverse_post_order_ext(const T &G, SetType &S) {
  return make_range(ipo_ext_begin(G, S), ipo_ext_end(G, S));
}

//===--------------------------------------------------------------------===//
// Reverse Post Order CFG iterator code
//===--------------------------------------------------------------------===//
//
// This is used to visit basic blocks in a method in reverse post order.  This
// class is awkward to use because I don't know a good incremental algorithm to
// computer RPO from a graph.  Because of this, the construction of the
// ReversePostOrderTraversal object is expensive (it must walk the entire graph
// with a postorder iterator to build the data structures).  The moral of this
// story is: Don't create more ReversePostOrderTraversal classes than necessary.
//
// Because it does the traversal in its constructor, it won't invalidate when
// BasicBlocks are removed, *but* it may contain erased blocks. Some places
// rely on this behavior (i.e. GVN).
//
// This class should be used like this:
// {
//   ReversePostOrderTraversal<Function*> RPOT(FuncPtr); // Expensive to create
//   for (rpo_iterator I = RPOT.begin(); I != RPOT.end(); ++I) {
//      ...
//   }
//   for (rpo_iterator I = RPOT.begin(); I != RPOT.end(); ++I) {
//      ...
//   }
// }
//

template<class GraphT, class GT = GraphTraits<GraphT>>
class ReversePostOrderTraversal {
  using NodeRef = typename GT::NodeRef;

  using VecTy = SmallVector<NodeRef, 8>;
  VecTy Blocks; // Block list in normal PO order

  void Initialize(const GraphT &G) {
    std::copy(po_begin(G), po_end(G), std::back_inserter(Blocks));
  }

public:
  using rpo_iterator = typename VecTy::reverse_iterator;
  using const_rpo_iterator = typename VecTy::const_reverse_iterator;

  ReversePostOrderTraversal(const GraphT &G) { Initialize(G); }

  // Because we want a reverse post order, use reverse iterators from the vector
  rpo_iterator begin() { return Blocks.rbegin(); }
  const_rpo_iterator begin() const { return Blocks.rbegin(); }
  rpo_iterator end() { return Blocks.rend(); }
  const_rpo_iterator end() const { return Blocks.rend(); }
};

} // end namespace llvm

#endif // LLVM_ADT_POSTORDERITERATOR_H
