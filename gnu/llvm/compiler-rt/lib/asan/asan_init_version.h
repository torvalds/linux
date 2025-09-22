//===-- asan_init_version.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This header defines a versioned __asan_init function to be called at the
// startup of the instrumented program.
//===----------------------------------------------------------------------===//
#ifndef ASAN_INIT_VERSION_H
#define ASAN_INIT_VERSION_H

#include "sanitizer_common/sanitizer_platform.h"

extern "C" {
  // Every time the ASan ABI changes we also change the version number in the
  // __asan_init function name.  Objects built with incompatible ASan ABI
  // versions will not link with run-time.
  //
  // Changes between ABI versions:
  // v1=>v2: added 'module_name' to __asan_global
  // v2=>v3: stack frame description (created by the compiler)
  //         contains the function PC as the 3rd field (see
  //         DescribeAddressIfStack)
  // v3=>v4: added '__asan_global_source_location' to __asan_global
  // v4=>v5: changed the semantics and format of __asan_stack_malloc_ and
  //         __asan_stack_free_ functions
  // v5=>v6: changed the name of the version check symbol
  // v6=>v7: added 'odr_indicator' to __asan_global
  // v7=>v8: added '__asan_(un)register_image_globals' functions for dead
  //         stripping support on Mach-O platforms
#if SANITIZER_WORDSIZE == 32 && SANITIZER_ANDROID
  // v8=>v9: 32-bit Android switched to dynamic shadow
  #define __asan_version_mismatch_check __asan_version_mismatch_check_v9
#else
  #define __asan_version_mismatch_check __asan_version_mismatch_check_v8
#endif
}

#endif  // ASAN_INIT_VERSION_H
