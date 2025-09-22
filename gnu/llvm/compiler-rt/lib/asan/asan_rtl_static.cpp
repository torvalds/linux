//===-- asan_static_rtl.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Main file of the ASan run-time library.
//===----------------------------------------------------------------------===//

// This file is empty for now. Main reason to have it is workaround for Windows
// build, which complains because no files are part of the asan_static lib.

#include "sanitizer_common/sanitizer_common.h"

#define REPORT_FUNCTION(Name)                                       \
  extern "C" SANITIZER_WEAK_ATTRIBUTE void Name(__asan::uptr addr); \
  extern "C" void Name##_asm(uptr addr) { Name(addr); }

namespace __asan {

REPORT_FUNCTION(__asan_report_load1)
REPORT_FUNCTION(__asan_report_load2)
REPORT_FUNCTION(__asan_report_load4)
REPORT_FUNCTION(__asan_report_load8)
REPORT_FUNCTION(__asan_report_load16)
REPORT_FUNCTION(__asan_report_store1)
REPORT_FUNCTION(__asan_report_store2)
REPORT_FUNCTION(__asan_report_store4)
REPORT_FUNCTION(__asan_report_store8)
REPORT_FUNCTION(__asan_report_store16)

}  // namespace __asan
