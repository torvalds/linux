//===-- sanitizer/hwasan_interface.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// Public interface header.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_HWASAN_INTERFACE_H
#define SANITIZER_HWASAN_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif
// Libc hook for program startup in statically linked executables.
// Initializes enough of the runtime to run instrumented code. This function
// should only be called in statically linked executables because it modifies
// the GOT, which won't work in regular binaries because RELRO will already
// have been applied by the time the function is called. This also means that
// the function should be called before libc applies RELRO.
// Does not call libc unless there is an error.
// Can be called multiple times.
void SANITIZER_CDECL __hwasan_init_static(void);

// This function may be optionally provided by user and should return
// a string containing HWASan runtime options. See asan_flags.h for details.
const char *SANITIZER_CDECL __hwasan_default_options(void);

void SANITIZER_CDECL __hwasan_enable_allocator_tagging(void);
void SANITIZER_CDECL __hwasan_disable_allocator_tagging(void);

// Mark region of memory with the given tag. Both address and size need to be
// 16-byte aligned.
void SANITIZER_CDECL __hwasan_tag_memory(const volatile void *p,
                                         unsigned char tag, size_t size);

/// Set pointer tag. Previous tag is lost.
void *SANITIZER_CDECL __hwasan_tag_pointer(const volatile void *p,
                                           unsigned char tag);

/// Get tag from the pointer.
unsigned char SANITIZER_CDECL
__hwasan_get_tag_from_pointer(const volatile void *p);

// Set memory tag from the current SP address to the given address to zero.
// This is meant to annotate longjmp and other non-local jumps.
// This function needs to know the (almost) exact destination frame address;
// clearing shadow for the entire thread stack like __asan_handle_no_return
// does would cause false reports.
void SANITIZER_CDECL __hwasan_handle_longjmp(const void *sp_dst);

// Set memory tag for the part of the current thread stack below sp_dst to
// zero. Call this in vfork() before returning in the parent process.
void SANITIZER_CDECL __hwasan_handle_vfork(const void *sp_dst);

// Libc hook for thread creation. Should be called in the child thread before
// any instrumented code.
void SANITIZER_CDECL __hwasan_thread_enter();

// Libc hook for thread destruction. No instrumented code should run after
// this call.
void SANITIZER_CDECL __hwasan_thread_exit();

// Print shadow and origin for the memory range to stderr in a human-readable
// format.
void SANITIZER_CDECL __hwasan_print_shadow(const volatile void *x, size_t size);

// Print one-line report about the memory usage of the current process.
void SANITIZER_CDECL __hwasan_print_memory_usage();

/* Returns the offset of the first byte in the memory range that can not be
 * accessed through the pointer in x, or -1 if the whole range is good. */
intptr_t SANITIZER_CDECL __hwasan_test_shadow(const volatile void *x,
                                              size_t size);

/* Sets the callback function to be called during HWASan error reporting. */
void SANITIZER_CDECL
__hwasan_set_error_report_callback(void (*callback)(const char *));

int SANITIZER_CDECL __sanitizer_posix_memalign(void **memptr, size_t alignment,
                                               size_t size);
void *SANITIZER_CDECL __sanitizer_memalign(size_t alignment, size_t size);
void *SANITIZER_CDECL __sanitizer_aligned_alloc(size_t alignment, size_t size);
void *SANITIZER_CDECL __sanitizer___libc_memalign(size_t alignment,
                                                  size_t size);
void *SANITIZER_CDECL __sanitizer_valloc(size_t size);
void *SANITIZER_CDECL __sanitizer_pvalloc(size_t size);
void SANITIZER_CDECL __sanitizer_free(void *ptr);
void SANITIZER_CDECL __sanitizer_cfree(void *ptr);
size_t SANITIZER_CDECL __sanitizer_malloc_usable_size(const void *ptr);
struct mallinfo SANITIZER_CDECL __sanitizer_mallinfo();
int SANITIZER_CDECL __sanitizer_mallopt(int cmd, int value);
void SANITIZER_CDECL __sanitizer_malloc_stats(void);
void *SANITIZER_CDECL __sanitizer_calloc(size_t nmemb, size_t size);
void *SANITIZER_CDECL __sanitizer_realloc(void *ptr, size_t size);
void *SANITIZER_CDECL __sanitizer_reallocarray(void *ptr, size_t nmemb,
                                               size_t size);
void *SANITIZER_CDECL __sanitizer_malloc(size_t size);
#ifdef __cplusplus
} // extern "C"
#endif

#endif // SANITIZER_HWASAN_INTERFACE_H
