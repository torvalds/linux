//===-- PThreadMutex.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/16/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_PTHREADMUTEX_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_PTHREADMUTEX_H

#include <cassert>
#include <cstdint>
#include <pthread.h>

//#define DEBUG_PTHREAD_MUTEX_DEADLOCKS 1

#if defined(DEBUG_PTHREAD_MUTEX_DEADLOCKS)
#define PTHREAD_MUTEX_LOCKER(var, mutex)                                       \
  PThreadMutex::Locker var(mutex, __FUNCTION__, __FILE__, __LINE__)

#else
#define PTHREAD_MUTEX_LOCKER(var, mutex) PThreadMutex::Locker var(mutex)
#endif

class PThreadMutex {
public:
  class Locker {
  public:
#if defined(DEBUG_PTHREAD_MUTEX_DEADLOCKS)

    Locker(PThreadMutex &m, const char *function, const char *file, int line);
    Locker(PThreadMutex *m, const char *function, const char *file, int line);
    Locker(pthread_mutex_t *mutex, const char *function, const char *file,
           int line);
    ~Locker();
    void Lock();
    void Unlock();

#else
    Locker(PThreadMutex &m) : m_pMutex(m.Mutex()) { Lock(); }

    Locker(PThreadMutex *m) : m_pMutex(m ? m->Mutex() : NULL) { Lock(); }

    Locker(pthread_mutex_t *mutex) : m_pMutex(mutex) { Lock(); }

    void Lock() {
      if (m_pMutex)
        ::pthread_mutex_lock(m_pMutex);
    }

    void Unlock() {
      if (m_pMutex)
        ::pthread_mutex_unlock(m_pMutex);
    }

    ~Locker() { Unlock(); }

#endif

    // unlock any the current mutex and lock the new one if it is valid
    void Reset(pthread_mutex_t *pMutex = NULL) {
      Unlock();
      m_pMutex = pMutex;
      Lock();
    }
    pthread_mutex_t *m_pMutex;
#if defined(DEBUG_PTHREAD_MUTEX_DEADLOCKS)
    const char *m_function;
    const char *m_file;
    int m_line;
    uint64_t m_lock_time;
#endif
  };

  PThreadMutex() {
    [[maybe_unused]] int err;
    err = ::pthread_mutex_init(&m_mutex, NULL);
    assert(err == 0);
  }

  PThreadMutex(int type) {
    [[maybe_unused]] int err;
    ::pthread_mutexattr_t attr;
    err = ::pthread_mutexattr_init(&attr);
    assert(err == 0);
    err = ::pthread_mutexattr_settype(&attr, type);
    assert(err == 0);
    err = ::pthread_mutex_init(&m_mutex, &attr);
    assert(err == 0);
    err = ::pthread_mutexattr_destroy(&attr);
    assert(err == 0);
  }

  ~PThreadMutex() {
    [[maybe_unused]] int err;
    err = ::pthread_mutex_destroy(&m_mutex);
    if (err != 0) {
      err = Unlock();
      if (err == 0)
        ::pthread_mutex_destroy(&m_mutex);
    }
  }

  pthread_mutex_t *Mutex() { return &m_mutex; }

  int Lock() { return ::pthread_mutex_lock(&m_mutex); }

  int Unlock() { return ::pthread_mutex_unlock(&m_mutex); }

protected:
  pthread_mutex_t m_mutex;
};

#endif
