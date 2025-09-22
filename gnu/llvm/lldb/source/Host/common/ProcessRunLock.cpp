//===-- ProcessRunLock.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _WIN32
#include "lldb/Host/ProcessRunLock.h"

namespace lldb_private {

ProcessRunLock::ProcessRunLock() {
  int err = ::pthread_rwlock_init(&m_rwlock, nullptr);
  (void)err;
}

ProcessRunLock::~ProcessRunLock() {
  int err = ::pthread_rwlock_destroy(&m_rwlock);
  (void)err;
}

bool ProcessRunLock::ReadTryLock() {
  ::pthread_rwlock_rdlock(&m_rwlock);
  if (!m_running) {
    // coverity[missing_unlock]
    return true;
  }
  ::pthread_rwlock_unlock(&m_rwlock);
  return false;
}

bool ProcessRunLock::ReadUnlock() {
  return ::pthread_rwlock_unlock(&m_rwlock) == 0;
}

bool ProcessRunLock::SetRunning() {
  ::pthread_rwlock_wrlock(&m_rwlock);
  m_running = true;
  ::pthread_rwlock_unlock(&m_rwlock);
  return true;
}

bool ProcessRunLock::TrySetRunning() {
  bool r;

  if (::pthread_rwlock_trywrlock(&m_rwlock) == 0) {
    r = !m_running;
    m_running = true;
    ::pthread_rwlock_unlock(&m_rwlock);
    return r;
  }
  return false;
}

bool ProcessRunLock::SetStopped() {
  ::pthread_rwlock_wrlock(&m_rwlock);
  m_running = false;
  ::pthread_rwlock_unlock(&m_rwlock);
  return true;
}
}

#endif
