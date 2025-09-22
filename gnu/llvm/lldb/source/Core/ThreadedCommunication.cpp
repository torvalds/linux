//===-- ThreadedCommunication.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ThreadedCommunication.h"

#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Utility/Connection.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/Compiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <shared_mutex>

#include <cerrno>
#include <cinttypes>
#include <cstdio>

using namespace lldb;
using namespace lldb_private;

llvm::StringRef ThreadedCommunication::GetStaticBroadcasterClass() {
  static constexpr llvm::StringLiteral class_name("lldb.communication");
  return class_name;
}

ThreadedCommunication::ThreadedCommunication(const char *name)
    : Communication(), Broadcaster(nullptr, name), m_read_thread_enabled(false),
      m_read_thread_did_exit(false), m_bytes(), m_bytes_mutex(),
      m_synchronize_mutex(), m_callback(nullptr), m_callback_baton(nullptr) {
  LLDB_LOG(GetLog(LLDBLog::Object | LLDBLog::Communication),
           "{0} ThreadedCommunication::ThreadedCommunication (name = {1})",
           this, name);

  SetEventName(eBroadcastBitDisconnected, "disconnected");
  SetEventName(eBroadcastBitReadThreadGotBytes, "got bytes");
  SetEventName(eBroadcastBitReadThreadDidExit, "read thread did exit");
  SetEventName(eBroadcastBitReadThreadShouldExit, "read thread should exit");
  SetEventName(eBroadcastBitPacketAvailable, "packet available");
  SetEventName(eBroadcastBitNoMorePendingInput, "no more pending input");

  CheckInWithManager();
}

ThreadedCommunication::~ThreadedCommunication() {
  LLDB_LOG(GetLog(LLDBLog::Object | LLDBLog::Communication),
           "{0} ThreadedCommunication::~ThreadedCommunication (name = {1})",
           this, GetBroadcasterName());
}

void ThreadedCommunication::Clear() {
  SetReadThreadBytesReceivedCallback(nullptr, nullptr);
  StopReadThread(nullptr);
  Communication::Clear();
}

ConnectionStatus ThreadedCommunication::Disconnect(Status *error_ptr) {
  assert((!m_read_thread_enabled || m_read_thread_did_exit) &&
         "Disconnecting while the read thread is running is racy!");
  return Communication::Disconnect(error_ptr);
}

size_t ThreadedCommunication::Read(void *dst, size_t dst_len,
                                   const Timeout<std::micro> &timeout,
                                   ConnectionStatus &status,
                                   Status *error_ptr) {
  Log *log = GetLog(LLDBLog::Communication);
  LLDB_LOG(
      log,
      "this = {0}, dst = {1}, dst_len = {2}, timeout = {3}, connection = {4}",
      this, dst, dst_len, timeout, m_connection_sp.get());

  if (m_read_thread_enabled) {
    // We have a dedicated read thread that is getting data for us
    size_t cached_bytes = GetCachedBytes(dst, dst_len);
    if (cached_bytes > 0) {
      status = eConnectionStatusSuccess;
      return cached_bytes;
    }
    if (timeout && timeout->count() == 0) {
      if (error_ptr)
        error_ptr->SetErrorString("Timed out.");
      status = eConnectionStatusTimedOut;
      return 0;
    }

    if (!m_connection_sp) {
      if (error_ptr)
        error_ptr->SetErrorString("Invalid connection.");
      status = eConnectionStatusNoConnection;
      return 0;
    }

    // No data yet, we have to start listening.
    ListenerSP listener_sp(
        Listener::MakeListener("ThreadedCommunication::Read"));
    listener_sp->StartListeningForEvents(
        this, eBroadcastBitReadThreadGotBytes | eBroadcastBitReadThreadDidExit);

    // Re-check for data, as it might have arrived while we were setting up our
    // listener.
    cached_bytes = GetCachedBytes(dst, dst_len);
    if (cached_bytes > 0) {
      status = eConnectionStatusSuccess;
      return cached_bytes;
    }

    EventSP event_sp;
    // Explicitly check for the thread exit, for the same reason.
    if (m_read_thread_did_exit) {
      // We've missed the event, lets just conjure one up.
      event_sp = std::make_shared<Event>(eBroadcastBitReadThreadDidExit);
    } else {
      if (!listener_sp->GetEvent(event_sp, timeout)) {
        if (error_ptr)
          error_ptr->SetErrorString("Timed out.");
        status = eConnectionStatusTimedOut;
        return 0;
      }
    }
    const uint32_t event_type = event_sp->GetType();
    if (event_type & eBroadcastBitReadThreadGotBytes) {
      return GetCachedBytes(dst, dst_len);
    }

    if (event_type & eBroadcastBitReadThreadDidExit) {
      // If the thread exited of its own accord, it either means it
      // hit an end-of-file condition or an error.
      status = m_pass_status;
      if (error_ptr)
        *error_ptr = std::move(m_pass_error);

      if (GetCloseOnEOF())
        Disconnect(nullptr);
      return 0;
    }
    llvm_unreachable("Got unexpected event type!");
  }

  // We aren't using a read thread, just read the data synchronously in this
  // thread.
  return Communication::Read(dst, dst_len, timeout, status, error_ptr);
}

bool ThreadedCommunication::StartReadThread(Status *error_ptr) {
  std::lock_guard<std::mutex> lock(m_read_thread_mutex);

  if (error_ptr)
    error_ptr->Clear();

  if (m_read_thread.IsJoinable())
    return true;

  LLDB_LOG(GetLog(LLDBLog::Communication),
           "{0} ThreadedCommunication::StartReadThread ()", this);

  const std::string thread_name =
      llvm::formatv("<lldb.comm.{0}>", GetBroadcasterName());

  m_read_thread_enabled = true;
  m_read_thread_did_exit = false;
  auto maybe_thread = ThreadLauncher::LaunchThread(
      thread_name, [this] { return ReadThread(); });
  if (maybe_thread) {
    m_read_thread = *maybe_thread;
  } else {
    if (error_ptr)
      *error_ptr = Status(maybe_thread.takeError());
    else {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Host), maybe_thread.takeError(),
                     "failed to launch host thread: {0}");
    }
  }

  if (!m_read_thread.IsJoinable())
    m_read_thread_enabled = false;

  return m_read_thread_enabled;
}

bool ThreadedCommunication::StopReadThread(Status *error_ptr) {
  std::lock_guard<std::mutex> lock(m_read_thread_mutex);

  if (!m_read_thread.IsJoinable())
    return true;

  LLDB_LOG(GetLog(LLDBLog::Communication),
           "{0} ThreadedCommunication::StopReadThread ()", this);

  m_read_thread_enabled = false;

  BroadcastEvent(eBroadcastBitReadThreadShouldExit, nullptr);

  Status error = m_read_thread.Join(nullptr);
  return error.Success();
}

bool ThreadedCommunication::JoinReadThread(Status *error_ptr) {
  std::lock_guard<std::mutex> lock(m_read_thread_mutex);

  if (!m_read_thread.IsJoinable())
    return true;

  Status error = m_read_thread.Join(nullptr);
  return error.Success();
}

size_t ThreadedCommunication::GetCachedBytes(void *dst, size_t dst_len) {
  std::lock_guard<std::recursive_mutex> guard(m_bytes_mutex);
  if (!m_bytes.empty()) {
    // If DST is nullptr and we have a thread, then return the number of bytes
    // that are available so the caller can call again
    if (dst == nullptr)
      return m_bytes.size();

    const size_t len = std::min<size_t>(dst_len, m_bytes.size());

    ::memcpy(dst, m_bytes.c_str(), len);
    m_bytes.erase(m_bytes.begin(), m_bytes.begin() + len);

    return len;
  }
  return 0;
}

void ThreadedCommunication::AppendBytesToCache(const uint8_t *bytes, size_t len,
                                               bool broadcast,
                                               ConnectionStatus status) {
  LLDB_LOG(GetLog(LLDBLog::Communication),
           "{0} ThreadedCommunication::AppendBytesToCache (src = {1}, src_len "
           "= {2}, "
           "broadcast = {3})",
           this, bytes, (uint64_t)len, broadcast);
  if ((bytes == nullptr || len == 0) &&
      (status != lldb::eConnectionStatusEndOfFile))
    return;
  if (m_callback) {
    // If the user registered a callback, then call it and do not broadcast
    m_callback(m_callback_baton, bytes, len);
  } else if (bytes != nullptr && len > 0) {
    std::lock_guard<std::recursive_mutex> guard(m_bytes_mutex);
    m_bytes.append((const char *)bytes, len);
    if (broadcast)
      BroadcastEventIfUnique(eBroadcastBitReadThreadGotBytes);
  }
}

bool ThreadedCommunication::ReadThreadIsRunning() {
  return m_read_thread_enabled;
}

lldb::thread_result_t ThreadedCommunication::ReadThread() {
  Log *log = GetLog(LLDBLog::Communication);

  LLDB_LOG(log, "Communication({0}) thread starting...", this);

  uint8_t buf[1024];

  Status error;
  ConnectionStatus status = eConnectionStatusSuccess;
  bool done = false;
  bool disconnect = false;
  while (!done && m_read_thread_enabled) {
    size_t bytes_read = ReadFromConnection(
        buf, sizeof(buf), std::chrono::seconds(5), status, &error);
    if (bytes_read > 0 || status == eConnectionStatusEndOfFile)
      AppendBytesToCache(buf, bytes_read, true, status);

    switch (status) {
    case eConnectionStatusSuccess:
      break;

    case eConnectionStatusEndOfFile:
      done = true;
      disconnect = GetCloseOnEOF();
      break;
    case eConnectionStatusError: // Check GetError() for details
      if (error.GetType() == eErrorTypePOSIX && error.GetError() == EIO) {
        // EIO on a pipe is usually caused by remote shutdown
        disconnect = GetCloseOnEOF();
        done = true;
      }
      if (error.Fail())
        LLDB_LOG(log, "error: {0}, status = {1}", error,
                 ThreadedCommunication::ConnectionStatusAsString(status));
      break;
    case eConnectionStatusInterrupted: // Synchronization signal from
                                       // SynchronizeWithReadThread()
      // The connection returns eConnectionStatusInterrupted only when there is
      // no input pending to be read, so we can signal that.
      BroadcastEvent(eBroadcastBitNoMorePendingInput);
      break;
    case eConnectionStatusNoConnection:   // No connection
    case eConnectionStatusLostConnection: // Lost connection while connected to
                                          // a valid connection
      done = true;
      [[fallthrough]];
    case eConnectionStatusTimedOut: // Request timed out
      if (error.Fail())
        LLDB_LOG(log, "error: {0}, status = {1}", error,
                 ThreadedCommunication::ConnectionStatusAsString(status));
      break;
    }
  }
  m_pass_status = status;
  m_pass_error = std::move(error);
  LLDB_LOG(log, "Communication({0}) thread exiting...", this);

  // Start shutting down. We need to do this in a very specific order to ensure
  // we don't race with threads wanting to read/synchronize with us.

  // First, we signal our intent to exit. This ensures no new thread start
  // waiting on events from us.
  m_read_thread_did_exit = true;

  // Unblock any existing thread waiting for the synchronization signal.
  BroadcastEvent(eBroadcastBitNoMorePendingInput);

  {
    // Wait for the synchronization thread to finish...
    std::lock_guard<std::mutex> guard(m_synchronize_mutex);
    // ... and disconnect.
    if (disconnect)
      Disconnect();
  }

  // Finally, unblock any readers waiting for us to exit.
  BroadcastEvent(eBroadcastBitReadThreadDidExit);
  return {};
}

void ThreadedCommunication::SetReadThreadBytesReceivedCallback(
    ReadThreadBytesReceived callback, void *callback_baton) {
  m_callback = callback;
  m_callback_baton = callback_baton;
}

void ThreadedCommunication::SynchronizeWithReadThread() {
  // Only one thread can do the synchronization dance at a time.
  std::lock_guard<std::mutex> guard(m_synchronize_mutex);

  // First start listening for the synchronization event.
  ListenerSP listener_sp(Listener::MakeListener(
      "ThreadedCommunication::SyncronizeWithReadThread"));
  listener_sp->StartListeningForEvents(this, eBroadcastBitNoMorePendingInput);

  // If the thread is not running, there is no point in synchronizing.
  if (!m_read_thread_enabled || m_read_thread_did_exit)
    return;

  // Notify the read thread.
  m_connection_sp->InterruptRead();

  // Wait for the synchronization event.
  EventSP event_sp;
  listener_sp->GetEvent(event_sp, std::nullopt);
}

void ThreadedCommunication::SetConnection(
    std::unique_ptr<Connection> connection) {
  StopReadThread(nullptr);
  Communication::SetConnection(std::move(connection));
}
