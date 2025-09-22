//===-- GDBRemote.h ----------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_GDBREMOTE_H
#define LLDB_UTILITY_GDBREMOTE_H

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"
#include "llvm/Support/raw_ostream.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lldb_private {

class StreamGDBRemote : public StreamString {
public:
  StreamGDBRemote();

  StreamGDBRemote(uint32_t flags, uint32_t addr_size,
                  lldb::ByteOrder byte_order);

  ~StreamGDBRemote() override;

  /// Output a block of data to the stream performing GDB-remote escaping.
  ///
  /// \param[in] s
  ///     A block of data.
  ///
  /// \param[in] src_len
  ///     The amount of data to write.
  ///
  /// \return
  ///     Number of bytes written.
  // TODO: Convert this function to take ArrayRef<uint8_t>
  int PutEscapedBytes(const void *s, size_t src_len);
};

/// GDB remote packet as used by the GDB remote communication history. Packets
/// can be serialized to file.
struct GDBRemotePacket {

  enum Type { ePacketTypeInvalid = 0, ePacketTypeSend, ePacketTypeRecv };

  GDBRemotePacket() = default;

  void Clear() {
    packet.data.clear();
    type = ePacketTypeInvalid;
    bytes_transmitted = 0;
    packet_idx = 0;
    tid = LLDB_INVALID_THREAD_ID;
  }

  struct BinaryData {
    std::string data;
  };

  void Dump(Stream &strm) const;

  BinaryData packet;
  Type type = ePacketTypeInvalid;
  uint32_t bytes_transmitted = 0;
  uint32_t packet_idx = 0;
  lldb::tid_t tid = LLDB_INVALID_THREAD_ID;

private:
  llvm::StringRef GetTypeStr() const;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_GDBREMOTE_H
