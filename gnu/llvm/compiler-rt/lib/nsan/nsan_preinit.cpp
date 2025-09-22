//===- nsan_preinit.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Call __nsan_init early using ELF DT_PREINIT_ARRAY.
//
//===----------------------------------------------------------------------===//

#include "nsan/nsan.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#if SANITIZER_CAN_USE_PREINIT_ARRAY

__attribute__((section(".preinit_array"), used)) static auto nsan_preinit =
    __nsan_init;

#endif
