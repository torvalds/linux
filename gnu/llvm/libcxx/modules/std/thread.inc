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
  // [thread.thread.class], class thread
  using std::thread;

  using std::swap;

  // [thread.jthread.class], class jthread
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN)
  using std::jthread;
#  endif

  // [thread.thread.this], namespace this_thread
  namespace this_thread {
    using std::this_thread::get_id;

    using std::this_thread::sleep_for;
    using std::this_thread::sleep_until;
    using std::this_thread::yield;
  } // namespace this_thread

  // [thread.thread.id]
  using std::operator==;
  using std::operator<=>;
#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
  using std::operator<<;
#  endif // _LIBCPP_HAS_NO_LOCALIZATION

#  if _LIBCPP_STD_VER >= 23
  using std::formatter;
#  endif

  using std::hash;
#endif // _LIBCPP_HAS_NO_THREADS
} // namespace std
