//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COMPARE_COMPARE_PARTIAL_ORDER_FALLBACK
#define _LIBCPP___COMPARE_COMPARE_PARTIAL_ORDER_FALLBACK

#include <__compare/ordering.h>
#include <__compare/partial_order.h>
#include <__config>
#include <__type_traits/decay.h>
#include <__type_traits/is_same.h>
#include <__utility/forward.h>
#include <__utility/priority_tag.h>

#ifndef _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [cmp.alg]
namespace __compare_partial_order_fallback {
struct __fn {
  template <class _Tp, class _Up>
    requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __go(_Tp&& __t, _Up&& __u, __priority_tag<1>) noexcept(
      noexcept(std::partial_order(std::forward<_Tp>(__t), std::forward<_Up>(__u))))
      -> decltype(std::partial_order(std::forward<_Tp>(__t), std::forward<_Up>(__u))) {
    return std::partial_order(std::forward<_Tp>(__t), std::forward<_Up>(__u));
  }

  template <class _Tp, class _Up>
    requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __go(_Tp&& __t, _Up&& __u, __priority_tag<0>) noexcept(noexcept(
      std::forward<_Tp>(__t) == std::forward<_Up>(__u)  ? partial_ordering::equivalent
      : std::forward<_Tp>(__t) < std::forward<_Up>(__u) ? partial_ordering::less
      : std::forward<_Up>(__u) < std::forward<_Tp>(__t)
          ? partial_ordering::greater
          : partial_ordering::unordered))
      -> decltype(std::forward<_Tp>(__t) == std::forward<_Up>(__u)  ? partial_ordering::equivalent
                  : std::forward<_Tp>(__t) < std::forward<_Up>(__u) ? partial_ordering::less
                  : std::forward<_Up>(__u) < std::forward<_Tp>(__t)
                      ? partial_ordering::greater
                      : partial_ordering::unordered) {
    return std::forward<_Tp>(__t) == std::forward<_Up>(__u) ? partial_ordering::equivalent
         : std::forward<_Tp>(__t) < std::forward<_Up>(__u)  ? partial_ordering::less
         : std::forward<_Up>(__u) < std::forward<_Tp>(__t)
             ? partial_ordering::greater
             : partial_ordering::unordered;
  }

  template <class _Tp, class _Up>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Tp&& __t, _Up&& __u) const
      noexcept(noexcept(__go(std::forward<_Tp>(__t), std::forward<_Up>(__u), __priority_tag<1>())))
          -> decltype(__go(std::forward<_Tp>(__t), std::forward<_Up>(__u), __priority_tag<1>())) {
    return __go(std::forward<_Tp>(__t), std::forward<_Up>(__u), __priority_tag<1>());
  }
};
} // namespace __compare_partial_order_fallback

inline namespace __cpo {
inline constexpr auto compare_partial_order_fallback = __compare_partial_order_fallback::__fn{};
} // namespace __cpo

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___COMPARE_COMPARE_PARTIAL_ORDER_FALLBACK
