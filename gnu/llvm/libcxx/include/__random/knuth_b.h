//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_KNUTH_B_H
#define _LIBCPP___RANDOM_KNUTH_B_H

#include <__config>
#include <__random/linear_congruential_engine.h>
#include <__random/shuffle_order_engine.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

typedef shuffle_order_engine<minstd_rand0, 256> knuth_b;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANDOM_KNUTH_B_H
