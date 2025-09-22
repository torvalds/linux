//===-- SBDebugger.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBDebugger.h"
#include "SystemInitializerFull.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/LLDBLog.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandInterpreterRunOptions.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBSourceManager.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBTrace.h"
#include "lldb/API/SBTypeCategory.h"
#include "lldb/API/SBTypeFilter.h"
#include "lldb/API/SBTypeFormat.h"
#include "lldb/API/SBTypeNameSpecifier.h"
#include "lldb/API/SBTypeSummary.h"
#include "lldb/API/SBTypeSynthetic.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/DebuggerEvents.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Progress.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Host/XML.h"
#include "lldb/Initialization/SystemLifetimeManager.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupPlatform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Diagnostics.h"
#include "lldb/Utility/State.h"
#include "lldb/Version/Version.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"

using namespace lldb;
using namespace lldb_private;

static llvm::ManagedStatic<SystemLifetimeManager> g_debugger_lifetime;

SBError SBInputReader::Initialize(
    lldb::SBDebugger &sb_debugger,
    unsigned long (*callback)(void *, lldb::SBInputReader *,
                              lldb::InputReaderAction, char const *,
                              unsigned long),
    void *a, lldb::InputReaderGranularity b, char const *c, char const *d,
    bool e) {
  LLDB_INSTRUMENT_VA(this, sb_debugger, callback, a, b, c, d, e);

  return SBError();
}

void SBInputReader::SetIsDone(bool b) { LLDB_INSTRUMENT_VA(this, b); }

bool SBInputReader::IsActive() const {
  LLDB_INSTRUMENT_VA(this);

  return false;
}

SBDebugger::SBDebugger() { LLDB_INSTRUMENT_VA(this); }

SBDebugger::SBDebugger(const lldb::DebuggerSP &debugger_sp)
    : m_opaque_sp(debugger_sp) {
  LLDB_INSTRUMENT_VA(this, debugger_sp);
}

SBDebugger::SBDebugger(const SBDebugger &rhs) : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBDebugger::~SBDebugger() = default;

SBDebugger &SBDebugger::operator=(const SBDebugger &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

const char *SBDebugger::GetBroadcasterClass() {
  LLDB_INSTRUMENT();

  return ConstString(Debugger::GetStaticBroadcasterClass()).AsCString();
}

const char *SBDebugger::GetProgressFromEvent(const lldb::SBEvent &event,
                                             uint64_t &progress_id,
                                             uint64_t &completed,
                                             uint64_t &total,
                                             bool &is_debugger_specific) {
  LLDB_INSTRUMENT_VA(event);

  const ProgressEventData *progress_data =
      ProgressEventData::GetEventDataFromEvent(event.get());
  if (progress_data == nullptr)
    return nullptr;
  progress_id = progress_data->GetID();
  completed = progress_data->GetCompleted();
  total = progress_data->GetTotal();
  is_debugger_specific = progress_data->IsDebuggerSpecific();
  ConstString message(progress_data->GetMessage());
  return message.AsCString();
}

lldb::SBStructuredData
SBDebugger::GetProgressDataFromEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  StructuredData::DictionarySP dictionary_sp =
      ProgressEventData::GetAsStructuredData(event.get());

  if (!dictionary_sp)
    return {};

  SBStructuredData data;
  data.m_impl_up->SetObjectSP(std::move(dictionary_sp));
  return data;
}

lldb::SBStructuredData
SBDebugger::GetDiagnosticFromEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  StructuredData::DictionarySP dictionary_sp =
      DiagnosticEventData::GetAsStructuredData(event.get());

  if (!dictionary_sp)
    return {};

  SBStructuredData data;
  data.m_impl_up->SetObjectSP(std::move(dictionary_sp));
  return data;
}

SBBroadcaster SBDebugger::GetBroadcaster() {
  LLDB_INSTRUMENT_VA(this);
  SBBroadcaster broadcaster(&m_opaque_sp->GetBroadcaster(), false);
  return broadcaster;
}

void SBDebugger::Initialize() {
  LLDB_INSTRUMENT();
  SBError ignored = SBDebugger::InitializeWithErrorHandling();
}

lldb::SBError SBDebugger::InitializeWithErrorHandling() {
  LLDB_INSTRUMENT();

  auto LoadPlugin = [](const lldb::DebuggerSP &debugger_sp,
                       const FileSpec &spec,
                       Status &error) -> llvm::sys::DynamicLibrary {
    llvm::sys::DynamicLibrary dynlib =
        llvm::sys::DynamicLibrary::getPermanentLibrary(spec.GetPath().c_str());
    if (dynlib.isValid()) {
      typedef bool (*LLDBCommandPluginInit)(lldb::SBDebugger & debugger);

      lldb::SBDebugger debugger_sb(debugger_sp);
      // This calls the bool lldb::PluginInitialize(lldb::SBDebugger debugger)
      // function.
      // TODO: mangle this differently for your system - on OSX, the first
      // underscore needs to be removed and the second one stays
      LLDBCommandPluginInit init_func =
          (LLDBCommandPluginInit)(uintptr_t)dynlib.getAddressOfSymbol(
              "_ZN4lldb16PluginInitializeENS_10SBDebuggerE");
      if (init_func) {
        if (init_func(debugger_sb))
          return dynlib;
        else
          error.SetErrorString("plug-in refused to load "
                               "(lldb::PluginInitialize(lldb::SBDebugger) "
                               "returned false)");
      } else {
        error.SetErrorString("plug-in is missing the required initialization: "
                             "lldb::PluginInitialize(lldb::SBDebugger)");
      }
    } else {
      if (FileSystem::Instance().Exists(spec))
        error.SetErrorString("this file does not represent a loadable dylib");
      else
        error.SetErrorString("no such file");
    }
    return llvm::sys::DynamicLibrary();
  };

  SBError error;
  if (auto e = g_debugger_lifetime->Initialize(
          std::make_unique<SystemInitializerFull>(), LoadPlugin)) {
    error.SetError(Status(std::move(e)));
  }
  return error;
}

void SBDebugger::PrintStackTraceOnError() {
  LLDB_INSTRUMENT();

  llvm::EnablePrettyStackTrace();
  static std::string executable =
      llvm::sys::fs::getMainExecutable(nullptr, nullptr);
  llvm::sys::PrintStackTraceOnErrorSignal(executable);
}

static void DumpDiagnostics(void *cookie) {
  Diagnostics::Instance().Dump(llvm::errs());
}

void SBDebugger::PrintDiagnosticsOnError() {
  LLDB_INSTRUMENT();

  llvm::sys::AddSignalHandler(&DumpDiagnostics, nullptr);
}

void SBDebugger::Terminate() {
  LLDB_INSTRUMENT();

  g_debugger_lifetime->Terminate();
}

void SBDebugger::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    m_opaque_sp->ClearIOHandlers();

  m_opaque_sp.reset();
}

SBDebugger SBDebugger::Create() {
  LLDB_INSTRUMENT();

  return SBDebugger::Create(false, nullptr, nullptr);
}

SBDebugger SBDebugger::Create(bool source_init_files) {
  LLDB_INSTRUMENT_VA(source_init_files);

  return SBDebugger::Create(source_init_files, nullptr, nullptr);
}

SBDebugger SBDebugger::Create(bool source_init_files,
                              lldb::LogOutputCallback callback, void *baton)

{
  LLDB_INSTRUMENT_VA(source_init_files, callback, baton);

  SBDebugger debugger;

  // Currently we have issues if this function is called simultaneously on two
  // different threads. The issues mainly revolve around the fact that the
  // lldb_private::FormatManager uses global collections and having two threads
  // parsing the .lldbinit files can cause mayhem. So to get around this for
  // now we need to use a mutex to prevent bad things from happening.
  static std::recursive_mutex g_mutex;
  std::lock_guard<std::recursive_mutex> guard(g_mutex);

  debugger.reset(Debugger::CreateInstance(callback, baton));

  SBCommandInterpreter interp = debugger.GetCommandInterpreter();
  if (source_init_files) {
    interp.get()->SkipLLDBInitFiles(false);
    interp.get()->SkipAppInitFiles(false);
    SBCommandReturnObject result;
    interp.SourceInitFileInGlobalDirectory(result);
    interp.SourceInitFileInHomeDirectory(result, false);
  } else {
    interp.get()->SkipLLDBInitFiles(true);
    interp.get()->SkipAppInitFiles(true);
  }
  return debugger;
}

void SBDebugger::Destroy(SBDebugger &debugger) {
  LLDB_INSTRUMENT_VA(debugger);

  Debugger::Destroy(debugger.m_opaque_sp);

  if (debugger.m_opaque_sp.get() != nullptr)
    debugger.m_opaque_sp.reset();
}

void SBDebugger::MemoryPressureDetected() {
  LLDB_INSTRUMENT();

  // Since this function can be call asynchronously, we allow it to be non-
  // mandatory. We have seen deadlocks with this function when called so we
  // need to safeguard against this until we can determine what is causing the
  // deadlocks.

  const bool mandatory = false;

  ModuleList::RemoveOrphanSharedModules(mandatory);
}

bool SBDebugger::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBDebugger::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

void SBDebugger::SetAsync(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  if (m_opaque_sp)
    m_opaque_sp->SetAsyncExecution(b);
}

bool SBDebugger::GetAsync() {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetAsyncExecution() : false);
}

void SBDebugger::SkipLLDBInitFiles(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  if (m_opaque_sp)
    m_opaque_sp->GetCommandInterpreter().SkipLLDBInitFiles(b);
}

void SBDebugger::SkipAppInitFiles(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  if (m_opaque_sp)
    m_opaque_sp->GetCommandInterpreter().SkipAppInitFiles(b);
}

void SBDebugger::SetInputFileHandle(FILE *fh, bool transfer_ownership) {
  LLDB_INSTRUMENT_VA(this, fh, transfer_ownership);
  if (m_opaque_sp)
    m_opaque_sp->SetInputFile(
        (FileSP)std::make_shared<NativeFile>(fh, transfer_ownership));
}

SBError SBDebugger::SetInputString(const char *data) {
  LLDB_INSTRUMENT_VA(this, data);
  SBError sb_error;
  if (data == nullptr) {
    sb_error.SetErrorString("String data is null");
    return sb_error;
  }

  size_t size = strlen(data);
  if (size == 0) {
    sb_error.SetErrorString("String data is empty");
    return sb_error;
  }

  if (!m_opaque_sp) {
    sb_error.SetErrorString("invalid debugger");
    return sb_error;
  }

  sb_error.SetError(m_opaque_sp->SetInputString(data));
  return sb_error;
}

// Shouldn't really be settable after initialization as this could cause lots
// of problems; don't want users trying to switch modes in the middle of a
// debugging session.
SBError SBDebugger::SetInputFile(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file);

  SBError error;
  if (!m_opaque_sp) {
    error.ref().SetErrorString("invalid debugger");
    return error;
  }
  if (!file) {
    error.ref().SetErrorString("invalid file");
    return error;
  }
  m_opaque_sp->SetInputFile(file.m_opaque_sp);
  return error;
}

SBError SBDebugger::SetInputFile(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);
  return SetInputFile(SBFile(file_sp));
}

SBError SBDebugger::SetOutputFile(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);
  return SetOutputFile(SBFile(file_sp));
}

void SBDebugger::SetOutputFileHandle(FILE *fh, bool transfer_ownership) {
  LLDB_INSTRUMENT_VA(this, fh, transfer_ownership);
  SetOutputFile((FileSP)std::make_shared<NativeFile>(fh, transfer_ownership));
}

SBError SBDebugger::SetOutputFile(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file);
  SBError error;
  if (!m_opaque_sp) {
    error.ref().SetErrorString("invalid debugger");
    return error;
  }
  if (!file) {
    error.ref().SetErrorString("invalid file");
    return error;
  }
  m_opaque_sp->SetOutputFile(file.m_opaque_sp);
  return error;
}

void SBDebugger::SetErrorFileHandle(FILE *fh, bool transfer_ownership) {
  LLDB_INSTRUMENT_VA(this, fh, transfer_ownership);
  SetErrorFile((FileSP)std::make_shared<NativeFile>(fh, transfer_ownership));
}

SBError SBDebugger::SetErrorFile(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);
  return SetErrorFile(SBFile(file_sp));
}

SBError SBDebugger::SetErrorFile(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file);
  SBError error;
  if (!m_opaque_sp) {
    error.ref().SetErrorString("invalid debugger");
    return error;
  }
  if (!file) {
    error.ref().SetErrorString("invalid file");
    return error;
  }
  m_opaque_sp->SetErrorFile(file.m_opaque_sp);
  return error;
}

lldb::SBStructuredData SBDebugger::GetSetting(const char *setting) {
  LLDB_INSTRUMENT_VA(this, setting);

  SBStructuredData data;
  if (!m_opaque_sp)
    return data;

  StreamString json_strm;
  ExecutionContext exe_ctx(
      m_opaque_sp->GetCommandInterpreter().GetExecutionContext());
  if (setting && strlen(setting) > 0)
    m_opaque_sp->DumpPropertyValue(&exe_ctx, json_strm, setting,
                                   /*dump_mask*/ 0,
                                   /*is_json*/ true);
  else
    m_opaque_sp->DumpAllPropertyValues(&exe_ctx, json_strm, /*dump_mask*/ 0,
                                       /*is_json*/ true);

  data.m_impl_up->SetObjectSP(StructuredData::ParseJSON(json_strm.GetString()));
  return data;
}

FILE *SBDebugger::GetInputFileHandle() {
  LLDB_INSTRUMENT_VA(this);
  if (m_opaque_sp) {
    File &file_sp = m_opaque_sp->GetInputFile();
    return file_sp.GetStream();
  }
  return nullptr;
}

SBFile SBDebugger::GetInputFile() {
  LLDB_INSTRUMENT_VA(this);
  if (m_opaque_sp) {
    return SBFile(m_opaque_sp->GetInputFileSP());
  }
  return SBFile();
}

FILE *SBDebugger::GetOutputFileHandle() {
  LLDB_INSTRUMENT_VA(this);
  if (m_opaque_sp) {
    StreamFile &stream_file = m_opaque_sp->GetOutputStream();
    return stream_file.GetFile().GetStream();
  }
  return nullptr;
}

SBFile SBDebugger::GetOutputFile() {
  LLDB_INSTRUMENT_VA(this);
  if (m_opaque_sp) {
    SBFile file(m_opaque_sp->GetOutputStream().GetFileSP());
    return file;
  }
  return SBFile();
}

FILE *SBDebugger::GetErrorFileHandle() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp) {
    StreamFile &stream_file = m_opaque_sp->GetErrorStream();
    return stream_file.GetFile().GetStream();
  }
  return nullptr;
}

SBFile SBDebugger::GetErrorFile() {
  LLDB_INSTRUMENT_VA(this);
  SBFile file;
  if (m_opaque_sp) {
    SBFile file(m_opaque_sp->GetErrorStream().GetFileSP());
    return file;
  }
  return SBFile();
}

void SBDebugger::SaveInputTerminalState() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    m_opaque_sp->SaveInputTerminalState();
}

void SBDebugger::RestoreInputTerminalState() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    m_opaque_sp->RestoreInputTerminalState();
}
SBCommandInterpreter SBDebugger::GetCommandInterpreter() {
  LLDB_INSTRUMENT_VA(this);

  SBCommandInterpreter sb_interpreter;
  if (m_opaque_sp)
    sb_interpreter.reset(&m_opaque_sp->GetCommandInterpreter());

  return sb_interpreter;
}

void SBDebugger::HandleCommand(const char *command) {
  LLDB_INSTRUMENT_VA(this, command);

  if (m_opaque_sp) {
    TargetSP target_sp(m_opaque_sp->GetSelectedTarget());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp)
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());

    SBCommandInterpreter sb_interpreter(GetCommandInterpreter());
    SBCommandReturnObject result;

    sb_interpreter.HandleCommand(command, result, false);

    result.PutError(m_opaque_sp->GetErrorStream().GetFileSP());
    result.PutOutput(m_opaque_sp->GetOutputStream().GetFileSP());

    if (!m_opaque_sp->GetAsyncExecution()) {
      SBProcess process(GetCommandInterpreter().GetProcess());
      ProcessSP process_sp(process.GetSP());
      if (process_sp) {
        EventSP event_sp;
        ListenerSP lldb_listener_sp = m_opaque_sp->GetListener();
        while (lldb_listener_sp->GetEventForBroadcaster(
            process_sp.get(), event_sp, std::chrono::seconds(0))) {
          SBEvent event(event_sp);
          HandleProcessEvent(process, event, GetOutputFile(), GetErrorFile());
        }
      }
    }
  }
}

SBListener SBDebugger::GetListener() {
  LLDB_INSTRUMENT_VA(this);

  SBListener sb_listener;
  if (m_opaque_sp)
    sb_listener.reset(m_opaque_sp->GetListener());

  return sb_listener;
}

void SBDebugger::HandleProcessEvent(const SBProcess &process,
                                    const SBEvent &event, SBFile out,
                                    SBFile err) {
  LLDB_INSTRUMENT_VA(this, process, event, out, err);

  return HandleProcessEvent(process, event, out.m_opaque_sp, err.m_opaque_sp);
}

void SBDebugger::HandleProcessEvent(const SBProcess &process,
                                    const SBEvent &event, FILE *out,
                                    FILE *err) {
  LLDB_INSTRUMENT_VA(this, process, event, out, err);

  FileSP outfile = std::make_shared<NativeFile>(out, false);
  FileSP errfile = std::make_shared<NativeFile>(err, false);
  return HandleProcessEvent(process, event, outfile, errfile);
}

void SBDebugger::HandleProcessEvent(const SBProcess &process,
                                    const SBEvent &event, FileSP out_sp,
                                    FileSP err_sp) {

  LLDB_INSTRUMENT_VA(this, process, event, out_sp, err_sp);

  if (!process.IsValid())
    return;

  TargetSP target_sp(process.GetTarget().GetSP());
  if (!target_sp)
    return;

  const uint32_t event_type = event.GetType();
  char stdio_buffer[1024];
  size_t len;

  std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

  if (event_type &
      (Process::eBroadcastBitSTDOUT | Process::eBroadcastBitStateChanged)) {
    // Drain stdout when we stop just in case we have any bytes
    while ((len = process.GetSTDOUT(stdio_buffer, sizeof(stdio_buffer))) > 0)
      if (out_sp)
        out_sp->Write(stdio_buffer, len);
  }

  if (event_type &
      (Process::eBroadcastBitSTDERR | Process::eBroadcastBitStateChanged)) {
    // Drain stderr when we stop just in case we have any bytes
    while ((len = process.GetSTDERR(stdio_buffer, sizeof(stdio_buffer))) > 0)
      if (err_sp)
        err_sp->Write(stdio_buffer, len);
  }

  if (event_type & Process::eBroadcastBitStateChanged) {
    StateType event_state = SBProcess::GetStateFromEvent(event);

    if (event_state == eStateInvalid)
      return;

    bool is_stopped = StateIsStoppedState(event_state);
    if (!is_stopped)
      process.ReportEventState(event, out_sp);
  }
}

SBSourceManager SBDebugger::GetSourceManager() {
  LLDB_INSTRUMENT_VA(this);

  SBSourceManager sb_source_manager(*this);
  return sb_source_manager;
}

bool SBDebugger::GetDefaultArchitecture(char *arch_name, size_t arch_name_len) {
  LLDB_INSTRUMENT_VA(arch_name, arch_name_len);

  if (arch_name && arch_name_len) {
    ArchSpec default_arch = Target::GetDefaultArchitecture();

    if (default_arch.IsValid()) {
      const std::string &triple_str = default_arch.GetTriple().str();
      if (!triple_str.empty())
        ::snprintf(arch_name, arch_name_len, "%s", triple_str.c_str());
      else
        ::snprintf(arch_name, arch_name_len, "%s",
                   default_arch.GetArchitectureName());
      return true;
    }
  }
  if (arch_name && arch_name_len)
    arch_name[0] = '\0';
  return false;
}

bool SBDebugger::SetDefaultArchitecture(const char *arch_name) {
  LLDB_INSTRUMENT_VA(arch_name);

  if (arch_name) {
    ArchSpec arch(arch_name);
    if (arch.IsValid()) {
      Target::SetDefaultArchitecture(arch);
      return true;
    }
  }
  return false;
}

ScriptLanguage
SBDebugger::GetScriptingLanguage(const char *script_language_name) {
  LLDB_INSTRUMENT_VA(this, script_language_name);

  if (!script_language_name)
    return eScriptLanguageDefault;
  return OptionArgParser::ToScriptLanguage(
      llvm::StringRef(script_language_name), eScriptLanguageDefault, nullptr);
}

SBStructuredData
SBDebugger::GetScriptInterpreterInfo(lldb::ScriptLanguage language) {
  LLDB_INSTRUMENT_VA(this, language);
  SBStructuredData data;
  if (m_opaque_sp) {
    lldb_private::ScriptInterpreter *interp =
        m_opaque_sp->GetScriptInterpreter(language);
    if (interp) {
      data.m_impl_up->SetObjectSP(interp->GetInterpreterInfo());
    }
  }
  return data;
}

const char *SBDebugger::GetVersionString() {
  LLDB_INSTRUMENT();

  return lldb_private::GetVersion();
}

const char *SBDebugger::StateAsCString(StateType state) {
  LLDB_INSTRUMENT_VA(state);

  return lldb_private::StateAsCString(state);
}

static void AddBoolConfigEntry(StructuredData::Dictionary &dict,
                               llvm::StringRef name, bool value,
                               llvm::StringRef description) {
  auto entry_up = std::make_unique<StructuredData::Dictionary>();
  entry_up->AddBooleanItem("value", value);
  entry_up->AddStringItem("description", description);
  dict.AddItem(name, std::move(entry_up));
}

static void AddLLVMTargets(StructuredData::Dictionary &dict) {
  auto array_up = std::make_unique<StructuredData::Array>();
#define LLVM_TARGET(target)                                                    \
  array_up->AddItem(std::make_unique<StructuredData::String>(#target));
#include "llvm/Config/Targets.def"
  auto entry_up = std::make_unique<StructuredData::Dictionary>();
  entry_up->AddItem("value", std::move(array_up));
  entry_up->AddStringItem("description", "A list of configured LLVM targets.");
  dict.AddItem("targets", std::move(entry_up));
}

SBStructuredData SBDebugger::GetBuildConfiguration() {
  LLDB_INSTRUMENT();

  auto config_up = std::make_unique<StructuredData::Dictionary>();
  AddBoolConfigEntry(
      *config_up, "xml", XMLDocument::XMLEnabled(),
      "A boolean value that indicates if XML support is enabled in LLDB");
  AddBoolConfigEntry(
      *config_up, "curses", LLDB_ENABLE_CURSES,
      "A boolean value that indicates if curses support is enabled in LLDB");
  AddBoolConfigEntry(
      *config_up, "editline", LLDB_ENABLE_LIBEDIT,
      "A boolean value that indicates if editline support is enabled in LLDB");
  AddBoolConfigEntry(*config_up, "editline_wchar", LLDB_EDITLINE_USE_WCHAR,
                     "A boolean value that indicates if editline wide "
                     "characters support is enabled in LLDB");
  AddBoolConfigEntry(
      *config_up, "lzma", LLDB_ENABLE_LZMA,
      "A boolean value that indicates if lzma support is enabled in LLDB");
  AddBoolConfigEntry(
      *config_up, "python", LLDB_ENABLE_PYTHON,
      "A boolean value that indicates if python support is enabled in LLDB");
  AddBoolConfigEntry(
      *config_up, "lua", LLDB_ENABLE_LUA,
      "A boolean value that indicates if lua support is enabled in LLDB");
  AddBoolConfigEntry(*config_up, "fbsdvmcore", LLDB_ENABLE_FBSDVMCORE,
                     "A boolean value that indicates if fbsdvmcore support is "
                     "enabled in LLDB");
  AddLLVMTargets(*config_up);

  SBStructuredData data;
  data.m_impl_up->SetObjectSP(std::move(config_up));
  return data;
}

bool SBDebugger::StateIsRunningState(StateType state) {
  LLDB_INSTRUMENT_VA(state);

  const bool result = lldb_private::StateIsRunningState(state);

  return result;
}

bool SBDebugger::StateIsStoppedState(StateType state) {
  LLDB_INSTRUMENT_VA(state);

  const bool result = lldb_private::StateIsStoppedState(state, false);

  return result;
}

lldb::SBTarget SBDebugger::CreateTarget(const char *filename,
                                        const char *target_triple,
                                        const char *platform_name,
                                        bool add_dependent_modules,
                                        lldb::SBError &sb_error) {
  LLDB_INSTRUMENT_VA(this, filename, target_triple, platform_name,
                     add_dependent_modules, sb_error);

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    sb_error.Clear();
    OptionGroupPlatform platform_options(false);
    platform_options.SetPlatformName(platform_name);

    sb_error.ref() = m_opaque_sp->GetTargetList().CreateTarget(
        *m_opaque_sp, filename, target_triple,
        add_dependent_modules ? eLoadDependentsYes : eLoadDependentsNo,
        &platform_options, target_sp);

    if (sb_error.Success())
      sb_target.SetSP(target_sp);
  } else {
    sb_error.SetErrorString("invalid debugger");
  }

  Log *log = GetLog(LLDBLog::API);
  LLDB_LOGF(log,
            "SBDebugger(%p)::CreateTarget (filename=\"%s\", triple=%s, "
            "platform_name=%s, add_dependent_modules=%u, error=%s) => "
            "SBTarget(%p)",
            static_cast<void *>(m_opaque_sp.get()), filename, target_triple,
            platform_name, add_dependent_modules, sb_error.GetCString(),
            static_cast<void *>(target_sp.get()));

  return sb_target;
}

SBTarget
SBDebugger::CreateTargetWithFileAndTargetTriple(const char *filename,
                                                const char *target_triple) {
  LLDB_INSTRUMENT_VA(this, filename, target_triple);

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    const bool add_dependent_modules = true;
    Status error(m_opaque_sp->GetTargetList().CreateTarget(
        *m_opaque_sp, filename, target_triple,
        add_dependent_modules ? eLoadDependentsYes : eLoadDependentsNo, nullptr,
        target_sp));
    sb_target.SetSP(target_sp);
  }

  Log *log = GetLog(LLDBLog::API);
  LLDB_LOGF(log,
            "SBDebugger(%p)::CreateTargetWithFileAndTargetTriple "
            "(filename=\"%s\", triple=%s) => SBTarget(%p)",
            static_cast<void *>(m_opaque_sp.get()), filename, target_triple,
            static_cast<void *>(target_sp.get()));

  return sb_target;
}

SBTarget SBDebugger::CreateTargetWithFileAndArch(const char *filename,
                                                 const char *arch_cstr) {
  LLDB_INSTRUMENT_VA(this, filename, arch_cstr);

  Log *log = GetLog(LLDBLog::API);

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    Status error;
    if (arch_cstr == nullptr) {
      // The version of CreateTarget that takes an ArchSpec won't accept an
      // empty ArchSpec, so when the arch hasn't been specified, we need to
      // call the target triple version.
      error = m_opaque_sp->GetTargetList().CreateTarget(
          *m_opaque_sp, filename, arch_cstr, eLoadDependentsYes, nullptr,
          target_sp);
    } else {
      PlatformSP platform_sp =
          m_opaque_sp->GetPlatformList().GetSelectedPlatform();
      ArchSpec arch =
          Platform::GetAugmentedArchSpec(platform_sp.get(), arch_cstr);
      if (arch.IsValid())
        error = m_opaque_sp->GetTargetList().CreateTarget(
            *m_opaque_sp, filename, arch, eLoadDependentsYes, platform_sp,
            target_sp);
      else
        error.SetErrorStringWithFormat("invalid arch_cstr: %s", arch_cstr);
    }
    if (error.Success())
      sb_target.SetSP(target_sp);
  }

  LLDB_LOGF(log,
            "SBDebugger(%p)::CreateTargetWithFileAndArch (filename=\"%s\", "
            "arch=%s) => SBTarget(%p)",
            static_cast<void *>(m_opaque_sp.get()),
            filename ? filename : "<unspecified>",
            arch_cstr ? arch_cstr : "<unspecified>",
            static_cast<void *>(target_sp.get()));

  return sb_target;
}

SBTarget SBDebugger::CreateTarget(const char *filename) {
  LLDB_INSTRUMENT_VA(this, filename);

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    Status error;
    const bool add_dependent_modules = true;
    error = m_opaque_sp->GetTargetList().CreateTarget(
        *m_opaque_sp, filename, "",
        add_dependent_modules ? eLoadDependentsYes : eLoadDependentsNo, nullptr,
        target_sp);

    if (error.Success())
      sb_target.SetSP(target_sp);
  }
  Log *log = GetLog(LLDBLog::API);
  LLDB_LOGF(log,
            "SBDebugger(%p)::CreateTarget (filename=\"%s\") => SBTarget(%p)",
            static_cast<void *>(m_opaque_sp.get()), filename,
            static_cast<void *>(target_sp.get()));
  return sb_target;
}

SBTarget SBDebugger::GetDummyTarget() {
  LLDB_INSTRUMENT_VA(this);

  SBTarget sb_target;
  if (m_opaque_sp) {
    sb_target.SetSP(m_opaque_sp->GetDummyTarget().shared_from_this());
  }
  Log *log = GetLog(LLDBLog::API);
  LLDB_LOGF(log, "SBDebugger(%p)::GetDummyTarget() => SBTarget(%p)",
            static_cast<void *>(m_opaque_sp.get()),
            static_cast<void *>(sb_target.GetSP().get()));
  return sb_target;
}

bool SBDebugger::DeleteTarget(lldb::SBTarget &target) {
  LLDB_INSTRUMENT_VA(this, target);

  bool result = false;
  if (m_opaque_sp) {
    TargetSP target_sp(target.GetSP());
    if (target_sp) {
      // No need to lock, the target list is thread safe
      result = m_opaque_sp->GetTargetList().DeleteTarget(target_sp);
      target_sp->Destroy();
      target.Clear();
    }
  }

  Log *log = GetLog(LLDBLog::API);
  LLDB_LOGF(log, "SBDebugger(%p)::DeleteTarget (SBTarget(%p)) => %i",
            static_cast<void *>(m_opaque_sp.get()),
            static_cast<void *>(target.m_opaque_sp.get()), result);

  return result;
}

SBTarget SBDebugger::GetTargetAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBTarget sb_target;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    sb_target.SetSP(m_opaque_sp->GetTargetList().GetTargetAtIndex(idx));
  }
  return sb_target;
}

uint32_t SBDebugger::GetIndexOfTarget(lldb::SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  lldb::TargetSP target_sp = target.GetSP();
  if (!target_sp)
    return UINT32_MAX;

  if (!m_opaque_sp)
    return UINT32_MAX;

  return m_opaque_sp->GetTargetList().GetIndexOfTarget(target.GetSP());
}

SBTarget SBDebugger::FindTargetWithProcessID(lldb::pid_t pid) {
  LLDB_INSTRUMENT_VA(this, pid);

  SBTarget sb_target;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    sb_target.SetSP(m_opaque_sp->GetTargetList().FindTargetWithProcessID(pid));
  }
  return sb_target;
}

SBTarget SBDebugger::FindTargetWithFileAndArch(const char *filename,
                                               const char *arch_name) {
  LLDB_INSTRUMENT_VA(this, filename, arch_name);

  SBTarget sb_target;
  if (m_opaque_sp && filename && filename[0]) {
    // No need to lock, the target list is thread safe
    ArchSpec arch = Platform::GetAugmentedArchSpec(
        m_opaque_sp->GetPlatformList().GetSelectedPlatform().get(), arch_name);
    TargetSP target_sp(
        m_opaque_sp->GetTargetList().FindTargetWithExecutableAndArchitecture(
            FileSpec(filename), arch_name ? &arch : nullptr));
    sb_target.SetSP(target_sp);
  }
  return sb_target;
}

SBTarget SBDebugger::FindTargetWithLLDBProcess(const ProcessSP &process_sp) {
  SBTarget sb_target;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    sb_target.SetSP(
        m_opaque_sp->GetTargetList().FindTargetWithProcess(process_sp.get()));
  }
  return sb_target;
}

uint32_t SBDebugger::GetNumTargets() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    return m_opaque_sp->GetTargetList().GetNumTargets();
  }
  return 0;
}

SBTarget SBDebugger::GetSelectedTarget() {
  LLDB_INSTRUMENT_VA(this);

  Log *log = GetLog(LLDBLog::API);

  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    // No need to lock, the target list is thread safe
    target_sp = m_opaque_sp->GetTargetList().GetSelectedTarget();
    sb_target.SetSP(target_sp);
  }

  if (log) {
    SBStream sstr;
    sb_target.GetDescription(sstr, eDescriptionLevelBrief);
    LLDB_LOGF(log, "SBDebugger(%p)::GetSelectedTarget () => SBTarget(%p): %s",
              static_cast<void *>(m_opaque_sp.get()),
              static_cast<void *>(target_sp.get()), sstr.GetData());
  }

  return sb_target;
}

void SBDebugger::SetSelectedTarget(SBTarget &sb_target) {
  LLDB_INSTRUMENT_VA(this, sb_target);

  Log *log = GetLog(LLDBLog::API);

  TargetSP target_sp(sb_target.GetSP());
  if (m_opaque_sp) {
    m_opaque_sp->GetTargetList().SetSelectedTarget(target_sp);
  }
  if (log) {
    SBStream sstr;
    sb_target.GetDescription(sstr, eDescriptionLevelBrief);
    LLDB_LOGF(log, "SBDebugger(%p)::SetSelectedTarget () => SBTarget(%p): %s",
              static_cast<void *>(m_opaque_sp.get()),
              static_cast<void *>(target_sp.get()), sstr.GetData());
  }
}

SBPlatform SBDebugger::GetSelectedPlatform() {
  LLDB_INSTRUMENT_VA(this);

  Log *log = GetLog(LLDBLog::API);

  SBPlatform sb_platform;
  DebuggerSP debugger_sp(m_opaque_sp);
  if (debugger_sp) {
    sb_platform.SetSP(debugger_sp->GetPlatformList().GetSelectedPlatform());
  }
  LLDB_LOGF(log, "SBDebugger(%p)::GetSelectedPlatform () => SBPlatform(%p): %s",
            static_cast<void *>(m_opaque_sp.get()),
            static_cast<void *>(sb_platform.GetSP().get()),
            sb_platform.GetName());
  return sb_platform;
}

void SBDebugger::SetSelectedPlatform(SBPlatform &sb_platform) {
  LLDB_INSTRUMENT_VA(this, sb_platform);

  Log *log = GetLog(LLDBLog::API);

  DebuggerSP debugger_sp(m_opaque_sp);
  if (debugger_sp) {
    debugger_sp->GetPlatformList().SetSelectedPlatform(sb_platform.GetSP());
  }

  LLDB_LOGF(log, "SBDebugger(%p)::SetSelectedPlatform (SBPlatform(%p) %s)",
            static_cast<void *>(m_opaque_sp.get()),
            static_cast<void *>(sb_platform.GetSP().get()),
            sb_platform.GetName());
}

uint32_t SBDebugger::GetNumPlatforms() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp) {
    // No need to lock, the platform list is thread safe
    return m_opaque_sp->GetPlatformList().GetSize();
  }
  return 0;
}

SBPlatform SBDebugger::GetPlatformAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBPlatform sb_platform;
  if (m_opaque_sp) {
    // No need to lock, the platform list is thread safe
    sb_platform.SetSP(m_opaque_sp->GetPlatformList().GetAtIndex(idx));
  }
  return sb_platform;
}

uint32_t SBDebugger::GetNumAvailablePlatforms() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t idx = 0;
  while (true) {
    if (PluginManager::GetPlatformPluginNameAtIndex(idx).empty()) {
      break;
    }
    ++idx;
  }
  // +1 for the host platform, which should always appear first in the list.
  return idx + 1;
}

SBStructuredData SBDebugger::GetAvailablePlatformInfoAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBStructuredData data;
  auto platform_dict = std::make_unique<StructuredData::Dictionary>();
  llvm::StringRef name_str("name"), desc_str("description");

  if (idx == 0) {
    PlatformSP host_platform_sp(Platform::GetHostPlatform());
    platform_dict->AddStringItem(name_str, host_platform_sp->GetPluginName());
    platform_dict->AddStringItem(
        desc_str, llvm::StringRef(host_platform_sp->GetDescription()));
  } else if (idx > 0) {
    llvm::StringRef plugin_name =
        PluginManager::GetPlatformPluginNameAtIndex(idx - 1);
    if (plugin_name.empty()) {
      return data;
    }
    platform_dict->AddStringItem(name_str, llvm::StringRef(plugin_name));

    llvm::StringRef plugin_desc =
        PluginManager::GetPlatformPluginDescriptionAtIndex(idx - 1);
    platform_dict->AddStringItem(desc_str, llvm::StringRef(plugin_desc));
  }

  data.m_impl_up->SetObjectSP(
      StructuredData::ObjectSP(platform_dict.release()));
  return data;
}

void SBDebugger::DispatchInput(void *baton, const void *data, size_t data_len) {
  LLDB_INSTRUMENT_VA(this, baton, data, data_len);

  DispatchInput(data, data_len);
}

void SBDebugger::DispatchInput(const void *data, size_t data_len) {
  LLDB_INSTRUMENT_VA(this, data, data_len);

  //    Log *log(GetLog (LLDBLog::API));
  //
  //    if (log)
  //        LLDB_LOGF(log, "SBDebugger(%p)::DispatchInput (data=\"%.*s\",
  //        size_t=%" PRIu64 ")",
  //                     m_opaque_sp.get(),
  //                     (int) data_len,
  //                     (const char *) data,
  //                     (uint64_t)data_len);
  //
  //    if (m_opaque_sp)
  //        m_opaque_sp->DispatchInput ((const char *) data, data_len);
}

void SBDebugger::DispatchInputInterrupt() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    m_opaque_sp->DispatchInputInterrupt();
}

void SBDebugger::DispatchInputEndOfFile() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    m_opaque_sp->DispatchInputEndOfFile();
}

void SBDebugger::PushInputReader(SBInputReader &reader) {
  LLDB_INSTRUMENT_VA(this, reader);
}

void SBDebugger::RunCommandInterpreter(bool auto_handle_events,
                                       bool spawn_thread) {
  LLDB_INSTRUMENT_VA(this, auto_handle_events, spawn_thread);

  if (m_opaque_sp) {
    CommandInterpreterRunOptions options;
    options.SetAutoHandleEvents(auto_handle_events);
    options.SetSpawnThread(spawn_thread);
    m_opaque_sp->GetCommandInterpreter().RunCommandInterpreter(options);
  }
}

void SBDebugger::RunCommandInterpreter(bool auto_handle_events,
                                       bool spawn_thread,
                                       SBCommandInterpreterRunOptions &options,
                                       int &num_errors, bool &quit_requested,
                                       bool &stopped_for_crash)

{
  LLDB_INSTRUMENT_VA(this, auto_handle_events, spawn_thread, options,
                     num_errors, quit_requested, stopped_for_crash);

  if (m_opaque_sp) {
    options.SetAutoHandleEvents(auto_handle_events);
    options.SetSpawnThread(spawn_thread);
    CommandInterpreter &interp = m_opaque_sp->GetCommandInterpreter();
    CommandInterpreterRunResult result =
        interp.RunCommandInterpreter(options.ref());
    num_errors = result.GetNumErrors();
    quit_requested =
        result.IsResult(lldb::eCommandInterpreterResultQuitRequested);
    stopped_for_crash =
        result.IsResult(lldb::eCommandInterpreterResultInferiorCrash);
  }
}

SBCommandInterpreterRunResult SBDebugger::RunCommandInterpreter(
    const SBCommandInterpreterRunOptions &options) {
  LLDB_INSTRUMENT_VA(this, options);

  if (!m_opaque_sp)
    return SBCommandInterpreterRunResult();

  CommandInterpreter &interp = m_opaque_sp->GetCommandInterpreter();
  CommandInterpreterRunResult result =
      interp.RunCommandInterpreter(options.ref());

  return SBCommandInterpreterRunResult(result);
}

SBError SBDebugger::RunREPL(lldb::LanguageType language,
                            const char *repl_options) {
  LLDB_INSTRUMENT_VA(this, language, repl_options);

  SBError error;
  if (m_opaque_sp)
    error.ref() = m_opaque_sp->RunREPL(language, repl_options);
  else
    error.SetErrorString("invalid debugger");
  return error;
}

void SBDebugger::reset(const DebuggerSP &debugger_sp) {
  m_opaque_sp = debugger_sp;
}

Debugger *SBDebugger::get() const { return m_opaque_sp.get(); }

Debugger &SBDebugger::ref() const {
  assert(m_opaque_sp.get());
  return *m_opaque_sp;
}

const lldb::DebuggerSP &SBDebugger::get_sp() const { return m_opaque_sp; }

SBDebugger SBDebugger::FindDebuggerWithID(int id) {
  LLDB_INSTRUMENT_VA(id);

  // No need to lock, the debugger list is thread safe
  SBDebugger sb_debugger;
  DebuggerSP debugger_sp = Debugger::FindDebuggerWithID(id);
  if (debugger_sp)
    sb_debugger.reset(debugger_sp);
  return sb_debugger;
}

const char *SBDebugger::GetInstanceName() {
  LLDB_INSTRUMENT_VA(this);

  if (!m_opaque_sp)
    return nullptr;

  return ConstString(m_opaque_sp->GetInstanceName()).AsCString();
}

SBError SBDebugger::SetInternalVariable(const char *var_name, const char *value,
                                        const char *debugger_instance_name) {
  LLDB_INSTRUMENT_VA(var_name, value, debugger_instance_name);

  SBError sb_error;
  DebuggerSP debugger_sp(
      Debugger::FindDebuggerWithInstanceName(debugger_instance_name));
  Status error;
  if (debugger_sp) {
    ExecutionContext exe_ctx(
        debugger_sp->GetCommandInterpreter().GetExecutionContext());
    error = debugger_sp->SetPropertyValue(&exe_ctx, eVarSetOperationAssign,
                                          var_name, value);
  } else {
    error.SetErrorStringWithFormat("invalid debugger instance name '%s'",
                                   debugger_instance_name);
  }
  if (error.Fail())
    sb_error.SetError(error);
  return sb_error;
}

SBStringList
SBDebugger::GetInternalVariableValue(const char *var_name,
                                     const char *debugger_instance_name) {
  LLDB_INSTRUMENT_VA(var_name, debugger_instance_name);

  DebuggerSP debugger_sp(
      Debugger::FindDebuggerWithInstanceName(debugger_instance_name));
  Status error;
  if (debugger_sp) {
    ExecutionContext exe_ctx(
        debugger_sp->GetCommandInterpreter().GetExecutionContext());
    lldb::OptionValueSP value_sp(
        debugger_sp->GetPropertyValue(&exe_ctx, var_name, error));
    if (value_sp) {
      StreamString value_strm;
      value_sp->DumpValue(&exe_ctx, value_strm, OptionValue::eDumpOptionValue);
      const std::string &value_str = std::string(value_strm.GetString());
      if (!value_str.empty()) {
        StringList string_list;
        string_list.SplitIntoLines(value_str);
        return SBStringList(&string_list);
      }
    }
  }
  return SBStringList();
}

uint32_t SBDebugger::GetTerminalWidth() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetTerminalWidth() : 0);
}

void SBDebugger::SetTerminalWidth(uint32_t term_width) {
  LLDB_INSTRUMENT_VA(this, term_width);

  if (m_opaque_sp)
    m_opaque_sp->SetTerminalWidth(term_width);
}

const char *SBDebugger::GetPrompt() const {
  LLDB_INSTRUMENT_VA(this);

  Log *log = GetLog(LLDBLog::API);

  LLDB_LOG(log, "SBDebugger({0:x})::GetPrompt () => \"{1}\"",
           static_cast<void *>(m_opaque_sp.get()),
           (m_opaque_sp ? m_opaque_sp->GetPrompt() : ""));

  return (m_opaque_sp ? ConstString(m_opaque_sp->GetPrompt()).GetCString()
                      : nullptr);
}

void SBDebugger::SetPrompt(const char *prompt) {
  LLDB_INSTRUMENT_VA(this, prompt);

  if (m_opaque_sp)
    m_opaque_sp->SetPrompt(llvm::StringRef(prompt));
}

const char *SBDebugger::GetReproducerPath() const {
  LLDB_INSTRUMENT_VA(this);

  return "GetReproducerPath has been deprecated";
}

ScriptLanguage SBDebugger::GetScriptLanguage() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetScriptLanguage() : eScriptLanguageNone);
}

void SBDebugger::SetScriptLanguage(ScriptLanguage script_lang) {
  LLDB_INSTRUMENT_VA(this, script_lang);

  if (m_opaque_sp) {
    m_opaque_sp->SetScriptLanguage(script_lang);
  }
}

LanguageType SBDebugger::GetREPLLanguage() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetREPLLanguage() : eLanguageTypeUnknown);
}

void SBDebugger::SetREPLLanguage(LanguageType repl_lang) {
  LLDB_INSTRUMENT_VA(this, repl_lang);

  if (m_opaque_sp) {
    m_opaque_sp->SetREPLLanguage(repl_lang);
  }
}

bool SBDebugger::SetUseExternalEditor(bool value) {
  LLDB_INSTRUMENT_VA(this, value);

  return (m_opaque_sp ? m_opaque_sp->SetUseExternalEditor(value) : false);
}

bool SBDebugger::GetUseExternalEditor() {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetUseExternalEditor() : false);
}

bool SBDebugger::SetUseColor(bool value) {
  LLDB_INSTRUMENT_VA(this, value);

  return (m_opaque_sp ? m_opaque_sp->SetUseColor(value) : false);
}

bool SBDebugger::GetUseColor() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetUseColor() : false);
}

bool SBDebugger::SetUseSourceCache(bool value) {
  LLDB_INSTRUMENT_VA(this, value);

  return (m_opaque_sp ? m_opaque_sp->SetUseSourceCache(value) : false);
}

bool SBDebugger::GetUseSourceCache() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetUseSourceCache() : false);
}

bool SBDebugger::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  if (m_opaque_sp) {
    const char *name = m_opaque_sp->GetInstanceName().c_str();
    user_id_t id = m_opaque_sp->GetID();
    strm.Printf("Debugger (instance: \"%s\", id: %" PRIu64 ")", name, id);
  } else
    strm.PutCString("No value");

  return true;
}

user_id_t SBDebugger::GetID() {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp ? m_opaque_sp->GetID() : LLDB_INVALID_UID);
}

SBError SBDebugger::SetCurrentPlatform(const char *platform_name_cstr) {
  LLDB_INSTRUMENT_VA(this, platform_name_cstr);

  SBError sb_error;
  if (m_opaque_sp) {
    if (platform_name_cstr && platform_name_cstr[0]) {
      PlatformList &platforms = m_opaque_sp->GetPlatformList();
      if (PlatformSP platform_sp = platforms.GetOrCreate(platform_name_cstr))
        platforms.SetSelectedPlatform(platform_sp);
      else
        sb_error.ref().SetErrorString("platform not found");
    } else {
      sb_error.ref().SetErrorString("invalid platform name");
    }
  } else {
    sb_error.ref().SetErrorString("invalid debugger");
  }
  return sb_error;
}

bool SBDebugger::SetCurrentPlatformSDKRoot(const char *sysroot) {
  LLDB_INSTRUMENT_VA(this, sysroot);

  if (SBPlatform platform = GetSelectedPlatform()) {
    platform.SetSDKRoot(sysroot);
    return true;
  }
  return false;
}

bool SBDebugger::GetCloseInputOnEOF() const {
  LLDB_INSTRUMENT_VA(this);

  return false;
}

void SBDebugger::SetCloseInputOnEOF(bool b) {
  LLDB_INSTRUMENT_VA(this, b);
}

SBTypeCategory SBDebugger::GetCategory(const char *category_name) {
  LLDB_INSTRUMENT_VA(this, category_name);

  if (!category_name || *category_name == 0)
    return SBTypeCategory();

  TypeCategoryImplSP category_sp;

  if (DataVisualization::Categories::GetCategory(ConstString(category_name),
                                                 category_sp, false)) {
    return SBTypeCategory(category_sp);
  } else {
    return SBTypeCategory();
  }
}

SBTypeCategory SBDebugger::GetCategory(lldb::LanguageType lang_type) {
  LLDB_INSTRUMENT_VA(this, lang_type);

  TypeCategoryImplSP category_sp;
  if (DataVisualization::Categories::GetCategory(lang_type, category_sp)) {
    return SBTypeCategory(category_sp);
  } else {
    return SBTypeCategory();
  }
}

SBTypeCategory SBDebugger::CreateCategory(const char *category_name) {
  LLDB_INSTRUMENT_VA(this, category_name);

  if (!category_name || *category_name == 0)
    return SBTypeCategory();

  TypeCategoryImplSP category_sp;

  if (DataVisualization::Categories::GetCategory(ConstString(category_name),
                                                 category_sp, true)) {
    return SBTypeCategory(category_sp);
  } else {
    return SBTypeCategory();
  }
}

bool SBDebugger::DeleteCategory(const char *category_name) {
  LLDB_INSTRUMENT_VA(this, category_name);

  if (!category_name || *category_name == 0)
    return false;

  return DataVisualization::Categories::Delete(ConstString(category_name));
}

uint32_t SBDebugger::GetNumCategories() {
  LLDB_INSTRUMENT_VA(this);

  return DataVisualization::Categories::GetCount();
}

SBTypeCategory SBDebugger::GetCategoryAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  return SBTypeCategory(
      DataVisualization::Categories::GetCategoryAtIndex(index));
}

SBTypeCategory SBDebugger::GetDefaultCategory() {
  LLDB_INSTRUMENT_VA(this);

  return GetCategory("default");
}

SBTypeFormat SBDebugger::GetFormatForType(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  SBTypeCategory default_category_sb = GetDefaultCategory();
  if (default_category_sb.GetEnabled())
    return default_category_sb.GetFormatForType(type_name);
  return SBTypeFormat();
}

SBTypeSummary SBDebugger::GetSummaryForType(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  if (!type_name.IsValid())
    return SBTypeSummary();
  return SBTypeSummary(DataVisualization::GetSummaryForType(type_name.GetSP()));
}

SBTypeFilter SBDebugger::GetFilterForType(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  if (!type_name.IsValid())
    return SBTypeFilter();
  return SBTypeFilter(DataVisualization::GetFilterForType(type_name.GetSP()));
}

SBTypeSynthetic SBDebugger::GetSyntheticForType(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  if (!type_name.IsValid())
    return SBTypeSynthetic();
  return SBTypeSynthetic(
      DataVisualization::GetSyntheticForType(type_name.GetSP()));
}

static llvm::ArrayRef<const char *> GetCategoryArray(const char **categories) {
  if (categories == nullptr)
    return {};
  size_t len = 0;
  while (categories[len] != nullptr)
    ++len;
  return llvm::ArrayRef(categories, len);
}

bool SBDebugger::EnableLog(const char *channel, const char **categories) {
  LLDB_INSTRUMENT_VA(this, channel, categories);

  if (m_opaque_sp) {
    uint32_t log_options =
        LLDB_LOG_OPTION_PREPEND_TIMESTAMP | LLDB_LOG_OPTION_PREPEND_THREAD_NAME;
    std::string error;
    llvm::raw_string_ostream error_stream(error);
    return m_opaque_sp->EnableLog(channel, GetCategoryArray(categories), "",
                                  log_options, /*buffer_size=*/0,
                                  eLogHandlerStream, error_stream);
  } else
    return false;
}

void SBDebugger::SetLoggingCallback(lldb::LogOutputCallback log_callback,
                                    void *baton) {
  LLDB_INSTRUMENT_VA(this, log_callback, baton);

  if (m_opaque_sp) {
    return m_opaque_sp->SetLoggingCallback(log_callback, baton);
  }
}

void SBDebugger::SetDestroyCallback(
    lldb::SBDebuggerDestroyCallback destroy_callback, void *baton) {
  LLDB_INSTRUMENT_VA(this, destroy_callback, baton);
  if (m_opaque_sp) {
    return m_opaque_sp->SetDestroyCallback(
        destroy_callback, baton);
  }
}

lldb::callback_token_t
SBDebugger::AddDestroyCallback(lldb::SBDebuggerDestroyCallback destroy_callback,
                               void *baton) {
  LLDB_INSTRUMENT_VA(this, destroy_callback, baton);

  if (m_opaque_sp)
    return m_opaque_sp->AddDestroyCallback(destroy_callback, baton);

  return LLDB_INVALID_CALLBACK_TOKEN;
}

bool SBDebugger::RemoveDestroyCallback(lldb::callback_token_t token) {
  LLDB_INSTRUMENT_VA(this, token);

  if (m_opaque_sp)
    return m_opaque_sp->RemoveDestroyCallback(token);

  return false;
}

SBTrace
SBDebugger::LoadTraceFromFile(SBError &error,
                              const SBFileSpec &trace_description_file) {
  LLDB_INSTRUMENT_VA(this, error, trace_description_file);
  return SBTrace::LoadTraceFromFile(error, *this, trace_description_file);
}

void SBDebugger::RequestInterrupt() {
  LLDB_INSTRUMENT_VA(this);
  
  if (m_opaque_sp)
    m_opaque_sp->RequestInterrupt();  
}
void SBDebugger::CancelInterruptRequest()  {
  LLDB_INSTRUMENT_VA(this);
  
  if (m_opaque_sp)
    m_opaque_sp->CancelInterruptRequest();  
}

bool SBDebugger::InterruptRequested()   {
  LLDB_INSTRUMENT_VA(this);
  
  if (m_opaque_sp)
    return m_opaque_sp->InterruptRequested();
  return false;
}

bool SBDebugger::SupportsLanguage(lldb::LanguageType language) {
  return TypeSystem::SupportsLanguageStatic(language);
}
