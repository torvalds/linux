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
  using ::wctrans_t _LIBCPP_USING_IF_EXISTS;
  using ::wctype_t _LIBCPP_USING_IF_EXISTS;
  using ::wint_t _LIBCPP_USING_IF_EXISTS;

  using ::iswalnum _LIBCPP_USING_IF_EXISTS;
  using ::iswalpha _LIBCPP_USING_IF_EXISTS;
  using ::iswblank _LIBCPP_USING_IF_EXISTS;
  using ::iswcntrl _LIBCPP_USING_IF_EXISTS;
  using ::iswctype _LIBCPP_USING_IF_EXISTS;
  using ::iswdigit _LIBCPP_USING_IF_EXISTS;
  using ::iswgraph _LIBCPP_USING_IF_EXISTS;
  using ::iswlower _LIBCPP_USING_IF_EXISTS;
  using ::iswprint _LIBCPP_USING_IF_EXISTS;
  using ::iswpunct _LIBCPP_USING_IF_EXISTS;
  using ::iswspace _LIBCPP_USING_IF_EXISTS;
  using ::iswupper _LIBCPP_USING_IF_EXISTS;
  using ::iswxdigit _LIBCPP_USING_IF_EXISTS;
  using ::towctrans _LIBCPP_USING_IF_EXISTS;
  using ::towlower _LIBCPP_USING_IF_EXISTS;
  using ::towupper _LIBCPP_USING_IF_EXISTS;
  using ::wctrans _LIBCPP_USING_IF_EXISTS;
  using ::wctype _LIBCPP_USING_IF_EXISTS;
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS
} // export
