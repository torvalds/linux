//===-- memprof_interface_internal.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// This header declares the MemProfiler runtime interface functions.
// The runtime library has to define these functions so the instrumented program
// could call them.
//
// See also include/sanitizer/memprof_interface.h
//===----------------------------------------------------------------------===//
#ifndef MEMPROF_INTERFACE_INTERNAL_H
#define MEMPROF_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"

#include "memprof_init_version.h"

using __sanitizer::u32;
using __sanitizer::u64;
using __sanitizer::uptr;

extern "C" {
// This function should be called at the very beginning of the process,
// before any instrumented code is executed and before any call to malloc.
SANITIZER_INTERFACE_ATTRIBUTE void __memprof_init();
SANITIZER_INTERFACE_ATTRIBUTE void __memprof_preinit();
SANITIZER_INTERFACE_ATTRIBUTE void __memprof_version_mismatch_check_v1();

SANITIZER_INTERFACE_ATTRIBUTE
void __memprof_record_access(void const volatile *addr);

SANITIZER_INTERFACE_ATTRIBUTE
void __memprof_record_access_range(void const volatile *addr, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE void __memprof_print_accumulated_stats();

SANITIZER_INTERFACE_ATTRIBUTE
const char *__memprof_default_options();

SANITIZER_INTERFACE_ATTRIBUTE
extern uptr __memprof_shadow_memory_dynamic_address;

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE extern char
    __memprof_profile_filename[1];
SANITIZER_INTERFACE_ATTRIBUTE int __memprof_profile_dump();
SANITIZER_INTERFACE_ATTRIBUTE void __memprof_profile_reset();

SANITIZER_INTERFACE_ATTRIBUTE void __memprof_load(uptr p);
SANITIZER_INTERFACE_ATTRIBUTE void __memprof_store(uptr p);

SANITIZER_INTERFACE_ATTRIBUTE
void *__memprof_memcpy(void *dst, const void *src, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void *__memprof_memset(void *s, int c, uptr n);
SANITIZER_INTERFACE_ATTRIBUTE
void *__memprof_memmove(void *dest, const void *src, uptr n);
} // extern "C"

#endif // MEMPROF_INTERFACE_INTERNAL_H
