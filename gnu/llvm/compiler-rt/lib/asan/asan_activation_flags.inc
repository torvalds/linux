//===-- asan_activation_flags.inc -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A subset of ASan (and common) runtime flags supported at activation time.
//
//===----------------------------------------------------------------------===//
#ifndef ASAN_ACTIVATION_FLAG
# error "Define ASAN_ACTIVATION_FLAG prior to including this file!"
#endif

#ifndef COMMON_ACTIVATION_FLAG
# error "Define COMMON_ACTIVATION_FLAG prior to including this file!"
#endif

// ASAN_ACTIVATION_FLAG(Type, Name)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

ASAN_ACTIVATION_FLAG(int, redzone)
ASAN_ACTIVATION_FLAG(int, max_redzone)
ASAN_ACTIVATION_FLAG(int, quarantine_size_mb)
ASAN_ACTIVATION_FLAG(int, thread_local_quarantine_size_kb)
ASAN_ACTIVATION_FLAG(bool, alloc_dealloc_mismatch)
ASAN_ACTIVATION_FLAG(bool, poison_heap)

COMMON_ACTIVATION_FLAG(bool, allocator_may_return_null)
COMMON_ACTIVATION_FLAG(int, malloc_context_size)
COMMON_ACTIVATION_FLAG(bool, coverage)
COMMON_ACTIVATION_FLAG(const char *, coverage_dir)
COMMON_ACTIVATION_FLAG(int, verbosity)
COMMON_ACTIVATION_FLAG(bool, help)
COMMON_ACTIVATION_FLAG(s32, allocator_release_to_os_interval_ms)
