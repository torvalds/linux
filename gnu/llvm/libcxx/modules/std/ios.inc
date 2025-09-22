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
  using std::fpos;
  // based on [tab:fpos.operations]
  using std::operator!=; // Note not affected by P1614, seems like a bug.
  using std::operator-;
  using std::operator==;

  using std::streamoff;
  using std::streamsize;

  using std::basic_ios;
  using std::ios_base;

  // [std.ios.manip], manipulators
  using std::boolalpha;
  using std::noboolalpha;

  using std::noshowbase;
  using std::showbase;

  using std::noshowpoint;
  using std::showpoint;

  using std::noshowpos;
  using std::showpos;

  using std::noskipws;
  using std::skipws;

  using std::nouppercase;
  using std::uppercase;

  using std::nounitbuf;
  using std::unitbuf;

  // [adjustfield.manip], adjustfield
  using std::internal;
  using std::left;
  using std::right;

  // [basefield.manip], basefield
  using std::dec;
  using std::hex;
  using std::oct;

  // [floatfield.manip], floatfield
  using std::defaultfloat;
  using std::fixed;
  using std::hexfloat;
  using std::scientific;

  // [error.reporting], error reporting
  using std::io_errc;

  using std::iostream_category;
  using std::is_error_code_enum;
  using std::make_error_code;
  using std::make_error_condition;

  // [iosfwd.syn]
  using std::ios;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wios;
#  endif
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
