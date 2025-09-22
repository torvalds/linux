//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cxxabi.h"
#include "abort_message.h"

namespace __cxxabiv1 {
extern "C" {
_LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN
void __cxa_pure_virtual(void) {
  abort_message("Pure virtual function called!");
}

_LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN
void __cxa_deleted_virtual(void) {
  abort_message("Deleted virtual function called!");
}
} // extern "C"
} // abi
