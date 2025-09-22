//===-- crash_handler.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// This file contains interface functions that can be called by an in-process or
// out-of-process crash handler after the process has terminated. Functions in
// this interface are never thread safe. For an in-process crash handler, the
// handler should call GuardedPoolAllocator::disable() to stop any other threads
// from retrieving new GWP-ASan allocations, which may corrupt the metadata.
#ifndef GWP_ASAN_INTERFACE_H_
#define GWP_ASAN_INTERFACE_H_

#include "gwp_asan/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// When a process crashes, there are three possible outcomes:
//  1. The crash is unrelated to GWP-ASan - in which case this function returns
//     false.
//  2. The crash is internally detected within GWP-ASan itself (e.g. a
//     double-free bug is caught in GuardedPoolAllocator::deallocate(), and
//     GWP-ASan will terminate the process). In this case - this function
//     returns true.
//  3. The crash is caused by a memory error at `AccessPtr` that's caught by the
//     system, but GWP-ASan is responsible for the allocation. In this case -
//     the function also returns true.
// This function takes an optional `AccessPtr` parameter. If the pointer that
// was attempted to be accessed is available, you should provide it here. In the
// case of some internally-detected errors, the crash may manifest as an abort
// or trap may or may not have an associated pointer. In these cases, the
// pointer can be obtained by a call to __gwp_asan_get_internal_crash_address.
bool __gwp_asan_error_is_mine(const gwp_asan::AllocatorState *State,
                              uintptr_t ErrorPtr = 0u);

// Diagnose and return the type of error that occurred at `ErrorPtr`. If
// `ErrorPtr` is unrelated to GWP-ASan, or if the error type cannot be deduced,
// this function returns Error::UNKNOWN.
gwp_asan::Error
__gwp_asan_diagnose_error(const gwp_asan::AllocatorState *State,
                          const gwp_asan::AllocationMetadata *Metadata,
                          uintptr_t ErrorPtr);

// This function, provided the fault address from the signal handler, returns
// the following values:
//  1. If the crash was caused by an internally-detected error (invalid free,
//     double free), this function returns the pointer that was used for the
//     internally-detected bad operation (i.e. the pointer given to free()).
//  2. For externally-detected crashes (use-after-free, buffer-overflow), this
//     function returns zero.
//  3. If GWP-ASan wasn't responsible for the crash at all, this function also
//     returns zero.
uintptr_t
__gwp_asan_get_internal_crash_address(const gwp_asan::AllocatorState *State,
                                      uintptr_t ErrorPtr);

// Returns a pointer to the metadata for the allocation that's responsible for
// the crash. This metadata should not be dereferenced directly due to API
// compatibility issues, but should be instead passed to functions below for
// information retrieval. Returns nullptr if there is no metadata available for
// this crash.
const gwp_asan::AllocationMetadata *
__gwp_asan_get_metadata(const gwp_asan::AllocatorState *State,
                        const gwp_asan::AllocationMetadata *Metadata,
                        uintptr_t ErrorPtr);

// +---------------------------------------------------------------------------+
// | Error Information Functions                                               |
// +---------------------------------------------------------------------------+
// Functions below return information about the type of error that was caught by
// GWP-ASan, or information about the allocation that caused the error. These
// functions generally take an `AllocationMeta` argument, which should be
// retrieved via. __gwp_asan_get_metadata.

// Returns the start of the allocation whose metadata is in `AllocationMeta`.
uintptr_t __gwp_asan_get_allocation_address(
    const gwp_asan::AllocationMetadata *AllocationMeta);

// Returns the size of the allocation whose metadata is in `AllocationMeta`
size_t __gwp_asan_get_allocation_size(
    const gwp_asan::AllocationMetadata *AllocationMeta);

// Returns the Thread ID that allocated the memory that caused the error at
// `ErrorPtr`. This function may not be called if __gwp_asan_has_metadata()
// returns false.
uint64_t __gwp_asan_get_allocation_thread_id(
    const gwp_asan::AllocationMetadata *AllocationMeta);

// Retrieve the allocation trace for the allocation whose metadata is in
// `AllocationMeta`, and place it into the provided `Buffer` that has at least
// `BufferLen` elements. This function returns the number of frames that would
// have been written into `Buffer` if the space was available (i.e. however many
// frames were stored by GWP-ASan). A return value greater than `BufferLen`
// indicates that the trace was truncated when storing to `Buffer`.
size_t __gwp_asan_get_allocation_trace(
    const gwp_asan::AllocationMetadata *AllocationMeta, uintptr_t *Buffer,
    size_t BufferLen);

// Returns whether the allocation whose metadata is in `AllocationMeta` has been
// deallocated. This function may not be called if __gwp_asan_has_metadata()
// returns false.
bool __gwp_asan_is_deallocated(
    const gwp_asan::AllocationMetadata *AllocationMeta);

// Returns the Thread ID that deallocated the memory whose metadata is in
// `AllocationMeta`. This function may not be called if
// __gwp_asan_is_deallocated() returns false.
uint64_t __gwp_asan_get_deallocation_thread_id(
    const gwp_asan::AllocationMetadata *AllocationMeta);

// Retrieve the deallocation trace for the allocation whose metadata is in
// `AllocationMeta`, and place it into the provided `Buffer` that has at least
// `BufferLen` elements. This function returns the number of frames that would
// have been written into `Buffer` if the space was available (i.e. however many
// frames were stored by GWP-ASan). A return value greater than `BufferLen`
// indicates that the trace was truncated when storing to `Buffer`. This
// function may not be called if __gwp_asan_is_deallocated() returns false.
size_t __gwp_asan_get_deallocation_trace(
    const gwp_asan::AllocationMetadata *AllocationMeta, uintptr_t *Buffer,
    size_t BufferLen);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GWP_ASAN_INTERFACE_H_
