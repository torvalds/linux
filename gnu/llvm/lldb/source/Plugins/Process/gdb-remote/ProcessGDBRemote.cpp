//===-- ProcessGDBRemote.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"

#include <cerrno>
#include <cstdlib>
#if LLDB_ENABLE_POSIX
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <ctime>
#include <sys/types.h>

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Breakpoint/WatchpointAlgorithms.h"
#include "lldb/Breakpoint/WatchpointResource.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/XML.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Interpreter/OptionGroupUInt64.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/RegisterFlags.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include <algorithm>
#include <csignal>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

#include "GDBRemoteRegisterContext.h"
#include "GDBRemoteRegisterFallback.h"
#include "Plugins/Process/Utility/GDBRemoteSignals.h"
#include "Plugins/Process/Utility/InferiorCallPOSIX.h"
#include "Plugins/Process/Utility/StopInfoMachException.h"
#include "ProcessGDBRemote.h"
#include "ProcessGDBRemoteLog.h"
#include "ThreadGDBRemote.h"
#include "lldb/Host/Host.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUGSERVER_BASENAME "debugserver"
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

LLDB_PLUGIN_DEFINE(ProcessGDBRemote)

namespace lldb {
// Provide a function that can easily dump the packet history if we know a
// ProcessGDBRemote * value (which we can get from logs or from debugging). We
// need the function in the lldb namespace so it makes it into the final
// executable since the LLDB shared library only exports stuff in the lldb
// namespace. This allows you to attach with a debugger and call this function
// and get the packet history dumped to a file.
void DumpProcessGDBRemotePacketHistory(void *p, const char *path) {
  auto file = FileSystem::Instance().Open(
      FileSpec(path), File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate);
  if (!file) {
    llvm::consumeError(file.takeError());
    return;
  }
  StreamFile stream(std::move(file.get()));
  ((Process *)p)->DumpPluginHistory(stream);
}
} // namespace lldb

namespace {

#define LLDB_PROPERTIES_processgdbremote
#include "ProcessGDBRemoteProperties.inc"

enum {
#define LLDB_PROPERTIES_processgdbremote
#include "ProcessGDBRemotePropertiesEnum.inc"
};

class PluginProperties : public Properties {
public:
  static llvm::StringRef GetSettingName() {
    return ProcessGDBRemote::GetPluginNameStatic();
  }

  PluginProperties() : Properties() {
    m_collection_sp = std::make_shared<OptionValueProperties>(GetSettingName());
    m_collection_sp->Initialize(g_processgdbremote_properties);
  }

  ~PluginProperties() override = default;

  uint64_t GetPacketTimeout() {
    const uint32_t idx = ePropertyPacketTimeout;
    return GetPropertyAtIndexAs<uint64_t>(
        idx, g_processgdbremote_properties[idx].default_uint_value);
  }

  bool SetPacketTimeout(uint64_t timeout) {
    const uint32_t idx = ePropertyPacketTimeout;
    return SetPropertyAtIndex(idx, timeout);
  }

  FileSpec GetTargetDefinitionFile() const {
    const uint32_t idx = ePropertyTargetDefinitionFile;
    return GetPropertyAtIndexAs<FileSpec>(idx, {});
  }

  bool GetUseSVR4() const {
    const uint32_t idx = ePropertyUseSVR4;
    return GetPropertyAtIndexAs<bool>(
        idx, g_processgdbremote_properties[idx].default_uint_value != 0);
  }

  bool GetUseGPacketForReading() const {
    const uint32_t idx = ePropertyUseGPacketForReading;
    return GetPropertyAtIndexAs<bool>(idx, true);
  }
};

} // namespace

static PluginProperties &GetGlobalPluginProperties() {
  static PluginProperties g_settings;
  return g_settings;
}

// TODO Randomly assigning a port is unsafe.  We should get an unused
// ephemeral port from the kernel and make sure we reserve it before passing it
// to debugserver.

#if defined(__APPLE__)
#define LOW_PORT (IPPORT_RESERVED)
#define HIGH_PORT (IPPORT_HIFIRSTAUTO)
#else
#define LOW_PORT (1024u)
#define HIGH_PORT (49151u)
#endif

llvm::StringRef ProcessGDBRemote::GetPluginDescriptionStatic() {
  return "GDB Remote protocol based debugging plug-in.";
}

void ProcessGDBRemote::Terminate() {
  PluginManager::UnregisterPlugin(ProcessGDBRemote::CreateInstance);
}

lldb::ProcessSP ProcessGDBRemote::CreateInstance(
    lldb::TargetSP target_sp, ListenerSP listener_sp,
    const FileSpec *crash_file_path, bool can_connect) {
  lldb::ProcessSP process_sp;
  if (crash_file_path == nullptr)
    process_sp = std::shared_ptr<ProcessGDBRemote>(
        new ProcessGDBRemote(target_sp, listener_sp));
  return process_sp;
}

void ProcessGDBRemote::DumpPluginHistory(Stream &s) {
  GDBRemoteCommunicationClient &gdb_comm(GetGDBRemote());
  gdb_comm.DumpHistory(s);
}

std::chrono::seconds ProcessGDBRemote::GetPacketTimeout() {
  return std::chrono::seconds(GetGlobalPluginProperties().GetPacketTimeout());
}

ArchSpec ProcessGDBRemote::GetSystemArchitecture() {
  return m_gdb_comm.GetHostArchitecture();
}

bool ProcessGDBRemote::CanDebug(lldb::TargetSP target_sp,
                                bool plugin_specified_by_name) {
  if (plugin_specified_by_name)
    return true;

  // For now we are just making sure the file exists for a given module
  Module *exe_module = target_sp->GetExecutableModulePointer();
  if (exe_module) {
    ObjectFile *exe_objfile = exe_module->GetObjectFile();
    // We can't debug core files...
    switch (exe_objfile->GetType()) {
    case ObjectFile::eTypeInvalid:
    case ObjectFile::eTypeCoreFile:
    case ObjectFile::eTypeDebugInfo:
    case ObjectFile::eTypeObjectFile:
    case ObjectFile::eTypeSharedLibrary:
    case ObjectFile::eTypeStubLibrary:
    case ObjectFile::eTypeJIT:
      return false;
    case ObjectFile::eTypeExecutable:
    case ObjectFile::eTypeDynamicLinker:
    case ObjectFile::eTypeUnknown:
      break;
    }
    return FileSystem::Instance().Exists(exe_module->GetFileSpec());
  }
  // However, if there is no executable module, we return true since we might
  // be preparing to attach.
  return true;
}

// ProcessGDBRemote constructor
ProcessGDBRemote::ProcessGDBRemote(lldb::TargetSP target_sp,
                                   ListenerSP listener_sp)
    : Process(target_sp, listener_sp),
      m_debugserver_pid(LLDB_INVALID_PROCESS_ID), m_register_info_sp(nullptr),
      m_async_broadcaster(nullptr, "lldb.process.gdb-remote.async-broadcaster"),
      m_async_listener_sp(
          Listener::MakeListener("lldb.process.gdb-remote.async-listener")),
      m_async_thread_state_mutex(), m_thread_ids(), m_thread_pcs(),
      m_jstopinfo_sp(), m_jthreadsinfo_sp(), m_continue_c_tids(),
      m_continue_C_tids(), m_continue_s_tids(), m_continue_S_tids(),
      m_max_memory_size(0), m_remote_stub_max_memory_size(0),
      m_addr_to_mmap_size(), m_thread_create_bp_sp(),
      m_waiting_for_attach(false), m_command_sp(), m_breakpoint_pc_offset(0),
      m_initial_tid(LLDB_INVALID_THREAD_ID), m_allow_flash_writes(false),
      m_erased_flash_ranges(), m_vfork_in_progress_count(0) {
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncThreadShouldExit,
                                   "async thread should exit");
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncContinue,
                                   "async thread continue");
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncThreadDidExit,
                                   "async thread did exit");

  Log *log = GetLog(GDBRLog::Async);

  const uint32_t async_event_mask =
      eBroadcastBitAsyncContinue | eBroadcastBitAsyncThreadShouldExit;

  if (m_async_listener_sp->StartListeningForEvents(
          &m_async_broadcaster, async_event_mask) != async_event_mask) {
    LLDB_LOGF(log,
              "ProcessGDBRemote::%s failed to listen for "
              "m_async_broadcaster events",
              __FUNCTION__);
  }

  const uint64_t timeout_seconds =
      GetGlobalPluginProperties().GetPacketTimeout();
  if (timeout_seconds > 0)
    m_gdb_comm.SetPacketTimeout(std::chrono::seconds(timeout_seconds));

  m_use_g_packet_for_reading =
      GetGlobalPluginProperties().GetUseGPacketForReading();
}

// Destructor
ProcessGDBRemote::~ProcessGDBRemote() {
  //  m_mach_process.UnregisterNotificationCallbacks (this);
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize(true /* destructing */);

  // The general Finalize is going to try to destroy the process and that
  // SHOULD shut down the async thread.  However, if we don't kill it it will
  // get stranded and its connection will go away so when it wakes up it will
  // crash.  So kill it for sure here.
  StopAsyncThread();
  KillDebugserverProcess();
}

bool ProcessGDBRemote::ParsePythonTargetDefinition(
    const FileSpec &target_definition_fspec) {
  ScriptInterpreter *interpreter =
      GetTarget().GetDebugger().GetScriptInterpreter();
  Status error;
  StructuredData::ObjectSP module_object_sp(
      interpreter->LoadPluginModule(target_definition_fspec, error));
  if (module_object_sp) {
    StructuredData::DictionarySP target_definition_sp(
        interpreter->GetDynamicSettings(module_object_sp, &GetTarget(),
                                        "gdb-server-target-definition", error));

    if (target_definition_sp) {
      StructuredData::ObjectSP target_object(
          target_definition_sp->GetValueForKey("host-info"));
      if (target_object) {
        if (auto host_info_dict = target_object->GetAsDictionary()) {
          StructuredData::ObjectSP triple_value =
              host_info_dict->GetValueForKey("triple");
          if (auto triple_string_value = triple_value->GetAsString()) {
            std::string triple_string =
                std::string(triple_string_value->GetValue());
            ArchSpec host_arch(triple_string.c_str());
            if (!host_arch.IsCompatibleMatch(GetTarget().GetArchitecture())) {
              GetTarget().SetArchitecture(host_arch);
            }
          }
        }
      }
      m_breakpoint_pc_offset = 0;
      StructuredData::ObjectSP breakpoint_pc_offset_value =
          target_definition_sp->GetValueForKey("breakpoint-pc-offset");
      if (breakpoint_pc_offset_value) {
        if (auto breakpoint_pc_int_value =
                breakpoint_pc_offset_value->GetAsSignedInteger())
          m_breakpoint_pc_offset = breakpoint_pc_int_value->GetValue();
      }

      if (m_register_info_sp->SetRegisterInfo(
              *target_definition_sp, GetTarget().GetArchitecture()) > 0) {
        return true;
      }
    }
  }
  return false;
}

static size_t SplitCommaSeparatedRegisterNumberString(
    const llvm::StringRef &comma_separated_register_numbers,
    std::vector<uint32_t> &regnums, int base) {
  regnums.clear();
  for (llvm::StringRef x : llvm::split(comma_separated_register_numbers, ',')) {
    uint32_t reg;
    if (llvm::to_integer(x, reg, base))
      regnums.push_back(reg);
  }
  return regnums.size();
}

void ProcessGDBRemote::BuildDynamicRegisterInfo(bool force) {
  if (!force && m_register_info_sp)
    return;

  m_register_info_sp = std::make_shared<GDBRemoteDynamicRegisterInfo>();

  // Check if qHostInfo specified a specific packet timeout for this
  // connection. If so then lets update our setting so the user knows what the
  // timeout is and can see it.
  const auto host_packet_timeout = m_gdb_comm.GetHostDefaultPacketTimeout();
  if (host_packet_timeout > std::chrono::seconds(0)) {
    GetGlobalPluginProperties().SetPacketTimeout(host_packet_timeout.count());
  }

  // Register info search order:
  //     1 - Use the target definition python file if one is specified.
  //     2 - If the target definition doesn't have any of the info from the
  //     target.xml (registers) then proceed to read the target.xml.
  //     3 - Fall back on the qRegisterInfo packets.
  //     4 - Use hardcoded defaults if available.

  FileSpec target_definition_fspec =
      GetGlobalPluginProperties().GetTargetDefinitionFile();
  if (!FileSystem::Instance().Exists(target_definition_fspec)) {
    // If the filename doesn't exist, it may be a ~ not having been expanded -
    // try to resolve it.
    FileSystem::Instance().Resolve(target_definition_fspec);
  }
  if (target_definition_fspec) {
    // See if we can get register definitions from a python file
    if (ParsePythonTargetDefinition(target_definition_fspec))
      return;

    Debugger::ReportError("target description file " +
                              target_definition_fspec.GetPath() +
                              " failed to parse",
                          GetTarget().GetDebugger().GetID());
  }

  const ArchSpec &target_arch = GetTarget().GetArchitecture();
  const ArchSpec &remote_host_arch = m_gdb_comm.GetHostArchitecture();
  const ArchSpec &remote_process_arch = m_gdb_comm.GetProcessArchitecture();

  // Use the process' architecture instead of the host arch, if available
  ArchSpec arch_to_use;
  if (remote_process_arch.IsValid())
    arch_to_use = remote_process_arch;
  else
    arch_to_use = remote_host_arch;

  if (!arch_to_use.IsValid())
    arch_to_use = target_arch;

  if (GetGDBServerRegisterInfo(arch_to_use))
    return;

  char packet[128];
  std::vector<DynamicRegisterInfo::Register> registers;
  uint32_t reg_num = 0;
  for (StringExtractorGDBRemote::ResponseType response_type =
           StringExtractorGDBRemote::eResponse;
       response_type == StringExtractorGDBRemote::eResponse; ++reg_num) {
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "qRegisterInfo%x", reg_num);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet, response) ==
        GDBRemoteCommunication::PacketResult::Success) {
      response_type = response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        llvm::StringRef name;
        llvm::StringRef value;
        DynamicRegisterInfo::Register reg_info;

        while (response.GetNameColonValue(name, value)) {
          if (name == "name") {
            reg_info.name.SetString(value);
          } else if (name == "alt-name") {
            reg_info.alt_name.SetString(value);
          } else if (name == "bitsize") {
            if (!value.getAsInteger(0, reg_info.byte_size))
              reg_info.byte_size /= CHAR_BIT;
          } else if (name == "offset") {
            value.getAsInteger(0, reg_info.byte_offset);
          } else if (name == "encoding") {
            const Encoding encoding = Args::StringToEncoding(value);
            if (encoding != eEncodingInvalid)
              reg_info.encoding = encoding;
          } else if (name == "format") {
            if (!OptionArgParser::ToFormat(value.str().c_str(), reg_info.format, nullptr)
                    .Success())
              reg_info.format =
                  llvm::StringSwitch<Format>(value)
                      .Case("binary", eFormatBinary)
                      .Case("decimal", eFormatDecimal)
                      .Case("hex", eFormatHex)
                      .Case("float", eFormatFloat)
                      .Case("vector-sint8", eFormatVectorOfSInt8)
                      .Case("vector-uint8", eFormatVectorOfUInt8)
                      .Case("vector-sint16", eFormatVectorOfSInt16)
                      .Case("vector-uint16", eFormatVectorOfUInt16)
                      .Case("vector-sint32", eFormatVectorOfSInt32)
                      .Case("vector-uint32", eFormatVectorOfUInt32)
                      .Case("vector-float32", eFormatVectorOfFloat32)
                      .Case("vector-uint64", eFormatVectorOfUInt64)
                      .Case("vector-uint128", eFormatVectorOfUInt128)
                      .Default(eFormatInvalid);
          } else if (name == "set") {
            reg_info.set_name.SetString(value);
          } else if (name == "gcc" || name == "ehframe") {
            value.getAsInteger(0, reg_info.regnum_ehframe);
          } else if (name == "dwarf") {
            value.getAsInteger(0, reg_info.regnum_dwarf);
          } else if (name == "generic") {
            reg_info.regnum_generic = Args::StringToGenericRegister(value);
          } else if (name == "container-regs") {
            SplitCommaSeparatedRegisterNumberString(value, reg_info.value_regs, 16);
          } else if (name == "invalidate-regs") {
            SplitCommaSeparatedRegisterNumberString(value, reg_info.invalidate_regs, 16);
          }
        }

        assert(reg_info.byte_size != 0);
        registers.push_back(reg_info);
      } else {
        break; // ensure exit before reg_num is incremented
      }
    } else {
      break;
    }
  }

  if (registers.empty())
    registers = GetFallbackRegisters(arch_to_use);

  AddRemoteRegisters(registers, arch_to_use);
}

Status ProcessGDBRemote::DoWillLaunch(lldb_private::Module *module) {
  return WillLaunchOrAttach();
}

Status ProcessGDBRemote::DoWillAttachToProcessWithID(lldb::pid_t pid) {
  return WillLaunchOrAttach();
}

Status ProcessGDBRemote::DoWillAttachToProcessWithName(const char *process_name,
                                                       bool wait_for_launch) {
  return WillLaunchOrAttach();
}

Status ProcessGDBRemote::DoConnectRemote(llvm::StringRef remote_url) {
  Log *log = GetLog(GDBRLog::Process);

  Status error(WillLaunchOrAttach());
  if (error.Fail())
    return error;

  error = ConnectToDebugserver(remote_url);
  if (error.Fail())
    return error;

  StartAsyncThread();

  lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID();
  if (pid == LLDB_INVALID_PROCESS_ID) {
    // We don't have a valid process ID, so note that we are connected and
    // could now request to launch or attach, or get remote process listings...
    SetPrivateState(eStateConnected);
  } else {
    // We have a valid process
    SetID(pid);
    GetThreadList();
    StringExtractorGDBRemote response;
    if (m_gdb_comm.GetStopReply(response)) {
      SetLastStopPacket(response);

      Target &target = GetTarget();
      if (!target.GetArchitecture().IsValid()) {
        if (m_gdb_comm.GetProcessArchitecture().IsValid()) {
          target.SetArchitecture(m_gdb_comm.GetProcessArchitecture());
        } else {
          if (m_gdb_comm.GetHostArchitecture().IsValid()) {
            target.SetArchitecture(m_gdb_comm.GetHostArchitecture());
          }
        }
      }

      const StateType state = SetThreadStopInfo(response);
      if (state != eStateInvalid) {
        SetPrivateState(state);
      } else
        error.SetErrorStringWithFormat(
            "Process %" PRIu64 " was reported after connecting to "
            "'%s', but state was not stopped: %s",
            pid, remote_url.str().c_str(), StateAsCString(state));
    } else
      error.SetErrorStringWithFormat("Process %" PRIu64
                                     " was reported after connecting to '%s', "
                                     "but no stop reply packet was received",
                                     pid, remote_url.str().c_str());
  }

  LLDB_LOGF(log,
            "ProcessGDBRemote::%s pid %" PRIu64
            ": normalizing target architecture initial triple: %s "
            "(GetTarget().GetArchitecture().IsValid() %s, "
            "m_gdb_comm.GetHostArchitecture().IsValid(): %s)",
            __FUNCTION__, GetID(),
            GetTarget().GetArchitecture().GetTriple().getTriple().c_str(),
            GetTarget().GetArchitecture().IsValid() ? "true" : "false",
            m_gdb_comm.GetHostArchitecture().IsValid() ? "true" : "false");

  if (error.Success() && !GetTarget().GetArchitecture().IsValid() &&
      m_gdb_comm.GetHostArchitecture().IsValid()) {
    // Prefer the *process'* architecture over that of the *host*, if
    // available.
    if (m_gdb_comm.GetProcessArchitecture().IsValid())
      GetTarget().SetArchitecture(m_gdb_comm.GetProcessArchitecture());
    else
      GetTarget().SetArchitecture(m_gdb_comm.GetHostArchitecture());
  }

  LLDB_LOGF(log,
            "ProcessGDBRemote::%s pid %" PRIu64
            ": normalized target architecture triple: %s",
            __FUNCTION__, GetID(),
            GetTarget().GetArchitecture().GetTriple().getTriple().c_str());

  return error;
}

Status ProcessGDBRemote::WillLaunchOrAttach() {
  Status error;
  m_stdio_communication.Clear();
  return error;
}

// Process Control
Status ProcessGDBRemote::DoLaunch(lldb_private::Module *exe_module,
                                  ProcessLaunchInfo &launch_info) {
  Log *log = GetLog(GDBRLog::Process);
  Status error;

  LLDB_LOGF(log, "ProcessGDBRemote::%s() entered", __FUNCTION__);

  uint32_t launch_flags = launch_info.GetFlags().Get();
  FileSpec stdin_file_spec{};
  FileSpec stdout_file_spec{};
  FileSpec stderr_file_spec{};
  FileSpec working_dir = launch_info.GetWorkingDirectory();

  const FileAction *file_action;
  file_action = launch_info.GetFileActionForFD(STDIN_FILENO);
  if (file_action) {
    if (file_action->GetAction() == FileAction::eFileActionOpen)
      stdin_file_spec = file_action->GetFileSpec();
  }
  file_action = launch_info.GetFileActionForFD(STDOUT_FILENO);
  if (file_action) {
    if (file_action->GetAction() == FileAction::eFileActionOpen)
      stdout_file_spec = file_action->GetFileSpec();
  }
  file_action = launch_info.GetFileActionForFD(STDERR_FILENO);
  if (file_action) {
    if (file_action->GetAction() == FileAction::eFileActionOpen)
      stderr_file_spec = file_action->GetFileSpec();
  }

  if (log) {
    if (stdin_file_spec || stdout_file_spec || stderr_file_spec)
      LLDB_LOGF(log,
                "ProcessGDBRemote::%s provided with STDIO paths via "
                "launch_info: stdin=%s, stdout=%s, stderr=%s",
                __FUNCTION__,
                stdin_file_spec ? stdin_file_spec.GetPath().c_str() : "<null>",
                stdout_file_spec ? stdout_file_spec.GetPath().c_str() : "<null>",
                stderr_file_spec ? stderr_file_spec.GetPath().c_str() : "<null>");
    else
      LLDB_LOGF(log,
                "ProcessGDBRemote::%s no STDIO paths given via launch_info",
                __FUNCTION__);
  }

  const bool disable_stdio = (launch_flags & eLaunchFlagDisableSTDIO) != 0;
  if (stdin_file_spec || disable_stdio) {
    // the inferior will be reading stdin from the specified file or stdio is
    // completely disabled
    m_stdin_forward = false;
  } else {
    m_stdin_forward = true;
  }

  //  ::LogSetBitMask (GDBR_LOG_DEFAULT);
  //  ::LogSetOptions (LLDB_LOG_OPTION_THREADSAFE |
  //  LLDB_LOG_OPTION_PREPEND_TIMESTAMP |
  //  LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD);
  //  ::LogSetLogFile ("/dev/stdout");

  error = EstablishConnectionIfNeeded(launch_info);
  if (error.Success()) {
    PseudoTerminal pty;
    const bool disable_stdio = (launch_flags & eLaunchFlagDisableSTDIO) != 0;

    PlatformSP platform_sp(GetTarget().GetPlatform());
    if (disable_stdio) {
      // set to /dev/null unless redirected to a file above
      if (!stdin_file_spec)
        stdin_file_spec.SetFile(FileSystem::DEV_NULL,
                                FileSpec::Style::native);
      if (!stdout_file_spec)
        stdout_file_spec.SetFile(FileSystem::DEV_NULL,
                                 FileSpec::Style::native);
      if (!stderr_file_spec)
        stderr_file_spec.SetFile(FileSystem::DEV_NULL,
                                 FileSpec::Style::native);
    } else if (platform_sp && platform_sp->IsHost()) {
      // If the debugserver is local and we aren't disabling STDIO, lets use
      // a pseudo terminal to instead of relying on the 'O' packets for stdio
      // since 'O' packets can really slow down debugging if the inferior
      // does a lot of output.
      if ((!stdin_file_spec || !stdout_file_spec || !stderr_file_spec) &&
          !errorToBool(pty.OpenFirstAvailablePrimary(O_RDWR | O_NOCTTY))) {
        FileSpec secondary_name(pty.GetSecondaryName());

        if (!stdin_file_spec)
          stdin_file_spec = secondary_name;

        if (!stdout_file_spec)
          stdout_file_spec = secondary_name;

        if (!stderr_file_spec)
          stderr_file_spec = secondary_name;
      }
      LLDB_LOGF(
          log,
          "ProcessGDBRemote::%s adjusted STDIO paths for local platform "
          "(IsHost() is true) using secondary: stdin=%s, stdout=%s, "
          "stderr=%s",
          __FUNCTION__,
          stdin_file_spec ? stdin_file_spec.GetPath().c_str() : "<null>",
          stdout_file_spec ? stdout_file_spec.GetPath().c_str() : "<null>",
          stderr_file_spec ? stderr_file_spec.GetPath().c_str() : "<null>");
    }

    LLDB_LOGF(log,
              "ProcessGDBRemote::%s final STDIO paths after all "
              "adjustments: stdin=%s, stdout=%s, stderr=%s",
              __FUNCTION__,
              stdin_file_spec ? stdin_file_spec.GetPath().c_str() : "<null>",
              stdout_file_spec ? stdout_file_spec.GetPath().c_str() : "<null>",
              stderr_file_spec ? stderr_file_spec.GetPath().c_str() : "<null>");

    if (stdin_file_spec)
      m_gdb_comm.SetSTDIN(stdin_file_spec);
    if (stdout_file_spec)
      m_gdb_comm.SetSTDOUT(stdout_file_spec);
    if (stderr_file_spec)
      m_gdb_comm.SetSTDERR(stderr_file_spec);

    m_gdb_comm.SetDisableASLR(launch_flags & eLaunchFlagDisableASLR);
    m_gdb_comm.SetDetachOnError(launch_flags & eLaunchFlagDetachOnError);

    m_gdb_comm.SendLaunchArchPacket(
        GetTarget().GetArchitecture().GetArchitectureName());

    const char *launch_event_data = launch_info.GetLaunchEventData();
    if (launch_event_data != nullptr && *launch_event_data != '\0')
      m_gdb_comm.SendLaunchEventDataPacket(launch_event_data);

    if (working_dir) {
      m_gdb_comm.SetWorkingDir(working_dir);
    }

    // Send the environment and the program + arguments after we connect
    m_gdb_comm.SendEnvironment(launch_info.GetEnvironment());

    {
      // Scope for the scoped timeout object
      GDBRemoteCommunication::ScopedTimeout timeout(m_gdb_comm,
                                                    std::chrono::seconds(10));

      // Since we can't send argv0 separate from the executable path, we need to
      // make sure to use the actual executable path found in the launch_info...
      Args args = launch_info.GetArguments();
      if (FileSpec exe_file = launch_info.GetExecutableFile())
        args.ReplaceArgumentAtIndex(0, exe_file.GetPath(false));
      if (llvm::Error err = m_gdb_comm.LaunchProcess(args)) {
        error.SetErrorStringWithFormatv("Cannot launch '{0}': {1}",
                                        args.GetArgumentAtIndex(0),
                                        llvm::fmt_consume(std::move(err)));
      } else {
        SetID(m_gdb_comm.GetCurrentProcessID());
      }
    }

    if (GetID() == LLDB_INVALID_PROCESS_ID) {
      LLDB_LOGF(log, "failed to connect to debugserver: %s",
                error.AsCString());
      KillDebugserverProcess();
      return error;
    }

    StringExtractorGDBRemote response;
    if (m_gdb_comm.GetStopReply(response)) {
      SetLastStopPacket(response);

      const ArchSpec &process_arch = m_gdb_comm.GetProcessArchitecture();

      if (process_arch.IsValid()) {
        GetTarget().MergeArchitecture(process_arch);
      } else {
        const ArchSpec &host_arch = m_gdb_comm.GetHostArchitecture();
        if (host_arch.IsValid())
          GetTarget().MergeArchitecture(host_arch);
      }

      SetPrivateState(SetThreadStopInfo(response));

      if (!disable_stdio) {
        if (pty.GetPrimaryFileDescriptor() != PseudoTerminal::invalid_fd)
          SetSTDIOFileDescriptor(pty.ReleasePrimaryFileDescriptor());
      }
    }
  } else {
    LLDB_LOGF(log, "failed to connect to debugserver: %s", error.AsCString());
  }
  return error;
}

Status ProcessGDBRemote::ConnectToDebugserver(llvm::StringRef connect_url) {
  Status error;
  // Only connect if we have a valid connect URL
  Log *log = GetLog(GDBRLog::Process);

  if (!connect_url.empty()) {
    LLDB_LOGF(log, "ProcessGDBRemote::%s Connecting to %s", __FUNCTION__,
              connect_url.str().c_str());
    std::unique_ptr<ConnectionFileDescriptor> conn_up(
        new ConnectionFileDescriptor());
    if (conn_up) {
      const uint32_t max_retry_count = 50;
      uint32_t retry_count = 0;
      while (!m_gdb_comm.IsConnected()) {
        if (conn_up->Connect(connect_url, &error) == eConnectionStatusSuccess) {
          m_gdb_comm.SetConnection(std::move(conn_up));
          break;
        }

        retry_count++;

        if (retry_count >= max_retry_count)
          break;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  if (!m_gdb_comm.IsConnected()) {
    if (error.Success())
      error.SetErrorString("not connected to remote gdb server");
    return error;
  }

  // We always seem to be able to open a connection to a local port so we need
  // to make sure we can then send data to it. If we can't then we aren't
  // actually connected to anything, so try and do the handshake with the
  // remote GDB server and make sure that goes alright.
  if (!m_gdb_comm.HandshakeWithServer(&error)) {
    m_gdb_comm.Disconnect();
    if (error.Success())
      error.SetErrorString("not connected to remote gdb server");
    return error;
  }

  m_gdb_comm.GetEchoSupported();
  m_gdb_comm.GetThreadSuffixSupported();
  m_gdb_comm.GetListThreadsInStopReplySupported();
  m_gdb_comm.GetHostInfo();
  m_gdb_comm.GetVContSupported('c');
  m_gdb_comm.GetVAttachOrWaitSupported();
  m_gdb_comm.EnableErrorStringInPacket();

  // First dispatch any commands from the platform:
  auto handle_cmds = [&] (const Args &args) ->  void {
    for (const Args::ArgEntry &entry : args) {
      StringExtractorGDBRemote response;
      m_gdb_comm.SendPacketAndWaitForResponse(
          entry.c_str(), response);
    }
  };

  PlatformSP platform_sp = GetTarget().GetPlatform();
  if (platform_sp) {
    handle_cmds(platform_sp->GetExtraStartupCommands());
  }

  // Then dispatch any process commands:
  handle_cmds(GetExtraStartupCommands());

  return error;
}

void ProcessGDBRemote::DidLaunchOrAttach(ArchSpec &process_arch) {
  Log *log = GetLog(GDBRLog::Process);
  BuildDynamicRegisterInfo(false);

  // See if the GDB server supports qHostInfo or qProcessInfo packets. Prefer
  // qProcessInfo as it will be more specific to our process.

  const ArchSpec &remote_process_arch = m_gdb_comm.GetProcessArchitecture();
  if (remote_process_arch.IsValid()) {
    process_arch = remote_process_arch;
    LLDB_LOG(log, "gdb-remote had process architecture, using {0} {1}",
             process_arch.GetArchitectureName(),
             process_arch.GetTriple().getTriple());
  } else {
    process_arch = m_gdb_comm.GetHostArchitecture();
    LLDB_LOG(log,
             "gdb-remote did not have process architecture, using gdb-remote "
             "host architecture {0} {1}",
             process_arch.GetArchitectureName(),
             process_arch.GetTriple().getTriple());
  }

  AddressableBits addressable_bits = m_gdb_comm.GetAddressableBits();
  SetAddressableBitMasks(addressable_bits);

  if (process_arch.IsValid()) {
    const ArchSpec &target_arch = GetTarget().GetArchitecture();
    if (target_arch.IsValid()) {
      LLDB_LOG(log, "analyzing target arch, currently {0} {1}",
               target_arch.GetArchitectureName(),
               target_arch.GetTriple().getTriple());

      // If the remote host is ARM and we have apple as the vendor, then
      // ARM executables and shared libraries can have mixed ARM
      // architectures.
      // You can have an armv6 executable, and if the host is armv7, then the
      // system will load the best possible architecture for all shared
      // libraries it has, so we really need to take the remote host
      // architecture as our defacto architecture in this case.

      if ((process_arch.GetMachine() == llvm::Triple::arm ||
           process_arch.GetMachine() == llvm::Triple::thumb) &&
          process_arch.GetTriple().getVendor() == llvm::Triple::Apple) {
        GetTarget().SetArchitecture(process_arch);
        LLDB_LOG(log,
                 "remote process is ARM/Apple, "
                 "setting target arch to {0} {1}",
                 process_arch.GetArchitectureName(),
                 process_arch.GetTriple().getTriple());
      } else {
        // Fill in what is missing in the triple
        const llvm::Triple &remote_triple = process_arch.GetTriple();
        llvm::Triple new_target_triple = target_arch.GetTriple();
        if (new_target_triple.getVendorName().size() == 0) {
          new_target_triple.setVendor(remote_triple.getVendor());

          if (new_target_triple.getOSName().size() == 0) {
            new_target_triple.setOS(remote_triple.getOS());

            if (new_target_triple.getEnvironmentName().size() == 0)
              new_target_triple.setEnvironment(remote_triple.getEnvironment());
          }

          ArchSpec new_target_arch = target_arch;
          new_target_arch.SetTriple(new_target_triple);
          GetTarget().SetArchitecture(new_target_arch);
        }
      }

      LLDB_LOG(log,
               "final target arch after adjustments for remote architecture: "
               "{0} {1}",
               target_arch.GetArchitectureName(),
               target_arch.GetTriple().getTriple());
    } else {
      // The target doesn't have a valid architecture yet, set it from the
      // architecture we got from the remote GDB server
      GetTarget().SetArchitecture(process_arch);
    }
  }

  // Target and Process are reasonably initailized;
  // load any binaries we have metadata for / set load address.
  LoadStubBinaries();
  MaybeLoadExecutableModule();

  // Find out which StructuredDataPlugins are supported by the debug monitor.
  // These plugins transmit data over async $J packets.
  if (StructuredData::Array *supported_packets =
          m_gdb_comm.GetSupportedStructuredDataPlugins())
    MapSupportedStructuredDataPlugins(*supported_packets);

  // If connected to LLDB ("native-signals+"), use signal defs for
  // the remote platform.  If connected to GDB, just use the standard set.
  if (!m_gdb_comm.UsesNativeSignals()) {
    SetUnixSignals(std::make_shared<GDBRemoteSignals>());
  } else {
    PlatformSP platform_sp = GetTarget().GetPlatform();
    if (platform_sp && platform_sp->IsConnected())
      SetUnixSignals(platform_sp->GetUnixSignals());
    else
      SetUnixSignals(UnixSignals::Create(GetTarget().GetArchitecture()));
  }
}

void ProcessGDBRemote::LoadStubBinaries() {
  // The remote stub may know about the "main binary" in
  // the context of a firmware debug session, and can
  // give us a UUID and an address/slide of where the
  // binary is loaded in memory.
  UUID standalone_uuid;
  addr_t standalone_value;
  bool standalone_value_is_offset;
  if (m_gdb_comm.GetProcessStandaloneBinary(standalone_uuid, standalone_value,
                                            standalone_value_is_offset)) {
    ModuleSP module_sp;

    if (standalone_uuid.IsValid()) {
      const bool force_symbol_search = true;
      const bool notify = true;
      const bool set_address_in_target = true;
      const bool allow_memory_image_last_resort = false;
      DynamicLoader::LoadBinaryWithUUIDAndAddress(
          this, "", standalone_uuid, standalone_value,
          standalone_value_is_offset, force_symbol_search, notify,
          set_address_in_target, allow_memory_image_last_resort);
    }
  }

  // The remote stub may know about a list of binaries to
  // force load into the process -- a firmware type situation
  // where multiple binaries are present in virtual memory,
  // and we are only given the addresses of the binaries.
  // Not intended for use with userland debugging, when we use
  // a DynamicLoader plugin that knows how to find the loaded
  // binaries, and will track updates as binaries are added.

  std::vector<addr_t> bin_addrs = m_gdb_comm.GetProcessStandaloneBinaries();
  if (bin_addrs.size()) {
    UUID uuid;
    const bool value_is_slide = false;
    for (addr_t addr : bin_addrs) {
      const bool notify = true;
      // First see if this is a special platform
      // binary that may determine the DynamicLoader and
      // Platform to be used in this Process and Target.
      if (GetTarget()
              .GetDebugger()
              .GetPlatformList()
              .LoadPlatformBinaryAndSetup(this, addr, notify))
        continue;

      const bool force_symbol_search = true;
      const bool set_address_in_target = true;
      const bool allow_memory_image_last_resort = false;
      // Second manually load this binary into the Target.
      DynamicLoader::LoadBinaryWithUUIDAndAddress(
          this, llvm::StringRef(), uuid, addr, value_is_slide,
          force_symbol_search, notify, set_address_in_target,
          allow_memory_image_last_resort);
    }
  }
}

void ProcessGDBRemote::MaybeLoadExecutableModule() {
  ModuleSP module_sp = GetTarget().GetExecutableModule();
  if (!module_sp)
    return;

  std::optional<QOffsets> offsets = m_gdb_comm.GetQOffsets();
  if (!offsets)
    return;

  bool is_uniform =
      size_t(llvm::count(offsets->offsets, offsets->offsets[0])) ==
      offsets->offsets.size();
  if (!is_uniform)
    return; // TODO: Handle non-uniform responses.

  bool changed = false;
  module_sp->SetLoadAddress(GetTarget(), offsets->offsets[0],
                            /*value_is_offset=*/true, changed);
  if (changed) {
    ModuleList list;
    list.Append(module_sp);
    m_process->GetTarget().ModulesDidLoad(list);
  }
}

void ProcessGDBRemote::DidLaunch() {
  ArchSpec process_arch;
  DidLaunchOrAttach(process_arch);
}

Status ProcessGDBRemote::DoAttachToProcessWithID(
    lldb::pid_t attach_pid, const ProcessAttachInfo &attach_info) {
  Log *log = GetLog(GDBRLog::Process);
  Status error;

  LLDB_LOGF(log, "ProcessGDBRemote::%s()", __FUNCTION__);

  // Clear out and clean up from any current state
  Clear();
  if (attach_pid != LLDB_INVALID_PROCESS_ID) {
    error = EstablishConnectionIfNeeded(attach_info);
    if (error.Success()) {
      m_gdb_comm.SetDetachOnError(attach_info.GetDetachOnError());

      char packet[64];
      const int packet_len =
          ::snprintf(packet, sizeof(packet), "vAttach;%" PRIx64, attach_pid);
      SetID(attach_pid);
      auto data_sp =
          std::make_shared<EventDataBytes>(llvm::StringRef(packet, packet_len));
      m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncContinue, data_sp);
    } else
      SetExitStatus(-1, error.AsCString());
  }

  return error;
}

Status ProcessGDBRemote::DoAttachToProcessWithName(
    const char *process_name, const ProcessAttachInfo &attach_info) {
  Status error;
  // Clear out and clean up from any current state
  Clear();

  if (process_name && process_name[0]) {
    error = EstablishConnectionIfNeeded(attach_info);
    if (error.Success()) {
      StreamString packet;

      m_gdb_comm.SetDetachOnError(attach_info.GetDetachOnError());

      if (attach_info.GetWaitForLaunch()) {
        if (!m_gdb_comm.GetVAttachOrWaitSupported()) {
          packet.PutCString("vAttachWait");
        } else {
          if (attach_info.GetIgnoreExisting())
            packet.PutCString("vAttachWait");
          else
            packet.PutCString("vAttachOrWait");
        }
      } else
        packet.PutCString("vAttachName");
      packet.PutChar(';');
      packet.PutBytesAsRawHex8(process_name, strlen(process_name),
                               endian::InlHostByteOrder(),
                               endian::InlHostByteOrder());

      auto data_sp = std::make_shared<EventDataBytes>(packet.GetString());
      m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncContinue, data_sp);

    } else
      SetExitStatus(-1, error.AsCString());
  }
  return error;
}

llvm::Expected<TraceSupportedResponse> ProcessGDBRemote::TraceSupported() {
  return m_gdb_comm.SendTraceSupported(GetInterruptTimeout());
}

llvm::Error ProcessGDBRemote::TraceStop(const TraceStopRequest &request) {
  return m_gdb_comm.SendTraceStop(request, GetInterruptTimeout());
}

llvm::Error ProcessGDBRemote::TraceStart(const llvm::json::Value &request) {
  return m_gdb_comm.SendTraceStart(request, GetInterruptTimeout());
}

llvm::Expected<std::string>
ProcessGDBRemote::TraceGetState(llvm::StringRef type) {
  return m_gdb_comm.SendTraceGetState(type, GetInterruptTimeout());
}

llvm::Expected<std::vector<uint8_t>>
ProcessGDBRemote::TraceGetBinaryData(const TraceGetBinaryDataRequest &request) {
  return m_gdb_comm.SendTraceGetBinaryData(request, GetInterruptTimeout());
}

void ProcessGDBRemote::DidExit() {
  // When we exit, disconnect from the GDB server communications
  m_gdb_comm.Disconnect();
}

void ProcessGDBRemote::DidAttach(ArchSpec &process_arch) {
  // If you can figure out what the architecture is, fill it in here.
  process_arch.Clear();
  DidLaunchOrAttach(process_arch);
}

Status ProcessGDBRemote::WillResume() {
  m_continue_c_tids.clear();
  m_continue_C_tids.clear();
  m_continue_s_tids.clear();
  m_continue_S_tids.clear();
  m_jstopinfo_sp.reset();
  m_jthreadsinfo_sp.reset();
  return Status();
}

Status ProcessGDBRemote::DoResume() {
  Status error;
  Log *log = GetLog(GDBRLog::Process);
  LLDB_LOGF(log, "ProcessGDBRemote::Resume()");

  ListenerSP listener_sp(
      Listener::MakeListener("gdb-remote.resume-packet-sent"));
  if (listener_sp->StartListeningForEvents(
          &m_gdb_comm, GDBRemoteClientBase::eBroadcastBitRunPacketSent)) {
    listener_sp->StartListeningForEvents(
        &m_async_broadcaster,
        ProcessGDBRemote::eBroadcastBitAsyncThreadDidExit);

    const size_t num_threads = GetThreadList().GetSize();

    StreamString continue_packet;
    bool continue_packet_error = false;
    if (m_gdb_comm.HasAnyVContSupport()) {
      std::string pid_prefix;
      if (m_gdb_comm.GetMultiprocessSupported())
        pid_prefix = llvm::formatv("p{0:x-}.", GetID());

      if (m_continue_c_tids.size() == num_threads ||
          (m_continue_c_tids.empty() && m_continue_C_tids.empty() &&
           m_continue_s_tids.empty() && m_continue_S_tids.empty())) {
        // All threads are continuing
        if (m_gdb_comm.GetMultiprocessSupported())
          continue_packet.Format("vCont;c:{0}-1", pid_prefix);
        else
          continue_packet.PutCString("c");
      } else {
        continue_packet.PutCString("vCont");

        if (!m_continue_c_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('c')) {
            for (tid_collection::const_iterator
                     t_pos = m_continue_c_tids.begin(),
                     t_end = m_continue_c_tids.end();
                 t_pos != t_end; ++t_pos)
              continue_packet.Format(";c:{0}{1:x-}", pid_prefix, *t_pos);
          } else
            continue_packet_error = true;
        }

        if (!continue_packet_error && !m_continue_C_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('C')) {
            for (tid_sig_collection::const_iterator
                     s_pos = m_continue_C_tids.begin(),
                     s_end = m_continue_C_tids.end();
                 s_pos != s_end; ++s_pos)
              continue_packet.Format(";C{0:x-2}:{1}{2:x-}", s_pos->second,
                                     pid_prefix, s_pos->first);
          } else
            continue_packet_error = true;
        }

        if (!continue_packet_error && !m_continue_s_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('s')) {
            for (tid_collection::const_iterator
                     t_pos = m_continue_s_tids.begin(),
                     t_end = m_continue_s_tids.end();
                 t_pos != t_end; ++t_pos)
              continue_packet.Format(";s:{0}{1:x-}", pid_prefix, *t_pos);
          } else
            continue_packet_error = true;
        }

        if (!continue_packet_error && !m_continue_S_tids.empty()) {
          if (m_gdb_comm.GetVContSupported('S')) {
            for (tid_sig_collection::const_iterator
                     s_pos = m_continue_S_tids.begin(),
                     s_end = m_continue_S_tids.end();
                 s_pos != s_end; ++s_pos)
              continue_packet.Format(";S{0:x-2}:{1}{2:x-}", s_pos->second,
                                     pid_prefix, s_pos->first);
          } else
            continue_packet_error = true;
        }

        if (continue_packet_error)
          continue_packet.Clear();
      }
    } else
      continue_packet_error = true;

    if (continue_packet_error) {
      // Either no vCont support, or we tried to use part of the vCont packet
      // that wasn't supported by the remote GDB server. We need to try and
      // make a simple packet that can do our continue
      const size_t num_continue_c_tids = m_continue_c_tids.size();
      const size_t num_continue_C_tids = m_continue_C_tids.size();
      const size_t num_continue_s_tids = m_continue_s_tids.size();
      const size_t num_continue_S_tids = m_continue_S_tids.size();
      if (num_continue_c_tids > 0) {
        if (num_continue_c_tids == num_threads) {
          // All threads are resuming...
          m_gdb_comm.SetCurrentThreadForRun(-1);
          continue_packet.PutChar('c');
          continue_packet_error = false;
        } else if (num_continue_c_tids == 1 && num_continue_C_tids == 0 &&
                   num_continue_s_tids == 0 && num_continue_S_tids == 0) {
          // Only one thread is continuing
          m_gdb_comm.SetCurrentThreadForRun(m_continue_c_tids.front());
          continue_packet.PutChar('c');
          continue_packet_error = false;
        }
      }

      if (continue_packet_error && num_continue_C_tids > 0) {
        if ((num_continue_C_tids + num_continue_c_tids) == num_threads &&
            num_continue_C_tids > 0 && num_continue_s_tids == 0 &&
            num_continue_S_tids == 0) {
          const int continue_signo = m_continue_C_tids.front().second;
          // Only one thread is continuing
          if (num_continue_C_tids > 1) {
            // More that one thread with a signal, yet we don't have vCont
            // support and we are being asked to resume each thread with a
            // signal, we need to make sure they are all the same signal, or we
            // can't issue the continue accurately with the current support...
            if (num_continue_C_tids > 1) {
              continue_packet_error = false;
              for (size_t i = 1; i < m_continue_C_tids.size(); ++i) {
                if (m_continue_C_tids[i].second != continue_signo)
                  continue_packet_error = true;
              }
            }
            if (!continue_packet_error)
              m_gdb_comm.SetCurrentThreadForRun(-1);
          } else {
            // Set the continue thread ID
            continue_packet_error = false;
            m_gdb_comm.SetCurrentThreadForRun(m_continue_C_tids.front().first);
          }
          if (!continue_packet_error) {
            // Add threads continuing with the same signo...
            continue_packet.Printf("C%2.2x", continue_signo);
          }
        }
      }

      if (continue_packet_error && num_continue_s_tids > 0) {
        if (num_continue_s_tids == num_threads) {
          // All threads are resuming...
          m_gdb_comm.SetCurrentThreadForRun(-1);

          continue_packet.PutChar('s');

          continue_packet_error = false;
        } else if (num_continue_c_tids == 0 && num_continue_C_tids == 0 &&
                   num_continue_s_tids == 1 && num_continue_S_tids == 0) {
          // Only one thread is stepping
          m_gdb_comm.SetCurrentThreadForRun(m_continue_s_tids.front());
          continue_packet.PutChar('s');
          continue_packet_error = false;
        }
      }

      if (!continue_packet_error && num_continue_S_tids > 0) {
        if (num_continue_S_tids == num_threads) {
          const int step_signo = m_continue_S_tids.front().second;
          // Are all threads trying to step with the same signal?
          continue_packet_error = false;
          if (num_continue_S_tids > 1) {
            for (size_t i = 1; i < num_threads; ++i) {
              if (m_continue_S_tids[i].second != step_signo)
                continue_packet_error = true;
            }
          }
          if (!continue_packet_error) {
            // Add threads stepping with the same signo...
            m_gdb_comm.SetCurrentThreadForRun(-1);
            continue_packet.Printf("S%2.2x", step_signo);
          }
        } else if (num_continue_c_tids == 0 && num_continue_C_tids == 0 &&
                   num_continue_s_tids == 0 && num_continue_S_tids == 1) {
          // Only one thread is stepping with signal
          m_gdb_comm.SetCurrentThreadForRun(m_continue_S_tids.front().first);
          continue_packet.Printf("S%2.2x", m_continue_S_tids.front().second);
          continue_packet_error = false;
        }
      }
    }

    if (continue_packet_error) {
      error.SetErrorString("can't make continue packet for this resume");
    } else {
      EventSP event_sp;
      if (!m_async_thread.IsJoinable()) {
        error.SetErrorString("Trying to resume but the async thread is dead.");
        LLDB_LOGF(log, "ProcessGDBRemote::DoResume: Trying to resume but the "
                       "async thread is dead.");
        return error;
      }

      auto data_sp =
          std::make_shared<EventDataBytes>(continue_packet.GetString());
      m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncContinue, data_sp);

      if (!listener_sp->GetEvent(event_sp, std::chrono::seconds(5))) {
        error.SetErrorString("Resume timed out.");
        LLDB_LOGF(log, "ProcessGDBRemote::DoResume: Resume timed out.");
      } else if (event_sp->BroadcasterIs(&m_async_broadcaster)) {
        error.SetErrorString("Broadcast continue, but the async thread was "
                             "killed before we got an ack back.");
        LLDB_LOGF(log,
                  "ProcessGDBRemote::DoResume: Broadcast continue, but the "
                  "async thread was killed before we got an ack back.");
        return error;
      }
    }
  }

  return error;
}

void ProcessGDBRemote::ClearThreadIDList() {
  std::lock_guard<std::recursive_mutex> guard(m_thread_list_real.GetMutex());
  m_thread_ids.clear();
  m_thread_pcs.clear();
}

size_t ProcessGDBRemote::UpdateThreadIDsFromStopReplyThreadsValue(
    llvm::StringRef value) {
  m_thread_ids.clear();
  lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID();
  StringExtractorGDBRemote thread_ids{value};

  do {
    auto pid_tid = thread_ids.GetPidTid(pid);
    if (pid_tid && pid_tid->first == pid) {
      lldb::tid_t tid = pid_tid->second;
      if (tid != LLDB_INVALID_THREAD_ID &&
          tid != StringExtractorGDBRemote::AllProcesses)
        m_thread_ids.push_back(tid);
    }
  } while (thread_ids.GetChar() == ',');

  return m_thread_ids.size();
}

size_t ProcessGDBRemote::UpdateThreadPCsFromStopReplyThreadsValue(
    llvm::StringRef value) {
  m_thread_pcs.clear();
  for (llvm::StringRef x : llvm::split(value, ',')) {
    lldb::addr_t pc;
    if (llvm::to_integer(x, pc, 16))
      m_thread_pcs.push_back(pc);
  }
  return m_thread_pcs.size();
}

bool ProcessGDBRemote::UpdateThreadIDList() {
  std::lock_guard<std::recursive_mutex> guard(m_thread_list_real.GetMutex());

  if (m_jthreadsinfo_sp) {
    // If we have the JSON threads info, we can get the thread list from that
    StructuredData::Array *thread_infos = m_jthreadsinfo_sp->GetAsArray();
    if (thread_infos && thread_infos->GetSize() > 0) {
      m_thread_ids.clear();
      m_thread_pcs.clear();
      thread_infos->ForEach([this](StructuredData::Object *object) -> bool {
        StructuredData::Dictionary *thread_dict = object->GetAsDictionary();
        if (thread_dict) {
          // Set the thread stop info from the JSON dictionary
          SetThreadStopInfo(thread_dict);
          lldb::tid_t tid = LLDB_INVALID_THREAD_ID;
          if (thread_dict->GetValueForKeyAsInteger<lldb::tid_t>("tid", tid))
            m_thread_ids.push_back(tid);
        }
        return true; // Keep iterating through all thread_info objects
      });
    }
    if (!m_thread_ids.empty())
      return true;
  } else {
    // See if we can get the thread IDs from the current stop reply packets
    // that might contain a "threads" key/value pair

    if (m_last_stop_packet) {
      // Get the thread stop info
      StringExtractorGDBRemote &stop_info = *m_last_stop_packet;
      const std::string &stop_info_str = std::string(stop_info.GetStringRef());

      m_thread_pcs.clear();
      const size_t thread_pcs_pos = stop_info_str.find(";thread-pcs:");
      if (thread_pcs_pos != std::string::npos) {
        const size_t start = thread_pcs_pos + strlen(";thread-pcs:");
        const size_t end = stop_info_str.find(';', start);
        if (end != std::string::npos) {
          std::string value = stop_info_str.substr(start, end - start);
          UpdateThreadPCsFromStopReplyThreadsValue(value);
        }
      }

      const size_t threads_pos = stop_info_str.find(";threads:");
      if (threads_pos != std::string::npos) {
        const size_t start = threads_pos + strlen(";threads:");
        const size_t end = stop_info_str.find(';', start);
        if (end != std::string::npos) {
          std::string value = stop_info_str.substr(start, end - start);
          if (UpdateThreadIDsFromStopReplyThreadsValue(value))
            return true;
        }
      }
    }
  }

  bool sequence_mutex_unavailable = false;
  m_gdb_comm.GetCurrentThreadIDs(m_thread_ids, sequence_mutex_unavailable);
  if (sequence_mutex_unavailable) {
    return false; // We just didn't get the list
  }
  return true;
}

bool ProcessGDBRemote::DoUpdateThreadList(ThreadList &old_thread_list,
                                          ThreadList &new_thread_list) {
  // locker will keep a mutex locked until it goes out of scope
  Log *log = GetLog(GDBRLog::Thread);
  LLDB_LOGV(log, "pid = {0}", GetID());

  size_t num_thread_ids = m_thread_ids.size();
  // The "m_thread_ids" thread ID list should always be updated after each stop
  // reply packet, but in case it isn't, update it here.
  if (num_thread_ids == 0) {
    if (!UpdateThreadIDList())
      return false;
    num_thread_ids = m_thread_ids.size();
  }

  ThreadList old_thread_list_copy(old_thread_list);
  if (num_thread_ids > 0) {
    for (size_t i = 0; i < num_thread_ids; ++i) {
      tid_t tid = m_thread_ids[i];
      ThreadSP thread_sp(
          old_thread_list_copy.RemoveThreadByProtocolID(tid, false));
      if (!thread_sp) {
        thread_sp = std::make_shared<ThreadGDBRemote>(*this, tid);
        LLDB_LOGV(log, "Making new thread: {0} for thread ID: {1:x}.",
                  thread_sp.get(), thread_sp->GetID());
      } else {
        LLDB_LOGV(log, "Found old thread: {0} for thread ID: {1:x}.",
                  thread_sp.get(), thread_sp->GetID());
      }

      SetThreadPc(thread_sp, i);
      new_thread_list.AddThreadSortedByIndexID(thread_sp);
    }
  }

  // Whatever that is left in old_thread_list_copy are not present in
  // new_thread_list. Remove non-existent threads from internal id table.
  size_t old_num_thread_ids = old_thread_list_copy.GetSize(false);
  for (size_t i = 0; i < old_num_thread_ids; i++) {
    ThreadSP old_thread_sp(old_thread_list_copy.GetThreadAtIndex(i, false));
    if (old_thread_sp) {
      lldb::tid_t old_thread_id = old_thread_sp->GetProtocolID();
      m_thread_id_to_index_id_map.erase(old_thread_id);
    }
  }

  return true;
}

void ProcessGDBRemote::SetThreadPc(const ThreadSP &thread_sp, uint64_t index) {
  if (m_thread_ids.size() == m_thread_pcs.size() && thread_sp.get() &&
      GetByteOrder() != eByteOrderInvalid) {
    ThreadGDBRemote *gdb_thread =
        static_cast<ThreadGDBRemote *>(thread_sp.get());
    RegisterContextSP reg_ctx_sp(thread_sp->GetRegisterContext());
    if (reg_ctx_sp) {
      uint32_t pc_regnum = reg_ctx_sp->ConvertRegisterKindToRegisterNumber(
          eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
      if (pc_regnum != LLDB_INVALID_REGNUM) {
        gdb_thread->PrivateSetRegisterValue(pc_regnum, m_thread_pcs[index]);
      }
    }
  }
}

bool ProcessGDBRemote::GetThreadStopInfoFromJSON(
    ThreadGDBRemote *thread, const StructuredData::ObjectSP &thread_infos_sp) {
  // See if we got thread stop infos for all threads via the "jThreadsInfo"
  // packet
  if (thread_infos_sp) {
    StructuredData::Array *thread_infos = thread_infos_sp->GetAsArray();
    if (thread_infos) {
      lldb::tid_t tid;
      const size_t n = thread_infos->GetSize();
      for (size_t i = 0; i < n; ++i) {
        StructuredData::Dictionary *thread_dict =
            thread_infos->GetItemAtIndex(i)->GetAsDictionary();
        if (thread_dict) {
          if (thread_dict->GetValueForKeyAsInteger<lldb::tid_t>(
                  "tid", tid, LLDB_INVALID_THREAD_ID)) {
            if (tid == thread->GetID())
              return (bool)SetThreadStopInfo(thread_dict);
          }
        }
      }
    }
  }
  return false;
}

bool ProcessGDBRemote::CalculateThreadStopInfo(ThreadGDBRemote *thread) {
  // See if we got thread stop infos for all threads via the "jThreadsInfo"
  // packet
  if (GetThreadStopInfoFromJSON(thread, m_jthreadsinfo_sp))
    return true;

  // See if we got thread stop info for any threads valid stop info reasons
  // threads via the "jstopinfo" packet stop reply packet key/value pair?
  if (m_jstopinfo_sp) {
    // If we have "jstopinfo" then we have stop descriptions for all threads
    // that have stop reasons, and if there is no entry for a thread, then it
    // has no stop reason.
    thread->GetRegisterContext()->InvalidateIfNeeded(true);
    if (!GetThreadStopInfoFromJSON(thread, m_jstopinfo_sp)) {
      // If a thread is stopped at a breakpoint site, set that as the stop
      // reason even if it hasn't executed the breakpoint instruction yet.
      // We will silently step over the breakpoint when we resume execution
      // and miss the fact that this thread hit the breakpoint.
      const size_t num_thread_ids = m_thread_ids.size();
      for (size_t i = 0; i < num_thread_ids; i++) {
        if (m_thread_ids[i] == thread->GetID() && m_thread_pcs.size() > i) {
          addr_t pc = m_thread_pcs[i];
          lldb::BreakpointSiteSP bp_site_sp =
              thread->GetProcess()->GetBreakpointSiteList().FindByAddress(pc);
          if (bp_site_sp) {
            if (bp_site_sp->ValidForThisThread(*thread)) {
              thread->SetStopInfo(
                  StopInfo::CreateStopReasonWithBreakpointSiteID(
                      *thread, bp_site_sp->GetID()));
              return true;
            }
          }
        }
      }
      thread->SetStopInfo(StopInfoSP());
    }
    return true;
  }

  // Fall back to using the qThreadStopInfo packet
  StringExtractorGDBRemote stop_packet;
  if (GetGDBRemote().GetThreadStopInfo(thread->GetProtocolID(), stop_packet))
    return SetThreadStopInfo(stop_packet) == eStateStopped;
  return false;
}

void ProcessGDBRemote::ParseExpeditedRegisters(
    ExpeditedRegisterMap &expedited_register_map, ThreadSP thread_sp) {
  ThreadGDBRemote *gdb_thread = static_cast<ThreadGDBRemote *>(thread_sp.get());
  RegisterContextSP gdb_reg_ctx_sp(gdb_thread->GetRegisterContext());

  for (const auto &pair : expedited_register_map) {
    StringExtractor reg_value_extractor(pair.second);
    WritableDataBufferSP buffer_sp(
        new DataBufferHeap(reg_value_extractor.GetStringRef().size() / 2, 0));
    reg_value_extractor.GetHexBytes(buffer_sp->GetData(), '\xcc');
    uint32_t lldb_regnum = gdb_reg_ctx_sp->ConvertRegisterKindToRegisterNumber(
        eRegisterKindProcessPlugin, pair.first);
    gdb_thread->PrivateSetRegisterValue(lldb_regnum, buffer_sp->GetData());
  }
}

ThreadSP ProcessGDBRemote::SetThreadStopInfo(
    lldb::tid_t tid, ExpeditedRegisterMap &expedited_register_map,
    uint8_t signo, const std::string &thread_name, const std::string &reason,
    const std::string &description, uint32_t exc_type,
    const std::vector<addr_t> &exc_data, addr_t thread_dispatch_qaddr,
    bool queue_vars_valid, // Set to true if queue_name, queue_kind and
                           // queue_serial are valid
    LazyBool associated_with_dispatch_queue, addr_t dispatch_queue_t,
    std::string &queue_name, QueueKind queue_kind, uint64_t queue_serial) {

  if (tid == LLDB_INVALID_THREAD_ID)
    return nullptr;

  ThreadSP thread_sp;
  // Scope for "locker" below
  {
    // m_thread_list_real does have its own mutex, but we need to hold onto the
    // mutex between the call to m_thread_list_real.FindThreadByID(...) and the
    // m_thread_list_real.AddThread(...) so it doesn't change on us
    std::lock_guard<std::recursive_mutex> guard(m_thread_list_real.GetMutex());
    thread_sp = m_thread_list_real.FindThreadByProtocolID(tid, false);

    if (!thread_sp) {
      // Create the thread if we need to
      thread_sp = std::make_shared<ThreadGDBRemote>(*this, tid);
      m_thread_list_real.AddThread(thread_sp);
    }
  }

  ThreadGDBRemote *gdb_thread = static_cast<ThreadGDBRemote *>(thread_sp.get());
  RegisterContextSP reg_ctx_sp(gdb_thread->GetRegisterContext());

  reg_ctx_sp->InvalidateIfNeeded(true);

  auto iter = std::find(m_thread_ids.begin(), m_thread_ids.end(), tid);
  if (iter != m_thread_ids.end())
    SetThreadPc(thread_sp, iter - m_thread_ids.begin());

  ParseExpeditedRegisters(expedited_register_map, thread_sp);

  if (reg_ctx_sp->ReconfigureRegisterInfo()) {
    // Now we have changed the offsets of all the registers, so the values
    // will be corrupted.
    reg_ctx_sp->InvalidateAllRegisters();
    // Expedited registers values will never contain registers that would be
    // resized by a reconfigure. So we are safe to continue using these
    // values.
    ParseExpeditedRegisters(expedited_register_map, thread_sp);
  }

  thread_sp->SetName(thread_name.empty() ? nullptr : thread_name.c_str());

  gdb_thread->SetThreadDispatchQAddr(thread_dispatch_qaddr);
  // Check if the GDB server was able to provide the queue name, kind and serial
  // number
  if (queue_vars_valid)
    gdb_thread->SetQueueInfo(std::move(queue_name), queue_kind, queue_serial,
                             dispatch_queue_t, associated_with_dispatch_queue);
  else
    gdb_thread->ClearQueueInfo();

  gdb_thread->SetAssociatedWithLibdispatchQueue(associated_with_dispatch_queue);

  if (dispatch_queue_t != LLDB_INVALID_ADDRESS)
    gdb_thread->SetQueueLibdispatchQueueAddress(dispatch_queue_t);

  // Make sure we update our thread stop reason just once, but don't overwrite
  // the stop info for threads that haven't moved:
  StopInfoSP current_stop_info_sp = thread_sp->GetPrivateStopInfo(false);
  if (thread_sp->GetTemporaryResumeState() == eStateSuspended &&
      current_stop_info_sp) {
    thread_sp->SetStopInfo(current_stop_info_sp);
    return thread_sp;
  }

  if (!thread_sp->StopInfoIsUpToDate()) {
    thread_sp->SetStopInfo(StopInfoSP());
    // If there's a memory thread backed by this thread, we need to use it to
    // calculate StopInfo.
    if (ThreadSP memory_thread_sp = m_thread_list.GetBackingThread(thread_sp))
      thread_sp = memory_thread_sp;

    if (exc_type != 0) {
      const size_t exc_data_size = exc_data.size();

      thread_sp->SetStopInfo(
          StopInfoMachException::CreateStopReasonWithMachException(
              *thread_sp, exc_type, exc_data_size,
              exc_data_size >= 1 ? exc_data[0] : 0,
              exc_data_size >= 2 ? exc_data[1] : 0,
              exc_data_size >= 3 ? exc_data[2] : 0));
    } else {
      bool handled = false;
      bool did_exec = false;
      // debugserver can send reason = "none" which is equivalent
      // to no reason.
      if (!reason.empty() && reason != "none") {
        if (reason == "trace") {
          addr_t pc = thread_sp->GetRegisterContext()->GetPC();
          lldb::BreakpointSiteSP bp_site_sp =
              thread_sp->GetProcess()->GetBreakpointSiteList().FindByAddress(
                  pc);

          // If the current pc is a breakpoint site then the StopInfo should be
          // set to Breakpoint Otherwise, it will be set to Trace.
          if (bp_site_sp && bp_site_sp->ValidForThisThread(*thread_sp)) {
            thread_sp->SetStopInfo(
                StopInfo::CreateStopReasonWithBreakpointSiteID(
                    *thread_sp, bp_site_sp->GetID()));
          } else
            thread_sp->SetStopInfo(
                StopInfo::CreateStopReasonToTrace(*thread_sp));
          handled = true;
        } else if (reason == "breakpoint") {
          addr_t pc = thread_sp->GetRegisterContext()->GetPC();
          lldb::BreakpointSiteSP bp_site_sp =
              thread_sp->GetProcess()->GetBreakpointSiteList().FindByAddress(
                  pc);
          if (bp_site_sp) {
            // If the breakpoint is for this thread, then we'll report the hit,
            // but if it is for another thread, we can just report no reason.
            // We don't need to worry about stepping over the breakpoint here,
            // that will be taken care of when the thread resumes and notices
            // that there's a breakpoint under the pc.
            handled = true;
            if (bp_site_sp->ValidForThisThread(*thread_sp)) {
              thread_sp->SetStopInfo(
                  StopInfo::CreateStopReasonWithBreakpointSiteID(
                      *thread_sp, bp_site_sp->GetID()));
            } else {
              StopInfoSP invalid_stop_info_sp;
              thread_sp->SetStopInfo(invalid_stop_info_sp);
            }
          }
        } else if (reason == "trap") {
          // Let the trap just use the standard signal stop reason below...
        } else if (reason == "watchpoint") {
          // We will have between 1 and 3 fields in the description.
          //
          // \a wp_addr which is the original start address that
          // lldb requested be watched, or an address that the
          // hardware reported.  This address should be within the
          // range of a currently active watchpoint region - lldb
          // should be able to find a watchpoint with this address.
          //
          // \a wp_index is the hardware watchpoint register number.
          //
          // \a wp_hit_addr is the actual address reported by the hardware,
          // which may be outside the range of a region we are watching.
          //
          // On MIPS, we may get a false watchpoint exception where an
          // access to the same 8 byte granule as a watchpoint will trigger,
          // even if the access was not within the range of the watched
          // region. When we get a \a wp_hit_addr outside the range of any
          // set watchpoint, continue execution without making it visible to
          // the user.
          //
          // On ARM, a related issue where a large access that starts
          // before the watched region (and extends into the watched
          // region) may report a hit address before the watched region.
          // lldb will not find the "nearest" watchpoint to
          // disable/step/re-enable it, so one of the valid watchpoint
          // addresses should be provided as \a wp_addr.
          StringExtractor desc_extractor(description.c_str());
          // FIXME NativeThreadLinux::SetStoppedByWatchpoint sends this
          // up as
          //  <address within wp range> <wp hw index> <actual accessed addr>
          // but this is not reading the <wp hw index>.  Seems like it
          // wouldn't work on MIPS, where that third field is important.
          addr_t wp_addr = desc_extractor.GetU64(LLDB_INVALID_ADDRESS);
          addr_t wp_hit_addr = desc_extractor.GetU64(LLDB_INVALID_ADDRESS);
          watch_id_t watch_id = LLDB_INVALID_WATCH_ID;
          bool silently_continue = false;
          WatchpointResourceSP wp_resource_sp;
          if (wp_hit_addr != LLDB_INVALID_ADDRESS) {
            wp_resource_sp =
                m_watchpoint_resource_list.FindByAddress(wp_hit_addr);
            // On MIPS, \a wp_hit_addr outside the range of a watched
            // region means we should silently continue, it is a false hit.
            ArchSpec::Core core = GetTarget().GetArchitecture().GetCore();
            if (!wp_resource_sp && core >= ArchSpec::kCore_mips_first &&
                core <= ArchSpec::kCore_mips_last)
              silently_continue = true;
          }
          if (!wp_resource_sp && wp_addr != LLDB_INVALID_ADDRESS)
            wp_resource_sp = m_watchpoint_resource_list.FindByAddress(wp_addr);
          if (!wp_resource_sp) {
            Log *log(GetLog(GDBRLog::Watchpoints));
            LLDB_LOGF(log, "failed to find watchpoint");
            watch_id = LLDB_INVALID_SITE_ID;
          } else {
            // LWP_TODO: This is hardcoding a single Watchpoint in a
            // Resource, need to add
            // StopInfo::CreateStopReasonWithWatchpointResource which
            // represents all watchpoints that were tripped at this stop.
            watch_id = wp_resource_sp->GetConstituentAtIndex(0)->GetID();
          }
          thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithWatchpointID(
              *thread_sp, watch_id, silently_continue));
          handled = true;
        } else if (reason == "exception") {
          thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithException(
              *thread_sp, description.c_str()));
          handled = true;
        } else if (reason == "exec") {
          did_exec = true;
          thread_sp->SetStopInfo(
              StopInfo::CreateStopReasonWithExec(*thread_sp));
          handled = true;
        } else if (reason == "processor trace") {
          thread_sp->SetStopInfo(StopInfo::CreateStopReasonProcessorTrace(
              *thread_sp, description.c_str()));
        } else if (reason == "fork") {
          StringExtractor desc_extractor(description.c_str());
          lldb::pid_t child_pid =
              desc_extractor.GetU64(LLDB_INVALID_PROCESS_ID);
          lldb::tid_t child_tid = desc_extractor.GetU64(LLDB_INVALID_THREAD_ID);
          thread_sp->SetStopInfo(
              StopInfo::CreateStopReasonFork(*thread_sp, child_pid, child_tid));
          handled = true;
        } else if (reason == "vfork") {
          StringExtractor desc_extractor(description.c_str());
          lldb::pid_t child_pid =
              desc_extractor.GetU64(LLDB_INVALID_PROCESS_ID);
          lldb::tid_t child_tid = desc_extractor.GetU64(LLDB_INVALID_THREAD_ID);
          thread_sp->SetStopInfo(StopInfo::CreateStopReasonVFork(
              *thread_sp, child_pid, child_tid));
          handled = true;
        } else if (reason == "vforkdone") {
          thread_sp->SetStopInfo(
              StopInfo::CreateStopReasonVForkDone(*thread_sp));
          handled = true;
        }
      } else if (!signo) {
        addr_t pc = thread_sp->GetRegisterContext()->GetPC();
        lldb::BreakpointSiteSP bp_site_sp =
            thread_sp->GetProcess()->GetBreakpointSiteList().FindByAddress(pc);

        // If a thread is stopped at a breakpoint site, set that as the stop
        // reason even if it hasn't executed the breakpoint instruction yet.
        // We will silently step over the breakpoint when we resume execution
        // and miss the fact that this thread hit the breakpoint.
        if (bp_site_sp && bp_site_sp->ValidForThisThread(*thread_sp)) {
          thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithBreakpointSiteID(
              *thread_sp, bp_site_sp->GetID()));
          handled = true;
        }
      }

      if (!handled && signo && !did_exec) {
        if (signo == SIGTRAP) {
          // Currently we are going to assume SIGTRAP means we are either
          // hitting a breakpoint or hardware single stepping.
          handled = true;
          addr_t pc =
              thread_sp->GetRegisterContext()->GetPC() + m_breakpoint_pc_offset;
          lldb::BreakpointSiteSP bp_site_sp =
              thread_sp->GetProcess()->GetBreakpointSiteList().FindByAddress(
                  pc);

          if (bp_site_sp) {
            // If the breakpoint is for this thread, then we'll report the hit,
            // but if it is for another thread, we can just report no reason.
            // We don't need to worry about stepping over the breakpoint here,
            // that will be taken care of when the thread resumes and notices
            // that there's a breakpoint under the pc.
            if (bp_site_sp->ValidForThisThread(*thread_sp)) {
              if (m_breakpoint_pc_offset != 0)
                thread_sp->GetRegisterContext()->SetPC(pc);
              thread_sp->SetStopInfo(
                  StopInfo::CreateStopReasonWithBreakpointSiteID(
                      *thread_sp, bp_site_sp->GetID()));
            } else {
              StopInfoSP invalid_stop_info_sp;
              thread_sp->SetStopInfo(invalid_stop_info_sp);
            }
          } else {
            // If we were stepping then assume the stop was the result of the
            // trace.  If we were not stepping then report the SIGTRAP.
            // FIXME: We are still missing the case where we single step over a
            // trap instruction.
            if (thread_sp->GetTemporaryResumeState() == eStateStepping)
              thread_sp->SetStopInfo(
                  StopInfo::CreateStopReasonToTrace(*thread_sp));
            else
              thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithSignal(
                  *thread_sp, signo, description.c_str()));
          }
        }
        if (!handled)
          thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithSignal(
              *thread_sp, signo, description.c_str()));
      }

      if (!description.empty()) {
        lldb::StopInfoSP stop_info_sp(thread_sp->GetStopInfo());
        if (stop_info_sp) {
          const char *stop_info_desc = stop_info_sp->GetDescription();
          if (!stop_info_desc || !stop_info_desc[0])
            stop_info_sp->SetDescription(description.c_str());
        } else {
          thread_sp->SetStopInfo(StopInfo::CreateStopReasonWithException(
              *thread_sp, description.c_str()));
        }
      }
    }
  }
  return thread_sp;
}

lldb::ThreadSP
ProcessGDBRemote::SetThreadStopInfo(StructuredData::Dictionary *thread_dict) {
  static constexpr llvm::StringLiteral g_key_tid("tid");
  static constexpr llvm::StringLiteral g_key_name("name");
  static constexpr llvm::StringLiteral g_key_reason("reason");
  static constexpr llvm::StringLiteral g_key_metype("metype");
  static constexpr llvm::StringLiteral g_key_medata("medata");
  static constexpr llvm::StringLiteral g_key_qaddr("qaddr");
  static constexpr llvm::StringLiteral g_key_dispatch_queue_t(
      "dispatch_queue_t");
  static constexpr llvm::StringLiteral g_key_associated_with_dispatch_queue(
      "associated_with_dispatch_queue");
  static constexpr llvm::StringLiteral g_key_queue_name("qname");
  static constexpr llvm::StringLiteral g_key_queue_kind("qkind");
  static constexpr llvm::StringLiteral g_key_queue_serial_number("qserialnum");
  static constexpr llvm::StringLiteral g_key_registers("registers");
  static constexpr llvm::StringLiteral g_key_memory("memory");
  static constexpr llvm::StringLiteral g_key_description("description");
  static constexpr llvm::StringLiteral g_key_signal("signal");

  // Stop with signal and thread info
  lldb::tid_t tid = LLDB_INVALID_THREAD_ID;
  uint8_t signo = 0;
  std::string value;
  std::string thread_name;
  std::string reason;
  std::string description;
  uint32_t exc_type = 0;
  std::vector<addr_t> exc_data;
  addr_t thread_dispatch_qaddr = LLDB_INVALID_ADDRESS;
  ExpeditedRegisterMap expedited_register_map;
  bool queue_vars_valid = false;
  addr_t dispatch_queue_t = LLDB_INVALID_ADDRESS;
  LazyBool associated_with_dispatch_queue = eLazyBoolCalculate;
  std::string queue_name;
  QueueKind queue_kind = eQueueKindUnknown;
  uint64_t queue_serial_number = 0;
  // Iterate through all of the thread dictionary key/value pairs from the
  // structured data dictionary

  // FIXME: we're silently ignoring invalid data here
  thread_dict->ForEach([this, &tid, &expedited_register_map, &thread_name,
                        &signo, &reason, &description, &exc_type, &exc_data,
                        &thread_dispatch_qaddr, &queue_vars_valid,
                        &associated_with_dispatch_queue, &dispatch_queue_t,
                        &queue_name, &queue_kind, &queue_serial_number](
                           llvm::StringRef key,
                           StructuredData::Object *object) -> bool {
    if (key == g_key_tid) {
      // thread in big endian hex
      tid = object->GetUnsignedIntegerValue(LLDB_INVALID_THREAD_ID);
    } else if (key == g_key_metype) {
      // exception type in big endian hex
      exc_type = object->GetUnsignedIntegerValue(0);
    } else if (key == g_key_medata) {
      // exception data in big endian hex
      StructuredData::Array *array = object->GetAsArray();
      if (array) {
        array->ForEach([&exc_data](StructuredData::Object *object) -> bool {
          exc_data.push_back(object->GetUnsignedIntegerValue());
          return true; // Keep iterating through all array items
        });
      }
    } else if (key == g_key_name) {
      thread_name = std::string(object->GetStringValue());
    } else if (key == g_key_qaddr) {
      thread_dispatch_qaddr =
          object->GetUnsignedIntegerValue(LLDB_INVALID_ADDRESS);
    } else if (key == g_key_queue_name) {
      queue_vars_valid = true;
      queue_name = std::string(object->GetStringValue());
    } else if (key == g_key_queue_kind) {
      std::string queue_kind_str = std::string(object->GetStringValue());
      if (queue_kind_str == "serial") {
        queue_vars_valid = true;
        queue_kind = eQueueKindSerial;
      } else if (queue_kind_str == "concurrent") {
        queue_vars_valid = true;
        queue_kind = eQueueKindConcurrent;
      }
    } else if (key == g_key_queue_serial_number) {
      queue_serial_number = object->GetUnsignedIntegerValue(0);
      if (queue_serial_number != 0)
        queue_vars_valid = true;
    } else if (key == g_key_dispatch_queue_t) {
      dispatch_queue_t = object->GetUnsignedIntegerValue(0);
      if (dispatch_queue_t != 0 && dispatch_queue_t != LLDB_INVALID_ADDRESS)
        queue_vars_valid = true;
    } else if (key == g_key_associated_with_dispatch_queue) {
      queue_vars_valid = true;
      bool associated = object->GetBooleanValue();
      if (associated)
        associated_with_dispatch_queue = eLazyBoolYes;
      else
        associated_with_dispatch_queue = eLazyBoolNo;
    } else if (key == g_key_reason) {
      reason = std::string(object->GetStringValue());
    } else if (key == g_key_description) {
      description = std::string(object->GetStringValue());
    } else if (key == g_key_registers) {
      StructuredData::Dictionary *registers_dict = object->GetAsDictionary();

      if (registers_dict) {
        registers_dict->ForEach(
            [&expedited_register_map](llvm::StringRef key,
                                      StructuredData::Object *object) -> bool {
              uint32_t reg;
              if (llvm::to_integer(key, reg))
                expedited_register_map[reg] =
                    std::string(object->GetStringValue());
              return true; // Keep iterating through all array items
            });
      }
    } else if (key == g_key_memory) {
      StructuredData::Array *array = object->GetAsArray();
      if (array) {
        array->ForEach([this](StructuredData::Object *object) -> bool {
          StructuredData::Dictionary *mem_cache_dict =
              object->GetAsDictionary();
          if (mem_cache_dict) {
            lldb::addr_t mem_cache_addr = LLDB_INVALID_ADDRESS;
            if (mem_cache_dict->GetValueForKeyAsInteger<lldb::addr_t>(
                    "address", mem_cache_addr)) {
              if (mem_cache_addr != LLDB_INVALID_ADDRESS) {
                llvm::StringRef str;
                if (mem_cache_dict->GetValueForKeyAsString("bytes", str)) {
                  StringExtractor bytes(str);
                  bytes.SetFilePos(0);

                  const size_t byte_size = bytes.GetStringRef().size() / 2;
                  WritableDataBufferSP data_buffer_sp(
                      new DataBufferHeap(byte_size, 0));
                  const size_t bytes_copied =
                      bytes.GetHexBytes(data_buffer_sp->GetData(), 0);
                  if (bytes_copied == byte_size)
                    m_memory_cache.AddL1CacheData(mem_cache_addr,
                                                  data_buffer_sp);
                }
              }
            }
          }
          return true; // Keep iterating through all array items
        });
      }

    } else if (key == g_key_signal)
      signo = object->GetUnsignedIntegerValue(LLDB_INVALID_SIGNAL_NUMBER);
    return true; // Keep iterating through all dictionary key/value pairs
  });

  return SetThreadStopInfo(tid, expedited_register_map, signo, thread_name,
                           reason, description, exc_type, exc_data,
                           thread_dispatch_qaddr, queue_vars_valid,
                           associated_with_dispatch_queue, dispatch_queue_t,
                           queue_name, queue_kind, queue_serial_number);
}

StateType ProcessGDBRemote::SetThreadStopInfo(StringExtractor &stop_packet) {
  lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID();
  stop_packet.SetFilePos(0);
  const char stop_type = stop_packet.GetChar();
  switch (stop_type) {
  case 'T':
  case 'S': {
    // This is a bit of a hack, but it is required. If we did exec, we need to
    // clear our thread lists and also know to rebuild our dynamic register
    // info before we lookup and threads and populate the expedited register
    // values so we need to know this right away so we can cleanup and update
    // our registers.
    const uint32_t stop_id = GetStopID();
    if (stop_id == 0) {
      // Our first stop, make sure we have a process ID, and also make sure we
      // know about our registers
      if (GetID() == LLDB_INVALID_PROCESS_ID && pid != LLDB_INVALID_PROCESS_ID)
        SetID(pid);
      BuildDynamicRegisterInfo(true);
    }
    // Stop with signal and thread info
    lldb::pid_t stop_pid = LLDB_INVALID_PROCESS_ID;
    lldb::tid_t tid = LLDB_INVALID_THREAD_ID;
    const uint8_t signo = stop_packet.GetHexU8();
    llvm::StringRef key;
    llvm::StringRef value;
    std::string thread_name;
    std::string reason;
    std::string description;
    uint32_t exc_type = 0;
    std::vector<addr_t> exc_data;
    addr_t thread_dispatch_qaddr = LLDB_INVALID_ADDRESS;
    bool queue_vars_valid =
        false; // says if locals below that start with "queue_" are valid
    addr_t dispatch_queue_t = LLDB_INVALID_ADDRESS;
    LazyBool associated_with_dispatch_queue = eLazyBoolCalculate;
    std::string queue_name;
    QueueKind queue_kind = eQueueKindUnknown;
    uint64_t queue_serial_number = 0;
    ExpeditedRegisterMap expedited_register_map;
    AddressableBits addressable_bits;
    while (stop_packet.GetNameColonValue(key, value)) {
      if (key.compare("metype") == 0) {
        // exception type in big endian hex
        value.getAsInteger(16, exc_type);
      } else if (key.compare("medata") == 0) {
        // exception data in big endian hex
        uint64_t x;
        value.getAsInteger(16, x);
        exc_data.push_back(x);
      } else if (key.compare("thread") == 0) {
        // thread-id
        StringExtractorGDBRemote thread_id{value};
        auto pid_tid = thread_id.GetPidTid(pid);
        if (pid_tid) {
          stop_pid = pid_tid->first;
          tid = pid_tid->second;
        } else
          tid = LLDB_INVALID_THREAD_ID;
      } else if (key.compare("threads") == 0) {
        std::lock_guard<std::recursive_mutex> guard(
            m_thread_list_real.GetMutex());
        UpdateThreadIDsFromStopReplyThreadsValue(value);
      } else if (key.compare("thread-pcs") == 0) {
        m_thread_pcs.clear();
        // A comma separated list of all threads in the current
        // process that includes the thread for this stop reply packet
        lldb::addr_t pc;
        while (!value.empty()) {
          llvm::StringRef pc_str;
          std::tie(pc_str, value) = value.split(',');
          if (pc_str.getAsInteger(16, pc))
            pc = LLDB_INVALID_ADDRESS;
          m_thread_pcs.push_back(pc);
        }
      } else if (key.compare("jstopinfo") == 0) {
        StringExtractor json_extractor(value);
        std::string json;
        // Now convert the HEX bytes into a string value
        json_extractor.GetHexByteString(json);

        // This JSON contains thread IDs and thread stop info for all threads.
        // It doesn't contain expedited registers, memory or queue info.
        m_jstopinfo_sp = StructuredData::ParseJSON(json);
      } else if (key.compare("hexname") == 0) {
        StringExtractor name_extractor(value);
        std::string name;
        // Now convert the HEX bytes into a string value
        name_extractor.GetHexByteString(thread_name);
      } else if (key.compare("name") == 0) {
        thread_name = std::string(value);
      } else if (key.compare("qaddr") == 0) {
        value.getAsInteger(16, thread_dispatch_qaddr);
      } else if (key.compare("dispatch_queue_t") == 0) {
        queue_vars_valid = true;
        value.getAsInteger(16, dispatch_queue_t);
      } else if (key.compare("qname") == 0) {
        queue_vars_valid = true;
        StringExtractor name_extractor(value);
        // Now convert the HEX bytes into a string value
        name_extractor.GetHexByteString(queue_name);
      } else if (key.compare("qkind") == 0) {
        queue_kind = llvm::StringSwitch<QueueKind>(value)
                         .Case("serial", eQueueKindSerial)
                         .Case("concurrent", eQueueKindConcurrent)
                         .Default(eQueueKindUnknown);
        queue_vars_valid = queue_kind != eQueueKindUnknown;
      } else if (key.compare("qserialnum") == 0) {
        if (!value.getAsInteger(0, queue_serial_number))
          queue_vars_valid = true;
      } else if (key.compare("reason") == 0) {
        reason = std::string(value);
      } else if (key.compare("description") == 0) {
        StringExtractor desc_extractor(value);
        // Now convert the HEX bytes into a string value
        desc_extractor.GetHexByteString(description);
      } else if (key.compare("memory") == 0) {
        // Expedited memory. GDB servers can choose to send back expedited
        // memory that can populate the L1 memory cache in the process so that
        // things like the frame pointer backchain can be expedited. This will
        // help stack backtracing be more efficient by not having to send as
        // many memory read requests down the remote GDB server.

        // Key/value pair format: memory:<addr>=<bytes>;
        // <addr> is a number whose base will be interpreted by the prefix:
        //      "0x[0-9a-fA-F]+" for hex
        //      "0[0-7]+" for octal
        //      "[1-9]+" for decimal
        // <bytes> is native endian ASCII hex bytes just like the register
        // values
        llvm::StringRef addr_str, bytes_str;
        std::tie(addr_str, bytes_str) = value.split('=');
        if (!addr_str.empty() && !bytes_str.empty()) {
          lldb::addr_t mem_cache_addr = LLDB_INVALID_ADDRESS;
          if (!addr_str.getAsInteger(0, mem_cache_addr)) {
            StringExtractor bytes(bytes_str);
            const size_t byte_size = bytes.GetBytesLeft() / 2;
            WritableDataBufferSP data_buffer_sp(
                new DataBufferHeap(byte_size, 0));
            const size_t bytes_copied =
                bytes.GetHexBytes(data_buffer_sp->GetData(), 0);
            if (bytes_copied == byte_size)
              m_memory_cache.AddL1CacheData(mem_cache_addr, data_buffer_sp);
          }
        }
      } else if (key.compare("watch") == 0 || key.compare("rwatch") == 0 ||
                 key.compare("awatch") == 0) {
        // Support standard GDB remote stop reply packet 'TAAwatch:addr'
        lldb::addr_t wp_addr = LLDB_INVALID_ADDRESS;
        value.getAsInteger(16, wp_addr);

        WatchpointResourceSP wp_resource_sp =
            m_watchpoint_resource_list.FindByAddress(wp_addr);

        // Rewrite gdb standard watch/rwatch/awatch to
        // "reason:watchpoint" + "description:ADDR",
        // which is parsed in SetThreadStopInfo.
        reason = "watchpoint";
        StreamString ostr;
        ostr.Printf("%" PRIu64, wp_addr);
        description = std::string(ostr.GetString());
      } else if (key.compare("library") == 0) {
        auto error = LoadModules();
        if (error) {
          Log *log(GetLog(GDBRLog::Process));
          LLDB_LOG_ERROR(log, std::move(error), "Failed to load modules: {0}");
        }
      } else if (key.compare("fork") == 0 || key.compare("vfork") == 0) {
        // fork includes child pid/tid in thread-id format
        StringExtractorGDBRemote thread_id{value};
        auto pid_tid = thread_id.GetPidTid(LLDB_INVALID_PROCESS_ID);
        if (!pid_tid) {
          Log *log(GetLog(GDBRLog::Process));
          LLDB_LOG(log, "Invalid PID/TID to fork: {0}", value);
          pid_tid = {{LLDB_INVALID_PROCESS_ID, LLDB_INVALID_THREAD_ID}};
        }

        reason = key.str();
        StreamString ostr;
        ostr.Printf("%" PRIu64 " %" PRIu64, pid_tid->first, pid_tid->second);
        description = std::string(ostr.GetString());
      } else if (key.compare("addressing_bits") == 0) {
        uint64_t addressing_bits;
        if (!value.getAsInteger(0, addressing_bits)) {
          addressable_bits.SetAddressableBits(addressing_bits);
        }
      } else if (key.compare("low_mem_addressing_bits") == 0) {
        uint64_t addressing_bits;
        if (!value.getAsInteger(0, addressing_bits)) {
          addressable_bits.SetLowmemAddressableBits(addressing_bits);
        }
      } else if (key.compare("high_mem_addressing_bits") == 0) {
        uint64_t addressing_bits;
        if (!value.getAsInteger(0, addressing_bits)) {
          addressable_bits.SetHighmemAddressableBits(addressing_bits);
        }
      } else if (key.size() == 2 && ::isxdigit(key[0]) && ::isxdigit(key[1])) {
        uint32_t reg = UINT32_MAX;
        if (!key.getAsInteger(16, reg))
          expedited_register_map[reg] = std::string(std::move(value));
      }
    }

    if (stop_pid != LLDB_INVALID_PROCESS_ID && stop_pid != pid) {
      Log *log = GetLog(GDBRLog::Process);
      LLDB_LOG(log,
               "Received stop for incorrect PID = {0} (inferior PID = {1})",
               stop_pid, pid);
      return eStateInvalid;
    }

    if (tid == LLDB_INVALID_THREAD_ID) {
      // A thread id may be invalid if the response is old style 'S' packet
      // which does not provide the
      // thread information. So update the thread list and choose the first
      // one.
      UpdateThreadIDList();

      if (!m_thread_ids.empty()) {
        tid = m_thread_ids.front();
      }
    }

    SetAddressableBitMasks(addressable_bits);

    ThreadSP thread_sp = SetThreadStopInfo(
        tid, expedited_register_map, signo, thread_name, reason, description,
        exc_type, exc_data, thread_dispatch_qaddr, queue_vars_valid,
        associated_with_dispatch_queue, dispatch_queue_t, queue_name,
        queue_kind, queue_serial_number);

    return eStateStopped;
  } break;

  case 'W':
  case 'X':
    // process exited
    return eStateExited;

  default:
    break;
  }
  return eStateInvalid;
}

void ProcessGDBRemote::RefreshStateAfterStop() {
  std::lock_guard<std::recursive_mutex> guard(m_thread_list_real.GetMutex());

  m_thread_ids.clear();
  m_thread_pcs.clear();

  // Set the thread stop info. It might have a "threads" key whose value is a
  // list of all thread IDs in the current process, so m_thread_ids might get
  // set.
  // Check to see if SetThreadStopInfo() filled in m_thread_ids?
  if (m_thread_ids.empty()) {
      // No, we need to fetch the thread list manually
      UpdateThreadIDList();
  }

  // We might set some stop info's so make sure the thread list is up to
  // date before we do that or we might overwrite what was computed here.
  UpdateThreadListIfNeeded();

  if (m_last_stop_packet)
    SetThreadStopInfo(*m_last_stop_packet);
  m_last_stop_packet.reset();

  // If we have queried for a default thread id
  if (m_initial_tid != LLDB_INVALID_THREAD_ID) {
    m_thread_list.SetSelectedThreadByID(m_initial_tid);
    m_initial_tid = LLDB_INVALID_THREAD_ID;
  }

  // Let all threads recover from stopping and do any clean up based on the
  // previous thread state (if any).
  m_thread_list_real.RefreshStateAfterStop();
}

Status ProcessGDBRemote::DoHalt(bool &caused_stop) {
  Status error;

  if (m_public_state.GetValue() == eStateAttaching) {
    // We are being asked to halt during an attach. We used to just close our
    // file handle and debugserver will go away, but with remote proxies, it
    // is better to send a positive signal, so let's send the interrupt first...
    caused_stop = m_gdb_comm.Interrupt(GetInterruptTimeout());
    m_gdb_comm.Disconnect();
  } else
    caused_stop = m_gdb_comm.Interrupt(GetInterruptTimeout());
  return error;
}

Status ProcessGDBRemote::DoDetach(bool keep_stopped) {
  Status error;
  Log *log = GetLog(GDBRLog::Process);
  LLDB_LOGF(log, "ProcessGDBRemote::DoDetach(keep_stopped: %i)", keep_stopped);

  error = m_gdb_comm.Detach(keep_stopped);
  if (log) {
    if (error.Success())
      log->PutCString(
          "ProcessGDBRemote::DoDetach() detach packet sent successfully");
    else
      LLDB_LOGF(log,
                "ProcessGDBRemote::DoDetach() detach packet send failed: %s",
                error.AsCString() ? error.AsCString() : "<unknown error>");
  }

  if (!error.Success())
    return error;

  // Sleep for one second to let the process get all detached...
  StopAsyncThread();

  SetPrivateState(eStateDetached);
  ResumePrivateStateThread();

  // KillDebugserverProcess ();
  return error;
}

Status ProcessGDBRemote::DoDestroy() {
  Log *log = GetLog(GDBRLog::Process);
  LLDB_LOGF(log, "ProcessGDBRemote::DoDestroy()");

  // Interrupt if our inferior is running...
  int exit_status = SIGABRT;
  std::string exit_string;

  if (m_gdb_comm.IsConnected()) {
    if (m_public_state.GetValue() != eStateAttaching) {
      llvm::Expected<int> kill_res = m_gdb_comm.KillProcess(GetID());

      if (kill_res) {
        exit_status = kill_res.get();
#if defined(__APPLE__)
        // For Native processes on Mac OS X, we launch through the Host
        // Platform, then hand the process off to debugserver, which becomes
        // the parent process through "PT_ATTACH".  Then when we go to kill
        // the process on Mac OS X we call ptrace(PT_KILL) to kill it, then
        // we call waitpid which returns with no error and the correct
        // status.  But amusingly enough that doesn't seem to actually reap
        // the process, but instead it is left around as a Zombie.  Probably
        // the kernel is in the process of switching ownership back to lldb
        // which was the original parent, and gets confused in the handoff.
        // Anyway, so call waitpid here to finally reap it.
        PlatformSP platform_sp(GetTarget().GetPlatform());
        if (platform_sp && platform_sp->IsHost()) {
          int status;
          ::pid_t reap_pid;
          reap_pid = waitpid(GetID(), &status, WNOHANG);
          LLDB_LOGF(log, "Reaped pid: %d, status: %d.\n", reap_pid, status);
        }
#endif
        ClearThreadIDList();
        exit_string.assign("killed");
      } else {
        exit_string.assign(llvm::toString(kill_res.takeError()));
      }
    } else {
      exit_string.assign("killed or interrupted while attaching.");
    }
  } else {
    // If we missed setting the exit status on the way out, do it here.
    // NB set exit status can be called multiple times, the first one sets the
    // status.
    exit_string.assign("destroying when not connected to debugserver");
  }

  SetExitStatus(exit_status, exit_string.c_str());

  StopAsyncThread();
  KillDebugserverProcess();
  return Status();
}

void ProcessGDBRemote::SetLastStopPacket(
    const StringExtractorGDBRemote &response) {
  const bool did_exec =
      response.GetStringRef().find(";reason:exec;") != std::string::npos;
  if (did_exec) {
    Log *log = GetLog(GDBRLog::Process);
    LLDB_LOGF(log, "ProcessGDBRemote::SetLastStopPacket () - detected exec");

    m_thread_list_real.Clear();
    m_thread_list.Clear();
    BuildDynamicRegisterInfo(true);
    m_gdb_comm.ResetDiscoverableSettings(did_exec);
  }

  m_last_stop_packet = response;
}

void ProcessGDBRemote::SetUnixSignals(const UnixSignalsSP &signals_sp) {
  Process::SetUnixSignals(std::make_shared<GDBRemoteSignals>(signals_sp));
}

// Process Queries

bool ProcessGDBRemote::IsAlive() {
  return m_gdb_comm.IsConnected() && Process::IsAlive();
}

addr_t ProcessGDBRemote::GetImageInfoAddress() {
  // request the link map address via the $qShlibInfoAddr packet
  lldb::addr_t addr = m_gdb_comm.GetShlibInfoAddr();

  // the loaded module list can also provides a link map address
  if (addr == LLDB_INVALID_ADDRESS) {
    llvm::Expected<LoadedModuleInfoList> list = GetLoadedModuleList();
    if (!list) {
      Log *log = GetLog(GDBRLog::Process);
      LLDB_LOG_ERROR(log, list.takeError(), "Failed to read module list: {0}.");
    } else {
      addr = list->m_link_map;
    }
  }

  return addr;
}

void ProcessGDBRemote::WillPublicStop() {
  // See if the GDB remote client supports the JSON threads info. If so, we
  // gather stop info for all threads, expedited registers, expedited memory,
  // runtime queue information (iOS and MacOSX only), and more. Expediting
  // memory will help stack backtracing be much faster. Expediting registers
  // will make sure we don't have to read the thread registers for GPRs.
  m_jthreadsinfo_sp = m_gdb_comm.GetThreadsInfo();

  if (m_jthreadsinfo_sp) {
    // Now set the stop info for each thread and also expedite any registers
    // and memory that was in the jThreadsInfo response.
    StructuredData::Array *thread_infos = m_jthreadsinfo_sp->GetAsArray();
    if (thread_infos) {
      const size_t n = thread_infos->GetSize();
      for (size_t i = 0; i < n; ++i) {
        StructuredData::Dictionary *thread_dict =
            thread_infos->GetItemAtIndex(i)->GetAsDictionary();
        if (thread_dict)
          SetThreadStopInfo(thread_dict);
      }
    }
  }
}

// Process Memory
size_t ProcessGDBRemote::DoReadMemory(addr_t addr, void *buf, size_t size,
                                      Status &error) {
  GetMaxMemorySize();
  bool binary_memory_read = m_gdb_comm.GetxPacketSupported();
  // M and m packets take 2 bytes for 1 byte of memory
  size_t max_memory_size =
      binary_memory_read ? m_max_memory_size : m_max_memory_size / 2;
  if (size > max_memory_size) {
    // Keep memory read sizes down to a sane limit. This function will be
    // called multiple times in order to complete the task by
    // lldb_private::Process so it is ok to do this.
    size = max_memory_size;
  }

  char packet[64];
  int packet_len;
  packet_len = ::snprintf(packet, sizeof(packet), "%c%" PRIx64 ",%" PRIx64,
                          binary_memory_read ? 'x' : 'm', (uint64_t)addr,
                          (uint64_t)size);
  assert(packet_len + 1 < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);
  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet, response,
                                              GetInterruptTimeout()) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsNormalResponse()) {
      error.Clear();
      if (binary_memory_read) {
        // The lower level GDBRemoteCommunication packet receive layer has
        // already de-quoted any 0x7d character escaping that was present in
        // the packet

        size_t data_received_size = response.GetBytesLeft();
        if (data_received_size > size) {
          // Don't write past the end of BUF if the remote debug server gave us
          // too much data for some reason.
          data_received_size = size;
        }
        memcpy(buf, response.GetStringRef().data(), data_received_size);
        return data_received_size;
      } else {
        return response.GetHexBytes(
            llvm::MutableArrayRef<uint8_t>((uint8_t *)buf, size), '\xdd');
      }
    } else if (response.IsErrorResponse())
      error.SetErrorStringWithFormat("memory read failed for 0x%" PRIx64, addr);
    else if (response.IsUnsupportedResponse())
      error.SetErrorStringWithFormat(
          "GDB server does not support reading memory");
    else
      error.SetErrorStringWithFormat(
          "unexpected response to GDB server memory read packet '%s': '%s'",
          packet, response.GetStringRef().data());
  } else {
    error.SetErrorStringWithFormat("failed to send packet: '%s'", packet);
  }
  return 0;
}

bool ProcessGDBRemote::SupportsMemoryTagging() {
  return m_gdb_comm.GetMemoryTaggingSupported();
}

llvm::Expected<std::vector<uint8_t>>
ProcessGDBRemote::DoReadMemoryTags(lldb::addr_t addr, size_t len,
                                   int32_t type) {
  // By this point ReadMemoryTags has validated that tagging is enabled
  // for this target/process/address.
  DataBufferSP buffer_sp = m_gdb_comm.ReadMemoryTags(addr, len, type);
  if (!buffer_sp) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Error reading memory tags from remote");
  }

  // Return the raw tag data
  llvm::ArrayRef<uint8_t> tag_data = buffer_sp->GetData();
  std::vector<uint8_t> got;
  got.reserve(tag_data.size());
  std::copy(tag_data.begin(), tag_data.end(), std::back_inserter(got));
  return got;
}

Status ProcessGDBRemote::DoWriteMemoryTags(lldb::addr_t addr, size_t len,
                                           int32_t type,
                                           const std::vector<uint8_t> &tags) {
  // By now WriteMemoryTags should have validated that tagging is enabled
  // for this target/process.
  return m_gdb_comm.WriteMemoryTags(addr, len, type, tags);
}

Status ProcessGDBRemote::WriteObjectFile(
    std::vector<ObjectFile::LoadableData> entries) {
  Status error;
  // Sort the entries by address because some writes, like those to flash
  // memory, must happen in order of increasing address.
  std::stable_sort(
      std::begin(entries), std::end(entries),
      [](const ObjectFile::LoadableData a, const ObjectFile::LoadableData b) {
        return a.Dest < b.Dest;
      });
  m_allow_flash_writes = true;
  error = Process::WriteObjectFile(entries);
  if (error.Success())
    error = FlashDone();
  else
    // Even though some of the writing failed, try to send a flash done if some
    // of the writing succeeded so the flash state is reset to normal, but
    // don't stomp on the error status that was set in the write failure since
    // that's the one we want to report back.
    FlashDone();
  m_allow_flash_writes = false;
  return error;
}

bool ProcessGDBRemote::HasErased(FlashRange range) {
  auto size = m_erased_flash_ranges.GetSize();
  for (size_t i = 0; i < size; ++i)
    if (m_erased_flash_ranges.GetEntryAtIndex(i)->Contains(range))
      return true;
  return false;
}

Status ProcessGDBRemote::FlashErase(lldb::addr_t addr, size_t size) {
  Status status;

  MemoryRegionInfo region;
  status = GetMemoryRegionInfo(addr, region);
  if (!status.Success())
    return status;

  // The gdb spec doesn't say if erasures are allowed across multiple regions,
  // but we'll disallow it to be safe and to keep the logic simple by worring
  // about only one region's block size.  DoMemoryWrite is this function's
  // primary user, and it can easily keep writes within a single memory region
  if (addr + size > region.GetRange().GetRangeEnd()) {
    status.SetErrorString("Unable to erase flash in multiple regions");
    return status;
  }

  uint64_t blocksize = region.GetBlocksize();
  if (blocksize == 0) {
    status.SetErrorString("Unable to erase flash because blocksize is 0");
    return status;
  }

  // Erasures can only be done on block boundary adresses, so round down addr
  // and round up size
  lldb::addr_t block_start_addr = addr - (addr % blocksize);
  size += (addr - block_start_addr);
  if ((size % blocksize) != 0)
    size += (blocksize - size % blocksize);

  FlashRange range(block_start_addr, size);

  if (HasErased(range))
    return status;

  // We haven't erased the entire range, but we may have erased part of it.
  // (e.g., block A is already erased and range starts in A and ends in B). So,
  // adjust range if necessary to exclude already erased blocks.
  if (!m_erased_flash_ranges.IsEmpty()) {
    // Assuming that writes and erasures are done in increasing addr order,
    // because that is a requirement of the vFlashWrite command.  Therefore, we
    // only need to look at the last range in the list for overlap.
    const auto &last_range = *m_erased_flash_ranges.Back();
    if (range.GetRangeBase() < last_range.GetRangeEnd()) {
      auto overlap = last_range.GetRangeEnd() - range.GetRangeBase();
      // overlap will be less than range.GetByteSize() or else HasErased()
      // would have been true
      range.SetByteSize(range.GetByteSize() - overlap);
      range.SetRangeBase(range.GetRangeBase() + overlap);
    }
  }

  StreamString packet;
  packet.Printf("vFlashErase:%" PRIx64 ",%" PRIx64, range.GetRangeBase(),
                (uint64_t)range.GetByteSize());

  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                              GetInterruptTimeout()) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsOKResponse()) {
      m_erased_flash_ranges.Insert(range, true);
    } else {
      if (response.IsErrorResponse())
        status.SetErrorStringWithFormat("flash erase failed for 0x%" PRIx64,
                                        addr);
      else if (response.IsUnsupportedResponse())
        status.SetErrorStringWithFormat("GDB server does not support flashing");
      else
        status.SetErrorStringWithFormat(
            "unexpected response to GDB server flash erase packet '%s': '%s'",
            packet.GetData(), response.GetStringRef().data());
    }
  } else {
    status.SetErrorStringWithFormat("failed to send packet: '%s'",
                                    packet.GetData());
  }
  return status;
}

Status ProcessGDBRemote::FlashDone() {
  Status status;
  // If we haven't erased any blocks, then we must not have written anything
  // either, so there is no need to actually send a vFlashDone command
  if (m_erased_flash_ranges.IsEmpty())
    return status;
  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse("vFlashDone", response,
                                              GetInterruptTimeout()) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsOKResponse()) {
      m_erased_flash_ranges.Clear();
    } else {
      if (response.IsErrorResponse())
        status.SetErrorStringWithFormat("flash done failed");
      else if (response.IsUnsupportedResponse())
        status.SetErrorStringWithFormat("GDB server does not support flashing");
      else
        status.SetErrorStringWithFormat(
            "unexpected response to GDB server flash done packet: '%s'",
            response.GetStringRef().data());
    }
  } else {
    status.SetErrorStringWithFormat("failed to send flash done packet");
  }
  return status;
}

size_t ProcessGDBRemote::DoWriteMemory(addr_t addr, const void *buf,
                                       size_t size, Status &error) {
  GetMaxMemorySize();
  // M and m packets take 2 bytes for 1 byte of memory
  size_t max_memory_size = m_max_memory_size / 2;
  if (size > max_memory_size) {
    // Keep memory read sizes down to a sane limit. This function will be
    // called multiple times in order to complete the task by
    // lldb_private::Process so it is ok to do this.
    size = max_memory_size;
  }

  StreamGDBRemote packet;

  MemoryRegionInfo region;
  Status region_status = GetMemoryRegionInfo(addr, region);

  bool is_flash =
      region_status.Success() && region.GetFlash() == MemoryRegionInfo::eYes;

  if (is_flash) {
    if (!m_allow_flash_writes) {
      error.SetErrorString("Writing to flash memory is not allowed");
      return 0;
    }
    // Keep the write within a flash memory region
    if (addr + size > region.GetRange().GetRangeEnd())
      size = region.GetRange().GetRangeEnd() - addr;
    // Flash memory must be erased before it can be written
    error = FlashErase(addr, size);
    if (!error.Success())
      return 0;
    packet.Printf("vFlashWrite:%" PRIx64 ":", addr);
    packet.PutEscapedBytes(buf, size);
  } else {
    packet.Printf("M%" PRIx64 ",%" PRIx64 ":", addr, (uint64_t)size);
    packet.PutBytesAsRawHex8(buf, size, endian::InlHostByteOrder(),
                             endian::InlHostByteOrder());
  }
  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response,
                                              GetInterruptTimeout()) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsOKResponse()) {
      error.Clear();
      return size;
    } else if (response.IsErrorResponse())
      error.SetErrorStringWithFormat("memory write failed for 0x%" PRIx64,
                                     addr);
    else if (response.IsUnsupportedResponse())
      error.SetErrorStringWithFormat(
          "GDB server does not support writing memory");
    else
      error.SetErrorStringWithFormat(
          "unexpected response to GDB server memory write packet '%s': '%s'",
          packet.GetData(), response.GetStringRef().data());
  } else {
    error.SetErrorStringWithFormat("failed to send packet: '%s'",
                                   packet.GetData());
  }
  return 0;
}

lldb::addr_t ProcessGDBRemote::DoAllocateMemory(size_t size,
                                                uint32_t permissions,
                                                Status &error) {
  Log *log = GetLog(LLDBLog::Process | LLDBLog::Expressions);
  addr_t allocated_addr = LLDB_INVALID_ADDRESS;

  if (m_gdb_comm.SupportsAllocDeallocMemory() != eLazyBoolNo) {
    allocated_addr = m_gdb_comm.AllocateMemory(size, permissions);
    if (allocated_addr != LLDB_INVALID_ADDRESS ||
        m_gdb_comm.SupportsAllocDeallocMemory() == eLazyBoolYes)
      return allocated_addr;
  }

  if (m_gdb_comm.SupportsAllocDeallocMemory() == eLazyBoolNo) {
    // Call mmap() to create memory in the inferior..
    unsigned prot = 0;
    if (permissions & lldb::ePermissionsReadable)
      prot |= eMmapProtRead;
    if (permissions & lldb::ePermissionsWritable)
      prot |= eMmapProtWrite;
    if (permissions & lldb::ePermissionsExecutable)
      prot |= eMmapProtExec;

    if (InferiorCallMmap(this, allocated_addr, 0, size, prot,
                         eMmapFlagsAnon | eMmapFlagsPrivate, -1, 0))
      m_addr_to_mmap_size[allocated_addr] = size;
    else {
      allocated_addr = LLDB_INVALID_ADDRESS;
      LLDB_LOGF(log,
                "ProcessGDBRemote::%s no direct stub support for memory "
                "allocation, and InferiorCallMmap also failed - is stub "
                "missing register context save/restore capability?",
                __FUNCTION__);
    }
  }

  if (allocated_addr == LLDB_INVALID_ADDRESS)
    error.SetErrorStringWithFormat(
        "unable to allocate %" PRIu64 " bytes of memory with permissions %s",
        (uint64_t)size, GetPermissionsAsCString(permissions));
  else
    error.Clear();
  return allocated_addr;
}

Status ProcessGDBRemote::DoGetMemoryRegionInfo(addr_t load_addr,
                                               MemoryRegionInfo &region_info) {

  Status error(m_gdb_comm.GetMemoryRegionInfo(load_addr, region_info));
  return error;
}

std::optional<uint32_t> ProcessGDBRemote::GetWatchpointSlotCount() {
  return m_gdb_comm.GetWatchpointSlotCount();
}

std::optional<bool> ProcessGDBRemote::DoGetWatchpointReportedAfter() {
  return m_gdb_comm.GetWatchpointReportedAfter();
}

Status ProcessGDBRemote::DoDeallocateMemory(lldb::addr_t addr) {
  Status error;
  LazyBool supported = m_gdb_comm.SupportsAllocDeallocMemory();

  switch (supported) {
  case eLazyBoolCalculate:
    // We should never be deallocating memory without allocating memory first
    // so we should never get eLazyBoolCalculate
    error.SetErrorString(
        "tried to deallocate memory without ever allocating memory");
    break;

  case eLazyBoolYes:
    if (!m_gdb_comm.DeallocateMemory(addr))
      error.SetErrorStringWithFormat(
          "unable to deallocate memory at 0x%" PRIx64, addr);
    break;

  case eLazyBoolNo:
    // Call munmap() to deallocate memory in the inferior..
    {
      MMapMap::iterator pos = m_addr_to_mmap_size.find(addr);
      if (pos != m_addr_to_mmap_size.end() &&
          InferiorCallMunmap(this, addr, pos->second))
        m_addr_to_mmap_size.erase(pos);
      else
        error.SetErrorStringWithFormat(
            "unable to deallocate memory at 0x%" PRIx64, addr);
    }
    break;
  }

  return error;
}

// Process STDIO
size_t ProcessGDBRemote::PutSTDIN(const char *src, size_t src_len,
                                  Status &error) {
  if (m_stdio_communication.IsConnected()) {
    ConnectionStatus status;
    m_stdio_communication.WriteAll(src, src_len, status, nullptr);
  } else if (m_stdin_forward) {
    m_gdb_comm.SendStdinNotification(src, src_len);
  }
  return 0;
}

Status ProcessGDBRemote::EnableBreakpointSite(BreakpointSite *bp_site) {
  Status error;
  assert(bp_site != nullptr);

  // Get logging info
  Log *log = GetLog(GDBRLog::Breakpoints);
  user_id_t site_id = bp_site->GetID();

  // Get the breakpoint address
  const addr_t addr = bp_site->GetLoadAddress();

  // Log that a breakpoint was requested
  LLDB_LOGF(log,
            "ProcessGDBRemote::EnableBreakpointSite (size_id = %" PRIu64
            ") address = 0x%" PRIx64,
            site_id, (uint64_t)addr);

  // Breakpoint already exists and is enabled
  if (bp_site->IsEnabled()) {
    LLDB_LOGF(log,
              "ProcessGDBRemote::EnableBreakpointSite (size_id = %" PRIu64
              ") address = 0x%" PRIx64 " -- SUCCESS (already enabled)",
              site_id, (uint64_t)addr);
    return error;
  }

  // Get the software breakpoint trap opcode size
  const size_t bp_op_size = GetSoftwareBreakpointTrapOpcode(bp_site);

  // SupportsGDBStoppointPacket() simply checks a boolean, indicating if this
  // breakpoint type is supported by the remote stub. These are set to true by
  // default, and later set to false only after we receive an unimplemented
  // response when sending a breakpoint packet. This means initially that
  // unless we were specifically instructed to use a hardware breakpoint, LLDB
  // will attempt to set a software breakpoint. HardwareRequired() also queries
  // a boolean variable which indicates if the user specifically asked for
  // hardware breakpoints.  If true then we will skip over software
  // breakpoints.
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointSoftware) &&
      (!bp_site->HardwareRequired())) {
    // Try to send off a software breakpoint packet ($Z0)
    uint8_t error_no = m_gdb_comm.SendGDBStoppointTypePacket(
        eBreakpointSoftware, true, addr, bp_op_size, GetInterruptTimeout());
    if (error_no == 0) {
      // The breakpoint was placed successfully
      bp_site->SetEnabled(true);
      bp_site->SetType(BreakpointSite::eExternal);
      return error;
    }

    // SendGDBStoppointTypePacket() will return an error if it was unable to
    // set this breakpoint. We need to differentiate between a error specific
    // to placing this breakpoint or if we have learned that this breakpoint
    // type is unsupported. To do this, we must test the support boolean for
    // this breakpoint type to see if it now indicates that this breakpoint
    // type is unsupported.  If they are still supported then we should return
    // with the error code.  If they are now unsupported, then we would like to
    // fall through and try another form of breakpoint.
    if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointSoftware)) {
      if (error_no != UINT8_MAX)
        error.SetErrorStringWithFormat(
            "error: %d sending the breakpoint request", error_no);
      else
        error.SetErrorString("error sending the breakpoint request");
      return error;
    }

    // We reach here when software breakpoints have been found to be
    // unsupported. For future calls to set a breakpoint, we will not attempt
    // to set a breakpoint with a type that is known not to be supported.
    LLDB_LOGF(log, "Software breakpoints are unsupported");

    // So we will fall through and try a hardware breakpoint
  }

  // The process of setting a hardware breakpoint is much the same as above.
  // We check the supported boolean for this breakpoint type, and if it is
  // thought to be supported then we will try to set this breakpoint with a
  // hardware breakpoint.
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointHardware)) {
    // Try to send off a hardware breakpoint packet ($Z1)
    uint8_t error_no = m_gdb_comm.SendGDBStoppointTypePacket(
        eBreakpointHardware, true, addr, bp_op_size, GetInterruptTimeout());
    if (error_no == 0) {
      // The breakpoint was placed successfully
      bp_site->SetEnabled(true);
      bp_site->SetType(BreakpointSite::eHardware);
      return error;
    }

    // Check if the error was something other then an unsupported breakpoint
    // type
    if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointHardware)) {
      // Unable to set this hardware breakpoint
      if (error_no != UINT8_MAX)
        error.SetErrorStringWithFormat(
            "error: %d sending the hardware breakpoint request "
            "(hardware breakpoint resources might be exhausted or unavailable)",
            error_no);
      else
        error.SetErrorString("error sending the hardware breakpoint request "
                             "(hardware breakpoint resources "
                             "might be exhausted or unavailable)");
      return error;
    }

    // We will reach here when the stub gives an unsupported response to a
    // hardware breakpoint
    LLDB_LOGF(log, "Hardware breakpoints are unsupported");

    // Finally we will falling through to a #trap style breakpoint
  }

  // Don't fall through when hardware breakpoints were specifically requested
  if (bp_site->HardwareRequired()) {
    error.SetErrorString("hardware breakpoints are not supported");
    return error;
  }

  // As a last resort we want to place a manual breakpoint. An instruction is
  // placed into the process memory using memory write packets.
  return EnableSoftwareBreakpoint(bp_site);
}

Status ProcessGDBRemote::DisableBreakpointSite(BreakpointSite *bp_site) {
  Status error;
  assert(bp_site != nullptr);
  addr_t addr = bp_site->GetLoadAddress();
  user_id_t site_id = bp_site->GetID();
  Log *log = GetLog(GDBRLog::Breakpoints);
  LLDB_LOGF(log,
            "ProcessGDBRemote::DisableBreakpointSite (site_id = %" PRIu64
            ") addr = 0x%8.8" PRIx64,
            site_id, (uint64_t)addr);

  if (bp_site->IsEnabled()) {
    const size_t bp_op_size = GetSoftwareBreakpointTrapOpcode(bp_site);

    BreakpointSite::Type bp_type = bp_site->GetType();
    switch (bp_type) {
    case BreakpointSite::eSoftware:
      error = DisableSoftwareBreakpoint(bp_site);
      break;

    case BreakpointSite::eHardware:
      if (m_gdb_comm.SendGDBStoppointTypePacket(eBreakpointHardware, false,
                                                addr, bp_op_size,
                                                GetInterruptTimeout()))
        error.SetErrorToGenericError();
      break;

    case BreakpointSite::eExternal: {
      if (m_gdb_comm.SendGDBStoppointTypePacket(eBreakpointSoftware, false,
                                                addr, bp_op_size,
                                                GetInterruptTimeout()))
        error.SetErrorToGenericError();
    } break;
    }
    if (error.Success())
      bp_site->SetEnabled(false);
  } else {
    LLDB_LOGF(log,
              "ProcessGDBRemote::DisableBreakpointSite (site_id = %" PRIu64
              ") addr = 0x%8.8" PRIx64 " -- SUCCESS (already disabled)",
              site_id, (uint64_t)addr);
    return error;
  }

  if (error.Success())
    error.SetErrorToGenericError();
  return error;
}

// Pre-requisite: wp != NULL.
static GDBStoppointType
GetGDBStoppointType(const WatchpointResourceSP &wp_res_sp) {
  assert(wp_res_sp);
  bool read = wp_res_sp->WatchpointResourceRead();
  bool write = wp_res_sp->WatchpointResourceWrite();

  assert((read || write) &&
         "WatchpointResource type is neither read nor write");
  if (read && write)
    return eWatchpointReadWrite;
  else if (read)
    return eWatchpointRead;
  else
    return eWatchpointWrite;
}

Status ProcessGDBRemote::EnableWatchpoint(WatchpointSP wp_sp, bool notify) {
  Status error;
  if (!wp_sp) {
    error.SetErrorString("No watchpoint specified");
    return error;
  }
  user_id_t watchID = wp_sp->GetID();
  addr_t addr = wp_sp->GetLoadAddress();
  Log *log(GetLog(GDBRLog::Watchpoints));
  LLDB_LOGF(log, "ProcessGDBRemote::EnableWatchpoint(watchID = %" PRIu64 ")",
            watchID);
  if (wp_sp->IsEnabled()) {
    LLDB_LOGF(log,
              "ProcessGDBRemote::EnableWatchpoint(watchID = %" PRIu64
              ") addr = 0x%8.8" PRIx64 ": watchpoint already enabled.",
              watchID, (uint64_t)addr);
    return error;
  }

  bool read = wp_sp->WatchpointRead();
  bool write = wp_sp->WatchpointWrite() || wp_sp->WatchpointModify();
  size_t size = wp_sp->GetByteSize();

  ArchSpec target_arch = GetTarget().GetArchitecture();
  WatchpointHardwareFeature supported_features =
      m_gdb_comm.GetSupportedWatchpointTypes();

  std::vector<WatchpointResourceSP> resources =
      WatchpointAlgorithms::AtomizeWatchpointRequest(
          addr, size, read, write, supported_features, target_arch);

  // LWP_TODO: Now that we know the WP Resources needed to implement this
  // Watchpoint, we need to look at currently allocated Resources in the
  // Process and if they match, or are within the same memory granule, or
  // overlapping memory ranges, then we need to combine them.  e.g. one
  // Watchpoint watching 1 byte at 0x1002 and a second watchpoint watching 1
  // byte at 0x1003, they must use the same hardware watchpoint register
  // (Resource) to watch them.

  // This may mean that an existing resource changes its type (read to
  // read+write) or address range it is watching, in which case the old
  // watchpoint needs to be disabled and the new Resource addr/size/type
  // watchpoint enabled.

  // If we modify a shared Resource to accomodate this newly added Watchpoint,
  // and we are unable to set all of the Resources for it in the inferior, we
  // will return an error for this Watchpoint and the shared Resource should
  // be restored.  e.g. this Watchpoint requires three Resources, one which
  // is shared with another Watchpoint.  We extend the shared Resouce to
  // handle both Watchpoints and we try to set two new ones.  But if we don't
  // have sufficient watchpoint register for all 3, we need to show an error
  // for creating this Watchpoint and we should reset the shared Resource to
  // its original configuration because it is no longer shared.

  bool set_all_resources = true;
  std::vector<WatchpointResourceSP> succesfully_set_resources;
  for (const auto &wp_res_sp : resources) {
    addr_t addr = wp_res_sp->GetLoadAddress();
    size_t size = wp_res_sp->GetByteSize();
    GDBStoppointType type = GetGDBStoppointType(wp_res_sp);
    if (!m_gdb_comm.SupportsGDBStoppointPacket(type) ||
        m_gdb_comm.SendGDBStoppointTypePacket(type, true, addr, size,
                                              GetInterruptTimeout())) {
      set_all_resources = false;
      break;
    } else {
      succesfully_set_resources.push_back(wp_res_sp);
    }
  }
  if (set_all_resources) {
    wp_sp->SetEnabled(true, notify);
    for (const auto &wp_res_sp : resources) {
      // LWP_TODO: If we expanded/reused an existing Resource,
      // it's already in the WatchpointResourceList.
      wp_res_sp->AddConstituent(wp_sp);
      m_watchpoint_resource_list.Add(wp_res_sp);
    }
    return error;
  } else {
    // We failed to allocate one of the resources.  Unset all
    // of the new resources we did successfully set in the
    // process.
    for (const auto &wp_res_sp : succesfully_set_resources) {
      addr_t addr = wp_res_sp->GetLoadAddress();
      size_t size = wp_res_sp->GetByteSize();
      GDBStoppointType type = GetGDBStoppointType(wp_res_sp);
      m_gdb_comm.SendGDBStoppointTypePacket(type, false, addr, size,
                                            GetInterruptTimeout());
    }
    error.SetErrorString("Setting one of the watchpoint resources failed");
  }
  return error;
}

Status ProcessGDBRemote::DisableWatchpoint(WatchpointSP wp_sp, bool notify) {
  Status error;
  if (!wp_sp) {
    error.SetErrorString("Watchpoint argument was NULL.");
    return error;
  }

  user_id_t watchID = wp_sp->GetID();

  Log *log(GetLog(GDBRLog::Watchpoints));

  addr_t addr = wp_sp->GetLoadAddress();

  LLDB_LOGF(log,
            "ProcessGDBRemote::DisableWatchpoint (watchID = %" PRIu64
            ") addr = 0x%8.8" PRIx64,
            watchID, (uint64_t)addr);

  if (!wp_sp->IsEnabled()) {
    LLDB_LOGF(log,
              "ProcessGDBRemote::DisableWatchpoint (watchID = %" PRIu64
              ") addr = 0x%8.8" PRIx64 " -- SUCCESS (already disabled)",
              watchID, (uint64_t)addr);
    // See also 'class WatchpointSentry' within StopInfo.cpp. This disabling
    // attempt might come from the user-supplied actions, we'll route it in
    // order for the watchpoint object to intelligently process this action.
    wp_sp->SetEnabled(false, notify);
    return error;
  }

  if (wp_sp->IsHardware()) {
    bool disabled_all = true;

    std::vector<WatchpointResourceSP> unused_resources;
    for (const auto &wp_res_sp : m_watchpoint_resource_list.Sites()) {
      if (wp_res_sp->ConstituentsContains(wp_sp)) {
        GDBStoppointType type = GetGDBStoppointType(wp_res_sp);
        addr_t addr = wp_res_sp->GetLoadAddress();
        size_t size = wp_res_sp->GetByteSize();
        if (m_gdb_comm.SendGDBStoppointTypePacket(type, false, addr, size,
                                                  GetInterruptTimeout())) {
          disabled_all = false;
        } else {
          wp_res_sp->RemoveConstituent(wp_sp);
          if (wp_res_sp->GetNumberOfConstituents() == 0)
            unused_resources.push_back(wp_res_sp);
        }
      }
    }
    for (auto &wp_res_sp : unused_resources)
      m_watchpoint_resource_list.Remove(wp_res_sp->GetID());

    wp_sp->SetEnabled(false, notify);
    if (!disabled_all)
      error.SetErrorString("Failure disabling one of the watchpoint locations");
  }
  return error;
}

void ProcessGDBRemote::Clear() {
  m_thread_list_real.Clear();
  m_thread_list.Clear();
}

Status ProcessGDBRemote::DoSignal(int signo) {
  Status error;
  Log *log = GetLog(GDBRLog::Process);
  LLDB_LOGF(log, "ProcessGDBRemote::DoSignal (signal = %d)", signo);

  if (!m_gdb_comm.SendAsyncSignal(signo, GetInterruptTimeout()))
    error.SetErrorStringWithFormat("failed to send signal %i", signo);
  return error;
}

Status
ProcessGDBRemote::EstablishConnectionIfNeeded(const ProcessInfo &process_info) {
  // Make sure we aren't already connected?
  if (m_gdb_comm.IsConnected())
    return Status();

  PlatformSP platform_sp(GetTarget().GetPlatform());
  if (platform_sp && !platform_sp->IsHost())
    return Status("Lost debug server connection");

  auto error = LaunchAndConnectToDebugserver(process_info);
  if (error.Fail()) {
    const char *error_string = error.AsCString();
    if (error_string == nullptr)
      error_string = "unable to launch " DEBUGSERVER_BASENAME;
  }
  return error;
}
#if !defined(_WIN32)
#define USE_SOCKETPAIR_FOR_LOCAL_CONNECTION 1
#endif

#ifdef USE_SOCKETPAIR_FOR_LOCAL_CONNECTION
static bool SetCloexecFlag(int fd) {
#if defined(FD_CLOEXEC)
  int flags = ::fcntl(fd, F_GETFD);
  if (flags == -1)
    return false;
  return (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0);
#else
  return false;
#endif
}
#endif

Status ProcessGDBRemote::LaunchAndConnectToDebugserver(
    const ProcessInfo &process_info) {
  using namespace std::placeholders; // For _1, _2, etc.

  Status error;
  if (m_debugserver_pid == LLDB_INVALID_PROCESS_ID) {
    // If we locate debugserver, keep that located version around
    static FileSpec g_debugserver_file_spec;

    ProcessLaunchInfo debugserver_launch_info;
    // Make debugserver run in its own session so signals generated by special
    // terminal key sequences (^C) don't affect debugserver.
    debugserver_launch_info.SetLaunchInSeparateProcessGroup(true);

    const std::weak_ptr<ProcessGDBRemote> this_wp =
        std::static_pointer_cast<ProcessGDBRemote>(shared_from_this());
    debugserver_launch_info.SetMonitorProcessCallback(
        std::bind(MonitorDebugserverProcess, this_wp, _1, _2, _3));
    debugserver_launch_info.SetUserID(process_info.GetUserID());

#if defined(__APPLE__)
    // On macOS 11, we need to support x86_64 applications translated to
    // arm64. We check whether a binary is translated and spawn the correct
    // debugserver accordingly.
    int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID,
                  static_cast<int>(process_info.GetProcessID()) };
    struct kinfo_proc processInfo;
    size_t bufsize = sizeof(processInfo);
    if (sysctl(mib, (unsigned)(sizeof(mib)/sizeof(int)), &processInfo,
               &bufsize, NULL, 0) == 0 && bufsize > 0) {
      if (processInfo.kp_proc.p_flag & P_TRANSLATED) {
        FileSpec rosetta_debugserver("/Library/Apple/usr/libexec/oah/debugserver");
        debugserver_launch_info.SetExecutableFile(rosetta_debugserver, false);
      }
    }
#endif

    int communication_fd = -1;
#ifdef USE_SOCKETPAIR_FOR_LOCAL_CONNECTION
    // Use a socketpair on non-Windows systems for security and performance
    // reasons.
    int sockets[2]; /* the pair of socket descriptors */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
      error.SetErrorToErrno();
      return error;
    }

    int our_socket = sockets[0];
    int gdb_socket = sockets[1];
    auto cleanup_our = llvm::make_scope_exit([&]() { close(our_socket); });
    auto cleanup_gdb = llvm::make_scope_exit([&]() { close(gdb_socket); });

    // Don't let any child processes inherit our communication socket
    SetCloexecFlag(our_socket);
    communication_fd = gdb_socket;
#endif

    error = m_gdb_comm.StartDebugserverProcess(
        nullptr, GetTarget().GetPlatform().get(), debugserver_launch_info,
        nullptr, nullptr, communication_fd);

    if (error.Success())
      m_debugserver_pid = debugserver_launch_info.GetProcessID();
    else
      m_debugserver_pid = LLDB_INVALID_PROCESS_ID;

    if (m_debugserver_pid != LLDB_INVALID_PROCESS_ID) {
#ifdef USE_SOCKETPAIR_FOR_LOCAL_CONNECTION
      // Our process spawned correctly, we can now set our connection to use
      // our end of the socket pair
      cleanup_our.release();
      m_gdb_comm.SetConnection(
          std::make_unique<ConnectionFileDescriptor>(our_socket, true));
#endif
      StartAsyncThread();
    }

    if (error.Fail()) {
      Log *log = GetLog(GDBRLog::Process);

      LLDB_LOGF(log, "failed to start debugserver process: %s",
                error.AsCString());
      return error;
    }

    if (m_gdb_comm.IsConnected()) {
      // Finish the connection process by doing the handshake without
      // connecting (send NULL URL)
      error = ConnectToDebugserver("");
    } else {
      error.SetErrorString("connection failed");
    }
  }
  return error;
}

void ProcessGDBRemote::MonitorDebugserverProcess(
    std::weak_ptr<ProcessGDBRemote> process_wp, lldb::pid_t debugserver_pid,
    int signo,      // Zero for no signal
    int exit_status // Exit value of process if signal is zero
) {
  // "debugserver_pid" argument passed in is the process ID for debugserver
  // that we are tracking...
  Log *log = GetLog(GDBRLog::Process);

  LLDB_LOGF(log,
            "ProcessGDBRemote::%s(process_wp, pid=%" PRIu64
            ", signo=%i (0x%x), exit_status=%i)",
            __FUNCTION__, debugserver_pid, signo, signo, exit_status);

  std::shared_ptr<ProcessGDBRemote> process_sp = process_wp.lock();
  LLDB_LOGF(log, "ProcessGDBRemote::%s(process = %p)", __FUNCTION__,
            static_cast<void *>(process_sp.get()));
  if (!process_sp || process_sp->m_debugserver_pid != debugserver_pid)
    return;

  // Sleep for a half a second to make sure our inferior process has time to
  // set its exit status before we set it incorrectly when both the debugserver
  // and the inferior process shut down.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // If our process hasn't yet exited, debugserver might have died. If the
  // process did exit, then we are reaping it.
  const StateType state = process_sp->GetState();

  if (state != eStateInvalid && state != eStateUnloaded &&
      state != eStateExited && state != eStateDetached) {
    StreamString stream;
    if (signo == 0)
      stream.Format(DEBUGSERVER_BASENAME " died with an exit status of {0:x8}",
                    exit_status);
    else {
      llvm::StringRef signal_name =
          process_sp->GetUnixSignals()->GetSignalAsStringRef(signo);
      const char *format_str = DEBUGSERVER_BASENAME " died with signal {0}";
      if (!signal_name.empty())
        stream.Format(format_str, signal_name);
      else
        stream.Format(format_str, signo);
    }
    process_sp->SetExitStatus(-1, stream.GetString());
  }
  // Debugserver has exited we need to let our ProcessGDBRemote know that it no
  // longer has a debugserver instance
  process_sp->m_debugserver_pid = LLDB_INVALID_PROCESS_ID;
}

void ProcessGDBRemote::KillDebugserverProcess() {
  m_gdb_comm.Disconnect();
  if (m_debugserver_pid != LLDB_INVALID_PROCESS_ID) {
    Host::Kill(m_debugserver_pid, SIGINT);
    m_debugserver_pid = LLDB_INVALID_PROCESS_ID;
  }
}

void ProcessGDBRemote::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance,
                                  DebuggerInitialize);
  });
}

void ProcessGDBRemote::DebuggerInitialize(Debugger &debugger) {
  if (!PluginManager::GetSettingForProcessPlugin(
          debugger, PluginProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForProcessPlugin(
        debugger, GetGlobalPluginProperties().GetValueProperties(),
        "Properties for the gdb-remote process plug-in.", is_global_setting);
  }
}

bool ProcessGDBRemote::StartAsyncThread() {
  Log *log = GetLog(GDBRLog::Process);

  LLDB_LOGF(log, "ProcessGDBRemote::%s ()", __FUNCTION__);

  std::lock_guard<std::recursive_mutex> guard(m_async_thread_state_mutex);
  if (!m_async_thread.IsJoinable()) {
    // Create a thread that watches our internal state and controls which
    // events make it to clients (into the DCProcess event queue).

    llvm::Expected<HostThread> async_thread =
        ThreadLauncher::LaunchThread("<lldb.process.gdb-remote.async>", [this] {
          return ProcessGDBRemote::AsyncThread();
        });
    if (!async_thread) {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Host), async_thread.takeError(),
                     "failed to launch host thread: {0}");
      return false;
    }
    m_async_thread = *async_thread;
  } else
    LLDB_LOGF(log,
              "ProcessGDBRemote::%s () - Called when Async thread was "
              "already running.",
              __FUNCTION__);

  return m_async_thread.IsJoinable();
}

void ProcessGDBRemote::StopAsyncThread() {
  Log *log = GetLog(GDBRLog::Process);

  LLDB_LOGF(log, "ProcessGDBRemote::%s ()", __FUNCTION__);

  std::lock_guard<std::recursive_mutex> guard(m_async_thread_state_mutex);
  if (m_async_thread.IsJoinable()) {
    m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncThreadShouldExit);

    //  This will shut down the async thread.
    m_gdb_comm.Disconnect(); // Disconnect from the debug server.

    // Stop the stdio thread
    m_async_thread.Join(nullptr);
    m_async_thread.Reset();
  } else
    LLDB_LOGF(
        log,
        "ProcessGDBRemote::%s () - Called when Async thread was not running.",
        __FUNCTION__);
}

thread_result_t ProcessGDBRemote::AsyncThread() {
  Log *log = GetLog(GDBRLog::Process);
  LLDB_LOGF(log, "ProcessGDBRemote::%s(pid = %" PRIu64 ") thread starting...",
            __FUNCTION__, GetID());

  EventSP event_sp;

  // We need to ignore any packets that come in after we have
  // have decided the process has exited.  There are some
  // situations, for instance when we try to interrupt a running
  // process and the interrupt fails, where another packet might
  // get delivered after we've decided to give up on the process.
  // But once we've decided we are done with the process we will
  // not be in a state to do anything useful with new packets.
  // So it is safer to simply ignore any remaining packets by
  // explicitly checking for eStateExited before reentering the
  // fetch loop.

  bool done = false;
  while (!done && GetPrivateState() != eStateExited) {
    LLDB_LOGF(log,
              "ProcessGDBRemote::%s(pid = %" PRIu64
              ") listener.WaitForEvent (NULL, event_sp)...",
              __FUNCTION__, GetID());

    if (m_async_listener_sp->GetEvent(event_sp, std::nullopt)) {
      const uint32_t event_type = event_sp->GetType();
      if (event_sp->BroadcasterIs(&m_async_broadcaster)) {
        LLDB_LOGF(log,
                  "ProcessGDBRemote::%s(pid = %" PRIu64
                  ") Got an event of type: %d...",
                  __FUNCTION__, GetID(), event_type);

        switch (event_type) {
        case eBroadcastBitAsyncContinue: {
          const EventDataBytes *continue_packet =
              EventDataBytes::GetEventDataFromEvent(event_sp.get());

          if (continue_packet) {
            const char *continue_cstr =
                (const char *)continue_packet->GetBytes();
            const size_t continue_cstr_len = continue_packet->GetByteSize();
            LLDB_LOGF(log,
                      "ProcessGDBRemote::%s(pid = %" PRIu64
                      ") got eBroadcastBitAsyncContinue: %s",
                      __FUNCTION__, GetID(), continue_cstr);

            if (::strstr(continue_cstr, "vAttach") == nullptr)
              SetPrivateState(eStateRunning);
            StringExtractorGDBRemote response;

            StateType stop_state =
                GetGDBRemote().SendContinuePacketAndWaitForResponse(
                    *this, *GetUnixSignals(),
                    llvm::StringRef(continue_cstr, continue_cstr_len),
                    GetInterruptTimeout(), response);

            // We need to immediately clear the thread ID list so we are sure
            // to get a valid list of threads. The thread ID list might be
            // contained within the "response", or the stop reply packet that
            // caused the stop. So clear it now before we give the stop reply
            // packet to the process using the
            // SetLastStopPacket()...
            ClearThreadIDList();

            switch (stop_state) {
            case eStateStopped:
            case eStateCrashed:
            case eStateSuspended:
              SetLastStopPacket(response);
              SetPrivateState(stop_state);
              break;

            case eStateExited: {
              SetLastStopPacket(response);
              ClearThreadIDList();
              response.SetFilePos(1);

              int exit_status = response.GetHexU8();
              std::string desc_string;
              if (response.GetBytesLeft() > 0 && response.GetChar('-') == ';') {
                llvm::StringRef desc_str;
                llvm::StringRef desc_token;
                while (response.GetNameColonValue(desc_token, desc_str)) {
                  if (desc_token != "description")
                    continue;
                  StringExtractor extractor(desc_str);
                  extractor.GetHexByteString(desc_string);
                }
              }
              SetExitStatus(exit_status, desc_string.c_str());
              done = true;
              break;
            }
            case eStateInvalid: {
              // Check to see if we were trying to attach and if we got back
              // the "E87" error code from debugserver -- this indicates that
              // the process is not debuggable.  Return a slightly more
              // helpful error message about why the attach failed.
              if (::strstr(continue_cstr, "vAttach") != nullptr &&
                  response.GetError() == 0x87) {
                SetExitStatus(-1, "cannot attach to process due to "
                                  "System Integrity Protection");
              } else if (::strstr(continue_cstr, "vAttach") != nullptr &&
                         response.GetStatus().Fail()) {
                SetExitStatus(-1, response.GetStatus().AsCString());
              } else {
                SetExitStatus(-1, "lost connection");
              }
              done = true;
              break;
            }

            default:
              SetPrivateState(stop_state);
              break;
            }   // switch(stop_state)
          }     // if (continue_packet)
        }       // case eBroadcastBitAsyncContinue
        break;

        case eBroadcastBitAsyncThreadShouldExit:
          LLDB_LOGF(log,
                    "ProcessGDBRemote::%s(pid = %" PRIu64
                    ") got eBroadcastBitAsyncThreadShouldExit...",
                    __FUNCTION__, GetID());
          done = true;
          break;

        default:
          LLDB_LOGF(log,
                    "ProcessGDBRemote::%s(pid = %" PRIu64
                    ") got unknown event 0x%8.8x",
                    __FUNCTION__, GetID(), event_type);
          done = true;
          break;
        }
      }
    } else {
      LLDB_LOGF(log,
                "ProcessGDBRemote::%s(pid = %" PRIu64
                ") listener.WaitForEvent (NULL, event_sp) => false",
                __FUNCTION__, GetID());
      done = true;
    }
  }

  LLDB_LOGF(log, "ProcessGDBRemote::%s(pid = %" PRIu64 ") thread exiting...",
            __FUNCTION__, GetID());

  return {};
}

// uint32_t
// ProcessGDBRemote::ListProcessesMatchingName (const char *name, StringList
// &matches, std::vector<lldb::pid_t> &pids)
//{
//    // If we are planning to launch the debugserver remotely, then we need to
//    fire up a debugserver
//    // process and ask it for the list of processes. But if we are local, we
//    can let the Host do it.
//    if (m_local_debugserver)
//    {
//        return Host::ListProcessesMatchingName (name, matches, pids);
//    }
//    else
//    {
//        // FIXME: Implement talking to the remote debugserver.
//        return 0;
//    }
//
//}
//
bool ProcessGDBRemote::NewThreadNotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, lldb::user_id_t break_id,
    lldb::user_id_t break_loc_id) {
  // I don't think I have to do anything here, just make sure I notice the new
  // thread when it starts to
  // run so I can stop it if that's what I want to do.
  Log *log = GetLog(LLDBLog::Step);
  LLDB_LOGF(log, "Hit New Thread Notification breakpoint.");
  return false;
}

Status ProcessGDBRemote::UpdateAutomaticSignalFiltering() {
  Log *log = GetLog(GDBRLog::Process);
  LLDB_LOG(log, "Check if need to update ignored signals");

  // QPassSignals package is not supported by the server, there is no way we
  // can ignore any signals on server side.
  if (!m_gdb_comm.GetQPassSignalsSupported())
    return Status();

  // No signals, nothing to send.
  if (m_unix_signals_sp == nullptr)
    return Status();

  // Signals' version hasn't changed, no need to send anything.
  uint64_t new_signals_version = m_unix_signals_sp->GetVersion();
  if (new_signals_version == m_last_signals_version) {
    LLDB_LOG(log, "Signals' version hasn't changed. version={0}",
             m_last_signals_version);
    return Status();
  }

  auto signals_to_ignore =
      m_unix_signals_sp->GetFilteredSignals(false, false, false);
  Status error = m_gdb_comm.SendSignalsToIgnore(signals_to_ignore);

  LLDB_LOG(log,
           "Signals' version changed. old version={0}, new version={1}, "
           "signals ignored={2}, update result={3}",
           m_last_signals_version, new_signals_version,
           signals_to_ignore.size(), error);

  if (error.Success())
    m_last_signals_version = new_signals_version;

  return error;
}

bool ProcessGDBRemote::StartNoticingNewThreads() {
  Log *log = GetLog(LLDBLog::Step);
  if (m_thread_create_bp_sp) {
    if (log && log->GetVerbose())
      LLDB_LOGF(log, "Enabled noticing new thread breakpoint.");
    m_thread_create_bp_sp->SetEnabled(true);
  } else {
    PlatformSP platform_sp(GetTarget().GetPlatform());
    if (platform_sp) {
      m_thread_create_bp_sp =
          platform_sp->SetThreadCreationBreakpoint(GetTarget());
      if (m_thread_create_bp_sp) {
        if (log && log->GetVerbose())
          LLDB_LOGF(
              log, "Successfully created new thread notification breakpoint %i",
              m_thread_create_bp_sp->GetID());
        m_thread_create_bp_sp->SetCallback(
            ProcessGDBRemote::NewThreadNotifyBreakpointHit, this, true);
      } else {
        LLDB_LOGF(log, "Failed to create new thread notification breakpoint.");
      }
    }
  }
  return m_thread_create_bp_sp.get() != nullptr;
}

bool ProcessGDBRemote::StopNoticingNewThreads() {
  Log *log = GetLog(LLDBLog::Step);
  if (log && log->GetVerbose())
    LLDB_LOGF(log, "Disabling new thread notification breakpoint.");

  if (m_thread_create_bp_sp)
    m_thread_create_bp_sp->SetEnabled(false);

  return true;
}

DynamicLoader *ProcessGDBRemote::GetDynamicLoader() {
  if (m_dyld_up.get() == nullptr)
    m_dyld_up.reset(DynamicLoader::FindPlugin(this, ""));
  return m_dyld_up.get();
}

Status ProcessGDBRemote::SendEventData(const char *data) {
  int return_value;
  bool was_supported;

  Status error;

  return_value = m_gdb_comm.SendLaunchEventDataPacket(data, &was_supported);
  if (return_value != 0) {
    if (!was_supported)
      error.SetErrorString("Sending events is not supported for this process.");
    else
      error.SetErrorStringWithFormat("Error sending event data: %d.",
                                     return_value);
  }
  return error;
}

DataExtractor ProcessGDBRemote::GetAuxvData() {
  DataBufferSP buf;
  if (m_gdb_comm.GetQXferAuxvReadSupported()) {
    llvm::Expected<std::string> response = m_gdb_comm.ReadExtFeature("auxv", "");
    if (response)
      buf = std::make_shared<DataBufferHeap>(response->c_str(),
                                             response->length());
    else
      LLDB_LOG_ERROR(GetLog(GDBRLog::Process), response.takeError(), "{0}");
  }
  return DataExtractor(buf, GetByteOrder(), GetAddressByteSize());
}

StructuredData::ObjectSP
ProcessGDBRemote::GetExtendedInfoForThread(lldb::tid_t tid) {
  StructuredData::ObjectSP object_sp;

  if (m_gdb_comm.GetThreadExtendedInfoSupported()) {
    StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());
    SystemRuntime *runtime = GetSystemRuntime();
    if (runtime) {
      runtime->AddThreadExtendedInfoPacketHints(args_dict);
    }
    args_dict->GetAsDictionary()->AddIntegerItem("thread", tid);

    StreamString packet;
    packet << "jThreadExtendedInfo:";
    args_dict->Dump(packet, false);

    // FIXME the final character of a JSON dictionary, '}', is the escape
    // character in gdb-remote binary mode.  lldb currently doesn't escape
    // these characters in its packet output -- so we add the quoted version of
    // the } character here manually in case we talk to a debugserver which un-
    // escapes the characters at packet read time.
    packet << (char)(0x7d ^ 0x20);

    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response) ==
        GDBRemoteCommunication::PacketResult::Success) {
      StringExtractorGDBRemote::ResponseType response_type =
          response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        if (!response.Empty()) {
          object_sp = StructuredData::ParseJSON(response.GetStringRef());
        }
      }
    }
  }
  return object_sp;
}

StructuredData::ObjectSP ProcessGDBRemote::GetLoadedDynamicLibrariesInfos(
    lldb::addr_t image_list_address, lldb::addr_t image_count) {

  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());
  args_dict->GetAsDictionary()->AddIntegerItem("image_list_address",
                                               image_list_address);
  args_dict->GetAsDictionary()->AddIntegerItem("image_count", image_count);

  return GetLoadedDynamicLibrariesInfos_sender(args_dict);
}

StructuredData::ObjectSP ProcessGDBRemote::GetLoadedDynamicLibrariesInfos() {
  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());

  args_dict->GetAsDictionary()->AddBooleanItem("fetch_all_solibs", true);

  return GetLoadedDynamicLibrariesInfos_sender(args_dict);
}

StructuredData::ObjectSP ProcessGDBRemote::GetLoadedDynamicLibrariesInfos(
    const std::vector<lldb::addr_t> &load_addresses) {
  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());
  StructuredData::ArraySP addresses(new StructuredData::Array);

  for (auto addr : load_addresses)
    addresses->AddIntegerItem(addr);

  args_dict->GetAsDictionary()->AddItem("solib_addresses", addresses);

  return GetLoadedDynamicLibrariesInfos_sender(args_dict);
}

StructuredData::ObjectSP
ProcessGDBRemote::GetLoadedDynamicLibrariesInfos_sender(
    StructuredData::ObjectSP args_dict) {
  StructuredData::ObjectSP object_sp;

  if (m_gdb_comm.GetLoadedDynamicLibrariesInfosSupported()) {
    // Scope for the scoped timeout object
    GDBRemoteCommunication::ScopedTimeout timeout(m_gdb_comm,
                                                  std::chrono::seconds(10));

    StreamString packet;
    packet << "jGetLoadedDynamicLibrariesInfos:";
    args_dict->Dump(packet, false);

    // FIXME the final character of a JSON dictionary, '}', is the escape
    // character in gdb-remote binary mode.  lldb currently doesn't escape
    // these characters in its packet output -- so we add the quoted version of
    // the } character here manually in case we talk to a debugserver which un-
    // escapes the characters at packet read time.
    packet << (char)(0x7d ^ 0x20);

    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response) ==
        GDBRemoteCommunication::PacketResult::Success) {
      StringExtractorGDBRemote::ResponseType response_type =
          response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        if (!response.Empty()) {
          object_sp = StructuredData::ParseJSON(response.GetStringRef());
        }
      }
    }
  }
  return object_sp;
}

StructuredData::ObjectSP ProcessGDBRemote::GetDynamicLoaderProcessState() {
  StructuredData::ObjectSP object_sp;
  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());

  if (m_gdb_comm.GetDynamicLoaderProcessStateSupported()) {
    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (m_gdb_comm.SendPacketAndWaitForResponse("jGetDyldProcessState",
                                                response) ==
        GDBRemoteCommunication::PacketResult::Success) {
      StringExtractorGDBRemote::ResponseType response_type =
          response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        if (!response.Empty()) {
          object_sp = StructuredData::ParseJSON(response.GetStringRef());
        }
      }
    }
  }
  return object_sp;
}

StructuredData::ObjectSP ProcessGDBRemote::GetSharedCacheInfo() {
  StructuredData::ObjectSP object_sp;
  StructuredData::ObjectSP args_dict(new StructuredData::Dictionary());

  if (m_gdb_comm.GetSharedCacheInfoSupported()) {
    StreamString packet;
    packet << "jGetSharedCacheInfo:";
    args_dict->Dump(packet, false);

    // FIXME the final character of a JSON dictionary, '}', is the escape
    // character in gdb-remote binary mode.  lldb currently doesn't escape
    // these characters in its packet output -- so we add the quoted version of
    // the } character here manually in case we talk to a debugserver which un-
    // escapes the characters at packet read time.
    packet << (char)(0x7d ^ 0x20);

    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response) ==
        GDBRemoteCommunication::PacketResult::Success) {
      StringExtractorGDBRemote::ResponseType response_type =
          response.GetResponseType();
      if (response_type == StringExtractorGDBRemote::eResponse) {
        if (!response.Empty()) {
          object_sp = StructuredData::ParseJSON(response.GetStringRef());
        }
      }
    }
  }
  return object_sp;
}

Status ProcessGDBRemote::ConfigureStructuredData(
    llvm::StringRef type_name, const StructuredData::ObjectSP &config_sp) {
  return m_gdb_comm.ConfigureRemoteStructuredData(type_name, config_sp);
}

// Establish the largest memory read/write payloads we should use. If the
// remote stub has a max packet size, stay under that size.
//
// If the remote stub's max packet size is crazy large, use a reasonable
// largeish default.
//
// If the remote stub doesn't advertise a max packet size, use a conservative
// default.

void ProcessGDBRemote::GetMaxMemorySize() {
  const uint64_t reasonable_largeish_default = 128 * 1024;
  const uint64_t conservative_default = 512;

  if (m_max_memory_size == 0) {
    uint64_t stub_max_size = m_gdb_comm.GetRemoteMaxPacketSize();
    if (stub_max_size != UINT64_MAX && stub_max_size != 0) {
      // Save the stub's claimed maximum packet size
      m_remote_stub_max_memory_size = stub_max_size;

      // Even if the stub says it can support ginormous packets, don't exceed
      // our reasonable largeish default packet size.
      if (stub_max_size > reasonable_largeish_default) {
        stub_max_size = reasonable_largeish_default;
      }

      // Memory packet have other overheads too like Maddr,size:#NN Instead of
      // calculating the bytes taken by size and addr every time, we take a
      // maximum guess here.
      if (stub_max_size > 70)
        stub_max_size -= 32 + 32 + 6;
      else {
        // In unlikely scenario that max packet size is less then 70, we will
        // hope that data being written is small enough to fit.
        Log *log(GetLog(GDBRLog::Comm | GDBRLog::Memory));
        if (log)
          log->Warning("Packet size is too small. "
                       "LLDB may face problems while writing memory");
      }

      m_max_memory_size = stub_max_size;
    } else {
      m_max_memory_size = conservative_default;
    }
  }
}

void ProcessGDBRemote::SetUserSpecifiedMaxMemoryTransferSize(
    uint64_t user_specified_max) {
  if (user_specified_max != 0) {
    GetMaxMemorySize();

    if (m_remote_stub_max_memory_size != 0) {
      if (m_remote_stub_max_memory_size < user_specified_max) {
        m_max_memory_size = m_remote_stub_max_memory_size; // user specified a
                                                           // packet size too
                                                           // big, go as big
        // as the remote stub says we can go.
      } else {
        m_max_memory_size = user_specified_max; // user's packet size is good
      }
    } else {
      m_max_memory_size =
          user_specified_max; // user's packet size is probably fine
    }
  }
}

bool ProcessGDBRemote::GetModuleSpec(const FileSpec &module_file_spec,
                                     const ArchSpec &arch,
                                     ModuleSpec &module_spec) {
  Log *log = GetLog(LLDBLog::Platform);

  const ModuleCacheKey key(module_file_spec.GetPath(),
                           arch.GetTriple().getTriple());
  auto cached = m_cached_module_specs.find(key);
  if (cached != m_cached_module_specs.end()) {
    module_spec = cached->second;
    return bool(module_spec);
  }

  if (!m_gdb_comm.GetModuleInfo(module_file_spec, arch, module_spec)) {
    LLDB_LOGF(log, "ProcessGDBRemote::%s - failed to get module info for %s:%s",
              __FUNCTION__, module_file_spec.GetPath().c_str(),
              arch.GetTriple().getTriple().c_str());
    return false;
  }

  if (log) {
    StreamString stream;
    module_spec.Dump(stream);
    LLDB_LOGF(log, "ProcessGDBRemote::%s - got module info for (%s:%s) : %s",
              __FUNCTION__, module_file_spec.GetPath().c_str(),
              arch.GetTriple().getTriple().c_str(), stream.GetData());
  }

  m_cached_module_specs[key] = module_spec;
  return true;
}

void ProcessGDBRemote::PrefetchModuleSpecs(
    llvm::ArrayRef<FileSpec> module_file_specs, const llvm::Triple &triple) {
  auto module_specs = m_gdb_comm.GetModulesInfo(module_file_specs, triple);
  if (module_specs) {
    for (const FileSpec &spec : module_file_specs)
      m_cached_module_specs[ModuleCacheKey(spec.GetPath(),
                                           triple.getTriple())] = ModuleSpec();
    for (const ModuleSpec &spec : *module_specs)
      m_cached_module_specs[ModuleCacheKey(spec.GetFileSpec().GetPath(),
                                           triple.getTriple())] = spec;
  }
}

llvm::VersionTuple ProcessGDBRemote::GetHostOSVersion() {
  return m_gdb_comm.GetOSVersion();
}

llvm::VersionTuple ProcessGDBRemote::GetHostMacCatalystVersion() {
  return m_gdb_comm.GetMacCatalystVersion();
}

namespace {

typedef std::vector<std::string> stringVec;

typedef std::vector<struct GdbServerRegisterInfo> GDBServerRegisterVec;
struct RegisterSetInfo {
  ConstString name;
};

typedef std::map<uint32_t, RegisterSetInfo> RegisterSetMap;

struct GdbServerTargetInfo {
  std::string arch;
  std::string osabi;
  stringVec includes;
  RegisterSetMap reg_set_map;
};

static FieldEnum::Enumerators ParseEnumEvalues(const XMLNode &enum_node) {
  Log *log(GetLog(GDBRLog::Process));
  // We will use the last instance of each value. Also we preserve the order
  // of declaration in the XML, as it may not be numerical.
  // For example, hardware may intially release with two states that softwware
  // can read from a register field:
  // 0 = startup, 1 = running
  // If in a future hardware release, the designers added a pre-startup state:
  // 0 = startup, 1 = running, 2 = pre-startup
  // Now it makes more sense to list them in this logical order as opposed to
  // numerical order:
  // 2 = pre-startup, 1 = startup, 0 = startup
  // This only matters for "register info" but let's trust what the server
  // chose regardless.
  std::map<uint64_t, FieldEnum::Enumerator> enumerators;

  enum_node.ForEachChildElementWithName(
      "evalue", [&enumerators, &log](const XMLNode &enumerator_node) {
        std::optional<llvm::StringRef> name;
        std::optional<uint64_t> value;

        enumerator_node.ForEachAttribute(
            [&name, &value, &log](const llvm::StringRef &attr_name,
                                  const llvm::StringRef &attr_value) {
              if (attr_name == "name") {
                if (attr_value.size())
                  name = attr_value;
                else
                  LLDB_LOG(log, "ProcessGDBRemote::ParseEnumEvalues "
                                "Ignoring empty name in evalue");
              } else if (attr_name == "value") {
                uint64_t parsed_value = 0;
                if (llvm::to_integer(attr_value, parsed_value))
                  value = parsed_value;
                else
                  LLDB_LOG(log,
                           "ProcessGDBRemote::ParseEnumEvalues "
                           "Invalid value \"{0}\" in "
                           "evalue",
                           attr_value.data());
              } else
                LLDB_LOG(log,
                         "ProcessGDBRemote::ParseEnumEvalues Ignoring "
                         "unknown attribute "
                         "\"{0}\" in evalue",
                         attr_name.data());

              // Keep walking attributes.
              return true;
            });

        if (value && name)
          enumerators.insert_or_assign(
              *value, FieldEnum::Enumerator(*value, name->str()));

        // Find all evalue elements.
        return true;
      });

  FieldEnum::Enumerators final_enumerators;
  for (auto [_, enumerator] : enumerators)
    final_enumerators.push_back(enumerator);

  return final_enumerators;
}

static void
ParseEnums(XMLNode feature_node,
           llvm::StringMap<std::unique_ptr<FieldEnum>> &registers_enum_types) {
  Log *log(GetLog(GDBRLog::Process));

  // The top level element is "<enum...".
  feature_node.ForEachChildElementWithName(
      "enum", [log, &registers_enum_types](const XMLNode &enum_node) {
        std::string id;

        enum_node.ForEachAttribute([&id](const llvm::StringRef &attr_name,
                                         const llvm::StringRef &attr_value) {
          if (attr_name == "id")
            id = attr_value;

          // There is also a "size" attribute that is supposed to be the size in
          // bytes of the register this applies to. However:
          // * LLDB doesn't need this information.
          // * It  is difficult to verify because you have to wait until the
          //   enum is applied to a field.
          //
          // So we will emit this attribute in XML for GDB's sake, but will not
          // bother ingesting it.

          // Walk all attributes.
          return true;
        });

        if (!id.empty()) {
          FieldEnum::Enumerators enumerators = ParseEnumEvalues(enum_node);
          if (!enumerators.empty()) {
            LLDB_LOG(log,
                     "ProcessGDBRemote::ParseEnums Found enum type \"{0}\"",
                     id);
            registers_enum_types.insert_or_assign(
                id, std::make_unique<FieldEnum>(id, enumerators));
          }
        }

        // Find all <enum> elements.
        return true;
      });
}

static std::vector<RegisterFlags::Field> ParseFlagsFields(
    XMLNode flags_node, unsigned size,
    const llvm::StringMap<std::unique_ptr<FieldEnum>> &registers_enum_types) {
  Log *log(GetLog(GDBRLog::Process));
  const unsigned max_start_bit = size * 8 - 1;

  // Process the fields of this set of flags.
  std::vector<RegisterFlags::Field> fields;
  flags_node.ForEachChildElementWithName("field", [&fields, max_start_bit, &log,
                                                   &registers_enum_types](
                                                      const XMLNode
                                                          &field_node) {
    std::optional<llvm::StringRef> name;
    std::optional<unsigned> start;
    std::optional<unsigned> end;
    std::optional<llvm::StringRef> type;

    field_node.ForEachAttribute([&name, &start, &end, &type, max_start_bit,
                                 &log](const llvm::StringRef &attr_name,
                                       const llvm::StringRef &attr_value) {
      // Note that XML in general requires that each of these attributes only
      // appears once, so we don't have to handle that here.
      if (attr_name == "name") {
        LLDB_LOG(
            log,
            "ProcessGDBRemote::ParseFlagsFields Found field node name \"{0}\"",
            attr_value.data());
        name = attr_value;
      } else if (attr_name == "start") {
        unsigned parsed_start = 0;
        if (llvm::to_integer(attr_value, parsed_start)) {
          if (parsed_start > max_start_bit) {
            LLDB_LOG(log,
                     "ProcessGDBRemote::ParseFlagsFields Invalid start {0} in "
                     "field node, "
                     "cannot be > {1}",
                     parsed_start, max_start_bit);
          } else
            start = parsed_start;
        } else {
          LLDB_LOG(
              log,
              "ProcessGDBRemote::ParseFlagsFields Invalid start \"{0}\" in "
              "field node",
              attr_value.data());
        }
      } else if (attr_name == "end") {
        unsigned parsed_end = 0;
        if (llvm::to_integer(attr_value, parsed_end))
          if (parsed_end > max_start_bit) {
            LLDB_LOG(log,
                     "ProcessGDBRemote::ParseFlagsFields Invalid end {0} in "
                     "field node, "
                     "cannot be > {1}",
                     parsed_end, max_start_bit);
          } else
            end = parsed_end;
        else {
          LLDB_LOG(log,
                   "ProcessGDBRemote::ParseFlagsFields Invalid end \"{0}\" in "
                   "field node",
                   attr_value.data());
        }
      } else if (attr_name == "type") {
        type = attr_value;
      } else {
        LLDB_LOG(
            log,
            "ProcessGDBRemote::ParseFlagsFields Ignoring unknown attribute "
            "\"{0}\" in field node",
            attr_name.data());
      }

      return true; // Walk all attributes of the field.
    });

    if (name && start && end) {
      if (*start > *end)
        LLDB_LOG(
            log,
            "ProcessGDBRemote::ParseFlagsFields Start {0} > end {1} in field "
            "\"{2}\", ignoring",
            *start, *end, name->data());
      else {
        if (RegisterFlags::Field::GetSizeInBits(*start, *end) > 64)
          LLDB_LOG(log,
                   "ProcessGDBRemote::ParseFlagsFields Ignoring field \"{2}\" "
                   "that has "
                   "size > 64 bits, this is not supported",
                   name->data());
        else {
          // A field's type may be set to the name of an enum type.
          const FieldEnum *enum_type = nullptr;
          if (type && !type->empty()) {
            auto found = registers_enum_types.find(*type);
            if (found != registers_enum_types.end()) {
              enum_type = found->second.get();

              // No enumerator can exceed the range of the field itself.
              uint64_t max_value =
                  RegisterFlags::Field::GetMaxValue(*start, *end);
              for (const auto &enumerator : enum_type->GetEnumerators()) {
                if (enumerator.m_value > max_value) {
                  enum_type = nullptr;
                  LLDB_LOG(
                      log,
                      "ProcessGDBRemote::ParseFlagsFields In enum \"{0}\" "
                      "evalue \"{1}\" with value {2} exceeds the maximum value "
                      "of field \"{3}\" ({4}), ignoring enum",
                      type->data(), enumerator.m_name, enumerator.m_value,
                      name->data(), max_value);
                  break;
                }
              }
            } else {
              LLDB_LOG(log,
                       "ProcessGDBRemote::ParseFlagsFields Could not find type "
                       "\"{0}\" "
                       "for field \"{1}\", ignoring",
                       type->data(), name->data());
            }
          }

          fields.push_back(
              RegisterFlags::Field(name->str(), *start, *end, enum_type));
        }
      }
    }

    return true; // Iterate all "field" nodes.
  });
  return fields;
}

void ParseFlags(
    XMLNode feature_node,
    llvm::StringMap<std::unique_ptr<RegisterFlags>> &registers_flags_types,
    const llvm::StringMap<std::unique_ptr<FieldEnum>> &registers_enum_types) {
  Log *log(GetLog(GDBRLog::Process));

  feature_node.ForEachChildElementWithName(
      "flags",
      [&log, &registers_flags_types,
       &registers_enum_types](const XMLNode &flags_node) -> bool {
        LLDB_LOG(log, "ProcessGDBRemote::ParseFlags Found flags node \"{0}\"",
                 flags_node.GetAttributeValue("id").c_str());

        std::optional<llvm::StringRef> id;
        std::optional<unsigned> size;
        flags_node.ForEachAttribute(
            [&id, &size, &log](const llvm::StringRef &name,
                               const llvm::StringRef &value) {
              if (name == "id") {
                id = value;
              } else if (name == "size") {
                unsigned parsed_size = 0;
                if (llvm::to_integer(value, parsed_size))
                  size = parsed_size;
                else {
                  LLDB_LOG(log,
                           "ProcessGDBRemote::ParseFlags Invalid size \"{0}\" "
                           "in flags node",
                           value.data());
                }
              } else {
                LLDB_LOG(log,
                         "ProcessGDBRemote::ParseFlags Ignoring unknown "
                         "attribute \"{0}\" in flags node",
                         name.data());
              }
              return true; // Walk all attributes.
            });

        if (id && size) {
          // Process the fields of this set of flags.
          std::vector<RegisterFlags::Field> fields =
              ParseFlagsFields(flags_node, *size, registers_enum_types);
          if (fields.size()) {
            // Sort so that the fields with the MSBs are first.
            std::sort(fields.rbegin(), fields.rend());
            std::vector<RegisterFlags::Field>::const_iterator overlap =
                std::adjacent_find(fields.begin(), fields.end(),
                                   [](const RegisterFlags::Field &lhs,
                                      const RegisterFlags::Field &rhs) {
                                     return lhs.Overlaps(rhs);
                                   });

            // If no fields overlap, use them.
            if (overlap == fields.end()) {
              if (registers_flags_types.contains(*id)) {
                // In theory you could define some flag set, use it with a
                // register then redefine it. We do not know if anyone does
                // that, or what they would expect to happen in that case.
                //
                // LLDB chooses to take the first definition and ignore the rest
                // as waiting until everything has been processed is more
                // expensive and difficult. This means that pointers to flag
                // sets in the register info remain valid if later the flag set
                // is redefined. If we allowed redefinitions, LLDB would crash
                // when you tried to print a register that used the original
                // definition.
                LLDB_LOG(
                    log,
                    "ProcessGDBRemote::ParseFlags Definition of flags "
                    "\"{0}\" shadows "
                    "previous definition, using original definition instead.",
                    id->data());
              } else {
                registers_flags_types.insert_or_assign(
                    *id, std::make_unique<RegisterFlags>(id->str(), *size,
                                                         std::move(fields)));
              }
            } else {
              // If any fields overlap, ignore the whole set of flags.
              std::vector<RegisterFlags::Field>::const_iterator next =
                  std::next(overlap);
              LLDB_LOG(
                  log,
                  "ProcessGDBRemote::ParseFlags Ignoring flags because fields "
                  "{0} (start: {1} end: {2}) and {3} (start: {4} end: {5}) "
                  "overlap.",
                  overlap->GetName().c_str(), overlap->GetStart(),
                  overlap->GetEnd(), next->GetName().c_str(), next->GetStart(),
                  next->GetEnd());
            }
          } else {
            LLDB_LOG(
                log,
                "ProcessGDBRemote::ParseFlags Ignoring definition of flags "
                "\"{0}\" because it contains no fields.",
                id->data());
          }
        }

        return true; // Keep iterating through all "flags" elements.
      });
}

bool ParseRegisters(
    XMLNode feature_node, GdbServerTargetInfo &target_info,
    std::vector<DynamicRegisterInfo::Register> &registers,
    llvm::StringMap<std::unique_ptr<RegisterFlags>> &registers_flags_types,
    llvm::StringMap<std::unique_ptr<FieldEnum>> &registers_enum_types) {
  if (!feature_node)
    return false;

  Log *log(GetLog(GDBRLog::Process));

  // Enums first because they are referenced by fields in the flags.
  ParseEnums(feature_node, registers_enum_types);
  for (const auto &enum_type : registers_enum_types)
    enum_type.second->DumpToLog(log);

  ParseFlags(feature_node, registers_flags_types, registers_enum_types);
  for (const auto &flags : registers_flags_types)
    flags.second->DumpToLog(log);

  feature_node.ForEachChildElementWithName(
      "reg",
      [&target_info, &registers, &registers_flags_types,
       log](const XMLNode &reg_node) -> bool {
        std::string gdb_group;
        std::string gdb_type;
        DynamicRegisterInfo::Register reg_info;
        bool encoding_set = false;
        bool format_set = false;

        // FIXME: we're silently ignoring invalid data here
        reg_node.ForEachAttribute([&target_info, &gdb_group, &gdb_type,
                                   &encoding_set, &format_set, &reg_info,
                                   log](const llvm::StringRef &name,
                                        const llvm::StringRef &value) -> bool {
          if (name == "name") {
            reg_info.name.SetString(value);
          } else if (name == "bitsize") {
            if (llvm::to_integer(value, reg_info.byte_size))
              reg_info.byte_size =
                  llvm::divideCeil(reg_info.byte_size, CHAR_BIT);
          } else if (name == "type") {
            gdb_type = value.str();
          } else if (name == "group") {
            gdb_group = value.str();
          } else if (name == "regnum") {
            llvm::to_integer(value, reg_info.regnum_remote);
          } else if (name == "offset") {
            llvm::to_integer(value, reg_info.byte_offset);
          } else if (name == "altname") {
            reg_info.alt_name.SetString(value);
          } else if (name == "encoding") {
            encoding_set = true;
            reg_info.encoding = Args::StringToEncoding(value, eEncodingUint);
          } else if (name == "format") {
            format_set = true;
            if (!OptionArgParser::ToFormat(value.data(), reg_info.format,
                                           nullptr)
                     .Success())
              reg_info.format =
                  llvm::StringSwitch<lldb::Format>(value)
                      .Case("vector-sint8", eFormatVectorOfSInt8)
                      .Case("vector-uint8", eFormatVectorOfUInt8)
                      .Case("vector-sint16", eFormatVectorOfSInt16)
                      .Case("vector-uint16", eFormatVectorOfUInt16)
                      .Case("vector-sint32", eFormatVectorOfSInt32)
                      .Case("vector-uint32", eFormatVectorOfUInt32)
                      .Case("vector-float32", eFormatVectorOfFloat32)
                      .Case("vector-uint64", eFormatVectorOfUInt64)
                      .Case("vector-uint128", eFormatVectorOfUInt128)
                      .Default(eFormatInvalid);
          } else if (name == "group_id") {
            uint32_t set_id = UINT32_MAX;
            llvm::to_integer(value, set_id);
            RegisterSetMap::const_iterator pos =
                target_info.reg_set_map.find(set_id);
            if (pos != target_info.reg_set_map.end())
              reg_info.set_name = pos->second.name;
          } else if (name == "gcc_regnum" || name == "ehframe_regnum") {
            llvm::to_integer(value, reg_info.regnum_ehframe);
          } else if (name == "dwarf_regnum") {
            llvm::to_integer(value, reg_info.regnum_dwarf);
          } else if (name == "generic") {
            reg_info.regnum_generic = Args::StringToGenericRegister(value);
          } else if (name == "value_regnums") {
            SplitCommaSeparatedRegisterNumberString(value, reg_info.value_regs,
                                                    0);
          } else if (name == "invalidate_regnums") {
            SplitCommaSeparatedRegisterNumberString(
                value, reg_info.invalidate_regs, 0);
          } else {
            LLDB_LOGF(log,
                      "ProcessGDBRemote::ParseRegisters unhandled reg "
                      "attribute %s = %s",
                      name.data(), value.data());
          }
          return true; // Keep iterating through all attributes
        });

        if (!gdb_type.empty()) {
          // gdb_type could reference some flags type defined in XML.
          llvm::StringMap<std::unique_ptr<RegisterFlags>>::iterator it =
              registers_flags_types.find(gdb_type);
          if (it != registers_flags_types.end()) {
            auto flags_type = it->second.get();
            if (reg_info.byte_size == flags_type->GetSize())
              reg_info.flags_type = flags_type;
            else
              LLDB_LOGF(log,
                        "ProcessGDBRemote::ParseRegisters Size of register "
                        "flags %s (%d bytes) for "
                        "register %s does not match the register size (%d "
                        "bytes). Ignoring this set of flags.",
                        flags_type->GetID().c_str(), flags_type->GetSize(),
                        reg_info.name.AsCString(), reg_info.byte_size);
          }

          // There's a slim chance that the gdb_type name is both a flags type
          // and a simple type. Just in case, look for that too (setting both
          // does no harm).
          if (!gdb_type.empty() && !(encoding_set || format_set)) {
            if (llvm::StringRef(gdb_type).starts_with("int")) {
              reg_info.format = eFormatHex;
              reg_info.encoding = eEncodingUint;
            } else if (gdb_type == "data_ptr" || gdb_type == "code_ptr") {
              reg_info.format = eFormatAddressInfo;
              reg_info.encoding = eEncodingUint;
            } else if (gdb_type == "float") {
              reg_info.format = eFormatFloat;
              reg_info.encoding = eEncodingIEEE754;
            } else if (gdb_type == "aarch64v" ||
                       llvm::StringRef(gdb_type).starts_with("vec") ||
                       gdb_type == "i387_ext" || gdb_type == "uint128") {
              // lldb doesn't handle 128-bit uints correctly (for ymm*h), so
              // treat them as vector (similarly to xmm/ymm)
              reg_info.format = eFormatVectorOfUInt8;
              reg_info.encoding = eEncodingVector;
            } else {
              LLDB_LOGF(
                  log,
                  "ProcessGDBRemote::ParseRegisters Could not determine lldb"
                  "format and encoding for gdb type %s",
                  gdb_type.c_str());
            }
          }
        }

        // Only update the register set name if we didn't get a "reg_set"
        // attribute. "set_name" will be empty if we didn't have a "reg_set"
        // attribute.
        if (!reg_info.set_name) {
          if (!gdb_group.empty()) {
            reg_info.set_name.SetCString(gdb_group.c_str());
          } else {
            // If no register group name provided anywhere,
            // we'll create a 'general' register set
            reg_info.set_name.SetCString("general");
          }
        }

        if (reg_info.byte_size == 0) {
          LLDB_LOGF(log,
                    "ProcessGDBRemote::%s Skipping zero bitsize register %s",
                    __FUNCTION__, reg_info.name.AsCString());
        } else
          registers.push_back(reg_info);

        return true; // Keep iterating through all "reg" elements
      });
  return true;
}

} // namespace

// This method fetches a register description feature xml file from
// the remote stub and adds registers/register groupsets/architecture
// information to the current process.  It will call itself recursively
// for nested register definition files.  It returns true if it was able
// to fetch and parse an xml file.
bool ProcessGDBRemote::GetGDBServerRegisterInfoXMLAndProcess(
    ArchSpec &arch_to_use, std::string xml_filename,
    std::vector<DynamicRegisterInfo::Register> &registers) {
  // request the target xml file
  llvm::Expected<std::string> raw = m_gdb_comm.ReadExtFeature("features", xml_filename);
  if (errorToBool(raw.takeError()))
    return false;

  XMLDocument xml_document;

  if (xml_document.ParseMemory(raw->c_str(), raw->size(),
                               xml_filename.c_str())) {
    GdbServerTargetInfo target_info;
    std::vector<XMLNode> feature_nodes;

    // The top level feature XML file will start with a <target> tag.
    XMLNode target_node = xml_document.GetRootElement("target");
    if (target_node) {
      target_node.ForEachChildElement([&target_info, &feature_nodes](
                                          const XMLNode &node) -> bool {
        llvm::StringRef name = node.GetName();
        if (name == "architecture") {
          node.GetElementText(target_info.arch);
        } else if (name == "osabi") {
          node.GetElementText(target_info.osabi);
        } else if (name == "xi:include" || name == "include") {
          std::string href = node.GetAttributeValue("href");
          if (!href.empty())
            target_info.includes.push_back(href);
        } else if (name == "feature") {
          feature_nodes.push_back(node);
        } else if (name == "groups") {
          node.ForEachChildElementWithName(
              "group", [&target_info](const XMLNode &node) -> bool {
                uint32_t set_id = UINT32_MAX;
                RegisterSetInfo set_info;

                node.ForEachAttribute(
                    [&set_id, &set_info](const llvm::StringRef &name,
                                         const llvm::StringRef &value) -> bool {
                      // FIXME: we're silently ignoring invalid data here
                      if (name == "id")
                        llvm::to_integer(value, set_id);
                      if (name == "name")
                        set_info.name = ConstString(value);
                      return true; // Keep iterating through all attributes
                    });

                if (set_id != UINT32_MAX)
                  target_info.reg_set_map[set_id] = set_info;
                return true; // Keep iterating through all "group" elements
              });
        }
        return true; // Keep iterating through all children of the target_node
      });
    } else {
      // In an included XML feature file, we're already "inside" the <target>
      // tag of the initial XML file; this included file will likely only have
      // a <feature> tag.  Need to check for any more included files in this
      // <feature> element.
      XMLNode feature_node = xml_document.GetRootElement("feature");
      if (feature_node) {
        feature_nodes.push_back(feature_node);
        feature_node.ForEachChildElement([&target_info](
                                        const XMLNode &node) -> bool {
          llvm::StringRef name = node.GetName();
          if (name == "xi:include" || name == "include") {
            std::string href = node.GetAttributeValue("href");
            if (!href.empty())
              target_info.includes.push_back(href);
            }
            return true;
          });
      }
    }

    // gdbserver does not implement the LLDB packets used to determine host
    // or process architecture.  If that is the case, attempt to use
    // the <architecture/> field from target.xml, e.g.:
    //
    //   <architecture>i386:x86-64</architecture> (seen from VMWare ESXi)
    //   <architecture>arm</architecture> (seen from Segger JLink on unspecified
    //   arm board)
    if (!arch_to_use.IsValid() && !target_info.arch.empty()) {
      // We don't have any information about vendor or OS.
      arch_to_use.SetTriple(llvm::StringSwitch<std::string>(target_info.arch)
                                .Case("i386:x86-64", "x86_64")
                                .Case("riscv:rv64", "riscv64")
                                .Case("riscv:rv32", "riscv32")
                                .Default(target_info.arch) +
                            "--");

      if (arch_to_use.IsValid())
        GetTarget().MergeArchitecture(arch_to_use);
    }

    if (arch_to_use.IsValid()) {
      for (auto &feature_node : feature_nodes) {
        ParseRegisters(feature_node, target_info, registers,
                       m_registers_flags_types, m_registers_enum_types);
      }

      for (const auto &include : target_info.includes) {
        GetGDBServerRegisterInfoXMLAndProcess(arch_to_use, include,
                                              registers);
      }
    }
  } else {
    return false;
  }
  return true;
}

void ProcessGDBRemote::AddRemoteRegisters(
    std::vector<DynamicRegisterInfo::Register> &registers,
    const ArchSpec &arch_to_use) {
  std::map<uint32_t, uint32_t> remote_to_local_map;
  uint32_t remote_regnum = 0;
  for (auto it : llvm::enumerate(registers)) {
    DynamicRegisterInfo::Register &remote_reg_info = it.value();

    // Assign successive remote regnums if missing.
    if (remote_reg_info.regnum_remote == LLDB_INVALID_REGNUM)
      remote_reg_info.regnum_remote = remote_regnum;

    // Create a mapping from remote to local regnos.
    remote_to_local_map[remote_reg_info.regnum_remote] = it.index();

    remote_regnum = remote_reg_info.regnum_remote + 1;
  }

  for (DynamicRegisterInfo::Register &remote_reg_info : registers) {
    auto proc_to_lldb = [&remote_to_local_map](uint32_t process_regnum) {
      auto lldb_regit = remote_to_local_map.find(process_regnum);
      return lldb_regit != remote_to_local_map.end() ? lldb_regit->second
                                                     : LLDB_INVALID_REGNUM;
    };

    llvm::transform(remote_reg_info.value_regs,
                    remote_reg_info.value_regs.begin(), proc_to_lldb);
    llvm::transform(remote_reg_info.invalidate_regs,
                    remote_reg_info.invalidate_regs.begin(), proc_to_lldb);
  }

  // Don't use Process::GetABI, this code gets called from DidAttach, and
  // in that context we haven't set the Target's architecture yet, so the
  // ABI is also potentially incorrect.
  if (ABISP abi_sp = ABI::FindPlugin(shared_from_this(), arch_to_use))
    abi_sp->AugmentRegisterInfo(registers);

  m_register_info_sp->SetRegisterInfo(std::move(registers), arch_to_use);
}

// query the target of gdb-remote for extended target information returns
// true on success (got register definitions), false on failure (did not).
bool ProcessGDBRemote::GetGDBServerRegisterInfo(ArchSpec &arch_to_use) {
  // Make sure LLDB has an XML parser it can use first
  if (!XMLDocument::XMLEnabled())
    return false;

  // check that we have extended feature read support
  if (!m_gdb_comm.GetQXferFeaturesReadSupported())
    return false;

  // These hold register type information for the whole of target.xml.
  // target.xml may include further documents that
  // GetGDBServerRegisterInfoXMLAndProcess will recurse to fetch and process.
  // That's why we clear the cache here, and not in
  // GetGDBServerRegisterInfoXMLAndProcess. To prevent it being cleared on every
  // include read.
  m_registers_flags_types.clear();
  m_registers_enum_types.clear();
  std::vector<DynamicRegisterInfo::Register> registers;
  if (GetGDBServerRegisterInfoXMLAndProcess(arch_to_use, "target.xml",
                                            registers) &&
      // Target XML is not required to include register information.
      !registers.empty())
    AddRemoteRegisters(registers, arch_to_use);

  return m_register_info_sp->GetNumRegisters() > 0;
}

llvm::Expected<LoadedModuleInfoList> ProcessGDBRemote::GetLoadedModuleList() {
  // Make sure LLDB has an XML parser it can use first
  if (!XMLDocument::XMLEnabled())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "XML parsing not available");

  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log, "ProcessGDBRemote::%s", __FUNCTION__);

  LoadedModuleInfoList list;
  GDBRemoteCommunicationClient &comm = m_gdb_comm;
  bool can_use_svr4 = GetGlobalPluginProperties().GetUseSVR4();

  // check that we have extended feature read support
  if (can_use_svr4 && comm.GetQXferLibrariesSVR4ReadSupported()) {
    // request the loaded library list
    llvm::Expected<std::string> raw = comm.ReadExtFeature("libraries-svr4", "");
    if (!raw)
      return raw.takeError();

    // parse the xml file in memory
    LLDB_LOGF(log, "parsing: %s", raw->c_str());
    XMLDocument doc;

    if (!doc.ParseMemory(raw->c_str(), raw->size(), "noname.xml"))
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Error reading noname.xml");

    XMLNode root_element = doc.GetRootElement("library-list-svr4");
    if (!root_element)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Error finding library-list-svr4 xml element");

    // main link map structure
    std::string main_lm = root_element.GetAttributeValue("main-lm");
    // FIXME: we're silently ignoring invalid data here
    if (!main_lm.empty())
      llvm::to_integer(main_lm, list.m_link_map);

    root_element.ForEachChildElementWithName(
        "library", [log, &list](const XMLNode &library) -> bool {
          LoadedModuleInfoList::LoadedModuleInfo module;

          // FIXME: we're silently ignoring invalid data here
          library.ForEachAttribute(
              [&module](const llvm::StringRef &name,
                        const llvm::StringRef &value) -> bool {
                uint64_t uint_value = LLDB_INVALID_ADDRESS;
                if (name == "name")
                  module.set_name(value.str());
                else if (name == "lm") {
                  // the address of the link_map struct.
                  llvm::to_integer(value, uint_value);
                  module.set_link_map(uint_value);
                } else if (name == "l_addr") {
                  // the displacement as read from the field 'l_addr' of the
                  // link_map struct.
                  llvm::to_integer(value, uint_value);
                  module.set_base(uint_value);
                  // base address is always a displacement, not an absolute
                  // value.
                  module.set_base_is_offset(true);
                } else if (name == "l_ld") {
                  // the memory address of the libraries PT_DYNAMIC section.
                  llvm::to_integer(value, uint_value);
                  module.set_dynamic(uint_value);
                }

                return true; // Keep iterating over all properties of "library"
              });

          if (log) {
            std::string name;
            lldb::addr_t lm = 0, base = 0, ld = 0;
            bool base_is_offset;

            module.get_name(name);
            module.get_link_map(lm);
            module.get_base(base);
            module.get_base_is_offset(base_is_offset);
            module.get_dynamic(ld);

            LLDB_LOGF(log,
                      "found (link_map:0x%08" PRIx64 ", base:0x%08" PRIx64
                      "[%s], ld:0x%08" PRIx64 ", name:'%s')",
                      lm, base, (base_is_offset ? "offset" : "absolute"), ld,
                      name.c_str());
          }

          list.add(module);
          return true; // Keep iterating over all "library" elements in the root
                       // node
        });

    if (log)
      LLDB_LOGF(log, "found %" PRId32 " modules in total",
                (int)list.m_list.size());
    return list;
  } else if (comm.GetQXferLibrariesReadSupported()) {
    // request the loaded library list
    llvm::Expected<std::string> raw = comm.ReadExtFeature("libraries", "");

    if (!raw)
      return raw.takeError();

    LLDB_LOGF(log, "parsing: %s", raw->c_str());
    XMLDocument doc;

    if (!doc.ParseMemory(raw->c_str(), raw->size(), "noname.xml"))
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Error reading noname.xml");

    XMLNode root_element = doc.GetRootElement("library-list");
    if (!root_element)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Error finding library-list xml element");

    // FIXME: we're silently ignoring invalid data here
    root_element.ForEachChildElementWithName(
        "library", [log, &list](const XMLNode &library) -> bool {
          LoadedModuleInfoList::LoadedModuleInfo module;

          std::string name = library.GetAttributeValue("name");
          module.set_name(name);

          // The base address of a given library will be the address of its
          // first section. Most remotes send only one section for Windows
          // targets for example.
          const XMLNode &section =
              library.FindFirstChildElementWithName("section");
          std::string address = section.GetAttributeValue("address");
          uint64_t address_value = LLDB_INVALID_ADDRESS;
          llvm::to_integer(address, address_value);
          module.set_base(address_value);
          // These addresses are absolute values.
          module.set_base_is_offset(false);

          if (log) {
            std::string name;
            lldb::addr_t base = 0;
            bool base_is_offset;
            module.get_name(name);
            module.get_base(base);
            module.get_base_is_offset(base_is_offset);

            LLDB_LOGF(log, "found (base:0x%08" PRIx64 "[%s], name:'%s')", base,
                      (base_is_offset ? "offset" : "absolute"), name.c_str());
          }

          list.add(module);
          return true; // Keep iterating over all "library" elements in the root
                       // node
        });

    if (log)
      LLDB_LOGF(log, "found %" PRId32 " modules in total",
                (int)list.m_list.size());
    return list;
  } else {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Remote libraries not supported");
  }
}

lldb::ModuleSP ProcessGDBRemote::LoadModuleAtAddress(const FileSpec &file,
                                                     lldb::addr_t link_map,
                                                     lldb::addr_t base_addr,
                                                     bool value_is_offset) {
  DynamicLoader *loader = GetDynamicLoader();
  if (!loader)
    return nullptr;

  return loader->LoadModuleAtAddress(file, link_map, base_addr,
                                     value_is_offset);
}

llvm::Error ProcessGDBRemote::LoadModules() {
  using lldb_private::process_gdb_remote::ProcessGDBRemote;

  // request a list of loaded libraries from GDBServer
  llvm::Expected<LoadedModuleInfoList> module_list = GetLoadedModuleList();
  if (!module_list)
    return module_list.takeError();

  // get a list of all the modules
  ModuleList new_modules;

  for (LoadedModuleInfoList::LoadedModuleInfo &modInfo : module_list->m_list) {
    std::string mod_name;
    lldb::addr_t mod_base;
    lldb::addr_t link_map;
    bool mod_base_is_offset;

    bool valid = true;
    valid &= modInfo.get_name(mod_name);
    valid &= modInfo.get_base(mod_base);
    valid &= modInfo.get_base_is_offset(mod_base_is_offset);
    if (!valid)
      continue;

    if (!modInfo.get_link_map(link_map))
      link_map = LLDB_INVALID_ADDRESS;

    FileSpec file(mod_name);
    FileSystem::Instance().Resolve(file);
    lldb::ModuleSP module_sp =
        LoadModuleAtAddress(file, link_map, mod_base, mod_base_is_offset);

    if (module_sp.get())
      new_modules.Append(module_sp);
  }

  if (new_modules.GetSize() > 0) {
    ModuleList removed_modules;
    Target &target = GetTarget();
    ModuleList &loaded_modules = m_process->GetTarget().GetImages();

    for (size_t i = 0; i < loaded_modules.GetSize(); ++i) {
      const lldb::ModuleSP loaded_module = loaded_modules.GetModuleAtIndex(i);

      bool found = false;
      for (size_t j = 0; j < new_modules.GetSize(); ++j) {
        if (new_modules.GetModuleAtIndex(j).get() == loaded_module.get())
          found = true;
      }

      // The main executable will never be included in libraries-svr4, don't
      // remove it
      if (!found &&
          loaded_module.get() != target.GetExecutableModulePointer()) {
        removed_modules.Append(loaded_module);
      }
    }

    loaded_modules.Remove(removed_modules);
    m_process->GetTarget().ModulesDidUnload(removed_modules, false);

    new_modules.ForEach([&target](const lldb::ModuleSP module_sp) -> bool {
      lldb_private::ObjectFile *obj = module_sp->GetObjectFile();
      if (!obj)
        return true;

      if (obj->GetType() != ObjectFile::Type::eTypeExecutable)
        return true;

      lldb::ModuleSP module_copy_sp = module_sp;
      target.SetExecutableModule(module_copy_sp, eLoadDependentsNo);
      return false;
    });

    loaded_modules.AppendIfNeeded(new_modules);
    m_process->GetTarget().ModulesDidLoad(new_modules);
  }

  return llvm::ErrorSuccess();
}

Status ProcessGDBRemote::GetFileLoadAddress(const FileSpec &file,
                                            bool &is_loaded,
                                            lldb::addr_t &load_addr) {
  is_loaded = false;
  load_addr = LLDB_INVALID_ADDRESS;

  std::string file_path = file.GetPath(false);
  if (file_path.empty())
    return Status("Empty file name specified");

  StreamString packet;
  packet.PutCString("qFileLoadAddress:");
  packet.PutStringAsRawHex8(file_path);

  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response) !=
      GDBRemoteCommunication::PacketResult::Success)
    return Status("Sending qFileLoadAddress packet failed");

  if (response.IsErrorResponse()) {
    if (response.GetError() == 1) {
      // The file is not loaded into the inferior
      is_loaded = false;
      load_addr = LLDB_INVALID_ADDRESS;
      return Status();
    }

    return Status(
        "Fetching file load address from remote server returned an error");
  }

  if (response.IsNormalResponse()) {
    is_loaded = true;
    load_addr = response.GetHexMaxU64(false, LLDB_INVALID_ADDRESS);
    return Status();
  }

  return Status(
      "Unknown error happened during sending the load address packet");
}

void ProcessGDBRemote::ModulesDidLoad(ModuleList &module_list) {
  // We must call the lldb_private::Process::ModulesDidLoad () first before we
  // do anything
  Process::ModulesDidLoad(module_list);

  // After loading shared libraries, we can ask our remote GDB server if it
  // needs any symbols.
  m_gdb_comm.ServeSymbolLookups(this);
}

void ProcessGDBRemote::HandleAsyncStdout(llvm::StringRef out) {
  AppendSTDOUT(out.data(), out.size());
}

static const char *end_delimiter = "--end--;";
static const int end_delimiter_len = 8;

void ProcessGDBRemote::HandleAsyncMisc(llvm::StringRef data) {
  std::string input = data.str(); // '1' to move beyond 'A'
  if (m_partial_profile_data.length() > 0) {
    m_partial_profile_data.append(input);
    input = m_partial_profile_data;
    m_partial_profile_data.clear();
  }

  size_t found, pos = 0, len = input.length();
  while ((found = input.find(end_delimiter, pos)) != std::string::npos) {
    StringExtractorGDBRemote profileDataExtractor(
        input.substr(pos, found).c_str());
    std::string profile_data =
        HarmonizeThreadIdsForProfileData(profileDataExtractor);
    BroadcastAsyncProfileData(profile_data);

    pos = found + end_delimiter_len;
  }

  if (pos < len) {
    // Last incomplete chunk.
    m_partial_profile_data = input.substr(pos);
  }
}

std::string ProcessGDBRemote::HarmonizeThreadIdsForProfileData(
    StringExtractorGDBRemote &profileDataExtractor) {
  std::map<uint64_t, uint32_t> new_thread_id_to_used_usec_map;
  std::string output;
  llvm::raw_string_ostream output_stream(output);
  llvm::StringRef name, value;

  // Going to assuming thread_used_usec comes first, else bail out.
  while (profileDataExtractor.GetNameColonValue(name, value)) {
    if (name.compare("thread_used_id") == 0) {
      StringExtractor threadIDHexExtractor(value);
      uint64_t thread_id = threadIDHexExtractor.GetHexMaxU64(false, 0);

      bool has_used_usec = false;
      uint32_t curr_used_usec = 0;
      llvm::StringRef usec_name, usec_value;
      uint32_t input_file_pos = profileDataExtractor.GetFilePos();
      if (profileDataExtractor.GetNameColonValue(usec_name, usec_value)) {
        if (usec_name == "thread_used_usec") {
          has_used_usec = true;
          usec_value.getAsInteger(0, curr_used_usec);
        } else {
          // We didn't find what we want, it is probably an older version. Bail
          // out.
          profileDataExtractor.SetFilePos(input_file_pos);
        }
      }

      if (has_used_usec) {
        uint32_t prev_used_usec = 0;
        std::map<uint64_t, uint32_t>::iterator iterator =
            m_thread_id_to_used_usec_map.find(thread_id);
        if (iterator != m_thread_id_to_used_usec_map.end()) {
          prev_used_usec = m_thread_id_to_used_usec_map[thread_id];
        }

        uint32_t real_used_usec = curr_used_usec - prev_used_usec;
        // A good first time record is one that runs for at least 0.25 sec
        bool good_first_time =
            (prev_used_usec == 0) && (real_used_usec > 250000);
        bool good_subsequent_time =
            (prev_used_usec > 0) &&
            ((real_used_usec > 0) || (HasAssignedIndexIDToThread(thread_id)));

        if (good_first_time || good_subsequent_time) {
          // We try to avoid doing too many index id reservation, resulting in
          // fast increase of index ids.

          output_stream << name << ":";
          int32_t index_id = AssignIndexIDToThread(thread_id);
          output_stream << index_id << ";";

          output_stream << usec_name << ":" << usec_value << ";";
        } else {
          // Skip past 'thread_used_name'.
          llvm::StringRef local_name, local_value;
          profileDataExtractor.GetNameColonValue(local_name, local_value);
        }

        // Store current time as previous time so that they can be compared
        // later.
        new_thread_id_to_used_usec_map[thread_id] = curr_used_usec;
      } else {
        // Bail out and use old string.
        output_stream << name << ":" << value << ";";
      }
    } else {
      output_stream << name << ":" << value << ";";
    }
  }
  output_stream << end_delimiter;
  m_thread_id_to_used_usec_map = new_thread_id_to_used_usec_map;

  return output_stream.str();
}

void ProcessGDBRemote::HandleStopReply() {
  if (GetStopID() != 0)
    return;

  if (GetID() == LLDB_INVALID_PROCESS_ID) {
    lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID();
    if (pid != LLDB_INVALID_PROCESS_ID)
      SetID(pid);
  }
  BuildDynamicRegisterInfo(true);
}

llvm::Expected<bool> ProcessGDBRemote::SaveCore(llvm::StringRef outfile) {
  if (!m_gdb_comm.GetSaveCoreSupported())
    return false;

  StreamString packet;
  packet.PutCString("qSaveCore;path-hint:");
  packet.PutStringAsRawHex8(outfile);

  StringExtractorGDBRemote response;
  if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetString(), response) ==
      GDBRemoteCommunication::PacketResult::Success) {
    // TODO: grab error message from the packet?  StringExtractor seems to
    // be missing a method for that
    if (response.IsErrorResponse())
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          llvm::formatv("qSaveCore returned an error"));

    std::string path;

    // process the response
    for (auto x : llvm::split(response.GetStringRef(), ';')) {
      if (x.consume_front("core-path:"))
        StringExtractor(x).GetHexByteString(path);
    }

    // verify that we've gotten what we need
    if (path.empty())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "qSaveCore returned no core path");

    // now transfer the core file
    FileSpec remote_core{llvm::StringRef(path)};
    Platform &platform = *GetTarget().GetPlatform();
    Status error = platform.GetFile(remote_core, FileSpec(outfile));

    if (platform.IsRemote()) {
      // NB: we unlink the file on error too
      platform.Unlink(remote_core);
      if (error.Fail())
        return error.ToError();
    }

    return true;
  }

  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "Unable to send qSaveCore");
}

static const char *const s_async_json_packet_prefix = "JSON-async:";

static StructuredData::ObjectSP
ParseStructuredDataPacket(llvm::StringRef packet) {
  Log *log = GetLog(GDBRLog::Process);

  if (!packet.consume_front(s_async_json_packet_prefix)) {
    if (log) {
      LLDB_LOGF(
          log,
          "GDBRemoteCommunicationClientBase::%s() received $J packet "
          "but was not a StructuredData packet: packet starts with "
          "%s",
          __FUNCTION__,
          packet.slice(0, strlen(s_async_json_packet_prefix)).str().c_str());
    }
    return StructuredData::ObjectSP();
  }

  // This is an asynchronous JSON packet, destined for a StructuredDataPlugin.
  StructuredData::ObjectSP json_sp = StructuredData::ParseJSON(packet);
  if (log) {
    if (json_sp) {
      StreamString json_str;
      json_sp->Dump(json_str, true);
      json_str.Flush();
      LLDB_LOGF(log,
                "ProcessGDBRemote::%s() "
                "received Async StructuredData packet: %s",
                __FUNCTION__, json_str.GetData());
    } else {
      LLDB_LOGF(log,
                "ProcessGDBRemote::%s"
                "() received StructuredData packet:"
                " parse failure",
                __FUNCTION__);
    }
  }
  return json_sp;
}

void ProcessGDBRemote::HandleAsyncStructuredDataPacket(llvm::StringRef data) {
  auto structured_data_sp = ParseStructuredDataPacket(data);
  if (structured_data_sp)
    RouteAsyncStructuredData(structured_data_sp);
}

class CommandObjectProcessGDBRemoteSpeedTest : public CommandObjectParsed {
public:
  CommandObjectProcessGDBRemoteSpeedTest(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process plugin packet speed-test",
                            "Tests packet speeds of various sizes to determine "
                            "the performance characteristics of the GDB remote "
                            "connection. ",
                            nullptr),
        m_option_group(),
        m_num_packets(LLDB_OPT_SET_1, false, "count", 'c', 0, eArgTypeCount,
                      "The number of packets to send of each varying size "
                      "(default is 1000).",
                      1000),
        m_max_send(LLDB_OPT_SET_1, false, "max-send", 's', 0, eArgTypeCount,
                   "The maximum number of bytes to send in a packet. Sizes "
                   "increase in powers of 2 while the size is less than or "
                   "equal to this option value. (default 1024).",
                   1024),
        m_max_recv(LLDB_OPT_SET_1, false, "max-receive", 'r', 0, eArgTypeCount,
                   "The maximum number of bytes to receive in a packet. Sizes "
                   "increase in powers of 2 while the size is less than or "
                   "equal to this option value. (default 1024).",
                   1024),
        m_json(LLDB_OPT_SET_1, false, "json", 'j',
               "Print the output as JSON data for easy parsing.", false, true) {
    m_option_group.Append(&m_num_packets, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_max_send, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_max_recv, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_json, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectProcessGDBRemoteSpeedTest() override = default;

  Options *GetOptions() override { return &m_option_group; }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      ProcessGDBRemote *process =
          (ProcessGDBRemote *)m_interpreter.GetExecutionContext()
              .GetProcessPtr();
      if (process) {
        StreamSP output_stream_sp = result.GetImmediateOutputStream();
        if (!output_stream_sp)
          output_stream_sp =
              StreamSP(m_interpreter.GetDebugger().GetAsyncOutputStream());
        result.SetImmediateOutputStream(output_stream_sp);

        const uint32_t num_packets =
            (uint32_t)m_num_packets.GetOptionValue().GetCurrentValue();
        const uint64_t max_send = m_max_send.GetOptionValue().GetCurrentValue();
        const uint64_t max_recv = m_max_recv.GetOptionValue().GetCurrentValue();
        const bool json = m_json.GetOptionValue().GetCurrentValue();
        const uint64_t k_recv_amount =
            4 * 1024 * 1024; // Receive amount in bytes
        process->GetGDBRemote().TestPacketSpeed(
            num_packets, max_send, max_recv, k_recv_amount, json,
            output_stream_sp ? *output_stream_sp : result.GetOutputStream());
        result.SetStatus(eReturnStatusSuccessFinishResult);
        return;
      }
    } else {
      result.AppendErrorWithFormat("'%s' takes no arguments",
                                   m_cmd_name.c_str());
    }
    result.SetStatus(eReturnStatusFailed);
  }

protected:
  OptionGroupOptions m_option_group;
  OptionGroupUInt64 m_num_packets;
  OptionGroupUInt64 m_max_send;
  OptionGroupUInt64 m_max_recv;
  OptionGroupBoolean m_json;
};

class CommandObjectProcessGDBRemotePacketHistory : public CommandObjectParsed {
private:
public:
  CommandObjectProcessGDBRemotePacketHistory(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process plugin packet history",
                            "Dumps the packet history buffer. ", nullptr) {}

  ~CommandObjectProcessGDBRemotePacketHistory() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    ProcessGDBRemote *process =
        (ProcessGDBRemote *)m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process) {
      process->DumpPluginHistory(result.GetOutputStream());
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return;
    }
    result.SetStatus(eReturnStatusFailed);
  }
};

class CommandObjectProcessGDBRemotePacketXferSize : public CommandObjectParsed {
private:
public:
  CommandObjectProcessGDBRemotePacketXferSize(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process plugin packet xfer-size",
            "Maximum size that lldb will try to read/write one one chunk.",
            nullptr) {
    AddSimpleArgumentList(eArgTypeUnsignedInteger);
  }

  ~CommandObjectProcessGDBRemotePacketXferSize() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      result.AppendErrorWithFormat("'%s' takes an argument to specify the max "
                                   "amount to be transferred when "
                                   "reading/writing",
                                   m_cmd_name.c_str());
      return;
    }

    ProcessGDBRemote *process =
        (ProcessGDBRemote *)m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process) {
      const char *packet_size = command.GetArgumentAtIndex(0);
      errno = 0;
      uint64_t user_specified_max = strtoul(packet_size, nullptr, 10);
      if (errno == 0 && user_specified_max != 0) {
        process->SetUserSpecifiedMaxMemoryTransferSize(user_specified_max);
        result.SetStatus(eReturnStatusSuccessFinishResult);
        return;
      }
    }
    result.SetStatus(eReturnStatusFailed);
  }
};

class CommandObjectProcessGDBRemotePacketSend : public CommandObjectParsed {
private:
public:
  CommandObjectProcessGDBRemotePacketSend(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process plugin packet send",
                            "Send a custom packet through the GDB remote "
                            "protocol and print the answer. "
                            "The packet header and footer will automatically "
                            "be added to the packet prior to sending and "
                            "stripped from the result.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeNone, eArgRepeatStar);
  }

  ~CommandObjectProcessGDBRemotePacketSend() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      result.AppendErrorWithFormat(
          "'%s' takes a one or more packet content arguments",
          m_cmd_name.c_str());
      return;
    }

    ProcessGDBRemote *process =
        (ProcessGDBRemote *)m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process) {
      for (size_t i = 0; i < argc; ++i) {
        const char *packet_cstr = command.GetArgumentAtIndex(0);
        StringExtractorGDBRemote response;
        process->GetGDBRemote().SendPacketAndWaitForResponse(
            packet_cstr, response, process->GetInterruptTimeout());
        result.SetStatus(eReturnStatusSuccessFinishResult);
        Stream &output_strm = result.GetOutputStream();
        output_strm.Printf("  packet: %s\n", packet_cstr);
        std::string response_str = std::string(response.GetStringRef());

        if (strstr(packet_cstr, "qGetProfileData") != nullptr) {
          response_str = process->HarmonizeThreadIdsForProfileData(response);
        }

        if (response_str.empty())
          output_strm.PutCString("response: \nerror: UNIMPLEMENTED\n");
        else
          output_strm.Printf("response: %s\n", response.GetStringRef().data());
      }
    }
  }
};

class CommandObjectProcessGDBRemotePacketMonitor : public CommandObjectRaw {
private:
public:
  CommandObjectProcessGDBRemotePacketMonitor(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "process plugin packet monitor",
                         "Send a qRcmd packet through the GDB remote protocol "
                         "and print the response."
                         "The argument passed to this command will be hex "
                         "encoded into a valid 'qRcmd' packet, sent and the "
                         "response will be printed.") {}

  ~CommandObjectProcessGDBRemotePacketMonitor() override = default;

  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    if (command.empty()) {
      result.AppendErrorWithFormat("'%s' takes a command string argument",
                                   m_cmd_name.c_str());
      return;
    }

    ProcessGDBRemote *process =
        (ProcessGDBRemote *)m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process) {
      StreamString packet;
      packet.PutCString("qRcmd,");
      packet.PutBytesAsRawHex8(command.data(), command.size());

      StringExtractorGDBRemote response;
      Stream &output_strm = result.GetOutputStream();
      process->GetGDBRemote().SendPacketAndReceiveResponseWithOutputSupport(
          packet.GetString(), response, process->GetInterruptTimeout(),
          [&output_strm](llvm::StringRef output) { output_strm << output; });
      result.SetStatus(eReturnStatusSuccessFinishResult);
      output_strm.Printf("  packet: %s\n", packet.GetData());
      const std::string &response_str = std::string(response.GetStringRef());

      if (response_str.empty())
        output_strm.PutCString("response: \nerror: UNIMPLEMENTED\n");
      else
        output_strm.Printf("response: %s\n", response.GetStringRef().data());
    }
  }
};

class CommandObjectProcessGDBRemotePacket : public CommandObjectMultiword {
private:
public:
  CommandObjectProcessGDBRemotePacket(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "process plugin packet",
                               "Commands that deal with GDB remote packets.",
                               nullptr) {
    LoadSubCommand(
        "history",
        CommandObjectSP(
            new CommandObjectProcessGDBRemotePacketHistory(interpreter)));
    LoadSubCommand(
        "send", CommandObjectSP(
                    new CommandObjectProcessGDBRemotePacketSend(interpreter)));
    LoadSubCommand(
        "monitor",
        CommandObjectSP(
            new CommandObjectProcessGDBRemotePacketMonitor(interpreter)));
    LoadSubCommand(
        "xfer-size",
        CommandObjectSP(
            new CommandObjectProcessGDBRemotePacketXferSize(interpreter)));
    LoadSubCommand("speed-test",
                   CommandObjectSP(new CommandObjectProcessGDBRemoteSpeedTest(
                       interpreter)));
  }

  ~CommandObjectProcessGDBRemotePacket() override = default;
};

class CommandObjectMultiwordProcessGDBRemote : public CommandObjectMultiword {
public:
  CommandObjectMultiwordProcessGDBRemote(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "process plugin",
            "Commands for operating on a ProcessGDBRemote process.",
            "process plugin <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "packet",
        CommandObjectSP(new CommandObjectProcessGDBRemotePacket(interpreter)));
  }

  ~CommandObjectMultiwordProcessGDBRemote() override = default;
};

CommandObject *ProcessGDBRemote::GetPluginCommandObject() {
  if (!m_command_sp)
    m_command_sp = std::make_shared<CommandObjectMultiwordProcessGDBRemote>(
        GetTarget().GetDebugger().GetCommandInterpreter());
  return m_command_sp.get();
}

void ProcessGDBRemote::DidForkSwitchSoftwareBreakpoints(bool enable) {
  GetBreakpointSiteList().ForEach([this, enable](BreakpointSite *bp_site) {
    if (bp_site->IsEnabled() &&
        (bp_site->GetType() == BreakpointSite::eSoftware ||
         bp_site->GetType() == BreakpointSite::eExternal)) {
      m_gdb_comm.SendGDBStoppointTypePacket(
          eBreakpointSoftware, enable, bp_site->GetLoadAddress(),
          GetSoftwareBreakpointTrapOpcode(bp_site), GetInterruptTimeout());
    }
  });
}

void ProcessGDBRemote::DidForkSwitchHardwareTraps(bool enable) {
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointHardware)) {
    GetBreakpointSiteList().ForEach([this, enable](BreakpointSite *bp_site) {
      if (bp_site->IsEnabled() &&
          bp_site->GetType() == BreakpointSite::eHardware) {
        m_gdb_comm.SendGDBStoppointTypePacket(
            eBreakpointHardware, enable, bp_site->GetLoadAddress(),
            GetSoftwareBreakpointTrapOpcode(bp_site), GetInterruptTimeout());
      }
    });
  }

  for (const auto &wp_res_sp : m_watchpoint_resource_list.Sites()) {
    addr_t addr = wp_res_sp->GetLoadAddress();
    size_t size = wp_res_sp->GetByteSize();
    GDBStoppointType type = GetGDBStoppointType(wp_res_sp);
    m_gdb_comm.SendGDBStoppointTypePacket(type, enable, addr, size,
                                          GetInterruptTimeout());
  }
}

void ProcessGDBRemote::DidFork(lldb::pid_t child_pid, lldb::tid_t child_tid) {
  Log *log = GetLog(GDBRLog::Process);

  lldb::pid_t parent_pid = m_gdb_comm.GetCurrentProcessID();
  // Any valid TID will suffice, thread-relevant actions will set a proper TID
  // anyway.
  lldb::tid_t parent_tid = m_thread_ids.front();

  lldb::pid_t follow_pid, detach_pid;
  lldb::tid_t follow_tid, detach_tid;

  switch (GetFollowForkMode()) {
  case eFollowParent:
    follow_pid = parent_pid;
    follow_tid = parent_tid;
    detach_pid = child_pid;
    detach_tid = child_tid;
    break;
  case eFollowChild:
    follow_pid = child_pid;
    follow_tid = child_tid;
    detach_pid = parent_pid;
    detach_tid = parent_tid;
    break;
  }

  // Switch to the process that is going to be detached.
  if (!m_gdb_comm.SetCurrentThread(detach_tid, detach_pid)) {
    LLDB_LOG(log, "ProcessGDBRemote::DidFork() unable to set pid/tid");
    return;
  }

  // Disable all software breakpoints in the forked process.
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointSoftware))
    DidForkSwitchSoftwareBreakpoints(false);

  // Remove hardware breakpoints / watchpoints from parent process if we're
  // following child.
  if (GetFollowForkMode() == eFollowChild)
    DidForkSwitchHardwareTraps(false);

  // Switch to the process that is going to be followed
  if (!m_gdb_comm.SetCurrentThread(follow_tid, follow_pid) ||
      !m_gdb_comm.SetCurrentThreadForRun(follow_tid, follow_pid)) {
    LLDB_LOG(log, "ProcessGDBRemote::DidFork() unable to reset pid/tid");
    return;
  }

  LLDB_LOG(log, "Detaching process {0}", detach_pid);
  Status error = m_gdb_comm.Detach(false, detach_pid);
  if (error.Fail()) {
    LLDB_LOG(log, "ProcessGDBRemote::DidFork() detach packet send failed: {0}",
             error.AsCString() ? error.AsCString() : "<unknown error>");
    return;
  }

  // Hardware breakpoints/watchpoints are not inherited implicitly,
  // so we need to readd them if we're following child.
  if (GetFollowForkMode() == eFollowChild) {
    DidForkSwitchHardwareTraps(true);
    // Update our PID
    SetID(child_pid);
  }
}

void ProcessGDBRemote::DidVFork(lldb::pid_t child_pid, lldb::tid_t child_tid) {
  Log *log = GetLog(GDBRLog::Process);

  LLDB_LOG(
      log,
      "ProcessGDBRemote::DidFork() called for child_pid: {0}, child_tid {1}",
      child_pid, child_tid);
  ++m_vfork_in_progress_count;

  // Disable all software breakpoints for the duration of vfork.
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointSoftware))
    DidForkSwitchSoftwareBreakpoints(false);

  lldb::pid_t detach_pid;
  lldb::tid_t detach_tid;

  switch (GetFollowForkMode()) {
  case eFollowParent:
    detach_pid = child_pid;
    detach_tid = child_tid;
    break;
  case eFollowChild:
    detach_pid = m_gdb_comm.GetCurrentProcessID();
    // Any valid TID will suffice, thread-relevant actions will set a proper TID
    // anyway.
    detach_tid = m_thread_ids.front();

    // Switch to the parent process before detaching it.
    if (!m_gdb_comm.SetCurrentThread(detach_tid, detach_pid)) {
      LLDB_LOG(log, "ProcessGDBRemote::DidFork() unable to set pid/tid");
      return;
    }

    // Remove hardware breakpoints / watchpoints from the parent process.
    DidForkSwitchHardwareTraps(false);

    // Switch to the child process.
    if (!m_gdb_comm.SetCurrentThread(child_tid, child_pid) ||
        !m_gdb_comm.SetCurrentThreadForRun(child_tid, child_pid)) {
      LLDB_LOG(log, "ProcessGDBRemote::DidFork() unable to reset pid/tid");
      return;
    }
    break;
  }

  LLDB_LOG(log, "Detaching process {0}", detach_pid);
  Status error = m_gdb_comm.Detach(false, detach_pid);
  if (error.Fail()) {
      LLDB_LOG(log,
               "ProcessGDBRemote::DidFork() detach packet send failed: {0}",
                error.AsCString() ? error.AsCString() : "<unknown error>");
      return;
  }

  if (GetFollowForkMode() == eFollowChild) {
    // Update our PID
    SetID(child_pid);
  }
}

void ProcessGDBRemote::DidVForkDone() {
  assert(m_vfork_in_progress_count > 0);
  --m_vfork_in_progress_count;

  // Reenable all software breakpoints that were enabled before vfork.
  if (m_gdb_comm.SupportsGDBStoppointPacket(eBreakpointSoftware))
    DidForkSwitchSoftwareBreakpoints(true);
}

void ProcessGDBRemote::DidExec() {
  // If we are following children, vfork is finished by exec (rather than
  // vforkdone that is submitted for parent).
  if (GetFollowForkMode() == eFollowChild) {
    if (m_vfork_in_progress_count > 0)
      --m_vfork_in_progress_count;
  }
  Process::DidExec();
}
