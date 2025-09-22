//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdarg> // va_start, va_end
#include <locale>
#include <memory>
#include <type_traits>

#include <__locale_dir/locale_base_api/locale_guard.h>

int __libcpp_vasprintf(char** sptr, const char* __restrict fmt, va_list ap);

using std::__libcpp_locale_guard;

// FIXME: base and mask currently unused. Needs manual work to construct the new locale
locale_t newlocale(int /*mask*/, const char* locale, locale_t /*base*/) {
  return {_create_locale(LC_ALL, locale), locale};
}

decltype(MB_CUR_MAX) MB_CUR_MAX_L(locale_t __l) {
#if defined(_LIBCPP_MSVCRT)
  return ___mb_cur_max_l_func(__l);
#else
  __libcpp_locale_guard __current(__l);
  return MB_CUR_MAX;
#endif
}

lconv* localeconv_l(locale_t& loc) {
  __libcpp_locale_guard __current(loc);
  lconv* lc = localeconv();
  if (!lc)
    return lc;
  return loc.__store_lconv(lc);
}
size_t mbrlen_l(const char* __restrict s, size_t n, mbstate_t* __restrict ps, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return mbrlen(s, n, ps);
}
size_t
mbsrtowcs_l(wchar_t* __restrict dst, const char** __restrict src, size_t len, mbstate_t* __restrict ps, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return mbsrtowcs(dst, src, len, ps);
}
size_t wcrtomb_l(char* __restrict s, wchar_t wc, mbstate_t* __restrict ps, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return wcrtomb(s, wc, ps);
}
size_t mbrtowc_l(wchar_t* __restrict pwc, const char* __restrict s, size_t n, mbstate_t* __restrict ps, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return mbrtowc(pwc, s, n, ps);
}
size_t mbsnrtowcs_l(wchar_t* __restrict dst,
                    const char** __restrict src,
                    size_t nms,
                    size_t len,
                    mbstate_t* __restrict ps,
                    locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return mbsnrtowcs(dst, src, nms, len, ps);
}
size_t wcsnrtombs_l(char* __restrict dst,
                    const wchar_t** __restrict src,
                    size_t nwc,
                    size_t len,
                    mbstate_t* __restrict ps,
                    locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return wcsnrtombs(dst, src, nwc, len, ps);
}
wint_t btowc_l(int c, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return btowc(c);
}
int wctob_l(wint_t c, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return wctob(c);
}

int snprintf_l(char* ret, size_t n, locale_t loc, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
#if defined(_LIBCPP_MSVCRT)
  // FIXME: Remove usage of internal CRT function and globals.
  int result = __stdio_common_vsprintf(
      _CRT_INTERNAL_LOCAL_PRINTF_OPTIONS | _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR, ret, n, format, loc, ap);
#else
  __libcpp_locale_guard __current(loc);
  _LIBCPP_DIAGNOSTIC_PUSH
  _LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wformat-nonliteral")
  int result = vsnprintf(ret, n, format, ap);
  _LIBCPP_DIAGNOSTIC_POP
#endif
  va_end(ap);
  return result;
}

int asprintf_l(char** ret, locale_t loc, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = vasprintf_l(ret, loc, format, ap);
  va_end(ap);
  return result;
}
int vasprintf_l(char** ret, locale_t loc, const char* format, va_list ap) {
  __libcpp_locale_guard __current(loc);
  return __libcpp_vasprintf(ret, format, ap);
}

#if !defined(_LIBCPP_MSVCRT)
float strtof_l(const char* nptr, char** endptr, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return strtof(nptr, endptr);
}

long double strtold_l(const char* nptr, char** endptr, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return strtold(nptr, endptr);
}
#endif

#if defined(__MINGW32__) && __MSVCRT_VERSION__ < 0x0800
size_t strftime_l(char* ret, size_t n, const char* format, const struct tm* tm, locale_t loc) {
  __libcpp_locale_guard __current(loc);
  return strftime(ret, n, format, tm);
}
#endif
