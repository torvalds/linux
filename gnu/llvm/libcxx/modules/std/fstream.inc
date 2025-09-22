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
  using std::basic_filebuf;

#  ifndef _LIBCPP_HAS_NO_FILESYSTEM
  using std::swap;
#  endif

  using std::filebuf;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wfilebuf;
#  endif

  using std::basic_ifstream;

  using std::ifstream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wifstream;
#  endif

  using std::basic_ofstream;

  using std::ofstream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wofstream;
#  endif

  using std::basic_fstream;

  using std::fstream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wfstream;
#  endif
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
