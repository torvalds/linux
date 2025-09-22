//===-- cpu_model/aarch64.h --------------------------------------------- -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cpu_model.h"

#if !defined(__aarch64__)
#error This file is intended only for aarch64-based targets
#endif

#if !defined(DISABLE_AARCH64_FMV)

#include "AArch64CPUFeatures.inc"

void __init_cpu_features(void);

#endif // !defined(DISABLE_AARCH64_FMV)
