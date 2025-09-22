//===-- GDBRemoteClientBase.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteClientBase.h"

#include "llvm/ADT/StringExtras.h"

#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/LLDBAssert.h"

#include "ProcessGDBRemoteLog.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;
using namespace std::chrono;

// When we've sent a continue packet and are waiting for the target to stop,
// we wake up the wait with this interval to make sure the stub hasn't gone
// away while we were waiting.
static const seconds kWakeupInterval(5);

/////////////////////////
// GDBRemoteClientBase //
/////////////////////////

GDBRemoteClientBase::ContinueDelegate::~ContinueDelegate() = default;

GDBRemoteClientBase::GDBRemoteClientBase(const char *comm_name)
    : GDBRemoteCommunication(), Broadcaster(nullptr, comm_name),
      m_async_count(0), m_is_running(false), m_should_stop(false) {}

StateType GDBRemoteClientBase::SendContinuePacketAndWaitForResponse(
    ContinueDelegate &delegate, const UnixSignals &signals,
    llvm::StringRef payload, std::chrono::seconds interrupt_timeout,
    StringExtractorGDBRemote &response) {
  Log *log = GetLog(GDBRLog::Process);
  response.Clear();

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_continue_packet = std::string(payload);
    m_should_stop = false;
  }
  ContinueLock cont_lock(*this);
  if (!cont_lock)
    return eStateInvalid;
  OnRunPacketSent(true);
  // The main ReadPacket loop wakes up at computed_timeout intervals, just to 
  // check that the connection hasn't dropped.  When we wake up we also check
  // whether there is an interrupt request that has reached its endpoint.
  // If we want a shorter interrupt timeout that kWakeupInterval, we need to 
  // choose the shorter interval for the wake up as well.
  std::chrono::seconds computed_timeout = std::min(interrupt_timeout, 
                                                   kWakeupInterval);
  for (;;) {
    PacketResult read_result = ReadPacket(response, computed_timeout, false);
    // Reset the computed_timeout to the default value in case we are going
    // round again.
    computed_timeout = std::min(interrupt_timeout, kWakeupInterval);
    switch (read_result) {
    case PacketResult::ErrorReplyTimeout: {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_async_count == 0) {
        continue;
      }
      auto cur_time = steady_clock::now();
      if (cur_time >= m_interrupt_endpoint)
        return eStateInvalid;
      else {
        // We woke up and found an interrupt is in flight, but we haven't
        // exceeded the interrupt wait time.  So reset the wait time to the
        // time left till the interrupt timeout.  But don't wait longer
        // than our wakeup timeout.
        auto new_wait = m_interrupt_endpoint - cur_time;
        computed_timeout = std::min(kWakeupInterval,
            std::chrono::duration_cast<std::chrono::seconds>(new_wait));
        continue;
      }
      break;
    }
    case PacketResult::Success:
      break;
    default:
      LLDB_LOGF(log, "GDBRemoteClientBase::%s () ReadPacket(...) => false",
                __FUNCTION__);
      return eStateInvalid;
    }
    if (response.Empty())
      return eStateInvalid;

    const char stop_type = response.GetChar();
    LLDB_LOGF(log, "GDBRemoteClientBase::%s () got packet: %s", __FUNCTION__,
              response.GetStringRef().data());

    switch (stop_type) {
    case 'W':
    case 'X':
      return eStateExited;
    case 'E':
      // ERROR
      return eStateInvalid;
    default:
      LLDB_LOGF(log, "GDBRemoteClientBase::%s () unrecognized async packet",
                __FUNCTION__);
      return eStateInvalid;
    case 'O': {
      std::string inferior_stdout;
      response.GetHexByteString(inferior_stdout);
      delegate.HandleAsyncStdout(inferior_stdout);
      break;
    }
    case 'A':
      delegate.HandleAsyncMisc(
          llvm::StringRef(response.GetStringRef()).substr(1));
      break;
    case 'J':
      delegate.HandleAsyncStructuredDataPacket(response.GetStringRef());
      break;
    case 'T':
    case 'S':
      // Do this with the continue lock held.
      const bool should_stop = ShouldStop(signals, response);
      response.SetFilePos(0);

      // The packet we should resume with. In the future we should check our
      // thread list and "do the right thing" for new threads that show up
      // while we stop and run async packets. Setting the packet to 'c' to
      // continue all threads is the right thing to do 99.99% of the time
      // because if a thread was single stepping, and we sent an interrupt, we
      // will notice above that we didn't stop due to an interrupt but stopped
      // due to stepping and we would _not_ continue. This packet may get
      // modified by the async actions (e.g. to send a signal).
      m_continue_packet = 'c';
      cont_lock.unlock();

      delegate.HandleStopReply();
      if (should_stop)
        return eStateStopped;

      switch (cont_lock.lock()) {
      case ContinueLock::LockResult::Success:
        break;
      case ContinueLock::LockResult::Failed:
        return eStateInvalid;
      case ContinueLock::LockResult::Cancelled:
        return eStateStopped;
      }
      OnRunPacketSent(false);
      break;
    }
  }
}

bool GDBRemoteClientBase::SendAsyncSignal(
    int signo, std::chrono::seconds interrupt_timeout) {
  Lock lock(*this, interrupt_timeout);
  if (!lock || !lock.DidInterrupt())
    return false;

  m_continue_packet = 'C';
  m_continue_packet += llvm::hexdigit((signo / 16) % 16);
  m_continue_packet += llvm::hexdigit(signo % 16);
  return true;
}

bool GDBRemoteClientBase::Interrupt(std::chrono::seconds interrupt_timeout) {
  Lock lock(*this, interrupt_timeout);
  if (!lock.DidInterrupt())
    return false;
  m_should_stop = true;
  return true;
}

GDBRemoteCommunication::PacketResult
GDBRemoteClientBase::SendPacketAndWaitForResponse(
    llvm::StringRef payload, StringExtractorGDBRemote &response,
    std::chrono::seconds interrupt_timeout) {
  Lock lock(*this, interrupt_timeout);
  if (!lock) {
    if (Log *log = GetLog(GDBRLog::Process))
      LLDB_LOGF(log,
                "GDBRemoteClientBase::%s failed to get mutex, not sending "
                "packet '%.*s'",
                __FUNCTION__, int(payload.size()), payload.data());
    return PacketResult::ErrorSendFailed;
  }

  return SendPacketAndWaitForResponseNoLock(payload, response);
}

GDBRemoteCommunication::PacketResult
GDBRemoteClientBase::ReadPacketWithOutputSupport(
    StringExtractorGDBRemote &response, Timeout<std::micro> timeout,
    bool sync_on_timeout,
    llvm::function_ref<void(llvm::StringRef)> output_callback) {
  auto result = ReadPacket(response, timeout, sync_on_timeout);
  while (result == PacketResult::Success && response.IsNormalResponse() &&
         response.PeekChar() == 'O') {
    response.GetChar();
    std::string output;
    if (response.GetHexByteString(output))
      output_callback(output);
    result = ReadPacket(response, timeout, sync_on_timeout);
  }
  return result;
}

GDBRemoteCommunication::PacketResult
GDBRemoteClientBase::SendPacketAndReceiveResponseWithOutputSupport(
    llvm::StringRef payload, StringExtractorGDBRemote &response,
    std::chrono::seconds interrupt_timeout,
    llvm::function_ref<void(llvm::StringRef)> output_callback) {
  Lock lock(*this, interrupt_timeout);
  if (!lock) {
    if (Log *log = GetLog(GDBRLog::Process))
      LLDB_LOGF(log,
                "GDBRemoteClientBase::%s failed to get mutex, not sending "
                "packet '%.*s'",
                __FUNCTION__, int(payload.size()), payload.data());
    return PacketResult::ErrorSendFailed;
  }

  PacketResult packet_result = SendPacketNoLock(payload);
  if (packet_result != PacketResult::Success)
    return packet_result;

  return ReadPacketWithOutputSupport(response, GetPacketTimeout(), true,
                                     output_callback);
}

GDBRemoteCommunication::PacketResult
GDBRemoteClientBase::SendPacketAndWaitForResponseNoLock(
    llvm::StringRef payload, StringExtractorGDBRemote &response) {
  PacketResult packet_result = SendPacketNoLock(payload);
  if (packet_result != PacketResult::Success)
    return packet_result;

  const size_t max_response_retries = 3;
  for (size_t i = 0; i < max_response_retries; ++i) {
    packet_result = ReadPacket(response, GetPacketTimeout(), true);
    // Make sure we received a response
    if (packet_result != PacketResult::Success)
      return packet_result;
    // Make sure our response is valid for the payload that was sent
    if (response.ValidateResponse())
      return packet_result;
    // Response says it wasn't valid
    Log *log = GetLog(GDBRLog::Packets);
    LLDB_LOGF(
        log,
        "error: packet with payload \"%.*s\" got invalid response \"%s\": %s",
        int(payload.size()), payload.data(), response.GetStringRef().data(),
        (i == (max_response_retries - 1))
            ? "using invalid response and giving up"
            : "ignoring response and waiting for another");
  }
  return packet_result;
}

bool GDBRemoteClientBase::ShouldStop(const UnixSignals &signals,
                                     StringExtractorGDBRemote &response) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_async_count == 0)
    return true; // We were not interrupted. The process stopped on its own.

  // Older debugserver stubs (before April 2016) can return two stop-reply
  // packets in response to a ^C packet. Additionally, all debugservers still
  // return two stop replies if the inferior stops due to some other reason
  // before the remote stub manages to interrupt it. We need to wait for this
  // additional packet to make sure the packet sequence does not get skewed.
  StringExtractorGDBRemote extra_stop_reply_packet;
  ReadPacket(extra_stop_reply_packet, milliseconds(100), false);

  // Interrupting is typically done using SIGSTOP or SIGINT, so if the process
  // stops with some other signal, we definitely want to stop.
  const uint8_t signo = response.GetHexU8(UINT8_MAX);
  if (signo != signals.GetSignalNumberFromName("SIGSTOP") &&
      signo != signals.GetSignalNumberFromName("SIGINT"))
    return true;

  // We probably only stopped to perform some async processing, so continue
  // after that is done.
  // TODO: This is not 100% correct, as the process may have been stopped with
  // SIGINT or SIGSTOP that was not caused by us (e.g. raise(SIGINT)). This will
  // normally cause a stop, but if it's done concurrently with a async
  // interrupt, that stop will get eaten (llvm.org/pr20231).
  return false;
}

void GDBRemoteClientBase::OnRunPacketSent(bool first) {
  if (first)
    BroadcastEvent(eBroadcastBitRunPacketSent, nullptr);
}

///////////////////////////////////////
// GDBRemoteClientBase::ContinueLock //
///////////////////////////////////////

GDBRemoteClientBase::ContinueLock::ContinueLock(GDBRemoteClientBase &comm)
    : m_comm(comm), m_acquired(false) {
  lock();
}

GDBRemoteClientBase::ContinueLock::~ContinueLock() {
  if (m_acquired)
    unlock();
}

void GDBRemoteClientBase::ContinueLock::unlock() {
  lldbassert(m_acquired);
  {
    std::unique_lock<std::mutex> lock(m_comm.m_mutex);
    m_comm.m_is_running = false;
  }
  m_comm.m_cv.notify_all();
  m_acquired = false;
}

GDBRemoteClientBase::ContinueLock::LockResult
GDBRemoteClientBase::ContinueLock::lock() {
  Log *log = GetLog(GDBRLog::Process);
  LLDB_LOGF(log, "GDBRemoteClientBase::ContinueLock::%s() resuming with %s",
            __FUNCTION__, m_comm.m_continue_packet.c_str());

  lldbassert(!m_acquired);
  std::unique_lock<std::mutex> lock(m_comm.m_mutex);
  m_comm.m_cv.wait(lock, [this] { return m_comm.m_async_count == 0; });
  if (m_comm.m_should_stop) {
    m_comm.m_should_stop = false;
    LLDB_LOGF(log, "GDBRemoteClientBase::ContinueLock::%s() cancelled",
              __FUNCTION__);
    return LockResult::Cancelled;
  }
  if (m_comm.SendPacketNoLock(m_comm.m_continue_packet) !=
      PacketResult::Success)
    return LockResult::Failed;

  lldbassert(!m_comm.m_is_running);
  m_comm.m_is_running = true;
  m_acquired = true;
  return LockResult::Success;
}

///////////////////////////////
// GDBRemoteClientBase::Lock //
///////////////////////////////

GDBRemoteClientBase::Lock::Lock(GDBRemoteClientBase &comm,
                                std::chrono::seconds interrupt_timeout)
    : m_async_lock(comm.m_async_mutex, std::defer_lock), m_comm(comm),
      m_interrupt_timeout(interrupt_timeout), m_acquired(false),
      m_did_interrupt(false) {
  SyncWithContinueThread();
  if (m_acquired)
    m_async_lock.lock();
}

void GDBRemoteClientBase::Lock::SyncWithContinueThread() {
  Log *log = GetLog(GDBRLog::Process|GDBRLog::Packets);
  std::unique_lock<std::mutex> lock(m_comm.m_mutex);
  if (m_comm.m_is_running && m_interrupt_timeout == std::chrono::seconds(0))
    return; // We were asked to avoid interrupting the sender. Lock is not
            // acquired.

  ++m_comm.m_async_count;
  if (m_comm.m_is_running) {
    if (m_comm.m_async_count == 1) {
      // The sender has sent the continue packet and we are the first async
      // packet. Let's interrupt it.
      const char ctrl_c = '\x03';
      ConnectionStatus status = eConnectionStatusSuccess;
      size_t bytes_written = m_comm.Write(&ctrl_c, 1, status, nullptr);
      if (bytes_written == 0) {
        --m_comm.m_async_count;
        LLDB_LOGF(log, "GDBRemoteClientBase::Lock::Lock failed to send "
                       "interrupt packet");
        return;
      }
      m_comm.m_interrupt_endpoint = steady_clock::now() + m_interrupt_timeout;
      if (log)
        log->PutCString("GDBRemoteClientBase::Lock::Lock sent packet: \\x03");
    }
    m_comm.m_cv.wait(lock, [this] { return !m_comm.m_is_running; });
    m_did_interrupt = true;
  }
  m_acquired = true;
}

GDBRemoteClientBase::Lock::~Lock() {
  if (!m_acquired)
    return;
  {
    std::unique_lock<std::mutex> lock(m_comm.m_mutex);
    --m_comm.m_async_count;
  }
  m_comm.m_cv.notify_one();
}
