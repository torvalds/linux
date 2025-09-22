//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_TRANSFORM_H
#define _LIBCPP___ALGORITHM_RANGES_TRANSFORM_H

#include <__algorithm/in_in_out_result.h>
#include <__algorithm/in_out_result.h>
#include <__concepts/constructible.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
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

template <class _Ip, class _Op>
using unary_transform_result = in_out_result<_Ip, _Op>;

template <class _I1, class _I2, class _O1>
using binary_transform_result = in_in_out_result<_I1, _I2, _O1>;

namespace __transform {
struct __fn {
private:
  template <class _InIter, class _Sent, class _OutIter, class _Func, class _Proj>
  _LIBCPP_HIDE_FROM_ABI static constexpr unary_transform_result<_InIter, _OutIter>
  __unary(_InIter __first, _Sent __last, _OutIter __result, _Func& __operation, _Proj& __projection) {
    while (__first != __last) {
      *__result = std::invoke(__operation, std::invoke(__projection, *__first));
      ++__first;
      ++__result;
    }

    return {std::move(__first), std::move(__result)};
  }

  template <class _InIter1,
            class _Sent1,
            class _InIter2,
            class _Sent2,
            class _OutIter,
            class _Func,
            class _Proj1,
            class _Proj2>
  _LIBCPP_HIDE_FROM_ABI static constexpr binary_transform_result<_InIter1, _InIter2, _OutIter>
  __binary(_InIter1 __first1,
           _Sent1 __last1,
           _InIter2 __first2,
           _Sent2 __last2,
           _OutIter __result,
           _Func& __binary_operation,
           _Proj1& __projection1,
           _Proj2& __projection2) {
    while (__first1 != __last1 && __first2 != __last2) {
      *__result =
          std::invoke(__binary_operation, std::invoke(__projection1, *__first1), std::invoke(__projection2, *__first2));
      ++__first1;
      ++__first2;
      ++__result;
    }
    return {std::move(__first1), std::move(__first2), std::move(__result)};
  }

public:
  template <input_iterator _InIter,
            sentinel_for<_InIter> _Sent,
            weakly_incrementable _OutIter,
            copy_constructible _Func,
            class _Proj = identity>
    requires indirectly_writable<_OutIter, indirect_result_t<_Func&, projected<_InIter, _Proj>>>
  _LIBCPP_HIDE_FROM_ABI constexpr unary_transform_result<_InIter, _OutIter>
  operator()(_InIter __first, _Sent __last, _OutIter __result, _Func __operation, _Proj __proj = {}) const {
    return __unary(std::move(__first), std::move(__last), std::move(__result), __operation, __proj);
  }

  template <input_range _Range, weakly_incrementable _OutIter, copy_constructible _Func, class _Proj = identity>
    requires indirectly_writable<_OutIter, indirect_result_t<_Func, projected<iterator_t<_Range>, _Proj>>>
  _LIBCPP_HIDE_FROM_ABI constexpr unary_transform_result<borrowed_iterator_t<_Range>, _OutIter>
  operator()(_Range&& __range, _OutIter __result, _Func __operation, _Proj __projection = {}) const {
    return __unary(ranges::begin(__range), ranges::end(__range), std::move(__result), __operation, __projection);
  }

  template <input_iterator _InIter1,
            sentinel_for<_InIter1> _Sent1,
            input_iterator _InIter2,
            sentinel_for<_InIter2> _Sent2,
            weakly_incrementable _OutIter,
            copy_constructible _Func,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_writable<_OutIter,
                                 indirect_result_t<_Func&, projected<_InIter1, _Proj1>, projected<_InIter2, _Proj2>>>
  _LIBCPP_HIDE_FROM_ABI constexpr binary_transform_result<_InIter1, _InIter2, _OutIter> operator()(
      _InIter1 __first1,
      _Sent1 __last1,
      _InIter2 __first2,
      _Sent2 __last2,
      _OutIter __result,
      _Func __binary_operation,
      _Proj1 __projection1 = {},
      _Proj2 __projection2 = {}) const {
    return __binary(
        std::move(__first1),
        std::move(__last1),
        std::move(__first2),
        std::move(__last2),
        std::move(__result),
        __binary_operation,
        __projection1,
        __projection2);
  }

  template <input_range _Range1,
            input_range _Range2,
            weakly_incrementable _OutIter,
            copy_constructible _Func,
            class _Proj1 = identity,
            class _Proj2 = identity>
    requires indirectly_writable<
        _OutIter,
        indirect_result_t<_Func&, projected<iterator_t<_Range1>, _Proj1>, projected<iterator_t<_Range2>, _Proj2>>>
  _LIBCPP_HIDE_FROM_ABI constexpr binary_transform_result<borrowed_iterator_t<_Range1>,
                                                          borrowed_iterator_t<_Range2>,
                                                          _OutIter>
  operator()(_Range1&& __range1,
             _Range2&& __range2,
             _OutIter __result,
             _Func __binary_operation,
             _Proj1 __projection1 = {},
             _Proj2 __projection2 = {}) const {
    return __binary(
        ranges::begin(__range1),
        ranges::end(__range1),
        ranges::begin(__range2),
        ranges::end(__range2),
        std::move(__result),
        __binary_operation,
        __projection1,
        __projection2);
  }
};
} // namespace __transform

inline namespace __cpo {
inline constexpr auto transform = __transform::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_TRANSFORM_H
