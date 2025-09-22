// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___MDSPAN_DEFAULT_ACCESSOR_H
#define _LIBCPP___MDSPAN_DEFAULT_ACCESSOR_H

#include <__config>
#include <__type_traits/is_abstract.h>
#include <__type_traits/is_array.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/remove_const.h>
#include <cinttypes>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class _ElementType>
struct default_accessor {
  static_assert(!is_array_v<_ElementType>, "default_accessor: template argument may not be an array type");
  static_assert(!is_abstract_v<_ElementType>, "default_accessor: template argument may not be an abstract class");

  using offset_policy    = default_accessor;
  using element_type     = _ElementType;
  using reference        = _ElementType&;
  using data_handle_type = _ElementType*;

  _LIBCPP_HIDE_FROM_ABI constexpr default_accessor() noexcept = default;
  template <class _OtherElementType>
    requires(is_convertible_v<_OtherElementType (*)[], element_type (*)[]>)
  _LIBCPP_HIDE_FROM_ABI constexpr default_accessor(default_accessor<_OtherElementType>) noexcept {}

  _LIBCPP_HIDE_FROM_ABI constexpr reference access(data_handle_type __p, size_t __i) const noexcept { return __p[__i]; }
  _LIBCPP_HIDE_FROM_ABI constexpr data_handle_type offset(data_handle_type __p, size_t __i) const noexcept {
    return __p + __i;
  }
};

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MDSPAN_DEFAULT_ACCESSOR_H
