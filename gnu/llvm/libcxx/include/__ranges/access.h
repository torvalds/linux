// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_ACCESS_H
#define _LIBCPP___RANGES_ACCESS_H

#include <__concepts/class_or_enum.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/readable_traits.h>
#include <__ranges/enable_borrowed_range.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/remove_reference.h>
#include <__utility/auto_cast.h>
#include <__utility/declval.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
template <class _Tp>
concept __can_borrow = is_lvalue_reference_v<_Tp> || enable_borrowed_range<remove_cvref_t<_Tp>>;
} // namespace ranges

// [range.access.begin]

namespace ranges {
namespace __begin {
template <class _Tp>
concept __member_begin = __can_borrow<_Tp> && requires(_Tp&& __t) {
  { _LIBCPP_AUTO_CAST(__t.begin()) } -> input_or_output_iterator;
};

void begin() = delete;

template <class _Tp>
concept __unqualified_begin =
    !__member_begin<_Tp> && __can_borrow<_Tp> && __class_or_enum<remove_cvref_t<_Tp>> && requires(_Tp&& __t) {
      { _LIBCPP_AUTO_CAST(begin(__t)) } -> input_or_output_iterator;
    };

struct __fn {
  template <class _Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp (&__t)[]) const noexcept
    requires(sizeof(_Tp) >= 0) // Disallow incomplete element types.
  {
    return __t + 0;
  }

  template <class _Tp, size_t _Np>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp (&__t)[_Np]) const noexcept
    requires(sizeof(_Tp) >= 0) // Disallow incomplete element types.
  {
    return __t + 0;
  }

  template <class _Tp>
    requires __member_begin<_Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(__t.begin()))) {
    return _LIBCPP_AUTO_CAST(__t.begin());
  }

  template <class _Tp>
    requires __unqualified_begin<_Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(begin(__t)))) {
    return _LIBCPP_AUTO_CAST(begin(__t));
  }

  void operator()(auto&&) const = delete;
};
} // namespace __begin

inline namespace __cpo {
inline constexpr auto begin = __begin::__fn{};
} // namespace __cpo
} // namespace ranges

// [range.range]

namespace ranges {
template <class _Tp>
using iterator_t = decltype(ranges::begin(std::declval<_Tp&>()));
} // namespace ranges

// [range.access.end]

namespace ranges {
namespace __end {
template <class _Tp>
concept __member_end = __can_borrow<_Tp> && requires(_Tp&& __t) {
  typename iterator_t<_Tp>;
  { _LIBCPP_AUTO_CAST(__t.end()) } -> sentinel_for<iterator_t<_Tp>>;
};

void end() = delete;

template <class _Tp>
concept __unqualified_end =
    !__member_end<_Tp> && __can_borrow<_Tp> && __class_or_enum<remove_cvref_t<_Tp>> && requires(_Tp&& __t) {
      typename iterator_t<_Tp>;
      { _LIBCPP_AUTO_CAST(end(__t)) } -> sentinel_for<iterator_t<_Tp>>;
    };

struct __fn {
  template <class _Tp, size_t _Np>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp (&__t)[_Np]) const noexcept
    requires(sizeof(_Tp) >= 0) // Disallow incomplete element types.
  {
    return __t + _Np;
  }

  template <class _Tp>
    requires __member_end<_Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(__t.end()))) {
    return _LIBCPP_AUTO_CAST(__t.end());
  }

  template <class _Tp>
    requires __unqualified_end<_Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(end(__t)))) {
    return _LIBCPP_AUTO_CAST(end(__t));
  }

  void operator()(auto&&) const = delete;
};
} // namespace __end

inline namespace __cpo {
inline constexpr auto end = __end::__fn{};
} // namespace __cpo
} // namespace ranges

// [range.access.cbegin]

namespace ranges {
namespace __cbegin {
struct __fn {
  template <class _Tp>
    requires is_lvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(ranges::begin(static_cast<const remove_reference_t<_Tp>&>(__t))))
          -> decltype(ranges::begin(static_cast<const remove_reference_t<_Tp>&>(__t))) {
    return ranges::begin(static_cast<const remove_reference_t<_Tp>&>(__t));
  }

  template <class _Tp>
    requires is_rvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(ranges::begin(static_cast<const _Tp&&>(__t))))
          -> decltype(ranges::begin(static_cast<const _Tp&&>(__t))) {
    return ranges::begin(static_cast<const _Tp&&>(__t));
  }
};
} // namespace __cbegin

inline namespace __cpo {
inline constexpr auto cbegin = __cbegin::__fn{};
} // namespace __cpo
} // namespace ranges

// [range.access.cend]

namespace ranges {
namespace __cend {
struct __fn {
  template <class _Tp>
    requires is_lvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const
      noexcept(noexcept(ranges::end(static_cast<const remove_reference_t<_Tp>&>(__t))))
          -> decltype(ranges::end(static_cast<const remove_reference_t<_Tp>&>(__t))) {
    return ranges::end(static_cast<const remove_reference_t<_Tp>&>(__t));
  }

  template <class _Tp>
    requires is_rvalue_reference_v<_Tp&&>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t) const noexcept(
      noexcept(ranges::end(static_cast<const _Tp&&>(__t)))) -> decltype(ranges::end(static_cast<const _Tp&&>(__t))) {
    return ranges::end(static_cast<const _Tp&&>(__t));
  }
};
} // namespace __cend

inline namespace __cpo {
inline constexpr auto cend = __cend::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_ACCESS_H
