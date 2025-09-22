//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_REMOVE_H
#define _LIBCPP___ALGORITHM_RANGES_REMOVE_H
#include <__config>

#include <__algorithm/ranges_remove_if.h>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/permutable.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/subrange.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __remove {
struct __fn {
  template <permutable _Iter, sentinel_for<_Iter> _Sent, class _Type, class _Proj = identity>
    requires indirect_binary_predicate<ranges::equal_to, projected<_Iter, _Proj>, const _Type*>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr subrange<_Iter>
  operator()(_Iter __first, _Sent __last, const _Type& __value, _Proj __proj = {}) const {
    auto __pred = [&](auto&& __other) -> bool { return __value == __other; };
    return ranges::__remove_if_impl(std::move(__first), std::move(__last), __pred, __proj);
  }

  template <forward_range _Range, class _Type, class _Proj = identity>
    requires permutable<iterator_t<_Range>> &&
             indirect_binary_predicate<ranges::equal_to, projected<iterator_t<_Range>, _Proj>, const _Type*>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr borrowed_subrange_t<_Range>
  operator()(_Range&& __range, const _Type& __value, _Proj __proj = {}) const {
    auto __pred = [&](auto&& __other) -> bool { return __value == __other; };
    return ranges::__remove_if_impl(ranges::begin(__range), ranges::end(__range), __pred, __proj);
  }
};
} // namespace __remove

inline namespace __cpo {
inline constexpr auto remove = __remove::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_REMOVE_H
