//===-- TraceIntelPT.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPT_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPT_H

#include "TaskTimer.h"
#include "ThreadDecoder.h"
#include "TraceIntelPTBundleLoader.h"
#include "TraceIntelPTMultiCpuDecoder.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

namespace lldb_private {
namespace trace_intel_pt {

class TraceIntelPT : public Trace {
public:
  /// Properties to be used with the `settings` command.
  class PluginProperties : public Properties {
  public:
    static llvm::StringRef GetSettingName();

    PluginProperties();

    ~PluginProperties() override = default;

    uint64_t GetInfiniteDecodingLoopVerificationThreshold();

    uint64_t GetExtremelyLargeDecodingThreshold();
  };

  /// Return the global properties for this trace plug-in.
  static PluginProperties &GetGlobalProperties();

  void Dump(Stream *s) const override;

  llvm::Expected<FileSpec> SaveToDisk(FileSpec directory,
                                      bool compact) override;

  ~TraceIntelPT() override = default;

  /// PluginInterface protocol
  /// \{
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  static void Initialize();

  static void Terminate();

  /// Create an instance of this class from a trace bundle.
  ///
  /// \param[in] trace_bundle_description
  ///     The description of the trace bundle. See \a Trace::FindPlugin.
  ///
  /// \param[in] bundle_dir
  ///     The path to the directory that contains the trace bundle.
  ///
  /// \param[in] debugger
  ///     The debugger instance where new Targets will be created as part of the
  ///     JSON data parsing.
  ///
  /// \return
  ///     A trace instance or an error in case of failures.
  static llvm::Expected<lldb::TraceSP> CreateInstanceForTraceBundle(
      const llvm::json::Value &trace_bundle_description,
      llvm::StringRef bundle_dir, Debugger &debugger);

  static llvm::Expected<lldb::TraceSP>
  CreateInstanceForLiveProcess(Process &process);

  static llvm::StringRef GetPluginNameStatic() { return "intel-pt"; }

  static void DebuggerInitialize(Debugger &debugger);
  /// \}

  lldb::CommandObjectSP
  GetProcessTraceStartCommand(CommandInterpreter &interpreter) override;

  lldb::CommandObjectSP
  GetThreadTraceStartCommand(CommandInterpreter &interpreter) override;

  llvm::StringRef GetSchema() override;

  llvm::Expected<lldb::TraceCursorSP> CreateNewCursor(Thread &thread) override;

  void DumpTraceInfo(Thread &thread, Stream &s, bool verbose,
                     bool json) override;

  llvm::Expected<std::optional<uint64_t>> GetRawTraceSize(Thread &thread);

  llvm::Error DoRefreshLiveProcessState(TraceGetStateResponse state,
                                        llvm::StringRef json_response) override;

  bool IsTraced(lldb::tid_t tid) override;

  const char *GetStartConfigurationHelp() override;

  /// Start tracing a live process.
  ///
  /// More information on the parameters below can be found in the
  /// jLLDBTraceStart section in lldb/docs/lldb-gdb-remote.txt.
  ///
  /// \param[in] ipt_trace_size
  ///     Trace size per thread in bytes.
  ///
  /// \param[in] total_buffer_size_limit
  ///     Maximum total trace size per process in bytes.
  ///
  /// \param[in] enable_tsc
  ///     Whether to use enable TSC timestamps or not.
  ///
  /// \param[in] psb_period
  ///     This value defines the period in which PSB packets will be generated.
  ///
  /// \param[in] per_cpu_tracing
  ///     This value defines whether to have an intel pt trace buffer per thread
  ///     or per cpu core.
  ///
  /// \param[in] disable_cgroup_filtering
  ///     Disable the cgroup filtering that is automatically applied when doing
  ///     per cpu tracing.
  ///
  /// \return
  ///     \a llvm::Error::success if the operation was successful, or
  ///     \a llvm::Error otherwise.
  llvm::Error Start(uint64_t ipt_trace_size, uint64_t total_buffer_size_limit,
                    bool enable_tsc, std::optional<uint64_t> psb_period,
                    bool m_per_cpu_tracing, bool disable_cgroup_filtering);

  /// \copydoc Trace::Start
  llvm::Error Start(StructuredData::ObjectSP configuration =
                        StructuredData::ObjectSP()) override;

  /// Start tracing live threads.
  ///
  /// More information on the parameters below can be found in the
  /// jLLDBTraceStart section in lldb/docs/lldb-gdb-remote.txt.
  ///
  /// \param[in] tids
  ///     Threads to trace.
  ///
  /// \param[in] ipt_trace_size
  ///     Trace size per thread or per cpu core in bytes.
  ///
  /// \param[in] enable_tsc
  ///     Whether to use enable TSC timestamps or not.
  ///
  /// \param[in] psb_period
  ///     This value defines the period in which PSB packets will be generated.
  ///
  /// \return
  ///     \a llvm::Error::success if the operation was successful, or
  ///     \a llvm::Error otherwise.
  llvm::Error Start(llvm::ArrayRef<lldb::tid_t> tids, uint64_t ipt_trace_size,
                    bool enable_tsc, std::optional<uint64_t> psb_period);

  /// \copydoc Trace::Start
  llvm::Error Start(llvm::ArrayRef<lldb::tid_t> tids,
                    StructuredData::ObjectSP configuration =
                        StructuredData::ObjectSP()) override;

  /// See \a Trace::OnThreadBinaryDataRead().
  llvm::Error OnThreadBufferRead(lldb::tid_t tid,
                                 OnBinaryDataReadCallback callback);

  /// Get or fetch the cpu information from, for example, /proc/cpuinfo.
  llvm::Expected<pt_cpu> GetCPUInfo();

  /// Get or fetch the values used to convert to and from TSCs and nanos.
  std::optional<LinuxPerfZeroTscConversion> GetPerfZeroTscConversion();

  /// \return
  ///     The timer object for this trace.
  TaskTimer &GetTimer();

  /// \return
  ///     The ScopedTaskTimer object for the given thread in this trace.
  ScopedTaskTimer &GetThreadTimer(lldb::tid_t tid);

  /// \return
  ///     The global copedTaskTimer object for this trace.
  ScopedTaskTimer &GetGlobalTimer();

  TraceIntelPTSP GetSharedPtr();

  enum class TraceMode { UserMode, KernelMode };

  TraceMode GetTraceMode();

private:
  friend class TraceIntelPTBundleLoader;

  llvm::Expected<pt_cpu> GetCPUInfoForLiveProcess();

  /// Postmortem trace constructor
  ///
  /// \param[in] bundle_description
  ///     The definition file for the postmortem bundle.
  ///
  /// \param[in] traced_processes
  ///     The processes traced in the postmortem session.
  ///
  /// \param[in] trace_threads
  ///     The threads traced in the postmortem session. They must belong to the
  ///     processes mentioned above.
  ///
  /// \param[in] trace_mode
  ///     The tracing mode of the postmortem session.
  ///
  /// \return
  ///     A TraceIntelPT shared pointer instance.
  /// \{
  static TraceIntelPTSP CreateInstanceForPostmortemTrace(
      JSONTraceBundleDescription &bundle_description,
      llvm::ArrayRef<lldb::ProcessSP> traced_processes,
      llvm::ArrayRef<lldb::ThreadPostMortemTraceSP> traced_threads,
      TraceMode trace_mode);

  /// This constructor is used by CreateInstanceForPostmortemTrace to get the
  /// instance ready before using shared pointers, which is a limitation of C++.
  TraceIntelPT(JSONTraceBundleDescription &bundle_description,
               llvm::ArrayRef<lldb::ProcessSP> traced_processes,
               TraceMode trace_mode);
  /// \}

  /// Constructor for live processes
  TraceIntelPT(Process &live_process)
      : Trace(live_process), trace_mode(TraceMode::UserMode){};

  /// Decode the trace of the given thread that, i.e. recontruct the traced
  /// instructions.
  ///
  /// \param[in] thread
  ///     If \a thread is a \a ThreadTrace, then its internal trace file will be
  ///     decoded. Live threads are not currently supported.
  ///
  /// \return
  ///     A \a DecodedThread shared pointer with the decoded instructions. Any
  ///     errors are embedded in the instruction list. An \a llvm::Error is
  ///     returned if the decoder couldn't be properly set up.
  llvm::Expected<DecodedThreadSP> Decode(Thread &thread);

  /// \return
  ///     The lowest timestamp in nanoseconds in all traces if available, \a
  ///     std::nullopt if all the traces were empty or no trace contained no
  ///     timing information, or an \a llvm::Error if it was not possible to set
  ///     up the decoder for some trace.
  llvm::Expected<std::optional<uint64_t>> FindBeginningOfTimeNanos();

  // Dump out trace info in JSON format
  void DumpTraceInfoAsJson(Thread &thread, Stream &s, bool verbose);

  /// We package all the data that can change upon process stops to make sure
  /// this contract is very visible.
  /// This variable should only be accessed directly by constructores or live
  /// process data refreshers.
  struct Storage {
    std::optional<TraceIntelPTMultiCpuDecoder> multicpu_decoder;
    /// These decoders are used for the non-per-cpu case
    llvm::DenseMap<lldb::tid_t, std::unique_ptr<ThreadDecoder>> thread_decoders;
    /// Helper variable used to track long running operations for telemetry.
    TaskTimer task_timer;
    /// It is provided by either a trace bundle or a live process to convert TSC
    /// counters to and from nanos. It might not be available on all hosts.
    std::optional<LinuxPerfZeroTscConversion> tsc_conversion;
    std::optional<uint64_t> beginning_of_time_nanos;
    bool beginning_of_time_nanos_calculated = false;
  } m_storage;

  /// It is provided by either a trace bundle or a live process' "cpuInfo"
  /// binary data. We don't put it in the Storage because this variable doesn't
  /// change.
  std::optional<pt_cpu> m_cpu_info;

  /// Get the storage after refreshing the data in the case of a live process.
  Storage &GetUpdatedStorage();

  /// The tracing mode of post mortem trace.
  TraceMode trace_mode;
};

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPT_H
