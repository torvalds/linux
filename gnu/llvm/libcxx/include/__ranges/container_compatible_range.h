// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_CONTAINER_COMPATIBLE_RANGE_H
#define _LIBCPP___RANGES_CONTAINER_COMPATIBLE_RANGE_H

#include <__concepts/convertible_to.h>
#include <__config>
#include <__ranges/concepts.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class _Range, class _Tp>
concept _ContainerCompatibleRange =
    ranges::input_range<_Range> && convertible_to<ranges::range_reference_t<_Range>, _Tp>;

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_CONTAINER_COMPATIBLE_RANGE_H
