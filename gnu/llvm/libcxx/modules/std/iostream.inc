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
  using std::cerr;
  using std::cin;
  using std::clog;
  using std::cout;

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wcerr;
  using std::wcin;
  using std::wclog;
  using std::wcout;
#  endif
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
