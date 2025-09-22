// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  using std::imaxdiv_t _LIBCPP_USING_IF_EXISTS;

  using std::imaxabs _LIBCPP_USING_IF_EXISTS;
  using std::imaxdiv _LIBCPP_USING_IF_EXISTS;
  using std::strtoimax _LIBCPP_USING_IF_EXISTS;
  using std::strtoumax _LIBCPP_USING_IF_EXISTS;
  using std::wcstoimax _LIBCPP_USING_IF_EXISTS;
  using std::wcstoumax _LIBCPP_USING_IF_EXISTS;

  // abs is conditionally here, but always present in cmath.cppm. To avoid
  // conflicing declarations omit the using here.

  // div is conditionally here, but always present in cstdlib.cppm. To avoid
  // conflicing declarations omit the using here.
} // namespace std
