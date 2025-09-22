//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_ROTATE_H
#define _LIBCPP___ALGORITHM_RANGES_ROTATE_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/ranges_iterator_concept.h>
#include <__algorithm/rotate.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/permutable.h>
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
namespace __rotate {

struct __fn {
  template <class _Iter, class _Sent>
  _LIBCPP_HIDE_FROM_ABI constexpr static subrange<_Iter> __rotate_fn_impl(_Iter __first, _Iter __middle, _Sent __last) {
    auto __ret = std::__rotate<_RangeAlgPolicy>(std::move(__first), std::move(__middle), std::move(__last));
    return {std::move(__ret.first), std::move(__ret.second)};
  }

  template <permutable _Iter, sentinel_for<_Iter> _Sent>
  _LIBCPP_HIDE_FROM_ABI constexpr subrange<_Iter> operator()(_Iter __first, _Iter __middle, _Sent __last) const {
    return __rotate_fn_impl(std::move(__first), std::move(__middle), std::move(__last));
  }

  template <forward_range _Range>
    requires permutable<iterator_t<_Range>>
  _LIBCPP_HIDE_FROM_ABI constexpr borrowed_subrange_t<_Range>
  operator()(_Range&& __range, iterator_t<_Range> __middle) const {
    return __rotate_fn_impl(ranges::begin(__range), std::move(__middle), ranges::end(__range));
  }
};

} // namespace __rotate

inline namespace __cpo {
inline constexpr auto rotate = __rotate::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_ROTATE_H
