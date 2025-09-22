// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_STRING_H
#define _LIBCPP_STRING_H

/*
    string.h synopsis

Macros:

    NULL

Types:

    size_t

void* memcpy(void* restrict s1, const void* restrict s2, size_t n);
void* memmove(void* s1, const void* s2, size_t n);
char* strcpy (char* restrict s1, const char* restrict s2);
char* strncpy(char* restrict s1, const char* restrict s2, size_t n);
char* strcat (char* restrict s1, const char* restrict s2);
char* strncat(char* restrict s1, const char* restrict s2, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
int strcmp (const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
int strcoll(const char* s1, const char* s2);
size_t strxfrm(char* restrict s1, const char* restrict s2, size_t n);
const void* memchr(const void* s, int c, size_t n);
      void* memchr(      void* s, int c, size_t n);
const char* strchr(const char* s, int c);
      char* strchr(      char* s, int c);
size_t strcspn(const char* s1, const char* s2);
const char* strpbrk(const char* s1, const char* s2);
      char* strpbrk(      char* s1, const char* s2);
const char* strrchr(const char* s, int c);
      char* strrchr(      char* s, int c);
size_t strspn(const char* s1, const char* s2);
const char* strstr(const char* s1, const char* s2);
      char* strstr(      char* s1, const char* s2);
char* strtok(char* restrict s1, const char* restrict s2);
void* memset(void* s, int c, size_t n);
char* strerror(int errnum);
size_t strlen(const char* s);

*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if __has_include_next(<string.h>)
#  include_next <string.h>
#endif

// MSVCRT, GNU libc and its derivates may already have the correct prototype in
// <string.h>. This macro can be defined by users if their C library provides
// the right signature.
#if defined(__CORRECT_ISO_CPP_STRING_H_PROTO) || defined(_LIBCPP_MSVCRT) || defined(_STRING_H_CPLUSPLUS_98_CONFORMANCE_)
#  define _LIBCPP_STRING_H_HAS_CONST_OVERLOADS
#endif

#if defined(__cplusplus) && !defined(_LIBCPP_STRING_H_HAS_CONST_OVERLOADS) && defined(_LIBCPP_PREFERRED_OVERLOAD)
extern "C++" {
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const char* strchr(const char* __s, int __c) {
  return __builtin_strchr(__s, __c);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD char* strchr(char* __s, int __c) {
  return __builtin_strchr(__s, __c);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const char* strpbrk(const char* __s1, const char* __s2) {
  return __builtin_strpbrk(__s1, __s2);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD char* strpbrk(char* __s1, const char* __s2) {
  return __builtin_strpbrk(__s1, __s2);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const char* strrchr(const char* __s, int __c) {
  return __builtin_strrchr(__s, __c);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD char* strrchr(char* __s, int __c) {
  return __builtin_strrchr(__s, __c);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const void* memchr(const void* __s, int __c, size_t __n) {
  return __builtin_memchr(__s, __c, __n);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD void* memchr(void* __s, int __c, size_t __n) {
  return __builtin_memchr(__s, __c, __n);
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const char* strstr(const char* __s1, const char* __s2) {
  return __builtin_strstr(__s1, __s2);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD char* strstr(char* __s1, const char* __s2) {
  return __builtin_strstr(__s1, __s2);
}
} // extern "C++"
#endif

#endif // _LIBCPP_STRING_H
