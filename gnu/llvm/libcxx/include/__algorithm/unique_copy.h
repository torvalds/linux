//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_UNIQUE_COPY_H
#define _LIBCPP___ALGORITHM_UNIQUE_COPY_H

#include <__algorithm/comp.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__type_traits/conditional.h>
#include <__type_traits/is_base_of.h>
#include <__type_traits/is_same.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __unique_copy_tags {

struct __reread_from_input_tag {};
struct __reread_from_output_tag {};
struct __read_from_tmp_value_tag {};

} // namespace __unique_copy_tags

template <class _AlgPolicy, class _BinaryPredicate, class _InputIterator, class _Sent, class _OutputIterator>
_LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_HIDE_FROM_ABI pair<_InputIterator, _OutputIterator>
__unique_copy(_InputIterator __first,
              _Sent __last,
              _OutputIterator __result,
              _BinaryPredicate&& __pred,
              __unique_copy_tags::__read_from_tmp_value_tag) {
  if (__first != __last) {
    typename _IterOps<_AlgPolicy>::template __value_type<_InputIterator> __t(*__first);
    *__result = __t;
    ++__result;
    while (++__first != __last) {
      if (!__pred(__t, *__first)) {
        __t       = *__first;
        *__result = __t;
        ++__result;
      }
    }
  }
  return pair<_InputIterator, _OutputIterator>(std::move(__first), std::move(__result));
}

template <class _AlgPolicy, class _BinaryPredicate, class _ForwardIterator, class _Sent, class _OutputIterator>
_LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_HIDE_FROM_ABI pair<_ForwardIterator, _OutputIterator>
__unique_copy(_ForwardIterator __first,
              _Sent __last,
              _OutputIterator __result,
              _BinaryPredicate&& __pred,
              __unique_copy_tags::__reread_from_input_tag) {
  if (__first != __last) {
    _ForwardIterator __i = __first;
    *__result            = *__i;
    ++__result;
    while (++__first != __last) {
      if (!__pred(*__i, *__first)) {
        *__result = *__first;
        ++__result;
        __i = __first;
      }
    }
  }
  return pair<_ForwardIterator, _OutputIterator>(std::move(__first), std::move(__result));
}

template <class _AlgPolicy, class _BinaryPredicate, class _InputIterator, class _Sent, class _InputAndOutputIterator>
_LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_HIDE_FROM_ABI pair<_InputIterator, _InputAndOutputIterator>
__unique_copy(_InputIterator __first,
              _Sent __last,
              _InputAndOutputIterator __result,
              _BinaryPredicate&& __pred,
              __unique_copy_tags::__reread_from_output_tag) {
  if (__first != __last) {
    *__result = *__first;
    while (++__first != __last)
      if (!__pred(*__result, *__first))
        *++__result = *__first;
    ++__result;
  }
  return pair<_InputIterator, _InputAndOutputIterator>(std::move(__first), std::move(__result));
}

template <class _InputIterator, class _OutputIterator, class _BinaryPredicate>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
unique_copy(_InputIterator __first, _InputIterator __last, _OutputIterator __result, _BinaryPredicate __pred) {
  using __algo_tag = __conditional_t<
      is_base_of<forward_iterator_tag, typename iterator_traits<_InputIterator>::iterator_category>::value,
      __unique_copy_tags::__reread_from_input_tag,
      __conditional_t<
          is_base_of<forward_iterator_tag, typename iterator_traits<_OutputIterator>::iterator_category>::value &&
              is_same< typename iterator_traits<_InputIterator>::value_type,
                       typename iterator_traits<_OutputIterator>::value_type>::value,
          __unique_copy_tags::__reread_from_output_tag,
          __unique_copy_tags::__read_from_tmp_value_tag> >;
  return std::__unique_copy<_ClassicAlgPolicy>(
             std::move(__first), std::move(__last), std::move(__result), __pred, __algo_tag())
      .second;
}

template <class _InputIterator, class _OutputIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
unique_copy(_InputIterator __first, _InputIterator __last, _OutputIterator __result) {
  return std::unique_copy(std::move(__first), std::move(__last), std::move(__result), __equal_to());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_UNIQUE_COPY_H
