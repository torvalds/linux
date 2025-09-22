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
  using std::wctrans_t _LIBCPP_USING_IF_EXISTS;
  using std::wctype_t _LIBCPP_USING_IF_EXISTS;
  using std::wint_t _LIBCPP_USING_IF_EXISTS;

  using std::iswalnum _LIBCPP_USING_IF_EXISTS;
  using std::iswalpha _LIBCPP_USING_IF_EXISTS;
  using std::iswblank _LIBCPP_USING_IF_EXISTS;
  using std::iswcntrl _LIBCPP_USING_IF_EXISTS;
  using std::iswctype _LIBCPP_USING_IF_EXISTS;
  using std::iswdigit _LIBCPP_USING_IF_EXISTS;
  using std::iswgraph _LIBCPP_USING_IF_EXISTS;
  using std::iswlower _LIBCPP_USING_IF_EXISTS;
  using std::iswprint _LIBCPP_USING_IF_EXISTS;
  using std::iswpunct _LIBCPP_USING_IF_EXISTS;
  using std::iswspace _LIBCPP_USING_IF_EXISTS;
  using std::iswupper _LIBCPP_USING_IF_EXISTS;
  using std::iswxdigit _LIBCPP_USING_IF_EXISTS;
  using std::towctrans _LIBCPP_USING_IF_EXISTS;
  using std::towlower _LIBCPP_USING_IF_EXISTS;
  using std::towupper _LIBCPP_USING_IF_EXISTS;
  using std::wctrans _LIBCPP_USING_IF_EXISTS;
  using std::wctype _LIBCPP_USING_IF_EXISTS;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS
} // namespace std
