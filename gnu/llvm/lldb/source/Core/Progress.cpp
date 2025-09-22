//===-- Progress.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Progress.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Utility/StreamString.h"

#include <cstdint>
#include <mutex>
#include <optional>

using namespace lldb;
using namespace lldb_private;

std::atomic<uint64_t> Progress::g_id(0);

Progress::Progress(std::string title, std::string details,
                   std::optional<uint64_t> total,
                   lldb_private::Debugger *debugger)
    : m_details(details), m_completed(0),
      m_total(Progress::kNonDeterministicTotal),
      m_progress_data{title, ++g_id,
                      /*m_progress_data.debugger_id=*/std::nullopt} {
  if (total)
    m_total = *total;

  if (debugger)
    m_progress_data.debugger_id = debugger->GetID();

  std::lock_guard<std::mutex> guard(m_mutex);
  ReportProgress();

  // Report to the ProgressManager if that subsystem is enabled.
  if (ProgressManager::Enabled())
    ProgressManager::Instance().Increment(m_progress_data);
}

Progress::~Progress() {
  // Make sure to always report progress completed when this object is
  // destructed so it indicates the progress dialog/activity should go away.
  std::lock_guard<std::mutex> guard(m_mutex);
  if (!m_completed)
    m_completed = m_total;
  ReportProgress();

  // Report to the ProgressManager if that subsystem is enabled.
  if (ProgressManager::Enabled())
    ProgressManager::Instance().Decrement(m_progress_data);
}

void Progress::Increment(uint64_t amount,
                         std::optional<std::string> updated_detail) {
  if (amount > 0) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (updated_detail)
      m_details = std::move(updated_detail.value());
    // Watch out for unsigned overflow and make sure we don't increment too
    // much and exceed the total.
    if (m_total && (amount > (m_total - m_completed)))
      m_completed = m_total;
    else
      m_completed += amount;
    ReportProgress();
  }
}

void Progress::ReportProgress() {
  if (!m_complete) {
    // Make sure we only send one notification that indicates the progress is
    // complete
    m_complete = m_completed == m_total;
    Debugger::ReportProgress(m_progress_data.progress_id, m_progress_data.title,
                             m_details, m_completed, m_total,
                             m_progress_data.debugger_id);
  }
}

ProgressManager::ProgressManager()
    : m_entries(), m_alarm(std::chrono::milliseconds(100)) {}

ProgressManager::~ProgressManager() {}

void ProgressManager::Initialize() {
  assert(!InstanceImpl() && "Already initialized.");
  InstanceImpl().emplace();
}

void ProgressManager::Terminate() {
  assert(InstanceImpl() && "Already terminated.");
  InstanceImpl().reset();
}

bool ProgressManager::Enabled() { return InstanceImpl().operator bool(); }

ProgressManager &ProgressManager::Instance() {
  assert(InstanceImpl() && "ProgressManager must be initialized");
  return *InstanceImpl();
}

std::optional<ProgressManager> &ProgressManager::InstanceImpl() {
  static std::optional<ProgressManager> g_progress_manager;
  return g_progress_manager;
}

void ProgressManager::Increment(const Progress::ProgressData &progress_data) {
  std::lock_guard<std::mutex> lock(m_entries_mutex);

  llvm::StringRef key = progress_data.title;
  bool new_entry = !m_entries.contains(key);
  Entry &entry = m_entries[progress_data.title];

  if (new_entry) {
    // This is a new progress event. Report progress and store the progress
    // data.
    ReportProgress(progress_data, EventType::Begin);
    entry.data = progress_data;
  } else if (entry.refcount == 0) {
    // This is an existing entry that was scheduled to be deleted but a new one
    // came in before the timer expired.
    assert(entry.handle != Alarm::INVALID_HANDLE);

    if (!m_alarm.Cancel(entry.handle)) {
      // The timer expired before we had a chance to cancel it. We have to treat
      // this as an entirely new progress event.
      ReportProgress(progress_data, EventType::Begin);
    }
    // Clear the alarm handle.
    entry.handle = Alarm::INVALID_HANDLE;
  }

  // Regardless of how we got here, we need to bump the reference count.
  entry.refcount++;
}

void ProgressManager::Decrement(const Progress::ProgressData &progress_data) {
  std::lock_guard<std::mutex> lock(m_entries_mutex);
  llvm::StringRef key = progress_data.title;

  if (!m_entries.contains(key))
    return;

  Entry &entry = m_entries[key];
  entry.refcount--;

  if (entry.refcount == 0) {
    assert(entry.handle == Alarm::INVALID_HANDLE);

    // Copy the key to a std::string so we can pass it by value to the lambda.
    // The underlying StringRef will not exist by the time the callback is
    // called.
    std::string key_str = std::string(key);

    // Start a timer. If it expires before we see another progress event, it
    // will be reported.
    entry.handle = m_alarm.Create([=]() { Expire(key_str); });
  }
}

void ProgressManager::ReportProgress(
    const Progress::ProgressData &progress_data, EventType type) {
  // The category bit only keeps track of when progress report categories have
  // started and ended, so clear the details and reset other fields when
  // broadcasting to it since that bit doesn't need that information.
  const uint64_t completed =
      (type == EventType::Begin) ? 0 : Progress::kNonDeterministicTotal;
  Debugger::ReportProgress(progress_data.progress_id, progress_data.title, "",
                           completed, Progress::kNonDeterministicTotal,
                           progress_data.debugger_id,
                           lldb::eBroadcastBitProgressCategory);
}

void ProgressManager::Expire(llvm::StringRef key) {
  std::lock_guard<std::mutex> lock(m_entries_mutex);

  // This shouldn't happen but be resilient anyway.
  if (!m_entries.contains(key))
    return;

  // A new event came in and the alarm fired before we had a chance to restart
  // it.
  if (m_entries[key].refcount != 0)
    return;

  // We're done with this entry.
  ReportProgress(m_entries[key].data, EventType::End);
  m_entries.erase(key);
}
