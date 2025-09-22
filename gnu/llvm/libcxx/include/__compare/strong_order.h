//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COMPARE_STRONG_ORDER
#define _LIBCPP___COMPARE_STRONG_ORDER

#include <__bit/bit_cast.h>
#include <__compare/compare_three_way.h>
#include <__compare/ordering.h>
#include <__config>
#include <__math/exponential_functions.h>
#include <__math/traits.h>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_floating_point.h>
#include <__type_traits/is_same.h>
#include <__utility/forward.h>
#include <__utility/priority_tag.h>
#include <cstdint>
#include <limits>

#ifndef _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [cmp.alg]
namespace __strong_order {
void strong_order() = delete;

struct __fn {
  // NOLINTBEGIN(libcpp-robust-against-adl) strong_order should use ADL, but only here
  template <class _Tp, class _Up>
    requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __go(_Tp&& __t, _Up&& __u, __priority_tag<2>) noexcept(
      noexcept(strong_ordering(strong_order(std::forward<_Tp>(__t), std::forward<_Up>(__u)))))
      -> decltype(strong_ordering(strong_order(std::forward<_Tp>(__t), std::forward<_Up>(__u)))) {
    return strong_ordering(strong_order(std::forward<_Tp>(__t), std::forward<_Up>(__u)));
  }
  // NOLINTEND(libcpp-robust-against-adl)

  template <class _Tp, class _Up, class _Dp = decay_t<_Tp>>
    requires is_same_v<_Dp, decay_t<_Up>> && is_floating_point_v<_Dp>
  _LIBCPP_HIDE_FROM_ABI static constexpr strong_ordering __go(_Tp&& __t, _Up&& __u, __priority_tag<1>) noexcept {
    if constexpr (numeric_limits<_Dp>::is_iec559 && sizeof(_Dp) == sizeof(int32_t)) {
      int32_t __rx = std::bit_cast<int32_t>(__t);
      int32_t __ry = std::bit_cast<int32_t>(__u);
      __rx         = (__rx < 0) ? (numeric_limits<int32_t>::min() - __rx - 1) : __rx;
      __ry         = (__ry < 0) ? (numeric_limits<int32_t>::min() - __ry - 1) : __ry;
      return (__rx <=> __ry);
    } else if constexpr (numeric_limits<_Dp>::is_iec559 && sizeof(_Dp) == sizeof(int64_t)) {
      int64_t __rx = std::bit_cast<int64_t>(__t);
      int64_t __ry = std::bit_cast<int64_t>(__u);
      __rx         = (__rx < 0) ? (numeric_limits<int64_t>::min() - __rx - 1) : __rx;
      __ry         = (__ry < 0) ? (numeric_limits<int64_t>::min() - __ry - 1) : __ry;
      return (__rx <=> __ry);
    } else if (__t < __u) {
      return strong_ordering::less;
    } else if (__t > __u) {
      return strong_ordering::greater;
    } else if (__t == __u) {
      if constexpr (numeric_limits<_Dp>::radix == 2) {
        return __math::signbit(__u) <=> __math::signbit(__t);
      } else {
        // This is bullet 3 of the IEEE754 algorithm, relevant
        // only for decimal floating-point;
        // see https://stackoverflow.com/questions/69068075/
        if (__t == 0 || __math::isinf(__t)) {
          return __math::signbit(__u) <=> __math::signbit(__t);
        } else {
          int __texp, __uexp;
          (void)__math::frexp(__t, &__texp);
          (void)__math::frexp(__u, &__uexp);
          return (__t < 0) ? (__texp <=> __uexp) : (__uexp <=> __texp);
        }
      }
    } else {
      // They're unordered, so one of them must be a NAN.
      // The order is -QNAN, -SNAN, numbers, +SNAN, +QNAN.
      bool __t_is_nan      = __math::isnan(__t);
      bool __u_is_nan      = __math::isnan(__u);
      bool __t_is_negative = __math::signbit(__t);
      bool __u_is_negative = __math::signbit(__u);
      using _IntType =
          conditional_t< sizeof(__t) == sizeof(int32_t),
                         int32_t,
                         conditional_t< sizeof(__t) == sizeof(int64_t), int64_t, void> >;
      if constexpr (is_same_v<_IntType, void>) {
        static_assert(sizeof(_Dp) == 0, "std::strong_order is unimplemented for this floating-point type");
      } else if (__t_is_nan && __u_is_nan) {
        // Order by sign bit, then by "payload bits" (we'll just use bit_cast).
        if (__t_is_negative != __u_is_negative) {
          return (__u_is_negative <=> __t_is_negative);
        } else {
          return std::bit_cast<_IntType>(__t) <=> std::bit_cast<_IntType>(__u);
        }
      } else if (__t_is_nan) {
        return __t_is_negative ? strong_ordering::less : strong_ordering::greater;
      } else {
        return __u_is_negative ? strong_ordering::greater : strong_ordering::less;
      }
    }
  }

  template <class _Tp, class _Up>
    requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __go(_Tp&& __t, _Up&& __u, __priority_tag<0>) noexcept(
      noexcept(strong_ordering(compare_three_way()(std::forward<_Tp>(__t), std::forward<_Up>(__u)))))
      -> decltype(strong_ordering(compare_three_way()(std::forward<_Tp>(__t), std::forward<_Up>(__u)))) {
    return strong_ordering(compare_three_way()(std::forward<_Tp>(__t), std::forward<_Up>(__u)));
  }

  template <class _Tp, class _Up>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t, _Up&& __u) const
      noexcept(noexcept(__go(std::forward<_Tp>(__t), std::forward<_Up>(__u), __priority_tag<2>())))
          -> decltype(__go(std::forward<_Tp>(__t), std::forward<_Up>(__u), __priority_tag<2>())) {
    return __go(std::forward<_Tp>(__t), std::forward<_Up>(__u), __priority_tag<2>());
  }
};
} // namespace __strong_order

inline namespace __cpo {
inline constexpr auto strong_order = __strong_order::__fn{};
} // namespace __cpo

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___COMPARE_STRONG_ORDER
