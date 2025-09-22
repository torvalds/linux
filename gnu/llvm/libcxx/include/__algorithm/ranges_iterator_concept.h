//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_ITERATOR_CONCEPT_H
#define _LIBCPP___ALGORITHM_RANGES_ITERATOR_CONCEPT_H

#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/remove_cvref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

template <class _IterMaybeQualified>
consteval auto __get_iterator_concept() {
  using _Iter = __remove_cvref_t<_IterMaybeQualified>;

  if constexpr (contiguous_iterator<_Iter>)
    return contiguous_iterator_tag();
  else if constexpr (random_access_iterator<_Iter>)
    return random_access_iterator_tag();
  else if constexpr (bidirectional_iterator<_Iter>)
    return bidirectional_iterator_tag();
  else if constexpr (forward_iterator<_Iter>)
    return forward_iterator_tag();
  else if constexpr (input_iterator<_Iter>)
    return input_iterator_tag();
}

template <class _Iter>
using __iterator_concept = decltype(__get_iterator_concept<_Iter>());

} // namespace ranges
_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_RANGES_ITERATOR_CONCEPT_H
