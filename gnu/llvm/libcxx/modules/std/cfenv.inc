// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // types
  using std::fenv_t _LIBCPP_USING_IF_EXISTS;
  using std::fexcept_t _LIBCPP_USING_IF_EXISTS;

  // functions
  using std::feclearexcept _LIBCPP_USING_IF_EXISTS;
  using std::fegetexceptflag _LIBCPP_USING_IF_EXISTS;
  using std::feraiseexcept _LIBCPP_USING_IF_EXISTS;
  using std::fesetexceptflag _LIBCPP_USING_IF_EXISTS;
  using std::fetestexcept _LIBCPP_USING_IF_EXISTS;

  using std::fegetround _LIBCPP_USING_IF_EXISTS;
  using std::fesetround _LIBCPP_USING_IF_EXISTS;

  using std::fegetenv _LIBCPP_USING_IF_EXISTS;
  using std::feholdexcept _LIBCPP_USING_IF_EXISTS;
  using std::fesetenv _LIBCPP_USING_IF_EXISTS;
  using std::feupdateenv _LIBCPP_USING_IF_EXISTS;

} // namespace std
