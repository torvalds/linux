//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_IS_SEED_SEQUENCE_H
#define _LIBCPP___RANDOM_IS_SEED_SEQUENCE_H

#include <__config>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_same.h>
#include <__type_traits/remove_cv.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Sseq, class _Engine>
struct __is_seed_sequence {
  static _LIBCPP_CONSTEXPR const bool value =
      !is_convertible<_Sseq, typename _Engine::result_type>::value && !is_same<__remove_cv_t<_Sseq>, _Engine>::value;
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANDOM_IS_SEED_SEQUENCE_H
