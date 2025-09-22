// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___MDSPAN_LAYOUT_RIGHT_H
#define _LIBCPP___MDSPAN_LAYOUT_RIGHT_H

#include <__assert>
#include <__config>
#include <__fwd/mdspan.h>
#include <__mdspan/extents.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__utility/integer_sequence.h>
#include <cinttypes>
#include <cstddef>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class _Extents>
class layout_right::mapping {
public:
  static_assert(__mdspan_detail::__is_extents<_Extents>::value,
                "layout_right::mapping template argument must be a specialization of extents.");

  using extents_type = _Extents;
  using index_type   = typename extents_type::index_type;
  using size_type    = typename extents_type::size_type;
  using rank_type    = typename extents_type::rank_type;
  using layout_type  = layout_right;

private:
  _LIBCPP_HIDE_FROM_ABI static constexpr bool __required_span_size_is_representable(const extents_type& __ext) {
    if constexpr (extents_type::rank() == 0)
      return true;

    index_type __prod = __ext.extent(0);
    for (rank_type __r = 1; __r < extents_type::rank(); __r++) {
      bool __overflowed = __builtin_mul_overflow(__prod, __ext.extent(__r), &__prod);
      if (__overflowed)
        return false;
    }
    return true;
  }

  static_assert(extents_type::rank_dynamic() > 0 || __required_span_size_is_representable(extents_type()),
                "layout_right::mapping product of static extents must be representable as index_type.");

public:
  // [mdspan.layout.right.cons], constructors
  _LIBCPP_HIDE_FROM_ABI constexpr mapping() noexcept               = default;
  _LIBCPP_HIDE_FROM_ABI constexpr mapping(const mapping&) noexcept = default;
  _LIBCPP_HIDE_FROM_ABI constexpr mapping(const extents_type& __ext) noexcept : __extents_(__ext) {
    // not catching this could lead to out-of-bounds access later when used inside mdspan
    // mapping<dextents<char, 2>> map(dextents<char, 2>(40,40)); map(3, 10) == -126
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __required_span_size_is_representable(__ext),
        "layout_right::mapping extents ctor: product of extents must be representable as index_type.");
  }

  template <class _OtherExtents>
    requires(is_constructible_v<extents_type, _OtherExtents>)
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<_OtherExtents, extents_type>)
      mapping(const mapping<_OtherExtents>& __other) noexcept
      : __extents_(__other.extents()) {
    // not catching this could lead to out-of-bounds access later when used inside mdspan
    // mapping<dextents<char, 2>> map(mapping<dextents<int, 2>>(dextents<int, 2>(40,40))); map(3, 10) == -126
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __mdspan_detail::__is_representable_as<index_type>(__other.required_span_size()),
        "layout_right::mapping converting ctor: other.required_span_size() must be representable as index_type.");
  }

  template <class _OtherExtents>
    requires(is_constructible_v<extents_type, _OtherExtents> && _OtherExtents::rank() <= 1)
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(!is_convertible_v<_OtherExtents, extents_type>)
      mapping(const layout_left::mapping<_OtherExtents>& __other) noexcept
      : __extents_(__other.extents()) {
    // not catching this could lead to out-of-bounds access later when used inside mdspan
    // Note: since this is constraint to rank 1, extents itself would catch the invalid conversion first
    //       and thus this assertion should never be triggered, but keeping it here for consistency
    // layout_right::mapping<dextents<char, 1>> map(
    //           layout_left::mapping<dextents<unsigned, 1>>(dextents<unsigned, 1>(200))); map.extents().extent(0) ==
    //           -56
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __mdspan_detail::__is_representable_as<index_type>(__other.required_span_size()),
        "layout_right::mapping converting ctor: other.required_span_size() must be representable as index_type.");
  }

  template <class _OtherExtents>
    requires(is_constructible_v<extents_type, _OtherExtents>)
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(extents_type::rank() > 0)
      mapping(const layout_stride::mapping<_OtherExtents>& __other) noexcept
      : __extents_(__other.extents()) {
    if constexpr (extents_type::rank() > 0) {
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          ([&]() {
            using _CommonType = common_type_t<typename extents_type::index_type, typename _OtherExtents::index_type>;
            for (rank_type __r = 0; __r < extents_type::rank(); __r++)
              if (static_cast<_CommonType>(stride(__r)) != static_cast<_CommonType>(__other.stride(__r)))
                return false;
            return true;
          }()),
          "layout_right::mapping from layout_stride ctor: strides are not compatible with layout_right.");
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __mdspan_detail::__is_representable_as<index_type>(__other.required_span_size()),
          "layout_right::mapping from layout_stride ctor: other.required_span_size() must be representable as "
          "index_type.");
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr mapping& operator=(const mapping&) noexcept = default;

  // [mdspan.layout.right.obs], observers
  _LIBCPP_HIDE_FROM_ABI constexpr const extents_type& extents() const noexcept { return __extents_; }

  _LIBCPP_HIDE_FROM_ABI constexpr index_type required_span_size() const noexcept {
    index_type __size = 1;
    for (size_t __r = 0; __r < extents_type::rank(); __r++)
      __size *= __extents_.extent(__r);
    return __size;
  }

  template <class... _Indices>
    requires((sizeof...(_Indices) == extents_type::rank()) && (is_convertible_v<_Indices, index_type> && ...) &&
             (is_nothrow_constructible_v<index_type, _Indices> && ...))
  _LIBCPP_HIDE_FROM_ABI constexpr index_type operator()(_Indices... __idx) const noexcept {
    // Mappings are generally meant to be used for accessing allocations and are meant to guarantee to never
    // return a value exceeding required_span_size(), which is used to know how large an allocation one needs
    // Thus, this is a canonical point in multi-dimensional data structures to make invalid element access checks
    // However, mdspan does check this on its own, so for now we avoid double checking in hardened mode
    _LIBCPP_ASSERT_UNCATEGORIZED(__mdspan_detail::__is_multidimensional_index_in(__extents_, __idx...),
                                 "layout_right::mapping: out of bounds indexing");
    return [&]<size_t... _Pos>(index_sequence<_Pos...>) {
      index_type __res = 0;
      ((__res = static_cast<index_type>(__idx) + __extents_.extent(_Pos) * __res), ...);
      return __res;
    }(make_index_sequence<sizeof...(_Indices)>());
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_always_unique() noexcept { return true; }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_always_exhaustive() noexcept { return true; }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_always_strided() noexcept { return true; }

  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_unique() noexcept { return true; }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_exhaustive() noexcept { return true; }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_strided() noexcept { return true; }

  _LIBCPP_HIDE_FROM_ABI constexpr index_type stride(rank_type __r) const noexcept
    requires(extents_type::rank() > 0)
  {
    // While it would be caught by extents itself too, using a too large __r
    // is functionally an out of bounds access on the stored information needed to compute strides
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __r < extents_type::rank(), "layout_right::mapping::stride(): invalid rank index");
    index_type __s = 1;
    for (rank_type __i = extents_type::rank() - 1; __i > __r; __i--)
      __s *= __extents_.extent(__i);
    return __s;
  }

  template <class _OtherExtents>
    requires(_OtherExtents::rank() == extents_type::rank())
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool
  operator==(const mapping& __lhs, const mapping<_OtherExtents>& __rhs) noexcept {
    return __lhs.extents() == __rhs.extents();
  }

private:
  _LIBCPP_NO_UNIQUE_ADDRESS extents_type __extents_{};
};

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MDSPAN_LAYOUT_RIGHT_H
