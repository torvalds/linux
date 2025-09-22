//===- llvm/ADT/ADL.h - Argument dependent lookup utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ADL_H
#define LLVM_ADT_ADL_H

#include <type_traits>
#include <iterator>
#include <utility>

namespace llvm {

// Only used by compiler if both template types are the same.  Useful when
// using SFINAE to test for the existence of member functions.
template <typename T, T> struct SameType;

namespace adl_detail {

using std::begin;

template <typename RangeT>
constexpr auto begin_impl(RangeT &&range)
    -> decltype(begin(std::forward<RangeT>(range))) {
  return begin(std::forward<RangeT>(range));
}

using std::end;

template <typename RangeT>
constexpr auto end_impl(RangeT &&range)
    -> decltype(end(std::forward<RangeT>(range))) {
  return end(std::forward<RangeT>(range));
}

using std::rbegin;

template <typename RangeT>
constexpr auto rbegin_impl(RangeT &&range)
    -> decltype(rbegin(std::forward<RangeT>(range))) {
  return rbegin(std::forward<RangeT>(range));
}

using std::rend;

template <typename RangeT>
constexpr auto rend_impl(RangeT &&range)
    -> decltype(rend(std::forward<RangeT>(range))) {
  return rend(std::forward<RangeT>(range));
}

using std::swap;

template <typename T>
constexpr void swap_impl(T &&lhs,
                         T &&rhs) noexcept(noexcept(swap(std::declval<T>(),
                                                         std::declval<T>()))) {
  swap(std::forward<T>(lhs), std::forward<T>(rhs));
}

using std::size;

template <typename RangeT>
constexpr auto size_impl(RangeT &&range)
    -> decltype(size(std::forward<RangeT>(range))) {
  return size(std::forward<RangeT>(range));
}

} // end namespace adl_detail

/// Returns the begin iterator to \p range using `std::begin` and
/// function found through Argument-Dependent Lookup (ADL).
template <typename RangeT>
constexpr auto adl_begin(RangeT &&range)
    -> decltype(adl_detail::begin_impl(std::forward<RangeT>(range))) {
  return adl_detail::begin_impl(std::forward<RangeT>(range));
}

/// Returns the end iterator to \p range using `std::end` and
/// functions found through Argument-Dependent Lookup (ADL).
template <typename RangeT>
constexpr auto adl_end(RangeT &&range)
    -> decltype(adl_detail::end_impl(std::forward<RangeT>(range))) {
  return adl_detail::end_impl(std::forward<RangeT>(range));
}

/// Returns the reverse-begin iterator to \p range using `std::rbegin` and
/// function found through Argument-Dependent Lookup (ADL).
template <typename RangeT>
constexpr auto adl_rbegin(RangeT &&range)
    -> decltype(adl_detail::rbegin_impl(std::forward<RangeT>(range))) {
  return adl_detail::rbegin_impl(std::forward<RangeT>(range));
}

/// Returns the reverse-end iterator to \p range using `std::rend` and
/// functions found through Argument-Dependent Lookup (ADL).
template <typename RangeT>
constexpr auto adl_rend(RangeT &&range)
    -> decltype(adl_detail::rend_impl(std::forward<RangeT>(range))) {
  return adl_detail::rend_impl(std::forward<RangeT>(range));
}

/// Swaps \p lhs with \p rhs using `std::swap` and functions found through
/// Argument-Dependent Lookup (ADL).
template <typename T>
constexpr void adl_swap(T &&lhs, T &&rhs) noexcept(
    noexcept(adl_detail::swap_impl(std::declval<T>(), std::declval<T>()))) {
  adl_detail::swap_impl(std::forward<T>(lhs), std::forward<T>(rhs));
}

/// Returns the size of \p range using `std::size` and functions found through
/// Argument-Dependent Lookup (ADL).
template <typename RangeT>
constexpr auto adl_size(RangeT &&range)
    -> decltype(adl_detail::size_impl(std::forward<RangeT>(range))) {
  return adl_detail::size_impl(std::forward<RangeT>(range));
}

namespace detail {

template <typename RangeT>
using IterOfRange = decltype(adl_begin(std::declval<RangeT &>()));

template <typename RangeT>
using ValueOfRange =
    std::remove_reference_t<decltype(*adl_begin(std::declval<RangeT &>()))>;

} // namespace detail
} // namespace llvm

#endif // LLVM_ADT_ADL_H
