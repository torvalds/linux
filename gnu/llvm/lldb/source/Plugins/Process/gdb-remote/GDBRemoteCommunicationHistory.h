//===-- GDBRemoteCommunicationHistory.h--------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONHISTORY_H
#define LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONHISTORY_H

#include <string>
#include <vector>

#include "lldb/Utility/GDBRemote.h"
#include "lldb/lldb-public.h"
#include "llvm/Support/raw_ostream.h"

namespace lldb_private {
namespace process_gdb_remote {

/// The history keeps a circular buffer of GDB remote packets. The history is
/// used for logging and replaying GDB remote packets.
class GDBRemoteCommunicationHistory {
public:
  GDBRemoteCommunicationHistory(uint32_t size = 0);

  ~GDBRemoteCommunicationHistory();

  // For single char packets for ack, nack and /x03
  void AddPacket(char packet_char, GDBRemotePacket::Type type,
                 uint32_t bytes_transmitted);

  void AddPacket(const std::string &src, uint32_t src_len,
                 GDBRemotePacket::Type type, uint32_t bytes_transmitted);

  void Dump(Stream &strm) const;
  void Dump(Log *log) const;
  bool DidDumpToLog() const { return m_dumped_to_log; }

private:
  uint32_t GetFirstSavedPacketIndex() const {
    if (m_total_packet_count < m_packets.size())
      return 0;
    else
      return m_curr_idx + 1;
  }

  uint32_t GetNumPacketsInHistory() const {
    if (m_total_packet_count < m_packets.size())
      return m_total_packet_count;
    else
      return (uint32_t)m_packets.size();
  }

  uint32_t GetNextIndex() {
    ++m_total_packet_count;
    const uint32_t idx = m_curr_idx;
    m_curr_idx = NormalizeIndex(idx + 1);
    return idx;
  }

  uint32_t NormalizeIndex(uint32_t i) const {
    return m_packets.empty() ? 0 : i % m_packets.size();
  }

  std::vector<GDBRemotePacket> m_packets;
  uint32_t m_curr_idx = 0;
  uint32_t m_total_packet_count = 0;
  mutable bool m_dumped_to_log = false;
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONHISTORY_H
