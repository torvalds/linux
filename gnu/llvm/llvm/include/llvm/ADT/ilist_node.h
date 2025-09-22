//===- llvm/ADT/ilist_node.h - Intrusive Linked List Helper -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the ilist_node class template, which is a convenient
/// base class for creating classes that can be used with ilists.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_NODE_H
#define LLVM_ADT_ILIST_NODE_H

#include "llvm/ADT/ilist_node_base.h"
#include "llvm/ADT/ilist_node_options.h"

namespace llvm {

namespace ilist_detail {

struct NodeAccess;

/// Mixin base class that is used to add \a getParent() and
/// \a setParent(ParentTy*) methods to \a ilist_node_impl iff \a ilist_parent
/// has been set in the list options.
template <class NodeTy, class ParentTy> class node_parent_access {
public:
  inline const ParentTy *getParent() const {
    return static_cast<const NodeTy *>(this)->getNodeBaseParent();
  }
  inline ParentTy *getParent() {
    return static_cast<NodeTy *>(this)->getNodeBaseParent();
  }
  void setParent(ParentTy *Parent) {
    return static_cast<NodeTy *>(this)->setNodeBaseParent(Parent);
  }
};
template <class NodeTy> class node_parent_access<NodeTy, void> {};

} // end namespace ilist_detail

template <class OptionsT, bool IsReverse, bool IsConst> class ilist_iterator;
template <class OptionsT, bool IsReverse, bool IsConst>
class ilist_iterator_w_bits;
template <class OptionsT> class ilist_sentinel;

// Selector for which iterator type to pick given the iterator-bits node option.
template <bool use_iterator_bits, typename Opts, bool arg1, bool arg2>
class ilist_select_iterator_type {
public:
  using type = ilist_iterator<Opts, arg1, arg2>;
};
template <typename Opts, bool arg1, bool arg2>
class ilist_select_iterator_type<true, Opts, arg1, arg2> {
public:
  using type = ilist_iterator_w_bits<Opts, arg1, arg2>;
};

/// Implementation for an ilist node.
///
/// Templated on an appropriate \a ilist_detail::node_options, usually computed
/// by \a ilist_detail::compute_node_options.
///
/// This is a wrapper around \a ilist_node_base whose main purpose is to
/// provide type safety: you can't insert nodes of \a ilist_node_impl into the
/// wrong \a simple_ilist or \a iplist.
template <class OptionsT>
class ilist_node_impl
    : OptionsT::node_base_type,
      public ilist_detail::node_parent_access<ilist_node_impl<OptionsT>,
                                              typename OptionsT::parent_ty> {
  using value_type = typename OptionsT::value_type;
  using node_base_type = typename OptionsT::node_base_type;
  using list_base_type = typename OptionsT::list_base_type;

  friend typename OptionsT::list_base_type;
  friend struct ilist_detail::NodeAccess;
  friend class ilist_sentinel<OptionsT>;

  friend class ilist_detail::node_parent_access<ilist_node_impl<OptionsT>,
                                                typename OptionsT::parent_ty>;
  friend class ilist_iterator<OptionsT, false, false>;
  friend class ilist_iterator<OptionsT, false, true>;
  friend class ilist_iterator<OptionsT, true, false>;
  friend class ilist_iterator<OptionsT, true, true>;
  friend class ilist_iterator_w_bits<OptionsT, false, false>;
  friend class ilist_iterator_w_bits<OptionsT, false, true>;
  friend class ilist_iterator_w_bits<OptionsT, true, false>;
  friend class ilist_iterator_w_bits<OptionsT, true, true>;

protected:
  using self_iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          false, false>::type;
  using const_self_iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          false, true>::type;
  using reverse_self_iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          true, false>::type;
  using const_reverse_self_iterator =
      typename ilist_select_iterator_type<OptionsT::has_iterator_bits, OptionsT,
                                          true, true>::type;

  ilist_node_impl() = default;

private:
  ilist_node_impl *getPrev() {
    return static_cast<ilist_node_impl *>(node_base_type::getPrev());
  }

  ilist_node_impl *getNext() {
    return static_cast<ilist_node_impl *>(node_base_type::getNext());
  }

  const ilist_node_impl *getPrev() const {
    return static_cast<ilist_node_impl *>(node_base_type::getPrev());
  }

  const ilist_node_impl *getNext() const {
    return static_cast<ilist_node_impl *>(node_base_type::getNext());
  }

  void setPrev(ilist_node_impl *N) { node_base_type::setPrev(N); }
  void setNext(ilist_node_impl *N) { node_base_type::setNext(N); }

public:
  self_iterator getIterator() { return self_iterator(*this); }
  const_self_iterator getIterator() const { return const_self_iterator(*this); }

  reverse_self_iterator getReverseIterator() {
    return reverse_self_iterator(*this);
  }

  const_reverse_self_iterator getReverseIterator() const {
    return const_reverse_self_iterator(*this);
  }

  // Under-approximation, but always available for assertions.
  using node_base_type::isKnownSentinel;

  /// Check whether this is the sentinel node.
  ///
  /// This requires sentinel tracking to be explicitly enabled.  Use the
  /// ilist_sentinel_tracking<true> option to get this API.
  bool isSentinel() const {
    static_assert(OptionsT::is_sentinel_tracking_explicit,
                  "Use ilist_sentinel_tracking<true> to enable isSentinel()");
    return node_base_type::isSentinel();
  }
};

/// An intrusive list node.
///
/// A base class to enable membership in intrusive lists, including \a
/// simple_ilist, \a iplist, and \a ilist.  The first template parameter is the
/// \a value_type for the list.
///
/// An ilist node can be configured with compile-time options to change
/// behaviour and/or add API.
///
/// By default, an \a ilist_node knows whether it is the list sentinel (an
/// instance of \a ilist_sentinel) if and only if
/// LLVM_ENABLE_ABI_BREAKING_CHECKS.  The function \a isKnownSentinel() always
/// returns \c false tracking is off.  Sentinel tracking steals a bit from the
/// "prev" link, which adds a mask operation when decrementing an iterator, but
/// enables bug-finding assertions in \a ilist_iterator.
///
/// To turn sentinel tracking on all the time, pass in the
/// ilist_sentinel_tracking<true> template parameter.  This also enables the \a
/// isSentinel() function.  The same option must be passed to the intrusive
/// list.  (ilist_sentinel_tracking<false> turns sentinel tracking off all the
/// time.)
///
/// A type can inherit from ilist_node multiple times by passing in different
/// \a ilist_tag options.  This allows a single instance to be inserted into
/// multiple lists simultaneously, where each list is given the same tag.
///
/// \example
/// struct A {};
/// struct B {};
/// struct N : ilist_node<N, ilist_tag<A>>, ilist_node<N, ilist_tag<B>> {};
///
/// void foo() {
///   simple_ilist<N, ilist_tag<A>> ListA;
///   simple_ilist<N, ilist_tag<B>> ListB;
///   N N1;
///   ListA.push_back(N1);
///   ListB.push_back(N1);
/// }
/// \endexample
///
/// When the \a ilist_parent<ParentTy> option is passed to an ilist_node and the
/// owning ilist, each node contains a pointer to the ilist's owner. This adds
/// \a getParent() and \a setParent(ParentTy*) methods to the ilist_node, which
/// will be used for node access by the ilist if the node class publicly
/// inherits from \a ilist_node_with_parent. By default, setParent() is not
/// automatically called by the ilist; a SymbolTableList will call setParent()
/// on inserted nodes, but the sentinel must still be manually set after the
/// list is created (e.g. SymTabList.end()->setParent(Parent)).
///
/// The primary benefit of using ilist_parent is that a parent
/// pointer will be stored in the sentinel, meaning that you can safely use \a
/// ilist_iterator::getNodeParent() to get the node parent from any valid (i.e.
/// non-null) iterator, even one that points to a sentinel value.
///
/// See \a is_valid_option for steps on adding a new option.
template <class T, class... Options>
class ilist_node
    : public ilist_node_impl<
          typename ilist_detail::compute_node_options<T, Options...>::type> {
  static_assert(ilist_detail::check_options<Options...>::value,
                "Unrecognized node option!");
};

namespace ilist_detail {

/// An access class for ilist_node private API.
///
/// This gives access to the private parts of ilist nodes.  Nodes for an ilist
/// should friend this class if they inherit privately from ilist_node.
///
/// Using this class outside of the ilist implementation is unsupported.
struct NodeAccess {
protected:
  template <class OptionsT>
  static ilist_node_impl<OptionsT> *getNodePtr(typename OptionsT::pointer N) {
    return N;
  }

  template <class OptionsT>
  static const ilist_node_impl<OptionsT> *
  getNodePtr(typename OptionsT::const_pointer N) {
    return N;
  }

  template <class OptionsT>
  static typename OptionsT::pointer getValuePtr(ilist_node_impl<OptionsT> *N) {
    return static_cast<typename OptionsT::pointer>(N);
  }

  template <class OptionsT>
  static typename OptionsT::const_pointer
  getValuePtr(const ilist_node_impl<OptionsT> *N) {
    return static_cast<typename OptionsT::const_pointer>(N);
  }

  template <class OptionsT>
  static ilist_node_impl<OptionsT> *getPrev(ilist_node_impl<OptionsT> &N) {
    return N.getPrev();
  }

  template <class OptionsT>
  static ilist_node_impl<OptionsT> *getNext(ilist_node_impl<OptionsT> &N) {
    return N.getNext();
  }

  template <class OptionsT>
  static const ilist_node_impl<OptionsT> *
  getPrev(const ilist_node_impl<OptionsT> &N) {
    return N.getPrev();
  }

  template <class OptionsT>
  static const ilist_node_impl<OptionsT> *
  getNext(const ilist_node_impl<OptionsT> &N) {
    return N.getNext();
  }
};

template <class OptionsT> struct SpecificNodeAccess : NodeAccess {
protected:
  using pointer = typename OptionsT::pointer;
  using const_pointer = typename OptionsT::const_pointer;
  using node_type = ilist_node_impl<OptionsT>;

  static node_type *getNodePtr(pointer N) {
    return NodeAccess::getNodePtr<OptionsT>(N);
  }

  static const node_type *getNodePtr(const_pointer N) {
    return NodeAccess::getNodePtr<OptionsT>(N);
  }

  static pointer getValuePtr(node_type *N) {
    return NodeAccess::getValuePtr<OptionsT>(N);
  }

  static const_pointer getValuePtr(const node_type *N) {
    return NodeAccess::getValuePtr<OptionsT>(N);
  }
};

} // end namespace ilist_detail

template <class OptionsT>
class ilist_sentinel : public ilist_node_impl<OptionsT> {
public:
  ilist_sentinel() {
    this->initializeSentinel();
    reset();
  }

  void reset() {
    this->setPrev(this);
    this->setNext(this);
  }

  bool empty() const { return this == this->getPrev(); }
};

/// An ilist node that can access its parent list.
///
/// Requires \c NodeTy to have \a getParent() to find the parent node, and the
/// \c ParentTy to have \a getSublistAccess() to get a reference to the list.
template <typename NodeTy, typename ParentTy, class... Options>
class ilist_node_with_parent : public ilist_node<NodeTy, Options...> {
protected:
  ilist_node_with_parent() = default;

private:
  /// Forward to NodeTy::getParent().
  ///
  /// Note: do not use the name "getParent()".  We want a compile error
  /// (instead of recursion) when the subclass fails to implement \a
  /// getParent().
  const ParentTy *getNodeParent() const {
    return static_cast<const NodeTy *>(this)->getParent();
  }

public:
  /// @name Adjacent Node Accessors
  /// @{
  /// Get the previous node, or \c nullptr for the list head.
  NodeTy *getPrevNode() {
    // Should be separated to a reused function, but then we couldn't use auto
    // (and would need the type of the list).
    const auto &List =
        getNodeParent()->*(ParentTy::getSublistAccess((NodeTy *)nullptr));
    return List.getPrevNode(*static_cast<NodeTy *>(this));
  }

  /// Get the previous node, or \c nullptr for the list head.
  const NodeTy *getPrevNode() const {
    return const_cast<ilist_node_with_parent *>(this)->getPrevNode();
  }

  /// Get the next node, or \c nullptr for the list tail.
  NodeTy *getNextNode() {
    // Should be separated to a reused function, but then we couldn't use auto
    // (and would need the type of the list).
    const auto &List =
        getNodeParent()->*(ParentTy::getSublistAccess((NodeTy *)nullptr));
    return List.getNextNode(*static_cast<NodeTy *>(this));
  }

  /// Get the next node, or \c nullptr for the list tail.
  const NodeTy *getNextNode() const {
    return const_cast<ilist_node_with_parent *>(this)->getNextNode();
  }
  /// @}
};

} // end namespace llvm

#endif // LLVM_ADT_ILIST_NODE_H
