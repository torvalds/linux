//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_COPY_CVREF_H
#define _LIBCPP___TYPE_TRAITS_COPY_CVREF_H

#include <__config>
#include <__type_traits/add_lvalue_reference.h>
#include <__type_traits/add_rvalue_reference.h>
#include <__type_traits/copy_cv.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _From, class _To>
struct __copy_cvref {
  using type = __copy_cv_t<_From, _To>;
};

template <class _From, class _To>
struct __copy_cvref<_From&, _To> {
  using type = __add_lvalue_reference_t<__copy_cv_t<_From, _To> >;
};

template <class _From, class _To>
struct __copy_cvref<_From&&, _To> {
  using type = __add_rvalue_reference_t<__copy_cv_t<_From, _To> >;
};

template <class _From, class _To>
using __copy_cvref_t = typename __copy_cvref<_From, _To>::type;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_COPY_CVREF_H
