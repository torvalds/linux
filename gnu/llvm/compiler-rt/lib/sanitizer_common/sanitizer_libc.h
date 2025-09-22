//===-- sanitizer_libc.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
// These tools can not use some of the libc functions directly because those
// functions are intercepted. Instead, we implement a tiny subset of libc here.
// FIXME: Some of functions declared in this file are in fact POSIX, not libc.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_LIBC_H
#define SANITIZER_LIBC_H

// ----------- ATTENTION -------------
// This header should NOT include any other headers from sanitizer runtime.
#include "sanitizer_internal_defs.h"

namespace __sanitizer {

// internal_X() is a custom implementation of X() for use in RTL.

extern "C" {
// These are used as builtin replacements; see sanitizer_redefine_builtins.h.
// In normal runtime code, use the __sanitizer::internal_X() aliases instead.
SANITIZER_INTERFACE_ATTRIBUTE void *__sanitizer_internal_memcpy(void *dest,
                                                                const void *src,
                                                                uptr n);
SANITIZER_INTERFACE_ATTRIBUTE void *__sanitizer_internal_memmove(
    void *dest, const void *src, uptr n);
SANITIZER_INTERFACE_ATTRIBUTE void *__sanitizer_internal_memset(void *s, int c,
                                                                uptr n);
}  // extern "C"

// String functions
s64 internal_atoll(const char *nptr);
void *internal_memchr(const void *s, int c, uptr n);
void *internal_memrchr(const void *s, int c, uptr n);
int internal_memcmp(const void* s1, const void* s2, uptr n);
ALWAYS_INLINE void *internal_memcpy(void *dest, const void *src, uptr n) {
  return __sanitizer_internal_memcpy(dest, src, n);
}
ALWAYS_INLINE void *internal_memmove(void *dest, const void *src, uptr n) {
  return __sanitizer_internal_memmove(dest, src, n);
}
// Should not be used in performance-critical places.
ALWAYS_INLINE void *internal_memset(void *s, int c, uptr n) {
  return __sanitizer_internal_memset(s, c, n);
}
char* internal_strchr(const char *s, int c);
char *internal_strchrnul(const char *s, int c);
int internal_strcmp(const char *s1, const char *s2);
uptr internal_strcspn(const char *s, const char *reject);
char *internal_strdup(const char *s);
uptr internal_strlen(const char *s);
uptr internal_strlcat(char *dst, const char *src, uptr maxlen);
char *internal_strncat(char *dst, const char *src, uptr n);
int internal_strncmp(const char *s1, const char *s2, uptr n);
uptr internal_strlcpy(char *dst, const char *src, uptr maxlen);
char *internal_strncpy(char *dst, const char *src, uptr n);
uptr internal_strnlen(const char *s, uptr maxlen);
char *internal_strrchr(const char *s, int c);
char *internal_strstr(const char *haystack, const char *needle);
// Works only for base=10 and doesn't set errno.
s64 internal_simple_strtoll(const char *nptr, const char **endptr, int base);
int internal_snprintf(char *buffer, uptr length, const char *format, ...)
    FORMAT(3, 4);
uptr internal_wcslen(const wchar_t *s);
uptr internal_wcsnlen(const wchar_t *s, uptr maxlen);
wchar_t *internal_wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t *internal_wcsncpy(wchar_t *dst, const wchar_t *src, uptr maxlen);
// Return true if all bytes in [mem, mem+size) are zero.
// Optimized for the case when the result is true.
bool mem_is_zero(const char *mem, uptr size);

// I/O
// Define these as macros so we can use them in linker initialized global
// structs without dynamic initialization.
#define kInvalidFd ((fd_t)-1)
#define kStdinFd ((fd_t)0)
#define kStdoutFd ((fd_t)1)
#define kStderrFd ((fd_t)2)

uptr internal_ftruncate(fd_t fd, uptr size);

// OS
void NORETURN internal__exit(int exitcode);
void internal_sleep(unsigned seconds);
void internal_usleep(u64 useconds);

uptr internal_getpid();
uptr internal_getppid();

int internal_dlinfo(void *handle, int request, void *p);

// Threading
uptr internal_sched_yield();

// Error handling
bool internal_iserror(uptr retval, int *rverrno = nullptr);

} // namespace __sanitizer

#endif // SANITIZER_LIBC_H
