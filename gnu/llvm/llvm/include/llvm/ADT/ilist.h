//==-- llvm/ADT/ilist.h - Intrusive Linked List Template ---------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines classes to implement an intrusive doubly linked list class
/// (i.e. each node of the list must contain a next and previous field for the
/// list.
///
/// The ilist class itself should be a plug in replacement for list.  This list
/// replacement does not provide a constant time size() method, so be careful to
/// use empty() when you really want to know if it's empty.
///
/// The ilist class is implemented as a circular list.  The list itself contains
/// a sentinel node, whose Next points at begin() and whose Prev points at
/// rbegin().  The sentinel node itself serves as end() and rend().
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_H
#define LLVM_ADT_ILIST_H

#include "llvm/ADT/simple_ilist.h"
#include <cassert>
#include <cstddef>
#include <iterator>

namespace llvm {

/// Use delete by default for iplist and ilist.
///
/// Specialize this to get different behaviour for ownership-related API.  (If
/// you really want ownership semantics, consider using std::list or building
/// something like \a BumpPtrList.)
///
/// \see ilist_noalloc_traits
template <typename NodeTy> struct ilist_alloc_traits {
  static void deleteNode(NodeTy *V) { delete V; }
};

/// Custom traits to do nothing on deletion.
///
/// Specialize ilist_alloc_traits to inherit from this to disable the
/// non-intrusive deletion in iplist (which implies ownership).
///
/// If you want purely intrusive semantics with no callbacks, consider using \a
/// simple_ilist instead.
///
/// \code
/// template <>
/// struct ilist_alloc_traits<MyType> : ilist_noalloc_traits<MyType> {};
/// \endcode
template <typename NodeTy> struct ilist_noalloc_traits {
  static void deleteNode(NodeTy *V) {}
};

/// Callbacks do nothing by default in iplist and ilist.
///
/// Specialize this for to use callbacks for when nodes change their list
/// membership.
template <typename NodeTy> struct ilist_callback_traits {
  void addNodeToList(NodeTy *) {}
  void removeNodeFromList(NodeTy *) {}

  /// Callback before transferring nodes to this list. The nodes may already be
  /// in this same list.
  template <class Iterator>
  void transferNodesFromList(ilist_callback_traits &OldList, Iterator /*first*/,
                             Iterator /*last*/) {
    (void)OldList;
  }
};

/// A fragment for template traits for intrusive list that provides default
/// node related operations.
///
/// TODO: Remove this layer of indirection.  It's not necessary.
template <typename NodeTy>
struct ilist_node_traits : ilist_alloc_traits<NodeTy>,
                           ilist_callback_traits<NodeTy> {};

/// Template traits for intrusive list.
///
/// Customize callbacks and allocation semantics.
template <typename NodeTy>
struct ilist_traits : public ilist_node_traits<NodeTy> {};

/// Const traits should never be instantiated.
template <typename Ty> struct ilist_traits<const Ty> {};

//===----------------------------------------------------------------------===//
//
/// A wrapper around an intrusive list with callbacks and non-intrusive
/// ownership.
///
/// This wraps a purely intrusive list (like simple_ilist) with a configurable
/// traits class.  The traits can implement callbacks and customize the
/// ownership semantics.
///
/// This is a subset of ilist functionality that can safely be used on nodes of
/// polymorphic types, i.e. a heterogeneous list with a common base class that
/// holds the next/prev pointers.  The only state of the list itself is an
/// ilist_sentinel, which holds pointers to the first and last nodes in the
/// list.
template <class IntrusiveListT, class TraitsT>
class iplist_impl : public TraitsT, IntrusiveListT {
  typedef IntrusiveListT base_list_type;

public:
  typedef typename base_list_type::pointer pointer;
  typedef typename base_list_type::const_pointer const_pointer;
  typedef typename base_list_type::reference reference;
  typedef typename base_list_type::const_reference const_reference;
  typedef typename base_list_type::value_type value_type;
  typedef typename base_list_type::size_type size_type;
  typedef typename base_list_type::difference_type difference_type;
  typedef typename base_list_type::iterator iterator;
  typedef typename base_list_type::const_iterator const_iterator;
  typedef typename base_list_type::reverse_iterator reverse_iterator;
  typedef
      typename base_list_type::const_reverse_iterator const_reverse_iterator;

private:
  static bool op_less(const_reference L, const_reference R) { return L < R; }
  static bool op_equal(const_reference L, const_reference R) { return L == R; }

public:
  iplist_impl() = default;

  iplist_impl(const iplist_impl &) = delete;
  iplist_impl &operator=(const iplist_impl &) = delete;

  iplist_impl(iplist_impl &&X)
      : TraitsT(std::move(static_cast<TraitsT &>(X))),
        IntrusiveListT(std::move(static_cast<IntrusiveListT &>(X))) {}
  iplist_impl &operator=(iplist_impl &&X) {
    *static_cast<TraitsT *>(this) = std::move(static_cast<TraitsT &>(X));
    *static_cast<IntrusiveListT *>(this) =
        std::move(static_cast<IntrusiveListT &>(X));
    return *this;
  }

  ~iplist_impl() { clear(); }

  // Miscellaneous inspection routines.
  size_type max_size() const { return size_type(-1); }

  using base_list_type::begin;
  using base_list_type::end;
  using base_list_type::rbegin;
  using base_list_type::rend;
  using base_list_type::empty;
  using base_list_type::front;
  using base_list_type::back;

  void swap(iplist_impl &RHS) {
    assert(0 && "Swap does not use list traits callback correctly yet!");
    base_list_type::swap(RHS);
  }

  iterator insert(iterator where, pointer New) {
    this->addNodeToList(New); // Notify traits that we added a node...
    return base_list_type::insert(where, *New);
  }

  iterator insert(iterator where, const_reference New) {
    return this->insert(where, new value_type(New));
  }

  iterator insertAfter(iterator where, pointer New) {
    if (empty())
      return insert(begin(), New);
    else
      return insert(++where, New);
  }

  /// Clone another list.
  template <class Cloner> void cloneFrom(const iplist_impl &L2, Cloner clone) {
    clear();
    for (const_reference V : L2)
      push_back(clone(V));
  }

  pointer remove(iterator &IT) {
    pointer Node = &*IT++;
    this->removeNodeFromList(Node); // Notify traits that we removed a node...
    base_list_type::remove(*Node);
    return Node;
  }

  pointer remove(const iterator &IT) {
    iterator MutIt = IT;
    return remove(MutIt);
  }

  pointer remove(pointer IT) { return remove(iterator(IT)); }
  pointer remove(reference IT) { return remove(iterator(IT)); }

  // erase - remove a node from the controlled sequence... and delete it.
  iterator erase(iterator where) {
    this->deleteNode(remove(where));
    return where;
  }

  iterator erase(pointer IT) { return erase(iterator(IT)); }
  iterator erase(reference IT) { return erase(iterator(IT)); }

  /// Remove all nodes from the list like clear(), but do not call
  /// removeNodeFromList() or deleteNode().
  ///
  /// This should only be used immediately before freeing nodes in bulk to
  /// avoid traversing the list and bringing all the nodes into cache.
  void clearAndLeakNodesUnsafely() { base_list_type::clear(); }

private:
  // transfer - The heart of the splice function.  Move linked list nodes from
  // [first, last) into position.
  //
  void transfer(iterator position, iplist_impl &L2, iterator first, iterator last) {
    if (position == last)
      return;

    // Notify traits we moved the nodes...
    this->transferNodesFromList(L2, first, last);

    base_list_type::splice(position, L2, first, last);
  }

public:
  //===----------------------------------------------------------------------===
  // Functionality derived from other functions defined above...
  //

  using base_list_type::size;

  iterator erase(iterator first, iterator last) {
    while (first != last)
      first = erase(first);
    return last;
  }

  void clear() { erase(begin(), end()); }

  // Front and back inserters...
  void push_front(pointer val) { insert(begin(), val); }
  void push_back(pointer val) { insert(end(), val); }
  void pop_front() {
    assert(!empty() && "pop_front() on empty list!");
    erase(begin());
  }
  void pop_back() {
    assert(!empty() && "pop_back() on empty list!");
    iterator t = end(); erase(--t);
  }

  // Special forms of insert...
  template<class InIt> void insert(iterator where, InIt first, InIt last) {
    for (; first != last; ++first) insert(where, *first);
  }

  // Splice members - defined in terms of transfer...
  void splice(iterator where, iplist_impl &L2) {
    if (!L2.empty())
      transfer(where, L2, L2.begin(), L2.end());
  }
  void splice(iterator where, iplist_impl &L2, iterator first) {
    iterator last = first; ++last;
    if (where == first || where == last) return; // No change
    transfer(where, L2, first, last);
  }
  void splice(iterator where, iplist_impl &L2, iterator first, iterator last) {
    if (first != last) transfer(where, L2, first, last);
  }
  void splice(iterator where, iplist_impl &L2, reference N) {
    splice(where, L2, iterator(N));
  }
  void splice(iterator where, iplist_impl &L2, pointer N) {
    splice(where, L2, iterator(N));
  }

  template <class Compare>
  void merge(iplist_impl &Right, Compare comp) {
    if (this == &Right)
      return;
    this->transferNodesFromList(Right, Right.begin(), Right.end());
    base_list_type::merge(Right, comp);
  }
  void merge(iplist_impl &Right) { return merge(Right, op_less); }

  using base_list_type::sort;

  /// Get the previous node, or \c nullptr for the list head.
  pointer getPrevNode(reference N) const {
    auto I = N.getIterator();
    if (I == begin())
      return nullptr;
    return &*std::prev(I);
  }
  /// Get the previous node, or \c nullptr for the list head.
  const_pointer getPrevNode(const_reference N) const {
    return getPrevNode(const_cast<reference >(N));
  }

  /// Get the next node, or \c nullptr for the list tail.
  pointer getNextNode(reference N) const {
    auto Next = std::next(N.getIterator());
    if (Next == end())
      return nullptr;
    return &*Next;
  }
  /// Get the next node, or \c nullptr for the list tail.
  const_pointer getNextNode(const_reference N) const {
    return getNextNode(const_cast<reference >(N));
  }
};

/// An intrusive list with ownership and callbacks specified/controlled by
/// ilist_traits, only with API safe for polymorphic types.
///
/// The \p Options parameters are the same as those for \a simple_ilist.  See
/// there for a description of what's available.
template <class T, class... Options>
class iplist
    : public iplist_impl<simple_ilist<T, Options...>, ilist_traits<T>> {
  using iplist_impl_type = typename iplist::iplist_impl;

public:
  iplist() = default;

  iplist(const iplist &X) = delete;
  iplist &operator=(const iplist &X) = delete;

  iplist(iplist &&X) : iplist_impl_type(std::move(X)) {}
  iplist &operator=(iplist &&X) {
    *static_cast<iplist_impl_type *>(this) = std::move(X);
    return *this;
  }
};

template <class T, class... Options> using ilist = iplist<T, Options...>;

} // end namespace llvm

namespace std {

  // Ensure that swap uses the fast list swap...
  template<class Ty>
  void swap(llvm::iplist<Ty> &Left, llvm::iplist<Ty> &Right) {
    Left.swap(Right);
  }

} // end namespace std

#endif // LLVM_ADT_ILIST_H
