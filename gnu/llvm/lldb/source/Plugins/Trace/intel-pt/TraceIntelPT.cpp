//===-- TraceIntelPT.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TraceIntelPT.h"

#include "../common/ThreadPostMortemTrace.h"
#include "CommandObjectTraceStartIntelPT.h"
#include "DecodedThread.h"
#include "TraceCursorIntelPT.h"
#include "TraceIntelPTBundleLoader.h"
#include "TraceIntelPTBundleSaver.h"
#include "TraceIntelPTConstants.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

LLDB_PLUGIN_DEFINE(TraceIntelPT)

lldb::CommandObjectSP
TraceIntelPT::GetProcessTraceStartCommand(CommandInterpreter &interpreter) {
  return CommandObjectSP(
      new CommandObjectProcessTraceStartIntelPT(*this, interpreter));
}

lldb::CommandObjectSP
TraceIntelPT::GetThreadTraceStartCommand(CommandInterpreter &interpreter) {
  return CommandObjectSP(
      new CommandObjectThreadTraceStartIntelPT(*this, interpreter));
}

#define LLDB_PROPERTIES_traceintelpt
#include "TraceIntelPTProperties.inc"

enum {
#define LLDB_PROPERTIES_traceintelpt
#include "TraceIntelPTPropertiesEnum.inc"
};

llvm::StringRef TraceIntelPT::PluginProperties::GetSettingName() {
  return TraceIntelPT::GetPluginNameStatic();
}

TraceIntelPT::PluginProperties::PluginProperties() : Properties() {
  m_collection_sp = std::make_shared<OptionValueProperties>(GetSettingName());
  m_collection_sp->Initialize(g_traceintelpt_properties);
}

uint64_t
TraceIntelPT::PluginProperties::GetInfiniteDecodingLoopVerificationThreshold() {
  const uint32_t idx = ePropertyInfiniteDecodingLoopVerificationThreshold;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_traceintelpt_properties[idx].default_uint_value);
}

uint64_t TraceIntelPT::PluginProperties::GetExtremelyLargeDecodingThreshold() {
  const uint32_t idx = ePropertyExtremelyLargeDecodingThreshold;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_traceintelpt_properties[idx].default_uint_value);
}

TraceIntelPT::PluginProperties &TraceIntelPT::GetGlobalProperties() {
  static TraceIntelPT::PluginProperties g_settings;
  return g_settings;
}

void TraceIntelPT::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "Intel Processor Trace",
      CreateInstanceForTraceBundle, CreateInstanceForLiveProcess,
      TraceIntelPTBundleLoader::GetSchema(), DebuggerInitialize);
}

void TraceIntelPT::DebuggerInitialize(Debugger &debugger) {
  if (!PluginManager::GetSettingForProcessPlugin(
          debugger, PluginProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForTracePlugin(
        debugger, GetGlobalProperties().GetValueProperties(),
        "Properties for the intel-pt trace plug-in.", is_global_setting);
  }
}

void TraceIntelPT::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstanceForTraceBundle);
}

StringRef TraceIntelPT::GetSchema() {
  return TraceIntelPTBundleLoader::GetSchema();
}

void TraceIntelPT::Dump(Stream *s) const {}

Expected<FileSpec> TraceIntelPT::SaveToDisk(FileSpec directory, bool compact) {
  RefreshLiveProcessState();
  return TraceIntelPTBundleSaver().SaveToDisk(*this, directory, compact);
}

Expected<TraceSP> TraceIntelPT::CreateInstanceForTraceBundle(
    const json::Value &bundle_description, StringRef bundle_dir,
    Debugger &debugger) {
  return TraceIntelPTBundleLoader(debugger, bundle_description, bundle_dir)
      .Load();
}

Expected<TraceSP> TraceIntelPT::CreateInstanceForLiveProcess(Process &process) {
  TraceSP instance(new TraceIntelPT(process));
  process.GetTarget().SetTrace(instance);
  return instance;
}

TraceIntelPTSP TraceIntelPT::GetSharedPtr() {
  return std::static_pointer_cast<TraceIntelPT>(shared_from_this());
}

TraceIntelPT::TraceMode TraceIntelPT::GetTraceMode() { return trace_mode; }

TraceIntelPTSP TraceIntelPT::CreateInstanceForPostmortemTrace(
    JSONTraceBundleDescription &bundle_description,
    ArrayRef<ProcessSP> traced_processes,
    ArrayRef<ThreadPostMortemTraceSP> traced_threads, TraceMode trace_mode) {
  TraceIntelPTSP trace_sp(
      new TraceIntelPT(bundle_description, traced_processes, trace_mode));
  trace_sp->m_storage.tsc_conversion =
      bundle_description.tsc_perf_zero_conversion;

  if (bundle_description.cpus) {
    std::vector<cpu_id_t> cpus;

    for (const JSONCpu &cpu : *bundle_description.cpus) {
      trace_sp->SetPostMortemCpuDataFile(cpu.id, IntelPTDataKinds::kIptTrace,
                                         FileSpec(cpu.ipt_trace));

      trace_sp->SetPostMortemCpuDataFile(
          cpu.id, IntelPTDataKinds::kPerfContextSwitchTrace,
          FileSpec(cpu.context_switch_trace));
      cpus.push_back(cpu.id);
    }

    if (trace_mode == TraceMode::UserMode) {
      trace_sp->m_storage.multicpu_decoder.emplace(trace_sp);
    }
  }

  if (!bundle_description.cpus || trace_mode == TraceMode::KernelMode) {
    for (const ThreadPostMortemTraceSP &thread : traced_threads) {
      trace_sp->m_storage.thread_decoders.try_emplace(
          thread->GetID(), std::make_unique<ThreadDecoder>(thread, *trace_sp));
      if (const std::optional<FileSpec> &trace_file = thread->GetTraceFile()) {
        trace_sp->SetPostMortemThreadDataFile(
            thread->GetID(), IntelPTDataKinds::kIptTrace, *trace_file);
      }
    }
  }

  for (const ProcessSP &process_sp : traced_processes)
    process_sp->GetTarget().SetTrace(trace_sp);
  return trace_sp;
}

TraceIntelPT::TraceIntelPT(JSONTraceBundleDescription &bundle_description,
                           ArrayRef<ProcessSP> traced_processes,
                           TraceMode trace_mode)
    : Trace(traced_processes, bundle_description.GetCpuIds()),
      m_cpu_info(bundle_description.cpu_info), trace_mode(trace_mode) {}

Expected<DecodedThreadSP> TraceIntelPT::Decode(Thread &thread) {
  if (const char *error = RefreshLiveProcessState())
    return createStringError(inconvertibleErrorCode(), error);

  Storage &storage = GetUpdatedStorage();
  if (storage.multicpu_decoder)
    return storage.multicpu_decoder->Decode(thread);

  auto it = storage.thread_decoders.find(thread.GetID());
  if (it == storage.thread_decoders.end())
    return createStringError(inconvertibleErrorCode(), "thread not traced");
  return it->second->Decode();
}

Expected<std::optional<uint64_t>> TraceIntelPT::FindBeginningOfTimeNanos() {
  Storage &storage = GetUpdatedStorage();
  if (storage.beginning_of_time_nanos_calculated)
    return storage.beginning_of_time_nanos;
  storage.beginning_of_time_nanos_calculated = true;

  if (!storage.tsc_conversion)
    return std::nullopt;

  std::optional<uint64_t> lowest_tsc;

  if (storage.multicpu_decoder) {
    if (Expected<std::optional<uint64_t>> tsc =
            storage.multicpu_decoder->FindLowestTSC()) {
      lowest_tsc = *tsc;
    } else {
      return tsc.takeError();
    }
  }

  for (auto &decoder : storage.thread_decoders) {
    Expected<std::optional<uint64_t>> tsc = decoder.second->FindLowestTSC();
    if (!tsc)
      return tsc.takeError();

    if (*tsc && (!lowest_tsc || *lowest_tsc > **tsc))
      lowest_tsc = **tsc;
  }

  if (lowest_tsc) {
    storage.beginning_of_time_nanos =
        storage.tsc_conversion->ToNanos(*lowest_tsc);
  }
  return storage.beginning_of_time_nanos;
}

llvm::Expected<lldb::TraceCursorSP>
TraceIntelPT::CreateNewCursor(Thread &thread) {
  if (Expected<DecodedThreadSP> decoded_thread = Decode(thread)) {
    if (Expected<std::optional<uint64_t>> beginning_of_time =
            FindBeginningOfTimeNanos())
      return std::make_shared<TraceCursorIntelPT>(
          thread.shared_from_this(), *decoded_thread, m_storage.tsc_conversion,
          *beginning_of_time);
    else
      return beginning_of_time.takeError();
  } else
    return decoded_thread.takeError();
}

void TraceIntelPT::DumpTraceInfo(Thread &thread, Stream &s, bool verbose,
                                 bool json) {
  Storage &storage = GetUpdatedStorage();

  lldb::tid_t tid = thread.GetID();
  if (json) {
    DumpTraceInfoAsJson(thread, s, verbose);
    return;
  }

  s.Format("\nthread #{0}: tid = {1}", thread.GetIndexID(), thread.GetID());
  if (!IsTraced(tid)) {
    s << ", not traced\n";
    return;
  }
  s << "\n";

  Expected<DecodedThreadSP> decoded_thread_sp_or_err = Decode(thread);
  if (!decoded_thread_sp_or_err) {
    s << toString(decoded_thread_sp_or_err.takeError()) << "\n";
    return;
  }

  DecodedThreadSP &decoded_thread_sp = *decoded_thread_sp_or_err;

  Expected<std::optional<uint64_t>> raw_size_or_error = GetRawTraceSize(thread);
  if (!raw_size_or_error) {
    s.Format("  {0}\n", toString(raw_size_or_error.takeError()));
    return;
  }
  std::optional<uint64_t> raw_size = *raw_size_or_error;

  s.Format("\n  Trace technology: {0}\n", GetPluginName());

  /// Instruction stats
  {
    uint64_t items_count = decoded_thread_sp->GetItemsCount();
    uint64_t mem_used = decoded_thread_sp->CalculateApproximateMemoryUsage();

    s.Format("\n  Total number of trace items: {0}\n", items_count);

    s << "\n  Memory usage:\n";
    if (raw_size)
      s.Format("    Raw trace size: {0} KiB\n", *raw_size / 1024);

    s.Format(
        "    Total approximate memory usage (excluding raw trace): {0:2} KiB\n",
        (double)mem_used / 1024);
    if (items_count != 0)
      s.Format("    Average memory usage per item (excluding raw trace): "
               "{0:2} bytes\n",
               (double)mem_used / items_count);
  }

  // Timing
  {
    s << "\n  Timing for this thread:\n";
    auto print_duration = [&](const std::string &name,
                              std::chrono::milliseconds duration) {
      s.Format("    {0}: {1:2}s\n", name, duration.count() / 1000.0);
    };
    GetThreadTimer(tid).ForEachTimedTask(print_duration);

    s << "\n  Timing for global tasks:\n";
    GetGlobalTimer().ForEachTimedTask(print_duration);
  }

  // Instruction events stats
  {
    const DecodedThread::EventsStats &events_stats =
        decoded_thread_sp->GetEventsStats();
    s << "\n  Events:\n";
    s.Format("    Number of individual events: {0}\n",
             events_stats.total_count);
    for (const auto &event_to_count : events_stats.events_counts) {
      s.Format("      {0}: {1}\n",
               TraceCursor::EventKindToString(event_to_count.first),
               event_to_count.second);
    }
  }
  // Trace error stats
  {
    const DecodedThread::ErrorStats &error_stats =
        decoded_thread_sp->GetErrorStats();
    s << "\n  Errors:\n";
    s.Format("    Number of individual errors: {0}\n",
             error_stats.GetTotalCount());
    s.Format("      Number of fatal errors: {0}\n", error_stats.fatal_errors);
    for (const auto &[kind, count] : error_stats.libipt_errors) {
      s.Format("     Number of libipt errors of kind [{0}]: {1}\n", kind,
               count);
    }
    s.Format("      Number of other errors: {0}\n", error_stats.other_errors);
  }

  if (storage.multicpu_decoder) {
    s << "\n  Multi-cpu decoding:\n";
    s.Format("    Total number of continuous executions found: {0}\n",
             storage.multicpu_decoder->GetTotalContinuousExecutionsCount());
    s.Format(
        "    Number of continuous executions for this thread: {0}\n",
        storage.multicpu_decoder->GetNumContinuousExecutionsForThread(tid));
    s.Format("    Total number of PSB blocks found: {0}\n",
             storage.multicpu_decoder->GetTotalPSBBlocksCount());
    s.Format("    Number of PSB blocks for this thread: {0}\n",
             storage.multicpu_decoder->GePSBBlocksCountForThread(tid));
    s.Format("    Total number of unattributed PSB blocks found: {0}\n",
             storage.multicpu_decoder->GetUnattributedPSBBlocksCount());
  }
}

void TraceIntelPT::DumpTraceInfoAsJson(Thread &thread, Stream &s,
                                       bool verbose) {
  Storage &storage = GetUpdatedStorage();

  lldb::tid_t tid = thread.GetID();
  json::OStream json_str(s.AsRawOstream(), 2);
  if (!IsTraced(tid)) {
    s << "error: thread not traced\n";
    return;
  }

  Expected<std::optional<uint64_t>> raw_size_or_error = GetRawTraceSize(thread);
  if (!raw_size_or_error) {
    s << "error: " << toString(raw_size_or_error.takeError()) << "\n";
    return;
  }

  Expected<DecodedThreadSP> decoded_thread_sp_or_err = Decode(thread);
  if (!decoded_thread_sp_or_err) {
    s << "error: " << toString(decoded_thread_sp_or_err.takeError()) << "\n";
    return;
  }
  DecodedThreadSP &decoded_thread_sp = *decoded_thread_sp_or_err;

  json_str.object([&] {
    json_str.attribute("traceTechnology", "intel-pt");
    json_str.attributeObject("threadStats", [&] {
      json_str.attribute("tid", tid);

      uint64_t insn_len = decoded_thread_sp->GetItemsCount();
      json_str.attribute("traceItemsCount", insn_len);

      // Instruction stats
      uint64_t mem_used = decoded_thread_sp->CalculateApproximateMemoryUsage();
      json_str.attributeObject("memoryUsage", [&] {
        json_str.attribute("totalInBytes", std::to_string(mem_used));
        std::optional<double> avg;
        if (insn_len != 0)
          avg = double(mem_used) / insn_len;
        json_str.attribute("avgPerItemInBytes", avg);
      });

      // Timing
      json_str.attributeObject("timingInSeconds", [&] {
        GetTimer().ForThread(tid).ForEachTimedTask(
            [&](const std::string &name, std::chrono::milliseconds duration) {
              json_str.attribute(name, duration.count() / 1000.0);
            });
      });

      // Instruction events stats
      const DecodedThread::EventsStats &events_stats =
          decoded_thread_sp->GetEventsStats();
      json_str.attributeObject("events", [&] {
        json_str.attribute("totalCount", events_stats.total_count);
        json_str.attributeObject("individualCounts", [&] {
          for (const auto &event_to_count : events_stats.events_counts) {
            json_str.attribute(
                TraceCursor::EventKindToString(event_to_count.first),
                event_to_count.second);
          }
        });
      });
      // Trace error stats
      const DecodedThread::ErrorStats &error_stats =
          decoded_thread_sp->GetErrorStats();
      json_str.attributeObject("errors", [&] {
        json_str.attribute("totalCount", error_stats.GetTotalCount());
        json_str.attributeObject("libiptErrors", [&] {
          for (const auto &[kind, count] : error_stats.libipt_errors) {
            json_str.attribute(kind, count);
          }
        });
        json_str.attribute("fatalErrors", error_stats.fatal_errors);
        json_str.attribute("otherErrors", error_stats.other_errors);
      });

      if (storage.multicpu_decoder) {
        json_str.attribute(
            "continuousExecutions",
            storage.multicpu_decoder->GetNumContinuousExecutionsForThread(tid));
        json_str.attribute(
            "PSBBlocks",
            storage.multicpu_decoder->GePSBBlocksCountForThread(tid));
      }
    });

    json_str.attributeObject("globalStats", [&] {
      json_str.attributeObject("timingInSeconds", [&] {
        GetTimer().ForGlobal().ForEachTimedTask(
            [&](const std::string &name, std::chrono::milliseconds duration) {
              json_str.attribute(name, duration.count() / 1000.0);
            });
      });
      if (storage.multicpu_decoder) {
        json_str.attribute(
            "totalUnattributedPSBBlocks",
            storage.multicpu_decoder->GetUnattributedPSBBlocksCount());
        json_str.attribute(
            "totalCountinuosExecutions",
            storage.multicpu_decoder->GetTotalContinuousExecutionsCount());
        json_str.attribute("totalPSBBlocks",
                           storage.multicpu_decoder->GetTotalPSBBlocksCount());
        json_str.attribute(
            "totalContinuousExecutions",
            storage.multicpu_decoder->GetTotalContinuousExecutionsCount());
      }
    });
  });
}

llvm::Expected<std::optional<uint64_t>>
TraceIntelPT::GetRawTraceSize(Thread &thread) {
  if (GetUpdatedStorage().multicpu_decoder)
    return std::nullopt; // TODO: calculate the amount of intel pt raw trace associated
                 // with the given thread.
  if (GetLiveProcess())
    return GetLiveThreadBinaryDataSize(thread.GetID(),
                                       IntelPTDataKinds::kIptTrace);
  uint64_t size;
  auto callback = [&](llvm::ArrayRef<uint8_t> data) {
    size = data.size();
    return Error::success();
  };
  if (Error err = OnThreadBufferRead(thread.GetID(), callback))
    return std::move(err);

  return size;
}

Expected<pt_cpu> TraceIntelPT::GetCPUInfoForLiveProcess() {
  Expected<std::vector<uint8_t>> cpu_info =
      GetLiveProcessBinaryData(IntelPTDataKinds::kProcFsCpuInfo);
  if (!cpu_info)
    return cpu_info.takeError();

  int64_t cpu_family = -1;
  int64_t model = -1;
  int64_t stepping = -1;
  std::string vendor_id;

  StringRef rest(reinterpret_cast<const char *>(cpu_info->data()),
                 cpu_info->size());
  while (!rest.empty()) {
    StringRef line;
    std::tie(line, rest) = rest.split('\n');

    SmallVector<StringRef, 2> columns;
    line.split(columns, StringRef(":"), -1, false);

    if (columns.size() < 2)
      continue; // continue searching

    columns[1] = columns[1].trim(" ");
    if (columns[0].contains("cpu family") &&
        columns[1].getAsInteger(10, cpu_family))
      continue;

    else if (columns[0].contains("model") && columns[1].getAsInteger(10, model))
      continue;

    else if (columns[0].contains("stepping") &&
             columns[1].getAsInteger(10, stepping))
      continue;

    else if (columns[0].contains("vendor_id")) {
      vendor_id = columns[1].str();
      if (!vendor_id.empty())
        continue;
    }

    if ((cpu_family != -1) && (model != -1) && (stepping != -1) &&
        (!vendor_id.empty())) {
      return pt_cpu{vendor_id == "GenuineIntel" ? pcv_intel : pcv_unknown,
                    static_cast<uint16_t>(cpu_family),
                    static_cast<uint8_t>(model),
                    static_cast<uint8_t>(stepping)};
    }
  }
  return createStringError(inconvertibleErrorCode(),
                           "Failed parsing the target's /proc/cpuinfo file");
}

Expected<pt_cpu> TraceIntelPT::GetCPUInfo() {
  if (!m_cpu_info) {
    if (llvm::Expected<pt_cpu> cpu_info = GetCPUInfoForLiveProcess())
      m_cpu_info = *cpu_info;
    else
      return cpu_info.takeError();
  }
  return *m_cpu_info;
}

std::optional<LinuxPerfZeroTscConversion>
TraceIntelPT::GetPerfZeroTscConversion() {
  return GetUpdatedStorage().tsc_conversion;
}

TraceIntelPT::Storage &TraceIntelPT::GetUpdatedStorage() {
  RefreshLiveProcessState();
  return m_storage;
}

Error TraceIntelPT::DoRefreshLiveProcessState(TraceGetStateResponse state,
                                              StringRef json_response) {
  m_storage = Storage();

  Expected<TraceIntelPTGetStateResponse> intelpt_state =
      json::parse<TraceIntelPTGetStateResponse>(json_response,
                                                "TraceIntelPTGetStateResponse");
  if (!intelpt_state)
    return intelpt_state.takeError();

  m_storage.tsc_conversion = intelpt_state->tsc_perf_zero_conversion;

  if (!intelpt_state->cpus) {
    for (const TraceThreadState &thread_state : state.traced_threads) {
      ThreadSP thread_sp =
          GetLiveProcess()->GetThreadList().FindThreadByID(thread_state.tid);
      m_storage.thread_decoders.try_emplace(
          thread_state.tid, std::make_unique<ThreadDecoder>(thread_sp, *this));
    }
  } else {
    std::vector<cpu_id_t> cpus;
    for (const TraceCpuState &cpu : *intelpt_state->cpus)
      cpus.push_back(cpu.id);

    std::vector<tid_t> tids;
    for (const TraceThreadState &thread : intelpt_state->traced_threads)
      tids.push_back(thread.tid);

    if (!intelpt_state->tsc_perf_zero_conversion)
      return createStringError(inconvertibleErrorCode(),
                               "Missing perf time_zero conversion values");
    m_storage.multicpu_decoder.emplace(GetSharedPtr());
  }

  if (m_storage.tsc_conversion) {
    Log *log = GetLog(LLDBLog::Target);
    LLDB_LOG(log, "TraceIntelPT found TSC conversion information");
  }
  return Error::success();
}

bool TraceIntelPT::IsTraced(lldb::tid_t tid) {
  Storage &storage = GetUpdatedStorage();
  if (storage.multicpu_decoder)
    return storage.multicpu_decoder->TracesThread(tid);
  return storage.thread_decoders.count(tid);
}

// The information here should match the description of the intel-pt section
// of the jLLDBTraceStart packet in the lldb/docs/lldb-gdb-remote.txt
// documentation file. Similarly, it should match the CLI help messages of the
// TraceIntelPTOptions.td file.
const char *TraceIntelPT::GetStartConfigurationHelp() {
  static std::optional<std::string> message;
  if (!message) {
    message.emplace(formatv(R"(Parameters:

  See the jLLDBTraceStart section in lldb/docs/lldb-gdb-remote.txt for a
  description of each parameter below.

  - int iptTraceSize (defaults to {0} bytes):
    [process and thread tracing]

  - boolean enableTsc (default to {1}):
    [process and thread tracing]

  - int psbPeriod (defaults to {2}):
    [process and thread tracing]

  - boolean perCpuTracing (default to {3}):
    [process tracing only]

  - int processBufferSizeLimit (defaults to {4} MiB):
    [process tracing only]

  - boolean disableCgroupFiltering (default to {5}):
    [process tracing only])",
                            kDefaultIptTraceSize, kDefaultEnableTscValue,
                            kDefaultPsbPeriod, kDefaultPerCpuTracing,
                            kDefaultProcessBufferSizeLimit / 1024 / 1024,
                            kDefaultDisableCgroupFiltering));
  }
  return message->c_str();
}

Error TraceIntelPT::Start(uint64_t ipt_trace_size,
                          uint64_t total_buffer_size_limit, bool enable_tsc,
                          std::optional<uint64_t> psb_period,
                          bool per_cpu_tracing, bool disable_cgroup_filtering) {
  TraceIntelPTStartRequest request;
  request.ipt_trace_size = ipt_trace_size;
  request.process_buffer_size_limit = total_buffer_size_limit;
  request.enable_tsc = enable_tsc;
  request.psb_period = psb_period;
  request.type = GetPluginName().str();
  request.per_cpu_tracing = per_cpu_tracing;
  request.disable_cgroup_filtering = disable_cgroup_filtering;
  return Trace::Start(toJSON(request));
}

Error TraceIntelPT::Start(StructuredData::ObjectSP configuration) {
  uint64_t ipt_trace_size = kDefaultIptTraceSize;
  uint64_t process_buffer_size_limit = kDefaultProcessBufferSizeLimit;
  bool enable_tsc = kDefaultEnableTscValue;
  std::optional<uint64_t> psb_period = kDefaultPsbPeriod;
  bool per_cpu_tracing = kDefaultPerCpuTracing;
  bool disable_cgroup_filtering = kDefaultDisableCgroupFiltering;

  if (configuration) {
    if (StructuredData::Dictionary *dict = configuration->GetAsDictionary()) {
      dict->GetValueForKeyAsInteger("iptTraceSize", ipt_trace_size);
      dict->GetValueForKeyAsInteger("processBufferSizeLimit",
                                    process_buffer_size_limit);
      dict->GetValueForKeyAsBoolean("enableTsc", enable_tsc);
      dict->GetValueForKeyAsInteger("psbPeriod", psb_period);
      dict->GetValueForKeyAsBoolean("perCpuTracing", per_cpu_tracing);
      dict->GetValueForKeyAsBoolean("disableCgroupFiltering",
                                    disable_cgroup_filtering);
    } else {
      return createStringError(inconvertibleErrorCode(),
                               "configuration object is not a dictionary");
    }
  }

  return Start(ipt_trace_size, process_buffer_size_limit, enable_tsc,
               psb_period, per_cpu_tracing, disable_cgroup_filtering);
}

llvm::Error TraceIntelPT::Start(llvm::ArrayRef<lldb::tid_t> tids,
                                uint64_t ipt_trace_size, bool enable_tsc,
                                std::optional<uint64_t> psb_period) {
  TraceIntelPTStartRequest request;
  request.ipt_trace_size = ipt_trace_size;
  request.enable_tsc = enable_tsc;
  request.psb_period = psb_period;
  request.type = GetPluginName().str();
  request.tids.emplace();
  for (lldb::tid_t tid : tids)
    request.tids->push_back(tid);
  return Trace::Start(toJSON(request));
}

Error TraceIntelPT::Start(llvm::ArrayRef<lldb::tid_t> tids,
                          StructuredData::ObjectSP configuration) {
  uint64_t ipt_trace_size = kDefaultIptTraceSize;
  bool enable_tsc = kDefaultEnableTscValue;
  std::optional<uint64_t> psb_period = kDefaultPsbPeriod;

  if (configuration) {
    if (StructuredData::Dictionary *dict = configuration->GetAsDictionary()) {
      llvm::StringRef ipt_trace_size_not_parsed;
      if (dict->GetValueForKeyAsString("iptTraceSize",
                                       ipt_trace_size_not_parsed)) {
        if (std::optional<uint64_t> bytes =
                ParsingUtils::ParseUserFriendlySizeExpression(
                    ipt_trace_size_not_parsed))
          ipt_trace_size = *bytes;
        else
          return createStringError(inconvertibleErrorCode(),
                                   "iptTraceSize is wrong bytes expression");
      } else {
        dict->GetValueForKeyAsInteger("iptTraceSize", ipt_trace_size);
      }

      dict->GetValueForKeyAsBoolean("enableTsc", enable_tsc);
      dict->GetValueForKeyAsInteger("psbPeriod", psb_period);
    } else {
      return createStringError(inconvertibleErrorCode(),
                               "configuration object is not a dictionary");
    }
  }

  return Start(tids, ipt_trace_size, enable_tsc, psb_period);
}

Error TraceIntelPT::OnThreadBufferRead(lldb::tid_t tid,
                                       OnBinaryDataReadCallback callback) {
  return OnThreadBinaryDataRead(tid, IntelPTDataKinds::kIptTrace, callback);
}

TaskTimer &TraceIntelPT::GetTimer() { return GetUpdatedStorage().task_timer; }

ScopedTaskTimer &TraceIntelPT::GetThreadTimer(lldb::tid_t tid) {
  return GetTimer().ForThread(tid);
}

ScopedTaskTimer &TraceIntelPT::GetGlobalTimer() {
  return GetTimer().ForGlobal();
}
