//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COMPARE_ORDERING_H
#define _LIBCPP___COMPARE_ORDERING_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_same.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// exposition only
enum class _OrdResult : signed char { __less = -1, __equiv = 0, __greater = 1 };

enum class _NCmpResult : signed char { __unordered = -127 };

class partial_ordering;
class weak_ordering;
class strong_ordering;

template <class _Tp, class... _Args>
inline constexpr bool __one_of_v = (is_same_v<_Tp, _Args> || ...);

struct _CmpUnspecifiedParam {
  _LIBCPP_HIDE_FROM_ABI constexpr _CmpUnspecifiedParam(int _CmpUnspecifiedParam::*) noexcept {}

  template <class _Tp, class = enable_if_t<!__one_of_v<_Tp, int, partial_ordering, weak_ordering, strong_ordering>>>
  _CmpUnspecifiedParam(_Tp) = delete;
};

class partial_ordering {
  using _ValueT = signed char;

  _LIBCPP_HIDE_FROM_ABI explicit constexpr partial_ordering(_OrdResult __v) noexcept : __value_(_ValueT(__v)) {}

  _LIBCPP_HIDE_FROM_ABI explicit constexpr partial_ordering(_NCmpResult __v) noexcept : __value_(_ValueT(__v)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr bool __is_ordered() const noexcept {
    return __value_ != _ValueT(_NCmpResult::__unordered);
  }

public:
  // valid values
  static const partial_ordering less;
  static const partial_ordering equivalent;
  static const partial_ordering greater;
  static const partial_ordering unordered;

  // comparisons
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(partial_ordering, partial_ordering) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(partial_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__is_ordered() && __v.__value_ == 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(partial_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__is_ordered() && __v.__value_ < 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(partial_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__is_ordered() && __v.__value_ <= 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(partial_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__is_ordered() && __v.__value_ > 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(partial_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__is_ordered() && __v.__value_ >= 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(_CmpUnspecifiedParam, partial_ordering __v) noexcept {
    return __v.__is_ordered() && 0 < __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(_CmpUnspecifiedParam, partial_ordering __v) noexcept {
    return __v.__is_ordered() && 0 <= __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(_CmpUnspecifiedParam, partial_ordering __v) noexcept {
    return __v.__is_ordered() && 0 > __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(_CmpUnspecifiedParam, partial_ordering __v) noexcept {
    return __v.__is_ordered() && 0 >= __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr partial_ordering
  operator<=>(partial_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr partial_ordering
  operator<=>(_CmpUnspecifiedParam, partial_ordering __v) noexcept {
    return __v < 0 ? partial_ordering::greater : (__v > 0 ? partial_ordering::less : __v);
  }

private:
  _ValueT __value_;
};

inline constexpr partial_ordering partial_ordering::less(_OrdResult::__less);
inline constexpr partial_ordering partial_ordering::equivalent(_OrdResult::__equiv);
inline constexpr partial_ordering partial_ordering::greater(_OrdResult::__greater);
inline constexpr partial_ordering partial_ordering::unordered(_NCmpResult ::__unordered);

class weak_ordering {
  using _ValueT = signed char;

  _LIBCPP_HIDE_FROM_ABI explicit constexpr weak_ordering(_OrdResult __v) noexcept : __value_(_ValueT(__v)) {}

public:
  static const weak_ordering less;
  static const weak_ordering equivalent;
  static const weak_ordering greater;

  _LIBCPP_HIDE_FROM_ABI constexpr operator partial_ordering() const noexcept {
    return __value_ == 0 ? partial_ordering::equivalent
                         : (__value_ < 0 ? partial_ordering::less : partial_ordering::greater);
  }

  // comparisons
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(weak_ordering, weak_ordering) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(weak_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ == 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(weak_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ < 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(weak_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ <= 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(weak_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ > 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(weak_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ >= 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(_CmpUnspecifiedParam, weak_ordering __v) noexcept {
    return 0 < __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(_CmpUnspecifiedParam, weak_ordering __v) noexcept {
    return 0 <= __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(_CmpUnspecifiedParam, weak_ordering __v) noexcept {
    return 0 > __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(_CmpUnspecifiedParam, weak_ordering __v) noexcept {
    return 0 >= __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr weak_ordering operator<=>(weak_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr weak_ordering operator<=>(_CmpUnspecifiedParam, weak_ordering __v) noexcept {
    return __v < 0 ? weak_ordering::greater : (__v > 0 ? weak_ordering::less : __v);
  }

private:
  _ValueT __value_;
};

inline constexpr weak_ordering weak_ordering::less(_OrdResult::__less);
inline constexpr weak_ordering weak_ordering::equivalent(_OrdResult::__equiv);
inline constexpr weak_ordering weak_ordering::greater(_OrdResult::__greater);

class strong_ordering {
  using _ValueT = signed char;

  _LIBCPP_HIDE_FROM_ABI explicit constexpr strong_ordering(_OrdResult __v) noexcept : __value_(_ValueT(__v)) {}

public:
  static const strong_ordering less;
  static const strong_ordering equal;
  static const strong_ordering equivalent;
  static const strong_ordering greater;

  // conversions
  _LIBCPP_HIDE_FROM_ABI constexpr operator partial_ordering() const noexcept {
    return __value_ == 0 ? partial_ordering::equivalent
                         : (__value_ < 0 ? partial_ordering::less : partial_ordering::greater);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr operator weak_ordering() const noexcept {
    return __value_ == 0 ? weak_ordering::equivalent : (__value_ < 0 ? weak_ordering::less : weak_ordering::greater);
  }

  // comparisons
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(strong_ordering, strong_ordering) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(strong_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ == 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(strong_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ < 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(strong_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ <= 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(strong_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ > 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(strong_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v.__value_ >= 0;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(_CmpUnspecifiedParam, strong_ordering __v) noexcept {
    return 0 < __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(_CmpUnspecifiedParam, strong_ordering __v) noexcept {
    return 0 <= __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(_CmpUnspecifiedParam, strong_ordering __v) noexcept {
    return 0 > __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(_CmpUnspecifiedParam, strong_ordering __v) noexcept {
    return 0 >= __v.__value_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr strong_ordering
  operator<=>(strong_ordering __v, _CmpUnspecifiedParam) noexcept {
    return __v;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr strong_ordering
  operator<=>(_CmpUnspecifiedParam, strong_ordering __v) noexcept {
    return __v < 0 ? strong_ordering::greater : (__v > 0 ? strong_ordering::less : __v);
  }

private:
  _ValueT __value_;
};

inline constexpr strong_ordering strong_ordering::less(_OrdResult::__less);
inline constexpr strong_ordering strong_ordering::equal(_OrdResult::__equiv);
inline constexpr strong_ordering strong_ordering::equivalent(_OrdResult::__equiv);
inline constexpr strong_ordering strong_ordering::greater(_OrdResult::__greater);

/// [cmp.categories.pre]/1
/// The types partial_ordering, weak_ordering, and strong_ordering are
/// collectively termed the comparison category types.
template <class _Tp>
concept __comparison_category = __one_of_v<_Tp, partial_ordering, weak_ordering, strong_ordering>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___COMPARE_ORDERING_H
