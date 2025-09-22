//===-- Alarm.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Alarm.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

Alarm::Alarm(Duration timeout, bool run_callback_on_exit)
    : m_timeout(timeout), m_run_callbacks_on_exit(run_callback_on_exit) {
  StartAlarmThread();
}

Alarm::~Alarm() { StopAlarmThread(); }

Alarm::Handle Alarm::Create(std::function<void()> callback) {
  // Gracefully deal with the unlikely event that the alarm thread failed to
  // launch.
  if (!AlarmThreadRunning())
    return INVALID_HANDLE;

  // Compute the next expiration before we take the lock. This ensures that
  // waiting on the lock doesn't eat into the timeout.
  const TimePoint expiration = GetNextExpiration();

  Handle handle = INVALID_HANDLE;

  {
    std::lock_guard<std::mutex> alarm_guard(m_alarm_mutex);

    // Create a new unique entry and remember its handle.
    m_entries.emplace_back(callback, expiration);
    handle = m_entries.back().handle;

    // Tell the alarm thread we need to recompute the next alarm.
    m_recompute_next_alarm = true;
  }

  m_alarm_cv.notify_one();
  return handle;
}

bool Alarm::Restart(Handle handle) {
  // Gracefully deal with the unlikely event that the alarm thread failed to
  // launch.
  if (!AlarmThreadRunning())
    return false;

  // Compute the next expiration before we take the lock. This ensures that
  // waiting on the lock doesn't eat into the timeout.
  const TimePoint expiration = GetNextExpiration();

  {
    std::lock_guard<std::mutex> alarm_guard(m_alarm_mutex);

    // Find the entry corresponding to the given handle.
    const auto it =
        std::find_if(m_entries.begin(), m_entries.end(),
                     [handle](Entry &entry) { return entry.handle == handle; });
    if (it == m_entries.end())
      return false;

    // Update the expiration.
    it->expiration = expiration;

    // Tell the alarm thread we need to recompute the next alarm.
    m_recompute_next_alarm = true;
  }

  m_alarm_cv.notify_one();
  return true;
}

bool Alarm::Cancel(Handle handle) {
  // Gracefully deal with the unlikely event that the alarm thread failed to
  // launch.
  if (!AlarmThreadRunning())
    return false;

  {
    std::lock_guard<std::mutex> alarm_guard(m_alarm_mutex);

    const auto it =
        std::find_if(m_entries.begin(), m_entries.end(),
                     [handle](Entry &entry) { return entry.handle == handle; });

    if (it == m_entries.end())
      return false;

    m_entries.erase(it);
  }

  // No need to notify the alarm thread. This only affects the alarm thread if
  // we removed the entry that corresponds to the next alarm. If that's the
  // case, the thread will wake up as scheduled, find no expired events, and
  // recompute the next alarm time.
  return true;
}

Alarm::Entry::Entry(Alarm::Callback callback, Alarm::TimePoint expiration)
    : handle(Alarm::GetNextUniqueHandle()), callback(std::move(callback)),
      expiration(std::move(expiration)) {}

void Alarm::StartAlarmThread() {
  if (!m_alarm_thread.IsJoinable()) {
    llvm::Expected<HostThread> alarm_thread = ThreadLauncher::LaunchThread(
        "lldb.debugger.alarm-thread", [this] { return AlarmThread(); },
        8 * 1024 * 1024); // Use larger 8MB stack for this thread
    if (alarm_thread) {
      m_alarm_thread = *alarm_thread;
    } else {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Host), alarm_thread.takeError(),
                     "failed to launch host thread: {0}");
    }
  }
}

void Alarm::StopAlarmThread() {
  if (m_alarm_thread.IsJoinable()) {
    {
      std::lock_guard<std::mutex> alarm_guard(m_alarm_mutex);
      m_exit = true;
    }
    m_alarm_cv.notify_one();
    m_alarm_thread.Join(nullptr);
  }
}

bool Alarm::AlarmThreadRunning() { return m_alarm_thread.IsJoinable(); }

lldb::thread_result_t Alarm::AlarmThread() {
  bool exit = false;
  std::optional<TimePoint> next_alarm;

  const auto predicate = [this] { return m_exit || m_recompute_next_alarm; };

  while (!exit) {
    // Synchronization between the main thread and the alarm thread using a
    // mutex and condition variable. There are 2 reasons the thread can wake up:
    //
    // 1. The timeout for the next alarm expired.
    //
    // 2. The condition variable is notified that one of our shared variables
    //    (see predicate) was modified. Either the thread is asked to shut down
    //    or a new alarm came in and we need to recompute the next timeout.
    //
    // Below we only deal with the timeout expiring and fall through for dealing
    // with the rest.
    llvm::SmallVector<Callback, 1> callbacks;
    {
      std::unique_lock<std::mutex> alarm_lock(m_alarm_mutex);
      if (next_alarm) {
        if (!m_alarm_cv.wait_until(alarm_lock, *next_alarm, predicate)) {
          // The timeout for the next alarm expired.

          // Clear the next timeout to signal that we need to recompute the next
          // timeout.
          next_alarm.reset();

          // Iterate over all the callbacks. Call the ones that have expired
          // and remove them from the list.
          const TimePoint now = std::chrono::system_clock::now();
          auto it = m_entries.begin();
          while (it != m_entries.end()) {
            if (it->expiration <= now) {
              callbacks.emplace_back(std::move(it->callback));
              it = m_entries.erase(it);
            } else {
              it++;
            }
          }
        }
      } else {
        m_alarm_cv.wait(alarm_lock, predicate);
      }

      // Fall through after waiting on the condition variable. At this point
      // either the predicate is true or we woke up because an alarm expired.

      // The alarm thread is shutting down.
      if (m_exit) {
        exit = true;
        if (m_run_callbacks_on_exit) {
          for (Entry &entry : m_entries)
            callbacks.emplace_back(std::move(entry.callback));
        }
      }

      // A new alarm was added or an alarm expired. Either way we need to
      // recompute when this thread should wake up for the next alarm.
      if (m_recompute_next_alarm || !next_alarm) {
        for (Entry &entry : m_entries) {
          if (!next_alarm || entry.expiration < *next_alarm)
            next_alarm = entry.expiration;
        }
        m_recompute_next_alarm = false;
      }
    }

    // Outside the lock, call the callbacks.
    for (Callback &callback : callbacks)
      callback();
  }
  return {};
}

Alarm::TimePoint Alarm::GetNextExpiration() const {
  return std::chrono::system_clock::now() + m_timeout;
}

Alarm::Handle Alarm::GetNextUniqueHandle() {
  static std::atomic<Handle> g_next_handle = 1;
  return g_next_handle++;
}
