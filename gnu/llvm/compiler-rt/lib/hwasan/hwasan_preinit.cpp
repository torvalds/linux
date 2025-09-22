//===-- hwasan_preinit.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer, an address sanity checker.
//
// Call __hwasan_init at the very early stage of process startup.
//===----------------------------------------------------------------------===//
#include "hwasan_interface_internal.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#if SANITIZER_CAN_USE_PREINIT_ARRAY
// This section is linked into the main executable when -fsanitize=hwaddress is
// specified to perform initialization at a very early stage.
__attribute__((section(".preinit_array"), used)) static auto preinit =
    __hwasan_init;
#endif
