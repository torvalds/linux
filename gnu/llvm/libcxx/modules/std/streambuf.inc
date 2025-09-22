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
  using std::basic_streambuf;
  using std::streambuf;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wstreambuf;
#  endif
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
