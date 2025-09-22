//===-- DNBTimer.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 12/13/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBTIMER_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBTIMER_H

#include "DNBDefs.h"
#include "PThreadMutex.h"
#include <cstdint>
#include <memory>
#include <sys/time.h>

class DNBTimer {
public:
  // Constructors and Destructors
  DNBTimer(bool threadSafe) : m_mutexAP() {
    if (threadSafe)
      m_mutexAP.reset(new PThreadMutex(PTHREAD_MUTEX_RECURSIVE));
    Reset();
  }

  DNBTimer(const DNBTimer &rhs) : m_mutexAP() {
    // Create a new mutex to make this timer thread safe as well if
    // the timer we are copying is thread safe
    if (rhs.IsThreadSafe())
      m_mutexAP.reset(new PThreadMutex(PTHREAD_MUTEX_RECURSIVE));
    m_timeval = rhs.m_timeval;
  }

  DNBTimer &operator=(const DNBTimer &rhs) {
    // Create a new mutex to make this timer thread safe as well if
    // the timer we are copying is thread safe
    if (rhs.IsThreadSafe())
      m_mutexAP.reset(new PThreadMutex(PTHREAD_MUTEX_RECURSIVE));
    m_timeval = rhs.m_timeval;
    return *this;
  }

  ~DNBTimer() {}

  bool IsThreadSafe() const { return m_mutexAP.get() != NULL; }
  // Reset the time value to now
  void Reset() {
    PTHREAD_MUTEX_LOCKER(locker, m_mutexAP.get());
    gettimeofday(&m_timeval, NULL);
  }
  // Get the total microseconds since Jan 1, 1970
  uint64_t TotalMicroSeconds() const {
    PTHREAD_MUTEX_LOCKER(locker, m_mutexAP.get());
    return (uint64_t)(m_timeval.tv_sec) * 1000000ull +
           (uint64_t)m_timeval.tv_usec;
  }

  void GetTime(uint64_t &sec, uint32_t &usec) const {
    PTHREAD_MUTEX_LOCKER(locker, m_mutexAP.get());
    sec = m_timeval.tv_sec;
    usec = m_timeval.tv_usec;
  }
  // Return the number of microseconds elapsed between now and the
  // m_timeval
  uint64_t ElapsedMicroSeconds(bool update) {
    PTHREAD_MUTEX_LOCKER(locker, m_mutexAP.get());
    struct timeval now;
    gettimeofday(&now, NULL);
    uint64_t now_usec =
        (uint64_t)(now.tv_sec) * 1000000ull + (uint64_t)now.tv_usec;
    uint64_t this_usec =
        (uint64_t)(m_timeval.tv_sec) * 1000000ull + (uint64_t)m_timeval.tv_usec;
    uint64_t elapsed = now_usec - this_usec;
    // Update the timer time value if requeseted
    if (update)
      m_timeval = now;
    return elapsed;
  }

  static uint64_t GetTimeOfDay() {
    struct timeval now;
    gettimeofday(&now, NULL);
    uint64_t now_usec =
        (uint64_t)(now.tv_sec) * 1000000ull + (uint64_t)now.tv_usec;
    return now_usec;
  }

  static void OffsetTimeOfDay(struct timespec *ts,
                              __darwin_time_t sec_offset = 0,
                              long nsec_offset = 0) {
    if (ts == NULL)
      return;
    // Get the current time in a timeval structure
    struct timeval now;
    gettimeofday(&now, NULL);
    // Morph it into a timespec
    TIMEVAL_TO_TIMESPEC(&now, ts);
    // Offset the timespec if requested
    if (sec_offset != 0 || nsec_offset != 0) {
      // Offset the nano seconds
      ts->tv_nsec += nsec_offset;
      // Offset the seconds taking into account a nano-second overflow
      ts->tv_sec = ts->tv_sec + ts->tv_nsec / 1000000000 + sec_offset;
      // Trim the nanoseconds back there was an overflow
      ts->tv_nsec = ts->tv_nsec % 1000000000;
    }
  }
  static bool TimeOfDayLaterThan(struct timespec &ts) {
    struct timespec now;
    OffsetTimeOfDay(&now);
    if (now.tv_sec > ts.tv_sec)
      return true;
    else if (now.tv_sec < ts.tv_sec)
      return false;
    else {
      if (now.tv_nsec > ts.tv_nsec)
        return true;
      else
        return false;
    }
  }

protected:
  // Classes that inherit from DNBTimer can see and modify these
  std::unique_ptr<PThreadMutex> m_mutexAP;
  struct timeval m_timeval;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBTIMER_H
