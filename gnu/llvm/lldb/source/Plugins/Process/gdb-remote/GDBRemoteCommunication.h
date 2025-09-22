//===-- GDBRemoteCommunication.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATION_H
#define LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATION_H

#include "GDBRemoteCommunicationHistory.h"

#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "lldb/Core/Communication.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"
#include "lldb/lldb-public.h"

namespace lldb_private {
namespace repro {
class PacketRecorder;
}
namespace process_gdb_remote {

enum GDBStoppointType {
  eStoppointInvalid = -1,
  eBreakpointSoftware = 0,
  eBreakpointHardware,
  eWatchpointWrite,
  eWatchpointRead,
  eWatchpointReadWrite
};

enum class CompressionType {
  None = 0,    // no compression
  ZlibDeflate, // zlib's deflate compression scheme, requires zlib or Apple's
               // libcompression
  LZFSE,       // an Apple compression scheme, requires Apple's libcompression
  LZ4, // lz compression - called "lz4 raw" in libcompression terms, compat with
       // https://code.google.com/p/lz4/
  LZMA, // Lempel–Ziv–Markov chain algorithm
};

// Data included in the vFile:fstat packet.
// https://sourceware.org/gdb/onlinedocs/gdb/struct-stat.html#struct-stat
struct GDBRemoteFStatData {
  llvm::support::ubig32_t gdb_st_dev;
  llvm::support::ubig32_t gdb_st_ino;
  llvm::support::ubig32_t gdb_st_mode;
  llvm::support::ubig32_t gdb_st_nlink;
  llvm::support::ubig32_t gdb_st_uid;
  llvm::support::ubig32_t gdb_st_gid;
  llvm::support::ubig32_t gdb_st_rdev;
  llvm::support::ubig64_t gdb_st_size;
  llvm::support::ubig64_t gdb_st_blksize;
  llvm::support::ubig64_t gdb_st_blocks;
  llvm::support::ubig32_t gdb_st_atime;
  llvm::support::ubig32_t gdb_st_mtime;
  llvm::support::ubig32_t gdb_st_ctime;
};
static_assert(sizeof(GDBRemoteFStatData) == 64,
              "size of GDBRemoteFStatData is not 64");

enum GDBErrno {
#define HANDLE_ERRNO(name, value) GDB_##name = value,
#include "Plugins/Process/gdb-remote/GDBRemoteErrno.def"
  GDB_EUNKNOWN = 9999
};

class ProcessGDBRemote;

class GDBRemoteCommunication : public Communication {
public:
  enum class PacketType { Invalid = 0, Standard, Notify };

  enum class PacketResult {
    Success = 0,        // Success
    ErrorSendFailed,    // Status sending the packet
    ErrorSendAck,       // Didn't get an ack back after sending a packet
    ErrorReplyFailed,   // Status getting the reply
    ErrorReplyTimeout,  // Timed out waiting for reply
    ErrorReplyInvalid,  // Got a reply but it wasn't valid for the packet that
                        // was sent
    ErrorReplyAck,      // Sending reply ack failed
    ErrorDisconnected,  // We were disconnected
    ErrorNoSequenceLock // We couldn't get the sequence lock for a multi-packet
                        // request
  };

  // Class to change the timeout for a given scope and restore it to the
  // original value when the
  // created ScopedTimeout object got out of scope
  class ScopedTimeout {
  public:
    ScopedTimeout(GDBRemoteCommunication &gdb_comm,
                  std::chrono::seconds timeout);
    ~ScopedTimeout();

  private:
    GDBRemoteCommunication &m_gdb_comm;
    std::chrono::seconds m_saved_timeout;
    // Don't ever reduce the timeout for a packet, only increase it. If the
    // requested timeout if less than the current timeout, we don't set it
    // and won't need to restore it.
    bool m_timeout_modified;
  };

  GDBRemoteCommunication();

  ~GDBRemoteCommunication() override;

  PacketResult GetAck();

  size_t SendAck();

  size_t SendNack();

  char CalculcateChecksum(llvm::StringRef payload);

  PacketType CheckForPacket(const uint8_t *src, size_t src_len,
                            StringExtractorGDBRemote &packet);

  bool GetSendAcks() { return m_send_acks; }

  // Set the global packet timeout.
  //
  // For clients, this is the timeout that gets used when sending
  // packets and waiting for responses. For servers, this is used when waiting
  // for ACKs.
  std::chrono::seconds SetPacketTimeout(std::chrono::seconds packet_timeout) {
    const auto old_packet_timeout = m_packet_timeout;
    m_packet_timeout = packet_timeout;
    return old_packet_timeout;
  }

  std::chrono::seconds GetPacketTimeout() const { return m_packet_timeout; }

  // Start a debugserver instance on the current host using the
  // supplied connection URL.
  Status StartDebugserverProcess(
      const char *url,
      Platform *platform, // If non nullptr, then check with the platform for
                          // the GDB server binary if it can't be located
      ProcessLaunchInfo &launch_info, uint16_t *port, const Args *inferior_args,
      int pass_comm_fd); // Communication file descriptor to pass during
                         // fork/exec to avoid having to connect/accept

  void DumpHistory(Stream &strm);

  void SetPacketRecorder(repro::PacketRecorder *recorder);

  static llvm::Error ConnectLocally(GDBRemoteCommunication &client,
                                    GDBRemoteCommunication &server);

  /// Expand GDB run-length encoding.
  static std::string ExpandRLE(std::string);

protected:
  std::chrono::seconds m_packet_timeout;
  uint32_t m_echo_number;
  LazyBool m_supports_qEcho;
  GDBRemoteCommunicationHistory m_history;
  bool m_send_acks;
  bool m_is_platform; // Set to true if this class represents a platform,
                      // false if this class represents a debug session for
                      // a single process

  std::string m_bytes;
  std::recursive_mutex m_bytes_mutex;
  CompressionType m_compression_type;

  PacketResult SendPacketNoLock(llvm::StringRef payload);
  PacketResult SendNotificationPacketNoLock(llvm::StringRef notify_type,
                                            std::deque<std::string>& queue,
                                            llvm::StringRef payload);
  PacketResult SendRawPacketNoLock(llvm::StringRef payload,
                                   bool skip_ack = false);

  PacketResult ReadPacket(StringExtractorGDBRemote &response,
                          Timeout<std::micro> timeout, bool sync_on_timeout);

  PacketResult WaitForPacketNoLock(StringExtractorGDBRemote &response,
                                   Timeout<std::micro> timeout,
                                   bool sync_on_timeout);

  bool CompressionIsEnabled() {
    return m_compression_type != CompressionType::None;
  }

  // If compression is enabled, decompress the packet in m_bytes and update
  // m_bytes with the uncompressed version.
  // Returns 'true' packet was decompressed and m_bytes is the now-decompressed
  // text.
  // Returns 'false' if unable to decompress or if the checksum was invalid.
  //
  // NB: Once the packet has been decompressed, checksum cannot be computed
  // based
  // on m_bytes.  The checksum was for the compressed packet.
  bool DecompressPacket();

  Status StartListenThread(const char *hostname = "127.0.0.1",
                           uint16_t port = 0);

  bool JoinListenThread();

  lldb::thread_result_t ListenThread();

private:
  // Promise used to grab the port number from listening thread
  std::promise<uint16_t> m_port_promise;

  HostThread m_listen_thread;
  std::string m_listen_url;

#if defined(HAVE_LIBCOMPRESSION)
  CompressionType m_decompression_scratch_type = CompressionType::None;
  void *m_decompression_scratch = nullptr;
#endif

  GDBRemoteCommunication(const GDBRemoteCommunication &) = delete;
  const GDBRemoteCommunication &
  operator=(const GDBRemoteCommunication &) = delete;
};

} // namespace process_gdb_remote
} // namespace lldb_private

namespace llvm {
template <>
struct format_provider<
    lldb_private::process_gdb_remote::GDBRemoteCommunication::PacketResult> {
  static void format(const lldb_private::process_gdb_remote::
                         GDBRemoteCommunication::PacketResult &state,
                     raw_ostream &Stream, StringRef Style);
};
} // namespace llvm

#endif // LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATION_H
