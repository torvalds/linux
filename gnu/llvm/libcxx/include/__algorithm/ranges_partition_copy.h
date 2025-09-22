//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_PARTITION_COPY_H
#define _LIBCPP___ALGORITHM_RANGES_PARTITION_COPY_H

#include <__algorithm/in_out_out_result.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

template <class _InIter, class _OutIter1, class _OutIter2>
using partition_copy_result = in_out_out_result<_InIter, _OutIter1, _OutIter2>;

namespace __partition_copy {

struct __fn {
  // TODO(ranges): delegate to the classic algorithm.
  template <class _InIter, class _Sent, class _OutIter1, class _OutIter2, class _Proj, class _Pred>
  _LIBCPP_HIDE_FROM_ABI constexpr static partition_copy_result<__remove_cvref_t<_InIter>,
                                                               __remove_cvref_t<_OutIter1>,
                                                               __remove_cvref_t<_OutIter2> >
  __partition_copy_fn_impl(
      _InIter&& __first,
      _Sent&& __last,
      _OutIter1&& __out_true,
      _OutIter2&& __out_false,
      _Pred& __pred,
      _Proj& __proj) {
    for (; __first != __last; ++__first) {
      if (std::invoke(__pred, std::invoke(__proj, *__first))) {
        *__out_true = *__first;
        ++__out_true;

      } else {
        *__out_false = *__first;
        ++__out_false;
      }
    }

    return {std::move(__first), std::move(__out_true), std::move(__out_false)};
  }

  template <input_iterator _InIter,
            sentinel_for<_InIter> _Sent,
            weakly_incrementable _OutIter1,
            weakly_incrementable _OutIter2,
            class _Proj = identity,
            indirect_unary_predicate<projected<_InIter, _Proj>> _Pred>
    requires indirectly_copyable<_InIter, _OutIter1> && indirectly_copyable<_InIter, _OutIter2>
  _LIBCPP_HIDE_FROM_ABI constexpr partition_copy_result<_InIter, _OutIter1, _OutIter2> operator()(
      _InIter __first, _Sent __last, _OutIter1 __out_true, _OutIter2 __out_false, _Pred __pred, _Proj __proj = {})
      const {
    return __partition_copy_fn_impl(
        std::move(__first), std::move(__last), std::move(__out_true), std::move(__out_false), __pred, __proj);
  }

  template <input_range _Range,
            weakly_incrementable _OutIter1,
            weakly_incrementable _OutIter2,
            class _Proj = identity,
            indirect_unary_predicate<projected<iterator_t<_Range>, _Proj>> _Pred>
    requires indirectly_copyable<iterator_t<_Range>, _OutIter1> && indirectly_copyable<iterator_t<_Range>, _OutIter2>
  _LIBCPP_HIDE_FROM_ABI constexpr partition_copy_result<borrowed_iterator_t<_Range>, _OutIter1, _OutIter2>
  operator()(_Range&& __range, _OutIter1 __out_true, _OutIter2 __out_false, _Pred __pred, _Proj __proj = {}) const {
    return __partition_copy_fn_impl(
        ranges::begin(__range), ranges::end(__range), std::move(__out_true), std::move(__out_false), __pred, __proj);
  }
};

} // namespace __partition_copy

inline namespace __cpo {
inline constexpr auto partition_copy = __partition_copy::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_PARTITION_COPY_H
