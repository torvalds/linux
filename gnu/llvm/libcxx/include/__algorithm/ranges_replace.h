//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_REPLACE_H
#define _LIBCPP___ALGORITHM_RANGES_REPLACE_H

#include <__algorithm/ranges_replace_if.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
namespace __replace {
struct __fn {
  template <input_iterator _Iter, sentinel_for<_Iter> _Sent, class _Type1, class _Type2, class _Proj = identity>
    requires indirectly_writable<_Iter, const _Type2&> &&
             indirect_binary_predicate<ranges::equal_to, projected<_Iter, _Proj>, const _Type1*>
  _LIBCPP_HIDE_FROM_ABI constexpr _Iter operator()(
      _Iter __first, _Sent __last, const _Type1& __old_value, const _Type2& __new_value, _Proj __proj = {}) const {
    auto __pred = [&](const auto& __val) -> bool { return __val == __old_value; };
    return ranges::__replace_if_impl(std::move(__first), std::move(__last), __pred, __new_value, __proj);
  }

  template <input_range _Range, class _Type1, class _Type2, class _Proj = identity>
    requires indirectly_writable<iterator_t<_Range>, const _Type2&> &&
             indirect_binary_predicate<ranges::equal_to, projected<iterator_t<_Range>, _Proj>, const _Type1*>
  _LIBCPP_HIDE_FROM_ABI constexpr borrowed_iterator_t<_Range>
  operator()(_Range&& __range, const _Type1& __old_value, const _Type2& __new_value, _Proj __proj = {}) const {
    auto __pred = [&](auto&& __val) -> bool { return __val == __old_value; };
    return ranges::__replace_if_impl(ranges::begin(__range), ranges::end(__range), __pred, __new_value, __proj);
  }
};
} // namespace __replace

inline namespace __cpo {
inline constexpr auto replace = __replace::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_REPLACE_H
