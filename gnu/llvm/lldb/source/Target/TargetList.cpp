//===-- TargetList.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/TargetList.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionGroupPlatform.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/TildeExpressionResolver.h"
#include "lldb/Utility/Timer.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"

using namespace lldb;
using namespace lldb_private;

llvm::StringRef TargetList::GetStaticBroadcasterClass() {
  static constexpr llvm::StringLiteral class_name("lldb.targetList");
  return class_name;
}

// TargetList constructor
TargetList::TargetList(Debugger &debugger)
    : Broadcaster(debugger.GetBroadcasterManager(),
                  TargetList::GetStaticBroadcasterClass().str()),
      m_target_list(), m_target_list_mutex(), m_selected_target_idx(0) {
  CheckInWithManager();
}

Status TargetList::CreateTarget(Debugger &debugger,
                                llvm::StringRef user_exe_path,
                                llvm::StringRef triple_str,
                                LoadDependentFiles load_dependent_files,
                                const OptionGroupPlatform *platform_options,
                                TargetSP &target_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto result = TargetList::CreateTargetInternal(
      debugger, user_exe_path, triple_str, load_dependent_files,
      platform_options, target_sp);

  if (target_sp && result.Success())
    AddTargetInternal(target_sp, /*do_select*/ true);
  return result;
}

Status TargetList::CreateTarget(Debugger &debugger,
                                llvm::StringRef user_exe_path,
                                const ArchSpec &specified_arch,
                                LoadDependentFiles load_dependent_files,
                                PlatformSP &platform_sp, TargetSP &target_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto result = TargetList::CreateTargetInternal(
      debugger, user_exe_path, specified_arch, load_dependent_files,
      platform_sp, target_sp);

  if (target_sp && result.Success())
    AddTargetInternal(target_sp, /*do_select*/ true);
  return result;
}

Status TargetList::CreateTargetInternal(
    Debugger &debugger, llvm::StringRef user_exe_path,
    llvm::StringRef triple_str, LoadDependentFiles load_dependent_files,
    const OptionGroupPlatform *platform_options, TargetSP &target_sp) {
  Status error;

  PlatformList &platform_list = debugger.GetPlatformList();
  // Let's start by looking at the selected platform.
  PlatformSP platform_sp = platform_list.GetSelectedPlatform();

  // This variable corresponds to the architecture specified by the triple
  // string. If that string was empty the currently selected platform will
  // determine the architecture.
  const ArchSpec arch(triple_str);
  if (!triple_str.empty() && !arch.IsValid()) {
    error.SetErrorStringWithFormat("invalid triple '%s'",
                                   triple_str.str().c_str());
    return error;
  }

  ArchSpec platform_arch(arch);

  // Create a new platform if a platform was specified in the platform options
  // and doesn't match the selected platform.
  if (platform_options && platform_options->PlatformWasSpecified() &&
      !platform_options->PlatformMatches(platform_sp)) {
    const bool select_platform = true;
    platform_sp = platform_options->CreatePlatformWithOptions(
        debugger.GetCommandInterpreter(), arch, select_platform, error,
        platform_arch);
    if (!platform_sp)
      return error;
  }

  bool prefer_platform_arch = false;
  auto update_platform_arch = [&](const ArchSpec &module_arch) {
    // If the OS or vendor weren't specified, then adopt the module's
    // architecture so that the platform matching can be more accurate.
    if (!platform_arch.TripleOSWasSpecified() ||
        !platform_arch.TripleVendorWasSpecified()) {
      prefer_platform_arch = true;
      platform_arch = module_arch;
    }
  };

  if (!user_exe_path.empty()) {
    ModuleSpec module_spec(FileSpec(user_exe_path, FileSpec::Style::native));
    FileSystem::Instance().Resolve(module_spec.GetFileSpec());

    // Try to resolve the exe based on PATH and/or platform-specific suffixes,
    // but only if using the host platform.
    if (platform_sp->IsHost() &&
        !FileSystem::Instance().Exists(module_spec.GetFileSpec()))
      FileSystem::Instance().ResolveExecutableLocation(
          module_spec.GetFileSpec());

    // Resolve the executable in case we are given a path to a application
    // bundle like a .app bundle on MacOSX.
    Host::ResolveExecutableInBundle(module_spec.GetFileSpec());

    lldb::offset_t file_offset = 0;
    lldb::offset_t file_size = 0;
    ModuleSpecList module_specs;
    const size_t num_specs = ObjectFile::GetModuleSpecifications(
        module_spec.GetFileSpec(), file_offset, file_size, module_specs);

    if (num_specs > 0) {
      ModuleSpec matching_module_spec;

      if (num_specs == 1) {
        if (module_specs.GetModuleSpecAtIndex(0, matching_module_spec)) {
          if (platform_arch.IsValid()) {
            if (platform_arch.IsCompatibleMatch(
                    matching_module_spec.GetArchitecture())) {
              // If the OS or vendor weren't specified, then adopt the module's
              // architecture so that the platform matching can be more
              // accurate.
              update_platform_arch(matching_module_spec.GetArchitecture());
            } else {
              StreamString platform_arch_strm;
              StreamString module_arch_strm;

              platform_arch.DumpTriple(platform_arch_strm.AsRawOstream());
              matching_module_spec.GetArchitecture().DumpTriple(
                  module_arch_strm.AsRawOstream());
              error.SetErrorStringWithFormat(
                  "the specified architecture '%s' is not compatible with '%s' "
                  "in '%s'",
                  platform_arch_strm.GetData(), module_arch_strm.GetData(),
                  module_spec.GetFileSpec().GetPath().c_str());
              return error;
            }
          } else {
            // Only one arch and none was specified.
            prefer_platform_arch = true;
            platform_arch = matching_module_spec.GetArchitecture();
          }
        }
      } else if (arch.IsValid()) {
        // Fat binary. A (valid) architecture was specified.
        module_spec.GetArchitecture() = arch;
        if (module_specs.FindMatchingModuleSpec(module_spec,
                                                matching_module_spec))
            update_platform_arch(matching_module_spec.GetArchitecture());
      } else {
        // Fat binary. No architecture specified, check if there is
        // only one platform for all of the architectures.
        std::vector<PlatformSP> candidates;
        std::vector<ArchSpec> archs;
        for (const ModuleSpec &spec : module_specs.ModuleSpecs())
          archs.push_back(spec.GetArchitecture());
        if (PlatformSP platform_for_archs_sp =
                platform_list.GetOrCreate(archs, {}, candidates)) {
          platform_sp = platform_for_archs_sp;
        } else if (candidates.empty()) {
          error.SetErrorString("no matching platforms found for this file");
          return error;
        } else {
          // More than one platform claims to support this file.
          StreamString error_strm;
          std::set<llvm::StringRef> platform_set;
          error_strm.Printf(
              "more than one platform supports this executable (");
          for (const auto &candidate : candidates) {
            llvm::StringRef platform_name = candidate->GetName();
            if (platform_set.count(platform_name))
              continue;
            if (!platform_set.empty())
              error_strm.PutCString(", ");
            error_strm.PutCString(platform_name);
            platform_set.insert(platform_name);
          }
          error_strm.Printf("), specify an architecture to disambiguate");
          error.SetErrorString(error_strm.GetString());
          return error;
        }
      }
    }
  }

  // If we have a valid architecture, make sure the current platform is
  // compatible with that architecture.
  if (!prefer_platform_arch && arch.IsValid()) {
    if (!platform_sp->IsCompatibleArchitecture(
            arch, {}, ArchSpec::CompatibleMatch, nullptr)) {
      platform_sp = platform_list.GetOrCreate(arch, {}, &platform_arch);
      if (platform_sp)
        platform_list.SetSelectedPlatform(platform_sp);
    }
  } else if (platform_arch.IsValid()) {
    // If "arch" isn't valid, yet "platform_arch" is, it means we have an
    // executable file with a single architecture which should be used.
    ArchSpec fixed_platform_arch;
    if (!platform_sp->IsCompatibleArchitecture(
            platform_arch, {}, ArchSpec::CompatibleMatch, nullptr)) {
      platform_sp =
          platform_list.GetOrCreate(platform_arch, {}, &fixed_platform_arch);
      if (platform_sp)
        platform_list.SetSelectedPlatform(platform_sp);
    }
  }

  if (!platform_arch.IsValid())
    platform_arch = arch;

  return TargetList::CreateTargetInternal(debugger, user_exe_path,
                                          platform_arch, load_dependent_files,
                                          platform_sp, target_sp);
}

Status TargetList::CreateTargetInternal(Debugger &debugger,
                                        llvm::StringRef user_exe_path,
                                        const ArchSpec &specified_arch,
                                        LoadDependentFiles load_dependent_files,
                                        lldb::PlatformSP &platform_sp,
                                        lldb::TargetSP &target_sp) {
  LLDB_SCOPED_TIMERF("TargetList::CreateTarget (file = '%s', arch = '%s')",
                     user_exe_path.str().c_str(),
                     specified_arch.GetArchitectureName());
  Status error;
  const bool is_dummy_target = false;

  ArchSpec arch(specified_arch);

  if (arch.IsValid()) {
    if (!platform_sp || !platform_sp->IsCompatibleArchitecture(
                            arch, {}, ArchSpec::CompatibleMatch, nullptr))
      platform_sp =
          debugger.GetPlatformList().GetOrCreate(specified_arch, {}, &arch);
  }

  if (!platform_sp)
    platform_sp = debugger.GetPlatformList().GetSelectedPlatform();

  if (!arch.IsValid())
    arch = specified_arch;

  FileSpec file(user_exe_path);
  if (!FileSystem::Instance().Exists(file) && user_exe_path.starts_with("~")) {
    // we want to expand the tilde but we don't want to resolve any symbolic
    // links so we can't use the FileSpec constructor's resolve flag
    llvm::SmallString<64> unglobbed_path;
    StandardTildeExpressionResolver Resolver;
    Resolver.ResolveFullPath(user_exe_path, unglobbed_path);

    if (unglobbed_path.empty())
      file = FileSpec(user_exe_path);
    else
      file = FileSpec(unglobbed_path.c_str());
  }

  bool user_exe_path_is_bundle = false;
  char resolved_bundle_exe_path[PATH_MAX];
  resolved_bundle_exe_path[0] = '\0';
  if (file) {
    if (FileSystem::Instance().IsDirectory(file))
      user_exe_path_is_bundle = true;

    if (file.IsRelative() && !user_exe_path.empty()) {
      llvm::SmallString<64> cwd;
      if (! llvm::sys::fs::current_path(cwd)) {
        FileSpec cwd_file(cwd.c_str());
        cwd_file.AppendPathComponent(file);
        if (FileSystem::Instance().Exists(cwd_file))
          file = cwd_file;
      }
    }

    ModuleSP exe_module_sp;
    if (platform_sp) {
      FileSpecList executable_search_paths(
          Target::GetDefaultExecutableSearchPaths());
      ModuleSpec module_spec(file, arch);
      error = platform_sp->ResolveExecutable(module_spec, exe_module_sp,
                                             executable_search_paths.GetSize()
                                                 ? &executable_search_paths
                                                 : nullptr);
    }

    if (error.Success() && exe_module_sp) {
      if (exe_module_sp->GetObjectFile() == nullptr) {
        if (arch.IsValid()) {
          error.SetErrorStringWithFormat(
              "\"%s\" doesn't contain architecture %s", file.GetPath().c_str(),
              arch.GetArchitectureName());
        } else {
          error.SetErrorStringWithFormat("unsupported file type \"%s\"",
                                         file.GetPath().c_str());
        }
        return error;
      }
      target_sp.reset(new Target(debugger, arch, platform_sp, is_dummy_target));
      debugger.GetTargetList().RegisterInProcessTarget(target_sp);
      target_sp->SetExecutableModule(exe_module_sp, load_dependent_files);
      if (user_exe_path_is_bundle)
        exe_module_sp->GetFileSpec().GetPath(resolved_bundle_exe_path,
                                             sizeof(resolved_bundle_exe_path));
      if (target_sp->GetPreloadSymbols())
        exe_module_sp->PreloadSymbols();
    }
  } else {
    // No file was specified, just create an empty target with any arch if a
    // valid arch was specified
    target_sp.reset(new Target(debugger, arch, platform_sp, is_dummy_target));
    debugger.GetTargetList().RegisterInProcessTarget(target_sp);
  }

  if (!target_sp)
    return error;

  // Set argv0 with what the user typed, unless the user specified a
  // directory. If the user specified a directory, then it is probably a
  // bundle that was resolved and we need to use the resolved bundle path
  if (!user_exe_path.empty()) {
    // Use exactly what the user typed as the first argument when we exec or
    // posix_spawn
    if (user_exe_path_is_bundle && resolved_bundle_exe_path[0]) {
      target_sp->SetArg0(resolved_bundle_exe_path);
    } else {
      // Use resolved path
      target_sp->SetArg0(file.GetPath().c_str());
    }
  }
  if (file.GetDirectory()) {
    FileSpec file_dir;
    file_dir.SetDirectory(file.GetDirectory());
    target_sp->AppendExecutableSearchPaths(file_dir);
  }

  // Now prime this from the dummy target:
  target_sp->PrimeFromDummyTarget(debugger.GetDummyTarget());

  return error;
}

bool TargetList::DeleteTarget(TargetSP &target_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto it = llvm::find(m_target_list, target_sp);
  if (it == m_target_list.end())
    return false;

  m_target_list.erase(it);
  return true;
}

TargetSP TargetList::FindTargetWithExecutableAndArchitecture(
    const FileSpec &exe_file_spec, const ArchSpec *exe_arch_ptr) const {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto it = std::find_if(m_target_list.begin(), m_target_list.end(),
      [&exe_file_spec, exe_arch_ptr](const TargetSP &item) {
        Module *exe_module = item->GetExecutableModulePointer();
        if (!exe_module ||
            !FileSpec::Match(exe_file_spec, exe_module->GetFileSpec()))
          return false;

        return !exe_arch_ptr ||
               exe_arch_ptr->IsCompatibleMatch(exe_module->GetArchitecture());
      });

  if (it != m_target_list.end())
    return *it;

  return TargetSP();
}

TargetSP TargetList::FindTargetWithProcessID(lldb::pid_t pid) const {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto it = std::find_if(m_target_list.begin(), m_target_list.end(),
      [pid](const TargetSP &item) {
        auto *process_ptr = item->GetProcessSP().get();
        return process_ptr && (process_ptr->GetID() == pid);
      });

  if (it != m_target_list.end())
    return *it;

  return TargetSP();
}

TargetSP TargetList::FindTargetWithProcess(Process *process) const {
  TargetSP target_sp;
  if (!process)
    return target_sp;

  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto it = std::find_if(m_target_list.begin(), m_target_list.end(),
      [process](const TargetSP &item) {
        return item->GetProcessSP().get() == process;
      });

  if (it != m_target_list.end())
    target_sp = *it;

  return target_sp;
}

TargetSP TargetList::GetTargetSP(Target *target) const {
  TargetSP target_sp;
  if (!target)
    return target_sp;

  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto it = std::find_if(m_target_list.begin(), m_target_list.end(),
      [target](const TargetSP &item) { return item.get() == target; });
  if (it != m_target_list.end())
    target_sp = *it;

  return target_sp;
}

uint32_t TargetList::SendAsyncInterrupt(lldb::pid_t pid) {
  uint32_t num_async_interrupts_sent = 0;

  if (pid != LLDB_INVALID_PROCESS_ID) {
    TargetSP target_sp(FindTargetWithProcessID(pid));
    if (target_sp) {
      Process *process = target_sp->GetProcessSP().get();
      if (process) {
        process->SendAsyncInterrupt();
        ++num_async_interrupts_sent;
      }
    }
  } else {
    // We don't have a valid pid to broadcast to, so broadcast to the target
    // list's async broadcaster...
    BroadcastEvent(Process::eBroadcastBitInterrupt, nullptr);
  }

  return num_async_interrupts_sent;
}

uint32_t TargetList::SignalIfRunning(lldb::pid_t pid, int signo) {
  uint32_t num_signals_sent = 0;
  Process *process = nullptr;
  if (pid == LLDB_INVALID_PROCESS_ID) {
    // Signal all processes with signal
    std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
    for (const auto &target_sp : m_target_list) {
      process = target_sp->GetProcessSP().get();
      if (process && process->IsAlive()) {
        ++num_signals_sent;
        process->Signal(signo);
      }
    }
  } else {
    // Signal a specific process with signal
    TargetSP target_sp(FindTargetWithProcessID(pid));
    if (target_sp) {
      process = target_sp->GetProcessSP().get();
      if (process && process->IsAlive()) {
        ++num_signals_sent;
        process->Signal(signo);
      }
    }
  }
  return num_signals_sent;
}

size_t TargetList::GetNumTargets() const {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  return m_target_list.size();
}

lldb::TargetSP TargetList::GetTargetAtIndex(uint32_t idx) const {
  TargetSP target_sp;
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  if (idx < m_target_list.size())
    target_sp = m_target_list[idx];
  return target_sp;
}

uint32_t TargetList::GetIndexOfTarget(lldb::TargetSP target_sp) const {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  auto it = llvm::find(m_target_list, target_sp);
  if (it != m_target_list.end())
    return std::distance(m_target_list.begin(), it);
  return UINT32_MAX;
}

void TargetList::AddTargetInternal(TargetSP target_sp, bool do_select) {
  lldbassert(!llvm::is_contained(m_target_list, target_sp) &&
             "target already exists it the list");
  UnregisterInProcessTarget(target_sp);
  m_target_list.push_back(std::move(target_sp));
  if (do_select)
    SetSelectedTargetInternal(m_target_list.size() - 1);
}

void TargetList::SetSelectedTargetInternal(uint32_t index) {
  lldbassert(!m_target_list.empty());
  m_selected_target_idx = index < m_target_list.size() ? index : 0;
}

void TargetList::SetSelectedTarget(uint32_t index) {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  SetSelectedTargetInternal(index);
}

void TargetList::SetSelectedTarget(const TargetSP &target_sp) {
  // Don't allow an invalid target shared pointer or a target that has been
  // destroyed to become the selected target.
  if (target_sp && target_sp->IsValid()) {
    std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
    auto it = llvm::find(m_target_list, target_sp);
    SetSelectedTargetInternal(std::distance(m_target_list.begin(), it));
  }
}

lldb::TargetSP TargetList::GetSelectedTarget() {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  if (m_selected_target_idx >= m_target_list.size())
    m_selected_target_idx = 0;
  return GetTargetAtIndex(m_selected_target_idx);
}

bool TargetList::AnyTargetContainsModule(Module &module) {
  std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
  for (const auto &target_sp : m_target_list) {
    if (target_sp->GetImages().FindModule(&module))
      return true;
  }
  for (const auto &target_sp: m_in_process_target_list) {
    if (target_sp->GetImages().FindModule(&module))
      return true;
  }
  return false;
}

  void TargetList::RegisterInProcessTarget(TargetSP target_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
    [[maybe_unused]] bool was_added;
    std::tie(std::ignore, was_added) =
        m_in_process_target_list.insert(target_sp);
    assert(was_added && "Target pointer was left in the in-process map");
  }
  
  void TargetList::UnregisterInProcessTarget(TargetSP target_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
    [[maybe_unused]] bool was_present =
        m_in_process_target_list.erase(target_sp);
    assert(was_present && "Target pointer being removed was not registered");
  }
  
  bool TargetList::IsTargetInProcess(TargetSP target_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_target_list_mutex);
    return m_in_process_target_list.count(target_sp) == 1; 
  }
