//===-- PlatformWindows.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformWindows.h"

#include <cstdio>
#include <optional>
#if defined(_WIN32)
#include "lldb/Host/windows/windows.h"
#include <winsock2.h>
#endif

#include "Plugins/Platform/gdb-server/PlatformRemoteGDBServer.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Status.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/ConvertUTF.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(PlatformWindows)

static uint32_t g_initialize_count = 0;

PlatformSP PlatformWindows::CreateInstance(bool force,
                                           const lldb_private::ArchSpec *arch) {
  // The only time we create an instance is when we are creating a remote
  // windows platform
  const bool is_host = false;

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    const llvm::Triple &triple = arch->GetTriple();
    switch (triple.getVendor()) {
    case llvm::Triple::PC:
      create = true;
      break;

    case llvm::Triple::UnknownVendor:
      create = !arch->TripleVendorWasSpecified();
      break;

    default:
      break;
    }

    if (create) {
      switch (triple.getOS()) {
      case llvm::Triple::Win32:
        break;

      case llvm::Triple::UnknownOS:
        create = arch->TripleOSWasSpecified();
        break;

      default:
        create = false;
        break;
      }
    }
  }
  if (create)
    return PlatformSP(new PlatformWindows(is_host));
  return PlatformSP();
}

llvm::StringRef PlatformWindows::GetPluginDescriptionStatic(bool is_host) {
  return is_host ? "Local Windows user platform plug-in."
                 : "Remote Windows user platform plug-in.";
}

void PlatformWindows::Initialize() {
  Platform::Initialize();

  if (g_initialize_count++ == 0) {
#if defined(_WIN32)
    // Force a host flag to true for the default platform object.
    PlatformSP default_platform_sp(new PlatformWindows(true));
    default_platform_sp->SetSystemArchitecture(HostInfo::GetArchitecture());
    Platform::SetHostPlatform(default_platform_sp);
#endif
    PluginManager::RegisterPlugin(
        PlatformWindows::GetPluginNameStatic(false),
        PlatformWindows::GetPluginDescriptionStatic(false),
        PlatformWindows::CreateInstance);
  }
}

void PlatformWindows::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformWindows::CreateInstance);
    }
  }

  Platform::Terminate();
}

/// Default Constructor
PlatformWindows::PlatformWindows(bool is_host) : RemoteAwarePlatform(is_host) {
  const auto &AddArch = [&](const ArchSpec &spec) {
    if (llvm::any_of(m_supported_architectures, [spec](const ArchSpec &rhs) {
          return spec.IsExactMatch(rhs);
        }))
      return;
    if (spec.IsValid())
      m_supported_architectures.push_back(spec);
  };
  AddArch(HostInfo::GetArchitecture(HostInfo::eArchKindDefault));
  AddArch(HostInfo::GetArchitecture(HostInfo::eArchKind32));
  AddArch(HostInfo::GetArchitecture(HostInfo::eArchKind64));
}

Status PlatformWindows::ConnectRemote(Args &args) {
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

    if (m_remote_platform_sp) {
      if (error.Success()) {
        if (m_remote_platform_sp) {
          error = m_remote_platform_sp->ConnectRemote(args);
        } else {
          error.SetErrorString(
              "\"platform connect\" takes a single argument: <connect-url>");
        }
      }
    } else
      error.SetErrorString("failed to create a 'remote-gdb-server' platform");

    if (error.Fail())
      m_remote_platform_sp.reset();
  }

  return error;
}

uint32_t PlatformWindows::DoLoadImage(Process *process,
                                      const FileSpec &remote_file,
                                      const std::vector<std::string> *paths,
                                      Status &error, FileSpec *loaded_image) {
  DiagnosticManager diagnostics;

  if (loaded_image)
    loaded_image->Clear();

  ThreadSP thread = process->GetThreadList().GetExpressionExecutionThread();
  if (!thread) {
    error.SetErrorString("LoadLibrary error: no thread available to invoke LoadLibrary");
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  ExecutionContext context;
  thread->CalculateExecutionContext(context);

  Status status;
  UtilityFunction *loader =
      process->GetLoadImageUtilityFunction(this, [&]() -> std::unique_ptr<UtilityFunction> {
        return MakeLoadImageUtilityFunction(context, status);
      });
  if (loader == nullptr)
    return LLDB_INVALID_IMAGE_TOKEN;

  FunctionCaller *invocation = loader->GetFunctionCaller();
  if (!invocation) {
    error.SetErrorString("LoadLibrary error: could not get function caller");
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  /* Convert name */
  llvm::SmallVector<llvm::UTF16, 261> name;
  if (!llvm::convertUTF8ToUTF16String(remote_file.GetPath(), name)) {
    error.SetErrorString("LoadLibrary error: could not convert path to UCS2");
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  name.emplace_back(L'\0');

  /* Inject name paramter into inferior */
  lldb::addr_t injected_name =
      process->AllocateMemory(name.size() * sizeof(llvm::UTF16),
                              ePermissionsReadable | ePermissionsWritable,
                              status);
  if (injected_name == LLDB_INVALID_ADDRESS) {
    error.SetErrorStringWithFormat("LoadLibrary error: unable to allocate memory for name: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  auto name_cleanup = llvm::make_scope_exit([process, injected_name]() {
    process->DeallocateMemory(injected_name);
  });

  process->WriteMemory(injected_name, name.data(),
                       name.size() * sizeof(llvm::UTF16), status);
  if (status.Fail()) {
    error.SetErrorStringWithFormat("LoadLibrary error: unable to write name: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  /* Inject paths parameter into inferior */
  lldb::addr_t injected_paths{0x0};
  std::optional<llvm::detail::scope_exit<std::function<void()>>> paths_cleanup;
  if (paths) {
    llvm::SmallVector<llvm::UTF16, 261> search_paths;

    for (const auto &path : *paths) {
      if (path.empty())
        continue;

      llvm::SmallVector<llvm::UTF16, 261> buffer;
      if (!llvm::convertUTF8ToUTF16String(path, buffer))
        continue;

      search_paths.append(std::begin(buffer), std::end(buffer));
      search_paths.emplace_back(L'\0');
    }
    search_paths.emplace_back(L'\0');

    injected_paths =
        process->AllocateMemory(search_paths.size() * sizeof(llvm::UTF16),
                                ePermissionsReadable | ePermissionsWritable,
                                status);
    if (injected_paths == LLDB_INVALID_ADDRESS) {
      error.SetErrorStringWithFormat("LoadLibrary error: unable to allocate memory for paths: %s",
                                     status.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }

    paths_cleanup.emplace([process, injected_paths]() {
      process->DeallocateMemory(injected_paths);
    });

    process->WriteMemory(injected_paths, search_paths.data(),
                         search_paths.size() * sizeof(llvm::UTF16), status);
    if (status.Fail()) {
      error.SetErrorStringWithFormat("LoadLibrary error: unable to write paths: %s",
                                     status.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }
  }

  /* Inject wszModulePath into inferior */
  // FIXME(compnerd) should do something better for the length?
  // GetModuleFileNameA is likely limited to PATH_MAX rather than the NT path
  // limit.
  unsigned injected_length = 261;

  lldb::addr_t injected_module_path =
      process->AllocateMemory(injected_length + 1,
                              ePermissionsReadable | ePermissionsWritable,
                              status);
  if (injected_module_path == LLDB_INVALID_ADDRESS) {
    error.SetErrorStringWithFormat("LoadLibrary error: unable to allocate memory for module location: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  auto injected_module_path_cleanup =
      llvm::make_scope_exit([process, injected_module_path]() {
    process->DeallocateMemory(injected_module_path);
  });

  /* Inject __lldb_LoadLibraryResult into inferior */
  const uint32_t word_size = process->GetAddressByteSize();
  lldb::addr_t injected_result =
      process->AllocateMemory(3 * word_size,
                              ePermissionsReadable | ePermissionsWritable,
                              status);
  if (status.Fail()) {
    error.SetErrorStringWithFormat("LoadLibrary error: could not allocate memory for result: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  auto result_cleanup = llvm::make_scope_exit([process, injected_result]() {
    process->DeallocateMemory(injected_result);
  });

  process->WritePointerToMemory(injected_result + word_size,
                                injected_module_path, status);
  if (status.Fail()) {
    error.SetErrorStringWithFormat("LoadLibrary error: could not initialize result: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  // XXX(compnerd) should we use the compiler to get the sizeof(unsigned)?
  process->WriteScalarToMemory(injected_result + 2 * word_size,
                               Scalar{injected_length}, sizeof(unsigned),
                               status);
  if (status.Fail()) {
    error.SetErrorStringWithFormat("LoadLibrary error: could not initialize result: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  /* Setup Formal Parameters */
  ValueList parameters = invocation->GetArgumentValues();
  parameters.GetValueAtIndex(0)->GetScalar() = injected_name;
  parameters.GetValueAtIndex(1)->GetScalar() = injected_paths;
  parameters.GetValueAtIndex(2)->GetScalar() = injected_result;

  lldb::addr_t injected_parameters = LLDB_INVALID_ADDRESS;
  diagnostics.Clear();
  if (!invocation->WriteFunctionArguments(context, injected_parameters,
                                          parameters, diagnostics)) {
    error.SetErrorStringWithFormat("LoadLibrary error: unable to write function parameters: %s",
                                   diagnostics.GetString().c_str());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  auto parameter_cleanup =
      llvm::make_scope_exit([invocation, &context, injected_parameters]() {
        invocation->DeallocateFunctionResults(context, injected_parameters);
      });

  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(process->GetTarget());
  if (!scratch_ts_sp) {
    error.SetErrorString("LoadLibrary error: unable to get (clang) type system");
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  /* Setup Return Type */
  CompilerType VoidPtrTy =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();

  Value value;
  value.SetCompilerType(VoidPtrTy);

  /* Invoke expression */
  EvaluateExpressionOptions options;
  options.SetExecutionPolicy(eExecutionPolicyAlways);
  options.SetLanguage(eLanguageTypeC_plus_plus);
  options.SetIgnoreBreakpoints(true);
  options.SetUnwindOnError(true);
  // LoadLibraryEx{A,W}/FreeLibrary cannot raise exceptions which we can handle.
  // They may potentially throw SEH exceptions which we do not know how to
  // handle currently.
  options.SetTrapExceptions(false);
  options.SetTimeout(process->GetUtilityExpressionTimeout());
  options.SetIsForUtilityExpr(true);

  ExpressionResults result =
      invocation->ExecuteFunction(context, &injected_parameters, options,
                                  diagnostics, value);
  if (result != eExpressionCompleted) {
    error.SetErrorStringWithFormat("LoadLibrary error: failed to execute LoadLibrary helper: %s",
                                   diagnostics.GetString().c_str());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  /* Read result */
  lldb::addr_t token = process->ReadPointerFromMemory(injected_result, status);
  if (status.Fail()) {
    error.SetErrorStringWithFormat("LoadLibrary error: could not read the result: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  if (!token) {
    // XXX(compnerd) should we use the compiler to get the sizeof(unsigned)?
    uint64_t error_code =
        process->ReadUnsignedIntegerFromMemory(injected_result + 2 * word_size + sizeof(unsigned),
                                               word_size, 0, status);
    if (status.Fail()) {
      error.SetErrorStringWithFormat("LoadLibrary error: could not read error status: %s",
                                     status.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }

    error.SetErrorStringWithFormat("LoadLibrary Error: %" PRIu64, error_code);
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  std::string module_path;
  process->ReadCStringFromMemory(injected_module_path, module_path, status);
  if (status.Fail()) {
    error.SetErrorStringWithFormat("LoadLibrary error: could not read module path: %s",
                                   status.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }

  if (loaded_image)
    loaded_image->SetFile(module_path, llvm::sys::path::Style::native);
  return process->AddImageToken(token);
}

Status PlatformWindows::UnloadImage(Process *process, uint32_t image_token) {
  const addr_t address = process->GetImagePtrFromToken(image_token);
  if (address == LLDB_INVALID_IMAGE_TOKEN)
    return Status("invalid image token");

  StreamString expression;
  expression.Printf("FreeLibrary((HMODULE)0x%" PRIx64 ")", address);

  ValueObjectSP value;
  Status result =
      EvaluateLoaderExpression(process, expression.GetData(), value);
  if (result.Fail())
    return result;

  if (value->GetError().Fail())
    return value->GetError();

  Scalar scalar;
  if (value->ResolveValue(scalar)) {
    if (scalar.UInt(1))
      return Status("expression failed: \"%s\"", expression.GetData());
    process->ResetImageToken(image_token);
  }

  return Status();
}

Status PlatformWindows::DisconnectRemote() {
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

ProcessSP PlatformWindows::DebugProcess(ProcessLaunchInfo &launch_info,
                                        Debugger &debugger, Target &target,
                                        Status &error) {
  // Windows has special considerations that must be followed when launching or
  // attaching to a process.  The key requirement is that when launching or
  // attaching to a process, you must do it from the same the thread that will
  // go into a permanent loop which will then receive debug events from the
  // process.  In particular, this means we can't use any of LLDB's generic
  // mechanisms to do it for us, because it doesn't have the special knowledge
  // required for setting up the background thread or passing the right flags.
  //
  // Another problem is that LLDB's standard model for debugging a process
  // is to first launch it, have it stop at the entry point, and then attach to
  // it.  In Windows this doesn't quite work, you have to specify as an
  // argument to CreateProcess() that you're going to debug the process.  So we
  // override DebugProcess here to handle this.  Launch operations go directly
  // to the process plugin, and attach operations almost go directly to the
  // process plugin (but we hijack the events first).  In essence, we
  // encapsulate all the logic of Launching and Attaching in the process
  // plugin, and PlatformWindows::DebugProcess is just a pass-through to get to
  // the process plugin.

  if (IsRemote()) {
    if (m_remote_platform_sp)
      return m_remote_platform_sp->DebugProcess(launch_info, debugger, target,
                                                error);
    else
      error.SetErrorString("the platform is not currently connected");
  }

  if (launch_info.GetProcessID() != LLDB_INVALID_PROCESS_ID) {
    // This is a process attach.  Don't need to launch anything.
    ProcessAttachInfo attach_info(launch_info);
    return Attach(attach_info, debugger, &target, error);
  }

  ProcessSP process_sp =
      target.CreateProcess(launch_info.GetListener(),
                           launch_info.GetProcessPluginName(), nullptr, false);

  process_sp->HijackProcessEvents(launch_info.GetHijackListener());

  // We need to launch and attach to the process.
  launch_info.GetFlags().Set(eLaunchFlagDebug);
  if (process_sp)
    error = process_sp->Launch(launch_info);

  return process_sp;
}

lldb::ProcessSP PlatformWindows::Attach(ProcessAttachInfo &attach_info,
                                        Debugger &debugger, Target *target,
                                        Status &error) {
  error.Clear();
  lldb::ProcessSP process_sp;
  if (!IsHost()) {
    if (m_remote_platform_sp)
      process_sp =
          m_remote_platform_sp->Attach(attach_info, debugger, target, error);
    else
      error.SetErrorString("the platform is not currently connected");
    return process_sp;
  }

  if (target == nullptr) {
    TargetSP new_target_sp;
    FileSpec emptyFileSpec;
    ArchSpec emptyArchSpec;

    error = debugger.GetTargetList().CreateTarget(
        debugger, "", "", eLoadDependentsNo, nullptr, new_target_sp);
    target = new_target_sp.get();
  }

  if (!target || error.Fail())
    return process_sp;

  process_sp =
      target->CreateProcess(attach_info.GetListenerForProcess(debugger),
                            attach_info.GetProcessPluginName(), nullptr, false);

  process_sp->HijackProcessEvents(attach_info.GetHijackListener());
  if (process_sp)
    error = process_sp->Attach(attach_info);

  return process_sp;
}

void PlatformWindows::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);

#ifdef _WIN32
  llvm::VersionTuple version = HostInfo::GetOSVersion();
  strm << "      Host: Windows " << version.getAsString() << '\n';
#endif
}

bool PlatformWindows::CanDebugProcess() { return true; }

ConstString PlatformWindows::GetFullNameForDylib(ConstString basename) {
  if (basename.IsEmpty())
    return basename;

  StreamString stream;
  stream.Printf("%s.dll", basename.GetCString());
  return ConstString(stream.GetString());
}

size_t
PlatformWindows::GetSoftwareBreakpointTrapOpcode(Target &target,
                                                 BreakpointSite *bp_site) {
  ArchSpec arch = target.GetArchitecture();
  assert(arch.IsValid());
  const uint8_t *trap_opcode = nullptr;
  size_t trap_opcode_size = 0;

  switch (arch.GetMachine()) {
  case llvm::Triple::aarch64: {
    static const uint8_t g_aarch64_opcode[] = {0x00, 0x00, 0x3e, 0xd4}; // brk #0xf000
    trap_opcode = g_aarch64_opcode;
    trap_opcode_size = sizeof(g_aarch64_opcode);

    if (bp_site->SetTrapOpcode(trap_opcode, trap_opcode_size))
      return trap_opcode_size;
    return 0;
  } break;

  case llvm::Triple::arm:
  case llvm::Triple::thumb: {
    static const uint8_t g_thumb_opcode[] = {0xfe, 0xde}; // udf #0xfe
    trap_opcode = g_thumb_opcode;
    trap_opcode_size = sizeof(g_thumb_opcode);

    if (bp_site->SetTrapOpcode(trap_opcode, trap_opcode_size))
      return trap_opcode_size;
    return 0;
  } break;

  default:
    return Platform::GetSoftwareBreakpointTrapOpcode(target, bp_site);
  }
}

std::unique_ptr<UtilityFunction>
PlatformWindows::MakeLoadImageUtilityFunction(ExecutionContext &context,
                                              Status &status) {
  // FIXME(compnerd) `-fdeclspec` is not passed to the clang instance?
  static constexpr const char kLoaderDecls[] = R"(
extern "C" {
// errhandlingapi.h

// `LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS`
//
// Directories in the standard search path are not searched. This value cannot
// be combined with `LOAD_WITH_ALTERED_SEARCH_PATH`.
//
// This value represents the recommended maximum number of directories an
// application should include in its DLL search path.
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000

// WINBASEAPI DWORD WINAPI GetLastError(VOID);
/* __declspec(dllimport) */ uint32_t __stdcall GetLastError();

// libloaderapi.h

// WINBASEAPI DLL_DIRECTORY_COOKIE WINAPI AddDllDirectory(LPCWSTR);
/* __declspec(dllimport) */ void * __stdcall AddDllDirectory(const wchar_t *);

// WINBASEAPI BOOL WINAPI FreeModule(HMODULE);
/* __declspec(dllimport) */ int __stdcall FreeModule(void *hLibModule);

// WINBASEAPI DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
/* __declspec(dllimport) */ uint32_t GetModuleFileNameA(void *, char *, uint32_t);

// WINBASEAPI HMODULE WINAPI LoadLibraryExW(LPCWSTR, HANDLE, DWORD);
/* __declspec(dllimport) */ void * __stdcall LoadLibraryExW(const wchar_t *, void *, uint32_t);

// corecrt_wstring.h

// _ACRTIMP size_t __cdecl wcslen(wchar_t const *_String);
/* __declspec(dllimport) */ size_t __cdecl wcslen(const wchar_t *);

// lldb specific code

struct __lldb_LoadLibraryResult {
  void *ImageBase;
  char *ModulePath;
  unsigned Length;
  unsigned ErrorCode;
};

_Static_assert(sizeof(struct __lldb_LoadLibraryResult) <= 3 * sizeof(void *),
               "__lldb_LoadLibraryResult size mismatch");

void * __lldb_LoadLibraryHelper(const wchar_t *name, const wchar_t *paths,
                                __lldb_LoadLibraryResult *result) {
  for (const wchar_t *path = paths; path && *path; ) {
    (void)AddDllDirectory(path);
    path += wcslen(path) + 1;
  }

  result->ImageBase = LoadLibraryExW(name, nullptr,
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (result->ImageBase == nullptr)
    result->ErrorCode = GetLastError();
  else
    result->Length = GetModuleFileNameA(result->ImageBase, result->ModulePath,
                                        result->Length);

  return result->ImageBase;
}
}
  )";

  static constexpr const char kName[] = "__lldb_LoadLibraryHelper";

  ProcessSP process = context.GetProcessSP();
  Target &target = process->GetTarget();

  auto function = target.CreateUtilityFunction(std::string{kLoaderDecls}, kName,
                                               eLanguageTypeC_plus_plus,
                                               context);
  if (!function) {
    std::string error = llvm::toString(function.takeError());
    status.SetErrorStringWithFormat("LoadLibrary error: could not create utility function: %s",
                                    error.c_str());
    return nullptr;
  }

  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(target);
  if (!scratch_ts_sp)
    return nullptr;

  CompilerType VoidPtrTy =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
  CompilerType WCharPtrTy =
      scratch_ts_sp->GetBasicType(eBasicTypeWChar).GetPointerType();

  ValueList parameters;

  Value value;
  value.SetValueType(Value::ValueType::Scalar);

  value.SetCompilerType(WCharPtrTy);
  parameters.PushValue(value);  // name
  parameters.PushValue(value);  // paths

  value.SetCompilerType(VoidPtrTy);
  parameters.PushValue(value);  // result

  Status error;
  std::unique_ptr<UtilityFunction> utility{std::move(*function)};
  utility->MakeFunctionCaller(VoidPtrTy, parameters, context.GetThreadSP(),
                              error);
  if (error.Fail()) {
    status.SetErrorStringWithFormat("LoadLibrary error: could not create function caller: %s",
                                    error.AsCString());
    return nullptr;
  }

  if (!utility->GetFunctionCaller()) {
    status.SetErrorString("LoadLibrary error: could not get function caller");
    return nullptr;
  }

  return utility;
}

Status PlatformWindows::EvaluateLoaderExpression(Process *process,
                                                 const char *expression,
                                                 ValueObjectSP &value) {
  // FIXME(compnerd) `-fdeclspec` is not passed to the clang instance?
  static constexpr const char kLoaderDecls[] = R"(
extern "C" {
// libloaderapi.h

// WINBASEAPI DLL_DIRECTORY_COOKIE WINAPI AddDllDirectory(LPCWSTR);
/* __declspec(dllimport) */ void * __stdcall AddDllDirectory(const wchar_t *);

// WINBASEAPI BOOL WINAPI FreeModule(HMODULE);
/* __declspec(dllimport) */ int __stdcall FreeModule(void *);

// WINBASEAPI DWORD WINAPI GetModuleFileNameA(HMODULE, LPSTR, DWORD);
/* __declspec(dllimport) */ uint32_t GetModuleFileNameA(void *, char *, uint32_t);

// WINBASEAPI HMODULE WINAPI LoadLibraryExW(LPCWSTR, HANDLE, DWORD);
/* __declspec(dllimport) */ void * __stdcall LoadLibraryExW(const wchar_t *, void *, uint32_t);
}
  )";

  if (DynamicLoader *loader = process->GetDynamicLoader()) {
    Status result = loader->CanLoadImage();
    if (result.Fail())
      return result;
  }

  ThreadSP thread = process->GetThreadList().GetExpressionExecutionThread();
  if (!thread)
    return Status("selected thread is invalid");

  StackFrameSP frame = thread->GetStackFrameAtIndex(0);
  if (!frame)
    return Status("frame 0 is invalid");

  ExecutionContext context;
  frame->CalculateExecutionContext(context);

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetExecutionPolicy(eExecutionPolicyAlways);
  options.SetLanguage(eLanguageTypeC_plus_plus);
  // LoadLibraryEx{A,W}/FreeLibrary cannot raise exceptions which we can handle.
  // They may potentially throw SEH exceptions which we do not know how to
  // handle currently.
  options.SetTrapExceptions(false);
  options.SetTimeout(process->GetUtilityExpressionTimeout());

  Status error;
  ExpressionResults result = UserExpression::Evaluate(
      context, options, expression, kLoaderDecls, value, error);
  if (result != eExpressionCompleted)
    return error;

  if (value->GetError().Fail())
    return value->GetError();

  return Status();
}
