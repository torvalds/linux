//===- llvm/ADT/AllocatorList.h - Custom allocator list ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ALLOCATORLIST_H
#define LLVM_ADT_ALLOCATORLIST_H

#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace llvm {

/// A linked-list with a custom, local allocator.
///
/// Expose a std::list-like interface that owns and uses a custom LLVM-style
/// allocator (e.g., BumpPtrAllocator), leveraging \a simple_ilist for the
/// implementation details.
///
/// Because this list owns the allocator, calling \a splice() with a different
/// list isn't generally safe.  As such, \a splice has been left out of the
/// interface entirely.
template <class T, class AllocatorT> class AllocatorList : AllocatorT {
  struct Node : ilist_node<Node> {
    Node(Node &&) = delete;
    Node(const Node &) = delete;
    Node &operator=(Node &&) = delete;
    Node &operator=(const Node &) = delete;

    Node(T &&V) : V(std::move(V)) {}
    Node(const T &V) : V(V) {}
    template <class... Ts> Node(Ts &&... Vs) : V(std::forward<Ts>(Vs)...) {}
    T V;
  };

  using list_type = simple_ilist<Node>;

  list_type List;

  AllocatorT &getAlloc() { return *this; }
  const AllocatorT &getAlloc() const { return *this; }

  template <class... ArgTs> Node *create(ArgTs &&... Args) {
    return new (getAlloc()) Node(std::forward<ArgTs>(Args)...);
  }

  struct Cloner {
    AllocatorList &AL;

    Cloner(AllocatorList &AL) : AL(AL) {}

    Node *operator()(const Node &N) const { return AL.create(N.V); }
  };

  struct Disposer {
    AllocatorList &AL;

    Disposer(AllocatorList &AL) : AL(AL) {}

    void operator()(Node *N) const {
      N->~Node();
      AL.getAlloc().Deallocate(N);
    }
  };

public:
  using value_type = T;
  using pointer = T *;
  using reference = T &;
  using const_pointer = const T *;
  using const_reference = const T &;
  using size_type = typename list_type::size_type;
  using difference_type = typename list_type::difference_type;

private:
  template <class ValueT, class IteratorBase>
  class IteratorImpl
      : public iterator_adaptor_base<IteratorImpl<ValueT, IteratorBase>,
                                     IteratorBase,
                                     std::bidirectional_iterator_tag, ValueT> {
    template <class OtherValueT, class OtherIteratorBase>
    friend class IteratorImpl;
    friend AllocatorList;

    using base_type =
        iterator_adaptor_base<IteratorImpl<ValueT, IteratorBase>, IteratorBase,
                              std::bidirectional_iterator_tag, ValueT>;

  public:
    using value_type = ValueT;
    using pointer = ValueT *;
    using reference = ValueT &;

    IteratorImpl() = default;
    IteratorImpl(const IteratorImpl &) = default;
    IteratorImpl &operator=(const IteratorImpl &) = default;

    explicit IteratorImpl(const IteratorBase &I) : base_type(I) {}

    template <class OtherValueT, class OtherIteratorBase>
    IteratorImpl(const IteratorImpl<OtherValueT, OtherIteratorBase> &X,
                 std::enable_if_t<std::is_convertible<
                     OtherIteratorBase, IteratorBase>::value> * = nullptr)
        : base_type(X.wrapped()) {}

    ~IteratorImpl() = default;

    reference operator*() const { return base_type::wrapped()->V; }
    pointer operator->() const { return &operator*(); }
  };

public:
  using iterator = IteratorImpl<T, typename list_type::iterator>;
  using reverse_iterator =
      IteratorImpl<T, typename list_type::reverse_iterator>;
  using const_iterator =
      IteratorImpl<const T, typename list_type::const_iterator>;
  using const_reverse_iterator =
      IteratorImpl<const T, typename list_type::const_reverse_iterator>;

  AllocatorList() = default;
  AllocatorList(AllocatorList &&X)
      : AllocatorT(std::move(X.getAlloc())), List(std::move(X.List)) {}

  AllocatorList(const AllocatorList &X) {
    List.cloneFrom(X.List, Cloner(*this), Disposer(*this));
  }

  AllocatorList &operator=(AllocatorList &&X) {
    clear(); // Dispose of current nodes explicitly.
    List = std::move(X.List);
    getAlloc() = std::move(X.getAlloc());
    return *this;
  }

  AllocatorList &operator=(const AllocatorList &X) {
    List.cloneFrom(X.List, Cloner(*this), Disposer(*this));
    return *this;
  }

  ~AllocatorList() { clear(); }

  void swap(AllocatorList &RHS) {
    List.swap(RHS.List);
    std::swap(getAlloc(), RHS.getAlloc());
  }

  bool empty() { return List.empty(); }
  size_t size() { return List.size(); }

  iterator begin() { return iterator(List.begin()); }
  iterator end() { return iterator(List.end()); }
  const_iterator begin() const { return const_iterator(List.begin()); }
  const_iterator end() const { return const_iterator(List.end()); }
  reverse_iterator rbegin() { return reverse_iterator(List.rbegin()); }
  reverse_iterator rend() { return reverse_iterator(List.rend()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(List.rbegin());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(List.rend());
  }

  T &back() { return List.back().V; }
  T &front() { return List.front().V; }
  const T &back() const { return List.back().V; }
  const T &front() const { return List.front().V; }

  template <class... Ts> iterator emplace(iterator I, Ts &&... Vs) {
    return iterator(List.insert(I.wrapped(), *create(std::forward<Ts>(Vs)...)));
  }

  iterator insert(iterator I, T &&V) {
    return iterator(List.insert(I.wrapped(), *create(std::move(V))));
  }
  iterator insert(iterator I, const T &V) {
    return iterator(List.insert(I.wrapped(), *create(V)));
  }

  template <class Iterator>
  void insert(iterator I, Iterator First, Iterator Last) {
    for (; First != Last; ++First)
      List.insert(I.wrapped(), *create(*First));
  }

  iterator erase(iterator I) {
    return iterator(List.eraseAndDispose(I.wrapped(), Disposer(*this)));
  }

  iterator erase(iterator First, iterator Last) {
    return iterator(
        List.eraseAndDispose(First.wrapped(), Last.wrapped(), Disposer(*this)));
  }

  void clear() { List.clearAndDispose(Disposer(*this)); }
  void pop_back() { List.eraseAndDispose(--List.end(), Disposer(*this)); }
  void pop_front() { List.eraseAndDispose(List.begin(), Disposer(*this)); }
  void push_back(T &&V) { insert(end(), std::move(V)); }
  void push_front(T &&V) { insert(begin(), std::move(V)); }
  void push_back(const T &V) { insert(end(), V); }
  void push_front(const T &V) { insert(begin(), V); }
  template <class... Ts> void emplace_back(Ts &&... Vs) {
    emplace(end(), std::forward<Ts>(Vs)...);
  }
  template <class... Ts> void emplace_front(Ts &&... Vs) {
    emplace(begin(), std::forward<Ts>(Vs)...);
  }

  /// Reset the underlying allocator.
  ///
  /// \pre \c empty()
  void resetAlloc() {
    assert(empty() && "Cannot reset allocator if not empty");
    getAlloc().Reset();
  }
};

template <class T> using BumpPtrList = AllocatorList<T, BumpPtrAllocator>;

} // end namespace llvm

#endif // LLVM_ADT_ALLOCATORLIST_H
