// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // Note the Standard does not mark these symbols optional, but libc++'s header
  // does. So this seems strictly not to be conforming.

  // mbstate_t is conditionally here, but always present in cwchar.cppm. To avoid
  // conflicing declarations omit the using here.

  // size_t is conditionally here, but always present in cstddef.cppm. To avoid
  // conflicing declarations omit the using here.

#if !defined(_LIBCPP_HAS_NO_C8RTOMB_MBRTOC8)
  using std::mbrtoc8 _LIBCPP_USING_IF_EXISTS;
  using std::c8rtomb _LIBCPP_USING_IF_EXISTS;
#endif
  using std::mbrtoc16 _LIBCPP_USING_IF_EXISTS;
  using std::c16rtomb _LIBCPP_USING_IF_EXISTS;
  using std::mbrtoc32 _LIBCPP_USING_IF_EXISTS;
  using std::c32rtomb _LIBCPP_USING_IF_EXISTS;
} // namespace std
