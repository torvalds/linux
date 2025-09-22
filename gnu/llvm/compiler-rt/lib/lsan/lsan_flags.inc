//===-- lsan_flags.inc ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef LSAN_FLAG
# error "Define LSAN_FLAG prior to including this file!"
#endif

// LSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

LSAN_FLAG(bool, report_objects, false,
          "Print addresses of leaked objects after main leak report.")
LSAN_FLAG(
    int, resolution, 0,
    "Aggregate two objects into one leak if this many stack frames match. If "
    "zero, the entire stack trace must match.")
LSAN_FLAG(int, max_leaks, 0, "The number of leaks reported.")

// Flags controlling the root set of reachable memory.
LSAN_FLAG(bool, use_globals, true,
          "Root set: include global variables (.data and .bss)")
LSAN_FLAG(bool, use_stacks, true, "Root set: include thread stacks")
LSAN_FLAG(bool, use_registers, true, "Root set: include thread registers")
LSAN_FLAG(bool, use_tls, true,
          "Root set: include TLS and thread-specific storage")
LSAN_FLAG(bool, use_root_regions, true,
          "Root set: include regions added via __lsan_register_root_region().")
LSAN_FLAG(bool, use_ld_allocations, true,
          "Root set: mark as reachable all allocations made from dynamic "
          "linker. This was the old way to handle dynamic TLS, and will "
          "be removed soon. Do not use this flag.")

LSAN_FLAG(bool, use_unaligned, false, "Consider unaligned pointers valid.")
LSAN_FLAG(bool, use_poisoned, false,
          "Consider pointers found in poisoned memory to be valid.")
LSAN_FLAG(bool, log_pointers, false, "Debug logging")
LSAN_FLAG(bool, log_threads, false, "Debug logging")
LSAN_FLAG(const char *, suppressions, "", "Suppressions file name.")
