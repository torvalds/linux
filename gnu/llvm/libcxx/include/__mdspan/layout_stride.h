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

#ifndef _LIBCPP___MDSPAN_LAYOUT_STRIDE_H
#define _LIBCPP___MDSPAN_LAYOUT_STRIDE_H

#include <__assert>
#include <__config>
#include <__fwd/mdspan.h>
#include <__mdspan/extents.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__utility/as_const.h>
#include <__utility/integer_sequence.h>
#include <__utility/swap.h>
#include <array>
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

namespace __mdspan_detail {
template <class _Layout, class _Mapping>
constexpr bool __is_mapping_of =
    is_same_v<typename _Layout::template mapping<typename _Mapping::extents_type>, _Mapping>;

template <class _Mapping>
concept __layout_mapping_alike = requires {
  requires __is_mapping_of<typename _Mapping::layout_type, _Mapping>;
  requires __is_extents_v<typename _Mapping::extents_type>;
  { _Mapping::is_always_strided() } -> same_as<bool>;
  { _Mapping::is_always_exhaustive() } -> same_as<bool>;
  { _Mapping::is_always_unique() } -> same_as<bool>;
  bool_constant<_Mapping::is_always_strided()>::value;
  bool_constant<_Mapping::is_always_exhaustive()>::value;
  bool_constant<_Mapping::is_always_unique()>::value;
};
} // namespace __mdspan_detail

template <class _Extents>
class layout_stride::mapping {
public:
  static_assert(__mdspan_detail::__is_extents<_Extents>::value,
                "layout_stride::mapping template argument must be a specialization of extents.");

  using extents_type = _Extents;
  using index_type   = typename extents_type::index_type;
  using size_type    = typename extents_type::size_type;
  using rank_type    = typename extents_type::rank_type;
  using layout_type  = layout_stride;

private:
  static constexpr rank_type __rank_ = extents_type::rank();

  // Used for default construction check and mandates
  _LIBCPP_HIDE_FROM_ABI static constexpr bool __required_span_size_is_representable(const extents_type& __ext) {
    if constexpr (__rank_ == 0)
      return true;

    index_type __prod = __ext.extent(0);
    for (rank_type __r = 1; __r < __rank_; __r++) {
      bool __overflowed = __builtin_mul_overflow(__prod, __ext.extent(__r), &__prod);
      if (__overflowed)
        return false;
    }
    return true;
  }

  template <class _OtherIndexType>
  _LIBCPP_HIDE_FROM_ABI static constexpr bool
  __required_span_size_is_representable(const extents_type& __ext, span<_OtherIndexType, __rank_> __strides) {
    if constexpr (__rank_ == 0)
      return true;

    index_type __size = 1;
    for (rank_type __r = 0; __r < __rank_; __r++) {
      // We can only check correct conversion of _OtherIndexType if it is an integral
      if constexpr (is_integral_v<_OtherIndexType>) {
        using _CommonType = common_type_t<index_type, _OtherIndexType>;
        if (static_cast<_CommonType>(__strides[__r]) > static_cast<_CommonType>(numeric_limits<index_type>::max()))
          return false;
      }
      if (__ext.extent(__r) == static_cast<index_type>(0))
        return true;
      index_type __prod     = (__ext.extent(__r) - 1);
      bool __overflowed_mul = __builtin_mul_overflow(__prod, static_cast<index_type>(__strides[__r]), &__prod);
      if (__overflowed_mul)
        return false;
      bool __overflowed_add = __builtin_add_overflow(__size, __prod, &__size);
      if (__overflowed_add)
        return false;
    }
    return true;
  }

  // compute offset of a strided layout mapping
  template <class _StridedMapping>
  _LIBCPP_HIDE_FROM_ABI static constexpr index_type __offset(const _StridedMapping& __mapping) {
    if constexpr (_StridedMapping::extents_type::rank() == 0) {
      return static_cast<index_type>(__mapping());
    } else if (__mapping.required_span_size() == static_cast<typename _StridedMapping::index_type>(0)) {
      return static_cast<index_type>(0);
    } else {
      return [&]<size_t... _Pos>(index_sequence<_Pos...>) {
        return static_cast<index_type>(__mapping((_Pos ? 0 : 0)...));
      }(make_index_sequence<__rank_>());
    }
  }

  // compute the permutation for sorting the stride array
  // we never actually sort the stride array
  _LIBCPP_HIDE_FROM_ABI constexpr void __bubble_sort_by_strides(array<rank_type, __rank_>& __permute) const {
    for (rank_type __i = __rank_ - 1; __i > 0; __i--) {
      for (rank_type __r = 0; __r < __i; __r++) {
        if (__strides_[__permute[__r]] > __strides_[__permute[__r + 1]]) {
          swap(__permute[__r], __permute[__r + 1]);
        } else {
          // if two strides are the same then one of the associated extents must be 1 or 0
          // both could be, but you can't have one larger than 1 come first
          if ((__strides_[__permute[__r]] == __strides_[__permute[__r + 1]]) &&
              (__extents_.extent(__permute[__r]) > static_cast<index_type>(1)))
            swap(__permute[__r], __permute[__r + 1]);
        }
      }
    }
  }

  static_assert(extents_type::rank_dynamic() > 0 || __required_span_size_is_representable(extents_type()),
                "layout_stride::mapping product of static extents must be representable as index_type.");

public:
  // [mdspan.layout.stride.cons], constructors
  _LIBCPP_HIDE_FROM_ABI constexpr mapping() noexcept : __extents_(extents_type()) {
    // Note the nominal precondition is covered by above static assert since
    // if rank_dynamic is != 0 required_span_size is zero for default construction
    if constexpr (__rank_ > 0) {
      index_type __stride = 1;
      for (rank_type __r = __rank_ - 1; __r > static_cast<rank_type>(0); __r--) {
        __strides_[__r] = __stride;
        __stride *= __extents_.extent(__r);
      }
      __strides_[0] = __stride;
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr mapping(const mapping&) noexcept = default;

  template <class _OtherIndexType>
    requires(is_convertible_v<const _OtherIndexType&, index_type> &&
             is_nothrow_constructible_v<index_type, const _OtherIndexType&>)
  _LIBCPP_HIDE_FROM_ABI constexpr mapping(const extents_type& __ext, span<_OtherIndexType, __rank_> __strides) noexcept
      : __extents_(__ext), __strides_([&]<size_t... _Pos>(index_sequence<_Pos...>) {
          return __mdspan_detail::__possibly_empty_array<index_type, __rank_>{
              static_cast<index_type>(std::as_const(__strides[_Pos]))...};
        }(make_index_sequence<__rank_>())) {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        ([&]<size_t... _Pos>(index_sequence<_Pos...>) {
          // For integrals we can do a pre-conversion check, for other types not
          if constexpr (is_integral_v<_OtherIndexType>) {
            return ((__strides[_Pos] > static_cast<_OtherIndexType>(0)) && ... && true);
          } else {
            return ((static_cast<index_type>(__strides[_Pos]) > static_cast<index_type>(0)) && ... && true);
          }
        }(make_index_sequence<__rank_>())),
        "layout_stride::mapping ctor: all strides must be greater than 0");
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __required_span_size_is_representable(__ext, __strides),
        "layout_stride::mapping ctor: required span size is not representable as index_type.");
    if constexpr (__rank_ > 1) {
      _LIBCPP_ASSERT_UNCATEGORIZED(
          ([&]<size_t... _Pos>(index_sequence<_Pos...>) {
            // basically sort the dimensions based on strides and extents, sorting is represented in permute array
            array<rank_type, __rank_> __permute{_Pos...};
            __bubble_sort_by_strides(__permute);

            // check that this permutations represents a growing set
            for (rank_type __i = 1; __i < __rank_; __i++)
              if (static_cast<index_type>(__strides[__permute[__i]]) <
                  static_cast<index_type>(__strides[__permute[__i - 1]]) * __extents_.extent(__permute[__i - 1]))
                return false;
            return true;
          }(make_index_sequence<__rank_>())),
          "layout_stride::mapping ctor: the provided extents and strides lead to a non-unique mapping");
    }
  }

  template <class _OtherIndexType>
    requires(is_convertible_v<const _OtherIndexType&, index_type> &&
             is_nothrow_constructible_v<index_type, const _OtherIndexType&>)
  _LIBCPP_HIDE_FROM_ABI constexpr mapping(const extents_type& __ext,
                                          const array<_OtherIndexType, __rank_>& __strides) noexcept
      : mapping(__ext, span(__strides)) {}

  template <class _StridedLayoutMapping>
    requires(__mdspan_detail::__layout_mapping_alike<_StridedLayoutMapping> &&
             is_constructible_v<extents_type, typename _StridedLayoutMapping::extents_type> &&
             _StridedLayoutMapping::is_always_unique() && _StridedLayoutMapping::is_always_strided())
  _LIBCPP_HIDE_FROM_ABI constexpr explicit(
      !(is_convertible_v<typename _StridedLayoutMapping::extents_type, extents_type> &&
        (__mdspan_detail::__is_mapping_of<layout_left, _StridedLayoutMapping> ||
         __mdspan_detail::__is_mapping_of<layout_right, _StridedLayoutMapping> ||
         __mdspan_detail::__is_mapping_of<layout_stride, _StridedLayoutMapping>)))
      mapping(const _StridedLayoutMapping& __other) noexcept
      : __extents_(__other.extents()), __strides_([&]<size_t... _Pos>(index_sequence<_Pos...>) {
          // stride() only compiles for rank > 0
          if constexpr (__rank_ > 0) {
            return __mdspan_detail::__possibly_empty_array<index_type, __rank_>{
                static_cast<index_type>(__other.stride(_Pos))...};
          } else {
            return __mdspan_detail::__possibly_empty_array<index_type, 0>{};
          }
        }(make_index_sequence<__rank_>())) {
    // stride() only compiles for rank > 0
    if constexpr (__rank_ > 0) {
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          ([&]<size_t... _Pos>(index_sequence<_Pos...>) {
            return ((static_cast<index_type>(__other.stride(_Pos)) > static_cast<index_type>(0)) && ... && true);
          }(make_index_sequence<__rank_>())),
          "layout_stride::mapping converting ctor: all strides must be greater than 0");
    }
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __mdspan_detail::__is_representable_as<index_type>(__other.required_span_size()),
        "layout_stride::mapping converting ctor: other.required_span_size() must be representable as index_type.");
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(static_cast<index_type>(0) == __offset(__other),
                                        "layout_stride::mapping converting ctor: base offset of mapping must be zero.");
  }

  _LIBCPP_HIDE_FROM_ABI constexpr mapping& operator=(const mapping&) noexcept = default;

  // [mdspan.layout.stride.obs], observers
  _LIBCPP_HIDE_FROM_ABI constexpr const extents_type& extents() const noexcept { return __extents_; }

  _LIBCPP_HIDE_FROM_ABI constexpr array<index_type, __rank_> strides() const noexcept {
    return [&]<size_t... _Pos>(index_sequence<_Pos...>) {
      return array<index_type, __rank_>{__strides_[_Pos]...};
    }(make_index_sequence<__rank_>());
  }

  _LIBCPP_HIDE_FROM_ABI constexpr index_type required_span_size() const noexcept {
    if constexpr (__rank_ == 0) {
      return static_cast<index_type>(1);
    } else {
      return [&]<size_t... _Pos>(index_sequence<_Pos...>) {
        if ((__extents_.extent(_Pos) * ... * 1) == 0)
          return static_cast<index_type>(0);
        else
          return static_cast<index_type>(
              static_cast<index_type>(1) +
              (((__extents_.extent(_Pos) - static_cast<index_type>(1)) * __strides_[_Pos]) + ... +
               static_cast<index_type>(0)));
      }(make_index_sequence<__rank_>());
    }
  }

  template <class... _Indices>
    requires((sizeof...(_Indices) == __rank_) && (is_convertible_v<_Indices, index_type> && ...) &&
             (is_nothrow_constructible_v<index_type, _Indices> && ...))
  _LIBCPP_HIDE_FROM_ABI constexpr index_type operator()(_Indices... __idx) const noexcept {
    // Mappings are generally meant to be used for accessing allocations and are meant to guarantee to never
    // return a value exceeding required_span_size(), which is used to know how large an allocation one needs
    // Thus, this is a canonical point in multi-dimensional data structures to make invalid element access checks
    // However, mdspan does check this on its own, so for now we avoid double checking in hardened mode
    _LIBCPP_ASSERT_UNCATEGORIZED(__mdspan_detail::__is_multidimensional_index_in(__extents_, __idx...),
                                 "layout_stride::mapping: out of bounds indexing");
    return [&]<size_t... _Pos>(index_sequence<_Pos...>) {
      return ((static_cast<index_type>(__idx) * __strides_[_Pos]) + ... + index_type(0));
    }(make_index_sequence<sizeof...(_Indices)>());
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_always_unique() noexcept { return true; }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_always_exhaustive() noexcept { return false; }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_always_strided() noexcept { return true; }

  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_unique() noexcept { return true; }
  // The answer of this function is fairly complex in the case where one or more
  // extents are zero.
  // Technically it is meaningless to query is_exhaustive() in that case, but unfortunately
  // the way the standard defines this function, we can't give a simple true or false then.
  _LIBCPP_HIDE_FROM_ABI constexpr bool is_exhaustive() const noexcept {
    if constexpr (__rank_ == 0)
      return true;
    else {
      index_type __span_size = required_span_size();
      if (__span_size == static_cast<index_type>(0)) {
        if constexpr (__rank_ == 1)
          return __strides_[0] == 1;
        else {
          rank_type __r_largest = 0;
          for (rank_type __r = 1; __r < __rank_; __r++)
            if (__strides_[__r] > __strides_[__r_largest])
              __r_largest = __r;
          for (rank_type __r = 0; __r < __rank_; __r++)
            if (__extents_.extent(__r) == 0 && __r != __r_largest)
              return false;
          return true;
        }
      } else {
        return required_span_size() == [&]<size_t... _Pos>(index_sequence<_Pos...>) {
          return (__extents_.extent(_Pos) * ... * static_cast<index_type>(1));
        }(make_index_sequence<__rank_>());
      }
    }
  }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool is_strided() noexcept { return true; }

  // according to the standard layout_stride does not have a constraint on stride(r) for rank>0
  // it still has the precondition though
  _LIBCPP_HIDE_FROM_ABI constexpr index_type stride(rank_type __r) const noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__r < __rank_, "layout_stride::mapping::stride(): invalid rank index");
    return __strides_[__r];
  }

  template <class _OtherMapping>
    requires(__mdspan_detail::__layout_mapping_alike<_OtherMapping> &&
             (_OtherMapping::extents_type::rank() == __rank_) && _OtherMapping::is_always_strided())
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const mapping& __lhs, const _OtherMapping& __rhs) noexcept {
    if (__offset(__rhs))
      return false;
    if constexpr (__rank_ == 0)
      return true;
    else {
      return __lhs.extents() == __rhs.extents() && [&]<size_t... _Pos>(index_sequence<_Pos...>) {
        // avoid warning when comparing signed and unsigner integers and pick the wider of two types
        using _CommonType = common_type_t<index_type, typename _OtherMapping::index_type>;
        return ((static_cast<_CommonType>(__lhs.stride(_Pos)) == static_cast<_CommonType>(__rhs.stride(_Pos))) && ... &&
                true);
      }(make_index_sequence<__rank_>());
    }
  }

private:
  _LIBCPP_NO_UNIQUE_ADDRESS extents_type __extents_{};
  _LIBCPP_NO_UNIQUE_ADDRESS __mdspan_detail::__possibly_empty_array<index_type, __rank_> __strides_{};
};

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MDSPAN_LAYOUT_STRIDE_H
