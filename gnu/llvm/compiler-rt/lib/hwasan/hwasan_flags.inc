//===-- hwasan_flags.inc ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hwasan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef HWASAN_FLAG
# error "Define HWASAN_FLAG prior to including this file!"
#endif

// HWASAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

HWASAN_FLAG(bool, verbose_threads, false,
            "inform on thread creation/destruction")
HWASAN_FLAG(bool, tag_in_malloc, true, "")
HWASAN_FLAG(bool, tag_in_free, true, "")
HWASAN_FLAG(bool, print_stats, false, "")
HWASAN_FLAG(bool, halt_on_error, true, "")
HWASAN_FLAG(bool, atexit, false, "")
HWASAN_FLAG(
    bool, print_live_threads_info, true,
    "If set, prints the remaining threads in report as an extra information.")

// Test only flag to disable malloc/realloc/free memory tagging on startup.
// Tagging can be reenabled with __hwasan_enable_allocator_tagging().
HWASAN_FLAG(bool, disable_allocator_tagging, false, "")

// If false, use simple increment of a thread local counter to generate new
// tags.
HWASAN_FLAG(bool, random_tags, true, "")

HWASAN_FLAG(
    int, max_malloc_fill_size, 0,
    "HWASan allocator flag. max_malloc_fill_size is the maximal amount of "
    "bytes that will be filled with malloc_fill_byte on malloc.")

HWASAN_FLAG(bool, free_checks_tail_magic, 1,
    "If set, free() will check the magic values "
    "after the allocated object "
    "if the allocation size is not a divident of the granule size")
HWASAN_FLAG(
    int, max_free_fill_size, 0,
    "HWASan allocator flag. max_free_fill_size is the maximal amount of "
    "bytes that will be filled with free_fill_byte during free.")
HWASAN_FLAG(int, malloc_fill_byte, 0xbe,
          "Value used to fill the newly allocated memory.")
HWASAN_FLAG(int, free_fill_byte, 0x55,
          "Value used to fill deallocated memory.")
HWASAN_FLAG(int, heap_history_size, 1023,
          "The number of heap (de)allocations remembered per thread. "
          "Affects the quality of heap-related reports, but not the ability "
          "to find bugs.")
HWASAN_FLAG(bool, export_memory_stats, true,
            "Export up-to-date memory stats through /proc")
HWASAN_FLAG(int, stack_history_size, 1024,
            "The number of stack frames remembered per thread. "
            "Affects the quality of stack-related reports, but not the ability "
            "to find bugs.")

// Malloc / free bisection. Only tag malloc and free calls when a hash of
// allocation size and stack trace is between malloc_bisect_left and
// malloc_bisect_right (both inclusive). [0, 0] range is special and disables
// bisection (i.e. everything is tagged). Once the range is narrowed down
// enough, use malloc_bisect_dump to see interesting allocations.
HWASAN_FLAG(uptr, malloc_bisect_left, 0,
            "Left bound of malloc bisection, inclusive.")
HWASAN_FLAG(uptr, malloc_bisect_right, 0,
            "Right bound of malloc bisection, inclusive.")
HWASAN_FLAG(bool, malloc_bisect_dump, false,
            "Print all allocations within [malloc_bisect_left, "
            "malloc_bisect_right] range ")


// Exit if we fail to enable the AArch64 kernel ABI relaxation which allows
// tagged pointers in syscalls.  This is the default, but being able to disable
// that behaviour is useful for running the testsuite on more platforms (the
// testsuite can run since we manually ensure any pointer arguments to syscalls
// are untagged before the call.
HWASAN_FLAG(bool, fail_without_syscall_abi, true,
            "Exit if fail to request relaxed syscall ABI.")

HWASAN_FLAG(
    uptr, fixed_shadow_base, -1,
    "If not -1, HWASan will attempt to allocate the shadow at this address, "
    "instead of choosing one dynamically."
    "Tip: this can be combined with the compiler option, "
    "-hwasan-mapping-offset, to optimize the instrumentation.")
