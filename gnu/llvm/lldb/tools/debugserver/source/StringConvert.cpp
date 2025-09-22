//===-- StringConvert.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include "StringConvert.h"

namespace StringConvert {

int64_t ToSInt64(const char *s, int64_t fail_value, int base,
                 bool *success_ptr) {
  if (s && s[0]) {
    char *end = nullptr;
    int64_t uval = ::strtoll(s, &end, base);
    if (*end == '\0') {
      if (success_ptr)
        *success_ptr = true;
      return uval; // All characters were used, return the result
    }
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

uint64_t ToUInt64(const char *s, uint64_t fail_value, int base,
                  bool *success_ptr) {
  if (s && s[0]) {
    char *end = nullptr;
    uint64_t uval = ::strtoull(s, &end, base);
    if (*end == '\0') {
      if (success_ptr)
        *success_ptr = true;
      return uval; // All characters were used, return the result
    }
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

double ToDouble(const char *s, double fail_value, bool *success_ptr) {
  if (s && s[0]) {
    char *end = nullptr;
    double val = strtod(s, &end);
    if (*end == '\0') {
      if (success_ptr)
        *success_ptr = true;
      return val; // All characters were used, return the result
    }
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

} // namespace StringConvert
