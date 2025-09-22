//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_COPY_MOVE_COMMON_H
#define _LIBCPP___ALGORITHM_COPY_MOVE_COMMON_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/unwrap_iter.h>
#include <__algorithm/unwrap_range.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__memory/pointer_traits.h>
#include <__string/constexpr_c_functions.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_always_bitcastable.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_trivially_assignable.h>
#include <__type_traits/is_volatile.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// Type traits.

template <class _From, class _To>
struct __can_lower_copy_assignment_to_memmove {
  static const bool value =
      // If the types are always bitcastable, it's valid to do a bitwise copy between them.
      __is_always_bitcastable<_From, _To>::value &&
      // Reject conversions that wouldn't be performed by the regular built-in assignment (e.g. between arrays).
      is_trivially_assignable<_To&, const _From&>::value &&
      // `memmove` doesn't accept `volatile` pointers, make sure the optimization SFINAEs away in that case.
      !is_volatile<_From>::value && !is_volatile<_To>::value;
};

template <class _From, class _To>
struct __can_lower_move_assignment_to_memmove {
  static const bool value =
      __is_always_bitcastable<_From, _To>::value && is_trivially_assignable<_To&, _From&&>::value &&
      !is_volatile<_From>::value && !is_volatile<_To>::value;
};

// `memmove` algorithms implementation.

template <class _In, class _Out>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_In*, _Out*>
__copy_trivial_impl(_In* __first, _In* __last, _Out* __result) {
  const size_t __n = static_cast<size_t>(__last - __first);

  std::__constexpr_memmove(__result, __first, __element_count(__n));

  return std::make_pair(__last, __result + __n);
}

template <class _In, class _Out>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_In*, _Out*>
__copy_backward_trivial_impl(_In* __first, _In* __last, _Out* __result) {
  const size_t __n = static_cast<size_t>(__last - __first);
  __result -= __n;

  std::__constexpr_memmove(__result, __first, __element_count(__n));

  return std::make_pair(__last, __result);
}

// Iterator unwrapping and dispatching to the correct overload.

template <class _InIter, class _OutIter>
struct __can_rewrap
    : integral_constant<bool, is_copy_constructible<_InIter>::value && is_copy_constructible<_OutIter>::value> {};

template <class _Algorithm,
          class _InIter,
          class _Sent,
          class _OutIter,
          __enable_if_t<__can_rewrap<_InIter, _OutIter>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 pair<_InIter, _OutIter>
__copy_move_unwrap_iters(_InIter __first, _Sent __last, _OutIter __out_first) {
  auto __range  = std::__unwrap_range(__first, std::move(__last));
  auto __result = _Algorithm()(std::move(__range.first), std::move(__range.second), std::__unwrap_iter(__out_first));
  return std::make_pair(std::__rewrap_range<_Sent>(std::move(__first), std::move(__result.first)),
                        std::__rewrap_iter(std::move(__out_first), std::move(__result.second)));
}

template <class _Algorithm,
          class _InIter,
          class _Sent,
          class _OutIter,
          __enable_if_t<!__can_rewrap<_InIter, _OutIter>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 pair<_InIter, _OutIter>
__copy_move_unwrap_iters(_InIter __first, _Sent __last, _OutIter __out_first) {
  return _Algorithm()(std::move(__first), std::move(__last), std::move(__out_first));
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_COPY_MOVE_COMMON_H
