// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#if !defined(_LIBCPP_HAS_NO_LOCALIZATION) && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_SYNCSTREAM)
  using std::basic_syncbuf;

  // [syncstream.syncbuf.special], specialized algorithms
  using std::swap;

  using std::syncbuf;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wsyncbuf;
#  endif
  using std::basic_osyncstream;

  using std::osyncstream;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wosyncstream;
#  endif
#endif // !defined(_LIBCPP_HAS_NO_LOCALIZATION) && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_SYNCSTREAM)
} // namespace std
