//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_PSTL_H
#define _LIBCPP___ALGORITHM_PSTL_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if !defined(_LIBCPP_HAS_NO_INCOMPLETE_PSTL) && _LIBCPP_STD_VER >= 17

#  include <__functional/operations.h>
#  include <__iterator/cpp17_iterator_concepts.h>
#  include <__iterator/iterator_traits.h>
#  include <__pstl/backend.h>
#  include <__pstl/dispatch.h>
#  include <__pstl/handle_exception.h>
#  include <__type_traits/enable_if.h>
#  include <__type_traits/is_execution_policy.h>
#  include <__type_traits/remove_cvref.h>
#  include <__utility/forward.h>
#  include <__utility/move.h>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Predicate,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool
any_of(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "any_of requires a ForwardIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__any_of, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Pred,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool
all_of(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Pred __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "all_of requires a ForwardIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__all_of, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Pred,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool
none_of(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Pred __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "none_of requires a ForwardIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__none_of, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _ForwardOutIterator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator
copy(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _ForwardOutIterator __result) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(
      _ForwardIterator, "copy(first, last, result) requires [first, last) to be ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(
      _ForwardOutIterator, "copy(first, last, result) requires result to be a ForwardIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(*__first), "copy(first, last, result) requires result to be an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__copy, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__result));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _ForwardOutIterator,
          class _Size,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator
copy_n(_ExecutionPolicy&& __policy, _ForwardIterator __first, _Size __n, _ForwardOutIterator __result) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(
      _ForwardIterator, "copy_n(first, n, result) requires first to be a ForwardIterator");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(
      _ForwardOutIterator, "copy_n(first, n, result) requires result to be a ForwardIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(*__first), "copy_n(first, n, result) requires result to be an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__copy_n, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__n), std::move(__result));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Predicate,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI __iter_diff_t<_ForwardIterator>
count_if(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(
      _ForwardIterator, "count_if(first, last, pred) requires [first, last) to be ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__count_if, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI __iter_diff_t<_ForwardIterator>
count(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(
      _ForwardIterator, "count(first, last, val) requires [first, last) to be ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__count, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), __value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _Pred,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI bool
equal(_ExecutionPolicy&& __policy,
      _ForwardIterator1 __first1,
      _ForwardIterator1 __last1,
      _ForwardIterator2 __first2,
      _Pred __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "equal requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "equal requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__equal_3leg, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI bool
equal(_ExecutionPolicy&& __policy, _ForwardIterator1 __first1, _ForwardIterator1 __last1, _ForwardIterator2 __first2) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "equal requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "equal requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__equal_3leg, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      equal_to{});
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _Pred,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI bool
equal(_ExecutionPolicy&& __policy,
      _ForwardIterator1 __first1,
      _ForwardIterator1 __last1,
      _ForwardIterator2 __first2,
      _ForwardIterator2 __last2,
      _Pred __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "equal requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "equal requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__equal, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI bool
equal(_ExecutionPolicy&& __policy,
      _ForwardIterator1 __first1,
      _ForwardIterator1 __last1,
      _ForwardIterator2 __first2,
      _ForwardIterator2 __last2) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "equal requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "equal requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__equal, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      equal_to{});
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
fill(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "fill requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__fill, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), __value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Size,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
fill_n(_ExecutionPolicy&& __policy, _ForwardIterator __first, _Size __n, const _Tp& __value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "fill_n requires a ForwardIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__fill_n, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__n), __value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Predicate,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardIterator
find_if(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "find_if requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__find_if, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Predicate,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardIterator
find_if_not(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "find_if_not requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__find_if_not, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardIterator
find(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "find requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__find, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), __value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Function,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
for_each(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Function __func) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "for_each requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__for_each, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__func));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Size,
          class _Function,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
for_each_n(_ExecutionPolicy&& __policy, _ForwardIterator __first, _Size __size, _Function __func) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "for_each_n requires a ForwardIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__for_each_n, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__size), std::move(__func));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Generator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
generate(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Generator __gen) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "generate requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__generate, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__gen));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Size,
          class _Generator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
generate_n(_ExecutionPolicy&& __policy, _ForwardIterator __first, _Size __n, _Generator __gen) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "generate_n requires a ForwardIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__generate_n, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__n), std::move(__gen));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Predicate,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI bool
is_partitioned(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "is_partitioned requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__is_partitioned, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred));
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _ForwardOutIterator,
          class _Comp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator
merge(_ExecutionPolicy&& __policy,
      _ForwardIterator1 __first1,
      _ForwardIterator1 __last1,
      _ForwardIterator2 __first2,
      _ForwardIterator2 __last2,
      _ForwardOutIterator __result,
      _Comp __comp) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "merge requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "merge requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(_ForwardOutIterator, decltype(*__first1), "merge requires an OutputIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(_ForwardOutIterator, decltype(*__first2), "merge requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__merge, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      std::move(__result),
      std::move(__comp));
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _ForwardOutIterator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator
merge(_ExecutionPolicy&& __policy,
      _ForwardIterator1 __first1,
      _ForwardIterator1 __last1,
      _ForwardIterator2 __first2,
      _ForwardIterator2 __last2,
      _ForwardOutIterator __result) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "merge requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "merge requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(_ForwardOutIterator, decltype(*__first1), "merge requires an OutputIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(_ForwardOutIterator, decltype(*__first2), "merge requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__merge, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      std::move(__result),
      less{});
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _ForwardOutIterator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator
move(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _ForwardOutIterator __result) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "move requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardOutIterator, "move requires an OutputIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(std::move(*__first)), "move requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__move, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__result));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Pred,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
replace_if(_ExecutionPolicy&& __policy,
           _ForwardIterator __first,
           _ForwardIterator __last,
           _Pred __pred,
           const _Tp& __new_value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "replace_if requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__replace_if, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__pred), __new_value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
replace(_ExecutionPolicy&& __policy,
        _ForwardIterator __first,
        _ForwardIterator __last,
        const _Tp& __old_value,
        const _Tp& __new_value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "replace requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__replace, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), __old_value, __new_value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _ForwardOutIterator,
          class _Pred,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void replace_copy_if(
    _ExecutionPolicy&& __policy,
    _ForwardIterator __first,
    _ForwardIterator __last,
    _ForwardOutIterator __result,
    _Pred __pred,
    const _Tp& __new_value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "replace_copy_if requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardOutIterator, "replace_copy_if requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(*__first), "replace_copy_if requires an OutputIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(_ForwardOutIterator, const _Tp&, "replace_copy requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__replace_copy_if, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first),
      std::move(__last),
      std::move(__result),
      std::move(__pred),
      __new_value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _ForwardOutIterator,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void replace_copy(
    _ExecutionPolicy&& __policy,
    _ForwardIterator __first,
    _ForwardIterator __last,
    _ForwardOutIterator __result,
    const _Tp& __old_value,
    const _Tp& __new_value) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "replace_copy requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardOutIterator, "replace_copy requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(*__first), "replace_copy requires an OutputIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(_ForwardOutIterator, const _Tp&, "replace_copy requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__replace_copy, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first),
      std::move(__last),
      std::move(__result),
      __old_value,
      __new_value);
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _ForwardOutIterator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator rotate_copy(
    _ExecutionPolicy&& __policy,
    _ForwardIterator __first,
    _ForwardIterator __middle,
    _ForwardIterator __last,
    _ForwardOutIterator __result) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "rotate_copy requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardOutIterator, "rotate_copy requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(*__first), "rotate_copy requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__rotate_copy, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first),
      std::move(__middle),
      std::move(__last),
      std::move(__result));
}

template <class _ExecutionPolicy,
          class _RandomAccessIterator,
          class _Comp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
sort(_ExecutionPolicy&& __policy, _RandomAccessIterator __first, _RandomAccessIterator __last, _Comp __comp) {
  _LIBCPP_REQUIRE_CPP17_RANDOM_ACCESS_ITERATOR(_RandomAccessIterator, "sort requires RandomAccessIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__sort, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__comp));
}

template <class _ExecutionPolicy,
          class _RandomAccessIterator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
sort(_ExecutionPolicy&& __policy, _RandomAccessIterator __first, _RandomAccessIterator __last) {
  _LIBCPP_REQUIRE_CPP17_RANDOM_ACCESS_ITERATOR(_RandomAccessIterator, "sort requires RandomAccessIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__sort, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), less{});
}

template <class _ExecutionPolicy,
          class _RandomAccessIterator,
          class _Comp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
stable_sort(_ExecutionPolicy&& __policy, _RandomAccessIterator __first, _RandomAccessIterator __last, _Comp __comp) {
  _LIBCPP_REQUIRE_CPP17_RANDOM_ACCESS_ITERATOR(_RandomAccessIterator, "stable_sort requires RandomAccessIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__stable_sort, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__comp));
}

template <class _ExecutionPolicy,
          class _RandomAccessIterator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI void
stable_sort(_ExecutionPolicy&& __policy, _RandomAccessIterator __first, _RandomAccessIterator __last) {
  _LIBCPP_REQUIRE_CPP17_RANDOM_ACCESS_ITERATOR(_RandomAccessIterator, "stable_sort requires RandomAccessIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__stable_sort, __pstl::__current_configuration, _RawPolicy>;
  __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), less{});
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _ForwardOutIterator,
          class _UnaryOperation,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator transform(
    _ExecutionPolicy&& __policy,
    _ForwardIterator __first,
    _ForwardIterator __last,
    _ForwardOutIterator __result,
    _UnaryOperation __op) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "transform requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardOutIterator, "transform requires an OutputIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(__op(*__first)), "transform requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__transform, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first),
      std::move(__last),
      std::move(__result),
      std::move(__op));
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _ForwardOutIterator,
          class _BinaryOperation,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _ForwardOutIterator transform(
    _ExecutionPolicy&& __policy,
    _ForwardIterator1 __first1,
    _ForwardIterator1 __last1,
    _ForwardIterator2 __first2,
    _ForwardOutIterator __result,
    _BinaryOperation __op) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "transform requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "transform requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardOutIterator, "transform requires an OutputIterator");
  _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(
      _ForwardOutIterator, decltype(__op(*__first1, *__first2)), "transform requires an OutputIterator");
  using _Implementation = __pstl::__dispatch<__pstl::__transform_binary, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__result),
      std::move(__op));
}

_LIBCPP_END_NAMESPACE_STD

#endif // !defined(_LIBCPP_HAS_NO_INCOMPLETE_PSTL) && _LIBCPP_STD_VER >= 17

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_PSTL_H
