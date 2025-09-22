// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_EXPERIMENTAL___SIMD_UTILITY_H
#define _LIBCPP_EXPERIMENTAL___SIMD_UTILITY_H

#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_const.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_unsigned.h>
#include <__type_traits/is_volatile.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <__utility/integer_sequence.h>
#include <cstddef>
#include <cstdint>
#include <experimental/__config>
#include <limits>

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)

_LIBCPP_BEGIN_NAMESPACE_EXPERIMENTAL
inline namespace parallelism_v2 {
template <class _Tp>
inline constexpr bool __is_vectorizable_v =
    is_arithmetic_v<_Tp> && !is_const_v<_Tp> && !is_volatile_v<_Tp> && !is_same_v<_Tp, bool>;

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI auto __choose_mask_type() {
  if constexpr (sizeof(_Tp) == 1) {
    return uint8_t{};
  } else if constexpr (sizeof(_Tp) == 2) {
    return uint16_t{};
  } else if constexpr (sizeof(_Tp) == 4) {
    return uint32_t{};
  } else if constexpr (sizeof(_Tp) == 8) {
    return uint64_t{};
  }
#  ifndef _LIBCPP_HAS_NO_INT128
  else if constexpr (sizeof(_Tp) == 16) {
    return __uint128_t{};
  }
#  endif
  else
    static_assert(sizeof(_Tp) == 0, "Unexpected size");
}

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI auto constexpr __set_all_bits(bool __v) {
  return __v ? (numeric_limits<decltype(__choose_mask_type<_Tp>())>::max()) : 0;
}

template <class _From, class _To, class = void>
inline constexpr bool __is_non_narrowing_convertible_v = false;

template <class _From, class _To>
inline constexpr bool __is_non_narrowing_convertible_v<_From, _To, std::void_t<decltype(_To{std::declval<_From>()})>> =
    true;

template <class _Tp, class _Up>
inline constexpr bool __can_broadcast_v =
    (__is_vectorizable_v<_Up> && __is_non_narrowing_convertible_v<_Up, _Tp>) ||
    (!__is_vectorizable_v<_Up> && is_convertible_v<_Up, _Tp>) || is_same_v<_Up, int> ||
    (is_same_v<_Up, unsigned int> && is_unsigned_v<_Tp>);

template <class _Tp, class _Generator, std::size_t _Idx, class = void>
inline constexpr bool __is_well_formed = false;

template <class _Tp, class _Generator, std::size_t _Idx>
inline constexpr bool
    __is_well_formed<_Tp,
                     _Generator,
                     _Idx,
                     std::void_t<decltype(std::declval<_Generator>()(integral_constant<size_t, _Idx>()))>> =
        __can_broadcast_v<_Tp, decltype(std::declval<_Generator>()(integral_constant<size_t, _Idx>()))>;

template <class _Tp, class _Generator, std::size_t... _Idxes>
_LIBCPP_HIDE_FROM_ABI constexpr bool __can_generate(index_sequence<_Idxes...>) {
  return (true && ... && __is_well_formed<_Tp, _Generator, _Idxes>);
}

template <class _Tp, class _Generator, std::size_t _Size>
inline constexpr bool __can_generate_v = experimental::__can_generate<_Tp, _Generator>(make_index_sequence<_Size>());

} // namespace parallelism_v2
_LIBCPP_END_NAMESPACE_EXPERIMENTAL

#endif // _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)

_LIBCPP_POP_MACROS

#endif // _LIBCPP_EXPERIMENTAL___SIMD_UTILITY_H
