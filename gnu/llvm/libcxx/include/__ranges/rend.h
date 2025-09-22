// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_REND_H
#define _LIBCPP___RANGES_REND_H

#include <__concepts/class_or_enum.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/readable_traits.h>
#include <__iterator/reverse_iterator.h>
#include <__ranges/access.h>
#include <__ranges/rbegin.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/remove_reference.h>
#include <__utility/auto_cast.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [range.access.rend]

namespace ranges {
namespace __rend {
template <class _Tp>
concept __member_rend = __can_borrow<_Tp> && requires(_Tp&& __t) {
  ranges::rbegin(__t);
  { _LIBCPP_AUTO_CAST(__t.rend()) } -> sentinel_for<decltype(ranges::rbegin(__t))>;
};

void rend() = delete;

template <class _Tp>
concept __unqualified_rend =
    !__member_rend<_Tp> && __can_borrow<_Tp> && __class_or_enum<remove_cvref_t<_Tp>> && requires(_Tp&& __t) {
      ranges::rbegin(__t);
      { _LIBCPP_AUTO_CAST(rend(__t)) } -> sentinel_for<decltype(ranges::rbegin(__t))>;
    };

template <class _Tp>
concept __can_reverse = __can_borrow<_Tp> && !__member_rend<_Tp> && !__unqualified_rend<_Tp> && requires(_Tp&& __t) {
  { ranges::begin(__t) } -> same_as<decltype(ranges::end(__t))>;
  { ranges::begin(__t) } -> bidirectional_iterator;
};

class __fn {
public:
  template <class _Tp>
    requires __member_rend<_Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(__t.rend()))) {
    return _LIBCPP_AUTO_CAST(__t.rend());
  }

  template <class _Tp>
    requires __unqualified_rend<_Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(rend(__t)))) {
    return _LIBCPP_AUTO_CAST(rend(__t));
  }

  template <class _Tp>
    requires __can_reverse<_Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(ranges::begin(__t))) {
    return std::make_reverse_iterator(ranges::begin(__t));
  }

  void operator()(auto&&) const = delete;
};
} // namespace __rend

inline namespace __cpo {
inline constexpr auto rend = __rend::__fn{};
} // namespace __cpo
} // namespace ranges

// [range.access.crend]

namespace ranges {
namespace __crend {
struct __fn {
  template <class _Tp>
    requires is_lvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(ranges::rend(static_cast<const remove_reference_t<_Tp>&>(__t))))
          -> decltype(ranges::rend(static_cast<const remove_reference_t<_Tp>&>(__t))) {
    return ranges::rend(static_cast<const remove_reference_t<_Tp>&>(__t));
  }

  template <class _Tp>
    requires is_rvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const noexcept(
      noexcept(ranges::rend(static_cast<const _Tp&&>(__t)))) -> decltype(ranges::rend(static_cast<const _Tp&&>(__t))) {
    return ranges::rend(static_cast<const _Tp&&>(__t));
  }
};
} // namespace __crend

inline namespace __cpo {
inline constexpr auto crend = __crend::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_REND_H
