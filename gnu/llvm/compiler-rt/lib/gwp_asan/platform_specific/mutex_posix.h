//===-- mutex_posix.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__unix__)
#ifndef GWP_ASAN_MUTEX_POSIX_H_
#define GWP_ASAN_MUTEX_POSIX_H_

#include <pthread.h>

namespace gwp_asan {
class PlatformMutex {
protected:
  pthread_mutex_t Mu = PTHREAD_MUTEX_INITIALIZER;
};
} // namespace gwp_asan

#endif // GWP_ASAN_MUTEX_POSIX_H_
#endif // defined(__unix__)
