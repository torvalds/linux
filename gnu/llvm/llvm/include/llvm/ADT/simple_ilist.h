//===- llvm/ADT/simple_ilist.h - Simple Intrusive List ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SIMPLE_ILIST_H
#define LLVM_ADT_SIMPLE_ILIST_H

#include "llvm/ADT/ilist_base.h"
#include "llvm/ADT/ilist_iterator.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/ilist_node_options.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <utility>

namespace llvm {

/// A simple intrusive list implementation.
///
/// This is a simple intrusive list for a \c T that inherits from \c
/// ilist_node<T>.  The list never takes ownership of anything inserted in it.
///
/// Unlike \a iplist<T> and \a ilist<T>, \a simple_ilist<T> never deletes
/// values, and has no callback traits.
///
/// The API for adding nodes include \a push_front(), \a push_back(), and \a
/// insert().  These all take values by reference (not by pointer), except for
/// the range version of \a insert().
///
/// There are three sets of API for discarding nodes from the list: \a
/// remove(), which takes a reference to the node to remove, \a erase(), which
/// takes an iterator or iterator range and returns the next one, and \a
/// clear(), which empties out the container.  All three are constant time
/// operations.  None of these deletes any nodes; in particular, if there is a
/// single node in the list, then these have identical semantics:
/// \li \c L.remove(L.front());
/// \li \c L.erase(L.begin());
/// \li \c L.clear();
///
/// As a convenience for callers, there are parallel APIs that take a \c
/// Disposer (such as \c std::default_delete<T>): \a removeAndDispose(), \a
/// eraseAndDispose(), and \a clearAndDispose().  These have different names
/// because the extra semantic is otherwise non-obvious.  They are equivalent
/// to calling \a std::for_each() on the range to be discarded.
///
/// The currently available \p Options customize the nodes in the list.  The
/// same options must be specified in the \a ilist_node instantiation for
/// compatibility (although the order is irrelevant).
/// \li Use \a ilist_tag to designate which ilist_node for a given \p T this
/// list should use.  This is useful if a type \p T is part of multiple,
/// independent lists simultaneously.
/// \li Use \a ilist_sentinel_tracking to always (or never) track whether a
/// node is a sentinel.  Specifying \c true enables the \a
/// ilist_node::isSentinel() API.  Unlike \a ilist_node::isKnownSentinel(),
/// which is only appropriate for assertions, \a ilist_node::isSentinel() is
/// appropriate for real logic.
///
/// Here are examples of \p Options usage:
/// \li \c simple_ilist<T> gives the defaults.  \li \c
/// simple_ilist<T,ilist_sentinel_tracking<true>> enables the \a
/// ilist_node::isSentinel() API.
/// \li \c simple_ilist<T,ilist_tag<A>,ilist_sentinel_tracking<false>>
/// specifies a tag of A and that tracking should be off (even when
/// LLVM_ENABLE_ABI_BREAKING_CHECKS are enabled).
/// \li \c simple_ilist<T,ilist_sentinel_tracking<false>,ilist_tag<A>> is
/// equivalent to the last.
///
/// See \a is_valid_option for steps on adding a new option.
template <typename T, class... Options>
class simple_ilist
    : ilist_detail::compute_node_options<T, Options...>::type::list_base_type,
      ilist_detail::SpecificNodeAccess<
          typename ilist_detail::compute_node_options<T, Options...>::type> {
  static_assert(ilist_detail::check_options<Options...>::value,
                "Unrecognized node option!");
  using OptionsT =
      typename ilist_detail::compute_node_options<T, Options...>::type;
  using list_base_type = typename OptionsT::list_base_type;
  ilist_sentinel<OptionsT> Sentinel;

public:
  using value_type = typename OptionsT::value_type;
  using pointer = typename OptionsT::pointer;
  using reference = typename OptionsT::reference;
  using const_pointer = typename OptionsT::const_pointer;
  using const_reference = typename OptionsT::const_reference;
  using iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          false, false>::type;
  using const_iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          false, true>::type;
  using reverse_iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          true, false>::type;
  using const_reverse_iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          true, true>::type;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  simple_ilist() = default;
  ~simple_ilist() = default;

  // No copy constructors.
  simple_ilist(const simple_ilist &) = delete;
  simple_ilist &operator=(const simple_ilist &) = delete;

  // Move constructors.
  simple_ilist(simple_ilist &&X) { splice(end(), X); }
  simple_ilist &operator=(simple_ilist &&X) {
    clear();
    splice(end(), X);
    return *this;
  }

  iterator begin() { return ++iterator(Sentinel); }
  const_iterator begin() const { return ++const_iterator(Sentinel); }
  iterator end() { return iterator(Sentinel); }
  const_iterator end() const { return const_iterator(Sentinel); }
  reverse_iterator rbegin() { return ++reverse_iterator(Sentinel); }
  const_reverse_iterator rbegin() const {
    return ++const_reverse_iterator(Sentinel);
  }
  reverse_iterator rend() { return reverse_iterator(Sentinel); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(Sentinel);
  }

  /// Check if the list is empty in constant time.
  [[nodiscard]] bool empty() const { return Sentinel.empty(); }

  /// Calculate the size of the list in linear time.
  [[nodiscard]] size_type size() const { return std::distance(begin(), end()); }

  reference front() { return *begin(); }
  const_reference front() const { return *begin(); }
  reference back() { return *rbegin(); }
  const_reference back() const { return *rbegin(); }

  /// Insert a node at the front; never copies.
  void push_front(reference Node) { insert(begin(), Node); }

  /// Insert a node at the back; never copies.
  void push_back(reference Node) { insert(end(), Node); }

  /// Remove the node at the front; never deletes.
  void pop_front() { erase(begin()); }

  /// Remove the node at the back; never deletes.
  void pop_back() { erase(--end()); }

  /// Swap with another list in place using std::swap.
  void swap(simple_ilist &X) { std::swap(*this, X); }

  /// Insert a node by reference; never copies.
  iterator insert(iterator I, reference Node) {
    list_base_type::insertBefore(*I.getNodePtr(), *this->getNodePtr(&Node));
    return iterator(&Node);
  }

  /// Insert a range of nodes; never copies.
  template <class Iterator>
  void insert(iterator I, Iterator First, Iterator Last) {
    for (; First != Last; ++First)
      insert(I, *First);
  }

  /// Clone another list.
  template <class Cloner, class Disposer>
  void cloneFrom(const simple_ilist &L2, Cloner clone, Disposer dispose) {
    clearAndDispose(dispose);
    for (const_reference V : L2)
      push_back(*clone(V));
  }

  /// Remove a node by reference; never deletes.
  ///
  /// \see \a erase() for removing by iterator.
  /// \see \a removeAndDispose() if the node should be deleted.
  void remove(reference N) { list_base_type::remove(*this->getNodePtr(&N)); }

  /// Remove a node by reference and dispose of it.
  template <class Disposer>
  void removeAndDispose(reference N, Disposer dispose) {
    remove(N);
    dispose(&N);
  }

  /// Remove a node by iterator; never deletes.
  ///
  /// \see \a remove() for removing by reference.
  /// \see \a eraseAndDispose() it the node should be deleted.
  iterator erase(iterator I) {
    assert(I != end() && "Cannot remove end of list!");
    remove(*I++);
    return I;
  }

  /// Remove a range of nodes; never deletes.
  ///
  /// \see \a eraseAndDispose() if the nodes should be deleted.
  iterator erase(iterator First, iterator Last) {
    list_base_type::removeRange(*First.getNodePtr(), *Last.getNodePtr());
    return Last;
  }

  /// Remove a node by iterator and dispose of it.
  template <class Disposer>
  iterator eraseAndDispose(iterator I, Disposer dispose) {
    auto Next = std::next(I);
    erase(I);
    dispose(&*I);
    return Next;
  }

  /// Remove a range of nodes and dispose of them.
  template <class Disposer>
  iterator eraseAndDispose(iterator First, iterator Last, Disposer dispose) {
    while (First != Last)
      First = eraseAndDispose(First, dispose);
    return Last;
  }

  /// Clear the list; never deletes.
  ///
  /// \see \a clearAndDispose() if the nodes should be deleted.
  void clear() { Sentinel.reset(); }

  /// Clear the list and dispose of the nodes.
  template <class Disposer> void clearAndDispose(Disposer dispose) {
    eraseAndDispose(begin(), end(), dispose);
  }

  /// Splice in another list.
  void splice(iterator I, simple_ilist &L2) {
    splice(I, L2, L2.begin(), L2.end());
  }

  /// Splice in a node from another list.
  void splice(iterator I, simple_ilist &L2, iterator Node) {
    splice(I, L2, Node, std::next(Node));
  }

  /// Splice in a range of nodes from another list.
  void splice(iterator I, simple_ilist &, iterator First, iterator Last) {
    list_base_type::transferBefore(*I.getNodePtr(), *First.getNodePtr(),
                                   *Last.getNodePtr());
  }

  /// Merge in another list.
  ///
  /// \pre \c this and \p RHS are sorted.
  ///@{
  void merge(simple_ilist &RHS) { merge(RHS, std::less<T>()); }
  template <class Compare> void merge(simple_ilist &RHS, Compare comp);
  ///@}

  /// Sort the list.
  ///@{
  void sort() { sort(std::less<T>()); }
  template <class Compare> void sort(Compare comp);
  ///@}
};

template <class T, class... Options>
template <class Compare>
void simple_ilist<T, Options...>::merge(simple_ilist &RHS, Compare comp) {
  if (this == &RHS || RHS.empty())
    return;
  iterator LI = begin(), LE = end();
  iterator RI = RHS.begin(), RE = RHS.end();
  while (LI != LE) {
    if (comp(*RI, *LI)) {
      // Transfer a run of at least size 1 from RHS to LHS.
      iterator RunStart = RI++;
      RI = std::find_if(RI, RE, [&](reference RV) { return !comp(RV, *LI); });
      splice(LI, RHS, RunStart, RI);
      if (RI == RE)
        return;
    }
    ++LI;
  }
  // Transfer the remaining RHS nodes once LHS is finished.
  splice(LE, RHS, RI, RE);
}

template <class T, class... Options>
template <class Compare>
void simple_ilist<T, Options...>::sort(Compare comp) {
  // Vacuously sorted.
  if (empty() || std::next(begin()) == end())
    return;

  // Split the list in the middle.
  iterator Center = begin(), End = begin();
  while (End != end() && ++End != end()) {
    ++Center;
    ++End;
  }
  simple_ilist RHS;
  RHS.splice(RHS.end(), *this, Center, end());

  // Sort the sublists and merge back together.
  sort(comp);
  RHS.sort(comp);
  merge(RHS, comp);
}

} // end namespace llvm

#endif // LLVM_ADT_SIMPLE_ILIST_H
