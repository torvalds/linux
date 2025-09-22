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
  using std::basic_istream;

  using std::istream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wistream;
#  endif

  using std::basic_iostream;

  using std::iostream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wiostream;
#  endif

  using std::ws;

  using std::operator>>;
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
