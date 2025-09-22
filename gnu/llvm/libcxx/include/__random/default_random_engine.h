//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANDOM_DEFAULT_RANDOM_ENGINE_H
#define _LIBCPP___RANDOM_DEFAULT_RANDOM_ENGINE_H

#include <__config>
#include <__random/linear_congruential_engine.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

typedef minstd_rand default_random_engine;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANDOM_DEFAULT_RANDOM_ENGINE_H
