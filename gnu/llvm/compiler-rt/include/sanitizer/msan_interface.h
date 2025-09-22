//===-- msan_interface.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Public interface header.
//===----------------------------------------------------------------------===//
#ifndef MSAN_INTERFACE_H
#define MSAN_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Set raw origin for the memory range. */
void SANITIZER_CDECL __msan_set_origin(const volatile void *a, size_t size,
                                       uint32_t origin);

/* Get raw origin for an address. */
uint32_t SANITIZER_CDECL __msan_get_origin(const volatile void *a);

/* Test that this_id is a descendant of prev_id (or they are simply equal).
 * "descendant" here means they are part of the same chain, created with
 * __msan_chain_origin. */
int SANITIZER_CDECL __msan_origin_is_descendant_or_same(uint32_t this_id,
                                                        uint32_t prev_id);

/* Returns non-zero if tracking origins. */
int SANITIZER_CDECL __msan_get_track_origins(void);

/* Returns the origin id of the latest UMR in the calling thread. */
uint32_t SANITIZER_CDECL __msan_get_umr_origin(void);

/* Make memory region fully initialized (without changing its contents). */
void SANITIZER_CDECL __msan_unpoison(const volatile void *a, size_t size);

/* Make a null-terminated string fully initialized (without changing its
   contents). */
void SANITIZER_CDECL __msan_unpoison_string(const volatile char *a);

/* Make first n parameters of the next function call fully initialized. */
void SANITIZER_CDECL __msan_unpoison_param(size_t n);

/* Make memory region fully uninitialized (without changing its contents).
   This is a legacy interface that does not update origin information. Use
   __msan_allocated_memory() instead. */
void SANITIZER_CDECL __msan_poison(const volatile void *a, size_t size);

/* Make memory region partially uninitialized (without changing its contents).
 */
void SANITIZER_CDECL __msan_partial_poison(const volatile void *data,
                                           void *shadow, size_t size);

/* Returns the offset of the first (at least partially) poisoned byte in the
   memory range, or -1 if the whole range is good. */
intptr_t SANITIZER_CDECL __msan_test_shadow(const volatile void *x,
                                            size_t size);

/* Checks that memory range is fully initialized, and reports an error if it
 * is not. */
void SANITIZER_CDECL __msan_check_mem_is_initialized(const volatile void *x,
                                                     size_t size);

/* For testing:
   __msan_set_expect_umr(1);
   ... some buggy code ...
   __msan_set_expect_umr(0);
   The last line will verify that a UMR happened. */
void SANITIZER_CDECL __msan_set_expect_umr(int expect_umr);

/* Change the value of keep_going flag. Non-zero value means don't terminate
   program execution when an error is detected. This will not affect error in
   modules that were compiled without the corresponding compiler flag. */
void SANITIZER_CDECL __msan_set_keep_going(int keep_going);

/* Print shadow and origin for the memory range to stderr in a human-readable
   format. */
void SANITIZER_CDECL __msan_print_shadow(const volatile void *x, size_t size);

/* Print shadow for the memory range to stderr in a minimalistic
   human-readable format. */
void SANITIZER_CDECL __msan_dump_shadow(const volatile void *x, size_t size);

/* Returns true if running under a dynamic tool (DynamoRio-based). */
int SANITIZER_CDECL __msan_has_dynamic_component(void);

/* Tell MSan about newly allocated memory (ex.: custom allocator).
   Memory will be marked uninitialized, with origin at the call site. */
void SANITIZER_CDECL __msan_allocated_memory(const volatile void *data,
                                             size_t size);

/* Tell MSan about newly destroyed memory. Mark memory as uninitialized. */
void SANITIZER_CDECL __sanitizer_dtor_callback(const volatile void *data,
                                               size_t size);
void SANITIZER_CDECL __sanitizer_dtor_callback_fields(const volatile void *data,
                                                      size_t size);
void SANITIZER_CDECL __sanitizer_dtor_callback_vptr(const volatile void *data);

/* This function may be optionally provided by user and should return
   a string containing Msan runtime options. See msan_flags.h for details. */
const char *SANITIZER_CDECL __msan_default_options(void);

/* Deprecated. Call __sanitizer_set_death_callback instead. */
void SANITIZER_CDECL
__msan_set_death_callback(void(SANITIZER_CDECL *callback)(void));

/* Update shadow for the application copy of size bytes from src to dst.
   Src and dst are application addresses. This function does not copy the
   actual application memory, it only updates shadow and origin for such
   copy. Source and destination regions can overlap. */
void SANITIZER_CDECL __msan_copy_shadow(const volatile void *dst,
                                        const volatile void *src, size_t size);

/* Disables uninitialized memory checks in interceptors. */
void SANITIZER_CDECL __msan_scoped_disable_interceptor_checks(void);

/* Re-enables uninitialized memory checks in interceptors after a previous
   call to __msan_scoped_disable_interceptor_checks. */
void SANITIZER_CDECL __msan_scoped_enable_interceptor_checks(void);

void SANITIZER_CDECL __msan_start_switch_fiber(const void *bottom, size_t size);
void SANITIZER_CDECL __msan_finish_switch_fiber(const void **bottom_old,
                                                size_t *size_old);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
