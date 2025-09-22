// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  using std::bad_exception;
  using std::current_exception;
  using std::exception;
  using std::exception_ptr;
  using std::get_terminate;
  using std::make_exception_ptr;
  using std::nested_exception;
  using std::rethrow_exception;
  using std::rethrow_if_nested;
  using std::set_terminate;
  using std::terminate;
  using std::terminate_handler;
  using std::throw_with_nested;
  using std::uncaught_exception;
  using std::uncaught_exceptions;
} // namespace std
