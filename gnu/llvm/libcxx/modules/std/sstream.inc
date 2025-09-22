// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#ifndef _LIBCPP_HAS_NO_LOCALIZATION
  using std::basic_stringbuf;

  using std::swap;

  using std::stringbuf;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wstringbuf;
#  endif

  using std::basic_istringstream;

  using std::istringstream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wistringstream;
#  endif

  using std::basic_ostringstream;

  using std::ostringstream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wostringstream;
#  endif

  using std::basic_stringstream;

  using std::stringstream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wstringstream;
#  endif
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
