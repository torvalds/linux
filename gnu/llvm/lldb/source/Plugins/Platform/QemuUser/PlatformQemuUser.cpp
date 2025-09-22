//===-- PlatformQemuUser.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Platform/QemuUser/PlatformQemuUser.h"
#include "Plugins/Process/gdb-remote/ProcessGDBRemote.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(PlatformQemuUser)

namespace {
#define LLDB_PROPERTIES_platformqemuuser
#include "PlatformQemuUserProperties.inc"

enum {
#define LLDB_PROPERTIES_platformqemuuser
#include "PlatformQemuUserPropertiesEnum.inc"
};

class PluginProperties : public Properties {
public:
  PluginProperties() {
    m_collection_sp = std::make_shared<OptionValueProperties>(
        PlatformQemuUser::GetPluginNameStatic());
    m_collection_sp->Initialize(g_platformqemuuser_properties);
  }

  llvm::StringRef GetArchitecture() {
    return GetPropertyAtIndexAs<llvm::StringRef>(ePropertyArchitecture, "");
  }

  FileSpec GetEmulatorPath() {
    return GetPropertyAtIndexAs<FileSpec>(ePropertyEmulatorPath, {});
  }

  Args GetEmulatorArgs() {
    Args result;
    m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyEmulatorArgs, result);
    return result;
  }

  Environment GetEmulatorEnvVars() {
    Args args;
    m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyEmulatorEnvVars, args);
    return Environment(args);
  }

  Environment GetTargetEnvVars() {
    Args args;
    m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyTargetEnvVars, args);
    return Environment(args);
  }
};

} // namespace

static PluginProperties &GetGlobalProperties() {
  static PluginProperties g_settings;
  return g_settings;
}

llvm::StringRef PlatformQemuUser::GetPluginDescriptionStatic() {
  return "Platform for debugging binaries under user mode qemu";
}

void PlatformQemuUser::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), GetPluginDescriptionStatic(),
      PlatformQemuUser::CreateInstance, PlatformQemuUser::DebuggerInitialize);
}

void PlatformQemuUser::Terminate() {
  PluginManager::UnregisterPlugin(PlatformQemuUser::CreateInstance);
}

void PlatformQemuUser::DebuggerInitialize(Debugger &debugger) {
  if (!PluginManager::GetSettingForPlatformPlugin(debugger,
                                                  GetPluginNameStatic())) {
    PluginManager::CreateSettingForPlatformPlugin(
        debugger, GetGlobalProperties().GetValueProperties(),
        "Properties for the qemu-user platform plugin.",
        /*is_global_property=*/true);
  }
}

PlatformSP PlatformQemuUser::CreateInstance(bool force, const ArchSpec *arch) {
  if (force)
    return PlatformSP(new PlatformQemuUser());
  return nullptr;
}

std::vector<ArchSpec>
PlatformQemuUser::GetSupportedArchitectures(const ArchSpec &process_host_arch) {
  llvm::Triple triple = HostInfo::GetArchitecture().GetTriple();
  triple.setEnvironment(llvm::Triple::UnknownEnvironment);
  triple.setArchName(GetGlobalProperties().GetArchitecture());
  if (triple.getArch() != llvm::Triple::UnknownArch)
    return {ArchSpec(triple)};
  return {};
}

static auto get_arg_range(const Args &args) {
  return llvm::make_range(args.GetArgumentArrayRef().begin(),
                          args.GetArgumentArrayRef().end());
}

// Returns the emulator environment which result in the desired environment
// being presented to the emulated process. We want to be careful about
// preserving the host environment, as it may contain entries (LD_LIBRARY_PATH,
// for example) needed for the operation of the emulator itself.
static Environment ComputeLaunchEnvironment(Environment target,
                                            Environment host) {
  std::vector<std::string> set_env;
  for (const auto &KV : target) {
    // If the host value differs from the target (or is unset), then set it
    // through QEMU_SET_ENV. Identical entries will be forwarded automatically.
    auto host_it = host.find(KV.first());
    if (host_it == host.end() || host_it->second != KV.second)
      set_env.push_back(Environment::compose(KV));
  }
  llvm::sort(set_env);

  std::vector<llvm::StringRef> unset_env;
  for (const auto &KV : host) {
    // If the target is missing some host entries, then unset them through
    // QEMU_UNSET_ENV.
    if (target.count(KV.first()) == 0)
      unset_env.push_back(KV.first());
  }
  llvm::sort(unset_env);

  // The actual QEMU_(UN)SET_ENV variables should not be forwarded to the
  // target.
  if (!set_env.empty()) {
    host["QEMU_SET_ENV"] = llvm::join(set_env, ",");
    unset_env.push_back("QEMU_SET_ENV");
  }
  if (!unset_env.empty()) {
    unset_env.push_back("QEMU_UNSET_ENV");
    host["QEMU_UNSET_ENV"] = llvm::join(unset_env, ",");
  }
  return host;
}

lldb::ProcessSP PlatformQemuUser::DebugProcess(ProcessLaunchInfo &launch_info,
                                               Debugger &debugger,
                                               Target &target, Status &error) {
  Log *log = GetLog(LLDBLog::Platform);

  // If platform.plugin.qemu-user.emulator-path is set, use it.
  FileSpec qemu = GetGlobalProperties().GetEmulatorPath();
  // If platform.plugin.qemu-user.emulator-path is not set, build the
  // executable name from platform.plugin.qemu-user.architecture.
  if (!qemu) {
    llvm::StringRef arch = GetGlobalProperties().GetArchitecture();
    // If platform.plugin.qemu-user.architecture is not set, build the
    // executable name from the target Triple's ArchName
    if (arch.empty())
      arch = target.GetArchitecture().GetTriple().getArchName();
    qemu.SetPath(("qemu-" + arch).str());
  }
  FileSystem::Instance().ResolveExecutableLocation(qemu);

  llvm::SmallString<0> socket_model, socket_path;
  HostInfo::GetProcessTempDir().GetPath(socket_model);
  llvm::sys::path::append(socket_model, "qemu-%%%%%%%%.socket");
  do {
    llvm::sys::fs::createUniquePath(socket_model, socket_path, false);
  } while (FileSystem::Instance().Exists(socket_path));

  Args args({qemu.GetPath(), "-g", socket_path});
  if (!launch_info.GetArg0().empty()) {
    args.AppendArgument("-0");
    args.AppendArgument(launch_info.GetArg0());
  }
  args.AppendArguments(GetGlobalProperties().GetEmulatorArgs());
  args.AppendArgument("--");
  args.AppendArgument(launch_info.GetExecutableFile().GetPath());
  for (size_t i = 1; i < launch_info.GetArguments().size(); ++i)
    args.AppendArgument(launch_info.GetArguments()[i].ref());

  LLDB_LOG(log, "{0} -> {1}", get_arg_range(launch_info.GetArguments()),
           get_arg_range(args));

  launch_info.SetArguments(args, true);

  Environment emulator_env = Host::GetEnvironment();
  if (const std::string &sysroot = GetSDKRootDirectory(); !sysroot.empty())
    emulator_env["QEMU_LD_PREFIX"] = sysroot;
  for (const auto &KV : GetGlobalProperties().GetEmulatorEnvVars())
    emulator_env[KV.first()] = KV.second;
  launch_info.GetEnvironment() = ComputeLaunchEnvironment(
      std::move(launch_info.GetEnvironment()), std::move(emulator_env));

  launch_info.SetLaunchInSeparateProcessGroup(true);
  launch_info.GetFlags().Clear(eLaunchFlagDebug);
  launch_info.SetMonitorProcessCallback(ProcessLaunchInfo::NoOpMonitorCallback);

  // This is automatically done for host platform in
  // Target::FinalizeFileActions, but we're not a host platform.
  llvm::Error Err = launch_info.SetUpPtyRedirection();
  LLDB_LOG_ERROR(log, std::move(Err), "SetUpPtyRedirection failed: {0}");

  error = Host::LaunchProcess(launch_info);
  if (error.Fail())
    return nullptr;

  ProcessSP process_sp = target.CreateProcess(
      launch_info.GetListener(),
      process_gdb_remote::ProcessGDBRemote::GetPluginNameStatic(), nullptr,
      true);
  if (!process_sp) {
    error.SetErrorString("Failed to create GDB process");
    return nullptr;
  }

  process_sp->HijackProcessEvents(launch_info.GetHijackListener());

  error = process_sp->ConnectRemote(("unix-connect://" + socket_path).str());
  if (error.Fail())
    return nullptr;

  if (launch_info.GetPTY().GetPrimaryFileDescriptor() !=
      PseudoTerminal::invalid_fd)
    process_sp->SetSTDIOFileDescriptor(
        launch_info.GetPTY().ReleasePrimaryFileDescriptor());

  return process_sp;
}

Environment PlatformQemuUser::GetEnvironment() {
  Environment env = Host::GetEnvironment();
  for (const auto &KV : GetGlobalProperties().GetTargetEnvVars())
    env[KV.first()] = KV.second;
  return env;
}
