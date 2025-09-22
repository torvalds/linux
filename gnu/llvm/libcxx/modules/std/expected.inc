// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#if _LIBCPP_STD_VER >= 23
  // [expected.unexpected], class template unexpected
  using std::unexpected;

  // [expected.bad], class template bad_expected_access
  using std::bad_expected_access;

  // in-place construction of unexpected values
  using std::unexpect;
  using std::unexpect_t;

  // [expected.expected], class template expected
  using std::expected;
#endif // _LIBCPP_STD_VER >= 23
} // namespace std
