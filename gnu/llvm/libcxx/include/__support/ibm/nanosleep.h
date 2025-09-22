// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SUPPORT_IBM_NANOSLEEP_H
#define _LIBCPP___SUPPORT_IBM_NANOSLEEP_H

#include <unistd.h>

inline int nanosleep(const struct timespec* __req, struct timespec* __rem) {
  // The nanosleep() function is not available on z/OS. Therefore, we will call
  // sleep() to sleep for whole seconds and usleep() to sleep for any remaining
  // fraction of a second. Any remaining nanoseconds will round up to the next
  // microsecond.
  if (__req->tv_sec < 0 || __req->tv_nsec < 0 || __req->tv_nsec > 999999999) {
    errno = EINVAL;
    return -1;
  }
  long __micro_sec = (__req->tv_nsec + 999) / 1000;
  time_t __sec     = __req->tv_sec;
  if (__micro_sec > 999999) {
    ++__sec;
    __micro_sec -= 1000000;
  }
  __sec = static_cast<time_t>(sleep(static_cast<unsigned int>(__sec)));
  if (__sec) {
    if (__rem) {
      // Updating the remaining time to sleep in case of unsuccessful call to sleep().
      __rem->tv_sec  = __sec;
      __rem->tv_nsec = __micro_sec * 1000;
    }
    errno = EINTR;
    return -1;
  }
  if (__micro_sec) {
    int __rt = usleep(static_cast<unsigned int>(__micro_sec));
    if (__rt != 0 && __rem) {
      // The usleep() does not provide the amount of remaining time upon its failure,
      // so the time slept will be ignored.
      __rem->tv_sec  = 0;
      __rem->tv_nsec = __micro_sec * 1000;
      // The errno is already set.
      return -1;
    }
    return __rt;
  }
  return 0;
}

#endif // _LIBCPP___SUPPORT_IBM_NANOSLEEP_H
