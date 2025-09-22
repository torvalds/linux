//===-----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___LOCALE_DIR_LOCALE_BASE_API_H
#define _LIBCPP___LOCALE_DIR_LOCALE_BASE_API_H

#if defined(_LIBCPP_MSVCRT_LIKE)
#  include <__locale_dir/locale_base_api/win32.h>
#elif defined(_AIX) || defined(__MVS__)
#  include <__locale_dir/locale_base_api/ibm.h>
#elif defined(__ANDROID__)
#  include <__locale_dir/locale_base_api/android.h>
#elif defined(__sun__)
#  include <__locale_dir/locale_base_api/solaris.h>
#elif defined(_NEWLIB_VERSION)
#  include <__locale_dir/locale_base_api/newlib.h>
#elif defined(__OpenBSD__)
#  include <__locale_dir/locale_base_api/openbsd.h>
#elif defined(__Fuchsia__)
#  include <__locale_dir/locale_base_api/fuchsia.h>
#elif defined(__wasi__) || defined(_LIBCPP_HAS_MUSL_LIBC)
#  include <__locale_dir/locale_base_api/musl.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#  include <xlocale.h>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

/*
The platform-specific headers have to provide the following interface:

// TODO: rename this to __libcpp_locale_t
using locale_t = implementation-defined;

implementation-defined __libcpp_mb_cur_max_l(locale_t);
wint_t __libcpp_btowc_l(int, locale_t);
int __libcpp_wctob_l(wint_t, locale_t);
size_t __libcpp_wcsnrtombs_l(char* dest, const wchar_t** src, size_t wide_char_count, size_t len, mbstate_t, locale_t);
size_t __libcpp_wcrtomb_l(char* str, wchar_t wide_char, mbstate_t*, locale_t);
size_t __libcpp_mbsnrtowcs_l(wchar_t* dest, const char** src, size_t max_out, size_t len, mbstate_t*, locale_t);
size_t __libcpp_mbrtowc_l(wchar_t* dest, cosnt char* src, size_t count, mbstate_t*, locale_t);
int __libcpp_mbtowc_l(wchar_t* dest, const char* src, size_t count, locale_t);
size_t __libcpp_mbrlen_l(const char* str, size_t count, mbstate_t*, locale_t);
lconv* __libcpp_localeconv_l(locale_t);
size_t __libcpp_mbsrtowcs_l(wchar_t* dest, const char** src, size_t len, mbstate_t*, locale_t);
int __libcpp_snprintf_l(char* dest, size_t buff_size, locale_t, const char* format, ...);
int __libcpp_asprintf_l(char** dest, locale_t, const char* format, ...);
int __libcpp_sscanf_l(const char* dest, locale_t, const char* format, ...);

// TODO: change these to reserved names
float strtof_l(const char* str, char** str_end, locale_t);
double strtod_l(const char* str, char** str_end, locale_t);
long double strtold_l(const char* str, char** str_end, locale_t);
long long strtoll_l(const char* str, char** str_end, locale_t);
unsigned long long strtoull_l(const char* str, char** str_end, locale_t);

locale_t newlocale(int category_mask, const char* locale, locale_t base);
void freelocale(locale_t);

int islower_l(int ch, locale_t);
int isupper_l(int ch, locale_t);
int isdigit_l(int ch, locale_t);
int isxdigit_l(int ch, locale_t);
int strcoll_l(const char* lhs, const char* rhs, locale_t);
size_t strxfrm_l(char* dst, const char* src, size_t n, locale_t);
int wcscoll_l(const char* lhs, const char* rhs, locale_t);
size_t wcsxfrm_l(wchar_t* dst, const wchar_t* src, size_t n, locale_t);
int toupper_l(int ch, locale_t);
int tolower_l(int ch, locale_t);
int iswspace_l(wint_t ch, locale_t);
int iswprint_l(wint_t ch, locale_t);
int iswcntrl_l(wint_t ch, locale_t);
int iswupper_l(wint_t ch, locale_t);
int iswlower_l(wint_t ch, locale_t);
int iswalpha_l(wint_t ch, locale_t);
int iswblank_l(wint_t ch, locale_t);
int iswdigit_l(wint_t ch, locale_t);
int iswpunct_l(wint_t ch, locale_t);
int iswxdigit_l(wint_t ch, locale_t);
wint_t towupper_l(wint_t ch, locale_t);
wint_t towlower_l(wint_t ch, locale_t);
size_t strftime_l(char* str, size_t len, const char* format, const tm*, locale_t);


These functions are equivalent to their C counterparts,
except that locale_t is used instead of the current global locale.

The variadic functions may be implemented as templates with a parameter pack instead of variadic functions.
*/

#endif // _LIBCPP___LOCALE_DIR_LOCALE_BASE_API_H
