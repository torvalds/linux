// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  using std::error_category;
  using std::generic_category;
  using std::system_category;

  using std::error_code;
  using std::error_condition;
  using std::system_error;

  using std::is_error_code_enum;
  using std::is_error_condition_enum;

  using std::errc;

  // [syserr.errcode.nonmembers], non-member functions
  using std::make_error_code;

  using std::operator<<;

  // [syserr.errcondition.nonmembers], non-member functions
  using std::make_error_condition;

  // [syserr.compare], comparison operator functions
  using std::operator==;
  using std::operator<=>;

  // [syserr.hash], hash support
  using std::hash;

  // [syserr], system error support
  using std::is_error_code_enum_v;
  using std::is_error_condition_enum_v;
} // namespace std
