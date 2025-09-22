//===-- hwasan_interface_internal.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// Private Hwasan interface header.
//===----------------------------------------------------------------------===//

#ifndef HWASAN_INTERFACE_INTERNAL_H
#define HWASAN_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include <link.h>

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_init_static();

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_init();

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_library_loaded(ElfW(Addr) base, const ElfW(Phdr) * phdr,
                             ElfW(Half) phnum);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_library_unloaded(ElfW(Addr) base, const ElfW(Phdr) * phdr,
                               ElfW(Half) phnum);

using __sanitizer::uptr;
using __sanitizer::sptr;
using __sanitizer::uu64;
using __sanitizer::uu32;
using __sanitizer::uu16;
using __sanitizer::u64;
using __sanitizer::u32;
using __sanitizer::u16;
using __sanitizer::u8;

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_init_frames(uptr, uptr);

SANITIZER_INTERFACE_ATTRIBUTE
extern uptr __hwasan_shadow_memory_dynamic_address;

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_loadN(uptr, uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load1(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load2(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load4(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load8(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load16(uptr);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_loadN_noabort(uptr, uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load1_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load2_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load4_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load8_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load16_noabort(uptr);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_loadN_match_all(uptr, uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load1_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load2_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load4_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load8_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load16_match_all(uptr, u8);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_loadN_match_all_noabort(uptr, uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load1_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load2_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load4_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load8_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_load16_match_all_noabort(uptr, u8);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_storeN(uptr, uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store1(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store2(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store4(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store8(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store16(uptr);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_storeN_noabort(uptr, uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store1_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store2_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store4_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store8_noabort(uptr);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store16_noabort(uptr);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_storeN_match_all(uptr, uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store1_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store2_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store4_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store8_match_all(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store16_match_all(uptr, u8);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_storeN_match_all_noabort(uptr, uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store1_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store2_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store4_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store8_match_all_noabort(uptr, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_store16_match_all_noabort(uptr, u8);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_tag_memory(uptr p, u8 tag, uptr sz);

SANITIZER_INTERFACE_ATTRIBUTE
uptr __hwasan_tag_pointer(uptr p, u8 tag);

SANITIZER_INTERFACE_ATTRIBUTE
u8 __hwasan_get_tag_from_pointer(uptr p);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_tag_mismatch(uptr addr, u8 ts);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_tag_mismatch4(uptr addr, uptr access_info, uptr *registers_frame,
                            size_t outsize);

SANITIZER_INTERFACE_ATTRIBUTE
u8 __hwasan_generate_tag();

// Returns the offset of the first tag mismatch or -1 if the whole range is
// good.
SANITIZER_INTERFACE_ATTRIBUTE
sptr __hwasan_test_shadow(const void *x, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
/* OPTIONAL */ const char* __hwasan_default_options();

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_print_shadow(const void *x, uptr size);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_handle_longjmp(const void *sp_dst);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_handle_vfork(const void *sp_dst);

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
void __hwasan_enable_allocator_tagging();

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_disable_allocator_tagging();

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_thread_enter();

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_thread_exit();

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_print_memory_usage();

// The compiler will generate this when
// `-hwasan-record-stack-history-with-calls` is added as a flag, which will add
// frame record information to the stack ring buffer. This is an alternative to
// the compiler emitting instructions in the prologue for doing the same thing
// by accessing the ring buffer directly.
SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_add_frame_record(u64 frame_record_info);

SANITIZER_INTERFACE_ATTRIBUTE
void *__hwasan_memcpy(void *dst, const void *src, uptr size);
SANITIZER_INTERFACE_ATTRIBUTE
void *__hwasan_memset(void *s, int c, uptr n);
SANITIZER_INTERFACE_ATTRIBUTE
void *__hwasan_memmove(void *dest, const void *src, uptr n);

SANITIZER_INTERFACE_ATTRIBUTE
void *__hwasan_memcpy_match_all(void *dst, const void *src, uptr size, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void *__hwasan_memset_match_all(void *s, int c, uptr n, u8);
SANITIZER_INTERFACE_ATTRIBUTE
void *__hwasan_memmove_match_all(void *dest, const void *src, uptr n, u8);

SANITIZER_INTERFACE_ATTRIBUTE
void __hwasan_set_error_report_callback(void (*callback)(const char *));
}  // extern "C"

#endif  // HWASAN_INTERFACE_INTERNAL_H
