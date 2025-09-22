//===-- ubsan_init_standalone_preinit.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Initialization of standalone UBSan runtime.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#if !CAN_SANITIZE_UB
#error "UBSan is not supported on this platform!"
#endif

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "ubsan_init.h"
#include "ubsan_signals_standalone.h"

#if SANITIZER_CAN_USE_PREINIT_ARRAY

namespace __ubsan {

static void PreInitAsStandalone() {
  InitAsStandalone();
  InitializeDeadlySignals();
}

} // namespace __ubsan

__attribute__((section(".preinit_array"), used)) static auto preinit =
    __ubsan::PreInitAsStandalone;
#endif // SANITIZER_CAN_USE_PREINIT_ARRAY
