//===-- ProgressEvent.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>

#include "DAPForward.h"

#include "llvm/Support/JSON.h"

namespace lldb_dap {

enum ProgressEventType { progressStart, progressUpdate, progressEnd };

class ProgressEvent;
using ProgressEventReportCallback = std::function<void(ProgressEvent &)>;

class ProgressEvent {
public:
  /// Actual constructor to use that returns an optional, as the event might be
  /// not apt for the IDE, e.g. an unnamed start event, or a redundant one.
  ///
  /// \param[in] progress_id
  ///   ID for this event.
  ///
  /// \param[in] message
  ///   Message to display in the UI. Required for start events.
  ///
  /// \param[in] completed
  ///   Number of jobs completed.
  ///
  /// \param[in] total
  ///   Total number of jobs, or \b UINT64_MAX if not determined.
  ///
  /// \param[in] prev_event
  ///   Previous event if this one is an update. If \b nullptr, then a start
  ///   event will be created.
  static std::optional<ProgressEvent>
  Create(uint64_t progress_id, std::optional<llvm::StringRef> message,
         uint64_t completed, uint64_t total,
         const ProgressEvent *prev_event = nullptr);

  llvm::json::Value ToJSON() const;

  /// \return
  ///       \b true if two event messages would result in the same event for the
  ///       IDE, e.g. same rounded percentage.
  bool EqualsForIDE(const ProgressEvent &other) const;

  llvm::StringRef GetEventName() const;

  ProgressEventType GetEventType() const;

  /// Report this progress event to the provided callback only if enough time
  /// has passed since the creation of the event and since the previous reported
  /// update.
  bool Report(ProgressEventReportCallback callback);

  bool Reported() const;

private:
  ProgressEvent(uint64_t progress_id, std::optional<llvm::StringRef> message,
                uint64_t completed, uint64_t total,
                const ProgressEvent *prev_event);

  uint64_t m_progress_id;
  std::string m_message;
  ProgressEventType m_event_type;
  std::optional<uint32_t> m_percentage;
  std::chrono::duration<double> m_creation_time =
      std::chrono::system_clock::now().time_since_epoch();
  std::chrono::duration<double> m_minimum_allowed_report_time;
  bool m_reported = false;
};

/// Class that keeps the start event and its most recent update.
/// It controls when the event should start being reported to the IDE.
class ProgressEventManager {
public:
  ProgressEventManager(const ProgressEvent &start_event,
                       ProgressEventReportCallback report_callback);

  /// Report the start event and the most recent update if the event has lasted
  /// for long enough.
  ///
  /// \return
  ///     \b false if the event hasn't finished and hasn't reported anything
  ///     yet.
  bool ReportIfNeeded();

  /// Receive a new progress event for the start event and try to report it if
  /// appropriate.
  void Update(uint64_t progress_id, uint64_t completed, uint64_t total);

  /// \return
  ///     \b true if a \a progressEnd event has been notified. There's no
  ///     need to try to report manually an event that has finished.
  bool Finished() const;

  const ProgressEvent &GetMostRecentEvent() const;

private:
  ProgressEvent m_start_event;
  std::optional<ProgressEvent> m_last_update_event;
  bool m_finished;
  ProgressEventReportCallback m_report_callback;
};

using ProgressEventManagerSP = std::shared_ptr<ProgressEventManager>;

/// Class that filters out progress event messages that shouldn't be reported
/// to the IDE, because they are invalid, they carry no new information, or they
/// don't last long enough.
///
/// We need to limit the amount of events that are sent to the IDE, as they slow
/// the render thread of the UI user, and they end up spamming the DAP
/// connection, which also takes some processing time out of the IDE.
class ProgressEventReporter {
public:
  /// \param[in] report_callback
  ///     Function to invoke to report the event to the IDE.
  ProgressEventReporter(ProgressEventReportCallback report_callback);

  ~ProgressEventReporter();

  /// Add a new event to the internal queue and report the event if
  /// appropriate.
  void Push(uint64_t progress_id, const char *message, uint64_t completed,
            uint64_t total);

private:
  /// Report to the IDE events that haven't been reported to the IDE and have
  /// lasted long enough.
  void ReportStartEvents();

  ProgressEventReportCallback m_report_callback;
  std::map<uint64_t, ProgressEventManagerSP> m_event_managers;
  /// Queue of start events in chronological order
  std::queue<ProgressEventManagerSP> m_unreported_start_events;
  /// Thread used to invoke \a ReportStartEvents periodically.
  std::thread m_thread;
  std::atomic<bool> m_thread_should_exit;
  /// Mutex that prevents running \a Push and \a ReportStartEvents
  /// simultaneously, as both read and modify the same underlying objects.
  std::mutex m_mutex;
};

} // namespace lldb_dap
