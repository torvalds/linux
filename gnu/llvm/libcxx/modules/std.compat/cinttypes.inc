// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export {
  using ::imaxdiv_t _LIBCPP_USING_IF_EXISTS;

  using ::imaxabs _LIBCPP_USING_IF_EXISTS;
  using ::imaxdiv _LIBCPP_USING_IF_EXISTS;
  using ::strtoimax _LIBCPP_USING_IF_EXISTS;
  using ::strtoumax _LIBCPP_USING_IF_EXISTS;
  using ::wcstoimax _LIBCPP_USING_IF_EXISTS;
  using ::wcstoumax _LIBCPP_USING_IF_EXISTS;

  // abs is conditionally here, but always present in cmath.cppm. To avoid
  // conflicing declarations omit the using here.

  // div is conditionally here, but always present in cstdlib.cppm. To avoid
  // conflicing declarations omit the using here.
} // export
