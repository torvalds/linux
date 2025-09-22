// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SUPPORT_IBM_GETTOD_ZOS_H
#define _LIBCPP___SUPPORT_IBM_GETTOD_ZOS_H

#include <time.h>

inline _LIBCPP_HIDE_FROM_ABI int gettimeofdayMonotonic(struct timespec64* Output) {
  // The POSIX gettimeofday() function is not available on z/OS. Therefore,
  // we will call stcke and other hardware instructions in implement equivalent.
  // Note that nanoseconds alone will overflow when reaching new epoch in 2042.

  struct _t {
    uint64_t Hi;
    uint64_t Lo;
  };
  struct _t Value = {0, 0};
  uint64_t CC     = 0;
  asm(" stcke %0\n"
      " ipm %1\n"
      " srlg %1,%1,28\n"
      : "=m"(Value), "+r"(CC)::);

  if (CC != 0) {
    errno = EMVSTODNOTSET;
    return CC;
  }
  uint64_t us = (Value.Hi >> 4);
  uint64_t ns = ((Value.Hi & 0x0F) << 8) + (Value.Lo >> 56);
  ns          = (ns * 1000) >> 12;
  us          = us - 2208988800000000;

  register uint64_t DivPair0 asm("r0"); // dividend (upper half), remainder
  DivPair0 = 0;
  register uint64_t DivPair1 asm("r1"); // dividend (lower half), quotient
  DivPair1         = us;
  uint64_t Divisor = 1000000;
  asm(" dlgr %0,%2" : "+r"(DivPair0), "+r"(DivPair1) : "r"(Divisor) :);

  Output->tv_sec  = DivPair1;
  Output->tv_nsec = DivPair0 * 1000 + ns;
  return 0;
}

#endif // _LIBCPP___SUPPORT_IBM_GETTOD_ZOS_H
