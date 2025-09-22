//===-- sanitizer_pthread_wrappers.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of *Sanitizer runtime.
// It provides handy wrappers for thread manipulation, that:
//  a) assert on any failure rather than returning an error code
//  b) defines pthread-like interface on platforms where where <pthread.h>
//     is not supplied by default.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_PTHREAD_WRAPPERS_H
#define SANITIZER_PTHREAD_WRAPPERS_H

#include "sanitizer_test_utils.h"

#if !defined(_WIN32)
# include <pthread.h>
// Simply forward the arguments and check that the pthread functions succeed.
# define PTHREAD_CREATE(a, b, c, d) ASSERT_EQ(0, pthread_create(a, b, c, d))
# define PTHREAD_JOIN(a, b) ASSERT_EQ(0, pthread_join(a, b))
#else
typedef HANDLE pthread_t;

struct PthreadHelperCreateThreadInfo {
  void *(*start_routine)(void *);
  void *arg;
};

inline DWORD WINAPI PthreadHelperThreadProc(void *arg) {
  PthreadHelperCreateThreadInfo *start_data =
      reinterpret_cast<PthreadHelperCreateThreadInfo*>(arg);
  (start_data->start_routine)(start_data->arg);
  delete start_data;
  return 0;
}

inline void PTHREAD_CREATE(pthread_t *thread, void *attr,
                           void *(*start_routine)(void *), void *arg) {
  ASSERT_EQ(0, attr) << "Thread attributes are not supported yet.";
  PthreadHelperCreateThreadInfo *data = new PthreadHelperCreateThreadInfo;
  data->start_routine = start_routine;
  data->arg = arg;
  *thread = CreateThread(0, 0, PthreadHelperThreadProc, data, 0, 0);
  DWORD err = GetLastError();
  ASSERT_NE(nullptr, *thread) << "Failed to create a thread, got error 0x"
                              << std::hex << err;
}

inline void PTHREAD_JOIN(pthread_t thread, void **value_ptr) {
  ASSERT_EQ(0, value_ptr) << "Nonzero value_ptr is not supported yet.";
  ASSERT_EQ(WAIT_OBJECT_0, WaitForSingleObject(thread, INFINITE));
  ASSERT_NE(0, CloseHandle(thread));
}

inline void pthread_exit(void *retval) {
  ASSERT_EQ(0, retval) << "Nonzero retval is not supported yet.";
  ExitThread(0);
}
#endif  // _WIN32

#endif  // SANITIZER_PTHREAD_WRAPPERS_H
