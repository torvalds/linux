// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_DEFAULT_SEARCHER_H
#define _LIBCPP___FUNCTIONAL_DEFAULT_SEARCHER_H

#include <__algorithm/search.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/operations.h>
#include <__iterator/iterator_traits.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

// default searcher
template <class _ForwardIterator, class _BinaryPredicate = equal_to<>>
class _LIBCPP_TEMPLATE_VIS default_searcher {
public:
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20
  default_searcher(_ForwardIterator __f, _ForwardIterator __l, _BinaryPredicate __p = _BinaryPredicate())
      : __first_(__f), __last_(__l), __pred_(__p) {}

  template <typename _ForwardIterator2>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_ForwardIterator2, _ForwardIterator2>
  operator()(_ForwardIterator2 __f, _ForwardIterator2 __l) const {
    auto __proj = __identity();
    return std::__search_impl(__f, __l, __first_, __last_, __pred_, __proj, __proj);
  }

private:
  _ForwardIterator __first_;
  _ForwardIterator __last_;
  _BinaryPredicate __pred_;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(default_searcher);

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_DEFAULT_SEARCHER_H
