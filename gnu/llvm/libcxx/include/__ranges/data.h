// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_DATA_H
#define _LIBCPP___RANGES_DATA_H

#include <__concepts/class_or_enum.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__memory/pointer_traits.h>
#include <__ranges/access.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_pointer.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/remove_pointer.h>
#include <__type_traits/remove_reference.h>
#include <__utility/auto_cast.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [range.prim.data]

namespace ranges {
namespace __data {
template <class _Tp>
concept __ptr_to_object = is_pointer_v<_Tp> && is_object_v<remove_pointer_t<_Tp>>;

template <class _Tp>
concept __member_data = __can_borrow<_Tp> && requires(_Tp&& __t) {
  { _LIBCPP_AUTO_CAST(__t.data()) } -> __ptr_to_object;
};

template <class _Tp>
concept __ranges_begin_invocable = !__member_data<_Tp> && __can_borrow<_Tp> && requires(_Tp&& __t) {
  { ranges::begin(__t) } -> contiguous_iterator;
};

struct __fn {
  template <__member_data _Tp>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const noexcept(noexcept(__t.data())) {
    return __t.data();
  }

  template <__ranges_begin_invocable _Tp>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(std::to_address(ranges::begin(__t)))) {
    return std::to_address(ranges::begin(__t));
  }
};
} // namespace __data

inline namespace __cpo {
inline constexpr auto data = __data::__fn{};
} // namespace __cpo
} // namespace ranges

// [range.prim.cdata]

namespace ranges {
namespace __cdata {
struct __fn {
  template <class _Tp>
    requires is_lvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(ranges::data(static_cast<const remove_reference_t<_Tp>&>(__t))))
          -> decltype(ranges::data(static_cast<const remove_reference_t<_Tp>&>(__t))) {
    return ranges::data(static_cast<const remove_reference_t<_Tp>&>(__t));
  }

  template <class _Tp>
    requires is_rvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const noexcept(
      noexcept(ranges::data(static_cast<const _Tp&&>(__t)))) -> decltype(ranges::data(static_cast<const _Tp&&>(__t))) {
    return ranges::data(static_cast<const _Tp&&>(__t));
  }
};
} // namespace __cdata

inline namespace __cpo {
inline constexpr auto cdata = __cdata::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_DATA_H
