// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_FRONT_INSERT_ITERATOR_H
#define _LIBCPP___ITERATOR_FRONT_INSERT_ITERATOR_H

#include <__config>
#include <__iterator/iterator.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__utility/move.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_SUPPRESS_DEPRECATED_PUSH
template <class _Container>
class _LIBCPP_TEMPLATE_VIS front_insert_iterator
#if _LIBCPP_STD_VER <= 14 || !defined(_LIBCPP_ABI_NO_ITERATOR_BASES)
    : public iterator<output_iterator_tag, void, void, void, void>
#endif
{
  _LIBCPP_SUPPRESS_DEPRECATED_POP

protected:
  _Container* container;

public:
  typedef output_iterator_tag iterator_category;
  typedef void value_type;
#if _LIBCPP_STD_VER >= 20
  typedef ptrdiff_t difference_type;
#else
  typedef void difference_type;
#endif
  typedef void pointer;
  typedef void reference;
  typedef _Container container_type;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 explicit front_insert_iterator(_Container& __x)
      : container(std::addressof(__x)) {}
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 front_insert_iterator&
  operator=(const typename _Container::value_type& __value) {
    container->push_front(__value);
    return *this;
  }
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 front_insert_iterator&
  operator=(typename _Container::value_type&& __value) {
    container->push_front(std::move(__value));
    return *this;
  }
#endif // _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 front_insert_iterator& operator*() { return *this; }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 front_insert_iterator& operator++() { return *this; }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 front_insert_iterator operator++(int) { return *this; }
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(front_insert_iterator);

template <class _Container>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 front_insert_iterator<_Container>
front_inserter(_Container& __x) {
  return front_insert_iterator<_Container>(__x);
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_FRONT_INSERT_ITERATOR_H
