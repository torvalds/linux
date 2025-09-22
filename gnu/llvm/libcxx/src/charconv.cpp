//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <charconv>
#include <string.h>

#include "include/to_chars_floating_point.h"

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_ABI_DO_NOT_EXPORT_TO_CHARS_BASE_10

namespace __itoa {

_LIBCPP_EXPORTED_FROM_ABI char* __u32toa(uint32_t value, char* buffer) noexcept { return __base_10_u32(buffer, value); }

_LIBCPP_EXPORTED_FROM_ABI char* __u64toa(uint64_t value, char* buffer) noexcept { return __base_10_u64(buffer, value); }

} // namespace __itoa

#endif // _LIBCPP_ABI_DO_NOT_EXPORT_TO_CHARS_BASE_10

// The original version of floating-point to_chars was written by Microsoft and
// contributed with the following license.

// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// This implementation is dedicated to the memory of Mary and Thavatchai.

to_chars_result to_chars(char* __first, char* __last, float __value) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Plain>(__first, __last, __value, chars_format{}, 0);
}

to_chars_result to_chars(char* __first, char* __last, double __value) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Plain>(__first, __last, __value, chars_format{}, 0);
}

to_chars_result to_chars(char* __first, char* __last, long double __value) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Plain>(
      __first, __last, static_cast<double>(__value), chars_format{}, 0);
}

to_chars_result to_chars(char* __first, char* __last, float __value, chars_format __fmt) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Format_only>(__first, __last, __value, __fmt, 0);
}

to_chars_result to_chars(char* __first, char* __last, double __value, chars_format __fmt) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Format_only>(__first, __last, __value, __fmt, 0);
}

to_chars_result to_chars(char* __first, char* __last, long double __value, chars_format __fmt) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Format_only>(
      __first, __last, static_cast<double>(__value), __fmt, 0);
}

to_chars_result to_chars(char* __first, char* __last, float __value, chars_format __fmt, int __precision) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Format_precision>(
      __first, __last, __value, __fmt, __precision);
}

to_chars_result to_chars(char* __first, char* __last, double __value, chars_format __fmt, int __precision) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Format_precision>(
      __first, __last, __value, __fmt, __precision);
}

to_chars_result to_chars(char* __first, char* __last, long double __value, chars_format __fmt, int __precision) {
  return _Floating_to_chars<_Floating_to_chars_overload::_Format_precision>(
      __first, __last, static_cast<double>(__value), __fmt, __precision);
}

_LIBCPP_END_NAMESPACE_STD
