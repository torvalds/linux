//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_CLAMP_H
#define _LIBCPP___ALGORITHM_RANGES_CLAMP_H

#include <__assert>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/projected.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __clamp {
struct __fn {
  template <class _Type,
            class _Proj                                                      = identity,
            indirect_strict_weak_order<projected<const _Type*, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr const _Type& operator()(
      const _Type& __value, const _Type& __low, const _Type& __high, _Comp __comp = {}, _Proj __proj = {}) const {
    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        !bool(std::invoke(__comp, std::invoke(__proj, __high), std::invoke(__proj, __low))),
        "Bad bounds passed to std::ranges::clamp");

    auto&& __projected = std::invoke(__proj, __value);
    if (std::invoke(__comp, std::forward<decltype(__projected)>(__projected), std::invoke(__proj, __low)))
      return __low;
    else if (std::invoke(__comp, std::invoke(__proj, __high), std::forward<decltype(__projected)>(__projected)))
      return __high;
    else
      return __value;
  }
};
} // namespace __clamp

inline namespace __cpo {
inline constexpr auto clamp = __clamp::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_CLAMP_H
