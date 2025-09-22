//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_MEMORY_RESOURCE_H
#define _LIBCPP___FWD_MEMORY_RESOURCE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace pmr {
template <class _ValueType>
class _LIBCPP_AVAILABILITY_PMR _LIBCPP_TEMPLATE_VIS polymorphic_allocator;
} // namespace pmr

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FWD_MEMORY_RESOURCE_H
