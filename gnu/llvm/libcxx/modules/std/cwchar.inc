// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::mbstate_t _LIBCPP_USING_IF_EXISTS;
  using std::size_t _LIBCPP_USING_IF_EXISTS;
  using std::wint_t _LIBCPP_USING_IF_EXISTS;

  using std::tm _LIBCPP_USING_IF_EXISTS;

  using std::btowc _LIBCPP_USING_IF_EXISTS;
  using std::fgetwc _LIBCPP_USING_IF_EXISTS;
  using std::fgetws _LIBCPP_USING_IF_EXISTS;
  using std::fputwc _LIBCPP_USING_IF_EXISTS;
  using std::fputws _LIBCPP_USING_IF_EXISTS;
  using std::fwide _LIBCPP_USING_IF_EXISTS;
  using std::fwprintf _LIBCPP_USING_IF_EXISTS;
  using std::fwscanf _LIBCPP_USING_IF_EXISTS;
  using std::getwc _LIBCPP_USING_IF_EXISTS;
  using std::getwchar _LIBCPP_USING_IF_EXISTS;
  using std::putwc _LIBCPP_USING_IF_EXISTS;
  using std::putwchar _LIBCPP_USING_IF_EXISTS;
  using std::swprintf _LIBCPP_USING_IF_EXISTS;
  using std::swscanf _LIBCPP_USING_IF_EXISTS;
  using std::ungetwc _LIBCPP_USING_IF_EXISTS;
  using std::vfwprintf _LIBCPP_USING_IF_EXISTS;
  using std::vfwscanf _LIBCPP_USING_IF_EXISTS;
  using std::vswprintf _LIBCPP_USING_IF_EXISTS;
  using std::vswscanf _LIBCPP_USING_IF_EXISTS;
  using std::vwprintf _LIBCPP_USING_IF_EXISTS;
  using std::vwscanf _LIBCPP_USING_IF_EXISTS;
  using std::wcscat _LIBCPP_USING_IF_EXISTS;
  using std::wcschr _LIBCPP_USING_IF_EXISTS;
  using std::wcscmp _LIBCPP_USING_IF_EXISTS;
  using std::wcscoll _LIBCPP_USING_IF_EXISTS;
  using std::wcscpy _LIBCPP_USING_IF_EXISTS;
  using std::wcscspn _LIBCPP_USING_IF_EXISTS;
  using std::wcsftime _LIBCPP_USING_IF_EXISTS;
  using std::wcslen _LIBCPP_USING_IF_EXISTS;
  using std::wcsncat _LIBCPP_USING_IF_EXISTS;
  using std::wcsncmp _LIBCPP_USING_IF_EXISTS;
  using std::wcsncpy _LIBCPP_USING_IF_EXISTS;
  using std::wcspbrk _LIBCPP_USING_IF_EXISTS;
  using std::wcsrchr _LIBCPP_USING_IF_EXISTS;
  using std::wcsspn _LIBCPP_USING_IF_EXISTS;
  using std::wcsstr _LIBCPP_USING_IF_EXISTS;
  using std::wcstod _LIBCPP_USING_IF_EXISTS;
  using std::wcstof _LIBCPP_USING_IF_EXISTS;
  using std::wcstok _LIBCPP_USING_IF_EXISTS;
  using std::wcstol _LIBCPP_USING_IF_EXISTS;
  using std::wcstold _LIBCPP_USING_IF_EXISTS;
  using std::wcstoll _LIBCPP_USING_IF_EXISTS;
  using std::wcstoul _LIBCPP_USING_IF_EXISTS;
  using std::wcstoull _LIBCPP_USING_IF_EXISTS;
  using std::wcsxfrm _LIBCPP_USING_IF_EXISTS;
  using std::wctob _LIBCPP_USING_IF_EXISTS;
  using std::wmemchr _LIBCPP_USING_IF_EXISTS;
  using std::wmemcmp _LIBCPP_USING_IF_EXISTS;
  using std::wmemcpy _LIBCPP_USING_IF_EXISTS;
  using std::wmemmove _LIBCPP_USING_IF_EXISTS;
  using std::wmemset _LIBCPP_USING_IF_EXISTS;
  using std::wprintf _LIBCPP_USING_IF_EXISTS;
  using std::wscanf _LIBCPP_USING_IF_EXISTS;

  // [c.mb.wcs], multibyte / wide string and character conversion functions
  using std::mbrlen _LIBCPP_USING_IF_EXISTS;
  using std::mbrtowc _LIBCPP_USING_IF_EXISTS;
  using std::mbsinit _LIBCPP_USING_IF_EXISTS;
  using std::mbsrtowcs _LIBCPP_USING_IF_EXISTS;
  using std::wcrtomb _LIBCPP_USING_IF_EXISTS;
  using std::wcsrtombs _LIBCPP_USING_IF_EXISTS;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS
} // namespace std
