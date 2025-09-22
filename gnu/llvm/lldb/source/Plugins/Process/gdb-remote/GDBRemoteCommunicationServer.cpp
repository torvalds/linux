//===-- GDBRemoteCommunicationServer.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>

#include "lldb/Host/Config.h"

#include "GDBRemoteCommunicationServer.h"

#include "ProcessGDBRemoteLog.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"
#include "lldb/Utility/UnimplementedError.h"
#include "llvm/Support/JSON.h"
#include <cstring>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;
using namespace llvm;

GDBRemoteCommunicationServer::GDBRemoteCommunicationServer()
    : GDBRemoteCommunication(), m_exit_now(false) {
  RegisterPacketHandler(
      StringExtractorGDBRemote::eServerPacketType_QEnableErrorStrings,
      [this](StringExtractorGDBRemote packet, Status &error, bool &interrupt,
             bool &quit) { return this->Handle_QErrorStringEnable(packet); });
}

GDBRemoteCommunicationServer::~GDBRemoteCommunicationServer() = default;

void GDBRemoteCommunicationServer::RegisterPacketHandler(
    StringExtractorGDBRemote::ServerPacketType packet_type,
    PacketHandler handler) {
  m_packet_handlers[packet_type] = std::move(handler);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::GetPacketAndSendResponse(
    Timeout<std::micro> timeout, Status &error, bool &interrupt, bool &quit) {
  StringExtractorGDBRemote packet;

  PacketResult packet_result = ReadPacket(packet, timeout, false);
  if (packet_result == PacketResult::Success) {
    const StringExtractorGDBRemote::ServerPacketType packet_type =
        packet.GetServerPacketType();
    switch (packet_type) {
    case StringExtractorGDBRemote::eServerPacketType_nack:
    case StringExtractorGDBRemote::eServerPacketType_ack:
      break;

    case StringExtractorGDBRemote::eServerPacketType_invalid:
      error.SetErrorString("invalid packet");
      quit = true;
      break;

    case StringExtractorGDBRemote::eServerPacketType_unimplemented:
      packet_result = SendUnimplementedResponse(packet.GetStringRef().data());
      break;

    default:
      auto handler_it = m_packet_handlers.find(packet_type);
      if (handler_it == m_packet_handlers.end())
        packet_result = SendUnimplementedResponse(packet.GetStringRef().data());
      else
        packet_result = handler_it->second(packet, error, interrupt, quit);
      break;
    }
  } else {
    if (!IsConnected()) {
      error.SetErrorString("lost connection");
      quit = true;
    } else {
      error.SetErrorString("timeout");
    }
  }

  // Check if anything occurred that would force us to want to exit.
  if (m_exit_now)
    quit = true;

  return packet_result;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendUnimplementedResponse(const char *) {
  // TODO: Log the packet we aren't handling...
  return SendPacketNoLock("");
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendErrorResponse(uint8_t err) {
  char packet[16];
  int packet_len = ::snprintf(packet, sizeof(packet), "E%2.2x", err);
  assert(packet_len < (int)sizeof(packet));
  return SendPacketNoLock(llvm::StringRef(packet, packet_len));
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendErrorResponse(const Status &error) {
  if (m_send_error_strings) {
    lldb_private::StreamString packet;
    packet.Printf("E%2.2x;", static_cast<uint8_t>(error.GetError()));
    packet.PutStringAsRawHex8(error.AsCString());
    return SendPacketNoLock(packet.GetString());
  } else
    return SendErrorResponse(error.GetError());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendErrorResponse(llvm::Error error) {
  assert(error);
  std::unique_ptr<llvm::ErrorInfoBase> EIB;
  std::unique_ptr<UnimplementedError> UE;
  llvm::handleAllErrors(
      std::move(error),
      [&](std::unique_ptr<UnimplementedError> E) { UE = std::move(E); },
      [&](std::unique_ptr<llvm::ErrorInfoBase> E) { EIB = std::move(E); });

  if (EIB)
    return SendErrorResponse(Status(llvm::Error(std::move(EIB))));
  return SendUnimplementedResponse("");
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::Handle_QErrorStringEnable(
    StringExtractorGDBRemote &packet) {
  m_send_error_strings = true;
  return SendOKResponse();
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendIllFormedResponse(
    const StringExtractorGDBRemote &failed_packet, const char *message) {
  Log *log = GetLog(GDBRLog::Packets);
  LLDB_LOGF(log, "GDBRemoteCommunicationServer::%s: ILLFORMED: '%s' (%s)",
            __FUNCTION__, failed_packet.GetStringRef().data(),
            message ? message : "");
  return SendErrorResponse(0x03);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendOKResponse() {
  return SendPacketNoLock("OK");
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendJSONResponse(const json::Value &value) {
  std::string json_string;
  raw_string_ostream os(json_string);
  os << value;
  os.flush();
  StreamGDBRemote escaped_response;
  escaped_response.PutEscapedBytes(json_string.c_str(), json_string.size());
  return SendPacketNoLock(escaped_response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServer::SendJSONResponse(Expected<json::Value> value) {
  if (!value)
    return SendErrorResponse(value.takeError());
  return SendJSONResponse(*value);
}
