//===-- ScriptedProcess.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ScriptedProcess.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Queue.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/ScriptedMetadata.h"
#include "lldb/Utility/State.h"

#include <mutex>

LLDB_PLUGIN_DEFINE(ScriptedProcess)

using namespace lldb;
using namespace lldb_private;

llvm::StringRef ScriptedProcess::GetPluginDescriptionStatic() {
  return "Scripted Process plug-in.";
}

static constexpr lldb::ScriptLanguage g_supported_script_languages[] = {
    ScriptLanguage::eScriptLanguagePython,
};

bool ScriptedProcess::IsScriptLanguageSupported(lldb::ScriptLanguage language) {
  llvm::ArrayRef<lldb::ScriptLanguage> supported_languages =
      llvm::ArrayRef(g_supported_script_languages);

  return llvm::is_contained(supported_languages, language);
}

lldb::ProcessSP ScriptedProcess::CreateInstance(lldb::TargetSP target_sp,
                                                lldb::ListenerSP listener_sp,
                                                const FileSpec *file,
                                                bool can_connect) {
  if (!target_sp ||
      !IsScriptLanguageSupported(target_sp->GetDebugger().GetScriptLanguage()))
    return nullptr;

  ScriptedMetadata scripted_metadata(target_sp->GetProcessLaunchInfo());

  Status error;
  auto process_sp = std::shared_ptr<ScriptedProcess>(
      new ScriptedProcess(target_sp, listener_sp, scripted_metadata, error));

  if (error.Fail() || !process_sp || !process_sp->m_interface_up) {
    LLDB_LOGF(GetLog(LLDBLog::Process), "%s", error.AsCString());
    return nullptr;
  }

  return process_sp;
}

bool ScriptedProcess::CanDebug(lldb::TargetSP target_sp,
                               bool plugin_specified_by_name) {
  return true;
}

ScriptedProcess::ScriptedProcess(lldb::TargetSP target_sp,
                                 lldb::ListenerSP listener_sp,
                                 const ScriptedMetadata &scripted_metadata,
                                 Status &error)
    : Process(target_sp, listener_sp), m_scripted_metadata(scripted_metadata) {

  if (!target_sp) {
    error.SetErrorStringWithFormat("ScriptedProcess::%s () - ERROR: %s",
                                   __FUNCTION__, "Invalid target");
    return;
  }

  ScriptInterpreter *interpreter =
      target_sp->GetDebugger().GetScriptInterpreter();

  if (!interpreter) {
    error.SetErrorStringWithFormat("ScriptedProcess::%s () - ERROR: %s",
                                   __FUNCTION__,
                                   "Debugger has no Script Interpreter");
    return;
  }

  // Create process instance interface
  m_interface_up = interpreter->CreateScriptedProcessInterface();
  if (!m_interface_up) {
    error.SetErrorStringWithFormat(
        "ScriptedProcess::%s () - ERROR: %s", __FUNCTION__,
        "Script interpreter couldn't create Scripted Process Interface");
    return;
  }

  ExecutionContext exe_ctx(target_sp, /*get_process=*/false);

  // Create process script object
  auto obj_or_err = GetInterface().CreatePluginObject(
      m_scripted_metadata.GetClassName(), exe_ctx,
      m_scripted_metadata.GetArgsSP());

  if (!obj_or_err) {
    llvm::consumeError(obj_or_err.takeError());
    error.SetErrorString("Failed to create script object.");
    return;
  }

  StructuredData::GenericSP object_sp = *obj_or_err;

  if (!object_sp || !object_sp->IsValid()) {
    error.SetErrorStringWithFormat("ScriptedProcess::%s () - ERROR: %s",
                                   __FUNCTION__,
                                   "Failed to create valid script object");
    return;
  }
}

ScriptedProcess::~ScriptedProcess() {
  Clear();
  // If the interface is not valid, we can't call Finalize(). When that happens
  // it means that the Scripted Process instanciation failed and the
  // CreateProcess function returns a nullptr, so no one besides this class
  // should have access to that bogus process object.
  if (!m_interface_up)
    return;
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize(true /* destructing */);
}

void ScriptedProcess::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance);
  });
}

void ScriptedProcess::Terminate() {
  PluginManager::UnregisterPlugin(ScriptedProcess::CreateInstance);
}

Status ScriptedProcess::DoLoadCore() {
  ProcessLaunchInfo launch_info = GetTarget().GetProcessLaunchInfo();

  return DoLaunch(nullptr, launch_info);
}

Status ScriptedProcess::DoLaunch(Module *exe_module,
                                 ProcessLaunchInfo &launch_info) {
  LLDB_LOGF(GetLog(LLDBLog::Process), "ScriptedProcess::%s launching process", __FUNCTION__);
  
  /* MARK: This doesn't reflect how lldb actually launches a process.
           In reality, it attaches to debugserver, then resume the process.
           That's not true in all cases.  If debugserver is remote, lldb
           asks debugserver to launch the process for it. */
  Status error = GetInterface().Launch();
  SetPrivateState(eStateStopped);
  return error;
}

void ScriptedProcess::DidLaunch() { m_pid = GetInterface().GetProcessID(); }

void ScriptedProcess::DidResume() {
  // Update the PID again, in case the user provided a placeholder pid at launch
  m_pid = GetInterface().GetProcessID();
}

Status ScriptedProcess::DoResume() {
  LLDB_LOGF(GetLog(LLDBLog::Process), "ScriptedProcess::%s resuming process", __FUNCTION__);

  return GetInterface().Resume();
}

Status ScriptedProcess::DoAttach(const ProcessAttachInfo &attach_info) {
  Status error = GetInterface().Attach(attach_info);
  SetPrivateState(eStateRunning);
  SetPrivateState(eStateStopped);
  if (error.Fail())
    return error;
  // NOTE: We need to set the PID before finishing to attach otherwise we will
  // hit an assert when calling the attach completion handler.
  DidLaunch();

  return {};
}

Status
ScriptedProcess::DoAttachToProcessWithID(lldb::pid_t pid,
                                         const ProcessAttachInfo &attach_info) {
  return DoAttach(attach_info);
}

Status ScriptedProcess::DoAttachToProcessWithName(
    const char *process_name, const ProcessAttachInfo &attach_info) {
  return DoAttach(attach_info);
}

void ScriptedProcess::DidAttach(ArchSpec &process_arch) {
  process_arch = GetArchitecture();
}

Status ScriptedProcess::DoDestroy() { return Status(); }

bool ScriptedProcess::IsAlive() { return GetInterface().IsAlive(); }

size_t ScriptedProcess::DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                     Status &error) {
  lldb::DataExtractorSP data_extractor_sp =
      GetInterface().ReadMemoryAtAddress(addr, size, error);

  if (!data_extractor_sp || !data_extractor_sp->GetByteSize() || error.Fail())
    return 0;

  offset_t bytes_copied = data_extractor_sp->CopyByteOrderedData(
      0, data_extractor_sp->GetByteSize(), buf, size, GetByteOrder());

  if (!bytes_copied || bytes_copied == LLDB_INVALID_OFFSET)
    return ScriptedInterface::ErrorWithMessage<size_t>(
        LLVM_PRETTY_FUNCTION, "Failed to copy read memory to buffer.", error);

  // FIXME: We should use the diagnostic system to report a warning if the
  // `bytes_copied` is different from `size`.

  return bytes_copied;
}

size_t ScriptedProcess::DoWriteMemory(lldb::addr_t vm_addr, const void *buf,
                                      size_t size, Status &error) {
  lldb::DataExtractorSP data_extractor_sp = std::make_shared<DataExtractor>(
      buf, size, GetByteOrder(), GetAddressByteSize());

  if (!data_extractor_sp || !data_extractor_sp->GetByteSize())
    return 0;

  lldb::offset_t bytes_written =
      GetInterface().WriteMemoryAtAddress(vm_addr, data_extractor_sp, error);

  if (!bytes_written || bytes_written == LLDB_INVALID_OFFSET)
    return ScriptedInterface::ErrorWithMessage<size_t>(
        LLVM_PRETTY_FUNCTION, "Failed to copy write buffer to memory.", error);

  // FIXME: We should use the diagnostic system to report a warning if the
  // `bytes_written` is different from `size`.

  return bytes_written;
}

Status ScriptedProcess::EnableBreakpointSite(BreakpointSite *bp_site) {
  assert(bp_site != nullptr);

  if (bp_site->IsEnabled()) {
    return {};
  }

  if (bp_site->HardwareRequired()) {
    return Status("Scripted Processes don't support hardware breakpoints");
  }

  Status error;
  GetInterface().CreateBreakpoint(bp_site->GetLoadAddress(), error);

  return error;
}

ArchSpec ScriptedProcess::GetArchitecture() {
  return GetTarget().GetArchitecture();
}

Status ScriptedProcess::DoGetMemoryRegionInfo(lldb::addr_t load_addr,
                                              MemoryRegionInfo &region) {
  Status error;
  if (auto region_or_err =
          GetInterface().GetMemoryRegionContainingAddress(load_addr, error))
    region = *region_or_err;

  return error;
}

Status ScriptedProcess::GetMemoryRegions(MemoryRegionInfos &region_list) {
  Status error;
  lldb::addr_t address = 0;

  while (auto region_or_err =
             GetInterface().GetMemoryRegionContainingAddress(address, error)) {
    if (error.Fail())
      break;

    MemoryRegionInfo &mem_region = *region_or_err;
    auto range = mem_region.GetRange();
    address += range.GetRangeBase() + range.GetByteSize();
    region_list.push_back(mem_region);
  }

  return error;
}

void ScriptedProcess::Clear() { Process::m_thread_list.Clear(); }

bool ScriptedProcess::DoUpdateThreadList(ThreadList &old_thread_list,
                                         ThreadList &new_thread_list) {
  // TODO: Implement
  // This is supposed to get the current set of threads, if any of them are in
  // old_thread_list then they get copied to new_thread_list, and then any
  // actually new threads will get added to new_thread_list.
  m_thread_plans.ClearThreadCache();

  Status error;
  StructuredData::DictionarySP thread_info_sp = GetInterface().GetThreadsInfo();

  if (!thread_info_sp)
    return ScriptedInterface::ErrorWithMessage<bool>(
        LLVM_PRETTY_FUNCTION,
        "Couldn't fetch thread list from Scripted Process.", error);

  // Because `StructuredData::Dictionary` uses a `std::map<ConstString,
  // ObjectSP>` for storage, each item is sorted based on the key alphabetical
  // order. Since `GetThreadsInfo` provides thread indices as the key element,
  // thread info comes ordered alphabetically, instead of numerically, so we
  // need to sort the thread indices before creating thread.

  StructuredData::ArraySP keys = thread_info_sp->GetKeys();

  std::map<size_t, StructuredData::ObjectSP> sorted_threads;
  auto sort_keys = [&sorted_threads,
                    &thread_info_sp](StructuredData::Object *item) -> bool {
    if (!item)
      return false;

    llvm::StringRef key = item->GetStringValue();
    size_t idx = 0;

    // Make sure the provided index is actually an integer
    if (!llvm::to_integer(key, idx))
      return false;

    sorted_threads[idx] = thread_info_sp->GetValueForKey(key);
    return true;
  };

  size_t thread_count = thread_info_sp->GetSize();

  if (!keys->ForEach(sort_keys) || sorted_threads.size() != thread_count)
    // Might be worth showing the unsorted thread list instead of return early.
    return ScriptedInterface::ErrorWithMessage<bool>(
        LLVM_PRETTY_FUNCTION, "Couldn't sort thread list.", error);

  auto create_scripted_thread =
      [this, &error, &new_thread_list](
          const std::pair<size_t, StructuredData::ObjectSP> pair) -> bool {
    size_t idx = pair.first;
    StructuredData::ObjectSP object_sp = pair.second;

    if (!object_sp)
      return ScriptedInterface::ErrorWithMessage<bool>(
          LLVM_PRETTY_FUNCTION, "Invalid thread info object", error);

    auto thread_or_error =
        ScriptedThread::Create(*this, object_sp->GetAsGeneric());

    if (!thread_or_error)
      return ScriptedInterface::ErrorWithMessage<bool>(
          LLVM_PRETTY_FUNCTION, toString(thread_or_error.takeError()), error);

    ThreadSP thread_sp = thread_or_error.get();
    lldbassert(thread_sp && "Couldn't initialize scripted thread.");

    RegisterContextSP reg_ctx_sp = thread_sp->GetRegisterContext();
    if (!reg_ctx_sp)
      return ScriptedInterface::ErrorWithMessage<bool>(
          LLVM_PRETTY_FUNCTION,
          llvm::Twine("Invalid Register Context for thread " + llvm::Twine(idx))
              .str(),
          error);

    new_thread_list.AddThread(thread_sp);

    return true;
  };

  llvm::for_each(sorted_threads, create_scripted_thread);

  return new_thread_list.GetSize(false) > 0;
}

void ScriptedProcess::RefreshStateAfterStop() {
  // Let all threads recover from stopping and do any clean up based on the
  // previous thread state (if any).
  m_thread_list.RefreshStateAfterStop();
}

bool ScriptedProcess::GetProcessInfo(ProcessInstanceInfo &info) {
  info.Clear();
  info.SetProcessID(GetID());
  info.SetArchitecture(GetArchitecture());
  lldb::ModuleSP module_sp = GetTarget().GetExecutableModule();
  if (module_sp) {
    const bool add_exe_file_as_first_arg = false;
    info.SetExecutableFile(GetTarget().GetExecutableModule()->GetFileSpec(),
                           add_exe_file_as_first_arg);
  }
  return true;
}

lldb_private::StructuredData::ObjectSP
ScriptedProcess::GetLoadedDynamicLibrariesInfos() {
  Status error;
  auto error_with_message = [&error](llvm::StringRef message) {
    return ScriptedInterface::ErrorWithMessage<bool>(LLVM_PRETTY_FUNCTION,
                                                     message.data(), error);
  };

  StructuredData::ArraySP loaded_images_sp = GetInterface().GetLoadedImages();

  if (!loaded_images_sp || !loaded_images_sp->GetSize())
    return ScriptedInterface::ErrorWithMessage<StructuredData::ObjectSP>(
        LLVM_PRETTY_FUNCTION, "No loaded images.", error);

  ModuleList module_list;
  Target &target = GetTarget();

  auto reload_image = [&target, &module_list, &error_with_message](
                          StructuredData::Object *obj) -> bool {
    StructuredData::Dictionary *dict = obj->GetAsDictionary();

    if (!dict)
      return error_with_message("Couldn't cast image object into dictionary.");

    ModuleSpec module_spec;
    llvm::StringRef value;

    bool has_path = dict->HasKey("path");
    bool has_uuid = dict->HasKey("uuid");
    if (!has_path && !has_uuid)
      return error_with_message("Dictionary should have key 'path' or 'uuid'");
    if (!dict->HasKey("load_addr"))
      return error_with_message("Dictionary is missing key 'load_addr'");

    if (has_path) {
      dict->GetValueForKeyAsString("path", value);
      module_spec.GetFileSpec().SetPath(value);
    }

    if (has_uuid) {
      dict->GetValueForKeyAsString("uuid", value);
      module_spec.GetUUID().SetFromStringRef(value);
    }
    module_spec.GetArchitecture() = target.GetArchitecture();

    ModuleSP module_sp =
        target.GetOrCreateModule(module_spec, true /* notify */);

    if (!module_sp)
      return error_with_message("Couldn't create or get module.");

    lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
    lldb::offset_t slide = LLDB_INVALID_OFFSET;
    dict->GetValueForKeyAsInteger("load_addr", load_addr);
    dict->GetValueForKeyAsInteger("slide", slide);
    if (load_addr == LLDB_INVALID_ADDRESS)
      return error_with_message(
          "Couldn't get valid load address or slide offset.");

    if (slide != LLDB_INVALID_OFFSET)
      load_addr += slide;

    bool changed = false;
    module_sp->SetLoadAddress(target, load_addr, false /*=value_is_offset*/,
                              changed);

    if (!changed && !module_sp->GetObjectFile())
      return error_with_message("Couldn't set the load address for module.");

    dict->GetValueForKeyAsString("path", value);
    FileSpec objfile(value);
    module_sp->SetFileSpecAndObjectName(objfile, objfile.GetFilename());

    return module_list.AppendIfNeeded(module_sp);
  };

  if (!loaded_images_sp->ForEach(reload_image))
    return ScriptedInterface::ErrorWithMessage<StructuredData::ObjectSP>(
        LLVM_PRETTY_FUNCTION, "Couldn't reload all images.", error);

  target.ModulesDidLoad(module_list);

  return loaded_images_sp;
}

lldb_private::StructuredData::DictionarySP ScriptedProcess::GetMetadata() {
  StructuredData::DictionarySP metadata_sp = GetInterface().GetMetadata();

  Status error;
  if (!metadata_sp || !metadata_sp->GetSize())
    return ScriptedInterface::ErrorWithMessage<StructuredData::DictionarySP>(
        LLVM_PRETTY_FUNCTION, "No metadata.", error);

  return metadata_sp;
}

void ScriptedProcess::UpdateQueueListIfNeeded() {
  CheckScriptedInterface();
  for (ThreadSP thread_sp : Threads()) {
    if (const char *queue_name = thread_sp->GetQueueName()) {
      QueueSP queue_sp = std::make_shared<Queue>(
          m_process->shared_from_this(), thread_sp->GetQueueID(), queue_name);
      m_queue_list.AddQueue(queue_sp);
    }
  }
}

ScriptedProcessInterface &ScriptedProcess::GetInterface() const {
  CheckScriptedInterface();
  return *m_interface_up;
}

void *ScriptedProcess::GetImplementation() {
  StructuredData::GenericSP object_instance_sp =
      GetInterface().GetScriptObjectInstance();
  if (object_instance_sp &&
      object_instance_sp->GetType() == eStructuredDataTypeGeneric)
    return object_instance_sp->GetAsGeneric()->GetValue();
  return nullptr;
}
