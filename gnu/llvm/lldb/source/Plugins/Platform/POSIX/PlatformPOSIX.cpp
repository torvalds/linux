//===-- PlatformPOSIX.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformPOSIX.h"

#include "Plugins/Platform/gdb-server/PlatformRemoteGDBServer.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/File.h"
#include "lldb/Host/FileCache.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/ScopeExit.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

/// Default Constructor
PlatformPOSIX::PlatformPOSIX(bool is_host)
    : RemoteAwarePlatform(is_host), // This is the local host platform
      m_option_group_platform_rsync(new OptionGroupPlatformRSync()),
      m_option_group_platform_ssh(new OptionGroupPlatformSSH()),
      m_option_group_platform_caching(new OptionGroupPlatformCaching()) {}

/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
PlatformPOSIX::~PlatformPOSIX() = default;

lldb_private::OptionGroupOptions *PlatformPOSIX::GetConnectionOptions(
    lldb_private::CommandInterpreter &interpreter) {
  auto iter = m_options.find(&interpreter), end = m_options.end();
  if (iter == end) {
    std::unique_ptr<lldb_private::OptionGroupOptions> options(
        new OptionGroupOptions());
    options->Append(m_option_group_platform_rsync.get());
    options->Append(m_option_group_platform_ssh.get());
    options->Append(m_option_group_platform_caching.get());
    m_options[&interpreter] = std::move(options);
  }

  return m_options.at(&interpreter).get();
}

static uint32_t chown_file(Platform *platform, const char *path,
                           uint32_t uid = UINT32_MAX,
                           uint32_t gid = UINT32_MAX) {
  if (!platform || !path || *path == 0)
    return UINT32_MAX;

  if (uid == UINT32_MAX && gid == UINT32_MAX)
    return 0; // pretend I did chown correctly - actually I just didn't care

  StreamString command;
  command.PutCString("chown ");
  if (uid != UINT32_MAX)
    command.Printf("%d", uid);
  if (gid != UINT32_MAX)
    command.Printf(":%d", gid);
  command.Printf("%s", path);
  int status;
  platform->RunShellCommand(command.GetData(), FileSpec(), &status, nullptr,
                            nullptr, std::chrono::seconds(10));
  return status;
}

lldb_private::Status
PlatformPOSIX::PutFile(const lldb_private::FileSpec &source,
                       const lldb_private::FileSpec &destination, uint32_t uid,
                       uint32_t gid) {
  Log *log = GetLog(LLDBLog::Platform);

  if (IsHost()) {
    if (source == destination)
      return Status();
    // cp src dst
    // chown uid:gid dst
    std::string src_path(source.GetPath());
    if (src_path.empty())
      return Status("unable to get file path for source");
    std::string dst_path(destination.GetPath());
    if (dst_path.empty())
      return Status("unable to get file path for destination");
    StreamString command;
    command.Printf("cp %s %s", src_path.c_str(), dst_path.c_str());
    int status;
    RunShellCommand(command.GetData(), FileSpec(), &status, nullptr, nullptr,
                    std::chrono::seconds(10));
    if (status != 0)
      return Status("unable to perform copy");
    if (uid == UINT32_MAX && gid == UINT32_MAX)
      return Status();
    if (chown_file(this, dst_path.c_str(), uid, gid) != 0)
      return Status("unable to perform chown");
    return Status();
  } else if (m_remote_platform_sp) {
    if (GetSupportsRSync()) {
      std::string src_path(source.GetPath());
      if (src_path.empty())
        return Status("unable to get file path for source");
      std::string dst_path(destination.GetPath());
      if (dst_path.empty())
        return Status("unable to get file path for destination");
      StreamString command;
      if (GetIgnoresRemoteHostname()) {
        if (!GetRSyncPrefix())
          command.Printf("rsync %s %s %s", GetRSyncOpts(), src_path.c_str(),
                         dst_path.c_str());
        else
          command.Printf("rsync %s %s %s%s", GetRSyncOpts(), src_path.c_str(),
                         GetRSyncPrefix(), dst_path.c_str());
      } else
        command.Printf("rsync %s %s %s:%s", GetRSyncOpts(), src_path.c_str(),
                       GetHostname(), dst_path.c_str());
      LLDB_LOGF(log, "[PutFile] Running command: %s\n", command.GetData());
      int retcode;
      Host::RunShellCommand(command.GetData(), FileSpec(), &retcode, nullptr,
                            nullptr, std::chrono::minutes(1));
      if (retcode == 0) {
        // Don't chown a local file for a remote system
        //                if (chown_file(this,dst_path.c_str(),uid,gid) != 0)
        //                    return Status("unable to perform chown");
        return Status();
      }
      // if we are still here rsync has failed - let's try the slow way before
      // giving up
    }
  }
  return Platform::PutFile(source, destination, uid, gid);
}

lldb_private::Status PlatformPOSIX::GetFile(
    const lldb_private::FileSpec &source,      // remote file path
    const lldb_private::FileSpec &destination) // local file path
{
  Log *log = GetLog(LLDBLog::Platform);

  // Check the args, first.
  std::string src_path(source.GetPath());
  if (src_path.empty())
    return Status("unable to get file path for source");
  std::string dst_path(destination.GetPath());
  if (dst_path.empty())
    return Status("unable to get file path for destination");
  if (IsHost()) {
    if (source == destination)
      return Status("local scenario->source and destination are the same file "
                    "path: no operation performed");
    // cp src dst
    StreamString cp_command;
    cp_command.Printf("cp %s %s", src_path.c_str(), dst_path.c_str());
    int status;
    RunShellCommand(cp_command.GetData(), FileSpec(), &status, nullptr, nullptr,
                    std::chrono::seconds(10));
    if (status != 0)
      return Status("unable to perform copy");
    return Status();
  } else if (m_remote_platform_sp) {
    if (GetSupportsRSync()) {
      StreamString command;
      if (GetIgnoresRemoteHostname()) {
        if (!GetRSyncPrefix())
          command.Printf("rsync %s %s %s", GetRSyncOpts(), src_path.c_str(),
                         dst_path.c_str());
        else
          command.Printf("rsync %s %s%s %s", GetRSyncOpts(), GetRSyncPrefix(),
                         src_path.c_str(), dst_path.c_str());
      } else
        command.Printf("rsync %s %s:%s %s", GetRSyncOpts(),
                       m_remote_platform_sp->GetHostname(), src_path.c_str(),
                       dst_path.c_str());
      LLDB_LOGF(log, "[GetFile] Running command: %s\n", command.GetData());
      int retcode;
      Host::RunShellCommand(command.GetData(), FileSpec(), &retcode, nullptr,
                            nullptr, std::chrono::minutes(1));
      if (retcode == 0)
        return Status();
      // If we are here, rsync has failed - let's try the slow way before
      // giving up
    }
    // open src and dst
    // read/write, read/write, read/write, ...
    // close src
    // close dst
    LLDB_LOGF(log, "[GetFile] Using block by block transfer....\n");
    Status error;
    user_id_t fd_src = OpenFile(source, File::eOpenOptionReadOnly,
                                lldb::eFilePermissionsFileDefault, error);

    if (fd_src == UINT64_MAX)
      return Status("unable to open source file");

    uint32_t permissions = 0;
    error = GetFilePermissions(source, permissions);

    if (permissions == 0)
      permissions = lldb::eFilePermissionsFileDefault;

    user_id_t fd_dst = FileCache::GetInstance().OpenFile(
        destination, File::eOpenOptionCanCreate | File::eOpenOptionWriteOnly |
                         File::eOpenOptionTruncate,
        permissions, error);

    if (fd_dst == UINT64_MAX) {
      if (error.Success())
        error.SetErrorString("unable to open destination file");
    }

    if (error.Success()) {
      lldb::WritableDataBufferSP buffer_sp(new DataBufferHeap(1024, 0));
      uint64_t offset = 0;
      error.Clear();
      while (error.Success()) {
        const uint64_t n_read = ReadFile(fd_src, offset, buffer_sp->GetBytes(),
                                         buffer_sp->GetByteSize(), error);
        if (error.Fail())
          break;
        if (n_read == 0)
          break;
        if (FileCache::GetInstance().WriteFile(fd_dst, offset,
                                               buffer_sp->GetBytes(), n_read,
                                               error) != n_read) {
          if (!error.Fail())
            error.SetErrorString("unable to write to destination file");
          break;
        }
        offset += n_read;
      }
    }
    // Ignore the close error of src.
    if (fd_src != UINT64_MAX)
      CloseFile(fd_src, error);
    // And close the dst file descriptot.
    if (fd_dst != UINT64_MAX &&
        !FileCache::GetInstance().CloseFile(fd_dst, error)) {
      if (!error.Fail())
        error.SetErrorString("unable to close destination file");
    }
    return error;
  }
  return Platform::GetFile(source, destination);
}

std::string PlatformPOSIX::GetPlatformSpecificConnectionInformation() {
  StreamString stream;
  if (GetSupportsRSync()) {
    stream.PutCString("rsync");
    if ((GetRSyncOpts() && *GetRSyncOpts()) ||
        (GetRSyncPrefix() && *GetRSyncPrefix()) || GetIgnoresRemoteHostname()) {
      stream.Printf(", options: ");
      if (GetRSyncOpts() && *GetRSyncOpts())
        stream.Printf("'%s' ", GetRSyncOpts());
      stream.Printf(", prefix: ");
      if (GetRSyncPrefix() && *GetRSyncPrefix())
        stream.Printf("'%s' ", GetRSyncPrefix());
      if (GetIgnoresRemoteHostname())
        stream.Printf("ignore remote-hostname ");
    }
  }
  if (GetSupportsSSH()) {
    stream.PutCString("ssh");
    if (GetSSHOpts() && *GetSSHOpts())
      stream.Printf(", options: '%s' ", GetSSHOpts());
  }
  if (GetLocalCacheDirectory() && *GetLocalCacheDirectory())
    stream.Printf("cache dir: %s", GetLocalCacheDirectory());
  if (stream.GetSize())
    return std::string(stream.GetString());
  else
    return "";
}

const lldb::UnixSignalsSP &PlatformPOSIX::GetRemoteUnixSignals() {
  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteUnixSignals();
  return Platform::GetRemoteUnixSignals();
}

Status PlatformPOSIX::ConnectRemote(Args &args) {
  Status error;
  if (IsHost()) {
    error.SetErrorStringWithFormatv(
        "can't connect to the host platform '{0}', always connected",
        GetPluginName());
  } else {
    if (!m_remote_platform_sp)
      m_remote_platform_sp =
          platform_gdb_server::PlatformRemoteGDBServer::CreateInstance(
              /*force=*/true, nullptr);

    if (m_remote_platform_sp && error.Success())
      error = m_remote_platform_sp->ConnectRemote(args);
    else
      error.SetErrorString("failed to create a 'remote-gdb-server' platform");

    if (error.Fail())
      m_remote_platform_sp.reset();
  }

  if (error.Success() && m_remote_platform_sp) {
    if (m_option_group_platform_rsync.get() &&
        m_option_group_platform_ssh.get() &&
        m_option_group_platform_caching.get()) {
      if (m_option_group_platform_rsync->m_rsync) {
        SetSupportsRSync(true);
        SetRSyncOpts(m_option_group_platform_rsync->m_rsync_opts.c_str());
        SetRSyncPrefix(m_option_group_platform_rsync->m_rsync_prefix.c_str());
        SetIgnoresRemoteHostname(
            m_option_group_platform_rsync->m_ignores_remote_hostname);
      }
      if (m_option_group_platform_ssh->m_ssh) {
        SetSupportsSSH(true);
        SetSSHOpts(m_option_group_platform_ssh->m_ssh_opts.c_str());
      }
      SetLocalCacheDirectory(
          m_option_group_platform_caching->m_cache_dir.c_str());
    }
  }

  return error;
}

Status PlatformPOSIX::DisconnectRemote() {
  Status error;

  if (IsHost()) {
    error.SetErrorStringWithFormatv(
        "can't disconnect from the host platform '{0}', always connected",
        GetPluginName());
  } else {
    if (m_remote_platform_sp)
      error = m_remote_platform_sp->DisconnectRemote();
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return error;
}

lldb::ProcessSP PlatformPOSIX::Attach(ProcessAttachInfo &attach_info,
                                      Debugger &debugger, Target *target,
                                      Status &error) {
  lldb::ProcessSP process_sp;
  Log *log = GetLog(LLDBLog::Platform);

  if (IsHost()) {
    if (target == nullptr) {
      TargetSP new_target_sp;

      error = debugger.GetTargetList().CreateTarget(
          debugger, "", "", eLoadDependentsNo, nullptr, new_target_sp);
      target = new_target_sp.get();
      LLDB_LOGF(log, "PlatformPOSIX::%s created new target", __FUNCTION__);
    } else {
      error.Clear();
      LLDB_LOGF(log, "PlatformPOSIX::%s target already existed, setting target",
                __FUNCTION__);
    }

    if (target && error.Success()) {
      if (log) {
        ModuleSP exe_module_sp = target->GetExecutableModule();
        LLDB_LOGF(log, "PlatformPOSIX::%s set selected target to %p %s",
                  __FUNCTION__, (void *)target,
                  exe_module_sp ? exe_module_sp->GetFileSpec().GetPath().c_str()
                                : "<null>");
      }

      process_sp =
          target->CreateProcess(attach_info.GetListenerForProcess(debugger),
                                "gdb-remote", nullptr, true);

      if (process_sp) {
        ListenerSP listener_sp = attach_info.GetHijackListener();
        if (listener_sp == nullptr) {
          listener_sp =
              Listener::MakeListener("lldb.PlatformPOSIX.attach.hijack");
          attach_info.SetHijackListener(listener_sp);
        }
        process_sp->HijackProcessEvents(listener_sp);
        process_sp->SetShadowListener(attach_info.GetShadowListener());
        error = process_sp->Attach(attach_info);
      }
    }
  } else {
    if (m_remote_platform_sp)
      process_sp =
          m_remote_platform_sp->Attach(attach_info, debugger, target, error);
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return process_sp;
}

lldb::ProcessSP PlatformPOSIX::DebugProcess(ProcessLaunchInfo &launch_info,
                                            Debugger &debugger, Target &target,
                                            Status &error) {
  Log *log = GetLog(LLDBLog::Platform);
  LLDB_LOG(log, "target {0}", &target);

  ProcessSP process_sp;

  if (!IsHost()) {
    if (m_remote_platform_sp)
      process_sp = m_remote_platform_sp->DebugProcess(launch_info, debugger,
                                                      target, error);
    else
      error.SetErrorString("the platform is not currently connected");
    return process_sp;
  }

  //
  // For local debugging, we'll insist on having ProcessGDBRemote create the
  // process.
  //

  // Make sure we stop at the entry point
  launch_info.GetFlags().Set(eLaunchFlagDebug);

  // We always launch the process we are going to debug in a separate process
  // group, since then we can handle ^C interrupts ourselves w/o having to
  // worry about the target getting them as well.
  launch_info.SetLaunchInSeparateProcessGroup(true);

  // Now create the gdb-remote process.
  LLDB_LOG(log, "having target create process with gdb-remote plugin");
  process_sp = target.CreateProcess(launch_info.GetListener(), "gdb-remote",
                                    nullptr, true);

  if (!process_sp) {
    error.SetErrorString("CreateProcess() failed for gdb-remote process");
    LLDB_LOG(log, "error: {0}", error);
    return process_sp;
  }

  LLDB_LOG(log, "successfully created process");

  process_sp->HijackProcessEvents(launch_info.GetHijackListener());
  process_sp->SetShadowListener(launch_info.GetShadowListener());

  // Log file actions.
  if (log) {
    LLDB_LOG(log, "launching process with the following file actions:");
    StreamString stream;
    size_t i = 0;
    const FileAction *file_action;
    while ((file_action = launch_info.GetFileActionAtIndex(i++)) != nullptr) {
      file_action->Dump(stream);
      LLDB_LOG(log, "{0}", stream.GetData());
      stream.Clear();
    }
  }

  // Do the launch.
  error = process_sp->Launch(launch_info);
  if (error.Success()) {
    // Hook up process PTY if we have one (which we should for local debugging
    // with llgs).
    int pty_fd = launch_info.GetPTY().ReleasePrimaryFileDescriptor();
    if (pty_fd != PseudoTerminal::invalid_fd) {
      process_sp->SetSTDIOFileDescriptor(pty_fd);
      LLDB_LOG(log, "hooked up STDIO pty to process");
    } else
      LLDB_LOG(log, "not using process STDIO pty");
  } else {
    LLDB_LOG(log, "{0}", error);
    // FIXME figure out appropriate cleanup here. Do we delete the process?
    // Does our caller do that?
  }

  return process_sp;
}

void PlatformPOSIX::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

Status PlatformPOSIX::EvaluateLibdlExpression(
    lldb_private::Process *process, const char *expr_cstr,
    llvm::StringRef expr_prefix, lldb::ValueObjectSP &result_valobj_sp) {
  DynamicLoader *loader = process->GetDynamicLoader();
  if (loader) {
    Status error = loader->CanLoadImage();
    if (error.Fail())
      return error;
  }

  ThreadSP thread_sp(process->GetThreadList().GetExpressionExecutionThread());
  if (!thread_sp)
    return Status("Selected thread isn't valid");

  StackFrameSP frame_sp(thread_sp->GetStackFrameAtIndex(0));
  if (!frame_sp)
    return Status("Frame 0 isn't valid");

  ExecutionContext exe_ctx;
  frame_sp->CalculateExecutionContext(exe_ctx);
  EvaluateExpressionOptions expr_options;
  expr_options.SetUnwindOnError(true);
  expr_options.SetIgnoreBreakpoints(true);
  expr_options.SetExecutionPolicy(eExecutionPolicyAlways);
  expr_options.SetLanguage(eLanguageTypeC_plus_plus);
  expr_options.SetTrapExceptions(false); // dlopen can't throw exceptions, so
                                         // don't do the work to trap them.
  expr_options.SetTimeout(process->GetUtilityExpressionTimeout());

  Status expr_error;
  ExpressionResults result =
      UserExpression::Evaluate(exe_ctx, expr_options, expr_cstr, expr_prefix,
                               result_valobj_sp, expr_error);
  if (result != eExpressionCompleted)
    return expr_error;

  if (result_valobj_sp->GetError().Fail())
    return result_valobj_sp->GetError();
  return Status();
}

std::unique_ptr<UtilityFunction>
PlatformPOSIX::MakeLoadImageUtilityFunction(ExecutionContext &exe_ctx,
                                            Status &error) {
  // Remember to prepend this with the prefix from
  // GetLibdlFunctionDeclarations. The returned values are all in
  // __lldb_dlopen_result for consistency. The wrapper returns a void * but
  // doesn't use it because UtilityFunctions don't work with void returns at
  // present.
  //
  // Use lazy binding so as to not make dlopen()'s success conditional on
  // forcing every symbol in the library.
  //
  // In general, the debugger should allow programs to load & run with
  // libraries as far as they can, instead of defaulting to being super-picky
  // about unavailable symbols.
  //
  // The value "1" appears to imply lazy binding (RTLD_LAZY) on both Darwin
  // and other POSIX OSes.
  static const char *dlopen_wrapper_code = R"(
  const int RTLD_LAZY = 1;

  struct __lldb_dlopen_result {
    void *image_ptr;
    const char *error_str;
  };
  
  extern "C" void *memcpy(void *, const void *, size_t size);
  extern "C" size_t strlen(const char *);
  

  void * __lldb_dlopen_wrapper (const char *name, 
                                const char *path_strings,
                                char *buffer,
                                __lldb_dlopen_result *result_ptr)
  {
    // This is the case where the name is the full path:
    if (!path_strings) {
      result_ptr->image_ptr = dlopen(name, RTLD_LAZY);
      if (result_ptr->image_ptr)
        result_ptr->error_str = nullptr;
      else
        result_ptr->error_str = dlerror();
      return nullptr;
    }
    
    // This is the case where we have a list of paths:
    size_t name_len = strlen(name);
    while (path_strings && path_strings[0] != '\0') {
      size_t path_len = strlen(path_strings);
      memcpy((void *) buffer, (void *) path_strings, path_len);
      buffer[path_len] = '/';
      char *target_ptr = buffer+path_len+1; 
      memcpy((void *) target_ptr, (void *) name, name_len + 1);
      result_ptr->image_ptr = dlopen(buffer, RTLD_LAZY);
      if (result_ptr->image_ptr) {
        result_ptr->error_str = nullptr;
        break;
      }
      result_ptr->error_str = dlerror();
      path_strings = path_strings + path_len + 1;
    }
    return nullptr;
  }
  )";

  static const char *dlopen_wrapper_name = "__lldb_dlopen_wrapper";
  Process *process = exe_ctx.GetProcessSP().get();
  // Insert the dlopen shim defines into our generic expression:
  std::string expr(std::string(GetLibdlFunctionDeclarations(process)));
  expr.append(dlopen_wrapper_code);
  Status utility_error;
  DiagnosticManager diagnostics;

  auto utility_fn_or_error = process->GetTarget().CreateUtilityFunction(
      std::move(expr), dlopen_wrapper_name, eLanguageTypeC_plus_plus, exe_ctx);
  if (!utility_fn_or_error) {
    std::string error_str = llvm::toString(utility_fn_or_error.takeError());
    error.SetErrorStringWithFormat(
        "dlopen error: could not create utility function: %s",
        error_str.c_str());
    return nullptr;
  }
  std::unique_ptr<UtilityFunction> dlopen_utility_func_up =
      std::move(*utility_fn_or_error);

  Value value;
  ValueList arguments;
  FunctionCaller *do_dlopen_function = nullptr;

  // Fetch the clang types we will need:
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(process->GetTarget());
  if (!scratch_ts_sp)
    return nullptr;

  CompilerType clang_void_pointer_type =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
  CompilerType clang_char_pointer_type =
      scratch_ts_sp->GetBasicType(eBasicTypeChar).GetPointerType();

  // We are passing four arguments, the basename, the list of places to look,
  // a buffer big enough for all the path + name combos, and
  // a pointer to the storage we've made for the result:
  value.SetValueType(Value::ValueType::Scalar);
  value.SetCompilerType(clang_void_pointer_type);
  arguments.PushValue(value);
  value.SetCompilerType(clang_char_pointer_type);
  arguments.PushValue(value);
  arguments.PushValue(value);
  arguments.PushValue(value);
  
  do_dlopen_function = dlopen_utility_func_up->MakeFunctionCaller(
      clang_void_pointer_type, arguments, exe_ctx.GetThreadSP(), utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat(
        "dlopen error: could not make function caller: %s",
        utility_error.AsCString());
    return nullptr;
  }
  
  do_dlopen_function = dlopen_utility_func_up->GetFunctionCaller();
  if (!do_dlopen_function) {
    error.SetErrorString("dlopen error: could not get function caller.");
    return nullptr;
  }
  
  // We made a good utility function, so cache it in the process:
  return dlopen_utility_func_up;
}

uint32_t PlatformPOSIX::DoLoadImage(lldb_private::Process *process,
                                    const lldb_private::FileSpec &remote_file,
                                    const std::vector<std::string> *paths,
                                    lldb_private::Status &error,
                                    lldb_private::FileSpec *loaded_image) {
  if (loaded_image)
    loaded_image->Clear();

  std::string path;
  path = remote_file.GetPath(false);

  ThreadSP thread_sp = process->GetThreadList().GetExpressionExecutionThread();
  if (!thread_sp) {
    error.SetErrorString("dlopen error: no thread available to call dlopen.");
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  DiagnosticManager diagnostics;
  
  ExecutionContext exe_ctx;
  thread_sp->CalculateExecutionContext(exe_ctx);

  Status utility_error;
  UtilityFunction *dlopen_utility_func;
  ValueList arguments;
  FunctionCaller *do_dlopen_function = nullptr;

  // The UtilityFunction is held in the Process.  Platforms don't track the
  // lifespan of the Targets that use them, we can't put this in the Platform.
  dlopen_utility_func = process->GetLoadImageUtilityFunction(
      this, [&]() -> std::unique_ptr<UtilityFunction> {
        return MakeLoadImageUtilityFunction(exe_ctx, error);
      });
  // If we couldn't make it, the error will be in error, so we can exit here.
  if (!dlopen_utility_func)
    return LLDB_INVALID_IMAGE_TOKEN;
    
  do_dlopen_function = dlopen_utility_func->GetFunctionCaller();
  if (!do_dlopen_function) {
    error.SetErrorString("dlopen error: could not get function caller.");
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  arguments = do_dlopen_function->GetArgumentValues();
  
  // Now insert the path we are searching for and the result structure into the
  // target.
  uint32_t permissions = ePermissionsReadable|ePermissionsWritable;
  size_t path_len = path.size() + 1;
  lldb::addr_t path_addr = process->AllocateMemory(path_len, 
                                                   permissions,
                                                   utility_error);
  if (path_addr == LLDB_INVALID_ADDRESS) {
    error.SetErrorStringWithFormat(
        "dlopen error: could not allocate memory for path: %s",
        utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  // Make sure we deallocate the input string memory:
  auto path_cleanup = llvm::make_scope_exit([process, path_addr] {
    // Deallocate the buffer.
    process->DeallocateMemory(path_addr);
  });

  process->WriteMemory(path_addr, path.c_str(), path_len, utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat(
        "dlopen error: could not write path string: %s",
        utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Make space for our return structure.  It is two pointers big: the token
  // and the error string.
  const uint32_t addr_size = process->GetAddressByteSize();
  lldb::addr_t return_addr = process->CallocateMemory(2*addr_size,
                                                      permissions,
                                                      utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat(
        "dlopen error: could not allocate memory for path: %s",
        utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Make sure we deallocate the result structure memory
  auto return_cleanup = llvm::make_scope_exit([process, return_addr] {
    // Deallocate the buffer
    process->DeallocateMemory(return_addr);
  });

  // This will be the address of the storage for paths, if we are using them,
  // or nullptr to signal we aren't.
  lldb::addr_t path_array_addr = 0x0;
  std::optional<llvm::detail::scope_exit<std::function<void()>>>
      path_array_cleanup;

  // This is the address to a buffer large enough to hold the largest path
  // conjoined with the library name we're passing in.  This is a convenience 
  // to avoid having to call malloc in the dlopen function.
  lldb::addr_t buffer_addr = 0x0;
  std::optional<llvm::detail::scope_exit<std::function<void()>>> buffer_cleanup;

  // Set the values into our args and write them to the target:
  if (paths != nullptr) {
    // First insert the paths into the target.  This is expected to be a 
    // continuous buffer with the strings laid out null terminated and
    // end to end with an empty string terminating the buffer.
    // We also compute the buffer's required size as we go.
    size_t buffer_size = 0;
    std::string path_array;
    for (auto path : *paths) {
      // Don't insert empty paths, they will make us abort the path
      // search prematurely.
      if (path.empty())
        continue;
      size_t path_size = path.size();
      path_array.append(path);
      path_array.push_back('\0');
      if (path_size > buffer_size)
        buffer_size = path_size;
    }
    path_array.push_back('\0');
    
    path_array_addr = process->AllocateMemory(path_array.size(), 
                                              permissions,
                                              utility_error);
    if (path_array_addr == LLDB_INVALID_ADDRESS) {
      error.SetErrorStringWithFormat(
          "dlopen error: could not allocate memory for path array: %s",
          utility_error.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }
    
    // Make sure we deallocate the paths array.
    path_array_cleanup.emplace([process, path_array_addr]() {
      // Deallocate the path array.
      process->DeallocateMemory(path_array_addr);
    });

    process->WriteMemory(path_array_addr, path_array.data(), 
                         path_array.size(), utility_error);

    if (utility_error.Fail()) {
      error.SetErrorStringWithFormat(
          "dlopen error: could not write path array: %s",
          utility_error.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }
    // Now make spaces in the target for the buffer.  We need to add one for
    // the '/' that the utility function will insert and one for the '\0':
    buffer_size += path.size() + 2;
    
    buffer_addr = process->AllocateMemory(buffer_size, 
                                          permissions,
                                          utility_error);
    if (buffer_addr == LLDB_INVALID_ADDRESS) {
      error.SetErrorStringWithFormat(
          "dlopen error: could not allocate memory for buffer: %s",
          utility_error.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }
  
    // Make sure we deallocate the buffer memory:
    buffer_cleanup.emplace([process, buffer_addr]() {
      // Deallocate the buffer.
      process->DeallocateMemory(buffer_addr);
    });
  }
    
  arguments.GetValueAtIndex(0)->GetScalar() = path_addr;
  arguments.GetValueAtIndex(1)->GetScalar() = path_array_addr;
  arguments.GetValueAtIndex(2)->GetScalar() = buffer_addr;
  arguments.GetValueAtIndex(3)->GetScalar() = return_addr;

  lldb::addr_t func_args_addr = LLDB_INVALID_ADDRESS;
  
  diagnostics.Clear();
  if (!do_dlopen_function->WriteFunctionArguments(exe_ctx, 
                                                 func_args_addr,
                                                 arguments,
                                                 diagnostics)) {
    error.SetErrorStringWithFormat(
        "dlopen error: could not write function arguments: %s",
        diagnostics.GetString().c_str());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Make sure we clean up the args structure.  We can't reuse it because the
  // Platform lives longer than the process and the Platforms don't get a
  // signal to clean up cached data when a process goes away.
  auto args_cleanup =
      llvm::make_scope_exit([do_dlopen_function, &exe_ctx, func_args_addr] {
        do_dlopen_function->DeallocateFunctionResults(exe_ctx, func_args_addr);
      });

  // Now run the caller:
  EvaluateExpressionOptions options;
  options.SetExecutionPolicy(eExecutionPolicyAlways);
  options.SetLanguage(eLanguageTypeC_plus_plus);
  options.SetIgnoreBreakpoints(true);
  options.SetUnwindOnError(true);
  options.SetTrapExceptions(false); // dlopen can't throw exceptions, so
                                    // don't do the work to trap them.
  options.SetTimeout(process->GetUtilityExpressionTimeout());
  options.SetIsForUtilityExpr(true);

  Value return_value;
  // Fetch the clang types we will need:
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(process->GetTarget());
  if (!scratch_ts_sp) {
    error.SetErrorString("dlopen error: Unable to get TypeSystemClang");
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  CompilerType clang_void_pointer_type =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();

  return_value.SetCompilerType(clang_void_pointer_type);
  
  ExpressionResults results = do_dlopen_function->ExecuteFunction(
      exe_ctx, &func_args_addr, options, diagnostics, return_value);
  if (results != eExpressionCompleted) {
    error.SetErrorStringWithFormat(
        "dlopen error: failed executing dlopen wrapper function: %s",
        diagnostics.GetString().c_str());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Read the dlopen token from the return area:
  lldb::addr_t token = process->ReadPointerFromMemory(return_addr, 
                                                      utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat(
        "dlopen error: could not read the return struct: %s",
        utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // The dlopen succeeded!
  if (token != 0x0) {
    if (loaded_image && buffer_addr != 0x0)
    {
      // Capture the image which was loaded.  We leave it in the buffer on
      // exit from the dlopen function, so we can just read it from there:
      std::string name_string;
      process->ReadCStringFromMemory(buffer_addr, name_string, utility_error);
      if (utility_error.Success())
        loaded_image->SetFile(name_string, llvm::sys::path::Style::posix);
    }
    return process->AddImageToken(token);
  }
    
  // We got an error, lets read in the error string:
  std::string dlopen_error_str;
  lldb::addr_t error_addr 
    = process->ReadPointerFromMemory(return_addr + addr_size, utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat(
        "dlopen error: could not read error string: %s",
        utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  size_t num_chars = process->ReadCStringFromMemory(error_addr + addr_size, 
                                                    dlopen_error_str, 
                                                    utility_error);
  if (utility_error.Success() && num_chars > 0)
    error.SetErrorStringWithFormat("dlopen error: %s",
                                   dlopen_error_str.c_str());
  else
    error.SetErrorStringWithFormat("dlopen failed for unknown reasons.");

  return LLDB_INVALID_IMAGE_TOKEN;
}

Status PlatformPOSIX::UnloadImage(lldb_private::Process *process,
                                  uint32_t image_token) {
  const addr_t image_addr = process->GetImagePtrFromToken(image_token);
  if (image_addr == LLDB_INVALID_IMAGE_TOKEN)
    return Status("Invalid image token");

  StreamString expr;
  expr.Printf("dlclose((void *)0x%" PRIx64 ")", image_addr);
  llvm::StringRef prefix = GetLibdlFunctionDeclarations(process);
  lldb::ValueObjectSP result_valobj_sp;
  Status error = EvaluateLibdlExpression(process, expr.GetData(), prefix,
                                         result_valobj_sp);
  if (error.Fail())
    return error;

  if (result_valobj_sp->GetError().Fail())
    return result_valobj_sp->GetError();

  Scalar scalar;
  if (result_valobj_sp->ResolveValue(scalar)) {
    if (scalar.UInt(1))
      return Status("expression failed: \"%s\"", expr.GetData());
    process->ResetImageToken(image_token);
  }
  return Status();
}

llvm::StringRef
PlatformPOSIX::GetLibdlFunctionDeclarations(lldb_private::Process *process) {
  return R"(
              extern "C" void* dlopen(const char*, int);
              extern "C" void* dlsym(void*, const char*);
              extern "C" int   dlclose(void*);
              extern "C" char* dlerror(void);
             )";
}

ConstString PlatformPOSIX::GetFullNameForDylib(ConstString basename) {
  if (basename.IsEmpty())
    return basename;

  StreamString stream;
  stream.Printf("lib%s.so", basename.GetCString());
  return ConstString(stream.GetString());
}
