//===-- Trace.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Trace.h"

#include "llvm/Support/Format.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Stream.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

// Helper structs used to extract the type of a JSON trace bundle description
// object without having to parse the entire object.

struct JSONSimpleTraceBundleDescription {
  std::string type;
};

namespace llvm {
namespace json {

bool fromJSON(const Value &value, JSONSimpleTraceBundleDescription &bundle,
              Path path) {
  json::ObjectMapper o(value, path);
  return o && o.map("type", bundle.type);
}

} // namespace json
} // namespace llvm

/// Helper functions for fetching data in maps and returning Optionals or
/// pointers instead of iterators for simplicity. It's worth mentioning that the
/// Optionals version can't return the inner data by reference because of
/// limitations in move constructors.
/// \{
template <typename K, typename V>
static std::optional<V> Lookup(DenseMap<K, V> &map, K k) {
  auto it = map.find(k);
  if (it == map.end())
    return std::nullopt;
  return it->second;
}

template <typename K, typename V>
static V *LookupAsPtr(DenseMap<K, V> &map, K k) {
  auto it = map.find(k);
  if (it == map.end())
    return nullptr;
  return &it->second;
}

/// Similar to the methods above but it looks for an item in a map of maps.
template <typename K1, typename K2, typename V>
static std::optional<V> Lookup(DenseMap<K1, DenseMap<K2, V>> &map, K1 k1,
                               K2 k2) {
  auto it = map.find(k1);
  if (it == map.end())
    return std::nullopt;
  return Lookup(it->second, k2);
}

/// Similar to the methods above but it looks for an item in a map of maps.
template <typename K1, typename K2, typename V>
static V *LookupAsPtr(DenseMap<K1, DenseMap<K2, V>> &map, K1 k1, K2 k2) {
  auto it = map.find(k1);
  if (it == map.end())
    return nullptr;
  return LookupAsPtr(it->second, k2);
}
/// \}

static Error createInvalidPlugInError(StringRef plugin_name) {
  return createStringError(
      std::errc::invalid_argument,
      "no trace plug-in matches the specified type: \"%s\"",
      plugin_name.data());
}

Expected<lldb::TraceSP>
Trace::LoadPostMortemTraceFromFile(Debugger &debugger,
                                   const FileSpec &trace_description_file) {

  auto buffer_or_error =
      MemoryBuffer::getFile(trace_description_file.GetPath());
  if (!buffer_or_error) {
    return createStringError(std::errc::invalid_argument,
                             "could not open input file: %s - %s.",
                             trace_description_file.GetPath().c_str(),
                             buffer_or_error.getError().message().c_str());
  }

  Expected<json::Value> session_file =
      json::parse(buffer_or_error.get()->getBuffer().str());
  if (!session_file) {
    return session_file.takeError();
  }

  return Trace::FindPluginForPostMortemProcess(
      debugger, *session_file,
      trace_description_file.GetDirectory().AsCString());
}

Expected<lldb::TraceSP> Trace::FindPluginForPostMortemProcess(
    Debugger &debugger, const json::Value &trace_bundle_description,
    StringRef bundle_dir) {
  JSONSimpleTraceBundleDescription json_bundle;
  json::Path::Root root("traceBundle");
  if (!json::fromJSON(trace_bundle_description, json_bundle, root))
    return root.getError();

  if (auto create_callback =
          PluginManager::GetTraceCreateCallback(json_bundle.type))
    return create_callback(trace_bundle_description, bundle_dir, debugger);

  return createInvalidPlugInError(json_bundle.type);
}

Expected<lldb::TraceSP> Trace::FindPluginForLiveProcess(llvm::StringRef name,
                                                        Process &process) {
  if (!process.IsLiveDebugSession())
    return createStringError(inconvertibleErrorCode(),
                             "Can't trace non-live processes");

  if (auto create_callback =
          PluginManager::GetTraceCreateCallbackForLiveProcess(name))
    return create_callback(process);

  return createInvalidPlugInError(name);
}

Expected<StringRef> Trace::FindPluginSchema(StringRef name) {
  StringRef schema = PluginManager::GetTraceSchema(name);
  if (!schema.empty())
    return schema;

  return createInvalidPlugInError(name);
}

Error Trace::Start(const llvm::json::Value &request) {
  if (!m_live_process)
    return createStringError(
        inconvertibleErrorCode(),
        "Attempted to start tracing without a live process.");
  return m_live_process->TraceStart(request);
}

Error Trace::Stop() {
  if (!m_live_process)
    return createStringError(
        inconvertibleErrorCode(),
        "Attempted to stop tracing without a live process.");
  return m_live_process->TraceStop(TraceStopRequest(GetPluginName()));
}

Error Trace::Stop(llvm::ArrayRef<lldb::tid_t> tids) {
  if (!m_live_process)
    return createStringError(
        inconvertibleErrorCode(),
        "Attempted to stop tracing without a live process.");
  return m_live_process->TraceStop(TraceStopRequest(GetPluginName(), tids));
}

Expected<std::string> Trace::GetLiveProcessState() {
  if (!m_live_process)
    return createStringError(
        inconvertibleErrorCode(),
        "Attempted to fetch live trace information without a live process.");
  return m_live_process->TraceGetState(GetPluginName());
}

std::optional<uint64_t>
Trace::GetLiveThreadBinaryDataSize(lldb::tid_t tid, llvm::StringRef kind) {
  Storage &storage = GetUpdatedStorage();
  return Lookup(storage.live_thread_data, tid, ConstString(kind));
}

std::optional<uint64_t> Trace::GetLiveCpuBinaryDataSize(lldb::cpu_id_t cpu_id,
                                                        llvm::StringRef kind) {
  Storage &storage = GetUpdatedStorage();
  return Lookup(storage.live_cpu_data_sizes, cpu_id, ConstString(kind));
}

std::optional<uint64_t>
Trace::GetLiveProcessBinaryDataSize(llvm::StringRef kind) {
  Storage &storage = GetUpdatedStorage();
  return Lookup(storage.live_process_data, ConstString(kind));
}

Expected<std::vector<uint8_t>>
Trace::GetLiveTraceBinaryData(const TraceGetBinaryDataRequest &request,
                              uint64_t expected_size) {
  if (!m_live_process)
    return createStringError(
        inconvertibleErrorCode(),
        formatv("Attempted to fetch live trace data without a live process. "
                "Data kind = {0}, tid = {1}, cpu id = {2}.",
                request.kind, request.tid, request.cpu_id));

  Expected<std::vector<uint8_t>> data =
      m_live_process->TraceGetBinaryData(request);

  if (!data)
    return data.takeError();

  if (data->size() != expected_size)
    return createStringError(
        inconvertibleErrorCode(),
        formatv("Got incomplete live trace data. Data kind = {0}, expected "
                "size = {1}, actual size = {2}, tid = {3}, cpu id = {4}",
                request.kind, expected_size, data->size(), request.tid,
                request.cpu_id));

  return data;
}

Expected<std::vector<uint8_t>>
Trace::GetLiveThreadBinaryData(lldb::tid_t tid, llvm::StringRef kind) {
  std::optional<uint64_t> size = GetLiveThreadBinaryDataSize(tid, kind);
  if (!size)
    return createStringError(
        inconvertibleErrorCode(),
        "Tracing data \"%s\" is not available for thread %" PRIu64 ".",
        kind.data(), tid);

  TraceGetBinaryDataRequest request{GetPluginName().str(), kind.str(), tid,
                                    /*cpu_id=*/std::nullopt};
  return GetLiveTraceBinaryData(request, *size);
}

Expected<std::vector<uint8_t>>
Trace::GetLiveCpuBinaryData(lldb::cpu_id_t cpu_id, llvm::StringRef kind) {
  if (!m_live_process)
    return createStringError(
        inconvertibleErrorCode(),
        "Attempted to fetch live cpu data without a live process.");
  std::optional<uint64_t> size = GetLiveCpuBinaryDataSize(cpu_id, kind);
  if (!size)
    return createStringError(
        inconvertibleErrorCode(),
        "Tracing data \"%s\" is not available for cpu_id %" PRIu64 ".",
        kind.data(), cpu_id);

  TraceGetBinaryDataRequest request{GetPluginName().str(), kind.str(),
                                    /*tid=*/std::nullopt, cpu_id};
  return m_live_process->TraceGetBinaryData(request);
}

Expected<std::vector<uint8_t>>
Trace::GetLiveProcessBinaryData(llvm::StringRef kind) {
  std::optional<uint64_t> size = GetLiveProcessBinaryDataSize(kind);
  if (!size)
    return createStringError(
        inconvertibleErrorCode(),
        "Tracing data \"%s\" is not available for the process.", kind.data());

  TraceGetBinaryDataRequest request{GetPluginName().str(), kind.str(),
                                    /*tid=*/std::nullopt,
                                    /*cpu_id*/ std::nullopt};
  return GetLiveTraceBinaryData(request, *size);
}

Trace::Storage &Trace::GetUpdatedStorage() {
  RefreshLiveProcessState();
  return m_storage;
}

const char *Trace::RefreshLiveProcessState() {
  if (!m_live_process)
    return nullptr;

  uint32_t new_stop_id = m_live_process->GetStopID();
  if (new_stop_id == m_stop_id)
    return nullptr;

  Log *log = GetLog(LLDBLog::Target);
  LLDB_LOG(log, "Trace::RefreshLiveProcessState invoked");

  m_stop_id = new_stop_id;
  m_storage = Trace::Storage();

  auto do_refresh = [&]() -> Error {
    Expected<std::string> json_string = GetLiveProcessState();
    if (!json_string)
      return json_string.takeError();

    Expected<TraceGetStateResponse> live_process_state =
        json::parse<TraceGetStateResponse>(*json_string,
                                           "TraceGetStateResponse");
    if (!live_process_state)
      return live_process_state.takeError();

    if (live_process_state->warnings) {
      for (std::string &warning : *live_process_state->warnings)
        LLDB_LOG(log, "== Warning when fetching the trace state: {0}", warning);
    }

    for (const TraceThreadState &thread_state :
         live_process_state->traced_threads) {
      for (const TraceBinaryData &item : thread_state.binary_data)
        m_storage.live_thread_data[thread_state.tid].insert(
            {ConstString(item.kind), item.size});
    }

    LLDB_LOG(log, "== Found {0} threads being traced",
             live_process_state->traced_threads.size());

    if (live_process_state->cpus) {
      m_storage.cpus.emplace();
      for (const TraceCpuState &cpu_state : *live_process_state->cpus) {
        m_storage.cpus->push_back(cpu_state.id);
        for (const TraceBinaryData &item : cpu_state.binary_data)
          m_storage.live_cpu_data_sizes[cpu_state.id].insert(
              {ConstString(item.kind), item.size});
      }
      LLDB_LOG(log, "== Found {0} cpu cpus being traced",
               live_process_state->cpus->size());
    }

    for (const TraceBinaryData &item : live_process_state->process_binary_data)
      m_storage.live_process_data.insert({ConstString(item.kind), item.size});

    return DoRefreshLiveProcessState(std::move(*live_process_state),
                                     *json_string);
  };

  if (Error err = do_refresh()) {
    m_storage.live_refresh_error = toString(std::move(err));
    return m_storage.live_refresh_error->c_str();
  }

  return nullptr;
}

Trace::Trace(ArrayRef<ProcessSP> postmortem_processes,
             std::optional<std::vector<lldb::cpu_id_t>> postmortem_cpus) {
  for (ProcessSP process_sp : postmortem_processes)
    m_storage.postmortem_processes.push_back(process_sp.get());
  m_storage.cpus = postmortem_cpus;
}

Process *Trace::GetLiveProcess() { return m_live_process; }

ArrayRef<Process *> Trace::GetPostMortemProcesses() {
  return m_storage.postmortem_processes;
}

std::vector<Process *> Trace::GetAllProcesses() {
  if (Process *proc = GetLiveProcess())
    return {proc};
  return GetPostMortemProcesses();
}

uint32_t Trace::GetStopID() {
  RefreshLiveProcessState();
  return m_stop_id;
}

llvm::Expected<FileSpec>
Trace::GetPostMortemThreadDataFile(lldb::tid_t tid, llvm::StringRef kind) {
  Storage &storage = GetUpdatedStorage();
  if (std::optional<FileSpec> file =
          Lookup(storage.postmortem_thread_data, tid, ConstString(kind)))
    return *file;
  else
    return createStringError(
        inconvertibleErrorCode(),
        formatv("The thread with tid={0} doesn't have the tracing data {1}",
                tid, kind));
}

llvm::Expected<FileSpec> Trace::GetPostMortemCpuDataFile(lldb::cpu_id_t cpu_id,
                                                         llvm::StringRef kind) {
  Storage &storage = GetUpdatedStorage();
  if (std::optional<FileSpec> file =
          Lookup(storage.postmortem_cpu_data, cpu_id, ConstString(kind)))
    return *file;
  else
    return createStringError(
        inconvertibleErrorCode(),
        formatv("The cpu with id={0} doesn't have the tracing data {1}", cpu_id,
                kind));
}

void Trace::SetPostMortemThreadDataFile(lldb::tid_t tid, llvm::StringRef kind,
                                        FileSpec file_spec) {
  Storage &storage = GetUpdatedStorage();
  storage.postmortem_thread_data[tid].insert({ConstString(kind), file_spec});
}

void Trace::SetPostMortemCpuDataFile(lldb::cpu_id_t cpu_id,
                                     llvm::StringRef kind, FileSpec file_spec) {
  Storage &storage = GetUpdatedStorage();
  storage.postmortem_cpu_data[cpu_id].insert({ConstString(kind), file_spec});
}

llvm::Error
Trace::OnLiveThreadBinaryDataRead(lldb::tid_t tid, llvm::StringRef kind,
                                  OnBinaryDataReadCallback callback) {
  Expected<std::vector<uint8_t>> data = GetLiveThreadBinaryData(tid, kind);
  if (!data)
    return data.takeError();
  return callback(*data);
}

llvm::Error Trace::OnLiveCpuBinaryDataRead(lldb::cpu_id_t cpu_id,
                                           llvm::StringRef kind,
                                           OnBinaryDataReadCallback callback) {
  Storage &storage = GetUpdatedStorage();
  if (std::vector<uint8_t> *cpu_data =
          LookupAsPtr(storage.live_cpu_data, cpu_id, ConstString(kind)))
    return callback(*cpu_data);

  Expected<std::vector<uint8_t>> data = GetLiveCpuBinaryData(cpu_id, kind);
  if (!data)
    return data.takeError();
  auto it = storage.live_cpu_data[cpu_id].insert(
      {ConstString(kind), std::move(*data)});
  return callback(it.first->second);
}

llvm::Error Trace::OnDataFileRead(FileSpec file,
                                  OnBinaryDataReadCallback callback) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> trace_or_error =
      MemoryBuffer::getFile(file.GetPath());
  if (std::error_code err = trace_or_error.getError())
    return createStringError(
        inconvertibleErrorCode(), "Failed fetching trace-related file %s. %s",
        file.GetPath().c_str(), toString(errorCodeToError(err)).c_str());

  MemoryBuffer &data = **trace_or_error;
  ArrayRef<uint8_t> array_ref(
      reinterpret_cast<const uint8_t *>(data.getBufferStart()),
      data.getBufferSize());
  return callback(array_ref);
}

llvm::Error
Trace::OnPostMortemThreadBinaryDataRead(lldb::tid_t tid, llvm::StringRef kind,
                                        OnBinaryDataReadCallback callback) {
  if (Expected<FileSpec> file = GetPostMortemThreadDataFile(tid, kind))
    return OnDataFileRead(*file, callback);
  else
    return file.takeError();
}

llvm::Error
Trace::OnPostMortemCpuBinaryDataRead(lldb::cpu_id_t cpu_id,
                                     llvm::StringRef kind,
                                     OnBinaryDataReadCallback callback) {
  if (Expected<FileSpec> file = GetPostMortemCpuDataFile(cpu_id, kind))
    return OnDataFileRead(*file, callback);
  else
    return file.takeError();
}

llvm::Error Trace::OnThreadBinaryDataRead(lldb::tid_t tid, llvm::StringRef kind,
                                          OnBinaryDataReadCallback callback) {
  if (m_live_process)
    return OnLiveThreadBinaryDataRead(tid, kind, callback);
  else
    return OnPostMortemThreadBinaryDataRead(tid, kind, callback);
}

llvm::Error
Trace::OnAllCpusBinaryDataRead(llvm::StringRef kind,
                               OnCpusBinaryDataReadCallback callback) {
  DenseMap<cpu_id_t, ArrayRef<uint8_t>> buffers;
  Storage &storage = GetUpdatedStorage();
  if (!storage.cpus)
    return Error::success();

  std::function<Error(std::vector<cpu_id_t>::iterator)> process_cpu =
      [&](std::vector<cpu_id_t>::iterator cpu_id) -> Error {
    if (cpu_id == storage.cpus->end())
      return callback(buffers);

    return OnCpuBinaryDataRead(*cpu_id, kind,
                               [&](ArrayRef<uint8_t> data) -> Error {
                                 buffers.try_emplace(*cpu_id, data);
                                 auto next_id = cpu_id;
                                 next_id++;
                                 return process_cpu(next_id);
                               });
  };
  return process_cpu(storage.cpus->begin());
}

llvm::Error Trace::OnCpuBinaryDataRead(lldb::cpu_id_t cpu_id,
                                       llvm::StringRef kind,
                                       OnBinaryDataReadCallback callback) {
  if (m_live_process)
    return OnLiveCpuBinaryDataRead(cpu_id, kind, callback);
  else
    return OnPostMortemCpuBinaryDataRead(cpu_id, kind, callback);
}

ArrayRef<lldb::cpu_id_t> Trace::GetTracedCpus() {
  Storage &storage = GetUpdatedStorage();
  if (storage.cpus)
    return *storage.cpus;
  return {};
}

std::vector<Process *> Trace::GetTracedProcesses() {
  std::vector<Process *> processes;
  Storage &storage = GetUpdatedStorage();

  for (Process *proc : storage.postmortem_processes)
    processes.push_back(proc);

  if (m_live_process)
    processes.push_back(m_live_process);
  return processes;
}
