//===-- wrappers_c_bionic.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

// This is only used when compiled as part of Bionic.
#if SCUDO_ANDROID && _BIONIC

#include "allocator_config.h"
#include "internal_defs.h"
#include "platform.h"
#include "scudo/interface.h"
#include "wrappers_c.h"
#include "wrappers_c_checks.h"

#include <stdint.h>
#include <stdio.h>

// Regular MallocDispatch definitions.
#define SCUDO_PREFIX(name) CONCATENATE(scudo_, name)
#define SCUDO_ALLOCATOR Allocator

extern "C" void SCUDO_PREFIX(malloc_postinit)();
SCUDO_REQUIRE_CONSTANT_INITIALIZATION
static scudo::Allocator<scudo::Config, SCUDO_PREFIX(malloc_postinit)>
    SCUDO_ALLOCATOR;

#include "wrappers_c.inc"

#undef SCUDO_ALLOCATOR
#undef SCUDO_PREFIX

// TODO(kostyak): support both allocators.
INTERFACE void __scudo_print_stats(void) { Allocator.printStats(); }

INTERFACE void __scudo_get_error_info(
    struct scudo_error_info *error_info, uintptr_t fault_addr,
    const char *stack_depot, size_t stack_depot_size, const char *region_info,
    const char *ring_buffer, size_t ring_buffer_size, const char *memory,
    const char *memory_tags, uintptr_t memory_addr, size_t memory_size) {
  Allocator.getErrorInfo(error_info, fault_addr, stack_depot, stack_depot_size,
                         region_info, ring_buffer, ring_buffer_size, memory,
                         memory_tags, memory_addr, memory_size);
}

INTERFACE const char *__scudo_get_stack_depot_addr() {
  return Allocator.getStackDepotAddress();
}

INTERFACE size_t __scudo_get_stack_depot_size() {
  return Allocator.getStackDepotSize();
}

INTERFACE const char *__scudo_get_region_info_addr() {
  return Allocator.getRegionInfoArrayAddress();
}

INTERFACE size_t __scudo_get_region_info_size() {
  return Allocator.getRegionInfoArraySize();
}

INTERFACE const char *__scudo_get_ring_buffer_addr() {
  return Allocator.getRingBufferAddress();
}

INTERFACE size_t __scudo_get_ring_buffer_size() {
  return Allocator.getRingBufferSize();
}

#endif // SCUDO_ANDROID && _BIONIC
