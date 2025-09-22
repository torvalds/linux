// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export {
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using ::mbstate_t _LIBCPP_USING_IF_EXISTS;
  using ::size_t _LIBCPP_USING_IF_EXISTS;
  using ::wint_t _LIBCPP_USING_IF_EXISTS;

  using ::tm _LIBCPP_USING_IF_EXISTS;

  using ::btowc _LIBCPP_USING_IF_EXISTS;
  using ::fgetwc _LIBCPP_USING_IF_EXISTS;
  using ::fgetws _LIBCPP_USING_IF_EXISTS;
  using ::fputwc _LIBCPP_USING_IF_EXISTS;
  using ::fputws _LIBCPP_USING_IF_EXISTS;
  using ::fwide _LIBCPP_USING_IF_EXISTS;
  using ::fwprintf _LIBCPP_USING_IF_EXISTS;
  using ::fwscanf _LIBCPP_USING_IF_EXISTS;
  using ::getwc _LIBCPP_USING_IF_EXISTS;
  using ::getwchar _LIBCPP_USING_IF_EXISTS;
  using ::putwc _LIBCPP_USING_IF_EXISTS;
  using ::putwchar _LIBCPP_USING_IF_EXISTS;
  using ::swprintf _LIBCPP_USING_IF_EXISTS;
  using ::swscanf _LIBCPP_USING_IF_EXISTS;
  using ::ungetwc _LIBCPP_USING_IF_EXISTS;
  using ::vfwprintf _LIBCPP_USING_IF_EXISTS;
  using ::vfwscanf _LIBCPP_USING_IF_EXISTS;
  using ::vswprintf _LIBCPP_USING_IF_EXISTS;
  using ::vswscanf _LIBCPP_USING_IF_EXISTS;
  using ::vwprintf _LIBCPP_USING_IF_EXISTS;
  using ::vwscanf _LIBCPP_USING_IF_EXISTS;
  using ::wcscat _LIBCPP_USING_IF_EXISTS;
  using ::wcschr _LIBCPP_USING_IF_EXISTS;
  using ::wcscmp _LIBCPP_USING_IF_EXISTS;
  using ::wcscoll _LIBCPP_USING_IF_EXISTS;
  using ::wcscpy _LIBCPP_USING_IF_EXISTS;
  using ::wcscspn _LIBCPP_USING_IF_EXISTS;
  using ::wcsftime _LIBCPP_USING_IF_EXISTS;
  using ::wcslen _LIBCPP_USING_IF_EXISTS;
  using ::wcsncat _LIBCPP_USING_IF_EXISTS;
  using ::wcsncmp _LIBCPP_USING_IF_EXISTS;
  using ::wcsncpy _LIBCPP_USING_IF_EXISTS;
  using ::wcspbrk _LIBCPP_USING_IF_EXISTS;
  using ::wcsrchr _LIBCPP_USING_IF_EXISTS;
  using ::wcsspn _LIBCPP_USING_IF_EXISTS;
  using ::wcsstr _LIBCPP_USING_IF_EXISTS;
  using ::wcstod _LIBCPP_USING_IF_EXISTS;
  using ::wcstof _LIBCPP_USING_IF_EXISTS;
  using ::wcstok _LIBCPP_USING_IF_EXISTS;
  using ::wcstol _LIBCPP_USING_IF_EXISTS;
  using ::wcstold _LIBCPP_USING_IF_EXISTS;
  using ::wcstoll _LIBCPP_USING_IF_EXISTS;
  using ::wcstoul _LIBCPP_USING_IF_EXISTS;
  using ::wcstoull _LIBCPP_USING_IF_EXISTS;
  using ::wcsxfrm _LIBCPP_USING_IF_EXISTS;
  using ::wctob _LIBCPP_USING_IF_EXISTS;
  using ::wmemchr _LIBCPP_USING_IF_EXISTS;
  using ::wmemcmp _LIBCPP_USING_IF_EXISTS;
  using ::wmemcpy _LIBCPP_USING_IF_EXISTS;
  using ::wmemmove _LIBCPP_USING_IF_EXISTS;
  using ::wmemset _LIBCPP_USING_IF_EXISTS;
  using ::wprintf _LIBCPP_USING_IF_EXISTS;
  using ::wscanf _LIBCPP_USING_IF_EXISTS;

  // [c.mb.wcs], multibyte / wide string and character conversion functions
  using ::mbrlen _LIBCPP_USING_IF_EXISTS;
  using ::mbrtowc _LIBCPP_USING_IF_EXISTS;
  using ::mbsinit _LIBCPP_USING_IF_EXISTS;
  using ::mbsrtowcs _LIBCPP_USING_IF_EXISTS;
  using ::wcrtomb _LIBCPP_USING_IF_EXISTS;
  using ::wcsrtombs _LIBCPP_USING_IF_EXISTS;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS
} // export
