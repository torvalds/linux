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
  // [thread.condition.condvar], class condition_variable
  using std::condition_variable;
  // [thread.condition.condvarany], class condition_variable_any
  using std::condition_variable_any;

  // [thread.condition.nonmember], non-member functions
  using std::notify_all_at_thread_exit;

  using std::cv_status;
#endif // _LIBCPP_HAS_NO_THREADS
} // namespace std
