//===-- GDBRemoteCommunicationServerCommon.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteCommunicationServerCommon.h"

#include <cerrno>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <chrono>
#include <cstring>
#include <optional>

#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/File.h"
#include "lldb/Host/FileAction.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/SafeMachO.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Platform.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/GDBRemote.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/JSON.h"
#include "llvm/TargetParser/Triple.h"

#include "ProcessGDBRemoteLog.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"

#ifdef __ANDROID__
#include "lldb/Host/android/HostInfoAndroid.h"
#include "lldb/Host/common/ZipFileResolver.h"
#endif

using namespace lldb;
using namespace lldb_private::process_gdb_remote;
using namespace lldb_private;

#ifdef __ANDROID__
const static uint32_t g_default_packet_timeout_sec = 20; // seconds
#else
const static uint32_t g_default_packet_timeout_sec = 0; // not specified
#endif

// GDBRemoteCommunicationServerCommon constructor
GDBRemoteCommunicationServerCommon::GDBRemoteCommunicationServerCommon()
    : GDBRemoteCommunicationServer(), m_process_launch_info(),
      m_process_launch_error(), m_proc_infos(), m_proc_infos_index(0) {
  RegisterMemberFunctionHandler(StringExtractorGDBRemote::eServerPacketType_A,
                                &GDBRemoteCommunicationServerCommon::Handle_A);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QEnvironment,
      &GDBRemoteCommunicationServerCommon::Handle_QEnvironment);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QEnvironmentHexEncoded,
      &GDBRemoteCommunicationServerCommon::Handle_QEnvironmentHexEncoded);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qfProcessInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qfProcessInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qGroupName,
      &GDBRemoteCommunicationServerCommon::Handle_qGroupName);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qHostInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qHostInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QLaunchArch,
      &GDBRemoteCommunicationServerCommon::Handle_QLaunchArch);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qLaunchSuccess,
      &GDBRemoteCommunicationServerCommon::Handle_qLaunchSuccess);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qEcho,
      &GDBRemoteCommunicationServerCommon::Handle_qEcho);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qModuleInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qModuleInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_jModulesInfo,
      &GDBRemoteCommunicationServerCommon::Handle_jModulesInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qPlatform_chmod,
      &GDBRemoteCommunicationServerCommon::Handle_qPlatform_chmod);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qPlatform_mkdir,
      &GDBRemoteCommunicationServerCommon::Handle_qPlatform_mkdir);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qPlatform_shell,
      &GDBRemoteCommunicationServerCommon::Handle_qPlatform_shell);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qProcessInfoPID,
      &GDBRemoteCommunicationServerCommon::Handle_qProcessInfoPID);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetDetachOnError,
      &GDBRemoteCommunicationServerCommon::Handle_QSetDetachOnError);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetSTDERR,
      &GDBRemoteCommunicationServerCommon::Handle_QSetSTDERR);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetSTDIN,
      &GDBRemoteCommunicationServerCommon::Handle_QSetSTDIN);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetSTDOUT,
      &GDBRemoteCommunicationServerCommon::Handle_QSetSTDOUT);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qSpeedTest,
      &GDBRemoteCommunicationServerCommon::Handle_qSpeedTest);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qsProcessInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qsProcessInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QStartNoAckMode,
      &GDBRemoteCommunicationServerCommon::Handle_QStartNoAckMode);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qSupported,
      &GDBRemoteCommunicationServerCommon::Handle_qSupported);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qUserName,
      &GDBRemoteCommunicationServerCommon::Handle_qUserName);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_close,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Close);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_exists,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Exists);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_md5,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_MD5);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_mode,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Mode);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_open,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Open);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_pread,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_pRead);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_pwrite,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_pWrite);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_size,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Size);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_fstat,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_FStat);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_stat,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Stat);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_symlink,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_symlink);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_unlink,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_unlink);
}

// Destructor
GDBRemoteCommunicationServerCommon::~GDBRemoteCommunicationServerCommon() =
    default;

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qHostInfo(
    StringExtractorGDBRemote &packet) {
  StreamString response;

  // $cputype:16777223;cpusubtype:3;ostype:Darwin;vendor:apple;endian:little;ptrsize:8;#00

  ArchSpec host_arch(HostInfo::GetArchitecture());
  const llvm::Triple &host_triple = host_arch.GetTriple();
  response.PutCString("triple:");
  response.PutStringAsRawHex8(host_triple.getTriple());
  response.Printf(";ptrsize:%u;", host_arch.GetAddressByteSize());

  llvm::StringRef distribution_id = HostInfo::GetDistributionId();
  if (!distribution_id.empty()) {
    response.PutCString("distribution_id:");
    response.PutStringAsRawHex8(distribution_id);
    response.PutCString(";");
  }

#if defined(__APPLE__)
  // For parity with debugserver, we'll include the vendor key.
  response.PutCString("vendor:apple;");

  // Send out MachO info.
  uint32_t cpu = host_arch.GetMachOCPUType();
  uint32_t sub = host_arch.GetMachOCPUSubType();
  if (cpu != LLDB_INVALID_CPUTYPE)
    response.Printf("cputype:%u;", cpu);
  if (sub != LLDB_INVALID_CPUTYPE)
    response.Printf("cpusubtype:%u;", sub);

  if (cpu == llvm::MachO::CPU_TYPE_ARM || cpu == llvm::MachO::CPU_TYPE_ARM64) {
// Indicate the OS type.
#if defined(TARGET_OS_TV) && TARGET_OS_TV == 1
    response.PutCString("ostype:tvos;");
#elif defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
    response.PutCString("ostype:watchos;");
#elif defined(TARGET_OS_BRIDGE) && TARGET_OS_BRIDGE == 1
    response.PutCString("ostype:bridgeos;");
#else
    response.PutCString("ostype:ios;");
#endif

    // On arm, we use "synchronous" watchpoints which means the exception is
    // delivered before the instruction executes.
    response.PutCString("watchpoint_exceptions_received:before;");
  } else {
    response.PutCString("ostype:macosx;");
    response.Printf("watchpoint_exceptions_received:after;");
  }

#else
  if (host_arch.GetMachine() == llvm::Triple::aarch64 ||
      host_arch.GetMachine() == llvm::Triple::aarch64_32 ||
      host_arch.GetMachine() == llvm::Triple::aarch64_be ||
      host_arch.GetMachine() == llvm::Triple::arm ||
      host_arch.GetMachine() == llvm::Triple::armeb || host_arch.IsMIPS())
    response.Printf("watchpoint_exceptions_received:before;");
  else
    response.Printf("watchpoint_exceptions_received:after;");
#endif

  switch (endian::InlHostByteOrder()) {
  case eByteOrderBig:
    response.PutCString("endian:big;");
    break;
  case eByteOrderLittle:
    response.PutCString("endian:little;");
    break;
  case eByteOrderPDP:
    response.PutCString("endian:pdp;");
    break;
  default:
    response.PutCString("endian:unknown;");
    break;
  }

  llvm::VersionTuple version = HostInfo::GetOSVersion();
  if (!version.empty()) {
    response.Format("os_version:{0}", version.getAsString());
    response.PutChar(';');
  }

#if defined(__APPLE__)
  llvm::VersionTuple maccatalyst_version = HostInfo::GetMacCatalystVersion();
  if (!maccatalyst_version.empty()) {
    response.Format("maccatalyst_version:{0}",
                    maccatalyst_version.getAsString());
    response.PutChar(';');
  }
#endif

  if (std::optional<std::string> s = HostInfo::GetOSBuildString()) {
    response.PutCString("os_build:");
    response.PutStringAsRawHex8(*s);
    response.PutChar(';');
  }
  if (std::optional<std::string> s = HostInfo::GetOSKernelDescription()) {
    response.PutCString("os_kernel:");
    response.PutStringAsRawHex8(*s);
    response.PutChar(';');
  }

  std::string s;
#if defined(__APPLE__)

#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  // For iOS devices, we are connected through a USB Mux so we never pretend to
  // actually have a hostname as far as the remote lldb that is connecting to
  // this lldb-platform is concerned
  response.PutCString("hostname:");
  response.PutStringAsRawHex8("127.0.0.1");
  response.PutChar(';');
#else  // #if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  if (HostInfo::GetHostname(s)) {
    response.PutCString("hostname:");
    response.PutStringAsRawHex8(s);
    response.PutChar(';');
  }
#endif // #if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)

#else  // #if defined(__APPLE__)
  if (HostInfo::GetHostname(s)) {
    response.PutCString("hostname:");
    response.PutStringAsRawHex8(s);
    response.PutChar(';');
  }
#endif // #if defined(__APPLE__)
  // coverity[unsigned_compare]
  if (g_default_packet_timeout_sec > 0)
    response.Printf("default_packet_timeout:%u;", g_default_packet_timeout_sec);

  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qProcessInfoPID(
    StringExtractorGDBRemote &packet) {
  // Packet format: "qProcessInfoPID:%i" where %i is the pid
  packet.SetFilePos(::strlen("qProcessInfoPID:"));
  lldb::pid_t pid = packet.GetU32(LLDB_INVALID_PROCESS_ID);
  if (pid != LLDB_INVALID_PROCESS_ID) {
    ProcessInstanceInfo proc_info;
    if (Host::GetProcessInfo(pid, proc_info)) {
      StreamString response;
      CreateProcessInfoResponse(proc_info, response);
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(1);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qfProcessInfo(
    StringExtractorGDBRemote &packet) {
  m_proc_infos_index = 0;
  m_proc_infos.clear();

  ProcessInstanceInfoMatch match_info;
  packet.SetFilePos(::strlen("qfProcessInfo"));
  if (packet.GetChar() == ':') {
    llvm::StringRef key;
    llvm::StringRef value;
    while (packet.GetNameColonValue(key, value)) {
      bool success = true;
      if (key == "name") {
        StringExtractor extractor(value);
        std::string file;
        extractor.GetHexByteString(file);
        match_info.GetProcessInfo().GetExecutableFile().SetFile(
            file, FileSpec::Style::native);
      } else if (key == "name_match") {
        NameMatch name_match = llvm::StringSwitch<NameMatch>(value)
                                   .Case("equals", NameMatch::Equals)
                                   .Case("starts_with", NameMatch::StartsWith)
                                   .Case("ends_with", NameMatch::EndsWith)
                                   .Case("contains", NameMatch::Contains)
                                   .Case("regex", NameMatch::RegularExpression)
                                   .Default(NameMatch::Ignore);
        match_info.SetNameMatchType(name_match);
        if (name_match == NameMatch::Ignore)
          return SendErrorResponse(2);
      } else if (key == "pid") {
        lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
        if (value.getAsInteger(0, pid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetProcessID(pid);
      } else if (key == "parent_pid") {
        lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
        if (value.getAsInteger(0, pid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetParentProcessID(pid);
      } else if (key == "uid") {
        uint32_t uid = UINT32_MAX;
        if (value.getAsInteger(0, uid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetUserID(uid);
      } else if (key == "gid") {
        uint32_t gid = UINT32_MAX;
        if (value.getAsInteger(0, gid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetGroupID(gid);
      } else if (key == "euid") {
        uint32_t uid = UINT32_MAX;
        if (value.getAsInteger(0, uid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetEffectiveUserID(uid);
      } else if (key == "egid") {
        uint32_t gid = UINT32_MAX;
        if (value.getAsInteger(0, gid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetEffectiveGroupID(gid);
      } else if (key == "all_users") {
        match_info.SetMatchAllUsers(
            OptionArgParser::ToBoolean(value, false, &success));
      } else if (key == "triple") {
        match_info.GetProcessInfo().GetArchitecture() =
            HostInfo::GetAugmentedArchSpec(value);
      } else {
        success = false;
      }

      if (!success)
        return SendErrorResponse(2);
    }
  }

  if (Host::FindProcesses(match_info, m_proc_infos)) {
    // We found something, return the first item by calling the get subsequent
    // process info packet handler...
    return Handle_qsProcessInfo(packet);
  }
  return SendErrorResponse(3);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qsProcessInfo(
    StringExtractorGDBRemote &packet) {
  if (m_proc_infos_index < m_proc_infos.size()) {
    StreamString response;
    CreateProcessInfoResponse(m_proc_infos[m_proc_infos_index], response);
    ++m_proc_infos_index;
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(4);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qUserName(
    StringExtractorGDBRemote &packet) {
#if LLDB_ENABLE_POSIX
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log, "GDBRemoteCommunicationServerCommon::%s begin", __FUNCTION__);

  // Packet format: "qUserName:%i" where %i is the uid
  packet.SetFilePos(::strlen("qUserName:"));
  uint32_t uid = packet.GetU32(UINT32_MAX);
  if (uid != UINT32_MAX) {
    if (std::optional<llvm::StringRef> name =
            HostInfo::GetUserIDResolver().GetUserName(uid)) {
      StreamString response;
      response.PutStringAsRawHex8(*name);
      return SendPacketNoLock(response.GetString());
    }
  }
  LLDB_LOGF(log, "GDBRemoteCommunicationServerCommon::%s end", __FUNCTION__);
#endif
  return SendErrorResponse(5);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qGroupName(
    StringExtractorGDBRemote &packet) {
#if LLDB_ENABLE_POSIX
  // Packet format: "qGroupName:%i" where %i is the gid
  packet.SetFilePos(::strlen("qGroupName:"));
  uint32_t gid = packet.GetU32(UINT32_MAX);
  if (gid != UINT32_MAX) {
    if (std::optional<llvm::StringRef> name =
            HostInfo::GetUserIDResolver().GetGroupName(gid)) {
      StreamString response;
      response.PutStringAsRawHex8(*name);
      return SendPacketNoLock(response.GetString());
    }
  }
#endif
  return SendErrorResponse(6);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qSpeedTest(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qSpeedTest:"));

  llvm::StringRef key;
  llvm::StringRef value;
  bool success = packet.GetNameColonValue(key, value);
  if (success && key == "response_size") {
    uint32_t response_size = 0;
    if (!value.getAsInteger(0, response_size)) {
      if (response_size == 0)
        return SendOKResponse();
      StreamString response;
      uint32_t bytes_left = response_size;
      response.PutCString("data:");
      while (bytes_left > 0) {
        if (bytes_left >= 26) {
          response.PutCString("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
          bytes_left -= 26;
        } else {
          response.Printf("%*.*s;", bytes_left, bytes_left,
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
          bytes_left = 0;
        }
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(7);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Open(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:open:"));
  std::string path;
  packet.GetHexByteStringTerminatedBy(path, ',');
  if (!path.empty()) {
    if (packet.GetChar() == ',') {
      auto flags = File::OpenOptions(packet.GetHexMaxU32(false, 0));
      if (packet.GetChar() == ',') {
        mode_t mode = packet.GetHexMaxU32(false, 0600);
        FileSpec path_spec(path);
        FileSystem::Instance().Resolve(path_spec);
        // Do not close fd.
        auto file = FileSystem::Instance().Open(path_spec, flags, mode, false);

        StreamString response;
        response.PutChar('F');

        int descriptor = File::kInvalidDescriptor;
        if (file) {
          descriptor = file.get()->GetDescriptor();
          response.Printf("%x", descriptor);
        } else {
          response.PutCString("-1");
          std::error_code code = errorToErrorCode(file.takeError());
          if (code.category() == std::system_category()) {
            response.Printf(",%x", code.value());
          }
        }

        return SendPacketNoLock(response.GetString());
      }
    }
  }
  return SendErrorResponse(18);
}

static GDBErrno system_errno_to_gdb(int err) {
  switch (err) {
#define HANDLE_ERRNO(name, value)                                              \
  case name:                                                                   \
    return GDB_##name;
#include "Plugins/Process/gdb-remote/GDBRemoteErrno.def"
  default:
    return GDB_EUNKNOWN;
  }
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Close(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:close:"));
  int fd = packet.GetS32(-1, 16);
  int err = -1;
  int save_errno = 0;
  if (fd >= 0) {
    NativeFile file(fd, File::OpenOptions(0), true);
    Status error = file.Close();
    err = 0;
    save_errno = error.GetError();
  } else {
    save_errno = EINVAL;
  }
  StreamString response;
  response.PutChar('F');
  response.Printf("%x", err);
  if (save_errno)
    response.Printf(",%x", system_errno_to_gdb(save_errno));
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_pRead(
    StringExtractorGDBRemote &packet) {
  StreamGDBRemote response;
  packet.SetFilePos(::strlen("vFile:pread:"));
  int fd = packet.GetS32(-1, 16);
  if (packet.GetChar() == ',') {
    size_t count = packet.GetHexMaxU64(false, SIZE_MAX);
    if (packet.GetChar() == ',') {
      off_t offset = packet.GetHexMaxU32(false, UINT32_MAX);
      if (count == SIZE_MAX) {
        response.Printf("F-1:%x", EINVAL);
        return SendPacketNoLock(response.GetString());
      }

      std::string buffer(count, 0);
      NativeFile file(fd, File::eOpenOptionReadOnly, false);
      Status error = file.Read(static_cast<void *>(&buffer[0]), count, offset);
      const int save_errno = error.GetError();
      response.PutChar('F');
      if (error.Success()) {
        response.Printf("%zx", count);
        response.PutChar(';');
        response.PutEscapedBytes(&buffer[0], count);
      } else {
        response.PutCString("-1");
        if (save_errno)
          response.Printf(",%x", system_errno_to_gdb(save_errno));
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(21);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_pWrite(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:pwrite:"));

  StreamGDBRemote response;
  response.PutChar('F');

  int fd = packet.GetS32(-1, 16);
  if (packet.GetChar() == ',') {
    off_t offset = packet.GetHexMaxU32(false, UINT32_MAX);
    if (packet.GetChar() == ',') {
      std::string buffer;
      if (packet.GetEscapedBinaryData(buffer)) {
        NativeFile file(fd, File::eOpenOptionWriteOnly, false);
        size_t count = buffer.size();
        Status error =
            file.Write(static_cast<const void *>(&buffer[0]), count, offset);
        const int save_errno = error.GetError();
        if (error.Success())
          response.Printf("%zx", count);
        else {
          response.PutCString("-1");
          if (save_errno)
            response.Printf(",%x", system_errno_to_gdb(save_errno));
        }
      } else {
        response.Printf("-1,%x", EINVAL);
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(27);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Size(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:size:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    uint64_t Size;
    if (llvm::sys::fs::file_size(path, Size))
      return SendErrorResponse(5);
    StreamString response;
    response.PutChar('F');
    response.PutHex64(Size);
    if (Size == UINT64_MAX) {
      response.PutChar(',');
      response.PutHex64(Size); // TODO: replace with Host::GetSyswideErrorCode()
    }
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(22);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Mode(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:mode:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    FileSpec file_spec(path);
    FileSystem::Instance().Resolve(file_spec);
    std::error_code ec;
    const uint32_t mode = FileSystem::Instance().GetPermissions(file_spec, ec);
    StreamString response;
    if (mode != llvm::sys::fs::perms_not_known)
      response.Printf("F%x", mode);
    else
      response.Printf("F-1,%x", (int)Status(ec).GetError());
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(23);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Exists(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:exists:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    bool retcode = llvm::sys::fs::exists(path);
    StreamString response;
    response.PutChar('F');
    response.PutChar(',');
    if (retcode)
      response.PutChar('1');
    else
      response.PutChar('0');
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(24);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_symlink(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:symlink:"));
  std::string dst, src;
  packet.GetHexByteStringTerminatedBy(dst, ',');
  packet.GetChar(); // Skip ',' char
  packet.GetHexByteString(src);

  FileSpec src_spec(src);
  FileSystem::Instance().Resolve(src_spec);
  Status error = FileSystem::Instance().Symlink(src_spec, FileSpec(dst));

  StreamString response;
  response.Printf("F%x,%x", error.GetError(), error.GetError());
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_unlink(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:unlink:"));
  std::string path;
  packet.GetHexByteString(path);
  Status error(llvm::sys::fs::remove(path));
  StreamString response;
  response.Printf("F%x,%x", error.GetError(), error.GetError());
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qPlatform_shell(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qPlatform_shell:"));
  std::string path;
  std::string working_dir;
  packet.GetHexByteStringTerminatedBy(path, ',');
  if (!path.empty()) {
    if (packet.GetChar() == ',') {
      // FIXME: add timeout to qPlatform_shell packet
      // uint32_t timeout = packet.GetHexMaxU32(false, 32);
      if (packet.GetChar() == ',')
        packet.GetHexByteString(working_dir);
      int status, signo;
      std::string output;
      FileSpec working_spec(working_dir);
      FileSystem::Instance().Resolve(working_spec);
      Status err =
          Host::RunShellCommand(path.c_str(), working_spec, &status, &signo,
                                &output, std::chrono::seconds(10));
      StreamGDBRemote response;
      if (err.Fail()) {
        response.PutCString("F,");
        response.PutHex32(UINT32_MAX);
      } else {
        response.PutCString("F,");
        response.PutHex32(status);
        response.PutChar(',');
        response.PutHex32(signo);
        response.PutChar(',');
        response.PutEscapedBytes(output.c_str(), output.size());
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(24);
}

template <typename T, typename U>
static void fill_clamp(T &dest, U src, typename T::value_type fallback) {
  static_assert(std::is_unsigned<typename T::value_type>::value,
                "Destination type must be unsigned.");
  using UU = std::make_unsigned_t<U>;
  constexpr auto T_max = std::numeric_limits<typename T::value_type>::max();
  dest = src >= 0 && static_cast<UU>(src) <= T_max ? src : fallback;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_FStat(
    StringExtractorGDBRemote &packet) {
  StreamGDBRemote response;
  packet.SetFilePos(::strlen("vFile:fstat:"));
  int fd = packet.GetS32(-1, 16);

  struct stat file_stats;
  if (::fstat(fd, &file_stats) == -1) {
    const int save_errno = errno;
    response.Printf("F-1,%x", system_errno_to_gdb(save_errno));
    return SendPacketNoLock(response.GetString());
  }

  GDBRemoteFStatData data;
  fill_clamp(data.gdb_st_dev, file_stats.st_dev, 0);
  fill_clamp(data.gdb_st_ino, file_stats.st_ino, 0);
  data.gdb_st_mode = file_stats.st_mode;
  fill_clamp(data.gdb_st_nlink, file_stats.st_nlink, UINT32_MAX);
  fill_clamp(data.gdb_st_uid, file_stats.st_uid, 0);
  fill_clamp(data.gdb_st_gid, file_stats.st_gid, 0);
  fill_clamp(data.gdb_st_rdev, file_stats.st_rdev, 0);
  data.gdb_st_size = file_stats.st_size;
#if !defined(_WIN32)
  data.gdb_st_blksize = file_stats.st_blksize;
  data.gdb_st_blocks = file_stats.st_blocks;
#else
  data.gdb_st_blksize = 0;
  data.gdb_st_blocks = 0;
#endif
  fill_clamp(data.gdb_st_atime, file_stats.st_atime, 0);
  fill_clamp(data.gdb_st_mtime, file_stats.st_mtime, 0);
  fill_clamp(data.gdb_st_ctime, file_stats.st_ctime, 0);

  response.Printf("F%zx;", sizeof(data));
  response.PutEscapedBytes(&data, sizeof(data));
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Stat(
    StringExtractorGDBRemote &packet) {
  return SendUnimplementedResponse(
      "GDBRemoteCommunicationServerCommon::Handle_vFile_Stat() unimplemented");
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_MD5(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:MD5:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    StreamGDBRemote response;
    auto Result = llvm::sys::fs::md5_contents(path);
    if (!Result) {
      response.PutCString("F,");
      response.PutCString("x");
    } else {
      response.PutCString("F,");
      response.PutHex64(Result->low());
      response.PutHex64(Result->high());
    }
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(25);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qPlatform_mkdir(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qPlatform_mkdir:"));
  mode_t mode = packet.GetHexMaxU32(false, UINT32_MAX);
  if (packet.GetChar() == ',') {
    std::string path;
    packet.GetHexByteString(path);
    Status error(llvm::sys::fs::create_directory(path, mode));

    StreamGDBRemote response;
    response.Printf("F%x", error.GetError());

    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(20);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qPlatform_chmod(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qPlatform_chmod:"));

  auto perms =
      static_cast<llvm::sys::fs::perms>(packet.GetHexMaxU32(false, UINT32_MAX));
  if (packet.GetChar() == ',') {
    std::string path;
    packet.GetHexByteString(path);
    Status error(llvm::sys::fs::setPermissions(path, perms));

    StreamGDBRemote response;
    response.Printf("F%x", error.GetError());

    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(19);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qSupported(
    StringExtractorGDBRemote &packet) {
  // Parse client-indicated features.
  llvm::SmallVector<llvm::StringRef, 4> client_features;
  packet.GetStringRef().split(client_features, ';');
  return SendPacketNoLock(llvm::join(HandleFeatures(client_features), ";"));
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetDetachOnError(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetDetachOnError:"));
  if (packet.GetU32(0))
    m_process_launch_info.GetFlags().Set(eLaunchFlagDetachOnError);
  else
    m_process_launch_info.GetFlags().Clear(eLaunchFlagDetachOnError);
  return SendOKResponse();
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QStartNoAckMode(
    StringExtractorGDBRemote &packet) {
  // Send response first before changing m_send_acks to we ack this packet
  PacketResult packet_result = SendOKResponse();
  m_send_acks = false;
  return packet_result;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetSTDIN(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetSTDIN:"));
  FileAction file_action;
  std::string path;
  packet.GetHexByteString(path);
  const bool read = true;
  const bool write = false;
  if (file_action.Open(STDIN_FILENO, FileSpec(path), read, write)) {
    m_process_launch_info.AppendFileAction(file_action);
    return SendOKResponse();
  }
  return SendErrorResponse(15);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetSTDOUT(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetSTDOUT:"));
  FileAction file_action;
  std::string path;
  packet.GetHexByteString(path);
  const bool read = false;
  const bool write = true;
  if (file_action.Open(STDOUT_FILENO, FileSpec(path), read, write)) {
    m_process_launch_info.AppendFileAction(file_action);
    return SendOKResponse();
  }
  return SendErrorResponse(16);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetSTDERR(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetSTDERR:"));
  FileAction file_action;
  std::string path;
  packet.GetHexByteString(path);
  const bool read = false;
  const bool write = true;
  if (file_action.Open(STDERR_FILENO, FileSpec(path), read, write)) {
    m_process_launch_info.AppendFileAction(file_action);
    return SendOKResponse();
  }
  return SendErrorResponse(17);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qLaunchSuccess(
    StringExtractorGDBRemote &packet) {
  if (m_process_launch_error.Success())
    return SendOKResponse();
  StreamString response;
  response.PutChar('E');
  response.PutCString(m_process_launch_error.AsCString("<unknown error>"));
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QEnvironment(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QEnvironment:"));
  const uint32_t bytes_left = packet.GetBytesLeft();
  if (bytes_left > 0) {
    m_process_launch_info.GetEnvironment().insert(packet.Peek());
    return SendOKResponse();
  }
  return SendErrorResponse(12);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QEnvironmentHexEncoded(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QEnvironmentHexEncoded:"));
  const uint32_t bytes_left = packet.GetBytesLeft();
  if (bytes_left > 0) {
    std::string str;
    packet.GetHexByteString(str);
    m_process_launch_info.GetEnvironment().insert(str);
    return SendOKResponse();
  }
  return SendErrorResponse(12);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QLaunchArch(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QLaunchArch:"));
  const uint32_t bytes_left = packet.GetBytesLeft();
  if (bytes_left > 0) {
    const char *arch_triple = packet.Peek();
    m_process_launch_info.SetArchitecture(
        HostInfo::GetAugmentedArchSpec(arch_triple));
    return SendOKResponse();
  }
  return SendErrorResponse(13);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_A(StringExtractorGDBRemote &packet) {
  // The 'A' packet is the most over designed packet ever here with redundant
  // argument indexes, redundant argument lengths and needed hex encoded
  // argument string values. Really all that is needed is a comma separated hex
  // encoded argument value list, but we will stay true to the documented
  // version of the 'A' packet here...

  Log *log = GetLog(LLDBLog::Process);
  int actual_arg_index = 0;

  packet.SetFilePos(1); // Skip the 'A'
  bool success = true;
  while (success && packet.GetBytesLeft() > 0) {
    // Decode the decimal argument string length. This length is the number of
    // hex nibbles in the argument string value.
    const uint32_t arg_len = packet.GetU32(UINT32_MAX);
    if (arg_len == UINT32_MAX)
      success = false;
    else {
      // Make sure the argument hex string length is followed by a comma
      if (packet.GetChar() != ',')
        success = false;
      else {
        // Decode the argument index. We ignore this really because who would
        // really send down the arguments in a random order???
        const uint32_t arg_idx = packet.GetU32(UINT32_MAX);
        if (arg_idx == UINT32_MAX)
          success = false;
        else {
          // Make sure the argument index is followed by a comma
          if (packet.GetChar() != ',')
            success = false;
          else {
            // Decode the argument string value from hex bytes back into a UTF8
            // string and make sure the length matches the one supplied in the
            // packet
            std::string arg;
            if (packet.GetHexByteStringFixedLength(arg, arg_len) !=
                (arg_len / 2))
              success = false;
            else {
              // If there are any bytes left
              if (packet.GetBytesLeft()) {
                if (packet.GetChar() != ',')
                  success = false;
              }

              if (success) {
                if (arg_idx == 0)
                  m_process_launch_info.GetExecutableFile().SetFile(
                      arg, FileSpec::Style::native);
                m_process_launch_info.GetArguments().AppendArgument(arg);
                LLDB_LOGF(log, "LLGSPacketHandler::%s added arg %d: \"%s\"",
                          __FUNCTION__, actual_arg_index, arg.c_str());
                ++actual_arg_index;
              }
            }
          }
        }
      }
    }
  }

  if (success) {
    m_process_launch_error = LaunchProcess();
    if (m_process_launch_error.Success())
      return SendOKResponse();
    LLDB_LOG(log, "failed to launch exe: {0}", m_process_launch_error);
  }
  return SendErrorResponse(8);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qEcho(
    StringExtractorGDBRemote &packet) {
  // Just echo back the exact same packet for qEcho...
  return SendPacketNoLock(packet.GetStringRef());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qModuleInfo(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qModuleInfo:"));

  std::string module_path;
  packet.GetHexByteStringTerminatedBy(module_path, ';');
  if (module_path.empty())
    return SendErrorResponse(1);

  if (packet.GetChar() != ';')
    return SendErrorResponse(2);

  std::string triple;
  packet.GetHexByteString(triple);

  ModuleSpec matched_module_spec = GetModuleInfo(module_path, triple);
  if (!matched_module_spec.GetFileSpec())
    return SendErrorResponse(3);

  const auto file_offset = matched_module_spec.GetObjectOffset();
  const auto file_size = matched_module_spec.GetObjectSize();
  const auto uuid_str = matched_module_spec.GetUUID().GetAsString("");

  StreamGDBRemote response;

  if (uuid_str.empty()) {
    auto Result = llvm::sys::fs::md5_contents(
        matched_module_spec.GetFileSpec().GetPath());
    if (!Result)
      return SendErrorResponse(5);
    response.PutCString("md5:");
    response.PutStringAsRawHex8(Result->digest());
  } else {
    response.PutCString("uuid:");
    response.PutStringAsRawHex8(uuid_str);
  }
  response.PutChar(';');

  const auto &module_arch = matched_module_spec.GetArchitecture();
  response.PutCString("triple:");
  response.PutStringAsRawHex8(module_arch.GetTriple().getTriple());
  response.PutChar(';');

  response.PutCString("file_path:");
  response.PutStringAsRawHex8(
      matched_module_spec.GetFileSpec().GetPath().c_str());
  response.PutChar(';');
  response.PutCString("file_offset:");
  response.PutHex64(file_offset);
  response.PutChar(';');
  response.PutCString("file_size:");
  response.PutHex64(file_size);
  response.PutChar(';');

  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_jModulesInfo(
    StringExtractorGDBRemote &packet) {
  namespace json = llvm::json;

  packet.SetFilePos(::strlen("jModulesInfo:"));

  StructuredData::ObjectSP object_sp = StructuredData::ParseJSON(packet.Peek());
  if (!object_sp)
    return SendErrorResponse(1);

  StructuredData::Array *packet_array = object_sp->GetAsArray();
  if (!packet_array)
    return SendErrorResponse(2);

  json::Array response_array;
  for (size_t i = 0; i < packet_array->GetSize(); ++i) {
    StructuredData::Dictionary *query =
        packet_array->GetItemAtIndex(i)->GetAsDictionary();
    if (!query)
      continue;
    llvm::StringRef file, triple;
    if (!query->GetValueForKeyAsString("file", file) ||
        !query->GetValueForKeyAsString("triple", triple))
      continue;

    ModuleSpec matched_module_spec = GetModuleInfo(file, triple);
    if (!matched_module_spec.GetFileSpec())
      continue;

    const auto file_offset = matched_module_spec.GetObjectOffset();
    const auto file_size = matched_module_spec.GetObjectSize();
    const auto uuid_str = matched_module_spec.GetUUID().GetAsString("");
    if (uuid_str.empty())
      continue;
    const auto triple_str =
        matched_module_spec.GetArchitecture().GetTriple().getTriple();
    const auto file_path = matched_module_spec.GetFileSpec().GetPath();

    json::Object response{{"uuid", uuid_str},
                          {"triple", triple_str},
                          {"file_path", file_path},
                          {"file_offset", static_cast<int64_t>(file_offset)},
                          {"file_size", static_cast<int64_t>(file_size)}};
    response_array.push_back(std::move(response));
  }

  StreamString response;
  response.AsRawOstream() << std::move(response_array);
  StreamGDBRemote escaped_response;
  escaped_response.PutEscapedBytes(response.GetString().data(),
                                   response.GetSize());
  return SendPacketNoLock(escaped_response.GetString());
}

void GDBRemoteCommunicationServerCommon::CreateProcessInfoResponse(
    const ProcessInstanceInfo &proc_info, StreamString &response) {
  response.Printf(
      "pid:%" PRIu64 ";ppid:%" PRIu64 ";uid:%i;gid:%i;euid:%i;egid:%i;",
      proc_info.GetProcessID(), proc_info.GetParentProcessID(),
      proc_info.GetUserID(), proc_info.GetGroupID(),
      proc_info.GetEffectiveUserID(), proc_info.GetEffectiveGroupID());
  response.PutCString("name:");
  response.PutStringAsRawHex8(proc_info.GetExecutableFile().GetPath().c_str());

  response.PutChar(';');
  response.PutCString("args:");
  response.PutStringAsRawHex8(proc_info.GetArg0());
  for (auto &arg : proc_info.GetArguments()) {
    response.PutChar('-');
    response.PutStringAsRawHex8(arg.ref());
  }

  response.PutChar(';');
  const ArchSpec &proc_arch = proc_info.GetArchitecture();
  if (proc_arch.IsValid()) {
    const llvm::Triple &proc_triple = proc_arch.GetTriple();
    response.PutCString("triple:");
    response.PutStringAsRawHex8(proc_triple.getTriple());
    response.PutChar(';');
  }
}

void GDBRemoteCommunicationServerCommon::
    CreateProcessInfoResponse_DebugServerStyle(
        const ProcessInstanceInfo &proc_info, StreamString &response) {
  response.Printf("pid:%" PRIx64 ";parent-pid:%" PRIx64
                  ";real-uid:%x;real-gid:%x;effective-uid:%x;effective-gid:%x;",
                  proc_info.GetProcessID(), proc_info.GetParentProcessID(),
                  proc_info.GetUserID(), proc_info.GetGroupID(),
                  proc_info.GetEffectiveUserID(),
                  proc_info.GetEffectiveGroupID());

  const ArchSpec &proc_arch = proc_info.GetArchitecture();
  if (proc_arch.IsValid()) {
    const llvm::Triple &proc_triple = proc_arch.GetTriple();
#if defined(__APPLE__)
    // We'll send cputype/cpusubtype.
    const uint32_t cpu_type = proc_arch.GetMachOCPUType();
    if (cpu_type != 0)
      response.Printf("cputype:%" PRIx32 ";", cpu_type);

    const uint32_t cpu_subtype = proc_arch.GetMachOCPUSubType();
    if (cpu_subtype != 0)
      response.Printf("cpusubtype:%" PRIx32 ";", cpu_subtype);

    const std::string vendor = proc_triple.getVendorName().str();
    if (!vendor.empty())
      response.Printf("vendor:%s;", vendor.c_str());
#else
    // We'll send the triple.
    response.PutCString("triple:");
    response.PutStringAsRawHex8(proc_triple.getTriple());
    response.PutChar(';');
#endif
    std::string ostype = std::string(proc_triple.getOSName());
    // Adjust so ostype reports ios for Apple/ARM and Apple/ARM64.
    if (proc_triple.getVendor() == llvm::Triple::Apple) {
      switch (proc_triple.getArch()) {
      case llvm::Triple::arm:
      case llvm::Triple::thumb:
      case llvm::Triple::aarch64:
      case llvm::Triple::aarch64_32:
        ostype = "ios";
        break;
      default:
        // No change.
        break;
      }
    }
    response.Printf("ostype:%s;", ostype.c_str());

    switch (proc_arch.GetByteOrder()) {
    case lldb::eByteOrderLittle:
      response.PutCString("endian:little;");
      break;
    case lldb::eByteOrderBig:
      response.PutCString("endian:big;");
      break;
    case lldb::eByteOrderPDP:
      response.PutCString("endian:pdp;");
      break;
    default:
      // Nothing.
      break;
    }
    // In case of MIPS64, pointer size is depend on ELF ABI For N32 the pointer
    // size is 4 and for N64 it is 8
    std::string abi = proc_arch.GetTargetABI();
    if (!abi.empty())
      response.Printf("elf_abi:%s;", abi.c_str());
    response.Printf("ptrsize:%d;", proc_arch.GetAddressByteSize());
  }
}

FileSpec GDBRemoteCommunicationServerCommon::FindModuleFile(
    const std::string &module_path, const ArchSpec &arch) {
#ifdef __ANDROID__
  return HostInfoAndroid::ResolveLibraryPath(module_path, arch);
#else
  FileSpec file_spec(module_path);
  FileSystem::Instance().Resolve(file_spec);
  return file_spec;
#endif
}

ModuleSpec
GDBRemoteCommunicationServerCommon::GetModuleInfo(llvm::StringRef module_path,
                                                  llvm::StringRef triple) {
  ArchSpec arch(triple);

  FileSpec req_module_path_spec(module_path);
  FileSystem::Instance().Resolve(req_module_path_spec);

  const FileSpec module_path_spec =
      FindModuleFile(req_module_path_spec.GetPath(), arch);

  lldb::offset_t file_offset = 0;
  lldb::offset_t file_size = 0;
#ifdef __ANDROID__
  // In Android API level 23 and above, dynamic loader is able to load .so file
  // directly from zip file. In that case, module_path will be
  // "zip_path!/so_path". Resolve the zip file path, .so file offset and size.
  ZipFileResolver::FileKind file_kind = ZipFileResolver::eFileKindInvalid;
  std::string file_path;
  if (!ZipFileResolver::ResolveSharedLibraryPath(
          module_path_spec, file_kind, file_path, file_offset, file_size)) {
    return ModuleSpec();
  }
  lldbassert(file_kind != ZipFileResolver::eFileKindInvalid);
  // For zip .so file, this file_path will contain only the actual zip file
  // path for the object file processing. Otherwise it is the same as
  // module_path.
  const FileSpec actual_module_path_spec(file_path);
#else
  // It is just module_path_spec reference for other platforms.
  const FileSpec &actual_module_path_spec = module_path_spec;
#endif

  const ModuleSpec module_spec(actual_module_path_spec, arch);

  ModuleSpecList module_specs;
  if (!ObjectFile::GetModuleSpecifications(actual_module_path_spec, file_offset,
                                           file_size, module_specs))
    return ModuleSpec();

  ModuleSpec matched_module_spec;
  if (!module_specs.FindMatchingModuleSpec(module_spec, matched_module_spec))
    return ModuleSpec();

#ifdef __ANDROID__
  if (file_kind == ZipFileResolver::eFileKindZip) {
    // For zip .so file, matched_module_spec contains only the actual zip file
    // path for the object file processing. Overwrite the matched_module_spec
    // file spec with the original module_path_spec to pass "zip_path!/so_path"
    // through to PlatformAndroid::DownloadModuleSlice.
    *matched_module_spec.GetFileSpecPtr() = module_path_spec;
  }
#endif

  return matched_module_spec;
}

std::vector<std::string> GDBRemoteCommunicationServerCommon::HandleFeatures(
    const llvm::ArrayRef<llvm::StringRef> client_features) {
  // 128KBytes is a reasonable max packet size--debugger can always use less.
  constexpr uint32_t max_packet_size = 128 * 1024;

  // Features common to platform server and llgs.
  return {
      llvm::formatv("PacketSize={0}", max_packet_size),
      "QStartNoAckMode+",
      "qEcho+",
      "native-signals+",
  };
}
