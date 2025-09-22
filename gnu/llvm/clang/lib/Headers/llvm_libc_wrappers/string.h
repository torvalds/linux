//===-- Wrapper for C standard string.h declarations on the GPU -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __CLANG_LLVM_LIBC_WRAPPERS_STRING_H__
#define __CLANG_LLVM_LIBC_WRAPPERS_STRING_H__

#if !defined(_OPENMP) && !defined(__HIP__) && !defined(__CUDA__)
#error "This file is for GPU offloading compilation only"
#endif

#include_next <string.h>

#if __has_include(<llvm-libc-decls/string.h>)

#if defined(__HIP__) || defined(__CUDA__)
#define __LIBC_ATTRS __attribute__((device))
#endif

#pragma omp begin declare target

// The GNU headers provide C++ standard compliant headers when in C++ mode and
// the LLVM libc does not. We need to manually provide the definitions using the
// same prototypes.
#if defined(__cplusplus) && defined(__GLIBC__) &&                              \
    defined(__CORRECT_ISO_CPP_STRING_H_PROTO)

#ifndef __LIBC_ATTRS
#define __LIBC_ATTRS
#endif

extern "C" {
void *memccpy(void *__restrict, const void *__restrict, int,
              size_t) __LIBC_ATTRS;
int memcmp(const void *, const void *, size_t) __LIBC_ATTRS;
void *memcpy(void *__restrict, const void *__restrict, size_t) __LIBC_ATTRS;
void *memmem(const void *, size_t, const void *, size_t) __LIBC_ATTRS;
void *memmove(void *, const void *, size_t) __LIBC_ATTRS;
void *mempcpy(void *__restrict, const void *__restrict, size_t) __LIBC_ATTRS;
void *memset(void *, int, size_t) __LIBC_ATTRS;
char *stpcpy(char *__restrict, const char *__restrict) __LIBC_ATTRS;
char *stpncpy(char *__restrict, const char *__restrict, size_t) __LIBC_ATTRS;
char *strcat(char *__restrict, const char *__restrict) __LIBC_ATTRS;
int strcmp(const char *, const char *) __LIBC_ATTRS;
int strcoll(const char *, const char *) __LIBC_ATTRS;
char *strcpy(char *__restrict, const char *__restrict) __LIBC_ATTRS;
size_t strcspn(const char *, const char *) __LIBC_ATTRS;
char *strdup(const char *) __LIBC_ATTRS;
size_t strlen(const char *) __LIBC_ATTRS;
char *strncat(char *__restrict, const char *__restrict, size_t) __LIBC_ATTRS;
int strncmp(const char *, const char *, size_t) __LIBC_ATTRS;
char *strncpy(char *__restrict, const char *__restrict, size_t) __LIBC_ATTRS;
char *strndup(const char *, size_t) __LIBC_ATTRS;
size_t strnlen(const char *, size_t) __LIBC_ATTRS;
size_t strspn(const char *, const char *) __LIBC_ATTRS;
char *strtok(char *__restrict, const char *__restrict) __LIBC_ATTRS;
char *strtok_r(char *__restrict, const char *__restrict,
               char **__restrict) __LIBC_ATTRS;
size_t strxfrm(char *__restrict, const char *__restrict, size_t) __LIBC_ATTRS;
}

extern "C++" {
char *strstr(char *, const char *) noexcept __LIBC_ATTRS;
const char *strstr(const char *, const char *) noexcept __LIBC_ATTRS;
char *strpbrk(char *, const char *) noexcept __LIBC_ATTRS;
const char *strpbrk(const char *, const char *) noexcept __LIBC_ATTRS;
char *strrchr(char *, int) noexcept __LIBC_ATTRS;
const char *strrchr(const char *, int) noexcept __LIBC_ATTRS;
char *strchr(char *, int) noexcept __LIBC_ATTRS;
const char *strchr(const char *, int) noexcept __LIBC_ATTRS;
char *strchrnul(char *, int) noexcept __LIBC_ATTRS;
const char *strchrnul(const char *, int) noexcept __LIBC_ATTRS;
char *strcasestr(char *, const char *) noexcept __LIBC_ATTRS;
const char *strcasestr(const char *, const char *) noexcept __LIBC_ATTRS;
void *memrchr(void *__s, int __c, size_t __n) noexcept __LIBC_ATTRS;
const void *memrchr(const void *__s, int __c, size_t __n) noexcept __LIBC_ATTRS;
void *memchr(void *__s, int __c, size_t __n) noexcept __LIBC_ATTRS;
const void *memchr(const void *__s, int __c, size_t __n) noexcept __LIBC_ATTRS;
}

#else
#include <llvm-libc-decls/string.h>

#endif

#pragma omp end declare target

#undef __LIBC_ATTRS

#endif

#endif // __CLANG_LLVM_LIBC_WRAPPERS_STRING_H__
