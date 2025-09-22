//===-- TraceIntelPTBundleSaver.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TraceIntelPTBundleSaver.h"
#include "PerfContextSwitchDecoder.h"
#include "TraceIntelPT.h"
#include "TraceIntelPTConstants.h"
#include "TraceIntelPTJSONStructs.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

/// Strip the \p directory component from the given \p path. It assumes that \p
/// directory is a prefix of \p path.
static std::string GetRelativePath(const FileSpec &directory,
                                   const FileSpec &path) {
  return path.GetPath().substr(directory.GetPath().size() + 1);
}

/// Write a stream of bytes from \p data to the given output file.
/// It creates or overwrites the output file, but not append.
static llvm::Error WriteBytesToDisk(FileSpec &output_file,
                                    ArrayRef<uint8_t> data) {
  std::basic_fstream<char> out_fs = std::fstream(
      output_file.GetPath().c_str(), std::ios::out | std::ios::binary);
  if (!data.empty())
    out_fs.write(reinterpret_cast<const char *>(&data[0]), data.size());

  out_fs.close();
  if (!out_fs)
    return createStringError(inconvertibleErrorCode(),
                             formatv("couldn't write to the file {0}",
                                     output_file.GetPath().c_str()));
  return Error::success();
}

/// Save the trace bundle description JSON object inside the given directory
/// as a file named \a trace.json.
///
/// \param[in] trace_bundle_description
///     The trace bundle description as JSON Object.
///
/// \param[in] directory
///     The directory where the JSON file will be saved.
///
/// \return
///     A \a FileSpec pointing to the bundle description file, or an \a
///     llvm::Error otherwise.
static Expected<FileSpec>
SaveTraceBundleDescription(const llvm::json::Value &trace_bundle_description,
                           const FileSpec &directory) {
  FileSpec trace_path = directory;
  trace_path.AppendPathComponent("trace.json");
  std::ofstream os(trace_path.GetPath());
  os << formatv("{0:2}", trace_bundle_description).str();
  os.close();
  if (!os)
    return createStringError(inconvertibleErrorCode(),
                             formatv("couldn't write to the file {0}",
                                     trace_path.GetPath().c_str()));
  return trace_path;
}

/// Build the threads sub-section of the trace bundle description file.
/// Any associated binary files are created inside the given directory.
///
/// \param[in] process
///     The process being traced.
///
/// \param[in] directory
///     The directory where files will be saved when building the threads
///     section.
///
/// \return
///     The threads section or \a llvm::Error in case of failures.
static llvm::Expected<std::vector<JSONThread>>
BuildThreadsSection(Process &process, FileSpec directory) {
  std::vector<JSONThread> json_threads;
  TraceSP trace_sp = process.GetTarget().GetTrace();

  FileSpec threads_dir = directory;
  threads_dir.AppendPathComponent("threads");
  sys::fs::create_directories(threads_dir.GetPath().c_str());

  for (ThreadSP thread_sp : process.Threads()) {
    lldb::tid_t tid = thread_sp->GetID();
    if (!trace_sp->IsTraced(tid))
      continue;

    JSONThread json_thread;
    json_thread.tid = tid;

    if (trace_sp->GetTracedCpus().empty()) {
      FileSpec output_file = threads_dir;
      output_file.AppendPathComponent(std::to_string(tid) + ".intelpt_trace");
      json_thread.ipt_trace = GetRelativePath(directory, output_file);

      llvm::Error err = process.GetTarget().GetTrace()->OnThreadBinaryDataRead(
          tid, IntelPTDataKinds::kIptTrace,
          [&](llvm::ArrayRef<uint8_t> data) -> llvm::Error {
            return WriteBytesToDisk(output_file, data);
          });
      if (err)
        return std::move(err);
    }

    json_threads.push_back(std::move(json_thread));
  }
  return json_threads;
}

/// \return
///   an \a llvm::Error in case of failures, \a std::nullopt if the trace is not
///   written to disk because the trace is empty and the \p compact flag is
///   present, or the FileSpec of the trace file on disk.
static Expected<std::optional<FileSpec>>
WriteContextSwitchTrace(TraceIntelPT &trace_ipt, lldb::cpu_id_t cpu_id,
                        const FileSpec &cpus_dir, bool compact) {
  FileSpec output_context_switch_trace = cpus_dir;
  output_context_switch_trace.AppendPathComponent(std::to_string(cpu_id) +
                                                  ".perf_context_switch_trace");

  bool should_skip = false;

  Error err = trace_ipt.OnCpuBinaryDataRead(
      cpu_id, IntelPTDataKinds::kPerfContextSwitchTrace,
      [&](llvm::ArrayRef<uint8_t> data) -> llvm::Error {
        if (!compact)
          return WriteBytesToDisk(output_context_switch_trace, data);

        std::set<lldb::pid_t> pids;
        for (Process *process : trace_ipt.GetAllProcesses())
          pids.insert(process->GetID());

        Expected<std::vector<uint8_t>> compact_context_switch_trace =
            FilterProcessesFromContextSwitchTrace(data, pids);
        if (!compact_context_switch_trace)
          return compact_context_switch_trace.takeError();

        if (compact_context_switch_trace->empty()) {
          should_skip = true;
          return Error::success();
        }

        return WriteBytesToDisk(output_context_switch_trace,
                                *compact_context_switch_trace);
      });
  if (err)
    return std::move(err);

  if (should_skip)
    return std::nullopt;
  return output_context_switch_trace;
}

static Expected<FileSpec> WriteIntelPTTrace(TraceIntelPT &trace_ipt,
                                            lldb::cpu_id_t cpu_id,
                                            const FileSpec &cpus_dir) {
  FileSpec output_trace = cpus_dir;
  output_trace.AppendPathComponent(std::to_string(cpu_id) + ".intelpt_trace");

  Error err = trace_ipt.OnCpuBinaryDataRead(
      cpu_id, IntelPTDataKinds::kIptTrace,
      [&](llvm::ArrayRef<uint8_t> data) -> llvm::Error {
        return WriteBytesToDisk(output_trace, data);
      });
  if (err)
    return std::move(err);
  return output_trace;
}

static llvm::Expected<std::optional<std::vector<JSONCpu>>>
BuildCpusSection(TraceIntelPT &trace_ipt, FileSpec directory, bool compact) {
  if (trace_ipt.GetTracedCpus().empty())
    return std::nullopt;

  std::vector<JSONCpu> json_cpus;
  FileSpec cpus_dir = directory;
  cpus_dir.AppendPathComponent("cpus");
  sys::fs::create_directories(cpus_dir.GetPath().c_str());

  for (lldb::cpu_id_t cpu_id : trace_ipt.GetTracedCpus()) {
    JSONCpu json_cpu;
    json_cpu.id = cpu_id;
    Expected<std::optional<FileSpec>> context_switch_trace_path =
        WriteContextSwitchTrace(trace_ipt, cpu_id, cpus_dir, compact);
    if (!context_switch_trace_path)
      return context_switch_trace_path.takeError();
    if (!*context_switch_trace_path)
      continue;
    json_cpu.context_switch_trace =
        GetRelativePath(directory, **context_switch_trace_path);

    if (Expected<FileSpec> ipt_trace_path =
            WriteIntelPTTrace(trace_ipt, cpu_id, cpus_dir))
      json_cpu.ipt_trace = GetRelativePath(directory, *ipt_trace_path);
    else
      return ipt_trace_path.takeError();

    json_cpus.push_back(std::move(json_cpu));
  }
  return json_cpus;
}

/// Build modules sub-section of the trace bundle. The original modules
/// will be copied over to the \a <directory/modules> folder. Invalid modules
/// are skipped.
/// Copying the modules has the benefit of making these
/// directories self-contained, as the raw traces and modules are part of the
/// output directory and can be sent to another machine, where lldb can load
/// them and replicate exactly the same trace session.
///
/// \param[in] process
///     The process being traced.
///
/// \param[in] directory
///     The directory where the modules files will be saved when building
///     the modules section.
///     Example: If a module \a libbar.so exists in the path
///     \a /usr/lib/foo/libbar.so, then it will be copied to
///     \a <directory>/modules/usr/lib/foo/libbar.so.
///
/// \return
///     The modules section or \a llvm::Error in case of failures.
static llvm::Expected<std::vector<JSONModule>>
BuildModulesSection(Process &process, FileSpec directory) {
  std::vector<JSONModule> json_modules;
  ModuleList module_list = process.GetTarget().GetImages();
  for (size_t i = 0; i < module_list.GetSize(); ++i) {
    ModuleSP module_sp(module_list.GetModuleAtIndex(i));
    if (!module_sp)
      continue;
    std::string system_path = module_sp->GetPlatformFileSpec().GetPath();
    // TODO: support memory-only libraries like [vdso]
    if (!module_sp->GetFileSpec().IsAbsolute())
      continue;

    std::string file = module_sp->GetFileSpec().GetPath();
    ObjectFile *objfile = module_sp->GetObjectFile();
    if (objfile == nullptr)
      continue;

    lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
    Address base_addr(objfile->GetBaseAddress());
    if (base_addr.IsValid() &&
        !process.GetTarget().GetSectionLoadList().IsEmpty())
      load_addr = base_addr.GetLoadAddress(&process.GetTarget());

    if (load_addr == LLDB_INVALID_ADDRESS)
      continue;

    FileSpec path_to_copy_module = directory;
    path_to_copy_module.AppendPathComponent("modules");
    path_to_copy_module.AppendPathComponent(system_path);
    sys::fs::create_directories(path_to_copy_module.GetDirectory().AsCString());

    if (std::error_code ec =
            llvm::sys::fs::copy_file(file, path_to_copy_module.GetPath()))
      return createStringError(
          inconvertibleErrorCode(),
          formatv("couldn't write to the file. {0}", ec.message()));

    json_modules.push_back(
        JSONModule{system_path, GetRelativePath(directory, path_to_copy_module),
                   JSONUINT64{load_addr}, module_sp->GetUUID().GetAsString()});
  }
  return json_modules;
}

/// Build the processes section of the trace bundle description object. Besides
/// returning the processes information, this method saves to disk all modules
/// and raw traces corresponding to the traced threads of the given process.
///
/// \param[in] process
///     The process being traced.
///
/// \param[in] directory
///     The directory where files will be saved when building the processes
///     section.
///
/// \return
///     The processes section or \a llvm::Error in case of failures.
static llvm::Expected<JSONProcess>
BuildProcessSection(Process &process, const FileSpec &directory) {
  Expected<std::vector<JSONThread>> json_threads =
      BuildThreadsSection(process, directory);
  if (!json_threads)
    return json_threads.takeError();

  Expected<std::vector<JSONModule>> json_modules =
      BuildModulesSection(process, directory);
  if (!json_modules)
    return json_modules.takeError();

  return JSONProcess{
      process.GetID(),
      process.GetTarget().GetArchitecture().GetTriple().getTriple(),
      json_threads.get(), json_modules.get()};
}

/// See BuildProcessSection()
static llvm::Expected<std::vector<JSONProcess>>
BuildProcessesSection(TraceIntelPT &trace_ipt, const FileSpec &directory) {
  std::vector<JSONProcess> processes;
  for (Process *process : trace_ipt.GetAllProcesses()) {
    if (llvm::Expected<JSONProcess> json_process =
            BuildProcessSection(*process, directory))
      processes.push_back(std::move(*json_process));
    else
      return json_process.takeError();
  }
  return processes;
}

static llvm::Expected<JSONKernel>
BuildKernelSection(TraceIntelPT &trace_ipt, const FileSpec &directory) {
  JSONKernel json_kernel;
  std::vector<Process *> processes = trace_ipt.GetAllProcesses();
  Process *kernel_process = processes[0];

  assert(processes.size() == 1 && "User processeses exist in kernel mode");
  assert(kernel_process->GetID() == kDefaultKernelProcessID &&
         "Kernel process not exist");

  Expected<std::vector<JSONModule>> json_modules =
      BuildModulesSection(*kernel_process, directory);
  if (!json_modules)
    return json_modules.takeError();

  JSONModule kernel_image = json_modules.get()[0];
  return JSONKernel{kernel_image.load_address, kernel_image.system_path};
}

Expected<FileSpec> TraceIntelPTBundleSaver::SaveToDisk(TraceIntelPT &trace_ipt,
                                                       FileSpec directory,
                                                       bool compact) {
  if (std::error_code ec =
          sys::fs::create_directories(directory.GetPath().c_str()))
    return llvm::errorCodeToError(ec);

  Expected<pt_cpu> cpu_info = trace_ipt.GetCPUInfo();
  if (!cpu_info)
    return cpu_info.takeError();

  FileSystem::Instance().Resolve(directory);

  Expected<std::optional<std::vector<JSONCpu>>> json_cpus =
      BuildCpusSection(trace_ipt, directory, compact);
  if (!json_cpus)
    return json_cpus.takeError();

  std::optional<std::vector<JSONProcess>> json_processes;
  std::optional<JSONKernel> json_kernel;

  if (trace_ipt.GetTraceMode() == TraceIntelPT::TraceMode::KernelMode) {
    Expected<std::optional<JSONKernel>> exp_json_kernel =
        BuildKernelSection(trace_ipt, directory);
    if (!exp_json_kernel)
      return exp_json_kernel.takeError();
    else
      json_kernel = *exp_json_kernel;
  } else {
    Expected<std::optional<std::vector<JSONProcess>>> exp_json_processes =
        BuildProcessesSection(trace_ipt, directory);
    if (!exp_json_processes)
      return exp_json_processes.takeError();
    else
      json_processes = *exp_json_processes;
  }

  JSONTraceBundleDescription json_intel_pt_bundle_desc{
      "intel-pt",
      *cpu_info,
      json_processes,
      *json_cpus,
      trace_ipt.GetPerfZeroTscConversion(),
      json_kernel};

  return SaveTraceBundleDescription(toJSON(json_intel_pt_bundle_desc),
                                    directory);
}
