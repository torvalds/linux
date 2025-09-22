//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__config>

#ifdef _LIBCPP_DEPRECATED_ABI_LEGACY_LIBRARY_DEFINITIONS_FOR_INLINE_FUNCTIONS
#  define _LIBCPP_ERROR_CATEGORY_DEFINE_LEGACY_INLINE_FUNCTIONS
#endif

#include <system_error>

_LIBCPP_BEGIN_NAMESPACE_STD

// class error_category

#if defined(_LIBCPP_ERROR_CATEGORY_DEFINE_LEGACY_INLINE_FUNCTIONS)
error_category::error_category() noexcept {}
#endif

error_category::~error_category() noexcept {}

error_condition error_category::default_error_condition(int ev) const noexcept { return error_condition(ev, *this); }

bool error_category::equivalent(int code, const error_condition& condition) const noexcept {
  return default_error_condition(code) == condition;
}

bool error_category::equivalent(const error_code& code, int condition) const noexcept {
  return *this == code.category() && code.value() == condition;
}

_LIBCPP_END_NAMESPACE_STD
