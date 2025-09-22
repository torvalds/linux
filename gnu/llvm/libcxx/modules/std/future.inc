// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#ifndef _LIBCPP_HAS_NO_THREADS
  using std::future_errc;
  using std::future_status;
  using std::launch;

  // launch is a bitmask type.
  // [bitmask.types] specified operators
  using std::operator&;
  using std::operator&=;
  using std::operator^;
  using std::operator^=;
  using std::operator|;
  using std::operator|=;
  using std::operator~;

  // [futures.errors], error handling
  using std::is_error_code_enum;
  using std::make_error_code;
  using std::make_error_condition;

  using std::future_category;

  // [futures.future.error], class future_error
  using std::future_error;

  // [futures.promise], class template promise
  using std::promise;

  using std::swap;

  using std::uses_allocator;

  // [futures.unique.future], class template future
  using std::future;

  // [futures.shared.future], class template shared_future
  using std::shared_future;

  // [futures.task], class template packaged_task
  using std::packaged_task;

  // [futures.async], function template async
  using std::async;
#endif // _LIBCPP_HAS_NO_THREADS
} // namespace std
