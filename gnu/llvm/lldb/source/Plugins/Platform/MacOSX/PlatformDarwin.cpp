//===-- PlatformDarwin.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformDarwin.h"

#include <cstring>

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/XML.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Timer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VersionTuple.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

using namespace lldb;
using namespace lldb_private;

static Status ExceptionMaskValidator(const char *string, void *unused) {
  Status error;
  llvm::StringRef str_ref(string);
  llvm::SmallVector<llvm::StringRef> candidates;
  str_ref.split(candidates, '|');
  for (auto candidate : candidates) {
    if (!(candidate == "EXC_BAD_ACCESS"
          || candidate == "EXC_BAD_INSTRUCTION"
          || candidate == "EXC_ARITHMETIC"
          || candidate == "EXC_RESOURCE"
          || candidate == "EXC_GUARD"
          || candidate == "EXC_SYSCALL")) {
      error.SetErrorStringWithFormat("invalid exception type: '%s'",
          candidate.str().c_str());
      return error;
    }
  }
  return {};
}

/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
PlatformDarwin::~PlatformDarwin() = default;

// Static Variables
static uint32_t g_initialize_count = 0;

void PlatformDarwin::Initialize() {
  Platform::Initialize();

  if (g_initialize_count++ == 0) {
    PluginManager::RegisterPlugin(PlatformDarwin::GetPluginNameStatic(),
                                  PlatformDarwin::GetDescriptionStatic(),
                                  PlatformDarwin::CreateInstance,
                                  PlatformDarwin::DebuggerInitialize);
  }
}

void PlatformDarwin::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformDarwin::CreateInstance);
    }
  }

  Platform::Terminate();
}

llvm::StringRef PlatformDarwin::GetDescriptionStatic() {
  return "Darwin platform plug-in.";
}

PlatformSP PlatformDarwin::CreateInstance(bool force, const ArchSpec *arch) {
   // We only create subclasses of the PlatformDarwin plugin.
   return PlatformSP();
}

#define LLDB_PROPERTIES_platformdarwin
#include "PlatformMacOSXProperties.inc"

#define LLDB_PROPERTIES_platformdarwin
enum {
#include "PlatformMacOSXPropertiesEnum.inc"
};

class PlatformDarwinProperties : public Properties {
public:
  static llvm::StringRef GetSettingName() {
    static constexpr llvm::StringLiteral g_setting_name("darwin");
    return g_setting_name;
  }

  PlatformDarwinProperties() : Properties() {
    m_collection_sp = std::make_shared<OptionValueProperties>(GetSettingName());
    m_collection_sp->Initialize(g_platformdarwin_properties);
  }

  ~PlatformDarwinProperties() override = default;

  const char *GetIgnoredExceptions() const {
    const uint32_t idx = ePropertyIgnoredExceptions;
    const OptionValueString *option_value =
        m_collection_sp->GetPropertyAtIndexAsOptionValueString(idx);
    assert(option_value);
    return option_value->GetCurrentValue();
  }

  OptionValueString *GetIgnoredExceptionValue() {
    const uint32_t idx = ePropertyIgnoredExceptions;
    OptionValueString *option_value =
        m_collection_sp->GetPropertyAtIndexAsOptionValueString(idx);
    assert(option_value);
    return option_value;
  }
};

static PlatformDarwinProperties &GetGlobalProperties() {
  static PlatformDarwinProperties g_settings;
  return g_settings;
}

void PlatformDarwin::DebuggerInitialize(
    lldb_private::Debugger &debugger) {
  if (!PluginManager::GetSettingForPlatformPlugin(
          debugger, PlatformDarwinProperties::GetSettingName())) {
    const bool is_global_setting = false;
    PluginManager::CreateSettingForPlatformPlugin(
        debugger, GetGlobalProperties().GetValueProperties(),
        "Properties for the Darwin platform plug-in.", is_global_setting);
    OptionValueString *value = GetGlobalProperties().GetIgnoredExceptionValue();
    value->SetValidator(ExceptionMaskValidator);
  }
}

Args
PlatformDarwin::GetExtraStartupCommands() {
  std::string ignored_exceptions
      = GetGlobalProperties().GetIgnoredExceptions();
  if (ignored_exceptions.empty())
    return {};
  Args ret_args;
  std::string packet = "QSetIgnoredExceptions:";
  packet.append(ignored_exceptions);
  ret_args.AppendArgument(packet);
  return ret_args;
}

lldb_private::Status
PlatformDarwin::PutFile(const lldb_private::FileSpec &source,
                        const lldb_private::FileSpec &destination, uint32_t uid,
                        uint32_t gid) {
  // Unconditionally unlink the destination. If it is an executable,
  // simply opening it and truncating its contents would invalidate
  // its cached code signature.
  Unlink(destination);
  return PlatformPOSIX::PutFile(source, destination, uid, gid);
}

FileSpecList PlatformDarwin::LocateExecutableScriptingResources(
    Target *target, Module &module, Stream &feedback_stream) {
  FileSpecList file_list;
  if (target &&
      target->GetDebugger().GetScriptLanguage() == eScriptLanguagePython) {
    // NB some extensions might be meaningful and should not be stripped -
    // "this.binary.file"
    // should not lose ".file" but GetFileNameStrippingExtension() will do
    // precisely that. Ideally, we should have a per-platform list of
    // extensions (".exe", ".app", ".dSYM", ".framework") which should be
    // stripped while leaving "this.binary.file" as-is.

    FileSpec module_spec = module.GetFileSpec();

    if (module_spec) {
      if (SymbolFile *symfile = module.GetSymbolFile()) {
        ObjectFile *objfile = symfile->GetObjectFile();
        if (objfile) {
          FileSpec symfile_spec(objfile->GetFileSpec());
          if (symfile_spec &&
              llvm::StringRef(symfile_spec.GetPath())
                  .contains_insensitive(".dSYM/Contents/Resources/DWARF") &&
              FileSystem::Instance().Exists(symfile_spec)) {
            while (module_spec.GetFilename()) {
              std::string module_basename(
                  module_spec.GetFilename().GetCString());
              std::string original_module_basename(module_basename);

              bool was_keyword = false;

              // FIXME: for Python, we cannot allow certain characters in
              // module
              // filenames we import. Theoretically, different scripting
              // languages may have different sets of forbidden tokens in
              // filenames, and that should be dealt with by each
              // ScriptInterpreter. For now, we just replace dots with
              // underscores, but if we ever support anything other than
              // Python we will need to rework this
              std::replace(module_basename.begin(), module_basename.end(), '.',
                           '_');
              std::replace(module_basename.begin(), module_basename.end(), ' ',
                           '_');
              std::replace(module_basename.begin(), module_basename.end(), '-',
                           '_');
              ScriptInterpreter *script_interpreter =
                  target->GetDebugger().GetScriptInterpreter();
              if (script_interpreter &&
                  script_interpreter->IsReservedWord(module_basename.c_str())) {
                module_basename.insert(module_basename.begin(), '_');
                was_keyword = true;
              }

              StreamString path_string;
              StreamString original_path_string;
              // for OSX we are going to be in
              // .dSYM/Contents/Resources/DWARF/<basename> let us go to
              // .dSYM/Contents/Resources/Python/<basename>.py and see if the
              // file exists
              path_string.Printf("%s/../Python/%s.py",
                                 symfile_spec.GetDirectory().GetCString(),
                                 module_basename.c_str());
              original_path_string.Printf(
                  "%s/../Python/%s.py",
                  symfile_spec.GetDirectory().GetCString(),
                  original_module_basename.c_str());
              FileSpec script_fspec(path_string.GetString());
              FileSystem::Instance().Resolve(script_fspec);
              FileSpec orig_script_fspec(original_path_string.GetString());
              FileSystem::Instance().Resolve(orig_script_fspec);

              // if we did some replacements of reserved characters, and a
              // file with the untampered name exists, then warn the user
              // that the file as-is shall not be loaded
              if (module_basename != original_module_basename &&
                  FileSystem::Instance().Exists(orig_script_fspec)) {
                const char *reason_for_complaint =
                    was_keyword ? "conflicts with a keyword"
                                : "contains reserved characters";
                if (FileSystem::Instance().Exists(script_fspec))
                  feedback_stream.Printf(
                      "warning: the symbol file '%s' contains a debug "
                      "script. However, its name"
                      " '%s' %s and as such cannot be loaded. LLDB will"
                      " load '%s' instead. Consider removing the file with "
                      "the malformed name to"
                      " eliminate this warning.\n",
                      symfile_spec.GetPath().c_str(),
                      original_path_string.GetData(), reason_for_complaint,
                      path_string.GetData());
                else
                  feedback_stream.Printf(
                      "warning: the symbol file '%s' contains a debug "
                      "script. However, its name"
                      " %s and as such cannot be loaded. If you intend"
                      " to have this script loaded, please rename '%s' to "
                      "'%s' and retry.\n",
                      symfile_spec.GetPath().c_str(), reason_for_complaint,
                      original_path_string.GetData(), path_string.GetData());
              }

              if (FileSystem::Instance().Exists(script_fspec)) {
                file_list.Append(script_fspec);
                break;
              }

              // If we didn't find the python file, then keep stripping the
              // extensions and try again
              ConstString filename_no_extension(
                  module_spec.GetFileNameStrippingExtension());
              if (module_spec.GetFilename() == filename_no_extension)
                break;

              module_spec.SetFilename(filename_no_extension);
            }
          }
        }
      }
    }
  }
  return file_list;
}

Status PlatformDarwin::ResolveSymbolFile(Target &target,
                                         const ModuleSpec &sym_spec,
                                         FileSpec &sym_file) {
  sym_file = sym_spec.GetSymbolFileSpec();
  if (FileSystem::Instance().IsDirectory(sym_file)) {
    sym_file = PluginManager::FindSymbolFileInBundle(
        sym_file, sym_spec.GetUUIDPtr(), sym_spec.GetArchitecturePtr());
  }
  return {};
}

Status PlatformDarwin::GetSharedModule(
    const ModuleSpec &module_spec, Process *process, ModuleSP &module_sp,
    const FileSpecList *module_search_paths_ptr,
    llvm::SmallVectorImpl<ModuleSP> *old_modules, bool *did_create_ptr) {
  Status error;
  module_sp.reset();

  if (IsRemote()) {
    // If we have a remote platform always, let it try and locate the shared
    // module first.
    if (m_remote_platform_sp) {
      error = m_remote_platform_sp->GetSharedModule(
          module_spec, process, module_sp, module_search_paths_ptr, old_modules,
          did_create_ptr);
    }
  }

  if (!module_sp) {
    // Fall back to the local platform and find the file locally
    error = Platform::GetSharedModule(module_spec, process, module_sp,
                                      module_search_paths_ptr, old_modules,
                                      did_create_ptr);

    const FileSpec &platform_file = module_spec.GetFileSpec();
    if (!module_sp && module_search_paths_ptr && platform_file) {
      // We can try to pull off part of the file path up to the bundle
      // directory level and try any module search paths...
      FileSpec bundle_directory;
      if (Host::GetBundleDirectory(platform_file, bundle_directory)) {
        if (platform_file == bundle_directory) {
          ModuleSpec new_module_spec(module_spec);
          new_module_spec.GetFileSpec() = bundle_directory;
          if (Host::ResolveExecutableInBundle(new_module_spec.GetFileSpec())) {
            Status new_error(Platform::GetSharedModule(
                new_module_spec, process, module_sp, nullptr, old_modules,
                did_create_ptr));

            if (module_sp)
              return new_error;
          }
        } else {
          char platform_path[PATH_MAX];
          char bundle_dir[PATH_MAX];
          platform_file.GetPath(platform_path, sizeof(platform_path));
          const size_t bundle_directory_len =
              bundle_directory.GetPath(bundle_dir, sizeof(bundle_dir));
          char new_path[PATH_MAX];
          size_t num_module_search_paths = module_search_paths_ptr->GetSize();
          for (size_t i = 0; i < num_module_search_paths; ++i) {
            const size_t search_path_len =
                module_search_paths_ptr->GetFileSpecAtIndex(i).GetPath(
                    new_path, sizeof(new_path));
            if (search_path_len < sizeof(new_path)) {
              snprintf(new_path + search_path_len,
                       sizeof(new_path) - search_path_len, "/%s",
                       platform_path + bundle_directory_len);
              FileSpec new_file_spec(new_path);
              if (FileSystem::Instance().Exists(new_file_spec)) {
                ModuleSpec new_module_spec(module_spec);
                new_module_spec.GetFileSpec() = new_file_spec;
                Status new_error(Platform::GetSharedModule(
                    new_module_spec, process, module_sp, nullptr, old_modules,
                    did_create_ptr));

                if (module_sp) {
                  module_sp->SetPlatformFileSpec(new_file_spec);
                  return new_error;
                }
              }
            }
          }
        }
      }
    }
  }
  if (module_sp)
    module_sp->SetPlatformFileSpec(module_spec.GetFileSpec());
  return error;
}

size_t
PlatformDarwin::GetSoftwareBreakpointTrapOpcode(Target &target,
                                                BreakpointSite *bp_site) {
  const uint8_t *trap_opcode = nullptr;
  uint32_t trap_opcode_size = 0;
  bool bp_is_thumb = false;

  llvm::Triple::ArchType machine = target.GetArchitecture().GetMachine();
  switch (machine) {
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64: {
    // 'brk #0' or 0xd4200000 in BE byte order
    static const uint8_t g_arm64_breakpoint_opcode[] = {0x00, 0x00, 0x20, 0xD4};
    trap_opcode = g_arm64_breakpoint_opcode;
    trap_opcode_size = sizeof(g_arm64_breakpoint_opcode);
  } break;

  case llvm::Triple::thumb:
    bp_is_thumb = true;
    [[fallthrough]];
  case llvm::Triple::arm: {
    static const uint8_t g_arm_breakpoint_opcode[] = {0xFE, 0xDE, 0xFF, 0xE7};
    static const uint8_t g_thumb_breakpooint_opcode[] = {0xFE, 0xDE};

    // Auto detect arm/thumb if it wasn't explicitly specified
    if (!bp_is_thumb) {
      lldb::BreakpointLocationSP bp_loc_sp(bp_site->GetConstituentAtIndex(0));
      if (bp_loc_sp)
        bp_is_thumb = bp_loc_sp->GetAddress().GetAddressClass() ==
                      AddressClass::eCodeAlternateISA;
    }
    if (bp_is_thumb) {
      trap_opcode = g_thumb_breakpooint_opcode;
      trap_opcode_size = sizeof(g_thumb_breakpooint_opcode);
      break;
    }
    trap_opcode = g_arm_breakpoint_opcode;
    trap_opcode_size = sizeof(g_arm_breakpoint_opcode);
  } break;

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64: {
    static const uint8_t g_ppc_breakpoint_opcode[] = {0x7F, 0xC0, 0x00, 0x08};
    trap_opcode = g_ppc_breakpoint_opcode;
    trap_opcode_size = sizeof(g_ppc_breakpoint_opcode);
  } break;

  default:
    return Platform::GetSoftwareBreakpointTrapOpcode(target, bp_site);
  }

  if (trap_opcode && trap_opcode_size) {
    if (bp_site->SetTrapOpcode(trap_opcode, trap_opcode_size))
      return trap_opcode_size;
  }
  return 0;
}

bool PlatformDarwin::ModuleIsExcludedForUnconstrainedSearches(
    lldb_private::Target &target, const lldb::ModuleSP &module_sp) {
  if (!module_sp)
    return false;

  ObjectFile *obj_file = module_sp->GetObjectFile();
  if (!obj_file)
    return false;

  ObjectFile::Type obj_type = obj_file->GetType();
  return obj_type == ObjectFile::eTypeDynamicLinker;
}

void PlatformDarwin::x86GetSupportedArchitectures(
    std::vector<ArchSpec> &archs) {
  ArchSpec host_arch = HostInfo::GetArchitecture(HostInfo::eArchKindDefault);
  archs.push_back(host_arch);

  if (host_arch.GetCore() == ArchSpec::eCore_x86_64_x86_64h) {
    archs.push_back(ArchSpec("x86_64-apple-macosx"));
    archs.push_back(HostInfo::GetArchitecture(HostInfo::eArchKind32));
  } else {
    ArchSpec host_arch64 = HostInfo::GetArchitecture(HostInfo::eArchKind64);
    if (host_arch.IsExactMatch(host_arch64))
      archs.push_back(HostInfo::GetArchitecture(HostInfo::eArchKind32));
  }
}

static llvm::ArrayRef<const char *> GetCompatibleArchs(ArchSpec::Core core) {
  switch (core) {
  default:
    [[fallthrough]];
  case ArchSpec::eCore_arm_arm64e: {
    static const char *g_arm64e_compatible_archs[] = {
        "arm64e",    "arm64",    "armv7",    "armv7f",   "armv7k",   "armv7s",
        "armv7m",    "armv7em",  "armv6m",   "armv6",    "armv5",    "armv4",
        "arm",       "thumbv7",  "thumbv7f", "thumbv7k", "thumbv7s", "thumbv7m",
        "thumbv7em", "thumbv6m", "thumbv6",  "thumbv5",  "thumbv4t", "thumb",
    };
    return {g_arm64e_compatible_archs};
  }
  case ArchSpec::eCore_arm_arm64: {
    static const char *g_arm64_compatible_archs[] = {
        "arm64",    "armv7",    "armv7f",   "armv7k",   "armv7s",   "armv7m",
        "armv7em",  "armv6m",   "armv6",    "armv5",    "armv4",    "arm",
        "thumbv7",  "thumbv7f", "thumbv7k", "thumbv7s", "thumbv7m", "thumbv7em",
        "thumbv6m", "thumbv6",  "thumbv5",  "thumbv4t", "thumb",
    };
    return {g_arm64_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv7: {
    static const char *g_armv7_compatible_archs[] = {
        "armv7",   "armv6m",   "armv6",   "armv5",   "armv4",    "arm",
        "thumbv7", "thumbv6m", "thumbv6", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv7_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv7f: {
    static const char *g_armv7f_compatible_archs[] = {
        "armv7f",  "armv7",   "armv6m",   "armv6",   "armv5",
        "armv4",   "arm",     "thumbv7f", "thumbv7", "thumbv6m",
        "thumbv6", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv7f_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv7k: {
    static const char *g_armv7k_compatible_archs[] = {
        "armv7k",  "armv7",   "armv6m",   "armv6",   "armv5",
        "armv4",   "arm",     "thumbv7k", "thumbv7", "thumbv6m",
        "thumbv6", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv7k_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv7s: {
    static const char *g_armv7s_compatible_archs[] = {
        "armv7s",  "armv7",   "armv6m",   "armv6",   "armv5",
        "armv4",   "arm",     "thumbv7s", "thumbv7", "thumbv6m",
        "thumbv6", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv7s_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv7m: {
    static const char *g_armv7m_compatible_archs[] = {
        "armv7m",  "armv7",   "armv6m",   "armv6",   "armv5",
        "armv4",   "arm",     "thumbv7m", "thumbv7", "thumbv6m",
        "thumbv6", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv7m_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv7em: {
    static const char *g_armv7em_compatible_archs[] = {
        "armv7em", "armv7",   "armv6m",    "armv6",   "armv5",
        "armv4",   "arm",     "thumbv7em", "thumbv7", "thumbv6m",
        "thumbv6", "thumbv5", "thumbv4t",  "thumb",
    };
    return {g_armv7em_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv6m: {
    static const char *g_armv6m_compatible_archs[] = {
        "armv6m",   "armv6",   "armv5",   "armv4",    "arm",
        "thumbv6m", "thumbv6", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv6m_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv6: {
    static const char *g_armv6_compatible_archs[] = {
        "armv6",   "armv5",   "armv4",    "arm",
        "thumbv6", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv6_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv5: {
    static const char *g_armv5_compatible_archs[] = {
        "armv5", "armv4", "arm", "thumbv5", "thumbv4t", "thumb",
    };
    return {g_armv5_compatible_archs};
  }
  case ArchSpec::eCore_arm_armv4: {
    static const char *g_armv4_compatible_archs[] = {
        "armv4",
        "arm",
        "thumbv4t",
        "thumb",
    };
    return {g_armv4_compatible_archs};
  }
  }
  return {};
}

/// The architecture selection rules for arm processors These cpu subtypes have
/// distinct names (e.g. armv7f) but armv7 binaries run fine on an armv7f
/// processor.
void PlatformDarwin::ARMGetSupportedArchitectures(
    std::vector<ArchSpec> &archs, std::optional<llvm::Triple::OSType> os) {
  const ArchSpec system_arch = GetSystemArchitecture();
  const ArchSpec::Core system_core = system_arch.GetCore();
  for (const char *arch : GetCompatibleArchs(system_core)) {
    llvm::Triple triple;
    triple.setArchName(arch);
    triple.setVendor(llvm::Triple::VendorType::Apple);
    if (os)
      triple.setOS(*os);
    archs.push_back(ArchSpec(triple));
  }
}

static FileSpec GetXcodeSelectPath() {
  static FileSpec g_xcode_select_filespec;

  if (!g_xcode_select_filespec) {
    FileSpec xcode_select_cmd("/usr/bin/xcode-select");
    if (FileSystem::Instance().Exists(xcode_select_cmd)) {
      int exit_status = -1;
      int signo = -1;
      std::string command_output;
      Status status =
          Host::RunShellCommand("/usr/bin/xcode-select --print-path",
                                FileSpec(), // current working directory
                                &exit_status, &signo, &command_output,
                                std::chrono::seconds(2), // short timeout
                                false);                  // don't run in a shell
      if (status.Success() && exit_status == 0 && !command_output.empty()) {
        size_t first_non_newline = command_output.find_last_not_of("\r\n");
        if (first_non_newline != std::string::npos) {
          command_output.erase(first_non_newline + 1);
        }
        g_xcode_select_filespec = FileSpec(command_output);
      }
    }
  }

  return g_xcode_select_filespec;
}

BreakpointSP PlatformDarwin::SetThreadCreationBreakpoint(Target &target) {
  BreakpointSP bp_sp;
  static const char *g_bp_names[] = {
      "start_wqthread", "_pthread_wqthread", "_pthread_start",
  };

  static const char *g_bp_modules[] = {"libsystem_c.dylib",
                                       "libSystem.B.dylib"};

  FileSpecList bp_modules;
  for (size_t i = 0; i < std::size(g_bp_modules); i++) {
    const char *bp_module = g_bp_modules[i];
    bp_modules.EmplaceBack(bp_module);
  }

  bool internal = true;
  bool hardware = false;
  LazyBool skip_prologue = eLazyBoolNo;
  bp_sp = target.CreateBreakpoint(&bp_modules, nullptr, g_bp_names,
                                  std::size(g_bp_names), eFunctionNameTypeFull,
                                  eLanguageTypeUnknown, 0, skip_prologue,
                                  internal, hardware);
  bp_sp->SetBreakpointKind("thread-creation");

  return bp_sp;
}

uint32_t
PlatformDarwin::GetResumeCountForLaunchInfo(ProcessLaunchInfo &launch_info) {
  const FileSpec &shell = launch_info.GetShell();
  if (!shell)
    return 1;

  std::string shell_string = shell.GetPath();
  const char *shell_name = strrchr(shell_string.c_str(), '/');
  if (shell_name == nullptr)
    shell_name = shell_string.c_str();
  else
    shell_name++;

  if (strcmp(shell_name, "sh") == 0) {
    // /bin/sh re-exec's itself as /bin/bash requiring another resume. But it
    // only does this if the COMMAND_MODE environment variable is set to
    // "legacy".
    if (launch_info.GetEnvironment().lookup("COMMAND_MODE") == "legacy")
      return 2;
    return 1;
  } else if (strcmp(shell_name, "csh") == 0 ||
             strcmp(shell_name, "tcsh") == 0 ||
             strcmp(shell_name, "zsh") == 0) {
    // csh and tcsh always seem to re-exec themselves.
    return 2;
  } else
    return 1;
}

lldb::ProcessSP PlatformDarwin::DebugProcess(ProcessLaunchInfo &launch_info,
                                             Debugger &debugger, Target &target,
                                             Status &error) {
  ProcessSP process_sp;

  if (IsHost()) {
    // We are going to hand this process off to debugserver which will be in
    // charge of setting the exit status.  However, we still need to reap it
    // from lldb. So, make sure we use a exit callback which does not set exit
    // status.
    launch_info.SetMonitorProcessCallback(
        &ProcessLaunchInfo::NoOpMonitorCallback);
    process_sp = Platform::DebugProcess(launch_info, debugger, target, error);
  } else {
    if (m_remote_platform_sp)
      process_sp = m_remote_platform_sp->DebugProcess(launch_info, debugger,
                                                      target, error);
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return process_sp;
}

void PlatformDarwin::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

static FileSpec GetCommandLineToolsLibraryPath() {
  static FileSpec g_command_line_tools_filespec;

  if (!g_command_line_tools_filespec) {
    FileSpec command_line_tools_path(GetXcodeSelectPath());
    command_line_tools_path.AppendPathComponent("Library");
    if (FileSystem::Instance().Exists(command_line_tools_path)) {
      g_command_line_tools_filespec = command_line_tools_path;
    }
  }

  return g_command_line_tools_filespec;
}

FileSystem::EnumerateDirectoryResult PlatformDarwin::DirectoryEnumerator(
    void *baton, llvm::sys::fs::file_type file_type, llvm::StringRef path) {
  SDKEnumeratorInfo *enumerator_info = static_cast<SDKEnumeratorInfo *>(baton);

  FileSpec spec(path);
  if (XcodeSDK::SDKSupportsModules(enumerator_info->sdk_type, spec)) {
    enumerator_info->found_path = spec;
    return FileSystem::EnumerateDirectoryResult::eEnumerateDirectoryResultNext;
  }

  return FileSystem::EnumerateDirectoryResult::eEnumerateDirectoryResultNext;
}

FileSpec PlatformDarwin::FindSDKInXcodeForModules(XcodeSDK::Type sdk_type,
                                                  const FileSpec &sdks_spec) {
  // Look inside Xcode for the required installed iOS SDK version

  if (!FileSystem::Instance().IsDirectory(sdks_spec)) {
    return FileSpec();
  }

  const bool find_directories = true;
  const bool find_files = false;
  const bool find_other = true; // include symlinks

  SDKEnumeratorInfo enumerator_info;

  enumerator_info.sdk_type = sdk_type;

  FileSystem::Instance().EnumerateDirectory(
      sdks_spec.GetPath(), find_directories, find_files, find_other,
      DirectoryEnumerator, &enumerator_info);

  if (FileSystem::Instance().IsDirectory(enumerator_info.found_path))
    return enumerator_info.found_path;
  else
    return FileSpec();
}

FileSpec PlatformDarwin::GetSDKDirectoryForModules(XcodeSDK::Type sdk_type) {
  FileSpec sdks_spec = HostInfo::GetXcodeContentsDirectory();
  sdks_spec.AppendPathComponent("Developer");
  sdks_spec.AppendPathComponent("Platforms");

  switch (sdk_type) {
  case XcodeSDK::Type::MacOSX:
    sdks_spec.AppendPathComponent("MacOSX.platform");
    break;
  case XcodeSDK::Type::iPhoneSimulator:
    sdks_spec.AppendPathComponent("iPhoneSimulator.platform");
    break;
  case XcodeSDK::Type::iPhoneOS:
    sdks_spec.AppendPathComponent("iPhoneOS.platform");
    break;
  case XcodeSDK::Type::WatchSimulator:
    sdks_spec.AppendPathComponent("WatchSimulator.platform");
    break;
  case XcodeSDK::Type::AppleTVSimulator:
    sdks_spec.AppendPathComponent("AppleTVSimulator.platform");
    break;
  case XcodeSDK::Type::XRSimulator:
    sdks_spec.AppendPathComponent("XRSimulator.platform");
    break;
  default:
    llvm_unreachable("unsupported sdk");
  }

  sdks_spec.AppendPathComponent("Developer");
  sdks_spec.AppendPathComponent("SDKs");

  if (sdk_type == XcodeSDK::Type::MacOSX) {
    llvm::VersionTuple version = HostInfo::GetOSVersion();

    if (!version.empty()) {
      if (XcodeSDK::SDKSupportsModules(XcodeSDK::Type::MacOSX, version)) {
        // If the Xcode SDKs are not available then try to use the
        // Command Line Tools one which is only for MacOSX.
        if (!FileSystem::Instance().Exists(sdks_spec)) {
          sdks_spec = GetCommandLineToolsLibraryPath();
          sdks_spec.AppendPathComponent("SDKs");
        }

        // We slightly prefer the exact SDK for this machine.  See if it is
        // there.

        FileSpec native_sdk_spec = sdks_spec;
        StreamString native_sdk_name;
        native_sdk_name.Printf("MacOSX%u.%u.sdk", version.getMajor(),
                               version.getMinor().value_or(0));
        native_sdk_spec.AppendPathComponent(native_sdk_name.GetString());

        if (FileSystem::Instance().Exists(native_sdk_spec)) {
          return native_sdk_spec;
        }
      }
    }
  }

  return FindSDKInXcodeForModules(sdk_type, sdks_spec);
}

std::tuple<llvm::VersionTuple, llvm::StringRef>
PlatformDarwin::ParseVersionBuildDir(llvm::StringRef dir) {
  llvm::StringRef build;
  llvm::StringRef version_str;
  llvm::StringRef build_str;
  std::tie(version_str, build_str) = dir.split(' ');
  llvm::VersionTuple version;
  if (!version.tryParse(version_str) ||
      build_str.empty()) {
    if (build_str.consume_front("(")) {
      size_t pos = build_str.find(')');
      build = build_str.slice(0, pos);
    }
  }

  return std::make_tuple(version, build);
}

llvm::Expected<StructuredData::DictionarySP>
PlatformDarwin::FetchExtendedCrashInformation(Process &process) {
  StructuredData::DictionarySP extended_crash_info =
      std::make_shared<StructuredData::Dictionary>();

  StructuredData::ArraySP annotations = ExtractCrashInfoAnnotations(process);
  if (annotations && annotations->GetSize())
    extended_crash_info->AddItem("Crash-Info Annotations", annotations);

  StructuredData::DictionarySP app_specific_info =
      ExtractAppSpecificInfo(process);
  if (app_specific_info && app_specific_info->GetSize())
    extended_crash_info->AddItem("Application Specific Information",
                                 app_specific_info);

  return extended_crash_info->GetSize() ? extended_crash_info : nullptr;
}

StructuredData::ArraySP
PlatformDarwin::ExtractCrashInfoAnnotations(Process &process) {
  Log *log = GetLog(LLDBLog::Process);

  ConstString section_name("__crash_info");
  Target &target = process.GetTarget();
  StructuredData::ArraySP array_sp = std::make_shared<StructuredData::Array>();

  for (ModuleSP module : target.GetImages().Modules()) {
    SectionList *sections = module->GetSectionList();

    std::string module_name = module->GetSpecificationDescription();

    // The DYDL module is skipped since it's always loaded when running the
    // binary.
    if (module_name == "/usr/lib/dyld")
      continue;

    if (!sections) {
      LLDB_LOG(log, "Module {0} doesn't have any section!", module_name);
      continue;
    }

    SectionSP crash_info = sections->FindSectionByName(section_name);
    if (!crash_info) {
      LLDB_LOG(log, "Module {0} doesn't have section {1}!", module_name,
               section_name);
      continue;
    }

    addr_t load_addr = crash_info->GetLoadBaseAddress(&target);

    if (load_addr == LLDB_INVALID_ADDRESS) {
      LLDB_LOG(log, "Module {0} has an invalid '{1}' section load address: {2}",
               module_name, section_name, load_addr);
      continue;
    }

    Status error;
    CrashInfoAnnotations annotations;
    size_t expected_size = sizeof(CrashInfoAnnotations);
    size_t bytes_read = process.ReadMemoryFromInferior(load_addr, &annotations,
                                                       expected_size, error);

    if (expected_size != bytes_read || error.Fail()) {
      LLDB_LOG(log, "Failed to read {0} section from memory in module {1}: {2}",
               section_name, module_name, error);
      continue;
    }

    // initial support added for version 5
    if (annotations.version < 5) {
      LLDB_LOG(log,
               "Annotation version lower than 5 unsupported! Module {0} has "
               "version {1} instead.",
               module_name, annotations.version);
      continue;
    }

    if (!annotations.message) {
      LLDB_LOG(log, "No message available for module {0}.", module_name);
      continue;
    }

    std::string message;
    bytes_read =
        process.ReadCStringFromMemory(annotations.message, message, error);

    if (message.empty() || bytes_read != message.size() || error.Fail()) {
      LLDB_LOG(log, "Failed to read the message from memory in module {0}: {1}",
               module_name, error);
      continue;
    }

    // Remove trailing newline from message
    if (message.back() == '\n')
      message.pop_back();

    if (!annotations.message2)
      LLDB_LOG(log, "No message2 available for module {0}.", module_name);

    std::string message2;
    bytes_read =
        process.ReadCStringFromMemory(annotations.message2, message2, error);

    if (!message2.empty() && bytes_read == message2.size() && error.Success())
      if (message2.back() == '\n')
        message2.pop_back();

    StructuredData::DictionarySP entry_sp =
        std::make_shared<StructuredData::Dictionary>();

    entry_sp->AddStringItem("image", module->GetFileSpec().GetPath(false));
    entry_sp->AddStringItem("uuid", module->GetUUID().GetAsString());
    entry_sp->AddStringItem("message", message);
    entry_sp->AddStringItem("message2", message2);
    entry_sp->AddIntegerItem("abort-cause", annotations.abort_cause);

    array_sp->AddItem(entry_sp);
  }

  return array_sp;
}

StructuredData::DictionarySP
PlatformDarwin::ExtractAppSpecificInfo(Process &process) {
  StructuredData::DictionarySP metadata_sp = process.GetMetadata();

  if (!metadata_sp || !metadata_sp->GetSize() || !metadata_sp->HasKey("asi"))
    return {};

  StructuredData::Dictionary *asi;
  if (!metadata_sp->GetValueForKeyAsDictionary("asi", asi))
    return {};

  StructuredData::DictionarySP dict_sp =
      std::make_shared<StructuredData::Dictionary>();

  auto flatten_asi_dict = [&dict_sp](llvm::StringRef key,
                                     StructuredData::Object *val) -> bool {
    if (!val)
      return false;

    StructuredData::Array *arr = val->GetAsArray();
    if (!arr || !arr->GetSize())
      return false;

    dict_sp->AddItem(key, arr->GetItemAtIndex(0));
    return true;
  };

  asi->ForEach(flatten_asi_dict);

  return dict_sp;
}

void PlatformDarwin::AddClangModuleCompilationOptionsForSDKType(
    Target *target, std::vector<std::string> &options, XcodeSDK::Type sdk_type) {
  const std::vector<std::string> apple_arguments = {
      "-x",       "objective-c++", "-fobjc-arc",
      "-fblocks", "-D_ISO646_H",   "-D__ISO646_H",
      "-fgnuc-version=4.2.1"};

  options.insert(options.end(), apple_arguments.begin(), apple_arguments.end());

  StreamString minimum_version_option;
  bool use_current_os_version = false;
  // If the SDK type is for the host OS, use its version number.
  auto get_host_os = []() { return HostInfo::GetTargetTriple().getOS(); };
  switch (sdk_type) {
  case XcodeSDK::Type::MacOSX:
    use_current_os_version = get_host_os() == llvm::Triple::MacOSX;
    break;
  case XcodeSDK::Type::iPhoneOS:
    use_current_os_version = get_host_os() == llvm::Triple::IOS;
    break;
  case XcodeSDK::Type::AppleTVOS:
    use_current_os_version = get_host_os() == llvm::Triple::TvOS;
    break;
  case XcodeSDK::Type::watchOS:
    use_current_os_version = get_host_os() == llvm::Triple::WatchOS;
    break;
  case XcodeSDK::Type::XROS:
    use_current_os_version = get_host_os() == llvm::Triple::XROS;
    break;
  default:
    break;
  }

  llvm::VersionTuple version;
  if (use_current_os_version)
    version = GetOSVersion();
  else if (target) {
    // Our OS doesn't match our executable so we need to get the min OS version
    // from the object file
    ModuleSP exe_module_sp = target->GetExecutableModule();
    if (exe_module_sp) {
      ObjectFile *object_file = exe_module_sp->GetObjectFile();
      if (object_file)
        version = object_file->GetMinimumOSVersion();
    }
  }
  // Only add the version-min options if we got a version from somewhere.
  // clang has no version-min clang flag for XROS.
  if (!version.empty() && sdk_type != XcodeSDK::Type::Linux &&
      sdk_type != XcodeSDK::Type::XROS) {
#define OPTION(PREFIX, NAME, VAR, ...)                                         \
  llvm::StringRef opt_##VAR = NAME;                                            \
  (void)opt_##VAR;
#include "clang/Driver/Options.inc"
#undef OPTION
    minimum_version_option << '-';
    switch (sdk_type) {
    case XcodeSDK::Type::MacOSX:
      minimum_version_option << opt_mmacos_version_min_EQ;
      break;
    case XcodeSDK::Type::iPhoneSimulator:
      minimum_version_option << opt_mios_simulator_version_min_EQ;
      break;
    case XcodeSDK::Type::iPhoneOS:
      minimum_version_option << opt_mios_version_min_EQ;
      break;
    case XcodeSDK::Type::AppleTVSimulator:
      minimum_version_option << opt_mtvos_simulator_version_min_EQ;
      break;
    case XcodeSDK::Type::AppleTVOS:
      minimum_version_option << opt_mtvos_version_min_EQ;
      break;
    case XcodeSDK::Type::WatchSimulator:
      minimum_version_option << opt_mwatchos_simulator_version_min_EQ;
      break;
    case XcodeSDK::Type::watchOS:
      minimum_version_option << opt_mwatchos_version_min_EQ;
      break;
    case XcodeSDK::Type::XRSimulator:
    case XcodeSDK::Type::XROS:
      // FIXME: Pass the right argument once it exists.
    case XcodeSDK::Type::bridgeOS:
    case XcodeSDK::Type::Linux:
    case XcodeSDK::Type::unknown:
      if (Log *log = GetLog(LLDBLog::Host)) {
        XcodeSDK::Info info;
        info.type = sdk_type;
        LLDB_LOGF(log, "Clang modules on %s are not supported",
                  XcodeSDK::GetCanonicalName(info).c_str());
      }
      return;
    }
    minimum_version_option << version.getAsString();
    options.emplace_back(std::string(minimum_version_option.GetString()));
  }

  FileSpec sysroot_spec;

  if (target) {
    if (ModuleSP exe_module_sp = target->GetExecutableModule()) {
      auto path_or_err = ResolveSDKPathFromDebugInfo(*exe_module_sp);
      if (path_or_err) {
        sysroot_spec = FileSpec(*path_or_err);
      } else {
        LLDB_LOG_ERROR(GetLog(LLDBLog::Types | LLDBLog::Host),
                       path_or_err.takeError(),
                       "Failed to resolve SDK path: {0}");
      }
    }
  }

  if (!FileSystem::Instance().IsDirectory(sysroot_spec.GetPath())) {
    std::lock_guard<std::mutex> guard(m_mutex);
    sysroot_spec = GetSDKDirectoryForModules(sdk_type);
  }

  if (FileSystem::Instance().IsDirectory(sysroot_spec.GetPath())) {
    options.push_back("-isysroot");
    options.push_back(sysroot_spec.GetPath());
  }
}

ConstString PlatformDarwin::GetFullNameForDylib(ConstString basename) {
  if (basename.IsEmpty())
    return basename;

  StreamString stream;
  stream.Printf("lib%s.dylib", basename.GetCString());
  return ConstString(stream.GetString());
}

llvm::VersionTuple PlatformDarwin::GetOSVersion(Process *process) {
  if (process && GetPluginName().contains("-simulator")) {
    lldb_private::ProcessInstanceInfo proc_info;
    if (Host::GetProcessInfo(process->GetID(), proc_info)) {
      const Environment &env = proc_info.GetEnvironment();

      llvm::VersionTuple result;
      if (!result.tryParse(env.lookup("SIMULATOR_RUNTIME_VERSION")))
        return result;

      std::string dyld_root_path = env.lookup("DYLD_ROOT_PATH");
      if (!dyld_root_path.empty()) {
        dyld_root_path += "/System/Library/CoreServices/SystemVersion.plist";
        ApplePropertyList system_version_plist(dyld_root_path.c_str());
        std::string product_version;
        if (system_version_plist.GetValueAsString("ProductVersion",
                                                  product_version)) {
          if (!result.tryParse(product_version))
            return result;
        }
      }
    }
    // For simulator platforms, do NOT call back through
    // Platform::GetOSVersion() as it might call Process::GetHostOSVersion()
    // which we don't want as it will be incorrect
    return llvm::VersionTuple();
  }

  return Platform::GetOSVersion(process);
}

lldb_private::FileSpec PlatformDarwin::LocateExecutable(const char *basename) {
  // A collection of SBFileSpec whose SBFileSpec.m_directory members are filled
  // in with any executable directories that should be searched.
  static std::vector<FileSpec> g_executable_dirs;

  // Find the global list of directories that we will search for executables
  // once so we don't keep doing the work over and over.
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {

    // When locating executables, trust the DEVELOPER_DIR first if it is set
    FileSpec xcode_contents_dir = HostInfo::GetXcodeContentsDirectory();
    if (xcode_contents_dir) {
      FileSpec xcode_lldb_resources = xcode_contents_dir;
      xcode_lldb_resources.AppendPathComponent("SharedFrameworks");
      xcode_lldb_resources.AppendPathComponent("LLDB.framework");
      xcode_lldb_resources.AppendPathComponent("Resources");
      if (FileSystem::Instance().Exists(xcode_lldb_resources)) {
        FileSpec dir;
        dir.SetDirectory(xcode_lldb_resources.GetPathAsConstString());
        g_executable_dirs.push_back(dir);
      }
    }
    // Xcode might not be installed so we also check for the Command Line Tools.
    FileSpec command_line_tools_dir = GetCommandLineToolsLibraryPath();
    if (command_line_tools_dir) {
      FileSpec cmd_line_lldb_resources = command_line_tools_dir;
      cmd_line_lldb_resources.AppendPathComponent("PrivateFrameworks");
      cmd_line_lldb_resources.AppendPathComponent("LLDB.framework");
      cmd_line_lldb_resources.AppendPathComponent("Resources");
      if (FileSystem::Instance().Exists(cmd_line_lldb_resources)) {
        FileSpec dir;
        dir.SetDirectory(cmd_line_lldb_resources.GetPathAsConstString());
        g_executable_dirs.push_back(dir);
      }
    }
  });

  // Now search the global list of executable directories for the executable we
  // are looking for
  for (const auto &executable_dir : g_executable_dirs) {
    FileSpec executable_file;
    executable_file.SetDirectory(executable_dir.GetDirectory());
    executable_file.SetFilename(basename);
    if (FileSystem::Instance().Exists(executable_file))
      return executable_file;
  }

  return FileSpec();
}

lldb_private::Status
PlatformDarwin::LaunchProcess(lldb_private::ProcessLaunchInfo &launch_info) {
  // Starting in Fall 2016 OSes, NSLog messages only get mirrored to stderr if
  // the OS_ACTIVITY_DT_MODE environment variable is set.  (It doesn't require
  // any specific value; rather, it just needs to exist). We will set it here
  // as long as the IDE_DISABLED_OS_ACTIVITY_DT_MODE flag is not set.  Xcode
  // makes use of IDE_DISABLED_OS_ACTIVITY_DT_MODE to tell
  // LLDB *not* to muck with the OS_ACTIVITY_DT_MODE flag when they
  // specifically want it unset.
  const char *disable_env_var = "IDE_DISABLED_OS_ACTIVITY_DT_MODE";
  auto &env_vars = launch_info.GetEnvironment();
  if (!env_vars.count(disable_env_var)) {
    // We want to make sure that OS_ACTIVITY_DT_MODE is set so that we get
    // os_log and NSLog messages mirrored to the target process stderr.
    env_vars.try_emplace("OS_ACTIVITY_DT_MODE", "enable");
  }

  // Let our parent class do the real launching.
  return PlatformPOSIX::LaunchProcess(launch_info);
}

lldb_private::Status PlatformDarwin::FindBundleBinaryInExecSearchPaths(
    const ModuleSpec &module_spec, Process *process, ModuleSP &module_sp,
    const FileSpecList *module_search_paths_ptr,
    llvm::SmallVectorImpl<ModuleSP> *old_modules, bool *did_create_ptr) {
  const FileSpec &platform_file = module_spec.GetFileSpec();
  // See if the file is present in any of the module_search_paths_ptr
  // directories.
  if (!module_sp && module_search_paths_ptr && platform_file) {
    // create a vector of all the file / directory names in platform_file e.g.
    // this might be
    // /System/Library/PrivateFrameworks/UIFoundation.framework/UIFoundation
    //
    // We'll need to look in the module_search_paths_ptr directories for both
    // "UIFoundation" and "UIFoundation.framework" -- most likely the latter
    // will be the one we find there.

    std::vector<llvm::StringRef> path_parts = platform_file.GetComponents();
    // We want the components in reverse order.
    std::reverse(path_parts.begin(), path_parts.end());
    const size_t path_parts_size = path_parts.size();

    size_t num_module_search_paths = module_search_paths_ptr->GetSize();
    for (size_t i = 0; i < num_module_search_paths; ++i) {
      Log *log_verbose = GetLog(LLDBLog::Host);
      LLDB_LOGF(
          log_verbose,
          "PlatformRemoteDarwinDevice::GetSharedModule searching for binary in "
          "search-path %s",
          module_search_paths_ptr->GetFileSpecAtIndex(i).GetPath().c_str());
      // Create a new FileSpec with this module_search_paths_ptr plus just the
      // filename ("UIFoundation"), then the parent dir plus filename
      // ("UIFoundation.framework/UIFoundation") etc - up to four names (to
      // handle "Foo.framework/Contents/MacOS/Foo")

      for (size_t j = 0; j < 4 && j < path_parts_size - 1; ++j) {
        FileSpec path_to_try(module_search_paths_ptr->GetFileSpecAtIndex(i));

        // Add the components backwards.  For
        // .../PrivateFrameworks/UIFoundation.framework/UIFoundation path_parts
        // is
        //   [0] UIFoundation
        //   [1] UIFoundation.framework
        //   [2] PrivateFrameworks
        //
        // and if 'j' is 2, we want to append path_parts[1] and then
        // path_parts[0], aka 'UIFoundation.framework/UIFoundation', to the
        // module_search_paths_ptr path.

        for (int k = j; k >= 0; --k) {
          path_to_try.AppendPathComponent(path_parts[k]);
        }

        if (FileSystem::Instance().Exists(path_to_try)) {
          ModuleSpec new_module_spec(module_spec);
          new_module_spec.GetFileSpec() = path_to_try;
          Status new_error(
              Platform::GetSharedModule(new_module_spec, process, module_sp,
                                        nullptr, old_modules, did_create_ptr));

          if (module_sp) {
            module_sp->SetPlatformFileSpec(path_to_try);
            return new_error;
          }
        }
      }
    }
  }
  return Status();
}

std::string PlatformDarwin::FindComponentInPath(llvm::StringRef path,
                                                llvm::StringRef component) {
  auto begin = llvm::sys::path::begin(path);
  auto end = llvm::sys::path::end(path);
  for (auto it = begin; it != end; ++it) {
    if (it->contains(component)) {
      llvm::SmallString<128> buffer;
      llvm::sys::path::append(buffer, begin, ++it,
                              llvm::sys::path::Style::posix);
      return buffer.str().str();
    }
  }
  return {};
}

FileSpec PlatformDarwin::GetCurrentToolchainDirectory() {
  if (FileSpec fspec = HostInfo::GetShlibDir())
    return FileSpec(FindComponentInPath(fspec.GetPath(), ".xctoolchain"));
  return {};
}

FileSpec PlatformDarwin::GetCurrentCommandLineToolsDirectory() {
  if (FileSpec fspec = HostInfo::GetShlibDir())
    return FileSpec(FindComponentInPath(fspec.GetPath(), "CommandLineTools"));
  return {};
}

llvm::Triple::OSType PlatformDarwin::GetHostOSType() {
#if !defined(__APPLE__)
  return llvm::Triple::MacOSX;
#else
#if TARGET_OS_OSX
  return llvm::Triple::MacOSX;
#elif TARGET_OS_IOS
  return llvm::Triple::IOS;
#elif TARGET_OS_WATCH
  return llvm::Triple::WatchOS;
#elif TARGET_OS_TV
  return llvm::Triple::TvOS;
#elif TARGET_OS_BRIDGE
  return llvm::Triple::BridgeOS;
#elif TARGET_OS_XR
  return llvm::Triple::XROS;
#else
#error "LLDB being compiled for an unrecognized Darwin OS"
#endif
#endif // __APPLE__
}

llvm::Expected<std::pair<XcodeSDK, bool>>
PlatformDarwin::GetSDKPathFromDebugInfo(Module &module) {
  SymbolFile *sym_file = module.GetSymbolFile();
  if (!sym_file)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        llvm::formatv("No symbol file available for module '{0}'",
                      module.GetFileSpec().GetFilename().AsCString("")));

  bool found_public_sdk = false;
  bool found_internal_sdk = false;
  XcodeSDK merged_sdk;
  for (unsigned i = 0; i < sym_file->GetNumCompileUnits(); ++i) {
    if (auto cu_sp = sym_file->GetCompileUnitAtIndex(i)) {
      auto cu_sdk = sym_file->ParseXcodeSDK(*cu_sp);
      bool is_internal_sdk = cu_sdk.IsAppleInternalSDK();
      found_public_sdk |= !is_internal_sdk;
      found_internal_sdk |= is_internal_sdk;

      merged_sdk.Merge(cu_sdk);
    }
  }

  const bool found_mismatch = found_internal_sdk && found_public_sdk;

  return std::pair{std::move(merged_sdk), found_mismatch};
}

llvm::Expected<std::string>
PlatformDarwin::ResolveSDKPathFromDebugInfo(Module &module) {
  auto sdk_or_err = GetSDKPathFromDebugInfo(module);
  if (!sdk_or_err)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        llvm::formatv("Failed to parse SDK path from debug-info: {0}",
                      llvm::toString(sdk_or_err.takeError())));

  auto [sdk, _] = std::move(*sdk_or_err);

  auto path_or_err = HostInfo::GetSDKRoot(HostInfo::SDKOptions{sdk});
  if (!path_or_err)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        llvm::formatv("Error while searching for SDK (XcodeSDK '{0}'): {1}",
                      sdk.GetString(),
                      llvm::toString(path_or_err.takeError())));

  return path_or_err->str();
}
