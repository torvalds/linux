//===- iterator.h - Utilities for using and defining iterators --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ITERATOR_H
#define LLVM_ADT_ITERATOR_H

#include "llvm/ADT/iterator_range.h"
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace llvm {

/// CRTP base class which implements the entire standard iterator facade
/// in terms of a minimal subset of the interface.
///
/// Use this when it is reasonable to implement most of the iterator
/// functionality in terms of a core subset. If you need special behavior or
/// there are performance implications for this, you may want to override the
/// relevant members instead.
///
/// Note, one abstraction that this does *not* provide is implementing
/// subtraction in terms of addition by negating the difference. Negation isn't
/// always information preserving, and I can see very reasonable iterator
/// designs where this doesn't work well. It doesn't really force much added
/// boilerplate anyways.
///
/// Another abstraction that this doesn't provide is implementing increment in
/// terms of addition of one. These aren't equivalent for all iterator
/// categories, and respecting that adds a lot of complexity for little gain.
///
/// Iterators are expected to have const rules analogous to pointers, with a
/// single, const-qualified operator*() that returns ReferenceT. This matches
/// the second and third pointers in the following example:
/// \code
///   int Value;
///   { int *I = &Value; }             // ReferenceT 'int&'
///   { int *const I = &Value; }       // ReferenceT 'int&'; const
///   { const int *I = &Value; }       // ReferenceT 'const int&'
///   { const int *const I = &Value; } // ReferenceT 'const int&'; const
/// \endcode
/// If an iterator facade returns a handle to its own state, then T (and
/// PointerT and ReferenceT) should usually be const-qualified. Otherwise, if
/// clients are expected to modify the handle itself, the field can be declared
/// mutable or use const_cast.
///
/// Classes wishing to use `iterator_facade_base` should implement the following
/// methods:
///
/// Forward Iterators:
///   (All of the following methods)
///   - DerivedT &operator=(const DerivedT &R);
///   - bool operator==(const DerivedT &R) const;
///   - T &operator*() const;
///   - DerivedT &operator++();
///
/// Bidirectional Iterators:
///   (All methods of forward iterators, plus the following)
///   - DerivedT &operator--();
///
/// Random-access Iterators:
///   (All methods of bidirectional iterators excluding the following)
///   - DerivedT &operator++();
///   - DerivedT &operator--();
///   (and plus the following)
///   - bool operator<(const DerivedT &RHS) const;
///   - DifferenceTypeT operator-(const DerivedT &R) const;
///   - DerivedT &operator+=(DifferenceTypeT N);
///   - DerivedT &operator-=(DifferenceTypeT N);
///
template <typename DerivedT, typename IteratorCategoryT, typename T,
          typename DifferenceTypeT = std::ptrdiff_t, typename PointerT = T *,
          typename ReferenceT = T &>
class iterator_facade_base {
public:
  using iterator_category = IteratorCategoryT;
  using value_type = T;
  using difference_type = DifferenceTypeT;
  using pointer = PointerT;
  using reference = ReferenceT;

protected:
  enum {
    IsRandomAccess = std::is_base_of<std::random_access_iterator_tag,
                                     IteratorCategoryT>::value,
    IsBidirectional = std::is_base_of<std::bidirectional_iterator_tag,
                                      IteratorCategoryT>::value,
  };

  /// A proxy object for computing a reference via indirecting a copy of an
  /// iterator. This is used in APIs which need to produce a reference via
  /// indirection but for which the iterator object might be a temporary. The
  /// proxy preserves the iterator internally and exposes the indirected
  /// reference via a conversion operator.
  class ReferenceProxy {
    friend iterator_facade_base;

    DerivedT I;

    ReferenceProxy(DerivedT I) : I(std::move(I)) {}

  public:
    operator ReferenceT() const { return *I; }
  };

  /// A proxy object for computing a pointer via indirecting a copy of a
  /// reference. This is used in APIs which need to produce a pointer but for
  /// which the reference might be a temporary. The proxy preserves the
  /// reference internally and exposes the pointer via a arrow operator.
  class PointerProxy {
    friend iterator_facade_base;

    ReferenceT R;

    template <typename RefT>
    PointerProxy(RefT &&R) : R(std::forward<RefT>(R)) {}

  public:
    PointerT operator->() const { return &R; }
  };

public:
  DerivedT operator+(DifferenceTypeT n) const {
    static_assert(std::is_base_of<iterator_facade_base, DerivedT>::value,
                  "Must pass the derived type to this template!");
    static_assert(
        IsRandomAccess,
        "The '+' operator is only defined for random access iterators.");
    DerivedT tmp = *static_cast<const DerivedT *>(this);
    tmp += n;
    return tmp;
  }
  friend DerivedT operator+(DifferenceTypeT n, const DerivedT &i) {
    static_assert(
        IsRandomAccess,
        "The '+' operator is only defined for random access iterators.");
    return i + n;
  }
  DerivedT operator-(DifferenceTypeT n) const {
    static_assert(
        IsRandomAccess,
        "The '-' operator is only defined for random access iterators.");
    DerivedT tmp = *static_cast<const DerivedT *>(this);
    tmp -= n;
    return tmp;
  }

  DerivedT &operator++() {
    static_assert(std::is_base_of<iterator_facade_base, DerivedT>::value,
                  "Must pass the derived type to this template!");
    return static_cast<DerivedT *>(this)->operator+=(1);
  }
  DerivedT operator++(int) {
    DerivedT tmp = *static_cast<DerivedT *>(this);
    ++*static_cast<DerivedT *>(this);
    return tmp;
  }
  DerivedT &operator--() {
    static_assert(
        IsBidirectional,
        "The decrement operator is only defined for bidirectional iterators.");
    return static_cast<DerivedT *>(this)->operator-=(1);
  }
  DerivedT operator--(int) {
    static_assert(
        IsBidirectional,
        "The decrement operator is only defined for bidirectional iterators.");
    DerivedT tmp = *static_cast<DerivedT *>(this);
    --*static_cast<DerivedT *>(this);
    return tmp;
  }

#ifndef __cpp_impl_three_way_comparison
  bool operator!=(const DerivedT &RHS) const {
    return !(static_cast<const DerivedT &>(*this) == RHS);
  }
#endif

  bool operator>(const DerivedT &RHS) const {
    static_assert(
        IsRandomAccess,
        "Relational operators are only defined for random access iterators.");
    return !(static_cast<const DerivedT &>(*this) < RHS) &&
           !(static_cast<const DerivedT &>(*this) == RHS);
  }
  bool operator<=(const DerivedT &RHS) const {
    static_assert(
        IsRandomAccess,
        "Relational operators are only defined for random access iterators.");
    return !(static_cast<const DerivedT &>(*this) > RHS);
  }
  bool operator>=(const DerivedT &RHS) const {
    static_assert(
        IsRandomAccess,
        "Relational operators are only defined for random access iterators.");
    return !(static_cast<const DerivedT &>(*this) < RHS);
  }

  PointerProxy operator->() const {
    return static_cast<const DerivedT *>(this)->operator*();
  }
  ReferenceProxy operator[](DifferenceTypeT n) const {
    static_assert(IsRandomAccess,
                  "Subscripting is only defined for random access iterators.");
    return static_cast<const DerivedT *>(this)->operator+(n);
  }
};

/// CRTP base class for adapting an iterator to a different type.
///
/// This class can be used through CRTP to adapt one iterator into another.
/// Typically this is done through providing in the derived class a custom \c
/// operator* implementation. Other methods can be overridden as well.
template <
    typename DerivedT, typename WrappedIteratorT,
    typename IteratorCategoryT =
        typename std::iterator_traits<WrappedIteratorT>::iterator_category,
    typename T = typename std::iterator_traits<WrappedIteratorT>::value_type,
    typename DifferenceTypeT =
        typename std::iterator_traits<WrappedIteratorT>::difference_type,
    typename PointerT = std::conditional_t<
        std::is_same<T, typename std::iterator_traits<
                            WrappedIteratorT>::value_type>::value,
        typename std::iterator_traits<WrappedIteratorT>::pointer, T *>,
    typename ReferenceT = std::conditional_t<
        std::is_same<T, typename std::iterator_traits<
                            WrappedIteratorT>::value_type>::value,
        typename std::iterator_traits<WrappedIteratorT>::reference, T &>>
class iterator_adaptor_base
    : public iterator_facade_base<DerivedT, IteratorCategoryT, T,
                                  DifferenceTypeT, PointerT, ReferenceT> {
  using BaseT = typename iterator_adaptor_base::iterator_facade_base;

protected:
  WrappedIteratorT I;

  iterator_adaptor_base() = default;

  explicit iterator_adaptor_base(WrappedIteratorT u) : I(std::move(u)) {
    static_assert(std::is_base_of<iterator_adaptor_base, DerivedT>::value,
                  "Must pass the derived type to this template!");
  }

  const WrappedIteratorT &wrapped() const { return I; }

public:
  using difference_type = DifferenceTypeT;

  DerivedT &operator+=(difference_type n) {
    static_assert(
        BaseT::IsRandomAccess,
        "The '+=' operator is only defined for random access iterators.");
    I += n;
    return *static_cast<DerivedT *>(this);
  }
  DerivedT &operator-=(difference_type n) {
    static_assert(
        BaseT::IsRandomAccess,
        "The '-=' operator is only defined for random access iterators.");
    I -= n;
    return *static_cast<DerivedT *>(this);
  }
  using BaseT::operator-;
  difference_type operator-(const DerivedT &RHS) const {
    static_assert(
        BaseT::IsRandomAccess,
        "The '-' operator is only defined for random access iterators.");
    return I - RHS.I;
  }

  // We have to explicitly provide ++ and -- rather than letting the facade
  // forward to += because WrappedIteratorT might not support +=.
  using BaseT::operator++;
  DerivedT &operator++() {
    ++I;
    return *static_cast<DerivedT *>(this);
  }
  using BaseT::operator--;
  DerivedT &operator--() {
    static_assert(
        BaseT::IsBidirectional,
        "The decrement operator is only defined for bidirectional iterators.");
    --I;
    return *static_cast<DerivedT *>(this);
  }

  friend bool operator==(const iterator_adaptor_base &LHS,
                         const iterator_adaptor_base &RHS) {
    return LHS.I == RHS.I;
  }
  friend bool operator<(const iterator_adaptor_base &LHS,
                        const iterator_adaptor_base &RHS) {
    static_assert(
        BaseT::IsRandomAccess,
        "Relational operators are only defined for random access iterators.");
    return LHS.I < RHS.I;
  }

  ReferenceT operator*() const { return *I; }
};

/// An iterator type that allows iterating over the pointees via some
/// other iterator.
///
/// The typical usage of this is to expose a type that iterates over Ts, but
/// which is implemented with some iterator over T*s:
///
/// \code
///   using iterator = pointee_iterator<SmallVectorImpl<T *>::iterator>;
/// \endcode
template <typename WrappedIteratorT,
          typename T = std::remove_reference_t<decltype(
              **std::declval<WrappedIteratorT>())>>
struct pointee_iterator
    : iterator_adaptor_base<
          pointee_iterator<WrappedIteratorT, T>, WrappedIteratorT,
          typename std::iterator_traits<WrappedIteratorT>::iterator_category,
          T> {
  pointee_iterator() = default;
  template <typename U>
  pointee_iterator(U &&u)
      : pointee_iterator::iterator_adaptor_base(std::forward<U &&>(u)) {}

  T &operator*() const { return **this->I; }
};

template <typename RangeT, typename WrappedIteratorT =
                               decltype(std::begin(std::declval<RangeT>()))>
iterator_range<pointee_iterator<WrappedIteratorT>>
make_pointee_range(RangeT &&Range) {
  using PointeeIteratorT = pointee_iterator<WrappedIteratorT>;
  return make_range(PointeeIteratorT(std::begin(std::forward<RangeT>(Range))),
                    PointeeIteratorT(std::end(std::forward<RangeT>(Range))));
}

template <typename WrappedIteratorT,
          typename T = decltype(&*std::declval<WrappedIteratorT>())>
class pointer_iterator
    : public iterator_adaptor_base<
          pointer_iterator<WrappedIteratorT, T>, WrappedIteratorT,
          typename std::iterator_traits<WrappedIteratorT>::iterator_category,
          T> {
  mutable T Ptr;

public:
  pointer_iterator() = default;

  explicit pointer_iterator(WrappedIteratorT u)
      : pointer_iterator::iterator_adaptor_base(std::move(u)) {}

  T &operator*() const { return Ptr = &*this->I; }
};

template <typename RangeT, typename WrappedIteratorT =
                               decltype(std::begin(std::declval<RangeT>()))>
iterator_range<pointer_iterator<WrappedIteratorT>>
make_pointer_range(RangeT &&Range) {
  using PointerIteratorT = pointer_iterator<WrappedIteratorT>;
  return make_range(PointerIteratorT(std::begin(std::forward<RangeT>(Range))),
                    PointerIteratorT(std::end(std::forward<RangeT>(Range))));
}

template <typename WrappedIteratorT,
          typename T1 = std::remove_reference_t<decltype(
              **std::declval<WrappedIteratorT>())>,
          typename T2 = std::add_pointer_t<T1>>
using raw_pointer_iterator =
    pointer_iterator<pointee_iterator<WrappedIteratorT, T1>, T2>;

} // end namespace llvm

#endif // LLVM_ADT_ITERATOR_H
