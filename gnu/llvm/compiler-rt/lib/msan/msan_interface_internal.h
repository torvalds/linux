//===-- msan_interface_internal.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Private MSan interface header.
//===----------------------------------------------------------------------===//

#ifndef MSAN_INTERFACE_INTERNAL_H
#define MSAN_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"

extern "C" {
// FIXME: document all interface functions.

SANITIZER_INTERFACE_ATTRIBUTE
int __msan_get_track_origins();

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_init();

// Print a warning and maybe return.
// This function can die based on common_flags()->exitcode.
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_warning();

// Print a warning and die.
// Instrumentation inserts calls to this function when building in "fast" mode
// (i.e. -mllvm -msan-keep-going)
SANITIZER_INTERFACE_ATTRIBUTE __attribute__((noreturn))
void __msan_warning_noreturn();

using __sanitizer::uptr;
using __sanitizer::sptr;
using __sanitizer::uu64;
using __sanitizer::uu32;
using __sanitizer::uu16;
using __sanitizer::u64;
using __sanitizer::u32;
using __sanitizer::u16;
using __sanitizer::u8;

// Versions of the above which take Origin as a parameter
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_warning_with_origin(u32 origin);
SANITIZER_INTERFACE_ATTRIBUTE __attribute__((noreturn)) void
__msan_warning_with_origin_noreturn(u32 origin);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_warning_1(u8 s, u32 o);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_warning_2(u16 s, u32 o);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_warning_4(u32 s, u32 o);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_warning_8(u64 s, u32 o);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_store_origin_1(u8 s, void *p, u32 o);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_store_origin_2(u16 s, void *p, u32 o);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_store_origin_4(u32 s, void *p, u32 o);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_maybe_store_origin_8(u64 s, void *p, u32 o);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_unpoison(const void *a, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_unpoison_string(const char *s);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_unpoison_param(uptr n);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_clear_and_unpoison(void *a, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void* __msan_memcpy(void *dst, const void *src, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void* __msan_memset(void *s, int c, uptr n);
SANITIZER_INTERFACE_ATTRIBUTE
void* __msan_memmove(void* dest, const void* src, uptr n);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_poison(const void *a, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_poison_stack(void *a, uptr size);

// Copy size bytes from src to dst and unpoison the result.
// Useful to implement unsafe loads.
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_load_unpoisoned(void *src, uptr size, void *dst);

// Returns the offset of the first (at least partially) poisoned byte,
// or -1 if the whole range is good.
SANITIZER_INTERFACE_ATTRIBUTE
sptr __msan_test_shadow(const void *x, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_check_mem_is_initialized(const void *x, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_origin(const void *a, uptr size, u32 origin);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_alloca_origin(void *a, uptr size, char *descr);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_alloca_origin4(void *a, uptr size, char *descr, uptr pc);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_alloca_origin_with_descr(void *a, uptr size, u32 *id_ptr,
                                         char *descr);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_alloca_origin_no_descr(void *a, uptr size, u32 *id_ptr);
SANITIZER_INTERFACE_ATTRIBUTE
u32 __msan_chain_origin(u32 id);
SANITIZER_INTERFACE_ATTRIBUTE
u32 __msan_get_origin(const void *a);

// Test that this_id is a descendant of prev_id (or they are simply equal).
// "descendant" here means that are part of the same chain, created with
// __msan_chain_origin.
SANITIZER_INTERFACE_ATTRIBUTE
int __msan_origin_is_descendant_or_same(u32 this_id, u32 prev_id);


SANITIZER_INTERFACE_ATTRIBUTE
void __msan_clear_on_return();

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_keep_going(int keep_going);

SANITIZER_INTERFACE_ATTRIBUTE
int __msan_set_poison_in_malloc(int do_poison);

SANITIZER_INTERFACE_ATTRIBUTE
const char *__msan_default_options();

// For testing.
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_expect_umr(int expect_umr);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_print_shadow(const void *x, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_dump_shadow(const void *x, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
int  __msan_has_dynamic_component();

// For testing.
SANITIZER_INTERFACE_ATTRIBUTE
u32 __msan_get_umr_origin();
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_partial_poison(const void* data, void* shadow, uptr size);

// Tell MSan about newly allocated memory (ex.: custom allocator).
// Memory will be marked uninitialized, with origin at the call site.
SANITIZER_INTERFACE_ATTRIBUTE
void __msan_allocated_memory(const void* data, uptr size);

// Tell MSan about newly destroyed memory. Memory will be marked
// uninitialized.
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_dtor_callback(const void* data, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_dtor_callback_fields(const void *data, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_dtor_callback_vptr(const void *data);

SANITIZER_INTERFACE_ATTRIBUTE
u16 __sanitizer_unaligned_load16(const uu16 *p);

SANITIZER_INTERFACE_ATTRIBUTE
u32 __sanitizer_unaligned_load32(const uu32 *p);

SANITIZER_INTERFACE_ATTRIBUTE
u64 __sanitizer_unaligned_load64(const uu64 *p);

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store16(uu16 *p, u16 x);

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store32(uu32 *p, u32 x);

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store64(uu64 *p, u64 x);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_set_death_callback(void (*callback)(void));

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_copy_shadow(void *dst, const void *src, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_scoped_disable_interceptor_checks();

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_scoped_enable_interceptor_checks();

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_start_switch_fiber(const void *bottom, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE
void __msan_finish_switch_fiber(const void **bottom_old, uptr *size_old);
}  // extern "C"

#endif  // MSAN_INTERFACE_INTERNAL_H
