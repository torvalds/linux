//===-- wrappers_c.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

// Skip this compilation unit if compiled as part of Bionic.
#if !SCUDO_ANDROID || !_BIONIC

#include "allocator_config.h"
#include "internal_defs.h"
#include "platform.h"
#include "scudo/interface.h"
#include "wrappers_c.h"
#include "wrappers_c_checks.h"

#include <stdint.h>
#include <stdio.h>

#define SCUDO_PREFIX(name) name
#define SCUDO_ALLOCATOR Allocator

// Export the static allocator so that the C++ wrappers can access it.
// Technically we could have a completely separated heap for C & C++ but in
// reality the amount of cross pollination between the two is staggering.
SCUDO_REQUIRE_CONSTANT_INITIALIZATION
scudo::Allocator<scudo::Config, SCUDO_PREFIX(malloc_postinit)> SCUDO_ALLOCATOR;

#include "wrappers_c.inc"

#undef SCUDO_ALLOCATOR
#undef SCUDO_PREFIX

extern "C" INTERFACE void __scudo_print_stats(void) { Allocator.printStats(); }

#endif // !SCUDO_ANDROID || !_BIONIC
