//===-- ProgressEvent.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProgressEvent.h"

#include "JSONUtils.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>

using namespace lldb_dap;
using namespace llvm;

// The minimum duration of an event for it to be reported
const std::chrono::duration<double> kStartProgressEventReportDelay =
    std::chrono::seconds(1);
// The minimum time interval between update events for reporting. If multiple
// updates fall within the same time interval, only the latest is reported.
const std::chrono::duration<double> kUpdateProgressEventReportDelay =
    std::chrono::milliseconds(250);

ProgressEvent::ProgressEvent(uint64_t progress_id,
                             std::optional<StringRef> message,
                             uint64_t completed, uint64_t total,
                             const ProgressEvent *prev_event)
    : m_progress_id(progress_id) {
  if (message)
    m_message = message->str();

  const bool calculate_percentage = total != UINT64_MAX;
  if (completed == 0) {
    // Start event
    m_event_type = progressStart;
    // Wait a bit before reporting the start event in case in completes really
    // quickly.
    m_minimum_allowed_report_time =
        m_creation_time + kStartProgressEventReportDelay;
    if (calculate_percentage)
      m_percentage = 0;
  } else if (completed == total) {
    // End event
    m_event_type = progressEnd;
    // We should report the end event right away.
    m_minimum_allowed_report_time = std::chrono::seconds::zero();
    if (calculate_percentage)
      m_percentage = 100;
  } else {
    // Update event
    m_event_type = progressUpdate;
    m_percentage = std::min(
        (uint32_t)((double)completed / (double)total * 100.0), (uint32_t)99);
    if (prev_event->Reported()) {
      // Add a small delay between reports
      m_minimum_allowed_report_time =
          prev_event->m_minimum_allowed_report_time +
          kUpdateProgressEventReportDelay;
    } else {
      // We should use the previous timestamp, as it's still pending
      m_minimum_allowed_report_time = prev_event->m_minimum_allowed_report_time;
    }
  }
}

std::optional<ProgressEvent>
ProgressEvent::Create(uint64_t progress_id, std::optional<StringRef> message,
                      uint64_t completed, uint64_t total,
                      const ProgressEvent *prev_event) {
  // If it's an update without a previous event, we abort
  if (completed > 0 && completed < total && !prev_event)
    return std::nullopt;
  ProgressEvent event(progress_id, message, completed, total, prev_event);
  // We shouldn't show unnamed start events in the IDE
  if (event.GetEventType() == progressStart && event.GetEventName().empty())
    return std::nullopt;

  if (prev_event && prev_event->EqualsForIDE(event))
    return std::nullopt;

  return event;
}

bool ProgressEvent::EqualsForIDE(const ProgressEvent &other) const {
  return m_progress_id == other.m_progress_id &&
         m_event_type == other.m_event_type &&
         m_percentage == other.m_percentage;
}

ProgressEventType ProgressEvent::GetEventType() const { return m_event_type; }

StringRef ProgressEvent::GetEventName() const {
  switch (m_event_type) {
  case progressStart:
    return "progressStart";
  case progressUpdate:
    return "progressUpdate";
  case progressEnd:
    return "progressEnd";
  }
  llvm_unreachable("All cases handled above!");
}

json::Value ProgressEvent::ToJSON() const {
  llvm::json::Object event(CreateEventObject(GetEventName()));
  llvm::json::Object body;

  std::string progress_id_str;
  llvm::raw_string_ostream progress_id_strm(progress_id_str);
  progress_id_strm << m_progress_id;
  progress_id_strm.flush();
  body.try_emplace("progressId", progress_id_str);

  if (m_event_type == progressStart) {
    EmplaceSafeString(body, "title", m_message);
    body.try_emplace("cancellable", false);
  }

  std::string timestamp(llvm::formatv("{0:f9}", m_creation_time.count()));
  EmplaceSafeString(body, "timestamp", timestamp);

  if (m_percentage)
    body.try_emplace("percentage", *m_percentage);

  event.try_emplace("body", std::move(body));
  return json::Value(std::move(event));
}

bool ProgressEvent::Report(ProgressEventReportCallback callback) {
  if (Reported())
    return true;
  if (std::chrono::system_clock::now().time_since_epoch() <
      m_minimum_allowed_report_time)
    return false;

  m_reported = true;
  callback(*this);
  return true;
}

bool ProgressEvent::Reported() const { return m_reported; }

ProgressEventManager::ProgressEventManager(
    const ProgressEvent &start_event,
    ProgressEventReportCallback report_callback)
    : m_start_event(start_event), m_finished(false),
      m_report_callback(report_callback) {}

bool ProgressEventManager::ReportIfNeeded() {
  // The event finished before we were able to report it.
  if (!m_start_event.Reported() && Finished())
    return true;

  if (!m_start_event.Report(m_report_callback))
    return false;

  if (m_last_update_event)
    m_last_update_event->Report(m_report_callback);
  return true;
}

const ProgressEvent &ProgressEventManager::GetMostRecentEvent() const {
  return m_last_update_event ? *m_last_update_event : m_start_event;
}

void ProgressEventManager::Update(uint64_t progress_id, uint64_t completed,
                                  uint64_t total) {
  if (std::optional<ProgressEvent> event = ProgressEvent::Create(
          progress_id, std::nullopt, completed, total, &GetMostRecentEvent())) {
    if (event->GetEventType() == progressEnd)
      m_finished = true;

    m_last_update_event = *event;
    ReportIfNeeded();
  }
}

bool ProgressEventManager::Finished() const { return m_finished; }

ProgressEventReporter::ProgressEventReporter(
    ProgressEventReportCallback report_callback)
    : m_report_callback(report_callback) {
  m_thread_should_exit = false;
  m_thread = std::thread([&] {
    while (!m_thread_should_exit) {
      std::this_thread::sleep_for(kUpdateProgressEventReportDelay);
      ReportStartEvents();
    }
  });
}

ProgressEventReporter::~ProgressEventReporter() {
  m_thread_should_exit = true;
  m_thread.join();
}

void ProgressEventReporter::ReportStartEvents() {
  std::lock_guard<std::mutex> locker(m_mutex);

  while (!m_unreported_start_events.empty()) {
    ProgressEventManagerSP event_manager = m_unreported_start_events.front();
    if (event_manager->Finished())
      m_unreported_start_events.pop();
    else if (event_manager->ReportIfNeeded())
      m_unreported_start_events
          .pop(); // we remove it from the queue as it started reporting
                  // already, the Push method will be able to continue its
                  // reports.
    else
      break; // If we couldn't report it, then the next event in the queue won't
             // be able as well, as it came later.
  }
}

void ProgressEventReporter::Push(uint64_t progress_id, const char *message,
                                 uint64_t completed, uint64_t total) {
  std::lock_guard<std::mutex> locker(m_mutex);

  auto it = m_event_managers.find(progress_id);
  if (it == m_event_managers.end()) {
    if (std::optional<ProgressEvent> event = ProgressEvent::Create(
            progress_id, StringRef(message), completed, total)) {
      ProgressEventManagerSP event_manager =
          std::make_shared<ProgressEventManager>(*event, m_report_callback);
      m_event_managers.insert({progress_id, event_manager});
      m_unreported_start_events.push(event_manager);
    }
  } else {
    it->second->Update(progress_id, completed, total);
    if (it->second->Finished())
      m_event_managers.erase(it);
  }
}
