//===-- GDBRemoteCommunicationServerCommon.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONSERVERCOMMON_H
#define LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONSERVERCOMMON_H

#include <string>

#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/lldb-private-forward.h"

#include "GDBRemoteCommunicationServer.h"

class StringExtractorGDBRemote;

namespace lldb_private {
namespace process_gdb_remote {

class ProcessGDBRemote;

class GDBRemoteCommunicationServerCommon : public GDBRemoteCommunicationServer {
public:
  GDBRemoteCommunicationServerCommon();

  ~GDBRemoteCommunicationServerCommon() override;

protected:
  ProcessLaunchInfo m_process_launch_info;
  Status m_process_launch_error;
  ProcessInstanceInfoList m_proc_infos;
  uint32_t m_proc_infos_index;

  PacketResult Handle_A(StringExtractorGDBRemote &packet);

  PacketResult Handle_qHostInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qProcessInfoPID(StringExtractorGDBRemote &packet);

  PacketResult Handle_qfProcessInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qsProcessInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qUserName(StringExtractorGDBRemote &packet);

  PacketResult Handle_qGroupName(StringExtractorGDBRemote &packet);

  PacketResult Handle_qSpeedTest(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Open(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Close(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_pRead(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_pWrite(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Size(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Mode(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Exists(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_symlink(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_unlink(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_FStat(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_Stat(StringExtractorGDBRemote &packet);

  PacketResult Handle_vFile_MD5(StringExtractorGDBRemote &packet);

  PacketResult Handle_qEcho(StringExtractorGDBRemote &packet);

  PacketResult Handle_qModuleInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_jModulesInfo(StringExtractorGDBRemote &packet);

  PacketResult Handle_qPlatform_shell(StringExtractorGDBRemote &packet);

  PacketResult Handle_qPlatform_mkdir(StringExtractorGDBRemote &packet);

  PacketResult Handle_qPlatform_chmod(StringExtractorGDBRemote &packet);

  PacketResult Handle_qSupported(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetDetachOnError(StringExtractorGDBRemote &packet);

  PacketResult Handle_QStartNoAckMode(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetSTDIN(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetSTDOUT(StringExtractorGDBRemote &packet);

  PacketResult Handle_QSetSTDERR(StringExtractorGDBRemote &packet);

  PacketResult Handle_qLaunchSuccess(StringExtractorGDBRemote &packet);

  PacketResult Handle_QEnvironment(StringExtractorGDBRemote &packet);

  PacketResult Handle_QEnvironmentHexEncoded(StringExtractorGDBRemote &packet);

  PacketResult Handle_QLaunchArch(StringExtractorGDBRemote &packet);

  static void CreateProcessInfoResponse(const ProcessInstanceInfo &proc_info,
                                        StreamString &response);

  static void CreateProcessInfoResponse_DebugServerStyle(
      const ProcessInstanceInfo &proc_info, StreamString &response);

  template <typename T>
  void RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::ServerPacketType packet_type,
      PacketResult (T::*handler)(StringExtractorGDBRemote &packet)) {
    RegisterPacketHandler(packet_type,
                          [this, handler](StringExtractorGDBRemote packet,
                                          Status &error, bool &interrupt,
                                          bool &quit) {
                            return (static_cast<T *>(this)->*handler)(packet);
                          });
  }

  /// Launch a process with the current launch settings.
  ///
  /// This method supports running an lldb-gdbserver or similar
  /// server in a situation where the startup code has been provided
  /// with all the information for a child process to be launched.
  ///
  /// \return
  ///     An Status object indicating the success or failure of the
  ///     launch.
  virtual Status LaunchProcess() = 0;

  virtual FileSpec FindModuleFile(const std::string &module_path,
                                  const ArchSpec &arch);

  // Process client_features (qSupported) and return an array of server features
  // to be returned in response.
  virtual std::vector<std::string>
  HandleFeatures(llvm::ArrayRef<llvm::StringRef> client_features);

private:
  ModuleSpec GetModuleInfo(llvm::StringRef module_path, llvm::StringRef triple);
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_GDB_REMOTE_GDBREMOTECOMMUNICATIONSERVERCOMMON_H
