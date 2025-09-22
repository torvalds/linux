//===-- memprof_flags.inc --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MemProf runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef MEMPROF_FLAG
#error "Define MEMPROF_FLAG prior to including this file!"
#endif

// MEMPROF_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

MEMPROF_FLAG(bool, unmap_shadow_on_exit, false,
             "If set, explicitly unmaps the (huge) shadow at exit.")
MEMPROF_FLAG(bool, protect_shadow_gap, true, "If set, mprotect the shadow gap")
MEMPROF_FLAG(bool, print_legend, true, "Print the legend for the shadow bytes.")
MEMPROF_FLAG(bool, atexit, false,
             "If set, prints MemProf exit stats even after program terminates "
             "successfully.")
MEMPROF_FLAG(
    bool, print_full_thread_history, true,
    "If set, prints thread creation stacks for the threads involved in the "
    "report and their ancestors up to the main thread.")

MEMPROF_FLAG(bool, halt_on_error, true,
             "Crash the program after printing the first error report "
             "(WARNING: USE AT YOUR OWN RISK!)")
MEMPROF_FLAG(bool, allocator_frees_and_returns_null_on_realloc_zero, true,
             "realloc(p, 0) is equivalent to free(p) by default (Same as the "
             "POSIX standard). If set to false, realloc(p, 0) will return a "
             "pointer to an allocated space which can not be used.")
MEMPROF_FLAG(bool, print_text, false,
  "If set, prints the heap profile in text format. Else use the raw binary serialization format.")
MEMPROF_FLAG(bool, print_terse, false,
             "If set, prints memory profile in a terse format. Only applicable if print_text = true.")