//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___NUMERIC_PSTL_H
#define _LIBCPP___NUMERIC_PSTL_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if !defined(_LIBCPP_HAS_NO_INCOMPLETE_PSTL) && _LIBCPP_STD_VER >= 17

#  include <__functional/identity.h>
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
          class _Tp,
          class _BinaryOperation,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _Tp reduce(
    _ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Tp __init, _BinaryOperation __op) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "reduce requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__reduce, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first),
      std::move(__last),
      std::move(__init),
      std::move(__op));
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _Tp
reduce(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last, _Tp __init) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "reduce requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__reduce, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy), std::move(__first), std::move(__last), std::move(__init), plus{});
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI __iter_value_type<_ForwardIterator>
reduce(_ExecutionPolicy&& __policy, _ForwardIterator __first, _ForwardIterator __last) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "reduce requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__reduce, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first),
      std::move(__last),
      __iter_value_type<_ForwardIterator>(),
      plus{});
}

template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _Tp,
          class _BinaryOperation1,
          class _BinaryOperation2,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _Tp transform_reduce(
    _ExecutionPolicy&& __policy,
    _ForwardIterator1 __first1,
    _ForwardIterator1 __last1,
    _ForwardIterator2 __first2,
    _Tp __init,
    _BinaryOperation1 __reduce,
    _BinaryOperation2 __transform) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "transform_reduce requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "transform_reduce requires ForwardIterators");
  using _Implementation =
      __pstl::__dispatch<__pstl::__transform_reduce_binary, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__init),
      std::move(__reduce),
      std::move(__transform));
}

// This overload doesn't get a customization point because it's trivial to detect (through e.g.
// __desugars_to_v) when specializing the more general variant, which should always be preferred
template <class _ExecutionPolicy,
          class _ForwardIterator1,
          class _ForwardIterator2,
          class _Tp,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _Tp transform_reduce(
    _ExecutionPolicy&& __policy,
    _ForwardIterator1 __first1,
    _ForwardIterator1 __last1,
    _ForwardIterator2 __first2,
    _Tp __init) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator1, "transform_reduce requires ForwardIterators");
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator2, "transform_reduce requires ForwardIterators");
  using _Implementation =
      __pstl::__dispatch<__pstl::__transform_reduce_binary, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__init),
      plus{},
      multiplies{});
}

template <class _ExecutionPolicy,
          class _ForwardIterator,
          class _Tp,
          class _BinaryOperation,
          class _UnaryOperation,
          class _RawPolicy                                    = __remove_cvref_t<_ExecutionPolicy>,
          enable_if_t<is_execution_policy_v<_RawPolicy>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _Tp transform_reduce(
    _ExecutionPolicy&& __policy,
    _ForwardIterator __first,
    _ForwardIterator __last,
    _Tp __init,
    _BinaryOperation __reduce,
    _UnaryOperation __transform) {
  _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(_ForwardIterator, "transform_reduce requires ForwardIterators");
  using _Implementation = __pstl::__dispatch<__pstl::__transform_reduce, __pstl::__current_configuration, _RawPolicy>;
  return __pstl::__handle_exception<_Implementation>(
      std::forward<_ExecutionPolicy>(__policy),
      std::move(__first),
      std::move(__last),
      std::move(__init),
      std::move(__reduce),
      std::move(__transform));
}

_LIBCPP_END_NAMESPACE_STD

#endif // !defined(_LIBCPP_HAS_NO_INCOMPLETE_PSTL) && _LIBCPP_STD_VER >= 17

_LIBCPP_POP_MACROS

#endif // _LIBCPP___NUMERIC_PSTL_H
