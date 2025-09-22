//===--- fallible_iterator.h - Wrapper for fallible iterators ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_FALLIBLE_ITERATOR_H
#define LLVM_ADT_FALLIBLE_ITERATOR_H

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Error.h"

#include <type_traits>

namespace llvm {

/// A wrapper class for fallible iterators.
///
///   The fallible_iterator template wraps an underlying iterator-like class
/// whose increment and decrement operations are replaced with fallible versions
/// like:
///
///   @code{.cpp}
///   Error inc();
///   Error dec();
///   @endcode
///
///   It produces an interface that is (mostly) compatible with a traditional
/// c++ iterator, including ++ and -- operators that do not fail.
///
///   Instances of the wrapper are constructed with an instance of the
/// underlying iterator and (for non-end iterators) a reference to an Error
/// instance. If the underlying increment/decrement operations fail, the Error
/// is returned via this reference, and the resulting iterator value set to an
/// end-of-range sentinel value. This enables the following loop idiom:
///
///   @code{.cpp}
///   class Archive { // E.g. Potentially malformed on-disk archive
///   public:
///     fallible_iterator<ArchiveChildItr> children_begin(Error &Err);
///     fallible_iterator<ArchiveChildItr> children_end();
///     iterator_range<fallible_iterator<ArchiveChildItr>>
///     children(Error &Err) {
///       return make_range(children_begin(Err), children_end());
///     //...
///   };
///
///   void walk(Archive &A) {
///     Error Err = Error::success();
///     for (auto &C : A.children(Err)) {
///       // Loop body only entered when increment succeeds.
///     }
///     if (Err) {
///       // handle error.
///     }
///   }
///   @endcode
///
///   The wrapper marks the referenced Error as unchecked after each increment
/// and/or decrement operation, and clears the unchecked flag when a non-end
/// value is compared against end (since, by the increment invariant, not being
/// an end value proves that there was no error, and is equivalent to checking
/// that the Error is success). This allows early exits from the loop body
/// without requiring redundant error checks.
template <typename Underlying> class fallible_iterator {
private:
  template <typename T>
  using enable_if_struct_deref_supported = std::enable_if_t<
      !std::is_void<decltype(std::declval<T>().operator->())>::value,
      decltype(std::declval<T>().operator->())>;

public:
  /// Construct a fallible iterator that *cannot* be used as an end-of-range
  /// value.
  ///
  /// A value created by this method can be dereferenced, incremented,
  /// decremented and compared, providing the underlying type supports it.
  ///
  /// The error that is passed in will be initially marked as checked, so if the
  /// iterator is not used at all the Error need not be checked.
  static fallible_iterator itr(Underlying I, Error &Err) {
    (void)!!Err;
    return fallible_iterator(std::move(I), &Err);
  }

  /// Construct a fallible iterator that can be used as an end-of-range value.
  ///
  /// A value created by this method can be dereferenced (if the underlying
  /// value points at a valid value) and compared, but not incremented or
  /// decremented.
  static fallible_iterator end(Underlying I) {
    return fallible_iterator(std::move(I), nullptr);
  }

  /// Forward dereference to the underlying iterator.
  decltype(auto) operator*() { return *I; }

  /// Forward const dereference to the underlying iterator.
  decltype(auto) operator*() const { return *I; }

  /// Forward structure dereference to the underlying iterator (if the
  /// underlying iterator supports it).
  template <typename T = Underlying>
  enable_if_struct_deref_supported<T> operator->() {
    return I.operator->();
  }

  /// Forward const structure dereference to the underlying iterator (if the
  /// underlying iterator supports it).
  template <typename T = Underlying>
  enable_if_struct_deref_supported<const T> operator->() const {
    return I.operator->();
  }

  /// Increment the fallible iterator.
  ///
  /// If the underlying 'inc' operation fails, this will set the Error value
  /// and update this iterator value to point to end-of-range.
  ///
  /// The Error value is marked as needing checking, regardless of whether the
  /// 'inc' operation succeeds or fails.
  fallible_iterator &operator++() {
    assert(getErrPtr() && "Cannot increment end iterator");
    if (auto Err = I.inc())
      handleError(std::move(Err));
    else
      resetCheckedFlag();
    return *this;
  }

  /// Decrement the fallible iterator.
  ///
  /// If the underlying 'dec' operation fails, this will set the Error value
  /// and update this iterator value to point to end-of-range.
  ///
  /// The Error value is marked as needing checking, regardless of whether the
  /// 'dec' operation succeeds or fails.
  fallible_iterator &operator--() {
    assert(getErrPtr() && "Cannot decrement end iterator");
    if (auto Err = I.dec())
      handleError(std::move(Err));
    else
      resetCheckedFlag();
    return *this;
  }

  /// Compare fallible iterators for equality.
  ///
  /// Returns true if both LHS and RHS are end-of-range values, or if both are
  /// non-end-of-range values whose underlying iterator values compare equal.
  ///
  /// If this is a comparison between an end-of-range iterator and a
  /// non-end-of-range iterator, then the Error (referenced by the
  /// non-end-of-range value) is marked as checked: Since all
  /// increment/decrement operations result in an end-of-range value, comparing
  /// false against end-of-range is equivalent to checking that the Error value
  /// is success. This flag management enables early returns from loop bodies
  /// without redundant Error checks.
  friend bool operator==(const fallible_iterator &LHS,
                         const fallible_iterator &RHS) {
    // If both iterators are in the end state they compare
    // equal, regardless of whether either is valid.
    if (LHS.isEnd() && RHS.isEnd())
      return true;

    assert(LHS.isValid() && RHS.isValid() &&
           "Invalid iterators can only be compared against end");

    bool Equal = LHS.I == RHS.I;

    // If the iterators differ and this is a comparison against end then mark
    // the Error as checked.
    if (!Equal) {
      if (LHS.isEnd())
        (void)!!*RHS.getErrPtr();
      else
        (void)!!*LHS.getErrPtr();
    }

    return Equal;
  }

  /// Compare fallible iterators for inequality.
  ///
  /// See notes for operator==.
  friend bool operator!=(const fallible_iterator &LHS,
                         const fallible_iterator &RHS) {
    return !(LHS == RHS);
  }

private:
  fallible_iterator(Underlying I, Error *Err)
      : I(std::move(I)), ErrState(Err, false) {}

  Error *getErrPtr() const { return ErrState.getPointer(); }

  bool isEnd() const { return getErrPtr() == nullptr; }

  bool isValid() const { return !ErrState.getInt(); }

  void handleError(Error Err) {
    *getErrPtr() = std::move(Err);
    ErrState.setPointer(nullptr);
    ErrState.setInt(true);
  }

  void resetCheckedFlag() {
    *getErrPtr() = Error::success();
  }

  Underlying I;
  mutable PointerIntPair<Error *, 1> ErrState;
};

/// Convenience wrapper to make a fallible_iterator value from an instance
/// of an underlying iterator and an Error reference.
template <typename Underlying>
fallible_iterator<Underlying> make_fallible_itr(Underlying I, Error &Err) {
  return fallible_iterator<Underlying>::itr(std::move(I), Err);
}

/// Convenience wrapper to make a fallible_iterator end value from an instance
/// of an underlying iterator.
template <typename Underlying>
fallible_iterator<Underlying> make_fallible_end(Underlying E) {
  return fallible_iterator<Underlying>::end(std::move(E));
}

template <typename Underlying>
iterator_range<fallible_iterator<Underlying>>
make_fallible_range(Underlying I, Underlying E, Error &Err) {
  return make_range(make_fallible_itr(std::move(I), Err),
                    make_fallible_end(std::move(E)));
}

} // end namespace llvm

#endif // LLVM_ADT_FALLIBLE_ITERATOR_H
