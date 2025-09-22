//===-- asan_test_config.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
//===----------------------------------------------------------------------===//
#if !defined(INCLUDED_FROM_ASAN_TEST_UTILS_H)
# error "This file should be included into asan_test_utils.h only"
#endif

#ifndef ASAN_TEST_CONFIG_H
#define ASAN_TEST_CONFIG_H

#include <string>

using std::string;

#ifndef ASAN_UAR
# error "please define ASAN_UAR"
#endif

#ifndef ASAN_HAS_EXCEPTIONS
# error "please define ASAN_HAS_EXCEPTIONS"
#endif

#ifndef ASAN_HAS_IGNORELIST
# error "please define ASAN_HAS_IGNORELIST"
#endif

#ifndef ASAN_NEEDS_SEGV
# if defined(_WIN32)
#  define ASAN_NEEDS_SEGV 0
# else
#  define ASAN_NEEDS_SEGV 1
# endif
#endif

#ifndef ASAN_AVOID_EXPENSIVE_TESTS
# define ASAN_AVOID_EXPENSIVE_TESTS 0
#endif

#define ASAN_PCRE_DOTALL ""

#endif  // ASAN_TEST_CONFIG_H
