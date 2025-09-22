//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <functional>

_LIBCPP_BEGIN_NAMESPACE_STD

bad_function_call::~bad_function_call() noexcept {}

#ifdef _LIBCPP_ABI_BAD_FUNCTION_CALL_GOOD_WHAT_MESSAGE
const char* bad_function_call::what() const noexcept { return "std::bad_function_call"; }
#endif

_LIBCPP_END_NAMESPACE_STD
