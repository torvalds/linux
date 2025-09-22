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
#  if _LIBCPP_STD_VER < 26 || defined(_LIBCPP_ENABLE_CXX26_REMOVED_CODECVT)
  using std::codecvt_mode;

  using std::codecvt_utf16;
  using std::codecvt_utf8;
  using std::codecvt_utf8_utf16;
#  endif // _LIBCPP_STD_VER < 26 || defined(_LIBCPP_ENABLE_CXX26_REMOVED_CODECVT)
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
