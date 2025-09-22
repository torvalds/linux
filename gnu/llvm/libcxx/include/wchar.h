// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__need_wint_t) || defined(__need_mbstate_t)

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

#  include_next <wchar.h>

#elif !defined(_LIBCPP_WCHAR_H)
#  define _LIBCPP_WCHAR_H

/*
    wchar.h synopsis

Macros:

    NULL
    WCHAR_MAX
    WCHAR_MIN
    WEOF

Types:

    mbstate_t
    size_t
    tm
    wint_t

int fwprintf(FILE* restrict stream, const wchar_t* restrict format, ...);
int fwscanf(FILE* restrict stream, const wchar_t* restrict format, ...);
int swprintf(wchar_t* restrict s, size_t n, const wchar_t* restrict format, ...);
int swscanf(const wchar_t* restrict s, const wchar_t* restrict format, ...);
int vfwprintf(FILE* restrict stream, const wchar_t* restrict format, va_list arg);
int vfwscanf(FILE* restrict stream, const wchar_t* restrict format, va_list arg);  // C99
int vswprintf(wchar_t* restrict s, size_t n, const wchar_t* restrict format, va_list arg);
int vswscanf(const wchar_t* restrict s, const wchar_t* restrict format, va_list arg);  // C99
int vwprintf(const wchar_t* restrict format, va_list arg);
int vwscanf(const wchar_t* restrict format, va_list arg);  // C99
int wprintf(const wchar_t* restrict format, ...);
int wscanf(const wchar_t* restrict format, ...);
wint_t fgetwc(FILE* stream);
wchar_t* fgetws(wchar_t* restrict s, int n, FILE* restrict stream);
wint_t fputwc(wchar_t c, FILE* stream);
int fputws(const wchar_t* restrict s, FILE* restrict stream);
int fwide(FILE* stream, int mode);
wint_t getwc(FILE* stream);
wint_t getwchar();
wint_t putwc(wchar_t c, FILE* stream);
wint_t putwchar(wchar_t c);
wint_t ungetwc(wint_t c, FILE* stream);
double wcstod(const wchar_t* restrict nptr, wchar_t** restrict endptr);
float wcstof(const wchar_t* restrict nptr, wchar_t** restrict endptr);         // C99
long double wcstold(const wchar_t* restrict nptr, wchar_t** restrict endptr);  // C99
long wcstol(const wchar_t* restrict nptr, wchar_t** restrict endptr, int base);
long long wcstoll(const wchar_t* restrict nptr, wchar_t** restrict endptr, int base);  // C99
unsigned long wcstoul(const wchar_t* restrict nptr, wchar_t** restrict endptr, int base);
unsigned long long wcstoull(const wchar_t* restrict nptr, wchar_t** restrict endptr, int base);  // C99
wchar_t* wcscpy(wchar_t* restrict s1, const wchar_t* restrict s2);
wchar_t* wcsncpy(wchar_t* restrict s1, const wchar_t* restrict s2, size_t n);
wchar_t* wcscat(wchar_t* restrict s1, const wchar_t* restrict s2);
wchar_t* wcsncat(wchar_t* restrict s1, const wchar_t* restrict s2, size_t n);
int wcscmp(const wchar_t* s1, const wchar_t* s2);
int wcscoll(const wchar_t* s1, const wchar_t* s2);
int wcsncmp(const wchar_t* s1, const wchar_t* s2, size_t n);
size_t wcsxfrm(wchar_t* restrict s1, const wchar_t* restrict s2, size_t n);
const wchar_t* wcschr(const wchar_t* s, wchar_t c);
      wchar_t* wcschr(      wchar_t* s, wchar_t c);
size_t wcscspn(const wchar_t* s1, const wchar_t* s2);
size_t wcslen(const wchar_t* s);
const wchar_t* wcspbrk(const wchar_t* s1, const wchar_t* s2);
      wchar_t* wcspbrk(      wchar_t* s1, const wchar_t* s2);
const wchar_t* wcsrchr(const wchar_t* s, wchar_t c);
      wchar_t* wcsrchr(      wchar_t* s, wchar_t c);
size_t wcsspn(const wchar_t* s1, const wchar_t* s2);
const wchar_t* wcsstr(const wchar_t* s1, const wchar_t* s2);
      wchar_t* wcsstr(      wchar_t* s1, const wchar_t* s2);
wchar_t* wcstok(wchar_t* restrict s1, const wchar_t* restrict s2, wchar_t** restrict ptr);
const wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n);
      wchar_t* wmemchr(      wchar_t* s, wchar_t c, size_t n);
int wmemcmp(wchar_t* restrict s1, const wchar_t* restrict s2, size_t n);
wchar_t* wmemcpy(wchar_t* restrict s1, const wchar_t* restrict s2, size_t n);
wchar_t* wmemmove(wchar_t* s1, const wchar_t* s2, size_t n);
wchar_t* wmemset(wchar_t* s, wchar_t c, size_t n);
size_t wcsftime(wchar_t* restrict s, size_t maxsize, const wchar_t* restrict format,
                const tm* restrict timeptr);
wint_t btowc(int c);
int wctob(wint_t c);
int mbsinit(const mbstate_t* ps);
size_t mbrlen(const char* restrict s, size_t n, mbstate_t* restrict ps);
size_t mbrtowc(wchar_t* restrict pwc, const char* restrict s, size_t n, mbstate_t* restrict ps);
size_t wcrtomb(char* restrict s, wchar_t wc, mbstate_t* restrict ps);
size_t mbsrtowcs(wchar_t* restrict dst, const char** restrict src, size_t len,
                 mbstate_t* restrict ps);
size_t wcsrtombs(char* restrict dst, const wchar_t** restrict src, size_t len,
                 mbstate_t* restrict ps);

*/

#  include <__config>
#  include <stddef.h>

#  if defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)
#    error                                                                                                             \
        "The <wchar.h> header is not supported since libc++ has been configured with LIBCXX_ENABLE_WIDE_CHARACTERS disabled"
#  endif

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

// We define this here to support older versions of glibc <wchar.h> that do
// not define this for clang.
#  ifdef __cplusplus
#    define __CORRECT_ISO_CPP_WCHAR_H_PROTO
#  endif

#  if __has_include_next(<wchar.h>)
#    include_next <wchar.h>
#  else
#    include <__mbstate_t.h> // make sure we have mbstate_t regardless of the existence of <wchar.h>
#  endif

// Determine whether we have const-correct overloads for wcschr and friends.
#  if defined(_WCHAR_H_CPLUSPLUS_98_CONFORMANCE_)
#    define _LIBCPP_WCHAR_H_HAS_CONST_OVERLOADS 1
#  elif defined(__GLIBC_PREREQ)
#    if __GLIBC_PREREQ(2, 10)
#      define _LIBCPP_WCHAR_H_HAS_CONST_OVERLOADS 1
#    endif
#  elif defined(_LIBCPP_MSVCRT)
#    if defined(_CRT_CONST_CORRECT_OVERLOADS)
#      define _LIBCPP_WCHAR_H_HAS_CONST_OVERLOADS 1
#    endif
#  endif

#  if defined(__cplusplus) && !defined(_LIBCPP_WCHAR_H_HAS_CONST_OVERLOADS) && defined(_LIBCPP_PREFERRED_OVERLOAD)
extern "C++" {
inline _LIBCPP_HIDE_FROM_ABI wchar_t* __libcpp_wcschr(const wchar_t* __s, wchar_t __c) {
  return (wchar_t*)wcschr(__s, __c);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const wchar_t* wcschr(const wchar_t* __s, wchar_t __c) {
  return __libcpp_wcschr(__s, __c);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD wchar_t* wcschr(wchar_t* __s, wchar_t __c) {
  return __libcpp_wcschr(__s, __c);
}

inline _LIBCPP_HIDE_FROM_ABI wchar_t* __libcpp_wcspbrk(const wchar_t* __s1, const wchar_t* __s2) {
  return (wchar_t*)wcspbrk(__s1, __s2);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const wchar_t*
wcspbrk(const wchar_t* __s1, const wchar_t* __s2) {
  return __libcpp_wcspbrk(__s1, __s2);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD wchar_t* wcspbrk(wchar_t* __s1, const wchar_t* __s2) {
  return __libcpp_wcspbrk(__s1, __s2);
}

inline _LIBCPP_HIDE_FROM_ABI wchar_t* __libcpp_wcsrchr(const wchar_t* __s, wchar_t __c) {
  return (wchar_t*)wcsrchr(__s, __c);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const wchar_t* wcsrchr(const wchar_t* __s, wchar_t __c) {
  return __libcpp_wcsrchr(__s, __c);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD wchar_t* wcsrchr(wchar_t* __s, wchar_t __c) {
  return __libcpp_wcsrchr(__s, __c);
}

inline _LIBCPP_HIDE_FROM_ABI wchar_t* __libcpp_wcsstr(const wchar_t* __s1, const wchar_t* __s2) {
  return (wchar_t*)wcsstr(__s1, __s2);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const wchar_t*
wcsstr(const wchar_t* __s1, const wchar_t* __s2) {
  return __libcpp_wcsstr(__s1, __s2);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD wchar_t* wcsstr(wchar_t* __s1, const wchar_t* __s2) {
  return __libcpp_wcsstr(__s1, __s2);
}

inline _LIBCPP_HIDE_FROM_ABI wchar_t* __libcpp_wmemchr(const wchar_t* __s, wchar_t __c, size_t __n) {
  return (wchar_t*)wmemchr(__s, __c, __n);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD const wchar_t*
wmemchr(const wchar_t* __s, wchar_t __c, size_t __n) {
  return __libcpp_wmemchr(__s, __c, __n);
}
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_PREFERRED_OVERLOAD wchar_t* wmemchr(wchar_t* __s, wchar_t __c, size_t __n) {
  return __libcpp_wmemchr(__s, __c, __n);
}
}
#  endif

#  if defined(__cplusplus) && (defined(_LIBCPP_MSVCRT_LIKE) || defined(__MVS__))
extern "C" {
size_t mbsnrtowcs(
    wchar_t* __restrict __dst, const char** __restrict __src, size_t __nmc, size_t __len, mbstate_t* __restrict __ps);
size_t wcsnrtombs(
    char* __restrict __dst, const wchar_t** __restrict __src, size_t __nwc, size_t __len, mbstate_t* __restrict __ps);
} // extern "C"
#  endif // __cplusplus && (_LIBCPP_MSVCRT || __MVS__)

#endif // _LIBCPP_WCHAR_H
