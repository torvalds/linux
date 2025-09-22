//===-- GDBRemoteClientBase.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECLIENTBASE_H
#define LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECLIENTBASE_H

#include "GDBRemoteCommunication.h"

#include <condition_variable>

namespace lldb_private {
namespace process_gdb_remote {

class GDBRemoteClientBase : public GDBRemoteCommunication, public Broadcaster {
public:
  enum {
    eBroadcastBitRunPacketSent = (1u << 0),
  };

  struct ContinueDelegate {
    virtual ~ContinueDelegate();
    virtual void HandleAsyncStdout(llvm::StringRef out) = 0;
    virtual void HandleAsyncMisc(llvm::StringRef data) = 0;
    virtual void HandleStopReply() = 0;

    /// Process asynchronously-received structured data.
    ///
    /// \param[in] data
    ///   The complete data packet, expected to start with JSON-async.
    virtual void HandleAsyncStructuredDataPacket(llvm::StringRef data) = 0;
  };

  GDBRemoteClientBase(const char *comm_name);

  bool SendAsyncSignal(int signo, std::chrono::seconds interrupt_timeout);

  bool Interrupt(std::chrono::seconds interrupt_timeout);

  lldb::StateType SendContinuePacketAndWaitForResponse(
      ContinueDelegate &delegate, const UnixSignals &signals,
      llvm::StringRef payload, std::chrono::seconds interrupt_timeout,
      StringExtractorGDBRemote &response);

  // If interrupt_timeout == 0 seconds, don't interrupt the target.
  // Only send the packet if the target is stopped.
  // If you want to use this mode, use the fact that the timeout is defaulted
  // so it's clear from the call-site that you are using no-interrupt.
  // If it is non-zero, interrupt the target if it is running, and
  // send the packet.
  // It the target doesn't respond within the given timeout, it returns
  // ErrorReplyTimeout.
  PacketResult SendPacketAndWaitForResponse(
      llvm::StringRef payload, StringExtractorGDBRemote &response,
      std::chrono::seconds interrupt_timeout = std::chrono::seconds(0));

  PacketResult ReadPacketWithOutputSupport(
      StringExtractorGDBRemote &response, Timeout<std::micro> timeout,
      bool sync_on_timeout,
      llvm::function_ref<void(llvm::StringRef)> output_callback);

  PacketResult SendPacketAndReceiveResponseWithOutputSupport(
      llvm::StringRef payload, StringExtractorGDBRemote &response,
      std::chrono::seconds interrupt_timeout,
      llvm::function_ref<void(llvm::StringRef)> output_callback);

  class Lock {
  public:
    // If interrupt_timeout == 0 seconds, only take the lock if the target is
    // not running. If using this option, use the fact that the
    // interrupt_timeout is defaulted so it will be obvious at the call site
    // that you are choosing this mode. If it is non-zero, interrupt the target
    // if it is running, waiting for the given timeout for the interrupt to
    // succeed.
    Lock(GDBRemoteClientBase &comm,
         std::chrono::seconds interrupt_timeout = std::chrono::seconds(0));
    ~Lock();

    explicit operator bool() { return m_acquired; }

    // Whether we had to interrupt the continue thread to acquire the
    // connection.
    bool DidInterrupt() const { return m_did_interrupt; }

  private:
    std::unique_lock<std::recursive_mutex> m_async_lock;
    GDBRemoteClientBase &m_comm;
    std::chrono::seconds m_interrupt_timeout;
    bool m_acquired;
    bool m_did_interrupt;

    void SyncWithContinueThread();
  };

protected:
  PacketResult
  SendPacketAndWaitForResponseNoLock(llvm::StringRef payload,
                                     StringExtractorGDBRemote &response);

  virtual void OnRunPacketSent(bool first);

private:
  /// Variables handling synchronization between the Continue thread and any
  /// other threads wishing to send packets over the connection. Either the
  /// continue thread has control over the connection (m_is_running == true) or
  /// the connection is free for an arbitrary number of other senders to take
  /// which indicate their interest by incrementing m_async_count.
  ///
  /// Semantics of individual states:
  ///
  /// - m_continue_packet == false, m_async_count == 0:
  ///   connection is free
  /// - m_continue_packet == true, m_async_count == 0:
  ///   only continue thread is present
  /// - m_continue_packet == true, m_async_count > 0:
  ///   continue thread has control, async threads should interrupt it and wait
  ///   for it to set m_continue_packet to false
  /// - m_continue_packet == false, m_async_count > 0:
  ///   async threads have control, continue thread needs to wait for them to
  ///   finish (m_async_count goes down to 0).
  /// @{
  std::mutex m_mutex;
  std::condition_variable m_cv;

  /// Packet with which to resume after an async interrupt. Can be changed by
  /// an async thread e.g. to inject a signal.
  std::string m_continue_packet;

  /// When was the interrupt packet sent. Used to make sure we time out if the
  /// stub does not respond to interrupt requests.
  std::chrono::time_point<std::chrono::steady_clock> m_interrupt_endpoint;

  /// Number of threads interested in sending.
  uint32_t m_async_count;

  /// Whether the continue thread has control.
  bool m_is_running;

  /// Whether we should resume after a stop.
  bool m_should_stop;
  /// @}

  /// This handles the synchronization between individual async threads. For
  /// now they just use a simple mutex.
  std::recursive_mutex m_async_mutex;

  bool ShouldStop(const UnixSignals &signals,
                  StringExtractorGDBRemote &response);

  class ContinueLock {
  public:
    enum class LockResult { Success, Cancelled, Failed };

    explicit ContinueLock(GDBRemoteClientBase &comm);
    ~ContinueLock();
    explicit operator bool() { return m_acquired; }

    LockResult lock();

    void unlock();

  private:
    GDBRemoteClientBase &m_comm;
    bool m_acquired;
  };
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECLIENTBASE_H
