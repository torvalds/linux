//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_NAT_H
#define _LIBCPP___TYPE_TRAITS_NAT_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

struct __nat {
#ifndef _LIBCPP_CXX03_LANG
  __nat()                        = delete;
  __nat(const __nat&)            = delete;
  __nat& operator=(const __nat&) = delete;
  ~__nat()                       = delete;
#endif
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_NAT_H
