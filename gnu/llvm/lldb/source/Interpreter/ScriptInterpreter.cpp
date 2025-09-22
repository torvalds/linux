//===-- ScriptInterpreter.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#if defined(_WIN32)
#include "lldb/Host/windows/ConnectionGenericFileWindows.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

using namespace lldb;
using namespace lldb_private;

ScriptInterpreter::ScriptInterpreter(Debugger &debugger,
                                     lldb::ScriptLanguage script_lang)
    : m_debugger(debugger), m_script_lang(script_lang) {}

void ScriptInterpreter::CollectDataForBreakpointCommandCallback(
    std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
    CommandReturnObject &result) {
  result.AppendError(
      "This script interpreter does not support breakpoint callbacks.");
}

void ScriptInterpreter::CollectDataForWatchpointCommandCallback(
    WatchpointOptions *bp_options, CommandReturnObject &result) {
  result.AppendError(
      "This script interpreter does not support watchpoint callbacks.");
}

StructuredData::DictionarySP ScriptInterpreter::GetInterpreterInfo() {
  return nullptr;
}

bool ScriptInterpreter::LoadScriptingModule(const char *filename,
                                            const LoadScriptOptions &options,
                                            lldb_private::Status &error,
                                            StructuredData::ObjectSP *module_sp,
                                            FileSpec extra_search_dir) {
  error.SetErrorString(
      "This script interpreter does not support importing modules.");
  return false;
}

std::string ScriptInterpreter::LanguageToString(lldb::ScriptLanguage language) {
  switch (language) {
  case eScriptLanguageNone:
    return "None";
  case eScriptLanguagePython:
    return "Python";
  case eScriptLanguageLua:
    return "Lua";
  case eScriptLanguageUnknown:
    return "Unknown";
  }
  llvm_unreachable("Unhandled ScriptInterpreter!");
}

lldb::DataExtractorSP
ScriptInterpreter::GetDataExtractorFromSBData(const lldb::SBData &data) const {
  return data.m_opaque_sp;
}

lldb::BreakpointSP ScriptInterpreter::GetOpaqueTypeFromSBBreakpoint(
    const lldb::SBBreakpoint &breakpoint) const {
  return breakpoint.m_opaque_wp.lock();
}

lldb::ProcessAttachInfoSP ScriptInterpreter::GetOpaqueTypeFromSBAttachInfo(
    const lldb::SBAttachInfo &attach_info) const {
  return attach_info.m_opaque_sp;
}

lldb::ProcessLaunchInfoSP ScriptInterpreter::GetOpaqueTypeFromSBLaunchInfo(
    const lldb::SBLaunchInfo &launch_info) const {
  return std::make_shared<ProcessLaunchInfo>(
      *reinterpret_cast<ProcessLaunchInfo *>(launch_info.m_opaque_sp.get()));
}

Status
ScriptInterpreter::GetStatusFromSBError(const lldb::SBError &error) const {
  if (error.m_opaque_up)
    return *error.m_opaque_up;

  return Status();
}

Event *
ScriptInterpreter::GetOpaqueTypeFromSBEvent(const lldb::SBEvent &event) const {
  return event.m_opaque_ptr;
}

lldb::StreamSP ScriptInterpreter::GetOpaqueTypeFromSBStream(
    const lldb::SBStream &stream) const {
  if (stream.m_opaque_up) {
    lldb::StreamSP s = std::make_shared<lldb_private::StreamString>();
    *s << reinterpret_cast<StreamString *>(stream.m_opaque_up.get())->m_packet;
    return s;
  }

  return nullptr;
}

std::optional<MemoryRegionInfo>
ScriptInterpreter::GetOpaqueTypeFromSBMemoryRegionInfo(
    const lldb::SBMemoryRegionInfo &mem_region) const {
  if (!mem_region.m_opaque_up)
    return std::nullopt;
  return *mem_region.m_opaque_up.get();
}

lldb::ScriptLanguage
ScriptInterpreter::StringToLanguage(const llvm::StringRef &language) {
  if (language.equals_insensitive(LanguageToString(eScriptLanguageNone)))
    return eScriptLanguageNone;
  if (language.equals_insensitive(LanguageToString(eScriptLanguagePython)))
    return eScriptLanguagePython;
  if (language.equals_insensitive(LanguageToString(eScriptLanguageLua)))
    return eScriptLanguageLua;
  return eScriptLanguageUnknown;
}

Status ScriptInterpreter::SetBreakpointCommandCallback(
    std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
    const char *callback_text) {
  Status error;
  for (BreakpointOptions &bp_options : bp_options_vec) {
    error = SetBreakpointCommandCallback(bp_options, callback_text,
                                         /*is_callback=*/false);
    if (!error.Success())
      break;
  }
  return error;
}

Status ScriptInterpreter::SetBreakpointCommandCallbackFunction(
    std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
    const char *function_name, StructuredData::ObjectSP extra_args_sp) {
  Status error;
  for (BreakpointOptions &bp_options : bp_options_vec) {
    error = SetBreakpointCommandCallbackFunction(bp_options, function_name,
                                                 extra_args_sp);
    if (!error.Success())
      return error;
  }
  return error;
}

std::unique_ptr<ScriptInterpreterLocker>
ScriptInterpreter::AcquireInterpreterLock() {
  return std::make_unique<ScriptInterpreterLocker>();
}

static void ReadThreadBytesReceived(void *baton, const void *src,
                                    size_t src_len) {
  if (src && src_len) {
    Stream *strm = (Stream *)baton;
    strm->Write(src, src_len);
    strm->Flush();
  }
}

llvm::Expected<std::unique_ptr<ScriptInterpreterIORedirect>>
ScriptInterpreterIORedirect::Create(bool enable_io, Debugger &debugger,
                                    CommandReturnObject *result) {
  if (enable_io)
    return std::unique_ptr<ScriptInterpreterIORedirect>(
        new ScriptInterpreterIORedirect(debugger, result));

  auto nullin = FileSystem::Instance().Open(FileSpec(FileSystem::DEV_NULL),
                                            File::eOpenOptionReadOnly);
  if (!nullin)
    return nullin.takeError();

  auto nullout = FileSystem::Instance().Open(FileSpec(FileSystem::DEV_NULL),
                                             File::eOpenOptionWriteOnly);
  if (!nullout)
    return nullin.takeError();

  return std::unique_ptr<ScriptInterpreterIORedirect>(
      new ScriptInterpreterIORedirect(std::move(*nullin), std::move(*nullout)));
}

ScriptInterpreterIORedirect::ScriptInterpreterIORedirect(
    std::unique_ptr<File> input, std::unique_ptr<File> output)
    : m_input_file_sp(std::move(input)),
      m_output_file_sp(std::make_shared<StreamFile>(std::move(output))),
      m_error_file_sp(m_output_file_sp),
      m_communication("lldb.ScriptInterpreterIORedirect.comm"),
      m_disconnect(false) {}

ScriptInterpreterIORedirect::ScriptInterpreterIORedirect(
    Debugger &debugger, CommandReturnObject *result)
    : m_communication("lldb.ScriptInterpreterIORedirect.comm"),
      m_disconnect(false) {

  if (result) {
    m_input_file_sp = debugger.GetInputFileSP();

    Pipe pipe;
    Status pipe_result = pipe.CreateNew(false);
#if defined(_WIN32)
    lldb::file_t read_file = pipe.GetReadNativeHandle();
    pipe.ReleaseReadFileDescriptor();
    std::unique_ptr<ConnectionGenericFile> conn_up =
        std::make_unique<ConnectionGenericFile>(read_file, true);
#else
    std::unique_ptr<ConnectionFileDescriptor> conn_up =
        std::make_unique<ConnectionFileDescriptor>(
            pipe.ReleaseReadFileDescriptor(), true);
#endif

    if (conn_up->IsConnected()) {
      m_communication.SetConnection(std::move(conn_up));
      m_communication.SetReadThreadBytesReceivedCallback(
          ReadThreadBytesReceived, &result->GetOutputStream());
      m_communication.StartReadThread();
      m_disconnect = true;

      FILE *outfile_handle = fdopen(pipe.ReleaseWriteFileDescriptor(), "w");
      m_output_file_sp = std::make_shared<StreamFile>(outfile_handle, true);
      m_error_file_sp = m_output_file_sp;
      if (outfile_handle)
        ::setbuf(outfile_handle, nullptr);

      result->SetImmediateOutputFile(debugger.GetOutputStream().GetFileSP());
      result->SetImmediateErrorFile(debugger.GetErrorStream().GetFileSP());
    }
  }

  if (!m_input_file_sp || !m_output_file_sp || !m_error_file_sp)
    debugger.AdoptTopIOHandlerFilesIfInvalid(m_input_file_sp, m_output_file_sp,
                                             m_error_file_sp);
}

void ScriptInterpreterIORedirect::Flush() {
  if (m_output_file_sp)
    m_output_file_sp->Flush();
  if (m_error_file_sp)
    m_error_file_sp->Flush();
}

ScriptInterpreterIORedirect::~ScriptInterpreterIORedirect() {
  if (!m_disconnect)
    return;

  assert(m_output_file_sp);
  assert(m_error_file_sp);
  assert(m_output_file_sp == m_error_file_sp);

  // Close the write end of the pipe since we are done with our one line
  // script. This should cause the read thread that output_comm is using to
  // exit.
  m_output_file_sp->GetFile().Close();
  // The close above should cause this thread to exit when it gets to the end
  // of file, so let it get all its data.
  m_communication.JoinReadThread();
  // Now we can close the read end of the pipe.
  m_communication.Disconnect();
}
