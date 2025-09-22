// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export {
  // types
  using ::fenv_t _LIBCPP_USING_IF_EXISTS;
  using ::fexcept_t _LIBCPP_USING_IF_EXISTS;

  // functions
  using ::feclearexcept _LIBCPP_USING_IF_EXISTS;
  using ::fegetexceptflag _LIBCPP_USING_IF_EXISTS;
  using ::feraiseexcept _LIBCPP_USING_IF_EXISTS;
  using ::fesetexceptflag _LIBCPP_USING_IF_EXISTS;
  using ::fetestexcept _LIBCPP_USING_IF_EXISTS;

  using ::fegetround _LIBCPP_USING_IF_EXISTS;
  using ::fesetround _LIBCPP_USING_IF_EXISTS;

  using ::fegetenv _LIBCPP_USING_IF_EXISTS;
  using ::feholdexcept _LIBCPP_USING_IF_EXISTS;
  using ::fesetenv _LIBCPP_USING_IF_EXISTS;
  using ::feupdateenv _LIBCPP_USING_IF_EXISTS;
} // export
