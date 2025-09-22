//===-- AdbClient.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AdbClient.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileUtilities.h"

#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timeout.h"

#include <climits>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

// On Windows, transitive dependencies pull in <Windows.h>, which defines a
// macro that clashes with a method name.
#ifdef SendMessage
#undef SendMessage
#endif

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::platform_android;
using namespace std::chrono;

static const seconds kReadTimeout(20);
static const char *kOKAY = "OKAY";
static const char *kFAIL = "FAIL";
static const char *kDATA = "DATA";
static const char *kDONE = "DONE";

static const char *kSEND = "SEND";
static const char *kRECV = "RECV";
static const char *kSTAT = "STAT";

static const size_t kSyncPacketLen = 8;
// Maximum size of a filesync DATA packet.
static const size_t kMaxPushData = 2 * 1024;
// Default mode for pushed files.
static const uint32_t kDefaultMode = 0100770; // S_IFREG | S_IRWXU | S_IRWXG

static const char *kSocketNamespaceAbstract = "localabstract";
static const char *kSocketNamespaceFileSystem = "localfilesystem";

static Status ReadAllBytes(Connection &conn, void *buffer, size_t size) {

  Status error;
  ConnectionStatus status;
  char *read_buffer = static_cast<char *>(buffer);

  auto now = steady_clock::now();
  const auto deadline = now + kReadTimeout;
  size_t total_read_bytes = 0;
  while (total_read_bytes < size && now < deadline) {
    auto read_bytes =
        conn.Read(read_buffer + total_read_bytes, size - total_read_bytes,
                  duration_cast<microseconds>(deadline - now), status, &error);
    if (error.Fail())
      return error;
    total_read_bytes += read_bytes;
    if (status != eConnectionStatusSuccess)
      break;
    now = steady_clock::now();
  }
  if (total_read_bytes < size)
    error = Status(
        "Unable to read requested number of bytes. Connection status: %d.",
        status);
  return error;
}

Status AdbClient::CreateByDeviceID(const std::string &device_id,
                                   AdbClient &adb) {
  Status error;
  std::string android_serial;
  if (!device_id.empty())
    android_serial = device_id;
  else if (const char *env_serial = std::getenv("ANDROID_SERIAL"))
    android_serial = env_serial;

  if (android_serial.empty()) {
    DeviceIDList connected_devices;
    error = adb.GetDevices(connected_devices);
    if (error.Fail())
      return error;

    if (connected_devices.size() != 1)
      return Status("Expected a single connected device, got instead %zu - try "
                    "setting 'ANDROID_SERIAL'",
                    connected_devices.size());
    adb.SetDeviceID(connected_devices.front());
  } else {
    adb.SetDeviceID(android_serial);
  }
  return error;
}

AdbClient::AdbClient() = default;

AdbClient::AdbClient(const std::string &device_id) : m_device_id(device_id) {}

AdbClient::~AdbClient() = default;

void AdbClient::SetDeviceID(const std::string &device_id) {
  m_device_id = device_id;
}

const std::string &AdbClient::GetDeviceID() const { return m_device_id; }

Status AdbClient::Connect() {
  Status error;
  m_conn = std::make_unique<ConnectionFileDescriptor>();
  std::string port = "5037";
  if (const char *env_port = std::getenv("ANDROID_ADB_SERVER_PORT")) {
    port = env_port;
  }
  std::string uri = "connect://127.0.0.1:" + port;
  m_conn->Connect(uri.c_str(), &error);

  return error;
}

Status AdbClient::GetDevices(DeviceIDList &device_list) {
  device_list.clear();

  auto error = SendMessage("host:devices");
  if (error.Fail())
    return error;

  error = ReadResponseStatus();
  if (error.Fail())
    return error;

  std::vector<char> in_buffer;
  error = ReadMessage(in_buffer);

  llvm::StringRef response(&in_buffer[0], in_buffer.size());
  llvm::SmallVector<llvm::StringRef, 4> devices;
  response.split(devices, "\n", -1, false);

  for (const auto &device : devices)
    device_list.push_back(std::string(device.split('\t').first));

  // Force disconnect since ADB closes connection after host:devices response
  // is sent.
  m_conn.reset();
  return error;
}

Status AdbClient::SetPortForwarding(const uint16_t local_port,
                                    const uint16_t remote_port) {
  char message[48];
  snprintf(message, sizeof(message), "forward:tcp:%d;tcp:%d", local_port,
           remote_port);

  const auto error = SendDeviceMessage(message);
  if (error.Fail())
    return error;

  return ReadResponseStatus();
}

Status
AdbClient::SetPortForwarding(const uint16_t local_port,
                             llvm::StringRef remote_socket_name,
                             const UnixSocketNamespace socket_namespace) {
  char message[PATH_MAX];
  const char *sock_namespace_str =
      (socket_namespace == UnixSocketNamespaceAbstract)
          ? kSocketNamespaceAbstract
          : kSocketNamespaceFileSystem;
  snprintf(message, sizeof(message), "forward:tcp:%d;%s:%s", local_port,
           sock_namespace_str, remote_socket_name.str().c_str());

  const auto error = SendDeviceMessage(message);
  if (error.Fail())
    return error;

  return ReadResponseStatus();
}

Status AdbClient::DeletePortForwarding(const uint16_t local_port) {
  char message[32];
  snprintf(message, sizeof(message), "killforward:tcp:%d", local_port);

  const auto error = SendDeviceMessage(message);
  if (error.Fail())
    return error;

  return ReadResponseStatus();
}

Status AdbClient::SendMessage(const std::string &packet, const bool reconnect) {
  Status error;
  if (!m_conn || reconnect) {
    error = Connect();
    if (error.Fail())
      return error;
  }

  char length_buffer[5];
  snprintf(length_buffer, sizeof(length_buffer), "%04x",
           static_cast<int>(packet.size()));

  ConnectionStatus status;

  m_conn->Write(length_buffer, 4, status, &error);
  if (error.Fail())
    return error;

  m_conn->Write(packet.c_str(), packet.size(), status, &error);
  return error;
}

Status AdbClient::SendDeviceMessage(const std::string &packet) {
  std::ostringstream msg;
  msg << "host-serial:" << m_device_id << ":" << packet;
  return SendMessage(msg.str());
}

Status AdbClient::ReadMessage(std::vector<char> &message) {
  message.clear();

  char buffer[5];
  buffer[4] = 0;

  auto error = ReadAllBytes(buffer, 4);
  if (error.Fail())
    return error;

  unsigned int packet_len = 0;
  sscanf(buffer, "%x", &packet_len);

  message.resize(packet_len, 0);
  error = ReadAllBytes(&message[0], packet_len);
  if (error.Fail())
    message.clear();

  return error;
}

Status AdbClient::ReadMessageStream(std::vector<char> &message,
                                    milliseconds timeout) {
  auto start = steady_clock::now();
  message.clear();

  Status error;
  lldb::ConnectionStatus status = lldb::eConnectionStatusSuccess;
  char buffer[1024];
  while (error.Success() && status == lldb::eConnectionStatusSuccess) {
    auto end = steady_clock::now();
    auto elapsed = end - start;
    if (elapsed >= timeout)
      return Status("Timed out");

    size_t n = m_conn->Read(buffer, sizeof(buffer),
                            duration_cast<microseconds>(timeout - elapsed),
                            status, &error);
    if (n > 0)
      message.insert(message.end(), &buffer[0], &buffer[n]);
  }
  return error;
}

Status AdbClient::ReadResponseStatus() {
  char response_id[5];

  static const size_t packet_len = 4;
  response_id[packet_len] = 0;

  auto error = ReadAllBytes(response_id, packet_len);
  if (error.Fail())
    return error;

  if (strncmp(response_id, kOKAY, packet_len) != 0)
    return GetResponseError(response_id);

  return error;
}

Status AdbClient::GetResponseError(const char *response_id) {
  if (strcmp(response_id, kFAIL) != 0)
    return Status("Got unexpected response id from adb: \"%s\"", response_id);

  std::vector<char> error_message;
  auto error = ReadMessage(error_message);
  if (error.Success())
    error.SetErrorString(
        std::string(&error_message[0], error_message.size()).c_str());

  return error;
}

Status AdbClient::SwitchDeviceTransport() {
  std::ostringstream msg;
  msg << "host:transport:" << m_device_id;

  auto error = SendMessage(msg.str());
  if (error.Fail())
    return error;

  return ReadResponseStatus();
}

Status AdbClient::StartSync() {
  auto error = SwitchDeviceTransport();
  if (error.Fail())
    return Status("Failed to switch to device transport: %s",
                  error.AsCString());

  error = Sync();
  if (error.Fail())
    return Status("Sync failed: %s", error.AsCString());

  return error;
}

Status AdbClient::Sync() {
  auto error = SendMessage("sync:", false);
  if (error.Fail())
    return error;

  return ReadResponseStatus();
}

Status AdbClient::ReadAllBytes(void *buffer, size_t size) {
  return ::ReadAllBytes(*m_conn, buffer, size);
}

Status AdbClient::internalShell(const char *command, milliseconds timeout,
                                std::vector<char> &output_buf) {
  output_buf.clear();

  auto error = SwitchDeviceTransport();
  if (error.Fail())
    return Status("Failed to switch to device transport: %s",
                  error.AsCString());

  StreamString adb_command;
  adb_command.Printf("shell:%s", command);
  error = SendMessage(std::string(adb_command.GetString()), false);
  if (error.Fail())
    return error;

  error = ReadResponseStatus();
  if (error.Fail())
    return error;

  error = ReadMessageStream(output_buf, timeout);
  if (error.Fail())
    return error;

  // ADB doesn't propagate return code of shell execution - if
  // output starts with /system/bin/sh: most likely command failed.
  static const char *kShellPrefix = "/system/bin/sh:";
  if (output_buf.size() > strlen(kShellPrefix)) {
    if (!memcmp(&output_buf[0], kShellPrefix, strlen(kShellPrefix)))
      return Status("Shell command %s failed: %s", command,
                    std::string(output_buf.begin(), output_buf.end()).c_str());
  }

  return Status();
}

Status AdbClient::Shell(const char *command, milliseconds timeout,
                        std::string *output) {
  std::vector<char> output_buffer;
  auto error = internalShell(command, timeout, output_buffer);
  if (error.Fail())
    return error;

  if (output)
    output->assign(output_buffer.begin(), output_buffer.end());
  return error;
}

Status AdbClient::ShellToFile(const char *command, milliseconds timeout,
                              const FileSpec &output_file_spec) {
  std::vector<char> output_buffer;
  auto error = internalShell(command, timeout, output_buffer);
  if (error.Fail())
    return error;

  const auto output_filename = output_file_spec.GetPath();
  std::error_code EC;
  llvm::raw_fd_ostream dst(output_filename, EC, llvm::sys::fs::OF_None);
  if (EC)
    return Status("Unable to open local file %s", output_filename.c_str());

  dst.write(&output_buffer[0], output_buffer.size());
  dst.close();
  if (dst.has_error())
    return Status("Failed to write file %s", output_filename.c_str());
  return Status();
}

std::unique_ptr<AdbClient::SyncService>
AdbClient::GetSyncService(Status &error) {
  std::unique_ptr<SyncService> sync_service;
  error = StartSync();
  if (error.Success())
    sync_service.reset(new SyncService(std::move(m_conn)));

  return sync_service;
}

Status AdbClient::SyncService::internalPullFile(const FileSpec &remote_file,
                                                const FileSpec &local_file) {
  const auto local_file_path = local_file.GetPath();
  llvm::FileRemover local_file_remover(local_file_path);

  std::error_code EC;
  llvm::raw_fd_ostream dst(local_file_path, EC, llvm::sys::fs::OF_None);
  if (EC)
    return Status("Unable to open local file %s", local_file_path.c_str());

  const auto remote_file_path = remote_file.GetPath(false);
  auto error = SendSyncRequest(kRECV, remote_file_path.length(),
                               remote_file_path.c_str());
  if (error.Fail())
    return error;

  std::vector<char> chunk;
  bool eof = false;
  while (!eof) {
    error = PullFileChunk(chunk, eof);
    if (error.Fail())
      return error;
    if (!eof)
      dst.write(&chunk[0], chunk.size());
  }
  dst.close();
  if (dst.has_error())
    return Status("Failed to write file %s", local_file_path.c_str());

  local_file_remover.releaseFile();
  return error;
}

Status AdbClient::SyncService::internalPushFile(const FileSpec &local_file,
                                                const FileSpec &remote_file) {
  const auto local_file_path(local_file.GetPath());
  std::ifstream src(local_file_path.c_str(), std::ios::in | std::ios::binary);
  if (!src.is_open())
    return Status("Unable to open local file %s", local_file_path.c_str());

  std::stringstream file_description;
  file_description << remote_file.GetPath(false).c_str() << "," << kDefaultMode;
  std::string file_description_str = file_description.str();
  auto error = SendSyncRequest(kSEND, file_description_str.length(),
                               file_description_str.c_str());
  if (error.Fail())
    return error;

  char chunk[kMaxPushData];
  while (!src.eof() && !src.read(chunk, kMaxPushData).bad()) {
    size_t chunk_size = src.gcount();
    error = SendSyncRequest(kDATA, chunk_size, chunk);
    if (error.Fail())
      return Status("Failed to send file chunk: %s", error.AsCString());
  }
  error = SendSyncRequest(
      kDONE, llvm::sys::toTimeT(FileSystem::Instance().GetModificationTime(local_file)),
      nullptr);
  if (error.Fail())
    return error;

  std::string response_id;
  uint32_t data_len;
  error = ReadSyncHeader(response_id, data_len);
  if (error.Fail())
    return Status("Failed to read DONE response: %s", error.AsCString());
  if (response_id == kFAIL) {
    std::string error_message(data_len, 0);
    error = ReadAllBytes(&error_message[0], data_len);
    if (error.Fail())
      return Status("Failed to read DONE error message: %s", error.AsCString());
    return Status("Failed to push file: %s", error_message.c_str());
  } else if (response_id != kOKAY)
    return Status("Got unexpected DONE response: %s", response_id.c_str());

  // If there was an error reading the source file, finish the adb file
  // transfer first so that adb isn't expecting any more data.
  if (src.bad())
    return Status("Failed read on %s", local_file_path.c_str());
  return error;
}

Status AdbClient::SyncService::internalStat(const FileSpec &remote_file,
                                            uint32_t &mode, uint32_t &size,
                                            uint32_t &mtime) {
  const std::string remote_file_path(remote_file.GetPath(false));
  auto error = SendSyncRequest(kSTAT, remote_file_path.length(),
                               remote_file_path.c_str());
  if (error.Fail())
    return Status("Failed to send request: %s", error.AsCString());

  static const size_t stat_len = strlen(kSTAT);
  static const size_t response_len = stat_len + (sizeof(uint32_t) * 3);

  std::vector<char> buffer(response_len);
  error = ReadAllBytes(&buffer[0], buffer.size());
  if (error.Fail())
    return Status("Failed to read response: %s", error.AsCString());

  DataExtractor extractor(&buffer[0], buffer.size(), eByteOrderLittle,
                          sizeof(void *));
  offset_t offset = 0;

  const void *command = extractor.GetData(&offset, stat_len);
  if (!command)
    return Status("Failed to get response command");
  const char *command_str = static_cast<const char *>(command);
  if (strncmp(command_str, kSTAT, stat_len))
    return Status("Got invalid stat command: %s", command_str);

  mode = extractor.GetU32(&offset);
  size = extractor.GetU32(&offset);
  mtime = extractor.GetU32(&offset);
  return Status();
}

Status AdbClient::SyncService::PullFile(const FileSpec &remote_file,
                                        const FileSpec &local_file) {
  return executeCommand([this, &remote_file, &local_file]() {
    return internalPullFile(remote_file, local_file);
  });
}

Status AdbClient::SyncService::PushFile(const FileSpec &local_file,
                                        const FileSpec &remote_file) {
  return executeCommand([this, &local_file, &remote_file]() {
    return internalPushFile(local_file, remote_file);
  });
}

Status AdbClient::SyncService::Stat(const FileSpec &remote_file, uint32_t &mode,
                                    uint32_t &size, uint32_t &mtime) {
  return executeCommand([this, &remote_file, &mode, &size, &mtime]() {
    return internalStat(remote_file, mode, size, mtime);
  });
}

bool AdbClient::SyncService::IsConnected() const {
  return m_conn && m_conn->IsConnected();
}

AdbClient::SyncService::SyncService(std::unique_ptr<Connection> &&conn)
    : m_conn(std::move(conn)) {}

Status
AdbClient::SyncService::executeCommand(const std::function<Status()> &cmd) {
  if (!m_conn)
    return Status("SyncService is disconnected");

  const auto error = cmd();
  if (error.Fail())
    m_conn.reset();

  return error;
}

AdbClient::SyncService::~SyncService() = default;

Status AdbClient::SyncService::SendSyncRequest(const char *request_id,
                                               const uint32_t data_len,
                                               const void *data) {
  DataEncoder encoder(eByteOrderLittle, sizeof(void *));
  encoder.AppendData(llvm::StringRef(request_id));
  encoder.AppendU32(data_len);
  llvm::ArrayRef<uint8_t> bytes = encoder.GetData();
  Status error;
  ConnectionStatus status;
  m_conn->Write(bytes.data(), kSyncPacketLen, status, &error);
  if (error.Fail())
    return error;

  if (data)
    m_conn->Write(data, data_len, status, &error);
  return error;
}

Status AdbClient::SyncService::ReadSyncHeader(std::string &response_id,
                                              uint32_t &data_len) {
  char buffer[kSyncPacketLen];

  auto error = ReadAllBytes(buffer, kSyncPacketLen);
  if (error.Success()) {
    response_id.assign(&buffer[0], 4);
    DataExtractor extractor(&buffer[4], 4, eByteOrderLittle, sizeof(void *));
    offset_t offset = 0;
    data_len = extractor.GetU32(&offset);
  }

  return error;
}

Status AdbClient::SyncService::PullFileChunk(std::vector<char> &buffer,
                                             bool &eof) {
  buffer.clear();

  std::string response_id;
  uint32_t data_len;
  auto error = ReadSyncHeader(response_id, data_len);
  if (error.Fail())
    return error;

  if (response_id == kDATA) {
    buffer.resize(data_len, 0);
    error = ReadAllBytes(&buffer[0], data_len);
    if (error.Fail())
      buffer.clear();
  } else if (response_id == kDONE) {
    eof = true;
  } else if (response_id == kFAIL) {
    std::string error_message(data_len, 0);
    error = ReadAllBytes(&error_message[0], data_len);
    if (error.Fail())
      return Status("Failed to read pull error message: %s", error.AsCString());
    return Status("Failed to pull file: %s", error_message.c_str());
  } else
    return Status("Pull failed with unknown response: %s", response_id.c_str());

  return Status();
}

Status AdbClient::SyncService::ReadAllBytes(void *buffer, size_t size) {
  return ::ReadAllBytes(*m_conn, buffer, size);
}
